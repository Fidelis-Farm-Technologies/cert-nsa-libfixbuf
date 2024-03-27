/*
 *  Copyright 2014-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file ipfixDump.c
 *  @brief This dumps an ipfix file to outspec or stdout.
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Emily Sarneso
 *  ------------------------------------------------------------------------
 */

#include "ipfixDump.h"

/* CERT IPFIX Private Enterprise Number */
#define CERT_PEN    6871

/* Initial size of the buffer for an in-memory record. */
#define RECBUF_CAPACITY_INITIAL 256

/* Flags that indicate the types of Sets in a Message  */
#define TMPL_MSG 0x1
#define DATA_MSG 0x2

/* String used to wrap long argument descriptions */
#define WRAP_STRING "\n                              "

/* Check whether TemplateField is a particular InfoElement */
#define FIELD_IS_flowEndMilliseconds(f) \
    fbTemplateFieldCheckIdent((f), 0, 153)
#define FIELD_IS_templateId(f) \
    fbTemplateFieldCheckIdent((f), 0, 145)
#define FIELD_IS_templateName(f) \
    fbTemplateFieldCheckIdent((f), CERT_PEN, 1000)


static char        *inspec = NULL;
static char        *outspec = NULL;
static gboolean     id_version = FALSE;
static gboolean     yaf = FALSE;
static gboolean     rfc5610 = FALSE;
static FILE        *outfile = NULL;
static FILE        *infile = NULL;
static gchar       *cert_xml = NULL;
static gchar      **xml_files = NULL;
static gboolean     only_tmpl = FALSE;
static gboolean     only_data = FALSE;
static gboolean     file_stats = FALSE;
static gboolean     tmpl_stats = FALSE;
static gchar       *show_argument = NULL;
static gchar       *octet_format_argument = NULL;
static gchar       *string_format_argument = NULL;
static gchar       *suppress_argument = NULL;

static gboolean     print_tmpl = TRUE;
static gboolean     print_data = TRUE;
static gboolean     print_msg = FALSE;
static gboolean     print_file_stats = FALSE;
static gboolean     print_tmpl_stats = FALSE;

static unsigned int msg_count = 0;
static unsigned int msg_rec_count = 0;
static unsigned int msg_tmpl_count = 0;
static unsigned int tmpl_count = 0;
static gboolean     eom = TRUE;

int id_tmpl_stats[1 + UINT16_MAX];
static int          max_tmpl_id = 0;
static int          min_tmpl_id = UINT16_MAX;
GHashTable         *template_names;

gboolean            suppress_record_number = FALSE;
static gboolean     suppress_export_time = FALSE;
static gboolean     suppress_message_length = FALSE;
static gboolean     suppress_sequence_number = FALSE;
static gboolean     suppress_stream_offset = FALSE;
static gboolean     suppress_messages = FALSE;

OctetArrayFormat_t  octet_array_format = OCTET_ARRAY_SHORTHEX;
StringFormat_t      string_format = STRING_FMT_QUOTED;

static uint32_t     sequence_number = 0;
static size_t       msglen;
static size_t       offset = 0;
static int          curr_msg_set_types;

/* map from --show argument to variable that should be enabled */
static struct showNameValueMap_st {
    const char  *name;
    gboolean    *value;
} showNameValueMap[] = {
    {"data",            &print_data},
    {"templates",       &print_tmpl},
    {"messages",        &print_msg},
    {"file-stats",      &print_file_stats},
    {"template-counts", &print_tmpl_stats},
    {NULL, NULL}
};

/* map from --supress argument to variable that should be enabled */
static struct suppressNameValueMap_st {
    const char  *name;
    gboolean    *value;
} suppressNameValueMap[] = {
    {"export-time",     &suppress_export_time},
    {"stream-offset",   &suppress_stream_offset},
    {"message-length",  &suppress_message_length},
    {"record-number",   &suppress_record_number},
    {"sequence-number", &suppress_sequence_number},
    {"messages",        &suppress_messages},
    {NULL, NULL}
};

