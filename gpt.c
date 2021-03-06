/* 
 * Copyright (c) 2008-2012 Joshua R. Elsasser.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "disk.h"
#include "gpt.h"
#include "map.h"
#include "util.h"

/* XXX this code needs more intelligent handling of a bad
   primary/secondady GPT, and more consistency checking between the two */

#define GPT_SIZE                0x5c
#define GPT_PRIOFF(st, sz)      ((st) + UINT64_C(1))
#define GPT_PRISIZE(st, sz)     ((sz) - UINT64_C(1))
#define GPT_SECOFF(st, sz)      ((st) + (sz) - UINT64_C(1))
#define GPT_SECSIZE(st, sz)     (UINT64_C(1))
#define GPT_OFFSEC              -1
#define GPT_MAGIC               UINT64_C(0x5452415020494645)
#define GPT_REVISION            0x10000
#define GPT_GUID_FMT \
    "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x"
#define GPT_GUID_FMT_ARGS(guid) \
    UP_LETOH32((guid)->data1), UP_LETOH16((guid)->data2), \
    UP_LETOH16((guid)->data3), (guid)->data4[0], (guid)->data4[1], \
    (guid)->data4[2], (guid)->data4[3], (guid)->data4[4], (guid)->data4[5], \
    (guid)->data4[6], (guid)->data4[7]
#define GPT_GUID_DATA4_SIZE     8
#define GPT_PART_SIZE           0x80
#define GPT_NAME_SIZE           0x48

#pragma pack(1)

struct up_guid_p
{
    uint32_t            data1;
    uint16_t            data2;
    uint16_t            data3;
    uint8_t             data4[GPT_GUID_DATA4_SIZE];
};

struct up_gpt_p
{
    uint64_t            magic;
    uint32_t            revision;
    uint32_t            size;
    uint32_t            gptcrc;
    uint32_t            unused;
    uint64_t            gpt1sect;
    uint64_t            gpt2sect;
    uint64_t            firstsect;
    uint64_t            lastsect;
    struct up_guid_p    guid;
    uint64_t            partsect;
    uint32_t            maxpart;
    uint32_t            partsize;
    uint32_t            partcrc;
};

struct up_gptpart_p
{
    struct up_guid_p    type;
    struct up_guid_p    guid;
    uint64_t            start;
    uint64_t            end;
    uint64_t            flags;
    char                name[GPT_NAME_SIZE];
};

#pragma pack()

struct up_gptpart
{
    struct up_gptpart_p part;
    int                 index;
};

struct up_gpt
{
    struct up_gpt_p     gpt;
    int                 partitions;
};

static int	gpt_load(const struct disk *, const struct part *,
    void **);
static int	gpt_setup(struct disk *, struct map *);
static int	gpt_getinfo(const struct map *, FILE *);
static int	gpt_getindex(const struct part *, char *, size_t);
static int	gpt_getextrahdr(const struct map *, FILE *);
static int	gpt_getextra(const struct part *, FILE *);
static int	gpt_findhdr(const struct disk *, int64_t, int64_t,
    struct up_gpt_p *);
static int	gpt_readhdr(const struct disk *, int64_t, int64_t,
    const struct up_gpt_p **);
static int	gpt_checkcrc(struct up_gpt_p *);
static const char *gpt_typename(const struct up_guid_p *);

void
up_gpt_register(void)
{
	struct map_funcs funcs;

	up_map_funcs_init(&funcs);
	funcs.label = "EFI GPT";
	funcs.load = gpt_load;
	funcs.setup = gpt_setup;
	funcs.print_header = gpt_getinfo;
	funcs.get_index = gpt_getindex;
	funcs.print_extrahdr = gpt_getextrahdr;
	funcs.print_extra = gpt_getextra;

	up_map_register(UP_MAP_GPT, &funcs);
}

int
gpt_load(const struct disk *disk, const struct part *parent, void **priv)
{
	struct up_gpt_p pk;
	struct up_gpt *gpt;
	int res;

	assert(GPT_SIZE == sizeof(pk) &&
	    GPT_PART_SIZE == sizeof(struct up_gptpart_p));
	*priv = NULL;

	/*
	  Try to load either the primary or secondary gpt headers, and
	  check the magic and crc.
	*/
	if ((res = gpt_findhdr(disk, UP_PART_PHYSADDR(parent), parent->size,
		    &pk)) <= 0)
		return (res);

	/* check revision */
	if (GPT_REVISION != UP_LETOH32(pk.revision)) {
		if (UP_NOISY(QUIET))
			up_err("gpt with unknown revision: %u.%u",
			    (UP_LETOH32(pk.revision) >> 16) & 0xffff,
			    UP_LETOH32(pk.revision) & 0xffff);
		return (0);
	}

	/* XXX validate other fields */

	/* create map private struct */
	if ((gpt = xalloc(1, sizeof(*gpt), XA_ZERO)) == NULL)
		return (-1);
	gpt->gpt = pk;
	*priv = gpt;

	return (1);
}

