##  Copyright 2006-2023 Carnegie Mellon University
##  See license information in LICENSE.txt.

##  Process this file with automake to produce Makefile.in
##  ------------------------------------------------------------------------
##  Makefile.am (source)
##  autotools build system for libfixbuf
##  ------------------------------------------------------------------------
##  Authors: Brian Trammell
##  ------------------------------------------------------------------------

lib_LTLIBRARIES = libfixbuf.la

AM_CFLAGS = $(WARN_CFLAGS) $(DEBUG_CFLAGS) $(GLIB_CFLAGS)
LDADD = $(GLIB_LDADD) $(GLIB_LIBS)

libfixbuf_la_SOURCES =  fbcollector.c fbcollector.h fbconnspec.c fbexporter.c \
                        fbinfomodel.c fblistener.c  fbnetflow.c  fbsession.c \
                        fbsflow.c     fbtemplate.c  fbuf.c       fbxml.c      
nodist_libfixbuf_la_SOURCES = $(MAKE_INFOMODEL_OUTPUTS)
libfixbuf_la_LDFLAGS = -version-info $(LIBCOMPAT)

EXTRA_DIST = xml2fixbuf.xslt make-infomodel

SUBDIRS = infomodel

MAKE_INFOMODEL_OUTPUTS = infomodel.c infomodel.h
RUN_MAKE_INFOMODEL = \
  srcdir='' ; \
  test -f ./make-infomodel || srcdir=$(srcdir)/ ; \
  $(PERL) "$${srcdir}make-infomodel" --package $(PACKAGE) \
    $(INFOMODEL_REGISTRY_PREFIXES) \
  || { rm -f $(MAKE_INFOMODEL_OUTPUTS) ; exit 1 ; }

BUILT_SOURCES = $(MAKE_INFOMODEL_OUTPUTS)
if ENABLE_TOOLS
BUILT_SOURCES += ipfixDump.h ipfix2json.h
endif
CLEANFILES = $(BUILT_SOURCES)

infomodel.c : infomodel.h
infomodel.h : make-infomodel Makefile
	$(AM_V_GEN)$(RUN_MAKE_INFOMODEL)

bin_PROGRAMS =
man1_MANS =
if ENABLE_TOOLS
bin_PROGRAMS += ipfixDump ipfix2json
man1_MANS += ipfixDump.1 ipfix2json.1
endif

PODFILES = ipfixDump.pod ipfix2json.pod
HTMLFILES = ipfixDump.html ipfix2json.html
EXTRA_DIST += $(PODFILES) ipfixDump.h.in ipfix2json.h.in
CLEANFILES += $(man1_MANS) $(HTMLFILES)

ipfixDump_SOURCES = ipfixDump.c ipfixDumpPrint.c
nodist_ipfixDump_SOURCES = ipfixDump.h
ipfixDump_LDADD = libfixbuf.la $(LDADD)

ipfix2json_SOURCES = ipfix2json.c ipfix2jsonPrint.c
nodist_ipfix2json_SOURCES = ipfix2json.h 
ipfix2json_LDADD = libfixbuf.la $(LDADD)

ipfixDump.h: ipfixDump.h.in Makefile
	$(AM_V_GEN)$(MAKE_TOOL_H)

ipfix2json.h: ipfix2json.h.in Makefile
	$(AM_V_GEN)$(MAKE_TOOL_H)

# Command to fill package name/version place-holders in the input stream
TOOL_H_SED = sed \
  -e 's|@FIXBUF_PACKAGE_NAME[@]|$(PACKAGE)|g' \
  -e 's|@FIXBUF_PACKAGE_VERSION[@]|$(PACKAGE_VERSION)|g' \
  -e 's|@FIXBUF_PACKAGE_DATADIR[@]|$(pkgdatadir)|g'

# Command to create FOO.h from FOO.h.in using above sed command
MAKE_TOOL_H = \
  srcdir='' ; \
  test -f "./$@.in" || srcdir=$(srcdir)/ ; \
  $(TOOL_H_SED) "$${srcdir}$@.in" > $@ || { rm -f $@ ; exit 1 ; }

