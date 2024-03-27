/*
 *  Copyright 2006-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file fbtemplate.c
 *  IPFIX Template implementation
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
#if !GLIB_CHECK_VERSION(2, 68, 0)
/* g_memdup2() uses gsize in place of guint */
#define g_memdup2(_mem, _size)   g_memdup((_mem), (_size))
#endif


/**
 *  The maximum number of elements in a template is the max message length
 *  minus the lengths of the message header, the set header, and the template
 *  header, divided by 4 for each element ID and length.
 *
 *  (For most transports, the MTU is less than the full message size.)
 */
#define FB_TMPL_MAX_ELEMENTS   ((65535 - 16 - 4 - 4) >> 2)

/**
 *  Add '_addend_' to '_current_' checking whether the result overflows a
 *  uint16_t.  If it would overflow, return FALSE.  If okay, do the addition
 *  and return TRUE.
 */
#define FB_SAFE_ADD_U16_NOT_BUILTIN(_current_, _addend_)     \
    (((_addend_) > (unsigned int)(UINT16_MAX - (_current_))) \
     ? FALSE                                                 \
     : (((_current_) += (_addend_)), TRUE))

/**
 *  Add '_addend_' to '_current_' checking whether the result overflows a
 *  uint16_t.  If it would overflow, return FALSE.  If okay, do the addition
 *  and return TRUE.  '_tmp_result_' temporarily holds the result.
 */
#define FB_SAFE_ADD_U16_BUILTIN(_current_, _addend_, _tmp_result_)    \
    ((__builtin_add_overflow((_current_), (_addend_), &_tmp_result_)) \
     ? FALSE                                                          \
     : ((_current_ = _tmp_result_), TRUE))


/* struct used by applications receiving and sending metadata records */
struct fbTemplateInfo_st {
    /* the human meaningful name of this template */
    const char         *name;
    /* description of the template, for display purposes only */
    const char         *description;
    /* information about basic list elements, an array */
    fbBasicListInfo_t  *blInfo;
    /* number of elements in the blInfo array */
    uint16_t            blCount;
    /* the template id being described */
    uint16_t            tid;
    /* location in the template hierarchy of this template. 0 means top */
    uint16_t            parentTid;
    /* application label that uses this template; make this an array */
    uint16_t            appLabel;
};


/*
 * fbBasicListInfo_t is the type of the array in the fbTemplateInfo_t that
 * holds information about FB_BASIC_LIST-type objects in a template, namely
 * the IE of the list itself and the IE of its content.
 *
 * typedef struct fbBasicListInfo_st fbBasicListInfo_t;   // public.h
 */
struct fbBasicListInfo_st {
    uint32_t   blEnt;
    uint16_t   blNum;
    uint16_t   contentNum;
    uint32_t   contentEnt;
};


/**
 *  Template metadata template
 *
 *  When using fbTemplateAppendSpecArray(), specify flags of 1 for an internal
 *  v1 metadata record and 6 for an internal v3 metadata record.
 *
 *  For fbTemplateContainsAllFlaggedElementsByName(), specify flags of 0 for
 *  v1 metadata and 4 for internal v3 metadata.
 *
 *  We use fbSessionAddTemplatesForExport() to remove the padding if needed.
 */
static fbInfoElementSpec_t fb_template_info_spec[] = {
    {"templateId",              2,            0 },
    {"silkAppLabel",            2,            4 },
    {"templateId",              2,            4 },
    {"paddingOctets",           2,            2 },
    {"paddingOctets",           6,            1 },
    {"templateName",            FB_IE_VARLEN, 0 },
    {"templateDescription",     FB_IE_VARLEN, 0 },
    {"subTemplateList",         FB_IE_VARLEN, 4 },
    /*{"contentsOfBasicLists",    FB_IE_VARLEN, 4 },*/
    FB_IESPEC_NULL
};

static fbInfoElementSpec_t fb_basiclist_info_spec[] = {
    {"privateEnterpriseNumber", 4, 0 },
    {"informationElementId",    2, 0 },
    {"informationElementId",    2, 0 },
    {"privateEnterpriseNumber", 4, 0 },
    FB_IESPEC_NULL
};


/**
 * fbTemplateFree
 *
 * @param tmpl
 *
 *
 */
static void
fbTemplateFree(
    fbTemplate_t  *tmpl);

static uint32_t
fbTemplateFieldHash(
    fbTemplateField_t  *ie)
{
    return (((ie->canon->ent & 0x0000ffff) << 16) | (ie->canon->num << 2)
            | (ie->midx << 4));
}

static gboolean
fbTemplateFieldEqual(
    const fbTemplateField_t  *a,
    const fbTemplateField_t  *b)
{
    return (a->canon->num == b->canon->num &&
            a->canon->ent == b->canon->ent &&
            a->midx == b->midx);
}

void
fbTemplateDebug(
    const char          *label,
    uint16_t             tid,
    const fbTemplate_t  *tmpl)
{
    const fbTemplateField_t *ie;
    unsigned int             i;

    fprintf(stderr, "%s template %04x [%p] iec=%u sc=%u len=%u\n", label, tid,
            (void *)tmpl, tmpl->ie_count, tmpl->scope_count, tmpl->ie_len);

    for (i = 0; i < tmpl->ie_count; ++i) {
        ie = tmpl->ie_ary[i];
        if (ie->len == FB_IE_VARLEN) {
            fprintf(stderr, "\t%2u VL %02x %08x:%04x (%s)\n",
                    i, (ie->canon->flags & 0xFF),
                    ie->canon->ent, ie->canon->num, ie->canon->name);
        } else {
            fprintf(stderr, "\t%2u %2u %02x %08x:%04x (%s)\n",
                    i, ie->len, (ie->canon->flags & 0xFF),
                    ie->canon->ent, ie->canon->num, ie->canon->name);
        }
    }
}

fbTemplate_t *
fbTemplateAlloc(
    fbInfoModel_t  *model)
{
    fbTemplate_t *tmpl = NULL;

    /* create a new template */
    tmpl = g_slice_new0(fbTemplate_t);

    /* fill it in */
    tmpl->model = model;
    tmpl->tmpl_len = 4;
    tmpl->active = FALSE;

    /* allocate indices table */
    tmpl->indices = g_hash_table_new((GHashFunc)fbTemplateFieldHash,
                                     (GEqualFunc)fbTemplateFieldEqual);
    return tmpl;
}


