#include "config.h"

#include <assert.h>
#include <ctype.h>
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
