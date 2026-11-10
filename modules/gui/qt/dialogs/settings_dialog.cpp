/*****************************************************************************
 * settings_dialog.cpp : the redesigned Settings window (P6)
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
 * Geometry, measured on "Settings - global.png" (window-relative, px):
 *   window 1019 x 841, #0E0E0E; header strip 0..48.6 #090909
 *   search 7.8,7.8 194.4x33 #151515; title x 234, centred at 25.5
 *   category panel 7.8,56.4 194.4x776.8 #181818; rows 39px on a 46.6px
 *   pitch, group labels 33.8px; selected row #222222 inset 6.2px
 *   content from x 234 to 995.5; controls 310px wide, rows 9.8px apart,
 *   37px from a section's last row to the next heading, 15px under it
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "dialogs/settings_dialog.hpp"
#include "components/settings_controls.hpp"
#include "dialogs/preferences.hpp"
#include "dialogs/plugins.hpp"
#include "dialogs_provider.hpp"
#include "actions_manager.hpp"
#include "extensions_manager.hpp"
#include "input_manager.hpp"
#include "main_interface.hpp"
#include "recents.hpp"
#include "styles/vlc_theme.hpp"

#include <vlc_aout.h>
#include <vlc_configuration.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>

#include "../../audio_filter/equalizer_presets.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

using namespace SettingsUI;

namespace {

enum
{
    SHADOW = 24,
    WIN_W = 1019, WIN_H = 841,
    HEADER_H = 49,
    SIDE_W = 202,          /* left column, panel 7.8..202.2 */
    EDGE = 8,              /* 7.8: panel and search inset */
    CONTENT_X = 234,
    CONTENT_RIGHT = 24,    /* 1019 - 995.5 */
    ROW_GAP = 10,          /* 9.8 */
    SECTION_GAP = 34,      /* last row -> next heading text: 37 */
    HEADING_GAP = 13,      /* heading text -> first row: 15 */
};

QString qtKey( const QString &k ) { return QString( "Settings/" ) + k; }

/*********************************************************************
 * The category list (painted)
 *********************************************************************/
struct NavEntry
{
    bool group;
    QString text, icon;
    int page;
    bool visible;
};

} /* namespace */

class SettingsNav : public QWidget
{
public:
    SettingsNav( QWidget *parent ) : QWidget( parent ), current( 0 ), hover( -1 )
    {
        setMouseTracking( true );
        setCursor( Qt::PointingHandCursor );
        setFocusPolicy( Qt::NoFocus );
    }
    void addGroup( const QString &t ) { entries << NavEntry{ true, t, QString(), -1, true }; }
    void addItem( const QString &t, const QString &icon, int page )
    {
        entries << NavEntry{ false, t, icon, page, true };
    }
    void setCurrent( int page ) { current = page; update(); }
    void setPageVisible( int page, bool v )
    {
        for( NavEntry &e : entries )
            if( e.page == page ) e.visible = v;
        /* a group shows while one of its items does */
        for( int i = 0; i < entries.count(); i++ )
        {
            if( !entries[i].group ) continue;
            bool any = false;
            for( int j = i + 1; j < entries.count() && !entries[j].group; j++ )
                any = any || entries[j].visible;
            entries[i].visible = any;
        }
        update();
    }
    std::function<void( int )> onSelect;

protected:
    /* 4.7px top padding, group label rows 33.8, item rows 39, 7.6 apart */
    QList<QPair<QRectF, int> > layoutRows() const
    {
        QList<QPair<QRectF, int> > out;
        qreal y = 4.7;
        for( int i = 0; i < entries.count(); i++ )
        {
            if( !entries[i].visible ) continue;
            const qreal h = entries[i].group ? 33.8 : 39.0;
            out << qMakePair( QRectF( 6.2, y, width() - 12.4, h ), i );
            y += h + 7.6;
        }
        return out;
    }
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE
    {
        const Colors c = colors();
        QPainter p( this );
        p.setRenderHint( QPainter::Antialiasing );
        p.setPen( Qt::NoPen );
        p.setBrush( c.nav );
        p.drawRoundedRect( rect(), 8, 8 );
        p.setFont( SettingsUI::font( 14 ) );
        for( const auto &r : layoutRows() )
        {
            const NavEntry &e = entries[r.second];
            if( e.group )
            {
                p.setPen( c.faint );
                p.drawText( r.first.adjusted( 9.6, 0, 0, 0 ), Qt::AlignVCenter | Qt::AlignLeft, e.text );
                continue;
            }
            const bool sel = e.page == current;
            if( sel || r.second == hover )
            {
                QColor bg = c.navSelected;
                if( !sel ) bg.setAlphaF( 0.6 );
                p.setPen( Qt::NoPen );
                p.setBrush( bg );
                p.drawRoundedRect( r.first, 6, 6 );
            }
            const int g = 24;
            p.drawPixmap( qRound( r.first.left() + 7 ), qRound( r.first.center().y() - g / 2.0 ),
                          Theme::pixmap( e.icon, g, sel ? Theme::accent() : c.text ) );
            p.setPen( sel ? c.navSelectedText : c.text );
            p.drawText( r.first.adjusted( 43, 0, 0, 0 ), Qt::AlignVCenter | Qt::AlignLeft, e.text );
        }
    }
    void mouseMoveEvent( QMouseEvent *ev ) Q_DECL_OVERRIDE
    {
        int h = -1;
        for( const auto &r : layoutRows() )
            if( !entries[r.second].group && r.first.contains( ev->pos() ) ) h = r.second;
        if( h != hover ) { hover = h; update(); }
    }
    void leaveEvent( QEvent * ) Q_DECL_OVERRIDE { hover = -1; update(); }
    void mousePressEvent( QMouseEvent *ev ) Q_DECL_OVERRIDE
    {
        for( const auto &r : layoutRows() )
            if( !entries[r.second].group && r.first.contains( ev->pos() ) && onSelect )
                onSelect( entries[r.second].page );
    }
private:
    QList<NavEntry> entries;
    int current, hover;
};

/*********************************************************************
 * A row: label (+ info) on the left, its control on the right
 *********************************************************************/
class SettingsRow : public QWidget
{
public:
    SettingsRow( const QString &label, const QString &tip, QWidget *control,
                 const QString &section, QWidget *parent )
        : QWidget( parent ), available( true ), active( true ), info( NULL ),
          baseTip( tip ), ctl( control )
    {
        QHBoxLayout *l = new QHBoxLayout( this );
        l->setContentsMargins( 0, 0, 0, 0 );
        l->setSpacing( 0 );
        text = new QLabel( label, this );
        text->setObjectName( "SLabel" );
        text->setFont( SettingsUI::font( 14 ) );
        l->addWidget( text );
        if( !tip.isEmpty() )
        {
            l->addSpacing( 11 );
            info = new SInfo( tip, this );
            l->addWidget( info, 0, Qt::AlignVCenter );
        }
        l->addStretch( 1 );
        l->addSpacing( 16 );
        if( control )
        {
            control->setParent( this );
            if( control->sizePolicy().horizontalPolicy() != QSizePolicy::Fixed
             && control->maximumWidth() >= QWIDGETSIZE_MAX )
                control->setFixedWidth( CONTROL_W );
            l->addWidget( control, 0, Qt::AlignVCenter );
            setMinimumHeight( control->sizeHint().height() );
        }
        keywords = ( label + " " + tip + " " + section ).toLower();
    }
    void setAvailable( bool a, const QString &reason )
    {
        available = a;
        if( !a && !reason.isEmpty() )
        {
            baseTip = baseTip.isEmpty() ? reason : baseTip + "\n\n" + reason;
            if( info ) info->setToolTip( baseTip );
            else setToolTip( reason );
        }
        apply();
    }
    void setActive( bool a ) { active = a; apply(); }
    void setOptionName( const QString &n ) { option = n; keywords += " " + n; }
    void developerTip( bool dev )
    {
        if( !info ) return;
        info->setToolTip( dev && !option.isEmpty()
                          ? baseTip + QString( "\n\n--%1" ).arg( option ) : baseTip );
    }
    bool matches( const QStringList &words ) const
    {
        for( const QString &w : words )
            if( !keywords.contains( w ) ) return false;
        return true;
    }
    QWidget *control() const { return ctl; }

private:
    void apply()
    {
        const bool on = available && active;
        if( ctl ) ctl->setEnabled( on );
        text->setEnabled( on );
    }
    bool available, active;
    QLabel *text;
    SInfo *info;
    QString baseTip, option, keywords;
    QWidget *ctl;
};

/*********************************************************************
 * "Advanced Options >": 37px #181818 bar, text and chevron centred
 *********************************************************************/
class SAdvancedButton : public QAbstractButton
{
public:
    SAdvancedButton( QWidget *parent ) : QAbstractButton( parent ), open( false ), hovered( false )
    {
        setFixedHeight( 37 );
        setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
        setCursor( Qt::PointingHandCursor );
        setText( qtr( "Advanced Options" ) );
    }
    void setOpen( bool o ) { open = o; update(); }
protected:
    void enterEvent( QEvent * ) Q_DECL_OVERRIDE { hovered = true; update(); }
    void leaveEvent( QEvent * ) Q_DECL_OVERRIDE { hovered = false; update(); }
    void paintEvent( QPaintEvent * ) Q_DECL_OVERRIDE
    {
        const Colors c = colors();
        QPainter p( this );
        p.setRenderHint( QPainter::Antialiasing );
        p.setPen( Qt::NoPen );
        p.setBrush( hovered ? c.nav.lighter( 120 ) : c.nav );
        p.drawRoundedRect( rect(), 4, 4 );
        p.setFont( SettingsUI::font( 14 ) );
        const int tw = p.fontMetrics().horizontalAdvance( text() );
        const int g = 16, gap = 8;
        const int x = ( width() - tw - gap - g ) / 2;
        p.setPen( hovered ? c.strong : c.text );
        p.drawText( QRect( x, 0, tw + 2, height() ), Qt::AlignVCenter | Qt::AlignLeft, text() );
        p.drawPixmap( x + tw + gap, ( height() - g ) / 2,
                      Theme::pixmap( open ? "chevron-down" : "chevron", g, hovered ? c.strong : c.text ) );
    }
private:
    bool open, hovered;
};

/*********************************************************************
 * A page: sections, then an "Advanced Options" expander with more
 *********************************************************************/
class SettingsPage : public QWidget
{
public:
    struct Section
    {
        QLabel *heading;
        QWidget *box;
        QWidget *gap;       /* the 37px above the heading */
        QList<SettingsRow *> rows;
        QList<QWidget *> rowGaps;   /* the 9.8px above each row but the first */
        bool advanced;
    };

    SettingsPage( const QString &t, QWidget *parent )
        : QWidget( parent ), pageTitle( t ), inAdvanced( false ), advOpen( false ),
          advButton( NULL ), advBox( NULL )
    {
        outer = new QVBoxLayout( this );
        outer->setContentsMargins( CONTENT_X - SIDE_W, 28, CONTENT_RIGHT, 40 );
        outer->setSpacing( 0 );
        target = outer;
    }

    void section( const QString &name )
    {
        Section s;
        s.advanced = inAdvanced;
        s.box = new QWidget( this );
        QVBoxLayout *v = new QVBoxLayout( s.box );
        v->setContentsMargins( 0, 0, 0, 0 );
        v->setSpacing( 0 );
        /* the gap above a heading: 37px from the previous row (none on top) */
        const bool first = sections.isEmpty() || ( inAdvanced && advFirst );
        s.gap = new QWidget( s.box );
        s.gap->setFixedHeight( SECTION_GAP );
        s.gap->setVisible( !first );
        s.gap->setProperty( "firstOfBlock", first );
        v->addWidget( s.gap );
        advFirst = false;
        s.heading = NULL;
        if( !name.isEmpty() )
        {
            s.heading = new QLabel( name, s.box );
            s.heading->setObjectName( "SHeading" );
            s.heading->setFont( SettingsUI::font( 15, true ) );
            v->addWidget( s.heading );
            v->addSpacing( HEADING_GAP );
        }
        target->addWidget( s.box );
        sections << s;
    }

    void addRow( SettingsRow *r )
    {
        if( sections.isEmpty() )
            section( QString() );
        Section &s = sections.last();
        QVBoxLayout *v = static_cast<QVBoxLayout *>( s.box->layout() );
        QWidget *gap = new QWidget( s.box );
        gap->setFixedHeight( ROW_GAP );
        gap->setVisible( !s.rows.isEmpty() );
        v->addWidget( gap );
        s.rowGaps << gap;
        r->setParent( s.box );
        v->addWidget( r );
        s.rows << r;
    }

    /* everything after this goes under "Advanced Options" */
    void beginAdvanced()
    {
        outer->addSpacing( 24 );
        advButton = new SAdvancedButton( this );
        outer->addWidget( advButton );
        advBox = new QWidget( this );
        advLayout = new QVBoxLayout( advBox );
        advLayout->setContentsMargins( 0, 20, 0, 0 );
        advLayout->setSpacing( 0 );
        outer->addWidget( advBox );
        target = advLayout;
        inAdvanced = true;
        advFirst = true;
        QObject::connect( advButton, &QAbstractButton::clicked, [this]() { setAdvancedOpen( !advOpen ); } );
        setAdvancedOpen( false );
    }

    void setAdvancedOpen( bool o )
    {
        if( !advButton ) return;
        advOpen = o;
        advBox->setVisible( o );
        advButton->setOpen( o );
    }

    void finish() { outer->addStretch( 1 ); }
    bool isAdvancedOpen() const { return advOpen; }
    void setRightMargin( int m )
    {
        QMargins mg = outer->contentsMargins();
        if( mg.right() != m ) { mg.setRight( qMax( 0, m ) ); outer->setContentsMargins( mg ); }
    }

