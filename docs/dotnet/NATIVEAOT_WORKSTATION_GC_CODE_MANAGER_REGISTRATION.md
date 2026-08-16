# C011EC17 — Production code-manager registration reaches the native-helper boundary (Outcome D)

Date: 2026-08-16

## Result

C011EC17 is classified as Outcome D in the milestone taxonomy. The guideXOS
direct-ELF startup path now performs the real NativeAOT production
code-manager registration before `InitializeModules`, and the registration is
observed exactly once. The locked `CoffNativeCodeManager` exists and the
registered NativeAOT managed-code range is nonzero and checked.

The authentic suspended-thread transition PC is nevertheless
`0x100547F6`. It is in the guideXOS `RhpNewArray` runtime helper, not in the
NativeAOT managed-code bookend range `0x10001000..0x10050950`. Consequently,
`IsManaged(controlPC)` is false and
`GetCodeManagerForAddress(controlPC)` correctly returns null. The prior
C011EC16 null-code-manager boundary therefore remains the first stack-walk
boundary. No range widening, PC special case, fake `ICodeManager`, fake
unwind data, or fake method metadata was added.

This is a production-registration milestone, not a stack-root success.
C011EC16 remains historical Outcome D and is not rewritten.

## Repository boundary

Starting state was verified before implementation:

* branch: `v1.1_DOTNET_SUPPORT`
* HEAD: `fd433056d5a4321a0aa42fd13c9cba1668e72ba3`
* upstream: `origin/v1.1_DOTNET_SUPPORT`
* divergence: ahead `0`, behind `0` (the requested expected ahead `1` was not
  present in the repository)
* tracked worktree: clean
* untracked entries: none
* previous commit: `Isolate NativeAOT stack root transition`

The existing C011EC16 commit was neither amended nor rewritten. The previous
push failure (`Permission denied (publickey)`) was not retried.

## Locked identity and authoritative contract

The proof used the existing locked identity without runtime drift:

* NativeAOT `9.0.0`
* AMD64
* Workstation GC
* runtime interfaces `5.3 / 2`
* NativeAOT source commit
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

The locked upstream contract was traced in:

* `src/coreclr/nativeaot/Bootstrap/main.cpp`: stock startup calls
  `RhInitialize`, obtains the OS module, calls `RhRegisterOSModule` with the
  managed/unboxing bookends and classlib table, then calls `InitializeModules`.
* `src/coreclr/nativeaot/Runtime/windows/CoffNativeCodeManager.cpp`:
  `RhRegisterOSModule` reads the PE DOS/NT headers and exception directory,
  constructs `CoffNativeCodeManager`, and registers it in the
  `RuntimeInstance`.
* `src/coreclr/nativeaot/Runtime/RuntimeInstance.cpp`:
  `RegisterCodeManager` stores the production manager and managed range;
  `IsManaged` and `GetCodeManagerForAddress` validate that range.
* `src/coreclr/nativeaot/Common/src/Internal/Runtime/CompilerHelpers/StartupCodeHelpers.cs`:
  `InitializeModules` calls `RhpRegisterOsModule` and creates the module
  `TypeManager` objects. TypeManager registration is separate from code-manager
  registration.
* `src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp`: the initial frame
  obtains a code manager from the control PC, then calls `FindMethodInfo` and
  calculates frame state. A null lookup fails before method metadata or GCInfo
  processing.

Before C011EC17, guideXOS `initializeNativeAotModules()` in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`
performed the `InitializeModules`/TypeManager path but the direct ELF launch
did not reproduce the stock `RhRegisterOSModule` step. `RhpRegisterOsModule`
only records the OS module; it does not construct or register the production
code manager.

The missing transition was therefore:

`RhpReversePInvoke -> initializeRuntimeState -> RhInitialize (already present) -> RhRegisterOSModule (missing) -> InitializeModules`

The new registration is in `initializeNativeAotModules()` at the normal
runtime-pack startup boundary, before the existing `InitializeModules` call
and before the suspended GC stack walk. It is not lazy and is not called from
`Thread::GcScanRoots` or the proof harness.

## Production implementation

The new path in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`
uses:

* the real loaded module handle returned by
  `PalGetModuleHandleFromPointer(&InitializeModules)`;
