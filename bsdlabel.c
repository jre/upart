#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

#define LABEL_LABEL             "BSD disklabel"
#define LABEL_MAGIC             (UINT32_C(0x82564557))
#define LABEL_MAGIC0BE          (0x82)
#define LABEL_MAGIC0LE          (0x57)
#define LABEL_OFF_MAGIC1        (0x0)
#define LABEL_OFF_MAGIC2        (0x84)
#define LABEL_BASE_SIZE         (0x94)
#define LABEL_PART_SIZE         (0x10)

#define LABEL_LGETINT16(labl, fld) \
    (UP_ETOH16((labl)->label.fld, (labl)->endian))
#define LABEL_LGETINT32(labl, fld) \
    (UP_ETOH32((labl)->label.fld, (labl)->endian))

#pragma pack(1)

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
    uint16_t            v1_sectcount_h;
    uint16_t            obsd_version;
    uint32_t            spare[4];
    uint32_t            magic2;
    uint16_t            checksum;
    uint16_t            maxpart;
    uint32_t            bootsize;
    uint32_t            superblockmax;
};

struct up_bsdpart0_p
{
    uint32_t            size;
    uint32_t            start;
    uint32_t            fsize;
    uint8_t             type;
    uint8_t             frags;
    uint16_t            cpg;
};

struct up_bsdpart1_p
{
    uint32_t            size_l;
    uint32_t            start_l;
    uint16_t            start_h;
    uint16_t            size_h;
    uint8_t             type;
    uint8_t             bf;
    uint16_t            cpg;
};

#pragma pack()

struct up_bsd
{
    int                 sectoff;
    int                 byteoff;
    int                 endian;
	int		version;
    struct up_bsd_p     label;
};

struct up_bsdpart
{
	uint32_t fsize;
	int type;
	int frags;
	int cpg;
	int index;
};

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
    "vnd/vinum",
    "ATAPI/DOC2K",
    "RAID/RAID",
    "ld",
    "jfs"
    "cgd",
    "vinum",
    "flash"
};

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
    "ADOS/vinum",
    "HFS/raid",
    "ADFS/Filecore",
    "ext2fs",
    "ccd/NTFS",
    "RAID",
    "NTFS/ccd",
    "UDF/jfs",
    "Apple UFS",
    "vinum",
    "UDF",
    "SysVBFS",
    "EFS",
    "ZFS"
};

static int bsdlabel_load(const struct up_disk *disk,
                         const struct up_part *parent, void **priv,
                         const struct up_opts *opts);
static int bsdlabel_setup(struct up_disk *disk, struct up_map *map,
                          const struct up_opts *opts);
static int bsdlabel_getpart_v0(struct up_map *, struct up_bsdpart *,
    const uint8_t *);
static int bsdlabel_getpart_v1(struct up_map *, struct up_bsdpart *,
    const uint8_t *);
static int bsdlabel_info(const struct up_map *map, int verbose,
                         char *buf, int size);
static int bsdlabel_index(const struct up_part *part, char *buf, int size);
static int bsdlabel_extrahdr(const struct up_map *map, int verbose,
                             char *buf, int size);
static int bsdlabel_extra(const struct up_part *part, int verbose,
                          char *buf, int size);
static int bsdlabel_dump(const struct up_map *map, int64_t start,
                         const void *data, int64_t size, int tag, char *buf,
                         int buflen);
static int bsdlabel_scan(const struct up_disk *disk, int64_t start,
                         int64_t size, const uint8_t **ret, int *sectoff,
                         int *byteoff, int *endian, const struct up_opts *opts);
static int bsdlabel_read(const struct up_disk *disk, int64_t start,
                         int64_t size, const uint8_t **ret, int *off,
                         int *endian, const struct up_opts *opts);
static uint16_t bsdlabel_cksum(struct up_bsd_p *hdr,
                               const uint8_t *partitions, int size);

void up_bsdlabel_register(void)
{
    up_map_register(UP_MAP_BSD,
                    LABEL_LABEL,
                    0,
                    bsdlabel_load,
                    bsdlabel_setup,
                    bsdlabel_info,
                    bsdlabel_index,
                    bsdlabel_extrahdr,
                    bsdlabel_extra,
                    bsdlabel_dump,
                    up_map_freeprivmap_def,
                    up_map_freeprivpart_def);
}

