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

// ================================================================
// Context field helper tests (Section 7)
// ================================================================

void test_context_helpers()
{
    // Slot context: speed
    {
        const uint32_t dw0 = slot_ctx_set_speed(0u, XHCI_SPEED_HIGH);
        expect(slot_ctx_speed(dw0) == XHCI_SPEED_HIGH, "slot ctx speed High");
        expect(slot_ctx_route_string(dw0) == 0u, "slot ctx route string clear");
    }
    // Slot context: context entries
    {
        const uint32_t dw0 = slot_ctx_set_context_entries(0u, 1u);
        expect(slot_ctx_context_entries(dw0) == 1u, "slot ctx context entries 1");
    }
    // Slot context: root port
    {
        const uint32_t dw1 = slot_ctx_set_root_port(0u, 9u);
        expect(slot_ctx_root_port(dw1) == 9u, "slot ctx root port 9");
    }
    // Slot context: interrupter target + usb address
    {
        const uint32_t dw4 = slot_ctx_set_interrupter_target(0u, 0u) |
                             slot_ctx_set_usb_address(0u, 5u);
        expect(slot_ctx_interrupter_target(dw4) == 0u, "slot ctx interrupter target");
        expect(slot_ctx_usb_address(dw4) == 5u, "slot ctx usb address 5");
    }
    // Endpoint context: max packet size
    {
        const uint32_t dw0 = ep_ctx_set_max_packet_size(0u, 64u);
        expect(ep_ctx_max_packet_size(dw0) == 64u, "ep ctx mps 64");
    }
    // Endpoint context: type + cerr + max burst
    {
        const uint32_t dw1 = ep_ctx_set_type(0u, XHCI_EP_TYPE_CONTROL) |
                             ep_ctx_set_cerr(0u, 3u) |
                             ep_ctx_set_max_burst(0u, 0u);
        expect(ep_ctx_type(dw1) == XHCI_EP_TYPE_CONTROL, "ep ctx type control");
        expect(ep_ctx_cerr(dw1) == 3u, "ep ctx cerr 3");
        expect(ep_ctx_max_burst(dw1) == 0u, "ep ctx max burst 0");
    }
    // Endpoint context: dequeue pointer
    {
        const uint64_t ptr = ep_ctx_dequeue_ptr(0x12345678u, 0x9ABCDEF0u);
        expect(ptr == 0x9ABCDEF012345678ULL, "ep ctx dequeue ptr");
    }
    // Endpoint context: interval
    {
        const uint32_t dw0 = ep_ctx_set_interval(0u, 5u);
        expect(ep_ctx_interval(dw0) == 5u, "ep ctx interval 5");
    }
    // Invalid speed value (should be masked to 4 bits)
    {
        const uint32_t dw0 = slot_ctx_set_speed(0u, 0xFFu);
        expect(slot_ctx_speed(dw0) == 0x0Fu, "slot ctx speed masked");
    }
}

// ================================================================
// TRB builder tests (Sections 6, 8, 9)
// ================================================================

void test_trb_builders()
{
    // Enable Slot
    {
        const XhciTrb trb = trb_enable_slot(0);
        expect(trb_type(trb) == TRB_TYPE_ENABLE_SLOT, "enable slot type");
        expect(trb_cycle_bit(trb) == 0u, "enable slot cycle clear");
        expect(trb.parameter == 0u, "enable slot parameter zero");
    }
    // Address Device
    {
        const XhciTrb trb = trb_address_device(0x1000u, false, 1u);
        expect(trb_type(trb) == TRB_TYPE_ADDRESS_DEVICE, "address device type");
        expect(trb.parameter == 0x1000u, "address device parameter");
        expect(((trb.control >> 24) & 0xFFu) == 1u, "address device slot id");
    }
    // Address Device BSR
    {
        const XhciTrb trb = trb_address_device(0x2000u, true, 2u);
        expect(((trb.control >> 9) & 0x01u) == 1u, "address device BSR set");
        expect(((trb.control >> 24) & 0xFFu) == 2u, "address device BSR slot id");
    }
    // Setup Stage TRB
    {
        kernel::usb::SetupPacket setup{};
        setup.bmRequestType = 0x80;
        setup.bRequest = 0x06;
        setup.wValue = 0x0100;
        setup.wIndex = 0;
        setup.wLength = 18;
        const XhciTrb trb = trb_setup_stage(setup, 2u, false);
        expect(trb_type(trb) == TRB_TYPE_SETUP_STAGE, "setup stage type");
        expect((trb.status & 0x0000FFFFu) == 8u, "setup stage length 8");
        expect(((trb.status >> 16) & 0x03u) == 2u, "setup stage TRT IN");
        expect(((trb.control >> 6) & 0x01u) == 1u, "setup stage IDT");
        expect((trb.control & 0x01u) == 0u, "setup stage cycle clear");
    }
    // Data Stage TRB IN
    {
        const XhciTrb trb = trb_data_stage(0x3000u, 18u, true, false, true);
        expect(trb_type(trb) == TRB_TYPE_DATA_STAGE, "data stage type");
        expect((trb.status & 0x0000FFFFu) == 18u, "data stage length");
        expect(((trb.status >> 16) & 0x01u) == 1u, "data stage IN");
        expect((trb.control & 0x02u) == 0x02u, "data stage chain");
    }
    // Data Stage TRB OUT
    {
        const XhciTrb trb = trb_data_stage(0x4000u, 64u, false, false, false);
        expect(((trb.status >> 16) & 0x01u) == 0u, "data stage OUT");
        expect((trb.control & 0x02u) == 0u, "data stage no chain");
    }
    // Status Stage TRB
    {
        const XhciTrb trb = trb_status_stage(true, true);
        expect(trb_type(trb) == TRB_TYPE_STATUS_STAGE, "status stage type");
        expect(((trb.status >> 16) & 0x01u) == 1u, "status stage IN");
        expect(((trb.control >> 5) & 0x01u) == 1u, "status stage IOC");
    }
}

