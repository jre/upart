AC_DEFUN([UP_CHECK_FRAMEWORK],
   [up_save_libs="$LIBS"
    LIBS="$LIBS -framework $2"
    AC_CACHE_CHECK([for $2 framework], [up_cv_have_framework_$2],
        [AC_TRY_LINK_FUNC([$1],
            [up_cv_have_framework_$2=yes], [up_cv_have_framework_$2=no])])
    if test x"$up_cv_have_framework_$2" != xyes
    then
        LIBS="$up_save_libs"
    fi])
