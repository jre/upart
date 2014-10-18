/* 
 * Copyright (c) 2010-2011 Joshua R. Elsasser.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef HDR_UPART_OS_PRIVATE
#define HDR_UPART_OS_PRIVATE

struct disk_params;
struct os_device_handle;

#ifdef OS_TYPE_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HANDLE os_handle;

#else

typedef int os_handle;

#endif

#define OS_HANDLE_IN(e)		((os_handle)(size_t)(e))
#define OS_HANDLE_OUT(i)	((os_device_handle)(size_t)(i))

typedef int (*os_list_callback_func)(const char *, void *);
typedef int (*os_list_func)(os_list_callback_func, void *);
typedef int (*os_open_func)(const char *, int, char *, size_t, os_handle *);
typedef int (*os_params_func)(os_handle, struct disk_params *, const char *);
typedef int (*os_desc_func)(os_handle, char *, size_t, const char *);

/* os-bsd.c */
int	os_listdev_sysctl(os_list_callback_func, void *);
int	os_opendisk_opendisk(const char *, int, char *, size_t, os_handle *);
int	os_opendisk_opendev(const char *, int, char *, size_t, os_handle *);
int	os_getparams_disklabel(os_handle, struct disk_params *, const char *);
int	os_getparams_freebsd(os_handle, struct disk_params *, const char *);
int	os_getdesc_diocinq(os_handle, char *, size_t, const char *);

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

/* os-unix.c */
int	os_opendisk_unix(const char *, int, char *, size_t, os_handle *);

/* os-windows.c */
int	os_listdev_windows(os_list_callback_func, void *);
int	os_opendisk_windows(const char *, int, char *, size_t, os_handle *);
int	os_getparams_windows(os_handle, struct disk_params *, const char *);

#define OS_GENERATE_LISTDEV_STUB(fn) \
	int fn(os_list_callback_func f, void *a) { return (0); }
#define OS_GENERATE_OPENDISK_STUB(fn) \
	int fn(const char *n, int f, char *b, size_t l, os_handle *r) { return (0); }
#define OS_GENERATE_GETPARAMS_STUB(fn) \
	int fn(os_handle h, struct disk_params *p, const char *n) { return (0); }
#define OS_GENERATE_GETDESC_STUB(fn) \
	int fn(os_handle h, char *p, size_t s, const char *n) { return (0); }

#endif /* HDR_UPART_OS_PRIVATE */
