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

static const char IPTV_BASE[] = "https://iptv-org.github.io/iptv/";
static const char IPTV_INDEX[] =
    "https://raw.githubusercontent.com/iptv-org/iptv/master/PLAYLISTS.md";

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
IptvCatalog *IptvCatalog::instance( intf_thread_t *p_intf )
{
    static QPointer<IptvCatalog> catalog;
    if( !catalog )
        catalog = new IptvCatalog( p_intf, THEMIM );
    return catalog;
}

QString IptvCatalog::listUrl( const QString &path )
{
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

QVector<IptvChannel> IptvCatalog::channels( const QString &path, bool *loading, bool *failed )
{
    if( loading )
        *loading = false;
    if( failed )
        *failed = false;
    if( lists.contains( path ) )
        return lists.value( path );
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
    fetch( listUrl( path ), LIST_MAX, [this, path]( const QByteArray &data ) {
        listsLoading.remove( path );
        const QVector<IptvChannel> ch = parseM3u( data );
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

void IptvCatalog::play( const QString &path, int from )
{
    const QVector<IptvChannel> ch = lists.value( path );
    if( ch.isEmpty() )
        return;
    from = qBound( 0, from, ch.size() - 1 );
    playlist_Clear( THEPL, pl_Unlocked );
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
        playlist_AddInput( THEPL, in, i == from, true );
        input_item_Release( in );
    }
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
