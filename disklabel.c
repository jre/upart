#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "disk.h"
#include "disklabel.h"
#include "mbr.h"
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

#define LABEL_MAGIC             (0x82564557U)
#define LABEL_MAGIC0BE          (0x82)
#define LABEL_MAGIC0LE          (0x57)
#define LABEL_VERSION           (0)
#define LABEL_OFF_MAGIC1        (0x0)
#define LABEL_OFF_TYPE          (0x4)
#define LABEL_OFF_SUBTYPE       (0x6)
#define LABEL_OFF_TYPENAME      (0x8)
#define LABEL_SIZE_TYPENAME     (0x10)
#define LABEL_OFF_PACKNAME      (0x18)
#define LABEL_SIZE_PACKNAME     (0x10)
#define LABEL_OFF_SECSIZE       (0x28)
#define LABEL_OFF_NSECTORS      (0x2c)
#define LABEL_OFF_NTRACKS       (0x30)
#define LABEL_OFF_NCYLINDERS    (0x34)
#define LABEL_OFF_SECPERCYL     (0x38)
#define LABEL_OFF_SECPERUNIT    (0x3c)
#define LABEL_OFF_SPARESPERTRACK (0x40)
#define LABEL_OFF_SPARESPERCYL  (0x42)
#define LABEL_OFF_ACYLINDERS    (0x44)
#define LABEL_OFF_RPM           (0x48)
#define LABEL_OFF_INTERLEAVE    (0x4a)
#define LABEL_OFF_TRACKSKEW     (0x4c)
#define LABEL_OFF_CYLSKEW       (0x4e)
#define LABEL_OFF_HEADSWITCH    (0x50)
#define LABEL_OFF_TRKSEEK       (0x54)
#define LABEL_OFF_FLAGS         (0x58)
#define LABEL_OFF_DRIVEDATA     (0x5c)
#define LABEL_COUNT_DRIVEDATA   (5)
#define LABEL_OFF_MAGIC2        (0x84)
#define LABEL_OFF_CKSUM         (0x88)
#define LABEL_OFF_NPART         (0x8a)
#define LABEL_OFF_PART0         (0x94)
#define LABEL_BASE_SIZE         LABEL_OFF_PART0
#define LABEL_PART_SIZE         (0x10)
#define LABEL_OFF_PSIZE         (0x0)
#define LABEL_OFF_POFFSET       (0x4)
#define LABEL_OFF_PFSIZE        (0x8)
#define LABEL_OFF_PFSTYPE       (0xc)
#define LABEL_OFF_PFRAG         (0xd)
#define LABEL_OFF_PCPG          (0xe)

struct up_labelpart
{
    uint32_t            uplp_size;
    uint32_t            uplp_offset;
    uint32_t            uplp_fsize;
    uint8_t             uplp_fstype;
    uint8_t             uplp_frag;
    uint16_t            uplp_cpg;
};

struct up_label
{
    int64_t             upl_base;
    int64_t             upl_max;
    int                 upl_sectoff;
    int                 upl_byteoff;
    int                 upl_size;
    uint8_t            *upl_buf;
    int                 upl_buflen;
    unsigned int        upl_bigendian : 1;
    uint16_t            upl_type;
    uint16_t            upl_subtype;
    char                upl_typename[LABEL_SIZE_TYPENAME+1];
    char                upl_packname[LABEL_SIZE_PACKNAME+1];
    uint32_t            upl_secsize;
    uint32_t            upl_nsectors;
    uint32_t            upl_ntracks;
    uint32_t            upl_ncylinders;
    uint32_t            upl_secpercyl;
    uint32_t            upl_secperunit;
    uint16_t            upl_sparespertrack;
    uint16_t            upl_sparespercyl;
    uint32_t            upl_acylinders;
    uint16_t            upl_rpm;
    uint16_t            upl_interleave;
    uint16_t            upl_trackskew;
    uint16_t            upl_cylskew;
    uint32_t            upl_headswitch;
    uint32_t            upl_trkseek;
    uint32_t            upl_flags;
    uint32_t            upl_drivedata[LABEL_COUNT_DRIVEDATA];
    uint16_t            upl_npartitions;
    struct up_labelpart*upl_partitions;
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
    "vnd",
    "ATAPI",
    "RAID"
};

#define LABEL_FSTYPE_UNUSED     (0)
#define LABEL_FSTYPE_42BSD      (7)
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

