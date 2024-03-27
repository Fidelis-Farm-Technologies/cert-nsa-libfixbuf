/*
 *  Copyright 2014-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file ipfixDumpPrint.c
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Emily Sarneso
 *  ------------------------------------------------------------------------
 */

#include "ipfixDump.h"

/* size of buffer to hold indentation prefix */
#define PREFIX_BUFSIZ       256

/* size of buffer to hold element name and (ent/id) */
#define ELEMENT_BUFSIZ      128

/* size of buffer to hold template ID and name */
#define TMPL_NAME_BUFSIZ    128

/* size of buffer to hold list semantic */
#define SEMANTIC_BUFSIZ      32

static void
idPrintValue(
    FILE                     *fp,
    const fbTemplateField_t  *field,
    fbRecordValue_t          *value,
    char                     *str_prefix);


/**
 * mdPrintIP6Address
 *
 *
 */
static void
mdPrintIP6Address(
    char     *ipaddr_buf,
    uint8_t  *ipaddr)
{
    char     *cp = ipaddr_buf;
    uint16_t *aqp = (uint16_t *)ipaddr;
    uint16_t  aq;
    gboolean  colon_start = FALSE;
    gboolean  colon_end = FALSE;

    for (; (uint8_t *)aqp < ipaddr + 16; aqp++) {
        aq = g_ntohs(*aqp);
        if (aq || colon_end) {
            if ((uint8_t *)aqp < ipaddr + 14) {
                snprintf(cp, 6, "%04hx:", aq);
                cp += 5;
            } else {
                snprintf(cp, 5, "%04hx", aq);
                cp += 4;
            }
            if (colon_start) {
                colon_end = TRUE;
            }
        } else if (!colon_start) {
            if ((uint8_t *)aqp == ipaddr) {
                snprintf(cp, 3, "::");
                cp += 2;
            } else {
                snprintf(cp, 2, ":");
                cp += 1;
            }
            colon_start = TRUE;
        }
    }
}


/**
 *    Sets 'dest' to the contents of 'current' plus one indentation layer.
 *
 *    Using this function makes it easy to change the indentation.
 *
 */
static void
idAddIndentLevel(
    char        *dest,
    const char  *current,
    size_t       dest_bufsiz)
{
    /* pointless if() quiets truncation warning in gcc-7.1 */
    if ((size_t)snprintf(dest, dest_bufsiz, "%s+", current) >= dest_bufsiz) {}
}


/**
 *    Puts textual information about the template whose ID is 'tid'
 *    into 'tmpl_str'.  The information is the template ID (both
 *    decimal and hex) and the template name if available.
 *
 *    @param tmpl_str        The buffer to write to
 *    @param tid             The ID of the template to get info of
 *    @param tmpl_str_bufsiz The sizeof the tmpl_str buffer
 *
 */
static void
idFormatTemplateId(
    char    *tmpl_str,
    int      tid,
    size_t   tmpl_str_bufsiz)
{
    const char *name;

    name = (char *)g_hash_table_lookup(template_names, GINT_TO_POINTER(tid));
    if (name) {
        snprintf(tmpl_str, tmpl_str_bufsiz, "%5u (%#06x) %s", tid, tid, name);
    } else {
        snprintf(tmpl_str, tmpl_str_bufsiz, "%5u (%#06x)", tid, tid);
    }
}


static void
idFormatListSemantic(
    char    *sem_str,
    int      semantic,
    size_t   sem_str_bufsiz)
{
    switch (semantic) {
      case 0:
        snprintf(sem_str, sem_str_bufsiz, "{noneOf}");
        break;
      case 1:
        snprintf(sem_str, sem_str_bufsiz, "{exactlyOneOf}");
        break;
      case 2:
        snprintf(sem_str, sem_str_bufsiz, "{oneOrMoreOf}");
        break;
      case 3:
        snprintf(sem_str, sem_str_bufsiz, "{allOf}");
        break;
      case 4:
        snprintf(sem_str, sem_str_bufsiz, "{ordered}");
        break;
      case 0xFF:
        snprintf(sem_str, sem_str_bufsiz, "{undefined}");
        break;
      default:
        snprintf(sem_str, sem_str_bufsiz, "{unassigned(%u)}", semantic);
        break;
    }
}


