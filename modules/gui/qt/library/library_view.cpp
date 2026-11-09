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
#include <QApplication>
#include <QDesktopServices>
#include <QDrag>
#include <QMimeData>
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

/* Figma row 2 #7 (Home) and Components "Home": logical px at 1920 */
enum
{
    SHELF_TOP        = 37,    /* rule to the first heading */
    SHELF_HEAD_H     = 24,
    SHELF_HEAD_FONT  = 20,
    SHELF_CHEVRON_GAP = 5,
    SHELF_TILES_Y    = 44,    /* heading top to the tiles */
    SHELF_LABEL_Y    = 20,    /* tile bottom to the title */
    SHELF_LINE       = 19,
    SHELF_SUB_Y      = 29,    /* title top to the artist line */
    SHELF_NEXT       = 77,    /* label bottom to the next heading */
    SHELF_WRAP       = 39,    /* label bottom to the next row (no headings) */
    SHELF_RADIUS     = 12,
    SHELF_WIDE_W     = 316,
    SHELF_WIDE_H     = 178,
    SHELF_SQUARE     = 200,
    SHELF_GLYPH      = 48,
    SHELF_BAR_H      = 4,
    SIDE_HEAD_Y      = 1,     /* rows: grid top to the first heading row */
    SIDE_HEAD_H      = 47,    /* heading row to the first item row */
    SIDE_SHELF_GAP   = 20,
    SIDE_SHELF_ROWS  = 3,
};
static const QColor SHELF_TILE  ( 0x17, 0x17, 0x17 );
static const QColor SHELF_GLYPH_BG( 0x1E, 0x1E, 0x1E );
static const QColor SIDE_THUMB_BG( 0x30, 0x30, 0x30 );

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
    hasHeaders = hasTiles = false;
    foreach( const LibraryEntry &x, entries )
    {
        hasHeaders = hasHeaders || x.type == LibraryEntry::HEADER;
        hasTiles = hasTiles || x.tile != LibraryEntry::TILE_AUTO;
    }
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

QSize LibraryGrid::tileSize( const LibraryEntry &e ) const
{
    if( e.tile == LibraryEntry::TILE_WIDE || e.tile == LibraryEntry::TILE_AUTO )
        return QSize( SHELF_WIDE_W, SHELF_WIDE_H );
    return QSize( SHELF_SQUARE, SHELF_SQUARE );
}

void LibraryGrid::layoutShelves()
{
    rects.fill( QRect(), entries.size() );
    const int w = viewport()->width() - marginLeft() - marginRight();
    int bottom = 0;
    if( compact )
    {
        /* Components "Home": heading rows, then up to three 42px rows */
        int y = SIDE_HEAD_Y, inShelf = 0;
        bool first = true;
        for( int i = 0; i < entries.size(); i++ )
        {
            if( entries[i].type == LibraryEntry::HEADER )
            {
                if( !first )
                    y += SIDE_SHELF_GAP;
                first = false;
                rects[i] = QRect( 0, y, w, SIDE_ROW_H );
                y += SIDE_HEAD_H;
                inShelf = 0;
                continue;
            }
            if( hasHeaders && inShelf >= SIDE_SHELF_ROWS )
                continue;
            rects[i] = QRect( 0, y, w, SIDE_ROW_H );
            y += SIDE_ROW_H;
            inShelf++;
        }
        bottom = y;
    }
    else
    {
        /* Figma row 2 #7: a heading, then one row of fixed tiles; without
         * headings (a shelf's whole list) the tiles wrap */
        QFont hf = font();
        hf.setPixelSize( SHELF_HEAD_FONT );
        const QFontMetrics hfm( hf );
        const int x0 = marginLeft(), right = marginLeft() + w;
        int x = x0, rowTop = hasHeaders ? SHELF_TOP : GRID_TOP, rowBottom = rowTop;
        bool rowFull = false, first = true;
        for( int i = 0; i < entries.size(); i++ )
        {
            const LibraryEntry &e = entries[i];
            if( e.type == LibraryEntry::HEADER )
            {
                const int y = first ? SHELF_TOP : rowBottom + SHELF_NEXT;
                first = false;
                rects[i] = QRect( x0, y, hfm.horizontalAdvance( e.title ) + SHELF_CHEVRON_GAP + 24,
                                  SHELF_HEAD_H );
                rowTop = rowBottom = y + SHELF_TILES_Y;
                x = x0;
                rowFull = false;
                continue;
            }
            const QSize t = tileSize( e );
            const int h = t.height() + SHELF_LABEL_Y + SHELF_LINE
                          + ( e.subtitle.isEmpty() ? 0 : SHELF_SUB_Y );
            if( x > x0 && x + t.width() > right )
            {
                if( hasHeaders )
                    rowFull = true;
                else
                {
                    rowTop = rowBottom + SHELF_WRAP;
                    x = x0;
                }
            }
            if( rowFull )
                continue;
            rects[i] = QRect( x, rowTop, t.width(), h );
            x += t.width() + TILE_GAP;
            rowBottom = qMax( rowBottom, rowTop + h );
        }
        bottom = rowBottom + SHELF_WRAP;
    }
    verticalScrollBar()->setPageStep( viewport()->height() );
    verticalScrollBar()->setRange( 0, qMax( 0, bottom - viewport()->height() ) );
}

