# C108 — NativeAOT `[ThreadStatic]` / TLS resident-dispatch lifecycle

Status: **Outcome A — concrete guideXOS bridge lifecycle defect repaired**

Date: 2026-09-12

## Scope and C107 handoff

C107 proved that one authentic resident NativeAOT image can dispatch multiple
logical applications. C108 kept that architecture unchanged and investigated
only the apparent `[ThreadStatic]` reset. The C107 observation reproduced in
the pre-repair runs: the same logical App A field returned `0 -> 1` on each
resident invocation even though ordinary statics persisted.

The pre-repair serial log with the reset marker is preserved under
`out/dotnet/c011ec108-threadstatic-tls-lifecycle/prior-run-20260912-221651/`.
The final repaired evidence is under
`out/dotnet/c011ec108-threadstatic-tls-lifecycle/boot-01/` through `boot-03/`.

The C108 sequence is:

`A1 -> B1 -> A2 -> B2 -> A3 -> invalid rejected`

The test continues to use one composite image at `/system/wall/C107.ELF`.
It does not load a second image or modify the independent-image `Busy` guard.

## NativeAOT source architecture

The locked NativeAOT source is commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` under
`out/dotnet/gc-feasibility-baseline/nativeaot-runtime`.

The NativeAOT TLS representation for the current managed thread is the
compiler TLS variable `tls_CurrentThread`, whose type is
`RuntimeThreadLocals`. `ThreadStore::RawGetCurrentThread()` returns the
address of that TLS record cast as `Thread*`. `RuntimeThreadLocals` owns the
thread allocation context, transition state, PAL thread handle, ordinary
thread-static storage pointer, inlined thread-static root list, GC-frame state,
stack bounds, and the NativeAOT thread id.

Relevant locked-source locations:

- `src/coreclr/nativeaot/Runtime/threadstore.inl`: `tls_CurrentThread` and
  `RawGetCurrentThread()`.
- `src/coreclr/nativeaot/Runtime/thread.h`: `RuntimeThreadLocals`,
  `GetThreadStaticStorage()`, and the inlined-root API.
- `src/coreclr/nativeaot/Runtime/threadstore.cpp`: attach/detach and the
  `tls_CurrentThread` definition.
- `src/coreclr/nativeaot/Runtime/thread.cpp`: reverse-P/Invoke attach logic,
  thread-static storage access, and inlined-root registration.
- `src/coreclr/nativeaot/System.Private.CoreLib/src/Internal/Runtime/ThreadStatics.cs`:
  the inlined `[ThreadStatic]` base, slow allocation, and root publication.
- `src/coreclr/nativeaot/System.Private.CoreLib/src/System/Runtime/RuntimeImports.cs`:
  `RhGetThreadStaticStorage` and `RhRegisterInlinedThreadStaticRoot`.

For inlined thread statics, NativeAOT uses the managed
`ThreadStatics.t_inlinedThreadStaticBase` field. The first access allocates a
thread-static base object, registers its `InlinedThreadStaticRoot` with the
current NativeAOT `Thread`, and stores the base in that thread's TLS-managed
state. A continuing attached thread therefore retains the base and its fields.
`ThreadStore::AttachCurrentThread()` returns when the current NativeAOT thread
is already initialized; detach is the operation that tears down that identity.

## guideXOS TLS bridge architecture

guideXOS has a separate bridge envelope in `kernel/core/nativeaot_application.cpp`:

- `g_tlsArea` is the GS-area structure with the vector at offset `0x58`.
- `g_tlsVector[0]` points to `g_tlsBlock`.
- `g_tlsBlock` is the guideXOS FLS/runtime-cell block (`0x110` bytes).
- `installTls()` writes the GS base MSR and verifies it.

This bridge block is not the NativeAOT `tls_CurrentThread` record. The
runtime-pack's `currentTlsBlock()` reads the guideXOS GS vector, while the
locked NativeAOT `ThreadStore` APIs read `tls_CurrentThread`. The bridge also
uses a guideXOS FLS cell to point at the NativeAOT `Thread*` for diagnostics.

The adapter in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_threadstore_adapter.cpp`
is a guideXOS lifecycle record used by the launch seam. It is deliberately
reported separately from the locked NativeAOT `Thread*` identity.

## Initial-launch call path

The observed and source-backed path is:

