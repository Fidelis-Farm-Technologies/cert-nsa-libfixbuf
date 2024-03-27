//  Copyright 2020-2023 Carnegie Mellon University
//  See license information in LICENSE.txt.

#include <fixbuf/public.h>
#define FATAL(e)                                \
    { fprintf(stderr, "Failed at %s:%d: %s\n",  \
              __FILE__, __LINE__, e->message);  \
        exit(1); }

int main()
{
    fbInfoElementSpec_t exportTemplate[] = {
        {"flowStartMilliseconds",               8, 0 },
        {"flowEndMilliseconds",                 8, 0 },
        {"octetTotalCount",                     8, 0 },
        {"packetTotalCount",                    8, 0 },
        {"sourceIPv4Address",                   4, 0 },
        {"destinationIPv4Address",              4, 0 },
        {"sourceTransportPort",                 2, 0 },
        {"destinationTransportPort",            2, 0 },
        {"protocolIdentifier",                  1, 0 },
        {"paddingOctets",                       3, 0 },
        {"ipPayloadPacketSection",              0, 0 },
        FB_IESPEC_NULL
    };
    struct exportRecord_st {
        uint64_t      flowStartMilliseconds;
        uint64_t      flowEndMilliseconds;
        uint64_t      octetTotalCount;
        uint64_t      packetTotalCount;
        uint32_t      sourceIPv4Address;
        uint32_t      destinationIPv4Address;
        uint16_t      sourceTransportPort;
        uint16_t      destinationTransportPort;
        uint8_t       protocolIdentifier;
        uint8_t       padding[3];
        fbVarfield_t  payload;
    } exportRecord;

    fbInfoModel_t   *model;
    fbSession_t     *session;
    fbExporter_t    *exporter;
    fbTemplate_t    *tmpl;
    fBuf_t          *fbuf;
    uint16_t         tid;
    int              i;
    GError          *err = NULL;

    memset(&exportRecord, 0, sizeof(exportRecord));

    model = fbInfoModelAlloc();
    //  Use if needed to define elements used by YAF.
    //if (!fbInfoModelReadXMLFile(model, "cert_ipfix.xml", &err))
    //    FATAL(err);

    session = fbSessionAlloc(model);
    exporter = fbExporterAllocFP(stdout);

    fbuf = fBufAllocForExport(session, exporter);

    tmpl = fbTemplateAlloc(model);
    if (!fbTemplateAppendSpecArray(tmpl, exportTemplate, ~0, &err))
        FATAL(err);

    if (!(tid = fbSessionAddTemplatesForExport(
              session, FB_TID_AUTO, tmpl, NULL, &err)))
        FATAL(err);

    //  No need to call this since templates were added after
    //  the Session and Exporter were linked to the fBuf_t.
    //if (!fbSessionExportTemplates(session, &err))
    //    FATAL(err);

    if (!fBufSetTemplatesForExport(fbuf, tid, &err))
        FATAL(err);

    //  This sample exports 5 records
    for (i = 0; i < 5; ++i) {
        //  Fill the exportRecord with data
        int payload[2];
        if (0 == i)  srand(0x2431e60f);
        exportRecord.flowEndMilliseconds = time(NULL) * 1000;
        exportRecord.flowStartMilliseconds =
            exportRecord.flowEndMilliseconds - rand();
        exportRecord.packetTotalCount = rand() >> 20;
        exportRecord.octetTotalCount =
            exportRecord.packetTotalCount * (rand() >> 26);
        exportRecord.sourceIPv4Address = rand();
        exportRecord.destinationIPv4Address = ~rand();
        exportRecord.sourceTransportPort = rand() & 0xffff;
        exportRecord.destinationTransportPort = rand() & 0xffff;
        switch (rand() & 0x7) {
          case 1:
            exportRecord.protocolIdentifier = 1;
            break;
          case 2:
          case 3:
            exportRecord.protocolIdentifier = 17;
            break;
          default:
            exportRecord.protocolIdentifier = 6;
            break;
        }
        payload[0] = rand();
        payload[1] = rand();
        exportRecord.payload.buf = (uint8_t *)payload;
        exportRecord.payload.len = sizeof(payload);

        if (!fBufAppend(fbuf, (uint8_t *)&exportRecord,
                        sizeof(exportRecord), &err))
            FATAL(err);
    }

    if (!fBufEmit(fbuf, &err))
        FATAL(err);;

    //  This frees the Buffer, Session, Templates, and Exporter.
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