/* map from --octet-format argument to setting */
static struct octetformatNameValueMap_st {
    const char          *name;
    OctetArrayFormat_t   value;
} octetformatNameValueMap[] = {
    {"none",        OCTET_ARRAY_NONE},
    {"hexadecimal", OCTET_ARRAY_HEXADECIMAL},
    {"short-hex",   OCTET_ARRAY_SHORTHEX},
    {"base64",      OCTET_ARRAY_BASE64},
    {"string",      OCTET_ARRAY_STRING},
    {"hexdump",     OCTET_ARRAY_HEXDUMP},
    {NULL,          OCTET_ARRAY_NONE}
};

/* map from --string-format argument to setting */
static struct stringformatNameValueMap_st {
    const char      *name;
    StringFormat_t   value;
} stringformatNameValueMap[] = {
    {"quoted",      STRING_FMT_QUOTED},
    {"raw",         STRING_FMT_RAW},
    {"base64",      STRING_FMT_BASE64},
    {"hexadecimal", STRING_FMT_HEXADECIMAL},
    {"hexdump",     STRING_FMT_HEXDUMP},
    {"none",        STRING_FMT_NONE},
    {NULL,          STRING_FMT_NONE}
};


static GOptionEntry id_core_option[] = {
    {"in", 'i', 0, G_OPTION_ARG_STRING, &inspec,
     "Specify file to process [-]", "path"},
    {"out", 'o', 0, G_OPTION_ARG_STRING, &outspec,
     "Specify file to write to [-]", "path"},
    {"rfc5610", '\0', 0, G_OPTION_ARG_NONE, &rfc5610,
     "Add IEs that are read from element type records", NULL},
    {"element-file", 'e', 0, G_OPTION_ARG_FILENAME_ARRAY, &xml_files,
     "Load information elements from the given XML file", "path"},
    {"yaf", 'y', 0, G_OPTION_ARG_NONE, &yaf,
     "Load XML file of CERT information elements", NULL},
    {"templates", 't', 0, G_OPTION_ARG_NONE, &only_tmpl,
     "Print ONLY IPFIX templates that are present", NULL},
    {"data", 'd', 0, G_OPTION_ARG_NONE, &only_data,
     "Print ONLY IPFIX data records that are present", NULL},
    {"file-stats", 'f', 0, G_OPTION_ARG_NONE, &file_stats,
     "Print ONLY File Statistics", NULL},
    {"template-counts", 'c', 0, G_OPTION_ARG_NONE, &tmpl_stats,
     "Print ONLY Template Statistics", NULL},
    {"show", 's', 0, G_OPTION_ARG_STRING, &show_argument,
     ("Print only these specified items. Choices:" WRAP_STRING
      "data,templates,messages,file-stats,template-counts"),
     "csv_list"},
    {"octet-format", '\0', 0, G_OPTION_ARG_STRING, &octet_format_argument,
     ("Print octetArray values in this format [short-hex]" WRAP_STRING
      "Choices: short-hex,hexadecimal,hexdump,base64,string,none"), "format"},
    {"string-format", '\0', 0, G_OPTION_ARG_STRING, &string_format_argument,
     ("Print string values in this format [quoted]" WRAP_STRING
      "Choices: quoted,raw,base64,hexdump,hexadecimal,none"), "format"},
    {"suppress", '\0', 0, G_OPTION_ARG_STRING, &suppress_argument,
     ("Do not display these aspects of the stream." WRAP_STRING
      "Choices: export-time,stream-offset,message-length," WRAP_STRING
      "record-number,sequence-number,messages"), "csv_list"},
    {"version", 'V', 0, G_OPTION_ARG_NONE, &id_version,
     "Print application version to stderr and exit", NULL},
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
};


