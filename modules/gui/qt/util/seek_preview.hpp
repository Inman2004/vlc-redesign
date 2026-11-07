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

/* VLC 3 has no thumbnailer API. A preview is decoded by a second, hidden
 * input whose video goes to the "vmem" display: it starts at the hovered
 * time (fast seek, no audio, no subtitles) and is closed as soon as the
 * first picture arrives. One job runs at a time; the latest hover wins,
 * and pictures are cached per time bucket for the current file. */
class SeekPreview : public QObject
{
    Q_OBJECT
public:
    enum { MAX_W = 252, MAX_H = 142 };

    SeekPreview( intf_thread_t *, QObject *parent = NULL );
    virtual ~SeekPreview();

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
    struct Job;
    static unsigned setupCb( void **, char *, unsigned *, unsigned *,
                             unsigned *, unsigned * );
    static void *lockCb( void *, void ** );
    static void displayCb( void *, void * );

    int bucketOf( float time, float length ) const;
    void startNext();
    void finish( Job *, const QImage & );
    void dropCache();

    intf_thread_t *p_intf;
    vlc_object_t *host;    /* carries the vmem callbacks for the inputs */
    Job *job;
    QTimer debounce, watchdog;
    QString uri;
    int pendingBucket;
    float pendingTime;
    float pendingLength;
    QMap<int, QImage> cache;
    QList<int> cacheOrder;
};

#endif
