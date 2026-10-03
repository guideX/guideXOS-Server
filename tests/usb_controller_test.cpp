// Hosted tests for the modern-input USB controller foundation.
//
// Covers the architecture-independent layer added by INPUT1:
//   - PCI USB host-controller classification from class/subclass/prog-if
//   - xHCI capability-register parsing (valid + malformed/boundary)
//   - xHCI supported-protocol extended-capability parsing (bounded walk)
//   - combined controller snapshot parsing
//   - diagnostic formatting, including small-buffer truncation
//
// Build (see scripts/run-usb-controller-test.ps1):
//   g++ -std=c++17 -Wall -Wextra -O2 -iquote kernel/core/include
//       tests/usb_controller_test.cpp -o out/.../usb_controller_test.exe
//
// Copyright (c) 2026 guideXOS Server
//

#include "kernel/usb_controller.h"

#include <cstring>
#include <iostream>

namespace {

int g_failures = 0;

bool expect(bool value, const char* label)
{
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    }
    return value;
}

bool contains(const char* haystack, const char* needle)
{
    return haystack != nullptr && needle != nullptr &&
           std::strstr(haystack, needle) != nullptr;
}

using kernel::usb::UsbControllerType;

void test_classification()
{
    using kernel::usb::classify_usb_controller;

    {
        const auto c = classify_usb_controller(0x0C, 0x03, 0x00);
        expect(c.isUsbController && c.isHostController &&
               c.type == UsbControllerType::UHCI, "classify UHCI");
    }
    {
        const auto c = classify_usb_controller(0x0C, 0x03, 0x10);
        expect(c.isUsbController && c.isHostController &&
               c.type == UsbControllerType::OHCI, "classify OHCI");
    }
    {
        const auto c = classify_usb_controller(0x0C, 0x03, 0x20);
        expect(c.isUsbController && c.isHostController &&
               c.type == UsbControllerType::EHCI, "classify EHCI");
    }
    {
        const auto c = classify_usb_controller(0x0C, 0x03, 0x30);
        expect(c.isUsbController && c.isHostController &&
               c.type == UsbControllerType::xHCI, "classify xHCI");
    }
    {
        const auto c = classify_usb_controller(0x0C, 0x03, 0x80);
        expect(c.isUsbController && c.isHostController &&
               c.type == UsbControllerType::NoSpecificInterface,
               "classify USB no-specific-interface");
    }
    {
        const auto c = classify_usb_controller(0x0C, 0x03, 0xFE);
        expect(c.isUsbController && !c.isHostController &&
               c.type == UsbControllerType::UsbDevice,
               "classify USB device (not host controller)");
    }
    {
        const auto c = classify_usb_controller(0x0C, 0x03, 0xFF);
        expect(c.isUsbController && !c.isHostController &&
               c.type == UsbControllerType::Unknown,
               "classify reserved prog-if");
    }
    {
        const auto c = classify_usb_controller(0x01, 0x03, 0x30);
        expect(!c.isUsbController && !c.isHostController,
               "reject non-serial-bus base class");
    }
    {
        const auto c = classify_usb_controller(0x0C, 0x00, 0x30);
        expect(!c.isUsbController, "reject wrong subclass");
    }

    expect(std::strcmp(kernel::usb::usb_controller_type_name(UsbControllerType::xHCI),
                       "xHCI") == 0, "type name xHCI");
    expect(std::strcmp(kernel::usb::usb_controller_type_name(UsbControllerType::Unknown),
                       "unknown") == 0, "type name unknown");
}

// Build a plausible xHCI capability block for tests.
void build_cap_block(uint32_t* cap, uint32_t caplength, uint32_t hciVersion,
                     uint32_t maxSlots, uint32_t maxInterrupters,
                     uint32_t maxPorts, uint32_t hcsp2, uint32_t hccParams1,
                     uint32_t dboff, uint32_t rtsoff)
{
    cap[0] = (caplength & 0xFFu) | ((hciVersion & 0xFFFFu) << 16);
    cap[1] = (maxSlots & 0xFFu) | ((maxInterrupters & 0x7FFu) << 8) |
             ((maxPorts & 0xFFu) << 24);
    cap[2] = hcsp2;
    cap[3] = 0u;
    cap[4] = hccParams1;
    cap[5] = dboff;
    cap[6] = rtsoff;
    cap[7] = 0u;
}

