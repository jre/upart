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
    assert(UP_MAP_NONE < (typ) && UP_MAP_ID_COUNT > (typ) && \
           UP_TYPE_REGISTERED & st_types[(typ)].flags)

struct map_funcs {
	char *label;
	int flags;
	int (*load)(const struct disk *, const struct part *, void **);
	int (*setup)(struct disk *, struct map *);
	int (*getinfo)(const struct map *, char *, int);
	int (*getindex)(const struct part *, char *, int);
	int (*getextrahdr)(const struct map *, char *, int);
	int (*getextra)(const struct part *, char *, int);
	int (*getdump)(const struct map *, int64_t, const void *, int64_t, int,
	    char *, int);
	void (*freeprivmap)(struct map *, void *);
	void (*freeprivpart)(struct part *, void *);
};

static int		 map_loadall(struct disk *, struct part *);
static struct part	*map_newcontainer(int64_t);
static void		 map_freecontainer(struct disk *, struct part *);
static struct map	*map_new(struct disk *, struct part *, enum mapid,
    void *);
static void		 map_printcontainer(const struct part *, FILE *);
static void		 map_indent(int, FILE *);

static struct map_funcs st_types[UP_MAP_ID_COUNT];

void
up_map_register(enum mapid type, const char *label, int flags,
    int (*load)(const struct disk *, const struct part *, void **),
    int (*setup)(struct disk *, struct map *),
    int (*getinfo)(const struct map *, char *, int),
    int (*getindex)(const struct part *, char *, int),
    int (*getextrahdr)(const struct map *, char *, int),
    int (*getextra)(const struct part *, char *, int),
    int (*getdumpextra)(const struct map *, int64_t, const void *, int64_t,
	int, char *, int),
    void (*freeprivmap)(struct map *, void *),
    void (*freeprivpart)(struct part *, void *))
{
	struct map_funcs *funcs;

	assert(type > UP_MAP_NONE && type < UP_MAP_ID_COUNT);
	assert(!(UP_TYPE_REGISTERED & st_types[type].flags));
	assert(!(UP_TYPE_REGISTERED & flags));

	funcs = &st_types[type];
	funcs->label = strdup(label);
	funcs->flags = UP_TYPE_REGISTERED | flags;
	funcs->load = load;
	funcs->setup = setup;
	funcs->getinfo = getinfo;
	funcs->getindex = getindex;
	funcs->getextrahdr = getextrahdr;
	funcs->getextra = getextra;
	funcs->getdump = getdumpextra;
	funcs->freeprivmap = freeprivmap;
	funcs->freeprivpart = freeprivpart;
}

int
up_map_load(struct disk *disk, struct part *parent, enum mapid type,
    struct map **ret)
{
	struct map_funcs *funcs;
	void *priv;
	struct map *map;
	int res;

	CHECKTYPE(type);
	assert(parent->start >= 0 && parent->size >= 0 &&
	    parent->start + parent->size <= UP_DISK_SIZESECTS(disk));

	funcs = &st_types[type];
	*ret = NULL;
	priv = NULL;

#ifdef MAP_PROBE_DEBUG
	fprintf(stderr, "probe %"PRId64" %s\n", parent->start, funcs->label);
#endif
	switch (funcs->load(disk, parent, &priv)) {
	case 1:
#ifdef MAP_PROBE_DEBUG
		fprintf(stderr, "matched %"PRId64" %s\n",
		    parent->start, funcs->label);
#endif
		map = map_new(disk, parent, type, priv);
		if (map == NULL) {
			if (funcs->freeprivmap && priv)
				funcs->freeprivmap(NULL, priv);
			return (-1);
		}
		map->parent = parent; /* XXX this is so broken */
		res = funcs->setup(disk, map);
		if (res <= 0) {
#ifdef MAP_PROBE_DEBUG
			fprintf(stderr, "setup failed\n");
#endif
			map->parent = NULL;
			up_map_free(disk, map);
			return (res);
		}
		SIMPLEQ_INSERT_TAIL(&parent->submap, map, link);
		*ret = map;
		return (1);

	case 0:
		assert(priv == NULL);
		return (0);

	default:
#ifdef MAP_PROBE_DEBUG
		fprintf(stderr, "error\n");
#endif
		assert(priv == NULL);
		return (-1);
	}
}

int
up_map_loadall(struct disk *disk)
{
	assert(disk->maps == NULL);

	disk->maps = map_newcontainer(UP_DISK_SIZESECTS(disk));
	if(disk->maps == NULL)
		return (-1);

	if(map_loadall(disk, disk->maps) < 0) {
		up_map_freeall(disk);
		return (-1);
	}

	return (0);
}

static int
map_loadall(struct disk *disk, struct part *container)
{
    enum mapid    type;
    struct map      *map;
    struct part     *ii;

    /* iterate through all partition types */
    for(type = UP_MAP_NONE + 1; UP_MAP_ID_COUNT > type; type++)
    {
        CHECKTYPE(type);

        /* try to load a map of this type */
        if(0 > up_map_load(disk, container, type, &map))
            return -1;

        if(!map)
            continue;

        /* try to recurse on each partition in the map */
        for(ii = SIMPLEQ_FIRST(&map->list); ii; ii = SIMPLEQ_NEXT(ii, link))
            if(!UP_PART_IS_BAD(ii->flags) && map_loadall(disk, ii))
                return -1;
    }

    return 0;
}

