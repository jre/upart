#ifndef HDR_UPART_DISK_PARAMS_ONLY
#define HDR_UPART_DISK_PARAMS_ONLY
struct disk_params {
	int64_t cyls;		/* total number of cylinders */
	int64_t heads;		/* number of tracks per cylinder */
	int64_t sects;		/* number of sectors per track */
	int64_t size;		/* total number of sects */
	int sectsize;		/* size of a sector in bytes */
};
#endif /* HDR_UPART_DISK_PARAMS_ONLY */

#if !defined(HDR_UPART_DISK) && !defined(UPART_DISK_PARAMS_ONLY)
#define HDR_UPART_DISK

#include "bsdtree.h"

struct map;
struct part;
struct img;
struct os_device_handle;

#define UP_SECT_OFF(sect)       ((sect)->first)
#define UP_SECT_COUNT(sect)     ((sect)->last - (sect)->first + 1)
#define UP_SECT_MAP(sect)       ((sect)->ref)
#define UP_SECT_DATA(sect)      ((sect)->data)

struct disk_sect {
	int64_t first;
	int64_t last;
	const struct map *ref;
	void *data;
	int tag;
	RB_ENTRY(disk_sect) link;
};

RB_HEAD(disk_sect_map, disk_sect);

enum disk_type {
	DT_UNKNOWN = 0,
	DT_DEVICE,
	DT_FILE,
	DT_IMAGE
};

union disk_handle {
	struct os_device_handle *dev;
	FILE *file;
	struct img *img;
};

struct disk {
	char *name;		/* disk name supplied by user */
	char *path;		/* path to opened device node */
	struct disk_params params;
	char desc[128];		/* hardware description provided by OS */

	/* don't touch any of these */
	unsigned setup_done;
	enum disk_type type;
	union disk_handle handle;
	uint8_t *buf;
	struct part *maps;
	struct disk_sect_map sectsused;
	int64_t sectsused_count;
};

#define UP_DISK_NAME(disk)      ((disk)->name)
#define UP_DISK_PATH(disk)      ((disk)->path)
#define UP_DISK_DESC(disk)      ((disk)->desc)
#define UP_DISK_1SECT(disk)     ((disk)->params.sectsize)
#define UP_DISK_CYLS(disk)      ((disk)->params.cyls)
#define UP_DISK_HEADS(disk)     ((disk)->params.heads)
#define UP_DISK_SPT(disk)       ((disk)->params.sects)
#define UP_DISK_SIZESECTS(disk) ((disk)->params.size)
/* XXX should handle overflow here and anywhere sects are converted to bytes */
#define UP_DISK_SIZEBYTES(disk) \
    ((disk)->params.size * (disk)->params.sectsize)

typedef int (*up_disk_iterfunc_t)(const struct disk *,
    const struct disk_sect *, void *);

/* Returns the currently-open disk, or NULL */
struct disk	*current_disk(void);

/* Open the disk device, must call up_disk_setup() after this */
struct disk	*up_disk_open(const char *);

/* Read get drive parameters and make disk ready to read from or write to */
int		 up_disk_setup(struct disk *, const struct disk_params *);

/* Read from disk into buffer. Note that START, SIZE, and the return
   value are in sectors but bufsize is in bytes. */
int64_t		 up_disk_read(const struct disk *, int64_t, int64_t,
    void *, size_t);

/* Read a single sector. The returned pointer is valid until function
   is called again. */
const void	*up_disk_getsect(const struct disk *, int64_t);

/* return true if a sector is marked as used, false otherwise */
int up_disk_check1sect(const struct disk *disk, int64_t sect);

/* return true if any sector in a range is marked as used, false otherwise */
int up_disk_checksectrange(const struct disk *disk, int64_t first,
                           int64_t size);

/* mark a sector as used and return 0, return -1 if already used */
const void	*up_disk_save1sect(struct disk *, int64_t,
    const struct map *, int);

/* mark range of sectors as used and return 0, return -1 if already used */
const void	*up_disk_savesectrange(struct disk *, int64_t, int64_t,
    const struct map *, int);

/* mark all sectors associated with REF unused */
void up_disk_sectsunref(struct disk *disk, const void *ref);

/* iterate through marked sectors */
/* the passed function should return 0 to stop iteration */
void up_disk_sectsiter(const struct disk *disk,
                       up_disk_iterfunc_t func, void *arg);
/* return the nth sector */
const struct disk_sect	*up_disk_nthsect(const struct disk *, int);

/* Close disk and free struct. */
void up_disk_close(struct disk *disk);

/* Print disk info to STREAM. */
void up_disk_print(const struct disk *disk, void *_stream);

/* Print hexdump of sectors with partition information to STREAM. */
void up_disk_dump(const struct disk *disk, void *_stream);

#endif /* HDR_UPART_DISK */
