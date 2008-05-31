#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdqueue.h"
#include "disk.h"
#include "map.h"
#include "util.h"

/* #define MAP_PROBE_DEBUG */

#define CHECKTYPE(typ) \
    assert(UP_MAP_NONE < (typ) && UP_MAP_TYPE_COUNT > (typ) && \
           UP_TYPE_REGISTERED & st_types[(typ)].flags)

struct up_map_funcs
{
    char *label;
    int flags;
    int (*load)(struct up_disk *, const struct up_part *, void **,
                const struct up_opts *);
    int (*setup)(struct up_map *, const struct up_opts *);
    int (*getinfo)(const struct up_map *, int, char *, int);
    int (*getindex)(const struct up_part *, char *, int);
    int (*getextrahdr)(const struct up_map *, int, char *, int);
    int (*getextra)(const struct up_part *, int, char *, int);
    int (*getdump)(const struct up_map *, int64_t, const void *, int64_t,
                   int, char *, int);
    void (*freeprivmap)(struct up_map *, void *);
    void (*freeprivpart)(struct up_part *, void *);
};

static int map_loadall(struct up_disk *disk, struct up_part *container,
                       const struct up_opts *opts);
static struct up_part *map_newcontainer(int64_t size);
static void map_freecontainer(struct up_part *container);
static struct up_map *map_new(struct up_disk *disk, struct up_part *parent,
                              enum up_map_type type, void *priv);
static void map_printcontainer(const struct up_part *container, FILE *stream,
                               int verbose);
static void map_indent(int depth, FILE *stream);

static struct up_map_funcs st_types[UP_MAP_TYPE_COUNT];

void
up_map_register(enum up_map_type type, const char *label, int flags,
                int (*load)(struct up_disk *, const struct up_part *, void **,
                            const struct up_opts *),
                int (*setup)(struct up_map *, const struct up_opts *),
                int (*getinfo)(const struct up_map *, int, char *, int),
                int (*getindex)(const struct up_part *, char *, int),
                int (*getextrahdr)(const struct up_map *, int, char *, int),
                int (*getextra)(const struct up_part *, int, char *, int),
                int (*getdumpextra)(const struct up_map *, int64_t, const void *,
                                int64_t, int, char *, int),
                void (*freeprivmap)(struct up_map *, void *),
                void (*freeprivpart)(struct up_part *, void *))
{
    struct up_map_funcs *funcs;

    assert(UP_MAP_NONE < type && UP_MAP_TYPE_COUNT > type);
    assert(!(UP_TYPE_REGISTERED & st_types[type].flags));
    assert(!(UP_TYPE_REGISTERED & flags));

    funcs                     = &st_types[type];
    funcs->label              = strdup(label);
    funcs->flags              = UP_TYPE_REGISTERED | flags;
    funcs->load               = load;
    funcs->setup              = setup;
    funcs->getinfo            = getinfo;
    funcs->getindex           = getindex;
    funcs->getextrahdr        = getextrahdr;
    funcs->getextra           = getextra;
    funcs->getdump            = getdumpextra;
    funcs->freeprivmap        = freeprivmap;
    funcs->freeprivpart       = freeprivpart;
}

int
up_map_load(struct up_disk *disk, struct up_part *parent,
            enum up_map_type type, struct up_map **mapret,
            const struct up_opts *opts)
{
    struct up_map_funcs*funcs;
    void               *priv;
    struct up_map      *map;
    int                 res;

    CHECKTYPE(type);
    assert(0 <= parent->start && 0 <= parent->size &&
           parent->start + parent->size <= UP_DISK_SIZESECTS(disk));

    funcs     = &st_types[type];
    *mapret   = NULL;
    priv      = NULL;

#ifdef MAP_PROBE_DEBUG
    fprintf(stderr, "probe %"PRId64" %s\n", parent->start, funcs->label);
#endif
    switch(funcs->load(disk, parent, &priv, opts))
    {
        case 1:
#ifdef MAP_PROBE_DEBUG
            fprintf(stderr, "matched %"PRId64" %s\n",
                    parent->start, funcs->label);
#endif
            map = map_new(disk, parent, type, priv);
            if(!map)
            {
                if(funcs->freeprivmap && priv)
                    funcs->freeprivmap(NULL, priv);
                return -1;
            }
            map->parent = parent; /* XXX this is so broken */
            res = funcs->setup(map, opts);
            if(0 >= res)
            {
#ifdef MAP_PROBE_DEBUG
                fprintf(stderr, "setup failed\n");
#endif
                map->parent = NULL;
                up_map_free(map);
                return res;
            }
            SIMPLEQ_INSERT_TAIL(&parent->submap, map, link);
            *mapret = map;
            return 1;

        case 0:
            assert(NULL == priv);
            return 0;

        default:
#ifdef MAP_PROBE_DEBUG
            fprintf(stderr, "error\n");
#endif
            assert(NULL == priv);
            return -1;
    }
}

