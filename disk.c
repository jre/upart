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

static int fixparams(struct up_disk *disk, const struct up_opts *opts);
static int fixparams_checkone(struct up_disk *disk);
static int sectcmp(struct up_disk_sectnode *left,
                   struct up_disk_sectnode *right);

RB_GENERATE_STATIC(up_disk_sectmap, up_disk_sectnode, link, sectcmp);

struct up_disk *
up_disk_open(const char *name, struct up_opts *opts)
{
    struct up_disk *disk;
    struct up_img  *img;
    const char     *path;
    int             fd, res;
    struct stat     sb;

    /* open device */
    fd = up_os_opendisk(name, &path, opts);
    if(0 > fd)
    {
        fprintf(stderr, "failed to open %s for reading: %s\n",
                (path ? path : name), strerror(errno));
        return NULL;
    }

    /* is it a plain file? */
    if(0 == fstat(fd, &sb) && S_ISREG(sb.st_mode))
        opts->plainfile = 1;

    /* check if it's an image file */
    res = up_img_load(fd, name, opts, &img);
    if(0 > res)
    {
        close(fd);
        return NULL;
    }

    /* disk struct */
    disk = calloc(1, sizeof *disk);
    if(!disk)
    {
        perror("malloc");
        close(fd);
        return NULL;
    }
    RB_INIT(&disk->upd_sectsused);
    disk->upd_sectsused_count = 0;
    disk->upd_name = strdup(name);
    if(!disk->upd_name)
    {
        perror("malloc");
        close(fd);
        goto fail;
    }

    if(0 == res)
    {
        /* it's not an image, initalize the disk struct normally */
        disk->upd_fd = fd;

        /* copy full device path */
        if(!path)
            disk->upd_path = disk->upd_name;
        else
        {
            disk->upd_path = strdup(path);
            if(!disk->upd_path)
            {
                perror("malloc");
                goto fail;
            }
        }

        /* try to get drive parameters from OS */
        up_os_getparams(fd, disk, opts);
    }
    else
    {
        /* it is an image, initalize the disk accordingly */
        close(fd);
        disk->upd_fd = -1;
        disk->upd_img = img;
        disk->upd_path = disk->upd_name;

        /* try to get drive parameters from image */
        if(0 > up_img_getparams(disk, img))
            goto fail;
    }

    /* try to fill in missing drive paramaters */
    if(0 > fixparams(disk, opts))
    {
        fprintf(stderr, "failed to determine disk parameters for %s\n",
                disk->upd_path);
        goto fail;
    }

    return disk;

  fail:
    up_disk_close(disk);
    return NULL;
}

int64_t
up_disk_read(const struct up_disk *disk, int64_t start, int64_t size,
             void *buf, size_t bufsize)
{
    ssize_t res;

    /* validate and fixup arguments */
    assert(0 < bufsize && 0 < size && 0 <= start);
    bufsize -= bufsize % disk->upd_sectsize;
    if(bufsize / disk->upd_sectsize < size)
        size = bufsize / disk->upd_sectsize;
    if(0 == bufsize || 0 == size)
        return 0;
    if(INT64_MAX / disk->upd_sectsize < start)
    {
        errno = EINVAL;
        return -1;
    }

    /* if there's an image then read from it instead */
    if(disk->upd_img)
        return up_img_read(disk->upd_img, start, size, buf);

    /* otherwise try to read from the disk */
    res = pread(disk->upd_fd, buf, size * disk->upd_sectsize,
                start * disk->upd_sectsize);
    if(0 > res)
    {
        fprintf(stderr, "failed to read %s sector %"PRIu64": %s\n",
                disk->upd_path, start, strerror(errno));
        return -1;
    }

    return res / disk->upd_sectsize;
}

const void *
up_disk_getsect(struct up_disk *disk, int64_t sect)
{
    if(!disk->upd_buf)
    {
        disk->upd_buf = malloc(disk->upd_sectsize);
        if(!disk->upd_buf)
        {
            perror("malloc");
            return NULL;
        }
    }

    if(1 > up_disk_read(disk, sect, 1, disk->upd_buf, disk->upd_sectsize))
        return NULL;
    else
        return disk->upd_buf;
}

int
up_disk_check1sect(struct up_disk *disk, int64_t sect)
{
    return up_disk_checksectrange(disk, sect, 1);
}

