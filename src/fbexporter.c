/*
 *  Copyright 2006-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file fbexporter.c
 *  IPFIX Exporting Process single transport session implementation
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Brian Trammell
 *  ------------------------------------------------------------------------
 */

#define _FIXBUF_SOURCE_
#include <fixbuf/private.h>


/**
 * If set in exporter SCTP mode, use simple automatic stream selection as
 * specified in the IPFIX protocol without flexible stream selection: send
 * templates on stream 0, and data on stream 1.
 */
#define FB_F_SCTP_AUTOSTREAM        0x80000000

/**
 * If set in exporter SCTP mode, use TTL-based partial reliability for
 * non-template messages.
 */
#define FB_F_SCTP_PR_TTL            0x40000000

/**
 *  Signature of function for exporter->exopen()
 */
typedef gboolean
(*fbExporterOpen_fn)(
    fbExporter_t  *exporter,
    GError       **err);

/**
 *  Signature of function for exporter->exwrite()
 */
typedef gboolean
(*fbExporterWrite_fn)(
    fbExporter_t  *exporter,
    uint8_t       *msgbase,
    size_t         msglen,
    GError       **err);

/**
 *  Signature of function for exporter->exclose()
 */
typedef void
(*fbExporterClose_fn)(
    fbExporter_t  *exporter);

struct fbExporter_st {
    /** Specifier used for stream open */
    union {
        fbConnSpec_t  *conn;
        char          *path;
    }                           spec;
    /** Current export stream */
    union {
        /** Buffered file pointer, for file transport */
        FILE     *fp;
        /** Buffer for data if providing own transport */
        uint8_t  *buffer;
        /**
         * Unbuffered socket, for SCTP, TCP, or UDP transport.
         * Also used as base socket for TLS and DTLS support.
         */
        int       fd;
    }                           stream;
#ifdef HAVE_OPENSSL
    /** OpenSSL socket, for TLS or DTLS over the socket in fd. */
    SSL                 *ssl;
#endif
    /**
     * Callback function to open an exporter.  This is only called by
     * fbExportMessage() when exporter's `active` flag is false.
     */
    fbExporterOpen_fn    exopen;
    /**
     * Callback function to writer to an exporter.  This is only called by
     * fbExportMessage().
     */
    fbExporterWrite_fn   exwrite;
    /**
     * Callback function to close to an exporter.  Called by fbExporterClose()
     * and by fbExportMessage() if the write callback fails.
     */
    fbExporterClose_fn   exclose;
    /** Stores the `msglen` param of last call to fbExporterWriteBuffer() */
    size_t               msg_len;
    /** Stores number of octets written to the file, stream, or buffer.  This
     * sum does not include partial (short) writes to a socket. */
    size_t               export_len;
    /** SCTP mode. Union of FB_SCTP_F_* flags. */
    uint32_t             sctp_mode;
    /** Next SCTP stream */
    int                  sctp_stream;
    /** Partial reliability parameter (see mode) */
    int                  sctp_pr_param;
    uint16_t             mtu;
    gboolean             active;
};

/**
 * fbExporterOpenFile
 *
 * Implements exporter->exopen()
 *
 *
 * @param exporter
 * @param err
 *
 * @return
 */
static gboolean
fbExporterOpenFile(
    fbExporter_t  *exporter,
    GError       **err)
{
    /* check to see if we're opening stdout */
    if ((strlen(exporter->spec.path) == 1) &&
        (exporter->spec.path[0] == '-'))
    {
        /* don't open a terminal */
        if (isatty(fileno(stdout))) {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IO,
                        "Refusing to open stdout terminal for export");
            return FALSE;
        }

        /* yep, stdout */
        exporter->stream.fp = stdout;
    } else {
        /* nope, just a regular file */
        exporter->stream.fp = fopen(exporter->spec.path, "w");
    }

    /* check for error */
    if (!exporter->stream.fp) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IO,
                    "Couldn't open %s for export: %s",
                    exporter->spec.path, strerror(errno));
        return FALSE;
    }

    /* set active flag */
    exporter->active = TRUE;

    return TRUE;
}

/**
 * fbExporterWriteFile
 *
 * Implements exporter->exwrite()
 *
 * @param exporter
 * @param msgbase
 * @param msglen
 * @param err
 *
 * @return
 */
