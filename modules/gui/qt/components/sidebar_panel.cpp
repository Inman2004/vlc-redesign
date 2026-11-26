/*****************************************************************************
 * sidebar_panel.cpp : redesigned library sidebar
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

#include "components/sidebar_panel.hpp"
#include "components/nav_rail.hpp"
#include "components/playlist/playlist.hpp"
#include "components/playlist/standardpanel.hpp"
#include "components/playlist/selector.hpp"
#include "components/playlist/playlist_model.hpp"
#include "components/playlist/vlc_model.hpp"
#include "util/searchlineedit.hpp"
#include "styles/vlc_theme.hpp"
#include "components/redesign_menus.hpp"
#include "dialogs_provider.hpp"
#include "recents.hpp"
#include "library/library_browser.hpp"
#include "library/library_view.hpp"
#include "library/media_library.hpp"

#include <vlc_playlist.h>
#include <vlc_services_discovery.h>

#include <QFileInfo>
#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>

#include <QCollator>

#include <algorithm>
#include <functional>
#include <QListWidget>
#include <QMenu>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

static QToolButton *sideButton( QWidget *parent, const QString &icon, const QString &tip )
{
    QToolButton *b = new QToolButton( parent );
    Theme::setupIconButton( b, icon, 32, 24 );
    b->setToolTip( tip );
    return b;
}

/* Library rows as drawn in the design: 42px, line icon, muted name */
namespace {
/* a breadcrumb segment that goes back to its level: plain text, flush
 * like the others (a QToolButton would add its own padding) */
class CrumbLink : public QLabel
{
public:
    CrumbLink( const QString &text, std::function<void()> f, QWidget *parent )
        : QLabel( text, parent ), onClick( f )
    {
        setObjectName( "SidebarCrumbLink" );
        setCursor( Qt::PointingHandCursor );
        setAttribute( Qt::WA_Hover );
    }
protected:
    void mouseReleaseEvent( QMouseEvent *e ) override
    {
        if( e->button() == Qt::LeftButton && rect().contains( e->pos() ) )
            onClick();
    }
private:
    std::function<void()> onClick;
};

class SidebarItemDelegate : public QStyledItemDelegate
{
public:
    SidebarItemDelegate( QObject *parent ) : QStyledItemDelegate( parent ) {}

    QSize sizeHint( const QStyleOptionViewItem &, const QModelIndex & ) const Q_DECL_OVERRIDE
    {
        return QSize( 100, 42 );
    }

    void paint( QPainter *p, const QStyleOptionViewItem &opt,
                const QModelIndex &index ) const Q_DECL_OVERRIDE
    {
        const Theme::Palette &c = Theme::colors();
        p->save();
        p->setRenderHint( QPainter::Antialiasing );
        const QRectF r = QRectF( opt.rect ).adjusted( 0, 1, -2, -1 );

        if( opt.state & QStyle::State_Selected )
        {
            QColor sel = Theme::accent();
            sel.setAlpha( 40 );
            p->setPen( Qt::NoPen );
            p->setBrush( sel );
            p->drawRoundedRect( r, 6, 6 );
        }
        else if( opt.state & QStyle::State_MouseOver )
        {
            p->setPen( Qt::NoPen );
            p->setBrush( c.hover );
            p->drawRoundedRect( r, 6, 6 );
        }

        const bool current = index.data( VLCModel::CURRENT_ITEM_ROLE ).toBool();
        const bool leaf = index.data( VLCModel::LEAF_NODE_ROLE ).toBool();
        const QColor fg = current ? Theme::accent() : c.textMuted;

        const QRect iconRect( opt.rect.left() + 10, opt.rect.center().y() - 10, 20, 20 );
        p->drawPixmap( iconRect, Theme::pixmap( leaf ? "video" : "playlist", 20,
                                                current ? Theme::accent() : c.icon ) );

        QFont f = opt.font;
        f.setPixelSize( 15 );
        p->setFont( f );
        p->setPen( current ? c.text : fg );
        const QRect textRect = opt.rect.adjusted( 46, 0, -8, 0 );
        const QString text = QFontMetrics( f ).elidedText(
                    index.data( Qt::DisplayRole ).toString(), Qt::ElideRight, textRect.width() );
        p->drawText( textRect, Qt::AlignLeft | Qt::AlignVCenter, text );
        p->restore();
    }
};
}

