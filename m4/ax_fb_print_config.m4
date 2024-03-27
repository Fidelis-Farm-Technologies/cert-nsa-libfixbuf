##  ax_fb_print_config.m4       -*- mode: autoconf -*-

dnl Copyright 2006-2023 Carnegie Mellon University
dnl See license information in LICENSE.txt.

dnl Print a summary of the configuration

AC_DEFUN([AX_FB_PRINT_CONFIG],[

    FB_BUILD_CONFIG="
    * Configured package:           ${PACKAGE_STRING}
    * Host type:                    ${build}
    * Source files (\$top_srcdir):   ${srcdir}
    * Install directory:            ${prefix}"

    if test "x${enable_tools}" = xyes
    then
    FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * Build command-line tools:     YES"
    else
    FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * Build command-line tools:     NO"
    fi

    FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * pkg-config path:              ${PKG_CONFIG_PATH}
    * GLIB:                         ${GLIB_LIBS}"

    # OpenSSL
    if test "x${FIXBUF_REQ_LIBSSL}" = "x1"
    then
        FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * OpenSSL Support:              YES"
    else
        FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * OpenSSL Support:              NO"
    fi

    # DTLS
    if test "x${ac_cv_lib_ssl_DTLS_method}" = xyes
    then
        FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * DTLS Support:                 YES"
    else
        FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * DTLS Support:                 NO"
    fi

    # SCTP
    if test "x${FIXBUF_REQ_SCTPDEV}" = "x1"
    then
        FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * SCTP Support:                 YES"
    else
        FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * SCTP Support:                 NO"
    fi

    # Remove leading whitespace
    fb_msg_cflags="${AM_CPPFLAGS} ${CPPFLAGS} ${WARN_CFLAGS} ${DEBUG_CFLAGS} ${CFLAGS}"
    fb_msg_cflags=`echo "${fb_msg_cflags}" | sed 's/^ *//' | sed 's/  */ /g'`

    fb_msg_ldflags="${FB_LDFLAGS} ${LDFLAGS}"
    fb_msg_ldflags=`echo "${fb_msg_ldflags}" | sed 's/^ *//' | sed 's/  */ /g'`

    fb_msg_libs="${LIBS}"
    fb_msg_libs=`echo "${fb_msg_libs}" | sed 's/^ *//' | sed 's/  */ /g'`

    FB_BUILD_CONFIG="${FB_BUILD_CONFIG}
    * Compiler (CC):                ${CC}
    * Compiler flags (CFLAGS):      ${fb_msg_cflags}
    * Linker flags (LDFLAGS):       ${fb_msg_ldflags}
    * Libraries (LIBS):             ${fb_msg_libs}
"

    # First argument becomes option to config.status
    AC_CONFIG_COMMANDS([print-config],[
        echo "${FB_BUILD_CONFIG}"
    ],[FB_BUILD_CONFIG='${FB_BUILD_CONFIG}'])
])
