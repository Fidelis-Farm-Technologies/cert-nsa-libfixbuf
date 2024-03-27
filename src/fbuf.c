/*
 *  Copyright 2006-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file fbuf.c
 *  IPFIX Message buffer implementation
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Brian Trammell, Dan Ruef, Emily Ecoff
 *  ------------------------------------------------------------------------
 */

#define _FIXBUF_SOURCE_
#include <fixbuf/private.h>


#define FB_MTU_MIN              32
#define FB_TCPLAN_NULL          -1

/* Debugger switches. We'll want to stick these in autoinc at some point. */
#define FB_DEBUG_TC         0
#define FB_DEBUG_TMPL       0
#define FB_DEBUG_WR         0
#define FB_DEBUG_RD         0
#define FB_DEBUG_LWR        0
#define FB_DEBUG_LRD        0

/* The number of seconds between Jan 1, 1900 (the NTP epoch in NTP era 0) and
 * Jan 1, 1970 (the UNIX epoch) */
#define NTP_EPOCH_TO_UNIX_EPOCH INT64_C(0x83AA7E80)

/* Define ntohll() if that declaration was not found by configure. */
#if defined(HAVE_DECL_NTOHLL) && !HAVE_DECL_NTOHLL
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define  ntohll(x_)                                                     \
    ((((uint64_t)ntohl((uint32_t)((x_) & UINT64_C(0xffffffff)))) << 32) \
     | ntohl((uint32_t)((x_) >> 32)))
#else
#define  ntohll(x_)   (x_)
#endif  /* G_BYTE_ORDER */
#endif  /* HAVE_NTOHLL */


typedef struct fbTranscodePlan_st {
    /* source template */
    const fbTemplate_t *s_tmpl;
    /* destination template */
    const fbTemplate_t *d_tmpl;
    /* source index array: for each field in `d_tmpl` the index of that field
     * in `s_tmpl` or FB_TCPLAN_NULL if not present */
    int32_t            *si;
} fbTranscodePlan_t;

typedef struct fbDLL_st fbDLL_t;
struct fbDLL_st {
    fbDLL_t  *next;
    fbDLL_t  *prev;
};

typedef struct fbTCPlanEntry_st fbTCPlanEntry_t;
struct fbTCPlanEntry_st {
    fbTCPlanEntry_t    *next;
    fbTCPlanEntry_t    *prev;
    fbTranscodePlan_t  *tcplan;
};

/* typedef struct fBuf_st fBuf_t;  // public.h */
struct fBuf_st {
    /** Transport session. Contains template and sequence number state. */
    fbSession_t      *session;
    /** Exporter. Writes messages to a remote endpoint on flush. */
    fbExporter_t     *exporter;
    /** Collector. Reads messages from a remote endpoint on demand. */
    fbCollector_t    *collector;
    /** Cached transcoder plan */
    fbTCPlanEntry_t  *latestTcplan;
    /** Current internal template. */
    fbTemplate_t     *int_tmpl;
    /** Current external template. */
    fbTemplate_t     *ext_tmpl;
    /** Current internal template ID. */
    uint16_t          int_tid;
    /** Current external template ID. */
    uint16_t          ext_tid;
    /** Current special set ID. */
    uint16_t          spec_tid;
    /** Whether reading of RFC5610 (info element type records) is enabled. */
    gboolean          auto_rfc5610;
    /** Whether reading of template metadata is enabled */
    gboolean          auto_tmplInfo;
    /** Automatic read/write next message mode flag */
    gboolean          auto_next_msg;
    /** Export time in seconds since 0UTC 1 Jan 1970 */
    uint32_t          extime;
    /** Record counter. */
    uint32_t          rc;
    /** length of buffer passed from app */
    size_t            buflen;
    /**
     * Current position pointer.
     * Pointer to the next byte in the buffer to be written or read.
     */
    uint8_t          *cp;
    /**
     * Pointer to first byte in the buffer in the current message.
     * NULL if there is no current message.
     */
    uint8_t          *msgbase;
    /**
     * Message end position pointer.
     * Pointer to first byte in the buffer after the current message.
     */
    uint8_t          *mep;
    /**
     * Pointer to first byte in the buffer in the current set.
     * NULL if there is no current set.
     */
    uint8_t          *setbase;
    /**
     * Set end position pointer.
     * Valid only after a call to fBufNextSetHeader() (called by fBufNext()).
     */
    uint8_t          *sep;
    /** Message buffer. */
    uint8_t           buf[FB_MSGLEN_MAX + 1];
};


static void
fbRecordFillValue(
    const fbTemplateField_t  *field,
    const uint8_t            *buf,
    fbRecordValue_t          *value);


/*==================================================================
 *
 * Doubly Linked List Functions
 *
 *==================================================================*/

/**
 * detachHeadOfDLL
 *
 * takes the head off of the dynamic linked list
 *
 * @param head
 * @param tail
 * @param toRemove
 *
 *
 */
static void
detachHeadOfDLL(
    fbDLL_t **head,
    fbDLL_t **tail,
    fbDLL_t **toRemove)
{
    /*  assign the out pointer to the head */
    *toRemove = *head;
    /*  move the head pointer to pointer to the next element*/
    *head = (*head)->next;

    /*  if the new head's not NULL, set its prev to NULL */
    if (*head) {
        (*head)->prev = NULL;
    } else {
        /*  if it's NULL, it means there are no more elements, if
         *  there's a tail pointer, set it to NULL too */
        if (tail) {
            *tail = NULL;
        }
    }
}

/**
 * attachHeadToDLL
 *
 * puts a new element to the head of the dynamic linked list
 *
 * @param head
 * @param tail
 * @param newEntry
 *
 */
static void
attachHeadToDLL(
    fbDLL_t **head,
    fbDLL_t **tail,
    fbDLL_t  *newEntry)
{
    /*  if this is NOT the first entry in the list */
    if (*head) {
        /*  typical linked list attachements */
        newEntry->next = *head;
        newEntry->prev = NULL;
        (*head)->prev = newEntry;
        *head = newEntry;
    } else {
        /*  the new entry is the only entry now, set head to it */
        *head = newEntry;
        newEntry->prev = NULL;
        newEntry->next = NULL;
        /*  if we're keeping track of tail, assign that too */
        if (tail) {
            *tail = newEntry;
        }
    }
}

/**
 * moveThisEntryToHeadOfDLL
 *
 * moves an entry within the dynamically linked list to the head of the list
 *
 * @param head - the head of the dynamic linked list
 * @param tail - unused
 * @param thisEntry - list element to move to the head
 *
 */
static void
moveThisEntryToHeadOfDLL(
    fbDLL_t **head,
    fbDLL_t **tail __attribute__((unused)),
    fbDLL_t  *thisEntry)
{
    if (thisEntry == *head) {
        return;
    }

    if (thisEntry->prev) {
        thisEntry->prev->next = thisEntry->next;
    }

    if (thisEntry->next) {
        thisEntry->next->prev = thisEntry->prev;
    }

    thisEntry->prev = NULL;
    thisEntry->next = *head;
    (*head)->prev = thisEntry;
    *head = thisEntry;
}

/**
 * detachThisEntryOfDLL
 *
 * removes an entry from the dynamically linked list
 *
 * @param head
 * @param tail
 * @param entry
 *
 */
static void
detachThisEntryOfDLL(
    fbDLL_t **head,
    fbDLL_t **tail,
    fbDLL_t  *entry)
{
    /*  entry already points to the entry to remove, so we're good
     *  there */
    /*  if it's NOT the head of the list, patch up entry->prev */
    if (entry->prev != NULL) {
        entry->prev->next = entry->next;
    } else {
        /*  if it's the head, reassign the head */
        *head = entry->next;
    }
    /*  if it's NOT the tail of the list, patch up entry->next */
    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    } else {
        /*  it is the last entry in the list, if we're tracking the
         *  tail, reassign */
        if (tail) {
            *tail = entry->prev;
        }
    }

    /*  finish detaching by setting the next and prev pointers to
     *  null */
    entry->prev = NULL;
    entry->next = NULL;
}

/*==================================================================
 *
 * Debugger Functions
 *
 *==================================================================*/

#define FB_REM_MSG(_fbuf_) (_fbuf_->mep - _fbuf_->cp)

#define FB_REM_SET(_fbuf_) (_fbuf_->sep - _fbuf_->cp)

#if FB_DEBUG_WR || FB_DEBUG_RD || FB_DEBUG_TC

static uint32_t
fBufDebugHexLine(
    GString     *str,
    const char  *lpfx,
    uint8_t     *cp,
    uint32_t     lineoff,
    uint32_t     buflen)
{
    uint32_t cwr = 0, twr = 0;

    /* stubbornly refuse to print nothing */
    if (!buflen) {return 0;}

    /* print line header */
    g_string_append_printf(str, "%s %04x:", lpfx, lineoff);

    /* print hex characters */
    for (twr = 0; twr < 16; twr++) {
        if (buflen) {
            g_string_append_printf(str, " %02hhx", cp[twr]);
            cwr++; buflen--;
        } else {
            g_string_append(str, "   ");
        }
    }

    /* print characters */
    g_string_append_c(str, ' ');
    for (twr = 0; twr < cwr; twr++) {
        if (cp[twr] > 32 && cp[twr] < 128) {
            g_string_append_c(str, cp[twr]);
        } else {
            g_string_append_c(str, '.');
        }
    }
    g_string_append_c(str, '\n');

    return cwr;
}

static void
fBufDebugHex(
    const char  *lpfx,
    uint8_t     *buf,
    uint32_t     len)
{
    GString *str = g_string_new("");
    uint32_t cwr = 0, lineoff = 0;

    do {
        cwr = fBufDebugHexLine(str, lpfx, buf, lineoff, len);
        buf += cwr; len -= cwr; lineoff += cwr;
    } while (cwr == 16);

    fprintf(stderr, "%s", str->str);
    g_string_free(str, TRUE);
}

#endif /* if FB_DEBUG_WR || FB_DEBUG_RD || FB_DEBUG_TC */

#if FB_DEBUG_WR || FB_DEBUG_RD

#if FB_DEBUG_TC

static void
fBufDebugTranscodePlan(
    fbTranscodePlan_t  *tcplan)
{
    int i;

    fprintf(stderr, "transcode plan %p -> %p\n",
            tcplan->s_tmpl, tcplan->d_tmpl);
    for (i = 0; i < tcplan->d_tmpl->ie_count; i++) {
        fprintf(stderr, "\td[%2u]=s[%2d]\n", i, tcplan->si[i]);
    }
}

static void
fBufDebugTranscodeOffsets(
    fbTemplate_t  *tmpl,
    uint16_t      *offsets)
{
    int i;

    fprintf(stderr, "offsets %p\n", tmpl);
    for (i = 0; i < tmpl->ie_count; i++) {
        fprintf(stderr, "\to[%2u]=%4x\n", i, offsets[i]);
    }
}

#endif /* if FB_DEBUG_TC */

static void
fBufDebugBuffer(
    const char  *label,
    fBuf_t      *fbuf,
    size_t       len,
    gboolean     reverse)
{
    uint8_t *xcp = fbuf->cp - len;
    uint8_t *rcp = reverse ? xcp : fbuf->cp;

    fprintf(stderr, "%s len %5lu mp %5u (0x%04x) sp %5u mr %5u sr %5u\n",
            label, len, rcp - fbuf->msgbase, rcp - fbuf->msgbase,
            fbuf->setbase ? (rcp - fbuf->setbase) : 0,
            fbuf->mep - fbuf->cp,
            fbuf->sep ? (fbuf->sep - fbuf->cp) : 0);

    fBufDebugHex(label, rcp, len);
}

#endif /* if FB_DEBUG_WR || FB_DEBUG_RD */

/*==================================================================
 *
 * Transcoder Functions
 *
 *==================================================================*/

static gboolean
fbTranscode(
    fBuf_t    *fbuf,
    gboolean   decode,
    uint8_t   *s_base,
    uint8_t   *d_base,
    size_t    *s_len,
    size_t    *d_len,
    GError   **err);

static gboolean
fbEncodeBasicList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err);

static gboolean
fbDecodeBasicList(
    fbInfoModel_t  *model,
    uint8_t        *src,
    uint8_t       **dst,
    uint32_t       *d_rem,
    fBuf_t         *fbuf,
    GError        **err);

static void *
fbBasicListAllocData(
    fbBasicList_t  *basicList,
    uint16_t        numElements);

static gboolean
fbEncodeSubTemplateList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err);

static gboolean
fbDecodeSubTemplateList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err);

static void *
fbSubTemplateListAllocData(
    fbSubTemplateList_t  *subTemplateList,
    uint16_t              numElements);

static gboolean
fbEncodeSubTemplateMultiList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err);

static gboolean
fbDecodeSubTemplateMultiList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err);

static void *
fbSubTemplateMultiListEntryAllocData(
    fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                        numElements);

static gboolean
fBufCheckTemplateDefaultLength(
    const fbTemplate_t  *int_tmpl,
    uint16_t             int_tid,
    GError             **err);

static gboolean
fBufSetEncodeSubTemplates(
    fBuf_t    *fbuf,
    uint16_t   ext_tid,
    uint16_t   int_tid,
    GError   **err);


#define FB_TC_SBC_OFF(_need_)                                       \
    if (s_rem < (_need_)) {                                         \
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,             \
                    "End of message. "                              \
                    "Underrun on transcode offset calculation "     \
                    "(need %lu bytes, %lu available)",              \
                    (unsigned long)(_need_), (unsigned long)s_rem); \
        goto err;                                                   \
    }

#define FB_TC_DBC_DEST(_need_, _op_, _dest_)                                 \
    if (*d_rem < (_need_)) {                                                 \
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,                      \
                    "End of message. "                                       \
                    "Overrun on %s (need %lu bytes, %lu available)",         \
                    (_op_), (unsigned long)(_need_), (unsigned long)*d_rem); \
        _dest_;                                                              \
    }

#define FB_TC_DBC(_need_, _op_) \
    FB_TC_DBC_DEST((_need_), (_op_), return FALSE)

#define FB_TC_DBC_ERR(_need_, _op_) \
    FB_TC_DBC_DEST((_need_), (_op_), goto err)

/**
 * fbTranscodePlan
 *
 * @param fbuf
 * @param s_tmpl
 * @param d_tmpl
 *
 */
static fbTranscodePlan_t *
fbTranscodePlan(
    fBuf_t        *fbuf,
    fbTemplate_t  *s_tmpl,
    fbTemplate_t  *d_tmpl)
{
    void            *sik, *siv;
    uint32_t         i;
    fbTCPlanEntry_t *entry;
    fbTranscodePlan_t *tcplan;

    /* check to see if plan is cached */
    if (fbuf->latestTcplan) {
        entry = fbuf->latestTcplan;
        while (entry) {
            tcplan = entry->tcplan;
            if (tcplan->s_tmpl == s_tmpl &&
                tcplan->d_tmpl == d_tmpl)
            {
                moveThisEntryToHeadOfDLL(
                    (fbDLL_t **)(void *)&(fbuf->latestTcplan),
                    NULL,
                    (fbDLL_t *)entry);
                return tcplan;
            }
            entry = entry->next;
        }
    }

    entry = g_slice_new0(fbTCPlanEntry_t);

    /* create new transcode plan and cache it */
    entry->tcplan = g_slice_new0(fbTranscodePlan_t);

    tcplan = entry->tcplan;
    /* fill in template refs */
    tcplan->s_tmpl = s_tmpl;
    tcplan->d_tmpl = d_tmpl;

    tcplan->si = g_new0(int32_t, d_tmpl->ie_count);
    /* for each destination element */
    for (i = 0; i < d_tmpl->ie_count; i++) {
        /* find source index */
        if (g_hash_table_lookup_extended(s_tmpl->indices,
                                         d_tmpl->ie_ary[i],
                                         &sik, &siv))
        {
            tcplan->si[i] = GPOINTER_TO_INT(siv);
        } else {
            tcplan->si[i] = FB_TCPLAN_NULL;
        }
    }

    attachHeadToDLL((fbDLL_t **)(void *)&(fbuf->latestTcplan),
                    NULL,
                    (fbDLL_t *)entry);
    return tcplan;
}

/**
 * fbTranscodeFreeVarlenOffsets
 *
 * @param s_tmpl
 * @param offsets
 *
 */
static void
fbTranscodeFreeVarlenOffsets(
    fbTemplate_t  *s_tmpl,
    uint16_t      *offsets)
{
    if (s_tmpl->is_varlen) {g_free(offsets);}
}

/**
 *
 * Macros for decode reading
 */


#ifdef HAVE_ALIGNED_ACCESS_REQUIRED

#define FB_READ_U16(_val_, _ptr_)             \
    {                                         \
        uint16_t _x;                          \
        memcpy(&_x, _ptr_, sizeof(uint16_t)); \
        _val_ = g_ntohs(_x);                  \
    }

#define FB_READ_U32(_val_, _ptr_)             \
    {                                         \
        uint32_t _x;                          \
        memcpy(&_x, _ptr_, sizeof(uint32_t)); \
        _val_ = g_ntohl(_x);                  \
    }

#define FB_WRITE_U16(_ptr_, _val_)            \
    {                                         \
        uint16_t _x = g_htons(_val_);         \
        memcpy(_ptr_, &_x, sizeof(uint16_t)); \
    }

#define FB_WRITE_U32(_ptr_, _val_)            \
    {                                         \
        uint32_t _x = g_htonl(_val_);         \
        memcpy(_ptr_, &_x, sizeof(uint32_t)); \
    }

#else  /* if HAVE_ALIGNED_ACCESS_REQUIRED */

#define FB_READ_U16(_val_, _ptr_)              \
    {                                          \
        _val_ = g_ntohs(*((uint16_t *)_ptr_)); \
    }

#define FB_READ_U32(_val_, _ptr_)              \
    {                                          \
        _val_ = g_ntohl(*((uint32_t *)_ptr_)); \
    }

#define FB_WRITE_U16(_ptr_, _val_) \
    *(uint16_t *)(_ptr_) = g_htons(_val_)

#define FB_WRITE_U32(_ptr_, _val_) \
    *(uint32_t *)(_ptr_) = g_htonl(_val_)

#endif /* if HAVE_ALIGNED_ACCESS_REQUIRED */

#define FB_READ_U8(_val_, _ptr_) \
    _val_ = *(_ptr_)

#define FB_WRITE_U8(_ptr_, _val_) \
    (*(_ptr_)) = _val_

#define FB_READINC_U8(_val_, _ptr_) \
    {                               \
        FB_READ_U8(_val_, _ptr_);   \
        ++(_ptr_);                  \
    }

#define FB_READINC_U16(_val_, _ptr_) \
    {                                \
        FB_READ_U16(_val_, _ptr_);   \
        (_ptr_) += sizeof(uint16_t); \
    }

#define FB_READINC_U32(_val_, _ptr_) \
    {                                \
        FB_READ_U32(_val_, _ptr_);   \
        (_ptr_) += sizeof(uint32_t); \
    }

#define FB_WRITEINC_U8(_ptr_, _val_) \
    {                                \
        FB_WRITE_U8(_ptr_, _val_);   \
        ++(_ptr_);                   \
    }

#define FB_WRITEINC_U16(_ptr_, _val_) \
    {                                 \
        FB_WRITE_U16(_ptr_, _val_);   \
        (_ptr_) += sizeof(uint16_t);  \
    }

#define FB_WRITEINC_U32(_ptr_, _val_) \
    {                                 \
        FB_WRITE_U32(_ptr_, _val_);   \
        (_ptr_) += sizeof(uint32_t);  \
    }

#define FB_READINCREM_U8(_val_, _ptr_, _rem_) \
    {                                         \
        FB_READINC_U8(_val_, _ptr_);          \
        --(_rem_);                            \
    }

#define FB_READINCREM_U16(_val_, _ptr_, _rem_) \
    {                                          \
        FB_READINC_U16(_val_, _ptr_);          \
        (_rem_) -= sizeof(uint16_t);           \
    }

#define FB_READINCREM_U32(_val_, _ptr_, _rem_) \
    {                                          \
        FB_READINC_U32(_val_, _ptr_);          \
        (_rem_) -= sizeof(uint32_t);           \
    }

#define FB_WRITEINCREM_U8(_ptr_, _val_, _rem_) \
    {                                          \
        FB_WRITEINC_U8(_ptr_, _val_);          \
        --(_rem_);                             \
    }

#define FB_WRITEINCREM_U16(_ptr_, _val_, _rem_) \
    {                                           \
        FB_WRITEINC_U16(_ptr_, _val_);          \
        (_rem_) -= sizeof(uint16_t);            \
    }

#define FB_WRITEINCREM_U32(_ptr_, _val_, _rem_) \
    {                                           \
        FB_WRITEINC_U32(_ptr_, _val_);          \
        (_rem_) -= sizeof(uint32_t);            \
    }

#define FB_READ_LIST_LENGTH(_len_, _ptr_) \
    {                                     \
        FB_READINC_U8(_len_, _ptr_);      \
        if ((_len_) == 255) {             \
            FB_READINC_U16(_len_, _ptr_); \
        }                                 \
    }

#define FB_READ_LIST_LENGTH_REM(_len_, _ptr_, _rem_) \
    {                                                \
        FB_READINCREM_U8(_len_, _ptr_, _rem_);       \
        if ((_len_) == 255) {                        \
            FB_READINCREM_U16(_len_, _ptr_, _rem_);  \
        }                                            \
    }


/**
 * fbTranscodeOffsets
 *
 * @param s_tmpl
 * @param s_base
 * @param s_rem
 * @param decode
 * @param offsets_out
 * @param err - glib2 GError structure that returns the message on failure
 *
 * @return
 *
 */
static ssize_t
fbTranscodeOffsets(
    fbTemplate_t  *s_tmpl,
    uint8_t       *s_base,
    uint32_t       s_rem,
    gboolean       decode,
    uint16_t     **offsets_out,
    GError       **err)
{
    fbTemplateField_t *s_ie;
    uint8_t           *sp;
    uint16_t          *offsets;
    uint32_t           s_len, i;

    /* short circuit - return offset cache if present in template */
    if (s_tmpl->off_cache) {
        if (offsets_out) {*offsets_out = s_tmpl->off_cache;}
        return s_tmpl->off_cache[s_tmpl->ie_count];
    }

    /* create new offsets array */
    offsets = g_new0(uint16_t, s_tmpl->ie_count + 1);

    /* populate it */
    if (decode) {
        for (i = 0, sp = s_base; i < s_tmpl->ie_count; i++) {
            offsets[i] = sp - s_base;
            s_ie = s_tmpl->ie_ary[i];
            if (s_ie->len == FB_IE_VARLEN) {
                FB_TC_SBC_OFF((*sp == 255) ? 3 : 1);
                FB_READ_LIST_LENGTH_REM(s_len, sp, s_rem);
                FB_TC_SBC_OFF(s_len);
                sp += s_len; s_rem -= s_len;
            } else {
                FB_TC_SBC_OFF(s_ie->len);
                sp += s_ie->len; s_rem -= s_ie->len;
            }
        }
    } else {
        for (i = 0, sp = s_base; i < s_tmpl->ie_count; i++) {
            offsets[i] = sp - s_base;
            s_ie = s_tmpl->ie_ary[i];
            if (s_ie->len == FB_IE_VARLEN) {
                if (s_ie->canon->type == FB_BASIC_LIST) {
                    FB_TC_SBC_OFF(sizeof(fbBasicList_t));
                    sp += sizeof(fbBasicList_t);
                    s_rem -= sizeof(fbBasicList_t);
                } else if (s_ie->canon->type == FB_SUB_TMPL_LIST) {
                    FB_TC_SBC_OFF(sizeof(fbSubTemplateList_t));
                    sp += sizeof(fbSubTemplateList_t);
                    s_rem -= sizeof(fbSubTemplateList_t);
                } else if (s_ie->canon->type == FB_SUB_TMPL_MULTI_LIST) {
                    FB_TC_SBC_OFF(sizeof(fbSubTemplateMultiList_t));
                    sp += sizeof(fbSubTemplateMultiList_t);
                    s_rem -= sizeof(fbSubTemplateMultiList_t);
                } else {
                    FB_TC_SBC_OFF(sizeof(fbVarfield_t));
                    sp += sizeof(fbVarfield_t);
                    s_rem -= sizeof(fbVarfield_t);
                }
            } else {
                FB_TC_SBC_OFF(s_ie->len);
                sp += s_ie->len; s_rem -= s_ie->len;
            }
        }
    }

    /* get EOR offset */
    s_len = offsets[i] = sp - s_base;

    if (NULL == offsets_out) {
        /* can only return s_len, not the offsets */
        fbTranscodeFreeVarlenOffsets(s_tmpl, offsets);
    } else {
        /* cache offsets if possible */
        if (!s_tmpl->is_varlen) {
            s_tmpl->off_cache = offsets;
        }

        /* return offsets */
        *offsets_out = offsets;
    }

    /* return EOR offset */
    return s_len;

  err:
    g_free(offsets);
    return -1;
}


/**
 * fbTranscodeZero
 *
 *
 *
 *
 *
 */
