#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apm.h"
#include "disk.h"
#include "map.h"
#include "util.h"

/*
  http://developer.apple.com/documentation/mac/Devices/Devices-121.html#MARKER-2-27
*/

/* XXX this is probably way broken on block sizes other than 512 */

#define APM_ENTRY_SIZE          (512)
#define APM_OFFSET              (1)
#define APM_MAGIC               (0x504d)
#define APM_MAP_PART_TYPE       "Apple_partition_map"

#define BZB_MAGIC               (0xabadbabe)
#define BZB_TYPE_UNIX           (0x1)
#define BZB_TYPE_AUTO           (0x2)
#define BZB_TYPE_SWAP           (0x3)
#define BZB_FLAG_ROOT           (1U << 31)
#define BZB_FLAG_USR            (1U << 30)
#define BZB_FLAG_CRIT           (1U << 29)
#define BZB_FLAG_SLICE(val)     (((0x1fU << 16) & val) >> 16)

#pragma pack(1)

#pragma pack(1)

struct up_apm_p
{
    uint16_t            sig;
    uint16_t            sigpad;
    uint32_t            mapblocks;
    uint32_t            partstart;
    uint32_t            partsize;
    char                name[32];
    char                type[32];
    uint32_t            datastart;
    uint32_t            datasize;
    uint32_t            status;
    uint32_t            bootstart;
    uint32_t            bootsize;
    uint32_t            bootload;
    uint32_t            bootload2;
    uint32_t            bootentry;
    uint32_t            bootentry2;
    uint32_t            bootsum;
    char                cpu[16];
    uint32_t            bzbmagic;
    uint8_t             bzbcluster;
    uint8_t             bzbtype;
    uint16_t            bzbinode;
    uint32_t            bzbflags;
    uint32_t            bzbtcreat;
    uint32_t            bzbtmount;
    uint32_t            bzbtumount;
    uint32_t            abmsize;
    uint32_t            abments;
    uint32_t            abmstart;
    uint32_t            abmpad[7];
    char                bzbmountpoint[64];
    uint32_t            pad[62];
};

#pragma pack()

struct up_apm
{
    size_t              size;
    int64_t             firstsect;
    int64_t             sectcount;
    const uint8_t      *tmpbuf;
};

struct up_apmpart
{
    struct up_apm_p     part;
    int                 index;
};

static const char *bzb_types[] =
{
    NULL,
    "fs",
    "efs",
    "swap",
};

static int	apm_load(const struct disk *, const struct part *, void **);
static int	apm_setup(struct disk *, struct map *);
static int	apm_info(const struct map *, FILE *);
static int	apm_index(const struct part *, char *, size_t);
static int	apm_extrahdr(const struct map *, FILE *);
static int	apm_extra(const struct part *, FILE *);
static void	apm_bounds(const struct up_apm_p *, int64_t *, int64_t *);
static int	apm_find(const struct disk *, int64_t, int64_t,
    int64_t *, int64_t *);

void
up_apm_register(void)
{
	struct map_funcs funcs;

	up_map_funcs_init(&funcs);
	funcs.label = "Apple partition map";
	funcs.load = apm_load;
	funcs.setup = apm_setup;
	funcs.print_header = apm_info;
	funcs.get_index = apm_index;
	funcs.print_extrahdr = apm_extrahdr;
	funcs.print_extra = apm_extra;

	up_map_register(UP_MAP_APM, &funcs);
}

static int
apm_load(const struct disk *disk, const struct part *parent,
         void **priv)
{
    int                 res;
    struct up_apm      *apm;
    int64_t             start, size;

    assert(APM_ENTRY_SIZE == sizeof(struct up_apm_p));
    *priv = NULL;

    if(UP_DISK_1SECT(disk) > APM_ENTRY_SIZE)
        return 0;

    /* find partitions */
    res = apm_find(disk, parent->start, parent->size, &start, &size);
    if(0 >= res)
        return res;

    /* allocate apm struct and buffer for raw map */
    apm = calloc(1, sizeof *apm);
    if(!apm)
    {
        perror("malloc");
        return -1;
    }
    apm->size = UP_DISK_1SECT(disk) * size;
    apm->firstsect = start;
    apm->sectcount = size;

    *priv = apm;

    return 1;
}

static int
apm_setup(struct disk *disk, struct map *map)
{
    struct up_apm              *apm = map->priv;
    int                         ii, flags;
    struct up_apmpart          *part;
    int64_t                     start, size;
    const uint8_t              *data;

    data = up_disk_savesectrange(disk, apm->firstsect, apm->sectcount,
                                 map, 0);
    if(!data)
        return -1;
    apm->tmpbuf = data;

    for(ii = 0; apm->size / UP_DISK_1SECT(disk) > ii; ii++)
    {
        part = calloc(1, sizeof *part);
        if(!part)
        {
            perror("malloc");
            return -1;
        }
        memcpy(&part->part, data + ii * UP_DISK_1SECT(disk),
               sizeof part->part);
        part->index   = ii;
        apm_bounds(&part->part, &start, &size);
        flags         = 0;

        if(APM_MAGIC != UP_BETOH16(part->part.sig))
            flags |= UP_PART_EMPTY;

        if(!up_map_add(map, start, size, flags, part))
        {
            free(part);
            return -1;
        }
    }

    return 1;
}

