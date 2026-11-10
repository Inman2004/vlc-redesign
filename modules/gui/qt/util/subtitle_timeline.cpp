/*****************************************************************************
 * subtitle_timeline.cpp : subtitle cues for the seek bar (P7)
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

#include "util/subtitle_timeline.hpp"
#include "input_manager.hpp"
#include "qt.hpp"
#include "styles/vlc_theme.hpp"

#include <vlc_actions.h>
#include <vlc_input.h>
#include <vlc_input_item.h>

#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace
{
SubtitleTimeline *g_instance = NULL;
const int64_t US = INT64_C( 1000000 );

/* "<i>Hello</i>" / "{\an8}Hello\Nthere" -> "Hello" / "Hello there" */
QString cleanText( QString t )
{
    static const QRegularExpression html( "<[^>]*>" ), ass( "\\{[^}]*\\}" );
    t.replace( "\\N", " " ).replace( "\\n", " " ).replace( "\\h", " " );
    t.remove( html ).remove( ass );
    return t.simplified();
}

int64_t clockOf( const QString &h, const QString &m, const QString &s, QString frac )
{
    /* .5 = 500 ms, .05 = 50 ms (ASS centiseconds), .005 = 5 ms */
    while( frac.size() < 3 )
        frac.append( '0' );
    return ( ( h.toLongLong() * 60 + m.toLongLong() ) * 60 + s.toLongLong() ) * US
         + frac.left( 3 ).toLongLong() * 1000;
}

/* .srt and .vtt: "00:01:02,345 --> 00:01:04,000" (vtt may drop the hours) */
QVector<SubtitleTimeline::Cue> parseSrt( const QStringList &lines )
{
    static const QRegularExpression timing(
        "^\\s*(?:(\\d+):)?(\\d{1,2}):(\\d{1,2})[,.](\\d{1,3})\\s*-->\\s*"
        "(?:(\\d+):)?(\\d{1,2}):(\\d{1,2})[,.](\\d{1,3})" );
    QVector<SubtitleTimeline::Cue> cues;
    for( int i = 0; i < lines.size(); i++ )
    {
        const QRegularExpressionMatch m = timing.match( lines[i] );
        if( !m.hasMatch() )
            continue;
        SubtitleTimeline::Cue c;
        c.start = clockOf( m.captured( 1 ), m.captured( 2 ), m.captured( 3 ), m.captured( 4 ) );
        c.end = clockOf( m.captured( 5 ), m.captured( 6 ), m.captured( 7 ), m.captured( 8 ) );
        /* the first non-empty line after the timing */
        for( int j = i + 1; j < lines.size() && !lines[j].trimmed().isEmpty(); j++ )
        {
            c.text = cleanText( lines[j] );
            if( !c.text.isEmpty() )
                break;
        }
        if( c.end > c.start )
            cues << c;
    }
    return cues;
}

/* .ass / .ssa: "Dialogue: 0,0:01:02.34,0:01:04.00,Default,,0,0,0,,Text",
 * with the column order from the [Events] "Format:" line */
QVector<SubtitleTimeline::Cue> parseAss( const QStringList &lines )
{
    static const QRegularExpression clock( "^\\s*(\\d+):(\\d{1,2}):(\\d{1,2})[.,](\\d{1,3})\\s*$" );
    QVector<SubtitleTimeline::Cue> cues;
    int startCol = 1, endCol = 2, textCol = 9;
    bool inEvents = false;
    foreach( const QString &raw, lines )
    {
        const QString line = raw.trimmed();
        if( line.startsWith( '[' ) )
        {
            inEvents = line.compare( "[Events]", Qt::CaseInsensitive ) == 0;
            continue;
        }
        if( !inEvents )
            continue;
        if( line.startsWith( "Format:", Qt::CaseInsensitive ) )
        {
            const QStringList cols = line.mid( 7 ).split( ',' );
            for( int k = 0; k < cols.size(); k++ )
            {
                const QString c = cols[k].trimmed().toLower();
                if( c == "start" ) startCol = k;
                else if( c == "end" ) endCol = k;
                else if( c == "text" ) textCol = k;
            }
            continue;
        }
        if( !line.startsWith( "Dialogue:", Qt::CaseInsensitive ) )
            continue;
        /* the text is the last column and may hold commas */
        const QStringList f = line.mid( 9 ).split( ',' );
        if( f.size() <= qMax( textCol, qMax( startCol, endCol ) ) )
            continue;
        const QRegularExpressionMatch a = clock.match( f[startCol] ), b = clock.match( f[endCol] );
        if( !a.hasMatch() || !b.hasMatch() )
            continue;
        SubtitleTimeline::Cue c;
        c.start = clockOf( a.captured( 1 ), a.captured( 2 ), a.captured( 3 ), a.captured( 4 ) );
        c.end = clockOf( b.captured( 1 ), b.captured( 2 ), b.captured( 3 ), b.captured( 4 ) );
        c.text = cleanText( f.mid( textCol ).join( ',' ) );
        if( c.end > c.start )
            cues << c;
    }
    return cues;
}
}

