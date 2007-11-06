AC_INIT
AC_PREREQ([2.52])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([common.mk])

if test -z "$CFLAGS"
then
    CFLAGS="-g -Wall -Werror -O"
fi

dnl ugh, we should test if _GNU_SOURCE is needed instead of using it everywhere
AC_DEFINE([_GNU_SOURCE])

AC_PROG_CC

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

dnl look for opendisk() in libutil
AC_SEARCH_LIBS([opendisk], [util],
[AC_CHECK_HEADERS([util.h])
AC_DEFINE([HAVE_OPENDISK])])

AC_OUTPUT

cat <<EOF

Configuration complete, now type 'name' to build.
EOF
