// Conservative NVMe PCIe block transport.
//
// The current profile registers namespace 1 from each discovered controller,
// uses one serialized I/O command at a time, and transfers through a bounded
// aligned bounce page. A queue/controller fault quarantines that registration;
// the driver does not reset hardware or replay an ambiguous command.

#include "include/kernel/nvme.h"
#include "include/kernel/nvme_logic.h"
#include "include/kernel/block_device.h"
#include "include/kernel/arch.h"
#include "include/kernel/mmio.h"
#include "include/kernel/pit.h"
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
#include "include/kernel/serial_debug.h"
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#define NVME_ALIGNED(n) __declspec(align(n))
#else
#define NVME_ALIGNED(n) __attribute__((aligned(n)))
#endif

namespace kernel {
namespace nvme {
namespace {

static const uint8_t kMaxControllers = MAX_NVME_DEVICES;
// NVMe requires register space through the first I/O queue doorbells. Restrict
// mapping to the minimum 8 KiB aperture rather than mapping unrelated MMIO
// following a small BAR; larger reported doorbell strides fail closed.
static const uint32_t kMappedBarBytes = 0x2000u;
static const uint32_t kCommandTransferBytes = 4096u;
static const uint32_t kMaxBlockTransferBytes = 128u * 1024u;
static const uint32_t kCommandTimeoutTicks = 3000u; // 30 seconds at 100 Hz.
static const uint32_t kFlushTimeoutTicks = 6000u;   // 60 seconds at 100 Hz.
static const uint32_t kAdminTimeoutTicks = 3000u;
static const uint32_t kReadyTimeoutTicks = 12000u;  // 120 seconds, hard cap.
static const uint32_t kLockPollLimit = 25000000u;
static const uint32_t kFallbackPollLimit = 250000000u;
static const uint64_t kDmaPhysicalLimit = 0x000FFFFFFFFFFFFFULL;
static const uint32_t kControllerTimeoutDefaultTicks = 1000u;
static const uint32_t kControllerDoorbellOffset = 0x1000u;
static const uint32_t kCstsRdy = 1u;
static const uint32_t kCstsCfs = 2u;
static const uint32_t kCcEnable = 1u;
static const uint32_t kCcIoSqEntrySize = 6u << 16;
static const uint32_t kCcIoCqEntrySize = 4u << 20;
static const uint32_t kMmioFlags = mmio::MAP_FLAG_NON_USER |
    mmio::MAP_FLAG_NO_EXEC | mmio::MAP_FLAG_UNCACHED;

struct QueuePair {
    NVME_ALIGNED(4096) SubmissionEntry submission[NVME_QUEUE_DEPTH];
    NVME_ALIGNED(4096) CompletionEntry completion[NVME_QUEUE_DEPTH];
    logic::QueueOwnership ownership;
    uint16_t depth;
    uint16_t queueId;
};

struct IoDiagnostic {
    bool valid;
    bool submitted;
    block::Status status;
    uint8_t stage;
    uint8_t opcode;
    uint8_t statusCodeType;
    uint8_t statusCode;
    uint16_t queueId;
    uint16_t commandId;
    uint16_t submissionHead;
    uint16_t rawStatus;
    uint32_t controllerStatus;
    uint64_t lba;
    uint32_t requestedSectors;
    uint32_t completedSectors;
};

struct Controller {
    bool active;
    bool online;
    bool fatal;
    uint8_t pciBus;
    uint8_t pciDevice;
    uint8_t pciFunction;
    uint8_t doorbellStrideShift;
    uint16_t queueDepth;
    uint32_t version;
    uint64_t capabilities;
    uint64_t bar0Physical;
    uint64_t bar0Virtual;
    uint64_t kernelPhysicalBase;
    uint32_t globalBlockIndex;
    uint64_t registrationId;
    uint32_t identifyNamespaceCount;
    uint8_t volatileWriteCache;
    bool vwcEnabledKnown;
    bool vwcEnabled;
    volatile uint32_t ioLock;
    QueuePair admin;
    QueuePair io;
    NVME_ALIGNED(4096) uint8_t identifyBuffer[4096];
    NVME_ALIGNED(4096) uint8_t ioBounce[4096];
    IoDiagnostic diagnostic;
};

static_assert(sizeof(SubmissionEntry) == 64u, "NVMe SQ entries are 64 bytes");
static_assert(sizeof(CompletionEntry) == 16u, "NVMe CQ entries are 16 bytes");
static_assert(sizeof(IdentifyController) == 4096u,
              "NVMe Identify Controller data is one page");
static_assert(sizeof(IdentifyNamespace) == 4096u,
              "NVMe Identify Namespace data is one page");

static NVMeDevice s_devices[kMaxControllers];
static Controller s_controllers[kMaxControllers];
static uint8_t s_deviceCount;
static bool s_initialized;
static uint64_t s_kernelPhysicalBase = 0x100000ULL;
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
static uint8_t s_proofDmaTraceCount;
#endif

static void memzero(void* destination, size_t length)
{
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

static void memcopy(void* destination, const void* source, size_t length)
{
    uint8_t* dst = static_cast<uint8_t*>(destination);
    const uint8_t* src = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < length; ++i) dst[i] = src[i];
}

static bool bytes_nonzero(const uint8_t* bytes, uint32_t length)
{
    return logic::is_nonzero_identifier(bytes, length);
}

static void fix_nvme_string(char* value, uint32_t length)
{
    int32_t end = static_cast<int32_t>(length) - 1;
    while (end >= 0 && (value[end] == ' ' || value[end] == '\0'))
        value[end--] = '\0';
}

static uint32_t nvme_read32(uint64_t base, uint32_t offset)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + offset));
    return *reg;
}

static void nvme_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(
        static_cast<uintptr_t>(base + offset));
    *reg = value;
}

static uint64_t nvme_read64(uint64_t base, uint32_t offset)
{
    const uint32_t low = nvme_read32(base, offset);
    const uint32_t high = nvme_read32(base, offset + 4u);
    return (static_cast<uint64_t>(high) << 32) | low;
}

