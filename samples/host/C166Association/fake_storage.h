#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include "../../../kernel/core/include/kernel/file_association_service.h"

struct FakeAssociationStorage {
    std::array<std::vector<uint8_t>, 2> slots;
    int partialWrite = -1;
    int failReadSlot = -1;
    int failFlushSlot = -1;
    int failWriteSlot = -1;
    int corruptReadbackSlot = -1;
    uint32_t writeCalls = 0;

    static bool Read(void* context, uint32_t slot, void* buffer, uint32_t size,
                     bool* exists) {
        auto& self = *static_cast<FakeAssociationStorage*>(context);
        if (slot > 1 || !exists) return false;
        auto& bytes = self.slots[slot];
        *exists = !bytes.empty();
        if (static_cast<int>(slot) == self.failReadSlot || bytes.size() != size)
            return false;
        if (static_cast<int>(slot) == self.corruptReadbackSlot && !bytes.empty()) {
            bytes[0] ^= 0x80;
            self.corruptReadbackSlot = -1;
        }
        std::copy(bytes.begin(), bytes.end(), static_cast<uint8_t*>(buffer));
        return true;
    }
    static bool Write(void* context, uint32_t slot, const void* buffer,
                      uint32_t size) {
        auto& self = *static_cast<FakeAssociationStorage*>(context);
        ++self.writeCalls;
        if (slot > 1) return false;
        if (static_cast<int>(slot) == self.failWriteSlot) return false;
        auto& bytes = self.slots[slot];
        const uint32_t count = self.partialWrite < 0
            ? size : static_cast<uint32_t>(self.partialWrite);
        bytes.assign(static_cast<const uint8_t*>(buffer),
                     static_cast<const uint8_t*>(buffer) + count);
        return count == size;
    }
    static bool Flush(void* context, uint32_t slot) {
        auto& self = *static_cast<FakeAssociationStorage*>(context);
        return slot < 2 && static_cast<int>(slot) != self.failFlushSlot;
    }
    kernel::appmodel::AssociationStorage Interface() {
        return { this, Read, Write, Flush };
    }
};
