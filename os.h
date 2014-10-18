/* 
 * Copyright (c) 2008-2011 Joshua R. Elsasser.
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

#ifndef HDR_UPART_OS
#define HDR_UPART_OS

struct disk_params;
enum disk_type;

typedef struct os_device_handle * os_device_handle;
typedef int os_error;

int		 os_list_devices(FILE *);
enum disk_type	 os_dev_open(const char *, const char **, os_device_handle *);
int		 os_dev_params(os_device_handle, struct disk_params *,
    const char *);
int		 os_dev_desc(os_device_handle, char *, size_t, const char *);
ssize_t		 os_dev_read(os_device_handle, void *, size_t, int64_t);
int		 os_dev_close(os_device_handle);
int64_t		 os_file_size(FILE *);
int		 os_handle_type(os_device_handle, enum disk_type *);
int		 os_open_flags(const char *);
os_error	 os_lasterr(void);
void		 os_setlasterr(os_error);
const char	*os_lasterrstr(void);
const char	*os_errstr(os_error);

#endif /* HDR_UPART_OS */
