/* 
 * Copyright (c) 2007-2012 Joshua R. Elsasser.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

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

static int		 map_loadall(struct disk *, struct part *);
static struct part	*map_newcontainer(int64_t);
static void		 map_freecontainer(struct disk *, struct part *);
static struct map	*map_new(struct disk *, struct part *, enum mapid,
    void *);
static void		 map_print(const struct map *, int, FILE *);
static void		 map_printcontainer(const struct part *, int, FILE *);
static void		 map_indent(int, FILE *);

static struct map_funcs st_types[UP_MAP_ID_COUNT];

void
up_map_funcs_init(struct map_funcs *funcs)
{
	memset(funcs, 0, sizeof(*funcs));
	funcs->free_partpriv = up_map_freeprivpart_def;
	funcs->free_mappriv = up_map_freeprivmap_def;
}

void
up_map_register(enum mapid type, const struct map_funcs *params)
{
	struct map_funcs *funcs;

	assert(type > UP_MAP_NONE && type < UP_MAP_ID_COUNT);
	assert(!(UP_TYPE_REGISTERED & st_types[type].flags));
	assert(!(UP_TYPE_REGISTERED & params->flags));

	funcs = &st_types[type];
	funcs->label = xstrdup(params->label, XA_FATAL);
	funcs->flags = UP_TYPE_REGISTERED | params->flags;
	funcs->load = params->load;
	funcs->setup = params->setup;
	funcs->get_index = params->get_index;
	funcs->print_header = params->print_header;
	funcs->print_extrahdr = params->print_extrahdr;
	funcs->print_extra = params->print_extra;
	funcs->dump_extra = params->dump_extra;
	funcs->free_mappriv = params->free_mappriv;
	funcs->free_partpriv = params->free_partpriv;
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
	assert(UP_PART_VIRTADDR(parent) >= 0 && parent->size >= 0 &&
	    UP_PART_VIRTADDR(parent) + parent->size <=
	    UP_DISK_SIZESECTS(disk));

	funcs = &st_types[type];
	*ret = NULL;
	priv = NULL;

#ifdef MAP_PROBE_DEBUG
	fprintf(stderr, "probe %"PRId64" %s\n",
	    UP_PART_VIRTADDR(parent), funcs->label);
#endif
	switch (funcs->load(disk, parent, &priv)) {
	case 1:
#ifdef MAP_PROBE_DEBUG
		fprintf(stderr, "matched %"PRId64" %s\n",
		    UP_PART_VIRTADDR(parent), funcs->label);
#endif
		map = map_new(disk, parent, type, priv);
		if (map == NULL) {
			if (funcs->free_mappriv && priv)
				funcs->free_mappriv(NULL, priv);
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
map_new(struct disk *disk, struct part *parent, enum mapid type, void *priv)
{
	struct map *map;

	if ((map = xalloc(1, sizeof(*map), XA_ZERO)) == NULL)
		return (NULL);

	map->disk = disk;
	map->type = type;
	map->size = parent->size;
	map->virtoff = (parent->map ? parent->map->virtoff : 0);
	if (parent->flags & UP_PART_VIRTDISK) {
		map->virtstart = 0;
		map->virtoff += + parent->virtstart;
	} else {
		map->virtstart = UP_PART_VIRTADDR(parent);
	}
	map->depth = (parent->map ? parent->map->depth + 1 : 0);
	map->priv = priv;
	SIMPLEQ_INIT(&map->list);

	return (map);
}

static struct part *
map_newcontainer(int64_t size)
{
	struct part *container;

	if ((container = xalloc(1, sizeof(*container), XA_ZERO)) == NULL)
		return (NULL);

	container->virtstart = 0;
	container->size = size;
	SIMPLEQ_INIT(&container->submap);

	return (container);
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
up_map_add(struct map *map, int64_t start, int64_t size, int flags, void *priv)
{
	struct part *part;

	if ((part = xalloc(1, sizeof(*part), XA_ZERO)) == NULL)
		return (NULL);

	if (0 == size)
		flags |= UP_PART_EMPTY;
	if (start < map->virtstart ||
	    start + size > map->virtstart + map->size)
		flags |= UP_PART_OOB;

	part->virtstart = start;
	part->size = size;
	part->flags = flags;
	part->priv = priv;
	part->map = map;
	SIMPLEQ_INIT(&part->submap);
	SIMPLEQ_INSERT_TAIL(&map->list, part, link);

	return (part);
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
        if(st_types[ii->map->type].free_partpriv && ii->priv)
            st_types[ii->map->type].free_partpriv(ii, ii->priv);
        free(ii);
    }

    /* free private data */
    if(st_types[map->type].free_mappriv && map->priv)
        st_types[map->type].free_mappriv(map, map->priv);

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

