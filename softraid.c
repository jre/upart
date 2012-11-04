#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "softraid.h"
#include "disk.h"
#include "map.h"
#include "md5.h"
#include "util.h"

#define SR_LABEL	"OpenBSD software RAID"
#define SR_OFFSET	(16)
#define SR_MAGIC	(0x4d4152436372616dLLU)
/* XXX support v3 and 4 */
#define SR_VERSION	(5)

#define SR_LEVEL_CRYPTO	('C')
#define SR_LEVEL_HOT	(0xffffffff)
#define SR_LEVEL_KEY	(0xfffffffe)

#pragma pack(1)

struct up_sr_hdr_p
{
	uint64_t	magic;		/* magic number */
	uint32_t	vers;		/* metadata version */
	uint32_t	vol_flags;	/* volume flags */
	uint8_t		uuid[16];	/* uuid */
	uint32_t	chunk_count;	/* number of chunks */
	uint32_t	chunk_id;	/* chunk id */
	uint32_t	opt_num;	/* number of optional MD elements */
	uint32_t	pad;
	uint32_t	volid;		/* volume id */
	uint32_t	raidlvl;	/* raid level */
	int64_t		size;		/* volume size */
	char		vendor[8];	/* volume vendor */
	char		product[16];	/* volume product */
	char		revision[4];	/* volume revision */
	uint32_t	strip_size;	/* strip size */
	uint8_t		checksum[16];   /* checksum of above data */
	char		devname[32];	/* device name */
	uint32_t	flags;		/* metadata flags */
	uint32_t	data_off;	/* data offset */
	uint64_t	ondisk;		/* on disk version counter ? */
	int64_t		rebuild;	/* last block of rebuild */
};

struct up_sr_chunk_p
{
	uint32_t	volid;		/* volume id */
	uint32_t	chunk_id;	/* chunk id */
	char		devname[32];	/* device name */
	int64_t		size;		/* size */
	int64_t		coerced_size;	/* coerced size? */
	uint8_t		uuid[16];	/* uuid */
	uint8_t		checksum[16];   /* checksum of above data */
	uint32_t	status;		/* disk status? */
};

#pragma pack()

struct up_sr
{
	struct up_sr_hdr_p	meta;
	int64_t		metasect;
	int		endian;
};

static int	sr_load(const struct disk *, const struct part *, void **);
static int	sr_setup(struct disk *, struct map *);
static int	sr_info(const struct map *, FILE *);
static int	sr_extrahdr(const struct map *, FILE *);
static int	sr_extra(const struct part *, FILE *);
static int	sr_readmeta(const struct disk *, int64_t, int64_t,
    const uint8_t **, int *);
static int	sr_checksum(const void *, const void *, const uint8_t *md5);

void
up_softraid_register(void)
{
	struct map_funcs funcs;

	up_map_funcs_init(&funcs);
	funcs.label = SR_LABEL;
	funcs.load = sr_load;
	funcs.setup = sr_setup;
	funcs.print_header = sr_info;
	funcs.print_extrahdr = sr_extrahdr;
	funcs.print_extra = sr_extra;

	up_map_register(UP_MAP_SOFTRAID, &funcs);
}

static int
sr_load(const struct disk *disk, const struct part *parent, void **privret)
{
	struct up_sr *priv;
	const uint8_t *buf;
	int ret, endian;

	*privret = NULL;

	if (SR_OFFSET > parent->size)
		return (0);
	if ((ret = sr_readmeta(disk, UP_PART_PHYSADDR(parent) + SR_OFFSET,
		    parent->size - SR_OFFSET, &buf, &endian)) <= 0)
		return (ret);

	if ((priv = xalloc(1, sizeof(*priv), XA_ZERO)) == NULL)
		return (-1);

	memcpy(&priv->meta, buf, sizeof(priv->meta));
	priv->metasect = UP_PART_VIRTADDR(parent) + SR_OFFSET;
	priv->endian = endian;

	*privret = priv;

	return (1);
}

static int
sr_setup(struct disk *disk, struct map *map)
{
	struct up_sr *priv;
	int flags;

	priv = map->priv;

	/* XXX checksum and save optional metadata */

	if (up_disk_save1sect(disk, UP_MAP_VIRT_TO_PHYS(map, priv->metasect),
		    map, 0) == NULL)
		return (-1);

	/* verify the checksum */
	if (!sr_checksum(&priv->meta, &priv->meta.checksum,
		    priv->meta.checksum)) {
		if (UP_NOISY(QUIET))
			up_msg((opts->relaxed ? UP_MSG_FWARN : UP_MSG_FERR),
			    "bad softraid metadata checksum");
		if (!opts->relaxed)
			return (0);
	}

	flags = UP_PART_VIRTDISK;
	if (UP_ETOH32(priv->meta.raidlvl, priv->endian) != 1)
		flags |= UP_PART_UNREADABLE;
	if (!up_map_add(map, UP_MAP_VIRTADDR(map) +
		UP_ETOH32(priv->meta.data_off, priv->endian),
		UP_ETOH64(priv->meta.size, priv->endian), flags, NULL))
		return (-1);

	return (1);
}

