/* 
 * Copyright (c) 2009-2010 Joshua R. Elsasser.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined(HAVE_SYS_DISK_H)
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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <stdio.h>

#define UPART_DISK_PARAMS_ONLY
#include "disk.h"
#include "os-private.h"
#include "util.h"

#if defined(HAVE_COREFOUNDATION_COREFOUNDATION_H) && \
    defined(HAVE_IOKIT_IOKITLIB_H) && \
    defined(kIOMediaClass) && defined(kIOBSDNameKey)

#include <mach/mach_error.h>

int
os_listdev_iokit(os_list_callback_func func, void *arg)
{
	CFMutableDictionaryRef dict;
	io_iterator_t iter;
	kern_return_t ret;
	io_object_t serv;
	CFStringRef name;

	if ((dict = IOServiceMatching(kIOMediaClass)) == NULL) {
		/* XXX does this set errno? */
		up_warn("failed to create matching dictionary: %s",
		    strerror(errno));
		return (-1);
	}
	CFDictionarySetValue(dict, CFSTR(kIOMediaWholeKey), kCFBooleanTrue);

	ret = IOServiceGetMatchingServices(kIOMasterPortDefault, dict, &iter);
	if (ret != KERN_SUCCESS) {
		up_warn("failed to get maching IOKit services: %s\n",
		    mach_error_string(ret));
		return (-1);
	}

	while ((serv = IOIteratorNext(iter))) {
		char *suchAFuckingPainInTheAss;
		CFIndex len;

		name = IORegistryEntryCreateCFProperty(serv, 
		    CFSTR(kIOBSDNameKey), kCFAllocatorDefault, 0);
		if (name == NULL) {
			/* XXX does this set errno? */
			up_warn("failed to get service property: %s",
			    strerror(errno));
			IOObjectRelease(serv);
			continue;
		}

		len = CFStringGetLength(name) + 1;
		if ((suchAFuckingPainInTheAss = malloc(len)) == NULL) {
			perror("malloc");
			IOObjectRelease(serv);
			CFRelease(name);
			break;
		}
		if (!CFStringGetCString(name, suchAFuckingPainInTheAss, len,
			kCFStringEncodingASCII)) {
			/* XXX does this set errno? */
			up_warn("failed to convert string: %s",
			    strerror(errno));
		} else {
			func(suchAFuckingPainInTheAss, arg);
		}
		free(suchAFuckingPainInTheAss);
		IOObjectRelease(serv);
		CFRelease(name);
	}
	IOObjectRelease(iter);

	return (1);
}

#else
OS_GENERATE_LISTDEV_STUB(os_listdev_iokit)
#endif

#if defined(HAVE_SYS_DISK_H) && defined(DKIOCGETBLOCKSIZE)
int
os_getparams_darwin(int fd, struct disk_params *params, const char *name)
{
	uint32_t smallsize;
	uint64_t bigsize;

	if (ioctl(fd, DKIOCGETBLOCKSIZE, &smallsize) == 0)
		params->sectsize = smallsize;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get sector size for %s: %s",
		    name, strerror(errno));
	if (ioctl(fd, DKIOCGETBLOCKCOUNT, &bigsize) == 0)
		params->size = bigsize;
	else if (UP_NOISY(QUIET))
		up_warn("failed to get block count for %s: %s",
		    name, strerror(errno));

	return (1);
}
#else
OS_GENERATE_GETPARAMS_STUB(os_getparams_darwin)
#endif
