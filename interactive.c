#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "util.h"

#ifdef HAVE_READLINE

#include <readline/readline.h>
#include <readline/history.h>

#else /*  HAVE_READLINE */

#include <assert.h>
#include <string.h>

static size_t nrl_getline(char *buf, size_t size, FILE *stream);

#endif /*  HAVE_READLINE */

static int interactive_init(const struct up_opts *opts);
static const char *interactive_nextline(const char *prompt,
                                        const struct up_opts *opts);

int
interactive_loop(const struct up_opts *opts)
{
    const char *line;

    if(0 > interactive_init(opts))
        return -1;

    for(;;)
    {
        /* XXX make this configurable */
        line = interactive_nextline("> ", opts);
        if(!line)
            return 0;

        printf("echo: %s\n", line);
    }
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
    else
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
