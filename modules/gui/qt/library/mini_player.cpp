/*****************************************************************************
 * mini_player.cpp : the library window's mini player (P5a)
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

#include "library/mini_player.hpp"
#include "components/redesign_menus.hpp"
#include "styles/vlc_theme.hpp"
#include "components/queue_panel.hpp"
#include "input_manager.hpp"
#include "dialogs_provider.hpp"

#include <vlc_input.h>
#include <vlc_url.h>

#include <QContextMenuEvent>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStackedLayout>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QApplication>
#include <QShowEvent>

/* Figma #3, logical px, measured on the canvas (x1.4327) */
enum
{
    PAD          = 8,    /* card edge to the picture and the progress */
    PICTURE_H    = 210,  /* 374 x 210, 16:9 */
    PROGRESS_Y   = 223,
    PROGRESS_H   = 16,
    TEXT_INSET   = 17,   /* title and chips */
    TITLE_CAP_Y  = 265,  /* top of the title's capitals */
    CHIPS_MID_Y  = 309,  /* centre line of the chip row */
    QUEUE_RIGHT  = 14,   /* card edge to the queue button */
    /* the queue variant (Components "Miniplayer", canvas x1.43): the card
     * grows 350 upward; a 56px #151515 "Queue" bar, the count row centred
     * at 89, 44px rows from 117; handle 43, name 70, duration 32 from the
     * right; sort / - / + centred at 262, 307, 350 */
    QUEUE_H      = 350,
    QUEUE_TITLE_H = 56,
    QUEUE_HEAD_MID = 89,
    QUEUE_ROWS_Y = 117,
};

/* the thin seek bar under the picture: #090909 track, seek fill, click
 * or drag to seek */
class MiniProgress : public QWidget
{
public:
    MiniProgress( intf_thread_t *_p_intf, QWidget *parent )
        : QWidget( parent ), p_intf( _p_intf )
    {
        setFixedHeight( PROGRESS_H );
        setCursor( Qt::PointingHandCursor );
    }
    void setPosition( float p )
    {
        if( dragging )
            return;
        pos = p;
        update();
    }
protected:
    void paintEvent( QPaintEvent * ) override
    {
        QPainter p( this );
        p.setRenderHint( QPainter::Antialiasing );
        const QRectF r = QRectF( rect() ).adjusted( 0.5, 0.5, -0.5, -0.5 );
        p.setPen( QColor( 0x18, 0x18, 0x18 ) );
        p.setBrush( QColor( 0x09, 0x09, 0x09 ) );
        p.drawRoundedRect( r, 4, 4 );
        if( pos > 0 )
        {
            QPainterPath clip;
            clip.addRoundedRect( r, 4, 4 );
            p.setClipPath( clip );
            p.fillRect( QRectF( r.left(), r.top(), r.width() * qBound( 0.f, pos, 1.f ), r.height() ),
                        Theme::seekFill() );
        }
    }
    void mousePressEvent( QMouseEvent *e ) override { dragging = true; seek( e->x() ); }
    void mouseMoveEvent( QMouseEvent *e ) override { if( dragging ) seek( e->x() ); }
    void mouseReleaseEvent( QMouseEvent *e ) override
    {
        seek( e->x() );
        dragging = false;
    }
private:
    void seek( int x )
    {
        pos = qBound( 0.f, float( x ) / qMax( 1, width() ), 1.f );
        update();
        THEMIM->getIM()->sliderUpdate( pos );
    }
    intf_thread_t *p_intf;
    float pos = 0;
    bool dragging = false;
};

