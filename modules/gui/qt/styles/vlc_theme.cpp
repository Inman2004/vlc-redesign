/*****************************************************************************
 * vlc_theme.cpp : design tokens and helpers for the redesigned Qt interface
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "styles/vlc_theme.hpp"

#include <vlc_variables.h>

#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QScreen>
#include <QSvgRenderer>
#include <QToolButton>
#include <QMenu>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QVariantAnimation>
#include <QEvent>

namespace Theme
{

const char *const ACCENT_VALUES[] = {
    "#D37918", "#A03B26", "#8C721B", "#147528", "#147A97",
    "#144497", "#6631B5", "#AB3C80", "#7E9E13",
};
const char *const ACCENT_NAMES[] = {
    N_("Amber"), N_("Rust"), N_("Olive"), N_("Green"), N_("Teal"),
    N_("Blue"), N_("Violet"), N_("Magenta"), N_("Lime"),
};
const int ACCENT_COUNT = sizeof( ACCENT_VALUES ) / sizeof( ACCENT_VALUES[0] );

static bool    g_redesign  = true;
static bool    g_frameless = true;
static bool    g_dark      = true;
static QColor  g_accent( "#D37918" );
static Palette g_pal;
static intf_thread_t *g_intf = NULL;

static Palette darkPalette()
{
    Palette p;
    p.titlebar     = QColor( "#1B1B1B" );   /* measured */
    p.rail         = QColor( "#191919" );   /* measured */
    p.sidebar      = QColor( "#0F0F0F" );   /* measured */
    p.content      = QColor( "#2D2B29" );   /* measured */
    p.controlbar   = QColor( "#1B1B1B" );   /* measured */
    p.track        = QColor( "#090909" );   /* measured */
    p.elevated     = QColor( "#242322" );
    p.border       = QColor( "#373737" );   /* measured: header divider */
    p.hover        = QColor( 255, 255, 255, 22 );
    p.text         = QColor( "#D6D6D6" );
    p.textMuted    = QColor( "#8F8F8F" );   /* measured */
    p.textFaint    = QColor( "#676767" );   /* measured: item counts */
    p.icon         = QColor( "#8F8F8F" );   /* measured */
    p.iconHover    = QColor( "#D6D6D6" );
    p.iconDisabled = QColor( "#4A4A4A" );
    return p;
}

static Palette lightPalette()
{
    Palette p;
    p.titlebar     = QColor( "#F1F0EF" );
    p.rail         = QColor( "#EBEAE8" );
    p.sidebar      = QColor( "#E3E1DF" );
    p.content      = QColor( "#FAF9F8" );
    p.controlbar   = QColor( "#F1F0EF" );
    p.track        = QColor( "#D9D6D2" );
    p.elevated     = QColor( "#FFFFFF" );
    p.border       = QColor( "#D3D0CC" );
    p.hover        = QColor( 0, 0, 0, 18 );
    p.text         = QColor( "#1E1E1E" );
    p.textMuted    = QColor( "#6B6B6B" );
    p.textFaint    = QColor( "#8C8C8C" );
    p.icon         = QColor( "#6B6B6B" );
    p.iconHover    = QColor( "#1E1E1E" );
    p.iconDisabled = QColor( "#B5B5B5" );
    return p;
}

void init( intf_thread_t *p_intf )
{
    g_intf = p_intf;
    g_redesign  = var_InheritBool( p_intf, "qt-redesign" );
    g_frameless = g_redesign && var_InheritBool( p_intf, "qt-redesign-frameless" );

    char *psz_theme = var_InheritString( p_intf, "qt-theme" );
    QString theme = psz_theme ? QString::fromUtf8( psz_theme ) : QString( "dark" );
    free( psz_theme );
    if( theme == "light" )
        g_dark = false;
    else if( theme == "system" )
        g_dark = QGuiApplication::palette().color( QPalette::Window ).lightness() < 128;
    else
        g_dark = true;

    char *psz_accent = var_InheritString( p_intf, "qt-accent-color" );
    QColor accentColor( psz_accent ? QString::fromUtf8( psz_accent ) : QString() );
    free( psz_accent );
    g_accent = accentColor.isValid() ? accentColor : QColor( ACCENT_VALUES[0] );

    g_pal = g_dark ? darkPalette() : lightPalette();

    /* Bundled UI font: Google Sans (SIL Open Font License 1.1, see
     * pixmaps/redesign/fonts/OFL.txt) */
    if( g_redesign )
    {
        QFontDatabase::addApplicationFont( ":/redesign/fonts/GoogleSans-Regular.ttf" );
        QFontDatabase::addApplicationFont( ":/redesign/fonts/GoogleSans-Medium.ttf" );
    }
}

