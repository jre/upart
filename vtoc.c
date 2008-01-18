#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vtoc.h"
#include "disk.h"
#include "map.h"
#include "util.h"

#define VTOC_OFF                (1)
#define VTOC_MAGIC              (0x600DDEEE)
#define VTOC_MAGIC_OFF          (12)
#define VTOC_VERSION            (1)
#define VTOC_VERSION_OFF        (16)
#define VTOC_SIZE               (512)
/* XXX should use partition count from disk instead of hardcoding this */
#define VTOC_MAXPARTITIONS      (16)

struct up_vpart_p
{
    uint16_t            type;
    uint16_t            flags;
    uint32_t            start;
    uint32_t            size;
};

struct up_vtoc_p
{
    uint32_t            pad1[3];
    uint32_t            magic;
    uint32_t            version;
    char                name[8];
    uint16_t            sectsize;
    uint16_t            partcount;
    uint32_t            pad2[10];
    struct up_vpart_p   parts[VTOC_MAXPARTITIONS];
    uint32_t            pad3[VTOC_MAXPARTITIONS];
    char                ascii[128];
    uint32_t            physcyls;
    uint32_t            datacyls;
    uint16_t            altcyls;
    uint16_t            cyloff;
    uint32_t            heads;
    uint32_t            sects;
    uint16_t            interleave;
    uint16_t            skew;
    uint16_t            alts;
    uint16_t            rpm;
    uint16_t            writeskip;
    uint16_t            readskip;
    uint16_t            pad4[4];
    char                pad5[16];
} __attribute__((packed));

struct up_vtoc
{
    struct up_vtoc_p    vtoc;
};

struct up_vtocpart
{
    struct up_vpart_p   part;
    int                 index;
};

