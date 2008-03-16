#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "disk.h"
#include "map.h"
#include "os.h"
#include "util.h"

static int fixparams(struct up_disk *disk, const struct up_opts *opts);
static int fixparams_checkone(struct up_disk *disk);
static int sectcmp(struct up_disk_sectnode *left,
                   struct up_disk_sectnode *right);

RB_GENERATE_STATIC(up_disk_sectmap, up_disk_sectnode, link, sectcmp);

struct up_disk *
up_disk_open(const char *name, const struct up_opts *opts)
{
    struct up_disk *disk;
    char *path;
    int fd;

    /* open device */
    fd = up_os_opendisk(name, &path, opts);
    if(0 > fd)
    {
        fprintf(stderr, "failed to open %s for reading: %s\n",
                (path ? path : name), strerror(errno));
        return NULL;
    }

    /* disk struct and fd */
    disk = calloc(1, sizeof *disk);
    if(!disk)
    {
        perror("malloc");
        return NULL;
    }
    disk->upd_fd = fd;
    RB_INIT(&disk->upd_sectsused);

    /* device name */
    disk->upd_name = strdup(name);
    if(!disk->upd_name)
    {
        perror("malloc");
        goto fail;
    }

    /* device path */
    if(!path)
        disk->upd_path = disk->upd_name;
    else
    {
        disk->upd_path = strdup(path);
        if(!disk->upd_path)
        {
            perror("malloc");
            goto fail;
        }
    }

    /* get drive parameters */
    up_os_getparams(fd, disk, opts);
    if(0 > fixparams(disk, opts))
    {
        fprintf(stderr, "failed to determine disk parameters for %s\n",
                disk->upd_path);
        goto fail;
    }

    disk->upd_buf = malloc(disk->upd_sectsize);
    if(!disk->upd_buf)
    {
        perror("malloc");
        goto fail;
    }

    return disk;

  fail:
    up_disk_close(disk);
    return NULL;
}

int64_t
up_disk_read(const struct up_disk *disk, int64_t start, int64_t size,
             void *buf, size_t bufsize)
{
    ssize_t res;

    /* validate and fixup arguments */
    assert(0 < bufsize && 0 < size && 0 <= start);
    bufsize -= bufsize % disk->upd_sectsize;
    if(bufsize / disk->upd_sectsize < size)
        size = bufsize / disk->upd_sectsize;
    if(0 == bufsize || 0 == size)
        return 0;
    if(INT64_MAX / disk->upd_sectsize < start)
    {
        errno = EINVAL;
        return -1;
    }

    res = pread(disk->upd_fd, buf, size * disk->upd_sectsize,
                start * disk->upd_sectsize);
    if(0 > res)
    {
        fprintf(stderr, "failed to read %s sector %"PRIu64": %s\n",
                disk->upd_path, start, strerror(errno));
        return -1;
    }

    return res / disk->upd_sectsize;
}

const void *
up_disk_getsect(struct up_disk *disk, int64_t sect)
{
    if(1 > up_disk_read(disk, sect, 1, disk->upd_buf, disk->upd_sectsize))
        return NULL;
    else
        return disk->upd_buf;
}

int
up_disk_check1sect(struct up_disk *disk, int64_t sect)
{
    return up_disk_checksectrange(disk, sect, 1);
}

int
up_disk_checksectrange(struct up_disk *disk, int64_t start, int64_t size)
{
    struct up_disk_sectnode key;

    assert(0 < size);
    memset(&key, 0, sizeof key);
    key.first = start;
    key.last  = start + size - 1;
    if(NULL == RB_FIND(up_disk_sectmap, &disk->upd_sectsused, &key))
    {
        /*
        printf("check %"PRId64"+%"PRId64": free\n", start, size);
        */
        return 0;
    }
    else
    {
        /*
        printf("check %"PRId64"+%"PRId64": used\n", start, size);
        */
        return 1;
    }
}

int
up_disk_mark1sect(struct up_disk *disk, int64_t sect, const void *ref)
{
    return up_disk_marksectrange(disk, sect, 1, ref);
}

int
up_disk_marksectrange(struct up_disk *disk, int64_t first, int64_t size,
                      const void *ref)
{
    struct up_disk_sectnode *new;

    assert(0 < size);
    new = calloc(1, sizeof *new);
    if(NULL == new)
    {
        perror("malloc");
        return -1;
    }
    new->first = first;
    new->last  = first + size - 1;
    new->ref   = ref;
    if(NULL == RB_INSERT(up_disk_sectmap, &disk->upd_sectsused, new))
    {
        /*
        printf("mark %"PRId64"+%"PRId64" with %p\n", first, size, ref);
        */
        return 0;
    }
    else
    {
        /*
        printf("failed to mark %"PRId64"+%"PRId64" with %p, already used\n",
               first, size, ref);
        */
        free(new);
        return -1;
    }
}

void
up_disk_sectsunref(struct up_disk *disk, const void *ref)
{
    struct up_disk_sectnode *ii, *inc;

    for(ii = RB_MIN(up_disk_sectmap, &disk->upd_sectsused); ii; ii = inc)
    {
        inc = RB_NEXT(up_disk_sectmap, &disk->upd_sectsused, ii);
        if(ref == ii->ref)
        {
            /*
            printf("unmark %"PRId64"+%"PRId64" with %p\n",
                   ii->first, ii->last - ii->first + 1, ii->ref);
            */
            RB_REMOVE(up_disk_sectmap, &disk->upd_sectsused, ii);
            free(ii);
        }
    }
}