SubtitleTimeline *SubtitleTimeline::instance( intf_thread_t *p_intf )
{
    if( !g_instance )
        g_instance = new SubtitleTimeline( p_intf );
    return g_instance;
}

SubtitleTimeline::SubtitleTimeline( intf_thread_t *_p_intf )
    : QObject( THEMIM ), p_intf( _p_intf ), stamp( 0 ), lastTime( 0 ), lengthUs( 0 )
{
    on = var_InheritBool( p_intf, "qt-subtitle-timeline" );
    sinceUpdate.start();

    /* smooth scrolling between the core's position updates */
    ticker = new QTimer( this );
    ticker->setInterval( 33 );
    connect( ticker, &QTimer::timeout, this, &SubtitleTimeline::moved );

    connect( THEMIM, &MainInputManager::inputChanged, this, &SubtitleTimeline::reload );
    connect( THEMIM->getIM(), &InputManager::esChanged, this, &SubtitleTimeline::reload );
    connect( THEMIM->getIM(), &InputManager::synchroChanged, this, &SubtitleTimeline::changed );
    connect( THEMIM->getIM(), &InputManager::positionUpdated, this, &SubtitleTimeline::onPosition );
    connect( THEMIM->getIM(), &InputManager::playingStatusChanged, this, [this]( int s ) {
        if( s == PLAYING_S && active() )
            ticker->start();
        else
            ticker->stop();
        sinceUpdate.restart();
        emit moved();
    } );
    connect( this, &SubtitleTimeline::changed, this, [this]() {
        if( active() && THEMIM->getIM()->playingStatus() == PLAYING_S )
            ticker->start();
        else
            ticker->stop();
    } );
    reload();
}

bool SubtitleTimeline::enabled() const
{
    return on && Theme::redesign();
}

void SubtitleTimeline::setEnabled( bool b )
{
    if( b == on )
        return;
    on = b;
    config_PutInt( p_intf, "qt-subtitle-timeline", b );
    emit changed();
}

bool SubtitleTimeline::active() const
{
    return enabled() && !list.isEmpty() && lengthUs > 0;
}

int64_t SubtitleTimeline::delay() const
{
    input_thread_t *input = THEMIM->getInput();
    return input ? var_GetInteger( input, "spu-delay" ) : 0;
}

int64_t SubtitleTimeline::now() const
{
    if( THEMIM->getIM()->playingStatus() != PLAYING_S )
        return lastTime;
    input_thread_t *input = THEMIM->getInput();
    const float rate = input ? var_GetFloat( input, "rate" ) : 1.f;
    /* never run more than a second ahead of the last real update */
    const int64_t ahead = qMin<int64_t>( sinceUpdate.elapsed(), 1000 ) * 1000;
    return qBound<int64_t>( 0, lastTime + int64_t( ahead * rate ), qMax<int64_t>( lengthUs, 0 ) );
}

void SubtitleTimeline::window( int64_t *start, int64_t *end, int64_t around ) const
{
    if( around < 0 )
        around = now();
    const int64_t w = lengthUs > 0 ? qMin( WINDOW, lengthUs ) : WINDOW;
    int64_t s = around - w / 2;
    s = qBound<int64_t>( 0, s, qMax<int64_t>( 0, lengthUs - w ) );
    *start = s;
    *end = s + w;
}

int SubtitleTimeline::cueAt( int64_t time ) const
{
    const int64_t t = time - delay();
    /* cues are sorted by start; overlapping ones are rare, take the last */
    auto it = std::upper_bound( list.constBegin(), list.constEnd(), t,
                                []( int64_t v, const Cue &c ) { return v < c.start; } );
    while( it != list.constBegin() )
    {
        --it;
        if( it->end > t )
            return int( it - list.constBegin() );
        if( t - it->start > 10 * US )   /* nothing long enough to reach t */
            break;
    }
    return -1;
}

void SubtitleTimeline::onPosition( float, int64_t time, int length )
{
    lastTime = time;
    sinceUpdate.restart();
    const int64_t len = int64_t( length ) * US;
    if( len != lengthUs )
    {
        lengthUs = len;
        emit changed();
    }
    emit moved();
}

/* the file behind the selected subtitle track: the item's subtitle slaves
 * come last in "spu-es", in the order they were added */
