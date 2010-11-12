#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdtree.h"
#include "disk.h"
#include "os.h"
#include "os-private.h"
#include "util.h"

struct os_listdev_node {
	char *name;
	RB_ENTRY(os_listdev_node) entry;
};

RB_HEAD(os_listdev_map, os_listdev_node);

static int	sortdisk(struct os_listdev_node *, struct os_listdev_node *);
static int	listdev_add(const char *, void *);
static int	listdev_print(struct os_listdev_map *, FILE *);
static void	listdev_free(struct os_listdev_map *);

RB_GENERATE_STATIC(os_listdev_map, os_listdev_node, entry, sortdisk)

int
os_list_devices(FILE *stream)
{
	static os_list_func funcs[] = {
		os_listdev_windows,
		os_listdev_sysctl,
		os_listdev_iokit,
		os_listdev_linux,
		os_listdev_haiku,
		os_listdev_solaris,
	};
	struct os_listdev_map map;
	int i;

	RB_INIT(&map);
	for (i = 0; i < NITEMS(funcs); i++) {
		switch ((funcs[i])(listdev_add, &map)) {
		case -1:
			if (UP_NOISY(QUIET))
				up_err("failed to list devices: %s",
				    os_lasterrstr());
			break;
		case 0:
			break;
		case 1:
			if (!RB_EMPTY(&map))
				goto done;
			break;
		default:
			assert(!"bad return value");
			break;
		}
	}

done:
	if (RB_EMPTY(&map)) {
		if (UP_NOISY(QUIET))
			up_err("don't know how to list devices "
			    "on this platform");
		return (-1);
	}
	listdev_print(&map, stream);
	listdev_free(&map);
	return (0);
}

enum disk_type
os_dev_open(const char *name, const char **path, os_device_handle *ret)
{
	static os_open_func funcs[] = {
		os_opendisk_windows,
		os_opendisk_opendev,
		os_opendisk_opendisk,
		os_opendisk_haiku,
		os_opendisk_solaris,
		os_opendisk_unix,
	};
	static char buf[MAXPATHLEN];
	enum disk_type type;
	os_handle hand;
	int i, flags;

	assert(sizeof(os_device_handle) >= sizeof(os_handle));

	*path = NULL;
	flags = os_open_flags("r");

	if (opts->plainfile)
		return (DT_FILE);

	for (i = 0; i < NITEMS(funcs); i++) {
		switch((funcs[i])(name, flags, buf, sizeof(buf), &hand)) {
		case -1:
			return (DT_UNKNOWN);
		case 0:
			continue;
		case 1:
			break;
		default:
			assert(!"bad return value");
			break;
		}

		if (os_handle_type(OS_HANDLE_OUT(hand), &type) < 0) {
			os_error saved = os_lasterr();
			os_dev_close(OS_HANDLE_OUT(hand));
			os_setlasterr(saved);
			return (DT_UNKNOWN);
		}

		switch(type) {
		case DT_FILE:
			os_dev_close(OS_HANDLE_OUT(hand));
			break;
		case DT_DEVICE:
			if (buf[0] != '\0')
				*path = buf;
			*ret = OS_HANDLE_OUT(hand);
			break;
		default:
			assert(!"bad return value");
			break;
		}
		return (type);
	}

	assert(!"device open not implemented on this platform?!?");
	return (DT_UNKNOWN);
}

int
os_dev_params(os_device_handle ehand, struct disk_params *params, const char *name)
{
	static os_params_func funcs[] = {
		os_getparams_windows,
		os_getparams_freebsd,
		/*
		  os_getparams_disklabel() compiles on FreeBSD but
		  fails to work, so make sure os_getparams_freebsd()
		  is tried first.
		*/
		os_getparams_disklabel,
		os_getparams_linux,
		os_getparams_darwin,
		os_getparams_solaris,
		os_getparams_haiku,
	};
	os_handle hand;
	int i;

	assert(sizeof(os_device_handle) >= sizeof(os_handle));
	hand = OS_HANDLE_IN(ehand);

	for (i = 0; i < NITEMS(funcs); i++) {
		switch (funcs[i](hand, params, name)) {
		case -1:
			return (-1);
		case 0:
			break;
		case 1:
			return (0);
		default:
			assert(!"bad return value");
			break;
		}
	}
	if (UP_NOISY(QUIET))
		up_err("don't know how to get disk parameters "
		    "on this platform");

	return (-1);
}

static int
sortdisk(struct os_listdev_node *a, struct os_listdev_node *b)
{
	return (strcmp(a->name, b->name));
}

static int
listdev_add(const char *name, void *arg)
{
	struct os_listdev_map *map = arg;
	struct os_listdev_node key, *new;

	key.name = (char *)name;
	if (RB_FIND(os_listdev_map, map, &key) != NULL)
		return (0);

	if ((new = xalloc(1, sizeof(*new), 0)) == NULL)
		return (-1);
	if ((new->name = xstrdup(name, 0)) == NULL) {
		free(new);
		return (-1);
	}
	RB_INSERT(os_listdev_map, map, new);

	return (0);
}

static void
listdev_free(struct os_listdev_map *map)
{
	struct os_listdev_node *dead, *next;

	for (dead = RB_MIN(os_listdev_map, map); dead != NULL; dead = next) {
		next = RB_NEXT(os_listdev_map, map, dead);
		RB_REMOVE(os_listdev_map, map, dead);
		free(dead->name);
		free(dead);
	}
}

static int
listdev_print(struct os_listdev_map *map, FILE *stream)
{
	struct os_listdev_node *node;
	int once;

	once = 0;
	RB_FOREACH(node, os_listdev_map, map) {
		if (once)
			putc(' ', stream);
		else
			once = 1;
		fputs(node->name, stream);
	}
	if (once)
		putc('\n', stream);

	return (0);
}
