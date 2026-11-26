/*****************************************************************************
 * queue_panel.cpp : Queue / Bookmarks / Chapters panel (P5d)
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

#include "components/queue_panel.hpp"
#include "util/qt_compat.hpp"
#include "components/playlist/playlist_model.hpp"
#include "components/redesign_menus.hpp"
#include "adapters/seekpoints.hpp"
#include "dialogs_provider.hpp"
#include "input_manager.hpp"
#include "styles/vlc_theme.hpp"
#include "util/bookmarks.hpp"
#include "library/playlist_store.hpp"

#include <vlc_playlist.h>
#include <vlc_input.h>

#include <QButtonGroup>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include "util/frame_animation.hpp"
#include <QVBoxLayout>

/* Components "Queue" (canvas 1.43): tabs 50, the header row centred 36
 * under them, 44px rows; handle 29, name 63, duration 25 from the right */
enum
{
    TAB_BAR_H   = 50,
    HEADER_H    = 66,    /* its row centred 36 under the tabs, rows from 66 */
    ROW_H       = 44,
    INSET       = 29,
    NAME_X      = 63,
    RIGHT_INSET = 25,
};

/* "2h 3m", "14m", "45s" */
static QString span( qint64 us )
{
    const qint64 s = us / CLOCK_FREQ;
    if( s >= 3600 )
        return QString( "%1h %2m" ).arg( s / 3600 ).arg( ( s / 60 ) % 60 );
    if( s >= 60 )
        return QString( "%1m" ).arg( s / 60 );
    return QString( "%1s" ).arg( s );
}

/**********************************************************************
 * Rows: drag handle, name, duration (the playing one in the accent)
 **********************************************************************/
class QueueDelegate : public QStyledItemDelegate
{
public:
    QueueDelegate( int hx, int nx, int ri, QObject *parent )
        : QStyledItemDelegate( parent ), handleX( hx ), nameX( nx ), rightInset( ri ) {}

    QSize sizeHint( const QStyleOptionViewItem &, const QModelIndex & ) const override
    {
        return QSize( 100, ROW_H );
    }

    void paint( QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index ) const override
    {
        p->save();
        p->setRenderHint( QPainter::Antialiasing );
        const QRect r = opt.rect;
        const bool hover = opt.state & QStyle::State_MouseOver;
        const bool selected = opt.state & QStyle::State_Selected;
        if( hover || selected )
        {
            p->setPen( Qt::NoPen );
            p->setBrush( selected ? Theme::colors().hover.lighter( 115 ) : Theme::colors().hover );
            p->drawRoundedRect( QRectF( r ).adjusted( 10, 2, -10, -2 ), 6, 6 );
        }
        const bool current = index.data( VLCModel::CURRENT_ITEM_ROLE ).toBool();
        const QColor fg = current ? Theme::accent()
                        : ( hover ? Theme::colors().text : Theme::colors().textMuted );

        /* Figma "move": two short lines */
        p->setPen( QPen( current ? Theme::accent() : Theme::colors().icon, 1.5, Qt::SolidLine, Qt::RoundCap ) );
        const qreal cy = r.center().y() + 0.5;
        p->drawLine( QPointF( r.left() + handleX, cy - 2.5 ), QPointF( r.left() + handleX + 10, cy - 2.5 ) );
        p->drawLine( QPointF( r.left() + handleX, cy + 2.5 ), QPointF( r.left() + handleX + 10, cy + 2.5 ) );

        QFont f = opt.font;
        f.setPixelSize( 14 );
        p->setFont( f );
        const QFontMetrics fm( f );
        const QModelIndex dur = index.sibling( index.row(), VLCModel::columnFromMeta( COLUMN_DURATION ) );
        QString d = dur.data( Qt::DisplayRole ).toString();
        if( d == "--:--" )
            d.clear();
        if( d.isEmpty() )
        {
            /* no length (a live channel): its group from the playlist,
             * like "News", in that column instead */
            const QModelIndex grp = index.sibling( index.row(), VLCModel::columnFromMeta( COLUMN_ALBUM ) );
            QString g = grp.data( Qt::DisplayRole ).toString();
            if( g.compare( "Undefined", Qt::CaseInsensitive ) == 0 )
                g.clear();          /* iptv-org's "no category" */
            d = fm.elidedText( g.replace( ';', ", " ), Qt::ElideRight, r.width() / 3 );
        }
        const int dw = d.isEmpty() ? 0 : QtCompat::textWidth( fm, d );
        p->setPen( current ? Theme::accent() : QColor( 0x6B, 0x6B, 0x6B ) );
        p->drawText( QRect( r.right() - rightInset - dw, r.top(), dw + 1, r.height() ),
                     Qt::AlignRight | Qt::AlignVCenter, d );
        p->setPen( fg );
        const QRect name( r.left() + nameX, r.top(), r.width() - nameX - rightInset - dw - 16, r.height() );
        p->drawText( name, Qt::AlignLeft | Qt::AlignVCenter,
                     fm.elidedText( index.data( Qt::DisplayRole ).toString(), Qt::ElideMiddle, name.width() ) );
        p->restore();
    }

private:
    int handleX, nameX, rightInset;
};

