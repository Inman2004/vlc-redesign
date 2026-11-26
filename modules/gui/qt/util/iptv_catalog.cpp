/*****************************************************************************
 * iptv_catalog.cpp : Live TV lists from iptv-org, channel lists and logos
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

/* before VLC's headers: their msleep macro breaks QThread's */
#include <QThread>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "util/iptv_catalog.hpp"
#include "util/qt_compat.hpp"
#include "input_manager.hpp"

#include <vlc_access.h>
#include <vlc_interrupt.h>
#include <vlc_playlist.h>
#include <vlc_stream.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QMap>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

static const char IPTV_BASE[] = "https://iptv-org.github.io/iptv/";
static const char IPTV_INDEX[] =
    "https://raw.githubusercontent.com/iptv-org/iptv/master/PLAYLISTS.md";
/* any of its servers (round-robin DNS), as Radio Browser asks */
static const char RADIO_API[] = "https://all.api.radio-browser.info/json/";
/* hidebroken: only stations that passed the last check */
static const char RADIO_STATIONS[] = "?hidebroken=true&order=votes&reverse=true&limit=500";

/* Tamil and Indian lists kept by their authors on GitHub, what
 * iptv-org's own lists leave out: Sun TV, KTV, Star Vijay, Zee Tamil,
 * Colors Tamil, Jaya TV, History TV18 Tamil, Hungama, Nick, Cartoon
 * Network, and the local Tamil Nadu channels. First the Tamil ones. */
/* Each shows inside the iptv-org list it belongs with (home): the
 * Tamil ones in By Language > Tamil, the Indian ones in By Country > India. */
static const struct { const char *id, *title, *url, *home; } featuredLists[] = {
    { "pocket-tv", "Pocket TV",
      "https://raw.githubusercontent.com/jgog/My-IPTV/main/playlist.m3u", "languages/tam" },
    { "tamil-local", "Tamil Local Cable",
      "https://raw.githubusercontent.com/amazeyourself/tamil-local-iptv/main/channels.m3u", "languages/tam" },
    { "tamil-news", "Tamil News",
      "https://raw.githubusercontent.com/naveenland4/News_Channels/main/news_channels.m3u", "languages/tam" },
    { "tamil-world", "Tamil World",
      "https://raw.githubusercontent.com/snwebdev211-jpg/Tamil-World-IPTV/main/playlists/all-sources.m3u", "languages/tam" },
    { "pavish", "Pavish Family TV",
      "https://raw.githubusercontent.com/Vishvakumar/pavish-family-tv/main/playlist-full.m3u", "languages/tam" },
    { "india-live", "India Live TV",
      "https://raw.githubusercontent.com/Diljan707/new/main/playlist.m3u", "countries/in" },
    { "indian-tv", "Indian TV",
      "https://raw.githubusercontent.com/krshnaprsad/tv-playlist/main/tv-playlist.m3u", "countries/in" },
    { "india-vip", "India VIP",
      "https://raw.githubusercontent.com/Mrbotrx/BD-OTT-PLAYLIST/main/india/India_vip.m3u", "countries/in" },
};

/* every feed of every channel, with its languages (~8 MB) */
static const char IPTV_FEEDS[] = "https://iptv-org.github.io/api/feeds.json";

enum
{
    LIST_MAX   = 32 << 20,      /* the whole index is ~3 MB */
    INDEX_MAX  = 4 << 20,
    LOGO_MAX   = 2 << 20,
    LOGO_JOBS  = 4,             /* logo downloads at a time */
    LOGO_CACHE = 400,           /* decoded logos kept in memory */
};

/*********************************************************************
 * IptvFetch
 *********************************************************************/
IptvFetch::IptvFetch( intf_thread_t *_p_intf, const QString &u, int max )
    : p_intf( _p_intf ), url( u ), maxBytes( max ), ctx( vlc_interrupt_create() )
{
}

IptvFetch::~IptvFetch()
{
    cancel();
    if( thread )
    {
        thread->wait();
        delete thread;
    }
    if( ctx )
        vlc_interrupt_destroy( ctx );
}

void IptvFetch::start()
{
    thread = QtCompat::createThread( [this]() { run(); } );
    thread->start();
}

void IptvFetch::cancel()
{
    if( ctx )
        vlc_interrupt_kill( ctx );
}

