/*
 *  Copyright 2019-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file ipfix2json.c
 *  @brief IPFIX to JSON conversion tool.
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Emily Sarneso, Matt Coates
 *  ------------------------------------------------------------------------
 */

#include "ipfix2json.h"

/* CERT IPFIX Private Enterprise Number */
#define CERT_PEN    6871

/* Initial size of the buffer for an in-memory record. */
#define RECBUF_CAPACITY_INITIAL 256

/* String used in option descriptions when the text wraps multiple
 * lines. */
#define WRAP_STRING "\n                                   "

static char        *inspec = NULL;
static char        *outspec = NULL;
static gboolean     id_version = FALSE;
static gboolean     rfc5610 = FALSE;
static gboolean     only_tmpl = FALSE;
static gboolean     only_data = FALSE;
static gchar       *octet_format_string = NULL;

static FILE        *outfile = NULL;
static FILE        *infile = NULL;
static gchar       *cert_xml = NULL;
static gchar      **xml_files = NULL;
static gchar       *cert_element_file_path;

GHashTable         *template_names;
GStringChunk       *object_keys;

gboolean            full_structure = FALSE;
gboolean            allow_duplicate_keys = FALSE;
OctetArrayFormat_t  octet_array_format = OCTET_ARRAY_BASE64;

static GOptionEntry id_core_option[] = {
    {"in", 'i', 0, G_OPTION_ARG_STRING, &inspec,
     "Specify file to process [-]", "path"},
    {"out", 'o', 0, G_OPTION_ARG_STRING, &outspec,
     "Specify file to write to [-]", "path"},
    {"rfc5610", '\0', 0, G_OPTION_ARG_NONE, &rfc5610,
     ("Add IEs that are read from element type" WRAP_STRING
      "option records"), NULL},
    {"element-file", 'e', 0, G_OPTION_ARG_FILENAME_ARRAY, &xml_files,
     ("Load information elements from the given XML" WRAP_STRING
      "file. May be repeated"), "path"},
    {"cert-element-path", 'c', 0, G_OPTION_ARG_STRING, &cert_element_file_path,
     "Add a search location for the CERT IE XML" WRAP_STRING "file", "path"},
    {"templates", 't', 0, G_OPTION_ARG_NONE, &only_tmpl,
     "Print ONLY IPFIX templates", NULL},
    {"data", 'd', 0, G_OPTION_ARG_NONE, &only_data,
     "Print ONLY IPFIX data records", NULL},
    {"full-structure", '\0', 0, G_OPTION_ARG_NONE, &full_structure,
     ("Disable all compaction of intermediate" WRAP_STRING
      "template structure"), NULL},
    {"allow-duplicates", '\0', 0, G_OPTION_ARG_NONE, &allow_duplicate_keys,
     ("Allow duplicate keys within an object when" WRAP_STRING
      "an element or template appears multiple times"), NULL},
    {"octet-format", '\0', 0, G_OPTION_ARG_STRING, &octet_format_string,
     ("Print octetArray values in 'format' [base64]" WRAP_STRING
      "Choices: base64,string,hexadecimal,empty"), "format"},
    {"version", 'V', 0, G_OPTION_ARG_NONE, &id_version,
     "Print application version to stderr and exit", NULL},
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
};


static void
idPrintVersion(
    void)
{
    fprintf(stderr, "ipfix2json version %s "
            "(C) 2019-2023 Carnegie Mellon University.\n",
            FIXBUF_PACKAGE_VERISON);
    fprintf(stderr, "GNU General Public License (GPL) Rights "
            "pursuant to Version 2, June 1991\n");
    fprintf(stderr, "Some included library code covered by LGPL 2.1; "
            "see source for details.\n");
    fprintf(stderr, "Send bug reports, feature requests, and comments to "
            "netsa-help@cert.org.\n");
}


/**
 *    Attempts to find the cert_ipfix.xml file and returns its
 *    location.  Takes the program name as an argument.
 */