bool redesign()  { return g_redesign; }
bool frameless() { return g_frameless; }
bool isDark()    { return g_dark; }
const Palette &colors() { return g_pal; }
QColor accent()  { return g_accent; }
QColor accentHover()   { return g_accent.lighter( 112 ); }
QColor accentPressed() { return g_accent.darker( 118 ); }

static QColor mix( const QColor &a, const QColor &b, qreal t )
{
    return QColor::fromRgbF( a.redF()   * ( 1 - t ) + b.redF()   * t,
                             a.greenF() * ( 1 - t ) + b.greenF() * t,
                             a.blueF()  * ( 1 - t ) + b.blueF()  * t );
}

QColor seekFill()
{
    /* The design draws #371F06 (accent at ~22% over the track), which is
     * nearly invisible. We keep the hue and raise it to 48% so the played
     * part reads at a glance. */
    return mix( g_pal.track, g_accent, g_dark ? 0.48 : 0.55 );
}

/*********************************************************************
 * Icons
 *********************************************************************/
QPixmap pixmap( const QString &name, int size, const QColor &color )
{
    static QHash<QString, QPixmap> cache;
    const qreal dpr = qApp->devicePixelRatio();
    const QString key = QString( "%1|%2|%3|%4" ).arg( name ).arg( size )
                            .arg( color.name( QColor::HexArgb ) ).arg( dpr );
    auto it = cache.constFind( key );
    if( it != cache.constEnd() )
        return *it;

    const int px = qRound( size * dpr );
    QImage img( px, px, QImage::Format_ARGB32_Premultiplied );
    img.fill( Qt::transparent );
    {
        QSvgRenderer renderer( QString( ":/redesign/%1.svg" ).arg( name ) );
        QPainter p( &img );
        p.setRenderHint( QPainter::Antialiasing );
        renderer.render( &p, QRectF( 0, 0, px, px ) );
        p.setCompositionMode( QPainter::CompositionMode_SourceIn );
        p.fillRect( img.rect(), color );
    }
    QPixmap pix = QPixmap::fromImage( img );
    pix.setDevicePixelRatio( dpr );
    cache.insert( key, pix );
    return pix;
}

QIcon icon( const QString &name, int size )
{
    QIcon ic;
    ic.addPixmap( pixmap( name, size, g_pal.icon ), QIcon::Normal, QIcon::Off );
    ic.addPixmap( pixmap( name, size, g_pal.iconHover ), QIcon::Active, QIcon::Off );
    ic.addPixmap( pixmap( name, size, g_pal.iconDisabled ), QIcon::Disabled, QIcon::Off );
    ic.addPixmap( pixmap( name, size, g_accent ), QIcon::Normal, QIcon::On );
    ic.addPixmap( pixmap( name, size, accentHover() ), QIcon::Active, QIcon::On );
    ic.addPixmap( pixmap( name, size, g_pal.iconDisabled ), QIcon::Disabled, QIcon::On );
    return ic;
}

void setupIconButton( QAbstractButton *b, const QString &name, int box, int glyph )
{
    b->setIcon( icon( name, glyph ) );
    b->setIconSize( QSize( glyph, glyph ) );
    b->setFixedSize( box, box );
    b->setFocusPolicy( Qt::NoFocus );
    b->setCursor( Qt::PointingHandCursor );
    QToolButton *tb = qobject_cast<QToolButton *>( b );
    if( tb )
        tb->setAutoRaise( true );
    installHoverFade( b );
}

/*********************************************************************
 * Hover fade
 *********************************************************************/
