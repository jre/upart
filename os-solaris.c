#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_DKIO_H
#include <sys/dkio.h>
#endif
#ifdef HAVE_SYS_VTOC_H
#include <sys/vtoc.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#define UPART_DISK_PARAMS_ONLY
#include "disk.h"
#include "os-private.h"
#include "util.h"

#if defined(sun) || defined(__sun) || defined(__sun__)

#error "Solaris is too broken to run this program, sorry."

#define DEVPATH_COOKED		"/dev/dsk/"
#define DEVPATH_RAW		"/dev/rdsk/"
#define WHOLE_PART		"s2"

int
os_listdev_solaris(os_list_callback_func func, void *arg)
{
	static const char hex[] = "0123456789ABCDEF";
	static const char num[] = "0123456789";
	struct dirent *ent;
	DIR *dir;
	int fd;

	/* XXX this sucks */

	if ((dir = opendir(DEVPATH_COOKED)) == NULL) {
		up_warn("failed to list %s: %s",
		    DEVPATH_COOKED, strerror(errno));
		return (-1);
	}

	while ((ent = readdir(dir)) != NULL) {
		size_t off, inc;
		if (ent->d_name[0] != 'c' ||
		    (inc = strspn(ent->d_name + 1, num)) == 0)
			continue;
		off = 1 + inc;
		if (ent->d_name[off] == 't') {
			if ((inc = strspn(ent->d_name + off + 1, hex)) == 0)
				continue;
			off += 1 + inc;
		}
		if (ent->d_name[off] != 'd' ||
		    (inc = strspn(ent->d_name + off + 1, num)) == 0)
			continue;
		off += 1 + inc;
		if (strcmp(ent->d_name + off, WHOLE_PART) != 0)
			continue;
		if (os_opendisk_solaris(ent->d_name, O_RDONLY, NULL, 0, &fd) < 0) {
			if (errno == ENOENT)
				continue;
		} else {
			close(fd);
		}
		ent->d_name[off] = '\0';
		func(ent->d_name, arg);
		ent->d_name[off] = 's';
	}
	closedir(dir);

	return (1);
}

int
os_opendisk_solaris(const char *name, int flags, char *buf, size_t buflen,
    int *ret)
{
	static char mybuf[MAXPATHLEN];

	if (NULL == buf) {
		buf = mybuf;
		buflen = sizeof(mybuf);
	}

	if (strlcpy(buf, name, buflen) >= buflen)
		goto trunc;
	if ((*ret = open(name, flags)) >= 0)
		return (1);
	if (errno != ENOENT)
		return (-1);

	if(strlcat(buf, WHOLE_PART, buflen) >= buflen)
		goto trunc;
	if ((*ret = open(buf, flags)) >= 0)
		return (1);
	if (errno != ENOENT || strchr(name, '/') != NULL)

	if (strlcpy(buf, DEVPATH_RAW, buflen) >= buflen ||
	    strlcat(buf, name, buflen) >= buflen)
		goto trunc;
	if ((*ret = open(buf, flags)) >= 0)
		return (1);
	if (errno != ENOENT)
		return (-1);

	if(strlcat(buf, WHOLE_PART, buflen) >= buflen)
		goto trunc;
	if ((*ret = open(buf, flags)) >= 0)
		return (1);
	return (-1);
trunc:
	errno = ENOMEM;
	return (-1);
}

#else
OS_GENERATE_LISTDEV_STUB(os_listdev_solaris)
OS_GENERATE_OPENDISK_STUB(os_opendisk_solaris)
#endif

#if defined(HAVE_SYS_DKIO_H) && \
    (defined(DKIOCGGEOM) || defined(DKIOCGMEDIAINFO))
int
os_getparams_solaris(int fd, struct disk_params *params, const char *name)
{
#ifdef DKIOCGGEOM
	struct dk_geom geom;
#endif
#ifdef DKIOCGMEDIAINFO
	struct dk_minfo minfo;
#endif
#if defined(HAVE_SYS_VTOC_H) && defined(DKIOCGVTOC)
	struct vtoc vtoc;
#endif

	params->sectsize = NBPSCTR;

#ifdef DKIOCGMEDIAINFO
	if (ioctl(fd, DKIOCGMEDIAINFO, &minfo) == 0) {
		params->sectsize = minfo.dki_lbsize;
		params->size = minfo.dki_capacity;
	}
#endif

#ifdef DKIOCGGEOM
	if (ioctl(fd, DKIOCG_PHYGEOM, &geom) == 0 ||
	    ioctl(fd, DKIOCGGEOM, &geom) == 0) {
		params->cyls = geom.dkg_pcyl;
		params->heads = geom.dkg_nhead;
		params->sects = geom.dkg_nsect;
	}
#endif

#if defined(HAVE_SYS_VTOC_H) && defined(DKIOCGVTOC)
	if (ioctl(fd, DKIOCGVTOC, &vtoc) == 0) {
		int i;
		params->sectsize = vtoc.v_sectorsz;
		for (i = 0; i < vtoc.v_nparts; i++) {
			if (vtoc.v_part[i].p_tag == V_BACKUP) {
				params->size = vtoc.v_part[i].p_size;
				break;
			}
		}
	}
#endif

	return (1);
}
#else
OS_GENERATE_GETPARAMS_STUB(os_getparams_solaris)
#endif
