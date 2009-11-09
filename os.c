#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_HDREG_H
#include <linux/hdreg.h>
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#define MINIMAL_NAMESPACE_POLLUTION_PLEASE
#include "disk.h"
#include "os.h"
#include "os-bsd.h"
#include "os-darwin.h"
#include "os-haiku.h"
#include "os-solaris.h"
#include "util.h"

#ifdef O_LARGEFILE
#define OPENFLAGS(flags)        (O_LARGEFILE | (flags))
#else
#define OPENFLAGS(flags)        (flags)
#endif

#if defined(linux) || defined(__linux) || defined(__linux__)
#define HAVE_LISTDEV_LINUX
static int	listdev_linux(FILE *);
#endif

#if defined(HAVE_LINUX_FS_H) || defined(HAVE_LINUX_HDREG_H)
#define HAVE_GETPARAMS_LINUX
static int	getparams_linux(int, struct disk_params *, const char *);
#endif

#ifdef HAVE_OPENDISK
#define OS_OPENDISK_OPENDISK	(opendisk_opendisk)
static int	opendisk_opendisk(const char *, int, char *, size_t, int);
#else
#define OS_OPENDISK_OPENDISK	(0)
#endif
static int	opendisk_generic(const char *, int, char *, size_t, int);

#define DEVPREFIX		"/dev/"

int
os_list_devices(void *stream)
{
	int (*funcs[])(FILE *) = {
	/* The order of these is significant, more than one may be defined. */
		OS_LISTDEV_IOKIT,
#ifdef HAVE_LISTDEV_LINUX
		listdev_linux,
#endif
		OS_LISTDEV_HAIKU,
		OS_LISTDEV_SOLARIS,
		OS_LISTDEV_SYSCTL,
	};
	int once, i;

	once = 0;
	for (i = 0; i < NITEMS(funcs); i++) {
		if (funcs[i] != NULL) {
			once = 1;
			if ((funcs[i])(stream) == 0)
				return (0);
		}
	}
	if (!once)
		up_err("don't know how to list devices on this platform");

	return (-1);
}

int
up_os_opendisk(const char *name, const char **path)
{
	int (*funcs[])(const char *, int, char *, size_t, int) = {
	/* The order of these is significant, more than one may be defined. */
		OS_OPENDISK_OPENDISK,
		OS_OPENDISK_HAIKU,
		OS_OPENDISK_SOLARIS,
		opendisk_generic,
	};
	static char buf[MAXPATHLEN];
	int flags, i, ret;

	*path = NULL;
	flags = OPENFLAGS(O_RDONLY);

	if (opts->plainfile)
		return open(name, flags);

	for (i = 0; i < NITEMS(funcs); i++) {
		if (funcs[i] == NULL)
			continue;
		ret = (funcs[i])(name, flags, buf, sizeof(buf), 0);
		if (ret >= 0 && buf[0] != '\0')
			*path = buf;
		return (ret);
	}

	return (-1);
}

int
up_os_getparams(int fd, struct disk_params *params, const char *name)
{
	int (*funcs[])(int, struct disk_params *, const char *) = {
	/* The order of these is significant, more than one may be defined. */
	    OS_GETPARAMS_FREEBSD,
	    OS_GETPARAMS_DISKLABEL,
#ifdef HAVE_GETPARAMS_LINUX
	    getparams_linux,
#endif
	    OS_GETPARAMS_DARWIN,
	    OS_GETPARAMS_SOLARIS,
	    OS_GETPARAMS_HAIKU,
	};
	int once, i;

	once = 0;
	for (i = 0; i < NITEMS(funcs); i++) {
		if (funcs[i] != NULL) {
			once = 1;
			if ((funcs[i])(fd, params, name) == 0)
				return (0);
		}
	}
	if (!once)
		up_err("don't know how to get disk parameters "
		    "on this platform");

	return (-1);
}

#ifdef HAVE_LISTDEV_LINUX
static int
listdev_linux(FILE *stream)
{
	struct dirent *ent;
	DIR *dir;
	int once, i;

	/* XXX ugh, there has to be a better way to do this */

	if ((dir = opendir("/dev")) == NULL) {
		up_warn("failed to list /dev: %s", strerror(errno));
		return (-1);
	}
	once = 0;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_type != DT_BLK ||
		    strlen(ent->d_name) < 3 ||
		    ent->d_name[1] != 'd')
			continue;
		switch (ent->d_name[0]) {
		case 'h':
		case 's':
			break;
		default:
			continue;
		}
		for (i = 2; ent->d_name[i] != '\0'; i++)
			if (ent->d_name[i] < 'a' ||
			    ent->d_name[i] > 'z')
				break;
		if (ent->d_name[i] == '\0') {
			if (once)
				putc(' ', stream);
			fputs(ent->d_name, stream);
			once = 1;
		}
	}
	closedir(dir);
	if (once)
		putc('\n', stream);
	return (0);
}
#endif

#ifdef HAVE_GETPARAMS_LINUX
static int
getparams_linux(int fd, struct disk_params *params, const char *name)
{
	struct hd_geometry geom;
	int smallsize;
	uint64_t bigsize;

	/* XXX rather than an ugly maze of #ifdefs I'll just assume these
	   ioctls all exist for now and fix it later if it ever breaks */
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
			    name, strerror(errno));
		return (-1);
	}

	return (0);
}
#endif

#if HAVE_OPENDISK
static int
opendisk_opendisk(const char *name, int flags, char *buf, size_t buflen,
    int cooked)
{
	buf[0] = '\0';
	return (opendisk(name, flags, buf, buflen, cooked));
}
#endif /* HAVE_OPENDISK */

static int
opendisk_generic(const char *name, int flags, char *buf, size_t buflen,
    int cooked)
{
	int ret;

	strlcpy(buf, name, buflen);
	if ((ret = open(name, flags)) >= 0 ||
	    errno != ENOENT ||
	    strchr(name, '/') != NULL)
		return (ret);

	if (strlcpy(buf, DEVPREFIX, buflen) >= buflen ||
	    strlcat(buf, name, buflen) >= buflen) {
		errno = ENOMEM;
		return (-1);
	}
	return (open(buf, flags));
}
