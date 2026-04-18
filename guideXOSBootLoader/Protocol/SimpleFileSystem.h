#pragma once

// Minimal stub for Protocol/SimpleFileSystem.h

#include "../Uefi.h"

// EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64      Revision;
    EFI_STATUS  (EFIAPI *OpenVolume)(
                    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
                    EFI_FILE_PROTOCOL               **Root
                );
};

// GUID for simple file system protocol
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
  { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

extern EFI_GUID gEfiSimpleFileSystemProtocolGuid;