void
fbTemplateRetain(
    fbTemplate_t  *tmpl)
{
    /* Increment reference count */
    ++(tmpl->ref_count);
}

void
fbTemplateRelease(
    fbTemplate_t  *tmpl)
{
    /* Decrement reference count */
    --(tmpl->ref_count);
    /* Free if not referenced */
    fbTemplateFreeUnused(tmpl);
}

void
fbTemplateFreeUnused(
    fbTemplate_t  *tmpl)
{
    if (tmpl->ref_count <= 0) {
        fbTemplateFree(tmpl);
    }
}

static void
fbTemplateFree(
    fbTemplate_t  *tmpl)
{
    int i;

    if (tmpl->ctx_free) {
        tmpl->ctx_free(tmpl->tmpl_ctx, tmpl->app_ctx);
    }
    /* destroy index table if present */
    if (tmpl->indices) {g_hash_table_destroy(tmpl->indices);}

    /* destroy IE array */
    for (i = 0; i < tmpl->ie_count; i++) {
        g_slice_free(fbTemplateField_t, tmpl->ie_ary[i]);
    }
    g_free(tmpl->ie_ary);

    /* free the positions of the lists */
    g_free(tmpl->bl.positions);
    g_free(tmpl->stl.positions);
    g_free(tmpl->stml.positions);

    /* do not free template metadata, it belongs to the session.
     * template pointers can be in multiple sessions */

    /* destroy offset cache if present */
    g_free(tmpl->off_cache);
    /* destroy template */
    g_slice_free(fbTemplate_t, tmpl);
}

static void
fbElementPositionsAppend(
    fbElementPositions_t  *posArray,
    uint16_t               position)
{
    if (0 == posArray->count) {
        posArray->positions = g_new(uint16_t, 1);
        posArray->count = 1;
    } else {
        posArray->positions =
            g_renew(uint16_t, posArray->positions, ++posArray->count);
    }
    posArray->positions[posArray->count - 1] = position;
}


static fbTemplateField_t *
fbTemplateExtendElements(
    fbTemplate_t  *tmpl)
{
    /* extend the array */
    if (tmpl->ie_count) {
        if (tmpl->ie_count == FB_TMPL_MAX_ELEMENTS) {
            return NULL;
        }
        tmpl->ie_ary =
            g_renew(fbTemplateField_t *, tmpl->ie_ary, ++(tmpl->ie_count));
    } else {
        tmpl->ie_ary = g_new(fbTemplateField_t *, 1);
        ++(tmpl->ie_count);
    }

    /* allocate the element */
    tmpl->ie_ary[tmpl->ie_count - 1] = g_slice_new0(fbTemplateField_t);
    tmpl->ie_ary[tmpl->ie_count - 1]->tmpl = tmpl;

    return tmpl->ie_ary[tmpl->ie_count - 1];
}


/**
 *  Removes the final element from the template.
 */
static void
fbTemplateReduceElements(
    fbTemplate_t  *tmpl)
{
    if (tmpl->ie_count) {
        --tmpl->ie_count;
        g_slice_free(fbTemplateField_t, tmpl->ie_ary[tmpl->ie_count]);
        /* we could also shrink the array here */
    }
}


/**
 *  Updates the `midx` member of `tmpl_ie` as needed, adds `tmpl_ie` to the
 *  `indices` table, and updates the length members (`tmpl_len`, `ie_len`,
 *  `ie_internal_len`) of `tmpl`.
 *
 *  If adding an element would cause the size of the template to overflow,
 *  FALSE is returned.
 */
static gboolean
fbTemplateExtendIndices(
    fbTemplate_t       *tmpl,
    fbTemplateField_t  *tmpl_ie)
{
    gboolean ok;
#ifdef HAVE___BUILTIN_ADD_OVERFLOW
    uint16_t result;
#define FB_SAFE_ADD_U16(x_, y_)  FB_SAFE_ADD_U16_BUILTIN(x_, y_, result)
#else
#define FB_SAFE_ADD_U16(x_, y_)  FB_SAFE_ADD_U16_NOT_BUILTIN(x_, y_)
#endif

    /* search indices table for multiple IE index */
    while (g_hash_table_contains(tmpl->indices, tmpl_ie)) {
        ++tmpl_ie->midx;
        g_assert(0 != tmpl_ie->midx);
    }

    tmpl_ie->offset = tmpl->ie_internal_len;

    /* increment template lengths */
    if (!FB_SAFE_ADD_U16(tmpl->tmpl_len, tmpl_ie->canon->ent ? 8 : 4)) {
        return FALSE;
    }
    if (!FB_SAFE_ADD_U16(tmpl->ie_len,
                         (tmpl_ie->len == FB_IE_VARLEN) ? 1 : tmpl_ie->len))
    {
        tmpl->tmpl_len -= tmpl_ie->canon->ent ? 8 : 4;
        return FALSE;
    }

    if (tmpl_ie->len == FB_IE_VARLEN) {
        tmpl->is_varlen = TRUE;
        if (tmpl_ie->canon->type == FB_BASIC_LIST) {
            tmpl->contains_list = TRUE;
            ok = FB_SAFE_ADD_U16(tmpl->ie_internal_len,
                                 sizeof(fbBasicList_t));
        } else if (tmpl_ie->canon->type == FB_SUB_TMPL_LIST) {
            tmpl->contains_list = TRUE;
            ok = FB_SAFE_ADD_U16(tmpl->ie_internal_len,
                                 sizeof(fbSubTemplateList_t));
        } else if (tmpl_ie->canon->type == FB_SUB_TMPL_MULTI_LIST) {
            tmpl->contains_list = TRUE;
            ok = FB_SAFE_ADD_U16(tmpl->ie_internal_len,
                                 sizeof(fbSubTemplateMultiList_t));
        } else {
            ok = FB_SAFE_ADD_U16(tmpl->ie_internal_len,
                                 sizeof(fbVarfield_t));
        }
    } else {
        ok = FB_SAFE_ADD_U16(tmpl->ie_internal_len, tmpl_ie->len);
    }

    if (!ok) {
        tmpl->tmpl_len -= tmpl_ie->canon->ent ? 8 : 4;
        tmpl->ie_len -= ((tmpl_ie->len == FB_IE_VARLEN) ? 1 : tmpl_ie->len);
        return FALSE;
    }

    tmpl_ie->tmpl = tmpl;
    /* Add index of this information element to the indices table */
    g_hash_table_insert(tmpl->indices, tmpl_ie,
                        GUINT_TO_POINTER(tmpl->ie_count - 1));

    switch (fbTemplateFieldGetType(tmpl_ie)) {
      case FB_BASIC_LIST:
        fbElementPositionsAppend(&tmpl->bl, tmpl->ie_count - 1);
        break;
      case FB_SUB_TMPL_LIST:
        fbElementPositionsAppend(&tmpl->stl, tmpl->ie_count - 1);
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        fbElementPositionsAppend(&tmpl->stml, tmpl->ie_count - 1);
        break;
      default:
        break;
    }

    return TRUE;
}

