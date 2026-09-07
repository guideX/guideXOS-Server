// Minimal architecture contract used by the common kernel scheduler.
//
// This header deliberately contains scheduler-facing operations only.  MMU,
// GIC, exception-register, and platform discovery details stay in the
// architecture implementation.

#pragma once

#include <stdint.h>

namespace kernel {
namespace arch {

typedef uint64_t interrupt_state_t;
typedef void (*thread_entry_t)(void*);

// The interrupt frame storage is opaque to common code.  The size is a
// contract limit, not an assertion that every architecture uses all words.
static const uint32_t kInterruptContextWords = 40;

interrupt_state_t irq_save();
void irq_restore(interrupt_state_t state);
void irq_enable();
void irq_disable();
void idle();
void memory_barrier();

const char* interface_name();

// A context is allocated on the owner task's kernel stack.  The returned
// pointer is stable until that task is switched out; a later switch updates
// the pointer to the newly saved frame.
void* context_init(uint64_t stack_base, uint64_t stack_size,
                   thread_entry_t entry, void* argument);
void context_switch(void** old_context, void* new_context);

// Save an exception frame supplied by the architecture vector entry and turn
// a cooperative context into a resumable exception-return context.  Both
// operations are used only while IRQs are masked on the single Phase-3 CPU.
bool interrupt_frame_save(void* frame, void* destination);
bool context_to_interrupt(void* context, void* destination);

} // namespace arch
} // namespace kernel

