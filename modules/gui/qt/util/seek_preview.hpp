/*****************************************************************************
 * seek_preview.hpp : thumbnails for the seek bar hover (P2b)
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

#ifndef VLC_QT_SEEK_PREVIEW_HPP_
#define VLC_QT_SEEK_PREVIEW_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QObject>
#include <QImage>
#include <QTimer>
#include <QMap>
#include <QList>
#include <QSet>

class FrameGrabber;

/* Seek bar thumbnails: pictures from FrameGrabber at the hovered time,
 * one job at a time; the latest hover wins. From the first hover the rest
 * of the file is filled in the background, nearest first, about 250
 * pictures cached per time bucket for the current file. */
class SeekPreview : public QObject
{
    Q_OBJECT
public:
    enum { MAX_W = 252, MAX_H = 142 };

    SeekPreview( intf_thread_t *, QObject *parent = NULL );
    virtual ~SeekPreview();

    static SeekPreview *shared( intf_thread_t * );
    /* qt-seek-preview, and only for local files with video */
    static bool available( intf_thread_t * );

    /* time and length in seconds */
    void request( float time, float length );
    void cancel();
    /* a cached picture near this time, if any */
    QImage cached( float time, float length ) const;

signals:
    void ready( float time, const QImage & );

private:
    int bucketOf( float time, float length ) const;
    void startNext();
    int nextFillBucket() const;
    void grabbed( const QString &uri, const QImage &, qint64 elapsedMs );
    void dropCache();

    intf_thread_t *p_intf;
    FrameGrabber *grabber;
    QTimer debounce;
    QString uri;
    int pendingBucket;
    float pendingTime;
    float pendingLength;
    int jobBucket = -1;      /* the bucket being decoded */
    float jobTime = 0;
    QMap<int, QImage> cache;
    QList<int> cacheOrder;
    QSet<int> failed;
    int fillCenter = 0;
    float fillLength = 0;
};

#endif