static void
map_print(const struct map *map, int sizewidth, FILE *stream)
{
	struct map_funcs *funcs;
	const struct part *part;
	int indented;
	char idx[5], flag;

	CHECKTYPE(map->type);
	funcs = &st_types[map->type];
	indented = 0;

	if (!UP_NOISY(NORMAL))
		return;

	/* print info line(s) */
	if (funcs->print_header != NULL) {
		map_indent(map->depth, stream);
		indented = 1;
		if (funcs->print_header(map, stream) > 0)
			indented = 0;
	}

	/* print the header line */
	if (!(UP_TYPE_NOPRINTHDR & funcs->flags)) {
		if (!indented)
			map_indent(map->depth, stream);
		fprintf(stream, "       %*s %*s",
		    sizewidth, (opts->swapcols ?  "Size" : "Start"),
		    sizewidth, (opts->swapcols ?  "Start" : "Size"));
		if (funcs->print_extrahdr != NULL)
			funcs->print_extrahdr(map, stream);
		putc('\n', stream);
		indented = 0;
	}

	/* print partitions */
	for (part = up_map_first(map); part != NULL; part = up_map_next(part)) {
		/* skip empty partitions unless verbose */
		if (UP_PART_EMPTY & part->flags && !UP_NOISY(EXTRA))
			continue;

		if (!indented)
			map_indent(map->depth, stream);

		if (UP_PART_IS_BAD(part->flags))
			flag = 'X';
		else
			flag = ' ';

		idx[0] = '\0';
		if (funcs->get_index)
			funcs->get_index(part, idx, sizeof(idx));
		idx[sizeof(idx)-1] = '\0';
		if (idx[0] != '\0')
			strlcat(idx, ":", sizeof(idx));

		fprintf(stream, "%-4s %c ", idx, flag);
		printsect_pad((opts->swapcols ? part->size : part->virtstart),
		    sizewidth, stream);
		putc(' ', stream);
		printsect_pad((opts->swapcols ? part->virtstart : part->size),
		    sizewidth, stream);
		if (funcs->print_extra != NULL)
			funcs->print_extra(part, stream);
		putc('\n', stream);
		indented = 0;

		/* recurse */
		map_printcontainer(part, sizewidth, stream);
	}
}

void
up_map_printall(const struct disk *disk, void *stream)
{
	map_printcontainer(disk->maps, 0, stream);
}

static void
map_printcontainer(const struct part *up, int width, FILE *stream)
{
	const struct map *map;
	const struct part *part;
	int64_t size;

	if (width <= 0) {
		size = 0;
		for (map = up_map_firstmap(up); map != NULL;
		     map = up_map_nextmap(map)) {
			for (part = up_map_first(map); part != NULL;
			     part = up_map_next(part)) {
				if (size < part->virtstart ||
				    size < part->size)
					size = MAX(part->virtstart,
					    part->size);
			}
		}
		width = printsect(size, NULL);
		if (width <= 0)
			width = 15;
	}

	for (map = up_map_firstmap(up); map != NULL; map = up_map_nextmap(map))
		map_print(map, width, stream);
}

static void
map_indent(int depth, FILE *stream)
{
    int ii;

    for(ii = 0; depth > ii; ii++)
        putc(' ', stream);
}

void
up_map_dumpsect(const struct map *map, FILE *stream, int64_t start,
    int64_t size, const void *data, int tag)
{
	struct map_funcs *funcs;

	CHECKTYPE(map->type);
	funcs = &st_types[map->type];

	fprintf(stream, "\n\nDump of %s %s at ",
            UP_DISK_PATH(map->disk), funcs->label);
	printsect_verbose(start, stream);
	fprintf(stream, " (0x%"PRIx64")", start);
	if (funcs->dump_extra != NULL)
		funcs->dump_extra(map, start, data, size, tag, stream);
	fputs(":\n", stream);

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
