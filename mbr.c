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
#define MBR_MAGIC_OFF           (0x1fe)
#define MBR_MAGIC               (0xaa55)
#define MBR_MAP_OFF             (0x1be)
#define MBR_PART_SIZE           (0x10)
#define MBR_PART_COUNT          (4)
#define MBR_FLAG_ACTIVE         (0x80)
#define MBR_EXT_ID              (0x05)
#define MBR_UNUSED_ID           (0x00)
#define MBR_EXT_PART_OFF        (MBR_MAP_OFF)
#define MBR_EXT_NEXT_OFF        (MBR_MAP_OFF + MBR_PART_SIZE)

#define MBR_GETSECT(buf) \
    (((const uint8_t *)(buf))[0] & 0x3f)
#define MBR_GETCYL(buf) \
    (((uint16_t)(((const uint8_t *)(buf))[0] & 0xc0) << 2) | \
      (uint16_t)(((const uint8_t *)(buf))[1]))

struct up_mbr_part
{
    unsigned int        upmp_valid : 1;
    uint8_t             upmp_flags;
    uint8_t             upmp_firsthead;
    uint8_t             upmp_firstsect;
    uint16_t            upmp_firstcyl;
    uint8_t             upmp_type;
    uint8_t             upmp_lasthead;
    uint8_t             upmp_lastsect;
    uint16_t            upmp_lastcyl;
    uint32_t            upmp_start;
    uint32_t            upmp_size;
};

struct up_mbr_ext
{
    int64_t             upme_absoff;
    int64_t             upme_reloff;
    int64_t             upme_size;
    char                upme_buf[MBR_SIZE];
    struct up_mbr_part  upme_part;
    SLIST_ENTRY(up_mbr_ext) upme_next;
};

struct up_mbr
{
    char                upm_buf[MBR_SIZE];
    struct up_mbr_part  upm_parts[MBR_PART_COUNT];
    SLIST_HEAD(up_mbr_chain, up_mbr_ext) upm_ext[MBR_PART_COUNT];
};

static int mbr_loadext(struct up_disk *disk, struct up_mbr_part *part,
                       struct up_mbr_chain *chain);
static int readmbr(struct up_disk *disk, int64_t start, int64_t size,
                   const uint8_t **buf);
static int parsembr(const struct up_disk *disk, struct up_mbr *mbr,
                    int64_t start, int64_t size,
                    const uint8_t *buf, size_t buflen);
static int parsembrext(const struct up_disk *disk, struct up_mbr_ext *ext,
                       int64_t start, int64_t size, const uint8_t *buf,
                       size_t buflen, struct up_mbr_part *next);
static void parsembrpart(const uint8_t *buf, size_t buflen, int64_t start,
                         int64_t size, struct up_mbr_part *part);
static void printpart(FILE *stream, const struct up_disk *disk,
                      const struct up_mbr_part *part, int index, int verbose);

int
up_mbr_test(struct up_disk *disk, int64_t start, int64_t size)
{
    return readmbr(disk, start, size, NULL);
}

void *
up_mbr_load(struct up_disk *disk, int64_t start, int64_t size)
{
    void *              mbr;

    up_mbr_testload(disk, start, size, &mbr);
    return mbr;
}

int
up_mbr_testload(struct up_disk *disk, int64_t start, int64_t size, void **map)
{
    const uint8_t *     buf;
    int                 res, ii;
    struct up_mbr *     mbr;

    /* load MBR */
    *map = NULL;
    if(start)
        return 0;
    res = readmbr(disk, start, size, &buf);
    if(0 >= res)
        return res;

    /* alloc mbr struct */
    mbr = calloc(1, sizeof *mbr);
    if(!mbr)
    {
        perror("malloc");
        return -1;
    }

    /* parse MBR */
    if(0 > parsembr(disk, mbr, start, size, buf, disk->upd_sectsize))
    {
        up_mbr_free(mbr);
        return 0;
    }

    /* handle extended partitions */
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        if(MBR_EXT_ID == mbr->upm_parts[ii].upmp_type &&
           mbr->upm_parts[ii].upmp_valid &&
           0 > mbr_loadext(disk, &mbr->upm_parts[ii], &mbr->upm_ext[ii]))
        {
            up_mbr_free(mbr);
            return -1;
        }
    }

    *map = mbr;
    return 1;
}

