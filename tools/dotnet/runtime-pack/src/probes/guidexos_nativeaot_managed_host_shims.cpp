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
