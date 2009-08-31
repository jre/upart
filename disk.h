#ifndef HDR_UPART_DISK
#define HDR_UPART_DISK

#include "bsdtree.h"

struct up_map;
struct up_part;
struct img;
struct disk_params;

#define UP_SECT_OFF(sect)       ((sect)->first)
#define UP_SECT_COUNT(sect)     ((sect)->last - (sect)->first + 1)
#define UP_SECT_MAP(sect)       ((sect)->ref)
#define UP_SECT_DATA(sect)      ((sect)->data)

struct disk_params {
	int64_t cyls;		/* total number of cylinders */
	int64_t heads;		/* number of tracks per cylinder */
	int64_t sects;		/* number of sectors per track */
	int64_t size;		/* total number of sects */
	int sectsize;		/* size of a sector in bytes */
};

struct disk_sect {
	int64_t first;
	int64_t last;
	const struct up_map *ref;
	void *data;
	int tag;
	RB_ENTRY(disk_sect) link;
};

RB_HEAD(disk_sect_map, disk_sect);

struct up_disk
{
    char *                  ud_name;        /* disk name supplied by user */
    char *                  ud_path;        /* path to opened device node */
    struct disk_params ud_params;

    unsigned int            ud_flag_setup     : 1;
    unsigned int            ud_flag_plainfile : 1;

    /* don't touch any of these */
    int                     upd_fd;
    uint8_t                *upd_buf;
    struct img *upd_img;
    struct up_part         *maps;
    struct disk_sect_map upd_sectsused;
    int64_t                 upd_sectsused_count;
};

#define UP_DISK_LABEL(disk)     ((disk)->ud_name)
#define UP_DISK_PATH(disk)      ((disk)->ud_path)
#define UP_DISK_1SECT(disk)     ((disk)->ud_params.sectsize)
#define UP_DISK_CYLS(disk)      ((disk)->ud_params.cyls)
#define UP_DISK_HEADS(disk)     ((disk)->ud_params.heads)
#define UP_DISK_SPT(disk)       ((disk)->ud_params.sects)
#define UP_DISK_SIZESECTS(disk) ((disk)->ud_params.size)
/* XXX should handle overflow here and anywhere sects are converted to bytes */
#define UP_DISK_SIZEBYTES(disk) \
    ((disk)->ud_params.size * (disk)->ud_params.sectsize)
#define UP_DISK_IS_IMG(disk)    (NULL != (disk)->upd_img)
#define UP_DISK_IS_FILE(disk)   ((disk)->ud_flag_plainfile)

typedef int (*up_disk_iterfunc_t)(const struct up_disk *,
    const struct disk_sect *, void *);

/* Open the disk device, must call up_disk_setup() after this */
struct up_disk	*up_disk_open(const char *);

/* Read get drive parameters and make disk ready to read from or write to */
int		 up_disk_setup(struct up_disk *, const struct disk_params *);

/* Read from disk into buffer. Note that START, SIZE, and the return
   value are in sectors but bufsize is in bytes. */
int64_t		 up_disk_read(const struct up_disk *, int64_t, int64_t,
    void *, size_t);

/* Read a single sector. The returned pointer is valid until function
   is called again. */
const void	*up_disk_getsect(const struct up_disk *, int64_t);

/* return true if a sector is marked as used, false otherwise */
int up_disk_check1sect(const struct up_disk *disk, int64_t sect);

/* return true if any sector in a range is marked as used, false otherwise */
int up_disk_checksectrange(const struct up_disk *disk, int64_t first,
                           int64_t size);

/* mark a sector as used and return 0, return -1 if already used */
const void	*up_disk_save1sect(struct up_disk *, int64_t,
    const struct up_map *, int);

/* mark range of sectors as used and return 0, return -1 if already used */
const void	*up_disk_savesectrange(struct up_disk *, int64_t, int64_t,
    const struct up_map *, int);

/* mark all sectors associated with REF unused */
void up_disk_sectsunref(struct up_disk *disk, const void *ref);

/* iterate through marked sectors */
/* the passed function should return 0 to stop iteration */
void up_disk_sectsiter(const struct up_disk *disk,
                       up_disk_iterfunc_t func, void *arg);
/* return the nth sector */
const struct disk_sect	*up_disk_nthsect(const struct up_disk *, int);

/* Close disk and free struct. */
void up_disk_close(struct up_disk *disk);

/* Print disk info to STREAM. */
void up_disk_print(const struct up_disk *disk, void *_stream);

/* Print hexdump of sectors with partition information to STREAM. */
void up_disk_dump(const struct up_disk *disk, void *_stream);

#endif /* HDR_UPART_DISK */
