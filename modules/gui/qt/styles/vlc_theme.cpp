/*****************************************************************************
 * Copyright (C) 2009-2026 VLC authors and VideoLAN project
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * ---------------------------------------------------------------------------
 * Runtime resolution of the redesign theme.
 *
 * The static token table lives in vlc_theme.hpp. This file turns it into
 * live QPalette and application stylesheet, honouring the qt-theme and
 * qt-accent-color settings so the palette can be changed at runtime.
 *****************************************************************************/

#include "vlc_theme.hpp"

#include <QApplication>
#include <QWidget>   /* qApp */
#include <QColor>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSettings>
#include <QString>
#include <QStyleFactory>
#include <QTextStream>

namespace Theme
{

static bool g_dark = true;
static QColor g_accent( THEME_ACCENT_DEFAULT );

QColor color( const char *hex )
{
    return QColor( QString::fromLatin1( hex ) );
}

bool isDark()
{
    return g_dark;
}

QColor accent()
{
    return g_accent;
}

/**
 * Resolve "system" against the platform palette brightness, then cache the
 * result. Called once at startup and whenever the setting changes.
 */
static void resolveScheme( intf_thread_t *p_intf )
{
    const char *psz_scheme = var_InheritString( p_intf, "qt-theme" );
    if( !psz_scheme || !strcmp( psz_scheme, "dark" ) )
        g_dark = true;
    else if( !strcmp( psz_scheme, "light" ) )
        g_dark = false;
    else
    {
        /* "system": follow the platform's own preference where we can
         * detect it, otherwise stay dark since that is the design default. */
        QPalette plat = QApplication::palette();
        const QColor window = plat.color( QPalette::Window );
#if defined( Q_OS_MAC )
        g_dark = window.lightness() < 128;
#else
        g_dark = window.lightness() < 128;
#endif
    }
}

static QColor accentShade( const QColor &c, int delta )
{
    QColor out = c;
    if( delta > 0 )
        out = out.lighter( 100 + delta );
    else
        out = out.darker( 100 - delta );
    return out;
}

/**
 * Build the palette for the current scheme.  Mirrors the structure of the
 * hand-rolled dark palette in qt.cpp so that widgets which read QPalette
 * directly (scrollbars, tooltips, combo popups) match the stylesheet.
 */
static QPalette buildPalette()
{
    QPalette p;
    if( g_dark )
    {
        const QColor window  = color( DARK_CONTROLBAR );
        const QColor base    = color( DARK_TRACK );
        const QColor text    = color( DARK_TEXT );
        const QColor muted   = color( DARK_TEXT_MUTED );
        const QColor disabled= color( DARK_BORDER );
        const QColor alt     = color( DARK_CONTENT );

        p.setColor( QPalette::Window,          window );
        p.setColor( QPalette::WindowText,      text );
        p.setColor( QPalette::Base,            base );
        p.setColor( QPalette::AlternateBase,   alt );
        p.setColor( QPalette::ToolTipBase,     alt );
        p.setColor( QPalette::Text,            text );
        p.setColor( QPalette::Button,          window );
        p.setColor( QPalette::ButtonText,      text );
        p.setColor( QPalette::BrightText,      Qt::red );
        p.setColor( QPalette::Light,           accentShade( window, 40 ) );
        p.setColor( QPalette::Midlight,        accentShade( window, 20 ) );
        p.setColor( QPalette::Dark,            accentShade( window, -20 ) );
        p.setColor( QPalette::Mid,             accentShade( window, -10 ) );
        p.setColor( QPalette::Shadow,          Qt::black );
        p.setColor( QPalette::Highlight,       g_accent );
        p.setColor( QPalette::HighlightedText, Qt::white );
        p.setColor( QPalette::Link,            g_accent );
        p.setColor( QPalette::LinkVisited,     accentShade( g_accent, -20 ) );
        p.setColor( QPalette::Disabled, QPalette::WindowText, muted );
        p.setColor( QPalette::Disabled, QPalette::Text,       muted );
        p.setColor( QPalette::Disabled, QPalette::ButtonText, muted );
        p.setColor( QPalette::Disabled, QPalette::Window,     window );
        p.setColor( QPalette::Disabled, QPalette::Base,       base );
        p.setColor( QPalette::Disabled, QPalette::Highlight,  disabled );
        p.setColor( QPalette::Disabled, QPalette::HighlightedText, muted );
    }
    else
    {
        const QColor window  = color( LIGHT_CONTROLBAR );
        const QColor base    = color( LIGHT_TRACK );
        const QColor text    = color( LIGHT_TEXT );
        const QColor muted   = color( LIGHT_TEXT_MUTED );
        const QColor alt     = color( LIGHT_CONTENT );

        p.setColor( QPalette::Window,          window );
        p.setColor( QPalette::WindowText,      text );
        p.setColor( QPalette::Base,            base );
        p.setColor( QPalette::AlternateBase,   alt );
        p.setColor( QPalette::ToolTipBase,     alt );
        p.setColor( QPalette::Text,            text );
        p.setColor( QPalette::Button,          window );
        p.setColor( QPalette::ButtonText,      text );
        p.setColor( QPalette::BrightText,      Qt::red );
        p.setColor( QPalette::Light,           accentShade( window, 20 ) );
        p.setColor( QPalette::Midlight,        accentShade( window, 10 ) );
        p.setColor( QPalette::Dark,            accentShade( window, -20 ) );
        p.setColor( QPalette::Mid,             accentShade( window, -10 ) );
        p.setColor( QPalette::Shadow,          Qt::black );
        p.setColor( QPalette::Highlight,       g_accent );
        p.setColor( QPalette::HighlightedText, Qt::white );
        p.setColor( QPalette::Link,            g_accent );
        p.setColor( QPalette::LinkVisited,     accentShade( g_accent, -20 ) );
        p.setColor( QPalette::Disabled, QPalette::WindowText, muted );
        p.setColor( QPalette::Disabled, QPalette::Text,       muted );
        p.setColor( QPalette::Disabled, QPalette::ButtonText, muted );
        p.setColor( QPalette::Disabled, QPalette::Window,     window );
        p.setColor( QPalette::Disabled, QPalette::Base,       base );
        p.setColor( QPalette::Disabled, QPalette::Highlight,  muted );
        p.setColor( QPalette::Disabled, QPalette::HighlightedText, text );
    }
    return p;
}

/**
 * The stylesheet.  Kept in code rather than a .qss resource so that the
 * accent colour can be substituted at runtime; the static parts mirror what
 * a resource file would hold.  Selectors target object names, which is why
 * P1 also adds setObjectName() to the control-bar widgets.
 */
static QString buildStyleSheet()
{
    QString accent = g_accent.name();
    QString bg     = g_dark ? QString::fromLatin1( DARK_CONTROLBAR )
                            : QString::fromLatin1( LIGHT_CONTROLBAR );
    QString track  = g_dark ? QString::fromLatin1( DARK_TRACK )
                            : QString::fromLatin1( LIGHT_TRACK );
    QString text   = g_dark ? QString::fromLatin1( DARK_TEXT )
                            : QString::fromLatin1( LIGHT_TEXT );
    QString muted  = g_dark ? QString::fromLatin1( DARK_TEXT_MUTED )
                            : QString::fromLatin1( LIGHT_TEXT_MUTED );
    QString border = g_dark ? QString::fromLatin1( DARK_BORDER )
                            : QString::fromLatin1( LIGHT_BORDER );
    QString elevated = g_dark ? QString::fromLatin1( DARK_ELEVATED )
                              : QString::fromLatin1( LIGHT_ELEVATED );

    QString ss;
    ss += QString(
        "QWidget#MainInterface { background: %1; }\n"          /* window background */
        "QFrame#ControlBar { background: %1; }\n"
        "QFrame#InputControls { background: %1; }\n"
        "QWidget#SideBar, QWidget#PlaylistSideBar { background: %2; }\n"
        "QLabel { color: %3; }\n"
        "QLabel#Muted { color: %4; }\n"
        "QLabel#TimeLabel { color: %3; padding: 0 6px; }\n"    /* was a setStyleSheet */
        "QLabel#SpeedLabel { color: %4; }\n"                    /* was a setStyleSheet */
        "QLabel#NameLabel { padding: 0 6px; }\n"
        "QToolButton { color: %4; border: none; background: transparent; }\n"
        "QToolButton:hover { background: rgba(255,255,255,0.08); }\n"
        "QToolButton:pressed { background: %5; }\n"             /* accent tint */
        "QToolButton:checked { background: %5; }\n"
        "QToolButton:disabled { color: %6; }\n"
        "QPushButton { background: %7; color: %3; border: 1px solid %6;"
        "  border-radius: 4px; padding: 4px 12px; }\n"
        "QPushButton:hover { border-color: %8; }\n"
        "QPushButton:pressed { background: %5; }\n"
        "QPushButton:checked { background: %8; color: white; border-color: %8; }\n"
        "QMenu { background: %7; color: %3; border: 1px solid %6; }\n"
        "QMenu::item { padding: 6px 24px; }\n"
        "QMenu::item:selected { background: %5; }\n"
        "QMenu::separator { height: 1px; background: %6; margin: 4px 8px; }\n"
        "QMenuBar { background: %1; color: %3; }\n"
        "QMenuBar::item:selected { background: %5; }\n"
        "QStatusBar { background: %1; color: %4; }\n"
        "QTabWidget::pane { border: none; }\n"
        "QTabBar::tab { color: %4; padding: 6px 14px; }\n"
        "QTabBar::tab:selected { color: %3; border-bottom: 2px solid %8; }\n"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }\n"
        "QScrollBar::handle:vertical { background: %6; border-radius: 4px;"
        "  min-height: 24px; }\n"
        "QScrollBar::handle:vertical:hover { background: %4; }\n"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }\n"
        "QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; }\n"
        "QScrollBar::handle:horizontal { background: %6; border-radius: 4px;"
        "  min-width: 24px; }\n"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }\n"
        "QLineEdit, QComboBox, QSpinBox { background: %9; color: %3;"
        "  border: 1px solid %6; border-radius: 4px; padding: 4px 8px; }\n"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: %8; }\n"
        "QComboBox::drop-down { border: none; width: 20px; }\n"
        "QComboBox QAbstractItemView { background: %7; color: %3;"
        "  selection-background-color: %5; border: 1px solid %6; }\n"
        "QCheckBox, QRadioButton { color: %3; spacing: 6px; }\n"
        "QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; }\n"
        "QGroupBox { color: %3; border: 1px solid %6; border-radius: 6px;"
        "  margin-top: 10px; padding-top: 8px; }\n"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }\n"
    ).arg( bg,        /* 1 */ QString::fromLatin1( DARK_SIDEBAR ), /* 2 */
           text,      /* 3 */
           muted,     /* 4 */
           QString( "rgba(%1,24,50,255)" ).arg( accent ),   /* 5 tint */
           border,    /* 6 */
           elevated,  /* 7 */
           accent,    /* 8 */
           g_dark ? QString::fromLatin1( DARK_TRACK )
                  : QString::fromLatin1( LIGHT_ELEVATED ) ); /* 9 */
    return ss;
}

