#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsdlabel.h"
#include "disk.h"
#include "map.h"
#include "util.h"

/*
  known label locations:
    sector 0, offset 0
    sector 0, offset 64
    sector 0, offset 128
    sector 1, offset 0
    sector 2, offset 0
*/
#define LABEL_PROBE_SECTS       (3)

#define LABEL_MAGIC             (UINT32_C(0x82564557))
#define LABEL_MAGIC0BE          (0x82)
#define LABEL_MAGIC0LE          (0x57)
#define LABEL_OFF_MAGIC1        (0x0)
#define LABEL_OFF_MAGIC2        (0x84)
#define LABEL_OFF_CKSUM         (0x88)
#define LABEL_BASE_SIZE         (0x94)
#define LABEL_PART_SIZE         (0x10)

#define LABEL_LGETINT16(labl, fld) \
    (UP_ETOH16((labl)->label.fld, (labl)->endian))
#define LABEL_LGETINT32(labl, fld) \
    (UP_ETOH32((labl)->label.fld, (labl)->endian))
#define LABEL_PGETINT16(labl, prt, fld) \
    (UP_ETOH16((prt)->part.fld, (labl)->endian))
#define LABEL_PGETINT32(labl, prt, fld) \
    (UP_ETOH32((prt)->part.fld, (labl)->endian))

struct up_bsd_p
{
    uint32_t            magic1;
    uint16_t            disktype;
    uint16_t            subtype;
    char                typename[16];
    char                packname[16];
    uint32_t            sectsize;
    uint32_t            sectpertrack;
    uint32_t            trackpercyl;
    uint32_t            cylcount;
    uint32_t            sectpercyl;
    uint32_t            sectcount;
    uint16_t            sparepertrack;
    uint16_t            sparepercyl;
    uint32_t            altcyls;
    uint16_t            rpm;
    uint16_t            interleave;
    uint16_t            trackskew;
    uint16_t            cylskew;
    uint32_t            headswitch;
    uint32_t            trackseek;
    uint32_t            flags;
    uint32_t            drivedata[5];
    uint32_t            spare[5];
    uint32_t            magic2;
    uint16_t            checksum;
    uint16_t            maxpart;
    uint32_t            bootsize;
    uint32_t            superblockmax;
} __attribute__((packed));

struct up_bsdpart_p
{
    uint32_t            size;
    uint32_t            start;
    uint32_t            fragsize;
    uint8_t             type;
    uint8_t             fragperblock;
    uint16_t            cylpergroup;
} __attribute__((packed));

struct up_bsd
{
    int                 sectoff;
    int                 byteoff;
    int                 endian;
    uint8_t            *buf;
    int                 bufsize;
    struct up_bsd_p     label;
};

struct up_bsdpart
{
    struct up_bsdpart_p part;
    int                 index;
};

/* XXX check these against other BSDs */
static char *up_disktypes[] =
{
    "unknown",
    "SMD",
    "MSCP",
    "old DEC",
    "SCSI",
    "ESDI",
    "ST506",
    "HP-IB",
    "HP-FL",
    "type 9",
    "floppy",
    "ccd",
    "vnd",
    "ATAPI",
    "RAID"
};

#define LABEL_FSTYPE_UNUSED     (0)
#define LABEL_FSTYPE_42BSD      (7)
/* XXX these too */
static char *up_fstypes[] =
{
    "unused",
    "swap",
    "Version6",
    "Version7",
    "SystemV",
    "4.1BSD",
    "Eighth-Edition",
    "4.2BSD",
    "MSDOS",
    "4.4LFS",
    "unknown",
    "HPFS",
    "ISO9660",
    "boot",
    "ADOS",
    "HFS",
    "ADFS",
    "ext2fs",
    "ccd",
    "RAID",
    "NTFS",
    "UDF",
};

static int bsdlabel_load(struct up_disk *disk, const struct up_part *parent,
                         void **priv, struct up_opts *opts);
static int bsdlabel_setup(struct up_map *map, struct up_opts *opts);
static int bsdlabel_info(const struct up_map *map, int verbose,
                         char *buf, int size);