    /* returns the number of matching rows */
    int filter( const QStringList &words )
    {
        int total = 0, advHits = 0;
        const bool all = words.isEmpty();
        bool firstMain = true, firstAdv = true;
        for( Section &s : sections )
        {
            int hits = 0;
            const bool headingHit = !all && s.heading
                && words.size() == 1 && s.heading->text().toLower().contains( words[0] );
            for( int i = 0; i < s.rows.count(); i++ )
            {
                SettingsRow *r = s.rows[i];
                const bool m = all || headingHit || r->matches( words );
                r->setVisible( m );
                s.rowGaps[i]->setVisible( m && hits > 0 );
                hits += m;
            }
            const bool shown = hits > 0 || ( all && s.rows.isEmpty() );
            s.box->setVisible( shown );
            /* no gap above the first section still showing in its block */
            bool &firstInBlock = s.advanced ? firstAdv : firstMain;
            s.gap->setVisible( shown && !firstInBlock );
            if( shown ) firstInBlock = false;
            total += hits;
            if( s.advanced ) advHits += hits;
        }
        if( advButton )
        {
            advButton->setVisible( all || advHits > 0 );
            if( !all )
                setAdvancedOpen( advHits > 0 );
            else
                setAdvancedOpen( keepAdvancedOpen );
        }
        return total;
    }

    QString pageTitle;
    QList<Section> sections;
    bool keepAdvancedOpen = false;

private:
    QVBoxLayout *outer, *target, *advLayout = NULL;
    bool inAdvanced, advOpen, advFirst = false;
    SAdvancedButton *advButton;
    QWidget *advBox;
};

/*********************************************************************
 * Helpers
 *********************************************************************/
static QString configString( intf_thread_t *p_intf, const char *opt )
{
    char *psz = config_GetPsz( p_intf, opt );
    QString s = psz ? qfu( psz ) : QString();
    free( psz );
    return s;
}

static bool hasOption( const char *opt )
{
    return opt && config_FindConfig( opt ) != NULL;
}

static const QString notInBuild()
{
    return qtr( "Not available in this build of VLC." );
}

/*********************************************************************
 * The window
 *********************************************************************/
SettingsDialog::SettingsDialog( intf_thread_t *_p_intf )
    : QWidget( NULL ), dirty( false ), themeDirty( false ), building( true )
{
    p_intf = _p_intf;
    setObjectName( "VLCSettings" );
    setWindowTitle( qtr( "Settings" ) );
    setWindowFlags( Qt::Window | Qt::FramelessWindowHint );
    setAttribute( Qt::WA_TranslucentBackground );
    setWindowRole( "vlc-settings" );

    QWidget *mi = p_intf->p_sys->p_mi;
    if( mi )
        setWindowIcon( mi->windowIcon() );

    /* the shadowed body; everything below is placed by layoutChrome() */
    frame = new QWidget( this );
    frame->setObjectName( "SetFrame" );
    frame->installEventFilter( this );
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect( frame );
    shadow->setBlurRadius( 40 );
    shadow->setOffset( 0, 10 );
    shadow->setColor( QColor( 0, 0, 0, 150 ) );
    frame->setGraphicsEffect( shadow );

    search = new QLineEdit( frame );
    search->setObjectName( "SSearch" );
    search->setPlaceholderText( qtr( "Search" ) );
    search->setFont( SettingsUI::font( 14 ) );
    connect( search, &QLineEdit::textChanged, this, &SettingsDialog::applySearch );

    title = new QLabel( frame );
    title->setObjectName( "STitle" );
    title->setFont( SettingsUI::font( 16 ) );
    title->installEventFilter( this );

    closeButton = new QToolButton( frame );
    closeButton->setToolTip( qtr( "Close (Esc)" ) );
    Theme::setupIconButton( closeButton, "win-close", 32, 24 );
    connect( closeButton, &QToolButton::clicked, this, &QWidget::close );

    nav = new SettingsNav( frame );
    nav->onSelect = [this]( int p ) { showPage( p ); };
    nav->addGroup( qtr( "App" ) );
    nav->addItem( qtr( "General" ), "sliders", GENERAL );
    nav->addItem( qtr( "Interface" ), "cursor", INTERFACE );
    nav->addItem( qtr( "Privacy" ), "shield", PRIVACY );
    nav->addItem( qtr( "Controls" ), "keyboard", CONTROLS );
    nav->addGroup( qtr( "Media" ) );
    nav->addItem( qtr( "Playback" ), "play-outline", PLAYBACK );
    nav->addItem( qtr( "Video" ), "video-defaults", VIDEO );
    nav->addItem( qtr( "Audio" ), "audio", AUDIO );
    nav->addItem( qtr( "Subtitles" ), "subtitles", SUBTITLES );
    nav->addItem( qtr( "Library" ), "folder", LIBRARY );
    nav->addGroup( qtr( "System" ) );
    nav->addItem( qtr( "Network" ), "network", NETWORK );
    nav->addItem( qtr( "Performance" ), "sliders", PERFORMANCE );
    nav->addItem( qtr( "Recording" ), "record", RECORDING );
    nav->addItem( qtr( "Extensions" ), "puzzle", EXTENSIONS );

    stack = new QStackedWidget( frame );
    stack->setObjectName( "SStack" );

    static const char *const titles[PAGE_COUNT] = {
        N_("Global Settings"), N_("Interface Settings"), N_("Privacy Settings"),
        N_("Mouse, Keyboard & Remote Controls"), N_("Playback Preferences"),
        N_("Video Defaults"), N_("Audio Defaults"), N_("Subtitle Defaults"),
        N_("Playlists, Folders & Files"), N_("Network Connections & Security"),
        N_("Performance Settings"), N_("Recording, Casting & Streaming Out"),
        N_("Add-ons & Scripts") };
    for( int i = 0; i < PAGE_COUNT; i++ )
    {
        QScrollArea *sa = new QScrollArea( stack );
        sa->setObjectName( "SScroll" );
        sa->setFrameShape( QFrame::NoFrame );
        sa->setWidgetResizable( true );
        sa->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        SettingsPage *p = new SettingsPage( qtr( titles[i] ), sa );
        p->setObjectName( "SPage" );
        sa->setWidget( p );
        /* the 8px bar sits in the 24px right margin instead of pushing
         * the controls left */
        connect( sa->verticalScrollBar(), &QScrollBar::rangeChanged, p, [p, sa]( int, int max ) {
            p->setRightMargin( max > 0 ? CONTENT_RIGHT - sa->verticalScrollBar()->sizeHint().width() : CONTENT_RIGHT );
        } );
        stack->addWidget( sa );
        pages << p;
    }

    noResults = new QLabel( qtr( "No settings match your search." ), frame );
    noResults->setObjectName( "SNoResults" );
    noResults->setFont( SettingsUI::font( 14 ) );
    noResults->setAlignment( Qt::AlignCenter );
    noResults->hide();

    buildGeneral();
    buildInterface();
    buildPrivacy();
    buildControls();
    buildPlayback();
    buildVideo();
    buildAudio();
    buildSubtitles();
    buildLibrary();
    buildNetwork();
    buildPerformance();
    buildRecording();
    buildExtensions();
    for( SettingsPage *p : pages )
        p->finish();

    const bool dev = getSettings()->value( qtKey( "developerMode" ), false ).toBool();
    for( SettingsPage *p : pages )
    {
        p->keepAdvancedOpen = dev;
        p->setAdvancedOpen( dev );
    }
    updateDeveloperTips();

    restyle();
    refreshAll();
    building = false;
    showPage( GENERAL );
    resize( WIN_W + 2 * SHADOW, WIN_H + 2 * SHADOW );
}

SettingsDialog::~SettingsDialog()
{
    if( dirty )
        config_SaveConfigFile( p_intf );
}

void SettingsDialog::showPage( int page )
{
    if( page < 0 || page >= PAGE_COUNT )
        return;
    stack->setCurrentIndex( page );
    nav->setCurrent( page );
    title->setText( pages[page]->pageTitle );
}

void SettingsDialog::refreshAll()
{
    for( auto it = refreshers.begin(); it != refreshers.end(); ++it )
        it.value()();
}

void SettingsDialog::onRefresh( const char *option, std::function<void()> f )
{
    refreshers.insert( QString( option ), f );
}

void SettingsDialog::changed( const char *option )
{
    if( building )
        return;
    dirty = true;
    auto range = refreshers.equal_range( QString( option ) );
    for( auto it = range.first; it != range.second; ++it )
        it.value()();
}

/*********************************************************************
 * Chrome
 *********************************************************************/
void SettingsDialog::layoutChrome()
{
    frame->setGeometry( SHADOW, SHADOW, width() - 2 * SHADOW, height() - 2 * SHADOW );
    const int W = frame->width(), H = frame->height();
    search->setGeometry( EDGE, EDGE, SIDE_W - EDGE, 33 );
    const QSize cs = closeButton->size();
    closeButton->move( W - 30 - cs.width() / 2, 24 - cs.height() / 2 );
    title->setGeometry( CONTENT_X, 0, W - CONTENT_X - 60, HEADER_H + 2 );
    nav->setGeometry( EDGE, 56, SIDE_W - EDGE, H - 56 - EDGE );
    stack->setGeometry( SIDE_W, HEADER_H, W - SIDE_W, H - HEADER_H - 1 );
    noResults->setGeometry( stack->geometry() );
}

void SettingsDialog::resizeEvent( QResizeEvent * ) { layoutChrome(); }

void SettingsDialog::paintEvent( QPaintEvent * ) {}

bool SettingsDialog::eventFilter( QObject *o, QEvent *e )
{
    if( o == frame && e->type() == QEvent::Paint )
    {
        const Colors c = colors();
        QPainter p( frame );
        p.setRenderHint( QPainter::Antialiasing );
        QPainterPath clip;
        clip.addRoundedRect( QRectF( frame->rect() ), 12, 12 );
        p.setClipPath( clip );
        p.fillRect( frame->rect(), c.window );
        p.fillRect( QRect( 0, 0, frame->width(), HEADER_H ), c.header );
        /* the search well */
        p.setPen( Qt::NoPen );
        p.setBrush( c.search );
        p.drawRoundedRect( QRectF( search->geometry() ), 6, 6 );
        p.drawPixmap( search->x() + 9, search->y() + ( search->height() - 20 ) / 2,
                      Theme::pixmap( "search", 20, c.faint ) );
        return true;
    }
    if( ( o == frame || o == title ) && e->type() == QEvent::MouseButtonPress )
    {
        QMouseEvent *me = static_cast<QMouseEvent *>( e );
        const QPoint pt = o == title ? title->mapTo( frame, me->pos() ) : me->pos();
        if( me->button() == Qt::LeftButton && pt.y() < HEADER_H && windowHandle() )
        {
            windowHandle()->startSystemMove();
            return true;
        }
    }
    return QWidget::eventFilter( o, e );
}

void SettingsDialog::keyPressEvent( QKeyEvent *e )
{
    if( e->key() == Qt::Key_Escape )
    {
        if( !search->text().isEmpty() )
            search->clear();
        else
            close();
        return;
    }
    if( e->matches( QKeySequence::Find ) )
    {
        search->setFocus();
        search->selectAll();
        return;
    }
    QWidget::keyPressEvent( e );
}

void SettingsDialog::showEvent( QShowEvent *e )
{
    /* centred on the player, never bigger than the screen */
    QWidget *mi = p_intf->p_sys->p_mi;
    QScreen *scr = mi && mi->windowHandle() ? mi->windowHandle()->screen() : QGuiApplication::primaryScreen();
    QRect avail = scr ? scr->availableGeometry() : QRect( 0, 0, 1920, 1080 );
    QSize s( qMin( (int)WIN_W, avail.width() - 40 ), qMin( (int)WIN_H, avail.height() - 40 ) );
    resize( s + QSize( 2 * SHADOW, 2 * SHADOW ) );
    QPoint ctr = mi && mi->isVisible() ? mi->geometry().center() : avail.center();
    QRect g( QPoint( 0, 0 ), size() );
    g.moveCenter( ctr );
    g.moveLeft( qBound( avail.left() - SHADOW, g.left(), avail.right() - g.width() + SHADOW ) );
    g.moveTop( qBound( avail.top() - SHADOW, g.top(), avail.bottom() - g.height() + SHADOW ) );
    move( g.topLeft() );
    refreshAll();
    QWidget::showEvent( e );
    /* stay above the player like a dialog without blocking it; no Qt
     * parent, so the singleton keeps ownership */
    if( mi && windowHandle() && mi->windowHandle() )
        windowHandle()->setTransientParent( mi->windowHandle() );
}

void SettingsDialog::hideEvent( QHideEvent *e )
{
    if( dirty )
    {
        config_SaveConfigFile( p_intf );
        dirty = false;
    }
    QWidget::hideEvent( e );
}