FIXBUF_POD2MAN_ARGS = --center='$(PACKAGE_NAME)' --release='$(PACKAGE_VERSION)' --date='$(BUILD_DATE)'
FIXBUF_POD2HTML_ARGS = --noindex --nopoderrors

dist_pkgdata_DATA =
noinst_DATA =
if ENABLE_TOOLS
dist_pkgdata_DATA += cert_ipfix.xml
noinst_DATA += share/$(PACKAGE)/cert_ipfix.xml
endif

# Creates a symlink to cert_ipfix.xml so that ipfixDump and ipfix2json
# work when called from within the build tree
SYMLINK_CERT_IPFIX = \
  target="$?" ; \
  expr "x$${target}" : "x/" >/dev/null || target="../../$${target}" ; \
  { test -d share/$(PACKAGE) || $(MKDIR_P) share/$(PACKAGE) ; } && \
  { cd share/$(PACKAGE) && $(LN_S) "$${target}" cert_ipfix.xml ; }

share/$(PACKAGE)/cert_ipfix.xml: cert_ipfix.xml
	$(AM_V_GEN)$(SYMLINK_CERT_IPFIX)

CLEANFILES += $(noinst_DATA)


if ENABLE_TOOLS
TOOLS_DOCS = tools-docs
endif

RUN_POD2HTML = \
  add_header=$(top_builddir)/doc/add-header.pl ; \
  test -f "$${add_header}" || add_header="$(top_srcdir)/doc/add-header.pl" ; \
  { $(POD2HTML) $(FIXBUF_POD2HTML_ARGS) --title="$*" \
    --infile="$<" --outfile="$@" && \
    $(PERL) "$${add_header}" "$@" ; } || { rm -f "$@" ; exit 1 ; }

.pod.1:
	$(AM_V_GEN)$(POD2MAN) $(FIXBUF_POD2MAN_ARGS) --section=1 $< $@

.pod.html:
	$(AM_V_GEN)$(RUN_POD2HTML)

docs: $(TOOLS_DOCS)

# Builds the HTML versions of the tools' manual pages
tools-docs: $(HTMLFILES)
	@test -d $(top_builddir)/doc/html || $(MKDIR_P) $(top_builddir)/doc/html
	cp -p $(HTMLFILES) $(top_builddir)/doc/html

# Removes the HTML versions of the tools' manual pages from build tree
CLEAN_HTML = \
  dir='$(top_builddir)/doc/html' ; \
  files='$(HTMLFILES)' ; \
  test -z "$$files" \
  || { test ! -d "$$dir" && test ! -f "$$dir" && test ! -r "$$dir" ; } \
  || { cd "$$dir" && rm -f $$files ; }

mostlyclean-local:
	$(CLEAN_HTML)
	rm -f pod2htm*.tmp

.PHONY: docs tools-docs

##  @DISTRIBUTION_STATEMENT_BEGIN@
##  libfixbuf 3.0.0
##
##  Copyright 2022 Carnegie Mellon University.
##
##  NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
##  INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
##  UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
##  AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
##  PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
##  THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF
##  ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT
##  INFRINGEMENT.
##
##  Released under a GNU GPL 2.0-style license, please see LICENSE.txt or
##  contact permission@sei.cmu.edu for full terms.
##
##  [DISTRIBUTION STATEMENT A] This material has been approved for public
##  release and unlimited distribution.  Please see Copyright notice for
##  non-US Government use and distribution.
##
##  Carnegie Mellon(R) and CERT(R) are registered in the U.S. Patent and
##  Trademark Office by Carnegie Mellon University.
##
##  This Software includes and/or makes use of the following Third-Party
##  Software subject to its own license:
##
##  1. GLib-2.0 (https://gitlab.gnome.org/GNOME/glib/-/blob/main/COPYING)
##     Copyright 1995 GLib-2.0 Team.
##
##  2. Doxygen (http://www.gnu.org/licenses/old-licenses/gpl-2.0.html)
##     Copyright 2021 Dimitri van Heesch.
##
##  DM22-0006
##  @DISTRIBUTION_STATEMENT_END@
