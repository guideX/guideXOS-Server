#pragma once

// Minimal UEFI type definitions for freestanding bootloader
// This is a lightweight stub to avoid full EDK2 dependency

#include <stdint.h>
#include <stdarg.h>

// VA_LIST macros for UEFI
#define VA_LIST va_list
#define VA_START va_start
#define VA_END va_end
#define VA_ARG va_arg
#define VA_COPY va_copy

// Basic UEFI types
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int8_t    INT8;
typedef int16_t   INT16;
typedef int32_t   INT32;
typedef int64_t   INT64;
typedef uint8_t   BOOLEAN;
typedef uint16_t  CHAR16;
typedef char      CHAR8;
typedef void      VOID;

#ifdef _WIN64
typedef uint64_t  UINTN;
typedef int64_t   INTN;
#else
typedef uint32_t  UINTN;
typedef int32_t   INTN;
#endif

typedef UINT64    EFI_PHYSICAL_ADDRESS;
typedef UINT64    EFI_VIRTUAL_ADDRESS;
typedef UINTN     EFI_STATUS;

// Boolean values
#define TRUE  ((BOOLEAN)1)
#define FALSE ((BOOLEAN)0)

// NULL
#ifndef NULL
#define NULL ((VOID*)0)
#endif

// IN, OUT, OPTIONAL, CONST
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define EFIAPI

// EFI_STATUS values
#define EFI_SUCCESS               0
#define EFI_LOAD_ERROR            (1 | (1ULL << 63))
#define EFI_INVALID_PARAMETER     (2 | (1ULL << 63))
#define EFI_UNSUPPORTED           (3 | (1ULL << 63))
#define EFI_BAD_BUFFER_SIZE       (4 | (1ULL << 63))
#define EFI_BUFFER_TOO_SMALL      (5 | (1ULL << 63))
#define EFI_NOT_READY             (6 | (1ULL << 63))
#define EFI_DEVICE_ERROR          (7 | (1ULL << 63))
#define EFI_WRITE_PROTECTED       (8 | (1ULL << 63))
#define EFI_OUT_OF_RESOURCES      (9 | (1ULL << 63))
#define EFI_VOLUME_CORRUPTED      (10 | (1ULL << 63))
#define EFI_VOLUME_FULL           (11 | (1ULL << 63))
#define EFI_NO_MEDIA              (12 | (1ULL << 63))
#define EFI_MEDIA_CHANGED         (13 | (1ULL << 63))
#define EFI_NOT_FOUND             (14 | (1ULL << 63))
#define EFI_ACCESS_DENIED         (15 | (1ULL << 63))
#define EFI_NO_RESPONSE           (16 | (1ULL << 63))
#define EFI_NO_MAPPING            (17 | (1ULL << 63))
#define EFI_TIMEOUT               (18 | (1ULL << 63))
#define EFI_NOT_STARTED           (19 | (1ULL << 63))
#define EFI_ALREADY_STARTED       (20 | (1ULL << 63))
#define EFI_ABORTED               (21 | (1ULL << 63))

#define EFI_ERROR(Status) ((INTN)(Status) < 0)

// Page size
#define EFI_PAGE_SIZE 4096

// Memory types
typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

// Memory allocation type
typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

// Memory descriptor
typedef struct {
    UINT32                Type;
    EFI_PHYSICAL_ADDRESS  PhysicalStart;
    EFI_VIRTUAL_ADDRESS   VirtualStart;
    UINT64                NumberOfPages;
    UINT64                Attribute;
} EFI_MEMORY_DESCRIPTOR;

// GUID structure
typedef struct {
    UINT32  Data1;
    UINT16  Data2;
    UINT16  Data3;
    UINT8   Data4[8];
} EFI_GUID;

// Forward declarations for protocol/service types
typedef struct _EFI_SYSTEM_TABLE              EFI_SYSTEM_TABLE;
typedef struct _EFI_BOOT_SERVICES             EFI_BOOT_SERVICES;
typedef struct _EFI_RUNTIME_SERVICES          EFI_RUNTIME_SERVICES;
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL  EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct _EFI_CONFIGURATION_TABLE       EFI_CONFIGURATION_TABLE;
typedef struct _EFI_FILE_PROTOCOL             EFI_FILE_PROTOCOL;
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL  EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct _EFI_LOADED_IMAGE_PROTOCOL     EFI_LOADED_IMAGE_PROTOCOL;

typedef VOID*  EFI_HANDLE;
typedef VOID*  EFI_EVENT;

// Simple Text Output Protocol
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID*       Reset;
    EFI_STATUS  (EFIAPI *OutputString)(
                    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
                    CHAR16 *String
                );
    VOID*       TestString;
    VOID*       QueryMode;
    VOID*       SetMode;
    VOID*       SetAttribute;
    VOID*       ClearScreen;
    VOID*       SetCursorPosition;
    VOID*       EnableCursor;
    VOID*       Mode;
};

// Boot Services (partial - only what's needed)
struct _EFI_BOOT_SERVICES {
    CHAR8   padding0[24];  // EFI_TABLE_HEADER
    
    // Task Priority Services (4)
    VOID*   RaiseTPL;
    VOID*   RestoreTPL;
    
