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

struct up_guid_p
{
    uint32_t            data1;
    uint16_t            data2;
    uint16_t            data3;
    uint8_t             data4[GPT_GUID_DATA4_SIZE];
} __attribute__((packed));

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
} __attribute__((packed));

struct up_gptpart_p
{
    struct up_guid_p    type;
    struct up_guid_p    guid;
    uint64_t            start;
    uint64_t            end;
    uint64_t            flags;
    char                name[GPT_NAME_SIZE];
} __attribute__((packed));

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

static int gpt_load(struct up_disk *disk, const struct up_part *parent,
                    void **priv, const struct up_opts *opts);
static int gpt_setup(struct up_map *map, const struct up_opts *opts);
static int gpt_getinfo(const struct up_map *part, int verbose,
                       char *buf, int size);
static int gpt_getindex(const struct up_part *part, char *buf, int size);
static int gpt_getextra(const struct up_part *part, int verbose,
                        char *buf, int size);
static int gpt_findhdr(struct up_disk *disk, int64_t start, int64_t size,
                       struct up_gpt_p *gpt);
static int gpt_readhdr(struct up_disk *disk, int64_t start, int64_t size,
                       const struct up_gpt_p **gpt);
static int gpt_checkcrc(struct up_gpt_p *gpt);
static const char *gpt_typename(const struct up_guid_p *guid);

void
up_gpt_register(void)
{
    up_map_register(UP_MAP_GPT,
                    "EFI GPT",
                    0,
                    gpt_load,
                    gpt_setup,
                    gpt_getinfo,
                    gpt_getindex,
                    gpt_getextra,
                    NULL,
                    up_map_freeprivmap_def,
                    up_map_freeprivpart_def);
}

int
gpt_load(struct up_disk *disk, const struct up_part *parent, void **priv,
         const struct up_opts *opts)
{
    struct up_gpt_p pk;
    int             res;
    struct up_gpt  *gpt;

    assert(GPT_SIZE == sizeof pk &&
           GPT_PART_SIZE == sizeof(struct up_gptpart_p));
    *priv = NULL;

    /* try to load either the primary or secondary gpt headers, and
       check the magic and crc */
    res = gpt_findhdr(disk, parent->start, parent->size, &pk);
    if(0 >= res)
        return res;

    /* check revision */
    if(GPT_REVISION != UP_LETOH32(pk.revision))
    {
        fprintf(stderr, "ignoring gpt with unknown revision %u.%u\n",
                (UP_LETOH32(pk.revision) >> 16) & 0xffff,
                UP_LETOH32(pk.revision) & 0xffff);
        return 0;
    }

    /* XXX validate other fields */

    /* create map private struct */
    gpt = calloc(1, sizeof *gpt);
    if(!gpt)
    {
        perror("malloc");
        return -1;
    }
    gpt->gpt = pk;
    *priv = gpt;

    return 1;
}

static int
gpt_setup(struct up_map *map, const struct up_opts *opts)
{
    struct up_gpt              *priv = map->priv;
    struct up_gpt_p            *gpt = &priv->gpt;
    struct up_gptpart          *part;
    const struct up_gptpart_p  *pk;
    uint64_t                    partsects, partbytes;
    const uint8_t              *data1, *data2;

    /* calculate partition buffer size */
    partbytes = UP_LETOH32(gpt->maxpart) * GPT_PART_SIZE;
    partsects = partbytes / map->disk->upd_sectsize;

    /* save sectors from primary and secondary maps */
    data1 = up_disk_savesectrange(map->disk, GPT_PRIOFF(map->start, map->size),
                                  1 + partsects, map, 0);
    data2 = up_disk_savesectrange(map->disk, GPT_SECOFF(map->start, map->size)
                                  - partsects, 1 + partsects, map, 0);
    if(!data1 || !data2)
        return -1;

    /* verify the crc */
    if(UP_LETOH32(gpt->partcrc) !=
       (up_crc32(data1 + map->disk->upd_sectsize,  partbytes, ~0) ^ ~0))
    {
        fprintf(stderr, "bad gpt partition crc\n");
        return 0;
    }

    /* walk through the partition buffer and add all partitions found */
    pk = (const struct up_gptpart_p *)(data1 + map->disk->upd_sectsize);
    while((const uint8_t *)pk + sizeof *pk
          <= data1 + map->disk->upd_sectsize * partsects)
    {
        part = calloc(1, sizeof *part);
        if(!part)
        {
            perror("malloc");
            return -1;
        }
        part->part = *pk;
        part->index = priv->partitions;
        if(!up_map_add(map, UP_LETOH64(pk->start), UP_LETOH64(pk->end)
                       - UP_LETOH64(pk->start), 0, part))
        {
            free(part);
            return -1;
        }
        priv->partitions++;
        pk++;
    }

    return 1;
}

