#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sunlabel-shared.h"
#include "sunlabel-x86.h"
#include "disk.h"
#include "map.h"
#include "util.h"

#define SUNX86_LABEL            "Sun x86 disk label"
#define SUNX86_OFF              (1)
#define SUNX86_MAGIC1           (0x600DDEEE)
#define SUNX86_MAGIC1_OFF       (12)
#define SUNX86_VERSION          (1)
#define SUNX86_VERSION_OFF      (16)
#define SUNX86_MAGIC2           (0xDABE)
#define SUNX86_MAGIC2_OFF       (508)
#define SUNX86_CHECKSUM_OFF     (510)
#define SUNX86_SIZE             (512)
/* XXX should use partition count from disk instead of hardcoding this */
#define SUNX86_MAXPARTITIONS    (16)

struct up_sunx86part_p
{
    uint16_t                    type;
    uint16_t                    flags;
    uint32_t                    start;
    uint32_t                    size;
};

struct up_sunx86_p
{
    uint32_t                    pad1[3];
    uint32_t                    magic;
    uint32_t                    version;
    char                        name[8];
    uint16_t                    sectsize;
    uint16_t                    partcount;
    uint32_t                    pad2[10];
    struct up_sunx86part_p      parts[SUNX86_MAXPARTITIONS];
    uint32_t                    pad3[SUNX86_MAXPARTITIONS];
    char                        ascii[128];
    uint32_t                    physcyls;
    uint32_t                    datacyls;
    uint16_t                    altcyls;
    uint16_t                    cyloff;
    uint32_t                    heads;
    uint32_t                    sects;
    uint16_t                    interleave;
    uint16_t                    skew;
    uint16_t                    alts;
    uint16_t                    rpm;
    uint16_t                    writeskip;
    uint16_t                    readskip;
    char                        pad4[20];
    uint16_t                    altmagic;
    uint16_t                    checksum;
} __attribute__((packed));

struct up_sunx86
{
    struct up_sunx86_p          packed;
};

struct up_sunx86part
{
    struct up_sunx86part_p      part;
    int                         index;
};

static int sun_x86_load(const struct up_disk *disk,
                        const struct up_part *parent, void **priv,
                        const struct up_opts *opts);
static int sun_x86_setup(struct up_disk *disk, struct up_map *map,
                         const struct up_opts *opts);
static int sun_x86_info(const struct up_map *map, int verbose,
                        char *buf, int size);
static int sun_x86_index(const struct up_part *part, char *buf, int size);
static int sun_x86_extrahdr(const struct up_map *map, int verbose,
                            char *buf, int size);
static int sun_x86_extra(const struct up_part *part, int verbose,
                         char *buf, int size);
static int sun_x86_read(const struct up_disk *disk, int64_t start, int64_t size,
                        const uint8_t **ret, const struct up_opts *opts);

void up_sunlabel_x86_register(void)
{
    up_map_register(UP_MAP_SUN_X86,
                    SUNX86_LABEL,
                    0,
                    sun_x86_load,
                    sun_x86_setup,
                    sun_x86_info,
                    sun_x86_index,
                    sun_x86_extrahdr,
                    sun_x86_extra,
                    NULL,
                    up_map_freeprivmap_def,
                    up_map_freeprivpart_def);
}

static int
sun_x86_load(const struct up_disk *disk, const struct up_part *parent,
             void **priv, const struct up_opts *opts)
{
    int                 res;
    const uint8_t      *buf;
    struct up_sunx86   *label;

    assert(SUNX86_SIZE == sizeof(struct up_sunx86_p));
    *priv = NULL;

    if(UP_DISK_1SECT(disk) < SUNX86_SIZE)
        return 0;

    /* read map and check magic */
    res = sun_x86_read(disk, parent->start, parent->size, &buf, opts);
    if(0 >= res)
        return res;

    /* allocate map struct */
    label = calloc(1, sizeof *label);
    if(!label)
    {
        perror("malloc");
        return -1;
    }
    memcpy(&label->packed, buf, sizeof label->packed);

    *priv = label;

    return 1;
}

