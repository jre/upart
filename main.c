#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "disklabel.h"
#include "mbr.h"
#include "util.h"

static int getlabel(struct up_disk *disk, int64_t start, int64_t size,
                    const char *label, void *arg);
static char *readargs(int argc, char *argv[], struct up_opts *opts);
static void usage(const char *argv0);

int
main(int argc, char *argv[])
{
    struct up_opts      opts;
    char               *name;
    struct up_disk     *disk;
    void               *mbr;

    name = readargs(argc, argv, &opts);
    if(NULL == name)
        return EXIT_FAILURE;

    disk = up_disk_open(name);
    if(!disk)
        return EXIT_FAILURE;

    up_disk_dump(disk, stdout, &opts);

    getlabel(disk, 0, disk->upd_size, NULL, &opts);

    switch(up_mbr_testload(disk, 0, disk->upd_size, &mbr))
    {
        case -1:
            return EXIT_FAILURE;
        case 0:
            break;
        case 1:
            fputc('\n', stdout);
            up_mbr_dump(disk, mbr, stdout, &opts);
            /* XXX need generic pertition framework for iteration */
            up_mbr_iter(disk, mbr, getlabel, &opts);
            up_mbr_free(mbr);
            break;
    }

    up_disk_close(disk);

    return EXIT_SUCCESS;
}

static int
getlabel(struct up_disk *disk, int64_t start, int64_t size,
         const char *label, void *arg)
{
    void               *disklabel;
    int                 res;

    res = up_disklabel_testload(disk, start, size, &disklabel);
    if(0 < res)
    {
        fputc('\n', stdout);
        up_disklabel_dump(disk, disklabel, stdout, arg, label);
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
