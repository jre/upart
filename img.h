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

#ifndef HDR_UPART_IMG
#define HDR_UPART_IMG

struct disk;
struct img;
struct disk_params;

/* serialize disk metainfo and partition sectors to a file */
int		 up_img_save(const struct disk *, FILE *, const char *,
    const char *);

int		 up_img_load(FILE *, const char *, struct img **);
void		 up_img_getparams(struct img *, struct disk_params *);
const char	*up_img_getlabel(struct img *);
int64_t		 up_img_read(struct img *, int64_t, int64_t, void *);
void		 up_img_free(struct img *);

#endif