SidebarPanel::SidebarPanel( intf_thread_t *_p_intf, QWidget *parent )
    : QWidget( parent ), p_intf( _p_intf ), plWidget( NULL ), plView( NULL ),
      browser( NULL ), libGrid( NULL ), libSearch( NULL ),
      historyPos( -1 ), navigating( false ), sortOrder( Qt::AscendingOrder )
{
    setObjectName( "VLCSidebar" );
    setAttribute( Qt::WA_StyledBackground, true );
    setMinimumWidth( Theme::SIDEBAR_MIN_WIDTH );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 16, 16, 18, 0 );
    layout->setSpacing( 0 );

    /* Row 1: back / forward, section title, more */
    QHBoxLayout *row1 = new QHBoxLayout;
    row1->setSpacing( 2 );
    backBtn = sideButton( this, "back", qtr( "Back" ) );
    fwdBtn  = sideButton( this, "forward", qtr( "Forward" ) );
    /* the title is a breadcrumb: earlier segments link back to that level */
    title = new QWidget( this );
    crumbLayout = new QHBoxLayout( title );
    crumbLayout->setContentsMargins( 0, 0, 0, 0 );
    crumbLayout->setSpacing( 0 );
    moreBtn = sideButton( this, "dots", qtr( "More" ) );
    popBtn = sideButton( this, "popout", qtr( "Open in a separate window" ) );
    row1->setSpacing( 4 );
    row1->addWidget( backBtn );
    row1->addWidget( fwdBtn );
    row1->addSpacing( 3 );
    row1->addWidget( title, 1 );
    row1->addWidget( moreBtn );
    row1->addSpacing( -4 );
    row1->addWidget( popBtn );
    layout->addLayout( row1 );
    layout->addSpacing( 13 );

    /* Row 2: item count, sort, view, search */
    QHBoxLayout *row2 = new QHBoxLayout;
    row2->setSpacing( 4 );
    row2->setContentsMargins( 8, 0, 0, 0 );
    count = new QLabel( this );
    count->setObjectName( "SidebarCount" );
    sortBtn   = sideButton( this, "sort", qtr( "Sort" ) );
    viewBtn   = sideButton( this, "view", qtr( "Change view" ) );
    searchBtn = sideButton( this, "search", qtr( "Search" ) );
    searchBtn->setCheckable( true );
    row2->addWidget( count, 1 );
    row2->addWidget( sortBtn );
    row2->addWidget( viewBtn );
    row2->addWidget( searchBtn );
    layout->addLayout( row2 );

    /* Search field (the playlist's own SearchLineEdit is moved here) */
    searchRow = new QWidget( this );
    searchLayout = new QVBoxLayout( searchRow );
    searchLayout->setContentsMargins( 0, 10, 0, 0 );
    searchRow->hide();
    layout->addWidget( searchRow );

    layout->addSpacing( 11 );
    QFrame *rule = new QFrame( this );
    rule->setObjectName( "SidebarRule" );
    rule->setFixedHeight( 2 ); /* measured: 2px #191919 */
    QHBoxLayout *ruleRow = new QHBoxLayout;
    ruleRow->setContentsMargins( 4, 0, 2, 0 );
    ruleRow->addWidget( rule );
    layout->addLayout( ruleRow );
    layout->addSpacing( 13 );

    /* Pages: the playlist view; the library rows */
    pages = new QStackedWidget( this );
    plPage = new QWidget( pages );
    plLayout = new QVBoxLayout( plPage );
    plLayout->setContentsMargins( 0, 0, 0, 0 );
    pages->addWidget( plPage );

    /* Videos, Music, Network: the library's rows, one column */
    browser = new LibraryBrowser( p_intf, "Sidebar/librarySort", this );
    browser->setActionRows( true );
    libGrid = new LibraryGrid( p_intf, pages );
    libGrid->setCompact( true );
    libGrid->setMode( getSettings()->value( "Sidebar/libraryView", "list" ).toString() == "grid"
                      ? LibraryGrid::GRID : LibraryGrid::LIST );
    pages->addWidget( libGrid );
    layout->addWidget( pages, 1 );

    libSearch = new QLineEdit( searchRow );
    libSearch->setPlaceholderText( qtr( "Search" ) );
    libSearch->setClearButtonEnabled( true );
    libSearch->hide();
    searchLayout->addWidget( libSearch );

    MediaLibrary *lib = MediaLibrary::instance( p_intf );
    connect( lib, &MediaLibrary::thumbnailReady, libGrid, &LibraryGrid::refreshThumbnail );
    connect( libGrid, &LibraryGrid::activated, browser, &LibraryBrowser::activate );
    connect( libGrid, &LibraryGrid::contextMenu, this, [this]( int i, const QPoint &pos ) {
        browser->showEntryMenu( i, pos, this );
    } );
    connect( browser, &LibraryBrowser::locationChanged, this, &SidebarPanel::libraryLocationChanged );
    connect( browser, &LibraryBrowser::entriesChanged, this, &SidebarPanel::libraryEntriesChanged );
    connect( browser, &LibraryBrowser::actionRequested, this, &SidebarPanel::libraryAction );
    connect( libSearch, &QLineEdit::textChanged, browser, &LibraryBrowser::setSearch );

    connect( backBtn, &QToolButton::clicked, this, &SidebarPanel::goBack );
    connect( fwdBtn, &QToolButton::clicked, this, &SidebarPanel::goForward );
    connect( searchBtn, &QToolButton::toggled, this, &SidebarPanel::toggleSearch );
    connect( sortBtn, &QToolButton::clicked, this, &SidebarPanel::showSortMenu );
    connect( moreBtn, &QToolButton::clicked, this, &SidebarPanel::showMoreMenu );
    connect( popBtn, &QToolButton::clicked, this, &SidebarPanel::popOutRequested );
    connect( viewBtn, &QToolButton::clicked, this, &SidebarPanel::showViewMenu );

    backBtn->setEnabled( false );
    fwdBtn->setEnabled( false );
}

