/*****************************************************************************
 * vlc_theme.hpp : design tokens and helpers for the redesigned Qt interface
 *****************************************************************************
 * Copyright (C) 2026 the VideoLAN team
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*
 * Provenance: Figma community file "VLC Redesign" (key 4UaxYzCN8lGeBI9uEAeWhN),
 * frame "Player" exported at 7680x4320 and measured at a 1920x1080 reference.
 * Dark surface colours and all metrics are measured. Light colours, hover and
 * pressed states are derived (the design does not show them).
 */

#ifndef VLC_QT_THEME_HPP_
#define VLC_QT_THEME_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>

#include <QColor>
#include <QIcon>
#include <QString>

class QPainter;
class QRectF;
class QWidget;
class QAbstractButton;

namespace Theme
{

/* Selectable accents (Settings -> General -> Accent color). */
extern const char *const ACCENT_VALUES[];
extern const char *const ACCENT_NAMES[];
extern const int ACCENT_COUNT;

/* Fixed colours that do not follow the accent. */
static const char *const VOLUME_FILL_HI = "#93B81A";
static const char *const VOLUME_FILL    = "#7E9E13";

/* Metrics, logical px. */
enum
{
    TITLEBAR_HEIGHT   = 40,
    RAIL_WIDTH        = 50,
    RAIL_ITEM_HEIGHT  = 46,
    RAIL_GLYPH        = 24,
    SIDEBAR_WIDTH     = 376,
    SIDEBAR_MIN_WIDTH = 240,
    CONTROLBAR_HEIGHT = 54,
    CONTENT_MARGIN    = 10,   /* gap between content panel and window edge */
    RADIUS            = 6,
    SEEK_THICKNESS    = 28,
    SEEK_RADIUS       = 5,
    SEEK_HANDLE_W     = 8,
    SEEK_HANDLE_H     = 24,
    PLAY_BUTTON_W     = 54,
    PLAY_BUTTON_H     = 28,
    BAR_BUTTON        = 32,   /* hit target of a control-bar icon button */
    BAR_GLYPH         = 24,   /* stroke icons are drawn on a 24px grid */
    SMALL_GLYPH       = 14,
    VOLUME_WIDTH      = 90,
    VOLUME_HEIGHT     = 28,
    VOLUME_TIP_H      = 10,   /* height of the thin end of the wedge */
    WINDOW_BUTTON_W   = 36,

    /* Control bar box model, measured against the 1920px frame:
     * prev 22-48 | play 58-112 | next 122-148 | elapsed ..254 | seek |
     * 1492.. total | mute 1598-1622 | wedge 1628-1718 | audio 1736,
     * subs 1778, adjustments 1820 | queue 1870-1902 */
    BAR_MARGIN_LEFT    = 22,
    BAR_MARGIN_RIGHT   = 18,
    BAR_SPACER         = 18,
    BAR_GROUP_SPACING  = 10,
    TRANSPORT_BUTTON_W = 26,
    TRANSPORT_GLYPH    = 20,
    TIME_LABEL_W       = 88,
    TIME_LABEL_GAP_L   = 13,  /* elapsed text -> seek (QLabel adds ~3px) */
    TIME_LABEL_GAP_R   = 11,  /* seek -> total text (QLabel adds ~3px) */
    MUTE_BUTTON_W      = 24,
    MUTE_TO_VOLUME     = 6,
};

struct Palette
{
    QColor titlebar, rail, sidebar, content, controlbar;
    QColor track, elevated, border, hover;
    QColor text, textMuted, textFaint, icon, iconHover, iconDisabled;
};

/* Call once from the Qt thread before any widget is built. */
void init( intf_thread_t *p_intf );
/* Install palette + application stylesheet. */
void apply();

bool redesign();      /* qt-redesign: new layout on/off */
bool frameless();     /* qt-redesign-frameless */
bool isDark();
const Palette &colors();
QColor accent();
QColor accentHover();
QColor accentPressed();
QColor seekFill();    /* filled part of the seek bar */

/* Monochrome icon from :/redesign/<name>.svg, tinted for every state. */
QIcon icon( const QString &name, int size = BAR_GLYPH );
QPixmap pixmap( const QString &name, int size, const QColor &color );

/* Apply the standard redesign look to a flat icon button. */
void setupIconButton( QAbstractButton *, const QString &icon,
                      int box = BAR_BUTTON, int glyph = BAR_GLYPH );

/* Animated hover background (120ms fade) for a flat button. Replaces the
 * instant :hover rule of the stylesheet. */
void installHoverFade( QAbstractButton *, int radius = RADIUS );

/* "Play / Pause (Space)": tooltip text with the user's current hotkey. */
QString tooltipWithKey( intf_thread_t *, const QString &text, const char *keyOption );

/* Custom painters for the two self-painted widgets. */
void paintSeekBar( QPainter *, const QRectF &groove, qreal position,
                   qreal buffering, const QList<qreal> &chapters,
                   bool enabled, qreal hover, int hoverX = -1 );
void paintVolumeWedge( QPainter *, const QRectF &rect, qreal fraction,
                       bool muted, bool hovered );

} /* namespace Theme */

#endif
