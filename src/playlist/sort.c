/*****************************************************************************
 * sort.c : Playlist sorting functions
 *****************************************************************************
 * Copyright (C) 1999-2009 VLC authors and VideoLAN
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Ilkka Ollakka <ileoo@videolan.org>
 *          Rémi Duraffort <ivoire@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_rand.h>
#include <vlc_fs.h>
#include <vlc_url.h>
#include <sys/stat.h>
#define  VLC_INTERNAL_PLAYLIST_SORT_FUNCTIONS
#include "vlc_playlist.h"
#include "playlist_internal.h"


/* General comparison functions */
/**
 * Compare two items using their title or name
 * @param first: the first item
 * @param second: the second item
 * @return -1, 0 or 1 like strcmp
 */
static inline int meta_strcasecmp_title( const playlist_item_t *first,
                              const playlist_item_t *second )
{
    int i_ret;
    char *psz_first = input_item_GetTitleFbName( first->p_input );
    char *psz_second = input_item_GetTitleFbName( second->p_input );

    if( psz_first && psz_second )
        i_ret = strcasecmp( psz_first, psz_second );
    else if( !psz_first && psz_second )
        i_ret = 1;
    else if( psz_first && !psz_second )
        i_ret = -1;
    else
        i_ret = 0;
    free( psz_first );
    free( psz_second );

    return i_ret;
}

/**
 * Compare two intems according to the given meta type
 * @param first: the first item
 * @param second: the second item
 * @param meta: the meta type to use to sort the items
 * @param b_integer: true if the meta are integers
 * @return -1, 0 or 1 like strcmp
 */
static inline int meta_sort( const playlist_item_t *first,
                             const playlist_item_t *second,
                             vlc_meta_type_t meta, bool b_integer )
{
    int i_ret;
    char *psz_first = input_item_GetMeta( first->p_input, meta );
    char *psz_second = input_item_GetMeta( second->p_input, meta );

    /* Nodes go first */
    if( first->i_children == -1 && second->i_children >= 0 )
        i_ret = 1;
    else if( first->i_children >= 0 && second->i_children == -1 )
       i_ret = -1;
    /* Both are nodes, sort by name */
    else if( first->i_children >= 0 && second->i_children >= 0 )
        i_ret = meta_strcasecmp_title( first, second );
    /* Both are items */
    else if( !psz_first && !psz_second )
        i_ret = 0;
    else if( !psz_first && psz_second )
        i_ret = 1;
    else if( psz_first && !psz_second )
        i_ret = -1;
    else
    {
        if( b_integer )
            i_ret = atoi( psz_first ) - atoi( psz_second );
        else
            i_ret = strcasecmp( psz_first, psz_second );
    }

    free( psz_first );
    free( psz_second );
    return i_ret;
}

/* Comparison functions */

/**
 * Return the comparison function appropriate for the SORT_* and ORDER_*
 * arguments given, or NULL for SORT_RANDOM.
 * @param i_mode: a SORT_* enum indicating the field to sort on
 * @param i_type: ORDER_NORMAL or ORDER_REVERSE
 * @return function pointer, or NULL for SORT_RANDOM or invalid input
 */
typedef int (*sortfn_t)(const void *,const void *);
static const sortfn_t sorting_fns[NUM_SORT_FNS][2];
static inline sortfn_t find_sorting_fn( unsigned i_mode, unsigned i_type )
{
    if( i_mode>=NUM_SORT_FNS || i_type>1 )
        return 0;
    return sorting_fns[i_mode][i_type];
}

/**
 * Sort an array of items recursively
 * @param i_items: number of items
 * @param pp_items: the array of items
 * @param p_sortfn: the sorting function
 * @return nothing
 */
