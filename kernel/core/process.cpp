//
// Process and native scheduler-owned thread management for guideXOS kernel.
//
// The process table remains intentionally small.  This file now also owns the
// single-CPU runnable queue and the adapter from the runtime-neutral wait
// queue to architecture context switching.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/process.h"
#include "include/kernel/vga.h"
#include "include/kernel/arch.h"
#include "include/kernel/pit.h"

#if defined(ARCH_AMD64)
#include <arch/context_switch.h>
#endif

namespace kernel {
namespace process {

static Process processes[16];
static int process_count = 0;

#if defined(ARCH_AMD64)
namespace {
    constexpr uint32_t kMaxThreads = 16;
    constexpr uint32_t kKernelStackBytes = 8192;

    struct KernelThread {
        tid_t tid;
        pid_t owner;
        const char* name;
        ThreadState state;
        bool live;
        bool ready_linked;
        KernelThread* ready_previous;
        KernelThread* ready_next;
        alignas(16) uint8_t stack[kKernelStackBytes];
        arch::context::ArchThreadData architecture;
        gxos::runtime::scheduler_wait::WaitNode wait;
    };

    KernelThread threads[kMaxThreads] = {};
    KernelThread* ready_head = nullptr;
    KernelThread* ready_tail = nullptr;
    KernelThread* current = nullptr;
    tid_t next_tid = 1;
    uint32_t critical_depth = 0;
    bool initialized = false;

    void reset_wait_node(KernelThread& thread) {
        thread.wait = gxos::runtime::scheduler_wait::WaitNode{};
        thread.wait.owner_thread = &thread;
    }

    void enqueue_ready(KernelThread* thread) {
        if (thread == nullptr || !thread->live ||
            thread->state != ThreadState::Runnable ||
            thread->ready_linked) {
            return;
        }
        thread->ready_previous = ready_tail;
        thread->ready_next = nullptr;
        thread->ready_linked = true;
        if (ready_tail != nullptr) {
            ready_tail->ready_next = thread;
        }
        else {
            ready_head = thread;
        }
        ready_tail = thread;
    }

    void remove_ready(KernelThread* thread) {
        if (thread == nullptr || !thread->ready_linked) {
            return;
        }
        if (thread->ready_previous != nullptr) {
            thread->ready_previous->ready_next = thread->ready_next;
        }
        else {
            ready_head = thread->ready_next;
        }
        if (thread->ready_next != nullptr) {
            thread->ready_next->ready_previous = thread->ready_previous;
        }
        else {
            ready_tail = thread->ready_previous;
        }
        thread->ready_previous = nullptr;
        thread->ready_next = nullptr;
        thread->ready_linked = false;
    }

    KernelThread* pop_ready() {
        KernelThread* thread = ready_head;
        if (thread != nullptr) {
            remove_ready(thread);
        }
        return thread;
    }

    KernelThread* find_thread(tid_t tid) {
        for (uint32_t i = 0; i < kMaxThreads; ++i) {
            if (threads[i].live && threads[i].tid == tid) {
                return &threads[i];
            }
        }
        return nullptr;
    }

    void* wait_enter_critical(void*) {
        if (critical_depth == 0) {
            arch::disable_interrupts();
        }
        ++critical_depth;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(critical_depth));
    }

    void wait_leave_critical(void*, void*) {
        if (critical_depth == 0) {
            return;
        }
        --critical_depth;
        if (critical_depth == 0) {
            arch::enable_interrupts();
        }
    }

    gxos::runtime::scheduler_wait::WaitNode* wait_current_node(void*) {
        return current == nullptr ? nullptr : &current->wait;
    }

    uint64_t wait_now_ticks(void*) {
        return pit::ticks();
    }

    uint64_t wait_duration_to_ticks(void*, uint64_t nanoseconds, bool* valid) {
        return pit::ticks_for_nanoseconds(nanoseconds, valid);
    }

    void wait_make_runnable(void*, gxos::runtime::scheduler_wait::WaitNode* node) {
        if (node == nullptr || node->owner_thread == nullptr) {
            return;
        }
        KernelThread* thread = static_cast<KernelThread*>(node->owner_thread);
        if (!thread->live || thread->state == ThreadState::Terminated) {
            return;
        }

        // A completion that wins before the park callback runs belongs to the
        // still-running current thread.  Do not insert it twice.  The idle
        // path below observes the completed node and resumes it directly.
        if (thread == current) {
            if (thread->state == ThreadState::Blocked ||
                thread->state == ThreadState::TimedWait) {
                thread->state = ThreadState::Runnable;
            }
            return;
        }

        thread->state = ThreadState::Runnable;
        enqueue_ready(thread);
    }

