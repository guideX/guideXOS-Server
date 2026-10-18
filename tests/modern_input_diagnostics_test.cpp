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

void test_boot_progress_and_presentation()
{
    initialize();
    Snapshot snapshot;
    copy_snapshot(&snapshot);
    require(snapshot.bootStage == BootStage::Unknown && snapshot.bootSequence == 0u,
            "boot progress starts without claiming a milestone");
    require(snapshot.snapshotSequence == 1u && snapshot.displayedSequence == 0u &&
            presentation_pending(),
            "initial snapshot is published and awaits a first presentation");

    record_boot_milestone(BootStage::KernelEntry);
    copy_snapshot(&snapshot);
    const uint64_t entrySequence = snapshot.snapshotSequence;
    require(snapshot.bootStage == BootStage::KernelEntry && snapshot.bootSequence == 1u,
            "kernel entry milestone is an executed, sequenced transition");
    record_panel_presented(entrySequence);
    require(!presentation_pending(), "presented snapshot clears the stale-display state");

    record_boot_milestone(BootStage::MemoryInitialized);
    record_boot_milestone(BootStage::FramebufferReady);
    record_boot_milestone(BootStage::PciDiscoveryEntered);
    copy_snapshot(&snapshot);
    const uint64_t earlyPaintSequence = snapshot.snapshotSequence;
    require(snapshot.bootStage == BootStage::PciDiscoveryEntered && snapshot.bootSequence == 4u,
            "boot milestones preserve the observed execution order");
    record_panel_presented(earlyPaintSequence);

    // Model a completed xHCI initialization arriving after the first desktop
    // paint. It must stay unpublished to the visible panel until the normal
    // renderer acknowledges the new sequence.
    const int32_t controller = add_xhci(0u, 20u, 0u, 0x8086u, 0x06EDu);
    require(controller == 0, "late xHCI result gets its bounded controller slot");
    record_boot_milestone(BootStage::XhciInitEntered);
    record_stage(controller, Stage::ControllerRunning, Status::Pass, 0u, false,
                 0u, false, 0u, 0u, 0u, 0u, false);
    record_boot_milestone(BootStage::XhciInitCompleted);
    require(presentation_pending(),
            "controller completion published after early paint is detected as stale");
    copy_snapshot(&snapshot);
    const uint64_t latestSequence = snapshot.snapshotSequence;
    require(snapshot.latestStage == Stage::ControllerRunning &&
            snapshot.latestStatus.status == Status::Pass && !snapshot.firstFailure.present,
            "successful controller completion remains distinct from a failure");
    record_panel_presented(earlyPaintSequence);
    require(presentation_pending(), "acknowledging an older frame cannot hide newer state");
    record_panel_presented(latestSequence);
    require(!presentation_pending(), "newly presented frame catches up to publication");

    record_stage(controller, Stage::BarMmio, Status::Fail, 7u, true,
                 0u, false, 0u, 0u, 0u, 0u, false, "synthetic late failure");
    require(presentation_pending(),
            "controller failure published after the early paint requests a repaint");

    const uint64_t beforeRepeatedPublish = latestSequence;
    record_stage(controller, Stage::BarMmio, Status::Fail, 7u, true,
                 0u, false, 0u, 0u, 0u, 0u, false, "synthetic pre-desktop failure");
    copy_snapshot(&snapshot);
    require(snapshot.snapshotSequence > beforeRepeatedPublish && presentation_pending(),
            "repeated state publication advances version and requests another repaint");

    require(std::strcmp(boot_stage_name(BootStage::KernelEntry), "KERNEL_ENTRY") == 0 &&
            std::strcmp(boot_stage_name(BootStage::XhciInitCompleted), "XHCI_INIT_COMPLETED") == 0 &&
            std::strcmp(boot_stage_name(BootStage::MainLoopEntered), "MAIN_LOOP_ENTERED") == 0 &&
            std::strcmp(boot_stage_name(static_cast<BootStage>(0xFFu)), "UNKNOWN") == 0,
            "boot milestone formatting handles known and malformed stages");
}

