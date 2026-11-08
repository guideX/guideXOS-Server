#include "kernel/modern_input_diagnostics.h"
#include "kernel/pci_audio.h"

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

StartupOperationContext pci_context(uint8_t bus, uint8_t device, uint8_t function,
                                   uint16_t vendor, uint16_t product,
                                   uint8_t baseClass, uint8_t subClass,
                                   uint8_t configOffset)
{
    StartupOperationContext context = {};
    context.bus = bus;
    context.device = device;
    context.function = function;
    context.vendorId = vendor;
    context.deviceId = product;
    context.baseClass = baseClass;
    context.subClass = subClass;
    context.progIf = 0x30u;
    context.configOffset = configOffset;
    context.hasPciAddress = true;
    context.hasVendorDevice = true;
    context.hasClass = true;
    context.hasConfigOffset = true;
    return context;
}

void test_startup_operation_frontier()
{
    initialize();
    const StartupOperationContext context = pci_context(
        0u, 31u, 3u, 0x8086u, 0xA170u, 0x04u, 0x03u, 0x10u);
    const uint64_t audioEntry = begin_startup_operation(StartupOperation::PciAudioEntry);
    const uint64_t scanEntry = begin_startup_operation(
        StartupOperation::PciAudioScanEnter);
    const uint64_t vendorRead = begin_startup_operation(
        StartupOperation::PciConfigDataRead, &context);
    Snapshot snapshot = {};
    copy_snapshot(&snapshot);
    require(snapshot.lastStartupOperation.operation == StartupOperation::PciConfigDataRead &&
            snapshot.lastStartupOperation.status == StartupOperationStatus::InProgress &&
            snapshot.lastStartupOperation.exitSequence == 0u &&
            snapshot.pciAudioStatus == StartupOperationStatus::InProgress,
            "entered PCI config read remains pending until it returns");
    require(snapshot.lastStartupOperation.bus == 0u &&
            snapshot.lastStartupOperation.device == 31u &&
            snapshot.lastStartupOperation.function == 3u &&
            snapshot.lastStartupOperation.vendorId == 0x8086u &&
            snapshot.lastStartupOperation.deviceId == 0xA170u &&
            snapshot.lastStartupOperation.baseClass == 0x04u &&
            snapshot.lastStartupOperation.subClass == 0x03u &&
            snapshot.lastStartupOperation.configOffset == 0x10u,
            "PCI operation retains BDF, known identity, class, and config offset");

    complete_startup_operation(vendorRead, StartupOperationStatus::Completed,
                               0xA1708086u, true);
    complete_startup_operation(scanEntry, StartupOperationStatus::Completed);
    const uint64_t probe = begin_startup_operation(
        StartupOperation::PciAudioProbeEnter, &context);
    copy_snapshot(&snapshot);
    require(snapshot.lastStartupOperation.operation == StartupOperation::PciAudioProbeEnter &&
            snapshot.lastStartupOperation.status == StartupOperationStatus::InProgress,
            "audio probe entry is visible before probe return");
    complete_startup_operation(probe, StartupOperationStatus::Completed);
    complete_startup_operation(audioEntry, StartupOperationStatus::Completed);
    copy_snapshot(&snapshot);
    require(snapshot.pciAudioStatus == StartupOperationStatus::Completed &&
            snapshot.lastCompletedStartupOperation.enterSequence == audioEntry,
            "successful audio initialization completes after its nested probe");
    require(snapshot.lastCompletedStartupOperation.exitSequence > audioEntry,
            "operation exit sequence advances independently from entry sequence");

    initialize();
    const uint64_t failedScan = begin_startup_operation(
        StartupOperation::PciAudioScanEnter);
    complete_startup_operation(failedScan,
        StartupOperationStatus::ReturnedError, 0xFFFFFFFFu, true, 7u, true);
    copy_snapshot(&snapshot);
    require(snapshot.lastCompletedStartupOperation.status ==
                StartupOperationStatus::ReturnedError &&
            snapshot.lastCompletedStartupOperation.errorCode == 7u &&
            snapshot.lastCompletedStartupOperation.hasErrorCode,
            "PCI audio discovery failure preserves its returned diagnostic code");

    initialize();
    const uint64_t timedOperation = begin_startup_operation(
        StartupOperation::HdaResetWaitCrst, &context);
    copy_snapshot(&snapshot);
    require(snapshot.lastStartupOperation.status == StartupOperationStatus::InProgress &&
            snapshot.lastStartupOperation.exitSequence == 0u,
            "a pending hardware wait is not inferred to have timed out");
    complete_startup_operation(timedOperation, StartupOperationStatus::TimedOut,
                               0u, false, 1u, true);
    copy_snapshot(&snapshot);
    require(snapshot.lastStartupOperation.status == StartupOperationStatus::TimedOut &&
            snapshot.lastStartupOperation.exitSequence != 0u,
            "only an explicit bounded-wait return records timeout");

    initialize();
    const StartupOperationContext unsupported = pci_context(
        0u, 20u, 0u, 0x10ECu, 0x8168u, 0x02u, 0x00u, 0x08u);
    const uint64_t unsupportedDevice = begin_startup_operation(
        StartupOperation::PciDeviceUnsupported, &unsupported);
    complete_startup_operation(unsupportedDevice,
        StartupOperationStatus::Unsupported, 0x02000000u, true);
    copy_snapshot(&snapshot);
    require(snapshot.lastStartupOperation.status == StartupOperationStatus::Unsupported &&
            snapshot.lastStartupOperation.vendorId == 0x10ECu &&
            snapshot.lastStartupOperation.baseClass == 0x02u,
            "unknown PCI devices keep their class and identity without becoming audio probes");

    char formatted[192];
    format_startup_operation(snapshot.lastStartupOperation, formatted, sizeof(formatted));
    require(std::strstr(formatted, "PCI_DEVICE_UNSUPPORTED") != nullptr &&
            std::strstr(formatted, "bdf=00:14.0") != nullptr &&
            std::strstr(formatted, "id=10ec:8168") != nullptr &&
            std::strstr(formatted, "class=02/00/30") != nullptr &&
            std::strstr(formatted, "cfg=08") != nullptr,
            "startup operation formatter reports bounded PCI identity details");
    char truncated[12];
    format_startup_operation(snapshot.lastStartupOperation, truncated, sizeof(truncated));
    require(truncated[sizeof(truncated) - 1u] == '\0',
            "startup operation formatting truncation remains terminated");

    initialize();
    const uint64_t audio = begin_startup_operation(StartupOperation::PciAudioEntry);
    complete_startup_operation(audio, StartupOperationStatus::Completed);
    const StartupOperationContext audio1 = pci_context(
        0u, 31u, 3u, 0x8086u, 0xA170u, 0x04u, 0x03u, 0x10u);
    const StartupOperationContext audio2 = pci_context(
        0u, 27u, 0u, 0x8086u, 0x1C20u, 0x04u, 0x03u, 0x10u);
    uint64_t found = begin_startup_operation(
        StartupOperation::PciAudioDeviceFound, &audio1);
    complete_startup_operation(found, StartupOperationStatus::Completed);
    found = begin_startup_operation(StartupOperation::PciAudioDeviceFound, &audio2);
    complete_startup_operation(found, StartupOperationStatus::Completed);
    copy_snapshot(&snapshot);
    require(snapshot.pciAudioDeviceCount == 2u,
            "multiple PCI audio devices are counted independently");
    const uint64_t beforeUsb = snapshot.startupOperationSequence;
    begin_pci_enumeration();
    copy_snapshot(&snapshot);
    require(snapshot.usbHostScanStatus == StartupOperationStatus::InProgress &&
            snapshot.lastStartupOperation.operation == StartupOperation::UsbHostScanEnter &&
            snapshot.lastStartupOperation.enterSequence > beforeUsb,
            "USB-host scan begins only after audio discovery has completed");
    complete_pci_enumeration(Status::Pass);
    copy_snapshot(&snapshot);
    require(snapshot.usbHostScanStatus == StartupOperationStatus::Completed &&
            snapshot.lastStartupOperation.operation == StartupOperation::UsbHostScanComplete,
            "USB scan completion is a distinct recorded boundary");

    initialize();
    const uint64_t noDeviceAudio = begin_startup_operation(StartupOperation::PciAudioEntry);
    complete_startup_operation(noDeviceAudio, StartupOperationStatus::Completed);
    copy_snapshot(&snapshot);
    require(snapshot.pciAudioDeviceCount == 0u,
            "audio scan with no audio-class device records a successful empty result");

    initialize();
    record_pci_audio_skipped();
    begin_pci_enumeration();
    complete_pci_enumeration(Status::Pass);
    copy_snapshot(&snapshot);
    require(snapshot.pciAudioSkipped &&
            snapshot.pciAudioStatus == StartupOperationStatus::Unsupported &&
            snapshot.usbHostScanStatus == StartupOperationStatus::Completed,
            "safe diagnostic audio bypass still permits the normal USB scan");

    initialize();
    const uint64_t xhciInit = begin_startup_operation(
        StartupOperation::XhciInitEnter);
    copy_snapshot(&snapshot);
    require(snapshot.xhciInitStatus == StartupOperationStatus::InProgress,
            "xHCI initialization entry is distinct from an uninvoked controller");
    complete_startup_operation(xhciInit,
        StartupOperationStatus::ReturnedError, 0u, false, 9u, true);
    copy_snapshot(&snapshot);
    require(snapshot.xhciInitStatus == StartupOperationStatus::ReturnedError,
            "xHCI return failure is distinguishable from a startup stall");

    initialize();
    for (uint8_t i = 0u; i < MAX_STARTUP_OPERATION_TRACE + 4u; ++i) {
        const StartupOperationContext rolling = pci_context(
            0u, i, 0u, 0x8086u, static_cast<uint16_t>(i), 0x04u, 0x03u, 0x10u);
        const uint64_t sequence = begin_startup_operation(
            StartupOperation::PciConfigDataRead, &rolling);
        complete_startup_operation(sequence, StartupOperationStatus::Completed);
    }
    copy_snapshot(&snapshot);
    require(snapshot.startupOperationTraceCount == MAX_STARTUP_OPERATION_TRACE &&
            snapshot.startupOperationTraceNext == 4u &&
            snapshot.lastStartupOperation.device == MAX_STARTUP_OPERATION_TRACE + 3u,
            "operation history remains fixed-capacity and retains its newest record");
}

