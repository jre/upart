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
#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#ifdef __HAIKU__

#include <DiskDeviceDefs.h>
#include <OS.h>

/* XXX this sucks */

// userland partition representation
struct user_partition_data {
	partition_id	id;
	off_t		offset;
	off_t		size;
	off_t		content_size;
	uint32		block_size;
	uint32		status;
	uint32		flags;
	dev_t		volume;
	int32		index;
	int32		change_counter;
	disk_system_id	disk_system;
	char*		name;
	char*		content_name;
	char*		type;
	char*		content_type;
	char*		parameters;
	char*		content_parameters;
	void*		user_data;
	int32		child_count;
	struct user_partition_data*	children[1];
};

// userland disk device representation
struct user_disk_device_data {
	uint32		device_flags;
	char*		path;
	struct user_partition_data	device_partition_data;
};

/* Disk Device Manager syscalls */
partition_id	_kern_get_next_disk_device_id(int32 *, size_t *);
partition_id	_kern_find_disk_device(const char *, size_t *);
status_t	_kern_get_disk_device_data(partition_id, bool,
    struct user_disk_device_data *, size_t , size_t *);

#define HAVE_OPENDISK
static int	opendisk(const char *, int, char *, size_t, int);

#endif /* __HAIKU__ */

#define MINIMAL_NAMESPACE_POLLUTION_PLEASE
#include "disk.h"
#include "os.h"
#include "util.h"

#ifdef O_LARGEFILE
#define OPENFLAGS(flags)        (O_LARGEFILE | (flags))
#else
#define OPENFLAGS(flags)        (flags)
#endif

#if defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTL)
#define HAVE_LISTDEV_SYSCTL
static int	listdev_sysctl(FILE *);
#endif

#if defined(HAVE_SYS_DISKLABEL_H) && \
    (defined(DIOCGPDINFO) || defined(DIOCGDINFO))
#define HAVE_GETPARAMS_DISKLABEL
static int	getparams_disklabel(int, struct disk_params *, const char *);
#endif
#if defined(HAVE_SYS_DISK_H) && defined(DIOCGSECTORSIZE)
#define HAVE_GETPARAMS_FREEBSD
static int	getparams_freebsd(int, struct disk_params *, const char *);
#endif
#if defined(HAVE_LINUX_FS_H) || defined(HAVE_LINUX_HDREG_H)
#define HAVE_GETPARAMS_LINUX
static int	getparams_linux(int, struct disk_params *, const char *);
#endif
#if defined(HAVE_SYS_DISK_H) && defined(DKIOCGETBLOCKSIZE)
#define HAVE_GETPARAMS_DARWIN
static int	getparams_darwin(int, struct disk_params *, const char *);
#endif
#if defined(HAVE_SYS_DKIO_H) && defined(DKIOCGGEOM)
#define HAVE_GETPARAMS_SUNOS
static int	getparams_sunos(int, struct disk_params *, const char *);
#endif
#if defined(__HAIKU__)
#define HAVE_GETPARAMS_HAIKU
#define HAVE_LISTDEV_HAIKU
static int	listdev_haiku(FILE *);
static int	getparams_haiku(int, struct disk_params *, const char *);
#endif

#if defined(sun) || defined(__sun) || defined(__sun__)
#define DEVPREFIX		"/dev/rdsk/"
#else
#define DEVPREFIX		"/dev/"
#endif

int
os_list_devices(void *stream)
{
	int (*funcs[])(FILE *) = {
	/* The order of these is significant, more than one may be defined. */
#ifdef HAVE_LISTDEV_SYSCTL
		listdev_sysctl,
#endif
#ifdef HAVE_LISTDEV_HAIKU
		listdev_haiku,
#endif
	};
	int i;

	if (NITEMS(funcs) == 0) {
		up_err("don't know how to list devices on this platform");
		return (-1);
	}
	for (i = 0; i < NITEMS(funcs); i++)
		if ((funcs[i])(stream) == 0)
			return (0);

	return (-1);
}

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
up_os_getparams(int fd, struct disk_params *params, const char *name)
{
	int (*funcs[])(int, struct disk_params *, const char *) = {
	/* The order of these is significant, more than one may be defined. */
#ifdef HAVE_GETPARAMS_FREEBSD
	    getparams_freebsd,
#endif
#ifdef HAVE_GETPARAMS_DISKLABEL
	    getparams_disklabel,
#endif
#ifdef HAVE_GETPARAMS_LINUX
	    getparams_linux,
#endif
#ifdef HAVE_GETPARAMS_DARWIN
	    getparams_darwin,
#endif
#ifdef HAVE_GETPARAMS_SUNOS
	    getparams_sunos,
#endif
#ifdef HAVE_GETPARAMS_HAIKU
	    getparams_haiku,
#endif
	};
	int i;

	if (NITEMS(funcs) == 0) {
		up_err("don't know how to get disk parameters "
		    "on this platform");
		return (-1);
	}
	for (i = 0; i < NITEMS(funcs); i++)
		if ((funcs[i])(fd, params, name) == 0)
			return (0);

	return (-1);
}

#ifdef HAVE_LISTDEV_SYSCTL
static int
listdev_sysctl(FILE *stream)
{
	int mib[2];
	size_t size, i;
	char *names;

	mib[0] = CTL_HW;
	mib[1] = HW_DISKNAMES;
	size = 0;
	if (sysctl(mib, 2, NULL, &size, NULL, 0) < 0) {
		up_warn("failed to retrieve hw.disknames sysctl: %s",
		    strerror(errno));
		return (-1);
	}
	names = malloc(size);
	if (names == NULL) {
		perror("malloc");
		return (-1);
	}
	if (sysctl(mib, 2, names, &size, NULL, 0) < 0) {
		up_warn("failed to retrieve hw.disknames sysctl: %s",
		    strerror(errno));
		return (-1);
	}

	for (i = 0; i < size; i++)
		if (names[i] == ',')
			names[i] = ' ';
	fprintf(stdout, "%s\n", names);
	free(names);
	return (0);
}
#endif