int
up_disk_checksectrange(struct up_disk *disk, int64_t start, int64_t size)
{
    struct up_disk_sectnode key;

    assert(0 < size);
    memset(&key, 0, sizeof key);
    key.first = start;
    key.last  = start + size - 1;
    if(NULL == RB_FIND(up_disk_sectmap, &disk->upd_sectsused, &key))
    {
#ifdef DEBUG_SECTOR_SAVE
        printf("check %"PRId64"+%"PRId64": free\n", start, size);
#endif
        return 0;
    }
    else
    {
#ifdef DEBUG_SECTOR_SAVE
        printf("check %"PRId64"+%"PRId64": used\n", start, size);
#endif
        return 1;
    }
}

const void *
up_disk_save1sect(struct up_disk *disk, int64_t sect,
                  const struct up_map *ref, int tag)
{
    return up_disk_savesectrange(disk, sect, 1, ref, tag);
}

const void *
up_disk_savesectrange(struct up_disk *disk, int64_t first, int64_t size,
                      const struct up_map *ref, int tag)
{
    struct up_disk_sectnode *new;

    /* allocate data structure */
    assert(0 < size);
    new = calloc(1, sizeof *new);
    if(NULL == new)
    {
        perror("malloc");
        return NULL;
    }
    new->data  = NULL;
    new->first = first;
    new->last  = first + size - 1;
    new->ref   = ref;
    new->tag   = tag;
    new->data  = calloc(size, disk->upd_sectsize);
    if(!new->data)
    {
        perror("malloc");
        free(new);
        return NULL;
    }

    /* try to read the sectors from disk */
    if(size != up_disk_read(disk, first, size, new->data,
                            size * disk->upd_sectsize))
    {
        free(new->data);
        free(new);
        return NULL;
    }

    /* insert it in the tree if the sectors aren't marked as used */
    if(RB_INSERT(up_disk_sectmap, &disk->upd_sectsused, new))
    {
#ifdef DEBUG_SECTOR_SAVE
        printf("failed to mark %"PRId64"+%"PRId64" with %p, already used\n",
               first, size, ref);
#endif
        free(new->data);
        free(new);
        return NULL;
    }
#ifdef DEBUG_SECTOR_SAVE
    printf("mark %"PRId64"+%"PRId64" with %p\n", first, size, ref);
#endif
    disk->upd_sectsused_count += size;

    /* return the data */
    return new->data;
}

