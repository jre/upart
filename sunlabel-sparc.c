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
#include "sunlabel-shared.h"
#include "sunlabel-sparc.h"
#include "util.h"

#define SPARC_LABEL             "Sun sparc disklabel"
#define SPARC_MAGIC             (0xdabe)
#define SPARC_MAGIC_OFF         (508)
#define SPARC_CHECKSUM_OFF      (510)
#define SPARC_MAXPART           (8)
#define SPARC_SIZE              (512)
#define SPARC_EXT_SIZE          (292)

#define SPARC_EXTFL_VTOC        (0x01)
#define SPARC_EXTFL_OBSD        (0x02)
#define SPARC_EXTFL_OBSD_TYPES  (0x06)
#define SPARC_ISEXT(ext, flag) \
    ((SPARC_EXTFL_##flag & (ext)) == SPARC_EXTFL_##flag)

#define VTOC_VERSION            (1)
#define VTOC_MAGIC              (0x600DDEEE)

#define OBSD_EXTRAPART          (8)
#define OBSD_MAXPART            (SPARC_MAXPART + OBSD_EXTRAPART)
#define OBSD_OLD_MAGIC          (0x199d1fe2 + OBSD_EXTRAPART)
#define OBSD_NEW_MAGIC          (OBSD_OLD_MAGIC + 1)

/* XXX should detect embedded netbsd label (offset 128) */

#pragma pack(1)

struct up_sparcpart_p
{
    uint32_t                    cyl;
    uint32_t                    size;
};

struct up_sparcvtocpart_p
{
    uint16_t tag;
    uint16_t flag;
};

struct up_sparcvtoc_p
{
    uint32_t                    version;
    char                        name[8];
    uint16_t                    partcount;
    struct up_sparcvtocpart_p   parts[SPARC_MAXPART];
    uint16_t                    alignpad;
    uint32_t                    bootinfo[3];
    uint32_t                    magic;
    uint32_t                    reserved[10];
    uint32_t                    timestamp[SPARC_MAXPART];
    uint16_t                    writeskip;
    uint16_t                    readskip;
    char                        pad[152];

};

struct up_sparcobsd_p
{
    uint32_t                    checksum;
    uint32_t                    magic;
    struct up_sparcpart_p       extparts[OBSD_EXTRAPART];
    uint8_t                     types[OBSD_MAXPART];
    uint8_t                     fragblock[OBSD_MAXPART];
    uint16_t                    cpg[OBSD_MAXPART];
    char                        pad[156];
};

struct up_sparc_p
{
    char                        label[128];
    union
    {
        struct up_sparcvtoc_p   vtoc;
        struct up_sparcobsd_p   obsd;
    } ATTR_PACKED               ext;
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
    struct up_sparcpart_p       parts[SPARC_MAXPART];
    uint16_t                    magic;
    uint16_t                    checksum;
};

#pragma pack()

struct up_sparc
{
    struct up_sparc_p           packed;
    unsigned int                ext;
};

struct up_sparcpart
{
    struct up_sparcpart_p       part;
    int                         index;
};

static int	sparc_load(const struct disk *, const struct part *,
    void **priv);
static int	sparc_setup(struct disk *, struct map *);
static int	sparc_info(const struct map *, FILE *);
static int	sparc_index(const struct part *, char *, size_t);
static int	sparc_extrahdr(const struct map *, FILE *);
static int	sparc_extra(const struct part *, FILE *);
static int	sparc_read(const struct disk *, int64_t, int64_t,
    const uint8_t **);
static unsigned int sparc_check_vtoc(const struct up_sparc_p *);
static unsigned int sparc_check_obsd(const struct up_sparcobsd_p *);

void up_sunlabel_sparc_register(void)
{
	struct map_funcs funcs;

	up_map_funcs_init(&funcs);
	funcs.label = SPARC_LABEL;
	funcs.load = sparc_load;
	funcs.setup = sparc_setup;
	funcs.print_header = sparc_info;
	funcs.get_index = sparc_index;
	funcs.print_extrahdr = sparc_extrahdr;
	funcs.print_extra = sparc_extra;

	up_map_register(UP_MAP_SUN_SPARC, &funcs);
}

static int
sparc_load(const struct disk *disk, const struct part *parent, void **priv)
{
	int res;
	const uint8_t *buf;
	struct up_sparc *label;

	assert(SPARC_EXT_SIZE == sizeof(struct up_sparcvtoc_p));
	assert(SPARC_EXT_SIZE == sizeof(struct up_sparcobsd_p));
	assert(SPARC_SIZE == sizeof(struct up_sparc_p));

	*priv = NULL;

	if (UP_DISK_1SECT(disk) < SPARC_SIZE)
		return (0);

	/* read map and check magic */
	if ((res = sparc_read(disk, parent->start, parent->size, &buf)) <= 0)
		return (res);

	/* allocate map struct */
	if ((label = xalloc(1, sizeof *label, XA_ZERO)) == NULL)
		return (-1);
	memcpy(&label->packed, buf, sizeof label->packed);
	label->ext = sparc_check_obsd(&label->packed.ext.obsd);
	if (label->ext == 0)
		label->ext = sparc_check_vtoc(&label->packed);

	*priv = label;

	return (1);
}

static int
sparc_setup(struct disk *disk, struct map *map)
{
	struct up_sparc *priv = map->priv;
	struct up_sparc_p *packed = &priv->packed;
	int64_t cylsize, start, size;
	struct up_sparcpart *part;
	int i, max, flags;

	if (!up_disk_save1sect(disk, map->start, map, 0))
		return (-1);

	cylsize = (uint64_t)UP_BETOH16(packed->heads) *
	    (uint64_t)UP_BETOH16(packed->sects);
	max = (SPARC_ISEXT(priv->ext, OBSD) ? OBSD_MAXPART : SPARC_MAXPART);

	for (i = 0; max > i; i++) {
		if ((part = xalloc(1, sizeof *part, XA_ZERO)) == NULL)
			return (-1);

		part->part = (SPARC_MAXPART > i ? packed->parts[i] :
		    packed->ext.obsd.extparts[i - SPARC_MAXPART]);
		part->index = i;
		start = map->start + cylsize * UP_BETOH32(part->part.cyl);
		size = UP_BETOH32(part->part.size);
		flags = 0;

		if (!up_map_add(map, start, size, flags, part)) {
			free(part);
			return (-1);
		}
	}

	return (1);
}

static int
sparc_info(const struct map *map, FILE *stream)
{
	const struct up_sparc *priv;
	const struct up_sparc_p *label;
	const struct up_sparcvtoc_p *vtoc;
	char name[sizeof(vtoc->name)+1];
	const char *extstr;

	if (!UP_NOISY(NORMAL))
		return (0);

	priv = map->priv;
	label = &priv->packed;

	if (SPARC_ISEXT(priv->ext, VTOC))
		extstr = " (Sun VTOC)";
	else if (SPARC_ISEXT(priv->ext, OBSD_TYPES))
		extstr = " (OpenBSD extensions)";
	else if (SPARC_ISEXT(priv->ext, OBSD))
		extstr = " (OpenBSD partitions)";
	else
		extstr = "";

	if (fprintf(stream, "%s%s at ", up_map_label(map), extstr) < 0 ||
	    printsect_verbose(map->start, stream) < 0 ||
	    fprintf(stream, " of %s:\n", UP_DISK_PATH(map->disk)) < 0)
		return (-1);

	if (!UP_NOISY(EXTRA))
		return (1);

	if (fprintf(stream,
		"  rpm: %u\n"
		"  physical cylinders: %u\n"
		"  alternates/cylinder: %u\n"
		"  interleave: %u\n"
		"  data cylinders: %u\n"
		"  alternate cylinders: %u\n"
		"  tracks/cylinder: %u\n"
		"  sectors/track: %u\n",
		UP_BETOH16(label->rpm),
		UP_BETOH16(label->physcyls),
		UP_BETOH16(label->alts),
		UP_BETOH16(label->interleave),
		UP_BETOH16(label->datacyls),
		UP_BETOH16(label->altcyls),
		UP_BETOH16(label->heads),
		UP_BETOH16(label->sects)) < 0)
		return (-1);

	if (SPARC_ISEXT(priv->ext, VTOC)) {
		vtoc = &label->ext.vtoc;
		memcpy(name, vtoc->name, sizeof vtoc->name);
		name[sizeof(name)-1] = 0;
		if (fprintf(stream,
			"  name: %s\n"
			"  partition count: %d\n"
			"  read sector skip: %d\n"
			"  write sector skip: %d\n",
			name,
			UP_BETOH16(vtoc->partcount),
			UP_BETOH16(vtoc->readskip),
			UP_BETOH16(vtoc->writeskip)) < 0)
			return (-1);
	}

	if (putc('\n', stream) == EOF)
		return (-1);
	return (1);
}

static int
sparc_index(const struct part *part, char *buf, size_t size)
{
	struct up_sparc *label;
	struct up_sparcpart *priv;

	label = part->map->priv;
	priv = part->priv;

	if (SPARC_ISEXT(label->ext, OBSD))
		return (snprintf(buf, size, "%c", 'a' + priv->index));
	else
		return (snprintf(buf, size, "%d", priv->index));
}

static int
sparc_extrahdr(const struct map *map, FILE *stream)
{
	struct up_sparc *priv;
	const char *hdr;

	if (!UP_NOISY(NORMAL))
		return (0);

	priv = map->priv;
	if (SPARC_ISEXT(priv->ext, VTOC))
		hdr = UP_SUNLABEL_FMT_HDR;
	else if (SPARC_ISEXT(priv->ext, OBSD_TYPES))
		hdr = UP_BSDLABEL_FMT_HDR();
	else
		hdr = NULL;

	if (hdr == NULL)
		return (0);
        return (fprintf(stream, " %s", hdr));
}

static int
sparc_extra(const struct part *part, FILE *stream)
{
	struct up_sparc *label;
	struct up_sparcvtoc_p *vtoc;
	struct up_sparcobsd_p *obsd;
	struct up_sparcpart *priv;

	if (!UP_NOISY(NORMAL))
		return (0);

	label = part->map->priv;
	vtoc = &label->packed.ext.vtoc;
	obsd = &label->packed.ext.obsd;
	priv = part->priv;

	if (SPARC_ISEXT(label->ext, VTOC) && UP_NOISY(NORMAL))
		return (up_sunlabel_fmt(stream,
			UP_BETOH16(vtoc->parts[priv->index].tag),
			UP_BETOH16(vtoc->parts[priv->index].flag)));
	else if (SPARC_ISEXT(label->ext, OBSD_TYPES)) {
		uint8_t bf;
		int frags;

		bf = obsd->fragblock[priv->index];
		frags = OBSDLABEL_BF_FRAG(bf);
		return (up_bsdlabel_fmt(part, obsd->types[priv->index],
			(frags ? OBSDLABEL_BF_BSIZE(bf) / frags : 0),
			frags, UP_BETOH16(obsd->cpg[priv->index]), stream));
	}
	else
		return (0);
}

static int
sparc_read(const struct disk *disk, int64_t start, int64_t size,
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

    memcpy(&magic, buf + SPARC_MAGIC_OFF, sizeof magic);
    memcpy(&sum, buf + SPARC_CHECKSUM_OFF, sizeof sum);

    if(SPARC_MAGIC != UP_BETOH16(magic))
    {
        if(SPARC_MAGIC == UP_LETOH16(magic) &&
           UP_NOISY(QUIET))
            /* this is kind of silly but hey, why not? */
            up_err("%s in sector %"PRId64" with unknown "
                   "byte order: little endian", SPARC_LABEL, start);
        return 0;
    }

    assert(0 == SPARC_SIZE % sizeof sum);
    calc = 0;
    ptr = (const uint16_t *)buf;
    while((const uint8_t *)ptr - buf < SPARC_CHECKSUM_OFF)
        calc ^= *(ptr++);

    if(calc != sum)
    {
        if(UP_NOISY(QUIET))
            up_msg((opts->relaxed ? UP_MSG_FWARN : UP_MSG_FERR),
                   "%s in sector %"PRId64" with bad checksum",
                   SPARC_LABEL, start);
        if(!opts->relaxed)
            return 0;
    }

    *ret = buf;
    return 1;
}

static unsigned int
sparc_check_vtoc(const struct up_sparc_p *sparcpart)
{
	const struct up_sparcvtoc_p *vtoc;
	int i;

	vtoc = &sparcpart->ext.vtoc;

	if (UP_BETOH32(vtoc->version) == VTOC_VERSION &&
	    UP_BETOH32(vtoc->magic) == VTOC_MAGIC)
		return (SPARC_EXTFL_VTOC);

	if (vtoc->magic != 0 || vtoc->version != 0 || vtoc->partcount != 0)
		return (0);

	/* what the fuck linux, seriously? */
	for (i = 0; i < SPARC_MAXPART; i++)
		if ((sparcpart->parts[i].cyl != 0 ||
			sparcpart->parts[i].size != 0) &&
		    vtoc->parts[i].tag == 0)
			return (0);
	return (SPARC_EXTFL_VTOC);
}

static unsigned int
sparc_check_obsd(const struct up_sparcobsd_p *obsd)
{
    int                 res;
    const uint32_t     *ptr, *end;
    uint32_t            sum;

    if(OBSD_OLD_MAGIC == UP_BETOH32(obsd->magic))
    {
        res = SPARC_EXTFL_OBSD;
        end = (const uint32_t *)&obsd->types;
    }
    else if(OBSD_NEW_MAGIC == UP_BETOH32(obsd->magic))
    {
        res = SPARC_EXTFL_OBSD_TYPES;
        end = (const uint32_t *)&obsd->pad;
    }
    else
        return 0;

    sum = 0;
    for(ptr = &obsd->magic; ptr < end; ptr++)
        sum += UP_BETOH32(*ptr);

    return (sum == UP_BETOH32(obsd->checksum) ? res : 0);
}
