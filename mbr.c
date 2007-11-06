#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdqueue.h"
#include "disk.h"
#include "mbr.h"
#include "util.h"

#define MBR_SIZE                (0x200)
#define MBR_MAGIC               (0xaa55)
#define MBR_PART_COUNT          (4)
#define MBR_FLAG_ACTIVE         (0x80)
#define MBR_ID_EXT              (0x05)
#define MBR_ID_UNUSED           (0x00)
#define MBR_EXTPART             (0)
#define MBR_EXTNEXT             (1)

#define MBR_GETSECT(sc)         ((sc)[0] & 0x3f)
#define MBR_GETCYL(sc)          ((((sc)[0] & 0xc0) << 2) | (sc)[1])

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
} __attribute__((packed));

struct up_mbr_p
{
    uint8_t             bootcode[446];
    struct up_mbrpart_p part[MBR_PART_COUNT];
    uint16_t            magic;
} __attribute__((packed));

struct up_mbrext
{
    int64_t             absoff;
    int64_t             reloff;
    int64_t             max;
    struct up_mbrpart_p part;
    int                 valid;
    struct up_mbr_p     mbr;
    SLIST_ENTRY(up_mbrext) next;
};

struct up_mbr
{
    int64_t             size;
    struct up_mbr_p     mbr;
    int                 valid[MBR_PART_COUNT];
    SLIST_HEAD(up_mbrchain, up_mbrext) ext[MBR_PART_COUNT];
};

static int mbr_loadext(struct up_disk *disk, struct up_mbrpart_p *part,
                       struct up_mbrchain *chain);
static int readmbr(struct up_disk *disk, int64_t start, int64_t size,
                   const struct up_mbr_p **mbr);
static int checkpart(struct up_mbrpart_p *part, int64_t start, int64_t size);
static void printpart(FILE *stream, const struct up_disk *disk,
                      const struct up_mbrpart_p *part, int valid,
                      int index, int verbose);

int
up_mbr_test(struct up_disk *disk, int64_t start, int64_t size)
{
    return readmbr(disk, start, size, NULL);
}

void *
up_mbr_load(struct up_disk *disk, int64_t start, int64_t size)
{
    void *mbr;

    up_mbr_testload(disk, start, size, &mbr);
    return mbr;
}

int
up_mbr_testload(struct up_disk *disk, int64_t start, int64_t size, void **map)
{
    const struct up_mbr_p      *buf;
    int                         res, ii;
    struct up_mbr              *mbr;

    assert(MBR_SIZE == sizeof *buf);

    /* load MBR */
    *map = NULL;
    if(start)
        return 0;
    res = readmbr(disk, start, size, &buf);
    if(0 >= res)
        return res;

    /* alloc and fill mbr struct */
    mbr = calloc(1, sizeof *mbr);
    if(!mbr)
    {
        perror("malloc");
        return -1;
    }
    assert(sizeof mbr->mbr == sizeof *buf);
    memcpy(&mbr->mbr, buf, sizeof *buf);
    mbr->size = size;

    /* check primary partitions and read extended partitions */
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        /* XXX should check for overlap */
        mbr->valid[ii] = checkpart(&mbr->mbr.part[ii], start, size);
        if(mbr->valid[ii] && MBR_ID_EXT == mbr->mbr.part[ii].type)
        {
            if(0 > mbr_loadext(disk, &mbr->mbr.part[ii], &mbr->ext[ii]))
            {
                up_mbr_free(mbr);
                return -1;
            }
        }
    }

    *map = mbr;
    return 1;
}

static int
mbr_loadext(struct up_disk *disk, struct up_mbrpart_p *part,
            struct up_mbrchain *chain)
{
    const struct up_mbr_p      *buf;
    int                         res;
    struct up_mbrext           *ext, *last;
    struct up_mbrpart_p         next;

    assert(SLIST_EMPTY(chain));
    last = NULL;
    memset(&next, 0, sizeof next);
    next.type = MBR_ID_EXT;
    next.size = part->size;

    while(MBR_ID_EXT == next.type)
    {
        /* load extended mbr */
        assert(part->size >  next.start &&
               part->size >= next.size &&
               part->size -  next.start >= next.size);
        res = readmbr(disk, part->start + next.start, next.size, &buf);
        if(0 >= res)
            return res;

        /* alloc and fill mbtext struct */
        ext = calloc(1, sizeof *ext);
        if(!ext)
        {
            perror("malloc");
            return -1;
        }
        assert(sizeof ext->mbr == sizeof *buf);
        memcpy(&ext->mbr, buf, sizeof *buf);
        ext->absoff      = part->start + next.start;
        ext->reloff      = next.start;
        ext->max         = next.size;
        ext->valid       = checkpart(&ext->mbr.part[MBR_EXTPART], 0,next.size);
        ext->part        = ext->mbr.part[MBR_EXTPART];
        ext->part.start += ext->absoff;

        /* append the partition to the chain */
        if(NULL == last)
            SLIST_INSERT_HEAD(chain, ext, next);
        else
            SLIST_INSERT_AFTER(last, ext, next);
        last = ext;

        /* check if link to next mbr is valid */
        next = ext->mbr.part[MBR_EXTNEXT];
        if(!checkpart(&next, ext->reloff, part->size))
            break;
    }

    return 1;
}

