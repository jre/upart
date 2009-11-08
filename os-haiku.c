#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

#ifdef __HAIKU__

/* XXX begin suckage */

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

/* Disk Device Manager syscalls */
partition_id	_kern_get_next_disk_device_id(int32 *, size_t *);
partition_id	_kern_find_disk_device(const char *, size_t *);
status_t	_kern_get_disk_device_data(partition_id, bool,
    struct user_disk_device_data *, size_t , size_t *);

/* XXX end suckage */

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

	cookie = 0;
	for (;;) {
		size = 0;
		id = _kern_get_next_disk_device_id(&cookie, &size);
		if (id < 0) {
			fputc('\n', stream);
			return (0);
		}
		dev = stat_disk_id(id, size);
		if (dev == NULL) {
			up_warn("failed to get device parameters for %ld: %s",
			    id, strerror(errno));
			return (-1);
		}
		if (dev->device_flags & B_DISK_DEVICE_HAS_MEDIA)
		    fprintf(stream, "%ld ", id);
		free(dev);
	}
}

int
os_opendisk_haiku(const char *name, int flags, char *path, size_t pathlen,
    int ignored)
{
	long id;
	char *end;
	struct user_disk_device_data *data;

	end = NULL;
	id = strtol(name, &end, 10);
	if (end == NULL || *end != '\0' || id < 0) {
		errno = ENOENT;
		return (-1);
	}

	data = stat_disk_id(id, 0);
	if (data == NULL)
		return (-1);
	strlcpy(path, data->path, pathlen);
	free(data);

	return (open(path, flags));
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
