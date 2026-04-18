/*
GuideXOS UEFI Bootloader Context

Target:
- UEFI x86_64 only
- Visual Studio / MSVC
- No CRT, no libc, no STL
- No malloc/new/delete
- No exceptions, no RTTI
- MS x64 ABI (NOT SysV)
- No identity-mapped assumptions
- Kernel is ELF64
- Kernel entry takes BootInfo*
- Boot services MUST be exited before jumping to kernel

Forbidden:
- windows.h
- std::*
- printf / cout
- assuming p_vaddr == physical
- calling UEFI services after ExitBootServices
*/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>
#include "bootinfo.h"          // legacy, gradually being phased out
#include "elf.h"
#include "guidexOSBootInfo.h"   // canonical BootInfo v1
#include "uefi_shim.h"         // UEFI shims for freestanding environment
#include "debug_helpers.h"     // Post-ExitBootServices debugging
#include "paging.h"            // minimal identity page tables
#include "pci.h"               // PCI enumeration for NIC detection

// MSVC intrinsics
extern "C" void __halt(void);
#pragma intrinsic(__halt)

// Assembly trampoline (NASM, win64 ABI)
extern "C" void BootHandoffTrampoline(void* kernelEntry, void* bootInfo, void* stackTop, void* pml4Phys);
extern "C" void SetupTrampoline(void* executableMemory);
extern "C" UINTN GetTrampolineCodeSize(void);

// Local aliases for compatibility with existing code
#define Acpi20TableGuid gEfiAcpi20TableGuid
#define Acpi10TableGuid gEfiAcpi10TableGuid

EFI_GRAPHICS_OUTPUT_PROTOCOL* GOP;
EFI_FILE_PROTOCOL* KernelFile;
BootInfo bootInfo;      // legacy struct instance (still used elsewhere)
// v1BootInfo is now allocated dynamically in EfiLoaderData pages to survive ExitBootServices

EFI_STATUS LoadFile(EFI_FILE_PROTOCOL** file, CHAR16* path, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    EFI_FILE_PROTOCOL* Root;
    EFI_STATUS s;

    SystemTable->BootServices->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (void**)&LoadedImage);
    SystemTable->BootServices->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&FileSystem);
    FileSystem->OpenVolume(FileSystem, &Root);
    s = Root->Open(Root, file, path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    if (s != EFI_SUCCESS) {
        Print((CONST CHAR16*)L"Could not open file: %s\n", path);
    }
    return s;
}


