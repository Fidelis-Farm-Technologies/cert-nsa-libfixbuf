/*
 *  Copyright 2019-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file ipfix2jsonPrint.c
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Emily Sarneso, Matt Coates
 *  ------------------------------------------------------------------------
 */

#include "ipfix2json.h"

static void
idPrintValue(
    FILE                     *fp,
    const fbTemplateField_t  *field,
    fbRecordValue_t          *value,
    GHashTable              **uniq_names);


/**
 *  Prints the name of `tmpl_field_`, a fbTemplateField_t, to `fp`, a FILE
 *  pointer, adding a repeat-count suffix if needed.  The name is surrounded
 *  by double-quotes and is followed by a space and colon.
 */
#define PRINT_FIELD_NAME(fp_, tmpl_field_)                             \
    if (allow_duplicate_keys                                           \
        || 0 == fbTemplateFieldGetRepeat(tmpl_field_))                 \
    {                                                                  \
        fprintf(fp_, "\"%s\": ", fbTemplateFieldGetName(tmpl_field_)); \
    } else {                                                           \
        fprintf(fp_, "\"%s-%u\": ",                                    \
                fbTemplateFieldGetName(tmpl_field_),                   \
                1 + fbTemplateFieldGetRepeat(tmpl_field_));            \
    }


/**
 *  Looks for 'name' in referent of 'table'.
 *
 *  If found, increments the counter in the table.  If 'namelen' is non-zero,
 *  appends the counter to 'name'.  Returns the updated counter.
 *
 *  If not found, initializes it, adds it to the table, does not modify
 *  'name', and returns 0.
 *
 *  Creates the table if it does not exist.
 */
static unsigned int
idUpdateUniqNameCount(
    GHashTable **table,
    char        *name,
    size_t       namelen)
{
    unsigned int *current;
    gchar        *uniqname;
    size_t        sz;

    if (allow_duplicate_keys) {
        return 0;
    }
    uniqname = g_string_chunk_insert_const(object_keys, name);

    if (*table) {
        /* if current exists in the table, update it in place */
        current = (unsigned int *)g_hash_table_lookup(*table, uniqname);
        if (current) {
            ++*current;
            if (namelen) {
                sz = strlen(name);
                snprintf(name + sz, namelen - sz, "-%u", *current);
            }
            return *current;
        }
    } else {
        *table = g_hash_table_new_full(g_direct_hash, NULL, NULL, &g_free);
    }

    /* add it to the table */
    current = g_new(unsigned int, 1);
    *current = 1;
    g_hash_table_insert(*table, uniqname, current);
    return 0;
}


/**
 * mdPrintIP6Address
 *
 *
 */
