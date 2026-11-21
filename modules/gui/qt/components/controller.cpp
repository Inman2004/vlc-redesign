/*****************************************************************************
 * controller.cpp : Controller for the main interface
 ****************************************************************************
 * Copyright (C) 2006-2009 the VideoLAN team
 * $Id$
 *
 * Authors: Jean-Baptiste Kempf <jb@videolan.org>
 *          Ilkka Ollakka <ileoo@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
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

#include <vlc_vout.h>                       /* vout_thread_t for FSC */

/* Widgets */
#include "components/controller.hpp"
#include "util/qt_compat.hpp"
#include "components/controller_widget.hpp"
#include "components/interface_widgets.hpp"
#include "util/buttons/DeckButtonsLayout.hpp"
#include "util/buttons/BrowseButton.hpp"
#include "util/buttons/RoundButton.hpp"

#include "dialogs_provider.hpp"                     /* Opening Dialogs */
#include "actions_manager.hpp"                             /* *_ACTION */

#include "util/input_slider.hpp"                         /* SeekSlider */
#include "util/customwidgets.hpp"                       /* qEventToKey */

#include "adapters/seekpoints.hpp"
#include "styles/vlc_theme.hpp"                      /* Redesign */
#include "menus.hpp"                         /* Track popups */
#include "components/extended_panels.hpp"   /* VideoFilters (rotate) */
#include "components/redesign_menus.hpp"    /* Track popover */

#include <QToolButton>
#include <QHBoxLayout>
#include <QRegion>
#include <QSignalMapper>
#include <QTimer>
#include <QApplication>
#include <QWindow>
#include <QScreen>
#include <QMenu>
#include <QLabel>
#include "util/frame_animation.hpp"
#include "main_interface.hpp"
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
#include <QDesktopWidget>
#endif

//#define DEBUG_LAYOUT 1

/**********************************************************************
 * TEH controls
 **********************************************************************/

/******
 * This is an abstract Toolbar/Controller
 * This has helper to create any toolbar, any buttons and to manage the actions
 *
 *****/
AbstractController::AbstractController( intf_thread_t * _p_i, QWidget *_parent )
                   : QFrame( _parent )
{
    p_intf = _p_i;
    advControls = NULL;
    buttonGroupLayout = NULL;

    /* Main action provider */
    toolbarActionsMapper = new QSignalMapper( this );
    connect( toolbarActionsMapper, QSIGNALMAPPER_MAPPEDINT_SIGNAL,
             ActionsManager::getInstance( p_intf  ), &ActionsManager::doAction );
    connect( THEMIM->getIM(), &InputManager::playingStatusChanged, this, &AbstractController::setStatus);

    setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Fixed );
}

/* Reemit some signals on status Change to activate some buttons */
void AbstractController::setStatus( int status )
{
    bool b_hasInput = THEMIM->getIM()->hasInput();
    /* Activate the interface buttons according to the presence of the input */
    emit inputExists( b_hasInput );

    emit inputPlaying( status == PLAYING_S );

    emit inputIsRecordable( b_hasInput &&
                            var_GetBool( THEMIM->getInput(), "can-record" ) );

    emit inputIsTrickPlayable( b_hasInput &&
                            var_GetBool( THEMIM->getInput(), "can-rewind" ) );
}

/* Toolbar settings: the redesign keeps its own keys and defaults */
const char *toolbarKey( int which )
{
    const bool r = Theme::redesign();
    switch( which )
    {
    case TOOLBAR_MAIN1: return r ? "MainWindow/RedesignToolbar1" : "MainWindow/MainToolbar1";
    case TOOLBAR_MAIN2: return r ? "MainWindow/RedesignToolbar2" : "MainWindow/MainToolbar2";
    case TOOLBAR_INPUT: return r ? "MainWindow/RedesignInputToolbar" : "MainWindow/InputToolbar";
    default:            return r ? "MainWindow/RedesignFSCtoolbar" : "MainWindow/FSCtoolbar";
    }
}

QString toolbarDefault( int which )
{
    const bool r = Theme::redesign();
    switch( which )
    {
    case TOOLBAR_MAIN1: return r ? REDESIGN_TB1_DEFAULT : MAIN_TB1_DEFAULT;
    case TOOLBAR_MAIN2: return r ? REDESIGN_TB2_DEFAULT : MAIN_TB2_DEFAULT;
    case TOOLBAR_INPUT: return r ? REDESIGN_INPT_TB_DEFAULT : INPT_TB_DEFAULT;
    default:            return r ? REDESIGN_FSC_TB_DEFAULT : FSC_TB_DEFAULT;
    }
}

/* Saved toolbar line; a line that still equals an earlier redesign default
 * is upgraded, so new default buttons appear without touching layouts the
 * user customised */
static QString toolbarLine( QSettings *settings, int which )
{
    QString line = settings->value( toolbarKey( which ), toolbarDefault( which ) ).toString();
    if( Theme::redesign() )
    {
        if( which == TOOLBAR_MAIN2 && line == REDESIGN_TB2_DEFAULT_V1 )
            line = REDESIGN_TB2_DEFAULT;
        else if( which == TOOLBAR_FSC && line == REDESIGN_FSC_TB_DEFAULT_V1 )
            line = REDESIGN_FSC_TB_DEFAULT;
    }
    return line;
}

/* Redesign glyph for a toolbar button, empty if it keeps its legacy icon */
static QString redesignIconName( int button )
{
    switch( button )
    {
    case PLAY_BUTTON:           return "play";
    case STOP_BUTTON:           return "stop";
    case OPEN_BUTTON:           return "open";
    case PREV_SLOW_BUTTON:
    case PREVIOUS_BUTTON:       return "prev";
    case NEXT_FAST_BUTTON:
    case NEXT_BUTTON:           return "next";
    case FULLSCREEN_BUTTON:
    case DEFULLSCREEN_BUTTON:   return "fullscreen";
    case EXTENDED_BUTTON:       return "playback-settings";
    case PLAYLIST_BUTTON:       return "lists";
    case OPEN_SUB_BUTTON:
    case SUBTITLE_TRACKS_BUTTON:return "subtitles";
    case AUDIO_TRACKS_BUTTON:   return "audio";
    case LOOP_BUTTON:           return "repeat";
    case ROTATE_BUTTON:         return "rotate";
    case SKIP_BACK_BUTTON:      return "back-15";
    case SKIP_FW_BUTTON:        return "forward-15";
    default:                    return QString();
    }
}

/* Tooltip with the hotkey, for the redesigned bar */
static QString buttonTooltip( int button )
{
    const QString base = qtr( tooltipL[button] );
    if( !Theme::redesign() )
        return base;
    switch( button )
    {
    case PLAY_BUTTON:        return Theme::tooltipWithKey( NULL, qtr( "Play / Pause" ), "key-play-pause" );
    case STOP_BUTTON:        return Theme::tooltipWithKey( NULL, qtr( "Stop" ), "key-stop" );
    case PREVIOUS_BUTTON:
    case PREV_SLOW_BUTTON:   return Theme::tooltipWithKey( NULL, qtr( "Previous" ), "key-prev" );
    case NEXT_BUTTON:
    case NEXT_FAST_BUTTON:   return Theme::tooltipWithKey( NULL, qtr( "Next" ), "key-next" );
    case FULLSCREEN_BUTTON:
    case DEFULLSCREEN_BUTTON:return Theme::tooltipWithKey( NULL, qtr( "Fullscreen" ), "key-toggle-fullscreen" );
    case AUDIO_TRACKS_BUTTON:return Theme::tooltipWithKey( NULL, qtr( "Audio tracks and device" ), "key-audio-track" );
    case SUBTITLE_TRACKS_BUTTON:return Theme::tooltipWithKey( NULL, qtr( "Subtitles" ), "key-subtitle-track" );
    case SNAPSHOT_BUTTON:    return Theme::tooltipWithKey( NULL, qtr( "Take a snapshot" ), "key-snapshot" );
    case RECORD_BUTTON:      return Theme::tooltipWithKey( NULL, qtr( "Record" ), "key-record" );
    case LOOP_BUTTON:        return Theme::tooltipWithKey( NULL, qtr( "Loop: off" ), "key-loop" );
    case ROTATE_BUTTON:      return qtr( "Rotate video 90\u00B0 (right-click for more)" );
    case EXTENDED_BUTTON:
        return QString( "%1  <span style='color:%2'>Ctrl+E</span>" )
                   .arg( qtr( "Adjustments and effects" ), Theme::colors().textMuted.name() );
    case PLAYLIST_BUTTON:
        return QString( "%1  <span style='color:%2'>Ctrl+L</span>" )
                   .arg( qtr( "Queue" ), Theme::colors().textMuted.name() );
    default:                 return base;
    }
}

static QIcon buttonIcon( int button )
{
    if( Theme::redesign() )
    {
        QString name = redesignIconName( button );
        if( !name.isEmpty() )
            return Theme::icon( name );
    }
    return QIcon( iconL[button] );
}

/* Generic button setup */
void AbstractController::setupButton( QAbstractButton *aButton, buttonType_e )
{
    static QSizePolicy sizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
    sizePolicy.setHorizontalStretch( 0 );
    sizePolicy.setVerticalStretch( 0 );

    aButton->setSizePolicy( sizePolicy );
    if( Theme::redesign() )
    {
        aButton->setFixedSize( QSize( Theme::BAR_BUTTON, Theme::BAR_BUTTON ) );
        aButton->setIconSize( QSize( Theme::BAR_GLYPH, Theme::BAR_GLYPH ) );
        aButton->setCursor( Qt::PointingHandCursor );
        QToolButton *tb = qobject_cast<QToolButton *>( aButton );
        if( tb ) tb->setAutoRaise( true );
        Theme::installHoverFade( aButton );
    }
    else
    {
        aButton->setFixedSize( QSize( 26, 26 ) );
        aButton->setIconSize( QSize( 20, 20 ) );
    }
    aButton->setFocusPolicy( Qt::NoFocus );
}

