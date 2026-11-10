/*****************************************************************************
 * bookmarks.hpp : quick bookmarks, their OSD and per-file memory (P7)
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
 * VLC 3 keeps an input's bookmarks only in the playlist item (the
 * "bookmarks=" option), so they are gone when VLC restarts. The redesign
 * remembers them per file in vlc-qt-bookmarks.conf and puts them back the
 * next time the file plays.
 */

#ifndef VLC_QT_BOOKMARKS_HPP_
#define VLC_QT_BOOKMARKS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>

#include <QList>
#include <QString>

namespace Bookmarks
{
    /* restore and save the playing file's bookmarks from now on */
    void init( intf_thread_t * );
    /* bookmark the current moment as "<Title> <n>"; returns the name, or
     * an empty string when nothing plays. With osd, confirm it on the video
     * (or over the player when there is no video). */
    QString add( intf_thread_t *, bool osd );
    /* the playing input's bookmark times, microseconds */
    QList<int64_t> times();
    /* a short message on the video, or over the player without one */
    void osd( intf_thread_t *, const QString & );
}

#endif
