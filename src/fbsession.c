/*
 *  Copyright 2006-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file fbsession.c
 *  IPFIX Transport Session state container
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Brian Trammell
 *  ------------------------------------------------------------------------
 */

#define _FIXBUF_SOURCE_
#include <fixbuf/private.h>


/* whether to debug writing of InfoElement and Template metadata */
#ifndef FB_DEBUG_MD
#define FB_DEBUG_MD 0
#endif

/**
 *  For a template pair, value used by the look-up table to denote that the
 *  caller wants to disable transcoding of the external template.
 */
#define TMPL_PAIR_NO_TRANSCODE  1

/** Size of the tmpl_pair_array.  Note: we could shrink this slightly by not
 * storing values for invalid TIDs 0-255. */
#define TMPL_PAIR_ARRAY_SIZE    (sizeof(uint16_t) * (1 << 16))

/* FIXME: Consider changing fbSession so the ext_FOO/int_FOO pairs of
 * members become a FOO[2] array and the `internal` gboolean used by
 * several function is used as the index into those arrays. */

struct fbSession_st {
    /** Information model. */
    fbInfoModel_t             *model;
    /**
     * Internal template table. Maps template ID to internal template.
     */
    GHashTable                *int_ttab;
    /**
     * External template table for current observation domain.
     * Maps template ID to external template.  References a value from
     * the `dom_ttab` table.
     */
    GHashTable                *ext_ttab;
    /**
     * Template metadata table for current observation domain.
     * Maps template ID to template metadata.  References a value from
     * the `dom_mdInfoTab` table.
     */
    GHashTable                *mdInfoTab;

    /**
     * Array of size 2^16 where index is external TID and value is
     * internal TID.  The number if valid entries in the array is
     * maintained by 'tmpl_pair_count'.
     */
    uint16_t                  *tmpl_pair_array;
    /**
     * Callback function to allow an application to assign template
     * pairs for transcoding purposes, and to add context to a
     * particular template.  Uses 'tmpl_app_ctx'.
     */
    fbNewTemplateCallback_fn   new_template_callback;
    /**
     * Context the caller provides for 'new_template_callback'.
     */
    void                      *tmpl_app_ctx;
    /**
     * A pointer to the largest template seen in this session.  The
     * size of this template is in `largestInternalTemplateLength`.
     */
    const fbTemplate_t        *largestInternalTemplate;
    /**
     * Domain external template table.
     * Maps domain to external template table.
     */
    GHashTable                *dom_ttab;
    /**
     * Domain metadata info table.
     * Maps domain to external template metadata info.
     */
    GHashTable                *dom_mdInfoTab;
    /**
     * Domain last/next sequence number table.
     * Maps domain to sequence number.
     */
    GHashTable                *dom_seqtab;
    /**
     * Current observation domain ID.
     */
    uint32_t                   domain;
    /**
     * Last/next sequence number in current observation domain.
     */
    uint32_t                   sequence;
    /**
     * Pointer to collector that was created with session
     */
    fbCollector_t             *collector;
    /**
     * Buffer instance to write template dynamics to.
     */
    fBuf_t                    *tdyn_buf;
    /**
     * The number of valid pairs in 'tmpl_pair_array'.  Used to free the array
     * when it is empty.
     */
    uint16_t                   tmpl_pair_count;
    /**
     * The TID to use for exporting enterprise-specific IEs (RFC5610) when
     * rfc5610_export is true.  See also rfc5610_int_tid;
     */
    uint16_t                   rfc5610_export_tid;
    /**
     * The TID to use for exporting templateInfo when tmplinfo_export is true.
     * See also tmplinfo_int_tid.
     */
    uint16_t                   tmplinfo_export_tid;
    /**
     * The TID to use for exporting the subTemplateList in the
     * fbTemplateInfoRecord_t that maintains the basicLists' IEs.
     */
    uint16_t                   blinfo_export_tid;
    /**
     * When reading data, the internal TID used to read info-element metadata
     * (RFC5610) records.  See also 'rfc5610_export_tid'.
     */
    uint16_t                   rfc5610_int_tid;
    /**
     * When reading data, the internal TID used to read fbTemplateInfoRecord_t
     * records.  See also 'tmplinfo_export_tid'.
     */
    uint16_t                   tmplinfo_int_tid;
    /**
     * The length of the template in 'largestInternalTemplate'.
     */
    uint16_t                   largestInternalTemplateLength;
    /**
     * Where to begin looking for an unused external template ID when the ID
     * is FB_TID_AUTO.  These begin at FB_TID_MIN_DATA and increment.
     */
    uint16_t                   ext_next_tid;
    /**
     * Where to begin looking for an unused internal template ID when the ID
     * is FB_TID_AUTO.  These begin at UINT16_MAX and decrement.
     */
    uint16_t                   int_next_tid;
    /**
     * If TRUE, options records will be exported for enterprise-specific IEs.
     * The TID used is 'rfc5610_export_tid'.
     */
    gboolean                   rfc5610_export;
    /**
     * If TRUE, options records will be exported for templates that have
     * associated metadata.  The TID used is 'tmplinfo_export_tid' for the
     * main template and 'blinfo_export_tid' for the subTemplateList it
     * contains.
     */
    gboolean                   tmplinfo_export;
    /**
     * Flag to set when an internal template is added or removed. The flag is
     * unset when SetInternalTemplate is called. This ensures the internal
     * template set to the most up-to-date template
     */
    gboolean                   intTmplTableChanged;
    /**
     * Flag to set when an external template is added or removed. The flag is
     * unset when SetExportTemplate is called. This ensures the external
     * template set to the most up-to-date template
     */
    gboolean                   extTmplTableChanged;
    /**
     * Flag to set when you want fbSessionLookupTemplatePair() always
     * to return the template ID it was given.
     */
    gboolean                   tmpl_pair_disabled;
};



fbSession_t *
fbSessionAlloc(
    fbInfoModel_t  *model)
{
    fbSession_t *session = NULL;

    /* Create a new session */
    session = g_slice_new0(fbSession_t);

    /* Store reference to information model */
    session->model = model;

    /* Allocate internal template table */
    session->int_ttab = g_hash_table_new(g_direct_hash, g_direct_equal);

    /* Reset session externals (will allocate domain template tables, etc.) */
    fbSessionResetExternal(session);

    session->tmpl_pair_array = NULL;
    session->tmpl_pair_count = 0;
    session->new_template_callback = NULL;

    session->int_next_tid = UINT16_MAX;
    session->ext_next_tid = FB_TID_MIN_DATA;

    /* All done */
    return session;
}


