/*****************************************************************************
 * settings_controls.hpp : the controls of the redesigned Settings window (P6)
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

/*
 * Measured on the Figma exports "Settings - global.png" and
 * "Settings - audio.png" (3234px for a 1048px frame, so 3.086x):
 *   a control sits in a #090909 well with a 2px rim; the surface inside
 *   is #191919, the selected part is the accent at 26% with accent text,
 *   knobs and handles are the accent at 81%.
 *   dropdown / field / segmented / swatches: 310 x 35, toggle: 66 x 31.
 */

#ifndef VLC_QT_SETTINGS_CONTROLS_HPP_
#define VLC_QT_SETTINGS_CONTROLS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QAbstractButton>
#include <QComboBox>
#include <QLineEdit>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QToolButton;

namespace SettingsUI
{
    enum
    {
        CONTROL_W   = 310,   /* the right-hand control column */
        CONTROL_H   = 35,    /* well height of dropdowns, fields, segments */
        TOGGLE_W    = 66,
        TOGGLE_H    = 31,
        BUTTON_H    = 31,
        BROWSE_W    = 97,
        RIM         = 2,
    };
    struct Colors
    {
        QColor window, header, nav, navSelected, search;
        QColor well, surface, knobOff;
        QColor text, strong, title, navSelectedText, faint, info;
    };
    /* dark is measured; light is derived (the design has no light frame) */
    Colors colors();
    QFont font( int px, bool medium = false );
}

/* ON / OFF switch: the label box and the knob change sides */
class SToggle : public QAbstractButton
{
    Q_OBJECT
public:
    explicit SToggle( QWidget *parent = NULL );
    QSize sizeHint() const Q_DECL_OVERRIDE;
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void enterEvent( QEvent * ) Q_DECL_OVERRIDE;
    void leaveEvent( QEvent * ) Q_DECL_OVERRIDE;
private:
    bool hovered;
};

/* Dark | Light | System */
class SSegmented : public QWidget
{
    Q_OBJECT
public:
    SSegmented( const QStringList &labels, QWidget *parent = NULL );
    int current() const { return cur; }
    void setCurrent( int );
    QSize sizeHint() const Q_DECL_OVERRIDE;
signals:
    void changed( int );
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void mousePressEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void mouseMoveEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void leaveEvent( QEvent * ) Q_DECL_OVERRIDE;
    void keyPressEvent( QKeyEvent * ) Q_DECL_OVERRIDE;
private:
    QRectF segment( int ) const;
    int at( const QPoint & ) const;
    QStringList labels;
    int cur, hover;
};

/* the nine accent colours */
class SSwatches : public QWidget
{
    Q_OBJECT
public:
    SSwatches( const QStringList &colors, const QStringList &names, QWidget *parent = NULL );
    int current() const { return cur; }
    void setCurrent( int );
    QSize sizeHint() const Q_DECL_OVERRIDE;
signals:
    void changed( int );
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void mousePressEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void mouseMoveEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void leaveEvent( QEvent * ) Q_DECL_OVERRIDE;
    bool event( QEvent * ) Q_DECL_OVERRIDE;
private:
    QPointF center( int ) const;
    int at( const QPoint & ) const;
    QStringList colorList, nameList;
    int cur, hover;
};

/* dropdown: painted well, text at 13px, chevron 23px from the right */
class SCombo : public QComboBox
{
    Q_OBJECT
public:
    explicit SCombo( QWidget *parent = NULL );
    QSize sizeHint() const Q_DECL_OVERRIDE;
    QSize minimumSizeHint() const Q_DECL_OVERRIDE;
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void enterEvent( QEvent * ) Q_DECL_OVERRIDE;
    void leaveEvent( QEvent * ) Q_DECL_OVERRIDE;
private:
    bool hovered;
};

/* text field in a #191919 rim */
class SField : public QLineEdit
{
    Q_OBJECT
public:
    explicit SField( QWidget *parent = NULL );
    QSize sizeHint() const Q_DECL_OVERRIDE;
};

/* flat #191919 button with medium text (Browse, Reset, Clear...) */
class SButton : public QAbstractButton
{
    Q_OBJECT
public:
    SButton( const QString &text, QWidget *parent = NULL );
    QSize sizeHint() const Q_DECL_OVERRIDE;
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void enterEvent( QEvent * ) Q_DECL_OVERRIDE;
    void leaveEvent( QEvent * ) Q_DECL_OVERRIDE;
private:
    bool hovered;
};

