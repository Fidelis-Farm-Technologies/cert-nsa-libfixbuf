/*
 *  Copyright 2006-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file fbinfomodel.c
 *  IPFIX Information Model and IE storage management
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Brian Trammell
 *  ------------------------------------------------------------------------
 */

#define _FIXBUF_SOURCE_
#include <fixbuf/private.h>


#if !GLIB_CHECK_VERSION(2, 32, 0)
#define g_hash_table_contains(_table, _key) \
    g_hash_table_lookup_extended((_table), (_key), NULL, NULL)
#endif


/*
 *  This determines the behavior of fbInfoElementCheckTypesSize().  If the
 *  value is true, failing the template check causes the template to be
 *  rejected.  If the value is false, failing the check produces a non-fatal
 *  g_message() but accepts the template.
 */
#ifndef FIXBUF_REJECT_TYPE_LEN_MISMATCH
#define FIXBUF_REJECT_TYPE_LEN_MISMATCH  1
#endif


/* maximum length allowed for an element name */
#define FB_IE_NAME_BUFSIZ           1024
/* maximum length allowed for an element description */
#define FB_IE_DESCRIPTION_BUFSIZ    4096

struct fbInfoModel_st {
    GHashTable    *ie_table;
    GHashTable    *ie_byname;
    GStringChunk  *ie_names;
    GStringChunk  *ie_desc;
};


#include "infomodel.h"


const fbInfoElementSpec_t ie_type_spec[] = {
    {"privateEnterpriseNumber",         4, 0 },
    {"informationElementId",            2, 0 },
    {"informationElementDataType",      1, 0 },
    {"informationElementSemantics",     1, 0 },
    {"informationElementUnits",         2, 0 },
    {"paddingOctets",                   6, 1 },
    {"informationElementRangeBegin",    8, 0 },
    {"informationElementRangeEnd",      8, 0 },
    {"informationElementName",          FB_IE_VARLEN, 0 },
    {"informationElementDescription",   FB_IE_VARLEN, 0 },
    FB_IESPEC_NULL
};


uint32_t
fbInfoElementHash(
    const fbInfoElement_t  *ie)
{
    return ((ie->ent & 0x0000ffff) << 16) | (ie->num << 2);
}

gboolean
fbInfoElementEqual(
    const fbInfoElement_t  *a,
    const fbInfoElement_t  *b)
{
    return ((a->ent == b->ent) && (a->num == b->num));
}

void
fbInfoElementDebug(
    gboolean                tmpl,
    const fbInfoElement_t  *ie)
{
    (void)tmpl;
    if (ie->len == FB_IE_VARLEN) {
        fprintf(stderr, "VL %02x %08x:%04x (%s)\n",
                ie->flags, ie->ent, ie->num, ie->name);
    } else {
        fprintf(stderr, "%2u %02x %08x:%04x (%s)\n",
                ie->len, ie->flags, ie->ent, ie->num, ie->name);
    }
}

static void
fbInfoElementFree(
    fbInfoElement_t  *ie)
{
    g_slice_free(fbInfoElement_t, ie);
}

fbInfoModel_t *
fbInfoModelAlloc(
    void)
{
    fbInfoModel_t *model = NULL;

    /* Create an information model */
    model = g_slice_new0(fbInfoModel_t);

    /* Allocate information element tables */
    model->ie_table = g_hash_table_new_full(
        (GHashFunc)fbInfoElementHash, (GEqualFunc)fbInfoElementEqual,
        NULL, (GDestroyNotify)fbInfoElementFree);

    model->ie_byname = g_hash_table_new(g_str_hash, g_str_equal);

    /* Allocate information element name chunk */
    model->ie_names = g_string_chunk_new(512);
    model->ie_desc = g_string_chunk_new(1024);

    /* Add elements to the information model */
    infomodelAddGlobalElements(model);

    /* Return the new information model */
    return model;
}

void
fbInfoModelFree(
    fbInfoModel_t  *model)
{
    if (NULL == model) {
        return;
    }
    g_hash_table_destroy(model->ie_byname);
    g_string_chunk_free(model->ie_names);
    g_string_chunk_free(model->ie_desc);
    g_hash_table_destroy(model->ie_table);
    g_slice_free(fbInfoModel_t, model);
}


/**
 *  Fill 'revname' with the reverse of the name in 'fwdname'.  Return FALSE if
 *  the resulting name is larger than 'revname_sz'.
 */
