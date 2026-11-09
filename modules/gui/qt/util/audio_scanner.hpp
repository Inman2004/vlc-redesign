/*****************************************************************************
 * audio_scanner.hpp : tags, cover art and waveform peaks of a song (P5c)
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

#ifndef VLC_QT_AUDIO_SCANNER_HPP_
#define VLC_QT_AUDIO_SCANNER_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>

/* What one pass over a song gives */
struct AudioScan
{
    bool ok = false;
    QString title, artist, album, genre;
    int track = 0;
    qint64 length = 0;        /* microseconds, 0 when unknown */
    QByteArray art;           /* encoded picture (embedded, or folder art) */
    QByteArray peaks;         /* PEAK_COUNT bytes, 0..255, empty on failure */
};

/* VLC 3 exposes neither tags without playing nor audio samples. A hidden
 * input opens the song (meta and attached pictures come with the demux)
 * and transcodes it to 8 kHz mono PCM in a temporary file; writing to a
 * file is not paced by the clock, so a song takes a second or two. The
 * peaks are the loudest samples of PEAK_COUNT equal slices. */
class AudioScanner : public QObject
{
    Q_OBJECT
public:
    enum { PEAK_COUNT = 600, SAMPLE_RATE = 8000 };

    AudioScanner( intf_thread_t *, QObject *parent = NULL );
    virtual ~AudioScanner();

    bool isBusy() const { return job != NULL; }
    bool scan( const QString &path );

    struct Job;     /* public for the C callbacks */

signals:
    void finished( const QString &path, const AudioScan &, qint64 elapsedMs );

private:
    static int eventCb( vlc_object_t *, const char *, vlc_value_t, vlc_value_t, void * );
    void finish( Job * );

    intf_thread_t *p_intf;
    vlc_object_t *host;
    Job *job;
    QTimer watchdog;
    std::atomic<int> closing { 0 };
};

Q_DECLARE_METATYPE( AudioScan )

#endif
