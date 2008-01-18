#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

#define MBR_ISPRI(idx)          (MBR_PART_COUNT > (idx))
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

struct up_mbr
{
    struct up_mbr_p     mbr;
    int                 extcount;
};

static int mbr_load(struct up_disk *disk, const struct up_part *parent,
                    void **priv, struct up_opts *opts);
static int mbrext_load(struct up_disk *disk, const struct up_part *parent,
                       void **priv, struct up_opts *opts);
static int mbr_setup(struct up_map *map, struct up_opts *opts);
static int mbrext_setup(struct up_map *map, struct up_opts *opts);
static int mbr_getinfo(const struct up_map *part, int verbose,
                       char *buf, int size);
static int mbr_getindex(const struct up_part *part, char *buf, int size);
static int mbr_getextra(const struct up_part *part, int verbose,
                        char *buf, int size);
static void mbr_dump(const struct up_map *map, void *stream);
static int mbr_addpart(struct up_map *map, const struct up_mbrpart_p *part,
                       int index, int64_t off, const struct up_mbr_p *mbr);
static int mbr_read(struct up_disk *disk, int64_t start, int64_t size,
                    const struct up_mbr_p **mbr);
static const char *mbr_name(uint8_t type);

void
up_mbr_register(void)
{
    up_map_register(UP_MAP_MBR,
                    0,
                    mbr_load,
                    mbr_setup,
                    mbr_getinfo,
                    mbr_getindex,
                    mbr_getextra,
                    mbr_dump,
                    up_map_freeprivmap_def,
                    up_map_freeprivpart_def);

    up_map_register(UP_MAP_MBREXT,
                    UP_TYPE_NOPRINTHDR,
                    mbrext_load,
                    mbrext_setup,
                    NULL,
                    mbr_getindex,
                    mbr_getextra,
                    NULL,
                    NULL,
                    up_map_freeprivpart_def);
}

static int
mbr_load(struct up_disk *disk, const struct up_part *parent, void **priv,
         struct up_opts *opts)
{
    const struct up_mbr_p      *buf;
    int                         res;
    struct up_mbr              *mbr;

    assert(MBR_SIZE == sizeof *buf);
    *priv = NULL;

    /* refuse to load if parent map is extended mbr */
    if(parent->map && (UP_MAP_MBR == parent->map->type ||
                       UP_MAP_MBREXT == parent->map->type))
        return 0;

    /* load the mbr sector */
    res = mbr_read(disk, parent->start, parent->size, &buf);
    if(0 >= res)
        return res;

    /* create map private struct */
    mbr = calloc(1, sizeof *mbr);
    if(!mbr)
    {
        perror("malloc");
        return -1;
    }

    mbr->mbr          = *buf;
    mbr->extcount     = 0;
    *priv             = mbr;

    return 1;
}

static int
mbrext_load(struct up_disk *disk, const struct up_part *parent, void **priv,
            struct up_opts *opts)
{
    const struct up_mbr_p *buf;

    assert(MBR_SIZE == sizeof *buf);
    *priv = NULL;

    /* refuse to load unless parent is the right type of mbr partition */
    if(!parent->map || UP_MAP_MBR != parent->map->type ||
       MBR_ID_EXT != ((const struct up_mbrpart*)parent->priv)->part.type)
        return 0;

    /* load and discard the first extended mbr sector to check the magic */
    return mbr_read(disk, parent->start, parent->size, &buf);
}

static int
mbr_setup(struct up_map *map, struct up_opts *opts)
{
    struct up_mbr              *mbr = map->priv;
    int                         ii;

    if(0 > up_disk_mark1sect(map->disk, map->start, map))
        return -1;

    /* add primary partitions */
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
        if(0 > mbr_addpart(map, &mbr->mbr.part[ii], ii, 0, NULL))
            return -1;

    return 0;
}

/* XXX need a way to detect loops */
static int
mbrext_setup(struct up_map *map, struct up_opts *opts)
{
    struct up_mbr              *parent;
    const struct up_mbr_p      *buf;
    int                         res, index;
    int64_t                     absoff, reloff, max;

    assert(UP_MAP_MBR == map->parent->map->type);

    parent    = map->parent->map->priv;
    absoff    = map->start;
    reloff    = 0;
    max       = map->size;
    index     = MBR_PART_COUNT + parent->extcount;

    do
    {
        /* load extended mbr */
        assert(absoff >= map->start && absoff + max <= map->start + map->size);
        res = mbr_read(map->disk, absoff, max, &buf);
        if(0 >= res)
            return res;

        if(0 > up_disk_mark1sect(map->disk, absoff, map))
            return -1;
        if(0 > mbr_addpart(map, &buf->part[MBR_EXTPART], index, absoff, buf))
            return -1;
        index++;

        max    = buf->part[MBR_EXTNEXT].size;
        reloff = buf->part[MBR_EXTNEXT].start;
        absoff = reloff + map->start;
        if(reloff + max > map->size)
            max = map->size - reloff;
        if(0 > max)
            max = 0;
    } while(MBR_ID_EXT == buf->part[MBR_EXTNEXT].type &&
            reloff + max <= map->size && 0 < max);

    /* XXX should give better diagnostic here */
    if(MBR_ID_EXT == buf->part[MBR_EXTNEXT].type)
        fprintf(stderr, "skipping logical MBR partition #%d: out of range\n",
                index);

    parent->extcount = index - MBR_PART_COUNT;

    return 0;
}

