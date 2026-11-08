/*****************************************************************************
 * library_browser.cpp : browse state shared by the library page and the
 *                       player sidebar (P5a)
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

#include "library/library_browser.hpp"
#include "library/media_library.hpp"
#include "components/redesign_menus.hpp"
#include "dialogs_provider.hpp"
#include "recents.hpp"

#include <vlc_playlist.h>

#include <QCollator>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHash>
#include <QMenu>
#include <QUrl>

#include <algorithm>

LibraryBrowser::LibraryBrowser( intf_thread_t *_p_intf, const QString &key, QObject *parent )
    : QObject( parent ), p_intf( _p_intf ), lib( MediaLibrary::instance( _p_intf ) ),
      settingsKey( key )
{
    sort = getSettings()->value( settingsKey, UNWATCHED_FIRST ).toInt();

    connect( lib, &MediaLibrary::itemsChanged, this, &LibraryBrowser::rebuild );
    connect( lib, &MediaLibrary::scanFinished, this, &LibraryBrowser::rebuild );
    /* watched / progress changes: refresh the visible state, debounced */
    refresh.setSingleShot( true );
    refresh.setInterval( 400 );
    connect( &refresh, &QTimer::timeout, this, &LibraryBrowser::rebuild );
    connect( lib, &MediaLibrary::itemChanged, this, [this]() {
        if( !refresh.isActive() )
            refresh.start();
    } );
}

QString LibraryBrowser::sectionTitle( const QString &key )
{
    if( key == "home" )    return qtr( "Home" );
    if( key == "videos" )  return qtr( "Videos" );
    if( key == "music" )   return qtr( "Music" );
    if( key == "recent" )  return qtr( "Recent" );
    if( key == "shows" )   return qtr( "Shows" );
    if( key == "network" ) return qtr( "Network" );
    return key;
}

QString LibraryBrowser::segmentTitle( int i ) const
{
    const QString s = loc.value( i );
    return i == 0 || s == "shows" ? sectionTitle( s ) : s;
}

void LibraryBrowser::openSection( const QString &key )
{
    needle.clear();
    backStack.clear();
    fwdStack.clear();
    loc.clear();
    setLocation( QStringList() << key, false );
}

void LibraryBrowser::setLocation( const QStringList &l, bool record )
{
    if( record && !loc.isEmpty() && l != loc )
    {
        backStack.append( loc );
        fwdStack.clear();
    }
    loc = l;
    emit locationChanged();
    rebuild();
}

void LibraryBrowser::goBack()
{
    if( backStack.isEmpty() )
        return;
    fwdStack.prepend( loc );
    setLocation( backStack.takeLast(), false );
}

void LibraryBrowser::goForward()
{
    if( fwdStack.isEmpty() )
        return;
    backStack.append( loc );
    setLocation( fwdStack.takeFirst(), false );
}

void LibraryBrowser::setSearch( const QString &s )
{
    if( s == needle )
        return;
    needle = s;
    rebuild();
}

int LibraryBrowser::itemCount() const
{
    int n = 0;
    /* Figma counts every row but the "Add..." one ("Network: 2 items") */
    foreach( const LibraryEntry &e, current )
        if( e.type != LibraryEntry::ACTION || e.icon != "add" )
            n++;
    return n;
}

QString LibraryBrowser::sourceLabel() const
{
    return loc.size() > 1 ? segmentTitle( loc.size() - 1 ) : sectionTitle( section() );
}

void LibraryBrowser::rebuild()
{
    current = collect();
    sortEntries( current );
    emit entriesChanged();
}

