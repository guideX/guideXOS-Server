#pragma once

#include "../Uefi.h"

#define EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GUID \
  { 0x2f707ebb, 0x4a1a, 0x11d4, { 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

// x64 UEFI ABI layout. The first 18 pointer-sized slots cover ParentHandle,
// the protocol methods, and the three two-function access tables; the final
// field is the authoritative PCI segment number.
struct _EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL {
    VOID* Fields[18];
    UINT32 SegmentNumber;
};

typedef struct _EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL;

extern EFI_GUID gEfiPciRootBridgeIoProtocolGuid;