void IptvFetch::run()
{
    if( ctx )
        vlc_interrupt_set( ctx );
    QByteArray out;
    /* its own object: a background download never pops a dialog */
    vlc_object_t *obj = static_cast<vlc_object_t *>( vlc_object_create( p_intf, sizeof( vlc_object_t ) ) );
    if( obj )
    {
        obj->obj.flags |= OBJECT_FLAGS_NOINTERACT;
        /* the access alone: VLC's stream filters would turn an M3U into
         * a playlist (a folder), and we want its text */
        stream_t *s = vlc_access_NewMRL( obj, qtu( url ) );
        if( s )
        {
            char buf[16384];
            for( ;; )
            {
                const ssize_t n = vlc_stream_Read( s, buf, sizeof( buf ) );
                if( n <= 0 )
                    break;
                out.append( buf, n );
                if( out.size() > maxBytes )
                {
                    out.clear();
                    break;
                }
            }
            vlc_stream_Delete( s );
        }
        vlc_object_release( obj );
    }
    if( ctx )
        vlc_interrupt_set( NULL );
    emit fetched( out );
}

/*********************************************************************
 * IptvCatalog
 *********************************************************************/
static QPointer<IptvCatalog> catalog;

IptvCatalog *IptvCatalog::instance( intf_thread_t *p_intf )
{
    if( !catalog )
        catalog = new IptvCatalog( p_intf, THEMIM );
    return catalog;
}

QString IptvCatalog::listUrl( const QString &path )
{
    if( path.startsWith( "url:" ) )
        return path.mid( 4 );
    if( path == "radio/top" )
        return QString( RADIO_API ) + "stations/topvote/200";
    if( path.startsWith( "radio/language/" ) )
        return QString( RADIO_API ) + "stations/bylanguageexact/"
             + QString::fromLatin1( QUrl::toPercentEncoding( path.mid( 15 ) ) ) + RADIO_STATIONS;
    if( path.startsWith( "radio/country/" ) )
        return QString( RADIO_API ) + "stations/bycountrycodeexact/" + path.mid( 14 ) + RADIO_STATIONS;
    if( path.startsWith( "featured/" ) )
    {
        const QString id = path.mid( 9 );
        for( const auto &f : featuredLists )
            if( id == f.id )
                return f.url;
        return QString();
    }
    return QString( IPTV_BASE ) + path + ".m3u";
}

IptvCatalog::IptvCatalog( intf_thread_t *_p_intf, QObject *parent )
    : QObject( parent ), p_intf( _p_intf )
{
    char *psz = config_GetUserDir( VLC_CACHE_DIR );
    cacheDir = qfu( psz ) + "/iptv";
    free( psz );
    QDir().mkpath( cacheDir + "/logos" );
    builtInLists();
    QFile f( cacheDir + "/PLAYLISTS.md" );
    if( f.open( QIODevice::ReadOnly ) )
        parseLists( f.readAll() );
    refreshLists();
    /* the playlist's own copy of the option, to turn on for channels */
    var_Create( THEPL, "play-and-stop", VLC_VAR_BOOL | VLC_VAR_DOINHERIT );
    connect( THEMIM, &MainInputManager::inputChanged, this, &IptvCatalog::inputChanged );
}

IptvCatalog::~IptvCatalog()
{
    /* stop the downloads before the interface goes */
    foreach( IptvFetch *j, jobs )
        j->cancel();
    foreach( IptvFetch *j, jobs )
    {
        j->disconnect( this );
        delete j;               /* waits for its thread */
    }
}

void IptvCatalog::fetch( const QString &url, int maxBytes,
                         std::function<void( const QByteArray & )> done )
{
    IptvFetch *j = new IptvFetch( p_intf, url, maxBytes );
    jobs << j;
    connect( j, &IptvFetch::fetched, this, [this, j, done]( const QByteArray &data ) {
        done( data );
        jobs.removeAll( j );
        j->deleteLater();
    } );
    j->start();
}

/* the index once a day; the cached copy (or the built-in lists) until then */
void IptvCatalog::refreshLists()
{
    const QFileInfo fi( cacheDir + "/PLAYLISTS.md" );
    if( fi.exists() && fi.lastModified().secsTo( QDateTime::currentDateTime() ) < 24 * 3600 )
        return;
    fetch( IPTV_INDEX, INDEX_MAX, [this]( const QByteArray &data ) {
        if( !data.contains( "iptv-org.github.io/iptv/" ) )
        {
            msg_Dbg( p_intf, "Live TV: could not refresh the iptv-org index" );
            return;
        }
        QSaveFile f( cacheDir + "/PLAYLISTS.md" );
        if( f.open( QIODevice::WriteOnly ) )
        {
            f.write( data );
            f.commit();
        }
        parseLists( data );
    } );
}