uint16_t
fbSessionSetMetadataExportElements(
    fbSession_t  *session,
    gboolean      enabled,
    uint16_t      tid,
    GError      **err)
{
    fbTemplate_t *ie_type_tmpl;

    session->rfc5610_export = enabled;

    /* external */
    ie_type_tmpl = fbInfoElementAllocTypeTemplate(session->model, err);
    if (!ie_type_tmpl) {
        return 0;
    }

    session->rfc5610_export_tid =
        fbSessionAddTemplatesForExport(session, tid, ie_type_tmpl, NULL, err);
    if (!session->rfc5610_export_tid) {
        fbTemplateFreeUnused(ie_type_tmpl);
        return 0;
    }

    return session->rfc5610_export_tid;
}


/*
 *  Writes the info element type metadata for all non-standard elements in the
 *  info model to the template dynamics buffer of 'session'.
 *
 *  Does NOT restore the internal and external templates of the fBuf.
 */
static gboolean
fbSessionWriteTypeMetadata(
    fbSession_t  *session,
    GError      **err)
{
    fbInfoModelIter_t      iter;
    const fbInfoElement_t *ie = NULL;
    GError *child_err = NULL;

    if (!session->rfc5610_export) {
        return TRUE;
    }
    if (!session->rfc5610_export_tid) {
#if FB_DEBUG_MD
        fprintf(stderr, "Called fbSessionWriteTypeMetadata on a session"
                " that is not configured for element metadata\n");
#endif
        return TRUE;
    }

#if FB_DEBUG_MD
    fprintf(stderr, "Exporting info element metadata; template %#x\n",
            session->rfc5610_export_tid);
#endif
    if (!fbSessionExportTemplate(
            session, session->rfc5610_export_tid, &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "fbSessionExportTemplate(%#x) failed: %s\n",
                session->rfc5610_export_tid, child_err->message);
#endif
        g_propagate_error(err, child_err);
        return FALSE;
    }

#if FB_DEBUG_MD
    fprintf(stderr, "Writing info element metadata tmpl %#x to fbuf %p\n",
            session->rfc5610_export_tid, (void *)session->tdyn_buf);
#endif

    if (!fBufSetInternalTemplate(
            session->tdyn_buf, session->rfc5610_export_tid, &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "fBufSetInternalTemplate(%#x) failed: %s\n",
                session->rfc5610_export_tid, child_err->message);
#endif
        g_propagate_error(err, child_err);
        return FALSE;
    }

    if (!fBufSetExportTemplate(
            session->tdyn_buf, session->rfc5610_export_tid, &child_err))
    {
        if (g_error_matches(child_err, FB_ERROR_DOMAIN, FB_ERROR_TMPL)) {
#if FB_DEBUG_MD
            fprintf(stderr,
                    "Ignoring failed fBufSetExternalTemplate(%#x): %s\n",
                    session->rfc5610_export_tid, child_err->message);
#endif
            g_clear_error(&child_err);
            return TRUE;
        }
#if FB_DEBUG_MD
        fprintf(stderr, "fBufSetExternalTemplate(%#x) failed: %s\n",
                session->rfc5610_export_tid, child_err->message);
#endif
        g_propagate_error(err, child_err);
        return FALSE;
    }

    fbInfoModelIterInit(&iter, session->model);

    while ( (ie = fbInfoModelIterNext(&iter)) ) {
        /* No need to send metadata for IEs in the standard info model */
        if (ie->ent == 0 || ie->ent == FB_IE_PEN_REVERSE) {
            continue;
        }

        if (!fbInfoElementWriteOptionsRecord(
                session->tdyn_buf, ie, session->rfc5610_export_tid,
                session->rfc5610_export_tid, &child_err))
        {
#if FB_DEBUG_MD
            fprintf(stderr, "fbInfoElementWriteOptionsRecord(%s) failed: %s\n",
                    ie->name, child_err->message);
#endif
            g_propagate_error(err, child_err);
            return FALSE;
        }
    }

#if FB_DEBUG_MD
    fprintf(stderr, "Finished writing info element type metadata\n");
#endif
    return TRUE;
}


uint16_t
fbSessionGetTemplatePath(
    const fbSession_t       *session,
    const fbTemplateInfo_t  *mdInfo,
    uint16_t                 path[],
    uint16_t                 path_size,
    GError                 **err)
{
    uint16_t parent = 0;
    uint16_t depth  = 0;

    g_assert(session);
    g_assert(mdInfo);

    if (NULL == path) {
        path_size = 0;
    }
    /* record this template ID in position 0 */
    if (depth < path_size) {
        path[depth] = fbTemplateInfoGetTemplateId(mdInfo);
    }
    ++depth;

    for (;; ) {
        parent = fbTemplateInfoGetParentTid(mdInfo);
        if (FB_TMPL_MD_LEVEL_NA == parent) {
            if (depth != 1) {
                g_warning("mdInfo for TID %u has unexpected parentTid %u",
                          fbTemplateInfoGetTemplateId(mdInfo),
                          fbTemplateInfoGetParentTid(mdInfo));
                return 0;
            }
            break;
        }
        if (FB_TMPL_MD_LEVEL_0 == parent) {
            /* at top-level; add a 0 for the top-level template unless the
             * initial `mdInfo` was a top-level.  This should only happen if a
             * first-level child uses an actual top-level template id instead
             * of FB_TMPL_MD_LEVEL_1 as its parent */
            if (depth != 1) {
                if (depth < path_size) {
                    path[depth] = 0;
                }
                ++depth;
            }
            break;
        }
        if (FB_TMPL_MD_LEVEL_1 == parent) {
            /* at a first-level template; add a 0 for the top-level
             * template */
            if (depth < path_size) {
                path[depth] = 0;
            }
            ++depth;
            break;
        }
        if (depth < path_size) {
            path[depth] = parent;
        }
        ++depth;
        mdInfo = fbSessionGetTemplateInfo(session, parent);
        if (NULL == mdInfo) {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                        "Cannot find metadata for template %#06x", parent);
            return 0;
        }
    }

    if (path && depth > path_size) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                    "Template path depth %u exceeds available space (%u)",
                    depth, path_size);
        return 0;
    }

    return depth;
}


const fbTemplateInfo_t *
fbSessionGetTemplateInfo(
    const fbSession_t  *session,
    uint16_t            tid)
{
    return g_hash_table_lookup(session->mdInfoTab, GUINT_TO_POINTER(tid));
}

