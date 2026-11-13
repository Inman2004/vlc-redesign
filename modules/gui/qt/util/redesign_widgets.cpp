/*****************************************************************************
 * redesign_widgets.cpp : small controls of the redesigned interface
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

#include "util/redesign_widgets.hpp"
#include "styles/vlc_theme.hpp"
#include "util/frame_animation.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStyleOptionSlider>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QColorDialog>
#include <QRegularExpressionValidator>
#include <QTransform>

static QColor mixed( const QColor &a, const QColor &b, qreal t )
{
    return QColor::fromRgbF( a.redF()   * ( 1 - t ) + b.redF()   * t,
                             a.greenF() * ( 1 - t ) + b.greenF() * t,
                             a.blueF()  * ( 1 - t ) + b.blueF()  * t );
}

/**********************************************************************
 * AdjustSlider
 **********************************************************************/
AdjustSlider::AdjustSlider( QWidget *parent )
    : QSlider( Qt::Horizontal, parent ), hover( 0 ), origin( 0 ),
      hasOrigin( false ), ticks( 0 ), resetValue( 0 ), hasReset( false )
{
    setAttribute( Qt::WA_Hover );
    setFocusPolicy( Qt::StrongFocus );
    setFixedHeight( HANDLE_H );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );

    hoverAnim = new FrameAnimation( this );
    hoverAnim->setDuration( 120 );
    hoverAnim->setEasingCurve( QEasingCurve::OutCubic );
    connect( hoverAnim, &FrameAnimation::valueChanged, this, [this]( const QVariant &v ) {
        hover = v.toReal();
        update();
    } );
}

void AdjustSlider::setOrigin( int value ) { origin = value; hasOrigin = true; update(); }
void AdjustSlider::setTicks( int intervals ) { ticks = intervals; update(); }
void AdjustSlider::setResetValue( int value ) { resetValue = value; hasReset = true; }

QSize AdjustSlider::sizeHint() const { return QSize( 320, HANDLE_H ); }
QSize AdjustSlider::minimumSizeHint() const { return QSize( 80, HANDLE_H ); }

qreal AdjustSlider::xFor( int value ) const
{
    const int span = maximum() - minimum();
    const qreal f = span > 0 ? qreal( value - minimum() ) / span : 0.;
    return HANDLE_W / 2. + f * ( width() - HANDLE_W );
}

int AdjustSlider::valueAt( int x ) const
{
    const qreal usable = qMax( 1, width() - HANDLE_W );
    const qreal f = qBound( 0., ( x - HANDLE_W / 2. ) / usable, 1. );
    return minimum() + qRound( f * ( maximum() - minimum() ) );
}

void AdjustSlider::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    const Theme::Palette &c = Theme::colors();
    const bool on = isEnabled();

    const QRectF track( 0, ( height() - TRACK_H ) / 2., width(), TRACK_H );
    QPainterPath groove;
    groove.addRoundedRect( track, TRACK_H / 2., TRACK_H / 2. );
    p.fillPath( groove, c.track );

    const qreal hx = xFor( value() );
    if( on )
    {
        const qreal ox = xFor( hasOrigin ? origin : minimum() );
        QRectF fill( qMin( ox, hx ), track.top(), qAbs( hx - ox ), TRACK_H );
        if( !hasOrigin || origin == minimum() )
            fill.setLeft( track.left() );
        p.save();
        p.setClipPath( groove );
        p.fillRect( fill, Theme::seekFill() );
        p.restore();
    }

    if( ticks > 1 )
    {
        p.setPen( QPen( QColor( 255, 255, 255, on ? 22 : 14 ), 1 ) );
        const qreal tickH = 8;
        for( int i = 1; i < ticks; i++ )
        {
            const qreal x = qRound( HANDLE_W / 2. + ( width() - HANDLE_W ) * qreal( i ) / ticks ) + .5;
            p.drawLine( QPointF( x, height() / 2. - tickH / 2 ), QPointF( x, height() / 2. + tickH / 2 ) );
        }
    }

    /* handle: 8x24, a touch wider while hovered or dragged */
    const qreal grow = isSliderDown() ? 1. : hover;
    const qreal w = HANDLE_W + 2 * grow;
    const QRectF handle( hx - w / 2, 0, w, HANDLE_H );
    QColor hc = on ? mixed( Theme::accentPressed(), Theme::accent(), grow )
                   : QColor( 0x70, 0x70, 0x70 );
    p.setPen( Qt::NoPen );
    p.setBrush( hc );
    p.drawRoundedRect( handle, 2, 2 );

    if( hasFocus() && on && focusPolicy() != Qt::NoFocus
     && ( hasFocus() && !underMouse() ) )
    {
        p.setBrush( Qt::NoBrush );
        p.setPen( QPen( mixed( c.track, Theme::accent(), .6 ), 1 ) );
        p.drawRoundedRect( handle.adjusted( -2.5, .5, 2.5, -.5 ), 3, 3 );
    }
}

