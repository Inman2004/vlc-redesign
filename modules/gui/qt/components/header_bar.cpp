/*****************************************************************************
 * header_bar.cpp : redesigned window header (title bar)
 *****************************************************************************
 * Copyright (C) 2026 the VideoLAN team
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "components/header_bar.hpp"
#include "util/qt_compat.hpp"
#include "styles/vlc_theme.hpp"
#include "main_interface.hpp"
#include "input_manager.hpp"
#include "actions_manager.hpp"
#include "menus.hpp"
#include "components/redesign_menus.hpp"

#include <vlc_input.h>
#include <vlc_url.h>

#include <QPainter>
#include <QSvgRenderer>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QToolButton>
#include <QUrl>
#include <QWindow>

static QToolButton *headerButton( QWidget *parent, const QString &icon,
                                  const QString &tip, int w = 32 )
{
    QToolButton *b = new QToolButton( parent );
    Theme::setupIconButton( b, icon, 32, 24 );
    b->setFixedSize( w, 32 );
    b->setToolTip( tip );
    return b;
}

HeaderBar::HeaderBar( intf_thread_t *_p_intf, MainInterface *_mi )
         : QWidget( _mi ), p_intf( _p_intf ), mi( _mi )
{
    setObjectName( "VLCHeader" );
    setAttribute( Qt::WA_StyledBackground, true );
    setFixedHeight( Theme::TITLEBAR_HEIGHT );

    QHBoxLayout *layout = new QHBoxLayout( this );
    layout->setContentsMargins( 12, 0, 6, 0 );
    layout->setSpacing( 0 );

    /* Left group */
    leftGroup = new QWidget( this );
    QHBoxLayout *left = new QHBoxLayout( leftGroup );
    left->setContentsMargins( 0, 0, 0, 0 );
    left->setSpacing( 6 );

    QLabel *cone = new QLabel( leftGroup );
    {
        /* Figma's cone (Icon "vlc"), drawn in its own colours */
        const qreal dpr = devicePixelRatioF();
        QImage img( QSize( 24, 24 ) * dpr, QImage::Format_ARGB32_Premultiplied );
        img.fill( Qt::transparent );
        QPainter p( &img );
        QSvgRenderer( QString( ":/redesign/vlc.svg" ) ).render( &p, QRectF( 0, 0, img.width(), img.height() ) );
        p.end();
        QPixmap conePix = QPixmap::fromImage( img );
        conePix.setDevicePixelRatio( dpr );
        cone->setPixmap( conePix );
    }
    cone->setFixedSize( 26, 32 );
    cone->setAlignment( Qt::AlignCenter );
    left->addWidget( cone );
    left->addSpacing( 5 );

    menuBtn = headerButton( leftGroup, "dots", qtr( "Menu" ) );
    connect( menuBtn, &QToolButton::clicked, this, &HeaderBar::showMainMenu );
    left->addWidget( menuBtn );

    sidebarBtn = headerButton( leftGroup, "sidebar", qtr( "Show or hide the library" ) );
    sidebarBtn->setCheckable( true );
    {
        /* the design keeps this glyph grey in both states; the panel in
         * the glyph is wide while the sidebar is open (Figma "sidebar"
         * active / left) */
        QIcon ic;
        const QColor n = Theme::colors().icon, h = Theme::colors().iconHover;
        ic.addPixmap( Theme::pixmap( "sidebar-left", 24, n ), QIcon::Normal, QIcon::Off );
        ic.addPixmap( Theme::pixmap( "sidebar", 24, n ), QIcon::Normal, QIcon::On );
        ic.addPixmap( Theme::pixmap( "sidebar-left", 24, h ), QIcon::Active, QIcon::Off );
        ic.addPixmap( Theme::pixmap( "sidebar", 24, h ), QIcon::Active, QIcon::On );
        sidebarBtn->setIcon( ic );
    }
    connect( sidebarBtn, &QToolButton::toggled, this, &HeaderBar::sidebarToggled );
    left->addWidget( sidebarBtn );

    divider = new QFrame( leftGroup );
    divider->setObjectName( "HeaderDivider" );
    divider->setFixedSize( 1, 18 );
    left->addSpacing( 4 );
    left->addWidget( divider );
    left->addSpacing( 6 );

    folderIcon = new QLabel( leftGroup );
    folderIcon->setPixmap( Theme::pixmap( "folder", 20, Theme::colors().icon ) );
    left->addWidget( folderIcon );
    left->addSpacing( 6 );
    crumb = new QLabel( leftGroup );
    crumb->setObjectName( "HeaderCrumb" );
    left->addWidget( crumb );
    folderIcon->hide();
    crumb->hide();

    layout->addWidget( leftGroup );
    layout->addStretch( 1 );

    /* Right group: window buttons */
    rightGroup = new QWidget( this );
    QHBoxLayout *right = new QHBoxLayout( rightGroup );
    right->setContentsMargins( 0, 0, 0, 0 );
    right->setSpacing( 0 );

    minBtn   = headerButton( rightGroup, "win-min", qtr( "Minimize" ), Theme::WINDOW_BUTTON_W );
    maxBtn   = headerButton( rightGroup, "win-max", qtr( "Maximize" ), Theme::WINDOW_BUTTON_W );
    fsBtn    = headerButton( rightGroup, "win-fullscreen", qtr( "Fullscreen" ), Theme::WINDOW_BUTTON_W );
    closeBtn = headerButton( rightGroup, "win-close", qtr( "Close" ), Theme::WINDOW_BUTTON_W );
    closeBtn->setObjectName( "WinClose" );

    connect( minBtn, &QToolButton::clicked, mi, &QWidget::showMinimized );
    connect( maxBtn, &QToolButton::clicked, this, &HeaderBar::toggleMaximized );
    connect( fsBtn, &QToolButton::clicked, this, [this]() {
        if( THEMIM->getIM()->hasVideo() )
            ActionsManager::getInstance( p_intf )->doAction( FULLSCREEN_ACTION );
        else
            mi->toggleInterfaceFullScreen();
    } );
    connect( closeBtn, &QToolButton::clicked, mi, &QWidget::close );

    right->addWidget( minBtn );
    right->addWidget( maxBtn );
    right->addWidget( fsBtn );
    right->addWidget( closeBtn );
    if( !Theme::frameless() )
    {
        /* the system frame already has these */
        minBtn->hide();
        maxBtn->hide();
        closeBtn->hide();
    }
    layout->addWidget( rightGroup );

    /* Centred title, placed by hand so it stays centred on the window
     * whatever the width of the side groups */
    title = new QLabel( this );
    title->setObjectName( "HeaderTitle" );
    title->setAlignment( Qt::AlignCenter );
    title->setAttribute( Qt::WA_TransparentForMouseEvents );
    setTitle( QString() );

    connect( THEMIM->getIM(), &InputManager::nameChanged, this, &HeaderBar::setTitle );
    connect( THEMIM, &MainInputManager::inputChanged, this, &HeaderBar::updateCrumb );

    mi->installEventFilter( this );
}