static gchar *
idFindCertXml(
    const gchar  *argv0)
{
    GArray *locations;
    const gchar * const *sysdirs;
    const gchar *fb_pkg_dir;
    gchar  *path;
    gchar  *dir;
    int     i;

    /* directories that will be checked for the file */
    locations = g_array_sized_new(TRUE, TRUE, sizeof(gchar *), 8);

    /* the directory ../share/libfixbuf relative to the
     * application's location */
    path = g_path_get_dirname(argv0);
    dir = g_build_filename(path, "..", "share", FIXBUF_PACKAGE_NAME, NULL);
    g_array_append_val(locations, dir);
    g_free(path);

#if 0
    /* the user's data dir */
    dir = g_build_filename(g_get_user_data_dir(), FIXBUF_PACKAGE_NAME, NULL);
    g_array_append_val(locations, dir);
#endif  /* 0 */

    /* the compile-time location */
    fb_pkg_dir = FIXBUF_PACKAGE_DATADIR;
    if (fb_pkg_dir && *fb_pkg_dir) {
        dir = g_build_filename(fb_pkg_dir, NULL);
        g_array_append_val(locations, dir);
    }

    /* system locations */
    sysdirs = g_get_system_data_dirs();
    for (i = 0; (sysdirs[i]); ++i) {
        dir = g_build_filename(sysdirs[i], FIXBUF_PACKAGE_NAME, NULL);
        g_array_append_val(locations, dir);
    }

    /* cli-provided location */
    if (cert_element_file_path) {
        if (g_file_test(cert_element_file_path, G_FILE_TEST_IS_REGULAR)) {
            dir = g_path_get_dirname(cert_element_file_path);
        } else {
            dir = g_build_filename(cert_element_file_path, NULL);
        }
        g_array_append_val(locations, dir);
    }

    /* search for the file */
    path = NULL;
    for (i = 0; !path && (dir = g_array_index(locations, gchar *, i)); ++i) {
        path = g_build_filename(dir, CERT_IPFIX_BASENAME, NULL);
        if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
            g_free(path);
            path = NULL;
        }
    }

    if (NULL == path) {
        fprintf(stderr, "%s: Failed to find '%s' in '%s'",
                g_get_prgname(), CERT_IPFIX_BASENAME,
                g_array_index(locations, gchar *, 0));
        for (i = 1; (dir = g_array_index(locations, gchar *, i)); ++i) {
            fprintf(stderr, ", '%s'", g_array_index(locations, gchar *, i));
        }
        fprintf(stderr, "\n");
        fprintf(stderr, ("%s: Use --cert-element-path and"
                         " specify its location\n"), g_get_prgname());
    }

    for (i = 0; (dir = g_array_index(locations, gchar *, i)); ++i) {
        g_free(dir);
    }
    g_array_free(locations, TRUE);

    return path;
}


/**
 *    Parse the command line options.
 */
static void
idParseOptions(
    int   *argc,
    char **argv[])
{
    GOptionContext *ctx = NULL;
    GError         *err = NULL;
    gchar          *app;

    app = g_path_get_basename((*argv)[0]);
    g_set_prgname(app);
    g_free(app);

    ctx = g_option_context_new(" - ipfix2json Options");

    g_option_context_add_main_entries(ctx, id_core_option, NULL);

    g_option_context_set_help_enabled(ctx, TRUE);

    if (!g_option_context_parse(ctx, argc, argv, &err)) {
        fprintf(stderr, "%s: Option parsing failed: %s\n",
                g_get_prgname(), err->message);
        g_clear_error(&err);
        exit(1);
    }

    if (id_version) {
        idPrintVersion();
        exit(0);
    }

    cert_xml = idFindCertXml((*argv)[0]);
    if (NULL == cert_xml) {
        exit(1);
    }

    /* check for non-arguments in argv */
    if (1 != *argc) {
        fprintf(stderr, "%s: Unrecognized argument %s\n",
                g_get_prgname(), (*argv)[1]);
        exit(1);
    }

    if (octet_format_string) {
        if (0 == strcmp(octet_format_string, "empty")) {
            octet_array_format = OCTET_ARRAY_EMPTY;
        } else if (0 == strcmp(octet_format_string, "base64")) {
            octet_array_format = OCTET_ARRAY_BASE64;
        } else if (0 == strcmp(octet_format_string, "string")) {
            octet_array_format = OCTET_ARRAY_STRING;
        } else if (0 == strcmp(octet_format_string, "hexadecimal")) {
            octet_array_format = OCTET_ARRAY_HEXADECIMAL;
        } else {
            fprintf(stderr, "%s: Unrecognized octet-format option: %s\n",
                    g_get_prgname(), octet_format_string);
            exit(1);
        }
    }

    if (inspec != NULL) {
        if ((strlen(inspec) == 1) && inspec[0] == '-') {
            infile = stdin;
        } else {
            infile = fopen(inspec, "r");
            if (infile == NULL) {
                fprintf(stderr, "%s: Opening input file %s failed: %s\n",
                        g_get_prgname(), inspec, strerror(errno));
                exit(1);
            }
        }
    } else if (isatty(fileno(stdin))) {
        fprintf(stderr, "%s: No input argument and stdin is a terminal\n",
                g_get_prgname());
        exit(1);
    } else {
        infile = stdin;
    }

    if (outspec == NULL) {
        outfile = stdout;
    } else {
        /* file or a directory or stdout */
        if ((strlen(outspec) == 1) && outspec[0] == '-') {
            outfile = stdout;
        } else {
            outfile = fopen(outspec, "w");
            if (outfile == NULL) {
                fprintf(stderr, "%s: Opening output file %s failed: %s\n",
                        g_get_prgname(), outspec, strerror(errno));
                exit(1);
            }
        }
    }

    g_option_context_free(ctx);
}


