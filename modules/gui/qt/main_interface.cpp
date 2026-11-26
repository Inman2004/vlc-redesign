/*****************************************************************************
 * main_interface.cpp : Main interface
 ****************************************************************************
 * Copyright (C) 2006-2011 VideoLAN and AUTHORS
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Ilkka Ollakka <ileoo@videolan.org>
 *
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
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"
#include "util/qt_compat.hpp"
#include "util/iptv_catalog.hpp"

#include "main_interface.hpp"
#include "input_manager.hpp"                    // Creation
#include "actions_manager.hpp"                  // killInstance

#include "util/customwidgets.hpp"               // qtEventToVLCKey, QVLCStackedWidget
#include "util/qt_dirs.hpp"                     // toNativeSeparators
#include "util/imagehelper.hpp"

#include "components/interface_widgets.hpp"     // bgWidget, videoWidget
#include "components/controller.hpp"            // controllers
#include "components/playlist/playlist.hpp"     // plWidget
#include "components/header_bar.hpp"            // Redesign header
#include "components/resume_card.hpp"
#include "components/music_view.hpp"
#include "components/queue_panel.hpp"
#include "components/nav_rail.hpp"              // Redesign rail
#include "components/sidebar_panel.hpp"
#include "library/media_library.hpp"
#include "library/library_view.hpp"
#include "library/mini_player.hpp"         // Redesign sidebar
#include "components/adjust_panel.hpp"          // Redesign adjustments
#include "dialogs/redesign_dialogs.hpp"          // Remembered subtitles
#include "styles/vlc_theme.hpp"                 // Redesign tokens
#include "dialogs/firstrun.hpp"                 // First Run
#include "dialogs/playlist.hpp"                 // PlaylistDialog

#include "menus.hpp"                            // Menu creation
#include "recents.hpp"                          // RecentItems when DnD

#include <QCloseEvent>
#include <QKeyEvent>

#include <QUrl>
#include <QSize>
#include <QDate>
#include <QMimeData>

#include <QWindow>
#include <QMenu>
#include <QToolButton>
#include <QMenuBar>
#include <QLabel>
#include <QStackedWidget>
#include <QSplitter>
#include <QShortcut>
#include "util/frame_animation.hpp"
#include <QHBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <vlc_modules.h>
#include "util/key_map.hpp"
#include "util/bookmarks.hpp"
#ifdef _WIN32
#include <QFileInfo>
#endif

#ifndef QT_NO_STATUSBAR
# include <QStatusBar>
#endif

#if ! HAS_QT510 && defined(QT_HAS_X11)
# include <QX11Info>
# include <X11/Xlib.h>
#endif

#include <QTimer>

#include <vlc_actions.h>                    /* Wheel event */
#include <vlc_vout_display.h>               /* vout_thread_t and VOUT_ events */

// #define DEBUG_INTF

/* Callback prototypes */
static int PopupMenuCB( vlc_object_t *p_this, const char *psz_variable,
                        vlc_value_t old_val, vlc_value_t new_val, void *param );
static int IntfShowCB( vlc_object_t *p_this, const char *psz_variable,
                       vlc_value_t old_val, vlc_value_t new_val, void *param );
static int IntfBossCB( vlc_object_t *p_this, const char *psz_variable,
                       vlc_value_t old_val, vlc_value_t new_val, void *param );
static int IntfRaiseMainCB( vlc_object_t *p_this, const char *psz_variable,
                           vlc_value_t old_val, vlc_value_t new_val,
                           void *param );

const QEvent::Type MainInterface::ToolbarsNeedRebuild =
        (QEvent::Type)QEvent::registerEventType();

/*****************************************************************************
 * ChromeAutoHider (redesign, P2b)
 * Windowed counterpart of the fullscreen controller's hiding: with
 * qt-autohide-controls = Always the header, rail, sidebar and bar go away
 * while a video plays and the mouse rests on it ("Player - hidden" frame),
 * and come back on the next move. In Hide UI the I key peeks at them.
 * Nothing is resized: the video simply takes the freed space.
 *****************************************************************************/
static const QEvent::Type VoutMouseMovedEvent = QEvent::Type( QEvent::registerEventType() );

class ChromeAutoHider : public QObject
{
public:
    ChromeAutoHider( intf_thread_t *_p_intf, MainInterface *_mi )
        : QObject( _mi ), p_intf( _p_intf ), mi( _mi )
    {
        timer.setSingleShot( true );
        connect( &timer, &QTimer::timeout, this, [this]() { tick(); } );
        connect( THEMIM->getIM(), &InputManager::voutListChanged, this,
                 [this]( vout_thread_t **pp, int n ) { setVouts( pp, n ); },
                 Qt::DirectConnection );
        connect( THEMIM->getIM(), &InputManager::playingStatusChanged, this,
                 [this]( int ) { activity(); } );
        qApp->installEventFilter( this );
    }

    ~ChromeAutoHider()
    {
        qApp->removeEventFilter( this );
        setVouts( NULL, 0 );
    }

    bool isHidden() const { return autoHidden; }

    /* back to the state the view mode expects, before the mode changes */
    void reset()
    {
        timer.stop();
        if( autoHidden )
            mi->setChromeVisible( true );
        if( peeking )
            mi->setChromeVisible( false );
        autoHidden = peeking = false;
        arm();
    }

    /* the I key outside fullscreen */
    void peek()
    {
        if( mi->b_videoFullScreen )
            return;
        if( mi->b_minimalView )
        {
            peeking = !peeking;
            msg_Dbg( p_intf, "window chrome: peek %s", peeking ? "on" : "off" );
            mi->setChromeVisible( peeking );
            if( peeking )
            {
                peekCursor = QCursor::pos();
                timer.start( delay() );
            }
        }
        else if( autoHidden )
            activity();
    }

protected:
    bool eventFilter( QObject *obj, QEvent *e ) Q_DECL_OVERRIDE
    {
        if( e->type() == QEvent::MouseMove && obj->isWidgetType()
            && static_cast<QWidget *>( obj )->window() == mi && pointerMoved() )
            activity();
        return QObject::eventFilter( obj, e );
    }

    void customEvent( QEvent *e ) Q_DECL_OVERRIDE
    {
        if( e->type() == VoutMouseMovedEvent && pointerMoved() )
            activity();
    }

    /* A bar sliding under a still pointer makes the window system report
     * motion too; only a pointer that really moved is the user. */
    bool pointerMoved()
    {
        const QPoint p = QCursor::pos();
        if( p == lastPointer )
            return false;
        lastPointer = p;
        return true;
    }

private:
    static int MouseMoved( vlc_object_t *, const char *, vlc_value_t,
                           vlc_value_t, void *data )
    {
        QApplication::postEvent( static_cast<ChromeAutoHider *>( data ),
                                 new QEvent( VoutMouseMovedEvent ) );
        return VLC_SUCCESS;
    }

    void setVouts( vout_thread_t **pp, int n )
    {
        QList<vout_thread_t *> next;
        for( int i = 0; i < n; i++ )
            next << pp[i];
        foreach( vout_thread_t *v, vouts )
            if( !next.contains( v ) )
            {
                var_DelCallback( v, "mouse-moved", MouseMoved, this );
                vlc_object_release( v );
            }
        foreach( vout_thread_t *v, next )
            if( !vouts.contains( v ) )
            {
                vlc_object_hold( v );
                var_AddCallback( v, "mouse-moved", MouseMoved, this );
            }
        vouts = next;
    }

    int delay() const
    {
        return qMax( 300, (int)var_InheritInteger( p_intf, "mouse-hide-timeout" ) );
    }

    void arm()
    {
        if( mode() == 2 && !mi->b_minimalView && !mi->b_videoFullScreen )
            timer.start( delay() );
    }

    void activity()
    {
        if( mi->b_videoFullScreen )
            return;
        if( autoHidden )
        {
            autoHidden = false;
            msg_Dbg( p_intf, "window chrome: shown again" );
            mi->setChromeVisible( true );
        }
        if( peeking )
            timer.start( delay() );
        else
            arm();
    }

    /* the mouse rests on a chrome widget, or a menu or dialog is up */
    bool busy() const
    {
        if( qApp->activePopupWidget() || qApp->activeModalWidget() )
            return true;
        if( !mi->isActiveWindow() )
            return false;
        /* a peek appears under a cursor that did not move: that is not
         * the user resting on the controls */
        if( peeking && QCursor::pos() == peekCursor )
            return false;
        QWidget *w = QApplication::widgetAt( QCursor::pos() );
        for( ; w; w = w->parentWidget() )
            if( w == mi->header || w == mi->controls || w == mi->rail
                || w == mi->sidebar || w == (QWidget *)mi->adjustDock )
                return true;
        return false;
    }

    void tick()
    {
        if( mi->b_videoFullScreen )
            return;
        if( busy() )
        {
            timer.start( delay() );
            return;
        }
        if( peeking )
        {
            peeking = false;
            msg_Dbg( p_intf, "window chrome: peek ended" );
            if( mi->b_minimalView )
                mi->setChromeVisible( false );
            return;
        }
        if( mode() == 2 && !mi->b_minimalView && THEMIM->getIM()->hasVideo()
            && THEMIM->getIM()->playingStatus() == PLAYING_S )
        {
            autoHidden = true;
            msg_Dbg( p_intf, "window chrome: auto-hide" );
            mi->setChromeVisible( false );
        }
    }

    intf_thread_t *p_intf;
    MainInterface *mi;
    QTimer timer;
    int mode() const { return var_InheritInteger( p_intf, "qt-autohide-controls" ); }
    bool autoHidden = false, peeking = false;
    QPoint peekCursor;
    QPoint lastPointer;
    QList<vout_thread_t *> vouts;
};

/*****************************************************************************
 * ChromeOverlay (redesign)
 * While a video plays in the window and qt-autohide-controls is Always,
 * the header and the bar float over the video's top and bottom edges
 * instead of sitting beside it, so ChromeAutoHider can slide them away
 * without the video ever changing size. The rail, the sidebar and the
 * side panels keep clear of them with padding; only the video runs edge
 * to edge. Anywhere else (library, music, fullscreen, Hide UI) they dock.
 *****************************************************************************/
class ChromeOverlay : public QObject
{
public:
    ChromeOverlay( intf_thread_t *_p_intf, MainInterface *_mi )
        : QObject( _mi ), p_intf( _p_intf ), mi( _mi )
    {
        slide = new FrameAnimation( this );
        slide->setDuration( 240 );
        connect( slide, &FrameAnimation::valueChanged, this, [this]( const QVariant &v ) {
            shown = v.toReal();
            place();
        } );
        mi->centralWidget()->installEventFilter( this );

        /* re-decide once things settle, not on every intermediate signal */
        pending.setSingleShot( true );
        pending.setInterval( 0 );
        connect( &pending, &QTimer::timeout, this, [this]() { apply(); } );
        connect( THEMIM->getIM(), &InputManager::voutListChanged, this,
                 [this]( vout_thread_t **, int ) { update(); }, Qt::QueuedConnection );
        connect( THEMIM->getIM(), &InputManager::playingStatusChanged, this,
                 [this]( int ) { update(); } );
        connect( THEMIM, &MainInputManager::inputChanged, this, [this]( bool ) { update(); } );
    }

    bool isOn() const { return on; }

    /* a side panel built after the bars started floating needs the
     * same padding as the rest, or its top hides under the title bar */
    void repad() { if( on ) pad( true ); }

    /* the view changed: decide again, soon */
    void update() { pending.start(); }

    /* ChromeAutoHider: slide the header and the bar in or out */
    void slideTo( bool visible )
    {
        const qreal target = visible ? 1. : 0.;
        if( qFuzzyCompare( shown + 1., target + 1. ) && slide->state() != QAbstractAnimation::Running )
            return;
        slide->stop();
        slide->setEasingCurve( visible ? QEasingCurve::OutCubic : QEasingCurve::InOutCubic );
        slide->setStartValue( shown );
        slide->setEndValue( target );
        slide->start();
    }

