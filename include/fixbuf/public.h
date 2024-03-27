/*
 *  Copyright 2006-2023 Carnegie Mellon University
 *  See license information in LICENSE.txt.
 */
/**
 *  @file public.h
 *  Fixbuf IPFIX protocol library public interface
 */
/*
 *  ------------------------------------------------------------------------
 *  Authors: Brian Trammell, Dan Ruef
 *  ------------------------------------------------------------------------
 */

#ifndef _FB_PUBLIC_H_
#define _FB_PUBLIC_H_

#include <fixbuf/autoinc.h>
#include <fixbuf/version.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 *  Evaluates to a non-zero value if the version number of libfixbuf is at
 *  least major.minor.release.
 *  @since libfixbuf 1.8.0
 *
 *  @note When using libfixbuf 1.8.0, the calling code must use the following
 *  to get a reliable result from this macro, since public.h did not include
 *  version.h in that release of libfixbuf.
 *  @code{.c}
 *  #include <fixbuf/public.h>
 *  #include <fixbuf/version.h>
 *  @endcode
 */
#define FIXBUF_CHECK_VERSION(major, minor, release)                         \
    (FIXBUF_VERSION_MAJOR > (major) ||                                      \
     (FIXBUF_VERSION_MAJOR == (major) && FIXBUF_VERSION_MINOR > (minor)) || \
     (FIXBUF_VERSION_MAJOR == (major) && FIXBUF_VERSION_MINOR == (minor) && \
      FIXBUF_VERSION_RELEASE >= (release)))

/*
 *  Error Handling Definitions
 */

/**
 *  All fixbuf errors are returned within the FB_ERROR_DOMAIN domain.
 */
#define FB_ERROR_DOMAIN             g_quark_from_string("fixbufError")
/**
 *  No template was available for the given template ID, the session's
 *  template table is full, an attempt was made to modify a template that was
 *  previously added to a session, or the template is at its maximum size.
 */
#define FB_ERROR_TMPL               1
/**
 *  End of IPFIX message. Either there are no more records present in the
 *  message on read, or the message MTU has been reached on write.
 */
#define FB_ERROR_EOM                2
/**
 *  End of IPFIX Message stream. No more messages are available from the
 *  transport layer on read, either because the session has closed, or the
 *  file has been processed.
 */
#define FB_ERROR_EOF                3
/**
 *  Illegal IPFIX message content on read. The input stream is malformed, or
 *  is not an IPFIX Message after all.
 */
#define FB_ERROR_IPFIX              4
/**
 *  A message was received larger than the collector buffer size.  This should
 *  only occur when reading from an @ref fBuf_t configured with
 *  fBufSetBuffer().
 */
#define FB_ERROR_BUFSZ              5
/**
 *  The requested feature is not yet implemented.
 */
#define FB_ERROR_IMPL               6
/**
 *  An unspecified I/O error occured.
 */
#define FB_ERROR_IO                 7
/**
 *  No data is available for reading from the transport layer.  Either a
 *  transport layer read was interrupted, or timed out.
 */
#define FB_ERROR_NLREAD             8
/**
 *  An attempt to write data to the transport layer failed due to closure of
 *  the remote end of the connection. Currently only occurs with the TCP
 *  transport layer.
 */
#define FB_ERROR_NLWRITE            9
/**
 *  The specified Information Element does not exist in the Information Model.
 */
#define FB_ERROR_NOELEMENT          10
/**
 *  A connection or association could not be established or maintained.
 */
#define FB_ERROR_CONN               11
/**
 *  Illegal NetflowV9 content on a read.  Can't parse the Netflow header or
 *  the stream is not a NetflowV9 stream
 */
#define FB_ERROR_NETFLOWV9          12
/**
 *  Miscellaneous error occured during translator operation
 */
#define FB_ERROR_TRANSMISC          13
/**
 *  Illegal sFlow content on a read.
 */
#define FB_ERROR_SFLOW              14
/**
 *  Setup error
 */
#define FB_ERROR_SETUP              15
/**
 *  Internal template with defaulted (zero) element sizes. See @ref
 *  fbInfoElementSpec_st.
 */
#define FB_ERROR_LAXSIZE            16

/*
 *  Public Datatypes and Constants
 */

/**
 *  An IPFIX message buffer. Used to encode and decode records from IPFIX
 *  Messages. The internals of this structure are private to libfixbuf.
 */
typedef struct fBuf_st fBuf_t;

/**
 *  A variable-length field value. Variable-length information element content
 *  is represented by an fbVarfield_t on the internal side of the transcoder;
 *  that is, variable length fields in an IPFIX Message must be represented by
 *  this structure within the application record.
 */
typedef struct fbVarfield_st {
    /** Length of content in buffer. */
    size_t    len;
    /**
     * Content buffer. In network byte order as appropriate. On write, this
     * buffer will be copied into the message buffer. On read, this buffer
     * points into the message buffer and must be copied by the caller before
     * any call to fBufNext().
     */
    uint8_t  *buf;
} fbVarfield_t;


/**
 *  An IPFIX information model. Contains information element definitions.  The
 *  internals of this structure are private to libfixbuf.
 */
typedef struct fbInfoModel_st fbInfoModel_t;

/**
 *  An iterator over the information elements in an information model.
 */
typedef GHashTableIter fbInfoModelIter_t;

/**
 *  Convenience macro for creating full @ref fbInfoElement_t static
 *  initializer.  Used for creating information element arrays suitable for
 *  passing to fbInfoModelAddElementArray().
 */
#define FB_IE_INIT_FULL(_name_, _ent_, _num_, _len_, _flags_, \
                        _min_, _max_, _type_, _desc_)         \
    { _ent_, _num_, _len_, _flags_, _type_, _min_, _max_, _name_, _desc_ }

/**
 *  Convenience macro for creating full @ref fbInfoElement_t static
 *  initializers.  Used for creating information element arrays suitable for
 *  passing to fbInfoModelAddElementArray().
 *
 *  _rev_ and _endian_ are booleans,
 *  _semantics_ is a @ref fbInfoElementSemantics_t
 *  _units_ is a @ref fbInfoElementUnits_t
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_IE_INIT_FULL_SPLIT(_name_, _ent_, _num_, _len_,           \
                              _rev_, _endian_, _semantics_, _units_, \
                              _min_, _max_, _type_, _desc_)          \
    FB_IE_INIT_FULL(_name_, _ent_, _num_, _len_,                     \
                    (((_rev_) ? FB_IE_F_REVERSIBLE : 0) |            \
                     ((_endian_) ? FB_IE_F_ENDIAN : 0) |             \
                     (((_semantic) & 0xFF) << 8) |                   \
                     (((_units_) & 0xFFFF) << 16)),                  \
                    _min_, _max_, _type_, _desc_)

/**
 *  Convenience macro for creating default @ref fbInfoElement_t static
 *  initializers.  Used for creating information element arrays suitable for
 *  passing to fbInfoModelAddElementArray().
 */
#define FB_IE_INIT(_name_, _ent_, _num_, _len_, _flags_) \
    FB_IE_INIT_FULL(_name_, _ent_, _num_, _len_, _flags_, 0, 0, 0, (char *)NULL)


/**
 *  Convenience macro defining a null @ref fbInfoElement_t initializer to
 *  terminate a constant information element array for passing to
 *  fbInfoModelAddElementArray().
 */
#define FB_IE_NULL FB_IE_INIT(NULL, 0, 0, 0, 0)

/**
 *  Convenience macro for extracting the information element semantic value
 *  (@ref fbInfoElementSemantics_t) from the flags member of the @ref
 *  fbInfoElement_t struct.
 *
 *  @see fbInfoElementGetSemantics()
 *  https://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-semantics
 *
 */
#define FB_IE_SEMANTIC(flags) ((flags & 0x0000ff00) >> 8)

/**
 *  Convenience macro for extracting the information element units value (@ref
 *  fbInfoElementUnits_t) from the flags member of the @ref fbInfoElement_t
 *  struct.
 *
 *  @see fbInfoElementGetUnits()
 *  https://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-units.
 */
#define FB_IE_UNITS(flags) ((flags & 0xFFFF0000) >> 16)

/**
 *  Default treatment of `flags` member in an @ref fbInfoElement_t. Provided
 *  for initializer convenience.  Corresponds to octet-array semantics for a
 *  non-reversible, non-alien IE.
 */
#define FB_IE_F_NONE                            0x00000000

/**
 *  Information element endian conversion bit in the flags member of @ref
 *  fbInfoElement_t. If set, IE is an integer and will be endian-converted on
 *  transcode.
 *
 *  @see fbInfoElementIsEndian()
 */
#define FB_IE_F_ENDIAN                          0x00000001

/**
 *  Information element reversible bit in the flags member of @ref
 *  fbInfoElement_t.  Adding the information element via
 *  fbInfoModelAddElement() or fbInfoModelAddElementArray() will cause a
 *  second, reverse information element to be added to the model following the
 *  conventions in IETF [RFC 5103][].  This means that, if there is no
 *  enterprise number, the reverse element will get an enterprise number of
 *  @ref FB_IE_PEN_REVERSE, and if there is an enterprise number, the reverse
 *  element's numeric identifier will have its @ref FB_IE_VENDOR_BIT_REVERSE
 *  bit set.
 *
 *  [RFC 5103]: https://tools.ietf.org/html/rfc5103
 *
 *  @see fbInfoElementIsReversible()
 */
#define FB_IE_F_REVERSIBLE                      0x00000040

/**
 *  Information element alien bit in the flags member of @ref
 *  fbInfoElement_t. If set, IE is enterprise-specific and was recieved via an
 *  external template at a Collecting Process. It is therefore subject to
 *  semantic typing via options (not yet implemented). Do not set this flag on
 *  information elements added programmatically to an information model via
 *  fbInfoModelAddElement() or fbInfoModelAddElementArray().
 *
 *  @see fbInfoElementIsAlien()
 */
#define FB_IE_F_ALIEN                           0x00000080

/**
 *  Information Element Semantics - See [RFC 5610][]
 *
 *  [RFC 5610]:  https://tools.ietf.org/html/rfc5610
 *
 *  https://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-semantics
 */

/**
 *  The Information Element Semantics bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as a quantity.
 *
 *  fbInfoElementGetSemantics() and @ref fbInfoElementSemantics_t provide an
 *  alternate interface.
 */
#define FB_IE_QUANTITY                          0x00000100

/**
 *  The Information Element Semantics bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as a totalCounter.
 *
 *  fbInfoElementGetSemantics() and @ref fbInfoElementSemantics_t provide an
 *  alternate interface.
 */
#define FB_IE_TOTALCOUNTER                      0x00000200

/**
 *  The Information Element Semantics bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as a deltaCounter.
 *
 *  fbInfoElementGetSemantics() and @ref fbInfoElementSemantics_t provide an
 *  alternate interface.
 */
#define FB_IE_DELTACOUNTER                      0x00000300

/**
 *  The Information Element Semantics bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as an identifier.
 *
 *  fbInfoElementGetSemantics() and @ref fbInfoElementSemantics_t provide an
 *  alternate interface.
 */
#define FB_IE_IDENTIFIER                        0x00000400

/**
 *  The Information Element Semantics bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as a flags element.
 *
 *  fbInfoElementGetSemantics() and @ref fbInfoElementSemantics_t provide an
 *  alternate interface.
 */
#define FB_IE_FLAGS                             0x00000500

/**
 *  The Information Element Semantics bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as a list element.
 *
 *  fbInfoElementGetSemantics() and @ref fbInfoElementSemantics_t provide an
 *  alternate interface.
 */
#define FB_IE_LIST                              0x00000600

/**
 *  The Information Element Semantics bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as an SNMP counter.
 *
 *  fbInfoElementGetSemantics() and @ref fbInfoElementSemantics_t provide an
 *  alternate interface.
 *  @since libfixbuf 2.0.0
 */
#define FB_IE_SNMPCOUNTER                       0x00000700

/**
 *  The Information Element Semantics bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as a SNMP gauge.
 *
 *  fbInfoElementGetSemantics() and @ref fbInfoElementSemantics_t provide an
 *  alternate interface.
 *  @since libfixbuf 2.0.0
 */
#define FB_IE_SNMPGAUGE                         0x00000800

/**
 *  The Information Element Semantics bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as having no specific
 *  semantics.
 *
 *  fbInfoElementGetSemantics() and @ref fbInfoElementSemantics_t provide an
 *  alternate interface.
 */
#define FB_IE_DEFAULT                           0x00000000


/**
 *  fbInfoElementSemantics_t defines the possible semantics of an @ref
 *  fbInfoElement_t.  See [RFC 5610][]
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 *
 *  @see fbInfoElementGetSemantics()
 *
 *  https://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-semantics
 *  @since libfixbuf 3.0.0
 */
typedef enum fbInfoElementSemantics_en {
    /**
     * Describes an information element as having no specific semantics.
     */
    FB_IE_SEM_DEFAULT = 0,
    /**
     * Describes an information element as a quantity.
     */
    FB_IE_SEM_QUANTITY = 1,
    /**
     * Describes an information element as a totalCounter.
     */
    FB_IE_SEM_TOTALCOUNTER = 2,
    /**
     * Describes an information element as a deltaCounter.
     */
    FB_IE_SEM_DELTACOUNTER = 3,
    /**
     * Describes an information element as an identifier.
     */
    FB_IE_SEM_IDENTIFIER = 4,
    /**
     * Describes an information element as a flags element.
     */
    FB_IE_SEM_FLAGS = 5,
    /**
     * Describes an information element as a list element.
     */
    FB_IE_SEM_LIST = 6,
    /**
     * Describes an information element as an SNMP counter.
     */
    FB_IE_SEM_SNMPCOUNTER = 7,
    /**
     * Describes an information element as a SNMP gauge.
     */
    FB_IE_SEM_SNMPGAUGE = 8
} fbInfoElementSemantics_t;


/**
 *  Information Element Units - See RFC 5610
 *
 *  https://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-units
 */

/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting bits.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_BITS                           0x00010000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting octets.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_OCTETS                         0x00020000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting packets.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_PACKETS                        0x00030000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting flows.
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 *
 */
#define FB_UNITS_FLOWS                          0x00040000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting seconds.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_SECONDS                        0x00050000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting
 *  milliseconds.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_MILLISECONDS                   0x00060000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting
 *  microseconds.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_MICROSECONDS                   0x00070000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting
 *  nanoseconds.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_NANOSECONDS                    0x00080000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting 4-octet
 *  words.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_WORDS                          0x00090000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting messages.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_MESSAGES                       0x000A0000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting hops.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_HOPS                           0x000B0000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting entries.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_ENTRIES                        0x000C0000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting frames.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 */
#define FB_UNITS_FRAMES                         0x000D0000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as counting ports.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 *  @since libfixbuf 2.1.0
 */
#define FB_UNITS_PORTS                          0x000E0000
/**
 *  The Information Element Units bits within the flags member of an @ref
 *  fbInfoElement_t that denote the information element as having units
 *  inferred from some other element.
 *
 *  fbInfoElementGetUnits() and @ref fbInfoElementUnits_t provide an alternate
 *  interface.
 *  @since libfixbuf 2.1.0
 */
#define FB_UNITS_INFERRED                       0x000F0000

/**
 *  fbInfoElementUnits_t defines the possible units on an @ref
 *  fbInfoElement_t.  See [RFC 5610][]
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 *
 *  @see fbInfoElementGetUnits()
 *
 *  https://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-units
 *  @since libfixbuf 3.0.0
 */
typedef enum fbInfoElementUnits_en {
    /**
     * Describes an information element as having no units.
     */
    FB_IE_UNITS_NONE = 0,
    /**
     * Describes an information element as counting bits.
     */
    FB_IE_UNITS_BITS = 1,
    /**
     * Describes an information element as counting octets.
     */
    FB_IE_UNITS_OCTETS = 2,
    /**
     * Describes an information element as counting packets.
     */
    FB_IE_UNITS_PACKETS = 3,
    /**
     * Describes an information element as counting flows.
     */
    FB_IE_UNITS_FLOWS = 4,
    /**
     * Describes an information element as counting seconds.
     */
    FB_IE_UNITS_SECONDS = 5,
    /**
     * Describes an information element as counting milliseconds.
     */
    FB_IE_UNITS_MILLISECONDS = 6,
    /**
     * Describes an information element as counting microseconds.
     */
    FB_IE_UNITS_MICROSECONDS = 7,
    /**
     * Describes an information element as counting nanoseconds.
     */
    FB_IE_UNITS_NANOSECONDS = 8,
    /**
     * Describes an information element as counting 4-octet words, as for
     * IPv4-header length.
     */
    FB_IE_UNITS_WORDS = 9,
    /**
     * Describes an information element as counting messages.
     */
    FB_IE_UNITS_MESSAGES = 10,
    /**
     * Describes an information element as counting hops, as for TTL.
     */
    FB_IE_UNITS_HOPS = 11,
    /**
     * Describes an information element as counting entries, as for MPLS label
     * stack.
     */
    FB_IE_UNITS_ENTRIES = 12,
    /**
     * Describes an information element as counting frames, as for Layer 2
     * frames.
     */
    FB_IE_UNITS_FRAMES = 13,
    /**
     * Describes an information element as counting ports.
     */
    FB_IE_UNITS_PORTS = 14,
    /**
     * Describes an information element as having units inferred from some
     * other element.  For example, the units of absoluteError (IANA 320) are
     * inferred from the element that it is the absoluteError of.
     */
    FB_IE_UNITS_INFERRED = 15
} fbInfoElementUnits_t;

/**
 *  Information element length constant for variable-length IE.
 */
#define FB_IE_VARLEN                            65535

/**
 *  The Reverse Information Element Private Enterprise Number as defined by
 *  [RFC 5103][] section 6.1.  When an information element having the @ref
 *  FB_IE_F_REVERSIBLE flag bit set and a zero enterprise number (i.e., an
 *  IANA-assigned information element) is added to a model, the reverse IE is
 *  generated by setting the enterprise number to this constant.
 *
 *  [RFC 5103]: https://tools.ietf.org/html/rfc5103
 */
#define FB_IE_PEN_REVERSE                       29305

/**
 *  Reverse information element bit for vendor-specific information elements
 *  (see [RFC 5103][] section 6.2). If an information element with @ref
 *  FB_IE_F_REVERSIBLE and a non-zero enterprise number (i.e., a
 *  vendor-specific information element) is added to a model, the reverse IE
 *  number will be generated by ORing this bit with the given forward
 *  information element number.
 *
 *  [RFC 5103]: https://tools.ietf.org/html/rfc5103
 */
#define FB_IE_VENDOR_BIT_REVERSE                0x4000

/**
 *  Reverse information element name prefix. This string is prepended to an
 *  information element name, and the first character after this string is
 *  capitalized, when generating a reverse information element.
 */
#define FB_IE_REVERSE_STR                       "reverse"

/** Length of reverse information element name prefix. */
#define FB_IE_REVERSE_STRLEN                    7

/**
 *  Generic Information Element ID for undefined Cisco NetFlow v9 Elements.
 */
#define FB_CISCO_GENERIC                       9999
/**
 *  Information Element ID for Cisco NSEL Element NF_F_FW_EVENT often exported
 *  by Cisco's ASA Device.  This must be converted to a different Information
 *  Element ID due to the reverse IE bit in IPFIX.  Cisco uses IE ID 40005.
 *  http://www.cisco.com/en/US/docs/security/asa/asa82/netflow/netflow.html
 */
#define FB_CISCO_ASA_EVENT_ID                  9998
/**
 *  Information Element ID for Cisco NSEL Element NF_F_FW_EXT_EVENT often
 *  exported by Cisco's ASA Device.  This must be converted to a different
 *  Information Element ID due to the reverse IE bit in IPFIX.  Cisco uses IE
 *  ID 33002
 *  http://www.cisco.com/en/US/docs/security/asa/asa82/netflow/netflow.html
 *  More Information about event codes can be found here:
 *  http://www.cisco.com/en/US/docs/security/asa/asa84/system/netflow/netflow.pdf
 */
#define FB_CISCO_ASA_EVENT_XTRA                9997


/**
 *  From [RFC 5610][]: A description of the abstract data type of an IPFIX
 *  information element as registered in the IANA IPFIX IE Data Type
 *  subregistry.
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 *
 *  @see fbInfoElementGetType(), fbTemplateFieldGetType()
 *  https://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-information-element-data-types
 */
typedef enum fbInfoElementDataType_en {
    /** The "octetArray" data type: A finite-length string of
     *  octets. */
    FB_OCTET_ARRAY,
    /** The "unsigned8" data type: A non-negative integer value in the
     *  range of 0 to 255 (0xFF). */
    FB_UINT_8,
    /** The "unsigned16" data type: A non-negative integer value in
     *  the range of 0 to 65535 (0xFFFF). */
    FB_UINT_16,
    /** The "unsigned32" data type: A non-negative integer value in
     *  the range of 0 to 4_294_967_295 (0xFFFFFFFF). */
    FB_UINT_32,
    /** The "unsigned64" data type: A non-negative integer value in
     *  the range of 0 to 18_446_744_073_709_551_615
     *  (0xFFFFFFFFFFFFFFFF). */
    FB_UINT_64,
    /** The "signed8" data type: An integer value in the range of -128
     *  to 127. */
    FB_INT_8,
    /** The "signed16" data type: An integer value in the range of
     *  -32768 to 32767.*/
    FB_INT_16,
    /** The "signed32" data type: An integer value in the range of
     *  -2_147_483_648 to 2_147_483_647.*/
    FB_INT_32,
    /** The "signed64" data type: An integer value in the range of
    *  -9_223_372_036_854_775_808 to 9_223_372_036_854_775_807. */
    FB_INT_64,
    /** The "float32" data type: An IEEE single-precision 32-bit
     *  floating point type. */
    FB_FLOAT_32,
    /** The "float64" data type: An IEEE double-precision 64-bit
     *  floating point type. */
    FB_FLOAT_64,
    /** The "boolean" data type: A binary value: "true" or "false". */
    FB_BOOL,
    /** The "macAddress" data type: A MAC-48 address as a string of 6
     *  octets. */
    FB_MAC_ADDR,
    /** The "string" data type: A finite-length string of valid
     *  characters from the Unicode character encoding set. */
    FB_STRING,
    /** The "dateTimeSeconds" data type: An unsigned 32-bit integer
     *  containing the number of seconds since the UNIX epoch,
     *  1970-Jan-01 00:00 UTC.  */
    FB_DT_SEC,
    /** The "dateTimeMilliseconds" data type: An unsigned 64-bit
     *  integer containing the number of milliseconds since the UNIX
     *  epoch, 1970-Jan-01 00:00 UTC.  */
    FB_DT_MILSEC,
    /** The "dateTimeMicroseconds" data type: Two 32-bit fields where
     *  the first is the number seconds since the NTP epoch,
     *  1900-Jan-01 00:00 UTC, and the second is the number of
     *  1/(2^32) fractional seconds. */
    FB_DT_MICROSEC,
    /** The "dateTimeMicroseconds" data type: Two 32-bit fields where
     *  the first is the number seconds since the NTP epoch,
     *  1900-Jan-01 00:00 UTC, and the second is the number of
     *  1/(2^32) fractional seconds. */
    FB_DT_NANOSEC,
    /** The "ipv4Address" data type: A value of an IPv4 address. */
    FB_IP4_ADDR,
    /** The "ipv6Address" data type: A value of an IPv6 address. */
    FB_IP6_ADDR,
    /** The "basicList" data type: A structured data element as
     *  described in RFC6313, Section 4.5.1. */
    FB_BASIC_LIST,
    /** The "subTemplateList" data type: A structured data element as
     *  described in RFC6313, Section 4.5.2. */
    FB_SUB_TMPL_LIST,
    /** The "subTemplateMultiList" data type: A structured data element as
     *  described in RFC6313, Section 4.5.3. */
    FB_SUB_TMPL_MULTI_LIST
} fbInfoElementDataType_t;

/**
 *  An IPFIX Template or Options Template. Templates define the structure of
 *  data records and options records within an IPFIX Message.
 *
 *  The internals of this structure are private to libfixbuf.
 */
typedef struct fbTemplate_st fbTemplate_t;

/**
 *  A single IPFIX Information Element definition.  An Information Element
 *  describes the standard properties for a field in a record.  This structure
 *  is contained in an @ref fbInfoModel_t.  As of libfixbuf 3.0.0, an @ref
 *  fbTemplateField_t represents an fbInfoElement_t when used in an @ref
 *  fbTemplate_t.
 *
 *  @since Changed in libfixbuf 3.0.0: Added the `name` member and removed the
 *  `midx` and `ref` members.
 */
typedef struct fbInfoElement_st {
    /**
     * Private Enterprise Number. Set to 0 for IETF-defined IEs.
     */
    uint32_t     ent;
    /**
     * Information Element number. Does not include the on-wire
     * enterprise bit; i.e. num & 0x8000 == 0 even if ent > 0.
     */
    uint16_t     num;
    /**
     * Standard information element length in octets; FB_IE_VARLEN for
     * strings, octetArrays, and structured data (lists).
     */
    uint16_t     len;
    /** Flags. Bitwise OR of FB_IE_F_* constants. */
    /** Use Macros to get units and semantic */
    uint32_t     flags;
    /** Data Type, an @ref fbInfoElementDataType_t */
    uint8_t      type;
    /** range min */
    uint64_t     min;
    /** range max */
    uint64_t     max;
    /**
     * Information element name. Storage for this is managed by fbInfoModel.
     * @since libfixbuf 3.0.0
     */
    const char  *name;
    /** Element description. */
    const char  *description;
} fbInfoElement_t;


/**
 *  Returns TRUE if the private enterprise number and element id of @ref
 *  fbInfoElement_t match the given values.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementCheckIdent(_ie, _enterpriseNumber, _elementId) \
    ((_elementId) == (_ie)->num && (_enterpriseNumber) == (_ie)->ent)

/**
 *  Returns the description of an @ref fbInfoElement_t, a C-string.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetDescription(_ie)   ((_ie)->description)

/**
 *  Returns the element id of an @ref fbInfoElement_t, a uint16_t.  This value
 *  does not include the on-wire enterprise bit.  That is, the high-order bit
 *  is always 0.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetId(_ie)        ((_ie)->num)

/**
 *  Returns the length of an @ref fbInfoElement_t, a uint16_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetLen(_ie)       ((_ie)->len)

/**
 *  Returns the maximum value of an @ref fbInfoElement_t, a uint64_t.  Returns
 *  0 if no range values have been specified for the element.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetMax(_ie)       ((_ie)->max)

/**
 *  Returns the minimum value of an @ref fbInfoElement_t, a uint64_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetMin(_ie)       ((_ie)->min)

/**
 *  Returns the name of an @ref fbInfoElement_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetName(_ie)      ((_ie)->name)

/**
 *  Returns the private enterprise number (PEN) of an @ref fbInfoElement_t, a
 *  uint32_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetPEN(_ie)       ((_ie)->ent)

/**
 *  Returns the semantics of an @ref fbInfoElement_t, an @ref
 *  fbInfoElementSemantics_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetSemantics(_ie)                         \
    ((fbInfoElementSemantics_t)(FB_IE_SEMANTIC((_ie)->flags)))

/**
 *  Returns the data type of an @ref fbInfoElement_t, a @ref
 *  fbInfoElementDataType_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetType(_ie)              \
    ((fbInfoElementDataType_t)((_ie)->type))

/**
 *  Returns the units of an @ref fbInfoElement_t, an @ref
 *  fbInfoElementUnits_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementGetUnits(_ie)                     \
    ((fbInfoElementUnits_t)(FB_IE_UNITS((_ie)->units)))

/**
 *  Returns TRUE if the @ref fbInfoElement_t is unknown and was received via
 *  an external template from a collecting process and FALSE if not.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsAlien(_ie)      (!!((_ie)->flags & FB_IE_F_ALIEN))

/**
 *  Returns TRUE if the @ref fbInfoElement_t should be endian-converted on
 *  transcode and FALSE if not.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsEndian(_ie)     (!!((_ie)->flags & FB_IE_F_ENDIAN))

/**
 *  Returns TRUE if the @ref fbInfoElement_t should be considered reversible
 *  and FALSE if not.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsReversible(_ie) (!!((_ie)->flags & FB_IE_F_REVERSIBLE))

/**
 *  Returns TRUE if the @ref fbInfoElement_t is a unsigned integer type and
 *  FALSE if not.  The values of @ref fbInfoElementDataType_t that are
 *  unsigned integers are @ref FB_UINT_8, @ref FB_UINT_16, @ref FB_UINT_32,
 *  and @ref FB_UINT_64.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsUnsigned(_ie) \
    ((_ie)->type >= FB_UINT_8 && (_ie)->type <= FB_UINT_64)

/**
 *  Returns TRUE if the @ref fbInfoElement_t is a signed integer type and
 *  FALSE if not.  The values of @ref fbInfoElementDataType_t that are signed
 *  integers are @ref FB_INT_8, @ref FB_INT_16, @ref FB_INT_32, and @ref
 *  FB_INT_64.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsSigned(_ie) \
    ((_ie)->type >= FB_INT_8 && (_ie)->type <= FB_INT_64)

/**
 *  Returns TRUE if the @ref fbInfoElement_t is an integer type and FALSE if
 *  not.  The values of @ref fbInfoElementDataType_t that are integer types
 *  are @ref FB_UINT_8, @ref FB_UINT_16, @ref FB_UINT_32, and @ref FB_UINT_64,
 *  @ref FB_INT_8, @ref FB_INT_16, @ref FB_INT_32, and @ref FB_INT_64.
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsInteger(_ie) \
    ((_ie)->type >= FB_UINT_8 && (_ie)->type <= FB_INT_64)

/**
 *  Returns TRUE if the @ref fbInfoElement_t is a floating point type and
 *  FALSE if not.  The values of @ref fbInfoElementDataType_t that are floats
 *  are @ref FB_FLOAT_32 and @ref FB_FLOAT_64.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsFloat(_ie) \
    ((_ie)->type >= FB_FLOAT_32 && (_ie)->type <= FB_FLOAT_64)

/**
 *  Returns TRUE if the @ref fbInfoElement_t is an unsigned integer type, a
 *  signed integer type, or a float type, and FALSE if not.  The values of
 *  @ref fbInfoElementDataType_t that are numbers are @ref FB_UINT_8, @ref
 *  FB_UINT_16, @ref FB_UINT_32, and @ref FB_UINT_64, @ref FB_INT_8, @ref
 *  FB_INT_16, @ref FB_INT_32, @ref FB_INT_64, @ref FB_FLOAT_32, and @ref
 *  FB_FLOAT_64.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsNumber(_ie) \
    ((_ie)->type >= FB_UINT_8 && (_ie)->type <= FB_FLOAT_64)

/**
 *  Returns TRUE if the @ref fbInfoElement_t is an IP Address type, and FALSE
 *  if not.  The values of @ref fbInfoElementDataType_t that are numbers are
 *  @ref FB_IP4_ADDR and @ref FB_IP6_ADDR.
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsIPAddress(_ie) \
    ((_ie)->type >= FB_IP4_ADDR && (_ie)->type <= FB_IP6_ADDR)

/**
 *  Returns TRUE if the @ref fbInfoElement_t is a date-time type, and FALSE if
 *  not.  The values of @ref fbInfoElementDataType_t that are date-times are
 *  @ref FB_DT_SEC, @ref FB_DT_MILSEC, @ref FB_DT_MICROSEC, and @ref
 *  FB_DT_NANOSEC.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsDatetime(_ie) \
    ((_ie)->type >= FB_DT_SEC && (_ie)->type <= FB_DT_NANOSEC)

/**
 *  Returns TRUE if the type of the @ref fbInfoElement_t is a structured data
 *  element (a list) and FALSE if not.  The values of @ref
 *  fbInfoElementDataType_t that are structured data are @ref FB_BASIC_LIST,
 *  @ref FB_SUB_TMPL_LIST, and @ref FB_SUB_TMPL_MULTI_LIST.
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsList(_ie) \
    ((_ie)->type >= FB_BASIC_LIST && (_ie)->type <= FB_SUB_TMPL_MULTI_LIST)

/**
 *  Returns TRUE if the @ref fbInfoElement_t is paddingOctets (IANA defined
 *  element 210) and FALSE if not.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbInfoElementIsPadding(_ie)    (210 == (_ie)->num && 0 == (_ie)->ent)


/**
 *  fbTemplateField_t represents an @ref fbInfoElement_t that has been added
 *  to an @ref fbTemplate_t.
 *
 *  @since libfixbuf 3.0.0
 */
