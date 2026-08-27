//
// Bare-metal NativeElf loader and diagnostic execution route.
//

#include "native_elf_loader.h"

#include "native_elf_executor.h"
#include "arch/amd64.h"
#include "kernel/serial_debug.h"
#include "kernel/vfs.h"

namespace kernel {
namespace native_elf {
namespace {

static const uint64_t PTE_PRESENT = 1ULL << 0;
static const uint64_t PTE_WRITABLE = 1ULL << 1;
static const uint64_t PTE_LARGE = 1ULL << 7;
static const uint64_t PTE_NX = 1ULL << 63;
static const uint64_t PTE_ADDRESS_MASK = 0x000FFFFFFFFFF000ULL;

static NativeElfExecutionContext s_context = {};
static bool s_contextConfigured = false;
static bool s_nxEnabled = false;
static uint8_t s_file[guidexos::native_elf::MAX_ELF_FILE_BYTES];

static void clear_report(NativeElfRunReport* report)
{
    if (!report) return;
    *report = {};
    report->error = "NativeElf execution did not start";
}

static bool fail_report(NativeElfRunReport* report, const char* error)
{
    if (report) {
        report->success = false;
        report->error = error;
    }
    serial::puts("ELF Loader: ");
    serial::puts(error ? error : "unknown failure");
    serial::putc('\n');
    return false;
}

static void put_decimal_u64(uint64_t value)
{
    char digits[20];
    uint32_t count = 0;
    if (value == 0) {
        serial::putc('0');
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (count != 0) serial::putc(digits[--count]);
}

static void put_decimal_i32(int32_t value)
{
    if (value < 0) {
        serial::putc('-');
        const uint32_t magnitude = static_cast<uint32_t>(-(value + 1)) + 1U;
        put_decimal_u64(magnitude);
    } else {
        put_decimal_u64(static_cast<uint32_t>(value));
    }
}

static uint64_t align_down(uint64_t value)
{
    return value & ~(static_cast<uint64_t>(guidexos::native_elf::PAGE_SIZE) - 1ULL);
}

static bool align_up(uint64_t value, uint64_t* result)
{
    if (!result) return false;
    const uint64_t mask = static_cast<uint64_t>(guidexos::native_elf::PAGE_SIZE) - 1ULL;
    if (value > ~static_cast<uint64_t>(0) - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

static bool find_pte(uint64_t virtualAddress, volatile uint64_t** outPte)
{
    if (!outPte || !s_contextConfigured || s_context.pageTableRoot == 0 ||
        (s_context.pageTableRoot & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0) {
        return false;
    }

    volatile uint64_t* table = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(s_context.pageTableRoot));
    const uint64_t indices[4] = {
        (virtualAddress >> 39) & 0x1FFULL,
        (virtualAddress >> 30) & 0x1FFULL,
        (virtualAddress >> 21) & 0x1FFULL,
        (virtualAddress >> 12) & 0x1FFULL
    };

    for (uint32_t level = 0; level < 3; ++level) {
        const uint64_t entry = table[indices[level]];
        if ((entry & PTE_PRESENT) == 0 || (entry & PTE_LARGE) != 0) return false;
        table = reinterpret_cast<volatile uint64_t*>(
            static_cast<uintptr_t>(entry & PTE_ADDRESS_MASK));
    }

    volatile uint64_t* pte = &table[indices[3]];
    const uint64_t entry = *pte;
    if ((entry & PTE_PRESENT) == 0 || (entry & PTE_LARGE) != 0) return false;
    if ((entry & PTE_ADDRESS_MASK) != (virtualAddress & PTE_ADDRESS_MASK)) return false;
    *outPte = pte;
    return true;
}

static bool set_page_permissions(uint64_t start,
                                  uint64_t end,
                                  bool writable,
                                  bool executable)
{
    if (start >= end || (start & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0 ||
        (end & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0) return false;

    for (uint64_t page = start; page < end; page += guidexos::native_elf::PAGE_SIZE) {
        volatile uint64_t* pte = nullptr;
        if (!find_pte(page, &pte)) return false;
        uint64_t entry = *pte;
        if (writable) entry |= PTE_WRITABLE;
        else entry &= ~PTE_WRITABLE;
        if (s_nxEnabled) {
            if (executable) entry &= ~PTE_NX;
            else entry |= PTE_NX;
        }
        *pte = entry;
        arch::amd64::invlpg(reinterpret_cast<void*>(static_cast<uintptr_t>(page)));
    }
    return true;
}

static void clear_bytes(uint8_t* bytes, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i) bytes[i] = 0;
}

static void copy_bytes(uint8_t* destination, const uint8_t* source, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i) destination[i] = source[i];
}

static bool prepare_page_tables(const NativeElfValidationResult& validation,
                                NativeElfRunReport* report)
{
    uint64_t mappedEnd = 0;
    if (validation.imageBase > ~static_cast<uint64_t>(0) - validation.mappedBytes) {
        return fail_report(report, "mapped image end overflows");
    }
    mappedEnd = validation.imageBase + validation.mappedBytes;

    // The reusable window is writable but non-executable during population.
    // This is the only temporary write permission transition needed here.
    if (!set_page_permissions(validation.imageBase, mappedEnd, true, false)) {
        return fail_report(report, "application pages are not safely mapped by the kernel page tables");
    }

    uint8_t* imageDestination = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(validation.imageBase));
    clear_bytes(imageDestination, validation.mappedBytes);

    for (uint32_t i = 0; i < validation.loadCount; ++i) {
        const NativeElfLoad& load = validation.loads[i];
        uint8_t* destination = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(load.virtualAddress));
        copy_bytes(destination, s_file + load.fileOffset, load.fileSize);

        uint64_t loadEnd = 0;
        if (load.virtualAddress > ~static_cast<uint64_t>(0) - load.memorySize) {
            return fail_report(report, "PT_LOAD end overflows during mapping");
        }
        loadEnd = load.virtualAddress + load.memorySize;
        uint64_t alignedEnd = 0;
        if (!align_up(loadEnd, &alignedEnd)) {
            return fail_report(report, "PT_LOAD page range overflows during permission setup");
        }
        if (!set_page_permissions(align_down(load.virtualAddress), alignedEnd,
                                  (load.flags & PF_W) != 0,
                                  (load.flags & PF_X) != 0)) {
            return fail_report(report, "PT_LOAD permissions could not be installed");
        }
    }
    return true;
}

} // namespace

bool configure_execution_context(const NativeElfExecutionContext& context)
{
    if (context.pageTableRoot == 0 || context.regionBase != guidexos::native_elf::IMAGE_BASE ||
        context.regionSize < guidexos::native_elf::REGION_SIZE ||
        (context.regionBase & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0 ||
        (context.regionSize & (guidexos::native_elf::PAGE_SIZE - 1U)) != 0) {
        s_contextConfigured = false;
        return false;
    }
    s_context = context;
    s_contextConfigured = true;
    s_nxEnabled = arch::amd64::enable_nx();
    return true;
}

bool execution_context_configured()
{
    return s_contextConfigured;
}

bool run_file(const char* path, int32_t* returnValue, NativeElfRunReport* report)
{
    clear_report(report);
    if (returnValue) *returnValue = 0;
    if (!s_contextConfigured) return fail_report(report, "execution context is not configured");
    if (!path || path[0] == '\0') return fail_report(report, "ELF path is empty");
    if (!returnValue) return fail_report(report, "return-value output is null");

    serial::puts("ELF Loader: file=");
    serial::puts(path);
    serial::putc('\n');

    vfs::FileInfo info = {};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR) {
        return fail_report(report, "ELF file is not a regular VFS file");
    }
    if (info.size == 0 || info.size > guidexos::native_elf::MAX_ELF_FILE_BYTES) {
        return fail_report(report, "ELF file exceeds loader bounds");
    }
    const uint32_t fileBytes = static_cast<uint32_t>(info.size);
    const int32_t readBytes = vfs::read_file(path, s_file, fileBytes);
    if (readBytes < 0 || static_cast<uint32_t>(readBytes) != fileBytes) {
        return fail_report(report, "ELF file read was incomplete");
    }

    NativeElfValidationPolicy policy = default_validation_policy();
    policy.regionBase = s_context.regionBase;
    policy.regionSize = s_context.regionSize;
    NativeElfValidationResult validation = {};
    if (!validate_native_elf(s_file, fileBytes, policy, &validation)) {
        return fail_report(report, validation.error);
    }
    if (report) {
        report->imageBase = validation.imageBase;
        report->entryPoint = validation.entryPoint;
        report->mappedBytes = validation.mappedBytes;
        report->nxEnabled = s_nxEnabled;
    }

    serial::puts("ELF Loader: validation PASS\n");
    serial::puts("ELF Loader: image_base=0x");
    serial::put_hex64(validation.imageBase);
    serial::putc('\n');
    serial::puts("ELF Loader: entry=0x");
    serial::put_hex64(validation.entryPoint);
    serial::putc('\n');
    serial::puts("ELF Loader: mapped_bytes=");
    put_decimal_u64(validation.mappedBytes);
    serial::puts(" nx=");
    serial::puts(s_nxEnabled ? "enabled\n" : "unavailable\n");

    if (!prepare_page_tables(validation, report)) return false;

    serial::puts("ELF Loader: invoking gx_main\n");
    int32_t result = 0;
    if (!invoke_validated_native_entry(validation.entryPoint, nullptr, &result)) {
        return fail_report(report, "validated NativeElf entry invocation failed");
    }
    *returnValue = result;
    if (report) {
        report->returnValue = result;
        report->success = true;
        report->error = "";
    }
    serial::puts("ELF Loader: gx_main returned ");
    put_decimal_i32(result);
    serial::putc('\n');
    serial::puts("ELF Loader: execution PASS\n");
    return true;
}

} // namespace native_elf
} // namespace kernel
