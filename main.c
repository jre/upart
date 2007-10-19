#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"

#define BESTDECIMAL(d)          (10.0 > (d) ? 2 : (100.0 > (d) ? 1 : 0))

static void usage(const char *argv0);
static float fmtsize(int64_t num, const char **units);

int
main(int argc, char *argv[])
{
    struct up_disk *    disk;
    const char *        unit;
    float               size;

    if(2 != argc)
        usage(argv[0]);

    disk = up_disk_new(argv[1]);
    if(!disk)
        return EXIT_FAILURE;
    if(0 > up_disk_load(disk))
    {
        up_disk_free(disk);
        return EXIT_FAILURE;
    }
    size = fmtsize(disk->upd_size * (int64_t)disk->upd_sectsize, &unit);
    printf("%s\n"
           "  sector size: %d\n"
           "  cylinders:   %d\n"
           "  heads:       %d\n"
           "  sectors:     %d\n"
           "  size:        %.*f%s\n",
           disk->upd_path, disk->upd_sectsize, (int)disk->upd_cyls,
           (int)disk->upd_heads, (int)disk->upd_sects,
           BESTDECIMAL(size), size, unit);
    up_disk_free(disk);
    return EXIT_SUCCESS;
}

static void
usage(const char *argv0)
{
    const char *name;

    if(!(name = strrchr(argv0, '/')) || !*(++name))
        name = argv0;

    printf("usage: %s device-path\n", name);

    exit(EXIT_FAILURE);
}

static float
fmtsize(int64_t num, const char **units)
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
