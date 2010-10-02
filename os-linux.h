#ifndef HDR_UPART_OS_LINUX
#define HDR_UPART_OS_LINUX

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_HDREG_H
#include <linux/hdreg.h>
#endif

#if defined(linux) || defined(__linux) || defined(__linux__)
#define OS_HAVE_LINUX
#define OS_LISTDEV_LINUX	(os_listdev_linux)
int	os_listdev_linux(int (*)(const char *, void *), void *);
#else
#define OS_LISTDEV_LINUX	(NULL)
#endif

#if defined(HAVE_LINUX_FS_H) || defined(HAVE_LINUX_HDREG_H)
#define OS_GETPARAMS_LINUX	(os_getparams_linux)
int	os_getparams_linux(int, struct disk_params *, const char *);
#else
#define OS_GETPARAMS_LINUX	(NULL)
#endif

#endif /* HDR_UPART_OS_LINUX */
