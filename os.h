#ifndef HDR_UPART_OS
#define HDR_UPART_OS

struct up_opts;
struct up_disk;

int up_os_opendisk(const char *name, const char **path,
                   const struct up_opts *opts);
int up_os_getparams(int fd, struct up_disk *disk, const struct up_opts *opts);

#endif /* HDR_UPART_OS */