static const struct { const char *id, *title; } builtInCategories[] = {
    { "animation", N_( "Animation" ) }, { "auto", N_( "Auto" ) },
    { "business", N_( "Business" ) }, { "classic", N_( "Classic" ) },
    { "comedy", N_( "Comedy" ) }, { "cooking", N_( "Cooking" ) },
    { "culture", N_( "Culture" ) }, { "documentary", N_( "Documentary" ) },
    { "education", N_( "Education" ) }, { "entertainment", N_( "Entertainment" ) },
    { "family", N_( "Family" ) }, { "general", N_( "General" ) },
    { "interactive", N_( "Interactive" ) }, { "kids", N_( "Kids" ) },
    { "legislative", N_( "Legislative" ) }, { "lifestyle", N_( "Lifestyle" ) },
    { "movies", N_( "Movies" ) }, { "music", N_( "Music" ) },
    { "news", N_( "News" ) }, { "outdoor", N_( "Outdoor" ) },
    { "public", N_( "Public" ) }, { "relax", N_( "Relax" ) },
    { "religious", N_( "Religious" ) }, { "science", N_( "Science" ) },
    { "series", N_( "Series" ) }, { "shop", N_( "Shopping" ) },
    { "sports", N_( "Sports" ) }, { "travel", N_( "Travel" ) },
    { "weather", N_( "Weather" ) }, { "woman", N_( "Women" ) },
    { "undefined", N_( "Other" ) },
};

QString IptvCatalog::flagUrl( const QString &code )
{
    return code.size() == 2 ? QString( "https://flagcdn.com/w160/%1.png" ).arg( code ) : QString();
}

/* the curated Tamil and Indian lists, as paths the catalog fetches */
QVector<IptvList> IptvCatalog::featured( const QString &home ) const
{
    QVector<IptvList> out;
    for( const auto &f : featuredLists )
    {
        if( !home.isEmpty() && home != QLatin1String( f.home ) )
            continue;
        IptvList l;
        l.path = QString( "featured/" ) + f.id;
        l.title = QString::fromUtf8( f.title );
        out << l;
    }
    return out;
}

/* offline: the categories, and the countries Qt knows (no languages:
 * iptv-org names them by ISO 639-3 code, which Qt 5 cannot give) */
void IptvCatalog::builtInLists()
{
    cats.clear();
    for( const auto &c : builtInCategories )
    {
        IptvList l;
        l.path = QString( "categories/" ) + c.id;
        l.title = qtr( c.title );
        cats << l;
    }
    QMap<QString, QString> byName;
    foreach( const QLocale &loc, QLocale::matchingLocales( QLocale::AnyLanguage, QLocale::AnyScript,
                                                           QLocale::AnyCountry ) )
    {
        const QString name = loc.name();
        const int us = name.lastIndexOf( '_' );
        if( us < 0 || loc.country() == QLocale::AnyCountry )
            continue;
        QString code = name.mid( us + 1 ).toLower();
        if( code.size() != 2 )
            continue;               /* "419" (Latin America) and such */
        if( code == "gb" )
            code = "uk";            /* iptv-org's code for the UK */
        byName.insert( QLocale::countryToString( loc.country() ), code );
    }
    ctrs.clear();
    for( auto it = byName.constBegin(); it != byName.constEnd(); ++it )
    {
        IptvList l;
        l.path = "countries/" + it.value();
        l.title = it.key();
        l.code = it.value() == "uk" ? QString( "gb" ) : it.value();
        ctrs << l;
    }
}

/* PLAYLISTS.md: HTML table rows for categories and languages
 * (name, channels, URL), a "- <flag> Name <code>URL</code>" list for
 * countries */