static gboolean
fbExporterWriteFile(
    fbExporter_t  *exporter,
    uint8_t       *msgbase,
    size_t         msglen,
    GError       **err)
{
    if (msglen != fwrite(msgbase, 1, msglen, exporter->stream.fp)) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IO,
                    "Couldn't write %u bytes to %s: %s",
                    (uint32_t)msglen, exporter->spec.path, strerror(errno));
        return FALSE;
    }

    return TRUE;
}

/**
 * fbExporterCloseFile
 *
 * Implements exporter->exclose()
 *
 *
 * @param exporter
 *
 */
static void
fbExporterCloseFile(
    fbExporter_t  *exporter)
{
    if (exporter->stream.fp == stdout) {
        fflush(exporter->stream.fp);
    } else {
        fclose(exporter->stream.fp);
    }
    exporter->stream.fp = NULL;
    exporter->active = FALSE;
}

/**
 * fbExporterAllocFile
 *
 *
 * @param path
 * @param exporter
 *
 * @return
 */
fbExporter_t *
fbExporterAllocFile(
    const char  *path)
{
    fbExporter_t *exporter;

    /* Create a new exporter */
    exporter = g_slice_new0(fbExporter_t);

    /* Copy the path */
    exporter->spec.path = g_strdup(path);

    /* Set up stream management functions */
    exporter->exopen = fbExporterOpenFile;
    exporter->exwrite = fbExporterWriteFile;
    exporter->exclose = fbExporterCloseFile;
    exporter->mtu = 65496;

    return exporter;
}

/**
 * fbExporterOpenBuffer
 *
 * Implements exporter->exopen()
 *
 *
 * @param exporter
 * @param err
 */
static gboolean
fbExporterOpenBuffer(
    fbExporter_t  *exporter,
    GError       **err)
{
    (void)err;
    /* set active flag */
    exporter->active = TRUE;

    return TRUE;
}

/**
 * fbExporterCloseBuffer
 *
 * Implements exporter->exclose()
 *
 *
 * @param exporter
 *
 */
static void
fbExporterCloseBuffer(
    fbExporter_t  *exporter)
{
    exporter->active = FALSE;
}

/**
 * fbExporterWriteBuffer
 *
 * Implements exporter->exwrite()
 *
 *
 * @param exporter
 * @param msgbase
 * @param msglen
 * @param err
 */
static gboolean
fbExporterWriteBuffer(
    fbExporter_t  *exporter,
    uint8_t       *msgbase,
    size_t         msglen,
    GError       **err)
{
    (void)err;
    memcpy(exporter->stream.buffer, msgbase, msglen);
    exporter->msg_len = msglen;

    return TRUE;
}

/**
 * fbExporterAllocBuffer
 *
 *
 *
 */
fbExporter_t *
fbExporterAllocBuffer(
    uint8_t   *buf,
    uint16_t   bufsize)
{
    fbExporter_t *exporter = NULL;

    exporter = g_slice_new0(fbExporter_t);
    exporter->exwrite = fbExporterWriteBuffer;
    exporter->exopen = fbExporterOpenBuffer;
    exporter->exclose = fbExporterCloseBuffer;
    exporter->mtu = bufsize;
    exporter->stream.buffer = buf;

    return exporter;
}

/**
 * fbExporterAllocFP
 *
 * @param fp
 *
 * @return
 */
fbExporter_t *
fbExporterAllocFP(
    FILE  *fp)
{
    fbExporter_t *exporter;

    /* Create a new exporter */
    exporter = g_slice_new0(fbExporter_t);

    /* Reference the path */
    exporter->spec.path = g_strdup("FP");

    /* Set up stream management functions */
    exporter->exwrite = fbExporterWriteFile;
    exporter->mtu = 65496;

    /* set active flag */
    exporter->active = TRUE;

    /* set file pointer */
    exporter->stream.fp = fp;

    return exporter;
}

/**
 * fbExporterIgnoreSigpipe
 *
 *
 */
static void
fbExporterIgnoreSigpipe(
    void)
{
    static gboolean  ignored = FALSE;
    struct sigaction sa, osa;

    if (ignored) {return;}

    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGPIPE, &sa, &osa)) {
        g_error("sigaction(SIGPIPE) failed: %s", strerror(errno));
    }

    ignored = TRUE;
}