void
up_map_freeall(struct disk *disk)
{
    if(!disk->maps)
        return;

    map_freecontainer(disk, disk->maps);
    free(disk->maps);
    disk->maps = NULL;
}

static struct map *
map_new(struct disk *disk, struct part *parent,
        enum mapid type, void *priv)
{
    struct map *map;

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

static struct part *
map_newcontainer(int64_t size)
{
    struct part *container;

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
map_freecontainer(struct disk *disk, struct part *container)
{
    struct map *ii;

    while((ii = SIMPLEQ_FIRST(&container->submap)))
    {
        SIMPLEQ_REMOVE_HEAD(&container->submap, link);
        ii->parent = NULL;
        up_map_free(disk, ii);
    }
}

struct part *
up_map_add(struct map *map, int64_t start, int64_t size,
           int flags, void *priv)
{
    struct part *part;

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
up_map_free(struct disk *disk, struct map *map)
{
    struct part *ii;

    if(!map)
        return;

    CHECKTYPE(map->type);
    /* freeing a map with a parent isn't supported due to laziness */
    assert(!map->parent);

    /* free partitions */
    while((ii = SIMPLEQ_FIRST(&map->list)))
    {
        SIMPLEQ_REMOVE_HEAD(&map->list, link);
        map_freecontainer(disk, ii);
        if(st_types[ii->map->type].freeprivpart && ii->priv)
            st_types[ii->map->type].freeprivpart(ii, ii->priv);
        free(ii);
    }

    /* free private data */
    if(st_types[map->type].freeprivmap && map->priv)
        st_types[map->type].freeprivmap(map, map->priv);

    /* mark sectors unused */
    up_disk_sectsunref(disk, map);

    /* free map */
    free(map);
}

void
up_map_freeprivmap_def(struct map *map, void *priv)
{
    free(priv);
}

void
up_map_freeprivpart_def(struct part *part, void *priv)
{
    free(priv);
}

const char *
up_map_label(const struct map *map)
{
    CHECKTYPE(map->type);

    return st_types[map->type].label;
}

void
up_map_print(const struct map *map, void *_stream, int recurse)
{
    FILE                       *stream = _stream;
    struct map_funcs        *funcs;
    char                        buf[512], idx[5], flag;
    const struct part       *ii;
    int                         len;

    CHECKTYPE(map->type);

    funcs = &st_types[map->type];

    /* print info line */
    if(funcs->getinfo)
    {
        len = funcs->getinfo(map, buf, sizeof buf);
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
            len = funcs->getextrahdr(map, buf, sizeof buf);
        else if(funcs->getextra)
            len = funcs->getextra(NULL, buf, sizeof buf);

        /* print */
        if(UP_NOISY(NORMAL) || len)
            map_indent(map->depth, stream);
        if(UP_NOISY(NORMAL))
            fputs("                 Start            Size", stream);
        if(len)
        {
            putc(' ', stream);
            fputs(buf, stream);
        }
        if(UP_NOISY(NORMAL) || len)
            putc('\n', stream);
    }

    /* print partitions */
    for(ii = up_map_first(map); ii; ii = up_map_next(ii))
    {
        /* skip empty partitions unless verbose */
        if(UP_PART_EMPTY & ii->flags && !UP_NOISY(EXTRA))
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
            len = funcs->getextra(ii, buf, sizeof buf);

        /* print */
        if(UP_NOISY(NORMAL) || len)
            map_indent(map->depth, stream);
        if(UP_NOISY(NORMAL))
            fprintf(stream, "%-4s %c %15"PRId64" %15"PRId64,
                    idx, flag, ii->start, ii->size);
        if(len)
        {
            putc(' ', stream);
            fputs(buf, stream);
        }
        if(UP_NOISY(NORMAL) || len)
            putc('\n', stream);

        /* recurse */
        if(recurse)
            map_printcontainer(ii, stream);
    }
}

void
up_map_printall(const struct disk *disk, void *stream)
{
	map_printcontainer(disk->maps, stream);
}

static void
map_printcontainer(const struct part *container, FILE *stream)
{
	const struct map *ii;

	for (ii = up_map_firstmap(container); ii; ii = up_map_nextmap(ii))
		up_map_print(ii, stream, 1);
}

static void
map_indent(int depth, FILE *stream)
{
    int ii;

    for(ii = 0; depth > ii; ii++)
        putc(' ', stream);
}

void
up_map_dumpsect(const struct map *map, void *_stream, int64_t start,
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

const struct part *
up_map_first(const struct map *map)
{
    return SIMPLEQ_FIRST(&map->list);
}

const struct part *
up_map_next(const struct part *part)
{
    return SIMPLEQ_NEXT(part, link);
}

const struct map *
up_map_firstmap(const struct part *part)
{
    return SIMPLEQ_FIRST(&part->submap);
}

const struct map *
up_map_nextmap(const struct map *map)
{
    return SIMPLEQ_NEXT(map, link);
}
