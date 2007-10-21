#ifndef HDR_UPART_DISK
#define HDR_UPART_DISK

struct up_disk
{
    char   *upd_path;           /* path to device node or plain file */
    int     upd_sectsize;       /* size of a sector in bytes */
    int64_t upd_cyls;           /* total number of cylinders */
    int64_t upd_heads;          /* number of tracks per cylinder */
    int64_t upd_sects;          /* number of sectors per track */
    int64_t upd_size;           /* total number of sects */
};

/* Open the disk device or file read-only and return descriptor */
int up_disk_open(const char *path);

/* Close disk descriptor */
void up_disk_close(int fd);

/* Load disk params and return newly allocated struct up_disk */
struct up_disk *up_disk_load(const char *path, int fd);

/* Free struct up_disk */
void up_disk_free(struct up_disk *disk);

#endif /* HDR_UPART_DISK */
