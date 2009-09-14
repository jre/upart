#ifndef HDR_UPART_OS
#define HDR_UPART_OS

struct disk_params;

int	os_list_devices(void *);
int	up_os_opendisk(const char *, const char **);
int	up_os_getparams(int, struct disk_params *, const char *);

#endif /* HDR_UPART_OS */
