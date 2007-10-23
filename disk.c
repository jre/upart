#include "config.h"

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
#include "util.h"

#if defined(HAVE_SYS_DISKLABEL_H) && \
    (defined(DIOCGPDINFO) || defined(DIOCGDINFO))
#define HAVE_GETPARAMS_DISKLABEL
#endif

static int up_opendisk(const char *name, char **path);
#ifdef HAVE_OPENDISK
static int up_opendisk_libutil(const char *name, char **path);
#endif
static int getparams(int fd, struct up_disk *disk);
#ifdef HAVE_GETPARAMS_DISKLABEL
static int getparams_disklabel(int fd, struct up_disk *disk);
#endif

struct up_disk *
up_disk_open(const char *name)
{
    struct up_disk *disk;
    char *path;
    int fd;

    /* open device */
    fd = up_opendisk(name, &path);
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
    if(0 > getparams(fd, disk))
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
up_opendisk(const char *name, char **path)
{
    *path = NULL;
#ifdef HAVE_OPENDISK
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
getparams(int fd, struct up_disk *disk)
{
#ifdef HAVE_GETPARAMS_DISKLABEL
    if(0 == getparams_disklabel(fd, disk)) return 0;
#endif
        return -1;
}

#ifdef HAVE_GETPARAMS_DISKLABEL
static int
getparams_disklabel(int fd, struct up_disk *disk)
{
    struct disklabel dl;

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
    if(opt->upo_verbose)
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