#define PFLAG_UNMNT        (0x01)
#define PFLAG_UNMNT_CHRS   ("mu")
#define PFLAG_RONLY        (0x10)
#define PFLAG_RONLY_CHRS   ("wr")
#define PFLAG_KNOWN        (PFLAG_UNMNT | PFLAG_RONLY)
#define PFLAG_GETCHR(var, flag) \
    ((PFLAG_##flag##_CHRS)[(PFLAG_##flag & (var) ? 1 : 0)])

static const char *up_parttypes[] =
{
    "unassigned",
    "boot",
    "root",
    "swap",
    "usr",
    "backup",
    "stand",
    "var",
    "home",
    "altsctr",
    "cache",
    "reserved",
};

static int vtoc_load(struct up_disk *disk, const struct up_part *parent,
                     void **priv, struct up_opts *opts);
static int vtoc_setup(struct up_map *map, struct up_opts *opts);
static int vtoc_info(const struct up_map *map, int verbose,
                     char *buf, int size);
static int vtoc_index(const struct up_part *part, char *buf, int size);
static int vtoc_extra(const struct up_part *part, int verbose,
                      char *buf, int size);
static void vtoc_dump(const struct up_map *map, void *stream);
static int vtoc_read(struct up_disk *disk, int64_t start, int64_t size,
                     const uint8_t **ret);

void up_vtoc_register(void)
{
    up_map_register(UP_MAP_VTOC,
                    0,
                    vtoc_load,
                    vtoc_setup,
                    vtoc_info,
                    vtoc_index,
                    vtoc_extra,
                    vtoc_dump,
                    up_map_freeprivmap_def,
                    up_map_freeprivpart_def);
}

static int
vtoc_load(struct up_disk *disk, const struct up_part *parent, void **priv,
          struct up_opts *opts)
{
    int                 res;
    const uint8_t      *buf;
    struct up_vtoc     *vtoc;

    assert(VTOC_SIZE == sizeof(struct up_vtoc_p));
    *priv = NULL;

    if(disk->upd_sectsize < VTOC_SIZE)
        return 0;

    /* read map and check magic */
    res = vtoc_read(disk, parent->start, parent->size, &buf);
    if(0 >= res)
        return res;

    /* allocate map struct */
    vtoc = calloc(1, sizeof *vtoc);
    if(!vtoc)
    {
        perror("malloc");
        return -1;
    }
    memcpy(&vtoc->vtoc, buf, sizeof vtoc->vtoc);

    *priv = vtoc;

    return 1;
}

static int
vtoc_setup(struct up_map *map, struct up_opts *opts)
{
    struct up_vtoc             *priv = map->priv;
    struct up_vtoc_p           *vtoc = &priv->vtoc;
    int                         ii, max, flags;
    struct up_vtocpart         *part;
    int64_t                     start, size;

    if(0 > up_disk_mark1sect(map->disk, map->start + VTOC_OFF, map))
        return -1;

    max = UP_LETOH16(vtoc->partcount);
    if(VTOC_MAXPARTITIONS < max)
    {
        fprintf(stderr, "warning: ignoring VTOC partitions beyond %d\n",
                VTOC_MAXPARTITIONS);
        max = VTOC_MAXPARTITIONS;
    }

    for(ii = 0; max > ii; ii++)
    {
        part = calloc(1, sizeof *part);
        if(!part)
        {
            perror("malloc");
            return -1;
        }

        memcpy(&part->part, &vtoc->parts[ii], sizeof part->part);
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

    return 0;
}

static int
vtoc_info(const struct up_map *map, int verbose, char *buf, int size)
{
    const struct up_vtoc       *priv = map->priv;
    const struct up_vtoc_p     *vtoc = &priv->vtoc;
    char                        name[sizeof(vtoc->name)+1];

    if(!verbose)
        return snprintf(buf, size, "VTOC in sector %"PRId64" of %s:",
                        map->start, map->disk->upd_name);

    memcpy(name, vtoc->name, sizeof(vtoc->name));
    name[sizeof(name)-1] = 0;

    return snprintf(buf, size, "VTOC in sector %"PRId64" (offset %d) of %s:\n"
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
                    map->start, VTOC_OFF, map->disk->upd_name,
                    name,
                    UP_LETOH16(vtoc->sectsize),
                    UP_LETOH16(vtoc->partcount),
                    UP_LETOH32(vtoc->physcyls),
                    UP_LETOH32(vtoc->datacyls),
                    UP_LETOH16(vtoc->altcyls),
                    UP_LETOH16(vtoc->cyloff),
                    UP_LETOH32(vtoc->heads),
                    UP_LETOH32(vtoc->sects),
                    UP_LETOH16(vtoc->interleave),
                    UP_LETOH16(vtoc->skew),
                    UP_LETOH16(vtoc->alts),
                    UP_LETOH16(vtoc->rpm),
                    UP_LETOH16(vtoc->writeskip),
                    UP_LETOH16(vtoc->readskip));
}

static int
vtoc_index(const struct up_part *part, char *buf, int size)
{
    struct up_vtocpart *priv = part->priv;

    return snprintf(buf, size, "%c", 'a' + priv->index);
}

static int
vtoc_extra(const struct up_part *part, int verbose, char *buf, int size)
{
    const struct up_vtocpart   *priv;
    uint16_t                    type, flags;
    char                        flagstr[5];

    if(!part)
    {
        return snprintf(buf, size, "Flags Type");
    }
    priv  = part->priv;
    type  = UP_LETOH16(priv->part.type);
    flags = UP_LETOH16(priv->part.flags);

    if(~PFLAG_KNOWN & flags)
        snprintf(flagstr, sizeof flagstr, "%04x", flags);
    else
    {
        flagstr[0] = PFLAG_GETCHR(flags, RONLY);
        flagstr[1] = PFLAG_GETCHR(flags, UNMNT);
        flagstr[2] = '\0';
    }

    if(sizeof(up_parttypes) / sizeof(up_parttypes[0]) > type)
        return snprintf(buf, size, "%-5s %s", flagstr, up_parttypes[type]);
    else
        return snprintf(buf, size, "%-5s %u", flagstr, type);
}

static void
vtoc_dump(const struct up_map *map, void *stream)
{
    struct up_vtoc *priv = map->priv;

    fprintf(stream, "Dump of %s vtoc at sector %"PRId64" (0x%"PRIx64") off %d:\n",
            map->disk->upd_name, map->start, map->start, VTOC_OFF);
    up_hexdump(&priv->vtoc, sizeof priv->vtoc, map->start + VTOC_OFF, stream);
}

static int
vtoc_read(struct up_disk *disk, int64_t start, int64_t size,
          const uint8_t **ret)
{
    const uint8_t      *buf;
    uint32_t            magic, vers;

    if(VTOC_OFF >= size)
        return 0;

    if(up_disk_check1sect(disk, start + VTOC_OFF))
        return 0;
    buf = up_disk_getsect(disk, start + VTOC_OFF);
    if(!buf)
        return -1;

    memcpy(&magic, buf + VTOC_MAGIC_OFF, sizeof magic);
    memcpy(&vers, buf + VTOC_VERSION_OFF, sizeof vers);
    if(VTOC_MAGIC == UP_LETOH32(magic))
    {
        if(VTOC_VERSION != UP_LETOH32(vers))
            fprintf(stderr, "ignoring VTOC in sector %"PRId64" (offset %d) "
                    "with unknown version: %u\n",
                    start, VTOC_OFF, UP_LETOH32(vers));
        else
        {
            *ret = buf;
            return 1;
        }
    }
    else if(VTOC_MAGIC == UP_BETOH32(magic))
        fprintf(stderr, "ignoring VTOC in sector %"PRId64" (offset %d) "
                "with unknown byte order: big endian\n", start, VTOC_OFF);

    return 0;
}