/**
 * fbExporterMaxSendbuf
 *
 *
 * @param sock
 * @param size
 *
 * @return
 */
static gboolean
fbExporterMaxSendbuf(
    int   sock,
    int  *size)
{
    while (*size > 4096) {
        if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, size,
                       sizeof(*size)) == 0)
        {
            return TRUE;
        }
        if (errno != ENOBUFS) {
            return FALSE;
        }
        *size -= (*size > 1024 * 1024)
                        ? 1024 * 1024
                        : 2048;
    }
    return FALSE;
}

#define FB_SOCKBUF_DEFAULT (4 * 1024 * 1024)

/**
 * fbExporterOpenSocket
 *
 * Implements exporter->exopen() for TCP, UDP, SCTP
 *
 *
 * @param exporter
 * @param err
 *
 * @return
 */
static gboolean
fbExporterOpenSocket(
    fbExporter_t  *exporter,
    GError       **err)
{
    struct addrinfo *ai = NULL;
    int              sockbuf_sz = FB_SOCKBUF_DEFAULT;

    /* Turn the exporter connection specifier into an addrinfo */
    if (!fbConnSpecLookupAI(exporter->spec.conn, FALSE, err)) {return FALSE;}
    ai = (struct addrinfo *)exporter->spec.conn->vai;

    /* ignore sigpipe if we're doing TCP or SCTP export */
    if ((exporter->spec.conn->transport == FB_TCP) ||
        (exporter->spec.conn->transport == FB_TLS_TCP)
#ifdef FB_ENABLE_SCTP
        || (exporter->spec.conn->transport == FB_SCTP)
        || (exporter->spec.conn->transport == FB_DTLS_SCTP)
#endif
        )
    {
        fbExporterIgnoreSigpipe();
    }

    /* open socket of appropriate type for connection specifier */
    do {
#ifdef FB_ENABLE_SCTP
        if ((exporter->spec.conn->transport == FB_SCTP) ||
            (exporter->spec.conn->transport == FB_DTLS_SCTP))
        {
            /* Kludge for SCTP. addrinfo doesn't accept SCTP hints. */
            ai->ai_socktype = SOCK_STREAM;
            ai->ai_protocol = IPPROTO_SCTP;
        }
#endif /* if FB_ENABLE_SCTP */

        exporter->stream.fd = socket(ai->ai_family, ai->ai_socktype,
                                     ai->ai_protocol);
        if (exporter->stream.fd < 0) {continue;}
        if (connect(exporter->stream.fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(exporter->stream.fd);
    } while ((ai = ai->ai_next));

    /* check for no openable socket */
    if (ai == NULL) {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_CONN,
                    "couldn't create connected TCP socket to %s:%s %s",
                    exporter->spec.conn->host, exporter->spec.conn->svc,
                    strerror(errno));
        return FALSE;
    }

    /* increase send buffer size for UDP */
    if ((exporter->spec.conn->transport == FB_UDP) ||
        (exporter->spec.conn->transport == FB_DTLS_UDP))
    {
        if (!fbExporterMaxSendbuf(exporter->stream.fd, &sockbuf_sz)) {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_CONN,
                        "couldn't set socket buffer size on %s: %s",
                        exporter->spec.conn->host, strerror(errno));
            close(exporter->stream.fd);
            return FALSE;
        }
    }

    /* set active flag */
    exporter->active = TRUE;

    return TRUE;
}

#ifdef FB_ENABLE_SCTP

/**
 * fbExporterWriteSCTP
 *
 * Implements exporter->exwrite()
 *
 *
 * @param exporter
 * @param msgbase
 * @param msglen
 * @param err
 *
 * @return
 */
