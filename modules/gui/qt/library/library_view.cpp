/*****************************************************************************
 * library_view.cpp : library window state, browse view (P5a)
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

#include "library/library_view.hpp"
#include "library/library_browser.hpp"
#include "library/media_library.hpp"
#include "library/mini_player.hpp"
#include "input_manager.hpp"
#include "components/redesign_menus.hpp"
#include "styles/vlc_theme.hpp"
#include "dialogs_provider.hpp"
#include "recents.hpp"

#include <vlc_playlist.h>

#include <QCollator>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

/* Figma row 2 #3-6, canvas x 1.4327 = logical px at 1920 */
enum
{
    NAV_W          = 200,
    PAGE_INSET     = 7,     /* nav panel to window edge */
    TOOLBAR_H      = 70,
    GRID_MARGIN    = 29,    /* nav edge to the first tile */
    GRID_MARGIN_R  = 23,
    GRID_TOP       = 19,    /* divider to the first tile */
    TILE_W         = 315,   /* target; tiles stretch to fill the row */
    TILE_GAP       = 20,
    TILE_LABEL_H   = 78,    /* tile bottom to the next row */
    TILE_RADIUS    = 4,
    UNDERLINE_H    = 3,
    UNDERLINE_INSET = 4,
    LIST_COLUMNS   = 4,
    LIST_ROW_H     = 42,
    LIST_THUMB     = 32,
    LIST_TOP       = 25,
};

static const QColor LABEL      ( 0x96, 0x96, 0x96 );
static const QColor LABEL_DIM  ( 0x6B, 0x6B, 0x6B );
static const QColor TILE_EMPTY ( 0x00, 0x00, 0x00 );
static const QColor DIVIDER    ( 0x18, 0x18, 0x18 );

/* Components sidebar panels (S01 list, Videos, Network): 42px rows, a
 * 32px square thumbnail 4px in or a 20px glyph 10px in, the name 46px in */
enum
{
    SIDE_ROW_H     = 42,
    SIDE_THUMB_X   = 4,
    SIDE_ICON_X    = 10,
    SIDE_ICON      = 20,
    SIDE_TEXT_X    = 46,
    SIDE_TILE_W    = 160,
    SIDE_TILE_GAP  = 12,
    SIDE_LABEL_H   = 44,
};

/**********************************************************************
 * LibraryGrid
 **********************************************************************/
LibraryGrid::LibraryGrid( intf_thread_t *_p_intf, QWidget *parent )
    : QAbstractScrollArea( parent ), p_intf( _p_intf ),
      lib( MediaLibrary::instance( _p_intf ) )
{
    setObjectName( "LibraryGrid" );
    setFrameShape( QFrame::NoFrame );
    setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    /* no bar taking width (Figma draws none): a thin indicator is painted
     * over the grid while it scrolls; wheel and keys still scroll */
    setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    indicatorTimer.setSingleShot( true );
    indicatorTimer.setInterval( 900 );
    connect( &indicatorTimer, &QTimer::timeout, viewport(), QOverload<>::of( &QWidget::update ) );
    viewport()->setMouseTracking( true );
    viewport()->setAutoFillBackground( false );
    setAttribute( Qt::WA_Hover );
    verticalScrollBar()->setSingleStep( 40 );

    /* the playing item is drawn in the accent */
    connect( THEMIM, &MainInputManager::inputChanged, this, &LibraryGrid::followInput );
    followInput();
}

void LibraryGrid::followInput()
{
    QString path;
    input_thread_t *input = THEMIM->getInput();
    if( input )
    {
        input_item_t *item = input_GetItem( input );
        char *uri = item ? input_item_GetURI( item ) : NULL;
        if( uri )
        {
            const QUrl url( qfu( uri ) );
            if( url.isLocalFile() )
                path = url.toLocalFile();
            free( uri );
        }
    }
    setPlayingPath( path );
}

void LibraryGrid::setCompact( bool b )
{
    compact = b;
    relayout();
    viewport()->update();
}

void LibraryGrid::setEntries( const QVector<LibraryEntry> &e )
{
    entries = e;
    hovered = pressed = -1;
    relayout();
    verticalScrollBar()->setValue( 0 );
    viewport()->update();
}

void LibraryGrid::setMode( Mode m )
{
    if( viewMode == m )
        return;
    viewMode = m;
    relayout();
    viewport()->update();
}

