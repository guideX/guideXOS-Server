#pragma once

// Minimal stub for Protocol/LoadedImage.h

#include "../Uefi.h"

struct _EFI_LOADED_IMAGE_PROTOCOL {
    UINT32                    Revision;
    EFI_HANDLE                ParentHandle;
    EFI_SYSTEM_TABLE          *SystemTable;
    
    // Source location of the image
    EFI_HANDLE                DeviceHandle;
    VOID                      *FilePath;
    VOID                      *Reserved;
    
    // Image's load options
    UINT32                    LoadOptionsSize;
    VOID                      *LoadOptions;
    
    // Location where image was loaded
    VOID                      *ImageBase;
    UINT64                    ImageSize;
    EFI_MEMORY_TYPE           ImageCodeType;
    EFI_MEMORY_TYPE           ImageDataType;
    VOID                      *Unload;
};

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
  { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

extern EFI_GUID gEfiLoadedImageProtocolGuid;
