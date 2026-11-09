/*****************************************************************************
 * queue_panel.hpp : Queue / Bookmarks / Chapters panel (P5d)
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

#ifndef VLC_QT_QUEUE_PANEL_HPP_
#define VLC_QT_QUEUE_PANEL_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QListView>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QListView;
class QListWidget;
class QStackedWidget;
class QToolButton;
class QVariantAnimation;
class PLModel;
class SeekPoints;

/* The play queue as Figma's rows (drag handle, name, duration; the
 * playing one in the accent), on its own playlist model of VLC's playing
 * node so the sidebar's browsing never moves it. Rows drag to reorder;
 * files dragged in from the library (or anywhere) are inserted where
 * they are dropped. Used by the Queue panel and the mini player. */
class QueueList : public QListView
{
    Q_OBJECT
public:
    QueueList( intf_thread_t *, QWidget *parent,
               int handleX, int nameX, int rightInset );
    PLModel *plModel() const { return model; }
    void removeSelected();
    void showSortMenu( const QPoint &globalPos );
    /* "4 items - 2h 3m" */
    static QString summary( intf_thread_t * );

signals:
    void changed();

protected:
    void dragEnterEvent( QDragEnterEvent * ) override;
    void dragMoveEvent( QDragMoveEvent * ) override;
    void dropEvent( QDropEvent * ) override;
    void keyPressEvent( QKeyEvent * ) override;

private:
    bool external( const QMimeData * ) const;
    intf_thread_t *p_intf;
    PLModel *model;
};

/* Components "Queue" panel: tabs Queue | Bookmarks | Chapters over the
 * play queue (its own playlist model on VLC's playing node, so the
 * sidebar's browsing never moves it), the input's bookmarks and its
 * chapters. 400px, docked right like the adjustment panel. */
class QueuePanel : public QWidget
{
    Q_OBJECT
public:
    enum Tab { QUEUE_TAB, BOOKMARKS_TAB, CHAPTERS_TAB, TAB_COUNT };
    enum { PANEL_WIDTH = 400 };

    QueuePanel( intf_thread_t *, QWidget *parent );
    void setCurrentTab( int, bool animate = true );

signals:
    void closeRequested();

protected:
    void keyPressEvent( QKeyEvent * ) override;
    bool eventFilter( QObject *, QEvent * ) override;

private:
    QWidget *buildQueuePage();
    QWidget *buildBookmarksPage();
    QWidget *buildChaptersPage();
    QWidget *headerRow( QLabel **count );
    QToolButton *headerButton( QWidget *parent, const QString &icon, const QString &tip );
    void placeUnderline( bool animate );
    void updateQueueHeader();
    void updateModes();
    void refreshBookmarks();
    void refreshChapters();
    void showSortMenu();

    intf_thread_t *p_intf;
    QWidget *tabBar;
    QWidget *underline;
    QVariantAnimation *underlineAnim;
    QButtonGroup *tabs;
    QStackedWidget *stack;

    PLModel *model;
    QueueList *queueView;
    QLabel *queueCount, *bookmarkCount, *chapterCount;
    QToolButton *sortBtn, *repeatBtn, *shuffleBtn;
    QListWidget *bookmarkList, *chapterList;
    SeekPoints *chapters;
};

/* the panel's slot right of the content, sliding open like AdjustDock */
class QueueDock : public QWidget
{
public:
    enum { GAP = 10 };
    QueueDock( intf_thread_t *, QWidget *parent );
    QueuePanel *panel() const { return p; }
    bool isOpen() const { return open; }
    void setOpen( bool, bool animate = true );

protected:
    void resizeEvent( QResizeEvent * ) override;

private:
    QueuePanel *p;
    QVariantAnimation *anim;
    bool open;
};

#endif
