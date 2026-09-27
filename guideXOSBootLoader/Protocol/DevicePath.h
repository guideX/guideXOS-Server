#pragma once

#include "../Uefi.h"

// EFI_DEVICE_PATH_PROTOCOL uses byte-packed nodes. Do not dereference the
// UINT16 length field directly: node addresses are not guaranteed aligned.
struct _EFI_DEVICE_PATH_PROTOCOL {
    UINT8 Type;
    UINT8 SubType;
    UINT8 Length[2];
};

typedef struct _EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH_PROTOCOL;

#define EFI_DEVICE_PATH_PROTOCOL_GUID \
  { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

extern EFI_GUID gEfiDevicePathProtocolGuid;
