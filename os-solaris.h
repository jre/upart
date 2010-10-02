#ifndef HDR_UPART_OS_SOLARIS
#define HDR_UPART_OS_SOLARIS

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_DKIO_H
#include <sys/dkio.h>
#endif

#if defined(sun) || defined(__sun) || defined(__sun__)
#error "Solaris is too broken to run this program, sorry."
#define OS_HAVE_SOLARIS
#define OS_LISTDEV_SOLARIS	(os_listdev_solaris)
#define OS_OPENDISK_SOLARIS	(os_opendisk_solaris)
int	os_listdev_solaris(int (*)(const char *, void *), void *);
int	os_opendisk_solaris(const char *, int, char *, size_t, int);
#else
#define OS_LISTDEV_SOLARIS	(NULL)
#define OS_OPENDISK_SOLARIS	(NULL)
#endif /* sun */

#if defined(HAVE_SYS_DKIO_H) && \
    (defined(DKIOCGGEOM) || defined(DKIOCGMEDIAINFO))
#define OS_GETPARAMS_SOLARIS	(os_getparams_solaris)
int	os_getparams_solaris(int, struct disk_params *, const char *);
#else
#define OS_GETPARAMS_SOLARIS	(NULL)
#endif

#endif /*  HDR_UPART_OS_SOLARIS */
