/*****************************************************************************
 * redesign_menus.cpp : main menu and track popover of the redesign (P4b)
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

#include "components/redesign_menus.hpp"
#include "util/qt_compat.hpp"
#include "styles/vlc_theme.hpp"
#include "main_interface.hpp"
#include "dialogs_provider.hpp"
#include "input_manager.hpp"
#include "recents.hpp"
#include "components/adjust_panel.hpp"
#include "dialogs/redesign_dialogs.hpp"

#include <vlc_aout.h>
#include <vlc_input.h>
#include <vlc_input_item.h>
#include <vlc_url.h>

#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QFrame>
#include <QKeyEvent>
#include "util/frame_animation.hpp"
#include <QDateTime>
#include <QScreen>
#include <QFileInfo>
#include <QUrl>
#include <QSettings>
#include <QTimer>
#include <QIconEngine>
#include <QPainter>

/**********************************************************************
 * Main menu
 **********************************************************************/
/* Qt's stylesheet menus ask icons for the default 16px with no widget, so
 * the main menu's glyphs come from an engine that always hands back its
 * 24px pixmap (centred in the 28px box the style reserves) */
namespace {
class FixedIconEngine : public QIconEngine
{
public:
    explicit FixedIconEngine( const QPixmap &p ) : pm( p ) {}
    void paint( QPainter *painter, const QRect &rect, QIcon::Mode, QIcon::State ) override
    {
        const QSize s = pm.size() / pm.devicePixelRatio();
        painter->drawPixmap( QRect( rect.center() - QPoint( s.width() / 2, s.height() / 2 ), s ), pm );
    }
    QPixmap pixmap( const QSize &, QIcon::Mode mode, QIcon::State ) override
    {
        if( mode != QIcon::Disabled )
            return pm;
        QPixmap faded( pm.size() );
        faded.setDevicePixelRatio( pm.devicePixelRatio() );
        faded.fill( Qt::transparent );
        QPainter p( &faded );
        p.setOpacity( 0.4 );
        p.drawPixmap( 0, 0, pm );
        return faded;
    }
    QSize actualSize( const QSize &, QIcon::Mode, QIcon::State ) override
    {
        return pm.size() / pm.devicePixelRatio();
    }
    QIconEngine *clone() const override { return new FixedIconEngine( pm ); }
private:
    QPixmap pm;
};
}

QIcon RedesignMenus::menuIcon( const QString &name, int size )
{
    return QIcon( new FixedIconEngine( Theme::pixmap( name, size, Theme::accent() ) ) );
}

QString RedesignMenus::withShortcut( const QString &label, const QString &keys )
{
    return keys.isEmpty() ? label : label + "\t" + keys;
}

QStringList RedesignMenus::profiles( intf_thread_t *p_intf )
{
    return getSettings()->value( "Profiles/names",
        QStringList() << qtr( "Movies" ) << qtr( "Shows" ) << qtr( "Music" ) << qtr( "Work" ) )
        .toStringList();
}

QString RedesignMenus::currentProfile( intf_thread_t *p_intf )
{
    const QStringList all = profiles( p_intf );
    const QString cur = getSettings()->value( "Profiles/current" ).toString();
    return all.contains( cur ) ? cur : all.value( 0 );
}