void LibraryGrid::setPlayingPath( const QString &p )
{
    playingPath = p;
    viewport()->update();
}

void LibraryGrid::refreshThumbnail( const QString &path )
{
    const int i = lib->indexOf( path );
    if( i < 0 )
        return;
    const QString file = lib->thumbnailFile( lib->items()[i] );
    QMutableHashIterator<QString, QPixmap> it( pixmaps );
    while( it.hasNext() )
        if( it.next().key().startsWith( file + "@" ) )
            it.remove();
    viewport()->update();
}

int LibraryGrid::marginLeft() const { return compact ? 0 : GRID_MARGIN; }
int LibraryGrid::marginRight() const { return compact ? 0 : GRID_MARGIN_R; }
int LibraryGrid::topOffset() const
{
    return compact ? 0 : ( viewMode == GRID ? GRID_TOP : LIST_TOP );
}
int LibraryGrid::tileGap() const { return compact ? SIDE_TILE_GAP : TILE_GAP; }
int LibraryGrid::labelHeight() const { return compact ? SIDE_LABEL_H : TILE_LABEL_H; }

void LibraryGrid::relayout()
{
    const int w = viewport()->width() - marginLeft() - marginRight();
    if( viewMode == GRID )
    {
        const int target = compact ? SIDE_TILE_W : TILE_W;
        columns = qMax( 1, qRound( qreal( w + tileGap() ) / ( target + tileGap() ) ) );
        const int tileW = ( w - ( columns - 1 ) * tileGap() ) / columns;
        cell = QSize( tileW, tileW * 9 / 16 );
    }
    else
    {
        columns = compact ? 1 : LIST_COLUMNS;
        cell = QSize( w / columns, compact ? SIDE_ROW_H : LIST_ROW_H );
    }
    const int rows = ( entries.size() + columns - 1 ) / columns;
    const int pitch = viewMode == GRID ? cell.height() + labelHeight() : cell.height();
    const int contentH = topOffset() + rows * pitch;
    verticalScrollBar()->setPageStep( viewport()->height() );
    verticalScrollBar()->setRange( 0, qMax( 0, contentH - viewport()->height() ) );
}

QRect LibraryGrid::entryRect( int i ) const
{
    const int row = i / columns, col = i % columns;
    if( viewMode == GRID )
        return QRect( marginLeft() + col * ( cell.width() + tileGap() ),
                      topOffset() + row * ( cell.height() + labelHeight() ),
                      cell.width(), cell.height() + labelHeight() - ( compact ? 8 : 16 ) );
    /* Figma #4: the first row's centre is 46px under the divider */
    return QRect( marginLeft() + col * cell.width(), topOffset() + row * cell.height(),
                  cell.width() - ( compact ? 0 : 12 ), cell.height() );
}

int LibraryGrid::entryAt( const QPoint &pos ) const
{
    const QPoint p = pos + QPoint( 0, verticalScrollBar()->value() );
    for( int i = 0; i < entries.size(); i++ )
        if( entryRect( i ).contains( p ) )
            return i;
    return -1;
}

/* the item's thumbnail scaled to cover the size, cached per size */
QPixmap LibraryGrid::thumbnail( const LibraryEntry &e, const QSize &size )
{
    const int i = lib->indexOf( e.thumbFrom );
    if( i < 0 )
        return QPixmap();
    const QString file = lib->thumbnailFile( lib->items()[i] );
    if( file.isEmpty() )
        return QPixmap();
    const qreal dpr = devicePixelRatioF();
    const QString key = QString( "%1@%2x%3" ).arg( file ).arg( size.width() ).arg( size.height() );
    QHash<QString, QPixmap>::const_iterator it = pixmaps.constFind( key );
    if( it != pixmaps.constEnd() )
        return it.value();
    QPixmap src( file );
    if( src.isNull() )
        return QPixmap();
    const QSize px = size * dpr;
    QPixmap scaled = src.scaled( px, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation );
    scaled = scaled.copy( ( scaled.width() - px.width() ) / 2,
                          ( scaled.height() - px.height() ) / 2, px.width(), px.height() );
    scaled.setDevicePixelRatio( dpr );
    if( pixmaps.size() > 600 )
        pixmaps.clear();
    pixmaps.insert( key, scaled );
    return scaled;
}