void AdjustSlider::mousePressEvent( QMouseEvent *e )
{
    if( e->button() != Qt::LeftButton )
        return QSlider::mousePressEvent( e );
    setSliderDown( true );
    setValue( valueAt( e->pos().x() ) );
    e->accept();
}

void AdjustSlider::mouseMoveEvent( QMouseEvent *e )
{
    if( !isSliderDown() )
        return QSlider::mouseMoveEvent( e );
    setValue( valueAt( e->pos().x() ) );
    e->accept();
}

void AdjustSlider::mouseReleaseEvent( QMouseEvent *e )
{
    if( !isSliderDown() )
        return QSlider::mouseReleaseEvent( e );
    setSliderDown( false );
    update();
    e->accept();
}

void AdjustSlider::mouseDoubleClickEvent( QMouseEvent *e )
{
    if( e->button() == Qt::LeftButton )
    {
        setValue( hasReset ? resetValue : ( hasOrigin ? origin : minimum() ) );
        e->accept();
    }
}

void AdjustSlider::wheelEvent( QWheelEvent *e )
{
    if( !hasFocus() )
    {
        e->ignore(); /* let the panel scroll */
        return;
    }
    QSlider::wheelEvent( e );
}

void AdjustSlider::animateHover( bool in )
{
    hoverAnim->stop();
    hoverAnim->setStartValue( hover );
    hoverAnim->setEndValue( in ? 1. : 0. );
    hoverAnim->start();
}

void AdjustSlider::enterEvent( QEvent *e ) { animateHover( true ); QSlider::enterEvent( e ); }
void AdjustSlider::leaveEvent( QEvent *e ) { animateHover( false ); QSlider::leaveEvent( e ); }

/**********************************************************************
 * ToggleSwitch
 **********************************************************************/
ToggleSwitch::ToggleSwitch( QWidget *parent )
    : QAbstractButton( parent ), pos( 0 ), hovered( false )
{
    setCheckable( true );
    setCursor( Qt::PointingHandCursor );
    setFocusPolicy( Qt::StrongFocus );
    setFixedSize( sizeHint() );
    anim = new FrameAnimation( this );
    anim->setDuration( 140 );
    anim->setEasingCurve( QEasingCurve::OutCubic );
    connect( anim, &FrameAnimation::valueChanged, this, [this]( const QVariant &v ) {
        pos = v.toReal();
        update();
    } );
}

QSize ToggleSwitch::sizeHint() const { return QSize( 68, 32 ); }

void ToggleSwitch::animateTo( bool on )
{
    anim->stop();
    if( !isVisible() )
    {
        pos = on ? 1 : 0;
        update();
        return;
    }
    anim->setStartValue( pos );
    anim->setEndValue( on ? 1. : 0. );
    anim->start();
}

void ToggleSwitch::checkStateSet()
{
    /* programmatic setChecked: jump, no animation */
    anim->stop();
    pos = isChecked() ? 1 : 0;
    update();
}

void ToggleSwitch::nextCheckState()
{
    setCheckedAnimated( !isChecked() );
}

void ToggleSwitch::setCheckedAnimated( bool b )
{
    if( b == isChecked() )
        return;
    const qreal from = pos;
    setChecked( b );      /* checkStateSet() jumps the knob... */
    pos = from;           /* ...so slide it from where it was */
    animateTo( b );
}

void ToggleSwitch::enterEvent( QEvent *e ) { hovered = true; update(); QAbstractButton::enterEvent( e ); }
void ToggleSwitch::leaveEvent( QEvent *e ) { hovered = false; update(); QAbstractButton::leaveEvent( e ); }