namespace {
class HoverFade : public QObject
{
public:
    HoverFade( QAbstractButton *b, int r ) : QObject( b ), button( b ), radius( r ), t( 0.0 )
    {
        anim = new QVariantAnimation( this );
        anim->setDuration( 120 );
        anim->setEasingCurve( QEasingCurve::OutCubic );
        QObject::connect( anim, &QVariantAnimation::valueChanged, this,
                          [this]( const QVariant &v ) { t = v.toReal(); button->update(); } );
        b->setAttribute( Qt::WA_Hover );
        b->installEventFilter( this );
    }
protected:
    bool eventFilter( QObject *, QEvent *e ) Q_DECL_OVERRIDE
    {
        switch( e->type() )
        {
        case QEvent::HoverEnter:
        case QEvent::Enter:
            fadeTo( button->isEnabled() ? 1.0 : 0.0 );
            break;
        case QEvent::HoverLeave:
        case QEvent::Leave:
        case QEvent::Hide:
            fadeTo( 0.0 );
            break;
        case QEvent::Paint:
            if( t > 0.0 )
            {
                QPainter p( button );
                p.setRenderHint( QPainter::Antialiasing );
                p.setPen( Qt::NoPen );
                QColor c = g_pal.hover;
                c.setAlphaF( c.alphaF() * t );
                p.setBrush( c );
                p.drawRoundedRect( QRectF( button->rect() ), radius, radius );
            }
            break;
        default:
            break;
        }
        return false;
    }
private:
    void fadeTo( qreal target )
    {
        if( qFuzzyCompare( t + 1, target + 1 ) && anim->state() != QAbstractAnimation::Running )
            return;
        anim->stop();
        anim->setStartValue( t );
        anim->setEndValue( target );
        anim->start();
    }
    QAbstractButton *button;
    QVariantAnimation *anim;
    int radius;
    qreal t;
};
}

void installHoverFade( QAbstractButton *b, int radius )
{
    if( b->property( "vlcHoverFade" ).toBool() )
        return;
    b->setProperty( "vlcHoverFade", true );
    new HoverFade( b, radius );
}

QString tooltipWithKey( intf_thread_t *p_intf, const QString &text, const char *keyOption )
{
    if( !p_intf )
        p_intf = g_intf;
    if( !p_intf )
        return text;
    char *psz_key = var_InheritString( p_intf, keyOption );
    QString key = psz_key ? QString::fromUtf8( psz_key ) : QString();
    free( psz_key );
    if( key.isEmpty() )
        return text;
    /* VLC stores several keys separated by tabs: show the first one */
    key = key.section( '\t', 0, 0 );
    return QString( "%1  <span style='color:%2'>%3</span>" )
               .arg( text.toHtmlEscaped(), g_pal.textMuted.name(), key.toHtmlEscaped() );
}

/*********************************************************************
 * Palette and stylesheet
 *********************************************************************/
static QPalette buildPalette()
{
    const Palette &c = g_pal;
    QPalette p;
    p.setColor( QPalette::Window,          c.titlebar );
    p.setColor( QPalette::WindowText,      c.text );
    p.setColor( QPalette::Base,            g_dark ? c.sidebar : c.elevated );
    p.setColor( QPalette::AlternateBase,   c.rail );
    p.setColor( QPalette::ToolTipBase,     c.elevated );
    p.setColor( QPalette::ToolTipText,     c.text );
    p.setColor( QPalette::Text,            c.text );
    p.setColor( QPalette::Button,          c.elevated );
    p.setColor( QPalette::ButtonText,      c.text );
    p.setColor( QPalette::BrightText,      Qt::white );
    p.setColor( QPalette::Highlight,       g_accent );
    p.setColor( QPalette::HighlightedText, Qt::white );
    p.setColor( QPalette::Link,            g_accent );
    p.setColor( QPalette::LinkVisited,     accentPressed() );
    p.setColor( QPalette::Light,           c.border.lighter( 130 ) );
    p.setColor( QPalette::Midlight,        c.border );
    p.setColor( QPalette::Mid,             c.border );
    p.setColor( QPalette::Dark,            c.track );
    p.setColor( QPalette::Shadow,          Qt::black );
    p.setColor( QPalette::PlaceholderText, c.textMuted );

    p.setColor( QPalette::Disabled, QPalette::WindowText, c.iconDisabled );
    p.setColor( QPalette::Disabled, QPalette::Text,       c.iconDisabled );
    p.setColor( QPalette::Disabled, QPalette::ButtonText, c.iconDisabled );
    p.setColor( QPalette::Inactive, QPalette::Highlight,  g_accent.darker( 130 ) );
    return p;
}

static QString rgba( const QColor &c )
{
    return QString( "rgba(%1,%2,%3,%4)" ).arg( c.red() ).arg( c.green() )
               .arg( c.blue() ).arg( c.alpha() );
}

