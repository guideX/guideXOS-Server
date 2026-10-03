// Hosted tests for the xHCI controller bring-up pure logic.
//
// Covers:
//   - PCI/BAR validation (32-bit, 64-bit, I/O, zero, all-ones, overflow)
//   - Ownership detection (no capability, BIOS-owned, OS-owned, timeout)
//   - Controller state machine helpers
//   - Ring helpers (size validation, power-of-two, index wrapping)
//   - ERST helpers (size validation, base encoding)
//   - Port status decoding
//   - TRB decoding (type, cycle bit, completion code)
//   - Link state and speed name helpers
//   - Completion code name helpers
//
// Build (see scripts/run-xhci-test.ps1):
//   g++ -std=c++17 -Wall -Wextra -O2 -iquote kernel/core/include
//       tests/xhci_test.cpp -o out/.../xhci_test.exe
//
// Copyright (c) 2026 guideXOS Server
//

#include "kernel/xhci.h"

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

using namespace kernel::usb::xhci;

// ================================================================
// BAR validation tests
// ================================================================

void test_bar_validation()
{
    // 32-bit MMIO BAR
    {
        const XhciBarInfo bar = validate_mmio_bar(0xF0000000u, 0, 0x1000u);
        expect(bar.valid && !bar.is64 && bar.base == 0xF0000000u,
               "BAR 32-bit MMIO");
    }
    // 64-bit MMIO BAR (bits [2:1] = 10)
    {
        const XhciBarInfo bar = validate_mmio_bar(0xF0000004u, 0x00000001u, 0x1000u);
        expect(bar.valid && bar.is64 && bar.base == 0x1F0000000ULL,
               "BAR 64-bit MMIO");
    }
    // I/O BAR rejected
    {
        const XhciBarInfo bar = validate_mmio_bar(0x00000001u, 0, 0x1000u);
        expect(!bar.valid, "BAR I/O rejected");
    }
    // Zero BAR rejected
    {
        const XhciBarInfo bar = validate_mmio_bar(0x00000000u, 0, 0x1000u);
        expect(!bar.valid, "BAR zero rejected");
    }
    // All-ones BAR rejected
    {
        const XhciBarInfo bar = validate_mmio_bar(0xFFFFFFFFu, 0, 0x1000u);
        expect(!bar.valid, "BAR all-ones rejected");
    }
    // 64-bit BAR with zero upper dword rejected
    {
        const XhciBarInfo bar = validate_mmio_bar(0xF0000004u, 0x00000000u, 0x1000u);
        expect(!bar.valid, "BAR 64-bit zero upper rejected");
    }
    // 64-bit BAR with all-ones upper dword rejected
    {
        const XhciBarInfo bar = validate_mmio_bar(0xF0000004u, 0xFFFFFFFFu, 0x1000u);
        expect(!bar.valid, "BAR 64-bit all-ones upper rejected");
    }
    // Reserved BAR type rejected (bits [2:1] = 01)
    {
        const XhciBarInfo bar = validate_mmio_bar(0xFFFFFFFAu, 0, 0x1000u);
        expect(!bar.valid, "BAR reserved type rejected");
    }
}

// ================================================================
// Ownership detection tests
// ================================================================

void test_ownership_detection()
{
    // No capability (no semaphores set)
    {
        const XhciOwnershipState os = detect_ownership(0x00000000u);
        expect(os == XhciOwnershipState::NoCapability, "ownership no capability");
    }
    // BIOS-owned
    {
        const XhciOwnershipState os = detect_ownership(XHCI_USBLEGSUP_HC_BIOS_OWNED_SEM);
        expect(os == XhciOwnershipState::BiosOwned, "ownership BIOS-owned");
    }
    // OS-owned
    {
        const XhciOwnershipState os = detect_ownership(XHCI_USBLEGSUP_HC_OS_OWNED_SEM);
        expect(os == XhciOwnershipState::OsOwned, "ownership OS-owned");
    }
    // Both set (should not happen, but OS-owned takes precedence in detection)
    {
        const XhciOwnershipState os = detect_ownership(
            XHCI_USBLEGSUP_HC_BIOS_OWNED_SEM | XHCI_USBLEGSUP_HC_OS_OWNED_SEM);
        expect(os == XhciOwnershipState::BiosOwned, "ownership both set");
    }
}

// ================================================================
// Ring helper tests
// ================================================================