void test_hda_child_range_bounds()
{
    uint16_t end = 0u;
    require(kernel::pci_audio::validate_hda_child_range(0u, 0u, &end) && end == 0u,
            "empty HDA child ranges are valid and empty");
    require(kernel::pci_audio::validate_hda_child_range(1u, 1u, &end) && end == 2u,
            "ordinary HDA child ranges compute their exclusive end");
    require(kernel::pci_audio::validate_hda_child_range(255u, 1u, &end) && end == 256u,
            "last representable HDA child ID does not wrap its loop counter");
    require(!kernel::pci_audio::validate_hda_child_range(255u, 2u, &end),
            "malformed HDA child range beyond 255 is rejected");
    require(kernel::pci_audio::validate_hda_child_range(128u, 128u, &end) &&
            end == 256u,
            "largest valid HDA child range remains bounded");
    require(!kernel::pci_audio::validate_hda_child_range(200u, 100u, &end),
            "overflowing HDA node or widget range cannot create an unbounded loop");
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

void test_unsupported_dma_failure_is_sticky()
{
    initialize();
    const int32_t controller = add_xhci(3u, 0u, 3u, 0x1022u, 0x1503u);
    record_stage(controller, Stage::HidDiscovery, Status::Unsupported,
                 0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                 "HID_DISCOVERY_NOT_IMPLEMENTED");
    Snapshot snapshot = {};
    copy_snapshot(&snapshot);
    require(!snapshot.firstFailure.present,
            "unsupported HID frontier is not reported as a hardware failure");

    record_stage(controller, Stage::DmaPrerequisites, Status::Unsupported,
                 2u, true, 0u, false, 0u, 0u, 0u, 0u, false,
                 "SCRATCHPAD_COUNT_EXCEEDS_STATIC_CAPACITY");

    copy_snapshot(&snapshot);
    require(snapshot.controllers[0].stages[
                static_cast<uint32_t>(Stage::DmaPrerequisites)].status == Status::Unsupported &&
            snapshot.firstFailure.present &&
            snapshot.firstFailure.stage == Stage::DmaPrerequisites &&
            snapshot.firstFailure.result.status == Status::Unsupported,
            "unsupported DMA prerequisites become the first meaningful failure");

    char formatted[160];
    format_stage_result(snapshot.firstFailure.result, formatted, sizeof(formatted));
    require(std::strstr(formatted, "UNSUPPORTED") != nullptr &&
            std::strstr(formatted, "SCRATCHPAD_COUNT_EXCEEDS_STATIC_CAPACITY") != nullptr,
            "unsupported prerequisite formatting preserves its exact reason");
    format_first_failure(snapshot, formatted, sizeof(formatted));
    require(std::strstr(formatted, "xHCI 03:00.3 DMA prerequisites: UNSUPPORTED") != nullptr &&
            std::strstr(formatted, "SCRATCHPAD_COUNT_EXCEEDS_STATIC_CAPACITY") != nullptr,
            "first-failure panel formatting reports unsupported DMA reason");

    record_stage(controller, Stage::ControllerHalt, Status::Fail,
                 7u, true, 0u, false, 0u, 0u, 0u, 0u, false,
                 "synthetic later failure");
    copy_snapshot(&snapshot);
    require(snapshot.firstFailure.stage == Stage::DmaPrerequisites &&
            snapshot.firstFailure.result.status == Status::Unsupported,
            "later hardware errors do not replace unsupported prerequisite evidence");
    require(std::strcmp(iommu_status_name(IommuStatus::Unknown), "UNKNOWN") == 0 &&
            std::strcmp(iommu_status_name(IommuStatus::NoAcpiRemappingTable),
                        "NO ACPI REMAP TABLE") == 0 &&
            std::strcmp(iommu_status_name(IommuStatus::AcpiRemappingTablePresent),
                        "ACPI REMAP TABLE PRESENT") == 0,
            "IOMMU discovery states preserve unknown versus ACPI evidence");
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

struct IvrsFixture {
    uint8_t bytes[4096];
    uint32_t length;
};

void put_u16(uint8_t* out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8u);
}

void put_u32(uint8_t* out, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; ++i) {
        out[i] = static_cast<uint8_t>(value >> (i * 8u));
    }
}