static QString buildStyleSheet()
{
    const Palette &c = g_pal;
    QHash<QString, QString> t;
    t["titlebar"]   = c.titlebar.name();
    t["rail"]       = c.rail.name();
    t["sidebar"]    = c.sidebar.name();
    t["content"]    = c.content.name();
    t["bar"]        = c.controlbar.name();
    t["track"]      = c.track.name();
    t["elevated"]   = c.elevated.name();
    t["border"]     = c.border.name();
    t["hover"]      = rgba( c.hover );
    t["text"]       = c.text.name();
    t["muted"]      = c.textMuted.name();
    t["faint"]      = c.textFaint.name();
    t["menu"]       = g_dark ? QString( "#232323" ) : c.elevated.name();
    t["menuBorder"] = g_dark ? QString( "#2E2E2E" ) : c.border.name();
    t["iconDisabled"] = c.iconDisabled.name();
    t["accent"]     = g_accent.name();
    t["accentHov"]  = accentHover().name();
    t["accentPre"]  = accentPressed().name();
    t["accentSoft"] = rgba( QColor( g_accent.red(), g_accent.green(), g_accent.blue(), 46 ) );
    /* adjustment panel, measured on the Figma "Subtitles" frame */
    t["adjBg"]      = g_dark ? QString( "#0E0E0E" ) : QString( "#F4F3F2" );
    t["adjTitle"]   = g_dark ? QString( "#969696" ) : QString( "#4A4A4A" );
    t["adjLabel"]   = g_dark ? QString( "#6B6B6B" ) : QString( "#6E6E6E" );
    t["adjRule"]    = g_dark ? QString( "#222222" ) : QString( "#E2E0DE" );
    t["adjBorder"]  = g_dark ? QString( "#191919" ) : QString( "#DAD7D4" );
    t["adjBorderHi"] = g_dark ? QString( "#2C2C2C" ) : QString( "#C4C0BC" );
    t["adjCard"]    = c.titlebar.name();

    QString ss = QString::fromLatin1(
    /* Fonts: bundled Google Sans, falling back to the platform UI font. */
    "#VLCMainWindow, #VLCMainWindow QWidget { font-family: 'Google Sans', 'Noto Sans', sans-serif; }\n"

    /* Window chrome */
    "#VLCCentral { background: @titlebar; }\n"
    "#VLCHeader { background: @titlebar; }\n"
    "#VLCHeader QLabel#HeaderCrumb { color: @muted; font-size: 15px; }\n"
    "#VLCHeader QLabel#HeaderTitle { color: @muted; font-size: 15px; }\n"
    "#VLCHeader QFrame#HeaderDivider { background: @border; }\n"
    "#VLCRail { background: @rail; }\n"
    "#VLCContent { background: @content; border-top-right-radius: 8px;"
    "  border-bottom-right-radius: 8px; }\n"
    "#VLCContent > QStackedWidget, #VLCContent BackgroundWidget { background: @content; }\n"

    /* Flat icon buttons used by header, rail, sidebar and control bar */
    "#VLCHeader QToolButton, #VLCRail QToolButton, #VLCSidebar QToolButton,"
    " #VLCControlBar QToolButton {"
    "  background: transparent; border: none; border-radius: 6px; padding: 0; }\n"
    /* hover backgrounds are animated by installHoverFade() */
    "#VLCHeader QToolButton:pressed, #VLCRail QToolButton:pressed, #VLCSidebar QToolButton:pressed,"
    " #VLCControlBar QToolButton:pressed { background: @accentSoft; }\n"
    "#VLCRail QToolButton:checked, #VLCHeader QToolButton:checked { background: transparent; }\n"
    "#VLCHeader QToolButton#WinClose:hover { background: #C42B1C; }\n"
    "#VLCHeader QToolButton::menu-indicator, #VLCControlBar QToolButton::menu-indicator,"
    " #VLCSidebar QToolButton::menu-indicator { image: none; width: 0; }\n"

    /* Control bar */
    "#VLCControlBar { background: @bar; }\n"
    "#VLCControlBar QToolButton#PlayButton { background: @accent; border-radius: 6px; }\n"
    "#VLCControlBar QToolButton#PlayButton:hover { background: @accentHov; }\n"
    "#VLCControlBar QToolButton#PlayButton:pressed { background: @accentPre; }\n"
    "#VLCControlBar TimeLabel, #VLCFullscreenBar TimeLabel { color: @muted; font-size: 15px;"
    "  padding: 0; margin: 0; background: transparent; }\n"
    "#VLCControlBar SpeedLabel { color: @muted; }\n"

    /* Sidebar */
    "#VLCSidebar { background: @sidebar; }\n"
    "#VLCSidebar QLabel#SidebarTitle { color: @muted; font-size: 16px; }\n"
    "#VLCSidebar QLabel#SidebarCount { color: @faint; font-size: 14px; }\n"
    "#VLCSidebar QFrame#SidebarRule { background: @rail; }\n"
    "#VLCSidebar QAbstractScrollArea { qproperty-horizontalScrollBarPolicy: ScrollBarAlwaysOff; }\n"
    "#VLCSidebar QAbstractItemView { background: @sidebar; border: none; color: @text;"
    "  outline: 0; selection-background-color: @accentSoft; selection-color: @text;"
    "  font-size: 13px; }\n"
    "#VLCSidebar QAbstractItemView::item { padding: 6px 4px; border: none; border-radius: 6px; }\n"
    "#VLCSidebar QAbstractItemView::item:hover { background: @hover; }\n"
    "#VLCSidebar QAbstractItemView::item:selected { background: @accentSoft; color: @text; }\n"
    "#VLCSidebar QHeaderView::section { background: @sidebar; color: @muted; border: none;"
    "  padding: 4px 6px; font-size: 12px; }\n"
    "#VLCSidebar QLineEdit { background: @track; color: @text; border: 1px solid @border;"
    "  border-radius: 6px; padding: 5px 8px; font-size: 13px; }\n"
    "#VLCSidebar QLineEdit:focus { border-color: @accent; }\n"
    "#VLCSidebar QListWidget#RecentsList::item { padding: 8px 6px; }\n"

    /* Splitter between sidebar and content */
    "#VLCBody QSplitter::handle { background: @titlebar; }\n"

    /* Adjustment sidebar (P3) */
    "#VLCAdjust { background: @adjBg; border-radius: 8px; }\n"
    "#VLCAdjust QScrollArea#AdjustScroll, #VLCAdjust QWidget#AdjustPage { background: transparent; border: none; }\n"
    "#VLCAdjust QWidget#AdjustTabs { background: @track; border-top-left-radius: 8px;"
    "  border-top-right-radius: 8px; }\n"
    "#VLCAdjust QWidget#AdjustTabs QToolButton { color: @adjLabel; font-size: 14px; border: none;"
    "  background: transparent; padding: 0 6px; }\n"
    "#VLCAdjust QWidget#AdjustTabs QToolButton:hover, #VLCAdjust QWidget#AdjustTabs QToolButton:checked"
    " { color: @muted; }\n"
    "#VLCAdjust QWidget#AdjustTabLine { background: @accent; }\n"
    "#VLCAdjust QScrollBar#AdjustOverlayBar { background: transparent; width: 6px; margin: 0; }\n"
    "#VLCAdjust QScrollBar#AdjustOverlayBar::handle { background: rgba(255,255,255,28); border-radius: 3px;"
    "  min-height: 32px; }\n"
    "#VLCAdjust QScrollBar#AdjustOverlayBar::handle:hover { background: rgba(255,255,255,56); }\n"
    "#VLCAdjust QScrollBar#AdjustOverlayBar::add-line, #VLCAdjust QScrollBar#AdjustOverlayBar::sub-line,"
    " #VLCAdjust QScrollBar#AdjustOverlayBar::add-page, #VLCAdjust QScrollBar#AdjustOverlayBar::sub-page"
    " { background: none; height: 0; }\n"
    "#VLCAdjust QFrame#AdjustRule { background: @adjRule; border: none; }\n"
    "#VLCAdjust QFrame#AdjustCardRule { background: @adjRule; border: none; margin: 2px 0; }\n"
    "#VLCAdjust QLabel#AdjustSectionTitle { color: @adjTitle; font-size: 17px; }\n"
    "#VLCAdjust QLabel#AdjustLabel { color: @adjLabel; font-size: 15px; }\n"
    "#VLCAdjust QLabel#AdjustLabel:disabled, #VLCAdjust QLabel#AdjustInlineValue:disabled { color: @iconDisabled; }\n"
    "#VLCAdjust QLabel#AdjustInlineValue { color: @muted; font-size: 15px; }\n"
    "#VLCAdjust QLabel#AdjustScale { color: @adjLabel; font-size: 13px; padding: 0 1px; }\n"
    /* value boxes: 140x36, #090909 well, 2px #191919 border */
    "#VLCAdjust QComboBox, #VLCAdjust QAbstractSpinBox#AdjustSpin, #VLCAdjust QLineEdit#AdjustEdit,"
    " #VLCAdjust QWidget#AdjustColor, #VLCAdjust QToolButton#AdjustButton, #VLCAdjust QLabel#AdjustValueSmall"
    " { background: @track; border: 2px solid @adjBorder; border-radius: 6px; color: @muted;"
    "  font-size: 15px; padding: 0 12px; selection-background-color: @accentSoft; selection-color: @text; }\n"
    "#VLCAdjust QComboBox:hover, #VLCAdjust QAbstractSpinBox#AdjustSpin:hover, #VLCAdjust QLineEdit#AdjustEdit:hover,"
    " #VLCAdjust QWidget#AdjustColor:hover, #VLCAdjust QToolButton#AdjustButton:hover { border-color: @adjBorderHi; }\n"
    "#VLCAdjust QComboBox:focus, #VLCAdjust QAbstractSpinBox#AdjustSpin:focus, #VLCAdjust QLineEdit#AdjustEdit:focus"
    " { border-color: @accentPre; }\n"
    "#VLCAdjust QComboBox:disabled, #VLCAdjust QAbstractSpinBox#AdjustSpin:disabled,"
    " #VLCAdjust QToolButton#AdjustButton:disabled { color: @iconDisabled; }\n"
    "#VLCAdjust QToolButton#AdjustButton { text-align: left; }\n"
    "#VLCAdjust QToolButton#AdjustButton[active=\"true\"] { color: @accent; }\n"
    "#VLCAdjust QLabel#AdjustValueSmall { min-width: 108px; max-width: 108px; min-height: 28px;"
    "  max-height: 28px; font-size: 14px; border: none; }\n"
    "#VLCAdjust QComboBox::drop-down { border: none; width: 26px; }\n"
    "#VLCAdjust QComboBox::down-arrow { image: url(:/redesign/chevron-down.svg); width: 14px; height: 14px; }\n"
    "#VLCAdjust QComboBox#AdjustComboWide { border-radius: 8px; padding-left: 16px; }\n"
    "#VLCAdjust QComboBox QAbstractItemView { background: @menu; border: 1px solid @menuBorder; color: @muted;"
    "  selection-background-color: @hover; selection-color: @text; outline: 0; padding: 4px; }\n"
    "#VLCAdjust QLineEdit#AdjustColorText { background: transparent; border: none; padding: 0; color: @muted;"
    "  font-size: 15px; }\n"
    "#VLCAdjust QWidget#AdjustColor { padding: 0; }\n"
    "#VLCAdjust QWidget#AdjustCard { background: @adjCard; border-radius: 8px; }\n"
    "#VLCAdjust QLabel#AdjustCardTitle { color: @adjLabel; font-size: 15px; }\n"
    "#VLCAdjust QWidget#AdjustCropBox { background: @track; border-radius: 6px; }\n"
    "#VLCAdjust QSpinBox#AdjustCropSpin { background: transparent; border: none; color: @muted;"
    "  font-size: 15px; selection-background-color: @accentSoft; }\n"
    "#VLCAdjust QToolButton#AdjustLink { border: none; background: transparent; border-radius: 6px; }\n"
    "#VLCAdjust QToolButton#AdjustLink:hover { background: @hover; }\n"
    "#VLCAdjust QWidget#AdjustFooter { background: @adjBg; border-top: 1px solid @adjRule;"
    "  border-bottom-left-radius: 8px; border-bottom-right-radius: 8px; }\n"
    "#VLCAdjust QPushButton#PresetButton { background: @track; border: 2px solid @track; border-radius: 8px;"
    "  color: @muted; font-size: 15px; text-align: left; padding: 0 14px; }\n"
    "#VLCAdjust QPushButton#PresetButton:hover { border-color: @adjBorderHi; }\n"
    "#VLCAdjust QLabel#PresetLabel { color: @muted; font-size: 15px; background: transparent; }\n"
    "#VLCAdjust QPushButton#PresetButton::menu-indicator { image: none; width: 0; }\n"
    "#VLCAdjust QToolButton#AdvancedLink { color: @adjLabel; font-size: 15px; border: none;"
    "  background: transparent; padding: 4px 6px; }\n"
    "#VLCAdjust QToolButton#AdvancedLink:hover { color: @muted; }\n"

    /* Menus and tooltips, application wide */
    /* Menus, measured on the "right-click menu" frame: #232323, 45px rows,
     * 15px text, icon column 20-36, text at 49, shortcut flush right */
    "QMenu { background: @menu; color: @muted; border: 1px solid @menuBorder;"
    "  border-radius: 10px; padding: 6px 0; font-size: 15px; }\n"
    "QMenu::item { padding: 12px 20px 12px 14px; margin: 0 6px; border-radius: 6px;"
    "  background: transparent; }\n"
    "QMenu::item:selected { background: @hover; color: @text; }\n"
    "QMenu::item:disabled { color: @iconDisabled; }\n"
    "QMenu::separator { height: 1px; background: @menuBorder; margin: 4px 14px; }\n"
    "QMenu::icon { padding-left: 14px; }\n"
    "QMenu::right-arrow { image: url(:/redesign/chevron.svg); width: 12px; height: 12px;"
    "  padding-right: 10px; }\n"
    "QMenu::indicator { width: 14px; height: 14px; padding-left: 14px; }\n"
    "QToolTip { background: @elevated; color: @text; border: 1px solid @border;"
    "  padding: 4px 8px; border-radius: 4px; }\n"

    /* Thin scrollbars inside the redesigned surfaces */
    "#VLCMainWindow QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }\n"
    "#VLCMainWindow QScrollBar::handle:vertical { background: @border; border-radius: 3px;"
    "  min-height: 24px; }\n"
    "#VLCMainWindow QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }\n"
    "#VLCMainWindow QScrollBar::handle:horizontal { background: @border; border-radius: 3px;"
    "  min-width: 24px; }\n"
    "#VLCMainWindow QScrollBar::add-line, #VLCMainWindow QScrollBar::sub-line,"
    " #VLCMainWindow QScrollBar::add-page, #VLCMainWindow QScrollBar::sub-page"
    " { background: none; width: 0; height: 0; }\n"
    );

    /* Longest keys first so "accentHov" is not eaten by "accent". */
    QStringList keys = t.keys();
    std::sort( keys.begin(), keys.end(), []( const QString &a, const QString &b ) {
        return a.size() > b.size(); } );
    foreach( const QString &k, keys )
        ss.replace( "@" + k, t.value( k ) );
    return ss;
}