/**
 *    Puts textual information about the InfoElement 'ie' into into
 *    'elem_str'.  The information is the ID (and enterprise ID when
 *    non-standard) and the element's name.  The 'in_basicList' flag
 *    changes how the element's name is accessed and changes the
 *    formatting by removing whitespace between the ID and name.
 */
static void
idFormatElement(
    char                     *elem_str,
    const fbTemplateField_t  *field,
    size_t                    elem_str_bufsiz,
    int                       ie_name_maxlen,
    gboolean                  in_basicList)
{
    char buf[32];

    if (0 == fbTemplateFieldGetPEN(field)) {
        snprintf(buf, sizeof(buf), "(%d)",
                 fbTemplateFieldGetId(field));
    } else {
        snprintf(buf, sizeof(buf), "(%d/%d)",
                 fbTemplateFieldGetPEN(field), fbTemplateFieldGetId(field));
    }
    if (in_basicList) {
        snprintf(elem_str, elem_str_bufsiz, "%*s %s",
                 ie_name_maxlen, fbTemplateFieldGetName(field), buf);
    } else {
        snprintf(elem_str, elem_str_bufsiz, "%*s%13s",
                 ie_name_maxlen, fbTemplateFieldGetName(field), buf);
    }
}


/**
 *    Formats a timestamp and prints it to the handle 'fp'.  The
 *    timestamp is given as an UNIX epoch offset, with seconds in
 *    'sec' and fractional seconds in 'frac'.  The value 'frac_places'
 *    is the number of fractional digits (0 for seconds, 3 for milli,
 *    6 for micro, 9 for nano).
 */
void
idPrintTimestamp(
    FILE                     *fp,
    const struct timespec    *ts,
    fbInfoElementDataType_t   datatype)
{
    struct tm time_tm;
    char      timebuf[32];

    /* "%Y-%m-%d %H:%M:%S" */
    strftime(timebuf, sizeof(timebuf), "%F %T",
             gmtime_r(&ts->tv_sec, &time_tm));

    switch (datatype) {
      case FB_DT_SEC:
        fprintf(fp, "%s\n", timebuf);
        break;
      case FB_DT_MILSEC:
        fprintf(fp, "%s.%03ld\n", timebuf, ts->tv_nsec / 1000000);
        break;
      case FB_DT_MICROSEC:
        fprintf(fp, "%s.%06ld\n", timebuf, ts->tv_nsec / 1000);
        break;
      case FB_DT_NANOSEC:
        fprintf(fp, "%s.%09ld\n", timebuf, ts->tv_nsec);
        break;
      default:
        g_error("Invalid datatype %d", datatype);
    }
}


/**
 *    Puts the name of the datatype given by 'dt' into 'dt_str'.
 */
static void
idFormatDataType(
    char     *dt_str,
    uint8_t   dt,
    size_t    dt_str_bufsiz)
{
    switch (dt) {
      case FB_OCTET_ARRAY:
        strncpy(dt_str, "<octets>", dt_str_bufsiz);
        break;
      case FB_UINT_8:
        strncpy(dt_str, "<uint8>", dt_str_bufsiz);
        break;
      case FB_UINT_16:
        strncpy(dt_str, "<uint16>", dt_str_bufsiz);
        break;
      case FB_UINT_32:
        strncpy(dt_str, "<uint32>", dt_str_bufsiz);
        break;
      case FB_UINT_64:
        strncpy(dt_str, "<uint64>", dt_str_bufsiz);
        break;
      case FB_INT_8:
        strncpy(dt_str, "<int8>", dt_str_bufsiz);
        break;
      case FB_INT_16:
        strncpy(dt_str, "<int16>", dt_str_bufsiz);
        break;
      case FB_INT_32:
        strncpy(dt_str, "<int32>", dt_str_bufsiz);
        break;
      case FB_INT_64:
        strncpy(dt_str, "<int64>", dt_str_bufsiz);
        break;
      case FB_FLOAT_32:
        strncpy(dt_str, "<float32>", dt_str_bufsiz);
        break;
      case FB_FLOAT_64:
        strncpy(dt_str, "<float64>", dt_str_bufsiz);
        break;
      case FB_BOOL:
        strncpy(dt_str, "<boolean>", dt_str_bufsiz);
        break;
      case FB_MAC_ADDR:
        strncpy(dt_str, "<macaddr>", dt_str_bufsiz);
        break;
      case FB_STRING:
        strncpy(dt_str, "<string>", dt_str_bufsiz);
        break;
      case FB_DT_SEC:
        strncpy(dt_str, "<seconds>", dt_str_bufsiz);
        break;
      case FB_DT_MILSEC:
        strncpy(dt_str, "<millisec>", dt_str_bufsiz);
        break;
      case FB_DT_MICROSEC:
        strncpy(dt_str, "<microsec>", dt_str_bufsiz);
        break;
      case FB_DT_NANOSEC:
        strncpy(dt_str, "<nanosec>", dt_str_bufsiz);
        break;
      case FB_IP4_ADDR:
        strncpy(dt_str, "<ipv4>", dt_str_bufsiz);
        break;
      case FB_IP6_ADDR:
        strncpy(dt_str, "<ipv6>", dt_str_bufsiz);
        break;
      case FB_BASIC_LIST:
        strncpy(dt_str, "<bl>", dt_str_bufsiz);
        break;
      case FB_SUB_TMPL_LIST:
        strncpy(dt_str, "<stl>", dt_str_bufsiz);
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        strncpy(dt_str, "<stml>", dt_str_bufsiz);
        break;
      default:
        snprintf(dt_str, dt_str_bufsiz, "<%d>", dt);
    }
}