typedef struct fbTemplateField_st {
    /** Pointer to canonical IE. */
    const fbInfoElement_t  *canon;
    /**
     * Multiple IE index.  Defines the ordering of identical IEs in templates.
     * Set and managed automatically by the fbTemplate_t routines.
     */
    uint16_t                midx;
    /**
     * Octet-length of this field as specified by the template specification.
     */
    uint16_t                len;
    /**
     * Octet-offset of this field in a record. Calculated by template.
     */
    uint16_t                offset;
    /**
     * Octet-length of this IE in memory.   Is this needed?
     */
    /*uint16_t                memsize; */
    /**
     * Position of this field in the template.  Is this needed?
     */
    /*uint16_t                position; */
    /**
     * Pointer to the template that own this field.  Is this needed?
     */
    fbTemplate_t  *tmpl;
} fbTemplateField_t;


/**
 *  A static initializer for an @ref fbTemplateField_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TEMPLATEFIELD_INIT  {NULL, 0, 0, 0, NULL}


/**
 *  Returns TRUE if the private enterprise number and element id of @ref
 *  fbInfoElement_t used by the @ref fbTemplateField_t match the given values.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbTemplateFieldCheckIdent(_field, _enterpriseNumber, _elementId) \
    ((_elementId) == (_field)->canon->num &&                              \
     (_enterpriseNumber) == (_field)->canon->ent)

/**
 *  Returns the @ref fbInfoElement_t used by an @ref fbTemplateField_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbTemplateFieldGetIE(_field)       ((_field)->canon)

/**
 *  Returns the element ID of the @ref fbInfoElement_t used by an @ref
 *  fbTemplateField_t, a uint16_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbTemplateFieldGetId(_field)       ((_field)->canon->num)

/**
 *  Returns the octet length of an @ref fbTemplateField_t, a uint16_t, as
 *  specified when the @ref fbTemplate_t was created.  That is, this is the
 *  length specified by the @ref fbInfoElementSpec_t or @ref
 *  fbInfoElementSpecId_t for Templates created by the user, or the length
 *  read from a Collector.
 *
 *  @since libfixbuf 3.0.0
 *  @see fbTemplateFieldGetMemsize()
 */
#define  fbTemplateFieldGetLen(_field)      ((_field)->len)

/**
 *  Returns the octet length in memory of an @ref fbTemplateField_t, a
 *  unit16_t.  For fixed length elements, this is same as
 *  fbTemplateFieldGetLen().  For variable-length fields, this is the size of
 *  the data structure used by libfixbuf to represent the element in memory.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbTemplateFieldGetMemsize(_field)                      \
    ((FB_IE_VARLEN != (_field)->len)                            \
     ? (_field)->len                                            \
     : ((FB_BASIC_LIST == (_field)->canon->type)                \
        ? sizeof(fbBasicList_t)                                 \
        : ((FB_SUB_TMPL_LIST == (_field)->canon->type)          \
           ? sizeof(fbSubTemplateList_t)                        \
           : ((FB_SUB_TMPL_MULTI_LIST == (_field)->canon->type) \
              ? sizeof(fbSubTemplateMultiList_t)                \
              : sizeof(fbVarfield_t)))))

/**
 *  Returns the name of the @ref fbInfoElement_t used by an @ref
 *  fbTemplateField_t, a const char *.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbTemplateFieldGetName(_field)   ((_field)->canon->name)

/**
 *  Returns the offset of the @ref fbTemplateField_t from the start of an (in
 *  meory) record, a uint16_t.  This is the offset of this field in an array
 *  of uint8_t's representing a data record.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbTemplateFieldGetOffset(_field)   ((_field)->offset)

/**
 *  Returns the private enterprise number (PEN) of the @ref fbInfoElement_t
 *  used by an @ref fbTemplateField_t, a uint32_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbTemplateFieldGetPEN(_field)      ((_field)->canon->ent)

/**
 *  Returns the data type of the @ref fbInfoElement_t used by an @ref
 *  fbTemplateField_t, a @ref fbInfoElementDataType_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbTemplateFieldGetType(_field)                 \
    ((fbInfoElementDataType_t)((_field)->canon->type))

/**
 *  Returns the repeat index for the @ref fbInfoElement_t used by this @ref
 *  fbTemplateField_t in the containing @ref fbTemplate_t.
 *
 *  When an InfoElement is repeated within a Template, the repeat index, also
 *  called the multi-use index, is used to distinguish them.  The InfoElement
 *  with the lowest offset has repeat index 0, the next lowest has index 1, et
 *  cetera.
 *
 *  The value can also be considered as the number of times this
 *  TemplateField's InfoElement is used in the Template prior to this
 *  occurrence.
 *
 *  @since libfixbuf 3.0.0
 */
#define  fbTemplateFieldGetRepeat(_field)   ((_field)->midx)

/**
 *  fbTemplateIter_t iterates over the @ref fbTemplateField_t objects in an
 *  @ref fbTemplate_t.
 *
 *  To use, define the fbTemplateIter_t on the stack, using @ref
 *  FB_TEMPLATE_ITER_NULL to zero it if desired.  Bind it to a Template with
 *  fbTemplateIterInit(), iterate over the TemplateFields with
 *  fbTemplateIterNext() until that function returns NULL.
 *
 *  @since libfixbuf 3.0.0
 */
typedef struct fbTemplateIter_st {
    const fbTemplate_t  *tmpl;
    uint16_t             pos;
} fbTemplateIter_t;

/**
 *  A static initializer for an @ref fbTemplateIter_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TEMPLATE_ITER_NULL  {NULL, UINT16_MAX}

/**
 *  Initializes a TemplateIter to visit the @ref fbTemplateField_t objects in
 *  a Template.  The TemplateFields are visited in the order they appear.  Use
 *  fbTemplateIterNext() to visit each TemplateField.
 *
 *  @param iter  The TemplateIter to initialize
 *  @param tmpl  The Template to be iterated over
 *  @since libfixbuf 3.0.0
 */
void
fbTemplateIterInit(
    fbTemplateIter_t    *iter,
    const fbTemplate_t  *tmpl);

/**
 *  Returns the same TemplateField returned by the most recent call to
 *  fbTemplateIterNext().  Does not advance the iterator.  Returns NULL if
 *  fbTemplateIterNext() has not been called or if the iterator has visited
 *  all TemplateFields.  Assumes fbTemplateIterInit() has been called on
 *  `iter`.
 *
 *  @param iter  A TemplateIter
 *  @return The same TemplateField returned by the most recent call to
 *  fbTemplateIterNext().
 *  @since libfixbuf 3.0.0
 */
const fbTemplateField_t *
fbTemplateIterGetField(
    const fbTemplateIter_t  *iter);

/**
 *  Returns the position of the @ref fbTemplateField_t returned by the most
 *  recent call to fbTemplateIterNext().  Does not advance the iterator.
 *  Returns fbTemplateCountElements() if the iterator is exhausted.  Returns
 *  UINT16_MAX if fbTemplateIterNext() has not been called.
 *
 *  @param iter  A TemplateIter
 *  @return  The position of the iterator.
 *  @since libfixbuf 3.0.0
 */
uint16_t
fbTemplateIterGetPosition(
    const fbTemplateIter_t  *iter);

/**
 *  Returns the Template to which the TemplateIter is bound.
 *
 *  @param iter  A TemplateIter
 *  @since libfixbuf 3.0.0
 */
const fbTemplate_t *
fbTemplateIterGetTemplate(
    const fbTemplateIter_t  *iter);

/**
 *  Returns the next TemplateField in the @ref fbTemplate_t.  Returns the
 *  first TemplateField if this is the first call to fbTemplateIterNext()
 *  after calling fbTemplateIterInit().  Returns NULL if the iterator is
 *  exhausted.  Assumes fbTemplateIterInit() has been called on `iter`.
 *
 *  To get this same TemplateField, you may call fbTemplateIterGetField().
 *
 *  @param iter  A TemplateIter
 *  @return  The next TemplateField in the iterator.
 *  @since libfixbuf 3.0.0
 */
const fbTemplateField_t *
fbTemplateIterNext(
    fbTemplateIter_t  *iter);


/**
 *  A data type to represent an @ref fbInfoElement_t of type basicList (@ref
 *  FB_BASIC_LIST).  A BasicList contains zero or more instances of a single
 *  Information Element.
 *
 *  When reading from a @ref fbCollector_t, you should first initialize the
 *  BasicList (fbBasicListCollectorInit() or memset() the list to `0`).  After
 *  calling fBufNext(), you may query the BasicList for its element
 *  (fbBasicListGetInfoElement()), get the number of elements
 *  (fbBasicListCountElements()), get the element at a specific position
 *  (fbBasicListGetIndexedDataPtr()), and iterate over the elements
 *  (fbBasicListGetNextPtr() or fbBLNext()).  When extracting data from the
 *  basicList, reduced length encoding of the element needs to be accounted
 *  for by the caller.
 *
 *  When building a BasicList for an @ref fbExporter_t, use
 *  fbBasicListInitWithLength() to initialize the BasicList, then fill in the
 *  value for each element in the list.  To add entries to the BasicList while
 *  building it, use fbBasicListAddNewElements().
 *
 *  In either case, the data buffer of the BasicList should be freed when it
 *  no longer needed (fbBasicListClear()) or before calling fBufNext() again.
 */
typedef struct fbBasicList_st fbBasicList_t;

/**
 *  A data type to represent an @ref fbInfoElement_t of type subTemplateList
 *  (@ref FB_SUB_TMPL_LIST).  A SubTemplateList contains zero or more
 *  instances of structured a data type, where the data type is described by a
 *  single @ref fbTemplate_t.
 *
 *  When reading from a @ref fbCollector_t, you should first initialize the
 *  SubTemplateList (fbSubTemplateListCollectorInit() or memset() the list to
 *  `0`).  After calling fBufNext(), you may query the SubTemplateList for its
 *  Template (fbSubTemplateListGetTemplate() and
 *  fbSubTemplateListGetTemplateID()), get the number of elements
 *  (fbSubTemplateListCountElements()), get the element at a specific position
 *  (fbSubTemplateListGetIndexedDataPtr()), and iterate over the elements
 *  (fbSubTemplateListGetNextPtr() or fbSTLNext()).
 *
 *  When building a SubTemplateList for an @ref fbExporter_t, use
 *  fbSubTemplateListInit() to initialize the SubTemplateList, then fill in
 *  the value for each element in the list.  To add entries to the
 *  SubTemplateList while building it, use fbSubTemplateListAddNewElements().
 *
 *  In either case, the data buffer of the SubTemplateList should be freed
 *  when it no longer needed (fbSubTemplateListClear()) or before calling
 *  fBufNext() again.
 */
typedef struct fbSubTemplateList_st fbSubTemplateList_t;

/**
 *  A data type to represent an @ref fbInfoElement_t of type
 *  subTemplateMultiList (@ref FB_SUB_TMPL_MULTI_LIST).  A
 *  SubTemplateMultiList contains zero or more instances of structured data,
 *  where different elements may be described by different @ref fbTemplate_t
 *  definitions.
 *
 *  In libfixbuf, groups of consecutive data instances that share the same
 *  Template are represented by an @ref fbSubTemplateMultiListEntry_t.  The
 *  SubTemplateMultiList contains only the list semantic, an array of
 *  SubTemplateMultiListEntries, and the length of the array.
 *
 *  A SubTemplateMultiList may be thought of as a list of SubTemplateLists
 *  (where SubTemplateMultiListEntry is equivalent to SubTemplateList).
 *
 *  When reading from a @ref fbCollector_t, you call fBufNext() to fill the
 *  SubTemplateMultiList.  You may then query the SubTemplateMultiList for the
 *  number of entries (fbSubTemplateMultiListCountElements()), get the entry
 *  at a specific position (fbSubTemplateMultiListGetIndexedEntry()), and
 *  iterate over the entries (fbSubTemplateMultiListGetNextEntry() or
 *  fbSTMLNext()).  For each entry, you will need to iterate over its contents
 *  (details in the description of @ref fbSubTemplateMultiListEntry_t).
 *
 *  When building a SubTemplateMultiList for an @ref fbExporter_t, use
 *  fbSubTemplateMultiListInit() to initialize the SubTemplateMultiList, then
 *  fill in the value for each @ref fbSubTemplateMultiListEntry_t in the list.
 *  To add entries to the SubTemplateMultiList while building it, use
 *  fbSubTemplateMultiListAddNewEntries().
 *
 *  In either case, the data buffer of the SubTemplateMultiList should be
 *  freed (fbSubTemplateMultiListClear()) before collecting or exporting
 *  another record.
 */
typedef struct fbSubTemplateMultiList_st fbSubTemplateMultiList_t;


/**
 *  fbRecord_t maintains a buffer holding an IPFIX record's data, the @ref
 *  fbTemplate_t that describes that data and the template's ID.
 *
 *  The caller should initialize `rec` with an data buffer and store the
 *  length of that buffer in `reccapacity`.  All fixbuf functions treat
 *  `reccapacity` as constant.  FIXME---We could allow the fbRecord_t to
 *  maintain this buffer itself.
 *
 *  Before each call to fBufNextRecord(), the caller sets `tid` to the
 *  template to use to read the record.  fBufNextRecord() updates `tmpl` and
 *  `recsize` reads the data into `rec`.
 *
 *  The caller is responsible for calling fbRecordFreeLists() before reusing
 *  the record.  FIXME---We should probably change this.
 *
 *  @since libfixbuf 3.0.0
 */
typedef struct fbRecord_st {
    /** The template that describes the bytes in `rec`. */
    const fbTemplate_t  *tmpl;
    /** The buffer holding the data for a record. */
    uint8_t             *rec;
    /** The number of bytes allocated for the `rec` buffer. */
    size_t               reccapacity;
    /** The number of bytes used by the record current in `rec`. */
    size_t               recsize;
    /** The template ID for `tmpl`. */
    uint16_t             tid;
} fbRecord_t;


/**
 *  A static initializer for an @ref fbRecord_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_RECORD_INIT  {NULL, NULL, 0, 0, 0}


/**
 *  fbRecordValue_t is used to access the value of a single Element (or Field)
 *  in an @ref fbRecord_t.
 *
 *  When an fbRecordValue_t is created on the stack, it should be initialized
 *  with @ref FB_RECORD_VALUE_INIT.
 *
 *  If you use it to access @ref FB_STRING or @ref FB_OCTET_ARRAY data, call
 *  either fbRecordValueClear() to ensure its internal string buffer is freed
 *  or fbRecordValueTakeVarfieldBuf() to take ownship of the internal string
 *  buffer before the RecordValue leaves scope.
 *
 *  @since libfixbuf 3.0.0
 */
typedef struct fbRecordValue_st {
    /** The element that describes this data. */
    const fbInfoElement_t  *ie;
    /** An internal buffer used to store the `varfield` value. */
    GString                *stringbuf;
    /** The value depending on the type of `ie`. */
    union v_un {
        /**
         * Used when `ie` is @ref FB_IP6_ADDR.
         */
        uint8_t                          ip6[16];
        /**
         * Used when `ie` is @ref FB_BASIC_LIST.
         */
        const fbBasicList_t             *bl;
        /**
         * Used when `ie` is @ref FB_SUB_TMPL_LIST.
         */
        const fbSubTemplateList_t       *stl;
        /**
         * Used when `ie` is @ref FB_SUB_TMPL_MULTI_LIST.
         */
        const fbSubTemplateMultiList_t  *stml;
        /**
         * Used when `ie` is @ref FB_STRING or @ref FB_OCTET_ARRAY.  The `buf`
         * member of `varfield` is backed by a GString; the value is
         * terminated by `\0`.  The `len` member is the length of `buf` not
         * including the terminator.
         */
        fbVarfield_t                     varfield;
        /**
         * Used when `ie` is @ref FB_DT_SEC, @ref FB_DT_MILSEC, @ref
         * FB_DT_MICROSEC, or @ref FB_DT_NANOSEC.  `dt.tv_nsec` is in
         * nanoseconds; divide by 1000000 to get milliseconds or 1000 to get
         * microseconds.
         */
        struct timespec                  dt;
        /**
         * Used when `ie` is @ref FB_IP4_ADDR.
         */
        uint32_t                         ip4;
        /**
         * Used when `ie` is @ref FB_UINT_8, @ref FB_UINT_16, @ref FB_UINT_32,
         * @ref FB_UINT_64, or @ref FB_BOOL (0 == false, 1 == true).
         */
        uint64_t                         u64;
        /**
         * Used when `ie` is @ref FB_INT_8, @ref FB_INT_16, @ref FB_INT_32, or
         * @ref FB_INT_64.
         */
        int64_t                          s64;
        /**
         * Used when `ie` is @ref FB_FLOAT_64 or @ref FB_FLOAT_32.
         */
        double                           dbl;
        /**
         * Used when `ie` is @ref FB_MAC_ADDR.
         */
        uint8_t                          mac[6];
    } v;
} fbRecordValue_t;


/**
 *  A static initializer for an @ref fbRecordValue_t.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_RECORD_VALUE_INIT \
    { NULL, NULL, { {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} } }


/**
 *  The corresponding C struct for a record whose template is the
 *  [RFC 5610][] Information Element Type Options Template.
 *
 *  If collecting this record, use the function
 *  fbInfoElementAddOptRecElement() to add the @ref fbInfoElement_t it
 *  describes to the @ref fbInfoModel_t.
 *
 *  To export RFC5610 elements, use fbSessionSetMetadataExportElements().
 *
 *  @code{.c}
 *  fbInfoElementSpec_t rfc5610_spec[] = {
 *      {"privateEnterpriseNumber",         4, 0 },
 *      {"informationElementId",            2, 0 },
 *      {"informationElementDataType",      1, 0 },
 *      {"informationElementSemantics",     1, 0 },
 *      {"informationElementUnits",         2, 0 },
 *      {"paddingOctets",                   6, 0 },
 *      {"informationElementRangeBegin",    8, 0 },
 *      {"informationElementRangeEnd",      8, 0 },
 *      {"informationElementName",          FB_IE_VARLEN, 0 },
 *      {"informationElementDescription",   FB_IE_VARLEN, 0 },
 *      FB_IESPEC_NULL
 *  };
 *  @endcode
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 */
typedef struct fbInfoElementOptRec_st {
    /** private enterprise number */
    uint32_t       ie_pen;
    /** information element id */
    uint16_t       ie_id;
    /** ie data type (@ref fbInfoElementDataType_t) */
    uint8_t        ie_type;
    /** ie semantic (@ref fbInfoElementSemantics_t) */
    uint8_t        ie_semantic;
    /** ie units (@ref fbInfoElementUnits_t) */
    uint16_t       ie_units;
    /** padding to align within the template */
    uint8_t        padding[6];
    /** ie range min */
    uint64_t       ie_range_begin;
    /** ie range max */
    uint64_t       ie_range_end;
    /** information element name */
    fbVarfield_t   ie_name;
    /** information element description */
    fbVarfield_t   ie_desc;
} fbInfoElementOptRec_t;


/**
 *  Template ID argument used when adding an @ref fbTemplate_t to an @ref
 *  fbSession_t that automatically assigns a template ID.
 *
 *  Functions that accept this value are fbSessionAddTemplate(),
 *  fbSessionAddTemplatesForExport(), fbSessionSetMetadataExportElements(),
 *  and fbSessionSetMetadataExportTemplates().
 *
 *  For internal templates, FB_TID_AUTO starts from 65535 and decrements.  For
 *  external templates, FB_TID_AUTO starts from 256 and increments.  This is
 *  to avoid inadvertant unrelated external and internal templates having the
 *  same ID.
 */
#define FB_TID_AUTO         0

/**
 *  Reserved set ID for template sets, per RFC 7011.
 */
#define FB_TID_TS           2

/**
 *  Reserved set ID for options template sets, per RFC 7011.
 */
#define FB_TID_OTS          3

/**
 *  Minimum non-reserved template ID available for data sets, per RFC 7011.
 */
#define FB_TID_MIN_DATA     256

/**
 *  Convenience macro defining a null information element specification
 *  initializer (@ref fbInfoElementSpec_t) to terminate a constant information
 *  element specifier array for passing to fbTemplateAppendSpecArray().
 */
#define FB_IESPEC_NULL { NULL, 0, 0 }

/**
 *  A single IPFIX Information Element specification.  Used to name an
 *  information element (@ref fbInfoElement_t) for inclusion in an @ref
 *  fbTemplate_t by fbTemplateAppendSpecArray() and for querying whether a
 *  template contains an element via fbTemplateContainsElementByName().
 *
 *  @see fbInfoElementSpecId_t
 */
typedef struct fbInfoElementSpec_st {
    /**
     *  Information element name
     */
    const char  *name;
    /**
     *  The size of the information element in bytes.  For internal templates,
     *  this is the size of the memory location that will be filled by the
     *  transcoder (i.e., the size of a field in a struct).
     *
     *  As of libfixbuf 2.0, zero may not be used as the size of elements used
     *  in internal templates except as noted below. (Attempting to do so
     *  returns @ref FB_ERROR_LAXSIZE.) This restriction is so changes in the
     *  default length of the element (as defined in the Information Model)
     *  will not silently be different then the sizes of fields in an internal
     *  struct definition.
     *
     *  Zero may be used as the size of @ref FB_IE_VARLEN elements, which is
     *  the same as specifying FB_IE_VARLEN. Because of this, fixbuf does not
     *  allow an @ref FB_OCTET_ARRAY or @ref FB_STRING field to have a fixed
     *  length of zero despite RFC 7011 allowing it.
     *
     *  This field can also be used to specify reduced-length encoding.
     */
    uint16_t   len_override;
    /**
     *  Application flags word. If nonzero, then the `wantedFlags` argument to
     *  fbTemplateAppendSpec(), fbTemplateAppendSpecArray(), or
     *  fbTemplateContainsAllFlaggedElementsByName() MUST match ALL the bits
     *  of this flags word in order for the information element to be
     *  considered.
     */
    uint32_t   flags;
} fbInfoElementSpec_t;

/**
 *  Convenience macro defining a null information element specification
 *  initializer (@ref fbInfoElementSpecId_t) to terminate a constant
 *  information element specifier array for passing to
 *  fbTemplateAppendArraySpecId().
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_IESPECID_NULL { {0, 0}, 0, 0 }

/**
 *  A single IPFIX Information Element specification using the element's
 *  numeric identifier and private enterprise number.  Used to name an
 *  information element (@ref fbInfoElement_t) for inclusion in an @ref
 *  fbTemplate_t by fbTemplateAppendArraySpecId().
 *
 *  @see fbInfoElementSpec_t
 *  @since libfixbuf 3.0.0
 */
typedef struct fbInfoElementSpecId_st {
    /**
     *  The numeric pair to identify the element.
     */
    struct fbInfoElementSpecIdIdent_st {
        /**
         *  The private enterprise number (PEN) of the element or 0 for an
         *  IANA-defined element.
         */
        uint32_t   enterprise_id;
        /**
         *  The numeric identifier of the element within its PEN.
         */
        uint16_t   element_id;
    } ident;
    /**
     *  The size of the information element in bytes.  For internal templates,
     *  this is the size of the memory location that will be filled by the
     *  transcoder (i.e., the size of a field in a struct).
     *
     *  As of libfixbuf 2.0, zero may not be used as the size of elements used
     *  in internal templates except as noted below. (Attempting to do so
     *  returns @ref FB_ERROR_LAXSIZE.) This restriction is so changes in the
     *  default length of the element (as defined in the Information Model)
     *  will not silently be different then the sizes of fields in an internal
     *  struct definition.
     *
     *  Zero may be used as the size of @ref FB_IE_VARLEN elements, which is
     *  the same as specifying FB_IE_VARLEN. Because of this, fixbuf does not
     *  allow an @ref FB_OCTET_ARRAY or @ref FB_STRING field to have a fixed
     *  length of zero despite RFC 7011 allowing it.
     *
     *  This field can also be used to specify reduced-length encoding.
     */
    uint16_t   len_override;
    /**
     *  Application flags word. If nonzero, then the `wantedFlags` argument to
     *  fbTemplateAppendSpecId() or fbTemplateAppendArraySpecId() MUST match
     *  ALL the bits of this flags word in order for the information element
     *  to be considered.
     */
    uint32_t   flags;
} fbInfoElementSpecId_t;

/**
 *  An IPFIX Transport Session state container. Though Session creation and
 *  lifetime are managed by the @ref fbCollector_t and @ref fbExporter_t
 *  types, each @ref fBuf_t buffer uses this type to store session state,
 *  including internal and external Templates and Message Sequence Number
 *  information.
 */
typedef struct fbSession_st fbSession_t;

/** Transport protocol for connection specifier. */
typedef enum fbTransport_en {
    /**
     * Partially reliable datagram transport via SCTP.
     * Only available if fixbuf was built with SCTP support.
     */
    FB_SCTP,
    /** Reliable stream transport via TCP. */
    FB_TCP,
    /** Unreliable datagram transport via UDP. */
    FB_UDP,
    /**
     * Secure, partially reliable datagram transport via DTLS over SCTP.  Only
     * available if fixbuf was built with OpenSSL support.  Requires an
     * OpenSSL implementation of DLTS over SCTP, not yet available.
     */
    FB_DTLS_SCTP,
    /**
     * Secure, reliable stream transport via TLS over TCP.
     * Only available if fixbuf was built with OpenSSL support.
     */
    FB_TLS_TCP,
    /**
     * Secure, unreliable datagram transport via DTLS over UDP.
     * Only available if fixbuf was built with OpenSSL support.
     * Requires OpenSSL 0.9.8 or later with DTLS support.
     */
    FB_DTLS_UDP
} fbTransport_t;

/**
 *  Connection specifier. Used to define a peer address for @ref
 *  fbExporter_t, or a passive address for @ref fbListener_t.
 */
typedef struct fbConnSpec_st {
    /** Transport protocol to use */
    fbTransport_t   transport;
    /** Hostname to connect/listen to. NULL to listen on all interfaces. */
    char           *host;
    /** Service name or port number to connect/listen to. */
    char           *svc;
    /** Path to certificate authority file. Only used for OpenSSL transport. */
    char           *ssl_ca_file;
    /** Path to certificate file. Only used for OpenSSL transport. */
    char           *ssl_cert_file;
    /** Path to private key file. Only used for OpenSSL transport. */
    char           *ssl_key_file;
    /** Private key decryption password. Only used for OpenSSL transport. */
    char           *ssl_key_pass;
    /**
     * Pointer to address info cache. Initialize to NULL.
     * For fixbuf internal use only.
     */
    void           *vai;
    /**
     * Pointer to SSL context cache. Initialize to NULL.
     * For fixbuf internal use only.
     */
    void           *vssl_ctx;
} fbConnSpec_t;