void HeaderBar::setTitle( const QString &name )
{
    /* Figma music mode titles the window with the song ("untangle"), not
     * VLC's "artist - title" */
    QString text = name;
    input_thread_t *p_input = THEMIM->getInput();
    input_item_t *item = p_input ? input_GetItem( p_input ) : NULL;
    if( item )
    {
        char *t = input_item_GetTitle( item );
        char *a = input_item_GetArtist( item );
        if( t && *t && a && *a )
            text = qfu( t );
        free( t );
        free( a );
    }
    title->setText( text.isEmpty() ? qtr( "VLC Media Player" ) : text );
    placeTitle();
}

/* The folder of the current media, shown as the entry point to the
 * library ("the name of the show"). */
void HeaderBar::updateCrumb()
{
    QString folder;
    input_thread_t *p_input = THEMIM->getInput();
    if( p_input )
    {
        input_item_t *item = input_GetItem( p_input );
        char *uri = item ? input_item_GetURI( item ) : NULL;
        if( uri )
        {
            char *path = vlc_uri2path( uri );
            if( path )
            {
                folder = QFileInfo( qfu( path ) ).dir().dirName();
                free( path );
            }
            free( uri );
        }
    }
    crumb->setText( folder );
    crumb->setVisible( !folder.isEmpty() && !libraryMode );
    folderIcon->setVisible( !folder.isEmpty() && !libraryMode );
    placeTitle();
}

void HeaderBar::setLibraryMode( bool on )
{
    libraryMode = on;
    sidebarBtn->setVisible( !on );
    divider->setVisible( !on );
    updateCrumb();
}

void HeaderBar::showMainMenu()
{
    /* The single designed menu; the classic eight stay reachable through
     * the right-click menu and keep their shortcuts in the hidden menu bar */
    QMenu *menu = RedesignMenus::mainMenu( p_intf, this );
    menu->exec( menuBtn->mapToGlobal( QPoint( 0, menuBtn->height() + 4 ) ) );
    menu->deleteLater();
}

void HeaderBar::toggleMaximized()
{
    if( mi->isMaximized() )
        mi->showNormal();
    else
        mi->showMaximized();
}