* the linker-produced `__managedcode_a`/`__managedcode_z` bookends;
* the linker-produced `__unbox_a`/`__unbox_z` bookends;
* the same 16-entry classlib function table used by the locked bootstrapper;
* the locked runtime's external `RhRegisterOSModule` entry point.

`getNativeAotRange()` rejects null, inverted, zero, and greater-than-32-bit
ranges before registration. A second registration or a failed registration
fails truthfully with the C011EC17 diagnostic tag. The observed concrete
implementation is `CoffNativeCodeManager`.

The PE-to-ELF envelope in
`tools/dotnet/pe_to_elf_v2_fixed_base.py` was also corrected. Its first
fixed-base PT_LOAD now preserves the real PE headers (`MZ`, NT headers, and
data directories) at the loaded image base. The converter retains fileless
section memory and rejects malformed bounds/overflow conditions. The proof
link uses the NativeAOT linker contract `/MERGE:.managedcode=.text` and
`/MERGE:hydrated=.bss`; it does not manufacture managed metadata.

## Loaded-image geometry

The final three-boot build recorded this geometry:

* PE image/module base: `0x10000000`
* ELF fixed-base header PT_LOAD: `0x10000000`, file size `0x400`, memory
  size `0x1000`, read-only
* executable PT_LOAD: `0x10001000..0x100C1A00`
* `__managedcode_a`: `0x10001000`
* `__managedcode_z`: `0x10050950`
* managed-code start: `0x10001000`
* managed-code size: `0x4F950`
* managed-code end: `0x10050950` (checked, exclusive)
* unboxing-stub bookends: `0x100C1460..0x100C19C0`
* exception directory / `.pdata` load: represented by the PE exception data
  directory and loaded at the `.pdata` image range; the converted image's
  read-only load begins at `0x10249000`
* registered `CoffNativeCodeManager`: `0x1021AE70`
* C011EC16 deferred transition control PC: `0x100547F6`
* range test: `0x100547F6` is outside
  `0x10001000 <= PC < 0x10050950`

The linker map identifies the PC as being inside the guideXOS `RhpNewArray`
helper beginning at `0x10054350`. The range was not inferred from all ELF
memory and was not widened to include this helper.

## Runtime proof

The bounded C011EC17 preflight marker was emitted immediately before the
existing GC-root instrumentation:

```text
[nativeaot-code-manager] registered module=0000000010000000 managedStart=0000000010001000 managedSize=000000000004F950 managedEnd=0000000010050950 manager=000000001021AE70 registrationCount=00000001
[nativeaot-code-manager] preflight runtime=0000000010123900 manager=0000000000000000 managedStart=0000000010001000 managedSize=000000000004F950 managedEnd=0000000010050950 controlPC=00000000100547F6 isManaged=00000000 lookup=0000000000000000 registration=00000001 marker=C011EC17-PREFLIGHT
```

The marker proves the production manager pointer through the startup
registration line and proves the lookup result separately in the preflight.
No C011EC17 success marker was emitted.

The first stack-frame transition reached `CalculateCurrentMethodState` and
the genuine `GetCodeManagerForAddress` null result. Because the iterator's
code-manager condition short-circuits, `FindMethodInfo` was not attempted;
therefore no method metadata became valid and frame-pointer calculation was
not reached. The stack walker remained at zero frames.

Retained GC evidence for the three runs:

* `Thread::GcScanRoots` entered once; the stack-provider callback count was
  `0` and stack-root slots visited were `0`.
* second `Promote` attempts/entries: `0`/`0`.
* second queue insertions: `0`; queue cursor remained `1`.
* queue slot 0 retained storage object `0x100A02F50`; the first insertion is
  the earlier ThreadStatic/storage-object proof, not stack-root evidence.
* sentinel `0x100A01F38` remained unchanged.
* mark-bit writes: `0`; child-reference reads: `0`; graph traversal: `0`.
* no restart or resume occurred.
* the existing single-mutator EE/ThreadStore sequence remained intact:
  one registered mutator, ThreadStore ownership held by the initiator with
  recursion depth `1`, cooperative state `1`, preemptive state `0`,
  `thread_under_crawl` equal to the current thread, and no concurrent scan.
  These are the C011EC16 direct scalar invariants retained by this proof; the
  new marker adds only fixed-size scalar/pointer output.
