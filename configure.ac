AC_INIT([upart], [0.2.3])
dnl need 2.60 for AC_PROG_MKDIR_P
AC_PREREQ([2.60])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([common.mk])

dnl remember if CFLAGS was set before AC_PROG_CC messes with it
had_prior_cflags=${CFLAGS:+yes}

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

dnl bsd and solaris disk ioctls
AC_CHECK_HEADERS([sys/dkio.h],,,
[#include <sys/types.h>
#ifdef HAVE_SYS_DISKLABEL_H
#include <sys/disklabel.h>
#endif])

dnl solaris vtoc
AC_CHECK_HEADERS([sys/vtoc.h])

dnl freebsd headers
AC_CHECK_HEADERS([sys/disk.h])

dnl look for opendev() in libutil
AC_SEARCH_LIBS([opendev], [util],
[AC_CHECK_HEADERS([util.h])
AC_DEFINE([HAVE_OPENDEV])])

dnl look for opendisk() in libutil
AC_SEARCH_LIBS([opendisk], [util],
[AC_CHECK_HEADERS([util.h])
AC_DEFINE([HAVE_OPENDISK])])

dnl some linux headers
AC_CHECK_HEADERS([linux/fs.h linux/hdreg.h])

dnl sysctl to list disks on bsd systems
AC_CHECK_HEADERS([sys/sysctl.h],,,
[#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif])
AC_CHECK_FUNCS([sysctl sysctlnametomib])

dnl osx headers and frameworks
AC_CHECK_HEADERS([CoreFoundation/CoreFoundation.h],
    [up_have_cf=yes], [up_have_cf=no])
if test x"$ac_cv_header_CoreFoundation_CoreFoundation_h" = xyes
then
    UP_CHECK_FRAMEWORK([CFRelease], [CoreFoundation])
fi
AC_CHECK_HEADERS([IOKit/IOKitLib.h IOKit/IOBSD.h IOKit/storage/IOMedia.h])
if test x"$ac_cv_header_IOKit_IOKitLib_h" = xyes
then
    UP_CHECK_FRAMEWORK([IOObjectRelease], [IOKit])
fi

dnl look for sane string functions
AC_CHECK_FUNCS([strlcpy strlcat])

dnl try to figure out flags to get makefile dependencies
dnl XXX there has to be a better way of doing this
AC_CACHE_CHECK([for C compiler option to generate Makefile dependencies],
               [my_cc_cv_mkdep_flags],
               [my_cc_cv_mkdep_flags=-M
                AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [[#ifndef __SUNPRO_C
       not sunpro
#endif
]])], [my_cc_cv_mkdep_flags=-xM])])
dnl XXX should use AC_ARG_VAR so user can override
AC_SUBST([CC_MKDEP_FLAGS], [$my_cc_cv_mkdep_flags])

dnl try to make sure all warnings are on and fatal for gcc
if test xyes = x"$GCC" && test xyes != x"$had_prior_cflags"
then
    CFLAGS="$CFLAGS -Wall -Werror"
fi

AC_OUTPUT

cat <<EOF

Configuration complete, now type 'make' to build.
EOF