void put_u64(uint8_t* out, uint64_t value)
{
    put_u32(out, static_cast<uint32_t>(value));
    put_u32(out + 4u, static_cast<uint32_t>(value >> 32u));
}

void begin_ivrs_fixture(IvrsFixture* fixture)
{
    std::memset(fixture, 0, sizeof(*fixture));
    std::memcpy(fixture->bytes, "IVRS", 4u);
    fixture->bytes[8] = 2u;
    fixture->length = kernel::amd_ivrs::IVRS_HEADER_BYTES;
}

uint32_t add_ivhd(IvrsFixture* fixture, uint8_t type, uint16_t iommuId,
                  uint16_t segment, uint64_t mmioBase)
{
    const uint32_t offset = fixture->length;
    const uint32_t headerBytes = type == 0x10u ? 24u : 40u;
    uint8_t* block = fixture->bytes + offset;
    block[0] = type;
    block[1] = 0u;
    put_u16(block + 2u, static_cast<uint16_t>(headerBytes));
    put_u16(block + 4u, iommuId);
    put_u16(block + 6u, 0x40u);
    put_u64(block + 8u, mmioBase);
    put_u16(block + 16u, segment);
    put_u16(block + 18u, 0x1234u);
    if (type == 0x10u) {
        put_u32(block + 20u, 0xAABBCCDDu);
    } else {
        put_u32(block + 20u, 0x01020304u);
        put_u64(block + 24u, 0x1122334455667788ULL);
        put_u64(block + 32u, 0x8877665544332211ULL);
    }
    fixture->length += headerBytes;
    return offset;
}

