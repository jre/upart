#ifndef HDR_UPART_DISK
#define HDR_UPART_DISK

#include "bsdtree.h"

struct up_opts;
struct up_map;
struct up_part;
struct up_img;

struct up_disk_sectnode
{
    int64_t                     first;
    int64_t                     last;
    const struct up_map        *ref;
    void                       *data;
    int                         tag;
    RB_ENTRY(up_disk_sectnode)  link;
};

RB_HEAD(up_disk_sectmap, up_disk_sectnode);

struct up_disk
{
    char *      upd_name;           /* disk name supplied by the user */
    char *      upd_path;           /* path to opened device node */
    int         upd_sectsize;       /* size of a sector in bytes */
    int64_t     upd_cyls;           /* total number of cylinders */
    int64_t     upd_heads;          /* number of tracks per cylinder */
    int64_t     upd_sects;          /* number of sectors per track */
    int64_t     upd_size;           /* total number of sects */

    /* don't touch any of these */
    int                     upd_fd;
    uint8_t                *upd_buf;
    struct up_img          *upd_img;
    struct up_part         *maps;
    struct up_disk_sectmap  upd_sectsused;
    int64_t                 upd_sectsused_count;
};

/* Open the disk device read-only and get drive params */
struct up_disk *up_disk_open(const char *path, struct up_opts *opts);

/* Read from disk into buffer. Note that START, SIZE, and the return
   value are in sectors but bufsize is in bytes. */
int64_t up_disk_read(const struct up_disk *disk, int64_t start, int64_t size,
                     void *buf, size_t bufsize, int verbose);

/* Read a single sector. The returned pointer is valid until function
   is called again. */
const void *up_disk_getsect(struct up_disk *disk, int64_t sect, int verbose);

/* return true if a sector is marked as used, false otherwise */
int up_disk_check1sect(struct up_disk *disk, int64_t sect);

/* return true if any sector in a range is marked as used, false otherwise */
int up_disk_checksectrange(struct up_disk *disk, int64_t first, int64_t size);

/* mark a sector as used and return 0, return -1 if already used */
const void *up_disk_save1sect(struct up_disk *disk, int64_t sect,
                              const struct up_map *ref, int tag, int verbose);

/* mark range of sectors as used and return 0, return -1 if already used */
const void *up_disk_savesectrange(struct up_disk *disk, int64_t first, int64_t size,
                                  const struct up_map *ref, int tag, int verbose);

/* mark all sectors associated with REF unused */
void up_disk_sectsunref(struct up_disk *disk, const void *ref);

/* iterate through marked sectors */
void up_disk_sectsiter(const struct up_disk *disk,
                       void (*func)(const struct up_disk *,
                                    const struct up_disk_sectnode *, void *),
                       void *arg);

/* Close disk and free struct. */
void up_disk_close(struct up_disk *disk);

/* Print disk info to STREAM. */
void up_disk_print(const struct up_disk *disk, void *_stream, int verbose);

/* Print hexdump of sectors with partition information to STREAM. */
void up_disk_dump(const struct up_disk *disk, void *_stream);

#endif /* HDR_UPART_DISK */