void SettingsDialog::restyle()
{
    const Colors c = colors();
    QString css = QString(
        "#VLCSettings QLabel#STitle { color: %1; background: transparent; }"
        "#VLCSettings QLabel#SHeading { color: %2; background: transparent; }"
        "#VLCSettings QLabel#SLabel { color: %3; background: transparent; }"
        "#VLCSettings QLabel#SLabel:disabled { color: %4; }"
        "#VLCSettings QLabel#SNoResults { color: %5; background: %6; }"
        "#VLCSettings QLineEdit#SSearch { background: transparent; border: none;"
        "  padding-left: 39px; color: %3; selection-background-color: %7; }"
        "#VLCSettings QScrollArea#SScroll, #VLCSettings QWidget#SPage,"
        "#VLCSettings QStackedWidget#SStack { background: %6; }"
        "#VLCSettings QScrollArea#SScroll > QWidget > QWidget { background: %6; }"
        "#VLCSettings QLineEdit#SField { background: %8; border: 2px solid %9; border-radius: 6px;"
        "  padding: 0 11px; color: %3; selection-background-color: %7; }"
        "#VLCSettings QLineEdit#SField:focus { border-color: %10; }"
        "#VLCSettings QLineEdit#SField:disabled { color: %4; border-color: %8; }"
        "#VLCSettings QLineEdit#SColorEdit, #VLCSettings QLineEdit#SVolumeEdit {"
        "  background: transparent; border: none; color: %3; }"
        "#VLCSettings QLineEdit#SVolumeEdit { background: %6; border-radius: 4px; padding: 0 9px; }"
        "#VLCSettings QLineEdit#SEqBox { background: %8; border: none; border-radius: 4px; color: %3; }"
        "#VLCSettings QToolButton#SEqHeader { background: transparent; border: none; color: %3; }"
        "#VLCSettings QToolButton#SAdvanced { background: %11; border: none; border-radius: 4px;"
        "  color: %3; padding: 0 8px; }"
        "#VLCSettings QToolButton#SAdvanced:hover { color: %2; }"
        "#VLCSettings QListView#SAddons { background: %8; border: 2px solid %12; border-radius: 6px;"
        "  color: %3; padding: 4px; }"
        "#VLCSettings QListView#SAddons::item { padding: 6px 8px; border-radius: 4px; }"
        "#VLCSettings QListView#SAddons::item:selected { background: %7; color: %13; }"
        "#VLCSettings QScrollArea#SScroll QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 2px 4px 0; }"
        "#VLCSettings QScrollArea#SScroll QScrollBar::handle:vertical { background: %14; border-radius: 3px; min-height: 40px; }"
        "#VLCSettings QScrollArea#SScroll QScrollBar::add-line, #VLCSettings QScrollArea#SScroll QScrollBar::sub-line { height: 0; }"
        "#VLCSettings QScrollArea#SScroll QScrollBar::add-page, #VLCSettings QScrollArea#SScroll QScrollBar::sub-page { background: none; }" )
        .arg( c.title.name(), c.strong.name(), c.text.name(), c.info.name(), c.faint.name(),
              c.window.name(), Theme::accentDim().name(), c.well.name(), c.surface.name() )
        .arg( Theme::accentDim().name(), c.nav.name(), c.well.name(), Theme::accent().name(),
              c.knobOff.name() );
    setStyleSheet( css );
    for( SettingsPage *p : pages )
        p->setAdvancedOpen( p->isAdvancedOpen() );
    update();
}

void SettingsDialog::updateDeveloperTips()
{
    const bool dev = getSettings()->value( qtKey( "developerMode" ), false ).toBool();
    for( SettingsRow *r : rows )
        r->developerTip( dev );
}

void SettingsDialog::applySearch( const QString &text )
{
    const QStringList words = text.trimmed().toLower().split( ' ', Qt::SkipEmptyParts );
    int first = -1, total = 0;
    for( int i = 0; i < pages.count(); i++ )
    {
        const int n = pages[i]->filter( words );
        nav->setPageVisible( i, words.isEmpty() || n > 0 );
        if( n > 0 && first < 0 ) first = i;
        total += n;
    }
    const bool none = !words.isEmpty() && total == 0;
    noResults->setVisible( none );
    stack->setVisible( !none );
    if( none )
        return;
    /* stay on the current page while it still has hits */
    const int cur = stack->currentIndex();
    if( !words.isEmpty() && pages[cur]->filter( words ) == 0 && first >= 0 )
        showPage( first );
}

/*********************************************************************
 * Row builders
 *********************************************************************/
SettingsRow *SettingsDialog::row( SettingsPage *page, const QString &label, const QString &tip,
                                  QWidget *control, const char *option )
{
    QString sectionName;
    if( !page->sections.isEmpty() && page->sections.last().heading )
        sectionName = page->sections.last().heading->text();
    SettingsRow *r = new SettingsRow( label, tip, control, sectionName + " " + page->pageTitle, page );
    if( option )
    {
        r->setOptionName( QString( option ) );
        if( !hasOption( option ) )
            r->setAvailable( false, notInBuild() );
    }
    page->addRow( r );
    rows << r;
    return r;
}

SettingsRow *SettingsDialog::unavailable( SettingsPage *page, const QString &label,
                                          const QString &reason, QWidget *control )
{
    SettingsRow *r = row( page, label, QString(), control );
    r->setAvailable( false, reason );
    return r;
}

SToggle *SettingsDialog::boolOption( SettingsPage *page, const QString &label, const QString &tip,
                                     const char *option, bool invert )
{
    SToggle *t = new SToggle;
    row( page, label, tip, t, option );
    if( !hasOption( option ) )
        return t;
    onRefresh( option, [this, t, option, invert]() {
        QSignalBlocker b( t );
        t->setChecked( ( config_GetInt( p_intf, option ) != 0 ) != invert );
    } );
    connect( t, &SToggle::toggled, this, [this, option, invert]( bool on ) {
        config_PutInt( p_intf, option, on != invert );
        changed( option );
    } );
    return t;
}

SToggle *SettingsDialog::qtToggle( SettingsPage *page, const QString &label, const QString &tip,
                                   const QString &key, bool def, std::function<void( bool )> apply )
{
    SToggle *t = new SToggle;
    row( page, label, tip, t );
    t->setChecked( getSettings()->value( qtKey( key ), def ).toBool() );
    connect( t, &SToggle::toggled, this, [this, key, apply]( bool on ) {
        getSettings()->setValue( qtKey( key ), on );
        if( apply ) apply( on );
    } );
    return t;
}

SCombo *SettingsDialog::intChoice( SettingsPage *page, const QString &label, const QString &tip,
                                   const char *option, const QList<QPair<QString, int> > &items )
{
    SCombo *c = new SCombo;
    row( page, label, tip, c, option );
    if( !hasOption( option ) )
        return c;
    if( items.isEmpty() )
    {
        int64_t *values;
        char **texts;
        ssize_t n = config_GetIntChoices( VLC_OBJECT( p_intf ), option, &values, &texts );
        for( ssize_t i = 0; i < n; i++ )
        {
            c->addItem( qtr( texts[i] ), QVariant( (qlonglong)values[i] ) );
            free( texts[i] );
        }
        if( n > 0 ) { free( values ); free( texts ); }
    }
    else
        for( const auto &it : items )
            c->addItem( it.first, QVariant( (qlonglong)it.second ) );
    onRefresh( option, [this, c, option]() {
        QSignalBlocker b( c );
        const qlonglong v = config_GetInt( p_intf, option );
        int i = c->findData( QVariant( v ) );
        if( i < 0 )
        {
            /* a value the list does not name: show it rather than lie */
            c->addItem( QString::number( v ), QVariant( v ) );
            i = c->count() - 1;
        }
        c->setCurrentIndex( i );
    } );
    connect( c, QOverload<int>::of( &QComboBox::activated ), this, [this, c, option]( int i ) {
        config_PutInt( p_intf, option, c->itemData( i ).toLongLong() );
        changed( option );
    } );
    return c;
}

SCombo *SettingsDialog::stringChoice( SettingsPage *page, const QString &label, const QString &tip,
                                      const char *option, const QList<QPair<QString, QString> > &items )
{
    SCombo *c = new SCombo;
    row( page, label, tip, c, option );
    if( !hasOption( option ) )
        return c;
    if( items.isEmpty() )
    {
        char **values, **texts;
        ssize_t n = config_GetPszChoices( VLC_OBJECT( p_intf ), option, &values, &texts );
        for( ssize_t i = 0; i < n; i++ )
        {
            c->addItem( qtr( texts[i] ), QVariant( qfu( values[i] ? values[i] : "" ) ) );
            free( values[i] );
            free( texts[i] );
        }
        if( n > 0 ) { free( values ); free( texts ); }
    }
    else
        for( const auto &it : items )
            c->addItem( it.first, QVariant( it.second ) );
    onRefresh( option, [this, c, option]() {
        QSignalBlocker b( c );
        const QString v = configString( p_intf, option );
        int i = c->findData( QVariant( v ) );
        if( i < 0 && !v.isEmpty() )
        {
            c->addItem( v, QVariant( v ) );
            i = c->count() - 1;
        }
        c->setCurrentIndex( qMax( i, 0 ) );
    } );
    connect( c, QOverload<int>::of( &QComboBox::activated ), this, [this, c, option]( int i ) {
        config_PutPsz( p_intf, option, qtu( c->itemData( i ).toString() ) );
        changed( option );
    } );
    return c;
}

SField *SettingsDialog::textOption( SettingsPage *page, const QString &label, const QString &tip,
                                    const char *option, const QString &placeholder, bool password )
{
    SField *f = new SField;
    f->setPlaceholderText( placeholder );
    if( password )
        f->setEchoMode( QLineEdit::Password );
    row( page, label, tip, f, option );
    if( !hasOption( option ) )
        return f;
    onRefresh( option, [this, f, option]() {
        QSignalBlocker b( f );
        f->setText( configString( p_intf, option ) );
    } );
    connect( f, &QLineEdit::editingFinished, this, [this, f, option]() {
        if( f->text() == configString( p_intf, option ) )
            return;
        config_PutPsz( p_intf, option, qtu( f->text() ) );
        changed( option );
    } );
    return f;
}

/* "10 s", "5000 ms": the value with its unit, parsed back leniently */
SField *SettingsDialog::numberOption( SettingsPage *page, const QString &label, const QString &tip,
                                      const char *option, const QString &suffix, double scale )
{
    SField *f = new SField;
    row( page, label, tip, f, option );
    if( !hasOption( option ) )
        return f;
    module_config_t *cfg = config_FindConfig( option );
    const bool isFloat = cfg && cfg->i_type == CONFIG_ITEM_FLOAT;
    auto show = [this, f, option, suffix, scale, isFloat]() {
        QSignalBlocker b( f );
        double v = isFloat ? config_GetFloat( p_intf, option ) : (double)config_GetInt( p_intf, option );
        v /= scale;
        QString t = isFloat ? QString::number( v, 'g', 4 ) : QString::number( qRound64( v ) );
        f->setText( suffix.isEmpty() ? t : t + " " + suffix );
    };
    onRefresh( option, show );
    connect( f, &QLineEdit::editingFinished, this, [this, f, option, suffix, scale, isFloat, show]() {
        QString t = f->text();
        if( !suffix.isEmpty() ) t.remove( suffix );
        bool ok;
        double v = t.trimmed().toDouble( &ok );
        if( ok )
        {
            v *= scale;
            module_config_t *c = config_FindConfig( option );
            if( isFloat )
            {
                if( c && ( c->min.f || c->max.f ) ) v = qBound( (double)c->min.f, v, (double)c->max.f );
                config_PutFloat( p_intf, option, v );
            }
            else
            {
                int64_t iv = qRound64( v );
                if( c && ( c->min.i || c->max.i ) ) iv = qBound( c->min.i, iv, c->max.i );
                config_PutInt( p_intf, option, iv );
            }
            changed( option );
        }
        show();
    } );
    return f;
}

SField *SettingsDialog::pathOption( SettingsPage *page, const QString &label, const QString &tip,
                                    const char *option, bool directory, const QString &placeholder )
{
    QWidget *box = new QWidget;
    box->setFixedSize( CONTROL_W, CONTROL_H );
    QHBoxLayout *l = new QHBoxLayout( box );
    l->setContentsMargins( 0, 0, 0, 0 );
    l->setSpacing( 10 );
    SField *f = new SField( box );
    f->setPlaceholderText( placeholder );
    SButton *b = new SButton( qtr( "Browse" ), box );
    b->setFixedWidth( BROWSE_W );
    l->addWidget( f, 1 );
    l->addWidget( b );
    row( page, label, tip, box, option );
    if( !hasOption( option ) )
        return f;
    onRefresh( option, [this, f, option]() {
        QSignalBlocker bl( f );
        f->setText( configString( p_intf, option ) );
    } );
    auto store = [this, f, option]() {
        if( f->text() == configString( p_intf, option ) ) return;
        config_PutPsz( p_intf, option, qtu( f->text() ) );
        changed( option );
    };
    connect( f, &QLineEdit::editingFinished, this, store );
    connect( b, &SButton::clicked, this, [this, f, directory, store]() {
        QString start = f->text().isEmpty() ? QDir::homePath() : f->text();
        QString path = directory
            ? QFileDialog::getExistingDirectory( this, qtr( "Choose a folder" ), start )
            : QFileDialog::getSaveFileName( this, qtr( "Choose a file" ), start, QString(), NULL,
                                            QFileDialog::DontConfirmOverwrite );
        if( path.isEmpty() ) return;
        f->setText( QDir::toNativeSeparators( path ) );
        store();
    } );
    return f;
}

STickSlider *SettingsDialog::sliderOption( SettingsPage *page, const QString &label, const QString &tip,
                                           const char *option, double min, double max, double step,
                                           int ticks, const QVector<QPair<double, QString> > &labels,
                                           double scale )
{
    STickSlider *s = new STickSlider( min, max );
    s->setStep( step );
    s->setTicks( ticks );
    s->setLabels( labels );
    row( page, label, tip, s, option );
    if( !hasOption( option ) )
        return s;
    module_config_t *cfg = config_FindConfig( option );
    const bool isFloat = cfg && cfg->i_type == CONFIG_ITEM_FLOAT;
    onRefresh( option, [this, s, option, isFloat, scale]() {
        QSignalBlocker b( s );
        s->setValue( ( isFloat ? config_GetFloat( p_intf, option )
                               : (double)config_GetInt( p_intf, option ) ) / scale );
    } );
    connect( s, &STickSlider::released, this, [this, option, isFloat, scale]( double v ) {
        if( isFloat ) config_PutFloat( p_intf, option, v * scale );
        else config_PutInt( p_intf, option, qRound64( v * scale ) );
        changed( option );
    } );
    return s;
}