/**
 *    Print a textual representation of 'tmpl' to 'fp'.  'ctx' is the
 *    template context created when the template was first read.
 */
void
idPrintTemplate(
    FILE                 *fp,
    const fbTemplate_t   *tmpl,
    const tmplContext_t  *ctx)
{
    uint32_t    count = ctx->count;
    uint16_t    scope = ctx->scope;
    char        dt_str[25];
    char        len_str[16];
    char        ident[32];
    const fbTemplateField_t *field = NULL;
    fbTemplateIter_t iter;
    const char *name;

    fprintf(fp, "\n---%s Template Record", (ctx->scope ? " Options" : ""));
    fprintf(fp, " --- tid: %5u (%#06x), fields: %u, scope: %u",
            ctx->tid, ctx->tid, count, scope);
    name = (char *)g_hash_table_lookup(template_names,
                                       GINT_TO_POINTER(ctx->tid));
    if (name) {
        fprintf(fp, ", name: %s", name);
    }
    fprintf(fp, " ---\n");

    fbTemplateIterInit(&iter, tmpl);
    while ((field = fbTemplateIterNext(&iter)) != NULL) {
        idFormatDataType(dt_str, fbTemplateFieldGetType(field), sizeof(dt_str));
        fprintf(fp, "  %*s", ctx->iename_width, fbTemplateFieldGetName(field));
        if (fbTemplateFieldGetPEN(field)) {
            snprintf(ident, sizeof(ident), "(%u/%u)",
                     fbTemplateFieldGetPEN(field), fbTemplateFieldGetId(field));
        } else {
            snprintf(ident, sizeof(ident), "(%u)",
                     fbTemplateFieldGetId(field));
        }
#if 0
        if (FB_IE_VARLEN == fbTemplateFieldGetLen(field)) {
            snprintf(len_str, sizeof(len_str), "[v]");
        } else
#endif
        {
            snprintf(len_str, sizeof(len_str), "[%u]",
                     fbTemplateFieldGetLen(field));
        }
        fprintf(fp, "%13s %-10s %7s%s\n", ident, dt_str, len_str,
                ((fbTemplateIterGetPosition(&iter) < scope) ? " {scope}" : ""));
    }
}



/**
 *    Print a textual representation of 'entry' to 'fp'.  'index' is
 *    the location of the entry in the STML.
 */
static void
idPrintSTMLEntry(
    FILE                                 *fp,
    const fbSubTemplateMultiListEntry_t  *entry,
    unsigned int                          index,
    char                                 *prefix)
{
    char           str_template[TMPL_NAME_BUFSIZ];
    fbRecord_t     subrec = FB_RECORD_INIT;
    tmplContext_t *tctx;

    subrec.tid = fbSubTemplateMultiListEntryGetTemplateID(entry);
    subrec.tmpl = fbSubTemplateMultiListEntryGetTemplate(entry);
    if (subrec.tmpl) {
        tctx = (tmplContext_t *)fbTemplateGetContext(subrec.tmpl);
    } else {
        tctx = NULL;
    }

    idFormatTemplateId(str_template, subrec.tid, sizeof(str_template));

    while ((subrec.rec = fbSTMLEntryNext(uint8_t, entry, subrec.rec))) {
        ++id_tmpl_stats[subrec.tid];
        ++index;
        idPrintDataRecord(fp, tctx, &subrec, index, "STML", prefix);
    }
}


