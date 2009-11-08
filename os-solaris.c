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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MINIMAL_NAMESPACE_POLLUTION_PLEASE
#include "disk.h"
#include "os-solaris.h"
#include "util.h"

#ifdef OS_HAVE_SOLARIS

int
os_listdev_solaris(FILE *stream)
{
	return (-1);
}

int
os_opendisk_solaris(const char *name, int flags, char *buf, size_t buflen,
    int iscooked)
{
	return (-1);
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