void test_failure_before_desktop_initialization()
{
    initialize();
    const int32_t controller = add_xhci(0u, 20u, 0u, 0x8086u, 0x06EDu);
    record_stage(controller, Stage::BarMmio, Status::Fail, 3u, true,
                 0u, false, 0u, 0u, 0u, 0u, false,
                 "synthetic failure before desktop initialization");
    Snapshot snapshot;
    copy_snapshot(&snapshot);
    require(snapshot.firstFailure.present &&
            snapshot.firstFailure.stage == Stage::BarMmio &&
            snapshot.bootStage == BootStage::Unknown,
            "controller failure can be retained before desktop initialization");

    record_boot_milestone(BootStage::DesktopInitialized);
    copy_snapshot(&snapshot);
    require(snapshot.firstFailure.present && snapshot.latestStage == Stage::BarMmio &&
            snapshot.latestStatus.status == Status::Fail,
            "desktop initialization does not erase a prior controller failure");
}

void test_snapshot_reset()
{
    initialize();
    record_boot_milestone(BootStage::KernelEntry);
    const int32_t controller = add_xhci(0u, 20u, 0u, 0x8086u, 0x06EDu);
    record_stage(controller, Stage::BarMmio, Status::Fail, 1u, true,
                 0u, false, 0u, 0u, 0u, 0u, false);
    Snapshot beforeReset;
    copy_snapshot(&beforeReset);
    record_panel_presented(beforeReset.snapshotSequence);
    require(!presentation_pending(), "fixture frame is acknowledged before reset");

    initialize();
    Snapshot afterReset;
    copy_snapshot(&afterReset);
    require(afterReset.snapshotSequence == 1u && afterReset.displayedSequence == 0u &&
            afterReset.bootStage == BootStage::Unknown && afterReset.bootSequence == 0u,
            "initialization resets generation and boot progress deterministically");
    require(!afterReset.firstFailure.present && afterReset.usbControllerCount == 0u &&
            afterReset.pciScanState == PciScanState::NotInvoked && presentation_pending(),
            "initialization clears prior hardware evidence and requires a fresh paint");
}

} // namespace

