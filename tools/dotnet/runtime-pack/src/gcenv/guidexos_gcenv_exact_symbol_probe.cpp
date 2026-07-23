// Inactive hosted probe for the exact locked GCToOSInterface, GCEvent and
// CLRCriticalSection symbols supplied by the adapted Workstation GC archive.
// It deliberately does not call RhInitialize or construct a GC heap.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <windows.h>

#include "gcenv.structs.h"
#include "gcenv.os.h"
#include "gcenv.windows.inl"
#include "../platform/guidexos_nativeaot_fls_adapter.h"
#include "../platform/guidexos_nativeaot_thread_adapter.h"
#include "../platform/guidexos_nativeaot_virtual_memory_adapter.h"

extern GCSystemInfo g_SystemInfo;

namespace {

int g_check = 0;

LONG WINAPI unhandledException(EXCEPTION_POINTERS* pointers) {
    const ULONG_PTR rip = pointers == nullptr || pointers->ContextRecord == nullptr
        ? 0 : static_cast<ULONG_PTR>(pointers->ContextRecord->Rip);
    const void* address = pointers == nullptr || pointers->ExceptionRecord == nullptr
        ? nullptr : pointers->ExceptionRecord->ExceptionAddress;
    const DWORD code = pointers == nullptr || pointers->ExceptionRecord == nullptr
        ? 0 : pointers->ExceptionRecord->ExceptionCode;
    const ULONG_PTR module = reinterpret_cast<ULONG_PTR>(GetModuleHandleW(nullptr));
    std::printf("unhandled exception 0x%08lx address=%p rip=0x%llx module=0x%llx rva=0x%llx\n",
                static_cast<unsigned long>(code), address,
                static_cast<unsigned long long>(rip),
                static_cast<unsigned long long>(module),
                static_cast<unsigned long long>(rip - module));
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

void require(bool condition) {
    ++g_check;
    if (!condition) std::exit(100 + g_check);
}

void flsCallback(void*) {}

} // namespace

int main() {
    SetUnhandledExceptionFilter(unhandledException);
    require(GCToOSInterface::Initialize());
    require(GCToOSInterface::GetPageSize() == 4096);
    require(g_SystemInfo.dwAllocationGranularity >= 4096);
    require(GCToOSInterface::GetTotalProcessorCount() == 1);
    require(GCToOSInterface::CanGetCurrentProcessorNumber());
    require(GCToOSInterface::GetCurrentProcessorNumber() == 0);
    require(GCToOSInterface::GetCurrentThreadIdForLogging() != 0);
    require(GCToOSInterface::GetCurrentProcessId() == 1);
    require(GCToOSInterface::QueryPerformanceFrequency() > 0);
    require(GCToOSInterface::QueryPerformanceCounter() > 0);
    require(GCToOSInterface::GetLowPrecisionTimeStamp() > 0);
    constexpr std::size_t pageSize = 4096;
    constexpr std::size_t regionSize = pageSize * 2;
    void* region = GCToOSInterface::VirtualReserve(
        regionSize, pageSize, VirtualReserveFlags::None);
    require(region != nullptr);
    require(GCToOSInterface::VirtualCommit(region, pageSize));
    auto* bytes = static_cast<unsigned char*>(region);
    for (std::size_t index = 0; index < pageSize; ++index) {
        bytes[index] = static_cast<unsigned char>(index ^ 0x5Au);
    }
    for (std::size_t index = 0; index < pageSize; ++index) {
        require(bytes[index] == static_cast<unsigned char>(index ^ 0x5Au));
    }
    require(GCToOSInterface::VirtualDecommit(region, pageSize));
    require(GCToOSInterface::VirtualCommit(region, pageSize));
    for (std::size_t index = 0; index < pageSize; ++index) {
        require(bytes[index] == 0);
    }
    require(guidexos::nativeaot::virtual_memory::protectVirtualMemoryRaw(
        region, pageSize,
        guidexos::nativeaot::virtual_memory::MemoryProtection::ReadOnly));
    require(GCToOSInterface::VirtualRelease(region, regionSize));

    GCEvent event;
    require(event.CreateAutoEventNoThrow(false));
    require(event.Wait(0, false) == 258);
    event.Set();
    require(event.Wait(1000, false) == 0);
    event.CloseEvent();

    CLRCriticalSection critical;
    require(critical.Initialize());
    critical.Enter();
    critical.Leave();
    critical.Destroy();

    guidexos_nativeaot_fls_initialize();
    require(guidexos_nativeaot_fls_attach_current_thread() != 0);
    const unsigned long flsIndex = guidexos_nativeaot_fls_alloc(flsCallback);
    require(flsIndex != 0xFFFFFFFFu);
    int flsValue = 42;
    require(guidexos_nativeaot_fls_set(flsIndex, &flsValue) != 0);
    require(guidexos_nativeaot_fls_get(flsIndex) == &flsValue);
    require(guidexos_nativeaot_fls_free(flsIndex) != 0);
    require(guidexos_nativeaot_fls_detach_current_thread() != 0);
    guidexos_nativeaot_fls_shutdown();

    void* threadValue = reinterpret_cast<void*>(0x1234u);
    guidexos::nativeaot::HelperThreadProbe* probe =
        guidexos::nativeaot::createHelperThreadProbe(threadValue);
    require(probe != nullptr);
    require(guidexos::nativeaot::startHelperThreadProbe(probe) ==
            gxos::runtime::EventStatus::Ok);
    std::uintptr_t result = 0;
    require(guidexos::nativeaot::joinHelperThreadProbe(
                probe, gxos::runtime::WaitTimeout::infinite(), &result) ==
            gxos::runtime::WaitResult::Signaled);
    require(result == reinterpret_cast<std::uintptr_t>(threadValue));
    require(guidexos::nativeaot::destroyHelperThreadProbe(probe) ==
            gxos::runtime::ThreadResult::Ok);

    GCToOSInterface::Shutdown();
    std::printf("Exact-symbol hosted probe: PASS\n");
    return 0;
}
