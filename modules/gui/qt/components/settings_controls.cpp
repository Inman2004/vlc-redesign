/*****************************************************************************
 * settings_controls.cpp : the controls of the redesigned Settings window (P6)
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

#include "qt.hpp"
#include "components/settings_controls.hpp"
#include "styles/vlc_theme.hpp"

#include <QColorDialog>
#include <QDoubleValidator>
#include <QHelpEvent>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStylePainter>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>

#include <cmath>

using namespace SettingsUI;

/*********************************************************************
 * Tokens
 *********************************************************************/
Colors SettingsUI::colors()
{
    Colors c;
    if( Theme::isDark() )
    {
        c.window          = QColor( "#0E0E0E" );
        c.header          = QColor( "#090909" );
        c.nav             = QColor( "#181818" );
        c.navSelected     = QColor( "#222222" );
        c.search          = QColor( "#151515" );
        c.well            = QColor( "#090909" );
        c.surface         = QColor( "#191919" );
        c.knobOff         = QColor( "#404040" );
        c.text            = QColor( "#8F8F8F" );
        c.strong          = QColor( "#C6C6C6" );
        c.title           = QColor( "#969696" );
        c.navSelectedText = QColor( "#EAEAEA" );
        c.faint           = QColor( "#6B6B6B" );
        c.info            = QColor( "#404040" );
    }
    else
    {
        c.window          = QColor( "#F6F5F4" );
        c.header          = QColor( "#ECEAE8" );
        c.nav             = QColor( "#EDEBE9" );
        c.navSelected     = QColor( "#E0DDDA" );
        c.search          = QColor( "#E3E1DF" );
        c.well            = QColor( "#DEDBD7" );
        c.surface         = QColor( "#FBFAF9" );
        c.knobOff         = QColor( "#B9B5B1" );
        c.text            = QColor( "#55524F" );
        c.strong          = QColor( "#1E1E1E" );
        c.title           = QColor( "#3A3836" );
        c.navSelectedText = QColor( "#111111" );
        c.faint           = QColor( "#85817D" );
        c.info            = QColor( "#AAA6A2" );
    }
    return c;
}

QFont SettingsUI::font( int px, bool medium )
{
    QFont f( "Google Sans" );
    f.setPixelSize( px );
    if( medium )
        f.setWeight( QFont::Medium );
    return f;
}

static void paintWell( QPainter &p, const QRectF &r, const QColor &rim, const QColor &inner,
                       qreal rimWidth = RIM, qreal outerRadius = 6 )
{
    p.setPen( Qt::NoPen );
    p.setBrush( rim );
    p.drawRoundedRect( r, outerRadius, outerRadius );
    p.setBrush( inner );
    const qreal ir = qMax<qreal>( outerRadius - rimWidth, 2 );
    p.drawRoundedRect( r.adjusted( rimWidth, rimWidth, -rimWidth, -rimWidth ), ir, ir );
}

/*********************************************************************
 * SToggle: 66x31; a 3px #090909 rim; OFF = #404040 knob | "OFF" box,
 * ON = "ON" box (accent 26%) | knob (accent 81%)
 *********************************************************************/
SToggle::SToggle( QWidget *parent ) : QAbstractButton( parent ), hovered( false )
{
    setCheckable( true );
    setCursor( Qt::PointingHandCursor );
    setFocusPolicy( Qt::TabFocus );
    setFixedSize( TOGGLE_W, TOGGLE_H );
}

QSize SToggle::sizeHint() const { return QSize( TOGGLE_W, TOGGLE_H ); }

void SToggle::enterEvent( QEvent *e ) { hovered = true; update(); QAbstractButton::enterEvent( e ); }
void SToggle::leaveEvent( QEvent *e ) { hovered = false; update(); QAbstractButton::leaveEvent( e ); }

