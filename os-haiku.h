#ifndef HDR_UPART_OS_HAIKU
#define HDR_UPART_OS_HAIKU

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __HAIKU__
#define OS_LISTDEV_HAIKU	(os_listdev_haiku)
#define OS_OPENDISK_HAIKU	(os_opendisk_haiku)
#define OS_GETPARAMS_HAIKU	(os_getparams_haiku)
int	os_listdev_haiku(FILE *);
int	os_opendisk_haiku(const char *, int, char *, size_t, int);
int	os_getparams_haiku(int, struct disk_params *, const char *);
#else
#define OS_LISTDEV_HAIKU	(NULL)
#define OS_OPENDISK_HAIKU	(NULL)
#define OS_GETPARAMS_HAIKU	(NULL)
#endif /* __HAIKU__ */

#endif /*  HDR_UPART_OS_HAIKU */