static gboolean
fbExporterWriteSCTP(
    fbExporter_t  *exporter,
    uint8_t       *msgbase,
    size_t         msglen,
    GError       **err)
{
    ssize_t  rc;
    uint16_t initial_setid;
    gboolean is_template;
    int      sctp_flags = 0;
    uint32_t sctp_ttl = 0;

    /* Check to see if this is a template message */
    initial_setid = *(uint16_t *)(msgbase + 16);
    if (initial_setid == FB_TID_TS || initial_setid == FB_TID_OTS) {
        is_template = TRUE;
    } else {
        is_template = FALSE;
    }

    /* Do automatic stream selection if requested. */
    if (exporter->sctp_mode & FB_F_SCTP_AUTOSTREAM) {
        if (is_template) {
            exporter->sctp_stream = 0;
        } else {
            exporter->sctp_stream = 1;
        }
    }

    /* Use partial reliability if requested for non-template messages */
    if (!is_template && (exporter->sctp_mode & FB_F_SCTP_PR_TTL)) {
        sctp_flags |= FB_F_SCTP_PR_TTL;
        sctp_ttl = exporter->sctp_pr_param;
    }

    rc = sctp_sendmsg(exporter->stream.fd, msgbase, msglen,
                      NULL, 0,      /* destination sockaddr */
                      0,            /* payload protocol */
                      sctp_flags,   /* flags */
                      exporter->sctp_stream,  /* stream */
                      sctp_ttl,     /* message lifetime (ms) */
                      0);           /* context */

    if (rc == (ssize_t)msglen) {
        return TRUE;
    } else if (rc == -1) {
        if (errno == EPIPE) {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_NLWRITE,
                        "Connection reset (EPIPE) on SCTP write");
        } else {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IO,
                        "I/O error: %s", strerror(errno));
        }
        return FALSE;
    } else {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IO,
                    "short write: wrote %d while writing %lu",
                    (int)rc, msglen);
        return FALSE;
    }
}

#endif /* FB_ENABLE_SCTP */


/**
 * fbExporterWriteTCP
 *
 * Implements exporter->exwrite()
 *
 *
 * @param exporter
 * @param msgbase
 * @param msglen
 * @param err
 *
 * @return
 */
static gboolean
fbExporterWriteTCP(
    fbExporter_t  *exporter,
    uint8_t       *msgbase,
    size_t         msglen,
    GError       **err)
{
    ssize_t rc;

    rc = write(exporter->stream.fd, msgbase, msglen);
    if (rc == (ssize_t)msglen) {
        return TRUE;
    } else if (rc == -1) {
        if (errno == EPIPE) {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_NLWRITE,
                        "Connection reset (EPIPE) on TCP write");
        } else {
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IO,
                        "I/O error: %s", strerror(errno));
        }
        return FALSE;
    } else {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IO,
                    "short write: wrote %d while writing %u",
                    (int)rc, (uint32_t)msglen);
        return FALSE;
    }
}

/**
 * fbExporterWriteUDP
 *
 * Implements exporter->exwrite()
 *
 *
 * @param exporter
 * @param msgbase
 * @param msglen
 * @param err
 *
 * @return
 */
static gboolean
fbExporterWriteUDP(
    fbExporter_t  *exporter,
    uint8_t       *msgbase,
    size_t         msglen,
    GError       **err)
{
    static gboolean sendGood = TRUE;
    ssize_t         rc;

    /* Send the buffer as a single message */
    rc = send(exporter->stream.fd, msgbase, msglen, 0);

    /* Deal with the results */
    if (rc == (ssize_t)msglen) {
        return TRUE;
    } else if (rc == -1) {
        if (TRUE == sendGood) {
            g_warning( "I/O error on UDP send: %s (socket closed on receiver?)",
                       strerror(errno));
            g_warning("packets will be lost");
            send(exporter->stream.fd, msgbase, msglen, 0);
            sendGood = FALSE;
            return TRUE;
        }
        return TRUE;
    } else {
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IO,
                    "Short write on UDP send: wrote %d while writing %u",
                    (int)rc, (uint32_t)msglen);
        return FALSE;
    }
}

/**
 * fbExporterCloseSocket
 *
 * Implements exporter->exclose() for TCP, UDP, SCTP
 *
 *
 * @param exporter
 *
 */
static void
fbExporterCloseSocket(
    fbExporter_t  *exporter)
{
    close(exporter->stream.fd);
    exporter->active = FALSE;
}

#ifdef HAVE_OPENSSL

/**
 * fbExporterOpenTLS
 *
 * Implements exporter->exopen()
 *
 *
 * @param exporter
 * @param err
 *
 * @return
 */