/**********************************************************************
 * QueueList
 **********************************************************************/
QueueList::QueueList( intf_thread_t *_p_intf, QWidget *parent, int handleX, int nameX, int rightInset )
    : QListView( parent ), p_intf( _p_intf )
{
    setObjectName( "QueueList" );
    playlist_Lock( THEPL );
    playlist_item_t *root = THEPL->p_playing;
    playlist_Unlock( THEPL );
    model = new PLModel( THEPL, p_intf, root, this );
    setFrameShape( QFrame::NoFrame );
    setModel( model );
    setModelColumn( VLCModel::columnFromMeta( COLUMN_TITLE ) );
    setItemDelegate( new QueueDelegate( handleX, nameX, rightInset, this ) );
    setUniformItemSizes( true );
    setMouseTracking( true );
    setSelectionMode( QAbstractItemView::ExtendedSelection );
    setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    setDragEnabled( true );
    setAcceptDrops( true );
    setDropIndicatorShown( true );
    setDragDropMode( QAbstractItemView::DragDrop );
    setDefaultDropAction( Qt::MoveAction );
    connect( this, &QListView::activated, this, [this]( const QModelIndex &i ) {
        model->activateItem( i );
    } );
    connect( model, &QAbstractItemModel::rowsInserted, this, &QueueList::changed );
    connect( model, &QAbstractItemModel::rowsRemoved, this, &QueueList::changed );
    connect( model, &QAbstractItemModel::modelReset, this, &QueueList::changed );
    connect( model, &QAbstractItemModel::dataChanged, this, &QueueList::changed );
    connect( model, &PLModel::currentIndexChanged, viewport(), QOverload<>::of( &QWidget::update ) );
}

void QueueList::removeSelected()
{
    model->doDelete( selectionModel()->selectedIndexes() );
}

/* files from outside the queue (the library, a file manager) */
bool QueueList::external( const QMimeData *m ) const
{
    return m->hasUrls() && !qobject_cast<const PlMimeData *>( m );
}

void QueueList::dragEnterEvent( QDragEnterEvent *e )
{
    if( external( e->mimeData() ) )
    {
        e->setDropAction( Qt::CopyAction );
        e->accept();
        return;
    }
    QListView::dragEnterEvent( e );
}

void QueueList::dragMoveEvent( QDragMoveEvent *e )
{
    if( external( e->mimeData() ) )
    {
        e->setDropAction( Qt::CopyAction );
        e->accept();
        return;
    }
    QListView::dragMoveEvent( e );
}

void QueueList::dropEvent( QDropEvent *e )
{
    if( !external( e->mimeData() ) )
    {
        QListView::dropEvent( e );
        return;
    }
    /* before the row under the pointer, after it in its lower half */
    const QModelIndex at = indexAt( e->pos() );
    int row = -1;
    if( at.isValid() )
        row = at.row() + ( e->pos().y() > visualRect( at ).center().y() ? 1 : 0 );
    playlist_Lock( THEPL );
    foreach( const QUrl &url, e->mimeData()->urls() )
    {
        input_item_t *in = input_item_New( qtu( url.toString( QUrl::FullyEncoded ) ), NULL );
        if( !in )
            continue;
        playlist_NodeAddInput( THEPL, in, THEPL->p_playing, row < 0 ? PLAYLIST_END : row++ );
        input_item_Release( in );
    }
    playlist_Unlock( THEPL );
    e->setDropAction( Qt::CopyAction );
    e->accept();
}