static void
idPrintVersion(
    void)
{
    fprintf(stderr, "ipfixDump version %s "
            "(C) 2014-2023 Carnegie Mellon University.\n",
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
 *
 *    This file is used by the --yaf switch.
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
        fprintf(stderr, ("%s: Replace --yaf with --element-file and"
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
    int             num_only_args = 0;

    app = g_path_get_basename((*argv)[0]);
    g_set_prgname(app);
    g_free(app);

    ctx = g_option_context_new(" - ipfixDump Options");

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

    if (yaf) {
        cert_xml = idFindCertXml((*argv)[0]);
        if (NULL == cert_xml) {
            exit(1);
        }
    }

    if (tmpl_stats) {
        memset(id_tmpl_stats, 0, sizeof(id_tmpl_stats));
        print_data = FALSE;
        print_tmpl = FALSE;
        print_tmpl_stats = TRUE;
        ++num_only_args;
    }

    if (file_stats) {
        print_data = FALSE;
        print_tmpl = FALSE;
        print_file_stats = TRUE;
        ++num_only_args;
    }

    if (only_data) {
        print_tmpl = FALSE;
        ++num_only_args;
    }

    if (only_tmpl) {
        print_data = FALSE;
        ++num_only_args;
    }

    if (num_only_args > 1) {
        fprintf(stderr, "%s: Using multiple specific print arguments is not "
                "allowed, use --show instead.\n",
                g_get_prgname());
        exit(1);
    }

    /* parse the comma separated value argument to --show */
    if (show_argument != NULL) {
        gchar      **show_list = g_strsplit(show_argument, ",", -1);
        gchar       *show_item;
        unsigned int i, j;
        print_data = FALSE;
        print_tmpl = FALSE;
        print_file_stats = FALSE;
        print_tmpl_stats = FALSE;

        for (i = 0; show_list[i] != NULL; ++i) {
            show_item = g_strstrip(show_list[i]);
            for (j = 0; showNameValueMap[j].name != NULL; ++j) {
                if (0 == strcmp(show_item, showNameValueMap[j].name)) {
                    *showNameValueMap[j].value = TRUE;
                    break;
                }
            }
            /* warn about invalid argument unless empty */
            if (NULL == showNameValueMap[j].name && '\0' != show_item[0]) {
                fprintf(stderr, "%s: Unrecognized show option: %s\n",
                        g_get_prgname(), show_item);
                g_strfreev(show_list);
                exit(1);
            }
        }
        g_strfreev(show_list);
    }

    /* parse the single argument to --octet-format */
    if (octet_format_argument) {
        unsigned int i;
        for (i = 0; octetformatNameValueMap[i].name != NULL; ++i) {
            if (0 ==
                strcmp(octet_format_argument, octetformatNameValueMap[i].name))
            {
                octet_array_format = octetformatNameValueMap[i].value;
                break;
            }
        }
        if (NULL == octetformatNameValueMap[i].name) {
            fprintf(stderr, "%s: Unrecognized octet-format option: %s\n",
                    g_get_prgname(), octet_format_argument);
            exit(1);
        }
    }

    /* parse the single argument to --string-format */
    if (string_format_argument) {
        unsigned int i;
        for (i = 0; stringformatNameValueMap[i].name != NULL; ++i) {
            if (0 == strcmp(string_format_argument,
                            stringformatNameValueMap[i].name))
            {
                string_format = stringformatNameValueMap[i].value;
                break;
            }
        }
        if (NULL == stringformatNameValueMap[i].name) {
            fprintf(stderr, "%s: Unrecognized string-format option: %s\n",
                    g_get_prgname(), string_format_argument);
            exit(1);
        }
    }

    /* parse the comma separated value argument to --suppress */
    if (suppress_argument) {
        gchar      **suppress_list = g_strsplit(suppress_argument, ",", -1);
        gchar       *item;
        unsigned int i, j;
        for (i = 0; suppress_list[i] != NULL; ++i) {
            item = g_strstrip(suppress_list[i]);
            for (j = 0; suppressNameValueMap[j].name != NULL; ++j) {
                if (0 == strcmp(item, suppressNameValueMap[j].name)) {
                    *suppressNameValueMap[j].value = TRUE;
                    break;
                }
            }
            /* warn about invalid argument unless empty */
            if (NULL == suppressNameValueMap[j].name && '\0' != item[0]) {
                fprintf(stderr, "%s: Unrecognized suppress option: %s\n",
                        g_get_prgname(), item);
                g_strfreev(suppress_list);
                exit(1);
            }
        }
        g_strfreev(suppress_list);
    }

    /* check for non-arguments in argv */
    if (1 != *argc) {
        fprintf(stderr, "%s: Unrecognized argument %s\n",
                g_get_prgname(), (*argv)[1]);
        exit(1);
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
 *    Free the template context object.  The signature of this
 *    function is given by fbTemplateCtxFree_fn.
 */
static void
templateFree(
    void  *ctx,
    void  *app_ctx)
{
    tmplContext_t *tctx = (tmplContext_t *)ctx;

    (void)app_ctx;
    g_free(tctx);
}

/**
 *    Print statistics for the current message if the msg_count is
 *    non-zero.
 */
static void
idCloseMessage(
    void)
{
    const char *prefix = "\n*** Msg Stats:";

    /* the 'msg_count' check is to avoid creating output before the first
     * message is seen. */
    if (msg_count && !suppress_messages) {
        if (print_msg || (msg_rec_count && print_data)) {
            fprintf(outfile, "%s %u Data Records", prefix, msg_rec_count);
            prefix = ",";
        }
        if (print_msg || (msg_tmpl_count && print_tmpl)) {
            fprintf(outfile, "%s %u Template Records", prefix, msg_tmpl_count);
            prefix = ",";
        }
        if ('\n' != prefix[0]) {
            fprintf(outfile, " ***\n\n");
        }
    }
    tmpl_count += msg_tmpl_count;
}


/**
 *    Print a textual representation of an IPFIX message header to
 *    'outfile'.
 */
static void
idPrintHeader(
    fBuf_t          *fbuf,
    const uint32_t   seq_number,
    const size_t     msg_length,
    const size_t     stream_offset)
{
    const fbSession_t *session;
    long epochtime;
    struct tm          time_tm;
    char timebuf[32];

    session = fBufGetSession(fbuf);

    if (suppress_export_time) {
        snprintf(timebuf, sizeof(timebuf), "%19s", "");
    } else {
        /* "%Y-%m-%d %H:%M:%S" */
        epochtime = fBufGetExportTime(fbuf);
        strftime(timebuf, sizeof(timebuf), "%F %T",
                 gmtime_r(&epochtime, &time_tm));
    }

    fprintf(outfile, "--- Message Header ---\n");
    fprintf(outfile, "export time: %s\t", timebuf);
    fprintf(outfile, "observation domain id: %u\n",
            fbSessionGetDomain(session));
    if (!suppress_message_length) {
        fprintf(outfile, "message length: %-16zu\t", msg_length);
    } else {
        fprintf(outfile, "message length: %16s\t", "");
    }
    if (!suppress_sequence_number) {
        fprintf(outfile, "sequence number: %u (%#x)\n",
                seq_number, seq_number);
    } else {
        fprintf(outfile, "sequence number:\n");
    }
    if (!suppress_stream_offset) {
        fprintf(outfile, "stream offset: %zu\n", stream_offset);
    }
}


/**
 *    Close the current message, print the header for the new message,
 *    and reset the message counters.
 */
static void
idNewMessage(
    fBuf_t  *fbuf)
{
    idCloseMessage();

    if (!suppress_messages &&
        (print_msg ||
         (print_data && (curr_msg_set_types & DATA_MSG)) ||
         (print_tmpl && (curr_msg_set_types & TMPL_MSG))))
    {
        idPrintHeader(fbuf, sequence_number, msglen, offset - msglen);
    }

    eom = FALSE;
    /* reset msg counters */
    msg_rec_count = 0;
    msg_tmpl_count = 0;
    ++msg_count;
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
    const fbTemplateField_t *field;
    gboolean       need_id;
    gboolean       need_name;
    gboolean       need_time;
    uint32_t       i;
    int            len;

    if (eom) {
        idNewMessage((fBuf_t *)app_ctx);
    }

    myctx->count = fbTemplateCountElements(tmpl);
    myctx->scope = fbTemplateGetOptionsScope(tmpl);
    myctx->len = fbTemplateGetIELenOfMemBuffer(tmpl);
    myctx->tid = tid;
    myctx->iename_width = MIN_IENAME_WIDTH;

    if (!fbSessionAddTemplate(session, TRUE, tid, tmpl, NULL, &err)) {
        fprintf(stderr, "%s: Error adding template to session: %s\n",
                g_get_prgname(), err->message);
        g_clear_error(&err);
    }

    /* mark every tmpl we have received */
    if (id_tmpl_stats[tid] == 0) {
        id_tmpl_stats[tid] = 1;
    }
    if (tid > max_tmpl_id) {
        max_tmpl_id = tid;
    }
    if (tid < min_tmpl_id) {
        min_tmpl_id = tid;
    }

    /* which element(s) to look for in the template */
    if (myctx->scope == 0) {
        /* look for flowEndMilliseconds */
        need_time = TRUE;
        need_id = need_name = FALSE;
    } else if (rfc5610 && fbTemplateIsMetadata(tmpl, FB_TMPL_IS_META_ELEMENT)) {
        /* no need to look for anything */
        myctx->is_meta_element = TRUE;
        need_id = need_name = need_time = FALSE;
    } else {
        /* look for templateId and templateName */
        need_id = need_name = TRUE;
        need_time = FALSE;
    }

    /* examine template, looking for specific fields and also finding the
     * length of the longest IE name */
    for (i = 0; i < myctx->count; ++i) {
        field = fbTemplateGetFieldByPosition(tmpl, i);
        len = strlen(fbTemplateFieldGetName(field));
        if (len > myctx->iename_width) {
            myctx->iename_width = len;
        }
        if (need_time && FIELD_IS_flowEndMilliseconds(field)) {
            /* flowEndMilliseconds */
            myctx->end_time_index = i;
            myctx->contains_end_time = TRUE;
            need_time = FALSE;
        } else if (need_id && FIELD_IS_templateId(field)) {
            /* templateId */
            myctx->meta_tmpl[0] = i;
            need_id = FALSE;
            if (!need_name) {
                myctx->is_meta_template = TRUE;
            }
        } else if (need_name && FIELD_IS_templateName(field)) {
            /* templateName */
            myctx->meta_tmpl[1] = i;
            need_name = FALSE;
            if (!need_id) {
                myctx->is_meta_template = TRUE;
            }
        }
    }

    myctx->iename_width *= -1;
    if (print_tmpl) {
        idPrintTemplate(outfile, tmpl, myctx);
    }

    ++msg_tmpl_count;
    *ctx = myctx;
    *fn = templateFree;
}


/**
 *  Visits the subTemplate(Multi)Lists in 'record' to count the number of
 *  times each template is used.
 *
 *  This function is called when print_data is FALSE and print_tmpl_stats is
 *  TRUE.
 */
static void
idUpdateTemplateCounts(
    const fbRecord_t  *record)
{
    const fbTemplateField_t *field;
    fbRecord_t subrec = FB_RECORD_INIT;
    uint16_t   pos;

    /* visit any STLs */
    for (pos = 0;
         (field = fbTemplateFindFieldByDataType(
              record->tmpl, FB_SUB_TMPL_LIST, &pos, 0));
         ++pos)
    {
        fbSubTemplateList_t *stl;

        stl = ((fbSubTemplateList_t *)
               (record->rec + fbTemplateFieldGetOffset(field)));
        subrec.tid = fbSubTemplateListGetTemplateID(stl);
        id_tmpl_stats[subrec.tid] += fbSubTemplateListCountElements(stl);
        subrec.tmpl = fbSubTemplateListGetTemplate(stl);
        subrec.rec = NULL;
        while ((subrec.rec = fbSTLNext(uint8_t, stl, subrec.rec))) {
            idUpdateTemplateCounts(&subrec);
        }
        fbSubTemplateListClear(stl);
    }

    /* visit any STMLs */
    for (pos = 0;
         (field = fbTemplateFindFieldByDataType(
              record->tmpl, FB_SUB_TMPL_MULTI_LIST, &pos, 0));
         ++pos)
    {
        fbSubTemplateMultiList_t      *stml;
        fbSubTemplateMultiListEntry_t *entry;

        stml = ((fbSubTemplateMultiList_t *)
                (record->rec + fbTemplateFieldGetOffset(field)));
        entry = NULL;
        while ((entry = fbSubTemplateMultiListGetNextEntry(stml, entry))) {
            subrec.tid = fbSubTemplateMultiListEntryGetTemplateID(entry);
            id_tmpl_stats[subrec.tid] +=
                fbSubTemplateMultiListEntryCountElements(entry);
            subrec.tmpl = fbSubTemplateMultiListEntryGetTemplate(entry);
            subrec.rec = NULL;
            while ((subrec.rec = fbSTMLEntryNext(uint8_t, entry, subrec.rec))) {
                idUpdateTemplateCounts(&subrec);
            }
        }
        fbSubTemplateMultiListClear(stml);
    }

    /* clear any BLs */
    for (pos = 0;
         (field = fbTemplateFindFieldByDataType(
              record->tmpl, FB_BASIC_LIST, &pos, 0));
         ++pos)
    {
        fbBasicList_t *bl;

        bl = ((fbBasicList_t *)
              (record->rec + fbTemplateFieldGetOffset(field)));
        fbBasicListClear(bl);
    }
}


/**
 *    Parse a template name options record and insert the TID/name
 *    pair into the template_names hash table.
 */
static void
idTemplateNameRecord(
    fbInfoModel_t        *model,
    const tmplContext_t  *tcxt,
    const fbRecord_t     *record)
{
    const fbTemplateField_t *field;
    fbRecordValue_t          value = FB_RECORD_VALUE_INIT;
    uint16_t tid;

    (void)model;

    /* templateId */
    field = fbTemplateGetFieldByPosition(record->tmpl, tcxt->meta_tmpl[0]);
    fbRecordCopyFieldValue(record, field, &tid, sizeof(tid));

    /* templateName */
    field = fbTemplateGetFieldByPosition(record->tmpl, tcxt->meta_tmpl[1]);
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
    fbTemplateIter_t         iter = FB_TEMPLATE_ITER_NULL;
    fbInfoElementOptRec_t    rec;
    const fbTemplateField_t *field;

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


static struct timespec *
idMillisToTimespec(
    struct timespec  *ts,
    uint64_t          millisec)
{
    ts->tv_sec = millisec / 1000;
    ts->tv_nsec = (millisec % 1000) * 1000000;
    return ts;
}


/* Returns a number representing whether or not the message contains
 * (option) templates sets, data sets or both */
static uint8_t
getMsgSetTypes(
    const uint8_t  *msgbuf,
    size_t          msg_len)
{
    uint16_t set_len;
    size_t   msg_offset = 16;    /* account for msg_header */
    uint16_t set_id;
    uint8_t  retval = 0;

    while (msg_offset < msg_len && retval != (TMPL_MSG | DATA_MSG)) {
        set_id = ntohs(*((uint16_t *)(msgbuf + msg_offset)));
        if (set_id == FB_TID_TS || set_id == FB_TID_OTS) {
            retval |= TMPL_MSG;
        } else if (set_id >= FB_TID_MIN_DATA) {
            retval |= DATA_MSG;
        }
        set_len = ntohs(*((uint16_t *)(msgbuf + msg_offset + 2)));
        msg_offset += set_len;
    }
    return retval;
}


int
main(
    int    argc,
    char  *argv[])
{
    uint8_t        msgbuf[UINT16_MAX];
    fbInfoModel_t *model = NULL;
    fBuf_t        *fbuf = NULL;
    fbTemplate_t  *tmpl = NULL;
    GError        *err = NULL;
    gboolean       rc;
    fbSession_t   *session = NULL;
    tmplContext_t *tctx;
    int            rec_count = 0;
    gchar        **xml_file;
    int            i;
    size_t         got;
    uint64_t       start_millis = 0;
    uint64_t       last_millis = 0;
    uint64_t       max_millis = 0;
    uint64_t       min_millis = UINT64_MAX;
    uint32_t       start_msg_secs = 0;
    uint32_t       last_msg_secs = 0;
    fbRecord_t     record = FB_RECORD_INIT;

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

    /* Create table to hold template names */
    template_names = g_hash_table_new_full(g_direct_hash, NULL, NULL, g_free);

    /* Allocate Collection Buffer */
    fbuf = fBufAllocForCollection(session, NULL);

    fBufSetAutomaticMode(fbuf, FALSE);

    fbSessionAddNewTemplateCallback(session, idTemplateCallback, fbuf);

    /* Allocate buffer for a single in-memory record */
    record.reccapacity = RECBUF_CAPACITY_INITIAL;
    record.rec = g_new(uint8_t, record.reccapacity);

    for (;;) {
        tmpl = fBufNextCollectionTemplate(fbuf, &record.tid, &err);
        if (!tmpl) {
            /* If no template - no message */
            if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_EOF)) {
                fBufFree(fbuf);
                g_clear_error(&err);
                eom = TRUE;
                break;
            }
            if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_BUFSZ)) {
                got = fread(msgbuf, 1, 4, infile);
                if (got != 4) {
                    if (0 != got) {
                        fprintf(stderr, "%s: Read %zu octets of 4 expected\n",
                                g_get_prgname(), got);
                    }
                    fBufFree(fbuf);
                    g_clear_error(&err);
                    eom = TRUE;
                    break;
                }
                if (ntohs(*((uint16_t *)(msgbuf))) != 10) {
                    fprintf(stderr,
                            "%s: Error: Illegal IPFIX Message version %#04x; "
                            "input is probably not an IPFIX Message stream.\n",
                            g_get_prgname(), ntohs(*((uint16_t *)(msgbuf))));
                    fBufFree(fbuf);
                    g_clear_error(&err);
                    eom = TRUE;
                    break;
                }
                msglen = ntohs(*((uint16_t *)(msgbuf + 2)));
                if (msglen < 16) {
                    fprintf(stderr,
                            "%s: Message length %zu too short to be IPFIX\n",
                            g_get_prgname(), msglen);
                    fBufFree(fbuf);
                    g_clear_error(&err);
                    eom = TRUE;
                    break;
                }
                got = fread(msgbuf + 4, 1, msglen - 4, infile);
                if (got < msglen - 4) {
                    fprintf(stderr, "%s: Read %zu octets of %zu expected\n",
                            g_get_prgname(), got, msglen - 4);
                    fBufFree(fbuf);
                    g_clear_error(&err);
                    eom = TRUE;
                    break;
                }
                sequence_number = ntohl(*((uint32_t *)(msgbuf + 8)));
                curr_msg_set_types = getMsgSetTypes(msgbuf, msglen);
                fBufSetBuffer(fbuf, msgbuf, msglen);
                offset += msglen;
                eom = TRUE;
            } else if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_EOM)) {
                eom = TRUE;
            } else {
                fprintf(stderr, "%s: Warning: %s\n",
                        g_get_prgname(), err->message);
            }
            g_clear_error(&err);
            continue;
        }

        if (eom) {
            idNewMessage(fbuf);

            if (print_file_stats) {
                last_msg_secs = fBufGetExportTime(fbuf);
                if (start_msg_secs == 0) {
                    start_msg_secs = last_msg_secs;
                }
            }
        }

        tctx = fbTemplateGetContext(tmpl);
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
                eom = TRUE;
                fprintf(stderr, "%s: END OF FILE\n", g_get_prgname());
                fBufFree(fbuf);
                g_clear_error(&err);
                break;
            }
            if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_EOM)) {
                eom = TRUE;
            } else if (g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_BUFSZ)) {
                eom = TRUE;
            } else {
                fprintf(stderr, "%s: Warning: %s\n",
                        g_get_prgname(), err->message);
            }
            g_clear_error(&err);
            continue;
        }

        ++id_tmpl_stats[record.tid];
        ++rec_count;
        ++msg_rec_count;

        if (tctx->is_meta_template) {
            idTemplateNameRecord(model, tctx, &record);
        } else if (tctx->is_meta_element) {
            idInfoElementRecord(model, tctx, &record);
        }

        /* Gather timestamps for file stats */
        if (print_file_stats && tctx->contains_end_time) {
            const fbTemplateField_t *field = fbTemplateGetFieldByPosition(
                record.tmpl, tctx->end_time_index);
            fbRecordCopyFieldValue(
                &record, field, &last_millis, sizeof(last_millis));
            if (start_millis == 0) {
                start_millis = last_millis;
            }
            if (last_millis > max_millis) {
                max_millis = last_millis;
            }
            if (last_millis < min_millis) {
                min_millis = last_millis;
            }
        }

        if (print_data) {
            idPrintDataRecord(outfile, tctx, &record, rec_count,
                              "Data", "");
        } else if (print_tmpl_stats) {
            idUpdateTemplateCounts(&record);
        } else {
            fbRecordFreeLists(&record);
        }
    }

    if (eom) {
        idCloseMessage();
    }

    fbInfoModelFree(model);
    g_free(record.rec);

    /* account for missing normally newline printed by idCloseMessage */
    if (suppress_messages && (print_data || print_tmpl)) {
        fprintf(outfile, "\n");
    }

    if (print_tmpl_stats) {
        if (g_hash_table_size(template_names) == 0) {
            /* print the template usage counts without the names */
            fprintf(outfile, "  Template ID |  Records\n");
            for (i = min_tmpl_id; i <= max_tmpl_id; i++) {
                if (id_tmpl_stats[i] > 0) {
                    fprintf(outfile, "%5d (%#06x)|%9d\n",
                            i, i, id_tmpl_stats[i] - 1);
                }
            }
        } else {
            /* print the template usage counts with the names; in the format,
             * the count column's width is eleven, that is not a long long
             * modifier */
            const char *name;
            fprintf(outfile, "  Template ID |  Records  | Template Name\n");
            for (i = min_tmpl_id; i <= max_tmpl_id; i++) {
                if (id_tmpl_stats[i] > 0) {
                    name = (char *)g_hash_table_lookup(template_names,
                                                       GINT_TO_POINTER(i));
                    fprintf(outfile, "%5d (%#06x)|%11d| %s\n",
                            i, i, id_tmpl_stats[i] - 1, ((name) ? name : ""));
                }
            }
        }
    }
    if (print_file_stats) {
        struct timespec ts;
        fprintf(outfile, "Time stats:\n");
        fprintf(outfile, "First record time:  ");
        if (UINT64_MAX == min_millis) {
            fprintf(outfile, "N/A\n");
        } else {
            idPrintTimestamp(
                outfile, idMillisToTimespec(&ts, start_millis), FB_DT_MILSEC);
        }
        fprintf(outfile, "Last record time:   ");
        if (UINT64_MAX == min_millis) {
            fprintf(outfile, "N/A\n");
        } else {
            idPrintTimestamp(
                outfile, idMillisToTimespec(&ts, last_millis), FB_DT_MILSEC);
        }
        fprintf(outfile, "Min record time:    ");
        if (UINT64_MAX == min_millis) {
            fprintf(outfile, "N/A\n");
        } else {
            idPrintTimestamp(
                outfile, idMillisToTimespec(&ts, min_millis), FB_DT_MILSEC);
        }
        fprintf(outfile, "Max record time:    ");
        if (UINT64_MAX == min_millis) {
            fprintf(outfile, "N/A\n");
        } else {
            idPrintTimestamp(
                outfile, idMillisToTimespec(&ts, max_millis), FB_DT_MILSEC);
        }
        fprintf(outfile, "First message time: ");
        if (0 == msg_count) {
            fprintf(outfile, "N/A\n");
        } else {
            ts.tv_nsec = 0;
            ts.tv_sec = start_msg_secs;
            idPrintTimestamp(outfile, &ts, FB_DT_SEC);
        }
        fprintf(outfile, "Last message time:  ");
        if (0 == msg_count) {
            fprintf(outfile, "N/A\n");
        } else {
            ts.tv_sec = last_msg_secs;
            idPrintTimestamp(outfile, &ts, FB_DT_SEC);
        }
        fprintf(outfile, "\n");
    }

    if (print_data || print_tmpl || print_msg || print_file_stats) {
        fprintf(outfile, "*** File Stats: %u Messages, %u Data Records, "
                "%u Template Records ***\n", msg_count, rec_count, tmpl_count);
    }

    g_hash_table_destroy(template_names);

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