static gboolean
fbTranscodeZero(
    uint8_t  **dp,
    uint32_t  *d_rem,
    uint32_t   len,
    GError   **err)
{
    /* Check for write overrun */
    FB_TC_DBC(len, "zero transcode");

    /* fill zeroes */
    memset(*dp, 0, len);

    /* maintain counters */
    *dp += len; *d_rem -= len;

    return TRUE;
}


#if G_BYTE_ORDER == G_BIG_ENDIAN

/**
 *  Implements fbEncodeFixed() and fbDecodeFixed() on a big endian system.
 *
 *  Transcodes `s_len` octets in `sp` to `d_len` octets in `dp` where `sp` and
 *  `dp` hold data for two fixed-length elements other than @ref FB_FLOAT_64
 *  (double) values.  Moves `dp` `d_len` bytes forward and reduces `d_rem` by
 *  `d_len`.
 *
 *  If `d_rem` is less than `d_len`, makes no change to `dp` or `d_len` and
 *  returns FALSE.  Otherwise returns TRUE.
 *
 *  `is_endian` and `is_signed` control what happens if the lengths are not
 *  equal.
 *
 *  If `s_len` equals `d_len`, the data is copied and `is_endian` and
 *  `is_signed` are ignored.
 *
 *  If `s_len` is larger, only `d_len` bytes of data are copied. If
 *  `is_endian` is FALSE, the first `d_len` bytes of data from `sp` are copied
 *  to `dp`; otherwise the final `d_len` bytes are copied. This may alter the
 *  value if significant octets are removed during truncation.
 *
 *  If `d_len` is larger, all `s_len` bytes of data are copied. If `is_endian`
 *  is FALSE, the octets in `sp` are copied to the beginning of `dp` and the
 *  remaining (`d_len` - `s_len`) octets of `dp` are zeroed. Otherwise, the
 *  data in `sp` is copied to the end of `dp` and the first (`d_len` -
 *  `s_len`) octets of `dp` are filled: If `is_signed` is FALSE or if the
 *  most-significant bit (the sign bit) of `sp` is 0, they are set to 0,
 *  otherwise they are set to 0xff.
 */
static gboolean
fbTranscodeFixedBigEndian(
    const uint8_t  *sp,
    uint8_t       **dp,
    uint32_t       *d_rem,
    uint32_t        s_len,
    uint32_t        d_len,
    gboolean        is_endian,
    gboolean        is_signed,
    GError        **err)
{
    FB_TC_DBC(d_len, "fixed transcode");

    /* if `is_signed` is true, `is_endian` must also be true */
    g_assert(FALSE == is_signed || TRUE == is_endian);

    /* handle the (d_len == s_len) case with d_len < s_len */

    if (!is_endian) {
        /* copy the first MIN(d_len, s_len) bytes; zero the tail */
        if (d_len <= s_len) {
            memcpy(*dp, sp, d_len);
        } else {
            memset(*dp + s_len, 0, d_len - s_len);
            memcpy(*dp, sp, s_len);
        }
    } else if (d_len <= s_len) {
        /* copy the final `d_len` bytes of `sp` to `dp` */
        memcpy(*dp, sp + (s_len - d_len), d_len);
    } else {
        /* copy the `s_len` bytes to tail of `dp`; fill the head */
        int fill = ((!is_signed) || (0 == (0x80u & *(sp)))) ? 0x00 : 0xff;
        memset(*dp, fill, d_len - s_len);
        memcpy(*dp + (d_len - s_len), sp, s_len);
    }

    /* maintain counters */
    *dp += d_len; *d_rem -= d_len;

    return TRUE;
}

/**
 *  Implements fbEncodeDouble() and fbDecodeDouble() on a big endian system.
 *
 *  Transcodes `s_len` octets in `sp` to `d_len` octets in `dp` where `sp` and
 *  `dp` hold data for two @ref FB_FLOAT_64 (floating point) values.  Moves
 *  `dp` `d_len` bytes forward and reduces `d_rem` by `d_len`.
 *
 *  If `d_rem` is less than `d_len`, makes no change to `dp` or `d_len` and
 *  returns FALSE.  Otherwise returns TRUE.
 *
 *  If `s_len` equals `d_len`, the data is copied.
 *
 *  Otherwise, reads `sp` as a float or double, converts it to a double or
 *  float, and stores the new value in `dp`.
 *
 *  If `s_len` does not equal `d_len` and either value is not the size or a
 *  float or double, zeroes `dp` and logs a warning.
 */
static gboolean
fbTranscodeDoubleBigEndian(
    const uint8_t  *sp,
    uint8_t       **dp,
    uint32_t       *d_rem,
    uint32_t        s_len,
    uint32_t        d_len,
    GError        **err)
{
    double d;
    float  f;

    FB_TC_DBC(d_len, "double transcode");

    if (s_len == d_len) {
        memcpy(*dp, sp, d_len);
    } else if (s_len == SIZEOF_DOUBLE && d_len == SIZEOF_FLOAT) {
        /* source is double, dest is float */
        memcpy(&d, sp, sizeof(d));
        f = d;
        memcpy(*dp, &f, sizeof(f));
    } else if (s_len == SIZEOF_FLOAT && d_len == SIZEOF_DOUBLE) {
        /* source is float, dest is double */
        memcpy(&f, sp, sizeof(f));
        d = f;
        memcpy(*dp, &d, sizeof(d));
    } else {
        g_warning("float transcode: Unexpected element lengths"
                  " s_len = %u, d_len = %u", s_len, d_len);
        memset(dp, 0, d_len);
    }

    /* maintain counters */
    *dp += d_len; *d_rem -= d_len;

    return TRUE;
}

/**
 *  Implements fbCopyInteger() on a big endian system.
 *
 *  Copies an integer value between two memory locations that may differ in
 *  length.  This is a memory-to-memory copy and not to be used for
 *  transcoding.
 *
 *  Copies from `spv` of length `s_len` to `dpv` of length `d_len`.
 *  `is_signed` should be true if the value is signed and false otherwise.
 */
static void
fbCopyIntegerBigEndian(
    const void    *spv,
    void          *dpv,
    size_t         s_len,
    size_t         d_len,
    unsigned int   is_signed)
{
    const uint8_t *sp = (const uint8_t *)spv;
    uint8_t       *dp = (uint8_t *)dpv;

    if (d_len <= s_len) {
        memcpy(dp, sp + (s_len - d_len), d_len);
    } else {
        int fill = ((!is_signed) || (0 == (0x80u & *sp))) ? 0x00 : 0xff;
        memset(dp, fill, d_len - s_len);
        memcpy(dp + (d_len - s_len), sp, s_len);
    }
}

#define fbEncodeFixed   fbTranscodeFixedBigEndian
#define fbDecodeFixed   fbTranscodeFixedBigEndian
#define fbEncodeDouble  fbTranscodeDoubleBigEndian
#define fbDecodeDouble  fbTranscodeDoubleBigEndian
#define fbCopyInteger   fbCopyIntegerBigEndian

#else  /* #if G_BYTE_ORDER == G_BIG_ENDIAN */

#if 0
/**
 *  fbTranscodeSwap
 *
 *
 *
 *
 *
 */
static void
fbTranscodeSwap(
    uint8_t   *a,
    uint32_t   len)
{
    uint32_t i;
    uint8_t  t;
    for (i = 0; i < len / 2; i++) {
        t = a[i];
        a[i] = a[(len - 1) - i];
        a[(len - 1) - i] = t;
    }
}
#endif  /* 0 */

/**
 *  Copies `len` bytes of data from `sp` to `dp` swapping the bytes as they
 *  are copied.
 */
static void
fbTranscodeCopySwap(
    uint8_t        *dp,
    const uint8_t  *sp,
    uint32_t        len)
{
    g_assert(sp != dp);

#ifndef HAVE_ALIGNED_ACCESS_REQUIRED
    switch (len) {
      case 1:
        *dp = *sp;
        return;
      case 2:
        *((uint16_t *)dp) = ntohs(*((uint16_t *)sp));
        return;
      case 4:
        *((uint32_t *)dp) = ntohl(*((uint32_t *)sp));
        return;
      case 8:
        *((uint64_t *)dp) = ntohll(*((uint64_t *)sp));
        return;
    }
#endif  /* #ifndef HAVE_ALIGNED_ACCESS_REQUIRED */

    sp += len - 1;
    for ( ; len > 0; --len, ++dp, --sp) {
        *dp = *sp;
    }
}



/**
 *  Implements fbEncodeFixed() on a little endian system.
 *
 *  Encodes for export `s_len` octets in `sp` to `d_len` octets in `dp` where
 *  `sp` and `dp` hold data for two fixed-length elements other @ref
 *  FB_FLOAT_64 (double) values.  Moves `dp` `d_len` bytes forward and reduces
 *  `d_rem` by `d_len`.
 *
 *  If `d_rem` is less than `d_len`, makes no change to `dp` or `d_len` and
 *  returns FALSE.  Otherwise returns TRUE.
 *
 *  `is_endian` and `is_signed` control byte-swapping of the values and what
 *  happens if the lengths are not equal.
 *
 *  If `is_endian` is FALSE, the first MIN(`s_len`, `d_len`) bytes of data
 *  from the start of `sp` are copied to the start of `dp` and any remaining
 *  bytes in `dp` are zeroed.
 *
 *  If `s_len` equals `d_len`, the data is copied and byte-swapped.
 *
 *  If `s_len` is larger, the first `d_len` bytes of data from `sp` are copied
 *  into `dp` and byte-swapped.
 *
 *  If `d_len` is larger, the data are copied to the head of `dp` and the
 *  final (`d_len` - `s_len`) bytes of `dp` are filled: If `is_signed` is
 *  FALSE or if the most-significant bit (the sign bit) of `sp` is 0, they are
 *  set to 0, otherwise they are set to 0xff. Finally, `dp` is byte-swapped.
 */
static gboolean
fbEncodeFixedLittleEndian(
    const uint8_t  *sp,
    uint8_t       **dp,
    uint32_t       *d_rem,
    uint32_t        s_len,
    uint32_t        d_len,
    gboolean        is_endian,
    gboolean        is_signed,
    GError        **err)
{
    FB_TC_DBC(d_len, "fixed LE encode");

    /* if `is_signed` is true, `is_endian` must also be true */
    g_assert(FALSE == is_signed || TRUE == is_endian);

    /* handle the (d_len == s_len) case with d_len < s_len */

    if (!is_endian) {
        /* handle the same as big endian */
        /* copy the first MIN(d_len, s_len) bytes; zero the tail */
        if (d_len <= s_len) {
            memcpy(*dp, sp, d_len);
        } else {
            /* FIXME: Is it faster just to memset() it all w/o math? */
            memset(*dp + s_len, 0, d_len - s_len);
            memcpy(*dp, sp, s_len);
        }
    } else if (d_len <= s_len) {
        /* copy and swap the first `d_len` bytes */
        fbTranscodeCopySwap(*dp, sp, d_len);
    } else {
        /* copy and swap `s_len` bytes to the tail of `dp`, fill the head */
        int fill = (((!is_signed) || (0 == (0x80u & *(sp + s_len - 1))))
                    ? 0x00 : 0xff);
        memset(*dp, fill, d_len - s_len);
        fbTranscodeCopySwap(*dp + d_len - s_len, sp, s_len);
    }

    /* maintain counters */
    *dp += d_len; *d_rem -= d_len;

    return TRUE;
}


/**
 *  Implements fbDecodeFixed() on a little endian system.
 *
 *  Decodes upon import `s_len` octets in `sp` to `d_len` octets in `dp` where
 *  `sp` and `dp` hold data for two fixed-length elements other @ref
 *  FB_FLOAT_64 (double) values.  Moves `dp` `d_len` bytes forward and reduces
 *  `d_rem` by `d_len`.
 *
 *  If `d_rem` is less than `d_len`, makes no change to `dp` or `d_len` and
 *  returns FALSE.  Otherwise returns TRUE.
 *
 *  `is_endian` and `is_signed` control byte-swapping of the values and what
 *  happens if the lengths are not equal.
 *
 *  If `is_endian` is FALSE, the first MIN(`s_len`, `d_len`) bytes of data
 *  from the start of `sp` are copied to the start of `dp` and any remaining
 *  bytes in `dp` are zeroed.
 *
 *  If `s_len` equals `d_len`, the data is copied and byte-swapped.
 *
 *  If `s_len` is larger, the final `d_len` bytes of data from `sp` are copied
 *  into `dp` and byte-swapped.
 *
 *  If `d_len` is larger, the data are copied to the tail of `dp` and the
 *  first (`d_len` - `s_len`) bytes of `dp` are filled: If `is_signed` is
 *  FALSE or if the most-significant bit (the sign bit) of `sp` is 0, they are
 *  set to 0, otherwise they are set to 0xff. Finally, `dp` is byte-swapped.
 */
static gboolean
fbDecodeFixedLittleEndian(
    const uint8_t  *sp,
    uint8_t       **dp,
    uint32_t       *d_rem,
    uint32_t        s_len,
    uint32_t        d_len,
    gboolean        is_endian,
    gboolean        is_signed,
    GError        **err)
{
    FB_TC_DBC(d_len, "fixed LE decode");

    /* if `is_signed` is true, `is_endian` must also be true */
    g_assert(FALSE == is_signed || TRUE == is_endian);

    /* handle the (d_len == s_len) case with d_len < s_len */

    if (!is_endian) {
        /* handle the same as big endian */
        /* copy the first MIN(d_len, s_len) bytes; zero the tail */
        if (d_len <= s_len) {
            memcpy(*dp, sp, d_len);
        } else {
            memset(*dp + s_len, 0, d_len - s_len);
            memcpy(*dp, sp, s_len);
        }
    } else if (d_len <= s_len) {
        /* copy and swap the final `d_len` bytes of `sp` */
        fbTranscodeCopySwap(*dp, sp + (s_len - d_len), d_len);
    } else {
        /* copy and swap `s_len` bytes to the head of `dp`, fill the tail */
        int fill = ((!is_signed) || (0 == (0x80u & *(sp)))) ? 0x00 : 0xff;
        memset(*dp + s_len, fill, (d_len - s_len));
        fbTranscodeCopySwap(*dp, sp, s_len);
    }

    /* maintain counters */
    *dp += d_len; *d_rem -= d_len;

    return TRUE;
}

/**
 *  Implements fbEncodeDouble() on a little endian system.
 *
 *  Encodes for export `s_len` octets in `sp` to `d_len` octets in `dp` where
 *  `sp` and `dp` hold data for two @ref FB_FLOAT_64 (floating point) values.
 *  Moves `dp` `d_len` bytes forward and reduces `d_rem` by `d_len`.
 *
 *  If `d_rem` is less than `d_len`, makes no change to `dp` or `d_len` and
 *  returns FALSE.  Otherwise returns TRUE.
 *
 *  If `s_len` equals `d_len`, the data is copied and byte-swapped.
 *
 *  Otherwise, reads `sp` as a float or double, converts it to a double or
 *  float, byte-swaps that value, and stores the new value in `dp`.
 *
 *  If `s_len` does not equal `d_len` and either value is not the size or a
 *  float or double, zeroes `dp` and logs a warning.
 */
static gboolean
fbEncodeDoubleLittleEndian(
    const uint8_t  *sp,
    uint8_t       **dp,
    uint32_t       *d_rem,
    uint32_t        s_len,
    uint32_t        d_len,
    GError        **err)
{
    union darr_un {
        uint8_t u[SIZEOF_DOUBLE];
        double  d;
    } darr;
    union farr_un {
        uint8_t u[SIZEOF_FLOAT];
        float   f;
    } farr;

    FB_TC_DBC(d_len, "float LE encode");

    /* Could switch on ((slen << 2) | (d_len)): 20:src=flt,dst=flt;
     * 40:src=dbl,dst=dbl; 24:src=flt,dst=dbl; 36:src=dbl,dst=flt  */

    if (s_len == d_len) {
        fbTranscodeCopySwap(*dp, sp, d_len);
    } else if (s_len == SIZEOF_DOUBLE && d_len == SIZEOF_FLOAT) {
        /* source is double, dest is float */
        memcpy(darr.u, sp, sizeof(darr.u));
        farr.f = darr.d;
        fbTranscodeCopySwap(*dp, farr.u, sizeof(farr.u));
    } else if (s_len == SIZEOF_FLOAT && d_len == SIZEOF_DOUBLE) {
        /* source is float, dest is double */
        memcpy(farr.u, sp, sizeof(farr.u));
        darr.d = farr.f;
        fbTranscodeCopySwap(*dp, darr.u, sizeof(darr.u));
    } else {
        g_warning("float LE transcode: Unexpected element lengths"
                  " s_len = %u, d_len = %u", s_len, d_len);
        memset(dp, 0, d_len);
    }

    return TRUE;
}

/**
 *  Implements fbDecodeDouble() on a little endian system.
 *
 *  Decodes upon import `s_len` octets in `sp` to `d_len` octets in `dp` where
 *  `sp` and `dp` hold data for two @ref FB_FLOAT_64 (floating point) values.
 *  Moves `dp` `d_len` bytes forward and reduces `d_rem` by `d_len`.
 *
 *  If `d_rem` is less than `d_len`, makes no change to `dp` or `d_len` and
 *  returns FALSE.  Otherwise returns TRUE.
 *
 *  If `s_len` equals `d_len`, the data is copied and byte-swapped.
 *
 *  Otherwise, reads `sp` as a float or double, byte-swaps the value, converts
 *  it to a double or float, and stores the new value in `dp`.
 *
 *  If `s_len` does not equal `d_len` and either value is not the size or a
 *  float or double, zeroes `dp` and logs a warning.
 */
static gboolean
fbDecodeDoubleLittleEndian(
    const uint8_t  *sp,
    uint8_t       **dp,
    uint32_t       *d_rem,
    uint32_t        s_len,
    uint32_t        d_len,
    GError        **err)
{
    union darr_un {
        uint8_t   u[SIZEOF_DOUBLE];
        double    d;
    } darr;
    union farr_un {
        uint8_t   u[SIZEOF_FLOAT];
        float     f;
    } farr;

    FB_TC_DBC(d_len, "float LE decode");

    if (s_len == d_len) {
        fbTranscodeCopySwap(*dp, sp, d_len);
    } else if (s_len == SIZEOF_DOUBLE && d_len == SIZEOF_FLOAT) {
        /* source is double, dest is float */
        fbTranscodeCopySwap(darr.u, sp, sizeof(darr.u));
        farr.f = darr.d;
        memcpy(*dp, farr.u, sizeof(farr.u));
    } else if (s_len == SIZEOF_FLOAT && d_len == SIZEOF_DOUBLE) {
        /* source is float, dest is double */
        fbTranscodeCopySwap(farr.u, sp, sizeof(farr.u));
        darr.d = farr.f;
        memcpy(*dp, darr.u, sizeof(darr.u));
    } else {
        g_warning("float LE transcode: Unexpected element lengths"
                  " s_len = %u, d_len = %u", s_len, d_len);
        memset(dp, 0, d_len);
    }

    return TRUE;
}

/**
 *  Implements fbCopyInteger() on a little endian system.
 *
 *  Copies an integer value between two memory locations that may differ in
 *  length.  This is a memory-to-memory copy and not to be used for
 *  transcoding.
 *
 *  Copies from `spv` of length `s_len` to `dpv` of length `d_len`.
 *  `is_signed` should be true if the value is signed and false otherwise.
 */
static void
fbCopyIntegerLittleEndian(
    const void    *spv,
    void          *dpv,
    size_t         s_len,
    size_t         d_len,
    unsigned int   is_signed)
{
    const uint8_t *sp = (const uint8_t *)spv;
    uint8_t       *dp = (uint8_t *)dpv;

    if (d_len <= s_len) {
        memcpy(dp, sp, d_len);
    } else {
        int fill = (((!is_signed) || (0 == (0x80u & *(sp + s_len - 1))))
                    ? 0x00 : 0xff);
        memset(dp + s_len, fill, d_len - s_len);
        memcpy(dp, sp, s_len);
    }
}

#define fbEncodeFixed   fbEncodeFixedLittleEndian
#define fbDecodeFixed   fbDecodeFixedLittleEndian
#define fbEncodeDouble  fbEncodeDoubleLittleEndian
#define fbDecodeDouble  fbDecodeDoubleLittleEndian
#define fbCopyInteger   fbCopyIntegerLittleEndian

#endif  /* #else of #if G_BYTE_ORDER == G_BIG_ENDIAN */


/**
 * fbEncodeVarfield
 *
 *
 *
 *
 *
 */
static gboolean
fbEncodeVarfield(
    uint8_t   *sp,
    uint8_t  **dp,
    uint32_t  *d_rem,
    GError   **err)
{
    uint32_t      d_len;
    fbVarfield_t *sv;
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    fbVarfield_t  sv_local;
    sv = &sv_local;
    memcpy(sv, sp, sizeof(fbVarfield_t));
#else
    sv = (fbVarfield_t *)sp;
#endif /* if HAVE_ALIGNED_ACCESS_REQUIRED */

    /* calculate total destination length */
    d_len = sv->len + ((sv->len < 255) ? 1 : 3);

    /* Check buffer bounds */
    FB_TC_DBC(d_len, "variable-length encode");

    /* emit IPFIX variable length */
    if (sv->len < 255) {
        FB_WRITEINC_U8(*dp, sv->len);
    } else {
        FB_WRITEINC_U8(*dp, 255);
        FB_WRITEINC_U16(*dp, sv->len);
    }

    /* emit buffer contents */
    if (sv->len && sv->buf) {memcpy(*dp, sv->buf, sv->len);}
    /* maintain counters */
    *dp += sv->len; *d_rem -= d_len;

    return TRUE;
}


/**
 * fbDecodeVarfield
 *
 * decodes a variable length IPFIX element into its C structure location
 *
 * @param sp source pointer
 * @param dp destination pointer
 * @param d_rem destination amount remaining
 * @param err glib2 error structure to return error information
 *
 * @return true on success, false on error, check err return param for details
 *
 */
static gboolean
fbDecodeVarfield(
    uint8_t   *sp,
    uint8_t  **dp,
    uint32_t  *d_rem,
    GError   **err)
{
    uint16_t      s_len;
    fbVarfield_t *dv;
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    fbVarfield_t  dv_local;
    dv = &dv_local;
#else
    dv = (fbVarfield_t *)*dp;
#endif

    /* calculate total source length */
    FB_READ_LIST_LENGTH(s_len, sp);

    /* Okay. We know how long the source is. Check buffer bounds. */
    FB_TC_DBC(sizeof(fbVarfield_t), "variable-length decode");

    /* Do transcode. Don't copy; fbVarfield_t's semantics allow us just
     * to return a pointer into the read buffer. */
    dv->len = (uint32_t)s_len;
    dv->buf = s_len ? sp : NULL;

#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    memcpy(*dp, dv, sizeof(fbVarfield_t));
#endif

    /* maintain counters */
    *dp += sizeof(fbVarfield_t); *d_rem -= sizeof(fbVarfield_t);

    return TRUE;
}

#if 0
/*
 * static gboolean
 * fbDecodeFixedToVarlen(
 *     uint8_t   *sp,
 *     uint8_t  **dp,
 *     uint32_t  *d_rem,
 *     uint32_t   flags __attribute__((unused)),
 *     GError   **err)
 * {
 *     return FALSE;
 * }
 *
 * static gboolean
 * fbEncodeFixedToVarlen(
 *     uint8_t   *sp,
 *     uint16_t   s_len,
 *     uint8_t  **dp,
 *     uint32_t  *d_rem,
 *     uint32_t   flags __attribute__((unused)),
 *     GError   **err)
 * {
 *     uint32_t d_len;
 *     uint16_t sll;
 *
 *     d_len = s_len + ((s_len < 255) ? 1 : 3);
 *     FB_TC_DBC(d_len, "fixed to variable lengthed encode");
 *     if (s_len < 255) {
 *         **dp = (uint8_t)s_len;
 *         *dp += 1;
 *     } else {
 *         **dp = 255;
 *         sll = g_htons(s_len);
 *         memcpy(*dp + 1, &sll, sizeof(uint16_t));
 *         *dp += 3;
 *     }
 *
 *     if (s_len) {
 *         memcpy(*dp, sp, s_len);
 *         *dp += s_len;
 *         *d_rem -= d_len;
 *     }
 *
 *     return TRUE;
 * }
 *
 * static gboolean
 * fbDecodeVarlenToFixed(
 *     uint8_t   *sp,
 *     uint8_t  **dp,
 *     uint32_t  *d_rem,
 *     uint32_t   flags __attribute__((unused)),
 *     GError   **err)
 * {
 *     return FALSE;
 * }
 *
 * static gboolean
 * fbEncodeVarlenToFixed(
 *     uint8_t   *sp,
 *     uint16_t   d_len,
 *     uint8_t  **dp,
 *     uint32_t  *d_rem,
 *     uint32_t   flags __attribute__((unused)),
 *     GError   **err)
 * {
 *     uint16_t      lenDiff;
 *     fbVarfield_t *sVar = (fbVarfield_t *)sp;
 *     FB_TC_DBC(d_len, "varlen to fixed encode");
 *     if (sVar->len < d_len) {
 *         lenDiff = d_len - sVar->len;
 *         memset(*dp, 0, lenDiff);
 *         memcpy(*dp + lenDiff, sVar->buf, sVar->len);
 *     } else {
 *         // copy d_len bytes, truncating the varfield at d_len
 *         memcpy(*dp, sVar->buf, d_len);
 *     }
 *
 *     if (d_len > 1 && (flags & FB_IE_F_ENDIAN)) {
 *         fbTranscodeSwap(*dp, d_len);
 *     }
 *
 *     *dp += d_len; *d_rem -= d_len;
 *
 *     return TRUE;
 * }
 */