void SToggle::paintEvent( QPaintEvent * )
{
    const Colors c = colors();
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    if( !isEnabled() )
        p.setOpacity( 0.4 );
    const qreal rim = 2.9, gap = 2.9, knob = 25.3;
    QRectF r( 0, 0, width(), height() );
    p.setPen( Qt::NoPen );
    p.setBrush( c.well );
    p.drawRoundedRect( r, 6, 6 );
    QRectF inner = r.adjusted( rim, rim, -rim, -rim );
    QRectF knobR, boxR;
    if( isChecked() )
    {
        boxR  = QRectF( inner.left(), inner.top(), inner.width() - knob - gap, inner.height() );
        knobR = QRectF( inner.right() - knob, inner.top(), knob, inner.height() );
    }
    else
    {
        knobR = QRectF( inner.left(), inner.top(), knob, inner.height() );
        boxR  = QRectF( knobR.right() + gap, inner.top(), inner.right() - knobR.right() - gap,
                        inner.height() );
    }
    QColor knobC = isChecked() ? Theme::accentKnob() : c.knobOff;
    if( hovered && isEnabled() )
        knobC = knobC.lighter( 118 );
    p.setBrush( isChecked() ? Theme::accentDim() : c.surface );
    p.drawRoundedRect( boxR, 3, 3 );
    p.setBrush( knobC );
    p.drawRoundedRect( knobR, 3, 3 );
    p.setFont( SettingsUI::font( 11 ) );
    p.setPen( isChecked() ? Theme::accent() : c.faint );
    p.drawText( boxR, Qt::AlignCenter, isChecked() ? qtr( "ON" ) : qtr( "OFF" ) );
    if( hasFocus() )
    {
        p.setPen( QPen( Theme::accent(), 1 ) );
        p.setBrush( Qt::NoBrush );
        p.drawRoundedRect( r.adjusted( 0.5, 0.5, -0.5, -0.5 ), 6, 6 );
    }
}

/*********************************************************************
 * SSegmented: 310x35 well, three 100.4px segments 2.5px apart
 *********************************************************************/
SSegmented::SSegmented( const QStringList &l, QWidget *parent )
    : QWidget( parent ), labels( l ), cur( 0 ), hover( -1 )
{
    setFixedHeight( CONTROL_H );
    setMouseTracking( true );
    setCursor( Qt::PointingHandCursor );
    setFocusPolicy( Qt::TabFocus );
}

QSize SSegmented::sizeHint() const { return QSize( CONTROL_W, CONTROL_H ); }

QRectF SSegmented::segment( int i ) const
{
    const int n = qMax( 1, labels.count() );
    const qreal gap = 2.5;
    QRectF in = QRectF( rect() ).adjusted( RIM, RIM, -RIM, -RIM );
    const qreal w = ( in.width() - gap * ( n - 1 ) ) / n;
    return QRectF( in.left() + i * ( w + gap ), in.top(), w, in.height() );
}

int SSegmented::at( const QPoint &pt ) const
{
    for( int i = 0; i < labels.count(); i++ )
        if( segment( i ).adjusted( -1.5, 0, 1.5, 0 ).contains( pt ) )
            return i;
    return -1;
}

void SSegmented::setCurrent( int i )
{
    if( i == cur || i < 0 || i >= labels.count() )
        return;
    cur = i;
    update();
}

void SSegmented::paintEvent( QPaintEvent * )
{
    const Colors c = colors();
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    if( !isEnabled() )
        p.setOpacity( 0.4 );
    p.setPen( Qt::NoPen );
    p.setBrush( c.well );
    p.drawRoundedRect( rect(), 6, 6 );
    p.setFont( SettingsUI::font( 14 ) );
    for( int i = 0; i < labels.count(); i++ )
    {
        QRectF s = segment( i );
        const bool sel = i == cur;
        QColor bg = sel ? Theme::accentDim() : c.surface;
        if( !sel && i == hover )
            bg = bg.lighter( 125 );
        p.setPen( Qt::NoPen );
        p.setBrush( bg );
        p.drawRoundedRect( s, 4, 4 );
        p.setPen( sel ? Theme::accent() : c.faint );
        p.drawText( s, Qt::AlignCenter, labels[i] );
    }
}

void SSegmented::mousePressEvent( QMouseEvent *e )
{
    const int i = at( e->pos() );
    if( i >= 0 && i != cur )
    {
        cur = i;
        update();
        emit changed( cur );
    }
}

void SSegmented::mouseMoveEvent( QMouseEvent *e )
{
    const int h = at( e->pos() );
    if( h != hover ) { hover = h; update(); }
}

void SSegmented::leaveEvent( QEvent * ) { hover = -1; update(); }

void SSegmented::keyPressEvent( QKeyEvent *e )
{
    int n = cur;
    if( e->key() == Qt::Key_Left ) n--;
    else if( e->key() == Qt::Key_Right ) n++;
    else { QWidget::keyPressEvent( e ); return; }
    if( n >= 0 && n < labels.count() )
    {
        cur = n;
        update();
        emit changed( cur );
    }
}

