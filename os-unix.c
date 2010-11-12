#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#include <assert.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "disk.h"
#include "os.h"
#include "os-private.h"
#include "util.h"

#ifdef OS_TYPE_UNIX

#ifdef O_LARGEFILE
#define OPENFLAGS(flags)        (O_LARGEFILE | (flags))
#else
#define OPENFLAGS(flags)        (flags)
#endif

#define DEVPREFIX		"/dev/"

int
os_opendisk_unix(const char *name, int flags, char *buf, size_t buflen,
    int *ret)
{
	if (strlcpy(buf, name, buflen) >= buflen) {
		errno = ENOMEM;
		return (-1);
	}
	if ((*ret = open(name, flags)) >= 0)
		return (1);
	if (errno != ENOENT || strchr(name, '/') != NULL)
		return (-1);

	if (strlcpy(buf, DEVPREFIX, buflen) >= buflen ||
	    strlcat(buf, name, buflen) >= buflen) {
		errno = ENOMEM;
		return (-1);
	}
	if ((*ret = open(buf, flags)) >= 0)
		return (1);
	return (-1);
}

ssize_t
os_dev_read(os_device_handle ehand, void *buf, size_t size, off_t off)
{
	return (pread(OS_HANDLE_IN(ehand), buf, size, off));
}

int
os_dev_close(os_device_handle ehand)
{
	return (close(OS_HANDLE_IN(ehand)));
}

int64_t
os_file_size(FILE *file)
{
	struct stat sb;

	if (fstat(fileno(file), &sb) < 0)
		return (-1);
	return (sb.st_size);
}

int
os_handle_type(os_device_handle ehand, enum disk_type *type)
{
	struct stat sb;

	if (fstat(OS_HANDLE_IN(ehand), &sb) < 0)
		return (-1);

	if (S_ISCHR(sb.st_mode) || S_ISBLK(sb.st_mode)) 
		*type = DT_DEVICE;
	else if (S_ISREG(sb.st_mode))
		*type = DT_FILE;
	else {
		errno = EINVAL;
		return (-1);
	}

	return (0);
}

int
os_open_flags(const char *str)
{
	int flags = 0;

	while (*str != '\0') {
		switch (*str) {
		case 'r':
			flags |= O_RDONLY;
			break;
		default:
			assert(!"bad format character");
			break;
		}
		str++;
	}

	return (OPENFLAGS(flags));
}

os_error
os_lasterr(void)
{
	return (errno);
}

void
os_setlasterr(os_error num)
{
	errno = num;
}

const char *
os_lasterrstr(void)
{
	return (strerror(errno));
}

const char *
os_errstr(os_error num)
{
	return (strerror(num));
}

#else
OS_GENERATE_OPENDISK_STUB(os_opendisk_unix)
#endif
