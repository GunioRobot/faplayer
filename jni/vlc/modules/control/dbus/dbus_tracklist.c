/*****************************************************************************
 * dbus-tracklist.c : dbus control module (mpris v2.1) - TrackList interface
 *****************************************************************************
 * Copyright © 2006-2011 Rafaël Carré
 * Copyright © 2007-2011 Mirsal Ennaime
 * Copyright © 2009-2011 The VideoLAN team
 * $Id: 8bf66dd5305cbeb8e980719b9c7f1dc7b7f8d42e $
 *
 * Authors:    Mirsal Ennaime <mirsal at mirsal fr>
 *             Rafaël Carré <funman at videolanorg>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_playlist.h>

#include <assert.h>

#include "dbus_tracklist.h"
#include "dbus_common.h"

DBUS_METHOD( AddTrack )
{ /* add the string to the playlist, and play it if the boolean is true */
    REPLY_INIT;

    DBusError error;
    dbus_error_init( &error );

    char *psz_mrl, *psz_aftertrack;
    dbus_bool_t b_play;

    dbus_message_get_args( p_from, &error,
            DBUS_TYPE_STRING, &psz_mrl,
            DBUS_TYPE_OBJECT_PATH, &psz_aftertrack,
            DBUS_TYPE_BOOLEAN, &b_play,
            DBUS_TYPE_INVALID );

    if( dbus_error_is_set( &error ) )
    {
        msg_Err( (vlc_object_t*) p_this, "D-Bus message reading : %s",
                error.message );
        dbus_error_free( &error );
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

#warning psz_aftertrack is not used
    playlist_Add( PL, psz_mrl, NULL, PLAYLIST_APPEND |
            ( ( b_play == TRUE ) ? PLAYLIST_GO : 0 ) ,
            PLAYLIST_END, true, false );

    REPLY_SEND;
}

