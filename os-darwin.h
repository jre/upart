#ifndef HDR_UPART_OS_DARWIN
#define HDR_UPART_OS_DARWIN

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined(HAVE_SYS_DISK_H) && !defined(UPART_INCLUDED_SYS_DISK)
#define UPART_INCLUDED_SYS_DISK
#include <sys/disk.h>
#endif
#ifdef HAVE_COREFOUNDATION_COREFOUNDATION_H
#include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef HAVE_IOKIT_IOKITLIB_H
#include <IOKit/IOKitLib.h>
#endif
#ifdef HAVE_IOKIT_IOBSD_H
#include <IOKit/IOBSD.h>
#endif
#ifdef HAVE_IOKIT_STORAGE_IOMEDIA_H
#include <IOKit/storage/IOMedia.h>
#endif

#if defined(HAVE_COREFOUNDATION_COREFOUNDATION_H) && \
    defined(HAVE_IOKIT_IOKITLIB_H) && \
    defined(kIOMediaClass) && defined(kIOBSDNameKey)
#define OS_HAVE_IOKIT
#define OS_LISTDEV_IOKIT	(os_listdev_iokit)
int	os_listdev_iokit(int (*)(const char *, void *), void *);
#else
#define OS_LISTDEV_IOKIT	(NULL)
#endif

#if defined(HAVE_SYS_DISK_H) && defined(DKIOCGETBLOCKSIZE)
#define OS_GETPARAMS_DARWIN	(os_getparams_darwin)
int	os_getparams_darwin(int, struct disk_params *, const char *);
#else
#define OS_GETPARAMS_DARWIN	(NULL)
#endif

#endif /*  HDR_UPART_OS_DARWIN */
