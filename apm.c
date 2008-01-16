#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apm.h"
#include "disk.h"
#include "map.h"
#include "util.h"

/* XXX this is probably way broken on block sizes other than 512 */

#define APM_ENTRY_SIZE          (512)
#define APM_OFFSET              (1)
#define APM_MAGIC               (0x504d)
#define APM_MAP_PART_TYPE       "Apple_partition_map"

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
    uint8_t             bootargs[128];
    uint8_t             pad[248];
} __attribute__((packed));

struct up_apm
{
    uint8_t            *buf;
    size_t              size;
};

struct up_apmpart
{
    struct up_apm_p     part;
    int                 index;
};

static int apm_load(struct up_disk *disk, const struct up_part *parent,
                    void **priv, struct up_opts *opts);
static int apm_setup(struct up_map *map, struct up_opts *opts);
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
                    int64_t *startret, int64_t *sizeret);

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
         struct up_opts *opts)
{
    int                 res;
    struct up_apm      *apm;
    int64_t             start, size, res64;

    assert(APM_ENTRY_SIZE == sizeof(struct up_apm_p));
    *priv = NULL;

    if(disk->upd_sectsize > APM_ENTRY_SIZE)
        return 0;

    /* refuse to load if parent map is an apm too */
    /* XXX need a better way to do this */
    if(parent->map && UP_MAP_APM == parent->map->type)
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
    apm->size = disk->upd_sectsize * size;
    apm->buf  = calloc(disk->upd_sectsize, size);
    if(!apm->buf)
    {
        perror("malloc");
        free(apm);
        return -1;
    }

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
apm_setup(struct up_map *map, struct up_opts *opts)
{
    struct up_apm              *apm = map->priv;
    int                         ii, flags;
    struct up_apmpart          *part;
    int64_t                     start, size;

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
    return snprintf(buf, size, "Apple partition map based in sector %"PRId64
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
    struct up_apmpart *priv;

    if(!part)
        return snprintf(buf, size, "%-20s %s", "Type", "Name");

    priv = part->priv;
    return snprintf(buf, size, "%-20s %s", priv->part.type, priv->part.name);
}

static void
apm_dump(const struct up_map *map, void *stream)
{
    struct up_apm *priv = map->priv;

    fprintf(stream, "Dump of %s Apple partition map in sector %"PRId64
            " (0x%"PRIx64"):\n", map->disk->upd_name, map->start + APM_OFFSET,
            map->start + APM_OFFSET);
    up_hexdump(priv->buf, priv->size, map->start + APM_OFFSET, stream);
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
         int64_t *startret, int64_t *sizeret)
{
    int                        off;
    const struct up_apm_p     *buf;
    int64_t                    blocks, pstart, psize;

    *startret = 0;
    *sizeret  = 0;

    for(off = 0; off + APM_OFFSET < size; off++)
    {
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
                fprintf(stderr, "invalid apple partition map\n");
                return 0;
            }
            *startret = APM_OFFSET;
            *sizeret  = blocks;
            return 1;
        }
    }

    return 0;
}