static int
apm_info(const struct map *map, FILE *stream)
{
	if (!UP_NOISY(NORMAL))
		return (0);

	/* XXX display driver info here like pdisk does? */
	/* XXX change "in" to "at" */
	if (fprintf(stream, "%s in ", up_map_label(map)) < 0 ||
	    printsect_verbose(map->start, stream)  < 0 ||
	    fprintf(stream, " of %s:\n", UP_DISK_PATH(map->disk)) < 0)
		return (-1);
	return (0);
}

static int
apm_index(const struct part *part, char *buf, size_t size)
{
	struct up_apmpart *priv = part->priv;

	return (snprintf(buf, size, "%d", 1 + priv->index));
}

static int
apm_extrahdr(const struct map *map, FILE *stream)
{

	if (!UP_NOISY(NORMAL))
		return 0;

        if (UP_NOISY(EXTRA))
		return (fprintf(stream, " %-24s %-24s %-10s %s",
			"Type", "Name", "Status", "A/UX boot data"));
        else
		return (fprintf(stream, " %-24s %s", "Type", "Name"));
}

static int
apm_extra(const struct part *part, FILE *stream)
{
	struct up_apmpart *priv;
	struct up_apm_p *raw;
	char type[sizeof(raw->type)+1], name[sizeof(raw->name)+1];

	if (!UP_NOISY(NORMAL))
		return (0);

	priv = part->priv;
	raw = &priv->part;
	memcpy(type, raw->type, sizeof raw->type);
	type[sizeof(type)-1] = '\0';
	memcpy(name, raw->name, sizeof raw->name);
	name[sizeof(name)-1] = '\0';

	if (!UP_NOISY(EXTRA))
		return (fprintf(stream, " %-24s %s", type, name));

	if (fprintf(stream, " %-24s %-24s 0x%08x",
		type, name, UP_BETOH32(raw->status)) < 0)
		return (-1);

        /* XXX this is broken */
        if(BZB_MAGIC == UP_BETOH32(raw->bzbmagic))
        {
            uint32_t flags, slice;

            flags = UP_BETOH32(raw->bzbflags);
            slice = BZB_FLAG_SLICE(flags);
	    if (putc(' ', stream) == EOF ||
		(slice &&
		    fprintf(stream, "slice %d, ", slice) < 0) ||
		(raw->bzbcluster &&
		    fprintf(stream, "cluster %d, ", raw->bzbcluster) < 0) ||
		((BZB_FLAG_ROOT & flags) &&
		    fputs("root, ", stream) == EOF) ||
		((BZB_FLAG_USR & flags) &&
		    fputs("usr, ", stream) == EOF) ||
		((BZB_FLAG_CRIT & flags) &&
		    fputs("crit, ", stream) == EOF))
		    return (-1);
            if (sizeof(bzb_types) / sizeof(bzb_types[0]) > raw->bzbtype &&
		NULL != bzb_types[raw->bzbtype])
		    if (fputs(bzb_types[raw->bzbtype], stream) == EOF)
			    return (-1);
        }

	return (0);
}

static void
apm_bounds(const struct up_apm_p *map, int64_t *start, int64_t *size)
{
    *start = UP_BETOH32(map->partstart) +
             UP_BETOH32(map->datastart);
    *size  = UP_BETOH32(map->datasize);
}

static int
apm_find(const struct disk *disk, int64_t start, int64_t size,
         int64_t *startret, int64_t *sizeret)
{
    int                        off;
    const struct up_apm_p     *buf;
    int64_t                    blocks, pstart, psize;

    *startret = 0;
    *sizeret  = 0;

    for(off = 0; off + APM_OFFSET < size; off++)
    {
        if(up_disk_check1sect(disk, start + off + APM_OFFSET))
            return 0;
        buf = up_disk_getsect(disk, start + off + APM_OFFSET);
        if(!buf)
            return -1;
        if(APM_MAGIC != UP_BETOH16(buf->sig))
        {
            if(off && UP_NOISY(QUIET))
                up_err("could not find %s partition in sectors %"
                       PRId64" to %"PRId64, APM_MAP_PART_TYPE,
                       start + APM_OFFSET, start + off + APM_OFFSET);
            return 0;
        }
        else if(0 == strcmp(APM_MAP_PART_TYPE, buf->type))
        {
            blocks = UP_BETOH32(buf->mapblocks);
            apm_bounds(buf, &pstart, &psize);
            if(start + APM_OFFSET != pstart || off > blocks ||
               blocks > psize || pstart + psize > start + size)
            {
                if(UP_NOISY(QUIET))
                    up_msg((opts->relaxed ? UP_MSG_FWARN : UP_MSG_FERR),
                           "invalid apple partition map in sector %"PRId64
                           "+%d, %s partition in sector %"
                           PRId64, start, APM_OFFSET, APM_MAP_PART_TYPE,
                           start + off + APM_OFFSET);
                if(!opts->relaxed)
                    return 0;
            }
            if(up_disk_checksectrange(disk, start + APM_OFFSET, blocks))
                return 0;
            *startret = start + APM_OFFSET;
            *sizeret  = MIN(blocks, size - APM_OFFSET);
            return 1;
        }
    }

    return 0;
}
