//
// USB Host Controller Classification + xHCI Capability Parsing
//
// Architecture-independent, standards-based helpers for the modern-input
// foundation.  This header contains only pure logic (no port I/O and no
// MMIO) so every layer can be exercised by deterministic hosted tests.
//
// Standards references:
//   - PCI Code and ID Assignment Specification (base class 0Ch, subclass 03h,
//     programming interfaces 00h/10h/20h/30h/80h/FEh).
//   - eXtensible Host Controller Interface for USB (xHCI) specification,
//     revision 1.1: capability registers (section 5.3), supported protocol
//     extended capability (section 7.2).
//
// Copyright (c) 2026 guideXOS Server
//

#pragma once

#include "kernel/types.h"

namespace kernel {
namespace usb {

// ================================================================
// PCI class / subclass / programming-interface values
// ================================================================

static const uint8_t PCI_CLASS_SERIAL_BUS = 0x0Cu;
static const uint8_t PCI_SUBCLASS_USB     = 0x03u;

static const uint8_t USB_PROGIF_UHCI    = 0x00u; // Universal Host Controller (USB 1.x)
static const uint8_t USB_PROGIF_OHCI    = 0x10u; // Open Host Controller (USB 1.x)
static const uint8_t USB_PROGIF_EHCI    = 0x20u; // Enhanced Host Controller (USB 2.0)
static const uint8_t USB_PROGIF_XHCI    = 0x30u; // eXtensible Host Controller (USB 3.x)
static const uint8_t USB_PROGIF_NOSPI   = 0x80u; // USB, no specific programming interface
static const uint8_t USB_PROGIF_DEVICE  = 0xFEu; // USB device (not a host controller)

// ================================================================
// Controller classification
// ================================================================

enum class UsbControllerType : uint8_t {
    Unknown = 0,
    UHCI,
    OHCI,
    EHCI,
    xHCI,
    NoSpecificInterface,
    UsbDevice
};

struct UsbControllerClass {
    bool             isUsbController;  // base class 0Ch and subclass 03h
    bool             isHostController; // a controller we could drive
    UsbControllerType type;
};

// Classify a PCI function from its class code bytes.  Pure: the caller is
// responsible for reading the config space.  Any value outside the USB
// serial-bus class is reported as not-a-controller rather than guessed.
inline UsbControllerClass classify_usb_controller(uint8_t baseClass,
                                                  uint8_t subClass,
                                                  uint8_t progIf)
{
    UsbControllerClass result;
    result.isUsbController  = (baseClass == PCI_CLASS_SERIAL_BUS &&
                               subClass  == PCI_SUBCLASS_USB);
    result.isHostController = false;
    result.type             = UsbControllerType::Unknown;

    if (!result.isUsbController) {
        return result;
    }

    switch (progIf) {
        case USB_PROGIF_UHCI:
            result.type = UsbControllerType::UHCI;
            result.isHostController = true;
            break;
        case USB_PROGIF_OHCI:
            result.type = UsbControllerType::OHCI;
            result.isHostController = true;
            break;
        case USB_PROGIF_EHCI:
            result.type = UsbControllerType::EHCI;
            result.isHostController = true;
            break;
        case USB_PROGIF_XHCI:
            result.type = UsbControllerType::xHCI;
            result.isHostController = true;
            break;
        case USB_PROGIF_NOSPI:
            result.type = UsbControllerType::NoSpecificInterface;
            result.isHostController = true;
            break;
        case USB_PROGIF_DEVICE:
            result.type = UsbControllerType::UsbDevice;
            result.isHostController = false;
            break;
        default:
            result.type = UsbControllerType::Unknown;
            result.isHostController = false;
            break;
    }

    return result;
}

inline const char* usb_controller_type_name(UsbControllerType type)
{
    switch (type) {
        case UsbControllerType::UHCI:                return "UHCI";
        case UsbControllerType::OHCI:                return "OHCI";
        case UsbControllerType::EHCI:                return "EHCI";
        case UsbControllerType::xHCI:                return "xHCI";
        case UsbControllerType::NoSpecificInterface: return "USB-nospi";
        case UsbControllerType::UsbDevice:           return "USB-device";
        default:                                     return "unknown";
    }
}

// ================================================================
// xHCI capability registers
//
// The caller supplies the first eight dwords of the controller MMIO BAR
// (BAR + 0x00 .. BAR + 0x1C).  Reading MMIO is the caller's responsibility;
// this parser never dereferences anything and therefore is safe to test.
// ================================================================

static const uint8_t  XHCI_CAP_DWORD_COUNT        = 8u;
static const uint32_t XHCI_MIN_CAPLENGTH          = 0x20u;
static const uint32_t XHCI_MAX_CAPLENGTH          = 0x100u;
static const uint32_t XHCI_EXT_CAP_USB_LEGACY     = 0x01u;
static const uint32_t XHCI_EXT_CAP_SUPPORTED_PROTOCOL = 0x02u;

struct XhciCapabilities {
    bool     valid;
    uint8_t  caplength;        // operational register offset from BAR (bytes)
    uint16_t hciVersion;       // BCD; e.g. 0x0100 == xHCI 1.0, 0x0110 == 1.1
    uint8_t  maxSlots;         // HCSPARAMS1[7:0]
    uint16_t maxInterrupters;  // HCSPARAMS1[18:8]
    uint8_t  maxPorts;         // HCSPARAMS1[31:24]
    uint8_t  ist;              // HCSPARAMS2[3:0]
    uint8_t  erstMax;          // HCSPARAMS2[7:4]
    uint32_t maxScratchpadBufs;// HCSPARAMS2 scratchpad buffer count
    uint32_t hccParams1;
    uint32_t hccParams2;
    uint32_t dboff;            // doorbell array offset (dwords from BAR)
    uint32_t rtsoff;           // runtime register offset (dwords from BAR)
    uint32_t xecp;             // extended capabilities offset (dwords from BAR)
    bool     ac64;             // HCCPARAMS1[0] 64-bit addressing capable
    bool     csz;              // HCCPARAMS1[2] context size (true == 64 bytes)
    bool     ppc;              // HCCPARAMS1[3] port power control
    bool     pae;              // HCCPARAMS1[8] port power always on
    // Derived byte offsets within the BAR.
    uint32_t opBase;           // == caplength
    uint32_t rtBase;           // == rtsoff * 4
    uint32_t dbBase;           // == dboff  * 4
    uint32_t xecpBase;         // == xecp   * 4
};

inline void xhci_zero_capabilities(XhciCapabilities* c)
{
    if (c == nullptr) return;
    c->valid            = false;
    c->caplength        = 0;
    c->hciVersion       = 0;
    c->maxSlots         = 0;
    c->maxInterrupters  = 0;
    c->maxPorts         = 0;
    c->ist              = 0;
    c->erstMax          = 0;
    c->maxScratchpadBufs= 0;
    c->hccParams1       = 0;
    c->hccParams2       = 0;
    c->dboff            = 0;
    c->rtsoff           = 0;
    c->xecp             = 0;
    c->ac64             = false;
    c->csz              = false;
    c->ppc              = false;
    c->pae              = false;
    c->opBase           = 0;
    c->rtBase           = 0;
    c->dbBase           = 0;
    c->xecpBase         = 0;
}

// Parse the capability register block.  `words` must be at least
// XHCI_CAP_DWORD_COUNT.  Malformed values (all-ones reads, zero
// CAPLENGTH/HCIVERSION/ports, null ring offsets) set valid == false while
// still returning the decoded fields for diagnostics.
inline XhciCapabilities parse_xhci_capabilities(const uint32_t* cap,
                                                uint32_t words)
{
    XhciCapabilities c;
    xhci_zero_capabilities(&c);

    if (cap == nullptr || words < XHCI_CAP_DWORD_COUNT) {
        return c;
    }

    const uint32_t dw0    = cap[0];
    const uint32_t hcsp1  = cap[1];
    const uint32_t hcsp2  = cap[2];

    c.caplength         = static_cast<uint8_t>(dw0 & 0xFFu);
    c.hciVersion        = static_cast<uint16_t>((dw0 >> 16) & 0xFFFFu);
    c.maxSlots          = static_cast<uint8_t>(hcsp1 & 0xFFu);
    c.maxInterrupters   = static_cast<uint16_t>((hcsp1 >> 8) & 0x7FFu);
    c.maxPorts          = static_cast<uint8_t>((hcsp1 >> 24) & 0xFFu);
    c.ist               = static_cast<uint8_t>(hcsp2 & 0x0Fu);
    c.erstMax           = static_cast<uint8_t>((hcsp2 >> 4) & 0x0Fu);
    c.maxScratchpadBufs = static_cast<uint32_t>(
        (((hcsp2 >> 21) & 0x1Fu) << 5) | ((hcsp2 >> 27) & 0x1Fu));

    c.hccParams1 = cap[4];
    c.dboff      = cap[5];
    c.rtsoff     = cap[6];
    c.hccParams2 = cap[7];

    c.ac64 = (c.hccParams1 & (1u << 0)) != 0u;
    c.csz  = (c.hccParams1 & (1u << 2)) != 0u;
    c.ppc  = (c.hccParams1 & (1u << 3)) != 0u;
    c.pae  = (c.hccParams1 & (1u << 8)) != 0u;
    c.xecp = (c.hccParams1 >> 16) & 0xFFFFu;

    c.opBase   = c.caplength;
    c.rtBase   = c.rtsoff * 4u;
    c.dbBase   = c.dboff * 4u;
    c.xecpBase = c.xecp * 4u;

    bool ok = true;
    if (c.caplength < XHCI_MIN_CAPLENGTH || c.caplength > XHCI_MAX_CAPLENGTH) ok = false;
    if (c.hciVersion == 0x0000u || c.hciVersion == 0xFFFFu) ok = false;
    if (c.maxSlots == 0u) ok = false;
    if (c.maxPorts == 0u) ok = false;
    if (c.rtsoff == 0u || c.dboff == 0u) ok = false;
    if (c.rtsoff == 0xFFFFu || c.dboff == 0xFFFFu) ok = false;

    c.valid = ok;
    return c;
}

// ================================================================
// xHCI supported-protocol extended capabilities
// ================================================================

static const uint32_t XHCI_MAX_PROTOCOLS      = 4u;
static const uint32_t XHCI_EXT_CAP_MAX_DWORDS = 64u;
static const uint32_t XHCI_EXT_CAP_MAX_WALK   = 64u;

struct XhciProtocol {
    uint8_t majorRevision;   // e.g. 3 for USB 3.x, 2 for USB 2.0
    uint8_t minorRevision;   // e.g. 0
    uint8_t portOffset;      // first compatible port number (1-based)
    uint8_t portCount;       // number of compatible ports
    uint8_t protocolDefined; // protocol-defined flags
    uint8_t speedIdCount;    // PSIC (protocol speed ID count)
    char    name[5];         // 4-char packed ASCII name + NUL
};

inline void xhci_zero_protocol(XhciProtocol* p)
{
    if (p == nullptr) return;
    p->majorRevision   = 0;
    p->minorRevision   = 0;
    p->portOffset      = 0;
    p->portCount       = 0;
    p->protocolDefined = 0;
    p->speedIdCount    = 0;
    p->name[0] = '\0';
    p->name[1] = '\0';
    p->name[2] = '\0';
    p->name[3] = '\0';
    p->name[4] = '\0';
}

// Walk the linked list of extended capabilities starting at `xecp` (the
// first extended-capability dword, i.e. BAR + xecp * 4) and copy every USB
// Supported Protocol entry into `out`.  Bounded against malformed next
// pointers and short buffers: never reads past `words` and never loops.
inline uint32_t parse_xhci_supported_protocols(const uint32_t* xecp,
                                               uint32_t words,
                                               XhciProtocol* out,
                                               uint32_t maxOut)
{
    if (xecp == nullptr || out == nullptr || words == 0u || maxOut == 0u) {
        return 0u;
    }

    uint32_t found       = 0u;
    uint32_t index       = 0u; // dword index relative to xecp
    uint32_t iterations  = 0u;

    while (index < words && iterations < XHCI_EXT_CAP_MAX_WALK) {
        ++iterations;

        const uint32_t dw0   = xecp[index];
        const uint32_t capId = dw0 & 0xFFu;
        const uint32_t next  = (dw0 >> 8) & 0xFFu;

        if (capId == 0u) {
            break; // end of extended capability list
        }

        if (capId == XHCI_EXT_CAP_SUPPORTED_PROTOCOL &&
            (index + 2u) < words && found < maxOut) {
            XhciProtocol p;
            xhci_zero_protocol(&p);

            p.majorRevision = static_cast<uint8_t>((dw0 >> 24) & 0xFFu);
            p.minorRevision = static_cast<uint8_t>((dw0 >> 16) & 0xFFu);

            const uint32_t name = xecp[index + 1u];
            p.name[0] = static_cast<char>(name & 0xFFu);
            p.name[1] = static_cast<char>((name >> 8) & 0xFFu);
            p.name[2] = static_cast<char>((name >> 16) & 0xFFu);
            p.name[3] = static_cast<char>((name >> 24) & 0xFFu);
            p.name[4] = '\0';

            const uint32_t dw2 = xecp[index + 2u];
            p.portOffset      = static_cast<uint8_t>(dw2 & 0xFFu);
            p.portCount       = static_cast<uint8_t>((dw2 >> 8) & 0xFFu);
            p.protocolDefined = static_cast<uint8_t>((dw2 >> 16) & 0xFFFu);
            p.speedIdCount    = static_cast<uint8_t>((dw2 >> 28) & 0x0Fu);

            out[found] = p;
            ++found;
        }

        if (next == 0u) {
            break; // last capability
        }

        const uint32_t nextIndex = index + next;
        if (nextIndex <= index) {
            break; // malformed (non-advancing) next pointer
        }
        index = nextIndex;
    }

    return found;
}

struct XhciControllerInfo {
    XhciCapabilities caps;
    uint32_t         protocolCount;
    XhciProtocol     protocols[XHCI_MAX_PROTOCOLS];
};

// Combine a capability-register snapshot with a separately-read extended
// capability snapshot.  Returns false when the capability block is not a
// usable xHCI controller.
inline bool parse_xhci_controller(const uint32_t* capWords,
                                  uint32_t capWordCount,
                                  const uint32_t* xecpWords,
                                  uint32_t xecpWordCount,
                                  XhciControllerInfo* out)
{
    if (out == nullptr) {
        return false;
    }

    out->caps          = parse_xhci_capabilities(capWords, capWordCount);
    out->protocolCount = 0u;
    for (uint32_t i = 0u; i < XHCI_MAX_PROTOCOLS; ++i) {
        xhci_zero_protocol(&out->protocols[i]);
    }

    if (!out->caps.valid) {
        return false;
    }

    if (out->caps.xecp != 0u && xecpWords != nullptr && xecpWordCount != 0u) {
        out->protocolCount = parse_xhci_supported_protocols(
            xecpWords, xecpWordCount, out->protocols, XHCI_MAX_PROTOCOLS);
    }

    return true;
}

// ================================================================
// Diagnostic formatting (pure; used by the kernel reporter and tests)
// ================================================================

namespace detail {

// The append helpers reserve the final byte so the caller can always
// NUL-terminate; this also lets the compiler prove the writes stay in bounds.
inline uint32_t fmt_append_str(char* out, uint32_t cap, uint32_t pos,
                               const char* s)
{
    if (s == nullptr) return pos;
    for (uint32_t i = 0u; s[i] != '\0' && (pos + 1u) < cap; ++i) {
        out[pos++] = s[i];
    }
    return pos;
}

inline uint32_t fmt_append_char(char* out, uint32_t cap, uint32_t pos, char c)
{
    if ((pos + 1u) < cap) out[pos++] = c;
    return pos;
}

inline uint32_t fmt_append_hex(char* out, uint32_t cap, uint32_t pos,
                               uint32_t value, uint32_t digits)
{
    const char* hex = "0123456789abcdef";
    for (uint32_t i = 0u; i < digits; ++i) {
        const uint32_t shift = (digits - 1u - i) * 4u;
        if ((pos + 1u) < cap) out[pos++] = hex[(value >> shift) & 0xFu];
    }
    return pos;
}

inline uint32_t fmt_append_dec(char* out, uint32_t cap, uint32_t pos,
                               uint32_t value)
{
    char tmp[10];
    uint32_t n = 0u;
    if (value == 0u) {
        tmp[n++] = '0';
    }
    while (value > 0u) {
        tmp[n++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    }
    while (n > 0u) {
        pos = fmt_append_char(out, cap, pos, tmp[--n]);
    }
    return pos;
}

inline void fmt_terminate(char* out, uint32_t cap, uint32_t pos)
{
    if (cap == 0u) return;
    out[pos < cap ? pos : (cap - 1u)] = '\0';
}

} // namespace detail

// Render the decoded capability fields, e.g.
//   "caplen=0x20 hciver=0x0110 slots=64 intrs=8 ports=16 dboff=0x2000 ..."
inline uint32_t format_xhci_capabilities(const XhciCapabilities& c,
                                         char* out, uint32_t cap)
{
    if (out == nullptr || cap == 0u) return 0u;
    uint32_t pos = 0u;

    pos = detail::fmt_append_str(out, cap, pos, "caplen=0x");
    pos = detail::fmt_append_hex(out, cap, pos, c.caplength, 2u);
    pos = detail::fmt_append_str(out, cap, pos, " hciver=0x");
    pos = detail::fmt_append_hex(out, cap, pos, c.hciVersion, 4u);
    pos = detail::fmt_append_str(out, cap, pos, " slots=");
    pos = detail::fmt_append_dec(out, cap, pos, c.maxSlots);
    pos = detail::fmt_append_str(out, cap, pos, " intrs=");
    pos = detail::fmt_append_dec(out, cap, pos, c.maxInterrupters);
    pos = detail::fmt_append_str(out, cap, pos, " ports=");
    pos = detail::fmt_append_dec(out, cap, pos, c.maxPorts);
    pos = detail::fmt_append_str(out, cap, pos, " dboff=0x");
    pos = detail::fmt_append_hex(out, cap, pos, c.dboff, 4u);
    pos = detail::fmt_append_str(out, cap, pos, " rtsoff=0x");
    pos = detail::fmt_append_hex(out, cap, pos, c.rtsoff, 4u);
    pos = detail::fmt_append_str(out, cap, pos, " xecp=0x");
    pos = detail::fmt_append_hex(out, cap, pos, c.xecp, 4u);
    pos = detail::fmt_append_str(out, cap, pos, c.ac64 ? " ac64" : " ac32");
    pos = detail::fmt_append_str(out, cap, pos, c.csz ? " csz64" : " csz32");
    pos = detail::fmt_append_str(out, cap, pos, c.valid ? " valid" : " invalid");

    detail::fmt_terminate(out, cap, pos);
    return pos;
}

// Render one supported-protocol entry, e.g.
//   "name=USB3 major=3 minor=0 ports=1-8 psic=4"
inline uint32_t format_xhci_protocol(const XhciProtocol& p,
                                     char* out, uint32_t cap)
{
    if (out == nullptr || cap == 0u) return 0u;
    uint32_t pos = 0u;

    pos = detail::fmt_append_str(out, cap, pos, "name=");
    for (uint32_t i = 0u; i < 4u && p.name[i] != '\0'; ++i) {
        pos = detail::fmt_append_char(out, cap, pos, p.name[i]);
    }
    pos = detail::fmt_append_str(out, cap, pos, " major=");
    pos = detail::fmt_append_dec(out, cap, pos, p.majorRevision);
    pos = detail::fmt_append_str(out, cap, pos, " minor=");
    pos = detail::fmt_append_dec(out, cap, pos, p.minorRevision);
    pos = detail::fmt_append_str(out, cap, pos, " ports=");
    pos = detail::fmt_append_dec(out, cap, pos, p.portOffset);
    if (p.portCount > 0u) {
        pos = detail::fmt_append_char(out, cap, pos, '-');
        pos = detail::fmt_append_dec(
            out, cap, pos,
            static_cast<uint32_t>(p.portOffset) + p.portCount - 1u);
    }
    pos = detail::fmt_append_str(out, cap, pos, " psic=");
    pos = detail::fmt_append_dec(out, cap, pos, p.speedIdCount);

    detail::fmt_terminate(out, cap, pos);
    return pos;
}

// ================================================================
// Kernel diagnostics (implemented in usb_controller.cpp)
//
// Enumerates PCI USB host controllers using the platform's read-only PCI
// config mechanism, classifies each one, and reports BAR/MMIO capability
// milestones to the serial console.  Returns the number of USB controllers
// discovered (0 on architectures without PCI config access).
// ================================================================

namespace diagnostics {

uint32_t scan_host_controllers();

} // namespace diagnostics

} // namespace usb
} // namespace kernel
