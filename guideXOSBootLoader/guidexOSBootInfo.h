#pragma once

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
        uint16_t SubsystemVendorId;
        uint16_t SubsystemDeviceId;
        uint8_t  RevisionId;
        uint8_t  ClassCode;
        uint8_t  Subclass;
        uint8_t  ProgIf;
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
    // Magic and version constants for BootInfo v2
    static const uint32_t GUIDEXOS_BOOTINFO_MAGIC   = 0x49425847; // 'GXBI'
    static const uint16_t GUIDEXOS_BOOTINFO_VERSION = 2;

    // Early panic: implemented in a .cpp file, infinite loop and/or framebuffer error.
    [[noreturn]] void guidexos_early_panic(const BootInfo* bi);

    // Compute 32-bit sum over BootInfo (Size rounded down to multiple of 4)
    static inline bool guidexos_bootinfo_checksum_valid(const BootInfo* bi)
    {
        if (!bi) return false;
        if (bi->Size == 0u) return false;
        uint32_t byteCount = bi->Size & ~0x3u; // round down to multiple of 4
        if (byteCount < sizeof(BootInfo)) return false;

        const uint32_t* p = reinterpret_cast<const uint32_t*>(bi);
        uint32_t count = byteCount / 4u;
        uint32_t sum = 0u;
        for (uint32_t i = 0; i < count; ++i)
            sum += p[i];
        return (sum == 0u);
    }

    // Very early, heap-less validation of BootInfo v2
    static inline void guidexos_validate_bootinfo_or_panic(const BootInfo* bi)
    {
        if (!bi) {
            guidexos_early_panic(nullptr);
        }

        // Basic size check before trusting any other fields
        if (bi->Size < sizeof(BootInfo)) {
            guidexos_early_panic(nullptr);
        }

        // 1. Magic, version, size (size equality is strict for v1)
        if (bi->Magic != GUIDEXOS_BOOTINFO_MAGIC) {
            guidexos_early_panic(bi);
        }
        if (bi->Version != GUIDEXOS_BOOTINFO_VERSION) {
            guidexos_early_panic(bi);
        }
        if (bi->Size != sizeof(BootInfo)) {
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