    /* recreateToolbars() made a new bar and docked it */
    void barRecreated()
    {
        if( !on )
            return;
        mi->mainLayout->removeWidget( mi->controls );
        floatOver( mi->controls );
        place();
    }

protected:
    bool eventFilter( QObject *o, QEvent *e ) Q_DECL_OVERRIDE
    {
        if( on && o == mi->centralWidget() && e->type() == QEvent::Resize )
            place();
        return QObject::eventFilter( o, e );
    }

private:
    bool wanted( bool needVideo = true ) const
    {
        return mi->header && mi->controls
            && var_InheritInteger( p_intf, "qt-autohide-controls" ) == 2
            && !mi->b_videoFullScreen && !mi->isFullScreen()
            && !mi->b_minimalView && !mi->b_libraryMode
            && mi->videoWidget && mi->stackCentralW->currentWidget() == mi->videoWidget
            && ( !needVideo || THEMIM->getIM()->hasVideo() );
    }

    /* a native window of its own, so it can sit above the native video */
    void floatOver( QWidget *w )
    {
        w->setAttribute( Qt::WA_NativeWindow );
        w->show();
        w->raise();
    }

    /* keeps the side columns' content clear of the floating bars */
    void pad( bool b )
    {
        const QMargins m = b ? QMargins( 0, Theme::TITLEBAR_HEIGHT, 0, Theme::CONTROLBAR_HEIGHT )
                             : QMargins();
        QList<QWidget *> side;
        side << mi->rail << mi->sidebar << (QWidget *)mi->adjustDock << (QWidget *)mi->queueDock;
        foreach( QWidget *w, side )
            if( w )
                w->setContentsMargins( m );
    }

    void apply()
    {
        const bool want = wanted();
        /* the video output goes away for a moment when it is rebuilt (a
         * new format, the next item): docking then would make the video
         * jump twice, so give it a little time to come back */
        if( !want && on && wanted( false ) && THEMIM->getIM()->hasInput() )
        {
            if( !videoGone.isValid() )
                videoGone.start();
            /* a live channel takes seconds to tune in */
            if( videoGone.elapsed() < ( IptvCatalog::liveQueue() ? LIVE_GRACE_MS : VIDEO_GRACE_MS ) )
            {
                QTimer::singleShot( 100, this, [this]() { apply(); } );
                return;
            }
        }
        videoGone.invalidate();
        if( want == on )
            return;
        on = want;
        slide->stop();
        shown = 1.;
        msg_Dbg( p_intf, "window chrome: %s", on ? "floats over the video" : "docked" );
        if( on )
        {
            mi->mainLayout->removeWidget( mi->header );
            mi->mainLayout->removeWidget( mi->controls );
            floatOver( mi->header );
            floatOver( mi->controls );
            pad( true );
            place();
        }
        else
        {
            pad( false );
            mi->mainLayout->insertWidget( 0, mi->header );
            mi->mainLayout->addWidget( mi->controls );
        }
    }

    void place()
    {
        if( !on )
            return;
        QWidget *central = mi->centralWidget();
        const int w = central->width(), h = central->height();
        const int hh = Theme::TITLEBAR_HEIGHT, ch = Theme::CONTROLBAR_HEIGHT;
        mi->header->setGeometry( 0, -qRound( hh * ( 1. - shown ) ), w, hh );
        mi->controls->setGeometry( 0, h - qRound( ch * shown ), w, ch );
        mi->header->raise();
        mi->controls->raise();
    }

    intf_thread_t *p_intf;
    MainInterface *mi;
    FrameAnimation *slide;
    QTimer pending;
    QElapsedTimer videoGone;
    enum { VIDEO_GRACE_MS = 800, LIVE_GRACE_MS = 10000 };
    bool on = false;
    qreal shown = 1.;   /* 1: in view, 0: slid off the edges */
};

void MainInterface::setChromeVisible( bool b )
{
    if( !header || b_libraryMode )
        return;
    if( chromeOverlay && chromeOverlay->isOn() )
    {
        /* floating over the video: the bars slide away; the rail and the
         * library go too, so the video has the whole window (the queue
         * stays: it is the player's own) */
        chromeOverlay->slideTo( b );
        rail->setVisible( b );
        sidebar->setVisible( b && playlistVisible );
        if( adjustDock )
            adjustDock->setVisible( b && adjustDock->isOpen() );
        return;
    }
    header->setVisible( b );
    rail->setVisible( b );
    sidebar->setVisible( b && playlistVisible );
    if( adjustDock )
        adjustDock->setVisible( b && adjustDock->isOpen() );
    controls->setVisible( b );
}

MainInterface::MainInterface( intf_thread_t *_p_intf ) : QVLCMW( _p_intf )
{
    /* Variables initialisation */
    bgWidget             = NULL;
    videoWidget          = NULL;
    playlistWidget       = NULL;
    stackCentralOldWidget= NULL;
    lastWinScreen        = NULL;
    sysTray              = NULL;
    fullscreenControls   = NULL;
    cryptedLabel         = NULL;
    controls             = NULL;
    inputC               = NULL;
    header               = NULL;
    rail                 = NULL;
    sidebar              = NULL;
    adjustDock           = NULL;
    bodySplitter         = NULL;
    body                 = NULL;

    b_hideAfterCreation  = false; // --qt-start-minimized
    playlistVisible      = false;
    input_name           = "";
    b_interfaceFullScreen= false;
    b_hasPausedWhenMinimized = false;
    i_kc_offset          = false;
    b_maximizedView      = false;
    b_isWindowTiled      = false;

    /* Ask for Privacy */
    FirstRun::CheckAndRun( this, p_intf );

    /**
     *  Configuration and settings
     *  Pre-building of interface
     **/
    /* Main settings */
    setFocusPolicy( Qt::StrongFocus );
    setAcceptDrops( true );
    setWindowRole( "vlc-main" );
    setObjectName( "VLCMainWindow" );
    if( Theme::frameless() )
    {
        setWindowFlags( windowFlags() | Qt::FramelessWindowHint );
        new FramelessResizer( this );
    }
    setWindowIcon( QApplication::windowIcon() );
    setWindowOpacity( var_InheritFloat( p_intf, "qt-opacity" ) );

    /* Does the interface resize to video size or the opposite */
    b_autoresize = var_InheritBool( p_intf, "qt-video-autoresize" );

    /* Are we in the enhanced always-video mode or not ? */
    b_minimalView = var_InheritBool( p_intf, "qt-minimal-view" );

    /* Do we want anoying popups or not */
    i_notificationSetting = var_InheritInteger( p_intf, "qt-notification" );

    /* */
    b_pauseOnMinimize = var_InheritBool( p_intf, "qt-pause-minimized" );

    /* Set the other interface settings */
    settings = getSettings();

    /* */
    b_plDocked = getSettings()->value( "MainWindow/pl-dock-status", true ).toBool();
    if( Theme::redesign() )
        b_plDocked = true; /* the playlist lives in the sidebar */

    /* Should the UI stays on top of other windows */
    b_interfaceOnTop = var_InheritBool( p_intf, "video-on-top" );

    /**************************
     *  UI and Widgets design
     **************************/
    setVLCWindowsTitle();

    /************
     * Menu Bar *
     ************/
    VLCMenuBar::createMenuBar( this, p_intf );
    /* Redesign: the same menus are reached from the header's menu button */
    if( Theme::redesign() )
    {
        menuBar()->setFixedHeight( 0 ); /* stays "visible" so its shortcuts keep working */
        /* Hide UI, as in the design's context menu */
        /* Q: the Queue panel (onboarding step 3); VLC binds no plain q */
        QShortcut *queueKey = new QShortcut( QKeySequence( Qt::Key_Q ), this );
        connect( queueKey, &QShortcut::activated, this, [this]() {
            setQueuePanelVisible( !( queueDock && queueDock->isOpen() ) );
        } );
        QShortcut *hideUi = new QShortcut( QKeySequence( "Ctrl+\\" ), this );
        connect( hideUi, &QShortcut::activated, this, [this]() {
            /* in fullscreen there is no window chrome: keep the
             * controller from coming up until the next Ctrl+\ or I */
            if( b_videoFullScreen && fullscreenControls )
                fullscreenControls->setSuppressed( !fullscreenControls->isSuppressed() );
            else
                toggleMinimalView( !b_minimalView );
        } );
    }
    connect( THEMIM->getIM(), &InputManager::voutListChanged,
             THEDP, &DialogsProvider::destroyPopupMenu );

    createMainWidget( settings );
    if( Theme::redesign() )
    {
        autoHider = new ChromeAutoHider( p_intf, this );
        chromeOverlay = new ChromeOverlay( p_intf, this );
        /* the library scans in the background from the start (P5a) */
        MediaLibrary::instance( p_intf );
    }

#ifndef QT_NO_STATUSBAR
    /**************
     * Status Bar *
     **************/
    createStatusBar();
    setStatusBarVisibility( getSettings()->value( "MainWindow/status-bar-visible", false ).toBool() );
#endif

    /*********************************
     * Create the Systray Management *
     *********************************/
    initSystray();

    /*************************************************************
     * Connect the input manager to the GUI elements it manages  *
     * Beware initSystray did some connects on input manager too *
     *************************************************************/
    /**
     * Connects on nameChanged()
     * Those connects are different because options can impeach them to trigger.
     **/
    /* Main Interface statusbar */
    connect( THEMIM->getIM(), &InputManager::nameChanged,
             this, &MainInterface::setName );
    /* and title of the Main Interface*/
    if( var_InheritBool( p_intf, "qt-name-in-title" ) )
    {
        connect( THEMIM->getIM(), &InputManager::nameChanged,
                 this, &MainInterface::setVLCWindowsTitle );
    }
    connect( THEMIM, &MainInputManager::inputChanged, this, &MainInterface::onInputChanged );
    if( Theme::redesign() ) /* subtitles chosen with "To This Folder / Playlist" */
    {
        connect( THEMIM, &MainInputManager::playlistItemAppended, this, [this]( int id, int ) {
            SubtitleScope::attachToItem( p_intf, id );
        } );
        connect( THEMIM, &MainInputManager::inputChanged, this, [this]( bool b ) {
            if( b )
                SubtitleScope::applyToCurrent( p_intf );
        } );
        /* a file given on the command line may already be playing */
        QTimer::singleShot( 0, this, [this]() {
            /* the playing item first: attaching would mark it as handled */
            SubtitleScope::applyToCurrent( p_intf );
            SubtitleScope::attachToQueued( p_intf );
        } );
    }
    connect( THEMIM->getIM(), &InputManager::voutChanged, this, &MainInterface::onVOutChanged );

    /* END CONNECTS ON IM */

    /* VideoWidget connects for asynchronous calls */
    b_videoFullScreen = false;
    connect( this, &MainInterface::askGetVideo,
             this, &MainInterface::getVideoSlot,
             Qt::BlockingQueuedConnection );
    connect( this, &MainInterface::askReleaseVideo,
             this, &MainInterface::releaseVideoSlot,
             Qt::BlockingQueuedConnection );
    connect( this, &MainInterface::askVideoOnTop, this, &MainInterface::setVideoOnTop );

    if( videoWidget )
    {
        if( b_autoresize )
        {
            connect( videoWidget, &VideoWidget::sizeChanged,
                     this, &MainInterface::videoSizeChanged );
        }
        connect( this, &MainInterface::askVideoToResize,
                 this, &MainInterface::setVideoSize );

        connect( this, &MainInterface::askVideoSetFullScreen,
                 this, &MainInterface::setVideoFullScreen );
        connect( this, &MainInterface::askHideMouse,
                 this, &MainInterface::setHideMouse );
    }

    connect( THEDP, &DialogsProvider::toolBarConfUpdated, this, &MainInterface::toolBarConfUpdated );
    installEventFilter( this );

    connect( this, &MainInterface::askToQuit, THEDP, &DialogsProvider::quit );

    connect( this, &MainInterface::askBoss, this, &MainInterface::setBoss );
    connect( this, &MainInterface::askRaise, this, &MainInterface::setRaise );


    connect( THEDP, &DialogsProvider::releaseMouseEvents, this, &MainInterface::voutReleaseMouseEvents ) ;
    /** END of CONNECTS**/


    /************
     * Callbacks
     ************/
    var_AddCallback( p_intf->obj.libvlc, "intf-toggle-fscontrol", IntfShowCB, p_intf );
    var_AddCallback( p_intf->obj.libvlc, "intf-boss", IntfBossCB, p_intf );
    var_AddCallback( p_intf->obj.libvlc, "intf-show", IntfRaiseMainCB, p_intf );

    /* Register callback for the intf-popupmenu variable */
    var_AddCallback( p_intf->obj.libvlc, "intf-popupmenu", PopupMenuCB, p_intf );


    /* Final Sizing, restoration and placement of the interface */
    if( Theme::redesign() )
    {
        lastSection = settings->value( "MainWindow/sidebar-section", "playlists" ).toString();
        /* opened to play a file (from the file manager): the player
         * alone, the library stays closed until asked for */
        if( settings->value( "MainWindow/sidebar-visible", true ).toBool()
         && !var_InheritBool( p_intf, "started-from-file" ) )
            activateSection( lastSection );
        else
            setSidebarVisible( false );
        if( settings->value( "MainWindow/adjust-visible", false ).toBool() )
            setAdjustPanelVisible( true );
    }
    else if( settings->value( "MainWindow/playlist-visible", false ).toBool() )
        togglePlaylist();

    /* the bindings the core loaded; edits in Keyboard Shortcuts apply
     * through KeyMap without a restart */
    KeyMap::init( p_intf );
    /* bookmarks come back the next time a file plays (P7) */
    if( Theme::redesign() )
        Bookmarks::init( p_intf );

    /* Settings > Interface > Remember window size and position */
    if( !Theme::redesign() || settings->value( "Settings/rememberGeometry", true ).toBool() )
        QVLCTools::restoreWidgetPosition( settings, this, QSize(600, 420) );
    else if( QScreen *scr = QGuiApplication::primaryScreen() )
    {
        QRect avail = scr->availableGeometry();
        resize( avail.size() * 3 / 4 );
        move( avail.center() - rect().center() );
    }

    if( Theme::redesign() )
    {
        /* Settings > Audio > Startup volume: "Always starts at" */
        const int startVol = settings->value( "Settings/startupVolume", -1 ).toInt();
        if( startVol >= 0 )
            playlist_VolumeSet( THEPL, startVol / 100.f );
        /* Settings > Recording > Discover cast devices */
        if( settings->value( "Settings/castDiscovery", false ).toBool() && module_exists( "microdns" ) )
            ActionsManager::getInstance( p_intf )->StartRendererScan();
    }

    b_interfaceFullScreen = isFullScreen();

    setVisible( !b_hideAfterCreation );

    /* Switch to minimal view if needed, must be called after the show() */
    if( b_minimalView )
        toggleMinimalView( true );

    computeMinimumSize();
}

