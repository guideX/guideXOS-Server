#pragma once

#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 1)

namespace guideXOS
{
    // Boot mode (future‑proof if more modes ever appear)
    enum class BootMode : uint32_t
    {
        Uefi = 1,   // UEFI boot
        // Bios = 2 // reserved, not currently used
    };

    // Simple framebuffer pixel format (subset of what GOP can describe)
    enum class FramebufferFormat : uint32_t
    {
        Unknown = 0,
        R8G8B8A8,   // 32‑bit, little‑endian, RGBA
        B8G8R8A8,   // 32‑bit, little‑endian, BGRA
    };

    // Diagnostic framebuffer origin for the boot handoff array.
    enum class FramebufferSource : uint32_t
    {
        Unknown = 0,
        Multiboot = 1,
        UefiGop = 2,
    };

    // Framebuffer descriptor flags.
    static const uint32_t FRAMEBUFFER_DESCRIPTOR_FLAG_VALID    = (1u << 0);
    static const uint32_t FRAMEBUFFER_DESCRIPTOR_FLAG_PRIMARY  = (1u << 1);
    static const uint32_t FRAMEBUFFER_DESCRIPTOR_FLAG_SELECTED = (1u << 2);
    static const uint32_t FRAMEBUFFER_DESCRIPTOR_FLAG_DUPLICATE = (1u << 3);
    static const uint32_t FRAMEBUFFER_DESCRIPTOR_FLAG_ALIAS    = (1u << 4);
    static const uint32_t FRAMEBUFFER_DESCRIPTOR_FLAG_SAME_AS_PRIMARY = (1u << 5);
    static const uint32_t FRAMEBUFFER_DESCRIPTOR_FLAG_SUSPICIOUS = (1u << 6);

    // Bounded diagnostic list size for framebuffer descriptors in BootInfo.
    static const uint32_t GUIDEXOS_MAX_FRAMEBUFFERS = 8u;

    struct FramebufferDescriptor
    {
        uint64_t Base;
        uint64_t Size;
        uint32_t Width;
        uint32_t Height;
        uint32_t Pitch;
        enum FramebufferFormat Format;
        uint32_t BitsPerPixel;
        enum FramebufferSource Source;
        uint32_t Flags;
    };

    // NIC device information (passed from bootloader to kernel)
    struct NicInfo
    {
        uint64_t MmioPhys;      // Physical BAR0 address
        uint64_t MmioVirt;      // Virtual address (mapped by bootloader)
        uint64_t MmioSize;      // Size of MMIO region
        uint16_t VendorId;
        uint16_t DeviceId;
        uint8_t  Bus;
        uint8_t  Device;
        uint8_t  Function;
        uint8_t  IrqLine;
        uint8_t  MacAddress[6]; // Placeholder MAC (00:00:00:00:00:00 if not read)
        uint8_t  Reserved0[2];
        uint32_t Flags;         // Bit 0: found, Bit 1: mapped, Bit 2: active
        uint32_t Reserved1;
    };

    // NIC flags
    static const uint32_t NIC_FLAG_FOUND  = (1u << 0);
    static const uint32_t NIC_FLAG_MAPPED = (1u << 1);
    static const uint32_t NIC_FLAG_ACTIVE = (1u << 2);

    // A copied, bounded UEFI device path. It contains bytes only: firmware
    // handles and protocol pointers never cross the handoff boundary.
    static const uint32_t GUIDEXOS_BOOT_SOURCE_PATH_MAX = 256u;
    static const uint16_t GUIDEXOS_BOOT_SOURCE_VERSION = 1u;
    static const uint32_t BOOT_SOURCE_FLAG_VALID = (1u << 0);
    static const uint32_t BOOT_SOURCE_FLAG_DEVICE_PATH_VALID = (1u << 1);
    static const uint32_t BOOT_SOURCE_FLAG_TRUNCATED = (1u << 2);
    static const uint32_t BOOT_SOURCE_FLAG_PCI_LOCATION_VALID = (1u << 3);

    struct BootSourceDescriptor
    {
        uint16_t Version;
        uint16_t Size;
        uint32_t Flags;
        uint16_t DevicePathLength;
        uint16_t Reserved;
        uint32_t PciSegment;
        uint8_t PciBus;
        uint8_t PciDevice;
        uint8_t PciFunction;
        uint8_t ReservedPci;
        uint8_t DevicePath[GUIDEXOS_BOOT_SOURCE_PATH_MAX];
    };

    static inline bool guidexos_uefi_device_path_valid(
        const uint8_t* bytes, uint16_t length)
    {
        if (!bytes || length < 4u || length > GUIDEXOS_BOOT_SOURCE_PATH_MAX)
            return false;
        uint32_t offset = 0;
        while (offset + 4u <= length) {
            const uint8_t type = bytes[offset];
            const uint8_t subtype = bytes[offset + 1u];
            const uint16_t nodeLength = static_cast<uint16_t>(bytes[offset + 2u]) |
                (static_cast<uint16_t>(bytes[offset + 3u]) << 8);
            if (nodeLength < 4u || nodeLength > length - offset) return false;
            if (type == 0x7Fu) {
                return subtype == 0xFFu && nodeLength == 4u &&
                       offset + nodeLength == length;
            }
            offset += nodeLength;
        }
        return false;
    }

