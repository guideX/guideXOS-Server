//
// Small, single-CPU scheduler for trusted AMD64 kernel work and controlled
// Native ELF execution. Most contexts remain cooperative; an explicitly
// eligible Native ELF task may be switched out by the outermost PIT IRQ.
//

#include "include/kernel/native_scheduler.h"

#include "include/kernel/arch.h"

#if defined(ARCH_AMD64)
extern "C" void gxos_context_switch(uint64_t* oldStackPointer,
                                     uint64_t newStackPointer,
                                     void* oldXstate, const void* newXstate);
extern "C" void gxos_context_switch_from_timer(uint64_t* oldStackPointer,
                                                uint64_t newStackPointer,
                                                void* oldXstate,
                                                const void* newXstate);
extern "C" void gxos_context_entry_trampoline();
extern "C" char __text_start[];
extern "C" char __text_end[];
#endif

extern "C" {
// Read by the switch assembly after initialize() configures XCR0.
uint64_t gxos_native_scheduler_xstate_mask = 0u;
}

namespace kernel {
namespace native_scheduler {
namespace {

static const uint32_t kInvalidSlot = 0xFFFFFFFFu;
static const uint64_t kLowStackGuard = 0x47584F534C4F5747ull;
static const uint64_t kHighStackGuard = 0x47584F5348494748ull;
static const uint32_t kStackAllocationBytes = Runtime::kStackBytes + 64u;
static const uint32_t kSavedFrameWords = 9u;
static const uint32_t kEntryShadowBytes = 40u;
static uint32_t s_enabledXstateBytes = 0u;
static const uint32_t kMaximumPreemptionDisableDepth = 32u;

static void increment_saturated(uint64_t* value)
{
    if (value && *value != UINT64_MAX) ++*value;
}

static uintptr_t align_up_16(uintptr_t value)
{
    return (value + 15u) & ~static_cast<uintptr_t>(15u);
}

static uintptr_t align_down_16(uintptr_t value)
{
    return value & ~static_cast<uintptr_t>(15u);
}

#if defined(ARCH_AMD64)
static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax,
                  uint32_t* ebx, uint32_t* ecx, uint32_t* edx)
{
    uint32_t a = leaf;
    uint32_t b = 0u;
    uint32_t c = subleaf;
    uint32_t d = 0u;
    asm volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static uint64_t read_xcr0()
{
    uint32_t low = 0u;
    uint32_t high = 0u;
    asm volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0u));
    return (static_cast<uint64_t>(high) << 32u) | low;
}

static void write_xcr0(uint64_t value)
{
    asm volatile("xsetbv" : : "c"(0u), "a"(static_cast<uint32_t>(value)),
                 "d"(static_cast<uint32_t>(value >> 32u)) : "memory");
}

static uint64_t read_cr0()
{
    uint64_t value = 0u;
    asm volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static void write_cr0(uint64_t value)
{
    asm volatile("mov %0, %%cr0" : : "r"(value) : "memory");
}

static uint64_t read_cr4()
{
    uint64_t value = 0u;
    asm volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

static void write_cr4(uint64_t value)
{
    asm volatile("mov %0, %%cr4" : : "r"(value) : "memory");
}

static bool initialize_xstate()
{
    uint32_t maxLeaf = 0u;
    cpuid(0u, 0u, &maxLeaf, nullptr, nullptr, nullptr);
    if (maxLeaf < 0xDu) return false;

    uint32_t featureEcx = 0u;
    cpuid(1u, 0u, nullptr, nullptr, &featureEcx, nullptr);
    if ((featureEcx & (1u << 26u)) == 0u) return false; // XSAVE
    const bool avxSupported = (featureEcx & (1u << 28u)) != 0u;

    uint32_t supportedLow = 0u;
    uint32_t supportedHigh = 0u;
    cpuid(0xDu, 0u, &supportedLow, nullptr, nullptr, &supportedHigh);
    const uint64_t supportedMask = (static_cast<uint64_t>(supportedHigh) << 32u) |
                                   supportedLow;
    if ((supportedMask & 0x3u) != 0x3u) return false;

    uint64_t flags = 0u;
    asm volatile("pushfq; popq %0" : "=r"(flags));
    asm volatile("cli" : : : "memory");

    uint64_t cr0 = read_cr0();
    const uint64_t originalCr0 = cr0;
    cr0 |= (1ull << 1u);       // MP
    cr0 &= ~((1ull << 2u) |    // EM
             (1ull << 3u));    // TS
    write_cr0(cr0);

    const uint64_t cr4 = read_cr4();
    const bool osxsaveWasEnabled = (cr4 & (1ull << 18u)) != 0u;
    write_cr4(cr4 | (1ull << 9u) | (1ull << 10u) | (1ull << 18u));

    const uint64_t originalXcr0 = read_xcr0();
    uint64_t xcr0 = osxsaveWasEnabled ? originalXcr0 : 0x3u;
    xcr0 |= 0x3u; // x87 and SSE are the minimum application state contract.
    if (avxSupported && (supportedMask & (1ull << 2u)) != 0u)
        xcr0 |= 1ull << 2u;
    if ((xcr0 & ~supportedMask) != 0u) {
        write_xcr0(originalXcr0);
        write_cr4(cr4);
        write_cr0(originalCr0);
        if (flags & (1ull << 9u)) asm volatile("sti" : : : "memory");
        return false;
    }
    write_xcr0(xcr0);

    uint32_t areaBytes = 0u;
    cpuid(0xDu, 0u, nullptr, &areaBytes, nullptr, nullptr);
    if (areaBytes < 576u || areaBytes > Runtime::kMaximumXstateBytes) {
        write_xcr0(originalXcr0);
        write_cr4(cr4);
        write_cr0(originalCr0);
        if (flags & (1ull << 9u)) asm volatile("sti" : : : "memory");
        return false;
    }
    gxos_native_scheduler_xstate_mask = xcr0;
    s_enabledXstateBytes = areaBytes;
    (void)osxsaveWasEnabled;
    if (flags & (1ull << 9u)) asm volatile("sti" : : : "memory");
    return true;
}

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
      m_dispatching(false), m_transition(false), m_preemptionPending(false),
      m_preemptionDisableDepth(0u), m_preemptionStats{}
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
        m_contexts[i].ownerKind = OwnerKind::TrustedKernelSystem;
        m_contexts[i].state = State::Empty;
        m_contexts[i].faultKind = FaultKind::None;
        m_contexts[i].entry = nullptr;
        m_contexts[i].argument = nullptr;
        m_contexts[i].exitCode = 0;
        m_contexts[i].retired = false;
        m_contexts[i].savedFromTimerInterrupt = false;
        m_contexts[i].preemptionEligible = false;
        m_contexts[i].suspendedPreemptionDisableDepth = 0u;
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
    if (!initialize_xstate()) return Result::Unsupported;

    // The AMD64 freestanding entry path does not run C++ .init_array
    // constructors. Treat initialize() as the authoritative BSS-object reset
    // so zero-initialized statics do not leave the current slot at slot 0 or
    // the runtime ID at zero.
    m_rootStackPointer = 0u;
    m_nextRuntimeId = 1u;
    m_nextSlot = 0u;
    m_currentSlot = kInvalidSlot;
    m_dispatching = false;
    m_transition = false;
    m_preemptionPending = false;
    m_preemptionDisableDepth = 0u;
    m_preemptionStats = {};
    for (uint32_t i = 0u; i < sizeof(m_rootXstate); ++i) m_rootXstate[i] = 0u;
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
        context.ownerKind = OwnerKind::TrustedKernelSystem;
        context.state = State::Empty;
        context.faultKind = FaultKind::None;
        context.entry = nullptr;
        context.argument = nullptr;
        context.exitCode = 0;
        context.retired = false;
        context.savedFromTimerInterrupt = false;
        context.preemptionEligible = false;
        context.suspendedPreemptionDisableDepth = 0u;
        for (uint32_t j = 0u; j < sizeof(context.xstate); ++j) context.xstate[j] = 0u;
    }
#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
    m_failNextAllocation = false;
#endif
    m_initialized = true;
    return Result::Ok;
#endif
}

int32_t Runtime::selectReusableSlot(bool nativeElfApplication,
                                    bool allKernelSlots) const
{
    uint32_t firstSlot = 0u;
    uint32_t slotCount = kKernelTaskSlots;
#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
    if (!nativeElfApplication && allKernelSlots) slotCount = kMaximumContexts;
#else
    (void)allKernelSlots;
#endif
    if (nativeElfApplication) {
        firstSlot = kKernelTaskSlots;
        slotCount = kNativeElfTaskSlots;
    }
    const uint32_t startSlot = m_nextSlot % kMaximumContexts;
    for (uint32_t offset = 0u; offset < kMaximumContexts; ++offset) {
        const uint32_t slot = (startSlot + offset) % kMaximumContexts;
        if (slot < firstSlot || slot >= firstSlot + slotCount) continue;
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
    return createTask(entry, argument, output, false, false);
}

Result Runtime::createNativeElfTask(Entry entry, void* argument, Handle* output)
{
    return createTask(entry, argument, output, true, false);
}

#if defined(GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE)
Result Runtime::createSelfTestTask(Entry entry, void* argument, Handle* output)
{
    return createTask(entry, argument, output, false, true);
}
#endif

Result Runtime::createTask(Entry entry, void* argument, Handle* output,
                           bool nativeElfApplication, bool allKernelSlots)
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

    const int32_t selected = selectReusableSlot(nativeElfApplication,
                                                allKernelSlots);
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
    context.ownerKind = nativeElfApplication ? OwnerKind::NativeElfApplication
                                             : OwnerKind::TrustedKernelSystem;
    context.state = State::Ready;
    context.faultKind = FaultKind::None;
    context.entry = entry;
    context.argument = argument;
    context.exitCode = 0;
    context.retired = false;
    context.savedFromTimerInterrupt = false;
    context.preemptionEligible = false;
    context.suspendedPreemptionDisableDepth = 0u;
    for (uint32_t i = 0u; i < sizeof(context.xstate); ++i) context.xstate[i] = 0u;
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

    // The timer may arrive at any instruction, including while the root is
    // handing off the stack. Mark this short interval so the IRQ can count
    // the tick without treating the root's stack as the selected task.
    asm volatile("cli" : : : "memory");
    m_transition = true;
    context.state = State::Running;
    if (context.dispatchCount != UINT64_MAX) ++context.dispatchCount;
    m_currentSlot = selected;
    m_nextSlot = (selected + 1u) % kMaximumContexts;
    m_dispatching = true;
    *dispatched = true;

    if (context.savedFromTimerInterrupt) {
        // Keep IRQs masked until the suspended handler reaches iretq, which
        // restores the interrupted Native ELF RFLAGS/IF exactly.
        asm volatile("cli" : : : "memory");
    } else {
        asm volatile("sti" : : : "memory");
    }
    gxos_context_switch(&m_rootStackPointer, context.savedStackPointer,
                        m_rootXstate, context.xstate);

    m_transition = false;
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
    if (m_preemptionDisableDepth != 0u) return Result::UnsafeContext;

    const uint32_t slot = m_currentSlot;
    Context& context = m_contexts[slot];
    if (context.state != State::Running) return Result::InvalidState;
    asm volatile("cli" : : : "memory");
    m_transition = true;
    context.state = State::Ready;
    m_currentSlot = kInvalidSlot;
    asm volatile("sti" : : : "memory");
    gxos_context_switch(&context.savedStackPointer, m_rootStackPointer,
                        context.xstate, m_rootXstate);

    if (context.suspendedPreemptionDisableDepth != 0u) {
        if (m_preemptionDisableDepth != 0u) return Result::InvalidState;
        m_preemptionDisableDepth = context.suspendedPreemptionDisableDepth;
        context.suspendedPreemptionDisableDepth = 0u;
    }
    m_transition = false;
    if (m_currentSlot != slot || context.state != State::Running)
        return Result::InvalidState;
    return Result::Ok;
#endif
}

Result Runtime::switchCurrentToRoot(bool timerInterrupt)
{
#if !defined(ARCH_AMD64)
    (void)timerInterrupt;
    return Result::Unsupported;
#else
    if (!m_initialized || m_currentSlot >= kMaximumContexts || !m_dispatching)
        return Result::InvalidState;
    const uint32_t slot = m_currentSlot;
    Context& context = m_contexts[slot];
    if (context.ownerKind != OwnerKind::NativeElfApplication ||
        context.state != State::Running || !m_rootStackPointer ||
        !context.preemptionEligible || m_transition ||
        context.suspendedPreemptionDisableDepth != 0u) return Result::UnsafeContext;

    if (!timerInterrupt) {
        if (!interrupts_enabled()) return Result::UnsafeContext;
        asm volatile("cli" : : : "memory");
        // A timer can consume the pending request after the caller's check
        // but before this function masks interrupts. Recheck atomically so a
        // resumed safe-boundary path cannot perform a redundant second switch.
        if (!m_preemptionPending) {
            asm volatile("sti" : : : "memory");
            return Result::Ok;
        }
    }
    m_transition = true;
    context.state = State::Ready;
    context.savedFromTimerInterrupt = timerInterrupt;
    m_currentSlot = kInvalidSlot;
    m_preemptionPending = false;
    increment_saturated(&m_preemptionStats.preemptiveSwitches);

    if (timerInterrupt) {
        gxos_context_switch_from_timer(&context.savedStackPointer,
            m_rootStackPointer, context.xstate, m_rootXstate);
        // This call returns only when the parked IRQ frame has been selected
        // again and is about to unwind through iretq. Future switches from
        // this context may be cooperative and must use the normal IF path.
        context.savedFromTimerInterrupt = false;
    } else {
        asm volatile("sti" : : : "memory");
        gxos_context_switch(&context.savedStackPointer, m_rootStackPointer,
            context.xstate, m_rootXstate);
    }

    // Reached only after the root selected this context again. For a timer
    // continuation, IF is still clear here and iretq restores the saved flags.
    if (m_currentSlot != slot || context.state != State::Running)
        return Result::InvalidState;
    context.savedFromTimerInterrupt = false;
    m_transition = false;
    increment_saturated(&m_preemptionStats.applicationResumptions);
    return Result::Ok;
#endif
}

void Runtime::timerInterrupt(bool outermostInterrupt)
{
#if defined(ARCH_AMD64)
    if (!m_initialized) return;
    increment_saturated(&m_preemptionStats.timerInterrupts);

    // A tick on the desktop/root stack, during scheduler handoff, or in the
    // trusted service task is a heartbeat only. Those classes are not yet
    // eligible for timer preemption.
    if (!outermostInterrupt || m_transition || m_currentSlot >= kMaximumContexts ||
        !m_dispatching) {
        if (m_currentSlot < kMaximumContexts && m_dispatching)
            increment_saturated(&m_preemptionStats.eligibilityRejections);
        return;
    }

    Context& context = m_contexts[m_currentSlot];
    if (context.ownerKind != OwnerKind::NativeElfApplication ||
        context.state != State::Running || interrupts_enabled()) {
        increment_saturated(&m_preemptionStats.eligibilityRejections);
        return;
    }
    if (!context.preemptionEligible) {
        increment_saturated(&m_preemptionStats.eligibilityRejections);
        return;
    }

    increment_saturated(&m_preemptionStats.preemptionDecisions);
    if (m_preemptionDisableDepth != 0u) {
        if (!m_preemptionPending)
            increment_saturated(&m_preemptionStats.deferredPreemptions);
        m_preemptionPending = true;
        return;
    }

    (void)switchCurrentToRoot(true);
#else
    (void)outermostInterrupt;
#endif
}

Result Runtime::enterNonPreemptible()
{
#if !defined(ARCH_AMD64)
    return Result::Unsupported;
#else
    if (!m_initialized) return Result::NotInitialized;
    if (m_currentSlot >= kMaximumContexts || !m_dispatching || m_transition ||
        m_contexts[m_currentSlot].ownerKind != OwnerKind::NativeElfApplication ||
        m_contexts[m_currentSlot].state != State::Running) return Result::UnsafeContext;
    if (m_preemptionDisableDepth >= kMaximumPreemptionDisableDepth)
        return Result::UnsafeContext;
    ++m_preemptionDisableDepth;
    return Result::Ok;
#endif
}

Result Runtime::leaveNonPreemptible()
{
#if !defined(ARCH_AMD64)
    return Result::Unsupported;
#else
    if (m_currentSlot >= kMaximumContexts || !m_dispatching || m_transition ||
        m_contexts[m_currentSlot].ownerKind != OwnerKind::NativeElfApplication ||
        m_contexts[m_currentSlot].state != State::Running ||
        m_preemptionDisableDepth == 0u) return Result::InvalidState;
    --m_preemptionDisableDepth;
    if (m_preemptionDisableDepth == 0u && m_preemptionPending)
        return serviceDeferredPreemption();
    return Result::Ok;
#endif
}

Result Runtime::suspendPreemptionForCooperativeYield()
{
#if !defined(ARCH_AMD64)
    return Result::Unsupported;
#else
    if (m_currentSlot >= kMaximumContexts || !m_dispatching || m_transition ||
        m_contexts[m_currentSlot].ownerKind != OwnerKind::NativeElfApplication ||
        m_contexts[m_currentSlot].state != State::Running ||
        m_preemptionDisableDepth == 0u) return Result::InvalidState;
    Context& context = m_contexts[m_currentSlot];
    if (context.suspendedPreemptionDisableDepth != 0u) return Result::InvalidState;
    context.suspendedPreemptionDisableDepth = m_preemptionDisableDepth;
    m_preemptionDisableDepth = 0u;
    // The explicit voluntary yield already transfers control to the root;
    // do not later count the same pending timer request as timer preemption.
    m_preemptionPending = false;
    return Result::Ok;
#endif
}

Result Runtime::cancelPreemptionSuspendForCooperativeYield()
{
#if !defined(ARCH_AMD64)
    return Result::Unsupported;
#else
    if (m_currentSlot >= kMaximumContexts || !m_dispatching || m_transition ||
        m_preemptionDisableDepth != 0u ||
        m_contexts[m_currentSlot].ownerKind != OwnerKind::NativeElfApplication ||
        m_contexts[m_currentSlot].state != State::Running ||
        m_contexts[m_currentSlot].suspendedPreemptionDisableDepth == 0u)
        return Result::InvalidState;
    Context& context = m_contexts[m_currentSlot];
    m_preemptionDisableDepth = context.suspendedPreemptionDisableDepth;
    context.suspendedPreemptionDisableDepth = 0u;
    return Result::Ok;
#endif
}

Result Runtime::setCurrentPreemptionEligible(bool eligible)
{
#if !defined(ARCH_AMD64)
    (void)eligible;
    return Result::Unsupported;
#else
    if (m_currentSlot >= kMaximumContexts || !m_dispatching || m_transition ||
        m_contexts[m_currentSlot].ownerKind != OwnerKind::NativeElfApplication ||
        m_contexts[m_currentSlot].state != State::Running) return Result::UnsafeContext;
    if (eligible && m_preemptionDisableDepth == 0u) return Result::UnsafeContext;
    m_contexts[m_currentSlot].preemptionEligible = eligible;
    // Ticks deferred during setup of the pilot predate its measured CPU work;
    // only requests arriving after eligibility is established can switch it.
    m_preemptionPending = false;
    return Result::Ok;
#endif
}

Result Runtime::serviceDeferredPreemption()
{
#if defined(ARCH_AMD64)
    if (!m_preemptionPending) return Result::Ok;
    if (m_currentSlot >= kMaximumContexts ||
        !m_contexts[m_currentSlot].preemptionEligible) {
        m_preemptionPending = false;
        return Result::Ok;
    }
    if (!interrupts_enabled()) return Result::UnsafeContext;
    return switchCurrentToRoot(false);
#else
    return Result::Unsupported;
#endif
}

void Runtime::finishCurrent(Context& context, int32_t exitCode)
{
#if defined(ARCH_AMD64)
    // Returning to the root context with IF clear would silently disable the
    // desktop's timer/input interrupts. Such a task violated the cooperative
    // entry contract, so fail closed instead of transferring that state.
    if (!interrupts_enabled() || m_preemptionDisableDepth != 0u ||
        context.suspendedPreemptionDisableDepth != 0u) halt_forever();
    asm volatile("cli" : : : "memory");
    context.exitCode = exitCode;
    context.state = State::Completed;
    m_transition = true;
    m_currentSlot = kInvalidSlot;
    asm volatile("sti" : : : "memory");
    gxos_context_switch(&context.savedStackPointer, m_rootStackPointer,
                        context.xstate, m_rootXstate);
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
    // Initial tasks cannot be preempted until their complete entry contract
    // has been established. The trampoline is the first safe task boundary.
    m_transition = false;
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
    output->ownerKind = context.ownerKind;
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

bool Runtime::executionStackBounds(Handle handle, uintptr_t* low,
                                  uintptr_t* top) const
{
    if (!low || !top || !validHandle(handle)) return false;
    const Context& context = m_contexts[handle.slot];
    if (!context.allocation || context.stackTop <= context.stackLow) return false;
    *low = context.stackLow;
    *top = context.stackTop;
    return true;
}

bool Runtime::isCurrent(Handle handle) const
{
    return validHandle(handle) && m_dispatching &&
           m_currentSlot == handle.slot &&
           m_contexts[handle.slot].state == State::Running;
}

uint64_t Runtime::enabledXstateMask() const
{
    return gxos_native_scheduler_xstate_mask;
}

uint32_t Runtime::enabledXstateBytes() const
{
    return s_enabledXstateBytes;
}

uint64_t Runtime::trustedKernelDispatchCount() const
{
    uint64_t total = 0u;
    for (uint32_t i = 0u; i < kMaximumContexts; ++i) {
        const Context& context = m_contexts[i];
        if (context.ownerKind != OwnerKind::TrustedKernelSystem) continue;
        if (UINT64_MAX - total < context.dispatchCount) return UINT64_MAX;
        total += context.dispatchCount;
    }
    return total;
}

PreemptionStats Runtime::preemptionStats() const
{
    return m_preemptionStats;
}

uint32_t Runtime::preemptionDisableDepth() const
{
    return m_preemptionDisableDepth;
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