static inline
void playlist_ItemArraySort( unsigned i_items, playlist_item_t **pp_items,
                             sortfn_t p_sortfn )
{
    if( p_sortfn )
    {
        qsort( pp_items, i_items, sizeof( pp_items[0] ), p_sortfn );
    }
    else /* Randomise */
    {
        unsigned i_position;
        unsigned i_new;
        playlist_item_t *p_temp;

        for( i_position = i_items - 1; i_position > 0; i_position-- )
        {
            i_new = ((unsigned)vlc_mrand48()) % (i_position+1);
            p_temp = pp_items[i_position];
            pp_items[i_position] = pp_items[i_new];
            pp_items[i_new] = p_temp;
        }
    }
}


/**
 * Sort a node recursively.
 * This function must be entered with the playlist lock !
 * @param p_playlist the playlist
 * @param p_node the node to sort
 * @param p_sortfn the sorting function
 * @return VLC_SUCCESS on success
 */
static int recursiveNodeSort( playlist_t *p_playlist, playlist_item_t *p_node,
                              sortfn_t p_sortfn )
{
    int i;
    playlist_ItemArraySort(p_node->i_children,p_node->pp_children,p_sortfn);
    for( i = 0 ; i< p_node->i_children; i++ )
    {
        if( p_node->pp_children[i]->i_children != -1 )
        {
            recursiveNodeSort( p_playlist, p_node->pp_children[i], p_sortfn );
        }
    }
    return VLC_SUCCESS;
}

/**
 * Collect all leaf items (non-node items) from a node recursively
 * This function must be entered with the playlist lock !
 */
static void collectLeafItems( playlist_item_t *p_node, playlist_item_t ***ppp_items, int *pi_count )
{
    int i;
    for( i = 0; i < p_node->i_children; i++ )
    {
        playlist_item_t *p_child = p_node->pp_children[i];
        if( p_child->i_children == -1 )
        {
            /* It's a leaf item, add it */
            *ppp_items = realloc( *ppp_items, (*pi_count + 1) * sizeof(playlist_item_t*) );
            (*ppp_items)[*pi_count] = p_child;
            (*pi_count)++;
        }
        else
        {
            /* It's a node, recurse */
            collectLeafItems( p_child, ppp_items, pi_count );
        }
    }
}

/**
 * Sort a node as a flat list (for file size sorting)
 * This function must be entered with the playlist lock !
 */
static int flatNodeSort( playlist_t *p_playlist, playlist_item_t *p_node,
                         sortfn_t p_sortfn )
{
    playlist_item_t **pp_items = NULL;
    int i_count = 0;
    int i;

    /* Collect all leaf items (hold references to them) */
    collectLeafItems( p_node, &pp_items, &i_count );

    if( i_count == 0 )
    {
        free( pp_items );
        /* Still need to delete any nodes that might be children */
        while( p_node->i_children > 0 )
        {
            playlist_item_t *p_child = p_node->pp_children[0];
            playlist_NodeDelete( p_playlist, p_child );
        }
        return VLC_SUCCESS;
    }

    /* Sort the collected items */
    playlist_ItemArraySort( i_count, pp_items, p_sortfn );

    /* Remove all children from the node, deleting nodes but keeping leaf items */
    while( p_node->i_children > 0 )
    {
        playlist_item_t *p_child = p_node->pp_children[0];
        /* If it's a node, delete it (which will also delete its children) */
        if( p_child->i_children != -1 )
        {
            playlist_NodeDelete( p_playlist, p_child );
        }
        else
        {
            /* It's a leaf item, just remove it from parent (we'll re-add it) */
            TAB_REMOVE( p_node->i_children, p_node->pp_children, p_child );
            p_child->p_parent = NULL;
        }
    }

    /* Add sorted items back as direct children */
    for( i = 0; i < i_count; i++ )
    {
        /* Only insert if item doesn't already have a parent */
        if( pp_items[i]->p_parent == NULL )
        {
            playlist_NodeInsert( p_node, pp_items[i], PLAYLIST_END );
        }
    }

    free( pp_items );
    return VLC_SUCCESS;
}

/**
 * Sort a node recursively.
 *
 * This function must be entered with the playlist lock !
 *
 * \param p_playlist the playlist
 * \param p_node the node to sort
 * \param i_mode: a SORT_* constant indicating the field to sort on
 * \param i_type: ORDER_NORMAL or ORDER_REVERSE (reversed order)
 * \return VLC_SUCCESS on success
 */
