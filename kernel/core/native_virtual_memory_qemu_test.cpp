#include "include/kernel/native_virtual_memory_qemu_test.h"

#if defined(GXOS_NATIVE_VIRTUAL_MEMORY_QEMU_TEST)

#include "include/kernel/serial_debug.h"
#include "runtime/memory/guidexos_virtual_memory_region.h"

namespace kernel {
namespace native_virtual_memory_qemu_test {
namespace {

using gxos::runtime::virtual_memory::MemoryProtection;
using gxos::runtime::virtual_memory::VmResult;
using gxos::runtime::virtual_memory::VirtualMemoryInfo;
using gxos::runtime::virtual_memory::VirtualMemoryRegion;
using gxos::runtime::virtual_memory::VirtualMemoryStats;

using vm_size = gxos_vm_size;

bool g_hasFailure = false;
bool g_hasBlocked = false;

void status(const char* name, bool passed) {
    kernel::serial::puts("[native-virtual-memory-test] ");
    kernel::serial::puts(name);
    kernel::serial::puts(passed ? ": PASS\n" : ": FAIL\n");
    if (!passed) g_hasFailure = true;
}

void blocked(const char* name) {
    kernel::serial::puts("[native-virtual-memory-test] ");
    kernel::serial::puts(name);
    kernel::serial::puts(": BLOCKED\n");
    g_hasBlocked = true;
}

bool zero(const uint8_t* address, vm_size size) {
    for (vm_size index = 0; index < size; ++index) {
        if (address[index] != 0) return false;
    }
    return true;
}

} // namespace

void run() {
    kernel::serial::puts("[native-virtual-memory-test] BEGIN\n");
    const vm_size page = gxos::runtime::virtual_memory::pageSize();
    const VirtualMemoryStats initial = gxos::runtime::virtual_memory::stats();
    status("Initial metadata", initial.activeRegions == 0 && initial.committedPages == 0);

    VirtualMemoryRegion region;
    const VmResult reserved = gxos::runtime::virtual_memory::reserve(
        page * 4, page, nullptr, &region);
    status("Reservation", reserved == VmResult::Ok && region.base != nullptr);

    VirtualMemoryInfo info{};
    const bool queried = reserved == VmResult::Ok &&
        gxos::runtime::virtual_memory::query(region.base, &info) == VmResult::Ok &&
        info.reserved && !info.committed;
    status("Query", queried);

    VirtualMemoryRegion overlap;
    const bool overlapRejected = reserved == VmResult::Ok &&
        gxos::runtime::virtual_memory::reserve(page, page, region.base, &overlap) ==
            VmResult::AddressUnavailable;
    status("Overlap rejection", overlapRejected);

    bool committed = false;
    bool zeroInitialized = false;
    bool readableWritable = false;
    if (reserved == VmResult::Ok) {
        uint8_t* bytes = static_cast<uint8_t*>(region.base);
        const VmResult result = gxos::runtime::virtual_memory::commit(
            region, 0, page, MemoryProtection::ReadWrite);
        committed = result == VmResult::Ok;
        zeroInitialized = committed && zero(bytes, page);
        if (committed) {
            bytes[0] = 0xA5;
            bytes[page - 1] = 0x5A;
        }
        readableWritable = committed && bytes[0] == 0xA5 && bytes[page - 1] == 0x5A;
    }
    status("Commit", committed);
    status("Zero initialization", zeroInitialized);
    status("Read/write access", readableWritable);

    bool partial = false;
    bool recommitZero = false;
    if (reserved == VmResult::Ok) {
        uint8_t* bytes = static_cast<uint8_t*>(region.base);
        const bool partialCommit =
            gxos::runtime::virtual_memory::commit(region, page, page,
                                                   MemoryProtection::ReadWrite) == VmResult::Ok &&
            gxos::runtime::virtual_memory::commit(region, page * 3, page,
                                                   MemoryProtection::ReadWrite) == VmResult::Ok;
        if (partialCommit) bytes[page * 3] = 0x3C;
        const bool partialQuery = partialCommit &&
            gxos::runtime::virtual_memory::query(bytes + page * 3, &info) == VmResult::Ok &&
            info.committed;
        const bool decommitted = partialCommit &&
            gxos::runtime::virtual_memory::decommit(region, page, page) == VmResult::Ok &&
            gxos::runtime::virtual_memory::query(bytes + page, &info) == VmResult::Ok &&
            !info.committed && bytes[page * 3] == 0x3C;
        const bool recommitted = decommitted &&
            gxos::runtime::virtual_memory::commit(region, page, page,
                                                   MemoryProtection::ReadWrite) == VmResult::Ok;
        recommitZero = recommitted && zero(bytes + page, page);
        partial = partialCommit && partialQuery && decommitted;
    }
    status("Partial commit", partial);
    status("Partial decommit", partial);
    status("Recommit zeroing", recommitZero);

    if (!gxos::runtime::virtual_memory::protectionIsEnforced()) {
        blocked("Protection transition");
        blocked("Expected-fault guard test");
        blocked("Physical-page leak check");
    }
    else {
        status("Protection transition", false);
        status("Expected-fault guard test", false);
        status("Physical-page leak check", false);
    }

    const void* oldBase = region.base;
    const VmResult released = gxos::runtime::virtual_memory::release(region);
    status("Release", released == VmResult::Ok && region.base == nullptr);
    status("Range reuse", gxos::runtime::virtual_memory::reserve(
        page, page, const_cast<void*>(oldBase), &region) == VmResult::Ok);
    if (region.base != nullptr) (void)gxos::runtime::virtual_memory::release(region);
    const VirtualMemoryStats final = gxos::runtime::virtual_memory::stats();
    status("Reservation-metadata leak check", final.activeRegions == 0);

    if (g_hasFailure) {
        kernel::serial::puts("[native-virtual-memory-test] ALL_FAIL\n");
    }
    else if (g_hasBlocked) {
        kernel::serial::puts("[native-virtual-memory-test] ALL_BLOCKED\n");
    }
    else {
        kernel::serial::puts("[native-virtual-memory-test] ALL_PASS\n");
    }
}

} // namespace native_virtual_memory_qemu_test
} // namespace kernel

#endif
