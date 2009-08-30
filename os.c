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
static int	getparams_disklabel(int, struct up_disk *);
#endif
#if defined(HAVE_SYS_DISK_H) && defined(DIOCGSECTORSIZE)
#define HAVE_GETPARAMS_FREEBSD
static int	getparams_freebsd(int, struct up_disk *);
#endif
#if defined(HAVE_LINUX_FS_H) || defined(HAVE_LINUX_HDREG_H)
#define HAVE_GETPARAMS_LINUX
static int	getparams_linux(int, struct up_disk *);
#endif
#if defined(HAVE_SYS_DISK_H) && defined(DKIOCGETBLOCKSIZE)
#define HAVE_GETPARAMS_DARWIN
static int	getparams_darwin(int, struct up_disk *);
#endif
#if defined(HAVE_SYS_DKIO_H) && defined(DKIOCGGEOM)
#define HAVE_GETPARAMS_SUNOS
static int	getparams_sunos(int, struct up_disk *);
#endif

#if defined(sun) || defined(__sun) || defined(__sun__)
#define DEVPREFIX		"/dev/rdsk/"
#else
#define DEVPREFIX		"/dev/"
#endif

int
up_os_opendisk(const char *name, const char **path)
{
	static char buf[MAXPATHLEN];
	int flags, ret;

	*path = NULL;
	flags = OPENFLAGS(O_RDONLY);

	if (opts->plainfile || strchr(name, '/'))
		return open(name, flags);

#ifdef HAVE_OPENDISK
	buf[0] = 0;
	ret = opendisk(name, flags, buf, sizeof buf, 0);
#else
	ret = open(name, flags);
	if (ret < 0) {
		strlcpy(buf, DEVPREFIX, sizeof buf);
		strlcat(buf, name, sizeof buf);
		ret = open(buf, flags);
	}
#endif
	if (ret >= 0 && buf[0])
		*path = buf;
	return (ret);
}

int
up_os_getparams(int fd, struct up_disk *disk)
{
	/* The order of these is significant, more than one may be defined. */
#ifdef HAVE_GETPARAMS_FREEBSD
	if (getparams_freebsd(fd, disk) == 0)
		return (0);
#endif
#ifdef HAVE_GETPARAMS_DISKLABEL
	if (getparams_disklabel(fd, disk) == 0)
		return (0);
#endif
#ifdef HAVE_GETPARAMS_LINUX
	if (getparams_linux(fd, disk) == 0)
		return (0);
#endif
#ifdef HAVE_GETPARAMS_DARWIN
	if (getparams_darwin(fd, disk) == 0)
		return (0);
#endif
#ifdef HAVE_GETPARAMS_SUNOS
	if (getparams_sunos(fd, disk) == 0)
		return (0);
#endif
	return (-1);
}

#ifdef HAVE_GETPARAMS_DISKLABEL
static int
getparams_disklabel(int fd, struct up_disk *disk)
{
	struct disk_params *params;
	struct disklabel dl;

	if (UP_DISK_IS_FILE(disk))
		return (-1);

	params = &disk->ud_params;
	errno = 0;
#ifdef DIOCGPDINFO
	if (ioctl(fd, DIOCGPDINFO, &dl) < 0)
#endif
#ifdef DIOCGDINFO
	if (ioctl(fd, DIOCGDINFO, &dl) < 0)
#endif
	{
		if (errno && UP_NOISY(QUIET))
			up_err("failed to get disklabel for %s: %s",
			    UP_DISK_PATH(disk), strerror(errno));
		return (-1);
	}

	params->sectsize = dl.d_secsize;
	params->cyls = dl.d_ncylinders;
	params->heads = dl.d_ntracks;
	params->sects = dl.d_nsectors;
#ifdef DL_GETDSIZE
	params->size = DL_GETDSIZE(&dl);
#else
	params->size = dl.d_secperunit;
#endif

	return (0);
}
#endif /* GETPARAMS_DISKLABEL */

