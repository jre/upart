#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <sys/ioctl.h>
#ifdef HAVE_SYS_DKIO_H
#include <sys/dkio.h>
#endif
#ifdef HAVE_SYS_VTOC_H
#include <sys/vtoc.h>
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MINIMAL_NAMESPACE_POLLUTION_PLEASE
#include "disk.h"
#include "os-solaris.h"
#include "util.h"

#define DEVPATH_COOKED		"/dev/dsk/"
#define DEVPATH_RAW		"/dev/rdsk/"
#define WHOLE_PART		"s2"

#ifdef OS_HAVE_SOLARIS

int
os_listdev_solaris(FILE *stream)
{
	static const char hex[] = "0123456789ABCDEF";
	static const char num[] = "0123456789";
	struct dirent *ent;
	int once, fd;
	DIR *dir;

	/* XXX this sucks */

	if ((dir = opendir(DEVPATH_COOKED)) == NULL) {
		up_warn("failed to list %s: %s",
		    DEVPATH_COOKED, strerror(errno));
		return (-1);
	}

	once = 0;
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
		fd = os_opendisk_solaris(ent->d_name, O_RDONLY, NULL, 0, 0);
		if (fd >= 0)
			close(fd);
		else if (errno == ENOENT)
			continue;
		if (once)
			putc(' ', stream);
		once = 1;
		ent->d_name[off] = '\0';
		fputs(ent->d_name, stream);
		ent->d_name[off] = 's';
	}
	closedir(dir);
	if (once)
		putc('\n', stream);

	return (0);
}

int
os_opendisk_solaris(const char *name, int flags, char *buf, size_t buflen,
    int iscooked)
{
	static char mybuf[MAXPATHLEN];
	const char *dir;
	int ret;

	if (NULL == buf) {
		buf = mybuf;
		buflen = sizeof(mybuf);
	}

	strlcpy(buf, name, buflen);
	if ((ret = open(name, flags)) >= 0 || errno != ENOENT)
		return (ret);

	if(strlcat(buf, WHOLE_PART, buflen) >= buflen) {
		errno = ENOMEM;
		return (-1);
	}
	if ((ret = open(buf, flags)) >= 0 || errno != ENOENT)
		return (ret);

	if (strchr(name, '/') != NULL)
		return (ret);

	dir = (iscooked ? DEVPATH_COOKED : DEVPATH_RAW);
	if (strlcpy(buf, dir, buflen) >= buflen ||
	    strlcat(buf, name, buflen) >= buflen) {
		errno = ENOMEM;
		return (-1);
	}
	if ((ret = open(buf, flags)) >= 0 || errno != ENOENT)
		return (ret);

	if(strlcat(buf, WHOLE_PART, buflen) >= buflen) {
		errno = ENOMEM;
		return (-1);
	}
	return (open(buf, flags));
}

#endif /* OS_HAVE_SOLARIS */

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

	return (0);
}
#endif /* HAVE_SYS_DKIO_H && DKIOCGGEOM */
