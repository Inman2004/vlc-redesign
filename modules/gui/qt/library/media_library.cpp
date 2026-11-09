/*****************************************************************************
 * media_library.cpp : Qt-side media library store (P5a)
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

/* before any VLC header: vlc_threads.h defines an msleep() macro that
 * breaks QThread::msleep's declaration */
#include <QThread>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "library/media_library.hpp"
#include "util/frame_grabber.hpp"
#include "util/audio_scanner.hpp"
#include "input_manager.hpp"
#include "recents.hpp"

#include <vlc_configuration.h>
#include <vlc_input.h>
#include <vlc_url.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#define STORE_VERSION     1
#define THUMB_W           320
#define THUMB_H           180
#define THUMB_GRABBERS    2
#define THUMB_MAX_TRIES   2
#define WATCHED_RATIO     0.92

static const QSet<QString> &videoExtensions()
{
    static const QSet<QString> s = {
        "mp4", "m4v", "mkv", "webm", "avi", "mov", "wmv", "flv", "mpg", "mpeg",
        "ts", "m2ts", "mts", "ogv", "3gp", "vob", "divx", "asf", "rm", "rmvb",
    };
    return s;
}

static const QSet<QString> &audioExtensions()
{
    static const QSet<QString> s = {
        "mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "wav", "wma", "alac",
        "ape", "mka", "aiff", "aif", "wv", "mpc",
    };
    return s;
}

static QString userDir( vlc_userdir_t type )
{
    char *dir = config_GetUserDir( type );
    const QString s = dir ? qfu( dir ) : QString();
    free( dir );
    return s;
}

/**********************************************************************
 * LibraryItem
 **********************************************************************/
QString LibraryItem::uri() const
{
    return QUrl::fromLocalFile( path ).toString( QUrl::FullyEncoded );
}

QString LibraryItem::name() const
{
    return QFileInfo( path ).fileName();
}

QString LibraryItem::songTitle() const
{
    return title.isEmpty() ? QFileInfo( path ).completeBaseName() : title;
}

QString LibraryItem::folder() const
{
    return QFileInfo( path ).absolutePath();
}

qreal LibraryItem::progressRatio() const
{
    if( duration <= 0 || progress <= 0 )
        return 0.0;
    return qBound( 0.0, qreal( progress ) / duration, 1.0 );
}

void MediaLibrary::classify( LibraryItem &it )
{
    it.show.clear();
    it.season.clear();
    if( it.kind == LibraryItem::AUDIO )
    {
        it.category = "music";
        return;
    }
    const QFileInfo fi( it.path );
    static const QRegularExpression sxe( "[Ss](\\d{1,2})[ ._-]?[Ee](\\d{1,3})" );
    static const QRegularExpression nxn( "(?:^|[ ._-])(\\d{1,2})x(\\d{2,3})(?:[ ._-]|$)" );
    static const QRegularExpression seasonDir( "^(?:season|series|s)[ ._-]?(\\d{1,2})$",
                                               QRegularExpression::CaseInsensitiveOption );
    const QString base = fi.completeBaseName();
    QRegularExpressionMatch m = sxe.match( base );
    if( !m.hasMatch() )
        m = nxn.match( base );
    const QString parent = fi.dir().dirName();
    const QRegularExpressionMatch sd = seasonDir.match( parent );

    if( m.hasMatch() || sd.hasMatch() )
    {
        it.category = "shows";
        const int season = m.hasMatch() ? m.captured( 1 ).toInt() : sd.captured( 1 ).toInt();
        it.season = QString( "S%1" ).arg( season, 2, 10, QChar( '0' ) );
        if( sd.hasMatch() )
        {
            /* Shows/The Wand/Season 1/x.mp4 */
            QDir up = fi.dir();
            up.cdUp();
            it.show = up.dirName();
        }
        else
        {
            /* The_Wand_S01E04.mp4: the name before the episode tag */
            QString name = base.left( m.capturedStart() );
            name.replace( QRegularExpression( "[._]" ), " " );
            name = name.trimmed();
            name.remove( QRegularExpression( "[ -]+$" ) );
            it.show = name.isEmpty() ? parent : name;
        }
        return;
    }
    it.category = "videos";
}

