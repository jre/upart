#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdlabel.h"
#include "disk.h"
#include "map.h"
#include "mbr.h"
#include "util.h"

static char *readargs(int argc, char *argv[], struct up_opts *opts);
static void usage(const char *argv0);

int
main(int argc, char *argv[])
{
    struct up_opts              opts;
    char                       *name;
    struct up_disk             *disk;

    if(0 > up_getendian())
        return EXIT_FAILURE;
    up_mbr_register();
    up_bsdlabel_register();

    name = readargs(argc, argv, &opts);
    if(NULL == name)
        return EXIT_FAILURE;

    disk = up_disk_open(name);
    if(!disk)
        return EXIT_FAILURE;
    if(0 > up_map_loadall(disk))
    {
        up_disk_close(disk);
        return EXIT_FAILURE;
    }

    up_disk_dump(disk, stdout, &opts);
    fputc('\n', stdout);
    up_map_printall(disk, stdout, opts.upo_verbose);
    if(opts.upo_verbose)
        up_map_dumpall(disk, stdout);

    up_disk_close(disk);

    return EXIT_SUCCESS;
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