uint16_t
fbSessionSetMetadataExportTemplates(
    fbSession_t  *session,
    gboolean      enabled,
    uint16_t      tmplinfo_export_tid,
    uint16_t      blinfo_export_tid,
    GError      **err)
{
    fbTemplate_t *tmplinfo_export_tmpl   = NULL;
    fbTemplate_t *blinfo_export_tmpl     = NULL;

    session->tmplinfo_export = enabled;

    /* allocate templates */
    if (!fbTemplateAllocTemplateInfoTemplates(
            session->model, &tmplinfo_export_tmpl, &blinfo_export_tmpl, err))
    {
        return 0;
    }

    /* the basicList contents template */
    session->blinfo_export_tid = fbSessionAddTemplatesForExport(
        session, blinfo_export_tid, blinfo_export_tmpl, NULL, err);
    if (!session->blinfo_export_tid) {
        fbTemplateFreeUnused(blinfo_export_tmpl);
        return 0;
    }

    /* the template description metadata template */
    session->tmplinfo_export_tid = fbSessionAddTemplatesForExport(
        session, tmplinfo_export_tid, tmplinfo_export_tmpl, NULL, err);
    if (!session->tmplinfo_export_tid) {
        fbTemplateFreeUnused(tmplinfo_export_tmpl);
        return 0;
    }

    return session->tmplinfo_export_tid;
}

/**
 *  Writes the metadata for 'tmpl' to 'session'.  Does nothing if 'mdInfo' is
 *  NULL or the session is not configured to export template metadata.
 */
static gboolean
fbSessionWriteOneTemplateInfo(
    fbSession_t             *session,
    const fbTemplateInfo_t  *mdInfo,
    GError                 **err)
{
    uint16_t      int_tid, ext_tid;
    fbTemplateInfoRecord_t mdRec;
    fbTemplate_t *blinfo_export_tmpl;
    GError       *child_err = NULL;
    gboolean      ret = TRUE;

    if (!mdInfo || !session->tmplinfo_export) {
        return TRUE;
    }

#if FB_DEBUG_MD
    fprintf(stderr, "writing metadata %p (%#06x %s) to fBuf %p\n",
            (void *)mdInfo, fbTemplateInfoGetTemplateId(mdInfo),
            fbTemplateInfoGetName(mdInfo),
            (void *)session->tdyn_buf);
#endif /* if FB_DEBUG_MD */

    blinfo_export_tmpl = fbSessionGetTemplate(
        session, TRUE, session->blinfo_export_tid, NULL);
    if (!blinfo_export_tmpl) {
#if FB_DEBUG_MD
        fprintf(stderr, "fbSessionGetTemplate(%#x) failed: %s\n",
                session->blinfo_export_tid, (err ? (*err)->message : ""));
#endif
        return FALSE;
    }

    int_tid = fBufGetInternalTemplate(session->tdyn_buf);
    ext_tid = fBufGetExportTemplate(session->tdyn_buf);

    if (!fBufSetInternalTemplate(
            session->tdyn_buf, session->tmplinfo_export_tid, err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "fBufSetInternalTemplate(%#x) failed: %s\n",
                session->tmplinfo_export_tid, (err ? (*err)->message : ""));
#endif
        ret = FALSE;
        goto RESET_INT;
    }
    if (!fBufSetExportTemplate(
            session->tdyn_buf, session->tmplinfo_export_tid, err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "fBufSetExportTemplate(%#x) failed: %s\n",
                session->tmplinfo_export_tid, (err ? (*err)->message : ""));
#endif
        ret = FALSE;
        goto RESET_EXT;
    }

    fbTemplateInfoFillRecord(
        mdInfo, &mdRec, blinfo_export_tmpl, session->blinfo_export_tid);
    ret = fBufAppend(session->tdyn_buf, (uint8_t *)&mdRec, sizeof(mdRec), err);
    fbTemplateInfoRecordClear(&mdRec);

#if FB_DEBUG_MD
    if (!ret) {
        fprintf(stderr, "fBufAppend(%p) failed: %s\n",
                (void *)&mdRec, (err ? (*err)->message : ""));
    }
#endif /* if FB_DEBUG_MD */
    /* if (!fBufEmit(session->tdyn_buf, err)) goto END; */

  RESET_EXT:
    if (ext_tid
        && !fBufSetExportTemplate(session->tdyn_buf, ext_tid, &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "restore fBufSetExportTemplate(%#x) failed: %s\n",
                ext_tid, child_err->message);
#endif
        if (!ret || g_error_matches(child_err, FB_ERROR_DOMAIN,
                                    FB_ERROR_TMPL))
        {
            /* ignore this error if either an error is already set or
             * this error is an unknown template error */
            g_clear_error(&child_err);
        } else {
            g_propagate_error(err, child_err);
            child_err = NULL;
            ret = FALSE;
        }
    }
  RESET_INT:
    if (int_tid
        && !fBufSetInternalTemplate(session->tdyn_buf, int_tid, &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "restore fBufSetInternalTemplate(%#x) failed: %s\n",
                int_tid, child_err->message);
#endif
        if (!ret || g_error_matches(child_err, FB_ERROR_DOMAIN,
                                    FB_ERROR_TMPL))
        {
            g_clear_error(&child_err);
        } else {
            g_propagate_error(err, child_err);
            child_err = NULL;
            ret = FALSE;
        }
    }

    return ret;
}


uint16_t
fbSessionAddTemplatesForExport(
    fbSession_t       *session,
    uint16_t           tid,
    fbTemplate_t      *tmpl,
    fbTemplateInfo_t  *mdInfo,
    GError           **err)
{
    fbInfoElementSpec_t spec = {"paddingOctets", 0, 0};
    uint16_t            etid, itid;
    fbTemplate_t       *t;

    if (fbTemplateContainsElementByName(tmpl, &spec) == FALSE) {
        t = tmpl;
    } else {
        t = fbTemplateCopy(tmpl, FB_TMPL_COPY_REMOVE_PADDING);
        if (NULL == t) {
            t = tmpl;
        }
    }

    etid = fbSessionAddTemplate(session, FALSE, tid, t, mdInfo, err);
    if (0 == etid) {
        if (t != tmpl) {
            fbTemplateFreeUnused(t);
        }
        return 0;
    }

    itid = fbSessionAddTemplate(session, TRUE, etid, tmpl, NULL, err);
    if (0 == itid) {
        return 0;
    }
    g_assert(etid == itid);

    return etid;
}


fbNewTemplateCallback_fn
fbSessionGetNewTemplateCallback(
    const fbSession_t  *session)
{
    return session->new_template_callback;
}

