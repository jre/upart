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
#include <unistd.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#include "disk.h"
#include "os.h"
#include "util.h"

#ifdef O_LARGEFILE
#define OPENFLAGS(flags)        (O_LARGEFILE | (flags))
#else
#define OPENFLAGS(flags)        (flags)
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
#if defined(HAVE_SYS_DKIO_H) && defined(DKIOCGGEOM)
#define HAVE_GETPARAMS_SUNOS
static int getparams_sunos(int fd, struct up_disk *disk,
                           const struct up_opts *opts);
#endif

int
up_os_opendisk(const char *name, const char **path, const struct up_opts *opts,
               int writable)
{
    static char buf[MAXPATHLEN];
    int flags, ret;

    *path = NULL;
    flags = OPENFLAGS(writable ? O_RDWR : O_RDONLY);

    if(opts->plainfile || strchr(name, '/'))
        return open(name, flags);

#ifdef HAVE_OPENDISK
    buf[0] = 0;
    ret = opendisk(name, flags, buf, sizeof buf, 0);
#else
    strlcpy(buf, "/dev/", sizeof buf);
    strlcat(buf, name, sizeof buf);
    ret = open(buf, flags);
#endif
    if(0 <= ret && buf[0])
        *path = buf;
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
#ifdef HAVE_GETPARAMS_SUNOS
    if(0 == getparams_sunos(fd, disk, opts))
        return 0;
#endif
    return -1;
}

#ifdef HAVE_GETPARAMS_DISKLABEL
static int
getparams_disklabel(int fd, struct up_disk *disk, const struct up_opts *opts)
{
    struct up_diskparams *params = &disk->ud_params;
    struct disklabel dl;

    if(UP_DISK_IS_FILE(disk))
        return -1;

    errno = 0;
#ifdef DIOCGPDINFO
    if(0 > ioctl(fd, DIOCGPDINFO, &dl))
#endif
#ifdef DIOCGDINFO
    if(0 > ioctl(fd, DIOCGDINFO, &dl))
#endif
    {
        if(errno && UP_NOISY(opts->verbosity, QUIET))
            up_err("failed to read disklabel for %s: %s",
                   UP_DISK_PATH(disk), strerror(errno));
        return -1;
    }

    params->ud_sectsize = dl.d_secsize;
    params->ud_cyls     = dl.d_ncylinders;
    params->ud_heads    = dl.d_ntracks;
    params->ud_sects    = dl.d_nsectors;
#ifdef DL_GETDSIZE
    params->ud_size     = DL_GETDSIZE(&dl);
#else
    params->ud_size     = dl.d_secperunit;
#endif

    return 0;
}
#endif /* GETPARAMS_DISKLABEL */

#ifdef HAVE_GETPARAMS_FREEBSD
static int
getparams_freebsd(int fd, struct up_disk *disk, const struct up_opts *opts)
{
    struct up_diskparams *params = &disk->ud_params;
    u_int ival;
    off_t oval;

    if(UP_DISK_IS_FILE(disk))
        return -1;

    if(0 == ioctl(fd, DIOCGSECTORSIZE, &ival))
        params->ud_sectsize = ival;
    else if(UP_NOISY(opts->verbosity, QUIET))
        up_warn("failed to read disk size for %s: %s",
                UP_DISK_PATH(disk), strerror(errno));

    if(0 < params->ud_sectsize && 0 == ioctl(fd, DIOCGMEDIASIZE, &oval))
        params->ud_size = oval / params->ud_sectsize;
    else if(UP_NOISY(opts->verbosity, QUIET))
        up_warn("failed to read sector size for %s: %s",
                UP_DISK_PATH(disk), strerror(errno));

    if(0 == ioctl(fd, DIOCGFWSECTORS, &ival))
        params->ud_sects = ival;
    else if(UP_NOISY(opts->verbosity, QUIET))
        up_warn("failed to read sectors per track for %s: %s",
                UP_DISK_PATH(disk), strerror(errno));

    if(0 == ioctl(fd, DIOCGFWHEADS, &ival))
        params->ud_heads = ival;
    else if(UP_NOISY(opts->verbosity, QUIET))
        up_warn("failed to read heads (tracks per cylinder) for %s: %s",
                UP_DISK_PATH(disk), strerror(errno));

    return 0;
}
#endif

#ifdef HAVE_GETPARAMS_LINUX
static int
getparams_linux(int fd, struct up_disk *disk, const struct up_opts *opts)
{
    struct up_diskparams *params = &disk->ud_params;
    /* XXX rather than an ugly maze of #ifdefs I'll just assume these
           all exist for now and fix it later if it ever breaks */
    struct hd_geometry geom;
    int smallsize;
    uint64_t bigsize;

    if(UP_DISK_IS_FILE(disk))
        return -1;

    if(0 == ioctl(fd, HDIO_GETGEO, &geom))
    {
        params->ud_cyls  = geom.cylinders;
        params->ud_heads = geom.heads;
        params->ud_sects = geom.sectors;
    }
    if(0 == ioctl(fd, BLKSSZGET, &smallsize))
    {
        params->ud_sectsize = smallsize;
        if(0 == ioctl(fd, BLKGETSIZE64, &bigsize))
            params->ud_size = bigsize / params->ud_sectsize;
    }
    else if(0 == ioctl(fd, BLKGETSIZE, &smallsize))
        params->ud_size = smallsize;
    else
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_err("failed to read disk size for %s: %s",
                UP_DISK_PATH(disk), strerror(errno));
        return -1;
    }

    return 0;
}
#endif

#ifdef HAVE_GETPARAMS_DARWIN
static int
getparams_darwin(int fd, struct up_disk *disk, const struct up_opts *opts)
{
    struct up_diskparams *params = &disk->ud_params;
    uint32_t smallsize;
    uint64_t bigsize;

    if(UP_DISK_IS_FILE(disk))
        return -1;

    if(0 == ioctl(fd, DKIOCGETBLOCKSIZE, &smallsize))
        params->ud_sectsize = smallsize;
    else if(UP_NOISY(opts->verbosity, QUIET))
        up_warn("failed to read sector size for %s: %s",
                UP_DISK_PATH(disk), strerror(errno));
    if(0 == ioctl(fd, DKIOCGETBLOCKCOUNT, &bigsize))
        params->ud_size = bigsize;
    else if(UP_NOISY(opts->verbosity, QUIET))
        up_warn("failed to read block count for %s: %s",
                UP_DISK_PATH(disk), strerror(errno));

    return 0;
}
#endif /* HAVE_GETPARAMS_DARWIN */

#ifdef HAVE_GETPARAMS_SUNOS
static int
getparams_sunos(int fd, struct up_disk *disk, const struct up_opts *opts)
{
    struct up_diskparams *params = &disk->ud_params;
    struct dk_geom geom;

    if(UP_DISK_IS_FILE(disk))
        return -1;

    /* XXX is there an ioctl or something to get sector size? */
    params->ud_sectsize = NBPSCTR;

    if(0 == ioctl(fd, DKIOCGGEOM, &geom))
    {
        params->ud_cyls = geom.dkg_pcyl;
        params->ud_heads = geom.dkg_nhead;
        params->ud_sects = geom.dkg_nsect;
    }
    else if(UP_NOISY(opts->verbosity, QUIET))
        up_warn("failed to read disk geometry for %s: %s",
                UP_DISK_PATH(disk), strerror(errno));

    return 0;
}
#endif /* HAVE_GETPARAMS_SUNOS */
