AC_INIT([upart], [0.1.1])
dnl need 2.60 for AC_PROG_MKDIR_P
AC_PREREQ([2.60])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([common.mk])

if test -z "$CFLAGS"
then
    CFLAGS="-g -Wall -Werror -O"
fi

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

AC_OUTPUT

cat <<EOF

Configuration complete, now type 'make' to build.
EOF
