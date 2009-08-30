#ifndef HDR_UPART_OS
#define HDR_UPART_OS

struct up_disk;

int	up_os_opendisk(const char *, const char **);
int	up_os_getparams(int, struct up_disk *);

#endif /* HDR_UPART_OS */