static int
gpt_getinfo(const struct up_map *map, int verbose, char *buf, int size)
{
    const struct up_gpt *gpt = map->priv;

    if(UP_NOISY(verbose, EXTRA))
        return snprintf(buf, size, "EFI GPT partition table in sectors %"PRId64" "
                    "and %"PRId64" of %s:\n"
                    "  size:                 %u\n"
                    "  primary gpt sector:   %"PRIu64"\n"
                    "  backup gpt sector:    %"PRIu64"\n"
                    "  first data sector:    %"PRIu64"\n"
                    "  last data sector:     %"PRIu64"\n"
                    "  guid:                 "GPT_GUID_FMT"\n"
                    "  partition sector:     %"PRIu64"\n"
                    "  max partitions:       %u\n"
                    "  partition size:       %u\n"
                    "\n",
                    GPT_PRIOFF(map->start, map->size),
                    GPT_SECOFF(map->start, map->size), map->disk->upd_name,
                    UP_LETOH32(gpt->gpt.size),
                    UP_LETOH64(gpt->gpt.gpt1sect),
                    UP_LETOH64(gpt->gpt.gpt2sect),
                    UP_LETOH64(gpt->gpt.firstsect),
                    UP_LETOH64(gpt->gpt.lastsect),
                    GPT_GUID_FMT_ARGS(&gpt->gpt.guid),
                    UP_LETOH64(gpt->gpt.partsect),
                    UP_LETOH32(gpt->gpt.maxpart),
                    UP_LETOH32(gpt->gpt.partsize));
    else if(UP_NOISY(verbose, NORMAL))
        return snprintf(buf, size, "EFI GPT partition table in sectors %"PRId64
                        " and %"PRId64" of %s:",
                        GPT_PRIOFF(map->start, map->size),
                        GPT_SECOFF(map->start, map->size), map->disk->upd_name);
    else
        return 0;
}

static int
gpt_getindex(const struct up_part *part, char *buf, int size)
{
    const struct up_gptpart *priv = part->priv;

    return snprintf(buf, size, "%d", priv->index + 1);
}

static int
gpt_getextra(const struct up_part *part, int verbose, char *buf, int size)
{
    const struct up_gptpart    *priv;
    const char                 *label;

    if(!UP_NOISY(verbose, NORMAL))
        return 0;

    if(!part)
    {
        if(UP_NOISY(verbose, EXTRA))
            return snprintf(buf, size, "%-36s Type", "GUID");
        else
            return snprintf(buf, size, "Type");
    }

    priv = part->priv;
    label = gpt_typename(&priv->part.type);

    if(UP_NOISY(verbose, EXTRA))
        return snprintf(buf, size, GPT_GUID_FMT " " GPT_GUID_FMT " %s",
                        GPT_GUID_FMT_ARGS(&priv->part.guid),
                        GPT_GUID_FMT_ARGS(&priv->part.type),
                        (label ? label : ""));
    else
    {
        if(label)
            return snprintf(buf, size, "%s", label);
        else
            return snprintf(buf, size, GPT_GUID_FMT,
                            GPT_GUID_FMT_ARGS(&priv->part.type));
    }
}

static int
gpt_findhdr(struct up_disk *disk, int64_t start, int64_t size,
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
        fprintf(stderr, "bad crc on primary gpt in sector %"PRId64"\n",
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
        fprintf(stderr, "bad crc on secondary gpt in sector %"PRId64"\n",
                GPT_SECOFF(start, size));
        badcrc = 1;
    }

    return (badcrc ? -1 : 0);
}

static int
gpt_readhdr(struct up_disk *disk, int64_t start, int64_t size,
            const struct up_gpt_p **gpt)
{
    const void *buf;

    *gpt = NULL;

    if(0 >= size || sizeof *gpt > disk->upd_sectsize)
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