/**
 *  Examines the template describing a template name to find the position of
 *  each element of interest.  This is a helper for the new template callback.
 */
static void
idTemplateNameExamine(
    tmplContext_t  *tctx,
    fbTemplate_t   *tmpl)
{
    const fbTemplateField_t *field;
    unsigned int             i;
    gboolean templateId = FALSE;
    gboolean templateName = FALSE;

    for (i = 0; i < tctx->count && !(templateId && templateName); ++i) {
        field = fbTemplateGetFieldByPosition(tmpl, i);
        if (!templateId && fbTemplateFieldCheckIdent(field, 0, 145)) {
            templateId = TRUE;
            tctx->meta_tmpl[0] = i;
        } else if (!templateName &&
                   fbTemplateFieldCheckIdent(field, CERT_PEN, 1000))
        {
            templateName = TRUE;
            tctx->meta_tmpl[1] = i;
        }
    }

    if (templateId && templateName) {
        tctx->is_meta_template = TRUE;
    }
}


/**
 *    Free the template context object.  The signature of this
 *    function is given by fbTemplateCtxFree_fn.
 */
static void
templateFree(
    void  *ctx,
    void  *app_ctx)
{
    (void)app_ctx;
    g_free(ctx);
}

/**
 *    Callback invoked when a new template is seen.  The signature of
 *    this function is given by fbNewTemplateCallback_fn.
 */
static void
idTemplateCallback(
    fbSession_t           *session,
    uint16_t               tid,
    fbTemplate_t          *tmpl,
    void                  *app_ctx,
    void                 **ctx,
    fbTemplateCtxFree_fn  *fn)
{
    GError        *err = NULL;
    tmplContext_t *myctx = g_new0(tmplContext_t, 1);
    static fbInfoElementSpec_t templateNameSpec[] = {
        {"templateId",   2,            0},
        {"templateName", FB_IE_VARLEN, 0},
        FB_IESPEC_NULL
    };

    (void)app_ctx;

    myctx->count = fbTemplateCountElements(tmpl);
    myctx->len = fbTemplateGetIELenOfMemBuffer(tmpl);
    myctx->tid = tid;

    if (!only_data) {
        idPrintTemplate(outfile, tmpl, myctx);
    }

    if (!fbSessionAddTemplate(session, TRUE, tid, tmpl, NULL, &err)) {
        fprintf(stderr, "%s: Error adding template to session: %s\n",
                g_get_prgname(), err->message);
        g_clear_error(&err);
    }

    if (fbTemplateGetOptionsScope(tmpl)) {
        if (fbTemplateContainsAllElementsByName(tmpl, templateNameSpec)) {
            idTemplateNameExamine(myctx, tmpl);
        } else if (rfc5610 &&
                   fbTemplateIsMetadata(tmpl, FB_TMPL_IS_META_ELEMENT))
        {
            myctx->is_meta_element = TRUE;
        }
    }

    *ctx = myctx;
    *fn = templateFree;
}


/**
 *    Parse a template name options record and insert the TID/name
 *    pair into the template_names hash table.
 */
