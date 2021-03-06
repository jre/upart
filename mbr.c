/* 
 * Copyright (c) 2007-2012 Joshua R. Elsasser.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

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
#define MBR_ID_EXTDOS           (0x05)
#define MBR_ID_EXTWIN           (0x0f)
#define MBR_ID_UNUSED           (0x00)
#define MBR_EXTPART             (0)
#define MBR_EXTNEXT             (1)

#define MBR_GETSECT(sc)         ((sc)[0] & 0x3f)
#define MBR_GETCYL(sc)          ((((uint16_t)((sc)[0] & 0xc0)) << 2) | (sc)[1])
#define MBR_ID_IS_EXT(id)       (MBR_ID_EXTDOS == (id) || MBR_ID_EXTWIN == (id))

#pragma pack(1)

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
};

struct up_mbr_p
{
    uint8_t             bootcode[446];
    struct up_mbrpart_p part[MBR_PART_COUNT];
    uint16_t            magic;
};

#pragma pack()

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

static int	mbr_load(const struct disk *, const struct part *,
    void **);
static int	mbrext_load(const struct disk *, const struct part *,
    void **);
static int	mbr_setup(struct disk *, struct map *);
static int	mbrext_setup(struct disk *, struct map *);
static int	mbr_getinfo(const struct map *, FILE *);
static int	mbr_getindex(const struct part *, char *, size_t);
static int	mbr_getextrahdr(const struct map *, FILE *);
static int	mbr_getextra(const struct part *, FILE *);
static int	mbr_addpart(struct map *, const struct up_mbrpart_p *,
    int, int64_t, const struct up_mbr_p *);
static int	mbr_read(const struct disk *, int64_t, int64_t,
    const struct up_mbr_p **);
static const char *mbr_name(uint8_t type);

void
up_mbr_register(void)
{
	struct map_funcs funcs;

	up_map_funcs_init(&funcs);
	funcs.label = "MBR";
	funcs.load = mbr_load;
	funcs.setup = mbr_setup;
	funcs.print_header = mbr_getinfo;
	funcs.get_index = mbr_getindex;
	funcs.print_extrahdr = mbr_getextrahdr;
	funcs.print_extra = mbr_getextra;
	up_map_register(UP_MAP_MBR, &funcs);

	up_map_funcs_init(&funcs);
	funcs.label = "extended MBR";
	funcs.flags |= UP_TYPE_NOPRINTHDR;
	funcs.load = mbrext_load;
	funcs.setup = mbrext_setup;
	funcs.get_index = mbr_getindex;
	funcs.print_extrahdr = mbr_getextrahdr;
	funcs.print_extra = mbr_getextra;
	funcs.free_mappriv = NULL;
	up_map_register(UP_MAP_MBREXT, &funcs);
}

static int
mbr_load(const struct disk *disk, const struct part *parent, void **priv)
{
	const struct up_mbr_p *buf;
	struct up_mbr *mbr;
	int res;

	assert(MBR_SIZE == sizeof(*buf));
	*priv = NULL;

	/*
	  Don't load if there's a parent map to avoid false positives
	  with partition boot sectors.
	*/
	if (parent->map && !(parent->flags & UP_PART_VIRTDISK))
		return (0);

	/* load the mbr sector */
	if ((res = mbr_read(disk, UP_PART_PHYSADDR(parent), parent->size,
		    &buf)) <= 0)
		return (res);

	/* create map private struct */
	if ((mbr = xalloc(1, sizeof(*mbr), XA_ZERO)) == NULL)
		return (-1);

	mbr->mbr = *buf;
	mbr->extcount = 0;
	*priv = mbr;

	return (1);
}

static int
mbrext_load(const struct disk *disk, const struct part *parent,
    void **priv)
{
    *priv = NULL;

    /* refuse to load unless parent is the right type of mbr partition */
    return (NULL != parent->map && UP_MAP_MBR == parent->map->type &&
            MBR_ID_IS_EXT(((const struct up_mbrpart*)parent->priv)->part.type));
}

static int
mbr_setup(struct disk *disk, struct map *map)
{
    struct up_mbr              *mbr = map->priv;
    int                         ii;

    if(!up_disk_save1sect(disk, UP_MAP_PHYSADDR(map), map, 0))
        return -1;

    /* add primary partitions */
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
        if(0 > mbr_addpart(map, &mbr->mbr.part[ii], ii, 0, NULL))
            return -1;

    return 1;
}

