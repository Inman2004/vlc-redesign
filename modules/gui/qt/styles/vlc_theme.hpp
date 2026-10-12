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
 * VLC redesign theme tokens.
 *
 * Single source of truth for every colour and metric used by the redesigned
 * Qt interface. Values are taken from the "VLC Redesign" Figma file
 * (4UaxYzCN8lGeBI9uEAeWhN), measured from the exported 7680x4320 Player
 * frame at a 1920-wide reference.
 *****************************************************************************/

#ifndef VLC_QT_STYLES_VLC_THEME_HPP
#define VLC_QT_STYLES_VLC_THEME_HPP

#include <vlc_common.h>
#include <vlc_interface.h>   /* intf_thread_t */

#ifdef __cplusplus
class QPainter;
#include <QList>
#include <QRect>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define THEME_TEXT N_("Theme")
#define THEME_LONGTEXT N_("Colour scheme of the interface.")



#define ACCENT_COLOR_TEXT N_("Accent color")
#define ACCENT_COLOR_LONGTEXT N_("Colour used for highlighted controls, sliders and switches.")

#define THEME_ACCENT_DEFAULT "#D37918"

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------ *
 * Design tokens.  Do not use raw colour literals elsewhere in the GUI. *
 * ------------------------------------------------------------------ */

namespace Theme
{

/* Apply the resolved theme: palette plus application stylesheet.
 * Defined in vlc_theme.cpp.  Takes an intf_thread_t* to read qt-theme and
 * qt-accent-color. */
void apply( intf_thread_t *p_intf );

/* True when the resolved scheme is the dark one. Widgets that paint
 * themselves consult this to decide between the redesign renderer and the
 * legacy VLC one. */
bool isDark();

/* ---- Custom painters -------------------------------------------------
 * SeekSlider and SoundSlider draw themselves, so the stylesheet cannot
 * reach them. These routines reproduce the Figma geometry exactly and are
 * called from SeekStyle (seek bar) and SoundWidget (volume cone).
 *
 * Seek bar: 28px tall, track #090909, elapsed #371F06, 8px accent handle.
 * Volume:   a genuine cone 132px at the waist filling #7E9E13.
 * ------------------------------------------------------------------- */
struct SeekBarGeometry
{
    QRect groove;     /* full track rect, already inset to the handle */
    qreal position;   /* 0.0 .. 1.0 progress */
    bool enabled;
    bool hovered;     /* handle visible */
    qreal buffering;  /* 0.0 .. 1.0 buffered fraction, 0 = unknown */
    QList<int64_t> chapters; /* absolute ms, may be empty */
    int64_t length;   /* ms */
};

void paintSeekBar( QPainter &painter, const SeekBarGeometry &geo,
                   const QRect &widgetRect );
void paintVolumeCone( QPainter &painter, const QRect &rect, qreal value,
                      bool muted );

/* The default accent, and the 9 selectable accents offered in
 * Preferences -> Interface -> Theme -> Accent colour. */
static const char *const ACCENT_PALETTE[] = {
    "#D37918", /* default  - muted amber  */
    "#A03B26", /* rust              */
    "#8C721B", /* olive             */
    "#147528", /* green             */
    "#147A97", /* teal             */
    "#144497", /* blue              */
    "#6631B5", /* violet            */
    "#AB3C80", /* magenta           */
    "#7E9E13", /* yellow-green (volume cone) */
};
static const int ACCENT_PALETTE_SIZE = 9;

/* Surfaces, dark scheme. These are the values measured in the design. */
static const char *const DARK_TITLEBAR   = "#1B1B1B";
static const char *const DARK_RAIL       = "#191919";
static const char *const DARK_SIDEBAR    = "#0F0F0F";
static const char *const DARK_CONTENT    = "#2D2B29";
static const char *const DARK_CONTROLBAR = "#1B1B1B";
static const char *const DARK_TRACK      = "#090909";
static const char *const DARK_SEEKFILL   = "#371F06";
static const char *const DARK_ELEVATED   = "#232120";
static const char *const DARK_BORDER     = "#383634";

/* Surfaces, light scheme. */
static const char *const LIGHT_TITLEBAR   = "#F2F1F0";
static const char *const LIGHT_RAIL       = "#E9E7E5";
static const char *const LIGHT_SIDEBAR    = "#E2E0DE";
static const char *const LIGHT_CONTENT    = "#FAF9F8";
static const char *const LIGHT_CONTROLBAR = "#EFEEEC";
static const char *const LIGHT_TRACK      = "#D8D5D2";
static const char *const LIGHT_SEEKFILL   = "#C9C4BE";
static const char *const LIGHT_ELEVATED   = "#FFFFFF";
static const char *const LIGHT_BORDER     = "#D1CFCB";

/* Text and icons. */
static const char *const DARK_TEXT        = "#ECEEF0";
static const char *const DARK_TEXT_MUTED  = "#8F8F8F";
static const char *const DARK_ICON        = "#8F8F8F";
static const char *const LIGHT_TEXT       = "#1A1C1E";
static const char *const LIGHT_TEXT_MUTED = "#6E767D";
static const char *const LIGHT_ICON       = "#5C5F63";

/* Accent roles. */
static const char *const ACCENT            = "#D37918";
static const char *const ACCENT_HOVER      = "#E08A25";
static const char *const ACCENT_PRESSED    = "#B06413";
static const char *const ACCENT_SUBTLE_BG  = "rgba(211,121,24,0.16)";
static const char *const VOLUME_FILL       = "#7E9E13";
static const char *const TRACK_HANDLE      = "#D37918";

/* -------------------------------------------------------------- *
 * Metrics, expressed in logical pixels at a 1920-wide reference. *
 * -------------------------------------------------------------- */

static const int RADIUS            = 6;   /* "rectangular but less sharp" */
static const int RADIUS_SMALL      = 4;
static const int CONTROLBAR_HEIGHT = 54;
static const int TITLEBAR_HEIGHT   = 40;
static const int RAIL_WIDTH        = 50;
static const int SIDEBAR_WIDTH     = 376;
static const int SEEK_THICKNESS    = 28;
static const int PLAY_BUTTON_W     = 54;
static const int PLAY_BUTTON_H     = 28;
static const int ICON_SIZE         = 20;
static const int ICON_SIZE_SMALL   = 16;
static const int SEEK_HANDLE_W     = 8;
static const int VOLUME_WIDTH      = 132; /* cone waist */
static const int VOLUME_HEIGHT     = 28;

} /* namespace Theme */

#endif /* VLC_QT_STYLES_VLC_THEME_HPP */