//
// Small, single-CPU cooperative scheduler for trusted AMD64 kernel work.
// Scheduling occurs only at explicit call boundaries from the desktop loop.
// Timer interrupts never switch contexts.
//

#include "include/kernel/native_scheduler.h"

#include "include/kernel/arch.h"

#if defined(ARCH_AMD64)
extern "C" void gxos_context_switch(uint64_t* oldStackPointer,
                                     uint64_t newStackPointer);
extern "C" void gxos_context_entry_trampoline();
extern "C" char __text_start[];
extern "C" char __text_end[];
#endif

namespace kernel {
namespace native_scheduler {
namespace {

static const uint32_t kInvalidSlot = 0xFFFFFFFFu;
static const uint64_t kLowStackGuard = 0x47584F534C4F5747ull;
static const uint64_t kHighStackGuard = 0x47584F5348494748ull;
static const uint32_t kStackAllocationBytes = Runtime::kStackBytes + 64u;
static const uint32_t kSavedFrameWords = 9u;
static const uint32_t kEntryShadowBytes = 40u;

static uintptr_t align_up_16(uintptr_t value)
{
    return (value + 15u) & ~static_cast<uintptr_t>(15u);
}

static uintptr_t align_down_16(uintptr_t value)
{
    return value & ~static_cast<uintptr_t>(15u);
}

#if defined(ARCH_AMD64)
static bool interrupts_enabled()
{
    uint64_t flags = 0u;
    asm volatile("pushfq; popq %0" : "=r"(flags));
    return (flags & (1ull << 9u)) != 0u;
}

static void halt_forever()
{
    for (;;) kernel::arch::halt();
}
#endif

Runtime s_runtime;

} // namespace

Runtime::Runtime()
    : m_contexts{}, m_rootStackPointer(0u), m_nextRuntimeId(1u),
      m_nextSlot(0u), m_currentSlot(kInvalidSlot), m_initialized(false),
      m_dispatching(false)
#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
      , m_failNextAllocation(false)
#endif
{
    for (uint32_t i = 0u; i < kMaximumContexts; ++i) {
        m_contexts[i].allocation = nullptr;
        m_contexts[i].stackLow = 0u;
        m_contexts[i].stackTop = 0u;
        m_contexts[i].savedStackPointer = 0u;
        m_contexts[i].runtimeId = 0u;
        m_contexts[i].dispatchCount = 0u;
        m_contexts[i].generation = 0u;
        m_contexts[i].configuredStackBytes = 0u;
        m_contexts[i].state = State::Empty;
        m_contexts[i].faultKind = FaultKind::None;
        m_contexts[i].entry = nullptr;
        m_contexts[i].argument = nullptr;
        m_contexts[i].exitCode = 0;
        m_contexts[i].retired = false;
    }
}

Result Runtime::initialize()
{
#if !defined(ARCH_AMD64)
    return Result::Unsupported;
#else
#if defined(__SSE__) || defined(__SSE2__) || defined(__MMX__)
#error "The cooperative AMD64 scheduler requires kernel SIMD/MMX code generation to remain disabled."
#endif
    if (m_initialized) return Result::InvalidState;
    if (!interrupts_enabled()) return Result::UnsafeContext;

    // The AMD64 freestanding entry path does not run C++ .init_array
    // constructors. Treat initialize() as the authoritative BSS-object reset
    // so zero-initialized statics do not leave the current slot at slot 0 or
    // the runtime ID at zero.
    m_rootStackPointer = 0u;
    m_nextRuntimeId = 1u;
    m_nextSlot = 0u;
    m_currentSlot = kInvalidSlot;
    m_dispatching = false;
    for (uint32_t i = 0u; i < kMaximumContexts; ++i) {
        Context& context = m_contexts[i];
        context.allocation = nullptr;
        context.stackLow = 0u;
        context.stackTop = 0u;
        context.savedStackPointer = 0u;
        context.runtimeId = 0u;
        context.dispatchCount = 0u;
        context.generation = 0u;
        context.configuredStackBytes = 0u;
        context.state = State::Empty;
        context.faultKind = FaultKind::None;
        context.entry = nullptr;
        context.argument = nullptr;
        context.exitCode = 0;
        context.retired = false;
    }
#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
    m_failNextAllocation = false;
#endif
    m_initialized = true;
    return Result::Ok;
#endif
}

int32_t Runtime::selectReusableSlot() const
{
    for (uint32_t offset = 0u; offset < kMaximumContexts; ++offset) {
        const uint32_t slot = (m_nextSlot + offset) % kMaximumContexts;
        const Context& context = m_contexts[slot];
        if (context.retired || context.allocation != nullptr) continue;
        if (context.state == State::Empty || context.state == State::Completed ||
            context.state == State::Faulted) {
            if (context.generation != UINT32_MAX) return static_cast<int32_t>(slot);
        }
    }
    return -1;
}

bool Runtime::validHandle(Handle handle) const
{
    if (handle.slot >= kMaximumContexts || handle.runtimeId == 0u ||
        handle.generation == 0u) return false;
    const Context& context = m_contexts[handle.slot];
    return context.runtimeId == handle.runtimeId &&
           context.generation == handle.generation &&
           context.state != State::Empty;
}

bool Runtime::validSavedStack(const Context& context) const
{
#if !defined(ARCH_AMD64)
    (void)context;
    return false;
#else
    const uintptr_t saved = static_cast<uintptr_t>(context.savedStackPointer);
    const uintptr_t frameBytes = static_cast<uintptr_t>(kSavedFrameWords) * sizeof(uint64_t);
    if (context.allocation == nullptr || context.stackLow == 0u ||
        context.stackTop <= context.stackLow || (context.stackTop & 0xFu) != 0u ||
        (saved & 0x7u) != 0u || saved < context.stackLow ||
        saved > context.stackTop || frameBytes > context.stackTop - saved) {
        return false;
    }
    const uint64_t instruction = *reinterpret_cast<const uint64_t*>(
        saved + (kSavedFrameWords - 1u) * sizeof(uint64_t));
    const uintptr_t textStart = reinterpret_cast<uintptr_t>(__text_start);
    const uintptr_t textEnd = reinterpret_cast<uintptr_t>(__text_end);
    return textStart != 0u && textEnd > textStart && instruction >= textStart &&
           instruction < textEnd;
#endif
}

bool Runtime::intactStackGuards(const Context& context) const
{
    if (context.allocation == nullptr || context.stackLow < sizeof(uint64_t) ||
        context.stackTop == 0u) return false;
    const uint64_t* lowGuard = reinterpret_cast<const uint64_t*>(context.stackLow - sizeof(uint64_t));
    const uint64_t* highGuard = reinterpret_cast<const uint64_t*>(context.stackTop);
    return *lowGuard == kLowStackGuard && *highGuard == kHighStackGuard;
}

Result Runtime::createKernelTask(Entry entry, void* argument, Handle* output)
{
#if !defined(ARCH_AMD64)
    (void)entry;
    (void)argument;
    (void)output;
    return Result::Unsupported;
#else
    if (!output || !entry) return Result::InvalidArgument;
    if (!m_initialized) return Result::NotInitialized;
    if (m_currentSlot != kInvalidSlot || m_dispatching || !interrupts_enabled())
        return Result::UnsafeContext;
    if (m_nextRuntimeId == 0u) return Result::CapacityExhausted;

    const int32_t selected = selectReusableSlot();
    if (selected < 0) return Result::CapacityExhausted;

#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
    if (m_failNextAllocation) {
        m_failNextAllocation = false;
        return Result::AllocationFailed;
    }
#endif
    uint8_t* allocation = new uint8_t[kStackAllocationBytes];
    if (allocation == nullptr) return Result::AllocationFailed;

    const uintptr_t raw = reinterpret_cast<uintptr_t>(allocation);
    const uintptr_t alignedStackBase = align_up_16(raw);
    const uintptr_t lowGuardAddress = alignedStackBase + sizeof(uint64_t);
    const uintptr_t stackLow = lowGuardAddress + sizeof(uint64_t);
    const uintptr_t stackTop = stackLow + kStackBytes;
    const uintptr_t frameBytes = static_cast<uintptr_t>(kSavedFrameWords) * sizeof(uint64_t);
    const uintptr_t frameAndShadow = frameBytes + kEntryShadowBytes;
    if (stackTop <= stackLow || stackTop + sizeof(uint64_t) >
        raw + kStackAllocationBytes ||
        stackTop - stackLow < frameAndShadow) {
        delete[] allocation;
        return Result::AllocationFailed;
    }

    uintptr_t frameAddress = align_down_16(stackTop - frameAndShadow);
    if (frameAddress < stackLow || frameAddress + frameBytes > stackTop) {
        delete[] allocation;
        return Result::AllocationFailed;
    }

    *reinterpret_cast<uint64_t*>(lowGuardAddress) = kLowStackGuard;
    *reinterpret_cast<uint64_t*>(stackTop) = kHighStackGuard;

    // gxos_context_switch pops this synthetic frame and returns to the entry
    // trampoline. R12 carries a per-context pointer; there is no shared
    // pending-entry global, so contexts can be created independently.
    uint64_t* frame = reinterpret_cast<uint64_t*>(frameAddress);
    frame[0] = 0u; // R15
    frame[1] = 0u; // R14
    frame[2] = 0u; // R13
    frame[3] = 0u; // R12, set after the context object is committed
    frame[4] = 0u; // RDI
    frame[5] = 0u; // RSI
    frame[6] = 0u; // RBP
    frame[7] = 0u; // RBX
    frame[8] = reinterpret_cast<uint64_t>(&gxos_context_entry_trampoline);

    Context& context = m_contexts[static_cast<uint32_t>(selected)];
    const uint32_t generation = context.generation + 1u;
    const uint64_t runtimeId = m_nextRuntimeId;
    context.allocation = allocation;
    context.stackLow = stackLow;
    context.stackTop = stackTop;
    context.savedStackPointer = frameAddress;
    context.runtimeId = runtimeId;
    context.dispatchCount = 0u;
    context.generation = generation;
    context.configuredStackBytes = static_cast<uint32_t>(stackTop - stackLow);
    context.state = State::Ready;
    context.faultKind = FaultKind::None;
    context.entry = entry;
    context.argument = argument;
    context.exitCode = 0;
    context.retired = false;
    frame[3] = reinterpret_cast<uint64_t>(&context);

    ++m_nextRuntimeId;
    m_nextSlot = (static_cast<uint32_t>(selected) + 1u) % kMaximumContexts;
    output->runtimeId = runtimeId;
    output->slot = static_cast<uint32_t>(selected);
    output->generation = generation;
    return Result::Ok;
#endif
}

void Runtime::releaseCompletedStack(Context& context)
{
    if (context.allocation != nullptr) {
        delete[] context.allocation;
        context.allocation = nullptr;
    }
    context.stackLow = 0u;
    context.stackTop = 0u;
    context.savedStackPointer = 0u;
    context.entry = nullptr;
    context.argument = nullptr;
}

Result Runtime::dispatchOne(bool* dispatched)
{
#if !defined(ARCH_AMD64)
    if (dispatched) *dispatched = false;
    return Result::Unsupported;
#else
    if (!dispatched) return Result::InvalidArgument;
    *dispatched = false;
    if (!m_initialized) return Result::NotInitialized;
    if (m_currentSlot != kInvalidSlot || m_dispatching || !interrupts_enabled())
        return Result::UnsafeContext;

    uint32_t selected = kInvalidSlot;
    for (uint32_t offset = 0u; offset < kMaximumContexts; ++offset) {
        const uint32_t slot = (m_nextSlot + offset) % kMaximumContexts;
        if (m_contexts[slot].state == State::Ready) {
            selected = slot;
            break;
        }
    }
    if (selected == kInvalidSlot) return Result::Ok;

    Context& context = m_contexts[selected];
    if (!intactStackGuards(context)) {
        context.state = State::Faulted;
        context.faultKind = FaultKind::StackGuardCorruption;
        releaseCompletedStack(context);
        return Result::InvalidState;
    }
    if (!validSavedStack(context)) {
        context.state = State::Faulted;
        context.faultKind = FaultKind::SavedContextInvalid;
        releaseCompletedStack(context);
        return Result::InvalidState;
    }

    context.state = State::Running;
    if (context.dispatchCount != UINT64_MAX) ++context.dispatchCount;
    m_currentSlot = selected;
    m_nextSlot = (selected + 1u) % kMaximumContexts;
    m_dispatching = true;
    *dispatched = true;

    gxos_context_switch(&m_rootStackPointer, context.savedStackPointer);

    m_dispatching = false;
    if (m_currentSlot != kInvalidSlot) {
        m_currentSlot = kInvalidSlot;
        context.state = State::Faulted;
        context.faultKind = FaultKind::SchedulerInvariant;
        releaseCompletedStack(context);
        return Result::InvalidState;
    }

    if (context.state == State::Ready) {
        if (!intactStackGuards(context)) {
            context.state = State::Faulted;
            context.faultKind = FaultKind::StackGuardCorruption;
            releaseCompletedStack(context);
            return Result::InvalidState;
        }
        if (!validSavedStack(context)) {
            context.state = State::Faulted;
            context.faultKind = FaultKind::SavedContextInvalid;
            releaseCompletedStack(context);
            return Result::InvalidState;
        }
    } else if (context.state == State::Completed) {
        if (!intactStackGuards(context)) {
            context.state = State::Faulted;
            context.faultKind = FaultKind::StackGuardCorruption;
            releaseCompletedStack(context);
            return Result::InvalidState;
        }
        if (!validSavedStack(context)) {
            context.state = State::Faulted;
            context.faultKind = FaultKind::SavedContextInvalid;
            releaseCompletedStack(context);
            return Result::InvalidState;
        }
        // Reap only after the switch has returned to the root stack.
        releaseCompletedStack(context);
    } else {
        context.state = State::Faulted;
        context.faultKind = FaultKind::SchedulerInvariant;
        releaseCompletedStack(context);
        return Result::InvalidState;
    }
    return Result::Ok;
#endif
}

Result Runtime::yieldCurrent()
{
#if !defined(ARCH_AMD64)
    return Result::Unsupported;
#else
    if (!m_initialized) return Result::NotInitialized;
    if (m_currentSlot == kInvalidSlot || !m_dispatching) return Result::InvalidState;
    if (!interrupts_enabled()) return Result::UnsafeContext;

    const uint32_t slot = m_currentSlot;
    Context& context = m_contexts[slot];
    if (context.state != State::Running) return Result::InvalidState;
    context.state = State::Ready;
    m_currentSlot = kInvalidSlot;
    gxos_context_switch(&context.savedStackPointer, m_rootStackPointer);

    if (m_currentSlot != slot || context.state != State::Running)
        return Result::InvalidState;
    return Result::Ok;
#endif
}

void Runtime::finishCurrent(Context& context, int32_t exitCode)
{
#if defined(ARCH_AMD64)
    // Returning to the root context with IF clear would silently disable the
    // desktop's timer/input interrupts. Such a task violated the cooperative
    // entry contract, so fail closed instead of transferring that state.
    if (!interrupts_enabled()) halt_forever();
    context.exitCode = exitCode;
    context.state = State::Completed;
    m_currentSlot = kInvalidSlot;
    gxos_context_switch(&context.savedStackPointer, m_rootStackPointer);
    halt_forever();
#else
    (void)context;
    (void)exitCode;
#endif
}

void Runtime::enterFromTrampoline(void* opaque)
{
#if defined(ARCH_AMD64)
    Context* selected = static_cast<Context*>(opaque);
    if (!selected || m_currentSlot >= kMaximumContexts ||
        selected != &m_contexts[m_currentSlot] || !m_dispatching ||
        selected->state != State::Running || !selected->entry) {
        halt_forever();
    }
    Entry entry = selected->entry;
    void* argument = selected->argument;
    const int32_t exitCode = entry(argument);
    finishCurrent(*selected, exitCode);
#else
    (void)opaque;
#endif
}

bool Runtime::query(Handle handle, Snapshot* output) const
{
    if (!output || !validHandle(handle)) return false;
    const Context& context = m_contexts[handle.slot];
    output->handle = handle;
    output->state = context.state;
    output->ownerKind = OwnerKind::TrustedKernelSystem;
    output->faultKind = context.faultKind;
    output->configuredStackBytes = context.configuredStackBytes;
    output->dispatchCount = context.dispatchCount;
    output->exitCode = context.exitCode;
    output->stackAllocated = context.allocation != nullptr;
    output->hasApplicationIdentity = false;
    output->hasSessionAttribution = false;
    return true;
}

bool Runtime::stacksAreSeparate(Handle first, Handle second) const
{
    if (!validHandle(first) || !validHandle(second) || first.slot == second.slot)
        return false;
    const Context& left = m_contexts[first.slot];
    const Context& right = m_contexts[second.slot];
    if (!left.allocation || !right.allocation) return false;
    return left.stackTop <= right.stackLow || right.stackTop <= left.stackLow;
}

Result Runtime::requestTermination(Handle handle) const
{
    if (!validHandle(handle)) return Result::InvalidHandle;
    return Result::Unsupported;
}

#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
void Runtime::failNextAllocationForTest()
{
    m_failNextAllocation = true;
}

bool Runtime::corruptStackGuardForTest(Handle handle)
{
    if (!validHandle(handle)) return false;
    Context& context = m_contexts[handle.slot];
    if (!context.allocation || !intactStackGuards(context)) return false;
    *reinterpret_cast<uint64_t*>(context.stackLow - sizeof(uint64_t)) = 0u;
    return true;
}
#endif

Runtime& runtime()
{
    return s_runtime;
}

const char* resultName(Result result)
{
    switch (result) {
        case Result::Ok: return "ok";
        case Result::InvalidArgument: return "invalid-argument";
        case Result::NotInitialized: return "not-initialized";
        case Result::CapacityExhausted: return "capacity-exhausted";
        case Result::AllocationFailed: return "allocation-failed";
        case Result::InvalidHandle: return "invalid-handle";
        case Result::InvalidState: return "invalid-state";
        case Result::UnsafeContext: return "unsafe-context";
        case Result::Unsupported: return "unsupported";
        default: return "unknown";
    }
}

const char* stateName(State state)
{
    switch (state) {
        case State::Empty: return "Empty";
        case State::Ready: return "Ready";
        case State::Running: return "Running";
        case State::Completed: return "Completed";
        case State::Faulted: return "Faulted";
        default: return "Unknown";
    }
}

} // namespace native_scheduler
} // namespace kernel

extern "C" void gxos_scheduler_entry(void* context)
{
    kernel::native_scheduler::runtime().enterFromTrampoline(context);
}