void LibraryGrid::requestVisibleThumbnails()
{
    const QRect visible( 0, verticalScrollBar()->value(), viewport()->width(), viewport()->height() );
    /* last first: requestThumbnail puts each at the front of the queue */
    for( int i = entries.size() - 1; i >= 0; i-- )
    {
        if( !entryRect( i ).intersects( visible ) )
            continue;
        const int k = lib->indexOf( entries[i].thumbFrom );
        if( k >= 0 && lib->thumbnailFile( lib->items()[k] ).isEmpty() )
            lib->requestThumbnail( entries[i].thumbFrom );
    }
}

void LibraryGrid::paintEvent( QPaintEvent * )
{
    QPainter p( viewport() );
    p.setRenderHint( QPainter::Antialiasing );
    const int dy = verticalScrollBar()->value();
    const QRect visible( 0, dy, viewport()->width(), viewport()->height() );
    QFont labelFont = font();
    labelFont.setPixelSize( viewMode == GRID ? ( compact ? 14 : 16 ) : 15 );
    p.setFont( labelFont );
    const QFontMetrics fm( labelFont );
    const QColor accent = Theme::accent();

    for( int i = 0; i < entries.size(); i++ )
    {
        const QRect r = entryRect( i );
        if( !r.intersects( visible ) )
            continue;
        const LibraryEntry &e = entries[i];
        const QRect rr = r.translated( 0, -dy );
        const bool playing = e.type == LibraryEntry::ITEM && !playingPath.isEmpty()
                             && e.thumbFrom == playingPath;
        QColor text = playing ? accent : ( e.watched ? LABEL_DIM : LABEL );
        if( i == hovered && !playing )
            text = text.lighter( 130 );

        /* sidebar rows with a glyph instead of a picture: actions, folders */
        const bool glyphRow = compact && viewMode == LIST
                              && ( e.type == LibraryEntry::ACTION || !e.icon.isEmpty() );
        if( compact && viewMode == LIST && i == hovered )
        {
            p.setPen( Qt::NoPen );
            p.setBrush( Theme::colors().hover );
            p.drawRoundedRect( QRectF( rr ).adjusted( 0, 1, -2, -1 ), 6, 6 );
        }
        if( glyphRow || e.type == LibraryEntry::ACTION )
        {
            const QColor fg = i == hovered ? Theme::colors().text : Theme::colors().icon;
            p.drawPixmap( QRect( rr.left() + SIDE_ICON_X, rr.center().y() - SIDE_ICON / 2 + 1,
                                 SIDE_ICON, SIDE_ICON ),
                          Theme::pixmap( e.icon.isEmpty() ? QString( "folder" ) : e.icon, SIDE_ICON, fg ) );
            p.setPen( i == hovered ? Theme::colors().text : LABEL );
            const QRect tr = rr.adjusted( SIDE_TEXT_X, 0, -8, 0 );
            p.drawText( tr, Qt::AlignLeft | Qt::AlignVCenter,
                        fm.elidedText( e.title, Qt::ElideRight, tr.width() ) );
            continue;
        }

        QRect thumbRect;
        QRect textRect;
        if( viewMode == GRID )
        {
            thumbRect = QRect( rr.topLeft(), cell );
            textRect = QRect( rr.left(), thumbRect.bottom() + 16, rr.width(), fm.height() + 4 );
        }
        else if( compact )
        {
            thumbRect = QRect( rr.left() + SIDE_THUMB_X, rr.top() + ( rr.height() - LIST_THUMB ) / 2,
                               LIST_THUMB, LIST_THUMB );
            textRect = rr.adjusted( SIDE_TEXT_X, 0, -8, 0 );
        }
        else
        {
            thumbRect = QRect( rr.left(), rr.top() + ( rr.height() - LIST_THUMB ) / 2,
                               LIST_THUMB, LIST_THUMB );
            textRect = QRect( thumbRect.right() + 12, rr.top(),
                              rr.width() - LIST_THUMB - 12, rr.height() );
        }

        /* picture */
        QPainterPath clip;
        clip.addRoundedRect( QRectF( thumbRect ), TILE_RADIUS, TILE_RADIUS );
        p.save();
        p.setClipPath( clip );
        p.fillRect( thumbRect, TILE_EMPTY );
        const QPixmap pm = thumbnail( e, thumbRect.size() );
        if( !pm.isNull() )
            p.drawPixmap( thumbRect, pm );
        else
        {
            const int k = lib->indexOf( e.thumbFrom );
            const bool audio = k >= 0 && lib->items()[k].kind == LibraryItem::AUDIO;
            const QString glyph = e.type == LibraryEntry::GROUP ? "folder"
                                : ( audio ? "music" : "video" );
            const int g = viewMode == GRID ? 32 : 18;
            p.drawPixmap( QRect( thumbRect.center() - QPoint( g / 2, g / 2 ), QSize( g, g ) ),
                          Theme::pixmap( glyph, g, QColor( 0x3A, 0x3A, 0x3A ) ) );
        }
        if( i == hovered && !( compact && viewMode == LIST ) )
            p.fillRect( thumbRect, QColor( 255, 255, 255, 18 ) );
        /* progress underline, inside the bottom edge */
        qreal ratio = e.progress;
        if( e.watched && ratio <= 0.01 )
            ratio = 1.0;
        if( ratio > 0.01 && e.type == LibraryEntry::ITEM )
        {
            const int inset = viewMode == GRID ? UNDERLINE_INSET : 2;
            const int h = viewMode == GRID ? UNDERLINE_H : 2;
            const int full = thumbRect.width() - 2 * inset;
            p.fillRect( QRect( thumbRect.left() + inset, thumbRect.bottom() - h + 1,
                               int( full * ratio ), h ), accent );
        }
        p.restore();

        /* label */
        p.setPen( text );
        QString label = e.title;
        if( e.type == LibraryEntry::GROUP && e.count > 0 && !compact )
            label = QString( "%1  ·  %2" ).arg( e.title ).arg( e.count );
        p.drawText( textRect, Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText( label, Qt::ElideMiddle, textRect.width() ) );
    }

    if( entries.isEmpty() )
    {
        p.setPen( LABEL_DIM );
        p.drawText( compact ? viewport()->rect().adjusted( 0, 0, 0, -viewport()->height() / 2 )
                            : viewport()->rect().adjusted( 0, 0, 0, -80 ), Qt::AlignCenter,
                    lib->isScanning() ? qtr( "Scanning your folders…" )
                                      : qtr( "Nothing here yet" ) );
    }

    /* scroll position, while scrolling */
    const int range = verticalScrollBar()->maximum();
    if( range > 0 && indicatorTimer.isActive() )
    {
        const int vh = viewport()->height();
        const int h = qMax( 40, vh * vh / ( vh + range ) );
        const int y = ( vh - h ) * verticalScrollBar()->value() / range;
        p.setPen( Qt::NoPen );
        p.setBrush( QColor( 255, 255, 255, 60 ) );
        p.drawRoundedRect( QRectF( viewport()->width() - 8, y + 2, 4, h - 4 ), 2, 2 );
    }

    /* ask for the visible thumbnails once painting settles */
    QTimer::singleShot( 120, this, &LibraryGrid::requestVisibleThumbnails );
}

