/*****************************************************************************
 * bookmarks.cpp : quick bookmarks, their OSD and per-file memory (P7)
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

#include "util/bookmarks.hpp"
#include "input_manager.hpp"
#include "main_interface.hpp"
#include "qt.hpp"
#include "styles/vlc_theme.hpp"

#include <vlc_input.h>
#include <vlc_vout.h>
#include <vlc_vout_osd.h>

#include <QCryptographicHash>
#include <QLabel>
#include <QPointer>
#include <QPropertyAnimation>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

namespace
{
QString g_restored;   /* the URI whose stored bookmarks were put back */
intf_thread_t *g_intf = NULL;

QString currentUri()
{
    intf_thread_t *p_intf = g_intf;
    input_thread_t *input = THEMIM->getInput();
    if( !input )
        return QString();
    char *uri = input_item_GetURI( input_GetItem( input ) );
    const QString s = qfu( uri );
    free( uri );
    return s;
}

QSettings &store()
{
    static QSettings s( QSettings::IniFormat, QSettings::UserScope, "vlc", "vlc-qt-bookmarks" );
    return s;
}

QString keyOf( const QString &uri )
{
    return QString::fromLatin1(
        QCryptographicHash::hash( uri.toUtf8(), QCryptographicHash::Md5 ).toHex() );
}

struct Mark { QString name; int64_t time; };

QList<Mark> current()
{
    intf_thread_t *p_intf = g_intf;
    QList<Mark> marks;
    input_thread_t *input = THEMIM->getInput();
    seekpoint_t **pp = NULL;
    int n = 0;
    if( input && input_Control( input, INPUT_GET_BOOKMARKS, &pp, &n ) == VLC_SUCCESS )
    {
        for( int i = 0; i < n; i++ )
        {
            marks << Mark{ qfu( pp[i]->psz_name ), pp[i]->i_time_offset };
            vlc_seekpoint_Delete( pp[i] );
        }
        free( pp );
    }
    return marks;
}

void save()
{
    const QString uri = currentUri();
    if( uri.isEmpty() || uri != g_restored )
        return;
    const QList<Mark> marks = current();
    QSettings &s = store();
    s.beginGroup( keyOf( uri ) );
    if( marks.isEmpty() )
        s.remove( "" );
    else
    {
        QStringList names, times;   /* times in microseconds, readable */
        foreach( const Mark &m, marks )
        {
            names << m.name;
            times << QString::number( m.time );
        }
        s.setValue( "uri", uri );
        s.setValue( "names", names );
        s.setValue( "times", times );
    }
    s.endGroup();
    s.sync();
}

void restore()
{
    intf_thread_t *p_intf = g_intf;
    const QString uri = currentUri();
    input_thread_t *input = THEMIM->getInput();
    if( uri.isEmpty() || !input || uri == g_restored )
        return;
    g_restored = uri;
    QSettings &s = store();
    s.beginGroup( keyOf( uri ) );
    const QStringList names = s.value( "names" ).toStringList();
    const QStringList times = s.value( "times" ).toStringList();
    s.endGroup();

    /* the playlist item may have brought some back already */
    const QList<Mark> have = current();
    for( int i = 0; i < names.size() && i < times.size(); i++ )
    {
        bool ok;
        const int64_t t = times[i].toLongLong( &ok );
        if( !ok )
            continue;
        bool dup = false;
        foreach( const Mark &m, have )
            dup |= qAbs( m.time - t ) < 500000;
        if( dup )
            continue;
        seekpoint_t bm;
        memset( &bm, 0, sizeof( bm ) );
        bm.i_time_offset = t;
        QByteArray raw = names[i].toUtf8();
        bm.psz_name = raw.data();
        input_Control( input, INPUT_ADD_BOOKMARK, &bm );
    }
}

