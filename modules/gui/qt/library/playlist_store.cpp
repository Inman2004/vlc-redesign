/*****************************************************************************
 * playlist_store.cpp : the user's named playlists (P5d)
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

#include "library/playlist_store.hpp"
#include "util/qt_compat.hpp"
#include "dialogs/redesign_dialogs.hpp"      /* SubtitleScope::playlistDir / writeXspf */
#include "library/media_library.hpp"
#include "dialogs_provider.hpp"
#include "input_manager.hpp"
#include "recents.hpp"

#include <vlc_playlist.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamReader>

PlaylistStore *PlaylistStore::instance( intf_thread_t *p_intf )
{
    static QPointer<PlaylistStore> store;
    if( !store )
        store = new PlaylistStore( p_intf, THEMIM );
    return store;
}

PlaylistStore::PlaylistStore( intf_thread_t *_p_intf, QObject *parent )
    : QObject( parent ), p_intf( _p_intf ), dir( SubtitleScope::playlistDir() )
{
    debounce.setSingleShot( true );
    debounce.setInterval( 200 );
    connect( &debounce, &QTimer::timeout, this, &PlaylistStore::reload );
    watcher.addPath( dir );
    connect( &watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { debounce.start(); } );
    connect( &watcher, &QFileSystemWatcher::fileChanged, this, [this]() { debounce.start(); } );
    reload();
}

SavedPlaylist PlaylistStore::read( const QString &file )
{
    SavedPlaylist p;
    p.file = file;
    QFile f( file );
    if( !f.open( QIODevice::ReadOnly ) )
        return p;
    QXmlStreamReader x( &f );
    bool inTrack = false;
    while( !x.atEnd() )
    {
        x.readNext();
        if( x.isStartElement() )
        {
            const QStringRef n = x.name();
            if( n == "track" )
                inTrack = true;
            else if( n == "location" && inTrack )
                p.uris << x.readElementText().trimmed();
            else if( n == "title" && !inTrack && p.title.isEmpty() )
                p.title = x.readElementText().trimmed();
            else if( n == "image" && !inTrack && p.image.isEmpty() )
                p.image = x.readElementText().trimmed();
        }
        else if( x.isEndElement() && x.name() == "track" )
            inTrack = false;
    }
    if( p.title.isEmpty() )
        p.title = QFileInfo( file ).completeBaseName();
    return p;
}

void PlaylistStore::reload()
{
    QSettings *s = getSettings();
    list.clear();
    QDir d( dir );
    foreach( const QFileInfo &fi, d.entryInfoList( QStringList() << "*.xspf", QDir::Files, QDir::Name ) )
    {
        SavedPlaylist p = read( fi.absoluteFilePath() );
        const QString key = "Playlists/" + fi.completeBaseName();
        p.plays = s->value( key + "/plays", 0 ).toInt();
        p.lastPlayed = s->value( key + "/lastPlayed", 0 ).toLongLong();
        list << p;
    }
    /* the watcher drops files that were replaced: watch them again */
    if( !watcher.files().isEmpty() )
        watcher.removePaths( watcher.files() );
    foreach( const SavedPlaylist &p, list )
        watcher.addPath( p.file );
    emit changed();
}

int PlaylistStore::indexOf( const QString &file ) const
{
    for( int i = 0; i < list.size(); i++ )
        if( list[i].file == file )
            return i;
    return -1;
}

bool PlaylistStore::write( const SavedPlaylist &p )
{
    const bool ok = SubtitleScope::writeXspf( p.file, p.title, p.image, p.uris );
    debounce.start();
    return ok;
}

QString PlaylistStore::create( const QString &title, const QStringList &uris )
{
    QString base = QString( title ).replace( '/', '-' ).trimmed();
    if( base.isEmpty() )
        return QString();
    QString file = dir + "/" + base + ".xspf";
    for( int n = 2; QFileInfo::exists( file ); n++ )
        file = dir + "/" + base + QString( " (%1).xspf" ).arg( n );
    SavedPlaylist p;
    p.file = file;
    p.title = title.trimmed();
    p.uris = uris;
    return write( p ) ? file : QString();
}

bool PlaylistStore::append( const QString &file, const QStringList &uris )
{
    const int i = indexOf( file );
    if( i < 0 )
        return false;
    SavedPlaylist p = list[i];
    p.uris << uris;
    return write( p );
}

bool PlaylistStore::removeTrack( const QString &file, int track )
{
    const int i = indexOf( file );
    if( i < 0 || track < 0 || track >= list[i].uris.size() )
        return false;
    SavedPlaylist p = list[i];
    p.uris.removeAt( track );
    return write( p );
}

bool PlaylistStore::rename( const QString &file, const QString &title )
{
    const int i = indexOf( file );
    if( i < 0 || title.trimmed().isEmpty() )
        return false;
    SavedPlaylist p = list[i];
    p.title = title.trimmed();
    return write( p );
}

bool PlaylistStore::remove( const QString &file )
{
    bool ok = QtCompat::moveToTrash( file );
    if( !ok )
        ok = QFile::remove( file );
    debounce.start();
    return ok;
}

bool PlaylistStore::exportTo( const QString &file, const QString &dest )
{
    const int i = indexOf( file );
    if( i < 0 )
        return false;
    const QString ext = QFileInfo( dest ).suffix().toLower();
    if( ext != "m3u" && ext != "m3u8" )
    {
        QFile::remove( dest );
        return QFile::copy( file, dest );
    }
    QSaveFile f( dest );
    if( !f.open( QIODevice::WriteOnly ) )
        return false;
    QTextStream out( &f );
    out.setCodec( "UTF-8" );
    out << "#EXTM3U\n#PLAYLIST:" << list[i].title << "\n";
    foreach( const QString &u, list[i].uris )
    {
        const QUrl url( u );
        out << ( url.isLocalFile() ? url.toLocalFile() : u ) << "\n";
    }
    out.flush();
    return f.commit();
}

void PlaylistStore::play( const QString &file, int track )
{
    const int i = indexOf( file );
    if( i < 0 || list[i].uris.isEmpty() )
        return;
    const SavedPlaylist p = list[i];
    playlist_Clear( THEPL, pl_Unlocked );
    track = qBound( 0, track, p.uris.size() - 1 );
    for( int k = 0; k < p.uris.size(); k++ )
        Open::openMRL( p_intf, p.uris[( track + k ) % p.uris.size()], k == 0, true );

    /* Home's Frequently Visited Playlists */
    const QString key = "Playlists/" + QFileInfo( file ).completeBaseName();
    getSettings()->setValue( key + "/plays", p.plays + 1 );
    getSettings()->setValue( key + "/lastPlayed", QtCompat::currentSecsSinceEpoch() );
    list[i].plays = p.plays + 1;
    list[i].lastPlayed = QtCompat::currentSecsSinceEpoch();
    emit changed();
}