static int
sun_x86_setup(struct up_disk *disk, struct up_map *map,
              const struct up_opts *opts)
{
    struct up_sunx86           *priv = map->priv;
    struct up_sunx86_p         *packed = &priv->packed;
    int                         ii, max, flags;
    struct up_sunx86part       *part;
    int64_t                     start, size;

    if(!up_disk_save1sect(disk, map->start + SUNX86_OFF, map, 0,
                          opts->verbosity))
        return -1;

    max = UP_LETOH16(packed->partcount);
    /* this probably isn't worth checking for */
    if(SUNX86_MAXPARTITIONS < max)
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_warn("clamping partition count in %s from %d down to %d",
                    up_map_label(map), max, SUNX86_MAXPARTITIONS);
        max = SUNX86_MAXPARTITIONS;
    }

    for(ii = 0; max > ii; ii++)
    {
        part = calloc(1, sizeof *part);
        if(!part)
        {
            perror("malloc");
            return -1;
        }

        memcpy(&part->part, &packed->parts[ii], sizeof part->part);
        part->index = ii;
        start         = map->start + UP_LETOH32(part->part.start);
        size          = UP_LETOH32(part->part.size);
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
sun_x86_info(const struct up_map *map, int verbose, char *buf, int size)
{
    const struct up_sunx86     *priv = map->priv;
    const struct up_sunx86_p   *packed = &priv->packed;
    char                        name[sizeof(packed->name)+1];

    if(UP_NOISY(verbose, EXTRA))
    {
        memcpy(name, packed->name, sizeof(packed->name));
        name[sizeof(name)-1] = 0;
        return snprintf(buf, size, "%s in sector %"PRId64" (offset %d) of %s:\n"
                    "  name: %s\n"
                    "  bytes/sector: %u\n"
                    "  partition count: %u\n"
                    "  physical cylinders: %u\n"
                    "  data cylinders: %u\n"
                    "  alternate cylinders: %u\n"
                    "  cylinders offset: %u\n"
                    "  tracks/cylinder: %u\n"
                    "  sectors/track: %u\n"
                    "  interleave: %u\n"
                    "  skew: %u\n"
                    "  alternates/cylinder: %u\n"
                    "  rpm: %u\n"
                    "  write sectskip: %u\n"
                    "  read sectskip: %u\n",
                    up_map_label(map),
                    map->start, SUNX86_OFF, UP_DISK_PATH(map->disk),
                    name,
                    UP_LETOH16(packed->sectsize),
                    UP_LETOH16(packed->partcount),
                    UP_LETOH32(packed->physcyls),
                    UP_LETOH32(packed->datacyls),
                    UP_LETOH16(packed->altcyls),
                    UP_LETOH16(packed->cyloff),
                    UP_LETOH32(packed->heads),
                    UP_LETOH32(packed->sects),
                    UP_LETOH16(packed->interleave),
                    UP_LETOH16(packed->skew),
                    UP_LETOH16(packed->alts),
                    UP_LETOH16(packed->rpm),
                    UP_LETOH16(packed->writeskip),
                    UP_LETOH16(packed->readskip));
    }
    else if(UP_NOISY(verbose, NORMAL))
        return snprintf(buf, size, "%s in sector %"PRId64" of %s:",
                        up_map_label(map), map->start, UP_DISK_PATH(map->disk));
    else
        return 0;
}

static int
sun_x86_index(const struct up_part *part, char *buf, int size)
{
    struct up_sunx86part *priv = part->priv;

    return snprintf(buf, size, "%d", priv->index);
}

static int
sun_x86_extrahdr(const struct up_map *map, int verbose, char *buf, int size)
{
    if(UP_NOISY(verbose, NORMAL))
        return snprintf(buf, size, UP_SUNLABEL_FMT_HDR);
    else
        return 0;
}

static int
sun_x86_extra(const struct up_part *part, int verbose, char *buf, int size)
{
    const struct up_sunx86part *priv = part->priv;

    if(UP_NOISY(verbose, NORMAL))
        return up_sunlabel_fmt(buf, size,
                               UP_LETOH16(priv->part.type),
                               UP_LETOH16(priv->part.flags));
    else
        return 0;
}

static int
sun_x86_read(const struct up_disk *disk, int64_t start, int64_t size,
             const uint8_t **ret, const struct up_opts *opts)
{
    const uint8_t      *buf;
    uint32_t            magic1, vers;
    uint16_t            magic2, sum, calc;
    const uint16_t     *ptr;

    if(SUNX86_OFF >= size)
        return 0;

    if(up_disk_check1sect(disk, start + SUNX86_OFF))
        return 0;
    buf = up_disk_getsect(disk, start + SUNX86_OFF, opts->verbosity);
    if(!buf)
        return -1;

    memcpy(&magic1, buf + SUNX86_MAGIC1_OFF, sizeof magic1);
    memcpy(&vers, buf + SUNX86_VERSION_OFF, sizeof vers);
    memcpy(&magic2, buf + SUNX86_MAGIC2_OFF, sizeof magic2);
    memcpy(&sum, buf + SUNX86_CHECKSUM_OFF, sizeof sum);

    if(SUNX86_MAGIC1 != UP_LETOH32(magic1))
    {
        if(SUNX86_MAGIC1 == UP_BETOH32(magic1) &&
           UP_NOISY(opts->verbosity, QUIET))
            up_err("%s in sector %"PRId64" (offset %d) "
                   "with unknown byte order: big endian",
                   SUNX86_LABEL, start, SUNX86_OFF);
        return 0;
    }

    if(SUNX86_VERSION != UP_LETOH32(vers))
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_err("%s in sector %"PRId64" (offset %d) "
                   "with unknown version: %u",
                   SUNX86_LABEL, start, SUNX86_OFF, UP_LETOH32(vers));
        return 0;
    }

    if(SUNX86_MAGIC2 != UP_LETOH16(magic2))
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_err("%s in sector %"PRId64" (offset %d) "
                   "with bad secondary magic number: 0x%04x",
                   SUNX86_LABEL, start, SUNX86_OFF, UP_LETOH16(magic2));
        return 0;
    }

    assert(0 == SUNX86_SIZE % sizeof sum);
    calc = 0;
    ptr = (const uint16_t *)buf;
    while((const uint8_t *)ptr - buf < SUNX86_CHECKSUM_OFF)
        calc ^= *(ptr++);

    if(calc != sum)
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_msg((opts->relaxed ? UP_MSG_FWARN : UP_MSG_FERR),
                   "%s in sector %"PRId64" (offset %d) "
                   "with bad checksum",
                   SUNX86_LABEL, start, SUNX86_OFF);
        if(!opts->relaxed)
            return 0;
    }

    *ret = buf;
    return 1;
}