void LibraryGrid::resizeEvent( QResizeEvent *e )
{
    QAbstractScrollArea::resizeEvent( e );
    relayout();
}

void LibraryGrid::scrollContentsBy( int, int )
{
    indicatorTimer.start();
    viewport()->update();
}

void LibraryGrid::mouseMoveEvent( QMouseEvent *e )
{
    const int h = entryAt( e->pos() );
    if( h != hovered )
    {
        hovered = h;
        viewport()->setCursor( h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor );
        viewport()->update();
    }
}

void LibraryGrid::mousePressEvent( QMouseEvent *e )
{
    if( e->button() == Qt::LeftButton )
        pressed = entryAt( e->pos() );
}

void LibraryGrid::mouseReleaseEvent( QMouseEvent *e )
{
    if( e->button() == Qt::LeftButton && pressed >= 0 && entryAt( e->pos() ) == pressed )
        emit activated( pressed );
    pressed = -1;
}

void LibraryGrid::leaveEvent( QEvent * )
{
    hovered = -1;
    viewport()->update();
}

void LibraryGrid::contextMenuEvent( QContextMenuEvent *e )
{
    /* viewport coordinates: QAbstractScrollArea forwards the viewport event */
    const int i = entryAt( e->pos() );
    if( i >= 0 )
        emit contextMenu( i, e->globalPos() );
}

/**********************************************************************
 * LibraryView
 **********************************************************************/
static QToolButton *toolButton( QWidget *parent, const QString &icon, const QString &tip )
{
    QToolButton *b = new QToolButton( parent );
    Theme::setupIconButton( b, icon, 32, 24 );
    b->setFixedSize( 32, 32 );
    b->setToolTip( tip );
    return b;
}