static int
mbr_getinfo(const struct up_map *map, int verbose, char *buf, int size)
{
    return snprintf(buf, size, "MBR partition table in sector %"PRId64" of %s:",
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

    label     = mbr_name(priv->part.type);
    active    = MBR_FLAG_ACTIVE & priv->part.flags ? '*' : ' ';
    firstcyl  = MBR_GETCYL(priv->part.firstsectcyl);
    firstsect = MBR_GETSECT(priv->part.firstsectcyl);
    lastcyl   = MBR_GETCYL(priv->part.lastsectcyl);
    lastsect  = MBR_GETSECT(priv->part.lastsectcyl);

    if(verbose)
        return snprintf(buf, size, "%c %4u/%3u/%2u-%4u/%3u/%2u %s (0x%02x)",
                        active, firstcyl, priv->part.firsthead, firstsect,
                        lastcyl, priv->part.lasthead, lastsect, label,
                        priv->part.type);
    else
        return snprintf(buf, size, "%c %s (0x%02x)",
                        active, label, priv->part.type);
}

static void
mbr_dump(const struct up_map *map, void *_stream)
{
    FILE                       *stream = _stream;
    const struct up_part       *ii;
    const struct up_mbrpart    *priv;

    /* dump MBR sector */
    fprintf(stream, "Dump of %s MBR at sector %"PRId64" (0x%08"PRIx64"):\n",
            map->disk->upd_name, map->start, map->start);
    up_hexdump(map->priv, sizeof(struct up_mbr_p), map->start, stream);

    /* dump all extended MBR sectors */
    for(ii = up_map_first(map); ii; ii = up_map_next(ii))
    {
        priv = ii->priv;
        if(MBR_ISPRI(priv->index))
            continue;
        fprintf(stream, "\nDump of %s extended MBR #%d "
                "at sector %"PRId64" (0x%08"PRIx64"):\n",
                map->disk->upd_name, priv->index, priv->extoff, priv->extoff);
        up_hexdump(&priv->extmbr, sizeof priv->extmbr, priv->extoff, stream);
    }
}

static int
mbr_addpart(struct up_map *map, const struct up_mbrpart_p *part, int index,
            int64_t extoff, const struct up_mbr_p *extmbr)
{
    struct up_mbrpart  *priv;
    int                 flags;

    assert(( MBR_ISPRI(index) && 0 == extoff && !extmbr) ||
           (!MBR_ISPRI(index) && 0 <  extoff &&  extmbr));

    priv = calloc(1, sizeof *priv);
    if(!priv)
    {
        perror("malloc");
        return -1;
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

    if(!up_map_add(map, part->start, part->size, flags, priv))
    {
        free(priv);
        return -1;
    }

    return 0;
}

static int
mbr_read(struct up_disk *disk, int64_t start, int64_t size,
        const struct up_mbr_p **mbr)
{
    const void *buf;

    *mbr = NULL;

    if(0 >= size || sizeof *mbr > disk->upd_sectsize)
        return 0;

    if(up_disk_check1sect(disk, start))
        return 0;
    buf = up_disk_getsect(disk, start);
    if(!buf)
        return -1;
    *mbr = buf;

    if(MBR_MAGIC != UP_LETOH16((*mbr)->magic))
        return 0;

    return 1;
}

static const char *mbr_name_table[] =
{
    /* 0x00 */ "unused",
    /* 0x01 */ "DOS FAT12",
    /* 0x02 */ NULL,
    /* 0x03 */ NULL,
    /* 0x04 */ "DOS FAT16 (16-bit)",
    /* 0x05 */ "DOS Extended",
    /* 0x06 */ "DOS Fat16 (32-bit)",
    /* 0x07 */ "NTFS or HPFS",
    /* 0x08 */ NULL,
    /* 0x09 */ NULL,
    /* 0x0a */ NULL,
    /* 0x0b */ "Windows FAT32",
    /* 0x0c */ "Windows FAT32 (LBA)",
    /* 0x0d */ NULL,
    /* 0x0e */ "Windows FAT16 (LBA)",
    /* 0x0f */ "Windows Extended (LBA)",
    /* 0x10 */ NULL,
    /* 0x11 */ "Hidden DOS FAT12",
    /* 0x12 */ NULL,
    /* 0x13 */ NULL,
    /* 0x14 */ "Hidden DOS FAT16 (16-bit)",
    /* 0x15 */ "Hidden DOS Extended Partition",
    /* 0x16 */ "Hidden DOS Fat16 (32-bit)",
    /* 0x17 */ NULL,
    /* 0x18 */ NULL,
    /* 0x19 */ NULL,
    /* 0x1a */ NULL,
    /* 0x1b */ NULL,
    /* 0x1c */ NULL,
    /* 0x1d */ NULL,
    /* 0x1e */ NULL,
    /* 0x1f */ NULL,
    /* 0x20 */ NULL,
    /* 0x21 */ NULL,
    /* 0x22 */ NULL,
    /* 0x23 */ NULL,
    /* 0x24 */ NULL,
    /* 0x25 */ NULL,
    /* 0x26 */ NULL,
    /* 0x27 */ NULL,
    /* 0x28 */ NULL,
    /* 0x29 */ NULL,
    /* 0x2a */ "Syllable",
    /* 0x2b */ NULL,
    /* 0x2c */ NULL,
    /* 0x2d */ NULL,
    /* 0x2e */ NULL,
    /* 0x2f */ NULL,
    /* 0x30 */ NULL,
    /* 0x31 */ NULL,
    /* 0x32 */ NULL,
    /* 0x33 */ NULL,
    /* 0x34 */ NULL,
    /* 0x35 */ NULL,
    /* 0x36 */ NULL,
    /* 0x37 */ NULL,
    /* 0x38 */ NULL,
    /* 0x39 */ NULL,
    /* 0x3a */ NULL,
    /* 0x3b */ NULL,
    /* 0x3c */ NULL,
    /* 0x3d */ NULL,
    /* 0x3e */ NULL,
    /* 0x3f */ NULL,
    /* 0x40 */ NULL,
    /* 0x41 */ NULL,
    /* 0x42 */ NULL,
    /* 0x43 */ NULL,
    /* 0x44 */ NULL,
    /* 0x45 */ NULL,
    /* 0x46 */ NULL,
    /* 0x47 */ NULL,
    /* 0x48 */ NULL,
    /* 0x49 */ NULL,
    /* 0x4a */ NULL,
    /* 0x4b */ NULL,
    /* 0x4c */ NULL,
    /* 0x4d */ NULL,
    /* 0x4e */ NULL,
    /* 0x4f */ NULL,
    /* 0x50 */ NULL,
    /* 0x51 */ NULL,
    /* 0x52 */ NULL,
    /* 0x53 */ NULL,
    /* 0x54 */ NULL,
    /* 0x55 */ NULL,
    /* 0x56 */ NULL,
    /* 0x57 */ NULL,
    /* 0x58 */ NULL,
    /* 0x59 */ NULL,
    /* 0x5a */ NULL,
    /* 0x5b */ NULL,
    /* 0x5c */ NULL,
    /* 0x5d */ NULL,
    /* 0x5e */ NULL,
    /* 0x5f */ NULL,
    /* 0x60 */ NULL,
    /* 0x61 */ NULL,
    /* 0x62 */ NULL,
    /* 0x63 */ NULL,
    /* 0x64 */ NULL,
    /* 0x65 */ NULL,
    /* 0x66 */ NULL,
    /* 0x67 */ NULL,
    /* 0x68 */ NULL,
    /* 0x69 */ NULL,
    /* 0x6a */ NULL,
    /* 0x6b */ NULL,
    /* 0x6c */ NULL,
    /* 0x6d */ NULL,
    /* 0x6e */ NULL,
    /* 0x6f */ NULL,
    /* 0x70 */ NULL,
    /* 0x71 */ NULL,
    /* 0x72 */ NULL,
    /* 0x73 */ NULL,
    /* 0x74 */ NULL,
    /* 0x75 */ NULL,
    /* 0x76 */ NULL,
    /* 0x77 */ NULL,
    /* 0x78 */ NULL,
    /* 0x79 */ NULL,
    /* 0x7a */ NULL,
    /* 0x7b */ NULL,
    /* 0x7c */ NULL,
    /* 0x7d */ NULL,
    /* 0x7e */ NULL,
    /* 0x7f */ NULL,
    /* 0x80 */ NULL,
    /* 0x81 */ NULL,
    /* 0x82 */ "Linux Swap",
    /* 0x83 */ "Linux Filesystem",
    /* 0x84 */ NULL,
    /* 0x85 */ NULL,
    /* 0x86 */ NULL,
    /* 0x87 */ NULL,
    /* 0x88 */ NULL,
    /* 0x89 */ NULL,
    /* 0x8a */ NULL,
    /* 0x8b */ NULL,
    /* 0x8c */ NULL,
    /* 0x8d */ NULL,
    /* 0x8e */ NULL,
    /* 0x8f */ NULL,
    /* 0x90 */ NULL,
    /* 0x91 */ NULL,
    /* 0x92 */ NULL,
    /* 0x93 */ NULL,
    /* 0x94 */ NULL,
    /* 0x95 */ NULL,
    /* 0x96 */ NULL,
    /* 0x97 */ NULL,
    /* 0x98 */ NULL,
    /* 0x99 */ NULL,
    /* 0x9a */ NULL,
    /* 0x9b */ NULL,
    /* 0x9c */ NULL,
    /* 0x9d */ NULL,
    /* 0x9e */ NULL,
    /* 0x9f */ NULL,
    /* 0xa0 */ NULL,
    /* 0xa1 */ NULL,
    /* 0xa2 */ NULL,
    /* 0xa3 */ NULL,
    /* 0xa4 */ NULL,
    /* 0xa5 */ "FreeBSD or DragonFly",
    /* 0xa6 */ "OpenBSD",
    /* 0xa7 */ NULL,
    /* 0xa8 */ "MacOS X UFS", /* XXX verify this */
    /* 0xa9 */ "NetBSD",
    /* 0xaa */ NULL,
    /* 0xab */ "MacOS X Boot", /* XXX verify this */
    /* 0xac */ NULL,
    /* 0xad */ NULL,
    /* 0xae */ NULL,
    /* 0xaf */ "MacOS X HFS+",
    /* 0xb0 */ NULL,
    /* 0xb1 */ NULL,
    /* 0xb2 */ NULL,
    /* 0xb3 */ NULL,
    /* 0xb4 */ NULL,
    /* 0xb5 */ NULL,
    /* 0xb6 */ NULL,
    /* 0xb7 */ NULL,
    /* 0xb8 */ NULL,
    /* 0xb9 */ NULL,
    /* 0xba */ NULL,
    /* 0xbb */ NULL,
    /* 0xbc */ NULL,
    /* 0xbd */ NULL,
    /* 0xbe */ NULL,
    /* 0xbf */ "Solaris",
    /* 0xc0 */ NULL,
    /* 0xc1 */ NULL,
    /* 0xc2 */ NULL,
    /* 0xc3 */ NULL,
    /* 0xc4 */ NULL,
    /* 0xc5 */ NULL,
    /* 0xc6 */ NULL,
    /* 0xc7 */ NULL,
    /* 0xc8 */ NULL,
    /* 0xc9 */ NULL,
    /* 0xca */ NULL,
    /* 0xcb */ NULL,
    /* 0xcc */ NULL,
    /* 0xcd */ NULL,
    /* 0xce */ NULL,
    /* 0xcf */ NULL,
    /* 0xd0 */ NULL,
    /* 0xd1 */ NULL,
    /* 0xd2 */ NULL,
    /* 0xd3 */ NULL,
    /* 0xd4 */ NULL,
    /* 0xd5 */ NULL,
    /* 0xd6 */ NULL,
    /* 0xd7 */ NULL,
    /* 0xd8 */ NULL,
    /* 0xd9 */ NULL,
    /* 0xda */ NULL,
    /* 0xdb */ NULL,
    /* 0xdc */ NULL,
    /* 0xdd */ NULL,
    /* 0xde */ NULL,
    /* 0xdf */ NULL,
    /* 0xe0 */ NULL,
    /* 0xe1 */ NULL,
    /* 0xe2 */ NULL,
    /* 0xe3 */ NULL,
    /* 0xe4 */ NULL,
    /* 0xe5 */ NULL,
    /* 0xe6 */ NULL,
    /* 0xe7 */ NULL,
    /* 0xe8 */ NULL,
    /* 0xe9 */ NULL,
    /* 0xea */ NULL,
    /* 0xeb */ "BeOS",
    /* 0xec */ NULL,
    /* 0xed */ NULL,
    /* 0xee */ "EFI GPT",
    /* 0xef */ "EFI System",
    /* 0xf0 */ NULL,
    /* 0xf1 */ NULL,
    /* 0xf2 */ NULL,
    /* 0xf3 */ NULL,
    /* 0xf4 */ NULL,
    /* 0xf5 */ NULL,
    /* 0xf6 */ NULL,
    /* 0xf7 */ NULL,
    /* 0xf8 */ NULL,
    /* 0xf9 */ NULL,
    /* 0xfa */ NULL,
    /* 0xfb */ NULL,
    /* 0xfc */ NULL,
    /* 0xfd */ NULL,
    /* 0xfe */ NULL,
    /* 0xff */ NULL,
};

static const char *
mbr_name(uint8_t type)
{
    assert(0x100 == sizeof(mbr_name_table) / sizeof(mbr_name_table[0]));
    return (mbr_name_table[type] ? mbr_name_table[type] : "unknown");
}
