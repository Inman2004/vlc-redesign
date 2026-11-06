/*****************************************************************************
 * redesign_widgets.hpp : small controls of the redesigned interface
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

#ifndef VLC_QT_REDESIGN_WIDGETS_HPP_
#define VLC_QT_REDESIGN_WIDGETS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QSlider>
#include <QAbstractButton>
#include <QWidget>
#include <QColor>

class QVariantAnimation;
class QLabel;
class QLineEdit;
class QToolButton;
class QVBoxLayout;

/* Horizontal slider of the adjustment panel (Figma "Property 1=expanded"
 * frames): 16px track, 8x24 handle, fill from an origin so bipolar values
 * (delays, hue) fill from their zero, optional ticks. Click jumps, drag
 * scrubs, double-click resets, the wheel only acts once focused so the
 * panel can still be scrolled over it. */
class AdjustSlider : public QSlider
{
    Q_OBJECT
public:
    explicit AdjustSlider( QWidget *parent = NULL );

    void setOrigin( int value );   /* fill starts here, default = minimum */
    void setTicks( int intervals ); /* 0 = no ticks */
    void setResetValue( int value );
    int resetTarget() const { return hasReset ? resetValue : hasOrigin ? origin : minimum(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    enum { TRACK_H = 16, HANDLE_W = 8, HANDLE_H = 24 };

protected:
    void paintEvent( QPaintEvent * ) override;
    void mousePressEvent( QMouseEvent * ) override;
    void mouseMoveEvent( QMouseEvent * ) override;
    void mouseReleaseEvent( QMouseEvent * ) override;
    void mouseDoubleClickEvent( QMouseEvent * ) override;
    void wheelEvent( QWheelEvent * ) override;
    void enterEvent( QEvent * ) override;
    void leaveEvent( QEvent * ) override;

private:
    int valueAt( int x ) const;
    qreal xFor( int value ) const;
    void animateHover( bool );

    QVariantAnimation *hoverAnim;
    qreal hover;
    int origin;
    bool hasOrigin;
    int ticks;
    int resetValue;
    bool hasReset;
};

/* ON/OFF switch, 68x32: the knob slides to the "on" side and takes the
 * accent; the free half shows ON or OFF. */
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch( QWidget *parent = NULL );
    QSize sizeHint() const override;
    void setCheckedAnimated( bool );

protected:
    void paintEvent( QPaintEvent * ) override;
    void enterEvent( QEvent * ) override;
    void leaveEvent( QEvent * ) override;
    void checkStateSet() override;
    void nextCheckState() override;

private:
    void animateTo( bool on );
    QVariantAnimation *anim;
    qreal pos;     /* 0 = off, 1 = on */
    bool hovered;
};

/* Card holding the settings of one effect: an "Expand Settings" header
 * with a rotating chevron, and a body that grows and shrinks. */
class CollapsibleSection : public QWidget
{
    Q_OBJECT
public:
    explicit CollapsibleSection( QWidget *parent = NULL );
    QVBoxLayout *bodyLayout() const { return body; }
    QWidget *bodyWidget() const { return bodyHost; }
    bool isExpanded() const { return expanded; }

public slots:
    void setExpanded( bool, bool animate = true );
    void toggle() { setExpanded( !expanded ); }

signals:
    void expandedChanged( bool );

protected:
    bool eventFilter( QObject *, QEvent * ) override;

private:
    void updateHeader();
    QWidget *header;
    QLabel *headerText;
    QLabel *chevron;
    QWidget *bodyHost;
    QVBoxLayout *body;
    QVariantAnimation *anim;
    qreal angle;
    bool expanded;
};

/* "#B8B8B8 [swatch]" value box: hex text field plus a swatch opening the
 * colour picker. */
class ColorField : public QWidget
{
    Q_OBJECT
public:
    explicit ColorField( QWidget *parent = NULL );
    QColor color() const { return current; }
    void setColor( const QColor & );

signals:
    void colorChanged( const QColor & );

private:
    void commitText();
    void updateSwatch();
    QLineEdit *edit;
    QToolButton *swatch;
    QColor current;
};

#endif
