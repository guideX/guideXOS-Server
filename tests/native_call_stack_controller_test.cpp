#include "kernel/core/native_elf/native_elf_call_stack.h"

#include <iostream>

namespace {

bool require(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "native_call_stack_controller_test FAIL: " << message << "\n";
    return false;
}

} // namespace

int main()
{
    const uint64_t stackLow = 0x7000;
    const uint64_t stackHigh = 0x9000;
    const uint64_t imageBase = 0x10000000;
    const uint64_t imageSize = 0x2000;

    if (!require(kernel::native_elf::native_elf_frame_shape_valid(
                     0x7048, 0x7080, stackLow, stackHigh),
                 "supported framed context is accepted")) return 1;
    if (!require(!kernel::native_elf::native_elf_frame_shape_valid(
                     0x7048, 0x7081, stackLow, stackHigh),
                 "unaligned RBP is rejected")) return 1;
    if (!require(!kernel::native_elf::native_elf_frame_shape_valid(
                     0x7048, 0x8100, stackLow, stackHigh),
                 "oversized frame distance is rejected")) return 1;
    if (!require(!kernel::native_elf::native_elf_frame_pointer_valid(
                     0x8ff8, stackLow, stackHigh),
                 "RBP whose full link is outside the stack is rejected")) return 1;
    if (!require(kernel::native_elf::native_elf_caller_link_valid(
                     0x7080, 0x7100, 0x10000120, stackLow, stackHigh,
                     imageBase, imageSize),
                 "monotonic caller link inside image is accepted")) return 1;
    if (!require(!kernel::native_elf::native_elf_caller_link_valid(
                     0x7080, 0x7100, 0x20000120, stackLow, stackHigh,
                     imageBase, imageSize),
                 "return address outside image is rejected")) return 1;
    if (!require(!kernel::native_elf::native_elf_caller_link_valid(
                     0x7080, 0x7040, 0x10000120, stackLow, stackHigh,
                     imageBase, imageSize),
                 "backward caller link is rejected")) return 1;
    if (!require(!kernel::native_elf::native_elf_caller_link_valid(
                     0x7080, 0x8088, 0x10000120, stackLow, stackHigh,
                     imageBase, imageSize),
                 "huge caller jump is rejected")) return 1;

    uint64_t seen[GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES] = {};
    seen[0] = 0x7080;
    seen[1] = 0x7100;
    if (!require(kernel::native_elf::native_elf_call_stack_frame_pointer_seen(
                     seen, 2, 0x7100),
                 "repeated RBP is detected")) return 1;
    if (!require(!kernel::native_elf::native_elf_call_stack_frame_pointer_seen(
                     seen, 2, 0x7180),
                 "distinct recursive frame locations remain distinct")) return 1;
    if (!require(kernel::native_elf::native_elf_call_stack_can_append(15),
                 "maximum-capacity frame can be appended")) return 1;
    if (!require(!kernel::native_elf::native_elf_call_stack_can_append(16),
                 "maximum-capacity frame cannot overflow the result")) return 1;

    std::cout << "native_call_stack_controller_test: PASS\n";
    return 0;
}