    gxos::runtime::scheduler_wait::WakeReason wait_park(
        void*, gxos::runtime::scheduler_wait::WaitNode* node) {
        if (current == nullptr || node == nullptr ||
            node->owner_thread != current) {
            return gxos::runtime::scheduler_wait::WakeReason::Interrupted;
        }

        void* token = wait_enter_critical(nullptr);
        if (node->state != gxos::runtime::scheduler_wait::WaitNodeState::Waiting) {
            wait_leave_critical(nullptr, token);
            return node->reason;
        }

        KernelThread* blocked = current;
        blocked->state = node->timed ? ThreadState::TimedWait : ThreadState::Blocked;
        remove_ready(blocked);
        KernelThread* next = pop_ready();

        if (next == nullptr) {
            // There is no separate idle TCB yet.  Use the existing HLT idle
            // behavior while interrupts remain enabled; PIT processing or a
            // signal marks this same thread runnable and completes its node.
            while (node->state == gxos::runtime::scheduler_wait::WaitNodeState::Waiting) {
                arch::enable_interrupts();
                arch::halt();
                arch::disable_interrupts();
            }
            blocked->state = ThreadState::Running;
            wait_leave_critical(nullptr, token);
            return node->reason;
        }

        next->state = ThreadState::Running;
        current = next;
        wait_leave_critical(nullptr, token);
        arch::context::arch_switch_to(&blocked->architecture, &next->architecture);

        // The old stack resumes only after another runnable thread selects it.
        blocked->state = ThreadState::Running;
        return node->state == gxos::runtime::scheduler_wait::WaitNodeState::Completed
            ? node->reason
            : gxos::runtime::scheduler_wait::WakeReason::Interrupted;
    }