static void buildRecentMenu( intf_thread_t *p_intf, QMenu *m )
{
    m->clear();
    RecentsMRL *rmrl = RecentsMRL::getInstance( p_intf );
    const QStringList list = rmrl->recentList();
    for( int i = 0; i < qMin( list.count(), 10 ); i++ )
    {
        const QString mrl = list.at( i );
        char *psz = vlc_uri_decode_duplicate( qtu( mrl ) );
        QString path = qfu( psz );
        free( psz );
        const QUrl url( mrl );
        const QString name = url.isLocalFile() ? QFileInfo( url.toLocalFile() ).fileName() : path;
        QAction *a = m->addAction( QString( name ).replace( "&", "&&" ), m, [p_intf, mrl]() {
            Open::openMRL( p_intf, mrl );
        } );
        a->setToolTip( url.isLocalFile() ? url.toLocalFile() : path );
        if( i < 9 )
            a->setText( RedesignMenus::withShortcut( a->text(), QString( "Ctrl + %1" ).arg( i + 1 ) ) );
    }
    if( list.isEmpty() )
        m->addAction( qtr( "Nothing played yet" ) )->setEnabled( false );
    else
    {
        m->addSeparator();
        m->addAction( qtr( "Clear" ), m, [rmrl]() { rmrl->clear(); } );
    }
}

static void buildProfileMenu( intf_thread_t *p_intf, QMenu *m, QAction *owner )
{
    m->clear();
    const QString cur = RedesignMenus::currentProfile( p_intf );
    QActionGroup *g = new QActionGroup( m );
    foreach( const QString &name, RedesignMenus::profiles( p_intf ) )
    {
        /* Figma: an accent check in the icon column marks the current one */
        QAction *a = m->addAction( name == cur ? RedesignMenus::menuIcon( "check", 20 )
                                               : RedesignMenus::menuIcon( "blank", 20 ), name );
        g->addAction( a );
        QObject::connect( a, &QAction::triggered, m, [p_intf, name, owner]() {
            getSettings()->setValue( "Profiles/current", name );
            owner->setText( qtr( "Profile (%1)" ).arg( name ) );
        } );
    }
    m->addSeparator();
    m->addAction( RedesignMenus::menuIcon( "add" ), qtr( "New" ), m, [p_intf, owner]() {
        bool ok;
        const QString name = QInputDialog::getText( p_intf->p_sys->p_mi, qtr( "New Profile" ),
                                                    qtr( "Profile name:" ), QLineEdit::Normal,
                                                    QString(), &ok ).trimmed();
        if( !ok || name.isEmpty() )
            return;
        QStringList all = RedesignMenus::profiles( p_intf );
        if( !all.contains( name ) )
            all << name;
        getSettings()->setValue( "Profiles/names", all );
        getSettings()->setValue( "Profiles/current", name );
        owner->setText( qtr( "Profile (%1)" ).arg( name ) );
    } );
}

QMenu *RedesignMenus::mainMenu( intf_thread_t *p_intf, QWidget *parent )
{
    QMenu *m = new QMenu( parent );
    m->setObjectName( "VLCMainMenu" );
    m->setToolTipsVisible( true );
    m->setMinimumWidth( 351 ); /* Figma width */
    DialogsProvider *dp = THEDP;

    m->addAction( menuIcon( "file" ), withShortcut( qtr( "Open File(s)" ), "Ctrl + O" ),
                  dp, &DialogsProvider::simpleOpenDialog );
    m->addAction( menuIcon( "folder" ), withShortcut( qtr( "Open Folder" ), "Ctrl + F" ),
                  dp, &DialogsProvider::PLOpenDir );
    m->addAction( menuIcon( "stream" ), withShortcut( qtr( "Open Network Stream" ), "Ctrl + N" ),
                  dp, &DialogsProvider::openNetDialog );
    m->addAction( menuIcon( "disc" ), withShortcut( qtr( "Open Disc" ), "Ctrl + D" ),
                  dp, &DialogsProvider::openDiscDialog );
    m->addAction( menuIcon( "capture" ), withShortcut( qtr( "Open Capture Device" ), "Ctrl + C" ),
                  dp, &DialogsProvider::openCaptureDialog );
    m->addAction( menuIcon( "convert" ), withShortcut( qtr( "Convert" ), "Ctrl + R" ),
                  dp, &DialogsProvider::openAndTranscodingDialogs );
    QMenu *recent = new QMenu( qtr( "Open Recent" ), m );
    recent->setToolTipsVisible( true );
    QObject::connect( recent, &QMenu::aboutToShow, recent, [p_intf, recent]() {
        buildRecentMenu( p_intf, recent );
    } );
    buildRecentMenu( p_intf, recent );
    m->addMenu( recent )->setIcon( menuIcon( "recent" ) );
    m->addSeparator();

    QMenu *profile = new QMenu( m );
    QAction *pa = m->addMenu( profile );
    pa->setText( qtr( "Profile (%1)" ).arg( currentProfile( p_intf ) ) );
    pa->setIcon( menuIcon( "profile" ) );
    pa->setToolTip( qtr( "Profiles are remembered now; what they switch "
                         "(layout, presets, theme) arrives with the Customize Player work" ) );
    QObject::connect( profile, &QMenu::aboutToShow, profile, [p_intf, profile, pa]() {
        buildProfileMenu( p_intf, profile, pa );
    } );
    buildProfileMenu( p_intf, profile, pa );

    m->addAction( menuIcon( "keyboard" ), qtr( "Keyboard Shortcuts" ),
                  dp, &DialogsProvider::hotkeysDialog );
    m->addAction( menuIcon( "customize" ), qtr( "Customize Player" ),
                  dp, &DialogsProvider::toolbarDialog );
    m->addAction( menuIcon( "settings" ), withShortcut( qtr( "Settings" ), "Ctrl + P" ),
                  dp, &DialogsProvider::prefsDialog );
    m->addAction( menuIcon( "info" ), withShortcut( qtr( "About VLC" ), "Shift + F1" ),
                  dp, &DialogsProvider::aboutDialog );
    return m;
}