void
fbSessionAddNewTemplateCallback(
    fbSession_t               *session,
    fbNewTemplateCallback_fn   callback,
    void                      *app_ctx)
{
    session->new_template_callback = callback;
    session->tmpl_app_ctx = app_ctx;
}

void *
fbSessionGetNewTemplateCallbackAppCtx(
    const fbSession_t  *session)
{
    return session->tmpl_app_ctx;
}

void
fbSessionSetCallbackCopyTemplates(
    fbSession_t  *incomingSession,
    fbSession_t  *exportSession)
{
    fbSessionAddNewTemplateCallback(
        incomingSession, fbSessionCopyIncomingTemplatesCallback, exportSession);
}

void
fbSessionCopyIncomingTemplatesCallback(
    fbSession_t           *incomingSession,
    uint16_t               tid,
    fbTemplate_t          *tmpl,
    void                  *exportSession,
    void                 **tmpl_ctx,
    fbTemplateCtxFree_fn  *tmpl_ctx_free_fn)
{
    (void)incomingSession;
    (void)tmpl_ctx;
    (void)tmpl_ctx_free_fn;

    fbSessionAddTemplatesForExport((fbSession_t *)exportSession,
                                   tid, tmpl, NULL, NULL);
}



void
fbSessionAddTemplatePair(
    fbSession_t  *session,
    uint16_t      ext_tid,
    uint16_t      int_tid)
{
    if (ext_tid < FB_TID_MIN_DATA) {
        return;
    }
    if (0 == int_tid) {
        int_tid = TMPL_PAIR_NO_TRANSCODE;
    } else if (int_tid < FB_TID_MIN_DATA) {
        return;
    }

    /* if int_tid is not ext_tid and is not 0, check whether it exists on the
     * session.  if not, do nothing. */
    if ((int_tid != ext_tid) &&
        (int_tid != TMPL_PAIR_NO_TRANSCODE) &&
        !fbSessionGetTemplate(session, TRUE, int_tid, NULL))
    {
        return;
    }

    /* increment the count and create array if needed */
    if (!session->tmpl_pair_array) {
        session->tmpl_pair_array =
            (uint16_t *)g_slice_alloc0(TMPL_PAIR_ARRAY_SIZE);
        session->tmpl_pair_count = 1;
    } else if (0 == session->tmpl_pair_array[ext_tid]) {
        session->tmpl_pair_count++;
    }
    session->tmpl_pair_array[ext_tid] = int_tid;
}


void
fbSessionRemoveTemplatePair(
    fbSession_t  *session,
    uint16_t      ext_tid)
{
    if (!session->tmpl_pair_array || ext_tid < FB_TID_MIN_DATA) {
        return;
    }

    if (session->tmpl_pair_array[ext_tid]) {
        session->tmpl_pair_count--;
        if (!session->tmpl_pair_count) {
            /* this was the last one, free the array */
            g_slice_free1(TMPL_PAIR_ARRAY_SIZE, session->tmpl_pair_array);
            session->tmpl_pair_array = NULL;
            return;
        }
        session->tmpl_pair_array[ext_tid] = 0;
    }
}

uint16_t
fbSessionLookupTemplatePair(
    const fbSession_t  *session,
    uint16_t            ext_tid)
{
    uint16_t int_tid;
    fbSessionGetTemplatePair(session, ext_tid, &int_tid, NULL, NULL, NULL);
    return int_tid;
}

/*
 * fbSessionGetTemplatePair
 *
 *
 *
 * private.h function
 */
gboolean
fbSessionGetTemplatePair(
    const fbSession_t  *session,
    uint16_t            ext_tid,
    uint16_t           *int_tid,
    fbTemplate_t      **ext_tmpl,
    fbTemplate_t      **int_tmpl,
    GError            **err)
{
    /* may not have only one of ext_tmpl or int_tmpl set */
    g_assert((int_tmpl && ext_tmpl) || (!int_tmpl && !ext_tmpl));
    g_assert(int_tid);

    if (ext_tmpl) {
        /* get the external template; fail if not available */
        *ext_tmpl = fbSessionGetTemplate(session, FALSE, ext_tid, err);
        if (NULL == *ext_tmpl) {
            *int_tmpl = NULL;
            *int_tid = 0;
            return FALSE;
        }
    }

    /* if there are no current pairs, just return ext_tid because that means
     * we should decode the entire external template
     */
    if (!session->tmpl_pair_array || session->tmpl_pair_disabled) {
        if (int_tmpl) {
            *int_tmpl = *ext_tmpl;
        }
        *int_tid = ext_tid;
        return TRUE;
    }

    *int_tid = session->tmpl_pair_array[ext_tid];

    if (0 == *int_tid || TMPL_PAIR_NO_TRANSCODE == *int_tid) {
        /* either undefined or explicitly disabled */
        if (int_tmpl) {
            *int_tmpl = NULL;
        }
        *int_tid = 0;
        return TRUE;
    }

    if (int_tmpl) {
        /* check for an internal template having the given ID */
        *int_tmpl = fbSessionGetTemplate(session, TRUE, *int_tid, NULL);
        if (NULL == *int_tmpl) {
            /* not found; if the template IDs are the same, use external
             * template as the internal */
            if (*int_tid == ext_tid) {
                *int_tmpl = *ext_tmpl;
            } else {
                /* different template ID given but no template with that ID */
                g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                            "Cannot find internal template %#06x paired with"
                            " external template %#06x",
                            *int_tid, ext_tid);
                return FALSE;
            }
        }
    }

    return TRUE;
}


/*
 * fbSessionSetTemplatePairsDisabled
 *
 *
 *
 * private.h function
 */
void
fbSessionSetTemplatePairsDisabled(
    fbSession_t  *session,
    gboolean      disabled)
{
    session->tmpl_pair_disabled = disabled;
}


/**
 *  Releases all templates used by the template table 'ttab' and removes them
 *  from the table.  Does not free the table itself.
 */
static void
fbSessionClearTemplateTable(
    fbSession_t  *session,
    GHashTable   *ttab)
{
    GHashTableIter iter;
    fbTemplate_t  *tmpl;

    g_hash_table_iter_init(&iter, ttab);
    if (session->tdyn_buf) {
        while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&tmpl)) {
            fBufRemoveTemplateTcplan(session->tdyn_buf, tmpl);
            fbTemplateRelease(tmpl);
            /* fbSessionRemoveTemplatePair(session, tid); */
        }
    } else {
        while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&tmpl)) {
            fbTemplateRelease(tmpl);
            /* fbSessionRemoveTemplatePair(session, tid); */
        }
    }
    g_hash_table_remove_all(ttab);
}


