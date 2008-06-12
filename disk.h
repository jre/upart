#ifndef HDR_UPART_DISK
#define HDR_UPART_DISK

#include "bsdtree.h"
#include "util.h"

struct up_opts;
struct up_map;
struct up_part;
struct up_img;

#define UP_SECT_OFF(sect)       ((sect)->first)
#define UP_SECT_COUNT(sect)     ((sect)->last - (sect)->first + 1)
#define UP_SECT_MAP(sect)       ((sect)->ref)
#define UP_SECT_DATA(sect)      ((sect)->data)

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
    char *                  ud_name;        /* disk name supplied by user */
    char *                  ud_path;        /* path to opened device node */
    struct up_diskparams    ud_params;

    unsigned int            ud_flag_setup     : 1;
    unsigned int            ud_flag_plainfile : 1;

    /* don't touch any of these */
    int                     upd_fd;
    uint8_t                *upd_buf;
    struct up_img          *upd_img;
    struct up_part         *maps;
    struct up_disk_sectmap  upd_sectsused;
    int64_t                 upd_sectsused_count;
};

#define UP_DISK_LABEL(disk)     ((disk)->ud_name)
#define UP_DISK_PATH(disk)      ((disk)->ud_path)
#define UP_DISK_1SECT(disk)     ((disk)->ud_params.ud_sectsize)
#define UP_DISK_CYLS(disk)      ((disk)->ud_params.ud_cyls)
#define UP_DISK_HEADS(disk)     ((disk)->ud_params.ud_heads)
#define UP_DISK_SPT(disk)       ((disk)->ud_params.ud_sects)
#define UP_DISK_SIZESECTS(disk) ((disk)->ud_params.ud_size)
/* XXX should handle overflow here and anywhere sects are converted to bytes */
#define UP_DISK_SIZEBYTES(disk) \
    ((disk)->ud_params.ud_size * (disk)->ud_params.ud_sectsize)
#define UP_DISK_IS_IMG(disk)    (NULL != (disk)->upd_img)
#define UP_DISK_IS_FILE(disk)   ((disk)->ud_flag_plainfile)

typedef int (*up_disk_iterfunc_t)(const struct up_disk *,
                                   const struct up_disk_sectnode *, void *);

/* Open the disk device, must call up_disk_setup() after this */
struct up_disk *up_disk_open(const char *path, const struct up_opts *opts,
                             int writable);

/* Read get drive parameters and make disk ready to read from or write to */
int up_disk_setup(struct up_disk *disk, const struct up_opts *opts);

/* Read from disk into buffer. Note that START, SIZE, and the return
   value are in sectors but bufsize is in bytes. */
int64_t up_disk_read(const struct up_disk *disk, int64_t start, int64_t size,
                     void *buf, size_t bufsize, int verbose);

/* Read a single sector. The returned pointer is valid until function
   is called again. */
const void *up_disk_getsect(const struct up_disk *disk, int64_t sect,
                            int verbose);

/* return true if a sector is marked as used, false otherwise */
int up_disk_check1sect(const struct up_disk *disk, int64_t sect);

/* return true if any sector in a range is marked as used, false otherwise */
int up_disk_checksectrange(const struct up_disk *disk, int64_t first,
                           int64_t size);

/* mark a sector as used and return 0, return -1 if already used */
const void *up_disk_save1sect(struct up_disk *disk, int64_t sect,
                              const struct up_map *ref, int tag, int verbose);

/* mark range of sectors as used and return 0, return -1 if already used */
const void *up_disk_savesectrange(struct up_disk *disk, int64_t first,
                                  int64_t size, const struct up_map *ref,
                                  int tag, int verbose);

/* mark all sectors associated with REF unused */
void up_disk_sectsunref(struct up_disk *disk, const void *ref);

/* iterate through marked sectors */
/* the passed function should return 0 to stop iteration */
void up_disk_sectsiter(const struct up_disk *disk,
                       up_disk_iterfunc_t func, void *arg);
/* return the nth sector */
const struct up_disk_sectnode *up_disk_nthsect(const struct up_disk *disk, int n);

/* Close disk and free struct. */
void up_disk_close(struct up_disk *disk);

/* Print disk info to STREAM. */
void up_disk_print(const struct up_disk *disk, void *_stream, int verbose);

/* Print hexdump of sectors with partition information to STREAM. */
void up_disk_dump(const struct up_disk *disk, void *_stream);

#endif /* HDR_UPART_DISK */