void IptvCatalog::parseLists( const QByteArray &data )
{
    const QString md = QString::fromUtf8( data );
    static const QRegularExpression row(
        "<td[^>]*>([^<]+)</td>\\s*<td[^>]*>(\\d+)</td>\\s*<td[^>]*><code>"
        "https://iptv-org\\.github\\.io/iptv/((categories|languages)/[^<.]+)\\.m3u</code>" );
    static const QRegularExpression country(
        "^- (\\S+) (.+?) <code>https://iptv-org\\.github\\.io/iptv/(countries/[a-z]+)\\.m3u</code>",
        QRegularExpression::MultilineOption );
    QVector<IptvList> c, l, k;
    QRegularExpressionMatchIterator it = row.globalMatch( md );
    while( it.hasNext() )
    {
        const QRegularExpressionMatch m = it.next();
        IptvList e;
        e.path = m.captured( 3 );
        e.title = m.captured( 1 ).trimmed();
        e.count = m.captured( 2 ).toInt();
        if( e.path.startsWith( "categories/" ) )
        {
            if( e.path == "categories/xxx" )
                continue;           /* adult channels: not listed */
            for( const auto &b : builtInCategories )
                if( e.path.mid( 11 ) == b.id )
                    e.title = qtr( b.title );
            c << e;
        }
        else
            l << e;
    }
    it = country.globalMatch( md );
    while( it.hasNext() )
    {
        const QRegularExpressionMatch m = it.next();
        IptvList e;
        e.title = m.captured( 2 ).trimmed();
        e.path = m.captured( 3 );
        e.code = e.path.mid( 10 ) == "uk" ? QString( "gb" ) : e.path.mid( 10 );
        k << e;
    }
    if( c.isEmpty() && l.isEmpty() && k.isEmpty() )
        return;                     /* not the format we know: keep what we have */
    if( !c.isEmpty() )
        cats = c;
    if( !l.isEmpty() )
        langs = l;
    if( !k.isEmpty() )
        ctrs = k;
    emit changed();
}

/* the channels of an M3U: #EXTINF attributes and name, #EXTVLCOPT, URL */
static QVector<IptvChannel> parseM3u( const QByteArray &data )
{
    QVector<IptvChannel> out;
    static const QRegularExpression attr( "([A-Za-z0-9_-]+)=\"([^\"]*)\"" );
    IptvChannel cur;
    foreach( QByteArray raw, data.split( '\n' ) )
    {
        const QString line = QString::fromUtf8( raw ).trimmed();
        if( line.isEmpty() )
            continue;
        if( line.startsWith( "#EXTINF:" ) )
        {
            cur = IptvChannel();
            /* the name follows the first comma outside quotes */
            bool quoted = false;
            int comma = -1;
            for( int i = 8; i < line.size(); i++ )
            {
                if( line[i] == '"' )
                    quoted = !quoted;
                else if( line[i] == ',' && !quoted )
                {
                    comma = i;
                    break;
                }
            }
            const QString head = comma < 0 ? line : line.left( comma );
            cur.title = comma < 0 ? QString() : line.mid( comma + 1 ).trimmed();
            QRegularExpressionMatchIterator it = attr.globalMatch( head );
            QString tvgName;
            while( it.hasNext() )
            {
                const QRegularExpressionMatch m = it.next();
                const QString k = m.captured( 1 ).toLower();
                if( k == "tvg-logo" )
                    cur.logo = m.captured( 2 );
                else if( k == "group-title" )
                    cur.group = m.captured( 2 );
                else if( k == "tvg-id" )
                    cur.id = m.captured( 2 );
                else if( k == "tvg-name" )
                    tvgName = m.captured( 2 );
            }
            if( cur.title.isEmpty() )
                cur.title = tvgName;
        }
        else if( line.startsWith( "#EXTVLCOPT:" ) )
            cur.options << line.mid( 11 );
        else if( !line.startsWith( '#' ) )
        {
            cur.url = line;
            if( cur.title.isEmpty() )
                cur.title = line;
            out << cur;
            cur = IptvChannel();
        }
    }
    return out;
}

/* Radio Browser's station JSON: name, stream, logo, genre tags */
static QVector<IptvChannel> parseStations( const QByteArray &data )
{
    QVector<IptvChannel> out;
    const QJsonArray a = QJsonDocument::fromJson( data ).array();
    QSet<QString> seen;
    foreach( const QJsonValue &v, a )
    {
        const QJsonObject o = v.toObject();
        IptvChannel c;
        c.title = o.value( "name" ).toString().trimmed();
        c.url = o.value( "url_resolved" ).toString();
        if( c.url.isEmpty() )
            c.url = o.value( "url" ).toString();
        if( c.title.isEmpty() || c.url.isEmpty() || seen.contains( c.url ) )
            continue;
        seen.insert( c.url );
        c.logo = o.value( "favicon" ).toString();
        /* the first genres it lists, like "news, talk" */
        QStringList tags = o.value( "tags" ).toString().split( ',', QString::SkipEmptyParts );
        for( QString &t : tags )
            t = t.trimmed();
        c.group = QStringList( tags.mid( 0, 2 ) ).join( ";" );
        out << c;
    }
    return out;
}

