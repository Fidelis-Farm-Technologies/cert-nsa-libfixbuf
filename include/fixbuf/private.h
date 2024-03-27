/*
 *  Copyright 2006-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file private.h
 *  fixbuf IPFIX protocol library private interface.
 *
 *  These calls and structures are intended for the use of libfixbuf modules,
 *  and as such are not documented or guaranteed to remain stable in any
 *  way. Applications using these calls and structures may have to be modified
 *  to track changes to this interface across minor version releases of
 *  fixbuf.
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Brian Trammell
 *  ------------------------------------------------------------------------
 */

#ifndef _FB_PRIVATE_H_
#define _FB_PRIVATE_H_
#include <fixbuf/public.h>


/** define the bit in ID's that marks the Enterprise ID's */
#define IPFIX_ENTERPRISE_BIT    0x8000

/** definition of the max-size of an fbuf_t buffer, or the
 *  default/only size */
#define FB_MSGLEN_MAX       65535

/** size of the buffer for OpenSSL error messages */
#define FB_SSL_ERR_BUFSIZ   512

/**
 * An UDP Connection specifier.  These are managed by the
 * collector.  The collector creates one fbUDPConnSpec_t
 * per "UDP session." A UDP session is defined by a unique
 * IP and observation domain."
 */
typedef struct fbUDPConnSpec_st {
    /** pointer to the session for this peer address */
    fbSession_t              *session;
    /** application context. Created and owned by the app */
    void                     *ctx;
    /** key to this conn spec */
    union {
        struct sockaddr       so;
        struct sockaddr_in    ip4;
        struct sockaddr_in6   ip6;
    } peer;
    /** size of peer */
    size_t                    peerlen;
    /** link to next one in list */
    struct fbUDPConnSpec_st  *next;
    /** doubly linked to timeout faster */
    struct fbUDPConnSpec_st  *prev;
    /** last seen time */
    time_t                    last_seen;
    /** with peer address this is the key */
    uint32_t                  obdomain;
    /** reject flag */
    gboolean                  reject;
} fbUDPConnSpec_t;


/**
 *  TemplateInfo options record structure
 *
 *  This is the form the fbTemplateInfo_t has on the wire.
 *
 */
/* TODO...add a way to say if this field is this value, it highlights these
 * records...thus removing app label spec...or screw it and leave it specific
 */
typedef struct fbTemplateInfoRecord_st {
    /** Template ID */
    uint16_t              tid;
    uint16_t              appLabel;
    uint16_t              parentTid;
    uint8_t               padding[2];
    /** Template name */
    fbVarfield_t          name;
    /** Template description (optional) */
    fbVarfield_t          description;
    /** List of PEN, IE num pairs */
    fbSubTemplateList_t   blInfoList;
} fbTemplateInfoRecord_t;


/**
 *  fbElementPositions_t supports a variable-sized array that is used to
 *  store the index positions of a particular element in Template.
 */
typedef struct fbElementPositions_st {
    /** The length of the array of positions. */
    uint16_t   count;
    /** The list of positions; resized as needed. */
    uint16_t  *positions;
} fbElementPositions_t;

/**
 * An IPFIX template or options template structure. Part of the private
 * interface. Applications should use the fbTemplate calls defined in public.h
 * to manipulate templates instead of accessing this structure directly.
 */
