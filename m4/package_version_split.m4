##  package_version_split.m4    -*- mode: autoconf -*-

dnl Copyright 2015-2023 Carnegie Mellon University
dnl See license information in LICENSE.txt.

AC_DEFUN([PACKAGE_VERSION_SPLIT],[
    PACKAGE_VERSION_MAJOR=`echo "$PACKAGE_VERSION"   | $SED 's/\([[^.]]*\).*/\1/'`
    PACKAGE_VERSION_MINOR=`echo "$PACKAGE_VERSION"   | $SED 's/[[^.]]*\.\([[^.]]*\).*/\1/'`
    PACKAGE_VERSION_RELEASE=`echo "$PACKAGE_VERSION" | $SED 's/[[^.]]*\.[[^.]]*\.\([[^.]]*\).*/\1/'`
    PACKAGE_VERSION_BUILD=`echo "$PACKAGE_VERSION"   | $SED 's/[[^.]]*\.[[^.]]*\.[[^.]]*\.\([[0-9][0-9]*]\)/\1/'`
    if test "x$PACKAGE_VERSION_BUILD" = "x$PACKAGE_VERSION"; then
        PACKAGE_VERSION_BUILD="0"
    fi
    AC_SUBST(PACKAGE_VERSION_MAJOR)
    AC_SUBST(PACKAGE_VERSION_MINOR)
    AC_SUBST(PACKAGE_VERSION_RELEASE)
    AC_SUBST(PACKAGE_VERSION_BUILD)
])

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
