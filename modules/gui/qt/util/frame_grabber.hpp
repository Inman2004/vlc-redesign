/*****************************************************************************
 * frame_grabber.hpp : one picture from a media file, off screen (P2b/P5a)
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

#ifndef VLC_QT_FRAME_GRABBER_HPP_
#define VLC_QT_FRAME_GRABBER_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QTimer>
#include <atomic>

/* VLC 3 has no thumbnailer API. A picture is decoded by a hidden input
 * whose video goes to the "vmem" display and that has no window:
 *  - it starts at the asked time (fast seek: the keyframe before it), or
 *    at a fraction of the length once the length is known;
 *  - no audio, subtitles, OSD or filters, one decoder thread, software
 *    decoding, no input caching;
 *  - pictures shown before the seek has run are ignored (the start seek
 *    is a queued control and the input demuxes once before running it);
 *  - closing (about 40ms for 4K) runs on a worker thread.
 * One job at a time per grabber; run several grabbers for parallelism. */
class FrameGrabber : public QObject
{
    Q_OBJECT
public:
    FrameGrabber( intf_thread_t *, QSize maxSize, QObject *parent = NULL );
    virtual ~FrameGrabber();

    bool isBusy() const { return job != NULL; }
    /* seconds >= 0: that time; fraction in (0,1) when seconds < 0 */
    bool grab( const QString &uri, double seconds, double fraction = 0.0 );
    void setTimeout( int ms ) { watchdog.setInterval( ms ); }

    struct Job;     /* public for the C callbacks */

signals:
    /* img is null on failure; length in microseconds, 0 when unknown */
    void finished( const QString &uri, const QImage &img, qint64 length,
                   qint64 elapsedMs );

private:
    static unsigned setupCb( void **, char *, unsigned *, unsigned *,
                             unsigned *, unsigned * );
    static void *lockCb( void *, void ** );
    static void displayCb( void *, void * );
    void finish( Job *, const QImage & );

    intf_thread_t *p_intf;
    vlc_object_t *host;    /* carries the vmem callbacks and window=none */
    QSize maxSize;
    Job *job;
    QTimer watchdog;
    std::atomic<int> closing { 0 };
};

#endif
