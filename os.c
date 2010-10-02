#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MINIMAL_NAMESPACE_POLLUTION_PLEASE
#include "disk.h"
#include "os.h"
#include "os-bsd.h"
#include "os-darwin.h"
#include "os-haiku.h"
#include "os-linux.h"
#include "os-solaris.h"
#include "util.h"

#ifdef O_LARGEFILE
#define OPENFLAGS(flags)        (O_LARGEFILE | (flags))
#else
#define OPENFLAGS(flags)        (flags)
#endif

static int	listdev_print(const char *, void *);
static int	opendisk_generic(const char *, int, char *, size_t, int);

#define DEVPREFIX		"/dev/"

struct listdev_print {
	FILE *stream;
	int once;
};

int
os_list_devices(void *stream)
{
	static int (*funcs[])(int (*)(const char *, void *), void *) = {
		OS_LISTDEV_IOKIT,
		OS_LISTDEV_LINUX,
		OS_LISTDEV_HAIKU,
		OS_LISTDEV_SOLARIS,
		/* sysctl compiles but doesn't work on linux and darwin */
		/* XXX the order these are called in is no longer as important */
		OS_LISTDEV_SYSCTL,
	};
	struct listdev_print arg;
	int i;

	arg.stream = stream;
	arg.once = 0;
	for (i = 0; i < NITEMS(funcs); i++)
		if (funcs[i] != NULL &&
		    (funcs[i])(listdev_print, &arg) == 0)
			break;
	if (arg.once) {
		putc('\n', stream);
		return (0);
	}

	up_err("don't know how to list devices on this platform");

	return (-1);
}

int
listdev_print(const char *name, void *_arg)
{
	struct listdev_print *arg = _arg;

	if (arg->once)
		putc(' ', arg->stream);
	else
		arg->once = 1;
	fputs(name, arg->stream);

	return (1);
}

int
up_os_opendisk(const char *name, const char **path)
{
	static int (*funcs[])(const char *, int, char *, size_t, int) = {
		OS_OPENDISK_OPENDEV,
		OS_OPENDISK_OPENDISK,
		OS_OPENDISK_HAIKU,
		OS_OPENDISK_SOLARIS,
		opendisk_generic,
	};
	static char buf[MAXPATHLEN];
	int flags, i, ret;

	*path = NULL;
	flags = OPENFLAGS(O_RDONLY);

	if (opts->plainfile)
		return open(name, flags);

	for (i = 0; i < NITEMS(funcs); i++) {
		if (funcs[i] == NULL)
			continue;
		ret = (funcs[i])(name, flags, buf, sizeof(buf), 0);
		if (ret >= 0 && buf[0] != '\0')
			*path = buf;
		return (ret);
	}

	return (-1);
}

int
up_os_getparams(int fd, struct disk_params *params, const char *name)
{
	static int (*funcs[])(int, struct disk_params *, const char *) = {
	    OS_GETPARAMS_FREEBSD,
	    /* disklabel compiles but doesn't work on freebsd */
	    OS_GETPARAMS_DISKLABEL,
	    OS_GETPARAMS_LINUX,
	    OS_GETPARAMS_DARWIN,
	    OS_GETPARAMS_SOLARIS,
	    OS_GETPARAMS_HAIKU,
	};
	int once, i;

	once = 0;
	for (i = 0; i < NITEMS(funcs); i++) {
		if (funcs[i] != NULL) {
			once = 1;
			if ((funcs[i])(fd, params, name) == 0)
				return (0);
		}
	}
	if (!once)
		up_err("don't know how to get disk parameters "
		    "on this platform");

	return (-1);
}

static int
opendisk_generic(const char *name, int flags, char *buf, size_t buflen,
    int cooked)
{
	int ret;

	if (strlcpy(buf, name, buflen) >= buflen) {
		errno = ENOMEM;
		return (-1);
	}
	if ((ret = open(name, flags)) >= 0 ||
	    errno != ENOENT ||
	    strchr(name, '/') != NULL)
		return (ret);

	if (strlcpy(buf, DEVPREFIX, buflen) >= buflen ||
	    strlcat(buf, name, buflen) >= buflen) {
		errno = ENOMEM;
		return (-1);
	}
	return (open(buf, flags));
}
