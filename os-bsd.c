#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#define MINIMAL_NAMESPACE_POLLUTION_PLEASE
#include "disk.h"
#include "os-bsd.h"
#include "util.h"

#if defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTL)

#if (defined(CTL_HW) && defined(HW_DISKNAMES)) || defined(HAVE_SYSCTLNAMETOMIB)
static char *
sysctl_disk_names(int *name, unsigned int namelen)
{
	size_t size;
	char *str;

	size = 0;
	if (sysctl(name, namelen, NULL, &size, NULL, 0) < 0 ||
	    (str = malloc(size)) == NULL)
		return (NULL);
	if (sysctl(name, namelen, str, &size, NULL, 0) < 0) {
		int saved = errno;
		free(str);
		errno = saved;
		return (NULL);
	}
	return (str);
}
#endif /*  (CTL_HW && HW_DISKNAMES) || HAVE_SYSCTLNAMETOMIB */

int
os_listdev_sysctl(FILE *stream)
{
	char *names;

	names = NULL;

#if defined(CTL_HW) && defined(HW_DISKNAMES)
	{
		int mib[2] = { CTL_HW, HW_DISKNAMES };
		size_t i;
		names = sysctl_disk_names(mib, 2);
		if (names == NULL && errno != ENOENT) {
			if (errno == ENOMEM)
				perror("malloc");
			else
				up_warn("failed to retrieve hw.disknames "
				    "sysctl: %s", strerror(errno));
			return (-1);
		}
		if (names != NULL)
			for (i = 0; names[i] != '\0'; i++)
				if (names[i] == ',')
					names[i] = ' ';
	}
#endif /* CTL_HW && HW_DISKNAMES */

#ifdef HAVE_SYSCTLNAMETOMIB
	if (names == NULL) {
		int mib[2];
		size_t i = 2;
		if (sysctlnametomib("kern.disks", mib, &i) < 0 ||
		    (names = sysctl_disk_names(mib, i)) == NULL) {
			if (errno == ENOMEM)
				perror("malloc");
			else
				up_warn("failed to retrieve kern.disks "
				    "sysctl: %s", strerror(errno));
			return (-1);
		}
	}
#endif

	if (names == NULL)
		return (-1);

	fprintf(stream, "%s\n", names);
	free(names);
	return (0);
}
#endif /* HAVE_SYSCTL_H && HAVE_SYSCTL */

#if HAVE_OPENDISK
int
os_opendisk_opendisk(const char *name, int flags, char *buf, size_t buflen,
    int cooked)
{
	buf[0] = '\0';
	return (opendisk(name, flags, buf, buflen, cooked));
}
#endif /* HAVE_OPENDISK */

#if HAVE_OPENDEV
int
os_opendisk_opendev(const char *name, int oflags, char *buf, size_t buflen,
    int cooked)
{
	char *realname = NULL;
	int dflags = OPENDEV_PART;
	int ret;

	buf[0] = '\0';
	if (cooked)
		dflags |= OPENDEV_BLCK;
	if ((ret = opendev((char*)name, oflags, dflags, &realname)) >= 0)
		strlcpy(buf, realname, buflen);
	return (ret);
}
#endif /* HAVE_OPENDEV */

#if defined(HAVE_SYS_DISKLABEL_H) && \
    (defined(DIOCGPDINFO) || defined(DIOCGDINFO))
int
os_getparams_disklabel(int fd, struct disk_params *params, const char *name)
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
#endif

#if defined(HAVE_SYS_DISK_H) && defined(DIOCGSECTORSIZE)
int
os_getparams_freebsd(int fd, struct disk_params *params, const char *name)
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