static gboolean
fbInfoModelReversifyName(
    const char  *fwdname,
    char        *revname,
    size_t       revname_sz)
{
    ssize_t sz;

    sz = snprintf(revname, revname_sz, (FB_IE_REVERSE_STR "%s"), fwdname);

    /* uppercase first char */
    revname[FB_IE_REVERSE_STRLEN] = toupper(revname[FB_IE_REVERSE_STRLEN]);

    return ((size_t)sz < revname_sz);
}


/**
 *  Updates the two hash tables of 'model' with the data in 'model_ie'.  A
 *  helper function for fbInfoModelAddElement().
 */
static void
fbInfoModelInsertElement(
    fbInfoModel_t    *model,
    fbInfoElement_t  *model_ie)
{
    fbInfoElement_t *found;

    /* Check for an existing element with same ID.  If it is not known, add it
     * to both tables. */
    found = g_hash_table_lookup(model->ie_table, model_ie);
    if (found == NULL) {
        g_hash_table_insert(model->ie_table, model_ie, model_ie);
        g_hash_table_insert(model->ie_byname, (char *)model_ie->name, model_ie);
        return;
    }

    /* If 'found' has a different name than 'model_ie', remove found from the
     * model->ie_byname table if it is present.  We can compare name pointers
     * since we use g_string_chunk_insert_const(). */
    if (found->name != model_ie->name) {
        if (g_hash_table_lookup(model->ie_byname, found->name) == found) {
            g_hash_table_remove(model->ie_byname, found->name);
        }
    }

    /* Update the existing element in place */
    memcpy(found, model_ie, sizeof(*found));

    /* (Re)add found to the ie_byname table */
    g_hash_table_insert(model->ie_byname, (char *)found->name, found);

    /* Free unused model_ie */
    g_slice_free(fbInfoElement_t, model_ie);
}

void
fbInfoModelAddElement(
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ie)
{
    fbInfoElement_t *model_ie = NULL;
    char             revname[FB_IE_NAME_BUFSIZ];

    g_assert(ie);

    /* Allocate a new information element */
    model_ie = g_slice_new0(fbInfoElement_t);

    /* Copy external IE to model IE */
    model_ie->name = g_string_chunk_insert_const(model->ie_names, ie->name);
    model_ie->ent = ie->ent;
    model_ie->num = ie->num;
    model_ie->len = ie->len;
    model_ie->flags = ie->flags;
    model_ie->min = ie->min;
    model_ie->max = ie->max;
    model_ie->type = ie->type;
    if (ie->description) {
        model_ie->description = (g_string_chunk_insert_const(
                                     model->ie_desc, ie->description));
    }

    fbInfoModelInsertElement(model, model_ie);

    /* Short circuit if not reversible */
    if (!(ie->flags & FB_IE_F_REVERSIBLE)) {
        return;
    }

    /* Generate reverse name */
    if (!fbInfoModelReversifyName(ie->name, revname, sizeof(revname))) {
        return;
    }

    /* Allocate a new reverse information element */
    model_ie = g_slice_new0(fbInfoElement_t);

    /* Copy external IE to reverse model IE */
    model_ie->name = g_string_chunk_insert_const(model->ie_names, revname);
    model_ie->ent = ie->ent ? ie->ent : FB_IE_PEN_REVERSE;
    model_ie->num = ie->ent ? ie->num | FB_IE_VENDOR_BIT_REVERSE : ie->num;
    model_ie->len = ie->len;
    model_ie->flags = ie->flags;
    model_ie->min = ie->min;
    model_ie->max = ie->max;
    model_ie->type = ie->type;

    fbInfoModelInsertElement(model, model_ie);
}

void
fbInfoModelAddElementArray(
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ie)
{
    g_assert(ie);
    for (; ie->name; ie++) {fbInfoModelAddElement(model, ie);}
}

const fbInfoElement_t *
fbInfoModelGetElement(
    const fbInfoModel_t    *model,
    const fbInfoElement_t  *ex_ie)
{
    return g_hash_table_lookup(model->ie_table, ex_ie);
}

gboolean
fbInfoModelContainsElement(
    const fbInfoModel_t    *model,
    const fbInfoElement_t  *ex_ie)
{
    return (g_hash_table_contains(model->ie_table, ex_ie) ||
            g_hash_table_contains(model->ie_byname, ex_ie->name));
}

/*
 *  Checks if the specified size 'len' of the element 'model_ie' is valid
 *  given the element's type.  Returns TRUE if valid.  Sets 'err' and returns
 *  FALSE if not.  The list of things that are check are:
 *
 *  -- Whether a fixed size element (e.g., IP address, datetime) uses an
 *     different size.
 *  -- Whether a float64 uses a size other than 4 or 8.
 *  -- Whether an integer uses a size larger than the natural size of the
 *     integer (e.g., attempting to use 4 bytes for a unsigned16).
 *  -- Whether VARLEN is used for anything other than strings, octetArrays,
 *     and structed data (lists).
 *  -- Whether a size of 0 is used for anything other than string or
 *     octetArray.
 */