void QueueList::keyPressEvent( QKeyEvent *e )
{
    if( e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace )
    {
        removeSelected();
        return;
    }
    QListView::keyPressEvent( e );
}

/**********************************************************************
 * Panel
 **********************************************************************/
QueuePanel::QueuePanel( intf_thread_t *_p_intf, QWidget *parent )
    : QWidget( parent ), p_intf( _p_intf )
{
    setObjectName( "VLCQueue" );
    setAttribute( Qt::WA_StyledBackground, true );
    setFocusPolicy( Qt::ClickFocus );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    /* the adjustment panel's tab strip (P3) */
    tabBar = new QWidget( this );
    tabBar->setObjectName( "QueueTabs" );
    tabBar->setAttribute( Qt::WA_StyledBackground, true );
    tabBar->setFixedHeight( TAB_BAR_H );
    QHBoxLayout *tl = new QHBoxLayout( tabBar );
    tl->setContentsMargins( 13, 0, 13, 0 );   /* Figma: three equal tabs */
    tl->setSpacing( 2 );
    tabs = new QButtonGroup( this );
    tabs->setExclusive( true );
    const char *icons[] = { "queue", "bookmarks", "chapters" };
    const QString names[] = { qtr( "Queue" ), qtr( "Bookmarks" ), qtr( "Chapters" ) };
    for( int i = 0; i < TAB_COUNT; i++ )
    {
        QToolButton *b = new QToolButton( tabBar );
        b->setCheckable( true );
        b->setText( names[i] );
        b->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
        b->setIconSize( QSize( 18, 18 ) );
        QIcon ic;
        ic.addPixmap( Theme::pixmap( icons[i], 18, QColor( 0x40, 0x40, 0x40 ) ), QIcon::Normal, QIcon::Off );
        ic.addPixmap( Theme::pixmap( icons[i], 18, Theme::accent() ), QIcon::Normal, QIcon::On );
        ic.addPixmap( Theme::pixmap( icons[i], 18, Theme::colors().textMuted ), QIcon::Active, QIcon::Off );
        ic.addPixmap( Theme::pixmap( icons[i], 18, Theme::accent() ), QIcon::Active, QIcon::On );
        b->setIcon( ic );
        b->setFixedHeight( TAB_BAR_H );
        QFont f = b->font();
        f.setPixelSize( 14 );
        b->setFont( f );
        b->setCursor( Qt::PointingHandCursor );
        b->setFocusPolicy( Qt::TabFocus );
        tabs->addButton( b, i );
        /* Figma: three equal tabs across the panel */
        tl->addWidget( b, 1 );
    }
    underline = new QWidget( tabBar );
    underline->setObjectName( "QueueTabLine" );
    underline->setAttribute( Qt::WA_StyledBackground, true );
    underlineAnim = new FrameAnimation( this );
    underlineAnim->setDuration( 200 );
    underlineAnim->setEasingCurve( QEasingCurve::OutCubic );
    connect( underlineAnim, &FrameAnimation::valueChanged, this, [this]( const QVariant &v ) {
        underline->setGeometry( v.toRect() );
    } );
    tabBar->installEventFilter( this );
    layout->addWidget( tabBar );

    stack = new QStackedWidget( this );
    stack->addWidget( buildQueuePage() );
    stack->addWidget( buildBookmarksPage() );
    stack->addWidget( buildChaptersPage() );
    layout->addWidget( stack, 1 );

    connect( tabs, QOverload<int>::of( &QButtonGroup::buttonClicked ), this,
             [this]( int id ) { setCurrentTab( id ); } );
    setCurrentTab( qBound( 0, getSettings()->value( "Queue/tab", QUEUE_TAB ).toInt(), TAB_COUNT - 1 ), false );

    connect( THEMIM, &MainInputManager::inputChanged, this, [this]() {
        refreshBookmarks();
        refreshChapters();
        queueView->viewport()->update();
    } );
    connect( THEMIM, &MainInputManager::randomChanged, this, [this]() { updateModes(); } );
    connect( THEMIM, &MainInputManager::repeatLoopChanged, this, [this]() { updateModes(); } );
    updateModes();
}

