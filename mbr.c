#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "map.h"
#include "mbr.h"
#include "util.h"

#define MBR_SIZE                (0x200)
#define MBR_MAGIC               (0xaa55)
#define MBR_PART_COUNT          (4)
#define MBR_FLAG_ACTIVE         (0x80)
#define MBR_ID_EXT              (0x05)
#define MBR_ID_UNUSED           (0x00)
#define MBR_EXTPART             (0)
#define MBR_EXTNEXT             (1)

#define MBR_GETSECT(sc)         ((sc)[0] & 0x3f)
#define MBR_GETCYL(sc)          ((((uint16_t)((sc)[0] & 0xc0)) << 2) | (sc)[1])

struct up_mbrpart_p
{
    uint8_t         flags;
    uint8_t         firsthead;
    uint8_t         firstsectcyl[2];
    uint8_t         type;
    uint8_t         lasthead;
    uint8_t         lastsectcyl[2];
    uint32_t        start;
    uint32_t        size;
} __attribute__((packed));

struct up_mbr_p
{
    uint8_t             bootcode[446];
    struct up_mbrpart_p part[MBR_PART_COUNT];
    uint16_t            magic;
} __attribute__((packed));

struct up_mbrpart
{
    struct up_mbrpart_p part;
    int                 index;
    int64_t             extoff;
    struct up_mbr_p     extmbr;
};

static int mbr_load(struct up_disk *disk, int64_t start, int64_t size,
                    void **priv);
static int mbr_setup(struct up_map *map);
static int mbr_getinfo(const struct up_map *part, char *buf, int size);
static int mbr_getindex(const struct up_part *part, char *buf, int size);
static int mbr_getextra(const struct up_part *part, int verbose,
                        char *buf, int size);
static void mbr_dump(const struct up_map *map, void *stream);
static struct up_part *mbr_addpart(struct up_map *map, struct up_part *parent,
                                   const struct up_mbrpart_p *part, int index,
                                   int64_t off, const struct up_mbr_p *mbr);
static int mbr_loadext(struct up_disk *disk, struct up_map *map,
                       struct up_part *parent, int *index);
static int readmbr(struct up_disk *disk, int64_t start, int64_t size,
                   const struct up_mbr_p **mbr);

void
up_mbr_register(void)
{
    up_map_register(UP_MAP_MBR, "MBR", mbr_load, mbr_setup, mbr_getinfo,
                    mbr_getindex, mbr_getextra, mbr_dump,
                    up_map_freeprivmap_def, up_map_freeprivpart_def);
}

static int
mbr_load(struct up_disk *disk, int64_t start, int64_t size, void **priv)
{
    const struct up_mbr_p      *buf;
    int                         res;
    struct up_mbr_p            *mbr;

    assert(MBR_SIZE == sizeof *buf);

    /* try to load MBR */
    *priv = NULL;
    if(start)
        return 0;
    res = readmbr(disk, start, size, &buf);
    if(0 >= res)
        return res;

    /* create map private struct */
    mbr = calloc(1, sizeof *mbr);
    if(!mbr)
    {
        perror("malloc");
        return -1;
    }

    *mbr = *buf;
    *priv = mbr;

    return 1;
}

static int
mbr_setup(struct up_map *map)
{
    struct up_mbr_p            *mbr = map->priv;
    int                         ii, jj;
    struct up_part             *part;

    /* add primary partitions and logical subpartitions */
    jj = MBR_PART_COUNT;
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        part = mbr_addpart(map, NULL, &mbr->part[ii], ii, 0, NULL);
        if(!part)
        {
            up_map_free(map);
            return -1;
        }
        if(UP_PART_IS_BAD(part->flags) || MBR_ID_EXT != mbr->part[ii].type)
            continue;
        if(0 > mbr_loadext(map->disk, map, part, &jj))
        {
            up_map_free(map);
            return -1;
        }
    }

    return 0;
}

static int
mbr_getinfo(const struct up_map *map, char *buf, int size)
{
    return snprintf(buf, size, "MBR partition table in sector %"PRId64" of %s",
                    map->start, map->disk->upd_name);
}

static int
mbr_getindex(const struct up_part *part, char *buf, int size)
{
    struct up_mbrpart *priv = part->priv;

    return snprintf(buf, size, "%d", priv->index);
}