/**********************************************************************
 * TrackPopover
 **********************************************************************/
enum { POP_W = 400, POP_H = 522, TAB_H = 50, ROW_H = 44, POP_GAP = 22 };

static QPointer<TrackPopover> s_popover;
static qint64 s_hiddenAt = 0;
static QAbstractButton *s_hiddenAnchor = NULL;

void TrackPopover::toggle( intf_thread_t *p_intf, QAbstractButton *anchor, int tab )
{
    if( s_popover && s_popover->isVisible() )
    {
        if( s_popover->anchor == anchor )
        {
            s_popover->close();
            return;
        }
        s_popover->close();
    }
    /* the press on the anchor already closed the popup: don't reopen */
    if( s_hiddenAnchor == anchor && QDateTime::currentMSecsSinceEpoch() - s_hiddenAt < 250 )
        return;

    TrackPopover *p = new TrackPopover( p_intf, anchor );
    s_popover = p;
    p->setTab( tab, false );

    /* right-aligned with the bar's end, 22px above it (Figma) */
    QWidget *win = anchor->window();
    QWidget *bar = anchor->parentWidget() ? anchor->parentWidget() : anchor;
    const QPoint winTopRight = win->mapToGlobal( QPoint( win->width(), 0 ) );
    const QPoint barTop = bar->mapToGlobal( QPoint( 0, 0 ) );
    int x = winTopRight.x() - Theme::BAR_MARGIN_RIGHT - POP_W;
    int y = barTop.y() - POP_GAP - POP_H;
    const QRect screen = QtCompat::screenOf( anchor ) ? QtCompat::screenOf( anchor )->availableGeometry() : QRect();
    if( screen.isValid() && y < screen.top() )
        y = barTop.y() + bar->height() + POP_GAP; /* no room above: below */
    p->move( x, y );
    p->show();
}