static int
mbr_loadext(struct up_disk *disk, struct up_mbr_part *part,
            struct up_mbr_chain *chain)
{
    const uint8_t *     buf;
    int                 res;
    struct up_mbr_ext * ext, * last;
    struct up_mbr_part  next;

    assert(SLIST_EMPTY(chain));
    last  = NULL;
    memset(&next, 0, sizeof next);
    next.upmp_valid = 1;
    next.upmp_type  = MBR_EXT_ID;
    next.upmp_size  = part->upmp_size;

    while(MBR_EXT_ID == next.upmp_type && next.upmp_valid)
    {
        res = readmbr(disk, part->upmp_start + next.upmp_start,
                      next.upmp_size, &buf);
        if(0 >= res)
            return res;

        ext = calloc(1, sizeof *ext);
        if(!ext)
        {
            perror("malloc");
            return -1;
        }

        if(0 > parsembrext(disk, ext, part->upmp_start, part->upmp_size,
                           buf, disk->upd_sectsize, &next))
        {
            free(ext);
            return 0;
        }

        if(NULL == last)
            SLIST_INSERT_HEAD(chain, ext, upme_next);
        else
            SLIST_INSERT_AFTER(last, ext, upme_next);
        last = ext;
    }

    return 1;
}

static int
readmbr(struct up_disk *disk, int64_t start, int64_t size,
        const uint8_t **buf)
{
    const uint8_t *     ret;

    if(buf)
        *buf = NULL;

    if(MBR_SIZE > size * disk->upd_sectsize || MBR_SIZE > disk->upd_sectsize)
        return 0;

    ret = up_disk_getsect(disk, start);
    if(!ret)
        return -1;

    if(MBR_MAGIC != UP_GETBUF16LE(ret + MBR_MAGIC_OFF))
        return 0;

    if(buf)
        *buf = ret;

    return 1;
}

static int
parsembr(const struct up_disk *disk, struct up_mbr *mbr,
         int64_t start, int64_t size, const uint8_t *buf, size_t buflen)
{
    int                 ii;

    assert(0 <= start && 0 < size);
    assert(size * disk->upd_sectsize >= MBR_SIZE);
    assert(MBR_SIZE == buflen);
    assert(MBR_MAGIC == UP_GETBUF16LE(buf + MBR_MAGIC_OFF));

    memcpy(mbr->upm_buf, buf, MBR_SIZE);

    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        assert(MBR_MAP_OFF + (ii * MBR_PART_SIZE) + MBR_PART_SIZE <= MBR_SIZE);
        parsembrpart(buf + MBR_MAP_OFF + (ii * MBR_PART_SIZE), MBR_PART_SIZE,
                     start, size, &mbr->upm_parts[ii]);
    }

    /* XXX should check for partition overlap */

    return 0;
}

static int
parsembrext(const struct up_disk *disk, struct up_mbr_ext *ext, int64_t start,
            int64_t size, const uint8_t *buf, size_t buflen,
            struct up_mbr_part *next)
{
    assert(0 < start && 0 < size);
    assert(size * disk->upd_sectsize >= MBR_SIZE);
    assert(MBR_SIZE == buflen);
    assert(MBR_MAGIC == UP_GETBUF16LE(buf + MBR_MAGIC_OFF));

    /* save extended mbr */
    ext->upme_absoff = start + next->upmp_start;
    ext->upme_reloff = next->upmp_start;
    ext->upme_size   = next->upmp_size;
    memcpy(ext->upme_buf, buf, MBR_SIZE);

    /* read real partition */
    parsembrpart(buf + MBR_EXT_PART_OFF, MBR_PART_SIZE, 0,
                 ext->upme_size, &ext->upme_part);
    ext->upme_part.upmp_start += ext->upme_absoff;

    /* read link to next extended mbr */
    memset(next, 0, sizeof *next);
    parsembrpart(buf + MBR_EXT_NEXT_OFF, MBR_PART_SIZE,
                 ext->upme_reloff, size, next);

    return 0;
}