static gboolean
fbExporterOpenTLS(
    fbExporter_t  *exporter,
    GError       **err)
{
    BIO     *conn = NULL;
    char     errbuf[FB_SSL_ERR_BUFSIZ];
    gboolean ok = TRUE;

    /* Initialize SSL context if necessary */
    if (!exporter->spec.conn->vssl_ctx) {
        if (!fbConnSpecInitTLS(exporter->spec.conn, FALSE, err)) {
            return FALSE;
        }
    }

    /* open underlying socket */
    if (!fbExporterOpenSocket(exporter, err)) {return FALSE;}

    /* wrap a stream BIO around the opened socket */
    if (!(conn = BIO_new_socket(exporter->stream.fd, 1))) {
        ok = FALSE;
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_CONN,
                    "couldn't wrap socket to %s:%s for TLS: %s",
                    exporter->spec.conn->host, exporter->spec.conn->svc,
                    errbuf);
        ERR_clear_error();
        goto end;
    }

    /* create SSL socket */
    if (!(exporter->ssl = SSL_new((SSL_CTX *)exporter->spec.conn->vssl_ctx))) {
        ok = FALSE;
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_CONN,
                    "couldnt create TLS socket: %s", errbuf);
        ERR_clear_error();
        goto end;
    }

    /* connect to it */
    SSL_set_bio(exporter->ssl, conn, conn);
    if (SSL_connect(exporter->ssl) <= 0) {
        ok = FALSE;
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_CONN,
                    "couldn't connect TLS socket to %s:%s: %s",
                    exporter->spec.conn->host, exporter->spec.conn->svc,
                    errbuf);
        ERR_clear_error();
        goto end;
    }

    /* FIXME do post-connection verification */

  end:
    if (!ok) {
        exporter->active = FALSE;
        if (exporter->ssl) {
            SSL_free(exporter->ssl);
            exporter->ssl = NULL;
        } else if (conn) {
            BIO_vfree(conn);
        }
    }
    return ok;
}

#ifdef HAVE_OPENSSL_DTLS

/**
 * fbExporterOpenDTLS
 *
 * Implements exporter->exopen()
 *
 * @param exporter
 * @param err
 *
 * @return
 *
 */
static gboolean
fbExporterOpenDTLS(
    fbExporter_t  *exporter,
    GError       **err)
{
    BIO            *conn = NULL;
    gboolean        ok = TRUE;
    struct sockaddr peer;
    char            errbuf[FB_SSL_ERR_BUFSIZ];
    size_t          peerlen = sizeof(struct sockaddr);

    /* Initialize SSL context if necessary */
    if (!exporter->spec.conn->vssl_ctx) {
        if (!fbConnSpecInitTLS(exporter->spec.conn, FALSE, err)) {
            return FALSE;
        }
    }

    /* open underlying socket */
    if (!fbExporterOpenSocket(exporter, err)) {return FALSE;}

    /* wrap a datagram BIO around the opened socket */
    if (!(conn = BIO_new_dgram(exporter->stream.fd, 1))) {
        ok = FALSE;
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_CONN,
                    "couldn't wrap socket to %s:%s for DTLS: %s",
                    exporter->spec.conn->host, exporter->spec.conn->svc,
                    errbuf);
        ERR_clear_error();
        goto end;
    }

    /* Tell dgram bio what its name is */
    if (getsockname(exporter->stream.fd, &peer, (socklen_t *)&peerlen) < 0) {
        ok = FALSE;
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_CONN,
                    "couldn't wrap socket to %s:%s for DTLS: %s",
                    exporter->spec.conn->host, exporter->spec.conn->svc,
                    strerror(errno));
        goto end;
    }
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    BIO_ctrl_set_connected(conn, 1, &peer);
#else
    BIO_ctrl_set_connected(conn, &peer);
#endif

    /* create SSL socket */
    if (!(exporter->ssl = SSL_new((SSL_CTX *)exporter->spec.conn->vssl_ctx))) {
        ok = FALSE;
        ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
        g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_CONN,
                    "couldnt create DTLS socket: %s", errbuf);
        ERR_clear_error();
        goto end;
    }

    /* connect to it */
    SSL_set_bio(exporter->ssl, conn, conn);
    SSL_set_connect_state(exporter->ssl);

    /* FIXME do post-connection verification */

  end:
    if (!ok) {
        exporter->active = FALSE;
        if (exporter->ssl) {
            SSL_free(exporter->ssl);
            exporter->ssl = NULL;
        } else if (conn) {
            BIO_vfree(conn);
        }
    }
    return ok;
}

#endif /* HAVE_OPENSSL_DTLS */