/**
 *  fbTemplateInfo_t describes an @ref fbTemplate_t.  This information
 *  is encoded and sent between IPFIX senders and receives to provide enhanced
 *  description of the structure of the templates.
 *
 *  @since libfixbuf 3.0.0
 */
typedef struct fbTemplateInfo_st fbTemplateInfo_t;

/**
 *  fbBasicListInfo_t describes a basicList and is used by @ref
 *  fbTemplateInfo_t.  fbBasicListInfo_t contains the enterprise and element
 *  numbers of the @ref fbInfoElement_t the list contains and the numbers for
 *  the basicList itself.
 *
 *  @since libfixbuf 3.0.0
 */
typedef struct fbBasicListInfo_st fbBasicListInfo_t;

/**
 *  Allocates and returns an empty template metadata information structure.
 *  Exits the application on allocation failure.
 *
 *  @return A newly allocated TemplateInfo.
 *  @since libfixbuf 3.0.0
 */
fbTemplateInfo_t *
fbTemplateInfoAlloc(
    void);

/**
 *  Frees a template metadata structure.
 *
 *  @param tmplInfo The object to be freed.
 *  @since libfixbuf 3.0.0
 */
void
fbTemplateInfoFree(
    fbTemplateInfo_t  *tmplInfo);

/**
 *  Makes a copy of TemplateInfo.  A TemplateInfo is tied to a @ref
 *  fbTemplate_t in a specific @ref fbSession_t.  To use it in another Session
 *  it should be copied.
 *
 *  @param tmplInfo The object to be copied.
 *  @since libfixbuf 3.0.0
 */
fbTemplateInfo_t *
fbTemplateInfoCopy(
    const fbTemplateInfo_t  *tmplInfo);

/**
 *  Initializes a template metadata structure.
 *
 *  @param tmplInfo     the object to initialize
 *  @param name         the name of the template, required
 *  @param description  a description of the template
 *  @param appLabel     the silkAppLabel value this template is associate with
 *  @param parentTid    the parent template id.  use @ref FB_TMPL_MD_LEVEL_0
 *                      if this is top-level and @ref FB_TMPL_MD_LEVEL_1 if
 *                      this is used immediately below top-level.
 *  @return TRUE unless name is NULL, then FALSE
 *  @since libfixbuf 3.0.0
 */
gboolean
fbTemplateInfoInit(
    fbTemplateInfo_t  *tmplInfo,
    const char        *name,
    const char        *description,
    uint16_t           appLabel,
    uint16_t           parentTid);

/**
 *  Value to use for the `parentTid` parameter of fbTemplateInfoInit()
 *  to indicate the template is not used as a sub-record (that is, that it is
 *  not used in a subTemplateList or subTemplateMultiList).
 *
 *  @since libfixbuf 3.0.0
 */
#define  FB_TMPL_MD_LEVEL_0    0
/**
 *  Value to use for the `parentTid` parameter of fbTemplateInfoInit()
 *  to indicate the template is used as a first level sub-record.
 *
 *  @since libfixbuf 3.0.0
 */
#define  FB_TMPL_MD_LEVEL_1    1
/**
 *  Value set by an @ref fbCollector_t to denote that the version of the
 *  received TemplateInfo does not include parent template ID.
 *
 *  @since libfixbuf 3.0.0
 */
#define  FB_TMPL_MD_LEVEL_NA   0xFF

/**
 *  Adds a description for an @ref fbBasicList_t that is used by the @ref
 *  fbTemplate_t described by `tmplInfo`.
 *
 *  @param tmplInfo   the template info to modify
 *  @param blEnt      the enterprise number of the @ref fbInfoElement_t of the
 *                    the list itself
 *  @param blNum      the element number of the @ref fbInfoElement_t of the
 *                    list itself
 *  @param contentEnt the enterprise number of the @ref fbInfoElement_t for
 *                    the contents of the list
 *  @param contentNum the element number of the @ref fbInfoElement_t for the
 *                    contents of the list
 *
 *  @since libfixbuf 3.0.0
 */
void
fbTemplateInfoAddBasicList(
    fbTemplateInfo_t  *tmplInfo,
    uint32_t           blEnt,
    uint16_t           blNum,
    uint32_t           contentEnt,
    uint16_t           contentNum);

/**
 *  Returns the `applabel` value specified in fbTemplateInfoInit().
 *
 *  @since libfixbuf 3.0.0
 */
uint16_t
fbTemplateInfoGetApplabel(
    const fbTemplateInfo_t  *tmplInfo);

/**
 *  Returns the `parentTid` value specified in fbTemplateInfoInit().
 *
 *  @since libfixbuf 3.0.0
 */
uint16_t
fbTemplateInfoGetParentTid(
    const fbTemplateInfo_t  *tmplInfo);

/**
 *  Returns the `name` value specified in fbTemplateInfoInit().
 *
 *  @since libfixbuf 3.0.0
 */
const char *
fbTemplateInfoGetName(
    const fbTemplateInfo_t  *tmplInfo);

/**
 *  Returns the `description` value specified in fbTemplateInfoInit().
 *
 *  @since libfixbuf 3.0.0
 */
const char *
fbTemplateInfoGetDescription(
    const fbTemplateInfo_t  *tmplInfo);

/**
 *  Returns the Template ID value associated with this TemplateInfo.
 *
 *  @since libfixbuf 3.0.0
 */
uint16_t
fbTemplateInfoGetTemplateId(
    const fbTemplateInfo_t  *tmplInfo);

/**
 *  Returns a description of the next @ref fbBasicList_t that is used by the
 *  @ref fbTemplate_t described by `tmplInfo`.  Returns NULL when there are no
 *  more BasicListInfos.  When `blInfo` is NULL, returns the the first
 *  BasicListInfo.
 *
 *  Use fbTemplateInfoAddBasicList() to add descriptions of BasicLists
 *  to an fbTemplateInfo_t.
 *
 *  @param tmplInfo  the template info to be queried
 *  @param blInfo    the value returned on the previous call to this function
 *                   or NULL for the initial call
 *  @return The information for the next basicList, or NULL when no more data.
 *  @since libfixbuf 3.0.0
 */
const fbBasicListInfo_t *
fbTemplateInfoGetNextBasicList(
    const fbTemplateInfo_t   *tmplInfo,
    const fbBasicListInfo_t  *blInfo);

/**
 *  Gets from `blInfo` the element numbers for the @ref fbInfoElement_t used
 *  by the BasicList element itself.  That is, this is the element that a @ref
 *  fbTemplate_t which uses this BasicList would contain.  Returns the element
 *  number and sets the referent of `pen` to the enterprise number when `pen`
 *  is not NULL.
 *
 *  @param blInfo   the BasicListInfo to be queried
 *  @param pen      an output parameter filled with the enterprise number if
 *                  non-NULL
 *  @return The element number used by the BasicList itself.
 *  @since libfixbuf 3.0.0
 *
 *  @see fbBasicListInfoGetContentIdent() to get information about
 *  the element that the BasicList contains.
 *
 *  fbTemplateInfoGetNextBasicList() to get a handle to a BasicListInfo.
 */
uint16_t
fbBasicListInfoGetListIdent(
    const fbBasicListInfo_t  *blInfo,
    uint32_t                 *pen);

/**
 *  Gets from `blInfo` the element numbers for the @ref fbInfoElement_t that
 *  the BasicList contains.  These are the values that would be returned by
 *  fbBasicListGetElementIdent().  Returns the element number and sets the
 *  referent of `pen` to the enterprise number when `pen` is not NULL.
 *
 *  @param blInfo   the BasicListInfo to be queried
 *  @param pen      an output parameter filled with the enterprise number if
 *                  non-NULL
 *  @return The element number of the InfoElement the BasicList contains.
 *  @since libfixbuf 3.0.0
 *  @see fbBasicListInfoGetListIdent() to get information about the element
 *  used by a @ref fbTemplate_t that uses this BasicList.
 *  fbTemplateInfoGetNextBasicList() to get a handle to a BasicListInfo.
 */
uint16_t
fbBasicListInfoGetContentIdent(
    const fbBasicListInfo_t  *blInfo,
    uint32_t                 *pen);


/**
 *  Returns the metadata for template whose ID is `tid` in the current domain
 *  or NULL if no metadata is available.
 *
 *  @param session   A session to be queried
 *  @param tid       The template ID to search for
 *  @return  The template information if found or NULL otherwise.
 *  @since libfixbuf 3.0.0
 */
const fbTemplateInfo_t *
fbSessionGetTemplateInfo(
    const fbSession_t  *session,
    uint16_t            tid);


/**
 *  Finds the template path of the @ref fbTemplate_t described by `tmplInfo`.
 *
 *  FIXME: Should the caller supply tmplInfo or TID?  If tmplInfo, should we
 *  confirm that tmplInfo exists in this session?
 *
 *  The function fills the array `path` with the Template IDs starting at
 *  `tmplInfo` (at position 0), its parent at position 1, and continuing to
 *  the top-level template at position "count-1".  The function returns the
 *  value of count.  The caller must supply the number of IDs `path` can hold
 *  in the `path_size` parameter.
 *
 *  To get the count of the Template described by `tmplInfo` without getting
 *  the template IDs, pass NULL to `path` and 0 to `path_size`.
 *
 *  If `tmplInfo` is for a top-level template, the path[0] is that ID and the
 *  return value is 1.
 *
 *  For a first-level template, its ID is in path[0], 0 is in path[1] (meaning
 *  any top-level template), and the return value is 2.
 *
 *  If 'path' is not-null and the depth is larger than `path_size`, the
 *  function sets the `err` value and returns 0.
 *
 *  If the metadata for a template ID in the path is not known to the session,
 *  the function sets an error and returns 0.
 *
 *  If `tmplInfo` does not contain information other then template ID, name,
 *  and description, path[0] is FB_TMPL_MD_LEVEL_NA and the return value is 1.
 *
 *  @param session   The Session in which to find the template path
 *  @param tmplInfo  The metadata to start from when finding the path.
 *  @param path      An array to be filled in with the template IDs or NULL
 *  @param path_size The number of values `path` is capable of holding
 *  @param err       An error description, set on failure
 *  @return The number of templates IDs added to `path` or 0 on error
 *  @since libfixbuf 3.0.0
 */
uint16_t
fbSessionGetTemplatePath(
    const fbSession_t       *session,
    const fbTemplateInfo_t  *tmplInfo,
    uint16_t                 path[],
    uint16_t                 path_size,
    GError                 **err);


/**
 *  Convenience macro defining a null static @ref fbConnSpec_t.
 */
#define FB_CONNSPEC_INIT \
    { FB_SCTP, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }

/**
 *  IPFIX Exporting Process endpoint. Used to export messages from an
 *  associated IPFIX Message Buffer to a remote Collecting Process, or to an
 *  IPFIX File.  The internals of this structure are private to libfixbuf.
 */
typedef struct fbExporter_st fbExporter_t;

/**
 *  IPFIX Collecting Process endpoint. Used to collect messages into an
 *  associated IPFIX Message Buffer from a remote Exporting Process, or from
 *  an IPFIX File. Use this with the @ref fbListener_t structure to
 *  implement a full Collecting Process, including Transport Session
 *  setup. The internals of this structure are private to libfixbuf.
 */
typedef struct fbCollector_st fbCollector_t;

/**
 *  IPFIX Collecting Process session listener. Used to wait for connections
 *  from IPFIX Exporting Processes, and to manage open connections via a
 *  select(2)-based mechanism. The internals of this structure are private
 *  to libfixbuf.
 */
typedef struct fbListener_st fbListener_t;

/*
 *  ListenerGroup and associated data type definitions
 */

/**
 *  Structure that represents a group of listeners.
 */
typedef struct fbListenerGroup_st fbListenerGroup_t;

/**
 *  ListenerEntry's make up an @ref fbListenerGroup_t as a linked list
 */
typedef struct fbListenerEntry_st {
    /** pointer to the next listener entry in the linked list */
    struct fbListenerEntry_st  *next;
    /** pointer to the previous listener entry in the linked list */
    struct fbListenerEntry_st  *prev;
    /** pointer to the listener to add to the list */
    fbListener_t               *listener;
} fbListenerEntry_t;

/**
 *  A ListenerGroupResult contains the fbListener whose listening socket got a
 *  new connection (cf. fbListenerGroupWait()).  It is tied to the @ref fBuf_t
 *  that is produced for the connection.  These comprise a linked list.
 */
typedef struct fbListenerGroupResult_st {
    /** Pointer to the next listener group result */
    struct fbListenerGroupResult_st  *next;
    /** pointer to the listener that received a new connection */
    fbListener_t                     *listener;
    /** pointer to the fbuf created for that new connection */
    fBuf_t                           *fbuf;
} fbListenerGroupResult_t;

/**
 *  A callback function that is called when a template is freed.  This
 *  function should be set during the @ref fbNewTemplateCallback_fn.
 *
 *  @param tmpl_ctx a pointer to the ctx that is stored within the fbTemplate.
 *                  This is the context to be cleaned up.
 *  @param app_ctx the app_ctx pointer that was passed to the
 *                 fbSessionAddNewTemplateCallback() call.  This is for
 *                 context only and should not be cleaned up.
 *  @since Changed in libfixbuf 2.0.0.
 */
typedef void (*fbTemplateCtxFree_fn)(
    void  *tmpl_ctx,
    void  *app_ctx);

/**
 *  A callback function that will be called when the session receives
 *  a new external template.  This callback can be used to assign an
 *  internal template to an incoming external template for nested template
 *  records using fbSessionAddTemplatePair() or to apply some context variable
 *  to a template.
 *
 *  The callback should be set using fbSessionAddNewTemplateCallback(), and
 *  that function should be called after fbSessionAlloc().  Libfixbuf often
 *  clones session upon receiving a connection (particularly in the UDP case
 *  since a collector and fbuf can have multiple sessions), and this callback
 *  is carried over to cloned sessions.
 *
 *  @param session  a pointer to the session that received the template
 *  @param tid      the template ID for the template that was received
 *  @param tmpl     pointer to the template information of the received
 *                  template
 *  @param app_ctx  the app_ctx pointer that was passed to the
 *                  fbSessionAddNewTemplateCallback() call
 *  @param tmpl_ctx pointer that is stored in the fbTemplate structure.
 *  @param tmpl_ctx_free_fn a callback function that should be called to
 *                  free the tmpl_ctx when the template is freed/replaced.
 *  @since Changed in libfixbuf 2.0.0.
 */
typedef void (*fbNewTemplateCallback_fn)(
    fbSession_t           *session,
    uint16_t               tid,
    fbTemplate_t          *tmpl,
    void                  *app_ctx,
    void                 **tmpl_ctx,
    fbTemplateCtxFree_fn  *tmpl_ctx_free_fn);


/**
 *  fbListSemantics_t defines the possible values for the semantics of
 *  the structured Data Types: basicLists, subTemplateLists, and
 *  subTemplateMultiLists.  See [RFC 6313][].
 *
 *  [RFC 6313]: https://tools.ietf.org/html/rfc6313
 *
 *  https://www.iana.org/assignments/ipfix/ipfix.xhtml#ipfix-structured-data-types-semantics
 *  @since libfixbuf 3.0.0
 *  @see fbBasicListGetSemantic(), fbSubTemplateListGetSemantic(),
 *  fbSubTemplateMultiListGetSemantic()
 */
typedef enum fbListSemantics_en {
    /**
     * Semantic field for indicating the value has not been set
     */
    FB_LIST_SEM_UNDEFINED = 0xFF,
    /**
     * Semantic field for none-of value defined in RFC 6313
     */
    FB_LIST_SEM_NONE_OF = 0,
    /**
     * Semantic field for exactly-one-of value defined in RFC 6313
     */
    FB_LIST_SEM_EXACTLY_ONE_OF = 1,
    /**
     * Semantic field for the one-or-more-of value defined in RFC 6313
     */
    FB_LIST_SEM_ONE_OR_MORE_OF = 2,
    /**
     * Semantic field for the all-of value defined in RFC 6313
     */
    FB_LIST_SEM_ALL_OF = 3,
    /**
     * Semantic field for the ordered value defined in RFC 6313
     */
    FB_LIST_SEM_ORDERED = 4
} fbListSemantics_t;

/**
 *  Validates the value of a structured data types semantic field, as defined
 *  in [RFC 6313][] and listed at IANA.
 *
 *  @param semantic The value of the semantic field to be checked
 *  @return TRUE if valid (@ref fbListSemantics_t), FALSE otherwise
 *
 *  @since libfixbuf 3.0.0; previously called fbListValidSemantic().
 *
 *  [RFC 6313]: https://tools.ietf.org/html/rfc6313
 */
gboolean
fbListSemanticsIsValid(
    uint8_t   semantic);


/****** BASICLIST FUNCTIONS AND STRUCTS *******/

/**
 *  @ref fbBasicList_t provides the internal representation of an @ref
 *  fbInfoElement_t of type basicList (@ref FB_BASIC_LIST).
 *
 *  @since Changed in libfixbuf 3.0.0: Added the `field` member and removed
 *  the `infoElement` member.
 */
/* typedef struct fbBasicList_st fbBasicList_t; */
struct fbBasicList_st {
    /**
     *  Pointer to the memory that stores the elements in the list.
     */
    uint8_t            *dataPtr;
    /**
     *  The description of one element in the list.
     *  @since libfixbuf 3.0.0
     */
    fbTemplateField_t   field;
    /**
     *  The number of elements in the list.
     */
    uint16_t            numElements;
    /**
     *  The length of the buffer used to store the elements in the list.
     */
    uint16_t            dataLength;
    /**
     *  The semantic value to describe the contents of the list, a value
     *  defined in @ref fbListSemantics_t.
     */
    uint8_t             semantic;
};


/**
 *  Allocates and returns an empty BasicList.  Exits the application on
 *  allocation failure.  Use fbBasicListFree() to free the BasicList.
 *
 *  Given typical usage where list structures are defined as part of a C
 *  `struct`, this function is not often used.
 *
 *  @return A newly allocated BasicList.
 */
fbBasicList_t *
fbBasicListAlloc(
    void);

/**
 *  Initializes a BasicList for export.
 *
 *  This function calls fbBasicListInitWithLength() using the default length
 *  of `infoElement` as the value for the `elementLength` parameter.  See that
 *  function for more information.
 *
 *  @param basicList a pointer to the basic list structure to fill
 *  @param semantic the semantic value to be used in the basic list, a @ref
 *         fbListSemantics_t value
 *  @param infoElement a pointer to the info element to be used in the list
 *  @param numElements number of elements in the list
 *  @return a pointer to the memory where the list data is to be written
 */
void *
fbBasicListInit(
    fbBasicList_t          *basicList,
    uint8_t                 semantic,
    const fbInfoElement_t  *infoElement,
    uint16_t                numElements);


/**
 *  Initializes a BasicList for export using a user-specified element length.
 *  Sets the semantic of the list, sets the type of element stored by the
 *  list, and allocates a data buffer large enough to hold `numElements`
 *  elements, each of size `elementLength`.  Returns a pointer to the newly
 *  allocated and zeroed data buffer, or returns NULL if `infoElement` does
 *  not support elements of length `elementLength`.
 *
 *  The function fbBasicListInit() is a convenience function to initalize a
 *  BasicList using the default length of `infoElement`.
 *
 *  If `basicList` has been used previously, it must be cleared with
 *  fbBasicListClear() prior to invoking this function.  Otherwise the data
 *  array will be overwritten without freeing it, resulting in a memory leak.
 *
 *  @param basicList a pointer to the basic list structure to fill
 *  @param semantic the semantic value to be used in the basic list, a @ref
 *         fbListSemantics_t value
 *  @param infoElement a pointer to the info element to be used in the list
 *  @param elementLength the length for each element in the list
 *  @param numElements number of elements in the list
 *  @return a pointer to the memory where the list data is to be written
 *  @see Use fbBasicListCollectorInit() when using a @ref fbCollector_t.
 */
void *
fbBasicListInitWithLength(
    fbBasicList_t          *basicList,
    uint8_t                 semantic,
    const fbInfoElement_t  *infoElement,
    uint16_t                elementLength,
    uint16_t                numElements);

/**
 *  Initializes a BasicList for collection, setting all properties of the list
 *  to 0 or NULL.  This is equivalent to calling
 *  @code{.c}
 *  memset(basicList, 0, sizeof(*basicList));
 *  @endcode
 *
 *  If `basicList` has been used previously, it must be cleared with
 *  fbBasicListClear() (otherwise a memory leak may occur) and calling this
 *  function is not necessary.
 *
 *  @param basicList pointer to the basic list to be initialized
 */
void
fbBasicListCollectorInit(
    fbBasicList_t  *basicList);

/**
 *  Returns the octet length of an individual element in the BasicList.  That
 *  is, returns the value specified when the list was initialized for export
 *  (fbBasicListInitWithLength()) or read from the @ref fbCollector_t.
 *
 *  @param basicList pointer to the basic list
 *  @return the size of one element
 *  @since libfixbuf 3.0.0
 */
uint16_t
fbBasicListGetElementLength(
    const fbBasicList_t  *basicList);

/**
 *  Returns the number of elements the basic list is capable of holding.  That
 *  is, returns the value specified when the list was initialized for export
 *  (fbBasicListInitWithLength()), expanded (fbBasicListAddNewElements()), or
 *  read from the @ref fbCollector_t.
 *
 *  @param basicList pointer to the basic list
 *  @return the number of elements on the basic list
 *  @since libfixbuf 2.3.0
 */
uint16_t
fbBasicListCountElements(
    const fbBasicList_t  *basicList);

/**
 *  Returns the list semantics value (@ref fbListSemantics_t) for a BasicList.
 *
 *  @param basicList pointer to the basic list to retrieve the semantic from
 *  @return the 8-bit semantic value describing the basic list
 */
uint8_t
fbBasicListGetSemantic(
    const fbBasicList_t  *basicList);

/**
 *  Sets the list semantics value (@ref fbListSemantics_t) for describing a
 *  basic list.
 *
 *  @param basicList pointer to the basic list to set the semantic
 *  @param semantic value to set the semantic field to
 */
void
fbBasicListSetSemantic(
    fbBasicList_t  *basicList,
    uint8_t         semantic);

/**
 *  Returns a pointer to the information element used in the basic list.
 *  It is mainly used in an @ref fbCollector_t to retrieve information.
 *
 *  @param basicList pointer to the basic list to get the infoElement from
 *  @return pointer to the information element from the list
 */
const fbInfoElement_t *
fbBasicListGetInfoElement(
    const fbBasicList_t  *basicList);

/**
 *  Gets the element ID and enterprise number for the @ref fbInfoElement_t
 *  used by a BasicList.  Returns the element ID and sets the referent of
 *  `pen` to the enterprise number when `pen` is not NULL.
 *
 *  @param basicList  BasicList from which to get the information
 *  @param pen        An output parameter used to get the enterprise number
 *  @return The element ID
 *  @since libfixbuf 3.0.0
 */
uint16_t
fbBasicListGetElementIdent(
    const fbBasicList_t  *basicList,
    uint32_t             *pen);

/**
 *  Returns a simulated TemplateField that describes the data within a
 *  basicList.  The TemplateField is not associated with an fbRecord_t, and it
 *  must not be used for fbRecordGetValueForField() or similar functions.
 *
 *  @param basicList  BasicList from which to get the information
 *  @return A TemplateField describing the contents of `basicList`
 *  @since libfixbuf 3.0.0
 */
const fbTemplateField_t *
fbBasicListGetTemplateField(
    const fbBasicList_t  *basicList);

/**
 *  Gets a pointer to the data buffer for the basic list.
 *
 *  @param basicList pointer to the basic list to get the data pointer from
 *  @return the pointer to the data held by the basic list
 */
void *
fbBasicListGetDataPtr(
    const fbBasicList_t  *basicList);

/**
 *  Retrieves the element at position `index` in the basic list or returns
 *  NULL if `index` is out of range.  The first element is at index 0, and the
 *  last at fbBasicListCountElements()-1.
 *
 *  @param basicList pointer to the basic list to retrieve the dataPtr
 *  @param index the index of the element to retrieve (0-based)
 *  @return a pointer to the data in the index'th slot in the list, or NULL
 *  if the index is outside the bounds of the list
 *  @see fbBasicListGetIndexedRecordValue()
 */
void *
fbBasicListGetIndexedDataPtr(
    const fbBasicList_t  *basicList,
    uint16_t              index);

/**
 *  Fills a RecordValue from the data for the `index`th element in
 *  `basicList`.  Returns FALSE if `index` is invalid; the valid range for
 *  `index` is 0 to fbBasicListCountElements()-1 inclusive.
 *
 *  @param basicList  The basicList to get the value from.
 *  @param index      Which item to get the value of; 0 is first.
 *  @param value      An output parameter to fill with the value.
 *  @returns TRUE unless `basicList` is NULL or `index` is out of range.
 *  @since libfixbuf 3.0.0
 *  @see fbBasicListGetIndexedDataPtr()
 */
gboolean
fbBasicListGetIndexedRecordValue(
    const fbBasicList_t  *basicList,
    uint16_t              index,
    fbRecordValue_t      *value);

/**
 *  Retrieves a pointer to the data element in the basicList that follows the
 *  one at `currentPtr`.  Retrieves the first element if `currentPtr` is NULL.
 *  Returns NULL when there are no more elements or when `currentPtr` is
 *  outside the buffer used by the basic list.
 *
 *  @param basicList pointer to the basic list
 *  @param currentPtr pointer to the current element being used.  Set to NULL
 *  to retrieve the first element.
 *  @return a pointer to the next data slot, based on the current pointer.
 *  NULL if the new pointer is passed the end of the buffer
 *  @see fbBLNext()
 */
void *
fbBasicListGetNextPtr(
    const fbBasicList_t  *basicList,
    const void           *currentPtr);

/**
 *  Retrieves the next item from an @ref fbBasicList_t.  Returns a pointer to
 *  the element in `basicList` that follows `current` and casts it to a
 *  pointer to `type`.  If `current` is NULL, gets the first item.  Returns
 *  NULL when there are no more items.
 *
 *  @since libfixbuf 3.0.0
 */
#define fbBLNext(type, basicList, current) \
    ((type *)(fbBasicListGetNextPtr((basicList), (current))))

/**
 *  Resizes and zeroes the list's internal buffer for elements.  Returns a
 *  handle to the new buffer which is capable of holding `newCount` total
 *  elements.  The other properties of the BasicList are unchanged.
 *
 *  May only be called on an initialized BasicList.  This function does not
 *  free any recursive structured-data records used by the existing buffer
 *  before resizing it.
 *
 *  @param basicList the BasicList to resize
 *  @param newCount  total count of elements the resized list can hold
 *  @return pointer to the list's data buffer
 *  @see fbBasicListAddNewElements() to add additional elements to an existing
 *  list.
 *  @since libfixbuf 3.0.0; previously called fbBasicListRealloc().
 */
void *
fbBasicListResize(
    fbBasicList_t  *basicList,
    uint16_t        newCount);

/**
 *  Increases the size of the list's internal data buffer to hold `additional`
 *  more elements.  Returns a pointer to the first additional element.  Any
 *  elements in the buffer prior to this call remain unchanged (though they
 *  may be in a new memory location).  May only be called on an initialized
 *  BasicList.
 *
 *  @param basicList pointer to the basic list to add elements to
 *  @param additional number of elements to add to the list
 *  @return A pointer to the first newly allocated element.
 */
void *
fbBasicListAddNewElements(
    fbBasicList_t  *basicList,
    uint16_t        additional);

/**
 *  Frees the data array of `basicList` and sets all parameters of `basicList`
 *  to 0 or NULL.
 *
 *  This function should only be called on a properly initialized basicList,
 *  and it leaves the BasicList in the same state as
 *  fbBasicListCollectorInit().  To reuse the BasicList for export after this
 *  call, re-initialize it with fbBasicListInit().
 *
 *  @param basicList pointer to the basic list to clear
 *  @see fBufListFree() to recursively free all structured data on a record
 */
void
fbBasicListClear(
    fbBasicList_t  *basicList);

/**
 *  Sets all parameters of `basicList` to 0 or NULL except for the internal
 *  data array and its allocated length.
 *
 *  Use of this function is not recommended.
 *
 *  @param basicList pointer to the basic list to clear without freeing
 */
void
fbBasicListClearWithoutFree(
    fbBasicList_t  *basicList);

/**
 *  Frees the data array of `basicList` (fbBasicListClear()), then frees
 *  `basicList` itself.  This is typically paired with fbBasicListAlloc(), and
 *  it not often used.
 *
 *  @param basicList pointer to the basic list to free
 */
void
fbBasicListFree(
    fbBasicList_t  *basicList);


/******* END OF BASICLIST ********/



/******* SUBTEMPLATELIST FUNCTIONS ****/


/**
 *  @ref fbSubTemplateList_t provides the internal representation of an @ref
 *  fbInfoElement_t of type subTemplateList (@ref FB_SUB_TMPL_LIST).
 */
