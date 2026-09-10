// Minimal Win32 import bindings used by the locked NativeAOT Workstation GC
// during guideXOS startup.  A converted ELF has no PE loader to populate the
// import-address table, so the production image supplies the small, fixed
// platform contract directly.  This is deliberately not a general PE import
// resolver and does not expose a Windows compatibility layer to managed code.

#include <windows.h>

#include <stdint.h>
#include <new>

#include "guidexos_nativeaot_pal_contract.h"

namespace {

DWORD g_lastError = ERROR_SUCCESS;

DWORD WINAPI startupGetLastError() {
    return g_lastError;
}

BOOL WINAPI startupSetLastError(DWORD value) {
    g_lastError = value;
    return TRUE;
}

DWORD WINAPI startupGetEnvironmentVariableW(
    LPCWSTR, LPWSTR, DWORD) {
    g_lastError = ERROR_ENVVAR_NOT_FOUND;
    return 0;
}

VOID WINAPI startupGetSystemInfo(LPSYSTEM_INFO result) {
    if (result == nullptr) return;
    *result = {};
    result->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
    result->dwPageSize = 4096u;
    result->lpMinimumApplicationAddress = reinterpret_cast<void*>(0x10000000u);
    result->lpMaximumApplicationAddress = reinterpret_cast<void*>(0x7FFFFFFFFFFFu);
    result->dwActiveProcessorMask = 1u;
    result->dwNumberOfProcessors = 1u;
    result->dwProcessorType = PROCESSOR_INTEL_386;
    result->dwAllocationGranularity = 4096u;
}

HANDLE WINAPI startupGetCurrentProcess() {
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
}

HANDLE WINAPI startupGetCurrentThread() {
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2));
}

DWORD WINAPI startupGetCurrentThreadId() {
    return 1u;
}

DWORD WINAPI startupGetCurrentProcessorNumber() {
    return 0u;
}

VOID WINAPI startupGetCurrentProcessorNumberEx(PPROCESSOR_NUMBER result) {
    if (result != nullptr) *result = {};
}

BOOL WINAPI startupGetProcessGroupAffinity(
    HANDLE, PUSHORT groupCount, PUSHORT) {
    if (groupCount != nullptr) *groupCount = 1u;
    g_lastError = ERROR_INSUFFICIENT_BUFFER;
    return FALSE;
}

BOOL WINAPI startupGetProcessAffinityMask(
    HANDLE, PDWORD_PTR processMask, PDWORD_PTR systemMask) {
    if (processMask != nullptr) *processMask = 1u;
    if (systemMask != nullptr) *systemMask = 1u;
    return TRUE;
}

BOOL WINAPI startupGetLogicalProcessorInformationEx(
    LOGICAL_PROCESSOR_RELATIONSHIP,
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD length) {
    if (length != nullptr) *length = 0;
    g_lastError = ERROR_INSUFFICIENT_BUFFER;
    return FALSE;
}

BOOL WINAPI startupGlobalMemoryStatusEx(LPMEMORYSTATUSEX result) {
    if (result == nullptr) return FALSE;
    *result = {};
    result->dwLength = sizeof(MEMORYSTATUSEX);
    result->dwMemoryLoad = 0;
    result->ullTotalPhys = UINT64_C(1024) * 1024u * 1024u;
    result->ullAvailPhys = result->ullTotalPhys;
    result->ullTotalPageFile = result->ullTotalPhys;
    result->ullAvailPageFile = result->ullTotalPhys;
    result->ullTotalVirtual = UINT64_C(4) * 1024u * 1024u * 1024u;
    result->ullAvailVirtual = result->ullTotalVirtual;
    result->ullAvailExtendedVirtual = 0;
    return TRUE;
}

ULONGLONG WINAPI startupGetTickCount64() {
    return 1u;
}

BOOL WINAPI startupQueryPerformanceCounter(PLARGE_INTEGER result) {
    if (result == nullptr) return FALSE;
    result->QuadPart = 1;
    return TRUE;
}

BOOL WINAPI startupQueryPerformanceFrequency(PLARGE_INTEGER result) {
    if (result == nullptr) return FALSE;
    result->QuadPart = 1000000;
    return TRUE;
}

DWORD WINAPI startupSleepEx(DWORD, BOOL) {
    return 0u;
}

BOOL WINAPI startupSwitchToThread() {
    return TRUE;
}

VOID WINAPI startupFlushProcessWriteBuffers() {}