void test_capabilities_valid()
{
    using namespace kernel::usb;

    uint32_t cap[XHCI_CAP_DWORD_COUNT];
    // ac64 | csz | ppc, xECP = 0x100 dwords
    const uint32_t hcc1 = (1u << 0) | (1u << 2) | (1u << 3) | (0x100u << 16);
    build_cap_block(cap, 0x20u, 0x0110u, 64u, 8u, 16u, 0x10u, hcc1,
                    0x2000u, 0x1000u);

    const XhciCapabilities c = parse_xhci_capabilities(cap, XHCI_CAP_DWORD_COUNT);
    expect(c.valid, "caps valid");
    expect(c.caplength == 0x20u, "caps caplength");
    expect(c.hciVersion == 0x0110u, "caps hciVersion");
    expect(c.maxSlots == 64u, "caps maxSlots");
    expect(c.maxInterrupters == 8u, "caps maxInterrupters");
    expect(c.maxPorts == 16u, "caps maxPorts");
    expect(c.ist == 0u, "caps ist");
    expect(c.erstMax == 1u, "caps erstMax");
    expect(c.xecp == 0x100u, "caps xecp");
    expect(c.ac64 && c.csz && c.ppc && !c.pae, "caps hcc flags");
    expect(c.opBase == 0x20u, "caps opBase");
    expect(c.rtBase == 0x4000u, "caps rtBase");
    expect(c.dbBase == 0x8000u, "caps dbBase");
    expect(c.xecpBase == 0x400u, "caps xecpBase");
}

void test_capabilities_boundaries()
{
    using namespace kernel::usb;

    {
        const XhciCapabilities c = parse_xhci_capabilities(nullptr, 8u);
        expect(!c.valid, "caps null invalid");
    }
    {
        uint32_t cap[XHCI_CAP_DWORD_COUNT] = {0};
        const XhciCapabilities c =
            parse_xhci_capabilities(cap, XHCI_CAP_DWORD_COUNT - 1u);
        expect(!c.valid, "caps short buffer invalid");
    }
    {
        // hciVersion 0xFFFF (typical all-ones read)
        uint32_t cap[XHCI_CAP_DWORD_COUNT];
        build_cap_block(cap, 0x20u, 0xFFFFu, 64u, 8u, 16u, 0u, 0u,
                        0x2000u, 0x1000u);
        expect(!parse_xhci_capabilities(cap, 8u).valid, "caps 0xFFFF version invalid");
    }
    {
        // CAPLENGTH below the xHCI minimum
        uint32_t cap[XHCI_CAP_DWORD_COUNT];
        build_cap_block(cap, 0x10u, 0x0100u, 64u, 8u, 16u, 0u, 0u,
                        0x2000u, 0x1000u);
        expect(!parse_xhci_capabilities(cap, 8u).valid, "caps short caplength invalid");
    }
    {
        // Zero ports is impossible for a real controller.
        uint32_t cap[XHCI_CAP_DWORD_COUNT];
        build_cap_block(cap, 0x20u, 0x0100u, 64u, 8u, 0u, 0u, 0u,
                        0x2000u, 0x1000u);
        expect(!parse_xhci_capabilities(cap, 8u).valid, "caps zero ports invalid");
    }
    {
        // Zero ring offsets indicate a bad read.
        uint32_t cap[XHCI_CAP_DWORD_COUNT];
        build_cap_block(cap, 0x20u, 0x0100u, 64u, 8u, 16u, 0u, 0u,
                        0u, 0x1000u);
        expect(!parse_xhci_capabilities(cap, 8u).valid, "caps zero dboff invalid");
    }
    {
        // Scratchpad buffer count is split hi/lo across HCSPARAMS2.
        uint32_t cap[XHCI_CAP_DWORD_COUNT];
        const uint32_t hcsp2 = (3u << 27) | (2u << 21); // lo=3, hi=2 -> 67
        build_cap_block(cap, 0x20u, 0x0100u, 64u, 8u, 16u, hcsp2, 0u,
                        0x2000u, 0x1000u);
        const XhciCapabilities c = parse_xhci_capabilities(cap, 8u);
        expect(c.valid && c.maxScratchpadBufs == 67u, "caps scratchpad hi/lo");
    }
}