void add_ivhd_entry4(IvrsFixture* fixture, uint32_t blockOffset,
                     uint8_t type, uint16_t deviceId, uint8_t setting = 0u)
{
    uint8_t* entry = fixture->bytes + fixture->length;
    entry[0] = type;
    put_u16(entry + 1u, deviceId);
    entry[3] = setting;
    fixture->length += 4u;
    put_u16(fixture->bytes + blockOffset + 2u,
            static_cast<uint16_t>(fixture->length - blockOffset));
}

void add_ivhd_entry8(IvrsFixture* fixture, uint32_t blockOffset,
                     uint8_t type, uint16_t deviceId, uint8_t setting,
                     uint32_t extra)
{
    uint8_t* entry = fixture->bytes + fixture->length;
    std::memset(entry, 0, 8u);
    entry[0] = type;
    put_u16(entry + 1u, deviceId);
    entry[3] = setting;
    put_u32(entry + 4u, extra);
    fixture->length += 8u;
    put_u16(fixture->bytes + blockOffset + 2u,
            static_cast<uint16_t>(fixture->length - blockOffset));
}

void add_ivmd(IvrsFixture* fixture, uint8_t type, uint8_t flags,
              uint16_t segment, uint64_t start, uint64_t length)
{
    uint8_t* block = fixture->bytes + fixture->length;
    std::memset(block, 0, 32u);
    block[0] = type;
    block[1] = flags;
    put_u16(block + 2u, 32u);
    put_u16(block + 4u, 0x1234u);
    put_u16(block + 6u, 0x0056u);
    put_u16(block + 8u, segment);
    put_u64(block + 16u, start);
    put_u64(block + 24u, length);
    fixture->length += 32u;
}

void finish_ivrs_fixture(IvrsFixture* fixture)
{
    put_u32(fixture->bytes + 4u, fixture->length);
    fixture->bytes[9] = 0u;
    uint8_t checksum = 0u;
    for (uint32_t i = 0u; i < fixture->length; ++i) {
        checksum = static_cast<uint8_t>(checksum + fixture->bytes[i]);
    }
    fixture->bytes[9] = static_cast<uint8_t>(0u - checksum);
}

