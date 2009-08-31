#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "disk.h"
#include "img.h"
#include "map.h"
#include "os.h"
#include "util.h"

/* #define DEBUG_SECTOR_SAVE */

static int	fixparams(struct up_disk *, const struct disk_params *);
static int	fixparams_checkone(struct disk_params *disk);
static int	sectcmp(struct disk_sect *, struct disk_sect *);

RB_GENERATE_STATIC(disk_sect_map, disk_sect, link, sectcmp)

struct up_disk *
up_disk_open(const char *name)
{
    struct up_disk *disk;
 	struct img  *img;
    const char     *path;
    int             fd, res, plain;
    struct stat     sb;
    char           *newname, *newpath;

    /* open device */
    fd = up_os_opendisk(name, &path);
    if(0 > fd)
    {
        if(UP_NOISY(QUIET))
            up_err("failed to open %s for reading: %s",
		(path ? path : name), strerror(errno));
        return NULL;
    }

    /* is it a plain file? */
    if(0 == fstat(fd, &sb) && S_ISREG(sb.st_mode))
        plain = 1;
    else
        plain = 0;

    /* check if it's an image file */
    img = NULL;
    if(plain)
    {
        res = up_img_load(fd, name, &img);
	switch (res) {
	case -1:
		close(fd);
		return NULL;
	case 0:
		assert(NULL == img);
		break;
	case 1:
		assert(NULL != img);
		break;
	default:
		assert(!"bad return value");
		break;
	}
    }

    /* allocate disk struct and name and path strings */
    disk = calloc(1, sizeof *disk);
    if(!disk)
    {
        perror("malloc");
        close(fd);
        return NULL;
    }
    newname = strdup(name);
    if(!newname)
    {
        perror("malloc");
        close(fd);
        free(disk);
        return NULL;
    }
    newpath = strdup(path ? path : newname);
    if(!newpath)
    {
        perror("malloc");
        close(fd);
        free(disk);
        free(newname);
        return NULL;
    }

    /* populate disk struct */
    disk->ud_flag_plainfile = plain;
    RB_INIT(&disk->upd_sectsused);
    disk->upd_sectsused_count = 0;
    disk->ud_name = newname;
    disk->ud_path = newpath;
    if(NULL == img)
        disk->upd_fd = fd;
    else
    {
        close(fd);
        disk->upd_fd = -1;
        disk->upd_img = img;
    }

    return disk;
}

int
up_disk_setup(struct up_disk *disk, const struct disk_params *params)
{
	if (!UP_DISK_IS_IMG(disk))
		/* try to get drive parameters from OS */
		up_os_getparams(disk->upd_fd, disk);
	else
		/* try to get drive parameters from image */
		if(0 > up_img_getparams(disk->upd_img, &disk->ud_params))
			return (-1);

	/* try to fill in missing drive paramaters */
	if (fixparams(disk, params) < 0) {
		if (UP_NOISY(QUIET))
			up_err("failed to determine disk parameters for %s",
			    UP_DISK_PATH(disk));
		return (-1);
	}

	assert(disk->upd_buf == NULL);
	disk->upd_buf = malloc(UP_DISK_1SECT(disk));
	if(disk->upd_buf == NULL) {
		perror("malloc");
		return (-1);
	}

	disk->ud_flag_setup = 1;
	return (0);
}

