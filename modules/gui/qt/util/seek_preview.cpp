/*****************************************************************************
 * seek_preview.cpp : thumbnails for the seek bar hover (P2b)
 *****************************************************************************
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "util/seek_preview.hpp"
#include "input_manager.hpp"

#include <vlc_input.h>
#include <vlc_input_item.h>
#include <vlc_variables.h>

#include <QPointer>
#include <atomic>
#include <vector>
#include <QElapsedTimer>
#include <thread>

#define BUCKETS 250
#define CACHE_SIZE ( BUCKETS + 10 )

struct SeekPreview::Job
{
    QPointer<SeekPreview> owner;
    input_thread_t *input;
    input_item_t *item;
    int bucket;
    float time;
    unsigned w, h;
    std::vector<uint32_t> pixels;
    std::atomic<bool> done;
    std::atomic<bool> seeked { false };
    float target = 0;
    QElapsedTimer clock;
};

/* The vmem callbacks run on the hidden input's video output thread. They
 * reach the job through a pointer stored in "vmem-data" on that input. */
unsigned SeekPreview::setupCb( void **opaque, char *chroma, unsigned *width,
                               unsigned *height, unsigned *pitches,
                               unsigned *lines )
{
    Job *j = static_cast<Job *>( *opaque );
    if( *width == 0 || *height == 0 )
        return 0;
    const double scale = qMin( double( MAX_W ) / *width, double( MAX_H ) / *height );
    j->w = qMax( 2u, unsigned( *width * scale ) & ~1u );
    j->h = qMax( 2u, unsigned( *height * scale ) & ~1u );
    memcpy( chroma, "RV32", 4 );
    *width = j->w;
    *height = j->h;
    pitches[0] = j->w * 4;
    lines[0] = j->h;
    j->pixels.assign( size_t( j->w ) * j->h, 0 );
    return 1;
}

/* Position events: the first one comes from the initial demux at 0s, the
 * next ones from after the start-time seek */
static int InputEventCb( vlc_object_t *obj, const char *, vlc_value_t,
                         vlc_value_t cur, void *data )
{
    if( cur.i_int != INPUT_EVENT_POSITION )
        return VLC_SUCCESS;
    SeekPreview::Job *j = static_cast<SeekPreview::Job *>( data );
    const float t = var_GetInteger( obj, "time" ) / 1e6f;
    if( t >= qMin( 0.25f, j->target / 2 ) )
        j->seeked = true;
    return VLC_SUCCESS;
}

void *SeekPreview::lockCb( void *opaque, void **planes )
{
    Job *j = static_cast<Job *>( opaque );
    planes[0] = j->pixels.data();
    return NULL;
}

void SeekPreview::displayCb( void *opaque, void * )
{
    Job *j = static_cast<Job *>( opaque );
    /* The start-time seek is a queued control: the input demuxes once
     * before running it, so the first pictures come from 0s. Wait for the
     * seek (it flushes the decoder and the output first). */
    if( !j->seeked.load() )
        return;
    if( j->done.exchange( true ) )
        return;
    /* RV32 with the vmem masks is BGRX in memory: QImage::Format_RGB32 */
    QImage img( reinterpret_cast<const uchar *>( j->pixels.data() ), int( j->w ),
                int( j->h ), int( j->w * 4 ), QImage::Format_RGB32 );
    const QImage copy = img.copy();
    QPointer<SeekPreview> owner = j->owner;
    if( owner )
        QMetaObject::invokeMethod( owner.data(), [owner, j, copy]() {
            if( owner )
                owner->finish( j, copy );
        }, Qt::QueuedConnection );
}