LibraryView::LibraryView( intf_thread_t *_p_intf, QWidget *parent )
    : QWidget( parent ), p_intf( _p_intf ), lib( MediaLibrary::instance( _p_intf ) ),
      browser( new LibraryBrowser( _p_intf, "Library/sort", this ) )
{
    setObjectName( "VLCLibrary" );
    setAttribute( Qt::WA_StyledBackground, true );

    QHBoxLayout *l = new QHBoxLayout( this );
    l->setContentsMargins( 0, 0, 0, 0 );
    l->setSpacing( 0 );
    /* only the nav panel is inset from the window edges */
    QVBoxLayout *navBox = new QVBoxLayout;
    navBox->setContentsMargins( PAGE_INSET, PAGE_INSET, 0, PAGE_INSET );
    navBox->addWidget( buildNav() );
    l->addLayout( navBox );

    QWidget *content = new QWidget( this );
    QVBoxLayout *cl = new QVBoxLayout( content );
    cl->setContentsMargins( 0, 0, 0, 0 );
    cl->setSpacing( 0 );
    cl->addWidget( buildToolbar() );
    QFrame *rule = new QFrame( content );
    rule->setObjectName( "LibraryRule" );
    rule->setFixedHeight( 1 );
    QHBoxLayout *ruleRow = new QHBoxLayout;
    ruleRow->setContentsMargins( GRID_MARGIN, 0, GRID_MARGIN_R, 0 );
    ruleRow->addWidget( rule );
    cl->addLayout( ruleRow );
    grid = new LibraryGrid( p_intf, content );
    cl->addWidget( grid, 1 );
    l->addWidget( content, 1 );

    connect( grid, &LibraryGrid::activated, browser, &LibraryBrowser::activate );
    connect( grid, &LibraryGrid::contextMenu, this, [this]( int i, const QPoint &pos ) {
        browser->showEntryMenu( i, pos, this );
    } );
    connect( lib, &MediaLibrary::thumbnailReady, grid, &LibraryGrid::refreshThumbnail );
    connect( browser, &LibraryBrowser::locationChanged, this, &LibraryView::locationChanged );
    connect( browser, &LibraryBrowser::entriesChanged, this, &LibraryView::entriesChanged );
    connect( browser, &LibraryBrowser::played, this, [this]( const QString &first ) {
        grid->setPlayingPath( first );
        /* the playlist chip names where playback came from */
        mini->setSourceLabel( browser->sourceLabel() );
        emit played();
    } );

    /* the mini player, over the grid while something plays */
    mini = new MiniPlayer( p_intf, this );
    mini->setDocked( getSettings()->value( "Library/miniDocked", false ).toBool() );
    connect( mini, &MiniPlayer::dockChanged, this, [this]( bool d ) {
        getSettings()->setValue( "Library/miniDocked", d );
        placeMini();
    } );
    connect( THEMIM, &MainInputManager::inputChanged, this, &LibraryView::placeMini );
    connect( THEMIM->getIM(), &InputManager::playingStatusChanged, this, &LibraryView::placeMini );
    placeMini();

    grid->setMode( getSettings()->value( "Library/view", "grid" ).toString() == "list"
                   ? LibraryGrid::LIST : LibraryGrid::GRID );
    viewBtn->setIcon( Theme::icon( grid->mode() == LibraryGrid::GRID ? "grid" : "view", 24 ) );
    openSection( "videos" );
}

/* A nav row with the Figma offsets: 24px icon 18px in, label 53px in.
 * QToolButton cannot place its label, so the row paints itself. */
class LibraryNavItem : public QAbstractButton
{
public:
    LibraryNavItem( const QString &icon, const QString &text, QWidget *parent )
        : QAbstractButton( parent ), iconName( icon )
    {
        setText( text );
        setFixedHeight( 46 );
        setCursor( Qt::PointingHandCursor );
        setAttribute( Qt::WA_Hover );
    }
protected:
    void paintEvent( QPaintEvent * ) override
    {
        QPainter p( this );
        p.setRenderHint( QPainter::Antialiasing );
        const bool on = isChecked();
        const bool hover = underMouse();
        if( hover )
        {
            p.setPen( Qt::NoPen );
            p.setBrush( Theme::colors().hover );
            p.drawRoundedRect( QRectF( rect() ).adjusted( 6, 3, -6, -3 ), 6, 6 );
        }
        const QColor fg = ( on || hover ) ? Theme::colors().text : Theme::colors().icon;
        p.drawPixmap( 18, ( height() - 24 ) / 2, Theme::pixmap( iconName, 24, fg ) );
        QFont f = font();
        f.setPixelSize( 15 );
        p.setFont( f );
        p.setPen( fg );
        p.drawText( QRect( 53, 0, width() - 60, height() ), Qt::AlignLeft | Qt::AlignVCenter, text() );
    }
private:
    QString iconName;
};

