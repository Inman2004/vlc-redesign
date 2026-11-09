/*****************************************************************************
 * music_view.cpp : the player's music mode (P5c, Figma row 2 #8-9)
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

#include "components/music_view.hpp"
#include "library/media_library.hpp"
#include "input_manager.hpp"
#include "dialogs_provider.hpp"
#include "styles/vlc_theme.hpp"

#include <vlc_input.h>
#include <vlc_input_item.h>

#include <QContextMenuEvent>
#include <QEvent>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

/* Figma row 2 #8-9 at 1920x1080: content 986 tall, art 736 square, 79
 * from the art to the caption's line box, caption 24px */
enum
{
    ART_RADIUS   = 16,
    CAPTION_GAP  = 79,
    CAPTION_LINE = 24,
    CAPTION_FONT = 24,
    SIDE_MARGIN  = 24,
};
static const qreal ART_OF_HEIGHT = 736.0 / 986.0;

MusicView::MusicView( intf_thread_t *_p_intf, QWidget *background )
    : QWidget( background ), p_intf( _p_intf ), lib( MediaLibrary::instance( _p_intf ) )
{
    setObjectName( "MusicView" );
    setAttribute( Qt::WA_OpaquePaintEvent );
    hide();
    background->installEventFilter( this );
    setGeometry( background->rect() );

    settle.setSingleShot( true );
    settle.setInterval( 500 );
    connect( &settle, &QTimer::timeout, this, [this]() { refresh(); } );

    InputManager *im = THEMIM->getIM();
    connect( THEMIM, &MainInputManager::inputChanged, this, [this]() {
        settle.start();
        refresh();
    } );
    connect( im, &InputManager::voutChanged, this, [this]() { refresh(); } );
    connect( im, &InputManager::playingStatusChanged, this, [this]() { refresh(); } );
    connect( im, &InputManager::nameChanged, this, [this]() { refresh(); } );
    connect( im, QOverload<QString>::of( &InputManager::artChanged ), this, [this]( QString url ) {
        vlcArt = url;
        loadArt();
    } );
    connect( lib, &MediaLibrary::thumbnailReady, this, [this]( const QString &p ) {
        if( p == path )
            loadArt();
    } );
    connect( lib, &MediaLibrary::itemChanged, this, [this]() { refresh(); } );
}

bool MusicView::eventFilter( QObject *o, QEvent *e )
{
    if( o == parentWidget() && e->type() == QEvent::Resize )
        setGeometry( parentWidget()->rect() );
    return QWidget::eventFilter( o, e );
}

/* Music mode while an input plays without a video output. A song the
 * library knows switches at once; anything else waits for the video
 * output a video brings a moment after its input. */
void MusicView::refresh()
{
    InputManager *im = THEMIM->getIM();
    input_thread_t *input = THEMIM->getInput();
    QString p;
    QString title, artist;
    if( input )
    {
        input_item_t *item = input_GetItem( input );
        char *uri = item ? input_item_GetURI( item ) : NULL;
        if( uri )
        {
            const QUrl url( qfu( uri ) );
            if( url.isLocalFile() )
                p = url.toLocalFile();
            free( uri );
        }
        if( item )
        {
            char *t = input_item_GetTitleFbName( item );
            char *a = input_item_GetArtist( item );
            title = t ? qfu( t ) : QString();
            artist = a ? qfu( a ) : QString();
            free( t );
            free( a );
        }
    }
    const int i = p.isEmpty() ? -1 : lib->indexOf( p );
    const bool knownSong = i >= 0 && lib->items()[i].kind == LibraryItem::AUDIO;
    if( knownSong )
    {
        title = lib->items()[i].songTitle();
        if( !lib->items()[i].artist.isEmpty() )
            artist = lib->items()[i].artist;
    }
    caption = artist.isEmpty() ? title : QString( "%1 - %2" ).arg( title, artist );

    if( p != path )
    {
        path = p;
        vlcArt.clear();
        loadArt();
        /* a song outside the library is read on demand (art and peaks) */
        if( !path.isEmpty() && !knownSong )
            lib->peaks( path );
    }

    const bool audioOnly = input && !im->hasVideo() && im->playingStatus() != END_S;
    setActive( audioOnly && ( knownSong || !settle.isActive() ) );
    update();
}

