//
// Process management implementation
//
// Copyright (c) 2024 guideX
//

#include "include/kernel/process.h"
#include "include/kernel/vga.h"
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace kernel {
namespace process {

// Simple process table (stub for now)
static Process processes[16];
static int process_count = 0;

void init()
{
    process_count = 0;
    kernel::vga::print("[....] Initializing process subsystem...\r");
    kernel::vga::print_colored("[ OK ]", kernel::vga::Color::LightGreen, kernel::vga::Color::Black);
    kernel::vga::print(" Process subsystem initialized\n");
}

pid_t create_init_process(uint64_t entry, uint64_t stack_top, const char* name)
{
    if (process_count >= 16) {
        return 0; // Process table full
    }
    
    Process& proc = processes[process_count];
    proc.pid = process_count + 1;
    proc.state = State::Ready;
    proc.entry_point = entry;
    proc.stack_pointer = stack_top;
    proc.name = name;
    
    process_count++;
    
    kernel::vga::print("[INFO] Created init process: ");
    kernel::vga::print(name);
    kernel::vga::print(" (PID ");
    kernel::vga::print_dec(proc.pid);
    kernel::vga::print(")\n");
    
    return proc.pid;
}

void schedule()
{
    // Stub: In real implementation, this would:
    // 1. Save current process state
    // 2. Pick next ready process
    // 3. Restore its state and jump to it
    
    // For now, just yield CPU
#ifdef _MSC_VER
    __nop(); // MSVC intrinsic - placeholder for __halt()
#else
    asm volatile("hlt"); // GCC/Clang inline assembly
#endif
}

} // namespace process
} // namespace kernel