/* 
 * Copyright (c) 2010 Joshua R. Elsasser.
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "os.h"
#include "os-private.h"
#include "util.h"

#ifdef OS_TYPE_WINDOWS

#include <io.h>
#include <winioctl.h>

#define DEV_NAME	"PhysicalDrive"
#define DEV_PREFIX	"\\\\.\\"

int
os_listdev_windows(os_list_callback_func func, void *arg)
{
	char *buf, *new;
	size_t size;
	DWORD res;

	size = 513;
	buf = NULL;
	do {
		size = (size - 1) * 2 + 1;
		if ((new = realloc(buf, size)) == NULL) {
			free(buf);
			fputs("failed to allocate memory\n", stderr);
			return (-1);
		}
		buf = new;

		if ((res = QueryDosDevice(NULL, buf, size - 1)) == 0 &&
		    GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			/* XXX clobber error */
			free(buf);
			return (-1);
		}
	} while (res <= 0 || res >= size - 1);

	buf[size-1] = '\0';
	for (new = buf;
	     new < buf + size && *new != '\0';
	     new = strchr(new, '\0') + 1) {
		if (strncmp(new, DEV_NAME, sizeof(DEV_NAME) - 1) == 0) {
			/* XXX return value */
			func(new, arg);
		}
	}

	free(buf);
	return (1);
}

int
os_opendisk_windows(const char *name, int flags, char *buf, size_t buflen,
    HANDLE *ret)
{
	char *end;
	long id;

	if (strlcpy(buf, name, buflen) >= buflen) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}

	*ret = CreateFile(buf, flags, FILE_SHARE_READ | FILE_SHARE_WRITE,
	    NULL, OPEN_EXISTING, 0, NULL);
	if (*ret != INVALID_HANDLE_VALUE)
		return (1);
	else if (GetLastError() != ERROR_FILE_NOT_FOUND ||
	    strchr(name, '\\') != NULL ||
	    strchr(name, '//') != NULL)
		return (-1);

	end = NULL;
	if ((id = strtol(name, &end, 10)) >= 0 &&
	    end != NULL && *end == '\0')
		snprintf(buf, buflen, "%s%s%ld",
		    DEV_PREFIX, DEV_NAME, id);
	else if (name[0] == '\\')
		strlcpy(buf, name, buflen);
	else
		snprintf(buf, buflen, "%s%s", DEV_PREFIX, name);

	*ret = CreateFile(buf, flags, FILE_SHARE_READ | FILE_SHARE_WRITE,
	    NULL, OPEN_EXISTING, 0, NULL);
	if (*ret != INVALID_HANDLE_VALUE)
		return (1);
	return (-1);
}

int
os_getparams_windows(HANDLE hdl, struct disk_params *params, const char *name)
{
	DISK_GEOMETRY geom;
	DWORD whoops;

	memset(&geom, 0, sizeof(geom));

	if (!DeviceIoControl(hdl, IOCTL_DISK_GET_DRIVE_GEOMETRY,
		NULL, 0, &geom, sizeof(geom), &whoops, NULL))
		return (-1);

	params->cyls = geom.Cylinders.QuadPart;
	params->heads = geom.TracksPerCylinder;
	params->sects = geom.SectorsPerTrack;
	params->sectsize = geom.BytesPerSector;

	return (1);
}

ssize_t
os_dev_read(os_device_handle ehand, void *buf, size_t size, int64_t off)
{
	OVERLAPPED pos;
	DWORD res;

	memset(&pos, 0, sizeof(pos));
	pos.Offset = off % 0xffffffff;
	pos.OffsetHigh = (off >> 32) % 0x7fffffff;
	res = 0;
	if (!ReadFile(OS_HANDLE_IN(ehand), buf, size, &res, &pos))
		return (-1);
	return (res);
}

int
os_dev_close(os_device_handle ehand)
{
	if (CloseHandle(OS_HANDLE_IN(ehand)) == 0)
		return (-1);
	return (0);
}

int64_t
os_file_size(FILE *file)
{
	return (_filelengthi64(_fileno(file)));
}

int
os_handle_type(os_device_handle ehand, enum disk_type *type)
{
	BY_HANDLE_FILE_INFORMATION info;

	/* XXX there has to be a better way than this */
	if (GetFileInformationByHandle(OS_HANDLE_IN(ehand), &info))
		*type = DT_FILE;
	else
		*type = DT_DEVICE;
	return (1);
}

int
os_open_flags(const char *str)
{
	int flags = 0;

	while (*str != '\0') {
		switch (*str) {
		case 'r':
			flags |= GENERIC_READ;
			break;
		default:
			assert(!"bad format character");
			break;
		}
		str++;
	}

	return (flags);
}

os_error
os_lasterr(void)
{
	return (GetLastError());
}

void
os_setlasterr(os_error num)
{
	SetLastError((DWORD)num);
}

const char *
os_lasterrstr(void)
{
	return (os_errstr(GetLastError()));
}

const char *
os_errstr(os_error num)
{
	static char *buf = NULL;

	if (buf != NULL)
		LocalFree(buf);
	buf = NULL;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, (DWORD)num, 0, (char*)&buf, 0, NULL) == 0)
		return (NULL);
	return (buf);
}

#else
OS_GENERATE_LISTDEV_STUB(os_listdev_windows)
OS_GENERATE_OPENDISK_STUB(os_opendisk_windows)
OS_GENERATE_GETPARAMS_STUB(os_getparams_windows)
#endif