#endif  /* 0 */


/*
 *  Returns the size of the memory needed to hold an info element.
 *
 *  For fixed-length elements, this is its length.  For variable
 *  length elements, it is the size of a struct, either fbVarfield_t
 *  or one of the List structures.
 */
static uint16_t
fbSizeofIE(
    const fbTemplateField_t  *ie)
{
    if (FB_IE_VARLEN != ie->len) {
        return ie->len;
    }
    switch (fbTemplateFieldGetType(ie)) {
      case FB_BASIC_LIST:
        return sizeof(fbBasicList_t);
      case FB_SUB_TMPL_LIST:
        return sizeof(fbSubTemplateList_t);
      case FB_SUB_TMPL_MULTI_LIST:
        return sizeof(fbSubTemplateMultiList_t);
      default:
        return sizeof(fbVarfield_t);
    }
}

static gboolean
validBasicList(
    fbBasicList_t  *basicList,
    GError        **err)
{
    if (!basicList) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Null basic list pointer passed to encode");
        return FALSE;
    } else if (!basicList->field.canon /* || !basicList->infoElement */) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Null information element in basic list passed to encode");
        return FALSE;
    } else if (basicList->numElements && !basicList->dataLength) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Positive num elements, but zero data length in basiclist");
        return FALSE;
    } else if (basicList->dataLength && !basicList->dataPtr) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Positive data length but null data pointer in basiclist");
        return FALSE;
    }
    return TRUE;
}

static gboolean
validSubTemplateList(
    fbSubTemplateList_t  *STL,
    GError              **err)
{
    if (!STL) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Null sub template list pointer passed to encode");
        return FALSE;
    } else if (!STL->tmpl || !STL->tmplID) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Invalid template pointer %p or ID %d passed to STL encode",
                    (void *)STL->tmpl, STL->tmplID);
        return FALSE;
    } else if (STL->numElements && !STL->dataLength) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Positive num elements, but zero data length in STL");
        return FALSE;
    } else if (STL->dataLength && !STL->dataPtr) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Positive data length but null data pointer in STL");
        return FALSE;
    }
    return TRUE;
}

static gboolean
validSubTemplateMultiList(
    fbSubTemplateMultiList_t  *sTML,
    GError                   **err)
{
    if (!sTML) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Null sub template multi list pointer passed to encode");
        return FALSE;
    } else if (sTML->numElements && !sTML->firstEntry) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Positive num elements, but NULL first Entry in STML");
        return FALSE;
    }
    return TRUE;
}

static gboolean
validSubTemplateMultiListEntry(
    fbSubTemplateMultiListEntry_t  *entry,
    GError                        **err)
{
    if (!entry) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Null sub template multi list entry pointer");
        return FALSE;
    } else if (!entry->tmpl || !entry->tmplID) {
        g_set_error(
            err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
            "Invalid template pointer %p or ID %d passed to STML encode",
            (void *)entry->tmpl, entry->tmplID);
        return FALSE;
    } else if (entry->dataLength && !entry->dataPtr) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Positive data length but null data pointer in STML");
        return FALSE;
    }
    return TRUE;
}


#if 0
/*
 * bytesUsedBySrcTemplate
 *
 *
 *
 *  parses the data according to the external template to determine the number
 *  of bytes in the src for this template instance
 *  this function is intended to be used in decoding
 *  and assumes the values are still in NETWORK byte order
 *  data: pointer to the data that came accross the wire
 *  ext_tmpl: external template...what the data looks like on arrival
 *  bytesInSrc:  number of bytes in incoming data used by the ext_tmpl
 */
static uint16_t
bytesUsedBySrcTemplate(
    const uint8_t       *data,
    const fbTemplate_t  *ext_tmpl)
{
    const fbTemplateField_t *ie;
    const uint8_t           *srcWalker = data;
    uint16_t len;
    uint16_t i;

    if (!ext_tmpl->is_varlen) {
        return ext_tmpl->ie_len;
    }

    for (i = 0; i < ext_tmpl->ie_count; i++) {
        ie = ext_tmpl->ie_ary[i];
        if (ie->len == FB_IE_VARLEN) {
            FB_READ_LIST_LENGTH(len, srcWalker);
            srcWalker += len;
        } else {
            srcWalker += ie->len;
        }
    }
    return srcWalker - data;
}
#endif  /* 0 */


static gboolean
fbEncodeBasicList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err)
{
    uint16_t       totalLength;
    uint16_t       headerLength;
    uint16_t       ie_len;
    uint16_t       ie_num;
    uint8_t       *lengthPtr       = NULL;
    uint16_t       i;
    gboolean       enterprise      = FALSE;
    uint8_t       *prevDst         = NULL;
    fbBasicList_t *basicList;
    uint8_t       *thisItem        = NULL;
    gboolean       retval          = FALSE;
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    fbBasicList_t  basicList_local;
    basicList = &basicList_local;
    memcpy(basicList, src, sizeof(fbBasicList_t));
#else
    basicList = (fbBasicList_t *)src;
#endif /* if HAVE_ALIGNED_ACCESS_REQUIRED */

    if (!validBasicList(basicList, err)) {
        return FALSE;
    }

    /* we need to check the buffer bounds throughout the function at each
     * stage then decrement d_rem as we go */

    /* minimum header is 5 bytes:
     * -- 1 for the semantic
     * -- 2 for the field id
     * -- 2 for the field length
     */

    headerLength = 5;
    ie_len = basicList->field.len;

    /* get the info element number */
    ie_num = basicList->field.canon->num;

    /* check for enterprise value in the information element, to set bit.
     * Need to know if IE is enterprise before adding totalLength for
     * fixed length IE's */

    if (basicList->field.canon->ent) {
        enterprise = TRUE;
        ie_num |= IPFIX_ENTERPRISE_BIT;
        headerLength += 4;
    }

    /* check the length */
    if (ie_len == FB_IE_VARLEN) {
        /* check for room for the header */
        FB_TC_DBC(headerLength, "basic list encode header");
    } else {
        /* fixed length info element; we know how long the entire list will
         * be; test its length */
        totalLength = headerLength + basicList->numElements * ie_len;
        FB_TC_DBC(totalLength, "basic list encode fixed list");
    }
    (*d_rem) -= headerLength;

    /* encode as variable length field */
    FB_TC_DBC(3, "basic list variable length encode header");
    FB_WRITEINCREM_U8(*dst, 255, *d_rem);

    /* Mark location of length */
    lengthPtr = *dst;
    (*dst) += 2;
    (*d_rem) -= 2;

    /* Mark beginning of element */
    prevDst = *dst;

    /* add the semantic field */
    FB_WRITEINC_U8(*dst, basicList->semantic);

    /* write the element number */
    FB_WRITEINC_U16(*dst, ie_num);

    /* add the info element length */
    FB_WRITEINC_U16(*dst, ie_len);

    /* if enterprise specific info element, add the enterprise number */
    if (enterprise) {
        /* we alredy check room above (headerLength) for enterprise field */
        FB_WRITEINC_U32(*dst, basicList->field.canon->ent);
    }

    if (basicList->numElements) {
        /* add the data */
        if (ie_len == FB_IE_VARLEN) {
            /* all future length checks will be done by the called
             * encoding functions */
            thisItem = basicList->dataPtr;
            switch (fbTemplateFieldGetType(&basicList->field)) {
              case FB_BASIC_LIST:
                for (i = 0; i < basicList->numElements; i++) {
                    if (!fbEncodeBasicList(thisItem, dst, d_rem, fbuf, err)) {
                        goto err;
                    }
                    thisItem += sizeof(fbBasicList_t);
                }
                break;
              case FB_SUB_TMPL_LIST:
                for (i = 0; i < basicList->numElements; i++) {
                    if (!fbEncodeSubTemplateList(thisItem, dst, d_rem,
                                                 fbuf, err))
                    {
                        goto err;
                    }
                    thisItem += sizeof(fbSubTemplateList_t);
                }
                break;
              case FB_SUB_TMPL_MULTI_LIST:
                for (i = 0; i < basicList->numElements; i++) {
                    if (!fbEncodeSubTemplateMultiList(thisItem, dst,
                                                      d_rem, fbuf, err))
                    {
                        goto err;
                    }
                    thisItem += sizeof(fbSubTemplateMultiList_t);
                }
                break;
              default:
                /* add the varfields, adding up the length field */
                for (i = 0; i < basicList->numElements; i++) {
                    if (!fbEncodeVarfield(thisItem, dst, d_rem, err)) {
                        goto err;
                    }
                    thisItem += sizeof(fbVarfield_t);
                }
            }
        } else {
            /* Since the length of source and dest are the same, we can use
             * fbEncodeFixed() for everything with is_endian set correctly and
             * is_signed=FALSE. */
            gboolean isEndian = fbInfoElementIsEndian(basicList->field.canon);
            thisItem = basicList->dataPtr;
            for (i = 0; i < basicList->numElements; ++i, thisItem += ie_len) {
                if (!fbEncodeFixed(thisItem, dst, d_rem, ie_len, ie_len,
                                   isEndian, 0, err))
                {
                    goto err;
                }
            }
        }
    }

    retval = TRUE;

  err:
    totalLength = (uint16_t)((*dst) - prevDst);
    FB_WRITE_U16(lengthPtr, totalLength);

    return retval;
}

static gboolean
fbDecodeBasicList(
    fbInfoModel_t  *model,
    uint8_t        *src,
    uint8_t       **dst,
    uint32_t       *d_rem,
    fBuf_t         *fbuf,
    GError        **err)
{
    uint16_t        srcLen;
    uint16_t        elementLen;
    fbInfoElement_t tempElement     = FB_IE_NULL;
    fbBasicList_t  *basicList;
    uint8_t        *srcWalker       = NULL;
    uint8_t        *thisItem        = NULL;
    fbVarfield_t   *thisVarfield    = NULL;
    const fbInfoElement_t *ie;
    uint16_t        len;
    int             i;
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    fbBasicList_t   basicList_local;
    basicList = &basicList_local;
    memcpy(basicList, *dst, sizeof(fbBasicList_t));
#else
    basicList = (fbBasicList_t *)*dst;
#endif /* if HAVE_ALIGNED_ACCESS_REQUIRED */

    if (basicList->dataLength || basicList->dataPtr) {
        fbBasicListClear(basicList);
    }

    /* check buffer bounds */
    if (d_rem) {
        FB_TC_DBC(sizeof(fbBasicList_t), "basic-list decode");
    }

    /* FIXME: We should do something to handle a basicList of size 0.  Perhaps
     * set the element to paddingOctets or something so it is not NULL. */

    FB_READ_LIST_LENGTH(srcLen, src);
    if (srcLen < 5) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,
                    "Stated length (%d) is too small for a BasicList header",
                    srcLen);
        return FALSE;
    }

    /* read the semantic field, element ID, and element length */
    FB_READINCREM_U8(basicList->semantic, src, srcLen);
    FB_READINCREM_U16(tempElement.num, src, srcLen);
    FB_READINCREM_U16(elementLen, src, srcLen);
    if (!elementLen) {
        /* FIXME: What if element is octetArray or string?  The length could
         * legally be zero */
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Illegal basic list element length (0)");
        return FALSE;
    }

    /* if enterprise bit is set, read this field */
    if (0 == (tempElement.num & IPFIX_ENTERPRISE_BIT)) {
        tempElement.ent = 0;
    } else if (srcLen < 4) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,
                    ("Stated length (%d) is too small for a BasicList header"
                     " with enterprise number"), srcLen);
        return FALSE;
    } else {
        FB_READINCREM_U32(tempElement.ent, src, srcLen);
        tempElement.num &= ~IPFIX_ENTERPRISE_BIT;
    }

    /* find the proper info element pointer based on what we built */
    ie = fbInfoModelGetElement(model, &tempElement);
    if (!ie) {
        /* if infoElement does not exist, note it's alien and add it */
        tempElement.len = elementLen;
        ie = fbInfoModelAddAlienElement(model, &tempElement);
        if (!ie) {
            g_log(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                  "BasicList Decode Error: No Information Element with ID %d "
                  "defined", tempElement.num);
            fbBasicListCollectorInit(basicList);
            goto END;
        }
    }
    basicList->field.canon = ie;
    basicList->field.len = elementLen;
    basicList->field.midx = 0;
    basicList->field.offset = 0;
    basicList->field.tmpl = NULL;

    if (elementLen == FB_IE_VARLEN) {
        /* first we need to find out the number of elements */
        basicList->numElements = 0;
        srcWalker = src;
        /* while we haven't walked the entire list... */
        while (srcLen > (srcWalker - src)) {
            /* parse the length of each, and jump to the next */
            FB_READ_LIST_LENGTH(len, srcWalker);
            srcWalker += len;
            basicList->numElements++;
        }

        thisItem = fbBasicListAllocData(basicList, basicList->numElements);

        /* parse the specific varlen field */
        switch (fbTemplateFieldGetType(&basicList->field)) {
          case FB_BASIC_LIST:
            /* thisItem will be incremented by DecodeBasicList's dst
             * double pointer */
            for (i = 0; i < basicList->numElements; i++) {
                if (!fbDecodeBasicList(model, src, &thisItem, NULL, fbuf,
                                       err))
                {
                    return FALSE;
                }
                /* now figure out how much to increment src by and repeat */
                FB_READ_LIST_LENGTH(len, src);
                src += len;
            }
            break;
          case FB_SUB_TMPL_LIST:
            /* thisItem will be incremented by DecodeSubTemplateList's
             * dst double pointer */
            for (i = 0; i < basicList->numElements; i++) {
                if (!fbDecodeSubTemplateList(src, &thisItem, NULL, fbuf, err)) {
                    return FALSE;
                }
                /* now figure out how much to increment src by and repeat */
                FB_READ_LIST_LENGTH(len, src);
                src += len;
            }
            break;
          case FB_SUB_TMPL_MULTI_LIST:
            /* thisItem will be incremented by DecodeSubTemplateMultiList's
             * dst double pointer */
            for (i = 0; i < basicList->numElements; i++) {
                if (!fbDecodeSubTemplateMultiList(src, &thisItem,
                                                  NULL, fbuf, err))
                {
                    return FALSE;
                }
                /* now figure out how much to increment src by and repeat */
                FB_READ_LIST_LENGTH(len, src);
                src += len;
            }
            break;
          default:
            /* now pull the data numElements times */
            g_assert(FB_STRING == basicList->field.canon->type ||
                     FB_OCTET_ARRAY == basicList->field.canon->type);
            thisVarfield = (fbVarfield_t *)basicList->dataPtr;
            for (i = 0; i < basicList->numElements; ++i, ++thisVarfield) {
                /* decode the length */
                FB_READ_LIST_LENGTH(thisVarfield->len, src);
                thisVarfield->buf = src;
                src += thisVarfield->len;
            }
            break;
        }
    } else {
        /* FIXME: We transcode using the length of the element given, so the
         * user needs to be aware of any reduced length encoding and handle it
         * herself. Perhaps we should transcode into the default size for the
         * element's type instead. */

        if (srcLen) {
            /* fixed length field, allocate then copy */
            gboolean isEndian = fbInfoElementIsEndian(basicList->field.canon);
            uint32_t dRem     = (uint32_t)srcLen;

            basicList->numElements = srcLen / elementLen;
            thisItem = fbBasicListAllocData(basicList, basicList->numElements);

            /* Since the length of source and dest are the same, we can use
             * fbDecodeFixed() for everything with is_endian set correctly and
             * is_signed=FALSE */
            for (i = 0; i < basicList->numElements; ++i, src += elementLen) {
                if (!fbDecodeFixed(src, &thisItem, &dRem, elementLen,
                                   elementLen, isEndian, 0, err))
                {
                    return FALSE;
                }
            }
        }
    }

  END:
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    memcpy(*dst, basicList, sizeof(fbBasicList_t));
#endif
    (*dst) += sizeof(fbBasicList_t);
    if (d_rem) {
        *d_rem -= sizeof(fbBasicList_t);
    }
    return TRUE;
}

static gboolean
fbEncodeSubTemplateList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err)
{
    fbSubTemplateList_t *subTemplateList;
    uint16_t             length;
    uint16_t             i;
    size_t srcLen;
    size_t dstLen;
    uint8_t             *lenPtr          = NULL;
    uint16_t             tempIntID;
    uint16_t             tempExtID;
    fbTemplate_t        *tempIntPtr;
    fbTemplate_t        *tempExtPtr;
    uint8_t             *srcPtr;
    size_t srcRem;
    gboolean             retval          = FALSE;
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    fbSubTemplateList_t  subTemplateList_local;
    subTemplateList = &subTemplateList_local;
    memcpy(subTemplateList, src, sizeof(fbSubTemplateList_t));
#else
    subTemplateList = (fbSubTemplateList_t *)src;
#endif /* if HAVE_ALIGNED_ACCESS_REQUIRED */

    if (!validSubTemplateList(subTemplateList, err)) {
        return FALSE;
    }

    /* check that there are 6 bytes available in the buffer for the header */
    FB_TC_DBC(6, "sub template list header");
    (*d_rem) -= 6;

    /* build the subtemplatelist metadata */
    /* encode as variable length */
    FB_WRITEINC_U8(*dst, 255);

    /* Save a pointer to the length location in this subTemplateList */
    lenPtr = *dst;
    (*dst) += 2;

    /* write the semantic value */
    FB_WRITEINC_U8(*dst, subTemplateList->semantic);

    /*  encode the template ID */
    FB_WRITEINC_U16(*dst, subTemplateList->tmplID);

    /* store the current templates so we can put them back */
    tempIntID = fbuf->int_tid;
    tempExtID = fbuf->ext_tid;
    tempIntPtr = fbuf->int_tmpl;
    tempExtPtr = fbuf->ext_tmpl;

    /* set the templates to those used for this subTemplateList */
    if (!fBufSetEncodeSubTemplates(fbuf, subTemplateList->tmplID,
                                   subTemplateList->tmplID, err))
    {
        goto err;
    }

    /* source is the STL's data */
    srcRem = subTemplateList->dataLength;
    srcPtr = subTemplateList->dataPtr;

    for (i = 0; i < subTemplateList->numElements; i++) {
        srcLen = srcRem;
        dstLen = *d_rem;

        /* transcode the sub template multi list*/
        if (!fbTranscode(fbuf, FALSE, srcPtr, *dst, &srcLen, &dstLen, err)) {
            goto err;
        }
        *dst   += dstLen;
        *d_rem -= dstLen;
        srcPtr += srcLen;
        srcRem -= srcLen;
    }

    retval = TRUE;

  err:
    /* once transcoding is done, store the list length */
    length = ((*dst) - lenPtr) - 2;
    FB_WRITE_U16(lenPtr, length);

    /* reset the templates */
    fbuf->int_tid = tempIntID;
    fbuf->ext_tid = tempExtID;
    fbuf->int_tmpl = tempIntPtr;
    fbuf->ext_tmpl = tempExtPtr;

    return retval;
}


static gboolean
fbDecodeSubTemplateList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err)
{
    fbSubTemplateList_t *subTemplateList;
    fbTemplate_t        *extTemplate     = NULL;
    fbTemplate_t        *intTemplate     = NULL;
    size_t        srcLen;
    size_t        dstLen;
    size_t        recLen;
    uint16_t      tempIntID;
    uint16_t      tempExtID;
    fbTemplate_t *tempIntPtr;
    fbTemplate_t *tempExtPtr;
    gboolean      rc              = TRUE;
    uint8_t      *subTemplateDst  = NULL;
    uint16_t      int_tid = 0;
    uint16_t      ext_tid;
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    fbSubTemplateList_t subTemplateList_local;
    subTemplateList = &subTemplateList_local;
    memcpy(subTemplateList, *dst, sizeof(fbSubTemplateList_t));
#else
    subTemplateList = (fbSubTemplateList_t *)*dst;
#endif /* if HAVE_ALIGNED_ACCESS_REQUIRED */

    if (subTemplateList->dataLength || subTemplateList->dataPtr) {
        fbSubTemplateListClear(subTemplateList);
    }

    /* decode the length of the list */
    FB_READ_LIST_LENGTH(srcLen, src);
    if (srcLen < 3) {
        fbSubTemplateListCollectorInit(subTemplateList);
        if (0 == srcLen) {
            return TRUE;
        }
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,
                    ("Stated length (%ld) is too small"
                     " for a SubTemplateList header"), (long)srcLen);
        return FALSE;
    }

    if (d_rem) {
        FB_TC_DBC(sizeof(fbSubTemplateList_t), "sub-template-list decode");
    }

    FB_READINCREM_U8(subTemplateList->semantic, src, srcLen);
    FB_READINCREM_U16(ext_tid, src, srcLen);

    /* get the external template and its template-pair */
    if (!fbSessionGetTemplatePair(fbuf->session, ext_tid, &int_tid,
                                  &extTemplate, &intTemplate, err))
    {
        if (extTemplate) {
            /* external tmpl found; error finding the match */
            g_prefix_error(err, "Error decoding SubTemplateList: ");
            return FALSE;
        }
        g_assert(NULL == intTemplate);
        g_clear_error(err);
        g_warning("Skipping SubTemplateList:"
                  " No external template %#010x:%04x Present.",
                  fbSessionGetDomain(fbuf->session), ext_tid);
    }

    if (!intTemplate) {
        /* either no external tmpl or the collector is not interested in the
         * tmpl */
        memset(subTemplateList, 0, sizeof(*subTemplateList));
        goto end;
    }
    if (intTemplate != extTemplate) {
        if (!fBufCheckTemplateDefaultLength(intTemplate, int_tid, err)) {
            return FALSE;
        }
    }

    subTemplateList->tmplID = int_tid;
    subTemplateList->tmpl = intTemplate;
    subTemplateList->recordLength = intTemplate->ie_internal_len;
    subTemplateList->numElements = 0;

    /* cache the fbuf's current templates */
    tempIntID = fbuf->int_tid;
    tempExtID = fbuf->ext_tid;
    tempIntPtr = fbuf->int_tmpl;
    tempExtPtr = fbuf->ext_tmpl;

    /* set the fbuf's templates to those we found from the template pair */
    fbuf->int_tid = int_tid;
    fbuf->ext_tid = ext_tid;
    fbuf->int_tmpl = intTemplate;
    fbuf->ext_tmpl = extTemplate;

    dstLen = subTemplateList->recordLength;
    while (srcLen) {
        subTemplateDst = fbSubTemplateListAddNewElements(subTemplateList, 1);
        recLen = srcLen;
        rc = fbTranscode(fbuf, TRUE, src, subTemplateDst, &recLen,
                         &dstLen, err);
        if (!rc) {
            g_prefix_error(err, "Error Decoding SubTemplateList: ");
            return FALSE;
        }
        g_assert(dstLen == subTemplateList->recordLength);
        srcLen -= recLen;
        src    += recLen;
    }

    /* restore the cached templates */
    fbuf->int_tid = tempIntID;
    fbuf->ext_tid = tempExtID;
    fbuf->int_tmpl = tempIntPtr;
    fbuf->ext_tmpl = tempExtPtr;

  end:
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    memcpy(*dst, subTemplateList, sizeof(fbSubTemplateList_t));
#endif
    *dst += sizeof(fbSubTemplateList_t);
    if (d_rem) {
        *d_rem -= sizeof(fbSubTemplateList_t);
    }
    return TRUE;
}