void
up_disk_sectsunref(struct up_disk *disk, const void *ref)
{
    struct up_disk_sectnode *ii, *inc;

    for(ii = RB_MIN(up_disk_sectmap, &disk->upd_sectsused); ii; ii = inc)
    {
        inc = RB_NEXT(up_disk_sectmap, &disk->upd_sectsused, ii);
        if(ref == ii->ref)
        {
#ifdef DEBUG_SECTOR_SAVE
            printf("unmark %"PRId64"+%"PRId64" with %p\n",
                   ii->first, ii->last - ii->first + 1, ii->ref);
#endif
            RB_REMOVE(up_disk_sectmap, &disk->upd_sectsused, ii);
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
    free(disk->upd_name);
    if(disk->upd_path != disk->upd_name)
        free(disk->upd_path);
    free(disk);
}

static int
fixparams(struct up_disk *disk, const struct up_opts *opts)
{
    struct stat sb;

    /* command-line options override any other values */
    if(0 < opts->sectsize)
        disk->upd_sectsize = opts->sectsize;
    if(0 < opts->cyls)
        disk->upd_cyls = opts->cyls;
    if(0 < opts->heads)
        disk->upd_heads = opts->heads;
    if(0 < opts->sects)
        disk->upd_sects = opts->sects;

    /* sector size defaults to 512 */
    if(0 >= disk->upd_sectsize)
        disk->upd_sectsize = 512;

    /* we can get the total size for a plain file */
    if(0 >= disk->upd_size && !disk->upd_img &&
       opts->plainfile && 0 == stat(disk->upd_path, &sb))
        disk->upd_size = sb.st_size / disk->upd_sectsize;

    /* is that good enough? */
    if(0 == fixparams_checkone(disk))
        return 0;

    /* apparently not, try defaulting heads and sectors to 255 and 63 */
    if(0 >= disk->upd_heads)
        disk->upd_heads = 255;
    if(0 >= disk->upd_sects)
        disk->upd_sects = 63;

    /* ok, is it good enough now? */
    if(0 == fixparams_checkone(disk))
        return 0;

    /* guess not */
    return -1;
}

static int
fixparams_checkone(struct up_disk *disk)
{
    /* if we have all 4 then we're good */
    if(0 <  disk->upd_cyls  && 0 <  disk->upd_heads &&
       0 <  disk->upd_sects && 0 <  disk->upd_size )
        return 0;

    /* guess cyls from other three */
    if(0 >= disk->upd_cyls  && 0 <  disk->upd_heads &&
       0 <  disk->upd_sects && 0 <  disk->upd_size )
    {
        disk->upd_cyls = disk->upd_size / disk->upd_sects / disk->upd_heads;
        return 0;
    }

    /* guess heads from other three */
    if(0 <  disk->upd_cyls  && 0 >= disk->upd_heads &&
       0 <  disk->upd_sects && 0 <  disk->upd_size )
    {
        disk->upd_heads = disk->upd_size / disk->upd_sects / disk->upd_cyls;
        return 0;
    }

    /* guess sects from other three */
    if(0 <  disk->upd_cyls  && 0 <  disk->upd_heads &&
       0 >= disk->upd_sects && 0 <  disk->upd_size )
    {
        disk->upd_sects = disk->upd_size / disk->upd_heads / disk->upd_cyls;
        return 0;
    }

    /* guess size from other three */
    if(0 <  disk->upd_cyls  && 0 <  disk->upd_heads &&
       0 <  disk->upd_sects && 0 >= disk->upd_size )
    {
        disk->upd_size = disk->upd_sects * disk->upd_heads * disk->upd_cyls;
        return 0;
    }

    /* can only guess with one missing param */
    return -1;
}

void
up_disk_print(const struct up_disk *disk, void *_stream,
              const struct up_opts *opt)
{
    FILE *              stream = _stream;
    const char *        unit;
    float               size;

    size = up_fmtsize(disk->upd_size * disk->upd_sectsize, &unit);
    fprintf(stream, "%s: %.*f%s (%"PRId64" sectors of %d bytes)\n",
            disk->upd_name, UP_BESTDECIMAL(size), size, unit,
            disk->upd_size, disk->upd_sectsize);
    if(opt->verbose)
    fprintf(stream,
            "    device path:         %s\n"
            "    sector size:         %d\n"
            "    total sectors:       %"PRId64"\n"
            "    total cylinders:     %d (cylinders)\n"
            "    tracks per cylinder: %d (heads)\n"
            "    sectors per track:   %d (sectors)\n"
            "\n", disk->upd_path, disk->upd_sectsize, disk->upd_size,
            (int)disk->upd_cyls, (int)disk->upd_heads, (int)disk->upd_sects);
}

void
up_disk_dump(const struct up_disk *disk, void *stream)
{
    struct up_disk_sectnode *node;

    for(node = RB_MIN(up_disk_sectmap, &disk->upd_sectsused); node;
        node = RB_NEXT(up_disk_sectmap, &disk->upd_sectsused, node))
        up_map_dumpsect(node->ref, stream, node->first,
                        node->last - node->first + 1, node->data, node->tag);
}

void
up_disk_sectsiter(const struct up_disk *disk,
                  void (*func)(const struct up_disk *,
                               const struct up_disk_sectnode *, void *),
                  void *arg)
{
    struct up_disk_sectnode *node;

    for(node = RB_MIN(up_disk_sectmap, &disk->upd_sectsused); node;
        node = RB_NEXT(up_disk_sectmap, &disk->upd_sectsused, node))
        func(disk, node, arg);
}

static int
sectcmp(struct up_disk_sectnode *left, struct up_disk_sectnode *right)
{
    if(left->last < right->first)
    {
        /*
        printf("cmp (%"PRId64"+%"PRId64") < (%"PRId64"+%"PRId64")\n",
               left->first, left->last - left->first + 1,
               right->first, right->last - right->first + 1);
        */
        return -1;
    }
    else if(left->first > right->last)
    {
        /*
        printf("cmp (%"PRId64"+%"PRId64") > (%"PRId64"+%"PRId64")\n",
               left->first, left->last - left->first + 1,
               right->first, right->last - right->first + 1);
        */
        return 1;
    }
    else
    {
        /*
        printf("cmp (%"PRId64"+%"PRId64") = (%"PRId64"+%"PRId64")\n",
               left->first, left->last - left->first + 1,
               right->first, right->last - right->first + 1);
        */
        return 0;
    }
}
