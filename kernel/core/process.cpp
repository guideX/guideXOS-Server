//
// Process and native scheduler-owned thread management for guideXOS kernel.
//
// The process table remains intentionally small.  This file now also owns the
// single-CPU runnable queue and the adapter from the runtime-neutral wait
// queue to architecture context switching.  Synchronization adapters use
// the same scheduler-owned thread generation as their opaque owner token;
// the mutex probe keeps that claim opaque to the scheduler during validation.
//
// Copyright (c) 2026 guideXOS Server
//

#include "include/kernel/process.h"
#include "include/kernel/vga.h"
#include "include/kernel/arch.h"
#include "include/kernel/pit.h"
#include "include/kernel/serial_debug.h"
#include "runtime/synchronization/guidexos_mutex.h"
#include "runtime/thread/guidexos_native_thread.h"

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
    constexpr uint32_t kKernelStackBytes =
        static_cast<uint32_t>(gxos::runtime::kNativeThreadMaximumStackSize);

    struct KernelThread {
        tid_t tid;
        pid_t owner;
        const char* name;
        ThreadState state;
        bool allocated;
        bool ready_linked;
        bool native_thread;
        bool detached;
        bool join_consumed;
        bool exit_signaled;
        bool exit_result_valid;
        bool teardown;
        bool deferred_reclaim;
        bool native_started;
        uint32_t generation;
        gxos::runtime::NativeThreadEntry native_entry;
        void* native_context;
        uintptr_t exit_result;
        uint32_t requested_stack_size;
        uint8_t* stack_base;
        uint8_t* stack_limit;
        uintptr_t initial_stack_pointer;
        uintptr_t initial_instruction_pointer;
        uintptr_t initial_rbx;
        uintptr_t initial_r12;
        uintptr_t initial_r13;
        uintptr_t initial_r14;
        uintptr_t initial_r15;
        KernelThread* ready_previous;
        KernelThread* ready_next;
        alignas(16) uint8_t stack[kKernelStackBytes];
        arch::context::ArchThreadData architecture;
        gxos::runtime::scheduler_wait::WaitNode wait;
        gxos::runtime::Event* completion;
    };

    KernelThread threads[kMaxThreads] = {};
    gxos::runtime::Event completion_events[kMaxThreads];
    KernelThread* ready_head = nullptr;
    KernelThread* ready_tail = nullptr;
    KernelThread* current = nullptr;
    tid_t next_tid = 1;
    uint32_t critical_depth = 0;
    bool initialized = false;

    uint32_t slot_index(const KernelThread* thread) {
        return thread == nullptr ? kMaxThreads
            : static_cast<uint32_t>(thread - threads);
    }

    void reset_wait_node(KernelThread& thread) {
        thread.wait = gxos::runtime::scheduler_wait::WaitNode{};
        thread.wait.owner_thread = &thread;
    }

    void reset_slot(KernelThread& thread, uint32_t index) {
        const uint32_t generation = thread.generation == 0 ? 1 : thread.generation;
        gxos::runtime::Event* completion = &completion_events[index];
        thread = KernelThread{};
        thread.generation = generation;
        thread.completion = completion;
        thread.stack_base = thread.stack;
        thread.stack_limit = thread.stack + kKernelStackBytes;
        reset_wait_node(thread);
        (void)completion->reset();
    }

    void zero_stack(KernelThread& thread) {
        for (uint32_t i = 0; i < kKernelStackBytes; ++i) {
            thread.stack[i] = 0;
        }
    }

    void enqueue_ready(KernelThread* thread) {
        if (thread == nullptr || !thread->allocated ||
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

    // These checks are compiled only for invariant-focused kernel runs.  The
    // normal image does not pay for production logging or assertion work.
    void check_thread_invariants() {
#if defined(GXOS_THREAD_DEBUG_ASSERTS)
        for (uint32_t i = 0; i < kMaxThreads; ++i) {
            KernelThread& thread = threads[i];
            if (thread.generation == 0 ||
                (thread.ready_linked &&
                 (!thread.allocated || thread.state != ThreadState::Runnable ||
                  &thread == current)) ||
                (thread.state == ThreadState::Terminated && thread.ready_linked) ||
                (thread.allocated &&
                 (thread.stack_base != thread.stack ||
                  thread.stack_limit < thread.stack_base ||
                  thread.stack_limit > thread.stack + kKernelStackBytes))) {
                __builtin_trap();
            }
        }
        if (current != nullptr &&
            (!current->allocated || current->state != ThreadState::Running ||
             current->ready_linked)) {
            __builtin_trap();
        }
#endif
    }

    KernelThread* find_thread(tid_t tid) {
        for (uint32_t i = 0; i < kMaxThreads; ++i) {
            if (threads[i].allocated && threads[i].tid == tid) {
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
        if (!thread->allocated || thread->state == ThreadState::Terminated) {
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
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
        serial::puts("[native-thread-test] wake runnable slot=");
        serial::put_hex32(slot_index(thread));
        serial::puts(" state=");
        serial::put_hex32(static_cast<uint32_t>(thread->state));
        serial::puts(" ready=");
        serial::put_hex32(thread->ready_linked ? 1U : 0U);
        serial::putc('\n');
#endif
    }

#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
    void trace_first_schedule(KernelThread* thread) {
        if (thread == nullptr || !thread->native_thread || thread->native_started) {
            return;
        }
        serial::puts("[native-thread-test] first schedule tid=");
        serial::put_hex64(thread->tid);
        serial::puts(" slot=");
        serial::put_hex32(slot_index(thread));
        serial::puts(" generation=");
        serial::put_hex32(thread->generation);
        serial::puts(" stack_low=");
        serial::put_hex64(reinterpret_cast<uintptr_t>(thread->stack_base));
        serial::puts(" stack_high=");
        serial::put_hex64(reinterpret_cast<uintptr_t>(thread->stack_limit));
        serial::puts(" initial_ip=");
        serial::put_hex64(thread->initial_instruction_pointer);
        serial::puts(" initial_sp=");
        serial::put_hex64(thread->initial_stack_pointer);
        serial::putc('\n');
    }
#else
    void trace_first_schedule(KernelThread*) {}
#endif

    void reap_detached_locked();

    gxos::runtime::scheduler_wait::WakeReason wait_park(
        void*, gxos::runtime::scheduler_wait::WaitNode* node) {
        if (current == nullptr || node == nullptr ||
            node->owner_thread != current) {
            return gxos::runtime::scheduler_wait::WakeReason::Interrupted;
        }

        for (;;) {
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
                // There is no separate idle TCB yet.  Use the existing HLT
                // behavior while interrupts remain enabled.  A timer or
                // signal may make a different thread runnable while this
                // node is still waiting; yield to that thread instead of
                // sleeping until the current node's much later deadline.
                while (node->state == gxos::runtime::scheduler_wait::WaitNodeState::Waiting &&
                       ready_head == nullptr) {
                    arch::enable_interrupts();
                    arch::halt();
                    arch::disable_interrupts();
                }
                if (node->state != gxos::runtime::scheduler_wait::WaitNodeState::Waiting) {
                    blocked->state = ThreadState::Running;
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
                    serial::puts("[native-thread-test] wait resume idle slot=");
                    serial::put_hex32(slot_index(blocked));
                    serial::puts(" reason=");
                    serial::put_hex32(static_cast<uint32_t>(node->reason));
                    serial::putc('\n');
#endif
                    wait_leave_critical(nullptr, token);
                    return node->reason;
                }
                next = pop_ready();
            }

            if (next == nullptr) {
                blocked->state = ThreadState::Running;
                wait_leave_critical(nullptr, token);
                return gxos::runtime::scheduler_wait::WakeReason::Interrupted;
            }

            trace_first_schedule(next);
            next->state = ThreadState::Running;
            current = next;
            wait_leave_critical(nullptr, token);
            arch::context::arch_switch_to(&blocked->architecture, &next->architecture);

            // The old stack resumes only after another runnable thread
            // selects it, normally after this node has completed.
            blocked->state = ThreadState::Running;
            void* resume_token = wait_enter_critical(nullptr);
            reap_detached_locked();
            wait_leave_critical(nullptr, resume_token);
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
            serial::puts("[native-thread-test] wait resume switch slot=");
            serial::put_hex32(slot_index(blocked));
            serial::puts(" state=");
            serial::put_hex32(static_cast<uint32_t>(node->state));
            serial::putc('\n');
#endif
            if (node->state == gxos::runtime::scheduler_wait::WaitNodeState::Completed) {
                return node->reason;
            }
        }
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

    gxos::runtime::MutexOwnerIdentity mutex_current_owner(void*) {
        return current == nullptr
            ? gxos::runtime::MutexOwnerIdentity{ 0, 0 }
            : gxos::runtime::MutexOwnerIdentity{ current->tid, current->generation };
    }

    void install_mutex_hooks() {
        const gxos::runtime::MutexPlatformHooks hooks = {
            nullptr,
            mutex_current_owner
        };
        gxos::runtime::installMutexPlatformHooks(&hooks);
    }

    KernelThread* lookup_handle(const gxos::runtime::ThreadHandle& handle) {
        if (!handle.isValid() || handle.slot >= kMaxThreads) {
            return nullptr;
        }
        KernelThread& thread = threads[handle.slot];
        if (!thread.allocated || thread.generation != handle.generation ||
            !thread.native_thread) {
            return nullptr;
        }
        return &thread;
    }

    void reclaim_slot(KernelThread& thread) {
        if (!thread.allocated || &thread == current || thread.ready_linked ||
            thread.state == ThreadState::Running ||
            thread.state == ThreadState::Runnable ||
            thread.state == ThreadState::Blocked ||
            thread.state == ThreadState::TimedWait) {
            return;
        }

        const uint32_t index = slot_index(&thread);
        if (index >= kMaxThreads) {
            return;
        }
        arch::context::arch_thread_destroy(&thread.architecture);
        zero_stack(thread);
        if (thread.generation != 0xFFFFFFFFu) {
            ++thread.generation;
        }
        thread.allocated = false;
        thread.state = ThreadState::Terminated;
        thread.deferred_reclaim = false;
        thread.join_consumed = false;
        thread.completion = &completion_events[index];
        (void)thread.completion->reset();
        reset_wait_node(thread);
    }

    void reap_detached_locked() {
        for (uint32_t i = 0; i < kMaxThreads; ++i) {
            KernelThread& thread = threads[i];
            if (thread.allocated && thread.detached &&
                thread.deferred_reclaim && &thread != current) {
                reclaim_slot(thread);
            }
        }
    }

    void native_entry_dispatch(void* argument) {
        KernelThread* thread = static_cast<KernelThread*>(argument);
        if (thread == nullptr || !thread->native_thread ||
            thread->native_entry == nullptr) {
            return;
        }
        thread->native_started = true;
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
        serial::puts("[native-thread-test] entry invocation tid=");
        serial::put_hex64(thread->tid);
        serial::puts(" slot=");
        serial::put_hex32(slot_index(thread));
        serial::puts(" generation=");
        serial::put_hex32(thread->generation);
        serial::puts(" context=");
        serial::put_hex64(reinterpret_cast<uintptr_t>(thread->native_context));
        serial::putc('\n');
#endif
        thread->exit_result = thread->native_entry(thread->native_context);
        thread->exit_result_valid = true;
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
        serial::puts("[native-thread-test] exit result=");
        serial::put_hex64(thread->exit_result);
        serial::putc('\n');
#endif
    }

    void exit_current_thread() {
        if (current == nullptr) {
            return;
        }

        KernelThread* exiting = current;
        void* token = wait_enter_critical(nullptr);
        gxos::runtime::scheduler_wait::abandonWait(&exiting->wait);
        remove_ready(exiting);
        exiting->state = ThreadState::Terminated;

        if (exiting->native_thread && !exiting->exit_signaled) {
            exiting->exit_signaled = true;
            if (exiting->completion != nullptr) {
                // The completion Event owns the wake-one operation.  The
                // result is stored before signal so a resumed joiner sees a
                // complete exit record.
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
                serial::puts("[native-thread-test] completion signal slot=");
                serial::put_hex32(slot_index(exiting));
                serial::putc('\n');
#endif
                (void)exiting->completion->signal();
            }
        }
        if (exiting->detached) {
            // The current stack remains active until the switch below has
            // completed.  A later scheduler operation reclaims this slot.
            exiting->deferred_reclaim = true;
        }

        reap_detached_locked();
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
        check_thread_invariants();
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
        serial::puts("[native-thread-test] exit switch next slot=");
        serial::put_hex32(slot_index(next));
        serial::puts(" context=");
        serial::put_hex64(reinterpret_cast<uintptr_t>(next->architecture.context));
        serial::puts(" rip=");
        serial::put_hex64(next->architecture.context == nullptr ? 0 : next->architecture.context->rip);
        serial::puts(" rsp=");
        serial::put_hex64(next->architecture.context == nullptr ? 0 : next->architecture.context->rsp);
        serial::putc('\n');
#endif
        wait_leave_critical(nullptr, token);
        arch::context::arch_switch_to(&exiting->architecture, &next->architecture);
    }

    gxos::runtime::ThreadResult native_create_hook(
        void*,
        gxos::runtime::NativeThreadEntry entry,
        void* context,
        const gxos::runtime::ThreadCreateOptions& options,
        gxos::runtime::ThreadHandle* result) {
        if (result == nullptr || entry == nullptr || !initialized) {
            return gxos::runtime::ThreadResult::InvalidArgument;
        }
        *result = gxos::runtime::ThreadHandle{};
        if (options.stackSize < gxos::runtime::kNativeThreadMinimumStackSize ||
            options.stackSize > kKernelStackBytes ||
            (options.stackSize % 16u) != 0) {
            return gxos::runtime::ThreadResult::InvalidStackSize;
        }

        void* token = wait_enter_critical(nullptr);
        reap_detached_locked();
        KernelThread* slot = nullptr;
        uint32_t index = 0;
        for (; index < kMaxThreads; ++index) {
            if (!threads[index].allocated) {
                slot = &threads[index];
                break;
            }
        }
        if (slot == nullptr) {
            wait_leave_critical(nullptr, token);
            return gxos::runtime::ThreadResult::NoResources;
        }

        reset_slot(*slot, index);
        zero_stack(*slot);
        slot->tid = next_tid++;
        slot->owner = current == nullptr ? 0 : current->owner;
        slot->name = options.debugName;
        slot->state = ThreadState::Runnable;
        slot->allocated = true;
        slot->native_thread = true;
        slot->detached = options.detached;
        slot->native_entry = entry;
        slot->native_context = context;
        slot->requested_stack_size = static_cast<uint32_t>(options.stackSize);
        slot->stack_base = slot->stack;
        slot->stack_limit = slot->stack + options.stackSize;
        arch::context::arch_thread_create(
            &slot->architecture,
            reinterpret_cast<uint64_t>(slot->stack_limit),
            native_entry_dispatch,
            slot);
        slot->initial_instruction_pointer = slot->architecture.context == nullptr
            ? 0
            : slot->architecture.context->rip;
        slot->initial_stack_pointer = slot->architecture.context == nullptr
            ? 0
            : reinterpret_cast<uintptr_t>(slot->architecture.context) +
              sizeof(arch::context::SwitchContext);
        slot->initial_rbx = slot->architecture.context == nullptr
            ? 0 : slot->architecture.context->rbx;
        slot->initial_r12 = slot->architecture.context == nullptr
            ? 0 : slot->architecture.context->r12;
        slot->initial_r13 = slot->architecture.context == nullptr
            ? 0 : slot->architecture.context->r13;
        slot->initial_r14 = slot->architecture.context == nullptr
            ? 0 : slot->architecture.context->r14;
        slot->initial_r15 = slot->architecture.context == nullptr
            ? 0 : slot->architecture.context->r15;
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
        serial::puts("[native-thread-test] created tid=");
        serial::put_hex64(slot->tid);
        serial::puts(" slot=");
        serial::put_hex32(index);
        serial::puts(" generation=");
        serial::put_hex32(slot->generation);
        serial::puts(" stack_low=");
        serial::put_hex64(reinterpret_cast<uintptr_t>(slot->stack_base));
        serial::puts(" stack_high=");
        serial::put_hex64(reinterpret_cast<uintptr_t>(slot->stack_limit));
        serial::puts(" initial_ip=");
        serial::put_hex64(slot->initial_instruction_pointer);
        serial::puts(" initial_sp=");
        serial::put_hex64(slot->initial_stack_pointer);
        serial::putc('\n');
#endif
        enqueue_ready(slot);
        *result = gxos::runtime::ThreadHandle{ index, slot->generation };
        check_thread_invariants();
        wait_leave_critical(nullptr, token);
        return gxos::runtime::ThreadResult::Ok;
    }

    gxos::runtime::WaitResult native_join_hook(
        void*,
        gxos::runtime::ThreadHandle handle,
        const gxos::runtime::WaitTimeout& timeout,
        uintptr_t* exitResult) {
        if (exitResult != nullptr) {
            *exitResult = 0;
        }
        void* token = wait_enter_critical(nullptr);
        KernelThread* target = lookup_handle(handle);
        if (target == nullptr || target == current || target->detached ||
            target->join_consumed) {
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
            serial::puts("[native-thread-test] join rejected slot=");
            serial::put_hex32(handle.slot);
            serial::puts(" generation=");
            serial::put_hex32(handle.generation);
            serial::puts(" current_slot=");
            serial::put_hex32(slot_index(current));
            serial::putc('\n');
#endif
            wait_leave_critical(nullptr, token);
            return gxos::runtime::WaitResult::Invalid;
        }
        gxos::runtime::Event* completion = target->completion;
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
        serial::puts("[native-thread-test] join timeout valid=");
        serial::put_hex32(timeout.valid ? 1U : 0U);
        serial::puts(" infinite=");
        serial::put_hex32(timeout.infinite_wait ? 1U : 0U);
        serial::puts(" nanos=");
        serial::put_hex64(timeout.nanoseconds);
        serial::putc('\n');
#endif
        wait_leave_critical(nullptr, token);

        const gxos::runtime::WaitResult waitResult = completion->wait(timeout);
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
        serial::puts("[native-thread-test] join wait result=");
        serial::put_hex32(static_cast<uint32_t>(waitResult));
        serial::putc('\n');
#endif
        if (waitResult != gxos::runtime::WaitResult::Signaled) {
            return waitResult;
        }
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
        serial::puts("[native-thread-test] join wake slot=");
        serial::put_hex32(handle.slot);
        serial::puts(" generation=");
        serial::put_hex32(handle.generation);
        serial::putc('\n');
#endif

        token = wait_enter_critical(nullptr);
        target = lookup_handle(handle);
        if (target == nullptr || target->detached || target->join_consumed) {
            wait_leave_critical(nullptr, token);
            return gxos::runtime::WaitResult::Invalid;
        }
        if (target->teardown) {
            wait_leave_critical(nullptr, token);
            return gxos::runtime::WaitResult::Destroyed;
        }
        if (target->state != ThreadState::Terminated || !target->exit_signaled) {
            wait_leave_critical(nullptr, token);
            return gxos::runtime::WaitResult::Invalid;
        }
        if (exitResult != nullptr) {
            *exitResult = target->exit_result;
        }
        target->join_consumed = true;
        target->deferred_reclaim = true;
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
        serial::puts("[native-thread-test] reclamation slot=");
        serial::put_hex32(handle.slot);
        serial::puts(" generation=");
        serial::put_hex32(handle.generation);
        serial::putc('\n');
#endif
        reclaim_slot(*target);
        check_thread_invariants();
        wait_leave_critical(nullptr, token);
        return gxos::runtime::WaitResult::Signaled;
    }

    gxos::runtime::ThreadResult native_detach_hook(
        void*, gxos::runtime::ThreadHandle handle) {
        void* token = wait_enter_critical(nullptr);
        KernelThread* target = lookup_handle(handle);
        if (target == nullptr) {
            wait_leave_critical(nullptr, token);
            return gxos::runtime::ThreadResult::InvalidHandle;
        }
        if (target->detached) {
            wait_leave_critical(nullptr, token);
            return gxos::runtime::ThreadResult::AlreadyDetached;
        }
        if (target->join_consumed) {
            wait_leave_critical(nullptr, token);
            return gxos::runtime::ThreadResult::AlreadyJoined;
        }
        target->detached = true;
        if (target->state == ThreadState::Terminated) {
            target->deferred_reclaim = true;
            if (target != current) {
#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)
                serial::puts("[native-thread-test] detached reclamation slot=");
                serial::put_hex32(handle.slot);
                serial::puts(" generation=");
                serial::put_hex32(handle.generation);
                serial::putc('\n');
#endif
                reclaim_slot(*target);
            }
        }
        check_thread_invariants();
        wait_leave_critical(nullptr, token);
        return gxos::runtime::ThreadResult::Ok;
    }

    void install_native_thread_hooks() {
        const gxos::runtime::NativeThreadPlatformHooks hooks = {
            nullptr,
            native_create_hook,
            native_join_hook,
            native_detach_hook
        };
        gxos::runtime::installNativeThreadPlatformHooks(&hooks);
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
        if (!completion_events[i].isInitialized()) {
            (void)completion_events[i].initialize(
                gxos::runtime::EventMode::ManualReset, false);
        }
        reset_slot(threads[i], i);
    }

    current = &threads[0];
    current->tid = next_tid++;
    current->owner = 0;
    current->name = "kernel-bootstrap";
    current->state = ThreadState::Running;
    current->allocated = true;
    current->stack_base = current->stack;
    current->stack_limit = current->stack + kKernelStackBytes;
    check_thread_invariants();
    install_wait_hooks();
    install_mutex_hooks();
    install_native_thread_hooks();
#else
    gxos::runtime::scheduler_wait::installSchedulerWaitHooks(nullptr);
    gxos::runtime::installMutexPlatformHooks(nullptr);
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
    reap_detached_locked();
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
    trace_first_schedule(next);
    next->state = ThreadState::Running;
    current = next;
    check_thread_invariants();
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
        if (!threads[i].allocated) {
            slot = &threads[i];
            break;
        }
    }
    if (slot == nullptr) {
        wait_leave_critical(nullptr, token);
        return 0;
    }

    const uint32_t index = static_cast<uint32_t>(slot - threads);
    reset_slot(*slot, index);
    zero_stack(*slot);
    slot->tid = next_tid++;
    slot->owner = owner;
    slot->name = name;
    slot->state = ThreadState::Runnable;
    slot->allocated = true;
    slot->stack_base = slot->stack;
    slot->stack_limit = slot->stack + kKernelStackBytes;
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

    if (thread->native_thread && thread != current) {
        // Native threads have no external kill operation.  Only process
        // teardown may remove a non-current native thread.
        return false;
    }

    if (thread == current) {
        exit_current_thread();
        return true;
    }

    void* token = wait_enter_critical(nullptr);
    gxos::runtime::scheduler_wait::abandonWait(&thread->wait);
    remove_ready(thread);
    thread->state = ThreadState::Terminated;
    reclaim_slot(*thread);
    wait_leave_critical(nullptr, token);
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
        if (!thread->allocated || thread->owner != owner) continue;
        gxos::runtime::scheduler_wait::abandonWait(&thread->wait);
        remove_ready(thread);
        thread->state = ThreadState::Terminated;
        if (thread->native_thread) {
            thread->teardown = true;
            if (!thread->exit_signaled && thread->completion != nullptr) {
                thread->exit_signaled = true;
                (void)thread->completion->signal();
            }
        }
        if (thread != current) {
            reclaim_slot(*thread);
        }
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
    check_thread_invariants();
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

#if defined(GXOS_NATIVE_THREAD_QEMU_TEST)

bool native_thread_test_snapshot_current(NativeThreadTestSnapshot* snapshot) {
    if (snapshot == nullptr || current == nullptr || !current->allocated) {
        return false;
    }
    snapshot->tid = current->tid;
    snapshot->slot = slot_index(current);
    snapshot->generation = current->generation;
    snapshot->stack_base = reinterpret_cast<uintptr_t>(current->stack_base);
    snapshot->stack_limit = reinterpret_cast<uintptr_t>(current->stack_limit);
    snapshot->stack_pointer = arch::context::get_sp();
    snapshot->initial_stack_pointer = current->initial_stack_pointer;
    snapshot->initial_instruction_pointer = current->initial_instruction_pointer;
    snapshot->initial_rbx = current->initial_rbx;
    snapshot->initial_r12 = current->initial_r12;
    snapshot->initial_r13 = current->initial_r13;
    snapshot->initial_r14 = current->initial_r14;
    snapshot->initial_r15 = current->initial_r15;
    snapshot->native_started = current->native_started;
    snapshot->wait_queue_linked = current->wait.queue_linked;
    snapshot->timer_linked = current->wait.timer_linked;
    return true;
}

uint32_t native_thread_test_live_count() {
    void* token = wait_enter_critical(nullptr);
    reap_detached_locked();
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxThreads; ++i) {
        if (threads[i].allocated) {
            ++count;
        }
    }
    wait_leave_critical(nullptr, token);
    return count;
}

bool native_thread_test_slot_matches(uint32_t slot, uint32_t generation) {
    void* token = wait_enter_critical(nullptr);
    const bool matches = slot < kMaxThreads && threads[slot].allocated &&
        threads[slot].generation == generation;
    wait_leave_critical(nullptr, token);
    return matches;
}

void native_thread_test_set_current_owner(pid_t owner) {
    void* token = wait_enter_critical(nullptr);
    if (current != nullptr && current->allocated) {
        current->owner = owner;
    }
    wait_leave_critical(nullptr, token);
}

#endif

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
