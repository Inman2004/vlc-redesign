/*****************************************************************************
 * key_map.hpp : hotkeys that follow the configuration without a restart
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
 * VLC 3's core reads the key-* options once, when libvlc starts, and has no
 * call to re-read them. The Qt interface therefore keeps its own copy of
 * the bindings: once a shortcut was edited, a key pressed in the interface
 * is turned into its action here and sent as "key-action", instead of
 * letting the core's stale map decide.
 */

#ifndef VLC_QT_KEY_MAP_HPP_
#define VLC_QT_KEY_MAP_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_interface.h>

namespace KeyMap
{
    /* remember the bindings the core loaded (call once at start) */
    void init( intf_thread_t * );
    /* re-read the key-* options after they changed */
    void reload( intf_thread_t * );
    /* send a key pressed in the interface to the right action */
    void send( intf_thread_t *, int vlcKey );
}

#endif