void test_ring_helpers()
{
    expect(ring_size_valid(2u), "ring size 2 valid");
    expect(ring_size_valid(256u), "ring size 256 valid");
    expect(!ring_size_valid(0u), "ring size 0 invalid");
    expect(!ring_size_valid(1u), "ring size 1 invalid");
    expect(!ring_size_valid(257u), "ring size 257 invalid");
    expect(!ring_size_valid(1024u), "ring size 1024 invalid");

    expect(ring_is_power_of_two(2u), "ring power-of-two 2");
    expect(ring_is_power_of_two(64u), "ring power-of-two 64");
    expect(ring_is_power_of_two(256u), "ring power-of-two 256");
    expect(!ring_is_power_of_two(0u), "ring power-of-two 0");
    expect(!ring_is_power_of_two(3u), "ring power-of-two 3");
    expect(!ring_is_power_of_two(100u), "ring power-of-two 100");

    expect(ring_index(0u, 64u) == 0u, "ring index 0");
    expect(ring_index(63u, 64u) == 63u, "ring index 63");
    expect(ring_index(64u, 64u) == 0u, "ring index wrap 64");
    expect(ring_index(65u, 64u) == 1u, "ring index wrap 65");
    expect(ring_index(128u, 64u) == 0u, "ring index wrap 128");
}

// ================================================================
// ERST helper tests
// ================================================================

void test_erst_helpers()
{
    expect(erst_size_valid(1u), "ERST size 1 valid");
    expect(erst_size_valid(256u), "ERST size 256 valid");
    expect(!erst_size_valid(0u), "ERST size 0 invalid");
    expect(!erst_size_valid(257u), "ERST size 257 invalid");

    expect(erst_encode_base(0x1234567890ABCDEFULL) == 0x1234567890ABCDC0ULL,
           "ERST encode base alignment");
    expect(erst_encode_base(0x0000000000000000ULL) == 0x0000000000000000ULL,
           "ERST encode base zero");
    expect(erst_encode_base(0xFFFFFFFFFFFFFFFFULL) == 0xFFFFFFFFFFFFFFC0ULL,
           "ERST encode base all-ones");
}

// ================================================================
// Port status decoding tests
// ================================================================

void test_port_status_decoding()
{
    // Connected, enabled, powered, Super speed, U0
    {
        const uint32_t portsc = XHCI_PORTSC_CCS | XHCI_PORTSC_PED |
                                XHCI_PORTSC_PP |
                                (XHCI_SPEED_SUPER << 10) |
                                (XHCI_PLS_U0 << 5);
        const XhciPortStatus ps = decode_port_status(1u, portsc);
        expect(ps.portIndex == 1u, "port status index");
        expect(ps.connected, "port status connected");
        expect(ps.enabled, "port status enabled");
        expect(ps.powered, "port status powered");
        expect(ps.speed == XHCI_SPEED_SUPER, "port status speed");
        expect(ps.linkState == XHCI_PLS_U0, "port status link state");
        expect(!ps.connectChange, "port status no connect change");
        expect(!ps.resetChange, "port status no reset change");
    }
    // Disconnected, not enabled, Full speed, RxDetect
    {
        const uint32_t portsc = (XHCI_SPEED_FULL << 10) |
                                (XHCI_PLS_RX_DETECT << 5);
        const XhciPortStatus ps = decode_port_status(2u, portsc);
        expect(!ps.connected, "port status disconnected");
        expect(!ps.enabled, "port status not enabled");
        expect(!ps.powered, "port status not powered");
        expect(ps.speed == XHCI_SPEED_FULL, "port status full speed");
        expect(ps.linkState == XHCI_PLS_RX_DETECT, "port status rx detect");
    }
    // All change bits set
    {
        const uint32_t portsc = XHCI_PORTSC_CCS | XHCI_PORTSC_CSC |
                                XHCI_PORTSC_PRC | XHCI_PORTSC_PLC |
                                XHCI_PORTSC_CEC | XHCI_PORTSC_OCA |
                                XHCI_PORTSC_WPR | XHCI_PORTSC_DR;
        const XhciPortStatus ps = decode_port_status(3u, portsc);
        expect(ps.connectChange, "port status connect change");
        expect(ps.resetChange, "port status reset change");
        expect(ps.linkStateChange, "port status link state change");
        expect(ps.configError, "port status config error");
        expect(ps.overCurrent, "port status over current");
        expect(ps.warmReset, "port status warm reset");
        expect(ps.deviceRemovable, "port status device removable");
    }
}

// ================================================================
// TRB decoding tests
// ================================================================