/*********************************************************************
 * SSwatches: 310x35 well; 19.5px squares on a 34.6px pitch, the first
 * centred 17.2px in; the chosen one sits on a 30.5px accent-26% cell
 *********************************************************************/
SSwatches::SSwatches( const QStringList &cl, const QStringList &nl, QWidget *parent )
    : QWidget( parent ), colorList( cl ), nameList( nl ), cur( 0 ), hover( -1 )
{
    setFixedHeight( CONTROL_H );
    setMouseTracking( true );
    setCursor( Qt::PointingHandCursor );
}

QSize SSwatches::sizeHint() const { return QSize( CONTROL_W, CONTROL_H ); }

QPointF SSwatches::center( int i ) const
{
    const int n = qMax( 1, colorList.count() );
    /* keep Figma's 17.2px edge and spread the rest evenly */
    const qreal edge = 17.2;
    const qreal pitch = n > 1 ? ( width() - 2 * edge ) / ( n - 1 ) : 0;
    return QPointF( edge + i * pitch, height() / 2.0 );
}

int SSwatches::at( const QPoint &pt ) const
{
    for( int i = 0; i < colorList.count(); i++ )
    {
        QPointF c = center( i );
        if( qAbs( pt.x() - c.x() ) <= 16 )
            return i;
    }
    return -1;
}

void SSwatches::setCurrent( int i )
{
    cur = i;
    update();
}