SColorField *SettingsDialog::colorOption( SettingsPage *page, const QString &label, const QString &tip,
                                          const char *option )
{
    SColorField *f = new SColorField;
    row( page, label, tip, f, option );
    if( !hasOption( option ) )
        return f;
    onRefresh( option, [this, f, option]() {
        QSignalBlocker b( f );
        f->setColor( QColor( (QRgb)config_GetInt( p_intf, option ) ) );
    } );
    connect( f, &SColorField::changed, this, [this, option]( const QColor &c ) {
        config_PutInt( p_intf, option, c.rgb() & 0xFFFFFF );
        changed( option );
    } );
    return f;
}

bool SettingsDialog::hasFilterModule( const char *chain, const char *module )
{
    return configString( p_intf, chain ).split( ':', Qt::SkipEmptyParts )
               .contains( QString( module ) );
}

void SettingsDialog::setFilterModule( const char *chain, const char *module, bool on )
{
    QStringList l = configString( p_intf, chain ).split( ':', Qt::SkipEmptyParts );
    l.removeAll( QString( module ) );
    if( on )
        l << QString( module );
    config_PutPsz( p_intf, chain, qtu( l.join( ':' ) ) );
    /* audio filters also change on the running output */
    if( !strcmp( chain, "audio-filter" ) )
        playlist_EnableAudioFilter( THEPL, module, on );
    changed( chain );
}

SToggle *SettingsDialog::filterToggle( SettingsPage *page, const QString &label, const QString &tip,
                                       const char *chain, const char *module )
{
    SToggle *t = new SToggle;
    SettingsRow *r = row( page, label, tip, t, chain );
    if( !module_exists( module ) )
    {
        r->setAvailable( false, notInBuild() );
        return t;
    }
    onRefresh( chain, [this, t, chain, module]() {
        QSignalBlocker b( t );
        t->setChecked( hasFilterModule( chain, module ) );
    } );
    connect( t, &SToggle::toggled, this, [this, chain, module]( bool on ) {
        setFilterModule( chain, module, on );
    } );
    return t;
}

/*********************************************************************
 * App › General
 *********************************************************************/
void SettingsDialog::buildGeneral()
{
    SettingsPage *p = pages[GENERAL];
    p->section( qtr( "Appearance" ) );

    /* VLC 3 only has a language option on Windows and OS/2 */
    SCombo *lang = new SCombo;
    if( hasOption( "language" ) )
    {
        delete lang;
        stringChoice( p, qtr( "Interface language" ), QString(), "language" );
    }
    else
    {
        lang->addItem( qtr( "System language" ) );
        unavailable( p, qtr( "Interface language" ),
                     qtr( "On this system VLC uses the language of your desktop session (LANG)." ),
                     lang );
    }

    SSegmented *theme = new SSegmented( QStringList() << qtr( "Dark" ) << qtr( "Light" ) << qtr( "System" ) );
    row( p, qtr( "Theme" ), QString(), theme, "qt-theme" );
    onRefresh( "qt-theme", [this, theme]() {
        const QString v = configString( p_intf, "qt-theme" );
        theme->setCurrent( v == "light" ? 1 : v == "system" ? 2 : 0 );
    } );
    connect( theme, &SSegmented::changed, this, [this]( int i ) {
        static const char *const v[] = { "dark", "light", "system" };
        config_PutPsz( p_intf, "qt-theme", v[i] );
        changed( "qt-theme" );
        Theme::reload();
        restyle();
    } );

    QStringList accents, names;
    for( int i = 0; i < Theme::ACCENT_COUNT; i++ )
    {
        accents << Theme::ACCENT_VALUES[i];
        names << qtr( Theme::ACCENT_NAMES[i] );
    }
    SSwatches *sw = new SSwatches( accents, names );
    row( p, qtr( "Accent color" ), QString(), sw, "qt-accent-color" );
    onRefresh( "qt-accent-color", [this, sw, accents]() {
        sw->setCurrent( accents.indexOf( configString( p_intf, "qt-accent-color" ).toUpper() ) );
    } );
    connect( sw, &SSwatches::changed, this, [this, accents]( int i ) {
        config_PutPsz( p_intf, "qt-accent-color", qtu( accents[i] ) );
        changed( "qt-accent-color" );
        Theme::reload();
        restyle();
    } );

    p->section( qtr( "Startup" ) );
    boolOption( p, qtr( "Show confirmation and error dialogs" ),
                qtr( "Show the dialogs that report unimportant errors and warnings." ),
                "qt-error-dialogs" );
    boolOption( p, qtr( "Check for updates automatically" ),
                qtr( "Check once in a while whether a newer VLC is available." ),
                "qt-updates-notif" );

    p->section( qtr( "Diagnostics" ) );
    qtToggle( p, qtr( "Developer mode" ),
              qtr( "Open Advanced Options on every page and show each setting's VLC option name in its tooltip." ),
              "developerMode", false, [this]( bool on ) {
                  for( SettingsPage *pg : pages )
                  {
                      pg->keepAdvancedOpen = on;
                      pg->setAdvancedOpen( on );
                  }
                  updateDeveloperTips();
              } );
    SToggle *saveLog = boolOption( p, qtr( "Save a log" ),
                qtr( "Write VLC's messages to a file. Applies the next time VLC starts." ),
                "file-logging" );
    Q_UNUSED( saveLog );
    SField *logFile = pathOption( p, qtr( "Log file" ),
                qtr( "Where the log is written. Empty: vlc-log.txt in your home folder." ),
                "logfile", false );
    SCombo *details = intChoice( p, qtr( "Log details" ), qtr( "How much the log records." ),
                "log-verbose", QList<QPair<QString, int> >()
                    << qMakePair( qtr( "Same as the terminal" ), -1 )
                    << qMakePair( qtr( "Errors only" ), 1 )
                    << qMakePair( qtr( "Errors and warnings" ), 2 )
                    << qMakePair( qtr( "Everything (debug)" ), 3 ) );
    QWidget *logFileRow = logFile->parentWidget()->parentWidget();
    QWidget *detailsRow = details->parentWidget();
    onRefresh( "file-logging", [this, logFileRow, detailsRow]() {
        const bool on = hasOption( "file-logging" ) && config_GetInt( p_intf, "file-logging" );
        static_cast<SettingsRow *>( logFileRow )->setActive( on );
        static_cast<SettingsRow *>( detailsRow )->setActive( on );
    } );

    p->beginAdvanced();
    p->section( QString() );
    intChoice( p, qtr( "Show minor warnings" ),
               qtr( "How much VLC reports in the terminal and in Tools › Messages." ),
               "verbose", QList<QPair<QString, int> >()
                   << qMakePair( qtr( "Errors only" ), 0 )
                   << qMakePair( qtr( "Errors and warnings" ), 1 )
                   << qMakePair( qtr( "Everything (debug)" ), 2 ) );
    SField *extra = new SField;
    unavailable( p, qtr( "Additional command line options" ),
                 qtr( "VLC 3 cannot add command line options from its settings. "
                      "Add them to the command that starts VLC instead." ), extra );
    stringChoice( p, qtr( "Log format" ), qtr( "Plain text or HTML." ), "logmode",
                  QList<QPair<QString, QString> >()
                      << qMakePair( qtr( "Plain text" ), QString( "text" ) )
                      << qMakePair( qtr( "HTML" ), QString( "html" ) ) );
    boolOption( p, qtr( "Collect playback statistics" ),
                qtr( "Count decoded, displayed and lost frames for Tools › Media Information." ),
                "stats" );
    SButton *all = new SButton( qtr( "Open All VLC Settings" ) );
    all->setFixedWidth( CONTROL_W );
    row( p, qtr( "Every VLC option" ),
         qtr( "The complete tree of VLC's options, including the modules'." ), all );
    connect( all, &SButton::clicked, this, [this]() {
        PrefsDialog *d = new PrefsDialog( (QWidget *)p_intf->p_sys->p_mi, p_intf );
        d->showAll();
        d->toggleVisible();
    } );
}

/*********************************************************************
 * App › Interface
 *********************************************************************/
void SettingsDialog::buildInterface()
{
    SettingsPage *p = pages[INTERFACE];
    p->section( qtr( "Skin" ) );
    SToggle *skin = new SToggle;
    unavailable( p, qtr( "Use a custom skin" ),
                 module_exists( "skins2" ) ? qtr( "Skins replace this interface; choose them in All VLC Settings › Interface." )
                                           : notInBuild(), skin );
    QWidget *skinFile = new QWidget;
    skinFile->setFixedSize( CONTROL_W, CONTROL_H );
    {
        QHBoxLayout *l = new QHBoxLayout( skinFile );
        l->setContentsMargins( 0, 0, 0, 0 );
        l->setSpacing( 10 );
        l->addWidget( new SField( skinFile ), 1 );
        SButton *b = new SButton( qtr( "Browse" ), skinFile );
        b->setFixedWidth( BROWSE_W );
        l->addWidget( b );
    }
    unavailable( p, qtr( "Skin file" ), QString(), skinFile );

    p->section( qtr( "Window" ) );
    boolOption( p, qtr( "Play video inside main window" ),
                qtr( "Off: video opens in a window of its own." ), "embedded-video" );
    intChoice( p, qtr( "Resize window to fit video" ),
               qtr( "Resize the player to the video's size when a video starts." ),
               "qt-video-autoresize", QList<QPair<QString, int> >()
                   << qMakePair( qtr( "Never" ), 0 ) << qMakePair( qtr( "Always" ), 1 ) );
    qtToggle( p, qtr( "Remember window size and position" ), QString(), "rememberGeometry", true );
    boolOption( p, qtr( "Keep window above other windows" ),
                qtr( "Keep the video above other windows." ), "video-on-top" );
    boolOption( p, qtr( "Show window decorations" ),
                qtr( "Draw the system title bar on separate video windows." ), "video-deco" );
    boolOption( p, qtr( "Show the file name in the title bar" ),
                qtr( "Off: the title bar shows the media's title." ), "qt-name-in-title" );
    {
        QList<QPair<QString, int> > screens;
        screens << qMakePair( qtr( "Current screen" ), -1 );
        for( int i = 0; i < QGuiApplication::screens().count(); i++ )
            screens << qMakePair( qtr( "Screen %1" ).arg( i + 1 ), i );
        intChoice( p, qtr( "Open fullscreen on" ),
                   qtr( "The screen fullscreen video uses." ), "qt-fullscreen-screennumber", screens );
    }

    p->section( qtr( "Playback Controls" ) );
    boolOption( p, qtr( "Start in compact mode" ),
                qtr( "Start with the interface hidden (Ctrl+H to show it)." ), "qt-minimal-view" );
    SCombo *autohide = intChoice( p, qtr( "Auto-hide the controls" ),
                qtr( "When the controls slide away while you watch." ), "qt-autohide-controls" );
    Q_UNUSED( autohide );
    STickSlider *hide = sliderOption( p, qtr( "Hide after" ), QString(), "mouse-hide-timeout",
                0, 10, 0.5, 20, QVector<QPair<double, QString> >()
                    << qMakePair( 0.0, QString( "0 s" ) ) << qMakePair( 5.0, QString( "5 s" ) )
                    << qMakePair( 10.0, QString( "10s" ) ), 1000 );
    QWidget *hideRow = hide->parentWidget();
    onRefresh( "qt-autohide-controls", [this, hideRow]() {
        static_cast<SettingsRow *>( hideRow )->setActive(
            config_GetInt( p_intf, "qt-autohide-controls" ) != 0 );
    } );
    boolOption( p, qtr( "Show controls in fullscreen" ),
                qtr( "Show the control bar when the mouse moves in fullscreen." ), "qt-fs-controller" );

    p->section( qtr( "Notifications and System Tray" ) );
    boolOption( p, qtr( "Show VLC in the system tray" ), QString(), "qt-system-tray" );
    intChoice( p, qtr( "Notify when the track changes" ),
               qtr( "A desktop notification with the title and artist." ), "qt-notification" );
    boolOption( p, qtr( "Start minimized to the tray" ), QString(), "qt-start-minimized" );
    boolOption( p, qtr( "Pause when minimized" ),
                qtr( "Pause video when the window is minimized." ), "qt-pause-minimized" );

    p->beginAdvanced();
    p->section( QString() );
    boolOption( p, qtr( "Draw VLC's own title bar" ),
                qtr( "Off: use the system's window frame. Applies the next time VLC starts." ),
                "qt-redesign-frameless" );
    boolOption( p, qtr( "Show the album art when nothing plays" ), QString(), "qt-bgcone" );
    boolOption( p, qtr( "Preview frames on the seek bar" ),
                qtr( "Show a thumbnail of the video while hovering the seek bar." ), "qt-seek-preview" );
    intChoice( p, qtr( "Raise the window when" ),
               qtr( "Bring the player to the front when playback starts from elsewhere." ),
               "qt-auto-raise" );
}

/*********************************************************************
 * App › Privacy
 *********************************************************************/
