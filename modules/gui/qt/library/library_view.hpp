/*****************************************************************************
 * library_view.hpp : library window state, browse view (P5a)
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

#ifndef VLC_QT_LIBRARY_VIEW_HPP_
#define VLC_QT_LIBRARY_VIEW_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QAbstractScrollArea>
#include <QHash>
#include <QPixmap>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <QTimer>

class QHBoxLayout;
class QLabel;
class QAbstractButton;
class QLineEdit;
class QToolButton;
class MediaLibrary;

/* One tile or row: a library item, or a group (a show, a season, the
 * "Shows" folder) that opens a deeper level. */
struct LibraryEntry
{
    enum Type { ITEM, GROUP };
    Type type = ITEM;
    QString title;
    QString segment;        /* GROUP: the location segment it opens */
    int index = -1;         /* ITEM: index in MediaLibrary::items() */
    QString thumbFrom;      /* the item whose thumbnail stands for it */
    int count = 0;          /* GROUP: items inside */
    bool watched = false;
    qreal progress = 0.0;
    qint64 date = 0;        /* newest file date, for sorting */
};

/* Grid of 16:9 tiles or four columns of rows, painted directly (a few
 * hundred visible entries, exact Figma geometry, no per-tile widgets). */
class LibraryGrid : public QAbstractScrollArea
{
    Q_OBJECT
public:
    enum Mode { GRID, LIST };
    LibraryGrid( intf_thread_t *, QWidget *parent );

    void setEntries( const QVector<LibraryEntry> & );
    void setMode( Mode );
    Mode mode() const { return viewMode; }
    void setPlayingPath( const QString & );
    void refreshThumbnail( const QString &path );

signals:
    void activated( int entry );
    void contextMenu( int entry, const QPoint &globalPos );

protected:
    void paintEvent( QPaintEvent * ) override;
    void resizeEvent( QResizeEvent * ) override;
    void mouseMoveEvent( QMouseEvent * ) override;
    void mousePressEvent( QMouseEvent * ) override;
    void mouseReleaseEvent( QMouseEvent * ) override;
    void leaveEvent( QEvent * ) override;
    void contextMenuEvent( QContextMenuEvent * ) override;
    void scrollContentsBy( int dx, int dy ) override;

private:
    void relayout();
    QRect entryRect( int i ) const;   /* in content coordinates */
    int entryAt( const QPoint &viewportPos ) const;
    QPixmap thumbnail( const LibraryEntry &, const QSize & );
    void requestVisibleThumbnails();

    intf_thread_t *p_intf;
    MediaLibrary *lib;
    QVector<LibraryEntry> entries;
    Mode viewMode = GRID;
    int columns = 1;
    QSize cell;           /* tile, or row */
    int hovered = -1, pressed = -1;
    QString playingPath;
    QHash<QString, QPixmap> pixmaps;   /* thumbnail file -> scaled pixmap */
    QTimer indicatorTimer;
};

/* The library page: labelled nav on the left, then the toolbar (back,
 * forward, breadcrumb, count, sort, view, search, +, more) over the grid.
 * Figma row 2 #3-6. */
class LibraryView : public QWidget
{
    Q_OBJECT
public:
    LibraryView( intf_thread_t *, QWidget *parent );

    QString section() const { return location.value( 0 ); }

public slots:
    void openSection( const QString &key );   /* "videos", "music", "recent" */

signals:
    /* a nav entry the library does not show itself (network, playlists) */
    void playerSectionRequested( const QString &key );
    void closeRequested();
    /* something was started from the library */
    void played();

protected:
    void keyPressEvent( QKeyEvent * ) override;

private:
    QWidget *buildNav();
    QWidget *buildToolbar();
    void setLocation( const QStringList &, bool record = true );
    void rebuild();
    void rebuildCrumb();
    QVector<LibraryEntry> collect() const;
    void sortEntries( QVector<LibraryEntry> & ) const;
    void activate( int entry );
    void showEntryMenu( int entry, const QPoint & );
    void showSortMenu();
    void play( const QStringList &paths );

    intf_thread_t *p_intf;
    MediaLibrary *lib;
    QHash<QString, QAbstractButton *> navButtons;
    QToolButton *backBtn, *fwdBtn, *sortBtn, *viewBtn;
    QWidget *crumb;
    QHBoxLayout *crumbLayout;
    QLabel *countLabel;
    QLineEdit *search;
    LibraryGrid *grid;

    QStringList location;
    QList<QStringList> backStack, fwdStack;
    QVector<LibraryEntry> current;
    int sortKey;
};

#endif