static gboolean
fbEncodeSubTemplateMultiList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err)
{
    fbSubTemplateMultiList_t      *multiList;
    fbSubTemplateMultiListEntry_t *entry = NULL;
    uint16_t      length;
    uint16_t      i, j;
    size_t        srcLen  = 0;
    size_t        dstLen  = 0;
    uint8_t      *lenPtr  = NULL;
    uint8_t      *entryLenPtr = NULL;
    uint16_t      tempIntID;
    uint16_t      tempExtID;
    fbTemplate_t *tempIntPtr;
    fbTemplate_t *tempExtPtr;
    uint8_t      *srcPtr;
    size_t        srcRem = 0;
    gboolean      retval = FALSE;
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    fbSubTemplateMultiList_t multiList_local;
    multiList = &multiList_local;
    memcpy(multiList, src, sizeof(fbSubTemplateMultiList_t));
#else
    multiList = (fbSubTemplateMultiList_t *)src;
#endif /* if HAVE_ALIGNED_ACCESS_REQUIRED */

    /* calculate total destination length */

    if (!validSubTemplateMultiList(multiList, err)) {
        return FALSE;
    }
    /* Check buffer bounds */
    FB_TC_DBC(4, "multi list header");
    (*d_rem) -= 4;

    FB_WRITEINC_U8(*dst, 255);

    /* set the pointer to the length of this subTemplateMultiList */
    lenPtr = *dst;
    (*dst) += 2;

    FB_WRITEINC_U8(*dst, multiList->semantic);

    /* cache the current templates */
    tempIntID = fbuf->int_tid;
    tempExtID = fbuf->ext_tid;
    tempIntPtr = fbuf->int_tmpl;
    tempExtPtr = fbuf->ext_tmpl;

    entry = multiList->firstEntry;

    for (i = 0; i < multiList->numElements; ++i, ++entry) {
        if (!validSubTemplateMultiListEntry(entry, NULL)) {
            continue;
        }

        /* check to see if there's enough length for the entry header */
        FB_TC_DBC_ERR(4, "multi list entry header");
        (*d_rem) -= 4;

        /* at this point, it's very similar to a subtemplatelist */
        /* template ID */
        FB_WRITEINC_U16(*dst, entry->tmplID);

        /* save template data length location */
        entryLenPtr = *dst;
        (*dst) += 2;

        if (!fBufSetEncodeSubTemplates(fbuf, entry->tmplID, entry->tmplID,
                                       err))
        {
            goto err;
        }

        srcRem = entry->dataLength;
        srcPtr = entry->dataPtr;
        for (j = 0; j < entry->numElements; j++) {
            srcLen = srcRem;
            dstLen = *d_rem;
            if (!fbTranscode(fbuf, FALSE, srcPtr, *dst, &srcLen, &dstLen,
                             err))
            {
                length = *dst - entryLenPtr + 2; /* +2 for template ID */
                FB_WRITE_U16(entryLenPtr, length);
                goto err;
            }
            *dst   += dstLen;
            *d_rem -= dstLen;
            srcPtr += srcLen;
            srcRem -= srcLen;
        }

        length = *dst - entryLenPtr + 2; /* +2 for template ID */
        FB_WRITE_U16(entryLenPtr, length);
    }

    retval = TRUE;

  err:
    /* Write length */
    length = ((*dst) - lenPtr) - 2;
    FB_WRITE_U16(lenPtr, length);

    /* restore the cached templates */
    fbuf->int_tid = tempIntID;
    fbuf->ext_tid = tempExtID;
    fbuf->int_tmpl = tempIntPtr;
    fbuf->ext_tmpl = tempExtPtr;

    return retval;
}

static gboolean
fbDecodeSubTemplateMultiList(
    uint8_t   *src,
    uint8_t  **dst,
    uint32_t  *d_rem,
    fBuf_t    *fbuf,
    GError   **err)
{
    fbSubTemplateMultiList_t *multiList;
    fbTemplate_t             *extTemplate = NULL, *intTemplate = NULL;
    size_t        srcLen;
    size_t        dstLen;
    size_t        recLen;
    uint16_t      tempIntID;
    uint16_t      tempExtID;
    fbTemplate_t *tempIntPtr;
    fbTemplate_t *tempExtPtr;
    gboolean      rc = TRUE;
    uint8_t      *srcWalker  = NULL;
    fbSubTemplateMultiListEntry_t *entry = NULL;
    uint16_t      entryLength;
    uint16_t      i;
    uint16_t      int_tid = 0;
    uint16_t      ext_tid;
    uint8_t      *thisTemplateDst;
#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    fbSubTemplateMultiList_t multiList_local;
    multiList = &multiList_local;
    memcpy(multiList, *dst, sizeof(fbSubTemplateMultiList_t));
#else
    multiList = (fbSubTemplateMultiList_t *)*dst;
#endif /* if HAVE_ALIGNED_ACCESS_REQUIRED */

    FB_READ_LIST_LENGTH(srcLen, src);
    if (srcLen == 0) {
        /* FIXME: Should we use Clear or memset to zero? */
        fbSubTemplateMultiListClear(multiList);
        return TRUE;
    }

    if (d_rem) {
        FB_TC_DBC(sizeof(fbSubTemplateMultiList_t),
                  "sub-template-multi-list decode");
    }

    FB_READINCREM_U8(multiList->semantic, src, srcLen);

    /* cache the current templates */
    tempIntID = fbuf->int_tid;
    tempExtID = fbuf->ext_tid;
    tempIntPtr = fbuf->int_tmpl;
    tempExtPtr = fbuf->ext_tmpl;

    multiList->numElements = 0;

    /* figure out how many elements are here; start at the offset of the
     * length (+2) and move forward by the advertised length each time to land
     * on the next length */
    srcWalker = src + 2;
    while (srcLen > (size_t)(srcWalker - src) + 2u) {
        FB_READ_U16(entryLength, srcWalker);
        if (entryLength < 4) {
            g_warning("Invalid Length (%d) in STML Record", entryLength);
            break;
        }
        srcWalker += entryLength;
        multiList->numElements++;
    }

    multiList->firstEntry = g_slice_alloc0(
        multiList->numElements * sizeof(fbSubTemplateMultiListEntry_t));
    entry = multiList->firstEntry;

    for (i = 0; i < multiList->numElements; ++i, ++entry) {
        FB_READINC_U16(ext_tid, src);
        FB_READINC_U16(entryLength, src);
        entryLength -= 4;

        /* get the external template and its template-pair */
        if (!fbSessionGetTemplatePair(fbuf->session, ext_tid, &int_tid,
                                      &extTemplate, &intTemplate, err))
        {
            if (extTemplate) {
                /* external tmpl found; error finding the match */
                g_prefix_error(err, "Error decoding SubTemplateMultiList: ");
                return FALSE;
            }
            g_assert(NULL == intTemplate);
            g_clear_error(err);
            g_warning("Skipping SubTemplateMultiList Item:"
                      " No external template %#010x:%04x Present.",
                      fbSessionGetDomain(fbuf->session), ext_tid);
        }

        if (!intTemplate) {
            /* either no external template or the collector is not
             * interested in the template */
            src += entryLength;
            continue;
        }
        if (intTemplate != extTemplate) {
            if (!fBufCheckTemplateDefaultLength(intTemplate, int_tid, err)) {
                return FALSE;
            }
        }

        entry->tmpl = intTemplate;
        entry->tmplID = int_tid;
        entry->recordLength = intTemplate->ie_internal_len;

        if (!entryLength) {
            continue;
        }

        /* set the fbuf's templates to those we found from the template pair */
        fbuf->int_tid = int_tid;
        fbuf->ext_tid = ext_tid;
        fbuf->int_tmpl = intTemplate;
        fbuf->ext_tmpl = extTemplate;

        dstLen = entry->recordLength;
        while (entryLength) {
            thisTemplateDst
                = fbSubTemplateMultiListEntryAddNewElements(entry, 1);
            recLen = entryLength;
            rc = fbTranscode(fbuf, TRUE, src, thisTemplateDst, &recLen,
                             &dstLen, err);
            if (!rc) {
                /* restore the cached templates */
                fbuf->int_tid = tempIntID;
                fbuf->ext_tid = tempExtID;
                fbuf->int_tmpl = tempIntPtr;
                fbuf->ext_tmpl = tempExtPtr;
                g_prefix_error(err, "Error Decoding SubTemplateMultiList: ");
                return FALSE;
            }
            g_assert(dstLen == entry->recordLength);
            entryLength -= recLen;
            src         += recLen;
        }
    }

    /* restore the cached templates */
    fbuf->int_tid = tempIntID;
    fbuf->ext_tid = tempExtID;
    fbuf->int_tmpl = tempIntPtr;
    fbuf->ext_tmpl = tempExtPtr;

#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    memcpy(*dst, multiList, sizeof(fbSubTemplateMultiList_t));
#endif
    *dst += sizeof(fbSubTemplateMultiList_t);
    if (d_rem) {
        *d_rem -= sizeof(fbSubTemplateMultiList_t);
    }
    return TRUE;
}


/**
 * fbTranscode
 *
 *
 *
 *  When called, the template information on `fbuf` must be set to those to
 *  use for transcoding.  If `decode` is TRUE, decodes the incoming data in
 *  `s_base` to an internal represenation in `d_base`, otherwise encodes the
 *  internal data in `s_base` to the external represenation in `d_base` for
 *  export.  The caller should set the referents of `s_len` and `d_len` to the
 *  octets of data to process and the available space, respectively.  On
 *  return, the referents are the octets of data read and the amount of space
 *  consumed, respectively.
 *
 */
static gboolean
fbTranscode(
    fBuf_t    *fbuf,
    gboolean   decode,
    uint8_t   *s_base,
    uint8_t   *d_base,
    size_t    *s_len,
    size_t    *d_len,
    GError   **err)
{
    fbTranscodePlan_t *tcplan;
    fbTemplate_t      *s_tmpl, *d_tmpl;
    ssize_t            s_len_offset;
    uint16_t          *offsets;
    uint8_t           *dp;
    uint32_t           s_off, d_rem, i;
    fbTemplateField_t *s_ie, *d_ie;
    gboolean           ok = TRUE;

    /* initialize walk of dest buffer */
    dp = d_base; d_rem = *d_len;
    /* select templates for transcode */
    if (decode) {
        s_tmpl = fbuf->ext_tmpl;
        d_tmpl = fbuf->int_tmpl;
    } else {
        s_tmpl = fbuf->int_tmpl;
        d_tmpl = fbuf->ext_tmpl;
    }

    /* get a transcode plan */
    tcplan = fbTranscodePlan(fbuf, s_tmpl, d_tmpl);

    /* get source record length and offsets */
    if ((s_len_offset = fbTranscodeOffsets(s_tmpl, s_base, *s_len,
                                           decode, &offsets, err)) < 0)
    {
        return FALSE;
    }
    *s_len = s_len_offset;
#if FB_DEBUG_TC && FB_DEBUG_RD && FB_DEBUG_WR
    fBufDebugTranscodePlan(tcplan);
    if (offsets) {fBufDebugTranscodeOffsets(s_tmpl, offsets);}
    fBufDebugHex("tsrc", s_base, *s_len);
#elif FB_DEBUG_TC && FB_DEBUG_RD
    if (decode) {
        fBufDebugTranscodePlan(tcplan);
        /*        if (offsets) fBufDebugTranscodeOffsets(s_tmpl, offsets);
         *        fBufDebugHex("tsrc", s_base, *s_len);*/
    }
    if (!decode) {
        fBufDebugTranscodePlan(tcplan);
        if (offsets) {fBufDebugTranscodeOffsets(s_tmpl, offsets);}
        fBufDebugHex("tsrc", s_base, *s_len);
    }
#endif /* if FB_DEBUG_TC && FB_DEBUG_RD && FB_DEBUG_WR */

    /* iterate over destination IEs, copying from source */
    if (decode) {
        for (i = 0; ok && i < d_tmpl->ie_count; i++) {
            /* Get pointers to information elements and source offset */
            d_ie = d_tmpl->ie_ary[i];
            s_ie = ((tcplan->si[i] == FB_TCPLAN_NULL) ?
                    NULL : s_tmpl->ie_ary[tcplan->si[i]]);
            s_off = s_ie ? offsets[tcplan->si[i]] : 0;
            if (s_ie == NULL) {
                /* Null source */
                uint32_t null_len;
                if (d_ie->len != FB_IE_VARLEN) {
                    null_len = d_ie->len;
                } else if (d_ie->canon->type == FB_BASIC_LIST) {
                    null_len = sizeof(fbBasicList_t);
                } else if (d_ie->canon->type == FB_SUB_TMPL_LIST) {
                    null_len = sizeof(fbSubTemplateList_t);
                } else if (d_ie->canon->type == FB_SUB_TMPL_MULTI_LIST) {
                    null_len = sizeof(fbSubTemplateMultiList_t);
                } else {
                    null_len = sizeof(fbVarfield_t);
                }
                ok = fbTranscodeZero(&dp, &d_rem, null_len, err);
            } else {
                switch (fbTemplateFieldGetType(d_ie)) {
                  case FB_BOOL:
                  case FB_UINT_8:
                  case FB_INT_8:
                  case FB_IP6_ADDR:
                  case FB_MAC_ADDR:
                    ok = fbDecodeFixed(s_base + s_off, &dp, &d_rem,
                                       s_ie->len, d_ie->len,
                                       0, 0, err);
                    break;
                  case FB_UINT_16:
                  case FB_UINT_32:
                  case FB_UINT_64:
                  case FB_IP4_ADDR:
                  case FB_DT_SEC:
                  case FB_DT_MILSEC:
                  case FB_DT_MICROSEC:
                  case FB_DT_NANOSEC:
                  case FB_FLOAT_32:
                    ok = fbDecodeFixed(s_base + s_off, &dp, &d_rem,
                                       s_ie->len, d_ie->len,
                                       1, 0, err);
                    break;
                  case FB_INT_16:
                  case FB_INT_32:
                  case FB_INT_64:
                    ok = fbDecodeFixed(s_base + s_off, &dp, &d_rem,
                                       s_ie->len, d_ie->len,
                                       1, 1, err);
                    break;
                  case FB_FLOAT_64:
                    ok = fbDecodeDouble(s_base + s_off, &dp, &d_rem,
                                        s_ie->len, d_ie->len, err);
                    break;
                  case FB_STRING:
                  case FB_OCTET_ARRAY:
                    if ((FB_IE_VARLEN == s_ie->len) ^
                        (FB_IE_VARLEN == d_ie->len))
                    {
                        g_set_error(
                            err, FB_ERROR_DOMAIN, FB_ERROR_IMPL,
                            "Transcoding between fixed and varlen IE "
                            "is not supported by this version of libfixbuf.");
                        ok = FALSE;
                        goto end;
                    }
                    if (FB_IE_VARLEN == s_ie->len) {
                        ok = fbDecodeVarfield(s_base + s_off, &dp,
                                              &d_rem, err);
                    } else {
                        ok = fbDecodeFixed(s_base + s_off, &dp, &d_rem,
                                           s_ie->len, d_ie->len,
                                           0, 0, err);
                    }
                    break;
                  case FB_BASIC_LIST:
                    ok = fbDecodeBasicList(fbuf->ext_tmpl->model,
                                           s_base + s_off,
                                           &dp, &d_rem, fbuf,
                                           err);
                    break;
                  case FB_SUB_TMPL_LIST:
                    ok = fbDecodeSubTemplateList(s_base + s_off,
                                                 &dp, &d_rem, fbuf, err);
                    break;
                  case FB_SUB_TMPL_MULTI_LIST:
                    ok = fbDecodeSubTemplateMultiList(s_base + s_off,
                                                      &dp, &d_rem, fbuf, err);
                    break;
                }
            }
        }
    } else {
        /* decode == FALSE */
        for (i = 0; ok && i < d_tmpl->ie_count; i++) {
            /* Get pointers to information elements and source offset */
            d_ie = d_tmpl->ie_ary[i];
            s_ie = ((tcplan->si[i] == FB_TCPLAN_NULL) ?
                    NULL : s_tmpl->ie_ary[tcplan->si[i]]);
            s_off = s_ie ? offsets[tcplan->si[i]] : 0;
            if (s_ie == NULL) {
                /* Null source */
                uint32_t null_len;
                if (d_ie->len == FB_IE_VARLEN) {
                    null_len = 1;
                } else {
                    null_len = d_ie->len;
                }
                ok = fbTranscodeZero(&dp, &d_rem, null_len, err);
            } else {
                switch (fbTemplateFieldGetType(d_ie)) {
                  case FB_BOOL:
                  case FB_UINT_8:
                  case FB_INT_8:
                  case FB_IP6_ADDR:
                  case FB_MAC_ADDR:
                    ok = fbEncodeFixed(s_base + s_off, &dp, &d_rem,
                                       s_ie->len, d_ie->len,
                                       0, 0, err);
                    break;
                  case FB_UINT_16:
                  case FB_UINT_32:
                  case FB_UINT_64:
                  case FB_IP4_ADDR:
                  case FB_DT_SEC:
                  case FB_DT_MILSEC:
                  case FB_DT_MICROSEC:
                  case FB_DT_NANOSEC:
                  case FB_FLOAT_32:
                    ok = fbEncodeFixed(s_base + s_off, &dp, &d_rem,
                                       s_ie->len, d_ie->len,
                                       1, 0, err);
                    break;
                  case FB_INT_16:
                  case FB_INT_32:
                  case FB_INT_64:
                    ok = fbEncodeFixed(s_base + s_off, &dp, &d_rem,
                                       s_ie->len, d_ie->len,
                                       1, 1, err);
                    break;
                  case FB_FLOAT_64:
                    ok = fbEncodeDouble(s_base + s_off, &dp, &d_rem,
                                        s_ie->len, d_ie->len, err);
                    break;
                  case FB_STRING:
                  case FB_OCTET_ARRAY:
                    if ((FB_IE_VARLEN == s_ie->len) ^
                        (FB_IE_VARLEN == d_ie->len))
                    {
                        g_set_error(
                            err, FB_ERROR_DOMAIN, FB_ERROR_IMPL,
                            "Transcoding between fixed and varlen IE "
                            "not supported by this version of libfixbuf.");
                        ok = FALSE;
                        goto end;
                    }
                    if (FB_IE_VARLEN == s_ie->len) {
                        ok = fbEncodeVarfield(s_base + s_off, &dp,
                                              &d_rem, err);
                    } else {
                        ok = fbEncodeFixed(s_base + s_off, &dp, &d_rem,
                                           s_ie->len, d_ie->len,
                                           0, 0, err);
                    }
                    break;
                  case FB_BASIC_LIST:
                    ok = fbEncodeBasicList(s_base + s_off, &dp,
                                           &d_rem, fbuf, err);
                    break;
                  case FB_SUB_TMPL_LIST:
                    ok = fbEncodeSubTemplateList(s_base + s_off, &dp,
                                                 &d_rem, fbuf, err);
                    break;
                  case FB_SUB_TMPL_MULTI_LIST:
                    ok = fbEncodeSubTemplateMultiList(s_base + s_off, &dp,
                                                      &d_rem, fbuf, err);
                    break;
                }
            }
        }
    }
    if (!ok) {
        goto end;
    }

    /* Return destination length */
    *d_len = dp - d_base;

#if FB_DEBUG_TC && FB_DEBUG_RD && FB_DEBUG_WR
    fBufDebugHex("tdst", d_base, *d_len);
#elif FB_DEBUG_TC && FB_DEBUG_RD
    if (decode) {fBufDebugHex("tdst", d_base, *d_len);}
#elif FB_DEBUG_TC && FB_DEBUG_WR
    if (!decode) {fBufDebugHex("tdst", d_base, *d_len);}
#endif /* if FB_DEBUG_TC && FB_DEBUG_RD && FB_DEBUG_WR */
    /* All done */
  end:
    fbTranscodeFreeVarlenOffsets(s_tmpl, offsets);
    return ok;
}

/*==================================================================
 *
 * Common Buffer Management Functions
 *
 *==================================================================*/


/**
 * fBufRewind
 *
 *
 *
 *
 *
 */
void
fBufRewind(
    fBuf_t  *fbuf)
{
    if (fbuf->collector || fbuf->exporter) {
        /* Reset the buffer */
        fbuf->cp = fbuf->buf;
    } else {
        /* set the buffer to the end of the message */
        fbuf->cp = fbuf->mep;
    }
    fbuf->mep = fbuf->cp;

    /* No message or set headers in buffer */
    fbuf->msgbase = NULL;
    fbuf->setbase = NULL;
    fbuf->sep = NULL;

    /* No records in buffer either */
    fbuf->rc = 0;
}



uint16_t
fBufGetInternalTemplate(
    const fBuf_t  *fbuf)
{
    return fbuf->int_tid;
}


/**
 *  Raise an error for an internal template that uses default lengths.
 */
static gboolean
fBufCheckTemplateDefaultLength(
    const fbTemplate_t  *int_tmpl,
    uint16_t             int_tid,
    GError             **err)
{
    if (int_tmpl->default_length) {
        /* ERROR: Internal templates may not be created with defaulted
         * lengths.  This is to ensure forward compatibility with respect to
         * default element size changes. */
#ifdef FB_ABORT_ON_DEFAULTED_LENGTH
        g_error(("ERROR: Attempt to use internal template %#06" PRIx16
                 " which has an element with a defaulted length"), int_tid);
#endif
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_LAXSIZE,
                    "Attempt to use internal template %#06" PRIx16
                    " which has an element with a defaulted length", int_tid);
        return FALSE;
    }
    return TRUE;
}


/**
 * fBufSetInternalTemplate
 *
 *
 *
 *
 *
 */
gboolean
fBufSetInternalTemplate(
    fBuf_t    *fbuf,
    uint16_t   int_tid,
    GError   **err)
{
    g_assert(int_tid >= FB_TID_MIN_DATA);

    /* Look up new internal template if necessary */
    if (fbuf->int_tid != int_tid ||
        fbSessionIntTmplTableFlagIsSet(fbuf->session))
    {
        fbSessionClearIntTmplTableFlag(fbuf->session);
        fbuf->int_tid = int_tid;
        fbuf->int_tmpl = fbSessionGetTemplate(fbuf->session, TRUE, int_tid,
                                              err);
        if (!fbuf->int_tmpl) {
            fbuf->int_tid = 0;
            return FALSE;
        }
        if (!fBufCheckTemplateDefaultLength(fbuf->int_tmpl, int_tid, err)) {
            fbuf->int_tid = 0;
            return FALSE;
        }
    }

#if FB_DEBUG_TMPL
    fbTemplateDebug("int", int_tid, fbuf->int_tmpl);
#endif
    return TRUE;
}

/**
 * fBufSetAutomaticNextMessage
 *
 *
 *
 *
 *
 */
void
fBufSetAutomaticNextMessage(
    fBuf_t    *fbuf,
    gboolean   automatic)
{
    fbuf->auto_next_msg = automatic;
}

void
fBufSetAutomaticMode(
    fBuf_t    *fbuf,
    gboolean   automatic)
{
    fbuf->auto_next_msg = automatic;
}


/* FIXME: Should fBufSetAutomaticElementInsert() take the TID to use for the
 * internal template it creates to ensure the caller does not clobber it?  If
 * so, we should add a new #define to fixbuf that means "use the value of the
 * TID read from the input as the internal TID". */
/**
 * fBufSetAutomaticElementInsert
 *
 *
 */
gboolean
fBufSetAutomaticElementInsert(
    fBuf_t  *fbuf,
    GError **err)
{
    if (!fbuf->auto_rfc5610) {
        fbuf->auto_rfc5610 =
            fbSessionAddInternalRfc5610Template(fbuf->session, err);
    }
    return fbuf->auto_rfc5610;
}

/* FIXME: Remove this. */
gboolean
fBufSetAutomaticInsert(
    fBuf_t  *fbuf,
    GError **err)
{
    return fBufSetAutomaticElementInsert(fbuf, err);
}


/* FIXME: Should fBufSetAutomaticMetadataAttach() take the TIDs to use for the
 * 2 internal templates it creates to ensure the caller does not clobber
 * them?  If so, we should add a new #define to fixbuf that means "use the
 * value of the TIDs read from the input as the internal TIDs". */
/**
 * fBufSetAutomaticMetadataAttach
 *
 *
 */
gboolean
fBufSetAutomaticMetadataAttach(
    fBuf_t  *fbuf,
    GError **err)
{
    if (!fbuf->auto_tmplInfo) {
        fbuf->auto_tmplInfo = fbSessionAddInternalTemplateInfoTemplate(
            fbuf->session, err);
    }
    return fbuf->auto_tmplInfo;
}

/**
 * fBufGetSession
 *
 *
 *
 *
 *
 */
fbSession_t *
fBufGetSession(
    const fBuf_t  *fbuf)
{
    return fbuf->session;
}


/**
 * fBufFree
 *
 *
 *
 *
 *
 */
void
fBufFree(
    fBuf_t  *fbuf)
{
    fbTCPlanEntry_t *entry;

    if (NULL == fbuf) {
        return;
    }

    /* free the tcplans */
    while (fbuf->latestTcplan) {
        entry = fbuf->latestTcplan;

        detachHeadOfDLL((fbDLL_t **)(void *)&(fbuf->latestTcplan), NULL,
                        (fbDLL_t **)(void *)&entry);
        g_free(entry->tcplan->si);

        g_slice_free(fbTranscodePlan_t, entry->tcplan);
        g_slice_free(fbTCPlanEntry_t, entry);
    }
    if (fbuf->exporter) {
        fbExporterFree(fbuf->exporter);
    }
    if (fbuf->collector) {
        fbCollectorRemoveListenerLastBuf(fbuf, fbuf->collector);
        fbCollectorFree(fbuf->collector);
    }

    fbSessionFree(fbuf->session);
    g_slice_free(fBuf_t, fbuf);
}

/*==================================================================
 *
 * Buffer Append (Writer) Functions
 *
 *==================================================================*/

#define FB_APPEND_U16(_val_) FB_WRITEINC_U16(fbuf->cp, _val_);

#define FB_APPEND_U32(_val_) FB_WRITEINC_U32(fbuf->cp, _val_);


/**
 * fBufAppendMessageHeader
 *
 *
 *
 *
 *
 */
static void
fBufAppendMessageHeader(
    fBuf_t  *fbuf)
{
    /* can only append message header at start of buffer */
    g_assert(fbuf->cp == fbuf->buf);

    /* can only append message header if we have an exporter */
    g_assert(fbuf->exporter);

    /* get MTU from exporter */
    fbuf->mep += fbExporterGetMTU(fbuf->exporter);
    g_assert(FB_REM_MSG(fbuf) > FB_MTU_MIN);

    /* set message base pointer to show we have an active message */
    fbuf->msgbase = fbuf->cp;

    /* add version to buffer */
    FB_APPEND_U16(0x000A);

    /* add message length to buffer */
    FB_APPEND_U16(0);

    /* add export time to buffer */
    if (fbuf->extime) {
        FB_APPEND_U32(fbuf->extime);
    } else {
        FB_APPEND_U32(time(NULL));
    }

    /* add sequence number to buffer */
    FB_APPEND_U32(fbSessionGetSequence(fbuf->session));

    /* add observation domain ID to buffer */
    FB_APPEND_U32(fbSessionGetDomain(fbuf->session));

#if FB_DEBUG_WR
    fBufDebugBuffer("amsg", fbuf, 16, TRUE);
#endif
}


