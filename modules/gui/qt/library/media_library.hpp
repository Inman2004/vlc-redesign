/*****************************************************************************
 * media_library.hpp : Qt-side media library store (P5a)
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

#ifndef VLC_QT_MEDIA_LIBRARY_HPP_
#define VLC_QT_MEDIA_LIBRARY_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QObject>
#include <QVector>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <QImage>
#include <atomic>

class QThread;
class FrameGrabber;

/* One file in the library. Times are milliseconds, dates seconds since the
 * epoch. The user's data (progress, watched, favourite, play count, last
 * played, chosen subtitle) survives rescans as long as the file is there. */
struct LibraryItem
{
    enum Kind { VIDEO, AUDIO };

    QString path;            /* absolute local path, the key */
    Kind kind = VIDEO;
    QString category;        /* "videos", "shows" or "music" */
    QString show, season;    /* shows: "The Wand", "S01" */
    qint64 size = 0;
    qint64 mtime = 0;
    qint64 duration = 0;     /* 0 until known */
    qint64 progress = 0;     /* resume position */
    bool watched = false;
    bool favorite = false;
    int playCount = 0;
    qint64 lastPlayed = 0;
    QString subtitle;

    QString uri() const;
    QString name() const;    /* file name */
    QString folder() const;  /* absolute directory */
    /* progress as 0..1 for the grid underline, 0 when unknown */
    qreal progressRatio() const;
};

/* VLC 3 keeps no media-library database (the core "library" is just a
 * playlist node), so the redesign keeps its own:
 *  - roots: qt-library-roots, or ~/Videos and ~/Music;
 *  - a background scan by extension, sorted into videos / shows / music;
 *  - per-item user data, seeded from the resume positions VLC already
 *    keeps (RecentsMRL) and updated from playback;
 *  - thumbnails grabbed off screen (FrameGrabber), cached as JPEG files;
 *  - stored as JSON in the user data directory. */
class MediaLibrary : public QObject
{
    Q_OBJECT
public:
    static MediaLibrary *instance( intf_thread_t * );
    virtual ~MediaLibrary();

    QStringList roots() const;
    const QVector<LibraryItem> &items() const { return list; }
    int indexOf( const QString &path ) const { return byPath.value( path, -1 ); }
    bool isScanning() const { return scanThread != NULL; }

    void setWatched( const QString &path, bool );
    void setFavorite( const QString &path, bool );

    /* the cached thumbnail file of a video, empty when not made yet */
    QString thumbnailFile( const LibraryItem & ) const;
    /* ask for a thumbnail now (visible tiles go first) */
    void requestThumbnail( const QString &path );

    /* "S01E04", "1x04" and "Season 1" folders make a show episode */
    static void classify( LibraryItem & );

public slots:
    void rescan();

signals:
    void scanStarted();
    void scanFinished( int count, qint64 elapsedMs );
    void itemsChanged();               /* added, removed or reordered */
    void itemChanged( int index );     /* data of one item */
    void thumbnailReady( const QString &path );

private:
    MediaLibrary( intf_thread_t *, QObject *parent );
    struct ScanEntry { QString path; qint64 size, mtime; bool audio; };
    void merge( const QVector<ScanEntry> & );
    void load();
    void scheduleSave();
    void save();
    void reindex();
    void trackInput();
    void trackPosition( float pos, int64_t time, int length );

    void queueThumbnails();
    void pumpThumbnails();
    void thumbnailDone( FrameGrabber *, const QString &uri, const QImage &,
                        qint64 length, qint64 elapsedMs );
    QString thumbnailKey( const LibraryItem & ) const;

    intf_thread_t *p_intf;
    QVector<LibraryItem> list;
    QHash<QString, int> byPath;
    QString storeFile, thumbDir;

    QThread *scanThread = NULL;
    std::atomic<bool> scanAbort { false };
    qint64 scanStart = 0;

    QTimer saveTimer;
    QString playingPath;       /* the library item being played, if any */

    QList<FrameGrabber *> grabbers;
    QHash<FrameGrabber *, QString> grabbing;   /* grabber -> path */
    QStringList thumbQueue;                    /* paths, front first */
    QHash<QString, int> thumbFailures;
    int thumbsMade = 0;
    qint64 thumbsStart = 0;
};

#endif