/**********************************************************************
 * MediaLibrary
 **********************************************************************/
MediaLibrary *MediaLibrary::instance( intf_thread_t *p_intf )
{
    /* lives with the input manager: destroyed after the main window, while
     * the interface still exists */
    static QPointer<MediaLibrary> lib;
    if( !lib )
        lib = new MediaLibrary( p_intf, THEMIM );
    return lib;
}

MediaLibrary::MediaLibrary( intf_thread_t *_p_intf, QObject *parent )
    : QObject( parent ), p_intf( _p_intf )
{
    storeFile = userDir( VLC_DATA_DIR ) + "/redesign-library.json";
    thumbDir = userDir( VLC_CACHE_DIR ) + "/thumbnails";
    QDir().mkpath( thumbDir );

    saveTimer.setSingleShot( true );
    saveTimer.setInterval( 1500 );
    connect( &saveTimer, &QTimer::timeout, this, &MediaLibrary::save );

    for( int i = 0; i < THUMB_GRABBERS; i++ )
    {
        FrameGrabber *g = new FrameGrabber( p_intf, QSize( THUMB_W, THUMB_H ), this );
        g->setTimeout( 6000 );
        connect( g, &FrameGrabber::finished, this,
                 [this, g]( const QString &u, const QImage &img, qint64 len, qint64 ms ) {
            thumbnailDone( g, u, img, len, ms );
        } );
        grabbers << g;
    }

    scanner = new AudioScanner( p_intf, this );
    connect( scanner, &AudioScanner::finished, this, &MediaLibrary::songDone );

    connect( THEMIM, &MainInputManager::inputChanged, this, &MediaLibrary::trackInput );
    connect( THEMIM->getIM(), &InputManager::positionUpdated, this, &MediaLibrary::trackPosition );

    load();
    /* scan once the window is up */
    QTimer::singleShot( 1500, this, &MediaLibrary::rescan );
}

MediaLibrary::~MediaLibrary()
{
    scanAbort = true;
    if( scanThread )
    {
        scanThread->disconnect( this );
        scanThread->wait();
        delete scanThread;
        scanThread = NULL;
    }
    if( saveTimer.isActive() )
        save();
}

QStringList MediaLibrary::roots() const
{
    QStringList out;
    char *s = var_InheritString( p_intf, "qt-library-roots" );
    if( s && *s )
        foreach( const QString &r, qfu( s ).split( ';', QString::SkipEmptyParts ) )
            out << QDir::cleanPath( QDir( r.trimmed() ).absolutePath() );
    free( s );
    if( out.isEmpty() )
    {
        foreach( QStandardPaths::StandardLocation loc,
                 QList<QStandardPaths::StandardLocation>()
                     << QStandardPaths::MoviesLocation << QStandardPaths::MusicLocation )
        {
            const QString dir = QStandardPaths::writableLocation( loc );
            /* only real folders, never the home directory itself */
            if( !dir.isEmpty() && dir != QDir::homePath() && QFileInfo( dir ).isDir() )
                out << dir;
        }
    }
    return out;
}

/* The walk runs on a worker thread and only collects paths, sizes and
 * dates; the merge with the user's data runs on the UI thread. */
