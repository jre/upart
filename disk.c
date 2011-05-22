#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "img.h"
#include "map.h"
#include "os.h"
#include "util.h"

/* #define DEBUG_SECTOR_SAVE */

static enum disk_type open_thing(const char *, union disk_handle *,
    const char **);
static int	fixparams(struct disk *, const struct disk_params *);
static int	fixparams_checkone(struct disk_params *disk);
static int	sectcmp(struct disk_sect *, struct disk_sect *);

RB_GENERATE_STATIC(disk_sect_map, disk_sect, link, sectcmp)

static struct disk *st_curdisk;

struct disk *
current_disk(void)
{
	return (st_curdisk);
}

struct disk *
up_disk_open(const char *name)
{
	struct disk *disk;
	const char *path;

 	/* allocate disk struct */
	if ((disk = xalloc(1, sizeof(*disk), 0)) == NULL)
		return (NULL);
	if ((disk->name = xstrdup(name, 0)) == NULL) {
		free(disk);
		return (NULL);
	}
	disk->path = NULL;
	memset(&disk->params, 0, sizeof(disk->params));
	disk->setup_done = 0;
	disk->buf = NULL;
	disk->maps = NULL;
	RB_INIT(&disk->sectsused);
	disk->sectsused_count = 0;

	/* open device */
	disk->type = open_thing(name, &disk->handle, &path);
	if (disk->type == DT_UNKNOWN ||
	    (disk->path = xstrdup((path ? path : name), 0)) == NULL) {
		up_disk_close(disk);
		return (NULL);
	}

	st_curdisk = disk;
	return (disk);
}

int
up_disk_setup(struct disk *disk, const struct disk_params *params)
{
	switch (disk->type) {
	case DT_IMAGE:
		/* get saved drive parameters from image */
		up_img_getparams(disk->handle.img, &disk->params);
		strlcpy(disk->desc, up_img_getlabel(disk->handle.img),
		    sizeof(disk->desc));
		break;
	case DT_DEVICE:
		/* try to get drive parameters via OS-dependent interfaces */
		if (os_dev_params(disk->handle.dev, &disk->params,
			UP_DISK_PATH(disk)) < 0) {
			if (UP_NOISY(QUIET))
				up_err("failed to determine disk parameters "
				    "for %s: %s",
				    UP_DISK_PATH(disk), os_lasterrstr());
			return (-1);
		}
		os_dev_desc(disk->handle.dev, disk->desc, sizeof(disk->desc),
		    UP_DISK_PATH(disk));
		break;
	case DT_FILE:
		break;
	default:
		assert(!"unknown disk type");
		break;
	}

	/* try to fill in missing drive paramaters */
	if (fixparams(disk, params) < 0) {
		if (UP_NOISY(QUIET))
			up_err("failed to determine disk parameters for %s",
			    UP_DISK_PATH(disk));
		return (-1);
	}

	assert(disk->buf == NULL);
	if ((disk->buf = xalloc(1, UP_DISK_1SECT(disk), 0)) == NULL)
		return (-1);

	disk->setup_done = 1;
	return (0);
}

int64_t
up_disk_read(const struct disk *disk, int64_t sect_off, int64_t sect_count,
    void *buf, size_t bufsize)
{
	size_t byte_count;
	int64_t byte_off;
	ssize_t res;

	/* validate and fixup arguments */
	assert(disk->setup_done);
	assert(0 < bufsize && 0 < sect_count && 0 <= sect_off);
	bufsize -= bufsize % UP_DISK_1SECT(disk);
	if (bufsize / UP_DISK_1SECT(disk) < sect_count)
		sect_count = bufsize / UP_DISK_1SECT(disk);
	if (0 == bufsize || 0 == sect_count)
		return (0);
	if (INT64_MAX / UP_DISK_1SECT(disk) < sect_off ||
	    SIZE_MAX / UP_DISK_1SECT(disk) < sect_count) {
		up_err("failed to read from disk: address out of range");
		return (-1);
	}
	byte_off = sect_off * UP_DISK_1SECT(disk);
	byte_count = sect_count * UP_DISK_1SECT(disk);

	switch (disk->type) {
	case DT_IMAGE:
		/* if there's an image then read from it instead */
		return (up_img_read(disk->handle.img, sect_off, sect_count,
			buf));
	case DT_FILE:
		/* plain files use stdio */
		if (fseeko(disk->handle.file, byte_off, SEEK_SET) != 0)
			res = (ferror(disk->handle.file) ? -1 : 0);
		else {
			res = fread(buf, 1, byte_count, disk->handle.file);
			if (res == 0 && ferror(disk->handle.file))
				res = -1;
		}
		break;
	case DT_DEVICE:
		/* otherwise try to read from the device */
		res = os_dev_read(disk->handle.dev, buf, byte_count, byte_off);
		break;
	default:
		assert(!"unknown disk type");
		/* goddamnit gcc just shut up already */
		res = 0;
		break;
	}

	if (res < 0) {
		if (UP_NOISY(QUIET))
			up_msg((opts->sloppyio ? UP_MSG_FWARN : UP_MSG_FERR),
			    "read from %s failed: %"PRIu64" sector(s) of %u "
			    "bytes at offset %"PRIu64": %s",
			    UP_DISK_PATH(disk), sect_count,
			    UP_DISK_1SECT(disk), sect_off, os_lasterrstr());
		if (!opts->sloppyio)
			return (-1);
		res = byte_count;
		memset(buf, 0, res);
	}

	return (res / UP_DISK_1SECT(disk));
}