static void nvme_write64(uint64_t base, uint32_t offset, uint64_t value)
{
    nvme_write32(base, offset, static_cast<uint32_t>(value));
    nvme_write32(base, offset + 4u, static_cast<uint32_t>(value >> 32));
}

static void memory_barrier()
{
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
    _mm_mfence();
#endif
}

static uint64_t virtual_to_physical(const void* pointer)
{
    const uint64_t virtualAddress = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(pointer));
#if defined(ARCH_AMD64)
    // Resolve the address through the active page tables. A load-base delta
    // is not sufficient when a UEFI memory map or large-page mapping differs
    // from the kernel's link-time virtual range.
    const uint64_t rootPhysical = arch::read_cr3() & 0x000FFFFFFFFFF000ULL;
    if (rootPhysical == 0u) return 0u;
    const volatile uint64_t* pml4 =
        reinterpret_cast<const volatile uint64_t*>(rootPhysical);
    const uint64_t pml4e = pml4[(virtualAddress >> 39) & 0x1FFu];
    if ((pml4e & 1u) == 0u) return 0u;
    const volatile uint64_t* pdpt = reinterpret_cast<
        const volatile uint64_t*>(pml4e & 0x000FFFFFFFFFF000ULL);
    const uint64_t pdpte = pdpt[(virtualAddress >> 30) & 0x1FFu];
    if ((pdpte & 1u) == 0u) return 0u;
    if ((pdpte & (1u << 7)) != 0u)
        return (pdpte & 0x000FFFFFC0000000ULL) |
            (virtualAddress & 0x3FFFFFFFULL);
    const volatile uint64_t* pd = reinterpret_cast<
        const volatile uint64_t*>(pdpte & 0x000FFFFFFFFFF000ULL);
    const uint64_t pde = pd[(virtualAddress >> 21) & 0x1FFu];
    if ((pde & 1u) == 0u) return 0u;
    if ((pde & (1u << 7)) != 0u)
        return (pde & 0x000FFFFFFFE00000ULL) |
            (virtualAddress & 0x1FFFFFULL);
    const volatile uint64_t* pt = reinterpret_cast<
        const volatile uint64_t*>(pde & 0x000FFFFFFFFFF000ULL);
    const uint64_t pte = pt[(virtualAddress >> 12) & 0x1FFu];
    if ((pte & 1u) == 0u) return 0u;
    return (pte & 0x000FFFFFFFFFF000ULL) | (virtualAddress & 0xFFFu);
#else
    if (virtualAddress < 0x100000ULL ||
        virtualAddress - 0x100000ULL > (~0ULL - s_kernelPhysicalBase))
        return 0;
    return s_kernelPhysicalBase + (virtualAddress - 0x100000ULL);
#endif
}

static bool dma_range_valid(uint64_t physicalAddress, uint64_t length,
                            uint64_t alignment)
{
    if (physicalAddress == 0u || length == 0u || alignment == 0u ||
        (physicalAddress % alignment) != 0u ||
        physicalAddress > kDmaPhysicalLimit ||
        length - 1u > kDmaPhysicalLimit - physicalAddress)
        return false;
    return true;
}

static bool physical_address(const void* pointer, uint64_t length,
                             uint64_t alignment, uint64_t& result)
{
    result = virtual_to_physical(pointer);
    return dma_range_valid(result, length, alignment);
}

static bool acquire_io_lock(Controller& controller)
{
    for (uint32_t i = 0; i < kLockPollLimit; ++i) {
#if defined(__GNUC__) || defined(__clang__)
        if (__sync_lock_test_and_set(&controller.ioLock, 1u) == 0u) {
            __sync_synchronize();
            return true;
        }
#elif defined(_MSC_VER)
        if (_InterlockedExchange(
                reinterpret_cast<volatile long*>(&controller.ioLock), 1) == 0) {
            _ReadWriteBarrier();
            return true;
        }
#endif
    }
    return false;
}

static void release_io_lock(Controller& controller)
{
#if defined(__GNUC__) || defined(__clang__)
    __sync_synchronize();
    __sync_lock_release(&controller.ioLock);
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
    _InterlockedExchange(
        reinterpret_cast<volatile long*>(&controller.ioLock), 0);
#endif
}

static uint32_t doorbell_offset(const Controller& controller, uint16_t queueId,
                                bool completion)
{
    const uint32_t stride = 4u << controller.doorbellStrideShift;
    return kControllerDoorbellOffset +
        (2u * queueId + (completion ? 1u : 0u)) * stride;
}

static void ring_submission_doorbell(const Controller& controller,
                                     const QueuePair& queue,
                                     uint16_t tail)
{
    nvme_write32(controller.bar0Virtual,
                 doorbell_offset(controller, queue.queueId, false), tail);
}

static void ring_completion_doorbell(const Controller& controller,
                                     const QueuePair& queue,
                                     uint16_t head)
{
    nvme_write32(controller.bar0Virtual,
                 doorbell_offset(controller, queue.queueId, true), head);
}

static void set_diagnostic(Controller& controller, block::Status status,
                           uint8_t stage, uint8_t opcode, uint16_t queueId,
                           bool submitted, const logic::CompletionResult& result,
                           uint32_t csts, uint64_t lba, uint32_t sectors,
                           uint32_t completed)
{
    IoDiagnostic& diagnostic = controller.diagnostic;
    diagnostic.valid = true;
    diagnostic.submitted = submitted;
    diagnostic.status = status;
    diagnostic.stage = stage;
    diagnostic.opcode = opcode;
    diagnostic.statusCodeType = result.statusCodeType;
    diagnostic.statusCode = result.statusCode;
    diagnostic.queueId = queueId;
    diagnostic.commandId = result.commandId;
    diagnostic.submissionHead = result.submissionHead;
    diagnostic.rawStatus = result.rawStatus;
    diagnostic.controllerStatus = csts;
    diagnostic.lba = lba;
    diagnostic.requestedSectors = sectors;
    diagnostic.completedSectors = completed;
}

