//  Copyright 2020-2023 Carnegie Mellon University
//  See license information in LICENSE.txt.

#include <fixbuf/public.h>
#define FATAL(e)                                \
    { fprintf(stderr, "Failed at %s:%d: %s\n",  \
              __FILE__, __LINE__, e->message);  \
        exit(1); }

int main()
{
    fbInfoElementSpec_t collectTemplate[] = {
        {"flowStartMilliseconds",               8, 0 },
        {"flowEndMilliseconds",                 8, 0 },
        {"sourceIPv4Address",                   4, 0 },
        {"destinationIPv4Address",              4, 0 },
        {"sourceTransportPort",                 2, 0 },
        {"destinationTransportPort",            2, 0 },
        {"protocolIdentifier",                  1, 0 },
        {"paddingOctets",                       3, 0 },
        {"packetTotalCount",                    8, 0 },
        {"octetTotalCount",                     8, 0 },
        {"ipPayloadPacketSection",              0, 0 },
        FB_IESPEC_NULL
    };
    struct collectRecord_st {
        uint64_t      flowStartMilliseconds;
        uint64_t      flowEndMilliseconds;
        uint32_t      sourceIPv4Address;
        uint32_t      destinationIPv4Address;
        uint16_t      sourceTransportPort;
        uint16_t      destinationTransportPort;
        uint8_t       protocolIdentifier;
        uint8_t       padding[3];
        uint64_t      packetTotalCount;
        uint64_t      octetTotalCount;
        fbVarfield_t  payload;
    } collectRecord;

    fbInfoModel_t   *model;
    fbSession_t     *session;
    fbCollector_t   *collector;
    fbTemplate_t    *tmpl;
    fBuf_t          *fbuf;
    uint16_t         tid;
    size_t           reclen;
    GError          *err = NULL;

    memset(&collectRecord, 0, sizeof(collectRecord));

    model = fbInfoModelAlloc();
    //  Use if needed to define elements used by YAF.
    //if (!fbInfoModelReadXMLFile(model, "cert_ipfix.xml", &err))
    //    FATAL(err);

    session = fbSessionAlloc(model);

    tmpl = fbTemplateAlloc(model);
    if (!fbTemplateAppendSpecArray(tmpl, collectTemplate, ~0, &err))
        FATAL(err);
    if (!(tid = fbSessionAddTemplate(
              session, TRUE, FB_TID_AUTO, tmpl, NULL, &err)))
        FATAL(err);

    collector = fbCollectorAllocFP(NULL, stdin);
    fbuf = fBufAllocForCollection(session, collector);

    if (!fBufSetInternalTemplate(fbuf, tid, &err))
        FATAL(err);

    reclen = sizeof(collectRecord);
    while (fBufNext(fbuf, (uint8_t *)&collectRecord, &reclen, &err)) {
        //  Print or process the collectRecord
        ldiv_t     dt;
        char       buf[256];
        size_t     sz;
        uint32_t   ip;
        dt = ldiv(collectRecord.flowStartMilliseconds, 1000);
        sz = strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                      gmtime((time_t *)&dt.quot));
        snprintf(buf + sz, sizeof(buf) - sz, ".%.3ld", dt.rem);
        printf("Start time:   %s\n", buf);
        dt = ldiv(collectRecord.flowEndMilliseconds, 1000);
        sz = strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                      gmtime((time_t *)&dt.quot));
        snprintf(buf + sz, sizeof(buf) - sz, ".%.3ld", dt.rem);
        printf("End time:     %s\n", buf);
        ip = collectRecord.sourceIPv4Address;
        printf("Source:       %d.%d.%d.%d:%d\n",
               (ip >> 24), (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff,
               collectRecord.sourceTransportPort);
        ip = collectRecord.destinationIPv4Address;
        printf("Destination:  %d.%d.%d.%d:%d\n",
               (ip >> 24), (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff,
               collectRecord.destinationTransportPort);
        printf("Protocol:     %d\n", collectRecord.protocolIdentifier);
        printf("Packets:      %" PRIu64 "\n", collectRecord.packetTotalCount);
        printf("Octets:       %" PRIu64 "\n", collectRecord.octetTotalCount);
        printf("Payload:     ");
        for (sz = 0; sz < collectRecord.payload.len; ++sz)
            printf(" %02x", collectRecord.payload.buf[sz]);
        printf("\n\n");
    }
    if (!g_error_matches(err, FB_ERROR_DOMAIN, FB_ERROR_EOF))
        FATAL(err);
    g_clear_error(&err);

    //  This frees the Buffer, Session, Templates, and Collector.
    fBufFree(fbuf);
    fbInfoModelFree(model);

    return 0;
}
// EndOfExample
// do not remove the previous line; Doxygen needs it

//  @DISTRIBUTION_STATEMENT_BEGIN@
//  libfixbuf 3.0.0
//
//  Copyright 2022 Carnegie Mellon University.
//
//  NO WARRANTY. THIS CARNEGIE MELLON UNIVERSITY AND SOFTWARE ENGINEERING
//  INSTITUTE MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. CARNEGIE MELLON
//  UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
//  AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
//  PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
//  THE MATERIAL. CARNEGIE MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF
//  ANY KIND WITH RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT
//  INFRINGEMENT.
//
//  Released under a GNU GPL 2.0-style license, please see LICENSE.txt or
//  contact permission@sei.cmu.edu for full terms.
//
//  [DISTRIBUTION STATEMENT A] This material has been approved for public
//  release and unlimited distribution.  Please see Copyright notice for
//  non-US Government use and distribution.
//
//  Carnegie Mellon(R) and CERT(R) are registered in the U.S. Patent and
//  Trademark Office by Carnegie Mellon University.
//
//  This Software includes and/or makes use of the following Third-Party
//  Software subject to its own license:
//
//  1. GLib-2.0 (https://gitlab.gnome.org/GNOME/glib/-/blob/main/COPYING)
//     Copyright 1995 GLib-2.0 Team.
//
//  2. Doxygen (http://www.gnu.org/licenses/old-licenses/gpl-2.0.html)
//     Copyright 2021 Dimitri van Heesch.
//
//  DM22-0006
//  @DISTRIBUTION_STATEMENT_END@