SeekPreview::SeekPreview( intf_thread_t *_p_intf, QObject *parent )
    : QObject( parent ), p_intf( _p_intf ), job( NULL ),
      pendingBucket( -1 ), pendingTime( 0 ), pendingLength( 0 )
{
    host = static_cast<vlc_object_t *>( vlc_object_create( p_intf, sizeof( vlc_object_t ) ) );
    if( host )
    {
        var_Create( host, "vmem-setup", VLC_VAR_ADDRESS );
        var_SetAddress( host, "vmem-setup", (void *)setupCb );
        var_Create( host, "vmem-lock", VLC_VAR_ADDRESS );
        var_SetAddress( host, "vmem-lock", (void *)lockCb );
        var_Create( host, "vmem-display", VLC_VAR_ADDRESS );
        var_SetAddress( host, "vmem-display", (void *)displayCb );
        var_Create( host, "vmem-unlock", VLC_VAR_ADDRESS );
        var_Create( host, "vmem-cleanup", VLC_VAR_ADDRESS );
        /* No window: the video output creates one before the display
         * module, and with the Qt one taken by the playing video it falls
         * back to a real X window that flashes on screen. vmem needs none.
         * Inherited by the preview inputs only. */
        var_Create( host, "window", VLC_VAR_STRING );
        var_SetString( host, "window", "none" );
        /* "vmem-data" is set per job, on the job's input */
        var_Create( host, "vmem-data", VLC_VAR_ADDRESS );
    }

    debounce.setSingleShot( true );
    debounce.setInterval( 25 );
    connect( &debounce, &QTimer::timeout, this, &SeekPreview::startNext );

    /* a file that never delivers a picture must not block the next job */
    watchdog.setSingleShot( true );
    watchdog.setInterval( 4000 );
    connect( &watchdog, &QTimer::timeout, this, [this]() {
        if( job )
            finish( job, QImage() );
    } );

    connect( THEMIM, &MainInputManager::inputChanged, this, [this]() {
        cancel();
        dropCache();
    } );
}

SeekPreview::~SeekPreview()
{
    debounce.stop();
    if( job )
        finish( job, QImage() );
    /* the host must outlive every input still closing on a worker */
    while( closing.load() > 0 )
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    if( host )
        vlc_object_release( host );
}

/* One engine for every seek bar (docked and fullscreen): one cache and one
 * background fill. It lives with the input manager, which is destroyed
 * after the main window but while the interface still exists. */
SeekPreview *SeekPreview::shared( intf_thread_t *p_intf )
{
    static QPointer<SeekPreview> instance;
    if( !instance )
        instance = new SeekPreview( p_intf, THEMIM );
    return instance;
}

bool SeekPreview::available( intf_thread_t *p_intf )
{
    if( !var_InheritBool( p_intf, "qt-seek-preview" ) )
        return false;
    input_thread_t *p_input = THEMIM->getInput();
    if( !p_input || !THEMIM->getIM()->hasVideo() )
        return false;
    input_item_t *item = input_GetItem( p_input );
    char *u = item ? input_item_GetURI( item ) : NULL;
    const bool local = u && !strncmp( u, "file://", 7 );
    free( u );
    return local;
}

/* about 250 buckets over the file, never finer than one second */
int SeekPreview::bucketOf( float time, float length ) const
{
    const float step = qMax( 1.0f, length / float( BUCKETS ) );
    return int( time / step );
}

/* the exact bucket, or the closest decoded one within a few buckets, so the
 * card shows a neighbouring frame at once while the exact one decodes */
QImage SeekPreview::cached( float time, float length ) const
{
    const int b = bucketOf( time, length );
    for( int d = 0; d <= 4; d++ )
    {
        if( cache.contains( b - d ) )
            return cache.value( b - d );
        if( cache.contains( b + d ) )
            return cache.value( b + d );
    }
    return QImage();
}

/* Background fill: the next missing bucket, nearest to the last hover
 * first, so scrubbing around reads ready frames */
int SeekPreview::nextFillBucket() const
{
    if( fillLength <= 0 )
        return -1;
    const int count = bucketOf( fillLength, fillLength ) + 1;
    for( int d = 0; d < count; d++ )
    {
        const int a = fillCenter + d, b = fillCenter - d;
        if( a < count && !cache.contains( a ) && !failed.contains( a ) )
            return a;
        if( b >= 0 && !cache.contains( b ) && !failed.contains( b ) )
            return b;
    }
    return -1;
}

void SeekPreview::request( float time, float length )
{
    if( !host || length <= 0 )
        return;
    input_thread_t *p_input = THEMIM->getInput();
    input_item_t *item = p_input ? input_GetItem( p_input ) : NULL;
    char *u = item ? input_item_GetURI( item ) : NULL;
    if( !u )
        return;
    const QString current = qfu( u );
    free( u );
    if( current != uri )
    {
        dropCache();
        uri = current;
    }

    const int bucket = bucketOf( time, length );
    fillCenter = bucket;
    fillLength = length;
    if( cache.contains( bucket ) )
    {
        pendingBucket = -1;
        emit ready( time, cache.value( bucket ) );
        if( !job )
            debounce.start(); /* keep filling around here */
        return;
    }
    pendingBucket = bucket;
    pendingTime = time;
    pendingLength = length;
    if( !job )
        debounce.start();
}