    struct BootInfo
    {
        uint32_t Magic;
        uint16_t Version;
        uint16_t Size;
        uint32_t Flags;
        uint32_t HeaderChecksum;
        uint32_t FramebufferUniqueCount;   // Unique physical framebuffer outputs after exact-identity dedupe.
        enum BootMode BootMode;
        uint32_t FramebufferDuplicateCount; // Exact duplicate / alias descriptor count.
        uint64_t MemoryMap;
        uint64_t MemoryMapEntryCount;
        uint64_t MemoryMapDescriptorSize;
        uint64_t FramebufferBase;
        uint64_t FramebufferSize;
        uint32_t FramebufferWidth;
        uint32_t FramebufferHeight;
        uint32_t FramebufferPitch;
        enum FramebufferFormat FramebufferFormat;
        uint32_t FramebufferCount;          // Raw GOP descriptor count exported by the bootloader.
        uint32_t FramebufferSuspiciousCount; // Base/size collisions with mismatched geometry/format.
        FramebufferDescriptor FramebufferDescriptors[GUIDEXOS_MAX_FRAMEBUFFERS];
        uint64_t AcpiRsdp;
        uint64_t CommandLine;
        uint64_t RamdiskBase;
        uint64_t RamdiskSize;
        // NIC information (uses former Reserved space)
        NicInfo  Nic;
        uint64_t KernelPhysicalBase;
        // Appended so the kernel can safely recognize legacy BootInfo v2,
        // where this descriptor is absent.
        BootSourceDescriptor BootSource;
    };

    static inline bool guidexos_framebuffer_descriptor_identity_matches(
        const FramebufferDescriptor* a,
        const FramebufferDescriptor* b)
    {
        if (!a || !b) return false;
        return a->Base == b->Base &&
               a->Size == b->Size &&
               a->Width == b->Width &&
               a->Height == b->Height &&
               a->Pitch == b->Pitch &&
               a->Format == b->Format;
    }

    static inline bool guidexos_framebuffer_descriptor_base_size_matches(
        const FramebufferDescriptor* a,
        const FramebufferDescriptor* b)
    {
        if (!a || !b) return false;
        return a->Base == b->Base &&
               a->Size == b->Size;
    }

    struct FramebufferClassificationSummary
    {
        uint32_t RawCount;
        uint32_t UniqueCount;
        uint32_t DuplicateCount;
        uint32_t SuspiciousCount;
    };

    static inline FramebufferClassificationSummary guidexos_classify_framebuffer_descriptors(
        FramebufferDescriptor* descriptors,
        uint32_t count)
    {
        FramebufferClassificationSummary summary{};
        summary.RawCount = count;

        if (!descriptors || count == 0u) {
            return summary;
        }

        for (uint32_t i = 0u; i < count; ++i) {
            FramebufferDescriptor& current = descriptors[i];
            current.Flags &= ~(
                FRAMEBUFFER_DESCRIPTOR_FLAG_DUPLICATE |
                FRAMEBUFFER_DESCRIPTOR_FLAG_ALIAS |
                FRAMEBUFFER_DESCRIPTOR_FLAG_SAME_AS_PRIMARY |
                FRAMEBUFFER_DESCRIPTOR_FLAG_SUSPICIOUS);

            if (!(current.Flags & FRAMEBUFFER_DESCRIPTOR_FLAG_VALID)) {
                continue;
            }

            bool isDuplicate = false;
            bool isSameAsPrimary = false;
            bool isSuspicious = false;

            for (uint32_t j = 0u; j < i; ++j) {
                const FramebufferDescriptor& prior = descriptors[j];
                if (!(prior.Flags & FRAMEBUFFER_DESCRIPTOR_FLAG_VALID)) {
                    continue;
                }

                if (guidexos_framebuffer_descriptor_identity_matches(&current, &prior)) {
                    isDuplicate = true;
                    if (prior.Flags & FRAMEBUFFER_DESCRIPTOR_FLAG_PRIMARY) {
                        isSameAsPrimary = true;
                    }
                    break;
                }

                if (!isSuspicious && guidexos_framebuffer_descriptor_base_size_matches(&current, &prior)) {
                    isSuspicious = true;
                }
            }

            if (isDuplicate) {
                current.Flags |= FRAMEBUFFER_DESCRIPTOR_FLAG_DUPLICATE | FRAMEBUFFER_DESCRIPTOR_FLAG_ALIAS;
                if (isSameAsPrimary) {
                    current.Flags |= FRAMEBUFFER_DESCRIPTOR_FLAG_SAME_AS_PRIMARY;
                }
                ++summary.DuplicateCount;
            } else {
                ++summary.UniqueCount;
                if (isSuspicious) {
                    current.Flags |= FRAMEBUFFER_DESCRIPTOR_FLAG_SUSPICIOUS;
                    ++summary.SuspiciousCount;
                }
            }
        }

        return summary;
    }
}