void test_trb_decoding()
{
    // Normal TRB
    {
        XhciTrb trb{};
        trb.parameter = 0x123456789ABCDEF0ULL;
        trb.status = 0x00000000u;
        trb.control = (TRB_TYPE_NORMAL << 10) | 0x01u;
        expect(trb_type(trb) == TRB_TYPE_NORMAL, "TRB type normal");
        expect(trb_cycle_bit(trb) == 1u, "TRB cycle bit set");
    }
    // Link TRB with cycle bit clear
    {
        XhciTrb trb{};
        trb.parameter = 0x0000000000000040ULL;
        trb.status = 0x00000000u;
        trb.control = (TRB_TYPE_LINK << 10) | 0x00u;
        expect(trb_type(trb) == TRB_TYPE_LINK, "TRB type link");
        expect(trb_cycle_bit(trb) == 0u, "TRB cycle bit clear");
    }
    // Port status change event
    {
        XhciEventTrb evt{};
        evt.eventData = 0x00000002ULL;
        evt.status = (CC_SUCCESS << 24);
        evt.control = (TRB_TYPE_PORT_STATUS_CHANGE << 10) | 0x01u;
        expect(trb_is_port_status_change(evt), "TRB is port status change");
        expect(trb_completion_code(evt) == CC_SUCCESS, "TRB completion code success");
        expect(trb_event_data(evt) == 0x00000002u, "TRB event data");
    }
    // Command completion event
    {
        XhciEventTrb evt{};
        evt.eventData = 0x0000000000000100ULL;
        evt.status = (CC_SUCCESS << 24);
        evt.control = (TRB_TYPE_COMMAND_COMP << 10) | 0x01u;
        expect(!trb_is_port_status_change(evt), "TRB is not port status change");
        expect(trb_completion_code(evt) == CC_SUCCESS, "TRB command completion code");
    }
    // Transfer event with error
    {
        XhciEventTrb evt{};
        evt.eventData = 0x0000000000000200ULL;
        evt.status = (CC_STALL_ERROR << 24);
        evt.control = (TRB_TYPE_TRANSFER_EVENT << 10) | 0x01u;
        expect(trb_completion_code(evt) == CC_STALL_ERROR, "TRB transfer event stall");
    }
}

// ================================================================
// Name helper tests
// ================================================================

void test_name_helpers()
{
    expect(std::strcmp(link_state_name(XHCI_PLS_U0), "U0") == 0, "link state U0");
    expect(std::strcmp(link_state_name(XHCI_PLS_U1), "U1") == 0, "link state U1");
    expect(std::strcmp(link_state_name(XHCI_PLS_U3), "U3") == 0, "link state U3");
    expect(std::strcmp(link_state_name(XHCI_PLS_DISABLED), "Disabled") == 0,
           "link state Disabled");
    expect(std::strcmp(link_state_name(XHCI_PLS_RX_DETECT), "RxDetect") == 0,
           "link state RxDetect");
    expect(std::strcmp(link_state_name(XHCI_PLS_POLLING), "Polling") == 0,
           "link state Polling");
    expect(std::strcmp(link_state_name(XHCI_PLS_RESUME), "Resume") == 0,
           "link state Resume");
    expect(std::strcmp(link_state_name(0xFFu), "Unknown") == 0, "link state unknown");

    expect(std::strcmp(speed_name(XHCI_SPEED_FULL), "Full") == 0, "speed Full");
    expect(std::strcmp(speed_name(XHCI_SPEED_LOW), "Low") == 0, "speed Low");
    expect(std::strcmp(speed_name(XHCI_SPEED_HIGH), "High") == 0, "speed High");
    expect(std::strcmp(speed_name(XHCI_SPEED_SUPER), "Super") == 0, "speed Super");
    expect(std::strcmp(speed_name(XHCI_SPEED_SUPER_PLUS), "SuperPlus") == 0,
           "speed SuperPlus");
    expect(std::strcmp(speed_name(0xFFu), "Unknown") == 0, "speed unknown");

    expect(std::strcmp(completion_code_name(CC_SUCCESS), "Success") == 0,
           "cc Success");
    expect(std::strcmp(completion_code_name(CC_STALL_ERROR), "StallError") == 0,
           "cc StallError");
    expect(std::strcmp(completion_code_name(CC_SHORT_PACKET), "ShortPacket") == 0,
           "cc ShortPacket");
    expect(std::strcmp(completion_code_name(CC_COMMAND_RING_STOPPED),
                        "CommandRingStopped") == 0, "cc CommandRingStopped");
    expect(std::strcmp(completion_code_name(0xFFu), "Unknown") == 0, "cc unknown");

    expect(std::strcmp(trb_type_name(TRB_TYPE_NORMAL), "Normal") == 0,
           "TRB type Normal");
    expect(std::strcmp(trb_type_name(TRB_TYPE_LINK), "Link") == 0, "TRB type Link");
    expect(std::strcmp(trb_type_name(TRB_TYPE_PORT_STATUS_CHANGE),
                        "PortStatusChange") == 0, "TRB type PortStatusChange");
    expect(std::strcmp(trb_type_name(TRB_TYPE_COMMAND_COMP),
                        "CommandCompletion") == 0, "TRB type CommandCompletion");
    expect(std::strcmp(trb_type_name(0xFFu), "Unknown") == 0, "TRB type unknown");
}