static int bsdlabel_index(const struct up_part *part, char *buf, int size);
static int bsdlabel_extra(const struct up_part *part, int verbose,
                          char *buf, int size);
static void bsdlabel_dump(const struct up_map *map, void *stream);
static void bsdlabel_freemap(struct up_map *map, void *priv);
static int bsdlabel_scan(struct up_disk *disk, int64_t start, int64_t size,
                         const uint8_t **ret, int *sectoff, int *byteoff,
                         int *endian);
static int bsdlabel_read(struct up_disk *disk, int64_t start, int64_t size,
                         const uint8_t **ret, int *off, int *endian);
static uint16_t bsdlabel_cksum(uint8_t *buf, int size);

void up_bsdlabel_register(void)
{
    up_map_register(UP_MAP_BSD,
                    0,
                    bsdlabel_load,
                    bsdlabel_setup,
                    bsdlabel_info,
                    bsdlabel_index,
                    bsdlabel_extra,
                    bsdlabel_dump,
                    bsdlabel_freemap,
                    up_map_freeprivpart_def);
}

static int
bsdlabel_load(struct up_disk *disk, const struct up_part *parent, void **priv,
              struct up_opts *opts)
{
    int                 res, sectoff, byteoff, endian, size;
    const uint8_t      *buf;
    struct up_bsd      *label;

    assert(LABEL_BASE_SIZE == sizeof(struct up_bsd_p) &&
           LABEL_PART_SIZE == sizeof(struct up_bsdpart_p));
    *priv = NULL;

    /* refuse to load if parent map is a disklabel */
    if(parent->map && UP_MAP_BSD == parent->map->type)
        return 0;

    /* search for disklabel */
    res = bsdlabel_scan(disk, parent->start, parent->size,
                        &buf, &sectoff, &byteoff, &endian);
    if(0 >= res)
        return res;
    assert(disk->upd_sectsize - LABEL_BASE_SIZE >= byteoff);

    /* allocate label struct */
    label = calloc(1, sizeof *label);
    if(!label)
    {
        perror("malloc");
        return -1;
    }
    label->buf = malloc(disk->upd_sectsize);
    if(!label->buf)
    {
        perror("malloc");
        free(label);
        return -1;
    }

    /* populate label struct */
    label->sectoff     = sectoff;
    label->byteoff     = byteoff;
    label->endian      = endian;
    label->bufsize     = disk->upd_sectsize;
    memcpy(label->buf, buf, disk->upd_sectsize);
    assert(byteoff + sizeof(label->label) <= disk->upd_sectsize);
    memcpy(&label->label, buf + byteoff, sizeof label->label);

    /* check if the label extends past the end of the sector */
    size = LABEL_BASE_SIZE + (LABEL_PART_SIZE *
                              LABEL_LGETINT16(label, maxpart));
    if(byteoff + size > disk->upd_sectsize)
    {
        fprintf(stderr, "ignoring truncated BSD disklabel (sector %"
                PRId64"\n", parent->start);
        bsdlabel_freemap(NULL, label);
        return -1;
    }

    /* verify the checksum */
    if(bsdlabel_cksum(label->buf + byteoff, size) !=
       LABEL_LGETINT16(label, checksum))
    {
        if(opts->relaxed)
            fprintf(stderr, "warning: BSD disklabel with bad checksum "
                    "(sector %"PRId64"\n", parent->start);
        else
        {
            fprintf(stderr, "ignoring BSD disklabel with bad checksum "
                    "(sector %"PRId64"\n", parent->start);
            bsdlabel_freemap(NULL, label);
            return -1;
        }
    }

    *priv = label;

    return 1;
}

