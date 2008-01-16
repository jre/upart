#include "config.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/disklabel.h>
#endif
#ifdef HAVE_SYS_DKIO_H
#include <sys/dkio.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#include "disk.h"
#include "map.h"
#include "util.h"

#if defined(HAVE_SYS_DISKLABEL_H) && \
    (defined(DIOCGPDINFO) || defined(DIOCGDINFO))
#define HAVE_GETPARAMS_DISKLABEL
#endif

static int up_opendisk(const char *name, char **path, const struct up_opts *opts);
#ifdef HAVE_OPENDISK
static int up_opendisk_libutil(const char *name, char **path);
#endif
static int getparams(int fd, struct up_disk *disk, const struct up_opts *opts);
#ifdef HAVE_GETPARAMS_DISKLABEL
static int getparams_disklabel(int fd, struct up_disk *disk,
                               const struct up_opts *opts);
#endif
static int fixparams(struct up_disk *disk, const struct up_opts *opts);
static int fixparams_checkone(struct up_disk *disk);

struct up_disk *
up_disk_open(const char *name, const struct up_opts *opts)
{
    struct up_disk *disk;
    char *path;
    int fd;

    /* open device */
    fd = up_opendisk(name, &path, opts);
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
    getparams(fd, disk, opts);
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

void
up_disk_close(struct up_disk *disk)
{
    if(!disk)
        return;
    up_map_freeall(disk);
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
up_opendisk(const char *name, char **path, const struct up_opts *opts)
{
    *path = NULL;
#ifdef HAVE_OPENDISK
    if(!opts->plainfile)
        return up_opendisk_libutil(name, path);
#endif
    return open(name, O_RDONLY);
}

#ifdef HAVE_OPENDISK
static int
up_opendisk_libutil(const char *name, char **path)
{
    char buf[MAXPATHLEN];
    int fd;

    buf[0] = 0;
    fd = opendisk(name, O_RDONLY, buf, sizeof buf, 0);
    if(0 <= fd && buf[0])
        *path = strdup(buf);

    return fd;
}
#endif

static int
getparams(int fd, struct up_disk *disk, const struct up_opts *opts)
{
#ifdef HAVE_GETPARAMS_DISKLABEL
    if(0 == getparams_disklabel(fd, disk, opts)) return 0;
#endif
        return -1;
}

#ifdef HAVE_GETPARAMS_DISKLABEL
static int
getparams_disklabel(int fd, struct up_disk *disk, const struct up_opts *opts)
{
    struct disklabel dl;

    if(opts->plainfile)
        return -1;

    errno = 0;
#ifdef DIOCGPDINFO
    if(0 > ioctl(fd, DIOCGPDINFO, &dl))
#endif
#ifdef DIOCGDINFO
    if(0 > ioctl(fd, DIOCGDINFO, &dl))
#endif
    {
        if(errno)
            fprintf(stderr, "failed to read disklabel for %s: %s\n",
                    disk->upd_path, strerror(errno));
        return -1;
    }

    disk->upd_sectsize = dl.d_secsize;
    disk->upd_cyls     = dl.d_ncylinders;
    disk->upd_heads    = dl.d_ntracks;
    disk->upd_sects    = dl.d_nsectors;
#ifdef DL_GETDSIZE
    disk->upd_size     = DL_GETDSIZE(&dl);
#else
    disk->upd_size     = dl.d_secperunit;
#endif

    return 0;
}
#endif /* GETPARAMS_DISKLABEL */

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