struct fbSubTemplateList_st {
    /**
     *  The pointer to the template used to structure the data in this list.
     */
    const fbTemplate_t  *tmpl;
    /**
     *  The pointer to the buffer used to hold the data in this list.
     */
    uint8_t             *dataPtr;
    /**
     *  The octet length of the allocated buffer used to hold the data.
     *  @since Changed in libfixbuf 3.0.0; previously this was a union and the
     *  value was in dataLength.length.
     */
    uint32_t             dataLength;
    /**
     *  The octet length of a record (in memory) described by the template.
     *  @since libfixbuf 3.0.0
     */
    uint32_t             recordLength;
    /**
     *  The number of elements (sub-records) in the list.
     */
    uint16_t             numElements;
    /**
     *  The ID of the template used to structure the data.
     */
    uint16_t             tmplID;
    /**
     *  The semantic value to describe the contents of the list, a value
     *  defined in @ref fbListSemantics_t.
     */
    uint8_t              semantic;
};

/**
 *  Allocates and returns an empty subTemplateList structure.  Exits the
 *  application on allocation failure.  Use fbSubTemplateListFree() to free
 *  the subTemplateList.
 *
 *  Given typical usage where list structures are defined as part of a C
 *  `struct`, this function is not often used.
 *
 *  @return A newly allocated SubTemplateList.
 */
fbSubTemplateList_t *
fbSubTemplateListAlloc(
    void);

/**
 *  Initializes `stl` with the given parameters and allocates the internal
 *  data array to a size capable of holding `numElements` records matching
 *  `tmpl`.
 *
 *  Returns a pointer to the allocated and zeroed data array; exits the
 *  application if the allocation fails.  Returns NULL only when `tmpl` is
 *  NULL.
 *
 *  If `stl` has been used previously, any sub-lists within it must be
 *  cleared, then `stl` must be cleared with fbSubTemplateListClear() prior to
 *  invoking this function.  Otherwise the data array will be overwritten
 *  without freeing it, resulting in a memory leak.
 *
 *  This is mainly used when preparing to encode data for output by an @ref
 *  fbExporter_t.  When reading data, use fbSubTemplateListCollectorInit() to
 *  initialize the subTemplateList.  The `tmpl` should exist on the @ref
 *  fbSession_t that will be used when exporting the record holding this
 *  subTemplateList.
 *
 *  @param stl pointer to the sub template list to initialize
 *  @param semantic the semantic value used to describe the list contents, a
 *         @ref fbListSemantics_t value
 *  @param tmplID id of the template used for encoding the list data
 *  @param tmpl pointer to the template struct used for encoding the list data
 *  @param numElements number of elements in the list
 *  @return a pointer to the allocated buffer (location of first element)
 */
void *
fbSubTemplateListInit(
    fbSubTemplateList_t  *stl,
    uint8_t               semantic,
    uint16_t              tmplID,
    const fbTemplate_t   *tmpl,
    uint16_t              numElements);

/**
 *  Initializes a SubTemplateList for collection, setting all properties of
 *  the list to 0 or NULL.  This is equivalent to calling
 *  @code{.c}
 *  memset(subTemplateList, 0, sizeof(*subTemplateList));
 *  @endcode
 *
 *  If `stl` has been used previously, any sub-lists within it must be
 *  cleared, then `stl` must be cleared with fbSubTemplateListClear()
 *  (otherwise a memory leak may occur) and calling this function is not
 *  necessary.
 *
 *  When using an @ref fbExporter_t, use fbSubTemplateListInit() to initialize
 *  the subTemplateList.
 *
 *  @param subTemplateList  The SubTemplateList to initialize for collection
 */
void
fbSubTemplateListCollectorInit(
    fbSubTemplateList_t  *subTemplateList);

/**
 *  Returns the pointer to the sub-record data array for the subTemplateList.
 *
 *  @param subTemplateList pointer to the STL to get the pointer from
 *  @return a pointer to the data buffer used by the sub template list
 */
void *
fbSubTemplateListGetDataPtr(
    const fbSubTemplateList_t  *subTemplateList);

/**
 *  Returns the data for the record at position `index` in the sub template
 *  list, or returns NULL if `index` is out of range.  The first element is at
 *  index 0, the last at fbSubTemplateListCountElements()-1.
 *
 *  @param subTemplateList pointer to the STL
 *  @param index The index of the element to be retrieved (0-based)
 *  @return a pointer to the desired element, or NULL when out of range
 */
void *
fbSubTemplateListGetIndexedDataPtr(
    const fbSubTemplateList_t  *subTemplateList,
    uint16_t                    index);

/**
 *  Retrieves a pointer to the data record in the sub template list that
 *  follows the one at `currentPtr`.  Retrieves the first record if
 *  `currentPtr` is NULL.  Returns NULL when there are no more records or when
 *  `currentPtr` is outside the buffer used by the sub template list.
 *
 *  @param subTemplateList pointer to the STL to get data from
 *  @param currentPtr pointer to the last element accessed.  NULL causes the
 *                    pointer to the first element to be returned
 *  @return the pointer to the next element in the list.  Returns NULL if
 *          currentPtr points to the last element in the list.
 *  @see fbSTLNext()
 */
void *
fbSubTemplateListGetNextPtr(
    const fbSubTemplateList_t  *subTemplateList,
    const void                 *currentPtr);


/**
 *  Retrieves the next record from an @ref fbSubTemplateList_t.  Returns a
 *  pointer to the record in `subTemplateList` that follows `current` and
 *  casts it to a pointer to `type`.  If `current` is NULL, gets the first
 *  record.  Returns NULL when there are no more records.
 *
 *  @since libfixbuf 3.0.0
 */
#define fbSTLNext(type, subTemplateList, current) \
    ((type *)(fbSubTemplateListGetNextPtr((subTemplateList), (current))))

/**
 *  Returns the number of elements (sub-records) the sub template list is
 *  capable of holding.
 *
 *  @param subTemplateList pointer to the sub template list
 *  @return the number of records on the sub template list
 *  @since libfixbuf 2.3.0
 */
uint16_t
fbSubTemplateListCountElements(
    const fbSubTemplateList_t  *subTemplateList);

/**
 *  Sets the list semantics value (@ref fbListSemantics_t) of a
 *  SubTemplateList.
 *
 *  @param subTemplateList pointer to the sub template list
 *  @param semantic Semantic value for the list
 */
void
fbSubTemplateListSetSemantic(
    fbSubTemplateList_t  *subTemplateList,
    uint8_t               semantic);

/**
 *  Gets the list semantics value (@ref fbListSemantics_t) from a
 *  SubTemplateList.
 *
 *  @param subTemplateList pointer to the sub template list
 *  @return the semantic field from the list
 */
uint8_t
fbSubTemplateListGetSemantic(
    const fbSubTemplateList_t  *subTemplateList);

/**
 *  Gets the template pointer from the list structure
 *
 *  @param subTemplateList pointer to the sub template list
 *  @return a pointer to the template used by the sub template list
 */
const fbTemplate_t *
fbSubTemplateListGetTemplate(
    const fbSubTemplateList_t  *subTemplateList);

/**
 *  Gets the template ID for the template used by the list
 *
 *  @param subTemplateList pointer to the sub template list
 *  @return the template ID used by the sub template list
 */
uint16_t
fbSubTemplateListGetTemplateID(
    const fbSubTemplateList_t  *subTemplateList);

/**
 *  Resizes and zeroes the list's internal buffer for elements (sub-records).
 *  Returns a handle to the new buffer which is capable of holding `newCount`
 *  total sub-records.  The other properties of the SubTemplateList are
 *  unchanged.
 *
 *  This function may only be called on an initialized SubTemplateList.  This
 *  function does not free any recursive structured-data records used by the
 *  existing buffer before resizing it.
 *
 *  @param subTemplateList the sub template list to resize
 *  @param newCount        total count of sub-records the resized list can hold
 *  @return pointer to the list's data buffer
 *  @see fbSubTemplateListAddNewElements() to add additional elements to an
 *  existing list.
 *  @since libfixbuf 3.0.0; previously called fbSubTemplateListRealloc().
 */
void *
fbSubTemplateListResize(
    fbSubTemplateList_t  *subTemplateList,
    uint16_t              newCount);

/**
 *  Increases the size of the list's internal buffer to hold `additional` more
 *  elements (sub-records).  Returns a pointer to the first additional
 *  sub-record.  Any sub-records in the buffer prior to this call remain
 *  unchanged (though they may be in a new memory location).  May only be
 *  called on an initialized SubTemplateList.
 *
 *  @param subTemplateList pointer to the sub template list
 *  @param additional number of new elements to add to the list
 *  @return A pointer to the first newly allocated element.
 */
void *
fbSubTemplateListAddNewElements(
    fbSubTemplateList_t  *subTemplateList,
    uint16_t              additional);

/**
 *  Frees the data array of `subTemplateList` and sets all parameters of
 *  `subTemplateList` to 0 or NULL.
 *
 *  This function does not free any structured-data elements used by
 *  sub-records in the the data array before freeing it.  The caller must do
 *  that before invoking this function.
 *
 *  This function should only be called on a properly initalized
 *  subTemplateList, and it leaves the subTemplateList in the same state as
 *  fbSubTemplateListCollectorInit().  To reuse the BasicList for export after
 *  this call, re-initialize it with fbBasicListInit().
 *
 *  This should be used after each call to fBufNext():
 *  If the dataPtr is not NULL in DecodeSubTemplateList, it will not allocate
 *  new memory for the new record, which could cause a buffer overflow if the
 *  new record has a longer list than the current one.
 *  An alternative is to allocate a large buffer and assign it to dataPtr
 *  on your own, then never clear it with this.  Be certain this buffer is
 *  longer than needed for all possible lists
 *
 *  @param subTemplateList pointer to the sub template list to clear
 *  @see fBufListFree() to recursively free all structured data on a record
 */
void
fbSubTemplateListClear(
    fbSubTemplateList_t  *subTemplateList);

/**
 *  Sets all parameters of `subTemplateList` to 0 or NULL except for the
 *  internal data array and its allocated length.
 *
 *  Use of this function is not recommended.
 *
 *  @param subTemplateList pointer to the sub template list to clear
 */
void
fbSubTemplateListClearWithoutFree(
    fbSubTemplateList_t  *subTemplateList);

/**
 *  Frees the data array of `subTemplateList` (fbSubTemplateListClear()), then
 *  frees `subTemplateList` itself.  This is typically paired with
 *  fbSubTemplateListAlloc(), and it not often used.
 *
 *  @param subTemplateList pointer to the sub template list to free
 */
void
fbSubTemplateListFree(
    fbSubTemplateList_t  *subTemplateList);

/********* END OF SUBTEMPLATELIST **********/



/******* SUBTEMPLATEMULTILIST FUNCTIONS ****/

/**
 *  A data type used in a @ref fbSubTemplateMultiList_t to represent
 *  structured data instances that share the same @ref fbTemplate_t.  The
 *  SubTemplateMultiList contains an array of these objects.
 *
 *  When reading from a @ref fbCollector_t, use the methods on the
 *  SubTemplateMultiList to get each SubTemplateMultiListEntry.  For each
 *  entry, you may get its Template (fbSubTemplateMultiListEntryGetTemplate()
 *  and fbSubTemplateMultiListEntryGetTemplateID()), get its number of
 *  elements (fbSubTemplateMultiListEntryCountElements()), get the element at
 *  a specific position (fbSubTemplateMultiListEntryGetIndexedPtr()), and
 *  iterate over the elements (fbSubTemplateMultiListEntryNextDataPtr() or
 *  fbSTMLEntryNext()).
 *
 *  When writing to an @ref fbExporter_t, first initialize the @ref
 *  fbSubTemplateMultiList_t and then get a reference to a
 *  SubTemplateMultiListEntry.  For the entry, initialize it
 *  (fbSubTemplateMultiListEntryInit()) and then fill in the value for each
 *  element in the list.  To add entries to the SubTemplateMultiListEntry
 *  while building it, use fbSubTemplateMultiListEntryAddNewElements().
 */
typedef struct fbSubTemplateMultiListEntry_st fbSubTemplateMultiListEntry_t;


/**
 *  @ref fbSubTemplateMultiList_t provides the internal representation of an
 *  @ref fbInfoElement_t of type subTemplateMultiList (@ref
 *  FB_SUB_TMPL_MULTI_LIST).
 */
/* typedef struct fbSubTemplateMultiList_st fbSubTemplateMultiList_t; */
struct fbSubTemplateMultiList_st {
    /**
     *  The pointer to the first entry in the multi list
     */
    fbSubTemplateMultiListEntry_t  *firstEntry;
    /**
     *  The number of sub template lists in the multi list
     */
    uint16_t                        numElements;
    /**
     *  The semantic value to describe the contents of the list, a value
     *  defined in @ref fbListSemantics_t.
     */
    uint8_t                         semantic;
};


/**
 *  @ref fbSubTemplateMultiListEntry_t represents structured data instances
 *  within a @ref fbSubTemplateMultiList_t that share the same @ref
 *  fbTemplate_t.
 */
struct fbSubTemplateMultiListEntry_st {
    /**
     *  The pointer to the template used to structure the data in this entry.
     */
    const fbTemplate_t  *tmpl;
    /**
     *  The pointer to the buffer used to hold the data in this entry.
     */
    uint8_t             *dataPtr;
    /**
     *  The octet length of the buffer used to hold the data in this entry.
     */
    uint32_t             dataLength;
    /**
     *  The octet length of a record (in memory) described by the template.
     *  @since libfixbuf 3.0.0
     */
    uint32_t             recordLength;
    /**
     *  The number of elements (sub-records) in this entry.
     */
    uint16_t             numElements;
    /**
     *  The ID of the template used to structure the data in this entry.
     */
    uint16_t             tmplID;
};


/**
 *  Allocates and returns an empty subTemplateMultiList structure.  Exits the
 *  application on allocation failure.  Use fbSubTemplateMultiListFree() to
 *  free the returned subTemplateMultiList.
 *
 *  Given typical usage where list structures are defined as part of a C
 *  `struct`, this function is not often used.
 *
 *  @return A newly allocated SubTemplateMultiList.
 */
fbSubTemplateMultiList_t *
fbSubTemplateMultiListAlloc(
    void);


/**
 *  Initializes the multi list with the list semantic and allocates a data
 *  array to store `numElements` entries.  Retuns a pointer to the first entry
 *  of the allocated and zeroed data array.  Exits the application on
 *  allocation failure.
 *
 *  If `STML` has been used previously, any sub-lists with it must be cleared,
 *  then `STML` must be cleared with fbSubTemplateMultiListClear() prior to
 *  invoking this function.  Otherwise the data array will be overwritten
 *  without freeing it, resulting in a memory leak.
 *
 *  @param STML pointer to the sub template multi list to initialize
 *  @param semantic value used to describe the entries in the multi list, a
 *         @ref fbListSemantics_t value
 *  @param numElements number of entries in the multi list
 *  @return pointer to the first uninitialized entry
 */
fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListInit(
    fbSubTemplateMultiList_t  *STML,
    uint8_t                    semantic,
    uint16_t                   numElements);

/**
 *  Returns the number of entries the sub template multi list is capable of
 *  holding.
 *
 *  @param STML pointer to the sub template multi list
 *  @return the number of entries on the sub template multi list
 *  @since libfixbuf 2.3.0
 */
uint16_t
fbSubTemplateMultiListCountElements(
    const fbSubTemplateMultiList_t  *STML);

/**
 *  Sets the list semantics value (@ref fbListSemantics_t) for the multi list.
 *
 *  @param STML pointer to the sub template multi list
 *  @param semantic Value for the semantic field of the sub template multi list
 */
void
fbSubTemplateMultiListSetSemantic(
    fbSubTemplateMultiList_t  *STML,
    uint8_t                    semantic);

/**
 *  Gets the list semantics value (@ref fbListSemantics_t) from the multi
 *  list.
 *
 *  @param STML pointer to the sub template multi list
 *  @return semantic parameter describing the contents of the multi list
 */
uint8_t
fbSubTemplateMultiListGetSemantic(
    const fbSubTemplateMultiList_t  *STML);

/**
 *  Calls fbSubTemplateMultiListEntryClear() for each Entry in `STML` then
 *  frees the `STML`'s data array and sets its parameters to 0 or NULL.  That
 *  is, frees its data array used by each Entry in `STML`, frees the data
 *  array in `STML` that held the Entries, and sets the parameters of `STML`
 *  to 0 or NULL.
 *
 *  This function does not free any recursive structured-data records used by
 *  the Entries' data arrays before freeing them.  The caller must do that
 *  before invoking this function.
 *
 *  This function should only be called on a properly initialized
 *  fbSubTemplateMultiList_t.
 *
 *  fbSubTemplateMultiListClearEntries() is similar to this function except it
 *  does not free `STML`'s data array or reset its parameters.
 *
 *  @param STML pointer to the sub template multi list to clear
 *  @see fBufListFree() to recursively free all structured data on a record
 */
void
fbSubTemplateMultiListClear(
    fbSubTemplateMultiList_t  *STML);

/**
 *  Calls fbSubTemplateMultiListEntryClear() for each Entry in `STML`.  That
 *  is, for each Entry in `STML`, frees its data array and sets its parameters
 *  to 0 or NULL.
 *
 *  This function does not free any recursive structured-data records used by
 *  the Entries' data arrays before freeing them.  The caller must do that
 *  before invoking this function.
 *
 *  This function should only be called on a properly initialized
 *  fbSubTemplateMultiList_t.
 *
 *  To free the data array in `STML` that holds the Entries in addition to
 *  freeing each Entry, use fbSubTemplateMultiListClear().
 *
 *  @param STML pointer to the sub template multi list
 *  @see fBufListFree() to recursively free all structured data on a record
 */
void
fbSubTemplateMultiListClearEntries(
    fbSubTemplateMultiList_t  *STML);

/**
 *  Frees the data arrays of each Entry and of `STML`
 *  (fbSubTemplateMultiListClear()), then frees `STML` itself.  This is
 *  typically paired with fbSubTemplateMultiListAlloc(), and it not often
 *  used.
 *
 *  @param STML pointer to the sub template multi list
 */
void
fbSubTemplateMultiListFree(
    fbSubTemplateMultiList_t  *STML);

/**
 *  Resizes and zeroes the list's internal buffer for entries (@ref
 *  fbSubTemplateMultiListEntry_t).  Returns a handle to the new buffer which
 *  is capable of holding `newCount` total entries.  The other properties of
 *  the SubTemplateMultiList are unchanged.
 *
 *  This function may only be called on an initialized SubTemplateMultiList.
 *  This function does not free any recursive structured-data records used by
 *  the existing buffer before resizing it.
 *
 *  @param STML      the sub template multi list to resize
 *  @param newCount  total number of entries the resized list can hold
 *  @return pointer to the entry's data buffer
 *  @see fbSubTemplateMultiListAddNewEntries() to add additional entries to an
 *  existing STML.
 *  @since libfixbuf 3.0.0; previously called fbSubTemplateMultiListRealloc().
 */
fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListResize(
    fbSubTemplateMultiList_t  *STML,
    uint16_t                   newCount);

/**
 *  Increases the size of the list's internal buffer to hold `additional` more
 *  entries.  Returns a pointer to the first additional entry.  Any entries in
 *  the buffer prior to this call remain unchanged (though they may be in a
 *  new memory location).  May only be used on an initialized
 *  SubTemplateMultiList.
 *
 *  Exits the application on allocation failure.
 *
 *  @param STML pointer to the sub template multi list
 *  @param additional number of entries to add to the list
 *  @return a pointer to the new entry; never returns NULL
 */
fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListAddNewEntries(
    fbSubTemplateMultiList_t  *STML,
    uint16_t                   additional);

/**
 *  Retrieves the first entry in the multi list.
 *
 *  @param STML pointer to the sub template multi list
 *  @return pointer to the first entry used by the list
 */
fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListGetFirstEntry(
    const fbSubTemplateMultiList_t  *STML);

/**
 *  Retrieves a pointer to the entry at a specific index, or returns NULL if
 *  `index` is out of range.  The first entry is at index 0, the last at
 *  fbSubTemplateMultiListCountElements()-1.
 *
 *  @param STML pointer to the sub template multi list
 *  @param index index of the entry to be returned (0-based)
 *  @return the index'th entry used by the list, or NULL if out of range
 */
fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListGetIndexedEntry(
    const fbSubTemplateMultiList_t  *STML,
    uint16_t                         index);

/**
 *  Retrieves a pointer to the entry in the multi list that follows the one at
 *  `currentEntry`.  Retrieves the first entry if `currentEntry` is NULL.
 *  Returns NULL when there are no more entries or when `currentEntry` is
 *  outside the buffer used by the multi list.
 *
 *  @param STML pointer to the sub template multi list to get data from
 *  @param currentEntry pointer to the last element accessed.
 *                      NULL means none have been accessed yet
 *  @return the pointer to the next element in the list.  Returns the NULL
 *          if currentEntry points to the last entry.
 *  @see fbSTMLNext()
 */
fbSubTemplateMultiListEntry_t *
fbSubTemplateMultiListGetNextEntry(
    const fbSubTemplateMultiList_t       *STML,
    const fbSubTemplateMultiListEntry_t  *currentEntry);

/**
 *  Retrieves the next @ref fbSubTemplateMultiListEntry_t from an @ref
 *  fbSubTemplateMultiList_t.  Returns a pointer to the entry in
 *  `subTemplateMultiList` that follows `current`.  If `current` is NULL, gets
 *  the first entry.  Returns NULL when there are no more entries.
 *
 *  @since libfixbuf 3.0.0
 */
#define fbSTMLNext(subTemplateMultiList, current) \
    fbSubTemplateMultiListGetNextEntry((subTemplateMultiList), (current))

/**
 *  Initializes `entry` with the given parameters and allocates the internal
 *  data array to a size capable of holding `numElements` records matching
 *  `tmpl`.
 *
 *  If `entry` has been used previously, any sub-lists with it must be
 *  cleared, then `entry` must be cleared with
 *  fbSubTemplateMultiListEntryClear() prior to invoking this function.
 *  Otherwise the data array will be overwritten without freeing it, resulting
 *  in a memory leak.
 *
 *  This is mainly used when preparing to encode data for output by an @ref
 *  fbExporter_t.  The `tmpl` should exist on the @ref fbSession_t that will
 *  be used when exporting the record holding the subTemplateMultiList.
 *
 *  @param entry pointer to the entry to initialize
 *  @param tmplID ID of the template used to structure the data elements
 *  @param tmpl pointer to the template used to structure the data elements
 *  @param numElements number of data elements in the entry
 *
 *  @return pointer to the data buffer to be filled in
 */
void *
fbSubTemplateMultiListEntryInit(
    fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                        tmplID,
    const fbTemplate_t             *tmpl,
    uint16_t                        numElements);

/**
 *  Resizes and zeroes the entry's internal buffer for elements (sub-records).
 *  Returns a handle to the new buffer which is capable of holding `newCount`
 *  total sub-records.  The other properties of the SubTemplateMultiListEntry
 *  are unchanged.
 *
 *  This function may only be called on an initialized
 *  SubTemplateMultiListEntry.  This function does not free any recursive
 *  structured-data records used by the existing buffer before resizing it.
 *
 *  @param entry     the STML entry to resize
 *  @param newCount  total count of sub-records the resized list can hold
 *  @return pointer to buffer to write data to
 *  @see fbSubTemplateMultiListEntryAddNewElements() to add additional
 *  elements to an existing sub template multi list entry.
 *  @since libfixbuf 3.0.0; previously called
 *  fbSubTemplateMultiListEntryRealloc().
 */
void *
fbSubTemplateMultiListEntryResize(
    fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                        newCount);

/**
 *  Increases the size of the entry's internal buffer to hold `additional`
 *  more elements (sub-records).  Returns a pointer to the first additional
 *  sub-record.  Any sub-records in the buffer prior to this call remain
 *  unchanged (though they may be in a new memory location).  May only be
 *  called on an initialized SubTemplateMultiListEntry.
 *
 *  Exits the application on allocation failure.
 *
 *  @param entry pointer to the STML entry to add additional sub-records to
 *  @param additional number of sub-records to add to the STML entry
 *  @return a pointer to the first newly allocated sub-record; never returns
 *  NULL
 */
void *
fbSubTemplateMultiListEntryAddNewElements(
    fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                        additional);

/**
 *  Frees the data array that holds the sub-records of `entry` and sets all
 *  parameters of `entry` to 0 or NULL.
 *
 *  This function does not free any recursive structured-data records used by
 *  the data array before freeing it.  The caller must do that before invoking
 *  this function.
 *
 *  This function should only be called on a properly initialized
 *  fbSubTemplateMultiListEntry_t.
 *
 *  Use fbSubTemplateMultiListClearEntries() to clear all entries in an @ref
 *  fbSubTemplateMultiList_t, and fbSubTemplateMultiListClear() to clear the
 *  entire fbSubTemplateMultiList_t.
 *
 *  @param entry pointer to the entry to be cleared
 *  @see fBufListFree() to recursively free all structured data on a record
 */
void
fbSubTemplateMultiListEntryClear(
    fbSubTemplateMultiListEntry_t  *entry);

/**
 *  Retrieves the data pointer for this entry.
 *
 *  @param entry pointer to the entry to get the data pointer from
 *  @return pointer to the buffer used to store data for this entry
 */
void *
fbSubTemplateMultiListEntryGetDataPtr(
    const fbSubTemplateMultiListEntry_t  *entry);

/**
 *  Retrieves a pointer to the data record in this entry that follows the one
 *  at `currentPtr`.  Retrieves the first record if `currentPtr` is NULL.
 *  Returns NULL when there are no more records or when `currentPtr` is
 *  outside the buffer used by the multi list entry.
 *
 *  @param entry pointer to the entry to get the next element from
 *  @param currentPtr pointer to the last element accessed.  NULL means return
 *                    a pointer to the first element.
 *  @return the pointer to the next element in the list.  Returns NULL if
 *          currentPtr points to the last element in the list
 *  @see fbSTMLEntryNext()
 */
void *
fbSubTemplateMultiListEntryNextDataPtr(
    const fbSubTemplateMultiListEntry_t  *entry,
    const void                           *currentPtr);


/**
 *  Retrieves the next record from an @ref fbSubTemplateMultiListEntry_t.
 *  Returns a pointer to the record in `subTemplateMultiListEntry` that
 *  follows `current` and casts it to a pointer to `type`.  If `current` is
 *  NULL, gets the first record.  Returns NULL when there are no more records.
 *
 *  @since libfixbuf 3.0.0
 */
#define fbSTMLEntryNext(type, subTemplateMultiListEntry, current) \
    ((type *)(fbSubTemplateMultiListEntryNextDataPtr(             \
                  (subTemplateMultiListEntry), (current))))

/**
 *  Retrieves a pointer to the data element in the entry at position `index`,
 *  or returns NULL when `index` is out of range.  The first element is at
 *  index 0, the last at fbSubTemplateMultiListEntryCountElements()-1.
 *
 *  @param entry pointer to the entry to get a data pointer from.
 *  @param index the number of the element in the list to return
 *  @return the pointer to the index'th element used by the entry, or NULL
 *          when out of range
 */
void *
fbSubTemplateMultiListEntryGetIndexedPtr(
    const fbSubTemplateMultiListEntry_t  *entry,
    uint16_t                              index);

/**
 *  Returns the number of entries the sub template multi list entry is capable
 *  of holding.
 *
 *  @param entry pointer to the sub template multi list entry
 *  @return the number of records on the sub template multi list entry
 *  @since libfixbuf 2.3.0
 */
uint16_t
fbSubTemplateMultiListEntryCountElements(
    const fbSubTemplateMultiListEntry_t  *entry);

/**
 *  Retrieves the template pointer used to structure the data elements.
 *
 *  @param entry pointer to the entry to get the template from
 *  @return the template pointer used to describe the contents of the entry
 */
const fbTemplate_t *
fbSubTemplateMultiListEntryGetTemplate(
    const fbSubTemplateMultiListEntry_t  *entry);

/**
 *  Retrieves the template ID for the template used to structure the data.
 *
 *  @param entry pointer to the entry to get the template ID from
 *  @returns the template ID for template that describes the data
 */
uint16_t
fbSubTemplateMultiListEntryGetTemplateID(
    const fbSubTemplateMultiListEntry_t  *entry);

/************** END OF STML FUNCTIONS *********** */

/**
 *  Frees all of the memory that fixbuf allocated during transcode of this
 *  record.  That is, it frees all of the memory allocated for list
 *  structures, recursively, when fixbuf was encoding or decoding the record.
 *  This function does not free the `record` array itself.
 *
 *  The `tmpl` argument MUST be the internal template that was set using
 *  fBufSetInternalTemplate() on the @ref fBuf_t before fBufNext() or
 *  fBufAppend() was called with the data.
 *
 *  This is equivalent to doing a depth-first traversal of the record and
 *  calling fbSubTemplateMultiListClear(), fbSubTemplateListClear(), or
 *  fbBasicListClear() on each structured data element.
 *
 *  @param tmpl pointer to the internal template that MUST match the record
 *  @param record pointer to the data
 *  @see fbRecordFreeLists() to recursively free all structured data when the
 *  @ref fbRecord_t type is being used.
 *  @since libfixbuf 1.7.0
 */
void
fBufListFree(
    const fbTemplate_t  *tmpl,
    uint8_t             *record);