void
up_disk_close(struct up_disk *disk)
{
    if(!disk)
        return;
    up_map_freeall(disk);
    assert(RB_EMPTY(&disk->upd_sectsused));
    if(0 <= disk->upd_fd)
        close(disk->upd_fd);
    if(disk->upd_buf)
        free(disk->upd_buf);
    if(disk->upd_name)
        free(disk->upd_name);
    if(disk->upd_path && disk->upd_path != disk->upd_name)
        free(disk->upd_path);
    free(disk);
}

static int
fixparams(struct up_disk *disk, const struct up_opts *opts)
{
    struct stat sb;

    /* command-line options override any other values */
    if(0 < opts->sectsize)
        disk->upd_sectsize = opts->sectsize;
    if(0 < opts->cyls)
        disk->upd_cyls = opts->cyls;
    if(0 < opts->heads)
        disk->upd_heads = opts->heads;
    if(0 < opts->sects)
        disk->upd_sects = opts->sects;

    /* sector size defaults to 512 */
    if(0 >= disk->upd_sectsize)
        disk->upd_sectsize = 512;

    /* we can get the total size for a plain file */
    if(0 >= disk->upd_size && opts->plainfile && 0 == stat(disk->upd_path, &sb))
    {
        disk->upd_size = sb.st_size / disk->upd_sectsize;
    }

    /* is that good enough? */
    if(0 == fixparams_checkone(disk))
        return 0;

    /* apparently not, try defaulting heads and sectors to 255 and 63 */
    if(0 >= disk->upd_heads)
        disk->upd_heads = 255;
    if(0 >= disk->upd_sects)
        disk->upd_sects = 63;

    /* ok, is it good enough now? */
    if(0 == fixparams_checkone(disk))
        return 0;

    /* guess not */
    return -1;
}

static int
fixparams_checkone(struct up_disk *disk)
{
    /* if we have all 4 then we're good */
    if(0 <  disk->upd_cyls  && 0 <  disk->upd_heads &&
       0 <  disk->upd_sects && 0 <  disk->upd_size )
        return 0;

    /* guess cyls from other three */
    if(0 >= disk->upd_cyls  && 0 <  disk->upd_heads &&
       0 <  disk->upd_sects && 0 <  disk->upd_size )
    {
        disk->upd_cyls = disk->upd_size / disk->upd_sects / disk->upd_heads;
        return 0;
    }

    /* guess heads from other three */
    if(0 <  disk->upd_cyls  && 0 >= disk->upd_heads &&
       0 <  disk->upd_sects && 0 <  disk->upd_size )
    {
        disk->upd_heads = disk->upd_size / disk->upd_sects / disk->upd_cyls;
        return 0;
    }

    /* guess sects from other three */
    if(0 <  disk->upd_cyls  && 0 <  disk->upd_heads &&
       0 >= disk->upd_sects && 0 <  disk->upd_size )
    {
        disk->upd_sects = disk->upd_size / disk->upd_heads / disk->upd_cyls;
        return 0;
    }

    /* guess size from other three */
    if(0 <  disk->upd_cyls  && 0 <  disk->upd_heads &&
       0 <  disk->upd_sects && 0 >= disk->upd_size )
    {
        disk->upd_size = disk->upd_sects * disk->upd_heads * disk->upd_cyls;
        return 0;
    }

    /* can only guess with one missing param */
    return -1;
}

void
up_disk_dump(const struct up_disk *disk, void *_stream,
             const struct up_opts *opt)
{
    FILE *              stream = _stream;
    const char *        unit;
    float               size;

    size = up_fmtsize(disk->upd_size * disk->upd_sectsize, &unit);
    fprintf(stream, "%s: %.*f%s (%"PRId64" sectors of %d bytes)\n",
            disk->upd_name, UP_BESTDECIMAL(size), size, unit,
            disk->upd_size, disk->upd_sectsize);
    if(opt->verbose)
    fprintf(stream,
            "    device path:         %s\n"
            "    sector size:         %d\n"
            "    total sectors:       %"PRId64"\n"
            "    total cylinders:     %d (cylinders)\n"
            "    tracks per cylinder: %d (heads)\n"
            "    sectors per track:   %d (sectors)\n"
            "\n", disk->upd_path, disk->upd_sectsize, disk->upd_size,
            (int)disk->upd_cyls, (int)disk->upd_heads, (int)disk->upd_sects);
}

static int
sectcmp(struct up_disk_sectnode *left, struct up_disk_sectnode *right)
{
    if(left->last < right->first)
    {
        /*
        printf("cmp (%"PRId64"+%"PRId64") < (%"PRId64"+%"PRId64")\n",
               left->first, left->last - left->first + 1,
               right->first, right->last - right->first + 1);
        */
        return -1;
    }
    else if(left->first > right->last)
    {
        /*
        printf("cmp (%"PRId64"+%"PRId64") > (%"PRId64"+%"PRId64")\n",
               left->first, left->last - left->first + 1,
               right->first, right->last - right->first + 1);
        */
        return 1;
    }
    else
    {
        /*
        printf("cmp (%"PRId64"+%"PRId64") = (%"PRId64"+%"PRId64")\n",
               left->first, left->last - left->first + 1,
               right->first, right->last - right->first + 1);
        */
        return 0;
    }
}