bool SSwatches::event( QEvent *e )
{
    if( e->type() == QEvent::ToolTip )
    {
        QHelpEvent *he = static_cast<QHelpEvent *>( e );
        const int i = at( he->pos() );
        if( i >= 0 && i < nameList.count() )
            QToolTip::showText( he->globalPos(), nameList[i], this );
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event( e );
}

void SSwatches::paintEvent( QPaintEvent * )
{
    const Colors c = colors();
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    p.setPen( Qt::NoPen );
    p.setBrush( c.well );
    p.drawRoundedRect( rect(), 6, 6 );
    for( int i = 0; i < colorList.count(); i++ )
    {
        QPointF ctr = center( i );
        if( i == cur || i == hover )
        {
            QColor cell = Theme::accentDim();
            if( i != cur )
                cell = c.surface;
            p.setBrush( cell );
            p.drawRoundedRect( QRectF( ctr.x() - 15.25, ctr.y() - 15.55, 30.5, 31.1 ), 4, 4 );
        }
        p.setBrush( QColor( colorList[i] ) );
        p.drawRoundedRect( QRectF( ctr.x() - 9.75, ctr.y() - 9.75, 19.5, 19.5 ), 2, 2 );
    }
}

void SSwatches::mousePressEvent( QMouseEvent *e )
{
    const int i = at( e->pos() );
    if( i >= 0 && i != cur )
    {
        cur = i;
        update();
        emit changed( cur );
    }
}

void SSwatches::mouseMoveEvent( QMouseEvent *e )
{
    const int h = at( e->pos() );
    if( h != hover ) { hover = h; update(); }
}

void SSwatches::leaveEvent( QEvent * ) { hover = -1; update(); }

/*********************************************************************
 * SCombo
 *********************************************************************/
SCombo::SCombo( QWidget *parent ) : QComboBox( parent ), hovered( false )
{
    setFixedHeight( CONTROL_H );
    setCursor( Qt::PointingHandCursor );
    setFont( SettingsUI::font( 14 ) );
    setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    setMinimumContentsLength( 8 );
    setFocusPolicy( Qt::TabFocus );
}

QSize SCombo::sizeHint() const { return QSize( CONTROL_W, CONTROL_H ); }
QSize SCombo::minimumSizeHint() const { return QSize( 120, CONTROL_H ); }

void SCombo::enterEvent( QEvent *e ) { hovered = true; update(); QComboBox::enterEvent( e ); }
void SCombo::leaveEvent( QEvent *e ) { hovered = false; update(); QComboBox::leaveEvent( e ); }

void SCombo::paintEvent( QPaintEvent * )
{
    const Colors c = colors();
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    if( !isEnabled() )
        p.setOpacity( 0.4 );
    QColor surface = c.surface;
    if( hovered && isEnabled() )
        surface = surface.lighter( 125 );
    paintWell( p, rect(), hasFocus() ? Theme::accentDim() : c.well, surface );
    /* "English" starts 15.3px in, the 16px chevron ends 15px from the right */
    p.setFont( SettingsUI::font( 14 ) );
    p.setPen( c.text );
    QRect textR = rect().adjusted( 15, 0, -44, 0 );
    p.drawText( textR, Qt::AlignVCenter | Qt::AlignLeft,
                p.fontMetrics().elidedText( currentText(), Qt::ElideRight, textR.width() ) );
    const int g = 16;
    p.drawPixmap( width() - 15 - g, ( height() - g ) / 2,
                  Theme::pixmap( "chevron-down", g, c.text ) );
}

/*********************************************************************
 * SField
 *********************************************************************/
SField::SField( QWidget *parent ) : QLineEdit( parent )
{
    setObjectName( "SField" );
    setFixedHeight( CONTROL_H );
    setFont( SettingsUI::font( 14 ) );
}

QSize SField::sizeHint() const { return QSize( CONTROL_W, CONTROL_H ); }

/*********************************************************************
 * SButton: 31px #191919, 14px medium #8F8F8F text
 *********************************************************************/
SButton::SButton( const QString &text, QWidget *parent )
    : QAbstractButton( parent ), hovered( false )
{
    setText( text );
    setFixedHeight( BUTTON_H );
    setCursor( Qt::PointingHandCursor );
    setFocusPolicy( Qt::TabFocus );
}

QSize SButton::sizeHint() const
{
    return QSize( qMax<int>( BROWSE_W, QFontMetrics( SettingsUI::font( 14, true ) ).horizontalAdvance( text() ) + 32 ),
                  BUTTON_H );
}

void SButton::enterEvent( QEvent *e ) { hovered = true; update(); QAbstractButton::enterEvent( e ); }
void SButton::leaveEvent( QEvent *e ) { hovered = false; update(); QAbstractButton::leaveEvent( e ); }

void SButton::paintEvent( QPaintEvent * )
{
    const Colors c = colors();
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    if( !isEnabled() )
        p.setOpacity( 0.4 );
    QColor bg = c.surface;
    if( isDown() )
        bg = bg.lighter( 140 );
    else if( hovered && isEnabled() )
        bg = bg.lighter( 125 );
    p.setPen( Qt::NoPen );
    p.setBrush( bg );
    p.drawRoundedRect( rect(), 4, 4 );
    p.setFont( SettingsUI::font( 14, true ) );
    p.setPen( hovered ? c.strong : c.text );
    p.drawText( rect(), Qt::AlignCenter, text() );
    if( hasFocus() )
    {
        p.setPen( QPen( Theme::accent(), 1 ) );
        p.setBrush( Qt::NoBrush );
        p.drawRoundedRect( QRectF( rect() ).adjusted( .5, .5, -.5, -.5 ), 4, 4 );
    }
}

/*********************************************************************
 * STickSlider
 *********************************************************************/
static const qreal TRACK_H = 15.6, HANDLE_W = 8, HANDLE_H = 23.5;

STickSlider::STickSlider( double min, double max, QWidget *parent )
    : QWidget( parent ), lo( min ), hi( max ), val( min ), step( 0 ), ticks( 0 ),
      dragging( false ), fmt( NULL )
{
    setCursor( Qt::PointingHandCursor );
    setFocusPolicy( Qt::TabFocus );
    setFixedHeight( CONTROL_H );
}

void STickSlider::setLabels( const QVector<QPair<double, QString> > &l )
{
    labels = l;
    /* the labels hang 6px under the track */
    setFixedHeight( l.isEmpty() ? CONTROL_H : 40 );
    update();
}

QSize STickSlider::sizeHint() const { return QSize( CONTROL_W, height() ); }

QRectF STickSlider::track() const
{
    const qreal top = labels.isEmpty() ? ( height() - HANDLE_H ) / 2.0 + ( HANDLE_H - TRACK_H ) / 2.0
                                       : ( HANDLE_H - TRACK_H ) / 2.0;
    return QRectF( 0, top, width(), TRACK_H );
}

void STickSlider::setValue( double v )
{
    v = qBound( lo, v, hi );
    if( v == val )
        return;
    val = v;
    update();
}

double STickSlider::fromX( int x ) const
{
    QRectF t = track().adjusted( HANDLE_W / 2, 0, -HANDLE_W / 2, 0 );
    double f = qBound( 0.0, ( x - t.left() ) / t.width(), 1.0 );
    double v = lo + f * ( hi - lo );
    if( step > 0 )
        v = lo + std::round( ( v - lo ) / step ) * step;
    return qBound( lo, v, hi );
}

void STickSlider::setFromUser( double v )
{
    v = qBound( lo, v, hi );
    if( v == val )
        return;
    val = v;
    update();
    emit valueChanged( val );
}

void STickSlider::paintEvent( QPaintEvent * )
{
    const Colors c = colors();
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    if( !isEnabled() )
        p.setOpacity( 0.4 );
    QRectF t = track();
    QRectF usable = t.adjusted( HANDLE_W / 2, 0, -HANDLE_W / 2, 0 );
    const double f = hi > lo ? ( val - lo ) / ( hi - lo ) : 0;
    const qreal hx = usable.left() + f * usable.width();

    p.setPen( Qt::NoPen );
    p.setBrush( c.well );
    p.drawRoundedRect( t, 4, 4 );
    QRectF fill = t.adjusted( RIM, RIM, 0, -RIM );
    fill.setRight( hx );
    if( fill.width() > 0 )
    {
        p.setBrush( Theme::accentDim() );
        p.drawRoundedRect( fill, 2.5, 2.5 );
    }
    /* ticks: 1px, 6px tall, brighter on the filled part */
    if( ticks > 1 )
    {
        for( int i = 1; i < ticks; i++ )
        {
            const qreal x = usable.left() + usable.width() * i / ticks;
            QColor tc = x < hx ? Theme::accent() : c.knobOff;
            tc.setAlphaF( x < hx ? 0.45 : 0.55 );
            p.fillRect( QRectF( x - 0.5, t.center().y() - 3, 1, 6 ), tc );
        }
    }
    QRectF h( hx - HANDLE_W / 2, t.center().y() - HANDLE_H / 2, HANDLE_W, HANDLE_H );
    QColor hc = Theme::accentKnob();
    if( dragging || hasFocus() )
        hc = Theme::accent();
    p.setBrush( hc );
    p.drawRoundedRect( h, 2, 2 );

    if( !labels.isEmpty() )
    {
        p.setFont( SettingsUI::font( 11 ) );
        p.setPen( c.faint );
        const qreal y = t.bottom() + 6;
        for( int i = 0; i < labels.count(); i++ )
        {
            const double lf = ( labels[i].first - lo ) / ( hi - lo );
            const qreal x = usable.left() + lf * usable.width();
            const int w = p.fontMetrics().horizontalAdvance( labels[i].second );
            qreal left = x - w / 2.0;
            if( i == 0 && lf <= 0.0 ) left = 0;
            if( i == labels.count() - 1 && lf >= 1.0 ) left = width() - w;
            p.drawText( QRectF( left, y, w + 1, 14 ), Qt::AlignLeft | Qt::AlignTop,
                        labels[i].second );
        }
    }
}

void STickSlider::mousePressEvent( QMouseEvent *e )
{
    if( e->button() != Qt::LeftButton )
        return;
    dragging = true;
    setFromUser( fromX( e->pos().x() ) );
    update();
}

void STickSlider::mouseMoveEvent( QMouseEvent *e )
{
    if( dragging )
        setFromUser( fromX( e->pos().x() ) );
    else if( fmt )
        setToolTip( fmt( val ) );
}

void STickSlider::mouseReleaseEvent( QMouseEvent * )
{
    if( !dragging )
        return;
    dragging = false;
    update();
    emit released( val );
}

void STickSlider::wheelEvent( QWheelEvent *e )
{
    const double s = step > 0 ? step : ( hi - lo ) / 100.0;
    setFromUser( val + ( e->angleDelta().y() > 0 ? s : -s ) );
    emit released( val );
    e->accept();
}

void STickSlider::keyPressEvent( QKeyEvent *e )
{
    const double s = step > 0 ? step : ( hi - lo ) / 100.0;
    if( e->key() == Qt::Key_Left || e->key() == Qt::Key_Down )
        setFromUser( val - s );
    else if( e->key() == Qt::Key_Right || e->key() == Qt::Key_Up )
        setFromUser( val + s );
    else if( e->key() == Qt::Key_Home )
        setFromUser( lo );
    else if( e->key() == Qt::Key_End )
        setFromUser( hi );
    else
    {
        QWidget::keyPressEvent( e );
        return;
    }
    emit released( val );
}

/*********************************************************************
 * SEqBand: -20..+20 dB
 *********************************************************************/
static const double EQ_MIN = -20.0, EQ_MAX = 20.0;

SEqBand::SEqBand( QWidget *parent ) : QWidget( parent ), val( 0 ), dragging( false )
{
    setCursor( Qt::PointingHandCursor );
    setFixedWidth( 24 );
}

QRectF SEqBand::track() const
{
    return QRectF( ( width() - TRACK_H ) / 2.0, 0, TRACK_H, height() );
}

void SEqBand::setValue( double v )
{
    val = qBound( EQ_MIN, v, EQ_MAX );
    update();
}

void SEqBand::setFromY( int y )
{
    QRectF t = track().adjusted( 0, HANDLE_W / 2, 0, -HANDLE_W / 2 );
    double f = 1.0 - qBound( 0.0, ( y - t.top() ) / t.height(), 1.0 );
    double v = std::round( ( EQ_MIN + f * ( EQ_MAX - EQ_MIN ) ) * 10 ) / 10;
    if( v != val )
    {
        val = v;
        update();
        emit valueChanged( val );
    }
}

void SEqBand::paintEvent( QPaintEvent * )
{
    const Colors c = colors();
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    if( !isEnabled() )
        p.setOpacity( 0.4 );
    QRectF t = track();
    QRectF usable = t.adjusted( 0, HANDLE_W / 2, 0, -HANDLE_W / 2 );
    const qreal hy = usable.bottom() - ( val - EQ_MIN ) / ( EQ_MAX - EQ_MIN ) * usable.height();
    p.setPen( Qt::NoPen );
    p.setBrush( c.well );
    p.drawRoundedRect( t, 4, 4 );
    QRectF fill = t.adjusted( RIM, 0, -RIM, -RIM );
    fill.setTop( hy );
    p.setBrush( Theme::accentDim() );
    p.drawRoundedRect( fill, 2.5, 2.5 );
    /* the handle overhangs the track: 23 x 7.5 */
    p.setBrush( dragging ? Theme::accent() : Theme::accentKnob() );
    p.drawRoundedRect( QRectF( width() / 2.0 - 11.5, hy - 3.75, 23, 7.5 ), 2, 2 );
}

void SEqBand::mousePressEvent( QMouseEvent *e )
{
    if( e->button() != Qt::LeftButton )
        return;
    dragging = true;
    setFromY( e->pos().y() );
}

void SEqBand::mouseMoveEvent( QMouseEvent *e )
{
    if( dragging )
        setFromY( e->pos().y() );
}

void SEqBand::mouseReleaseEvent( QMouseEvent * )
{
    dragging = false;
    update();
}

void SEqBand::wheelEvent( QWheelEvent *e )
{
    setValue( val + ( e->angleDelta().y() > 0 ? 0.5 : -0.5 ) );
    emit valueChanged( val );
    e->accept();
}

/*********************************************************************
 * SEqualizer: #191919 panel; header centred 19px down; 11 columns that
 * split the width minus 17.5px each side; tracks 52.5..363.5; names
 * centred at 405.7; dB boxes 58x31 at 424.8; 19px under them
 *********************************************************************/
static const int EQ_PANEL_H = 475, EQ_COLLAPSED_H = 38;

SEqualizer::SEqualizer( QWidget *parent ) : QWidget( parent ), open( true )
{
    const Colors c = colors();
    header = new QToolButton( this );
    header->setObjectName( "SEqHeader" );
    header->setCursor( Qt::PointingHandCursor );
    header->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    header->setLayoutDirection( Qt::RightToLeft );
    header->setFont( SettingsUI::font( 14 ) );
    header->setAutoRaise( true );
    connect( header, &QToolButton::clicked, this, [this]() { setOpen( !open ); } );

    static const char *const names_[] = {
        N_("Preamp"), "60 Hz", "170 Hz", "310 Hz", "600 Hz", "1 KHz",
        "3 KHz", "6 KHz", "12 KHz", "14 KHz", "16 KHz" };
    for( int i = 0; i < 11; i++ )
    {
        SEqBand *b = new SEqBand( this );
        sliders << b;
        QLabel *l = new QLabel( i == 0 ? qtr( names_[0] ) : QString( names_[i] ), this );
        l->setFont( SettingsUI::font( 14 ) );
        l->setAlignment( Qt::AlignCenter );
        l->setStyleSheet( QString( "color:%1; background:transparent;" ).arg( c.faint.name() ) );
        names << l;
        QLineEdit *box = new QLineEdit( this );
        box->setObjectName( "SEqBox" );
        box->setAlignment( Qt::AlignCenter );
        box->setFont( SettingsUI::font( 12 ) );
        box->setFixedSize( 58, 31 );
        boxes << box;
        connect( b, &SEqBand::valueChanged, this, [this, i]( double ) {
            syncBox( i );
            if( i == 0 ) emit preampChanged( sliders[0]->value() );
            else emit bandsChanged();
        } );
        connect( box, &QLineEdit::editingFinished, this, [this, i]() {
            QString t = boxes[i]->text();
            t.remove( "dB" );
            bool ok;
            double v = t.trimmed().toDouble( &ok );
            if( ok )
            {
                sliders[i]->setValue( v );
                if( i == 0 ) emit preampChanged( sliders[0]->value() );
                else emit bandsChanged();
            }
            syncBox( i );
        } );
        syncBox( i );
    }
    setOpen( true );
}

void SEqualizer::syncBox( int i )
{
    boxes[i]->setText( QString::number( sliders[i]->value(), 'f', 1 ) + " dB" );
}

void SEqualizer::setOpen( bool o )
{
    open = o;
    header->setText( open ? qtr( "Close Equalizer" ) : qtr( "Open Equalizer" ) );
    header->setIcon( QIcon( Theme::pixmap( open ? "chevron-down" : "chevron", 16, colors().text ) ) );
    for( int i = 0; i < sliders.count(); i++ )
    {
        sliders[i]->setVisible( open );
        names[i]->setVisible( open );
        boxes[i]->setVisible( open );
    }
    setFixedHeight( open ? EQ_PANEL_H : EQ_COLLAPSED_H );
    place();
}

void SEqualizer::setPreamp( double v ) { sliders[0]->setValue( v ); syncBox( 0 ); }

void SEqualizer::setBands( const QVector<double> &v )
{
    for( int i = 0; i < 10 && i < v.count(); i++ )
    {
        sliders[i + 1]->setValue( v[i] );
        syncBox( i + 1 );
    }
}

double SEqualizer::preamp() const { return sliders[0]->value(); }

QVector<double> SEqualizer::bands() const
{
    QVector<double> v;
    for( int i = 1; i < sliders.count(); i++ )
        v << sliders[i]->value();
    return v;
}

void SEqualizer::resizeEvent( QResizeEvent * ) { place(); }

void SEqualizer::place()
{
    QSize hs = header->sizeHint();
    header->setGeometry( ( width() - hs.width() ) / 2, ( EQ_COLLAPSED_H - hs.height() ) / 2,
                         hs.width(), hs.height() );
    const qreal margin = 17.5;
    const qreal col = ( width() - 2 * margin ) / 11.0;
    for( int i = 0; i < sliders.count(); i++ )
    {
        const qreal cx = margin + col * ( i + 0.5 );
        sliders[i]->setGeometry( qRound( cx - 12 ), 52, 24, 312 );
        names[i]->setGeometry( qRound( cx - col / 2 ), 395, qRound( col ), 22 );
        boxes[i]->move( qRound( cx - 29 ), 425 );
    }
}

void SEqualizer::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    p.setPen( Qt::NoPen );
    p.setBrush( colors().surface );
    p.drawRoundedRect( rect(), 6, 6 );
}

