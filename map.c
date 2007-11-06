#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "bsdqueue.h"
#include "map.h"

static void map_freelist(struct up_map *map, struct up_part_list *list);

struct up_map *
up_map_new(int64_t start, int64_t size, enum up_map_type type, void *priv,
           up_freepriv_map freepriv)
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
    map->priv         = priv;
    map->freepriv     = freepriv;
    SIMPLEQ_INIT(&map->list);

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
    part->priv        = priv;
    part->freepriv    = freepriv;
    SIMPLEQ_INIT(&part->children);
    if(parent)
        SIMPLEQ_INSERT_TAIL(&parent->children, part, link);
    else
        SIMPLEQ_INSERT_TAIL(&map->list, part, link);

    return part;
}

void
up_map_free(struct up_map *map)
{
    if(!map)
        return;
    map_freelist(map, &map->list);
    if(map->freepriv)
        map->freepriv(map);
    free(map);
}

static void
map_freelist(struct up_map *map, struct up_part_list *list)
{
    struct up_part *ii;

    while((ii = SIMPLEQ_FIRST(list)))
    {
        SIMPLEQ_REMOVE_HEAD(list, link);
        map_freelist(map, &ii->children);
        if(ii->priv && ii->freepriv)
            ii->freepriv(map, ii);
        free(ii);
    }
}

void
up_map_freeprivmap_def(struct up_map *map)
{
    free(map->priv);
}

void
up_map_freeprivpart_def(struct up_map *map, struct up_part *part)
{
    free(part->priv);
}