int
up_map_loadall(struct up_disk *disk, const struct up_opts *opts)
{
    assert(!disk->maps);

    disk->maps = map_newcontainer(UP_DISK_SIZESECTS(disk));
    if(!disk->maps)
        return -1;

    if(0 > map_loadall(disk, disk->maps, opts))
    {
        up_map_freeall(disk);
        return -1;
    }

    return 0;
}

static int
map_loadall(struct up_disk *disk, struct up_part *container,
            const struct up_opts *opts)
{
    enum up_map_type    type;
    struct up_map      *map;
    struct up_part     *ii;

    /* iterate through all partition types */
    for(type = UP_MAP_NONE + 1; UP_MAP_TYPE_COUNT > type; type++)
    {
        CHECKTYPE(type);

        /* try to load a map of this type */
        if(0 > up_map_load(disk, container, type, &map, opts))
            return -1;

        if(!map)
            continue;

        /* try to recurse on each partition in the map */
        for(ii = SIMPLEQ_FIRST(&map->list); ii; ii = SIMPLEQ_NEXT(ii, link))
            if(!UP_PART_IS_BAD(ii->flags) && map_loadall(disk, ii, opts))
                return -1;
    }

    return 0;
}

void
up_map_freeall(struct up_disk *disk)
{
    if(!disk->maps)
        return;

    map_freecontainer(disk->maps);
    free(disk->maps);
    disk->maps = NULL;
}

static struct up_map *
map_new(struct up_disk *disk, struct up_part *parent,
        enum up_map_type type, void *priv)
{
    struct up_map *map;

    map = calloc(1, sizeof *map);
    if(!map)
    {
        perror("malloc");
        return NULL;
    }

    map->disk         = disk;
    map->type         = type;
    map->start        = parent->start;
    map->size         = parent->size;
    map->depth        = (parent->map ? parent->map->depth + 1 : 0);
    map->priv         = priv;
    SIMPLEQ_INIT(&map->list);

    return map;
}

static struct up_part *
map_newcontainer(int64_t size)
{
    struct up_part *container;

    container = calloc(1, sizeof *container);
    if(!container)
    {
        perror("malloc");
        return NULL;
    }

    container->start  = 0;
    container->size   = size;
    SIMPLEQ_INIT(&container->submap);

    return container;
}

static void
map_freecontainer(struct up_part *container)
{
    struct up_map *ii;

    while((ii = SIMPLEQ_FIRST(&container->submap)))
    {
        SIMPLEQ_REMOVE_HEAD(&container->submap, link);
        ii->parent = NULL;
        up_map_free(ii);
    }
}

struct up_part *
up_map_add(struct up_map *map, int64_t start, int64_t size,
           int flags, void *priv)
{
    struct up_part *part;

    part = calloc(1, sizeof *part);
    if(!part)
    {
        perror("malloc");
        return NULL;
    }

    if(0 == size)
        flags |= UP_PART_EMPTY;
    if(start < map->start || start + size > map->start + map->size)
        flags |= UP_PART_OOB;

    part->start       = start;
    part->size        = size;
    part->flags       = flags;
    part->priv        = priv;
    part->map         = map;
    SIMPLEQ_INIT(&part->submap);
    SIMPLEQ_INSERT_TAIL(&map->list, part, link);

    return part;
}

