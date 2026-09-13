#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "guidexos/development_debug.h"

namespace {

struct Model {
    gx_development_debug_breakpoint records[GX_DEVELOPMENT_DEBUG_MAX_SOURCE_BREAKPOINTS] = {};
    uint32_t count = 0;
    uint64_t handle = 7;
    uint64_t generation = 41;
    uint64_t nextSequence = 1;

    uint64_t add(uint64_t address, uint32_t line, const char* path,
                 const char* condition, uint32_t* status) {
        if (count == GX_DEVELOPMENT_DEBUG_MAX_SOURCE_BREAKPOINTS) {
            *status = GX_DEVELOPMENT_DEBUG_BREAKPOINT_STATUS_CAPACITY;
            return 0;
        }
        for (auto& record : records) {
            if (record.breakpointId != 0 && record.targetAddress == address) {
                *status = GX_DEVELOPMENT_DEBUG_BREAKPOINT_STATUS_DUPLICATE;
                return 0;
            }
        }
        for (auto& record : records) {
            if (record.breakpointId == 0) {
                record.breakpointId = (handle << 32) | nextSequence++;
                record.sessionGeneration = generation;
                record.targetAddress = address;
                record.enabled = 1;
                record.sourceLine = line;
                record.sourceMappingValid = 1;
                std::strncpy(record.sourcePath, path, sizeof(record.sourcePath) - 1);
                if (condition && condition[0] != '\0') {
                    record.conditionPresent = 1;
                    record.conditionLength = static_cast<uint32_t>(std::strlen(condition));
                }
                ++count;
                *status = GX_DEVELOPMENT_DEBUG_BREAKPOINT_STATUS_SUCCESS;
                return record.breakpointId;
            }
        }
        *status = GX_DEVELOPMENT_DEBUG_BREAKPOINT_STATUS_CAPACITY;
        return 0;
    }

    void list(gx_development_debug_snapshot* snapshot) const {
        snapshot->breakpointCount = 0;
        snapshot->breakpointCapacity = GX_DEVELOPMENT_DEBUG_MAX_SOURCE_BREAKPOINTS;
        const gx_development_debug_breakpoint* ordered[GX_DEVELOPMENT_DEBUG_MAX_SOURCE_BREAKPOINTS] = {};
        uint32_t orderedCount = 0;
        for (const auto& record : records) {
            if (record.breakpointId == 0) continue;
            ordered[orderedCount++] = &record;
        }
        for (uint32_t index = 1; index < orderedCount; ++index) {
            const auto* candidate = ordered[index];
            uint32_t insertAt = index;
            while (insertAt > 0 && ordered[insertAt - 1]->breakpointId > candidate->breakpointId) {
                ordered[insertAt] = ordered[insertAt - 1];
                --insertAt;
            }
            ordered[insertAt] = candidate;
        }
        for (uint32_t index = 0; index < orderedCount; ++index)
            snapshot->breakpoints[snapshot->breakpointCount++] = *ordered[index];
    }

    gx_development_debug_breakpoint* find(uint64_t id) {
        for (auto& record : records) if (record.breakpointId == id) return &record;
        return nullptr;
    }
};

} // namespace

int main() {
    static_assert(GX_DEVELOPMENT_DEBUG_MAX_SOURCE_BREAKPOINTS == 8u, "capacity changed");
    static_assert(GX_DEVELOPMENT_DEBUG_REQUEST_LEGACY_BYTES == 104u, "legacy prefix changed");
    static_assert(GX_DEVELOPMENT_DEBUG_REQUEST_POLICY_BYTES == sizeof(gx_development_debug_request),
                  "full breakpoint policy request must be self-sized");

    Model model;
    uint32_t status = 0;
    const uint64_t first = model.add(0x1000, 9, "src/main.cpp", nullptr, &status);
    assert(status == GX_DEVELOPMENT_DEBUG_BREAKPOINT_STATUS_SUCCESS && first != 0);
    const uint64_t second = model.add(0x2000, 3, "src/helper.cpp", "input - 2", &status);
    assert(status == GX_DEVELOPMENT_DEBUG_BREAKPOINT_STATUS_SUCCESS && second > first);
    assert(model.add(0x2000, 3, "src/helper.cpp", nullptr, &status) == 0);
    assert(status == GX_DEVELOPMENT_DEBUG_BREAKPOINT_STATUS_DUPLICATE);

    for (uint32_t index = 2; index < GX_DEVELOPMENT_DEBUG_MAX_SOURCE_BREAKPOINTS; ++index) {
        assert(model.add(0x3000 + index * 0x100, 10 + index,
                         "src/main.cpp", nullptr, &status) != 0);
        assert(status == GX_DEVELOPMENT_DEBUG_BREAKPOINT_STATUS_SUCCESS);
    }
    assert(model.count == GX_DEVELOPMENT_DEBUG_MAX_SOURCE_BREAKPOINTS);
    assert(model.add(0x9000, 99, "src/main.cpp", nullptr, &status) == 0);
    assert(status == GX_DEVELOPMENT_DEBUG_BREAKPOINT_STATUS_CAPACITY);

    gx_development_debug_snapshot snapshot = {};
    model.list(&snapshot);
    assert(snapshot.breakpointCount == 8);
    assert(snapshot.breakpointCapacity == 8);
    for (uint32_t index = 1; index < snapshot.breakpointCount; ++index)
        assert(snapshot.breakpoints[index - 1].breakpointId < snapshot.breakpoints[index].breakpointId);
    assert(snapshot.breakpoints[1].conditionPresent == 1);

    gx_development_debug_breakpoint* current = model.find(second);
    assert(current != nullptr);
    current->enabled = 0;
    current->installed = 0;
    assert(current->sessionGeneration == model.generation);
    current->enabled = 1;
    current->installed = 1;
    ++current->hitCount;
    ++current->falseHitCount;
    ++current->hitCount;
    ++current->trueHitCount;
    assert(current->hitCount == 2 && current->falseHitCount == 1 && current->trueHitCount == 1);

    std::printf("Native breakpoint manager contract test PASS\n");
    return 0;
}