/* 20px menu icons (Qt's default small icon is 16px) */
namespace {
class RedesignStyle : public QProxyStyle
{
public:
    RedesignStyle( QStyle *base ) : QProxyStyle( base ) {}
    int pixelMetric( PixelMetric m, const QStyleOption *o, const QWidget *w ) const Q_DECL_OVERRIDE
    {
        if( m == PM_SmallIconSize && qobject_cast<const QMenu *>( w ) )
            return 20;
        return QProxyStyle::pixelMetric( m, o, w );
    }
};
}

/* Menus get rounded corners: needs a translucent, shadow-less window */
namespace {
class MenuPolisher : public QObject
{
public:
    MenuPolisher( QObject *parent ) : QObject( parent ) {}
protected:
    bool eventFilter( QObject *obj, QEvent *e ) Q_DECL_OVERRIDE
    {
        if( e->type() == QEvent::Polish )
        {
            QMenu *m = qobject_cast<QMenu *>( obj );
            if( m && !m->testAttribute( Qt::WA_TranslucentBackground ) )
            {
                m->setAttribute( Qt::WA_TranslucentBackground );
                m->setWindowFlags( m->windowFlags() | Qt::FramelessWindowHint
                                   | Qt::NoDropShadowWindowHint );
            }
        }
        return false;
    }
};
}

