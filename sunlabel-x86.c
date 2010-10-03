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

#pragma pack(1)

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
};

#pragma pack()

struct up_sunx86
{
    struct up_sunx86_p          packed;
};

struct up_sunx86part
{
    struct up_sunx86part_p      part;
    int                         index;
};

static int	sun_x86_load(const struct disk *, const struct part *,
    void **priv);
static int	sun_x86_setup(struct disk *, struct map *);
static int	sun_x86_info(const struct map *, FILE *);
static int	sun_x86_index(const struct part *, char *, size_t);
static int	sun_x86_extrahdr(const struct map *, FILE *);
static int	sun_x86_extra(const struct part *, FILE *);
static int	sun_x86_read(const struct disk *, int64_t, int64_t,
    const uint8_t **);

void up_sunlabel_x86_register(void)
{
	struct map_funcs funcs;

	up_map_funcs_init(&funcs);
	funcs.label = SUNX86_LABEL;
	funcs.load = sun_x86_load;
	funcs.setup = sun_x86_setup;
	funcs.print_header = sun_x86_info;
	funcs.get_index = sun_x86_index;
	funcs.print_extrahdr = sun_x86_extrahdr;
	funcs.print_extra = sun_x86_extra;

	up_map_register(UP_MAP_SUN_X86, &funcs);
}

static int
sun_x86_load(const struct disk *disk, const struct part *parent, void **priv)
{
	struct up_sunx86 *label;
	const uint8_t *buf;
	int res;

	assert(SUNX86_SIZE == sizeof(struct up_sunx86_p));
	*priv = NULL;

	if (UP_DISK_1SECT(disk) < SUNX86_SIZE)
		return (0);

	/* read map and check magic */
	if ((res = sun_x86_read(disk, parent->start, parent->size, &buf)) <= 0)
		return (res);

	/* allocate map struct */
	if ((label = xalloc(1, sizeof *label, XA_ZERO)) == NULL)
		return (-1);
	memcpy(&label->packed, buf, sizeof label->packed);

	*priv = label;

	return (1);
}

static int
sun_x86_setup(struct disk *disk, struct map *map)
{
	struct up_sunx86 *priv = map->priv;
	struct up_sunx86_p *packed = &priv->packed;
	struct up_sunx86part *part;
	int64_t start, size;
	int i, max, flags;

	if (!up_disk_save1sect(disk, map->start + SUNX86_OFF, map, 0))
		return (-1);

	max = UP_LETOH16(packed->partcount);
	/* this probably isn't worth checking for */
	if (SUNX86_MAXPARTITIONS < max) {
		if (UP_NOISY(QUIET))
			up_warn("clamping partition count in %s from %d "
			    "down to %d", up_map_label(map), max,
			    SUNX86_MAXPARTITIONS);
		max = SUNX86_MAXPARTITIONS;
	}

	for (i = 0; i < max; i++) {
		if ((part = xalloc(1, sizeof *part, XA_ZERO)) == NULL)
			return (-1);

		memcpy(&part->part, &packed->parts[i], sizeof part->part);
		part->index = i;
		start = map->start + UP_LETOH32(part->part.start);
		size = UP_LETOH32(part->part.size);
		flags = 0;

		if (!up_map_add(map, start, size, flags, part)) {
			free(part);
			return (-1);
		}
	}

	return (1);
}

static int
sun_x86_info(const struct map *map, FILE *stream)
{
	const struct up_sunx86 *priv;
	const struct up_sunx86_p *packed;
	char name[sizeof(packed->name)+1];

	if (!UP_NOISY(NORMAL))
		return (0);

	priv = map->priv;
	packed = &priv->packed;

	if (fprintf(stream, "%s at ", up_map_label(map)) < 0 ||
	    printsect_verbose(map->start, stream) < 0 ||
	    fprintf(stream, " (offset %d) of %s:\n",
		SUNX86_OFF, UP_DISK_PATH(map->disk)) < 0)
		return (-1);

	if (!UP_NOISY(EXTRA))
		return (1);

        memcpy(name, packed->name, sizeof(packed->name));
        name[sizeof(name)-1] = 0;
	if (fprintf(stream,
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
		"  read sectskip: %u\n\n",
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
		UP_LETOH16(packed->readskip)) < 0)
		return (-1);
	return (1);
}

static int
sun_x86_index(const struct part *part, char *buf, size_t size)
{
	struct up_sunx86part *priv;

	priv = part->priv;
	return (snprintf(buf, size, "%d", priv->index));
}

static int
sun_x86_extrahdr(const struct map *map, FILE *stream)
{
	if (UP_NOISY(NORMAL))
		return (fprintf(stream, " %s", UP_SUNLABEL_FMT_HDR));
	else
		return (0);
}

static int
sun_x86_extra(const struct part *part, FILE *stream)
{
	const struct up_sunx86part *priv;

	priv = part->priv;

	if (UP_NOISY(NORMAL))
		return (up_sunlabel_fmt(stream,
			UP_LETOH16(priv->part.type),
			UP_LETOH16(priv->part.flags)));
	else
		return (0);
}

static int
sun_x86_read(const struct disk *disk, int64_t start, int64_t size,
    const uint8_t **ret)
{
    const uint8_t      *buf;
    uint32_t            magic1, vers;
    uint16_t            magic2, sum, calc;
    const uint16_t     *ptr;

    if(SUNX86_OFF >= size)
        return 0;

    if(up_disk_check1sect(disk, start + SUNX86_OFF))
        return 0;
    buf = up_disk_getsect(disk, start + SUNX86_OFF);
    if(!buf)
        return -1;

    memcpy(&magic1, buf + SUNX86_MAGIC1_OFF, sizeof magic1);
    memcpy(&vers, buf + SUNX86_VERSION_OFF, sizeof vers);
    memcpy(&magic2, buf + SUNX86_MAGIC2_OFF, sizeof magic2);
    memcpy(&sum, buf + SUNX86_CHECKSUM_OFF, sizeof sum);

    if(SUNX86_MAGIC1 != UP_LETOH32(magic1))
    {
        if(SUNX86_MAGIC1 == UP_BETOH32(magic1) &&
           UP_NOISY(QUIET))
            up_err("%s in sector %"PRId64" (offset %d) "
                   "with unknown byte order: big endian",
                   SUNX86_LABEL, start, SUNX86_OFF);
        return 0;
    }

    if(SUNX86_VERSION != UP_LETOH32(vers))
    {
        if(UP_NOISY(QUIET))
            up_err("%s in sector %"PRId64" (offset %d) "
                   "with unknown version: %u",
                   SUNX86_LABEL, start, SUNX86_OFF, UP_LETOH32(vers));
        return 0;
    }

    if(SUNX86_MAGIC2 != UP_LETOH16(magic2))
    {
        if(UP_NOISY(QUIET))
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
        if(UP_NOISY(QUIET))
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