void ToggleSwitch::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    if( !isEnabled() )
        p.setOpacity( .45 );
    const Theme::Palette &c = Theme::colors();

    /* #090909 well, radius 6; a ring only for keyboard focus */
    p.setPen( Qt::NoPen );
    p.setBrush( c.track );
    p.drawRoundedRect( QRectF( rect() ), 6, 6 );
    if( hasFocus() && !hovered )
    {
        p.setBrush( Qt::NoBrush );
        p.setPen( QPen( mixed( c.track, Theme::accent(), .55 ), 1 ) );
        p.drawRoundedRect( QRectF( rect() ).adjusted( .5, .5, -.5, -.5 ), 6, 6 );
    }

    /* knob 26x26, 3px inset */
    const qreal k = 26;
    const qreal x = 3 + pos * ( width() - 6 - k );
    const QRectF knob( x, ( height() - k ) / 2., k, k );
    QColor off = hovered ? QColor( 0x4C, 0x4C, 0x4C ) : QColor( 0x40, 0x40, 0x40 );
    QColor onC = hovered ? Theme::accentHover() : Theme::accent();
    p.setPen( Qt::NoPen );
    p.setBrush( mixed( off, onC, pos ) );
    p.drawRoundedRect( knob, 4, 4 );

    /* label in the free half */
    QFont f = font();
    f.setPixelSize( 13 );
    p.setFont( f );
    const qreal free0 = pos > .5 ? 3 : x + k;
    const qreal free1 = pos > .5 ? x : width() - 3;
    QColor tc = pos > .5 ? c.textMuted : c.textFaint;
    tc.setAlphaF( qAbs( pos - .5 ) * 2 );
    p.setPen( tc );
    p.drawText( QRectF( free0, 0, free1 - free0, height() ), Qt::AlignCenter,
                pos > .5 ? tr( "ON" ) : tr( "OFF" ) );
}

/**********************************************************************
 * CollapsibleSection
 **********************************************************************/
CollapsibleSection::CollapsibleSection( QWidget *parent )
    : QWidget( parent ), angle( 0 ), expanded( false )
{
    setObjectName( "AdjustCard" );
    setAttribute( Qt::WA_StyledBackground, true );

    QVBoxLayout *outer = new QVBoxLayout( this );
    outer->setContentsMargins( 0, 0, 0, 0 );
    outer->setSpacing( 0 );

    header = new QWidget( this );
    header->setObjectName( "AdjustCardHeader" );
    header->setCursor( Qt::PointingHandCursor );
    header->setFixedHeight( 48 );
    header->setAttribute( Qt::WA_Hover );
    header->installEventFilter( this );
    QHBoxLayout *hl = new QHBoxLayout( header );
    hl->setContentsMargins( 16, 0, 14, 0 );
    headerText = new QLabel( header );
    headerText->setObjectName( "AdjustCardTitle" );
    chevron = new QLabel( header );
    chevron->setFixedSize( 16, 16 );
    hl->addWidget( headerText );
    hl->addStretch( 1 );
    hl->addWidget( chevron );
    outer->addWidget( header );

    /* bodyHost clips; bodyInner keeps its full height, so the content
     * never gets squeezed while the section opens or closes */
    bodyHost = new QWidget( this );
    bodyInner = new QWidget( bodyHost );
    body = new QVBoxLayout( bodyInner );
    body->setContentsMargins( 16, 0, 16, 18 );
    body->setSpacing( 14 );
    bodyHost->setFixedHeight( 0 );
    bodyHost->hide();
    bodyHost->installEventFilter( this );
    bodyInner->installEventFilter( this );
    outer->addWidget( bodyHost );

    anim = new FrameAnimation( this );
    anim->setDuration( 220 );
    anim->setEasingCurve( QEasingCurve::OutCubic );
    connect( anim, &FrameAnimation::valueChanged, this, [this]( const QVariant &v ) {
        const qreal t = v.toReal();
        angle = 90 * t;
        updateHeader();
        bodyHost->setFixedHeight( qRound( innerHeight() * t ) );
    } );
    connect( anim, &FrameAnimation::finished, this, [this]() {
        if( expanded )
            bodyHost->setFixedHeight( innerHeight() );
        else
            bodyHost->hide();
    } );
    updateHeader();
}

void CollapsibleSection::updateHeader()
{
    headerText->setText( expanded ? tr( "Collapse Settings" ) : tr( "Expand Settings" ) );
    const bool hot = header->underMouse();
    QPixmap px = Theme::pixmap( "chevron", 16, hot ? Theme::colors().textMuted
                                                  : Theme::colors().textFaint );
    if( angle > 0 )
    {
        QPixmap rot( px.size() );
        rot.setDevicePixelRatio( px.devicePixelRatio() );
        rot.fill( Qt::transparent );
        QPainter p( &rot );
        p.setRenderHint( QPainter::SmoothPixmapTransform );
        p.setRenderHint( QPainter::Antialiasing );
        p.translate( 8, 8 );
        p.rotate( angle );
        p.translate( -8, -8 );
        p.drawPixmap( 0, 0, px );
        p.end();
        px = rot;
    }
    chevron->setPixmap( px );
}

