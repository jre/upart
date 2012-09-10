#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/*
  OpenBSD non-512b sector bug detection.
*/
#define LABEL_BUG_ADJUST(d, s)	((s) / (UP_DISK_1SECT(d) / 512))
#define LABEL_BUG_GOODLABEL	(1<<0)
#define LABEL_BUG_BADLABEL	(1<<1)
#define LABEL_BUG_USEBADLABEL	(1<<2)

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

struct up_bsd_p {
	uint32_t magic1;			/* d_magic */
	uint16_t disktype;			/* d_type */
	uint16_t subtype;			/* d_subtype */
	char typename[16];			/* d_typename */
	char packname[16];			/* d_packname */
	uint32_t sectsize;			/* d_secsize */
	uint32_t sectpertrack;			/* d_nsectors */
	uint32_t trackpercyl;			/* d_ntracks */
	uint32_t cylcount;			/* d_ncylinders */
	uint32_t sectpercyl;			/* d_secpercyl */
	uint32_t sectcount;			/* d_secperunit */

	/* OpenBSD changed things here to add a UID */
	union {
		/* traditional BSD layout */
		struct {
			uint16_t sparepertrack;	/* d_sparespertrack */
			uint16_t sparepercyl;	/* d_sparespercyl */
			uint32_t altcyls;	/* d_acylinders */
			uint16_t rpm;		/* d_rpm */
			uint16_t interleave;	/* d_interleave */
		} s_nouid;
		/* OpenBSD with unique identifier */
		struct {
			uint8_t uid[8];		/* d_uid */
			uint32_t uid_altcyls;	/* d_acylinders */
		} s_uid;
	} u_uid;

	/* these fields are now garbage in OpenBSD labels */
	uint16_t trackskew;			/* d_trackskew */
	uint16_t cylskew;			/* d_cylskew */
	uint32_t headswitch;			/* d_headswitch */
	uint32_t trackseek;			/* d_trkseek */

	uint32_t flags;				/* d_flags */
	uint32_t drivedata[5];			/* d_drivedata */

	/* OpenBSD used a spare field to add these */
	uint16_t v1_sectcount_h;		/* d_secperunith */
	uint16_t obsd_version;			/* d_version */

	uint32_t spare[4];			/* d_spare */
	uint32_t magic2;			/* d_magic2 */
	uint16_t checksum;			/* d_checksum */
	uint16_t maxpart;			/* d_npartitions */
	uint32_t bootsize;			/* d_bbsize */
	uint32_t superblockmax;			/* d_sbsize */
};

/* traditional BSD layout */
struct up_bsdpart0_p {
	uint32_t size;				/* p_size */
	uint32_t start;				/* p_offset */
	uint32_t fsize;				/* p_fsize */
	uint8_t type;				/* p_fstype */
	uint8_t frags;				/* p_frag */
	uint16_t cpg;				/* p_cpg */
};

/* OpenBSD version 1 layout */
struct up_bsdpart1_p {
	uint32_t size_l;			/* p_size */
	uint32_t start_l;			/* p_offset */
	uint16_t start_h;			/* p_offseth */
	uint16_t size_h;			/* p_sizeh */
	uint8_t type;				/* p_fstype */
	uint8_t bf;				/* p_fragblock */
	uint16_t cpg;				/* p_cpg */
};

#pragma pack()

struct up_bsd {
	int64_t startsect;
	int sectoff;
	int byteoff;
	int endian;
	int version;
	int bugs;
	struct up_bsd_p label;
};

