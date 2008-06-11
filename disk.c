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
static int fixparams_checkone(struct up_diskparams *disk);
static int sectcmp(struct up_disk_sectnode *left,
                   struct up_disk_sectnode *right);

RB_GENERATE_STATIC(up_disk_sectmap, up_disk_sectnode, link, sectcmp);

struct up_disk *
up_disk_open(const char *name, const struct up_opts *opts,
             int writable)
{
    struct up_disk *disk;
    struct up_img  *img;
    const char     *path;
    int             fd, res, plain;
    struct stat     sb;

    /* open device */
    fd = up_os_opendisk(name, &path, opts, writable);
    if(0 > fd)
    {
        if(UP_NOISY(opts->verbosity, QUIET))
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
        res = up_img_load(fd, name, opts, &img);
        if(0 > res)
        {
            close(fd);
            return NULL;
        }
        else if(0 == res)
            assert(NULL != img);
    }

    /* disk struct */
    disk = calloc(1, sizeof *disk);
    if(!disk)
    {
        perror("malloc");
        close(fd);
        return NULL;
    }
    disk->ud_flag_plainfile = plain;
    RB_INIT(&disk->upd_sectsused);
    disk->upd_sectsused_count = 0;
    disk->ud_name = strdup(name);
    if(!disk->ud_name)
    {
        perror("malloc");
        close(fd);
        goto fail;
    }
    disk->ud_path = strdup(path ? path : disk->ud_name);
    if(!disk->ud_path)
    {
        perror("malloc");
        goto fail;
    }

    if(NULL == img)
    {
        /* it's not an image, initalize the disk struct normally */
        disk->upd_fd = fd;

        /* try to get drive parameters from OS */
        up_os_getparams(fd, disk, opts);
    }
    else
    {
        /* it is an image, initalize the disk accordingly */
        close(fd);
        disk->upd_fd = -1;
        disk->upd_img = img;

        /* try to get drive parameters from image */
        if(0 > up_img_getparams(&disk->ud_params, img))
            goto fail;
    }

    /* try to fill in missing drive paramaters */
    if(0 > fixparams(disk, opts))
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_err("failed to determine disk parameters for %s",
                   UP_DISK_PATH(disk));
        goto fail;
    }

    return disk;

  fail:
    up_disk_close(disk);
    return NULL;
}

int64_t
up_disk_read(const struct up_disk *disk, int64_t start, int64_t size,
             void *buf, size_t bufsize, int verbose)
{
    ssize_t res;

    /* validate and fixup arguments */
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
        return up_img_read(disk->upd_img, start, size, buf, verbose);

    /* otherwise try to read from the disk */
    res = pread(disk->upd_fd, buf, size * UP_DISK_1SECT(disk),
                start * UP_DISK_1SECT(disk));
    if(0 > res)
    {
        if(UP_NOISY(verbose, QUIET))
            up_err("failed to read %s sector %"PRIu64": %s",
                   UP_DISK_PATH(disk), start, strerror(errno));
        return -1;
    }

    return res / UP_DISK_1SECT(disk);
}

const void *
up_disk_getsect(struct up_disk *disk, int64_t sect, int vrb)
{
    if(!disk->upd_buf)
    {
        disk->upd_buf = malloc(UP_DISK_1SECT(disk));
        if(!disk->upd_buf)
        {
            perror("malloc");
            return NULL;
        }
    }

    if(1 > up_disk_read(disk, sect, 1, disk->upd_buf, UP_DISK_1SECT(disk), vrb))
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
                  const struct up_map *ref, int tag, int verbose)
{
    return up_disk_savesectrange(disk, sect, 1, ref, tag, verbose);
}

const void *
up_disk_savesectrange(struct up_disk *disk, int64_t first, int64_t size,
                      const struct up_map *ref, int tag, int verbose)
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
    new->data  = calloc(size, UP_DISK_1SECT(disk));
    if(!new->data)
    {
        perror("malloc");
        free(new);
        return NULL;
    }

    /* try to read the sectors from disk */
    if(size != up_disk_read(disk, first, size, new->data,
                            size * UP_DISK_1SECT(disk), verbose))
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
    free(disk->ud_name);
    free(disk->ud_path);
    free(disk);
}