static bool get_io_diagnostic(uint8_t driverIndex,
                              block::TransportIoDiagnostic& out)
{
    memzero(&out, sizeof(out));
    if (driverIndex >= kMaxControllers || !s_devices[driverIndex].active)
        return false;
    const IoDiagnostic& source = s_controllers[driverIndex].diagnostic;
    if (!source.valid) return false;
    out.valid = true;
    out.stage = source.stage;
    out.commandOpcode = source.opcode;
    out.commandSlot = static_cast<uint8_t>(source.queueId);
    out.statusCode = source.statusCode;
    out.transportStatus = source.rawStatus;
    out.transportError = source.controllerStatus;
    out.failingLba = source.lba;
    out.completedSectors = source.completedSectors;
    out.dataSectorsTransferred = source.completedSectors;
    out.nvmeQueueId = source.queueId;
    out.nvmeCommandId = source.commandId;
    out.nvmeSubmissionHead = source.submissionHead;
    out.nvmeRawStatus = source.rawStatus;
    out.nvmeStatusCodeType = source.statusCodeType;
    out.nvmeStatusCode = source.statusCode;
    out.nvmeControllerStatus = source.controllerStatus;
    out.nvmeSubmissionOccurred = source.submitted;
    return true;
}

struct CommandResult {
    logic::CompletionResult completion;
    bool submitted;
    bool controllerFault;
    uint32_t completionDword0;
    uint32_t controllerStatus;
};

static CommandResult submit_and_wait(Controller& controller, QueuePair& queue,
                                     SubmissionEntry& command,
                                     uint32_t timeoutTicks)
{
    CommandResult result = {};
    result.completion.kind = logic::COMPLETION_NOT_READY;
    if (!controller.online || queue.depth < 2u || queue.ownership.poisoned) {
        result.controllerFault = !controller.online || queue.ownership.poisoned;
        return result;
    }

    uint16_t commandId = 0u;
    uint16_t slot = 0u;
    uint16_t newTail = 0u;
    const logic::QueueBeginResult begin = logic::begin_command(
        queue.ownership, queue.depth, commandId, slot, newTail);
    if (begin != logic::QUEUE_BEGIN_OK) {
        if (begin == logic::QUEUE_BEGIN_BUSY || begin == logic::QUEUE_BEGIN_INVALID) {
            logic::poison_queue(queue.ownership);
            controller.online = false;
            controller.fatal = true;
            result.controllerFault = true;
        }
        return result;
    }

    command.commandId = commandId;
    memcopy(&queue.submission[slot], &command, sizeof(command));
    memory_barrier();
#if defined(GXOS_DM16_NVME_PRIVATE_PROOF)
    if (queue.queueId == 1u && command.opcode == NVME_IO_WRITE &&
        s_proofDmaTraceCount == 4u) {
        const uint64_t pauseStart = pit::ticks();
        serial::puts("[DM16-NVME-DMA] submit-pause=START sqVirtual=");
        serial::put_hex64(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
            &queue.submission[slot])));
        serial::puts(" sqPhysical=");
        serial::put_hex64(virtual_to_physical(&queue.submission[slot]));
        serial::puts(" slot="); serial::put_hex32(slot);
        serial::puts(" cid="); serial::put_hex32(commandId);
        serial::puts(" tail="); serial::put_hex32(newTail);
        serial::puts(" prp1="); serial::put_hex64(command.prp1);
        serial::puts(" ticks=2000\n");
        while (pit::ticks() - pauseStart < 2000u) { }
        serial::puts("[DM16-NVME-DMA] submit-pause=END\n");
    }
#endif
    result.submitted = true;
    ring_submission_doorbell(controller, queue, newTail);

    const uint64_t startTick = pit::ticks();
    uint64_t previousTick = startTick;
    bool clockAdvanced = false;
    volatile CompletionEntry* cqe = nullptr;
    for (uint32_t poll = 0; poll < kFallbackPollLimit; ++poll) {
        const uint32_t csts = nvme_read32(controller.bar0Virtual, NVME_CSTS);
        result.controllerStatus = csts;
        if ((csts & kCstsCfs) != 0u || (csts & kCstsRdy) == 0u) {
            logic::poison_queue(queue.ownership);
            controller.online = false;
            controller.fatal = true;
            result.controllerFault = true;
            result.completion.kind = logic::COMPLETION_CORRUPT;
            result.completion.commandId = commandId;
            return result;
        }

        memory_barrier();
        cqe = &queue.completion[queue.ownership.completionHead];
        CompletionEntry observed = {};
        observed.result = cqe->result;
        observed.reserved = cqe->reserved;
        observed.sqHead = cqe->sqHead;
        observed.sqId = cqe->sqId;
        observed.commandId = cqe->commandId;
        observed.status = cqe->status;
        memory_barrier();

        const logic::CompletionResult completion = logic::consume_completion(
            queue.ownership, observed, queue.queueId, queue.depth);
        if (completion.kind == logic::COMPLETION_NOT_READY) {
            const uint64_t now = pit::ticks();
            if (now != previousTick) clockAdvanced = true;
            previousTick = now;
            if (clockAdvanced && now >= startTick &&
                now - startTick >= timeoutTicks)
                break;
            continue;
        }

        result.completion = completion;
        if (completion.kind == logic::COMPLETION_CORRUPT) {
            logic::poison_queue(queue.ownership);
            controller.online = false;
            controller.fatal = true;
            result.controllerFault = true;
            return result;
        }

        result.completionDword0 = observed.result;
        ring_completion_doorbell(controller, queue,
                                 queue.ownership.completionHead);
        result.controllerStatus = nvme_read32(controller.bar0Virtual, NVME_CSTS);
        return result;
    }

    logic::poison_queue(queue.ownership);
    controller.online = false;
    controller.fatal = true;
    result.controllerFault = true;
    result.completion.kind = logic::COMPLETION_NOT_READY;
    result.completion.commandId = commandId;
    result.controllerStatus = nvme_read32(controller.bar0Virtual, NVME_CSTS);
    if (cqe) {
        result.completion.rawStatus = cqe->status;
        result.completion.submissionQueueId = cqe->sqId;
        result.completion.commandId = cqe->commandId;
        result.completion.submissionHead = cqe->sqHead;
        result.completion.statusCode = static_cast<uint8_t>((cqe->status >> 1) & 0xFFu);
        result.completion.statusCodeType = static_cast<uint8_t>((cqe->status >> 9) & 0x07u);
    }
    return result;
}