MainInterface::~MainInterface()
{
    /* Unsure we hide the videoWidget before destroying it */
    if( stackCentralOldWidget == videoWidget )
        showTab( bgWidget );

    if( videoWidget )
        releaseVideoSlot(true);

    /* Be sure to kill the actionsManager... Only used in the MI and control */
    ActionsManager::killInstance();

    /* Delete the FSC controller */
    delete fullscreenControls;

    /* Save states */

    settings->beginGroup("MainWindow");
    settings->setValue( "pl-dock-status", b_plDocked );

    /* Save playlist state */
    settings->setValue( "playlist-visible", playlistVisible );

    settings->setValue( "adv-controls",
                        getControlsVisibilityStatus() & CONTROLS_ADVANCED );
    settings->setValue( "status-bar-visible", b_statusbarVisible );
    if( sidebar )
    {
        settings->setValue( "sidebar-visible", playlistVisible );
        settings->setValue( "sidebar-section", lastSection );
        settings->setValue( "sidebar-splitter", bodySplitter->saveState() );
        settings->setValue( "adjust-visible", adjustDock && adjustDock->isOpen() );
    }

    /* Save the stackCentralW sizes */
    settings->setValue( "bgSize", stackWidgetsSizes[bgWidget] );
    settings->setValue( "playlistSize", stackWidgetsSizes[playlistWidget] );
    settings->endGroup();

    /* Save this size */
    QVLCTools::saveWidgetPosition(settings, this);

    /* Unregister callbacks */
    var_DelCallback( p_intf->obj.libvlc, "intf-boss", IntfBossCB, p_intf );
    var_DelCallback( p_intf->obj.libvlc, "intf-show", IntfRaiseMainCB, p_intf );
    var_DelCallback( p_intf->obj.libvlc, "intf-toggle-fscontrol", IntfShowCB, p_intf );
    var_DelCallback( p_intf->obj.libvlc, "intf-popupmenu", PopupMenuCB, p_intf );

    p_intf->p_sys->p_mi = NULL;
}

void MainInterface::computeMinimumSize()
{
    if( Theme::redesign() )
    {
        /* the bar folds to fit (ControlsWidget::fitToWidth) */
        setMinimumWidth( 480 );
        return;
    }
    int minWidth = 80;
    if( menuBar()->isVisible() )
        minWidth += controls->sizeHint().width();

    setMinimumWidth( minWidth );
}

/*****************************
 *   Main UI handling        *
 *****************************/
void MainInterface::recreateToolbars()
{
    bool b_adv = getControlsVisibilityStatus() & CONTROLS_ADVANCED;

    delete controls;
    delete inputC;

    controls = new ControlsWidget( p_intf, b_adv, this, b_musicMode );
    inputC = new InputControlsWidget( p_intf, this );
    if( resumeCard )
        resumeCard->setBar( controls );
    if( Theme::redesign() )
    {
        mainLayout->addWidget( inputC );
        mainLayout->addWidget( controls );
        if( chromeOverlay )
            chromeOverlay->barRecreated();
    }
    else
    {
        mainLayout->insertWidget( 2, inputC );
        mainLayout->insertWidget( settings->value( "MainWindow/ToolbarPos", false ).toBool() ? 0: 3,
                                  controls );
    }

    if( fullscreenControls )
    {
        delete fullscreenControls;
        fullscreenControls = new FullscreenControllerWidget( p_intf, this );
        connect( fullscreenControls, &FullscreenControllerWidget::keyPressed,
                 this, &MainInterface::handleKeyPress );
        THEMIM->requestVoutUpdate();
    }

    setMinimalView( b_minimalView );
}

void MainInterface::reloadPrefs()
{
    i_notificationSetting = var_InheritInteger( p_intf, "qt-notification" );
    b_pauseOnMinimize = var_InheritBool( p_intf, "qt-pause-minimized" );
    if( !var_InheritBool( p_intf, "qt-fs-controller" ) && fullscreenControls )
    {
        delete fullscreenControls;
        fullscreenControls = NULL;
    }
}

void MainInterface::createResumePanel( QWidget *w )
{
    resumePanel = new QWidget( w );
    resumePanel->hide();
    QHBoxLayout *resumePanelLayout = new QHBoxLayout( resumePanel );
    resumePanelLayout->setSpacing( 0 ); resumePanelLayout->setContentsMargins( 0, 0, 0, 0 );

    QLabel *continuePixmapLabel = new QLabel();
    continuePixmapLabel->setPixmap( ImageHelper::loadSvgToPixmap( ":/menu/help.svg" , fontMetrics().height(), fontMetrics().height()) );
    continuePixmapLabel->setContentsMargins( 5, 0, 5, 0 );

    QLabel *continueLabel = new QLabel( qtr( "Do you want to restart the playback where left off?") );

    QToolButton *cancel = new QToolButton( resumePanel );
    cancel->setAutoRaise( true );
    cancel->setText( "X" );

    QPushButton *ok = new QPushButton( qtr( "&Continue" )  );

    resumePanelLayout->addWidget( continuePixmapLabel );
    resumePanelLayout->addWidget( continueLabel );
    resumePanelLayout->addStretch( 1 );
    resumePanelLayout->addWidget( ok );
    resumePanelLayout->addWidget( cancel );

    resumeTimer = new QTimer( resumePanel );
    resumeTimer->setSingleShot( true );
    resumeTimer->setInterval( 6000 );

    connect( resumeTimer, &QTimer::timeout, this, &MainInterface::hideResumePanel );
    connect( cancel, &QToolButton::clicked, this, &MainInterface::hideResumePanel );
    connect( THEMIM->getIM(), &InputManager::resumePlayback, this, &MainInterface::showResumePanel );
    BUTTONACT( ok, resumePlayback );

    w->layout()->addWidget( resumePanel );
}

void MainInterface::showResumePanel( int64_t _time ) {
    int setting = var_InheritInteger( p_intf, "qt-continue" );

    if( setting == 0 )
        return;

    i_resumeTime = _time;

    if( setting == 2)
        resumePlayback();
    else if( Theme::redesign() )
    {
        /* Figma "continue": a card over the video, not a bar */
        if( !resumeCard )
        {
            resumeCard = new ResumeCard( this, controls );
            connect( resumeCard, &ResumeCard::continueRequested, this, &MainInterface::resumePlayback );
            connect( resumeCard, &ResumeCard::dismissed, this, &MainInterface::hideResumePanel );
        }
        resumeCard->showFor( _time );
        resumeTimer->start();
    }
    else
    {
        if( !isFullScreen() && !isMaximized() && !b_isWindowTiled )
            resizeWindow( width(), height() + resumePanel->height() );
        resumePanel->setVisible(true);
        resumeTimer->start();
    }
}

void MainInterface::hideResumePanel()
{
    if( resumeCard && resumeCard->isVisible() )
    {
        resumeCard->hide();
        resumeTimer->stop();
    }
    if( resumePanel->isVisible() )
    {
        if( !isFullScreen() && !isMaximized() && !b_isWindowTiled )
            resizeWindow( width(), height() - resumePanel->height() );
        resumePanel->hide();
        resumeTimer->stop();
    }
}

void MainInterface::resumePlayback()
{
    if( THEMIM->getIM()->hasInput() ) {
        var_SetInteger( THEMIM->getInput(), "time", i_resumeTime );
    }
    hideResumePanel();
}

void MainInterface::onInputChanged( bool hasInput )
{
    if( hasInput == false )
        return;
    int autoRaise = var_InheritInteger( p_intf, "qt-auto-raise" );
    if ( autoRaise == MainInterface::RAISE_NEVER )
        return;
    if( THEMIM->getIM()->hasVideo() == true )
    {
        if( ( autoRaise & MainInterface::RAISE_VIDEO ) == 0 )
            return;
    }
    else if ( ( autoRaise & MainInterface::RAISE_AUDIO ) == 0 )
        return;
    emit askRaise();
}

void MainInterface::onVOutChanged( bool video )
{
    int autoRaise = var_InheritInteger( p_intf, "qt-auto-raise" );
    if ( video && ( autoRaise & MainInterface::RAISE_VIDEO ) )
        emit askRaise();
}