DBUS_METHOD( GetTracksMetadata )
{
    REPLY_INIT;
    OUT_ARGUMENTS;

    int i_track_id = -1;
    const char *psz_track_id = NULL;

    playlist_t   *p_playlist = PL;
    input_item_t *p_input = NULL;

    DBusMessageIter in_args, track_ids, meta;
    dbus_message_iter_init( p_from, &in_args );

    if( DBUS_TYPE_ARRAY != dbus_message_iter_get_arg_type( &in_args ) )
    {
        msg_Err( (vlc_object_t*) p_this, "Invalid arguments" );
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    dbus_message_iter_recurse( &in_args, &track_ids );
    dbus_message_iter_open_container( &args, DBUS_TYPE_ARRAY, "a{sv}", &meta );

    while( DBUS_TYPE_OBJECT_PATH ==
           dbus_message_iter_get_arg_type( &track_ids ) )
    {
        dbus_message_iter_get_basic( &track_ids, &psz_track_id );

        if( 1 != sscanf( psz_track_id, MPRIS_TRACKID_FORMAT, &i_track_id ) )
        {
            msg_Err( (vlc_object_t*) p_this, "Invalid track id: %s",
                                             psz_track_id );
            continue;
        }

        PL_LOCK;
        for( int i = 0; i < playlist_CurrentSize( p_playlist ); i++ )
        {
            p_input = p_playlist->current.p_elems[i]->p_input;

            if( i_track_id == p_input->i_id )
            {
                GetInputMeta( p_input, &meta );
                break;
            }
        }
        PL_UNLOCK;

        dbus_message_iter_next( &track_ids );
    }

    dbus_message_iter_close_container( &args, &meta );
    REPLY_SEND;
}

DBUS_METHOD( GoTo )
{
    REPLY_INIT;

    int i_track_id = -1;
    const char *psz_track_id = NULL;
    playlist_t *p_playlist = PL;

    DBusError error;
    dbus_error_init( &error );

    dbus_message_get_args( p_from, &error,
            DBUS_TYPE_OBJECT_PATH, &psz_track_id,
            DBUS_TYPE_INVALID );

    if( dbus_error_is_set( &error ) )
    {
        msg_Err( (vlc_object_t*) p_this, "D-Bus message reading : %s",
                error.message );
        dbus_error_free( &error );
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if( 1 != sscanf( psz_track_id, MPRIS_TRACKID_FORMAT, &i_track_id ) )
    {
        msg_Err( (vlc_object_t*) p_this, "Invalid track id %s", psz_track_id );
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    PL_LOCK;

    for( int i = 0; i < playlist_CurrentSize( p_playlist ); i++ )
    {
        if( i_track_id == p_playlist->current.p_elems[i]->p_input->i_id )
        {
            playlist_Control( p_playlist, PLAYLIST_VIEWPLAY, true,
                              p_playlist->current.p_elems[i]->p_parent,
                              p_playlist->current.p_elems[i] );
            break;
        }
    }

    PL_UNLOCK;
    REPLY_SEND;
}

DBUS_METHOD( RemoveTrack )
{
    REPLY_INIT;

    DBusError error;
    dbus_error_init( &error );

    int   i_id = -1, i;
    char *psz_id = NULL;
    playlist_t *p_playlist = PL;
    input_item_t *p_input  = NULL;

    dbus_message_get_args( p_from, &error,
            DBUS_TYPE_OBJECT_PATH, &psz_id,
            DBUS_TYPE_INVALID );

    if( dbus_error_is_set( &error ) )
    {
        msg_Err( (vlc_object_t*) p_this, "D-Bus message reading : %s",
                error.message );
        dbus_error_free( &error );
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if( 1 != sscanf( psz_id, MPRIS_TRACKID_FORMAT, &i_id ) )
    {
        msg_Err( (vlc_object_t*) p_this, "Invalid track id: %s", psz_id );
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    PL_LOCK;

    for( i = 0; i < playlist_CurrentSize( p_playlist ); i++ )
    {
        p_input = p_playlist->current.p_elems[i]->p_input;

        if( i_id == p_input->i_id )
        {
            playlist_DeleteFromInput( p_playlist, p_input, true );
            break;
        }
    }

    PL_UNLOCK;
    REPLY_SEND;
}

/******************************************************************************
 * TrackListChange: tracklist order / length change signal
 *****************************************************************************/
DBUS_SIGNAL( TrackListChangeSignal )
{ /* emit the new tracklist lengh */
    SIGNAL_INIT( DBUS_MPRIS_TRACKLIST_INTERFACE,
                 DBUS_MPRIS_TRACKLIST_PATH,
                 "TrackListChange");

    OUT_ARGUMENTS;

    playlist_t *p_playlist = ((intf_thread_t*)p_data)->p_sys->p_playlist;
    PL_LOCK;
    dbus_int32_t i_elements = p_playlist->current.i_size;
    PL_UNLOCK;

    ADD_INT32( &i_elements );
    SIGNAL_SEND;
}

#define METHOD_FUNC( interface, method, function ) \
    else if( dbus_message_is_method_call( p_from, interface, method ) )\
        return function( p_conn, p_from, p_this )

DBusHandlerResult
handle_tracklist ( DBusConnection *p_conn, DBusMessage *p_from, void *p_this )
{
    if(0);

/*  METHOD_FUNC( DBUS_INTERFACE_PROPERTIES, "Get",    GetProperty );
    METHOD_FUNC( DBUS_INTERFACE_PROPERTIES, "Set",    SetProperty );
    METHOD_FUNC( DBUS_INTERFACE_PROPERTIES, "GetAll", GetAllProperties ); */

    /* here D-Bus method names are associated to an handler */

    METHOD_FUNC( DBUS_MPRIS_TRACKLIST_INTERFACE, "GetTracksMetadata", GetTracksMetadata );
    METHOD_FUNC( DBUS_MPRIS_TRACKLIST_INTERFACE, "AddTrack",        AddTrack );
    METHOD_FUNC( DBUS_MPRIS_TRACKLIST_INTERFACE, "RemoveTrack",     RemoveTrack );
    METHOD_FUNC( DBUS_MPRIS_TRACKLIST_INTERFACE, "GoTo",            GoTo );

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/*****************************************************************************
 * TrackListChangeEmit: Emits the TrackListChange signal
 *****************************************************************************/
/* FIXME: It is not called on tracklist reordering */
int TrackListChangeEmit( intf_thread_t *p_intf, int signal, int i_node )
{
    // "playlist-item-append"
    if( signal == SIGNAL_PLAYLIST_ITEM_APPEND )
    {
        /* don't signal when items are added/removed in p_category */
        playlist_t *p_playlist = p_intf->p_sys->p_playlist;
        PL_LOCK;
        playlist_item_t *p_item = playlist_ItemGetById( p_playlist, i_node );
        assert( p_item );
        while( p_item->p_parent )
            p_item = p_item->p_parent;
        if( p_item == p_playlist->p_root_category )
        {
            PL_UNLOCK;
            return VLC_SUCCESS;
        }
        PL_UNLOCK;
    }

    if( p_intf->p_sys->b_dead )
        return VLC_SUCCESS;

    TrackListChangeSignal( p_intf->p_sys->p_conn, p_intf );
    return VLC_SUCCESS;
}

#undef METHOD_FUNC
