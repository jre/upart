#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "apm.h"
#include "bsdlabel.h"
#include "disk.h"
#include "gpt.h"
#include "img.h"
#include "map.h"
#include "mbr.h"
#include "util.h"
#include "sunlabel-x86.h"

static char *readargs(int argc, char *argv[], struct up_opts *opts);
static void usage(const char *argv0, const char *fmt, ...);
static int serialize(const struct up_disk *disk, const struct up_opts *opts);

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
    up_apm_register();
    up_sunlabel_x86_register();
    up_gpt_register();

    name = readargs(argc, argv, &opts);
    if(NULL == name)
        return EXIT_FAILURE;

    disk = up_disk_open(name, &opts);
    if(!disk)
        return EXIT_FAILURE;
    if(0 > up_map_loadall(disk, &opts))
    {
        up_disk_close(disk);
        return EXIT_FAILURE;
    }

    if(opts.serialize)
    {
        if(0 > serialize(disk, &opts))
        {
            up_disk_close(disk);
            return EXIT_FAILURE;
        }
    }
    else
    {
        up_disk_print(disk, stdout, opts.verbosity);
        up_map_printall(disk, stdout, opts.verbosity);
        if(UP_NOISY(opts.verbosity, SPAM))
            up_disk_dump(disk, stdout);
    }

    up_disk_close(disk);

    return EXIT_SUCCESS;
}

static char *
readargs(int argc, char *argv[], struct up_opts *opts)
{
    int opt;

    memset(opts, 0, sizeof *opts);
    while(0 < (opt = getopt(argc, argv, "c:fh:l:qrs:vw:z:")))
    {
        switch(opt)
        {
            case 'c':
                opts->cyls = strtol(optarg, NULL, 0);
                if(0 >= opts->cyls)
                {
                    usage(argv[0], "illegal cylinder count: %s", optarg);
                    return NULL;
                }
                break;
            case 'f':
                opts->plainfile = 1;
                break;
            case 'h':
                opts->heads = strtol(optarg, NULL, 0);
                if(0 >= opts->heads)
                {
                    usage(argv[0], "illegal tracks per cylinder (head) count: %s", optarg);
                    return NULL;
                }
                break;
            case 'l':
                opts->label = optarg;
                break;
            case 'q':
                opts->verbosity--;
                break;
            case 'r':
                opts->relaxed = 1;
                break;
            case 's':
                opts->sects = strtol(optarg, NULL, 0);
                if(0 >= opts->sects)
                {
                    usage(argv[0], "illegal sectors per track count (sectors): %s", optarg);
                    return NULL;
                }
                break;
            case 'v':
                opts->verbosity++;
                break;
            case 'w':
                opts->serialize = optarg;
                break;
            case 'z':
                opts->sectsize = strtol(optarg, NULL, 0);
                if(0 >= opts->sectsize)
                {
                    usage(argv[0], "illegal sector size: %s", optarg);
                    return NULL;
                }
                break;
            default:
                usage(argv[0], NULL);
                return NULL;
        }
    }

    if(opts->label && !opts->serialize)
    {
        usage(argv[0], "-w is required for -l");
        return NULL;
    }
    if(optind + 1 == argc)
        return argv[optind];
    else
    {
        usage(argv[0], NULL);
        return NULL;
    }
}

static void
usage(const char *argv0, const char *message, ...)
{
    const char *name;
    va_list ap;

    if(!(name = strrchr(argv0, '/')) || !*(++name))
        name = argv0;

    if(message)
    {
        va_start(ap, message);
        vprintf(message, ap);
        va_end(ap);
        fputc('\n', stdout);
    }

    printf("usage: %s [options] path\n"
           "  -c cyls   total number of cylinders (cylinders)\n"
           "  -f        path is a plain file and not a device\n"
           "  -h heads  number of tracks per cylinder (heads)\n"
           "  -l label  label to use with -w option\n"
           "  -q        lower verbosity level when printing maps\n"
           "  -r        relax some checks when reading maps\n"
           "  -s sects  number of sectors per track (sectors)\n"
           "  -v        raise verbosity level when printing maps\n"
           "  -w file   write disk and partition info to file\n"
           "  -z size   sector size in bytes\n", name);
}

static int
serialize(const struct up_disk *disk, const struct up_opts *opts)
{
    FILE *out;

    out = fopen(opts->serialize, "wb");
    if(!out)
    {
        fprintf(stderr, "failed to open file for writing: %s: %s\n",
                opts->serialize, strerror(errno));
        return -1;
    }

    if(0 > up_img_save(disk, out, (opts->label ? opts->label : disk->upd_path),
                       opts->serialize))
    {
        fclose(out);
        return -1;
    }

    if(fclose(out))
    {
        fprintf(stderr, "failed to write to file: %s: %s\n",
                opts->serialize, strerror(errno));
        return -1;
    }

    return 0;
}