static int
fixparams(struct up_disk *disk, const struct up_opts *opts)
{
    struct stat sb;

    /* command-line options override any other values */
    if(0 < opts->params.ud_sectsize)
        disk->ud_params.ud_sectsize = opts->params.ud_sectsize;
    if(0 < opts->params.ud_cyls)
        disk->ud_params.ud_cyls = opts->params.ud_cyls;
    if(0 < opts->params.ud_heads)
        disk->ud_params.ud_heads = opts->params.ud_heads;
    if(0 < opts->params.ud_sects)
        disk->ud_params.ud_sects = opts->params.ud_sects;

    /* sector size defaults to 512 */
    if(0 >= disk->ud_params.ud_sectsize)
        disk->ud_params.ud_sectsize = 512;

    /* we can get the total size for a plain file */
    if(0 >= disk->ud_params.ud_size && !UP_DISK_IS_IMG(disk) &&
       UP_DISK_IS_FILE(disk) && 0 == stat(UP_DISK_PATH(disk), &sb))
        disk->ud_params.ud_size = sb.st_size / disk->ud_params.ud_sectsize;

    /* is that good enough? */
    if(0 == fixparams_checkone(&disk->ud_params))
        return 0;

    /* apparently not, try defaulting heads and sectors to 255 and 63 */
    if(0 >= disk->ud_params.ud_heads)
        disk->ud_params.ud_heads = 255;
    if(0 >= disk->ud_params.ud_sects)
        disk->ud_params.ud_sects = 63;

    /* ok, is it good enough now? */
    if(0 == fixparams_checkone(&disk->ud_params))
        return 0;

    /* guess not */
    return -1;
}

static int
fixparams_checkone(struct up_diskparams *disk)
{
    /* if we have all 4 then we're good */
    if(0 <  disk->ud_cyls  && 0 <  disk->ud_heads &&
       0 <  disk->ud_sects && 0 <  disk->ud_size )
        return 0;

    /* guess cyls from other three */
    if(0 >= disk->ud_cyls  && 0 <  disk->ud_heads &&
       0 <  disk->ud_sects && 0 <  disk->ud_size )
    {
        disk->ud_cyls = disk->ud_size / disk->ud_sects / disk->ud_heads;
        return 0;
    }

    /* guess heads from other three */
    if(0 <  disk->ud_cyls  && 0 >= disk->ud_heads &&
       0 <  disk->ud_sects && 0 <  disk->ud_size )
    {
        disk->ud_heads = disk->ud_size / disk->ud_sects / disk->ud_cyls;
        return 0;
    }

    /* guess sects from other three */
    if(0 <  disk->ud_cyls  && 0 <  disk->ud_heads &&
       0 >= disk->ud_sects && 0 <  disk->ud_size )
    {
        disk->ud_sects = disk->ud_size / disk->ud_heads / disk->ud_cyls;
        return 0;
    }

    /* guess size from other three */
    if(0 <  disk->ud_cyls  && 0 <  disk->ud_heads &&
       0 <  disk->ud_sects && 0 >= disk->ud_size )
    {
        disk->ud_size = disk->ud_sects * disk->ud_heads * disk->ud_cyls;
        return 0;
    }

    /* can only guess with one missing param */
    return -1;
}

void
up_disk_print(const struct up_disk *disk, void *_stream, int verbose)
{
    FILE *              stream = _stream;
    const char *        unit;
    float               size;

    size = up_fmtsize(UP_DISK_SIZEBYTES(disk), &unit);
    if(UP_NOISY(verbose, NORMAL))
        fprintf(stream, "%s: %.*f%s (%"PRId64" sectors of %d bytes)\n",
                UP_DISK_PATH(disk), UP_BESTDECIMAL(size), size, unit,
                UP_DISK_SIZESECTS(disk), UP_DISK_1SECT(disk));
    if(UP_NOISY(verbose, EXTRA))
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
    if(UP_NOISY(verbose, NORMAL))
        fputc('\n', stream);
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

const struct up_disk_sectnode *
up_disk_nthsect(const struct up_disk *disk, int off)
{
    struct up_disk_sectnode *node;

    assert(0 <= off);
    for(node = RB_MIN(up_disk_sectmap, &disk->upd_sectsused); node;
        node = RB_NEXT(up_disk_sectmap, &disk->upd_sectsused, node))
        if(0 == off--)
            return node;
    return NULL;
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