static int
mbrext_setup(struct disk *disk, struct map *map)
{
	int64_t diskoff, reloff, physoff;
	const struct up_mbr_p *buf;
	struct up_mbr *parent;
	int index;

	assert(map->parent->map->type == UP_MAP_MBR);

	parent = map->parent->map->priv;
	diskoff = UP_MAP_VIRTADDR(map);
	reloff = 0;
	index = MBR_PART_COUNT + parent->extcount;

	for(;;) {
		/* load extended mbr */
		assert(diskoff >= UP_MAP_VIRTADDR(map) &&
		    diskoff - UP_MAP_VIRTADDR(map) < map->size);
		physoff = UP_MAP_VIRT_TO_PHYS(map, diskoff);
		if (!(buf = up_disk_save1sect(disk, physoff, map, 1)))
			return (-1);

		if (UP_LETOH16(buf->magic) != MBR_MAGIC) {
			if (UP_NOISY(QUIET))
				up_msg((opts->relaxed ?
					UP_MSG_FWARN : UP_MSG_FERR),
				    "extended MBR in sector %"PRId64" has "
				    "invalid magic number", physoff);
			if (!opts->relaxed)
				return 0;
		}

		if (mbr_addpart(map, &buf->part[MBR_EXTPART], index, diskoff,
			    buf) < 0)
			return (-1);

		if (MBR_ID_UNUSED == buf->part[MBR_EXTNEXT].type)
			break;
		else if (!MBR_ID_IS_EXT(buf->part[MBR_EXTNEXT].type)) {
			if (UP_NOISY(QUIET))
				up_msg((opts->relaxed ?
					UP_MSG_FWARN : UP_MSG_FERR),
				    "extended MBR in sector %"PRId64" has "
				    "invalid type for partition %d 0x%02x",
				    physoff, MBR_EXTNEXT,
				    buf->part[MBR_EXTNEXT].type);
			if (!opts->relaxed)
				return (0);
			break;
		}

		index++;

		reloff = UP_LETOH32(buf->part[MBR_EXTNEXT].start);
		diskoff = UP_MAP_VIRTADDR(map) + reloff;

		if (reloff < 0 || reloff >= map->size) {
			if (UP_NOISY(QUIET))
				up_warn("logical MBR partition %d out of "
				    "range: offset %"PRId64"+%"PRId64" size "
				    "%"PRId64, index, UP_MAP_VIRTADDR(map),
				    reloff, map->size);
			break;
		}
	}

	parent->extcount = index - MBR_PART_COUNT;

	return (1);
}

static int
mbr_getinfo(const struct map *map, FILE *stream)
{
	if (!UP_NOISY(NORMAL))
		return (0);

	if (fprintf(stream, "%s partition table at ", up_map_label(map)) < 0 ||
	    printsect_verbose(UP_MAP_VIRTADDR(map), stream) < 0 ||
	    fprintf(stream, " of %s:\n", UP_DISK_PATH(map->disk)) < 0)
		return (-1);
	return (1);
}

static int
mbr_getindex(const struct part *part, char *buf, size_t size)
{
	struct up_mbrpart *priv;

	priv = part->priv;
	return (snprintf(buf, size, "%d", priv->index));
}

static int
mbr_getextrahdr(const struct map *map, FILE *stream)
{
	if (!UP_NOISY(NORMAL))
		return (0);

	if (UP_NOISY(EXTRA))
		return (fprintf(stream, " A    C   H  S    C   H  S Type"));
	else
		return (fprintf(stream, " A Type"));
}

static int
mbr_getextra(const struct part *part, FILE *stream)
{
	int firstcyl, firstsect, lastcyl, lastsect;
	struct up_mbrpart *priv;
	const char *label;
	char active;

	if (!UP_NOISY(NORMAL))
		return (0);

	priv = part->priv;

	label = mbr_name(priv->part.type);
	active = MBR_FLAG_ACTIVE & priv->part.flags ? '*' : ' ';
	firstcyl = MBR_GETCYL(priv->part.firstsectcyl);
	firstsect = MBR_GETSECT(priv->part.firstsectcyl);
	lastcyl = MBR_GETCYL(priv->part.lastsectcyl);
	lastsect = MBR_GETSECT(priv->part.lastsectcyl);

	if (UP_NOISY(EXTRA))
		return (fprintf(stream, " %c %4u/%3u/%2u-%4u/%3u/%2u %s "
			"(0x%02x)", active, firstcyl, priv->part.firsthead,
			firstsect, lastcyl, priv->part.lasthead, lastsect,
			label, priv->part.type));
	else
		return (fprintf(stream, " %c %s (0x%02x)",
			active, label, priv->part.type));
}

static int
mbr_addpart(struct map *map, const struct up_mbrpart_p *part, int index,
    int64_t extoff, const struct up_mbr_p *extmbr)
{
	struct up_mbrpart *priv;
	int flags;

	assert((MBR_PART_COUNT > index && 0 == extoff && !extmbr) ||
	    (MBR_PART_COUNT <= index && 0 < extoff && extmbr));

	if ((priv = xalloc(1, sizeof(*priv), XA_ZERO)) == NULL)
		return (-1);

	priv->part = *part;
	part = &priv->part;

	priv->part.start = UP_LETOH32(part->start) + extoff;
	priv->part.size = UP_LETOH32(part->size);
	priv->index = index;
	priv->extoff = extoff;
	if (extmbr != NULL)
		priv->extmbr = *extmbr;

	flags = 0;
	if (part->type == MBR_ID_UNUSED)
		flags |= UP_PART_EMPTY;

	if (!up_map_add(map, part->start, part->size, flags, priv)) {
		free(priv);
		return (-1);
	}

	return (0);
}

static int
mbr_read(const struct disk *disk, int64_t start, int64_t size,
    const struct up_mbr_p **mbr)
{
    const void *buf;

    *mbr = NULL;

    if(0 >= size || sizeof *mbr > UP_DISK_1SECT(disk))
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
    /* 0xa8 */ "Apple UFS",
    /* 0xa9 */ "NetBSD",
    /* 0xaa */ NULL,
    /* 0xab */ "Apple Boot",
    /* 0xac */ NULL,
    /* 0xad */ NULL,
    /* 0xae */ NULL,
    /* 0xaf */ "Apple HFS+",
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
