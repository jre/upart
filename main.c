#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "mbr.h"

#define BESTDECIMAL(d)          (10.0 > (d) ? 2 : (100.0 > (d) ? 1 : 0))

static void usage(const char *argv0);
static float fmtsize(int64_t num, const char **units);

int
main(int argc, char *argv[])
{
    int                 fd;
    struct up_disk *    disk;
    const char *        unit;
    float               size;
    void *              mbr;

    if(2 != argc)
        usage(argv[0]);

    fd = up_disk_open(argv[1]);
    if(0 > fd)
        return EXIT_FAILURE;
    disk = up_disk_load(argv[1], fd);
    if(!disk)
        return EXIT_FAILURE;

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

    puts("");
    switch(up_mbr_testload(disk, fd, 0, disk->upd_size, &mbr))
    {
        case -1:
            return EXIT_FAILURE;
        case 0:
            printf("no MBR found on %s\n", disk->upd_path);
            break;
        case 1:
            up_mbr_dump(mbr, stdout);
            break;
    }

    up_disk_close(fd);
    up_mbr_free(mbr);
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
