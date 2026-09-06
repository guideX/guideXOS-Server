// Minimal AArch64 UEFI loader for AARCH64-1.
//
// This target intentionally does not include the AMD64 loader's page-table,
// PCI, GOP, COM1, or x64 trampoline code.  It loads one fixed-address ELF64
// kernel, performs the UEFI memory-map/EBS choreography, and enters the
// AArch64 phase-1 contract.

#include "../Uefi.h"
#include "../Protocol/LoadedImage.h"
#include "../Protocol/SimpleFileSystem.h"
#ifdef GXOS_AARCH64_PHASE2
#include "../../aarch64/phase2/phase2_contract.h"
using Aarch64Handoff = gxos_aarch64_phase2_handoff;
#define GXOS_AARCH64_HANDOFF_MAGIC GXOS_AARCH64_PHASE2_HANDOFF_MAGIC
#define GXOS_AARCH64_HANDOFF_VERSION GXOS_AARCH64_PHASE2_HANDOFF_VERSION
#define GXOS_AARCH64_KERNEL_LOAD_ADDRESS GXOS_AARCH64_PHASE2_KERNEL_LOAD_ADDRESS
#define GXOS_AARCH64_UART_BASE GXOS_AARCH64_PHASE2_UART_FALLBACK
#define GXOS_AARCH64_FLAG_EBS_COMPLETE GXOS_AARCH64_PHASE2_FLAG_EBS_COMPLETE
#define GXOS_AARCH64_FLAG_IDENTITY_LOAD GXOS_AARCH64_PHASE2_FLAG_IDENTITY_LOAD
#define GXOS_AARCH64_FLAG_MMU_OFF_ON_ENTRY GXOS_AARCH64_PHASE2_FLAG_MMU_OFF_ON_ENTRY
#define GXOS_AARCH64_FLAG_STACK_ALLOCATED GXOS_AARCH64_PHASE2_FLAG_STACK_ALLOCATED
#define GXOS_AARCH64_FLAG_MEMORY_MAP_VALID GXOS_AARCH64_PHASE2_FLAG_MEMORY_MAP_VALID
#define GXOS_AARCH64_FLAG_DTB_VALID GXOS_AARCH64_PHASE2_FLAG_DTB_VALID
#define GXOS_AARCH64_FLAG_DTB_COPIED GXOS_AARCH64_PHASE2_FLAG_DTB_COPIED
#else
#include "../../aarch64/phase1/phase1_contract.h"
using Aarch64Handoff = gxos_aarch64_phase1_handoff;
#define GXOS_AARCH64_HANDOFF_MAGIC GXOS_AARCH64_PHASE1_HANDOFF_MAGIC
#define GXOS_AARCH64_HANDOFF_VERSION GXOS_AARCH64_PHASE1_HANDOFF_VERSION
#define GXOS_AARCH64_KERNEL_LOAD_ADDRESS GXOS_AARCH64_PHASE1_KERNEL_LOAD_ADDRESS
#define GXOS_AARCH64_UART_BASE GXOS_AARCH64_PHASE1_UART_BASE
#define GXOS_AARCH64_FLAG_EBS_COMPLETE GXOS_AARCH64_PHASE1_FLAG_EBS_COMPLETE
#define GXOS_AARCH64_FLAG_IDENTITY_LOAD GXOS_AARCH64_PHASE1_FLAG_IDENTITY_LOAD
#define GXOS_AARCH64_FLAG_MMU_OFF_ON_ENTRY GXOS_AARCH64_PHASE1_FLAG_MMU_OFF_ON_ENTRY
#define GXOS_AARCH64_FLAG_STACK_ALLOCATED GXOS_AARCH64_PHASE1_FLAG_STACK_ALLOCATED
#define GXOS_AARCH64_FLAG_MEMORY_MAP_VALID GXOS_AARCH64_PHASE1_FLAG_MEMORY_MAP_VALID
#endif