/**
 *    Print a textual representation of 'stl' to 'fp'.
 */
static void
idPrintSTL(
    FILE                       *fp,
    const fbSubTemplateList_t  *stl,
    char                       *parent_prefix)
{
    unsigned int   i = 0;
    char           prefix[PREFIX_BUFSIZ];
    char           str_template[TMPL_NAME_BUFSIZ];
    char           str_semantic[SEMANTIC_BUFSIZ];
    fbRecord_t     subrec = FB_RECORD_INIT;
    tmplContext_t *tctx;

    idFormatListSemantic(str_semantic, fbSubTemplateListGetSemantic(stl),
                         sizeof(str_semantic));
    subrec.tid = fbSubTemplateListGetTemplateID(stl);
    subrec.tmpl = fbSubTemplateListGetTemplate(stl);
    if (subrec.tmpl) {
        tctx = (tmplContext_t *)fbTemplateGetContext(subrec.tmpl);
    } else {
        tctx = NULL;
    }

    idFormatTemplateId(str_template, subrec.tid, sizeof(str_template));

    fprintf(fp, "count: %u %s tmpl: %s\n",
            fbSubTemplateListCountElements(stl), str_semantic, str_template);

    idAddIndentLevel(prefix, parent_prefix, sizeof(prefix));

    while ((subrec.rec = fbSTLNext(uint8_t, stl, subrec.rec))) {
        ++id_tmpl_stats[subrec.tid];
        ++i;
        idPrintDataRecord(fp, tctx, &subrec, i, "STL", prefix);
    }
}


/**
 *    Print a textual representation of 'stml' to 'fp'.
 */
static void
idPrintSTML(
    FILE                            *fp,
    const fbSubTemplateMultiList_t  *stml,
    char                            *parent_prefix)
{
    fbSubTemplateMultiListEntry_t *entry = NULL;
    char         prefix[PREFIX_BUFSIZ];
    char         str_semantic[SEMANTIC_BUFSIZ];
    unsigned int i;

    /* count number of records in the complete STML */
    i = 0;
    while ((entry = fbSubTemplateMultiListGetNextEntry(stml, entry))) {
        i += fbSubTemplateMultiListEntryCountElements(entry);
    }

    idFormatListSemantic(str_semantic, fbSubTemplateMultiListGetSemantic(stml),
                         sizeof(str_semantic));
    fprintf(fp, "count: %u %s templates: %u\n",
            i, str_semantic, fbSubTemplateMultiListCountElements(stml));

    idAddIndentLevel(prefix, parent_prefix, sizeof(prefix));

    i = 0;
    while ((entry = fbSTMLNext(stml, entry))) {
        idPrintSTMLEntry(fp, entry, i, prefix);
        i += fbSubTemplateMultiListEntryCountElements(entry);
    }
}


/**
 *    Print a textual representation of 'bl' to 'fp'.
 */
static void
idPrintBL(
    FILE                 *fp,
    const fbBasicList_t  *bl,
    const char           *parent_prefix)
{
    const fbTemplateField_t *field;
    fbRecordValue_t          value = FB_RECORD_VALUE_INIT;
    char         prefix[PREFIX_BUFSIZ];
    char         str_elem[ELEMENT_BUFSIZ];
    char         str_semantic[SEMANTIC_BUFSIZ];
    int          iename_width;
    unsigned int i;

    field = fbBasicListGetTemplateField(bl);

    idAddIndentLevel(prefix, parent_prefix, sizeof(prefix));

    /* add a space to the end of the prefix */
    if (strlen(prefix) + 2 < sizeof(prefix)) {
        prefix[strlen(prefix) + 1] = '\0';
        prefix[strlen(prefix)] = ' ';
    }

    idFormatListSemantic(str_semantic, fbBasicListGetSemantic(bl),
                         sizeof(str_semantic));
    iename_width = strlen(fbTemplateFieldGetName(field));
    iename_width = -1 * MAX(MIN_IENAME_WIDTH, iename_width);
    idFormatElement(str_elem, field, sizeof(str_elem), iename_width, TRUE);

    if (FB_IE_VARLEN == fbTemplateFieldGetLen(field)) {
        fprintf(fp, "count: %u %s ie: %s [v]\n",
                fbBasicListCountElements(bl), str_semantic,
                fbTemplateFieldGetName(field));
    } else {
        fprintf(fp, "count: %u %s ie: %s [%u]\n",
                fbBasicListCountElements(bl), str_semantic,
                fbTemplateFieldGetName(field),
                fbTemplateFieldGetLen(field));
    }

    for (i = 0; fbBasicListGetIndexedRecordValue(bl, i, &value); ++i) {
        fprintf(fp, "%s%s : ", prefix, str_elem);
        idPrintValue(fp, field, &value, prefix);
    }
}


