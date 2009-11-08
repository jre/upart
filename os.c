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

#ifdef HAVE_COREFOUNDATION_COREFOUNDATION_H
#include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef HAVE_IOKIT_IOKITLIB_H
#include <IOKit/IOKitLib.h>
#endif
#ifdef HAVE_IOKIT_IOBSD_H
#include <IOKit/IOBSD.h>
#endif
#ifdef HAVE_IOKIT_STORAGE_IOMEDIA_H
#include <IOKit/storage/IOMedia.h>
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
#include "os-solaris.h"
#include "util.h"

#ifdef O_LARGEFILE
#define OPENFLAGS(flags)        (O_LARGEFILE | (flags))
#else
#define OPENFLAGS(flags)        (flags)
#endif

#if defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTL) && \
    defined(CTL_HW) && defined(HW_DISKNAMES)
#define HAVE_LISTDEV_SYSCTL
static int	listdev_sysctl(FILE *);
#endif
#if defined(linux) || defined(__linux) || defined(__linux__)
#define HAVE_LISTDEV_LINUX
static int	listdev_linux(FILE *);
#endif
#if defined(HAVE_COREFOUNDATION_COREFOUNDATION_H) && \
    defined(HAVE_IOKIT_IOKITLIB_H) && \
    defined(kIOMediaClass) && defined(kIOBSDNameKey)
#define HAVE_LISTDEV_IOKIT
static int	listdev_iokit(FILE *);
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
#if defined(__HAIKU__)
#define HAVE_GETPARAMS_HAIKU
#define HAVE_LISTDEV_HAIKU
static int	listdev_haiku(FILE *);
static int	getparams_haiku(int, struct disk_params *, const char *);
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
#ifdef HAVE_LISTDEV_IOKIT
		listdev_iokit,
#endif
#ifdef HAVE_LISTDEV_LINUX
		listdev_linux,
#endif
#ifdef HAVE_LISTDEV_HAIKU
		listdev_haiku,
#endif
		OS_LISTDEV_SOLARIS,
#ifdef HAVE_LISTDEV_SYSCTL
		listdev_sysctl,
#endif
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
	    OS_GETPARAMS_SOLARIS,
#ifdef HAVE_GETPARAMS_HAIKU
	    getparams_haiku,
#endif
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
	fprintf(stream, "%s\n", names);
	free(names);
	return (0);
}
#endif

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

#ifdef HAVE_LISTDEV_IOKIT
static int
listdev_iokit(FILE *stream)
{
	CFMutableDictionaryRef dict;
	io_iterator_t iter;
	io_object_t serv;
	CFStringRef name;
	int once;

	if ((dict = IOServiceMatching(kIOMediaClass)) == NULL) {
		/* XXX */
		return (-1);
	}
	CFDictionarySetValue(dict, CFSTR(kIOMediaWholeKey), kCFBooleanTrue);

	if (IOServiceGetMatchingServices(kIOMasterPortDefault, dict, &iter) !=
	    KERN_SUCCESS) {
		/* XXX */
		return (-1);
	}
	once = 0;
	while ((serv = IOIteratorNext(iter))) {
		char *suchAFuckingPainInTheAss;
		CFIndex len;

		if ((name = IORegistryEntryCreateCFProperty(serv,
			    CFSTR(kIOBSDNameKey),
			    kCFAllocatorDefault,
			    0)) == NULL) {
			/* XXX */
			IOObjectRelease(serv);
			continue;
		}

		if (once)
			putc(' ', stream);
		len = CFStringGetLength(name) + 1;
		if ((suchAFuckingPainInTheAss = malloc(len)) == NULL) {
			perror("malloc");
			IOObjectRelease(serv);
			CFRelease(name);
			break;
		}
		if (!CFStringGetCString(name, suchAFuckingPainInTheAss, len,
			kCFStringEncodingASCII)) {
			/* XXX */
		} else {
			fputs(suchAFuckingPainInTheAss, stream);
			once = 1;
		}
		free(suchAFuckingPainInTheAss);
		IOObjectRelease(serv);
	}
	IOObjectRelease(iter);
	if (once)
		putc('\n', stream);

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
	    strchr(name, '/') != NULL)
		return (ret);

	if (strlcpy(buf, DEVPREFIX, buflen) >= buflen ||
	    strlcat(buf, name, buflen) >= buflen) {
		errno = ENOMEM;
		return (-1);
	}
	return (open(buf, flags));
}