    // Memory Services (2)
    EFI_STATUS (EFIAPI *AllocatePages)(
                    EFI_ALLOCATE_TYPE           Type,
                    EFI_MEMORY_TYPE             MemoryType,
                    UINTN                       Pages,
                    EFI_PHYSICAL_ADDRESS        *Memory
                );
    EFI_STATUS (EFIAPI *FreePages)(
                    EFI_PHYSICAL_ADDRESS        Memory,
                    UINTN                       Pages
                );
    EFI_STATUS (EFIAPI *GetMemoryMap)(
                    UINTN                       *MemoryMapSize,
                    EFI_MEMORY_DESCRIPTOR       *MemoryMap,
                    UINTN                       *MapKey,
                    UINTN                       *DescriptorSize,
                    UINT32                      *DescriptorVersion
                );
    EFI_STATUS (EFIAPI *AllocatePool)(
                    EFI_MEMORY_TYPE             PoolType,
                    UINTN                       Size,
                    VOID                        **Buffer
                );
    EFI_STATUS (EFIAPI *FreePool)(
                    VOID                        *Buffer
                );
    
    // Event & Timer Services (6)
    VOID*   CreateEvent;
    VOID*   SetTimer;
    VOID*   WaitForEvent;
    VOID*   SignalEvent;
    VOID*   CloseEvent;
    VOID*   CheckEvent;
    
    // Protocol Handler Services (9)
    VOID*   InstallProtocolInterface;
    VOID*   ReinstallProtocolInterface;
    VOID*   UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(
                    EFI_HANDLE                  Handle,
                    EFI_GUID                    *Protocol,
                    VOID                        **Interface
                );
    VOID*   Reserved;
    VOID*   RegisterProtocolNotify;
    VOID*   LocateHandle;
    VOID*   LocateDevicePath;
    VOID*   InstallConfigurationTable;
    
    // Image Services (5)
    VOID*   LoadImage;
    VOID*   StartImage;
    VOID*   Exit;
    VOID*   UnloadImage;
    EFI_STATUS (EFIAPI *ExitBootServices)(
                    EFI_HANDLE                  ImageHandle,
                    UINTN                       MapKey
                );
    
    // Miscellaneous Services (2)
    VOID*   GetNextMonotonicCount;
    VOID*   Stall;
    VOID*   SetWatchdogTimer;
    
    // DriverSupport Services (4)
    VOID*   ConnectController;
    VOID*   DisconnectController;
    
    // Open and Close Protocol Services (2)
    VOID*   OpenProtocol;
    VOID*   CloseProtocol;
    VOID*   OpenProtocolInformation;
    
    // Library Services (3)
    VOID*   ProtocolsPerHandle;
    VOID*   LocateHandleBuffer;
    EFI_STATUS (EFIAPI *LocateProtocol)(
                    EFI_GUID                    *Protocol,
                    VOID                        *Registration,
                    VOID                        **Interface
                );
    VOID*   InstallMultipleProtocolInterfaces;
    VOID*   UninstallMultipleProtocolInterfaces;
    
    // 32-bit CRC Services
    VOID*   CalculateCrc32;
    
    // Miscellaneous Services (3)
    VOID*   CopyMem;
    VOID*   SetMem;
    VOID*   CreateEventEx;
};

// Configuration Table
struct _EFI_CONFIGURATION_TABLE {
    EFI_GUID  VendorGuid;
    VOID      *VendorTable;
};

// System Table (partial)
struct _EFI_SYSTEM_TABLE {
    CHAR8                         padding0[24];  // EFI_TABLE_HEADER
    CHAR16                        *FirmwareVendor;
    UINT32                        FirmwareRevision;
    EFI_HANDLE                    ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *ConIn;
    EFI_HANDLE                    ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE                    StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES          *RuntimeServices;
    EFI_BOOT_SERVICES             *BootServices;
    UINTN                         NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE       *ConfigurationTable;
};

// File Protocol (minimal - only Read, Write, SetPosition needed)
#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
#define EFI_FILE_READ_ONLY   0x0000000000000001ULL
#define EFI_FILE_HIDDEN      0x0000000000000002ULL
#define EFI_FILE_SYSTEM      0x0000000000000004ULL
#define EFI_FILE_RESERVED    0x0000000000000008ULL
#define EFI_FILE_DIRECTORY   0x0000000000000010ULL
#define EFI_FILE_ARCHIVE     0x0000000000000020ULL
#define EFI_FILE_VALID_ATTR  0x0000000000000037ULL

struct _EFI_FILE_PROTOCOL {
    UINT64      Revision;
    EFI_STATUS  (EFIAPI *Open)(
                    EFI_FILE_PROTOCOL   *This,
                    EFI_FILE_PROTOCOL   **NewHandle,
                    CHAR16              *FileName,
                    UINT64              OpenMode,
                    UINT64              Attributes
                );
    VOID*       Close;
    VOID*       Delete;
    EFI_STATUS  (EFIAPI *Read)(
                    EFI_FILE_PROTOCOL   *This,
                    UINTN               *BufferSize,
                    VOID                *Buffer
                );
    VOID*       Write;
    VOID*       GetPosition;
    EFI_STATUS  (EFIAPI *SetPosition)(
                    EFI_FILE_PROTOCOL   *This,
                    UINT64              Position
                );
    EFI_STATUS  (EFIAPI *GetInfo)(
                    EFI_FILE_PROTOCOL   *This,
                    EFI_GUID            *InformationType,
                    UINTN               *BufferSize,
                    VOID                *Buffer
                );
    VOID*       SetInfo;
    VOID*       Flush;
};