static gboolean
fbInfoElementCheckTypesSize(
    const fbInfoElement_t  *model_ie,
    const uint16_t          len,
    GError                **err)
{
    GError *child_err = NULL;

    switch (fbInfoElementGetType(model_ie)) {
      case FB_BOOL:
      case FB_DT_MICROSEC:
      case FB_DT_MILSEC:
      case FB_DT_NANOSEC:
      case FB_DT_SEC:
      case FB_FLOAT_32:
      case FB_INT_8:
      case FB_IP4_ADDR:
      case FB_IP6_ADDR:
      case FB_MAC_ADDR:
      case FB_UINT_8:
        /* fixed size */
        if (len == model_ie->len) {
            return TRUE;
        }
        break;
      case FB_FLOAT_64:
        /* may be either 4 or 8 octets */
        if (len == 4 || len == 8) {
            return TRUE;
        }
        break;
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        /* these support reduced length encoding */
        if (len <= model_ie->len && len > 0) {
            return TRUE;
        }
        break;
      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
      case FB_SUB_TMPL_MULTI_LIST:
        if (len > 0) {
            return TRUE;
        }
        break;
      case FB_OCTET_ARRAY:
      case FB_STRING:
      default:
        /* may be any size */
        return TRUE;
    }

    if (len == FB_IE_VARLEN) {
        g_set_error(&child_err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Template warning: Information element %s"
                    " may not be variable length",
                    model_ie->name);
    } else {
        g_set_error(&child_err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Template warning: Illegal length %d"
                    " for information element %s",
                    len, model_ie->name);
    }
#if FIXBUF_REJECT_TYPE_LEN_MISMATCH
    g_propagate_error(err, child_err);
    return FALSE;
#else
    (void)err;  /* unused */
    g_message("%s", child_err->message);
    g_clear_error(&child_err);
    return TRUE;
#endif /* if FIXBUF_REJECT_TYPE_LEN_MISMATCH */
}


/**
 *  Initializes `tmpl_ie` to use `model_ie`, setting its length to
 *  `len_override` if non-zero; otherwise to the standard element's length.
 *
 *  Returns FALSE and leaves `tmpl_ie` unchanged when `len_override` is
 *  non-zero and an invalid length for `model_ie`.
 */
static gboolean
fbInfoElementCopyToTemplateHelper(
    const fbInfoElement_t  *model_ie,
    uint16_t                len_override,
    fbTemplateField_t      *tmpl_ie,
    GError                **err)
{
    if (len_override &&
        !fbInfoElementCheckTypesSize(model_ie, len_override, err))
    {
        return FALSE;
    }

    /* Refer to canonical IE in the model */
    tmpl_ie->canon = model_ie;

    /* midx gets updated by the caller */
    tmpl_ie->midx = 0;
    tmpl_ie->len = len_override ? len_override : model_ie->len;

    /* All done */
    return TRUE;
}


/*  Declared in private.h; helper for fbTemplateAppend() */
gboolean
fbInfoElementCopyToTemplate(
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ex_ie,
    fbTemplateField_t      *tmpl_ie,
    GError                **err)
{
    const fbInfoElement_t *model_ie;

    /* Look up information element in the model */
    model_ie = fbInfoModelGetElement(model, ex_ie);
    if (!model_ie) {
        /* Information element not in model. Note it's alien and add it. */
        model_ie = fbInfoModelAddAlienElement(model, ex_ie);
    } else if (!fbInfoElementCheckTypesSize(model_ie, ex_ie->len, err)) {
        return FALSE;
    }

    /* Refer to canonical IE in the model */
    tmpl_ie->canon = model_ie;

    /* midx gets updated by the caller */
    tmpl_ie->midx = 0;

    /* Always use the incoming length. Any illegal lengths should have caused
     * CheckTypesSize() to return FALSE; therefore, the only time the
     * following should affect things is for string and octetArray fields that
     * use a length of zero. */
    tmpl_ie->len = ex_ie->len;

    return TRUE;
}