void SidebarPanel::setPlaylistWidget( PlaylistWidget *w )
{
    /* (re)attach: also used when the pop-out window gives it back */
    w->setParent( plPage );
    w->setSidebarMode( true );
    plLayout->addWidget( w );
    w->show();
    if( plWidget == w )
        return;
    plWidget = w;

    SearchLineEdit *search = w->searchField();
    search->setMaximumWidth( QWIDGETSIZE_MAX );
    searchLayout->addWidget( search );

    StandardPLPanel *view = w->mainView;
    /* the design shows a plain icon + name list: use the tree view with
     * a single column, no header and our own row delegate */
    view->showView( StandardPLPanel::TREE_VIEW );
    QTreeView *tree = view->findChild<QTreeView *>();
    if( tree )
    {
        const int titleCol = VLCModel::columnFromMeta( COLUMN_TITLE );
        tree->header()->hide();
        for( int c = 0; c < tree->model()->columnCount(); c++ )
            tree->setColumnHidden( c, c != titleCol );
        tree->setRootIsDecorated( false );
        tree->setIndentation( 0 );
        tree->setUniformRowHeights( true );
        tree->setAlternatingRowColors( false );   /* Figma rows are never striped */
        tree->setFrameShape( QFrame::NoFrame );
        tree->setMouseTracking( true );
        tree->viewport()->setAttribute( Qt::WA_Hover );
        tree->setItemDelegate( new SidebarItemDelegate( tree ) );
    }
    connect( view, &StandardPLPanel::viewChanged, this, &SidebarPanel::rootChanged );
    plView = view;

    QAbstractItemModel *model = PLModel::getPLModel( p_intf );
    connect( model, &QAbstractItemModel::rowsInserted, this, &SidebarPanel::updateCount );
    connect( model, &QAbstractItemModel::rowsRemoved, this, &SidebarPanel::updateCount );
    connect( model, &QAbstractItemModel::modelReset, this, &SidebarPanel::updateCount );
    connect( model, &QAbstractItemModel::layoutChanged, this, &SidebarPanel::updateCount );
    updateCount();
}

