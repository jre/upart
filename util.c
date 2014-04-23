#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "util.h"

/* Consider everything above 0x19 and belot 0x7f printable */
#define UP_ISPRINT(chr) \
    (0x60 & (int)(chr) && \
     (0x7f & ((int)(chr) + 1)) && \
     !(0x80 & (int)(chr)))

static void up_vmsg(unsigned int flags, const char *fmt, va_list ap);

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
        /* XXX should call this after parsing args so verbosity is known */
        fprintf(stderr, "failed to determine machine byte order\n");
        return -1;
    }
}

void
up_hexdump(const void *_buf, size_t size, uint64_t dispoff, void *_stream)
{
    static const char   hex[] = "0123456789abcdef";
    const uint8_t      *buf = _buf;
    FILE *              stream = _stream;
    size_t              ii, jj;

    assert(stream);

    if(!size)
        return;

    for(ii = 0; size > ii; ii++)
    {
        if(!(ii % 0x10))
            fprintf(stream, "%012"PRIx64" ", (uint64_t)ii + dispoff);
        putc(' ', stream);
        putc(hex[(buf[ii] >> 4) & 0xf], stream);
        putc(hex[buf[ii] & 0xf], stream);
        if(!((ii + 1) % 0x8))
            putc(' ', stream);
        if(!((ii + 1) % 0x10))
        {
            putc(' ', stream);
            putc('|', stream);
            for(jj = ii - 0xf; ii >= jj; jj++)
            {
                if(UP_ISPRINT(buf[jj]))
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
            if(UP_ISPRINT(buf[jj]))
                putc(buf[jj], stream);
            else
                putc('.', stream);
        }
        putc('|', stream);
        putc('\n', stream);
    }

    fprintf(stream, "%012"PRIx64"\n", (uint64_t)size + dispoff);
}

void
up_hexdiff(const void *old, size_t osize, uint64_t ooff, const char *oname,
           const void *new, size_t nsize, uint64_t noff, const char *nname,
           void *stream)
{
    printf("XXX not yet\n");
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

void *
xalloc(size_t nmemb, size_t size, unsigned int flags)
{
	void *ptr;

	if (SIZE_MAX / nmemb < size) {
		ptr = NULL;
		errno = ENOMEM;
	}
	else if (flags & XA_ZERO)
		ptr = calloc(nmemb, size);
	else
		ptr = malloc(nmemb * size);

	if (ptr == NULL) {
		if (!(flags & XA_QUIET))
			fputs("failed to allocate memory\n", stderr);
		if (flags & XA_FATAL)
			abort();
	}

	return (ptr);
}

char *
xstrdup(const char *old, unsigned int flags)
{
	size_t len;
	char *new;

	len = strlen(old);
	if ((new = xalloc(len + 1, 1, flags)) != NULL)
		memcpy(new, old, len + 1);

	return (new);
}

static char *up_savedname = NULL;

int
up_savename(const char *argv0)
{
	const char *name;
	char *new;

	if (!(name = strrchr(argv0, '/')) || !*(++name))
		name = argv0;

	if ((new = xstrdup(name, 0)) == NULL)
		return (-1);

	free(up_savedname);
	up_savedname = new;

	return (0);
}

const char *
up_getname(void)
{
    return up_savedname;
}

void
up_err(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    up_vmsg(UP_MSG_FERR, fmt, ap);
    va_end(ap);
}

void
up_warn(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    up_vmsg(UP_MSG_FWARN, fmt, ap);
    va_end(ap);
}

void
up_msg(unsigned int flags, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    up_vmsg(flags, fmt, ap);
    va_end(ap);
}

static void
up_vmsg(unsigned int flags, const char *fmt, va_list ap)
{
    if(!(flags & UP_MSG_FBARE))
    {
        if(flags & UP_MSG_FWARN)
            fprintf(stderr, "%s: warning: ", up_getname());
        else if(flags & UP_MSG_FWARN)
            fprintf(stderr, "%s: error: ", up_getname());
        else
            fprintf(stderr, "%s: ", up_getname());
    }
    vfprintf(stderr, fmt, ap);
    if(!(flags & UP_MSG_FBARE))
        putc('\n', stderr);
}

int
printsect(uint64_t num, FILE *stream)
{
	return (printsect_pad(num, 0, stream));
}

int
printsect_pad(uint64_t num, int padding, FILE *stream)
{
	struct disk *disk;
	const char *unit;
	float size;

	if (opts->humansize) {
		disk = current_disk();
		size = up_fmtsize(num * (disk == NULL ?
			512 : UP_DISK_1SECT(disk)), &unit);
		padding = MAX(0, padding - 2);
		if (stream == NULL)
			return (8);
		return (fprintf(stream, "%*.2f%-2s", padding, size, unit));
	}
	else if (opts->printhex) {
		if (stream == NULL)
			return (snprintf(NULL, 0, "%#*"PRIx64, padding, num));
		else
			return (fprintf(stream, "%#*"PRIx64, padding, num));
	}
	else {
		if (stream == NULL)
			return (snprintf(NULL, 0, "%*"PRId64, padding, num));
		else
			return (fprintf(stream, "%*"PRId64, padding, num));
	}
}

int
printsect_verbose(uint64_t num, FILE *stream)
{
	if (!opts->humansize && stream != NULL &&
	    fputs("sector ", stream) == EOF)
		return (-1);
	return (printsect_pad(num, 0, stream));
}

const struct opts *opts = NULL;

void
init_options(struct opts *opts)
{
	memset(opts, 0, sizeof(*opts));
}

void
set_options(const struct opts *new_opts)
{
	static struct opts static_opts = { 0 };

	static_opts = *new_opts;
	opts = &static_opts;
}

#ifndef HAVE_STRLCPY
#include "strlcpy.c"
#endif /* HAVE_STRLCPY */

#ifndef HAVE_STRLCAT
#include "strlcat.c"
#endif /* HAVE_STRLCAT */
