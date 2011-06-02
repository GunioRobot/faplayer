/*****************************************************************************
 * objects.c: Generic lua<->vlc object wrapper
 *****************************************************************************
 * Copyright (C) 2007-2008 the VideoLAN team
 * $Id: 32c2bd2f77721edc848b5aea8de2dfb43098a3f4 $
 *
 * Authors: Antoine Cellerier <dionoea at videolan tod org>
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifndef  _GNU_SOURCE
#   define  _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>

#include <lua.h>        /* Low level lua C API */
#include <lauxlib.h>    /* Higher level C API */

#include "../vlc.h"
#include "../libs.h"
#include "objects.h"
#include "playlist.h"
#include "input.h"

typedef struct
{
    vlc_object_t *p_obj;
} vlclua_object_t;

/*****************************************************************************
 * Generic vlc_object_t wrapper creation
 *****************************************************************************/
int __vlclua_push_vlc_object( lua_State *L, vlc_object_t *p_obj,
                              lua_CFunction pf_gc )
{
    vlc_object_t **udata = (vlc_object_t **)
        lua_newuserdata( L, sizeof( vlc_object_t * ) );
    *udata = p_obj;

    if( luaL_newmetatable( L, "vlc_object" ) )
    {
        /* Hide the metatable */
        lua_pushliteral( L, "none of your business" );
        lua_setfield( L, -2, "__metatable" );
        if( pf_gc ) /* FIXME */
        {
            /* Set the garbage collector if needed */
            lua_pushcfunction( L, pf_gc );
            lua_setfield( L, -2, "__gc" );
        }
    }
    lua_setmetatable( L, -2 );
    return 1;
}

int vlclua_gc_release( lua_State *L )
{
    vlc_object_t **p_obj = (vlc_object_t **)luaL_checkudata( L, 1, "vlc_object" );
    lua_pop( L, 1 );
    vlc_object_release( *p_obj );
    return 0;
}

static int vlclua_object_find( lua_State *L )
{
    lua_pushnil( L );
    return 1;
}

static int vlclua_get_libvlc( lua_State *L )
{
    vlclua_push_vlc_object( L, vlclua_get_this( L )->p_libvlc,
                            NULL );
    return 1;
}

static int vlclua_get_playlist( lua_State *L )
{
    playlist_t *p_playlist = vlclua_get_playlist_internal( L );
    if( p_playlist )
    {
        vlclua_push_vlc_object( L, p_playlist, vlclua_gc_release );
        vlc_object_hold( p_playlist );
    }
    else lua_pushnil( L );
    return 1;
}

static int vlclua_get_input( lua_State *L )
{
    input_thread_t *p_input = vlclua_get_input_internal( L );
    if( p_input )
    {
        vlclua_push_vlc_object( L, p_input, vlclua_gc_release );
    }
    else lua_pushnil( L );
    return 1;
}

/*****************************************************************************
 *
 *****************************************************************************/
static const luaL_Reg vlclua_object_reg[] = {
    { "input", vlclua_get_input },
    { "playlist", vlclua_get_playlist },
    { "libvlc", vlclua_get_libvlc },
    { "find", vlclua_object_find },
    { NULL, NULL }
};

void luaopen_object( lua_State *L )
{
    lua_newtable( L );
    luaL_register( L, NULL, vlclua_object_reg );
    lua_setfield( L, -2, "object" );
}
