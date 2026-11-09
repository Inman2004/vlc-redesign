/*****************************************************************************
 * playlist_store.hpp : the user's named playlists (P5d)
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

#ifndef VLC_QT_PLAYLIST_STORE_HPP_
#define VLC_QT_PLAYLIST_STORE_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

struct SavedPlaylist
{
    QString file;           /* the .xspf, the key */
    QString title;
    QString image;          /* picture URI, may be empty */
    QStringList uris;
    int plays = 0;
    qint64 lastPlayed = 0;
};

/* Named playlists are XSPF files in VLC's data folder ("playlists/"),
 * as the New Playlist dialog (P4) writes them. The folder is watched, so
 * a playlist made anywhere appears at once. */
class PlaylistStore : public QObject
{
    Q_OBJECT
public:
    static PlaylistStore *instance( intf_thread_t * );

    const QVector<SavedPlaylist> &playlists() const { return list; }
    int indexOf( const QString &file ) const;

    /* returns the new file, empty on failure */
    QString create( const QString &title, const QStringList &uris );
    bool append( const QString &file, const QStringList &uris );
    bool removeTrack( const QString &file, int track );
    bool rename( const QString &file, const QString &title );
    bool remove( const QString &file );            /* to the trash */
    /* .m3u / .m3u8 by extension, else a copy of the XSPF */
    bool exportTo( const QString &file, const QString &dest );
    /* replaces the queue with the playlist from its track'th entry */
    void play( const QString &file, int track = 0 );

public slots:
    void reload();

signals:
    void changed();

private:
    PlaylistStore( intf_thread_t *, QObject *parent );
    bool write( const SavedPlaylist & );
    static SavedPlaylist read( const QString &file );

    intf_thread_t *p_intf;
    QString dir;
    QVector<SavedPlaylist> list;
    QFileSystemWatcher watcher;
    QTimer debounce;
};

#endif