gboolean
fbTemplateAppend(
    fbTemplate_t           *tmpl,
    const fbInfoElement_t  *ex_ie,
    GError                **err)
{
    fbTemplateField_t *tmpl_ie;

    g_assert(ex_ie);

    if (tmpl->ref_count > 0) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Cannot modify a template that is referenced by a session");
        return FALSE;
    }
    /* if (tmpl->scope_count) { */
    /*     g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL, */
    /*                 "Cannot modify a template that has a non-zero scope");
     */
    /*     return FALSE; */
    /* } */

    /* grow information element array */
    tmpl_ie = fbTemplateExtendElements(tmpl);
    if (!tmpl_ie) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Maximum template size reached");
        return FALSE;
    }

    /* copy information element out of the info model */
    if (!fbInfoElementCopyToTemplate(tmpl->model, ex_ie, tmpl_ie, err)) {
        return FALSE;
    }

    /* Handle index and counter updates */
    if (!fbTemplateExtendIndices(tmpl, tmpl_ie)) {
        fbTemplateReduceElements(tmpl);
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Maximum template size reached");
        return FALSE;
    }

    /* All done */
    return TRUE;
}

gboolean
fbTemplateAppendSpec(
    fbTemplate_t               *tmpl,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    flags,
    GError                    **err)
{
    fbTemplateField_t *tmpl_ie;

    /* Short-circuit on app flags mismatch */
    if (spec->flags && !((spec->flags & flags) == spec->flags)) {
        return TRUE;
    }

    if (tmpl->ref_count > 0) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Cannot modify a template that is referenced by a session");
        return FALSE;
    }
    /* if (tmpl->scope_count > 0) { */
    /*     g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL, */
    /*                 "Cannot modify a template that has a non-zero scope");
     */
    /*     return FALSE; */
    /* } */

    /* grow information element array */
    tmpl_ie = fbTemplateExtendElements(tmpl);
    if (!tmpl_ie) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Maximum template size reached");
        return FALSE;
    }

    /* copy information element out of the info model */
    if (!fbInfoElementCopyToTemplateByName(tmpl->model, spec, tmpl_ie, err)) {
        return FALSE;
    }
    if (spec->len_override == 0 && tmpl_ie->canon->len != FB_IE_VARLEN) {
        tmpl->default_length = TRUE;
    }

    /* Handle index and counter updates */
    if (!fbTemplateExtendIndices(tmpl, tmpl_ie)) {
        fbTemplateReduceElements(tmpl);
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Maximum template size reached");
        return FALSE;
    }

    return TRUE;
}

gboolean
fbTemplateAppendSpecArray(
    fbTemplate_t               *tmpl,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    flags,
    GError                    **err)
{
    for (; spec->name; spec++) {
        if (!fbTemplateAppendSpec(tmpl, spec, flags, err)) {
            return FALSE;
        }
    }
    return TRUE;
}


gboolean
fbTemplateAppendSpecId(
    fbTemplate_t                 *tmpl,
    const fbInfoElementSpecId_t  *spec,
    uint32_t                      flags,
    GError                      **err)
{
    fbTemplateField_t *tmpl_ie;

    /* Short-circuit on app flags mismatch */
    if (spec->flags && !((spec->flags & flags) == spec->flags)) {
        return TRUE;
    }

    if (tmpl->ref_count > 0) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Cannot modify a template that is referenced by a session");
        return FALSE;
    }
    /* if (tmpl->scope_count > 0) { */
    /*     g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL, */
    /*                 "Cannot modify a template that has a non-zero scope");
     */
    /*     return FALSE; */
    /* } */

    /* grow information element array */
    tmpl_ie = fbTemplateExtendElements(tmpl);
    if (!tmpl_ie) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Maximum template size reached");
        return FALSE;
    }

    /* copy information element out of the info model */
    if (!fbInfoElementCopyToTemplateByIdent(tmpl->model, spec, tmpl_ie, err)) {
        return FALSE;
    }
    if (spec->len_override == 0 && tmpl_ie->canon->len != FB_IE_VARLEN) {
        tmpl->default_length = TRUE;
    }

    /* Handle index and counter updates */
    if (!fbTemplateExtendIndices(tmpl, tmpl_ie)) {
        fbTemplateReduceElements(tmpl);
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Maximum template size reached");
        return FALSE;
    }
    /* All done */
    return TRUE;
}

gboolean
fbTemplateAppendArraySpecId(
    fbTemplate_t                 *tmpl,
    const fbInfoElementSpecId_t  *spec,
    uint32_t                      flags,
    GError                      **err)
{
    for (; spec->ident.element_id; spec++) {
        if (!fbTemplateAppendSpecId(tmpl, spec, flags, err)) {
            return FALSE;
        }
    }
    return TRUE;
}

gboolean
fbTemplateSetOptionsScope(
    fbTemplate_t  *tmpl,
    uint16_t       scope_count)
{
    /* Cannot set options scope if we've already done so or if the template is
     * in a session. */
    if (tmpl->scope_count > 0 || tmpl->ref_count > 0) {
        return FALSE;
    }

    /* Cannot set scope count higher than IE count */
    if (scope_count > tmpl->ie_count || 0 == tmpl->ie_count) {
        return FALSE;
    }

    /* scope count of zero means make the last IE the end of scope */
    tmpl->scope_count = scope_count ? scope_count : tmpl->ie_count;

    /* account for scope count in output */
    tmpl->tmpl_len += 2;

    return TRUE;
}


gboolean
fbTemplateContainsElement(
    const fbTemplate_t     *tmpl,
    const fbInfoElement_t  *ex_ie)
{
    fbTemplateField_t search;

    search.midx = 0;
    search.canon = ex_ie;

    return (ex_ie != NULL && tmpl != NULL
            && g_hash_table_contains(tmpl->indices, &search));
}

