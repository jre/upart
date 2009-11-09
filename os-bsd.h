#ifndef HDR_UPART_OS_BSD
#define HDR_UPART_OS_BSD

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#if defined(HAVE_SYS_DISKLABEL_H) && !defined(UPART_INCLUDED_SYS_DISKLABEL)
#define UPART_INCLUDED_SYS_DISKLABEL
#include <sys/disklabel.h>
#endif
#ifdef HAVE_SYS_DKIO_H
#include <sys/dkio.h>
#endif
#if defined(HAVE_SYS_DISK_H) && !defined(UPART_INCLUDED_SYS_DISK)
#define UPART_INCLUDED_SYS_DISK
#include <sys/disk.h>
#endif
#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#if defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTL) && \
    defined(CTL_HW) && defined(HW_DISKNAMES)
#define OS_LISTDEV_SYSCTL	(os_listdev_sysctl)
int	os_listdev_sysctl(FILE *);
#else
#define OS_LISTDEV_SYSCTL	(NULL)
#endif

#ifdef HAVE_OPENDISK
#define OS_OPENDISK_OPENDISK	(os_opendisk_opendisk)
int	os_opendisk_opendisk(const char *, int, char *, size_t, int);
#else
#define OS_OPENDISK_OPENDISK	(0)
#endif

#if defined(HAVE_SYS_DISKLABEL_H) && \
    (defined(DIOCGPDINFO) || defined(DIOCGDINFO))
#define OS_GETPARAMS_DISKLABEL	(os_getparams_disklabel)
int	os_getparams_disklabel(int, struct disk_params *, const char *);
#else
#define OS_GETPARAMS_DISKLABEL	(NULL)
#endif

#if defined(HAVE_SYS_DISK_H) && defined(DIOCGSECTORSIZE)
#define OS_GETPARAMS_FREEBSD	(os_getparams_freebsd)
int	os_getparams_freebsd(int, struct disk_params *, const char *);
#else
#define OS_GETPARAMS_FREEBSD	(NULL)
#endif

#endif /*  HDR_UPART_OS_BSD */