/* horizontal slider: 15.6px track, accent-26% fill, 8x23.5 handle,
 * optional ticks and value labels under the track */
class STickSlider : public QWidget
{
    Q_OBJECT
public:
    STickSlider( double min, double max, QWidget *parent = NULL );
    void setTicks( int intervals ) { ticks = intervals; update(); }
    /* value -> text, drawn left / centre / right aligned under the track */
    void setLabels( const QVector<QPair<double, QString> > &l );
    void setStep( double s ) { step = s; }
    void setFormatter( QString (*f)( double ) ) { fmt = f; }
    double value() const { return val; }
    void setValue( double );
    QSize sizeHint() const Q_DECL_OVERRIDE;
signals:
    void valueChanged( double );
    void released( double );
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void mousePressEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void mouseMoveEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void mouseReleaseEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void wheelEvent( QWheelEvent * ) Q_DECL_OVERRIDE;
    void keyPressEvent( QKeyEvent * ) Q_DECL_OVERRIDE;
private:
    QRectF track() const;
    double fromX( int x ) const;
    void setFromUser( double );
    double lo, hi, val, step;
    int ticks;
    bool dragging;
    QVector<QPair<double, QString> > labels;
    QString (*fmt)( double );
};

/* one vertical equalizer band (Settings - audio: 15.6 x 311) */
class SEqBand : public QWidget
{
    Q_OBJECT
public:
    explicit SEqBand( QWidget *parent = NULL );
    double value() const { return val; }
    void setValue( double );
signals:
    void valueChanged( double );
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void mousePressEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void mouseMoveEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void mouseReleaseEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void wheelEvent( QWheelEvent * ) Q_DECL_OVERRIDE;
private:
    QRectF track() const;
    void setFromY( int );
    double val;
    bool dragging;
};

/* the inline 10-band equalizer: preamp + bands, dB boxes, collapsible */
class SEqualizer : public QWidget
{
    Q_OBJECT
public:
    explicit SEqualizer( QWidget *parent = NULL );
    void setPreamp( double );
    void setBands( const QVector<double> & );
    double preamp() const;
    QVector<double> bands() const;
    void setOpen( bool );
signals:
    void preampChanged( double );
    void bandsChanged();
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void resizeEvent( QResizeEvent * ) Q_DECL_OVERRIDE;
private:
    void place();
    void syncBox( int );
    QToolButton *header;
    QVector<SEqBand *> sliders;
    QVector<QLabel *> names;
    QVector<QLineEdit *> boxes;
    bool open;
};

/* "#FFFFFF" + a swatch that opens a colour picker */
class SColorField : public QWidget
{
    Q_OBJECT
public:
    explicit SColorField( QWidget *parent = NULL );
    QColor color() const { return col; }
    void setColor( const QColor & );
    QSize sizeHint() const Q_DECL_OVERRIDE;
signals:
    void changed( const QColor & );
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void mousePressEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void resizeEvent( QResizeEvent * ) Q_DECL_OVERRIDE;
private:
    QRect swatchRect() const;
    QLineEdit *edit;
    QColor col;
};

/* Startup volume: "Remember last volume" | "Always starts at: [200%]" */
class SVolumeChoice : public QWidget
{
    Q_OBJECT
public:
    explicit SVolumeChoice( int maxPercent, QWidget *parent = NULL );
    /* -1: remember the last volume */
    int value() const;
    void setValue( int );
    void setMaximum( int );
signals:
    void changed( int );
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void mousePressEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void resizeEvent( QResizeEvent * ) Q_DECL_OVERRIDE;
private:
    QRectF rowRect( int ) const;
    QLineEdit *edit;
    bool remember;
    int maximum;
};

/* the small (i) after a label; the text is its tooltip */
class SInfo : public QWidget
{
    Q_OBJECT
public:
    SInfo( const QString &tip, QWidget *parent = NULL );
protected:
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE;
    void enterEvent( QEvent * ) Q_DECL_OVERRIDE;
    void leaveEvent( QEvent * ) Q_DECL_OVERRIDE;
private:
    bool hovered;
};

#endif
