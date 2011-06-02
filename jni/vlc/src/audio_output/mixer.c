/*****************************************************************************
 * mixer.c : audio output mixing operations
 *****************************************************************************
 * Copyright (C) 2002-2004 the VideoLAN team
 * $Id: 287df6422a4e0af04126d56306d9751b05c7817f $
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <assert.h>

#include <stddef.h>
#include <vlc_common.h>
#include <libvlc.h>
#include <vlc_modules.h>

#include <vlc_aout.h>
#include "aout_internal.h"
/*****************************************************************************
 * aout_MixerNew: prepare a mixer plug-in
 *****************************************************************************
 * Please note that you must hold the mixer lock.
 *****************************************************************************/
int aout_MixerNew( aout_instance_t * p_aout )
{
    assert( !p_aout->p_mixer );
    vlc_assert_locked( &p_aout->input_fifos_lock );

    aout_mixer_t *p_mixer = vlc_object_create( p_aout, sizeof(*p_mixer) );
    if( !p_mixer )
        return VLC_EGENERIC;

    p_mixer->fmt = p_aout->mixer_format;
    p_mixer->input = &p_aout->p_input->mixer;
    p_mixer->mix = NULL;
    p_mixer->sys = NULL;

    p_mixer->module = module_need( p_mixer, "audio mixer", NULL, false );
    if( !p_mixer->module )
    {
        msg_Err( p_aout, "no suitable audio mixer" );
        vlc_object_release( p_mixer );
        return VLC_EGENERIC;
    }

    /* */
    p_aout->p_mixer = p_mixer;
    return VLC_SUCCESS;
}

/*****************************************************************************
 * aout_MixerDelete: delete the mixer
 *****************************************************************************
 * Please note that you must hold the mixer lock.
 *****************************************************************************/
void aout_MixerDelete( aout_instance_t * p_aout )
{
    if( !p_aout->p_mixer )
        return;

    module_unneed( p_aout->p_mixer, p_aout->p_mixer->module );

    vlc_object_release( p_aout->p_mixer );

    /* */
    p_aout->p_mixer = NULL;
}

/*****************************************************************************
 * MixBuffer: try to prepare one output buffer
 *****************************************************************************
 * Please note that you must hold the mixer lock.
 *****************************************************************************/