int playlist_RecursiveNodeSort( playlist_t *p_playlist, playlist_item_t *p_node,
                                int i_mode, int i_type )
{
    PL_ASSERT_LOCKED;

    /* Ask the playlist to reset as we are changing the order */
    pl_priv(p_playlist)->b_reset_currently_playing = true;

    /* For file size sorting, treat as flat list */
    if( i_mode == SORT_FILE_SIZE )
    {
        return flatNodeSort( p_playlist, p_node, find_sorting_fn(i_mode,i_type) );
    }

    /* Do the real job recursively */
    return recursiveNodeSort(p_playlist,p_node,find_sorting_fn(i_mode,i_type));
}


/* This is the stuff the sorting functions are made of. The proto_##
 * functions are wrapped in cmp_a_## and cmp_d_## functions that do
 * void * to const playlist_item_t * casting and dereferencing and
 * cmp_d_## inverts the result, too. proto_## are static inline,
 * cmp_[ad]_## are merely static as they're the target of pointers.
 *
 * In any case, each SORT_## constant (except SORT_RANDOM) must have
 * a matching SORTFN( )-declared function here.
 */

#define SORTFN( SORT, first, second ) static inline int proto_##SORT \
    ( const playlist_item_t *first, const playlist_item_t *second )

SORTFN( SORT_TRACK_NUMBER, first, second )
{
    return meta_sort( first, second, vlc_meta_TrackNumber, true );
}

SORTFN( SORT_DISC_NUMBER, first, second )
{
    int i_ret = meta_sort( first, second, vlc_meta_DiscNumber, true );
    /* Items came from the same disc: compare the track numbers */
    if( i_ret == 0 )
        i_ret = proto_SORT_TRACK_NUMBER( first, second );

    return i_ret;
}

SORTFN( SORT_ALBUM, first, second )
{
    int i_ret = meta_sort( first, second, vlc_meta_Album, false );
    /* Items came from the same album: compare the disc numbers */
    if( i_ret == 0 )
        i_ret = proto_SORT_DISC_NUMBER( first, second );

    return i_ret;
}

SORTFN( SORT_DATE, first, second )
{
    int i_ret = meta_sort( first, second, vlc_meta_Date, true );
    /* Items came from the same date: compare the albums */
    if( i_ret == 0 )
        i_ret = proto_SORT_ALBUM( first, second );

    return i_ret;
}

SORTFN( SORT_ARTIST, first, second )
{
    int i_ret = meta_sort( first, second, vlc_meta_Artist, false );
    /* Items came from the same artist: compare the dates */
    if( i_ret == 0 )
        i_ret = proto_SORT_DATE( first, second );

    return i_ret;
}

SORTFN( SORT_DESCRIPTION, first, second )
{
    return meta_sort( first, second, vlc_meta_Description, false );
}

SORTFN( SORT_DURATION, first, second )
{
    vlc_tick_t time1 = input_item_GetDuration( first->p_input );
    vlc_tick_t time2 = input_item_GetDuration( second->p_input );
    int i_ret = time1 > time2 ? 1 :
                    ( time1 == time2 ? 0 : -1 );
    return i_ret;
}

SORTFN( SORT_GENRE, first, second )
{
    return meta_sort( first, second, vlc_meta_Genre, false );
}

SORTFN( SORT_ID, first, second )
{
    return first->i_id - second->i_id;
}

SORTFN( SORT_RATING, first, second )
{
    return meta_sort( first, second, vlc_meta_Rating, true );
}

SORTFN( SORT_TITLE, first, second )
{
    return meta_strcasecmp_title( first, second );
}

SORTFN( SORT_TITLE_NODES_FIRST, first, second )
{
    /* If first is a node but not second */
    if( first->i_children == -1 && second->i_children >= 0 )
        return -1;
    /* If second is a node but not first */
    else if( first->i_children >= 0 && second->i_children == -1 )
        return 1;
    /* Both are nodes or both are not nodes */
    else
        return meta_strcasecmp_title( first, second );
}