void MainInterface::createMainWidget( QSettings *creationSettings )
{
    /* Create the main Widget and the mainLayout */
    QWidget *main = new QWidget;
    setCentralWidget( main );
    mainLayout = new QVBoxLayout( main );
    main->setContentsMargins( 0, 0, 0, 0 );
    mainLayout->setSpacing( 0 ); mainLayout->setContentsMargins( 0, 0, 0, 0 );

    /* Redesign: header on top, then rail | sidebar | content */
    QWidget *contentPanel = NULL;
    if( Theme::redesign() )
    {
        main->setObjectName( "VLCCentral" );
        main->setAttribute( Qt::WA_StyledBackground, true );

        header = new HeaderBar( p_intf, this );
        mainLayout->addWidget( header );
        connect( header, &HeaderBar::sidebarToggled, this, &MainInterface::setSidebarVisible );
    }

    createResumePanel( main );

    if( Theme::redesign() )
    {
        body = new QWidget( main );
        body->setObjectName( "VLCBody" );
        QHBoxLayout *bodyLayout = new QHBoxLayout( body );
        bodyLayout->setContentsMargins( 0, 0, Theme::CONTENT_MARGIN, 0 );
        bodyLayout->setSpacing( 0 );

        rail = new NavRail( body );
        bodyLayout->addWidget( rail );
        connect( rail, &NavRail::sectionActivated, this, &MainInterface::activateSection );
        connect( rail, &NavRail::collapseRequested, this, [this]() { setSidebarVisible( false ); } );
        connect( rail, &NavRail::addRequested, THEDP, &DialogsProvider::simpleOpenDialog );

        bodySplitter = new QSplitter( Qt::Horizontal, body );
        bodySplitter->setHandleWidth( 1 );
        bodySplitter->setChildrenCollapsible( false );
        bodyLayout->addWidget( bodySplitter, 1 );

        sidebar = new SidebarPanel( p_intf, bodySplitter );
        bodySplitter->addWidget( sidebar );

        contentPanel = new QWidget( bodySplitter );
        contentPanel->setObjectName( "VLCContent" );
        contentPanel->setAttribute( Qt::WA_StyledBackground, true );
        QVBoxLayout *contentLayout = new QVBoxLayout( contentPanel );
        contentLayout->setContentsMargins( 0, 0, 0, 0 );
        bodySplitter->addWidget( contentPanel );
        bodySplitter->setStretchFactor( 0, 0 );
        bodySplitter->setStretchFactor( 1, 1 );
        if( !bodySplitter->restoreState(
                creationSettings->value( "MainWindow/sidebar-splitter" ).toByteArray() ) )
            bodySplitter->setSizes( QList<int>() << Theme::SIDEBAR_WIDTH << 800 );

        mainLayout->addWidget( body, 1 );

        /* the library window state replaces the body (P5a) */
        libraryView = new LibraryView( p_intf, main );
        libraryView->hide();
        mainLayout->addWidget( libraryView, 1 );
        connect( libraryView, &LibraryView::closeRequested, this, [this]() { setLibraryMode( false ); } );
        /* playing from the library stays there: the mini player shows it */
        connect( libraryView->miniPlayer(), &MiniPlayer::expandRequested, this,
                 [this]() { setLibraryMode( false ); } );
        connect( libraryView->miniPlayer(), &MiniPlayer::queueRequested, this, [this]() {
            setLibraryMode( false );
            activateSection( "playlists" );
            setSidebarVisible( true );
        } );
        connect( THEMIM->getIM(), &InputManager::voutChanged, this, [this]() {
            QTimer::singleShot( 0, this, &MainInterface::placeVideo );
        } );
        connect( libraryView, &LibraryView::playerSectionRequested, this, [this]( const QString &key ) {
            setLibraryMode( false );
            activateSection( key );
        } );
    }

    /* */
    stackCentralW = new QVLCStackedWidget( contentPanel ? contentPanel : main );

    /* Bg Cone */
    if ( QDate::currentDate().dayOfYear() >= QT_XMAS_JOKE_DAY
         && var_InheritBool( p_intf, "qt-icon-change" ) )
    {
        bgWidget = new EasterEggBackgroundWidget( p_intf );
        connect( this, SIGNAL( kc_pressed() ), bgWidget, SLOT( animate() ) );
    }
    else
        bgWidget = new BackgroundWidget( p_intf );

    stackCentralW->addWidget( bgWidget );
    if( Theme::redesign() )
    {
        /* let the rounded content panel show through */
        bgWidget->setAutoFillBackground( false );
        /* music mode (Figma row 2 #8-9): its own picture and bar */
        musicView = new MusicView( p_intf, bgWidget );
        connect( musicView, &MusicView::activeChanged, this, [this]( bool on ) {
            if( on == b_musicMode )
                return;
            b_musicMode = on;
            recreateToolbars();
        } );
    }
    if ( !var_InheritBool( p_intf, "qt-bgcone" ) )
        bgWidget->setWithArt( false );
    else
        if ( var_InheritBool( p_intf, "qt-bgcone-expands" ) )
            bgWidget->setExpandstoHeight( true );

    /* And video Outputs */
    if( var_InheritBool( p_intf, "embedded-video" ) )
    {
        videoWidget = new VideoWidget( p_intf, stackCentralW );
        stackCentralW->addWidget( videoWidget );
    }
    if( contentPanel )
        contentPanel->layout()->addWidget( stackCentralW );
    else
        mainLayout->insertWidget( 1, stackCentralW );

    stackWidgetsSizes[bgWidget] =
        creationSettings->value( "MainWindow/bgSize", QSize( 600, 0 ) ).toSize();
    /* Resize even if no-auto-resize, because we are at creation */
    resizeStack( stackWidgetsSizes[bgWidget].width(), stackWidgetsSizes[bgWidget].height() );

    /* Create the CONTROLS Widget */
    controls = new ControlsWidget( p_intf,
        creationSettings->value( "MainWindow/adv-controls", false ).toBool(), this );
    inputC = new InputControlsWidget( p_intf, this );

    if( Theme::redesign() )
    {
        mainLayout->addWidget( inputC );
        mainLayout->addWidget( controls );
        if( chromeOverlay )
            chromeOverlay->barRecreated();
    }
    else
    {
        mainLayout->insertWidget( 2, inputC );
        mainLayout->insertWidget(
            creationSettings->value( "MainWindow/ToolbarPos", false ).toBool() ? 0: 3,
            controls );
    }

    /* Visualisation, disabled for now, they SUCK */
    #if 0
    visualSelector = new VisualSelector( p_intf );
    mainLayout->insertWidget( 0, visualSelector );
    visualSelector->hide();
    #endif


    /* Enable the popup menu in the MI */
    main->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( main, &QWidget::customContextMenuRequested,
             THEDP, &DialogsProvider::setPopupMenu );

    if ( depth() > 8 ) /* 8bit depth has too many issues with opacity */
        /* Create the FULLSCREEN CONTROLS Widget */
        if( var_InheritBool( p_intf, "qt-fs-controller" ) )
        {
            fullscreenControls = new FullscreenControllerWidget( p_intf, this );
            connect( fullscreenControls, &FullscreenControllerWidget::keyPressed,
                     this, &MainInterface::handleKeyPress );
        }

    if ( b_interfaceOnTop )
        setWindowFlags( windowFlags() | Qt::WindowStaysOnTopHint );
}

inline void MainInterface::initSystray()
{
    bool b_systrayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
    bool b_systrayWanted = var_InheritBool( p_intf, "qt-system-tray" );

    if( var_InheritBool( p_intf, "qt-start-minimized") )
    {
        if( b_systrayAvailable )
        {
            b_systrayWanted = true;
            b_hideAfterCreation = true;
        }
        else
            msg_Err( p_intf, "cannot start minimized without system tray bar" );
    }

    if( b_systrayAvailable && b_systrayWanted )
        createSystray();
}

#ifndef QT_NO_STATUSBAR
inline void MainInterface::createStatusBar()
{
    /****************
     *  Status Bar  *
     ****************/
    /* Widgets Creation*/
    QStatusBar *statusBarr = statusBar();

    TimeLabel *timeLabel = new TimeLabel( p_intf );
    nameLabel = new ClickableQLabel();
    nameLabel->setTextInteractionFlags( Qt::TextSelectableByMouse
                                      | Qt::TextSelectableByKeyboard );
    SpeedLabel *speedLabel = new SpeedLabel( p_intf, this );

    /* Styling those labels */
    timeLabel->setFrameStyle( QFrame::Sunken | QFrame::Panel );
    speedLabel->setFrameStyle( QFrame::Sunken | QFrame::Panel );
    nameLabel->setFrameStyle( QFrame::Sunken | QFrame::StyledPanel);
    auto updateStyle = [=]() {
        timeLabel->setStyleSheet(
            "QLabel:hover { color: black; background-color: rgba(255, 255, 255, 50%) }" );
        speedLabel->setStyleSheet(
            "QLabel:hover { color: black; background-color: rgba(255, 255, 255, 50%) }" );
        /* pad both label and its tooltip */
        nameLabel->setStyleSheet( "padding-left: 5px; padding-right: 5px;" );

    };
    updateStyle();
//same as Qt::AA_UseStyleSheetPropagationInWidgetStyles
#if !HAS_QT57
    connect(qApp, &QApplication::paletteChanged, this, [updateStyle](){
        updateStyle();
    });
#endif

    /* and adding those */
    statusBarr->addWidget( nameLabel, 8 );
    statusBarr->addPermanentWidget( speedLabel, 0 );
    statusBarr->addPermanentWidget( timeLabel, 0 );

    connect( nameLabel, SIGNAL( doubleClicked() ), THEDP, SLOT( epgDialog() ) );
    /* timeLabel behaviour:
       - double clicking opens the goto time dialog
       - right-clicking and clicking just toggle between remaining and
         elapsed time.*/
    connect( timeLabel, &TimeLabel::doubleClicked, THEDP, &DialogsProvider::gotoTimeDialog );

#ifndef QT_NO_STATUSBAR
    connect( THEMIM->getIM(), &InputManager::encryptionChanged,
             this, &MainInterface::showCryptedLabel );
#endif

    /* This shouldn't be necessary, but for somehow reason, the statusBarr
       starts at height of 20px and when a text is shown it needs more space.
       But, as the QMainWindow policy doesn't allow statusBar to change QMW's
       geometry, we need to force a height. If you have a better idea, please
       tell me -- jb
     */
    statusBarr->setFixedHeight( statusBarr->sizeHint().height() + 2 );
}
#endif

/**********************************************************************
 * Handling of sizing of the components
 **********************************************************************/

void MainInterface::debug()
{
#ifdef DEBUG_INTF
    if( controls ) {
        msg_Dbg( p_intf, "Controls size: %i - %i", controls->size().height(), controls->size().width() );
        msg_Dbg( p_intf, "Controls minimumsize: %i - %i", controls->minimumSize().height(), controls->minimumSize().width() );
        msg_Dbg( p_intf, "Controls sizeHint: %i - %i", controls->sizeHint().height(), controls->sizeHint().width() );
    }

    msg_Dbg( p_intf, "size: %i - %i", size().height(), size().width() );
    msg_Dbg( p_intf, "sizeHint: %i - %i", sizeHint().height(), sizeHint().width() );
    msg_Dbg( p_intf, "minimumsize: %i - %i", minimumSize().height(), minimumSize().width() );

    msg_Dbg( p_intf, "Stack size: %i - %i", stackCentralW->size().height(), stackCentralW->size().width() );
    msg_Dbg( p_intf, "Stack sizeHint: %i - %i", stackCentralW->sizeHint().height(), stackCentralW->sizeHint().width() );
    msg_Dbg( p_intf, "Central size: %i - %i", centralWidget()->size().height(), centralWidget()->size().width() );
#endif
}

inline void MainInterface::showVideo() { showTab( videoWidget ); }
inline void MainInterface::restoreStackOldWidget( bool video_closing )
            { showTab( stackCentralOldWidget, video_closing ); }