/* the dimmed layer: rgba(0,0,0,0.55) with the picture's radius */
class MiniOverlay : public QWidget
{
public:
    MiniOverlay( QWidget *owner )
        : QWidget( owner, Qt::Tool | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint )
    {
        setAttribute( Qt::WA_TranslucentBackground );
        setAttribute( Qt::WA_ShowWithoutActivating );
        setObjectName( "MiniOverlay" );
    }
protected:
    void paintEvent( QPaintEvent * ) override
    {
        QPainter p( this );
        p.setRenderHint( QPainter::Antialiasing );
        p.setCompositionMode( QPainter::CompositionMode_Source );
        p.fillRect( rect(), Qt::transparent );
        p.setCompositionMode( QPainter::CompositionMode_SourceOver );
        p.setPen( Qt::NoPen );
        p.setBrush( QColor( 0, 0, 0, 140 ) );
        p.drawRoundedRect( QRectF( rect() ), 4, 4 );
    }
};

static QToolButton *overlayButton( QWidget *parent, const QString &icon, int glyph,
                                   const QString &tip )
{
    QToolButton *b = new QToolButton( parent );
    b->setObjectName( "MiniOverlayButton" );
    b->setIcon( QIcon( Theme::pixmap( icon, glyph, Qt::white ) ) );
    b->setIconSize( QSize( glyph, glyph ) );
    b->setFixedSize( glyph + 16, glyph + 16 );
    b->setToolTip( tip );
    b->setCursor( Qt::PointingHandCursor );
    return b;
}

