#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "disk.h"
#include "img.h"
#include "map.h"
#include "util.h"

#ifdef HAVE_READLINE

#include <readline/readline.h>
#include <readline/history.h>

#else /*  HAVE_READLINE */

#include <assert.h>

static size_t nrl_getline(char *buf, size_t size, FILE *stream);

#endif /*  HAVE_READLINE */

static int saveimg(const struct up_disk *disk, const char *path,
                   const struct up_opts *opts);
static void iter_listsect(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static int interactive_init(const struct up_opts *opts);
static const char *interactive_nextline(const char *prompt,
                                        const struct up_opts *opts);

int
interactive_image(struct up_disk *src, const char *dest,
                  const struct up_opts *origopts)
{
    struct up_opts opts;
    const char *line;
    int done, itercount;

    opts = *origopts;
    if(0 > interactive_init(&opts))
        return -1;

    printf("Interactive imaging mode (enter '?' for help)\n"
           "Source %s: %s\n"
           "Destination image: %s\n\n",
           (UP_DISK_IS_IMG(src) ? "image" : "disk"),
           UP_DISK_PATH(src),
           (dest ? dest : "none (read-only mode)"));

    done = 0;
    while(!done)
    {
        /* XXX make prompt configurable */
        line = interactive_nextline("> ", &opts);
        if(!line)
            return 0;
        if(!line[0])
            continue;

        switch(line[0])
        {
            case 'd':
            case 'D':
                up_disk_print(src, stdout, opts.verbosity);
                break;
            case 'h':
            case 'H':
                up_disk_dump(src, stdout);
                break;
            case 'l':
            case 'L':
                if(UP_NOISY(opts.verbosity, NORMAL))
                {
                    itercount = 0;
                    iter_listsect(src, NULL, NULL);
                    up_disk_sectsiter(src, iter_listsect, &itercount);
                }
                break;
            case 'm':
            case 'M':
                up_map_printall(src, stdout, opts.verbosity);
                break;
            case 'p':
            case 'P':
                up_disk_print(src, stdout, opts.verbosity);
                up_map_printall(src, stdout, opts.verbosity);
                if(UP_NOISY(opts.verbosity, SPAM))
                    up_disk_dump(src, stdout);
                break;
            case 'q':
            case 'Q':
                opts.verbosity--;
                printf("verbosity level is now %d\n", opts.verbosity);
                break;
            case 'v':
            case 'V':
                opts.verbosity++;
                printf("verbosity level is now %d\n", opts.verbosity);
                break;
            case 'w':
            case 'W':
                if(dest)
                {
                    if(0 == saveimg(src, dest, &opts))
                        printf("successfully wrote image to %s\n", dest);
                }
                else if(UP_NOISY(opts.verbosity, QUIET))
                    up_err("cannot save image: no destination path");
                break;
            case 'x':
            case 'X':
                done = 1;
                break;
            default:
                if('?' != line[0])
                    printf("invalid command: %c\n", line[0]);
                printf("Valid commands:\n"
"  ?  show this message\n"
"  d  print disk information\n"
"  h  print hexdump of all map sectors\n"
"  l  list offset and size of all map sectors\n"
"  m  print partition map\n"
"  p  print disk and map info\n"
"  q  decrease verbosity level\n"
"  v  increase verbosity level\n"
"  w  write image to destination file\n"
"  x  exit\n");
                break;
        }
    }

    return 0;
}

int
saveimg(const struct up_disk *disk, const char *path,
        const struct up_opts *opts)
{
    FILE *out;

    out = fopen(path, "wb");
    if(!out)
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_err("failed to open file for writing: %s: %s",
                   path, strerror(errno));
        return -1;
    }

    if(0 > up_img_save(disk, out, UP_DISK_LABEL(disk), path, opts))
    {
        fclose(out);
        return -1;
    }

    if(fclose(out))
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_err("failed to write to file: %s: %s",
                   path, strerror(errno));
        return -1;
    }

    return 0;
}

void
iter_listsect(const struct up_disk *disk,
              const struct up_disk_sectnode *sect, void *arg)
{
    int *count = arg;

    if(sect)
        printf("%3d %15"PRId64" %7"PRId64" %s\n", (*count)++, sect->first,
               sect->last - sect->first + 1, up_map_label(sect->ref));
    else
        printf("  # %15s %7s Type\n", "Offset", "Sectors");
}

#ifdef HAVE_READLINE

int
interactive_init(const struct up_opts *opts)
{
    rl_readline_name = PACKAGE_NAME;
    using_history();
    if(0 != rl_initialize())
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_err("failed to initialize readline");
        return -1;
    }
    return 0;
}

const char *
interactive_nextline(const char *prompt, const struct up_opts *opts)
{
    char *line = readline(prompt);

    if(line && line[0])
        add_history(line);

    return line;
}

#else /* HAVE_READLINE */

int
interactive_init(const struct up_opts *opts)
{
    /* this seems to be done automatically but it can't hurt to make sure */
    setlinebuf(stdin);

    return 0;
}

const char *
interactive_nextline(const char *prompt, const struct up_opts *opts)
{
    static char buf[1024];
    char *nl;
    size_t res;

    if(prompt)
        printf(prompt);

    res = nrl_getline(buf, sizeof buf, stdin);
    if(!res)
    {
        if(ferror(stdin) && UP_NOISY(opts->verbosity, QUIET))
            up_err("failed to read from standard input");
        return NULL;
    }

    nl = strchr(buf, '\n');
    if(nl)
        *nl = '\0';
    else if(!feof(stdin))
    {
        if(UP_NOISY(opts->verbosity, QUIET))
            up_warn("truncating line to %zu characters", sizeof(buf) - 1);
        nrl_getline(NULL, 0, stdin);
    }

    return buf;
}

/*
  Read at most SIZE-1 bytes from STREAM into BUF, up until reading
  end-of-file or a newline character.  If no buffer is provided then
  characters up to end-of-file or a newline will be read and
  discarded.  The buffer will always be nul-terminated, and will
  include a trailing newline unless end-of-file or the end of the
  buffer is encountered.  Returns the number of characters read, which
  is equal to the offset of the nul terminator.
 */
size_t
nrl_getline(char *buf, size_t size, FILE *stream)
{
    size_t off;
    int chr;

    assert(!buf || size);

    off = 0;
    while(!buf || size > off + 1)
    {
        chr = getc(stdin);
        if(EOF == chr)
            break;
        if(buf)
            buf[off] = chr;
        off++;
        if('\n' == chr)
            break;
    }

    if(buf)
        buf[off] = '\0';

    return off;
}

#endif /*  HAVE_READLINE */