QVector<IptvChannel> IptvCatalog::channels( const QString &path, bool *loading, bool *failed )
{
    if( loading )
        *loading = false;
    if( failed )
        *failed = false;
    if( lists.contains( path ) )
        return visible( path );
    if( listsFailed.contains( path ) )
    {
        if( failed )
            *failed = true;
        listsFailed.remove( path );     /* the next visit tries again */
        return QVector<IptvChannel>();
    }
    if( loading )
        *loading = true;
    if( listsLoading.contains( path ) )
        return QVector<IptvChannel>();
    listsLoading.insert( path );
    if( path.startsWith( "languages/" ) )
        loadMainLanguages();
    fetch( listUrl( path ), LIST_MAX, [this, path]( const QByteArray &data ) {
        listsLoading.remove( path );
        const QVector<IptvChannel> ch = path.startsWith( "radio/" ) ? parseStations( data )
                                                                   : parseM3u( data );
        if( ch.isEmpty() )
            listsFailed.insert( path );
        else
        {
            if( lists.size() > 8 )      /* "All channels" alone is 11,000 */
                lists.clear();
            lists.insert( path, ch );
        }
        emit channelsReady( path );
    } );
    return QVector<IptvChannel>();
}

/* a language list without the channels that only carry the language as
 * a second audio track (Movies Now, NDTV Good Times, MTA6 Asia in Tamil):
 * their main language, by iptv-org's feeds, is another one */
QVector<IptvChannel> IptvCatalog::visible( const QString &path ) const
{
    const QVector<IptvChannel> all = lists.value( path );
    if( !path.startsWith( "languages/" ) || mainLang.isEmpty() )
        return all;
    const QString code = path.mid( 10 );
    QVector<IptvChannel> out;
    foreach( const IptvChannel &c, all )
    {
        const QString main = mainLang.value( c.id );
        if( main.isEmpty() || main == code )
            out << c;
    }
    return out.isEmpty() ? all : out;
}

/* the main languages, from the cached table (a day old at most), else
 * from iptv-org's feeds; the open language lists follow */
void IptvCatalog::loadMainLanguages()
{
    if( mainLangFetching )
        return;
    const QString file = cacheDir + "/main-languages.tsv";
    const QFileInfo fi( file );
    if( !mainLangLoaded && fi.exists() )
    {
        QFile f( file );
        if( f.open( QIODevice::ReadOnly ) )
            foreach( const QByteArray &line, f.readAll().split( '\n' ) )
            {
                const int tab = line.indexOf( '\t' );
                if( tab > 0 )
                    mainLang.insert( QString::fromUtf8( line.left( tab ) ),
                                     QString::fromLatin1( line.mid( tab + 1 ) ) );
            }
        mainLangLoaded = true;
    }
    if( fi.exists() && fi.lastModified().secsTo( QDateTime::currentDateTime() ) < 24 * 3600 )
        return;
    mainLangFetching = true;
    fetch( IPTV_FEEDS, LIST_MAX, [this]( const QByteArray &data ) {
        parseFeeds( data );
    } );
}

void IptvCatalog::parseFeeds( const QByteArray &data )
{
    const QJsonArray feeds = QJsonDocument::fromJson( data ).array();
    if( feeds.isEmpty() )
    {
        msg_Dbg( p_intf, "Live TV: could not read iptv-org's feeds" );
        return;     /* mainLangFetching stays: no retry until next launch */
    }
    mainLangFetching = false;
    QHash<QString, QString> m;
    QByteArray table;
    foreach( const QJsonValue &v, feeds )
    {
        const QJsonObject o = v.toObject();
        const QJsonArray langs = o.value( "languages" ).toArray();
        if( langs.size() < 2 )
            continue;           /* one language: its lists are right */
        const QString channel = o.value( "channel" ).toString();
        const QString main = langs.first().toString();
        QStringList keys( channel + "@" + o.value( "id" ).toString() );
        if( o.value( "is_main" ).toBool() )
            keys << channel;    /* a tvg-id without a feed is the main one */
        foreach( const QString &k, keys )
        {
            m.insert( k, main );
            table += k.toUtf8() + '\t' + main.toLatin1() + '\n';
        }
    }
    mainLang = m;
    mainLangLoaded = true;
    QSaveFile f( cacheDir + "/main-languages.tsv" );
    if( f.open( QIODevice::WriteOnly ) )
    {
        f.write( table );
        f.commit();
    }
    foreach( const QString &path, lists.keys() )
        if( path.startsWith( "languages/" ) )
            emit channelsReady( path );
}

