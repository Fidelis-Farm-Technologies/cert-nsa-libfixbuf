##  ax_enable_warnings.m4       -*- mode: autoconf -*-

dnl Copyright 2006-2023 Carnegie Mellon University
dnl See license information in LICENSE.txt.

dnl Determines the compiler flags to use for warnings.  User may use
dnl --enable-warnings to provide their own or override the default.
dnl
dnl OUTPUT VARIABLE:  WARN_CFLAGS
dnl
AC_DEFUN([AX_ENABLE_WARNINGS],[
    AC_SUBST([WARN_CFLAGS])

    WARN_CFLAGS=
    default_warn_flags="-Wall -Wextra -Wshadow -Wpointer-arith -Wformat=2 -Wunused -Wundef -Wduplicated-cond -Wwrite-strings -Wmissing-prototypes -Wstrict-prototypes"

    AC_ARG_ENABLE([warnings],
    [AS_HELP_STRING([[--enable-warnings[=FLAGS]]],
        [enable compile-time warnings [default=yes]; if value given, use those compiler warnings instead of default])],
    [
        if test "X${enable_warnings}" = Xno
        then
            AC_MSG_NOTICE([disabled all compiler warning flags])
        elif test "X${enable_warnings}" != Xyes
        then
            WARN_CFLAGS="${enable_warnings}"
        fi
    ],[
        enable_warnings=yes
    ])

    if test "x${enable_warnings}" = xyes
    then
        save_cflags="${CFLAGS}"
        for f in ${default_warn_flags} ; do
            AC_MSG_CHECKING([whether ${CC} supports ${f}])
            CFLAGS="${save_cflags} -Werror ${f}"
            AC_COMPILE_IFELSE([
                AC_LANG_PROGRAM([[@%:@include <stdio.h>]],[[
                    printf("Hello, World\n");
                ]])
            ],[
                AC_MSG_RESULT([yes])
                WARN_CFLAGS="${WARN_CFLAGS} ${f}"
            ],[
                AC_MSG_RESULT([no])
            ])
        done
        CFLAGS="${save_cflags}"
    fi

])

dnl @DISTRIBUTION_STATEMENT_BEGIN@
dnl @DISTRIBUTION_STATEMENT_END@