static uint32_t controller_ready_timeout(const Controller& controller)
{
    uint32_t timeout = static_cast<uint32_t>((controller.capabilities >> 24) & 0xFFu);
    if (timeout == 0u) return kControllerTimeoutDefaultTicks;
    timeout *= 50u; // CAP.TO units are 500 ms at the configured 100 Hz PIT.
    return timeout > kReadyTimeoutTicks ? kReadyTimeoutTicks : timeout;
}

static bool wait_controller_ready(Controller& controller, bool ready,
                                  uint32_t timeoutTicks)
{
    const uint64_t startTick = pit::ticks();
    uint64_t previousTick = startTick;
    bool clockAdvanced = false;
    for (uint32_t poll = 0; poll < kFallbackPollLimit; ++poll) {
        const uint32_t status = nvme_read32(controller.bar0Virtual, NVME_CSTS);
        if ((status & kCstsCfs) != 0u) return false;
        if (((status & kCstsRdy) != 0u) == ready) return true;
        const uint64_t now = pit::ticks();
        if (now != previousTick) clockAdvanced = true;
        previousTick = now;
        if (clockAdvanced && now >= startTick && now - startTick >= timeoutTicks)
            return false;
    }
    return false;
}

static bool initialize_controller(Controller& controller, uint64_t bar0Physical,
                                  uint8_t bus, uint8_t device, uint8_t function)
{
    memzero(&controller, sizeof(controller));
    controller.pciBus = bus;
    controller.pciDevice = device;
    controller.pciFunction = function;
    controller.bar0Physical = bar0Physical;
    controller.kernelPhysicalBase = s_kernelPhysicalBase;

#if defined(ARCH_AMD64)
    mmio::MappingReport mapping = {};
    uint64_t mapped = 0u;
    if (!mmio::mapForDevice(bar0Physical, kMappedBarBytes, &mapped,
                            &mapping, kMmioFlags))
        return false;
    controller.bar0Virtual = mapped;
#else
    (void)bar0Physical;
    return false;
#endif

    controller.capabilities = nvme_read64(controller.bar0Virtual, NVME_CAP);
    controller.version = nvme_read32(controller.bar0Virtual, NVME_VS);
    const uint8_t minimumPageSize = static_cast<uint8_t>(
        (controller.capabilities >> 48) & 0x0Fu);
    const uint32_t maximumQueueEntries = static_cast<uint32_t>(
        (controller.capabilities & 0xFFFFu) + 1u);
    if (minimumPageSize != 0u || maximumQueueEntries < 2u)
        return false; // The driver uses 4 KiB pages and physically contiguous queues.
    if (((controller.capabilities >> 37) & 1u) == 0u)
        return false; // This driver requires the NVM command set.

    controller.doorbellStrideShift = static_cast<uint8_t>(
        (controller.capabilities >> 32) & 0x0Fu);
    controller.queueDepth = static_cast<uint16_t>(
        maximumQueueEntries < NVME_QUEUE_DEPTH
            ? maximumQueueEntries : NVME_QUEUE_DEPTH);
    if (doorbell_offset(controller, 1u, true) + 4u > kMappedBarBytes)
        return false;

    uint64_t adminSqPhysical = 0u;
    uint64_t adminCqPhysical = 0u;
    uint64_t identifyPhysical = 0u;
    uint64_t bouncePhysical = 0u;
    if (!physical_address(controller.admin.submission,
            sizeof(controller.admin.submission), 4096u, adminSqPhysical) ||
        !physical_address(controller.admin.completion,
            sizeof(controller.admin.completion), 4096u, adminCqPhysical) ||
        !physical_address(controller.identifyBuffer,
            sizeof(controller.identifyBuffer), 4096u, identifyPhysical) ||
        !physical_address(controller.ioBounce,
            sizeof(controller.ioBounce), 4096u, bouncePhysical))
        return false;

    controller.admin.depth = controller.queueDepth;
    controller.admin.queueId = 0u;
    logic::reset_queue(controller.admin.ownership);
    memzero(controller.admin.submission, sizeof(controller.admin.submission));
    memzero(controller.admin.completion, sizeof(controller.admin.completion));

    nvme_write32(controller.bar0Virtual, NVME_CC, 0u);
    if (!wait_controller_ready(controller, false,
                               controller_ready_timeout(controller)))
        return false;

    const uint32_t aqa =
        (static_cast<uint32_t>(controller.queueDepth - 1u) << 16) |
        static_cast<uint32_t>(controller.queueDepth - 1u);
    nvme_write64(controller.bar0Virtual, NVME_ASQ, adminSqPhysical);
    nvme_write64(controller.bar0Virtual, NVME_ACQ, adminCqPhysical);
    nvme_write32(controller.bar0Virtual, NVME_AQA, aqa);

    const uint32_t cc = kCcEnable | kCcIoSqEntrySize | kCcIoCqEntrySize;
    nvme_write32(controller.bar0Virtual, NVME_CC, cc);
    if (!wait_controller_ready(controller, true,
                               controller_ready_timeout(controller)))
        return false;
    controller.online = true;
    controller.active = true;
    (void)identifyPhysical;
    return true;
}

static block::Status admin_command(Controller& controller,
                                   SubmissionEntry& command,
                                   uint32_t timeoutTicks,
                                   uint32_t* resultDword0 = nullptr)
{
    const CommandResult result = submit_and_wait(
        controller, controller.admin, command, timeoutTicks);
    const block::Status status = !result.submitted
        ? (result.controllerFault ? block::BLOCK_ERR_NO_MEDIA
                                  : block::BLOCK_ERR_NOT_READY)
        : (result.completion.kind == logic::COMPLETION_SUCCESS
            ? block::BLOCK_OK : block::BLOCK_ERR_IO);
    if (resultDword0) *resultDword0 = result.completionDword0;
    return status;
}

