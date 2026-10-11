#include "kernel/modern_input_diagnostics.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

int g_assertions = 0;

void require(bool condition, const char* message)
{
    ++g_assertions;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

using namespace kernel::modern_input_diagnostics;

int32_t add_xhci(uint8_t bus, uint8_t device, uint8_t function,
                 uint16_t vendor, uint16_t product)
{
    return record_controller(bus, device, function, vendor, product,
                             0x0Cu, 0x03u, 0x30u, ControllerKind::XHCI);
}

void record_pair(uint32_t value)
{
    record_stage(0, Stage::AddressDevice, Status::Fail,
                 value, true, ~value, true,
                 2u, 4u, 1u, 0x11u, true);
}

void test_concurrent_publication()
{
    initialize();
    begin_pci_enumeration();
    const int32_t controller = add_xhci(0u, 20u, 0u, 0x8086u, 0x06EDu);
    require(controller == 0, "concurrency fixture controller index");
    complete_pci_enumeration(Status::Pass);

    std::atomic<bool> done(false);
    std::atomic<bool> consistent(true);
    std::thread writer([&]() {
        for (uint32_t i = 1u; i <= 20000u; ++i) record_pair(i);
        done.store(true, std::memory_order_release);
    });
    std::thread reader([&]() {
        while (!done.load(std::memory_order_acquire)) {
            Snapshot snapshot;
            copy_snapshot(&snapshot);
            const StageResult& result = snapshot.controllers[0].stages[
                static_cast<uint32_t>(Stage::AddressDevice)];
            if (result.hasErrorCode && result.controllerStatus != ~result.errorCode) {
                consistent.store(false, std::memory_order_relaxed);
                break;
            }
        }
    });
    writer.join();
    reader.join();
    require(consistent.load(std::memory_order_relaxed),
            "concurrent readers never observe a torn stage result");
}

} // namespace

