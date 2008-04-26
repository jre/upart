#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "map.h"
#include "sunlabel-sparc.h"
#include "util.h"

#define SPARC_MAGIC1_OFF        186
#define SPARC_MAGIC2            0xdabe
#define SPARC_MAGIC2_OFF        508
#define SPARC_CHECKSUM_OFF      510
#define SPARC_MAXPART           8
#define SPARC_SIZE              512

struct up_sparcflags_p
{
    uint16_t tag;
    uint16_t flag;
} __attribute__((packed));

struct up_sparcpart_p
{
    uint32_t                    cyl;
    uint32_t                    size;
} __attribute__((packed));

struct up_sparc_p
{
    char                        label[128];

    uint32_t                    version;
    char                        name[8];
    uint16_t                    partcount;
    struct up_sparcflags_p      part1[SPARC_MAXPART];
    uint32_t                    bootinfo[3];
    uint32_t                    magic1;
    uint32_t                    reserved[10];
    uint32_t                    timestamp[SPARC_MAXPART];

    uint16_t                    writeskip;
    uint16_t                    readskip;
    char                        pad1[154];
    uint16_t                    rpm;
    uint16_t                    physcyls;
    uint16_t                    alts;
    uint16_t                    pad2[2];
    uint16_t                    interleave;
    uint16_t                    datacyls;
    uint16_t                    altcyls;
    uint16_t                    heads;
    uint16_t                    sects;
    uint16_t                    pad3[2];
    struct up_sparcpart_p       part2[SPARC_MAXPART];
    uint16_t                    magic2;
    uint16_t                    checksum;
} __attribute__((packed));

struct up_sparc
{
    struct up_sparc_p           packed;
};

struct up_sparcpart
{
    struct up_sparcpart_p       part;
    int                         index;
};

static int sparc_load(struct up_disk *disk, const struct up_part *parent,
                      void **priv, const struct up_opts *opts);
static int sparc_setup(struct up_map *map, const struct up_opts *opts);
static int sparc_info(const struct up_map *map, int verbose,
                      char *buf, int size);
static int sparc_index(const struct up_part *part, char *buf, int size);
static int sparc_extra(const struct up_part *part, int verbose,
                       char *buf, int size);
static int sparc_read(struct up_disk *disk, int64_t start, int64_t size,
                      const uint8_t **ret);

void up_sunlabel_sparc_register(void)
{
    up_map_register(UP_MAP_SUN_SPARC,
                    "Sun sparc disklabel",
                    0,
                    sparc_load,
                    sparc_setup,
                    sparc_info,
                    sparc_index,
                    sparc_extra,
                    NULL,
                    up_map_freeprivmap_def,
                    up_map_freeprivpart_def);
}

static int
sparc_load(struct up_disk *disk, const struct up_part *parent, void **priv,
          const struct up_opts *opts)
{
    int                 res;
    const uint8_t      *buf;
    struct up_sparc    *sparc;

    assert(SPARC_SIZE == sizeof(struct up_sparc_p));
    *priv = NULL;

    if(disk->upd_sectsize < SPARC_SIZE)
        return 0;

    /* read map and check magic */
    res = sparc_read(disk, parent->start, parent->size, &buf);
    if(0 >= res)
        return res;

    /* allocate map struct */
    sparc = calloc(1, sizeof *sparc);
    if(!sparc)
    {
        perror("malloc");
        return -1;
    }
    memcpy(&sparc->packed, buf, sizeof sparc->packed);

    *priv = sparc;

    return 1;
}

