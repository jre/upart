#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/ioctl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MINIMAL_NAMESPACE_POLLUTION_PLEASE
#include "disk.h"
#include "os-bsd.h"
#include "util.h"

#if defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTL) && \
    defined(CTL_HW) && defined(HW_DISKNAMES)
int
os_listdev_sysctl(FILE *stream)
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