static void
parsembrpart(const uint8_t *buf, size_t buflen, int64_t start, int64_t size,
             struct up_mbr_part *part)
{
    assert(MBR_PART_SIZE <= buflen);
    if(!buf[4])
        memset(part, 0, sizeof *part);
    else
    {
        /* XXX should fill in lba info from chs if needed */
        part->upmp_flags     = buf[0];
        part->upmp_firsthead = buf[1];
        part->upmp_firstsect = MBR_GETSECT(buf + 2);
        part->upmp_firstcyl  = MBR_GETCYL(buf + 2);
        part->upmp_type      = buf[4];
        part->upmp_lasthead  = buf[5];
        part->upmp_lastsect  = MBR_GETSECT(buf + 6);
        part->upmp_lastcyl   = MBR_GETCYL(buf + 6);
        part->upmp_start     = UP_GETBUF32LE(buf + 8);
        part->upmp_size      = UP_GETBUF32LE(buf + 12);
        part->upmp_valid     = (start < part->upmp_start &&
                                start + size > part->upmp_start &&
                                start + size - part->upmp_start >=
                                part->upmp_size);
    }
}

void
up_mbr_free(void *_mbr)
{
    struct up_mbr *     mbr = _mbr;
    struct up_mbr_ext * ext;
    int                 ii;

    if(!mbr)
        return;

    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        while((ext = SLIST_FIRST(&mbr->upm_ext[ii])))
        {
            SLIST_REMOVE_HEAD(&mbr->upm_ext[ii], upme_next);
            free(ext);
        }
    }

    free(mbr);
}

void
up_mbr_dump(const struct up_disk *disk, const void *_mbr, void *_stream,
            const struct up_opts *opts)
{
    const struct up_mbr *       mbr = _mbr;
    FILE *                      stream = _stream;
    int                         ii, jj;
    struct up_mbr_ext *         ext;

    /* print header */
    printpart(stream, disk, NULL, 0, opts->upo_verbose);

    jj = MBR_PART_COUNT;
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        printpart(stream, disk, &mbr->upm_parts[ii], ii, opts->upo_verbose);
        SLIST_FOREACH(ext, &mbr->upm_ext[ii], upme_next)
        {
            printpart(stream, disk, &ext->upme_part, jj, opts->upo_verbose);
            jj++;
        }
    }

    if(!opts->upo_verbose)
        return;

    fprintf(stream, "\nDump of %s MBR:\n", disk->upd_name);
    up_hexdump(mbr->upm_buf, MBR_SIZE, 0, stream);
    putc('\n', stream);
    jj = MBR_PART_COUNT;
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        SLIST_FOREACH(ext, &mbr->upm_ext[ii], upme_next)
        {
            fprintf(stream, "Dump of %s extended MBR #%d "
                    "at sector %"PRId64" (0x%08"PRIx64"):\n",
                    disk->upd_name, jj, ext->upme_absoff, ext->upme_absoff);
            up_hexdump(ext->upme_buf, MBR_SIZE, ext->upme_absoff, stream);
            putc('\n', stream);
            jj++;
        }
    }
}

static void
printpart(FILE *stream, const struct up_disk *disk,
          const struct up_mbr_part *part, int index, int verbose)
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

    if(MBR_UNUSED_ID == part->upmp_type && !verbose)
        return;

    if(!part->upmp_valid)
        splat = 'X';
    else if(MBR_FLAG_ACTIVE & part->upmp_flags)
        splat = '*';
    else
        splat = ' ';

    if(MBR_PART_COUNT > index)
        fprintf(stream, "%d:  ", index + 1);
    else
        fprintf(stream, " %d: ", index + 1);

    if(verbose)
        fprintf(stream, "%c %4d/%3d/%2d-%4d/%3d/%2d %10d %10d %02x %s\n",
                splat, part->upmp_firstcyl, part->upmp_firsthead,
                part->upmp_firstsect, part->upmp_lastcyl, part->upmp_lasthead,
                part->upmp_lastsect, part->upmp_start, part->upmp_size,
                part->upmp_type, up_mbr_name(part->upmp_type));
    else
        fprintf(stream, "%c %10d %10d %02x %s\n",
                splat, part->upmp_start, part->upmp_size,
                part->upmp_type, up_mbr_name(part->upmp_type));
}