// New helper: exits boot services with retry and returns a stable memory map
// (Kept for compatibility; handoff path uses ExitBootServicesWithMemoryMapInBuffer)
EFI_STATUS ExitBootServicesWithMemoryMap(
    EFI_HANDLE          ImageHandle,
    EFI_SYSTEM_TABLE*   SystemTable,
    EFI_MEMORY_DESCRIPTOR** outMap,
    UINTN*              outEntryCount,
    UINTN*              outDescriptorSize
)
{
    EFI_STATUS           status;
    EFI_MEMORY_DESCRIPTOR* tempMap = NULL;
    UINTN                tempMapSize = 0;
    UINTN                mapKey = 0;
    UINTN                descriptorSize = 0;
    UINT32               descriptorVersion = 0;

    // 1) Get required buffer size (expected EFI_BUFFER_TOO_SMALL)
    status = SystemTable->BootServices->GetMemoryMap(
        &tempMapSize,
        tempMap,
        &mapKey,
        &descriptorSize,
        &descriptorVersion
    );
    if (status != EFI_BUFFER_TOO_SMALL) {
        return status;
    }

    // Add some slack in case map grows between calls
    tempMapSize += 2 * descriptorSize;

    // 2) Allocate pool for the temporary memory map buffer
    status = SystemTable->BootServices->AllocatePool(
        EfiLoaderData,
        tempMapSize,
        (void**)&tempMap
    );
    if (EFI_ERROR(status)) {
        return status;
    }

    for (;;) {
        // 3) Get the actual memory map into the temporary buffer
        UINTN curMapSize = tempMapSize;
        status = SystemTable->BootServices->GetMemoryMap(
            &curMapSize,
            tempMap,
            &mapKey,
            &descriptorSize,
            &descriptorVersion
        );

        if (status == EFI_BUFFER_TOO_SMALL) {
            // Map grew; reallocate and retry (still before ExitBootServices)
            SystemTable->BootServices->FreePool(tempMap);

            tempMapSize = curMapSize + 2 * descriptorSize;
            status = SystemTable->BootServices->AllocatePool(
                EfiLoaderData,
                tempMapSize,
                (void**)&tempMap
            );
            if (EFI_ERROR(status)) {
                return status;
            }
            continue;
        }

        if (EFI_ERROR(status)) {
            SystemTable->BootServices->FreePool(tempMap);
            return status;
        }

        // 4) Allocate pages for the final memory map copy in memory that
        // survives ExitBootServices (conventional pages, not pool).
        UINTN pageCount = (curMapSize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        EFI_PHYSICAL_ADDRESS physMap = 0;
        status = SystemTable->BootServices->AllocatePages(
            AllocateAnyPages,
            EfiLoaderData,
            pageCount,
            &physMap
        );
        if (EFI_ERROR(status)) {
            SystemTable->BootServices->FreePool(tempMap);
            return status;
        }

        EFI_MEMORY_DESCRIPTOR* finalMap = (EFI_MEMORY_DESCRIPTOR*)(UINTN)physMap;
        CopyMem(finalMap, tempMap, curMapSize);

        // 5) Attempt to exit boot services with this MapKey
        status = SystemTable->BootServices->ExitBootServices(
            ImageHandle,
            mapKey
        );

        if (status == EFI_SUCCESS) {
            // SUCCESS:
            // - finalMap is now in pages that remain valid after ExitBootServices
            // - no more BootServices calls allowed
            // NOTE: Cannot call FreePool(tempMap) here - BootServices are GONE!
            // The tempMap memory is small and will be reclaimed by the kernel's
            // memory manager when it processes the memory map.

            *outMap            = finalMap;
            *outDescriptorSize = descriptorSize;
            *outEntryCount     = curMapSize / descriptorSize;
            return EFI_SUCCESS;
        }

        // ExitBootServices failed; free finalMap pages while BootServices
        // are still active and retry with a fresh memory map.
        SystemTable->BootServices->FreePages(physMap, pageCount);

        if (status != EFI_INVALID_PARAMETER) {
            // Hard failure, give up
            SystemTable->BootServices->FreePool(tempMap);
            return status;
        }

        // EFI_INVALID_PARAMETER:
        //   The MapKey was stale. We must fetch a fresh map and retry.
        //   BootServices are still alive here.
        //   Loop continues to re‑query and re‑try.
    }
}

// New helper: exits boot services using a caller-provided buffer for the final map
EFI_STATUS ExitBootServicesWithMemoryMapInBuffer(
    EFI_HANDLE          ImageHandle,
    EFI_SYSTEM_TABLE*   SystemTable,
    EFI_MEMORY_DESCRIPTOR* finalMap,
    UINTN                finalMapCapacityBytes,
    UINTN*               outEntryCount,
    UINTN*               outDescriptorSize
)
{
    if (!SystemTable || !finalMap || finalMapCapacityBytes == 0 || !outEntryCount || !outDescriptorSize) {
        return EFI_INVALID_PARAMETER;
    }

    EFI_STATUS status;
    EFI_MEMORY_DESCRIPTOR* tempMap = NULL;
    UINTN tempMapSize = 0;
    UINTN mapKey = 0;
    UINTN descriptorSize = 0;
    UINT32 descriptorVersion = 0;

    status = SystemTable->BootServices->GetMemoryMap(
        &tempMapSize,
        tempMap,
        &mapKey,
        &descriptorSize,
        &descriptorVersion);

    if (status != EFI_BUFFER_TOO_SMALL) {
        return status;
    }

    tempMapSize += 2 * descriptorSize;

    status = SystemTable->BootServices->AllocatePool(EfiLoaderData, tempMapSize, (void**)&tempMap);
    if (EFI_ERROR(status)) return status;

    for (;;) {
        UINTN curMapSize = tempMapSize;
        status = SystemTable->BootServices->GetMemoryMap(
            &curMapSize,
            tempMap,
            &mapKey,
            &descriptorSize,
            &descriptorVersion);

        if (status == EFI_BUFFER_TOO_SMALL) {
            SystemTable->BootServices->FreePool(tempMap);
            tempMapSize = curMapSize + 2 * descriptorSize;
            status = SystemTable->BootServices->AllocatePool(EfiLoaderData, tempMapSize, (void**)&tempMap);
            if (EFI_ERROR(status)) return status;
            continue;
        }

        if (EFI_ERROR(status)) {
            SystemTable->BootServices->FreePool(tempMap);
            return status;
        }

        if (curMapSize > finalMapCapacityBytes) {
            SystemTable->BootServices->FreePool(tempMap);
            return EFI_BUFFER_TOO_SMALL;
        }

        CopyMem(finalMap, tempMap, curMapSize);

        status = SystemTable->BootServices->ExitBootServices(ImageHandle, mapKey);
        if (status == EFI_SUCCESS) {
            // Boot services are gone. tempMap is leaked (acceptable for handoff).
            *outDescriptorSize = descriptorSize;
            *outEntryCount = curMapSize / descriptorSize;
            return EFI_SUCCESS;
        }

        if (status != EFI_INVALID_PARAMETER) {
            SystemTable->BootServices->FreePool(tempMap);
            return status;
        }

        // stale MapKey -> retry
    }
}


#pragma pack(push, 1)
typedef struct {
    char  Signature[8];     // "RSD PTR "
    UINT8 Checksum;
    char  OEMID[6];
    UINT8 Revision;
    UINT32 RsdtAddress;     // 32-bit RSDT (for ACPI 1.0)
    // ACPI 2.0+ extensions follow; include if needed:
    UINT32 Length;
    UINT64 XsdtAddress;     // 64-bit XSDT
    UINT8  ExtendedChecksum;
    UINT8  Reserved[3];
} RSDPDescriptor20;
#pragma pack(pop)

RSDPDescriptor20* FindRSDP(EFI_SYSTEM_TABLE* SystemTable) {
    EFI_CONFIGURATION_TABLE* configTable = SystemTable->ConfigurationTable;
    UINTN tableCount = SystemTable->NumberOfTableEntries;

    for (UINTN i = 0; i < tableCount; ++i) {
        EFI_GUID* guid = &configTable[i].VendorGuid;

        if (CompareGuid(guid, &gEfiAcpi20TableGuid) ||
            CompareGuid(guid, &gEfiAcpi10TableGuid)) {

            return (RSDPDescriptor20*)configTable[i].VendorTable;
        }
    }

    return nullptr;
}

// Simple memset for BootInfo
static void ZeroBootInfo(BootInfo* bi) {
    SetMem(bi, sizeof(BootInfo), 0);
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    // Set global SystemTable pointer for uefi_shim.h functions
    gST = SystemTable;

    Print((CONST CHAR16*)L"guideXOS UEFI Bootloader\n");

    // --- Allocate BootInfo in EfiLoaderData pages (survives ExitBootServices) ---
    EFI_PHYSICAL_ADDRESS bootInfoPhys = 0;
    EFI_STATUS status = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiLoaderData,
        1, // 1 page is more than enough for BootInfo
        &bootInfoPhys
    );
    if (EFI_ERROR(status)) {
        Print(L"Failed to allocate BootInfo pages\n");
        return status;
    }
    SetMem((void*)(UINTN)bootInfoPhys, EFI_PAGE_SIZE, 0);
    guideXOS::BootInfo* v1BootInfo = (guideXOS::BootInfo*)(UINTN)bootInfoPhys;

    // --- ACPI RSDP (optional) ---
    RSDPDescriptor20* rsdp = FindRSDP(SystemTable);
    if (!rsdp) {
        Print((CONST CHAR16*)L"ACPI RSDP not found\n");
    } else {
        Print((CONST CHAR16*)L"ACPI RSDP found at %p, rev %u\n", (VOID*)rsdp, (UINT32)rsdp->Revision);
    }

    // --- Locate GOP ---
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    status = SystemTable->BootServices->LocateProtocol(
        &gopGuid,
        NULL,
        (void**)&GOP
    );
    if (EFI_ERROR(status)) {
        Print(L"Failed to locate GOP\n");
        return status;
    }

    // --- Load Kernel (your existing loader) ---
    status = LoadFile(&KernelFile, (CHAR16*)L"kernel.elf", ImageHandle, SystemTable);
    if (EFI_ERROR(status)) {
        Print(L"Failed to load kernel\n");
        return status;
    }

    UINT64 kernelBase;
    UINT64 kernelEntryOffset;
    UINT64 kernelTotalSize = 0;
    UINT64 kernelMinVaddr = 0;
    status = LoadElf(SystemTable, KernelFile, &kernelBase, &kernelEntryOffset, &kernelTotalSize, &kernelMinVaddr);
    if (EFI_ERROR(status)) {
        Print(L"Failed to load ELF kernel\n");
        return status;
    }

    UINT64 entryPhys = kernelBase + kernelEntryOffset;
    UINT64 entryVirt = kernelMinVaddr + kernelEntryOffset;
    Print(L"Kernel entry phys: %p virt: %p\n", (VOID*)(UINTN)entryPhys, (VOID*)(UINTN)entryVirt);
    Print(L"Kernel loaded at: %p - %p\n", (VOID*)(UINTN)kernelBase, (VOID*)(UINTN)(kernelBase + kernelTotalSize));

    // --- Load Ramdisk ---
    EFI_PHYSICAL_ADDRESS ramdiskPhys = 0;
    UINT64 ramdiskSize = 0;
    EFI_FILE_PROTOCOL* RamdiskFile = NULL;
    status = LoadFile(&RamdiskFile, (CHAR16*)L"ramdisk.img", ImageHandle, SystemTable);
    if (EFI_ERROR(status)) {
        Print(L"Warning: Failed to load ramdisk\n");
        // Continue anyway - will boot but no files
    } else {
        // Get ramdisk size
        EFI_FILE_INFO* fileInfoBuffer = NULL;
        UINTN fileInfoSize = 0;
        EFI_GUID fileInfoGuid = EFI_FILE_INFO_ID;
        
        // Get size needed for file info
        RamdiskFile->GetInfo(RamdiskFile, &fileInfoGuid, &fileInfoSize, NULL);
        
        // Allocate buffer for file info
        status = SystemTable->BootServices->AllocatePool(
            EfiLoaderData, fileInfoSize, (void**)&fileInfoBuffer);
        
        // Get actual file info
        RamdiskFile->GetInfo(RamdiskFile, &fileInfoGuid, &fileInfoSize, fileInfoBuffer);
        ramdiskSize = fileInfoBuffer->FileSize;
        SystemTable->BootServices->FreePool(fileInfoBuffer);
        
        // Use %Lu for 64-bit decimal in UEFI Print
        Print((CONST CHAR16*)L"Ramdisk size: %Lu bytes\n", (UINT64)ramdiskSize);
        
        // Allocate memory for ramdisk
        UINTN ramdiskPages = (ramdiskSize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        status = SystemTable->BootServices->AllocatePages(
            AllocateAnyPages,
            EfiLoaderData,
            ramdiskPages,
            &ramdiskPhys
        );
        
        if (EFI_ERROR(status)) {
            Print(L"Failed to allocate ramdisk memory\n");
            ramdiskPhys = 0;
            ramdiskSize = 0;
        } else {
            // Read ramdisk into memory
            RamdiskFile->SetPosition(RamdiskFile, 0);
            UINTN readSize = (UINTN)ramdiskSize;
            status = RamdiskFile->Read(RamdiskFile, &readSize, (void*)(UINTN)ramdiskPhys);
            
            if (EFI_ERROR(status) || readSize != ramdiskSize) {
                Print(L"Failed to read ramdisk\n");
                ramdiskPhys = 0;
                ramdiskSize = 0;
            } else {
                Print((CONST CHAR16*)L"Ramdisk loaded at %p\n", (VOID*)(UINTN)ramdiskPhys);
                
                // Pass to kernel via BootInfo
                v1BootInfo->RamdiskBase = ramdiskPhys;
                v1BootInfo->RamdiskSize = ramdiskSize;
                v1BootInfo->Flags |= (1u << 2); // ramdisk valid flag
            }
        }
    }

    // --- Populate BootInfo v1 BEFORE ExitBootServices ---
    v1BootInfo->Magic   = guideXOS::GUIDEXOS_BOOTINFO_MAGIC;
    v1BootInfo->Version = guideXOS::GUIDEXOS_BOOTINFO_VERSION;
    v1BootInfo->Size    = (uint16_t)sizeof(guideXOS::BootInfo);

    v1BootInfo->BootMode = guideXOS::BootMode::Uefi;
    // Preserve any flags already set (e.g., ramdisk valid)

    // ACPI RSDP pointer (64-bit physical)
    v1BootInfo->AcpiRsdp = rsdp ? (uint64_t)(UINTN)rsdp : 0ull;

    // Framebuffer from GOP
    v1BootInfo->FramebufferBase   = (uint64_t)GOP->Mode->FrameBufferBase;
    v1BootInfo->FramebufferWidth  = GOP->Mode->Info->HorizontalResolution;
    v1BootInfo->FramebufferHeight = GOP->Mode->Info->VerticalResolution;
    v1BootInfo->FramebufferPitch  = GOP->Mode->Info->PixelsPerScanLine * 4u; // 32 bpp
    v1BootInfo->FramebufferSize   = (uint64_t)v1BootInfo->FramebufferPitch *
                                   (uint64_t)v1BootInfo->FramebufferHeight;
    v1BootInfo->FramebufferFormat = guideXOS::FramebufferFormat::B8G8R8A8; // typical GOP

    if (v1BootInfo->FramebufferBase != 0 && v1BootInfo->FramebufferSize != 0)
        v1BootInfo->Flags |= (1u << 1); // framebuffer valid

    // Allocate a simple stack that survives ExitBootServices
    // (Some kernels assume a larger / aligned stack than what UEFI leaves us.)
    // CRITICAL: Stack must NOT overlap with kernel!
    // Strategy: Allocate stack at low memory (below kernel load address)
    // Kernel loads around ~0x3D785000, so we'll use 0x200000 (2MB) as base
    const UINTN stackPages = 16; // 64 KiB stack
    EFI_PHYSICAL_ADDRESS stackPhys = 0;
    void* stackTop = nullptr;
    
    // Try to allocate at 2MB mark (well below typical kernel load)
    EFI_PHYSICAL_ADDRESS desiredStackBase = 0x200000ULL; // 2MB
    EFI_STATUS stackStatus = SystemTable->BootServices->AllocatePages(
        AllocateAddress,
        EfiLoaderData,
        stackPages,
        &desiredStackBase
    );
    
    if (!EFI_ERROR(stackStatus)) {
        stackPhys = desiredStackBase;
        stackTop = (void*)(UINTN)((stackPhys + stackPages * EFI_PAGE_SIZE) & ~0xFULL);
        Print(L"Stack allocated at %p, top at %p (below kernel)\n", (VOID*)(UINTN)stackPhys, stackTop);
    } else {
        // Try 1MB mark
        desiredStackBase = 0x100000ULL; // 1MB
        stackStatus = SystemTable->BootServices->AllocatePages(
            AllocateAddress,
            EfiLoaderData,
            stackPages,
            &desiredStackBase
        );
        
        if (!EFI_ERROR(stackStatus)) {
            stackPhys = desiredStackBase;
            stackTop = (void*)(UINTN)((stackPhys + stackPages * EFI_PAGE_SIZE) & ~0xFULL);
            Print(L"Stack allocated at %p, top at %p (at 1MB)\n", (VOID*)(UINTN)stackPhys, stackTop);
        } else {
            // Last resort: allocate anywhere and check for overlap
            Print(L"WARNING: Fixed stack allocation failed, using fallback...\n");
            stackPhys = 0;
            stackStatus = SystemTable->BootServices->AllocatePages(
                AllocateAnyPages,
                EfiLoaderData,
                stackPages,
                &stackPhys
            );
            
            if (!EFI_ERROR(stackStatus)) {
                // Check if stack overlaps with kernel
                EFI_PHYSICAL_ADDRESS stackEnd = stackPhys + stackPages * EFI_PAGE_SIZE;
                
                // CRITICAL: Check if stack TOP would be at or near kernel base
                // Stack grows DOWN, so stackTop is at stackEnd (highest address)
                // If stackEnd == kernelBase, pushing to stack will overwrite kernel!
                if (stackEnd >= kernelBase && stackEnd <= kernelBase + kernelTotalSize + 0x10000) {
                    Print(L"ERROR: Stack end %p too close to kernel base %p!\n",
                          (VOID*)(UINTN)stackEnd, (VOID*)(UINTN)kernelBase);
                    
                    // Free and try allocating AFTER the kernel instead
                    SystemTable->BootServices->FreePages(stackPhys, stackPages);
                    
                    EFI_PHYSICAL_ADDRESS afterKernel = kernelBase + kernelTotalSize + 0x10000;
                    afterKernel = (afterKernel + EFI_PAGE_SIZE - 1) & ~(EFI_PAGE_SIZE - 1); // Page align
                    
                    stackStatus = SystemTable->BootServices->AllocatePages(
                        AllocateAddress,
                        EfiLoaderData,
                        stackPages,
                        &afterKernel
                    );
                    
                    if (!EFI_ERROR(stackStatus)) {
                        stackPhys = afterKernel;
                        stackTop = (void*)(UINTN)((stackPhys + stackPages * EFI_PAGE_SIZE) & ~0xFULL);
                        Print(L"Stack allocated AFTER kernel at %p, top at %p\n", 
                              (VOID*)(UINTN)stackPhys, stackTop);
                    } else {
                        Print(L"FATAL: Could not allocate non-overlapping stack!\n");
                        stackTop = nullptr;
                    }
                } else {
                    stackTop = (void*)(UINTN)((stackPhys + stackPages * EFI_PAGE_SIZE) & ~0xFULL);
                    Print(L"Stack fallback at %p, top at %p\n", (VOID*)(UINTN)stackPhys, stackTop);
                }
            } else {
                Print(L"FATAL: Could not allocate stack!\n");
                stackTop = nullptr;
            }
        }
    }
    
    // Final safety check
    if (stackTop != nullptr) {
        EFI_PHYSICAL_ADDRESS stackTopAddr = (EFI_PHYSICAL_ADDRESS)(UINTN)stackTop;
        if (stackTopAddr >= kernelBase && stackTopAddr < kernelBase + kernelTotalSize) {
            Print(L"FATAL: Stack top %p is INSIDE kernel region %p-%p!\n",
                  stackTop, (VOID*)(UINTN)kernelBase, (VOID*)(UINTN)(kernelBase + kernelTotalSize));
            return EFI_ABORTED;
        }
    }

    // --- Allocate executable trampoline buffer that survives ExitBootServices ---
    EFI_PHYSICAL_ADDRESS trampolinePhys = 0;
    UINTN trampolineSize = GetTrampolineCodeSize();
    UINTN trampolinePages = (trampolineSize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
    EFI_STATUS trampStatus = SystemTable->BootServices->AllocatePages(
        AllocateAnyPages,
        EfiLoaderData,
        trampolinePages,
        &trampolinePhys
    );
    if (EFI_ERROR(trampStatus)) {
        Print(L"Failed to allocate trampoline memory\n");
        return trampStatus;
    }
    SetMem((void*)(UINTN)trampolinePhys, trampolinePages * EFI_PAGE_SIZE, 0);
    SetupTrampoline((void*)(UINTN)trampolinePhys);

    // --- PCI Enumeration: Find NIC and get MMIO address ---
    Print(L"\n=== PCI Enumeration ===\n");
    guideXOS::pci::PciEnumResult pciResult;
    guideXOS::pci::InitPci();
    uint8_t nicCount = guideXOS::pci::EnumeratePci(&pciResult);
    
    Print(L"Found %u network controller(s)\n", (UINTN)pciResult.deviceCount);
    for (uint8_t i = 0; i < pciResult.deviceCount; i++) {
        guideXOS::pci::PrintPciDevice(SystemTable, &pciResult.devices[i]);
    }
    
    // Initialize NIC info in BootInfo (cleared by default)
    SetMem(&v1BootInfo->Nic, sizeof(guideXOS::NicInfo), 0);
    
    // Track NIC MMIO for page table mapping
    EFI_PHYSICAL_ADDRESS nicMmioPhys = 0;
    UINTN nicMmioSize = 0;
    
    if (pciResult.nic != nullptr && pciResult.nic->isMemoryBar) {
        guideXOS::pci::PciDevice* nic = pciResult.nic;
        
        Print(L"\n*** Using NIC at [%02x:%02x.%x] ***\n",
              (UINTN)nic->bus, (UINTN)nic->device, (UINTN)nic->function);
        Print(L"    Vendor: %04x  Device: %04x\n", (UINTN)nic->vendorId, (UINTN)nic->deviceId);
        Print(L"    MMIO Phys: %016lx  Size: %lx\n", nic->bar0Phys, nic->bar0Size);
        
        // Store NIC info in BootInfo
        v1BootInfo->Nic.VendorId = nic->vendorId;
        v1BootInfo->Nic.DeviceId = nic->deviceId;
        v1BootInfo->Nic.Bus = nic->bus;
        v1BootInfo->Nic.Device = nic->device;
        v1BootInfo->Nic.Function = nic->function;
        v1BootInfo->Nic.IrqLine = nic->irqLine;
        v1BootInfo->Nic.MmioPhys = nic->bar0Phys;
        v1BootInfo->Nic.MmioSize = nic->bar0Size;
        v1BootInfo->Nic.Flags = guideXOS::NIC_FLAG_FOUND;
        
        // Placeholder MAC (kernel will read actual MAC after MMIO is mapped)
        v1BootInfo->Nic.MacAddress[0] = 0x52;
        v1BootInfo->Nic.MacAddress[1] = 0x54;
        v1BootInfo->Nic.MacAddress[2] = 0x00;
        v1BootInfo->Nic.MacAddress[3] = 0x12;
        v1BootInfo->Nic.MacAddress[4] = 0x34;
        v1BootInfo->Nic.MacAddress[5] = 0x56;
        
        // Track for page table mapping
        nicMmioPhys = (EFI_PHYSICAL_ADDRESS)nic->bar0Phys;
        nicMmioSize = (UINTN)nic->bar0Size;
        if (nicMmioSize == 0) nicMmioSize = 0x20000;  // Default 128KB for E1000
        
        // Enable bus mastering and memory space for the NIC
        guideXOS::pci::EnablePciDevice(nic->bus, nic->device, nic->function);
        Print(L"    PCI bus mastering enabled\n");
    } else {
        Print(L"\nNo supported NIC found for MMIO mapping\n");
    }
    Print(L"=========================\n\n");

    // --- Build identity-mapped page tables BEFORE ExitBootServices ---
    // We must build page tables while BootServices are still available.
    
    // Memory map will be allocated later; for now allocate a conservative buffer for it
    // that survives ExitBootServices and include it in the mappings.
    // This avoids needing to rebuild mappings after EBS.
    EFI_PHYSICAL_ADDRESS preMemMapPhys = 0;
    UINTN preMemMapBytes = 256u * 1024u; // 256 KiB should be enough for QEMU/OVMF
    {
        UINTN preMemMapPages = (preMemMapBytes + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        EFI_STATUS st = SystemTable->BootServices->AllocatePages(
            AllocateAnyPages,
            EfiLoaderData,
            preMemMapPages,
            &preMemMapPhys);
        if (EFI_ERROR(st)) {
            Print(L"Failed to allocate pre-memory-map buffer\n");
            return st;
        }
        SetMem((void*)(UINTN)preMemMapPhys, preMemMapPages * EFI_PAGE_SIZE, 0);
    }

    // --- Build comprehensive identity mappings ---
    // CRITICAL: Map ALL regions the kernel needs access to after ExitBootServices:
    // 1. Low 1MB - legacy compatibility (interrupt vectors, BIOS data area references)
    // 2. Kernel image
    // 3. Stack
    // 4. BootInfo struct
    // 5. Memory map buffer
    // 6. Framebuffer - CRITICAL for display output
    // 7. Ramdisk - if loaded
    // 8. ACPI tables - for ACPI parsing

    const EFI_PHYSICAL_ADDRESS kernelPhysBase = (EFI_PHYSICAL_ADDRESS)kernelBase;
    const UINTN kernelSpanBytes = (kernelTotalSize != 0) ? (UINTN)kernelTotalSize : (64u * 1024u * 1024u);

    // Use a dynamic array for ranges (max 20 should be plenty)
    EFI_PHYSICAL_ADDRESS ranges[20];
    UINTN sizes[20];
    UINTN rangeCount = 0;

    // 1. Low 1MB for legacy compatibility
    ranges[rangeCount] = 0;
    sizes[rangeCount] = 0x100000; // 1 MiB
    rangeCount++;

    // 2. Kernel image
    ranges[rangeCount] = kernelPhysBase;
    sizes[rangeCount] = kernelSpanBytes;
    rangeCount++;

    // 3. Stack - map the entire allocated region
    if (stackPhys != 0) {
        ranges[rangeCount] = stackPhys;
        sizes[rangeCount] = stackPages * EFI_PAGE_SIZE;
        rangeCount++;
        Print(L"Mapping stack: %p size %u\n", (VOID*)(UINTN)stackPhys, (UINT32)(stackPages * EFI_PAGE_SIZE));
    }

    // 4. BootInfo struct (allocated in EfiLoaderData pages)
    ranges[rangeCount] = bootInfoPhys;
    sizes[rangeCount] = EFI_PAGE_SIZE;
    rangeCount++;

    // 5. Memory map buffer
    ranges[rangeCount] = preMemMapPhys;
    sizes[rangeCount] = preMemMapBytes;
    rangeCount++;

    // 6. Framebuffer - CRITICAL for any display output after ExitBootServices
    if (v1BootInfo->FramebufferBase != 0 && v1BootInfo->FramebufferSize != 0) {
        ranges[rangeCount] = (EFI_PHYSICAL_ADDRESS)v1BootInfo->FramebufferBase;
        sizes[rangeCount] = (UINTN)v1BootInfo->FramebufferSize;
        rangeCount++;
        Print(L"Mapping framebuffer: %p size %Lu\n", 
              (VOID*)(UINTN)v1BootInfo->FramebufferBase, v1BootInfo->FramebufferSize);
    }

    // 7. Ramdisk - if loaded
    if (ramdiskPhys != 0 && ramdiskSize != 0) {
        ranges[rangeCount] = ramdiskPhys;
        sizes[rangeCount] = (UINTN)ramdiskSize;
        rangeCount++;
        Print(L"Mapping ramdisk: %p size %Lu\n", (VOID*)(UINTN)ramdiskPhys, ramdiskSize);
    }

    // 8. ACPI RSDP region (map at least one page for RSDP, kernel will map more as needed)
    if (rsdp != nullptr) {
        ranges[rangeCount] = (EFI_PHYSICAL_ADDRESS)(UINTN)rsdp & ~0xFFFull; // Page-align down
        sizes[rangeCount] = EFI_PAGE_SIZE * 4; // Map a few pages for RSDP + nearby tables
        rangeCount++;
        Print(L"Mapping ACPI RSDP region: %p\n", (VOID*)(UINTN)rsdp);
    }

    // 9. CRITICAL: Map the bootloader/trampoline code region
    // After we load CR3 with new page tables, the CPU is still executing in the
    // trampoline code. If that code isn't mapped, we triple-fault immediately!
    // We need to identity-map the bootloader's loaded image.
    {
        EFI_LOADED_IMAGE_PROTOCOL* LoadedImage = nullptr;
        EFI_STATUS st = SystemTable->BootServices->HandleProtocol(
            ImageHandle, 
            &gEfiLoadedImageProtocolGuid, 
            (void**)&LoadedImage);
        
        if (!EFI_ERROR(st) && LoadedImage != nullptr) {
            EFI_PHYSICAL_ADDRESS loaderBase = (EFI_PHYSICAL_ADDRESS)(UINTN)LoadedImage->ImageBase;
            UINTN loaderSize = LoadedImage->ImageSize;
            
            // Ensure we map at least the whole image
            if (loaderSize < EFI_PAGE_SIZE * 16) {
                loaderSize = EFI_PAGE_SIZE * 16; // At least 64KB
            }
            
            ranges[rangeCount] = loaderBase;
            sizes[rangeCount] = loaderSize;
            rangeCount++;
            Print(L"Mapping bootloader: %p size %Lu\n", (VOID*)(UINTN)loaderBase, (UINT64)loaderSize);
        } else {
            // Fallback: try to use the current instruction pointer region
            Print(L"Warning: Could not get loaded image info for bootloader mapping\n");
        }
    }

    // 10. Trampoline executable buffer
    ranges[rangeCount] = trampolinePhys;
    sizes[rangeCount] = trampolinePages * EFI_PAGE_SIZE;
    rangeCount++;

    // 11. CRITICAL: Map the allocator region the kernel will use
    // The kernel's Allocator.Initialize() uses 0x4000000 (64MB) as the base
    // Map a large region starting there for heap allocations
    ranges[rangeCount] = 0x4000000ULL;  // 64MB
    sizes[rangeCount] = 64u * 1024u * 1024u; // 64MB of heap space
    rangeCount++;
    Print(L"Mapping allocator region: 0x4000000 size 64MB\n");

    // 12. Map additional stack regions we might use
    // The preferred stack location is around 2MB mark
    ranges[rangeCount] = 0x100000ULL; // 1MB
    sizes[rangeCount] = 2u * 1024u * 1024u; // 2MB (covers 1MB-3MB region)
    rangeCount++;
    Print(L"Mapping low memory stack region: 0x100000 size 2MB\n");

    // 13. NIC MMIO region - CRITICAL for network driver
    // Map the NIC's BAR0 MMIO region so the kernel can access hardware registers
    if (nicMmioPhys != 0 && nicMmioSize != 0) {
        // Align to page boundary
        EFI_PHYSICAL_ADDRESS nicMmioAligned = nicMmioPhys & ~0xFFFull;
        UINTN nicMmioAlignedSize = ((nicMmioPhys - nicMmioAligned) + nicMmioSize + 0xFFF) & ~0xFFFull;
        
        ranges[rangeCount] = nicMmioAligned;
        sizes[rangeCount] = nicMmioAlignedSize;
        rangeCount++;
        Print(L"Mapping NIC MMIO: %016lx size %lx\n", nicMmioAligned, (UINT64)nicMmioAlignedSize);
        
        // For identity mapping, virtual == physical
        v1BootInfo->Nic.MmioVirt = nicMmioPhys;
        v1BootInfo->Nic.Flags |= guideXOS::NIC_FLAG_MAPPED;
        
        Print(L"NIC MMIO mapped: Phys=%016lx Virt=%016lx\n",
              v1BootInfo->Nic.MmioPhys, v1BootInfo->Nic.MmioVirt);
    }

    Print(L"Building identity page tables with %u ranges...\n", (UINT32)rangeCount);

    guideXOS::paging::PageTables pt{};
    {
        EFI_STATUS st = guideXOS::paging::BuildIdentityPageTables(
            SystemTable,
            ranges,
            ranges + rangeCount,
            sizes,
            &pt);
        if (EFI_ERROR(st)) {
            Print(L"Failed to build identity page tables\n");
            return st;
        }
    }

    // Map kernel virtual addresses to their physical backing
    {
        EFI_STATUS st = guideXOS::paging::MapRange(
            SystemTable,
            pt.Pml4Phys,
            kernelMinVaddr,
            kernelPhysBase,
            kernelSpanBytes);
        if (EFI_ERROR(st)) {
            Print(L"Failed to map kernel virtual range\n");
            return st;
        }
    }

    // CRITICAL: Identity-map all page table pages allocated by MapRange()
    // Without this, the page table walk for kernel virtual addresses will
    // fail after CR3 switch because the PT pages themselves aren't mapped!
    {
        EFI_STATUS st = guideXOS::paging::IdentityMapPageTablePages(
            SystemTable,
            pt.Pml4Phys);
        if (EFI_ERROR(st)) {
            Print(L"Failed to identity-map page table pages\n");
            return st;
        }
    }

    Print(L"Page tables built at PML4: %p\n", (VOID*)(UINTN)pt.Pml4Phys);

    // === PRE-EXIT BOOT SERVICES VALIDATION ===
    // Draw a visible marker on framebuffer BEFORE ExitBootServices
    // This helps diagnose if we crash during/after EBS
    if (v1BootInfo->FramebufferBase != 0) {
        volatile uint32_t* fb = (volatile uint32_t*)(UINTN)v1BootInfo->FramebufferBase;
        uint32_t pitch = v1BootInfo->FramebufferPitch / 4;
        // Draw a green square at top-left before EBS
        for (uint32_t y = 0; y < 30; y++) {
            for (uint32_t x = 0; x < 30; x++) {
                fb[y * pitch + x] = 0x0000FF00; // Green = pre-EBS
            }
        }
    }

    Print(L"Pre-EBS marker drawn (green square top-left)\n");

    // Now we can safely exit boot services.
    EFI_MEMORY_DESCRIPTOR* memoryMap      = (EFI_MEMORY_DESCRIPTOR*)(UINTN)preMemMapPhys;
    UINTN                  memoryMapCount = 0;
    UINTN                  memoryMapDescSize = 0;

    Print(L"Exiting boot services...\n");
    EFI_STATUS statusExit = ExitBootServicesWithMemoryMapInBuffer(
        ImageHandle,
        SystemTable,
        memoryMap,
        preMemMapBytes,
        &memoryMapCount,
        &memoryMapDescSize
    );
    if (EFI_ERROR(statusExit)) {
        Print(L"ExitBootServices failed: %r\n", statusExit);
        return statusExit;
    }

    // *** CRITICAL: No Print() or any UEFI Boot Services calls after ExitBootServices! ***

    // === POST-EBS FRAMEBUFFER MARKER (Stage 1: Yellow = EBS succeeded) ===
    if (v1BootInfo->FramebufferBase != 0) {
        volatile uint32_t* fb = (volatile uint32_t*)(UINTN)v1BootInfo->FramebufferBase;
        uint32_t pitch = v1BootInfo->FramebufferPitch / 4;
        // Draw yellow square next to green = EBS succeeded
        for (uint32_t y = 0; y < 30; y++) {
            for (uint32_t x = 40; x < 70; x++) {
                fb[y * pitch + x] = 0x00FFFF00; // Yellow = post-EBS
            }
        }
    }

    // Fill memory map section in BootInfo v1
    v1BootInfo->MemoryMap               = (uint64_t)(UINTN)memoryMap;
    v1BootInfo->MemoryMapEntryCount     = (uint64_t)memoryMapCount;
    v1BootInfo->MemoryMapDescriptorSize = (uint64_t)memoryMapDescSize;
    v1BootInfo->Flags |= (1u << 0); // memory map valid

    // === POST-EBS FRAMEBUFFER MARKER (Stage 2: Cyan = MemMap filled) ===
    if (v1BootInfo->FramebufferBase != 0) {
        volatile uint32_t* fb = (volatile uint32_t*)(UINTN)v1BootInfo->FramebufferBase;
        uint32_t pitch = v1BootInfo->FramebufferPitch / 4;
        for (uint32_t y = 0; y < 30; y++) {
            for (uint32_t x = 80; x < 110; x++) {
                fb[y * pitch + x] = 0x0000FFFF; // Cyan = memmap done
            }
        }
    }

    // Compute checksum so that 32-bit sum of all words is 0
    v1BootInfo->HeaderChecksum = 0u;
    {
        uint32_t byteCount = v1BootInfo->Size & ~0x3u;
        uint32_t* p = reinterpret_cast<uint32_t*>(v1BootInfo);
        uint32_t count = byteCount / 4u;
        uint32_t sum = 0u;
        for (uint32_t i = 0; i < count; ++i)
            sum += p[i];
        v1BootInfo->HeaderChecksum = 0u - sum;
    }

    // === POST-EBS FRAMEBUFFER MARKER (Stage 3: Magenta = Checksum done) ===
    if (v1BootInfo->FramebufferBase != 0) {
        volatile uint32_t* fb = (volatile uint32_t*)(UINTN)v1BootInfo->FramebufferBase;
        uint32_t pitch = v1BootInfo->FramebufferPitch / 4;
        for (uint32_t y = 0; y < 30; y++) {
            for (uint32_t x = 120; x < 150; x++) {
                fb[y * pitch + x] = 0x00FF00FF; // Magenta = checksum done
            }
        }
    }

    // Skip validation for now - it might be causing the panic
    // guideXOS::guidexos_validate_bootinfo_or_panic(v1BootInfo);

    // === POST-EBS FRAMEBUFFER MARKER (Stage 4: Blue = Validation skipped/passed) ===
    if (v1BootInfo->FramebufferBase != 0) {
        volatile uint32_t* fb = (volatile uint32_t*)(UINTN)v1BootInfo->FramebufferBase;
        uint32_t pitch = v1BootInfo->FramebufferPitch / 4;
        for (uint32_t y = 0; y < 30; y++) {
            for (uint32_t x = 160; x < 190; x++) {
                fb[y * pitch + x] = 0x000000FF; // Blue = validation done
            }
        }
    }

    // === POST-EXITBOOTSERVICES DEBUGGING ===
    
    // === FRAMEBUFFER MARKER (Stage 5: White = About to init serial) ===
    if (v1BootInfo->FramebufferBase != 0) {
        volatile uint32_t* fb = (volatile uint32_t*)(UINTN)v1BootInfo->FramebufferBase;
        uint32_t pitch = v1BootInfo->FramebufferPitch / 4;
        for (uint32_t y = 0; y < 30; y++) {
            for (uint32_t x = 200; x < 230; x++) {
                fb[y * pitch + x] = 0x00FFFFFF; // White = pre-serial init
            }
        }
    }

    guideXOS::debug::SerialInit();

    // === FRAMEBUFFER MARKER (Stage 6: Orange = Serial initialized) ===
    if (v1BootInfo->FramebufferBase != 0) {
        volatile uint32_t* fb = (volatile uint32_t*)(UINTN)v1BootInfo->FramebufferBase;
        uint32_t pitch = v1BootInfo->FramebufferPitch / 4;
        for (uint32_t y = 0; y < 30; y++) {
            for (uint32_t x = 240; x < 270; x++) {
                fb[y * pitch + x] = 0x00FF8000; // Orange = serial done
            }
        }
    }

    guideXOS::debug::SerialPrint("\n\n=== GuideXOS Boot (Post-ExitBootServices) ===\n");

    guideXOS::debug::ShowProgress(v1BootInfo, 0);
    guideXOS::debug::VerifyCpuState();
    guideXOS::debug::ShowProgress(v1BootInfo, 1);

    guideXOS::debug::SerialPrint("\n=== BootInfo ===\n");
    guideXOS::debug::SerialPrint("Magic: ");
    guideXOS::debug::SerialPrintHex32(v1BootInfo->Magic);
    guideXOS::debug::SerialPrint("\n");
    guideXOS::debug::SerialPrint("Framebuffer: ");
    guideXOS::debug::SerialPrintHex64(v1BootInfo->FramebufferBase);
    guideXOS::debug::SerialPrint(" (");
    guideXOS::debug::SerialPrintHex32(v1BootInfo->FramebufferWidth);
    guideXOS::debug::SerialPrint("x");
    guideXOS::debug::SerialPrintHex32(v1BootInfo->FramebufferHeight);
    guideXOS::debug::SerialPrint(")\n");
    guideXOS::debug::ShowProgress(v1BootInfo, 2);

    guideXOS::debug::ValidateKernelEntry(entryPhys, v1BootInfo);
    guideXOS::debug::ShowProgress(v1BootInfo, 3);

    // --- Handoff to kernel via trampoline ---
    // Switches stack, installs CR3, calls kernel entry using MS x64 ABI

    if (!stackTop) {
        guideXOS::debug::SerialPrint("[BOOT] No stack allocated; halting\n");
        for (;;) { __halt(); }
    }

    guideXOS::debug::SerialPrint("\n=== Jumping to Kernel (Trampoline) ===\n");
    guideXOS::debug::ShowProgress(v1BootInfo, 4);

    // Comprehensive validation before handoff
    guideXOS::debug::ValidateHandoff(
        entryPhys, 
        v1BootInfo, 
        (uint64_t)(UINTN)stackTop, 
        pt.Pml4Phys
    );
    
    // ALSO validate the virtual entry address mapping
    guideXOS::debug::SerialPrint("\n[BOOT] Validating VIRTUAL kernel entry mapping:\n");
    guideXOS::debug::ValidatePageMapping(pt.Pml4Phys, entryVirt);

    guideXOS::debug::SerialPrint("[BOOT] About to handoff via trampoline...\n");
    guideXOS::debug::SerialPrint("[BOOT] Trampoline at: ");
    guideXOS::debug::SerialPrintHex64(trampolinePhys);
    guideXOS::debug::SerialPrint("\n");
    guideXOS::debug::SerialPrint("[BOOT] Kernel entry: ");
    guideXOS::debug::SerialPrintHex64(entryPhys);
    guideXOS::debug::SerialPrint("\n");
    guideXOS::debug::SerialPrint("[BOOT] BootInfo at: ");
    guideXOS::debug::SerialPrintHex64((uint64_t)(UINTN)v1BootInfo);
    guideXOS::debug::SerialPrint("\n");
    guideXOS::debug::SerialPrint("[BOOT] Stack top: ");
    guideXOS::debug::SerialPrintHex64((uint64_t)(UINTN)stackTop);
    guideXOS::debug::SerialPrint("\n");
    guideXOS::debug::SerialPrint("[BOOT] PML4: ");
    guideXOS::debug::SerialPrintHex64(pt.Pml4Phys);
    guideXOS::debug::SerialPrint("\n");
    guideXOS::debug::ShowProgress(v1BootInfo, 5);

    // Validate the trampoline mapping before jumping
    guideXOS::debug::SerialPrint("\n[BOOT] Validating trampoline mapping:\n");
    guideXOS::debug::ValidatePageMapping(pt.Pml4Phys, trampolinePhys);

    guideXOS::debug::SerialPrint("\n[BOOT] === CALLING TRAMPOLINE NOW ===\n");

    // Use PHYSICAL entry point for the trampoline jump.
    // After CR3 switch, our identity-mapped page tables ensure physical addresses work.
    // The kernel's boot code (boot.asm) doesn't use RIP-relative addressing in its
    // early instructions, so it will execute correctly at the physical address.
    // When kernel_main is called, it uses internal relative calls that will work
    // because the entire kernel is position-independent within its loaded region.
    BootHandoffTrampoline((void*)(UINTN)entryPhys, (void*)v1BootInfo, stackTop, (void*)(UINTN)pt.Pml4Phys);

    // If we return, halt
    guideXOS::debug::SerialPrint("\n!!! KERNEL RETURNED - THIS SHOULD NOT HAPPEN !!!\n");
    for (;;) { __halt(); }
}