inline void MainInterface::showTab( QWidget *widget, bool video_closing )
{
    if ( !widget ) widget = bgWidget; /* trying to restore a null oldwidget */
#ifdef DEBUG_INTF
    if ( stackCentralOldWidget )
        msg_Dbg( p_intf, "Old stackCentralOldWidget %s at index %i",
                 stackCentralOldWidget->metaObject()->className(),
                 stackCentralW->indexOf( stackCentralOldWidget ) );
    msg_Dbg( p_intf, "ShowTab request for %s", widget->metaObject()->className() );
#endif
    if ( stackCentralW->currentWidget() == widget )
        return;

    /* fixing when the playlist has been undocked after being hidden.
       restoreStackOldWidget() is called when video stops but
       stackCentralOldWidget would still be pointing to playlist */
    if ( widget == playlistWidget && !isPlDocked() )
        widget = bgWidget;

    stackCentralOldWidget = stackCentralW->currentWidget();
    if( !isFullScreen() )
        stackWidgetsSizes[stackCentralOldWidget] = stackCentralW->size();

    /* If we are playing video, embedded */
    if( !video_closing && videoWidget && THEMIM->getIM()->hasVideo() )
    {
        /* Video -> Playlist */
        if( videoWidget == stackCentralOldWidget && widget == playlistWidget )
        {
            stackCentralW->removeWidget( videoWidget );
            videoWidget->show(); videoWidget->raise();
        }

        /* Playlist -> Video */
        if( playlistWidget == stackCentralOldWidget && widget == videoWidget )
        {
            playlistWidget->artContainer->removeWidget( videoWidget );
            videoWidget->show(); videoWidget->raise();
            stackCentralW->addWidget( videoWidget );
        }

        /* Embedded playlist -> Non-embedded playlist */
        if( bgWidget == stackCentralOldWidget && widget == videoWidget )
        {
            /* In rare case when video is started before the interface */
            if( playlistWidget != NULL )
                playlistWidget->artContainer->removeWidget( videoWidget );
            videoWidget->show(); videoWidget->raise();
            stackCentralW->addWidget( videoWidget );
            stackCentralW->setCurrentWidget( videoWidget );
        }
    }

    stackCentralW->setCurrentWidget( widget );
    if( b_autoresize )
        resizeStack( stackWidgetsSizes[widget].width(), stackWidgetsSizes[widget].height() );

#ifdef DEBUG_INTF
    msg_Dbg( p_intf, "Stack state changed to %s, index %i",
              stackCentralW->currentWidget()->metaObject()->className(),
              stackCentralW->currentIndex() );
    msg_Dbg( p_intf, "New stackCentralOldWidget %s at index %i",
              stackCentralOldWidget->metaObject()->className(),
              stackCentralW->indexOf( stackCentralOldWidget ) );
#endif

    /* This part is done later, to account for the new pl size */
    if( !video_closing && videoWidget && THEMIM->getIM()->hasVideo() &&
        videoWidget == stackCentralOldWidget && widget == playlistWidget )
    {
        playlistWidget->artContainer->addWidget( videoWidget );
        playlistWidget->artContainer->setCurrentWidget( videoWidget );
    }
}

void MainInterface::toggleFSC()
{
   /* called from the hotkeys thread: I peeks at the chrome when windowed */
   if( Theme::redesign() )
       QtCompat::invokeQueued( this, [this]() {
           if( autoHider && !b_videoFullScreen )
               autoHider->peek();
       }, Qt::QueuedConnection );
   if( !fullscreenControls ) return;

   IMEvent *eShow = new IMEvent( IMEvent::FullscreenControlToggle );
   QApplication::postEvent( fullscreenControls, eShow );
}

/****************************************************************************
 * Video Handling
 ****************************************************************************/

/**
 * NOTE:
 * You must not change the state of this object or other Qt UI objects,
 * from the video output thread - only from the Qt UI main loop thread.
 * All window provider queries must be handled through signals or events.
 * That's why we have all those emit statements...
 */
bool MainInterface::getVideo( struct vout_window_t *p_wnd,
                              unsigned int i_width, unsigned int i_height,
                              bool fullscreen )
{
    bool result;

    /* This is a blocking call signal. Results are stored directly in the
     * vout_window_t and boolean pointers. Beware of deadlocks! */
    emit askGetVideo( p_wnd, i_width, i_height, fullscreen, &result );
    return result;
}

void MainInterface::getVideoSlot( struct vout_window_t *p_wnd,
                                  unsigned i_width, unsigned i_height,
                                  bool fullscreen, bool *res )
{
    /* Hidden or minimized, activate */
    if( isHidden() || isMinimized() )
        toggleUpdateSystrayMenu();

    /* Request the videoWidget */
    if ( !videoWidget )
    {
        videoWidget = new VideoWidget( p_intf, stackCentralW );
        stackCentralW->addWidget( videoWidget );
    }
    *res = videoWidget->request( p_wnd );
    if( *res ) /* The videoWidget is available */
    {
        setVideoFullScreen( fullscreen );

        /* Consider the video active now */
        showVideo();
        if( b_libraryMode )
            QTimer::singleShot( 0, this, &MainInterface::placeVideo );

        /* Ask videoWidget to resize correctly, if we are in normal mode
         * (in the library the video sits in the fixed-size mini player) */
        if( b_autoresize && !b_libraryMode && !IptvCatalog::liveQueue() ) {
#if HAS_QT56
            qreal factor = videoWidget->devicePixelRatioF();

            i_width = qRound( (qreal) i_width / factor );
            i_height = qRound( (qreal) i_height / factor );
#endif

            videoWidget->setSize( i_width, i_height );
        }
    }
}

/* Asynchronous call from the WindowClose function */
void MainInterface::releaseVideo( void )
{
    emit askReleaseVideo(false);
}

/* Function that is CONNECTED to the previous emit */
void MainInterface::releaseVideoSlot( bool forced )
{
    /* This function is called when the embedded video window is destroyed,
     * or in the rare case that the embedded window is still here but the
     * Qt interface exits. */
    assert( videoWidget );
    videoWidget->release( forced );
    setVideoOnTop( false );
    setVideoFullScreen( false );
    hideResumePanel();

    if( stackCentralW->currentWidget() == videoWidget )
        restoreStackOldWidget( true );
    else if( playlistWidget &&
             playlistWidget->artContainer->currentWidget() == videoWidget )
    {
        playlistWidget->artContainer->setCurrentIndex( 0 );
        stackCentralW->addWidget( videoWidget );
    }

    /* We don't want to have a blank video to popup */
    stackCentralOldWidget = bgWidget;
}

// The provided size is in physical pixels, coming from the core.
void MainInterface::setVideoSize( unsigned int w, unsigned int h )
{
    if (!isFullScreen() && !isMaximized() )
    {
        /* Resize video widget to video size, or keep it at the same
         * size. Call setSize() either way so that vout_window_ReportSize
         * will always get called.
         * If the video size is too large for the screen, resize it
         * to the screen size. Not in the library: the video is in the
         * fixed-size mini player and the window must not follow it.
         */
        /* not from channel to channel: each has its own size */
        if (b_autoresize && !b_libraryMode && !IptvCatalog::liveQueue())
        {
            QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
#if HAS_QT56
            float factor = videoWidget->devicePixelRatioF();
#else
            float factor = 1.0f;
#endif
            if( (float)h / factor > screen.height() )
            {
                w = screen.width();
                h = screen.height();
                if( !b_minimalView )
                {
                    if( menuBar()->isVisible() )
                        h -= menuBar()->height();
                    if( controls->isVisible() )
                        h -= controls->height();
#ifndef QT_NO_STATUSBAR
                    if( statusBar()->isVisible() )
                        h -= statusBar()->height();
#endif
                    if( inputC->isVisible() )
                        h -= inputC->height();
                }
                h -= style()->pixelMetric(QStyle::PM_TitleBarHeight);
                h -= style()->pixelMetric(QStyle::PM_LayoutBottomMargin);
                h -= 2 * style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
            }
            else
            {
                // Convert the size in logical pixels
                w = qRound( (float)w / factor );
                h = qRound( (float)h / factor );
                msg_Dbg( p_intf, "Logical video size: %ux%u", w, h );
            }
            videoWidget->setSize( w, h );
        }
        else
            videoWidget->setSize( videoWidget->width(), videoWidget->height() );
    }
}

void MainInterface::videoSizeChanged( int w, int h )
{
    if( b_libraryMode )
        return;
    if( !playlistWidget || playlistWidget->artContainer->currentWidget() != videoWidget )
        resizeStack( w, h );
}

void MainInterface::setVideoFullScreen( bool fs )
{
    /* fullscreen is the player's: leave the library first */
    if( fs && b_libraryMode )
        setLibraryMode( false );
    b_videoFullScreen = fs;
    if( fs )
    {
        int numscreen = var_InheritInteger( p_intf, "qt-fullscreen-screennumber" );

        if ( numscreen >= 0 && numscreen < QGuiApplication::screens().length() )
        {
            if( fullscreenControls )
                fullscreenControls->setTargetScreen( numscreen );

            QRect screenres = QGuiApplication::screens()[ numscreen ]->geometry();
            lastWinScreen = windowHandle()->screen();
            windowHandle()->setScreen(QGuiApplication::screens()[numscreen]);

            /* To be sure window is on proper-screen in xinerama */
            if( !screenres.contains( pos() ) )
            {
                lastWinPosition = pos();
                lastWinSize = size();
                msg_Dbg( p_intf, "Moving video to correct position");
                move( QPoint( screenres.x(), screenres.y() ) );
            }
        }

        if( playlistWidget != NULL && playlistWidget->artContainer->currentWidget() == videoWidget )
            showTab( videoWidget );

        /* we won't be able to get its windowed sized once in fullscreen, so update it now */
        stackWidgetsSizes[stackCentralW->currentWidget()] = stackCentralW->size();

        /* */
        displayNormalView();
        setInterfaceFullScreen( true );
    }
    else
    {
        setMinimalView( b_minimalView );
        setInterfaceFullScreen( b_interfaceFullScreen );
        if( lastWinScreen != NULL )
            windowHandle()->setScreen(lastWinScreen);
        if( lastWinPosition.isNull() == false )
        {
            move( lastWinPosition );
            resizeWindow( lastWinSize.width(), lastWinSize.height() );
            lastWinPosition = QPoint();
            lastWinSize = QSize();
        }

    }
    videoWidget->sync();
    if( chromeOverlay )
        chromeOverlay->update();
}

void MainInterface::setHideMouse( bool hide )
{
    videoWidget->setCursor( hide ? Qt::BlankCursor : Qt::ArrowCursor );
}

/* Slot to change the video always-on-top flag.
 * Emit askVideoOnTop() to invoke this from other thread. */
void MainInterface::setVideoOnTop( bool on_top )
{
    //don't apply changes if user has already sets its interface on top
    if ( b_interfaceOnTop )
        return;

    Qt::WindowFlags oldflags = windowFlags(), newflags;

    if( on_top )
        newflags = oldflags | Qt::WindowStaysOnTopHint;
    else
        newflags = oldflags & ~Qt::WindowStaysOnTopHint;
    if( newflags != oldflags && !b_videoFullScreen )
    {
        setWindowFlags( newflags );
        show(); /* necessary to apply window flags */
    }
}

void MainInterface::setInterfaceAlwaysOnTop( bool on_top )
{
    b_interfaceOnTop = on_top;
    Qt::WindowFlags oldflags = windowFlags(), newflags;

    if( on_top )
        newflags = oldflags | Qt::WindowStaysOnTopHint;
    else
        newflags = oldflags & ~Qt::WindowStaysOnTopHint;
    if( newflags != oldflags && !b_videoFullScreen )
    {
        setWindowFlags( newflags );
        show(); /* necessary to apply window flags */
    }
}

/* Asynchronous call from WindowControl function */
int MainInterface::controlVideo( int i_query, va_list args )
{
    switch( i_query )
    {
    case VOUT_WINDOW_SET_SIZE:
    {
        unsigned int i_width  = va_arg( args, unsigned int );
        unsigned int i_height = va_arg( args, unsigned int );

        emit askVideoToResize( i_width, i_height );
        return VLC_SUCCESS;
    }
    case VOUT_WINDOW_SET_STATE:
    {
        unsigned i_arg = va_arg( args, unsigned );
        unsigned on_top = i_arg & VOUT_WINDOW_STATE_ABOVE;

        emit askVideoOnTop( on_top != 0 );
        return VLC_SUCCESS;
    }
    case VOUT_WINDOW_SET_FULLSCREEN:
    {
        bool b_fs = va_arg( args, int );

        emit askVideoSetFullScreen( b_fs );
        return VLC_SUCCESS;
    }
    case VOUT_WINDOW_HIDE_MOUSE:
    {
        bool b_hide = va_arg( args, int );

        emit askHideMouse( b_hide );
        return VLC_SUCCESS;
    }
    default:
        msg_Warn( p_intf, "unsupported control query" );
        return VLC_EGENERIC;
    }
}

/*****************************************************************************
 * Playlist, Visualisation and Menus handling
 *****************************************************************************/
/**
 * Toggle the playlist widget or dialog
 **/