/**
 * fBufAppendSetHeader
 *
 *
 * @param fbuf
 * @param err   May be NULL
 */
static gboolean
fBufAppendSetHeader(
    fBuf_t  *fbuf,
    GError **err)
{
    uint16_t set_id, set_minlen;

    /* Select set ID and minimum set size based on special TID */
    if (fbuf->spec_tid) {
        set_id = fbuf->spec_tid;
        set_minlen = 4;
    } else {
        set_id = fbuf->ext_tid;
        set_minlen = (fbuf->ext_tmpl->ie_len + 4);
    }

    /* Need enough space in the message for a set header and a record */
    if (FB_REM_MSG(fbuf) < set_minlen) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,
                    "End of message. "
                    "Overrun on set header append "
                    "(need %u bytes, %u available)",
                    set_minlen, (uint32_t)FB_REM_MSG(fbuf));
        return FALSE;
    }

    /* set set base pointer to show we have an active set */
    fbuf->setbase = fbuf->cp;

    /* add set ID to buffer */
    FB_APPEND_U16(set_id);
    /* add set length to buffer */
    FB_APPEND_U16(0);

#if FB_DEBUG_WR
    fBufDebugBuffer("aset", fbuf, 4, TRUE);
#endif

    return TRUE;
}


/**
 * fBufAppendSetClose
 *
 *
 *
 *
 *
 */
static void
fBufAppendSetClose(
    fBuf_t  *fbuf)
{
    uint16_t setlen;

    /* If there's an active set... */
    if (fbuf->setbase) {
        /* store set length */
        setlen = g_htons(fbuf->cp - fbuf->setbase);
        memcpy(fbuf->setbase + 2, &setlen, sizeof(setlen));

#if FB_DEBUG_WR
        fBufDebugHex("cset", fbuf->setbase, 4);
#endif

        /* deactivate set */
        fbuf->setbase = NULL;
    }
}

uint16_t
fBufGetExportTemplate(
    const fBuf_t  *fbuf)
{
    return fbuf->ext_tid;
}


/**
 * fBufSetExportTemplate
 *
 *
 *
 *
 *
 */
gboolean
fBufSetExportTemplate(
    fBuf_t    *fbuf,
    uint16_t   ext_tid,
    GError   **err)
{
    /* Look up new external template if necessary */
    if (!fbuf->ext_tmpl || fbuf->ext_tid != ext_tid ||
        fbSessionExtTmplTableFlagIsSet(fbuf->session))
    {
        fbSessionClearExtTmplTableFlag(fbuf->session);

        fbuf->ext_tid = ext_tid;
        fbuf->ext_tmpl = fbSessionGetTemplate(fbuf->session, FALSE, ext_tid,
                                              err);
        if (!fbuf->ext_tmpl) {return FALSE;}

        /* Change of template means new set */
        fBufAppendSetClose(fbuf);
    }

#if FB_DEBUG_TMPL
    fbTemplateDebug("ext", ext_tid, fbuf->ext_tmpl);
#endif

    /* If we're here we're done. */
    return TRUE;
}


gboolean
fBufSetTemplatesForExport(
    fBuf_t    *fbuf,
    uint16_t   tid,
    GError   **err)
{
    return (fBufSetExportTemplate(fbuf, tid, err)
            && fBufSetInternalTemplate(fbuf, tid, err));
}


#if 0
/**
 *  Queries the Session for a template pair for `ext_tid`, sets the referent
 *  of `int_tid` to its value, and updates the referent of `int_tmpl`.
 *
 *  Specifically, if the paired template is 0, sets `int_tmpl` to NULL.
 *  Otherwise, checks if the Session has an internal template with the
 *  specified ID.  If no template is found and the paired template ID equals
 *  the external ID, sets `int_tmpl` to `ext_tmpl`.
 *
 *  Returns FALSE when a non-zero template pair has been specified, the
 *  template IDs are different, and no internal template with the specified ID
 *  exists in the Session.
 */
static gboolean
fbCheckTemplatePair(
    fBuf_t        *fbuf,
    fbTemplate_t  *ext_tmpl,
    fbTemplate_t **int_tmpl,
    uint16_t       ext_tid,
    uint16_t      *int_tid,
    GError       **err)
{
    *int_tid = fbSessionLookupTemplatePair(fbuf->session, ext_tid);
    if (0 == *int_tid) {
        *int_tmpl = NULL;
    } else {
        *int_tmpl = fbSessionGetTemplate(fbuf->session, TRUE, *int_tid, NULL);
        if (*int_tmpl) {
            if (!fBufCheckTemplateDefaultLength(*int_tmpl, *int_tid, err)) {
                return FALSE;
            }
        } else {
            if (*int_tid != ext_tid) {
                g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_TMPL,
                            "Cannot find internal template %#06x paired with"
                            " external template %#06x",
                            *int_tid, ext_tid);
                return FALSE;
            }
            *int_tmpl = ext_tmpl;
        }
    }

    return TRUE;
}
#endif  /* 0 */


/**
 *  Sets the external and internal templates on `fbuf` for encoding an STL
 *  or STMLEntry.  If `int_tid` does not reference an internal template and
 *  `int_tid` equals `ext_tid`, sets the internal template to the external
 *  template.
 *
 *  It is an error if `ext_tid` does not reference a template or if `int_tid`
 *  does not equal `ext_tid` and does not reference an internal template.
 */
static gboolean
fBufSetEncodeSubTemplates(
    fBuf_t    *fbuf,
    uint16_t   ext_tid,
    uint16_t   int_tid,
    GError   **err)
{
    fbuf->ext_tmpl = fbSessionGetTemplate(fbuf->session, FALSE, ext_tid, err);
    if (!fbuf->ext_tmpl) {
        return FALSE;
    }
    fbuf->ext_tid = ext_tid;

    fbuf->int_tmpl = fbSessionGetTemplate(fbuf->session, TRUE, int_tid, err);
    if (fbuf->int_tmpl) {
        if (!fBufCheckTemplateDefaultLength(fbuf->int_tmpl, int_tid, err)) {
            return FALSE;
        }
    } else {
        if (int_tid != ext_tid) {
            return FALSE;
        }
        g_clear_error(err);
        fbuf->int_tmpl = fbuf->ext_tmpl;
    }
    fbuf->int_tid = int_tid;

    return TRUE;
}


/**
 * fBufRemoveTemplateTcplan
 *
 */
void
fBufRemoveTemplateTcplan(
    fBuf_t              *fbuf,
    const fbTemplate_t  *tmpl)
{
    fbTCPlanEntry_t *entry;
    fbTCPlanEntry_t *otherEntry;

    if (!fbuf || !tmpl) {
        return;
    }

    entry = fbuf->latestTcplan;

    while (entry) {
        if (entry->tcplan->s_tmpl == tmpl ||
            entry->tcplan->d_tmpl == tmpl)
        {
            if (entry == fbuf->latestTcplan) {
                otherEntry = NULL;
            } else {
                otherEntry = entry->next;
            }

            detachThisEntryOfDLL((fbDLL_t **)(void *)(&(fbuf->latestTcplan)),
                                 NULL,
                                 (fbDLL_t *)entry);

            g_free(entry->tcplan->si);

            g_slice_free(fbTranscodePlan_t, entry->tcplan);
            g_slice_free(fbTCPlanEntry_t, entry);

            if (otherEntry) {
                entry = otherEntry;
            } else {
                entry = fbuf->latestTcplan;
            }
        } else {
            entry = entry->next;
        }
    }
}

/**
 * fBufAppendTemplateSingle
 *
 *
 *
 *
 * @param  err  May be NULL
 */
static gboolean
fBufAppendTemplateSingle(
    fBuf_t              *fbuf,
    uint16_t             tmpl_id,
    const fbTemplate_t  *tmpl,
    gboolean             revoked,
    GError             **err)
{
    uint16_t spec_tid, tmpl_len, ie_count, scope_count;
    int      i;

    /* Force message closed to start a new template message */
    if (!fbuf->spec_tid) {
        fbuf->spec_tid = (tmpl->scope_count) ? FB_TID_OTS : FB_TID_TS;
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,
                    "End of message. "
                    "Must start new message for template export.");
        return FALSE;
    }

    /* Start a new message if necessary */
    if (!fbuf->msgbase) {
        fBufAppendMessageHeader(fbuf);
    }

    /* Check for set ID change */
    spec_tid = (tmpl->scope_count) ? FB_TID_OTS : FB_TID_TS;
    if (fbuf->spec_tid != spec_tid) {
        fbuf->spec_tid = spec_tid;
        fBufAppendSetClose(fbuf);
    }

    /* Start a new set if necessary */
    if (!fbuf->setbase) {
        if (!fBufAppendSetHeader(fbuf, err)) {return FALSE;}
    }

    /*
     * Calculate template length and IE count based on whether this
     * is a revocation.
     */
    if (revoked) {
        tmpl_len = 4;
        ie_count = 0;
        scope_count = 0;
    } else {
        tmpl_len = tmpl->tmpl_len;
        ie_count = tmpl->ie_count;
        scope_count = tmpl->scope_count;
    }

    /* Ensure we have enough space for the template in the message */
    if (FB_REM_MSG(fbuf) < tmpl_len) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,
                    "End of message. "
                    "Overrun on template append "
                    "(need %u bytes, %u available)",
                    tmpl_len, (uint32_t)FB_REM_MSG(fbuf));
        return FALSE;
    }

    /* Copy the template header to the message */
    FB_APPEND_U16(tmpl_id);

    FB_APPEND_U16(ie_count);

    /* Copy scope IE count if present */
    if (scope_count) {
        FB_APPEND_U16(scope_count);
    }

    /* Now copy information element specifiers to the buffer */
    for (i = 0; i < ie_count; i++) {
        if (tmpl->ie_ary[i]->canon->ent) {
            FB_APPEND_U16(IPFIX_ENTERPRISE_BIT | tmpl->ie_ary[i]->canon->num);
            FB_APPEND_U16(tmpl->ie_ary[i]->len);
            FB_APPEND_U32(tmpl->ie_ary[i]->canon->ent);
        } else {
            FB_APPEND_U16(tmpl->ie_ary[i]->canon->num);
            FB_APPEND_U16(tmpl->ie_ary[i]->len);
        }
    }

#if FB_DEBUG_TMPL
    fbTemplateDebug("apd", tmpl_id, tmpl);
#endif
#if FB_DEBUG_WR
    fBufDebugBuffer("atpl", fbuf, tmpl_len, TRUE);
#endif

    /* Done */
    return TRUE;
}


/**
 * fBufAppendTemplate
 *
 *
 *
 *
 *
 */
gboolean
fBufAppendTemplate(
    fBuf_t              *fbuf,
    uint16_t             tmpl_id,
    const fbTemplate_t  *tmpl,
    gboolean             revoked,
    GError             **err)
{
    GError *child_err = NULL;

    /* Attempt single append */
    if (fBufAppendTemplateSingle(fbuf, tmpl_id, tmpl, revoked, &child_err)) {
        return TRUE;
    }

    /* Fail if not EOM or not automatic */
    if (!g_error_matches(child_err, FB_ERROR_DOMAIN, FB_ERROR_EOM) ||
        !fbuf->auto_next_msg)
    {
        g_propagate_error(err, child_err);
        return FALSE;
    }

    /* Retryable. Clear error. */
    g_clear_error(&child_err);

    /* Emit message */
    if (!fBufEmit(fbuf, err)) {return FALSE;}

    /* Retry single append */
    return fBufAppendTemplateSingle(fbuf, tmpl_id, tmpl, revoked, err);
}


/**
 * fBufAppendSingle
 *
 *
 *
 *
 *
 */
static gboolean
fBufAppendSingle(
    fBuf_t   *fbuf,
    uint8_t  *recbase,
    size_t    recsize,
    GError  **err)
{
    size_t bufsize;

    /* Buffer must have active templates */
    g_assert(fbuf->int_tmpl);
    g_assert(fbuf->ext_tmpl);

    /* Force message closed to finish any active template message */
    if (fbuf->spec_tid) {
        fbuf->spec_tid = 0;
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,
                    "End of message. "
                    "Must start new message after template export.");
        return FALSE;
    }

    /* Start a new message if necessary */
    if (!fbuf->msgbase) {
        fBufAppendMessageHeader(fbuf);
    }

    /* Cancel special set mode if necessary */
    if (fbuf->spec_tid) {
        fbuf->spec_tid = 0;
        fBufAppendSetClose(fbuf);
    }

    /* Start a new set if necessary */
    if (!fbuf->setbase) {
        if (!fBufAppendSetHeader(fbuf, err)) {
            return FALSE;
        }
    }

    /* Transcode bytes into buffer */
    bufsize = FB_REM_MSG(fbuf);

    if (!fbTranscode(fbuf, FALSE, recbase, fbuf->cp, &recsize, &bufsize, err)) {
        return FALSE;
    }

    /* Move current pointer forward by number of bytes written */
    fbuf->cp += bufsize;
    /* Increment record count */
    ++(fbuf->rc);

#if FB_DEBUG_WR
    fBufDebugBuffer("arec", fbuf, bufsize, TRUE);
#endif

    /* Done */
    return TRUE;
}


/**
 * fBufAppend
 *
 *
 *
 *
 *
 */
gboolean
fBufAppend(
    fBuf_t   *fbuf,
    uint8_t  *recbase,
    size_t    recsize,
    GError  **err)
{
    GError *child_err = NULL;

    g_assert(recbase);

    /* Attempt single append */
    if (fBufAppendSingle(fbuf, recbase, recsize, &child_err)) {return TRUE;}

    /* Fail if not EOM or not automatic */
    if (!g_error_matches(child_err, FB_ERROR_DOMAIN, FB_ERROR_EOM) ||
        !fbuf->auto_next_msg)
    {
        g_propagate_error(err, child_err);
        return FALSE;
    }

    /* Retryable. Clear error. */
    g_clear_error(&child_err);

    /* Emit message */
    if (!fBufEmit(fbuf, err)) {return FALSE;}

    /* Retry single append */
    return fBufAppendSingle(fbuf, recbase, recsize, err);
}


/**
 * fBufEmit
 *
 *
 *
 *
 *
 */
gboolean
fBufEmit(
    fBuf_t  *fbuf,
    GError **err)
{
    uint16_t msglen;

    /* Short-circuit on no message available */
    if (!fbuf->msgbase) {return TRUE;}

    /* Close current set */
    fBufAppendSetClose(fbuf);

    /* Store message length */
    msglen = g_htons(fbuf->cp - fbuf->msgbase);
    memcpy(fbuf->msgbase + 2, &msglen, sizeof(msglen));

    /* for (i = 0; i < g_ntohs(msglen); i++) { */
    /*     printf("%02x", fbuf->buf[i]); */
    /*     if ((i + 1) % 8 == 0) { */
    /*         printf("\n"); */
    /*     } */
    /* } */
    /* printf("\n\n\n\n\n"); */

#if FB_DEBUG_WR
    fBufDebugHex("emit", fbuf->buf, fbuf->cp - fbuf->msgbase);
#endif
#if FB_DEBUG_LWR
    fprintf(stderr, "emit %u (%04x)\n",
            fbuf->cp - fbuf->msgbase, fbuf->cp - fbuf->msgbase);
#endif

    /* Hand the message content to the exporter */
    if (!fbExportMessage(fbuf->exporter, fbuf->buf,
                         fbuf->cp - fbuf->msgbase, err))
    {
        return FALSE;
    }

    /* Increment next record sequence number */
    fbSessionSetSequence(fbuf->session, fbSessionGetSequence(fbuf->session) +
                         fbuf->rc);

    /* Rewind message */
    fBufRewind(fbuf);

    /* All done */
    return TRUE;
}


/**
 * fBufGetExporter
 *
 *
 *
 *
 *
 */
fbExporter_t *
fBufGetExporter(
    const fBuf_t  *fbuf)
{
    if (fbuf) {
        return fbuf->exporter;
    }

    return NULL;
}


/**
 * fBufSetExporter
 *
 *
 *
 *
 *
 */
void
fBufSetExporter(
    fBuf_t        *fbuf,
    fbExporter_t  *exporter)
{
    g_assert(exporter);

    if (fbuf->collector) {
        fbCollectorFree(fbuf->collector);
        fbuf->collector = NULL;
    }

    if (fbuf->exporter) {
        fbExporterFree(fbuf->exporter);
    }

    fbuf->exporter = exporter;
    fbSessionSetTemplateBuffer(fbuf->session, fbuf);
    fBufRewind(fbuf);
}


/**
 * fBufAllocForExport
 *
 *
 *
 *
 *
 */
fBuf_t *
fBufAllocForExport(
    fbSession_t   *session,
    fbExporter_t  *exporter)
{
    fBuf_t *fbuf = NULL;

    g_assert(session);

    /* Allocate a new buffer */
    fbuf = g_slice_new0(fBuf_t);

    /* Store reference to session */
    fbuf->session = session;

    /* Set up exporter */
    fBufSetExporter(fbuf, exporter);

    /* Buffers are automatic by default */
    fbuf->auto_next_msg = TRUE;

    return fbuf;
}

/**
 * fBufSetExportTime
 *
 *
 *
 *
 *
 */
void
fBufSetExportTime(
    fBuf_t    *fbuf,
    uint32_t   extime)
{
    fbuf->extime = extime;
}

/*==================================================================
 *
 * Buffer Consume (Reader) Functions
 *
 *==================================================================*/

#define FB_CHECK_AVAIL(_op_, _size_)                               \
    if (_size_ > FB_REM_MSG(fbuf)) {                               \
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_EOM,            \
                    "End of message %s "                           \
                    "(need %u bytes, %u available)",               \
                    (_op_), (_size_), (uint32_t)FB_REM_MSG(fbuf)); \
        return FALSE;                                              \
    }


#define FB_NEXT_U16(_val_) FB_READINC_U16(_val_, fbuf->cp)

#define FB_NEXT_U32(_val_) FB_READINC_U32(_val_, fbuf->cp)


/**
 * fBufNextMessage
 *
 *
 *
 *
 *
 */
gboolean
fBufNextMessage(
    fBuf_t  *fbuf,
    GError **err)
{
    size_t   msglen;
    uint16_t mh_version, mh_len;
    uint32_t ex_sequence, mh_sequence, mh_domain;

    /* Need a collector */
    /*g_assert(fbuf->collector);*/
    /* Clear external template */
    fbuf->ext_tid = 0;
    fbuf->ext_tmpl = NULL;

    /* Rewind the buffer before reading a new message */
    fBufRewind(fbuf);

    /* Read next message from the collector */
    if (fbuf->collector) {
        msglen = sizeof(fbuf->buf);
        if (!fbCollectMessage(fbuf->collector, fbuf->buf, &msglen, err)) {
            return FALSE;
        }
    } else {
        if (fbuf->buflen) {
            if (!fbCollectMessageBuffer(fbuf->cp, fbuf->buflen, &msglen, err)) {
                return FALSE;
            }
        } else {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_BUFSZ,
                        "Buffer length = 0");
            return FALSE;
        }
        /* subtract the next message from the total buffer length */
        fbuf->buflen -= msglen;
    }

    /* Set the message end pointer */
    fbuf->mep = fbuf->cp + msglen;

#if FB_DEBUG_RD
    fBufDebugHex("read", fbuf->buf, msglen);
#endif
#if FB_DEBUG_LWR
    fprintf(stderr, "read %lu (%04lx)\n", msglen, msglen);
#endif

    /* Make sure we have at least a message header */
    FB_CHECK_AVAIL("reading message header", 16);

#if FB_DEBUG_RD
    fBufDebugBuffer("rmsg", fbuf, 16, FALSE);
#endif
    /* Read and verify version */
    FB_NEXT_U16(mh_version);
    if (mh_version != 0x000A) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                    "Illegal IPFIX Message version %#06x; "
                    "input is probably not an IPFIX Message stream.",
                    mh_version);
        return FALSE;
    }

    /* Read and verify message length */
    FB_NEXT_U16(mh_len);

    if (mh_len != msglen) {
        if (NULL != fbuf->collector) {
            if (!fbCollectorHasTranslator(fbuf->collector)) {
                g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                            "IPFIX Message length mismatch "
                            "(message size %u, buffer has %u)",
                            mh_len, (uint32_t)msglen);
                return FALSE;
            }
        } else {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                        "IPFIX Message length mismatch "
                        "(message size %u, buffer has %u)",
                        mh_len, (uint32_t)msglen);
            return FALSE;
        }
    }

    /* Read and store export time */
    FB_NEXT_U32(fbuf->extime);

    /* Read sequence number */
    FB_NEXT_U32(mh_sequence);

    /* Read observation domain ID and reset domain if necessary */
    FB_NEXT_U32(mh_domain);
    fbSessionSetDomain(fbuf->session, mh_domain);

    /* Verify and update sequence number */
    ex_sequence = fbSessionGetSequence(fbuf->session);

    if (ex_sequence != mh_sequence) {
        if (ex_sequence) {
            g_warning("IPFIX Message out of sequence "
                      "(in domain %#010x, expected %#010x, got %#010x)",
                      fbSessionGetDomain(fbuf->session), ex_sequence,
                      mh_sequence);
        }
        fbSessionSetSequence(fbuf->session, mh_sequence);
    }

    /*
     * We successfully read a message header.
     * Set message base pointer to start of message.
     */
    fbuf->msgbase = fbuf->cp - 16;

    return TRUE;
}


/**
 * fBufSkipCurrentSet
 *
 *
 *
 *
 *
 */
static void
fBufSkipCurrentSet(
    fBuf_t  *fbuf)
{
    if (fbuf->setbase) {
        fbuf->cp += FB_REM_SET(fbuf);
        fbuf->setbase = NULL;
        fbuf->sep = NULL;
    }
}


/**
 * fBufNextSetHeader
 *
 *
 *
 *  Reads an IPFIX Set Header.  Sets the `setbase` and `sep` members of `fbuf`
 *  to the start and end of the set.
 *
 *  If a template set, sets the `spec_tid` member on `fbuf` to FB_TID_TS or
 *  FB_TID_OTS and leaves the `ext_tid` and `ext_tmpl` members unchanged.
 *
 *  If a data set, sets the `spec_tid` member to 0 and updates `ext_tid` and
 *  `ext_tmpl` to reference the external template.  If the template ID is
 *  missing, moves to the next IPFIX Set.
 *
 *  Returns FALSE when the set length is illegal, when the set length is
 *  larger than the octets available, or when the set ID is illegal.
 */
static gboolean
fBufNextSetHeader(
    fBuf_t  *fbuf,
    GError **err)
{
    uint16_t set_id, setlen;
    GError  *child_err = NULL;

    /* May loop over sets if we're missing templates */
    for (;; ) {
        /* Make sure we have at least a set header */
        FB_CHECK_AVAIL("reading set header", 4);

#if FB_DEBUG_RD
        fBufDebugBuffer("rset", fbuf, 4, FALSE);
#endif
        /* Read set ID */
        FB_NEXT_U16(set_id);
        /* Read set length */
        FB_NEXT_U16(setlen);
        /* Verify set length is legal */
        if (setlen < 4) {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                        "Illegal IPFIX Set length %hu",
                        setlen);
            return FALSE;
        }

        /* Verify set body fits in the message */
        FB_CHECK_AVAIL("checking set length", setlen - 4);
        /* Set up special set ID or external templates  */
        if (set_id < FB_TID_MIN_DATA) {
            if (!(FB_TID_TS == set_id || FB_TID_OTS == set_id)) {
                g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IPFIX,
                            "Illegal IPFIX Set ID %#06hx", set_id);
                return FALSE;
            }
            fbuf->spec_tid = set_id;
            /* fprintf(stderr, */
            /*         "SET INFO: got a TEMPLATE SET set ID of %x\n", */
            /*         set_id); */
        } else if (!fbuf->ext_tmpl || fbuf->ext_tid != set_id) {
            /* fprintf(stderr, "SET INFO: new TID: was %x, now it's %x\n", */
            /*         fbuf->ext_tid, set_id); */
            fbuf->spec_tid = 0;
            fbuf->ext_tid = set_id;
            fbuf->ext_tmpl = fbSessionGetTemplate(fbuf->session, FALSE,
                                                  set_id, &child_err);
            if (!fbuf->ext_tmpl) {
                if (g_error_matches(child_err, FB_ERROR_DOMAIN,
                                    FB_ERROR_TMPL))
                {
                    /* Merely warn and skip on missing templates */
                    g_warning("Skipping set: %s", child_err->message);
                    g_clear_error(&child_err);
                    fbuf->setbase = fbuf->cp - 4;
                    fbuf->sep = fbuf->setbase + setlen;
                    fBufSkipCurrentSet(fbuf);
                    continue;
                }
            }
        }

        /*
         * We successfully read a set header.
         * Set set base and end pointers.
         */
        fbuf->setbase = fbuf->cp - 4;
        fbuf->sep = fbuf->setbase + setlen;

        return TRUE;
    }
}