/* The entries of the current location (or the search results in it) */
QVector<LibraryEntry> LibraryBrowser::collect() const
{
    const QVector<LibraryItem> &items = lib->items();
    const QString sec = section();
    const QString search = needle.trimmed();
    QVector<LibraryEntry> out;

    auto action = []( const QString &key, const QString &title, const QString &icon ) {
        LibraryEntry e;
        e.type = LibraryEntry::ACTION;
        e.segment = key;
        e.title = title;
        e.icon = icon;
        return e;
    };
    auto itemEntry = [&items]( int i ) {
        LibraryEntry e;
        e.type = LibraryEntry::ITEM;
        e.index = i;
        e.title = items[i].name();
        e.thumbFrom = items[i].path;
        e.watched = items[i].watched;
        e.progress = items[i].progressRatio();
        e.date = items[i].mtime;
        return e;
    };
    auto inSection = [&sec]( const LibraryItem &it ) {
        if( sec == "videos" ) return it.kind == LibraryItem::VIDEO;
        if( sec == "music" )  return it.kind == LibraryItem::AUDIO;
        if( sec == "recent" ) return it.lastPlayed > 0;
        if( sec == "home" )   return it.lastPlayed > 0 && !it.watched && it.progress > 0;
        return false;
    };

    /* Components "Network" panel: the sources of VLC 3's discovery */
    if( sec == "network" )
    {
        out << action( "netstream", qtr( "Add Network Stream" ), "add" )
            << action( "podcast", qtr( "Podcasts" ), "network" )
            << action( "radio", qtr( "Radio" ), "network" );
        return out;
    }

    if( !search.isEmpty() )
    {
        /* search: every item of the section whose name matches */
        for( int i = 0; i < items.size(); i++ )
            if( inSection( items[i] ) && items[i].name().contains( search, Qt::CaseInsensitive ) )
                out << itemEntry( i );
        return out;
    }

    /* Components "Videos" / "Music" panels start with "Add" */
    if( actionRows && loc.size() == 1 && ( sec == "videos" || sec == "music" ) )
        out << action( "add", qtr( "Add" ), "add" );

    /* Videos and Music: the Shows folder (videos), one folder per
     * top-level folder of a library root (Films, Anime...), then the files
     * lying in a root itself (Components "Videos": Shows, Films, Anime) */
    const bool media = sec == "videos" || sec == "music";
    QStringList roots;
    foreach( const QString &r, lib->roots() )
        roots << ( r.endsWith( '/' ) ? r : r + '/' );
    if( media && loc.size() == 1 )
    {
        LibraryEntry shows;
        shows.type = LibraryEntry::GROUP;
        shows.title = qtr( "Shows" );
        shows.segment = "shows";
        shows.icon = "folder";
        QHash<QString, int> folderAt;
        QVector<LibraryEntry> folders, files;
        for( int i = 0; i < items.size(); i++ )
        {
            const LibraryItem &it = items[i];
            if( !inSection( it ) )
                continue;
            if( sec == "videos" && it.category == "shows" )
            {
                if( shows.count++ == 0 )
                    shows.thumbFrom = it.path;
                shows.date = qMax( shows.date, it.mtime );
                continue;
            }
            const QString folder = topFolder( roots, it.path );
            if( folder.isEmpty() )
            {
                files << itemEntry( i );
                continue;
            }
            int g = folderAt.value( folder, -1 );
            if( g < 0 )
            {
                LibraryEntry e;
                e.type = LibraryEntry::GROUP;
                e.title = e.segment = folder;
                e.icon = "folder";
                e.thumbFrom = it.path;
                e.watched = true;
                g = folders.size();
                folderAt.insert( folder, g );
                folders << e;
            }
            LibraryEntry &e = folders[g];
            e.count++;
            e.watched = e.watched && it.watched;
            e.date = qMax( e.date, it.mtime );
        }
        if( shows.count > 0 )
            out << shows;
        out << folders << files;
        return out;
    }
    if( media && loc.size() == 2 && loc[1] != "shows" )
    {
        /* a folder: everything under it */
        for( int i = 0; i < items.size(); i++ )
            if( inSection( items[i] ) && items[i].category != "shows"
                && topFolder( roots, items[i].path ) == loc[1] )
                out << itemEntry( i );
        return out;
    }
    if( sec == "videos" && loc.value( 1 ) == "shows" )
    {
        const QString show = loc.value( 2 );
        const QString season = loc.value( 3 );
        QHash<QString, int> groupAt;
        for( int i = 0; i < items.size(); i++ )
        {
            const LibraryItem &it = items[i];
            if( it.category != "shows" )
                continue;
            QString key;
            if( show.isEmpty() )
                key = it.show;                 /* Shows: one per show */
            else if( it.show != show )
                continue;
            else if( season.isEmpty() )
                key = it.season;               /* a show: one per season */
            else if( it.season != season )
                continue;
            if( key.isEmpty() )
            {
                out << itemEntry( i );         /* a season: the episodes */
                continue;
            }
            int g = groupAt.value( key, -1 );
            if( g < 0 )
            {
                LibraryEntry e;
                e.type = LibraryEntry::GROUP;
                e.title = key;
                e.segment = key;
                e.icon = "folder";
                e.thumbFrom = it.path;
                e.watched = true;
                g = out.size();
                groupAt.insert( key, g );
                out << e;
            }
            LibraryEntry &e = out[g];
            e.count++;
            e.watched = e.watched && it.watched;
            e.date = qMax( e.date, it.mtime );
        }
        return out;
    }
    for( int i = 0; i < items.size(); i++ )
        if( inSection( items[i] ) )
            out << itemEntry( i );
    return out;
}