/**
 * fbExporterWriteTLS
 *
 * Implements exporter->exwrite()
 *
 *
 * @param exporter
 * @param msgbase
 * @param msglen
 * @param err
 *
 * @return
 */
static gboolean
fbExporterWriteTLS(
    fbExporter_t  *exporter,
    uint8_t       *msgbase,
    size_t         msglen,
    GError       **err)
{
    char errbuf[FB_SSL_ERR_BUFSIZ];
    int  rc;

    while (msglen) {
        rc = SSL_write(exporter->ssl, msgbase, msglen);
        if (rc <= 0) {
            ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
            g_set_error(err, FB_ERROR_DOMAIN, FB_ERROR_IO,
                        "I/O error: %s", errbuf);
            ERR_clear_error();
            return FALSE;
        }

        /* we sent some bytes - advance pointers */
        msglen -= rc;
        msgbase += rc;
    }

    return TRUE;
}

/**
 * fbExporterCloseTLS
 *
 * Implements exporter->exclose()
 *
 *
 * @param exporter
 *
 */
static void
fbExporterCloseTLS(
    fbExporter_t  *exporter)
{
    SSL_shutdown(exporter->ssl);
    SSL_free(exporter->ssl);
    exporter->active = FALSE;
}

#endif /* HAVE_OPENSSL */

/**
 * fbExporterGetMTU
 *
 *
 * NOTE: fixbuf/private.h function
 *
 * @param exporter
 *
 * @return
 */
uint16_t
fbExporterGetMTU(
    const fbExporter_t  *exporter)
{
    return exporter->mtu;
}

/**
 * fbExporterAllocNet
 *
 * @param spec
 *
 * @return
 */
fbExporter_t *
fbExporterAllocNet(
    const fbConnSpec_t  *spec)
{
    fbExporter_t *exporter = NULL;

    /* Host must not be null */
    g_assert(spec->host);

    /* Create a new exporter */
    exporter = g_slice_new0(fbExporter_t);

    /* Copy the connection specifier */
    exporter->spec.conn = fbConnSpecCopy(spec);

    /* Set up functions */
    switch (spec->transport) {
#ifdef FB_ENABLE_SCTP
      case FB_SCTP:
        exporter->exopen = fbExporterOpenSocket;
        exporter->exwrite = fbExporterWriteSCTP;
        exporter->exclose = fbExporterCloseSocket;
        exporter->sctp_mode = FB_F_SCTP_AUTOSTREAM;
        exporter->sctp_stream = 0;
        exporter->mtu = 8192;
        break;
#endif /* if FB_ENABLE_SCTP */
      case FB_TCP:
        exporter->exopen = fbExporterOpenSocket;
        exporter->exwrite = fbExporterWriteTCP;
        exporter->exclose = fbExporterCloseSocket;
        exporter->mtu = 8192;
        break;
      case FB_UDP:
        exporter->exopen = fbExporterOpenSocket;
        exporter->exwrite = fbExporterWriteUDP;
        exporter->exclose = fbExporterCloseSocket;
        exporter->mtu = 1420;
        break;
#ifdef HAVE_OPENSSL
#ifdef HAVE_OPENSSL_DTLS_SCTP
      case FB_DTLS_SCTP:
        exporter->exopen = fbExporterOpenDTLS;
        exporter->exwrite = fbExporterWriteTLS;
        exporter->exclose = fbExporterCloseTLS;
        exporter->sctp_mode = FB_F_SCTP_AUTOSTREAM;
        exporter->sctp_stream = 0;
        exporter->mtu = 8192;
        break;
#endif /* if HAVE_OPENSSL_DTLS_SCTP */
      case FB_TLS_TCP:
        exporter->exopen = fbExporterOpenTLS;
        exporter->exwrite = fbExporterWriteTLS;
        exporter->exclose = fbExporterCloseTLS;
        exporter->mtu = 8192;
        break;
#ifdef HAVE_OPENSSL_DTLS
      case FB_DTLS_UDP:
        exporter->exopen = fbExporterOpenDTLS;
        exporter->exwrite = fbExporterWriteTLS;
        exporter->exclose = fbExporterCloseTLS;
        exporter->mtu = 1320;
        break;
#endif /* if HAVE_OPENSSL_DTLS */
#endif /* if HAVE_OPENSSL */
      default:
#ifndef FB_ENABLE_SCTP
        if (spec->transport == FB_SCTP || spec->transport == FB_DTLS_SCTP) {
            g_error("Libfixbuf not enabled for SCTP Transport. "
                    " Run configure with --with-sctp");
        }
#endif /* ifndef FB_ENABLE_SCTP */
        if (spec->transport == FB_TLS_TCP || spec->transport == FB_DTLS_SCTP ||
            spec->transport == FB_DTLS_UDP)
        {
            g_error("Libfixbuf not enabled for this mode of transport. "
                    " Run configure with --with-openssl");
        }
    }

    /* Return new exporter */
    return exporter;
}