/**
 *  Allocates and returns an empty ListenerGroup.  Use
 *  fbListenerGroupAddListener() to populate the ListenerGroup,
 *  fbListenerGroupWait() to wait for connections on those listeners, and
 *  fbListenerGroupFree() when the ListenerGroup is no longer needed.
 *
 *  Exits the application on allocation failure.
 *
 *  @return A newly allocated ListenerGroup.
 */
fbListenerGroup_t *
fbListenerGroupAlloc(
    void);

/**
 *  Frees a listener group
 *
 *  @param group fbListenerGroup
 */
void
fbListenerGroupFree(
    fbListenerGroup_t  *group);

/**
 *  Adds a previously allocated listener to the previously allocated group.
 *  The listener is placed at the head of the list
 *
 *  @param group pointer to the allocated group to add the listener to
 *  @param listener pointer to the listener structure to add to the group
 *  @return 0 upon success. "1" if entry couldn't be allocated
 *         "2" if either of the incoming pointers are NULL
 */
int
fbListenerGroupAddListener(
    fbListenerGroup_t   *group,
    const fbListener_t  *listener);

/**
 *  Removes the listener from the group.
 *  IT DOES NOT FREE THE LISTENER OR THE GROUP
 *
 *  @param group pointer to the group to remove from the listener from
 *  @param listener pointer to the listener to remove from the group
 *  @return 0 on success, and "1" if the listener is not found
 *         "2" if either of the pointers are NULL
 */
int
fbListenerGroupDeleteListener(
    fbListenerGroup_t   *group,
    const fbListener_t  *listener);

/**
 *  Accepts connections for multiple listeners.  Works similarly to
 *  fbListenerWait(), except that is looks for connections for any listener
 *  that is part of a previously allocated and filled listener group.  It
 *  returns a pointer to the head of a list of listenerGroupResults.  The
 *  caller is responsible for freeing the listenerGroupResult
 *  (fbListenerFreeGroupResult()).
 *
 *  @param group pointer to the group of listeners to wait on
 *  @param err error string structure seen throughout fixbuf
 *  @return pointer to the head of the listener group result list
 *          NULL on error, and sets the error string
 */
fbListenerGroupResult_t *
fbListenerGroupWait(
    fbListenerGroup_t  *group,
    GError            **err);

/**
 *  Frees the listener group result returned from fbListenerGroupWait().
 *
 *  @param result    A listener group result
 */
void
fbListenerFreeGroupResult(
    fbListenerGroupResult_t  *result);

/**
 *  Returns an fBuf wrapped around an independently managed socket and a
 *  properly created listener for TCP connections.
 *  The caller is only responsible for creating the socket.
 *  The existing collector code will close the socket and cleanup everything.
 *
 *  @param listener pointer to the listener to wrap around the socket
 *  @param sock the socket descriptor of the independently managed socket
 *  @param err standard fixbuf err structure pointer
 *  @return pointer to the fbuf for the collector.
 *          NULL if sock is 0, 1, or 2 (stdin, stdout, or stderr)
 */
fBuf_t *
fbListenerOwnSocketCollectorTCP(
    fbListener_t  *listener,
    int            sock,
    GError       **err);

/**
 *  Same as fbListenerOwnSocketCollectorTCP() but for TLS (not tested)
 *
 *  @param listener pointer to the listener to wait on
 *  @param sock independently managed socket descriptor
 *  @param err standard fixbuf err structure pointer
 *  @return pointer to the fbuf for the collector
 *          NULL if sock is 0, 1, or 2 (stdin, stdout, or stderr)
 */
fBuf_t *
fbListenerOwnSocketCollectorTLS(
    fbListener_t  *listener,
    int            sock,
    GError       **err);

/**
 *  Interrupts the select call of a specific collector by way of its fBuf.
 *  This is mainly used by fbListenerInterrupt() to interrupt all of the
 *  collector sockets well.
 */
void
fBufInterruptSocket(
    fBuf_t  *fbuf);


/**
 *  Application context initialization function type for fbListener_t.  Pass
 *  this function as the `appinit` parameter when creating the fbListener_t
 *  with fbListenerAlloc().
 *
 *  Fixbuf calls this function after `accept(2)` for TCP or SCTP with the peer
 *  address in the `peer` argument. For UDP, it is called during fbListener_t
 *  initialization and the peer address is NULL.  If the Collector is in UDP
 *  multi-session mode, the function is called again when a new UDP connection
 *  occurs with the peer address, similiar to the TCP case.  Use
 *  fbCollectorSetUDPMultiSession() to turn on multi-session mode (off by
 *  default).
 *
 *  The application may veto @ref fbCollector_t creation by having this
 *  function return FALSE. In multi-session mode, if the connection is to be
 *  ignored, the application should set error code @ref FB_ERROR_NLREAD on the
 *  `err` and return FALSE.  If the function returns FALSE, fixbuf will
 *  maintain information about that peer, and will reject connections from
 *  that peer until shutdown or until that session times out.  Fixbuf will
 *  return FB_ERROR_NLREAD for previously rejected sessions.
 *
 *  The context (returned via out-parameter `ctx`) is stored in the
 *  fbCollector_t, and it is retrievable via fbCollectorGetContext().  If not
 *  in multi-session mode and using the appinit fn, the ctx will be associated
 *  with all UDP sessions.
 *
 *  @see fbListenerAppFree_fn
 */
typedef gboolean (*fbListenerAppInit_fn)(
    fbListener_t     *listener,
    void            **ctx,
    int               fd,
    struct sockaddr  *peer,
    size_t            peerlen,
    GError          **err);

/**
 *  Application context free function type for @ref fbListener_t.  Pass this
 *  function as the `appfree` parameter when creating the fbListener_t with
 *  fbListenerAlloc().
 *
 *  For TCP and SCTP collectors, this is called when the connection is closed.
 *  If a UDP Collector is in multi-session mode (see @ref
 *  fbListenerAppInit_fn), this function is called if a session is timed out
 *  (does not receive a UDP message for more than 30 minutes.)  Called during
 *  @ref fbCollector_t cleanup.
 */
typedef void (*fbListenerAppFree_fn)(
    void  *ctx);

/**
 *  Sets the internal template on a buffer to the given template ID. The
 *  internal template describes the format of the record pointed to by the
 *  recbase parameter to fBufAppend() (for export) and fBufNext() (for
 *  collection). The given template ID must identify a current internal
 *  template in the buffer's associated session.
 *
 *  An internal template must be set on a buffer before calling fBufAppend()
 *  or fBufNext().
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param int_tid   template ID of the new internal template
 *  @param err       An error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 */
gboolean
fBufSetInternalTemplate(
    fBuf_t    *fbuf,
    uint16_t   int_tid,
    GError   **err);

/**
 *  Sets the external template for export on a buffer to the given template
 *  ID.  The external template describes the record that will be written to
 *  the IPFIX message. The buffer must be initialized for export. The given ID
 *  is scoped to the observation domain of the associated session (see
 *  fbSessionSetDomain()), and must identify a current external template for
 *  the current domain in the buffer's associated session.
 *
 *  An export template must be set on a buffer before calling fBufAppend().
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param ext_tid   template ID of the new external template within the
 *                   current domain.
 *  @param err       An error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 */
gboolean
fBufSetExportTemplate(
    fBuf_t    *fbuf,
    uint16_t   ext_tid,
    GError   **err);

/**
 *  Sets the internal and external templates on a Buffer to the given template
 *  ID.  This is a convenience function that invokes fBufSetExportTemplate()
 *  and fBufSetInternalTemplate().
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param tid       template ID used to find the internal and external
 *                   templates for the buffer
 *  @param err       An error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 *  @since libfixbuf 3.0.0
 */
gboolean
fBufSetTemplatesForExport(
    fBuf_t    *fbuf,
    uint16_t   tid,
    GError   **err);

/**
 *  Sets the automatic next-message mode flag on a Buffer.
 *
 *  Buffers are created in automatic next-message mode by default.
 *
 *  For a Buffer allocated for export (fBufAllocForExport()) that is in
 *  automatic next-message mode, a call to fBufAppend(),
 *  fbSessionAddTemplate(), or fbSessionExportTemplates() that overruns the
 *  available space in the Buffer causes the current message to be emitted
 *  (fBufEmit()) to the @ref fbExporter_t and a new message started.
 *
 *  For a Buffer allocated for reading (fBufAllocForCollection() or
 *  fbListenerWait()) that is in automatic next-message mode, a call to
 *  fBufNext() that reaches the end of Buffer causes the next message to be
 *  read (fBufNextMessage()) from the @ref fbCollector_t.
 *
 *  In manual next-message mode, the end of message on any buffer export/read
 *  call causes the call to return an error with a GError code of @ref
 *  FB_ERROR_EOM.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param automatic TRUE for this buffer to be automatic, FALSE for manual
 *  @since libfixbuf 3.0.0; previously called fBufSetAutomaticMode().
 */
void
fBufSetAutomaticNextMessage(
    fBuf_t    *fbuf,
    gboolean   automatic);


/**
 *  Deprecated.  Use fBufSetAutomaticNextMessage() instead.
 */
void
fBufSetAutomaticMode(
    fBuf_t    *fbuf,
    gboolean   automatic);


/**
 *  Enables automatic insertion of [RFC 5610][] elements read from a Buffer.
 *
 *  RFC 5610 records allow an application to retrieve information about a
 *  non-standard information elements.  By default, automatic insertion mode
 *  is disabled.
 *
 *  In automatic insertion mode, when the buffer reads an information element
 *  type record that has a non-zero value for the privateEnterpriseNumber and
 *  that is not already present in the buffer's session's information model, a
 *  new information element (@ref fbInfoElement_t) is created and added to the
 *  information model (@ref fbInfoModel_t).  This function creates the
 *  internal template for the Info Element Type Record (@ref
 *  fbInfoElementOptRec_t) and calls fbInfoElementAddOptRecElement() to add it
 *  to the buffer's session.
 *
 *  For export of RFC 5610 elements, use fbSessionSetMetadataExportElements().
 *
 *  @param fbuf        an IPFIX message buffer
 *  @param err         Gerror pointer
 *  @return            TRUE on success, FALSE if the internal template could
 *                    not be created
 *  @since libfixbuf 3.0.0; previously called fBufSetAutomaticInsert().
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 */
gboolean
fBufSetAutomaticElementInsert(
    fBuf_t  *fbuf,
    GError **err);

/**
 *  A deprecated alias for fBufSetAutomaticElementInsert().
 *
 *  @param fbuf        an IPFIX message buffer
 *  @param err         Gerror pointer
 *  @return            TRUE on success, FALSE if the internal template could
 *                    not be created
 *  @deprecated Use fBufSetAutomaticElementInsert() instead.
 */
gboolean
fBufSetAutomaticInsert(
    fBuf_t  *fbuf,
    GError **err);

/**
 *  Instructes a collection Buffer to process Template Metadata records.
 *
 *  Template Metadata records allow the exporter to provide a name and a
 *  description to a template.  As of libfixbuf 3.0.0, the Template Metadata
 *  also includes relationships among templates (for example, some are used in
 *  a @ref fbSubTemplateList_t or a @ref fbSubTemplateMultiList_t) and the
 *  @ref fbInfoElement_t used by each @ref fbBasicList_t in the Template.
 *
 *  Once the @ref fbCollector_t sees the template metadata records, they may
 *  be queried using fbSessionGetTemplateInfo(), fbSessionGetTemplatePath(),
 *  and fbTemplateInfoGetNextBasicList().  See @ref fbTemplateInfo_t and @ref
 *  fbBasicListInfo_t for more information.
 *
 *  This function enables recognition of these records and defines two
 *  internal templates for reading the data.
 *
 *  To export Template Metadata records, use
 *  fbSessionSetMetadataExportTemplates() and provide the metadata with the
 *  @ref fbTemplate_t to fbSessionAddTemplate().
 *
 *  @param fbuf   An IPFIX message buffer.
 *  @param err    An error description.
 *  @return       TRUE on success or FALSE if the
 *  @since  libfixbuf 3.0.0
 */
gboolean
fBufSetAutomaticMetadataAttach(
    fBuf_t  *fbuf,
    GError **err);


/**
 *  Retrieves the session associated with a buffer.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @return the associated session
 */
fbSession_t *
fBufGetSession(
    const fBuf_t  *fbuf);

/**
 *  Frees a buffer. Also frees any associated session, exporter, or collector,
 *  closing exporting process or collecting process endpoint connections and
 *  removing collecting process endpoints from any listeners, as necessary.
 *
 *  @param fbuf      an IPFIX message buffer
 */
void
fBufFree(
    fBuf_t  *fbuf);

/**
 *  Allocates a new buffer for export. Associates the buffer with a given
 *  session and exporting process endpoint; these become owned by the buffer.
 *  Session and Exporter are freed by fBufFree().  Must never be freed by the
 *  user.  Use fBufAppend() to add data to the buffer, and use fBufEmit() to
 *  flush the buffer.
 *
 *  Exits the application on allocation failure.
 *
 *  @param session   a session initialized with appropriate
 *                   internal and external templates
 *  @param exporter  an exporting process endpoint
 *  @return A new IPFIX message buffer, owning the Session and Exporter.
 */
fBuf_t *
fBufAllocForExport(
    fbSession_t   *session,
    fbExporter_t  *exporter);

/**
 *  Retrieves the exporting process endpoint associated with a buffer.
 *  The buffer must have been allocated with fBufAllocForExport();
 *  otherwise, returns NULL.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @return the associated exporting process endpoint
 */
fbExporter_t *
fBufGetExporter(
    const fBuf_t  *fbuf);

/**
 *  Associates an exporting process endpoint with a buffer.
 *  The exporter will be used to write IPFIX messgaes to a transport.
 *  The exporter becomes owned by the buffer; any previous exporter
 *  associated with the buffer is closed if necessary and freed.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param exporter  an exporting process endpoint
 */
void
fBufSetExporter(
    fBuf_t        *fbuf,
    fbExporter_t  *exporter);


/**
 *  When using fBufSetBuffer(), returns the number of unprocessed octets in
 *  the octet array.  An IPFIX collector that is not using libfixbuf to handle
 *  connections would use this function upon receiving an @ref FB_ERROR_BUFSZ
 *  error from fBufNext(), fBufNextCollectionTemplate(), or fBufNextRecord().
 *  The remaining data is the beginning of the next IPFIX message.
 *
 *  @param fbuf an IPFIX message buffer
 *  @return Number of octets in the octet array that were not processed.
 *
 */
size_t
fBufRemaining(
    fBuf_t  *fbuf);


/**
 *  Sets an octet array on an fBuf for collection.  This may be used by
 *  applications that want to handle their own connections, file reading, etc.
 *  This call should be made after the call to read() and before calling
 *  fBufNext(), fBufNextCollectionTemplate(), or fBufNextRecord().  These
 *  functions return @ref FB_ERROR_BUFSZ when there is not enough data in the
 *  octet array to read a complete IPFIX message.  Upon receipt of
 *  FB_ERROR_BUFSZ, the application should call fBufRemaining() to get the
 *  number of octets in the array that were not processed.
 *
 *  @param fbuf an IPFIX message buffer
 *  @param buf the octet array containing IPFIX data to be processed
 *  @param buflen the length of IPFIX data in `buf`
 *  @see @ref noconnect Connection-less collector
 */
void
fBufSetBuffer(
    fBuf_t   *fbuf,
    uint8_t  *buf,
    size_t    buflen);


/**
 *  Appends a record to a buffer. Uses the present internal template set via
 *  fBufSetInternalTemplate() to describe the record of size recsize located
 *  in memory at recbase.  Uses the present export template set via
 *  fBufSetExportTemplate() to describe the record structure to be written to
 *  the buffer. Information Elements present in the external template that are
 *  not present in the internal template are transcoded into the message as
 *  zeroes. If the buffer is in automatic mode, may cause a message to be
 *  emitted via fBufEmit() if there is insufficient space in the buffer for
 *  the record.
 *
 *  If the internal template contains any variable length Information
 *  Elements, those must be represented in the record by @ref fbVarfield_t
 *  structures.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param recbase   pointer to internal record
 *  @param recsize   size of internal record in bytes
 *  @param err       an error description, set on failure.
 *                   Must not be NULL, as it is used internally in
 *                   automatic mode to detect message restart.
 *  @return TRUE on success, FALSE on failure.
 */
gboolean
fBufAppend(
    fBuf_t   *fbuf,
    uint8_t  *recbase,
    size_t    recsize,
    GError  **err);

/**
 *  Emits the message currently in a buffer using the associated exporting
 *  process endpoint.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param err       an error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 */
gboolean
fBufEmit(
    fBuf_t  *fbuf,
    GError **err);

/**
 *  Sets the export time on the message currently in a buffer. This will be
 *  used as the export time of the message created by the first call to
 *  fBufAppend() after the current message, if any, is emitted. Use 0 for the
 *  export time to cause the export time to be taken from the system clock at
 *  message creation time.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param extime    the export time in epoch seconds.
 */
void
fBufSetExportTime(
    fBuf_t    *fbuf,
    uint32_t   extime);

/**
 *  Allocates a new buffer for collection. Associates the buffer with a given
 *  session and collecting process endpoint; these become owned by the buffer
 *  and must not be freed by the user.  The session and collector are freed by
 *  fBufFree().  Use fBufNext() to read data from the buffer, and
 *  fBufNextMessage() to move the buffer to the next IPFIX message.
 *
 *  To process an octet array of IPFIX data (see fBufSetBuffer()), invoke this
 *  function with a NULL collector.
 *
 *  Exits the application on allocation failure.
 *
 *  @param session   a session initialized with appropriate
 *                   internal templates
 *  @param collector  an collecting process endpoint
 *  @return A new IPFIX message buffer, owning the Session and Collector.
 */
fBuf_t *
fBufAllocForCollection(
    fbSession_t    *session,
    fbCollector_t  *collector);

/**
 *  Retrieves the collecting process endpoint associated with a buffer.
 *  The buffer must have been allocated with fBufAllocForCollection();
 *  otherwise, returns NULL.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @return the associated collecting process endpoint
 */
fbCollector_t *
fBufGetCollector(
    const fBuf_t  *fbuf);

/**
 *  Associates an collecting process endpoint with a buffer.
 *  The collector will be used to read IPFIX messgaes from a transport.
 *  The collector becomes owned by the buffer; any previous collector
 *  associated with the buffer is closed if necessary and freed.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param collector  an collecting process endpoint
 */
void
fBufSetCollector(
    fBuf_t         *fbuf,
    fbCollector_t  *collector);

/**
 *  Retrieves a record from a Buffer associated with a collecting process.
 *
 *  Uses the external template taken from the message to read the next record
 *  available from a data set in the message. Copies the record to the data
 *  buffer at `recbase`, with a maximum record size pointed to by `recsize`,
 *  described by the present internal template previous set via
 *  fBufSetInternalTemplate(). Updates `recsize` to hold the number of bytes
 *  written into the buffer.
 *
 *  Information Elements present in the internal template that are not present
 *  in the external template are transcoded into the record at `recbase` as
 *  zeros. Elements in the external template that are not present in the
 *  internal template are ignored.
 *
 *  The external @ref fbTemplate_t and ID that were used to read this record
 *  are available via fBufGetCollectionTemplate().
 *
 *  Reads and processes any templates and options templates between the last
 *  record read (or the beginning of the message) and the next data record.
 *  If the buffer is in automatic mode, may cause a message to be read via
 *  fBufNextMessage() if there are no more records available in the message
 *  buffer.
 *
 *  If the internal template contains Information Elements of type @ref
 *  FB_BASIC_LIST, @ref FB_SUB_TMPL_LIST, and @ref FB_SUB_TMPL_MULTI_LIST,
 *  they must be represented in the record at `recbase` by @ref fbBasicList_t,
 *  @ref fbSubTemplateList_t, and @ref fbSubTemplateMultiList_t structures,
 *  respectively. If it contains any other variable length Information
 *  Elements, those must be represented in the record at `recbase` by @ref
 *  fbVarfield_t structures.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param recbase   pointer to internal record buffer; will contain
 *                   record data after call.
 *  @param recsize   On call, pointer to size of internal record buffer
 *                   in bytes. Contains number of bytes actually transcoded
 *                   at end of call.
 *  @param err       an error description, set on failure.
 *                   Must not be NULL, as it is used internally in
 *                   automatic mode to detect message restart.
 *  @return TRUE on success, FALSE on failure.
 *  @see fBufNextRecord() for an alternate interface;
 *  fbSessionAddTemplatePair() for a description of how subtemplates in a
 *  subTemplateList or subTemplateMultiList are trancoded.
 */
gboolean
fBufNext(
    fBuf_t   *fbuf,
    uint8_t  *recbase,
    size_t   *recsize,
    GError  **err);

/**
 *  Sets the internal template on a Buffer and gets its next record.
 *
 *  Sets the internal template of `fbuf` to the value specified by the `tid`
 *  member of `record` (fBufSetInternalTemplate()) and fills the `rec` member
 *  (whose size is specified by `reccapacity`) with the next record
 *  from the Buffer (fBufNext()).  Returns TRUE on success, or sets `err` and
 *  returns FALSE otherwise.
 *
 *  Before calling this function, the caller must set the `tid` member to the
 *  template to use and ensure that the `rec` and `reccapacity` members have
 *  been initialized.
 *
 *  On a successful call, the `tmpl` member points to the internal template,
 *  the `rec` member holds the record's data, and `recsize` is the length of
 *  that data.
 *
 *  @param fbuf     an IPFIX message buffer
 *  @param record   an object to be updated with the record's data
 *  @param err      an error description, set on failure.
 *  @return TRUE on success, FALSE on failure
 *
 *  @since libfixbuf 3.0.0
 */
gboolean
fBufNextRecord(
    fBuf_t      *fbuf,
    fbRecord_t  *record,
    GError     **err);

/**
 *  Reads a new message into a buffer using the associated collecting
 *  process endpoint. Called by fBufNext() on end of message in automatic
 *  mode; should be called after an FB_ERROR_EOM return from fBufNext() in
 *  manual mode, or to skip the current message and go on to the next
 *  in the stream.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param err       an error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 */
gboolean
fBufNextMessage(
    fBuf_t  *fbuf,
    GError **err);

/**
 *  Retrieves the export time on the message currently in a buffer.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @return the export time in epoch seconds.
 */
uint32_t
fBufGetExportTime(
    const fBuf_t  *fbuf);

/**
 *  Retrieves the external template used to read the last record from the
 *  buffer.  If no record has been read, returns NULL. Stores the external
 *  template ID within the current domain in ext_tid, if not NULL.
 *
 *  This routine is not particularly useful to applications, as it would be
 *  called after the record described by the external template had been
 *  transcoded, and as such could not be used to select an
 *  appropriate internal template for a given external template. However,
 *  it is used by fBufNextCollectionTemplate(), and may be useful in certain
 *  contexts, so is made public.
 *
 *  Usually, you'll want to use fBufNextCollectionTemplate() instead.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param ext_tid   pointer to external template ID storage, or NULL.
 *  @return the external template describing the last record read.
 */
fbTemplate_t *
fBufGetCollectionTemplate(
    const fBuf_t  *fbuf,
    uint16_t      *ext_tid);

/**
 *  Retrieves the external template that will be used to read the next record
 *  from the buffer. If no next record is available, returns NULL. Stores the
 *  external template ID within the current domain in ext_tid, if not NULL.
 *  Reads and processes any templates and options
 *  templates between the last record read (or beginning of message) and the
 *  next data record. If the buffer is in automatic mode, may cause
 *  a message to be read via fBufNextMessage() if there are no more records
 *  available in the message buffer.
 *
 *  @param fbuf      an IPFIX message buffer
 *  @param ext_tid   pointer to external template ID storage, or NULL.
 *  @param err       an error description, set on failure.
 *                   Must not be NULL, as it is used internally in
 *                   automatic mode to detect message restart.
 *  @return the external template describing the last record read.
 */
fbTemplate_t *
fBufNextCollectionTemplate(
    fBuf_t    *fbuf,
    uint16_t  *ext_tid,
    GError   **err);

/**
 *  Allocates a new information model. The information model contains all the
 *  default information elements in the [IANA-managed][] number space, and may
 *  be extended via fbInfoModelAddElement(), fbInfoModelAddElementArray(),
 *  fbInfoModelReadXMLFile(), and fbInfoModelReadXMLData().
 *
 *  An Information Model is required to create Templates and Sessions. Each
 *  application should have only one Information Model.
 *
 *  Exits the application on allocation failure.
 *
 *  [IANA-managed]: https://www.iana.org/assignments/ipfix/ipfix.xhtml
 *
 *  @return A new Information Model.
 */
fbInfoModel_t *
fbInfoModelAlloc(
    void);

/**
 *  Frees an information model. Must not be called until all sessions and
 *  templates depending on the information model have also been freed; i.e.,
 *  at application cleanup time.
 *
 *  @param model     An information model
 */
void
fbInfoModelFree(
    fbInfoModel_t  *model);

/**
 *  Adds a single information element to an information
 *  model. The information element is assumed to be in "canonical" form; that
 *  is, its ref.name field should contain the information element name. The
 *  information element and its name are copied into the model; the caller may
 *  free or reuse its storage after this call.
 *
 *  See fbInfoModelAddElementArray() for a more convenient method of
 *  statically adding information elements to information models.
 *
 *  @param model     An information model
 *  @param ie        Pointer to an information element to copy into the model
 */
void
fbInfoModelAddElement(
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ie);

/**
 *  Adds multiple information elements in an array to an information
 *  model. The information elements are assumed to be in "canonical" form;
 *  that is, their ref.name fields should contain the information element
 *  name. Each information element and its name are copied into the model; the
 *  caller may free or reuse its storage after this call.
 *
 *  The ie parameter points to the first information element in an array,
 *  usually statically initialized with an array of @ref FB_IE_INIT_FULL
 *  macros followed by an @ref FB_IE_NULL macro.
 *
 *  @param model     An information model
 *  @param ie        Pointer to an IE array to copy into the model
 *
 *  @see fbInfoModelReadXMLFile() and fbInfoModelReadXMLData() for
 *  alternate ways to add information elements to information models.
 */
void
fbInfoModelAddElementArray(
    fbInfoModel_t          *model,
    const fbInfoElement_t  *ie);

/**
 *  Adds information specified in the given XML file to the information
 *  model. The XML file is expected to be in the format used by the
 *  [IANA IPFIX Entities registry][IPFIX XML], with the following two
 *  additions:
 *
 *  - A `<cert:enterpriseId>` field may be used to mark the enterprise ID for
 *   an element.
 *
 *  - A `<cert:reversible>` field may be used to mark an element as
 *   reversible (as per [RFC 5103][]).  Valid values for this field are
 *   true, false, yes, no, 1, and 0.
 *
 *  If the XML being parsed contains registries for the element data
 *  types, semantics, or units, those will be parsed and used to
 *  interpret the corresponding fields in the element records.  (These
 *  registries exist in IANA's registry.)
 *
 *  A parsed element that already exists in the given InfoModel will be
 *  replace the existing element.
 *
 *  @param model     An information model
 *  @param filename  The file containing the XML description
 *  @param err       Return location for a GError
 *  @return `FALSE` if an error occurred, TRUE on success
 *  @since libfixbuf 2.1.0
 *  @see fbInfoModelReadXMLData()
 *
 *  [IPFIX XML]: https://www.iana.org/assignments/ipfix/ipfix.xml
 *  [RFC 5103]: https://tools.ietf.org/html/rfc5103
 */
gboolean
fbInfoModelReadXMLFile(
    fbInfoModel_t  *model,
    const gchar    *filename,
    GError        **err);

/**
 *  Adds information specified in the given XML data to the information
 *  model. The XML data is expected to be in the format used by the
 *  [IANA IPFIX Entities registry][IPFIX XML], with the following two
 *  additions:
 *
 *  - A `<cert:enterpriseId>` field may be used to mark the enterprise ID for
 *   an element.
 *
 *  - A `<cert:reversible>` field may be used to mark an element as
 *   reversible (as per [RFC 5103][]).  Valid values for this field are
 *   true, false, yes, no, 1, and 0.
 *
 *  If the XML being parsed contains registries for the element data
 *  types, semantics, or units, those will be parsed and used to
 *  interpret the corresponding fields in the element records.  (These
 *  registries exist in IANA's registry.)
 *
 *  A parsed element that already exists in the given InfoModel will be
 *  replace the existing element.
 *
 *  @param model         An information model
 *  @param xml_data      A pointer to the XML data
 *  @param xml_data_len  The length of `xml_data` in bytes
 *  @param err           Return location for a GError
 *  @return `FALSE` if an error occurred, TRUE on success
 *  @since libfixbuf 2.1.0
 *  @see fbInfoModelReadXMLFile()
 *
 *  [IPFIX XML]: https://www.iana.org/assignments/ipfix/ipfix.xml
 *  [RFC 5103]: https://tools.ietf.org/html/rfc5103
 */
gboolean
fbInfoModelReadXMLData(
    fbInfoModel_t  *model,
    const gchar    *xml_data,
    gssize          xml_data_len,
    GError        **err);

/**
 *  Returns a pointer to the canonical information element within an
 *  information model given the information element name. The returned
 *  information element is owned by the information model and must not be
 *  modified.
 *
 *  @param model     An information model
 *  @param name      The name of the information element to look up
 *  @return          The named information element within the model,
 *                  or NULL if no such element exists.
 */
const fbInfoElement_t *
fbInfoModelGetElementByName(
    const fbInfoModel_t  *model,
    const char           *name);