static bool create_io_queues(Controller& controller)
{
    uint64_t cqPhysical = 0u;
    uint64_t sqPhysical = 0u;
    if (!physical_address(controller.io.completion,
            sizeof(controller.io.completion), 4096u, cqPhysical) ||
        !physical_address(controller.io.submission,
            sizeof(controller.io.submission), 4096u, sqPhysical))
        return false;

    controller.io.depth = controller.queueDepth;
    controller.io.queueId = 1u;
    logic::reset_queue(controller.io.ownership);
    memzero(controller.io.submission, sizeof(controller.io.submission));
    memzero(controller.io.completion, sizeof(controller.io.completion));

    SubmissionEntry command = {};
    command.opcode = NVME_ADM_CREATE_IOCQ;
    command.prp1 = cqPhysical;
    command.cdw10 = (static_cast<uint32_t>(controller.io.depth - 1u) << 16) | 1u;
    command.cdw11 = 0x01u; // Physically contiguous; interrupts disabled.
    if (admin_command(controller, command, kAdminTimeoutTicks) != block::BLOCK_OK)
        return false;

    command = {};
    command.opcode = NVME_ADM_CREATE_IOSQ;
    command.prp1 = sqPhysical;
    command.cdw10 = (static_cast<uint32_t>(controller.io.depth - 1u) << 16) | 1u;
    command.cdw11 = (1u << 16) | 0x01u; // SQID 1, CQID 1, physically contiguous.
    return admin_command(controller, command, kAdminTimeoutTicks) == block::BLOCK_OK;
}

static bool identify_controller(Controller& controller,
                                IdentifyController& identify)
{
    uint64_t identifyPhysical = 0u;
    if (!physical_address(controller.identifyBuffer,
            sizeof(controller.identifyBuffer), 4096u, identifyPhysical))
        return false;
    SubmissionEntry command = {};
    command.opcode = NVME_ADM_IDENTIFY;
    command.prp1 = identifyPhysical;
    command.cdw10 = 1u; // CNS = Identify Controller.
    if (admin_command(controller, command, kAdminTimeoutTicks) != block::BLOCK_OK)
        return false;
    memcopy(&identify, controller.identifyBuffer, sizeof(identify));
    return true;
}

static bool identify_namespace(Controller& controller, uint32_t nsid,
                               IdentifyNamespace& identify)
{
    uint64_t identifyPhysical = 0u;
    if (!physical_address(controller.identifyBuffer,
            sizeof(controller.identifyBuffer), 4096u, identifyPhysical))
        return false;
    SubmissionEntry command = {};
    command.opcode = NVME_ADM_IDENTIFY;
    command.nsid = nsid;
    command.prp1 = identifyPhysical;
    command.cdw10 = 0u; // CNS = Identify Namespace.
    if (admin_command(controller, command, kAdminTimeoutTicks) != block::BLOCK_OK)
        return false;
    memcopy(&identify, controller.identifyBuffer, sizeof(identify));
    return true;
}

static void identify_vwc_state(Controller& controller,
                               const IdentifyController& identify)
{
    controller.volatileWriteCache = identify.volatileWriteCache;
    if ((identify.volatileWriteCache & 1u) == 0u) {
        controller.vwcEnabledKnown = true;
        controller.vwcEnabled = false;
        return;
    }

    SubmissionEntry command = {};
    command.opcode = NVME_ADM_GET_FEATURES;
    command.cdw10 = NVME_FEATURE_VOLATILE_WRITE_CACHE;
    uint32_t featureValue = 0u;
    if (admin_command(controller, command, kAdminTimeoutTicks,
                      &featureValue) == block::BLOCK_OK) {
        controller.vwcEnabledKnown = true;
        controller.vwcEnabled = (featureValue & 1u) != 0u;
    }
}