void MediaLibrary::rescan()
{
    if( scanThread )
        return;
    const QStringList dirs = roots();
    scanAbort = false;
    scanStart = QDateTime::currentMSecsSinceEpoch();
    emit scanStarted();

    QPointer<MediaLibrary> self( this );
    std::atomic<bool> *abort = &scanAbort;
    scanThread = QThread::create( [dirs, self, abort]() {
        QVector<ScanEntry> found;
        QSet<QString> seen;
        foreach( const QString &root, dirs )
        {
            QDirIterator it( root, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories | QDirIterator::FollowSymlinks );
            while( it.hasNext() && !abort->load() )
            {
                const QString path = it.next();
                if( path.contains( "/." ) )
                    continue;   /* hidden files and folders */
                const QFileInfo fi = it.fileInfo();
                const QString ext = fi.suffix().toLower();
                const bool video = videoExtensions().contains( ext );
                const bool audio = !video && audioExtensions().contains( ext );
                if( !video && !audio )
                    continue;
                const QString canon = fi.canonicalFilePath();
                if( canon.isEmpty() || seen.contains( canon ) )
                    continue;
                seen.insert( canon );
                found.append( { fi.absoluteFilePath(), fi.size(),
                                fi.lastModified().toSecsSinceEpoch(), audio } );
            }
        }
        if( abort->load() )
            return;
        QMetaObject::invokeMethod( self.data(), [self, found]() {
            if( self )
                self->merge( found );
        }, Qt::QueuedConnection );
    } );
    connect( scanThread, &QThread::finished, this, [this]() {
        scanThread->deleteLater();
        scanThread = NULL;
    } );
    scanThread->start( QThread::LowPriority );
}

void MediaLibrary::merge( const QVector<ScanEntry> &found )
{
    QHash<QString, LibraryItem> old;
    foreach( const LibraryItem &it, list )
        old.insert( it.path, it );

    RecentsMRL *recents = RecentsMRL::getInstance( p_intf );
    QVector<LibraryItem> next;
    next.reserve( found.size() );
    foreach( const ScanEntry &e, found )
    {
        LibraryItem it = old.value( e.path );
        const bool known = !it.path.isEmpty();
        if( known && ( it.size != e.size || it.mtime != e.mtime ) )
        {
            it.duration = 0;   /* the file changed: length and thumbnail too */
            it.scanned = false;
        }
        it.path = e.path;
        it.kind = e.audio ? LibraryItem::AUDIO : LibraryItem::VIDEO;
        it.size = e.size;
        it.mtime = e.mtime;
        classify( it );
        if( !known )
        {
            /* the resume position VLC already keeps for recent files */
            const int t = recents->time( it.uri() );
            if( t > 0 )
                it.progress = t;
        }
        next.append( it );
    }
    list = next;
    reindex();
    const qint64 ms = QDateTime::currentMSecsSinceEpoch() - scanStart;
    msg_Dbg( p_intf, "library: scanned %d files in %lld ms (%s)", list.size(),
             (long long)ms, qtu( roots().join( ";" ) ) );
    emit itemsChanged();
    emit scanFinished( list.size(), ms );
    scheduleSave();
    queueThumbnails();
    queueSongs();
}

void MediaLibrary::reindex()
{
    byPath.clear();
    for( int i = 0; i < list.size(); i++ )
        byPath.insert( list[i].path, i );
}

void MediaLibrary::setWatched( const QString &path, bool b )
{
    const int i = indexOf( path );
    if( i < 0 || list[i].watched == b )
        return;
    list[i].watched = b;
    if( !b )
        list[i].progress = 0;
    emit itemChanged( i );
    scheduleSave();
}

void MediaLibrary::setFavorite( const QString &path, bool b )
{
    const int i = indexOf( path );
    if( i < 0 || list[i].favorite == b )
        return;
    list[i].favorite = b;
    emit itemChanged( i );
    scheduleSave();
}

/**********************************************************************
 * Playback tracking
 **********************************************************************/
void MediaLibrary::trackInput()
{
    playingPath.clear();
    input_thread_t *p_input = THEMIM->getInput();
    input_item_t *item = p_input ? input_GetItem( p_input ) : NULL;
    char *uri = item ? input_item_GetURI( item ) : NULL;
    char *path = uri ? vlc_uri2path( uri ) : NULL;
    if( path )
    {
        const int i = indexOf( QDir::cleanPath( qfu( path ) ) );
        if( i >= 0 )
        {
            playingPath = list[i].path;
            list[i].playCount++;
            list[i].lastPlayed = QDateTime::currentSecsSinceEpoch();
            emit itemChanged( i );
            scheduleSave();
        }
    }
    free( path );
    free( uri );
}

