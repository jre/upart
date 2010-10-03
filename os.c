#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bsdtree.h"
#include "os.h"
#include "os-private.h"
#include "util.h"

#ifdef O_LARGEFILE
#define OPENFLAGS(flags)        (O_LARGEFILE | (flags))
#else
#define OPENFLAGS(flags)        (flags)
#endif

#define DEVPREFIX		"/dev/"

struct os_listdev_node {
	char *name;
	RB_ENTRY(os_listdev_node) entry;
};

RB_HEAD(os_listdev_map, os_listdev_node);

static int	opendisk_generic(const char *, int, char *, size_t, int);
static int	sortdisk(struct os_listdev_node *, struct os_listdev_node *);
static int	listdev_add(const char *, void *);
static int	listdev_print(struct os_listdev_map *, FILE *);
static void	listdev_free(struct os_listdev_map *);

RB_GENERATE_STATIC(os_listdev_map, os_listdev_node, entry, sortdisk)

int
os_list_devices(void *stream)
{
	static int (*funcs[])(int (*)(const char *, void *), void *) = {
		os_listdev_sysctl,
		os_listdev_iokit,
		os_listdev_linux,
		os_listdev_haiku,
		os_listdev_solaris,
	};
	struct os_listdev_map map;
	int i;

	RB_INIT(&map);
	for (i = 0; i < NITEMS(funcs); i++)
		if ((funcs[i])(listdev_add, &map) == 0)
			break;
	if (RB_EMPTY(&map)) {
		up_err("don't know how to list devices on this platform");
		return (-1);
	}

	listdev_print(&map, stream);
	listdev_free(&map);
	return (0);
}

int
up_os_opendisk(const char *name, const char **path)
{
	static int (*funcs[])(const char *, int, char *, size_t, int) = {
		os_opendisk_opendev,
		os_opendisk_opendisk,
		os_opendisk_haiku,
		os_opendisk_solaris,
		opendisk_generic,
	};
	static char buf[MAXPATHLEN];
	int flags, i, ret;

	*path = NULL;
	flags = OPENFLAGS(O_RDONLY);

	if (opts->plainfile)
		return open(name, flags);

	for (i = 0; i < NITEMS(funcs); i++) {
		ret = (funcs[i])(name, flags, buf, sizeof(buf), 0);
		if (ret == INT_MAX)
			continue;
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
	    os_getparams_disklabel,
	    os_getparams_freebsd,
	    os_getparams_linux,
	    os_getparams_darwin,
	    os_getparams_solaris,
	    os_getparams_haiku,
	};
	int once, i;

	for (i = 0; i < NITEMS(funcs); i++) {
		switch (funcs[i](fd, params, name)) {
		case 0:
			return (0);
		case INT_MAX:
			break;
		default:
			once = 1;
			break;
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

	if ((new = malloc(sizeof(*new))) == NULL) {
		perror("malloc");
		return (-1);
	}
	if ((new->name = strdup(name)) == NULL) {
		perror("malloc");
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
