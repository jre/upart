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

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define UPART_DISK_PARAMS_ONLY
#include "disk.h"
#include "os-private.h"
#include "util.h"

#if defined(linux) || defined(__linux) || defined(__linux__)
int
os_listdev_linux(os_list_callback_func func, void *arg)
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
