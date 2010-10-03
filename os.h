#ifndef HDR_UPART_OS
#define HDR_UPART_OS

struct disk_params;
enum disk_type;

int	os_list_devices(FILE *);
enum disk_type	os_open_device(const char *, const char **, int *);
int	up_os_getparams(int, struct disk_params *, const char *);

#endif /* HDR_UPART_OS */