```text
kernel main loop
  -> launchInternal("/system/wall/C107.ELF", app id)
  -> read/probe and map one ELF image
  -> initialize/attach guideXOS local storage
  -> initialize/attach the guideXOS NativeAOT threadstore adapter
  -> call GuideXosNativeAotApplicationEntry(startup context)
       -> install PAL hooks, PAL hook table, and GC startup table
       -> RhInitialize(0)
       -> installTls() once for the initial managed entry
       -> ManagedMain(managed context)
            -> managed host callbacks / __Internal P/Invoke
            -> guideXOS RhpReversePInvoke(frame)
                 -> currentTlsBlock()
                 -> initializeRuntimeState(block)
                 -> InitializeModules() once on the first runtime setup
```

The application-owned startup wrapper is
`samples/managed/HostLogProof/c102_startup.c`. Startup stages 1 through 7,
PAL initialization, GC initialization, `RhInitialize`, code-manager
registration, and module initialization were each observed once per boot.

## Resident-reentry call path

The resident path is:

```text
kernel main loop
  -> launchResident("/system/wall/C107.ELF", app id)
  -> verify the same resident path; do not map another image
  -> reuse local storage and the guideXOS adapter record
  -> call GuideXosNativeAotApplicationEntry(startup context)
       -> g_startupState == Ready
       -> installTls() again (same GS area/vector/block)
       -> ManagedMain(managed context)
            -> managed host callbacks / __Internal P/Invoke
            -> guideXOS RhpReversePInvoke(frame)
                 -> currentTlsBlock()
                 -> initializeRuntimeState(block)
                 -> preserve the existing managed heap and allocation cursor
```

No resident call invokes `RhInitialize`, repeats PAL/GC startup, registers a
second code manager, initializes a second module, detaches the NativeAOT
thread, or maps another executable.

## Identity census

The following values were stable for every valid managed entry in all three
final boots. The bridge reports the PAL logging thread id as zero because this
minimal PAL logging implementation does not expose a separate OS thread id;
the guideXOS scheduler and adapter ids are the meaningful execution-thread
identities here.

| Identity | Observed value |
| --- | --- |
| guideXOS scheduler thread/task | `1` |
| guideXOS adapter record | `0x2C4500` |
| adapter native thread id / generation / attached | `1 / 1 / 1` |
| NativeAOT `Thread*` / `tls_CurrentThread` | `0x382D110` |
| guideXOS GS area | `0x382D1F0` |
| guideXOS TLS vector | `0x382D0D8` |
| guideXOS TLS block | `0x382D0E0` |
| NativeAOT thread-static storage pointer | `0x382D1A0` |
| NativeAOT inlined-root record | `0x382D0F0` |
| inlined thread-static base object | `0x100101408` |
| App A field address | `0x100101410` |
| App B field address | `0x100101414` |
| `RuntimeInstance` | `0x100141010` |
| code manager | `0x104300010` |

App A and App B field addresses are distinct, proving separate fields on the
same continuing managed thread rather than an application-local TLS scheme.

## Exact reset boundary and repair

The original explanation was incomplete. `installTls()` did run on each
resident logical-app entry, but it only rewrote the same GS base, vector, and
block. It did not clear the NativeAOT `Thread*` or its thread-static root.

The destructive operation was in the guideXOS runtime-pack bridge's
non-real-GC `initializeRuntimeState()`, called by the bridge's
`RhpReversePInvoke()` implementation. Before C108 repair, each call:

1. zeroed `g_guideXosAllocationDiagnostics`;
2. zeroed the fixed 64 KiB `g_guideXosManagedHeap`; and
3. rewrote the runtime-cell allocation pointer to the heap base.

The inlined NativeAOT thread-static base object was in that heap at
`0x100101408`. The reset therefore erased the object contents and rewound the
allocator, while the `Thread*`, root address, and field addresses remained
unchanged. The controlled pre-repair log shows seven `reset` events for this
workload, including one reset on each resident transition.

C108 now guards the launch-scoped heap initialization with one
`g_c108ManagedHeapInitialized` lifetime flag for the continuing managed
thread. The first transition performs the required zero-initialization once;
later reverse-P/Invoke transitions log `action=preserve` and leave both the
heap contents and runtime-cell allocation cursor intact. This is the smallest
bridge-side repair at the proven reset boundary. No NativeAOT runtime source,
ThreadStore implementation, PAL implementation, or GC algorithm was changed.

## Managed results

The final valid sequence produced the following values in every boot:

