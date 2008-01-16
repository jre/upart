#ifndef HDR_UPART_DISK
#define HDR_UPART_DISK

struct up_opts;
struct up_part;

struct up_disk
{
    char *      upd_name;           /* disk name supplied by the user */
    char *      upd_path;           /* path to opened device node */
    int         upd_sectsize;       /* size of a sector in bytes */
    int64_t     upd_cyls;           /* total number of cylinders */
    int64_t     upd_heads;          /* number of tracks per cylinder */
    int64_t     upd_sects;          /* number of sectors per track */
    int64_t     upd_size;           /* total number of sects */

    /* don't touch these, use up_disk_read() or up_disk_getsect() instead */
    int         upd_fd;
    uint8_t *   upd_buf;

    /* don't touch this either, use the up_map_*all() functions */
    struct up_part     *maps;
};

/* Open the disk device read-only and get drive params */
struct up_disk *up_disk_open(const char *path, const struct up_opts *opts);

/* Read from disk into buffer. Note that START, SIZE, and the return
   value are in sectors but bufsize is in bytes. */
int64_t up_disk_read(const struct up_disk *disk, int64_t start, int64_t size,
                     void *buf, size_t bufsize);

/* Read a single sector. The returned pointer is valid until function
   is called again. */
const void *up_disk_getsect(struct up_disk *disk, int64_t sect);

/* Close disk and free struct. */
void up_disk_close(struct up_disk *disk);

/* Print disk info to STREAM. */
void up_disk_dump(const struct up_disk *disk, void *_stream,
                  const struct up_opts *opt);

#endif /* HDR_UPART_DISK */
