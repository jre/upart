#include "config.h"

#include <sys/ioctl.h>
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/disklabel.h>
#endif
#ifdef HAVE_SYS_DKIO_H
#include <sys/dkio.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "disk.h"

#if defined(HAVE_SYS_DISKLABEL_H) && \
    (defined(DIOCGPDINFO) || defined(DIOCGDINFO))
#define HAVE_GETPARAMS_DISKLABEL
#endif

static int getparams(int fd, struct up_disk *disk);
#ifdef HAVE_GETPARAMS_DISKLABEL
static int getparams_disklabel(int fd, struct up_disk *disk);
#endif

int
up_disk_open(const char *path)
{
    int fd;

    fd = open(path, O_RDONLY);
    if(0 > fd)
    {
        fprintf(stderr, "failed to open %s for reading: %s\n",
                path, strerror(errno));
        return -1;
    }

    return fd;
}

void
up_disk_close(int fd)
{
    close(fd);
}

struct up_disk *
up_disk_load(const char *path, int fd)
{
    struct up_disk *disk;
    size_t len;

    disk = calloc(1, sizeof *disk);
    if(!disk)
    {
        perror("malloc");
        return NULL;
    }

    len = strlen(path);
    disk->upd_path = malloc(len + 1);
    if(!disk->upd_path)
    {
        perror("malloc");
        free(disk);
        return NULL;
    }
    snprintf(disk->upd_path, len + 1, "%s", path);

    if(0 > getparams(fd, disk))
    {
        fprintf(stderr, "failed to determine disk parameters for %s\n", path);
        up_disk_free(disk);
        return NULL;
    }

    return disk;
}

void
up_disk_free(struct up_disk *disk)
{
    if(!disk)
        return;
    if(disk->upd_path)
        free(disk->upd_path);
    free(disk);
}

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
