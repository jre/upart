#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsdqueue.h"
#include "map.h"

static void map_freelist(struct up_part_list *list);

struct up_map *
up_map_new(struct up_part *parent, int64_t start, int64_t size,
           enum up_map_type type, void *priv, up_freepriv_map freepriv)
{
    struct up_map *map;

    map = calloc(1, sizeof *map);
    if(!map)
    {
        perror("malloc");
        return NULL;
    }

    map->start        = start;
    map->size         = size;
    map->type         = type;
    map->depth        = (parent ? parent->depth + 1 : 0);
    map->priv         = priv;
    map->freepriv     = freepriv;
    map->parent       = parent;
    SIMPLEQ_INIT(&map->list);
    if(parent)
        SIMPLEQ_INSERT_TAIL(&parent->submap, map, link);

    return map;
}

struct up_part *
up_map_add(struct up_map *map, struct up_part *parent, int64_t start,
           int64_t size, int type, const char *label, int flags,
           void *priv, up_freepriv_part freepriv)
{
    struct up_part *part;

    part = calloc(1, sizeof *part);
    if(!part)
    {
        perror("malloc");
        return NULL;
    }

    part->start       = start;
    part->size        = size;
    part->type        = type;
    part->label       = label;
    part->flags       = flags;
    part->depth       = (parent ? parent->depth + 1 : map->depth);
    part->priv        = priv;
    part->freepriv    = freepriv;
    part->map         = map;
    part->parent      = parent;
    SIMPLEQ_INIT(&part->subpart);
    SIMPLEQ_INIT(&part->submap);
    if(parent)
        SIMPLEQ_INSERT_TAIL(&parent->subpart, part, link);
    else
        SIMPLEQ_INSERT_TAIL(&map->list, part, link);

    return part;
}

void
up_map_free(struct up_map *map)
{
    if(!map)
        return;

    /* freeing a map with a parent isn't supported due to laziness */
    assert(!map->parent);

    map_freelist(&map->list);
    if(map->freepriv)
        map->freepriv(map);
    free(map);
}

static void
map_freelist(struct up_part_list *list)
{
    struct up_part     *ii;
    struct up_map      *jj;

    while((ii = SIMPLEQ_FIRST(list)))
    {
        SIMPLEQ_REMOVE_HEAD(list, link);
        map_freelist(&ii->subpart);
        while((jj = SIMPLEQ_FIRST(&ii->submap)))
        {
            SIMPLEQ_REMOVE_HEAD(&ii->submap, link);
            jj->parent = NULL;
            up_map_free(jj);
        }
        if(ii->priv && ii->freepriv)
            ii->freepriv(ii);
        free(ii);
    }
}

void
up_map_freeprivmap_def(struct up_map *map)
{
    free(map->priv);
}

void
up_map_freeprivpart_def(struct up_part *part)
{
    free(part->priv);
}

const struct up_part *
up_map_first(const struct up_map *map)
{
    return SIMPLEQ_FIRST(&map->list);
}

const struct up_part *
up_map_firstsub(const struct up_part *part)
{
    return SIMPLEQ_FIRST(&part->subpart);
}

const struct up_part *
up_map_next(const struct up_part *part)
{
    const struct up_part *next;

    /* first subpartition */
    if((next = up_map_firstsub(part)))
    {
        assert(part == next->parent);
        return next;
    }

    /* next peer partition */
    if((next = SIMPLEQ_NEXT(part, link)))
        return next;

    /* parent partition's next peer partition */
    if(part->parent && (next = SIMPLEQ_NEXT(part->parent, link)))
        return next;

    /* no more partitions */
    return NULL;
}

const struct up_map *
up_map_firstmap(const struct up_part *part)
{
    return SIMPLEQ_FIRST(&part->submap);
}

const struct up_map *
up_map_nextmap(const struct up_map *map)
{
    return SIMPLEQ_NEXT(map, link);
}