int main()
{
    initialize();
    Snapshot snapshot;
    copy_snapshot(&snapshot);
    require(snapshot.pciEnumeration == Status::NotAttempted,
            "initial PCI scan is not attempted");
    require(snapshot.usbControllerCount == 0u && snapshot.xhciControllerCount == 0u,
            "initial controller counts are zero");
    require(snapshot.selectedControllerIndex == -1 && !snapshot.firstFailure.present,
            "initial selection and first failure are empty");

    begin_pci_enumeration();
    complete_pci_enumeration(Status::Pass);
    copy_snapshot(&snapshot);
    require(snapshot.pciEnumeration == Status::Pass,
            "no-controller PCI scan can complete successfully");
    require(snapshot.usbControllerCount == 0u && snapshot.xhciControllerCount == 0u,
            "no-controller scan preserves actual zero counts");

    begin_pci_enumeration();
    const int32_t first = add_xhci(0u, 20u, 0u, 0x8086u, 0x06EDu);
    require(first == 0, "first controller gets deterministic index zero");
    record_stage(first, Stage::PciDiscovery, Status::Pass, 0u, false,
                 0u, false, 0u, 0u, 0u, 0u, false);
    record_mmio_base(first, 0xFEDC0000u);
    record_capabilities(first, 0x20u, 0x0110u, 64u, 16u, 64u, true, 2u, 0u);
    record_ownership_status(first, OwnershipStatus::OsOwned);
    record_root_ports(first, 16u, 2u, 4u);
    record_stage(first, Stage::Capabilities, Status::Pass, 0u, false,
                 0u, false, 0u, 0u, 0u, 0u, false);
    record_stage(first, Stage::DmaPrerequisites, Status::Pass, 0u, false,
                 0u, false, 0u, 0u, 0u, 0u, false);
    record_stage(first, Stage::ControllerRunning, Status::Pass, 0u, false,
                 0u, false, 0u, 0u, 0u, 0u, false);
    record_stage(first, Stage::PortReset, Status::Pass, 0u, false,
                 0x00200001u, true, 0u, 4u, 0u, 0u, false);
    record_stage(first, Stage::EnableSlot, Status::Pass, 0u, false,
                 0u, false, 2u, 0u, 0u, 1u, true);
    record_stage(first, Stage::AddressDevice, Status::Fail,
                 0u, false, 0x00000002u, true,
                 2u, 4u, 0u, 0x11u, true);
    record_stage(first, Stage::ControlTransfer, Status::Timeout,
                 1000u, true, 0u, false, 2u, 4u, 1u, 0u, false);
    record_stage(first, Stage::HidDiscovery, Status::Unsupported,
                 0u, false, 0u, false, 0u, 0u, 0u, 0u, false);
    record_stage(first, Stage::HidReportDelivery, Status::NotAttempted,
                 0u, false, 0u, false, 0u, 0u, 0u, 0u, false);
    record_device_descriptor(first, true, true, 0x17EFu, 0x6099u);
    increment_counter(first, Counter::KeyboardReports);
    increment_counter(first, Counter::KeyboardReports);
    increment_counter(first, Counter::DecodedKeyEvents);

    copy_snapshot(&snapshot);
    require(snapshot.usbControllerCount == 1u && snapshot.xhciControllerCount == 1u,
            "single xHCI controller is counted");
    require(snapshot.controllers[0].capLength == 0x20u &&
            snapshot.controllers[0].hciVersion == 0x0110u &&
            snapshot.controllers[0].maxPorts == 16u &&
            snapshot.controllers[0].scratchpadBuffersRequired == 0u,
            "capability values are preserved from observation");
    require(snapshot.controllers[0].connectedPortCount == 2u &&
            snapshot.controllers[0].selectedPort == 4u,
            "root-port and selected-port results are preserved");
    require(snapshot.controllers[0].stages[static_cast<uint32_t>(Stage::EnableSlot)].slotId == 2u &&
            snapshot.controllers[0].stages[static_cast<uint32_t>(Stage::PortReset)].portId == 4u,
            "USB slot and port status are preserved");
    require(snapshot.controllers[0].descriptorTransfersSucceeded == 1u &&
            snapshot.controllers[0].deviceDescriptorValid &&
            snapshot.controllers[0].vendorDescriptor == 0x17EFu &&
            snapshot.controllers[0].productDescriptor == 0x6099u,
            "successful device descriptor identity is preserved");
    require(snapshot.controllers[0].inputCounters[static_cast<uint32_t>(Counter::KeyboardReports)] == 2u &&
            snapshot.controllers[0].inputCounters[static_cast<uint32_t>(Counter::DecodedKeyEvents)] == 1u,
            "event counters increment only through explicit observations");
    require(snapshot.firstFailure.present && snapshot.firstFailure.stage == Stage::AddressDevice &&
            snapshot.firstFailure.result.completionCode == 0x11u,
            "first failure is captured with completion code");
    require(snapshot.latestStage == Stage::HidReportDelivery &&
            snapshot.latestStatus.status == Status::NotAttempted,
            "latest status advances independently of first failure");

    char formatted[112];
    format_first_failure(snapshot, formatted, sizeof(formatted));
    require(std::strstr(formatted, "00:14.0") != nullptr &&
            std::strstr(formatted, "Address Device") != nullptr &&
            std::strstr(formatted, "cc=0x11") != nullptr &&
            std::strstr(formatted, "status=0x00000002") != nullptr &&
            std::strstr(formatted, "slot=2") != nullptr &&
            std::strstr(formatted, "port=4") != nullptr,
            "first-failure formatting includes controller, stage, result, slot, and port");
    char shortText[8];
    format_first_failure(snapshot, shortText, sizeof(shortText));
    require(shortText[sizeof(shortText) - 1u] == '\0',
            "failure text truncation always terminates the destination");

    StageResult error = {};
    error.status = Status::Fail;
    error.hasCompletionCode = true;
    error.completionCode = 0x0Bu;
    format_stage_result(error, formatted, sizeof(formatted));
    require(std::strstr(formatted, "FAIL") != nullptr &&
            std::strstr(formatted, "cc=0x0b") != nullptr,
            "error result formatting includes completion code");
    require(std::strcmp(status_name(Status::Unsupported), "UNSUPPORTED") == 0 &&
            std::strcmp(status_name(Status::NotAttempted), "NOT ATTEMPTED") == 0 &&
            std::strcmp(status_name(static_cast<Status>(0xFFu)), "UNKNOWN") == 0,
            "unsupported, not-attempted, and malformed status values stay distinct");
    require(std::strcmp(stage_name(static_cast<Stage>(0xFFu)), "unknown stage") == 0,
            "malformed stage names are bounded and explicit");

    const int32_t second = record_controller(0u, 21u, 0u, 0x1022u, 0x145Cu,
                                              0x0Cu, 0x03u, 0x30u,
                                              ControllerKind::XHCI);
    require(second == 1, "second controller gets deterministic index one");
    select_controller(second);
    record_stage(second, Stage::DmaPrerequisites, Status::Blocked,
                 1u, true, 0u, false, 0u, 0u, 0u, 0u, false);
    copy_snapshot(&snapshot);
    require(snapshot.usbControllerCount == 2u && snapshot.xhciControllerCount == 2u &&
            snapshot.selectedControllerIndex == 1 &&
            snapshot.controllers[0].stages[static_cast<uint32_t>(Stage::AddressDevice)].status == Status::Fail &&
            snapshot.controllers[1].stages[static_cast<uint32_t>(Stage::DmaPrerequisites)].status == Status::Blocked,
            "multiple controllers retain independent state and selected identity");
    require(snapshot.firstFailure.stage == Stage::AddressDevice,
            "later controller failure does not replace the first failure");

    begin_pci_enumeration();
    copy_snapshot(&snapshot);
    require(snapshot.firstFailure.present && snapshot.firstFailure.stage == Stage::AddressDevice &&
            snapshot.firstFailure.vendorId == 0x8086u && snapshot.firstFailure.deviceId == 0x06EDu,
            "first failure and controller identity survive later enumeration");
    for (uint32_t i = 0u; i < MAX_CONTROLLERS + 1u; ++i) {
        record_controller(0u, static_cast<uint8_t>(i), 0u, 0x8086u,
                          static_cast<uint16_t>(0x1000u + i), 0x0Cu, 0x03u,
                          0x30u, ControllerKind::XHCI);
    }
    complete_pci_enumeration(Status::Pass);
    copy_snapshot(&snapshot);
    require(snapshot.usbControllerCount == MAX_CONTROLLERS + 1u &&
            snapshot.storedControllerCount == MAX_CONTROLLERS &&
            snapshot.controllersOmitted == 1u,
            "controller publication is bounded and reports omitted devices");
    record_stage(9999, Stage::AddressDevice, Status::Fail, 9u, true,
                 0u, false, 0u, 0u, 0u, 0u, false);
    copy_snapshot(&snapshot);
    require(snapshot.firstFailure.present && snapshot.firstFailure.controllerIndex == 0 &&
            snapshot.latestControllerIndex == 9999,
            "malformed controller indices do not overwrite the preserved first failure");

    PanelLayout layout = {};
    require(!calculate_panel_layout(0u, 480u, &layout),
            "zero-width panel layout is rejected");
    require(calculate_panel_layout(128u, 96u, &layout) && layout.compact &&
            layout.width <= 128u && layout.height <= 96u && layout.rowCapacity > 0u,
            "small-screen panel layout remains within framebuffer bounds");
    require(calculate_panel_layout(1920u, 1080u, &layout) && !layout.compact &&
            layout.width <= 560u && layout.height <= 500u && layout.rowCapacity >= 33u,
            "common-screen panel stays bounded instead of covering the desktop");

    test_concurrent_publication();
    std::printf("Modern input diagnostics tests PASS (%d assertions).\n", g_assertions);
    return 0;
}
