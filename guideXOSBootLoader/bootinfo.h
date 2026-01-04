#pragma once
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Protocol/SimpleFileSystem.h>

//#include <efi.h>
//#include <efilib.h>

// This structure must be compatible with the VBEInfo struct in the C# kernel
// We will fill it with data from UEFI's GOP.
typedef struct {
    UINT64 PhysBase;
    UINT16 ScreenWidth;
    UINT16 ScreenHeight;
    UINT16 Pitch;
    UINT16 BitsPerPixel;
    // Add padding to match C# struct layout if necessary
} VBEInfo;

// This structure must be compatible with the MultibootInfo struct in the C# kernel
typedef struct {
    UINT32 Flags;
    UINT32 MemLower;
    UINT32 MemUpper;
    UINT32 BootDevice;
    UINT32 CmdLine;
    UINT32 ModsCount;
    UINT64 ModsAddr;
    UINT32 Syms1;
    UINT32 Syms2;
    UINT32 Syms3;
    UINT32 MmapLength;
    UINT32 MmapAddr;
    UINT32 DrivesLength;
    UINT32 DrivesAddr;
    UINT32 ConfigTable;
    UINT32 BootLoaderName;
    VBEInfo VBEInfo; // Directly embed, not a pointer
    UINT16 VbeMode;
    UINT16 VbeInterfaceSeg;
    UINT16 VbeInterfaceOff;
    UINT16 VbeInterfaceLen;
} BootInfo;

// This structure must be compatible with the module structure expected by the kernel
typedef struct {
    UINT32 ModStart;
    UINT32 ModEnd;
    UINT32 String;
    UINT32 Reserved;
} Module;