DWORD WINAPI startupGetConsoleOutputCP() {
    return 65001u;
}

DWORD64 WINAPI startupGetEnabledXStateFeatures() {
    return 0u;
}

VOID WINAPI startupGetSystemTimeAsFileTimeValue(LPFILETIME result) {
    if (result != nullptr) *result = {};
}

} // namespace

// These are ordinary, explicitly named startup-contract functions.  They are
// consumed only by production replacements of the small NativeAOT startup
// objects that otherwise call through the PE import address table.  Do not
// publish __imp_* definitions here: the rest of the locked runtime still uses
// the normal kernel32 import library and must not receive a global override.
extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetLastError() {
    return startupGetLastError();
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_SetLastError(DWORD value) {
    return startupSetLastError(value);
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetEnvironmentVariableW(
    LPCWSTR name, LPWSTR value, DWORD size) {
    return startupGetEnvironmentVariableW(name, value, size);
}

extern "C" VOID WINAPI guidexos_nativeaot_startup_GetSystemInfo(LPSYSTEM_INFO result) {
    startupGetSystemInfo(result);
}

extern "C" HANDLE WINAPI guidexos_nativeaot_startup_GetCurrentProcess() {
    return startupGetCurrentProcess();
}

extern "C" HANDLE WINAPI guidexos_nativeaot_startup_GetCurrentThread() {
    return startupGetCurrentThread();
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetCurrentThreadId() {
    return startupGetCurrentThreadId();
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetCurrentProcessorNumber() {
    return startupGetCurrentProcessorNumber();
}

extern "C" VOID WINAPI guidexos_nativeaot_startup_GetCurrentProcessorNumberEx(
    PPROCESSOR_NUMBER result) {
    startupGetCurrentProcessorNumberEx(result);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_GetProcessGroupAffinity(
    HANDLE process, PUSHORT groupCount, PUSHORT groups) {
    return startupGetProcessGroupAffinity(process, groupCount, groups);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_GetProcessAffinityMask(
    HANDLE process, PDWORD_PTR processMask, PDWORD_PTR systemMask) {
    return startupGetProcessAffinityMask(process, processMask, systemMask);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_GetLogicalProcessorInformationEx(
    LOGICAL_PROCESSOR_RELATIONSHIP relationship,
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX result, PDWORD length) {
    return startupGetLogicalProcessorInformationEx(relationship, result, length);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_GlobalMemoryStatusEx(
    LPMEMORYSTATUSEX result) {
    return startupGlobalMemoryStatusEx(result);
}

extern "C" ULONGLONG WINAPI guidexos_nativeaot_startup_GetTickCount64() {
    return startupGetTickCount64();
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_QueryPerformanceCounter(
    PLARGE_INTEGER result) {
    return startupQueryPerformanceCounter(result);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_QueryPerformanceFrequency(
    PLARGE_INTEGER result) {
    return startupQueryPerformanceFrequency(result);
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_SleepEx(DWORD timeout, BOOL alertable) {
    return startupSleepEx(timeout, alertable);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_SwitchToThread() {
    return startupSwitchToThread();
}

extern "C" VOID WINAPI guidexos_nativeaot_startup_FlushProcessWriteBuffers() {
    startupFlushProcessWriteBuffers();
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetConsoleOutputCP() {
    return startupGetConsoleOutputCP();
}

extern "C" DWORD64 WINAPI guidexos_nativeaot_startup_GetEnabledXStateFeatures() {
    return startupGetEnabledXStateFeatures();
}

extern "C" VOID WINAPI guidexos_nativeaot_startup_GetSystemTimeAsFileTime(
    LPFILETIME result) {
    startupGetSystemTimeAsFileTimeValue(result);
}

// The locked NativeAOT PAL headers declare their platform calls as
// __declspec(dllimport).  The two source-level replacements use unique import
// names so they can retain that ABI without colliding with the remaining
// kernel32 imports in the locked runtime.
#define GUIDEXOS_DEFINE_LOCAL_STARTUP_IMPORT(name) \
    extern "C" __declspec(selectany) void* __imp_##name = \
        reinterpret_cast<void*>(&name)

GUIDEXOS_DEFINE_LOCAL_STARTUP_IMPORT(guidexos_nativeaot_startup_GetLastError);
GUIDEXOS_DEFINE_LOCAL_STARTUP_IMPORT(guidexos_nativeaot_startup_GetEnvironmentVariableW);

// GcProbe.asm.obj carries the CRT abort symbol in its locked object contract.
// A production image has no CRT process terminator; preserve the non-returning
// failure semantics without importing the Windows CRT.
extern "C" __declspec(noreturn) void abort() {
    __debugbreak();
    for (;;) {}
}

extern "C" int __cdecl _stricmp(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return left == right ? 0 : (left == nullptr ? -1 : 1);
    }
    for (;;) {
        unsigned char leftChar = static_cast<unsigned char>(*left++);
        unsigned char rightChar = static_cast<unsigned char>(*right++);
        if (leftChar >= 'A' && leftChar <= 'Z') leftChar = static_cast<unsigned char>(leftChar + ('a' - 'A'));
        if (rightChar >= 'A' && rightChar <= 'Z') rightChar = static_cast<unsigned char>(rightChar + ('a' - 'A'));
        if (leftChar != rightChar) return leftChar < rightChar ? -1 : 1;
        if (leftChar == 0) return 0;
    }
}

extern "C" HMODULE WINAPI guidexos_nativeaot_startup_GetModuleHandleW(
    LPCWSTR) {
    return nullptr;
}

extern "C" FARPROC WINAPI guidexos_nativeaot_startup_GetProcAddress(
    HMODULE, LPCSTR) {
    return nullptr;
}

extern "C" int __cdecl guidexos_nativeaot_startup_atexit(
    void (__cdecl *)(void)) {
    // guideXOS keeps the kernel alive after application return, so this
    // process-shutdown registration has no production lifecycle consumer.
    return 0;
}

struct GuidexosNativeAotMallocHeader {
    uintptr_t size;
    uintptr_t reserved;
};

extern "C" void* __cdecl guidexos_nativeaot_startup_malloc(size_t size) {
    if (size == 0) size = 1;
    if (size > static_cast<size_t>(UINTPTR_MAX - sizeof(GuidexosNativeAotMallocHeader))) {
        return nullptr;
    }
    const uintptr_t total = static_cast<uintptr_t>(size) +
                            sizeof(GuidexosNativeAotMallocHeader);
    void* raw = guidexos_nativeaot_pal_virtual_alloc(
        nullptr, total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (raw == nullptr) return nullptr;
    auto* header = static_cast<GuidexosNativeAotMallocHeader*>(raw);
    header->size = total;
    header->reserved = 0;
    return header + 1;
}

extern "C" void __cdecl guidexos_nativeaot_startup_free(void* value) {
    if (value == nullptr) return;
    auto* header = static_cast<GuidexosNativeAotMallocHeader*>(value) - 1;
    (void)guidexos_nativeaot_pal_virtual_free(
        header, header->size, MEM_RELEASE);
}

extern "C" int __cdecl guidexos_nativeaot_startup_callnewh(size_t) {
    return 0;
}

extern "C" unsigned long long __cdecl guidexos_nativeaot_startup_strtoull(
    const char* value, char** end, int base) {
    if (end != nullptr) *end = const_cast<char*>(value);
    if (value == nullptr) return 0;
    if (base != 0 && (base < 2 || base > 36)) return 0;
    unsigned int radix = base == 0 ? 10u : static_cast<unsigned int>(base);
    unsigned long long result = 0;
    const char* cursor = value;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    bool negative = false;
    if (*cursor == '+' || *cursor == '-') {
        negative = *cursor == '-';
        ++cursor;
    }
    const char* first = cursor;
    for (;;) {
        unsigned int digit = 0;
        if (*cursor >= '0' && *cursor <= '9') digit = static_cast<unsigned int>(*cursor - '0');
        else if (*cursor >= 'a' && *cursor <= 'z') digit = static_cast<unsigned int>(*cursor - 'a' + 10);
        else if (*cursor >= 'A' && *cursor <= 'Z') digit = static_cast<unsigned int>(*cursor - 'A' + 10);
        else break;
        if (digit >= radix) break;
        result = result * radix + digit;
        ++cursor;
    }
    if (cursor == first) cursor = value;
    if (end != nullptr) *end = const_cast<char*>(cursor);
    return negative ? 0ull - result : result;
}

extern "C" unsigned long __cdecl guidexos_nativeaot_startup_strtoul(
    const char* value, char** end, int base) {
    return static_cast<unsigned long>(
        guidexos_nativeaot_startup_strtoull(value, end, base));
}

extern "C" void __cdecl guidexos_nativeaot_startup_wassert(
    const wchar_t*, const wchar_t*, unsigned) {
    guidexos_nativeaot_pal_fail_fast(0x43525401u);
}

extern "C" void* __cdecl malloc(size_t size) {
    return guidexos_nativeaot_startup_malloc(size);
}

extern "C" void __cdecl free(void* value) {
    guidexos_nativeaot_startup_free(value);
}

void* __cdecl operator new(size_t size) {
    void* result = guidexos_nativeaot_startup_malloc(size);
    if (result == nullptr) abort();
    return result;
}

void* __cdecl operator new(size_t size, const std::nothrow_t&) noexcept {
    return guidexos_nativeaot_startup_malloc(size);
}

void* __cdecl operator new[](size_t size) {
    return operator new(size);
}

void* __cdecl operator new[](size_t size, const std::nothrow_t&) noexcept {
    return guidexos_nativeaot_startup_malloc(size);
}

void __cdecl operator delete(void* value) noexcept {
    guidexos_nativeaot_startup_free(value);
}

void __cdecl operator delete(void* value, size_t) noexcept {
    guidexos_nativeaot_startup_free(value);
}

void __cdecl operator delete(void* value, const std::nothrow_t&) noexcept {
    guidexos_nativeaot_startup_free(value);
}

void __cdecl operator delete[](void* value) noexcept {
    guidexos_nativeaot_startup_free(value);
}

void __cdecl operator delete[](void* value, size_t) noexcept {
    guidexos_nativeaot_startup_free(value);
}

void __cdecl operator delete[](void* value, const std::nothrow_t&) noexcept {
    guidexos_nativeaot_startup_free(value);
}

extern "C" int __cdecl _callnewh(size_t size) {
    return guidexos_nativeaot_startup_callnewh(size);
}

extern "C" unsigned long long __cdecl strtoull(
    const char* value, char** end, int base) {
    return guidexos_nativeaot_startup_strtoull(value, end, base);
}

extern "C" unsigned long __cdecl strtoul(
    const char* value, char** end, int base) {
    return guidexos_nativeaot_startup_strtoul(value, end, base);
}

extern "C" void __cdecl _wassert(
    const wchar_t* expression, const wchar_t* file, unsigned line) {
    guidexos_nativeaot_startup_wassert(expression, file, line);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_InitializeCriticalSectionEx(
    LPCRITICAL_SECTION section, DWORD, DWORD) {
    if (section == nullptr) return FALSE;
    *section = {};
    section->LockCount = -1;
    return TRUE;
}

extern "C" VOID WINAPI guidexos_nativeaot_startup_EnterCriticalSection(
    LPCRITICAL_SECTION section) {
    if (section == nullptr) return;
    for (;;) {
        if (InterlockedCompareExchange(&section->LockCount, 0, -1) == -1) return;
        guidexos_nativeaot_pal_yield();
    }
}

extern "C" VOID WINAPI guidexos_nativeaot_startup_LeaveCriticalSection(
    LPCRITICAL_SECTION section) {
    if (section != nullptr) InterlockedExchange(&section->LockCount, -1);
}

extern "C" VOID WINAPI guidexos_nativeaot_startup_DeleteCriticalSection(
    LPCRITICAL_SECTION section) {
    if (section != nullptr) *section = {};
}

extern "C" HANDLE WINAPI guidexos_nativeaot_startup_CreateEventExW(
    LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD flags, DWORD) {
    const int manualReset = (flags & CREATE_EVENT_MANUAL_RESET) != 0;
    const int initialState = (flags & CREATE_EVENT_INITIAL_SET) != 0;
    return static_cast<HANDLE>(guidexos_nativeaot_pal_create_event(
        manualReset, initialState));
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_CloseHandle(HANDLE handle) {
    return guidexos_nativeaot_pal_close_handle(handle) == 0 ? TRUE : FALSE;
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_DuplicateHandle(
    HANDLE sourceProcess, HANDLE sourceHandle, HANDLE targetProcess,
    LPHANDLE targetHandle, DWORD access, BOOL inherit, DWORD options) {
    if (targetHandle == nullptr) return FALSE;
    return guidexos_nativeaot_pal_duplicate_handle(
        sourceProcess, sourceHandle, targetProcess, targetHandle,
        access, inherit ? 1 : 0, options) == 0 ? TRUE : FALSE;
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetStdHandle(DWORD) {
    return static_cast<DWORD>(reinterpret_cast<uintptr_t>(INVALID_HANDLE_VALUE));
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetThreadPriority(HANDLE) {
    return THREAD_PRIORITY_NORMAL;
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_IsDebuggerPresent() {
    return FALSE;
}

extern "C" HLOCAL WINAPI guidexos_nativeaot_startup_LocalFree(HLOCAL) {
    return nullptr;
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetModuleFileNameW(
    HMODULE, LPWSTR, DWORD) {
    g_lastError = ERROR_FILE_NOT_FOUND;
    return 0;
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_FormatMessageW(
    DWORD, LPCVOID, DWORD, DWORD, LPWSTR, DWORD, va_list*) {
    g_lastError = ERROR_NOT_SUPPORTED;
    return 0;
}

extern "C" int WINAPI guidexos_nativeaot_startup_MultiByteToWideChar(
    UINT, DWORD, LPCCH source, int sourceLength, LPWSTR destination,
    int destinationLength) {
    if (source == nullptr || destination == nullptr || destinationLength <= 0) return 0;
    int limit = sourceLength < 0 ? destinationLength - 1 : sourceLength;
    if (limit < 0) limit = 0;
    if (limit >= destinationLength) limit = destinationLength - 1;
    for (int i = 0; i < limit; ++i) destination[i] = static_cast<unsigned char>(source[i]);
    destination[limit] = L'\0';
    return limit + (sourceLength < 0 ? 1 : 0);
}

extern "C" int WINAPI guidexos_nativeaot_startup_WideCharToMultiByte(
    UINT, DWORD, LPCWCH source, int sourceLength, LPSTR destination,
    int destinationLength, LPCCH, LPBOOL) {
    if (source == nullptr || destination == nullptr || destinationLength <= 0) return 0;
    int limit = sourceLength < 0 ? destinationLength - 1 : sourceLength;
    if (limit < 0) limit = 0;
    if (limit >= destinationLength) limit = destinationLength - 1;
    for (int i = 0; i < limit; ++i) {
        destination[i] = source[i] <= 0x7F ? static_cast<char>(source[i]) : '?';
    }
    destination[limit] = '\0';
    return limit + (sourceLength < 0 ? 1 : 0);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_RaiseFailFastException(
    PEXCEPTION_RECORD, PCONTEXT, DWORD) {
    guidexos_nativeaot_pal_fail_fast(0x43525402u);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_SetEvent(HANDLE) {
    return FALSE;
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_ResetEvent(HANDLE) {
    return FALSE;
}

extern "C" VOID WINAPI guidexos_nativeaot_startup_Sleep(DWORD timeout) {
    (void)guidexos_nativeaot_startup_SleepEx(timeout, FALSE);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_WriteFile(
    HANDLE, LPCVOID, DWORD, LPDWORD written, LPOVERLAPPED) {
    if (written != nullptr) *written = 0;
    return FALSE;
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_WaitForSingleObjectEx(
    HANDLE handle, DWORD timeout, BOOL alertable) {
    HANDLE handles[1] = {handle};
    const int32_t result = guidexos_nativeaot_pal_wait_any(
        timeout, 1u, reinterpret_cast<void* const*>(handles), alertable ? 1 : 0);
    return result < 0 ? WAIT_FAILED : static_cast<DWORD>(result);
}

extern "C" DWORD WINAPI guidexos_nativeaot_startup_WaitForMultipleObjectsEx(
    DWORD count, const HANDLE* handles, BOOL waitAll, DWORD timeout, BOOL alertable) {
    if (waitAll) return WAIT_FAILED;
    const int32_t result = guidexos_nativeaot_pal_wait_any(
        timeout, count, reinterpret_cast<void* const*>(handles), alertable ? 1 : 0);
    return result < 0 ? WAIT_FAILED : static_cast<DWORD>(result);
}

extern "C" PVOID WINAPI guidexos_nativeaot_startup_AddVectoredExceptionHandler(
    ULONG, PVECTORED_EXCEPTION_HANDLER) {
    return nullptr;
}

extern "C" PVOID WINAPI guidexos_nativeaot_startup_RtlVirtualUnwind(
    ULONG, ULONG64, ULONG64, PRUNTIME_FUNCTION, PCONTEXT,
    PBOOLEAN, PULONG64, PKNONVOLATILE_CONTEXT_POINTERS) {
    return nullptr;
}

extern "C" SIZE_T WINAPI guidexos_nativeaot_startup_VirtualQuery(
    LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) {
    return 0;
}

extern "C" LPVOID WINAPI guidexos_nativeaot_startup_VirtualAlloc(
    LPVOID address, SIZE_T size, DWORD allocationType, DWORD protect) {
    return guidexos_nativeaot_pal_virtual_alloc(
        address, size, allocationType, protect);
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_VirtualFree(
    LPVOID address, SIZE_T size, DWORD freeType) {
    return guidexos_nativeaot_pal_virtual_free(address, size, freeType) == 0 ? TRUE : FALSE;
}

extern "C" HRESULT WINAPI guidexos_nativeaot_startup_CoInitializeEx(
    LPVOID, DWORD) {
    return E_NOTIMPL;
}

extern "C" void WINAPI guidexos_nativeaot_startup_CoUninitialize() {}

extern "C" HRESULT WINAPI guidexos_nativeaot_startup_CoGetApartmentType(
    APTTYPE*, APTTYPEQUALIFIER*) {
    return E_NOTIMPL;
}

extern "C" LONG WINAPI guidexos_nativeaot_startup_BCryptGenRandom(
    void*, PUCHAR, ULONG, ULONG) {
    return static_cast<LONG>(0xC00000BBu);
}

extern "C" HANDLE WINAPI guidexos_nativeaot_startup_RegisterEventSourceW(
    LPCWSTR, LPCWSTR) {
    return nullptr;
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_ReportEventW(
    HANDLE, WORD, WORD, DWORD, PSID, WORD, DWORD, LPCWSTR*, LPVOID) {
    return FALSE;
}

extern "C" BOOL WINAPI guidexos_nativeaot_startup_DeregisterEventSource(HANDLE) {
    return FALSE;
}

#define GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(name, target) \
    extern "C" __declspec(selectany) void* __imp_##name = \
        reinterpret_cast<void*>(&target)

GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetLastError, guidexos_nativeaot_startup_GetLastError);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(SetLastError, guidexos_nativeaot_startup_SetLastError);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetEnvironmentVariableW, guidexos_nativeaot_startup_GetEnvironmentVariableW);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetSystemInfo, guidexos_nativeaot_startup_GetSystemInfo);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetCurrentProcess, guidexos_nativeaot_startup_GetCurrentProcess);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetCurrentThread, guidexos_nativeaot_startup_GetCurrentThread);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetCurrentThreadId, guidexos_nativeaot_startup_GetCurrentThreadId);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetCurrentProcessorNumber, guidexos_nativeaot_startup_GetCurrentProcessorNumber);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetCurrentProcessorNumberEx, guidexos_nativeaot_startup_GetCurrentProcessorNumberEx);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetProcessGroupAffinity, guidexos_nativeaot_startup_GetProcessGroupAffinity);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetProcessAffinityMask, guidexos_nativeaot_startup_GetProcessAffinityMask);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetLogicalProcessorInformationEx, guidexos_nativeaot_startup_GetLogicalProcessorInformationEx);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetTickCount64, guidexos_nativeaot_startup_GetTickCount64);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(QueryPerformanceCounter, guidexos_nativeaot_startup_QueryPerformanceCounter);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(QueryPerformanceFrequency, guidexos_nativeaot_startup_QueryPerformanceFrequency);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(SleepEx, guidexos_nativeaot_startup_SleepEx);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(SwitchToThread, guidexos_nativeaot_startup_SwitchToThread);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(FlushProcessWriteBuffers, guidexos_nativeaot_startup_FlushProcessWriteBuffers);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetConsoleOutputCP, guidexos_nativeaot_startup_GetConsoleOutputCP);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetEnabledXStateFeatures, guidexos_nativeaot_startup_GetEnabledXStateFeatures);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetSystemTimeAsFileTime, guidexos_nativeaot_startup_GetSystemTimeAsFileTime);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(InitializeCriticalSectionEx, guidexos_nativeaot_startup_InitializeCriticalSectionEx);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(EnterCriticalSection, guidexos_nativeaot_startup_EnterCriticalSection);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(LeaveCriticalSection, guidexos_nativeaot_startup_LeaveCriticalSection);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(DeleteCriticalSection, guidexos_nativeaot_startup_DeleteCriticalSection);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(CloseHandle, guidexos_nativeaot_startup_CloseHandle);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(CreateEventExW, guidexos_nativeaot_startup_CreateEventExW);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(DuplicateHandle, guidexos_nativeaot_startup_DuplicateHandle);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(FormatMessageW, guidexos_nativeaot_startup_FormatMessageW);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetModuleFileNameW, guidexos_nativeaot_startup_GetModuleFileNameW);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetStdHandle, guidexos_nativeaot_startup_GetStdHandle);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(GetThreadPriority, guidexos_nativeaot_startup_GetThreadPriority);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(IsDebuggerPresent, guidexos_nativeaot_startup_IsDebuggerPresent);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(LocalFree, guidexos_nativeaot_startup_LocalFree);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(MultiByteToWideChar, guidexos_nativeaot_startup_MultiByteToWideChar);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(RaiseFailFastException, guidexos_nativeaot_startup_RaiseFailFastException);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(SetEvent, guidexos_nativeaot_startup_SetEvent);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(ResetEvent, guidexos_nativeaot_startup_ResetEvent);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(Sleep, guidexos_nativeaot_startup_Sleep);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(VirtualAlloc, guidexos_nativeaot_startup_VirtualAlloc);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(VirtualFree, guidexos_nativeaot_startup_VirtualFree);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(WaitForMultipleObjectsEx, guidexos_nativeaot_startup_WaitForMultipleObjectsEx);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(WideCharToMultiByte, guidexos_nativeaot_startup_WideCharToMultiByte);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(WriteFile, guidexos_nativeaot_startup_WriteFile);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(WaitForSingleObjectEx, guidexos_nativeaot_startup_WaitForSingleObjectEx);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(AddVectoredExceptionHandler, guidexos_nativeaot_startup_AddVectoredExceptionHandler);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(RtlVirtualUnwind, guidexos_nativeaot_startup_RtlVirtualUnwind);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(VirtualQuery, guidexos_nativeaot_startup_VirtualQuery);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(CoInitializeEx, guidexos_nativeaot_startup_CoInitializeEx);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(CoUninitialize, guidexos_nativeaot_startup_CoUninitialize);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(CoGetApartmentType, guidexos_nativeaot_startup_CoGetApartmentType);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(BCryptGenRandom, guidexos_nativeaot_startup_BCryptGenRandom);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(RegisterEventSourceW, guidexos_nativeaot_startup_RegisterEventSourceW);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(ReportEventW, guidexos_nativeaot_startup_ReportEventW);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(DeregisterEventSource, guidexos_nativeaot_startup_DeregisterEventSource);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(strtoull, guidexos_nativeaot_startup_strtoull);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(strtoul, guidexos_nativeaot_startup_strtoul);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(_wassert, guidexos_nativeaot_startup_wassert);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(_callnewh, guidexos_nativeaot_startup_callnewh);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(malloc, guidexos_nativeaot_startup_malloc);
GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT(free, guidexos_nativeaot_startup_free);

#undef GUIDEXOS_DEFINE_LOCAL_WIN_IMPORT

// A few locked NativeAOT objects reference the same APIs as direct C symbols
// instead of through the dllimport address cell.  These aliases keep those
// references on the same local PAL-backed implementations without restoring a
// PE import library.
#pragma comment(linker, "/alternatename:BCryptGenRandom=guidexos_nativeaot_startup_BCryptGenRandom")
#pragma comment(linker, "/alternatename:CloseHandle=guidexos_nativeaot_startup_CloseHandle")
#pragma comment(linker, "/alternatename:CoGetApartmentType=guidexos_nativeaot_startup_CoGetApartmentType")
#pragma comment(linker, "/alternatename:CoInitializeEx=guidexos_nativeaot_startup_CoInitializeEx")
#pragma comment(linker, "/alternatename:CoUninitialize=guidexos_nativeaot_startup_CoUninitialize")
#pragma comment(linker, "/alternatename:CreateEventExW=guidexos_nativeaot_startup_CreateEventExW")
#pragma comment(linker, "/alternatename:DeregisterEventSource=guidexos_nativeaot_startup_DeregisterEventSource")
#pragma comment(linker, "/alternatename:DuplicateHandle=guidexos_nativeaot_startup_DuplicateHandle")
#pragma comment(linker, "/alternatename:FormatMessageW=guidexos_nativeaot_startup_FormatMessageW")
#pragma comment(linker, "/alternatename:GetConsoleOutputCP=guidexos_nativeaot_startup_GetConsoleOutputCP")
#pragma comment(linker, "/alternatename:GetCurrentProcess=guidexos_nativeaot_startup_GetCurrentProcess")
#pragma comment(linker, "/alternatename:GetCurrentProcessorNumberEx=guidexos_nativeaot_startup_GetCurrentProcessorNumberEx")
#pragma comment(linker, "/alternatename:GetCurrentThread=guidexos_nativeaot_startup_GetCurrentThread")
#pragma comment(linker, "/alternatename:GetCurrentThreadId=guidexos_nativeaot_startup_GetCurrentThreadId")
#pragma comment(linker, "/alternatename:GetEnvironmentVariableW=guidexos_nativeaot_startup_GetEnvironmentVariableW")
#pragma comment(linker, "/alternatename:GetLastError=guidexos_nativeaot_startup_GetLastError")
#pragma comment(linker, "/alternatename:GetModuleFileNameW=guidexos_nativeaot_startup_GetModuleFileNameW")
#pragma comment(linker, "/alternatename:GetStdHandle=guidexos_nativeaot_startup_GetStdHandle")
#pragma comment(linker, "/alternatename:GetSystemTimeAsFileTime=guidexos_nativeaot_startup_GetSystemTimeAsFileTime")
#pragma comment(linker, "/alternatename:GetThreadPriority=guidexos_nativeaot_startup_GetThreadPriority")
#pragma comment(linker, "/alternatename:GetTickCount64=guidexos_nativeaot_startup_GetTickCount64")
#pragma comment(linker, "/alternatename:IsDebuggerPresent=guidexos_nativeaot_startup_IsDebuggerPresent")
#pragma comment(linker, "/alternatename:LocalFree=guidexos_nativeaot_startup_LocalFree")
#pragma comment(linker, "/alternatename:MultiByteToWideChar=guidexos_nativeaot_startup_MultiByteToWideChar")
#pragma comment(linker, "/alternatename:QueryPerformanceCounter=guidexos_nativeaot_startup_QueryPerformanceCounter")
#pragma comment(linker, "/alternatename:QueryPerformanceFrequency=guidexos_nativeaot_startup_QueryPerformanceFrequency")
#pragma comment(linker, "/alternatename:RaiseFailFastException=guidexos_nativeaot_startup_RaiseFailFastException")
#pragma comment(linker, "/alternatename:RegisterEventSourceW=guidexos_nativeaot_startup_RegisterEventSourceW")
#pragma comment(linker, "/alternatename:ReportEventW=guidexos_nativeaot_startup_ReportEventW")
#pragma comment(linker, "/alternatename:SetEvent=guidexos_nativeaot_startup_SetEvent")
#pragma comment(linker, "/alternatename:SetLastError=guidexos_nativeaot_startup_SetLastError")
#pragma comment(linker, "/alternatename:Sleep=guidexos_nativeaot_startup_Sleep")
#pragma comment(linker, "/alternatename:VirtualAlloc=guidexos_nativeaot_startup_VirtualAlloc")
#pragma comment(linker, "/alternatename:VirtualFree=guidexos_nativeaot_startup_VirtualFree")
#pragma comment(linker, "/alternatename:WaitForMultipleObjectsEx=guidexos_nativeaot_startup_WaitForMultipleObjectsEx")
#pragma comment(linker, "/alternatename:WaitForSingleObjectEx=guidexos_nativeaot_startup_WaitForSingleObjectEx")
#pragma comment(linker, "/alternatename:WideCharToMultiByte=guidexos_nativeaot_startup_WideCharToMultiByte")
#pragma comment(linker, "/alternatename:WriteFile=guidexos_nativeaot_startup_WriteFile")
#pragma comment(linker, "/alternatename:DeleteCriticalSection=guidexos_nativeaot_startup_DeleteCriticalSection")
#pragma comment(linker, "/alternatename:EnterCriticalSection=guidexos_nativeaot_startup_EnterCriticalSection")
#pragma comment(linker, "/alternatename:InitializeCriticalSectionEx=guidexos_nativeaot_startup_InitializeCriticalSectionEx")
#pragma comment(linker, "/alternatename:LeaveCriticalSection=guidexos_nativeaot_startup_LeaveCriticalSection")
#pragma comment(linker, "/alternatename:ResetEvent=guidexos_nativeaot_startup_ResetEvent")
#pragma comment(linker, "/alternatename:FlushProcessWriteBuffers=guidexos_nativeaot_startup_FlushProcessWriteBuffers")

GUIDEXOS_DEFINE_LOCAL_STARTUP_IMPORT(guidexos_nativeaot_startup_GetModuleHandleW);
GUIDEXOS_DEFINE_LOCAL_STARTUP_IMPORT(guidexos_nativeaot_startup_GetProcAddress);

#undef GUIDEXOS_DEFINE_LOCAL_STARTUP_IMPORT
