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
static NativeAppExecutionContext s_appRuntime = {};

static uint64_t read_stack_pointer()
{
#if defined(__GNUC__) || defined(__clang__)
    uint64_t value = 0;
    asm volatile ("mov %%rsp, %0" : "=r"(value));
    return value;
#else
    return 0;
#endif
}

static gx_result GX_CALL host_log(gx_app_context* context, const char* message)
{
    if (s_appRuntime.state != NativeAppExecutionState::Running ||
        !context || context != &s_appRuntime.appContext ||
        context->host != &s_appRuntime.hostCalls ||
        context->userData != &s_appRuntime) {
        return GX_ERROR_PERMISSION_DENIED;
    }

    uint32_t maximumReadableBytes = 0;
    if (!native_app_log_pointer_range(reinterpret_cast<uint64_t>(message),
                                      s_appRuntime.readOnlyDataBase,
                                      s_appRuntime.readOnlyDataSize,
                                      &maximumReadableBytes)) {
        return GX_ERROR_INVALID_ARGUMENT;
    }

    uint32_t length = 0;
    while (length < maximumReadableBytes && message[length] != '\0') ++length;
    if (length == maximumReadableBytes) return GX_ERROR_INVALID_ARGUMENT;

    serial::puts("NativeElf host log: ");
    serial::puts(message);
    serial::putc('\n');
    s_appRuntime.hostLogObserved = true;
    s_appRuntime.hostLogBytes = length;
    return GX_OK;
}

static uint32_t GX_CALL host_get_api_version(gx_app_context* context)
{
    if (s_appRuntime.state != NativeAppExecutionState::Running ||
        !context || context != &s_appRuntime.appContext ||
        context->host != &s_appRuntime.hostCalls ||
        context->userData != &s_appRuntime) return 0;
    return GX_API_VERSION;
}

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

static bool set_page_permissions(uint64_t start,
                                 uint64_t end,
                                 bool writable,
                                 bool executable);
static void clear_bytes(uint8_t* bytes, uint64_t count);

static bool find_read_only_data(const NativeElfValidationResult& validation,
                                uint64_t* dataBase,
                                uint64_t* dataBytes)
{
    if (!dataBase || !dataBytes) return false;
    *dataBase = 0;
    *dataBytes = 0;
    for (uint32_t i = 0; i < validation.loadCount; ++i) {
        const NativeElfLoad& load = validation.loads[i];
        if ((load.flags & PF_R) != 0 && (load.flags & PF_W) == 0 &&
            (load.flags & PF_X) == 0 && load.fileSize != 0) {
            *dataBase = load.virtualAddress;
            *dataBytes = load.fileSize;
            return true;
        }
    }
    return true;
}

static bool prepare_application_stack(NativeElfRunReport* report)
{
    NativeAppStackLayout stack = {};
    if (!calculate_application_stack_layout(APPLICATION_STACK_BASE,
                                            APPLICATION_STACK_SIZE,
                                            &stack)) {
        return fail_report(report, "application stack layout is invalid");
    }
    if (!set_page_permissions(stack.base, stack.top, true, false)) {
        return fail_report(report, "application stack is not safely mapped by kernel page tables");
    }
    clear_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(stack.base)), stack.size);
    s_appRuntime.stackBase = stack.base;
    s_appRuntime.stackSize = stack.size;
    s_appRuntime.stackTop = stack.top;
    if (report) {
        report->applicationStackBase = stack.base;
        report->applicationStackTop = stack.top;
    }
    return true;
}

static void initialize_app_context()
{
    s_appRuntime.hostCalls = {};
    s_appRuntime.hostCalls.size = sizeof(gx_host_calls);
    s_appRuntime.hostCalls.version = GX_API_VERSION;
    s_appRuntime.hostCalls.log = host_log;
    s_appRuntime.hostCalls.get_api_version = host_get_api_version;

    s_appRuntime.appContext = {};
    s_appRuntime.appContext.size = sizeof(gx_app_context);
    s_appRuntime.appContext.apiVersion = GX_API_VERSION;
    s_appRuntime.appContext.host = &s_appRuntime.hostCalls;
    s_appRuntime.appContext.userData = &s_appRuntime;
}