#pragma pack(pop)

namespace guideXOS
{
    // Magic and version constants for BootInfo v2/v3.
    static const uint32_t GUIDEXOS_BOOTINFO_MAGIC   = 0x49425847; // 'GXBI'
    static const uint16_t GUIDEXOS_BOOTINFO_LEGACY_VERSION = 2;
    static const uint16_t GUIDEXOS_BOOTINFO_VERSION = 3;
    static const uint16_t GUIDEXOS_BOOTINFO_LEGACY_SIZE =
        static_cast<uint16_t>(sizeof(BootInfo) - sizeof(BootSourceDescriptor));

    static inline bool guidexos_boot_source_descriptor_valid(
        const BootSourceDescriptor* source)
    {
        if (!source || source->Version != GUIDEXOS_BOOT_SOURCE_VERSION ||
            source->Size != sizeof(BootSourceDescriptor) ||
            (source->Flags & (BOOT_SOURCE_FLAG_VALID |
                              BOOT_SOURCE_FLAG_DEVICE_PATH_VALID)) !=
                (BOOT_SOURCE_FLAG_VALID | BOOT_SOURCE_FLAG_DEVICE_PATH_VALID) ||
            source->DevicePathLength < 4u ||
            source->DevicePathLength > GUIDEXOS_BOOT_SOURCE_PATH_MAX ||
            !guidexos_uefi_device_path_valid(source->DevicePath,
                                             source->DevicePathLength)) {
            return false;
        }
        return true;
    }

    // Early panic: implemented in a .cpp file, infinite loop and/or framebuffer error.
    [[noreturn]] void guidexos_early_panic(const BootInfo* bi);

    // Compute 32-bit sum over BootInfo (Size rounded down to multiple of 4)
    static inline bool guidexos_bootinfo_checksum_valid(const BootInfo* bi)
    {
        if (!bi) return false;
        if (bi->Size != GUIDEXOS_BOOTINFO_LEGACY_SIZE &&
            bi->Size != sizeof(BootInfo)) return false;
        uint32_t byteCount = bi->Size & ~0x3u; // round down to multiple of 4
        if (byteCount < GUIDEXOS_BOOTINFO_LEGACY_SIZE) return false;

        const uint32_t* p = reinterpret_cast<const uint32_t*>(bi);
        uint32_t count = byteCount / 4u;
        uint32_t sum = 0u;
        for (uint32_t i = 0; i < count; ++i)
            sum += p[i];
        return (sum == 0u);
    }

    // Very early, heap-less validation of BootInfo v2/v3.
    static inline void guidexos_validate_bootinfo_or_panic(const BootInfo* bi)
    {
        if (!bi) {
            guidexos_early_panic(nullptr);
        }

        // 1. Magic, version, and exact known size. Legacy v2 safely has no
        // boot-source tail, so its descriptor must never be read.
        if (bi->Magic != GUIDEXOS_BOOTINFO_MAGIC) {
            guidexos_early_panic(bi);
        }
        if (!((bi->Version == GUIDEXOS_BOOTINFO_LEGACY_VERSION &&
               bi->Size == GUIDEXOS_BOOTINFO_LEGACY_SIZE) ||
              (bi->Version == GUIDEXOS_BOOTINFO_VERSION &&
               bi->Size == sizeof(BootInfo)))) {
            guidexos_early_panic(bi);
        }

        if (bi->FramebufferCount > GUIDEXOS_MAX_FRAMEBUFFERS) {
            guidexos_early_panic(bi);
        }
        if (bi->FramebufferUniqueCount > bi->FramebufferCount ||
            bi->FramebufferDuplicateCount > bi->FramebufferCount ||
            bi->FramebufferSuspiciousCount > bi->FramebufferUniqueCount) {
            guidexos_early_panic(bi);
        }
        if ((bi->FramebufferUniqueCount + bi->FramebufferDuplicateCount) != bi->FramebufferCount) {
            guidexos_early_panic(bi);
        }

        // 2. Checksum / invariant
        if (!guidexos_bootinfo_checksum_valid(bi)) {
            guidexos_early_panic(bi);
        }

        // 3. Optional flags and pointer sanity checks
        // Framebuffer info valid?
        if (bi->Flags & (1u << 1)) {
            if (bi->FramebufferBase == 0u || bi->FramebufferSize == 0u) {
                guidexos_early_panic(bi);
            }
        }

        // Memory map valid?
        if (bi->Flags & (1u << 0)) {
            if (bi->MemoryMap == 0u ||
                bi->MemoryMapEntryCount == 0u ||
                bi->MemoryMapDescriptorSize == 0u) {
                guidexos_early_panic(bi);
            }
        }
    }
}
