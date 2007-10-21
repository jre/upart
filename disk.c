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

    return disk;

  fail:
    up_disk_close(disk);
    return NULL;
}

void
up_disk_close(struct up_disk *disk)
{
    if(!disk)
        return;
    if(0 <= disk->upd_fd)
        close(disk->upd_fd);
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
            fprintf(stderr, "failed to read disklabel for %s: %s",
                    disk->upd_path, strerror(errno));
        return -1;
    }

    disk->upd_sectsize = dl.d_secsize;
    disk->upd_cyls     = dl.d_ncylinders;
    disk->upd_heads    = dl.d_ntracks;
    disk->upd_sects    = dl.d_nsectors;
    disk->upd_size     = dl.d_secperunit;

    return 0;
}
#endif /* GETPARAMS_DISKLABEL */