#include <stdint.h>

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

#pragma pack(push, 1)
typedef struct {
    uint8_t e_ident[16];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff;
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

typedef struct {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    uint8_t create_time[16];
    uint8_t access_time[16];
    uint8_t modification_time[16];
    uint64_t attribute;
    CHAR16 file_name[1];
} EFI_FILE_INFO;
#pragma pack(pop)

static const uint16_t kElfClass64 = 2;
static const uint16_t kElfDataLittleEndian = 1;
static const uint16_t kElfTypeExec = 2;
static const uint16_t kElfMachineAarch64 = 183;
static const uint32_t kPtLoad = 1;
static const uint32_t kPfExecute = 1;
static const uint64_t kPageMask = UINT64_C(0xfff);
static const UINTN kKernelFileLimit = UINTN(64) * 1024 * 1024;
static const UINTN kMemoryMapCapacity = UINTN(256) * 1024;
static const UINTN kMemoryMapCapacityLimit = UINTN(2) * 1024 * 1024;
static const UINTN kStackPages = 16;

static const EFI_GUID kLoadedImageProtocolGuid =
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID kSimpleFileSystemProtocolGuid =
    { 0x964e5b22, 0x6459, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID kFileInfoGuid =
    { 0x09576e92, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };

#ifdef GXOS_AARCH64_PHASE2
// EFI_DTB_TABLE_GUID from the UEFI Device Tree Configuration Table protocol.
static const EFI_GUID kDtbTableGuid =
    { 0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 } };
#endif

static EFI_SYSTEM_TABLE* gSystemTable = nullptr;

static bool add_u64(uint64_t a, uint64_t b, uint64_t* result)
{
    if (b > UINT64_MAX - a) return false;
    *result = a + b;
    return true;
}

static bool mul_u64(uint64_t a, uint64_t b, uint64_t* result)
{
    if (a != 0 && b > UINT64_MAX / a) return false;
    *result = a * b;
    return true;
}

static bool range_in_file(uint64_t offset, uint64_t size, UINTN fileSize)
{
    uint64_t end = 0;
    return add_u64(offset, size, &end) && end <= (uint64_t)fileSize;
}

static uint32_t read_be32(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static bool guid_equal(const EFI_GUID& left, const EFI_GUID& right)
{
    if (left.Data1 != right.Data1 || left.Data2 != right.Data2 || left.Data3 != right.Data3) return false;
    for (UINTN i = 0; i < 8; ++i) if (left.Data4[i] != right.Data4[i]) return false;
    return true;
}

static void print_ascii(const char* text)
{
    if (!gSystemTable || !gSystemTable->ConOut || !text) return;

    CHAR16 buffer[128];
    UINTN count = 0;
    while (*text != 0) {
        if (count == (sizeof(buffer) / sizeof(buffer[0])) - 1) {
            buffer[count] = 0;
            gSystemTable->ConOut->OutputString(gSystemTable->ConOut, buffer);
            count = 0;
        }
        buffer[count++] = (CHAR16)(uint8_t)*text++;
    }
    buffer[count] = 0;
    gSystemTable->ConOut->OutputString(gSystemTable->ConOut, buffer);
}

static EFI_STATUS fail(const char* message)
{
    print_ascii("[A64 UEFI] ERROR: ");
    print_ascii(message);
    print_ascii("\r\n");
    return EFI_LOAD_ERROR;
}

static void set_bytes(void* destination, UINTN size, uint8_t value)
{
    uint8_t* bytes = (uint8_t*)destination;
    for (UINTN i = 0; i < size; ++i) bytes[i] = value;
}

static void copy_bytes(void* destination, const void* source, UINTN size)
{
    uint8_t* d = (uint8_t*)destination;
    const uint8_t* s = (const uint8_t*)source;
    for (UINTN i = 0; i < size; ++i) d[i] = s[i];
}

static uint64_t read_current_el()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
    return (value >> 2) & 3;
}

static uint64_t read_sctlr_el1()
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(value));
    return value;
}