static block::Status transfer(uint8_t driverIndex, uint64_t lba,
                              uint32_t count, void* readBuffer,
                              const void* writeBuffer, bool write)
{
    if (driverIndex >= kMaxControllers || !s_devices[driverIndex].active)
        return block::BLOCK_ERR_INVALID;
    Controller& controller = s_controllers[driverIndex];
    NVMeDevice& device = s_devices[driverIndex];
    if (!controller.online) return block::BLOCK_ERR_NO_MEDIA;
    if (!logic::checked_lba_range(device.totalSectors, lba, count) ||
        (!write && !readBuffer) || (write && !writeBuffer))
        return block::BLOCK_ERR_INVALID;
    const uint64_t totalBytes = static_cast<uint64_t>(count) * device.sectorSize;
    if (totalBytes == 0u || totalBytes > kMaxBlockTransferBytes ||
        totalBytes > UINTPTR_MAX ||
        reinterpret_cast<uintptr_t>(write ? writeBuffer : readBuffer) >
            UINTPTR_MAX - static_cast<uintptr_t>(totalBytes))
        return block::BLOCK_ERR_INVALID;
    if (!acquire_io_lock(controller)) return block::BLOCK_ERR_NOT_READY;
    if (!controller.online) {
        release_io_lock(controller);
        return block::BLOCK_ERR_NO_MEDIA;
    }

    const uint8_t opcode = write ? NVME_IO_WRITE : NVME_IO_READ;
    uint32_t remaining = count;
    uint64_t currentLba = lba;
    uint32_t completedSectors = 0u;
    uint8_t* readBytes = static_cast<uint8_t*>(readBuffer);
    const uint8_t* writeBytes = static_cast<const uint8_t*>(writeBuffer);
    block::Status status = block::BLOCK_OK;
    bool controllerFault = false;
    uint64_t bouncePhysical = 0u;
    if (!physical_address(controller.ioBounce, sizeof(controller.ioBounce),
                          4096u, bouncePhysical)) {
        release_io_lock(controller);
        return block::BLOCK_ERR_INVALID;
    }

    while (remaining != 0u) {
        const uint32_t chunk = logic::transfer_chunk(remaining,
                                                      device.sectorSize);
        if (chunk == 0u || !logic::command_fits_mdts(device.mdts, 0u,
                chunk * device.sectorSize)) {
            status = write && completedSectors != 0u
                ? block::BLOCK_ERR_WRITE_UNCERTAIN : block::BLOCK_ERR_INVALID;
            break;
        }
        const uint32_t byteCount = chunk * device.sectorSize;
        if (write) memcopy(controller.ioBounce, writeBytes, byteCount);

        SubmissionEntry command = {};
        if (!logic::build_rw_command(command, opcode, device.nsid, 0u,
                currentLba, chunk, bouncePhysical, byteCount)) {
            status = write && completedSectors != 0u
                ? block::BLOCK_ERR_WRITE_UNCERTAIN : block::BLOCK_ERR_INVALID;
            break;
        }
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
        const bool traceDma = s_proofDmaTraceCount < 8u &&
            currentLba >= device.totalSectors - 16u;
        if (traceDma) {
            serial::puts("[DM16-NVME-DMA] submit op=");
            serial::puts(write ? "write" : "read");
            serial::puts(" lba="); serial::put_hex64(currentLba);
            serial::puts(" blocks="); serial::put_hex32(chunk);
            serial::puts(" prp1="); serial::put_hex64(command.prp1);
            serial::puts(" bytes="); serial::put_hex32(byteCount);
            serial::puts(" data=");
            for (uint32_t i = 0; i < 16u; ++i)
                serial::put_hex8(controller.ioBounce[i]);
            const void* physicalAlias = reinterpret_cast<const void*>(
                static_cast<uintptr_t>(bouncePhysical));
            const uint64_t physicalAliasTranslation =
                virtual_to_physical(physicalAlias);
            serial::puts(" physicalAliasTranslation=");
            serial::put_hex64(physicalAliasTranslation);
            if (physicalAliasTranslation == bouncePhysical) {
                const volatile uint8_t* alias = static_cast<
                    const volatile uint8_t*>(physicalAlias);
                serial::puts(" physicalAliasData=");
                for (uint32_t i = 0; i < 16u; ++i)
                    serial::put_hex8(alias[i]);
            }
            serial::putc('\n');
        }
#endif
        const CommandResult commandResult = submit_and_wait(
            controller, controller.io, command, kCommandTimeoutTicks);
        logic::CompletionResult completion = commandResult.completion;
        if (!commandResult.submitted) {
            status = commandResult.controllerFault
                ? (write ? block::BLOCK_ERR_WRITE_UNCERTAIN
                         : block::BLOCK_ERR_NO_MEDIA)
                : (write && completedSectors != 0u
                    ? block::BLOCK_ERR_WRITE_UNCERTAIN
                    : block::BLOCK_ERR_NOT_READY);
        } else if (write) {
            status = logic::write_result(completion,
                                          !commandResult.controllerFault);
        } else {
            status = logic::read_result(completion,
                                         !commandResult.controllerFault);
        }

        set_diagnostic(controller, status, 1u, opcode, controller.io.queueId,
            commandResult.submitted, completion, commandResult.controllerStatus,
            currentLba, count, completedSectors);
        if (status != block::BLOCK_OK) {
            controllerFault = commandResult.controllerFault;
            break;
        }

        if (write) writeBytes += byteCount;
        else {
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
            if (traceDma) {
                serial::puts("[DM16-NVME-DMA] read-data=");
                for (uint32_t i = 0; i < 16u; ++i)
                    serial::put_hex8(controller.ioBounce[i]);
                serial::putc('\n');
            }
#endif
            memcopy(readBytes, controller.ioBounce, byteCount);
            readBytes += byteCount;
        }
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
        if (traceDma) ++s_proofDmaTraceCount;
#endif
        currentLba += chunk;
        remaining -= chunk;
        completedSectors += chunk;
    }

    release_io_lock(controller);
    if (controllerFault) {
        controller.online = false;
        (void)block::mark_device_offline(controller.globalBlockIndex,
                                         controller.registrationId);
    }
    return status;
}

static block::Status nvme_read_sectors(uint8_t driverIndex, uint64_t lba,
                                       uint32_t count, void* buffer)
{
    return transfer(driverIndex, lba, count, buffer, nullptr, false);
}

static block::Status nvme_write_sectors(uint8_t driverIndex, uint64_t lba,
                                        uint32_t count, const void* buffer)
{
    return transfer(driverIndex, lba, count, nullptr, buffer, true);
}

static block::Status nvme_flush(uint8_t driverIndex)
{
    if (driverIndex >= kMaxControllers || !s_devices[driverIndex].active)
        return block::BLOCK_ERR_INVALID;
    Controller& controller = s_controllers[driverIndex];
    NVMeDevice& device = s_devices[driverIndex];
    if (!controller.online) return block::BLOCK_ERR_DURABILITY_UNVERIFIED;
    if (!acquire_io_lock(controller))
        return block::BLOCK_ERR_DURABILITY_UNVERIFIED;

    SubmissionEntry command = {};
    logic::build_flush_command(command, device.nsid, 0u);
    const CommandResult result = submit_and_wait(controller, controller.io,
                                                 command, kFlushTimeoutTicks);
    block::Status status = !result.submitted
        ? block::BLOCK_ERR_DURABILITY_UNVERIFIED
        : logic::flush_result(result.completion, !result.controllerFault);
    set_diagnostic(controller, status, 2u, NVME_IO_FLUSH,
        controller.io.queueId, result.submitted, result.completion,
        result.controllerStatus, 0u, 0u, 0u);
    const bool quarantine = result.controllerFault || status != block::BLOCK_OK;
    release_io_lock(controller);
    if (quarantine) {
        controller.online = false;
        (void)block::mark_device_offline(controller.globalBlockIndex,
                                         controller.registrationId);
    }
    return status;
}