/**
 * fBufConsumeTemplateSet
 *
 *
 *
 *
 *
 */
static gboolean
fBufConsumeTemplateSet(
    fBuf_t  *fbuf,
    GError **err)
{
    unsigned int    required = 0;
    uint16_t        tid = 0;
    uint16_t        ie_count, scope_count;
    fbTemplate_t   *tmpl = NULL;
    fbInfoElement_t ex_ie = FB_IE_NULL;
    GError         *child_err = NULL;

    /* Keep reading until the set contains only padding. */
    while (FB_REM_SET(fbuf) >= 4) {
        /* Read the template ID and the IE count */
        FB_NEXT_U16(tid);
        FB_NEXT_U16(ie_count);

        /* FIXME: Add withdrawal template handling here */

        /* check for necessary length assuming no scope or enterprise
         * numbers */
        if ((required = 4 * ie_count) > FB_REM_SET(fbuf)) {
            goto ERROR;
        }

        /* Allocate the template.  If we find an illegal value (eg, scope) in
         * the template's definition, set 'tmpl' to NULL but continue to read
         * the template's data, then move to the next template in the set. */
        tmpl = fbTemplateAlloc(fbSessionGetInfoModel(fbuf->session));

        /* Read scope count if present and not a withdrawal tmpl */
        if (fbuf->spec_tid == FB_TID_OTS && ie_count > 0) {
            FB_NEXT_U16(scope_count);
            /* Check for illegal scope count */
            if (scope_count == 0 || scope_count > ie_count) {
                if (scope_count == 0) {
                    g_warning("Ignoring template %#06x: "
                              "Illegal IPFIX Options Template Scope Count 0",
                              tid);
                } else {
                    g_warning("Ignoring template %#06x: "
                              "Illegal IPFIX Options Template Scope Count "
                              "(scope count %hu, element count %hu)",
                              tid, scope_count, ie_count);
                }
                fbTemplateFreeUnused(tmpl);
                tmpl = NULL;
            }
            /* check for required bytes again */
            if (required > FB_REM_SET(fbuf)) {
                goto ERROR;
            }
        } else {
            scope_count = 0;
        }

        /* Add information elements to the template */
        for ( ; ie_count > 0; --ie_count) {
            /* Read information element specifier from buffer */
            FB_NEXT_U16(ex_ie.num);
            FB_NEXT_U16(ex_ie.len);
            if (ex_ie.num & IPFIX_ENTERPRISE_BIT) {
                /* Check required size for the remainder of the template;
                 * note: PEN is same size as NUM and LEN we just read */
                if ((required = 4 * ie_count) > FB_REM_SET(fbuf)) {
                    goto ERROR;
                }
                ex_ie.num &= ~IPFIX_ENTERPRISE_BIT;
                FB_NEXT_U32(ex_ie.ent);
            } else {
                ex_ie.ent = 0;
            }

            /* Add information element to template */
            if (tmpl && !fbTemplateAppend(tmpl, &ex_ie, &child_err)) {
                g_warning("Ignoring template %#06x: %s",
                          tid, child_err->message);
                g_clear_error(&child_err);
                fbTemplateFreeUnused(tmpl);
                tmpl = NULL;
            }
        }
        if (!tmpl) {
            /* rejected template, but may continue reading set */
            continue;
        }

        /* Set scope count in template */
        if (scope_count) {
            fbTemplateSetOptionsScope(tmpl, scope_count);
        }

        if (!fbSessionAddTemplate(fbuf->session, FALSE, tid, tmpl, NULL, err)) {
            return FALSE;
        }

        /* Invoke the received-new-template callback */
        if (fbSessionGetNewTemplateCallback(fbuf->session)) {
            g_assert(tmpl->app_ctx == NULL);
            (fbSessionGetNewTemplateCallback(fbuf->session))(
                fbuf->session, tid, tmpl,
                fbSessionGetNewTemplateCallbackAppCtx(fbuf->session),
                &(tmpl->tmpl_ctx), &(tmpl->ctx_free));
            if (NULL == tmpl->app_ctx) {
                tmpl->app_ctx =
                    fbSessionGetNewTemplateCallbackAppCtx(fbuf->session);
            }
        }

        /* if the template set on the fbuf has the same tid, reset tmpl
         * so we don't reference the old one if a data set follows */
        if (fbuf->ext_tid == tid) {
            fbuf->ext_tmpl = NULL;
            fbuf->ext_tid = 0;
        }

#if FB_DEBUG_RD
        fBufDebugBuffer("rtpl", fbuf, tmpl->tmpl_len, TRUE);
#endif
    }

    /* Skip any padding at the end of the set */
    fBufSkipCurrentSet(fbuf);

    /* Should set spec_tid to 0 so if next set is data */
    fbuf->spec_tid = 0;

    /* All done */
    return TRUE;

  ERROR:
    /* Not enough data in the template set. */
    g_warning("End of set reading template record %#06x "
              "(need %u bytes, %ld available)",
              tid, required, FB_REM_SET(fbuf));
    if (tmpl) { fbTemplateFreeUnused(tmpl); }
    fBufSkipCurrentSet(fbuf);
    fbuf->spec_tid = 0;
    return TRUE;
}


static gboolean
fBufConsumeRfc5610Set(
    fBuf_t  *fbuf,
    GError **err)
{
    fbInfoElementOptRec_t rec;
    size_t len = sizeof(fbInfoElementOptRec_t);
    uint16_t              tid = fbuf->int_tid;
    size_t bufsize;

    fbuf->int_tmpl = fbSessionGetInternalRfc5610Template(
        fbuf->session, &fbuf->int_tid, err);
    if (!fbuf->int_tmpl) {
        fbuf->int_tid = 0;
        return FALSE;
    }
#if FB_DEBUG_TMPL
    fbTemplateDebug("int", fbuf->int_tid, fbuf->int_tmpl);
#endif

    /* Keep reading until the set contains only padding. */
    while ((bufsize = FB_REM_SET(fbuf)) >= fbuf->ext_tmpl->ie_len) {
        /* Transcode bytes out of buffer */
        if (!fbTranscode(fbuf, TRUE, fbuf->cp, (uint8_t *)&rec, &bufsize, &len,
                         err))
        {
            return FALSE;
        }

        /* This only returns FALSE when PEN is 0 */
        fbInfoElementAddOptRecElement(fbuf->int_tmpl->model, &rec);

        /* Advance current record pointer by bytes read */
        fbuf->cp += bufsize;

        /* Increment record count */
        ++fbuf->rc;
    }

    if (!tid || !fBufSetInternalTemplate(fbuf, tid, NULL)) {
        fbuf->int_tid = 0;
        fbuf->int_tmpl = NULL;
    }

    return TRUE;
}


/*
 *  Consumes the IPFIX Set containing TemplateInfo options records that fbuf's
 *  data buffer is currently at the header of.  `mdRecVersion` is either
 *  FB_TMPL_IS_META_TEMPLATE_V3 for a v3.0.0 style metadata record or
 *  FB_TMPL_IS_META_TEMPLATE_V1 for a v1.8.0 style metadata record.
 */
static gboolean
fBufConsumeTemplateInfoSet(
    fBuf_t        *fbuf,
    unsigned int   mdRecVersion,
    GError       **err)
{
    fbTemplateInfoRecord_t mdRec;
    fbTemplateInfo_t      *mdInfo;
    size_t   len;
    uint16_t tid       = fbuf->int_tid;
    size_t   bufsize;
    GError  *child_err = NULL;

    fbuf->int_tmpl = fbSessionGetInternalTemplateInfoTemplate(
        fbuf->session, &fbuf->int_tid, err);
    if (!fbuf->int_tmpl) {
        fbuf->int_tid = 0;
        return FALSE;
    }
#if FB_DEBUG_TMPL
    fbTemplateDebug("int", fbuf->tid, fbuf->int_tmpl);
#endif

    fbTemplateInfoRecordInit(&mdRec);

    /* Disable template-pairs so the STL used by fbBasicListInfo_t is
     * transcoded using the external template. */
    fbSessionSetTemplatePairsDisabled(fbuf->session, TRUE);

    /* Keep reading until the set contains only padding. */
    while ((bufsize = FB_REM_SET(fbuf)) >= fbuf->ext_tmpl->ie_len) {
        len = sizeof(mdRec);
        if (!fbTranscode(fbuf, TRUE, fbuf->cp, (uint8_t *)&mdRec,
                         &bufsize, &len, err))
        {
            fbSessionSetTemplatePairsDisabled(fbuf->session, FALSE);
            return FALSE;
        }

        /* Convert to an fbTemplateInfo_t and add to session */
        mdInfo = fbTemplateInfoCreateFromRecord(
            &mdRec, mdRecVersion, &child_err);
        if (!mdInfo) {
            g_warning("Ignoring TemplateInfo record: %s",
                      child_err->message);
            g_clear_error(&child_err);
        } else {
            fbSessionSaveTemplateInfo(fbuf->session, mdInfo);
        }
        fbTemplateInfoRecordClear(&mdRec);

        /* Advance current record pointer by bytes read */
        fbuf->cp += bufsize;

        /* Increment record count */
        ++fbuf->rc;
    }

    /* Re-enable the template pairs. */
    fbSessionSetTemplatePairsDisabled(fbuf->session, FALSE);

    /*printf("read %d TMD records\n", fbuf->rc);*/
    if (!tid || !fBufSetInternalTemplate(fbuf, tid, NULL)) {
        fbuf->int_tid = 0;
        fbuf->int_tmpl = NULL;
    }

    return TRUE;
}

/**
 * fBufNextDataSet
 *
 *
 *
 *
 *
 */
static gboolean
fBufNextDataSet(
    fBuf_t  *fbuf,
    GError **err)
{
    /* May have to consume multiple template sets */
    for (;; ) {
        /* Read the next set header */
        if (!fBufNextSetHeader(fbuf, err)) {
            return FALSE;
        }

        /* Check to see if we need to consume a template set */
        if (fbuf->spec_tid) {
            if (!fBufConsumeTemplateSet(fbuf, err)) {
                return FALSE;
            }
            continue;
        }

        /* FIXME: This may be a bit of unnecessary optimization; add
         * FB_TMPL_IS_OPTIONS to the tests below if this next test is
         * removed. */
        /* Return if this is not an options template */
        if (0 == fbTemplateGetOptionsScope(fbuf->ext_tmpl)) {
            return TRUE;
        }

        /* FIXME: Should we change the Session to cache the TIDs of these
         * special templates, so we do not need to run the
         * fbTemplateIsMetadata() for each options record set?  If so, we need
         * to ensure the cached value is removed if the template is
         * revoked. */

        if (fbuf->auto_rfc5610) {
            if (fbTemplateIsMetadata(
                    fbuf->ext_tmpl, FB_TMPL_IS_META_ELEMENT))
            {
                if (!fBufConsumeRfc5610Set(fbuf, err)) {
                    return FALSE;
                }
                continue;
            }
        }

        if (fbuf->auto_tmplInfo) {
            if (!fbTemplateIsMetadata(
                    fbuf->ext_tmpl, FB_TMPL_IS_META_TEMPLATE_V1))
            {
                /* is not V1, therefore also not V3 */
            } else if (fbTemplateIsMetadata(
                           fbuf->ext_tmpl, FB_TMPL_IS_META_TEMPLATE_V3))
            {
                if (!fBufConsumeTemplateInfoSet(
                        fbuf, FB_TMPL_IS_META_TEMPLATE_V3, err))
                {
                    return FALSE;
                }
                continue;
            } else {
                if (!fBufConsumeTemplateInfoSet(
                        fbuf, FB_TMPL_IS_META_TEMPLATE_V1, err))
                {
                    return FALSE;
                }
                continue;
            }
        }

        return TRUE;
    }
}


/**
 * fBufGetCollectionTemplate
 *
 *
 *
 *
 *
 */
fbTemplate_t *
fBufGetCollectionTemplate(
    const fBuf_t  *fbuf,
    uint16_t      *ext_tid)
{
    if (fbuf->ext_tmpl) {
        if (ext_tid) {*ext_tid = fbuf->ext_tid;}
    }
    return fbuf->ext_tmpl;
}


/**
 * fBufNextCollectionTemplateSingle
 *
 *
 *
 *
 *
 */
static fbTemplate_t *
fBufNextCollectionTemplateSingle(
    fBuf_t    *fbuf,
    uint16_t  *ext_tid,
    GError   **err)
{
    /* Read a new message if necessary */
    if (!fbuf->msgbase) {
        if (!fBufNextMessage(fbuf, err)) {
            return FALSE;
        }
    }

    /* Skip any padding at end of current data set */
    if (fbuf->setbase &&
        (FB_REM_SET(fbuf) < fbuf->ext_tmpl->ie_len))
    {
        fBufSkipCurrentSet(fbuf);
    }

    /* Advance to the next data set if necessary */
    if (!fbuf->setbase) {
        if (!fBufNextDataSet(fbuf, err)) {
            return FALSE;
        }
    }

    return fBufGetCollectionTemplate(fbuf, ext_tid);
}


/**
 * fBufNextCollectionTemplate
 *
 *
 *
 *
 *
 */
fbTemplate_t *
fBufNextCollectionTemplate(
    fBuf_t    *fbuf,
    uint16_t  *ext_tid,
    GError   **err)
{
    fbTemplate_t *tmpl;
    GError       *child_err = NULL;

    for (;; ) {
        /* Attempt single record read */
        tmpl = fBufNextCollectionTemplateSingle(fbuf, ext_tid, &child_err);
        if (tmpl) {
            return tmpl;
        }

        /* Finish the message at EOM */
        if (g_error_matches(child_err, FB_ERROR_DOMAIN, FB_ERROR_EOM)) {
            /* Store next expected sequence number */
            fbSessionSetSequence(fbuf->session,
                                 fbSessionGetSequence(fbuf->session) +
                                 fbuf->rc);
            /* Rewind buffer to force next record read
             * to consume a new message. */
            fBufRewind(fbuf);

            /* Clear error and try again in automatic mode */
            if (fbuf->auto_next_msg) {
                g_clear_error(&child_err);
                continue;
            }
        }

        /* Error. Not EOM or not retryable. Fail. */
        g_propagate_error(err, child_err);
        return NULL;
    }
}


/**
 * fBufNextSingle
 *
 *
 *
 *
 *
 */
static gboolean
fBufNextSingle(
    fBuf_t   *fbuf,
    uint8_t  *recbase,
    size_t   *recsize,
    GError  **err)
{
    size_t bufsize;

    /* Buffer must have active internal template */
    g_assert(fbuf->int_tmpl);

    /* Read a new message if necessary */
    if (!fbuf->msgbase) {
        if (!fBufNextMessage(fbuf, err)) {
            return FALSE;
        }
    }

    /* Skip any padding at end of current data set */
    if (fbuf->setbase &&
        (FB_REM_SET(fbuf) < fbuf->ext_tmpl->ie_len))
    {
        fBufSkipCurrentSet(fbuf);
    }

    /* Advance to the next data set if necessary */
    if (!fbuf->setbase) {
        if (!fBufNextDataSet(fbuf, err)) {
            return FALSE;
        }
    }

    /* Transcode bytes out of buffer */
    bufsize = FB_REM_SET(fbuf);

    if (!fbTranscode(fbuf, TRUE, fbuf->cp, recbase, &bufsize, recsize, err)) {
        return FALSE;
    }

    /* Advance current record pointer by bytes read */
    fbuf->cp += bufsize;
    /* Increment record count */
    ++(fbuf->rc);
#if FB_DEBUG_RD
    fBufDebugBuffer("rrec", fbuf, bufsize, TRUE);
#endif
    /* Done */
    return TRUE;
}


/**
 * fBufNext
 *
 *
 *
 *
 *
 */
gboolean
fBufNext(
    fBuf_t   *fbuf,
    uint8_t  *recbase,
    size_t   *recsize,
    GError  **err)
{
    GError *child_err = NULL;

    g_assert(recbase);
    g_assert(recsize);

    for (;; ) {
        /* Attempt single record read */
        if (fBufNextSingle(fbuf, recbase, recsize, &child_err)) {return TRUE;}
        /* Finish the message at EOM */
        if (g_error_matches(child_err, FB_ERROR_DOMAIN, FB_ERROR_EOM)) {
            /* Store next expected sequence number */
            fbSessionSetSequence(fbuf->session,
                                 fbSessionGetSequence(fbuf->session) +
                                 fbuf->rc);
            /* Rewind buffer to force next record read
             * to consume a new message. */
            fBufRewind(fbuf);
            /* Clear error and try again in automatic mode */
            if (fbuf->auto_next_msg) {
                g_clear_error(&child_err);
                continue;
            }
        }

        /* Error. Not EOM or not retryable. Fail. */
        g_propagate_error(err, child_err);
        return FALSE;
    }
}


/*
 * fBufNextRecord
 *
 *
 */
gboolean
fBufNextRecord(
    fBuf_t      *fbuf,
    fbRecord_t  *record,
    GError     **err)
{
    if (!fBufSetInternalTemplate(fbuf, record->tid, err)) {
        return FALSE;
    }
    record->tmpl = fbuf->int_tmpl;
    record->recsize = record->reccapacity;
    return fBufNext(fbuf, record->rec, &record->recsize, err);
}


/*
 *
 * fBufRemaining
 *
 */
size_t
fBufRemaining(
    fBuf_t  *fbuf)
{
    return fbuf->buflen;
}



/**
 * fBufSetBuffer
 *
 *
 */
void
fBufSetBuffer(
    fBuf_t   *fbuf,
    uint8_t  *buf,
    size_t    buflen)
{
    /* not sure if this is necessary, but if these are not null, the
     * appropriate code will not execute*/
    fbuf->collector = NULL;
    fbuf->exporter = NULL;

    fbuf->cp = buf;
    fbuf->mep = fbuf->cp;
    fbuf->buflen = buflen;
}

/**
 * fBufGetCollector
 *
 *
 *
 *
 *
 */
fbCollector_t *
fBufGetCollector(
    const fBuf_t  *fbuf)
{
    return fbuf->collector;
}


/**
 * fBufSetCollector
 *
 *
 *
 *
 *
 */
void
fBufSetCollector(
    fBuf_t         *fbuf,
    fbCollector_t  *collector)
{
    if (fbuf->exporter) {
        fbSessionSetTemplateBuffer(fbuf->session, NULL);
        fbExporterFree(fbuf->exporter);
        fbuf->exporter = NULL;
    }

    if (fbuf->collector) {
        fbCollectorFree(fbuf->collector);
    }

    fbuf->collector = collector;

    fbSessionSetTemplateBuffer(fbuf->session, fbuf);

    fBufRewind(fbuf);
}

/**
 * fBufAllocForCollection
 *
 *
 *
 *
 *
 */
fBuf_t *
fBufAllocForCollection(
    fbSession_t    *session,
    fbCollector_t  *collector)
{
    fBuf_t *fbuf = NULL;

    g_assert(session);

    /* Allocate a new buffer */
    fbuf = g_slice_new0(fBuf_t);

    /* Store reference to session */
    fbuf->session = session;

    fbSessionSetCollector(session, collector);

    /* Set up collection */
    fBufSetCollector(fbuf, collector);

    /* Buffers are automatic by default */

    fbuf->auto_next_msg = TRUE;

    return fbuf;
}

/**
 * fBufSetSession
 *
 */
void
fBufSetSession(
    fBuf_t       *fbuf,
    fbSession_t  *session)
{
    fbuf->session = session;
}

/**
 * fBufGetExportTime
 *
 */
uint32_t
fBufGetExportTime(
    const fBuf_t  *fbuf)
{
    return fbuf->extime;
}

void
fBufInterruptSocket(
    fBuf_t  *fbuf)
{
    fbCollectorInterruptSocket(fbuf->collector);
}

gboolean
fbListSemanticsIsValid(
    uint8_t   semantic)
{
    return (semantic <= 0x04 || semantic == 0xFF);
}



/*
 *  ************************************************************************
 *  fbBasicList_t
 *  ************************************************************************
 */
fbBasicList_t *
fbBasicListAlloc(
    void)
{
    return g_slice_new0(fbBasicList_t);
}

/*
 *  Sets numElements, updates the dataLength, and allocates a new dataPtr for
 *  a basicList.
 */
static void *
fbBasicListAllocData(
    fbBasicList_t  *basicList,
    uint16_t        numElements)
{
    basicList->numElements = numElements;
    basicList->dataLength = numElements * fbSizeofIE(&basicList->field);
    basicList->dataPtr = g_slice_alloc0(basicList->dataLength);
    return (void *)basicList->dataPtr;
}

void *
fbBasicListInit(
    fbBasicList_t          *basicList,
    uint8_t                 semantic,
    const fbInfoElement_t  *infoElement,
    uint16_t                numElements)
{
    return fbBasicListInitWithLength(basicList, semantic, infoElement,
                                     infoElement->len, numElements);
}

void *
fbBasicListInitWithLength(
    fbBasicList_t          *basicList,
    uint8_t                 semantic,
    const fbInfoElement_t  *infoElement,
    uint16_t                elementLength,
    uint16_t                numElements)
{
    basicList->semantic     = semantic;

    if (!infoElement) {
        g_assert(infoElement);
        return NULL;
    }
    /* FIXME: Check the length here:  fbInfoElementCheckTypesSize() */

    basicList->field.canon = infoElement;
    basicList->field.len = elementLength;
    /* basicList->field.memsize = fbSizeofIE(&basicList->field); */
    basicList->field.midx = 0;
    basicList->field.offset = 0;
    basicList->field.tmpl = NULL;
    return fbBasicListAllocData(basicList, numElements);
}

void
fbBasicListCollectorInit(
    fbBasicList_t  *basicList)
{
    memset(&basicList->field, 0, sizeof(basicList->field));
    basicList->semantic = 0;
    basicList->numElements = 0;
    basicList->dataLength = 0;
    basicList->dataPtr = NULL;
}

void
fbBasicListClear(
    fbBasicList_t  *basicList)
{
    g_slice_free1(basicList->dataLength, basicList->dataPtr);
    fbBasicListCollectorInit(basicList);
}

void
fbBasicListClearWithoutFree(
    fbBasicList_t  *basicList)
{
    memset(&basicList->field, 0, sizeof(basicList->field));
    basicList->semantic = 0;
    basicList->numElements = 0;
}

void
fbBasicListFree(
    fbBasicList_t  *basicList)
{
    if (basicList) {
        fbBasicListClear(basicList);
        g_slice_free(fbBasicList_t, basicList);
    }
}

uint16_t
fbBasicListCountElements(
    const fbBasicList_t  *basicList)
{
    return basicList->numElements;
}

uint8_t
fbBasicListGetSemantic(
    const fbBasicList_t  *basicList)
{
    return basicList->semantic;
}

uint16_t
fbBasicListGetElementLength(
    const fbBasicList_t  *basicList)
{
    return basicList->field.len;
}

const fbInfoElement_t *
fbBasicListGetInfoElement(
    const fbBasicList_t  *basicList)
{
    return basicList->field.canon;
}

uint16_t
fbBasicListGetElementIdent(
    const fbBasicList_t  *basicList,
    uint32_t             *pen)
{
    if (pen) {
        *pen = basicList->field.canon->ent;
    }
    return basicList->field.canon->num;
}

const fbTemplateField_t *
fbBasicListGetTemplateField(
    const fbBasicList_t  *basicList)
{
    return &basicList->field;
}

void *
fbBasicListGetDataPtr(
    const fbBasicList_t  *basicList)
{
    return (void *)basicList->dataPtr;
}

void *
fbBasicListGetIndexedDataPtr(
    const fbBasicList_t  *basicList,
    uint16_t              bl_index)
{
    if (bl_index >= basicList->numElements) {
        return NULL;
    }
    return basicList->dataPtr + (bl_index * fbSizeofIE(&basicList->field));
}

gboolean
fbBasicListGetIndexedRecordValue(
    const fbBasicList_t  *basicList,
    uint16_t              index,
    fbRecordValue_t      *value)
{
    const uint8_t *item;

    if (NULL == basicList || index >= basicList->numElements) {
        return FALSE;
    }
    item = basicList->dataPtr + (index * fbSizeofIE(&basicList->field));
    fbRecordFillValue(&basicList->field, item, value);
    return TRUE;
}

void *
fbBasicListGetNextPtr(
    const fbBasicList_t  *basicList,
    const void           *curPtr)
{
    uint16_t ie_len;
    uint8_t *currentPtr = (uint8_t *)curPtr;

    if (!currentPtr) {
        return basicList->dataPtr;
    }
    if (!basicList->numElements || (currentPtr < basicList->dataPtr)) {
        return NULL;
    }

    ie_len = fbSizeofIE(&basicList->field);
    currentPtr += ie_len;

    if (currentPtr >= (basicList->dataPtr + ie_len * basicList->numElements)) {
        return NULL;
    }
    return (void *)currentPtr;
}

void
fbBasicListSetSemantic(
    fbBasicList_t  *basicList,
    uint8_t         semantic)
{
    basicList->semantic = semantic;
}