struct fbTemplate_st {
    /** Information model (for looking up information elements by spec) */
    fbInfoModel_t         *model;
    /** Ordered array of pointers to information elements in this template. */
    fbTemplateField_t    **ie_ary;
    /** Map of information element to index in ie_ary. */
    GHashTable            *indices;
    /** Field offset cache. For internal use by the transcoder. */
    uint16_t              *off_cache;
    /** Reference count */
    int                    ref_count;
    /** Count of information elements in template. */
    uint16_t               ie_count;
    /**
     * Count of scope information elements in template. If scope_count
     * is greater than 0, this template is an options template.
     */
    uint16_t               scope_count;
    /**
     * Total length of information elements in records described by
     * this template. If the is_varlen flag is set, this represents the
     * minimum length of the information elements in the record
     * (i.e. with each variable length IE's length set to 0).
     */
    uint16_t               ie_len;
    /**
     * Total length required to store this template in memory.
     * Uses sizeof(fbVarfield_t), sizeof(fbBasicList_t), etc instead of 0
     * as done with ie_len.
     */
    uint16_t               ie_internal_len;
    /**
     * Total length of the template record or options template record
     * defining this template. Used during template input and output.
     */
    uint16_t               tmpl_len;
    /**
     * TRUE if this template contains any variable length IEs.
     */
    gboolean               is_varlen;
    /**
     * TRUE if this template contains any structured data (lists).
     */
    gboolean               contains_list;
    /**
     * TRUE if this template has been activated (is no longer mutable).
     */
    gboolean               active;
    /**
     * TRUE if any field was created using an fbInfoElementSpec_t with a
     * defaulted length
     */
    gboolean               default_length;
    /**
     * Template context. Created and owned by the application
     * when the listener calls the fbNewTemplateCallback_fn.
     */
    void                  *tmpl_ctx;
    /**
     * Callback to free the ctx pointer when template is freed
     */
    fbTemplateCtxFree_fn   ctx_free;
    /**
     * The application's Context pointer for the ctx_free function.
     */
    void                  *app_ctx;

    /**
     * Index positions of the FB_BASIC_LIST elements.
     */
    fbElementPositions_t   bl;
    /**
     * Index positions of the FB_SUB_TMPL_LIST elements.
     */
    fbElementPositions_t   stl;
    /**
     * Index positions of the FB_SUB_TMPL_MULTI_LIST elements.
     */
    fbElementPositions_t   stml;
};

/**
 * fBufRewind
 *
 * @param fbuf
 *
 */
void
fBufRewind(
    fBuf_t  *fbuf);

/**
 * fBufAppendTemplate
 *
 * @param fbuf
 * @param tmpl_id
 * @param tmpl
 * @param revoked
 * @param err
 *
 * @return TRUE on success, FALSE on error
 */
gboolean
fBufAppendTemplate(
    fBuf_t              *fbuf,
    uint16_t             tmpl_id,
    const fbTemplate_t  *tmpl,
    gboolean             revoked,
    GError             **err);

/**
 * fBufRemoveTemplateTcplan
 *
 *
 */
void
fBufRemoveTemplateTcplan(
    fBuf_t              *fbuf,
    const fbTemplate_t  *tmpl);

/**
 * fBufSetSession
 *
 */
void
fBufSetSession(
    fBuf_t       *fbuf,
    fbSession_t  *session);

/**
 * fBufGetExportTemplate
 *
 */
uint16_t
fBufGetExportTemplate(
    const fBuf_t  *fbuf);


/**
 * fBufGetInternalTemplate
 *
 */
uint16_t
fBufGetInternalTemplate(
    const fBuf_t  *fbuf);

/**
 * fbInfoElementHash
 *
 * @param ie
 *
 *
 */
uint32_t
fbInfoElementHash(
    const fbInfoElement_t  *ie);

/**
 * fbInfoElementEqual
 *
 * @param a
 * @param b
 *
 *
 */
gboolean
fbInfoElementEqual(
    const fbInfoElement_t  *a,
    const fbInfoElement_t  *b);

/**
 * fbInfoElementDebug
 *
 * @param tmpl
 * @param ie
 *
 */
void
fbInfoElementDebug(
    gboolean                tmpl,
    const fbInfoElement_t  *ie);

/**
 * fbInfoModelGetElement
 *
 * @param model
 * @param ex_ie
 *
 */
const fbInfoElement_t *
fbInfoModelGetElement(
    const fbInfoModel_t    *model,
    const fbInfoElement_t  *ex_ie);

/**
 * fbInfoElementCopyToTemplate
 *
 * Finds `ex_ie` in the info model (using its element id and PEN) and updates
 * `tmpl_ie` to point at it using the length specified in `ex_ie`.  If `ex_ie`
 * is not in the information model, adds it as an alien element.
 *
 * Returns TRUE unless the length in `ex_ie` is invalid for the element's
 * type.
 *
 * This is only to be used when reading templates from a stream.
 *
 * @param model
 * @param ex_ie
 * @param tmpl_ie
 * @param err
 *
 */
gboolean
fbInfoElementCopyToTemplate(
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ex_ie,
    fbTemplateField_t      *tmpl_ie,
    GError                **err);

