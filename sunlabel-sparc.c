#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdlabel.h"
#include "disk.h"
#include "map.h"
#include "sunlabel-shared.h"
#include "sunlabel-sparc.h"
#include "util.h"

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

struct up_sparcpart_p
{
    uint32_t                    cyl;
    uint32_t                    size;
} __attribute__((packed));

struct up_sparcvtocpart_p
{
    uint16_t tag;
    uint16_t flag;
} __attribute__((packed));

struct up_sparcvtoc_p
{
    uint32_t                    version;
    char                        name[8];
    uint16_t                    partcount;
    struct up_sparcvtocpart_p   parts[SPARC_MAXPART];
    uint32_t                    bootinfo[3];
    uint32_t                    magic;
    uint32_t                    reserved[10];
    uint32_t                    timestamp[SPARC_MAXPART];
    uint16_t                    writeskip;
    uint16_t                    readskip;
    char                        pad[154];

} __attribute__((packed));

struct up_sparcobsd_p
{
    uint32_t                    checksum;
    uint32_t                    magic;
    struct up_sparcpart_p       extparts[OBSD_EXTRAPART];
    uint8_t                     types[OBSD_MAXPART];
    uint8_t                     fragblock[OBSD_MAXPART];
    uint16_t                    cpg[OBSD_MAXPART];
    char                        pad[156];
} __attribute__((packed));

struct up_sparc_p
{
    char                        label[128];
    union
    {
        struct up_sparcvtoc_p   vtoc;
        struct up_sparcobsd_p   obsd;
    } __attribute__((packed))   ext;
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
} __attribute__((packed));

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

static int sparc_load(struct up_disk *disk, const struct up_part *parent,
                      void **priv, const struct up_opts *opts);
static int sparc_setup(struct up_map *map, const struct up_opts *opts);
static int sparc_info(const struct up_map *map, int verbose,
                      char *buf, int size);
static int sparc_index(const struct up_part *part, char *buf, int size);
static int sparc_extrahdr(const struct up_map *map, int verbose,
                          char *buf, int size);
static int sparc_extra(const struct up_part *part, int verbose,
                       char *buf, int size);
static int sparc_read(struct up_disk *disk, int64_t start, int64_t size,
                      const uint8_t **ret, const struct up_opts *opts);
static unsigned int sparc_check_vtoc(const struct up_sparcvtoc_p *vtoc);
static unsigned int sparc_check_obsd(const struct up_sparcobsd_p *obsd);

void up_sunlabel_sparc_register(void)
{
    up_map_register(UP_MAP_SUN_SPARC,
                    "Sun sparc disklabel",
                    0,
                    sparc_load,
                    sparc_setup,
                    sparc_info,
                    sparc_index,
                    sparc_extrahdr,
                    sparc_extra,
                    NULL,
                    up_map_freeprivmap_def,
                    up_map_freeprivpart_def);
}

static int
sparc_load(struct up_disk *disk, const struct up_part *parent, void **priv,
          const struct up_opts *opts)
{
    int                 res;
    const uint8_t      *buf;
    struct up_sparc    *sparc;

    assert(SPARC_EXT_SIZE == sizeof(struct up_sparcvtoc_p));
    assert(SPARC_EXT_SIZE == sizeof(struct up_sparcobsd_p));
    assert(SPARC_SIZE == sizeof(struct up_sparc_p));

    *priv = NULL;

    if(disk->upd_sectsize < SPARC_SIZE)
        return 0;

    /* read map and check magic */
    res = sparc_read(disk, parent->start, parent->size, &buf, opts);
    if(0 >= res)
        return res;

    /* allocate map struct */
    sparc = calloc(1, sizeof *sparc);
    if(!sparc)
    {
        perror("malloc");
        return -1;
    }
    memcpy(&sparc->packed, buf, sizeof sparc->packed);
    sparc->ext = sparc_check_vtoc(&sparc->packed.ext.vtoc) |
                 sparc_check_obsd(&sparc->packed.ext.obsd);

    *priv = sparc;

    return 1;
}