TrackPopover::TrackPopover( intf_thread_t *_p_intf, QAbstractButton *_anchor )
    : QFrame( NULL, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint ),
      p_intf( _p_intf ), anchor( _anchor ), tab( -1 )
{
    setObjectName( "VLCTrackPopover" );
    setAttribute( Qt::WA_TranslucentBackground );
    setAttribute( Qt::WA_DeleteOnClose );
    setFixedSize( POP_W, POP_H );

    QVBoxLayout *outer = new QVBoxLayout( this );
    outer->setContentsMargins( 0, 0, 0, 0 );
    QFrame *frame = new QFrame( this );
    frame->setObjectName( "TPFrame" );
    outer->addWidget( frame );
    QVBoxLayout *l = new QVBoxLayout( frame );
    l->setContentsMargins( 0, 0, 0, 0 );
    l->setSpacing( 0 );

    /* tab strip: three equal tabs, a 2px accent underline */
    tabBar = new QWidget( frame );
    tabBar->setObjectName( "TPTabs" );
    tabBar->setAttribute( Qt::WA_StyledBackground, true );
    tabBar->setFixedHeight( TAB_H );
    QHBoxLayout *tl = new QHBoxLayout( tabBar );
    tl->setContentsMargins( 0, 0, 0, 0 );
    tl->setSpacing( 0 );
    tabs = new QButtonGroup( this );
    const char *icons[] = { "video", "audio", "subtitles" };
    const QString names[] = { qtr( "Video" ), qtr( "Audio" ), qtr( "Subtitles" ) };
    for( int i = 0; i < 3; i++ )
    {
        QToolButton *b = new QToolButton( tabBar );
        b->setCheckable( true );
        b->setText( names[i] );
        b->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        b->setIconSize( QSize( 18, 18 ) );
        QIcon ic;
        ic.addPixmap( Theme::pixmap( icons[i], 18, QColor( 0x40, 0x40, 0x40 ) ), QIcon::Normal, QIcon::Off );
        ic.addPixmap( Theme::pixmap( icons[i], 18, Theme::accent() ), QIcon::Normal, QIcon::On );
        ic.addPixmap( Theme::pixmap( icons[i], 18, Theme::colors().textMuted ), QIcon::Active, QIcon::Off );
        ic.addPixmap( Theme::pixmap( icons[i], 18, Theme::accent() ), QIcon::Active, QIcon::On );
        b->setIcon( ic );
        const int w = i == 2 ? POP_W - 2 * ( POP_W / 3 ) : POP_W / 3;
        b->setFixedSize( w, TAB_H );
        /* stylesheet tool buttons left-align icon and text: centre them */
        QFont f = b->font();
        f.setPixelSize( 14 );
        const int content = 18 + 6 + QtCompat::textWidth( QFontMetrics( f ), names[i] );
        b->setStyleSheet( QString( "padding-left: %1px;" ).arg( qMax( 0, ( w - content ) / 2 ) ) );
        b->setCursor( Qt::PointingHandCursor );
        tabs->addButton( b, i );
        tl->addWidget( b, 1 );
    }
    underline = new QWidget( tabBar );
    underline->setObjectName( "TPTabLine" );
    underline->setAttribute( Qt::WA_StyledBackground, true );
    underlineAnim = new FrameAnimation( this );
    underlineAnim->setDuration( 180 );
    underlineAnim->setEasingCurve( QEasingCurve::OutCubic );
    connect( underlineAnim, &FrameAnimation::valueChanged, this, [this]( const QVariant &v ) {
        underline->setGeometry( v.toRect() );
    } );
    connect( tabs, QOverload<int>::of( &QButtonGroup::buttonClicked ), this,
             [this]( int id ) { setTab( id, true ); } );
    l->addWidget( tabBar );

    QScrollArea *area = new QScrollArea( frame );
    area->setObjectName( "TPScroll" );
    area->setWidgetResizable( true );
    area->setFrameShape( QFrame::NoFrame );
    area->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    pageHost = new QWidget;
    pageHost->setObjectName( "TPPage" );
    pageLayout = new QVBoxLayout( pageHost );
    pageLayout->setContentsMargins( 0, 12, 0, 8 );
    pageLayout->setSpacing( 0 );
    area->setWidget( pageHost );
    l->addWidget( area, 1 );

    /* footer: Show Preferences opens the adjustment sidebar on this tab */
    QWidget *footer = new QWidget( frame );
    QHBoxLayout *fl = new QHBoxLayout( footer );
    fl->setContentsMargins( 20, 8, 20, 19 );
    QPushButton *prefs = new QPushButton( qtr( "Show Preferences" ), footer );
    prefs->setObjectName( "TPPrefs" );
    prefs->setFixedHeight( 32 );
    prefs->setCursor( Qt::PointingHandCursor );
    connect( prefs, &QPushButton::clicked, this, [this]() {
        static const int map[] = { AdjustPanel::VIDEO_TAB, AdjustPanel::AUDIO_TAB,
                                   AdjustPanel::SUBTITLES_TAB };
        MainInterface *mi = p_intf->p_sys->p_mi;
        const int t = map[qBound( 0, tab, 2 )];
        close();
        if( mi )
            mi->showAdjustPanel( t );
    } );
    fl->addWidget( prefs );
    l->addWidget( footer );

    /* the anchor shows that its popover is open */
    if( anchor )
    {
        anchorIcon = anchor->icon();
        anchor->setIcon( QIcon( Theme::pixmap( anchor->property( "vlcGlyph" ).toString().isEmpty()
                                               ? QString( "subtitles" ) : anchor->property( "vlcGlyph" ).toString(),
                                               Theme::BAR_GLYPH, Theme::accent() ) ) );
    }

    /* follow track and device changes while open */
    connect( THEMIM->getIM(), &InputManager::voutChanged, this, &TrackPopover::rebuild );
    connect( THEMIM, &MainInputManager::inputChanged, this, &TrackPopover::rebuild );
}

