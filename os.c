#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/disklabel.h>
#endif
#ifdef HAVE_SYS_DKIO_H
#include <sys/dkio.h>
#endif
#ifdef HAVE_SYS_DISK_H
#include <sys/disk.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_HDREG_H
#include <linux/hdreg.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#include "disk.h"
#include "os.h"
#include "util.h"

#ifdef O_LARGEFILE
#define OPENFLAGS O_RDONLY | O_LARGEFILE
#else
#define OPENFLAGS O_RDONLY
#endif

#if defined(HAVE_SYS_DISKLABEL_H) && \
    (defined(DIOCGPDINFO) || defined(DIOCGDINFO))
#define HAVE_GETPARAMS_DISKLABEL
static int getparams_disklabel(int fd, struct up_disk *disk,
                               const struct up_opts *opts);
#endif
#if defined(HAVE_SYS_DISK_H) && defined(DIOCGSECTORSIZE)
#define HAVE_GETPARAMS_FREEBSD
static int getparams_freebsd(int fd, struct up_disk *disk,
                             const struct up_opts *opts);
#endif
#if defined(HAVE_LINUX_FS_H) || defined(HAVE_LINUX_HDREG_H)
#define HAVE_GETPARAMS_LINUX
static int getparams_linux(int fd, struct up_disk *disk,
                           const struct up_opts *opts);
#endif
#if defined(HAVE_SYS_DISK_H) && defined(DKIOCGETBLOCKSIZE)
#define HAVE_GETPARAMS_DARWIN
static int getparams_darwin(int fd, struct up_disk *disk,
                           const struct up_opts *opts);
#endif

int
up_os_opendisk(const char *name, const char **path, const struct up_opts *opts)
{
    static char buf[MAXPATHLEN];
    int ret;

    *path = NULL;

#ifdef HAVE_OPENDISK
    buf[0] = 0;
    ret = opendisk(name, OPENFLAGS, buf, sizeof buf, 0);
    if(0 <= ret && buf[0])
        *path = buf;
    return ret;
#endif

    ret = open(name, OPENFLAGS);
    if(0 > ret && ENOENT == errno)
    {
        strlcpy(buf, "/dev/", sizeof buf);
        strlcat(buf, name, sizeof buf);
        ret = open(buf, OPENFLAGS);
        if(0 <= ret)
            *path = buf;
    }
    return ret;
}

int
up_os_getparams(int fd, struct up_disk *disk, const struct up_opts *opts)
{
#ifdef HAVE_GETPARAMS_FREEBSD
    if(0 == getparams_freebsd(fd, disk, opts))
        return 0;
#endif
#ifdef HAVE_GETPARAMS_DISKLABEL
    if(0 == getparams_disklabel(fd, disk, opts))
        return 0;
#endif
#ifdef HAVE_GETPARAMS_LINUX
    if(0 == getparams_linux(fd, disk, opts))
        return 0;
#endif
#ifdef HAVE_GETPARAMS_DARWIN
    if(0 == getparams_darwin(fd, disk, opts))
        return 0;
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

#ifdef HAVE_GETPARAMS_FREEBSD
static int
getparams_freebsd(int fd, struct up_disk *disk, const struct up_opts *opts)
{
    u_int ival;
    off_t oval;

    if(opts->plainfile)
        return -1;

    if(0 == ioctl(fd, DIOCGSECTORSIZE, &ival))
        disk->upd_sectsize = ival;
    else
        fprintf(stderr, "failed to read disk size for %s: %s\n",
                disk->upd_path, strerror(errno));

    if(0 < disk->upd_sectsize && 0 == ioctl(fd, DIOCGMEDIASIZE, &oval))
        disk->upd_size = oval / disk->upd_sectsize;
    else
        fprintf(stderr, "failed to read sector size for %s: %s\n",
                disk->upd_path, strerror(errno));

    if(0 == ioctl(fd, DIOCGFWSECTORS, &ival))
        disk->upd_sects = ival;
    else
        fprintf(stderr, "failed to read sectors per track for %s: %s\n",
                disk->upd_path, strerror(errno));

    if(0 == ioctl(fd, DIOCGFWHEADS, &ival))
        disk->upd_heads = ival;
    else
        fprintf(stderr, "failed to read heads (tracks per cylinder) for %s: %s\n",
                disk->upd_path, strerror(errno));

    return 0;
}
#endif

#ifdef HAVE_GETPARAMS_LINUX
static int
getparams_linux(int fd, struct up_disk *disk, const struct up_opts *opts)
{
    /* XXX rather than an ugly maze of #ifdefs I'll just assume these
           all exist for now and fix it later if it ever breaks */
    struct hd_geometry geom;
    int smallsize;
    uint64_t bigsize;

    if(opts->plainfile)
        return -1;

    if(0 == ioctl(fd, HDIO_GETGEO, &geom))
    {
        disk->upd_cyls  = geom.cylinders;
        disk->upd_heads = geom.heads;
        disk->upd_sects = geom.sectors;
    }
    if(0 == ioctl(fd, BLKSSZGET, &smallsize))
    {
        disk->upd_sectsize = smallsize;
        if(0 == ioctl(fd, BLKGETSIZE64, &bigsize))
            disk->upd_size = bigsize / disk->upd_sectsize;
    }
    else if(0 == ioctl(fd, BLKGETSIZE, &smallsize))
        disk->upd_size = smallsize;
    else
    {
        fprintf(stderr, "failed to read disk size for %s: %s\n",
                disk->upd_path, strerror(errno));
        return -1;
    }

    return 0;
}
#endif

#ifdef HAVE_GETPARAMS_DARWIN
static int
getparams_darwin(int fd, struct up_disk *disk, const struct up_opts *opts)
{
    uint32_t smallsize;
    uint64_t bigsize;

    if(opts->plainfile)
        return -1;

    if(0 == ioctl(fd, DKIOCGETBLOCKSIZE, &smallsize))
        disk->upd_sectsize = smallsize;
    else
        fprintf(stderr, "failed to read sector size for %s: %s\n",
                disk->upd_path, strerror(errno));
    if(0 == ioctl(fd, DKIOCGETBLOCKCOUNT, &bigsize))
        disk->upd_size = bigsize;
    else
        fprintf(stderr, "failed to read sector size for %s: %s\n",
                disk->upd_path, strerror(errno));

    return 0;
}
#endif /* HAVE_GETPARAMS_DARWIN */