struct up_bsdpart {
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

static int	bsdlabel_load(const struct disk *, const struct part *,
    void **);
static int	bsdlabel_setup(struct disk *, struct map *);
static int bsdlabel_getpart_v0(struct map *, struct up_bsdpart *,
    const uint8_t *);
static int bsdlabel_getpart_v1(struct map *, struct up_bsdpart *,
    const uint8_t *);
static int	bsdlabel_info(const struct map *, FILE *);
static int	bsdlabel_index(const struct part *, char *, size_t);
static int	bsdlabel_extrahdr(const struct map *, FILE *);
static int	bsdlabel_extra(const struct part *, FILE *);
static int	bsdlabel_dump(const struct map *, int64_t,
    const void *, int64_t, int, FILE *);
static int	bsdlabel_scan(const struct disk *, int64_t,
    int64_t, int64_t *, int *, int *);
static int	bsdlabel_read(const struct disk *, int64_t,
    int64_t, const uint8_t **, int *, int *);
static uint16_t bsdlabel_cksum(struct up_bsd_p *hdr,
                               const uint8_t *partitions, int size);

void up_bsdlabel_register(void)
{
	struct map_funcs funcs;

	up_map_funcs_init(&funcs);
	funcs.label = LABEL_LABEL;
	funcs.load = bsdlabel_load;
	funcs.setup = bsdlabel_setup;
	funcs.print_header = bsdlabel_info;
	funcs.get_index = bsdlabel_index;
	funcs.print_extrahdr = bsdlabel_extrahdr;
	funcs.print_extra = bsdlabel_extra;
	funcs.dump_extra = bsdlabel_dump;

	up_map_register(UP_MAP_BSD, &funcs);
}

static int
bsdlabel_load(const struct disk *disk, const struct part *parent, void **priv)
{
	int res, sectoff, byteoff, endian, size, bugflags;
	struct up_bsd *label;
	const uint8_t *buf;
	int64_t startsect;

	/* placate gcc */
	startsect = 0;
	sectoff = 0;

	assert(LABEL_BASE_SIZE == sizeof(struct up_bsd_p) &&
	    LABEL_PART_SIZE == sizeof(struct up_bsdpart0_p) &&
	    LABEL_PART_SIZE == sizeof(struct up_bsdpart1_p));
	*priv = NULL;

	/* search for disklabel */
	if ((res = bsdlabel_scan(disk, parent->start, parent->size,
		    &startsect, &sectoff, &bugflags)) <= 0 ||
	    (res = bsdlabel_read(disk, startsect + sectoff,
		parent->size - sectoff, &buf, &byteoff, &endian)) <= 0)
		return (res);
	assert(UP_DISK_1SECT(disk) - LABEL_BASE_SIZE >= byteoff);

	/* allocate label struct */
	if ((label = xalloc(1, sizeof(*label), XA_ZERO)) == NULL)
		return (-1);

	/* populate label struct */
	label->startsect = startsect;
	label->sectoff = sectoff;
	label->byteoff = byteoff;
	label->endian = endian;
	label->bugs = bugflags;
	assert(byteoff + sizeof(label->label) <= UP_DISK_1SECT(disk));
	memcpy(&label->label, buf + byteoff, sizeof label->label);
	label->version = LABEL_LGETINT16(label, obsd_version);

	/* warn about the big sector bug */
	if (bugflags & LABEL_BUG_BADLABEL && UP_NOISY(QUIET)) {
		if (bugflags & LABEL_BUG_USEBADLABEL) {
			assert(startsect == LABEL_BUG_ADJUST(disk, parent->start));
			up_warn("%d-byte sector bug: using misplaced %s at "
			    "sector %"PRId64" (offset %d), should be at "
			    "sector %"PRId64,
			    UP_DISK_1SECT(disk), LABEL_LABEL, startsect,
			    label->sectoff, parent->start);
		} else {
			assert(startsect == parent->start);
			up_warn("%d-byte sector bug: ignoring misplaced %s at "
			    "sector %"PRId64" (offset %d)",
			    UP_DISK_1SECT(disk), LABEL_LABEL,
			    LABEL_BUG_ADJUST(disk, startsect), label->sectoff);
		}
	}

	/* check if the label extends past the end of the sector */
	size = LABEL_BASE_SIZE + (LABEL_PART_SIZE *
	    LABEL_LGETINT16(label, maxpart));
	if (byteoff + size > UP_DISK_1SECT(disk)) {
		if (UP_NOISY(QUIET))
			up_err("ignoring truncated %s in sector %"PRId64" "
			    "(offset %d)",
			    LABEL_LABEL, startsect, label->sectoff);
		free(label);
		return (-1);
	}

	*priv = label;

	return (1);
}

static int
bsdlabel_setup(struct disk *disk, struct map *map)
{
	int (*getpart)(struct map *, struct up_bsdpart *, const uint8_t *);
	struct up_bsd *label = map->priv;
	struct up_bsdpart *part;
	const uint8_t *buf;
	int i, max;

	/* save the disklabel we're using */
	if ((buf = up_disk_save1sect(disk, label->startsect + label->sectoff,
		    map, 0)) == NULL)
		return (-1);

	/* if we found both a bad label but aren't using it, save it
	   as well */
	if ((label->bugs & (LABEL_BUG_BADLABEL|LABEL_BUG_USEBADLABEL)) ==
	    LABEL_BUG_BADLABEL &&
	    (buf = up_disk_save1sect(disk, LABEL_BUG_ADJUST(disk, map->start) +
		label->sectoff, map, 0)) == NULL)
		return (-1);

	max = LABEL_LGETINT16(label, maxpart);
	buf += label->byteoff + LABEL_BASE_SIZE;
	assert(UP_DISK_1SECT(disk) >=
	    label->byteoff + LABEL_BASE_SIZE + (LABEL_PART_SIZE * max));

	/* verify the checksum */
	if (bsdlabel_cksum(&label->label, buf, (LABEL_PART_SIZE * max)) !=
	    label->label.checksum) {
		if (UP_NOISY(QUIET))
			up_msg((opts->relaxed ? UP_MSG_FWARN : UP_MSG_FERR),
			    "%s with bad checksum in sector %"PRId64" "
			    "(offset %d)", up_map_label(map), label->startsect,
			    label->sectoff);
		if (!opts->relaxed)
			return (0);
	}

	getpart = (label->version == 0 ?
	    bsdlabel_getpart_v0 : bsdlabel_getpart_v1);
	for (i = 0; i < max; i++) {
		if ((part = xalloc(1, sizeof(*part), XA_ZERO)) == NULL)
			return (-1);

		part->index = i;
		if (!getpart(map, part, buf + (LABEL_PART_SIZE * i))) {
			free(part);
			return (-1);
		}
	}

	return (1);
}

static int
bsdlabel_getpart_v0(struct map *map, struct up_bsdpart *part, const uint8_t *buf)
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
bsdlabel_getpart_v1(struct map *map, struct up_bsdpart *part, const uint8_t *buf)
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
bsdlabel_info(const struct map *map, FILE *stream)
{
	struct up_bsd *priv;
	char typename[sizeof(priv->label.typename)+1];
	char packname[sizeof(priv->label.packname)+1];
	uint64_t sectcount;
	uint16_t disktype;
	char *disktypestr;
	int uid, i;

	if (!UP_NOISY(NORMAL))
		return (0);

	priv = map->priv;

	if (fprintf(stream, "%s at ", (priv->version > 0 ?
		    "OpenBSD disklabel" : up_map_label(map))) < 0 ||
	    printsect_verbose(priv->startsect, stream) < 0 ||
	    fprintf(stream, " (offset %d) of %s",
		priv->sectoff, UP_DISK_PATH(map->disk)) < 0 ||
	    (priv->bugs & LABEL_BUG_USEBADLABEL &&
		(fputs(" (should be at ", stream) == EOF ||
		printsect_verbose(map->start, stream) < 0 ||
		fputs(")", stream) == EOF)) ||
	    fputs(":\n", stream) == EOF)
		return (-1);

	if (!UP_NOISY(EXTRA))
		return (1);

	if (priv->version > 0 &&
	    fprintf(stream, "  version: %u\n", priv->version) < 0)
		return (-1);

        disktype = LABEL_LGETINT16(priv, disktype);
        disktypestr = (disktype < sizeof(up_disktypes) /
	    sizeof(up_disktypes[0]) ? up_disktypes[disktype] : "");
        memcpy(typename, priv->label.typename, sizeof(priv->label.typename));
        typename[sizeof(typename)-1] = 0;
        memcpy(packname, priv->label.packname, sizeof(priv->label.packname));
        packname[sizeof(packname)-1] = 0;
	sectcount = LABEL_LGETINT32(priv, sectcount);
	uid = 0;
	if (priv->version > 0) {
		sectcount |= (uint64_t)LABEL_LGETINT16(priv, v1_sectcount_h)
		    << 32;
		for (i = 0; i < sizeof(priv->label.u_uid.s_uid.uid); i++) {
			if (priv->label.u_uid.s_uid.uid[i] != 0) {
				uid = 1;
				break;
			}
		}
	}

	if (fprintf(stream,
		"  type: %s (%u)\n"
		"  disk: %s\n"
		"  label: %s\n"
		"  flags: %08x\n"
		"  bytes/sector: %u\n"
		"  sectors/track: %u\n"
		"  tracks/cylinder: %u\n"
		"  sectors/cylinder: %u\n"
		"  cylinders: %u\n"
		"  total sectors: %"PRId64"\n",
		disktypestr, disktype, typename, packname,
		LABEL_LGETINT32(priv, flags),
		LABEL_LGETINT32(priv, sectsize),
		LABEL_LGETINT32(priv, sectpertrack),
		LABEL_LGETINT32(priv, trackpercyl),
		LABEL_LGETINT32(priv, sectpercyl),
		LABEL_LGETINT32(priv, cylcount),
		sectcount) < 0)
		return (-1);

        if (UP_NOISY(SPAM)) {
		if (uid && fprintf(stream,
			"  alternate cylinders: %u\n",
			LABEL_LGETINT32(priv, u_uid.s_uid.uid_altcyls)) < 0)
			return (-1);
		if (!uid && fprintf(stream,
			"  spares/track: %u\n"
			"  spares/cylinder: %u\n"
			"  alternate cylinders: %u\n"
			"  rpm: %u\n"
			"  interleave: %u\n",
			LABEL_LGETINT32(priv, u_uid.s_nouid.sparepertrack),
			LABEL_LGETINT32(priv, u_uid.s_nouid.sparepercyl),
			LABEL_LGETINT32(priv, u_uid.s_nouid.altcyls),
			LABEL_LGETINT16(priv, u_uid.s_nouid.rpm),
			LABEL_LGETINT16(priv, u_uid.s_nouid.interleave)) < 0)
			return (-1);
		if (fprintf(stream,
			"  trackskew: %u\n"
			"  cylinderskew: %u\n"
			"  headswitch: %u\n"
			"  track-to-track seek: %u\n"
			"  bootsize: %u\n"
			"  superblock max size: %u\n"
			"  drivedata:",
			LABEL_LGETINT16(priv, trackskew),
			LABEL_LGETINT16(priv, cylskew),
			LABEL_LGETINT32(priv, headswitch),
			LABEL_LGETINT32(priv, trackseek),
			LABEL_LGETINT32(priv, bootsize),
			LABEL_LGETINT32(priv, superblockmax)) < 0)
			return (-1);
		for (i = 0; i < NITEMS(priv->label.drivedata); i++)
			if (fprintf(stream, " %d",
				LABEL_LGETINT32(priv, drivedata[i])) < 0)
				return (-1);
		if (fprintf(stream, "\n") < 0)
			return (-1);
	}

	if (uid) {
		if (fprintf(stream, "  uid: ") < 0)
			return (-1);
		for (i = 0; i < sizeof(priv->label.u_uid.s_uid.uid); i++)
			if (fprintf(stream, "%02x",
				priv->label.u_uid.s_uid.uid[i]) < 0)
				return (-1);
		if (fprintf(stream, "\n") < 0)
			return (-1);
	}

        return (fprintf(stream,
		"  byte order: %s endian\n"
		"  partition count: %u\n\n",
		(UP_ENDIAN_BIG == priv->endian ? "big" : "little"),
		LABEL_LGETINT16(priv, maxpart)));
}

static int
bsdlabel_index(const struct part *part, char *buf, size_t size)
{
	struct up_bsdpart *priv;

	priv = part->priv;
	return (snprintf(buf, size, "%c", 'a' + priv->index));
}

static int
bsdlabel_extrahdr(const struct map *map, FILE *stream)
{
	const char *hdr;

	hdr = UP_BSDLABEL_FMT_HDR();
	if (hdr == NULL)
		return (0);
        return (fprintf(stream, " %s", hdr));
}

static int
bsdlabel_extra(const struct part *part, FILE *stream)
{
	struct up_bsdpart *priv;

	priv = part->priv;
	return (up_bsdlabel_fmt(part, priv->type,
		priv->fsize, priv->frags, priv->cpg, stream));
}

static int
bsdlabel_dump(const struct map *map, int64_t start, const void *data,
              int64_t size, int tag, FILE *stream)
{
	struct up_bsd *priv;

	priv = map->priv;
	return (fprintf(stream, " offset %d (0x%x)",
		priv->byteoff, priv->byteoff));
}

static int
bsdlabel_scan(const struct disk *disk, int64_t start, int64_t size,
    int64_t *realstart, int *off, int *flags)
{
	int i, j;

	*flags = 0;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < LABEL_PROBE_SECTS; j++) {
			switch (bsdlabel_read(disk, start + j, size - j,
				NULL, NULL, NULL)) {
			case -1:
				return -1;
			case 0:
				continue;
			case 1:
				if (!*flags) {
					*off = j;
					*realstart = start;
				}
				*flags |= (i ? LABEL_BUG_BADLABEL : LABEL_BUG_GOODLABEL);
				goto found;
			}
		}
	found:
		if (LABEL_BUG_ADJUST(disk, start) == start)
			break;
		start = LABEL_BUG_ADJUST(disk, start);
	}

	if (*flags == LABEL_BUG_BADLABEL)
		*flags |= LABEL_BUG_USEBADLABEL;

	if (*flags)
		return (1);
	else
		return (0);
}

