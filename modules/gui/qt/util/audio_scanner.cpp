/*****************************************************************************
 * audio_scanner.cpp : tags, cover art and waveform peaks of a song (P5c)
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

#include "util/audio_scanner.hpp"
#include "util/qt_compat.hpp"

#include <vlc_input.h>
#include <vlc_input_item.h>
#include <vlc_url.h>
#include <vlc_variables.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QUrl>
#include <cmath>
#include <thread>
#include <vector>

struct AudioScanner::Job
{
    QPointer<AudioScanner> owner;
    QString path, pcm;
    input_thread_t *input = NULL;
    input_item_t *item = NULL;
    std::atomic<bool> ended { false };
    std::atomic<qint64> length { 0 };
    QByteArray art;               /* set on the input thread before "ended" */
    QElapsedTimer clock;
};

/* the picture among the attachments (ID3 APIC, FLAC/Vorbis pictures, MKV
 * cover attachments), read while the input is still open */
static QByteArray attachedPicture( input_thread_t *input )
{
    QByteArray out;
    input_attachment_t **att = NULL;
    int count = 0;
    if( input_Control( input, INPUT_GET_ATTACHMENTS, &att, &count ) != VLC_SUCCESS )
        return out;
    for( int i = 0; i < count; i++ )
    {
        if( out.isEmpty() && att[i]->psz_mime && !strncmp( att[i]->psz_mime, "image/", 6 )
            && att[i]->i_data > 0 )
            out = QByteArray( static_cast<const char *>( att[i]->p_data ), att[i]->i_data );
        vlc_input_attachment_Delete( att[i] );
    }
    free( att );
    return out;
}

int AudioScanner::eventCb( vlc_object_t *obj, const char *, vlc_value_t,
                           vlc_value_t cur, void *data )
{
    AudioScanner::Job *j = static_cast<AudioScanner::Job *>( data );
    input_thread_t *input = reinterpret_cast<input_thread_t *>( obj );
    if( cur.i_int == INPUT_EVENT_LENGTH )
    {
        const qint64 length = var_GetInteger( obj, "length" );
        if( length > 0 )
            j->length = length;
        return VLC_SUCCESS;
    }
    bool end = cur.i_int == INPUT_EVENT_DEAD;
    if( cur.i_int == INPUT_EVENT_STATE )
    {
        const int state = var_GetInteger( obj, "state" );
        end = state == END_S || state == ERROR_S;
    }
    if( !end || j->ended.load() )
        return VLC_SUCCESS;
    j->art = attachedPicture( input );
    j->ended = true;
    QPointer<AudioScanner> owner = j->owner;
    if( owner )
        QtCompat::invokeQueued( owner.data(), [owner, j]() {
            if( owner )
                owner->finish( j );
        }, Qt::QueuedConnection );
    return VLC_SUCCESS;
}

AudioScanner::AudioScanner( intf_thread_t *_p_intf, QObject *parent )
    : QObject( parent ), p_intf( _p_intf ), job( NULL )
{
    qRegisterMetaType<AudioScan>( "AudioScan" );
    host = static_cast<vlc_object_t *>( vlc_object_create( p_intf, sizeof( vlc_object_t ) ) );

    /* a song that never ends (a broken file, a stream) must not block */
    watchdog.setSingleShot( true );
    watchdog.setInterval( 30000 );
    connect( &watchdog, &QTimer::timeout, this, [this]() {
        if( job )
            finish( job );
    } );
}

AudioScanner::~AudioScanner()
{
    if( job )
        finish( job );
    while( closing.load() > 0 )
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    if( host )
        vlc_object_release( host );
}

bool AudioScanner::scan( const QString &path )
{
    if( job || !host || path.isEmpty() )
        return false;

    static std::atomic<int> serial { 0 };
    Job *j = new Job;
    j->owner = this;
    j->path = path;
    j->pcm = QDir::temp().filePath( QString( "vlc-qt-peaks-%1-%2.raw" )
                                    .arg( QCoreApplication::applicationPid() ).arg( serial++ ) );

    const QString uri = QUrl::fromLocalFile( path ).toString( QUrl::FullyEncoded );
    j->item = input_item_New( qtu( uri ), "audio scanner" );
    if( !j->item )
    {
        delete j;
        return false;
    }
    const QString sout = QString( ":sout=#transcode{acodec=s16l,channels=1,samplerate=%1}"
                                  ":std{access=file,mux=raw,dst='%2'}" )
                         .arg( int( SAMPLE_RATE ) ).arg( j->pcm );
    static const char *const options[] = {
        ":no-video", ":no-spu", ":no-sout-video", ":no-sout-spu", ":no-stats",
        ":no-sub-autodetect-file", ":file-caching=0", ":avcodec-hw=none",
    };
    for( const char *opt : options )
        input_item_AddOption( j->item, opt, VLC_INPUT_OPTION_TRUSTED );
    input_item_AddOption( j->item, qtu( sout ), VLC_INPUT_OPTION_TRUSTED );

    j->input = input_Create( host, j->item, "audio scanner", NULL, NULL );
    if( j->input )
        var_AddCallback( j->input, "intf-event", eventCb, j );
    if( !j->input || input_Start( j->input ) )
    {
        if( j->input )
        {
            var_DelCallback( j->input, "intf-event", eventCb, j );
            input_Close( j->input );
        }
        input_item_Release( j->item );
        delete j;
        return false;
    }
    job = j;
    j->clock.start();
    watchdog.start();
    return true;
}

