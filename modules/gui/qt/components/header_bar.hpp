/*****************************************************************************
 * header_bar.hpp : redesigned window header (title bar)
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

#ifndef VLC_QT_HEADER_BAR_HPP_
#define VLC_QT_HEADER_BAR_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QWidget>

class QLabel;
class QToolButton;
class MainInterface;

/* 40px header: cone, main menu, sidebar toggle, current folder,
 * centred media title and (when frameless) the window buttons.
 * Dragging empty space moves the window. */
class HeaderBar : public QWidget
{
    Q_OBJECT
public:
    HeaderBar( intf_thread_t *, MainInterface * );

    QToolButton *sidebarButton() const { return sidebarBtn; }

public slots:
    void setTitle( const QString & );
    void updateCrumb();

signals:
    void sidebarToggled( bool );

protected:
    void mousePressEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void mouseDoubleClickEvent( QMouseEvent * ) Q_DECL_OVERRIDE;
    void resizeEvent( QResizeEvent * ) Q_DECL_OVERRIDE;
    bool eventFilter( QObject *, QEvent * ) Q_DECL_OVERRIDE;

private:
    void showMainMenu();
    void toggleMaximized();
    void updateWindowButtons();
    void placeTitle();

    intf_thread_t *p_intf;
    MainInterface *mi;
    QToolButton *menuBtn, *sidebarBtn;
    QToolButton *minBtn, *maxBtn, *fsBtn, *closeBtn;
    QLabel *folderIcon, *crumb, *title;
    QWidget *leftGroup, *rightGroup;
};

/* Lets a frameless top-level window be resized from its edges. */
class FramelessResizer : public QObject
{
    Q_OBJECT
public:
    FramelessResizer( QWidget *window, int border = 6 );
protected:
    bool eventFilter( QObject *, QEvent * ) Q_DECL_OVERRIDE;
private:
    Qt::Edges edgesAt( const QPoint &globalPos ) const;
    void setEdgeCursor( Qt::Edges );
    QWidget *window;
    int border;
    bool cursorOverridden;
};

#endif
