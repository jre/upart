#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "disklabel.h"
#include "map.h"
#include "mbr.h"
#include "util.h"

static int getlabel(struct up_disk *disk, int64_t start, int64_t size, void *arg);
static char *readargs(int argc, char *argv[], struct up_opts *opts);
static void usage(const char *argv0);

int
main(int argc, char *argv[])
{
    struct up_opts              opts;
    char                       *name;
    struct up_disk             *disk;
    struct up_part              container;
    const struct up_map        *mbr;
    const struct up_part       *part;

    if(0 > up_getendian())
        return EXIT_FAILURE;
    up_mbr_register();

    name = readargs(argc, argv, &opts);
    if(NULL == name)
        return EXIT_FAILURE;

    disk = up_disk_open(name);
    if(!disk)
        return EXIT_FAILURE;

    up_disk_dump(disk, stdout, &opts);

    getlabel(disk, 0, disk->upd_size, &opts);

    switch(up_map_loadall(disk, &container))
    {
        case -1:
            return EXIT_FAILURE;
        case 0:
            break;
        case 1:
            mbr = up_map_firstmap(&container);
            if(mbr && UP_MAP_MBR == mbr->type)
            {
                fputc('\n', stdout);
                up_mbr_dump(disk, mbr, stdout, &opts);
                for(part = up_map_first(mbr); part; part = up_map_next(part))
                    getlabel(disk, part->start, part->size, &opts);
            }
            up_map_freeall(&container);
            break;
    }

    up_disk_close(disk);

    return EXIT_SUCCESS;
}

static int
getlabel(struct up_disk *disk, int64_t start, int64_t size, void *arg)
{
    void               *disklabel;
    int                 res;

    res = up_disklabel_testload(disk, start, size, &disklabel);
    if(0 < res)
    {
        fputc('\n', stdout);
        up_disklabel_dump(disk, disklabel, stdout, arg, NULL);
        up_disklabel_free(disklabel);
    }

    return res;    
}

static char *
readargs(int argc, char *argv[], struct up_opts *opts)
{
    int opt;

    memset(opts, 0, sizeof *opts);
    while(0 < (opt = getopt(argc, argv, "hv")))
    {
        switch(opt)
        {
            case 'v':
                opts->upo_verbose = 1;
                break;
            default:
                usage(argv[0]);
                return NULL;
        }
    }

    if(optind + 1 == argc)
        return argv[optind];
    else
    {
        usage(argv[0]);
        return NULL;
    }
}

static void
usage(const char *argv0)
{
    const char *name;

    if(!(name = strrchr(argv0, '/')) || !*(++name))
        name = argv0;

    printf("usage: %s [-v] device-path\n", name);
}
