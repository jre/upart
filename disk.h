#ifndef HDR_UPART_DISK
#define HDR_UPART_DISK

#include "bsdtree.h"
#include "os-private.h"

struct map;
struct part;
struct img;

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

struct disk {
	char *name;		/* disk name supplied by user */
	char *path;		/* path to opened device node */
	struct disk_params params;

	unsigned int f_setup : 1;
	unsigned int f_plainfile : 1;

	/* don't touch any of these */
	int fd;
	uint8_t *buf;
	struct img *img;
	struct part *maps;
	struct disk_sect_map sectsused;
	int64_t sectsused_count;
};

#define UP_DISK_LABEL(disk)     ((disk)->name)
#define UP_DISK_PATH(disk)      ((disk)->path)
#define UP_DISK_1SECT(disk)     ((disk)->params.sectsize)
#define UP_DISK_CYLS(disk)      ((disk)->params.cyls)
#define UP_DISK_HEADS(disk)     ((disk)->params.heads)
#define UP_DISK_SPT(disk)       ((disk)->params.sects)
#define UP_DISK_SIZESECTS(disk) ((disk)->params.size)
/* XXX should handle overflow here and anywhere sects are converted to bytes */
#define UP_DISK_SIZEBYTES(disk) \
    ((disk)->params.size * (disk)->params.sectsize)
#define UP_DISK_IS_IMG(disk)    (NULL != (disk)->img)
#define UP_DISK_IS_FILE(disk)   ((disk)->f_plainfile)

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