static int
bsdlabel_load(const struct up_disk *disk, const struct up_part *parent,
              void **priv, const struct up_opts *opts)
{
    int                 res, sectoff, byteoff, endian, size;
    const uint8_t      *buf;
    struct up_bsd      *label;

    assert(LABEL_BASE_SIZE == sizeof(struct up_bsd_p) &&
           LABEL_PART_SIZE == sizeof(struct up_bsdpart0_p) &&
           LABEL_PART_SIZE == sizeof(struct up_bsdpart1_p));
    *priv = NULL;

    /* search for disklabel */
    res = bsdlabel_scan(disk, parent->start, parent->size,
                        &buf, &sectoff, &byteoff, &endian, opts);
    if(0 >= res)
        return res;
    assert(UP_DISK_1SECT(disk) - LABEL_BASE_SIZE >= byteoff);

    /* allocate label struct */
    label = calloc(1, sizeof *label);
    if(!label)
    {
        perror("malloc");
        return -1;
    }

    /* populate label struct */
    label->sectoff     = sectoff;
    label->byteoff     = byteoff;
    label->endian      = endian;
    assert(byteoff + sizeof(label->label) <= UP_DISK_1SECT(disk));
    memcpy(&label->label, buf + byteoff, sizeof label->label);
    label->version = LABEL_LGETINT32(label, obsd_version);

    /* check if the label extends past the end of the sector */
    size = LABEL_BASE_SIZE + (LABEL_PART_SIZE *
                              LABEL_LGETINT16(label, maxpart));
    if(byteoff + size > UP_DISK_1SECT(disk))
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_err("ignoring truncated %s in sector %"PRId64" (offset %d)",
                   LABEL_LABEL, parent->start, label->sectoff);
        free(label);
        return -1;
    }

    *priv = label;

    return 1;
}

static int
bsdlabel_setup(struct up_disk *disk, struct up_map *map,
               const struct up_opts *opts)
{
    struct up_bsd              *label = map->priv;
    int                         ii, max;
    struct up_bsdpart          *part;
    const uint8_t              *buf;
    int (*getpart)(struct up_map *, struct up_bsdpart *, const uint8_t *);

    buf = up_disk_save1sect(disk, map->start + label->sectoff, map, 0, opts);
    if(!buf)
        return -1;

    max  = LABEL_LGETINT16(label, maxpart);
    buf += label->byteoff + LABEL_BASE_SIZE;
    assert(UP_DISK_1SECT(disk) >=
           label->byteoff + LABEL_BASE_SIZE + (LABEL_PART_SIZE * max));

    /* verify the checksum */
    if(bsdlabel_cksum(&label->label, buf, (LABEL_PART_SIZE * max)) !=
       label->label.checksum)
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_msg((opts->relaxed ? UP_MSG_FWARN : UP_MSG_FERR),
                   "%s with bad checksum in sector %"PRId64" (offset %d)",
                   up_map_label(map), map->start, label->sectoff);
        if(!opts->relaxed)
            return 0;
    }

    getpart = (label->version == 0 ? bsdlabel_getpart_v0 : bsdlabel_getpart_v1);
    for(ii = 0; max > ii; ii++)
    {
        part = calloc(1, sizeof *part);
        if(!part)
        {
            perror("malloc");
            return -1;
        }

        part->index = ii;
	if (!getpart(map, part, buf + (LABEL_PART_SIZE * ii))) {
            free(part);
            return (-1);
	}
    }

    return 1;
}

static int
bsdlabel_getpart_v0(struct up_map *map, struct up_bsdpart *part, const uint8_t *buf)
{
    struct up_bsd *label = map->priv;
    struct up_bsdpart0_p raw;
    int64_t start, size;

    memcpy(&raw, buf, LABEL_PART_SIZE);
    part->fsize = UP_ETOH32(raw.fsize, label->endian);
    part->type = raw.type;
    part->frags = raw.frags;
    part->cpg = UP_ETOH16(raw.cpg, label->endian);
    size = UP_ETOH32(raw.size, label->endian);
    start = UP_ETOH32(raw.start, label->endian);

    return (up_map_add(map, start, size, 0, part) != NULL);
}

