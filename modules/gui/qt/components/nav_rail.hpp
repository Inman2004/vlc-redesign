/*****************************************************************************
 * nav_rail.hpp : redesigned left navigation rail
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

#ifndef VLC_QT_NAV_RAIL_HPP_
#define VLC_QT_NAV_RAIL_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"

#include <QWidget>
#include <QMap>

class QToolButton;

/* 50px icon column: Home, Videos, Music, Network, Playlists, Recents, Add.
 * Clicking a section selects it; clicking the selected one collapses the
 * sidebar. */
class NavRail : public QWidget
{
    Q_OBJECT
public:
    NavRail( QWidget *parent );

    QString current() const { return currentKey; }
    static QString titleFor( const QString &key );

public slots:
    void setCurrent( const QString &key ); /* empty = none */

signals:
    void sectionActivated( const QString &key );
    void collapseRequested();
    void addRequested();

private:
    QMap<QString, QToolButton *> buttons;
    QString currentKey;
};

#endif