/*  Declared in private.h; helper for fbTemplateAppendSpec() */
gboolean
fbInfoElementCopyToTemplateByName(
    const fbInfoModel_t       *model,
    const fbInfoElementSpec_t *spec,
    fbTemplateField_t         *tmpl_ie,
    GError                   **err)
{
    const fbInfoElement_t *model_ie;

    /* Look up information element in the model */
    model_ie = fbInfoModelGetElementByName(model, spec->name);
    if (!model_ie) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_NOELEMENT,
                    "No such information element %s", spec->name);
        return FALSE;
    }

    return fbInfoElementCopyToTemplateHelper(
        model_ie, spec->len_override, tmpl_ie, err);
}

/*  Declared in private.h; helper for fbTemplateAppendSpecId() */
gboolean
fbInfoElementCopyToTemplateByIdent(
    const fbInfoModel_t          *model,
    const fbInfoElementSpecId_t  *spec,
    fbTemplateField_t            *tmpl_ie,
    GError                      **err)
{
    const fbInfoElement_t *model_ie;
    fbInfoElement_t        element;

    element.ent = spec->ident.enterprise_id;
    element.num = spec->ident.element_id;

    /* Look up information element in the model */
    model_ie = fbInfoModelGetElement(model, &element);
    if (!model_ie) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_NOELEMENT,
                    "No such information element with PEN = %u, ID = %d",
                    spec->ident.enterprise_id, spec->ident.element_id);
        return FALSE;
    }

    return fbInfoElementCopyToTemplateHelper(
        model_ie, spec->len_override, tmpl_ie, err);
}

const fbInfoElement_t *
fbInfoModelGetElementByName(
    const fbInfoModel_t  *model,
    const char           *name)
{
    g_assert(name);
    return g_hash_table_lookup(model->ie_byname, name);
}

const fbInfoElement_t *
fbInfoModelGetElementByID(
    const fbInfoModel_t  *model,
    uint16_t              id,
    uint32_t              ent)
{
    fbInfoElement_t tempElement;

    tempElement.ent = ent;
    tempElement.num = id;

    return fbInfoModelGetElement(model, &tempElement);
}

fbTemplate_t *
fbInfoElementAllocTypeTemplate(
    fbInfoModel_t  *model,
    GError        **err)
{
    return fbInfoElementAllocTypeTemplate2(model, TRUE, err);
}

fbTemplate_t *
fbInfoElementAllocTypeTemplate2(
    fbInfoModel_t  *model,
    gboolean        internal,
    GError        **err)
{
    fbTemplate_t *tmpl;
    uint32_t      flags;

    flags = internal ? ~0 : 0;

    tmpl = fbTemplateAlloc(model);
    if (!fbTemplateAppendSpecArray(tmpl, ie_type_spec, flags, err)) {
        fbTemplateFreeUnused(tmpl);
        return NULL;
    }
    fbTemplateSetOptionsScope(tmpl, 2);
    return tmpl;
}

gboolean
fbInfoElementWriteOptionsRecord(
    fBuf_t                 *fbuf,
    const fbInfoElement_t  *model_ie,
    uint16_t                itid,
    uint16_t                etid,
    GError                **err)
{
    fbInfoElementOptRec_t rec;

    g_assert(model_ie);
    if (model_ie == NULL) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_NOELEMENT,
                    "Invalid [NULL] Information Element");
        return FALSE;
    }

    rec.ie_range_begin = model_ie->min;
    rec.ie_range_end = model_ie->max;
    rec.ie_pen = model_ie->ent;
    rec.ie_units = FB_IE_UNITS(model_ie->flags);
    rec.ie_semantic = FB_IE_SEMANTIC(model_ie->flags);
    rec.ie_id = model_ie->num;
    rec.ie_type = model_ie->type;
    memset(rec.padding, 0, sizeof(rec.padding));
    rec.ie_name.buf = (uint8_t *)model_ie->name;
    rec.ie_name.len = strlen(model_ie->name);
    rec.ie_desc.buf = (uint8_t *)model_ie->description;
    if (model_ie->description) {
        rec.ie_desc.len = strlen(model_ie->description);
    } else {
        rec.ie_desc.len = 0;
    }

    if (!fBufSetExportTemplate(fbuf, etid, err)) {
        return FALSE;
    }

    if (!fBufSetInternalTemplate(fbuf, itid, err)) {
        return FALSE;
    }

    if (!fBufAppend(fbuf, (uint8_t *)&rec, sizeof(rec), err)) {
        return FALSE;
    }

    return TRUE;
}