void
fbSessionResetExternal(
    fbSession_t  *session)
{
    GHashTableIter iter;
    GHashTable    *ttab;

    session->ext_ttab = NULL;
    session->mdInfoTab = NULL;

    /* Clear out the domain template table; create if needed */
    if (NULL == session->dom_ttab) {
        /* Allocate domain template table */
        session->dom_ttab = g_hash_table_new_full(
            NULL, NULL, NULL, (GDestroyNotify)g_hash_table_destroy);
    } else {
        /* Release all the external templates (will free unless shared) */
        g_hash_table_iter_init(&iter, session->dom_ttab);
        while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&ttab)) {
            fbSessionClearTemplateTable(session, ttab);
        }
        /* Empty out the domain template table, which frees the hash tables it
         * contains */
        g_hash_table_remove_all(session->dom_ttab);
    }

    /* Clear out the domain template metadata table; create if needed */
    if (NULL == session->dom_mdInfoTab) {
        session->dom_mdInfoTab = g_hash_table_new_full(
            NULL, NULL, NULL, (GDestroyNotify)g_hash_table_destroy);
    } else {
        g_hash_table_remove_all(session->dom_mdInfoTab);
    }

    /* Clear out the sequence number table; create if needed */
    if (NULL == session->dom_seqtab) {
        session->dom_seqtab = g_hash_table_new(NULL, NULL);
    } else {
        g_hash_table_remove_all(session->dom_seqtab);
    }

    /* Zero sequence number and domain */
    session->sequence = 0;
    session->domain = 0;

    /* Set domain to 0 (initializes external template table) */
    fbSessionSetDomain(session, 0);
}

void
fbSessionFree(
    fbSession_t  *session)
{
    if (NULL == session) {
        return;
    }
    fbSessionResetExternal(session);
    fbSessionClearTemplateTable(session, session->int_ttab);
    g_hash_table_destroy(session->int_ttab);
    g_hash_table_destroy(session->dom_ttab);
    g_hash_table_destroy(session->dom_mdInfoTab);
    if (session->dom_seqtab) {
        g_hash_table_destroy(session->dom_seqtab);
    }
    g_slice_free1(TMPL_PAIR_ARRAY_SIZE, session->tmpl_pair_array);
    session->tmpl_pair_array = NULL;
    g_slice_free(fbSession_t, session);
}

void
fbSessionSetDomain(
    fbSession_t  *session,
    uint32_t      domain)
{
    /* Short-circuit identical domain if not initializing */
    if (session->ext_ttab && (domain == session->domain)) {return;}

    /* Update external template table; create if necessary. */
    session->ext_ttab = g_hash_table_lookup(session->dom_ttab,
                                            GUINT_TO_POINTER(domain));
    if (!session->ext_ttab) {
        session->ext_ttab = g_hash_table_new(NULL, NULL);
        g_hash_table_insert(session->dom_ttab, GUINT_TO_POINTER(domain),
                            session->ext_ttab);
    }

    /* Update template metadata table; create if necessary. */
    session->mdInfoTab = g_hash_table_lookup(session->dom_mdInfoTab,
                                             GUINT_TO_POINTER(domain));
    if (!session->mdInfoTab) {
        session->mdInfoTab = g_hash_table_new_full(
            NULL, NULL, NULL, (GDestroyNotify)fbTemplateInfoFree);
        g_hash_table_insert(session->dom_mdInfoTab, GUINT_TO_POINTER(domain),
                            session->mdInfoTab);
    }

    /* Stash current sequence number */
    g_hash_table_insert(session->dom_seqtab,
                        GUINT_TO_POINTER(session->domain),
                        GUINT_TO_POINTER(session->sequence));

    /* Get new sequence number */
    session->sequence = GPOINTER_TO_UINT(
        g_hash_table_lookup(session->dom_seqtab, GUINT_TO_POINTER(domain)));

    /* Stash new domain */
    session->domain = domain;
}

uint32_t
fbSessionGetDomain(
    const fbSession_t  *session)
{
    return session->domain;
}

/**
 * Find an unused template ID in the template table of `session`.
 */
static uint16_t
fbSessionFindUnusedTemplateId(
    fbSession_t  *session,
    gboolean      internal)
{
    /* Select a template table to add the template to */
    GHashTable *ttab = internal ? session->int_ttab : session->ext_ttab;
    uint16_t    tid = 0;

    if (internal) {
        if (g_hash_table_size(ttab) == (UINT16_MAX - FB_TID_MIN_DATA)) {
            return 0;
        }
        tid = session->int_next_tid;
        while (g_hash_table_lookup(ttab, GUINT_TO_POINTER((unsigned int)tid))) {
            tid = ((tid > FB_TID_MIN_DATA) ? (tid - 1) : UINT16_MAX);
        }
        session->int_next_tid =
            ((tid > FB_TID_MIN_DATA) ? (tid - 1) : UINT16_MAX);
    } else {
        if (g_hash_table_size(ttab) == (UINT16_MAX - FB_TID_MIN_DATA)) {
            return 0;
        }
        tid = session->ext_next_tid;
        while (g_hash_table_lookup(ttab, GUINT_TO_POINTER((unsigned int)tid))) {
            tid = ((tid < UINT16_MAX) ? (tid + 1) : FB_TID_MIN_DATA);
        }
        session->ext_next_tid =
            ((tid < UINT16_MAX) ? (tid + 1) : FB_TID_MIN_DATA);
    }
    return tid;
}


/*
 *  Internal.  Adds metadata to the session.
 */
void
fbSessionSaveTemplateInfo(
    fbSession_t       *session,
    fbTemplateInfo_t  *mdInfo)
{
    gpointer key;
    gpointer found;

    key = GUINT_TO_POINTER(fbTemplateInfoGetTemplateId(mdInfo));
    found = g_hash_table_lookup(session->mdInfoTab, key);
    if (found != (gpointer)mdInfo) {
        g_hash_table_insert(session->mdInfoTab, key, mdInfo);
    }
}


/*
 *  fbSessionAddTemplate
 *
 *
 *
 */
