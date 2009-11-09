#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define MINIMAL_NAMESPACE_POLLUTION_PLEASE
#include "disk.h"
#include "os-darwin.h"
#include "util.h"

#ifdef OS_HAVE_IOKIT
#include <mach/mach_error.h>
int
os_listdev_iokit(FILE *stream)
{
	CFMutableDictionaryRef dict;
	io_iterator_t iter;
	kern_return_t ret;
	io_object_t serv;
	CFStringRef name;
	int once;

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
	once = 0;
	while ((serv = IOIteratorNext(iter))) {
		char *suchAFuckingPainInTheAss;
		CFIndex len;

		if ((name = IORegistryEntryCreateCFProperty(serv,
			    CFSTR(kIOBSDNameKey),
			    kCFAllocatorDefault,
			    0)) == NULL) {
			/* XXX does this set errno? */
			up_warn("failed to get service property: %s",
			    strerror(errno));
			IOObjectRelease(serv);
			continue;
		}

		if (once)
			putc(' ', stream);
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
			fputs(suchAFuckingPainInTheAss, stream);
			once = 1;
		}
		free(suchAFuckingPainInTheAss);
		IOObjectRelease(serv);
	}
	IOObjectRelease(iter);
	if (once)
		putc('\n', stream);

	return (0);
}
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

	return (0);
}
#endif /* HAVE_GETPARAMS_DARWIN */