void test_ivrs_parser_and_routing()
{
    using namespace kernel::amd_ivrs;
    IvrsFixture fixture;
    Table table = {};
    begin_ivrs_fixture(&fixture);
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::Valid &&
            table.unitCount == 0u,
            "empty but checksummed IVRS table is valid with zero IOMMU units");
    require(route_device(table, 0u, 0x0303u).status == RouteStatus::Unmatched,
            "valid zero-unit IVRS table does not invent device ownership");

    begin_ivrs_fixture(&fixture);
    const uint32_t ivhd = add_ivhd(&fixture, 0x10u, 0x0002u, 0u, 0xFED80000u);
    add_ivhd_entry4(&fixture, ivhd, 0x03u, 0x0300u, 0x11u);
    add_ivhd_entry4(&fixture, ivhd, 0x04u, 0x0307u);
    add_ivhd_entry8(&fixture, ivhd, 0x42u, 0x0400u, 0x22u, 0x00000304u);
    add_ivhd_entry8(&fixture, ivhd, 0x43u, 0x0500u, 0x33u, 0x00000600u);
    add_ivhd_entry4(&fixture, ivhd, 0x04u, 0x0502u);
    add_ivhd_entry8(&fixture, ivhd, 0x46u, 0x0700u, 0x44u, 0x12345678u);
    add_ivhd_entry8(&fixture, ivhd, 0x47u, 0x0800u, 0x55u, 0xAABBCCDDu);
    add_ivhd_entry4(&fixture, ivhd, 0x04u, 0x0801u);
    add_ivhd_entry8(&fixture, ivhd, 0x48u, 0x0900u, 0x66u, 0x00030401u);
    add_ivhd_entry8(&fixture, ivhd, 0xF0u, 0x0A00u, 0u, 0u);
    // Replace the final fixed-size slot with the complete ACPI HID record.
    fixture.length -= 8u;
    put_u16(fixture.bytes + ivhd + 2u,
            static_cast<uint16_t>(fixture.length - ivhd));
    uint8_t* hid = fixture.bytes + fixture.length;
    std::memset(hid, 0, 25u);
    hid[0] = 0xF0u;
    put_u16(hid + 1u, 0x0A00u);
    hid[3] = 0x77u;
    put_u64(hid + 4u, 0x4142434445464748ULL);
    put_u64(hid + 12u, 0x3132333435363738ULL);
    hid[20] = 2u;
    hid[21] = 3u;
    hid[22] = 'u'; hid[23] = 'i'; hid[24] = 'd';
    fixture.length += 25u;
    put_u16(fixture.bytes + ivhd + 2u,
            static_cast<uint16_t>(fixture.length - ivhd));
    add_ivmd(&fixture, 0x20u, 0x0Eu, 0u, 0x100000u, 0x2000u);
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::Valid &&
            table.unitCount == 1u && table.units[0].ivhdType == 0x10u &&
            table.units[0].featureReporting == 0xAABBCCDDu &&
            table.units[0].ruleCount == 7u && table.ivmdCount == 1u,
            "type 10 IVHD preserves device entries, feature reporting, and IVMD memory definition");
    require(table.units[0].rules[1].aliasFirstId == 0x0304u &&
            table.units[0].rules[2].aliasFirstId == 0x0600u,
            "alias select and range payloads use the aligned UsedID offset");
    require(table.units[0].rules[3].extendedData == 0x12345678u &&
            table.units[0].rules[4].extendedData == 0xAABBCCDDu,
            "extended select and range payloads are retained");
    require(table.units[0].rules[6].acpiHid == 0x4142434445464748ULL &&
            table.units[0].rules[6].uidLength == 3u &&
            table.units[0].rules[6].uid[2] == 'd',
            "ACPI HID, UID length, and UID payload use their specification offsets");
    require(table.ivmd[0].flags == 0x0Eu && table.ivmd[0].segment == 0u &&
            table.ivmd[0].start == 0x100000u && table.ivmd[0].length == 0x2000u,
            "IVMD range flags, segment, base, and length are retained");
    Route route = route_device(table, 0u, 0x0300u);
    require(route.status == RouteStatus::Resolved && route.unitIndex == 0,
            "range start requester ID maps to its unique IOMMU unit");
    route = route_device(table, 0u, 0x0307u);
    require(route.status == RouteStatus::Resolved,
            "range end requester ID is inclusive");
    require(route_device(table, 0u, 0x0308u).status == RouteStatus::Unmatched,
            "requester ID outside the IVHD range stays unmatched");
    route = route_device(table, 0u, 0x0400u);
    require(route.status == RouteStatus::Resolved && route.effectiveRequesterId == 0x0304u,
            "alias select uses the aligned used-ID field and reports effective requester ID");
    route = route_device(table, 0u, 0x0501u);
    require(route.status == RouteStatus::Resolved && route.effectiveRequesterId == 0x0601u,
            "alias range maps requester offsets into the corresponding used-ID range");
    require(route_device(table, 0u, 0x0700u).status == RouteStatus::Resolved &&
            route_device(table, 0u, 0x0801u).status == RouteStatus::Resolved,
            "extended select and range entries cover their specified requester IDs");
    route = route_device(table, 0u, 0x0900u);
    require(route.status == RouteStatus::Resolved && route.effectiveRequesterId == 0x0304u,
            "special-device entry reports its used requester ID");
    require(route_device(table, 1u, 0x0303u).status == RouteStatus::Unmatched,
            "PCI segment mismatch does not route a device");

    begin_ivrs_fixture(&fixture);
    const uint32_t type11 = add_ivhd(&fixture, 0x11u, 0x0010u, 2u, 0xFEE00000u);
    add_ivhd_entry4(&fixture, type11, 0x02u, 0x1001u, 0x21u);
    const uint32_t type40 = add_ivhd(&fixture, 0x40u, 0x0020u, 2u, 0xFEE10000u);
    add_ivhd_entry4(&fixture, type40, 0x02u, 0x2001u, 0x31u);
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::Valid &&
            table.unitCount == 2u && table.units[0].ivhdType == 0x11u &&
            table.units[0].attributes == 0x01020304u &&
            table.units[0].efrImage == 0x1122334455667788ULL &&
            table.units[1].ivhdType == 0x40u,
            "IVHD types 11 and 40 preserve their extended hardware attributes");
    require(route_device(table, 2u, 0x1001u).unitIndex == 0 &&
            route_device(table, 2u, 0x2001u).unitIndex == 1,
            "multiple IOMMU units resolve devices by PCI segment and coverage");

    begin_ivrs_fixture(&fixture);
    const uint32_t first = add_ivhd(&fixture, 0x10u, 0x0001u, 0u, 0xFEE00000u);
    add_ivhd_entry4(&fixture, first, 0x01u, 0u);
    const uint32_t second = add_ivhd(&fixture, 0x10u, 0x0002u, 0u, 0xFEE10000u);
    add_ivhd_entry4(&fixture, second, 0x01u, 0u);
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::Valid &&
            route_device(table, 0u, 0x0303u).status == RouteStatus::Ambiguous,
            "overlapping unit coverage is reported as ambiguous instead of choosing the first unit");

    begin_ivrs_fixture(&fixture);
    const uint32_t unsupported = add_ivhd(&fixture, 0x10u, 0u, 0u, 0xFEE00000u);
    add_ivhd_entry4(&fixture, unsupported, 0x02u, 0x0303u);
    add_ivhd_entry4(&fixture, unsupported, 0x05u, 0x0304u);
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::ValidWithUnsupported &&
            table.hasUnsupportedTopology && table.unsupportedStructureCount == 1u &&
            route_device(table, 0u, 0x0303u).status == RouteStatus::Unsupported,
            "unknown bounded device entries are counted and make topology routing unsupported");

    begin_ivrs_fixture(&fixture);
    uint8_t* unknownBlock = fixture.bytes + fixture.length;
    unknownBlock[0] = 0x12u;
    put_u16(unknownBlock + 2u, 4u);
    fixture.length += 4u;
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::ValidWithUnsupported &&
            table.unitCount == 0u && table.unsupportedStructureCount == 1u,
            "unknown IVRS blocks are skipped by bounded length and marked unsupported");

    begin_ivrs_fixture(&fixture);
    finish_ivrs_fixture(&fixture);
    fixture.bytes[20] ^= 0x40u;
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::Malformed,
            "incorrect ACPI checksum is rejected");
    begin_ivrs_fixture(&fixture);
    const uint32_t truncated = add_ivhd(&fixture, 0x10u, 0u, 0u, 0xFEE00000u);
    add_ivhd_entry4(&fixture, truncated, 0x03u, 0x0300u);
    put_u16(fixture.bytes + truncated + 2u, 0x100u);
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::Malformed,
            "subtable length beyond available bytes is rejected");
    begin_ivrs_fixture(&fixture);
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length - 1u, &table) == ParseStatus::Malformed,
            "truncated declared ACPI table is rejected");
    begin_ivrs_fixture(&fixture);
    add_ivmd(&fixture, 0x22u, 0x08u, 0u, UINT64_MAX - 3u, 8u);
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::Malformed,
            "overflowing IVMD physical ranges are rejected");

    begin_ivrs_fixture(&fixture);
    add_ivmd(&fixture, 0x20u, 0u, 0u, 0x1000u, 0u);
    finish_ivrs_fixture(&fixture);
    require(parse(fixture.bytes, fixture.length, &table) == ParseStatus::Malformed,
            "zero-length IVMD memory definitions are malformed");

    IommuUnit unit = {};
    unit.mmioBase = 0xFED80000u;
    require(valid_mmio_resource(unit, IVHD_MMIO_MIN_BYTES) &&
            !valid_mmio_resource(unit, IVHD_MMIO_MIN_BYTES - 1u),
            "IOMMU MMIO resource validation requires the full defined span");
    unit.mmioBase += 1u;
    require(!valid_mmio_resource(unit, IVHD_MMIO_MIN_BYTES),
            "unaligned IOMMU MMIO bases are rejected");
    unit.mmioBase = UINT64_MAX - 0x1FFFu;
    require(!valid_mmio_resource(unit, IVHD_MMIO_MIN_BYTES),
            "IOMMU MMIO span arithmetic overflow is rejected");
    require(classify_translation(true, 1u, IOMMU_CONTROL_IOMMU_ENABLE) ==
                TranslationState::EnabledObserved &&
            classify_translation(true, 1u, 0u) == TranslationState::DisabledObserved &&
            classify_translation(false, 1u, 0u) == TranslationState::Unknown &&
            classify_translation(true, 2u, IOMMU_CONTROL_IOMMU_ENABLE) ==
                TranslationState::Unknown,
            "translation is classified only from a valid supported revision control snapshot");
}