static void sync_loaded_code(const uint8_t* start, UINTN size)
{
    uint64_t ctr = 0;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    UINTN lineSize = (UINTN)4 << (ctr & 0xf);
    if (lineSize < 16) lineSize = 16;

    uintptr_t begin = (uintptr_t)start & ~(uintptr_t)(lineSize - 1);
    uintptr_t end = ((uintptr_t)start + size + lineSize - 1) & ~(uintptr_t)(lineSize - 1);
    for (uintptr_t address = begin; address < end; address += lineSize) {
        __asm__ volatile("dc cvau, %0" : : "r"(address) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
    for (uintptr_t address = begin; address < end; address += lineSize) {
        __asm__ volatile("ic ivau, %0" : : "r"(address) : "memory");
    }
    __asm__ volatile("dsb sy\n isb" ::: "memory");
}

static EFI_STATUS allocate_pool(UINTN size, VOID** result)
{
    if (!gSystemTable || !gSystemTable->BootServices || !result) return EFI_INVALID_PARAMETER;
    *result = nullptr;
    return gSystemTable->BootServices->AllocatePool(EfiLoaderData, size, result);
}

static void free_pool(VOID* buffer)
{
    if (buffer && gSystemTable && gSystemTable->BootServices && gSystemTable->BootServices->FreePool) {
        gSystemTable->BootServices->FreePool(buffer);
    }
}

static EFI_STATUS open_kernel(EFI_HANDLE imageHandle, EFI_FILE_PROTOCOL** root, EFI_FILE_PROTOCOL** kernel)
{
    if (!gSystemTable || !gSystemTable->BootServices || !root || !kernel) return EFI_INVALID_PARAMETER;
    *root = nullptr;
    *kernel = nullptr;

    EFI_LOADED_IMAGE_PROTOCOL* loadedImage = nullptr;
    EFI_STATUS status = gSystemTable->BootServices->HandleProtocol(
        imageHandle, (EFI_GUID*)&kLoadedImageProtocolGuid, (VOID**)&loadedImage);
    if (EFI_ERROR(status) || !loadedImage) return fail("loaded-image protocol unavailable");

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fileSystem = nullptr;
    status = gSystemTable->BootServices->HandleProtocol(
        loadedImage->DeviceHandle, (EFI_GUID*)&kSimpleFileSystemProtocolGuid, (VOID**)&fileSystem);
    if (EFI_ERROR(status) || !fileSystem || !fileSystem->OpenVolume) {
        return fail("simple-file-system protocol unavailable");
    }

    status = fileSystem->OpenVolume(fileSystem, root);
    if (EFI_ERROR(status) || !*root) return fail("could not open ESP root");

    static const CHAR16 kernelName[] = { 'k','e','r','n','e','l','.', 'e','l','f', 0 };
    status = (*root)->Open(*root, kernel, (CHAR16*)kernelName, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || !*kernel) return fail("kernel.elf not found");
    return EFI_SUCCESS;
}

static EFI_STATUS read_file(EFI_FILE_PROTOCOL* file, uint8_t** bytes, UINTN* size)
{
    if (!file || !file->GetInfo || !file->Read || !file->SetPosition || !bytes || !size) {
        return EFI_INVALID_PARAMETER;
    }
    *bytes = nullptr;
    *size = 0;

    UINTN infoSize = 0;
    EFI_STATUS status = file->GetInfo(file, (EFI_GUID*)&kFileInfoGuid, &infoSize, nullptr);
    if (status != EFI_BUFFER_TOO_SMALL || infoSize < sizeof(EFI_FILE_INFO)) {
        return fail("kernel file info unavailable");
    }

    EFI_FILE_INFO* info = nullptr;
    status = allocate_pool(infoSize, (VOID**)&info);
    if (EFI_ERROR(status) || !info) return fail("kernel file info allocation failed");
    status = file->GetInfo(file, (EFI_GUID*)&kFileInfoGuid, &infoSize, info);
    if (EFI_ERROR(status)) {
        free_pool(info);
        return fail("kernel file info read failed");
    }

    if (info->file_size == 0 || info->file_size > kKernelFileLimit || info->file_size > UINT64_MAX) {
        free_pool(info);
        return fail("kernel file size is invalid");
    }
    UINTN fileSize = (UINTN)info->file_size;
    free_pool(info);

    uint8_t* buffer = nullptr;
    status = allocate_pool(fileSize, (VOID**)&buffer);
    if (EFI_ERROR(status) || !buffer) return fail("kernel file allocation failed");

    status = file->SetPosition(file, 0);
    if (EFI_ERROR(status)) {
        free_pool(buffer);
        return fail("kernel file seek failed");
    }

    UINTN remaining = fileSize;
    UINTN offset = 0;
    while (remaining != 0) {
        UINTN chunk = remaining;
        if (chunk > UINTN(1024) * 1024) chunk = UINTN(1024) * 1024;
        UINTN readSize = chunk;
        status = file->Read(file, &readSize, buffer + offset);
        if (EFI_ERROR(status) || readSize == 0 || readSize > remaining) {
            free_pool(buffer);
            return fail("kernel file read failed");
        }
        offset += readSize;
        remaining -= readSize;
    }
    *bytes = buffer;
    *size = fileSize;
    return EFI_SUCCESS;
}

static EFI_STATUS load_kernel(const uint8_t* file, UINTN fileSize,
                              uint64_t* kernelBase, uint64_t* kernelSize,
                              uint64_t* kernelEntry)
{
    if (!file || fileSize < sizeof(Elf64_Ehdr) || !kernelBase || !kernelSize || !kernelEntry) {
        return fail("kernel ELF is truncated");
    }
    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)file;
    if (ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        return fail("kernel is not ELF");
    }
    if (ehdr->e_ident[4] != kElfClass64 || ehdr->e_ident[5] != kElfDataLittleEndian ||
        ehdr->e_ident[6] != 1 || ehdr->e_machine != kElfMachineAarch64 ||
        ehdr->e_type != kElfTypeExec || ehdr->e_version != 1 ||
        ehdr->e_ehsize != sizeof(Elf64_Ehdr) || ehdr->e_phentsize < sizeof(Elf64_Phdr) ||
        ehdr->e_phnum == 0) {
        return fail("incompatible ELF: expected ELF64 little-endian EM_AARCH64 ET_EXEC");
    }

    uint64_t phSize = 0;
    if (!mul_u64(ehdr->e_phnum, ehdr->e_phentsize, &phSize) ||
        !range_in_file(ehdr->e_phoff, phSize, fileSize)) {
        return fail("ELF program-header bounds are invalid");
    }

    uint64_t minVaddr = UINT64_MAX;
    uint64_t maxVaddr = 0;
    bool hasLoad = false;
    bool entryExecutable = false;
    for (uint16_t index = 0; index < ehdr->e_phnum; ++index) {
        uint64_t offset = 0;
        if (!mul_u64(index, ehdr->e_phentsize, &offset) || !add_u64(ehdr->e_phoff, offset, &offset)) {
            return fail("ELF program-header offset overflow");
        }
        const Elf64_Phdr* ph = (const Elf64_Phdr*)(file + offset);
        if (ph->p_type != kPtLoad || ph->p_memsz == 0) continue;
        hasLoad = true;
        uint64_t segmentEnd = 0;
        if (ph->p_filesz > ph->p_memsz || !range_in_file(ph->p_offset, ph->p_filesz, fileSize) ||
            !add_u64(ph->p_vaddr, ph->p_memsz, &segmentEnd)) {
            return fail("ELF loadable segment bounds are invalid");
        }
        if (ph->p_align > 1 && ((ph->p_align & (ph->p_align - 1)) != 0 ||
                                (ph->p_vaddr % ph->p_align) != (ph->p_offset % ph->p_align))) {
            return fail("ELF segment alignment is invalid");
        }
        if (ph->p_vaddr < minVaddr) minVaddr = ph->p_vaddr;
        if (segmentEnd > maxVaddr) maxVaddr = segmentEnd;
        if ((ph->p_flags & kPfExecute) != 0 && ehdr->e_entry >= ph->p_vaddr && ehdr->e_entry < segmentEnd) {
            entryExecutable = true;
        }
    }
    if (!hasLoad || !entryExecutable || minVaddr == UINT64_MAX || maxVaddr <= minVaddr) {
        return fail("ELF has no valid executable PT_LOAD entry");
    }

    uint64_t imageStart = minVaddr & ~kPageMask;
    uint64_t imageEndUnrounded = 0;
    if (!add_u64(maxVaddr, kPageMask, &imageEndUnrounded)) return fail("ELF image size overflow");
    uint64_t imageEnd = imageEndUnrounded & ~kPageMask;
    if (imageEnd <= imageStart || imageStart != GXOS_AARCH64_KERNEL_LOAD_ADDRESS) {
        return fail("ELF load address is not the Phase 1 fixed address");
    }
    uint64_t imageSize64 = imageEnd - imageStart;
    if (imageSize64 > UINT64_MAX || imageSize64 == 0) return fail("ELF image size is invalid");
    UINTN imageSize = (UINTN)imageSize64;
    UINTN pages = (imageSize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;

    EFI_PHYSICAL_ADDRESS physical = GXOS_AARCH64_KERNEL_LOAD_ADDRESS;
    EFI_STATUS status = gSystemTable->BootServices->AllocatePages(
        AllocateAddress, EfiLoaderCode, pages, &physical);
    if (EFI_ERROR(status) || physical != GXOS_AARCH64_KERNEL_LOAD_ADDRESS) {
        return fail("fixed kernel allocation at 0x40000000 failed");
    }
    uint8_t* destination = (uint8_t*)(UINTN)physical;
    set_bytes(destination, imageSize, 0);

    for (uint16_t index = 0; index < ehdr->e_phnum; ++index) {
        uint64_t offset = 0;
        if (!mul_u64(index, ehdr->e_phentsize, &offset) || !add_u64(ehdr->e_phoff, offset, &offset)) {
            return fail("ELF program-header offset overflow during load");
        }
        const Elf64_Phdr* ph = (const Elf64_Phdr*)(file + offset);
        if (ph->p_type != kPtLoad || ph->p_memsz == 0) continue;
        uint64_t relative = ph->p_vaddr - imageStart;
        uint64_t destinationEnd = 0;
        if (!add_u64(relative, ph->p_memsz, &destinationEnd) || destinationEnd > imageSize64) {
            return fail("ELF segment does not fit allocated image");
        }
        if (ph->p_filesz != 0) {
            copy_bytes(destination + relative, file + ph->p_offset, (UINTN)ph->p_filesz);
        }
    }

    sync_loaded_code(destination, imageSize);
    *kernelBase = physical;
    *kernelSize = imageSize64;
    *kernelEntry = (UINT64)(UINTN)physical + (ehdr->e_entry - imageStart);
    print_ascii("[A64 UEFI] ELF machine: AArch64\r\n");
    print_ascii("[A64 UEFI] segments loaded\r\n");
    return EFI_SUCCESS;
}

static EFI_STATUS allocate_stack(uint64_t* stackBase, uint64_t* stackSize, uint64_t* stackTop)
{
    if (!stackBase || !stackSize || !stackTop) return EFI_INVALID_PARAMETER;
    EFI_PHYSICAL_ADDRESS base = 0;
    EFI_STATUS status = gSystemTable->BootServices->AllocatePages(
        AllocateAnyPages, EfiLoaderData, kStackPages, &base);
    if (EFI_ERROR(status)) return fail("kernel stack allocation failed");
    *stackBase = base;
    *stackSize = (uint64_t)kStackPages * EFI_PAGE_SIZE;
    *stackTop = (base + *stackSize) & ~UINT64_C(0xf);
    set_bytes((void*)(UINTN)base, (UINTN)*stackSize, 0);
    return EFI_SUCCESS;
}

static EFI_STATUS acquire_memory_map(EFI_MEMORY_DESCRIPTOR** buffer, UINTN* capacity,
                                     UINTN* mapSize, UINTN* mapKey, UINTN* descriptorSize,
                                     UINT32* descriptorVersion)
{
    if (!buffer || !capacity || !mapSize || !mapKey || !descriptorSize || !descriptorVersion) {
        return EFI_INVALID_PARAMETER;
    }
    if (*buffer == nullptr) {
        *capacity = kMemoryMapCapacity;
        EFI_STATUS allocateStatus = allocate_pool(*capacity, (VOID**)buffer);
        if (EFI_ERROR(allocateStatus) || !*buffer) return fail("memory-map buffer allocation failed");
    }

    for (;;) {
        *mapSize = *capacity;
        EFI_STATUS status = gSystemTable->BootServices->GetMemoryMap(
            mapSize, *buffer, mapKey, descriptorSize, descriptorVersion);
        if (status != EFI_BUFFER_TOO_SMALL) return status;
        if (*capacity >= kMemoryMapCapacityLimit) return fail("memory-map buffer limit exceeded");
        UINTN newCapacity = *capacity * 2;
        EFI_MEMORY_DESCRIPTOR* newBuffer = nullptr;
        EFI_STATUS allocateStatus = allocate_pool(newCapacity, (VOID**)&newBuffer);
        if (EFI_ERROR(allocateStatus) || !newBuffer) return fail("memory-map growth allocation failed");
        // Do not free the old buffer here: FreePool changes the map again.  The
        // old allocation is small and remains valid until the firmware handoff.
        *buffer = newBuffer;
        *capacity = newCapacity;
    }
}

static EFI_STATUS exit_boot_services(EFI_HANDLE imageHandle, Aarch64Handoff* handoff,
                                     EFI_MEMORY_DESCRIPTOR** mapBuffer)
{
    UINTN capacity = 0;
    UINTN mapSize = 0;
    UINTN mapKey = 0;
    UINTN descriptorSize = 0;
    UINT32 descriptorVersion = 0;
    EFI_MEMORY_DESCRIPTOR* memoryMap = nullptr;

    for (UINTN attempt = 0; attempt < 6; ++attempt) {
        EFI_STATUS status = acquire_memory_map(&memoryMap, &capacity, &mapSize, &mapKey,
                                               &descriptorSize, &descriptorVersion);
        if (EFI_ERROR(status)) return status;
        if (descriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) || mapSize == 0 ||
            (mapSize % descriptorSize) != 0) {
            return fail("firmware memory map descriptor is invalid");
        }

        handoff->memory_map = (uint64_t)(UINTN)memoryMap;
        handoff->memory_map_size = mapSize;
        handoff->memory_map_descriptor_size = descriptorSize;
        handoff->memory_map_entry_count = mapSize / descriptorSize;
        handoff->flags |= GXOS_AARCH64_FLAG_MEMORY_MAP_VALID;
        *mapBuffer = memoryMap;

        print_ascii("[A64 UEFI] memory map acquired\r\n");
        print_ascii("[A64 UEFI] ExitBootServices requested\r\n");
        status = gSystemTable->BootServices->ExitBootServices(imageHandle, mapKey);
        if (!EFI_ERROR(status)) {
            handoff->flags |= GXOS_AARCH64_FLAG_EBS_COMPLETE;
            return EFI_SUCCESS;
        }
        if (status != EFI_INVALID_PARAMETER) return fail("ExitBootServices failed");
        // A stale map key is the expected retry case.  No allocation or other
        // boot-service call occurs between this failure and the next map read.
    }
    return fail("ExitBootServices retry limit exceeded");
}

#ifdef GXOS_AARCH64_PHASE2
static bool validate_dtb_blob(const uint8_t* blob, uint64_t available, uint32_t* totalSize)
{
    if (!blob || available < 40 || available > UINT64_C(16) * 1024 * 1024 ||
        read_be32(blob) != UINT32_C(0xd00dfeed)) return false;
    const uint32_t total = read_be32(blob + 4);
    const uint32_t structOffset = read_be32(blob + 8);
    const uint32_t stringsOffset = read_be32(blob + 12);
    const uint32_t reserveOffset = read_be32(blob + 16);
    const uint32_t version = read_be32(blob + 20);
    const uint32_t lastCompatible = read_be32(blob + 24);
    const uint32_t stringsSize = read_be32(blob + 32);
    const uint32_t structSize = read_be32(blob + 36);
    if (total < 40 || total > available || total > UINT32_MAX || version < 16 || version > 19 ||
        lastCompatible < 16 || lastCompatible > version || (reserveOffset & 7) != 0 || reserveOffset > total ||
        !range_in_file(reserveOffset, 16, (UINTN)total) || (structOffset & 3) != 0 ||
        (stringsOffset & 3) != 0 || !range_in_file(structOffset, structSize, (UINTN)total) ||
        !range_in_file(stringsOffset, stringsSize, (UINTN)total)) return false;
    if (totalSize) *totalSize = total;
    return true;
}

static EFI_STATUS copy_dtb_from_configuration_table(Aarch64Handoff* handoff)
{
    if (!gSystemTable || !gSystemTable->ConfigurationTable || gSystemTable->NumberOfTableEntries == 0 ||
        gSystemTable->NumberOfTableEntries > 1024) return fail("DTB configuration table unavailable");
    for (UINTN i = 0; i < gSystemTable->NumberOfTableEntries; ++i) {
        const EFI_CONFIGURATION_TABLE& entry = gSystemTable->ConfigurationTable[i];
        if (!guid_equal(entry.VendorGuid, kDtbTableGuid) || !entry.VendorTable) continue;
        const uint8_t* source = (const uint8_t*)entry.VendorTable;
        uint32_t total = 0;
        if (!validate_dtb_blob(source, UINT64_C(16) * 1024 * 1024, &total)) {
            return fail("DTB header or block bounds are invalid");
        }
        const UINTN pages = ((UINTN)total + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
        EFI_PHYSICAL_ADDRESS copyPhysical = 0;
        EFI_STATUS status = gSystemTable->BootServices->AllocatePages(
            AllocateAnyPages, EfiLoaderData, pages, &copyPhysical);
        if (EFI_ERROR(status) || copyPhysical == 0) return fail("DTB copy allocation failed");
        set_bytes((void*)(UINTN)copyPhysical, pages * EFI_PAGE_SIZE, 0);
        copy_bytes((void*)(UINTN)copyPhysical, source, total);
        handoff->dtb_base = copyPhysical;
        handoff->dtb_size = total;
        handoff->flags |= GXOS_AARCH64_FLAG_DTB_VALID | GXOS_AARCH64_FLAG_DTB_COPIED;
        print_ascii("[A64 UEFI] DTB: copied from EFI configuration table\r\n");
        return EFI_SUCCESS;
    }
    return fail("EFI DTB configuration table not found");
}
#endif

extern "C" EFI_STATUS efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE* systemTable)
{
    gSystemTable = systemTable;
    print_ascii("[A64 UEFI] entry\r\n");
    if (!gSystemTable || !gSystemTable->BootServices) return EFI_INVALID_PARAMETER;

    uint64_t initialEl = read_current_el();
    uint64_t loaderSctlr = read_sctlr_el1();
    if (initialEl != 1 && initialEl != 2) return fail("unsupported initial exception level");
    print_ascii(initialEl == 2 ? "[A64 UEFI] CurrentEL: EL2\r\n" : "[A64 UEFI] CurrentEL: EL1\r\n");
    print_ascii((loaderSctlr & 1) != 0 ? "[A64 UEFI] loader MMU: ON\r\n" : "[A64 UEFI] loader MMU: OFF\r\n");

    EFI_FILE_PROTOCOL* root = nullptr;
    EFI_FILE_PROTOCOL* kernelFile = nullptr;
    EFI_STATUS status = open_kernel(imageHandle, &root, &kernelFile);
    if (EFI_ERROR(status)) return status;
    print_ascii("[A64 UEFI] kernel opened\r\n");

    uint8_t* kernelFileBytes = nullptr;
    UINTN kernelFileSize = 0;
    status = read_file(kernelFile, &kernelFileBytes, &kernelFileSize);
    if (EFI_ERROR(status)) return status;

    uint64_t kernelBase = 0;
    uint64_t kernelSize = 0;
    uint64_t kernelEntry = 0;
    status = load_kernel(kernelFileBytes, kernelFileSize, &kernelBase, &kernelSize, &kernelEntry);
    free_pool(kernelFileBytes);
    if (EFI_ERROR(status)) return status;

    typedef EFI_STATUS (*CloseFileFn)(EFI_FILE_PROTOCOL*);
    if (kernelFile->Close) ((CloseFileFn)kernelFile->Close)(kernelFile);
    if (root && root->Close) ((CloseFileFn)root->Close)(root);

    uint64_t stackBase = 0;
    uint64_t stackSize = 0;
    uint64_t stackTop = 0;
    status = allocate_stack(&stackBase, &stackSize, &stackTop);
    if (EFI_ERROR(status)) return status;

    Aarch64Handoff* handoff = nullptr;
    EFI_PHYSICAL_ADDRESS handoffPhysical = 0;
    status = gSystemTable->BootServices->AllocatePages(
        AllocateAnyPages, EfiLoaderData, 1, &handoffPhysical);
    handoff = (Aarch64Handoff*)(UINTN)handoffPhysical;
    if (EFI_ERROR(status) || !handoff) return fail("handoff allocation failed");
    set_bytes(handoff, EFI_PAGE_SIZE, 0);
    handoff->magic = GXOS_AARCH64_HANDOFF_MAGIC;
    handoff->version = GXOS_AARCH64_HANDOFF_VERSION;
    handoff->size = sizeof(*handoff);
    handoff->flags = GXOS_AARCH64_FLAG_IDENTITY_LOAD |
                     GXOS_AARCH64_FLAG_MMU_OFF_ON_ENTRY |
                     GXOS_AARCH64_FLAG_STACK_ALLOCATED;
    handoff->kernel_base = kernelBase;
    handoff->kernel_size = kernelSize;
    handoff->kernel_entry = kernelEntry;
    handoff->stack_base = stackBase;
    handoff->stack_size = stackSize;
    handoff->stack_top = stackTop;
    handoff->initial_current_el = initialEl;
    handoff->loader_sctlr_el1 = loaderSctlr;
    handoff->uart_base = GXOS_AARCH64_UART_BASE;

#ifdef GXOS_AARCH64_PHASE2
    status = copy_dtb_from_configuration_table(handoff);
    if (EFI_ERROR(status)) return status;
#endif

    EFI_MEMORY_DESCRIPTOR* mapBuffer = nullptr;
    status = exit_boot_services(imageHandle, handoff, &mapBuffer);
    if (EFI_ERROR(status)) return status;

    typedef void (*KernelEntryFn)(Aarch64Handoff*, uint64_t, uint64_t);
    KernelEntryFn entry = (KernelEntryFn)(UINTN)kernelEntry;
    entry(handoff, stackTop, 0);
    return EFI_ABORTED;
}
