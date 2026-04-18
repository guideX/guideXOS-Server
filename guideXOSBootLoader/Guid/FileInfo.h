#pragma once

// Minimal stub for Guid/FileInfo.h

#include "../Uefi.h"

#define EFI_FILE_INFO_ID \
  { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

typedef struct {
    UINT64    Size;
    UINT64    FileSize;
    UINT64    PhysicalSize;
    VOID      *CreateTime;
    VOID      *LastAccessTime;
    VOID      *ModificationTime;
    UINT64    Attribute;
    CHAR16    FileName[1];
} EFI_FILE_INFO;

extern EFI_GUID gEfiFileInfoGuid;
