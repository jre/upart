AC_INIT([upart], [0.1.2])
dnl need 2.60 for AC_PROG_MKDIR_P
AC_PREREQ([2.60])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([common.mk])

dnl default to some sane CFLAGS
if test -z "$CFLAGS"
then
    CFLAGS="-g -Wall -Werror -O"
fi

dnl check for --with-readline option
readline_enabled=yes
readline_prefix=
AC_ARG_WITH([readline],
            [  --with-readline         use readline for interactive mode],
            [],
            [with_readline=check])

dnl ugh, we should test if _GNU_SOURCE is needed instead of using it everywhere
AC_DEFINE([_GNU_SOURCE])

AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MKDIR_P

dnl check for crap needed for 64-bit off_t and such
AC_SYS_LARGEFILE

dnl some standard headers
AC_CHECK_HEADERS([sys/types.h sys/param.h stdint.h inttypes.h])

dnl bsd disklabel headers
AC_CHECK_HEADERS([sys/disklabel.h])
AC_CHECK_HEADERS([sys/dkio.h],,,
[#include <sys/types.h>
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/disklabel.h>
#endif])

dnl freebsd headers
AC_CHECK_HEADERS([sys/disk.h])

dnl look for opendisk() in libutil
AC_SEARCH_LIBS([opendisk], [util],
[AC_CHECK_HEADERS([util.h])
AC_DEFINE([HAVE_OPENDISK])])

dnl some linux headers
AC_CHECK_HEADERS([linux/fs.h linux/hdreg.h])

dnl look for sane string functions
AC_CHECK_FUNCS([strlcpy strlcat])

dnl try to find readline unless instructed otherwise
if test xno != x"$with_readline"
then
    AC_SEARCH_LIBS(tgetent, [termcap tinfo curses ncurses])
    if test xno = x"$ac_cv_search_tgetent"
    then
        if test xcheck != x"$with_readline"
        then
            AC_MSG_ERROR([failed to find termcap library, needed by readline])
        fi
    else
        AC_SEARCH_LIBS([readline], [readline])
        if test xno = x"$ac_cv_search_readline"
        then
            if test xcheck != x"$with_readline"
            then
                AC_ERROR([failed to find readline])
            fi
            # XXX should probably remove termcap lib here
        else
            AC_DEFINE([HAVE_READLINE])
        fi
    fi
fi

AC_OUTPUT

cat <<EOF

Configuration complete, now type 'make' to build.
EOF