/**
 * fbInfoElementCopyToTemplateByName
 *
 * Finds the element named `spec->name` in the info model and updates
 * `tmpl_ie` to point at it using the length `spec->len_override`.
 *
 * Returns FALSE if the info model does not contain an element named
 * `spec->name`.  Also returns FALSE if `spec->len_override` is invalid for
 * the element's type.
 *
 * @param model
 * @param spec
 * @param tmpl_ie
 * @param err
 *
 *
 */
gboolean
fbInfoElementCopyToTemplateByName(
    const fbInfoModel_t        *model,
    const fbInfoElementSpec_t  *spec,
    fbTemplateField_t          *tmpl_ie,
    GError                    **err);

/**
 * fbInfoElementCopyToTemplateByIdent
 *
 * Finds the element have the specified `spec->enterprise_id` and
 * `spec->element_id` in the info model and updates `tmpl_ie` to point at it
 * using the length `spec->len_override`.
 *
 * Returns FALSE if the info model does not contain an element with the
 * specified ID pair.  Also returns FALSE if `spec->len_override` is invalid
 * for the element's type.
 *
 * @param model
 * @param spec
 * @param tmpl_ie
 * @param err
 *
 *
 */
gboolean
fbInfoElementCopyToTemplateByIdent(
    const fbInfoModel_t          *model,
    const fbInfoElementSpecId_t  *spec,
    fbTemplateField_t            *tmpl_ie,
    GError                      **err);

/**
 * fbInfoModelAddAlienElement
 *
 * @param model
 * @param ex_ie
 * @return info_elemnt
 *
 */
const fbInfoElement_t *
fbInfoModelAddAlienElement(
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ex_ie);

/**
 * fbInfoElementAllocTypeTemplate2
 *
 * @param model
 * @param internal
 * @param err
 * @return tmpl
 */
fbTemplate_t *
fbInfoElementAllocTypeTemplate2(
    fbInfoModel_t  *model,
    gboolean        internal,
    GError        **err);

/**
 * fbInfoModelIsTemplateElementType
 *
 * @param tmpl
 */
gboolean
fbInfoModelIsTemplateElementType(
    const fbTemplate_t  *tmpl);


/**
 * fbTemplateRetain
 *
 * @param tmpl
 *
 *
 */
void
fbTemplateRetain(
    fbTemplate_t  *tmpl);

/**
 * fbTemplateRelease
 *
 *
 * @param tmpl
 *
 */
void
fbTemplateRelease(
    fbTemplate_t  *tmpl);

/**
 * fbTemplateDebug
 *
 * @param label
 * @param tid
 * @param tmpl
 *
 */
void
fbTemplateDebug(
    const char          *label,
    uint16_t             tid,
    const fbTemplate_t  *tmpl);

/**
 * fbTemplateInfoCreateFromRecord
 *
 * Allocates and initializes a TemplateInfo from data read
 * from a collector.
 *
 * @param mdRec
 * @param mdRecVersion
 * @param err
 *
 */
fbTemplateInfo_t *
fbTemplateInfoCreateFromRecord(
    const fbTemplateInfoRecord_t  *mdRec,
    unsigned int                   mdRecVersion,
    GError                       **err);

/**
 * fbTemplateInfoFillRecord
 *
 * @param mdInfo
 * @param mdRec
 * @param stlTemplate
 * @param stlTid
 *
 */
void
fbTemplateInfoFillRecord(
    const fbTemplateInfo_t  *mdInfo,
    fbTemplateInfoRecord_t  *mdRec,
    const fbTemplate_t      *stlTemplate,
    uint16_t                 stlTid);

/**
 * fbTemplateInfoSetTemplateId
 *
 * @param mdInfo
 * @param tid
 */
void
fbTemplateInfoSetTemplateId(
    fbTemplateInfo_t  *mdInfo,
    uint16_t           tid);

/**
 * fbTemplateInfoRecordInit
 *
 * Initalizes a fbTemplateInfoRecord_t
 *
 * @param mdRec
 */
void
fbTemplateInfoRecordInit(
    fbTemplateInfoRecord_t  *mdRec);

/**
 *
 * Clears any list data used by a fbTemplateInfoRecord_t
 *
 * @param mdRec
 */
