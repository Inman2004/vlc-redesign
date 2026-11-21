/*****************************************************************************
 * frame_animation.cpp : animations timed to the display's refresh rate
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

#include "util/frame_animation.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QLayout>
#include <QPainter>
#include <QScreen>
#include <QVariantAnimation>
#include <QWindow>

#include <cmath>

FrameAnimation::FrameAnimation( QObject *parent )
    : QObject( parent )
{
    curve = new QVariantAnimation( this );
    curve->setDuration( 1000 );     /* driven as a 0..1000 progress scale */
    timer.setTimerType( Qt::PreciseTimer );
    connect( &timer, &QTimer::timeout, this, &FrameAnimation::tick );
}

void FrameAnimation::setEasingCurve( const QEasingCurve &c ) { curve->setEasingCurve( c ); }
void FrameAnimation::setStartValue( const QVariant &v ) { curve->setStartValue( v ); }
void FrameAnimation::setEndValue( const QVariant &v ) { curve->setEndValue( v ); }
QVariant FrameAnimation::startValue() const { return curve->startValue(); }
QVariant FrameAnimation::endValue() const { return curve->endValue(); }
QVariant FrameAnimation::currentValue() const { return curve->currentValue(); }

int FrameAnimation::refreshInterval( const QWidget *w )
{
    /* the refresh rate of the screen the widget is on */
    QScreen *screen = NULL;
    if( w && w->window()->windowHandle() )
        screen = w->window()->windowHandle()->screen();
    if( !screen )
        screen = QGuiApplication::primaryScreen();
    const qreal hz = screen && screen->refreshRate() > 1 ? screen->refreshRate() : 60.;
    /* a little under the refresh period: never skip a refresh */
    return qBound( 4, int( std::floor( 1000. / hz ) ) - 1, 16 );
}

int FrameAnimation::frameInterval() const
{
    const QWidget *w = NULL;
    for( QObject *o = parent(); o && !w; o = o->parent() )
        if( o->isWidgetType() )
            w = static_cast<QWidget *>( o );
    return refreshInterval( w );
}

void FrameAnimation::start( QAbstractAnimation::DeletionPolicy policy )
{
    deleteWhenStopped = policy == QAbstractAnimation::DeleteWhenStopped;
    timer.stop();
    /* the first value at once: no blank first frame */
    curve->setCurrentTime( 0 );
    emit valueChanged( curve->currentValue() );
    if( duration <= 0 )
    {
        tick();
        return;
    }
    clock.start();
    timer.start( frameInterval() );
}

void FrameAnimation::stop()
{
    timer.stop();
}

void FrameAnimation::tick()
{
    const qint64 t = duration > 0 ? clock.elapsed() : 0;
    if( duration <= 0 || t >= duration )
    {
        timer.stop();
        curve->setCurrentTime( 1000 );
        emit valueChanged( curve->currentValue() );
        emit finished();
        if( deleteWhenStopped )
            deleteLater();
        return;
    }
    curve->setCurrentTime( int( t * 1000 / duration ) );
    emit valueChanged( curve->currentValue() );
}

/*********************************************************************
 * SnapshotOverlay
 *********************************************************************/
SnapshotOverlay::SnapshotOverlay( QWidget *host, const QPixmap &_still, const QColor &bg )
    : QWidget( host ), still( _still ), background( bg )
{
    setAttribute( Qt::WA_TransparentForMouseEvents );
    if( background.isValid() && background.alpha() == 255 )
        setAttribute( Qt::WA_OpaquePaintEvent );
    setGeometry( host->rect() );
    raise();
    show();
}

QPixmap SnapshotOverlay::grabOf( QWidget *w )
{
    /* lay the widget out at its current size before taking the still */
    QCoreApplication::sendPostedEvents( NULL, QEvent::LayoutRequest );
    if( w->layout() )
        w->layout()->activate();
    return w->grab();
}

void SnapshotOverlay::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    if( background.isValid() )
        p.fillRect( rect(), background );
    p.setOpacity( opacity );
    /* whole device pixels: a fractional offset would resample the still */
    const qreal dpr = still.devicePixelRatio();
    const QPointF o( std::round( offset.x() * dpr ) / dpr, std::round( offset.y() * dpr ) / dpr );
    p.drawPixmap( o, still );
}

/*********************************************************************
 * SlideDock
 *********************************************************************/
SlideDock::SlideDock( QWidget *parent )
    : QWidget( parent )
{
    setFixedWidth( 0 );
    anim = new FrameAnimation( this );
    anim->setDuration( DURATION );
    anim->setEasingCurve( QEasingCurve::OutCubic );
    connect( anim, &FrameAnimation::valueChanged, this, [this]( const QVariant &v ) {
        shift = v.toReal();
        place();
    } );
    connect( anim, &FrameAnimation::finished, this, [this]() {
        if( !open )
        {
            /* the width goes back to the content in one step */
            setFixedWidth( 0 );
            hide();
        }
    } );
    hide();
}

void SlideDock::setPanel( QWidget *w, int width )
{
    panelWidget = w;
    panelWidth = width;
    place();
}

void SlideDock::setOpen( bool b, bool animate )
{
    const int full = panelWidth + GAP;
    open = b;
    anim->stop();
    if( !animate || !window()->isVisible() )
    {
        shift = 0;
        setFixedWidth( b ? full : 0 );
        setVisible( b );
        place();
        return;
    }
    if( b )
    {
        if( !isVisible() )
            shift = full;           /* start out of sight */
        setFixedWidth( full );      /* the one relayout */
        show();
        anim->setEasingCurve( QEasingCurve::OutCubic );
        anim->setStartValue( shift );
        anim->setEndValue( 0. );
    }
    else
    {
        anim->setEasingCurve( QEasingCurve::InOutCubic );
        anim->setStartValue( shift );
        anim->setEndValue( qreal( full ) );
    }
    anim->start();
}

void SlideDock::place()
{
    /* inside the contents rect: the window's floating bars pad it */
    const QRect r = contentsRect();
    if( panelWidget )
        panelWidget->setGeometry( width() - panelWidth + qRound( shift ), r.top(),
                                  panelWidth, r.height() );
}

void SlideDock::resizeEvent( QResizeEvent *e )
{
    QWidget::resizeEvent( e );
    place();
}