/* the message over the player when there is no video to draw it on */
class Toast : public QLabel
{
public:
    Toast() : QLabel( NULL, Qt::ToolTip | Qt::FramelessWindowHint )
    {
        setAttribute( Qt::WA_TranslucentBackground );
        setAttribute( Qt::WA_ShowWithoutActivating );
        setAlignment( Qt::AlignCenter );
        hideTimer.setSingleShot( true );
        fade = new QPropertyAnimation( this, "windowOpacity", this );
        fade->setDuration( 250 );
        QObject::connect( &hideTimer, &QTimer::timeout, [this]() {
            fade->setStartValue( 1.0 );
            fade->setEndValue( 0.0 );
            fade->start();
        } );
        QObject::connect( fade, &QPropertyAnimation::finished, [this]() {
            if( windowOpacity() < 0.5 )
                hide();
        } );
    }
    void showOver( QWidget *anchor, const QString &text )
    {
        const Theme::Palette &c = Theme::colors();
        setStyleSheet( QString( "QLabel { background: %1; color: %2; border: 1px solid %3;"
                                " border-radius: 8px; padding: 10px 18px; font-size: 14px; }" )
                       .arg( c.elevated.name(), c.text.name(), c.border.name() ) );
        setText( text );
        adjustSize();
        const QRect r = anchor->geometry();
        move( r.center().x() - width() / 2, r.top() + Theme::TITLEBAR_HEIGHT + 24 );
        fade->stop();
        setWindowOpacity( 1.0 );
        show();
        raise();
        hideTimer.start( 1800 );
    }
private:
    QTimer hideTimer;
    QPropertyAnimation *fade;
};
}

void Bookmarks::init( intf_thread_t *p_intf )
{
    g_intf = p_intf;
    /* after Init, so the item's own "bookmarks=" are loaded first */
    QObject::connect( THEMIM->getIM(), &InputManager::playingStatusChanged, THEMIM, []( int s ) {
        if( s == PLAYING_S )
            restore();
    } );
    QObject::connect( THEMIM->getIM(), &InputManager::bookmarksChanged, THEMIM, []() { save(); } );
    /* a new input restores again (its item may have lost the option) */
    QObject::connect( THEMIM, &MainInputManager::inputChanged, THEMIM, []( bool ) {
        g_restored.clear();
    } );
}

QList<int64_t> Bookmarks::times()
{
    if( !g_intf )
        return QList<int64_t>();
    QList<int64_t> t;
    foreach( const Mark &m, current() )
        t << m.time;
    return t;
}

QString Bookmarks::add( intf_thread_t *p_intf, bool withOsd )
{
    g_intf = p_intf;
    input_thread_t *input = THEMIM->getInput();
    seekpoint_t bm;
    if( !input || input_Control( input, INPUT_GET_BOOKMARK, &bm ) )
        return QString();
    /* make sure later bookmarks of a new file are saved too */
    if( g_restored != currentUri() )
        restore();

    /* Figma: "DON'T BLINK 1", "DON'T BLINK 2" */
    char *title = input_item_GetTitleFbName( input_GetItem( input ) );
    QString base = qfu( title ).trimmed();
    free( title );
    const int dot = base.lastIndexOf( '.' );
    if( dot > 0 && base.size() - dot <= 5 )
        base.truncate( dot );   /* "clip.mkv" -> "clip" */
    if( base.isEmpty() )
        base = qtr( "Bookmark" );
    QStringList used;
    foreach( const Mark &m, current() )
        used << m.name;
    int n = used.size() + 1;
    while( used.contains( QString( "%1 %2" ).arg( base ).arg( n ) ) )
        n++;
    const QString name = QString( "%1 %2" ).arg( base ).arg( n );

    QByteArray raw = name.toUtf8();
    bm.psz_name = raw.data();
    if( input_Control( input, INPUT_ADD_BOOKMARK, &bm ) )
        return QString();
    if( withOsd )
    {
        const int64_t s = bm.i_time_offset / CLOCK_FREQ;
        osd( p_intf, qtr( "Bookmark added: %1 · %2" ).arg( name )
                         .arg( QString( "%1:%2:%3" ).arg( s / 3600, 2, 10, QChar( '0' ) )
                               .arg( ( s / 60 ) % 60, 2, 10, QChar( '0' ) )
                               .arg( s % 60, 2, 10, QChar( '0' ) ) ) );
    }
    return name;
}

void Bookmarks::osd( intf_thread_t *p_intf, const QString &text )
{
    vout_thread_t *vout = THEMIM->getVout();
    if( vout )
    {
        vout_OSDMessage( vout, VOUT_SPU_CHANNEL_OSD, "%s", qtu( text ) );
        vlc_object_release( vout );
        return;
    }
    if( !p_intf->p_sys->p_mi )
        return;
    static QPointer<QLabel> toast;
    if( !toast )
        toast = new Toast;
    static_cast<Toast *>( toast.data() )->showOver( p_intf->p_sys->p_mi, text );
}
