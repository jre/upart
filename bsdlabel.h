/* 
 * Copyright (c) 2007-2010 Joshua R. Elsasser.
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

#ifndef HDR_UPART_BSDLABEL
#define HDR_UPART_BSDLABEL

struct part;

/* register BSD disklabel partition map type */
void up_bsdlabel_register(void);

#define UP_BSDLABEL_FSTYPE_UNUSED       (0)
#define UP_BSDLABEL_FSTYPE_42BSD        (7)

#define OBSDLABEL_BF_BSIZE(bf)	((bf) ? 1 << (((bf) >> 3) + 12) : 0)
#define OBSDLABEL_BF_FRAG(bf)	((bf) ? 1 << (((bf) & 7) - 1) : 0)

#define UP_BSDLABEL_FMT_HDR() \
    (UP_NOISY(EXTRA) ? "Type    fsize bsize   cpg" : \
     (UP_NOISY(NORMAL) ? "Type" :  NULL))

const char *up_bsdlabel_fstype(int type);
int	up_bsdlabel_fmt(const struct part *, int, uint32_t, int, int, FILE *);

#endif /* HDR_UPART_BSDLABEL */