/**
 * fbExporterSetStream
 *
 * @param exporter
 * @param sctp_stream
 *
 */
void
fbExporterSetStream(
    fbExporter_t  *exporter,
    int            sctp_stream)
{
    exporter->sctp_mode &= ~FB_F_SCTP_AUTOSTREAM;
    exporter->sctp_stream = sctp_stream;
}

/**
 * fbExporterAutoStream
 *
 * @param exporter
 *
 *
 */
void
fbExporterAutoStream(
    fbExporter_t  *exporter)
{
    exporter->sctp_mode |= FB_F_SCTP_AUTOSTREAM;
}

#if 0
/**
 * fbExporterSetPRTTL
 *
 * @param exporter
 * @param pr_ttl
 *
 *
 */
void
fbExporterSetPRTTL(
    fbExporter_t  *exporter,
    int            pr_ttl)
{
    if (pr_ttl > 0) {
        exporter->sctp_mode |= FB_F_SCTP_PR_TTL;
        exporter->sctp_pr_param = pr_ttl;
    } else {
        exporter->sctp_mode &= ~FB_F_SCTP_PR_TTL;
        exporter->sctp_pr_param = 0;
    }
}
#endif  /* 0 */

/**
 * fbExportMessage
 *
 *
 * NOTE: fixbuf/private.h function
 *
 * @param exporter
 * @param msgbase
 * @param msglen
 * @param err
 *
 * @return
 *
 */
gboolean
fbExportMessage(
    fbExporter_t  *exporter,
    uint8_t       *msgbase,
    size_t         msglen,
    GError       **err)
{
    /* Ensure stream is open */
    if (!exporter->active) {
        g_assert(exporter->exopen);
        if (!exporter->exopen(exporter, err)) {return FALSE;}
        exporter->export_len = 0;
    }

    /* Attempt to write message */
    if (exporter->exwrite(exporter, msgbase, msglen, err)) {
        exporter->export_len += msglen;
        return TRUE;
    }

    /* Close exporter on write failure */
    if (exporter->exclose) {exporter->exclose(exporter);}
    return FALSE;
}

/**
 * fbExporterFree
 *
 *
 * NOTE: fixbuf/private.h function
 *
 * @param exporter
 *
 */
void
fbExporterFree(
    fbExporter_t  *exporter)
{
    fbExporterClose(exporter);
    if (exporter->exwrite == fbExporterWriteFile) {
        g_free(exporter->spec.path);
    } else {
        fbConnSpecFree(exporter->spec.conn);
    }

    g_slice_free(fbExporter_t, exporter);
}

/**
 * fbExporterClose
 *
 *
 *
 * @param exporter
 */
void
fbExporterClose(
    fbExporter_t  *exporter)
{
    if (exporter->active && exporter->exclose) {exporter->exclose(exporter);}
}

/**
 * fbExporterGetMsgLen
 *
 *
 * @param exporter
 */
size_t
fbExporterGetMsgLen(
    const fbExporter_t  *exporter)
{
    return exporter->msg_len;
}

/**
 * fbExporterGetOctetCount
 *
 *
 * @param exporter
 */
size_t
fbExporterGetOctetCount(
    const fbExporter_t  *exporter)
{
    return exporter->export_len;
}

/**
 * fbExporterGetOctetCountAndReset
 *
 *
 * @param exporter
 */
size_t
fbExporterGetOctetCountAndReset(
    fbExporter_t  *exporter)
{
    size_t len = exporter->export_len;
    exporter->export_len = 0;
    return len;
}

/**
 * fbExporterResetOctetCount
 *
 *
 * @param exporter
 */
void
fbExporterResetOctetCount(
    fbExporter_t  *exporter)
{
    exporter->export_len = 0;
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