void test_iommu_diagnostic_publication_and_formatting()
{
    using namespace kernel::amd_ivrs;
    using namespace kernel::modern_input_diagnostics;
    initialize();
    const int32_t first = add_xhci(3u, 0u, 3u, 0x1022u, 0x1503u);
    const int32_t second = add_xhci(3u, 0u, 4u, 0x1022u, 0x1504u);
    const int32_t third = add_xhci(4u, 0u, 0u, 0x1022u, 0x1505u);
    record_ivrs_discovery(ParseStatus::Valid, true, true, 1u, 1u, 2u, 0u);
    record_iommu_route(first, 0u, 0x0303u, 0x0303u,
                       RouteStatus::Resolved, 0, 0x0002u, 0xFED80000u);
    record_iommu_registers(first, InspectionStatus::Readable,
                           TranslationState::EnabledObserved, true, true);
    record_iommu_route(second, 0u, 0x0304u, 0x0400u,
                       RouteStatus::Resolved, 0, 0x0002u, 0xFED80000u);
    record_iommu_registers(second, InspectionStatus::Readable,
                           TranslationState::DisabledObserved, true, true);
    record_iommu_route(third, 0u, 0x0400u, 0x0400u,
                       RouteStatus::Ambiguous, -1, 0u, 0u);
    record_iommu_registers(third, InspectionStatus::NotAttempted,
                           TranslationState::Unknown);
    record_dma_safety("DEVICE_DMA_MAPPING_NOT_ESTABLISHED", false);
    record_stage(first, Stage::DmaPrerequisites, Status::Blocked,
                 0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                 "DEVICE_DMA_MAPPING_NOT_ESTABLISHED");
    select_controller(second);
    Snapshot snapshot = {};
    copy_snapshot(&snapshot);
    require(snapshot.controllers[first].iommuRouteStatus == RouteStatus::Resolved &&
            snapshot.controllers[second].effectiveRequesterId == 0x0400u &&
            snapshot.controllers[third].iommuRouteStatus == RouteStatus::Ambiguous,
            "three xHCI controllers retain independent route identities and states");
    require(snapshot.selectedIommuRouteStatus == RouteStatus::Resolved &&
            snapshot.selectedIommuTranslationState == TranslationState::DisabledObserved &&
            !snapshot.dmaMappingEstablished && snapshot.firstFailure.present &&
            snapshot.firstFailure.stage == Stage::DmaPrerequisites,
            "selected controller state stays distinct and uncertain mapping retains the first DMA block");
    char summary[128];
    format_ivrs_summary(snapshot, summary, sizeof(summary));
    require(std::strstr(summary, "ACPI IVRS VALID") != nullptr &&
            std::strstr(summary, "IVHD=1") != nullptr &&
            std::strstr(summary, "remap=YES") != nullptr,
            "IVRS panel summary reports validated table counts and remapping presence");
    char routeText[128];
    format_controller_iommu_route(snapshot.controllers[second], routeText, sizeof(routeText));
    require(std::strstr(routeText, "03:00.4") != nullptr &&
            std::strstr(routeText, "RID=0304") != nullptr &&
            std::strstr(routeText, "alias=0400") != nullptr &&
            std::strstr(routeText, "00:00.2") != nullptr &&
            std::strstr(routeText, "RESOLVED") != nullptr,
            "per-controller panel route includes BDF, segment RID, alias, and unit identity");
    char shortRoute[12];
    format_controller_iommu_route(snapshot.controllers[second], shortRoute,
                                  sizeof(shortRoute));
    require(shortRoute[sizeof(shortRoute) - 1u] == '\0',
            "per-controller route formatting remains terminated when truncated");
    record_ivrs_discovery(ParseStatus::NotPresent, false, false, 0u, 0u, 0u, 0u);
    record_dma_safety("ACPI_REMAP_SCAN_UNKNOWN", false);
    copy_snapshot(&snapshot);
    format_ivrs_summary(snapshot, summary, sizeof(summary));
    require(std::strstr(summary, "remap=UNKNOWN") != nullptr,
            "unknown ACPI scan remains distinct from a confirmed absence of remapping hardware");
}