void CollapsibleSection::setExpanded( bool b, bool animate )
{
    if( b == expanded && anim->state() != QAbstractAnimation::Running )
        return;
    expanded = b;
    const qreal from = angle / 90.;
    bodyHost->show();
    placeInner();
    anim->stop();
    if( !animate || !isVisible() )
    {
        angle = b ? 90 : 0;
        bodyHost->setFixedHeight( b ? innerHeight() : 0 );
        bodyHost->setVisible( b );
        updateHeader();
    }
    else
    {
        anim->setStartValue( from );
        anim->setEndValue( b ? 1. : 0. );
        anim->start();
    }
    emit expandedChanged( b );
}

int CollapsibleSection::innerHeight() const
{
    const int w = bodyHost->width();
    return bodyInner->hasHeightForWidth() ? bodyInner->heightForWidth( w )
                                          : bodyInner->sizeHint().height();
}

void CollapsibleSection::placeInner()
{
    bodyInner->setGeometry( 0, 0, bodyHost->width(), innerHeight() );
}

bool CollapsibleSection::eventFilter( QObject *o, QEvent *e )
{
    if( o == bodyHost && e->type() == QEvent::Resize )
        placeInner();
    else if( o == bodyInner && e->type() == QEvent::LayoutRequest )
    {
        /* the content changed: follow it when open and still */
        bodyHost->setMinimumWidth( bodyInner->minimumSizeHint().width() );
        placeInner();
        if( expanded && anim->state() != QAbstractAnimation::Running )
            bodyHost->setFixedHeight( innerHeight() );
    }
    if( o == header )
    {
        switch( e->type() )
        {
        case QEvent::MouseButtonRelease:
            if( static_cast<QMouseEvent *>( e )->button() == Qt::LeftButton )
                toggle();
            return true;
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
            updateHeader();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter( o, e );
}

/**********************************************************************
 * ColorField
 **********************************************************************/
ColorField::ColorField( QWidget *parent ) : QWidget( parent )
{
    setObjectName( "AdjustColor" );
    setAttribute( Qt::WA_StyledBackground, true );
    setFixedSize( 140, 36 );
    QHBoxLayout *l = new QHBoxLayout( this );
    l->setContentsMargins( 14, 0, 6, 0 );
    l->setSpacing( 4 );

    edit = new QLineEdit( this );
    edit->setObjectName( "AdjustColorText" );
    edit->setFrame( false );
    edit->setMaxLength( 7 );
    edit->setValidator( new QRegularExpressionValidator(
                            QRegularExpression( "#?[0-9A-Fa-f]{0,6}" ), edit ) );
    connect( edit, &QLineEdit::editingFinished, this, &ColorField::commitText );

    swatch = new QToolButton( this );
    swatch->setObjectName( "AdjustSwatch" );
    swatch->setFixedSize( 24, 24 );
    swatch->setCursor( Qt::PointingHandCursor );
    swatch->setToolTip( tr( "Pick a colour" ) );
    connect( swatch, &QToolButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor( current, window(), QString() );
        if( c.isValid() )
        {
            setColor( c );
            emit colorChanged( current );
        }
    } );

    l->addWidget( edit, 1 );
    l->addWidget( swatch );
    setColor( Qt::white );
}

void ColorField::setColor( const QColor &c )
{
    current = c;
    edit->setText( c.name().toUpper() );
    updateSwatch();
}

void ColorField::commitText()
{
    QString t = edit->text();
    if( !t.startsWith( '#' ) )
        t.prepend( '#' );
    QColor c( t );
    if( t.size() != 7 || !c.isValid() )
    {
        edit->setText( current.name().toUpper() ); /* revert */
        return;
    }
    if( c != current )
    {
        setColor( c );
        emit colorChanged( current );
    }
}

void ColorField::updateSwatch()
{
    swatch->setStyleSheet( QString( "QToolButton#AdjustSwatch { background: %1;"
                                    " border: 1px solid #404040; border-radius: 3px; }"
                                    "QToolButton#AdjustSwatch:hover { border-color: #8F8F8F; }" )
                               .arg( current.name() ) );
}