static int
bsdlabel_getpart_v1(struct up_map *map, struct up_bsdpart *part, const uint8_t *buf)
{
    struct up_bsd *label = map->priv;
    struct up_bsdpart1_p raw;
    int64_t start, size;

    memcpy(&raw, buf, LABEL_PART_SIZE);
    part->frags = OBSDLABEL_BF_FRAG(raw.bf);
    if (part->frags)
	    part->fsize = OBSDLABEL_BF_BSIZE(raw.bf) / part->frags;
    part->type = raw.type;
    part->cpg = UP_ETOH16(raw.cpg, label->endian);
    size = UP_ETOH32(raw.size_l, label->endian) |
	(int64_t)UP_ETOH16(raw.size_h, label->endian) << 32;
    start = UP_ETOH32(raw.start_l, label->endian) |
	(int64_t)UP_ETOH16(raw.start_h, label->endian) << 32;

    return (up_map_add(map, start, size, 0, part) != NULL);
}

static int
bsdlabel_info(const struct up_map *map, int verbose, char *buf, int size)
{
    struct up_bsd      *priv = map->priv;
    uint16_t            disktype;
    char               *disktypestr;
    char                typename[sizeof(priv->label.typename)+1];
    char                packname[sizeof(priv->label.packname)+1];
    uint64_t sectcount;
    const char *labeltype;
    int res;

    if (priv->version > 0)
	    labeltype = "OpenBSD disklabel";
    else
	    labeltype = up_map_label(map);

    if(UP_NOISY(verbose, EXTRA))
    {
        disktype = LABEL_LGETINT16(priv, disktype);
        disktypestr = (disktype < sizeof(up_disktypes) /
                       sizeof(up_disktypes[0]) ? up_disktypes[disktype] : "");
        memcpy(typename, priv->label.typename, sizeof(priv->label.typename));
        typename[sizeof(typename)-1] = 0;
        memcpy(packname, priv->label.packname, sizeof(priv->label.packname));
        packname[sizeof(packname)-1] = 0;
	sectcount = LABEL_LGETINT32(priv, sectcount);
	if (priv->version > 0) {
		sectcount |= (uint64_t)LABEL_LGETINT16(priv, v1_sectcount_h) << 32;
	}

	if (priv->version > 0)
		res = snprintf(buf, size, "%s in sector %"PRId64" (offset %d)"
		    " of %s:\n  version: %u\n", labeltype, map->start,
		    priv->sectoff, UP_DISK_PATH(map->disk), priv->version);
	else
		res = snprintf(buf, size, "%s in sector %"PRId64" (offset %d)"
		    " of %s:\n", labeltype, map->start, priv->sectoff,
		    UP_DISK_PATH(map->disk));

	if (res < 0 || res >= size)
		return (res);

        return snprintf(buf + res, size - res,
                    "  type: %s (%u)\n"
                    "  disk: %s\n"
                    "  label: %s\n"
                    "  flags: %08x\n"
                    "  bytes/sector: %u\n"
                    "  sectors/track: %u\n"
                    "  tracks/cylinder: %u\n"
                    "  sectors/cylinder: %u\n"
                    "  cylinders: %u\n"
                    "  total sectors: %"PRId64"\n"
                    "  rpm: %u\n"
                    "  interleave: %u\n"
                    "  trackskew: %u\n"
                    "  cylinderskew: %u\n"
                    "  headswitch: %u\n"
                    "  track-to-track seek: %u\n"
                    "  byte order: %s endian\n"
                    "  partition count: %u\n",
                    disktypestr, disktype,
                    typename,
                    packname,
                    LABEL_LGETINT32(priv, flags),
                    LABEL_LGETINT32(priv, sectsize),
                    LABEL_LGETINT32(priv, sectpertrack),
                    LABEL_LGETINT32(priv, trackpercyl),
                    LABEL_LGETINT32(priv, sectpercyl),
                    LABEL_LGETINT32(priv, cylcount),
                    sectcount,
                    LABEL_LGETINT16(priv, rpm),
                    LABEL_LGETINT16(priv, interleave),
                    LABEL_LGETINT16(priv, trackskew),
                    LABEL_LGETINT16(priv, cylskew),
                    LABEL_LGETINT32(priv, headswitch),
                    LABEL_LGETINT32(priv, trackseek),
                    (UP_ENDIAN_BIG == priv->endian ? "big" : "little"),
                    LABEL_LGETINT16(priv, maxpart));
    }
    else if(UP_NOISY(verbose, NORMAL))
        return snprintf(buf, size, "%s in sector %"PRId64" (offset %d) of %s:",
                        labeltype, map->start, priv->sectoff,
                        UP_DISK_PATH(map->disk));
    else
        return 0;
}