int64_t
up_disk_read(const struct up_disk *disk, int64_t start, int64_t size,
    void *buf, size_t bufsize)
{
    ssize_t res;

    /* validate and fixup arguments */
    assert(disk->ud_flag_setup);
    assert(0 < bufsize && 0 < size && 0 <= start);
    bufsize -= bufsize % UP_DISK_1SECT(disk);
    if(bufsize / UP_DISK_1SECT(disk) < size)
        size = bufsize / UP_DISK_1SECT(disk);
    if(0 == bufsize || 0 == size)
        return 0;
    if(INT64_MAX / UP_DISK_1SECT(disk) < start)
    {
        errno = EINVAL;
        return -1;
    }

    /* if there's an image then read from it instead */
    if(UP_DISK_IS_IMG(disk))
        return up_img_read(disk->upd_img, start, size, buf);

    /* otherwise try to read from the disk */
    res = pread(disk->upd_fd, buf, size * UP_DISK_1SECT(disk),
                start * UP_DISK_1SECT(disk));
    if(0 > res)
    {
        if(UP_NOISY(QUIET))
            up_msg((opts->sloppyio ? UP_MSG_FWARN : UP_MSG_FERR),
                   "read from %s failed: %"PRIu64" sector(s) of %u "
                   "bytes at offset %"PRIu64": %s", UP_DISK_PATH(disk), size,
                   UP_DISK_1SECT(disk), start, strerror(errno));
        if(!opts->sloppyio)
            return -1;
        res = size * UP_DISK_1SECT(disk);
        memset(buf, 0, res);
    }

    return res / UP_DISK_1SECT(disk);
}

const void *
up_disk_getsect(const struct up_disk *disk, int64_t sect)
{
    assert(disk->upd_buf);
    if(1 > up_disk_read(disk, sect, 1, disk->upd_buf, UP_DISK_1SECT(disk)))
        return NULL;
    else
        return disk->upd_buf;
}

int
up_disk_check1sect(const struct up_disk *disk, int64_t sect)
{
    return up_disk_checksectrange(disk, sect, 1);
}

int
up_disk_checksectrange(const struct up_disk *disk, int64_t start, int64_t size)
{
	struct disk_sect key;

	assert(disk->ud_flag_setup);
	assert(size > 0);
	memset(&key, 0, sizeof key);
	key.first = start;
	key.last = start + size - 1;
	if (RB_FIND(disk_sect_map, &disk->upd_sectsused, &key) == NULL) {
#ifdef DEBUG_SECTOR_SAVE
		printf("check %"PRId64"+%"PRId64": free\n", start, size);
#endif
		return (0);
	} else {
#ifdef DEBUG_SECTOR_SAVE
		printf("check %"PRId64"+%"PRId64": used\n", start, size);
#endif
		return (1);
	}
}

const void *
up_disk_save1sect(struct up_disk *disk, int64_t sect, const struct up_map *ref,
    int tag)
{
    return up_disk_savesectrange(disk, sect, 1, ref, tag);
}

const void *
up_disk_savesectrange(struct up_disk *disk, int64_t first, int64_t size,
    const struct up_map *ref, int tag)
{
	struct disk_sect *new;

	/* allocate data structure */
	assert(disk->ud_flag_setup);
	assert(size > 0);
	new = calloc(1, sizeof *new);
	if (new == NULL) {
		perror("malloc");
		return (NULL);
	}
	new->data = NULL;
	new->first = first;
	new->last = first + size - 1;
	new->ref = ref;
	new->tag = tag;
	new->data = calloc(size, UP_DISK_1SECT(disk));
	if (new->data == NULL) {
		perror("malloc");
		free(new);
		return (NULL);
	}

	/* try to read the sectors from disk */
	if (up_disk_read(disk, first, size, new->data,
	    size * UP_DISK_1SECT(disk)) != size) {
		free(new->data);
		free(new);
		return (NULL);
	}

	/* insert it in the tree if the sectors aren't marked as used */
	if (RB_INSERT(disk_sect_map, &disk->upd_sectsused, new)) {
#ifdef DEBUG_SECTOR_SAVE
		printf("failed to mark %"PRId64"+%"PRId64" with %p, "
		    "already used\n", first, size, ref);
#endif
		free(new->data);
		free(new);
		return (NULL);
	}
#ifdef DEBUG_SECTOR_SAVE
	printf("mark %"PRId64"+%"PRId64" with %p\n", first, size, ref);
#endif
	disk->upd_sectsused_count += size;

	/* return the data */
	return (new->data);
}