void
fbTemplateInfoRecordClear(
    fbTemplateInfoRecord_t  *mdRec);

/**
 * Allocates new Templates to describe records that hold TemplateInfo.
 *
 * @param model             The info model
 * @param tmplinfo_v3_tmpl If non-NULL, its referent is set to the internal
 * template that matches metadata records created by fixbuf v3.0.0
 * @param bl_ie_metadata_tmpl If non-NULL, its referent is set to the
 * template that matches basicList-contents records used the
 * subTemplateList on the metadata record created by fixbuf v3.0.0
 * @param err
 * @returns TRUE on success; FALSE on error
 */
gboolean
fbTemplateAllocTemplateInfoTemplates(
    fbInfoModel_t  *model,
    fbTemplate_t  **tmplinfo_v3_tmpl,
    fbTemplate_t  **bl_ie_metadata_tmpl,
    GError        **err);

/**
 * Returns the callback function for a given session.
 *
 * @param session
 * @return the callback function variable in the session
 */
fbNewTemplateCallback_fn
fbSessionGetNewTemplateCallback(
    const fbSession_t  *session);

/**
 * Returns the callback function's application context for a given
 * session
 *
 * @param session
 * @return the Application context pointer added to the session
 */
void *
fbSessionGetNewTemplateCallbackAppCtx(
    const fbSession_t  *session);

/**
 * fbSessionClone
 *
 * @param base
 *
 */
fbSession_t *
fbSessionClone(
    const fbSession_t  *base);

/**
 * fbSessionGetSequence
 *
 * @param session
 *
 *
 */
uint32_t
fbSessionGetSequence(
    const fbSession_t  *session);

/**
 * fbSessionSetSequence
 *
 * @param session
 * @param sequence
 *
 */
void
fbSessionSetSequence(
    fbSession_t  *session,
    uint32_t      sequence);

/**
 * fbSessionSetTemplateBuffer
 *
 * @param session
 * @param fbuf
 *
 */
void
fbSessionSetTemplateBuffer(
    fbSession_t  *session,
    fBuf_t       *fbuf);

/**
 *  Checks for a template pair and finds the associated external and internal
 *  templates.
 *
 *  Acts like fbSessionLookupTemplatePair() when `ext_tmpl` and `int_tmpl` are
 *  NULL.  Otherwise:
 *
 *  Sets the referent of `ext_tmpl` to the external template associated with
 *  `ext_tid`.  If no external template exists, sets the referents of all
 *  output parameters to zero or NULL and returns FALSE.
 *
 *  If no template pairs are defined or they are disabled
 *  (fbSessionSetTemplatePairsDisabled()), sets the referents of `int_tmpl`
 *  and `int_tid` to the external values and returns TRUE.
 *
 *  If there is no matching internal ID paired with `ext_tid` or it was
 *  explicitly disabled, sets the referents of `int_tid` and `int_tmpl` to 0
 *  and NULL and returns TRUE.
 *
 *  If there is a template pair, checks for an interal template having the
 *  paired ID.  If found, sets the referent of `int_tmpl` to the value, sets
 *  the referent of `int_tid` to the paired ID, and returns TRUE.
 *
 *  If no internal template having the paired ID exists and the paired ID
 *  equals `ext_tid`, sets the referents of `int_tmpl` and `int_tid` to the
 *  external values and returns TRUE.
 *
 *  Otherwise, sets an error and returns FALSE.
 *
 *  @param session
 *  @param ext_tid
 *  @param int_tid
 *  @param ext_tmpl
 *  @param int_tmpl
 *  @param err
 */
gboolean
fbSessionGetTemplatePair(
    const fbSession_t  *session,
    uint16_t            ext_tid,
    uint16_t           *int_tid,
    fbTemplate_t      **ext_tmpl,
    fbTemplate_t      **int_tmpl,
    GError            **err);

/**
 *  Sets the flag on a Session that says whether the template-pairs lookup
 *  table should be ignored.  If `disabled` is TRUE, the template-pairs table
 *  is ignored and fbSessionLookupTemplatePair() always returns the template
 *  ID it is given.  If `disabled` is FALSE, the template-pairs table is used.
 *
 *  @param session
 *  @param disabled
 */
void
fbSessionSetTemplatePairsDisabled(
    fbSession_t  *session,
    gboolean      disabled);