int main()
{
    initialize();
    Snapshot snapshot;
    copy_snapshot(&snapshot);
    require(snapshot.bootStage == BootStage::Unknown && snapshot.bootSequence == 0u,
            "initial boot stage does not infer execution");
    require(snapshot.pciEnumeration == Status::NotAttempted,
            "initial PCI scan is not attempted");
    require(snapshot.pciScanState == PciScanState::NotInvoked,
            "initial PCI scan execution state is not invoked");
    require(snapshot.usbControllerCount == 0u && snapshot.xhciControllerCount == 0u,
            "initial controller counts are zero");
    require(snapshot.selectedControllerIndex == -1 && !snapshot.firstFailure.present,
            "initial selection and first failure are empty");
    require(snapshot.snapshotSequence == 1u && snapshot.displayedSequence == 0u &&
            presentation_pending(),
            "initial diagnostic snapshot version is awaiting presentation");

    begin_pci_enumeration();
    copy_snapshot(&snapshot);
    require(snapshot.pciScanState == PciScanState::EntryReached,
            "PCI scan entry is published before discovery starts");
    complete_pci_enumeration(Status::Pass);
    copy_snapshot(&snapshot);
    require(snapshot.pciEnumeration == Status::Pass,
            "no-controller PCI scan can complete successfully");
    require(snapshot.pciScanState == PciScanState::NoControllers &&
            snapshot.latestStage == Stage::PciDiscovery &&
            snapshot.latestStatus.status == Status::Pass,
            "completed empty PCI scan is distinct from not invoked");
    require(snapshot.usbControllerCount == 0u && snapshot.xhciControllerCount == 0u,
            "no-controller scan preserves actual zero counts");

    begin_pci_enumeration();
    const int32_t first = add_xhci(0u, 20u, 0u, 0x8086u, 0x06EDu);
    require(first == 0, "first controller gets deterministic index zero");
    complete_pci_enumeration(Status::Pass);
    copy_snapshot(&snapshot);
    require(snapshot.pciScanState == PciScanState::ScanComplete,
            "PCI scan with a controller reports scan complete");
    record_stage(first, Stage::PciDiscovery, Status::Pass, 0u, false,
                 0u, false, 0u, 0u, 0u, 0u, false);
    record_mmio_base(first, 0xFEDC0000u);
    record_mmio_configuration(first, 0xE1020004u, 0u, 0x0002u,
                              2u, false, true, false);
    record_mmio_mapping(first, "test map", 0x4800u, true,
                        0xFFFFC00000000000ULL, 0x5000u,
                        "mapped into reserved kernel MMIO window");
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
                 2u, 4u, 0u, 0x11u, true,
                 "device command completion failed");
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
    require(snapshot.controllers[0].pciBar0 == 0xE1020004u &&
            snapshot.controllers[0].pciBar1 == 0u &&
            snapshot.controllers[0].pciBarIs64 &&
            snapshot.controllers[0].pciCommandInitial == 0x0002u &&
            snapshot.controllers[0].mmioRequiredSpan == 0x4800u &&
            snapshot.controllers[0].mmioMappingSucceeded &&
            snapshot.controllers[0].mmioValidationSubstage != nullptr &&
            snapshot.controllers[0].mmioMappedVirtual == 0xFFFFC00000000000ULL,
            "PCI BAR and verified MMIO mapping diagnostics are preserved");
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

    char formatted[192];
    format_first_failure(snapshot, formatted, sizeof(formatted));
    require(std::strstr(formatted, "00:14.0") != nullptr &&
            std::strstr(formatted, "Address Device") != nullptr &&
            std::strstr(formatted, "cc=0x11") != nullptr &&
            std::strstr(formatted, "status=0x00000002") != nullptr &&
            std::strstr(formatted, "reason=device command completion failed") != nullptr &&
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
    require(std::strcmp(pci_scan_state_name(PciScanState::NotInvoked), "NOT INVOKED") == 0 &&
            std::strcmp(pci_scan_state_name(PciScanState::EntryReached), "ENTRY REACHED") == 0 &&
            std::strcmp(pci_scan_state_name(PciScanState::ScanComplete), "SCAN COMPLETE") == 0 &&
            std::strcmp(pci_scan_state_name(PciScanState::NoControllers), "NO CONTROLLERS") == 0 &&
            std::strcmp(pci_scan_state_name(PciScanState::Failure), "FAILURE") == 0,
            "PCI scan execution states have deterministic names");
    require(std::strcmp(boot_stage_name(BootStage::PciDiscoveryCompleted),
                        "PCI_DISCOVERY_COMPLETED") == 0,
            "PCI boot milestone has a deterministic display name");
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

    complete_pci_enumeration(Status::Fail, "synthetic PCI scan failure");
    copy_snapshot(&snapshot);
    require(snapshot.pciScanState == PciScanState::Failure &&
            snapshot.pciScanFailure != nullptr &&
            std::strcmp(snapshot.pciScanFailure, "synthetic PCI scan failure") == 0,
            "PCI scan failure includes a stable reason");
    require(snapshot.firstFailure.stage == Stage::AddressDevice,
            "PCI scan failure does not overwrite earlier first-failure evidence");

    begin_pci_enumeration();
    copy_snapshot(&snapshot);
    require(snapshot.pciScanState == PciScanState::EntryReached &&
            snapshot.pciScanFailure == nullptr &&
            snapshot.firstFailure.present && snapshot.firstFailure.stage == Stage::AddressDevice &&
            snapshot.firstFailure.vendorId == 0x8086u && snapshot.firstFailure.deviceId == 0x06EDu,
            "new enumeration clears its prior scan error but preserves first failure");
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

    test_boot_progress_and_presentation();
    test_failure_before_desktop_initialization();
    test_snapshot_reset();
    test_concurrent_publication();
    std::printf("Modern input diagnostics tests PASS (%d assertions).\n", g_assertions);
    return 0;
}