QToolButton *QueuePanel::headerButton( QWidget *parent, const QString &icon, const QString &tip )
{
    QToolButton *b = new QToolButton( parent );
    Theme::setupIconButton( b, icon, 32, 24 );
    b->setToolTip( tip );
    return b;
}

/* "4 items - 2h 3m" on the left, the tab's buttons on the right */
QWidget *QueuePanel::headerRow( QLabel **count )
{
    QWidget *row = new QWidget;
    row->setFixedHeight( HEADER_H );
    QHBoxLayout *h = new QHBoxLayout( row );
    h->setContentsMargins( INSET + 1, 6, 16, 0 );
    h->setSpacing( 7 );
    *count = new QLabel( row );
    (*count)->setObjectName( "QueueCount" );
    h->addWidget( *count, 1 );
    return row;
}

QWidget *QueuePanel::buildQueuePage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *v = new QVBoxLayout( page );
    v->setContentsMargins( 0, 0, 0, 0 );
    v->setSpacing( 0 );

    QWidget *header = headerRow( &queueCount );
    QHBoxLayout *h = static_cast<QHBoxLayout *>( header->layout() );
    searchBtn = headerButton( header, "search", qtr( "Search the queue (Ctrl+F)" ) );
    searchBtn->setCheckable( true );
    QToolButton *saveBtn = headerButton( header, "save", qtr( "Save the queue as a playlist" ) );
    sortBtn = headerButton( header, "sort", qtr( "Sort" ) );
    repeatBtn = headerButton( header, "repeat", qtr( "Repeat" ) );
    shuffleBtn = headerButton( header, "shuffle", qtr( "Shuffle" ) );
    QToolButton *removeBtn = headerButton( header, "remove", qtr( "Remove from the queue" ) );
    QToolButton *addBtn = headerButton( header, "add", qtr( "Add to the queue" ) );
    repeatBtn->setCheckable( true );
    shuffleBtn->setCheckable( true );
    foreach( QToolButton *b, QList<QToolButton *>() << searchBtn << saveBtn << sortBtn << repeatBtn
                                                     << shuffleBtn << removeBtn << addBtn )
        h->addWidget( b );
    v->addWidget( header );

    searchRow = new QWidget( page );
    QHBoxLayout *sr = new QHBoxLayout( searchRow );
    sr->setContentsMargins( INSET - 9, 0, RIGHT_INSET - 9, 8 );
    searchEdit = new QLineEdit( searchRow );
    searchEdit->setObjectName( "QueueSearch" );
    searchEdit->setPlaceholderText( qtr( "Search the queue" ) );
    searchEdit->setClearButtonEnabled( true );
    searchEdit->setFixedHeight( 32 );
    searchEdit->addAction( QIcon( Theme::pixmap( "search", 16, Theme::colors().icon ) ),
                           QLineEdit::LeadingPosition );
    searchEdit->installEventFilter( this );
    sr->addWidget( searchEdit );
    searchRow->hide();
    v->addWidget( searchRow );
    searchTimer = new QTimer( this );
    searchTimer->setSingleShot( true );
    searchTimer->setInterval( 250 );    /* thousands of channels: not on every key */
    connect( searchTimer, &QTimer::timeout, this, &QueuePanel::applySearch );
    connect( searchEdit, &QLineEdit::textChanged, searchTimer, QOverload<>::of( &QTimer::start ) );
    connect( searchBtn, &QToolButton::clicked, this, [this]( bool on ) { setSearchVisible( on ); } );
    connect( saveBtn, &QToolButton::clicked, this, &QueuePanel::saveQueue );

    queueView = new QueueList( p_intf, page, INSET, NAME_X, RIGHT_INSET );
    model = queueView->plModel();
    v->addWidget( queueView, 1 );

    connect( removeBtn, &QToolButton::clicked, queueView, &QueueList::removeSelected );
    connect( addBtn, &QToolButton::clicked, this, [this]() { THEDP->PLAppendDialog(); } );
    connect( sortBtn, &QToolButton::clicked, this, &QueuePanel::showSortMenu );
    connect( shuffleBtn, &QToolButton::clicked, this, [this]() { THEMIM->toggleRandom(); } );
    connect( repeatBtn, &QToolButton::clicked, this, [this]() { THEMIM->loopRepeatLoopStatus(); } );

    connect( queueView, &QueueList::changed, this, &QueuePanel::updateQueueHeader );
    updateQueueHeader();
    return page;
}

