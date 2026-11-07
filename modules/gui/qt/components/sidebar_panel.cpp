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
#include "dialogs_provider.hpp"
#include "recents.hpp"

#include <vlc_playlist.h>

#include <QFileInfo>
#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QHBoxLayout>
#include <QLabel>
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
    : QWidget( parent ), p_intf( _p_intf ), plWidget( NULL ),
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
    title = new QLabel( this );
    title->setObjectName( "SidebarTitle" );
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

    /* Pages: browsing view, recents */
    pages = new QStackedWidget( this );
    plPage = new QWidget( pages );
    plLayout = new QVBoxLayout( plPage );
    plLayout->setContentsMargins( 0, 0, 0, 0 );
    pages->addWidget( plPage );

    recents = new QListWidget( pages );
    recents->setObjectName( "RecentsList" );
    recents->setFrameShape( QFrame::NoFrame );
    recents->setIconSize( QSize( 18, 18 ) );
    pages->addWidget( recents );
    layout->addWidget( pages, 1 );

    connect( backBtn, &QToolButton::clicked, this, &SidebarPanel::goBack );
    connect( fwdBtn, &QToolButton::clicked, this, &SidebarPanel::goForward );
    connect( searchBtn, &QToolButton::toggled, this, &SidebarPanel::toggleSearch );
    connect( sortBtn, &QToolButton::clicked, this, &SidebarPanel::showSortMenu );
    connect( moreBtn, &QToolButton::clicked, this, &SidebarPanel::showMoreMenu );
    connect( popBtn, &QToolButton::clicked, this, &SidebarPanel::popOutRequested );
    connect( recents, &QListWidget::itemActivated, this, [this]( QListWidgetItem *item ) {
        Open::openMRL( p_intf, item->data( Qt::UserRole ).toString(), true, true );
    } );

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
        tree->setFrameShape( QFrame::NoFrame );
        tree->setMouseTracking( true );
        tree->viewport()->setAttribute( Qt::WA_Hover );
        tree->setItemDelegate( new SidebarItemDelegate( tree ) );
    }
    connect( view, &StandardPLPanel::viewChanged, this, &SidebarPanel::rootChanged );
    connect( viewBtn, &QToolButton::clicked, view, &StandardPLPanel::cycleViews );

    QAbstractItemModel *model = PLModel::getPLModel( p_intf );
    connect( model, &QAbstractItemModel::rowsInserted, this, &SidebarPanel::updateCount );
    connect( model, &QAbstractItemModel::rowsRemoved, this, &SidebarPanel::updateCount );
    connect( model, &QAbstractItemModel::modelReset, this, &SidebarPanel::updateCount );
    connect( model, &QAbstractItemModel::layoutChanged, this, &SidebarPanel::updateCount );
    updateCount();
}

void SidebarPanel::showSection( const QString &key )
{
    section = key;
    title->setText( NavRail::titleFor( key ) );

    if( key == "recents" )
    {
        refreshRecents();
        pages->setCurrentWidget( recents );
        sortBtn->setEnabled( false );
        viewBtn->setEnabled( false );
        searchBtn->setEnabled( false );
        backBtn->setEnabled( false );
        fwdBtn->setEnabled( false );
        return;
    }

    pages->setCurrentWidget( plPage );
    sortBtn->setEnabled( true );
    viewBtn->setEnabled( true );
    searchBtn->setEnabled( true );
    if( !plWidget )
        return;

    /* Rail section -> existing service discovery / playlist node */
    QStringList candidates;
    if( key == "home" )            candidates << "library" << "playlist";
    else if( key == "videos" )     candidates << "video_dir";
    else if( key == "music" )      candidates << "audio_dir";
    else if( key == "network" )    candidates << "upnp" << "sap" << "podcast";
    else                           candidates << "playlist";

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
    backBtn->setEnabled( historyPos > 0 );
    fwdBtn->setEnabled( historyPos >= 0 && historyPos < history.size() - 1 );
    updateCount();
}

void SidebarPanel::goBack()
{
    if( historyPos <= 0 || !plWidget )
        return;
    navigating = true;
    historyPos--;
    plWidget->mainView->browseInto( history.at( historyPos ) );
}

void SidebarPanel::goForward()
{
    if( historyPos >= history.size() - 1 || !plWidget )
        return;
    navigating = true;
    historyPos++;
    plWidget->mainView->browseInto( history.at( historyPos ) );
}

void SidebarPanel::updateCount()
{
    if( section == "recents" )
    {
        const int r = recents->count();
        count->setText( r == 1 ? qtr( "1 item" ) : qtr( "%1 items" ).arg( r ) );
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
    if( plWidget )
    {
        if( on )
            plWidget->setSearchFieldFocus();
        else
            plWidget->searchField()->clear();
    }
}

void SidebarPanel::showSortMenu()
{
    static const struct { int meta; const char *name; } keys[] = {
        { COLUMN_TITLE,    N_("Title") },
        { COLUMN_DURATION, N_("Duration") },
        { COLUMN_ARTIST,   N_("Artist") },
        { COLUMN_ALBUM,    N_("Album") },
        { COLUMN_URI,      N_("File name") },
        { COLUMN_DATE,     N_("Date") },
    };
    QMenu menu( this );
    for( size_t i = 0; i < sizeof( keys ) / sizeof( keys[0] ); i++ )
    {
        const int column = VLCModel::columnFromMeta( keys[i].meta );
        menu.addAction( qtr( keys[i].name ), this, [this, column]() {
            PLModel::getPLModel( p_intf )->sort( column, sortOrder );
        } );
    }
    menu.addSeparator();
    QAction *asc = menu.addAction( qtr( "Ascending" ), this, [this]() {
        sortOrder = Qt::AscendingOrder; } );
    QAction *desc = menu.addAction( qtr( "Descending" ), this, [this]() {
        sortOrder = Qt::DescendingOrder; } );
    asc->setCheckable( true );
    desc->setCheckable( true );
    asc->setChecked( sortOrder == Qt::AscendingOrder );
    desc->setChecked( sortOrder == Qt::DescendingOrder );
    menu.exec( sortBtn->mapToGlobal( QPoint( 0, sortBtn->height() + 4 ) ) );
}

void SidebarPanel::showMoreMenu()
{
    QMenu menu( this );
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

void SidebarPanel::refreshRecents()
{
    recents->clear();
    const QStringList list = RecentsMRL::getInstance( p_intf )->recentList();
    foreach( const QString &mrl, list )
    {
        const QUrl url( mrl );
        QString name = url.isLocalFile() ? QFileInfo( url.toLocalFile() ).fileName()
                                         : url.toDisplayString();
        if( name.isEmpty() )
            name = mrl;
        QListWidgetItem *item = new QListWidgetItem( name, recents );
        item->setIcon( Theme::icon( url.isLocalFile() ? "video" : "network", 18 ) );
        item->setToolTip( url.toDisplayString() );
        item->setData( Qt::UserRole, mrl );
    }
    updateCount();
}