/*********************************************************************
 * SColorField
 *********************************************************************/
SColorField::SColorField( QWidget *parent ) : QWidget( parent )
{
    setFixedHeight( CONTROL_H );
    edit = new QLineEdit( this );
    edit->setObjectName( "SColorEdit" );
    edit->setFont( SettingsUI::font( 14 ) );
    edit->setMaxLength( 7 );
    edit->setFrame( false );
    connect( edit, &QLineEdit::editingFinished, this, [this]() {
        QString t = edit->text().trimmed();
        if( !t.startsWith( '#' ) ) t.prepend( '#' );
        QColor c( t );
        if( c.isValid() && c != col )
        {
            col = c;
            update();
            emit changed( col );
        }
        edit->setText( col.name().toUpper() );
    } );
}

QSize SColorField::sizeHint() const { return QSize( CONTROL_W, CONTROL_H ); }

QRect SColorField::swatchRect() const
{
    return QRect( width() - 12 - 17, ( height() - 17 ) / 2, 17, 17 );
}

void SColorField::resizeEvent( QResizeEvent * )
{
    edit->setGeometry( 13, RIM, width() - 13 - 40, height() - 2 * RIM );
}

void SColorField::setColor( const QColor &c )
{
    col = c;
    edit->setText( c.name().toUpper() );
    update();
}