void SettingsDialog::buildPrivacy()
{
    SettingsPage *p = pages[PRIVACY];
    p->section( qtr( "History" ) );
    boolOption( p, qtr( "Save recently played items" ),
                qtr( "Keep the Recents list and the Continue Watching shelf." ), "qt-recentplay" );
    SToggle *pos = new SToggle;
    row( p, qtr( "Remember the playback position" ),
         qtr( "Offer to continue where you stopped. Playback › Continue where you left off has more choices." ),
         pos, "qt-continue" );
    onRefresh( "qt-continue", [this, pos]() {
        QSignalBlocker b( pos );
        pos->setChecked( config_GetInt( p_intf, "qt-continue" ) != 0 );
    } );
    connect( pos, &SToggle::toggled, this, [this]( bool on ) {
        config_PutInt( p_intf, "qt-continue", on ? 1 : 0 );
        changed( "qt-continue" );
    } );
    textOption( p, qtr( "Never remember items matching" ),
                qtr( "A regular expression; matching files are kept out of the history." ),
                "qt-recentplay-filter" );
    SButton *clear = new SButton( qtr( "Clear" ) );
    clear->setFixedWidth( BROWSE_W );
    QWidget *clearBox = new QWidget;
    clearBox->setFixedSize( CONTROL_W, BUTTON_H );
    {
        QHBoxLayout *l = new QHBoxLayout( clearBox );
        l->setContentsMargins( 0, 0, 0, 0 );
        l->addStretch( 1 );
        clear->setParent( clearBox );
        l->addWidget( clear );
    }
    row( p, qtr( "Clear history now" ), qtr( "Forget recently played items and their positions." ), clearBox );
    connect( clear, &SButton::clicked, this, [this, clear]() {
        RecentsMRL::getInstance( p_intf )->clear();
        clear->setText( qtr( "Cleared" ) );
        clear->setEnabled( false );
    } );

    p->section( qtr( "Network" ) );
    boolOption( p, qtr( "Fetch titles and cover art from the internet" ),
                qtr( "Look up missing titles and album art online." ), "metadata-network-access" );
    const bool scrobbler = module_exists( "audioscrobbler" );
    SToggle *lastfm = new SToggle;
    SettingsRow *lastfmRow = row( p, qtr( "Submit played tracks to last.fm" ),
                                  qtr( "Scrobble the songs you play to your last.fm profile." ),
                                  lastfm, "control" );
    if( !scrobbler )
        lastfmRow->setAvailable( false, notInBuild() );
    else
    {
        onRefresh( "control", [this, lastfm]() {
            QSignalBlocker b( lastfm );
            lastfm->setChecked( hasFilterModule( "control", "audioscrobbler" ) );
        } );
        connect( lastfm, &SToggle::toggled, this, [this]( bool on ) {
            setFilterModule( "control", "audioscrobbler", on );
        } );
    }
    SField *user = textOption( p, qtr( "Last.fm username" ), QString(), "lastfm-username" );
    /* the design repeats "Last.fm username" here; it is the password */
    SField *pass = textOption( p, qtr( "Last.fm password" ), QString(), "lastfm-password",
                               QString(), true );
    SField *url = textOption( p, qtr( "Last.fm submission URL" ),
                              qtr( "Only for last.fm compatible services." ), "scrobbler-url" );
    QList<QWidget *> lf = { user->parentWidget(), pass->parentWidget(), url->parentWidget() };
    onRefresh( "control", [this, lf]() {
        const bool on = hasFilterModule( "control", "audioscrobbler" );
        for( QWidget *w : lf )
            static_cast<SettingsRow *>( w )->setActive( on );
    } );
}

/*********************************************************************
 * App › Controls
 *********************************************************************/
void SettingsDialog::buildControls()
{
    SettingsPage *p = pages[CONTROLS];
    p->section( qtr( "Keyboard Shortcuts" ) );
    SButton *edit = new SButton( qtr( "Edit Keyboard Shortcuts" ) );
    edit->setFixedWidth( CONTROL_W );
    row( p, qtr( "Defaults" ), QString(), edit );
    connect( edit, &SButton::clicked, this, [this]() { THEDP->hotkeysDialog(); } );
    SButton *reset = new SButton( qtr( "Reset" ) );
    reset->setFixedWidth( CONTROL_W );
    row( p, qtr( "Reset all shortcuts" ), QString(), reset );
    connect( reset, &SButton::clicked, this, [this]() {
        if( QMessageBox::question( this, qtr( "Reset all shortcuts" ),
                qtr( "Put every keyboard shortcut back to VLC's default?" ) ) != QMessageBox::Yes )
            return;
        unsigned n;
        module_config_t *cfg = module_config_get( module_get_main(), &n );
        for( unsigned i = 0; i < n; i++ )
            if( cfg[i].i_type == CONFIG_ITEM_KEY && cfg[i].psz_name )
                config_PutPsz( p_intf, cfg[i].psz_name, cfg[i].orig.psz );
        module_config_free( cfg );
        dirty = true;
    } );

    p->section( qtr( "Skip Amounts" ) );
    numberOption( p, qtr( "Very short skip" ), QString(), "extrashort-jump-size", "s" );
    numberOption( p, qtr( "Short skip" ), QString(), "short-jump-size", "s" );
    numberOption( p, qtr( "Medium skip" ), QString(), "medium-jump-size", "s" );
    numberOption( p, qtr( "Long skip" ), QString(), "long-jump-size", "s" );

    p->section( qtr( "Mouse" ) );
    SCombo *single = new SCombo;
    single->addItem( qtr( "Nothing" ) );
    unavailable( p, qtr( "Single click on the video" ),
                 qtr( "VLC 3 does not report single clicks on the video to the interface." ), single );
    SCombo *dbl = new SCombo;
    dbl->addItem( qtr( "Toggle fullscreen" ) );
    unavailable( p, qtr( "Double click on the video" ),
                 qtr( "In VLC 3 a double click always toggles fullscreen." ), dbl );
    QList<QPair<QString, int> > wheel = QList<QPair<QString, int> >()
        << qMakePair( qtr( "Volume" ), 0 ) << qMakePair( qtr( "Seek" ), 2 )
        << qMakePair( qtr( "Nothing" ), -1 );
    SCombo *vw = new SCombo;
    for( const auto &w : wheel )
        vw->addItem( w.first, QVariant( (qlonglong)w.second ) );
    row( p, qtr( "Scroll wheel" ), QString(), vw, "hotkeys-y-wheel-mode" );
    intChoice( p, qtr( "Horizontal scroll" ), QString(), "hotkeys-x-wheel-mode", wheel );
    /* "Seek" and "Seek reversed" are two values of the same option */
    onRefresh( "hotkeys-y-wheel-mode", [this, vw]() {
        QSignalBlocker b( vw );
        const int v = config_GetInt( p_intf, "hotkeys-y-wheel-mode" );
        vw->setCurrentIndex( vw->findData( QVariant( (qlonglong)( v == 3 ? 2 : v ) ) ) );
    } );
    SToggle *rev = new SToggle;
    SettingsRow *revRow = row( p, qtr( "Reverse scroll direction" ),
                               qtr( "Scroll up to seek backwards. Only when the wheel seeks." ),
                               rev, "hotkeys-y-wheel-mode" );
    onRefresh( "hotkeys-y-wheel-mode", [this, rev, revRow]() {
        QSignalBlocker b( rev );
        const int v = config_GetInt( p_intf, "hotkeys-y-wheel-mode" );
        rev->setChecked( v == 3 );
        revRow->setActive( v == 2 || v == 3 );
    } );
    connect( rev, &SToggle::toggled, this, [this]( bool on ) {
        config_PutInt( p_intf, "hotkeys-y-wheel-mode", on ? 3 : 2 );
        changed( "hotkeys-y-wheel-mode" );
    } );
    connect( vw, QOverload<int>::of( &QComboBox::activated ), this, [this, vw, rev]( int i ) {
        int v = vw->itemData( i ).toInt();
        /* keep "reversed" when switching to Seek again */
        if( v == 2 && rev->isChecked() )
            v = 3;
        config_PutInt( p_intf, "hotkeys-y-wheel-mode", v );
        changed( "hotkeys-y-wheel-mode" );
    } );

    p->section( qtr( "Remotes" ) );
    SToggle *web = new SToggle;
    SettingsRow *webRow = row( p, qtr( "Web remote control" ),
        qtr( "Control VLC from a browser at http://<this computer>:<port>. Applies the next time VLC starts." ),
        web, "extraintf" );
    if( !module_exists( "lua" ) )
        webRow->setAvailable( false, notInBuild() );
    else
    {
        onRefresh( "extraintf", [this, web]() {
            QSignalBlocker b( web );
            web->setChecked( hasFilterModule( "extraintf", "http" ) );
        } );
        connect( web, &SToggle::toggled, this, [this]( bool on ) {
            setFilterModule( "extraintf", "http", on );
        } );
    }
    SField *port = numberOption( p, qtr( "Port" ), QString(), "http-port" );
    SField *pw = textOption( p, qtr( "Password" ),
                             qtr( "The browser asks for it; the user name stays empty." ),
                             "http-password", QString(), true );
    QList<QWidget *> wr = { port->parentWidget(), pw->parentWidget() };
    onRefresh( "extraintf", [this, wr]() {
        const bool on = hasFilterModule( "extraintf", "http" );
        for( QWidget *w : wr )
            static_cast<SettingsRow *>( w )->setActive( on );
    } );
}

/*********************************************************************
 * Media › Playback
 *********************************************************************/
static QVector<QPair<double, QString> > speedLabels()
{
    QVector<QPair<double, QString> > l;
    const char *const t[] = { "0.25", "0.5", "0.75", "1", "1.25", "1.5", "1.75", "2" };
    for( int i = 0; i < 8; i++ )
        l << qMakePair( 0.25 * ( i + 1 ), QString( t[i] ) );
    return l;
}

static QList<QPair<QString, QString> > languages( const QString &none )
{
    QList<QPair<QString, QString> > l;
    l << qMakePair( none, QString() );
    const char *const codes[][2] = {
        { N_("English"), "en" }, { N_("Arabic"), "ar" }, { N_("Chinese"), "zh" },
        { N_("Dutch"), "nl" }, { N_("French"), "fr" }, { N_("German"), "de" },
        { N_("Hindi"), "hi" }, { N_("Italian"), "it" }, { N_("Japanese"), "ja" },
        { N_("Korean"), "ko" }, { N_("Polish"), "pl" }, { N_("Portuguese"), "pt" },
        { N_("Russian"), "ru" }, { N_("Spanish"), "es" }, { N_("Swedish"), "sv" },
        { N_("Tamil"), "ta" }, { N_("Turkish"), "tr" }, { N_("Ukrainian"), "uk" },
    };
    for( const auto &c : codes )
        l << qMakePair( qtr( c[0] ), QString( c[1] ) );
    return l;
}

void SettingsDialog::buildPlayback()
{
    SettingsPage *p = pages[PLAYBACK];
    p->section( qtr( "Startup" ) );
    intChoice( p, qtr( "Continue where you left off" ),
               qtr( "What happens when you open something you stopped part way." ), "qt-continue",
               QList<QPair<QString, int> >()
                   << qMakePair( qtr( "Never" ), 0 ) << qMakePair( qtr( "Ask" ), 1 )
                   << qMakePair( qtr( "Always" ), 2 ) );
    boolOption( p, qtr( "Start playing as soon as a file opens" ),
                qtr( "Off: opened items wait in the queue." ), "playlist-autostart" );
    sliderOption( p, qtr( "Default playback speed" ), QString(), "rate", 0.25, 2, 0.05, 7,
                  speedLabels() );
    SToggle *duck = new SToggle;
    unavailable( p, qtr( "Pause when another app uses audio" ),
                 qtr( "VLC 3 is not told when another application starts playing sound." ), duck );

    p->section( qtr( "Tracks" ) );
    intChoice( p, qtr( "Preferred video quality" ),
               qtr( "For streams that offer several resolutions." ), "preferred-resolution" );
    stringChoice( p, qtr( "Preferred menu language" ), qtr( "For DVD and Blu-ray menus." ),
                  "menu-language", languages( qtr( "Disc default" ) ) );
    numberOption( p, qtr( "Default audio track" ),
                  qtr( "The track number to start with; -1 lets VLC choose." ), "audio-track" );
    numberOption( p, qtr( "Default subtitle track" ),
                  qtr( "The track number to start with; -1 lets VLC choose." ), "sub-track" );

    p->section( qtr( "Order" ) );
    SCombo *repeat = new SCombo;
    repeat->addItem( qtr( "Off" ) );
    repeat->addItem( qtr( "All" ) );
    repeat->addItem( qtr( "One" ) );
    row( p, qtr( "Repeat" ), qtr( "Repeat the queue or the current item." ), repeat, "loop" );
    auto syncRepeat = [this, repeat]() {
        QSignalBlocker b( repeat );
        repeat->setCurrentIndex( config_GetInt( p_intf, "repeat" ) ? 2
                               : config_GetInt( p_intf, "loop" ) ? 1 : 0 );
    };
    onRefresh( "loop", syncRepeat );
    onRefresh( "repeat", syncRepeat );
    connect( repeat, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        config_PutInt( p_intf, "loop", i == 1 );
        config_PutInt( p_intf, "repeat", i == 2 );
        changed( "loop" );
    } );
    boolOption( p, qtr( "Shuffle" ), qtr( "Play the queue in random order." ), "random" );
    boolOption( p, qtr( "Stop after each item" ), qtr( "Play one item, then stop." ), "play-and-stop" );
    SCombo *finish = new SCombo;
    finish->addItem( qtr( "Stop" ) );
    finish->addItem( qtr( "Quit VLC" ) );
    row( p, qtr( "When playback finishes" ), qtr( "What VLC does after the last item." ),
         finish, "play-and-exit" );
    onRefresh( "play-and-exit", [this, finish]() {
        QSignalBlocker b( finish );
        finish->setCurrentIndex( config_GetInt( p_intf, "play-and-exit" ) ? 1 : 0 );
    } );
    connect( finish, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        config_PutInt( p_intf, "play-and-exit", i == 1 );
        changed( "play-and-exit" );
    } );

    p->section( qtr( "Seeking" ) );
    boolOption( p, qtr( "Fast seek" ),
                qtr( "Jump to the nearest keyframe: quicker, less precise." ), "input-fast-seek" );

    p->beginAdvanced();
    p->section( QString() );
    boolOption( p, qtr( "Pause on the last frame" ),
                qtr( "Keep the last frame of a video on screen when it ends." ), "play-and-pause" );
    numberOption( p, qtr( "Seek step for Left / Right" ),
                  qtr( "The \"Short skip\" from Controls, in seconds." ), "short-jump-size", "s" );
    boolOption( p, qtr( "Start paused" ), qtr( "Open media paused on its first frame." ), "start-paused" );
}

/*********************************************************************
 * Media › Video
 *********************************************************************/
