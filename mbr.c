#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int mbr_loadext(const struct up_disk *disk, uint8_t *buf, size_t buflen,
                       struct up_mbr_part *part, struct up_mbr_chain *chain);
static int readmbr(const struct up_disk *disk, int64_t start, int64_t size,
                   uint8_t *buf, size_t buflen);
static int parsembr(const struct up_disk *disk, struct up_mbr *mbr,
                    int64_t start, int64_t size,
                    const uint8_t *buf, size_t buflen);
static int parsembrext(const struct up_disk *disk, struct up_mbr_ext *ext,
                       int64_t start, int64_t size, const uint8_t *buf,
                       size_t buflen, struct up_mbr_part *next);
static void parsembrpart(const uint8_t *buf, size_t buflen, int64_t start,
                         int64_t size, struct up_mbr_part *part);

int
up_mbr_test(const struct up_disk *disk, int64_t start, int64_t size)
{
    uint8_t             buf[MBR_SIZE];

    return readmbr(disk, start, size, buf, sizeof buf);
}

void *
up_mbr_load(const struct up_disk *disk, int64_t start, int64_t size)
{
    void *              mbr;

    up_mbr_testload(disk, start, size, &mbr);
    return mbr;
}

int
up_mbr_testload(const struct up_disk *disk, int64_t start, int64_t size,
                void **map)
{
    uint8_t             buf[MBR_SIZE];
    int                 res, ii;
    struct up_mbr *     mbr;

    /* load MBR */
    *map = NULL;
    if(start)
        return 0;
    res = readmbr(disk, start, size, buf, sizeof buf);
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
    if(0 > parsembr(disk, mbr, start, size, buf, sizeof buf))
    {
        free(mbr);
        return 0;
    }

    /* handle extended partitions */
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        if(MBR_EXT_ID == mbr->upm_parts[ii].upmp_type &&
           mbr->upm_parts[ii].upmp_valid &&
           0 > mbr_loadext(disk, buf, sizeof buf, &mbr->upm_parts[ii],
                           &mbr->upm_ext[ii]))
        {
            up_mbr_free(mbr);
            return -1;
        }
    }

    *map = mbr;
    return 1;
}

static int
mbr_loadext(const struct up_disk *disk, uint8_t *buf, size_t buflen,
            struct up_mbr_part *part, struct up_mbr_chain *chain)
{
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
                      next.upmp_size, buf, buflen);
        if(0 >= res)
            return res;

        ext = calloc(1, sizeof *ext);
        if(!ext)
        {
            perror("malloc");
            return -1;
        }

        if(0 > parsembrext(disk, ext, part->upmp_start, part->upmp_size,
                           buf, buflen, &next))
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
readmbr(const struct up_disk *disk, int64_t start, int64_t size,
        uint8_t *buf, size_t buflen)
{
    ssize_t res;

    if(MBR_SIZE > size * disk->upd_sectsize || MBR_SIZE > buflen)
        return 0;

    res = pread(disk->upd_fd, buf, MBR_SIZE, start * disk->upd_sectsize);
    if(0 > res)
    {
        fprintf(stderr, "failed to read MBR at sector %"PRIu64" on %s: %s\n",
                start, disk->upd_path, strerror(errno));
        return -1;
    }
    if(MBR_SIZE > res)
    {
        fprintf(stderr, "failed to read MBR at sector %"PRIu64" on %s: "
                "short read count", start, disk->upd_path);
        return -1;
    }

    if(MBR_MAGIC != UP_GETBUF16LE(buf + MBR_MAGIC_OFF))
        return 0;

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
up_mbr_free(void *mbr)
{
    /* XXX free ext */
    if(mbr)
        free(mbr);
}

void
up_mbr_dump(const void *_mbr, void *_stream, const struct up_opts *opts)
{
    const struct up_mbr *       mbr = _mbr;
    FILE *                      stream = _stream;
    int                         ii, jj;
    const struct up_mbr_part *  part;
    struct up_mbr_ext *         ext;
    char                        splat;

    fprintf(stream, "MBR:\n"
            "         C   H  S    C   H  S      Start       Size ID Name\n");
    jj = MBR_PART_COUNT;
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        part = &mbr->upm_parts[ii];
        if(!part->upmp_valid)
            splat = 'X';
        else if(MBR_FLAG_ACTIVE & part->upmp_flags)
            splat = '*';
        else
            splat = ' ';
        /* XXX need to print something if partition is marked invalid */
        fprintf(stream, "%d:  %c %4d/%3d/%2d-%4d/%3d/%2d %10d+%10d %02x %s\n",
                ii, splat, part->upmp_firstcyl, part->upmp_firsthead,
                part->upmp_firstsect, part->upmp_lastcyl, part->upmp_lasthead,
                part->upmp_lastsect, part->upmp_start, part->upmp_size,
                part->upmp_type, up_mbr_name(part->upmp_type));
        SLIST_FOREACH(ext, &mbr->upm_ext[ii], upme_next)
        {
        part = &ext->upme_part;
        fprintf(stream, " %d: %c %4d/%3d/%2d-%4d/%3d/%2d %10d+%10d %02x %s\n",
                jj, splat, part->upmp_firstcyl, part->upmp_firsthead,
                part->upmp_firstsect, part->upmp_lastcyl, part->upmp_lasthead,
                part->upmp_lastsect, part->upmp_start, part->upmp_size,
                part->upmp_type, up_mbr_name(part->upmp_type));
        jj++;
        }
        part = &ext->upme_part;
    }

    if(!opts->upo_verbose)
        return;

    fprintf(stream, "Dump of MBR sector:\n");
    up_hexdump(mbr->upm_buf, MBR_SIZE, 0, stream);
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        SLIST_FOREACH(ext, &mbr->upm_ext[ii], upme_next)
        {
            fprintf(stream, "Dump of extended MBR at sector %"PRId64
                    " (0x%08"PRIx64"):\n", ext->upme_absoff, ext->upme_absoff);
            up_hexdump(ext->upme_buf, MBR_SIZE, ext->upme_absoff, stream);
        }
    }
}