void
up_disk_sectsunref(struct up_disk *disk, const void *ref)
{
	struct disk_sect *ii, *inc;

	assert(disk->ud_flag_setup);
	for (ii = RB_MIN(disk_sect_map, &disk->upd_sectsused); ii; ii = inc) {
		inc = RB_NEXT(disk_sect_map, &disk->upd_sectsused, ii);
		if (ref == ii->ref) {
#ifdef DEBUG_SECTOR_SAVE
			printf("unmark %"PRId64"+%"PRId64" with %p\n",
			    ii->first, ii->last - ii->first + 1, ii->ref);
#endif
			RB_REMOVE(disk_sect_map, &disk->upd_sectsused, ii);
			disk->upd_sectsused_count -= ii->last - ii->first + 1;
			assert(0 <= disk->upd_sectsused_count);
			free(ii->data);
			free(ii);
		}
	}
}

void
up_disk_close(struct up_disk *disk)
{
    if(!disk)
        return;
    up_map_freeall(disk);
    assert(RB_EMPTY(&disk->upd_sectsused));
    assert(0 == disk->upd_sectsused_count);
    if(0 <= disk->upd_fd)
        close(disk->upd_fd);
    up_img_free(disk->upd_img);
    free(disk->upd_buf);
    free(disk->ud_name);
    free(disk->ud_path);
    free(disk);
}

static int
fixparams(struct up_disk *disk, const struct disk_params *params)
{
	struct stat sb;

	/* command-line options override any other values */
	if (params->sectsize > 0)
		disk->ud_params.sectsize = params->sectsize;
	if (params->cyls > 0)
		disk->ud_params.cyls = params->cyls;
	if (params->heads > 0)
        	disk->ud_params.heads = params->heads;
	if (params->sects > 0)
        	disk->ud_params.sects = params->sects;

	/* sector size defaults to 512 */
	if (disk->ud_params.sectsize <= 0)
        	disk->ud_params.sectsize = 512;

	/* we can get the total size for a plain file */
	if (disk->ud_params.size <= 0 &&
	    !UP_DISK_IS_IMG(disk) &&
	    UP_DISK_IS_FILE(disk) &&
	    stat(UP_DISK_PATH(disk), &sb) == 0)
		disk->ud_params.size = sb.st_size / disk->ud_params.sectsize;

	/* is that good enough? */
	if (fixparams_checkone(&disk->ud_params) == 0)
		return (0);

	/* apparently not, try defaulting heads and sectors to 255 and 63 */
	if (disk->ud_params.heads <= 0)
        	disk->ud_params.heads = 255;
	if (disk->ud_params.sects <= 0)
		disk->ud_params.sects = 63;

	/* ok, is it good enough now? */
	if (fixparams_checkone(&disk->ud_params) == 0)
		return (0);

	/* guess not */
	return (-1);
}

static int
fixparams_checkone(struct disk_params *params)
{
	/* if we have all 4 then we're good */
	if (params->cyls > 0 && params->heads > 0 &&
	    params->sects > 0 && params->size > 0)
		return (0);

	/* guess cyls from other three */
	if (params->cyls <= 0 && params->heads > 0 &&
	    params->sects > 0 && params->size > 0)
	{
		params->cyls = params->size / params->sects / params->heads;
		return (0);
	}

	/* guess heads from other three */
	if (params->cyls > 0 && params->heads <= 0 &&
	    params->sects > 0 && params->size > 0)
	{
		params->heads = params->size / params->sects / params->cyls;
		return (0);
	}

	/* guess sects from other three */
	if (params->cyls > 0 && params->heads > 0 &&
	    params->sects <= 0 && params->size > 0)
	{
		params->sects = params->size / params->heads / params->cyls;
		return (0);
	}

	/* guess size from other three */
	if (params->cyls > 0 && params->heads > 0 &&
	    params->sects > 0 && params->size <= 0)
	{
		params->size = params->sects * params->heads * params->cyls;
		return (0);
	}

	/* can only guess with one missing param */
	return (-1);
}