| Logical app | Ordinary static | `[ThreadStatic]` | Allocation |
| --- | --- | --- | --- |
| App A | `0→1`, `1→2`, `2→3` | `0→1`, `1→2`, `2→3` | `byte[8]`, sum `36`, PASS each time |
| App B | `0→1`, `1→2` | `0→1`, `1→2` | `byte[5]`, weighted result `150`, PASS each time |

The invalid selector after A3 returned managed `-4`, was reported as
`invalid-app-id`, and did not disturb the resident image. All five valid calls
returned managed zero/native success and returned control to the kernel main
loop.

## Lifecycle decision

The selected model is **Model P — persistent attached managed thread**:

- one guideXOS scheduler thread performed every entry;
- one guideXOS adapter record remained attached;
- one NativeAOT `tls_CurrentThread` / `Thread*` remained observable;
- one NativeAOT thread-static storage/root identity remained observable; and
- no detach or fresh NativeAOT thread attachment occurred between calls.

Authentic NativeAOT semantics associate `[ThreadStatic]` with the continuing
managed thread, not with a logical application id. Under this guideXOS model,
the reset was a guideXOS bridge defect, not an intended fresh-thread contract.

## Fresh-run evidence

The final run used three fresh QEMU boots. All passed the strict runner checks:

| Boot | Serial SHA-256 | Result | Heap lifecycle |
| --- | --- | --- | --- |
| 1 | `5B4A9FD83B52E3E931AE07B3C1940F7CAACCCA07DCB0B3A3E89673132BB26655` | PASS | 1 reset / 6 preserves |
| 2 | `7CB98FD6872BE16C62399E404B620A999FEED6E9F422680E2B8F37630104A738` | PASS | 1 reset / 6 preserves |
| 3 | `BAC4F51300C39A37FFAEE4002920725534F7F7FC2C3291E078B607819DC1E0EB` | PASS | 1 reset / 6 preserves |

Final artifact hashes:

- composite ELF: `851CA06E68CFDB9D05F9E179B1B54C531EE681ADD4AB8E3671C9A0CDDF137DD1`
- composite map: `B45A1CE73F5B77C3C3E483E8917B0C8C87DE1AECEBE329005B7312AA3A1E1A97`
- kernel: `FCA24087D9878FDE5D0E6DFC733FEE7B90D0BB7C716BFF76AA50F7BB36B18714`
- runtime-pack platform object: `5355E767860E0145AEE00BC87B56F2736B54AF6311F80F6006911E5B480B132D`
- final ramdisk: `11E0562C5EC157C0CA815F19F8C219B07D687B8A7D67DFDF6F1C5FB63D314B17`

Evidence index:

- `out/dotnet/c011ec108-threadstatic-tls-lifecycle/c108.manifest.json`
- `out/dotnet/c011ec108-threadstatic-tls-lifecycle/inputs.json`
- `out/dotnet/c011ec108-threadstatic-tls-lifecycle/boot-01/serial.log`
- `out/dotnet/c011ec108-threadstatic-tls-lifecycle/boot-02/serial.log`
- `out/dotnet/c011ec108-threadstatic-tls-lifecycle/boot-03/serial.log`
- `out/dotnet/c011ec108-threadstatic-tls-lifecycle/build/runtime-pack/runtime-pack.manifest.json`
- pre-repair reset evidence:
  `out/dotnet/c011ec108-threadstatic-tls-lifecycle/prior-run-20260912-221651/boot-01/serial.log`

The manifest records one executable mapping, one runtime initialization, one
PAL initialization, one GC initialization, one code-manager registration, and
one module initialization. The independent-image `Busy` guard remains in the
resident loader and was not modified or bypassed by C108.

## Changes and boundaries

Production/support changes are limited to the C108 composite launch path,
identity diagnostics, and the guideXOS runtime-pack initialization guard. The
repair is in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`.

There were **no** changes to the locked NativeAOT runtime sources and **no**
GC algorithm changes. The extracted locked-source copies used for the audit
are evidence/build inputs only. Multi-module loading, runtime unload,
`RuntimeInstance` replacement, GC replacement, and `Busy` removal remain out
of scope.

## Next phase

Do not begin C109 as part of C108. The recommended next phase is to integrate
the proven persistent-attached-thread contract into the non-diagnostic
production launch configuration, with the same one-image/one-runtime/one-GC
boundary and without reopening historical GC or multi-module work.