/* an item's MRL, locked */
static QString itemUri( playlist_item_t *it )
{
    char *uri = input_item_GetURI( it->p_input );
    const QString s = qfu( uri );
    free( uri );
    return s;
}

void IptvCatalog::play( const QString &path, int from )
{
    const QVector<IptvChannel> ch = visible( path );
    if( ch.isEmpty() )
        return;
    from = qBound( 0, from, ch.size() - 1 );
    playlist_t *pl = THEPL;
    playlist_Lock( pl );
    playlist_item_t *root = pl->p_playing;

    /* the queue already is this list: zap, nothing else changes */
    if( path == queuedPath && root->i_children == ch.size() )
    {
        playlist_item_t *it = root->pp_children[from];
        if( it->i_children < 0 && itemUri( it ) == ch[from].url )
        {
            playlist_ViewPlay( pl, NULL, it );
            playlist_Unlock( pl );
            return;
        }
    }

    /* a new queue: the channels after the old items, then the old ones
     * go while the chosen channel takes over. Clearing first would stop
     * the player, drop the video window (and full screen) and start it
     * all again. */
    QVector<int> old;
    for( int i = 0; i < root->i_children; i++ )
        old << root->pp_children[i]->i_id;
    playlist_item_t *start = NULL;
    queuedUrls.clear();
    for( int i = 0; i < ch.size(); i++ )
    {
        const IptvChannel &c = ch[i];
        /* options from the internet stay untrusted, as when VLC reads
         * the M3U itself: unsafe ones are ignored */
        QList<QByteArray> raw;
        foreach( const QString &o, c.options )
            raw << o.toUtf8();
        QVector<const char *> opts;
        foreach( const QByteArray &o, raw )
            opts << o.constData();
        input_item_t *in = input_item_New( qtu( c.url ), qtu( c.title ) );
        if( !in )
            continue;
        input_item_AddOptions( in, opts.size(), opts.isEmpty() ? NULL : opts.data(), 0 );
        if( !c.logo.isEmpty() )
            input_item_SetArtURL( in, qtu( c.logo ) );
        if( !c.group.isEmpty() )
            input_item_SetAlbum( in, qtu( c.group ) );
        playlist_item_t *it = playlist_NodeAddInput( pl, in, root, PLAYLIST_END );
        if( it && i == from )
            start = it;
        input_item_Release( in );
        queuedUrls.insert( c.url );
    }
    queuedPath = path;
    /* marked live before it plays: the window must not jump to its size */
    live = start != NULL;
    if( start )
    {
        stopAfterItem( true );
        playlist_ViewPlay( pl, NULL, start );
    }

    /* the old items, except the one still playing: it goes once the
     * chosen channel has taken over (inputChanged) */
    playlist_item_t *playing = playlist_CurrentPlayingItem( pl );
    foreach( int id, old )
    {
        playlist_item_t *it = playlist_ItemGetById( pl, id );
        if( it && it != playing )
            playlist_NodeDelete( pl, it );
        else if( it )
            leftover = id;
    }
    playlist_Unlock( pl );
}

bool IptvCatalog::liveQueue()
{
    return catalog && catalog->live;
}

/* while a channel of the list plays: no moving on to the next one by
 * itself (a channel that fails or drops stops there: it is the one you
 * picked, Next zaps), and the window keeps its size */
