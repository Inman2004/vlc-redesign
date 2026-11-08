/*****************************************************************************
 * resume_card.cpp : "You already watched till ..." card (P5b)
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

#include "components/resume_card.hpp"
#include "styles/vlc_theme.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>

/* Figma "continue": padding 20/10/10/10, 60 between the text and the
 * buttons, 10 between the buttons; 16px corners */
enum
{
    CARD_H      = 52,
    CARD_RADIUS = 16,
    ABOVE_BAR   = 24,
    BUTTON_H    = 32,
    CLOSE_W     = 44,
    CARD_W      = 552,
};

ResumeCard::ResumeCard( QWidget *_window, QWidget *_bar )
    : QWidget( _window, Qt::Tool | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint ),
      window( _window ), bar( _bar )
{
    setObjectName( "ResumeCard" );
    setAttribute( Qt::WA_TranslucentBackground );
    setAttribute( Qt::WA_ShowWithoutActivating );
    setFocusPolicy( Qt::NoFocus );

    QHBoxLayout *l = new QHBoxLayout( this );
    l->setContentsMargins( 20, 10, 10, 10 );
    l->setSpacing( 0 );

    text = new QLabel( this );
    text->setObjectName( "ResumeText" );
    l->addWidget( text );
    l->addSpacing( 60 );
    l->addStretch( 1 );

    QPushButton *go = new QPushButton( qtr( "Continue Watching" ), this );
    go->setObjectName( "ResumeContinue" );
    go->setFixedHeight( BUTTON_H );
    go->setCursor( Qt::PointingHandCursor );
    go->setFocusPolicy( Qt::NoFocus );
    l->addWidget( go );
    l->addSpacing( 10 );

    QToolButton *close = new QToolButton( this );
    close->setObjectName( "ResumeClose" );
    close->setIcon( Theme::icon( "win-close", 24 ) );
    close->setIconSize( QSize( 24, 24 ) );
    close->setFixedSize( CLOSE_W, BUTTON_H );
    close->setCursor( Qt::PointingHandCursor );
    close->setFocusPolicy( Qt::NoFocus );
    close->setToolTip( qtr( "Start from the beginning" ) );
    l->addWidget( close );

    connect( go, &QPushButton::clicked, this, &ResumeCard::continueRequested );
    connect( close, &QToolButton::clicked, this, &ResumeCard::dismissed );

    setFixedHeight( CARD_H );
    window->installEventFilter( this );
    hide();
}

void ResumeCard::showFor( int64_t time )
{
    const int s = int( time / 1000000 );
    const QString t = QString( "%1:%2:%3" ).arg( s / 3600, 2, 10, QChar( '0' ) )
                                           .arg( ( s / 60 ) % 60, 2, 10, QChar( '0' ) )
                                           .arg( s % 60, 2, 10, QChar( '0' ) );
    text->setText( qtr( "You already watched till %1." ).arg( t ) );
    /* the design's width; longer translations grow it */
    setFixedWidth( qMax( int( CARD_W ), layout()->sizeHint().width() ) );
    place();
    if( window->isVisible() && !window->isMinimized() )
    {
        show();
        raise();
    }
}

/* centred on the window, 24px above the bar (or the window's bottom when
 * the bar is hidden, as in fullscreen) */
void ResumeCard::place()
{
    const QRect w( window->mapToGlobal( QPoint( 0, 0 ) ), window->size() );
    int bottom = w.bottom() + 1;
    if( bar && bar->isVisible() )
        bottom = bar->mapToGlobal( QPoint( 0, 0 ) ).y();
    move( w.left() + ( w.width() - width() ) / 2, bottom - ABOVE_BAR - height() );
}

bool ResumeCard::eventFilter( QObject *o, QEvent *e )
{
    if( o == window && isVisible() )
    {
        switch( e->type() )
        {
        case QEvent::Move:
        case QEvent::Resize:
            place();
            break;
        case QEvent::Hide:
        case QEvent::WindowDeactivate:
            /* above the window manager: never over other applications */
            emit dismissed();
            break;
        case QEvent::WindowStateChange:
            if( window->isMinimized() )
                emit dismissed();
            else
                place();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter( o, e );
}

void ResumeCard::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    p.setPen( Qt::NoPen );
    p.setBrush( QColor( 0x19, 0x19, 0x19, 242 ) );   /* #191919 at 95% */
    p.drawRoundedRect( QRectF( rect() ), CARD_RADIUS, CARD_RADIUS );
}
