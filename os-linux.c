#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_HDREG_H
#include <linux/hdreg.h>
#endif

#include <ctype.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#define UPART_DISK_PARAMS_ONLY
#include "disk.h"
#include "os-private.h"
#include "util.h"

#if defined(linux) || defined(__linux) || defined(__linux__)

/* skip over ramdisks in device listing */
#define SKIP_DEVNO(major, minor)	((major) == 1)

static int	scanf_at(int, const char *, const char *, ...);

static int
os_linux_listdev_sysfs(os_list_callback_func func, void *arg)
{
	char const blockpath[] = "/sys/block";
	int ret, cnt, devfd;
	struct dirent *ent;
	long major, minor;
	int64_t size;
	DIR *dir;

	if ((dir = opendir(blockpath)) == NULL) {
		if (errno == ENOENT)
			return (0);
		up_warn("failed to list /sys/block: %s", strerror(errno));
		return (-1);
	}

	ret = 0;
	while ((ent = readdir(dir)) != NULL) {
		const char *name = ent->d_name;
		if ((devfd = openat(dirfd(dir), name, O_RDONLY)) == -1) {
			if (errno == ENOENT)
				continue;
			if (UP_NOISY(QUIET))
				up_err("failed to open %s/%s: %s",
				    blockpath, name, strerror(errno));
			return (-1);
		}

		/* ignore devices with a size of 0 */
		size = -1;
		if ((cnt = scanf_at(devfd, "size", "%"PRId64, &size)) == -1) {
			if (UP_NOISY(QUIET))
				up_err("failed to read %s/%s/size: %s",
				    blockpath, name, strerror(errno));
			goto error;
		}
		if (cnt != 1 || size <= 0)
			goto skip;

		/* ignore devices with an uninteresting major/minor number */
		major = minor = -1;
		if ((cnt = scanf_at(devfd, "dev", "%ld:%ld", &major, &minor)) == -1) {
			if (UP_NOISY(QUIET))
				up_err("failed to read %s/%s/dev: %s",
				    blockpath, name, strerror(errno));
			goto error;
		}
		if (cnt != 2 || SKIP_DEVNO(major, minor))
			goto skip;

		ret = 1;
		func(name, arg);
	skip:
		close(devfd);
	}
	closedir(dir);
	return (ret);

error:
	close(devfd);
	return (-1);
}

static int
scanf_at(int dirfd, const char *name, const char *format, ...)
{
	int saved, fd, ret;
	va_list ap;
	FILE *fh;

	if ((fd = openat(dirfd, name, O_RDONLY)) == -1) {
		if (errno == ENOENT)
			return (0);
		return (-1);
	}
	if ((fh = fdopen(fd, "r")) == NULL) {
		saved = errno;
		close(fd);
		errno = saved;
		return (-1);
	}
	va_start(ap, format);
	ret = vfscanf(fh, format, ap);
	if (ferror(fh))
		ret = -1;
	va_end(ap);
	saved = errno;
	fclose(fh);
	errno = saved;
	return (ret);
}

static int
os_linux_listdev_devfs(os_list_callback_func func, void *arg)
{
	static const char letters[] = "abcdefghijklmnopqrstuvwxyz";
	struct dirent *ent;
	DIR *dir;

	/* XXX ugh, there has to be a better way to do this */

	if ((dir = opendir("/dev")) == NULL) {
		up_warn("failed to list /dev: %s", strerror(errno));
		return (-1);
	}

	while ((ent = readdir(dir)) != NULL) {
		size_t end;
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
		if ((end = strspn(ent->d_name + 2, letters)) > 0 &&
		    ent->d_name[2+end] == '\0')
			func(ent->d_name, arg);
	}
	closedir(dir);

	return (1);
}

int
os_listdev_linux(os_list_callback_func func, void *arg)
{
	int saved = 0;
	int ret = 0;

	if ((ret = os_linux_listdev_sysfs(func, arg)) < 0)
		saved = errno;

	if (ret <= 0)
		ret = os_linux_listdev_devfs(func, arg);
	if (ret == 0 && saved != 0) {
		ret = -1;
		errno = saved;
	}

	return (ret);
}

#else
OS_GENERATE_LISTDEV_STUB(os_listdev_linux)
#endif

#if defined(HAVE_LINUX_FS_H) || defined(HAVE_LINUX_HDREG_H)
int
os_getparams_linux(int fd, struct disk_params *params, const char *name)
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

	return (1);
}
#else
OS_GENERATE_GETPARAMS_STUB(os_getparams_linux)
#endif