/**
 *  Returns a pointer to the canonical information element within an
 *  information model given the information element ID and enterprise ID.  The
 *  returned information element is owned by the information model and must
 *  not be modified.
 *
 *  @param model     An information model
 *  @param id        An information element id
 *  @param ent       An enterprise id
 *  @return          The named information element within the model, or NULL
 *                  if no such element exists.
 */
const fbInfoElement_t *
fbInfoModelGetElementByID(
    const fbInfoModel_t  *model,
    uint16_t              id,
    uint32_t              ent);

/**
 *  Returns TRUE if `element` is present in the information model either by
 *  name or by element and enterprise id.
 *
 *  @param model     An information model
 *  @param element   An information element
 *  @return          TRUE if `element is in `model`.
 *  @since libfixbuf 3.0.0
 */
gboolean
fbInfoModelContainsElement(
    const fbInfoModel_t    *model,
    const fbInfoElement_t  *element);


/**
 *  Returns the number of information elements in the information model.
 *
 *  @param model     An information model
 *  @return          The number of information elements in the
 *                  information model
 */
guint
fbInfoModelCountElements(
    const fbInfoModel_t  *model);

/**
 *  Initializes an information model iterator for iteration over the
 *  information elements (@ref fbInfoElement_t) in the model.  The caller uses
 *  fbInfoModelIterNext() to visit the elements.
 *
 *  @param iter      A pointer to the iterator to initialize
 *  @param model     An information model
 */
void
fbInfoModelIterInit(
    fbInfoModelIter_t    *iter,
    const fbInfoModel_t  *model);

/**
 *  Returns a pointer to the next information element in the information
 *  model.  Returns NULL once all information elements have been
 *  returned.
 *
 *  @param iter      An information model iterator
 *  @return          The next information element within the model, or NULL
 *                  if there are no more elements.
 */
const fbInfoElement_t *
fbInfoModelIterNext(
    fbInfoModelIter_t  *iter);

/**
 *  Allocates and returns the Options Template that will be used to define
 *  Information Element Type Records.  This function does not add the template
 *  to the session or fbuf.  This function allocates the template, appends the
 *  appropriate elements to the template, and sets the scope on the template.
 *  See [RFC 5610][] for more info.
 *
 *  @param model  A pointer to an existing info model
 *  @param err    GError
 *  @return       A newly allocated template or NULL in the unlikely event
 *                a required InfoElement is missing.
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 */
fbTemplate_t *
fbInfoElementAllocTypeTemplate(
    fbInfoModel_t  *model,
    GError        **err);

/**
 *  Exports an options record to the given fbuf with information element type
 *  information about the given information element.  See RFC 5610 for
 *  details.  Use fbInfoElementAllocTypeTemplate() and add the returned
 *  template to the session, before calling this function.
 *
 *  @param fbuf       An existing fbuf
 *  @param model_ie   A pointer to the information element to export type info
 *  @param itid       The internal template id of the Options Template.
 *  @param etid       The external template id of the Options Template.
 *  @param err        GError
 *  @return           TRUE if successful, FALSE if an error occurred.
 *
 *  @since Changed in libfixbuf 2.0.0: The `etid` parameter was added.
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 */
gboolean
fbInfoElementWriteOptionsRecord(
    fBuf_t                 *fbuf,
    const fbInfoElement_t  *model_ie,
    uint16_t                itid,
    uint16_t                etid,
    GError                **err);

/**
 *  Adds an element that we received via an [RFC 5610][] Options Record to the
 *  given info model.  Returns TRUE if the element was added to the model or
 *  FALSE if it was not added because it either already exists in the model or
 *  has an privateEnterpriseNumber of 0 or FB_IE_PEN_REVERSE.
 *
 *  @param model        An information model
 *  @param rec          A pointer to the received fbInfoElementOptRec.
 *  @return             TRUE if item was successfully added to info model.
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 */
gboolean
fbInfoElementAddOptRecElement(
    fbInfoModel_t                *model,
    const fbInfoElementOptRec_t  *rec);

/**
 *  Checks to see if a template describes a meta-data record according to
 *  the bit-field value in `tests`.
 *
 *  With the exception of `FB_TMPL_IS_OPTIONS`, this function returns TRUE if
 *  any test is true---that is, you may check multiple aspects of a template.
 *  If `tests` includes `FB_TMPL_IS_OPTIONS` and the template is not an
 *  options template, the function immediately returns false and no other
 *  tests are performed.
 *
 *  Values for `tests` are:
 *
 *  FB_TMPL_IS_OPTIONS - Checks whether the template is an options template.
 *
 *  FB_TMPL_IS_META_ELEMENT - Checks whether the template contains all of the
 *  elements required by RFC 5610 for describing an information element type
 *  (type metadata).  See @ref fbInfoElementOptRec_t and
 *  fbInfoElementAddOptRecElement().
 *
 *  FB_TMPL_IS_META_TEMPLATE_v3 - Checks whether the template contains the
 *  elements requred to describe a template as used in libfixbuf 3.0.0.  This
 *  includes those in FB_TMPL_IS_META_TEMPLATE_V1 and silkAppLabel, a second
 *  templateId, and a subTemplateList.  See @ref fbTemplateInfo_t.
 *
 *  FB_TMPL_IS_META_BASICLIST - Checks whether the template contains the
 *  elements requred to describe a basicList as part of the template metadata.
 *  See @ref fbBasicListInfo_t.
 *
 *  FB_TMPL_IS_META_TEMPLATE_V1 - Checks whether the template contains the
 *  elements requred to describe a template as defined in libfixbuf 1.8.0.
 *  Specifically, it checks whether the Template contains templateId,
 *  templateName, and templateDescription.  Templates matching this may also
 *  match FB_TMPL_IS_META_TEMPLATE_V3.
 *
 *
 *  @param tmpl         A pointer to the template
 *  @param tests        A bitfield specifing which tests to check
 *  @return             TRUE if template passes the test
 *  @since libfixbuf 3.0.0
 */
gboolean
fbTemplateIsMetadata(
    const fbTemplate_t  *tmpl,
    uint32_t             tests);

/**
 *  A `tests` flag for fbTemplateIsMetadata() to check whether the @ref
 *  fbTemplate_t is an options template.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_IS_OPTIONS          (1u << 0)

/**
 *  A `tests` flag for fbTemplateIsMetadata() to check whether the @ref
 *  fbTemplate_t is for describing Information Elements ([RFC 5610][])
 *  (element metadata, @ref fbInfoElementOptRec_t).
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_IS_META_ELEMENT     (1u << 1)

/**
 *  A `tests` flag for fbTemplateIsMetadata() to check whether the @ref
 *  fbTemplate_t is for describing a Template as defined by libfixbuf 1.8.0.
 *  Templates matching this may also match @ref FB_TMPL_IS_META_TEMPLATE_V3.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_IS_META_TEMPLATE_V1 (1u << 2)

/**
 *  A `tests` flag for fbTemplateIsMetadata() to check whether the @ref
 *  fbTemplate_t is for describing a Template as defined by libfixbuf 3.0.0
 *  (@ref fbTemplateInfo_t).
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_IS_META_TEMPLATE_V3 (1u << 3)

/**
 *  A `tests` flag for fbTemplateIsMetadata() to check whether the @ref
 *  fbTemplate_t is for describing the relationship between a BasicList and
 *  its contents (@ref fbBasicListInfo_t).
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_IS_META_BASICLIST   (1u << 4)

/**
 *  A `tests` flag for fbTemplateIsMetadata() to check whether the @ref
 *  fbTemplate_t is related to template metadata (@ref fbTemplateInfo_t or
 *  @ref fbBasicListInfo_t).
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_IS_META_TMPL_ANY   \
    (FB_TMPL_IS_OPTIONS |          \
     FB_TMPL_IS_META_TEMPLATE_V1 | \
     FB_TMPL_IS_META_BASICLIST)

/**
 *  A `tests` flag for fbTemplateIsMetadata() to check whether the @ref
 *  fbTemplate_t is related to template metadata or element metadata.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_IS_META_ANY     \
    (FB_TMPL_IS_OPTIONS |       \
     FB_TMPL_IS_META_TMPL_ANY | \
     FB_TMPL_IS_META_ELEMENT)

/**
 *  Allocates a new empty template. The template will be associated with the
 *  given Information Model, and only able to use Information Elements defined
 *  within that Information Model. Templates may be associated with multiple
 *  sessions, with different template IDs each time, and as such are
 *  reference counted and owned by sessions. A template must be associated
 *  with at least one session or it will be leaked; each template is freed
 *  after its last associated session is freed.  To free a template that has
 *  not yet been added to a session, use fbTemplateFreeUnused().
 *
 *  Use fbTemplateAppend(), fbTemplateAppendSpec(), fbTemplateAppendSpecId(),
 *  fbTemplateAppendSpecArray(), and fbTemplateAppendArraySpecId() to "fill
 *  in" a template after creating it, and before associating it with any
 *  session.
 *
 *  Exits the application on allocation failure.
 *
 *  @param model     An information model
 *  @return A newly allocated empty Template.
 */
fbTemplate_t *
fbTemplateAlloc(
    fbInfoModel_t  *model);

/**
 *  Appends an information element to a template.
 *
 *  The `ex_ie` information element parameter is used as an example.  Its
 *  enterprise and element numbers are used to find the canonical element from
 *  the template's @ref fbInfoModel_t, and the length in `ex_ie` is used as
 *  the length of appended @ref fbTemplateField_t.
 *
 *  If no matching information element exists in the template's model, a new
 *  element is created, given the name "_alienInformationElement", given the
 *  datatype @ref FB_OCTET_ARRAY, and added to the model.
 *
 *  The primary use of this function is by @ref fBuf_t's template reader, but
 *  the function may be used to simulate receipt of templates over the wire.
 *  Typically templates are built with fbTemplateAppendSpec(),
 *  fbTemplateAppendSpecArray(), fbTemplateAppendSpecId(), and
 *  fbTemplateAppendArraySpecId().
 *
 *  Returns TRUE on success.  Fills `err` and returns FALSE if `tmpl` is
 *  already associated with an @ref fbSession_t (fbSessionAddTemplate()), if
 *  `ex_ie` names an existing element and the length in `ex_ie` is illegal for
 *  the datatype, or if `tmpl` is at its maximum size.
 *
 *  @param tmpl      Template to append information element to
 *  @param ex_ie     Example IE to add to the template
 *  @param err       an error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 */
gboolean
fbTemplateAppend(
    fbTemplate_t           *tmpl,
    const fbInfoElement_t  *ex_ie,
    GError                **err);

/**
 *  Potentially appends an information element described by the specifier to a
 *  template.  If the high bits in the `wantedFlags` parameter include all the
 *  high bits of the specifier's `flags` member, the @ref fbInfoElement_t
 *  named by the specifier is copied from the template's associated @ref
 *  fbInfoModel_t and the element's length is set to that from the specifier.
 *
 *  When the specifier's `flags` member is 0, the element is always included;
 *  otherwise the following must by true: (`wantedFlags` && `flags`) ==
 *  `flags`.  The `wantedFlags` parameter and `flags` member support building
 *  multiple templates with different information element features from a
 *  single specifier.
 *
 *  If `wantedFlags` prevents the element from being added to the template, no
 *  other checks of `spec` are performed and the function returns TRUE.
 *
 *  Fills `err` and returns FALSE if `spec` names an unknown element, if the
 *  `len_override` value in `spec` is illegal for the @ref
 *  fbInfoElementDataType_t of the element, if `tmpl` is associated with a
 *  @ref fbSession_t (fbSessionAddTemplate()), or if `tmpl` is at its maximum
 *  size.
 *
 *  @param tmpl        Template to append the information element to.
 *  @param spec        Specifier describing information element to append.
 *  @param wantedFlags Flags on whether to append the info element specifier.
 *  @param err         An error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 *  @see fbTemplateAppendSpecArray() fbTemplateAppendSpecId()
 */
gboolean
fbTemplateAppendSpec(
    fbTemplate_t               *tmpl,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    wantedFlags,
    GError                    **err);

/**
 *  Appends information elements described by a specifier array to a template.
 *  Calls fbTemplateAppendSpec() for each member of the array until the
 *  @ref FB_IESPEC_NULL convenience macro is encountered.
 *
 *  @param tmpl        Template to append the information elements to.
 *  @param spec        Pointer to first specifier in specifier array to append.
 *  @param wantedFlags Flags on whether to append each info element specifier.
 *  @param err         An error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 *  @see fbTemplateAppendArraySpecId()
 */
gboolean
fbTemplateAppendSpecArray(
    fbTemplate_t               *tmpl,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    wantedFlags,
    GError                    **err);

/**
 *  Potentially appends an information element described by the specifier to a
 *  template.  If the high bits in the `wantedFlags` parameter include all the
 *  high bits of the specifier's `flags` member, the @ref fbInfoElement_t
 *  referenced by the specifier is copied from the template's associated @ref
 *  fbInfoModel_t and the element's length is set to that from the specifier.
 *
 *  When the specifier's `flags` member is 0, the element is always included;
 *  otherwise the following must by true: (`wantedFlags` && `flags`) ==
 *  `flags`.  The `wantedFlags` parameter and `flags` member support building
 *  multiple templates with different information element features from a
 *  single specifier.
 *
 *  If `wantedFlags` prevents the element from being added to the template, no
 *  other checks of `spec` are performed and the function returns TRUE.
 *
 *  Fills `err` and returns FALSE if the enterpriseId and elementId of `spec`
 *  reference an unknown element, if the `len_override` value in `spec` is
 *  illegal for the @ref fbInfoElementDataType_t of the element, if `tmpl` is
 *  already associated with a @ref fbSession_t (fbSessionAddTemplate()), or if
 *  `tmpl` is at its maximum size.
 *
 *  @param tmpl        Template to append the information element to.
 *  @param spec        Specifier describing the information element to append.
 *  @param wantedFlags Flags on whether to append the info element specifier.
 *  @param err         An error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 *  @see fbTemplateAppendArraySpecId() fbTemplateAppendSpec()
 *  @since libfixbuf 3.0.0
 */
gboolean
fbTemplateAppendSpecId(
    fbTemplate_t                 *tmpl,
    const fbInfoElementSpecId_t  *spec,
    uint32_t                      wantedFlags,
    GError                      **err);

/**
 *  Appends information elements described by a specifier array to a template.
 *  Calls fbTemplateAppendSpecId() for each member of the array until the
 *  @ref FB_IESPECID_NULL convenience macro is encountered.
 *
 *  @param tmpl        Template to append the information elements to.
 *  @param spec        Pointer to first specifier in specifier array to append.
 *  @param wantedFlags Flags on whether to append each info element specifier.
 *  @param err         An error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 *  @see fbTemplateAppendSpecArray()
 *  @since libfixbuf 3.0.0
 */
gboolean
fbTemplateAppendArraySpecId(
    fbTemplate_t                 *tmpl,
    const fbInfoElementSpecId_t  *spec,
    uint32_t                      wantedFlags,
    GError                      **err);


/**
 *  Causes fbTemplatesCopy() to ignore paddingOctets elements while copying
 *  the template.  The resulting template will contain no paddingOctets
 *  elements.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_COPY_REMOVE_PADDING     0x01u

/**
 *  Prevents fbTemplatesCopy() from setting the options scope of the new
 *  template.  This can be used to create a template with a different scope
 *  count, since a template's scope may not be modified once set.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_COPY_IGNORE_SCOPE       0x04u


/**
 *  Creates a new template that is a copy of `tmpl` perhaps modified by the
 *  values specified in `flags`.
 *
 *  If `flags` is 0, the new template is an exact copy, having the same
 *  elements in the same order, having the same number of scope elements, and
 *  the elements having the same lengths.
 *
 *  When `flags` includes FB_TMPL_COPY_REMOVE_PADDING, the new templace does
 *  not contain the paddingOctets elements that were present in `tmpl`, if
 *  any.  This may be used when exporting data to create a compact external
 *  template from an existing internal template.
 *
 *  When `flags` includes FB_TMPL_COPY_IGNORE_SCOPE, the options scope of the
 *  new template is not set.
 *
 *
 *  @param tmpl      Template to copy
 *  @param flags     Controls features of the new template
 *  @return A newly allocated template or NULL in the unlikely event an
 *          InfoElement is no longer in the InfoModel.
 *
 *  @since libfixbuf 3.0.0
 */
fbTemplate_t *
fbTemplateCopy(
    const fbTemplate_t  *tmpl,
    uint32_t             flags);


/**
 *  Determines number of information elements in a template.
 *
 *  @param tmpl      A template
 *  @return information element count
 */
uint16_t
fbTemplateCountElements(
    const fbTemplate_t  *tmpl);

/**
 *  Sets the number of information elements in a template that are scope. This
 *  causes the template to become an options template.  This function may be
 *  called after all the scope information elements have been appended to the
 *  template and before the template has been added to an @ref fbSession_t.
 *
 *  A scope_count argument of zero sets the scope count to the number of IEs.
 *
 *  Returns TRUE on success.  Returns FALSE if a scope was previously set for
 *  `tmpl`, if the `scope_count` is larger than the number of elements, if
 *  `tmpl` is already associated with a @ref fbSession_t
 *  (fbSessionAddTemplate()), or if the template contains no elements.
 *
 *  @param tmpl          Template to set scope on
 *  @param scope_count   Number of scope information elements
 *
 *  @note The return type was void prior to libfixbuf 3.0.0.
 */
gboolean
fbTemplateSetOptionsScope(
    fbTemplate_t  *tmpl,
    uint16_t       scope_count);

/**
 *  Determines number of scope information elements in a template. The
 *  template is an options template if nonzero.
 *
 *  @param tmpl      A template
 *  @return scope information element count
 */
uint16_t
fbTemplateGetOptionsScope(
    const fbTemplate_t  *tmpl);

/**
 *  Determines if a template contains a given information element. Matches
 *  against information element private enterprise number and number.  Returns
 *  FALSE if either parameter is NULL.
 *
 *  @param tmpl      Template to search
 *  @param element   Pointer to an information element to search for
 *  @return          TRUE if the template contains the given IE
 *  @see fbTemplateContainsElementByName(), fbTemplateContainsSpecId(),
 *  fbTemplateFindFieldByElement(), fbTemplateFindFieldByIdent()
 */
gboolean
fbTemplateContainsElement(
    const fbTemplate_t     *tmpl,
    const fbInfoElement_t  *element);

/**
 *  Searches a Template for an Information Element and returns a TemplateField
 *  if found.
 *
 *  If `position` is non-NULL, the search begins from the position it
 *  references (where the first position is 0); on success, its referent is
 *  updated with the position of the found element.  When `skip` is non-zero,
 *  the (`skip`+1)'th field matching `ie` is returned.
 *
 *  To visit all fields `f` in template `t` that use element `ie`, use a
 *  construct like:
 *  @code{.c}
 *  for (i = 0; (f = fbTemplateFindFieldByElement(t, ie, &i, 0)); ++i) { ... }
 *  @endcode
 *
 *  @param tmpl     The template to be searched
 *  @param ie       The info element to search for
 *  @param position The starting position for search if non-NULL; updated with
 *                  location of found field
 *  @param skip     The number of matching fields to ignore before returning
 *  @return The field object for `ie` or NULL if not found
 *  @since libfixbuf 3.0.0
 *  @see fbTemplateFindFieldByIdent() to search by an Element's number and
 *  enterprise number; fbTemplateFindFieldByDataType() to search by an
 *  Element's datatype.
 */
const fbTemplateField_t *
fbTemplateFindFieldByElement(
    const fbTemplate_t     *tmpl,
    const fbInfoElement_t  *ie,
    uint16_t               *position,
    uint16_t                skip);


/**
 *  Searches a Template for a TemplateField by an @ref fbInfoElement_t's
 *  private enterprise number and element number.
 *
 *  If `position` is non-NULL, the search begins from the position it
 *  references (where the first position is 0); on success, its referent is
 *  updated with the position of the found element.  When `skip` is non-zero,
 *  the (`skip`+1)'th field matching `datatype` is returned.
 *
 *  To visit all fields `f` in template `t` having a particular ident, use a
 *  construct like:
 *  @code{.c}
 *  for (i = 0; (f = fbTemplateFindFieldByIdent(t, ent, num, &i, 0)); ++i) { ... }
 *  @endcode
 *
 *  @param tmpl     The template to be searched
 *  @param ent      The private enterprise number of the element to search for
 *  @param num      The element number of the element to search for
 *  @param position The starting position for search if non-NULL; updated with
 *                  location of found field
 *  @param skip     The number of matching fields to ignore before returning
 *  @return The field object for the element number or NULL if not found
 *  @since libfixbuf 3.0.0
 *  @see fbTemplateFindFieldByElement() to search when the @ref
 *  fbInfoElement_t is available and fbTemplateFindFieldByDataType() to search
 *  by an Element's data type.
 */
const fbTemplateField_t *
fbTemplateFindFieldByIdent(
    const fbTemplate_t  *tmpl,
    uint32_t             ent,
    uint16_t             num,
    uint16_t            *position,
    uint16_t             skip);


/**
 *  Searches a Template for a TemplateField by an @ref fbInfoElement_t's
 *  datatype.
 *
 *  If `position` is non-NULL, the search begins from the position it
 *  references (where the first position is 0); on success, its referent is
 *  updated with the position of the found element.  When `skip` is non-zero,
 *  the (`skip`+1)'th field matching `datatype` is returned.
 *
 *  To visit all fields `f` in template `t` of type `dt`, use a construct like:
 *  @code{.c}
 *  for (i = 0; (f = fbTemplateFindFieldByDataType(t, dt, &i, 0)); ++i) { ... }
 *  @endcode
 *
 *  @param tmpl     The template to be searched
 *  @param datatype The type of the info element to search for
 *  @param position The starting position for search if non-NULL; updated with
 *                  location of found field
 *  @param skip     The number of matching fields to ignore before returning
 *  @return The field object for `datatype` or NULL if not found
 *  @since libfixbuf 3.0.0
 *  @see fbTemplateFindFieldByElement() and fbTemplateFindFieldByIdent() to
 *  search for a specific @ref fbInfoElement_t
 */
const fbTemplateField_t *
fbTemplateFindFieldByDataType(
    const fbTemplate_t       *tmpl,
    fbInfoElementDataType_t   datatype,
    uint16_t                 *position,
    uint16_t                  skip);


/**
 *  Determines if a template contains at least one instance of a given
 *  information element (@ref fbInfoElement_t), specified by name in the
 *  template's information model (@ref fbInfoModel_t).  Returns FALSE if
 *  `tmpl` is NULL or if the name in `spec` does not name a known element.
 *
 *  The `len_override` and `flags` members of `spec` are ignored.
 *
 *  @param tmpl      Template to search
 *  @param spec      Specifier of information element to search for
 *  @return          TRUE if the template contains the given IE
 *  @see fbTemplateContainsElement(), fbTemplateContainsAllElementsByName(),
 *  fbTemplateContainsAllFlaggedElementsByName(), fbTemplateContainsSpecId()
 */
gboolean
fbTemplateContainsElementByName(
    const fbTemplate_t         *tmpl,
    const fbInfoElementSpec_t  *spec);

/**
 *  Determines if a template contains at least one instance of each @ref
 *  fbInfoElement_t in a given information element specifier array.  The array
 *  must be terminated by an empty specifier (@ref FB_IESPEC_NULL).  Returns
 *  TRUE if the array contains only FB_IESPEC_NULL.  Returns FALSE if any
 *  names used in `spec` are unknown by the template's @ref fbInfoModel_t.
 *
 *  Repeating an element's name in `spec` does not test whether the element
 *  appears multiple times in the template.  Use
 *  fbTemplateFindFieldByElement() to test for multiple occurrences of an
 *  element.
 *
 *  The `len_override` and `flags` members of InfoElementSpec are ignored.
 *
 *  @param tmpl      Template to search
 *  @param spec      Pointer to specifier array to search for
 *  @return          TRUE if the template contains all the given IEs
 *  @see fbTemplateContainsElementByName(),
 *  fbTemplateContainsAllFlaggedElementsByName(),
 *  fbTemplateContainsAllArraySpecId()
 */
gboolean
fbTemplateContainsAllElementsByName(
    const fbTemplate_t         *tmpl,
    const fbInfoElementSpec_t  *spec);

/**
 *  Determines if a template contains at least one instance of each
 *  information element in a given information element specifier array that
 *  match the given `wantedFlags` argument.  Returns TRUE if `wantedFlags` do
 *  not match any of the element specifiers in the array or if the array
 *  contains only @ref FB_IESPEC_NULL.
 *
 *  Repeating an element's name in `spec` does not test whether the element
 *  appears multiple times in the template.  Use
 *  fbTemplateFindFieldByElement() to test for multiple occurrences of an
 *  element.
 *
 *  The `len_override` member of InfoElementSpec is ignored.
 *
 *  @param tmpl        Template to search
 *  @param spec        Pointer to specifier array to search for
 *  @param wantedFlags Flags to choose info element specifiers. Specifier
 *                     elements whose `flags` member is non-zero and contains
 *                     high bits that are not present in `wantedFlags` are not
 *                     checked.
 *  @return            TRUE if the template contains all the given IEs
 *  @see fbTemplateContainsElementByName(),
 *  fbTemplateContainsAllElementsByName(),
 *  fbTemplateContainsAllFlaggedArraySpecId()
 */
gboolean
fbTemplateContainsAllFlaggedElementsByName(
    const fbTemplate_t         *tmpl,
    const fbInfoElementSpec_t  *spec,
    uint32_t                    wantedFlags);

/**
 *  Determines if a template contains at least one instance of a given
 *  information element (@ref fbInfoElement_t), specified by the element's
 *  enterpriseId and elementId.
 *
 *  The `len_override` and `flags` members of `spec` are ignored.
 *
 *  @param tmpl      Template to search
 *  @param spec      Specifier of information element to search for
 *  @return          TRUE if the template contains the given IE
 *
 *  @since libfixbuf-3.0.0
 *  @see fbTemplateContainsElement(), fbTemplateContainsElementByName(),
 *  fbTemplateContainsAllArraySpecId(),
 *  fbTemplateContainsAllFlaggedArraySpecId()
 */
gboolean
fbTemplateContainsSpecId(
    const fbTemplate_t           *tmpl,
    const fbInfoElementSpecId_t  *spec);

/**
 *  Determines if a template contains at least one instance of each @ref
 *  fbInfoElement_t in a specifier array.  The array must be terminated by an
 *  empty specifier (@ref FB_IESPECID_NULL).  Returns TRUE if the array
 *  contains only FB_IESPECID_NULL.
 *
 *  Repeating an {enterpriseId, elementId} pair in `spec` does not test
 *  whether the element appears multiple times in the template.  Use
 *  fbTemplateFindFieldByIdent() to test for multiple occurrences of an
 *  element.
 *
 *  The `len_override` and `flags` members of InfoElementSpecId are ignored.
 *
 *  @param tmpl      Template to search
 *  @param spec      Pointer to specifier array to search for
 *  @return          TRUE if the template contains all the given IEs
 *
 *  @since libfixbuf-3.0.0
 *  @see fbTemplateContainsSpecId(), fbTemplateContainsAllFlaggedArraySpecId(),
 *  fbTemplateContainsAllElementsByName()
 */
gboolean
fbTemplateContainsAllArraySpecId(
    const fbTemplate_t           *tmpl,
    const fbInfoElementSpecId_t  *spec);

/**
 *  Determines if a template contains at least one instance of each @ref
 *  fbInfoElement_t in a specifier array that match the given `wantedFlags`
 *  argument.  Returns TRUE if `wantedFlags` do not match any of the element
 *  specifiers in the array or if the array contains only @ref
 *  FB_IESPECID_NULL.
 *
 *  Repeating an {enterpriseId, elementId} pair in `spec` does not test
 *  whether the element appears multiple times in the template.  Use
 *  fbTemplateFindFieldByIdent() to test for multiple occurrences of an
 *  element.
 *
 *  The `len_override` member of InfoElementSpecId is ignored.
 *
 *  @param tmpl        Template to search
 *  @param spec        Pointer to specifier array to search for
 *  @param wantedFlags Flags to choose info element specifiers. Specifier
 *                     elements whose `flags` member is non-zero and contains
 *                     high bits that are not present in `wantedFlags` are not
 *                     checked.
 *  @return            TRUE if the template contains all the given IEs
 *
 *  @since libfixbuf-3.0.0
 *  @see fbTemplateContainsSpecId(), fbTemplateContainsAllArraySpecId(),
 *  fbTemplateContainsAllFlaggedElementsByName()
 */
gboolean
fbTemplateContainsAllFlaggedArraySpecId(
    const fbTemplate_t           *tmpl,
    const fbInfoElementSpecId_t  *spec,
    uint32_t                      wantedFlags);

/**
 *  Returns a field in a template at a specific position.  The positions of
 *  the first and last fields are 0 and one less than
 *  fbTemplateCountElements().
 *
 *  @param tmpl      Pointer to the template
 *  @param position  Index of the field to return
 *  @return          A pointer to the field at `position`, or
 *                  NULL if `position` is greater than number of elements.
 *  @since libfixbuf 3.0.0.
 */
const fbTemplateField_t *
fbTemplateGetFieldByPosition(
    const fbTemplate_t  *tmpl,
    uint16_t             position);

/**
 *  Deprecated alias for fbTemplateGetFieldByPosition.
 *
 *
 *  @param tmpl      Pointer to the template
 *  @param position  Index of the field to return
 *  @return          A pointer to the field at `position`, or
 *                  NULL if `position` is greater than number of elements.
 *  @deprecated Use fbTemplateGetFieldByPosition()
 */