bool SidebarPanel::libraryShown() const
{
    return pages->currentWidget() == libGrid;
}

/* The title as a breadcrumb (Components: "… ▸ The Wand ▸ S01"): the
 * last two segments, an ellipsis before them when there are more. */
void SidebarPanel::setTitleCrumb( const QStringList &segments, int linkedFrom )
{
    QLayoutItem *child;
    while( ( child = crumbLayout->takeAt( 0 ) ) )
    {
        delete child->widget();
        delete child;
    }
    /* Figma: a 6px triangle with 8-9px either side */
    auto separator = [this]() {
        QLabel *sep = new QLabel( QString::fromUtf8( "▸" ), title );
        sep->setObjectName( "SidebarCrumbSeparator" );
        sep->setContentsMargins( 7, 1, 6, 0 );
        crumbLayout->addWidget( sep );
    };
    const int first = qMax( 0, segments.size() - 2 );
    if( first > 0 )
    {
        QLabel *more = new QLabel( QString::fromUtf8( "…" ), title );
        more->setObjectName( "SidebarTitle" );
        more->setToolTip( segments.mid( 0, first ).join( QString::fromUtf8( " ▸ " ) ) );
        crumbLayout->addWidget( more );
        separator();
    }
    for( int i = first; i < segments.size(); i++ )
    {
        const bool last = i == segments.size() - 1;
        if( !last && i >= linkedFrom )
        {
            crumbLayout->addWidget( new CrumbLink( segments[i], [this, i]() {
                if( libraryShown() )
                    browser->setLocation( browser->location().mid( 0, i + 1 ) );
                else if( !sdFromNetwork.isEmpty() && i == 0 )
                    showSection( "network" );
            }, title ) );
        }
        else
        {
            QLabel *seg = new QLabel( segments[i], title );
            seg->setObjectName( "SidebarTitle" );
            crumbLayout->addWidget( seg, last ? 1 : 0 );
        }
        if( !last )
            separator();
    }
}

void SidebarPanel::showSection( const QString &key )
{
    section = key;
    sdFromNetwork.clear();
    setTitleCrumb( QStringList( NavRail::titleFor( key ) ), 0 );

    const bool library = key == "videos" || key == "music" || key == "network"
                         || key == "home" || key == "recents" || key == "playlists";
    searchBtn->blockSignals( true );
    searchBtn->setChecked( false );
    searchBtn->blockSignals( false );
    searchRow->hide();
    libSearch->clear();
    if( plWidget )
        plWidget->searchField()->setVisible( !library );
    libSearch->setVisible( library );

    if( library )
    {
        pages->setCurrentWidget( libGrid );
        const bool browse = key != "network";
        /* Home and Recent: in the order things were played */
        const bool ordered = key == "home" || key == "recents";
        sortBtn->setEnabled( browse && !ordered );
        viewBtn->setEnabled( browse && key != "home" );
        /* Network: for Live TV's long lists (countries) */
        searchBtn->setEnabled( browse || key == "network" );
        browser->openSection( key == "recents" ? QString( "recent" ) : key );
        return;
    }

    setSortDot( false );

    pages->setCurrentWidget( plPage );
    sortBtn->setEnabled( true );
    viewBtn->setEnabled( true );
    searchBtn->setEnabled( true );
    if( !plWidget )
        return;

    /* Rail section -> existing playlist node */
    QStringList candidates;
    candidates << "playlist";

    history.clear();
    historyPos = -1;
    navigating = false;
    PLSelector *selector = plWidget->sourceSelector();
    foreach( const QString &c, candidates )
        if( !selector->selectSource( c ).isEmpty() )
            break;
}

void SidebarPanel::rootChanged( const QModelIndex &index )
{
    currentRoot = index;
    if( !navigating )
    {
        while( history.size() > historyPos + 1 )
            history.removeLast();
        history.append( QPersistentModelIndex( index ) );
        historyPos = history.size() - 1;
    }
    navigating = false;
    if( pages->currentWidget() != plPage )
        return;
    /* a source opened from Network: Back returns to the Network list */
    backBtn->setEnabled( historyPos > 0 || !sdFromNetwork.isEmpty() );
    fwdBtn->setEnabled( historyPos >= 0 && historyPos < history.size() - 1 );
    updateCount();
}