void TrackPopover::hideEvent( QHideEvent *e )
{
    s_hiddenAt = QDateTime::currentMSecsSinceEpoch();
    s_hiddenAnchor = anchor;
    if( anchor )
        anchor->setIcon( anchorIcon );
    QFrame::hideEvent( e );
}

void TrackPopover::keyPressEvent( QKeyEvent *e )
{
    if( e->key() == Qt::Key_Left || e->key() == Qt::Key_Right )
    {
        setTab( ( tab + ( e->key() == Qt::Key_Right ? 1 : 2 ) ) % 3, true );
        return;
    }
    QFrame::keyPressEvent( e );
}

void TrackPopover::setTab( int t, bool animate )
{
    tab = t;
    tabs->button( t )->setChecked( true );
    rebuild();
    QTimer::singleShot( 0, this, [this, animate]() { moveUnderline( animate ); } );
}

void TrackPopover::moveUnderline( bool animate )
{
    QAbstractButton *b = tabs->checkedButton();
    if( !b )
        return;
    const QRect target( b->x() + 6, TAB_H - 2, b->width() - 12, 2 );
    underlineAnim->stop();
    if( !animate || underline->width() <= 0 )
    {
        underline->setGeometry( target );
        return;
    }
    underlineAnim->setStartValue( underline->geometry() );
    underlineAnim->setEndValue( target );
    underlineAnim->start();
}

QWidget *TrackPopover::sectionHeader( const QString &title, QWidget *extra )
{
    QWidget *w = new QWidget;
    w->setFixedHeight( ROW_H );
    QHBoxLayout *l = new QHBoxLayout( w );
    l->setContentsMargins( 27, 0, 20, 0 );
    QLabel *t = new QLabel( title );
    t->setObjectName( "TPSection" );
    l->addWidget( t );
    l->addStretch( 1 );
    if( extra )
        l->addWidget( extra );
    return w;
}