void IptvCatalog::inputChanged()
{
    playlist_t *pl = THEPL;
    playlist_Lock( pl );
    playlist_item_t *cur = playlist_CurrentPlayingItem( pl );
    const QString uri = cur ? itemUri( cur ) : QString();
    const int curId = cur ? cur->i_id : -1;
    if( leftover >= 0 && leftover != curId )
    {
        /* the item that played before the list: the list has taken over */
        playlist_item_t *it = playlist_ItemGetById( pl, leftover );
        if( it )
            playlist_NodeDelete( pl, it );
        leftover = -1;
    }
    playlist_Unlock( pl );

    if( !cur )
        return;         /* stopped: the queue may still be the list */
    const bool wasLive = live;
    live = !queuedUrls.isEmpty() && queuedUrls.contains( uri );
    if( !live )
    {
        queuedUrls.clear();
        queuedPath.clear();
    }
    if( live || wasLive )
        stopAfterItem( live );
}

/* VLC's "play and stop" while a channel plays, your setting otherwise */
void IptvCatalog::stopAfterItem( bool b )
{
    var_SetBool( THEPL, "play-and-stop", b || var_InheritBool( p_intf, "play-and-stop" ) );
}

/*********************************************************************
 * Radio Browser's languages and countries
 *********************************************************************/
QVector<IptvList> IptvCatalog::parseRadioLists( const QByteArray &data, bool countries ) const
{
    QVector<IptvList> out;
    const QJsonArray a = QJsonDocument::fromJson( data ).array();
    foreach( const QJsonValue &v, a )
    {
        const QJsonObject o = v.toObject();
        const QString name = o.value( "name" ).toString().trimmed();
        const int n = o.value( "stationcount" ).toInt();
        if( name.isEmpty() || n < 2 )
            continue;               /* hundreds of one-station spellings */
        IptvList l;
        l.count = n;
        if( countries )
        {
            const QString cc = o.value( "iso_3166_1" ).toString();
            if( cc.size() != 2 )
                continue;
            l.path = "radio/country/" + cc;
            l.code = cc.toLower();
            /* "The United States Of America": the Live TV name reads better */
            l.title = name;
            foreach( const IptvList &c, ctrs )
                if( c.code == l.code || ( l.code == "gb" && c.code == "gb" ) )
                    l.title = c.title;
        }
        else
        {
            /* real languages carry an ISO 639 code; the rest are tags
             * people typed ("80s", "#japan") */
            if( o.value( "iso_639" ).toString().isEmpty() )
                continue;
            l.path = "radio/language/" + name;
            l.title = name;
            l.title[0] = l.title[0].toUpper();
        }
        out << l;
    }
    return out;
}

void IptvCatalog::fetchRadioLists()
{
    static const char *files[] = { "radio-languages.json", "radio-countries.json" };
    static const char *apis[] = { "languages", "countries" };
    for( int k = 0; k < 2; k++ )
    {
        QFile f( cacheDir + "/" + files[k] );
        if( ( k ? radioCtrs : radioLangs ).isEmpty() && f.open( QIODevice::ReadOnly ) )
            ( k ? radioCtrs : radioLangs ) = parseRadioLists( f.readAll(), k );
    }
    const QFileInfo fi( cacheDir + "/" + files[1] );
    if( radioFetching
     || ( fi.exists() && fi.lastModified().secsTo( QDateTime::currentDateTime() ) < 24 * 3600 ) )
        return;
    radioFetching = true;
    for( int k = 0; k < 2; k++ )
    {
        const QString file = cacheDir + "/" + files[k];
        fetch( QString( RADIO_API ) + apis[k] + "?hidebroken=true", INDEX_MAX,
               [this, k, file]( const QByteArray &data ) {
            const QVector<IptvList> l = parseRadioLists( data, k );
            if( k == 1 )
                radioFetching = false;
            if( l.isEmpty() )
                return;
            QSaveFile f( file );
            if( f.open( QIODevice::WriteOnly ) )
            {
                f.write( data );
                f.commit();
            }
            ( k ? radioCtrs : radioLangs ) = l;
            emit changed();
        } );
    }
}

QVector<IptvList> IptvCatalog::radioLanguages()
{
    fetchRadioLists();
    return radioLangs;
}

QVector<IptvList> IptvCatalog::radioCountries()
{
    fetchRadioLists();
    return radioCtrs;
}

/*********************************************************************
 * Your own lists: "title<US>url" entries in the Qt settings
 *********************************************************************/
QVector<IptvList> IptvCatalog::myLists() const
{
    QVector<IptvList> out;
    foreach( const QString &e, getSettings()->value( "LiveTV/myLists" ).toStringList() )
    {
        const int sep = e.indexOf( QChar( 0x1F ) );
        if( sep < 0 )
            continue;
        IptvList l;
        l.title = e.left( sep );
        l.path = "url:" + e.mid( sep + 1 );
        out << l;
    }
    return out;
}

