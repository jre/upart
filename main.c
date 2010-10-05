#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif


#include "apm.h"
#include "bsdlabel.h"
#include "disk.h"
#include "gpt.h"
#include "img.h"
#include "map.h"
#include "mbr.h"
#include "os.h"
#include "util.h"
#include "sunlabel-sparc.h"
#include "sunlabel-x86.h"

static char	*readargs(int, char *[], struct opts *, struct disk_params *);
static void	 usage(const char *, ...);
static int	 serialize(const struct disk *);

int
main(int argc, char *argv[])
{
	struct disk_params params;
	struct opts newopts;
	char *name;
	struct disk *disk;
	int ret;

	if (up_savename(argv[0]) < 0 ||
	    up_getendian() < 0)
		return (EXIT_FAILURE);
	up_mbr_register();
	up_bsdlabel_register();
	up_apm_register();
	up_sunlabel_sparc_register();
	up_sunlabel_x86_register();
	up_gpt_register();

	name = readargs(argc, argv, &newopts, &params);
	if (name == NULL)
		return (EXIT_FAILURE);
	set_options(&newopts);

	disk = up_disk_open(name);
	if (!disk)
		return (EXIT_FAILURE);
	if (up_disk_setup(disk, &params) < 0 ||
	    up_map_loadall(disk) < 0) {
		up_disk_close(disk);
		return (EXIT_FAILURE);
	}

	ret = EXIT_SUCCESS;
	if (opts->serialize) {
		if (serialize(disk) < 0)
			ret = (EXIT_FAILURE);
	} else {
		up_disk_print(disk, stdout);
		up_map_printall(disk, stdout);
		if (UP_NOISY(SPAM))
			up_disk_dump(disk, stdout);
	}

	up_disk_close(disk);

	return (ret);
}

static char *
readargs(int argc, char *argv[], struct opts *newopts,
    struct disk_params *params)
{
	int opt;

	init_options(newopts);
	memset(params, 0, sizeof *params);
	while(0 < (opt = getopt(argc, argv, "C:fhH:klL:qrsS:vVw:xz:"))) {
		switch(opt) {
		case 'C':
			params->cyls = strtol(optarg, NULL, 0);
			if (0 >= params->cyls)
				usage("illegal cylinder count: %s", optarg);
			break;
		case 'f':
			newopts->plainfile = 1;
			break;
		case 'h':
			newopts->humansize = 1;
			break;
		case 'H':
			params->heads = strtol(optarg, NULL, 0);
			if (0 >= params->heads)
				usage("illegal tracks per cylinder (ie: head) "
				    "count: %s", optarg);
			break;
		case 'k':
			newopts->sloppyio = 1;
			break;
		case 'l':
			os_list_devices(stdout);
			exit(EXIT_SUCCESS);
			break;
		case 'L':
			newopts->label = optarg;
			break;
		case 'q':
			newopts->verbosity--;
			break;
		case 'r':
			newopts->relaxed = 1;
			break;
		case 's':
			newopts->swapcols = 1;
			break;
		case 'S':
			params->sects = strtol(optarg, NULL, 0);
			if (0 >= params->sects)
				usage("illegal sectors per track count (sectors): %s", optarg);
			break;
		case 'v':
			newopts->verbosity++;
			break;
		case 'V':
			printf("%s version %s\n"
			    "Copyright (c) 2007-2010 Joshua R. Elsasser\n",
			    PACKAGE_NAME, PACKAGE_VERSION);
			exit(EXIT_SUCCESS);
			break;
		case 'w':
			newopts->serialize = optarg;
			break;
		case 'x':
			newopts->printhex = 1;
			break;
		case 'z':
			params->sectsize = strtol(optarg, NULL, 0);
			if (0 >= params->sectsize)
				usage("illegal sector size: %s", optarg);
			break;
		default:
			usage(NULL);
			break;
		}
	}

	if (newopts->label && !newopts->serialize)
		usage("-w is required for -l");
	if (optind + 1 == argc)
		return (argv[optind]);
	else
		usage(NULL);
	return (NULL);
}

static void
usage(const char *message, ...)
{
	va_list ap;

	if (message != NULL) {
		va_start(ap, message);
		vprintf(message, ap);
		va_end(ap);
		fputc('\n', stdout);
	}

	printf("usage: %s [options] path\n"
	    "  -C cyls   total number of cylinders (cylinders)\n"
	    "  -f        path is a plain file and not a device\n"
	    "  -h        show human-readable sizes\n"
	    "  -H heads  number of tracks per cylinder (heads)\n"
	    "  -k        keep going after I/O errors\n"
	    "  -l        list valid disk devices and exit\n"
	    "  -L label  label to use with -w option\n"
	    "  -q        lower verbosity level when printing maps\n"
	    "  -s        swap start and size columns\n"
	    "  -r        relax some checks when reading maps\n"
	    "  -S sects  number of sectors per track (sectors)\n"
	    "  -v        raise verbosity level when printing maps\n"
	    "  -V        display the version of %s and exit\n"
	    "  -w file   write disk and partition info to file\n"
	    "  -x        display numbers in hexadecimal\n"
	    "  -z size   sector size in bytes\n",
	    up_getname(), PACKAGE_NAME);

	exit(EXIT_FAILURE);
}

static int
serialize(const struct disk *disk)
{
	FILE *out;
	const char *label;

	out = fopen(opts->serialize, "wb");
	if (out == NULL) {
		if (UP_NOISY(QUIET))
			up_err("failed to open file for writing: %s: %s",
			    opts->serialize, os_lasterrstr());
		return (-1);
	}

	if (opts->label != NULL)
		label = opts->label;
	else
		label = UP_DISK_LABEL(disk);
	if (up_img_save(disk, out, label, opts->serialize) < 0) {
		fclose(out);
		return (-1);
	}

	if (fclose(out)) {
		if (UP_NOISY(QUIET))
			up_err("failed to write to file: %s: %s",
			    opts->serialize, os_lasterrstr());
		return (-1);
	}

	return (0);
}