MiniPlayer::MiniPlayer( intf_thread_t *_p_intf, QWidget *parent )
    : QFrame( parent ), p_intf( _p_intf )
{
    setObjectName( "MiniPlayer" );
    setAttribute( Qt::WA_StyledBackground, true );
    setFixedSize( CARD_W, CARD_H );

    /* picture: the art, or the borrowed video */
    picture = new QWidget( this );
    picture->setObjectName( "MiniPicture" );
    picture->setAttribute( Qt::WA_StyledBackground, true );
    /* placed by layoutBody() */
    pictureStack = new QStackedLayout( picture );
    pictureStack->setStackingMode( QStackedLayout::StackOne );
    art = new QLabel( picture );
    art->setAlignment( Qt::AlignCenter );
    pictureStack->addWidget( art );
    videoHost = new QWidget( picture );
    videoLayout = new QVBoxLayout( videoHost );
    videoLayout->setContentsMargins( 1, 1, 1, 1 );
    pictureStack->addWidget( videoHost );

    progress = new MiniProgress( p_intf, this );


    /* placed by its capitals in layoutText(), once the style sheet font
     * is known */
    title = new QLabel( this );
    title->setObjectName( "MiniTitle" );
    title->setAlignment( Qt::AlignLeft | Qt::AlignTop );

    chipRow = new QWidget( this );
    QHBoxLayout *chips = new QHBoxLayout( chipRow );
    chips->setContentsMargins( 0, 0, 0, 0 );
    chips->setSpacing( 0 );
    QLabel *folderIcon = new QLabel( this );
    folderIcon->setPixmap( Theme::pixmap( "folder", 20, Theme::colors().icon ) );
    folderChip = new QLabel( this );
    folderChip->setObjectName( "MiniChip" );
    QLabel *sourceIcon = new QLabel( this );
    sourceIcon->setPixmap( Theme::pixmap( "artist", 20, Theme::colors().icon ) );
    sourceChip = new QLabel( this );
    sourceChip->setObjectName( "MiniChip" );
    QToolButton *queue = new QToolButton( this );
    Theme::setupIconButton( queue, "lists", 32, 20 );
    queue->setToolTip( qtr( "Queue" ) );
    queue->setCheckable( true );    /* accent while the queue is open (Figma) */
    queueBtn = queue;
    connect( queue, &QToolButton::clicked, this, [this]() { setQueueOpen( !queueOpen ); } );
    /* glyph to text 16, text to the next glyph 30 (Figma) */
    chips->addWidget( folderIcon );
    chips->addSpacing( 13 );
    chips->addWidget( folderChip );
    chips->addSpacing( 26 );
    chips->addWidget( sourceIcon );
    chips->addSpacing( 13 );
    chips->addWidget( sourceChip );
    chips->addStretch( 1 );
    chips->addWidget( queue );
    layoutBody();

    /* Hover controls over the picture (Components: dimmed video, close
     * top left, expand top right, previous / play / next). The video is a
     * native window, so the dimmed layer is a translucent top-level window
     * over it, like the fullscreen controller. */
    overlay = new MiniOverlay( this );
    QToolButton *expand = overlayButton( overlay, "popout", 18, qtr( "Back to the player" ) );
    QToolButton *close = overlayButton( overlay, "win-close", 18, qtr( "Stop" ) );
    QToolButton *prev = overlayButton( overlay, "prev", 24, qtr( "Previous" ) );
    playBtn = overlayButton( overlay, "pause", 32, qtr( "Play/Pause" ) );
    QToolButton *next = overlayButton( overlay, "next", 24, qtr( "Next" ) );
    connect( expand, &QToolButton::clicked, this, &MiniPlayer::expandRequested );
    connect( close, &QToolButton::clicked, THEMIM, &MainInputManager::stop );
    connect( prev, &QToolButton::clicked, THEMIM, &MainInputManager::prev );
    connect( playBtn, &QToolButton::clicked, THEMIM, &MainInputManager::togglePlayPause );
    connect( next, &QToolButton::clicked, THEMIM, &MainInputManager::next );
    QVBoxLayout *ov = new QVBoxLayout( overlay );
    ov->setContentsMargins( 4, 4, 4, 4 );
    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget( close );
    top->addStretch( 1 );
    top->addWidget( expand );
    ov->addLayout( top );
    ov->addStretch( 1 );
    QHBoxLayout *mid = new QHBoxLayout;
    mid->setSpacing( 14 );
    mid->addStretch( 1 );
    mid->addWidget( prev );
    mid->addWidget( playBtn );
    mid->addWidget( next );
    mid->addStretch( 1 );
    ov->addLayout( mid );
    ov->addStretch( 1 );
    ov->addSpacing( 28 );
    overlay->hide();

    /* Qt sees no mouse events over the native video: poll the cursor
     * while the card is shown */
    hoverPoll.setInterval( 80 );
    connect( &hoverPoll, &QTimer::timeout, this, [this]() {
        const QRect r( picture->mapToGlobal( QPoint( 0, 0 ) ), picture->size() );
        const bool active = window()->isActiveWindow() || overlay->isActiveWindow();
        setOverlayVisible( isVisible() && active && r.contains( QCursor::pos() )
                           && !qApp->activePopupWidget() );
    } );
    overlay->installEventFilter( this );

    InputManager *im = THEMIM->getIM();
    connect( im, &InputManager::positionUpdated, this, [this]( float pos, int64_t, int ) {
        progress->setPosition( pos < 0 ? 0 : pos );
    } );
    connect( im, &InputManager::nameChanged, this, [this]( const QString &name ) {
        title->setText( title->fontMetrics().elidedText( name, Qt::ElideMiddle,
                                                         CARD_W - 2 * TEXT_INSET ) );
    } );
    connect( im, &InputManager::playingStatusChanged, this, [this]( int status ) {
        playBtn->setIcon( QIcon( Theme::pixmap( status == PLAYING_S ? "pause" : "play", 32, Qt::white ) ) );
    } );
    connect( im, QOverload<QString>::of( &InputManager::artChanged ), this, &MiniPlayer::updateArt );
    connect( THEMIM, &MainInputManager::inputChanged, this, &MiniPlayer::updateInput );
    updateInput();
}

void MiniPlayer::setVideo( QWidget *w )
{
    if( !w || w == videoWidget )
        return;
    videoWidget = w;
    w->setParent( videoHost );
    videoLayout->addWidget( w );
    w->show();
    pictureStack->setCurrentWidget( videoHost );
    /* The video window is native, so Qt makes native windows for this
     * card and its siblings in creation order, which leaves the library
     * content pane above the card in the X stack: its painted pixels then
     * hide the video. Restack the card on top once those windows exist.
     * raise() alone is not enough: it skips the X restack when the widget
     * is already last among its siblings, and it touches the X window only
     * for widgets marked native (not implicitly native ancestors). */
    setAttribute( Qt::WA_NativeWindow );
    QTimer::singleShot( 0, this, [this]() { lower(); raise(); } );
}