static void
mdPrintIP6Address(
    char           *ipaddr_buf,
    const uint8_t  *ipaddr)
{
    char          *cp = ipaddr_buf;
    const uint8_t *aqp;
    uint16_t       aq;
    gboolean       colon_start = FALSE;
    gboolean       colon_end = FALSE;

    for (aqp = ipaddr; aqp < ipaddr + 16; aqp += 2) {
        aq = g_ntohs(*((const uint16_t *)aqp));
        if (aq || colon_end) {
            if (aqp < ipaddr + 14) {
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
            if (aqp == ipaddr) {
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
 *    Puts textual information about the template whose ID is 'tid'
 *    into 'tmpl_str'.  The information is the template ID (both
 *    decimal and hex) and the template name if available.
 *
 *    @param tmpl_str        The buffer to write to
 *    @param tid             The ID of the template to get info of
 *    @param tmpl_str_bufsiz The sizeof the tmpl_str buffer
 *
 */
void
idFormatTemplateId(
    char    *tmpl_str,
    int      tid,
    size_t   tmpl_str_bufsiz)
{
    const char *name;

    name = (char *)g_hash_table_lookup(template_names, GINT_TO_POINTER(tid));
    snprintf(tmpl_str, tmpl_str_bufsiz, "template:%#06x(%s)",
             tid, ((name) ? name : ""));
}


/**
 *    Print a textual representation of 'tmpl' to 'fp'.  'ctx' is the
 *    template context created when the template was first read.
 */
void
idPrintTemplate(
    FILE           *fp,
    fbTemplate_t   *tmpl,
    tmplContext_t  *ctx)
{
    const fbTemplateField_t *field = NULL;
    unsigned int             i;
    const char              *name;

    name = (char *)g_hash_table_lookup(template_names,
                                       GINT_TO_POINTER(ctx->tid));
    fprintf(fp, "{\"template_record:%#06x(%s)\":[",
            ctx->tid, ((name) ? name : ""));

    for (i = 0; i < ctx->count; ++i) {
        field = fbTemplateGetFieldByPosition(tmpl, i);
        fprintf(fp, "%s\"%s\"", ((i > 0) ? "," : ""),
                fbTemplateFieldGetName(field));
    }
    fprintf(fp, "]}\n");
}


/**
 *    Print a textual representation of 'entry' to 'fp'.
 */
static void
idPrintSTMLEntry(
    FILE                           *fp,
    fbSubTemplateMultiListEntry_t  *entry,
    GHashTable                    **uniq_names)
{
    gboolean   first = TRUE;
    char       str_template[TMPL_NAME_BUFSIZ];
    fbRecord_t subrec = FB_RECORD_INIT;

    subrec.tid = fbSubTemplateMultiListEntryGetTemplateID(entry);
    subrec.tmpl = fbSubTemplateMultiListEntryGetTemplate(entry);
    idFormatTemplateId(str_template, subrec.tid, sizeof(str_template));
    idUpdateUniqNameCount(uniq_names, str_template, sizeof(str_template));
    fprintf(fp, "\"%s\": [{", str_template);

    while ((subrec.rec = fbSTMLEntryNext(uint8_t, entry, subrec.rec))) {
        if (!first) {
            fprintf(fp, "},{");
        } else {
            first = FALSE;
        }
        idPrintDataRecord(fp, &subrec);
    }
    fprintf(fp, "}]");
}


/**
 *    Print a textual representation of 'stl' to 'fp'.
 */
static void
idPrintSTL(
    FILE                       *fp,
    const fbTemplateField_t    *field,
    const fbSubTemplateList_t  *stl,
    GHashTable                **uniq_names)
{
    char       str_template[TMPL_NAME_BUFSIZ];
    gboolean   first = TRUE;
    fbRecord_t subrec = FB_RECORD_INIT;

    subrec.tid = fbSubTemplateListGetTemplateID(stl);
    subrec.tmpl = fbSubTemplateListGetTemplate(stl);
    idFormatTemplateId(str_template, subrec.tid, sizeof(str_template));

    if (full_structure) {
        PRINT_FIELD_NAME(fp, field);
        fprintf(fp, "{");
    } else {
        idUpdateUniqNameCount(uniq_names, str_template, sizeof(str_template));
    }
    fprintf(fp, "\"%s\": [{", str_template);

    while ((subrec.rec = fbSTLNext(uint8_t, stl, subrec.rec))) {
        if (!first) {
            fprintf(fp, "},{");
        } else {
            first = FALSE;
        }
        idPrintDataRecord(fp, &subrec);
    }

    fprintf(fp, "}]%s", (full_structure ? "}" : ""));
}


/**
 *    Print a textual representation of 'stml' to 'fp'.
 */
static void
idPrintSTML(
    FILE                            *fp,
    const fbTemplateField_t         *field,
    const fbSubTemplateMultiList_t  *stml,
    GHashTable                     **uniq_names)
{
    fbSubTemplateMultiListEntry_t *entry = NULL;
    GHashTable *uniq_children = NULL;
    gboolean    first = TRUE;

    /* protect against a double or trailing comma in the parent
     * when this STML is empty */
    if (0 == fbSubTemplateMultiListCountElements(stml)) {
        PRINT_FIELD_NAME(fp, field);
        fprintf(fp, "{}");
        return;
    }

    if (full_structure) {
        PRINT_FIELD_NAME(fp, field);
        fprintf(fp, "{");

        /* use a local hash table in the unusual case were multiple STML
         * Entries use the same template. */
        uniq_names = &uniq_children;
    }
    while ((entry = fbSTMLNext(stml, entry))) {
        if (!first) {
            fprintf(fp, ",");
        } else {
            first = FALSE;
        }
        idPrintSTMLEntry(fp, entry, uniq_names);
    }
    if (full_structure) {
        fprintf(fp, "}");
        if (uniq_children) {
            g_hash_table_destroy(uniq_children);
        }
    }
}


/**
 *    Print a textual representation of 'bl' to 'fp'.
 */
static void
idPrintBL(
    FILE                     *fp,
    const fbTemplateField_t  *parent_field,
    const fbBasicList_t      *bl,
    GHashTable              **uniq_names)
{
    const fbTemplateField_t *field;
    fbRecordValue_t          value = FB_RECORD_VALUE_INIT;
    GHashTable              *uniq_children = NULL;
    unsigned int             i;

    field = fbBasicListGetTemplateField(bl);

    if (full_structure) {
        PRINT_FIELD_NAME(fp, parent_field);
        fprintf(fp, "{\"%s\": [", fbTemplateFieldGetName(field));
    } else {
        unsigned int count = idUpdateUniqNameCount(
            uniq_names, (char *)fbTemplateFieldGetName(field), 0);
        if (0 == count) {
            fprintf(fp, "\"%s\": [", fbTemplateFieldGetName(field));
        } else {
            fprintf(fp, "\"%s-%u\": [", fbTemplateFieldGetName(field), count);
        }
    }

    for (i = 0; fbBasicListGetIndexedRecordValue(bl, i, &value); ++i) {
        if (i > 0) {
            fprintf(fp, ",");
        }
        idPrintValue(fp, field, &value, &uniq_children);
    }
    if (uniq_children) {
        g_hash_table_destroy(uniq_children);
    }

    fprintf(fp, "]%s", (full_structure ? "}" : ""));
}


/**
 *    Print the value of element 'field' to 'fp'.  The value is given in
 *    'val'.
 */
static void
idPrintValue(
    FILE                     *fp,
    const fbTemplateField_t  *field,
    fbRecordValue_t          *value,
    GHashTable              **uniq_names)
{
    struct tm time_tm;
    char      timebuf[32];

    switch (fbTemplateFieldGetType(field)) {
      case FB_BOOL:
      case FB_UINT_8:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        fprintf(fp, "%" PRIu64, value->v.u64);
        break;

      case FB_INT_8:
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        fprintf(fp, "%" PRId64, value->v.s64);
        break;

      case FB_IP4_ADDR:
        fprintf(fp, "\"%u.%u.%u.%u\"",
                (value->v.ip4 >> 24), (value->v.ip4 >> 16) & 0xFF,
                (value->v.ip4 >> 8) & 0xFF, value->v.ip4 & 0xFF);
        break;
      case FB_IP6_ADDR:
        {
            char ip_buf[40];
            mdPrintIP6Address(ip_buf, value->v.ip6);
            fprintf(fp, "\"%s\"", ip_buf);
            break;
        }

      case FB_FLOAT_64:
      case FB_FLOAT_32:
        fprintf(fp, "%.8g", value->v.dbl);
        break;

      case FB_DT_SEC:
        /* "%Y-%m-%d %H:%M:%S" */
        strftime(timebuf, sizeof(timebuf), "%F %T",
                 gmtime_r(&value->v.dt.tv_sec, &time_tm));
        fprintf(fp, "\"%sZ\"", timebuf);
        break;
      case FB_DT_MILSEC:
        strftime(timebuf, sizeof(timebuf), "%F %T",
                 gmtime_r(&value->v.dt.tv_sec, &time_tm));
        fprintf(fp, "\"%s.%03ldZ\"", timebuf, value->v.dt.tv_nsec / 1000000);
        break;
      case FB_DT_MICROSEC:
        strftime(timebuf, sizeof(timebuf), "%F %T",
                 gmtime_r(&value->v.dt.tv_sec, &time_tm));
        fprintf(fp, "\"%s.%06ldZ\"", timebuf, value->v.dt.tv_nsec / 1000);
        break;
      case FB_DT_NANOSEC:
        strftime(timebuf, sizeof(timebuf), "%F %T",
                 gmtime_r(&value->v.dt.tv_sec, &time_tm));
        fprintf(fp, "\"%s.%09ldZ\"", timebuf, value->v.dt.tv_nsec);
        break;

      case FB_BASIC_LIST:
        idPrintBL(fp, field, value->v.bl, uniq_names);
        fbBasicListClear((fbBasicList_t *)value->v.bl);
        break;
      case FB_SUB_TMPL_LIST:
        idPrintSTL(fp, field, value->v.stl, uniq_names);
        fbSubTemplateListClear((fbSubTemplateList_t *)value->v.stl);
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        idPrintSTML(fp, field, value->v.stml, uniq_names);
        fbSubTemplateMultiListClear((fbSubTemplateMultiList_t *)value->v.stml);
        break;

      case FB_MAC_ADDR:
        fprintf(fp, "\"%02x:%02x:%02x:%02x:%02x:%02x\"",
                value->v.mac[0], value->v.mac[1], value->v.mac[2],
                value->v.mac[3], value->v.mac[4], value->v.mac[5]);
        break;

      case FB_STRING:
        {
            const uint8_t *c = value->v.varfield.buf;
            size_t         i;

            fprintf(fp, "\"");
            for (i = 0; i < value->v.varfield.len; ++i, ++c) {
                if (*c < (uint8_t)' ' || *c > (uint8_t)'~') {
                    /* escape control character using unicode notation */
                    fprintf(fp, "\\u00%02X", *c);
                } else if (*c == (uint8_t)'\\' || *c == (uint8_t)'\"') {
                    fprintf(fp, "\\%c", *c);
                } else {
                    fprintf(fp, "%c", *c);
                }
            }
            fprintf(fp, "\"");
            fbRecordValueClear(value);
        }
        break;

      case FB_OCTET_ARRAY:
        switch (octet_array_format) {
          case OCTET_ARRAY_EMPTY:
            fprintf(fp, "\"\"");
            break;
          case OCTET_ARRAY_BASE64:
            {
                char *b64buf = g_base64_encode(value->v.varfield.buf,
                                               value->v.varfield.len);
                fprintf(fp, "\"%s\"", b64buf);
                g_free(b64buf);
            }
            break;
          case OCTET_ARRAY_STRING:
            {
                const uint8_t *c = value->v.varfield.buf;
                size_t         i;
                fprintf(fp, "\"");
                for (i = 0; i < value->v.varfield.len; ++i, ++c) {
                    if (*c < (uint8_t)' ' || *c > (uint8_t)'~') {
                        /* escape control character using unicode notation */
                        fprintf(fp, "\\u00%02X", *c);
                    } else if (*c == (uint8_t)'\\' || *c == (uint8_t)'\"') {
                        fprintf(fp, "\\%c", *c);
                    } else {
                        fprintf(fp, "%c", *c);
                    }
                }
                fprintf(fp, "\"");
            }
            break;
          case OCTET_ARRAY_HEXADECIMAL:
            {
                size_t i;
                fprintf(fp, "\"");
                for (i = 0; i < value->v.varfield.len; ++i) {
                    fprintf(fp, "%02x", value->v.varfield.buf[i]);
                }
                fprintf(fp, "\"");
            }
            break;
        }
        fbRecordValueClear(value);
        break;
    }
}


/**
 *    Print a textual representation of a record to 'fp'.  The
 *    record's template is 'tmpl', its data is given by 'buffer'.
 */
void
idPrintDataRecord(
    FILE        *fp,
    fbRecord_t  *record)
{
    const fbTemplateField_t *field = NULL;
    fbRecordValue_t          value = FB_RECORD_VALUE_INIT;
    fbTemplateIter_t         iter;
    gboolean first = TRUE;
    GHashTable              *uniq_names = NULL;

    fbTemplateIterInit(&iter, record->tmpl);
    while ((field = fbTemplateIterNext(&iter))) {
        if (fbTemplateFieldCheckIdent(field, 0, 210)) {
            /* paddingOctets */
            continue;
        }
        if (!first) {
            fprintf(fp, ",");
        } else {
            first = FALSE;
        }
        if (!fbInfoElementIsList(fbTemplateFieldGetIE(field))) {
            PRINT_FIELD_NAME(fp, field);
        }
        fbRecordGetValueForField(record, field, &value);
        idPrintValue(fp, field, &value, &uniq_names);
        /* fflush(fp); */
    }

    if (uniq_names) {
        g_hash_table_destroy(uniq_names);
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
