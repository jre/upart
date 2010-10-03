#ifndef HDR_UPART_OS
#define HDR_UPART_OS

struct disk_params;
enum disk_type;

typedef struct os_device_handle * os_device_handle;
typedef uintmax_t os_error;

int		 os_list_devices(FILE *);
enum disk_type	 os_dev_open(const char *, const char **, os_device_handle *);
int		 os_dev_params(os_device_handle, struct disk_params *,
    const char *);
ssize_t		 os_dev_read(os_device_handle, void *, size_t, off_t);
int		 os_dev_close(os_device_handle);
int64_t		 os_file_size(FILE *);
int		 os_handle_type(os_device_handle, enum disk_type *);
os_error	 os_lasterr(void);
const char	*os_lasterrstr(void);
const char	*os_errstr(os_error);

#endif /* HDR_UPART_OS */