QWidget *LibraryView::buildNav()
{
    QWidget *nav = new QWidget( this );
    nav->setObjectName( "LibraryNav" );
    nav->setAttribute( Qt::WA_StyledBackground, true );
    nav->setFixedWidth( NAV_W );
    QVBoxLayout *v = new QVBoxLayout( nav );
    v->setContentsMargins( 0, 6, 0, 6 );
    v->setSpacing( 0 );

    struct { const char *key, *icon; const char *label; } items[] = {
        { "home", "home", N_( "Home" ) },
        { "videos", "videos", N_( "Videos" ) },
        { "music", "songs", N_( "Music" ) },
        { "network", "network", N_( "Network" ) },
        { "playlists", "playlist", N_( "Playlists" ) },
        { "recent", "recent", N_( "Recent" ) },
        { "add", "add", N_( "Add item" ) },
    };
    for( const auto &it : items )
    {
        LibraryNavItem *b = new LibraryNavItem( it.icon, qtr( it.label ), nav );
        b->setCheckable( strcmp( it.key, "add" ) != 0 );
        const QString key = it.key;
        connect( b, &QAbstractButton::clicked, this, [this, key]() {
            if( key == "network" || key == "playlists" )
                emit playerSectionRequested( key );
            else if( key == "add" )
                /* a folder for the library to watch */
                LibraryBrowser::addFolder( p_intf, this );
            else
                openSection( key );
        } );
        navButtons.insert( key, b );
        v->addWidget( b );
    }
    v->addStretch( 1 );
    return nav;
}

QWidget *LibraryView::buildToolbar()
{
    QWidget *bar = new QWidget( this );
    bar->setObjectName( "LibraryToolbar" );
    bar->setFixedHeight( TOOLBAR_H );
    QHBoxLayout *h = new QHBoxLayout( bar );
    h->setContentsMargins( 24, 0, 16, 0 );
    h->setSpacing( 0 );

    backBtn = toolButton( bar, "back", qtr( "Back" ) );
    fwdBtn = toolButton( bar, "forward", qtr( "Forward" ) );
    connect( backBtn, &QToolButton::clicked, browser, &LibraryBrowser::goBack );
    connect( fwdBtn, &QToolButton::clicked, browser, &LibraryBrowser::goForward );
    h->addWidget( backBtn );
    h->addSpacing( 7 );
    h->addWidget( fwdBtn );

    crumb = new QWidget( bar );
    crumbLayout = new QHBoxLayout( crumb );
    crumbLayout->setContentsMargins( 0, 0, 0, 0 );
    crumbLayout->setSpacing( 0 );
    h->addWidget( crumb );
    h->addStretch( 1 );

    countLabel = new QLabel( bar );
    countLabel->setObjectName( "LibraryCount" );
    h->addWidget( countLabel );
    h->addSpacing( 16 );

    sortBtn = toolButton( bar, "sort", qtr( "Sort" ) );
    connect( sortBtn, &QToolButton::clicked, this, [this]() {
        browser->showSortMenu( sortBtn->mapToGlobal( QPoint( 0, sortBtn->height() + 4 ) ), this );
    } );
    h->addWidget( sortBtn );
    h->addSpacing( 12 );
    viewBtn = toolButton( bar, "grid", qtr( "Grid or list" ) );
    connect( viewBtn, &QToolButton::clicked, this, [this]() {
        const bool toList = grid->mode() == LibraryGrid::GRID;
        grid->setMode( toList ? LibraryGrid::LIST : LibraryGrid::GRID );
        viewBtn->setIcon( Theme::icon( toList ? "view" : "grid", 24 ) );
        getSettings()->setValue( "Library/view", toList ? "list" : "grid" );
    } );
    h->addWidget( viewBtn );
    h->addSpacing( 16 );

    search = new QLineEdit( bar );
    search->setObjectName( "LibrarySearch" );
    search->setPlaceholderText( qtr( "Search" ) );
    search->setFixedSize( 324, 32 );
    /* a 24px glyph 9px in, text 42px in (line edit actions are 16px) */
    QLabel *glass = new QLabel( search );
    glass->setPixmap( Theme::pixmap( "search", 24, Theme::colors().icon ) );
    glass->setGeometry( 9, 4, 24, 24 );
    glass->setAttribute( Qt::WA_TransparentForMouseEvents );
    search->setTextMargins( 34, 0, 4, 0 );
    search->setClearButtonEnabled( true );
    connect( search, &QLineEdit::textChanged, browser, &LibraryBrowser::setSearch );
    h->addWidget( search );
    h->addSpacing( 17 );

    QToolButton *more = toolButton( bar, "dots", qtr( "More" ) );
    connect( more, &QToolButton::clicked, this, [this, more]() {
        QMenu menu( this );
        menu.addAction( RedesignMenus::menuIcon( "refresh", 20 ), qtr( "Rescan folders" ),
                        lib, &MediaLibrary::rescan );
        menu.addAction( RedesignMenus::menuIcon( "add", 20 ), qtr( "Add a folder…" ), this, [this]() {
            navButtons.value( "add" )->click();
        } );
        menu.exec( more->mapToGlobal( QPoint( 0, more->height() + 4 ) ) );
    } );
    h->addWidget( more );
    return bar;
}

