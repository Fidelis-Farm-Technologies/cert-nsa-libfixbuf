##  infomodel.m4                -*- mode: autoconf -*-

dnl Copyright 2018-2023 Carnegie Mellon University
dnl See license information in LICENSE.txt.

# ---------------------------------------------------------------------------
# INFOMODEL_AC_COLLECT_REGISTRIES(infomodel_dir)
#
#    Create a list of infomodel IE registries located in
#    $(top_srcdir)/$infomodel_dir.  Place the list of registry names in
#    INFOMODEL_REGISTRIES.  Place a list of registry names in
#    INFOMODEL_REGISTRY_PREFIXES.  Place a list of registry include
#    files in INFOMODEL_REGISTRY_INCLUDE_FILES.  Place a list of
#    registry include dependencies based from $srcdir in
#    INFOMODEL_REGISTRY_INCLUDES.
#
#    Output variables: INFOMODEL_REGISTRIES INFOMODEL_REGISTRY_PREFIXES
#        INFOMODEL_REGISTRY_INCLUDE_FILES INFOMODEL_REGISTRY_INCLUDES
#
AC_DEFUN([INFOMODEL_AC_COLLECT_REGISTRIES],[
    AC_SUBST(INFOMODEL_REGISTRY_PREFIXES)
    AC_SUBST(INFOMODEL_REGISTRY_INCLUDES)
    AC_SUBST(INFOMODEL_REGISTRY_INCLUDE_FILES)
    AC_SUBST(INFOMODEL_REGISTRIES)

    AC_MSG_CHECKING([for information element files])
    files=[`echo $][srcdir/$1/[A-Za-z0-9_]*.xml`]
    prefixes=[`echo $files | sed 's,[^ ]*/\([^/ ]*\)\.xml,\1,g'`]
    xml=[`echo $prefixes | sed 's,\([^ ]*\),\1.xml,g'`]
    inc_files=[`echo $prefixes | sed 's,\([^ ]*\),\1.i,g'`]
    includes=[`echo $inc_files | sed 's,\([^ ]*\),$(top_builddir)/$1/\1,g'`]
    INFOMODEL_REGISTRY_PREFIXES=$prefixes
    INFOMODEL_REGISTRY_INCLUDE_FILES=$inc_files
    INFOMODEL_REGISTRY_INCLUDES=$includes
    INFOMODEL_REGISTRIES=$xml
    AC_MSG_RESULT([$1/{$INFOMODEL_REGISTRY_PREFIXES}])
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
