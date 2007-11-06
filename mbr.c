#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdqueue.h"
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

static struct up_part *mbr_addpart(struct up_map *map, struct up_part *parent,
                                   const struct up_mbrpart_p *part, int index,
                                   int64_t off, const struct up_mbr_p *mbr);
static int mbr_loadext(struct up_disk *disk, struct up_map *map,
                       struct up_part *parent, int *index);
static int readmbr(struct up_disk *disk, int64_t start, int64_t size,
                   const struct up_mbr_p **mbr);
static void printpart(FILE *stream, const struct up_disk *disk,
                      const struct up_part *part, int verbose);

int
up_mbr_test(struct up_disk *disk, int64_t start, int64_t size)
{
    return readmbr(disk, start, size, NULL);
}

struct up_map *
up_mbr_load(struct up_disk *disk, int64_t start, int64_t size)
{
    struct up_map *map;

    up_mbr_testload(disk, start, size, &map);
    return map;
}

int
up_mbr_testload(struct up_disk *disk, int64_t start, int64_t size,
                struct up_map **mapret)
{
    const struct up_mbr_p      *buf;
    int                         res, ii, jj;
    struct up_mbr_p            *mbr;
    struct up_map              *map;
    struct up_part             *part;

    assert(MBR_SIZE == sizeof *buf);

    /* load MBR */
    *mapret = NULL;
    if(start)
        return 0;
    res = readmbr(disk, start, size, &buf);
    if(0 >= res)
        return res;

    /* alloc map structs */
    mbr = calloc(1, sizeof *mbr);
    if(!mbr)
    {
        perror("malloc");
        return -1;
    }
    *mbr = *buf;
    map = up_map_new(start, size, UP_MAP_MBR, mbr, up_map_freeprivmap_def);
    if(!map)
    {
        free(mbr);
        return -1;
    }

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
        if(0 > mbr_loadext(disk, map, part, &jj))
        {
            up_map_free(map);
            return -1;
        }
    }

    *mapret = map;
    return 1;
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

    ret = up_map_add(map, parent, part->start, part->size,
                     part->type, up_mbr_name(part->type),
                     flags, priv, up_map_freeprivpart_def);
    if(!ret)
        free(priv);

    return ret;
}

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

void
up_mbr_dump(const struct up_disk *disk, const struct up_map *map,
            void *_stream, const struct up_opts *opts)
{
    const struct up_mbr_p      *mbr = map->priv;
    FILE                       *stream = _stream;
    const struct up_part       *ii, *jj;
    const struct up_mbrpart    *priv;

    /* print header */
    printpart(stream, disk, NULL, opts->upo_verbose);

    for(ii = SIMPLEQ_FIRST(&map->list); ii; ii = SIMPLEQ_NEXT(ii, link))
    {
        printpart(stream, disk, ii, opts->upo_verbose);
        for(jj = SIMPLEQ_FIRST(&ii->children); jj; jj = SIMPLEQ_NEXT(jj, link))
            printpart(stream, disk, jj, opts->upo_verbose);
    }

    if(!opts->upo_verbose)
        return;

    fprintf(stream, "\nDump of %s MBR:\n", disk->upd_name);
    up_hexdump(mbr, sizeof *mbr, 0, stream);
    putc('\n', stream);
    for(ii = SIMPLEQ_FIRST(&map->list); ii; ii = SIMPLEQ_NEXT(ii, link))
    {
        for(jj = SIMPLEQ_FIRST(&ii->children); jj; jj = SIMPLEQ_NEXT(jj, link))
        {
            priv = jj->priv;
            fprintf(stream, "Dump of %s extended MBR #%d "
                    "at sector %"PRId64" (0x%08"PRIx64"):\n",
                    disk->upd_name, priv->index, priv->extoff, priv->extoff);
            up_hexdump(&priv->extmbr, sizeof priv->extmbr,
                       priv->extoff, stream);
            putc('\n', stream);
        }
    }
}

static void
printpart(FILE *stream, const struct up_disk *disk,
          const struct up_part *part, int verbose)
{
    const struct up_mbrpart    *priv;
    char                        splat;

    if(!part)
    {
        if(verbose)
            fprintf(stream, "MBR partition table for %s:\n"
                    "         C   H  S    C   H  S      Start       Size ID\n",
                    disk->upd_name);
        else
            fprintf(stream, "MBR partition table for %s:\n"
                    "           Start       Size ID\n",
                    disk->upd_name);
        return;
    }
    priv = part->priv;

    if(UP_PART_EMPTY & part->flags && !verbose)
        return;

    if(UP_PART_IS_BAD(part->flags))
        splat = 'X';
    else if(MBR_FLAG_ACTIVE & priv->part.flags)
        splat = '*';
    else
        splat = ' ';

    if(MBR_PART_COUNT > priv->index)
        fprintf(stream, "%d:  ", priv->index + 1);
    else
        fprintf(stream, " %d: ", priv->index + 1);

    if(verbose)
        fprintf(stream, "%c %4u/%3u/%2u-%4u/%3u/%2u "
                "%10"PRId64" %10"PRId64" %02x %s\n",
                splat, MBR_GETCYL(priv->part.firstsectcyl),
                priv->part.firsthead, MBR_GETSECT(priv->part.firstsectcyl),
                MBR_GETCYL(priv->part.lastsectcyl), priv->part.lasthead,
                MBR_GETSECT(priv->part.lastsectcyl), part->start, part->size,
                part->type, part->label);
    else
        fprintf(stream, "%c %10"PRId64" %10"PRId64" %02x %s\n",
                splat, part->start, part->size, part->type, part->label);
}

int
up_mbr_iter(struct up_disk *disk, const struct up_map *map,
            int (*func)(struct up_disk*, int64_t, int64_t, const char*, void*),
            void *arg)
{
    int                         res, max;
    const struct up_part       *ii, *jj;
    const struct up_mbrpart    *priv;
    char                        label[32];

    max = 0;
    for(ii = SIMPLEQ_FIRST(&map->list); ii; ii = SIMPLEQ_NEXT(ii, link))
    {
        priv = ii->priv;
        if(!UP_PART_IS_BAD(ii->flags))
        {
            snprintf(label, sizeof label, "MBR partition %d", priv->index + 1);
            res = func(disk, ii->start, ii->size, label, arg);
            if(0 > res)
                return res;
            if(res > max)
                max = res;
        }
        for(jj = SIMPLEQ_FIRST(&ii->children); jj; jj = SIMPLEQ_NEXT(jj, link))
        {
            priv = jj->priv;
            if(!UP_PART_IS_BAD(jj->flags))
            {
                snprintf(label, sizeof label, "MBR partition %d",
                         priv->index + 1);
                res = func(disk, jj->start, jj->size, label, arg);
                if(0 > res)
                    return res;
                if(res > max)
                    max = res;
            }
        }
    }

    return max;
}
