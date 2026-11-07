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

#define CACHE_SIZE 80

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

void *SeekPreview::lockCb( void *opaque, void **planes )
{
    Job *j = static_cast<Job *>( opaque );
    planes[0] = j->pixels.data();
    return NULL;
}

void SeekPreview::displayCb( void *opaque, void * )
{
    Job *j = static_cast<Job *>( opaque );
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
        /* "vmem-data" is set per job, on the job's input */
        var_Create( host, "vmem-data", VLC_VAR_ADDRESS );
    }

    debounce.setSingleShot( true );
    debounce.setInterval( 90 );
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
    if( host )
        vlc_object_release( host );
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
    const float step = qMax( 1.0f, length / 250.0f );
    return int( time / step );
}

QImage SeekPreview::cached( float time, float length ) const
{
    return cache.value( bucketOf( time, length ) );
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
    if( cache.contains( bucket ) )
    {
        pendingBucket = -1;
        emit ready( time, cache.value( bucket ) );
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
    uri.clear();
}

void SeekPreview::startNext()
{
    if( job || pendingBucket < 0 || uri.isEmpty() )
        return;

    Job *j = new Job;
    j->owner = this;
    j->bucket = pendingBucket;
    j->time = pendingTime;
    j->w = j->h = 0;
    j->done = false;
    j->input = NULL;
    pendingBucket = -1;

    /* decode the middle of the bucket */
    const float step = qMax( 1.0f, pendingLength / 250.0f );
    const float at = qMin( ( j->bucket + 0.5f ) * step, qMax( 0.0f, pendingLength - 0.5f ) );

    j->item = input_item_New( qtu( uri ), "seek-preview" );
    if( !j->item )
    {
        delete j;
        return;
    }
    static const char *const options[] = {
        ":vout=vmem", ":no-audio", ":no-spu", ":no-osd", ":no-video-title-show",
        ":no-sub-autodetect-file", ":input-fast-seek", ":no-stats",
        ":avcodec-hw=none", ":video-filter=", ":sub-source=",
    };
    for( const char *opt : options )
        input_item_AddOption( j->item, opt, VLC_INPUT_OPTION_TRUSTED );
    input_item_AddOption( j->item, qtu( QString( ":start-time=%1" ).arg( at, 0, 'f', 2 ) ),
                          VLC_INPUT_OPTION_TRUSTED );

    var_SetAddress( host, "vmem-data", j );
    j->input = input_Create( host, j->item, "seek preview", NULL, NULL );
    if( !j->input || input_Start( j->input ) )
    {
        if( j->input )
            input_Close( j->input );
        input_item_Release( j->item );
        delete j;
        return;
    }
    job = j;
    watchdog.start();
}

void SeekPreview::finish( Job *j, const QImage &img )
{
    if( j != job )
        return; /* already closed */
    job = NULL;
    watchdog.stop();

    /* closing joins the input and its video output, after which no
     * callback can touch the job any more */
    input_Stop( j->input );
    input_Close( j->input );
    input_item_Release( j->item );

    if( !img.isNull() )
    {
        cache.insert( j->bucket, img );
        cacheOrder.append( j->bucket );
        while( cacheOrder.size() > CACHE_SIZE )
            cache.remove( cacheOrder.takeFirst() );
        emit ready( j->time, img );
    }
    delete j;

    if( pendingBucket >= 0 )
    {
        if( cache.contains( pendingBucket ) )
        {
            emit ready( pendingTime, cache.value( pendingBucket ) );
            pendingBucket = -1;
        }
        else
            debounce.start( 0 );
    }
}