void MediaLibrary::trackPosition( float pos, int64_t time, int length )
{
    if( playingPath.isEmpty() || pos < 0 )
        return;
    const int i = indexOf( playingPath );
    if( i < 0 )
        return;
    LibraryItem &it = list[i];
    const qint64 progress = time / 1000;
    const qint64 duration = qint64( length ) * 1000;
    bool changed = false;
    if( duration > 0 && duration != it.duration )
    {
        it.duration = duration;
        changed = true;
    }
    /* update the stored position about once a second */
    if( qAbs( progress - it.progress ) >= 1000 )
    {
        it.progress = progress;
        changed = true;
    }
    if( !it.watched && pos >= WATCHED_RATIO )
    {
        it.watched = true;
        changed = true;
    }
    if( changed )
    {
        emit itemChanged( i );
        scheduleSave();
    }
}

/**********************************************************************
 * Thumbnails
 **********************************************************************/
QString MediaLibrary::fileKey( const QString &path, qint64 size, qint64 mtime )
{
    const QByteArray key = QString( "%1|%2|%3" ).arg( path ).arg( size ).arg( mtime ).toUtf8();
    return QString::fromLatin1( QCryptographicHash::hash( key, QCryptographicHash::Sha1 ).toHex() );
}

QString MediaLibrary::thumbnailKey( const LibraryItem &it ) const
{
    return fileKey( it.path, it.size, it.mtime );
}

/* a video's frame, or a song's cover art (both by the file's key) */
QString MediaLibrary::thumbnailFile( const LibraryItem &it ) const
{
    const QString f = thumbDir + "/" + thumbnailKey( it ) + ".jpg";
    return QFileInfo::exists( f ) ? f : QString();
}

void MediaLibrary::queueThumbnails()
{
    thumbQueue.clear();
    foreach( const LibraryItem &it, list )
        if( it.kind == LibraryItem::VIDEO && thumbnailFile( it ).isEmpty()
            && thumbFailures.value( it.path ) < THUMB_MAX_TRIES )
            thumbQueue << it.path;
    thumbsMade = 0;
    thumbsStart = QDateTime::currentMSecsSinceEpoch();
    if( !thumbQueue.isEmpty() )
        msg_Dbg( p_intf, "library: %d thumbnails to make", thumbQueue.size() );
    pumpThumbnails();
}

void MediaLibrary::requestThumbnail( const QString &path )
{
    const int i = indexOf( path );
    if( i >= 0 && list[i].kind == LibraryItem::AUDIO )
    {
        /* a song's picture comes with its tags */
        if( !list[i].scanned )
            requestSong( path, true );
        return;
    }
    if( i < 0 || list[i].kind != LibraryItem::VIDEO || !thumbnailFile( list[i] ).isEmpty() )
        return;
    if( grabbing.values().contains( path ) )
        return;
    thumbQueue.removeAll( path );
    thumbQueue.prepend( path );
    pumpThumbnails();
}

void MediaLibrary::pumpThumbnails()
{
    foreach( FrameGrabber *g, grabbers )
    {
        while( !g->isBusy() && !thumbQueue.isEmpty() )
        {
            const QString path = thumbQueue.takeFirst();
            const int i = indexOf( path );
            if( i < 0 )
                continue;
            /* a tenth of the way in skips logos and black openings */
            if( g->grab( list[i].uri(), -1, 0.1 ) )
                grabbing.insert( g, path );
            else
                thumbFailures[path]++;
        }
    }
}

void MediaLibrary::thumbnailDone( FrameGrabber *g, const QString &, const QImage &img,
                                  qint64 length, qint64 elapsedMs )
{
    const QString path = grabbing.take( g );
    const int i = indexOf( path );
    if( i >= 0 )
    {
        LibraryItem &it = list[i];
        if( length > 0 && it.duration <= 0 )
            it.duration = length / 1000;
        if( !img.isNull() )
        {
            const QString f = thumbDir + "/" + thumbnailKey( it ) + ".jpg";
            img.save( f, "JPG", 85 );
            thumbsMade++;
            emit thumbnailReady( path );
        }
        else
            thumbFailures[path]++;
        emit itemChanged( i );
        scheduleSave();
    }
    msg_Dbg( p_intf, "library: thumbnail %s in %lld ms", img.isNull() ? "failed" : "made",
             (long long)elapsedMs );
    pumpThumbnails();
    if( thumbQueue.isEmpty() && grabbing.isEmpty() && thumbsStart > 0 )
    {
        msg_Dbg( p_intf, "library: %d thumbnails made in %lld ms", thumbsMade,
                 (long long)( QDateTime::currentMSecsSinceEpoch() - thumbsStart ) );
        thumbsStart = 0;
    }
}