* C011EC16 observed thread stack base/low `0`, stack limit/high `0`, and
  `ScanContext.stack_limit = 0`. The code-manager failure occurs before
  those bounds are consumed, so C011EC17 does not redesign them.
* allocation attempts in the sensitive suspended transition: `0`; proof
  diagnostics used fixed-size scalar output only. Production manager
  construction occurs before the stack walk during startup and is not a
  lazy stack-walk allocation.

The fail-fast remained `0x47435354`, little-endian bytes `54 53 43 47`
(`TSCG`). It is the guideXOS startup-probe tag, not an official NativeAOT
fail-fast number. In the corrected image the current RIP was
`0x100547EB`; the semantic source boundary remains the null-manager branch in
`StackFrameIterator::CalculateCurrentMethodState`.

## Three fresh QEMU boots

QEMU version: `11.0.0`.

Evidence root (ignored generated output):
`out/dotnet/gc-stack-provider-code-manager-registration/run-20260816-083453061/`.

All three fresh boots were Outcome D with the same semantic checkpoints:

| boot | outcome | manager | managed range | control PC | IsManaged | lookup | FindMethodInfo | callbacks | roots | fail-fast |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| first-run | D | `0x1021AE70` | `0x10001000..0x10050950` | `0x100547F6` | false | null | not attempted | 0 | 0 | `0x47435354` |
| repeat-1 | D | `0x1021AE70` | `0x10001000..0x10050950` | `0x100547F6` | false | null | not attempted | 0 | 0 | `0x47435354` |
| repeat-2 | D | `0x1021AE70` | `0x10001000..0x10050950` | `0x100547F6` | false | null | not attempted | 0 | 0 | `0x47435354` |

Serial-log SHA-256 values, in the same order, were:

* `B62DF4D0DF2A6E4035A75A7988B60BF3C9CEB5265BFA51F4BAE7A3FDE01E1F0A`
* `3E1B4801DC74DE650EC7CB4F1582CDBAA695AD243470B30AA09665E84827D923`
* `9D85AC970287B3DF7FAA381733CAE7EECA88CDA1BE518BC670530DBDB3C98E79`

The semantic values matched even though boot text and serial hashes differed.

## Hashes and validation

For the final three-boot proof build:

* proof kernel: `91A37810A81066F5FF2DE0BA62B4BBB16208B4C74024888BBFAF5FA30911A113`
* linked NativeAOT PE payload:
  `65492940298999CD42527050B1C982CA54690BAAEBD01FDCAE43D5A4DC53B5EA`
* converted fixed-base ELF payload:
  `1B66A461229F8436E479568E5412EEDD363EE4534E02ED5F0763D3CADF25A053`
* runtime-pack lock file:
  `B13F86C33792F5030CA4768F469AA8E96D52586123B289BC32C8DAE659D74267`
* active adapted PAL archive retained by the existing harness:
  `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`

Validation completed:

* focused PE-to-ELF image-geometry unit test: PASS (`1/1`)
* PowerShell harness parse: PASS
* three fresh QEMU 11.0.0 boots: PASS, deterministic semantic Outcome D
* C011EC16: retained as historical Outcome D; no evidence relabeled
* C011EC15 and earlier focused evidence: retained; no stack-root success
  claimed for this milestone
* `git diff --check`: PASS

The ordinary kernel and ESP were recorded before proof deployment and restored
afterward. Both now equal the required SHA-256:

`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`

No QEMU process remains, and no proof payload remains deployed in the ordinary
kernel/ESP locations.

## Final classification and next milestone

Outcome D: legitimate production registration is fixed and independently
observed, but the direct-entry transition-frame provenance still presents a
native guideXOS helper PC to the authentic NativeAOT stack iterator. The
correct next milestone is to repair that production transition/entry
provenance so the authentic control PC is an actual NativeAOT managed-code PC
covered by the registered bookends, then rerun the same code-manager proof.
Separately, audit the zero stack bounds only after this boundary is crossed.
Do not widen the managed range or manufacture method/unwind metadata.
