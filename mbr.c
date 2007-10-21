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

/* note that this is in host byte order */
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
    off_t               upm_off;
    char                upm_buf[MBR_SIZE];
    struct up_mbr_part  upm_parts[MBR_PART_COUNT];
};

struct up_mbr_ext
{
    
};

static int readmbr(const struct up_disk *disk, int fd,
                   off_t start, off_t end, void *buf, size_t size);
static int parsembr(const struct up_disk *disk, struct up_mbr *mbr,
                    off_t start, off_t end, const void *_buf);

int
up_mbr_test(const struct up_disk *disk, int fd, off_t start, off_t end)
{
    char                buf[MBR_SIZE];

    return readmbr(disk, fd, start, end, buf, sizeof buf);
}

void *
up_mbr_load(const struct up_disk *disk, int fd, off_t start, off_t end)
{
    void *              mbr;

    up_mbr_testload(disk, fd, start, end, &mbr);
    return mbr;
}

int
up_mbr_testload(const struct up_disk *disk, int fd, off_t start, off_t end,
                void **map)
{
    char                buf[MBR_SIZE];
    int                 res;
    struct up_mbr *     mbr;

    *map = NULL;
    res = readmbr(disk, fd, start, end, buf, sizeof buf);
    if(0 >= res)
        return res;

    mbr = calloc(1, sizeof *mbr);
    if(!mbr)
    {
        perror("malloc");
        return -1;
    }

    if(0 > parsembr(disk, mbr, start, end, buf))
    {
        free(mbr);
        return 0;
    }

    *map = mbr;
    return 1;
}

static int
readmbr(const struct up_disk *disk, int fd, off_t start, off_t end,
        void *buf, size_t size)
{
    ssize_t res;

    assert(end >= start);
    if(MBR_SIZE > end - start || MBR_SIZE > size)
        return 0;

    res = pread(fd, buf, MBR_SIZE, start);
    if(0 > res)
    {
        fprintf(stderr, "failed to read MBR at offset %"PRIu64" of %s: %s\n",
                (uint64_t)start, disk->upd_path, strerror(errno));
        return -1;
    }
    if(MBR_SIZE > res)
    {
        fprintf(stderr, "failed to read MBR at offset %"PRIu64" of %s: "
                "short read count", (uint64_t)start, disk->upd_path);
        return -1;
    }

    if(MBR_MAGIC != UP_GETBUF16LE((uint8_t *)buf + MBR_MAGIC_OFF))
        return 0;

    return 1;
}

static int
parsembr(const struct up_disk *disk, struct up_mbr *mbr,
         off_t start, off_t end, const void *_buf)
{
    const uint8_t *     buf = _buf;
    int                 ii;
    struct up_mbr_part *part;

    mbr->upm_off = start;
    memcpy(mbr->upm_buf, buf, MBR_SIZE);
    buf += MBR_MAP_OFF;
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        part = &mbr->upm_parts[ii];
        if(!buf[4])
            memset(part, 0, sizeof *part);
        else
        {
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
        buf += MBR_PART_SIZE;
    }

    /*
      XXX should sanity check data here
            upmp_start > start
            upmp_start + upmp_size <= end
            partition overlap
    */

    return 0;
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

    fprintf(stream, "MBR at byte offset 0x%"PRIx64":\n"
            " #  A    C   H  S    C   H  S      Start       Size ID Name\n",
            (uint64_t)mbr->upm_off);
    for(ii = 0; MBR_PART_COUNT > ii; ii++)
    {
        part = &mbr->upm_parts[ii];
        fprintf(stream, " %d: %c %4d/%3d/%2d-%4d/%3d/%2d %10d+%10d %02x %s\n",
                ii, (MBR_FLAG_ACTIVE & part->upmp_flags ? 'y' : 'n'),
                part->upmp_firstcyl, part->upmp_firsthead,
                part->upmp_firstsect, part->upmp_lastcyl, part->upmp_lasthead,
                part->upmp_lastsect, part->upmp_start, part->upmp_size,
                part->upmp_type, up_mbr_name(part->upmp_type));
    }
    fprintf(stream, "Dump of sector 0x%x:\n", mbr->upm_off);
    up_hexdump(mbr->upm_buf, MBR_SIZE, mbr->upm_off, stream);
}
