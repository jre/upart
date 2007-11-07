#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdqueue.h"
#include "disk.h"
#include "map.h"

#define CHECKTYPE(typ) \
    assert(UP_MAP_NONE < (typ) && UP_MAP_TYPE_COUNT > (typ) && \
           st_types[(typ)].registered)

struct up_map_funcs
{
    int registered;
    const char *typestr;
    int (*load)(struct up_disk *, int64_t, int64_t, void **);
    int (*setup)(struct up_disk *, struct up_map *);
    int (*getindex)(const struct up_part *, char *, int);
    int (*getlabel)(const struct up_part *, char *, int);
    int (*getextra)(const struct up_part *, int, char *, int);
    void (*freeprivmap)(struct up_map *, void *);
    void (*freeprivpart)(struct up_part *, void *);
};

static struct up_map *map_new(struct up_part *parent, int64_t start,
                              int64_t size, enum up_map_type type, void *priv);
static void map_freelist(struct up_part_list *list);

static struct up_map_funcs st_types[UP_MAP_TYPE_COUNT];

void
up_map_register(enum up_map_type type, const char *typestr,
                int (*load)(struct up_disk *, int64_t, int64_t, void **),
                int (*setup)(struct up_disk *, struct up_map *),
                int (*getindex)(const struct up_part *, char *, int),
                int (*getlabel)(const struct up_part *, char *, int),
                int (*getextra)(const struct up_part *, int, char *, int),
                void (*freeprivmap)(struct up_map *, void *),
                void (*freeprivpart)(struct up_part *, void *))
{
    struct up_map_funcs *funcs;

    assert(UP_MAP_NONE < type && UP_MAP_TYPE_COUNT > type);
    assert(!st_types[type].registered);

    funcs                     = &st_types[type];
    funcs->registered         = 1;
    funcs->typestr            = typestr;
    funcs->load               = load;
    funcs->setup              = setup;
    funcs->getindex           = getindex;
    funcs->getlabel           = getlabel;
    funcs->getextra           = getextra;
    funcs->freeprivmap        = freeprivmap;
    funcs->freeprivpart       = freeprivpart;
}

int
up_map_load(struct up_disk *disk, struct up_part *parent, int64_t start,
            int64_t size, enum up_map_type type, struct up_map **mapret)
{
    struct up_map_funcs*funcs;
    void               *priv;
    struct up_map      *map;

    CHECKTYPE(type);
    assert(0 <= start && 0 <= size && start + size <= disk->upd_size);

    funcs     = &st_types[type];
    *mapret   = NULL;
    priv      = NULL;

    switch(funcs->load(disk, start, size, &priv))
    {
        case 1:
            map = map_new(parent, start, size, type, priv);
            if(!map)
            {
                if(funcs->freeprivmap && priv)
                    funcs->freeprivmap(NULL, priv);
                return -1;
            }
            if(0 > funcs->setup(disk, map))
            {
                up_map_free(map);
                return -1;
            }
            *mapret = map;
            return 1;

        case 0:
            assert(NULL == priv);
            return 0;

        default:
            assert(NULL == priv);
            return -1;
    }
}

int
up_map_loadall(struct up_disk *disk, struct up_part *container)
{
    enum up_map_type    type;
    struct up_map      *map;

    memset(container, 0, sizeof *container);
    SIMPLEQ_INIT(&container->submap);

    for(type = UP_MAP_NONE + 1; UP_MAP_TYPE_COUNT > type; type++)
    {
        if(!st_types[type].registered)
            continue;
        if(0 > up_map_load(disk, container, 0, disk->upd_size, type, &map))
        {
            up_map_freeall(container);
            return -1;
        }
    }

    return (SIMPLEQ_FIRST(&container->submap) ? 1 : 0);
}

void
up_map_freeall(struct up_part *container)
{
    struct up_map *ii;

    while((ii = SIMPLEQ_FIRST(&container->submap)))
    {
        SIMPLEQ_REMOVE_HEAD(&container->submap, link);
        ii->parent = NULL;
        up_map_free(ii);
    }
}

static struct up_map *
map_new(struct up_part *parent, int64_t start, int64_t size,
           enum up_map_type type, void *priv)
{
    struct up_map *map;

    map = calloc(1, sizeof *map);
    if(!map)
    {
        perror("malloc");
        return NULL;
    }

    map->type         = type;
    map->start        = start;
    map->size         = size;
    map->depth        = (parent ? parent->depth + 1 : 0);
    map->priv         = priv;
    map->parent       = parent;
    SIMPLEQ_INIT(&map->list);
    if(parent)
        SIMPLEQ_INSERT_TAIL(&parent->submap, map, link);

    return map;
}

struct up_part *
up_map_add(struct up_map *map, struct up_part *parent, int64_t start,
           int64_t size, int flags, void *priv)
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
    part->flags       = flags;
    part->depth       = (parent ? parent->depth + 1 : map->depth);
    part->priv        = priv;
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

    CHECKTYPE(map->type);
    /* freeing a map with a parent isn't supported due to laziness */
    assert(!map->parent);

    map_freelist(&map->list);
    if(st_types[map->type].freeprivmap && map->priv)
        st_types[map->type].freeprivmap(map, map->priv);
    free(map);
}

static void
map_freelist(struct up_part_list *list)
{
    struct up_part     *ii;

    while((ii = SIMPLEQ_FIRST(list)))
    {
        CHECKTYPE(ii->map->type);
        SIMPLEQ_REMOVE_HEAD(list, link);
        map_freelist(&ii->subpart);
        up_map_freeall(ii);
        if(st_types[ii->map->type].freeprivpart && ii->priv)
            st_types[ii->map->type].freeprivpart(ii, ii->priv);
        free(ii);
    }
}

void
up_map_freeprivmap_def(struct up_map *map, void *priv)
{
    free(priv);
}

void
up_map_freeprivpart_def(struct up_part *part, void *priv)
{
    free(priv);
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