static int MixBuffer( aout_instance_t * p_aout, float volume )
{
    aout_mixer_t *p_mixer = p_aout->p_mixer;

    assert( p_aout->p_input != NULL );

    aout_input_t *p_input = p_aout->p_input;
    if( p_input->b_paused )
        return -1;

    aout_fifo_t * p_fifo = &p_input->mixer.fifo;
    mtime_t now = mdate();

    aout_lock_input_fifos( p_aout );
    aout_lock_output_fifo( p_aout );

    /* Retrieve the date of the next buffer. */
    date_t exact_start_date = p_aout->output.fifo.end_date;
    mtime_t start_date = date_Get( &exact_start_date );

    if( start_date != 0 && start_date < now )
    {
        /* The output is _very_ late. This can only happen if the user
         * pauses the stream (or if the decoder is buggy, which cannot
         * happen :). */
        msg_Warn( p_aout, "output PTS is out of range (%"PRId64"), clearing out",
                  mdate() - start_date );
        aout_FifoSet( p_aout, &p_aout->output.fifo, 0 );
        date_Set( &exact_start_date, 0 );
        start_date = 0;
    }

    aout_unlock_output_fifo( p_aout );

    /* See if we have enough data to prepare a new buffer for the audio
     * output. First : start date. */
    if ( !start_date )
    {
        /* Find the latest start date available. */
        aout_buffer_t *p_buffer;
        for( ;; )
        {
            p_buffer = p_fifo->p_first;
            if( p_buffer == NULL )
                goto giveup;
            if( p_buffer->i_pts >= now )
                break;

            msg_Warn( p_aout, "input PTS is out of range (%"PRId64"), "
                      "trashing", now - p_buffer->i_pts );
            aout_BufferFree( aout_FifoPop( p_aout, p_fifo ) );
            p_input->mixer.begin = NULL;
        }

        date_Set( &exact_start_date, p_buffer->i_pts );
        start_date = p_buffer->i_pts;
    }

    date_Increment( &exact_start_date, p_aout->output.i_nb_samples );
    mtime_t end_date = date_Get( &exact_start_date );

    /* Check that start_date is available. */
    aout_buffer_t *p_buffer = p_fifo->p_first;
    mtime_t prev_date;

    for( ;; )
    {
        if( p_buffer == NULL )
            goto giveup;

        /* Check for the continuity of start_date */
        prev_date = p_buffer->i_pts + p_buffer->i_length;
        if( prev_date >= start_date - 1 )
            break;
        /* We authorize a +-1 because rounding errors get compensated
         * regularly. */
        msg_Warn( p_aout, "the mixer got a packet in the past (%"PRId64")",
                  start_date - prev_date );
        aout_BufferFree( aout_FifoPop( p_aout, p_fifo ) );
        p_input->mixer.begin = NULL;
        p_buffer = p_fifo->p_first;
    }

    /* Check that we have enough samples. */
    while( prev_date < end_date )
    {
        p_buffer = p_buffer->p_next;
        if( p_buffer == NULL )
            goto giveup;

        /* Check that all buffers are contiguous. */
        if( prev_date != p_buffer->i_pts )
        {
            msg_Warn( p_aout,
                      "buffer hole, dropping packets (%"PRId64")",
                      p_buffer->i_pts - prev_date );

            aout_buffer_t *p_deleted;
            while( (p_deleted = p_fifo->p_first) != p_buffer )
                aout_BufferFree( aout_FifoPop( p_aout, p_fifo ) );
        }

        prev_date = p_buffer->i_pts + p_buffer->i_length;
    }

    p_buffer = p_fifo->p_first;
    if( !AOUT_FMT_NON_LINEAR( &p_mixer->fmt ) )
    {
        /* Additionally check that p_first_byte_to_mix is well located. */
        mtime_t i_buffer = (start_date - p_buffer->i_pts)
                         * p_mixer->fmt.i_bytes_per_frame
                         * p_mixer->fmt.i_rate
                         / p_mixer->fmt.i_frame_length
                         / CLOCK_FREQ;
        if( p_input->mixer.begin == NULL )
            p_input->mixer.begin = p_buffer->p_buffer;

        ptrdiff_t bytes = p_input->mixer.begin - p_buffer->p_buffer;
        if( !((i_buffer + p_mixer->fmt.i_bytes_per_frame > bytes)
         && (i_buffer < p_mixer->fmt.i_bytes_per_frame + bytes)) )
        {
            msg_Warn( p_aout, "mixer start is not output start (%"PRId64")",
                      i_buffer - bytes );

            /* Round to the nearest multiple */
            i_buffer /= p_mixer->fmt.i_bytes_per_frame;
            i_buffer *= p_mixer->fmt.i_bytes_per_frame;
            if( i_buffer < 0 )
            {
                /* Is it really the best way to do it ? */
                aout_lock_output_fifo( p_aout );
                aout_FifoSet( p_aout, &p_aout->output.fifo, 0 );
                date_Set( &exact_start_date, 0 );
                aout_unlock_output_fifo( p_aout );
                goto giveup;
            }
            p_input->mixer.begin = p_buffer->p_buffer + i_buffer;
        }
    }

    /* Run the mixer. */
    p_buffer = p_mixer->mix( p_aout->p_mixer, p_aout->output.i_nb_samples,
                             volume );
    aout_unlock_input_fifos( p_aout );

    if( unlikely(p_buffer == NULL) )
        return -1;

    p_buffer->i_pts = start_date;
    p_buffer->i_length = end_date - start_date;
    aout_OutputPlay( p_aout, p_buffer );
    return 0;

giveup:
    /* Interrupted before the end... We can't run. */
    aout_unlock_input_fifos( p_aout );
    return -1;
}

/*****************************************************************************
 * aout_MixerRun: entry point for the mixer & post-filters processing
 *****************************************************************************
 * Please note that you must hold the mixer lock.
 *****************************************************************************/
void aout_MixerRun( aout_instance_t * p_aout, float volume )
{
    while( MixBuffer( p_aout, volume ) != -1 );
}