#define FB_TO_ASCII(c)                                                  \
    ((g_ascii_isprint(c) && !g_ascii_iscntrl(c)) ? (char)(c) : '.')

/**
 *    Print 'vf' in the style of the hexdump -vC
 */
static void
idPrintVarfieldHexdump(
    FILE                *fp,
    const fbVarfield_t  *vf,
    const char          *str_prefix)
{
    size_t offset;

    if (0 == vf->len) {
        return;
    }
    for (offset = 0; vf->len - offset >= 16; offset += 16) {
        fprintf(fp, ("%s| %08zx"
                     "  %02x %02x %02x %02x %02x %02x %02x %02x"
                     "  %02x %02x %02x %02x %02x %02x %02x %02x"
                     "  |%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c|\n"),
                str_prefix, offset,
                vf->buf[offset + 0x0],
                vf->buf[offset + 0x1],
                vf->buf[offset + 0x2],
                vf->buf[offset + 0x3],
                vf->buf[offset + 0x4],
                vf->buf[offset + 0x5],
                vf->buf[offset + 0x6],
                vf->buf[offset + 0x7],
                vf->buf[offset + 0x8],
                vf->buf[offset + 0x9],
                vf->buf[offset + 0xa],
                vf->buf[offset + 0xb],
                vf->buf[offset + 0xc],
                vf->buf[offset + 0xd],
                vf->buf[offset + 0xe],
                vf->buf[offset + 0xf],
                FB_TO_ASCII(vf->buf[offset + 0x0]),
                FB_TO_ASCII(vf->buf[offset + 0x1]),
                FB_TO_ASCII(vf->buf[offset + 0x2]),
                FB_TO_ASCII(vf->buf[offset + 0x3]),
                FB_TO_ASCII(vf->buf[offset + 0x4]),
                FB_TO_ASCII(vf->buf[offset + 0x5]),
                FB_TO_ASCII(vf->buf[offset + 0x6]),
                FB_TO_ASCII(vf->buf[offset + 0x7]),
                FB_TO_ASCII(vf->buf[offset + 0x8]),
                FB_TO_ASCII(vf->buf[offset + 0x9]),
                FB_TO_ASCII(vf->buf[offset + 0xa]),
                FB_TO_ASCII(vf->buf[offset + 0xb]),
                FB_TO_ASCII(vf->buf[offset + 0xc]),
                FB_TO_ASCII(vf->buf[offset + 0xd]),
                FB_TO_ASCII(vf->buf[offset + 0xe]),
                FB_TO_ASCII(vf->buf[offset + 0xf]));
    }
    if (offset < vf->len) {
        size_t j;
        fprintf(fp, "%s| %08zx", str_prefix, offset);
        for (j = offset; j < vf->len; ++j) {
            /* extra space before 0th and 8th value in row */
            fprintf(fp, " %s%02x", ((j & 0x7) == 0) ? " " : "", vf->buf[j]);
        }
        fprintf(fp, "%*s|", (int)((3 * (offset + 16 - j)) +
                                 (((j - offset) <= 8) ? 1 : 0) + 2), "");
        for (j = offset; j < vf->len; ++j) {
            fprintf(fp, "%c", FB_TO_ASCII(vf->buf[j]));
        }
        fprintf(fp, "%*s|", (int)(offset + 16 - j), "");
        fprintf(fp, "\n");
    }
    fprintf(fp, "%s| %08zx\n", str_prefix, vf->len);
}


/**
 *    Print the value of 'field' on 'record' to 'fp'.
 */
