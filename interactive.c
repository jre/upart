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
static void dorestore(const struct up_disk *src, const char *path,
                      const struct up_opts *opts);
static void iter_dorestore(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static void callsectlist(const struct up_disk *disk, char *sects,
                         void (*func)(const struct up_disk *,
                                      const struct up_disk_sectnode *, void *),
                         void *arg);
static void iter_dumpsect(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static void iter_sectmap(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static void iter_listsect(const struct up_disk *disk,
                          const struct up_disk_sectnode *sect, void *arg);
static int interactive_init(const struct up_opts *opts);
static char *interactive_nextline(const char *prompt,
                                  const struct up_opts *opts);

int
up_interactive(struct up_disk *src, const struct up_opts *origopts)
{
    struct up_opts opts;
    char *line, *args;

    opts = *origopts;
    if(0 > interactive_init(&opts))
        return -1;

    printf("Interactive imaging and restore mode (enter '?' for help)\n"
           "Source %s: %s\n\n",
           (UP_DISK_IS_IMG(src) ? "image" : "disk"),
           UP_DISK_PATH(src));

    for(;;)
    {
        /* XXX make prompt configurable */
        line = interactive_nextline("> ", &opts);
        if(!line)
            return 0;
        while(isspace(line[0]))
            line++;
        if(!line[0])
            continue;
        args = splitword(line);

        if(line[1])
        {
          usage:
            if('?' != line[0])
                printf("invalid command: %s\n", line);
            printf("Valid commands:\n"
"  ?             show this message\n"
"  d             print disk information\n"
"  h [sect#...]  print hexdump of all map sector(s)\n"
"  l             list offset and size of all map sectors\n"
"  m [sect#...]  print partition map\n"
"  p             print disk and map info\n"
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
                else
                    dorestore(src, args, &opts);
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
    num = strtol(str, &end, 10);
    if(!end || str == end || *end)
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
    struct up_disk             *dest;
    const struct up_opts       *opts;
    int                         index;
};

void
dorestore(const struct up_disk *src, const char *path,
          const struct up_opts *opts)
{
    struct restoredata data;

    /* XXX silently open ro to get parameters, then prompt to see if
       correct */
    data.dest = up_disk_open(path, opts, 0 /* XXX */);
    if(0 > up_disk_setup(data.dest, opts))
    {
        up_disk_close(data.dest);
        return;
    }
    if(!data.dest)
        return;

    data.opts = opts;
    data.index = 0;
    up_disk_sectsiter(src, iter_dorestore, &data);

    up_disk_close(data.dest);
}

void
iter_dorestore(const struct up_disk *disk, const struct up_disk_sectnode *sect,
               void *arg)
{
    struct restoredata *data = arg;
    int index = (data->index++);

    printf("XXX prompt restore index %d sector %"PRId64" count %"PRId64"\n",
           index, sect->first, sect->last - sect->first + 1);
}

void
callsectlist(const struct up_disk *disk, char *sects,
             void (*func)(const struct up_disk *,
                          const struct up_disk_sectnode *, void *), void *arg)
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
            if(node)
                func(disk, node, arg);
            else
                up_err("no sector at index %d", off);
        }
    }
}

void
iter_dumpsect(const struct up_disk *disk,
              const struct up_disk_sectnode *sect, void *arg)
{
    up_map_dumpsect(sect->ref, stdout, sect->first,
                    sect->last - sect->first + 1, sect->data, sect->tag);
}

void
iter_sectmap(const struct up_disk *disk,
             const struct up_disk_sectnode *sect, void *arg)
{
    const struct up_opts *opts = arg;
    up_map_print(sect->ref, stdout, opts->verbosity, 0);
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

char *
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
