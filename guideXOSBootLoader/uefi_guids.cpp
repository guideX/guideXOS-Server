#include <Uefi.h>
#include "Protocol/SimpleFileSystem.h"
#include "Protocol/LoadedImage.h"
#include "Protocol/DevicePath.h"
#include "Protocol/PciRootBridgeIo.h"
#include "Protocol/GraphicsOutput.h"
#include "Guid/Acpi.h"
#include "Guid/FileInfo.h"

// Define GUID objects to satisfy linker (no EDK II library linking)
// Values must match EDK II definitions
// The local protocol headers declare these globals with C++ linkage; keep the
// definitions consistent so both GCC-based checks and MSVC builds agree.
EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gEfiLoadedImageProtocolGuid      = EFI_LOADED_IMAGE_PROTOCOL_GUID;
EFI_GUID gEfiDevicePathProtocolGuid       = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gEfiPciRootBridgeIoProtocolGuid  = EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GUID;
EFI_GUID gEfiAcpi10TableGuid              = EFI_ACPI_10_TABLE_GUID;
EFI_GUID gEfiAcpi20TableGuid              = EFI_ACPI_20_TABLE_GUID;
EFI_GUID gEfiGraphicsOutputProtocolGuid   = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
EFI_GUID gEfiFileInfoGuid                 = EFI_FILE_INFO_ID;

// Global SystemTable pointer for uefi_shim.h functions
EFI_SYSTEM_TABLE* gST = nullptr;