void
up_map_free(struct up_map *map)
{
    struct up_part *ii;

    if(!map)
        return;

    CHECKTYPE(map->type);
    /* freeing a map with a parent isn't supported due to laziness */
    assert(!map->parent);

    /* free partitions */
    while((ii = SIMPLEQ_FIRST(&map->list)))
    {
        SIMPLEQ_REMOVE_HEAD(&map->list, link);
        map_freecontainer(ii);
        if(st_types[ii->map->type].freeprivpart && ii->priv)
            st_types[ii->map->type].freeprivpart(ii, ii->priv);
        free(ii);
    }

    /* free private data */
    if(st_types[map->type].freeprivmap && map->priv)
        st_types[map->type].freeprivmap(map, map->priv);

    /* mark sectors unused */
    up_disk_sectsunref(map->disk, map);

    /* free map */
    free(map);
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

const char *
up_map_label(const struct up_map *map)
{
    CHECKTYPE(map->type);

    return st_types[map->type].label;
}

void
up_map_print(const struct up_map *map, void *_stream, int verbose, int recurse)
{
    FILE                       *stream = _stream;
    struct up_map_funcs        *funcs;
    char                        buf[512], idx[5], flag;
    const struct up_part       *ii;
    int                         len;

    CHECKTYPE(map->type);

    funcs = &st_types[map->type];

    /* print info line */
    if(funcs->getinfo)
    {
        len = funcs->getinfo(map, verbose, buf, sizeof buf);
        if(len)
        {
            map_indent(map->depth, stream);
            fputs(buf, stream);
            putc('\n', stream);
        }
    }

    /* print the header line */
    if(!(UP_TYPE_NOPRINTHDR & funcs->flags))
    {
        /* extra */
        len = 0;
        if(funcs->getextrahdr)
            len = funcs->getextrahdr(map, verbose, buf, sizeof buf);
        else if(funcs->getextra)
            len = funcs->getextra(NULL, verbose, buf, sizeof buf);

        /* print */
        if(UP_NOISY(verbose, NORMAL) || len)
            map_indent(map->depth, stream);
        if(UP_NOISY(verbose, NORMAL))
            fputs("                 Start            Size", stream);
        if(len)
        {
            putc(' ', stream);
            fputs(buf, stream);
        }
        if(UP_NOISY(verbose, NORMAL) || len)
            putc('\n', stream);
    }

    /* print partitions */
    for(ii = up_map_first(map); ii; ii = up_map_next(ii))
    {
        /* skip empty partitions unless verbose */
        if(UP_PART_EMPTY & ii->flags && !UP_NOISY(verbose, EXTRA))
            continue;

        /* flags */
        if(UP_PART_IS_BAD(ii->flags))
            flag = 'X';
        else
            flag = ' ';

        /* index */
        idx[0] = 0;
        funcs->getindex(ii, idx, sizeof idx - 1);
        strlcat(idx, ":", sizeof idx);

        /* extra */
        len = 0;
        if(funcs->getextra)
            len = funcs->getextra(ii, verbose, buf, sizeof buf);

        /* print */
        if(UP_NOISY(verbose, NORMAL) || len)
            map_indent(map->depth, stream);
        if(UP_NOISY(verbose, NORMAL))
            fprintf(stream, "%-4s %c %15"PRId64" %15"PRId64,
                    idx, flag, ii->start, ii->size);
        if(len)
        {
            putc(' ', stream);
            fputs(buf, stream);
        }
        if(UP_NOISY(verbose, NORMAL) || len)
            putc('\n', stream);

        /* recurse */
        map_printcontainer(ii, stream, verbose);
    }
}

void
up_map_printall(const struct up_disk *disk, void *stream, int verbose)
{
    map_printcontainer(disk->maps, stream, verbose);
}

static void
map_printcontainer(const struct up_part *container, FILE *stream, int verbose)
{
    const struct up_map *ii;

    for(ii = up_map_firstmap(container); ii; ii = up_map_nextmap(ii))
    {
        up_map_print(ii, stream, verbose, 1);
    }
}

static void
map_indent(int depth, FILE *stream)
{
    int ii;

    for(ii = 0; depth > ii; ii++)
        putc(' ', stream);
}

void
up_map_dumpsect(const struct up_map *map, void *_stream, int64_t start,
                int64_t size, const void *data, int tag)
{
    FILE   *stream = _stream;
    char    buf[512];

    CHECKTYPE(map->type);

    buf[0] = 0;
    if(st_types[map->type].getdump)
        st_types[map->type].getdump(map, start, data, size,
                                     tag, buf, sizeof buf);

    fprintf(stream, "\n\nDump of %s %s at sector %"PRId64" (0x%"PRIx64")%s:\n",
            UP_DISK_PATH(map->disk), st_types[map->type].label,
            start, start, buf);

    up_hexdump(data, UP_DISK_1SECT(map->disk) * size,
               UP_DISK_1SECT(map->disk) * start, stream);
}

const struct up_part *
up_map_first(const struct up_map *map)
{
    return SIMPLEQ_FIRST(&map->list);
}

const struct up_part *
up_map_next(const struct up_part *part)
{
    return SIMPLEQ_NEXT(part, link);
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