void test_intel_diagnostic_publication_and_formatting()
{
    using namespace kernel::modern_input_diagnostics;
    initialize();
    const int32_t intel = record_controller(
        0u, 20u, 0u, 0x8086u, 0x06EDu, 0x0Cu, 0x03u, 0x30u,
        ControllerKind::XHCI);
    const int32_t second = record_controller(
        0u, 21u, 0u, 0x8086u, 0x1234u, 0x0Cu, 0x03u, 0x30u,
        ControllerKind::XHCI);
    select_controller(intel);
    record_platform(Platform::Intel);
    record_dmar_discovery(kernel::intel_dmar::ParseStatus::Valid,
                          true, true, 1u, 2u, 1u, 1u, 1u, 0u);
    const uint32_t rawHcsParams2 = (1u << 21u) | (2u << 27u);
    record_capabilities(intel, 0x80u, 0x0110u, 64u, 26u, 32u, true,
                        0u, 34u, IommuStatus::AcpiRemappingTablePresent,
                        rawHcsParams2, 1u, 2u);
    record_scratchpad_allocation(intel, 34u, Status::Pass,
                                 "VERIFIED_PHYSICAL_BACKING");
    record_scratchpad_allocation(second, 34u, Status::Blocked,
                                 "SCRATCHPAD_COUNT_EXCEEDED_STATIC_CAPACITY");
    record_vtd_route(intel, 0u, 0u, 20u, 0u,
                     kernel::intel_dmar::RouteStatus::Resolved, 1,
                     0xFEE00000u, false);
    record_vtd_route(second, 0u, 0u, 21u, 0u,
                     kernel::intel_dmar::RouteStatus::NotCovered, -1,
                     0u, false);
    const kernel::intel_dmar::RegisterSnapshot registers =
        kernel::intel_dmar::classify_registers(
            true, 0x10u, (1ULL << 16u) | (1ULL << 8u), 0u,
            0u, 1u << 31u, 0x1000u, 0u);
    record_vtd_registers(intel, registers);
    record_dma_safety("DEVICE_DMA_MAPPING_NOT_ESTABLISHED", false);
    record_stage(intel, Stage::DmaPrerequisites, Status::Blocked,
                 0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                 "DEVICE_DMA_MAPPING_NOT_ESTABLISHED");
    record_stage(second, Stage::DmaPrerequisites, Status::Fail,
                 0u, false, 0u, false, 0u, 0u, 0u, 0u, false,
                 "second controller failure");

    Snapshot snapshot = {};
    copy_snapshot(&snapshot);
    require(snapshot.platform == Platform::Intel && snapshot.dmarScanValid &&
            snapshot.dmarTablePresent && snapshot.dmarTableCount == 1u &&
            snapshot.drhdUnitCount == 2u && snapshot.rmrrCount == 1u &&
            snapshot.atsrCount == 1u && snapshot.rhsaCount == 1u,
            "Intel platform and bounded DMAR inventory publish independently");
    require(snapshot.controllers[intel].hcsParams2 == rawHcsParams2 &&
            snapshot.controllers[intel].scratchpadHigh == 1u &&
            snapshot.controllers[intel].scratchpadLow == 2u &&
            snapshot.controllers[intel].scratchpadBuffersRequired == 34u &&
            snapshot.controllers[intel].scratchpadCapacity == 34u &&
            snapshot.controllers[intel].scratchpadAllocationStatus == Status::Pass &&
            snapshot.controllers[second].scratchpadAllocationStatus == Status::Blocked,
            "scratchpad observations and per-controller allocation failures are retained");
    require(snapshot.controllers[intel].vtdRouteStatus ==
                kernel::intel_dmar::RouteStatus::Resolved &&
            snapshot.controllers[intel].vtdUnitIndex == 1 &&
            snapshot.controllers[intel].vtdRegisterStatus ==
                kernel::intel_dmar::RegisterStatus::Readable &&
            snapshot.controllers[intel].vtdTranslationState ==
                kernel::intel_dmar::TranslationState::EnabledObserved &&
            snapshot.controllers[second].vtdRouteStatus ==
                kernel::intel_dmar::RouteStatus::NotCovered,
            "multi-controller VT-d route and register states do not alias");
    require(!snapshot.dmaMappingEstablished && snapshot.firstFailure.present &&
            snapshot.firstFailure.controllerIndex == intel &&
            snapshot.dmaDecisionReason != nullptr &&
            std::strcmp(snapshot.dmaDecisionReason,
                        "DEVICE_DMA_MAPPING_NOT_ESTABLISHED") == 0,
            "translation-enabled hardware remains DMA blocked and preserves first failure");

    char summary[128];
    format_dmar_summary(snapshot, summary, sizeof(summary));
    require(std::strstr(summary, "ACPI DMAR VALID") != nullptr &&
            std::strstr(summary, "DRHD=2") != nullptr &&
            std::strstr(summary, "RMRR=1") != nullptr,
            "DMAR panel summary reports table status and structure counts");
    char route[96];
    format_controller_vtd_route(snapshot.controllers[intel], route, sizeof(route));
    require(std::strstr(route, "00:14.0") != nullptr &&
            std::strstr(route, "VT-d=RESOLVED") != nullptr &&
            std::strstr(route, "unit=1") != nullptr,
            "VT-d route formatting includes the Intel controller and unit");
    char registerText[128];
    format_controller_vtd_registers(snapshot.controllers[intel], registerText,
                                    sizeof(registerText));
    require(std::strstr(registerText, "READABLE") != nullptr &&
            std::strstr(registerText, "TES=ENABLED OBSERVED") != nullptr &&
            std::strstr(registerText, "root=CONFIGURED") != nullptr,
            "VT-d register formatter distinguishes state from DMA mapping");
    char tiny[8];
    format_controller_vtd_registers(snapshot.controllers[intel], tiny, sizeof(tiny));
    require(tiny[sizeof(tiny) - 1u] == '\0',
            "VT-d panel formatting stays terminated in a clipped row");

    char ivrs[64];
    format_ivrs_summary(snapshot, ivrs, sizeof(ivrs));
    require(std::strcmp(ivrs, "ACPI IVRS N/A ON INTEL") == 0,
            "Intel diagnostic panel does not treat missing IVRS as an error");
    record_platform(Platform::Amd);
    copy_snapshot(&snapshot);
    require(snapshot.platform == Platform::Amd,
            "AMD classification remains available after Intel diagnostic publication");
    require(intel_dmar_applies_to_platform(Platform::Intel, false) &&
            intel_dmar_applies_to_platform(Platform::Other, true) &&
            !intel_dmar_applies_to_platform(Platform::Other, false) &&
            !intel_dmar_applies_to_platform(Platform::Amd, true),
            "DMAR routing follows Intel tables without overriding the AMD IVRS platform path");
    require(intel_dmar_route_diagnostics_applies(0x8086u, false) &&
            intel_dmar_route_diagnostics_applies(0x1b36u, true) &&
            intel_dmar_route_diagnostics_applies(0x1022u, true) &&
            !intel_dmar_route_diagnostics_applies(0x1b36u, false),
            "a present DMAR table enables read-only xHCI route diagnostics across vendor IDs");
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
    require(snapshot.lastStartupOperation.operation == StartupOperation::None &&
            snapshot.lastStartupOperation.status == StartupOperationStatus::NotInvoked &&
            snapshot.usbHostScanStatus == StartupOperationStatus::NotInvoked &&
            snapshot.xhciInitStatus == StartupOperationStatus::NotInvoked,
            "startup frontier distinguishes uninvoked stages from pending operations");
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
            layout.width <= 560u && layout.height <= 700u && layout.rowCapacity >= 33u,
            "common-screen panel stays bounded instead of covering the desktop");

    test_startup_operation_frontier();
    test_hda_child_range_bounds();
    test_boot_progress_and_presentation();
    test_failure_before_desktop_initialization();
    test_unsupported_dma_failure_is_sticky();
    test_snapshot_reset();
    test_concurrent_publication();
    test_ivrs_parser_and_routing();
    test_iommu_diagnostic_publication_and_formatting();
    test_intel_diagnostic_publication_and_formatting();
    std::printf("Modern input diagnostics tests PASS (%d assertions).\n", g_assertions);
    return 0;
}
