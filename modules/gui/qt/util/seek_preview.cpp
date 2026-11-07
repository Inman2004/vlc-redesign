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
#include "util/frame_grabber.hpp"
#include "input_manager.hpp"

#include <vlc_input.h>
#include <vlc_input_item.h>

#include <QPointer>

#define BUCKETS 250
#define CACHE_SIZE ( BUCKETS + 10 )

SeekPreview::SeekPreview( intf_thread_t *_p_intf, QObject *parent )
    : QObject( parent ), p_intf( _p_intf ),
      pendingBucket( -1 ), pendingTime( 0 ), pendingLength( 0 )
{
    grabber = new FrameGrabber( p_intf, QSize( MAX_W, MAX_H ), this );
    connect( grabber, &FrameGrabber::finished, this,
             [this]( const QString &u, const QImage &img, qint64, qint64 ms ) {
        grabbed( u, img, ms );
    } );

    debounce.setSingleShot( true );
    debounce.setInterval( 25 );
    connect( &debounce, &QTimer::timeout, this, &SeekPreview::startNext );

    connect( THEMIM, &MainInputManager::inputChanged, this, [this]() {
        cancel();
        dropCache();
    } );
}

SeekPreview::~SeekPreview()
{
    debounce.stop();
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
    if( length <= 0 )
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
        if( !grabber->isBusy() )
            debounce.start(); /* keep filling around here */
        return;
    }
    pendingBucket = bucket;
    pendingTime = time;
    pendingLength = length;
    if( !grabber->isBusy() )
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
    if( grabber->isBusy() || uri.isEmpty() )
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

    /* decode the middle of the bucket */
    const float step = qMax( 1.0f, pendingLength / float( BUCKETS ) );
    const float at = qMin( ( pendingBucket + 0.5f ) * step, qMax( 0.0f, pendingLength - 0.5f ) );
    jobBucket = pendingBucket;
    jobTime = pendingTime;
    pendingBucket = -1;
    if( !grabber->grab( uri, at ) )
    {
        failed.insert( jobBucket );
        jobBucket = -1;
    }
}

void SeekPreview::grabbed( const QString &u, const QImage &img, qint64 ms )
{
    const int bucket = jobBucket;
    jobBucket = -1;
    msg_Dbg( p_intf, "seek preview: bucket %d %s in %lld ms", bucket,
             img.isNull() ? "failed" : "decoded", (long long)ms );
    if( u != uri || bucket < 0 )
    {
        debounce.start( 0 );
        return; /* a previous file */
    }
    if( !img.isNull() )
    {
        /* 24-bit keeps a full cache of 250 frames around 27 MB */
        cache.insert( bucket, img.convertToFormat( QImage::Format_RGB888 ) );
        cacheOrder.append( bucket );
        while( cacheOrder.size() > CACHE_SIZE )
            cache.remove( cacheOrder.takeFirst() );
        emit ready( jobTime, img );
    }
    else
        failed.insert( bucket );

    if( pendingBucket >= 0 && cache.contains( pendingBucket ) )
    {
        emit ready( pendingTime, cache.value( pendingBucket ) );
        pendingBucket = -1;
    }
    /* the hovered bucket first, then keep filling */
    debounce.start( 0 );
}