static int up_labelscan(struct up_disk *disk, int64_t start, int64_t size,
                        const uint8_t **buf, int *sectoff, int *byteofff);
static int up_readlabel(struct up_disk *disk, int64_t start, int64_t size,
                        const uint8_t **ret, int *off);
static struct up_label *up_alloclabel(const struct up_disk *disk,
                                      int64_t start, int64_t size);
static int up_parselabel(const struct up_disk *disk, struct up_label *label,
                         const uint8_t *buf, size_t buflen,
                         int sectoff, int byteoff);
static void up_memcpy(void *buf, const struct up_label *label,
                      int off, int size);
static uint8_t up_getint8(const struct up_label *label, int off);
static uint16_t up_getint16(const struct up_label *label, int off);
static uint32_t up_getint32(const struct up_label *label, int off);
static uint16_t up_labelcksum(struct up_label *label);

int
up_disklabel_test(struct up_disk *disk, int64_t start, int64_t size)
{
    int                 ii, res;

    for(ii = 0; LABEL_PROBE_SECTS > ii; ii++)
    {
        res = up_readlabel(disk, start + ii, size - ii, NULL, NULL);
        if(res)
            return res;
    }

    return 0;
}

void *
up_disklabel_load(struct up_disk *disk, int64_t start, int64_t size)
{
    void               *mbr;

    up_mbr_testload(disk, start, size, &mbr);
    return mbr;
}

int
up_disklabel_testload(struct up_disk *disk, int64_t start, int64_t size,
                      void **map)
{
    int                 res, sectoff, byteoff;
    const uint8_t      *buf;
    struct up_label    *label;

    *map = NULL;
    res = up_labelscan(disk, start, size, &buf, &sectoff, &byteoff);
    if(0 >= res)
    {
        return res;
    }

    label = up_alloclabel(disk, start, size);
    if(!label)
        return -1;

    if(0 > up_parselabel(disk, label, buf, disk->upd_sectsize,
                         sectoff, byteoff))
    {
        up_disklabel_free(label);
        return 0;
    }

    *map = label;
    return 1;
}

