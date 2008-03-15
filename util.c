#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

int up_endian = 0;

int
up_getendian(void)
{
    uint32_t num = 0x04030201;
    uint8_t bufbe[4] = {4, 3, 2, 1};
    uint8_t bufle[4] = {1, 2, 3, 4};

    assert(4 == sizeof(num) && 4 == sizeof(bufbe) && 4 == sizeof(bufle));
    if(!memcmp(&num, bufbe, 4))
    {
        up_endian = UP_ENDIAN_BIG;
        return 0;
    }
    else if(!memcmp(&num, bufle, 4))
    {
        up_endian = UP_ENDIAN_LITTLE;
        return 0;
    }
    else
    {
        fprintf(stderr, "failed to determine machine byte order\n");
        return -1;
    }
}

void
up_hexdump(const void *_buf, size_t size, size_t dispoff, void *_stream)
{
    static const char   hex[] = "0123456789abcdef";
    const char *        buf = _buf;
    FILE *              stream = _stream;
    size_t              ii, jj;

    if(NULL == stream)
        stream = stderr;

    if(!size)
        return;

    for(ii = 0; size > ii; ii++)
    {
        if(!(ii % 0x10))
            fprintf(stream, "%08zx ", ii + dispoff);
        putc(' ', stream);
        putc(hex[((unsigned char)(buf[ii]) & 0xf0) >> 4], stream);
        putc(hex[(unsigned char)(buf[ii]) & 0x0f], stream);
        if(!((ii + 1) % 0x8))
            putc(' ', stream);
        if(!((ii + 1) % 0x10))
        {
            putc(' ', stream);
            putc('|', stream);
            for(jj = ii - 0xf; ii >= jj; jj++)
            {
                /* for hexdump -C compatible output, use this instead */
                /* if('\xff' == (int)(buf[jj]) || isprint(buf[jj])) */
                if(isprint((int)buf[jj]))
                    putc(buf[jj], stream);
                else
                    putc('.', stream);
            }
            putc('|', stream);
            putc('\n', stream);
        }
    }

    if(ii % 0x10)
    {
        jj = ii % 0x10;
        fprintf(stream, "%*s", (int)(3 * (0x10 - jj) + (0x8 > jj) + 1), "");
        putc(' ', stream);
        putc('|', stream);
        for(jj = ii - jj; ii > jj; jj++)
        {
            /* for hexdump -C compatible output, use this instead */
            /* if('\xff' == (int)(buf[jj]) || isprint(buf[jj])) */
            if(isprint((int)buf[jj]))
                putc(buf[jj], stream);
            else
                putc('.', stream);
        }
        putc('|', stream);
        putc('\n', stream);
    }

    fprintf(stream, "%08zx\n", size + dispoff);
}

float
up_fmtsize(int64_t num, const char **units)
{
    static const char *sizes[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    float  ret;
    size_t ii;

    ret = num;
    for(ii = 0; sizeof(sizes) / sizeof(sizes[0]) > ii && 1000.0 < ret; ii++)
        ret /= 1024.0;

    if(NULL != units)
        *units = sizes[ii];

    return ret;
}

int
up_scatprintf(char *str, size_t size, const char *format, ...)
{
    char *nul;
    va_list ap;
    int res;

    nul = memchr(str, '\0', size);
    assert(NULL != nul);
    va_start(ap, format);
    res = vsnprintf(nul, size - (nul - str), format, ap);
    va_end(ap);

    if(0 > res)
        return res;
    else
        return res + (nul - str);
}

#ifndef HAVE_STRLCAT

/*	$OpenBSD: strlcat.c,v 1.13 2005/08/08 08:05:37 espie Exp $	*/

/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
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

/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t
strlcat(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;

	if (n == 0)
		return(dlen + strlen(s));
	while (*s != '\0') {
		if (n != 1) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';

	return(dlen + (s - src));	/* count does not include NUL */
}

#endif /* HAVE_STRLCAT */