static void
idTemplateNameRecord(
    fbInfoModel_t        *model,
    const tmplContext_t  *tctx,
    const fbRecord_t     *record)
{
    const fbTemplateField_t *field;
    fbRecordValue_t          value = FB_RECORD_VALUE_INIT;
    uint16_t tid;

    (void)model;

    /* templateId */
    field = fbTemplateGetFieldByPosition(record->tmpl, tctx->meta_tmpl[0]);
    fbRecordCopyFieldValue(record, field, &tid, sizeof(tid));

    /* templateName */
    field = fbTemplateGetFieldByPosition(record->tmpl, tctx->meta_tmpl[1]);
    fbRecordGetValueForField(record, field, &value);

    if (tid >= FB_TID_MIN_DATA && value.v.varfield.len > 0) {
        g_hash_table_replace(template_names, GINT_TO_POINTER(tid),
                             fbRecordValueTakeVarfieldBuf(&value));
    } else {
        fbRecordValueClear(&value);
    }
}


/**
 *    Process an RFC5610 Record describing an InfoElement and add that
 *    element to the InfoModel.
 */
static gboolean
idInfoElementRecord(
    fbInfoModel_t        *model,
    const tmplContext_t  *tctx,
    const fbRecord_t     *record)
{
    fbInfoElementOptRec_t    rec;
    const fbTemplateField_t *field;
    fbTemplateIter_t         iter = FB_TEMPLATE_ITER_NULL;

    (void)tctx;

    memset(&rec, 0, sizeof(rec));

    fbTemplateIterInit(&iter, record->tmpl);
    while ((field = fbTemplateIterNext(&iter))) {
        if (fbTemplateFieldGetPEN(field) == 0) {
            switch (fbTemplateFieldGetId(field)) {
              case 346:
                /* privateEnterpriseNumber */
                fbRecordCopyFieldValue(record, field, &rec.ie_pen,
                                       sizeof(rec.ie_pen));
                break;
              case 303:
                /* informationElementId */
                fbRecordCopyFieldValue(record, field, &rec.ie_id,
                                       sizeof(rec.ie_id));
                break;
              case 339:
                /* informationElementDataType */
                fbRecordCopyFieldValue(record, field, &rec.ie_type,
                                       sizeof(rec.ie_type));
                break;
              case 344:
                /* informationElementSemantics */
                fbRecordCopyFieldValue(record, field, &rec.ie_semantic,
                                       sizeof(rec.ie_semantic));
                break;
              case 345:
                /* informationElementUnits */
                fbRecordCopyFieldValue(record, field, &rec.ie_units,
                                       sizeof(rec.ie_units));
                break;
              case 342:
                /* informationElementRangeBegin */
                fbRecordCopyFieldValue(record, field, &rec.ie_range_begin,
                                       sizeof(rec.ie_range_begin));
                break;
              case 343:
                /* informationElementRangeEnd */
                fbRecordCopyFieldValue(record, field, &rec.ie_range_end,
                                       sizeof(rec.ie_range_end));
                break;
              case 341:
                /* informationElementName */
                fbRecordCopyFieldValue(record, field, &rec.ie_name,
                                       sizeof(rec.ie_name));
                break;
              case 340:
                /* informationElementDescription */
                fbRecordCopyFieldValue(record, field, &rec.ie_desc,
                                       sizeof(rec.ie_desc));
                break;
            }
        }
    }

    return fbInfoElementAddOptRecElement(model, &rec);
}