/**
 * fbSessionSetCollector
 *
 * @param session
 * @param collector
 *
 */
void
fbSessionSetCollector(
    fbSession_t    *session,
    fbCollector_t  *collector);

/**
 * fbSessionClearIntTmplTableFlag
 *
 * @param session
 *
 */
void
fbSessionClearIntTmplTableFlag(
    fbSession_t  *session);

/**
 * fbSessionClearExtTmplTableFlag
 *
 * @param session
 *
 */
void
fbSessionClearExtTmplTableFlag(
    fbSession_t  *session);

/**
 * fbSessionIntTmplTableFlagIsSet
 *
 * @param session
 *
 */
int
fbSessionIntTmplTableFlagIsSet(
    fbSession_t  *session);

/**
 * fbSessionExtTmplTableFlagIsSet
 *
 * @param session
 *
 */
int
fbSessionExtTmplTableFlagIsSet(
    fbSession_t  *session);

/**
 * fbSessionSaveTemplateInfo
 *
 * @param session
 * @param mdInfo
 *
 */
void
fbSessionSaveTemplateInfo(
    fbSession_t       *session,
    fbTemplateInfo_t  *mdInfo);

/**
 * fbSessionAddInternalRfc5610Template
 *
 * Adds an internal template for reading RFC5610 records (Info Element
 * Type definitions) unless it already exists.  Use
 * fbSessionGetInternalRfc5610Template() to get this template.
 *
 * @param session
 * @param err
 */
gboolean
fbSessionAddInternalRfc5610Template(
    fbSession_t  *session,
    GError      **err);

/**
 * fbSessionGetInternalRfc5610Template
 *
 * Gets the internal template for reading RFC5610 records (InfoElement
 * Type defintions) set by fbSessionAddInternalRfc5610Template().  If
 * `tid` is not NULL, it referent is set to the Template ID.
 *
 * @param session
 * @param tid
 * @param err
 */
fbTemplate_t *
fbSessionGetInternalRfc5610Template(
    fbSession_t  *session,
    uint16_t     *tid,
    GError      **err);

/**
 * fbSessionAddInternalTemplateInfoTemplate
 *
 * Adds an internal template for reading TemplateInfo records unless it
 * already exists.  Use fbSessionGetInternalTemplateInfoTemplate() to get this
 * Template.
 *
 * Does not allocate an internal template for the STL that the
 * fbTemplateInfo_t contains since it is not needed: When decoding the
 * TemplateInfo, any existing template-pairs are ignored and the external
 * template is used as the internal template.
 *
 * @param session
 * @param err
 */
gboolean
fbSessionAddInternalTemplateInfoTemplate(
    fbSession_t  *session,
    GError      **err);

/**
 * fbSessionGetInternalTemplateInfoTemplate
 *
 * Gets the internal template for reading TemplateInfo records.  If `tid` is
 * not NULL, its referent is to the Template ID.  This Template is created by
 * fbSessionAddInternalTemplateInfoTemplate().
 *
 * @param session
 * @param tid
 * @param err
 */
fbTemplate_t *
fbSessionGetInternalTemplateInfoTemplate(
    fbSession_t  *session,
    uint16_t     *tid,
    GError      **err);

/**
 * fbConnSpecLookupAI
 *
 * @param spec
 * @param passive
 * @param err
 *
 */
gboolean
fbConnSpecLookupAI(
    fbConnSpec_t  *spec,
    gboolean       passive,
    GError       **err);

/**
 * fbConnSpecInitTLS
 *
 * @param spec
 * @param passive
 * @param err
 *
 */
gboolean
fbConnSpecInitTLS(
    fbConnSpec_t  *spec,
    gboolean       passive,
    GError       **err);

/**
 * fbConnSpecCopy
 *
 * @param spec
 *
 *
 */
fbConnSpec_t *
fbConnSpecCopy(
    const fbConnSpec_t  *spec);

/**
 * fbConnSpecFree
 *
 * @param spec
 *
 *
 */
void
fbConnSpecFree(
    fbConnSpec_t  *spec);

/**
 * fbExporterGetMTU
 *
 * @param exporter
 *
 *
 */
uint16_t
fbExporterGetMTU(
    const fbExporter_t  *exporter);

