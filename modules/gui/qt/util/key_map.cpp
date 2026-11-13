/*****************************************************************************
 * key_map.cpp : hotkeys that follow the configuration without a restart
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

#include "util/key_map.hpp"
#include "util/qt_compat.hpp"
#include "util/bookmarks.hpp"

#include <vlc_actions.h>
#include <vlc_configuration.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>
#include <vlc_variables.h>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace
{
QHash<uint32_t, int> current;   /* key -> action, from the configuration now */
QSet<uint32_t> loaded;          /* keys the core's map knows (from start) */
bool active = false;            /* true once a binding changed this session */
QSet<uint32_t> quickBookmark;   /* qt-key-quick-bookmark: owned by the interface */

QSet<uint32_t> readKeys( intf_thread_t *p_intf, const char *option )
{
    QSet<uint32_t> keys;
    char *psz = config_GetPsz( p_intf, option );
    if( psz )
    {
        for( const QString &k : QString::fromUtf8( psz ).split( '\t', Qt::SkipEmptyParts ) )
        {
            const uint32_t code = vlc_str2keycode( k.toUtf8().constData() );
            if( code != KEY_UNSET )
                keys.insert( code );
        }
        free( psz );
    }
    return keys;
}

QHash<uint32_t, int> readBindings( intf_thread_t *p_intf )
{
    QHash<uint32_t, int> map;
    unsigned n;
    module_config_t *cfg = module_config_get( module_get_main(), &n );
    for( unsigned i = 0; i < n; i++ )
    {
        if( cfg[i].i_type != CONFIG_ITEM_KEY || !cfg[i].psz_name
         || strncmp( cfg[i].psz_name, "key-", 4 ) )
            continue;
        const vlc_action_id_t id = vlc_actions_get_id( cfg[i].psz_name );
        if( id == ACTIONID_NONE )
            continue;
        char *psz = config_GetPsz( p_intf, cfg[i].psz_name );
        if( !psz )
            continue;
        /* several keys are separated by tabs */
        for( const QString &k : QString::fromUtf8( psz ).split( '\t', Qt::SkipEmptyParts ) )
        {
            const uint32_t code = vlc_str2keycode( k.toUtf8().constData() );
            if( code != KEY_UNSET )
                map.insert( code, id );
        }
        free( psz );
    }
    module_config_free( cfg );
    return map;
}
}

void KeyMap::init( intf_thread_t *p_intf )
{
    current = readBindings( p_intf );
    loaded = QtCompat::keySet( current );
    active = false;
    quickBookmark = readKeys( p_intf, "qt-key-quick-bookmark" );
}

void KeyMap::reload( intf_thread_t *p_intf )
{
    if( loaded.isEmpty() && current.isEmpty() )
        init( p_intf );
    current = readBindings( p_intf );
    active = true;
    quickBookmark = readKeys( p_intf, "qt-key-quick-bookmark" );
}

void KeyMap::send( intf_thread_t *p_intf, int vlcKey )
{
    if( vlcKey <= 0 )
        return;
    if( quickBookmark.contains( (uint32_t)vlcKey ) )
    {
        Bookmarks::add( p_intf, true );
        return;
    }
    if( !active )
    {
        var_SetInteger( p_intf->obj.libvlc, "key-pressed", vlcKey );
        return;
    }
    auto it = current.constFind( (uint32_t)vlcKey );
    if( it != current.constEnd() )
        var_SetInteger( p_intf->obj.libvlc, "key-action", it.value() );
    else if( !loaded.contains( (uint32_t)vlcKey ) )
        /* a key nobody binds: still offer it to whoever listens */
        var_SetInteger( p_intf->obj.libvlc, "key-pressed", vlcKey );
    /* else: the core would run the binding that was just removed */
}
