dnl Copyright 2006-2023 Carnegie Mellon University
dnl See license information in LICENSE.txt.

dnl Process this file with autoconf to produce a configure script
dnl ------------------------------------------------------------------------
dnl configure.ac
dnl autotools build system for libfixbuf
dnl ------------------------------------------------------------------------
dnl Authors: Brian Trammell, Dan Ruef, Chris Inacio
dnl ------------------------------------------------------------------------

AC_INIT([libfixbuf], [3.0.0.alpha2], [netsa-help@cert.org], [libfixbuf], [http://tools.netsa.cert.org/fixbuf/])
AC_CONFIG_AUX_DIR([autoconf])
AC_COPYRIGHT([Copyright (C) 2006-2023 by Carnegie Mellon University
GNU General Public License (GPL) Rights pursuant to Version 2, June 1991])

AM_INIT_AUTOMAKE([1.6.3 foreign nostdinc])

dnl change this version number every new release. (1:0:0 starts at 1.3.0)
dnl the first number should be changed if the interface has changed
dnl the second number should be changed for bug fixes, small changes
dnl the third number should be changed if the first number changes and
dnl the release is backwards compatible with the previous release.
dnl https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html

# Previous release: 2.3.1, LIBCOMPAT 9:1:0
LIBCOMPAT="10:0:0"

AC_CONFIG_HEADERS([include/fixbuf/config.h])
AC_CONFIG_MACRO_DIR([m4])
LT_INIT

AM_MAINTAINER_MODE([enable])
AM_MISSING_PROG(XSLTPROC, xsltproc)

AC_LANG([C])
AC_PROG_CC
AC_DEBUG
AX_ENABLE_WARNINGS

if test "X${srcdir}" = "X." ; then
    # building in place
    AC_SUBST([AM_CPPFLAGS], ['-I. -I$(top_srcdir)/include'])
else
    AC_SUBST([AM_CPPFLAGS], ['-I. -I$(srcdir) -I$(top_builddir)/include -I$(top_srcdir)/include'])
fi

AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_MKDIR_P

AC_CHECK_HEADERS([unistd.h stdint.h errno.h arpa/inet.h netinet/in.h sys/errno.h sys/socket.h pthread.h])

AM_WITH_DMALLOC

RPM_CONFIG_FLAGS=""

dnl ----------------------------------------------------------------------
dnl Set up doxygen
dnl ----------------------------------------------------------------------

DX_HTML_FEATURE(ON)
DX_CHM_FEATURE(OFF)
DX_CHI_FEATURE(OFF)
DX_MAN_FEATURE(OFF)
DX_RTF_FEATURE(OFF)
DX_XML_FEATURE(OFF)
DX_PDF_FEATURE(OFF)
DX_PS_FEATURE(OFF)
DX_INIT_DOXYGEN(libfixbuf, Doxyfile, doc)

dnl ----------------------------------------------------------------------
dnl Check for aligned access required
dnl ----------------------------------------------------------------------

AX_CHECK_ALIGNED_ACCESS_REQUIRED

dnl ----------------------------------------------------------------------
dnl Check for a suitable glib
dnl ----------------------------------------------------------------------

GLIB_PRESENT=NO
GLIB_LDADD=

AM_PATH_GLIB_2_0([2.18.0],[GLIB_PRESENT=YES],,[gthread])

AC_ARG_WITH(glib-static,
[  --with-glib-static=prefix   use static glib tree],[
GLIB_STATIC_LIBGLIB2_A="${withval}/lib/libglib-2.0.a ${withval}/lib/libgthread-2.0.a"
GLIB_STATIC_INCLUDE="${withval}/include/glib-2.0"
GLIB_STATIC_LIBINCLUDE="${withval}/lib/glib-2.0/include"
if test -f ${GLIB_STATIC_LIBGLIB2_A}; then
    if test -f "${GLIB_STATIC_INCLUDE}/glib.h"; then
        GLIB_LDADD=${GLIB_STATIC_LIBGLIB2_A}
        GLIB_CFLAGS="-I${GLIB_STATIC_INCLUDE} -I${GLIB_STATIC_LIBINCLUDE}"
        AC_SUBST(GLIB_LIBS)
        AC_SUBST(GLIB_CFLAGS)
        GLIB_PRESENT=YES
        AC_MSG_NOTICE([assuming static glib in ${withval} is good...])
    fi
fi
])

if test x"$GLIB_PRESENT" != "xYES"; then
  AC_MSG_ERROR([Cannot find a suitable glib2 (>= 2.18)])
fi

AC_SUBST(GLIB_LDADD)

dnl ----------------------------------------------------------------------
dnl Check for getaddrinfo, ntohll
dnl ----------------------------------------------------------------------

AC_CHECK_FUNCS([getaddrinfo])
AC_CHECK_DECLS([ntohll])

dnl ---------------------------------------------------------------------
dnl Check for pthread
dnl --------------------------------------------------------------------
AC_CHECK_LIB([pthread],[pthread_mutex_lock],[],[
    AC_MSG_ERROR([Required pthread library not found])
])


dnl ----------------------------------------------------------------------
dnl check for all of the Solaris specific libraries
dnl ----------------------------------------------------------------------
AC_SEARCH_LIBS([nanosleep], [rt])
AC_SEARCH_LIBS([inet_ntoa], [nsl])
AC_SEARCH_LIBS([socket], [socket])

dnl ----------------------------------------------------------------------
dnl check sizeof types
dnl ----------------------------------------------------------------------
AC_CHECK_SIZEOF([double])
AC_CHECK_SIZEOF([float])
if test "x${ac_cv_sizeof_double}" != x8
then
    AC_MSG_ERROR([double has unexpected size ${ac_cv_sizeof_double}; expected 8])
fi
if test "x${ac_cv_sizeof_float}" != x4
then
    AC_MSG_ERROR([float has unexpected size ${ac_cv_sizeof_float}; expected 4])
fi


dnl ----------------------------------------------------------------------
dnl Aborting on defaulted size specifications
dnl ----------------------------------------------------------------------
fb_abort_on_default_sizespec=1
AC_ARG_ENABLE([abort-on-default-sizespec],
    [AS_HELP_STRING([--disable-abort-on-default-sizespec],
        [disable abort when using defaulted (zero-sized) information element specifications for internal templates.  This will cause fBufSetInternalTemplate() to return an error instead.])[]dnl
    ],[
        if test "x$enableval" = "xno"
        then
          fb_abort_on_default_sizespec=0
          RPM_CONFIG_FLAGS="${RPM_CONFIG_FLAGS} --disable-abort-on-default-sizespec"
        fi
    ])
AC_DEFINE_UNQUOTED([FB_ABORT_ON_DEFAULTED_LENGTH],
                   [$fb_abort_on_default_sizespec],
                   [Define to 1 to enable aborts of defaulted spec lengths])

dnl ----------------------------------------------------------------------
dnl Check for SCTP support
dnl ----------------------------------------------------------------------

AC_ARG_WITH([sctp],dnl
[AS_HELP_STRING([[--with-sctp[=DIR]]],
                [Enable SCTP support using libsctp [default=no]; if DIR given, find libsctp in that directory])],
[
    if test "x$withval" != "xno"; then
        if test -d ${withval}; then
            LDFLAGS="-L${withval}/lib ${LDFLAGS}"
            CPPFLAGS="-I${withval}/include ${CPPFLAGS}"
            RPM_CONFIG_FLAGS="${RPM_CONFIG_FLAGS} --with-sctp=${withval}"
        else
            RPM_CONFIG_FLAGS="${RPM_CONFIG_FLAGS} --with-sctp"
        fi

        # look for libsctp
        AC_CHECK_LIB([sctp],[sctp_sendmsg],[
            AC_DEFINE([FB_ENABLE_SCTP], [1],
                      [Define to 1 to enable SCTP support])
            LIBS="-lsctp ${LIBS}"
            AC_SUBST(FIXBUF_REQ_LIBSCTP, [1])
        ],[
            AC_MSG_FAILURE([--with-sctp given but cannot find libsctp])
        ])

        # look for SCTP header files
        AC_CHECK_HEADER([netinet/sctp.h],[
            AC_DEFINE([FB_INCLUDE_SCTP_H],[1],
                      [Define to 1 to use sctp.h header])
            AC_SUBST([FIXBUF_REQ_SCTPDEV], [1])
        ], [], [AC_INCLUDES_DEFAULT([
#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
        ])])

        AC_CHECK_HEADER([netinet/sctp_uio.h],[
            AC_DEFINE([FB_INCLUDE_SCTP_UIO_H],[1],
                      [[Define to 1 to use sctp_uio.h header]])
        ])
    fi
])


dnl ----------------------------------------------------------------------
dnl Check for OpenSSL TLS and DTLS support
dnl ----------------------------------------------------------------------

AC_ARG_WITH([openssl],dnl
[AS_HELP_STRING([[--with-openssl[=DIR]]],
                [Use OpenSSL for TLS/DTLS support [default=no]; if DIR given, find OpenSSL in that directory])],
[
    if test "x${withval}" != "xno" ; then
        if test -d "${withval}" ; then
            LDFLAGS="-L${withval}/lib ${LDFLAGS}"
            CPPFLAGS="-I${withval}/include ${CPPFLAGS}"
            RPM_CONFIG_FLAGS="${RPM_CONFIG_FLAGS} --with-openssl=${withval}"
        else
            RPM_CONFIG_FLAGS="${RPM_CONFIG_FLAGS} --with-openssl"
        fi

        # look for libssl 1.0.2 or later by checking for SSL_certs_clear
        AC_CHECK_LIB([ssl], [SSL_certs_clear], [
            AC_DEFINE([HAVE_OPENSSL], [1],
                      [Define to 1 to enable OpenSSL support])
            LIBS="-lssl -lcrypto ${LIBS}"
            AC_SUBST([FIXBUF_REQ_LIBSSL], [1])
        ],[
            AC_MSG_FAILURE([--with-openssl given but cannot find SSL_certs_clear()])
        ])

        # look for dtls
        AC_CHECK_LIB([ssl], [DTLS_method], [
            AC_DEFINE([HAVE_OPENSSL_DTLS], [1],
                      [Define to 1 to enable DTLS support])
dnl         AC_CHECK_LIB([ssl], [BIO_new_dgram_sctp], [
dnl             AC_DEFINE([HAVE_OPENSSL_DTLS_SCTP], [1],
dnl                       [Define to 1 to enable DTLS over SCTP support])
dnl         ])
        ],[
            AC_MSG_NOTICE([OpenSSL does not support DTLS])
        ])

        AC_CHECK_HEADER([openssl/ssl.h], [], [
           AC_MSG_FAILURE([--with-openssl given but cannot find libssl headers.])
        ])
    fi
])

AC_SUBST(LIBCOMPAT)
AC_SUBST(RPM_CONFIG_FLAGS)

dnl ----------------------------------------------------------------------
dnl Determine infomodel information
dnl ----------------------------------------------------------------------
INFOMODEL_AC_COLLECT_REGISTRIES([src/infomodel])
AC_PATH_PROG([PERL], [perl])

dnl automake 1.9 doesn't substitute these automatically, so we do it ourselves
dnl just in case
AC_SUBST([abs_top_srcdir])
AC_SUBST([abs_top_builddir])

dnl ----------------------------------------------------------------------
dnl Make installation of ipfixDump optional
dnl ----------------------------------------------------------------------

AC_ARG_ENABLE([tools],
    [AS_HELP_STRING([--disable-tools],
        [do not build or install ipfixDump, ipfix2json, and cert_ipfix.xml])[]dnl
    ],[
        # value of argument to --enable-tools
        enable_tools=${enableval}
    ],[
        # the --enable-tools switch was not given; enable them
        enable_tools=yes
    ])
AM_CONDITIONAL([ENABLE_TOOLS], [test "x${enable_tools}" = "xyes"])

dnl ----------------------------------------------------------------------
dnl Miscellaneous checks
dnl ----------------------------------------------------------------------

AC_PATH_PROG([POD2MAN],  [pod2man])
AC_PATH_PROG([POD2HTML], [pod2html])
AC_SUBST([BUILD_DATE], ["`date +'%v'`"])

PACKAGE_VERSION_SPLIT

AX_FB_PRINT_CONFIG

AC_CONFIG_FILES([
    Makefile
    src/Makefile
    src/infomodel/Makefile
    include/Makefile
    include/fixbuf/version.h
    libfixbuf.pc
    libfixbuf.spec
    Doxyfile])

AC_OUTPUT

dnl @DISTRIBUTION_STATEMENT_BEGIN@
dnl libfixbuf 3.0.0
dnl
dnl Copyright 2022 Carnegie Mellon University.
dnl
dnl NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
dnl INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
dnl UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
dnl AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
dnl PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
dnl THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF
dnl ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT
dnl INFRINGEMENT.
dnl
dnl Released under a GNU GPL 2.0-style license, please see LICENSE.txt or
dnl contact permission@sei.cmu.edu for full terms.
dnl
dnl [DISTRIBUTION STATEMENT A] This material has been approved for public
dnl release and unlimited distribution.  Please see Copyright notice for
dnl non-US Government use and distribution.
dnl
dnl Carnegie Mellon(R) and CERT(R) are registered in the U.S. Patent and
dnl Trademark Office by Carnegie Mellon University.
dnl
dnl This Software includes and/or makes use of the following Third-Party
dnl Software subject to its own license:
dnl
dnl 1. GLib-2.0 (https://gitlab.gnome.org/GNOME/glib/-/blob/main/COPYING)
dnl    Copyright 1995 GLib-2.0 Team.
dnl
dnl 2. Doxygen (http://www.gnu.org/licenses/old-licenses/gpl-2.0.html)
dnl    Copyright 2021 Dimitri van Heesch.
dnl
dnl DM22-0006
dnl @DISTRIBUTION_STATEMENT_END@