/* 44px row: accent check at x 27, text at x 61 */
QWidget *TrackPopover::row( const QString &text, bool checked, std::function<void()> onClick )
{
    QPushButton *r = new QPushButton;
    r->setObjectName( "TPRow" );
    r->setFlat( true );
    r->setText( text );
    r->setToolTip( text );
    r->setFixedHeight( ROW_H );
    r->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    r->setCursor( Qt::PointingHandCursor );
    r->setProperty( "current", checked );
    if( checked )
    {
        QLabel *c = new QLabel( r );
        c->setPixmap( Theme::pixmap( "check", 18, Theme::accent() ) );
        c->setAttribute( Qt::WA_TransparentForMouseEvents );
        c->move( 27, ( ROW_H - 18 ) / 2 );
    }
    if( onClick )
        connect( r, &QPushButton::clicked, this, [this, onClick]() {
            onClick();
            /* reflect the new selection once the core applied it */
            QTimer::singleShot( 120, this, &TrackPopover::rebuild );
        } );
    else
        r->setEnabled( false );
    return r;
}

void TrackPopover::addRule( QVBoxLayout *l )
{
    l->addSpacing( 8 );
    QFrame *f = new QFrame;
    f->setObjectName( "TPRule" );
    f->setFixedHeight( 1 );
    l->addWidget( f );
    l->addSpacing( 8 );
}

/* one row per elementary stream of the input variable; "Disable" reads None.
 * skipLast leaves out the last n streams (the external subtitles). */
void TrackPopover::fillEs( QVBoxLayout *l, const char *var, int skipLast )
{
    input_thread_t *p_input = THEMIM->getInput();
    if( !p_input )
    {
        l->addWidget( row( qtr( "Nothing is playing" ), false, nullptr ) );
        return;
    }
    vlc_value_t val, text;
    if( var_Change( p_input, var, VLC_VAR_GETCHOICES, &val, &text ) != VLC_SUCCESS )
        return;
    const int64_t cur = var_GetInteger( p_input, var );
    const int n = val.p_list->i_count;
    for( int i = 0; i < n - skipLast; i++ )
    {
        const int64_t id = val.p_list->p_values[i].i_int;
        const QString label = id < 0 ? qtr( "None" ) : qfu( text.p_list->p_values[i].psz_string );
        const QByteArray v( var );
        l->addWidget( row( label, id == cur, [this, v, id]() {
            if( input_thread_t *in = THEMIM->getInput() )
                var_SetInteger( in, v.constData(), id );
        } ) );
    }
    var_FreeList( &val, &text );
}

static QToolButton *headerIcon( const QString &glyph, const QString &tip )
{
    QToolButton *b = new QToolButton;
    b->setObjectName( "TPHeaderButton" );
    Theme::setupIconButton( b, glyph, 28, 18 );
    b->setToolTip( tip );
    return b;
}