static int
mbr_getextra(const struct up_part *part, int verbose, char *buf, int size)
{
    struct up_mbrpart  *priv;
    const char         *label;
    char                active;
    int                 firstcyl, firstsect, lastcyl, lastsect;

    if(!part)
    {
        if(verbose)
            return snprintf(buf, size, "A    C   H  S    C   H  S Type");
        else
            return snprintf(buf, size, "A Type");
    }
    priv = part->priv;

    label     = up_mbr_name(priv->part.type);
    active    = MBR_FLAG_ACTIVE & priv->part.flags ? '*' : ' ';
    firstcyl  = MBR_GETCYL(priv->part.firstsectcyl);
    firstsect = MBR_GETSECT(priv->part.firstsectcyl);
    lastcyl   = MBR_GETCYL(priv->part.lastsectcyl);
    lastsect  = MBR_GETSECT(priv->part.lastsectcyl);

    if(verbose)
        return snprintf(buf, size, "%c %4u/%3u/%2u-%4u/%3u/%2u %s (%02x)",
                        active, firstcyl, priv->part.firsthead, firstsect,
                        lastcyl, priv->part.lasthead, lastsect, label,
                        priv->part.type);
    else
        return snprintf(buf, size, "%c %s (%02x)",
                        active, label, priv->part.type);
}

static void
mbr_dump(const struct up_map *map, void *_stream)
{
    FILE                       *stream = _stream;
    const struct up_part       *ii;
    const struct up_mbrpart    *priv;

    /* dump MBR sector */
    fprintf(stream, "Dump of %s MBR at sector %"PRId64" (0x%08"PRIx64":\n",
            map->disk->upd_name, map->start, map->start);
    up_hexdump(map->priv, sizeof(struct up_mbr_p), map->start, stream);

    /* dump all extended MBR sectors */
    for(ii = up_map_first(map); ii; ii = up_map_next(ii))
    {
        priv = ii->priv;
        if(MBR_PART_COUNT > priv->index)
            continue;
        fprintf(stream, "\nDump of %s extended MBR #%d "
                "at sector %"PRId64" (0x%08"PRIx64"):\n",
                map->disk->upd_name, priv->index, priv->extoff, priv->extoff);
        up_hexdump(&priv->extmbr, sizeof priv->extmbr, priv->extoff, stream);
    }
}

static struct up_part *
mbr_addpart(struct up_map *map, struct up_part *parent,
            const struct up_mbrpart_p *part, int index, int64_t extoff,
            const struct up_mbr_p *extmbr)
{
    struct up_mbrpart  *priv;
    struct up_part     *ret;
    int                 flags;

    assert((!parent && MBR_PART_COUNT >  index && 0 == extoff && !extmbr) ||
           ( parent && MBR_PART_COUNT <= index && 0 <  extoff &&  extmbr));

    priv = calloc(1, sizeof *priv);
    if(!priv)
    {
        perror("malloc");
        return NULL;
    }

    priv->part = *part;
    part = &priv->part;

    priv->part.start  = UP_LETOH32(part->start) + extoff;
    priv->part.size   = UP_LETOH32(part->size);
    priv->index       = index;
    priv->extoff      = extoff;
    if(extmbr)
        priv->extmbr  = *extmbr;

    flags = 0;
    if(MBR_ID_UNUSED == part->type)
        flags |= UP_PART_EMPTY;
    if(part->start < map->start ||
       part->start + part->size > map->start + map->size ||
       (parent && (part->start < parent->start ||
                   part->start + part->size > parent->start + parent->size)))
        flags |= UP_PART_OOB;

    ret = up_map_add(map, parent, part->start, part->size, flags, priv);
    if(!ret)
        free(priv);

    return ret;
}

/* XXX need a way to detect loops */
static int
mbr_loadext(struct up_disk *disk, struct up_map *map, struct up_part *parent,
            int *index)
{
    const struct up_mbr_p      *buf;
    int                         res;
    int64_t                     absoff, reloff, max;

    absoff = parent->start;
    reloff = 0;
    max    = parent->size;

    do
    {
        /* load extended mbr */
        assert(absoff >= parent->start &&
               absoff + max <= parent->start + parent->size);
        res = readmbr(disk, absoff, max, &buf);
        if(0 >= res)
            return res;

        if(!mbr_addpart(map, parent, &buf->part[MBR_EXTPART], *index,
                        absoff, buf))
            return -1;
        (*index)++;

        max    = buf->part[MBR_EXTNEXT].size;
        reloff = buf->part[MBR_EXTNEXT].start;
        absoff = reloff + parent->start;
        if(reloff + max > parent->size)
            max = parent->size - reloff;
        if(0 > max)
            max = 0;
        if(reloff + max > parent->size || 0 == max)
            /* XXX should give better diagnostic here */
            fprintf(stderr, "skipping logical MBR partition out of range\n");
    } while(MBR_ID_EXT == buf->part[MBR_EXTNEXT].type &&
            reloff + max <= parent->size && 0 < max);

    return 1;
}

static int
readmbr(struct up_disk *disk, int64_t start, int64_t size,
        const struct up_mbr_p **mbr)
{
    const void *buf;

    *mbr = NULL;

    if(0 >= size || sizeof *mbr > disk->upd_sectsize)
        return 0;

    buf = up_disk_getsect(disk, start);
    if(!buf)
        return -1;
    *mbr = buf;

    if(MBR_MAGIC != UP_LETOH16((*mbr)->magic))
        return 0;

    return 1;
}