void SColorField::paintEvent( QPaintEvent * )
{
    const Colors c = colors();
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    paintWell( p, rect(), c.surface, c.well );
    p.setBrush( col );
    p.setPen( QPen( c.knobOff, 1 ) );
    p.drawRoundedRect( QRectF( swatchRect() ).adjusted( .5, .5, -.5, -.5 ), 2, 2 );
}

void SColorField::mousePressEvent( QMouseEvent *e )
{
    if( !swatchRect().adjusted( -6, -6, 6, 6 ).contains( e->pos() ) )
        return;
    QColor c = QColorDialog::getColor( col, this );
    if( c.isValid() && c != col )
    {
        setColor( c );
        emit changed( col );
    }
}

/*********************************************************************
 * SVolumeChoice: a #090909 well holding two 31px rows
 *********************************************************************/
SVolumeChoice::SVolumeChoice( int maxPercent, QWidget *parent )
    : QWidget( parent ), remember( true ), maximum( maxPercent )
{
    setFixedHeight( 2 * 31 + 3 * RIM );
    setCursor( Qt::PointingHandCursor );
    edit = new QLineEdit( this );
    edit->setObjectName( "SVolumeEdit" );
    edit->setFont( SettingsUI::font( 14 ) );
    edit->setPlaceholderText( "100%" );
    connect( edit, &QLineEdit::editingFinished, this, [this]() {
        QString t = edit->text();
        t.remove( '%' );
        bool ok;
        int v = t.trimmed().toInt( &ok );
        if( ok )
        {
            v = qBound( 0, v, maximum );
            edit->setText( QString( "%1%" ).arg( v ) );
            remember = false;
            update();
            emit changed( v );
        }
    } );
}