/* the first folder under the library root holding the path, or empty for
 * a file lying in a root itself */
QString LibraryBrowser::topFolder( const QStringList &roots, const QString &path )
{
    foreach( const QString &root, roots )
    {
        if( !path.startsWith( root ) )
            continue;
        const QString rel = path.mid( root.size() );
        const int slash = rel.indexOf( '/' );
        return slash < 0 ? QString() : rel.left( slash );
    }
    return QString();
}

void LibraryBrowser::sortEntries( QVector<LibraryEntry> &v ) const
{
    QCollator coll;
    coll.setNumericMode( true );
    coll.setCaseSensitivity( Qt::CaseInsensitive );
    const QString sec = section();
    const QVector<LibraryItem> &items = lib->items();
    const int key = sort;
    auto rank = []( LibraryEntry::Type t ) {
        return t == LibraryEntry::ACTION ? 0 : t == LibraryEntry::GROUP ? 1 : 2;
    };
    std::stable_sort( v.begin(), v.end(), [&]( const LibraryEntry &a, const LibraryEntry &b ) {
        if( a.type != b.type )
            return rank( a.type ) < rank( b.type );  /* actions, folders, items */
        if( a.type == LibraryEntry::ACTION )
            return false;                             /* keep their order */
        if( ( a.segment == "shows" ) != ( b.segment == "shows" )
            && a.type == LibraryEntry::GROUP && loc.size() == 1 )
            return a.segment == "shows";              /* Shows leads (Figma) */
        if( sec == "recent" || sec == "home" )
        {
            const qint64 la = a.index >= 0 ? items[a.index].lastPlayed : 0;
            const qint64 lb = b.index >= 0 ? items[b.index].lastPlayed : 0;
            if( la != lb )
                return la > lb;
        }
        switch( key )
        {
        case UNWATCHED_FIRST:
            if( a.watched != b.watched )
                return !a.watched;
            break;
        case WATCHED_FIRST:
            if( a.watched != b.watched )
                return a.watched;
            break;
        case TITLE_ZA:
            return coll.compare( a.title, b.title ) > 0;
        case NEWEST:
            if( a.date != b.date )
                return a.date > b.date;
            break;
        case OLDEST:
            if( a.date != b.date )
                return a.date < b.date;
            break;
        default:
            break;
        }
        return coll.compare( a.title, b.title ) < 0;
    } );
}

void LibraryBrowser::activate( int i )
{
    if( i < 0 || i >= current.size() )
        return;
    const LibraryEntry e = current[i];
    if( e.type == LibraryEntry::ACTION )
    {
        if( e.segment == "add" )
            addFolder( p_intf, NULL );
        else if( e.segment == "netstream" )
            THEDP->openNetDialog();
        else
            emit actionRequested( e.segment );
        return;
    }
    if( e.type == LibraryEntry::GROUP )
    {
        setLocation( loc + QStringList( e.segment ) );
        return;
    }
    /* play it, with the rest of this view after it */
    QStringList paths;
    for( int k = i; k < current.size(); k++ )
        if( current[k].type == LibraryEntry::ITEM )
            paths << lib->items()[current[k].index].path;
    play( paths );
}