/**
 * fbExportMessage
 *
 * @param exporter
 * @param msgbase
 * @param msglen
 * @param err
 *
 */
gboolean
fbExportMessage(
    fbExporter_t  *exporter,
    uint8_t       *msgbase,
    size_t         msglen,
    GError       **err);

/**
 * fbExporterFree
 *
 * @param exporter
 *
 *
 */
void
fbExporterFree(
    fbExporter_t  *exporter);

/**
 * fbCollectorRemoveListenerLastBuf
 *
 * @param fbuf
 * @param collector
 *
 */
void
fbCollectorRemoveListenerLastBuf(
    fBuf_t         *fbuf,
    fbCollector_t  *collector);

/**
 * fbCollectorAllocSocket
 *
 * @param listener
 * @param ctx
 * @param fd
 * @param peer
 * @param peerlen
 * @param err
 *
 */
fbCollector_t *
fbCollectorAllocSocket(
    fbListener_t     *listener,
    void             *ctx,
    int               fd,
    struct sockaddr  *peer,
    size_t            peerlen,
    GError          **err);

/**
 * fbCollectorAllocTLS
 *
 * @param listener
 * @param ctx
 * @param fd
 * @param peer
 * @param peerlen
 * @param err
 *
 */
fbCollector_t *
fbCollectorAllocTLS(
    fbListener_t     *listener,
    void             *ctx,
    int               fd,
    struct sockaddr  *peer,
    size_t            peerlen,
    GError          **err);

/**
 * fbCollectMessage
 *
 * @param collector
 * @param msgbase
 * @param msglen
 * @param err
 *
 */
gboolean
fbCollectMessage(
    fbCollector_t  *collector,
    uint8_t        *msgbase,
    size_t         *msglen,
    GError        **err);

/**
 * fbCollectorGetFD
 *
 * @param collector
 *
 *
 */
int
fbCollectorGetFD(
    const fbCollector_t  *collector);

/**
 * fbCollectorSetFD
 *
 *
 *
 */
void
fbCollectorSetFD(
    fbCollector_t  *collector,
    int             fd);

/**
 * fbCollectorFree
 *
 * @param collector
 *
 *
 */
void
fbCollectorFree(
    fbCollector_t  *collector);

/**
 * fbCollectorHasTranslator
 *
 * @param collector
 *
 *
 */
gboolean
fbCollectorHasTranslator(
    fbCollector_t  *collector);


/**
 * fbCollectMessageBuffer
 *
 * used for applications that manage their own connection, file reading, etc.
 *
 * @param hdr
 * @param b_len
 * @param m_len
 * @param err
 *
 * @return TRUE/FALSE
 *
 */
gboolean
fbCollectMessageBuffer(
    uint8_t  *hdr,
    size_t    b_len,
    size_t   *m_len,
    GError  **err);

/**
 * fbListenerAppFree
 *
 * @param listener
 * @param ctx
 *
 */
void
fbListenerAppFree(
    fbListener_t  *listener,
    void          *ctx);

/**
 * fbListenerRemoveLastBuf
 *
 * @param fbuf
 * @param listener
 *
 */
void
fbListenerRemoveLastBuf(
    fBuf_t        *fbuf,
    fbListener_t  *listener);

/**
 * fbListenerRemove
 *
 * @param listener
 * @param fd
 *
 */
void
fbListenerRemove(
    fbListener_t  *listener,
    int            fd);

/**
 * fbListenerGetConnSpec
 *
 * @param listener
 *
 *
 */
fbConnSpec_t *
fbListenerGetConnSpec(
    const fbListener_t  *listener);

/**
 * Interrupt the socket for a given collector to stop it from reading
 * more data
 *
 * @param collector pointer to the collector to stop reading from
 */
void
fbCollectorInterruptSocket(
    fbCollector_t  *collector);

/**
 * call appinit from UDP
 *
 */
gboolean
fbListenerCallAppInit(
    fbListener_t     *listener,
    fbUDPConnSpec_t  *spec,
    GError          **err);

/**
 * Set the session on the fbuf and listener.
 *
 */

fbSession_t *
fbListenerSetPeerSession(
    fbListener_t  *listener,
    fbSession_t   *session);


#endif  /* _FB_PRIVATE_H_ */

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