static int
readmbr(struct up_disk *disk, int64_t start, int64_t size,
        const struct up_mbr_p **mbr)
{
    const void *buf;

    *mbr = NULL;

    if(0 >= size || sizeof *mbr > disk->upd_sectsize)
        return 0;

    buf = up_disk_getsect(disk, start);
    if(!buf)
        return -1;
    *mbr = buf;

    if(MBR_MAGIC != UP_LETOH16((*mbr)->magic))
        return 0;

    return 1;
}

static int
checkpart(struct up_mbrpart_p *part, int64_t start, int64_t size)
{
    part->start = UP_LETOH32(part->start);
    part->size  = UP_LETOH32(part->size);

    if(MBR_ID_UNUSED == part->type)
        return 0;

    /* XXX should fill in lba info from chs if needed */
    return (start < part->start &&
            start + size > part->start &&
            start + size - part->start >= part->size);
}

void
up_mbr_free(void *_mbr)
{
    struct up_mbr      *mbr = _mbr;
    struct up_mbrext   *ext;
    int                 ii;

    if(!mbr)
        return;

    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        while((ext = SLIST_FIRST(&mbr->ext[ii])))
        {
            SLIST_REMOVE_HEAD(&mbr->ext[ii], next);
            free(ext);
        }
    }

    free(mbr);
}

void
up_mbr_dump(const struct up_disk *disk, const void *_mbr, void *_stream,
            const struct up_opts *opts)
{
    const struct up_mbr        *mbr = _mbr;
    FILE                       *stream = _stream;
    int                         ii, jj;
    struct up_mbrext           *ext;

    /* print header */
    printpart(stream, disk, NULL, 0, 0, opts->upo_verbose);

    jj = MBR_PART_COUNT;
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        printpart(stream, disk, &mbr->mbr.part[ii], mbr->valid[ii],
                  ii, opts->upo_verbose);
        SLIST_FOREACH(ext, &mbr->ext[ii], next)
        {
            printpart(stream, disk, &ext->part, ext->valid,
                      jj, opts->upo_verbose);
            jj++;
        }
    }

    if(!opts->upo_verbose)
        return;

    fprintf(stream, "\nDump of %s MBR:\n", disk->upd_name);
    up_hexdump(&mbr->mbr, sizeof mbr->mbr, 0, stream);
    putc('\n', stream);
    jj = MBR_PART_COUNT;
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        SLIST_FOREACH(ext, &mbr->ext[ii], next)
        {
            fprintf(stream, "Dump of %s extended MBR #%d "
                    "at sector %"PRId64" (0x%08"PRIx64"):\n",
                    disk->upd_name, jj, ext->absoff, ext->absoff);
            up_hexdump(&ext->mbr, sizeof ext->mbr, ext->absoff, stream);
            putc('\n', stream);
            jj++;
        }
    }
}

static void
printpart(FILE *stream, const struct up_disk *disk,
          const struct up_mbrpart_p *part, int valid, int index, int verbose)
{
    char                splat;

    if(!part)
    {
        if(verbose)
            fprintf(stream, "MBR partition table for %s:\n"
                    "         C   H  S    C   H  S      Start       Size ID\n",
                    disk->upd_name);
        else
            fprintf(stream, "MBR partition table for %s:\n"
                    "           Start       Size ID\n",
                    disk->upd_name);
        return;
    }

    if(MBR_ID_UNUSED == part->type && !verbose)
        return;

    if(!valid)
        splat = 'X';
    else if(MBR_FLAG_ACTIVE & part->flags)
        splat = '*';
    else
        splat = ' ';

    if(MBR_PART_COUNT > index)
        fprintf(stream, "%d:  ", index + 1);
    else
        fprintf(stream, " %d: ", index + 1);

    if(verbose)
        fprintf(stream, "%c %4d/%3d/%2d-%4d/%3d/%2d %10d %10d %02x %s\n",
                splat, MBR_GETCYL(part->firstsectcyl), part->firsthead,
                MBR_GETSECT(part->firstsectcyl), MBR_GETCYL(part->lastsectcyl),
                part->lasthead, MBR_GETSECT(part->lastsectcyl), part->start,
                part->size, part->type, up_mbr_name(part->type));
    else
        fprintf(stream, "%c %10d %10d %02x %s\n",
                splat, part->start, part->size,
                part->type, up_mbr_name(part->type));
}

int
up_mbr_iter(struct up_disk *disk, const void *_mbr,
            int (*func)(struct up_disk*, int64_t, int64_t, const char*, void*),
            void *arg)
{
    const struct up_mbr        *mbr = _mbr;
    int                         ii, jj, res, max;
    struct up_mbrext           *ext;
    const struct up_mbrpart_p  *part;
    char                        label[32];

    max = 0;
    jj  = MBR_PART_COUNT;
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        part = &mbr->mbr.part[ii];
        if(mbr->valid[ii])
        {
            snprintf(label, sizeof label, "MBR partition %d", ii + 1);
            res = func(disk, part->start, part->size, label, arg);
            if(0 > res)
                return res;
            if(res > max)
                max = res;
        }
        SLIST_FOREACH(ext, &mbr->ext[ii], next)
        {
            part = &ext->part;
            if(ext->valid)
            {
                snprintf(label, sizeof label, "MBR partition %d", jj + 1);
                res = func(disk, part->start, part->size, label, arg);
                if(0 > res)
                    return res;
                if(res > max)
                    max = res;
                jj++;
            }
        }
    }

    return max;
}