void LibraryBrowser::play( const QStringList &paths )
{
    if( paths.isEmpty() )
        return;
    playlist_Clear( THEPL, pl_Unlocked );
    for( int k = 0; k < paths.size(); k++ )
    {
        const QString uri = QUrl::fromLocalFile( paths[k] ).toString( QUrl::FullyEncoded );
        const int i = lib->indexOf( paths[k] );
        QStringList options;
        /* continue where it was left */
        if( k == 0 && i >= 0 && !lib->items()[i].watched && lib->items()[i].progress > 5000 )
            options << QString( ":start-time=%1" ).arg( lib->items()[i].progress / 1000.0, 0, 'f', 1 );
        Open::openMRLwithOptions( p_intf, uri, options.isEmpty() ? NULL : &options, k == 0, true );
    }
    emit played( paths.first() );
}

void LibraryBrowser::showEntryMenu( int i, const QPoint &pos, QWidget *parent )
{
    if( i < 0 || i >= current.size() || current[i].type == LibraryEntry::ACTION )
        return;
    const LibraryEntry e = current[i];
    QMenu menu( parent );
    menu.addAction( RedesignMenus::menuIcon( "play", 20 ),
                    e.type == LibraryEntry::GROUP ? qtr( "Open" ) : qtr( "Play" ),
                    this, [this, i]() { activate( i ); } );
    if( e.type == LibraryEntry::ITEM )
    {
        const LibraryItem it = lib->items()[e.index];
        menu.addAction( RedesignMenus::menuIcon( "queue", 20 ), qtr( "Add to Queue" ), this, [this, it]() {
            Open::openMRL( p_intf, it.uri(), false, true );
        } );
        menu.addSeparator();
        menu.addAction( RedesignMenus::menuIcon( "watched", 20 ),
                        it.watched ? qtr( "Mark as Unwatched" ) : qtr( "Mark as Watched" ),
                        this, [this, it]() { lib->setWatched( it.path, !it.watched ); } );
        menu.addAction( RedesignMenus::menuIcon( "heart", 20 ),
                        it.favorite ? qtr( "Remove from Favorites" ) : qtr( "Favorite" ),
                        this, [this, it]() { lib->setFavorite( it.path, !it.favorite ); } );
        menu.addSeparator();
        menu.addAction( RedesignMenus::menuIcon( "folder", 20 ), qtr( "Show in File Explorer" ), this, [it]() {
            QDesktopServices::openUrl( QUrl::fromLocalFile( it.folder() ) );
        } );
    }
    menu.exec( pos );
}

void LibraryBrowser::showSortMenu( const QPoint &pos, QWidget *parent )
{
    struct { int key; const char *label; } opts[] = {
        { UNWATCHED_FIRST, N_( "Unwatched First" ) },
        { WATCHED_FIRST, N_( "Watched First" ) },
        { TITLE_AZ, N_( "Title A–Z" ) },
        { TITLE_ZA, N_( "Title Z–A" ) },
        { NEWEST, N_( "Newest First" ) },
        { OLDEST, N_( "Oldest First" ) },
    };
    QMenu menu( parent );
    menu.setObjectName( "VLCSortMenu" );
    for( const auto &o : opts )
    {
        QAction *a = menu.addAction( o.key == sort ? RedesignMenus::menuIcon( "check", 20 )
                                                   : RedesignMenus::menuIcon( "blank", 20 ),
                                     qtr( o.label ) );
        const int key = o.key;
        connect( a, &QAction::triggered, this, [this, key]() {
            sort = key;
            getSettings()->setValue( settingsKey, key );
            rebuild();
        } );
    }
    menu.exec( pos );
}

void LibraryBrowser::addFolder( intf_thread_t *p_intf, QWidget *parent )
{
    const QString dir = QFileDialog::getExistingDirectory(
                parent, qtr( "Add a folder to the library" ), QDir::homePath() );
    if( dir.isEmpty() )
        return;
    MediaLibrary *lib = MediaLibrary::instance( p_intf );
    QStringList roots = lib->roots();
    if( !roots.contains( dir ) )
        roots << dir;
    config_PutPsz( p_intf, "qt-library-roots", qtu( roots.join( ";" ) ) );
    config_SaveConfigFile( p_intf );
    lib->rescan();
}