static void saveMyLists( QSettings *settings, const QVector<IptvList> &lists )
{
    QStringList out;
    foreach( const IptvList &l, lists )
        out << l.title + QChar( 0x1F ) + l.path.mid( 4 );
    settings->setValue( "LiveTV/myLists", out );
}

void IptvCatalog::addMyList( const QString &title, const QString &url )
{
    QVector<IptvList> l = myLists();
    foreach( const IptvList &e, l )
        if( e.path == "url:" + url )
            return;
    IptvList n;
    n.title = title;
    n.path = "url:" + url;
    l << n;
    saveMyLists( getSettings(), l );
    emit changed();
}

void IptvCatalog::renameMyList( const QString &path, const QString &title )
{
    QVector<IptvList> l = myLists();
    for( IptvList &e : l )
        if( e.path == path )
            e.title = title;
    saveMyLists( getSettings(), l );
    emit changed();
}

void IptvCatalog::removeMyList( const QString &path )
{
    QVector<IptvList> l = myLists();
    for( int i = l.size() - 1; i >= 0; i-- )
        if( l[i].path == path )
            l.remove( i );
    saveMyLists( getSettings(), l );
    lists.remove( path );
    emit changed();
}

/*********************************************************************
 * Logos
 *********************************************************************/
static QString logoFile( const QString &dir, const QString &url )
{
    return dir + "/logos/" + QString::fromLatin1(
        QCryptographicHash::hash( url.toUtf8(), QCryptographicHash::Sha1 ).toHex() );
}

QPixmap IptvCatalog::logo( const QString &url, const QSize &size, qreal dpr )
{
    if( url.isEmpty() || logosFailed.contains( url ) )
        return QPixmap();
    const QString key = QString( "%1@%2x%3x%4" ).arg( url ).arg( size.width() )
                                                 .arg( size.height() ).arg( dpr );
    QHash<QString, QPixmap>::const_iterator f = fitted.constFind( key );
    if( f != fitted.constEnd() )
        return f.value();
    QImage img = logos.value( url );
    if( img.isNull() )
    {
        /* from the disk cache, else download it */
        QImage disk( logoFile( cacheDir, url ) );
        if( disk.isNull() )
        {
            if( !logoQueue.contains( url ) && ( url.startsWith( "http://" ) || url.startsWith( "https://" ) ) )
            {
                logoQueue.prepend( url );   /* the latest asked first */
                pumpLogos();
            }
            return QPixmap();
        }
        if( logos.size() > LOGO_CACHE )
            logos.clear();
        logos.insert( url, disk );
        img = disk;
    }
    /* the logo whole, fitted in the tile with some room around it */
    const QSize px = size * dpr;
    const QSize box( px.width() * 3 / 4, px.height() * 3 / 4 );
    const QImage s = img.scaled( box, Qt::KeepAspectRatio, Qt::SmoothTransformation );
    QPixmap out( px );
    out.fill( Qt::transparent );
    {
        QPainter p( &out );
        p.drawImage( ( px.width() - s.width() ) / 2, ( px.height() - s.height() ) / 2, s );
    }
    out.setDevicePixelRatio( dpr );
    if( fitted.size() > LOGO_CACHE )
        fitted.clear();
    fitted.insert( key, out );
    return out;
}

void IptvCatalog::pumpLogos()
{
    while( logoJobs < LOGO_JOBS && !logoQueue.isEmpty() )
    {
        const QString url = logoQueue.takeFirst();
        logoJobs++;
        fetch( url, LOGO_MAX, [this, url]( const QByteArray &data ) {
            logoJobs--;
            QImage img;
            if( !data.isEmpty() && img.loadFromData( data ) && !img.isNull() )
            {
                /* keep a small copy: tiles never need more */
                if( img.width() > 512 || img.height() > 512 )
                    img = img.scaled( 512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation );
                img.save( logoFile( cacheDir, url ), "PNG" );
                logos.insert( url, img );
                emit logoReady( url );
            }
            else
                logosFailed.insert( url );
            pumpLogos();
        } );
    }
    /* the queue only matters for what is on screen now */
    while( logoQueue.size() > 200 )
        logoQueue.removeLast();
}