void SidebarPanel::goBack()
{
    if( libraryShown() )
    {
        browser->goBack();
        return;
    }
    if( historyPos <= 0 && !sdFromNetwork.isEmpty() )
    {
        showSection( "network" );
        return;
    }
    if( historyPos <= 0 || !plWidget )
        return;
    navigating = true;
    historyPos--;
    plWidget->mainView->browseInto( history.at( historyPos ) );
}

void SidebarPanel::goForward()
{
    if( libraryShown() )
    {
        browser->goForward();
        return;
    }
    if( historyPos >= history.size() - 1 || !plWidget )
        return;
    navigating = true;
    historyPos++;
    plWidget->mainView->browseInto( history.at( historyPos ) );
}

void SidebarPanel::updateCount()
{
    if( libraryShown() )
    {
        const int n = browser->itemCount();
        count->setText( n == 1 ? qtr( "1 item" ) : qtr( "%1 items" ).arg( n ) );
        return;
    }
    QAbstractItemModel *model = PLModel::getPLModel( p_intf );
    const int n = model->rowCount( currentRoot.isValid() ? QModelIndex( currentRoot )
                                                         : QModelIndex() );
    count->setText( n == 1 ? qtr( "1 item" ) : qtr( "%1 items" ).arg( n ) );
}

void SidebarPanel::toggleSearch( bool on )
{
    searchRow->setVisible( on );
    if( libraryShown() )
    {
        if( on )
            libSearch->setFocus();
        else
            libSearch->clear();
        return;
    }
    if( plWidget )
    {
        if( on )
            plWidget->setSearchFieldFocus();
        else
            plWidget->searchField()->clear();
    }
}

/* Figma sort menu. Watched/date orders need the library's history and
 * dates (P5a): shown, disabled. The last choice carries the accent check. */
void SidebarPanel::showSortMenu()
{
    if( libraryShown() )
    {
        browser->showSortMenu( sortBtn->mapToGlobal( QPoint( 0, sortBtn->height() + 4 ) ), this );
        return;
    }
    static const struct { const char *name; int meta; int order; } keys[] = {
        { N_("Unwatched First"), 0, 0 }, { N_("Watched First"), 0, 0 },
        { N_("Newest First"), 0, 0 },    { N_("Oldest First"), 0, 0 },
        { N_("Title A-Z"), COLUMN_TITLE, Qt::AscendingOrder },
        { N_("Title Z-A"), COLUMN_TITLE, Qt::DescendingOrder },
        { N_("Artist A-Z"), COLUMN_ARTIST, Qt::AscendingOrder },
        { N_("Artist Z-A"), COLUMN_ARTIST, Qt::DescendingOrder },
        { N_("Album A-Z"), COLUMN_ALBUM, Qt::AscendingOrder },
        { N_("Album Z-A"), COLUMN_ALBUM, Qt::DescendingOrder },
        { N_("Track Number Ascending"), COLUMN_TRACK_NUMBER, Qt::AscendingOrder },
        { N_("Track Number Descending"), COLUMN_TRACK_NUMBER, Qt::DescendingOrder },
        { N_("URI Ascending"), COLUMN_URI, Qt::AscendingOrder },
        { N_("URI Descending"), COLUMN_URI, Qt::DescendingOrder },
        { N_("Disc Number Ascending"), COLUMN_DISC_NUMBER, Qt::AscendingOrder },
        { N_("Disc Number Descending"), COLUMN_DISC_NUMBER, Qt::DescendingOrder },
    };
    QMenu menu( this );
    menu.setObjectName( "VLCSortMenu" );
    menu.setToolTipsVisible( true );
    const QString last = getSettings()->value( "Sidebar/sort" ).toString();
    for( size_t i = 0; i < sizeof( keys ) / sizeof( keys[0] ); i++ )
    {
        const QString key = keys[i].name;
        QAction *a = menu.addAction( key == last ? RedesignMenus::menuIcon( "check", 20 )
                                                 : RedesignMenus::menuIcon( "blank", 20 ),
                                     qtr( keys[i].name ) );
        if( !keys[i].meta )
        {
            a->setEnabled( false );
            a->setToolTip( qtr( "Arrives with the media library" ) );
            continue;
        }
        const int column = VLCModel::columnFromMeta( keys[i].meta );
        const Qt::SortOrder order = (Qt::SortOrder) keys[i].order;
        connect( a, &QAction::triggered, this, [this, column, order, key]() {
            sortOrder = order;
            PLModel::getPLModel( p_intf )->sort( column, order );
            getSettings()->setValue( "Sidebar/sort", key );
        } );
    }
    menu.exec( sortBtn->mapToGlobal( QPoint( 0, sortBtn->height() + 4 ) ) );
}

