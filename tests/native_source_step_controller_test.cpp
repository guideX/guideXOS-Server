#include "kernel/core/native_elf/native_elf_source_step.h"

#include <cstring>
#include <iostream>

using kernel::native_elf::SourceStepLocation;
using kernel::native_elf::SourceStepObservation;

namespace {

SourceStepLocation location(const char* path, const char* function,
                            uint32_t line, uint32_t column = 0)
{
    SourceStepLocation result = {};
    result.mapped = path && function && path[0] != '\0' && function[0] != '\0';
    result.line = line;
    result.column = column;
    if (path) std::strncpy(result.sourcePath, path, sizeof(result.sourcePath) - 1);
    if (function) std::strncpy(result.functionName, function, sizeof(result.functionName) - 1);
    return result;
}

bool require(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "native_source_step_controller_test FAIL: " << message << "\n";
    return false;
}

} // namespace

int main()
{
    const SourceStepLocation start = location("src/main.cpp", "gx_main", 10, 4);
    const SourceStepLocation sameLine = location("src/main.cpp", "gx_main", 10, 4);
    const SourceStepLocation sameLineDifferentSpan = location("src/main.cpp", "gx_main", 10, 0);
    const SourceStepLocation nextLine = location("src/main.cpp", "gx_main", 11, 4);
    const SourceStepLocation helper = location("src/helper.cpp", "helper", 4, 1);
    const SourceStepLocation branch = location("src/main.cpp", "gx_main", 13, 1);
    SourceStepLocation unmapped = {};

    if (!require(kernel::native_elf::source_step_start_valid(start),
                 "mapped paused source identity is accepted")) return 1;
    if (!require(!kernel::native_elf::source_step_start_valid(unmapped),
                 "unmapped paused source identity is rejected")) return 1;

    uint32_t count = 0;
    SourceStepObservation observation = kernel::native_elf::source_step_observe(
        start, sameLine, count, 128, true, false, &count);
    if (!require(observation == SourceStepObservation::Continue && count == 1,
                 "same source mapping continues after one instruction")) return 1;
    observation = kernel::native_elf::source_step_observe(
        start, sameLineDifferentSpan, count, 128, true, false, &count);
    if (!require(observation == SourceStepObservation::Continue && count == 2,
                 "same line with a non-meaningful column remains one source location")) return 1;
    observation = kernel::native_elf::source_step_observe(
        start, nextLine, count, 128, true, false, &count);
    if (!require(observation == SourceStepObservation::Completed && count == 3,
                 "new line completes source step with internal count")) return 1;

    count = 0;
    observation = kernel::native_elf::source_step_observe(
        start, helper, count, 128, true, false, &count);
    if (!require(observation == SourceStepObservation::Completed && count == 1,
                 "different function and file complete source step")) return 1;
    count = 1;
    observation = kernel::native_elf::source_step_observe(
        start, branch, count, 128, true, false, &count);
    if (!require(observation == SourceStepObservation::Completed && count == 2,
                 "actual branch mapping completes source step")) return 1;
    count = 0;
    observation = kernel::native_elf::source_step_observe(
        start, unmapped, count, 128, true, false, &count);
    if (!require(observation == SourceStepObservation::Continue && count == 1,
                 "unmapped compiler gap keeps source step active")) return 1;

    count = 127;
    observation = kernel::native_elf::source_step_observe(
        start, sameLine, count, 128, true, false, &count);
    if (!require(observation == SourceStepObservation::Limit && count == 128,
                 "source step stops at finite instruction bound")) return 1;
    observation = kernel::native_elf::source_step_observe(
        start, sameLine, 0, 128, false, false, &count);
    if (!require(observation == SourceStepObservation::TargetCompleted,
                 "target completion is not reported as source success")) return 1;
    observation = kernel::native_elf::source_step_observe(
        start, sameLine, 0, 128, true, true, &count);
    if (!require(observation == SourceStepObservation::TargetFailed,
                 "target failure is not reported as source success")) return 1;

    std::cout << "native_source_step_controller_test: PASS\n";
    return 0;
}