static bool teardown_application(NativeElfRunReport* report)
{
    bool clean = true;
    if (s_appRuntime.imageBase != 0 && s_appRuntime.imageSize != 0) {
        uint64_t imageEnd = 0;
        if (s_appRuntime.imageBase > ~static_cast<uint64_t>(0) - s_appRuntime.imageSize) {
            clean = false;
        } else {
            imageEnd = s_appRuntime.imageBase + s_appRuntime.imageSize;
            if (!set_page_permissions(s_appRuntime.imageBase, imageEnd, true, false)) clean = false;
            clear_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(s_appRuntime.imageBase)),
                        s_appRuntime.imageSize);
            if (!set_page_permissions(s_appRuntime.imageBase, imageEnd, false, false)) clean = false;
        }
    }

    if (s_appRuntime.stackBase != 0 && s_appRuntime.stackSize != 0) {
        const uint64_t stackEnd = s_appRuntime.stackBase + s_appRuntime.stackSize;
        clear_bytes(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(s_appRuntime.stackBase)),
                    s_appRuntime.stackSize);
        if (!set_page_permissions(s_appRuntime.stackBase, stackEnd, false, false)) clean = false;
    }

    const int32_t preservedResult = s_appRuntime.result;
    s_appRuntime.appContext = {};
    s_appRuntime.hostCalls = {};
    s_appRuntime.imageBase = 0;
    s_appRuntime.imageSize = 0;
    s_appRuntime.entryPoint = 0;
    s_appRuntime.readOnlyDataBase = 0;
    s_appRuntime.readOnlyDataSize = 0;
    s_appRuntime.stackBase = 0;
    s_appRuntime.stackSize = 0;
    s_appRuntime.stackTop = 0;
    s_appRuntime.result = preservedResult;
    s_appRuntime.state = clean ? NativeAppExecutionState::Cleaned : NativeAppExecutionState::Failed;
    s_appRuntime.error = clean ? "" : "NativeElf teardown could not restore page permissions";
    if (report) {
        report->teardownComplete = clean;
        report->finalState = s_appRuntime.state;
    }
    return clean;
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

    // A previous invocation must have completed its cleanup before the single
    // reusable application context can be prepared again.
    if (s_appRuntime.state == NativeAppExecutionState::Running ||
        s_appRuntime.state == NativeAppExecutionState::Loaded ||
        s_appRuntime.state == NativeAppExecutionState::Prepared) {
        return fail_report(report, "NativeElf application context is still active");
    }
    s_appRuntime = {};
    s_appRuntime.state = NativeAppExecutionState::Empty;

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

    s_appRuntime.imageBase = validation.imageBase;
    s_appRuntime.imageSize = validation.mappedBytes;
    s_appRuntime.entryPoint = validation.entryPoint;
    (void)find_read_only_data(validation, &s_appRuntime.readOnlyDataBase,
                              &s_appRuntime.readOnlyDataSize);
    s_appRuntime.state = NativeAppExecutionState::Loaded;
    if (report) {
        report->readOnlyDataBase = s_appRuntime.readOnlyDataBase;
        report->readOnlyDataBytes = s_appRuntime.readOnlyDataSize;
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

    if (!prepare_page_tables(validation, report)) {
        s_appRuntime.state = NativeAppExecutionState::Failed;
        (void)teardown_application(report);
        return false;
    }
    if (!prepare_application_stack(report)) {
        s_appRuntime.state = NativeAppExecutionState::Failed;
        (void)teardown_application(report);
        return false;
    }
    initialize_app_context();
    s_appRuntime.state = NativeAppExecutionState::Prepared;
    if (report) report->appContextValid =
        s_appRuntime.appContext.size == sizeof(gx_app_context) &&
        s_appRuntime.appContext.apiVersion == GX_API_VERSION &&
        s_appRuntime.appContext.host == &s_appRuntime.hostCalls &&
        s_appRuntime.appContext.host->log != nullptr &&
        s_appRuntime.appContext.userData == &s_appRuntime;

    serial::puts("ELF Loader: dedicated application stack base=0x");
    serial::put_hex64(s_appRuntime.stackBase);
    serial::puts(" top=0x");
    serial::put_hex64(s_appRuntime.stackTop);
    serial::putc('\n');
    serial::puts("ELF Loader: invoking gx_main with gx_app_context\n");
    s_appRuntime.kernelRspBefore = read_stack_pointer();
    s_appRuntime.state = NativeAppExecutionState::Running;
    NativeElfTrampolineResult trampoline = {};
    const bool invoked = invoke_native_entry_on_stack(validation.entryPoint,
                                                      &s_appRuntime.appContext,
                                                      s_appRuntime.stackTop,
                                                      &trampoline);
    s_appRuntime.kernelRspAfter = read_stack_pointer();
    s_appRuntime.applicationRsp = trampoline.applicationRsp;
    if (!invoked) {
        s_appRuntime.state = NativeAppExecutionState::Failed;
        if (report) {
            report->kernelRspBefore = s_appRuntime.kernelRspBefore;
            report->kernelRspAfter = s_appRuntime.kernelRspAfter;
            report->applicationRsp = s_appRuntime.applicationRsp;
        }
        (void)teardown_application(report);
        return fail_report(report, "NativeElf stack trampoline invocation failed");
    }
    s_appRuntime.result = trampoline.returnValue;
    *returnValue = s_appRuntime.result;
    s_appRuntime.state = NativeAppExecutionState::Returned;
    if (report) {
        report->returnValue = s_appRuntime.result;
        report->kernelRspBefore = s_appRuntime.kernelRspBefore;
        report->kernelRspAfter = s_appRuntime.kernelRspAfter;
        report->applicationRsp = s_appRuntime.applicationRsp;
        report->hostLogObserved = s_appRuntime.hostLogObserved;
        report->hostLogBytes = s_appRuntime.hostLogBytes;
        report->dedicatedStackUsed =
            native_app_pointer_in_range(s_appRuntime.applicationRsp,
                                        s_appRuntime.stackBase,
                                        s_appRuntime.stackSize) &&
            s_appRuntime.kernelRspBefore == s_appRuntime.kernelRspAfter;
    }
    serial::puts("ELF Loader: gx_main returned ");
    put_decimal_i32(s_appRuntime.result);
    serial::putc('\n');
    const bool teardownComplete = teardown_application(report);
    if (report) {
        report->success = teardownComplete;
        report->error = teardownComplete ? "" : s_appRuntime.error;
    }
    serial::puts(teardownComplete ? "ELF Loader: teardown PASS\n"
                                  : "ELF Loader: teardown FAIL\n");
    return teardownComplete;
}

