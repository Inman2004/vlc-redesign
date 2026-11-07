/*****************************************************************************
 * Copyright © 2011-2012 VideoLAN
 * $Id$
 *
 * Authors: Ludovic Fauvet <etix@l0cal.com>
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "timetooltip.hpp"
#include "styles/vlc_theme.hpp"
#include "util/seek_preview.hpp"

#include <QApplication>
#include <QPainter>
#include <QBitmap>
#include <QFontMetrics>
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
#include <QDesktopWidget>
#else
#include <QScreen>
#endif

#define TIP_HEIGHT 5

TimeTooltip::TimeTooltip( QWidget *parent ) :
    QWidget( parent )
{
    setWindowFlags( Qt::ToolTip                 |
                    Qt::WindowStaysOnTopHint    |
                    Qt::FramelessWindowHint     );

    // Tell Qt that it doesn't need to erase the background before
    // a paintEvent occurs. This should save some CPU cycles.
    setAttribute( Qt::WA_OpaquePaintEvent );
    setAttribute( Qt::WA_TranslucentBackground );
    setAttribute( Qt::WA_TransparentForMouseEvents );

    // Inherit from the system default font size -5
    mFont = QFont( "Verdana", qMax( qApp->font().pointSize() - 5, 7 ) );
    mTipX = -1;
    mPreview = false;
    mThumbSize = QSize( SeekPreview::MAX_W, SeekPreview::MAX_H );
    if( Theme::redesign() )
    {
        mFont = qApp->font();
        mFont.setPixelSize( 14 );
    }

    // By default the widget is unintialized and should not be displayed
    resize( 0, 0 );
}

void TimeTooltip::adjustPosition()
{
    if( mDisplayedText.isEmpty() )
    {
        resize( 0, 0 );
        return;
    }

    if( Theme::redesign() )
    {
        /* Components column: a 36px #0F0F0F pill with the time, or a
         * #191919 card with the thumbnail over the time; 8px of shadow */
        QFontMetrics metrics( mFont );
        const int textW = metrics.horizontalAdvance( mDisplayedText );
        QSize card;
        if( mPreview )
            card = QSize( qMax( mThumbSize.width() + 12, textW + 32 ),
                          mThumbSize.height() + 6 + 36 );
        else
            card = QSize( qMax( 96, textW + 32 ), 36 );
        const int pad = 8;
        QSize size( card.width() + 2 * pad, card.height() + 2 * pad );
        QPoint position( mTarget.x() - size.width() / 2,
                         mTarget.y() - size.height() + pad - 10 );
        QRect screen{};
        if( QScreen *sc = QGuiApplication::screenAt( mTarget ) )
            screen = sc->geometry();
        if( screen.isValid() )
            position.setX( qMax( screen.left(), qMin( position.x(),
                           screen.left() + screen.width() - size.width() ) ) );
        move( position );
        mBox = QRect( QPoint( pad, pad ), card );
        if( this->size() != size )
            resize( size );
        return;
    }

    // Get the bounding box required to print the text and add some padding
    QFontMetrics metrics( mFont );
    QRect textbox = metrics.boundingRect( mDisplayedText );
    textbox.adjust( -2, -2, 2, 2 );
    textbox.moveTo( 0, 0 );

    // Resize the widget to fit our needs
    QSize size( textbox.width() + 1, textbox.height() + TIP_HEIGHT + 1 );

    // The desired label position is just above the target
    QPoint position( mTarget.x() - size.width() / 2,
#if defined( Q_OS_WIN )
        mTarget.y() - 2 * size.height() - TIP_HEIGHT / 2 );
#else
        mTarget.y() - size.height() - TIP_HEIGHT / 2 );
#endif

    // Keep the tooltip on the same screen if possible
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QRect screen{};
    auto *screenAt = QGuiApplication::screenAt( mTarget );
    if (screenAt != nullptr)
        screen = screenAt->geometry();
#else
    QRect screen = QApplication::desktop()->screenGeometry( mTarget );
#endif
    position.setX( qMax( screen.left(), qMin( position.x(),
        screen.left() + screen.width() - size.width() ) ) );
    position.setY( qMax( screen.top(), qMin( position.y(),
        screen.top() + screen.height() - size.height() ) ) );

    move( position );

    int tipX = mTarget.x() - position.x();
    if( mBox != textbox || mTipX != tipX )
    {
        mBox = textbox;
        mTipX = tipX;

        resize( size );
        buildPath();
    }
}