uint16_t
fbSessionAddTemplate(
    fbSession_t       *session,
    gboolean           internal,
    uint16_t           tid,
    fbTemplate_t      *tmpl,
    fbTemplateInfo_t  *mdInfo,
    GError           **err)
{
    GHashTable *ttab;
    GError     *child_err = NULL;

    g_assert(tmpl);
    g_assert(tid == FB_TID_AUTO || tid >= FB_TID_MIN_DATA);

    if (mdInfo) {
        if (internal) {
            mdInfo = NULL;
        } else {
            if (!fbTemplateInfoGetName(mdInfo)) {
                g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_SETUP,
                            "TemplateInfo must include a name");
                return 0;
            }
            if (fbTemplateInfoGetTemplateId(mdInfo)) {
                g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_SETUP,
                            "TemplateInfo is already used by another"
                            " Session or Template");
                return 0;
            }
        }
    }

    /* ensure we do not clobbber special internal templates */
    if (internal && tid != FB_TID_AUTO) {
        fbTemplate_t *move_tmpl;
        uint16_t     *move_tid = NULL;

        if (tid == session->rfc5610_int_tid) {
            move_tid = &session->rfc5610_int_tid;
        } else if (tid == session->tmplinfo_int_tid) {
            move_tid = &session->tmplinfo_int_tid;
        }
        if (move_tid) {
            move_tmpl = fbSessionGetTemplate(session, TRUE, *move_tid, NULL);
            if (NULL == move_tmpl) {
                *move_tid = 0;
            } else {
                /* only error here should be full template table */
                *move_tid = fbSessionAddTemplate(session, TRUE, FB_TID_AUTO,
                                                 move_tmpl, NULL, err);
                if (!*move_tid) {
                    return 0;
                }
                /* do not remove the other copy of the special template here;
                * just rely on the call to fbSessionRemoveTemplate() below */
            }
        }
    }

    /* Select a template table to add the template to */
    ttab = internal ? session->int_ttab : session->ext_ttab;

    /* Handle an auto-template-id or an illegal ID */
    if (tid < FB_TID_MIN_DATA) {
        if (tid != FB_TID_AUTO) {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                        "Illegal template id %d", tid);
            return 0;
        }
        tid = fbSessionFindUnusedTemplateId(session, internal);
        if (0 == tid) {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                        "Template table is full, no IDs left");
            return 0;
        }
    }

    /* Revoke old template, ignoring missing template error. */
    if (!fbSessionRemoveTemplate(session, internal, tid, &child_err)) {
        if (g_error_matches(child_err, FB_ERROR_DOMAIN, FB_ERROR_TMPL)) {
            g_clear_error(&child_err);
        } else {
            g_propagate_error(err, child_err);
            return 0;
        }
    }

    if (mdInfo) {
        fbTemplateInfoSetTemplateId(mdInfo, tid);
        fbSessionSaveTemplateInfo(session, mdInfo);
    }

    /* Write template to dynamics buffer */
    if (fBufGetExporter(session->tdyn_buf) && !internal) {
        if (!fbSessionWriteOneTemplateInfo(session, mdInfo, &child_err)) {
            if (g_error_matches(child_err, FB_ERROR_DOMAIN, FB_ERROR_TMPL)) {
                g_clear_error(&child_err);
            } else {
                g_propagate_error(err, child_err);
                return 0;
            }
        }
        if (!fBufAppendTemplate(session->tdyn_buf, tid, tmpl, FALSE, err)) {
            return 0;
        }
    }

    /* Insert template into table */
    g_hash_table_insert(ttab, GUINT_TO_POINTER((unsigned int)tid), tmpl);

    if (internal &&
        tmpl->ie_internal_len > session->largestInternalTemplateLength &&
        session->largestInternalTemplateLength > 0)
    {
        session->largestInternalTemplate = tmpl;
        session->largestInternalTemplateLength = tmpl->ie_internal_len;
    }

    if (internal) {
        session->intTmplTableChanged = TRUE;
    } else {
        session->extTmplTableChanged = TRUE;
    }

    fbTemplateRetain(tmpl);

    return tid;
}


gboolean
fbSessionRemoveTemplate(
    fbSession_t  *session,
    gboolean      internal,
    uint16_t      tid,
    GError      **err)
{
    GHashTable   *ttab = NULL;
    fbTemplate_t *tmpl = NULL;
    gboolean      ok = TRUE;

    /* Select a template table to remove the template from */
    ttab = internal ? session->int_ttab : session->ext_ttab;

    /* Get the template to remove */
    tmpl = fbSessionGetTemplate(session, internal, tid, err);
    if (!tmpl) {return FALSE;}

    /* Write template withdrawal to dynamics buffer */
    if (fBufGetExporter(session->tdyn_buf) && !internal) {
        ok = fBufAppendTemplate(session->tdyn_buf, tid, tmpl, TRUE, err);
    }

    /* Remove template */
    g_hash_table_remove(ttab, GUINT_TO_POINTER((unsigned int)tid));

    /* FIXME: Remove the metadata also? */
#if 0
    /* Remove metadata */
    if (!internal) {
        g_hash_table_remove(session->mdInfoTab,
                            GUINT_TO_POINTER((unsigned int)tid));
    }
#endif  /* 0 */

    if (internal) {
        session->intTmplTableChanged = TRUE;
    } else {
        session->extTmplTableChanged = TRUE;
    }

    fbSessionRemoveTemplatePair(session, tid);

    fBufRemoveTemplateTcplan(session->tdyn_buf, tmpl);

    if (internal) {
        if (session->largestInternalTemplate == tmpl) {
            session->largestInternalTemplate = NULL;
            session->largestInternalTemplateLength = 0;
        }
        if (tid == session->rfc5610_int_tid) {
            session->rfc5610_int_tid = 0;
        } else if (tid == session->tmplinfo_int_tid) {
            session->tmplinfo_int_tid = 0;
        }
    }

    fbTemplateRelease(tmpl);

    return ok;
}

fbTemplate_t *
fbSessionGetTemplate(
    const fbSession_t  *session,
    gboolean            internal,
    uint16_t            tid,
    GError            **err)
{
    GHashTable   *ttab;
    fbTemplate_t *tmpl;

    /* Select a template table to get the template from */
    ttab = internal ? session->int_ttab : session->ext_ttab;

    tmpl = g_hash_table_lookup(ttab, GUINT_TO_POINTER((unsigned int)tid));
    /* Check for missing template */
    if (!tmpl) {
        if (internal) {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                        "Missing internal template %#06hx",
                        tid);
        } else {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                        "Missing external template %#10x:%04hx",
                        session->domain, tid);
        }
    }

    return tmpl;
}

