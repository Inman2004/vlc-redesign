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
#include <QXmlStreamWriter>

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
    QString uri, title, image;
    QStringList opts;
    while( !x.atEnd() )
    {
        x.readNext();
        if( x.isStartElement() )
        {
            const QStringRef n = x.name();
            if( n == "track" )
            {
                inTrack = true;
                uri.clear(); title.clear(); image.clear(); opts.clear();
            }
            else if( n == "location" && inTrack )
                uri = x.readElementText().trimmed();
            else if( n == "title" && inTrack )
                title = x.readElementText().trimmed();
            else if( n == "image" && inTrack )
                image = x.readElementText().trimmed();
            else if( n == "option" && inTrack )      /* <vlc:option> */
                opts << x.readElementText().trimmed();
            else if( n == "title" && !inTrack && p.title.isEmpty() )
                p.title = x.readElementText().trimmed();
            else if( n == "image" && !inTrack && p.image.isEmpty() )
                p.image = x.readElementText().trimmed();
        }
        else if( x.isEndElement() && x.name() == "track" )
        {
            inTrack = false;
            if( !uri.isEmpty() )
            {
                p.uris << uri;
                p.titles << title;
                p.images << image;
                p.options << opts;
            }
        }
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

/* keeps the per-track lists as long as the uris */
static void normalize( SavedPlaylist &p )
{
    while( p.titles.size() < p.uris.size() ) p.titles << QString();
    while( p.images.size() < p.uris.size() ) p.images << QString();
    while( p.options.size() < p.uris.size() ) p.options << QStringList();
    while( p.titles.size() > p.uris.size() ) p.titles.removeLast();
    while( p.images.size() > p.uris.size() ) p.images.removeLast();
    while( p.options.size() > p.uris.size() ) p.options.removeLast();
}

/* XSPF as VLC writes it: track titles, pictures and <vlc:option>s */
static bool writePlaylist( const SavedPlaylist &p )
{
    QSaveFile f( p.file );
    if( !f.open( QIODevice::WriteOnly ) )
        return false;
    QXmlStreamWriter x( &f );
    x.setAutoFormatting( true );
    x.writeStartDocument();
    x.writeStartElement( "playlist" );
    x.writeDefaultNamespace( "http://xspf.org/ns/0/" );
    x.writeNamespace( "http://www.videolan.org/vlc/playlist/ns/0/", "vlc" );
    x.writeAttribute( "version", "1" );
    x.writeTextElement( "title", p.title );
    if( !p.image.isEmpty() )
        x.writeTextElement( "image", p.image );
    x.writeStartElement( "trackList" );
    for( int i = 0; i < p.uris.size(); i++ )
    {
        x.writeStartElement( "track" );
        x.writeTextElement( "location", p.uris[i] );
        if( i < p.titles.size() && !p.titles[i].isEmpty() )
            x.writeTextElement( "title", p.titles[i] );
        if( i < p.images.size() && !p.images[i].isEmpty() )
            x.writeTextElement( "image", p.images[i] );
        if( i < p.options.size() && !p.options[i].isEmpty() )
        {
            x.writeStartElement( "extension" );
            x.writeAttribute( "application", "http://www.videolan.org/vlc/playlist/0" );
            foreach( const QString &o, p.options[i] )
                x.writeTextElement( "http://www.videolan.org/vlc/playlist/ns/0/", "option", o );
            x.writeEndElement();
        }
        x.writeEndElement();
    }
    x.writeEndElement();
    x.writeEndElement();
    x.writeEndDocument();
    return !x.hasError() && f.commit();
}

bool PlaylistStore::write( const SavedPlaylist &p )
{
    SavedPlaylist q = p;
    normalize( q );
    const bool ok = writePlaylist( q );
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

QString PlaylistStore::create( const SavedPlaylist &tracks )
{
    QString base = QString( tracks.title ).replace( '/', '-' ).trimmed();
    if( base.isEmpty() || tracks.uris.isEmpty() )
        return QString();
    QString file = dir + "/" + base + ".xspf";
    for( int n = 2; QFileInfo::exists( file ); n++ )
        file = dir + "/" + base + QString( " (%1).xspf" ).arg( n );
    SavedPlaylist p = tracks;
    p.file = file;
    p.title = tracks.title.trimmed();
    return write( p ) ? file : QString();
}

static void collectQueue( playlist_item_t *node, SavedPlaylist &p )
{
    for( int i = 0; i < node->i_children; i++ )
    {
        playlist_item_t *c = node->pp_children[i];
        if( c->i_children >= 0 )
        {
            collectQueue( c, p );
            continue;
        }
        input_item_t *in = c->p_input;
        char *uri = input_item_GetURI( in );
        if( !uri )
            continue;
        p.uris << qfu( uri );
        free( uri );
        char *name = input_item_GetTitleFbName( in );
        const QString title = qfu( name );
        free( name );
        /* VLC names an untitled item after its file: keep only real names */
        p.titles << ( title == QUrl( p.uris.last() ).fileName() ? QString() : title );
        char *art = input_item_GetArtURL( in );
        p.images << ( art && !strncmp( art, "http", 4 ) ? qfu( art ) : QString() );
        free( art );
        QStringList opts;
        vlc_mutex_lock( &in->lock );
        for( int k = 0; k < in->i_options; k++ )
            opts << qfu( in->ppsz_options[k] );
        vlc_mutex_unlock( &in->lock );
        p.options << opts;
    }
}

SavedPlaylist PlaylistStore::fromQueue( intf_thread_t *p_intf )
{
    SavedPlaylist p;
    playlist_Lock( THEPL );
    collectQueue( THEPL->p_playing, p );
    playlist_Unlock( THEPL );
    return p;
}

bool PlaylistStore::append( const QString &file, const QStringList &uris )
{
    const int i = indexOf( file );
    if( i < 0 )
        return false;
    SavedPlaylist p = list[i];
    normalize( p );
    p.uris << uris;
    return write( p );
}

bool PlaylistStore::removeTrack( const QString &file, int track )
{
    const int i = indexOf( file );
    if( i < 0 || track < 0 || track >= list[i].uris.size() )
        return false;
    SavedPlaylist p = list[i];
    normalize( p );
    p.uris.removeAt( track );
    p.titles.removeAt( track );
    p.images.removeAt( track );
    p.options.removeAt( track );
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
    SavedPlaylist p = list[i];
    normalize( p );
    for( int t = 0; t < p.uris.size(); t++ )
    {
        const QString &u = p.uris[t];
        if( !p.titles[t].isEmpty() || !p.images[t].isEmpty() )
        {
            out << "#EXTINF:-1";
            if( !p.images[t].isEmpty() )
                out << " tvg-logo=\"" << p.images[t] << "\"";
            out << "," << p.titles[t] << "\n";
        }
        foreach( const QString &o, p.options[t] )
            out << "#EXTVLCOPT:" << o << "\n";
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
    SavedPlaylist p = list[i];
    normalize( p );
    playlist_Clear( THEPL, pl_Unlocked );
    track = qBound( 0, track, p.uris.size() - 1 );
    for( int k = 0; k < p.uris.size(); k++ )
    {
        const int t = ( track + k ) % p.uris.size();
        if( p.options[t].isEmpty() && p.titles[t].isEmpty() )
        {
            Open::openMRL( p_intf, p.uris[t], k == 0, true );
            continue;
        }
        /* options from a playlist file stay untrusted, as when VLC reads
         * the playlist itself: unsafe ones are ignored */
        QList<QByteArray> raw;
        foreach( const QString &o, p.options[t] )
            raw << o.toUtf8();
        QVector<const char *> ppsz;
        foreach( const QByteArray &o, raw )
            ppsz << o.constData();
        const QByteArray title = p.titles[t].toUtf8();
        playlist_AddExt( THEPL, qtu( p.uris[t] ), title.isEmpty() ? NULL : title.constData(),
                         k == 0, ppsz.size(), ppsz.isEmpty() ? NULL : ppsz.data(), 0, true );
    }

    /* Home's Frequently Visited Playlists */
    const QString key = "Playlists/" + QFileInfo( file ).completeBaseName();
    getSettings()->setValue( key + "/plays", p.plays + 1 );
    getSettings()->setValue( key + "/lastPlayed", QtCompat::currentSecsSinceEpoch() );
    list[i].plays = p.plays + 1;
    list[i].lastPlayed = QtCompat::currentSecsSinceEpoch();
    emit changed();
}
