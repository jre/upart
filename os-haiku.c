#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __HAIKU__

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <DiskDeviceDefs.h>
#include <OS.h>

#define MINIMAL_NAMESPACE_POLLUTION_PLEASE
#include "disk.h"
#include "os-haiku.h"
#include "util.h"

/* XXX copy/pasting from private headers sucks */

/* begin paste from headers/private/system/ddm_userland_interface_defs.h */

// userland partition representation
struct user_partition_data {
	partition_id	id;
	off_t		offset;
	off_t		size;
	off_t		content_size;
	uint32		block_size;
	uint32		status;
	uint32		flags;
	dev_t		volume;
	int32		index;
	int32		change_counter;
	disk_system_id	disk_system;
	char*		name;
	char*		content_name;
	char*		type;
	char*		content_type;
	char*		parameters;
	char*		content_parameters;
	void*		user_data;
	int32		child_count;
	struct user_partition_data*	children[1];
};

// userland disk device representation
struct user_disk_device_data {
	uint32		device_flags;
	char*		path;
	struct user_partition_data	device_partition_data;
};

/* end paste from headers/private/system/ddm_userland_interface_defs.h */

/* begin paste from headers/private/system/syscalls.h */

/* Disk Device Manager syscalls */
partition_id	_kern_get_next_disk_device_id(int32 *, size_t *);
partition_id	_kern_find_disk_device(const char *, size_t *);
status_t	_kern_get_disk_device_data(partition_id, bool,
    struct user_disk_device_data *, size_t , size_t *);

/* end paste from headers/private/system/syscalls.h */

#define DEV_PREFIX		"/dev/disk/"
#define DEV_SUFFIX		"/raw"

static struct user_disk_device_data *
stat_disk_id(partition_id id, size_t size)
{
	struct user_disk_device_data *data, *new;
	status_t err;

	if (size == 0)
		size = sizeof(*data);
	data = NULL;

	for (;;) {
		new = realloc(data, size);
		if (new == NULL) {
			perror("malloc");
			free(data);
			return (NULL);
		}
		data = new;
		err = _kern_get_disk_device_data(id, true, data, size, &size);
		if (err == B_OK)
			return (data);
		else if (err != B_BUFFER_OVERFLOW) {
			errno = err;
			free(data);
			return (NULL);
		}
	}
}

int
os_listdev_haiku(FILE *stream)
{
	int32 cookie;
	partition_id id;
	size_t size;
	struct user_disk_device_data *dev;
	int once;

	once = 0;
	cookie = 0;
	for (;;) {
		size = 0;
		id = _kern_get_next_disk_device_id(&cookie, &size);
		if (id < 0) {
			if (once)
				putc('\n', stream);
			return (0);
		}
		dev = stat_disk_id(id, size);
		if (dev == NULL) {
			up_warn("failed to get device parameters for %ld: %s",
			    id, strerror(errno));
			return (-1);
		}
		if (dev->device_flags & B_DISK_DEVICE_HAS_MEDIA) {
			char *start, *end;
			size_t plen;

			if (once)
				putc(' ', stream);
			once = 1;
			plen = strlen(DEV_PREFIX);
			start = dev->path + plen;
			if (strncmp(dev->path, DEV_PREFIX, plen) != 0 ||
			    (end = strrchr(start, '/')) == NULL ||
			    end == start ||
			    strcmp(end, DEV_SUFFIX) != 0)
				fprintf(stream, "%ld", id);
			else {
				*end = '\0';
				fputs(start, stream);
				*end = '/';
			}
		}
		free(dev);
	}
}

static int
maybe_open_disk(const char *path, int flags, int *ret)
{
	struct stat sb;
	int fd;


	*ret = -1;
	fd = open(path, flags);
	if (fd < 0) {
		if (errno == ENOENT || errno == ENOTDIR)
			return (0);
		return (1);
	}
	if (fstat(fd, &sb) != 0) {
		int save = errno;
		close(fd);
		errno = save;
		return (1);
	}
	if (!S_ISBLK(sb.st_mode) && !S_ISCHR(sb.st_mode)) {
		close(fd);
		return (0);
	}
	*ret = fd;
	return (1);
}

int
os_opendisk_haiku(const char *name, int flags, char *buf, size_t buflen,
    int ignored)
{
	long id;
	char *end;
	int ret;

	end = NULL;
	id = strtol(name, &end, 10);
	if (end != NULL && *end == '\0' && id >= 0) {
		struct user_disk_device_data *data;
		data = stat_disk_id(id, 0);
		if (data == NULL)
			return (-1);
		if (strlcpy(buf, data->path, buflen) >= buflen) {
			errno = ENOMEM;
			free(data);
			return (-1);
		}
		free(data);
		return (open(buf, flags));
	}

	if (strlcpy(buf, name, buflen) >= buflen)
		goto trunc;
	if (maybe_open_disk(name, flags, &ret))
		return (ret);

	if (strlcat(buf, DEV_SUFFIX, buflen) >= buflen)
		goto trunc;
	if (maybe_open_disk(buf, flags, &ret))
		return (ret);

	if (strlcpy(buf, DEV_PREFIX, buflen) >= buflen ||
	    strlcat(buf, name, buflen) >= buflen)
		goto trunc;
	if (maybe_open_disk(buf, flags, &ret))
		return (ret);

	if (strlcat(buf, DEV_SUFFIX, buflen) >= buflen)
		goto trunc;
	return (open(buf, flags));

trunc:
	errno = ENOMEM;
	return (-1);
}

int
os_getparams_haiku(int fd, struct disk_params *params, const char *name)
{
	partition_id id;
	size_t size;
	struct user_disk_device_data *dev;

	size = 0;
	id = _kern_find_disk_device(name, &size);
	if (id < 0) {
		up_warn("not a disk device: %s", name);
		return (-1);
	}
	dev = stat_disk_id(id, size);
	if (dev == NULL) {
		up_warn("failed to get device parameters for %s: %s",
		    name, strerror(errno));
		return (-1);
	}
	if ((dev->device_flags & B_DISK_DEVICE_HAS_MEDIA) == 0) {
		up_warn("failed to get device parameters for %s: "
		    "no media loaded", name);
		return (-1);
	}

	params->sectsize = dev->device_partition_data.block_size;
	params->size = dev->device_partition_data.size / params->sectsize;
	free(dev);

	return (0);
}

#endif /* __HAIKU__ */