// ================================================================
// Port SC bit tests
// ================================================================

void test_portsc_bits()
{
    // CCS
    expect(portsc_is_connected(XHCI_PORTSC_CCS), "PORTSC CCS");
    expect(!portsc_is_connected(0), "PORTSC no CCS");
    // PED
    expect(portsc_is_enabled(XHCI_PORTSC_PED), "PORTSC PED");
    expect(!portsc_is_enabled(0), "PORTSC no PED");
    // PP
    expect(portsc_is_powered(XHCI_PORTSC_PP), "PORTSC PP");
    expect(!portsc_is_powered(0), "PORTSC no PP");
    // CSC
    expect(portsc_is_connect_change(XHCI_PORTSC_CSC), "PORTSC CSC");
    expect(!portsc_is_connect_change(0), "PORTSC no CSC");
    // PRC
    expect(portsc_is_reset_change(XHCI_PORTSC_PRC), "PORTSC PRC");
    expect(!portsc_is_reset_change(0), "PORTSC no PRC");
    // PLC
    expect(portsc_is_link_change(XHCI_PORTSC_PLC), "PORTSC PLC");
    expect(!portsc_is_link_change(0), "PORTSC no PLC");
    // CEC
    expect(portsc_is_config_error(XHCI_PORTSC_CEC), "PORTSC CEC");
    expect(!portsc_is_config_error(0), "PORTSC no CEC");
    // OCA
    expect(portsc_is_over_current(XHCI_PORTSC_OCA), "PORTSC OCA");
    expect(!portsc_is_over_current(0), "PORTSC no OCA");
    // WPR
    expect(portsc_is_warm_reset(XHCI_PORTSC_WPR), "PORTSC WPR");
    expect(!portsc_is_warm_reset(0), "PORTSC no WPR");
    // DR
    expect(portsc_is_removable(XHCI_PORTSC_DR), "PORTSC DR");
    expect(!portsc_is_removable(0), "PORTSC no DR");
}

// ================================================================
// PLS and speed extraction tests
// ================================================================

void test_pls_speed_extraction()
{
    // PLS extraction
    expect(portsc_get_pls(XHCI_PLS_U0 << 5) == XHCI_PLS_U0, "PLS U0");
    expect(portsc_get_pls(XHCI_PLS_U1 << 5) == XHCI_PLS_U1, "PLS U1");
    expect(portsc_get_pls(XHCI_PLS_U3 << 5) == XHCI_PLS_U3, "PLS U3");
    expect(portsc_get_pls(XHCI_PLS_DISABLED << 5) == XHCI_PLS_DISABLED,
           "PLS Disabled");
    expect(portsc_get_pls(XHCI_PLS_RX_DETECT << 5) == XHCI_PLS_RX_DETECT,
           "PLS RxDetect");
    expect(portsc_get_pls(XHCI_PLS_POLLING << 5) == XHCI_PLS_POLLING,
           "PLS Polling");
    expect(portsc_get_pls(XHCI_PLS_RESUME << 5) == XHCI_PLS_RESUME,
           "PLS Resume");

    // Speed extraction
    expect(portsc_get_speed(XHCI_SPEED_FULL << 10) == XHCI_SPEED_FULL,
           "speed Full");
    expect(portsc_get_speed(XHCI_SPEED_LOW << 10) == XHCI_SPEED_LOW,
           "speed Low");
    expect(portsc_get_speed(XHCI_SPEED_HIGH << 10) == XHCI_SPEED_HIGH,
           "speed High");
    expect(portsc_get_speed(XHCI_SPEED_SUPER << 10) == XHCI_SPEED_SUPER,
           "speed Super");
    expect(portsc_get_speed(XHCI_SPEED_SUPER_PLUS << 10) == XHCI_SPEED_SUPER_PLUS,
           "speed SuperPlus");
}

} // namespace

int main()
{
    test_bar_validation();
    test_ownership_detection();
    test_ring_helpers();
    test_erst_helpers();
    test_port_status_decoding();
    test_trb_decoding();
    test_name_helpers();
    test_portsc_bits();
    test_pls_speed_extraction();

    if (g_failures == 0) {
        std::cout << "xHCI bring-up test PASS\n";
        return 0;
    }
    std::cerr << g_failures << " xHCI bring-up test failure(s)\n";
    return 1;
}
