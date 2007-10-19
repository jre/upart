AC_INIT
AC_PREREQ([2.52])
AC_CONFIG_HEADERS([config.h])

dnl ugh, we should test if _GNU_SOURCE is needed instead of using it everywhere
AC_DEFINE([_GNU_SOURCE])

AC_PROG_CC

dnl check for crap needed for 64-bit off_t and such
AC_SYS_LARGEFILE

dnl some standard headers
AC_CHECK_HEADERS([sys/types.h stdint.h inttypes.h])

dnl bsd disklabel headers
AC_CHECK_HEADERS([sys/disklabel.h])
AC_CHECK_HEADERS([sys/dkio.h],,,
[#include <sys/types.h>
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/disklabel.h>
#endif])

AC_OUTPUT

cat <<EOF

Configuration complete. Now build with GNU make, which may be called
'make' or 'gmake' depending on your system.
EOF
