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
#include "library/library_browser.hpp"

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
class MiniPlayer;

/* Grid of 16:9 tiles or four columns of rows, painted directly (a few
 * hundred visible entries, exact Figma geometry, no per-tile widgets).
 * Compact: the player sidebar's one-column rows (Components panels). */
class LibraryGrid : public QAbstractScrollArea
{
    Q_OBJECT
public:
    enum Mode { GRID, LIST };
    LibraryGrid( intf_thread_t *, QWidget *parent );

    void setEntries( const QVector<LibraryEntry> & );
    void setMode( Mode );
    Mode mode() const { return viewMode; }
    void setCompact( bool );
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

private slots:
    void followInput();

private:
    int marginLeft() const;
    int marginRight() const;
    int topOffset() const;
    int tileGap() const;
    int labelHeight() const;
    void relayout();
    /* Home shelves (headings, fixed tiles, one row each) and the tile
     * pages behind them; in the sidebar, headed sections of rows */
    bool shelfLayout() const { return hasHeaders || ( !compact && hasTiles ); }
    void layoutShelves();
    QSize tileSize( const LibraryEntry & ) const;
    void paintShelfEntry( QPainter &, int i, const QRect &r, const QFontMetrics & );
    QRect entryRect( int i ) const;   /* in content coordinates */
    int entryAt( const QPoint &viewportPos ) const;
    QPixmap thumbnail( const LibraryEntry &, const QSize & );
    void requestVisibleThumbnails();

    intf_thread_t *p_intf;
    MediaLibrary *lib;
    QVector<LibraryEntry> entries;
    Mode viewMode = GRID;
    bool compact = false;
    int columns = 1;
    QSize cell;           /* tile, or row */
    bool hasHeaders = false, hasTiles = false;
    QVector<QRect> rects; /* shelf layout: per entry, empty when hidden */
    int hovered = -1, pressed = -1;
    QPoint pressPos;          /* a drag to the queue starts from here */
    void startDrag( int entry );
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

    QString section() const { return browser->section(); }
    MiniPlayer *miniPlayer() const { return mini; }

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
    void resizeEvent( QResizeEvent * ) override;

private:
    QWidget *buildNav();
    QWidget *buildToolbar();
    void locationChanged();
    void entriesChanged();
    void rebuildCrumb();
    void placeMini();

    intf_thread_t *p_intf;
    MediaLibrary *lib;
    QHash<QString, QAbstractButton *> navButtons;
    QToolButton *backBtn, *fwdBtn, *sortBtn, *viewBtn;
    QWidget *crumb;
    QHBoxLayout *crumbLayout;
    QLabel *countLabel;
    QLineEdit *search;
    LibraryGrid *grid;
    MiniPlayer *mini;
    LibraryBrowser *browser;
};

#endif