QWidget *MiniPlayer::takeVideo()
{
    QWidget *w = videoWidget;
    if( w )
        videoLayout->removeWidget( w );
    videoWidget = NULL;
    pictureStack->setCurrentWidget( art );
    return w;
}

void MiniPlayer::setDocked( bool d )
{
    if( docked == d )
        return;
    docked = d;
    emit dockChanged( d );
}

void MiniPlayer::setSourceLabel( const QString &s )
{
    sourceChip->setText( s );
}

void MiniPlayer::updateInput()
{
    QString folder;
    input_thread_t *p_input = THEMIM->getInput();
    input_item_t *item = p_input ? input_GetItem( p_input ) : NULL;
    char *uri = item ? input_item_GetURI( item ) : NULL;
    char *path = uri ? vlc_uri2path( uri ) : NULL;
    if( path )
        folder = QFileInfo( qfu( path ) ).dir().dirName();
    free( path );
    free( uri );
    folderChip->setText( folder );
    title->setText( title->fontMetrics().elidedText( THEMIM->getIM()->getName(),
                                                     Qt::ElideMiddle, CARD_W - 2 * TEXT_INSET ) );
    if( sourceChip->text().isEmpty() )
        sourceChip->setText( qtr( "Queue" ) );
    updateArt();
}

void MiniPlayer::updateArt()
{
    /* audio, or before the video starts: the cover, or the cone */
    /* decodeArtURL asserts an item: nothing is playing at startup */
    input_thread_t *p_input = THEMIM->getInput();
    input_item_t *item = p_input ? input_GetItem( p_input ) : NULL;
    const QString url = item ? InputManager::decodeArtURL( item ) : QString();
    QPixmap pm;
    if( !url.isEmpty() )
        pm = QPixmap( url );
    if( pm.isNull() )
        pm = QIcon( ":/logo/vlc128.png" ).pixmap( 72, 72 );
    else
        pm = pm.scaled( QSize( CARD_W, PICTURE_H ) * devicePixelRatioF(), Qt::KeepAspectRatio,
                        Qt::SmoothTransformation );
    pm.setDevicePixelRatio( devicePixelRatioF() );
    art->setPixmap( pm );
}

void MiniPlayer::placeOverlay()
{
    overlay->setGeometry( QRect( picture->mapToGlobal( QPoint( 0, 0 ) ), picture->size() ) );
}

void MiniPlayer::layoutText()
{
    title->ensurePolished();
    const QFontMetrics fm( title->font() );
    const int top = queueOpen ? int( QUEUE_H ) : 0;
    title->setGeometry( TEXT_INSET, top + TITLE_CAP_Y - ( fm.ascent() - fm.capHeight() ),
                        CARD_W - 2 * TEXT_INSET, fm.height() );
}

/* the card's own parts, under the queue when it is open */
void MiniPlayer::layoutBody()
{
    const int top = queueOpen ? int( QUEUE_H ) : 0;
    setFixedSize( CARD_W, CARD_H + top );
    picture->setGeometry( PAD, top + PAD, CARD_W - 2 * PAD, PICTURE_H );
    progress->setGeometry( PAD, top + PROGRESS_Y, CARD_W - 2 * PAD, PROGRESS_H );
    chipRow->setGeometry( TEXT_INSET, top + CHIPS_MID_Y - 16,
                          CARD_W - TEXT_INSET - QUEUE_RIGHT, 32 );
    layoutText();
    if( overlay && overlay->isVisible() )
        placeOverlay();
}