/* Figma view menu: Grid View / List View */
void SidebarPanel::showViewMenu()
{
    if( libraryShown() )
    {
        QMenu menu( this );
        menu.setObjectName( "VLCViewMenu" );
        const bool grid = libGrid->mode() == LibraryGrid::GRID;
        menu.addAction( RedesignMenus::menuIcon( "grid", 20 ), qtr( "Grid View" ), this, [this]() {
            libGrid->setMode( LibraryGrid::GRID );
            getSettings()->setValue( "Sidebar/libraryView", "grid" );
        } )->setEnabled( !grid );
        menu.addAction( RedesignMenus::menuIcon( "view", 20 ), qtr( "List View" ), this, [this]() {
            libGrid->setMode( LibraryGrid::LIST );
            getSettings()->setValue( "Sidebar/libraryView", "list" );
        } )->setEnabled( grid );
        menu.exec( viewBtn->mapToGlobal( QPoint( 0, viewBtn->height() + 4 ) ) );
        return;
    }
    if( !plView )
        return;
    QMenu menu( this );
    menu.setObjectName( "VLCViewMenu" );
    const bool grid = plView->currentViewIndex() == StandardPLPanel::ICON_VIEW;
    menu.addAction( RedesignMenus::menuIcon( "grid", 20 ), qtr( "Grid View" ), this, [this]() {
        plView->showView( StandardPLPanel::ICON_VIEW );
    } )->setEnabled( !grid );
    menu.addAction( RedesignMenus::menuIcon( "view", 20 ), qtr( "List View" ), this, [this]() {
        plView->showView( StandardPLPanel::TREE_VIEW );
    } )->setEnabled( grid );
    menu.exec( viewBtn->mapToGlobal( QPoint( 0, viewBtn->height() + 4 ) ) );
}

void SidebarPanel::showMoreMenu()
{
    QMenu menu( this );
    if( section == "network" )
    {
        /* every other discovery source of VLC 3 (local network, SAP...):
         * the panel shows the design's three rows only */
        char **longnames;
        int *categories;
        char **names = vlc_sd_GetNames( THEPL, &longnames, &categories );
        if( names )
        {
            for( int i = 0; names[i]; i++ )
            {
                const QString name = qfu( names[i] );
                const QString longname = qfu( longnames[i] );
                if( ( categories[i] == SD_CAT_LAN || categories[i] == SD_CAT_INTERNET )
                    && !name.startsWith( "podcast" ) && !name.contains( "icecast" ) )
                    menu.addAction( RedesignMenus::menuIcon( "network", 20 ), longname, this,
                                    [this, name, longname]() { openSource( name, longname ); } );
                free( names[i] );
                free( longnames[i] );
            }
            free( names );
            free( longnames );
            free( categories );
        }
        if( !menu.isEmpty() )
            menu.addSeparator();
        menu.addAction( RedesignMenus::menuIcon( "add", 20 ), qtr( "Subscribe to a podcast..." ),
                        THEDP, &DialogsProvider::podcastConfigureDialog );
        menu.addSeparator();
    }
    menu.addAction( qtr( "Add file..." ), THEDP, [](){ THEDP->PLAppendDialog(); } );
    menu.addAction( qtr( "Add folder..." ), THEDP, &DialogsProvider::PLAppendDir );
    menu.addAction( qtr( "Open network stream..." ), THEDP, &DialogsProvider::openNetDialog );
    menu.addSeparator();
    menu.addAction( qtr( "New playlist..." ), THEDP, &DialogsProvider::newPlaylistDialog );
    menu.addAction( qtr( "Save playlist to file..." ), THEDP,
                    &DialogsProvider::savePlayingToPlaylist );
    menu.addAction( qtr( "Clear the playlist" ), this, [this]() {
        playlist_Clear( THEPL, pl_Unlocked );
    } );
    menu.exec( moreBtn->mapToGlobal( QPoint( moreBtn->width(), moreBtn->height() + 4 ) )
               - QPoint( menu.sizeHint().width(), 0 ) );
}