void apply()
{
    if( !g_redesign )
        return;
    if( !dynamic_cast<RedesignStyle *>( qApp->style() ) )
        qApp->setStyle( new RedesignStyle( QStyleFactory::create( qApp->style()->objectName() ) ) );
    static MenuPolisher *polisher = NULL;
    if( !polisher )
    {
        polisher = new MenuPolisher( qApp );
        qApp->installEventFilter( polisher );
    }
    QFont font( "Google Sans" );
    font.setPointSizeF( qApp->font().pointSizeF() );
    qApp->setFont( font );
    qApp->setPalette( buildPalette() );
    qApp->setStyleSheet( buildStyleSheet() );
}

/*********************************************************************
 * Painters
 *********************************************************************/
void paintSeekBar( QPainter *p, const QRectF &groove, qreal position,
                   qreal buffering, const QList<qreal> &chapters,
                   bool enabled, qreal hover, int hoverX )
{
    p->save();
    p->setRenderHint( QPainter::Antialiasing );
    p->setPen( Qt::NoPen );

    /* Track */
    p->setBrush( g_pal.track );
    p->drawRoundedRect( groove, SEEK_RADIUS, SEEK_RADIUS );

    QPainterPath clip;
    clip.addRoundedRect( groove, SEEK_RADIUS, SEEK_RADIUS );
    p->setClipPath( clip );

    /* Buffered part of a network stream, very faint */
    if( buffering > 0.0 && buffering < 1.0 )
    {
        QColor buf = g_pal.textMuted;
        buf.setAlpha( 28 );
        p->setBrush( buf );
        p->drawRect( QRectF( groove.left(), groove.top(),
                             groove.width() * buffering, groove.height() ) );
    }

    const qreal handleX = groove.left() + SEEK_HANDLE_W / 2.0
                        + position * ( groove.width() - SEEK_HANDLE_W );

    /* Played part */
    if( enabled && position >= 0.0 )
    {
        QColor fill = mix( seekFill(), seekFill().lighter( 125 ), hover );
        p->setBrush( fill );
        p->drawRect( QRectF( groove.left(), groove.top(),
                             handleX - groove.left(), groove.height() ) );
    }

    /* Chapter marks */
    if( !chapters.isEmpty() )
    {
        QColor mark = g_pal.textMuted;
        mark.setAlpha( 110 );
        p->setBrush( mark );
        foreach( qreal c, chapters )
        {
            const qreal x = groove.left() + SEEK_HANDLE_W / 2.0
                          + c * ( groove.width() - SEEK_HANDLE_W );
            p->drawRect( QRectF( x - 0.75, groove.top() + 4, 1.5, 5 ) );
            p->drawRect( QRectF( x - 0.75, groove.bottom() - 9, 1.5, 5 ) );
        }
    }
    /* Preview marker under the cursor */
    if( enabled && hoverX >= 0 && hover > 0.0 )
    {
        QColor m = g_pal.text;
        m.setAlphaF( 0.35 * hover );
        p->setBrush( m );
        p->drawRect( QRectF( hoverX - 1, groove.top(), 2, groove.height() ) );
    }
    p->setClipping( false );

    /* Handle */
    if( enabled && position >= 0.0 )
    {
        /* grows slightly while hovered, up to the track height */
        const qreal hw = SEEK_HANDLE_W + 2.0 * hover;
        const qreal hh = qMin<qreal>( SEEK_HANDLE_H + 4.0 * hover, groove.height() );
        const QRectF handle( handleX - hw / 2.0, groove.center().y() - hh / 2.0, hw, hh );
        p->setBrush( mix( g_accent, accentHover(), hover ) );
        p->drawRoundedRect( handle, 2.5, 2.5 );
    }
    p->restore();
}