gboolean
fbSessionExportTemplate(
    fbSession_t  *session,
    uint16_t      tid,
    GError      **err)
{
    const fbTemplate_t     *tmpl;
    const fbTemplateInfo_t *mdInfo;
    GError *child_err = NULL;

    /* short-circuit on no template dynamics buffer */
    if (!fBufGetExporter(session->tdyn_buf)) {
        return TRUE;
    }

    /* look up template */
    if (!(tmpl = fbSessionGetTemplate(session, FALSE, tid, err))) {
        return FALSE;
    }

    mdInfo = fbSessionGetTemplateInfo(session, tid);
    if (!fbSessionWriteOneTemplateInfo(session, mdInfo, &child_err)) {
        if (g_error_matches(child_err, FB_ERROR_DOMAIN, FB_ERROR_TMPL)) {
            g_clear_error(&child_err);
        } else {
            g_propagate_error(err, child_err);
            return FALSE;
        }
    }

    /* export it */
    return fBufAppendTemplate(session->tdyn_buf, tid, tmpl, FALSE, err);
}


/**
 *  Does nothing if the session is not configured for metadata export.
 *  Otherwise, writes the Templates for TemplateInfo and BasicListInfo, and
 *  then writes TemplateInfoRecords for all TemplateInfo that have been added
 *  to the Session in the current domain.
 */
static gboolean
fbSessionWriteTemplateInfo(
    fbSession_t  *session,
    GError      **err)
{
    const fbTemplateInfo_t *mdInfo;
    fbTemplateInfoRecord_t  mdRec;
    GHashTableIter          iter;
    fbTemplate_t           *blinfo_export_tmpl;
    gpointer key;
    uint16_t tid;
    gboolean ret;
    GError  *child_err = NULL;

    if (!session->tmplinfo_export) {
        return TRUE;
    }
    if (!session->mdInfoTab) {
        return TRUE;
    }
    if (!session->tmplinfo_export_tid || !session->blinfo_export_tid) {
#if FB_DEBUG_MD
        fprintf(stderr, "Called fbSessionWriteTemplateInfo on a session"
                " that is not configured for template metadata\n");
#endif
        return TRUE;
    }

    blinfo_export_tmpl = fbSessionGetTemplate(
        session, TRUE, session->blinfo_export_tid, &child_err);
    if (NULL == blinfo_export_tmpl) {
#if FB_DEBUG_MD
        fprintf(stderr, "Failed to find basicListInfo template %#x: %s\n",
                session->blinfo_export_tid, child_err->message);
#endif
        g_propagate_error(err, child_err);
        return FALSE;
    }

#if FB_DEBUG_MD
    fprintf(stderr, "Exporting template metadata; template %#x\n",
            session->tmplinfo_export_tid);
#endif
    if (!fbSessionExportTemplate(session, session->tmplinfo_export_tid,
                                 &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "fbSessionExportTemplate(%#x) failed: %s\n",
                session->tmplinfo_export_tid, child_err->message);
#endif
        g_propagate_error(err, child_err);
        return FALSE;
    }

#if FB_DEBUG_MD
    fprintf(stderr, "Exporting template metadata BL; template %#x\n",
            session->blinfo_export_tid);
#endif
    if (!fbSessionExportTemplate(
            session, session->blinfo_export_tid, &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "fbSessionExportTemplate(%#x) failed: %s\n",
                session->blinfo_export_tid, child_err->message);
#endif
        g_propagate_error(err, child_err);
        return FALSE;
    }

    if (!fBufSetInternalTemplate(
            session->tdyn_buf, session->tmplinfo_export_tid, &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "fBufSetInternalTemplate(%#x) failed: %s\n",
                session->tmplinfo_export_tid, child_err->message);
#endif
        g_propagate_error(err, child_err);
        return FALSE;
    }

    if (!fBufSetExportTemplate(
            session->tdyn_buf, session->tmplinfo_export_tid, &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "fBufSetExportTemplate(%#x) failed: %s\n",
                session->tmplinfo_export_tid, child_err->message);
#endif
        g_propagate_error(err, child_err);
        return FALSE;
    }

    /* the fbufsetinternal/fbufsetexport template functions can't
     * occur in the GHFunc since pthread_mutex_lock has already been
     * called and fBufSetExportTemplate will call fbSessionGetTemplate
     * which will try to acquire lock */
    g_hash_table_iter_init(&iter, session->mdInfoTab);
    while (g_hash_table_iter_next(&iter, &key, (gpointer *)&mdInfo)
           && fBufGetExporter(session->tdyn_buf))
    {
        tid = (uint16_t)GPOINTER_TO_UINT(key);
        if (tid == session->rfc5610_export_tid
            || tid == session->blinfo_export_tid
            || tid == session->tmplinfo_export_tid)
        {
            continue;
        }
        fbTemplateInfoFillRecord(mdInfo, &mdRec, blinfo_export_tmpl,
                                 session->blinfo_export_tid);
        ret = fBufAppend(session->tdyn_buf, (uint8_t *)&mdRec,
                         sizeof(mdRec), &child_err);
        fbTemplateInfoRecordClear(&mdRec);
        if (!ret) {
#if FB_DEBUG_MD
            fprintf(stderr, "fBufAppend failed for metadata: %s\n",
                    child_err->message ? child_err->message : "");
#endif
            g_propagate_error(err, child_err);
            return FALSE;
        }
    }

    return TRUE;
}

gboolean
fbSessionExportTemplates(
    fbSession_t  *session,
    GError      **err)
{
    const fbTemplate_t *tmpl;
    GHashTableIter      iter;
    uint16_t            tid;
    uint16_t            int_tid;
    uint16_t            ext_tid;
    gpointer            key;
    GError             *child_err = NULL;
    gboolean            ret = FALSE;

    /* require an exporter*/
    if (!fBufGetExporter(session->tdyn_buf)) {
        return TRUE;
    }

    /* should always be true, but just in case */
    if (!session->ext_ttab) {
        return TRUE;
    }

    int_tid = fBufGetInternalTemplate(session->tdyn_buf);
    ext_tid = fBufGetExportTemplate(session->tdyn_buf);

    if (!fbSessionWriteTypeMetadata(session, err)) {
        goto END;
    }

    if (!fbSessionWriteTemplateInfo(session, err)) {
        goto END;
    }

    /* iterate over external table */
    g_hash_table_iter_init(&iter, session->ext_ttab);
    while (g_hash_table_iter_next(&iter, &key, (gpointer *)&tmpl)
           && fBufGetExporter(session->tdyn_buf))
    {
        tid = (uint16_t)GPOINTER_TO_UINT(key);
        if (tid == session->rfc5610_export_tid
            || tid == session->blinfo_export_tid
            || tid == session->tmplinfo_export_tid)
        {
            continue;
        }
        if (!fBufAppendTemplate(session->tdyn_buf, tid, tmpl, FALSE, err)) {
            goto END;
        }
    }

    ret = TRUE;

  END:
    if (int_tid
        && !fBufSetInternalTemplate(session->tdyn_buf, int_tid, &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "restore fBufSetInternalTemplate(%#x) failed: %s\n",
                int_tid, child_err->message);
#endif
        g_clear_error(&child_err);
    }
    if (ext_tid
        && !fBufSetExportTemplate(session->tdyn_buf, ext_tid, &child_err))
    {
#if FB_DEBUG_MD
        fprintf(stderr, "restore fBufSetExportTemplate(%#x) failed: %s\n",
                ext_tid, child_err->message);
#endif
        g_clear_error(&child_err);
    }
    return ret;
}