void SidebarPanel::libraryLocationChanged()
{
    QStringList segments;
    for( int i = 0; i < browser->location().size(); i++ )
        segments << browser->segmentTitle( i );
    setTitleCrumb( segments, 0 );
    backBtn->setEnabled( browser->canGoBack() );
    fwdBtn->setEnabled( browser->canGoForward() );
}

void SidebarPanel::libraryEntriesChanged()
{
    libGrid->setEntries( browser->entries() );
    updateCount();

    /* Components "S01": an accent dot on the sort glyph while the rows are
     * not in plain A-Z order (watched last, newest first...) */
    QCollator coll;
    coll.setNumericMode( true );
    coll.setCaseSensitivity( Qt::CaseInsensitive );
    QStringList titles;
    foreach( const LibraryEntry &e, browser->entries() )
        if( e.type == LibraryEntry::ITEM )
            titles << e.title;
    QStringList sorted = titles;
    std::sort( sorted.begin(), sorted.end(), [&coll]( const QString &a, const QString &b ) {
        return coll.compare( a, b ) < 0;
    } );
    const QString sec = browser->section();
    setSortDot( titles != sorted && sec != "home" && sec != "recent" );
}

void SidebarPanel::setSortDot( bool on )
{
    if( !on )
    {
        sortBtn->setIcon( Theme::icon( "sort", 24 ) );
        return;
    }
    QIcon icon;
    for( QIcon::Mode mode : { QIcon::Normal, QIcon::Active } )
    {
        const QColor fg = mode == QIcon::Normal ? Theme::colors().icon : Theme::colors().iconHover;
        QPixmap pm = Theme::pixmap( "sort", 24, fg );
        QPainter p( &pm );
        p.setRenderHint( QPainter::Antialiasing );
        p.setPen( Qt::NoPen );
        p.setBrush( Theme::accent() );
        p.drawEllipse( QRectF( 18.5, 1.5, 4, 4 ) );
        p.end();
        icon.addPixmap( pm, mode );
    }
    sortBtn->setIcon( icon );
}

/* Network rows: Podcasts and Radio are VLC 3 discovery sources */
void SidebarPanel::libraryAction( const QString &key )
{
    if( key == "podcast" )
        openSource( "podcast", qtr( "Podcasts" ) );
    else if( key == "radio" )
        openSource( "lua{sd='icecast'}", qtr( "Radio" ) );   /* share/lua/sd/icecast.lua */
}

void SidebarPanel::openSource( const QString &name, const QString &label )
{
    if( !plWidget )
        return;
    pages->setCurrentWidget( plPage );
    sortBtn->setEnabled( true );
    viewBtn->setEnabled( true );
    searchBtn->setEnabled( true );
    plWidget->searchField()->setVisible( true );
    libSearch->setVisible( false );
    history.clear();
    historyPos = -1;
    navigating = false;
    const QString longname = plWidget->sourceSelector()->selectSource( name );
    if( longname.isEmpty() )
        msg_Warn( p_intf, "sidebar: no discovery source %s", qtu( name ) );
    else if( name == "podcast" )
    {
        /* the selector lists podcast feeds in its own tree and never shows
         * the node: show it here, feeds as folders */
        playlist_Lock( THEPL );
        playlist_item_t *node = playlist_ChildSearchName( &THEPL->root,
                                                          vlc_gettext( qtu( longname ) ) );
        playlist_Unlock( THEPL );
        if( node )
            plWidget->mainView->setRootItem( node, false );
    }
    sdFromNetwork = name;
    setTitleCrumb( QStringList() << qtr( "Network" ) << label, 0 );
    backBtn->setEnabled( true );
    updateCount();
}