gboolean
fbTemplateContainsElementByName(
    const fbTemplate_t         *tmpl,
    const fbInfoElementSpec_t  *spec)
{
    return fbTemplateContainsElement(
        tmpl, fbInfoModelGetElementByName(tmpl->model, spec->name));
}

gboolean
fbTemplateContainsAllElementsByName(
    const fbTemplate_t         *tmpl,
    const fbInfoElementSpec_t  *spec)
{
    fbTemplateField_t search;

    g_assert(tmpl);
    g_assert(spec);

    search.midx = 0;

    for (; spec->name; spec++) {
        search.canon = fbInfoModelGetElementByName(tmpl->model, spec->name);
        if (NULL == search.canon ||
            !g_hash_table_contains(tmpl->indices, &search))
        {
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
fbTemplateContainsAllFlaggedElementsByName(
    const fbTemplate_t         *tmpl,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    flags)
{
    fbTemplateField_t search;

    g_assert(tmpl);
    g_assert(spec);

    search.midx = 0;

    for (; spec->name; spec++) {
        if (spec->flags && !((spec->flags & flags) == spec->flags)) {
            continue;
        }
        search.canon = fbInfoModelGetElementByName(tmpl->model, spec->name);
        if (NULL == search.canon ||
            !g_hash_table_contains(tmpl->indices, &search))
        {
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
fbTemplateContainsSpecId(
    const fbTemplate_t           *tmpl,
    const fbInfoElementSpecId_t  *spec)
{
    fbTemplateField_t search;
    fbInfoElement_t   ie;

    g_assert(tmpl);
    g_assert(spec);

    search.canon = &ie;
    search.midx = 0;
    ie.ent = spec->ident.enterprise_id;
    ie.num = spec->ident.element_id;

    return g_hash_table_contains(tmpl->indices, &search);
}

gboolean
fbTemplateContainsAllArraySpecId(
    const fbTemplate_t           *tmpl,
    const fbInfoElementSpecId_t  *spec)
{
    fbTemplateField_t search;
    fbInfoElement_t   ie;

    g_assert(tmpl);
    g_assert(spec);

    search.canon = &ie;
    search.midx = 0;

    for (; spec->ident.element_id; ++spec) {
        ie.ent = spec->ident.enterprise_id;
        ie.num = spec->ident.element_id;
        if (!g_hash_table_contains(tmpl->indices, &search)) { return FALSE; }
    }

    return TRUE;
}

gboolean
fbTemplateContainsAllFlaggedArraySpecId(
    const fbTemplate_t           *tmpl,
    const fbInfoElementSpecId_t  *spec,
    uint32_t                      flags)
{
    fbTemplateField_t search;
    fbInfoElement_t   ie;

    g_assert(tmpl);
    g_assert(spec);

    search.canon = &ie;
    search.midx = 0;

    for (; spec->ident.element_id; ++spec) {
        if (spec->flags && !((spec->flags & flags) == spec->flags)) {
            continue;
        }
        ie.ent = spec->ident.enterprise_id;
        ie.num = spec->ident.element_id;
        if (!g_hash_table_contains(tmpl->indices, &search)) { return FALSE; }
    }

    return TRUE;
}

uint16_t
fbTemplateCountElements(
    const fbTemplate_t  *tmpl)
{
    return tmpl->ie_count;
}


const fbTemplateField_t *
fbTemplateGetFieldByPosition(
    const fbTemplate_t  *tmpl,
    uint16_t             position)
{
    if (position < tmpl->ie_count) {
        return tmpl->ie_ary[position];
    }
    return NULL;
}

/* FIXME: remove this */
const fbTemplateField_t *
fbTemplateGetIndexedIE(
    const fbTemplate_t  *tmpl,
    uint16_t             position)
{
    if (position < tmpl->ie_count) {
        return tmpl->ie_ary[position];
    }
    return NULL;
}

const fbTemplateField_t *
fbTemplateFindFieldByElement(
    const fbTemplate_t     *tmpl,
    const fbInfoElement_t  *ie,
    uint16_t               *position,
    uint16_t                skip)
{
    fbTemplateField_t search = FB_TEMPLATEFIELD_INIT;
    gpointer          key = NULL;
    gpointer          value = NULL;
    uint16_t          i;

    /* if position is NULL or its referent 0, use the hash lookup code below;
     * otherwise, adjust the search depending on position's value */
    if (position && *position) {
        if (*position >= (tmpl->ie_count >> 1)) {
            /* position is in last half of template; linearly scan from
             * *position to find the ie */
            for (i = *position; i < tmpl->ie_count; ++i) {
                if (ie == tmpl->ie_ary[i]->canon) {
                    if (skip != 0) {
                        --skip;
                    } else {
                        *position = i;
                        return tmpl->ie_ary[i];
                    }
                }
            }
            return NULL;
        }

        /* position is in first half of template; increment skip by number of
         * times ie appears before position then use the hash lookup */

        /* since we may increment skip, protect against overflow */
        if (skip >= tmpl->ie_count) {
            return NULL;
        }

        i = 0;
        do {
            if (ie == tmpl->ie_ary[i]->canon) {
                ++skip;
            }
        } while (++i < *position);
    }

    /* use a hash lookup to find the ie */
    search.canon = ie;
    search.midx = skip;
    if (g_hash_table_lookup_extended(tmpl->indices, &search, &key, &value)) {
        if (position) {
            *position = GPOINTER_TO_INT(value);
        }
        return (fbTemplateField_t *)key;
    }

    return NULL;
}


/**
 *  fbTemplateFindInElementPositions
 *
 *  A helper function for fbTemplateFindFieldByDataType().
 *
 *  Uses the cache of InfoElement positions in `posArray` to find an element
 *  at or after the position given by the referent of `position` and skipping
 *  `skip` elements.
 */
static const fbTemplateField_t *
fbTemplateFindInElementPositions(
    const fbTemplate_t          *tmpl,
    const fbElementPositions_t  *posArray,
    uint16_t                    *position,
    uint16_t                     skip)
{
    const uint16_t i = ((position) ? *position : 0);
    uint16_t       j;

    for (j = 0; j < posArray->count; ++j) {
        if (i <= posArray->positions[j]) {
            j += skip;
            if (j < posArray->count) {
                if (position) {
                    *position = posArray->positions[j];
                }
                return tmpl->ie_ary[posArray->positions[j]];
            }
        }
    }

    return NULL;
}


const fbTemplateField_t *
fbTemplateFindFieldByDataType(
    const fbTemplate_t       *tmpl,
    fbInfoElementDataType_t   datatype,
    uint16_t                 *position,
    uint16_t                  skip)
{
    uint16_t i;

    switch (datatype) {
      case FB_BASIC_LIST:
        return fbTemplateFindInElementPositions(
            tmpl, &tmpl->bl, position, skip);
      case FB_SUB_TMPL_LIST:
        return fbTemplateFindInElementPositions(
            tmpl, &tmpl->stl, position, skip);
      case FB_SUB_TMPL_MULTI_LIST:
        return fbTemplateFindInElementPositions(
            tmpl, &tmpl->stml, position, skip);
      default:
        for (i = ((position) ? *position : 0); i < tmpl->ie_count; ++i) {
            if (fbInfoElementGetType(tmpl->ie_ary[i]->canon) == datatype) {
                if (skip != 0) {
                    --skip;
                } else {
                    if (position) {
                        *position = i;
                    }
                    return tmpl->ie_ary[i];
                }
            }
        }
    }

    return NULL;
}


const fbTemplateField_t *
fbTemplateFindFieldByIdent(
    const fbTemplate_t  *tmpl,
    uint32_t             ent,
    uint16_t             num,
    uint16_t            *position,
    uint16_t             skip)
{
    const fbInfoElement_t *ie;
    uint16_t i;

    ie = fbInfoModelGetElementByID(tmpl->model, ent, num);
    if (ie) {
        return fbTemplateFindFieldByElement(tmpl, ie, position, skip);
    }

    for (i = (position ? *position : 0); i < tmpl->ie_count; ++i) {
        if (fbInfoElementCheckIdent(tmpl->ie_ary[i]->canon, ent, num)) {
            if (skip != 0) {
                --skip;
            } else {
                if (position) {
                    *position = i;
                }
                return tmpl->ie_ary[i];
            }
        }
    }
    return NULL;
}


uint16_t
fbTemplateGetOptionsScope(
    const fbTemplate_t  *tmpl)
{
    return tmpl->scope_count;
}

void *
fbTemplateGetContext(
    const fbTemplate_t  *tmpl)
{
    return tmpl->tmpl_ctx;
}

void
fbTemplateSetContext(
    fbTemplate_t          *tmpl,
    void                  *tmpl_ctx,
    void                  *app_ctx,
    fbTemplateCtxFree_fn   ctx_free)
{
    if (tmpl->tmpl_ctx) {
        tmpl->ctx_free(tmpl->tmpl_ctx, tmpl->app_ctx);
    }

    tmpl->tmpl_ctx  = tmpl_ctx;
    tmpl->ctx_free  = ctx_free;
    tmpl->app_ctx = app_ctx;
}

uint16_t
fbTemplateGetIELenOfMemBuffer(
    const fbTemplate_t  *tmpl)
{
    return tmpl->ie_internal_len;
}

fbInfoModel_t *
fbTemplateGetInfoModel(
    const fbTemplate_t  *tmpl)
{
    return tmpl->model;
}


void
fbTemplateIterInit(
    fbTemplateIter_t    *iter,
    const fbTemplate_t  *tmpl)
{
    iter->tmpl = tmpl;
    iter->pos = UINT16_MAX;
}

const fbTemplateField_t *
fbTemplateIterGetField(
    const fbTemplateIter_t  *iter)
{
    g_assert(iter->tmpl);
    if (iter->pos < iter->tmpl->ie_count) {
        return iter->tmpl->ie_ary[iter->pos];
    }
    return NULL;
}

uint16_t
fbTemplateIterGetPosition(
    const fbTemplateIter_t  *iter)
{
    return iter->pos;
}

const fbTemplate_t *
fbTemplateIterGetTemplate(
    const fbTemplateIter_t  *iter)
{
    return iter->tmpl;
}

const fbTemplateField_t *
fbTemplateIterNext(
    fbTemplateIter_t  *iter)
{
    g_assert(iter->tmpl);
    if (UINT16_MAX == iter->pos) {
        iter->pos = 0;
    } else {
        ++iter->pos;
    }
    if (iter->pos < iter->tmpl->ie_count) {
        return iter->tmpl->ie_ary[iter->pos];
    }
    iter->pos = iter->tmpl->ie_count;
    return NULL;
}


fbTemplateInfo_t *
fbTemplateInfoAlloc(
    void)
{
    return g_slice_new0(fbTemplateInfo_t);
}

void
fbTemplateInfoFree(
    fbTemplateInfo_t  *tmplInfo)
{
    if (tmplInfo) {
        g_free((void *)tmplInfo->name);
        g_free((void *)tmplInfo->description);
        g_free(tmplInfo->blInfo);
        g_slice_free(fbTemplateInfo_t, tmplInfo);
    }
}

fbTemplateInfo_t *
fbTemplateInfoCopy(
    const fbTemplateInfo_t  *tmplInfo)
{
    fbTemplateInfo_t *copy;

    copy = fbTemplateInfoAlloc();
    fbTemplateInfoInit(copy, tmplInfo->name, tmplInfo->description,
                       tmplInfo->appLabel, tmplInfo->parentTid);
    if (tmplInfo->blCount) {
        copy->blCount = tmplInfo->blCount;
        copy->blInfo = (fbBasicListInfo_t *)g_memdup2(
            tmplInfo->blInfo, sizeof(fbBasicListInfo_t) * tmplInfo->blCount);
    }
    return copy;
}


gboolean
fbTemplateInfoInit(
    fbTemplateInfo_t  *tmplInfo,
    const char        *name,
    const char        *description,
    uint16_t           appLabel,
    uint16_t           parentTid)
{
    if (NULL == name) {
        return FALSE;
    }
    tmplInfo->name        = g_strdup(name);
    tmplInfo->appLabel    = appLabel;
    tmplInfo->parentTid   = parentTid;
    if (description) {
        tmplInfo->description = g_strdup(description);
    }
    return TRUE;
}


void
fbTemplateInfoAddBasicList(
    fbTemplateInfo_t  *tmplInfo,
    uint32_t           blEnt,
    uint16_t           blNum,
    uint32_t           contentEnt,
    uint16_t           contentNum)
{
    fbBasicListInfo_t *blInfo;

    ++tmplInfo->blCount;
    tmplInfo->blInfo = g_renew(fbBasicListInfo_t, tmplInfo->blInfo,
                               tmplInfo->blCount);

    blInfo = &tmplInfo->blInfo[tmplInfo->blCount - 1];

    blInfo->blEnt = blEnt;
    blInfo->blNum = blNum;
    blInfo->contentEnt = contentEnt;
    blInfo->contentNum = contentNum;
}


void
fbTemplateInfoSetTemplateId(
    fbTemplateInfo_t  *tmplInfo,
    uint16_t           tid)
{
    g_assert(0 == tmplInfo->tid);
    tmplInfo->tid = tid;
}


void
fbTemplateInfoRecordInit(
    fbTemplateInfoRecord_t  *mdRec)
{
    memset(mdRec, 0, sizeof(*mdRec));
}

void
fbTemplateInfoRecordClear(
    fbTemplateInfoRecord_t  *mdRec)
{
    fbSubTemplateListClear(&mdRec->blInfoList);
}

/* take a tmplInfo struct and prepare a record for export */
void
fbTemplateInfoFillRecord(
    const fbTemplateInfo_t  *tmplInfo,
    fbTemplateInfoRecord_t  *mdRec,
    const fbTemplate_t      *stlTemplate,
    uint16_t                 stlTid)
{
    fbBasicListInfo_t *blInfo;

    g_assert(tmplInfo);
    g_assert(tmplInfo->name);
    g_assert(mdRec);
    g_assert(stlTemplate);
    g_assert(stlTid);

    memset(mdRec, 0, sizeof(*mdRec));
    blInfo = fbSubTemplateListInit(&mdRec->blInfoList, 3,
                                   stlTid, stlTemplate, tmplInfo->blCount);
    if (tmplInfo->blCount) {
        memcpy(blInfo, tmplInfo->blInfo,
               tmplInfo->blCount * sizeof(fbBasicListInfo_t));
    }

    mdRec->tid          = tmplInfo->tid;
    mdRec->name.buf     = (uint8_t *)tmplInfo->name;
    mdRec->name.len     = strlen(tmplInfo->name);
    mdRec->parentTid    = tmplInfo->parentTid;
    mdRec->appLabel     = tmplInfo->appLabel;

    if (tmplInfo->description) {
        mdRec->description.buf = (uint8_t *)tmplInfo->description;
        mdRec->description.len = strlen(tmplInfo->description);
    }
}


/**
 *  Takes a template metadata record from the wire and converts it to a newly
 *  allocated TemplateInfo.  `mdRecVersion` is FB_TMPL_IS_META_TEMPLATE_V3 for
 *  a fixbuf-v3.0.0 style metadata record or FB_TMPL_IS_META_TEMPLATE_V1 for a
 *  fixbuf-v1.8.0 style metadata record.
 */
fbTemplateInfo_t *
fbTemplateInfoCreateFromRecord(
    const fbTemplateInfoRecord_t  *mdRec,
    unsigned int                   mdRecVersion,
    GError                       **err)
{
    fbTemplateInfo_t *tmplInfo;

    g_assert(mdRec);

    if (mdRec->tid < FB_TID_MIN_DATA) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "TemplateInfo record uses invalid template id %u",
                    mdRec->tid);
        return NULL;
    }
    if (mdRec->name.len == 0) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    ("TemplateInfo record for template ID %u"
                     " has a zero-length name"), mdRec->tid);
        return NULL;
    }

    tmplInfo = fbTemplateInfoAlloc();
    fbTemplateInfoInit(tmplInfo,
                       (const char *)mdRec->name.buf,
                       (const char *)mdRec->description.buf,
                       mdRec->appLabel,
                       ((FB_TMPL_IS_META_TEMPLATE_V3 == mdRecVersion)
                        ? mdRec->parentTid : FB_TMPL_MD_LEVEL_NA));
    tmplInfo->tid = mdRec->tid;
    tmplInfo->blCount = mdRec->blInfoList.numElements;
    if (tmplInfo->blCount) {
        tmplInfo->blInfo = (fbBasicListInfo_t *)g_memdup2(
            mdRec->blInfoList.dataPtr,
            sizeof(fbBasicListInfo_t) * tmplInfo->blCount);
    }

    return tmplInfo;
}


gboolean
fbTemplateIsMetadata(
    const fbTemplate_t  *tmpl,
    uint32_t             tests)
{
    if ((tests & FB_TMPL_IS_OPTIONS) && !fbTemplateGetOptionsScope(tmpl)) {
        return FALSE;
    }

    /* check whether element metadata */
    if ((tests & FB_TMPL_IS_META_ELEMENT) &&
        fbInfoModelIsTemplateElementType(tmpl))
    {
        return TRUE;
    }

    /* check whether top-level template metadata (fixbuf v3) */
    if ((tests & FB_TMPL_IS_META_TEMPLATE_V3) &&
        fbTemplateContainsAllFlaggedElementsByName(
            tmpl, fb_template_info_spec, 4))
    {
        return TRUE;
    }

    /* check whether top-level template metadata (fixbuf v1.8.0) */
    if ((tests & FB_TMPL_IS_META_TEMPLATE_V1) &&
        fbTemplateContainsAllFlaggedElementsByName(
            tmpl, fb_template_info_spec, 0))
    {
        return TRUE;
    }

    /* check whether the nested BasicList info template.
     * fbTemplateContainsAllElementsByName() is insufficient since it cannot
     * check for repeated elements.  Instead, try to find the 2nd occurance of
     * the informationElementId (303) and privateEnterpriseNumber (346)
     * template fields. */
    if ((tests & FB_TMPL_IS_META_BASICLIST) &&
        tmpl->ie_count == 4 &&
        NULL != fbTemplateFindFieldByIdent(tmpl, 0, 303, NULL, 1) &&
        NULL != fbTemplateFindFieldByIdent(tmpl, 0, 346, NULL, 1))
    {
        return TRUE;
    }

    return FALSE;
}



gboolean
fbTemplateAllocTemplateInfoTemplates(
    fbInfoModel_t  *model,
    fbTemplate_t  **metadata_v3_tmpl,
    fbTemplate_t  **bl_ie_metadata_tmpl,
    GError        **err)
{
    if (metadata_v3_tmpl) {
        *metadata_v3_tmpl = fbTemplateAlloc(model);
        if (!fbTemplateAppendSpecArray(
                *metadata_v3_tmpl, fb_template_info_spec, 6, err))
        {
            goto ERROR;
        }
        fbTemplateSetOptionsScope(*metadata_v3_tmpl, 1);
    }

    if (bl_ie_metadata_tmpl) {
        *bl_ie_metadata_tmpl = fbTemplateAlloc(model);
        if (!fbTemplateAppendSpecArray(
                *bl_ie_metadata_tmpl, fb_basiclist_info_spec, 0, err))
        {
            goto ERROR;
        }
        fbTemplateSetOptionsScope(*bl_ie_metadata_tmpl, 2);
    }

    return TRUE;

  ERROR:
    if (*metadata_v3_tmpl) {
        fbTemplateFreeUnused(*metadata_v3_tmpl);
        *metadata_v3_tmpl = NULL;
    }
    if (*bl_ie_metadata_tmpl) {
        fbTemplateFreeUnused(*bl_ie_metadata_tmpl);
        *bl_ie_metadata_tmpl = NULL;
    }
    return FALSE;
}


gboolean
fbTemplatesAreEqual(
    const fbTemplate_t  *tmpl1,
    const fbTemplate_t  *tmpl2)
{
    uint16_t i;

    if (tmpl1 == tmpl2) {
        return TRUE;
    }
    if (tmpl1->ie_count != tmpl2->ie_count ||
        tmpl1->scope_count != tmpl2->scope_count ||
        tmpl1->tmpl_len != tmpl2->tmpl_len ||
        tmpl1->ie_internal_len != tmpl2->ie_internal_len)
    {
        return FALSE;
    }
    for (i = 0; i < tmpl1->ie_count; i++) {
        if (!fbTemplateFieldEqual(tmpl1->ie_ary[i], tmpl2->ie_ary[i]) ||
            tmpl1->ie_ary[i]->len != tmpl2->ie_ary[i]->len)
        {
            return FALSE;
        }
    }
    return TRUE;
}


static gint
fbTemplateFieldsCmp(
    gconstpointer   a,
    gconstpointer   b,
    gpointer        ignore_len)
{
    const fbTemplateField_t *fa = *((fbTemplateField_t **)a);
    const fbTemplateField_t *fb = *((fbTemplateField_t **)b);
    gint order;

    order = ((gint)fbInfoElementGetPEN(fa->canon) -
             (gint)fbInfoElementGetPEN(fb->canon));
    if (order) { return order; }

    order = ((gint)fbInfoElementGetId(fa->canon) -
             (gint)fbInfoElementGetId(fb->canon));
    if (order || GPOINTER_TO_INT(ignore_len)) { return order; }

    return ((gint)fa->len - (gint)fb->len);
}


fbTemplatesSetCompareStatus_t
fbTemplatesSetCompare(
    const fbTemplate_t  *tmpl1,
    const fbTemplate_t  *tmpl2,
    uint16_t            *matching_fields,
    unsigned int         flags)
{
    const fbTemplate_t           *tmpl[2];
    const fbInfoElement_t         padding = FB_IE_INIT(NULL, 0, 210, 0, 0);
    const fbTemplateField_t      *f0, *f1;
    fbTemplatesSetCompareStatus_t ret;
    GPtrArray   *fields[2];
    gpointer     ignore_len;
    unsigned int j, k;
    uint16_t     matches;
    int          shorter;
    int          longer;
    int          cmp;

    if (tmpl1 == tmpl2) {
        /* same template pointer, compute matching fields if needed */
        if (matching_fields) {
            *matching_fields = tmpl1->ie_count;
            if (flags & FB_TMPL_CMP_IGNORE_PADDING) {
                for (k = 0; k < tmpl1->ie_count; ++k) {
                    f0 = tmpl1->ie_ary[k];
                    if (fbInfoElementEqual(f0->canon, &padding)) {
                        --*matching_fields;
                    }
                }
            }
        }
        return FB_TMPL_SETCMP_EQUAL;
    }

    ignore_len = GINT_TO_POINTER(flags & FB_TMPL_CMP_IGNORE_LENGTHS);

    tmpl[0] = tmpl1;
    tmpl[1] = tmpl2;

    /* create a sorted array of TemplateFields for each template */
    for (j = 0; j < 2; ++j) {
        fields[j] = g_ptr_array_sized_new(tmpl[j]->ie_count);
        for (k = 0; k < tmpl[j]->ie_count; ++k) {
            f0 = tmpl[j]->ie_ary[k];
            if (0 == (flags & FB_TMPL_CMP_IGNORE_PADDING)
                || !fbInfoElementEqual(f0->canon, &padding))
            {
                g_ptr_array_add(fields[j], (gpointer)f0);
            }
        }
        g_ptr_array_sort_with_data(fields[j], fbTemplateFieldsCmp, ignore_len);
    }

    /* loop over the TemplateField array with fewer elements */
    shorter = fields[1]->len < fields[0]->len;
    longer = !shorter;

    matches = 0;
    for (j = 0, k = 0; j < fields[shorter]->len; ++j) {
        f0 = (fbTemplateField_t *)g_ptr_array_index(fields[shorter], j);
        while (k < fields[longer]->len) {
            f1 = (fbTemplateField_t *)g_ptr_array_index(fields[longer], k);
            cmp = fbTemplateFieldsCmp(&f0, &f1, ignore_len);
            if (cmp < 0) {
                /* no match. go to next field in the shorter template */
                break;
            }
            ++k;
            if (0 == cmp) {
                ++matches;
                break;
            }
            /* go to next field in longer tmpl to try and find a match */
        }
    }

    if (matching_fields) {
        *matching_fields = matches;
    }
    if (fields[shorter]->len != matches) {
        if (0 == matches) {
            ret = FB_TMPL_SETCMP_DISJOINT;
        } else {
            ret = FB_TMPL_SETCMP_COMMON;
        }
    } else if (fields[1]->len == fields[0]->len) {
        /* templates are equal */
        ret = FB_TMPL_SETCMP_EQUAL;
    } else if (shorter == 0) {
        /* tmpl1 is a subset of tmpl2 */
        ret = FB_TMPL_SETCMP_SUBSET;
    } else {
        /* tmpl1 is a superset of tmpl2 */
        ret = FB_TMPL_SETCMP_SUPERSET;
    }

    g_ptr_array_free(fields[0], TRUE);
    g_ptr_array_free(fields[1], TRUE);
    return ret;
}


/*
 *  fbTemplatesCompare
 *
 *
 */
int
fbTemplatesCompare(
    const fbTemplate_t  *tmpl1,
    const fbTemplate_t  *tmpl2,
    unsigned int         flags)
{
    const fbTemplateField_t *field1, *field2;
    const fbInfoElement_t    padding = FB_IE_INIT(NULL, 0, 210, 0, 0);
    gboolean isscope1, isscope2;
    uint16_t i1, i2;

    if (tmpl1 == tmpl2) {
        return 0;
    }
    if (0 == (flags & FB_TMPL_CMP_IGNORE_PADDING)) {
        /* when not ignoring padding, ensure the lengths match */
        if (tmpl1->ie_count != tmpl2->ie_count) {
            return (int)tmpl1->ie_count - (int)tmpl2->ie_count;
        }
    }
    if (flags & FB_TMPL_CMP_IGNORE_SCOPE) {
        isscope1 = isscope2 = FALSE;
    } else {
        isscope1 = (tmpl1->scope_count > 0);
        isscope2 = (tmpl2->scope_count > 0);
    }

    i1 = i2 = 0;
    while (i1 < tmpl1->ie_count && i2 < tmpl2->ie_count) {
        field1 = tmpl1->ie_ary[i1];
        field2 = tmpl2->ie_ary[i2];
        if (flags & FB_TMPL_CMP_IGNORE_PADDING) {
            if (fbInfoElementEqual(field1->canon, &padding)) {
                ++i1;
                if (fbInfoElementEqual(field2->canon, &padding)) {
                    ++i2;
                }
                continue;
            }
            if (fbInfoElementEqual(field2->canon, &padding)) {
                ++i2;
                continue;
            }
        }
        ++i1; ++i2;
        if (isscope1 && i1 > tmpl1->scope_count) {
            isscope1 = FALSE;
        }
        if (isscope2 && i2 > tmpl2->scope_count) {
            isscope2 = FALSE;
        }

        if (!fbTemplateFieldEqual(field1, field2)) {
            return 1;
        }
        if (0 == (flags & FB_TMPL_CMP_IGNORE_LENGTHS) &&
            field1->len != field2->len)
        {
            return 1;
        }
        if (0 == (flags & FB_TMPL_CMP_IGNORE_SCOPE) &&
            isscope1 != isscope2)
        {
            return 1;
        }
    }

    if (flags & FB_TMPL_CMP_IGNORE_PADDING) {
        /* skip any padding at the end */
        for (; i1 < tmpl1->ie_count; ++i1) {
            field1 = tmpl1->ie_ary[i1];
            if (!fbInfoElementEqual(field1->canon, &padding)) {
                break;
            }
        }
        for (; i2 < tmpl2->ie_count; ++i2) {
            field2 = tmpl2->ie_ary[i2];
            if (!fbInfoElementEqual(field2->canon, &padding)) {
                break;
            }
        }
    }

    if (i2 < tmpl2->ie_count) {
        /* tmpl2 has more elements, so tmpl1 is less than tmpl2 */
        return -1;
    }
    return (i1 < tmpl1->ie_count);
}


fbTemplate_t *
fbTemplateCopy(
    const fbTemplate_t  *tmpl,
    uint32_t             flags)
{
    const fbTemplateField_t *ie;
    fbTemplate_t            *t;
    unsigned int             i;
    fbInfoElement_t          freshIE;
    uint16_t scope_count = 0;
    gboolean remove_padding;

    remove_padding = ((flags & FB_TMPL_COPY_REMOVE_PADDING) != 0);
    memset(&freshIE, 0, sizeof(freshIE));

    t = fbTemplateAlloc(fbTemplateGetInfoModel(tmpl));
    for (i = 0; i < tmpl->ie_count; ++i) {
        ie = tmpl->ie_ary[i];
        if (!remove_padding || ie->canon->num != 210 || ie->canon->ent != 0) {
            freshIE.num = ie->canon->num;
            freshIE.ent = ie->canon->ent;
            freshIE.len = ie->len;
            if (!fbTemplateAppend(t, &freshIE, NULL)) {
                fbTemplateFreeUnused(t);
                return NULL;
            }
            if (i < tmpl->scope_count) {
                ++scope_count;
            }
        }
    }
    if (scope_count && ((flags & FB_TMPL_COPY_IGNORE_SCOPE) == 0)) {
        fbTemplateSetOptionsScope(t, scope_count);
    }
    return t;
}


uint16_t
fbTemplateInfoGetApplabel(
    const fbTemplateInfo_t  *tmplInfo)
{
    return tmplInfo->appLabel;
}

uint16_t
fbTemplateInfoGetParentTid(
    const fbTemplateInfo_t  *tmplInfo)
{
    return tmplInfo->parentTid;
}

const char *
fbTemplateInfoGetName(
    const fbTemplateInfo_t  *tmplInfo)
{
    return tmplInfo->name;
}

const char *
fbTemplateInfoGetDescription(
    const fbTemplateInfo_t  *tmplInfo)
{
    return tmplInfo->description;
}

uint16_t
fbTemplateInfoGetTemplateId(
    const fbTemplateInfo_t  *tmplInfo)
{
    return tmplInfo->tid;
}

const fbBasicListInfo_t *
fbTemplateInfoGetNextBasicList(
    const fbTemplateInfo_t   *tmplInfo,
    const fbBasicListInfo_t  *blInfo)
{
    if (NULL == blInfo) {
        if (0 == tmplInfo->blCount) {
            return NULL;
        }
        return tmplInfo->blInfo;
    }

    if (blInfo < tmplInfo->blInfo
        || blInfo >= tmplInfo->blInfo + (tmplInfo->blCount - 1))
    {
        return NULL;
    }

    return blInfo + 1;
}

uint16_t
fbBasicListInfoGetListIdent(
    const fbBasicListInfo_t  *blInfo,
    uint32_t                 *pen)
{
    if (pen) {
        *pen = blInfo->blEnt;
    }
    return blInfo->blNum;
}

uint16_t
fbBasicListInfoGetContentIdent(
    const fbBasicListInfo_t  *blInfo,
    uint32_t                 *pen)
{
    if (pen) {
        *pen = blInfo->contentEnt;
    }
    return blInfo->contentNum;
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