    void install_wait_hooks() {
        const gxos::runtime::scheduler_wait::SchedulerWaitHooks hooks = {
            nullptr,
            wait_enter_critical,
            wait_leave_critical,
            wait_current_node,
            wait_now_ticks,
            wait_duration_to_ticks,
            wait_park,
            wait_make_runnable
        };
        gxos::runtime::scheduler_wait::installSchedulerWaitHooks(&hooks);
    }
}
#endif

void init()
{
    process_count = 0;

#if defined(ARCH_AMD64)
    ready_head = nullptr;
    ready_tail = nullptr;
    current = &threads[0];
    next_tid = 1;
    critical_depth = 0;
    initialized = true;

    for (uint32_t i = 0; i < kMaxThreads; ++i) {
        threads[i] = KernelThread{};
        reset_wait_node(threads[i]);
    }

    current = &threads[0];
    current->tid = next_tid++;
    current->owner = 0;
    current->name = "kernel-bootstrap";
    current->state = ThreadState::Running;
    current->live = true;
    install_wait_hooks();
#else
    gxos::runtime::scheduler_wait::installSchedulerWaitHooks(nullptr);
#endif
}

pid_t create_init_process(uint64_t entry, uint64_t stack_top, const char* name)
{
    if (process_count >= 16) {
        return 0;
    }

    Process& proc = processes[process_count];
    proc.pid = process_count + 1;
    proc.state = State::Ready;
    proc.entry_point = entry;
    proc.stack_pointer = stack_top;
    proc.name = name;

    ++process_count;

    kernel::vga::print("[INFO] Created init process: ");
    kernel::vga::print(name);
    kernel::vga::print(" (PID ");
    kernel::vga::print_dec(proc.pid);
    kernel::vga::print(")\n");

    return proc.pid;
}

void schedule()
{
#if defined(ARCH_AMD64)
    if (!initialized || current == nullptr) {
        arch::halt();
        return;
    }

    void* token = wait_enter_critical(nullptr);
    KernelThread* previous = current;
    if (previous->state == ThreadState::Running) {
        previous->state = ThreadState::Runnable;
        enqueue_ready(previous);
    }
    KernelThread* next = pop_ready();
    if (next == nullptr) {
        previous->state = ThreadState::Running;
        wait_leave_critical(nullptr, token);
        return;
    }
    next->state = ThreadState::Running;
    current = next;
    wait_leave_critical(nullptr, token);
    arch::context::arch_switch_to(&previous->architecture, &next->architecture);
#else
    arch::halt();
#endif
}

tid_t create_thread(pid_t owner, void (*entry)(void*), void* arg, const char* name)
{
#if defined(ARCH_AMD64)
    if (!initialized || entry == nullptr) {
        return 0;
    }

    void* token = wait_enter_critical(nullptr);
    KernelThread* slot = nullptr;
    for (uint32_t i = 0; i < kMaxThreads; ++i) {
        if (!threads[i].live) {
            slot = &threads[i];
            break;
        }
    }
    if (slot == nullptr) {
        wait_leave_critical(nullptr, token);
        return 0;
    }

    *slot = KernelThread{};
    reset_wait_node(*slot);
    slot->tid = next_tid++;
    slot->owner = owner;
    slot->name = name;
    slot->state = ThreadState::Runnable;
    slot->live = true;
    arch::context::arch_thread_create(
        &slot->architecture,
        reinterpret_cast<uint64_t>(slot->stack + kKernelStackBytes),
        entry,
        arg);
    enqueue_ready(slot);
    const tid_t result = slot->tid;
    wait_leave_critical(nullptr, token);
    return result;
#else
    (void)owner;
    (void)entry;
    (void)arg;
    (void)name;
    return 0;
#endif
}

bool terminate_thread(tid_t tid)
{
#if defined(ARCH_AMD64)
    KernelThread* thread = find_thread(tid);
    if (thread == nullptr) return false;

    void* token = wait_enter_critical(nullptr);
    gxos::runtime::scheduler_wait::abandonWait(&thread->wait);
    remove_ready(thread);
    thread->state = ThreadState::Terminated;
    thread->live = false;

    if (thread != current) {
        wait_leave_critical(nullptr, token);
        return true;
    }

    KernelThread* next = pop_ready();
    if (next == nullptr) {
        wait_leave_critical(nullptr, token);
        return true;
    }
    next->state = ThreadState::Running;
    current = next;
    wait_leave_critical(nullptr, token);
    arch::context::arch_switch_to(&thread->architecture, &next->architecture);
    return true;
#else
    (void)tid;
    return false;
#endif
}

uint32_t terminate_process_threads(pid_t owner)
{
#if defined(ARCH_AMD64)
    void* token = wait_enter_critical(nullptr);
    KernelThread* current_match = nullptr;
    uint32_t terminated = 0;
    for (uint32_t i = 0; i < kMaxThreads; ++i) {
        KernelThread* thread = &threads[i];
        if (!thread->live || thread->owner != owner) continue;
        gxos::runtime::scheduler_wait::abandonWait(&thread->wait);
        remove_ready(thread);
        thread->state = ThreadState::Terminated;
        thread->live = false;
        ++terminated;
        if (thread == current) current_match = thread;
    }

    if (current_match == nullptr) {
        wait_leave_critical(nullptr, token);
        return terminated;
    }

    KernelThread* next = pop_ready();
    if (next == nullptr) {
        wait_leave_critical(nullptr, token);
        for (;;) {
            arch::enable_interrupts();
            arch::halt();
            arch::disable_interrupts();
        }
    }
    next->state = ThreadState::Running;
    current = next;
    wait_leave_critical(nullptr, token);
    arch::context::arch_switch_to(&current_match->architecture, &next->architecture);
    return terminated;
#else
    (void)owner;
    return 0;
#endif
}

tid_t current_thread_id()
{
#if defined(ARCH_AMD64)
    return current == nullptr ? 0 : current->tid;
#else
    return 0;
#endif
}

void timer_tick()
{
#if defined(ARCH_AMD64)
    if (initialized) {
        // Bounded interrupt-context work; the deadline-ordered list avoids
        // touching future timers once the first one is not yet due.
        gxos::runtime::scheduler_wait::processExpired(pit::ticks(), 8);
    }
#endif
}

} // namespace process
} // namespace kernel

#if defined(ARCH_AMD64)
extern "C" void kernel_thread_exit()
{
    (void)kernel::process::terminate_thread(kernel::process::current_thread_id());
    for (;;) {
        kernel::arch::disable_interrupts();
        kernel::arch::halt();
    }
}
#endif