static int
sr_info(const struct map *map, FILE *stream)
{
	static const char hex[] = "0123456789abcdef";
	struct up_sr *priv;
	char buf[33];
	int i;

	if (!UP_NOISY(NORMAL))
		return (0);

	priv = map->priv;

	if (fprintf(stream, "%s at ", up_map_label(map)) < 0 ||
	    printsect_verbose(UP_MAP_VIRTADDR(map) + SR_OFFSET, stream) < 0 ||
	    fprintf(stream, " of %s:\n", UP_DISK_PATH(map->disk)) < 0)
		return (-1);

	if (!UP_NOISY(EXTRA))
		return (1);

	assert(sizeof(buf) > sizeof(priv->meta.vendor));
	memset(buf, '\0', sizeof(buf));
	memcpy(buf, priv->meta.vendor, sizeof(priv->meta.vendor));
	if (fprintf(stream, "  vendor: %s\n", buf) < 0)
		return (-1);

	assert(sizeof(buf) > sizeof(priv->meta.product));
	memset(buf, '\0', sizeof(buf));
	memcpy(buf, priv->meta.product, sizeof(priv->meta.product));
	if (fprintf(stream, "  product: %s\n", buf) < 0)
		return (-1);

	assert(sizeof(buf) > sizeof(priv->meta.revision));
	memset(buf, '\0', sizeof(buf));
	memcpy(buf, priv->meta.revision, sizeof(priv->meta.revision));
	if (fprintf(stream, "  revision: %s\n", buf) < 0)
		return (-1);

	assert(sizeof(buf) > sizeof(priv->meta.uuid) * 2);
	for (i = 0; i < sizeof(priv->meta.uuid); i++) {
		buf[i*2] = hex[(priv->meta.uuid[i] >> 4) & 0xf];
		buf[(i*2)+1] = hex[priv->meta.uuid[i] & 0xf];
	}
	buf[i*2] = '\0';
	if (fprintf(stream, "  uuid: %s\n", buf) < 0)
		return (-1);

	if (fprintf(stream,
		"  chunk count: %u\n"
		"  chunk id: 0x%x\n"
		"  optional MD elements: %u\n"
		"  volume id: 0x%x\n"
		"  raid level: %u\n"
		"  size: %"PRId64"\n"
		"  strip size: %u\n"
		"  data offset: %u\n"
		"  on disk version counter: %"PRIu64"\n"
		"  last rebuild block: %"PRIu64"\n",
		UP_ETOH32(priv->meta.chunk_count, priv->endian),
		UP_ETOH32(priv->meta.chunk_id, priv->endian),
		UP_ETOH32(priv->meta.opt_num, priv->endian),
		UP_ETOH32(priv->meta.volid, priv->endian),
		UP_ETOH32(priv->meta.raidlvl, priv->endian),
		UP_ETOH64(priv->meta.size, priv->endian),
		UP_ETOH32(priv->meta.strip_size, priv->endian),
		UP_ETOH32(priv->meta.data_off, priv->endian),
		UP_ETOH64(priv->meta.ondisk, priv->endian),
		UP_ETOH64(priv->meta.rebuild, priv->endian)) < 0)
		return (-1);

	return (1);
}

static int
sr_extrahdr(const struct map *map, FILE *stream)
{
	if (!UP_NOISY(NORMAL))
		return (0);

	return (fprintf(stream, " Level"));
}

static int
sr_extra(const struct part *part, FILE *stream)
{
	struct up_sr *priv;
	uint32_t level;

	if (!UP_NOISY(NORMAL))
		return (0);

	priv = part->map->priv;

	level = UP_ETOH32(priv->meta.raidlvl, priv->endian);
	switch (level) {
	case SR_LEVEL_CRYPTO:
		return (fprintf(stream, " Crypto"));
	case SR_LEVEL_HOT:
		return (fprintf(stream, " Hot Spare"));
	case SR_LEVEL_KEY:
		return (fprintf(stream, " Key Disk"));
	default:
		return (fprintf(stream, " RAID-%u", level));
	}
}

static int
sr_readmeta(const struct disk *disk, int64_t start, int64_t size,
    const uint8_t **bufret, int *endret)
{
	const struct up_sr_hdr_p *meta;
	int endian;

	if (up_disk_check1sect(disk, start))
		return (0);
        if (!(meta = up_disk_getsect(disk, start)))
		return (-1);

	if (UP_LETOH64(meta->magic) == SR_MAGIC)
		endian = UP_ENDIAN_LITTLE;
	else if (UP_BETOH64(meta->magic) == SR_MAGIC)
		endian = UP_ENDIAN_BIG;
	else
		return (0);

	if (UP_ETOH32(meta->vers, endian) != SR_VERSION) {
		if (UP_NOISY(QUIET))
			up_err("ignoring version %d %s in sector %"PRId64,
			    UP_ETOH32(meta->vers, endian), SR_LABEL, start);
		return (0);
	}

	*bufret = (const uint8_t *)meta;
	*endret = endian;

	return (1);
}

static int
sr_checksum(const void *start, const void *end, const uint8_t *md5)
{
	MD5_CTX ctx;
	uint8_t sum[MD5_DIGEST_LENGTH];

	MD5Init(&ctx);
	MD5Update(&ctx, start, (char *)end - (char *)start);
	MD5Final(sum, &ctx);

	return (!memcmp(sum, md5, MD5_DIGEST_LENGTH));
}