void TrackPopover::rebuild()
{
    QLayoutItem *it;
    while( ( it = pageLayout->takeAt( 0 ) ) )
    {
        if( it->widget() )
            it->widget()->deleteLater();
        delete it;
    }
    QVBoxLayout *l = pageLayout;
    input_thread_t *p_input = THEMIM->getInput();

    if( tab == VIDEO_TAB )
    {
        l->addWidget( sectionHeader( qtr( "Embedded Tracks" ) ) );
        fillEs( l, "video-es" );
    }
    else if( tab == AUDIO_TAB )
    {
        l->addWidget( sectionHeader( qtr( "Embedded Tracks" ) ) );
        fillEs( l, "audio-es" );
        addRule( l );
        QToolButton *refresh = headerIcon( "refresh", qtr( "Look for devices again" ) );
        connect( refresh, &QToolButton::clicked, this, &TrackPopover::rebuild );
        l->addWidget( sectionHeader( qtr( "Output Device" ), refresh ) );
        audio_output_t *aout = THEMIM->getAout();
        if( !aout )
            l->addWidget( row( qtr( "Default" ), true, nullptr ) );
        else
        {
            char **ids, **names;
            const int n = aout_DevicesList( aout, &ids, &names );
            char *cur = aout_DeviceGet( aout );
            for( int i = 0; i < n; i++ )
            {
                const QByteArray id( ids[i] );
                l->addWidget( row( qfu( names[i] ), cur && !strcmp( cur, ids[i] ), [this, id]() {
                    /* fetched again: the output may have changed meanwhile */
                    if( audio_output_t *a = THEMIM->getAout() )
                    {
                        aout_DeviceSet( a, id.constData() );
                        vlc_object_release( a );
                    }
                } ) );
                free( ids[i] ); free( names[i] );
            }
            if( n > 0 ) { free( ids ); free( names ); }
            if( n <= 0 )
                l->addWidget( row( qtr( "Default" ), true, nullptr ) );
            free( cur );
            vlc_object_release( aout );
        }
    }
    else
    {
        /* External = the subtitle files attached to the item. VLC creates
         * their streams after the file's own, so they are the last ones in
         * spu-es; when the counts don't add up they are listed as files only */
        QStringList external;
        if( p_input )
        {
            input_item_t *item = input_GetItem( p_input );
            vlc_mutex_lock( &item->lock );
            for( int i = 0; i < item->i_slaves; i++ )
                if( item->pp_slaves[i] && item->pp_slaves[i]->i_type == SLAVE_TYPE_SPU )
                {
                    const QString uri = qfu( item->pp_slaves[i]->psz_uri );
                    if( !external.contains( uri ) )
                        external << uri;
                }
            vlc_mutex_unlock( &item->lock );
        }
        int streams = 0;
        QList<int64_t> ids;
        int64_t cur = -1;
        if( p_input )
        {
            vlc_value_t val, text;
            if( var_Change( p_input, "spu-es", VLC_VAR_GETCHOICES, &val, &text ) == VLC_SUCCESS )
            {
                for( int i = 0; i < val.p_list->i_count; i++ )
                    ids << val.p_list->p_values[i].i_int;
                var_FreeList( &val, &text );
            }
            cur = var_GetInteger( p_input, "spu-es" );
            streams = ids.count() - ( ids.contains( -1 ) ? 1 : 0 );
        }
        const bool mapped = !external.isEmpty() && external.count() <= streams;

        l->addWidget( sectionHeader( qtr( "Embedded Tracks" ) ) );
        fillEs( l, "spu-es", mapped ? external.count() : 0 );

        QWidget *icons = new QWidget;
        QHBoxLayout *il = new QHBoxLayout( icons );
        il->setContentsMargins( 0, 0, 0, 0 );
        il->setSpacing( 6 );
        QToolButton *edit = headerIcon( "edit", qtr( "Timing and appearance" ) );
        connect( edit, &QToolButton::clicked, this, [this]() {
            MainInterface *mi = p_intf->p_sys->p_mi;
            close();
            if( mi )
                mi->showAdjustPanel( AdjustPanel::SUBTITLES_TAB );
        } );
        QToolButton *add = headerIcon( "add", qtr( "Add Subtitles" ) );
        connect( add, &QToolButton::clicked, this, [this]() {
            close();
            THEDP->loadSubtitlesFile();
        } );
        il->addWidget( edit );
        il->addWidget( add );
        addRule( l );
        l->addWidget( sectionHeader( qtr( "External Tracks" ), icons ) );
        if( external.isEmpty() )
            l->addWidget( row( qtr( "None added" ), false, nullptr ) );
        for( int i = 0; i < external.count(); i++ )
        {
            const QUrl u( external[i] );
            const QString name = u.isLocalFile() ? QFileInfo( u.toLocalFile() ).fileName() : external[i];
            if( !mapped )
            {
                l->addWidget( row( name, false, nullptr ) );
                continue;
            }
            const int64_t id = ids.at( ids.count() - external.count() + i );
            l->addWidget( row( name, id == cur, [this, id]() {
                if( input_thread_t *in = THEMIM->getInput() )
                    var_SetInteger( in, "spu-es", id );
            } ) );
        }
    }
    l->addStretch( 1 );
}
