/*****************************************************************************
 * frame_grabber.cpp : one picture from a media file, off screen (P2b/P5a)
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

#include "util/frame_grabber.hpp"

#include <vlc_input.h>
#include <vlc_input_item.h>
#include <vlc_variables.h>

#include <QElapsedTimer>
#include <QPointer>
#include <thread>
#include <vector>

struct FrameGrabber::Job
{
    QPointer<FrameGrabber> owner;
    QString uri;
    input_thread_t *input = NULL;
    input_item_t *item = NULL;
    unsigned maxW = 0, maxH = 0;
    unsigned w = 0, h = 0;
    std::vector<uint32_t> pixels;
    std::atomic<bool> done { false };
    std::atomic<bool> seeked { false };
    std::atomic<bool> positioned { false };
    std::atomic<qint64> length { 0 };
    float target = 0;       /* seconds, once known */
    double fraction = 0;    /* > 0: seek there when the length is known */
    QElapsedTimer clock;
};

/* The vmem callbacks run on the hidden input's video output thread. They
 * reach the job through "vmem-data", set on the job's own input. */
unsigned FrameGrabber::setupCb( void **opaque, char *chroma, unsigned *width,
                                unsigned *height, unsigned *pitches,
                                unsigned *lines )
{
    Job *j = static_cast<Job *>( *opaque );
    if( *width == 0 || *height == 0 )
        return 0;
    const double scale = qMin( 1.0, qMin( double( j->maxW ) / *width,
                                          double( j->maxH ) / *height ) );
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

void *FrameGrabber::lockCb( void *opaque, void **planes )
{
    Job *j = static_cast<Job *>( opaque );
    planes[0] = j->pixels.data();
    return NULL;
}

/* Input events, on the input thread.
 * - Fraction mode: when the length is first known, seek to the fraction.
 * - Either mode: the first position event reporting a time past the start
 *   comes after the seek, which flushed the decoder and the output; only
 *   pictures after it are taken. */
static int InputEventCb( vlc_object_t *obj, const char *, vlc_value_t,
                         vlc_value_t cur, void *data )
{
    FrameGrabber::Job *j = static_cast<FrameGrabber::Job *>( data );
    if( cur.i_int != INPUT_EVENT_POSITION && cur.i_int != INPUT_EVENT_LENGTH )
        return VLC_SUCCESS;

    const qint64 length = var_GetInteger( obj, "length" );
    if( length > 0 )
        j->length = length;

    if( j->fraction > 0 && !j->positioned && length > 0 )
    {
        j->positioned = true;
        j->target = float( j->fraction * length / 1e6 );
        if( j->target < 1.0f )
            j->seeked = true;   /* the first frames are the right ones */
        else
            var_SetFloat( obj, "position", float( j->fraction ) );
        return VLC_SUCCESS;
    }
    if( cur.i_int == INPUT_EVENT_POSITION && ( j->fraction <= 0 || j->positioned ) )
    {
        const float t = var_GetInteger( obj, "time" ) / 1e6f;
        if( t >= qMin( 0.25f, j->target / 2 ) )
            j->seeked = true;
    }
    return VLC_SUCCESS;
}

void FrameGrabber::displayCb( void *opaque, void * )
{
    Job *j = static_cast<Job *>( opaque );
    if( !j->seeked.load() )
        return;
    if( j->done.exchange( true ) )
        return;
    /* RV32 with the vmem masks is BGRX in memory: QImage::Format_RGB32 */
    QImage img( reinterpret_cast<const uchar *>( j->pixels.data() ), int( j->w ),
                int( j->h ), int( j->w * 4 ), QImage::Format_RGB32 );
    const QImage copy = img.copy();
    QPointer<FrameGrabber> owner = j->owner;
    if( owner )
        QMetaObject::invokeMethod( owner.data(), [owner, j, copy]() {
            if( owner )
                owner->finish( j, copy );
        }, Qt::QueuedConnection );
}

FrameGrabber::FrameGrabber( intf_thread_t *_p_intf, QSize _maxSize, QObject *parent )
    : QObject( parent ), p_intf( _p_intf ), maxSize( _maxSize ), job( NULL )
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
         * back to a real X window that flashes on screen. vmem needs none. */
        var_Create( host, "window", VLC_VAR_STRING );
        var_SetString( host, "window", "none" );
    }

    /* a file that never delivers a picture must not block the next job */
    watchdog.setSingleShot( true );
    watchdog.setInterval( 4000 );
    connect( &watchdog, &QTimer::timeout, this, [this]() {
        if( job )
            finish( job, QImage() );
    } );
}

FrameGrabber::~FrameGrabber()
{
    if( job )
        finish( job, QImage() );
    /* the host must outlive every input still closing on a worker */
    while( closing.load() > 0 )
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    if( host )
        vlc_object_release( host );
}

bool FrameGrabber::grab( const QString &uri, double seconds, double fraction )
{
    if( job || !host || uri.isEmpty() )
        return false;

    Job *j = new Job;
    j->owner = this;
    j->uri = uri;
    j->maxW = unsigned( maxSize.width() );
    j->maxH = unsigned( maxSize.height() );
    if( seconds >= 0 )
    {
        j->target = float( seconds );
        /* close to the start, the first frame is the right one anyway */
        j->seeked = seconds < 1.0;
    }
    else
        j->fraction = qBound( 0.0, fraction, 0.99 );

    j->item = input_item_New( qtu( uri ), "frame grabber" );
    if( !j->item )
    {
        delete j;
        return false;
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
    if( seconds >= 1.0 )
        input_item_AddOption( j->item, qtu( QString( ":start-time=%1" ).arg( seconds, 0, 'f', 2 ) ),
                              VLC_INPUT_OPTION_TRUSTED );

    j->input = input_Create( host, j->item, "frame grabber", NULL, NULL );
    if( j->input )
    {
        /* per input, so parallel grabbers never see each other's job */
        var_Create( j->input, "vmem-data", VLC_VAR_ADDRESS );
        var_SetAddress( j->input, "vmem-data", j );
        var_AddCallback( j->input, "intf-event", InputEventCb, j );
    }
    if( !j->input || input_Start( j->input ) )
    {
        if( j->input )
        {
            var_DelCallback( j->input, "intf-event", InputEventCb, j );
            input_Close( j->input );
        }
        input_item_Release( j->item );
        delete j;
        return false;
    }
    job = j;
    j->clock.start();
    watchdog.start();
    return true;
}

void FrameGrabber::finish( Job *j, const QImage &img )
{
    if( j != job )
        return; /* already closed */
    job = NULL;
    watchdog.stop();

    const QString uri = j->uri;
    const qint64 length = j->length.load();
    const qint64 elapsed = j->clock.elapsed();

    /* Closing joins the input and its video output: on a worker, so the UI
     * never waits. After the join no callback can touch the job. */
    input_thread_t *input = j->input;
    input_item_t *item = j->item;
    closing++;
    std::thread( [input, item, j, this]() {
        var_DelCallback( input, "intf-event", InputEventCb, j );
        input_Stop( input );
        input_Close( input );
        input_item_Release( item );
        delete j;
        closing--;
    } ).detach();

    emit finished( uri, img, length, elapsed );
}
