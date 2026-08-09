// Narrow local definitions for the Windows imports retained by the generated
// NativeAOT image. The disposable ELF launch never exercises these services;
// providing the symbols locally keeps the converted image import-free.
#include <stdint.h>

extern "C" long BCryptGenRandom(...) { return 0; }
extern "C" long CoGetApartmentType(...) { return 0; }
extern "C" long CoInitializeEx(...) { return 0; }
extern "C" void CoUninitialize(...) {}
extern "C" long DeregisterEventSource(...) { return 1; }
extern "C" long DuplicateHandle(...) { return 0; }
extern "C" uint32_t FormatMessageW(...) { return 0; }
extern "C" uint32_t GetConsoleOutputCP(...) { return 65001u; }
extern "C" void* GetCurrentProcess(...) { return reinterpret_cast<void*>(~uintptr_t(0)); }
extern "C" void GetCurrentProcessorNumberEx(...) {}
extern "C" void* GetCurrentThread(...) { return reinterpret_cast<void*>(~uintptr_t(0)); }
extern "C" uint32_t GetLastError(...) { return 0; }
extern "C" uint32_t GetModuleFileNameW(...) { return 0; }
extern "C" void* GetStdHandle(...) { return nullptr; }
extern "C" long GetThreadPriority(...) { return 0; }
extern "C" long IsDebuggerPresent(...) { return 0; }
extern "C" void* LoadLibraryExW(...) { return nullptr; }
extern "C" long FreeLibrary(...) { return 0; }
extern "C" uint32_t SetThreadErrorMode(...) { return 0; }
extern "C" void* LocalFree(...) { return nullptr; }
extern "C" int32_t MultiByteToWideChar(...) { return 0; }
extern "C" void* RegisterEventSourceW(...) { return nullptr; }
extern "C" long ReportEventW(...) { return 0; }
extern "C" void SetLastError(...) {}
extern "C" uint32_t WaitForMultipleObjectsEx(...) { return 0xFFFFFFFFu; }
extern "C" int32_t WideCharToMultiByte(...) { return 0; }
extern "C" long WriteFile(...) { return 0; }

#if defined(GUIDEXOS_NATIVEAOT_THREAD_STATIC_PROOF)
// The shared platform object is compiled with the locked real-GC allocation
// feature switches because it contains the common startup/runtime bridge.
// These three generated import cells belong only to the collection-boundary
// probe branches; the thread-static proof never calls them.  Keep the cells
// linkable without exposing a proof-specific managed operation or storage
// substitute.
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationValidateObject__Ansi = nullptr;
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationGetHardLimit__Ansi = nullptr;
extern "C" void* __pinvoke_HostLogProof__Module____Internal__guideXosManagedAllocationRecordSentinelValidation__Ansi = nullptr;

// The adapted Workstation GC object used by the bounded harness retains its
// optional VM-trace registration calls.  The standalone thread-static proof
// does not enable that experiment, so these proof-link shims leave the
// optional callbacks unset without changing the NativeAOT thread-static path.
extern "C" void __cdecl guideXosManagedAllocationInstallVmTraceCallbacks(
    uintptr_t, uintptr_t) {}
extern "C" void __cdecl guideXosManagedAllocationInstallSegmentDescriber(
    uintptr_t) {}
#endif