void *
fbBasicListResize(
    fbBasicList_t  *basicList,
    uint16_t        numElements)
{
    if (numElements == basicList->numElements) {
        return memset(basicList->dataPtr, 0, basicList->dataLength);
    }

    g_slice_free1(basicList->dataLength, basicList->dataPtr);
    return fbBasicListAllocData(basicList, numElements);
}

void *
fbBasicListAddNewElements(
    fbBasicList_t  *basicList,
    uint16_t        additional)
{
    uint16_t oldDataLength = basicList->dataLength;
    uint8_t *oldDataPtr    = basicList->dataPtr;

    fbBasicListAllocData(basicList, basicList->numElements + additional);

    if (oldDataPtr) {
        memcpy(basicList->dataPtr, oldDataPtr, oldDataLength);
        g_slice_free1(oldDataLength, oldDataPtr);
    }

    return (void *)(basicList->dataPtr + oldDataLength);
}


/*
 *  ************************************************************************
 *  fbSubTemplateList_t
 *  ************************************************************************
 */
fbSubTemplateList_t *
fbSubTemplateListAlloc(
    void)
{
    return g_slice_new0(fbSubTemplateList_t);
}

static void *
fbSubTemplateListAllocData(
    fbSubTemplateList_t  *subTemplateList,
    uint16_t              numElements)
{
    subTemplateList->numElements = numElements;
    subTemplateList->dataLength = numElements * subTemplateList->recordLength;
    subTemplateList->dataPtr = g_slice_alloc0(subTemplateList->dataLength);
    return (void *)subTemplateList->dataPtr;
}

void *
fbSubTemplateListInit(
    fbSubTemplateList_t  *subTemplateList,
    uint8_t               semantic,
    uint16_t              tmplID,
    const fbTemplate_t   *tmpl,
    uint16_t              numElements)
{
    g_assert(tmpl);
    g_assert(0 != tmplID);

    subTemplateList->semantic = semantic;
    subTemplateList->tmplID = tmplID;
    subTemplateList->tmpl = tmpl;
    if (!tmpl) {
        return NULL;
    }
    subTemplateList->recordLength = tmpl->ie_internal_len;
    return fbSubTemplateListAllocData(subTemplateList, numElements);
}

void
fbSubTemplateListCollectorInit(
    fbSubTemplateList_t  *subTemplateList)
{
    subTemplateList->semantic = 0;
    subTemplateList->numElements = 0;
    subTemplateList->recordLength = 0;
    subTemplateList->tmplID = 0;
    subTemplateList->tmpl = NULL;
    subTemplateList->dataLength = 0;
    subTemplateList->dataPtr = NULL;
}

void
fbSubTemplateListClear(
    fbSubTemplateList_t  *subTemplateList)
{
    g_slice_free1(subTemplateList->dataLength, subTemplateList->dataPtr);
    fbSubTemplateListCollectorInit(subTemplateList);
}


void
fbSubTemplateListFree(
    fbSubTemplateList_t  *subTemplateList)
{
    if (subTemplateList) {
        fbSubTemplateListClear(subTemplateList);
        g_slice_free(fbSubTemplateList_t, subTemplateList);
    }
}

void
fbSubTemplateListClearWithoutFree(
    fbSubTemplateList_t  *subTemplateList)
{
    subTemplateList->semantic = 0;
    subTemplateList->numElements = 0;
    subTemplateList->recordLength = 0;
    subTemplateList->tmplID = 0;
    subTemplateList->tmpl = NULL;
}


void *
fbSubTemplateListGetDataPtr(
    const fbSubTemplateList_t  *subTemplateList)
{
    return subTemplateList->dataPtr;
}

/* index is 0-based.  Goes from 0 - (numElements-1) */
void *
fbSubTemplateListGetIndexedDataPtr(
    const fbSubTemplateList_t  *subTemplateList,
    uint16_t                    stlIndex)
{
    if (stlIndex >= subTemplateList->numElements) {
        return NULL;
    }

    return ((uint8_t *)(subTemplateList->dataPtr) +
            stlIndex * subTemplateList->recordLength);
}

void *
fbSubTemplateListGetNextPtr(
    const fbSubTemplateList_t  *subTemplateList,
    const void                 *curPtr)
{
    uint8_t *currentPtr = (uint8_t *)curPtr;

    if (!currentPtr) {
        return subTemplateList->dataPtr;
    }
    if (!subTemplateList->numElements ||
        currentPtr < subTemplateList->dataPtr)
    {
        return NULL;
    }

    currentPtr += subTemplateList->recordLength;
    if (currentPtr >=
        (subTemplateList->dataPtr + subTemplateList->dataLength))
    {
        return NULL;
    }
    return (void *)currentPtr;
}

uint16_t
fbSubTemplateListCountElements(
    const fbSubTemplateList_t  *subTemplateList)
{
    return subTemplateList->numElements;
}

void
fbSubTemplateListSetSemantic(
    fbSubTemplateList_t  *subTemplateList,
    uint8_t               semantic)
{
    subTemplateList->semantic = semantic;
}

uint8_t
fbSubTemplateListGetSemantic(
    const fbSubTemplateList_t  *subTemplateList)
{
    return subTemplateList->semantic;
}

const fbTemplate_t *
fbSubTemplateListGetTemplate(
    const fbSubTemplateList_t  *subTemplateList)
{
    return subTemplateList->tmpl;
}

uint16_t
fbSubTemplateListGetTemplateID(
    const fbSubTemplateList_t  *subTemplateList)
{
    return subTemplateList->tmplID;
}

void *
fbSubTemplateListResize(
    fbSubTemplateList_t  *subTemplateList,
    uint16_t              newCount)
{
    if (newCount == subTemplateList->numElements) {
        return memset(subTemplateList->dataPtr, 0, subTemplateList->dataLength);
    }
    g_slice_free1(subTemplateList->dataLength, subTemplateList->dataPtr);
    return fbSubTemplateListAllocData(subTemplateList, newCount);
}

void *
fbSubTemplateListAddNewElements(
    fbSubTemplateList_t  *subTemplateList,
    uint16_t              additional)
{
    uint16_t oldDataLength = subTemplateList->dataLength;
    uint8_t *oldDataPtr    = subTemplateList->dataPtr;

    fbSubTemplateListAllocData(
        subTemplateList, subTemplateList->numElements + additional);

    if (oldDataPtr) {
        memcpy(subTemplateList->dataPtr, oldDataPtr, oldDataLength);
        g_slice_free1(oldDataLength, oldDataPtr);
    }

    return subTemplateList->dataPtr + oldDataLength;
}


/*
 *  ************************************************************************
 *  fbSubTemplateMultiList_t
 *  ************************************************************************
 */
fbSubTemplateMultiList_t *
fbSubTemplateMultiListAlloc(
    void)
{
    return g_slice_new0(fbSubTemplateMultiList_t);
}

fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListInit(
    fbSubTemplateMultiList_t  *sTML,
    uint8_t                    semantic,
    uint16_t                   numElements)
{
    sTML->semantic = semantic;
    sTML->numElements = numElements;
    sTML->firstEntry = g_slice_alloc0(sTML->numElements *
                                      sizeof(fbSubTemplateMultiListEntry_t));
    return sTML->firstEntry;
}

uint16_t
fbSubTemplateMultiListCountElements(
    const fbSubTemplateMultiList_t  *STML)
{
    return STML->numElements;
}

void
fbSubTemplateMultiListSetSemantic(
    fbSubTemplateMultiList_t  *STML,
    uint8_t                    semantic)
{
    STML->semantic = semantic;
}

uint8_t
fbSubTemplateMultiListGetSemantic(
    const fbSubTemplateMultiList_t  *STML)
{
    return STML->semantic;
}

void
fbSubTemplateMultiListClear(
    fbSubTemplateMultiList_t  *sTML)
{
    fbSubTemplateMultiListClearEntries(sTML);

    g_slice_free1(sTML->numElements * sizeof(fbSubTemplateMultiListEntry_t),
                  sTML->firstEntry);
    sTML->numElements = 0;
    sTML->firstEntry = NULL;
}

void
fbSubTemplateMultiListClearEntries(
    fbSubTemplateMultiList_t  *sTML)
{
    fbSubTemplateMultiListEntry_t *entry = NULL;
    while ((entry = fbSubTemplateMultiListGetNextEntry(sTML, entry))) {
        fbSubTemplateMultiListEntryClear(entry);
    }
}

void
fbSubTemplateMultiListFree(
    fbSubTemplateMultiList_t  *sTML)
{
    if (sTML) {
        fbSubTemplateMultiListClear(sTML);
        g_slice_free(fbSubTemplateMultiList_t, sTML);
    }
}

fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListResize(
    fbSubTemplateMultiList_t  *sTML,
    uint16_t                   newCount)
{
    fbSubTemplateMultiListClearEntries(sTML);
    if (newCount == sTML->numElements) {
        return memset(sTML->firstEntry, 0,
                      (sTML->numElements *
                       sizeof(fbSubTemplateMultiListEntry_t)));
    }
    g_slice_free1(sTML->numElements * sizeof(fbSubTemplateMultiListEntry_t),
                  sTML->firstEntry);
    sTML->numElements = newCount;
    sTML->firstEntry = g_slice_alloc0(sTML->numElements *
                                      sizeof(fbSubTemplateMultiListEntry_t));
    return sTML->firstEntry;
}

fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListAddNewEntries(
    fbSubTemplateMultiList_t  *sTML,
    uint16_t                   additional)
{
    fbSubTemplateMultiListEntry_t *oldEntries = sTML->firstEntry;
    size_t oldEntryLength =
        sTML->numElements * sizeof(fbSubTemplateMultiListEntry_t);

    sTML->numElements += additional;
    sTML->firstEntry = g_slice_alloc0(sTML->numElements *
                                      sizeof(fbSubTemplateMultiListEntry_t));

    if (oldEntries) {
        memcpy(sTML->firstEntry, oldEntries, oldEntryLength);
        g_slice_free1(oldEntryLength, oldEntries);
    }

    return sTML->firstEntry + (sTML->numElements - additional);
}

fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListGetFirstEntry(
    const fbSubTemplateMultiList_t  *sTML)
{
    return sTML->firstEntry;
}

fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListGetIndexedEntry(
    const fbSubTemplateMultiList_t  *sTML,
    uint16_t                         stmlIndex)
{
    if (stmlIndex >= sTML->numElements) {
        return NULL;
    }

    return sTML->firstEntry + stmlIndex;
}

fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListGetNextEntry(
    const fbSubTemplateMultiList_t       *sTML,
    const fbSubTemplateMultiListEntry_t  *currentEntry)
{
    if (!currentEntry) {
        return sTML->firstEntry;
    }
    if (!sTML->numElements || currentEntry < sTML->firstEntry) {
        return NULL;
    }

    currentEntry++;

    if (currentEntry >= sTML->firstEntry + sTML->numElements) {
        return NULL;
    }
    return (fbSubTemplateMultiListEntry_t *)currentEntry;
}

static void *
fbSubTemplateMultiListEntryAllocData(
    fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                        numElements)
{
    entry->numElements = numElements;
    entry->dataLength = numElements * entry->recordLength;
    entry->dataPtr = g_slice_alloc0(entry->dataLength);
    return (void *)entry->dataPtr;
}

void
fbSubTemplateMultiListEntryClear(
    fbSubTemplateMultiListEntry_t  *entry)
{
    g_slice_free1(entry->dataLength, entry->dataPtr);
    entry->tmplID = 0;
    entry->tmpl = NULL;
    entry->dataLength = 0;
    entry->dataPtr = NULL;
}

void *
fbSubTemplateMultiListEntryGetDataPtr(
    const fbSubTemplateMultiListEntry_t  *entry)
{
    return entry->dataPtr;
}

void *
fbSubTemplateMultiListEntryInit(
    fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                        tmplID,
    const fbTemplate_t             *tmpl,
    uint16_t                        numElements)
{
    g_assert(tmpl);
    g_assert(0 != tmplID);

    entry->tmplID = tmplID;
    entry->tmpl = tmpl;
    if (!tmpl) {
        return NULL;
    }
    entry->recordLength = tmpl->ie_internal_len;
    return fbSubTemplateMultiListEntryAllocData(entry, numElements);
}

uint16_t
fbSubTemplateMultiListEntryCountElements(
    const fbSubTemplateMultiListEntry_t  *entry)
{
    return entry->numElements;
}

const fbTemplate_t *
fbSubTemplateMultiListEntryGetTemplate(
    const fbSubTemplateMultiListEntry_t  *entry)
{
    return entry->tmpl;
}

uint16_t
fbSubTemplateMultiListEntryGetTemplateID(
    const fbSubTemplateMultiListEntry_t  *entry)
{
    return entry->tmplID;
}

void *
fbSubTemplateMultiListEntryResize(
    fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                        newCount)
{
    if (newCount == entry->numElements) {
        return memset(entry->dataPtr, 0, entry->dataLength);
    }
    g_slice_free1(entry->dataLength, entry->dataPtr);
    return fbSubTemplateMultiListEntryAllocData(entry, newCount);
}

void *
fbSubTemplateMultiListEntryAddNewElements(
    fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                        additional)
{
    uint16_t oldDataLength = entry->dataLength;
    uint8_t *oldDataPtr = entry->dataPtr;

    fbSubTemplateMultiListEntryAllocData(
        entry, entry->numElements + additional);

    if (oldDataPtr) {
        memcpy(entry->dataPtr, oldDataPtr, oldDataLength);
        g_slice_free1(oldDataLength, oldDataPtr);
    }

    return entry->dataPtr + oldDataLength;
}

void *
fbSubTemplateMultiListEntryNextDataPtr(
    const fbSubTemplateMultiListEntry_t  *entry,
    const void                           *curPtr)
{
    uint16_t tmplLen;
    uint8_t *currentPtr = (uint8_t *)curPtr;

    if (!currentPtr) {
        return entry->dataPtr;
    }
    if (!entry->numElements || currentPtr < entry->dataPtr) {
        return NULL;
    }

    tmplLen = entry->dataLength / entry->numElements;
    currentPtr += tmplLen;

    if (currentPtr >= (entry->dataPtr + entry->dataLength)) {
        return NULL;
    }
    return (void *)currentPtr;
}

void *
fbSubTemplateMultiListEntryGetIndexedPtr(
    const fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                              stmleIndex)
{
    if (stmleIndex >= entry->numElements) {
        return NULL;
    }

    return ((uint8_t *)(entry->dataPtr) +
            (stmleIndex * entry->dataLength / entry->numElements));
}


/**
 *  Returns the `nth` @ref fbBasicList_t on `record` or NULL if `record` does
 *  not contain an `nth` BasicList.
 */
static const fbBasicList_t *
fbRecordGetNthBL(
    const fbRecord_t  *record,
    uint16_t           nth)
{
    const fbTemplateField_t *field;

    if (nth < record->tmpl->bl.count) {
        field = record->tmpl->ie_ary[record->tmpl->bl.positions[nth]];
        return (const fbBasicList_t *)(record->rec + field->offset);
    }
    return NULL;
}

/**
 *  Returns the `nth` @ref fbSubTemplateList_t on `record` or NULL if `record`
 *  does not contain an `nth` SubTemplateList.
 */
static const fbSubTemplateList_t *
fbRecordGetNthSTL(
    const fbRecord_t  *record,
    uint16_t           nth)
{
    const fbTemplateField_t *field;

    if (nth < record->tmpl->stl.count) {
        field = record->tmpl->ie_ary[record->tmpl->stl.positions[nth]];
        return (const fbSubTemplateList_t *)(record->rec + field->offset);
    }
    return NULL;
}

/**
 *  Returns the `nth` @ref fbSubTemplateMultiList_t on `record` or NULL if
 *  `record` does not contain an `nth` SubTemplateMultiList.
 */
static const fbSubTemplateMultiList_t *
fbRecordGetNthSTML(
    const fbRecord_t  *record,
    uint16_t           nth)
{
    const fbTemplateField_t *field;

    if (nth < record->tmpl->stml.count) {
        field = record->tmpl->ie_ary[record->tmpl->stml.positions[nth]];
        return (const fbSubTemplateMultiList_t *)(record->rec + field->offset);
    }
    return NULL;
}



/**
 * fBufSTMLEntryRecordFree
 *
 * Free all records within the STMLEntry
 *
 */
static void
fBufSTMLEntryRecordFree(
    fbSubTemplateMultiListEntry_t  *entry)
{
    uint8_t *data = NULL;

    while ((data = fbSubTemplateMultiListEntryNextDataPtr(entry, data))) {
        fBufListFree(entry->tmpl, data);
    }
}


/**
 * fBufSTMLRecordFree
 *
 * Access each entry in the STML and free the records in each
 * entry.
 *
 */
static void
fBufSTMLRecordFree(
    fbSubTemplateMultiList_t  *stml)
{
    fbSubTemplateMultiListEntry_t *entry = NULL;

    while ((entry = fbSubTemplateMultiListGetNextEntry(stml, entry))) {
        fBufSTMLEntryRecordFree(entry);
    }
}

/**
 * fBufSTLRecordFree
 *
 * Free all records within the subTemplateList
 *
 */
static void
fBufSTLRecordFree(
    fbSubTemplateList_t  *stl)
{
    uint8_t *data = NULL;

    while ((data = fbSubTemplateListGetNextPtr(stl, data))) {
        fBufListFree((fbTemplate_t *)(stl->tmpl), data);
    }
}

/**
 * fBufBLRecordFree
 *
 * Free any list elements nested within a basicList
 *
 */
static void
fBufBLRecordFree(
    fbBasicList_t  *bl)
{
    uint8_t *data = NULL;

    switch (fbTemplateFieldGetType(&bl->field)) {
      case FB_SUB_TMPL_MULTI_LIST:
        while ((data = fbBasicListGetNextPtr(bl, data))) {
            fBufSTMLRecordFree((fbSubTemplateMultiList_t *)data);
            fbSubTemplateMultiListClear((fbSubTemplateMultiList_t *)data);
        }
        break;
      case FB_SUB_TMPL_LIST:
        while ((data = fbBasicListGetNextPtr(bl, data))) {
            fBufSTLRecordFree((fbSubTemplateList_t *)data);
            fbSubTemplateListClear((fbSubTemplateList_t *)data);
        }
        break;
      case FB_BASIC_LIST:
        while ((data = fbBasicListGetNextPtr(bl, data))) {
            fBufBLRecordFree((fbBasicList_t *)data);
            fbBasicListClear((fbBasicList_t *)data);
        }
        break;
      default:
        break;
    }
}

/**
 * fBufListFree
 *
 * This frees any list information elements that were allocated
 * either by fixbuf or the application during encode or decode.
 *
 * The template provided is the internal template and MUST match
 * the record exactly, or bad things will happen.
 * There is no way to check that the user is doing the right
 * thing.
 */
void
fBufListFree(
    const fbTemplate_t  *tmpl,
    uint8_t             *record)
{
    fbSubTemplateMultiList_t *stml;
    fbSubTemplateList_t      *stl;
    fbBasicList_t            *bl;
    fbRecord_t rec;
    uint16_t   i;

    if (!tmpl->contains_list) {
        /* no variable length fields in this template */
        return;
    }
    g_assert(record);

    rec.tmpl = tmpl;
    rec.rec = record;

    for (i = 0; (bl = (fbBasicList_t *)fbRecordGetNthBL(&rec, i)); ++i) {
        fBufBLRecordFree(bl);
        fbBasicListClear(bl);
    }

    for (i = 0; (stl = (fbSubTemplateList_t *)fbRecordGetNthSTL(&rec, i));
         ++i)
    {
        fBufSTLRecordFree(stl);
        fbSubTemplateListClear(stl);
    }

    for (i = 0;
         (stml = (fbSubTemplateMultiList_t *)fbRecordGetNthSTML(&rec, i));
         ++i)
    {
        fBufSTMLRecordFree(stml);
        fbSubTemplateMultiListClear(stml);
    }
}


/*
 * fbRecordGetFieldCount
 *
 *
 */
uint16_t
fbRecordGetFieldCount(
    const fbRecord_t  *record)
{
    if (record->tmpl) {
        return record->tmpl->ie_count;
    }
    return 0;
}


/*
 * fbRecordFreeLists
 *
 *
 */
void
fbRecordFreeLists(
    fbRecord_t  *record)
{
    fBufListFree(record->tmpl, record->rec);
}


/*
 * fbRecordValueClear
 *
 *
 */
void
fbRecordValueClear(
    fbRecordValue_t  *value)
{
    if (value) {
        g_string_free(value->stringbuf, TRUE);
        memset(value, 0, sizeof(*value));
    }
}


/*
 * fbRecordValueTakeVarfieldBuf
 *
 *
 */
gchar *
fbRecordValueTakeVarfieldBuf(
    fbRecordValue_t  *value)
{
    gchar *str;

    if (NULL == value) {
        return NULL;
    }
    str = g_string_free(value->stringbuf, FALSE);
    value->stringbuf = NULL;
    return str;
}


/*
 * fbRecordValueCopy
 *
 *
 */
fbRecordValue_t *
fbRecordValueCopy(
    fbRecordValue_t        *dstValue,
    const fbRecordValue_t  *srcValue)
{
    dstValue->ie = srcValue->ie;
    switch (fbInfoElementGetType(dstValue->ie)) {
      case FB_STRING:
      case FB_OCTET_ARRAY:
        dstValue->stringbuf = g_string_new_len(srcValue->stringbuf->str,
                                               srcValue->stringbuf->len);
        dstValue->v.varfield.buf = (uint8_t *)dstValue->stringbuf->str;
        dstValue->v.varfield.len = dstValue->stringbuf->len;
        break;
      default:
        dstValue->stringbuf = NULL;
        dstValue->v = srcValue->v;
        break;
    }

    return dstValue;
}


/**
 *  Treats `ntptime` as pointer to a uint64_t representing a timestamp in the
 *  NTP format and converts it to a timespec (sec, nanosec).
 *
 *  The NTP format has seconds since the NTP epoch (technically seconds within
 *  the current NTP era) in the most significant 32 bits. The least
 *  significant 32 bits contains the number of 1/(2^32)th fractional seconds
 *  (~233 picoseconds).
 *
 *  When fixbuf transcodes an NTP time, it treats the value as one 64 bit
 *  value.
 */
static void
fbNtptimeToTimespec(
    const void               *ntptime,
    struct timespec          *ts,
    fbInfoElementDataType_t   datatype)
{
    /* FIXME: Handle NTP wraparaound for Feb 8 2036 */

    /* timespec uses nanosecond: 1e9 */
    const uint64_t  frac_per_sec = UINT64_C(1000000000);
    const uint64_t *u64;

#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    uint64_t        u64_local;

    memcpy(&u64_local, ntptime, sizeof(u64_local));
    u64 = &u64_local;
#else
    u64 = (const uint64_t *)ntptime;
#endif

    ts->tv_sec = (int64_t)((*u64) >> 32) - NTP_EPOCH_TO_UNIX_EPOCH;

    /* divide fractional secs by 2^32 to get some fraction of a second,
     * multiply by 1e9 to get nanoseconds */
    if (datatype == FB_DT_MICROSEC) {
        /* ignore bottom 11 bits of precision */
        ts->tv_nsec = ((*u64 & UINT64_C(0xfffff800)) * frac_per_sec) >> 32;
    } else {
        ts->tv_nsec = ((*u64 & UINT32_MAX) * frac_per_sec) >> 32;
    }
}

#if 0
/**
 *  Treats `ntptime` as pointer a uint64_t representing a timestamp in the NTP
 *  format and converts it to a timeval (sec, microsec).
 */
static void
fbNtptimeToTimeval(
    const void               *ntptime,
    struct timeval           *tv,
    fbInfoElementDataType_t   datatype)
{
    /* FIXME: Handle NTP wraparaound for Feb 8 2036 */

    /* timeval uses microsecond: 1e6 */
    const uint64_t  frac_per_sec = UINT64_C(1000000);
    const uint64_t *u64;

#ifdef HAVE_ALIGNED_ACCESS_REQUIRED
    uint64_t        u64_local;

    memcpy(&u64_local, ntptime, sizeof(u64_local));
    u64 = &u64_local;
#else
    u64 = (const uint64_t *)ntptime;
#endif

    tv->tv_sec = (int64_t)((*u64) >> 32) - NTP_EPOCH_TO_UNIX_EPOCH;

    /* divide fractional secs by 2^32 to get some fraction of a second,
     * multiply by 1e6 to get microseconds */
    if (datatype == FB_DT_MICROSEC) {
        tv->tv_usec = ((*u64 & UINT64_C(0xfffff800)) * frac_per_sec) >> 32;
    } else {
        tv->tv_usec = ((*u64 & UINT32_MAX) * frac_per_sec) >> 32;
    }
}
#endif  /* 0 */


/*
 *  fbRecordFillValue
 *
 *  Copies the data in `buf` which contains a value described by `field` to
 *  `value`.
 *
 *  For an fbVarfield_t, the data is copied to a GString in `value`, replacing
 *  any previous value.
 *
 *  For a list structure, `value` contains a direct pointer to the record's
 *  list object in fixbuf.
 */