void SettingsDialog::buildVideo()
{
    SettingsPage *p = pages[VIDEO];
    p->section( qtr( "Output" ) );
    boolOption( p, qtr( "Enable video" ), qtr( "Off: play only the sound." ), "video" );
    boolOption( p, qtr( "Start videos in fullscreen" ), QString(), "fullscreen" );
    boolOption( p, qtr( "Stop the screensaver during playback" ), QString(), "disable-screensaver" );
    SToggle *wall = new SToggle;
    unavailable( p, qtr( "Play video as desktop wallpaper" ),
                 qtr( "VLC 3 can only do this with DirectX on Windows." ), wall );

    /* the picture defaults are the "adjust" filter's options; the filter
     * joins the chain while any of them is off its neutral value */
    p->section( qtr( "Picture" ) );
    struct Pic { const char *label, *opt; double lo, hi, step; int ticks; };
    static const Pic pics[] = {
        { N_("Hue"), "hue", -180, 180, 1, 0 },
        { N_("Brightness"), "brightness", 0, 2, 0.01, 0 },
        { N_("Contrast"), "contrast", 0, 2, 0.01, 0 },
        { N_("Saturation"), "saturation", 0, 3, 0.01, 3 },
        { N_("Gamma"), "gamma", 0.01, 10, 0.01, 10 },
    };
    const bool adjust = module_exists( "adjust" );
    for( const Pic &pc : pics )
    {
        STickSlider *s = sliderOption( p, qtr( pc.label ), QString(), pc.opt, pc.lo, pc.hi,
                                       pc.step, pc.ticks, QVector<QPair<double, QString> >() );
        if( !adjust )
            static_cast<SettingsRow *>( s->parentWidget() )->setAvailable( false, notInBuild() );
        const char *opt = pc.opt;
        connect( s, &STickSlider::released, this, [this]( double ) {
            const bool neutral =
                config_GetFloat( p_intf, "hue" ) == 0.f && config_GetFloat( p_intf, "brightness" ) == 1.f
             && config_GetFloat( p_intf, "contrast" ) == 1.f && config_GetFloat( p_intf, "saturation" ) == 1.f
             && config_GetFloat( p_intf, "gamma" ) == 1.f;
            QStringList l = configString( p_intf, "video-filter" ).split( ':', Qt::SkipEmptyParts );
            l.removeAll( "adjust" );
            if( !neutral ) l << "adjust";
            config_PutPsz( p_intf, "video-filter", qtu( l.join( ':' ) ) );
            changed( "video-filter" );
        } );
        Q_UNUSED( opt );
    }
    SButton *reset = new SButton( qtr( "Reset" ) );
    reset->setFixedWidth( BROWSE_W );
    QWidget *resetBox = new QWidget;
    resetBox->setFixedSize( CONTROL_W, BUTTON_H );
    {
        QHBoxLayout *l = new QHBoxLayout( resetBox );
        l->setContentsMargins( 0, 0, 0, 0 );
        l->addStretch( 1 );
        reset->setParent( resetBox );
        l->addWidget( reset );
    }
    SettingsRow *rr = row( p, qtr( "Reset picture" ), QString(), resetBox );
    if( !adjust ) rr->setAvailable( false, notInBuild() );
    connect( reset, &SButton::clicked, this, [this]() {
        static const char *const o[] = { "hue", "brightness", "contrast", "saturation", "gamma" };
        static const float v[] = { 0.f, 1.f, 1.f, 1.f, 1.f };
        for( int i = 0; i < 5; i++ )
        {
            config_PutFloat( p_intf, o[i], v[i] );
            changed( o[i] );
        }
        QStringList l = configString( p_intf, "video-filter" ).split( ':', Qt::SkipEmptyParts );
        l.removeAll( "adjust" );
        config_PutPsz( p_intf, "video-filter", qtu( l.join( ':' ) ) );
        changed( "video-filter" );
    } );

    p->section( qtr( "Deinterlacing" ) );
    intChoice( p, qtr( "Deinterlacing" ),
               qtr( "Remove the comb lines of interlaced video (TV, DVDs)." ), "deinterlace",
               QList<QPair<QString, int> >()
                   << qMakePair( qtr( "Automatic" ), -1 ) << qMakePair( qtr( "On" ), 1 )
                   << qMakePair( qtr( "Off" ), 0 ) );
    SCombo *method = stringChoice( p, qtr( "Method" ), qtr( "How the lines are merged." ), "deinterlace-mode" );
    if( method->count() && method->itemText( 0 ).toLower() == "auto" )
        method->setItemText( 0, qtr( "Automatic" ) );

    p->section( qtr( "Framing" ) );
    stringChoice( p, qtr( "Aspect ratio" ), qtr( "Force the shape of the picture." ), "aspect-ratio",
                  QList<QPair<QString, QString> >()
                      << qMakePair( qtr( "Automatic" ), QString() )
                      << qMakePair( QString( "1:1" ), QString( "1:1" ) )
                      << qMakePair( QString( "4:3" ), QString( "4:3" ) )
                      << qMakePair( QString( "5:4" ), QString( "5:4" ) )
                      << qMakePair( QString( "16:9" ), QString( "16:9" ) )
                      << qMakePair( QString( "16:10" ), QString( "16:10" ) )
                      << qMakePair( QString( "2.21:1" ), QString( "221:100" ) )
                      << qMakePair( QString( "2.35:1" ), QString( "235:100" ) )
                      << qMakePair( QString( "2.39:1" ), QString( "239:100" ) ) );
    stringChoice( p, qtr( "Crop" ), qtr( "Cut the picture to this ratio." ), "crop",
                  QList<QPair<QString, QString> >()
                      << qMakePair( qtr( "None" ), QString() )
                      << qMakePair( QString( "4:3" ), QString( "4:3" ) )
                      << qMakePair( QString( "16:9" ), QString( "16:9" ) )
                      << qMakePair( QString( "16:10" ), QString( "16:10" ) )
                      << qMakePair( QString( "2.35:1" ), QString( "235:100" ) ) );
    boolOption( p, qtr( "Fit the video to the window" ),
                qtr( "Off: show the video at its own size." ), "autoscale" );

    p->beginAdvanced();
    p->section( qtr( "Snapshots" ) );
    pathOption( p, qtr( "Save snapshots to" ), QString(), "snapshot-path", true,
                QStandardPaths::writableLocation( QStandardPaths::PicturesLocation ) );
    textOption( p, qtr( "File name prefix" ), QString(), "snapshot-prefix" );
    stringChoice( p, qtr( "Format" ), QString(), "snapshot-format" );
    boolOption( p, qtr( "Number the files in sequence" ),
                qtr( "Off: the file name carries the time." ), "snapshot-sequential" );
    p->section( qtr( "Output module" ) );
    stringChoice( p, qtr( "Video output" ), qtr( "Leave on Automatic unless video does not show." ), "vout" );
}

/*********************************************************************
 * Media › Audio
 *********************************************************************/
void SettingsDialog::buildAudio()
{
    SettingsPage *p = pages[AUDIO];
    p->section( qtr( "Output" ) );
    boolOption( p, qtr( "Enable audio" ), qtr( "Off: play only the picture." ), "audio" );
    SCombo *out = stringChoice( p, qtr( "Output device" ),
                                qtr( "How VLC reaches your speakers. Applies to the next item." ), "aout" );
    if( out->count() && ( out->itemData( 0 ).toString() == "any" || out->itemData( 0 ).toString().isEmpty() ) )
        out->setItemText( 0, qtr( "System default" ) );
    SCombo *spk = intChoice( p, qtr( "Speaker setup" ),
                             qtr( "How stereo sound is sent to the speakers." ), "stereo-mode" );
    if( spk->count() )
    {
        int i = spk->findData( QVariant( (qlonglong)0 ) );
        if( i >= 0 ) spk->setItemText( i, qtr( "Automatic" ) );
    }

    p->section( qtr( "Volume" ) );
    const int maxVol = hasOption( "qt-max-volume" ) ? config_GetInt( p_intf, "qt-max-volume" ) : 125;
    sliderOption( p, qtr( "Maximum volume" ), QString(), "qt-max-volume", 100, 300, 5, 8,
                  QVector<QPair<double, QString> >()
                      << qMakePair( 100.0, QString( "100%" ) ) << qMakePair( 200.0, QString( "200%" ) )
                      << qMakePair( 300.0, QString( "300%" ) ) );
    SVolumeChoice *startVol = new SVolumeChoice( maxVol );
    row( p, qtr( "Startup volume" ),
         qtr( "The volume VLC starts with: the last one used, or always the same." ), startVol );
    startVol->setValue( getSettings()->value( qtKey( "startupVolume" ), -1 ).toInt() );
    connect( startVol, &SVolumeChoice::changed, this, [this]( int v ) {
        getSettings()->setValue( qtKey( "startupVolume" ), v );
        config_PutInt( p_intf, "volume-save", v < 0 );
        dirty = true;
    } );
    onRefresh( "qt-max-volume", [this, startVol]() {
        startVol->setMaximum( config_GetInt( p_intf, "qt-max-volume" ) );
    } );
    /* volume-step is out of 256; show it as a percentage */
    numberOption( p, qtr( "Volume step" ), qtr( "How much one press of the volume keys changes." ),
                  "volume-step", "%", 256.0 / 100.0 );

    p->section( qtr( "Loudness" ) );
    filterToggle( p, qtr( "Normalize loudness" ),
                  qtr( "Even out loud and quiet media." ), "audio-filter", "normvol" );
    numberOption( p, qtr( "Target level" ), qtr( "Higher is louder." ), "norm-max-level" );
    filterToggle( p, qtr( "Night mode" ),
                  qtr( "Compress the dynamic range: quiet speech louder, explosions softer." ),
                  "audio-filter", "compressor" );
    stringChoice( p, qtr( "Replay gain" ), qtr( "Use the loudness tags stored in music files." ),
                  "audio-replay-gain-mode", QList<QPair<QString, QString> >()
                      << qMakePair( qtr( "Off" ), QString( "none" ) )
                      << qMakePair( qtr( "Track" ), QString( "track" ) )
                      << qMakePair( qtr( "Album" ), QString( "album" ) ) );
    numberOption( p, qtr( "Replay gain preamp" ), qtr( "Extra gain on top of the tags, in dB." ),
                  "audio-replay-gain-preamp" );

    p->section( qtr( "Effects" ) );
    SToggle *eqOn = filterToggle( p, qtr( "Equalizer" ),
                                  qtr( "Shape the sound with ten frequency bands." ),
                                  "audio-filter", "equalizer" );
    Q_UNUSED( eqOn );
    QList<QPair<QString, QString> > presets;
    presets << qMakePair( qtr( "None" ), QString() );
    for( int i = 0; i < NB_PRESETS; i++ )
        presets << qMakePair( qtr( preset_list_text[i] ), QString( preset_list[i] ) );
    SCombo *preset = stringChoice( p, qtr( "Equalizer preset" ), qtr( "A starting point for the bands." ),
                                   "equalizer-preset", presets );
    SEqualizer *eq = new SEqualizer;
    SettingsRow *eqRow = row( p, QString(), QString(), eq, "equalizer-bands" );
    eq->setMinimumWidth( 300 );
    eq->setMaximumWidth( QWIDGETSIZE_MAX );
    eq->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    /* the equalizer fills the row: no label column */
    if( QHBoxLayout *l = qobject_cast<QHBoxLayout *>( eqRow->layout() ) )
    {
        while( l->count() > 1 )
        {
            QLayoutItem *it = l->takeAt( 0 );
            if( it->widget() && it->widget() != eq ) it->widget()->hide();
            delete it;
        }
        l->setStretch( 0, 1 );
    }
    onRefresh( "equalizer-bands", [this, eq]() {
        QVector<double> v;
        for( const QString &s : configString( p_intf, "equalizer-bands" ).split( ' ', Qt::SkipEmptyParts ) )
            v << s.toDouble();
        while( v.count() < 10 ) v << 0.0;
        eq->setBands( v );
        eq->setPreamp( config_GetFloat( p_intf, "equalizer-preamp" ) );
    } );
    auto storeBands = [this, eq, preset]() {
        QStringList l;
        for( double d : eq->bands() ) l << QString::number( d, 'f', 1 );
        config_PutPsz( p_intf, "equalizer-bands", qtu( l.join( ' ' ) ) );
        config_PutPsz( p_intf, "equalizer-preset", "" );
        { QSignalBlocker b( preset ); preset->setCurrentIndex( 0 ); }
        dirty = true;
        audio_output_t *aout = playlist_GetAout( THEPL );
        if( aout )
        {
            var_SetString( aout, "equalizer-bands", qtu( l.join( ' ' ) ) );
            vlc_object_release( aout );
        }
    };
    connect( eq, &SEqualizer::bandsChanged, this, storeBands );
    connect( eq, &SEqualizer::preampChanged, this, [this]( double v ) {
        config_PutFloat( p_intf, "equalizer-preamp", v );
        dirty = true;
        audio_output_t *aout = playlist_GetAout( THEPL );
        if( aout )
        {
            var_SetFloat( aout, "equalizer-preamp", v );
            vlc_object_release( aout );
        }
    } );
    connect( preset, QOverload<int>::of( &QComboBox::activated ), this, [this, preset]( int i ) {
        const QString name = preset->itemData( i ).toString();
        for( int k = 0; k < NB_PRESETS; k++ )
        {
            if( name != preset_list[k] ) continue;
            QStringList l;
            for( int b = 0; b < EQZ_BANDS_MAX; b++ )
                l << QString::number( eqz_preset_10b[k].f_amp[b], 'f', 1 );
            config_PutPsz( p_intf, "equalizer-bands", qtu( l.join( ' ' ) ) );
            config_PutFloat( p_intf, "equalizer-preamp", eqz_preset_10b[k].f_preamp );
            audio_output_t *aout = playlist_GetAout( THEPL );
            if( aout )
            {
                var_SetString( aout, "equalizer-preset", preset_list[k] );
                vlc_object_release( aout );
            }
        }
        changed( "equalizer-bands" );
    } );
    boolOption( p, qtr( "Two-pass" ), qtr( "Filter twice for a stronger effect." ), "equalizer-2pass" );
    SToggle *spatial = new SToggle;
    SettingsRow *spatialRow = row( p, qtr( "Spatial audio" ),
                                   qtr( "Make headphones or speakers sound wider than they are." ),
                                   spatial, "audio-filter" );
    if( !module_exists( "headphone" ) && !module_exists( "spatializer" ) )
        spatialRow->setAvailable( false, notInBuild() );
    /* the mode picks which of the two spatial filters is in the chain */
    SCombo *mode = new SCombo;
    mode->addItem( qtr( "Headphone virtualization" ), "headphone" );
    mode->addItem( qtr( "Room (speakers)" ), "spatializer" );
    SettingsRow *modeRow = row( p, qtr( "Spatial audio mode" ),
                                qtr( "Headphones: surround placed around your head. Room: a reverberant space." ),
                                mode, "audio-filter" );
    onRefresh( "audio-filter", [this, mode, spatial, modeRow]() {
        QSignalBlocker b( mode ), b2( spatial );
        const bool room = hasFilterModule( "audio-filter", "spatializer" );
        const bool head = hasFilterModule( "audio-filter", "headphone" );
        mode->setCurrentIndex( room ? 1 : 0 );
        spatial->setChecked( room || head );
        modeRow->setActive( room || head );
    } );
    connect( spatial, &SToggle::toggled, this, [this, mode]( bool on ) {
        setFilterModule( "audio-filter", "headphone", on && mode->currentIndex() == 0 );
        setFilterModule( "audio-filter", "spatializer", on && mode->currentIndex() == 1 );
    } );
    connect( mode, QOverload<int>::of( &QComboBox::activated ), this, [this]( int i ) {
        setFilterModule( "audio-filter", "headphone", i == 0 );
        setFilterModule( "audio-filter", "spatializer", i == 1 );
    } );
    filterToggle( p, qtr( "Stereo widening" ), qtr( "Spread stereo sound further apart." ),
                  "audio-filter", "stereo_widen" );

    p->beginAdvanced();
    p->section( QString() );
    numberOption( p, qtr( "Audio delay" ), qtr( "Positive: the sound comes later than the picture." ),
                  "audio-desync", "ms" );
    boolOption( p, qtr( "Time-stretch at other speeds" ),
                qtr( "Keep the pitch when playing faster or slower." ), "audio-time-stretch" );
    boolOption( p, qtr( "Pass through Dolby / DTS" ),
                qtr( "Send encoded surround to an external decoder (S/PDIF, HDMI)." ), "spdif" );
    stringChoice( p, qtr( "Visualisation" ), qtr( "What music mode shows when the song has no art." ),
                  "audio-visual" );
}

