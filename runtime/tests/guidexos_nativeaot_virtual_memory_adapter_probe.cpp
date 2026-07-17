#include "../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_virtual_memory_adapter.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

int main() {
    using namespace guidexos::nativeaot::virtual_memory;
    using gxos::runtime::virtual_memory::MemoryProtection;
    using gxos::runtime::virtual_memory::VmResult;

    const std::size_t page = getPageSize();
    bool passed = page != 0 && getAllocationGranularity() >= page &&
        memoryAvailable(page) && !supportsLargePages() && !supportsNumaPlacement();
    const bool trueMode = trueReservationSemantics();
    std::cout << "Adapter backend mode: " << backendModeName() << "\n";
    std::cout << "True reserve/commit separation: "
              << (trueMode ? "PASS" : "FAIL") << "\n";
    passed = passed && trueMode;
    std::cout << "Page size: " << (passed ? "PASS" : "FAIL") << "\n";
    std::cout << "Allocation granularity: "
              << (passed ? "PASS" : "FAIL") << "\n";

    VirtualMemoryHandle* handle = nullptr;
    const VmResult reserved = reserveVirtualMemory(
        page * 3, page, nullptr, &handle, true);
    passed = passed && reserved == VmResult::Ok && handle != nullptr;
    std::cout << "Adapter reserve: " << (reserved == VmResult::Ok ? "PASS" : "FAIL") << "\n";

    bool zero = false;
    if (handle != nullptr) {
        std::uint8_t* bytes = static_cast<std::uint8_t*>(baseAddress(handle));
        const VmResult committed = commitVirtualMemory(
            handle, 0, page, MemoryProtection::ReadWrite);
        zero = committed == VmResult::Ok;
        if (zero) {
            for (std::size_t index = 0; index < page; ++index) {
                zero = zero && bytes[index] == 0;
            }
            bytes[0] = 0x7C;
        }
        passed = passed && zero;
        std::cout << "Adapter commit/zero: " << (zero ? "PASS" : "FAIL") << "\n";

        const VmResult decommitted = decommitVirtualMemory(handle, 0, page);
        const VmResult recommitted = commitVirtualMemory(
            handle, 0, page, MemoryProtection::ReadWrite);
        bool recommitZero = decommitted == VmResult::Ok && recommitted == VmResult::Ok;
        if (recommitZero) {
            for (std::size_t index = 0; index < page; ++index) {
                recommitZero = recommitZero && bytes[index] == 0;
            }
        }
        passed = passed && recommitZero;
        std::cout << "Adapter decommit/recommit: "
                  << (recommitZero ? "PASS" : "FAIL") << "\n";

        const VmResult reset = resetVirtualMemory(handle, 0, page, false);
        const bool resetUnsupported = reset == VmResult::Unsupported;
        passed = passed && resetUnsupported;
        std::cout << "Adapter reset classification: "
                  << (resetUnsupported ? "PASS" : "FAIL") << "\n";
    }

    const VmResult released = releaseVirtualMemory(&handle);
    const bool releasePassed = released == VmResult::Ok && handle == nullptr;
    passed = passed && releasePassed;
    std::cout << "Adapter release: " << (releasePassed ? "PASS" : "FAIL") << "\n";
    const bool doubleRelease = releaseVirtualMemory(&handle) == VmResult::AlreadyReleased;
    passed = passed && doubleRelease;
    std::cout << "Adapter stale release: " << (doubleRelease ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 1;
}
