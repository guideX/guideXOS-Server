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

    struct BootInfo
    {
        uint32_t Magic;
        uint16_t Version;
        uint16_t Size;
        uint32_t Flags;
        uint32_t HeaderChecksum;
        uint32_t Reserved0;
        BootMode BootMode;
        uint32_t Reserved1;
        uint64_t MemoryMap;
        uint64_t MemoryMapEntryCount;
        uint64_t MemoryMapDescriptorSize;
        uint64_t FramebufferBase;
        uint64_t FramebufferSize;
        uint32_t FramebufferWidth;
        uint32_t FramebufferHeight;
        uint32_t FramebufferPitch;
        FramebufferFormat FramebufferFormat;
        uint32_t Reserved2;
        uint64_t AcpiRsdp;
        uint64_t CommandLine;
        uint64_t RamdiskBase;
        uint64_t RamdiskSize;
        uint64_t Reserved[6];
    };
}

#pragma pack(pop)

namespace guideXOS
{
    // Magic and version constants for BootInfo v1
    static const uint32_t GUIDEXOS_BOOTINFO_MAGIC   = 0x49425847; // 'GXBI'
    static const uint16_t GUIDEXOS_BOOTINFO_VERSION = 1;

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

    // Very early, heap-less validation of BootInfo v1
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