#ifdef HAVE_GETPARAMS_FREEBSD
static int
getparams_freebsd(int fd, struct up_disk *disk)
{
	struct disk_params *params;
	u_int ival;
	off_t oval;

	if (UP_DISK_IS_FILE(disk))
		return -1;

	params = &disk->ud_params;
	if (ioctl(fd, DIOCGSECTORSIZE, &ival) == 0)
		params->sectsize = ival;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get disk size for %s: %s",
		    UP_DISK_PATH(disk), strerror(errno));

	if (params->sectsize > 0 && ioctl(fd, DIOCGMEDIASIZE, &oval) == 0)
		params->size = oval / params->sectsize;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get sector size for %s: %s",
		    UP_DISK_PATH(disk), strerror(errno));

	if (ioctl(fd, DIOCGFWSECTORS, &ival) == 0)
		params->sects = ival;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get sectors per track for %s: %s",
		    UP_DISK_PATH(disk), strerror(errno));

	if (ioctl(fd, DIOCGFWHEADS, &ival) == 0)
		params->heads = ival;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get heads (tracks per cylinder) for %s: %s",
		    UP_DISK_PATH(disk), strerror(errno));

	return (0);
}
#endif

#ifdef HAVE_GETPARAMS_LINUX
static int
getparams_linux(int fd, struct up_disk *disk)
{
	struct disk params *params;
	struct hd_geometry geom;
	int smallsize;
	uint64_t bigsize;

	if (UP_DISK_IS_FILE(disk))
		return (-1);

	/* XXX rather than an ugly maze of #ifdefs I'll just assume these
	   ioctls all exist for now and fix it later if it ever breaks */
	params = &disk->ud_params;
	if (ioctl(fd, HDIO_GETGEO, &geom) == 0) {
		params->cyls = geom.cylinders;
		params->heads = geom.heads;
		params->sects = geom.sectors;
	}
	if (ioctl(fd, BLKSSZGET, &smallsize) == 0) {
		params->sectsize = smallsize;
	if (ioctl(fd, BLKGETSIZE64, &bigsize) == 0)
		params->size = bigsize / params->sectsize;
	} else if (ioctl(fd, BLKGETSIZE, &smallsize) == 0)
		params->size = smallsize;
	else {
		if (UP_NOISY(QUIET))
			up_err("failed to get disk size for %s: %s",
			    UP_DISK_PATH(disk), strerror(errno));
		return (-1);
	}

	return (0);
}
#endif

#ifdef HAVE_GETPARAMS_DARWIN
static int
getparams_darwin(int fd, struct up_disk *disk)
{
	struct disk_params *params;
	uint32_t smallsize;
	uint64_t bigsize;

	if (UP_DISK_IS_FILE(disk))
		return (-1);

	params = &disk->ud_params;
	if (ioctl(fd, DKIOCGETBLOCKSIZE, &smallsize) == 0)
		params->sectsize = smallsize;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get sector size for %s: %s",
		    UP_DISK_PATH(disk), strerror(errno));
	if (ioctl(fd, DKIOCGETBLOCKCOUNT, &bigsize) == 0)
		params->size = bigsize;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get block count for %s: %s",
		    UP_DISK_PATH(disk), strerror(errno));

	return (0);
}
#endif /* HAVE_GETPARAMS_DARWIN */

#ifdef HAVE_GETPARAMS_SUNOS
static int
getparams_sunos(int fd, struct up_disk *disk)
{
	struct disk_params *params;
	struct dk_geom geom;

	if (UP_DISK_IS_FILE(disk))
        	return (-1);

	params = &disk->ud_params;
	/* XXX is there an ioctl or something to get sector size? */
	params->sectsize = NBPSCTR;

	if (ioctl(fd, DKIOCG_PHYGEOM, &geom) == 0 ||
	    ioctl(fd, DKIOCGGEOM, &geom) == 0) {
		params->cyls = geom.dkg_pcyl;
		params->heads = geom.dkg_nhead;
		params->sects = geom.dkg_nsect;
	} else if (UP_NOISY(QUIET))
		up_warn("failed to read disk geometry for %s: %s",
		    UP_DISK_PATH(disk), strerror(errno));

	return (0);
}
#endif /* HAVE_GETPARAMS_SUNOS */