void test_protocols()
{
    using namespace kernel::usb;

    uint32_t ext[XHCI_EXT_CAP_MAX_DWORDS];
    for (uint32_t i = 0; i < XHCI_EXT_CAP_MAX_DWORDS; ++i) ext[i] = 0u;

    // USB 3.0 supported protocol at index 0, next -> index 4
    ext[0] = 0x02u | (4u << 8) | (0u << 16) | (3u << 24);
    ext[1] = 0x33425355u; // "USB3"
    ext[2] = 0x01u | (8u << 8) | (4u << 28);
    // USB 2.0 supported protocol at index 4, last entry
    ext[4] = 0x02u | (0u << 8) | (0u << 16) | (2u << 24);
    ext[5] = 0x32425355u; // "USB2"
    ext[6] = 0x09u | (8u << 8);

    XhciProtocol p[XHCI_MAX_PROTOCOLS];
    const uint32_t n = parse_xhci_supported_protocols(
        ext, XHCI_EXT_CAP_MAX_DWORDS, p, XHCI_MAX_PROTOCOLS);

    expect(n == 2u, "protocols count");
    if (n == 2u) {
        expect(p[0].majorRevision == 3u && p[0].minorRevision == 0u,
               "protocol0 revision");
        expect(p[0].portOffset == 1u && p[0].portCount == 8u,
               "protocol0 ports");
        expect(p[0].speedIdCount == 4u, "protocol0 psic");
        expect(std::strcmp(p[0].name, "USB3") == 0, "protocol0 name");
        expect(p[1].majorRevision == 2u, "protocol1 revision");
        expect(p[1].portOffset == 9u && p[1].portCount == 8u,
               "protocol1 ports");
        expect(p[1].speedIdCount == 0u, "protocol1 psic");
        expect(std::strcmp(p[1].name, "USB2") == 0, "protocol1 name");
    }
}

void test_protocols_malformed()
{
    using namespace kernel::usb;

    {
        XhciProtocol p[XHCI_MAX_PROTOCOLS];
        expect(parse_xhci_supported_protocols(nullptr, 8u, p, XHCI_MAX_PROTOCOLS) == 0u,
               "protocols null buffer");
        uint32_t ext[8] = {0};
        expect(parse_xhci_supported_protocols(ext, 8u, nullptr, XHCI_MAX_PROTOCOLS) == 0u,
               "protocols null output");
        expect(parse_xhci_supported_protocols(ext, 0u, p, XHCI_MAX_PROTOCOLS) == 0u,
               "protocols zero words");
        expect(parse_xhci_supported_protocols(ext, 8u, p, 0u) == 0u,
               "protocols zero capacity");
    }
    {
        // CAP ID 0 terminates immediately.
        uint32_t ext[8] = {0};
        XhciProtocol p[XHCI_MAX_PROTOCOLS];
        expect(parse_xhci_supported_protocols(ext, 8u, p, XHCI_MAX_PROTOCOLS) == 0u,
               "protocols terminator");
    }
    {
        // Protocol entry with no room for its DWORD1/DWORD2 payload.
        uint32_t ext[8] = {0};
        ext[0] = 0x02u | (0u << 8);
        XhciProtocol p[XHCI_MAX_PROTOCOLS];
        expect(parse_xhci_supported_protocols(ext, 1u, p, XHCI_MAX_PROTOCOLS) == 0u,
               "protocols truncated entry");
    }
    {
        // Malformed next pointer that runs off the buffer must terminate.
        uint32_t ext[8] = {0};
        ext[0] = 0x02u | (200u << 8) | (3u << 24);
        ext[1] = 0x33425355u;
        ext[2] = 0x01u | (8u << 8);
        XhciProtocol p[XHCI_MAX_PROTOCOLS];
        const uint32_t n = parse_xhci_supported_protocols(
            ext, 8u, p, XHCI_MAX_PROTOCOLS);
        expect(n == 1u, "protocols off-buffer next terminates");
    }
    {
        // More protocols than capacity: only capacity entries are written.
        uint32_t ext[XHCI_EXT_CAP_MAX_DWORDS];
        for (uint32_t i = 0; i < XHCI_EXT_CAP_MAX_DWORDS; ++i) ext[i] = 0u;
        for (uint32_t k = 0; k < 3u; ++k) {
            const uint32_t idx = k * 3u;
            ext[idx] = 0x02u | (3u << 8) | (2u << 24);
            ext[idx + 1] = 0x33425355u;
            ext[idx + 2] = 0x01u | (8u << 8);
        }
        ext[6] = 0u; // terminate after the third
        XhciProtocol p[2];
        const uint32_t n = parse_xhci_supported_protocols(ext, 64u, p, 2u);
        expect(n == 2u, "protocols capacity clamp");
    }
}

