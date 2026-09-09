//
// Pure source-aware Step Into policy shared by the NativeElf controller and
// deterministic host tests.  It deliberately knows nothing about CPU state;
// the run service supplies one trusted post-#DB source lookup at a time.
//
#pragma once

#include <stdint.h>

namespace kernel {
namespace native_elf {

static const uint32_t kNativeElfSourceStepInstructionLimit = 128U;
static const uint32_t kNativeElfSourceStepMaxPathBytes = 160U;
static const uint32_t kNativeElfSourceStepMaxFunctionBytes = 64U;

struct SourceStepLocation {
    bool mapped;
    uint32_t line;
    uint32_t column;
    char sourcePath[kNativeElfSourceStepMaxPathBytes];
    char functionName[kNativeElfSourceStepMaxFunctionBytes];
};

enum class SourceStepObservation : uint32_t {
    Continue = 0,
    Completed = 1,
    Limit = 2,
    TargetCompleted = 3,
    TargetFailed = 4
};

inline bool source_step_text_equal(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    uint32_t i = 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) return false;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

inline bool source_step_location_equal(const SourceStepLocation& left,
                                       const SourceStepLocation& right)
{
    if (!left.mapped || !right.mapped ||
        !source_step_text_equal(left.sourcePath, right.sourcePath) ||
        !source_step_text_equal(left.functionName, right.functionName) ||
        left.line != right.line) return false;
    // A zero column is the metadata's line-level identity.  Only compare
    // columns when both records carry meaningful column information.
    return left.column == 0 || right.column == 0 || left.column == right.column;
}

inline bool source_step_start_valid(const SourceStepLocation& location)
{
    return location.mapped && location.line != 0 && location.sourcePath[0] != '\0' &&
        location.functionName[0] != '\0';
}

inline bool source_step_over_same_caller_function(
    const SourceStepLocation& starting, const SourceStepLocation& current)
{
    return source_step_start_valid(starting) && source_step_start_valid(current) &&
        source_step_text_equal(starting.sourcePath, current.sourcePath) &&
        source_step_text_equal(starting.functionName, current.functionName);
}

inline SourceStepObservation source_step_observe(
    const SourceStepLocation& starting, const SourceStepLocation& current,
    uint32_t completedInstructions, uint32_t instructionLimit,
    bool targetActive, bool targetFailed, uint32_t* nextCount)
{
    if (nextCount) *nextCount = completedInstructions;
    if (targetFailed) return SourceStepObservation::TargetFailed;
    if (!targetActive) return SourceStepObservation::TargetCompleted;
    if (instructionLimit == 0 || completedInstructions >= instructionLimit)
        return SourceStepObservation::Limit;
    const uint32_t next = completedInstructions + 1U;
    if (nextCount) *nextCount = next;
    if (current.mapped && !source_step_location_equal(starting, current))
        return SourceStepObservation::Completed;
    return next >= instructionLimit
        ? SourceStepObservation::Limit : SourceStepObservation::Continue;
}

} // namespace native_elf
} // namespace kernel
