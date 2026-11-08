/*****************************************************************************
 * sidebar_panel.hpp : redesigned library sidebar
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

#ifndef VLC_QT_SIDEBAR_PANEL_HPP_
#define VLC_QT_SIDEBAR_PANEL_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QWidget>
#include <QPersistentModelIndex>
#include <QList>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;
class PlaylistWidget;
class StandardPLPanel;
class LibraryBrowser;
class LibraryGrid;

/* The 376px library sidebar (Components panels). Videos, Music and
 * Network browse the Qt-side library through a LibraryBrowser; Home and
 * Playlists, and the discovery sources opened from Network, use the
 * existing PlaylistWidget (its StandardPLPanel does the browsing); Recents
 * is backed by RecentsMRL. */
class SidebarPanel : public QWidget
{
    Q_OBJECT
public:
    SidebarPanel( intf_thread_t *, QWidget *parent );

    void setPlaylistWidget( PlaylistWidget * );
    PlaylistWidget *playlistWidget() const { return plWidget; }

public slots:
    void showSection( const QString &key );

signals:
    void popOutRequested();

private slots:
    void rootChanged( const QModelIndex & );
    void updateCount();
    void goBack();
    void goForward();
    void toggleSearch( bool );
    void showSortMenu();
    void showViewMenu();
    void showMoreMenu();
    void refreshRecents();
    void libraryLocationChanged();
    void libraryEntriesChanged();
    void libraryAction( const QString & );

private:
    bool libraryShown() const;
    void openSource( const QString &name, const QString &title );
    void setTitleCrumb( const QStringList &segments, int linkedFrom );
    void setSortDot( bool );

    intf_thread_t *p_intf;
    PlaylistWidget *plWidget;
    StandardPLPanel *plView;

    QWidget *title;            /* the breadcrumb */
    QHBoxLayout *crumbLayout;
    QLabel *count;
    QToolButton *backBtn, *fwdBtn, *moreBtn, *popBtn, *sortBtn, *viewBtn, *searchBtn;
    QWidget *searchRow;
    QVBoxLayout *searchLayout;
    QStackedWidget *pages;
    QWidget *plPage;
    QVBoxLayout *plLayout;
    QListWidget *recents;
    LibraryBrowser *browser;
    LibraryGrid *libGrid;
    QLineEdit *libSearch;
    /* a discovery source opened from the Network panel: Back returns there */
    QString sdFromNetwork;

    QString section;
    QPersistentModelIndex currentRoot;
    QList<QPersistentModelIndex> history;
    int historyPos;
    bool navigating;
    Qt::SortOrder sortOrder;
};

#endif