void HeaderBar::updateWindowButtons()
{
    const bool max = mi->isMaximized();
    maxBtn->setIcon( Theme::icon( max ? "win-restore" : "win-max", 24 ) );
    maxBtn->setToolTip( max ? qtr( "Restore" ) : qtr( "Maximize" ) );
}

void HeaderBar::placeTitle()
{
    const int avail = width() - 2 * qMax( leftGroup->geometry().right(),
                                          width() - rightGroup->geometry().left() ) - 24;
    title->setVisible( avail > 60 );
    const QString text = title->fontMetrics().elidedText(
                title->text(), Qt::ElideMiddle, qMax( avail, 0 ) );
    if( text != title->text() )
        title->setToolTip( title->text() );
    const int w = qMax( avail, 0 );
    title->setGeometry( ( width() - w ) / 2, 0, w, height() );
}

void HeaderBar::resizeEvent( QResizeEvent *e )
{
    QWidget::resizeEvent( e );
    placeTitle();
}

bool HeaderBar::eventFilter( QObject *obj, QEvent *e )
{
    if( obj == mi && e->type() == QEvent::WindowStateChange )
        updateWindowButtons();
    return QWidget::eventFilter( obj, e );
}

void HeaderBar::mousePressEvent( QMouseEvent *e )
{
    if( e->button() == Qt::LeftButton && Theme::frameless() && mi->windowHandle()
        && !mi->isFullScreen() )
    {
        QtCompat::startSystemMove( mi );
        e->accept();
        return;
    }
    QWidget::mousePressEvent( e );
}

void HeaderBar::mouseDoubleClickEvent( QMouseEvent *e )
{
    if( e->button() == Qt::LeftButton && !mi->isFullScreen() )
    {
        toggleMaximized();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent( e );
}

/*********************************************************************
 * Frameless edge resizing
 *********************************************************************/
FramelessResizer::FramelessResizer( QWidget *_window, int _border )
    : QObject( _window ), window( _window ), border( _border ), cursorOverridden( false )
{
    qApp->installEventFilter( this );
}

Qt::Edges FramelessResizer::edgesAt( const QPoint &g ) const
{
    Qt::Edges edges;
    if( window->isMaximized() || window->isFullScreen() )
        return edges;
    const QRect r = window->frameGeometry();
    if( !r.adjusted( -1, -1, 1, 1 ).contains( g ) )
        return edges;
    if( g.x() <= r.left() + border )   edges |= Qt::LeftEdge;
    if( g.x() >= r.right() - border )  edges |= Qt::RightEdge;
    if( g.y() <= r.top() + border )    edges |= Qt::TopEdge;
    if( g.y() >= r.bottom() - border ) edges |= Qt::BottomEdge;
    return edges;
}

void FramelessResizer::setEdgeCursor( Qt::Edges edges )
{
    Qt::CursorShape shape = Qt::ArrowCursor;
    if( edges == ( Qt::LeftEdge | Qt::TopEdge ) || edges == ( Qt::RightEdge | Qt::BottomEdge ) )
        shape = Qt::SizeFDiagCursor;
    else if( edges == ( Qt::RightEdge | Qt::TopEdge ) || edges == ( Qt::LeftEdge | Qt::BottomEdge ) )
        shape = Qt::SizeBDiagCursor;
    else if( edges & ( Qt::LeftEdge | Qt::RightEdge ) )
        shape = Qt::SizeHorCursor;
    else if( edges & ( Qt::TopEdge | Qt::BottomEdge ) )
        shape = Qt::SizeVerCursor;

    if( edges && !cursorOverridden )
    {
        QApplication::setOverrideCursor( shape );
        cursorOverridden = true;
    }
    else if( edges && cursorOverridden )
        QApplication::changeOverrideCursor( shape );
    else if( !edges && cursorOverridden )
    {
        QApplication::restoreOverrideCursor();
        cursorOverridden = false;
    }
}

bool FramelessResizer::eventFilter( QObject *obj, QEvent *e )
{
    if( e->type() != QEvent::MouseMove && e->type() != QEvent::MouseButtonPress
        && e->type() != QEvent::HoverMove )
        return false;
    QWidget *w = qobject_cast<QWidget *>( obj );
    if( !w || w->window() != window )
        return false;

    const QPoint g = QCursor::pos();
    const Qt::Edges edges = edgesAt( g );

    if( e->type() == QEvent::MouseButtonPress )
    {
        QMouseEvent *me = static_cast<QMouseEvent *>( e );
        if( edges && me->button() == Qt::LeftButton && window->windowHandle() )
        {
            QtCompat::startSystemResize( window, edges );
            return true;
        }
        return false;
    }
    if( QApplication::mouseButtons() == Qt::NoButton )
        setEdgeCursor( edges );
    return false;
}