/**
 * Paint the seek bar to the Figma specification.
 *
 * Measured from the design at a 1920-wide reference: a 28px tall rounded
 * track in #090909, elapsed portion in #371F06, and a slim 8px wide accent
 * handle that only appears on hover. Note the elapsed colour is a dark
 * brown, NOT the accent -- this is what the design actually shows.
 */
void paintSeekBar( QPainter &painter, const SeekBarGeometry &geo,
                   const QRect &widgetRect )
{
    if( !geo.groove.isValid() )
        return;

    painter.save();
    painter.setRenderHint( QPainter::Antialiasing, true );

    const int radius = geo.groove.height() / 2;
    const qreal pos = qBound( 0.0, geo.position, 1.0 );

    /* track */
    painter.setPen( Qt::NoPen );
    painter.setBrush( color( DARK_TRACK ) );
    painter.drawRoundedRect( geo.groove, radius, radius );

    /* buffered-ahead, if known */
    if( geo.buffering > 0.0 && geo.buffering < 1.0 )
    {
        QRect buf = geo.groove;
        buf.setWidth( geo.groove.width() * geo.buffering );
        painter.setBrush( color( DARK_TRACK ).lighter( 160 ) );
        painter.drawRoundedRect( buf, radius, radius );
    }

    /* chapter ticks sit above the fill but below the handle */
    if( geo.chapters.size() && geo.length > 0 && geo.groove.width() )
    {
        painter.setBrush( Qt::NoBrush );
        painter.setPen( QPen( color( DARK_TEXT_MUTED ), 1 ) );
        foreach( const int64_t time, geo.chapters )
        {
            const qreal x = geo.groove.x()
                + geo.groove.width() * ( qreal(time) / (qreal)geo.length );
            painter.drawLine( QPointF( x, geo.groove.y() ),
                              QPointF( x, geo.groove.y() + 6 ) );
        }
    }

    /* elapsed */
    if( geo.enabled && pos > 0.0 )
    {
        QRect elapsed = geo.groove;
        elapsed.setWidth( geo.groove.width() * pos );
        if( elapsed.width() < radius * 2 )
            elapsed.setWidth( qMin( radius * 2, geo.groove.width() ) );
        painter.setBrush( color( DARK_SEEKFILL ) );
        painter.drawRoundedRect( elapsed, radius, radius );
    }

    /* handle: 8px wide, accent, inset vertically like the design */
    if( geo.hovered && geo.enabled )
    {
        const int hw = SEEK_HANDLE_W;
        const int inset = 2;
        QRect handle( geo.groove.x() + geo.groove.width() * pos - hw / 2,
                      geo.groove.y() + inset,
                      hw, geo.groove.height() - inset * 2 );
        painter.setBrush( g_accent );
        painter.drawRoundedRect( handle, hw / 2.0, hw / 2.0 );
    }

    painter.restore();
}