QWidget *MiniPlayer::buildQueueBox()
{
    QWidget *box = new QWidget( this );
    box->setGeometry( 0, 0, CARD_W, QUEUE_H );

    QLabel *bar = new QLabel( qtr( "Queue" ), box );
    bar->setObjectName( "MiniQueueTitle" );
    bar->setAlignment( Qt::AlignCenter );
    bar->setGeometry( 0, 0, CARD_W, QUEUE_TITLE_H );

    queueCount = new QLabel( box );
    queueCount->setObjectName( "MiniQueueCount" );
    queueCount->setGeometry( 36, QUEUE_HEAD_MID - 12, 200, 24 );
    struct { const char *icon; int centre; const char *tip; } buttons[] = {
        { "sort", 262, N_( "Sort" ) }, { "remove", 307, N_( "Remove from the queue" ) },
        { "add", 350, N_( "Add to the queue" ) },
    };
    QToolButton *b[3];
    for( int i = 0; i < 3; i++ )
    {
        b[i] = new QToolButton( box );
        Theme::setupIconButton( b[i], buttons[i].icon, 32, 24 );
        b[i]->setToolTip( qtr( buttons[i].tip ) );
        b[i]->move( buttons[i].centre - 16, QUEUE_HEAD_MID - 16 );
    }

    queueList = new QueueList( p_intf, box, 43, 70, 32 );
    queueList->setGeometry( 0, QUEUE_ROWS_Y, CARD_W, QUEUE_H - QUEUE_ROWS_Y - PAD );
    connect( b[0], &QToolButton::clicked, this, [this, b]() {
        queueList->showSortMenu( b[0]->mapToGlobal( QPoint( 0, b[0]->height() + 4 ) ) );
    } );
    connect( b[1], &QToolButton::clicked, queueList, &QueueList::removeSelected );
    connect( b[2], &QToolButton::clicked, this, [this]() { THEDP->PLAppendDialog(); } );
    connect( queueList, &QueueList::changed, this, [this]() {
        queueCount->setText( QueueList::summary( p_intf ) );
    } );
    queueCount->setText( QueueList::summary( p_intf ) );
    return box;
}

void MiniPlayer::setQueueOpen( bool b )
{
    if( b == queueOpen )
        return;
    queueOpen = b;
    if( b && !queueBox )
        queueBox = buildQueueBox();
    if( queueBox )
        queueBox->setVisible( b );
    queueBtn->setChecked( b );
    layoutBody();
    emit sizeChanged();
}

void MiniPlayer::showEvent( QShowEvent *e )
{
    QFrame::showEvent( e );
    layoutText();
    hoverPoll.start();
}

void MiniPlayer::hideEvent( QHideEvent *e )
{
    QFrame::hideEvent( e );
    hoverPoll.stop();
    setOverlayVisible( false );
}

void MiniPlayer::setOverlayVisible( bool on )
{
    if( on == overlay->isVisible() )
        return;
    if( on )
    {
        placeOverlay();
        overlay->show();
        overlay->raise();
    }
    else
        overlay->hide();
}

bool MiniPlayer::eventFilter( QObject *obj, QEvent *e )
{
    switch( e->type() )
    {
    case QEvent::MouseButtonDblClick:
        if( obj == overlay )
        {
            emit expandRequested();
            return true;
        }
        break;
    default:
        break;
    }
    return QFrame::eventFilter( obj, e );
}

void MiniPlayer::contextMenuEvent( QContextMenuEvent *e )
{
    QMenu menu( this );
    menu.addAction( RedesignMenus::menuIcon( "popout", 20 ), qtr( "Back to the Player" ),
                    this, &MiniPlayer::expandRequested );
    menu.addAction( RedesignMenus::menuIcon( "sidebar", 20 ),
                    docked ? qtr( "Float in the Corner" ) : qtr( "Dock Under the Menu" ),
                    this, [this]() { setDocked( !docked ); } );
    menu.exec( e->globalPos() );
}