/* the queue's count and length: every item under VLC's playing node */
static void sumItems( playlist_item_t *node, int &count, int &timed, qint64 &length )
{
    for( int i = 0; i < node->i_children; i++ )
    {
        playlist_item_t *c = node->pp_children[i];
        if( c->i_children >= 0 )
            sumItems( c, count, timed, length );
        else
        {
            count++;
            const qint64 d = input_item_GetDuration( c->p_input );
            if( d > 0 )
            {
                length += d;
                timed++;
            }
        }
    }
}

QString QueueList::summary( intf_thread_t *p_intf, int shown )
{
    int count = 0, timed = 0;
    qint64 length = 0;
    playlist_Lock( THEPL );
    sumItems( THEPL->p_playing, count, timed, length );
    playlist_Unlock( THEPL );
    const QLocale loc;
    if( shown >= 0 )
        return qtr( "%1 of %2" ).arg( loc.toString( shown ), loc.toString( count ) );
    QString text = count == 1 ? qtr( "1 item" ) : qtr( "%1 items" ).arg( loc.toString( count ) );
    /* a total only means something when (nearly) every item has a length:
     * live channels have none, and a channel's few seconds of buffered
     * stream are not its length */
    if( length > 0 && timed * 10 >= count * 9 )
        text += " - " + span( length );
    return text;
}

void QueuePanel::updateQueueHeader()
{
    const bool searching = searchEdit && !searchEdit->text().trimmed().isEmpty();
    queueCount->setText( QueueList::summary( p_intf, searching ? model->rowCount() : -1 ) );
}

void QueuePanel::setSearchVisible( bool b )
{
    searchBtn->setChecked( b );
    searchRow->setVisible( b );
    if( b )
    {
        searchEdit->setFocus( Qt::ShortcutFocusReason );
        searchEdit->selectAll();
    }
    else if( !searchEdit->text().isEmpty() )
    {
        /* hiding the row ends the search: the whole queue comes back */
        searchEdit->clear();
        searchTimer->stop();
        applySearch();
    }
}

void QueuePanel::applySearch()
{
    /* VLC's own live search: matches title, artist and album (an IPTV
     * channel's group), and hides the rest of the queue */
    model->filter( searchEdit->text().trimmed(), QModelIndex(), true );
    updateQueueHeader();
    const QModelIndex cur = model->currentIndex();
    if( cur.isValid() )
        queueView->scrollTo( cur.sibling( cur.row(), queueView->modelColumn() ) );
}

void QueuePanel::saveQueue()
{
    SavedPlaylist p = PlaylistStore::fromQueue( p_intf );
    if( p.uris.isEmpty() )
        return;
    bool ok = false;
    const QString name = QInputDialog::getText( this, qtr( "Save as Playlist" ), qtr( "Name" ),
                                                QLineEdit::Normal, QString(), &ok ).trimmed();
    if( !ok || name.isEmpty() )
        return;
    p.title = name;
    if( PlaylistStore::instance( p_intf )->create( p ).isEmpty() )
        msg_Warn( p_intf, "could not save the queue as playlist %s", qtu( name ) );
}

void QueuePanel::updateModes()
{
    const bool random = var_GetBool( THEPL, "random" );
    const bool loop = var_GetBool( THEPL, "loop" );
    const bool repeat = var_GetBool( THEPL, "repeat" );
    shuffleBtn->setChecked( random );
    repeatBtn->setChecked( loop || repeat );
    repeatBtn->setIcon( Theme::icon( repeat ? "repeat-one" : "repeat", 24 ) );
    repeatBtn->setToolTip( repeat ? qtr( "Repeat one" ) : loop ? qtr( "Repeat all" ) : qtr( "Repeat" ) );
}

void QueuePanel::showSortMenu()
{
    queueView->showSortMenu( sortBtn->mapToGlobal( QPoint( 0, sortBtn->height() + 4 ) ) );
}