/* The volume control is a horizontal cone: thin at the quiet end, full
 * height at the loud end, with softened corners. */
static QPainterPath wedgePath( const QRectF &r, qreal tipH )
{
    const qreal cy = r.center().y();
    const qreal rr = 3.0;            /* corner radius at the wide end */
    const qreal tr = tipH / 2.0;     /* the thin end is a half circle */
    QPainterPath path;
    path.moveTo( r.left() + tr, cy - tipH / 2.0 );
    path.lineTo( r.right() - rr, r.top() );
    path.quadTo( r.right(), r.top(), r.right(), r.top() + rr );
    path.lineTo( r.right(), r.bottom() - rr );
    path.quadTo( r.right(), r.bottom(), r.right() - rr, r.bottom() );
    path.lineTo( r.left() + tr, cy + tipH / 2.0 );
    path.arcTo( QRectF( r.left(), cy - tr, tipH, tipH ), 270, -180 );
    path.closeSubpath();
    return path;
}

void paintVolumeWedge( QPainter *p, const QRectF &rect, qreal fraction,
                       bool muted, bool hovered )
{
    p->save();
    p->setRenderHint( QPainter::Antialiasing );
    p->setPen( Qt::NoPen );

    const QPainterPath outer = wedgePath( rect, VOLUME_TIP_H );
    p->setBrush( g_pal.track );
    p->drawPath( outer );

    fraction = qBound( 0.0, fraction, 1.0 );
    if( fraction > 0.0 )
    {
        /* inner wedge, inset by 2px, clipped to the level */
        const QRectF inner = rect.adjusted( 2.5, 2.5, -2.5, -2.5 );
        const QPainterPath fillPath = wedgePath( inner, VOLUME_TIP_H - 5 );
        QLinearGradient g( inner.topLeft(), inner.topRight() );
        if( muted )
        {
            g.setColorAt( 0, g_pal.iconDisabled );
            g.setColorAt( 1, g_pal.iconDisabled );
        }
        else
        {
            QColor hi( VOLUME_FILL_HI ), lo( VOLUME_FILL );
            if( hovered ) { hi = hi.lighter( 110 ); lo = lo.lighter( 110 ); }
            g.setColorAt( 0, hi );
            g.setColorAt( 1, lo );
        }
        p->setClipRect( QRectF( inner.left(), rect.top(),
                                inner.width() * fraction, rect.height() ) );
        p->setBrush( g );
        p->drawPath( fillPath );
    }
    p->restore();
}

} /* namespace Theme */