void LibraryGrid::relayout()
{
    if( shelfLayout() )
    {
        layoutShelves();
        return;
    }
    rects.clear();
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
    if( !rects.isEmpty() )
        return rects.value( i );
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
    labelFont.setPixelSize( shelfLayout() && !compact ? 16
                            : viewMode == GRID ? ( compact ? 14 : 16 ) : 15 );
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
        if( shelfLayout() )
        {
            paintShelfEntry( p, i, rr, fm );
            p.setFont( labelFont );
            continue;
        }
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
        /* on the page an action (a recent stream) is a tile with its glyph */
        if( glyphRow || ( e.type == LibraryEntry::ACTION && compact ) )
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
            const QString glyph = e.type == LibraryEntry::ACTION ? e.icon
                                : e.type == LibraryEntry::GROUP
                                  ? ( e.tile == LibraryEntry::TILE_SQUARE ? "album" : "folder" )
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
        QString label = e.subtitle.isEmpty() ? e.title
                                             : QString( "%1 - %2" ).arg( e.title, e.subtitle );
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

/* Home shelves, their tile pages, and the sidebar's headed rows */
void LibraryGrid::paintShelfEntry( QPainter &p, int i, const QRect &rr, const QFontMetrics &fm )
{
    const LibraryEntry &e = entries[i];
    const bool hover = i == hovered;
    const QColor accent = Theme::accent();
    const bool playing = e.type == LibraryEntry::ITEM && !playingPath.isEmpty()
                         && e.thumbFrom == playingPath;
    const int k = e.index >= 0 ? e.index : lib->indexOf( e.thumbFrom );
    const bool audio = k >= 0 && lib->items()[k].kind == LibraryItem::AUDIO;

    if( e.type == LibraryEntry::HEADER )
    {
        QFont f = font();
        f.setPixelSize( compact ? 16 : SHELF_HEAD_FONT );
        p.setFont( f );
        const QColor fg = hover ? Theme::colors().text : LABEL;
        p.setPen( fg );
        const QPixmap chev = Theme::pixmap( "chevron", 24, hover ? Theme::colors().text
                                                                  : Theme::colors().icon );
        if( compact )
        {
            /* the name from the row's left, the chevron at its right */
            const QRect tr = rr.adjusted( SIDE_THUMB_X, 0, -32, 0 );
            p.drawText( tr, Qt::AlignLeft | Qt::AlignVCenter,
                        QFontMetrics( f ).elidedText( e.title, Qt::ElideRight, tr.width() ) );
            p.drawPixmap( rr.right() - 24 + 1, rr.center().y() - 12 + 1, chev );
        }
        else
        {
            const int tw = QFontMetrics( f ).horizontalAdvance( e.title );
            p.drawText( QRect( rr.left(), rr.top(), tw + 2, rr.height() ),
                        Qt::AlignLeft | Qt::AlignVCenter, e.title );
            p.drawPixmap( rr.left() + tw + SHELF_CHEVRON_GAP, rr.top(), chev );
        }
        return;
    }

    if( compact )
    {
        /* Components "Home": dim 14px names; a 32px picture or a glyph */
        QFont f = font();
        f.setPixelSize( hasHeaders ? 14 : 15 );
        p.setFont( f );
        const QFontMetrics rfm( f );
        if( hover )
        {
            p.setPen( Qt::NoPen );
            p.setBrush( Theme::colors().hover );
            p.drawRoundedRect( QRectF( rr ).adjusted( 0, 1, -2, -1 ), 6, 6 );
        }
        QColor text = playing ? accent : ( hasHeaders ? LABEL_DIM : ( e.watched ? LABEL_DIM : LABEL ) );
        if( hover && !playing )
            text = Theme::colors().text;
        const QRect tr = rr.adjusted( SIDE_TEXT_X, 0, -8, 0 );
        if( e.type == LibraryEntry::ACTION )
        {
            /* Home rows: a 32px glyph in the picture's place */
            const QColor fg = hover ? Theme::colors().text : Theme::colors().icon;
            const int g = hasHeaders ? LIST_THUMB : SIDE_ICON;
            const int gx = hasHeaders ? SIDE_THUMB_X : SIDE_ICON_X;
            p.drawPixmap( QRect( rr.left() + gx, rr.center().y() - g / 2 + 1, g, g ),
                          Theme::pixmap( e.icon.isEmpty() ? QString( "folder" ) : e.icon, g, fg ) );
        }
        else
        {
            const QRect thumbRect( rr.left() + SIDE_THUMB_X, rr.top() + ( rr.height() - LIST_THUMB ) / 2,
                                   LIST_THUMB, LIST_THUMB );
            QPainterPath clip;
            clip.addRoundedRect( QRectF( thumbRect ), 2, 2 );
            p.save();
            p.setClipPath( clip );
            p.fillRect( thumbRect, SIDE_THUMB_BG );
            const QPixmap pm = thumbnail( e, thumbRect.size() );
            if( !pm.isNull() )
                p.drawPixmap( thumbRect, pm );
            else
                p.drawPixmap( QRect( thumbRect.center() - QPoint( 9, 9 ), QSize( 18, 18 ) ),
                              Theme::pixmap( audio ? "music" : "video", 18, Theme::colors().icon ) );
            if( e.progress > 0.01 )
                p.fillRect( QRect( thumbRect.left(), thumbRect.bottom() - 2,
                                   qMax( 2, int( thumbRect.width() * e.progress ) ), 3 ), accent );
            p.restore();
        }
        p.setPen( text );
        const QString label = e.subtitle.isEmpty() ? e.title
                                                   : QString( "%1 - %2" ).arg( e.title, e.subtitle );
        p.drawText( tr, Qt::AlignLeft | Qt::AlignVCenter, rfm.elidedText( label, Qt::ElideRight, tr.width() ) );
        return;
    }

    /* page tiles: 16:9 video or a square, radius 12 (Figma row 2 #7) */
    const QSize t = tileSize( e );
    const QRect thumbRect( rr.topLeft(), t );
    QPainterPath clip;
    clip.addRoundedRect( QRectF( thumbRect ), SHELF_RADIUS, SHELF_RADIUS );
    p.save();
    p.setClipPath( clip );
    const bool glyphTile = e.tile == LibraryEntry::TILE_GLYPH;
    p.fillRect( thumbRect, glyphTile ? SHELF_GLYPH_BG : SHELF_TILE );
    const QPixmap pm = glyphTile ? QPixmap() : thumbnail( e, thumbRect.size() );
    if( !pm.isNull() )
        p.drawPixmap( thumbRect, pm );
    else
    {
        const QString glyph = glyphTile ? e.icon
                            : e.type == LibraryEntry::GROUP ? "album" : ( audio ? "music" : "video" );
        const QColor gc = glyphTile ? Theme::colors().icon : QColor( 0x3A, 0x3A, 0x3A );
        p.drawPixmap( QRect( thumbRect.center() - QPoint( SHELF_GLYPH / 2 - 1, SHELF_GLYPH / 2 - 1 ),
                             QSize( SHELF_GLYPH, SHELF_GLYPH ) ),
                      Theme::pixmap( glyph, SHELF_GLYPH, gc ) );
    }
    if( hover )
        p.fillRect( thumbRect, QColor( 255, 255, 255, 18 ) );
    if( e.type == LibraryEntry::ITEM && e.progress > 0.01 && !e.watched )
        p.fillRect( QRect( thumbRect.left(), thumbRect.bottom() - SHELF_BAR_H + 1,
                           qMax( 4, int( thumbRect.width() * e.progress ) ), SHELF_BAR_H ), accent );
    p.restore();

    QColor text = playing ? accent : LABEL;
    if( hover && !playing )
        text = text.lighter( 130 );
    p.setPen( text );
    const QRect title( rr.left(), thumbRect.bottom() + 1 + SHELF_LABEL_Y, rr.width(), SHELF_LINE );
    p.drawText( title, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText( e.title, Qt::ElideMiddle, title.width() ) );
    if( !e.subtitle.isEmpty() )
    {
        p.setPen( LABEL_DIM );
        const QRect sub = title.translated( 0, SHELF_SUB_Y );
        p.drawText( sub, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText( e.subtitle, Qt::ElideRight, sub.width() ) );
    }
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
    /* drag an item onto the queue (P5d) */
    if( ( e->buttons() & Qt::LeftButton ) && pressed >= 0
        && ( e->pos() - pressPos ).manhattanLength() >= QApplication::startDragDistance() )
    {
        const int i = pressed;
        pressed = -1;
        startDrag( i );
        return;
    }
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
    {
        pressed = entryAt( e->pos() );
        pressPos = e->pos();
    }
}

void LibraryGrid::mouseReleaseEvent( QMouseEvent *e )
{
    if( e->button() == Qt::LeftButton && pressed >= 0 && entryAt( e->pos() ) == pressed )
        emit activated( pressed );
    pressed = -1;
}

/* a library file, or a stream in a playlist, as a URL with its picture */
void LibraryGrid::startDrag( int i )
{
    if( i < 0 || i >= entries.size() )
        return;
    const LibraryEntry &e = entries[i];
    QString uri;
    if( e.type == LibraryEntry::ITEM && e.index >= 0 )
        uri = lib->items()[e.index].uri();
    else if( e.type == LibraryEntry::ACTION && e.segment.startsWith( "mrl:" ) )
        uri = e.segment.mid( 4 );
    if( uri.isEmpty() )
        return;
    QMimeData *mime = new QMimeData;
    mime->setUrls( QList<QUrl>() << QUrl( uri ) );
    QDrag *drag = new QDrag( this );
    drag->setMimeData( mime );
    QPixmap pm = thumbnail( e, QSize( 64, 36 ) );
    if( pm.isNull() )
        pm = Theme::pixmap( e.icon.isEmpty() ? QString( "video" ) : e.icon, 24, Theme::colors().text );
    drag->setPixmap( pm );
    drag->setHotSpot( QPoint( pm.width() / pm.devicePixelRatio() / 2, pm.height() / pm.devicePixelRatio() / 2 ) );
    drag->exec( Qt::CopyAction );
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
    connect( mini, &MiniPlayer::sizeChanged, this, &LibraryView::placeMini );
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
        /* Figma row 2 #7: the open section's glyph is the filled accent one */
        p.drawPixmap( 18, ( height() - 24 ) / 2,
                      on ? Theme::pixmap( Theme::activeName( iconName ), 24, Theme::accent() )
                         : Theme::pixmap( iconName, 24, fg ) );
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
            if( key == "network" )
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
    /* Home and Recent keep the order things were played in; Home's
     * shelves are tiles in either view */
    const QString sec = browser->section();
    sortBtn->setEnabled( sec != "home" && sec != "recent" );
    viewBtn->setEnabled( !( sec == "home" && browser->location().size() == 1 ) );
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
        /* Figma: a lone segment is plain ("Home"); the last of several
         * is semibold ("Videos ▶ Shows ▶ S01") */
        seg->setObjectName( !last ? "CrumbLink"
                                  : location.size() > 1 ? "CrumbCurrent" : "CrumbOnly" );
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
    mini->move( x, height() - mini->height() - 6 );
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