const fbTemplateField_t *
fbTemplateGetIndexedIE(
    const fbTemplate_t  *tmpl,
    uint16_t             position)
__attribute__((__deprecated__));

/**
 *  Returns the information model, as understood by the template.
 *
 *  @param tmpl Template Pointer
 *  @return The information model
 *  @since libfixbuf 2.1.0
 */
fbInfoModel_t *
fbTemplateGetInfoModel(
    const fbTemplate_t  *tmpl);

/**
 *  Frees a template if it is not currently in use by any Session. Use this
 *  to clean up while creating templates in case of error.
 *
 *  @param tmpl template to free
 */
void
fbTemplateFreeUnused(
    fbTemplate_t  *tmpl);

/**
 *  Gets the context pointer associated with a Template.  The context pointer
 *  is set during the @ref fbNewTemplateCallback_fn when the new template is
 *  received or by fbTemplateSetContext().
 *
 *  @param tmpl Template Pointer
 *  @return ctx The application Context Pointer
 */
void *
fbTemplateGetContext(
    const fbTemplate_t  *tmpl);


/**
 *  Sets the context pointers on a Template.  If there is an existing context
 *  pointer on the Template, its free function (@ref fbTemplateCtxFree_fn) is
 *  called before overwriting the existing values.
 *
 *  @param tmpl     The Template to update.
 *  @param tmpl_ctx The context pointer to set, may be NULL.
 *  @param app_ctx  The application context pointer to set, may be NULL.
 *  @param ctx_free The function to be called to free `tmpl_ctx`, may be NULL.
 *  @see fbSessionAddNewTemplateCallback(), @ref fbNewTemplateCallback_fn
 *  @since libfixbuf 2.2.0
 */
void
fbTemplateSetContext(
    fbTemplate_t          *tmpl,
    void                  *tmpl_ctx,
    void                  *app_ctx,
    fbTemplateCtxFree_fn   ctx_free);

/**
 *  Returns the number of octets required for a data buffer (an octet array)
 *  to store a data record described by this template.  Another description is
 *  this is the length of the record when this template is used as an internal
 *  template.
 *
 *  @param tmpl   The Template to query.
 *  @since libfixbuf 2.2.0
 */
uint16_t
fbTemplateGetIELenOfMemBuffer(
    const fbTemplate_t  *tmpl);

/**
 *  Returns TRUE when templates `tmpl1` and `tmpl2` are equal.  To be equal,
 *  the templates must have same set of @ref fbInfoElement_t's in the same
 *  order, each element pair must have the same length, and the templates must
 *  have the same options scope.
 *
 *  @param tmpl1 A template to compare
 *  @param tmpl2 A template to compare
 *  @return TRUE when the templates are equal, non-zero otherwise
 *  @see fbTemplatesCompare(), fbTemplateSetCompare().
 *  @since libfixbuf 3.0.0
 */
gboolean
fbTemplatesAreEqual(
    const fbTemplate_t  *tmpl1,
    const fbTemplate_t  *tmpl2);


/**
 *  Compares templates `tmpl1` and `tmpl2` according to `flags` and returns 0
 *  if the are the same and non-zero otherwise.
 *
 *  Currently the return value cannot be used for ordering templates since it
 *  may return any non-zero value when the templates differ.  In the future,
 *  the function will be modified to return a value suitable for ordering the
 *  templates.
 *
 *  If `flags` is 0, this function is similar to fbTemplatesAreEqual(): the
 *  templates must have the same elements in the same order, matching elements
 *  must have the same length, and the scope counts must be the same.
 *
 *  The following values may be ORed and passed as `flags` to affect the
 *  comparison:
 *
 *  @ref FB_TMPL_CMP_IGNORE_PADDING - Causes the comparison to ignore any
 *  paddingOctets elements in the templates.
 *
 *  @ref FB_TMPL_CMP_IGNORE_LENGTHS - Causes the comparison to ignore the
 *  lengths when comparing elements between the templates.
 *
 *  @ref FB_TMPL_CMP_IGNORE_SCOPE - Causes the comparison to ignore the
 *  options scope count when comparing the templates.
 *
 *  @param tmpl1  A template to compare
 *  @param tmpl2  A template to compare
 *  @param flags  Controls what aspects of the templates are checked
 *  @return 0 when the templates are the same, non-zero otherwise
 *  @see fbTemplatesAreEqual(), fbTemplatesSetCompare()
 *  @since libfixbuf 3.0.0
 */
int
fbTemplatesCompare(
    const fbTemplate_t  *tmpl1,
    const fbTemplate_t  *tmpl2,
    unsigned int         flags);


/**
 *  Defines the values returned by fbTemplatesSetCompare() when checking
 *  whether one @ref fbTemplate_t is a subset of another.
 *
 *  @since libfixbuf 3.0.0
 */
typedef enum fbTemplateSetCompareStatus_en {
    /** Indicates the first template is a strict subset of the second. */
    FB_TMPL_SETCMP_SUBSET = 1,
    /** Indicates the two templates contain the same elements. */
    FB_TMPL_SETCMP_EQUAL = 2,
    /** Indicates the first template is a strict superset of the second. */
    FB_TMPL_SETCMP_SUPERSET = 3,
    /** Indicates the templates contain common elements but each has elements
     * that the other does not. */
    FB_TMPL_SETCMP_COMMON = 4,
    /** Indicates the templates contain no common elements. */
    FB_TMPL_SETCMP_DISJOINT = 8
} fbTemplatesSetCompareStatus_t;

/**
 *  Compares two Templates to determine if one is a subset of the other.
 *
 *  (This description uses "subset" and "superset", but Templates may contain
 *  repeated elements and are therefore "multisets".  The function tests
 *  whether one Template is sub-multiset of the other.)
 *
 *  To be a subset, all elements in one `tmpl1` must be contained in `tmpl2`
 *  with at least their multiplicity in `tmpl1`.  If `tmpl2` is a subset of
 *  `tmpl1`, then `tmpl1` is a superset of `tmpl2`.
 *
 *  When comparing elements, the lengths must be identical unless @ref
 *  FB_TMPL_CMP_IGNORE_LENGTHS is included in `flags`.  Elements of type
 *  paddingOctets are compared unless @ref FB_TMPL_CMP_IGNORE_PADDING is
 *  included in `flags`.
 *
 *  When `matching_fields` is non-NULL, its referent is set to the number of
 *  elements the templates have in common.
 *
 *  @param tmpl1            The first template to compare
 *  @param tmpl2            The second template to compare
 *  @param flags            Controls what aspects of the templates are checked
 *  @param matching_fields  Output value to get the number of elements they
 *                          have in common; may be NULL
 *
 *  @return FB_TMPL_SETCMP_EQUAL if the templates have the same elements,
 *  FB_TMPL_SETCMP_SUBSET if `tmpl1` is a strict subset of `tmpl2`,
 *  FB_TMPL_SETCMP_SUPERSET if `tmpl1` is a strict superset of `tmpl2`,
 *  FB_TMPL_SETCMP_COMMON if the templates have at least one common element,
 *  FB_TMPL_SETCMP_DISJOINT if the templates have no elements in common
 *  @see fbTemplatesAreEqual(), fbTemplatesCompare()
 *  @since libfixbuf 3.0.0
 */
fbTemplatesSetCompareStatus_t
fbTemplatesSetCompare(
    const fbTemplate_t  *tmpl1,
    const fbTemplate_t  *tmpl2,
    uint16_t            *matching_fields,
    unsigned int         flags);

/**
 *  Causes fbTemplatesCompare() and fbTemplateSetCompare() to ignore
 *  paddingOctets elements when comparing templates.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_CMP_IGNORE_PADDING      0x01u

/**
 *  Causes fbTemplatesCompare() and fbTemplateSetCompare() to ignore the
 *  lengths of the elements when comparing templates.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_CMP_IGNORE_LENGTHS      0x02u

/**
 *  Causes fbTemplatesCompare() to ignore the options scope count when
 *  comparing templates.
 *
 *  @since libfixbuf 3.0.0
 */
#define FB_TMPL_CMP_IGNORE_SCOPE        0x04u


/**
 *  Returns the number of fields in a record.
 *
 *  @param record  The record to get the field count of
 *  @since libfixbuf 3.0.0
 */
uint16_t
fbRecordGetFieldCount(
    const fbRecord_t  *record);

/**
 *  Releases all of the memory allocated during transcode of this record,
 *  freeing the list structures, recursively, allocated when fixbuf was
 *  encoding or decoding the record.
 *
 *  Does not free the memory referenced by the `rec` member of `record`.
 *
 *  @param record  The record to be cleared.
 *  @since libfixbuf 3.0.0
 *  @see fBufListFree() to recursively free all structured data on a record
 *  when the fbRecord_t type is not being used.
 */
void
fbRecordFreeLists(
    fbRecord_t  *record);


/**
 *  Releases any memory used by the string buffer on `value` (that is used to
 *  store the @ref fbVarfield_t) and re-initializes `value`.  Does nothing if
 *  `value` is NULL.
 *
 *  Does not release any memory associated with the list structures.
 *
 *  @param value  The value to be cleared.
 *  @since libfixbuf 3.0.0
 *  @see To clear `value` while taking ownership of the string buffer, use
 *  fbRecordValueTakeVarfieldBuf().
 */
void
fbRecordValueClear(
    fbRecordValue_t  *value);

/**
 *  Copies `srcValue` to `dstValue` and returns `dstValue`.
 *
 *  If the @ref fbInfoElementDataType_t of the @ref fbInfoElement_t referenced
 *  by `srcValue` is string (@ref FB_STRING) or octetArray (@ref
 *  FB_OCTET_ARRAY), creates a new string buffer on `dstValue` to hold the
 *  value.  Otherwise, does a shallow copy of the value.  Note that lists are
 *  NOT recursively copied by this function.
 *
 *  The caller should fbRecordValueClear() `dstValue` once it is no longer
 *  needed.
 *
 *  The function assumes `dstValue` is empty. If it previously held a string
 *  buffer, call fbRecordValueClear() on it before calling this function.
 *
 *  @param dstValue The location to store the copied value.
 *  @param srcValue The value to be copied.
 *  @return dstValue
 *  @since libfixbuf 3.0.0
 */
fbRecordValue_t *
fbRecordValueCopy(
    fbRecordValue_t        *dstValue,
    const fbRecordValue_t  *srcValue);

/**
 *  Returns a pointer to the string buffer used by `value` to store @ref
 *  fbVarfield_t values and transfers ownership of that buffer to the caller.
 *  The caller must use g_free() to release the memory when it is no longer
 *  needed.  Returns NULL if `value` is NULL or if there is no string buffer
 *  on `value`.
 *
 *  @since libfixbuf 3.0.0
 */
gchar *
fbRecordValueTakeVarfieldBuf(
    fbRecordValue_t  *value);


/**
 *  Gets the value from a Record given a Field used by the Record's @ref
 *  fbTemplate_t.  The value is returned via an @ref fbRecordValue_t
 *  structure.
 *
 *  If `field` is owned by the @ref fbTemplate_t currently associated with
 *  `record`, fills the referent of `value` with that field's value in
 *  `record` and returns TRUE.  Otherwise leaves `value` unchanged and returns
 *  FALSE.  If `field` is NULL, returns FALSE.
 *
 *  When getting a value of type @ref FB_STRING or @ref FB_OCTET_ARRAY, the
 *  value is copied into a buffer on `value`.  Use fbRecordValueClear() to
 *  free this memory or fbRecordValueTakeVarfieldBuf() to take ownership of
 *  it.
 *
 *  To get a TemplateField from a Template, use
 *  fbTemplateGetFieldByPosition(), fbTemplateFindFieldByElement(),
 *  fbTemplateFindFieldByDataType(), and fbTemplateFindFieldByIdent().
 *
 *  @param record   The record to get the value from.
 *  @param field    The field to get the value of.
 *  @param value    An output parameter to fill with the value.
 *  @returns TRUE unless `field` is NULL or not owned by the Template of
 *  `record`
 *  @since libfixbuf 3.0.0
 *  @see fbRecordCopyFieldValue() for alternate way to get the value of a
 *  Field.
 */
gboolean
fbRecordGetValueForField(
    const fbRecord_t         *record,
    const fbTemplateField_t  *field,
    fbRecordValue_t          *value);

/**
 *  Copies the value from a Record given a Field used by the Record's
 *  @ref fbTemplate_t.  The value is copied to the specified destination.
 *
 *  If `field` is owned by the Template currently associated with `record` and
 *  the `destlen` value has _exactly_ the correct value, copies the value of
 *  the field to `dest` and returns TRUE.  Otherwise leaves `dest` unchanged
 *  and returns FALSE.
 *
 *  The correct value of `destlen` depends on the @ref fbInfoElementDataType_t
 *  of the Field:
 *
 *  @ref FB_OCTET_ARRAY, @ref FB_STRING -- sizeof(fbVarfield_t); `dest` should
 *  be treated as an @ref fbVarfield_t regardless of how the value is
 *  represented in libfixbuf.  There is no guarantee that the value is NUL
 *  terminated.
 *
 *  @ref FB_UINT_8, @ref FB_INT_8, @ref FB_BOOL -- 1
 *
 *  @ref FB_UINT_16, @ref FB_INT_16 -- 2
 *
 *  @ref FB_UINT_32, @ref FB_INT_32, @ref FB_IP4_ADDR, @ref FB_DT_SEC, @ref
 *  FB_FLOAT_32 -- 4
 *
 *  @ref FB_MAC_ADDR -- 6
 *
 *  @ref FB_UINT_64, @ref FB_INT_64, @ref FB_DT_MILSEC, @ref FB_FLOAT_64 -- 8
 *
 *  @ref FB_DT_MICROSEC, @ref FB_DT_NANOSEC -- sizeof(struct timespec); `dest`
 *  should be treated as `struct timespec`.
 *
 *  @ref FB_IP6_ADDR -- 16
 *
 *  @ref FB_BASIC_LIST -- sizeof(fbBasicList_t *); `dest` should be treated as
 *  a _pointer_ to an @ref fbBasicList_t.
 *
 *  @ref FB_SUB_TMPL_LIST -- sizeof(fbSubTemplateList_t *); `dest` should be
 *  treated as a _pointer_ to an @ref fbSubTemplateList_t.
 *
 *  @ref FB_SUB_TMPL_MULTI_LIST -- sizeof(fbSubTemplateMultiList_t *); `dest`
 *  should be treated as a _pointer_ to an @ref fbSubTemplateMultiList_t.
 *
 *  To get a TemplateField from a Template, use
 *  fbTemplateGetFieldByPosition(), fbTemplateFindFieldByElement(),
 *  fbTemplateFindFieldByDataType(), and fbTemplateFindFieldByIdent().
 *
 *  @param record   The record to get the value from.
 *  @param field    The field to get the value of.
 *  @param dest     The destination where the value should be copied.
 *  @param destlen  The length of the destination which must exactly match
 *                  the required length for the data type.
 *  @returns TRUE unless `field` is NULL, `field` is not owned by the Template
 *  of `record`, or `destlen` is incorrect.
 *  @since libfixbuf 3.0.0
 *  @see fbRecordGetValueForField() for alternate way to get the value of a
 *  Field.
 */
gboolean
fbRecordCopyFieldValue(
    const fbRecord_t         *record,
    const fbTemplateField_t  *field,
    void                     *dest,
    size_t                    destlen);


/**
 *  Searches a Record for an Element and gets its Value.
 *
 *  This function is a convenience wrapper over fbTemplateFindFieldByElement()
 *  and fbRecordGetValueForField().
 *
 *  @param record   The record to get the value from.
 *  @param ie       The info element to search for
 *  @param value    An output parameter to fill with the value.
 *  @param position The starting position for search if non-NULL; updated with
 *                  location of found field
 *  @param skip     The number of matching IEs to ignore before returning
 *  @returns TRUE if a matching element is found, FALSE otherwise
 *  @since libfixbuf 3.0.0
 */
gboolean
fbRecordFindValueForInfoElement(
    const fbRecord_t       *record,
    const fbInfoElement_t  *ie,
    fbRecordValue_t        *value,
    uint16_t               *position,
    uint16_t                skip);

/**
 *  Searches `record` for a basicList of `contentsElement` values.
 *
 *  Specifically, searches `record` for element whose type is @ref
 *  FB_BASIC_LIST, where the list contains `contentsElement` items; if found,
 *  returns the basicList.  If not found, returns NULL.
 *
 *  If `position` is non-NULL, the search begins from that element position
 *  (where the first position is 0); on success, its referent is updated with
 *  the position of the found element.  When `skip` is non-zero, the
 *  (`skip`+1)'th basicList whose contents matches `contentsElement` is
 *  returned.
 *
 *  @param record           The Record to be searched
 *  @param contentsElement  The element to search for
 *  @param position         The starting position for search if non-NULL;
 *                          updated with location of found field
 *  @param skip             The number of matching lists to ignore before
 *                          returning
 *  @returns the matching list is found, NULL otherwise
 *  @since libfixbuf 3.0.0
 */
const fbBasicList_t *
fbRecordFindBasicListOfIE(
    const fbRecord_t       *record,
    const fbInfoElement_t  *contentsElement,
    uint16_t               *position,
    uint16_t                skip);


/**
 *  Searches `record` for a subTemplateList that uses `tmpl`.
 *
 *  Specifically, searches `record` for element whose type is @ref
 *  FB_SUB_TMPL_LIST, where the list uses the template `tmpl`; if found,
 *  returns the subTemplateList.  If not found, returns NULL.
 *
 *  If `position` is non-NULL, the search begins from that element position
 *  (where the first position is 0); on success, its referent is updated with
 *  the position of the found element.  When `skip` is non-zero, the
 *  (`skip`+1)'th subTemplateList that uses `tmpl` is returned.
 *
 *  @param record   The Record to be searched
 *  @param tmpl     The Template to search for
 *  @param position The starting position for search if non-NULL;
 *                  updated with location of found field
 *  @param skip     The number of matching lists to ignore before returning
 *  @returns The matching list is found, NULL otherwise
 *  @since libfixbuf 3.0.0
 */
const fbSubTemplateList_t *
fbRecordFindStlOfTemplate(
    const fbRecord_t    *record,
    const fbTemplate_t  *tmpl,
    uint16_t            *position,
    uint16_t             skip);


/**
 *  Signature of the callback function required by
 *  fbRecordFindAllElementValues().  The function is called with
 *  `parent_record` being the Record that holds the field, `parent_bl` is
 *  either NULL or the basicList that holds items matching the desired @ref
 *  fbInfoElement_t, `field` is the InfoElement searched for, `value` is the
 *  value for the desired InfoElement, and `ctx` is the user-supplied context.
 *  The function should return 0 success, and non-zero on failure.
 *
 *  @since libfixbuf 3.0.0
 */
typedef int (*fbRecordValueCallback_fn)(
    const fbRecord_t       *parent_record,
    const fbBasicList_t    *parent_bl,
    const fbInfoElement_t  *field,
    const fbRecordValue_t  *value,
    void                   *ctx);

/**
 *  Recursively searches a Record and all its sub-records for an Info Element.
 *
 *  Checks the @ref fbTemplate_t of `record` for elements that match `ie`.
 *  For each one found, `callback` is invoked with the record, the field, the
 *  value of that element in `record`, and a user-provided context.
 *
 *  The InfoElement used by each BasicList on `record` is checked, for any
 *  that match `ie`, `callback` is invoked as described above and includes a
 *  reference to the BasicList.
 *
 *  The function also recurses into each SubTemplateList and
 *  SubTemplateMultiList on `record` looking for matching elements.
 *
 *  If any invocation of `callback` returns a non-zero value, recursion stops
 *  and that return value is passed to the previous invocation and eventually
 *  to the caller.
 *
 *  @param record   The Record to be searched
 *  @param ie       The Information Element to search for
 *  @param flags    Ignored (place to tailor how the recursion happens)
 *  @param callback The function invoked on each value found
 *  @param ctx      A context for the caller's use
 *  @return The result of the `callback` or 0.
 *  @since libfixbuf 3.0.0.
 */
int
fbRecordFindAllElementValues(
    const fbRecord_t          *record,
    const fbInfoElement_t     *ie,
    unsigned int               flags,
    fbRecordValueCallback_fn   callback,
    void                      *ctx);


/**
 *  Signature of the callback function required by
 *  fbRecordFindAllSubRecords().  The function is called with `record` being a
 *  Record whose @ref fbTemplate_t matches the desired Template ID and `ctx`
 *  holding the user-supplied context.  The function should return 0 success,
 *  and non-zero on failure.
 *
 *  @since libfixbuf 3.0.0
 */
typedef int (*fbRecordSubRecordCallback_fn)(
    const fbRecord_t  *record,
    void              *ctx);


/**
 *  Recursively searches a Record for uses of a @ref fbTemplate_t having a
 *  particular ID.
 *
 *  Checks whether the @ref fbTemplate_t of `record` has ID `tid`.  If so,
 *  `callback` is invoked with the record and a user-provided context.
 *
 *  The function also recurses into each BasicList, SubTemplateList, and
 *  SubTemplateMultiList on `record` looking for uses of the Template ID.
 *
 *  If any invocation of `callback` returns a non-zero value, recursion stops
 *  and that return value is passed to the previous invocation and eventually
 *  to the caller.
 *
 *  @param record   The Record to be searched
 *  @param tid      The Template ID to search for
 *  @param flags    Ignored (place to tailor how the recursion happens)
 *  @param callback The function invoked on each record found
 *  @param ctx      A context for the caller's use
 *  @return The result of the `callback` or 0.
 *  @since libfixbuf 3.0.0.
 */
int
fbRecordFindAllSubRecords(
    const fbRecord_t              *record,
    uint16_t                       tid,
    unsigned int                   flags,
    fbRecordSubRecordCallback_fn   callback,
    void                          *ctx);


/**
 *  Copies a Record to a destination Record that uses a different Template.
 *
 *  Given a source Record `srcRec`, a destination Record `dstRec` that has a
 *  valid `rec` buffer, and a destination Template `tmpl`, this function
 *  copies the values from `srcRec` to `dstRec` for the fields the Records
 *  have in common.  Fields in `tmpl` that do not have a match in `srcRec` are
 *  left initialized to 0 or NULL.  The `tmpl` and `tid` parameters are
 *  assigned to the 'tmpl' and `tid` members of `dstRec`; the `tid` value is
 *  not otherwise used.
 *
 *  `srcRec` and `dstRec` must be different fbRecord_t objects that use
 *  different buffers.
 *
 *  When copying an @ref fbVarfield_t, the `dstRec` is set to point at the
 *  data in `srcRec`.  Reusing the `srcRec` will affect the data in `dstRec`.
 *
 *  For structured data items (lists), the list object on `dstRec` is
 *  initialized with the same element type (@ref fbBasicList_t) or template
 *  (@ref fbSubTemplateList_t) as on `srcRec`, but the `dstRec` is initialized
 *  with size 0.  The caller must manually resize the list and copy the data
 *  between the lists as desired, including re-initializing the list if
 *  needed.
 *
 *  @param srcRec  The Record to be copied.
 *  @param dstRec  The destination Record to copy `srcRec` into.
 *  @param tmpl    The Template for the destination Record.
 *  @param tid     The Template ID for the destination Record.
 *  @param err     An error description, set on error.
 *  @return TRUE on success or FALSE on failure.
 *  @since libfixbuf 3.0.0.
 */
gboolean
fbRecordCopyToTemplate(
    const fbRecord_t    *srcRec,
    fbRecord_t          *dstRec,
    const fbTemplate_t  *tmpl,
    uint16_t             tid,
    GError             **err);



/**
 *  Allocates an empty transport session state container. The new session is
 *  associated with the given information model, contains no templates, and is
 *  usable either for collection or export.
 *
 *  Each @ref fbExporter_t, @ref fbListener_t, and @ref fbCollector_t must
 *  have its own session; session state cannot be shared.
 *
 *  The fbSession_t returned by this function is not freed by calling
 *  fBufFree() or fbListenerFree().  It should be freed by the application
 *  by calling fbSessionFree().
 *
 *  Exits the application on allocation failure.
 *
 *  @param model     An information model.  Not freed by fbSessionFree().
 *                   Must be freed by user after calling fbSessionFree().
 *  @return A newly allocated, empty session state container.
 */
fbSession_t *
fbSessionAlloc(
    fbInfoModel_t  *model);


/**
 *  Configures a session to export type information for enterprise-specific
 *  information elements as options records according to [RFC 5610][].
 *
 *  Regardless of the `enabled` value, this function creates the type
 *  information template and adds it to the session with the template ID `tid`
 *  (removing the existing template having that id, if any) or an arbitrary ID
 *  when `tid` is @ref FB_TID_AUTO.
 *
 *  If `enabled` is TRUE, the information metadata is exported each time
 *  fbSessionExportTemplates() is called.
 *
 *  When collecting, use fBufSetAutomaticElementInsert() to automatically
 *  update the information model with these RFC 5610 elements.
 *
 *  @param session A session state container
 *  @param enabled TRUE to enable type metadata export, FALSE to disable
 *  @param tid The template id to use for type-information records or
 *         FB_TID_AUTO to assign an arbitrary id.
 *  @param err An error descriptor, set on failure
 *  @return The template id for type-information records or 0 if the template
 *  could not be added to the session.
 *  @since libfixbuf 2.3.0
 *
 *  [RFC 5610]: https://tools.ietf.org/html/rfc5610
 */
uint16_t
fbSessionSetMetadataExportElements(
    fbSession_t  *session,
    gboolean      enabled,
    uint16_t      tid,
    GError      **err);

/**
 *  Configures a Session to export template metadata (@ref fbTemplateInfo_t)
 *  as options records.  Template metadata includes the name of a Template and
 *  optionally additional information as described by fbTemplateInfoInit() and
 *  fbTemplateInfoAddBasicList().
 *
 *  If enabled, the metadata is exported each time fbSessionExportTemplates()
 *  or fbSessionExportTemplate() is called.  In addition, the metadata is
 *  exported when fbSessionAddTemplate() is called if the `tmplInfo` parameter
 *  is not NULL and the Session is associated with an @ref fbExporter_t.
 *
 *  Regardless of the `enabled` value, this function creates the
 *  template-metadata template and adds it to the session with the template ID
 *  `tid` (removing the existing template having that id, if any) or an
 *  arbitrary ID when `tid` is @ref FB_TID_AUTO.
 *
 *  When collecting, use fBufSetAutomaticMetadataAttach() to recognize the
 *  Template Metadata records.
 *
 *  @param session A session state container
 *  @param enabled TRUE to enable template metadata export, FALSE to disable
 *  @param tmplInfoTid The template id to use for TemplateInfo records or
 *         FB_TID_AUTO to assign an arbitrary id.
 *  @param blInfoTid The template id to use for the @ref fbBasicListInfo_t
 *         subrecords or FB_TID_AUTO to assign an arbitrary id.
 *  @param err An error descriptor, set on failure
 *  @return The template id for TemplateInfo records or 0 if the template
 *  could not be added to the session.
 *  @since Added in libfixbuf 2.3.0; modified in libfixbuf 3.0.0 to take
 *  `blInfoTid`.
 */
uint16_t
fbSessionSetMetadataExportTemplates(
    fbSession_t  *session,
    gboolean      enabled,
    uint16_t      tmplInfoTid,
    uint16_t      blInfoTid,
    GError      **err);

/**
 *  Gets the info model for the session.
 *
 *  @param session
 *  @return a pointer to the info model for the session
 */
fbInfoModel_t *
fbSessionGetInfoModel(
    const fbSession_t  *session);

/**
 *  This function sets the callback that allows the application to set its
 *  own context variable with a new incoming template.  Assigning a callback
 *  is not required and is only useful if the application either needs to
 *  store some information about the template or to prevent certain nested
 *  templates from being transcoded.  If the application's template contains
 *  a subTemplateMultiList or subTemplateList and the callback is not used,
 *  all incoming templates contained in these lists will be fully transcoded
 *  and the application is responsible for freeing any nested lists contained
 *  within those objects.
 *
 *  This function should be called after fbSessionAlloc().  Fixbuf often
 *  clones sessions upon receiving a connection.  In the TCP case, the
 *  application has access to the session right after fbListenerWait() returns
 *  by calling fBufGetSession().  In the UDP case, the application does
 *  not have access to the fbSession until after a call to fBufNext() for
 *  fBufNextCollectionTemplate() and by this time the application may have
 *  already received some templates.  Therefore, it is important to call this
 *  function before fBufNext().  Any callbacks added to the session will be
 *  carried over to cloned sessions.
 *
 *  @param session pointer session to assign the callback to
 *  @param callback the function that should be called when a new template
 *                  is received
 *  @param app_ctx parameter that gets passed into the callback function.
 *                 This can be used to pass session-specific information
 *                 state information to the callback function.
 *  @since libfixbuf 2.0.0
 */
void
fbSessionAddNewTemplateCallback(
    fbSession_t               *session,
    fbNewTemplateCallback_fn   callback,
    void                      *app_ctx);


/**
 *  Arranges for templates read by `incomingSession` to be copied to
 *  `exportSession` by setting the new template callback.
 *
 *  This is a convenience function that sets the new template callback
 *  function (see fbSessionAddNewTemplateCallback()) on `incomingSession` to
 *  fbSessionCopyIncomingTemplatesCallback(), using `exportSession` as the
 *  value for the `app_ctx` paramter.
 *
 *  @param incomingSession  Session reading the templates to be copied
 *  @param exportSession    Session where the templates are to be copied
 *  @since libfixbuf 3.0.0
 */
