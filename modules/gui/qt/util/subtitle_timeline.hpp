/*****************************************************************************
 * subtitle_timeline.hpp : subtitle cues for the seek bar (P7)
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

/*
 * With "Show Subtitle Timeline on Seekbar" on, the seek bar shows a window
 * of the film around the playhead with the cues of the selected subtitle
 * track. VLC 3 does not tell the interface when a cue starts, so the cues
 * are read here from the subtitle file itself (.srt, .vtt, .ass / .ssa).
 * Subtitles inside the video file have no file to read: the bar stays plain.
 */

#ifndef VLC_QT_SUBTITLE_TIMELINE_HPP_
#define VLC_QT_SUBTITLE_TIMELINE_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

class SubtitleTimeline : public QObject
{
    Q_OBJECT
public:
    struct Cue
    {
        int64_t start, end;   /* microseconds, file time (before the delay) */
        QString text;         /* first line, tags removed */
    };
    /* the window the bar shows */
    static const int64_t WINDOW = INT64_C( 50000000 );

    static SubtitleTimeline *instance( intf_thread_t * );

    /* the switch (qt-subtitle-timeline) */
    bool enabled() const;
    void setEnabled( bool );
    /* switch on and a subtitle file with cues is playing */
    bool active() const;
    /* why the timeline is not shown although it is on, or empty */
    QString unavailableReason() const { return reason; }

    const QVector<Cue> &cues() const { return list; }
    int64_t delay() const;          /* spu-delay, microseconds */
    int64_t now() const;            /* the playhead, interpolated */
    int64_t length() const { return lengthUs; }
    /* [start, end) of the window around now(), or around `around` */
    void window( int64_t *start, int64_t *end, int64_t around = -1 ) const;
    /* the cue under a film time (delay applied), or -1 */
    int cueAt( int64_t time ) const;

public slots:
    void previousCue();
    void nextCue();
    void earlier();   /* VLC's "Subtitle delay down" */
    void later();     /* VLC's "Subtitle delay up" */

signals:
    /* the cues, the delay or the switch changed */
    void changed();
    /* the playhead moved (also ~30 times a second while playing) */
    void moved();

private:
    SubtitleTimeline( intf_thread_t * );
    void reload();
    void onPosition( float, int64_t, int );
    QString selectedFile() const;
    static QVector<Cue> parse( const QString &path );

    intf_thread_t *p_intf;
    QVector<Cue> list;
    QString path, reason;
    qint64 stamp;           /* mtime of `path` when read */
    int64_t lastTime, lengthUs;
    int64_t correction = 0;     /* drawn minus reported time at the last update */
    enum { DRIFT_MS = 300 };
    QElapsedTimer sinceUpdate;
    QTimer *ticker;
    bool on;
};

#endif