static void
idPrintValue(
    FILE                     *fp,
    const fbTemplateField_t  *field,
    fbRecordValue_t          *value,
    char                     *str_prefix)
{
    switch (fbTemplateFieldGetType(field)) {
      case FB_BOOL:
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        fprintf(fp, "%" PRIu64 "\n", value->v.u64);
        break;
      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        fprintf(fp, "%" PRId64 "\n", value->v.s64);
        break;
      case FB_IP4_ADDR:
        fprintf(fp, "%u.%u.%u.%u\n",
                (value->v.ip4 >> 24), (value->v.ip4 >> 16) & 0xFF,
                (value->v.ip4 >> 8) & 0xFF, value->v.ip4 & 0xFF);
        break;
      case FB_IP6_ADDR:
        {
            char ip_buf[40];
            mdPrintIP6Address(ip_buf, value->v.ip6);
            fprintf(fp, "%s\n", ip_buf);
            break;
        }
      case FB_FLOAT_64:
      case FB_FLOAT_32:
        fprintf(fp, "%.8g\n", value->v.dbl);
        break;
      case FB_DT_SEC:
      case FB_DT_MILSEC:
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        idPrintTimestamp(fp, &value->v.dt, fbTemplateFieldGetType(field));
        break;
      case FB_BASIC_LIST:
        idPrintBL(fp, value->v.bl, str_prefix);
        fbBasicListClear((fbBasicList_t *)value->v.bl);
        break;
      case FB_SUB_TMPL_LIST:
        idPrintSTL(fp, value->v.stl, str_prefix);
        fbSubTemplateListClear((fbSubTemplateList_t *)value->v.stl);
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        idPrintSTML(fp, value->v.stml, str_prefix);
        fbSubTemplateMultiListClear((fbSubTemplateMultiList_t *)value->v.stml);
        break;
      case FB_MAC_ADDR:
        fprintf(fp, "%02x:%02x:%02x:%02x:%02x:%02x\n",
                value->v.mac[0], value->v.mac[1], value->v.mac[2],
                value->v.mac[3], value->v.mac[4], value->v.mac[5]);
        break;

      case FB_STRING:
        switch (string_format) {
          case STRING_FMT_NONE:
            fprintf(fp, "[%zu]\n", value->v.varfield.len);
            break;
          case STRING_FMT_RAW:
            fprintf(fp, "[%zu] %s\n",
                    value->v.varfield.len, value->v.varfield.buf);
            break;
          case STRING_FMT_BASE64:
            {
                char *b64buf = g_base64_encode(value->v.varfield.buf,
                                               value->v.varfield.len);
                fprintf(fp, "[%zu] %s\n", value->v.varfield.len, b64buf);
                g_free(b64buf);
            }
            break;
          case STRING_FMT_HEXDUMP:
            fprintf(fp, "[%zu]\n", value->v.varfield.len);
            idPrintVarfieldHexdump(fp, &value->v.varfield, str_prefix);
            break;
          case STRING_FMT_HEXADECIMAL:
            if (0 == value->v.varfield.len) {
                fprintf(fp, "[%zu]\n", value->v.varfield.len);
            } else {
                size_t i;
                fprintf(fp, "[%zu] 0x", value->v.varfield.len);
                for (i = 0; i < value->v.varfield.len; ++i) {
                    fprintf(fp, "%02x", value->v.varfield.buf[i]);
                }
                fprintf(fp, "\n");
            }
            break;
          case STRING_FMT_QUOTED:
            {
                size_t i;

                fprintf(fp, "[%zu] \"", value->v.varfield.len);
                for (i = 0; i < value->v.varfield.len; ++i) {
                    const char c = value->v.varfield.buf[i];
                    switch (c) {
                      case '\n':
                        fprintf(fp, "\\n");
                        break;
                      case '\r':
                        fprintf(fp, "\\r");
                        break;
                      case '\t':
                        fprintf(fp, "\\t");
                        break;
                      case '"':
                        fprintf(fp, "\\\"");
                        break;
                      case '\\':
                        fprintf(fp, "\\\\");
                        break;
                      case ' ':
                        fprintf(fp, " ");
                        break;
                      default:
                        if (isgraph(c)) {
                            fprintf(fp, "%c", c);
                        } else {
                            fprintf(fp, "\\x%02" PRIx8, (uint8_t)c);
                        }
                        break;
                    }
                }
                fprintf(fp, "\"\n");
                break;
            }
        }
        fbRecordValueClear(value);
        break;

      case FB_OCTET_ARRAY:
        if (0 == value->v.varfield.len ||
            OCTET_ARRAY_NONE == octet_array_format ||
            fbTemplateFieldCheckIdent(field, 0, 210))
        {
            fprintf(fp, "[%zu]\n", value->v.varfield.len);
        } else {
            size_t i;

            switch (octet_array_format) {
              case OCTET_ARRAY_NONE:
                /* cannot happen; was handled above */
                g_error("Impossible situation");
                break;
              case OCTET_ARRAY_SHORTHEX:
                if (fbTemplateFieldGetLen(field) > 8) {
                    /* field either not fixed width or larger than a uint64_t;
                     * print only the length */
                    fprintf(fp, "[%zu]\n", value->v.varfield.len);
                    break;
                }
              /* FALLTHROUGH */
              case OCTET_ARRAY_HEXADECIMAL:
                fprintf(fp, "[%zu] 0x", value->v.varfield.len);
                for (i = 0; i < value->v.varfield.len; ++i) {
                    fprintf(fp, "%02x", value->v.varfield.buf[i]);
                }
                fprintf(fp, "\n");
                break;
              case OCTET_ARRAY_HEXDUMP:
                fprintf(fp, "[%zu]\n", value->v.varfield.len);
                idPrintVarfieldHexdump(fp, &value->v.varfield, str_prefix);
                break;
              case OCTET_ARRAY_BASE64:
                {
                    char *b64buf = g_base64_encode(value->v.varfield.buf,
                                                   value->v.varfield.len);
                    fprintf(fp, "[%zu] %s\n", value->v.varfield.len, b64buf);
                    g_free(b64buf);
                }
                break;
              case OCTET_ARRAY_STRING:
                fprintf(fp, "[%zu] \"", value->v.varfield.len);
                for (i = 0; i < value->v.varfield.len; ++i) {
                    if (value->v.varfield.buf[i] < (uint8_t)' ' ||
                        value->v.varfield.buf[i] > (uint8_t)'~')
                    {
                        /* escape control character using unicode notation */
                        fprintf(fp, "\\u00%02X", value->v.varfield.buf[i]);
                    } else if (value->v.varfield.buf[i] == (uint8_t)'\\' ||
                               value->v.varfield.buf[i] == (uint8_t)'\"')
                    {
                        fprintf(fp, "\\%c", value->v.varfield.buf[i]);
                    } else {
                        fprintf(fp, "%c", value->v.varfield.buf[i]);
                    }
                }
                fprintf(fp, "\"\n");
                break;
            }
        }
        fbRecordValueClear(value);
        break;
    }
}