void
fbSessionSetCallbackCopyTemplates(
    fbSession_t  *incomingSession,
    fbSession_t  *exportSession);


/**
 *  Arranges for templates read by `incomingSession` to be copied to
 *  `exportSession`.
 *
 *  This function may be used as the value of the `callback` parameter to
 *  fbSessionAddNewTemplateCallback().  The caller should use `exportSession`
 *  as the value for the `app_ctx` parameter to that function.  (See
 *  fbSessionSetCallbackCopyTemplates() that simplies this process.)
 *
 *  When invoked, the function calls fbSessionAddTemplatesForExport() using
 *  the `exportSession`, `tid`, and `tmpl` as the value for the first three
 *  parameters.
 *
 *  @param incomingSession  Session reading the templates to be copied
 *  @param tid              ID for the template that was received
 *  @param tmpl             Template that was received
 *  @param exportSession    Session where the templates are to be copied
 *  @param tmpl_ctx         Unused
 *  @param tmpl_ctx_free_fn Unused
 *  @since libfixbuf 3.0.0
 */
void
fbSessionCopyIncomingTemplatesCallback(
    fbSession_t           *incomingSession,
    uint16_t               tid,
    fbTemplate_t          *tmpl,
    void                  *exportSession,
    void                 **tmpl_ctx,
    fbTemplateCtxFree_fn  *tmpl_ctx_free_fn);

/**
 *  Adds an external-internal @ref fbTemplate_t mapping pair for list elements
 *  in a Session.  When used on a Session connected to an @ref fbCollector_t,
 *  it tells the Session that any sub-records in a an @ref fbSubTemplateList_t
 *  or @ref fbSubTemplateMultiList_t that use the incoming external template
 *  `ext_tid` should be mapped to (transcoded into) the internal template
 *  `int_tid`.
 *
 *  If the value of `int_tid` is 0, the Session does not decode any list where
 *  the external template is `ext_tid`.  This allows a collector to specify
 *  which templates that are used in lists it wishes handle.
 *
 *  For any other `int_tid`, the Session transcodes list sub-records that use
 *  `ext_tid` to the Template whose ID is `int_tid`.  If `int_tid` equals
 *  `ext_tid` and the Session does not contain a Template for `int_tid`, the
 *  transcoder uses the external template as the internal template.
 *
 *  The template pair is not added if `int_tid` has a value other than
 *  `ext_tid` or 0 and `session` does not have an internal template for
 *  `int_tid`.
 *
 *  Once any template pair has been added to a Session, the default behavior
 *  is that all unspecified incoming external template IDs used by lists are
 *  ignored.  If no template pairs exist for a Session, the default behavior
 *  is that all incoming external templates used by lists are fully decoded.
 *
 *  When the Session is used by an @ref fbExporter_t, the template mapping
 *  pairs set by this function are not used.
 *
 *  @param session pointer to the session to add the pair to
 *  @param ext_tid the external template ID
 *  @param int_tid the internal template ID used to decode the data when the
 *                 associated external template is used
 */
void
fbSessionAddTemplatePair(
    fbSession_t  *session,
    uint16_t      ext_tid,
    uint16_t      int_tid);

/**
 *  Removes a template mapping pair for list sub-records from the Session.
 *
 *  This is called by fixbuf when a template is revoked from the session by
 *  the node on the other end of the connection.
 *
 *  @param session pointer to the session to remove the pair from
 *  @param ext_tid the external template ID for the pair
 *  @see fbSessionAddTemplatePair()
 */
void
fbSessionRemoveTemplatePair(
    fbSession_t  *session,
    uint16_t      ext_tid);

/**
 *  Finds the internal template ID to use for sub-records in a list that use
 *  the external template `ext_tid` in `session`.
 *
 *  Returns the internal ID for `ext_tid` as set by
 *  fbSessionAddTemplatePair().  If no template pairs have been specified for
 *  the Session, returns `ext_tid`.  If template pairs exist and `ext_tid` is
 *  not found, returns 0.
 *
 *  @param session pointer to the session used to find the pair
 *  @param ext_tid external template ID used to find a pair
 *  @return the internal template ID from the pair
 *  @see fbSessionAddTemplatePair()
 */
uint16_t
fbSessionLookupTemplatePair(
    const fbSession_t  *session,
    uint16_t            ext_tid);

/**
 *  Frees a transport session state container. This is done automatically when
 *  freeing the listener or buffer with which the session is
 *  associated. Use this call if a session needs to be destroyed before it
 *  is associated.
 *
 *  @param session   session state container to free.
 */
void
fbSessionFree(
    fbSession_t  *session);

/**
 *  Resets the external state (sequence numbers and templates) in a session
 *  state container.
 *
 *  FIXME: Verify that this call actually makes sense; either that a session
 *  is reassociatable with a new collector, or that you need to do this when
 *  reassociating a collector with a connection. Once this is done, rewrite
 *  this documentation
 *
 *  @param session   session state container to reset
 */
void
fbSessionResetExternal(
    fbSession_t  *session);

/**
 *  Sets the current observation domain on a session. The domain
 *  is used to scope sequence numbers and external templates. This is called
 *  automatically during collection, but must be called to set the domain
 *  for export before adding external templates or writing records.
 *
 *  Notice that a domain change does not automatically cause any associated
 *  export buffers to emit messages; a domain change takes effect with the
 *  next message started. Therefore, call fBufEmit() before setting the domain
 *  on the buffer's associated session.
 *
 *  @param session   a session state container
 *  @param domain    ID of the observation domain to set
 */
void
fbSessionSetDomain(
    fbSession_t  *session,
    uint32_t      domain);

/**
 *  Retrieves the current domain on a session.
 *
 *  @param session a session state container
 *  @return the ID of the session's current observation domain
 */
uint32_t
fbSessionGetDomain(
    const fbSession_t  *session);

/**
 *  Gets the largest decoded size of an internal template in the session.
 *  This is the number of bytes needed by store the largest record described
 *  by an internal template in the session.
 *
 *  @param session a session
 *  @return The number of bytes needed to store the largest record described
 *  by an internal template in the session
 *  @since libfixbuf 2.2.0
 */
uint16_t
fbSessionGetLargestInternalTemplateSize(
    fbSession_t  *session);

/**
 *  Retrieves the Collector that was associated with `session` by an explicit
 *  call to fBufAllocForCollection() or implicitly by fbListenerWait().
 *  Returns NULL if `session` is not associated with a Collector (for example,
 *  it is being used for export).
 *
 *  One place this function is helpful is in the function assigned to
 *  fbSessionAddNewTemplateCallback() so that the callback may access the
 *  Collector and its context.
 *
 *  @param session a session state container
 *  @return fbCollector_t the collector that was created with the session
 *
 */
fbCollector_t *
fbSessionGetCollector(
    const fbSession_t  *session);

/**
 *  Exports a single external @ref fbTemplate_t in the current domain of a
 *  given Session.  Writes the template to the associated export @ref
 *  fBuf_t. May cause a message to be emitted if the associated export buffer
 *  is in automatic mode, or return with FB_ERROR_EOM if the associated export
 *  buffer is not in automatic mode.
 *
 *  Also exports an options record containing the template's metadata if a
 *  @ref fbTemplateInfo_t was specified to fbSessionAddTemplate() and metadata
 *  export is enabled (fbSessionSetMetadataExportTemplates()).
 *
 *  @param session   a session state container associated with an export buffer
 *  @param tid       template ID within current domain to export
 *  @param err       an error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 */
gboolean
fbSessionExportTemplate(
    fbSession_t  *session,
    uint16_t      tid,
    GError      **err);

/**
 *  Exports all external templates in the current domain of a given session.
 *  Writes templates to the associated export buffer. May cause a message to
 *  be emitted if the associated export buffer is in automatic mode, or return
 *  with FB_ERROR_EOM if the associated export buffer is not in automatic
 *  mode.
 *
 *  When template and/or information element metadata is enabled, those
 *  options records are also exported.
 *
 *  All external templates are exported each time this function is called.
 *
 *  @param session   a session state container associated with an export buffer
 *  @param err       an error description, set on failure.
 *  @return TRUE on success, FALSE on failure.
 *  @see fbSessionSetMetadataExportTemplates() to enable template metadata,
 *  fbSessionSetMetadataExportElements() to enable information element
 *  metadata
 */
gboolean
fbSessionExportTemplates(
    fbSession_t  *session,
    GError      **err);

/**
 *  Adds a Template to a Session.  Gives the Template the ID specified in
 *  `tid` (removing the existing template having that ID, if any) or assigns
 *  the Template an arbitrary ID if `tid` is @ref FB_TID_AUTO.
 *
 *  If the Template is external (`internal` is false), adds the Template to
 *  the current domain and exports the Template if the Session is already
 *  associated with an export buffer (@ref fBuf_t).
 *
 *  The `tmplInfo` parameter, which may be NULL, provides information about
 *  the Template including a name, and this data is exported as Option Records
 *  when the Session has been configured to do so
 *  (fbSessionSetMetadataExportTemplates()).  When `internal` is TRUE, the
 *  `tmplInfo` parameter is ignored.  When `internal` is FALSE and `tmplInfo`
 *  is provided, it must have a valid name.  If the Session is associated with
 *  an export buffer and metadata export is enabled, the TemplateInfo is
 *  exported prior to exporting the Template.
 *
 *  After this call, the Session owns the TemplateInfo and will free it when
 *  the Session is freed.  TemplateInfo may exist in exactly one Session; use
 *  fbTemplateInfoCopy() to add a copy of TemplateInfo to a different Session.
 *
 *  Calling this function twice with the same parameters may cause the
 *  Template to be freed and the Session to keep a handle to the invalid
 *  Template.  If necessary, first use fbSessionGetTemplate() to check for the
 *  presence of the Template on the Session.
 *
 *  @param session   A session state container
 *  @param internal  TRUE if the template is internal, FALSE if external.
 *  @param tid       Template ID to assign, replacing any current template
 *                   in case of collision; or FB_TID_AUTO to assign a new tId.
 *  @param tmpl      Template to add
 *  @param tmplInfo  The TemplateInfo for `tmpl` or NULL.
 *  @param err       An error description, set on failure
 *  @return The template ID of the added template, or 0 on failure.
 *  @since Modified in libfixbuf 3.0.0 to take the `tmplInfo` parameter.
 */
uint16_t
fbSessionAddTemplate(
    fbSession_t       *session,
    gboolean           internal,
    uint16_t           tid,
    fbTemplate_t      *tmpl,
    fbTemplateInfo_t  *tmplInfo,
    GError           **err);


/**
 *  Adds a Template to a Session being used for export as both an internal
 *  template and an external template.
 *
 *  This is a convenience function to save the caller from invoking
 *  fbSessionAddTemplate() multiple times. See that function for descriptions
 *  of the parameters.
 *
 *  If `tmpl` contains paddingOctets element(s), the function creates a copy
 *  of `tmpl` without padding by calling fbTemplateCopy() with `flags`
 *  parameter as @ref FB_TMPL_COPY_REMOVE_PADDING.  It calls
 *  fbSessionAddTemplate() twice, first to add the copy (or the original) an
 *  external template on `session` with template ID `tid`, and second to add
 *  the original `tmpl` as an internal template using the template ID returned
 *  when the external template was added.
 *
 *  @param session      A session state container
 *  @param tid          Template ID to assign, replacing any current template
 *                      in case of collision; or FB_TID_AUTO for a new tId.
 *  @param tmpl         Template to add
 *  @param tmplInfo     Template metadata
 *  @param err          error description, set on error
 *  @return template id of newly added template, 0 on error
 *  @see fbSessionAddTemplate() for additional information.
 *  @since libfixbuf 3.0.0
 */
uint16_t
fbSessionAddTemplatesForExport(
    fbSession_t       *session,
    uint16_t           tid,
    fbTemplate_t      *tmpl,
    fbTemplateInfo_t  *tmplInfo,
    GError           **err);


/**
 *  Removes a template from a session.  If external, removes the template from
 *  the current domain, and exports a template revocation set if the session
 *  is associated with an export buffer.
 *
 *  @param session   A session state container
 *  @param internal  TRUE if the template is internal, FALSE if external.
 *  @param tid       Template ID to remove.
 *  @param err       An error description, set on failure
 *  @return TRUE on success, FALSE on failure.
 */
gboolean
fbSessionRemoveTemplate(
    fbSession_t  *session,
    gboolean      internal,
    uint16_t      tid,
    GError      **err);

/**
 *  Retrieves a template from a session by ID. If external, retrieves the
 *  template within the current domain.
 *
 *  @param session   A session state container
 *  @param internal  TRUE if the template is internal, FALSE if external.
 *  @param tid       ID of the template to retrieve.
 *  @param err       An error description, set on failure.  May be NULL.
 *  @return The template with the given ID, or NULL on failure.
 */
fbTemplate_t *
fbSessionGetTemplate(
    const fbSession_t  *session,
    gboolean            internal,
    uint16_t            tid,
    GError            **err);

/**
 *  Allocates an exporting process endpoint for a network connection.  The
 *  remote collecting process is specified by the given connection specifier.
 *  The underlying socket connection is not opened until the first message is
 *  emitted from the buffer associated with the exporter.
 *
 *  Exits the application on allocation failure or if libfixbuf does not
 *  support the network connection type.
 *
 *  @param spec      remote endpoint connection specifier.  A copy is made
 *                   for the exporter, it is freed later.  User is responsible
 *                   for original spec pointer
 *  @return a new exporting process endpoint
 */
fbExporter_t *
fbExporterAllocNet(
    const fbConnSpec_t  *spec);

/**
 *  Allocates an exporting process endpoint for a named file. The underlying
 *  file will not be opened until the first message is emitted from the buffer
 *  associated with the exporter.
 *
 *  Exits the application on allocation failure.
 *
 *  @param path      pathname of the IPFIX File to write, or "-" to
 *                   open standard output.  Path is duplicated and handled.
 *                   Original pointer is up to the user.
 *  @return a new exporting process endpoint
 */
fbExporter_t *
fbExporterAllocFile(
    const char  *path);

/**
 *  Allocates an exporting process to use the existing buffer `buf` having the
 *  specified size.  Each call to fBufEmit() copies data into `buf` starting
 *  at `buf[0]`; use fbExporterGetMsgLen() to get the number of bytes copied
 *  into `buf`.
 *
 *  Exits the application on allocation failure.
 *
 *  @param buf       existing buffer that IPFIX messages are copied to
 *  @param bufsize   size of the buffer
 *  @return a new exporting process endpoint
 */
fbExporter_t *
fbExporterAllocBuffer(
    uint8_t   *buf,
    uint16_t   bufsize);


/**
 *  Allocates an exporting process endpoint for an opened ANSI C file pointer.
 *
 *  Exits the application on allocation failure.
 *
 *  @param fp        open file pointer to write to.  File pointer is created
 *                   and freed outside of the Exporter functions.
 *  @return a new exporting process endpoint
 */
fbExporter_t *
fbExporterAllocFP(
    FILE  *fp);


/**
 *  Sets the SCTP stream for the next message exported. To change the SCTP
 *  stream used for export, first emit any message in the exporter's
 *  associated buffer with fbufEmit(), then use this call to set the stream
 *  for the next message. This call cancels automatic stream selection, use
 *  fbExporterAutoStream() to re-enable it. This call is a no-op for non-SCTP
 *  exporters.
 *
 *  @param exporter      an exporting process endpoint.
 *  @param sctp_stream   SCTP stream to use for next message.
 */
void
fbExporterSetStream(
    fbExporter_t  *exporter,
    int            sctp_stream);

/**
 *  Enables automatic SCTP stream selection for the next message exported.
 *  Automatic stream selection is the default; use this call to re-enable it
 *  on a given exporter after using fbExporterSetStream(). With automatic
 *  stream selection, the minimal behavior specified in the original IPFIX
 *  protocol (RFC xxxx) is used: all templates and options templates are
 *  exported on stream 0, and all data is exported on stream 1. This call is a
 *  no-op for non-SCTP exporters.
 *
 *  @param exporter      an exporting process endpoint.
 */
void
fbExporterAutoStream(
    fbExporter_t  *exporter);

/**
 *  Forces the file or socket underlying an exporting process endpoint to
 *  close.  No effect on open file endpoints. The file or socket may be
 *  reopened on a subsequent message emission from the associated buffer.
 *
 *  @param exporter  an exporting process endpoint.
 */
void
fbExporterClose(
    fbExporter_t  *exporter);

/**
 *  Gets the (transcoded) message length that was copied to the exporting
 *  buffer upon fBufEmit() when using fbExporterAllocBuffer().
 *
 *  @param exporter an exporting process endpoint.
 */
size_t
fbExporterGetMsgLen(
    const fbExporter_t  *exporter);

/**
 *  Gets the number of octets that have been written to the exporter's file,
 *  socket, or buffer since the exporter was opened (allocated for `FP`
 *  exporters) or the counter was last reset.
 *
 *  The returned value does not include any partial messages in an @ref fBuf_t
 *  that have not yet been emitted to the exporter.
 *
 *  @param exporter an exporting process endpoint.
 *  @see fbExporterGetOctetCountAndReset(), fbExporterResetOctetCount(),
 *  fBufEmit()
 *  @since libfixbuf 3.0.0
 */
size_t
fbExporterGetOctetCount(
    const fbExporter_t  *exporter);

/**
 *  Gets the number of octets that have been written to the exporter's file,
 *  socket, or buffer since the exporter was opened (allocated for `FP`
 *  exporters) or the counter was last reset and resets the counter.
 *
 *  @param exporter an exporting process endpoint.
 *  @see fbExporterGetOctetCount(), fbExporterResetOctetCount()
 *  @since libfixbuf 3.0.0
 */
size_t
fbExporterGetOctetCountAndReset(
    fbExporter_t  *exporter);

/**
 *  Resets the counter that holds the number of octets written to the
 *  exporter's file, socket, or buffer.
 *
 *  @param exporter an exporting process endpoint.
 *  @see fbExporterGetOctetCount(), fbExporterGetOctetCountAndReset()
 *  @since libfixbuf 3.0.0
 */
void
fbExporterResetOctetCount(
    fbExporter_t  *exporter);

/**
 *  Allocates a collecting process endpoint for a named file. The underlying
 *  file will be opened immediately.
 *
 *  Exits the application on allocation failure.
 *
 *  @param ctx       application context; for application use, retrievable
 *                   by fbCollectorGetContext()
 *  @param path      path of file to read, or "-" to read standard input.
 *                   Used to get fp, user creates and frees.
 *  @param err       An error description, set on failure.
 *  @return a collecting process endpoint, or NULL if the file cannot be opened
 */
fbCollector_t *
fbCollectorAllocFile(
    void        *ctx,
    const char  *path,
    GError     **err);

/**
 *  Allocates a collecting process endpoint for an open file.
 *
 *  Exits the application on allocation failure.
 *
 *  @param ctx       application context; for application use, retrievable
 *                   by fbCollectorGetContext()
 *  @param fp        file pointer to file to read.  Created and freed by user.
 *                   Must be kept around for the life of the collector.
 *  @return a collecting process endpoint.
 */
fbCollector_t *
fbCollectorAllocFP(
    void  *ctx,
    FILE  *fp);


/**
 *  Retrieves the application context associated with a collector. This
 *  context is taken from the `ctx` argument of fbCollectorAllocFile() or
 *  fbCollectorAllocFP(), or passed out via the `ctx` argument of the
 *  `appinit` function argument (@ref fbListenerAppInit_fn) to
 *  fbListenerAlloc().
 *
 *  @param collector a collecting process endpoint.
 *  @return the application context
 */
void *
fbCollectorGetContext(
    const fbCollector_t  *collector);

/**
 *  Closes the file or socket underlying a collecting process endpoint.
 *  No effect on open file endpoints. If the collector is attached to a
 *  buffer managed by a listener, the buffer will be removed from the
 *  listener (that is, it will not be returned by subsequent fbListenerWait()
 *  calls).
 *
 *  @param collector  a collecting process endpoint.
 */
void
fbCollectorClose(
    fbCollector_t  *collector);


/**
 *  Sets the collector to only receive from the given IP address over UDP.
 *  The port will be ignored.  Use fbListenerGetCollector() to get the pointer
 *  to the collector after calling fbListenerAlloc(). ONLY valid for UDP.
 *  Set the address family in address.
 *
 *  @param collector pointer to collector
 *  @param address pointer to sockaddr struct with IP address and family.
 *  @param address_length address length
 *
 */
void
fbCollectorSetAcceptOnly(
    fbCollector_t    *collector,
    struct sockaddr  *address,
    size_t            address_length);

/**
 *  Allocates a listener. The listener will listen on a specified local
 *  endpoint, and create a new collecting process endpoint and collection
 *  buffer for each incoming connection. Each new buffer will be associated
 *  with a clone of a given session state container.
 *
 *  The application may associate context with each created collecting process
 *  endpoint, or veto a connection attempt, via a function colled on each
 *  connection attempt passed in via the appinit parameter. If this function
 *  will create application context, provide a function via the
 *  appfree parameter which will free it.
 *
 *  The fbListener_t returned should be freed by the application by calling
 *  fbListenerFree().
 *
 *  Exits the application on allocation failure.
 *
 *  @param spec      local endpoint connection specifier.
 *                   A copy is made of this, which is freed by listener.
 *                   Original pointer freeing is up to the user.
 *  @param session   session state container to clone for each collection
 *                   buffer created by the listener.  Not freed by listener.
 *                   Must be kept alive while listener exists.
 *  @param appinit   application connection initiation function. Called on
 *                   each collection attempt; vetoes connection attempts and
 *                   creates application context.
 *  @param appfree   application context free function.
 *  @param err       An error description, set on failure.
 *  @return a new listener, or NULL if the endpoint cannot be initialized.
 */
fbListener_t *
fbListenerAlloc(
    const fbConnSpec_t    *spec,
    fbSession_t           *session,
    fbListenerAppInit_fn   appinit,
    fbListenerAppFree_fn   appfree,
    GError               **err);

/**
 *  Frees a listener. Stops listening on the local endpoint, and frees any
 *  open buffers still managed by the listener.
 *
 *  @param listener a listener
 */
void
fbListenerFree(
    fbListener_t  *listener);

/**
 *  Waits on a listener. Accepts pending connections from exporting processes.
 *  Returns the next collection buffer with available data to read; if the
 *  collection buffer returned by the last call to fbListenerWait() is
 *  available, it is preferred. Blocks forever (or until fbListenerInterrupt()
 *  is called) if no messages or connections are available.
 *
 *  To effectively use fbListenerWait(), the application should set up an
 *  session state container with internal templates, call fbListenerWait() to
 *  accept a first connection, then read records from the collector buffer to
 *  end of message (@ref FB_ERROR_EOM). At end of message, the application
 *  should then call fbListenerWait() to accept pending connections or switch
 *  to another collector buffer with available data. Note that each collection
 *  buffer returned created by fbListenerWait() is set automatically read the
 *  next message and not to raise FB_ERROR_EOM.  To modify this behavior, call
 *  fBufSetAutomaticNextMessage() on the @ref fBuf_t returned from this
 *  function with FALSE as the second parameter.
 *
 *  @param listener  a listener
 *  @param err       An error description, set on failure.
 *  @return a collection buffer with available data, or NULL on failure.
 *  @see fbListenerWaitNoCollectors() for a function that does not monitor
 *  active Collectors.
 */
fBuf_t *
fbListenerWait(
    fbListener_t  *listener,
    GError       **err);

/**
 *  Waits for an incoming connection, just like fbListenerWait(), except that
 *  this function does not monitor active collectors.  This allows for a
 *  multi-threaded application to have one thread monitoring the listeners,
 *  and one keeping track of Collectors.
 *
 *  @param listener  The listener to wait for connections on
 *  @param err       An error description, set on failure.
 *  @return a collection buffer for the new connection, NULL on failure.
 */
fBuf_t *
fbListenerWaitNoCollectors(
    fbListener_t  *listener,
    GError       **err);

/**
 *  Causes the current or next call to fbListenerWait() to unblock and return.
 *  Use this from a thread or a signal handler to interrupt a blocked
 *  listener.
 *
 *  @param listener listener to interrupt.
 */
void
fbListenerInterrupt(
    fbListener_t  *listener);


/**
 *  If a collector is associated with the listener class, this will return a
 *  handle to the collector state structure.
 *
 *  @param listener handle to the listener state
 *  @param collector pointer to a collector state pointer, set on return
 *         if there is no error
 *  @param err a GError structure holding an error message on error
 *  @return FALSE on error, check err, TRUE on success
 */
gboolean
fbListenerGetCollector(
    const fbListener_t  *listener,
    fbCollector_t      **collector,
    GError             **err);



/**
 *  Removes an input translator from a given collector such that it
 *  will operate on IPFIX protocol again.
 *
 *  @param collector the collector on which to remove
 *         the translator
 *  @param err when an error occurs, a GLib GError
 *         structure is set with an error description
 *
 *  @return TRUE on success, FALSE on failure
 */
gboolean
fbCollectorClearTranslator(
    fbCollector_t  *collector,
    GError        **err);


/**
 *  Sets the collector input translator to convert NetFlowV9 into IPFIX
 *  for the given collector.
 *
 *  @param collector pointer to the collector state
 *         to perform Netflow V9 conversion on
 *  @param err GError structure that holds the error
 *         message if an error occurs
 *
 *  @return TRUE on success, FALSE on error
 */
gboolean
fbCollectorSetNetflowV9Translator(
    fbCollector_t  *collector,
    GError        **err);


/**
 *  Sets the collector input translator to convert SFlow into IPFIX for
 *  the given collector.
 *
 *  @param collector pointer to the collector state
 *         to perform SFlow conversion on
 *  @param err GError structure that holds the error
 *         message if an error occurs
 *
 *  @return TRUE on success, FALSE on error
 */
gboolean
fbCollectorSetSFlowTranslator(
    fbCollector_t  *collector,
    GError        **err);

/**
 *  Returns the number of potential missed export packets of the Netflow v9
 *  session that is currently set on the collector (the session is set on the
 *  collector when an export packet is received) if peer is NULL.  If peer is
 *  set, this will look up the session for that peer/obdomain pair and return
 *  the missed export packets associated with that peer and obdomain.  If
 *  peer/obdomain pair doesn't exist, this function returns 0.  This can't
 *  return the number of missed flow records since Netflow v9 increases
 *  sequence numbers by the number of export packets it has sent, NOT the
 *  number of flow records (like IPFIX and netflow v5 does).
 *
 *  @param collector
 *  @param peer [OPTIONAL] peer address of NetFlow v9 exporter
 *  @param peerlen size of peer object
 *  @param obdomain observation domain of NetFlow v9 exporter
 *  @return number of missed packets since beginning of session
 *
 */
uint32_t
fbCollectorGetNetflowMissed(
    const fbCollector_t    *collector,
    const struct sockaddr  *peer,
    size_t                  peerlen,
    uint32_t                obdomain);

/**
 *  Returns the number of potential missed export packets of the
 *  SFlow session that is identified with the given ip/agentID. The agent
 *  ID is a field that is in the sFlow header and is sent with every
 *  packet.  Fixbuf keeps track of sequence numbers for sFlow sessions
 *  per agent ID.
 *
 *  @param collector
 *  @param peer address of exporter to lookup
 *  @param peerlen sizeof(peer)
 *  @param obdomain observation domain of peer exporter
 *  @return number of missed packets since beginning of session
 *
 */
uint32_t
fbCollectorGetSFlowMissed(
    const fbCollector_t    *collector,
    const struct sockaddr  *peer,
    size_t                  peerlen,
    uint32_t                obdomain);

/**
 *  Retrieves information about the node connected to this collector
 *
 *  @param collector pointer to the collector to get peer information from
 *  @return pointer to sockaddr structure containing IP information of peer
 */
const struct sockaddr *
fbCollectorGetPeer(
    const fbCollector_t  *collector);

/**
 *  Retrieves the observation domain of the node connected to the UDP
 *  collector.  The observation domain only gets set on the collector when
 *  collecting via UDP.  If the collector is using another mode of transport,
 *  use fbSessionGetDomain().
 *
 *  @param collector
 *
 */
uint32_t
fbCollectorGetObservationDomain(
    const fbCollector_t  *collector);

/**
 *  Enables or disables multi-session mode for a @ref fbCollector_t associated
 *  with a UDP @ref fbListener_t.  The default setting is that multi-session
 *  mode is disabled.
 *
 *  When multi-session mode is enabled, libfixbuf invokes the `appinit`
 *  callback (@ref fbListenerAppInit_fn) set on fbListenerAlloc() when a new
 *  UDP connection occurs, allowing the callback to examine the peer address
 *  and set a context for that UDP address.  In addition, the `appfree`
 *  callback (@ref fbListenerAppFree_fn) is invoked when the @ref fbSession_t
 *  for that collector times-out.
 *
 *  Regardless of the multi-session mode setting, libfixbuf always calls the
 *  `appinit` function when a UDP listener is created, passing NULL as the
 *  peer address.
 *
 *  The caller may use fbListenerGetCollector() to get the collector given a
 *  listener.
 *
 *  @param collector     pointer to collector associated with listener.
 *  @param multi_session TRUE to enable multi-session, FALSE to disable
 */
void
fbCollectorSetUDPMultiSession(
    fbCollector_t  *collector,
    gboolean        multi_session);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _FB_PUBLIC_H_ */

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