void LibraryView::openSection( const QString &key )
{
    search->blockSignals( true );
    search->clear();
    search->blockSignals( false );
    browser->openSection( key );
}

void LibraryView::locationChanged()
{
    foreach( const QString &k, navButtons.keys() )
        navButtons[k]->setChecked( k == browser->section() );
    backBtn->setEnabled( browser->canGoBack() );
    fwdBtn->setEnabled( browser->canGoForward() );
    rebuildCrumb();
}

void LibraryView::entriesChanged()
{
    grid->setEntries( browser->entries() );
    const int n = browser->itemCount();
    countLabel->setText( n == 1 ? qtr( "1 item" ) : QString( qtr( "%1 items" ) ).arg( n ) );
}

void LibraryView::rebuildCrumb()
{
    QLayoutItem *child;
    while( ( child = crumbLayout->takeAt( 0 ) ) )
    {
        delete child->widget();
        delete child;
    }
    const QStringList location = browser->location();
    for( int i = 0; i < location.size(); i++ )
    {
        const bool last = i == location.size() - 1;
        QToolButton *seg = new QToolButton( crumb );
        seg->setObjectName( last ? "CrumbCurrent" : "CrumbLink" );
        seg->setText( browser->segmentTitle( i ) );
        seg->setCursor( last ? Qt::ArrowCursor : Qt::PointingHandCursor );
        const QStringList target = location.mid( 0, i + 1 );
        if( !last )
            connect( seg, &QToolButton::clicked, this, [this, target]() { browser->setLocation( target ); } );
        crumbLayout->addWidget( seg );
        if( !last )
        {
            QLabel *sep = new QLabel( QString::fromUtf8( "▸" ), crumb );
            sep->setObjectName( "CrumbSeparator" );
            sep->setContentsMargins( 1, 2, 1, 0 );
            crumbLayout->addWidget( sep );
        }
    }
}

void LibraryView::resizeEvent( QResizeEvent *e )
{
    QWidget::resizeEvent( e );
    placeMini();
}

/* Figma #3-4 floating bottom right (9px, 6px in), #5 docked bottom left */
void LibraryView::placeMini()
{
    const bool playing = THEMIM->getInput() != NULL
                         && THEMIM->getIM()->playingStatus() != END_S;
    if( !playing )
    {
        mini->hide();
        return;
    }
    const int x = mini->isDocked() ? PAGE_INSET : width() - MiniPlayer::CARD_W - 9;
    mini->move( x, height() - MiniPlayer::CARD_H - 6 );
    mini->show();
    mini->raise();
}

void LibraryView::keyPressEvent( QKeyEvent *e )
{
    if( e->key() == Qt::Key_Escape )
    {
        if( !search->text().isEmpty() )
            search->clear();
        else
            emit closeRequested();
        return;
    }
    if( e->key() == Qt::Key_Backspace && !search->hasFocus() && browser->canGoBack() )
    {
        backBtn->click();
        return;
    }
    if( e->matches( QKeySequence::Find ) )
    {
        search->setFocus();
        search->selectAll();
        return;
    }
    QWidget::keyPressEvent( e );
}