static int
bsdlabel_index(const struct up_part *part, char *buf, int size)
{
    struct up_bsdpart *priv = part->priv;

    return snprintf(buf, size, "%c", 'a' + priv->index);
}

static int
bsdlabel_extrahdr(const struct up_map *map, int verbose, char *buf, int size)
{
    const char *hdr = UP_BSDLABEL_FMT_HDR(verbose);

    if(hdr)
        return snprintf(buf, size, "%s", hdr);
    else
        return 0;
}

static int
bsdlabel_extra(const struct up_part *part, int verbose, char *buf, int size)
{
	struct up_bsdpart *priv = part->priv;

	return up_bsdlabel_fmt(part, verbose, buf, size,
	    priv->type, priv->fsize, priv->frags, priv->cpg);
}

static int
bsdlabel_dump(const struct up_map *map, int64_t start, const void *data,
              int64_t size, int tag, char *buf, int buflen)
{
    struct up_bsd *priv = map->priv;

    return snprintf(buf, buflen, " offset %d (0x%x)",
                    priv->byteoff, priv->byteoff);
}

static int
bsdlabel_scan(const struct up_disk *disk, int64_t start, int64_t size,
              const uint8_t **ret, int *sectoff, int *byteoff, int *endian,
              const struct up_opts *opts)
{
    int                 ii, off;
    const uint8_t      *buf;

    *ret      = NULL;
    *sectoff  = -1;
    *byteoff  = -1;
    *endian   = -1;

    for(ii = 0; LABEL_PROBE_SECTS > ii; ii++)
    {
        switch(bsdlabel_read(disk, start + ii, size - ii,
                             &buf, &off, endian, opts))
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
bsdlabel_read(const struct up_disk *disk, int64_t start, int64_t size,
              const uint8_t **ret, int *off, int *endian,
              const struct up_opts *opts)
{
    int                 ii;
    const uint8_t *     buf;
    uint32_t            magic1, magic2;

    *ret      = NULL;
    *off      = 0;
    *endian   = -1;

    if(0 >= size)
        return 0;

    if(up_disk_check1sect(disk, start))
        return 0;
    buf = up_disk_getsect(disk, start, opts);
    if(!buf)
        return -1;

    for(ii = 0; UP_DISK_1SECT(disk) - LABEL_BASE_SIZE >= ii; ii++)
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
bsdlabel_cksum(struct up_bsd_p *hdr, const uint8_t *partitions, int size)
{
    uint16_t            old, sum, tmp;
    int                 off;

    assert(sizeof(*hdr) % 2 == 0 && size % 2 == 0);

    /* save existing checksum and zero out checksum area in buffer */
    old = hdr->checksum;
    hdr->checksum = 0;

    /* calculate checksum */
    sum = 0;
    for(off = 0; sizeof(*hdr) > off; off += 2)
    {
        memcpy(&tmp, (uint8_t*)hdr + off, 2);
        sum ^= tmp;
    }
    for(off = 0; size > off; off += 2)
    {
        memcpy(&tmp, partitions + off, 2);
        sum ^= tmp;
    }

    /* restore existing checksum to buffer */
    hdr->checksum = old;

    return sum;
}

const char *
up_bsdlabel_fstype(int type)
{
    if(0 <= type && type < sizeof(up_fstypes) / sizeof(up_fstypes[0]))
        return up_fstypes[type];
    else
        return NULL;
}

int
up_bsdlabel_fmt(const struct up_part *part, int verbose, char *buf, int size,
    int type, uint32_t fsize, int frags, int cpg)
{
    const char *typestr;

    if(!UP_NOISY(verbose, NORMAL))
        return 0;

    typestr = up_bsdlabel_fstype(type);

    if(NULL == typestr)
        return snprintf(buf, size, "%u", type);
    else if(UP_NOISY(verbose, EXTRA) &&
            UP_BSDLABEL_FSTYPE_UNUSED == type && part->size)
        return snprintf(buf, size, "%-7s %5u %5u",
	    typestr, fsize, fsize * frags);
    else if(UP_NOISY(verbose, EXTRA) && UP_BSDLABEL_FSTYPE_42BSD == type)
        return snprintf(buf, size, "%-7s %5u %5u %5u",
                        typestr, fsize, fsize * frags, cpg);
    else
        return snprintf(buf, size, "%s", typestr);
}