void QueueList::showSortMenu( const QPoint &globalPos )
{
    struct { int meta; Qt::SortOrder order; const char *label; } keys[] = {
        { COLUMN_TITLE, Qt::AscendingOrder, N_( "Title A–Z" ) },
        { COLUMN_TITLE, Qt::DescendingOrder, N_( "Title Z–A" ) },
        { COLUMN_ARTIST, Qt::AscendingOrder, N_( "Artist A–Z" ) },
        { COLUMN_ALBUM, Qt::AscendingOrder, N_( "Album A–Z" ) },
        { COLUMN_DURATION, Qt::AscendingOrder, N_( "Shortest First" ) },
        { COLUMN_DURATION, Qt::DescendingOrder, N_( "Longest First" ) },
        { COLUMN_URI, Qt::AscendingOrder, N_( "URI Ascending" ) },
    };
    QMenu menu( this );
    menu.setObjectName( "VLCSortMenu" );
    for( const auto &k : keys )
    {
        const int column = VLCModel::columnFromMeta( k.meta );
        const Qt::SortOrder order = k.order;
        menu.addAction( RedesignMenus::menuIcon( "blank", 20 ), qtr( k.label ), this, [this, column, order]() {
            model->sort( column, order );
        } );
    }
    menu.exec( globalPos );
}

/**********************************************************************
 * Bookmarks: VLC's bookmarks of the input (P7 adds quick bookmarks)
 **********************************************************************/
/* a name and its time, in the queue rows' geometry */
/* measured (canvas ×1.43): bookmarks name 20, time 27 from the right;
 * chapters number 24 (accent), name 69, time 27 from the right */
class TimedRowDelegate : public QStyledItemDelegate
{
public:
    TimedRowDelegate( QObject *parent, int nameX, bool numbered )
        : QStyledItemDelegate( parent ), nameX( nameX ), numbered( numbered ) {}
    QSize sizeHint( const QStyleOptionViewItem &, const QModelIndex & ) const override
    {
        return QSize( 100, ROW_H );
    }
    void paint( QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index ) const override
    {
        p->save();
        p->setRenderHint( QPainter::Antialiasing );
        const QRect r = opt.rect;
        const bool hover = opt.state & QStyle::State_MouseOver;
        if( hover )
        {
            p->setPen( Qt::NoPen );
            p->setBrush( Theme::colors().hover );
            p->drawRoundedRect( QRectF( r ).adjusted( 10, 2, -10, -2 ), 6, 6 );
        }
        QFont f = opt.font;
        f.setPixelSize( 14 );
        p->setFont( f );
        const QFontMetrics fm( f );
        const QString t = index.data( Qt::UserRole ).toString();
        const int tw = QtCompat::textWidth( fm, t );
        p->setPen( QColor( 0x6B, 0x6B, 0x6B ) );
        p->drawText( QRect( r.right() - TIME_RIGHT - tw, r.top(), tw + 1, r.height() ),
                     Qt::AlignRight | Qt::AlignVCenter, t );
        if( numbered )
        {
            p->setPen( Theme::accent() );
            p->drawText( QRect( r.left() + NUMBER_X, r.top(), nameX - NUMBER_X, r.height() ),
                         Qt::AlignLeft | Qt::AlignVCenter, QString::number( index.row() + 1 ) );
        }
        p->setPen( hover ? Theme::colors().text : Theme::colors().textMuted );
        const QRect name( r.left() + nameX, r.top(), r.width() - nameX - TIME_RIGHT - tw - 16, r.height() );
        p->drawText( name, Qt::AlignLeft | Qt::AlignVCenter,
                     fm.elidedText( index.data( Qt::DisplayRole ).toString(), Qt::ElideRight, name.width() ) );
        p->restore();
    }
private:
    enum { NUMBER_X = 24, TIME_RIGHT = 26 };
    int nameX;
    bool numbered;
};

static QListWidget *panelList( QWidget *parent, int nameX, bool numbered )
{
    QListWidget *l = new QListWidget( parent );
    l->setObjectName( "QueueSimpleList" );
    l->setFrameShape( QFrame::NoFrame );
    l->setMouseTracking( true );
    l->setItemDelegate( new TimedRowDelegate( l, nameX, numbered ) );
    return l;
}

