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
#define BZB_FLAG_ROOT           (1 << 31)
#define BZB_FLAG_USR            (1 << 30)
#define BZB_FLAG_CRIT           (1 << 29)
#define BZB_FLAG_SLICE(val)     (((0x1f << 16) & val) >> 16)

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
} __attribute__((packed));

struct up_apm
{
    uint8_t            *buf;
    size_t              size;
    int64_t             firstsect;
    int64_t             sectcount;
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

static int apm_load(struct up_disk *disk, const struct up_part *parent,
                    void **priv, const struct up_opts *opts);
static int apm_setup(struct up_map *map, const struct up_opts *opts);
static int apm_info(const struct up_map *map, int verbose,
                    char *buf, int size);
static int apm_index(const struct up_part *part, char *buf, int size);
static int apm_extra(const struct up_part *part, int verbose,
                     char *buf, int size);
static void apm_dump(const struct up_map *map, void *stream);
static void apm_freemap(struct up_map *map, void *priv);
static void apm_bounds(const struct up_apm_p *map,
                       int64_t *start, int64_t *size);
static int apm_find(struct up_disk *disk, int64_t start, int64_t size,
                    int64_t *startret, int64_t *sizeret,
                    const struct up_opts *opts);

void up_apm_register(void)
{
    up_map_register(UP_MAP_APM,
                    0,
                    apm_load,
                    apm_setup,
                    apm_info,
                    apm_index,
                    apm_extra,
                    apm_dump,
                    apm_freemap,
                    up_map_freeprivpart_def);
}

static int
apm_load(struct up_disk *disk, const struct up_part *parent, void **priv,
         const struct up_opts *opts)
{
    int                 res;
    struct up_apm      *apm;
    int64_t             start, size, res64;

    assert(APM_ENTRY_SIZE == sizeof(struct up_apm_p));
    *priv = NULL;

    if(disk->upd_sectsize > APM_ENTRY_SIZE)
        return 0;

    /* find partitions */
    res = apm_find(disk, parent->start, parent->size, &start, &size, opts);
    if(0 >= res)
        return res;

    /* allocate apm struct and buffer for raw map */
    apm = calloc(1, sizeof *apm);
    if(!apm)
    {
        perror("malloc");
        return -1;
    }
    apm->size = disk->upd_sectsize * size;
    apm->buf  = calloc(disk->upd_sectsize, size);
    if(!apm->buf)
    {
        perror("malloc");
        free(apm);
        return -1;
    }
    apm->firstsect = start;
    apm->sectcount = size;

    /* read map */
    res64 = up_disk_read(disk, start, size, apm->buf, apm->size);
    if(size > res64)
    {
        if(0 <= res64)
            fprintf(stderr, "failed to read entire map\n");
        free(apm->buf);
        free(apm);
        return -1;
    }

    *priv = apm;

    return 1;
}

static int
apm_setup(struct up_map *map, const struct up_opts *opts)
{
    struct up_apm              *apm = map->priv;
    int                         ii, flags;
    struct up_apmpart          *part;
    int64_t                     start, size;

    if(0 > up_disk_marksectrange(map->disk, apm->firstsect, apm->sectcount, map))
        return -1;

    for(ii = 0; apm->size / map->disk->upd_sectsize > ii; ii++)
    {
        part = calloc(1, sizeof *part);
        if(!part)
        {
            perror("malloc");
            return -1;
        }
        memcpy(&part->part, apm->buf + ii * map->disk->upd_sectsize,
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

    return 0;
}

static int
apm_info(const struct up_map *map, int verbose, char *buf, int size)
{
    /* XXX display driver info here like pdisk does? */
    return snprintf(buf, size, "Apple partition map in sector %"PRId64
                    " of %s:", map->start, map->disk->upd_name);
}

static int
apm_index(const struct up_part *part, char *buf, int size)
{
    struct up_apmpart *priv = part->priv;

    return snprintf(buf, size, "%d", 1 + priv->index);
}

static int
apm_extra(const struct up_part *part, int verbose, char *buf, int size)
{
    struct up_apmpart  *priv;
    struct up_apm_p    *raw;
    char                type[sizeof(raw->type)+1], name[sizeof(raw->name)+1];
    uint32_t            flags, slice;
    int                 res;

    if(!part)
    {
        if(!verbose)
            return snprintf(buf, size, "%-24s %s", "Type", "Name");
        else
            return snprintf(buf, size, "%-24s %-24s %-10s %s",
                            "Type", "Name", "Status", "A/UX boot data");
    }

    priv = part->priv;
    raw  = &priv->part;
    memcpy(type, raw->type, sizeof raw->type);
    type[sizeof(type)-1] = '\0';
    memcpy(name, raw->name, sizeof raw->name);
    name[sizeof(name)-1] = '\0';

    if(!verbose)
        res = snprintf(buf, size, "%-24s %s", type, name);
    else
        res = snprintf(buf, size, "%-24s %-24s 0x%08x",
                       type, name, UP_BETOH32(raw->status));

    if(BZB_MAGIC == UP_BETOH32(raw->bzbmagic))
    {
        flags = UP_BETOH32(raw->bzbflags);
        slice = BZB_FLAG_SLICE(flags);
        strlcat(buf, " ", size);
        if(slice)
            res = up_scatprintf(buf, size, "slice %d, ", slice);
        if(0 > res || res >= size)
            return res;
        if(raw->bzbcluster)
            res = up_scatprintf(buf, size, "cluster %d, ", raw->bzbcluster);
        if(0 > res || res >= size)
            return res;
        if(BZB_FLAG_ROOT & flags)
            strlcat(buf, "root, ", size);
        if(BZB_FLAG_USR & flags)
            strlcat(buf, "usr, ", size);
        if(BZB_FLAG_CRIT & flags)
            strlcat(buf, "crit, ", size);
        if(sizeof(bzb_types) / sizeof(bzb_types[0]) > raw->bzbtype &&
           NULL != bzb_types[raw->bzbtype])
            strlcat(buf, bzb_types[raw->bzbtype], size);
        res = strlen(buf);
        if(2 <= res && ',' == buf[res-2] && ' ' == buf[res-1])
            buf[res -= 2] = '\0';
    }

    return res;
}

static void
apm_dump(const struct up_map *map, void *stream)
{
    struct up_apm *priv = map->priv;

    fprintf(stream, "Dump of %s Apple partition map in sector %"PRId64
            " (0x%"PRIx64"):\n", map->disk->upd_name, map->start + APM_OFFSET,
            map->start + APM_OFFSET);
    up_hexdump(priv->buf, priv->size, map->disk->upd_sectsize *
               (map->start + APM_OFFSET), stream);
}

static void
apm_freemap(struct up_map *map, void *priv)
{
    struct up_apm *apm = priv;

    free(apm->buf);
    free(apm);
}

static void
apm_bounds(const struct up_apm_p *map, int64_t *start, int64_t *size)
{
    *start = UP_BETOH32(map->partstart) +
             UP_BETOH32(map->datastart);
    *size  = UP_BETOH32(map->datasize);
}

static int
apm_find(struct up_disk *disk, int64_t start, int64_t size,
         int64_t *startret, int64_t *sizeret, const struct up_opts *opts)
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
            if(off)
                fprintf(stderr, "could not find %s partition in sectors %"
                        PRId64" to %"PRId64"\n", APM_MAP_PART_TYPE,
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
                fprintf(stderr, "invalid apple partition map in sector "
                        "%"PRId64"+%d, %s partition in sector %"PRId64"\n",
                        start, APM_OFFSET, APM_MAP_PART_TYPE,
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