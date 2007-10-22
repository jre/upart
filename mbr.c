#include "config.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

struct up_mbr
{
    char                upm_buf[MBR_SIZE];
    struct up_mbr_part  upm_parts[MBR_PART_COUNT];
};

static int readmbr(const struct up_disk *disk, int64_t start, int64_t size,
                   uint8_t *buf, size_t buflen);
static int parsembr(const struct up_disk *disk, struct up_mbr *mbr,
                    int64_t start, int64_t size,
                    const uint8_t *buf, size_t buflen);
static void parsembrpart(const uint8_t *buf, size_t buflen,
                         struct up_mbr_part *part);

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
    int                 res;
    struct up_mbr *     mbr;

    /* load MBR */
    *map = NULL;
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

    *map = mbr;
    return 1;
}

static int
readmbr(const struct up_disk *disk, int64_t start, int64_t size,
        uint8_t *buf, size_t buflen)
{
    ssize_t res;

    if(start || MBR_SIZE > size || MBR_SIZE > buflen)
        return 0;

    res = pread(disk->upd_fd, buf, MBR_SIZE, start);
    if(0 > res)
    {
        fprintf(stderr, "failed to read MBR at offset %"PRIu64" of %s: %s\n",
                start, disk->upd_path, strerror(errno));
        return -1;
    }
    if(MBR_SIZE > res)
    {
        fprintf(stderr, "failed to read MBR at offset %"PRIu64" of %s: "
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
    int64_t             pstart, psize;

    assert(0 == start && size >= MBR_SIZE && MBR_SIZE == buflen &&
           MBR_MAGIC == UP_GETBUF16LE(buf + MBR_MAGIC_OFF));
    memcpy(mbr->upm_buf, buf, MBR_SIZE);
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        assert(MBR_MAP_OFF + (ii * MBR_PART_SIZE) + MBR_PART_SIZE <= MBR_SIZE);
        parsembrpart(buf + MBR_MAP_OFF + (ii * MBR_PART_SIZE), MBR_PART_SIZE,
                     &mbr->upm_parts[ii]);
        pstart = mbr->upm_parts[ii].upmp_start;
        psize  = mbr->upm_parts[ii].upmp_size;
/*
        fprintf(stderr, "XXX valid %d %d %d ps=0x%x pz=0x%x ss=0x%x es=0x%x\n",
                (start < pstart), (start + size > pstart),
                (size - pstart >= psize),
                partstart, partsize, startsec, endsec);
*/
        if(start < pstart && start + size > pstart &&
           size - pstart >= psize)
            mbr->upm_parts[ii].upmp_valid = 1;
    }

    /* XXX should check for partition overlap */

    return 0;
}

static void
parsembrpart(const uint8_t *buf, size_t buflen, struct up_mbr_part *part)
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
    }
}

void
up_mbr_free(void *mbr)
{
    if(mbr)
        free(mbr);
}

void
up_mbr_dump(void *_mbr, void *_stream)
{
    struct up_mbr *     mbr = _mbr;
    FILE *              stream = _stream;
    int                 ii;
    struct up_mbr_part *part;
    char                splat;

    fprintf(stream, "MBR:\n"
            "#        C   H  S    C   H  S      Start       Size ID Name\n");
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
    }
    fprintf(stream, "Dump of MBR sector:\n");
    up_hexdump(mbr->upm_buf, MBR_SIZE, 0, stream);
}