#if ARCH_HAS_PORT_IO
static const uint16_t kPciConfigAddress = 0x0CF8u;
static const uint16_t kPciConfigData = 0x0CFCu;

static uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset)
{
    const uint32_t address = 0x80000000u |
        (static_cast<uint32_t>(bus) << 16) |
        (static_cast<uint32_t>(device) << 11) |
        (static_cast<uint32_t>(function) << 8) | (offset & 0xFCu);
    arch::outl(kPciConfigAddress, address);
    return arch::inl(kPciConfigData);
}

static void pci_write32(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset, uint32_t value)
{
    const uint32_t address = 0x80000000u |
        (static_cast<uint32_t>(bus) << 16) |
        (static_cast<uint32_t>(device) << 11) |
        (static_cast<uint32_t>(function) << 8) | (offset & 0xFCu);
    arch::outl(kPciConfigAddress, address);
    arch::outl(kPciConfigData, value);
}

static bool parse_bar0(uint8_t bus, uint8_t device, uint8_t function,
                       uint64_t& physicalBase)
{
    const uint32_t low = pci_read32(bus, device, function, 0x10u);
    if (low == 0xFFFFFFFFu || (low & 1u) != 0u) return false;
    const uint8_t type = static_cast<uint8_t>((low >> 1) & 0x03u);
    if (type != 0u && type != 2u) return false;
    uint64_t high = 0u;
    if (type == 2u)
        high = pci_read32(bus, device, function, 0x14u);
    physicalBase = (high << 32) | (low & 0xFFFFFFF0u);
    return physicalBase != 0u &&
           physicalBase <= UINT64_MAX - kMappedBarBytes;
}

static bool register_namespace(uint8_t slot, Controller& controller,
                               const IdentifyController& ctrlIdentify,
                               const IdentifyNamespace& namespaceIdentify)
{
    uint64_t capacityBytes = 0u;
    uint32_t sectorSize = 0u;
    const uint8_t formatIndex = namespaceIdentify.flbas & 0x0Fu;
    if (!logic::valid_namespace_geometry(namespaceIdentify.nsze,
            namespaceIdentify.ncap, namespaceIdentify.nlbaf, formatIndex,
            namespaceIdentify.lbaFormats[formatIndex].lbads,
            namespaceIdentify.lbaFormats[formatIndex].ms,
            capacityBytes, sectorSize))
        return false;
    (void)capacityBytes;
    if (ctrlIdentify.namespaceCount == 0u ||
        !logic::command_fits_mdts(ctrlIdentify.mdts, 0u,
                                  kCommandTransferBytes))
        return false;

    NVMeDevice& device = s_devices[slot];
    memzero(&device, sizeof(device));
    device.active = true;
    device.bar0 = controller.bar0Physical;
    device.nsid = 1u;
    device.totalSectors = namespaceIdentify.nsze;
    device.sectorSize = sectorSize;
    device.mdts = ctrlIdentify.mdts;
    device.pciBus = controller.pciBus;
    device.pciDevice = controller.pciDevice;
    device.pciFunction = controller.pciFunction;
    device.vwcPresent = ctrlIdentify.volatileWriteCache & 1u;
    device.vwcEnabledKnown = controller.vwcEnabledKnown;
    device.vwcEnabled = controller.vwcEnabled;
    memcopy(device.model, ctrlIdentify.mn, 40u);
    device.model[40] = '\0';
    fix_nvme_string(device.model, 40u);
    memcopy(device.serial, ctrlIdentify.sn, 20u);
    device.serial[20] = '\0';
    fix_nvme_string(device.serial, 20u);
    memcopy(device.firmware, ctrlIdentify.fr, 8u);
    device.firmware[8] = '\0';
    fix_nvme_string(device.firmware, 8u);
    memcopy(device.nguid, namespaceIdentify.nguid, sizeof(device.nguid));
    memcopy(device.eui64, namespaceIdentify.eui64, sizeof(device.eui64));
    device.nguidValid = bytes_nonzero(device.nguid, sizeof(device.nguid));
    device.eui64Valid = bytes_nonzero(device.eui64, sizeof(device.eui64));

    block::BlockDevice blockDevice = {};
    blockDevice.active = true;
    blockDevice.type = block::BDEV_NVME;
    blockDevice.bootProvenance = block::BOOT_PROVENANCE_UNKNOWN;
    blockDevice.driverIndex = slot;
    blockDevice.totalSectors = device.totalSectors;
    blockDevice.sectorSize = device.sectorSize;
    blockDevice.readFn = nvme_read_sectors;
#if defined(GXOS_DM16_NVME_DURABILITY_PROVEN)
    blockDevice.writeFn = nvme_write_sectors;
    blockDevice.flushFn = nvme_flush;
    blockDevice.flushSemanticsKnown = true;
#else
    // DM16 runtime validation found a real DMA data mismatch on QEMU NVMe.
    // Keep production persistence and destructive eligibility disabled until
    // the private write/Flush/read/restore gate is clean on the final transport.
    blockDevice.writeFn = nullptr;
    blockDevice.flushFn = nullptr;
    blockDevice.flushSemanticsKnown = false;
#endif
    blockDevice.pciLocationValid = true;
    blockDevice.pciSegment = 0u;
    blockDevice.pciBus = controller.pciBus;
    blockDevice.pciDevice = controller.pciDevice;
    blockDevice.pciFunction = controller.pciFunction;
    blockDevice.namespaceId = device.nsid;
    blockDevice.requiredBufferAlignment = 1u; // Every command uses the aligned bounce page.
    blockDevice.maxTransferBytes = kMaxBlockTransferBytes;
    blockDevice.getIoDiagnosticFn = get_io_diagnostic;
    memcopy(blockDevice.model, device.model, sizeof(blockDevice.model) - 1u);
    memcopy(blockDevice.serial, device.serial, sizeof(blockDevice.serial) - 1u);
    memcopy(blockDevice.firmwareRevision, device.firmware,
            sizeof(blockDevice.firmwareRevision));
    blockDevice.namespaceNguidValid = device.nguidValid;
    blockDevice.namespaceEui64Valid = device.eui64Valid;
    blockDevice.nvmeControllerVersion = controller.version;
    blockDevice.nvmeMdts = device.mdts;
    blockDevice.nvmeVwcState = logic::classify_vwc_state(
        device.vwcPresent != 0u, device.vwcEnabledKnown, device.vwcEnabled);
    memcopy(blockDevice.namespaceNguid, device.nguid,
            sizeof(blockDevice.namespaceNguid));
    memcopy(blockDevice.namespaceEui64, device.eui64,
            sizeof(blockDevice.namespaceEui64));
    blockDevice.name[0] = 'n'; blockDevice.name[1] = 'v';
    blockDevice.name[2] = 'm'; blockDevice.name[3] = 'e';
    blockDevice.name[4] = static_cast<char>('0' + slot);
    blockDevice.name[5] = 'n'; blockDevice.name[6] = '1';
    blockDevice.name[7] = '\0';

    const uint8_t globalIndex = block::register_device(blockDevice);
    if (globalIndex == 0xFFu) {
        memzero(&device, sizeof(device));
        return false;
    }
    block::BlockDevice registered = {};
    if (!block::copy_device(globalIndex, registered)) {
        (void)block::unregister_device(globalIndex);
        memzero(&device, sizeof(device));
        return false;
    }
    controller.globalBlockIndex = globalIndex;
    controller.registrationId = registered.registrationId;
    controller.identifyNamespaceCount = ctrlIdentify.namespaceCount;
    return true;
}

