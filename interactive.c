#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "util.h"

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

int
interactive_loop(const struct up_opts *opts)
{
#ifndef HAVE_READLINE
    char linebuf[1024];
    size_t res;
#endif
    const char *line;

#ifdef HAVE_READLINE
    rl_readline_name = PACKAGE_NAME;
    /* using_history(); */
#endif

    for(;;)
    {
#ifdef HAVE_READLINE
        /* XXX make this configurable */
        line = readline("> ");
        if(!line)
            return 0;
#else
        /* XXX need to make this line buffered */
        res = fread(linebuf, 1, sizeof linebuf - 1, stdin);
        linebuf[res] = '\0';
        if(res)
            line = linebuf;
        else
            /* XXX error message here for non-EOF case */
            return 0;
#endif

        printf("echo: %s\n", line);
    }
}