/* Audio / subtitle track popups (redesign). The menu content comes from
 * the same builders as the Audio and Subtitle menus of the menu bar. */
QWidget *AbstractController::trackMenuButton( buttonType_e button )
{
    QToolButton *b = new QToolButton;
    setupButton( b, button );
    b->setToolTip( buttonTooltip( button ) );
    b->setIcon( buttonIcon( button ) );
    intf_thread_t *intf = p_intf;
    b->setProperty( "vlcGlyph", button == AUDIO_TRACKS_BUTTON ? "audio" : "subtitles" );
    connect( b, &QToolButton::clicked, b, [b, button, intf]() {
        if( Theme::redesign() )
        {
            TrackPopover::toggle( intf, b, button == AUDIO_TRACKS_BUTTON
                                           ? TrackPopover::AUDIO_TAB : TrackPopover::SUBTITLES_TAB );
            return;
        }
        QMenu menu( b );
        if( button == AUDIO_TRACKS_BUTTON )
            VLCMenuBar::AudioMenu( intf, &menu );
        else
            VLCMenuBar::SubtitleMenu( intf, &menu, true );
        menu.ensurePolished();
        const QSize hint = menu.sizeHint();
        QPoint pos = b->mapToGlobal( QPoint( b->width() - hint.width(), -hint.height() - 6 ) );
        if( pos.y() < 0 ) /* no room above (fullscreen top edge): open below */
            pos = b->mapToGlobal( QPoint( b->width() - hint.width(), b->height() + 6 ) );
        menu.exec( pos );
    } );
    return b;
}

/* Rotate: each click turns the video 90 degrees clockwise through the
 * "transform" filter (none -> 90 -> 180 -> 270 -> none); right-click offers
 * the other transforms. Accent while the video is transformed. */
QWidget *AbstractController::rotateButton()
{
    QToolButton *b = new QToolButton;
    b->setObjectName( "ROTATEButton" );
    setupButton( b, ROTATE_BUTTON );
    b->setContextMenuPolicy( Qt::CustomContextMenu );
    intf_thread_t *intf = p_intf;

    auto current = [intf]() -> QString {
        return VideoFilters::isEnabled( intf, "transform" )
             ? VideoFilters::option( intf, "transform-type" ).toString() : QString();
    };
    auto show = [b]( const QString &t ) {
        static const struct { const char *type; const char *label; } names[] = {
            { "90", N_( "Rotated 90\u00B0" ) }, { "180", N_( "Rotated 180\u00B0" ) },
            { "270", N_( "Rotated 270\u00B0" ) }, { "hflip", N_( "Flipped horizontally" ) },
            { "vflip", N_( "Flipped vertically" ) }, { "transpose", N_( "Transposed" ) },
            { "antitranspose", N_( "Anti-transposed" ) } };
        QString state;
        for( auto &n : names )
            if( t == n.type )
                state = qtr( n.label );
        b->setIcon( QIcon( Theme::pixmap( "rotate", Theme::BAR_GLYPH,
                                          t.isEmpty() ? Theme::colors().icon : Theme::accent() ) ) );
        b->setToolTip( state.isEmpty() ? qtr( "Rotate video 90\u00B0 (right-click for more)" )
                                       : state + QString( "  <span style='color:%1'>%2</span>" )
                                             .arg( Theme::colors().textMuted.name(), qtr( "click to rotate further" ) ) );
    };
    auto apply = [intf, show]( const QString &t ) {
        VideoFilters::setEnabled( intf, "transform", false );
        if( !t.isEmpty() )
        {
            VideoFilters::setOption( intf, "transform", "transform-type", t );
            VideoFilters::setEnabled( intf, "transform", true );
        }
        show( t );
    };
    show( current() );

    connect( b, &QToolButton::clicked, b, [current, apply]() {
        const QString t = current();
        apply( t == "90" ? "180" : t == "180" ? "270" : t == "270" ? "" : "90" );
    } );
    connect( b, &QToolButton::customContextMenuRequested, b, [b, current, apply]( const QPoint & ) {
        QMenu menu( b );
        const QString t = current();
        static const struct { const char *type; const char *label; } items[] = {
            { "90", N_( "Rotate 90\u00B0" ) }, { "180", N_( "Rotate 180\u00B0" ) },
            { "270", N_( "Rotate 270\u00B0" ) }, { "hflip", N_( "Flip Horizontally" ) },
            { "vflip", N_( "Flip Vertically" ) } };
        for( auto &it : items )
        {
            QAction *a = menu.addAction( qtr( it.label ) );
            a->setCheckable( true );
            a->setChecked( t == it.type );
            const QString type = it.type;
            QObject::connect( a, &QAction::triggered, b, [apply, type]() { apply( type ); } );
        }
        menu.addSeparator();
        QAction *reset = menu.addAction( qtr( "Reset" ), b, [apply]() { apply( QString() ); } );
        reset->setEnabled( !t.isEmpty() );
        menu.ensurePolished();
        const QSize hint = menu.sizeHint();
        menu.exec( b->mapToGlobal( QPoint( b->width() - hint.width(), -hint.height() - 6 ) ) );
    } );
    return b;
}

/* Open the generic config line for the toolbar, parse it
 * and create the widgets accordingly */
void AbstractController::parseAndCreate( const QString& config,
                                         QBoxLayout *newControlLayout )
{
    QStringList list = config.split( ";",
                                      #if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
                                        Qt::SkipEmptyParts
                                      #else
                                        QString::SkipEmptyParts
                                      #endif
                                    );
    for( int i = 0; i < list.count(); i++ )
    {
        QStringList list2 = list.at( i ).split( "-" );
        if( list2.count() < 1 )
        {
            msg_Warn( p_intf, "Parsing error 1. Please, report this." );
            continue;
        }

        bool ok;
        int i_option = WIDGET_NORMAL;
        buttonType_e i_type = (buttonType_e)list2.at( 0 ).toInt( &ok );
        if( !ok )
        {
            msg_Warn( p_intf, "Parsing error 2. Please, report this." );
            continue;
        }

        if( list2.count() > 1 )
        {
            i_option = list2.at( 1 ).toInt( &ok );
            if( !ok )
            {
                msg_Warn( p_intf, "Parsing error 3. Please, report this." );
                continue;
            }
        }

        createAndAddWidget( newControlLayout, -1, i_type, i_option );
    }

    if( buttonGroupLayout )
    {
        newControlLayout->addLayout( buttonGroupLayout );
        buttonGroupLayout = NULL;
    }
}

void AbstractController::createAndAddWidget( QBoxLayout *controlLayout_,
                                             int i_index,
                                             buttonType_e i_type,
                                             int i_option )
{
    VLC_UNUSED( i_index ); // i_index should only be required for edition

    /* Close the current buttonGroup if we have a special widget or a spacer */
    if( buttonGroupLayout && i_type > BUTTON_MAX )
    {
        controlLayout_->addLayout( buttonGroupLayout );
        buttonGroupLayout = NULL;
    }

    /* Special case for SPACERS, who aren't QWidgets */
    if( i_type == WIDGET_SPACER )
    {
        controlLayout_->addSpacing( Theme::redesign() ? Theme::BAR_SPACER : 12 );
    }
    else if(  i_type == WIDGET_SPACER_EXTEND )
    {
        controlLayout_->addStretch( 12 );
    }
    else
    {
        /* Create the widget */
        QWidget *widg = createWidget( i_type, i_option );
        if( !widg ) return;
        /* lets a narrow bar choose what to fold away (fitToWidth) */
        widg->setProperty( "vlcBarItem", int( i_type ) );

        /* Buttons */
        if( i_type < BUTTON_MAX )
        {
            if( !buttonGroupLayout )
            {
                buttonGroupLayout = new QHBoxLayout;
                if( Theme::redesign() )
                    /* Figma's music bar packs prev/-15/play/+15/next tighter */
                    buttonGroupLayout->setSpacing( musicBar ? 8 : Theme::BAR_GROUP_SPACING );
            }
            buttonGroupLayout->addWidget( widg );
        }
        else /* Special widgets */
        {
            controlLayout_->addWidget( widg );
        }
    }
}

#define BUTTON_SET_BAR( a_button ) \
    a_button->setToolTip( buttonTooltip( button ) ); \
    a_button->setIcon( buttonIcon( button ) );