bool native_elf_execution_active()
{
    return s_appRuntime.state == NativeAppExecutionState::Running;
}

const NativeAppExecutionContext* native_elf_runtime_context()
{
    return &s_appRuntime;
}

bool native_elf_host_call_validation_smoke()
{
    NativeAppStackLayout stack = {};
    uint32_t maximumReadableBytes = 0;
    const uint64_t dataBase = guidexos::native_elf::IMAGE_BASE + 0x2000ULL;
    const bool stackBounds = calculate_application_stack_layout(
        APPLICATION_STACK_BASE, APPLICATION_STACK_SIZE, &stack) &&
        stack.top == APPLICATION_STACK_BASE + APPLICATION_STACK_SIZE &&
        (stack.top & (APPLICATION_STACK_ALIGNMENT - 1ULL)) == 0;
    const bool nullRejected = !native_app_log_pointer_range(0, dataBase, 256, &maximumReadableBytes);
    const bool beforeRejected = !native_app_log_pointer_range(dataBase - 1, dataBase, 256, &maximumReadableBytes);
    const bool afterRejected = !native_app_log_pointer_range(dataBase + 256, dataBase, 256, &maximumReadableBytes);
    const bool boundedAtEnd = native_app_log_pointer_range(dataBase + 254, dataBase, 256,
                                                           &maximumReadableBytes) &&
                              maximumReadableBytes == 2;
    const bool inactiveContextRejected = host_log(nullptr, nullptr) == GX_ERROR_PERMISSION_DENIED;
    return stackBounds && nullRejected && beforeRejected && afterRejected &&
           boundedAtEnd && inactiveContextRejected;
}

} // namespace native_elf
} // namespace kernel