static int
sparc_setup(struct up_map *map, const struct up_opts *opts)
{
    struct up_sparc            *priv = map->priv;
    struct up_sparc_p          *packed = &priv->packed;
    int                         ii, flags;
    struct up_sparcpart        *part;
    int64_t                     cylsize, start, size;

    if(!up_disk_save1sect(map->disk, map->start, map, 0))
        return -1;

    cylsize = (uint64_t)UP_BETOH16(packed->heads) *
              (uint64_t)UP_BETOH16(packed->sects);

    for(ii = 0; SPARC_MAXPART > ii; ii++)
    {
        part = calloc(1, sizeof *part);
        if(!part)
        {
            perror("malloc");
            return -1;
        }

        memcpy(&part->part, &packed->part2[ii], sizeof part->part);
        part->index = ii;
        start         = map->start + cylsize * UP_BETOH32(part->part.cyl);
        size          = UP_BETOH32(part->part.size);
        flags         = 0;

        if(!up_map_add(map, start, size, flags, part))
        {
            free(part);
            return -1;
        }
    }

    return 1;
}

static int
sparc_info(const struct up_map *map, int verbose, char *buf, int size)
{
    const struct up_sparc       *priv = map->priv;
    const struct up_sparc_p     *sparc = &priv->packed;

    if(UP_NOISY(verbose, EXTRA))
    {
        return snprintf(buf, size, "Sun sparc disklabel in sector %"PRId64" of %s:\n"
                        "  write sectskip: %u\n"
                        "  read sectskip: %u\n"
                        "  rpm: %u\n"
                        "  physical cylinders: %u\n"
                        "  alternates/cylinder: %u\n"
                        "  interleave: %u\n"
                        "  data cylinders: %u\n"
                        "  alternate cylinders: %u\n"
                        "  tracks/cylinder: %u\n"
                        "  sectors/track: %u\n"
                        "  magic 2: %04x\n"
                        "  checksum: %04x\n",
                        map->start, map->disk->upd_name,
                        UP_BETOH16(sparc->writeskip),
                        UP_BETOH16(sparc->readskip),
                        UP_BETOH16(sparc->rpm),
                        UP_BETOH16(sparc->physcyls),
                        UP_BETOH16(sparc->alts),
                        UP_BETOH16(sparc->interleave),
                        UP_BETOH16(sparc->datacyls),
                        UP_BETOH16(sparc->altcyls),
                        UP_BETOH16(sparc->heads),
                        UP_BETOH16(sparc->sects),
                        UP_BETOH16(sparc->magic2),
                        UP_BETOH16(sparc->checksum));
    }
    else if(UP_NOISY(verbose, NORMAL))
        return snprintf(buf, size, "Sun sparc disklabel in sector %"PRId64" of %s:",
                        map->start, map->disk->upd_name);
    else
        return 0;
}

static int
sparc_index(const struct up_part *part, char *buf, int size)
{
    struct up_sparcpart *priv = part->priv;

    return snprintf(buf, size, "%d", priv->index);
}

static int
sparc_extra(const struct up_part *part, int verbose, char *buf, int size)
{
    return 0;
}

static int
sparc_read(struct up_disk *disk, int64_t start, int64_t size,
           const uint8_t **ret)
{
    const uint8_t      *buf;
    uint16_t            magic, sum, calc;
    const uint16_t     *ptr;

    if(1 >= size)
        return 0;

    if(up_disk_check1sect(disk, start))
        return 0;
    buf = up_disk_getsect(disk, start);
    if(!buf)
        return -1;

    memcpy(&magic, buf + SPARC_MAGIC2_OFF, sizeof magic);
    memcpy(&sum, buf + SPARC_CHECKSUM_OFF, sizeof sum);

    if(SPARC_MAGIC2 != UP_BETOH16(magic))
    {
        if(SPARC_MAGIC2 == UP_LETOH16(magic))
            /* this is kind of silly but hey, why not? */
            fprintf(stderr, "ignoring sun sparc label in sector %"PRId64" "
                    "with unknown byte order: little endian\n", start);
        return 0;
    }

    assert(0 == SPARC_SIZE % sizeof sum);
    calc = 0;
    ptr = (const uint16_t *)buf;
    while((const uint8_t *)ptr - buf < SPARC_CHECKSUM_OFF)
        calc ^= *(ptr++);

    if(calc != UP_BETOH16(sum))
    {
        fprintf(stderr, "ignoring sun sparc label in sector %"PRId64
                " with bad checksum\n", start);
    }

    *ret = buf;
    return 1;
}
