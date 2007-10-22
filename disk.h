#ifndef HDR_UPART_DISK
#define HDR_UPART_DISK

struct up_opts;

struct up_disk
{
    char   *upd_name;           /* disk name supplied by the user */
    char   *upd_path;           /* path to opened device node or plain file */
    int     upd_fd;             /* descriptor for disk device */
    int     upd_sectsize;       /* size of a sector in bytes */
    int64_t upd_cyls;           /* total number of cylinders */
    int64_t upd_heads;          /* number of tracks per cylinder */
    int64_t upd_sects;          /* number of sectors per track */
    int64_t upd_size;           /* total number of sects */
};

/* Open the disk device read-only and get drive params */
struct up_disk *up_disk_open(const char *path);

/* Close disk */
void up_disk_close(struct up_disk *disk);

void up_disk_dump(const struct up_disk *disk, void *_stream,
                  const struct up_opts *opt);

#endif /* HDR_UPART_DISK */