/**
 *    Print a textual representation of a record to 'fp'.
 */
void
idPrintDataRecord(
    FILE                 *fp,
    const tmplContext_t  *tc,
    const fbRecord_t     *record,
    unsigned int          rec_count,
    const char           *rec_title,
    const char           *prefix)
{
    const fbTemplateField_t *field = NULL;
    fbRecordValue_t          value = FB_RECORD_VALUE_INIT;
    unsigned int             i;
    char     str_prefix[PREFIX_BUFSIZ];
    char     str_elem[ELEMENT_BUFSIZ];
    char     str_tmpl[TMPL_NAME_BUFSIZ];
    gboolean top_level;
    int      scope_width = 0;

    top_level = ('\0' == prefix[0]);
    if (top_level) {
        strncpy(str_prefix, "  ", sizeof(str_prefix));
        if (tc->scope) {
            scope_width = 3;
        }

        if (suppress_record_number) {
            fprintf(fp, "\n--- %s Record ---", rec_title);
        } else {
            fprintf(fp, "\n--- %s Record %u ---", rec_title, rec_count);
        }
    } else {
        snprintf(str_prefix, sizeof(str_prefix), "%s ", prefix);

        fprintf(fp, "%s--- %s Record %u ---",
                prefix, rec_title, rec_count);
    }

    idFormatTemplateId(str_tmpl, tc->tid, sizeof(str_tmpl));

    fprintf(fp, " fields: %u, tmpl: %s ---\n", tc->count, str_tmpl);

    for (i = 0; i < tc->count; i++) {
        field = fbTemplateGetFieldByPosition(record->tmpl, i);
        fbRecordGetValueForField(record, field, &value);
        idFormatElement(str_elem, field, sizeof(str_elem),
                        tc->iename_width, FALSE);
        fprintf(fp, "%s%s%*s : ", str_prefix, str_elem, scope_width,
                ((top_level && (i < tc->scope)) ? "{s}" : ""));
        idPrintValue(fp, field, &value, str_prefix);
    }
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