/**********************************************************************
 * Songs: tags, cover art and waveform peaks (AudioScanner)
 **********************************************************************/
void MediaLibrary::queueSongs()
{
    foreach( const LibraryItem &it, list )
        if( it.kind == LibraryItem::AUDIO && !it.scanned && !songQueue.contains( it.path )
            && songFailures.value( it.path ) < THUMB_MAX_TRIES && it.path != scanning )
            songQueue << it.path;
    if( !songQueue.isEmpty() )
        msg_Dbg( p_intf, "library: %d songs to read", songQueue.size() );
    pumpSongs();
}

void MediaLibrary::requestSong( const QString &path, bool front )
{
    if( path == scanning || songFailures.value( path ) >= THUMB_MAX_TRIES )
        return;
    songQueue.removeAll( path );
    if( front )
        songQueue.prepend( path );
    else
        songQueue.append( path );
    pumpSongs();
}

void MediaLibrary::pumpSongs()
{
    while( !scanner->isBusy() && !songQueue.isEmpty() )
    {
        const QString path = songQueue.takeFirst();
        if( scanner->scan( path ) )
            scanning = path;
        else
            songFailures[path]++;
    }
}

QByteArray MediaLibrary::peaks( const QString &path )
{
    QHash<QString, QByteArray>::const_iterator c = peakCache.constFind( path );
    if( c != peakCache.constEnd() )
        return c.value();
    const QFileInfo fi( path );
    if( !fi.isFile() )
        return QByteArray();
    QFile f( thumbDir + "/" + fileKey( path, fi.size(), fi.lastModified().toSecsSinceEpoch() )
             + ".peaks" );
    if( f.open( QIODevice::ReadOnly ) )
    {
        const QByteArray p = f.readAll();
        if( p.size() == AudioScanner::PEAK_COUNT )
        {
            peakCache.insert( path, p );
            return p;
        }
    }
    requestSong( path, true );
    return QByteArray();
}

void MediaLibrary::songDone( const QString &path, const AudioScan &r, qint64 elapsedMs )
{
    scanning.clear();
    const QFileInfo fi( path );
    const QString key = fileKey( path, fi.size(), fi.lastModified().toSecsSinceEpoch() );
    if( !r.peaks.isEmpty() )
    {
        QSaveFile f( thumbDir + "/" + key + ".peaks" );
        if( f.open( QIODevice::WriteOnly ) )
        {
            f.write( r.peaks );
            f.commit();
        }
        peakCache.insert( path, r.peaks );
    }
    bool art = false;
    if( !r.art.isEmpty() )
    {
        /* covers can be huge: keep a 600px copy beside the thumbnails */
        QImage img = QImage::fromData( r.art );
        if( !img.isNull() )
        {
            if( img.width() > 600 || img.height() > 600 )
                img = img.scaled( 600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation );
            art = img.convertToFormat( QImage::Format_RGB32 ).save( thumbDir + "/" + key + ".jpg", "JPG", 90 );
        }
    }

    const int i = indexOf( path );
    if( i >= 0 )
    {
        LibraryItem &it = list[i];
        if( r.ok || songFailures.value( path ) + 1 >= THUMB_MAX_TRIES )
            it.scanned = true;
        if( !r.ok )
            songFailures[path]++;
        it.title = r.title;
        it.artist = r.artist;
        it.album = r.album;
        it.genre = r.genre;
        it.track = r.track;
        if( r.length > 0 )
            it.duration = r.length / 1000;
        emit itemChanged( i );
        scheduleSave();
    }
    msg_Dbg( p_intf, "library: song %s read in %lld ms (tags %s, art %s, peaks %s)",
             qtu( fi.fileName() ), (long long)elapsedMs, r.title.isEmpty() ? "no" : "yes",
             art ? "yes" : "no", r.peaks.isEmpty() ? "no" : "yes" );
    if( art )
        emit thumbnailReady( path );
    if( !r.peaks.isEmpty() )
        emit peaksReady( path );
    pumpSongs();
}

