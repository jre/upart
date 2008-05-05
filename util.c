#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

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
            fprintf(stream, "%012"PRIx64" ", (uint64_t)ii + dispoff);
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

    fprintf(stream, "%012"PRIx64"\n", (uint64_t)size + dispoff);
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
up_malloc(size_t nmemb, size_t size)
{
    if(SIZE_MAX / nmemb < size)
    {
        errno = ENOMEM;
        return NULL;
    }
    else
        return malloc(nmemb * size);
}

static char *up_savedname = NULL;

int
up_savename(const char *argv0)
{
    const char *name;
    char       *new;

    if(!(name = strrchr(argv0, '/')) || !*(++name))
        name = argv0;

    new = strdup(name);
    if(!new)
    {
        perror("malloc");
        return -1;
    }

    free(up_savedname);
    up_savedname = new;

    return 0;
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
#ifdef XXXFMT
    if(!(flags & UP_MSG_FBARE))
    {
        if(flags & UP_MSG_FWARN)
            fprintf(stderr, "%s: warning: ", up_getname());
        else if(flags & UP_MSG_FWARN)
            fprintf(stderr, "%s: error: ", up_getname());
        else
            fprintf(stderr, "%s: ", up_getname());
    }
#endif
    vfprintf(stderr, fmt, ap);
    if(!(flags & UP_MSG_FBARE))
        putc('\n', stderr);
}

#ifndef HAVE_STRLCPY
#include "strlcpy.c"
#endif /* HAVE_STRLCPY */

#ifndef HAVE_STRLCAT
#include "strlcat.c"
#endif /* HAVE_STRLCAT */
