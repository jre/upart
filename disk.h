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

struct up_disk *up_disk_new(const char *path);
int up_disk_load(struct up_disk *disk);
void up_disk_free(struct up_disk *disk);

#endif /* HDR_UPART_DISK */