static int
gpt_setup(struct disk *disk, struct map *map)
{
	struct up_gpt *priv = map->priv;
	struct up_gpt_p *gpt = &priv->gpt;
	const struct up_gptpart_p *pk;
	uint64_t partsects, partbytes;
	const uint8_t *data1, *data2;
	struct up_gptpart *part;

	/* calculate partition buffer size */
	partbytes = UP_LETOH32(gpt->maxpart) * GPT_PART_SIZE;
	partsects = partbytes / UP_DISK_1SECT(disk);

	/* save sectors from primary and secondary maps */
	data1 = up_disk_savesectrange(disk,
	    GPT_PRIOFF(UP_MAP_PHYSADDR(map), map->size),
	    1 + partsects, map, 0);
	data2 = up_disk_savesectrange(disk,
	    GPT_SECOFF(UP_MAP_PHYSADDR(map), map->size) - partsects,
	    1 + partsects, map, 0);
	if (data1 == NULL || data2 == NULL)
		return (-1);

	/* verify the crc */
	if (UP_LETOH32(gpt->partcrc) !=
	    (up_crc32(data1 + UP_DISK_1SECT(disk),  partbytes, ~0) ^ ~0)) {
		if (UP_NOISY(QUIET))
			up_msg((opts->relaxed ? UP_MSG_FWARN : UP_MSG_FERR),
			    "bad gpt partition crc");
		if (!opts->relaxed)
			return (0);
	}

	/* walk through the partition buffer and add all partitions found */
	pk = (const struct up_gptpart_p *)(data1 + UP_DISK_1SECT(disk));
	while ((const uint8_t *)pk + sizeof(*pk) <=
	    data1 + UP_DISK_1SECT(disk) * partsects) {
		if ((part = xalloc(1, sizeof(*part), XA_ZERO)) == NULL)
			return (-1);
		part->part = *pk;
		part->index = priv->partitions;
		if (!up_map_add(map, UP_LETOH64(pk->start),
			UP_LETOH64(pk->end) - UP_LETOH64(pk->start),
			0, part)) {
			free(part);
			return (-1);
		}
		priv->partitions++;
		pk++;
	}

	return (1);
}

static int
gpt_getinfo(const struct map *map, FILE *stream)
{
	const struct up_gpt *gpt;

	if (!UP_NOISY(NORMAL))
		return (0);

	gpt = map->priv;

	if (fprintf(stream, "%s partition table at ",
		up_map_label(map)) < 0 ||
	    printsect_verbose(GPT_PRIOFF(UP_MAP_VIRTADDR(map), map->size),
		stream) < 0 ||
	    fputs(" (backup at ", stream) == EOF ||
	    printsect_verbose(GPT_SECOFF(UP_MAP_VIRTADDR(map), map->size),
		stream) < 0 ||
	    fprintf(stream, ") of %s:\n", UP_DISK_PATH(map->disk)) < 0)
		return (-1);

	if (UP_NOISY(EXTRA)) 
		return (fprintf(stream,
			"  size:                 %u\n"
			"  primary gpt sector:   %"PRIu64"\n"
			"  backup gpt sector:    %"PRIu64"\n"
			"  first data sector:    %"PRIu64"\n"
			"  last data sector:     %"PRIu64"\n"
			"  guid:                 "GPT_GUID_FMT"\n"
			"  partition sector:     %"PRIu64"\n"
			"  max partitions:       %u\n"
			"  partition size:       %u\n"
			"\n\n",
			UP_LETOH32(gpt->gpt.size),
			UP_LETOH64(gpt->gpt.gpt1sect),
			UP_LETOH64(gpt->gpt.gpt2sect),
			UP_LETOH64(gpt->gpt.firstsect),
			UP_LETOH64(gpt->gpt.lastsect),
			GPT_GUID_FMT_ARGS(&gpt->gpt.guid),
			UP_LETOH64(gpt->gpt.partsect),
			UP_LETOH32(gpt->gpt.maxpart),
			UP_LETOH32(gpt->gpt.partsize)));

	return (1);
}

static int
gpt_getindex(const struct part *part, char *buf, size_t size)
{
	const struct up_gptpart *priv;

	priv = part->priv;
	return (snprintf(buf, size, "%d", priv->index + 1));
}

static int
gpt_getextrahdr(const struct map *map, FILE *stream)
{
	if (!UP_NOISY(NORMAL))
		return (0);

        if (UP_NOISY(EXTRA))
		return (fprintf(stream, " %-36s Type", "GUID"));
        else
		return (fprintf(stream, " Type"));
}