#ifdef HAVE_GETPARAMS_DISKLABEL
static int
getparams_disklabel(int fd, struct disk_params *params, const char *name)
{
	struct disklabel dl;

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
			    name, strerror(errno));
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
getparams_freebsd(int fd, struct disk_params *params, const char *name)
{
	u_int ival;
	off_t oval;

	if (ioctl(fd, DIOCGSECTORSIZE, &ival) == 0)
		params->sectsize = ival;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get disk size for %s: %s",
		    name, strerror(errno));

	if (params->sectsize > 0 && ioctl(fd, DIOCGMEDIASIZE, &oval) == 0)
		params->size = oval / params->sectsize;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get sector size for %s: %s",
		    name, strerror(errno));

	if (ioctl(fd, DIOCGFWSECTORS, &ival) == 0)
		params->sects = ival;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get sectors per track for %s: %s",
		    name, strerror(errno));

	if (ioctl(fd, DIOCGFWHEADS, &ival) == 0)
		params->heads = ival;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get heads (tracks per cylinder) for %s: %s",
		    name, strerror(errno));

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

#ifdef HAVE_GETPARAMS_DARWIN
static int
getparams_darwin(int fd, struct disk_params *params, const char *name)
{
	uint32_t smallsize;
	uint64_t bigsize;

	if (ioctl(fd, DKIOCGETBLOCKSIZE, &smallsize) == 0)
		params->sectsize = smallsize;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get sector size for %s: %s",
		    name, strerror(errno));
	if (ioctl(fd, DKIOCGETBLOCKCOUNT, &bigsize) == 0)
		params->size = bigsize;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get block count for %s: %s",
		    name, strerror(errno));

	return (0);
}
#endif /* HAVE_GETPARAMS_DARWIN */

#ifdef HAVE_GETPARAMS_SUNOS
static int
getparams_sunos(int fd, struct disk_params *params, const char *name)
{
	struct dk_geom geom;

	/* XXX is there an ioctl or something to get sector size? */
	params->sectsize = NBPSCTR;

	if (ioctl(fd, DKIOCG_PHYGEOM, &geom) == 0 ||
	    ioctl(fd, DKIOCGGEOM, &geom) == 0) {
		params->cyls = geom.dkg_pcyl;
		params->heads = geom.dkg_nhead;
		params->sects = geom.dkg_nsect;
	} else if (UP_NOISY(QUIET))
		up_warn("failed to read disk geometry for %s: %s",
		    name, strerror(errno));

	return (0);
}
#endif /* HAVE_GETPARAMS_SUNOS */

#ifdef HAVE_GETPARAMS_HAIKU

static struct user_disk_device_data *
stat_disk_id(partition_id id, size_t size)
{
	struct user_disk_device_data *data, *new;
	status_t err;

	if (size == 0)
		size = sizeof(*data);
	data = NULL;

	for (;;) {
		new = realloc(data, size);
		if (new == NULL) {
			perror("malloc");
			free(data);
			return (NULL);
		}
		data = new;
		err = _kern_get_disk_device_data(id, true, data, size, &size);
		if (err == B_OK)
			return (data);
		else if (err != B_BUFFER_OVERFLOW) {
			errno = err;
			free(data);
			return (NULL);
		}
	}
}

static int
listdev_haiku(FILE *stream)
{
	int32 cookie;
	partition_id id;
	size_t size;
	struct user_disk_device_data *dev;

	cookie = 0;
	for (;;) {
		size = 0;
		id = _kern_get_next_disk_device_id(&cookie, &size);
		if (id < 0) {
			fputc('\n', stream);
			return (0);
		}
		dev = stat_disk_id(id, size);
		if (dev == NULL) {
			up_warn("failed to get device parameters for %ld: %s",
			    id, strerror(errno));
			return (-1);
		}
		if (dev->device_flags & B_DISK_DEVICE_HAS_MEDIA)
		    fprintf(stream, "%ld ", id);
		free(dev);
	}
}

static int
opendisk(const char *name, int flags, char *path, size_t pathlen, int ignored)
{
	long id;
	char *end;
	struct user_disk_device_data *data;

	end = NULL;
	id = strtol(name, &end, 10);
	if (end == NULL || *end != '\0' || id < 0) {
		errno = ENOENT;
		return (-1);
	}

	data = stat_disk_id(id, 0);
	if (data == NULL)
		return (-1);
	strlcpy(path, data->path, pathlen);
	free(data);

	return (open(path, flags));
}

static int
getparams_haiku(int fd, struct disk_params *params, const char *name)
{
	partition_id id;
	size_t size;
	struct user_disk_device_data *dev;

	size = 0;
	id = _kern_find_disk_device(name, &size);
	if (id < 0) {
		up_warn("not a disk device: %s", name);
		return (-1);
	}
	dev = stat_disk_id(id, size);
	if (dev == NULL) {
		up_warn("failed to get device parameters for %s: %s",
		    name, strerror(errno));
		return (-1);
	}
	if ((dev->device_flags & B_DISK_DEVICE_HAS_MEDIA) == 0) {
		up_warn("failed to get device parameters for %s: "
		    "no media loaded", name);
		return (-1);
	}

	params->sectsize = dev->device_partition_data.block_size;
	params->size = dev->device_partition_data.size / params->sectsize;
	free(dev);

	return (0);
}

#endif /* HAVE_GETPARAMS_HAIKU */