// ================================================================
// Event field accessor tests (Section 14)
// ================================================================

void test_event_accessors()
{
    // Command completion event
    {
        XhciEventTrb evt{};
        evt.eventData = 0x1234567890ABCDEFULL;
        evt.status = (CC_SUCCESS << 24);
        evt.control = (TRB_TYPE_COMMAND_COMP << 10) | (1u << 1) | (5u << 24);
        expect(event_cmd_comp_slot_id(evt) == 5u, "cmd comp slot id");
        expect(event_cmd_comp_trb_ptr(evt) == 0x1234567890ABCDEFULL, "cmd comp trb ptr");
    }
    // Transfer event
    {
        XhciEventTrb evt{};
        evt.eventData = 0x2000ULL;
        evt.status = (10u & 0x00FFFFFFu) | (CC_SUCCESS << 24);
        evt.control = (TRB_TYPE_TRANSFER_EVENT << 10) | (1u << 1) | (2u << 16) | (3u << 24);
        expect(event_transfer_slot_id(evt) == 3u, "transfer event slot id");
        expect(event_transfer_ep_id(evt) == 2u, "transfer event ep id");
        expect(event_transfer_length(evt) == 10u, "transfer event length");
        expect(event_transfer_trb_ptr(evt) == 0x2000ULL, "transfer event trb ptr");
    }
    // Transfer event with error
    {
        XhciEventTrb evt{};
        evt.status = (CC_STALL_ERROR << 24);
        evt.control = (TRB_TYPE_TRANSFER_EVENT << 10) | (1u << 1);
        expect(trb_completion_code(evt) == CC_STALL_ERROR, "transfer event stall");
    }
}

// ================================================================
// USB descriptor validation tests (Section 10)
// ================================================================

void test_descriptor_validation()
{
    // Valid device descriptor
    {
        const uint8_t desc[18] = {
            18, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
            0xEF, 0x17, 0x99, 0x60, 0x00, 0x01, 0, 0, 0, 1
        };
        const UsbDeviceDescriptorInfo info = validate_usb_device_descriptor(desc, 18);
        expect(info.valid, "valid device descriptor");
        expect(info.bcdUSB == 0x0200, "descriptor bcdUSB");
        expect(info.maxPacketSize0 == 64, "descriptor mps0");
        expect(info.idVendor == 0x17EF, "descriptor VID");
        expect(info.idProduct == 0x6099, "descriptor PID");
        expect(info.numConfigurations == 1, "descriptor num configs");
    }
    // Short descriptor
    {
        const uint8_t desc[8] = { 8, 0x01, 0, 0, 0, 0, 0, 8 };
        const UsbDeviceDescriptorInfo info = validate_usb_device_descriptor(desc, 8);
        expect(!info.valid, "short descriptor invalid");
    }
    // Wrong descriptor type
    {
        const uint8_t desc[18] = {
            18, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
            0xEF, 0x17, 0x99, 0x60, 0x00, 0x01, 0, 0, 0, 1
        };
        const UsbDeviceDescriptorInfo info = validate_usb_device_descriptor(desc, 18);
        expect(!info.valid, "wrong descriptor type invalid");
    }
    // Invalid bLength
    {
        const uint8_t desc[18] = {
            8, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
            0xEF, 0x17, 0x99, 0x60, 0x00, 0x01, 0, 0, 0, 1
        };
        const UsbDeviceDescriptorInfo info = validate_usb_device_descriptor(desc, 18);
        expect(!info.valid, "invalid bLength");
    }
    // Malformed EP0 packet size
    {
        const uint8_t desc[18] = {
            18, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 12,
            0xEF, 0x17, 0x99, 0x60, 0x00, 0x01, 0, 0, 0, 1
        };
        const UsbDeviceDescriptorInfo info = validate_usb_device_descriptor(desc, 18);
        expect(!info.valid, "malformed mps0");
    }
    // Null pointer
    {
        const UsbDeviceDescriptorInfo info = validate_usb_device_descriptor(nullptr, 18);
        expect(!info.valid, "null descriptor invalid");
    }
}

// ================================================================
// Control request tests (Section 9)
// ================================================================

void test_control_request()
{
    // GET_DESCRIPTOR(Device) request
    {
        const UsbControlRequest req = usb_get_device_descriptor_request(18);
        expect(req.bmRequestType == 0x80, "get desc bmRequestType");
        expect(req.bRequest == USB_REQ_GET_DESCRIPTOR, "get desc bRequest");
        expect(req.wValue == (USB_DESC_TYPE_DEVICE << 8), "get desc wValue");
        expect(req.wLength == 18, "get desc wLength");
        expect(req.dataIn, "get desc dataIn");
        expect(req.timeoutMs > 0u, "get desc timeout");
    }
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
    test_context_helpers();
    test_trb_builders();
    test_event_accessors();
    test_descriptor_validation();
    test_control_request();

    if (g_failures == 0) {
        std::cout << "xHCI bring-up test PASS\n";
        return 0;
    }
    std::cerr << g_failures << " xHCI bring-up test failure(s)\n";
    return 1;
}