static int
sparc_setup(struct up_map *map, const struct up_opts *opts)
{
    struct up_sparc            *priv = map->priv;
    struct up_sparc_p          *packed = &priv->packed;
    int                         ii, max, flags;
    struct up_sparcpart        *part;
    int64_t                     cylsize, start, size;

    if(!up_disk_save1sect(map->disk, map->start, map, 0, opts->verbosity))
        return -1;

    cylsize = (uint64_t)UP_BETOH16(packed->heads) *
              (uint64_t)UP_BETOH16(packed->sects);
    max = (SPARC_ISEXT(priv->ext, OBSD) ? OBSD_MAXPART : SPARC_MAXPART);

    for(ii = 0; max > ii; ii++)
    {
        part = calloc(1, sizeof *part);
        if(!part)
        {
            perror("malloc");
            return -1;
        }

        part->part    = (SPARC_MAXPART > ii ? packed->parts[ii] :
                         packed->ext.obsd.extparts[ii - SPARC_MAXPART]);
        part->index   = ii;
        start         = map->start + cylsize * UP_BETOH32(part->part.cyl);
        size          = UP_BETOH32(part->part.size);
        flags         = 0;

        if(!up_map_add(map, start, size, flags, part))
        {
            free(part);
            return -1;
        }
    }

    return 1;
}

static int
sparc_info(const struct up_map *map, int verbose, char *buf, int size)
{
    const struct up_sparc       *priv = map->priv;
    const struct up_sparc_p     *sparc = &priv->packed;
    const struct up_sparcvtoc_p *vtoc;
    int                         res1, res2;
    char                        name[sizeof(vtoc->name)+1];
    const char                  *extstr;

    if(SPARC_ISEXT(priv->ext, VTOC))
        extstr = " (Sun VTOC)";
    else if(SPARC_ISEXT(priv->ext, OBSD_TYPES))
        extstr = " (OpenBSD extensions)";
    else if(SPARC_ISEXT(priv->ext, OBSD))
        extstr = " (OpenBSD partitions)";
    else
        extstr = "";

    if(UP_NOISY(verbose, EXTRA))
    {
        res1 = snprintf(buf, size,
                        "Sun sparc disklabel%s in sector %"PRId64" of %s:\n"
                        "  rpm: %u\n"
                        "  physical cylinders: %u\n"
                        "  alternates/cylinder: %u\n"
                        "  interleave: %u\n"
                        "  data cylinders: %u\n"
                        "  alternate cylinders: %u\n"
                        "  tracks/cylinder: %u\n"
                        "  sectors/track: %u\n",
                        extstr, map->start, map->disk->upd_name,
                        UP_BETOH16(sparc->rpm),
                        UP_BETOH16(sparc->physcyls),
                        UP_BETOH16(sparc->alts),
                        UP_BETOH16(sparc->interleave),
                        UP_BETOH16(sparc->datacyls),
                        UP_BETOH16(sparc->altcyls),
                        UP_BETOH16(sparc->heads),
                        UP_BETOH16(sparc->sects));
        if(0 > res1 || res1 >= size)
            return res1;
        if(SPARC_ISEXT(priv->ext, VTOC))
        {
            vtoc = &sparc->ext.vtoc;
            memcpy(name, vtoc->name, sizeof vtoc->name);
            name[sizeof(name)-1] = 0;
            res2 = snprintf(buf + res1, size - res1,
                            "  name: %s\n"
                            "  partition count: %d\n"
                            "  read sector skip: %d\n"
                            "  write sector skip: %d\n",
                            name,
                            UP_BETOH16(vtoc->partcount),
                            UP_BETOH16(vtoc->readskip),
                            UP_BETOH16(vtoc->writeskip));
        }
        else
            res2 = 0;
        if(0 > res2)
            return res2;
        return res1 + res2;
    }
    else if(UP_NOISY(verbose, NORMAL))
        return snprintf(buf, size,
                        "Sun sparc disklabel%s in sector %"PRId64" of %s:",
                        extstr, map->start, map->disk->upd_name);
    else
        return 0;
}