gboolean
fbInfoElementAddOptRecElement(
    fbInfoModel_t                *model,
    const fbInfoElementOptRec_t  *rec)
{
    fbInfoElement_t ie;
    char            name[FB_IE_NAME_BUFSIZ];
    char            description[FB_IE_DESCRIPTION_BUFSIZ];
    size_t          len;

    /* reject a standard element or a reverse standard element */
    if (0 == rec->ie_pen || FB_IE_PEN_REVERSE == rec->ie_pen) {
        return FALSE;
    }
    /* reject an element whose name is unusually long */
    if (rec->ie_name.len >= sizeof(name)) {
        return FALSE;
    }

    /* reject an element whose PEN/ID is already in the model.  Section 4 of
     * RFC5610 */
    ie.ent = rec->ie_pen;
    ie.num = rec->ie_id;
    if (fbInfoModelGetElement(model, &ie)) {
        return FALSE;
    }

    /* reject an element whose name is already in the model */
    strncpy(name, (char *)rec->ie_name.buf, rec->ie_name.len);
    name[rec->ie_name.len] = '\0';
    if (fbInfoModelGetElementByName(model, name)) {
        return FALSE;
    }

    /* element is okay; copy the rest of the fields and add it */
    ie.min = rec->ie_range_begin;
    ie.max = rec->ie_range_end;
    ie.type = rec->ie_type;
    ie.name = name;
    len = ((rec->ie_desc.len < sizeof(description))
           ? rec->ie_desc.len : (sizeof(description) - 1));
    strncpy(description, (char *)rec->ie_desc.buf, len);
    description[len] = '\0';
    ie.description = description;
    ie.flags = 0;
    ie.flags |= rec->ie_units << 16;
    ie.flags |= rec->ie_semantic << 8;

    /* length is inferred from data type */
    switch (ie.type) {
      case FB_OCTET_ARRAY:
      case FB_STRING:
      case FB_BASIC_LIST:
      case FB_SUB_TMPL_LIST:
      case FB_SUB_TMPL_MULTI_LIST:
        ie.len = FB_IE_VARLEN;
        break;
      case FB_UINT_8:
      case FB_INT_8:
      case FB_BOOL:
        ie.len = 1;
        break;
      case FB_UINT_16:
      case FB_INT_16:
        ie.len = 2;
        ie.flags |= FB_IE_F_ENDIAN;
        break;
      case FB_UINT_32:
      case FB_INT_32:
      case FB_DT_SEC:
      case FB_FLOAT_32:
      case FB_IP4_ADDR:
        ie.len = 4;
        ie.flags |= FB_IE_F_ENDIAN;
        break;
      case FB_MAC_ADDR:
        ie.len = 6;
        break;
      case FB_UINT_64:
      case FB_INT_64:
      case FB_DT_MILSEC:
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
      case FB_FLOAT_64:
        ie.len = 8;
        ie.flags |= FB_IE_F_ENDIAN;
        break;
      case FB_IP6_ADDR:
        ie.len = 16;
        break;
      default:
        g_warning("Adding element %s with invalid data type [%d]", name,
                  rec->ie_type);
        ie.len = FB_IE_VARLEN;
    }

    fbInfoModelAddElement(model, &ie);
    return TRUE;
}


gboolean
fbInfoModelIsTemplateElementType(
    const fbTemplate_t  *tmpl)
{
    /* ignore padding. */
    return fbTemplateContainsAllFlaggedElementsByName(tmpl, ie_type_spec, 0);
}

guint
fbInfoModelCountElements(
    const fbInfoModel_t  *model)
{
    return g_hash_table_size(model->ie_table);
}

void
fbInfoModelIterInit(
    fbInfoModelIter_t    *iter,
    const fbInfoModel_t  *model)
{
    g_assert(iter);
    g_hash_table_iter_init(iter, model->ie_table);
}

const fbInfoElement_t *
fbInfoModelIterNext(
    fbInfoModelIter_t  *iter)
{
    const fbInfoElement_t *ie;
    g_assert(iter);
    if (g_hash_table_iter_next(iter, NULL, (gpointer *)&ie)) {
        return ie;
    }
    return NULL;
}

const fbInfoElement_t *
fbInfoModelAddAlienElement(
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ex_ie)
{
    const fbInfoElement_t *model_ie = NULL;
    fbInfoElement_t        alien_ie = FB_IE_NULL;

    if (ex_ie == NULL) {
        return NULL;
    }
    /* Information element not in model. Note it's alien and add it. */
    alien_ie.ent = ex_ie->ent;
    alien_ie.num = ex_ie->num;
    alien_ie.len = ex_ie->len;
    alien_ie.name = (g_string_chunk_insert_const(
                         model->ie_names, "_alienInformationElement"));
    alien_ie.flags |= FB_IE_F_ALIEN;
    fbInfoModelAddElement(model, &alien_ie);
    model_ie = fbInfoModelGetElement(model, &alien_ie);
    g_assert(model_ie);

    return model_ie;
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