/**********************************************************************
 * Storage
 **********************************************************************/
void MediaLibrary::scheduleSave()
{
    saveTimer.start();
}

void MediaLibrary::load()
{
    QFile f( storeFile );
    if( !f.open( QIODevice::ReadOnly ) )
        return;
    const QJsonObject root = QJsonDocument::fromJson( f.readAll() ).object();
    if( root.value( "version" ).toInt() != STORE_VERSION )
        return;
    foreach( const QJsonValue &v, root.value( "items" ).toArray() )
    {
        const QJsonObject o = v.toObject();
        LibraryItem it;
        it.path = o.value( "path" ).toString();
        if( it.path.isEmpty() )
            continue;
        it.kind = o.value( "audio" ).toBool() ? LibraryItem::AUDIO : LibraryItem::VIDEO;
        it.size = qint64( o.value( "size" ).toDouble() );
        it.mtime = qint64( o.value( "mtime" ).toDouble() );
        it.duration = qint64( o.value( "duration" ).toDouble() );
        it.progress = qint64( o.value( "progress" ).toDouble() );
        it.watched = o.value( "watched" ).toBool();
        it.favorite = o.value( "favorite" ).toBool();
        it.playCount = o.value( "plays" ).toInt();
        it.lastPlayed = qint64( o.value( "lastPlayed" ).toDouble() );
        it.subtitle = o.value( "subtitle" ).toString();
        it.title = o.value( "title" ).toString();
        it.artist = o.value( "artist" ).toString();
        it.album = o.value( "album" ).toString();
        it.genre = o.value( "genre" ).toString();
        it.track = o.value( "track" ).toInt();
        it.scanned = o.value( "scanned" ).toBool();
        classify( it );
        list.append( it );
    }
    reindex();
}

void MediaLibrary::save()
{
    QJsonArray items;
    foreach( const LibraryItem &it, list )
    {
        QJsonObject o;
        o.insert( "path", it.path );
        if( it.kind == LibraryItem::AUDIO )
            o.insert( "audio", true );
        o.insert( "size", double( it.size ) );
        o.insert( "mtime", double( it.mtime ) );
        if( it.duration > 0 )   o.insert( "duration", double( it.duration ) );
        if( it.progress > 0 )   o.insert( "progress", double( it.progress ) );
        if( it.watched )        o.insert( "watched", true );
        if( it.favorite )       o.insert( "favorite", true );
        if( it.playCount > 0 )  o.insert( "plays", it.playCount );
        if( it.lastPlayed > 0 ) o.insert( "lastPlayed", double( it.lastPlayed ) );
        if( !it.subtitle.isEmpty() ) o.insert( "subtitle", it.subtitle );
        if( !it.title.isEmpty() )  o.insert( "title", it.title );
        if( !it.artist.isEmpty() ) o.insert( "artist", it.artist );
        if( !it.album.isEmpty() )  o.insert( "album", it.album );
        if( !it.genre.isEmpty() )  o.insert( "genre", it.genre );
        if( it.track > 0 )         o.insert( "track", it.track );
        if( it.scanned )           o.insert( "scanned", true );
        items.append( o );
    }
    QJsonObject root;
    root.insert( "version", STORE_VERSION );
    root.insert( "items", items );

    QDir().mkpath( QFileInfo( storeFile ).absolutePath() );
    QSaveFile f( storeFile );
    if( f.open( QIODevice::WriteOnly ) )
    {
        f.write( QJsonDocument( root ).toJson( QJsonDocument::Compact ) );
        f.commit();
    }
}