static int
bsdlabel_read(const struct disk *disk, int64_t start, int64_t size,
              const uint8_t **bufret, int *off, int *endret)
{
	uint32_t magic1, magic2;
	const uint8_t *buf;
	int i, endian;

	if (size <= 0)
		return (0);

	if (up_disk_check1sect(disk, start))
		return (0);
	buf = up_disk_getsect(disk, start);
	if (!buf)
		return (-1);

	for (i = 0; i <= UP_DISK_1SECT(disk) - LABEL_BASE_SIZE; i++) {
		if (LABEL_MAGIC0BE == buf[LABEL_OFF_MAGIC1+i] &&
		    LABEL_MAGIC0BE == buf[LABEL_OFF_MAGIC2+i])
			endian = UP_ENDIAN_BIG;
		else if (LABEL_MAGIC0LE == buf[LABEL_OFF_MAGIC1+i] &&
		    LABEL_MAGIC0LE == buf[LABEL_OFF_MAGIC2+i])
			endian = UP_ENDIAN_LITTLE;
		else
			continue;
		memcpy(&magic1, buf + i + LABEL_OFF_MAGIC1, sizeof(magic1));
		memcpy(&magic2, buf + i + LABEL_OFF_MAGIC2, sizeof(magic2));
		if (UP_ETOH32(magic1, endian) == LABEL_MAGIC &&
		    magic1 == magic2) {
			if (bufret)
				*bufret = buf;
			if (off)
				*off = i;
			if (endret)
				*endret = endian;
			return (1);
		}
	}

	return (0);
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
up_bsdlabel_fmt(const struct part *part, int type,
    uint32_t fsize, int frags, int cpg, FILE *stream)
{
	const char *typestr;

	if (!UP_NOISY(NORMAL))
		return (0);

	typestr = up_bsdlabel_fstype(type);

	if (NULL == typestr)
		return (fprintf(stream, " %u", type));
	else if (UP_NOISY(EXTRA) &&
	    UP_BSDLABEL_FSTYPE_UNUSED == type && part->size)
		return (fprintf(stream, " %-7s %5u %5u",
			typestr, fsize, fsize * frags));
	else if(UP_NOISY(EXTRA) && UP_BSDLABEL_FSTYPE_42BSD == type)
		return (fprintf(stream, " %-7s %5u %5u %5u",
			typestr, fsize, fsize * frags, cpg));
	else
		return (fprintf(stream, " %s", typestr));
}