void TimeTooltip::buildPath()
{
    // Prepare the painter path for future use so
    // we only have to generate the text at runtime.

    // Draw the text box
    mPainterPath = QPainterPath();
    mPainterPath.addRect( mBox );

    // Draw the tip
    QPolygonF polygon;
    polygon << QPoint( qMax( 0, mTipX - 3 ), mBox.height() )
            << QPoint( mTipX, mBox.height() + TIP_HEIGHT )
            << QPoint( qMin( mTipX + 3, mBox.width() ), mBox.height() );
    mPainterPath.addPolygon( polygon );

    // Store the simplified version of the path
    mPainterPath = mPainterPath.simplified();
}

void TimeTooltip::setTip( const QPoint& target, const QString& time, const QString& text )
{
    mDisplayedText = time;
    if ( !text.isEmpty() )
        mDisplayedText.append( " - " ).append( text );

    if( mTarget != target || time.length() != mTime.length() || mText != text )
    {
        mTarget = target;
        mTime = time;
        mText = text;
        adjustPosition();
    }

    update();
    raise();
}

void TimeTooltip::show()
{
    setVisible( true );
    raise();
}

void TimeTooltip::setPreviewEnabled( bool b )
{
    if( mPreview == b )
        return;
    mPreview = b;
    if( !b )
        mThumb = QImage();
    adjustPosition();
    update();
}

void TimeTooltip::setThumbnail( const QImage &img )
{
    mThumb = img;
    if( !img.isNull() )
    {
        /* keep the card height steady: fit the picture in the 16:9 area */
        QSize s = img.size().scaled( SeekPreview::MAX_W, SeekPreview::MAX_H,
                                     Qt::KeepAspectRatio );
        if( s.width() != mThumbSize.width() )
        {
            mThumbSize = QSize( s.width(), SeekPreview::MAX_H );
            adjustPosition();
        }
    }
    update();
}

void TimeTooltip::paintEvent( QPaintEvent * )
{
    if( Theme::redesign() )
    {
        QPainter p( this );
        p.setRenderHint( QPainter::Antialiasing );
        p.setCompositionMode( QPainter::CompositionMode_Source );
        p.fillRect( rect(), Qt::transparent );
        p.setCompositionMode( QPainter::CompositionMode_SourceOver );
        const int radius = mPreview ? 10 : 7;
        /* soft shadow */
        for( int i = 8; i > 0; --i )
        {
            QColor c( 0, 0, 0, 10 + ( 8 - i ) * 3 );
            p.setPen( Qt::NoPen );
            p.setBrush( c );
            p.drawRoundedRect( QRectF( mBox ).adjusted( -i, -i + 2, i, i + 2 ),
                               radius + i, radius + i );
        }
        p.setBrush( mPreview ? QColor( 0x19, 0x19, 0x19 ) : QColor( 0x0F, 0x0F, 0x0F ) );
        p.drawRoundedRect( QRectF( mBox ), radius, radius );

        QRect textRect = mBox;
        if( mPreview )
        {
            const QRect area( mBox.left() + ( mBox.width() - mThumbSize.width() ) / 2,
                              mBox.top() + 6, mThumbSize.width(), mThumbSize.height() );
            QPainterPath clip;
            clip.addRoundedRect( QRectF( area ), 5, 5 );
            p.save();
            p.setClipPath( clip );
            p.fillRect( area, QColor( 0x25, 0x24, 0x23 ) );
            if( !mThumb.isNull() )
            {
                QSize s = mThumb.size().scaled( area.size(), Qt::KeepAspectRatio );
                QRect r( QPoint( 0, 0 ), s );
                r.moveCenter( area.center() );
                p.setRenderHint( QPainter::SmoothPixmapTransform );
                p.drawImage( r, mThumb );
            }
            p.restore();
            textRect = QRect( mBox.left(), area.bottom() + 1, mBox.width(),
                              mBox.bottom() - area.bottom() );
        }
        p.setFont( mFont );
        p.setPen( QColor( 0x8F, 0x8F, 0x8F ) );
        p.drawText( textRect, Qt::AlignCenter, mDisplayedText );
        return;
    }

    QPainter p( this );
    p.setRenderHints( QPainter::TextAntialiasing );

    p.setPen( Qt::black );
    p.setBrush( qApp->palette().base() );
    p.drawPath( mPainterPath );

    p.setFont( mFont );
    p.setPen( QPen( qApp->palette().text(), 1 ) );
    p.drawText( mBox, Qt::AlignCenter, mDisplayedText );
}