static void scan_pci_nvme()
{
#if defined(ARCH_AMD64)
    for (uint16_t bus = 0u; bus < 256u; ++bus) {
        for (uint8_t pciDevice = 0u; pciDevice < 32u; ++pciDevice) {
            for (uint8_t function = 0u; function < 8u; ++function) {
                if (s_deviceCount >= kMaxControllers) return;
                const uint32_t id = pci_read32(static_cast<uint8_t>(bus),
                                               pciDevice, function, 0u);
                if (id == 0xFFFFFFFFu) continue;
                const uint32_t classRegister = pci_read32(
                    static_cast<uint8_t>(bus), pciDevice, function, 0x08u);
                if (static_cast<uint8_t>(classRegister >> 24) != 0x01u ||
                    static_cast<uint8_t>(classRegister >> 16) != 0x08u ||
                    static_cast<uint8_t>(classRegister >> 8) != 0x02u)
                    continue;

                uint64_t bar0 = 0u;
                if (!parse_bar0(static_cast<uint8_t>(bus), pciDevice,
                                function, bar0))
                    continue;

                uint32_t commandStatus = pci_read32(
                    static_cast<uint8_t>(bus), pciDevice, function, 0x04u);
                commandStatus |= 0x06u; // Enable memory decode and bus mastering.
                pci_write32(static_cast<uint8_t>(bus), pciDevice, function,
                            0x04u, commandStatus);

                const uint8_t slot = s_deviceCount;
                Controller& controller = s_controllers[slot];
                if (!initialize_controller(controller, bar0,
                        static_cast<uint8_t>(bus), pciDevice, function))
                    continue;

                IdentifyController ctrlIdentify = {};
                if (!identify_controller(controller, ctrlIdentify) ||
                    !create_io_queues(controller)) {
                    controller.online = false;
                    continue;
                }
                identify_vwc_state(controller, ctrlIdentify);

                // DM16 intentionally supports namespace 1 only. The Identify
                // Controller NN count is retained for diagnostics; namespace
                // list traversal remains a separate capability expansion.
                IdentifyNamespace namespaceIdentify = {};
                if (!identify_namespace(controller, 1u, namespaceIdentify)) {
                    controller.online = false;
                    continue;
                }
                if (!register_namespace(slot, controller, ctrlIdentify,
                                        namespaceIdentify)) {
                    controller.online = false;
                    continue;
                }
                ++s_deviceCount;
            }
        }
    }
#endif
}

#endif // ARCH_HAS_PORT_IO

} // namespace

void init()
{
    if (s_initialized) return;
    s_initialized = true;
    memzero(s_devices, sizeof(s_devices));
    memzero(s_controllers, sizeof(s_controllers));
    s_deviceCount = 0u;

#if ARCH_HAS_PORT_IO
    scan_pci_nvme();
#endif
}

uint8_t device_count()
{
    return s_deviceCount;
}

const NVMeDevice* get_device(uint8_t index)
{
    if (index >= kMaxControllers || !s_devices[index].active) return nullptr;
    return &s_devices[index];
}

#if defined(GXOS_DM16_QEMU_NVME_PROOF)
block::Status proof_write(uint8_t driverIndex, uint64_t lba,
                          uint32_t count, const void* buffer)
{
    return transfer(driverIndex, lba, count, nullptr, buffer, true);
}

block::Status proof_flush(uint8_t driverIndex)
{
    return nvme_flush(driverIndex);
}
#endif

void set_kernel_physical_base(uint64_t physicalBase)
{
    if (physicalBase != 0u) s_kernelPhysicalBase = physicalBase;
#if defined(GXOS_DM16_QEMU_NVME_PROOF)
    serial::puts("[DM16-NVME-DMA] kernelPhysicalBase=");
    serial::put_hex64(s_kernelPhysicalBase);
    serial::puts(" controllerVirtual=");
    serial::put_hex64(static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(&s_controllers[0])));
    serial::puts(" controllerPhysical=");
    serial::put_hex64(virtual_to_physical(&s_controllers[0]));
    serial::puts(" bounceVirtual=");
    serial::put_hex64(static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(s_controllers[0].ioBounce)));
    serial::puts(" bouncePhysical=");
    serial::put_hex64(virtual_to_physical(s_controllers[0].ioBounce));
    serial::putc('\n');
#endif
}

} // namespace nvme
} // namespace kernel

#undef NVME_ALIGNED
