#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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

struct restoredata;

static int numstr(const char *str, int *num);
static char *splitword(char *str);
static int saveimg(const struct up_disk *disk, const char *path,
                   const struct up_opts *opts);
static int dorestore(const struct up_disk *src, const char *path,
                     struct up_opts *opts);
static int iter_dorestore(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static int cmpsects(const struct up_disk *first, const struct up_disk *second,
                    int64_t start, int64_t count, const struct up_opts *opts);
static int promptparam(int64_t *val, int64_t min, int64_t max,
                       const struct up_opts *opts,
                       const char *desc, const char *help);
static void callsectlist(const struct up_disk *disk, char *sects,
                         up_disk_iterfunc_t func, void *arg);
static int iter_dumpsect(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static int iter_sectmap(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static int iter_listsect(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static int iter_countsect(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static int interactive_init(const struct up_opts *opts);
static char *interactive_nextline(const char *prompt,
                                  const struct up_opts *opts);

int
up_interactive(const struct up_disk *src, const struct up_opts *origopts)
{
    struct up_opts opts;
    char *line, *args;

    opts = *origopts;
    if(0 > interactive_init(&opts))
        return -1;

    printf("Interactive imaging and restore mode\n"
           "  (enter '?' for help at any prompt)\n"
           "Source %s: %s\n\n",
           (UP_DISK_IS_IMG(src) ? "image" : "disk"),
           UP_DISK_PATH(src));

    for(;;)
    {
        line = interactive_nextline("> ", &opts);
        if(!line)
            return 0;
        while(isspace(line[0]))
            line++;
        if(!line[0])
            continue;
        args = splitword(line);
        /* XXX show error for extra args */

        if(line[1])
        {
          usage:
            if('?' != line[0])
                printf("invalid command: %s\n", line);
            printf("Valid commands:\n"
"  ?             show this message\n"
"  d             show disk information\n"
"  h [sect#...]  show hexdump of all map sector(s)\n"
"  l             list offset and size of all map sectors\n"
"  m [sect#...]  show partition map\n"
"  p             show disk and map info\n"
"  q             quit\n"
"  r device      interactive restore from source to device\n"
"  v [level]     get or set verbosity level\n"
"  w path        write an image to path\n");
            continue;
        }

        switch(line[0])
        {
            case 'd':
            case 'D':
                up_disk_print(src, stdout, opts.verbosity);
                break;
            case 'h':
            case 'H':
                if(*args)
                    callsectlist(src, args, iter_dumpsect, NULL);
                else
                    up_disk_dump(src, stdout);
                break;
            case 'l':
            case 'L':
                {
                    int itercount = 0;
                    iter_listsect(src, NULL, NULL);
                    up_disk_sectsiter(src, iter_listsect, &itercount);
                }
                break;
            case 'm':
            case 'M':
                if(*args)
                    callsectlist(src, args, iter_sectmap, &opts);
                else
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
                return 0;
            case 'r':
            case 'R':
                if(!*args)
                    up_err("cannot restore: no device given");
                else if(0 > dorestore(src, args, &opts))
                    return 0;
                break;
            case 'v':
            case 'V':
                if(!*args)
                    printf("verbosity level is %d\n", opts.verbosity);
                else
                {
                    int level;
                    if(0 == numstr(args, &level))
                    {
                        opts.verbosity = level;
                        printf("verbosity level is now %d\n", opts.verbosity);
                    }
                }
                break;
            case 'w':
            case 'W':
                if(!*args)
                    up_err("cannot save image: no path given");
                else if(0 == saveimg(src, args, &opts))
                    printf("successfully wrote image to %s\n", args);
                break;
            default:
                goto usage;
        }
    }

    assert("woa man, I'm freakin' out!");
    return -1;
}

int
numstr(const char *str, int *ret)
{
    char *end;
    long num;

    end = NULL;
    errno = 0;
    num = strtol(str, &end, 10);
    if(errno || !end || str == end || *end)
        up_err("argument is not a number: %s", str);
    else if(INT_MIN > num || INT_MAX < num)
        up_err("number out of range: %ld", num);
    else
    {
        *ret = num;
        return 0;
    }
    return -1;
}

char *
splitword(char *str)
{
    char *ii = str;

    /* skip over any initial whitespace */
    while(*ii && isspace(*ii))
        ii++;

    /* skip over first word */
    while(*ii && !isspace(*ii))
        ii++;

    /* if we hit end of string then there are no more words,
       return zero-length string */
    if(!*ii)
        return ii;

    /* terminate string at end of first word */
    *(ii++) = '\0';

    /* skip over whitespace */
    while(*ii && isspace(*ii))
        ii++;

    /* return beginning of next word or zero-length string */
    return ii;
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

struct restoredata
{
    const struct up_disk       *dest;
    struct up_opts             *opts;
    int                         index;
    int                         count;
    int                         giveup;
};

int
dorestore(const struct up_disk *src, const char *path,
          struct up_opts *opts)
{
    struct up_disk     *dest;
    struct restoredata  data;
    int64_t             annoyance;
    int                 saved;

    dest = up_disk_open(path, opts, 0);
    if(!dest)
        return 0;
    saved = opts->verbosity;
    opts->verbosity = UP_VERBOSITY_SILENT;
    up_disk_setup(dest, opts);
    opts->verbosity = saved;
    opts->params = dest->ud_params;
    up_disk_close(dest);

    printf("Interactive restore from %s to %s:\n",
           UP_DISK_PATH(src), path);

    printf("Drive parameters for %s:\n", path);
    annoyance = opts->params.ud_sectsize;
    if(0 > promptparam(&annoyance, 0, INT_MAX, opts,
                       "sector size",
                       "size of a single sector in bytes, usually 512") ||
       0 > promptparam(&opts->params.ud_cyls, 0, INT64_MAX, opts,
                       "total cylinders count",
                       "commonly referred to just as \"cylinders\"") ||
       0 > promptparam(&opts->params.ud_heads, 0, INT64_MAX, opts,
                       "tracks per cylinder",
                       "commonly called \"heads\"") ||
       0 > promptparam(&opts->params.ud_sects, 0, INT64_MAX, opts,
                       "sectors per track",
                       "commonly just referred to as \"sectors\""))
        return 0;
    opts->params.ud_sectsize = annoyance;

    dest = up_disk_open(path, opts, 0 /* XXX */);
    if(!dest)
        return 0;
    if(0 > up_disk_setup(dest, opts))
    {
        up_disk_close(dest);
        return 0;
    }

    if(src->ud_params.ud_sectsize != dest->ud_params.ud_sectsize)
    {
        up_err("source and destination disks have different sector sizes");
        up_disk_close(dest);
        return 0;
    }

    data.dest = dest;
    data.opts = opts;
    data.index = 0;
    data.count = 0;
    data.giveup = 0;
    up_disk_sectsiter(src, iter_countsect, &data.count);

    up_disk_sectsiter(src, iter_dorestore, &data);
    up_disk_close(dest);

    return (data.giveup ? -1 : 0);
}

int
iter_dorestore(const struct up_disk *src, const struct up_disk_sectnode *sect,
               void *arg)
{
    struct restoredata *data = arg;
    int index = (data->index++);
    char *line, *args;

    switch(cmpsects(src, data->dest, UP_SECT_OFF(sect), UP_SECT_COUNT(sect),
                    data->opts))
    {
        case -1:
            return 0;
        case 0:
            printf("Skipping sector group %d, it is the same on both disks\n",
                   index);
            return 1;
    }

    for(;;)
    {
        /* XXX mixing sector group offset and count here is confusing */
        printf("\nRestoring sector group %d of %d "
               "(%"PRId64" sector%s at %"PRId64")\n"
               "  map type: %s\n", index, data->count, UP_SECT_COUNT(sect),
               (1 == UP_SECT_COUNT(sect) ? "" : "s"), UP_SECT_OFF(sect),
               up_map_label(UP_SECT_MAP(sect)));
        line = interactive_nextline("] ", data->opts);
        if(!line)
        {
            data->giveup = 1;
            return 0;
        }
        while(isspace(line[0]))
            line++;
        if(!line[0])
            continue;
        args = splitword(line);

        if(line[1])
        {
          usage:
            printf("Valid commands:\n"
"  ?             show this message\n"
"  d             diff source and destination sector(s)\n"
"  h d           show hexdump of sector(s) from destination\n"
"  h s           show hexdump of sector(s) from source\n"
"  m             show partition map containing sector(s)\n"
"  q             abort and return to main prompt\n"
"  r             restore sector group from source to destination\n"
"  v [level]     get or set verbosity level\n");
            continue;
        }

        switch(line[0])
        {
            case 'd':
            case 'D':
                printf("XXX diff not implemented\n");
                break;
            case 'h':
            case 'H':
                switch(args[0])
                {
                    case 'd':
                    case 'D':
                        printf("XXX not quite yet\n");
                        break;
                    case 's':
                    case 'S':
                        iter_dumpsect(src, sect, NULL);
                        break;
                    default:
                        printf("argument to h command must be d or s\n");
                        goto usage;
                }
                break;
            case 'm':
            case 'M':
                iter_sectmap(src, sect, (void*)data->opts);
                break;
            case 'q':
            case 'Q':
                return 0;
            case 'r':
            case 'R':
                printf("XXX just pretend I wrote the sectors, ok?\n");
                return 1;
            case 'v':
            case 'V':
                if(!*args)
                    printf("verbosity level is %d\n", data->opts->verbosity);
                else
                {
                    int level;
                    if(0 == numstr(args, &level))
                    {
                        data->opts->verbosity = level;
                        printf("verbosity level is now %d\n",
                               data->opts->verbosity);
                    }
                }
                break;
            default:
                if('?' != line[0])
                    printf("invalid command: %s\n", line);
                goto usage;
        }
    }
}

int
cmpsects(const struct up_disk *first, const struct up_disk *second,
         int64_t start, int64_t count, const struct up_opts *opts)
{
    int64_t off;
    int ii;
    const uint8_t *firstbuf, *secondbuf;

    assert(first->ud_params.ud_sectsize == second->ud_params.ud_sectsize);

    for(off = start; off < start + count; off++)
    {
        firstbuf = up_disk_getsect(first, off, opts->verbosity);
        if(!firstbuf)
            return -1;
        secondbuf = up_disk_getsect(second, off, opts->verbosity);
        if(!secondbuf)
            return -1;
        for(ii = 0; ii < first->ud_params.ud_sectsize; ii++)
            if(firstbuf[ii] != secondbuf[ii])
                return 1;
    }

    return 0;
}

int
promptparam(int64_t *val, int64_t min, int64_t max, const struct up_opts *opts,
            const char *desc, const char *help)
{
    char prompt[80];
    char *line, *end;
    long long num;

    snprintf(prompt, sizeof prompt, "%s: [%"PRId64"] ", desc, *val);

    for(;;)
    {
        line = interactive_nextline(prompt, opts);
        if(!line)
            return -1;
        while(isspace(line[0]))
            line++;
        if(!line[0])
            return 0;
        if('?' == line[0])
        {
            printf("%s\n", help);
            continue;
        }

        end = NULL;
        errno = 0;
        num = strtol(line, &end, 10);
        if(errno || !end || *end)
            up_err("failed to parse number: %s", line);
        else if(min > num || max < num)
            up_err("number out of range: %s", line);
        else
        {
            *val = num;
            return 0;
        }
    }
}

void
callsectlist(const struct up_disk *disk, char *sects,
             up_disk_iterfunc_t func, void *arg)
{
    char *num;
    int off;
    const struct up_disk_sectnode *node;

    while(*sects)
    {
        num = sects;
        sects = splitword(sects);
        if(0 == numstr(num, &off))
        {
            node = up_disk_nthsect(disk, off);
            if(!node)
                up_err("no sector at index %d", off);
            else if(0 == func(disk, node, arg))
                break;
        }
    }
}

int
iter_dumpsect(const struct up_disk *disk,
              const struct up_disk_sectnode *sect, void *arg)
{
    up_map_dumpsect(UP_SECT_MAP(sect), stdout, UP_SECT_OFF(sect),
                    UP_SECT_COUNT(sect), UP_SECT_DATA(sect), sect->tag);
    return 1;
}

int
iter_sectmap(const struct up_disk *disk,
             const struct up_disk_sectnode *sect, void *arg)
{
    const struct up_opts *opts = arg;
    up_map_print(UP_SECT_MAP(sect), stdout, opts->verbosity, 0);
    return 1;
}

int
iter_listsect(const struct up_disk *disk,
              const struct up_disk_sectnode *sect, void *arg)
{
    int *count = arg;

    if(sect)
        printf("%3d %15"PRId64" %7"PRId64" %s\n", (*count)++, UP_SECT_OFF(sect),
               UP_SECT_COUNT(sect), up_map_label(UP_SECT_MAP(sect)));
    else
        printf("  # %15s %7s Type\n", "Offset", "Sectors");

    return 1;
}

int
iter_countsect(const struct up_disk *disk,
               const struct up_disk_sectnode *sect, void *arg)
{
    int *count = arg;
    (*count)++;
    return 1;
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

char *
interactive_nextline(const char *prompt, const struct up_opts *opts)
{
    char *line = readline(prompt);

    if(!line)
        rl_crlf();
    else if(line[0])
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

char *
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
