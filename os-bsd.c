#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/disklabel.h>
#endif
#ifdef HAVE_SYS_DKIO_H
#include <sys/dkio.h>
#endif
#ifdef HAVE_SYS_DISK_H
#include <sys/disk.h>
#endif
#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#define UPART_DISK_PARAMS_ONLY
#include "disk.h"
#include "os-private.h"
#include "util.h"


#if defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTL)

char *
os_bsd_sysctl_alloc(int *name, unsigned int namelen)
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

#    if defined(CTL_HW) && defined(HW_DISKNAMES)
/*
  The hw.disknames sysctl works on OpenBSD and NetBSD. FreeBSD has the
  CTL_HW and HW_DISKNAMES macros but fails with ENOENT. Linux doesn't
  even have the macros.

  On OpenBSD 4.8 and earlier as well on all NetBSD versions, the
  string is formatted as a comma-separated list of short device names,
  ie: "wd0,cd0,sd0,sd1". Versions of OpenBSD newer than 4.8 append a
  colon and an optional disk UID to the device name, eg:
  "wd0:a1d0642fc8eba9f5,cd0:,sd0:3e5540338a09c700,sd1:".
*/
int
os_bsd_listdev_hw_disknames(os_list_callback_func func, void *arg)
{
	int mib[2] = { CTL_HW, HW_DISKNAMES };
	char *names, *begin, *end;
	int count;

	if ((names = os_bsd_sysctl_alloc(mib, 2)) == NULL) {
		if (errno == ENOMEM)
			perror("malloc");
		else if (errno != ENOENT)
			up_warn("failed to retrieve hw.disknames "
			    "sysctl: %s", strerror(errno));
		return (-1);
	}

	count = 0;
	for (begin = names; begin != NULL; begin = end) {
		if ((end = strchr(begin, ':')) != NULL)
			*(end++) = '\0';
		if ((end = strchr(end == NULL ? begin : end, ',')) != NULL)
			*(end++) = '\0';
		if (*begin != '\0') {
			count++;
			func(begin, arg);
		}
	}

	free(names);
	return (count);
}
#    endif /* CTL_HW && HW_DISKNAMES */

#    ifdef HAVE_SYSCTLNAMETOMIB
/*
  On FreeBSD, the kern.disks sysctl, available only through
  sysctlbyname() or sysctlnametomib(), returns a space-separated list
  of short device names.
 */
int
os_bsd_listdev_kern_disks(os_list_callback_func func, void *arg)
{
	char *names, *begin, *end;
	int mib[2], count;
	size_t len;

	len = 2;
	names = NULL;
	if (sysctlnametomib("kern.disks", mib, &len) < 0 ||
	    (names = os_bsd_sysctl_alloc(mib, len)) == NULL) {
		if (errno == ENOMEM)
			perror("malloc");
		else if (errno != ENOENT)
			up_warn("failed to retrieve kern.disks "
			    "sysctl: %s", strerror(errno));
		return (-1);
	}

	count = 0;
	for (begin = names; begin != NULL; begin = end) {
		if ((end = strchr(begin, ' ')) != NULL)
			*(end++) = '\0';
		if (*begin != '\0') {
			count++;
			func(begin, arg);
		}
	}

	free(names);
	return (count);
}
#    endif /* HAVE_SYSCTLNAMETOMIB */

int
os_listdev_sysctl(os_list_callback_func func, void *arg)
{
	int saved = 0;
	int ret = 0;

#if defined(CTL_HW) && defined(HW_DISKNAMES)
	if ((ret = os_bsd_listdev_hw_disknames(func, arg)) < 0)
		saved = errno;
#endif

#ifdef HAVE_SYSCTLNAMETOMIB
	if (ret <= 0)
		ret = os_bsd_listdev_kern_disks(func, arg);
	if (ret == 0 && saved != 0) {
		ret = -1;
		errno = saved;
	}
#endif

	return (ret);
}

#else /* HAVE_SYS_SYSCTL_H && HAVE_SYSCTL */
OS_GENERATE_LISTDEV_STUB(os_listdev_sysctl)
#endif /* HAVE_SYS_SYSCTL_H && HAVE_SYSCTL */


#if HAVE_OPENDISK
int
os_opendisk_opendisk(const char *name, int flags, char *buf, size_t buflen,
    int *ret)
{
	buf[0] = '\0';
	if ((*ret = opendisk(name, flags, buf, buflen, 0)) >= 0)
		return (1);
	return (-1);
}
#else /* HAVE_OPENDISK */
OS_GENERATE_OPENDISK_STUB(os_opendisk_opendisk)
#endif /* HAVE_OPENDISK */

#if HAVE_OPENDEV
int
os_opendisk_opendev(const char *name, int oflags, char *buf, size_t buflen,
    int *ret)
{
	char *realname = NULL;

	buf[0] = '\0';
	if ((*ret = opendev((char*)name, oflags, OPENDEV_PART, &realname)) >= 0) {
		strlcpy(buf, realname, buflen);
		return (1);
	}
	return (-1);
}
#else /* HAVE_OPENDEV */
OS_GENERATE_OPENDISK_STUB(os_opendisk_opendev)
#endif /* HAVE_OPENDEV */

#if defined(HAVE_SYS_DISKLABEL_H) && \
    (defined(DIOCGPDINFO) || defined(DIOCGDINFO))
int
os_getparams_disklabel(int fd, struct disk_params *params, const char *name)
{
	struct disklabel dl;

	errno = 0;
#    ifdef DIOCGPDINFO
	if (ioctl(fd, DIOCGPDINFO, &dl) < 0)
#    endif
#    ifdef DIOCGDINFO
	if (ioctl(fd, DIOCGDINFO, &dl) < 0)
#    endif
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
#    ifdef DL_GETDSIZE
	params->size = DL_GETDSIZE(&dl);
#    else
	params->size = dl.d_secperunit;
#    endif

	return (1);
}
#else
OS_GENERATE_GETPARAMS_STUB(os_getparams_disklabel)
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

	return (1);
}
#else
OS_GENERATE_GETPARAMS_STUB(os_getparams_freebsd)
#endif
