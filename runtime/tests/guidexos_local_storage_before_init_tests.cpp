#include "runtime/local_storage/guidexos_local_storage.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

using gxos::runtime::LocalStorageIndex;
using gxos::runtime::LocalStorageResult;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void pass(const char* name) {
    std::cout << name << ": PASS\n";
}

void callback(void*) {}

} // namespace

int main() {
    try {
        require(!gxos::runtime::isLocalStorageInitialized(),
                "manager unexpectedly initialized at process start");

        LocalStorageIndex index{0xFFFFFFFFu, 0xFFFFFFFFu};
        const LocalStorageResult allocated =
            gxos::runtime::allocateLocalStorageIndex(callback, &index);
        require(allocated == LocalStorageResult::NotInitialized &&
                    !index.isValid() && index.generation == 0,
                "pre-init allocation did not return an invalid index");
        pass("Allocate before initialization");

        void* value = reinterpret_cast<void*>(0xCAFEu);
        require(gxos::runtime::getLocalStorageValue(index, &value) ==
                    LocalStorageResult::NotInitialized && value == nullptr,
                "pre-init get did not return NotInitialized");
        pass("Get before initialization");

        require(gxos::runtime::setLocalStorageValue(index,
                                                    reinterpret_cast<void*>(0x1u)) ==
                    LocalStorageResult::NotInitialized,
                "pre-init set did not return NotInitialized");
        pass("Set before initialization");

        require(gxos::runtime::releaseLocalStorageIndex(index) ==
                    LocalStorageResult::NotInitialized,
                "pre-init release did not return NotInitialized");
        pass("Release before initialization");

        require(gxos::runtime::attachLocalStorage() ==
                    LocalStorageResult::NotInitialized,
                "pre-init attach did not return NotInitialized");
        pass("Thread attach before initialization");

        require(gxos::runtime::shutdownLocalStorage() ==
                    LocalStorageResult::NotInitialized,
                "pre-init shutdown did not return NotInitialized");
        pass("Shutdown before initialization");

        require(!gxos::runtime::isLocalStorageInitialized(),
                "pre-init calls mutated manager initialization state");
        pass("No hidden pre-init manager state");

        require(gxos::runtime::initializeLocalStorage() ==
                    LocalStorageResult::Success,
                "manager initialization failed after pre-init calls");
        require(gxos::runtime::attachLocalStorage() ==
                    LocalStorageResult::Success,
                "attach failed after normal initialization");
        LocalStorageIndex recovered{};
        require(gxos::runtime::allocateLocalStorageIndex(callback, &recovered) ==
                    LocalStorageResult::Success && recovered.isValid(),
                "post-init allocation failed");
        const auto* recoveryValue = reinterpret_cast<void*>(0x1234u);
        require(gxos::runtime::setLocalStorageValue(
                    recovered, const_cast<void*>(recoveryValue)) ==
                    LocalStorageResult::Success,
                "post-init set failed");
        void* observed = nullptr;
        require(gxos::runtime::getLocalStorageValue(recovered, &observed) ==
                    LocalStorageResult::Success && observed == recoveryValue,
                "post-init get failed");
        require(gxos::runtime::releaseLocalStorageIndex(recovered) ==
                    LocalStorageResult::Success,
                "post-init release failed");
        require(gxos::runtime::detachLocalStorage() ==
                    LocalStorageResult::Success,
                "post-init detach failed");
        require(gxos::runtime::shutdownLocalStorage() ==
                    LocalStorageResult::Success,
                "post-init shutdown failed");
        pass("Initialized lifecycle recovery");
        std::cout << "FLS-before-init: ALL_PASS\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "FLS-before-init failure: " << error.what() << "\n";
        std::cout << "FLS-before-init: ALL_FAIL\n";
        return 1;
    }
}