QString SubtitleTimeline::selectedFile() const
{
    input_thread_t *input = THEMIM->getInput();
    if( !input )
        return QString();
    input_item_t *item = input_GetItem( input );
    QStringList slaves;
    vlc_mutex_lock( &item->lock );
    for( int i = 0; i < item->i_slaves; i++ )
        if( item->pp_slaves[i]->i_type == SLAVE_TYPE_SPU )
            slaves << qfu( item->pp_slaves[i]->psz_uri );
    vlc_mutex_unlock( &item->lock );

    const int64_t selected = var_GetInteger( input, "spu-es" );
    if( selected < 0 )
        return QString();
    vlc_value_t list, texts;
    if( var_Change( input, "spu-es", VLC_VAR_GETCHOICES, &list, &texts ) )
        return QString();
    QList<int64_t> ids;
    for( int i = 0; i < list.p_list->i_count; i++ )
        if( list.p_list->p_values[i].i_int >= 0 )
            ids << list.p_list->p_values[i].i_int;
    var_FreeList( &list, &texts );

    const int k = ids.indexOf( selected ) - ( ids.size() - slaves.size() );
    if( k < 0 || k >= slaves.size() )
        return QString();
    const QUrl url( slaves[k] );
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

void SubtitleTimeline::reload()
{
    const QString file = selectedFile();
    if( file.isEmpty() )
    {
        list.clear();
        path.clear();
        input_thread_t *input = THEMIM->getInput();
        reason = input && var_GetInteger( input, "spu-es" ) >= 0
               ? qtr( "Subtitles inside the video file can't be drawn on the seek bar. "
                      "Load a subtitle file (.srt, .vtt, .ass) to see them." )
               : qtr( "No subtitle file is selected." );
        emit changed();
        return;
    }
    const qint64 mtime = QFileInfo( file ).lastModified().toMSecsSinceEpoch();
    if( file != path || mtime != stamp )
    {
        path = file;
        stamp = mtime;
        list = parse( file );
        msg_Dbg( p_intf, "subtitle timeline: %d cues in %s", list.size(), qtu( file ) );
    }
    reason = list.isEmpty()
           ? qtr( "This subtitle format can't be drawn on the seek bar (only .srt, .vtt, .ass and .ssa)." )
           : QString();
    emit changed();
}

QVector<SubtitleTimeline::Cue> SubtitleTimeline::parse( const QString &file )
{
    QFile f( file );
    if( f.size() > 16 * 1024 * 1024 || !f.open( QIODevice::ReadOnly ) )
        return QVector<Cue>();
    const QByteArray raw = f.readAll();
    /* most files are UTF-8; older ones are in a legacy code page, and
     * only their (ASCII) timings matter for the blocks */
    QString text = QString::fromUtf8( raw );
    if( text.contains( QChar::ReplacementCharacter ) )
        text = QString::fromLatin1( raw );
    const QStringList lines = text.split( QRegularExpression( "\r\n|\r|\n" ) );

    QVector<Cue> cues;
    const QString ext = QFileInfo( file ).suffix().toLower();
    if( ext == "ass" || ext == "ssa" || text.contains( "[Events]", Qt::CaseInsensitive ) )
        cues = parseAss( lines );
    else if( ext == "srt" || ext == "vtt" || ext == "txt" || text.contains( "-->" ) )
        cues = parseSrt( lines );
    std::stable_sort( cues.begin(), cues.end(),
                      []( const Cue &a, const Cue &b ) { return a.start < b.start; } );
    return cues;
}

void SubtitleTimeline::previousCue()
{
    input_thread_t *input = THEMIM->getInput();
    if( !input || list.isEmpty() )
        return;
    /* a second of grace so repeated clicks keep walking back */
    const int64_t t = now() - delay() - US;
    int64_t target = -1;
    foreach( const Cue &c, list )
    {
        if( c.start >= t )
            break;
        target = c.start;
    }
    if( target < 0 )
        return;
    target = qMax<int64_t>( 0, target + delay() );
    var_SetInteger( input, "time", target );
    lastTime = target;
    sinceUpdate.restart();
    emit moved();
}

void SubtitleTimeline::nextCue()
{
    input_thread_t *input = THEMIM->getInput();
    if( !input || list.isEmpty() )
        return;
    const int64_t t = now() - delay() + 100000;
    foreach( const Cue &c, list )
    {
        if( c.start > t )
        {
            const int64_t target = qMax<int64_t>( 0, c.start + delay() );
            var_SetInteger( input, "time", target );
            lastTime = target;
            sinceUpdate.restart();
            emit moved();
            return;
        }
    }
}

void SubtitleTimeline::earlier()
{
    var_SetInteger( p_intf->obj.libvlc, "key-action", ACTIONID_SUBDELAY_DOWN );
}

void SubtitleTimeline::later()
{
    var_SetInteger( p_intf->obj.libvlc, "key-action", ACTIONID_SUBDELAY_UP );
}