static void
fbSessionCloneOneTemplate(
    void          *vtid,
    fbTemplate_t  *tmpl,
    fbSession_t   *session)
{
    uint16_t tid = (uint16_t)GPOINTER_TO_UINT(vtid);
    GError  *err = NULL;

    if (!fbSessionAddTemplate(session, TRUE, tid, tmpl, NULL, &err)) {
        g_warning("Session clone internal template copy failed: %s",
                  err->message);
        g_clear_error(&err);
    }
}

fbSession_t *
fbSessionClone(
    const fbSession_t  *base)
{
    fbSession_t *session = NULL;

    /* Create a new session using the information model from the base */
    session = fbSessionAlloc(base->model);

    /* Add each internal template from the base session to the new session */
    g_hash_table_foreach(base->int_ttab,
                         (GHFunc)fbSessionCloneOneTemplate, session);

    /* Need to copy over callbacks because in the UDP case we won't have
     * access to the session until after we call fBufNext and by that
     * time it's too late and we've already missed some templates */
    session->new_template_callback = base->new_template_callback;
    session->tmpl_app_ctx = base->tmpl_app_ctx;

    /* copy collector reference */
    session->collector = base->collector;

    /* Return the new session */
    return session;
}

uint32_t
fbSessionGetSequence(
    const fbSession_t  *session)
{
    return session->sequence;
}

/* private.h function */
void
fbSessionSetSequence(
    fbSession_t  *session,
    uint32_t      sequence)
{
    session->sequence = sequence;
}

void
fbSessionSetTemplateBuffer(
    fbSession_t  *session,
    fBuf_t       *fbuf)
{
    session->tdyn_buf = fbuf;
}

fbInfoModel_t *
fbSessionGetInfoModel(
    const fbSession_t  *session)
{
    return session->model;
}

void
fbSessionClearIntTmplTableFlag(
    fbSession_t  *session)
{
    session->intTmplTableChanged = FALSE;
}

void
fbSessionClearExtTmplTableFlag(
    fbSession_t  *session)
{
    session->extTmplTableChanged = FALSE;
}

int
fbSessionIntTmplTableFlagIsSet(
    fbSession_t  *session)
{
    return session->intTmplTableChanged;
}

int
fbSessionExtTmplTableFlagIsSet(
    fbSession_t  *session)
{
    return session->extTmplTableChanged;
}

void
fbSessionSetCollector(
    fbSession_t    *session,
    fbCollector_t  *collector)
{
    session->collector = collector;
}

fbCollector_t *
fbSessionGetCollector(
    const fbSession_t  *session)
{
    return session->collector;
}


static void
fbSessionSetLargestInternalTemplateLen(
    fbSession_t  *session)
{
    GHashTableIter      iter;
    const fbTemplate_t *tmpl;

    if (!session || !session->int_ttab) {
        return;
    }

    g_hash_table_iter_init(&iter, session->int_ttab);
    while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&tmpl)) {
        if (tmpl->ie_internal_len > session->largestInternalTemplateLength) {
            session->largestInternalTemplateLength  = tmpl->ie_internal_len;
            session->largestInternalTemplate        = tmpl;
        }
    }
}


uint16_t
fbSessionGetLargestInternalTemplateSize(
    fbSession_t  *session)
{
    if (!session->largestInternalTemplateLength) {
        fbSessionSetLargestInternalTemplateLen(session);
    }

    return session->largestInternalTemplateLength;
}


/*
 *  fbSessionAddInternalRfc5610Template
 *
 *
 *
 *  private.h function
 */
gboolean
fbSessionAddInternalRfc5610Template(
    fbSession_t  *session,
    GError      **err)
{
    fbTemplate_t *tmpl;

    if (session->rfc5610_int_tid) {
        return TRUE;
    }

    tmpl = fbInfoElementAllocTypeTemplate2(session->model, TRUE, err);
    if (!tmpl) {
        return FALSE;
    }
    session->rfc5610_int_tid = fbSessionAddTemplate(session, TRUE, FB_TID_AUTO,
                                                    tmpl, NULL, err);
    if (session->rfc5610_int_tid == 0) {
        fbTemplateFreeUnused(tmpl);
        return FALSE;
    }

    return TRUE;
}


/*
 *  fbSessionGetInternalRfc5610Template
 *
 *
 *
 *  private.h function
 */
fbTemplate_t *
fbSessionGetInternalRfc5610Template(
    fbSession_t  *session,
    uint16_t     *tid,
    GError      **err)
{
    if (tid) {
        *tid = session->rfc5610_int_tid;
    }
    return fbSessionGetTemplate(session, TRUE, session->rfc5610_int_tid, err);
}


/*
 *  fbSessionAddInternalTemplateInfoTemplates
 *
 *
 *
 *  private.h function
 */
gboolean
fbSessionAddInternalTemplateInfoTemplate(
    fbSession_t  *session,
    GError      **err)
{
    fbTemplate_t *tmpl;

    if (session->tmplinfo_int_tid) {
        return TRUE;
    }

    if (!fbTemplateAllocTemplateInfoTemplates(
            session->model, &tmpl, NULL, err))
    {
        return FALSE;
    }
    session->tmplinfo_int_tid = fbSessionAddTemplate(
        session, TRUE, FB_TID_AUTO, tmpl, NULL, err);
    if (session->tmplinfo_int_tid == 0) {
        fbTemplateFreeUnused(tmpl);
        return FALSE;
    }

    return TRUE;
}

/*
 *  fbSessionGetInternalRfc5610Template
 *
 *
 *
 *  private.h function
 */
fbTemplate_t *
fbSessionGetInternalTemplateInfoTemplate(
    fbSession_t  *session,
    uint16_t     *tmplInfoTid,
    GError      **err)
{
    if (tmplInfoTid) {
        *tmplInfoTid = session->tmplinfo_int_tid;
    }
    return fbSessionGetTemplate(session, TRUE, session->tmplinfo_int_tid, err);
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