QWidget *QueuePanel::buildBookmarksPage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *v = new QVBoxLayout( page );
    v->setContentsMargins( 0, 0, 0, 0 );
    v->setSpacing( 0 );
    QWidget *header = headerRow( &bookmarkCount );
    /* Figma draws this header 2px higher and the rows 5px higher than the
     * queue's: count centred 34 under the tabs, first row centred 83 */
    header->setFixedHeight( 59 );
    QHBoxLayout *h = static_cast<QHBoxLayout *>( header->layout() );
    h->setContentsMargins( INSET + 1, 6, 16, 0 );
    QToolButton *removeBtn = headerButton( header, "remove", qtr( "Remove the bookmark" ) );
    QToolButton *addBtn = headerButton( header, "add", qtr( "Bookmark this moment" ) );
    h->addWidget( removeBtn );
    h->addWidget( addBtn );
    v->addWidget( header );
    bookmarkList = panelList( page, 20, false );
    v->addWidget( bookmarkList, 1 );

    connect( addBtn, &QToolButton::clicked, this, [this]() {
        /* named like the quick bookmark (Ctrl+B): "<Title> <n>" */
        Bookmarks::add( p_intf, false );
        refreshBookmarks();
    } );
    connect( removeBtn, &QToolButton::clicked, this, [this]() {
        input_thread_t *input = THEMIM->getInput();
        const int row = bookmarkList->currentRow();
        if( input && row >= 0 )
            input_Control( input, INPUT_DEL_BOOKMARK, row );
        refreshBookmarks();
    } );
    connect( bookmarkList, &QListWidget::itemClicked, this, [this]( QListWidgetItem *it ) {
        input_thread_t *input = THEMIM->getInput();
        if( input )
            input_Control( input, INPUT_SET_BOOKMARK, bookmarkList->row( it ) );
    } );
    refreshBookmarks();
    return page;
}

void QueuePanel::refreshBookmarks()
{
    bookmarkList->clear();
    input_thread_t *input = THEMIM->getInput();
    seekpoint_t **marks = NULL;
    int n = 0;
    if( input && input_Control( input, INPUT_GET_BOOKMARKS, &marks, &n ) == VLC_SUCCESS )
    {
        for( int i = 0; i < n; i++ )
        {
            QListWidgetItem *it = new QListWidgetItem( bookmarkList );
            it->setText( qfu( marks[i]->psz_name ) );
            /* Figma: 00:00:42:023 */
            const qint64 ms = marks[i]->i_time_offset / 1000;
            it->setData( Qt::UserRole, QString::asprintf( "%02lld:%02lld:%02lld:%03lld",
                         ms / 3600000, ( ms / 60000 ) % 60, ( ms / 1000 ) % 60, ms % 1000 ) );
            vlc_seekpoint_Delete( marks[i] );
        }
        free( marks );
    }
    bookmarkCount->setText( n == 1 ? qtr( "1 Bookmark" ) : qtr( "%1 Bookmarks" ).arg( n ) );
}

/**********************************************************************
 * Chapters: the input's chapters (P7 draws them on the timeline)
 **********************************************************************/
QWidget *QueuePanel::buildChaptersPage()
{
    QWidget *page = new QWidget;
    QVBoxLayout *v = new QVBoxLayout( page );
    v->setContentsMargins( 0, 0, 0, 0 );
    v->setSpacing( 0 );
    /* Figma: no count row, the numbered list starts under the tabs (first
     * row centred 38 below them) */
    QWidget *chapterHeader = headerRow( &chapterCount );
    chapterHeader->setParent( page );
    chapterHeader->hide();
    v->addSpacing( 38 - ROW_H / 2 );
    chapterList = panelList( page, 69, true );
    v->addWidget( chapterList, 1 );
    chapters = new SeekPoints( this, p_intf );
    connect( THEMIM->getIM(), &InputManager::chapterChanged, this, [this]() {
        chapters->update();
        refreshChapters();
    } );
    connect( chapterList, &QListWidget::itemClicked, this, [this]( QListWidgetItem *it ) {
        chapters->jumpTo( chapterList->row( it ) );
    } );
    refreshChapters();
    return page;
}