static int
gpt_getextra(const struct part *part, FILE *stream)
{
	const struct up_gptpart *priv;
	const char *label;

	if (!UP_NOISY(NORMAL))
		return (0);

	priv = part->priv;
	label = gpt_typename(&priv->part.type);

	if (UP_NOISY(EXTRA))
		return (fprintf(stream, " "GPT_GUID_FMT" "GPT_GUID_FMT" %s",
                        GPT_GUID_FMT_ARGS(&priv->part.guid),
                        GPT_GUID_FMT_ARGS(&priv->part.type),
                        (label ? label : "")));
	else if (label)
		return (fprintf(stream, " %s", label));
        else
		return (fprintf(stream, " "GPT_GUID_FMT,
			GPT_GUID_FMT_ARGS(&priv->part.type)));
}

static int
gpt_findhdr(const struct disk *disk, int64_t start, int64_t size,
    struct up_gpt_p *gpt)
{
    const struct up_gpt_p  *buf;
    int                     res, badcrc;

    badcrc = 0;

    res = gpt_readhdr(disk, GPT_PRIOFF(start, size),
                      GPT_PRISIZE(start, size), &buf);
    if(0 >= res)
        return res;
    if(GPT_MAGIC == UP_LETOH64(buf->magic))
    {
        *gpt = *buf;
        if(gpt_checkcrc(gpt))
            return 1;
        if(UP_NOISY(QUIET))
            up_warn("bad crc on primary gpt in sector %"PRId64,
                    GPT_PRIOFF(start, size));
        badcrc = 1;
    }

    res = gpt_readhdr(disk, GPT_SECOFF(start, size),
                      GPT_SECSIZE(start, size), &buf);
    if(0 >= res)
        return res;
    if(GPT_MAGIC == UP_LETOH64(buf->magic))
    {
        *gpt = *buf;
        if(gpt_checkcrc(gpt))
            return 1;
        if(UP_NOISY(QUIET))
            up_err("bad crc on secondary gpt in sector %"PRId64,
                   GPT_SECOFF(start, size));
        badcrc = 1;
    }

    return (badcrc ? -1 : 0);
}

static int
gpt_readhdr(const struct disk *disk, int64_t start, int64_t size,
            const struct up_gpt_p **gpt)
{
    const void *buf;

    *gpt = NULL;

    if(0 >= size || sizeof *gpt > UP_DISK_1SECT(disk))
        return 0;

    if(up_disk_check1sect(disk, start))
        return 0;
    buf = up_disk_getsect(disk, start);
    if(!buf)
        return -1;
    *gpt = buf;

    return 1;
}

static int
gpt_checkcrc(struct up_gpt_p *gpt)
{
    uint32_t save, crc;

    save        = gpt->gptcrc;
    gpt->gptcrc = 0;
    crc         = ~0 ^ up_crc32(gpt, sizeof *gpt, ~0);
    gpt->gptcrc = save;

    return UP_LETOH32(save) == crc;
}

static const uint8_t gpt_data4_apple[] = "\xaa\x11\x00\x30\x65\x43\xec\xac";
static const uint8_t gpt_data4_sun[]   = "\x99\xA6\x08\x00\x20\x73\x66\x31";
static struct
{
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    const uint8_t *data4;
    const char *name;
} gpt_labels[] =
{
    {0, 0, 0, (const uint8_t*)"\0\0\0\0\0\0\0\0", "unused"},
    {0xc12a7328, 0xf81f, 0x11d2, (const uint8_t*)"\xba\x4b\x00\xa0\xc9\x3e\xc9\x3b", "EFI System Partition"},
    {0x48465300, 0, 0x11aa, gpt_data4_apple, "Apple HFS+"},
    {0x55465300, 0, 0x11aa, gpt_data4_apple, "Apple UFS"},
    {0xebd0a0a2, 0xb9e5, 0x4433, (const uint8_t*)"\x87\xc0\x68\xb6\xb7\x26\x99\xc7", "Microsoft Data"},
    {0x6A898CC3, 0x1dd2, 0x11b2, gpt_data4_sun, "Solaris /usr or Apple ZFS"},
};

static const char *
gpt_typename(const struct up_guid_p *guid)
{
    uint32_t data1;
    uint16_t data2, data3;
    int ii;

    data1 = UP_LETOH32(guid->data1);
    data2 = UP_LETOH16(guid->data2);
    data3 = UP_LETOH16(guid->data3);
    for(ii = 0; sizeof(gpt_labels) / sizeof(gpt_labels[0]) > ii; ii++)
    {
        if(data1 == gpt_labels[ii].data1 &&
           data2 == gpt_labels[ii].data2 &&
           data3 == gpt_labels[ii].data3 &&
           0 == memcmp(guid->data4, gpt_labels[ii].data4, GPT_GUID_DATA4_SIZE))
            return gpt_labels[ii].name;
    }

    return NULL;
}