void MainInterface::createPlaylist()
{
    PlaylistDialog *dialog = PlaylistDialog::getInstance( p_intf );

    if( sidebar )
    {
        playlistWidget = dialog->exportPlaylistWidget();
        sidebar->setPlaylistWidget( playlistWidget );
        /* pop-out window closed: take the playlist back into the sidebar */
        connect( dialog, &PlaylistDialog::visibilityChanged, this, [this, dialog]( bool visible ) {
            if( visible || !dialog->hasPlaylistWidget() )
                return;
            playlistWidget = dialog->exportPlaylistWidget();
            sidebar->setPlaylistWidget( playlistWidget );
        } );
        /* the sidebar expands into the library window (Figma row 2 #3-6);
         * the classic playlist window stays in right-click > Interface */
        connect( sidebar, &SidebarPanel::popOutRequested, this, [this]() {
            const QString sec = lastSection == "music" ? "music"
                              : lastSection == "recents" ? "recent"
                              : lastSection == "home" ? "home"
                              : lastSection == "playlists" ? "playlists" : "videos";
            setLibraryMode( true, sec );
        } );
        Q_UNUSED( dialog );
        return;
    }

    if( b_plDocked )
    {
        playlistWidget = dialog->exportPlaylistWidget();
        stackCentralW->addWidget( playlistWidget );
        stackWidgetsSizes[playlistWidget] = settings->value( "playlistSize", QSize( 600, 300 ) ).toSize();
    }
    connect( dialog, &PlaylistDialog::visibilityChanged, this, &MainInterface::setPlaylistVisibility );
}

void MainInterface::setSidebarVisible( bool b_visible )
{
    if( !sidebar )
        return;
    if( b_visible && !playlistWidget )
        createPlaylist();
    if( b_visible && rail->current().isEmpty() )
    {
        activateSection( lastSection.isEmpty() ? QString( "playlists" ) : lastSection );
        return;
    }
    animateSidebar( b_visible );
    if( !b_visible )
        rail->setCurrent( QString() );
    header->sidebarButton()->blockSignals( true );
    header->sidebarButton()->setChecked( b_visible );
    header->sidebarButton()->blockSignals( false );
    playlistVisible = b_visible;
}

/* The Queue panel takes the adjustment panel's place right of the
 * content; the bar's queue button is accent while it is open */
void MainInterface::setQueuePanelVisible( bool b, bool animate )
{
    if( !body )
        return;
    if( !queueDock )
    {
        if( !b )
            return;
        queueDock = new QueueDock( p_intf, body );
        static_cast<QHBoxLayout *>( body->layout() )->addWidget( queueDock );
        if( chromeOverlay )
            chromeOverlay->repad();
        connect( queueDock->panel(), &QueuePanel::closeRequested, this,
                 [this]() { setQueuePanelVisible( false ); } );
    }
    /* switching panels: the other one goes at once and this one slides
     * into the same slot, so the content keeps its width */
    const bool switching = b && adjustDock && adjustDock->isOpen();
    if( switching )
        setAdjustPanelVisible( false, false );
    queueDock->setOpen( b, animate && !b_minimalView && !isFullScreen() );
    if( b )
        queueDock->panel()->setFocus( Qt::OtherFocusReason );
    foreach( QToolButton *btn, findChildren<QToolButton *>( "PLAYLISTButton" ) )
    {
        if( !btn->property( "vlcIcon" ).isValid() )
            btn->setProperty( "vlcIcon", btn->icon() );
        btn->setIcon( b ? QIcon( Theme::pixmap( "lists", Theme::BAR_GLYPH, Theme::accent() ) )
                        : btn->property( "vlcIcon" ).value<QIcon>() );
    }
    if( !b && queueDock->panel()->isAncestorOf( QApplication::focusWidget() ) )
        setFocus();
}

/* The adjustment panel slides into its slot right of the content */
void MainInterface::setAdjustPanelVisible( bool b, bool animate )
{
    if( !body )
        return;
    if( !adjustDock )
    {
        if( !b )
            return;
        /* built on first use: it creates every effect widget */
        adjustDock = new AdjustDock( p_intf, body );
        static_cast<QHBoxLayout *>( body->layout() )->addWidget( adjustDock );
        if( chromeOverlay )
            chromeOverlay->repad();
        connect( adjustDock->panel(), &AdjustPanel::closeRequested, this,
                 [this]() { setAdjustPanelVisible( false ); } );
    }
    const bool switching = b && queueDock && queueDock->isOpen();
    if( switching )
        setQueuePanelVisible( false, false );
    adjustDock->setOpen( b, animate && !b_minimalView && !isFullScreen() );
    if( b )
        adjustDock->panel()->setFocus( Qt::OtherFocusReason );

    /* the bar's settings button shows the panel state */
    foreach( QToolButton *btn, findChildren<QToolButton *>( "EXTENDEDButton" ) )
    {
        if( !btn->property( "vlcIcon" ).isValid() )
            btn->setProperty( "vlcIcon", btn->icon() );
        btn->setIcon( b ? QIcon( Theme::pixmap( "playback-settings", Theme::BAR_GLYPH, Theme::accent() ) )
                        : btn->property( "vlcIcon" ).value<QIcon>() );
    }
    if( !b && adjustDock->panel()->isAncestorOf( QApplication::focusWidget() ) )
        setFocus();
}

void MainInterface::showAdjustPanel( int tab )
{
    if( !body )
        return;
    const bool wasOpen = adjustDock && adjustDock->isOpen();
    setAdjustPanelVisible( true );
    if( adjustDock )
        adjustDock->panel()->setCurrentTab( tab, wasOpen );
}

void MainInterface::toggleAdjustPanel( int tab )
{
    if( !body )
        return;
    const bool open = adjustDock && adjustDock->isOpen();
    if( open && ( tab < 0 || adjustDock->panel()->currentTab() == tab ) )
    {
        setAdjustPanelVisible( false );
        return;
    }
    setAdjustPanelVisible( true );
    if( tab >= 0 )
        adjustDock->panel()->setCurrentTab( tab, open );
}

/* The sidebar takes its width in one step and a still of its content
 * slides in over it; closing slides the still out, then gives the width
 * back in one step. Nothing re-lays out and the video resizes once. */
void MainInterface::animateSidebar( bool b_visible )
{
    FrameAnimation *anim = sidebar->findChild<FrameAnimation *>( "vlcSlide" );
    SnapshotOverlay *still = sidebar->findChild<SnapshotOverlay *>( "vlcSlideStill" );
    const bool animating = anim && anim->state() == QAbstractAnimation::Running;
    const bool closing = sidebar->property( "vlcClosing" ).toBool();
    const bool open = sidebar->isVisible() && !closing;
    if( open == b_visible )
        return;

    int target = sidebar->property( "vlcWidth" ).toInt();
    if( target <= 0 )
        target = Theme::SIDEBAR_WIDTH;
    if( sidebar->isVisible() && !animating )
        target = sidebar->width(); /* remember the user's width */
    sidebar->setProperty( "vlcWidth", target );

    /* where a reversed slide picks up from */
    const qreal from = animating && still ? still->getOffset().x()
                                          : ( b_visible ? -qreal( target ) : 0. );
    if( anim )
        anim->stop();
    delete still;
    sidebar->setProperty( "vlcClosing", false );

    /* No animation before the window is on screen or in minimal view */
    if( !isVisible() || b_minimalView || isFullScreen() )
    {
        sidebar->setVisible( b_visible );
        if( b_visible )
            bodySplitter->setSizes( QList<int>() << target << qMax( 1, bodySplitter->width() - target ) );
        return;
    }

    if( !anim )
    {
        anim = new FrameAnimation( sidebar );
        anim->setObjectName( "vlcSlide" );
        anim->setDuration( 220 );
        connect( anim, &FrameAnimation::valueChanged, this, [this]( const QVariant &v ) {
            if( SnapshotOverlay *o = sidebar->findChild<SnapshotOverlay *>( "vlcSlideStill" ) )
                o->setOffset( QPointF( v.toReal(), 0 ) );
        } );
        connect( anim, &FrameAnimation::finished, this, [this]() {
            delete sidebar->findChild<SnapshotOverlay *>( "vlcSlideStill" );
            if( sidebar->property( "vlcClosing" ).toBool() )
            {
                sidebar->setProperty( "vlcClosing", false );
                sidebar->hide();    /* the content takes the width back */
            }
        } );
    }

    if( b_visible )
    {
        sidebar->show();
        bodySplitter->setSizes( QList<int>() << target << qMax( 1, bodySplitter->width() - target ) );
        anim->setEasingCurve( QEasingCurve::OutCubic );
        anim->setEndValue( 0. );
    }
    else
    {
        sidebar->setProperty( "vlcClosing", true );
        anim->setEasingCurve( QEasingCurve::InOutCubic );
        anim->setEndValue( -qreal( sidebar->width() ) );
    }
    still = new SnapshotOverlay( sidebar, SnapshotOverlay::grabOf( sidebar ),
                                 Theme::colors().sidebar );
    still->setObjectName( "vlcSlideStill" );
    still->setOffset( QPointF( from, 0 ) );
    anim->setStartValue( from );
    anim->start();
}

void MainInterface::activateSection( const QString &key )
{
    if( !sidebar )
        return;
    if( !playlistWidget )
        createPlaylist();
    lastSection = key;
    rail->setCurrent( key );
    sidebar->showSection( key );
    setSidebarVisible( true );
}

void MainInterface::togglePlaylist()
{
    if( sidebar )
    {
        /* the bar's queue button: the Queue panel (Components "Queue") */
        setQueuePanelVisible( !( queueDock && queueDock->isOpen() ) );
        return;
    }

    if( !playlistWidget ) createPlaylist();

    PlaylistDialog *dialog = PlaylistDialog::getInstance( p_intf );
    if( b_plDocked )
    {
        if ( dialog->hasPlaylistWidget() )
            playlistWidget = dialog->exportPlaylistWidget();
        /* Playlist is not visible, show it */
        if( stackCentralW->currentWidget() != playlistWidget )
        {
            if( stackCentralW->indexOf( playlistWidget ) == -1 )
                stackCentralW->addWidget( playlistWidget );
            showTab( playlistWidget );
        }
        else /* Hide it! */
        {
            restoreStackOldWidget();
        }
        playlistVisible = ( stackCentralW->currentWidget() == playlistWidget );
    }
    else
    {
        playlistVisible = !playlistVisible;
        if ( ! dialog->hasPlaylistWidget() )
            dialog->importPlaylistWidget( playlistWidget );
        if ( playlistVisible )
            dialog->show();
        else
            dialog->hide();
    }
    debug();
}

const Qt::Key MainInterface::kc[10] =
{
    Qt::Key_Up, Qt::Key_Up,
    Qt::Key_Down, Qt::Key_Down,
    Qt::Key_Left, Qt::Key_Right, Qt::Key_Left, Qt::Key_Right,
    Qt::Key_B, Qt::Key_A
};

void MainInterface::dockPlaylist( bool p_docked )
{
    if( sidebar ) return; /* always docked in the sidebar */
    if( b_plDocked == p_docked ) return;
    /* some extra check */
    if ( b_plDocked && !playlistWidget ) createPlaylist();

    b_plDocked = p_docked;
    PlaylistDialog *dialog = PlaylistDialog::getInstance( p_intf );

    if( !p_docked ) /* Previously docked */
    {
        playlistVisible = playlistWidget->isVisible();

        /* repositioning the videowidget __before__ exporting the
           playlistwidget into the playlist dialog avoids two unneeded
           calls to the server in the qt library to reparent the underlying
           native window back and forth.
           For Wayland, this is mandatory since reparenting is not implemented.
           For X11 or Windows, this is just an optimization. */
        if ( videoWidget && THEMIM->getIM()->hasVideo() )
            showTab(videoWidget);
        else
            showTab(bgWidget);

        /* playlistwidget exported into the playlist dialog */
        stackCentralW->removeWidget( playlistWidget );
        dialog->importPlaylistWidget( playlistWidget );
        if ( playlistVisible ) dialog->show();
    }
    else /* Previously undocked */
    {
        playlistVisible = dialog->isVisible() && !( videoWidget && THEMIM->getIM()->hasVideo() );
        dialog->hide();
        playlistWidget = dialog->exportPlaylistWidget();
        stackCentralW->addWidget( playlistWidget );

        /* If playlist is invisible don't show it */
        if( playlistVisible ) showTab( playlistWidget );
    }
}