void test_combined_snapshot()
{
    using namespace kernel::usb;

    uint32_t cap[XHCI_CAP_DWORD_COUNT];
    const uint32_t hcc1 = (1u << 0) | (0x100u << 16);
    build_cap_block(cap, 0x20u, 0x0110u, 64u, 8u, 16u, 0u, hcc1,
                    0x2000u, 0x1000u);

    uint32_t ext[XHCI_EXT_CAP_MAX_DWORDS];
    for (uint32_t i = 0; i < XHCI_EXT_CAP_MAX_DWORDS; ++i) ext[i] = 0u;
    ext[0] = 0x02u | (3u << 8) | (3u << 24);
    ext[1] = 0x33425355u;
    ext[2] = 0x01u | (8u << 8);

    XhciControllerInfo info;
    const bool ok = parse_xhci_controller(cap, XHCI_CAP_DWORD_COUNT,
                                          ext, XHCI_EXT_CAP_MAX_DWORDS, &info);
    expect(ok, "combined snapshot valid");
    expect(info.protocolCount == 1u, "combined snapshot protocol count");

    // Invalid capabilities still zero the protocol list and return false.
    uint32_t badCap[XHCI_CAP_DWORD_COUNT] = {0};
    XhciControllerInfo bad;
    const bool badOk = parse_xhci_controller(badCap, XHCI_CAP_DWORD_COUNT,
                                             ext, XHCI_EXT_CAP_MAX_DWORDS, &bad);
    expect(!badOk && bad.protocolCount == 0u, "combined invalid caps");

    // xECP == 0 means no protocols, but capabilities remain valid.
    uint32_t noExtCap[XHCI_CAP_DWORD_COUNT];
    build_cap_block(noExtCap, 0x20u, 0x0100u, 32u, 4u, 8u, 0u, 0u,
                    0x1000u, 0x0800u);
    XhciControllerInfo noExt;
    expect(parse_xhci_controller(noExtCap, XHCI_CAP_DWORD_COUNT,
                                 nullptr, 0u, &noExt) &&
           noExt.protocolCount == 0u, "combined no extended caps");
}

void test_formatting()
{
    using namespace kernel::usb;

    uint32_t cap[XHCI_CAP_DWORD_COUNT];
    const uint32_t hcc1 = (1u << 0) | (1u << 2) | (1u << 3) | (0x100u << 16);
    build_cap_block(cap, 0x20u, 0x0110u, 64u, 8u, 16u, 0u, hcc1,
                    0x2000u, 0x1000u);
    const XhciCapabilities c = parse_xhci_capabilities(cap, XHCI_CAP_DWORD_COUNT);

    char line[192];
    format_xhci_capabilities(c, line, sizeof(line));
    expect(contains(line, "caplen=0x20"), "format caplen");
    expect(contains(line, "hciver=0x0110"), "format hciver");
    expect(contains(line, "slots=64"), "format slots");
    expect(contains(line, "ports=16"), "format ports");
    expect(contains(line, "dboff=0x2000"), "format dboff");
    expect(contains(line, "rtsoff=0x1000"), "format rtsoff");
    expect(contains(line, "xecp=0x0100"), "format xecp");
    expect(contains(line, "valid"), "format valid marker");

    XhciProtocol p;
    xhci_zero_protocol(&p);
    p.majorRevision = 3u;
    p.minorRevision = 0u;
    p.portOffset = 1u;
    p.portCount = 8u;
    p.speedIdCount = 4u;
    std::strcpy(p.name, "USB3");

    char proto[96];
    format_xhci_protocol(p, proto, sizeof(proto));
    expect(contains(proto, "name=USB3"), "format proto name");
    expect(contains(proto, "major=3"), "format proto major");
    expect(contains(proto, "ports=1-8"), "format proto port range");
    expect(contains(proto, "psic=4"), "format proto psic");

    // Small-buffer truncation must stay in bounds and terminate.
    char tiny[8];
    for (uint32_t i = 0; i < sizeof(tiny); ++i) tiny[i] = 'X';
    format_xhci_capabilities(c, tiny, sizeof(tiny));
    expect(tiny[sizeof(tiny) - 1] == '\0', "format truncates and terminates");
}

} // namespace

int main()
{
    test_classification();
    test_capabilities_valid();
    test_capabilities_boundaries();
    test_protocols();
    test_protocols_malformed();
    test_combined_snapshot();
    test_formatting();

    if (g_failures == 0) {
        std::cout << "USB controller foundation test PASS\n";
        return 0;
    }
    std::cerr << g_failures << " USB controller foundation test failure(s)\n";
    return 1;
}