static int
sparc_index(const struct up_part *part, char *buf, int size)
{
    struct up_sparc            *label = part->map->priv;
    struct up_sparcpart        *priv = part->priv;

    if(SPARC_ISEXT(label->ext, OBSD))
        return snprintf(buf, size, "%c", 'a' + priv->index);
    else
        return snprintf(buf, size, "%d", priv->index);
}

static int
sparc_extrahdr(const struct up_map *map, int verbose, char *buf, int size)
{
    struct up_sparc    *priv = map->priv;
    const char         *hdr;

    if(!UP_NOISY(verbose, NORMAL))
        return 0;

    if(SPARC_ISEXT(priv->ext, VTOC))
        hdr = UP_SUNLABEL_FMT_HDR;
    else if(SPARC_ISEXT(priv->ext, OBSD_TYPES))
        hdr = UP_BSDLABEL_FMT_HDR(verbose);
    else
        hdr = NULL;

    if(hdr)
        return snprintf(buf, size, "%s", hdr);
    else
        return 0;
}

static int
sparc_extra(const struct up_part *part, int verbose, char *buf, int size)
{
    struct up_sparc            *label = part->map->priv;
    struct up_sparcvtoc_p      *vtoc = &label->packed.ext.vtoc;
    struct up_sparcobsd_p      *obsd = &label->packed.ext.obsd;
    struct up_sparcpart        *priv = part->priv;

    if(!UP_NOISY(verbose, NORMAL))
        return 0;

    if(SPARC_ISEXT(label->ext, VTOC) && UP_NOISY(verbose, NORMAL))
        return up_sunlabel_fmt(buf, size,
                               UP_BETOH16(vtoc->parts[priv->index].tag),
                               UP_BETOH16(vtoc->parts[priv->index].flag));
    else if(SPARC_ISEXT(label->ext, OBSD_TYPES))
        return up_bsdlabel_fmt(part, verbose, buf, size,
                               obsd->types[priv->index],
                               0,
                               obsd->fragblock[priv->index],
                               UP_BETOH16(obsd->cpg[priv->index]),
                               1);
    else
        return 0;
}

static int
sparc_read(struct up_disk *disk, int64_t start, int64_t size,
           const uint8_t **ret, const struct up_opts *opts)
{
    const uint8_t      *buf;
    uint16_t            magic, sum, calc;
    const uint16_t     *ptr;

    if(1 >= size)
        return 0;

    if(up_disk_check1sect(disk, start))
        return 0;
    buf = up_disk_getsect(disk, start, opts->verbosity);
    if(!buf)
        return -1;

    memcpy(&magic, buf + SPARC_MAGIC_OFF, sizeof magic);
    memcpy(&sum, buf + SPARC_CHECKSUM_OFF, sizeof sum);

    if(SPARC_MAGIC != UP_BETOH16(magic))
    {
        if(SPARC_MAGIC == UP_LETOH16(magic) &&
           UP_NOISY(opts->verbosity, QUIET))
            /* this is kind of silly but hey, why not? */
            up_err("sun sparc label in sector %"PRId64" with unknown "
                   "byte order: little endian", start);
        return 0;
    }

    assert(0 == SPARC_SIZE % sizeof sum);
    calc = 0;
    ptr = (const uint16_t *)buf;
    while((const uint8_t *)ptr - buf < SPARC_CHECKSUM_OFF)
        calc ^= *(ptr++);

    if(calc != sum)
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_msg((opts->relaxed ? UP_MSG_FWARN : UP_MSG_FERR),
                   "sun sparc label in sector %"PRId64" with bad checksum",
                   start);
        if(!opts->relaxed)
            return 0;
    }

    *ret = buf;
    return 1;
}

static unsigned int
sparc_check_vtoc(const struct up_sparcvtoc_p *vtoc)
{
    if(VTOC_VERSION == UP_BETOH32(vtoc->version) &&
       VTOC_MAGIC   == UP_BETOH32(vtoc->magic))
        return SPARC_EXTFL_VTOC;
    else
        return 0;
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
