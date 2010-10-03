#ifndef HDR_UPART_OS_PRIVATE
#define HDR_UPART_OS_PRIVATE

struct disk_params;
struct os_device_handle;

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define OS_DEVTYPE_WINDOWS
typedef HANDLE os_handle;

#else

#define OS_DEVTYPE_UNIX
typedef int os_handle;

#endif

typedef int (*os_list_callback_func)(const char *, void *);
typedef int (*os_list_func)(os_list_callback_func, void *);
typedef int (*os_open_func)(const char *, int, char *, size_t, os_handle *);
typedef int (*os_params_func)(os_handle, struct disk_params *, const char *);

/* os-bsd.c */
int	os_listdev_sysctl(os_list_callback_func, void *);
int	os_opendisk_opendisk(const char *, int, char *, size_t, os_handle *);
int	os_opendisk_opendev(const char *, int, char *, size_t, os_handle *);
int	os_getparams_disklabel(os_handle, struct disk_params *, const char *);
int	os_getparams_freebsd(os_handle, struct disk_params *, const char *);

/* os-darwin.c */
int	os_listdev_iokit(os_list_callback_func, void *);
int	os_getparams_darwin(os_handle, struct disk_params *, const char *);

/* os-haiku.c */
int	os_listdev_haiku(os_list_callback_func, void *);
int	os_opendisk_haiku(const char *, int, char *, size_t, os_handle *);
int	os_getparams_haiku(os_handle, struct disk_params *, const char *);

/* os-linux.c */
int	os_listdev_linux(os_list_callback_func, void *);
int	os_getparams_linux(os_handle, struct disk_params *, const char *);

/* os-solaris.c */
int	os_listdev_solaris(os_list_callback_func, void *);
int	os_opendisk_solaris(const char *, int, char *, size_t, os_handle *);
int	os_getparams_solaris(os_handle, struct disk_params *, const char *);

#define OS_GENERATE_LISTDEV_STUB(fn) \
	int fn(os_list_callback_func f, void *a) { return (0); }
#define OS_GENERATE_OPENDISK_STUB(fn) \
	int fn(const char *n, int f, char *b, size_t l, os_handle *r) { return (0); }
#define OS_GENERATE_GETPARAMS_STUB(fn) \
	int fn(os_handle h, struct disk_params *p, const char *n) { return (0); }

#endif /* HDR_UPART_OS_PRIVATE */