int
main(
    int    argc,
    char  *argv[])
{
    fbInfoModel_t *model = NULL;
    fBuf_t        *fbuf = NULL;
    GError        *err = NULL;
    gboolean       rc;
    fbSession_t   *session = NULL;
    fbCollector_t *collector = NULL;
    tmplContext_t *tctx;
    fbRecord_t     record = FB_RECORD_INIT;
    gchar        **xml_file;

    idParseOptions(&argc, &argv);

    model = fbInfoModelAlloc();

    if (cert_xml) {
        if (!fbInfoModelReadXMLFile(model, cert_xml, &err)) {
            fprintf(stderr, "%s: Failed to load elements from '%s': %s\n",
                    g_get_prgname(), cert_xml, err->message);
            g_clear_error(&err);
            exit(-1);
        }
        g_free(cert_xml);
    }
    if (xml_files) {
        for (xml_file = xml_files; *xml_file; ++xml_file) {
            if (!fbInfoModelReadXMLFile(model, *xml_file, &err)) {
                fprintf(stderr, "%s: Failed to load elements from '%s': %s\n",
                        g_get_prgname(), *xml_file, err->message);
                g_clear_error(&err);
                exit(-1);
            }
        }
        g_strfreev(xml_files);
    }

    /* Create New Session */
    session = fbSessionAlloc(model);

    template_names = g_hash_table_new_full(g_direct_hash, NULL, NULL, g_free);
    object_keys = g_string_chunk_new(1024);

    /* Create a Collector */
    collector = fbCollectorAllocFP(NULL, infile);

    /* Allocate Collection Buffer */
    fbuf = fBufAllocForCollection(session, collector);

    fbSessionAddNewTemplateCallback(session, idTemplateCallback, fbuf);

    /* Allocate buffer for a single in-memory record */
    record.reccapacity = RECBUF_CAPACITY_INITIAL;
    record.rec = g_new(uint8_t, record.reccapacity);

    for (;;) {
        record.tmpl = fBufNextCollectionTemplate(fbuf, &record.tid, &err);
        if (!record.tmpl) {
            if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_EOF)) {
                fBufFree(fbuf);
                g_clear_error(&err);
                break;
            }
            fprintf(stderr, "%s: Warning: %s\n",
                    g_get_prgname(), err->message);
            g_clear_error(&err);
            continue;
        }

        tctx = fbTemplateGetContext(record.tmpl);
        record.recsize = tctx->len;

        if (record.recsize > record.reccapacity) {
            do {
                record.reccapacity <<= 1;
            } while (record.recsize > record.reccapacity);
            record.rec = g_renew(uint8_t, record.rec, record.reccapacity);
        }
        memset(record.rec, 0, record.reccapacity);

        rc = fBufNextRecord(fbuf, &record, &err);
        if (FALSE == rc) {
            if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_EOF)) {
                fprintf(stderr, "%s: END OF FILE\n", g_get_prgname());
                fBufFree(fbuf);
                g_clear_error(&err);
                break;
            }
            if (!g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_EOM) &&
                !g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_BUFSZ))
            {
                fprintf(stderr, "%s: Warning: %s\n",
                        g_get_prgname(), err->message);
            }
            g_clear_error(&err);
            continue;
        }

        if (tctx->is_meta_template) {
            idTemplateNameRecord(model, tctx, &record);
        } else if (tctx->is_meta_element) {
            idInfoElementRecord(model, tctx, &record);
        }
        if (only_tmpl) {
            fBufListFree(record.tmpl, record.rec);
        } else {
            char str_template[TMPL_NAME_BUFSIZ];
            idFormatTemplateId(str_template, record.tid, sizeof(str_template));
            fprintf(outfile, "{\"%s\": {", str_template);
            /*fprintf(outfile, "{\"data record\": {");*/
            idPrintDataRecord(outfile, &record);
            fprintf(outfile, "}}\n");
        }
    }

    fbInfoModelFree(model);
    g_free(record.rec);

    g_hash_table_destroy(template_names);
    g_string_chunk_free(object_keys);

    return 0;
}

/*
 *  @DISTRIBUTION_STATEMENT_BEGIN@
 *  libfixbuf 3.0.0
 *
 *  Copyright 2022 Carnegie Mellon University.
 *
 *  NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
 *  INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
 *  UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
 *  AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
 *  PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
 *  THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF
 *  ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT
 *  INFRINGEMENT.
 *
 *  Released under a GNU GPL 2.0-style license, please see LICENSE.txt or
 *  contact permission@sei.cmu.edu for full terms.
 *
 *  [DISTRIBUTION STATEMENT A] This material has been approved for public
 *  release and unlimited distribution.  Please see Copyright notice for
 *  non-US Government use and distribution.
 *
 *  Carnegie Mellon(R) and CERT(R) are registered in the U.S. Patent and
 *  Trademark Office by Carnegie Mellon University.
 *
 *  This Software includes and/or makes use of the following Third-Party
 *  Software subject to its own license:
 *
 *  1. GLib-2.0 (https://gitlab.gnome.org/GNOME/glib/-/blob/main/COPYING)
 *     Copyright 1995 GLib-2.0 Team.
 *
 *  2. Doxygen (http://www.gnu.org/licenses/old-licenses/gpl-2.0.html)
 *     Copyright 2021 Dimitri van Heesch.
 *
 *  DM22-0006
 *  @DISTRIBUTION_STATEMENT_END@
 */