/*********************************************************************
 * Media › Subtitles
 *********************************************************************/
void SettingsDialog::buildSubtitles()
{
    SettingsPage *p = pages[SUBTITLES];
    p->section( qtr( "Tracks & Languages" ) );
    boolOption( p, qtr( "Enable subtitles" ), qtr( "Off: no subtitle is ever shown." ), "spu" );
    stringChoice( p, qtr( "Preferred language" ), qtr( "Subtitle language to pick when there are several." ),
                  "sub-language", languages( qtr( "System language" ) ) );
    SCombo *enc = stringChoice( p, qtr( "Text encoding" ),
                                qtr( "For subtitle files that are not UTF-8." ), "subsdec-encoding" );
    if( enc->count() && enc->itemData( 0 ).toString().isEmpty() )
        enc->setItemText( 0, qtr( "Automatic" ) );

    p->section( qtr( "Loading Subtitles" ) );
    boolOption( p, qtr( "Load subtitle files automatically" ),
                qtr( "Look for subtitle files next to the video." ), "sub-autodetect-file" );
    /* sub-autodetect-fuzzy is a plain 0..4 integer in VLC 3; these are the
     * design's names for 1..4 (0 is the toggle above) */
    intChoice( p, qtr( "File name matching" ),
               qtr( "How close a subtitle file's name has to be to the video's." ),
               "sub-autodetect-fuzzy", QList<QPair<QString, int> >()
                   << qMakePair( qtr( "Any title" ), 1 )
                   << qMakePair( qtr( "Contains title" ), 2 )
                   << qMakePair( qtr( "Title with extra characters" ), 3 )
                   << qMakePair( qtr( "Exact title match" ), 4 ) );

    p->section( qtr( "Appearance" ) );
    textOption( p, qtr( "Font" ), qtr( "A font family name." ), "freetype-font",
                qtr( "System, sans-serif" ) );
    SSegmented *units = new SSegmented( QStringList() << qtr( "Relative" ) << qtr( "Pixels" ) );
    row( p, qtr( "Size values" ), qtr( "Relative follows the video's size; pixels stay fixed." ), units );
    SCombo *size = new SCombo;
    row( p, qtr( "Font size" ), QString(), size, "freetype-rel-fontsize" );
    auto fillSizes = [this, size, units]() {
        QSignalBlocker b( size );
        size->clear();
        if( units->current() == 0 )
        {
            int64_t *v; char **t;
            ssize_t n = config_GetIntChoices( VLC_OBJECT( p_intf ), "freetype-rel-fontsize", &v, &t );
            for( ssize_t i = 0; i < n; i++ )
            {
                size->addItem( qtr( t[i] ), QVariant( (qlonglong)v[i] ) );
                free( t[i] );
            }
            if( n > 0 ) { free( v ); free( t ); }
            if( size->count() && size->itemData( 0 ).toLongLong() == 0 )
                size->setItemText( 0, qtr( "Automatic" ) );
            int i = size->findData( QVariant( (qlonglong)config_GetInt( p_intf, "freetype-rel-fontsize" ) ) );
            size->setCurrentIndex( qMax( 0, i ) );
        }
        else
        {
            const int cur = config_GetInt( p_intf, "freetype-fontsize" );
            for( int px : { 12, 14, 16, 18, 20, 24, 28, 32, 36, 42, 48, 56, 64 } )
                size->addItem( QString( "%1 px" ).arg( px ), px );
            int i = size->findData( cur );
            if( i < 0 ) { size->addItem( QString( "%1 px" ).arg( cur ), cur ); i = size->count() - 1; }
            size->setCurrentIndex( i );
        }
    };
    onRefresh( "freetype-fontsize", [this, units, fillSizes]() {
        QSignalBlocker b( units );
        units->setCurrent( config_GetInt( p_intf, "freetype-fontsize" ) > 0 ? 1 : 0 );
        fillSizes();
    } );
    onRefresh( "freetype-rel-fontsize", fillSizes );
    connect( units, &SSegmented::changed, this, [this, fillSizes]( int i ) {
        /* a pixel size wins over the relative one when set */
        config_PutInt( p_intf, "freetype-fontsize", i == 1 ? 24 : 0 );
        changed( "freetype-fontsize" );
        fillSizes();
    } );
    connect( size, QOverload<int>::of( &QComboBox::activated ), this, [this, size, units]( int i ) {
        if( units->current() == 0 )
            config_PutInt( p_intf, "freetype-rel-fontsize", size->itemData( i ).toLongLong() );
        else
            config_PutInt( p_intf, "freetype-fontsize", size->itemData( i ).toLongLong() );
        dirty = true;
    } );
    boolOption( p, qtr( "Bold" ), QString(), "freetype-bold" );
    colorOption( p, qtr( "Text color" ), QString(), "freetype-color" );
    sliderOption( p, qtr( "Text opacity" ), QString(), "freetype-opacity", 0, 100, 1, 10,
                  QVector<QPair<double, QString> >()
                      << qMakePair( 0.0, QString( "0%" ) ) << qMakePair( 50.0, QString( "50%" ) )
                      << qMakePair( 100.0, QString( "100%" ) ), 2.55 );
    intChoice( p, qtr( "Outline thickness" ), QString(), "freetype-outline-thickness" );
    colorOption( p, qtr( "Outline color" ), QString(), "freetype-outline-color" );
    SToggle *shadow = new SToggle;
    row( p, qtr( "Shadow" ), QString(), shadow, "freetype-shadow-opacity" );
    onRefresh( "freetype-shadow-opacity", [this, shadow]() {
        QSignalBlocker b( shadow );
        shadow->setChecked( config_GetInt( p_intf, "freetype-shadow-opacity" ) > 0 );
    } );
    connect( shadow, &SToggle::toggled, this, [this]( bool on ) {
        config_PutInt( p_intf, "freetype-shadow-opacity", on ? 128 : 0 );
        changed( "freetype-shadow-opacity" );
    } );
    colorOption( p, qtr( "Shadow color" ), QString(), "freetype-shadow-color" );
    SToggle *bg = new SToggle;
    row( p, qtr( "Background box" ), qtr( "A box behind the text." ), bg, "freetype-background-opacity" );
    onRefresh( "freetype-background-opacity", [this, bg]() {
        QSignalBlocker b( bg );
        bg->setChecked( config_GetInt( p_intf, "freetype-background-opacity" ) > 0 );
    } );
    connect( bg, &SToggle::toggled, this, [this]( bool on ) {
        config_PutInt( p_intf, "freetype-background-opacity", on ? 128 : 0 );
        changed( "freetype-background-opacity" );
    } );
    colorOption( p, qtr( "Background color" ), QString(), "freetype-background-color" );

    p->beginAdvanced();
    p->section( QString() );
    numberOption( p, qtr( "Distance from the bottom" ), qtr( "Lift subtitles off the edge, in pixels." ),
                  "sub-margin", "px" );
    textOption( p, qtr( "Also look in these folders" ),
                qtr( "Comma-separated folders, relative to the video, searched for subtitle files." ),
                "sub-autodetect-path" );
    stringChoice( p, qtr( "Text renderer" ), QString(), "text-renderer" );
}

/*********************************************************************
 * Media › Library
 *********************************************************************/
void SettingsDialog::buildLibrary()
{
    SettingsPage *p = pages[LIBRARY];
    p->section( qtr( "Opening Files" ) );
    boolOption( p, qtr( "Only allow one VLC window" ),
                qtr( "Files opened while VLC runs go to the running player." ), "one-instance" );
    SToggle *fm = boolOption( p, qtr( "Apply to files opened from the file manager" ), QString(),
                              "one-instance-when-started-from-file" );
    SToggle *enq = boolOption( p, qtr( "Add to the playlist instead of replacing it" ), QString(),
                               "playlist-enqueue" );
    QList<QWidget *> dep = { fm->parentWidget(), enq->parentWidget() };
    onRefresh( "one-instance", [this, dep]() {
        const bool on = hasOption( "one-instance" ) && config_GetInt( p_intf, "one-instance" );
        for( QWidget *w : dep )
            static_cast<SettingsRow *>( w )->setActive( on );
    } );
    stringChoice( p, qtr( "When opening a folder" ), QString(), "recursive",
                  QList<QPair<QString, QString> >()
                      << qMakePair( qtr( "Include subfolders" ), QString( "expand" ) )
                      << qMakePair( qtr( "Subfolders collapsed" ), QString( "collapse" ) )
                      << qMakePair( qtr( "This folder only" ), QString( "none" ) ) );
    boolOption( p, qtr( "Show hidden files" ), QString(), "show-hiddenfiles" );

    p->section( qtr( "Metadata & Artwork" ) );
    intChoice( p, qtr( "Fetch titles and artwork" ), QString(), "album-art",
               QList<QPair<QString, int> >()
                   << qMakePair( qtr( "From the file only" ), 0 )
                   << qMakePair( qtr( "When an item starts playing" ), 1 )
                   << qMakePair( qtr( "As soon as an item is added" ), 2 ) );
    boolOption( p, qtr( "Use the media library" ),
                qtr( "Keep VLC's media library playlist between sessions." ), "media-library" );

    p->beginAdvanced();
    p->section( QString() );
    textOption( p, qtr( "Library folders" ),
                qtr( "Folders the Videos and Music sections scan, separated by ';'. Empty: your Videos and Music folders." ),
                "qt-library-roots" );
    textOption( p, qtr( "Ignored file extensions" ),
                qtr( "Files with these extensions are skipped when a folder is opened." ),
                "ignore-filetypes" );
    boolOption( p, qtr( "Read items' details in advance" ),
                qtr( "Read titles and durations as items are added." ), "auto-preparse" );
    intChoice( p, qtr( "Playlist tree" ), QString(), "playlist-tree" );
}

/*********************************************************************
 * System › Network
 *********************************************************************/
