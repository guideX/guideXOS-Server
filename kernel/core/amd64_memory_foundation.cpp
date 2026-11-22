#include "include/kernel/amd64_memory_foundation.h"
#include "include/kernel/physical_frame_owner.h"
#include "include/kernel/task_address_space.h"
#include "include/kernel/serial_debug.h"
#include "../../guideXOSBootLoader/guidexOSBootInfo.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "include/kernel/arch.h"
#include "../arch/amd64/include/arch/cpu_capabilities.h"
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

namespace kernel {
namespace memory {

#if defined(__x86_64__) || defined(_M_X64)
namespace {

static const uint64_t kIdentityMappedPhysicalLimit = 1ull << 47;
static const uint64_t kPageBytes = 4096u;
static const uint64_t kPageMask = kPageBytes - 1u;
static const uint64_t kPtePresent = 1ull << 0;
static const uint64_t kPteWritable = 1ull << 1;
static const uint64_t kPteUser = 1ull << 2;
static const uint64_t kPteLarge = 1ull << 7;
static const uint64_t kPteAddressMask = 0x000FFFFFFFFFF000ull;
static const uint32_t kMaximumBootPageTablePages = 512u;
static const uint32_t kIa32Efer = 0xC0000080u;
static const uint64_t kEferNxEnable = 1ull << 11;

static void* identity_map_physical(void*, uint64_t physicalAddress)
{
    if (physicalAddress >= kIdentityMappedPhysicalLimit) return nullptr;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(physicalAddress));
}

static bool identity_translation_is_kernel_writable(uint64_t rootPhysical,
                                                    uint64_t virtualAddress)
{
    if (rootPhysical == 0u || (rootPhysical & kPageMask) != 0u ||
        virtualAddress >= kIdentityMappedPhysicalLimit) return false;
    uint64_t* table = static_cast<uint64_t*>(identity_map_physical(nullptr, rootPhysical));
    if (!table) return false;
    const uint64_t indices[4] = {(virtualAddress >> 39) & 0x1FFu,
                                 (virtualAddress >> 30) & 0x1FFu,
                                 (virtualAddress >> 21) & 0x1FFu,
                                 (virtualAddress >> 12) & 0x1FFu};
    for (uint32_t level = 0; level < 4u; ++level) {
        const uint64_t entry = table[indices[level]];
        if ((entry & kPtePresent) == 0u || (entry & kPteUser) != 0u ||
            (entry & kPteWritable) == 0u) return false;
        if (level == 3u) {
            return (entry & kPteAddressMask) == (virtualAddress & ~kPageMask);
        }
        if ((entry & kPteLarge) != 0u) {
            if (level == 0u) return false;
            const uint32_t shift = level == 1u ? 30u : 21u;
            const uint64_t largePageMask = (1ull << shift) - 1u;
            const uint64_t physicalBase =
                (entry & kPteAddressMask) & ~largePageMask;
            return (physicalBase | (virtualAddress & largePageMask)) == virtualAddress;
        }
        const uint64_t childPhysical = entry & kPteAddressMask;
        table = static_cast<uint64_t*>(identity_map_physical(nullptr, childPhysical));
        if (!table) return false;
    }
    return false;
}

static bool identity_range_is_kernel_writable(uint64_t rootPhysical,
                                              uint64_t physicalBase,
                                              uint64_t byteCount)
{
    if (byteCount == 0u || physicalBase >= kIdentityMappedPhysicalLimit ||
        byteCount > kIdentityMappedPhysicalLimit - physicalBase) return false;
    const uint64_t rawEnd = physicalBase + byteCount;
    if (rawEnd > kIdentityMappedPhysicalLimit - kPageMask) return false;
    const uint64_t start = physicalBase & ~kPageMask;
    const uint64_t end = (rawEnd + kPageMask) & ~kPageMask;
    // Descriptor count and task-arena capacity cap this walk to 4,128 pages.
    for (uint64_t page = start; page < end; page += kPageBytes) {
        if (!identity_translation_is_kernel_writable(rootPhysical, page)) return false;
    }
    return true;
}

static bool ranges_overlap(uint64_t firstBase, uint64_t firstSize,
                           uint64_t secondBase, uint64_t secondSize)
{
    if (firstSize == 0u || secondSize == 0u) return false;
    if (firstBase > UINT64_MAX - firstSize ||
        secondBase > UINT64_MAX - secondSize) return true;
    return firstBase < secondBase + secondSize &&
           secondBase < firstBase + firstSize;
}

static bool page_table_tree_avoids_arena(uint64_t tablePhysical,
                                         uint32_t level,
                                         uint64_t arenaBase,
                                         uint64_t arenaEnd,
                                         uint32_t* tableCount)
{
    if (!tableCount || level > 3u || (tablePhysical & kPageMask) != 0u ||
        tablePhysical >= kIdentityMappedPhysicalLimit ||
        (tablePhysical >= arenaBase && tablePhysical < arenaEnd) ||
        ++*tableCount > kMaximumBootPageTablePages) return false;
    uint64_t* table = static_cast<uint64_t*>(identity_map_physical(nullptr, tablePhysical));
    if (!table) return false;
    if (level == 3u) return true;

    for (uint32_t i = 0; i < 512u; ++i) {
        const uint64_t entry = table[i];
        if ((entry & kPtePresent) == 0u) continue;
        if (level == 0u && (entry & kPteLarge) != 0u) return false;
        if (level > 0u && (entry & kPteLarge) != 0u) continue;
        if (!page_table_tree_avoids_arena(entry & kPteAddressMask, level + 1u,
                                          arenaBase, arenaEnd, tableCount)) return false;
    }
    return true;
}

static uint64_t current_cr3(void*)
{
    return kernel::arch::read_cr3();
}

static bool ensure_nx_enabled(void*)
{
    if (!kernel::arch::amd64::cpu_supports_nx()) return false;
    uint64_t efer = kernel::arch::read_msr(kIa32Efer);
    if ((efer & kEferNxEnable) == 0u) {
        kernel::arch::write_msr(kIa32Efer, efer | kEferNxEnable);
        efer = kernel::arch::read_msr(kIa32Efer);
    }
    return (efer & kEferNxEnable) != 0u;
}

static bool supports_safe_tlb_invalidation(void*)
{
    // The foundation uses INVLPG in the single, non-PCID address-space model.
    // PCID needs INVPCID or a tagged flush policy that is not established yet.
    static const uint64_t kCr4Pcide = 1ull << 17;
    return (kernel::arch::read_cr4() & kCr4Pcide) == 0u;
}

static bool invalidate_page(void*, uint64_t virtualAddress)
{
    kernel::arch::invalidate_tlb_entry(virtualAddress);
    return true;
}

static uint64_t enter_memory_critical_section(void*)
{
#if defined(_MSC_VER)
    const uint64_t flags = __readeflags();
    _disable();
    return flags;
#else
    uint64_t flags = 0u;
    asm volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
#endif
}

static void leave_memory_critical_section(void*, uint64_t savedFlags)
{
    if ((savedFlags & (1ull << 9)) != 0u) kernel::arch::enable_interrupts();
}

} // namespace
#endif

AddressSpacePlatform amd64_address_space_platform()
{
#if defined(__x86_64__) || defined(_M_X64)
    AddressSpacePlatform platform{};
    platform.mapPhysicalAddress = &identity_map_physical;
    platform.readCurrentCr3 = &current_cr3;
    platform.ensureNxEnabled = &ensure_nx_enabled;
    platform.supportsSafeTlbInvalidation = &supports_safe_tlb_invalidation;
    platform.invalidatePage = &invalidate_page;
    platform.enterCriticalSection = &enter_memory_critical_section;
    platform.leaveCriticalSection = &leave_memory_critical_section;
    return platform;
#else
    return AddressSpacePlatform{};
#endif
}

bool initialize_amd64_task_memory(const guideXOS::BootInfo* bootInfo)
{
#if defined(__x86_64__) || defined(_M_X64)
    if (!bootInfo || bootInfo->Magic != guideXOS::GUIDEXOS_BOOTINFO_MAGIC ||
        bootInfo->Version != guideXOS::GUIDEXOS_BOOTINFO_VERSION ||
        bootInfo->Size != sizeof(guideXOS::BootInfo) ||
        !guideXOS::guidexos_bootinfo_checksum_valid(bootInfo) ||
        (bootInfo->Flags & guideXOS::GUIDEXOS_BOOTINFO_FLAG_MEMORY_MAP_VALID) == 0u ||
        (bootInfo->Flags & guideXOS::GUIDEXOS_BOOTINFO_FLAG_TASK_FRAME_ARENA_VALID) == 0u) {
        kernel::serial::puts("[MEMORY-OWNERSHIP] status=boot-handoff-unavailable user-mode=disabled\n");
        return false;
    }

    FrameOwnerConfig config{};
    if (bootInfo->MemoryMapEntryCount == 0u ||
        bootInfo->MemoryMapEntryCount > kMaxFirmwareMemoryDescriptors ||
        bootInfo->MemoryMapDescriptorSize < sizeof(FirmwareMemoryDescriptor) ||
        bootInfo->MemoryMapDescriptorSize > 256u ||
        (bootInfo->MemoryMapDescriptorSize & 7u) != 0u ||
        bootInfo->MemoryMapEntryCount > UINT64_MAX / bootInfo->MemoryMapDescriptorSize ||
        bootInfo->MemoryMap == 0u || bootInfo->TaskFrameArenaBase == 0u ||
        bootInfo->TaskFrameArenaSize == 0u ||
        bootInfo->TaskFrameArenaSize >
            static_cast<uint64_t>(kMaxTaskArenaFrames) * kOwnedFrameBytes ||
        (bootInfo->TaskFrameArenaBase & kPageMask) != 0u ||
        (bootInfo->TaskFrameArenaSize & kPageMask) != 0u ||
        bootInfo->TaskFrameArenaBase > UINT64_MAX - bootInfo->TaskFrameArenaSize ||
        bootInfo->KernelPhysicalBase == 0u || bootInfo->KernelPhysicalSize == 0u ||
        (bootInfo->KernelPhysicalBase & kPageMask) != 0u ||
        bootInfo->KernelPhysicalBase > UINT64_MAX - bootInfo->KernelPhysicalSize ||
        ((bootInfo->BootStackBase == 0u) != (bootInfo->BootStackSize == 0u)) ||
        (bootInfo->BootStackBase != 0u &&
         ((bootInfo->BootStackBase & kPageMask) != 0u ||
          (bootInfo->BootStackSize & kPageMask) != 0u ||
          bootInfo->BootStackBase > UINT64_MAX - bootInfo->BootStackSize)) ||
        ((bootInfo->RamdiskBase == 0u) != (bootInfo->RamdiskSize == 0u)) ||
        (bootInfo->RamdiskBase != 0u &&
         bootInfo->RamdiskBase > UINT64_MAX - bootInfo->RamdiskSize) ||
        bootInfo->FramebufferCount > guideXOS::GUIDEXOS_MAX_FRAMEBUFFERS) {
        kernel::serial::puts("[MEMORY-OWNERSHIP] status=boot-range-invalid user-mode=disabled\n");
        return false;
    }
    const uint64_t memoryMapBytes =
        bootInfo->MemoryMapEntryCount * bootInfo->MemoryMapDescriptorSize;
    const uint64_t kernelRoot = kernel::arch::read_cr3() & kPteAddressMask;
    const uint64_t arenaBase = bootInfo->TaskFrameArenaBase;
    const uint64_t arenaSize = bootInfo->TaskFrameArenaSize;
    const uint64_t bootInfoPhysical = reinterpret_cast<uintptr_t>(bootInfo);
    bool overlapsReservedRange =
        ranges_overlap(arenaBase, arenaSize, bootInfo->MemoryMap, memoryMapBytes) ||
        ranges_overlap(arenaBase, arenaSize, bootInfo->KernelPhysicalBase,
                       bootInfo->KernelPhysicalSize) ||
        ranges_overlap(arenaBase, arenaSize, bootInfo->RamdiskBase,
                       bootInfo->RamdiskSize) ||
        ranges_overlap(arenaBase, arenaSize, bootInfoPhysical, kPageBytes) ||
        ranges_overlap(arenaBase, arenaSize, bootInfo->BootStackBase,
                       bootInfo->BootStackSize);
    if (bootInfo->FramebufferBase != 0u && bootInfo->FramebufferSize != 0u) {
        overlapsReservedRange = overlapsReservedRange ||
            ranges_overlap(arenaBase, arenaSize, bootInfo->FramebufferBase,
                           bootInfo->FramebufferSize);
    }
    for (uint32_t i = 0; i < bootInfo->FramebufferCount; ++i) {
        const guideXOS::FramebufferDescriptor& framebuffer =
            bootInfo->FramebufferDescriptors[i];
        if ((framebuffer.Flags & guideXOS::FRAMEBUFFER_DESCRIPTOR_FLAG_VALID) != 0u &&
            framebuffer.Size != 0u) {
            overlapsReservedRange = overlapsReservedRange ||
                ranges_overlap(arenaBase, arenaSize, framebuffer.Base,
                               framebuffer.Size);
        }
    }
    uint32_t pageTablePages = 0u;
    if (overlapsReservedRange ||
        !page_table_tree_avoids_arena(kernelRoot, 0u, arenaBase,
                                      arenaBase + arenaSize, &pageTablePages)) {
        kernel::serial::puts("[MEMORY-OWNERSHIP] status=overlapping-reservation user-mode=disabled\n");
        return false;
    }
    if (!identity_range_is_kernel_writable(kernelRoot, bootInfo->MemoryMap,
                                            memoryMapBytes) ||
        !identity_range_is_kernel_writable(kernelRoot, bootInfo->TaskFrameArenaBase,
                                            bootInfo->TaskFrameArenaSize)) {
        kernel::serial::puts("[MEMORY-OWNERSHIP] status=arena-not-safely-mapped user-mode=disabled\n");
        return false;
    }

    config.memoryMapBytes = reinterpret_cast<const void*>(
        static_cast<uintptr_t>(bootInfo->MemoryMap));
    config.memoryMapPhysicalAddress = bootInfo->MemoryMap;
    config.memoryMapEntryCount = bootInfo->MemoryMapEntryCount;
    config.memoryMapDescriptorSize = bootInfo->MemoryMapDescriptorSize;
    config.taskArenaPhysicalBase = bootInfo->TaskFrameArenaBase;
    config.taskArenaBytes = bootInfo->TaskFrameArenaSize;
    config.mapPhysicalAddress = &identity_map_physical;
    config.enterCriticalSection = &enter_memory_critical_section;
    config.leaveCriticalSection = &leave_memory_critical_section;

    const FrameStatus status = initialize_frame_owner(config);
    if (status != FrameStatus::Ok) {
        kernel::serial::puts("[MEMORY-OWNERSHIP] status=");
        kernel::serial::puts(frame_status_name(status));
        kernel::serial::puts(" user-mode=disabled\n");
        return false;
    }

    FrameAccounting accounting{};
    get_frame_accounting(&accounting);
    kernel::serial::puts("[MEMORY-OWNERSHIP] status=ready arena-pages=0x");
    kernel::serial::put_hex32(accounting.capacityFrames);
    kernel::serial::puts(" free-pages=0x");
    kernel::serial::put_hex32(accounting.freeFrames);
    kernel::serial::puts(" metadata=kernel-bss user-mode=disabled\n");
    return true;
#else
    (void)bootInfo;
    return false;
#endif
}

} // namespace memory
} // namespace kernel