static QString itemMeta( input_item_t *item, vlc_meta_type_t type )
{
    char *s = input_item_GetMeta( item, type );
    const QString out = s ? qfu( s ).trimmed() : QString();
    free( s );
    return out;
}

/* The loudness (RMS) of each of PEAK_COUNT slices, relative to the
 * song's loudest slice, with a 1.6 curve. The loudest sample alone sits
 * near full scale all through a mastered track and draws a flat bar;
 * loudness shows the intro, the drops and the breaks. */
static QByteArray readPeaks( const QString &pcm )
{
    QFile f( pcm );
    if( !f.open( QIODevice::ReadOnly ) )
        return QByteArray();
    const QByteArray raw = f.readAll();
    f.close();
    const qint64 n = raw.size() / 2;
    if( n < AudioScanner::PEAK_COUNT )
        return QByteArray();
    const qint16 *s = reinterpret_cast<const qint16 *>( raw.constData() );
    std::vector<double> sum( AudioScanner::PEAK_COUNT, 0.0 );
    std::vector<qint64> count( AudioScanner::PEAK_COUNT, 0 );
    for( qint64 i = 0; i < n; i++ )
    {
        const int slice = int( i * AudioScanner::PEAK_COUNT / n );
        sum[slice] += double( s[i] ) * s[i];
        count[slice]++;
    }
    std::vector<double> rms( AudioScanner::PEAK_COUNT, 0.0 );
    double loudest = 1e-9;
    for( int k = 0; k < AudioScanner::PEAK_COUNT; k++ )
    {
        rms[k] = count[k] > 0 ? std::sqrt( sum[k] / count[k] ) : 0.0;
        loudest = qMax( loudest, rms[k] );
    }
    QByteArray out( AudioScanner::PEAK_COUNT, 0 );
    for( int k = 0; k < AudioScanner::PEAK_COUNT; k++ )
        out[k] = char( qBound( 0, int( std::pow( rms[k] / loudest, 1.6 ) * 255 + 0.5 ), 255 ) );
    return out;
}

void AudioScanner::finish( Job *j )
{
    if( j != job )
        return;
    job = NULL;
    watchdog.stop();

    /* closing joins the input; reading the PCM is a few MB: both on a
     * worker, so the UI never waits */
    closing++;
    QPointer<AudioScanner> self( this );
    const bool ended = j->ended.load();
    std::thread( [self, j, ended, this]() {
        var_DelCallback( j->input, "intf-event", eventCb, j );
        input_Stop( j->input );
        input_Close( j->input );

        AudioScan r;
        r.ok = ended;
        r.title = itemMeta( j->item, vlc_meta_Title );
        r.artist = itemMeta( j->item, vlc_meta_Artist );
        if( r.artist.isEmpty() )
            r.artist = itemMeta( j->item, vlc_meta_AlbumArtist );
        r.album = itemMeta( j->item, vlc_meta_Album );
        r.genre = itemMeta( j->item, vlc_meta_Genre );
        r.track = itemMeta( j->item, vlc_meta_TrackNumber ).section( '/', 0, 0 ).toInt();
        r.length = j->length.load();
        if( r.length <= 0 )
            r.length = input_item_GetDuration( j->item );
        r.art = j->art;
        if( r.art.isEmpty() )
        {
            /* folder art (cover.jpg...) found by VLC's meta fetchers */
            const QString url = itemMeta( j->item, vlc_meta_ArtworkURL );
            if( url.startsWith( "file://" ) )
            {
                QFile a( QUrl( url ).toLocalFile() );
                if( a.size() < 16 * 1024 * 1024 && a.open( QIODevice::ReadOnly ) )
                    r.art = a.readAll();
            }
        }
        input_item_Release( j->item );
        if( ended )
            r.peaks = readPeaks( j->pcm );
        QFile::remove( j->pcm );

        const QString path = j->path;
        const qint64 ms = j->clock.elapsed();
        delete j;
        if( self )
            QtCompat::invokeQueued( self.data(), [self, path, r, ms]() {
                if( self )
                    emit self->finished( path, r, ms );
            }, Qt::QueuedConnection );
        closing--;
    } ).detach();
}