QRectF SVolumeChoice::rowRect( int i ) const
{
    return QRectF( RIM, RIM + i * ( 31 + RIM ), width() - 2 * RIM, 31 );
}

void SVolumeChoice::resizeEvent( QResizeEvent * )
{
    QRectF r = rowRect( 1 );
    const int x = 131;
    edit->setGeometry( x, qRound( r.top() ) + 1, width() - x - RIM - 1, 29 );
}

int SVolumeChoice::value() const
{
    if( remember )
        return -1;
    QString t = edit->text();
    t.remove( '%' );
    return t.trimmed().toInt();
}

void SVolumeChoice::setValue( int v )
{
    remember = v < 0;
    if( v >= 0 )
        edit->setText( QString( "%1%" ).arg( v ) );
    update();
}

void SVolumeChoice::setMaximum( int m ) { maximum = m; }

void SVolumeChoice::paintEvent( QPaintEvent * )
{
    const Colors c = colors();
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing );
    p.setPen( Qt::NoPen );
    p.setBrush( c.well );
    p.drawRoundedRect( rect(), 6, 6 );
    p.setFont( SettingsUI::font( 14 ) );
    for( int i = 0; i < 2; i++ )
    {
        QRectF r = rowRect( i );
        const bool sel = ( i == 0 ) == remember;
        p.setPen( Qt::NoPen );
        p.setBrush( sel ? Theme::accentDim() : c.surface );
        p.drawRoundedRect( r, 4, 4 );
        p.setPen( sel ? Theme::accent() : c.faint );
        p.drawText( r.adjusted( 9, 0, 0, 0 ), Qt::AlignVCenter | Qt::AlignLeft,
                    i == 0 ? qtr( "Remember last volume" ) : qtr( "Always starts at:" ) );
    }
}

void SVolumeChoice::mousePressEvent( QMouseEvent *e )
{
    const bool first = rowRect( 0 ).contains( e->pos() );
    if( first == remember )
        return;
    remember = first;
    update();
    if( !remember && edit->text().isEmpty() )
        edit->setText( "100%" );
    emit changed( value() );
}

/*********************************************************************
 * SInfo: 15px (i), #404040, brighter on hover
 *********************************************************************/
SInfo::SInfo( const QString &tip, QWidget *parent ) : QWidget( parent ), hovered( false )
{
    setFixedSize( 17, 17 );
    setToolTip( tip );
}

void SInfo::enterEvent( QEvent * ) { hovered = true; update(); }
void SInfo::leaveEvent( QEvent * ) { hovered = false; update(); }

void SInfo::paintEvent( QPaintEvent * )
{
    QPainter p( this );
    const Colors c = colors();
    p.drawPixmap( 1, 1, Theme::pixmap( "info", 15, hovered ? c.text : c.info ) );
}
