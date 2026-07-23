# guideXOS AMD64 NativeAOT runtime pack

This directory contains the source and build recipe for the first guideXOS
NativeAOT runtime-pack candidate. It is deliberately an application-specific
Approach B pack for the non-allocating `HostLogProof` entry. It is not a
general .NET runtime and it does not replace the stock pack in normal builds.

The candidate is built against the exact NativeAOT package identity recorded in
`runtime-pack.lock.json`. The external .NET runtime checkout is optional for
this first object-level candidate; when supplied to the build script it is
verified and only used as provenance/reference input. The checkout is never
modified.

## What this pack replaces

The stock Windows `Runtime.WorkstationGC` reverse-P/Invoke path reaches the
Windows FLS imports and then `ThreadStore::AttachCurrentThread`. The current
host image does not have the stock runtime's FLS allocator, runtime instance,
or thread-store state. The pack therefore supplies, at the runtime link
boundary:

* a deterministic guideXOS FLS index;
* per-native-thread FLS values stored in the host-provided NativeAOT TLS block;
* same-thread set/get behavior and no cross-thread global value;
* an idempotent first-entry runtime/thread-state binding;
* an AMD64 reverse-P/Invoke frame binding and return operation;
* fail-fast behavior when the host TLS envelope is absent or malformed.

The implementation does not enter the stock `ThreadStore::AttachCurrentThread`
or Windows `FlsGetValue`/`FlsSetValue` thunks. It does not allocate managed
objects, start a thread, collect, throw a managed exception, or claim support
for the stock GC/thread-store ABI beyond the exact transition used here.

## Inactive ThreadStore and stack-bound readiness adapter

The isolated `guidexos_nativeaot_threadstore_adapter` and
`guidexos_nativeaot_stack_bounds_adapter` provide the next startup-boundary
probe without changing this fixed reverse-P/Invoke path. The adapter owns a
bounded opaque runtime-thread registry, binds records through the completed
dynamic FLS manager, stores exact `[low, high)` stack bounds and current RSP,
initializes the NativeAOT transition sentinels and preemptive state, and
validates attach, lookup, detach, generation reuse, callback ordering, and
shutdown. Its worker is an ordinary native worker; it is not the real
finalizer/helper thread.

The probe remains inactive by design. It never calls `RhInitialize`, creates a
Workstation heap, enters managed code, starts finalization, allocates through
the real GC, or triggers collection. The fixed index-zero proof object and its
reverse-P/Invoke frame behavior remain unchanged. Collection-safe suspension,
ThreadStore enumeration, and exact binding of the stock GC-owned virtual-memory
symbols are still outside this adapter. The raw VM boundary itself is
documented and probed in
`docs/dotnet/NATIVEAOT_GC_OWNED_VIRTUAL_MEMORY.md`; the current single next
readiness blocker is replacing the stock `gcenv.windows.cpp.obj` binding and
its Windows VM imports with that adapter.

The build also creates an adapted copy of the locked `Runtime.WorkstationGC.lib`
in the ignored output directory. The matching stock `thread.cpp.obj` and
`EHHelpers.cpp.obj` members are extracted and their replaced exports are
renamed (`guideXosStock...`) before the archive is rebuilt. This avoids a
multiple-definition link and keeps the remaining stock runtime support objects
ABI-matched to the exact 9.0.0 package. The archive member timestamps are
normalized so clean builds are byte-reproducible.

## ABI boundary

`src/platform/guidexos_nativeaot_platform.cpp` only knows the NativeAOT TLS
layout needed by this experiment:

* `_tls_index` selects the per-thread vector entry;
* the runtime thread-data cell begins at offset `0x30` in the NativeAOT block;
* the transition-frame link is at offset `0x40` within that cell;
* the initialized flag is at offset `0x38` within that cell;
* eight guideXOS FLS cells are reserved at block offset `0x80`.

These offsets are validated against the generated disassembly and recorded in
the generated runtime-pack manifest. They are not part of the Server C ABI.

## Build

From the Server repository:

```powershell
scripts/dotnet/build-guidexos-nativeaot-runtime-pack.ps1
```

The generated object and manifest are written below
`out/dotnet/runtime-pack/` and are ignored by Git. A managed proof consumes
the object only when explicitly requested:

```powershell
scripts/dotnet/build-managed-hostlog-proof.ps1 `
  -RuntimePackRoot tools/dotnet/runtime-pack `
  -UseGuideXosRuntimePack `
  -Clean
```

The lock file is intentionally a source/toolchain lock, not a vendored runtime
tree. Generated libraries and objects must never be committed.

The standalone native harness is intentionally not a separate fake entry point:
the generated image is a fixed-base, sectionless ET_EXEC with the guideXOS host
context ABI. The existing experimental Server loader is therefore the narrow
real-method harness for this pass; a standalone harness remains a documented
follow-up once that loader contract is factored out.

## Construction approaches

Approach A, rebuilding the complete NativeAOT PAL and GC runtime from a
matching dotnet/runtime checkout, remains the long-term direction. It is
deferred because the current workspace has no matching source checkout and a
complete PAL port would exceed this non-allocating proof.

Approach B is selected for this pass because the generated proof already has a
small, isolated reachable transition and the stock pack's unresolved Windows
surface can be removed from that active path without changing the Server
loader. The candidate still uses the stock package for compiler inputs and
unreachable support objects; that is why the result must not be described as a
general freestanding runtime.

Approach C, adapting the old guideXOS C# runtime, is rejected as an
implementation source. Its startup, allocator, scheduler, and Win32 shim are
kernel-coupled and its reverse-P/Invoke helpers are no-op stubs from a
different runtime generation. Those sources remain design/reference material
only.