const void *
up_disk_getsect(const struct disk *disk, int64_t sect)
{
    assert(disk->buf);
    if(1 > up_disk_read(disk, sect, 1, disk->buf, UP_DISK_1SECT(disk)))
        return NULL;
    else
        return disk->buf;
}

int
up_disk_check1sect(const struct disk *disk, int64_t sect)
{
    return up_disk_checksectrange(disk, sect, 1);
}

int
up_disk_checksectrange(const struct disk *disk, int64_t start, int64_t size)
{
	struct disk_sect key;

	assert(disk->setup_done);
	assert(size > 0);
	memset(&key, 0, sizeof key);
	key.first = start;
	key.last = start + size - 1;
	if (RB_FIND(disk_sect_map, &disk->sectsused, &key) == NULL) {
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
up_disk_save1sect(struct disk *disk, int64_t sect, const struct map *ref,
    int tag)
{
    return up_disk_savesectrange(disk, sect, 1, ref, tag);
}

const void *
up_disk_savesectrange(struct disk *disk, int64_t first, int64_t size,
    const struct map *ref, int tag)
{
	struct disk_sect *new;

	/* allocate data structure */
	assert(disk->setup_done);
	assert(size > 0);
	if ((new = xalloc(1, sizeof(*new), XA_ZERO)) == NULL)
		return (NULL);
	new->data = NULL;
	new->first = first;
	new->last = first + size - 1;
	new->ref = ref;
	new->tag = tag;
	if ((new->data = xalloc(size, UP_DISK_1SECT(disk), XA_ZERO)) == NULL) {
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
	if (RB_INSERT(disk_sect_map, &disk->sectsused, new)) {
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
	disk->sectsused_count += size;

	/* return the data */
	return (new->data);
}

void
up_disk_sectsunref(struct disk *disk, const void *ref)
{
	struct disk_sect *ii, *inc;

	assert(disk->setup_done);
	for (ii = RB_MIN(disk_sect_map, &disk->sectsused); ii; ii = inc) {
		inc = RB_NEXT(disk_sect_map, &disk->sectsused, ii);
		if (ref == ii->ref) {
#ifdef DEBUG_SECTOR_SAVE
			printf("unmark %"PRId64"+%"PRId64" with %p\n",
			    ii->first, ii->last - ii->first + 1, ii->ref);
#endif
			RB_REMOVE(disk_sect_map, &disk->sectsused, ii);
			disk->sectsused_count -= ii->last - ii->first + 1;
			assert(0 <= disk->sectsused_count);
			free(ii->data);
			free(ii);
		}
	}
}

void
up_disk_close(struct disk *disk)
{
    if(!disk)
        return;
    up_map_freeall(disk);
    assert(RB_EMPTY(&disk->sectsused));
    assert(0 == disk->sectsused_count);
    switch (disk->type) {
    case DT_UNKNOWN:
	    assert(!disk->setup_done);
	    break;
    case DT_DEVICE:
	    assert(0 <= disk->handle.dev);
	    os_dev_close(disk->handle.dev);
	    break;
    case DT_FILE:
	    fclose(disk->handle.file);
	    break;
    case DT_IMAGE:
	    up_img_free(disk->handle.img);
	    break;
    default:
	    assert(!"bad disk type");
	    break;
    }
    free(disk->buf);
    free(disk->name);
    free(disk->path);
    free(disk);
    if (disk == st_curdisk)
	    st_curdisk = NULL;
}

static enum disk_type
open_thing(const char *name, union disk_handle *handle, const char **path)
{
	enum disk_type type;
	struct img *img;
	os_device_handle dev;
	FILE *fh;

	switch (type = os_dev_open(name, path, &dev)) {
	case DT_UNKNOWN:
		if (UP_NOISY(QUIET))
			up_err("failed to open device %s for reading: %s",
			    (*path ? *path : name), os_lasterrstr());
		break;
	case DT_DEVICE:
		handle->dev = dev;
		break;
	case DT_FILE:
		if ((fh = fopen(name, "rb")) == NULL) {
			up_err("failed to open file %s for reading: %s",
			    name, os_lasterrstr());
			return (DT_UNKNOWN);
		}

		/* check if it's an image file */
		img = NULL;
		switch (up_img_load(fh, name, &img)) {
		case -1:
			fclose(fh);
			return (DT_UNKNOWN);
		case 0:
			assert(img == NULL);
			handle->file = fh;
			break;
		case 1:
			assert(img != NULL);
			type = DT_IMAGE;
			fclose(fh);
			handle->img = img;
			break;
		default:
			assert(!"bad return value");
			break;
		}
		break;
	default:
		assert(!"bad return value");
		break;
	}

	return (type);
}

static int
fixparams(struct disk *disk, const struct disk_params *params)
{
	/* command-line options override any other values */
	if (params->sectsize > 0)
		disk->params.sectsize = params->sectsize;
	if (params->cyls > 0)
		disk->params.cyls = params->cyls;
	if (params->heads > 0)
        	disk->params.heads = params->heads;
	if (params->sects > 0)
        	disk->params.sects = params->sects;

	/* sector size defaults to 512 */
	if (disk->params.sectsize <= 0) {
		up_warn("couldn't determine sector size for %s, "
		    "assuming 512 bytes", UP_DISK_PATH(disk));
        	disk->params.sectsize = 512;
	}

	/* we can get the total size for a plain file */
	if (disk->params.size <= 0 &&
	    disk->type == DT_FILE &&
	    (disk->params.size = os_file_size(disk->handle.file)) > 0)
		disk->params.size /= disk->params.sectsize;

	/* is that good enough? */
	if (fixparams_checkone(&disk->params) == 0)
		return (0);

	/* apparently not, try defaulting heads and sectors to 255 and 63 */
	if (disk->params.heads <= 0) {
		up_warn("couldn't determine number of heads for %s, "
		    "assuming 255", UP_DISK_PATH(disk));
        	disk->params.heads = 255;
	}
	if (disk->params.sects <= 0) {
		up_warn("couldn't determine number of sectors/track for %s, "
		    "assuming 63", UP_DISK_PATH(disk));
		disk->params.sects = 63;
	}

	/* ok, is it good enough now? */
	if (fixparams_checkone(&disk->params) == 0)
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
up_disk_print(const struct disk *disk, void *_stream)
{
    FILE *              stream = _stream;
    const char *        unit;
    float               size;

    assert(disk->setup_done);
    size = up_fmtsize(UP_DISK_SIZEBYTES(disk), &unit);
    if(UP_NOISY(NORMAL))
        fprintf(stream, "%s: %.*f%s (%"PRId64" sectors of %d bytes)\n",
                UP_DISK_PATH(disk), UP_BESTDECIMAL(size), size, unit,
                UP_DISK_SIZESECTS(disk), UP_DISK_1SECT(disk));
    if(UP_NOISY(EXTRA))
        fprintf(stream,
#if 0
                "    description:         %s\n"
                "    device name:         %s\n"
#else
                "    label:               %s\n"
#endif
                "    device path:         %s\n"
                "    sector size:         %d\n"
                "    total sectors:       %"PRId64"\n"
                "    total cylinders:     %"PRId64" (cylinders)\n"
                "    tracks per cylinder: %"PRId64" (heads)\n"
                "    sectors per track:   %"PRId64" (sectors)\n"
#if 0
	    "\n", UP_DISK_DESC(disk), UP_DISK_NAME(disk), UP_DISK_PATH(disk),
	    UP_DISK_1SECT(disk), UP_DISK_SIZESECTS(disk),
	    UP_DISK_CYLS(disk), UP_DISK_HEADS(disk), UP_DISK_SPT(disk));
#else
                "\n", UP_DISK_NAME(disk),
                UP_DISK_PATH(disk), UP_DISK_1SECT(disk), UP_DISK_SIZESECTS(disk),
                UP_DISK_CYLS(disk), UP_DISK_HEADS(disk), UP_DISK_SPT(disk));
#endif
    if(UP_NOISY(NORMAL))
        fputc('\n', stream);
}

void
up_disk_dump(const struct disk *disk, void *stream)
{
	struct disk_sect *node;

	assert(disk->setup_done);
	for (node = RB_MIN(disk_sect_map, &disk->sectsused); node;
	    node = RB_NEXT(disk_sect_map, &disk->sectsused, node))
		up_map_dumpsect(node->ref, stream, node->first,
		    node->last - node->first + 1, node->data, node->tag);
}

void
up_disk_sectsiter(const struct disk *disk,
                  up_disk_iterfunc_t func, void *arg)
{
	struct disk_sect *node;

	assert(disk->setup_done);
	for (node = RB_MIN(disk_sect_map, &disk->sectsused); node;
	    node = RB_NEXT(disk_sect_map, &disk->sectsused, node))
		if (func(disk, node, arg) == 0)
			break;
}

const struct disk_sect *
up_disk_nthsect(const struct disk *disk, int off)
{
	struct disk_sect *node;

	assert(disk->setup_done);
	assert(off >= 0);
	for (node = RB_MIN(disk_sect_map, &disk->sectsused); node;
	    node = RB_NEXT(disk_sect_map, &disk->sectsused, node))
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