/*
 * displayNormalView is the private function used by
 * the SLOT setVideoFullScreen to restore the menuBar
 * if minimal view is off
 */
void MainInterface::displayNormalView()
{
    if( autoHider )
        autoHider->reset();
    if( chromeOverlay )
        chromeOverlay->update();
    if( header )
    {
        header->setVisible( false );
        rail->setVisible( false );
        sidebar->setVisible( false );
        if( adjustDock )
            adjustDock->setVisible( false );
    }
    menuBar()->setVisible( false );
    controls->setVisible( false );
#ifndef QT_NO_STATUSBAR
    statusBar()->setVisible( false );
#endif
    inputC->setVisible( false );
}

/*
 * setMinimalView is the private function used by
 * the SLOT toggleMinimalView
 */
void MainInterface::setMinimalView( bool b_minimal )
{
    if( autoHider )
        autoHider->reset();
    if( chromeOverlay )
        chromeOverlay->update();
    bool b_menuBarVisible = menuBar()->isVisible();
    bool b_controlsVisible = controls->isVisible();
#ifndef QT_NO_STATUSBAR
    bool b_statusBarVisible = statusBar()->isVisible();
#endif
    bool b_inputCVisible = inputC->isVisible();

    if( !isFullScreen() && !isMaximized() && b_minimal && !b_isWindowTiled )
    {
        int i_heightChange = 0;

        if( b_menuBarVisible )
            i_heightChange += menuBar()->height();
        if( b_controlsVisible )
            i_heightChange += controls->height();
#ifndef QT_NO_STATUSBAR
        if( b_statusBarVisible )
            i_heightChange += statusBar()->height();
#endif
        if( b_inputCVisible )
            i_heightChange += inputC->height();

        if( i_heightChange != 0 )
            resizeWindow( width(), height() - i_heightChange );
    }

    menuBar()->setVisible( !b_minimal );
    controls->setVisible( !b_minimal && !b_libraryMode );
    if( header )
    {
        header->setVisible( !b_minimal );
        rail->setVisible( !b_minimal );
        sidebar->setVisible( !b_minimal && playlistVisible );
        if( adjustDock )
            adjustDock->setVisible( !b_minimal && adjustDock->isOpen() );
    }
#ifndef QT_NO_STATUSBAR
    statusBar()->setVisible( !b_minimal && b_statusbarVisible );
#endif
    inputC->setVisible( !b_minimal );

    if( !isFullScreen() && !isMaximized() && !b_minimal && !b_isWindowTiled )
    {
        int i_heightChange = 0;

        if( !b_menuBarVisible && menuBar()->isVisible() )
            i_heightChange += menuBar()->height();
        if( !b_controlsVisible && controls->isVisible() )
            i_heightChange += controls->height();
#ifndef QT_NO_STATUSBAR
        if( !b_statusBarVisible && statusBar()->isVisible() )
            i_heightChange += statusBar()->height();
#endif
        if( !b_inputCVisible && inputC->isVisible() )
            i_heightChange += inputC->height();

        if( i_heightChange != 0 )
            resizeWindow( width(), height() + i_heightChange );
    }
}

/*
 * This public SLOT is used for moving to minimal View Mode
 *
 * If b_minimal is false, then we are normalView
 */
void MainInterface::setLibraryMode( bool on, const QString &section )
{
    if( !libraryView || b_videoFullScreen )
        return;
    if( on && !section.isEmpty() )
        libraryView->openSection( section );
    if( on == b_libraryMode )
        return;
    b_libraryMode = on;
    if( autoHider )
        autoHider->reset();
    if( chromeOverlay )
        chromeOverlay->update();
    header->setLibraryMode( on );
    if( on )
    {
        body->hide();
        controls->hide();
        libraryView->show();
        libraryView->setFocus();
    }
    else
    {
        libraryView->hide();
        body->show();
        controls->setVisible( !b_minimalView );
    }
    placeVideo();
}

void MainInterface::placeVideo()
{
    if( !videoWidget || !libraryView )
        return;
    MiniPlayer *mini = libraryView->miniPlayer();
    const bool wantMini = b_libraryMode && THEMIM->getIM()->hasVideo();
    if( wantMini && mini->video() != videoWidget )
    {
        if( stackCentralW->indexOf( videoWidget ) >= 0 )
        {
            stackCentralW->removeWidget( videoWidget );
            stackCentralW->setCurrentWidget( bgWidget );
        }
        mini->setVideo( videoWidget );
    }
    else if( !wantMini && mini->video() == videoWidget )
    {
        mini->takeVideo();
        stackCentralW->addWidget( videoWidget );
        videoWidget->show();
        if( THEMIM->getIM()->hasVideo() )
            stackCentralW->setCurrentWidget( videoWidget );
    }
}

void MainInterface::toggleMinimalView( bool b_minimal )
{
    if( !b_minimalView && b_autoresize ) /* Normal mode */
    {
        if( stackCentralW->currentWidget() == bgWidget )
        {
            if( stackCentralW->height() < 16 )
            {
                resizeStack( stackCentralW->width(), 100 );
            }
        }
    }
    b_minimalView = b_minimal;
    if( !b_videoFullScreen )
    {
        setMinimalView( b_minimalView );
        computeMinimumSize();
    }

    emit minimalViewToggled( b_minimalView );
}

/* toggling advanced controls buttons */
void MainInterface::toggleAdvancedButtons()
{
    controls->toggleAdvanced();
//    if( fullscreenControls ) fullscreenControls->toggleAdvanced();
}

/* Get the visibility status of the controls (hidden or not, advanced or not) */
int MainInterface::getControlsVisibilityStatus()
{
    if( !controls ) return 0;
    return( (controls->isVisible() ? CONTROLS_VISIBLE : CONTROLS_HIDDEN )
            + CONTROLS_ADVANCED * controls->b_advancedVisible );
}

/* Get whether the advanced buttons widget is available
 * (if its actually present in any of the configurable locations).
 */
bool MainInterface::isAdvancedWidgetAvailable()
{
    if( !controls) return false;
    return controls->advancedAvailable();
}

StandardPLPanel *MainInterface::getPlaylistView()
{
    if( !playlistWidget ) return NULL;
    else return playlistWidget->mainView;
}

void MainInterface::setStatusBarVisibility( bool b_visible )
{
#ifndef QT_NO_STATUSBAR
    statusBar()->setVisible( b_visible );
    b_statusbarVisible = b_visible;
    if( controls ) controls->setGripVisible( !b_statusbarVisible );
#endif
}


void MainInterface::setPlaylistVisibility( bool b_visible )
{
    if( isPlDocked() || THEDP->isDying() || (playlistWidget && playlistWidget->isMinimized() ) )
        return;

    playlistVisible = b_visible;
}

/************************************************************************
 * Other stuff
 ************************************************************************/
void MainInterface::setName( const QString& name )
{
    input_name = name; /* store it for the QSystray use */
#ifndef QT_NO_STATUSBAR
    /* Display it in the status bar, but also as a Tooltip in case it doesn't
       fit in the label */
    nameLabel->setText( name );
    nameLabel->setToolTip( name );
#endif // QT_NO_STATUSBAR
}

/**
 * Give the decorations of the Main Window a correct Name.
 * If nothing is given, set it to VLC...
 **/
void MainInterface::setVLCWindowsTitle( const QString& aTitle )
{
    if( aTitle.isEmpty() )
    {
        setWindowTitle( qtr( "VLC media player" ) );
    }
    else
    {
        setWindowTitle( aTitle + " - " + qtr( "VLC media player" ) );
    }
}

void MainInterface::showCryptedLabel( bool b_show )
{
#ifndef QT_NO_STATUSBAR
    if( cryptedLabel == NULL )
    {
        cryptedLabel = new QLabel;
        // The lock icon is not the right one for DRM protection/scrambled.
        //cryptedLabel->setPixmap( QPixmap( ":/lock.svg" ) );
        cryptedLabel->setText( "DRM" );
#ifndef QT_NO_STATUSBAR
        statusBar()->addWidget( cryptedLabel );
#endif
    }

    cryptedLabel->setVisible( b_show );
#endif
}

/*****************************************************************************
 * Systray Icon and Systray Menu
 *****************************************************************************/
/**
 * Create a SystemTray icon and a menu that would go with it.
 * Connects to a click handler on the icon.
 **/
void MainInterface::createSystray()
{
    QIcon iconVLC;
    if( QDate::currentDate().dayOfYear() >= QT_XMAS_JOKE_DAY && var_InheritBool( p_intf, "qt-icon-change" ) )
        iconVLC = QIcon::fromTheme( "vlc-xmas", QIcon( ":/logo/vlc128-xmas.png" ) );
    else
        iconVLC = QIcon::fromTheme( "vlc", QIcon( ":/logo/vlc256.png" ) );
    sysTray = new QSystemTrayIcon( iconVLC, this );
    sysTray->setToolTip( qtr( "VLC media player" ));

    systrayMenu = new QMenu( qtr( "VLC media player" ), this );
    systrayMenu->setIcon( iconVLC );

    VLCMenuBar::updateSystrayMenu( this, p_intf, true );
    sysTray->show();

    connect( sysTray, &QSystemTrayIcon::activated,
             this, &MainInterface::handleSystrayClick );

    /* Connects on nameChanged() */
    connect( THEMIM->getIM(), &InputManager::nameChanged,
             this, &MainInterface::updateSystrayTooltipName );
    /* Connect PLAY_STATUS on the systray */
    connect( THEMIM->getIM(), &InputManager::playingStatusChanged,
             this, &MainInterface::updateSystrayTooltipStatus );
}

void MainInterface::toggleUpdateSystrayMenuWhenVisible(bool)
{
    hide();
}

void MainInterface::resizeWindow(int w, int h)
{
#if ! HAS_QT510 && defined(QT_HAS_X11)
    if( QX11Info::isPlatformX11() )
    {
#if HAS_QT56
        qreal dpr = devicePixelRatioF();
#else
        qreal dpr = devicePixelRatio();
#endif
        QSize size(w, h);
        size = size.boundedTo(maximumSize()).expandedTo(minimumSize());
        /* X11 window managers are not required to accept geometry changes on
         * the top-level window.  Unfortunately, Qt < 5.10 assumes that the
         * change will succeed, and resizes all sub-windows unconditionally.
         * By calling XMoveResizeWindow directly, Qt will not see our change
         * request until the ConfigureNotify event on success
         * and not at all if it is rejected. */
        XResizeWindow( QX11Info::display(), winId(),
                       (unsigned int)size.width() * dpr, (unsigned int)size.height() * dpr);
        return;
    }
#endif
    resize(w, h);
}

/**
 * Updates the Systray Icon's menu and toggle the main interface
 */