static int
bsdlabel_setup(struct up_map *map, struct up_opts *opts)
{
    struct up_bsd              *label = map->priv;
    int                         ii, max, flags;
    struct up_bsdpart          *part;
    const uint8_t              *buf;
    int64_t                     start, size;

    max = LABEL_LGETINT16(label, maxpart);
    buf = label->buf + label->byteoff + LABEL_BASE_SIZE;
    for(ii = 0; max > ii; ii++)
    {
        part = calloc(1, sizeof *part);
        if(!part)
        {
            perror("malloc");
            return -1;
        }

        assert(label->buf + label->bufsize >=
               buf + (LABEL_PART_SIZE * (ii + 1)));
        memcpy(&part->part, buf + (LABEL_PART_SIZE * ii), LABEL_PART_SIZE);
        part->index   = ii;
        start         = LABEL_PGETINT32(label, part, start);
        size          = LABEL_PGETINT32(label, part, size);
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
bsdlabel_info(const struct up_map *map, int verbose, char *buf, int size)
{
    struct up_bsd      *priv = map->priv;
    uint16_t            disktype;
    char               *disktypestr;
    char                typename[sizeof(priv->label.typename)+1];
    char                packname[sizeof(priv->label.packname)+1];

    if(!verbose)
        return snprintf(buf, size, "BSD disklabel in sector %"PRId64" of %s:",
                        map->start, map->disk->upd_name);

    disktype = LABEL_LGETINT16(priv, disktype);
    disktypestr = (disktype < sizeof(up_disktypes) / sizeof(up_disktypes[0]) ?
                   up_disktypes[disktype] : "");
    memcpy(typename, priv->label.typename, sizeof(priv->label.typename));
    typename[sizeof(typename)-1] = 0;
    memcpy(packname, priv->label.packname, sizeof(priv->label.packname));
    packname[sizeof(packname)-1] = 0;

    return snprintf(buf, size, "BSD disklabel in sector %"PRId64" of %s:\n"
                    "  type: %s (%u)\n"
                    "  disk: %s\n"
                    "  label: %s\n"
                    "  flags: %08x\n"
                    "  bytes/sector: %u\n"
                    "  sectors/track: %u\n"
                    "  tracks/cylinder: %u\n"
                    "  sectors/cylinder: %u\n"
                    "  cylinders: %u\n"
                    "  total sectors: %u\n"
                    "  rpm: %u\n"
                    "  interleave: %u\n"
                    "  trackskew: %u\n"
                    "  cylinderskew: %u\n"
                    "  headswitch: %u\n"
                    "  track-to-track seek: %u\n"
                    "  byte order: %s endian\n"
                    "  partition count: %u\n",
                    map->start, map->disk->upd_name,
                    disktypestr, disktype,
                    typename,
                    packname,
                    LABEL_LGETINT32(priv, flags),
                    LABEL_LGETINT32(priv, sectsize),
                    LABEL_LGETINT32(priv, sectpertrack),
                    LABEL_LGETINT32(priv, trackpercyl),
                    LABEL_LGETINT32(priv, sectpercyl),
                    LABEL_LGETINT32(priv, cylcount),
                    LABEL_LGETINT32(priv, sectcount),
                    LABEL_LGETINT16(priv, rpm),
                    LABEL_LGETINT16(priv, interleave),
                    LABEL_LGETINT16(priv, trackskew),
                    LABEL_LGETINT16(priv, cylskew),
                    LABEL_LGETINT32(priv, headswitch),
                    LABEL_LGETINT32(priv, trackseek),
                    (UP_ENDIAN_BIG == priv->endian ? "big" : "little"),
                    LABEL_LGETINT16(priv, maxpart));
}

static int
bsdlabel_index(const struct up_part *part, char *buf, int size)
{
    struct up_bsdpart *priv = part->priv;

    return snprintf(buf, size, "%c", 'a' + priv->index);
}

static int
bsdlabel_extra(const struct up_part *part, int verbose, char *buf, int size)
{
    struct up_bsd      *label;
    struct up_bsdpart  *priv;
    uint32_t            fsize, bsize;
    uint16_t            cpg;

    if(!part)
    {
        if(!verbose)
            return snprintf(buf, size, "Type");
        else
            return snprintf(buf, size, "Type    fsize bsize   cpg");
    }
    label     = part->map->priv;
    priv      = part->priv;
    fsize     = LABEL_PGETINT32(label, priv, fragsize);
    bsize     = fsize * priv->part.fragperblock;
    cpg       = LABEL_PGETINT16(label, priv, cylpergroup);

    if(priv->part.type >= sizeof(up_fstypes) / sizeof(up_fstypes[0]))
        return snprintf(buf, size, "%u", priv->part.type);
    else if(verbose && LABEL_FSTYPE_UNUSED == priv->part.type && part->size)
        return snprintf(buf, size, "%-7s %5u %5u",
                        up_fstypes[priv->part.type], fsize, bsize);
    else if(verbose && LABEL_FSTYPE_42BSD == priv->part.type)
        return snprintf(buf, size, "%-7s %5u %5u %5u",
                        up_fstypes[priv->part.type], fsize, bsize, cpg);
    else
        return snprintf(buf, size, "%s", up_fstypes[priv->part.type]);
}

static void
bsdlabel_dump(const struct up_map *map, void *stream)
{
    struct up_bsd *priv = map->priv;

    fprintf(stream, "Dump of %s disklabel at sector %"PRId64" (0x%"PRIx64") "
            "offset %d (0x%x):\n", map->disk->upd_name,
            map->start + priv->sectoff, map->start + priv->sectoff,
            priv->byteoff, priv->byteoff);
    up_hexdump(priv->buf, priv->bufsize, map->start + priv->sectoff, stream);
}

static void
bsdlabel_freemap(struct up_map *map, void *priv)
{
    struct up_bsd *label = priv;

    free(label->buf);
    free(label);
}

static int
bsdlabel_scan(struct up_disk *disk, int64_t start, int64_t size,
             const uint8_t **ret, int *sectoff, int *byteoff, int *endian)
{
    int                 ii, off;
    const uint8_t      *buf;

    *ret      = NULL;
    *sectoff  = -1;
    *byteoff  = -1;
    *endian   = -1;

    for(ii = 0; LABEL_PROBE_SECTS > ii; ii++)
    {
        switch(bsdlabel_read(disk, start + ii, size - ii, &buf, &off, endian))
        {
            case -1:
                return -1;
            case 0:
                continue;
            case 1:
                if(ret)
                    *ret = buf;
                if(sectoff)
                    *sectoff = ii;
                if(byteoff)
                    *byteoff = off;
                return 1;
        }
    }

    return 0;
}

static int
bsdlabel_read(struct up_disk *disk, int64_t start, int64_t size,
              const uint8_t **ret, int *off, int *endian)
{
    int                 ii;
    const uint8_t *     buf;
    uint32_t            magic1, magic2;

    *ret      = NULL;
    *off      = 0;
    *endian   = -1;

    if(0 >= size)
        return 0;

    buf = up_disk_getsect(disk, start);
    if(!buf)
        return -1;

    for(ii = 0; disk->upd_sectsize - LABEL_BASE_SIZE >= ii; ii++)
    {
        if(LABEL_MAGIC0BE == buf[ii+LABEL_OFF_MAGIC1] &&
           LABEL_MAGIC0BE == buf[ii+LABEL_OFF_MAGIC2])
            *endian = UP_ENDIAN_BIG;
        else if(LABEL_MAGIC0LE == buf[ii+LABEL_OFF_MAGIC1] &&
                LABEL_MAGIC0LE == buf[ii+LABEL_OFF_MAGIC2])
            *endian = UP_ENDIAN_LITTLE;
        else
            continue;
        memcpy(&magic1, buf + ii + LABEL_OFF_MAGIC1, sizeof magic1);
        memcpy(&magic2, buf + ii + LABEL_OFF_MAGIC2, sizeof magic2);
        if(LABEL_MAGIC == UP_ETOH32(magic1, *endian) && magic1 == magic2)
        {
            *ret = buf;
            *off = ii;
            return 1;
        }
    }

    return 0;
}

static uint16_t
bsdlabel_cksum(uint8_t *buf, int size)
{
    uint8_t             old[2];
    uint16_t            sum, tmp;
    int                 off;

    assert(size % 2 == 0);

    /* save existing checksum and zero out checksum area in buffer */
    memcpy(old, buf + LABEL_OFF_CKSUM, 2);
    memset(buf + LABEL_OFF_CKSUM, 0, 2);

    /* calculate checksum */
    sum = 0;
    for(off = 0; size > off; off += 2)
    {
        memcpy(&tmp, buf + off, 2);
        sum ^= tmp;
    }

    /* restore existing checksum to buffer */
    memcpy(buf + LABEL_OFF_CKSUM, old, 2);

    return sum;
}