static void
fbRecordFillValue(
    const fbTemplateField_t  *field,
    const uint8_t            *buf,
    fbRecordValue_t          *value)
{
    memset(&value->v, 0, sizeof(value->v));
    value->ie = field->canon;
    switch (fbTemplateFieldGetType(field)) {
      case FB_BOOL:
        /* According to spec: TRUE == 1, FALSE == 2 */
        value->v.u64 = (1 == *buf);
        break;
      case FB_UINT_8:
        value->v.u64 = *buf;
        break;
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        fbCopyInteger(buf, &value->v.u64, field->len, sizeof(value->v.u64), 0);
        break;
      case FB_IP4_ADDR:
        memcpy(&value->v.ip4, buf, sizeof(value->v.ip4));
        break;
      case FB_IP6_ADDR:
        memcpy(value->v.ip6, buf, sizeof(value->v.ip6));
        break;
      case FB_MAC_ADDR:
        memcpy(value->v.mac, buf, sizeof(value->v.mac));
        break;
      case FB_DT_SEC:
        fbCopyInteger(buf, &value->v.dt.tv_sec,
                      field->len, sizeof(value->v.dt.tv_sec), 0);
        break;
      case FB_DT_MILSEC:
        {
            uint64_t u64;
            memcpy(&u64, buf, sizeof(u64));
            value->v.dt.tv_sec = u64 / 1000;
            value->v.dt.tv_nsec = (u64 % 1000) * 1000000;
        }
        break;
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        fbNtptimeToTimespec(buf, &value->v.dt, field->canon->type);
        break;
      case FB_FLOAT_64:
        if (field->len == 8) {
            memcpy(&value->v.dbl, buf, sizeof(value->v.dbl));
            break;
        }
      /* FALLTHROUGH */
      case FB_FLOAT_32:
        {
            float f;
            memcpy(&f, buf, sizeof(f));
            value->v.dbl = f;
            break;
        }
      case FB_INT_8:
        value->v.s64 = *buf;
        break;
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        fbCopyInteger(buf, &value->v.s64, field->len, sizeof(value->v.s64), 1);
        break;
      case FB_STRING:
      case FB_OCTET_ARRAY:
        {
            const char *vf_buf;
            size_t      vf_len;
            if (field->len == FB_IE_VARLEN) {
                vf_buf = (char *)( ((fbVarfield_t *)buf)->buf);
                vf_len = ((fbVarfield_t *)buf)->len;
            } else {
                vf_buf = (char *)buf;
                vf_len = field->len;
            }
            if (NULL == value->stringbuf) {
                value->stringbuf = g_string_new_len(vf_buf, vf_len);
            } else {
                g_string_truncate(value->stringbuf, 0);
                g_string_append_len(value->stringbuf, vf_buf, vf_len);
            }
            value->v.varfield.buf = (uint8_t *)value->stringbuf->str;
            value->v.varfield.len = value->stringbuf->len;
        }
        break;
      case FB_BASIC_LIST:
        value->v.bl = (fbBasicList_t *)buf;
        break;
      case FB_SUB_TMPL_LIST:
        value->v.stl = (fbSubTemplateList_t *)buf;
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        value->v.stml = (fbSubTemplateMultiList_t *)buf;
        break;
    }
}


gboolean
fbRecordCopyFieldValue(
    const fbRecord_t         *record,
    const fbTemplateField_t  *field,
    void                     *dest,
    size_t                    destlen)
{
    if (record->tmpl != field->tmpl) {
        return FALSE;
    }
    switch (fbTemplateFieldGetType(field)) {
      case FB_BOOL:
        /* According to spec: TRUE == 1, FALSE == 2 */
        if (sizeof(uint8_t) != destlen) {
            return FALSE;
        }
        *(uint8_t *)dest = (1 == *(record->rec + field->offset));
        break;
      case FB_UINT_8:
        if (sizeof(uint8_t) != destlen) {
            return FALSE;
        }
        *(uint8_t *)dest = *(record->rec + field->offset);
        break;
      case FB_UINT_16:
        if (sizeof(uint16_t) != destlen) {
            return FALSE;
        }
        fbCopyInteger(record->rec + field->offset, dest,
                      field->len, destlen, 0);
        break;
      case FB_UINT_32:
        if (sizeof(uint32_t) != destlen) {
            return FALSE;
        }
        fbCopyInteger(record->rec + field->offset, dest,
                      field->len, destlen, 0);
        break;
      case FB_UINT_64:
        if (sizeof(uint64_t) != destlen) {
            return FALSE;
        }
        fbCopyInteger(record->rec + field->offset, dest,
                      field->len, destlen, 0);
        break;
      case FB_DT_SEC:
      case FB_IP4_ADDR:
        if (sizeof(uint32_t) != destlen) {
            return FALSE;
        }
        memcpy(dest, record->rec + field->offset, destlen);
        break;
      case FB_DT_MILSEC:
        if (sizeof(uint64_t) != destlen) {
            return FALSE;
        }
        memcpy(dest, record->rec + field->offset, destlen);
        break;

      case FB_IP6_ADDR:
        if (16 != destlen) {
            return FALSE;
        }
        memcpy(dest, record->rec + field->offset, destlen);
        break;
      case FB_MAC_ADDR:
        if (6 != destlen) {
            return FALSE;
        }
        memcpy(dest, record->rec + field->offset, destlen);
        break;
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        if (sizeof(struct timespec) != destlen) {
            return FALSE;
        }
        {
            struct timespec dt;
            fbNtptimeToTimespec(record->rec + field->offset,
                                &dt, field->canon->type);
            memcpy(dest, &dt, destlen);
        }
        break;
      case FB_FLOAT_64:
        if (sizeof(double) != destlen) {
            return FALSE;
        }
        if (field->len == 8) {
            memcpy(dest, record->rec + field->offset, destlen);
        } else {
            double d;
            float  f;
            memcpy(&f, record->rec + field->offset, sizeof(f));
            d = f;
            memcpy(dest, &d, destlen);
        }
        break;
      case FB_FLOAT_32:
        if (sizeof(float) != destlen) {
            return FALSE;
        }
        memcpy(dest, record->rec + field->offset, destlen);
        break;
      case FB_INT_8:
        if (sizeof(int8_t) != destlen) {
            return FALSE;
        }
        *(int8_t *)dest = *(record->rec + field->offset);
        break;
      case FB_INT_16:
        if (sizeof(int16_t) != destlen) {
            return FALSE;
        }
        fbCopyInteger(record->rec + field->offset, dest,
                      field->len, destlen, 1);
        break;
      case FB_INT_32:
        if (sizeof(int32_t) != destlen) {
            return FALSE;
        }
        fbCopyInteger(record->rec + field->offset, dest,
                      field->len, destlen, 1);
        break;
      case FB_INT_64:
        if (sizeof(int64_t) != destlen) {
            return FALSE;
        }
        fbCopyInteger(record->rec + field->offset, dest,
                      field->len, destlen, 1);
        break;
      case FB_STRING:
      case FB_OCTET_ARRAY:
        if (sizeof(fbVarfield_t) != destlen) {
            return FALSE;
        }
        if (FB_IE_VARLEN == field->len) {
            memcpy(dest, record->rec + field->offset, destlen);
        } else {
            fbVarfield_t v;
            v.buf = record->rec + field->offset;
            v.len = field->len;
            memcpy(dest, &v, destlen);
        }
        break;
      case FB_BASIC_LIST:
        if (sizeof(fbBasicList_t *) != destlen) {
            return FALSE;
        }
        *((fbBasicList_t **)dest) =
            (fbBasicList_t *)(record->rec + field->offset);
        break;
      case FB_SUB_TMPL_LIST:
        if (sizeof(fbSubTemplateList_t *) != destlen) {
            return FALSE;
        }
        *((fbSubTemplateList_t **)dest) =
            (fbSubTemplateList_t *)(record->rec + field->offset);
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        if (sizeof(fbSubTemplateMultiList_t *) != destlen) {
            return FALSE;
        }
        *((fbSubTemplateMultiList_t **)dest) =
            (fbSubTemplateMultiList_t *)(record->rec + field->offset);
        break;
    }

    return TRUE;
}


/*
 * fbRecordFindValueForInfoElement
 *
 *
 */
gboolean
fbRecordFindValueForInfoElement(
    const fbRecord_t       *record,
    const fbInfoElement_t  *ie,
    fbRecordValue_t        *value,
    uint16_t               *position,
    uint16_t                skip)
{
    const fbTemplateField_t *field;

    field = fbTemplateFindFieldByElement(record->tmpl, ie, position, skip);
    if (field) {
        fbRecordFillValue(field, record->rec + field->offset, value);
        return TRUE;
    }
    return FALSE;
}


/*
 * fbRecordGetValueForField
 *
 *
 */
gboolean
fbRecordGetValueForField(
    const fbRecord_t         *record,
    const fbTemplateField_t  *field,
    fbRecordValue_t          *value)
{
    if (field && field->tmpl == record->tmpl) {
        fbRecordFillValue(field, record->rec + field->offset, value);
        return TRUE;
    }
    return FALSE;
}

/*
 * fbRecordFindBasicListOfIE
 *
 *
 */
const fbBasicList_t *
fbRecordFindBasicListOfIE(
    const fbRecord_t       *record,
    const fbInfoElement_t  *listElement,
    uint16_t               *position,
    uint16_t                skip)
{
    const fbBasicList_t *bl;
    unsigned int         i;

    for (i = 0; i < record->tmpl->bl.count; ++i) {
        /* skip any basicList before *position when it is non-NULL */
        if (position && (*position > record->tmpl->bl.positions[i])) {
            continue;
        }
        bl = fbRecordGetNthBL(record, i);
        if (fbBasicListGetInfoElement(bl) == listElement) {
            if (skip == 0) {
                if (position) {
                    *position = record->tmpl->bl.positions[i];
                }
                return bl;
            }
            --skip;
        }
    }
    return NULL;
}


/*
 * fbRecordFindStlOfTemplate
 *
 *
 */
const fbSubTemplateList_t *
fbRecordFindStlOfTemplate(
    const fbRecord_t    *record,
    const fbTemplate_t  *tmpl,
    uint16_t            *position,
    uint16_t             skip)
{
    const fbSubTemplateList_t *stl;
    unsigned int i;

    for (i = 0; i < record->tmpl->stl.count; ++i) {
        /* skip any STL before *position when it is non-NULL */
        if (position && (*position > record->tmpl->stl.positions[i])) {
            continue;
        }
        stl = fbRecordGetNthSTL(record, i);
        if (fbSubTemplateListGetTemplate(stl) == tmpl) {
            if (skip == 0) {
                if (position) {
                    *position = record->tmpl->stl.positions[i];
                }
                return stl;
            }
            --skip;
        }
    }

    return NULL;
}


/*
 * fbRecordFindAllElementValues
 *
 *
 */
int
fbRecordFindAllElementValues(
    const fbRecord_t          *record,
    const fbInfoElement_t     *ie,
    unsigned int               flags,
    fbRecordValueCallback_fn   callback,
    void                      *ctx)
{
    fbRecordValue_t            value = FB_RECORD_VALUE_INIT;
    const fbSubTemplateMultiListEntry_t *entry;
    const fbSubTemplateMultiList_t *stml;
    const fbSubTemplateList_t *stl;
    const fbTemplateField_t   *field;
    const fbBasicList_t       *bl;
    const uint8_t             *item;
    fbRecord_t subrec;
    uint16_t   i;
    int        rv;

    memset(&subrec, 0, sizeof(subrec));

    /* check the record itself */
    for (i = 0;
         (field = fbTemplateFindFieldByElement(record->tmpl, ie, &i, 0));
         ++i)
    {
        fbRecordGetValueForField(record, field, &value);
        rv = callback(record, NULL, ie, &value, ctx);
        if (rv) { return rv; }
    }

    /* check its basicLists */
    for (i = 0; (bl = fbRecordGetNthBL(record, i)); ++i) {
        if (bl->field.canon == ie) {
            item = NULL;
            while ((item = fbBLNext(uint8_t, bl, item))) {
                fbRecordFillValue(&bl->field, item, &value);
                rv = callback(record, bl, ie, &value, ctx);
                if (rv) { return rv; }
            }
        }
        /* FIXME: Handle the pathological cases of a structured data item in a
         * basicList. */
    }

    /* check its subTemplateLists */
    for (i = 0; (stl = fbRecordGetNthSTL(record, i)); ++i) {
        subrec.tmpl = fbSubTemplateListGetTemplate(stl);
        if (!fbTemplateContainsElement(subrec.tmpl, ie) &&
            !subrec.tmpl->contains_list)
        {
            /* does not contain element and no lists to search */
            continue;
        }
        /* set up a sub-record for the STL's contents */
        subrec.tid = fbSubTemplateListGetTemplateID(stl);
        subrec.recsize = subrec.reccapacity = subrec.tmpl->ie_internal_len;
        subrec.rec = NULL;
        while ((subrec.rec = fbSTLNext(uint8_t, stl, subrec.rec))) {
            rv = fbRecordFindAllElementValues(&subrec, ie, flags,
                                              callback, ctx);
            if (rv) { return rv; }
        }
    }

    /* check its subTemplateMultiList */
    for (i = 0; (stml = fbRecordGetNthSTML(record, i)); ++i) {
        entry = NULL;
        while ((entry = fbSubTemplateMultiListGetNextEntry(stml, entry))) {
            subrec.tmpl = fbSubTemplateMultiListEntryGetTemplate(entry);
            if (!fbTemplateContainsElement(subrec.tmpl, ie) &&
                !subrec.tmpl->contains_list)
            {
                continue;
            }
            subrec.tid = fbSubTemplateMultiListEntryGetTemplateID(entry);
            subrec.recsize = subrec.reccapacity = subrec.tmpl->ie_internal_len;
            subrec.rec = NULL;
            while ((subrec.rec = fbSTMLEntryNext(uint8_t, entry, subrec.rec))) {
                rv = fbRecordFindAllElementValues(&subrec, ie, flags,
                                                  callback, ctx);
                if (rv) { return rv; }
            }
        }
    }

    return 0;
}


/**
 *  fbRecordFindAllSubRecordsSTL
 *
 *  Helper function for fbRecordFindAllSubRecords() that checks a single
 *  SubTemplateList.
 */
static int
fbRecordFindAllSubRecordsSTL(
    const fbSubTemplateList_t     *stl,
    uint16_t                       tid,
    unsigned int                   flags,
    fbRecordSubRecordCallback_fn   callback,
    void                          *ctx)
{
    fbRecord_t subrec;
    int        rv;

    /* set up a sub-record for the STL's contents */
    subrec.tmpl = fbSubTemplateListGetTemplate(stl);
    subrec.tid = fbSubTemplateListGetTemplateID(stl);
    subrec.recsize = subrec.reccapacity = subrec.tmpl->ie_internal_len;
    subrec.rec = NULL;
    if (tid == subrec.tid) {
        /* this STL contains matching records */
        while ((subrec.rec = fbSTLNext(uint8_t, stl, subrec.rec))) {
            rv = callback(&subrec, ctx);
            if (rv) { return rv; }
        }
    } else if (subrec.tmpl->contains_list) {
        /* recurse into the STL's subrecords */
        while ((subrec.rec = fbSTLNext(uint8_t, stl, subrec.rec))) {
            rv = fbRecordFindAllSubRecords(&subrec, tid, flags, callback, ctx);
            if (rv) { return rv; }
        }
    }

    return 0;
}

/**
 *  fbRecordFindAllSubRecordsSTML
 *
 *  Helper function for fbRecordFindAllSubRecords() that checks a single
 *  SubTemplateMultiList.
 */
static int
fbRecordFindAllSubRecordsSTML(
    const fbSubTemplateMultiList_t  *stml,
    uint16_t                         tid,
    unsigned int                     flags,
    fbRecordSubRecordCallback_fn     callback,
    void                            *ctx)
{
    const fbSubTemplateMultiListEntry_t *entry;
    fbRecord_t subrec;
    int        rv;

    entry = NULL;
    while ((entry = fbSubTemplateMultiListGetNextEntry(stml, entry))) {
        /* set up a sub-record for the STMLEntry's contents */
        subrec.tmpl = fbSubTemplateMultiListEntryGetTemplate(entry);
        subrec.tid = fbSubTemplateMultiListEntryGetTemplateID(entry);
        subrec.recsize = subrec.reccapacity = subrec.tmpl->ie_internal_len;
        subrec.rec = NULL;
        if (tid == subrec.tid) {
            /* this STMLEntry contains matching records */
            while ((subrec.rec = fbSTMLEntryNext(uint8_t, entry, subrec.rec))) {
                rv = callback(&subrec, ctx);
                if (rv) { return rv; }
            }
        } else if (subrec.tmpl->contains_list) {
            /* recurse into the STMEntry's subrecords */
            while ((subrec.rec = fbSTMLEntryNext(uint8_t, entry, subrec.rec))) {
                rv = fbRecordFindAllSubRecords(&subrec, tid, flags,
                                               callback, ctx);
                if (rv) { return rv; }
            }
        }
    }

    return 0;
}

/**
 *  fbRecordFindAllSubRecordsBL
 *
 *  Helper function for fbRecordFindAllSubRecords() that checks a single
 *  BasicList.
 */
static int
fbRecordFindAllSubRecordsBL(
    const fbBasicList_t           *bl,
    uint16_t                       tid,
    unsigned int                   flags,
    fbRecordSubRecordCallback_fn   callback,
    void                          *ctx)
{
    const fbSubTemplateMultiList_t *stml;
    const fbSubTemplateList_t      *stl;
    const fbBasicList_t            *subBL;
    int rv;

    switch (fbTemplateFieldGetType(&bl->field)) {
      case FB_BASIC_LIST:
        subBL = NULL;
        while ((subBL = fbBLNext(const fbBasicList_t, bl, subBL))) {
            rv = fbRecordFindAllSubRecordsBL(subBL, tid, flags, callback, ctx);
            if (rv) { return rv; }
        }
        break;
      case FB_SUB_TMPL_LIST:
        stl = NULL;
        while ((stl = fbBLNext(const fbSubTemplateList_t, bl, stl))) {
            rv = fbRecordFindAllSubRecordsSTL(stl, tid, flags, callback, ctx);
            if (rv) { return rv; }
        }
        break;
      case FB_SUB_TMPL_MULTI_LIST:
        stml = NULL;
        while ((stml = fbBLNext(const fbSubTemplateMultiList_t, bl, stml))) {
            rv = fbRecordFindAllSubRecordsSTML(stml, tid, flags, callback, ctx);
            if (rv) { return rv; }
        }
        break;
      default:
        break;
    }

    return 0;
}


int
fbRecordFindAllSubRecords(
    const fbRecord_t              *record,
    uint16_t                       tid,
    unsigned int                   flags,
    fbRecordSubRecordCallback_fn   callback,
    void                          *ctx)
{
    const fbSubTemplateMultiList_t *stml;
    const fbSubTemplateList_t      *stl;
    const fbBasicList_t            *bl;
    uint16_t i;
    int      rv;

    /* check the record itself */
    if (tid == record->tid) {
        return callback(record, ctx);
    }

    /* check its basicLists */
    for (i = 0; (bl = fbRecordGetNthBL(record, i)); ++i) {
        rv = fbRecordFindAllSubRecordsBL(bl, tid, flags, callback, ctx);
        if (rv) { return rv; }
    }

    /* check its subTemplateLists */
    for (i = 0; (stl = fbRecordGetNthSTL(record, i)); ++i) {
        rv = fbRecordFindAllSubRecordsSTL(stl, tid, flags, callback, ctx);
        if (rv) { return rv; }
    }

    /* check its subTemplateMultiList */
    for (i = 0; (stml = fbRecordGetNthSTML(record, i)); ++i) {
        rv = fbRecordFindAllSubRecordsSTML(stml, tid, flags, callback, ctx);
        if (rv) { return rv; }
    }

    return 0;
}


static gboolean
fbRecordCopyField(
    const fbRecord_t         *srcRec,
    const fbTemplateField_t  *srcField,
    fbRecord_t               *dstRec,
    const fbTemplateField_t  *dstField,
    GError                  **err)
{
    if (srcField->canon->len != dstField->canon->len) {
        /* FIXME: Change the error code returned by this function */
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_SETUP,
                    "Copying between fixed and variable size is not supported");
        return FALSE;
    }
    switch (fbTemplateFieldGetType(srcField)) {
      case FB_BOOL:
      case FB_UINT_8:
      case FB_INT_8:
        *(dstRec->rec + dstField->offset) = *(srcRec->rec + srcField->offset);
        break;
      case FB_UINT_16:
      case FB_UINT_32:
      case FB_UINT_64:
        fbCopyInteger(srcRec->rec + srcField->offset,
                      dstRec->rec + dstField->offset,
                      srcField->len, dstField->len, 0);
        break;
      case FB_INT_16:
      case FB_INT_32:
      case FB_INT_64:
        fbCopyInteger(srcRec->rec + srcField->offset,
                      dstRec->rec + dstField->offset,
                      srcField->len, dstField->len, 0);
        break;
      case FB_FLOAT_64:
        if (dstField->len != srcField->len) {
            g_error("Copying between different sized floats not supported");
            /* FIXME */
        }
      /* FALLTHROUGH */
      case FB_FLOAT_32:
        memcpy(dstRec->rec + dstField->offset, srcRec->rec + srcField->offset,
               dstField->len);
        break;
      case FB_DT_SEC:
      case FB_IP4_ADDR:
      case FB_DT_MILSEC:
      case FB_IP6_ADDR:
      case FB_MAC_ADDR:
      case FB_DT_MICROSEC:
      case FB_DT_NANOSEC:
        g_assert(dstField->len == srcField->len);
        memcpy(dstRec->rec + dstField->offset, srcRec->rec + srcField->offset,
               dstField->len);
        break;
      case FB_STRING:
      case FB_OCTET_ARRAY:
        if (dstField->len != srcField->len) {
            /* allow different sized paddingOctets */
            if (fbInfoElementIsPadding(fbTemplateFieldGetIE(srcField))) {
                if (dstField->len != FB_IE_VARLEN) {
                    memset(dstRec->rec + dstField->offset, 0, dstField->len);
                }
                break;
            }
            /* FIXME: Handle copying between fixed lengths of diferent sizes
             * or fixed width and variable width */
            g_error(
                "Copying between different sized strings/octets not supported");
        } else if (dstField->len != FB_IE_VARLEN) {
            memcpy(dstRec->rec + dstField->offset,
                   srcRec->rec + srcField->offset,
                   dstField->len);
        } else {
            /* FIXME: properly handle memory that backs the text */
            memcpy(dstRec->rec + dstField->offset,
                   srcRec->rec + srcField->offset,
                   sizeof(fbVarfield_t));
        }
        break;
      case FB_BASIC_LIST:
        {
            /* FIXME: copy data? */
            const fbBasicList_t *srcBL;
            srcBL = (fbBasicList_t *)(srcRec->rec + srcField->offset);
            fbBasicListInitWithLength(
                (fbBasicList_t *)(dstRec->rec + dstField->offset),
                fbBasicListGetSemantic(srcBL),
                fbBasicListGetInfoElement(srcBL),
                fbBasicListGetElementLength(srcBL),
                0);
            break;
        }
      case FB_SUB_TMPL_LIST:
        {
            /* FIXME: initialize list; no copying */
            const fbSubTemplateList_t *srcSTL;
            srcSTL = (fbSubTemplateList_t *)(srcRec->rec + srcField->offset);
            fbSubTemplateListInit(
                (fbSubTemplateList_t *)(dstRec->rec + dstField->offset),
                fbSubTemplateListGetSemantic(srcSTL),
                fbSubTemplateListGetTemplateID(srcSTL),
                fbSubTemplateListGetTemplate(srcSTL),
                0);
            break;
        }
      case FB_SUB_TMPL_MULTI_LIST:
        {
            const fbSubTemplateMultiList_t *srcSTML;
            srcSTML = (fbSubTemplateMultiList_t *)(srcRec->rec
                                                   + srcField->offset);
            /* FIXME: initialize list; no copying */
            fbSubTemplateMultiListInit(
                (fbSubTemplateMultiList_t *)(dstRec->rec + dstField->offset),
                fbSubTemplateMultiListGetSemantic(srcSTML),
                0);
            break;
        }
    }

    return TRUE;
}


gboolean
fbRecordCopyToTemplate(
    const fbRecord_t    *srcRec,
    fbRecord_t          *dstRec,
    const fbTemplate_t  *tmpl,
    uint16_t             tid,
    GError             **err)
{
    const fbTemplateField_t *srcField;
    const fbTemplateField_t *dstField;
    uint16_t i;
    gpointer v;

    if (dstRec->reccapacity < tmpl->ie_internal_len) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_SETUP,
                    "Unable to copy record:"
                    " Needed %u bytes but only %lu available",
                    tmpl->ie_internal_len, (unsigned long)dstRec->reccapacity);
        return FALSE;
    }
    memset(dstRec->rec, 0, dstRec->reccapacity);
    dstRec->tmpl = tmpl;
    dstRec->tid = tid;
    dstRec->recsize = tmpl->ie_internal_len;

    for (i = 0; i < tmpl->ie_count; ++i) {
        dstField = tmpl->ie_ary[i];
        if (g_hash_table_lookup_extended(srcRec->tmpl->indices,
                                         dstField, NULL, &v))
        {
            srcField = srcRec->tmpl->ie_ary[GPOINTER_TO_INT(v)];
            if (!fbRecordCopyField(srcRec, srcField, dstRec, dstField, err)) {
                return FALSE;
            }
        }
    }

    return TRUE;
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