void
up_disk_print(const struct up_disk *disk, void *_stream)
{
    FILE *              stream = _stream;
    const char *        unit;
    float               size;

    assert(disk->ud_flag_setup);
    size = up_fmtsize(UP_DISK_SIZEBYTES(disk), &unit);
    if(UP_NOISY(NORMAL))
        fprintf(stream, "%s: %.*f%s (%"PRId64" sectors of %d bytes)\n",
                UP_DISK_PATH(disk), UP_BESTDECIMAL(size), size, unit,
                UP_DISK_SIZESECTS(disk), UP_DISK_1SECT(disk));
    if(UP_NOISY(EXTRA))
        fprintf(stream,
                "    label:               %s\n"
                "    device path:         %s\n"
                "    sector size:         %d\n"
                "    total sectors:       %"PRId64"\n"
                "    total cylinders:     %"PRId64" (cylinders)\n"
                "    tracks per cylinder: %"PRId64" (heads)\n"
                "    sectors per track:   %"PRId64" (sectors)\n"
                "\n", UP_DISK_LABEL(disk),
                UP_DISK_PATH(disk), UP_DISK_1SECT(disk), UP_DISK_SIZESECTS(disk),
                UP_DISK_CYLS(disk), UP_DISK_HEADS(disk), UP_DISK_SPT(disk));
    if(UP_NOISY(NORMAL))
        fputc('\n', stream);
}

void
up_disk_dump(const struct up_disk *disk, void *stream)
{
	struct disk_sect *node;

	assert(disk->ud_flag_setup);
	for (node = RB_MIN(disk_sect_map, &disk->upd_sectsused); node;
	    node = RB_NEXT(disk_sect_map, &disk->upd_sectsused, node))
		up_map_dumpsect(node->ref, stream, node->first,
		    node->last - node->first + 1, node->data, node->tag);
}

void
up_disk_sectsiter(const struct up_disk *disk,
                  up_disk_iterfunc_t func, void *arg)
{
	struct disk_sect *node;

	assert(disk->ud_flag_setup);
	for (node = RB_MIN(disk_sect_map, &disk->upd_sectsused); node;
	    node = RB_NEXT(disk_sect_map, &disk->upd_sectsused, node))
		if (func(disk, node, arg) == 0)
			break;
}

const struct disk_sect *
up_disk_nthsect(const struct up_disk *disk, int off)
{
	struct disk_sect *node;

	assert(disk->ud_flag_setup);
	assert(off >= 0);
	for (node = RB_MIN(disk_sect_map, &disk->upd_sectsused); node;
	    node = RB_NEXT(disk_sect_map, &disk->upd_sectsused, node))
		if (off-- == 0)
			return (node);
	return (NULL);
}

static int
sectcmp(struct disk_sect *left, struct disk_sect *right)
{
	if (left->last < right->first) {
		/*
		printf("cmp (%"PRId64"+%"PRId64") < (%"PRId64"+%"PRId64")\n",
		       left->first, left->last - left->first + 1,
		       right->first, right->last - right->first + 1);
		*/
		return (-1);
	} else if (left->first > right->last) {
		/*
		printf("cmp (%"PRId64"+%"PRId64") > (%"PRId64"+%"PRId64")\n",
		       left->first, left->last - left->first + 1,
		       right->first, right->last - right->first + 1);
		*/
		return (1);
	} else {
		/*
		printf("cmp (%"PRId64"+%"PRId64") = (%"PRId64"+%"PRId64")\n",
		       left->first, left->last - left->first + 1,
		       right->first, right->last - right->first + 1);
		*/
		return (0);
	}
}