void MusicView::loadArt()
{
    QString file = lib->artFile( path );
    if( file.isEmpty() && !vlcArt.isEmpty() )
        file = QUrl( vlcArt ).isLocalFile() ? QUrl( vlcArt ).toLocalFile() : vlcArt;
    art = file.isEmpty() ? QImage() : QImage( file );
    blurred = QPixmap();
    update();
}

void MusicView::setActive( bool b )
{
    if( b == active )
        return;
    active = b;
    setVisible( b );
    if( b )
        raise();
    emit activeChanged( b );
}

/* the art centred on the window (Figma keeps it there with the sidebar
 * open), kept inside the content area */
QRect MusicView::artRect() const
{
    const int contentH = height();
    int side = qRound( contentH * ART_OF_HEIGHT );
    side = qMin( side, width() - 2 * SIDE_MARGIN );
    side = qMin( side, contentH - CAPTION_GAP - CAPTION_LINE - 2 * SIDE_MARGIN );
    side = qMax( side, 0 );
    const QWidget *w = window();
    const int windowCentre = mapFrom( w, QPoint( w->width() / 2, 0 ) ).x();
    int x = windowCentre - side / 2;
    x = qBound( int( SIDE_MARGIN ), x, qMax( int( SIDE_MARGIN ), width() - SIDE_MARGIN - side ) );
    /* the art and the caption, centred in the height (73 / 77 at 1080) */
    const int block = side + CAPTION_GAP + CAPTION_LINE;
    const int y = ( contentH - block ) / 2 - 2;
    return QRect( x, y, side, side );
}

/* the art scaled to cover the view, blurred by a tiny round trip, dark */
QPixmap MusicView::backdrop()
{
    if( art.isNull() )
        return QPixmap();
    if( !blurred.isNull() && blurredFor == size() )
        return blurred;
    const qreal dpr = devicePixelRatioF();
    const QSize px = size() * dpr;
    const int cover = qMax( px.width(), px.height() );
    QImage small = art.scaled( 24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
    QImage mid = small.scaled( 96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
    QImage big = mid.scaled( cover, cover, Qt::IgnoreAspectRatio, Qt::SmoothTransformation );
    QImage view( px, QImage::Format_RGB32 );
    view.fill( Qt::black );
    {
        QPainter p( &view );
        p.drawImage( QPoint( ( px.width() - cover ) / 2, ( px.height() - cover ) / 2 ), big );
        /* Figma's backdrop stays near black: #0F at its brightest */
        p.fillRect( view.rect(), QColor( 0, 0, 0, 200 ) );
    }
    blurred = QPixmap::fromImage( view );
    blurred.setDevicePixelRatio( dpr );
    blurredFor = size();
    return blurred;
}

void MusicView::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    p.setRenderHint( QPainter::SmoothPixmapTransform );
    const QPixmap bg = backdrop();
    if( bg.isNull() )
        p.fillRect( rect(), Qt::black );
    else
        p.drawPixmap( 0, 0, bg );

    const QRect r = artRect();
    QPainterPath clip;
    clip.addRoundedRect( QRectF( r ), ART_RADIUS, ART_RADIUS );
    p.save();
    p.setClipPath( clip );
    if( art.isNull() )
    {
        p.fillRect( r, QColor( 0x17, 0x17, 0x17 ) );
        const int g = r.width() / 4;
        p.drawPixmap( QRect( r.center() - QPoint( g / 2, g / 2 ), QSize( g, g ) ),
                      Theme::pixmap( "music", g, QColor( 0x3A, 0x3A, 0x3A ) ) );
    }
    else
    {
        /* cover the square, cropping a non-square picture */
        const QSize s = art.size().scaled( r.size(), Qt::KeepAspectRatioByExpanding );
        p.drawImage( QRect( r.center() - QPoint( s.width() / 2, s.height() / 2 ), s ), art );
    }
    p.restore();

    QFont f = font();
    f.setPixelSize( CAPTION_FONT );
    p.setFont( f );
    p.setPen( Theme::colors().text );
    const QRect cap( 0, r.bottom() + 1 + CAPTION_GAP - 4, width(), CAPTION_LINE + 8 );
    p.drawText( cap, Qt::AlignHCenter | Qt::AlignVCenter,
                QFontMetrics( f ).elidedText( caption, Qt::ElideRight, width() - 2 * SIDE_MARGIN ) );
}

void MusicView::contextMenuEvent( QContextMenuEvent *e )
{
    THEDP->setPopupMenu();
    e->accept();
}