void QueuePanel::refreshChapters()
{
    chapterList->clear();
    chapters->update();
    const QList<SeekPoint> points = chapters->getPoints();
    for( int i = 0; i < points.size(); i++ )
    {
        QListWidgetItem *it = new QListWidgetItem( chapterList );
        it->setText( points[i].name.isEmpty() ? qtr( "Chapter %1" ).arg( i + 1 ) : points[i].name );
        const qint64 sec = points[i].time / CLOCK_FREQ;   /* Figma: 00:02:15 */
        it->setData( Qt::UserRole, QString::asprintf( "%02lld:%02lld:%02lld",
                     sec / 3600, ( sec / 60 ) % 60, sec % 60 ) );
    }
    chapterCount->setText( points.size() == 1 ? qtr( "1 chapter" )
                                              : qtr( "%1 chapters" ).arg( points.size() ) );
}

/**********************************************************************
 * Tabs
 **********************************************************************/
void QueuePanel::setCurrentTab( int id, bool animate )
{
    if( QAbstractButton *b = tabs->button( id ) )
        b->setChecked( true );
    stack->setCurrentIndex( id );
    getSettings()->setValue( "Queue/tab", id );
    placeUnderline( animate );
}

void QueuePanel::placeUnderline( bool animate )
{
    QAbstractButton *b = tabs->checkedButton();
    if( !b )
        return;
    /* Figma: the line spans the tab's glyph and name, 15px past each side */
    const int w = qMin( b->width(), b->sizeHint().width() + 36 );
    const QRect to( b->x() + ( b->width() - w ) / 2, TAB_BAR_H - 2, w, 2 );
    underlineAnim->stop();
    if( !animate || !underline->isVisible() || underline->width() == 0 )
    {
        underline->setGeometry( to );
        return;
    }
    underlineAnim->setStartValue( underline->geometry() );
    underlineAnim->setEndValue( to );
    underlineAnim->start();
}

bool QueuePanel::eventFilter( QObject *o, QEvent *e )
{
    if( o == tabBar && ( e->type() == QEvent::Resize || e->type() == QEvent::Show ) )
        placeUnderline( false );
    if( o == searchEdit && e->type() == QEvent::KeyPress )
    {
        QKeyEvent *k = static_cast<QKeyEvent *>( e );
        switch( k->key() )
        {
        case Qt::Key_Escape:            /* clears, then closes the search */
            if( !searchEdit->text().isEmpty() )
                searchEdit->clear();
            else
            {
                setSearchVisible( false );
                queueView->setFocus();
            }
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:             /* plays the first match */
            searchTimer->stop();
            applySearch();
            if( model->rowCount() > 0 )
            {
                const QModelIndex first = static_cast<QAbstractItemModel *>( model )->index( 0, queueView->modelColumn() );
                model->activateItem( first );
            }
            return true;
        case Qt::Key_Down:              /* into the results */
            if( model->rowCount() > 0 )
            {
                queueView->setFocus();
                queueView->setCurrentIndex( static_cast<QAbstractItemModel *>( model )->index( 0, queueView->modelColumn() ) );
            }
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter( o, e );
}

/* Ctrl+F is also VLC's Open Folder: inside the panel it is the search */
bool QueuePanel::event( QEvent *e )
{
    if( e->type() == QEvent::ShortcutOverride
     && static_cast<QKeyEvent *>( e )->matches( QKeySequence::Find ) )
    {
        e->accept();
        return true;
    }
    return QWidget::event( e );
}

void QueuePanel::keyPressEvent( QKeyEvent *e )
{
    if( e->key() == Qt::Key_Escape )
    {
        emit closeRequested();
        return;
    }
    if( e->key() == Qt::Key_Tab && ( e->modifiers() & Qt::ControlModifier ) )
    {
        setCurrentTab( ( stack->currentIndex() + 1 ) % TAB_COUNT );
        return;
    }
    if( e->matches( QKeySequence::Find ) )
    {
        setCurrentTab( QUEUE_TAB );
        setSearchVisible( true );
        return;
    }
    QWidget::keyPressEvent( e );
}

/**********************************************************************
 * Dock
 **********************************************************************/
QueueDock::QueueDock( intf_thread_t *p_intf, QWidget *parent )
    : SlideDock( parent )
{
    setObjectName( "VLCQueueDock" );
    p = new QueuePanel( p_intf, this );
    setPanel( p, QueuePanel::PANEL_WIDTH );
}