SORTFN( SORT_TITLE_NUMERIC, first, second )
{
    int i_ret;
    char *psz_first = input_item_GetTitleFbName( first->p_input );
    char *psz_second = input_item_GetTitleFbName( second->p_input );

    if( psz_first && psz_second )
        i_ret = atoi( psz_first ) - atoi( psz_second );
    else if( !psz_first && psz_second )
        i_ret = 1;
    else if( psz_first && !psz_second )
        i_ret = -1;
    else
        i_ret = 0;

    free( psz_first );
    free( psz_second );
    return i_ret;
}

SORTFN( SORT_URI, first, second )
{
    int i_ret;
    char *psz_first = input_item_GetURI( first->p_input );
    char *psz_second = input_item_GetURI( second->p_input );

    if( psz_first && psz_second )
        i_ret = strcasecmp( psz_first, psz_second );
    else if( !psz_first && psz_second )
        i_ret = 1;
    else if( psz_first && !psz_second )
        i_ret = -1;
    else
        i_ret = 0;

    free( psz_first );
    free( psz_second );
    return i_ret;
}

SORTFN( SORT_FILE_SIZE, first, second )
{
    int64_t i_size_first = 0;
    int64_t i_size_second = 0;
    char *psz_first = input_item_GetURI( first->p_input );
    char *psz_second = input_item_GetURI( second->p_input );

    /* Nodes go first */
    if( first->i_children == -1 && second->i_children >= 0 )
        i_size_first = 1;
    else if( first->i_children >= 0 && second->i_children == -1 )
        i_size_second = 1;
    /* Both are nodes, sort by name */
    else if( first->i_children >= 0 && second->i_children >= 0 )
    {
        free( psz_first );
        free( psz_second );
        return meta_strcasecmp_title( first, second );
    }
    /* Both are items, get file sizes */
    else if( psz_first && psz_second )
    {
        /* Convert URIs to file paths if needed */
        char *psz_path_first = vlc_uri2path( psz_first );
        char *psz_path_second = vlc_uri2path( psz_second );
        
        /* If URI conversion failed, try using URI directly */
        if( !psz_path_first )
            psz_path_first = strdup( psz_first );
        if( !psz_path_second )
            psz_path_second = strdup( psz_second );
        
        /* Try to get file size from file system */
        struct stat st_first, st_second;
        if( psz_path_first && vlc_stat( psz_path_first, &st_first ) == 0 && S_ISREG( st_first.st_mode ) )
            i_size_first = st_first.st_size;
        if( psz_path_second && vlc_stat( psz_path_second, &st_second ) == 0 && S_ISREG( st_second.st_mode ) )
            i_size_second = st_second.st_size;
            
        free( psz_path_first );
        free( psz_path_second );
    }

    free( psz_first );
    free( psz_second );

    if( i_size_first < i_size_second )
        return -1;
    else if( i_size_first > i_size_second )
        return 1;
    else
        return 0;
}

#undef  SORTFN

/* Generate stubs around the proto_## sorting functions, ascending and
 * descending both. Preprocessor magic up ahead. Brace yourself.
 */

#ifndef VLC_DEFINE_SORT_FUNCTIONS
#error  Where is VLC_DEFINE_SORT_FUNCTIONS?
#endif

#define DEF( s ) \
    static int cmp_a_##s(const void *l,const void *r) \
    { return proto_##s(*(const playlist_item_t *const *)l, \
                           *(const playlist_item_t *const *)r); } \
    static int cmp_d_##s(const void *l,const void *r) \
    { return -1*proto_##s(*(const playlist_item_t * const *)l, \
                              *(const playlist_item_t * const *)r); }

    VLC_DEFINE_SORT_FUNCTIONS

#undef  DEF

/* And populate an array with the addresses */

static const sortfn_t sorting_fns[NUM_SORT_FNS][2] =
#define DEF( a ) { cmp_a_##a, cmp_d_##a },
{ VLC_DEFINE_SORT_FUNCTIONS };
#undef  DEF
