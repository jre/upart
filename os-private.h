#ifndef HDR_UPART_OS_PRIVATE
#define HDR_UPART_OS_PRIVATE

#include <limits.h>

struct disk_params {
	int64_t cyls;		/* total number of cylinders */
	int64_t heads;		/* number of tracks per cylinder */
	int64_t sects;		/* number of sectors per track */
	int64_t size;		/* total number of sects */
	int sectsize;		/* size of a sector in bytes */
};

/* os-bsd.c */
int	os_listdev_sysctl(int (*)(const char *, void *), void *);
int	os_opendisk_opendisk(const char *, int, char *, size_t, int);
int	os_opendisk_opendev(const char *, int, char *, size_t, int);
int	os_getparams_disklabel(int, struct disk_params *, const char *);
int	os_getparams_freebsd(int, struct disk_params *, const char *);

/* os-darwin.c */
int	os_listdev_iokit(int (*)(const char *, void *), void *);
int	os_getparams_darwin(int, struct disk_params *, const char *);

/* os-haiku.c */
int	os_listdev_haiku(int (*)(const char *, void *), void *);
int	os_opendisk_haiku(const char *, int, char *, size_t, int);
int	os_getparams_haiku(int, struct disk_params *, const char *);

/* os-linux.c */
int	os_listdev_linux(int (*)(const char *, void *), void *);
int	os_getparams_linux(int, struct disk_params *, const char *);

/* os-solaris.c */
int	os_listdev_solaris(int (*)(const char *, void *), void *);
int	os_opendisk_solaris(const char *, int, char *, size_t, int);
int	os_getparams_solaris(int, struct disk_params *, const char *);

#define OS_GENERATE_LISTDEV_STUB(fn) \
	int fn(int (*f)(const char *, void *), void *a) { return (-1); }
#define OS_GENERATE_OPENDISK_STUB(fn) \
	int fn(const char *n, int f, char *b, size_t l, int c) { return (INT_MAX); }
#define OS_GENERATE_GETPARAMS_STUB(fn) \
	int fn(int f, struct disk_params *p, const char *n) { return (INT_MAX); }

#endif /* HDR_UPART_OS_PRIVATE */
