#include <Uefi.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/Acpi.h>

// Define GUID objects to satisfy linker (no EDK II library linking)
// Values must match EDK II definitions
extern "C" {
    EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID gEfiLoadedImageProtocolGuid      = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID gEfiAcpi10TableGuid              = ACPI_TABLE_GUID;
    EFI_GUID gEfiAcpi20TableGuid              = EFI_ACPI_TABLE_GUID;
    EFI_GUID gEfiGraphicsOutputProtocolGuid   = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
}

// Global SystemTable pointer for uefi_shim.h functions
EFI_SYSTEM_TABLE* gST = nullptr;