static int
up_labelscan(struct up_disk *disk, int64_t start, int64_t size,
             const uint8_t **ret, int *sectoff, int *byteoff)
{
    int                 ii, off;
    const uint8_t      *buf;

    if(ret)
        *ret = NULL;
    if(sectoff)
        *sectoff = -1;
    if(byteoff)
        *byteoff = -1;
    for(ii = 0; LABEL_PROBE_SECTS > ii; ii++)
    {
        switch(up_readlabel(disk, start + ii, size - ii, &buf, &off))
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
up_readlabel(struct up_disk *disk, int64_t start, int64_t size,
             const uint8_t **ret, int *off)
{
    int                 ii;
    const uint8_t *     buf;

    if(ret)
        *ret = NULL;
    if(off)
        *off = 0;

    if(0 >= size)
        return 0;

    buf = up_disk_getsect(disk, start);
    if(!buf)
        return -1;

    for(ii = 0; disk->upd_sectsize - LABEL_BASE_SIZE >= ii; ii++)
    {
        if((LABEL_MAGIC0BE == buf[ii + LABEL_OFF_MAGIC1] &&
            LABEL_MAGIC == UP_GETBUF32BE(buf + ii + LABEL_OFF_MAGIC1) &&
            LABEL_MAGIC == UP_GETBUF32BE(buf + ii + LABEL_OFF_MAGIC2)) ||
           (LABEL_MAGIC0LE == buf[ii + LABEL_OFF_MAGIC1] &&
            LABEL_MAGIC == UP_GETBUF32LE(buf + ii + LABEL_OFF_MAGIC1) &&
            LABEL_MAGIC == UP_GETBUF32LE(buf + ii + LABEL_OFF_MAGIC2)))
        {
            if(ret)
                *ret = buf;
            if(off)
                *off = ii;
            return 1;
        }
    }

    return 0;
}

static struct up_label *
up_alloclabel(const struct up_disk *disk, int64_t start, int64_t size)
{
    struct up_label    *label;

    label = calloc(1, sizeof *label);
    if(!label)
    {
        perror("malloc");
        return NULL;
    }
    label->upl_base  = start;
    label->upl_max   = size;
    label->upl_buflen = disk->upd_sectsize;
    label->upl_buf    = malloc(disk->upd_sectsize);
    if(!label->upl_buf)
    {
        perror("malloc");
        free(label);
        return NULL;
    }

    return label;
}

static int
up_parselabel(const struct up_disk *disk, struct up_label *label,
              const uint8_t *buf, size_t buflen, int sectoff, int byteoff)
{
    int                 ii, off;
    struct up_labelpart*part;

    /* copy in raw label and offsets */
    assert(buflen == label->upl_buflen);
    memcpy(label->upl_buf, buf, buflen);
    label->upl_sectoff = sectoff;
    label->upl_byteoff = byteoff;

    /* determine endianness */
    if(LABEL_MAGIC == UP_GETBUF32BE(buf + byteoff + LABEL_OFF_MAGIC1) &&
       LABEL_MAGIC == UP_GETBUF32BE(buf + byteoff + LABEL_OFF_MAGIC2))
        label->upl_bigendian = 1;
    else if(LABEL_MAGIC == UP_GETBUF32LE(buf + byteoff + LABEL_OFF_MAGIC1) &&
            LABEL_MAGIC == UP_GETBUF32LE(buf + byteoff + LABEL_OFF_MAGIC2))
        label->upl_bigendian = 0;
    else
        return -1;

    /* get number of partitions and calculate total label size */
    label->upl_npartitions = up_getint16(label, LABEL_OFF_NPART);
    label->upl_size        = LABEL_BASE_SIZE +
                             LABEL_PART_SIZE * label->upl_npartitions;
    /* sanity check labels */
    /* XXX it would be nice to support labels spanning multiple sectors */
    if(label->upl_byteoff + label->upl_size > label->upl_buflen)
    {
        fprintf(stderr, "disklabel at disk %s sector %"PRId64"+%d offset %d "
                "extends beyond end of sector, ignoring\n", disk->upd_name,
                label->upl_base, label->upl_sectoff, label->upl_byteoff);
        return -1;
    }
    /* verify label checksum */
    if(up_getint16(label, LABEL_OFF_CKSUM) != up_labelcksum(label))
    {
        fprintf(stderr, "disklabel at disk %s sector %"PRId64"+%d offset %d "
                "checksum is bad, ignoring\n", disk->upd_name,
                label->upl_base, label->upl_sectoff, label->upl_byteoff);
        return -1;
    }

    /* read disklabel drive data */
    label->upl_type           = up_getint16(label, LABEL_OFF_TYPE);
    label->upl_subtype        = up_getint16(label, LABEL_OFF_SUBTYPE);
    up_memcpy(label->upl_typename, label,
              LABEL_OFF_TYPENAME, LABEL_SIZE_TYPENAME);
    up_memcpy(label->upl_packname, label,
              LABEL_OFF_PACKNAME, LABEL_SIZE_PACKNAME);
    label->upl_secsize        = up_getint32(label, LABEL_OFF_SECSIZE);
    label->upl_nsectors       = up_getint32(label, LABEL_OFF_NSECTORS);
    label->upl_ntracks        = up_getint32(label, LABEL_OFF_NTRACKS);
    label->upl_ncylinders     = up_getint32(label, LABEL_OFF_NCYLINDERS);
    label->upl_secpercyl      = up_getint32(label, LABEL_OFF_SECPERCYL);
    label->upl_secperunit     = up_getint32(label, LABEL_OFF_SECPERUNIT);
    label->upl_sparespertrack = up_getint16(label, LABEL_OFF_SPARESPERTRACK);
    label->upl_sparespercyl   = up_getint16(label, LABEL_OFF_SPARESPERCYL);
    label->upl_acylinders     = up_getint32(label, LABEL_OFF_ACYLINDERS);
    label->upl_rpm            = up_getint16(label, LABEL_OFF_RPM);
    label->upl_interleave     = up_getint16(label, LABEL_OFF_INTERLEAVE);
    label->upl_trackskew      = up_getint16(label, LABEL_OFF_TRACKSKEW);
    label->upl_cylskew        = up_getint16(label, LABEL_OFF_CYLSKEW);
    label->upl_headswitch     = up_getint32(label, LABEL_OFF_HEADSWITCH);
    label->upl_trkseek        = up_getint32(label, LABEL_OFF_TRKSEEK);
    label->upl_flags          = up_getint32(label, LABEL_OFF_FLAGS);
    for(ii = 0; LABEL_COUNT_DRIVEDATA > ii; ii++)
        label->upl_drivedata[ii] =
            up_getint32(label, LABEL_OFF_DRIVEDATA + 4 * ii);

    /* read partition data */
    label->upl_partitions = calloc(label->upl_npartitions,
                                   sizeof *label->upl_partitions);
    for(ii = 0; label->upl_npartitions > ii; ii++)
    {
        part = &label->upl_partitions[ii];
        off = LABEL_OFF_PART0 + LABEL_PART_SIZE * ii;
        part->uplp_size         = up_getint32(label, off + LABEL_OFF_PSIZE);
        part->uplp_offset       = up_getint32(label, off + LABEL_OFF_POFFSET);
        part->uplp_fsize        = up_getint32(label, off + LABEL_OFF_PFSIZE);
        part->uplp_fstype       = up_getint8 (label, off + LABEL_OFF_PFSTYPE);
        part->uplp_frag         = up_getint8 (label, off + LABEL_OFF_PFRAG);
        part->uplp_cpg          = up_getint16(label, off + LABEL_OFF_PCPG);
    }

    return 0;
}

static void
up_memcpy(void *buf, const struct up_label *label, int off, int size)
{
    assert(label->upl_byteoff + off + size <= label->upl_buflen);
    memcpy(buf, label->upl_buf + label->upl_byteoff + off, size);
}

static uint8_t
up_getint8(const struct up_label *label, int off)
{
    assert(label->upl_byteoff + off + 1 <= label->upl_buflen);
    return label->upl_buf[label->upl_byteoff + off];
}

static uint16_t
up_getint16(const struct up_label *label, int off)
{
    assert(label->upl_byteoff + off + 2 <= label->upl_buflen);
    if(label->upl_bigendian)
        return UP_GETBUF16BE(label->upl_buf + label->upl_byteoff + off);
    else
        return UP_GETBUF16LE(label->upl_buf + label->upl_byteoff + off);
}

static uint32_t
up_getint32(const struct up_label *label, int off)
{
    assert(off + 4 <= label->upl_buflen);
    if(label->upl_bigendian)
        return UP_GETBUF32BE(label->upl_buf + label->upl_byteoff + off);
    else
        return UP_GETBUF32LE(label->upl_buf + label->upl_byteoff + off);
}

static uint16_t
up_labelcksum(struct up_label *label)
{
    uint8_t             old[2];
    uint16_t            sum;
    int                 off;

    assert(label->upl_size % 2 == 0);

    /* save existing checksum and zero out checksum area in buffer */
    memcpy(old, label->upl_buf + label->upl_byteoff + LABEL_OFF_CKSUM, 2);
    memset(label->upl_buf + label->upl_byteoff + LABEL_OFF_CKSUM, 0, 2);

    /* calculate checksum */
    sum = 0;
    for(off = 0; label->upl_size > off; off += 2)
        sum ^= up_getint16(label, off);

    /* restore existing checksum to buffer */
    memcpy(label->upl_buf + label->upl_byteoff + LABEL_OFF_CKSUM, old, 2);

    return sum;
}

void
up_disklabel_free(void *_label)
{
    struct up_label    *label = _label;

    if(!label)
        return;
    if(label->upl_buf)
        free(label->upl_buf);
    if(label->upl_partitions)
        free(label->upl_partitions);
    free(label);
}

#define DISKLABELV1_FFS_FRAGBLOCK(fsize, frag) 			\
	((fsize) * (frag) == 0 ? 0 :				\
	(((ffs((fsize) * (frag)) - 13) << 3) | (ffs(frag))))

#define DISKLABELV1_FFS_BSIZE(i) ((i) == 0 ? 0 : (1 << (((i) >> 3) + 12)))
#define DISKLABELV1_FFS_FRAG(i) ((i) == 0 ? 0 : (1 << (((i) & 0x07) - 1)))
#define DISKLABELV1_FFS_FSIZE(i) (DISKLABELV1_FFS_FRAG(i) == 0 ? 0 : \
	(DISKLABELV1_FFS_BSIZE(i) / DISKLABELV1_FFS_FRAG(i)))

void
up_disklabel_dump(const struct up_disk *disk, const void *_label,
                  void *_stream, const struct up_opts *opt)
{
    const struct up_label      *label   = _label;
    FILE                       *stream  = _stream;
    int                         ii, jj;
    struct up_labelpart *       part;
    uint32_t                    fsize, frag;
    uint8_t                     fragblock;

    fprintf(stream, "Disklabel on %s at sector %"PRId64"+%d offset %d "
            "with %d partition maximum:\n", disk->upd_name, label->upl_base,
            label->upl_sectoff, label->upl_byteoff, label->upl_npartitions);
    if(opt->upo_verbose)
    {
        if(label->upl_type < sizeof(up_disktypes) / sizeof(up_disktypes[0]))
            fprintf(stream, "  type: %s\n", up_disktypes[label->upl_type]);
        else
            fprintf(stream, "  type: %d\n", label->upl_type);
        fprintf(stream, "  disk: %s\n", label->upl_typename);
        fprintf(stream, "  label: %s\n", label->upl_packname);
        fprintf(stream, "  flags: %08x\n", label->upl_flags);
        fprintf(stream, "  bytes/sector: %d\n", label->upl_secsize);
        fprintf(stream, "  sectors/track: %d\n", label->upl_nsectors);
        fprintf(stream, "  tracks/cylinder: %d\n", label->upl_ntracks);
        fprintf(stream, "  sectors/cylinder: %d\n",label->upl_secpercyl);
        fprintf(stream, "  cylinders: %d\n", label->upl_ncylinders);
        fprintf(stream, "  total sectors: %d\n", label->upl_secperunit);
        fprintf(stream, "  rpm: %d\n", label->upl_rpm);
        fprintf(stream, "  interleave: %d\n", label->upl_interleave);
        fprintf(stream, "  trackskew: %d\n", label->upl_trackskew);
        fprintf(stream, "  cylinderskew: %d\n", label->upl_cylskew);
        fprintf(stream, "  headswitch: %d\n", label->upl_headswitch);
        fprintf(stream, "  track-to-track seek: %d\n", label->upl_trkseek);
        fprintf(stream, "  drive data:");
        for(jj = 0; LABEL_COUNT_DRIVEDATA > jj; jj++)
            fprintf(stream, " %d", label->upl_drivedata[jj]);
        fputc('\n', stream);
        fprintf(stream, "  disklabel byte order: %s\n",
                (label->upl_bigendian ? "big endian" : "little endian"));
        fputc('\n', stream);
        fprintf(stream, "      %10s %10s %7s %5s %5s %5s\n",
                "size", "offset", "fstype", "fsize", "bsize", "cpg");
    }
    else
        fprintf(stream, "      %10s %10s %7s\n", "size", "offset", "fstype");
    for(ii = 0; label->upl_npartitions > ii; ii++)
    {
        part = &label->upl_partitions[ii];
        fragblock = DISKLABELV1_FFS_FRAGBLOCK(part->uplp_fsize, part->uplp_frag);
        frag = DISKLABELV1_FFS_FRAG(fragblock);
        fsize = DISKLABELV1_FFS_FSIZE(fragblock);
        /* skip zero-length partitions unless verbose */
        if(!opt->upo_verbose && 0 == part->uplp_size)
            continue;
        /* print partition letter, size, and offset */
        fprintf(stream, " %c:   %10u %10u",
                'a' + ii, part->uplp_size, part->uplp_offset);
        /* print filesystem type name or number */
        if(part->uplp_fstype < sizeof(up_fstypes) / sizeof(up_fstypes[0]))
            fprintf(stream, " %7s", up_fstypes[part->uplp_fstype]);
        else
            fprintf(stream, " %7u", part->uplp_fstype);
        /* print filesystem-specific extra data */
        if(opt->upo_verbose)
        {
            if(LABEL_FSTYPE_UNUSED == part->uplp_fstype)
                fprintf(stream, " %5u %5u",
                        part->uplp_fsize, part->uplp_fsize * part->uplp_frag);
            else if(LABEL_FSTYPE_42BSD == part->uplp_fstype)
                fprintf(stream, " %5u %5u %5u", part->uplp_fsize,
                        part->uplp_fsize * part->uplp_frag, part->uplp_cpg);
        }
        fputc('\n', stream);
    }

    if(opt->upo_verbose)
    {
        fprintf(stream, "\nDump of %s disklabel at sector %"PRId64
                " (0x%"PRIx64") offset %d (0x%x):\n", disk->upd_name,
                label->upl_base + label->upl_sectoff,
                label->upl_base + label->upl_sectoff,
                label->upl_byteoff, label->upl_byteoff);
        up_hexdump(label->upl_buf, label->upl_buflen, label->upl_byteoff +
                   (label->upl_base + label->upl_sectoff) * disk->upd_sectsize,
                   stream);
        putc('\n', stream);
    }
}