void MainInterface::toggleUpdateSystrayMenu(bool requestActivate)
{
    /* If hidden, show it */
    if( requestActivate && isHidden() )
    {
        show();
        activateWindow();
    }
    else if( requestActivate && isMinimized() )
    {
        /* Minimized */
        showNormal();
        activateWindow();
    }
    else if ( !isHidden() && !isMinimized() )
    {
        /* Visible (possibly under other windows) */
        toggleUpdateSystrayMenuWhenVisible(requestActivate);
    }
    if( sysTray )
        VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/* First Item of the systray menu */
void MainInterface::showUpdateSystrayMenu()
{
    if( isHidden() )
        show();
    if( isMinimized() )
        showNormal();
    activateWindow();

    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/* First Item of the systray menu */
void MainInterface::hideUpdateSystrayMenu()
{
    hide();
    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/* Click on systray Icon */
void MainInterface::handleSystrayClick(
                                    QSystemTrayIcon::ActivationReason reason )
{
    switch( reason )
    {
        case QSystemTrayIcon::Trigger:
        case QSystemTrayIcon::DoubleClick:
#ifdef Q_OS_MAC
            VLCMenuBar::updateSystrayMenu( this, p_intf );
#else
            toggleUpdateSystrayMenu(true);
#endif
            break;
        case QSystemTrayIcon::MiddleClick:
            sysTray->showMessage( qtr( "VLC media player" ),
                    qtr( "Control menu for the player" ),
                    QSystemTrayIcon::Information, 3000 );
            break;
        default:
            break;
    }
}

/**
 * Updates the name of the systray Icon tooltip.
 * Doesn't check if the systray exists, check before you call it.
 **/
void MainInterface::updateSystrayTooltipName( const QString& name )
{
    if( name.isEmpty() )
    {
        sysTray->setToolTip( qtr( "VLC media player" ) );
    }
    else
    {
        sysTray->setToolTip( name );
        if( ( i_notificationSetting == NOTIFICATION_ALWAYS ) ||
            ( i_notificationSetting == NOTIFICATION_MINIMIZED && (isMinimized() || isHidden()) ) )
        {
            sysTray->showMessage( qtr( "VLC media player" ), name,
                    QSystemTrayIcon::NoIcon, 3000 );
        }
    }

    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

/**
 * Updates the status of the systray Icon tooltip.
 * Doesn't check if the systray exists, check before you call it.
 **/
void MainInterface::updateSystrayTooltipStatus( int i_status )
{
    switch( i_status )
    {
    case PLAYING_S:
        sysTray->setToolTip( input_name );
        break;
    case PAUSE_S:
        sysTray->setToolTip( input_name + " - " + qtr( "Paused") );
        break;
    default:
        sysTray->setToolTip( qtr( "VLC media player" ) );
        break;
    }
    VLCMenuBar::updateSystrayMenu( this, p_intf );
}

void MainInterface::changeEvent(QEvent *event)
{
    if( event->type() == QEvent::WindowStateChange )
    {
        QWindowStateChangeEvent *windowStateChangeEvent = static_cast<QWindowStateChangeEvent*>(event);
        Qt::WindowStates newState = windowState();
        Qt::WindowStates oldState = windowStateChangeEvent->oldState();

        /* b_maximizedView stores if the window was maximized before entering fullscreen.
         * It is set when entering maximized mode, unset when leaving it to normal mode.
         * Upon leaving full screen, if b_maximizedView is set,
         * the window should be maximized again. */
        if( newState & Qt::WindowMaximized &&
            !( oldState & Qt::WindowMaximized ) )
            b_maximizedView = true;

        if( !( newState & Qt::WindowMaximized ) &&
            oldState & Qt::WindowMaximized &&
            !b_videoFullScreen )
            b_maximizedView = false;

        if( !( newState & Qt::WindowFullScreen ) &&
            oldState & Qt::WindowFullScreen &&
            b_maximizedView )
        {
            showMaximized();
            return;
        }

        if( newState & Qt::WindowMinimized )
        {
            b_hasPausedWhenMinimized = false;

            if( THEMIM->getIM()->playingStatus() == PLAYING_S &&
                THEMIM->getIM()->hasVideo() && !THEMIM->getIM()->hasVisualisation() &&
                b_pauseOnMinimize )
            {
                b_hasPausedWhenMinimized = true;
                THEMIM->pause();
            }
        }
        else if( oldState & Qt::WindowMinimized && !( newState & Qt::WindowMinimized ) )
        {
            if( b_hasPausedWhenMinimized )
            {
                THEMIM->play();
            }
        }
    }

    QWidget::changeEvent(event);
}

/************************************************************************
 * D&D Events
 ************************************************************************/
void MainInterface::dropEvent(QDropEvent *event)
{
    dropEventPlay( event, true );
}

/**
 * dropEventPlay
 *
 * Event called if something is dropped onto a VLC window
 * \param event the event in question
 * \param b_play whether to play the file immediately
 * \param b_playlist true to add to playlist, false to add to media library
 * \return nothing
 */
void MainInterface::dropEventPlay( QDropEvent *event, bool b_play, bool b_playlist )
{
    if( event->possibleActions() & ( Qt::CopyAction | Qt::MoveAction | Qt::LinkAction ) )
       event->setDropAction( Qt::CopyAction );
    else
        return;

    const QMimeData *mimeData = event->mimeData();

    /* D&D of a subtitles file, add it on the fly */
    if( mimeData->urls().count() == 1 && THEMIM->getIM()->hasInput() )
    {
        if( !input_AddSlave( THEMIM->getInput(), SLAVE_TYPE_SPU,
                 qtu( mimeData->urls()[0].toString() ), true, true, true ) )
        {
            event->accept();
            return;
        }
    }

    bool first = b_play;
    foreach( const QUrl &url, mimeData->urls() )
    {
        if( url.isValid() )
        {
            QString mrl = toURI( url.toEncoded().constData() );
#ifdef _WIN32
            QFileInfo info( url.toLocalFile() );
            if( info.exists() && info.isSymLink() )
            {
                QString target = info.symLinkTarget();
                QUrl url;
                if( QFile::exists( target ) )
                {
                    url = QUrl::fromLocalFile( target );
                }
                else
                {
                    url.setUrl( target );
                }
                mrl = toURI( url.toEncoded().constData() );
            }
#endif
            if( mrl.length() > 0 )
            {
                Open::openMRL( p_intf, mrl, first, b_playlist );
                first = false;
            }
        }
    }

    /* Browsers give content as text if you dnd the addressbar,
       so check if mimedata has valid url in text and use it
       if we didn't get any normal Urls()*/
    if( !mimeData->hasUrls() && mimeData->hasText() &&
        QUrl(mimeData->text()).isValid() )
    {
        QString mrl = toURI( mimeData->text() );
        Open::openMRL( p_intf, mrl, first, b_playlist );
    }
    event->accept();
}
void MainInterface::dragEnterEvent(QDragEnterEvent *event)
{
     event->acceptProposedAction();
}
void MainInterface::dragMoveEvent(QDragMoveEvent *event)
{
     event->acceptProposedAction();
}
void MainInterface::dragLeaveEvent(QDragLeaveEvent *event)
{
     event->accept();
}

/************************************************************************
 * Events stuff
 ************************************************************************/
void MainInterface::keyPressEvent( QKeyEvent *e )
{
    handleKeyPress( e );

    /* easter eggs sequence handling */
    if ( e->key() == kc[ i_kc_offset ] )
        i_kc_offset++;
    else
        i_kc_offset = 0;

    if ( i_kc_offset == (sizeof( kc ) / sizeof( Qt::Key )) )
    {
        i_kc_offset = 0;
        emit kc_pressed();
    }
}

void MainInterface::handleKeyPress( QKeyEvent *e )
{
    if( ( ( e->modifiers() & Qt::ControlModifier ) && ( e->key() == Qt::Key_H ) ) ||
        ( b_minimalView && !b_videoFullScreen && e->key() == Qt::Key_Escape ) )
    {
        toggleMinimalView( !b_minimalView );
        e->accept();
    }
    else if( ( e->modifiers() & Qt::ControlModifier ) && ( e->key() == Qt::Key_K ) &&
        playlistWidget )
    {
        playlistWidget->setSearchFieldFocus();
        e->accept();
    }

    int i_vlck = qtEventToVLCKey( e );
    if( i_vlck > 0 )
    {
        KeyMap::send( p_intf, i_vlck );
        e->accept();
    }
    else
        e->ignore();
}

void MainInterface::wheelEvent( QWheelEvent *e )
{
    int i_vlckey = qtWheelEventToVLCKey( e );
    KeyMap::send( p_intf, i_vlckey );
    e->accept();
}

void MainInterface::closeEvent( QCloseEvent *e )
{
//  hide();
    if ( b_minimalView )
        setMinimalView( false );
    if( videoWidget )
        releaseVideoSlot( true );
    emit askToQuit(); /* ask THEDP to quit, so we have a unique method */
    /* Accept session quit. Otherwise we break the desktop mamager. */
    e->accept();
}

bool MainInterface::eventFilter( QObject *obj, QEvent *event )
{
    if ( event->type() == MainInterface::ToolbarsNeedRebuild ) {
        event->accept();
        recreateToolbars();
        return true;
    } else {
        return QObject::eventFilter( obj, event );
    }
}

void MainInterface::toolBarConfUpdated()
{
    QApplication::postEvent( this, new QEvent( MainInterface::ToolbarsNeedRebuild ) );
}

void MainInterface::setInterfaceFullScreen( bool fs )
{
    if( fs )
        setWindowState( windowState() | Qt::WindowFullScreen );
    else
        setWindowState( windowState() & ~Qt::WindowFullScreen );
}
void MainInterface::toggleInterfaceFullScreen()
{
    b_interfaceFullScreen = !b_interfaceFullScreen;
    if( !b_videoFullScreen )
        setInterfaceFullScreen( b_interfaceFullScreen );
    emit fullscreenInterfaceToggled( b_interfaceFullScreen );
}

void MainInterface::emitBoss()
{
    emit askBoss();
}
void MainInterface::setBoss()
{
    THEMIM->pause();
    if( sysTray )
    {
        hide();
    }
    else
    {
        showMinimized();
    }
}

void MainInterface::emitRaise()
{
    emit askRaise();
}
void MainInterface::setRaise()
{
    /* If hidden, show it */
    if( isHidden() )
    {
        show();
    }
    else if( isMinimized() )
    {
        /* Minimized */
        showNormal();
    }

    activateWindow();
    raise();
}

void MainInterface::voutReleaseMouseEvents()
{
    if (videoWidget)
    {
        QPoint pos = QCursor::pos();
        QPoint localpos = videoWidget->mapFromGlobal(pos);
        int buttons = QApplication::mouseButtons();
        int i_button = 1;
        while (buttons != 0)
        {
            if ( (buttons & 1) != 0 )
            {
                QMouseEvent new_e( QEvent::MouseButtonRelease, localpos,
                                   (Qt::MouseButton)i_button, (Qt::MouseButton)i_button, Qt::NoModifier );
                QApplication::sendEvent(videoWidget, &new_e);
            }
            buttons >>= 1;
            i_button <<= 1;
        }

    }
}

/*****************************************************************************
 * PopupMenuCB: callback triggered by the intf-popupmenu playlist variable.
 *  We don't show the menu directly here because we don't want the
 *  caller to block for a too long time.
 *****************************************************************************/
static int PopupMenuCB( vlc_object_t *, const char *,
                        vlc_value_t, vlc_value_t new_val, void *param )
{
    intf_thread_t *p_intf = (intf_thread_t *)param;

    if( p_intf->pf_show_dialog )
    {
        p_intf->pf_show_dialog( p_intf, INTF_DIALOG_POPUPMENU,
                                new_val.b_bool, NULL );
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * IntfShowCB: callback triggered by the intf-toggle-fscontrol libvlc variable.
 *****************************************************************************/
static int IntfShowCB( vlc_object_t *, const char *,
                       vlc_value_t, vlc_value_t, void *param )
{
    intf_thread_t *p_intf = (intf_thread_t *)param;
    p_intf->p_sys->p_mi->toggleFSC();

    /* Show event */
     return VLC_SUCCESS;
}

/*****************************************************************************
 * IntfRaiseMainCB: callback triggered by the intf-show-main libvlc variable.
 *****************************************************************************/
static int IntfRaiseMainCB( vlc_object_t *, const char *,
                            vlc_value_t, vlc_value_t, void *param )
{
    intf_thread_t *p_intf = (intf_thread_t *)param;
    p_intf->p_sys->p_mi->emitRaise();

    return VLC_SUCCESS;
}

/*****************************************************************************
 * IntfBossCB: callback triggered by the intf-boss libvlc variable.
 *****************************************************************************/
static int IntfBossCB( vlc_object_t *, const char *,
                       vlc_value_t, vlc_value_t, void *param )
{
    intf_thread_t *p_intf = (intf_thread_t *)param;
    p_intf->p_sys->p_mi->emitBoss();

    return VLC_SUCCESS;
}
