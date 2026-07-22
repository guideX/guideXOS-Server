# Runtime-Neutral Native Stack Bounds

Status: implemented and independently validated for hosted Windows threads and
the guideXOS AMD64 bare-metal thread model. This is a facts-only primitive; it
does not start NativeAOT, allocate through the GC, suspend threads, or scan a
stack.

## 1. Purpose

The NativeAOT PAL needs a trustworthy native stack interval for a runtime
thread. The generic API provides that fact without exposing NativeAOT,
ThreadStore, GC mode, reverse-P/Invoke, allocation-context, or exception-frame
types to the generic thread layer.

## 2. Stack-bound contract

`queryCurrentNativeStackBounds` returns `NativeStackBounds` with:

```text
[low, high)    native stack interval
current        current stack pointer sampled by the provider
```

Success requires `low < high` and `low <= current < high`. The provider does
not allocate, grow, mutate, or reserve a stack. Returned values are a snapshot
of the current thread's stack ownership and remain valid while that ownership
remains attached.

## 3. Endpoint semantics

The interval is low-inclusive and high-exclusive. This convention is used by
the generic validator and by the runtime-pack PAL adapter. NativeAOT's matching
Windows implementation is `PalGetMaximumStackBounds` in
`Runtime/windows/PalRedhawkCommon.cpp`: it uses the stack allocation base as
the low address and the TEB `StackBase` as the high address. The source calls
these maximum stack bounds; they are not a guessed range around the current
pointer.

On Windows the native stack grows downward. The low endpoint is page-aligned by
the host allocation contract and the high endpoint is the stack-base boundary.
The returned maximum interval describes the reserved stack extent, which can
include guard/reserved portions and need not mean that every page is currently
committed. Raw memory scanning must therefore still obey the eventual runtime's
stack-map and guard-page rules.

## 4. Initial/bootstrap stack source

The AMD64 boot entry switches to the linker-visible `boot_stack_top` symbol in
`kernel/arch/amd64/boot.asm`. `boot_stack_bottom` and `boot_stack_top` delimit
the exact `.bss` boot-stack storage used by the code that constructs the initial
TCB. The initial TCB is registered with those symbols, not with an unrelated
embedded worker-stack buffer. Its current pointer is sampled from the active
AMD64 context, so the initial RSP check proves that the code is executing on
the published interval.

The boot interval contains only the declared boot stack storage. Interrupt or
emergency stacks are not silently included in this descriptor.

## 5. Worker stack source

GuideXOS-created workers own the TCB's bounded stack allocation. The generic
provider returns the TCB's `stack_limit` as `low` and `stack_base` as `high`,
and samples the current AMD64 RSP. The worker context is built after these
values are established and before the first worker instruction runs. The
existing stack-size policy is retained; the stack-bound tests additionally
require at least 4096 bytes for a worker.

## 6. Hosted implementation

The hosted Windows provider is private to
`runtime/thread/guidexos_native_stack_bounds.cpp`. It queries the memory region
containing a local stack marker with `VirtualQuery`, uses the allocation base
and the current TEB `StackBase`, and samples the current stack pointer. The
public interface remains platform-neutral. If the host cannot produce a
validated interval, the API returns `Unavailable` or a validation failure; it
never substitutes a fixed-size guessed range.

The Windows result follows the matching NativeAOT PAL's maximum-reservation
semantics. A POSIX build, where available, uses the host thread-attribute
stack interval behind the same interface.

## 7. Bare-metal implementation

The bare-metal provider uses an installed generic platform hook. The kernel
hook reads the current guideXOS TCB, returns its exact stack low/high fields,
and obtains RSP from the AMD64 context helper. The hook is installed during
process initialization before native-thread hooks are used. If there is no
current registered thread, the result is `NoCurrentThread`; no fallback range
is fabricated.

## 8. Current-pointer validation

Validation is centralized in the generic implementation and repeated by the
tests and the runtime-pack PAL adapter. Null output is `InvalidOutput`; zero,
reversed, or otherwise malformed intervals are `InvalidBounds`; a current
pointer outside `[low, high)` is `CurrentPointerOutsideBounds`. The worker and
QEMU probes record the actual RSP observed by the provider and check it against
the returned interval.

## 9. Thread-lifetime integration

For workers, bounds are published before entry and remain valid through the
worker's native exit path, including runtime/local-storage detach callbacks and
the inactive ThreadStore detach probe. Native stack reclamation occurs only
after callbacks, runtime-record detach, result publication, completion
signaling, and the normal join/reclamation path. After generic thread state is
unavailable, a query fails honestly rather than returning stale bounds.

## 10. Local-storage detach interaction

The QEMU and hosted tests query the current bounds from a local-storage detach
callback. This proves that callback code still sees the live current TCB and
its stack interval. The callback does not retain the descriptor after detach;
the native worker remains alive until the callback and detach layers finish.

## 11. TCB reuse

Worker teardown invalidates the old runtime/native ownership before a TCB slot
is reused. The reuse test rejects the old state, then verifies that the new
worker receives a valid interval and current pointer. Repeated hosted reuse and
QEMU slot reuse both pass. No old stack bounds are copied into a new runtime
record.

## 12. Test coverage

`runtime/tests/guidexos_native_stack_bounds_tests.cpp` covers initial and worker
queries, ordering, page alignment, minimum worker size, actual RSP validation,
distinct worker stacks, detach-callback visibility, repeated host-slot reuse,
post-detach failure, invalid output, corrupt injected bounds, and an outside
current pointer. `scripts/smoke-native-stack-bounds.ps1` runs the hosted test,
bare-metal compile/build, and the inactive NativeAOT ThreadStore probe.

The opt-in QEMU thread mode additionally reports bootstrap exact-source
validation, initial/worker RSP checks, callback-time validity, invalidation
before reuse, and valid new bounds after reuse. The completed guest artifact
`out/runtime/native-thread-qemu-validation/smoke-20260719-213408/native-thread-serial.log`
ends with `[native-thread-test] ALL_PASS`.

## 13. Known limitations

This primitive does not prove collection-safe suspension, stack hijacking,
precise root enumeration, managed stack maps, or finalizer-stack adequacy. The
hosted Windows interval is a maximum reserved-stack interval rather than a
committed-page bitmap. Bare-metal interrupt-stack ownership and future managed
runtime stack policy remain separate work. The API is current-thread only and
returns no persistent object.

## 14. Future GC scanning implications

The exact interval is the prerequisite for a NativeAOT runtime-thread record,
not a claim that scanning is enabled. A future Workstation GC mode must combine
these bounds with the matching `Thread::GcScanRoots`, stack-map/context, guard,
and suspension contracts. Collection-safe enumeration and suspension remain
blocked and are deliberately not implemented here.