/**
 * Paint the volume slider as a real cone.
 *
 * The design replaces VLC's rounded rect with a traffic-cone silhouette:
 * narrow at both ends, widest at the middle, filled with a yellow-green
 * (#7E9E13) up to the current volume. Measured 132px at the waist, 28px
 * tall.
 */
void paintVolumeCone( QPainter &painter, const QRect &rect, qreal value,
                      bool muted )
{
    if( !rect.isValid() )
        return;

    painter.save();
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.setPen( Qt::NoPen );

    const qreal w = rect.width();
    const qreal h = rect.height();
    const qreal cy = rect.y() + h / 2.0;

    /* The silhouette measured from the design: ~9px wide at the very top,
     * widening to the full 132px waist at mid height, then narrowing again
     * to ~9px at the bottom. That is a lens/rhombus, widest in the middle
     * of the height -- the "deformed cone" the brief describes, made
     * symmetric. */
    const qreal tip = qMin( h / 3.0, 9.0 );

    QPainterPath cone;
    cone.moveTo( rect.x(), cy );                       /* left waist      */
    cone.quadTo( rect.x() + w * 0.25, cy - h / 2.0,
                 rect.center().x() - tip, rect.y() );  /* top-left taper  */
    cone.lineTo( rect.center().x() + tip, rect.y() );  /* flat 18px tip   */
    cone.quadTo( rect.x() + w * 0.75, cy - h / 2.0,
                 rect.x() + w, cy );                    /* top-right taper */
    cone.quadTo( rect.x() + w * 0.75, cy + h / 2.0,
                 rect.center().x() + tip, rect.bottom() ); /* bottom-right */
    cone.lineTo( rect.center().x() - tip, rect.bottom() );
    cone.quadTo( rect.x() + w * 0.25, cy + h / 2.0,
                 rect.x(), cy );
    cone.closeSubpath();

    /* empty cone: dark outline/fill */
    painter.setBrush( color( DARK_TRACK ) );
    painter.drawPath( cone );

    /* fill up to the volume level */
    const qreal v = qBound( 0.0, muted ? 0.0 : value, 1.0 );
    if( v > 0.0 )
    {
        painter.save();
        painter.setClipPath( cone, Qt::IntersectClip );
        QRectF level( rect.x(), rect.y(), w * v, h );
        painter.setBrush( color( VOLUME_FILL ) );
        painter.drawRect( level );
        painter.restore();
    }

    painter.restore();
}

/**
 * Entry point: resolve the settings and install palette + stylesheet.
 * Safe to call again after the user changes the theme.
 */
void apply( intf_thread_t *p_intf )
{
    const char *psz_accent = var_InheritString( p_intf, "qt-accent-color" );
    QColor c( psz_accent ? QString::fromLatin1( psz_accent )
                         : QString::fromLatin1( THEME_ACCENT_DEFAULT ) );
    if( !c.isValid() )
        c = color( THEME_ACCENT_DEFAULT );
    g_accent = c;

    resolveScheme( p_intf );

    qApp->setPalette( buildPalette() );
    qApp->setStyleSheet( buildStyleSheet() );
}

} /* namespace Theme */