void SeekPreview::cancel()
{
    pendingBucket = -1;
    debounce.stop();
}

void SeekPreview::dropCache()
{
    cache.clear();
    cacheOrder.clear();
    failed.clear();
    uri.clear();
    fillLength = 0;
}

void SeekPreview::startNext()
{
    if( job || uri.isEmpty() )
        return;
    if( pendingBucket < 0 )
    {
        /* nothing asked: fill the cache in the background */
        pendingBucket = nextFillBucket();
        if( pendingBucket < 0 )
            return;
        pendingTime = pendingBucket * qMax( 1.0f, fillLength / float( BUCKETS ) );
        pendingLength = fillLength;
    }

    Job *j = new Job;
    j->owner = this;
    j->bucket = pendingBucket;
    j->time = pendingTime;
    j->w = j->h = 0;
    j->done = false;
    j->input = NULL;
    pendingBucket = -1;

    /* decode the middle of the bucket */
    const float step = qMax( 1.0f, pendingLength / float( BUCKETS ) );
    const float at = qMin( ( j->bucket + 0.5f ) * step, qMax( 0.0f, pendingLength - 0.5f ) );

    j->target = at;
    /* close to the start, the first frame is the right one anyway */
    j->seeked = at < 1.0f;
    j->item = input_item_New( qtu( uri ), "seek-preview" );
    if( !j->item )
    {
        delete j;
        return;
    }
    static const char *const options[] = {
        ":vout=vmem", ":no-audio", ":no-spu", ":no-osd", ":no-video-title-show",
        ":no-sub-autodetect-file", ":input-fast-seek", ":no-stats",
        ":video-filter=", ":sub-source=",
        /* latency: no input buffering, no clock smoothing, cheap decoding */
        ":file-caching=0", ":clock-jitter=0", ":clock-synchro=0",
        ":avcodec-fast", ":avcodec-skiploopfilter=4", ":avcodec-threads=1",
        ":avcodec-hw=none",
    };
    for( const char *opt : options )
        input_item_AddOption( j->item, opt, VLC_INPUT_OPTION_TRUSTED );
    input_item_AddOption( j->item, qtu( QString( ":start-time=%1" ).arg( at, 0, 'f', 2 ) ),
                          VLC_INPUT_OPTION_TRUSTED );

    var_SetAddress( host, "vmem-data", j );
    j->input = input_Create( host, j->item, "seek preview", NULL, NULL );
    if( j->input )
        var_AddCallback( j->input, "intf-event", InputEventCb, j );
    if( !j->input || input_Start( j->input ) )
    {
        if( j->input )
        {
            var_DelCallback( j->input, "intf-event", InputEventCb, j );
            input_Close( j->input );
        }
        input_item_Release( j->item );
        delete j;
        return;
    }
    job = j;
    j->clock.start();
    watchdog.start();
}

void SeekPreview::finish( Job *j, const QImage &img )
{
    if( j != job )
        return; /* already closed */
    job = NULL;
    watchdog.stop();

    msg_Dbg( p_intf, "seek preview: bucket %d %s in %lld ms", j->bucket,
             img.isNull() ? "failed" : "decoded", (long long)j->clock.elapsed() );

    /* Closing joins the input and its video output (about 40ms for 4K):
     * do it on a worker so the UI never waits. After the join no callback
     * can touch the job any more. */
    input_thread_t *input = j->input;
    input_item_t *item = j->item;
    const int bucket = j->bucket;
    const float t = j->time;
    closing++;
    std::thread( [input, item, j, this]() {
        var_DelCallback( input, "intf-event", InputEventCb, j );
        input_Stop( input );
        input_Close( input );
        input_item_Release( item );
        delete j;
        closing--;
    } ).detach();

    if( !img.isNull() )
    {
        /* 24-bit keeps a full cache of 250 frames around 27 MB */
        cache.insert( bucket, img.convertToFormat( QImage::Format_RGB888 ) );
        cacheOrder.append( bucket );
        while( cacheOrder.size() > CACHE_SIZE )
            cache.remove( cacheOrder.takeFirst() );
    }
    else
        failed.insert( bucket );
    if( !img.isNull() )
        emit ready( t, img );

    if( pendingBucket >= 0 && cache.contains( pendingBucket ) )
    {
        emit ready( pendingTime, cache.value( pendingBucket ) );
        pendingBucket = -1;
    }
    /* the hovered bucket first, then keep filling */
    debounce.start( 0 );
}