void SettingsDialog::buildNetwork()
{
    SettingsPage *p = pages[NETWORK];
    p->section( qtr( "Buffering" ) );
    /* the presets of the old preferences: file caching, network 10/3 of it */
    /* local files, discs, network streams, live capture (ms); Normal is
     * VLC's own defaults */
    struct Cache { const char *name; int f, d, n, l; };
    static const Cache caches[] = {
        { N_("Custom"), 0, 0, 0, 0 },
        { N_("Lowest latency"), 100, 100, 333, 100 },
        { N_("Low latency"), 300, 200, 500, 200 },
        { N_("Normal"), 1000, 300, 1000, 300 },
        { N_("High latency"), 2000, 1000, 3000, 1000 },
        { N_("Higher latency"), 5000, 3000, 10000, 3000 },
    };
    SCombo *preset = new SCombo;
    for( unsigned i = 0; i < sizeof( caches ) / sizeof( caches[0] ); i++ )
        preset->addItem( qtr( caches[i].name ), (int)i );
    row( p, qtr( "Buffering" ), qtr( "How much VLC reads ahead: more is smoother, less starts faster." ),
         preset, "file-caching" );
    SField *f1 = numberOption( p, qtr( "Local files" ), QString(), "file-caching", "ms" );
    SField *f2 = numberOption( p, qtr( "Discs" ), QString(), "disc-caching", "ms" );
    SField *f3 = numberOption( p, qtr( "Network streams" ), QString(), "network-caching", "ms" );
    SField *f4 = numberOption( p, qtr( "Live capture" ), QString(), "live-caching", "ms" );
    QList<QWidget *> custom = { f1->parentWidget(), f2->parentWidget(), f3->parentWidget(), f4->parentWidget() };
    auto syncPreset = [this, preset, custom]() {
        QSignalBlocker b( preset );
        int idx = 0;
        for( unsigned i = 1; i < sizeof( caches ) / sizeof( caches[0] ); i++ )
            if( config_GetInt( p_intf, "file-caching" ) == caches[i].f
             && config_GetInt( p_intf, "disc-caching" ) == caches[i].d
             && config_GetInt( p_intf, "network-caching" ) == caches[i].n
             && config_GetInt( p_intf, "live-caching" ) == caches[i].l )
                idx = i;
        preset->setCurrentIndex( idx );
        for( QWidget *w : custom )
            static_cast<SettingsRow *>( w )->setActive( preset->currentIndex() == 0 );
    };
    for( const char *o : { "file-caching", "disc-caching", "network-caching", "live-caching" } )
        onRefresh( o, syncPreset );
    connect( preset, QOverload<int>::of( &QComboBox::activated ), this, [this, preset, custom]( int i ) {
        const Cache &c = caches[preset->itemData( i ).toInt()];
        if( c.f > 0 )
        {
            config_PutInt( p_intf, "file-caching", c.f );
            config_PutInt( p_intf, "network-caching", c.n );
            config_PutInt( p_intf, "disc-caching", c.d );
            config_PutInt( p_intf, "live-caching", c.l );
            changed( "file-caching" );
            changed( "network-caching" );
            changed( "disc-caching" );
            changed( "live-caching" );
        }
        for( QWidget *w : custom )
            static_cast<SettingsRow *>( w )->setActive( i == 0 );
    } );

    p->section( qtr( "Proxy" ) );
    textOption( p, qtr( "HTTP proxy" ), qtr( "http://host:port. Empty: the system proxy." ), "http-proxy",
                qtr( "System proxy" ) );
    textOption( p, qtr( "HTTP proxy password" ), QString(), "http-proxy-pwd", QString(), true );

    p->section( qtr( "Security" ) );
    boolOption( p, qtr( "Verify certificates against the system trust store" ),
                qtr( "Off: trust only the certificates you accepted in VLC." ), "gnutls-system-trust" );
    stringChoice( p, qtr( "Cipher preference" ), qtr( "Which TLS ciphers VLC offers first." ),
                  "gnutls-priorities", QList<QPair<QString, QString> >()
                      << qMakePair( qtr( "Faster" ), QString( "PERFORMANCE" ) )
                      << qMakePair( qtr( "Balanced" ), QString( "NORMAL" ) )
                      << qMakePair( qtr( "Secure (128-bit)" ), QString( "SECURE128" ) )
                      << qMakePair( qtr( "Most secure (256-bit)" ), QString( "SECURE256" ) ) );

    p->beginAdvanced();
    p->section( QString() );
    boolOption( p, qtr( "Ask before using the network at start" ), QString(), "qt-privacy-ask" );
    textOption( p, qtr( "User agent" ), qtr( "How VLC names itself to web servers." ), "user-agent" );
    boolOption( p, qtr( "Use IPv4 only" ), QString(), "ipv4" );
    numberOption( p, qtr( "Connection timeout" ), QString(), "ipv4-timeout", "ms" );
}

/*********************************************************************
 * System › Performance
 *********************************************************************/
void SettingsDialog::buildPerformance()
{
    SettingsPage *p = pages[PERFORMANCE];
    p->section( qtr( "Decoding" ) );
    SCombo *hw = stringChoice( p, qtr( "Hardware-accelerated decoding" ),
                               qtr( "Decode video on the graphics card when it can." ), "avcodec-hw" );
    for( int i = 0; i < hw->count(); i++ )
    {
        const QString d = hw->itemData( i ).toString();
        if( d == "any" ) hw->setItemText( i, qtr( "Automatic" ) );
        else if( d == "none" ) hw->setItemText( i, qtr( "Off" ) );
    }
    SField *pp = numberOption( p, qtr( "Video post-processing" ),
                               qtr( "Deblocking quality, 0 (off) to 6." ), "postproc-q" );
    Q_UNUSED( pp );
    intChoice( p, qtr( "Skip the H.264 loop filter" ),
               qtr( "Faster on slow computers, at some cost in picture quality." ),
               "avcodec-skiploopfilter" );
    numberOption( p, qtr( "Decoding threads" ), qtr( "0: as many as the processor has." ), "avcodec-threads" );

    p->section( qtr( "Smooth Playback" ) );
    boolOption( p, qtr( "Drop frames when the system can't keep up" ), QString(), "drop-late-frames" );
    boolOption( p, qtr( "Give VLC real-time priority" ),
                qtr( "Can make the whole system less responsive." ), "rt-priority" );
    numberOption( p, qtr( "Process priority" ), qtr( "Added to the priority of VLC's threads." ), "rt-offset" );
    SButton *rescan = new SButton( qtr( "Rescan" ) );
    rescan->setFixedWidth( CONTROL_W );
    row( p, qtr( "Rescan plugins" ), qtr( "Rebuild the list of plugins the next time VLC starts." ), rescan );
    connect( rescan, &SButton::clicked, this, [this, rescan]() {
        config_PutInt( p_intf, "reset-plugins-cache", 1 );
        dirty = true;
        rescan->setText( qtr( "Plugins are rescanned at the next start" ) );
        rescan->setEnabled( false );
    } );

    p->beginAdvanced();
    p->section( qtr( "Developer" ) );
    SCombo *enc = stringChoice( p, qtr( "Encoder" ), qtr( "Force an encoder module." ), "encoder" );
    Q_UNUSED( enc );
    stringChoice( p, qtr( "Decoder" ), qtr( "Force a decoder module." ), "codec" );
    intChoice( p, qtr( "Clock synchronisation" ), QString(), "clock-synchro",
               QList<QPair<QString, int> >()
                   << qMakePair( qtr( "Default" ), -1 ) << qMakePair( qtr( "Off" ), 0 )
                   << qMakePair( qtr( "On" ), 1 ) );
    numberOption( p, qtr( "Clock jitter" ), QString(), "clock-jitter", "ms" );
    boolOption( p, qtr( "Skip frames" ), qtr( "Let decoders skip frames to keep up." ), "skip-frames" );
}

/*********************************************************************
 * System › Recording
 *********************************************************************/
void SettingsDialog::buildRecording()
{
    SettingsPage *p = pages[RECORDING];
    p->section( qtr( "Recording" ) );
    pathOption( p, qtr( "Save recordings to" ), qtr( "Where the record button saves." ), "input-record-path",
                true, QStandardPaths::writableLocation( QStandardPaths::MoviesLocation ) );
    SToggle *native = new SToggle;
    native->setChecked( true );
    unavailable( p, qtr( "Record without re-encoding when possible" ),
                 qtr( "VLC 3 always records the original stream as it is." ), native );

    p->section( qtr( "Casting" ) );
    SToggle *cast = new SToggle;
    SettingsRow *castRow = row( p, qtr( "Discover cast devices" ),
                                qtr( "Look for Chromecast devices on your network." ), cast );
    if( !module_exists( "microdns" ) )
        castRow->setAvailable( false, notInBuild() );
    else
    {
        cast->setChecked( getSettings()->value( qtKey( "castDiscovery" ), false ).toBool() );
        connect( cast, &SToggle::toggled, this, [this]( bool on ) {
            getSettings()->setValue( qtKey( "castDiscovery" ), on );
            if( on ) ActionsManager::getInstance( p_intf )->StartRendererScan();
            else ActionsManager::getInstance( p_intf )->StopRendererScan();
        } );
    }
    intChoice( p, qtr( "Cast quality" ),
               qtr( "Quality of video converted for the device: higher needs more bandwidth." ),
               "sout-chromecast-conversion-quality", QList<QPair<QString, int> >()
                   << qMakePair( qtr( "High" ), 0 ) << qMakePair( qtr( "Medium" ), 1 )
                   << qMakePair( qtr( "Low" ), 2 ) << qMakePair( qtr( "Low CPU" ), 3 ) );

    p->section( qtr( "Streaming" ) );
    boolOption( p, qtr( "Play locally while streaming" ), QString(), "sout-display" );
    boolOption( p, qtr( "Include audio" ), QString(), "sout-audio" );
    boolOption( p, qtr( "Include video" ), QString(), "sout-video" );
    boolOption( p, qtr( "Include subtitles" ), QString(), "sout-spu" );
    stringChoice( p, qtr( "Encoding preset" ), qtr( "x264 speed against file size." ), "sout-x264-preset" );
    stringChoice( p, qtr( "Tuning" ), qtr( "x264 tuning for the kind of video." ), "sout-x264-tune" );

    p->beginAdvanced();
    p->section( QString() );
    boolOption( p, qtr( "Keep streaming outputs open" ),
                qtr( "Reuse the output between items instead of reopening it." ), "sout-keep" );
    textOption( p, qtr( "Default stream output" ), qtr( "A --sout chain used for everything played." ), "sout" );
}

/*********************************************************************
 * System › Extensions
 *********************************************************************/
void SettingsDialog::buildExtensions()
{
    SettingsPage *p = pages[EXTENSIONS];
    p->section( qtr( "Add-ons" ) );

    QWidget *box = new QWidget;
    QVBoxLayout *v = new QVBoxLayout( box );
    v->setContentsMargins( 0, 0, 0, 0 );
    v->setSpacing( 10 );
    QListView *list = new QListView( box );
    list->setObjectName( "SAddons" );
    list->setFixedHeight( 136 );
    list->setFont( SettingsUI::font( 14 ) );
    list->setEditTriggers( QAbstractItemView::NoEditTriggers );
    list->setIconSize( QSize( 24, 24 ) );
    ExtensionsManager *em = ExtensionsManager::getInstance( p_intf );
    if( !em->isLoaded() && !em->cannotLoad() )
        em->loadExtensions();
    ExtensionListModel *model = new ExtensionListModel( list, em );
    QSortFilterProxyModel *proxy = new QSortFilterProxyModel( list );
    proxy->setSourceModel( model );
    proxy->setFilterCaseSensitivity( Qt::CaseInsensitive );
    list->setModel( proxy );
    v->addWidget( list );

    QHBoxLayout *h = new QHBoxLayout;
    h->setSpacing( 8 );
    SField *find = new SField( box );
    find->setPlaceholderText( qtr( "Search" ) );
    find->setFixedSize( 146, 31 );
    connect( find, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString );
    SButton *more = new SButton( qtr( "Find more extensions" ), box );
    SButton *add = new SButton( qtr( "Add" ), box );
    SButton *remove = new SButton( qtr( "Remove" ), box );
    more->setFixedWidth( more->sizeHint().width() );
    add->setFixedWidth( 70 );
    remove->setFixedWidth( 80 );
    h->addWidget( find );
    h->addStretch( 1 );
    h->addWidget( more );
    h->addWidget( add );
    h->addWidget( remove );
    v->addLayout( h );
    box->setFixedHeight( 136 + 10 + 31 );
    box->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );

    SettingsRow *listRow = row( p, QString(), QString(), box );
    box->setMaximumWidth( QWIDGETSIZE_MAX );
    if( QHBoxLayout *l = qobject_cast<QHBoxLayout *>( listRow->layout() ) )
    {
        while( l->count() > 1 )
        {
            QLayoutItem *it = l->takeAt( 0 );
            if( it->widget() && it->widget() != box ) it->widget()->hide();
            delete it;
        }
        l->setStretch( 0, 1 );
    }

    connect( more, &SButton::clicked, this, []() { THEDP->pluginDialog(); } );
    char *psz_data = config_GetUserDir( VLC_DATA_DIR );
    const QString userDir = QString( "%1/lua/extensions" ).arg( qfu( psz_data ) );
    free( psz_data );
    connect( add, &SButton::clicked, this, [this, em, userDir]() {
        const QString file = QFileDialog::getOpenFileName( this, qtr( "Add an extension" ),
                                 QDir::homePath(), qtr( "VLC extensions (*.lua *.luac)" ) );
        if( file.isEmpty() ) return;
        QDir().mkpath( userDir );
        const QString dest = userDir + "/" + QFileInfo( file ).fileName();
        if( QFile::exists( dest ) ) QFile::remove( dest );
        if( !QFile::copy( file, dest ) )
        {
            QMessageBox::warning( this, qtr( "Add an extension" ),
                                  qtr( "Could not copy the file to %1." ).arg( userDir ) );
            return;
        }
        em->reloadExtensions();
    } );
    connect( remove, &SButton::clicked, this, [this, em, list, userDir]() {
        const QModelIndex i = list->currentIndex();
        if( !i.isValid() ) return;
        const QString file = i.data( ExtensionListModel::FilenameRole ).toString();
        if( !QFileInfo( file ).absoluteFilePath().startsWith( QFileInfo( userDir ).absoluteFilePath() ) )
        {
            QMessageBox::information( this, qtr( "Remove an extension" ),
                qtr( "This extension came with VLC and cannot be removed here." ) );
            return;
        }
        if( QMessageBox::question( this, qtr( "Remove an extension" ),
                qtr( "Move %1 to the trash?" ).arg( i.data( Qt::DisplayRole ).toString() ) )
            != QMessageBox::Yes )
            return;
        if( !QFile::moveToTrash( file ) )
            QFile::remove( file );
        em->reloadExtensions();
    } );

    boolOption( p, qtr( "Load add-ons on startup" ),
                qtr( "Off: extensions load the first time you open the View menu." ),
                "qt-autoload-extensions" );

    p->beginAdvanced();
    p->section( QString() );
    textOption( p, qtr( "Lua interface" ), qtr( "A Lua interface to load at start (for example: cli)." ),
                "lua-intf" );
    textOption( p, qtr( "Lua interface configuration" ), QString(), "lua-config" );
}