#define BUTTON_SET_BAR2( button, image, tooltip ) \
    button->setToolTip( tooltip );          \
    button->setIcon( QIcon( ":/"#image ".svg" ) );

#define ENABLE_ON_VIDEO( a ) \
    connect( THEMIM->getIM(), &InputManager::voutChanged, a, &QToolButton::setEnabled ); \
    a->setEnabled( THEMIM->getIM()->hasVideo() ); /* TODO: is this necessary? when input is started before the interface? */

#define ENABLE_ON_INPUT( a ) \
    connect( this, &AbstractController::inputExists, a, &QToolButton::setEnabled ); \
    a->setEnabled( THEMIM->getIM()->hasInput() ); /* TODO: is this necessary? when input is started before the interface? */

#define NORMAL_BUTTON( name )                           \
    QToolButton * name ## Button = new QToolButton;     \
    name ## Button->setObjectName( #name "Button" );    \
    setupButton( name ## Button );                      \
    connect( name ## Button, &QToolButton::clicked, toolbarActionsMapper, QOverload<>::of(&QSignalMapper::map) ); \
    toolbarActionsMapper->setMapping( name ## Button, name ## _ACTION ); \
    BUTTON_SET_BAR( name ## Button );                   \
    widget = name ## Button;

QWidget *AbstractController::createWidget( buttonType_e button, int options )
{
    bool b_flat  = options & WIDGET_FLAT;
    bool b_big   = options & WIDGET_BIG;
    bool b_shiny = options & WIDGET_SHINY;
    bool b_special = false;

    QWidget *widget = NULL;
    switch( button )
    {
    case PLAY_BUTTON: {
        PlayButton *playButton = new PlayButton;
        setupButton( playButton );
        BUTTON_SET_BAR(  playButton );
        if( Theme::redesign() )
        {
            /* accent pill, a little bigger than Figma's 54x28 */
            playButton->setObjectName( "PlayButton" );
            playButton->setFixedSize( QSize( Theme::PLAY_BUTTON_W, Theme::PLAY_BUTTON_H ) );
            playButton->setIconSize( QSize( Theme::PLAY_GLYPH, Theme::PLAY_GLYPH ) );
        }
        connect( playButton, &PlayButton::clicked, toolbarActionsMapper, QOverload<>::of(&QSignalMapper::map) );
        toolbarActionsMapper->setMapping( playButton, PLAY_ACTION );
        connect( this, &AbstractController::inputPlaying, playButton, &PlayButton::updateButtonIcons );
        playButton->updateButtonIcons( THEMIM->getIM()->playingStatus() == PLAYING_S );
        widget = playButton;
        }
        break;
    case STOP_BUTTON:{
        NORMAL_BUTTON( STOP );
        }
        break;
    case OPEN_BUTTON:{
        NORMAL_BUTTON( OPEN );
        }
        break;
    case OPEN_SUB_BUTTON:{
        NORMAL_BUTTON( OPEN_SUB );
        }
        break;
    case PREVIOUS_BUTTON:{
        NORMAL_BUTTON( PREVIOUS );
        if( Theme::redesign() ) /* narrow targets hug the play pill */
        {
            PREVIOUSButton->setFixedWidth( Theme::TRANSPORT_BUTTON_W );
            PREVIOUSButton->setIconSize( QSize( Theme::TRANSPORT_GLYPH, Theme::TRANSPORT_GLYPH ) );
        }
        }
        break;
    case NEXT_BUTTON: {
        NORMAL_BUTTON( NEXT );
        if( Theme::redesign() )
        {
            NEXTButton->setFixedWidth( Theme::TRANSPORT_BUTTON_W );
            NEXTButton->setIconSize( QSize( Theme::TRANSPORT_GLYPH, Theme::TRANSPORT_GLYPH ) );
        }
        }
        break;
    case SLOWER_BUTTON:{
        NORMAL_BUTTON( SLOWER );
        ENABLE_ON_INPUT( SLOWERButton );
        }
        break;
    case FASTER_BUTTON:{
        NORMAL_BUTTON( FASTER );
        ENABLE_ON_INPUT( FASTERButton );
        }
        break;
    case PREV_SLOW_BUTTON:{
        QToolButtonExt *but = new QToolButtonExt;
        setupButton( but );
        BUTTON_SET_BAR( but );
        connect( but, &QToolButtonExt::shortClicked, THEMIM, &MainInputManager::prev );
        connect( but, &QToolButtonExt::longClicked, THEAM, &ActionsManager::skipBackward );
        widget = but;
        }
        break;
    case NEXT_FAST_BUTTON:{
        QToolButtonExt *but = new QToolButtonExt;
        setupButton( but );
        BUTTON_SET_BAR( but );
        connect( but, &QToolButtonExt::shortClicked, THEMIM, &MainInputManager::next );
        connect( but, &QToolButtonExt::longClicked, THEAM, &ActionsManager::skipForward );
        widget = but;
        }
        break;
    case FRAME_BUTTON: {
        NORMAL_BUTTON( FRAME );
        ENABLE_ON_VIDEO( FRAMEButton );
        }
        break;
    case FULLSCREEN_BUTTON:
    case DEFULLSCREEN_BUTTON:
        {
        NORMAL_BUTTON( FULLSCREEN );
        ENABLE_ON_VIDEO( FULLSCREENButton );
        }
        break;
    case FULLWIDTH_BUTTON: {
            NORMAL_BUTTON( FULLWIDTH );
        }
        break;
    case AUDIO_TRACKS_BUTTON:
    case SUBTITLE_TRACKS_BUTTON:
        widget = trackMenuButton( button );
        break;
    case EXTENDED_BUTTON:{
        NORMAL_BUTTON( EXTENDED );
        }
        break;
    case PLAYLIST_BUTTON:{
        NORMAL_BUTTON( PLAYLIST );
        }
        break;
    case SNAPSHOT_BUTTON:{
        NORMAL_BUTTON( SNAPSHOT );
        ENABLE_ON_VIDEO( SNAPSHOTButton );
        }
        break;
    case RECORD_BUTTON:{
        QToolButton *recordButton = new QToolButton;
        setupButton( recordButton );
        recordButton->setCheckable( true );
        connect( recordButton, &QToolButton::clicked, toolbarActionsMapper, QOverload<>::of(&QSignalMapper::map) );
        toolbarActionsMapper->setMapping( recordButton, RECORD_ACTION );
        connect( THEMIM->getIM(), &InputManager::recordingStateChanged, recordButton, &QToolButton::setChecked );
        BUTTON_SET_BAR(  recordButton );
        ENABLE_ON_INPUT( recordButton );
        widget = recordButton;
        }
        break;
    case ATOB_BUTTON: {
        AtoB_Button *ABButton = new AtoB_Button;
        setupButton( ABButton );
        ABButton->setShortcut( qtr("Shift+L") );
        BUTTON_SET_BAR( ABButton );
        ENABLE_ON_INPUT( ABButton );
        connect( ABButton, &AtoB_Button::clicked, toolbarActionsMapper, QOverload<>::of(&QSignalMapper::map) );
        toolbarActionsMapper->setMapping( ABButton, ATOB_ACTION );
        connect( THEMIM->getIM(), &InputManager::AtoBchanged, ABButton, &AtoB_Button::updateButtonIcons );
        widget = ABButton;
        }
        break;
    case INPUT_SLIDER: {
        SeekSlider *slider = new SeekSlider( p_intf, Qt::Horizontal, NULL,
                                             !b_shiny && !Theme::redesign() );
        SeekPoints *chapters = new SeekPoints( this, p_intf );
        connect( THEMIM->getIM(), &InputManager::chapterChanged, chapters, &SeekPoints::update );
        slider->setChapters( chapters );

        /* Update the position when the IM has changed */
        connect( THEMIM->getIM(), &InputManager::positionUpdated,
                slider, &SeekSlider::setPosition );
        /* And update the IM, when the position has changed */
        connect( slider, &SeekSlider::sliderDragged,
                 THEMIM->getIM(), &InputManager::sliderUpdate );
        connect( THEMIM->getIM(), &InputManager::cachingChanged,
                 slider, &SeekSlider::updateBuffering );
        /* Give hint to disable slider's interactivity when useless */
        connect( THEMIM->getIM(), &InputManager::inputCanSeek,
                 slider, &SeekSlider::setSeekable );
        if( musicBar )
            slider->setWaveform( true );
        widget = slider;
        }
        break;
    case MENU_BUTTONS:
        widget = discFrame();
        break;
    case TELETEXT_BUTTONS:
        widget = telexFrame();
        widget->hide();
        break;
    case VOLUME_SPECIAL:
        b_special = true;
    case VOLUME:
        {
            SoundWidget *snd = new SoundWidget( this, p_intf,
                                                b_shiny || Theme::redesign(), b_special );
            widget = snd;
        }
        break;
    case TIME_LABEL:
        {
            TimeLabel *timeLabel = new TimeLabel( p_intf );
            widget = timeLabel;
        }
        break;
    case SPLITTER:
        {
            QFrame *line = new QFrame;
            line->setFrameShape( QFrame::VLine );
            line->setFrameShadow( QFrame::Raised );
            line->setLineWidth( 0 );
            line->setMidLineWidth( 1 );
            widget = line;
        }
        break;
    case ADVANCED_CONTROLLER:
        if( qobject_cast<AdvControlsWidget *>(this) == NULL )
        {
            advControls = new AdvControlsWidget( p_intf, this );
            widget = advControls;
        }
        break;
    case REVERSE_BUTTON:{
        QToolButton *reverseButton = new QToolButton;
        setupButton( reverseButton );
        connect( reverseButton, &QToolButton::clicked, THEMIM->getIM(), &InputManager::reverse);
        BUTTON_SET_BAR(  reverseButton );
        reverseButton->setCheckable( true );
        /* You should, of COURSE change this to the correct event,
           when/if we have one, that tells us if trickplay is possible . */
        connect( this, &AbstractController::inputIsTrickPlayable, reverseButton, &QToolButton::setVisible );
        reverseButton->setVisible( false );
        widget = reverseButton;
        }
        break;
    case SKIP_BACK_BUTTON: {
        NORMAL_BUTTON( SKIP_BACK );
        ENABLE_ON_INPUT( SKIP_BACKButton );
        }
        break;
    case SKIP_FW_BUTTON: {
        NORMAL_BUTTON( SKIP_FW );
        ENABLE_ON_INPUT( SKIP_FWButton );
        }
        break;
    case QUIT_BUTTON: {
        NORMAL_BUTTON( QUIT );
        }
        break;
    case RANDOM_BUTTON: {
        NORMAL_BUTTON( RANDOM );
        RANDOMButton->setCheckable( true );
        RANDOMButton->setChecked( var_GetBool( THEPL, "random" ) );
        connect( THEMIM, &MainInputManager::randomChanged, RANDOMButton, &QToolButton::setChecked );
        }
        break;
    case LOOP_BUTTON:{
        LoopButton *loopButton = new LoopButton;
        setupButton( loopButton, LOOP_BUTTON );
        loopButton->setToolTip( qtr( "Click to toggle between loop all, loop one and no loop") );
        /* the redesign shows the state with the glyph colour */
        loopButton->setCheckable( !Theme::redesign() );
        {
            int i_state = NORMAL;
            if( var_GetBool( THEPL, "loop" ) )   i_state = REPEAT_ALL;
            if( var_GetBool( THEPL, "repeat" ) ) i_state = REPEAT_ONE;
            loopButton->updateButtonIcons( i_state );
        }

        connect( THEMIM, &MainInputManager::repeatLoopChanged, loopButton, &LoopButton::updateButtonIcons );
        connect( loopButton, &LoopButton::clicked, THEMIM, &MainInputManager::loopRepeatLoopStatus );
        widget = loopButton;
        }
        break;
    case ROTATE_BUTTON:
        widget = rotateButton();
        break;
    case INFO_BUTTON: {
        NORMAL_BUTTON( INFO );
        }
        break;
    case PLAYBACK_BUTTONS:{
        widget = new QWidget;
        DeckButtonsLayout *layout = new DeckButtonsLayout( widget );
        BrowseButton *prev = new BrowseButton( widget, BrowseButton::Backward );
        BrowseButton *next = new BrowseButton( widget );
        RoundButton *play = new RoundButton( widget );
        layout->setBackwardButton( prev );
        layout->setForwardButton( next );
        layout->setRoundButton( play );
        connect( prev, &BrowseButton::clicked, toolbarActionsMapper, QOverload<>::of(&QSignalMapper::map) );
        toolbarActionsMapper->setMapping( prev, PREVIOUS_ACTION );
        connect( next, &BrowseButton::clicked, toolbarActionsMapper, QOverload<>::of(&QSignalMapper::map) );
        toolbarActionsMapper->setMapping( next, NEXT_ACTION );
        connect( play, &BrowseButton::clicked, toolbarActionsMapper, QOverload<>::of(&QSignalMapper::map) );
        toolbarActionsMapper->setMapping( play, PLAY_ACTION );
        }
        break;
    case ASPECT_RATIO_COMBOBOX:
        widget = new AspectRatioComboBox( p_intf );
        widget->setMinimumHeight( 26 );
        break;
    case SPEED_LABEL:
        widget = new SpeedLabel( p_intf, this );
        break;
    case TIME_LABEL_ELAPSED:
        widget = new TimeLabel( p_intf, TimeLabel::Elapsed );
        if( Theme::redesign() )
        {
            /* fixed box, text pinned next to the seek bar */
            TimeLabel *l = static_cast<TimeLabel *>( widget );
            l->setFixedWidth( Theme::TIME_LABEL_W );
            l->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
            l->setContentsMargins( 0, 0, Theme::TIME_LABEL_GAP_L, 0 );
        }
        break;
    case TIME_LABEL_REMAINING:
        widget = new TimeLabel( p_intf, TimeLabel::Remaining );
        if( Theme::redesign() )
        {
            TimeLabel *l = static_cast<TimeLabel *>( widget );
            l->setFixedWidth( Theme::TIME_LABEL_W );
            l->setAlignment( Qt::AlignLeft | Qt::AlignVCenter );
            l->setContentsMargins( Theme::TIME_LABEL_GAP_R, 0, 0, 0 );
        }
        break;
    default:
        msg_Warn( p_intf, "This should not happen %i", button );
        break;
    }

    /* Customize Buttons */
    if( b_flat || b_big )
    {
        QFrame *frame = qobject_cast<QFrame *>(widget);
        if( frame )
        {
            QList<QToolButton *> allTButtons = frame->findChildren<QToolButton *>();
            for( int i = 0; i < allTButtons.count(); i++ )
                applyAttributes( allTButtons[i], b_flat, b_big );
        }
        else
        {
            QToolButton *tmpButton = qobject_cast<QToolButton *>(widget);
            if( tmpButton )
                applyAttributes( tmpButton, b_flat, b_big );
        }
    }
    return widget;
}
#undef NORMAL_BUTTON

void AbstractController::applyAttributes( QToolButton *tmpButton, bool b_flat, bool b_big )
{
    if( tmpButton )
    {
        if( b_flat )
            tmpButton->setAutoRaise( b_flat );
        if( b_big )
        {
            tmpButton->setFixedSize( QSize( 32, 32 ) );
            tmpButton->setIconSize( QSize( 26, 26 ) );
        }
    }
}

QFrame *AbstractController::discFrame()
{
    /** Disc and Menus handling */
    QFrame *discFrame = new QFrame( this );

    QHBoxLayout *discLayout = new QHBoxLayout( discFrame );
    discLayout->setSpacing( 0 ); discLayout->setContentsMargins( 0, 0, 0, 0 );


    QFrame *chapFrame = new QFrame( discFrame );
    QHBoxLayout *chapLayout = new QHBoxLayout( chapFrame );
    chapLayout->setSpacing( 0 ); chapLayout->setContentsMargins( 0, 0, 0, 0 );

    QToolButton *prevSectionButton = new QToolButton( chapFrame );
    setupButton( prevSectionButton );
    BUTTON_SET_BAR2( prevSectionButton, toolbar/dvd_prev,
            qtr("Previous Chapter/Title" ) );
    chapLayout->addWidget( prevSectionButton );

    QToolButton *nextSectionButton = new QToolButton( chapFrame );
    setupButton( nextSectionButton );
    BUTTON_SET_BAR2( nextSectionButton, toolbar/dvd_next,
            qtr("Next Chapter/Title" ) );
    chapLayout->addWidget( nextSectionButton );

    discLayout->addWidget( chapFrame );
    chapFrame->hide();

    QFrame *menuFrame = new QFrame( discFrame );
    QHBoxLayout *menuLayout = new QHBoxLayout( menuFrame );
    menuLayout->setSpacing( 0 ); menuLayout->setContentsMargins( 0, 0, 0, 0 );

    QToolButton *menuButton = new QToolButton( menuFrame );
    setupButton( menuButton );
    menuLayout->addWidget( menuButton );
    BUTTON_SET_BAR2( menuButton, toolbar/dvd_menu, qtr( "Menu" ) );

    discLayout->addWidget( menuFrame );
    menuFrame->hide();

    /* Change the navigation button display when the IM
       navigation changes */
    connect( THEMIM->getIM(), &InputManager::chapterChanged, chapFrame, &QFrame::setVisible );
    connect( THEMIM->getIM(), &InputManager::titleChanged, menuFrame, &QFrame::setVisible );
    /* Changes the IM navigation when triggered on the nav buttons */
    connect( prevSectionButton, &QToolButton::clicked, THEMIM->getIM(), &InputManager::sectionPrev );
    connect( nextSectionButton, &QToolButton::clicked, THEMIM->getIM(), &InputManager::sectionNext );
    connect( menuButton, &QToolButton::clicked, THEMIM->getIM(), &InputManager::sectionMenu );

    return discFrame;
}

QFrame *AbstractController::telexFrame()
{
    /**
     * Telextext QFrame
     **/
    QFrame *telexFrame = new QFrame( this );
    QHBoxLayout *telexLayout = new QHBoxLayout( telexFrame );
    telexLayout->setSpacing( 0 ); telexLayout->setContentsMargins( 0, 0, 0, 0 );
    connect( THEMIM->getIM(), &InputManager::teletextPossible,
             telexFrame, &QFrame::setVisible );

    /* On/Off button */
    QToolButton *telexOn = new QToolButton;
    setupButton( telexOn );
    BUTTON_SET_BAR2( telexOn, toolbar/tv, qtr( "Teletext Activation" ) );
    telexOn->setEnabled( false );
    telexOn->setCheckable( true );

    telexLayout->addWidget( telexOn );

    /* Teletext Activation and set */
    connect( telexOn, &QToolButton::clicked, THEMIM->getIM(), &InputManager::activateTeletext );
    connect( THEMIM->getIM(), &InputManager::teletextPossible, telexOn, &QToolButton::setEnabled );

    /* Transparency button */
    QToolButton *telexTransparent = new QToolButton;
    setupButton( telexTransparent );
    BUTTON_SET_BAR2( telexTransparent, toolbar/tvtelx,
                     qtr( "Toggle Transparency" ) );
    telexTransparent->setEnabled( false );
    telexTransparent->setCheckable( true );
    telexLayout->addWidget( telexTransparent );

    /* Transparency change and set */
    connect( telexTransparent, &QToolButton::clicked, THEMIM->getIM(), &InputManager::telexSetTransparency );
    connect( THEMIM->getIM(), &InputManager::teletextTransparencyActivated, telexTransparent, &QToolButton::setChecked );


    /* Page setting */
    QSpinBox *telexPage = new QSpinBox( telexFrame );
    telexPage->setRange( 100, 899 );
    telexPage->setValue( 100 );
    telexPage->setAccelerated( true );
    telexPage->setWrapping( true );
    telexPage->setAlignment( Qt::AlignRight );
    telexPage->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Minimum );
    telexPage->setEnabled( false );
    telexLayout->addWidget( telexPage );

    /* Contextual & Index Buttons */
    QSignalMapper *contextButtonMapper = new QSignalMapper( this );
    QToolButton *contextButton = NULL;
    int i_iconminsize = __MAX( 16, telexOn->minimumHeight() );

#if HAS_QT56
    qreal f_ratio = QApplication::primaryScreen()->devicePixelRatio();
    QPixmap iconPixmap( i_iconminsize * f_ratio, i_iconminsize * f_ratio );
#else
    QPixmap iconPixmap( i_iconminsize, i_iconminsize );
#endif

    iconPixmap.fill( Qt::transparent );
    QPainter iconPixmapPainter( &iconPixmap );
    QLinearGradient iconPixmapPainterGradient( iconPixmap.rect().center() / 2,
                                               iconPixmap.rect().center() );

#define CREATE_CONTEXT_BUTTON(color, key) \
    iconPixmapPainterGradient.setColorAt( 0, QColor( color ).lighter(150) );\
    iconPixmapPainterGradient.setColorAt( 1.0, QColor( color ) );\
    iconPixmapPainter.setBrush( iconPixmapPainterGradient );\
    iconPixmapPainter.drawEllipse( iconPixmap.rect().adjusted( 4, 4, -5, -5 ) );\
    contextButton = new QToolButton();\
    setupButton( contextButton );\
    contextButton->setIcon( iconPixmap );\
    contextButton->setEnabled( false );\
    contextButtonMapper->setMapping( contextButton, key << 16 );\
    connect( contextButton, &QToolButton::clicked, contextButtonMapper, QOverload<>::of(&QSignalMapper::map) );\
    connect( contextButtonMapper, QSIGNALMAPPER_MAPPEDINT_SIGNAL, THEMIM->getIM(), &InputManager::telexSetPage );\
    connect( THEMIM->getIM(), &InputManager::teletextActivated, contextButton, &QToolButton::setEnabled );\
    telexLayout->addWidget( contextButton )

    CREATE_CONTEXT_BUTTON("grey", 'i'); /* index */
    CREATE_CONTEXT_BUTTON("red", 'r');
    CREATE_CONTEXT_BUTTON("green", 'g');
    CREATE_CONTEXT_BUTTON("yellow", 'y');
    CREATE_CONTEXT_BUTTON("blue", 'b');

#undef CREATE_CONTEXT_BUTTON

    /* Page change and set */
    connect( telexPage, QOverload<int>::of(&QSpinBox::valueChanged), THEMIM->getIM(), &InputManager::telexSetPage );
    connect( THEMIM->getIM(), &InputManager::newTelexPageSet, telexPage, &QSpinBox::setValue );

    connect( THEMIM->getIM(), &InputManager::teletextActivated, telexPage, &QSpinBox::setEnabled);
    connect( THEMIM->getIM(), &InputManager::teletextActivated, telexTransparent, &QToolButton::setEnabled );
    connect( THEMIM->getIM(), &InputManager::teletextActivated, telexOn, &QToolButton::setChecked );
    return telexFrame;
}
#undef BUTTON_SET_BAR
#undef BUTTON_SET_BAR2
#undef ENABLE_ON_VIDEO
#undef ENABLE_ON_INPUT

#include <QHBoxLayout>
/*****************************
 * DA Control Widget !
 *****************************/
ControlsWidget::ControlsWidget( intf_thread_t *_p_i,
                                bool b_advControls,
                                QWidget *_parent, bool music ) :
                                AbstractController( _p_i, _parent )
{
    RTL_UNAFFECTED_WIDGET
    /* advanced Controls handling */
    b_advancedVisible = b_advControls;
#ifdef DEBUG_LAYOUT
    setStyleSheet( "background: red ");
#endif
    grip = NULL;
    if( Theme::redesign() )
    {
        /* One row: both configurable lines are laid out side by side */
        setObjectName( "VLCControlBar" );
        setAttribute( Qt::WA_StyledBackground, true );
        setFixedHeight( Theme::CONTROLBAR_HEIGHT );
        controlLayout = new QHBoxLayout( this );
        controlLayout->setContentsMargins( Theme::BAR_MARGIN_LEFT, 0, Theme::BAR_MARGIN_RIGHT, 0 );
        controlLayout->setSpacing( 0 );
        /* the layout must not hold the window wide: fitToWidth folds the
         * bar instead, so a narrow window never squeezes controls together */
        controlLayout->setSizeConstraint( QLayout::SetNoConstraint );
        parseAndCreate( getSettings()->value( toolbarKey( TOOLBAR_MAIN1 ),
                            toolbarDefault( TOOLBAR_MAIN1 ) ).toString(), controlLayout );
        QString line = toolbarLine( getSettings(), TOOLBAR_MAIN2 );
        /* music mode swaps the default bar only: a customised one stays */
        if( music && line == REDESIGN_TB2_DEFAULT )
            line = REDESIGN_MUSIC_TB2;
        musicBar = music;
        parseAndCreate( line, controlLayout );
        if( !b_advancedVisible && advControls ) advControls->hide();
        return;
    }

    controlLayout = new QVBoxLayout( this );
    controlLayout->setContentsMargins( 3, 1, 0, 1 );
    controlLayout->setSpacing( 0 );
    QHBoxLayout *controlLayout1 = new QHBoxLayout;
    controlLayout1->setSpacing( 0 ); controlLayout1->setContentsMargins( 0, 0, 0, 0 );

    QString line1 = getSettings()->value( "MainWindow/MainToolbar1", MAIN_TB1_DEFAULT )
                                        .toString();
    parseAndCreate( line1, controlLayout1 );

    QHBoxLayout *controlLayout2 = new QHBoxLayout;
    controlLayout2->setSpacing( 0 ); controlLayout2->setContentsMargins( 0, 0, 0, 0 );
    QString line2 = getSettings()->value( "MainWindow/MainToolbar2", MAIN_TB2_DEFAULT )
                                        .toString();
    parseAndCreate( line2, controlLayout2 );

    grip = new QSizeGrip( this );
    controlLayout2->addWidget( grip, 0, Qt::AlignBottom|Qt::AlignRight );

    if( !b_advancedVisible && advControls ) advControls->hide();

    controlLayout->addLayout( controlLayout1 );
    controlLayout->addLayout( controlLayout2 );
}

namespace
{
/* the order a narrow bar folds in: level 1 goes first, 0 never goes */
int foldLevel( int type )
{
    switch( type )
    {
    case PLAY_BUTTON: case INPUT_SLIDER: case PLAYLIST_BUTTON:
        return 0;
    case ROTATE_BUTTON:
        return 2;
    case LOOP_BUTTON: case RANDOM_BUTTON:
        return 3;
    case AUDIO_TRACKS_BUTTON:
        return 4;
    case SUBTITLE_TRACKS_BUTTON:
        return 5;
    case EXTENDED_BUTTON:
        return 6;
    case VOLUME: case VOLUME_SPECIAL:
        return 7;
    case TIME_LABEL_REMAINING:
        return 8;
    case PREVIOUS_BUTTON: case NEXT_BUTTON: case SKIP_BACK_BUTTON: case SKIP_FW_BUTTON:
        return 9;
    case TIME_LABEL: case TIME_LABEL_ELAPSED:
        return 10;
    default:
        return 1;   /* extras from a customised bar go first */
    }
}
enum { FOLD_LEVELS = 10, MIN_SEEK_WIDTH = 160 };
}

QSize ControlsWidget::minimumSizeHint() const
{
    if( !Theme::redesign() )
        return AbstractController::minimumSizeHint();
    /* margins, the play pill, a usable seek bar and the queue button */
    return QSize( Theme::BAR_MARGIN_LEFT + Theme::BAR_MARGIN_RIGHT + Theme::PLAY_BUTTON_W
                  + 2 * Theme::BAR_SPACER + MIN_SEEK_WIDTH + Theme::BAR_BUTTON,
                  Theme::CONTROLBAR_HEIGHT );
}

void ControlsWidget::resizeEvent( QResizeEvent *e )
{
    AbstractController::resizeEvent( e );
    fitToWidth();
}

void ControlsWidget::showEvent( QShowEvent *e )
{
    AbstractController::showEvent( e );
    fitToWidth();
}

void ControlsWidget::fitToWidth()
{
    if( fitting || !Theme::redesign() || !controlLayout )
        return;
    fitting = true;

    QList<QWidget *> items;
    foreach( QWidget *w, findChildren<QWidget *>( QString(), Qt::FindDirectChildrenOnly ) )
        if( w->property( "vlcBarItem" ).isValid() )
            items << w;

    /* bring back what an earlier, narrower width folded away */
    foreach( QWidget *w, items )
        if( w->property( "vlcFolded" ).toBool() )
        {
            w->setProperty( "vlcFolded", false );
            w->setVisible( true );
        }

    /* the width the bar needs to show what is visible now at its natural
     * size, with room for a usable seek bar (the layout's own minimum lets
     * the seek bar vanish and the time labels overlap) */
    QWidget *slider = NULL;
    foreach( QWidget *w, items )
        if( w->property( "vlcBarItem" ).toInt() == INPUT_SLIDER )
            slider = w;
    auto needed = [this, slider]() {
        for( int i = 0; i < controlLayout->count(); i++ )
            if( QLayout *l = controlLayout->itemAt( i )->layout() )
                l->invalidate();
        controlLayout->invalidate();
        int w = controlLayout->sizeHint().width();
        if( slider && slider->isVisibleTo( this ) )
            w += MIN_SEEK_WIDTH - slider->sizeHint().width();
        return w;
    };
    for( int level = 1; level <= FOLD_LEVELS && needed() > width(); level++ )
        foreach( QWidget *w, items )
            if( foldLevel( w->property( "vlcBarItem" ).toInt() ) == level && w->isVisibleTo( this ) )
            {
                w->setProperty( "vlcFolded", true );
                w->setVisible( false );
            }
    fitting = false;
}

bool ControlsWidget::advancedAvailable()
{
    return advControls != nullptr;
}

void ControlsWidget::toggleAdvanced()
{
    if( !advControls ) return;

    advControls->setVisible( !b_advancedVisible );
    b_advancedVisible = !b_advancedVisible;
    emit advancedControlsToggled( b_advancedVisible );
}

AdvControlsWidget::AdvControlsWidget( intf_thread_t *_p_i, QWidget *_parent ) :
                                     AbstractController( _p_i, _parent )
{
    RTL_UNAFFECTED_WIDGET
    controlLayout = new QHBoxLayout( this );
    controlLayout->setContentsMargins( 0, 0, 0, 0 );
    controlLayout->setSpacing( 0 );
#ifdef DEBUG_LAYOUT
    setStyleSheet( "background: orange ");
#endif


    QString line = getSettings()->value( "MainWindow/AdvToolbar", ADV_TB_DEFAULT )
        .toString();
    parseAndCreate( line, controlLayout );
}

InputControlsWidget::InputControlsWidget( intf_thread_t *_p_i, QWidget *_parent ) :
                                     AbstractController( _p_i, _parent )
{
    RTL_UNAFFECTED_WIDGET
    controlLayout = new QHBoxLayout( this );
    controlLayout->setContentsMargins( 0, 0, 0, 0 );
    controlLayout->setSpacing( 0 );
#ifdef DEBUG_LAYOUT
    setStyleSheet( "background: green ");
#endif

    QString line = getSettings()->value( toolbarKey( TOOLBAR_INPUT ),
                                         toolbarDefault( TOOLBAR_INPUT ) ).toString();
    parseAndCreate( line, controlLayout );
    /* The redesign folds the seek row into the main bar */
    if( Theme::redesign() && line.isEmpty() )
        setFixedHeight( 0 );
}
/**********************************************************************
 * Fullscrenn control widget
 **********************************************************************/
FullscreenControllerWidget::FullscreenControllerWidget( intf_thread_t *_p_i, QWidget *_parent )
                           : AbstractController( _p_i, _parent )
{
    RTL_UNAFFECTED_WIDGET
    i_mouse_last_x      = -1;
    i_mouse_last_y      = -1;
    b_mouse_over        = false;
    i_mouse_last_move_x = -1;
    i_mouse_last_move_y = -1;
#if HAVE_TRANSPARENCY
    b_slow_hide_begin   = false;
    i_slow_hide_timeout = 1;
#endif
    b_fullscreen        = false;
    i_hide_timeout      = 1;
    i_screennumber      = -1;

    vout.clear();

    setWindowFlags( Qt::Tool | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint );
    setAttribute( Qt::WA_ShowWithoutActivating );
    setMinimumWidth( FSC_WIDTH );
    isWideFSC = false;

    setFrameShape( QFrame::StyledPanel );
    setFrameStyle( QFrame::Sunken );
    setSizePolicy( QSizePolicy::Minimum, QSizePolicy::Minimum );

    QVBoxLayout *controlLayout2 = new QVBoxLayout( this );
    controlLayout2->setContentsMargins( 4, 6, 4, 2 );

    if( Theme::redesign() )
    {
        /* Same single row as the docked bar, on a floating panel */
        setObjectName( "VLCFullscreenBar" );
        setFrameShape( QFrame::NoFrame );
        setStyleSheet( QString( "#VLCFullscreenBar { background: %1; border-radius: 8px; }"
                                "#VLCFullscreenBar QToolButton { background: transparent;"
                                " border: none; border-radius: 6px; }"
                                "#VLCFullscreenBar QToolButton:hover { background: %2; }"
                                "#VLCFullscreenBar QToolButton#PlayButton { background: %3; border-radius: 8px; }" )
                       .arg( Theme::colors().controlbar.name(),
                             Theme::colors().elevated.name(),
                             Theme::accent().name() ) );
        /* the docked bar's own box model, full width at the bottom */
        controlLayout2->setContentsMargins( Theme::BAR_MARGIN_LEFT, 0,
                                            Theme::BAR_MARGIN_RIGHT, 0 );
        setFixedHeight( Theme::CONTROLBAR_HEIGHT );
        setStyleSheet( styleSheet().replace( "border-radius: 8px;", "border-radius: 0px;" ) );
        isWideFSC = true;
        buildTopStrip();
        fadeAnim = new FrameAnimation( this );
        fadeAnim->setDuration( 150 );
        fadeAnim->setEasingCurve( QEasingCurve::OutCubic );
        connect( fadeAnim, &FrameAnimation::valueChanged, this, [this]( const QVariant &v ) {
            setWindowOpacity( v.toReal() );
            if( topStrip )
                topStrip->setWindowOpacity( v.toReal() );
        } );
        connect( fadeAnim, &FrameAnimation::finished, this, [this]() {
            if( fadeAnim->endValue().toReal() <= 0.0 )
            {
                hide();
                if( topStrip )
                    topStrip->hide();
            }
        } );
    }
    else
    {
        /* First line */
        InputControlsWidget *inputC = new InputControlsWidget( p_intf, this );
        controlLayout2->addWidget( inputC );
    }

    controlLayout = new QHBoxLayout;
    QString line = toolbarLine( getSettings(), TOOLBAR_FSC );
    parseAndCreate( line, controlLayout );
    controlLayout2->addLayout( controlLayout );

    /* hiding timer */
    p_hideTimer = new QTimer( this );
    p_hideTimer->setSingleShot( true );
    connect( p_hideTimer, &QTimer::timeout, this, &FullscreenControllerWidget::hideFSC );

    /* slow hiding timer */
#if HAVE_TRANSPARENCY
    p_slowHideTimer = new QTimer( this );
    connect( p_slowHideTimer, &QTimer::timeout, this, &FullscreenControllerWidget::slowHideFSC );
    f_opacity = var_InheritFloat( p_intf, "qt-fs-opacity" );
#endif

    i_sensitivity = var_InheritInteger( p_intf, "qt-fs-sensitivity" );

    vlc_mutex_init_recursive( &lock );

    connect( THEMIM->getIM(), &InputManager::voutListChanged,
             this, &FullscreenControllerWidget::setVoutList, Qt::DirectConnection );

    /* First Move */
    previousPosition = getSettings()->value( "FullScreen/pos" ).toPoint();
    screenRes = getSettings()->value( "FullScreen/screen" ).toRect();
    if( !Theme::redesign() )
        isWideFSC = getSettings()->value( "FullScreen/wide" ).toBool();

    connect( this, QOverload<bool>::of(&FullscreenControllerWidget::fullscreenChanged), THEMIM, &MainInputManager::changeFullscreen );

    Q_ASSERT( _parent );
    _parent->installEventFilter( this );
}

FullscreenControllerWidget::~FullscreenControllerWidget()
{
    QWidget *wParent = parentWidget();
    Q_ASSERT( wParent );
    wParent->removeEventFilter( this );

    getSettings()->setValue( "FullScreen/pos", previousPosition );
    getSettings()->setValue( "FullScreen/screen", screenRes );
    if( !Theme::redesign() )
        getSettings()->setValue( "FullScreen/wide", isWideFSC );
    delete topStrip;

    setVoutList( NULL, 0 );
    vlc_mutex_destroy( &lock );
}

void FullscreenControllerWidget::restoreFSC()
{
    if( !isWideFSC )
    {
        /* Restore half-bar and re-centre if needed */
        setMinimumWidth( FSC_WIDTH );
        adjustSize();

        if ( targetScreen() < 0 )
            return;

        QRect currentRes = QGuiApplication::screens()[ targetScreen() ]->geometry();
        QWindow *wh = windowHandle();
        if ( wh != Q_NULLPTR )
        {
            wh->setScreen(QGuiApplication::screens()[targetScreen()]);
        }

        if( currentRes == screenRes &&
            currentRes.contains( previousPosition, true ) )
        {
            /* Restore to the last known position */
            move( previousPosition );
        }
        else
        {
            /* FSC is out of screen or screen resolution changed */
            msg_Dbg( p_intf, "Recentering the Fullscreen Controller" );
            centerFSC( targetScreen() );
            screenRes = currentRes;
            previousPosition = pos();
        }
    }
    else if( Theme::redesign() )
    {
        const QRect sg = fsScreenGeometry();
        if( windowHandle() && QtCompat::screenAt( sg.center() ) )
            windowHandle()->setScreen( QtCompat::screenAt( sg.center() ) );
        setMinimumWidth( sg.width() );
        setGeometry( sg.x(), sg.y() + sg.height() - height(), sg.width(), height() );
        placeTopStrip();
    }
    else
    {
        /* Dock at the bottom of the screen */
        updateFullwidthGeometry( targetScreen() );
    }
}

/* The screen the video is fullscreen on (targetScreen() can be -1) */
QRect FullscreenControllerWidget::fsScreenGeometry()
{
    const int n = targetScreen();
    if( n >= 0 && n < QGuiApplication::screens().length() )
        return QGuiApplication::screens()[n]->geometry();
    QWidget *mi = parentWidget();
    if( mi && mi->windowHandle() && mi->windowHandle()->screen() )
        return mi->windowHandle()->screen()->geometry();
    return QGuiApplication::primaryScreen()->geometry();
}

/* Title strip: the windowed header's look (cone, centred title, leave
 * fullscreen, close), as its own frameless tool window like this bar */
void FullscreenControllerWidget::buildTopStrip()
{
    topStrip = new QWidget( NULL, Qt::Tool | Qt::FramelessWindowHint
                                  | Qt::X11BypassWindowManagerHint );
    topStrip->setObjectName( "VLCHeader" );
    topStrip->setAttribute( Qt::WA_ShowWithoutActivating );
    topStrip->setAttribute( Qt::WA_StyledBackground, true );
    topStrip->setFixedHeight( Theme::TITLEBAR_HEIGHT );
    topStrip->installEventFilter( this );

    QHBoxLayout *l = new QHBoxLayout( topStrip );
    l->setContentsMargins( 12, 0, 6, 0 );
    l->setSpacing( 0 );
    QLabel *cone = new QLabel( topStrip );
    cone->setPixmap( QIcon( ":/logo/vlc128.png" ).pixmap( 20, 20 ) );
    cone->setFixedSize( 26, 32 );
    cone->setAlignment( Qt::AlignCenter );
    l->addWidget( cone );

    topTitle = new QLabel( topStrip );
    topTitle->setObjectName( "HeaderTitle" );
    topTitle->setAlignment( Qt::AlignCenter );
    l->addWidget( topTitle, 1 );

    QToolButton *leave = new QToolButton( topStrip );
    Theme::setupIconButton( leave, "win-restore", 32, 24 );
    leave->setFixedSize( Theme::WINDOW_BUTTON_W, 32 );
    leave->setToolTip( Theme::tooltipWithKey( p_intf, qtr( "Leave fullscreen" ),
                                              "key-leave-fullscreen" ) );
    connect( leave, &QToolButton::clicked, this, [this]() {
        /* the same path as the Esc hotkey */
        var_SetBool( THEPL, "fullscreen", false );
        vout_thread_t *p_vout = THEMIM->getVout();
        if( p_vout )
        {
            var_SetBool( p_vout, "fullscreen", false );
            vlc_object_release( p_vout );
        }
    } );
    QToolButton *close = new QToolButton( topStrip );
    Theme::setupIconButton( close, "win-close", 32, 24 );
    close->setFixedSize( Theme::WINDOW_BUTTON_W, 32 );
    close->setObjectName( "WinClose" );
    close->setToolTip( qtr( "Close" ) );
    connect( close, &QToolButton::clicked, this, [this]() {
        if( parentWidget() )
            parentWidget()->close();
    } );
    /* balance the cone so the title stays centred on the screen */
    l->insertSpacing( 1, 2 * Theme::WINDOW_BUTTON_W - 26 );
    l->addWidget( leave );
    l->addWidget( close );

    auto setTitle = [this]( const QString &name ) {
        topTitle->setText( name.isEmpty() ? qtr( "VLC Media Player" ) : name );
    };
    connect( THEMIM->getIM(), &InputManager::nameChanged, topStrip, setTitle );
    setTitle( QString() );
}

void FullscreenControllerWidget::placeTopStrip()
{
    if( !topStrip )
        return;
    const QRect sg = fsScreenGeometry();
    if( topStrip->windowHandle() && QtCompat::screenAt( sg.center() ) )
        topStrip->windowHandle()->setScreen( QtCompat::screenAt( sg.center() ) );
    topStrip->setGeometry( sg.x(), sg.y(), sg.width(), Theme::TITLEBAR_HEIGHT );
}

void FullscreenControllerWidget::fadeTo( qreal target )
{
    if( fadeAnim->state() == QAbstractAnimation::Running
        && qFuzzyCompare( fadeAnim->endValue().toReal(), target ) )
        return;
    fadeAnim->stop();
    fadeAnim->setStartValue( windowOpacity() );
    fadeAnim->setEndValue( target );
    fadeAnim->start();
}

void FullscreenControllerWidget::hideFSC()
{
    if( !Theme::redesign() )
    {
        hide();
        return;
    }
    p_hideTimer->stop();
    if( isVisible() )
    {
        msg_Dbg( p_intf, "fullscreen chrome: hide" );
        fadeTo( 0.0 );
    }
    else if( topStrip )
        topStrip->hide();
}

void FullscreenControllerWidget::setSuppressed( bool b )
{
    b_suppressed = b;
    if( b )
        hideFSC();
}

void FullscreenControllerWidget::centerFSC( int number )
{
    QRect currentRes = QGuiApplication::screens()[ number ]->geometry();

    /* screen has changed, calculate new position */
    QPoint pos = QPoint( currentRes.x() + (currentRes.width() / 2) - (width() / 2),
            currentRes.y() + currentRes.height() - height());
    move( pos );
}

/**
 * Show fullscreen controller
 */
void FullscreenControllerWidget::showFSC()
{
    if( Theme::redesign() )
    {
        restoreFSC();
        if( !isVisible() )
            setWindowOpacity( 0.0 );
        if( topStrip && !topStrip->isVisible() )
        {
            topStrip->setWindowOpacity( windowOpacity() );
            topStrip->show();
            topStrip->raise();
        }
        if( !isVisible() )
            msg_Dbg( p_intf, "fullscreen chrome: show" );
        show();
        raise();
        fadeTo( 1.0 );
        return;
    }
    restoreFSC();

#if HAVE_TRANSPARENCY
    setWindowOpacity( f_opacity );
#endif

    show();
}

/**
 * Plane to hide fullscreen controller
 */
void FullscreenControllerWidget::planHideFSC()
{
    vlc_mutex_lock( &lock );
    int i_timeout = i_hide_timeout;
    vlc_mutex_unlock( &lock );

    if( Theme::redesign() )
    {
        /* "Never" keeps the chrome up for as long as the video is fullscreen */
        if( var_InheritInteger( p_intf, "qt-autohide-controls" ) != 0 )
            p_hideTimer->start( qMax( i_timeout, 0 ) );
        return;
    }

    p_hideTimer->start( i_timeout );

#if HAVE_TRANSPARENCY
    b_slow_hide_begin = true;
    i_slow_hide_timeout = i_timeout;
    p_slowHideTimer->start( i_slow_hide_timeout / 2 );
#endif
}

/**
 * Hidding fullscreen controller slowly
 * Linux: need composite manager
 * Windows: it is blinking, so it can be enabled by define TRASPARENCY
 */
void FullscreenControllerWidget::slowHideFSC()
{
#if HAVE_TRANSPARENCY
    if( b_slow_hide_begin )
    {
        b_slow_hide_begin = false;

        p_slowHideTimer->stop();
        /* the last part of time divided to 100 pieces */
        p_slowHideTimer->start( (int)( i_slow_hide_timeout / 2 / ( windowOpacity() * 100 ) ) );

    }
    else
    {
         if ( windowOpacity() > 0.0 )
         {
             /* we should use 0.01 because of 100 pieces ^^^
                but than it cannt be done in time */
             setWindowOpacity( windowOpacity() - 0.02 );
         }

         if ( windowOpacity() <= 0.0 )
             p_slowHideTimer->stop();
    }
#endif
}

void FullscreenControllerWidget::updateFullwidthGeometry( int number )
{
    QRect screenGeometry = QGuiApplication::screens()[ number ]->geometry();
    setMinimumWidth( screenGeometry.width() );
    setGeometry( screenGeometry.x(), screenGeometry.y() + screenGeometry.height() - height(), screenGeometry.width(), height() );
    adjustSize();
}

void FullscreenControllerWidget::toggleFullwidth()
{
    /* Toggle isWideFSC switch */
    isWideFSC = !isWideFSC;

    restoreFSC();
}


void FullscreenControllerWidget::setTargetScreen(int screennumber)
{
    i_screennumber = screennumber;
}


int FullscreenControllerWidget::targetScreen()
{
    if( i_screennumber < 0 || i_screennumber >= QGuiApplication::screens().length() )
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        auto *screen = QGuiApplication::screenAt( p_intf->p_sys->p_mi->pos() );
        if (screen != nullptr)
        {
            for (qsizetype i = 0; i < QGuiApplication::screens().length(); i++)
            {
                if (screen == QGuiApplication::screens()[i])
                    return i;
            }
        }
        return -1;
#else
        return QApplication::desktop()->screenNumber( p_intf->p_sys->p_mi );
#endif
    }
    return i_screennumber;
}

/**
 * event handling
 * events: show, hide, start timer for hiding
 */
void FullscreenControllerWidget::customEvent( QEvent *event )
{
    bool b_fs;

    switch( (int)event->type() )
    {
        /* This is used when the 'i' hotkey is used, to force quick toggle */
        case IMEvent::FullscreenControlToggle:
            vlc_mutex_lock( &lock );
            b_fs = b_fullscreen;
            vlc_mutex_unlock( &lock );

            if( b_fs )
            {
                if( isHidden() || ( fadeAnim && fadeAnim->endValue().toReal() <= 0.0
                                    && fadeAnim->state() == QAbstractAnimation::Running ) )
                {
                    b_suppressed = false;
                    p_hideTimer->stop();
                    showFSC();
                    /* a peek: it goes away again like after a mouse move */
                    if( Theme::redesign() && !b_mouse_over )
                        planHideFSC();
                }
                else
                    hideFSC();
            }
            break;
        /* Event called to Show the FSC on mouseChanged() */
        case IMEvent::FullscreenControlShow:
            vlc_mutex_lock( &lock );
            b_fs = b_fullscreen;
            vlc_mutex_unlock( &lock );

            if( Theme::redesign() )
            {
                if( b_fs && !b_suppressed )
                    showFSC();
                break;
            }
            if( b_fs && ( isHidden()
#if HAVE_TRANSPARENCY
                 || p_slowHideTimer->isActive()
#endif
                    ) )
                showFSC();

            break;
        /* Start the timer to hide later, called usually with above case */
        case IMEvent::FullscreenControlPlanHide:
            if( !b_mouse_over ) // Only if the mouse is not over FSC
                planHideFSC();
            break;
        /* Hide */
        case IMEvent::FullscreenControlHide:
            hideFSC();
            break;
        default:
            break;
    }
}

bool FullscreenControllerWidget::eventFilter( QObject *watched, QEvent *event )
{
    const QWidget *wParent = parentWidget();
    Q_ASSERT( wParent );

    if ( watched == wParent && event->type() == QEvent::ActivationChange )
    {
        /* Hide if not active */
        if ( !wParent->isActiveWindow() && !isActiveWindow() )
            hideFSC();
    }
    if( topStrip && watched == topStrip )
    {
        /* the strip counts as part of the controller */
        switch( event->type() )
        {
        case QEvent::Enter:
            b_mouse_over = true;
            p_hideTimer->stop();
            break;
        case QEvent::Leave:
            b_mouse_over = false;
            planHideFSC();
            break;
        case QEvent::KeyPress:
            emit keyPressed( static_cast<QKeyEvent *>( event ) );
            return true;
        default:
            break;
        }
    }

    return AbstractController::eventFilter( watched, event );
}

/**
 * On mouse move
 * moving with FSC
 */
void FullscreenControllerWidget::mouseMoveEvent( QMouseEvent *event )
{
    if( event->buttons() == Qt::LeftButton )
    {
        if( i_mouse_last_x == -1 || i_mouse_last_y == -1 )
            return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const auto pos = event->globalPosition();
#else
        const auto pos = event->globalPos();
#endif
        int i_moveX = pos.x() - i_mouse_last_x;
        int i_moveY = pos.y() - i_mouse_last_y;

        const QRect screenRect = QGuiApplication::screens()[ targetScreen() ]->geometry();

        const int i_x = qBound( screenRect.left(), x() + i_moveX, screenRect.right() - width() );
        const int i_y = qBound( screenRect.top(),  y() + i_moveY, screenRect.bottom() - height() );

        move( i_x, i_y );

        i_mouse_last_x = pos.x();
        i_mouse_last_y = pos.y();
    }
}

/**
 * On mouse press
 * store position of cursor
 */
void FullscreenControllerWidget::mousePressEvent( QMouseEvent *event )
{
    if( isWideFSC ) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    auto pos = event->globalPosition();
#else
    auto pos = event->globalPos();
#endif
    i_mouse_last_x = pos.x();
    i_mouse_last_y = pos.y();
    event->accept();
}

void FullscreenControllerWidget::mouseReleaseEvent( QMouseEvent *event )
{
    if( isWideFSC ) return;
    i_mouse_last_x = -1;
    i_mouse_last_y = -1;
    event->accept();

    // Save the new FSC position
    previousPosition = pos();
}

/**
 * On mouse go above FSC
 */
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void FullscreenControllerWidget::enterEvent( QEnterEvent *event )
#else
void FullscreenControllerWidget::enterEvent( QEvent *event )
#endif
{
    b_mouse_over = true;

    p_hideTimer->stop();
#if HAVE_TRANSPARENCY
    if( !Theme::redesign() )
    {
        p_slowHideTimer->stop();
        setWindowOpacity( f_opacity );
    }
#endif
    event->accept();
}

/**
 * On mouse go out from FSC
 */
void FullscreenControllerWidget::leaveEvent( QEvent *event )
{
    planHideFSC();

    b_mouse_over = false;
    event->accept();
}

/**
 * When you get pressed key, send it to video output
 */
void FullscreenControllerWidget::keyPressEvent( QKeyEvent *event )
{
    emit keyPressed( event );
}

/* */
int FullscreenControllerWidget::FullscreenChanged( vlc_object_t *obj,
        const char *, vlc_value_t, vlc_value_t new_val, void *data )
{
    vout_thread_t *p_vout = (vout_thread_t *) obj;

    msg_Dbg( p_vout, "Qt: Fullscreen state changed" );
    FullscreenControllerWidget *p_fs = (FullscreenControllerWidget *)data;

    p_fs->fullscreenChanged( p_vout, new_val.b_bool, var_GetInteger( p_vout, "mouse-hide-timeout" ) );
    p_fs->emit fullscreenChanged( new_val.b_bool );

    return VLC_SUCCESS;
}
/* */
static int FullscreenControllerWidgetMouseMoved( vlc_object_t *obj,
        const char *, vlc_value_t, vlc_value_t new_val, void *data )
{
    vout_thread_t *p_vout = (vout_thread_t *) obj;
    FullscreenControllerWidget *p_fs = (FullscreenControllerWidget *)data;

    /* Get the value from the Vout - Trust the vout more than Qt */
    p_fs->mouseChanged( p_vout, new_val.coords.x, new_val.coords.y );

    return VLC_SUCCESS;
}

/**
 * It is call to update the list of vout handled by the fullscreen controller
 */
void FullscreenControllerWidget::setVoutList( vout_thread_t **pp_vout, int i_vout )
{
    QList<vout_thread_t*> del;
    QList<vout_thread_t*> add;

    QList<vout_thread_t*> set;

    /* */
    for( int i = 0; i < i_vout; i++ )
        set += pp_vout[i];

    /* Vout to remove */
    vlc_mutex_lock( &lock );
    foreach( vout_thread_t *p_vout, vout )
    {
        if( !set.contains( p_vout ) )
            del += p_vout;
    }
    vlc_mutex_unlock( &lock );

    foreach( vout_thread_t *p_vout, del )
    {
        var_DelCallback( p_vout, "fullscreen",
                         FullscreenControllerWidget::FullscreenChanged, this );
        vlc_mutex_lock( &lock );
        fullscreenChanged( p_vout, false, 0 );
        vout.removeAll( p_vout );
        vlc_mutex_unlock( &lock );

        vlc_object_release( VLC_OBJECT(p_vout) );
    }

    /* Vout to track */
    vlc_mutex_lock( &lock );
    foreach( vout_thread_t *p_vout, set )
    {
        if( !vout.contains( p_vout ) )
            add += p_vout;
    }
    vlc_mutex_unlock( &lock );

    foreach( vout_thread_t *p_vout, add )
    {
        vlc_object_hold( VLC_OBJECT(p_vout) );

        vlc_mutex_lock( &lock );
        vout.append( p_vout );
        var_AddCallback( p_vout, "fullscreen",
                         FullscreenControllerWidget::FullscreenChanged, this );
        /* I miss a add and fire */
        emit fullscreenChanged( p_vout, var_InheritBool( p_vout, "fullscreen" ),
                           var_GetInteger( p_vout, "mouse-hide-timeout" ) );
        vlc_mutex_unlock( &lock );
    }
}
/**
 * Register and unregister callback for mouse moving
 */
void FullscreenControllerWidget::fullscreenChanged( vout_thread_t *p_vout,
        bool b_fs, int i_timeout )
{
    /* FIXME - multiple vout (ie multiple mouse position ?) and thread safety if multiple vout ? */

    vlc_mutex_lock( &lock );
    /* Entering fullscreen, register callback */
    if( b_fs && !b_fullscreen )
    {
        msg_Dbg( p_vout, "Qt: Entering Fullscreen" );
        b_fullscreen = true;
        i_hide_timeout = i_timeout;
        var_AddCallback( p_vout, "mouse-moved",
                FullscreenControllerWidgetMouseMoved, this );
        if( Theme::redesign() && var_InheritInteger( p_intf, "qt-autohide-controls" ) == 0 )
            QApplication::postEvent( this, new IMEvent( IMEvent::FullscreenControlShow, 0 ) );
    }
    /* Quitting fullscreen, unregistering callback */
    else if( !b_fs && b_fullscreen )
    {
        msg_Dbg( p_vout, "Qt: Quitting Fullscreen" );
        b_fullscreen = false;
        b_suppressed = false;
        i_hide_timeout = i_timeout;
        var_DelCallback( p_vout, "mouse-moved",
                FullscreenControllerWidgetMouseMoved, this );

        /* Force fs hiding */
        IMEvent *eHide = new IMEvent( IMEvent::FullscreenControlHide, 0 );
        QApplication::postEvent( this, eHide );
    }
    vlc_mutex_unlock( &lock );
}

/**
 * Mouse change callback (show/hide the controller on mouse movement)
 */
void FullscreenControllerWidget::mouseChanged( vout_thread_t *, int i_mousex, int i_mousey )
{
    const QWidget *wParent = parentWidget();
    Q_ASSERT( wParent );

    /* Ignore mouse events if not active */
    if ( !wParent->isActiveWindow() &&  !isActiveWindow() ) return;

    bool b_toShow;

    /* FIXME - multiple vout (ie multiple mouse position ?) and thread safety if multiple vout ? */

    b_toShow = false;
    if( ( i_mouse_last_move_x == -1 || i_mouse_last_move_y == -1 ) ||
        ( abs( i_mouse_last_move_x - i_mousex ) > i_sensitivity ||
          abs( i_mouse_last_move_y - i_mousey ) > i_sensitivity ) )
    {
        i_mouse_last_move_x = i_mousex;
        i_mouse_last_move_y = i_mousey;
        b_toShow = true;
    }

    if( b_toShow )
    {
        /* Show event */
        IMEvent *eShow = new IMEvent( IMEvent::FullscreenControlShow, 0 );
        QApplication::postEvent( this, eShow );

        /* Plan hide event */
        IMEvent *eHide = new IMEvent( IMEvent::FullscreenControlPlanHide, 0 );
        QApplication::postEvent( this, eHide );
    }
}

