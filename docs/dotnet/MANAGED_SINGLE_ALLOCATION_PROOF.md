# guideXOS NativeAOT Managed Single-Allocation Proof

## 1. Executive summary

The AMD64 `HostLogProof` now has an opt-in allocating mode. Its managed entry
creates exactly one `byte[]`, writes the fixed UTF-8 message `Hello from managed
heap` plus a managed NUL terminator, exposes the array contents only during the
synchronous guideXOS host callback, and returns `0`.

The proof passes twice in one experimental Server process and again in a
separately launched Server process. The array is a real NativeAOT managed
object allocated through the generated `RhpNewArray` path. The selected
strategy is Approach B: a 64 KiB image-backed, bounded, no-collection
allocation context that satisfies the stock NativeAOT allocation contract for
this one object. Collection is disabled and no GC support is claimed.

**Decision: Outcome A - one managed allocation executes successfully, with
collection disabled.**

## 2. Prior runtime-pack baseline

The preceding milestone remains the non-allocating reverse-P/Invoke proof. Its
locked inputs are unchanged:

| Input | Value |
| --- | --- |
| SDK | .NET SDK `10.0.301`, commit `96856fd726` |
| ILCompiler/runtime pack | `9.0.0` |
| NativeAOT source identity | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Target | `net9.0/win-x64`, AMD64 |
| Lock-file SHA-256 | recorded in `runtime-pack.manifest.json`; see exact value below |

The exact lock hash is currently
`BBB41AFBB6B1FDD41659F77AEB2932F3CE3D3A2B9E87FD0CB1ED12D016E18D77` in the
runtime-pack manifest. The previous non-allocating pack object and adapted
workstation library are preserved under the ignored comparison directories:

```text
out/dotnet/allocation-comparison/non-allocating/
out/dotnet/allocation-comparison/non-allocating-regression/
```

Baseline hashes:

```text
nonallocating platform object: 7678CEC1F9E5377F6229961FC158502DF37927F3C66A04222BA27C5286A9B588
nonallocating adapted library: 6B74B01196A73FFEC9C650F9F2C932BBDAAF70EC779127F66ACE3368B0F0FF71
nonallocating HostLogProof.elf: 647B7CDF5581F61C661BC484039FAECD1E33854E43EA662E20CD32EFD3ACBF69
```

The non-allocating source body remains selectable through the project property
`HostLogProofMode=NonAllocating`; the allocation body is not a replacement for
that regression mode.

## 3. Managed source change

`samples/managed/HostLogProof/Program.cs` has an `#if HOSTLOGPROOF_ALLOCATING`
branch. It contains one allocation:

```csharp
byte[] messageBuffer = new byte[] { ... 23 message bytes ..., 0 };
```

The generated `newarr` operation is followed by managed stores into the array
payload. The reference is passed as an opaque `nint` to the app-scoped
`__Internal` helper, then `GC.KeepAlive(messageBuffer)` keeps the managed local
live through the synchronous callback. No stack allocation is used in this
mode. There are no managed strings, tasks, threads, exceptions, or explicit GC
requests in the experimental body.

## 4. Generated allocation call path

Static map and disassembly evidence show this path:

```text
ManagedMain
  -> RhpReversePInvoke
  -> initializeRuntimeState
  -> RhpNewArray                 guideXOS wrapper
  -> guideXosStockRhpNewArray    renamed NativeAOT helper
  -> RhpNewFast                  stock AllocFast.asm helper
  -> ManagedMain payload stores
  -> generated __Internal P/Invoke stub
  -> guideXosManagedArrayHostLog
  -> existing host Log ABI
```

The wrapper reads the generated array EEType argument. For this `byte[]`, the
observed metadata is component size `1` and base size `16`. The requested
length is `24` bytes, producing an aligned object size of `40` bytes. The
generated `ManagedMain` loads the EEType address, passes `24` to `RhpNewArray`,
stores the 24 payload bytes at object offset `0x10`, and calls the bound helper.

## 5. New runtime dependencies

| Dependency/helper | Non-allocating | Allocating | Contributor | Required semantics |
| --- | ---: | ---: | --- | --- |
| `RhpNewArray` wrapper | No | Yes | guideXOS platform object | Validate EEType/length, compute object size, record diagnostics, enforce bounded context |
| stock `RhpNewArray` | No | Yes | renamed `AllocFast.asm.obj` | NativeAOT array header initialization and allocation contract |
| `RhpNewFast` | No | Yes | `Runtime.WorkstationGC.lib` | Consume current-thread allocation pointer/limit and zero/object initialization contract |
| EEType/module data | Entry-only | Yes | generated image/type manager data | Component size and array base size |
| 64 KiB managed region | No | Yes | guideXOS platform object BSS | Zero-filled bounded storage for this image instance |
| allocation context at TLS `+0x30` | Entry cell only | Yes | guideXOS TLS state | Pointer at `+0`, limit at `+8` |
| `guideXosManagedArrayHostLog` | No | Yes | app-scoped platform helper | Convert object reference to data pointer and invoke synchronous host Log |

No new reserve, commit, release, CRT heap, thread-store, synchronization, or
GC-suspension service was needed by the successful path. The workstation GC
archive is linked only for the reached allocation helpers and their required
metadata; the full workstation GC lifecycle is not initialized.

## 6. Heap and GC requirements

The first allocation requires:

- a writable, zero-filled region;
- a valid current-thread allocation context;
- the generated array EEType and module/type-manager data;
- the reverse-P/Invoke current-thread state established before `ManagedMain`;
- NativeAOT's `RhpNewArray` and `RhpNewFast` object-layout contract.

The experiment does not initialize the general NativeAOT GC, create heap
segments, establish generations, create a finalizer thread, or enter a GC
suspension path. It records `collectionRequested=0` and
`gcSuspensionEntered=0`. A bump-style context is used only because the stock
helper's required contract is satisfied for this bounded object and because no
collection or reclamation is permitted. It is not a GC-compatible heap and is
not presented as GC support.

## 7. Allocation strategy selected

Approach B was selected:

```text
managed allocation with collection disabled
```

The pack owns a fixed 64 KiB region per mapped application image. Allocation is
bounded and monotonically advances the current thread's NativeAOT allocation
context. An invalid type/length, unavailable context, or insufficient space
sets the experimental out-of-memory diagnostic and fail-fasts. It never
returns a fabricated managed reference and never attempts reclamation.

Approach A was not claimed because full matching workstation-GC startup and
platform services are outside this one-allocation boundary. Approach C is the
next requirement if collection or broader managed behavior is requested.

## 8. Platform memory interface

No Windows virtual-memory operation was added to the generic Server executor.
The only memory service proven necessary is the guideXOS runtime-pack-owned
image-backed region:

- ownership: the mapped NativeAOT application image owns its region;
- lifetime: region initialization occurs once for each image instance, on the
  first reverse-P/Invoke entry;
- size: `65536` bytes;
- alignment: the region is 16-byte aligned and object starts are 8-byte aligned;
- zeroing: the platform layer explicitly clears the region before use;
- page granularity: no runtime-facing reserve/commit granularity is exposed;
- failure: arithmetic, context, and bound failures fail-fast with an
  out-of-memory diagnostic;
- cleanup: unmapping the application image releases the region; no object
  reclamation is attempted;
- lifetime policy: heap state is per mapped application launch, not a shared
  process-wide heap.

This keeps memory ownership out of Server C++ allocator objects and leaves a
narrow place for later bare-metal reserve/commit/release replacements.

## 9. Per-thread allocation state

The existing AMD64 guideXOS TLS block remains the source of current-thread
runtime state. Before managed entry, `RhpReversePInvoke` establishes the
runtime cell and the allocation mode initializes:

```text
TLS runtime cell: block + 0x30
allocation pointer: cell + 0x00
allocation limit:   cell + 0x08
initialized marker:  block + 0x38
transition frame:    block + 0x40
local FLS cells:     block + 0x80
```

The second in-process launch receives a fresh mapped image and therefore a
fresh 64 KiB region and fresh allocation pointer. The second Server process
also receives a fresh region. No stale allocation pointer is reused across
launches. Concurrent managed applications and user-created managed threads
remain unsupported.

## 10. Array layout and metadata

The observed object contract is:

| Field | Value |
| --- | ---: |
| element type | `System.Byte` |
| component size | `1` |
| EEType/base size | `16` bytes |
| requested length | `24` |
| aligned object size | `40` bytes |
| object alignment | `8` bytes |
| method-table/type slot | object offset `0x00` |
| array length | object offset `0x08` |
| array data | object offset `0x10` |

The payload contains the 23 UTF-8 message bytes followed by one NUL byte. The
byte payload contains no managed references, so no write barrier is required
for its initialization. The array EEType is supplied by generated NativeAOT
code and is not recreated in native runtime support.

## 11. Pinning and interior-pointer behavior

The host callback is synchronous and the host never retains its pointer. The
selected helper receives an opaque managed object reference, computes the data
address inside the runtime-pack layer, and calls the existing host ABI with
only that data address. Object layout does not cross the host ABI.

Direct fixed/interior-pointer variants were tested during analysis. The fixed
managed-array path faulted and the direct interior-pointer path entered the
NativeAOT fail-fast path, so neither was accepted as evidence. The final mode
does not permit collection or movement, and the managed reference remains live
through the helper call with `GC.KeepAlive`. This is sufficient for the
bounded no-collection proof, but it must be replaced by a real pin/root
contract before collection is enabled.

## 12. Host ABI lifetime rules

`guideXosManagedArrayHostLog` validates the app context, host table, log
function, and opaque object reference. It computes `arrayObject + 0x10` inside
the app-scoped runtime layer and invokes the existing synchronous log callback.
The callback receives exactly 23 message bytes plus the managed NUL, does not
re-enter managed code, and does not retain the pointer. Server stores no
managed reference and receives no object-layout information.

## 13. Static inspection evidence

The final allocating static smoke checks:

- generated `ManagedMain` calls `RhpNewArray`;
- the wrapper calls `guideXosStockRhpNewArray`, which calls `RhpNewFast`;
- the generated code passes length `24`;
- `movups` stores populate the array at `+0x10`, followed by the final 8-byte
  payload store;
- the generated P/Invoke slot is present and bound to
  `guideXosManagedArrayHostLog` by the reverse-transition hook;
- the success string is present in managed generated read-only data and is not
  present in `guidexos_nativeaot_platform.cpp` or its runtime object;
- the managed source contains exactly one intentional `new byte[]` allocation;
- no FLS import is present in the allocating image;
- default app inventory counts remain unchanged.

Final evidence identifiers:

```text
runtime-pack identity: guidexos-nativeaot-runtime-pack-amd64-hostlog-allocating-nocollection-v1
runtime-pack source SHA-256: 0D2D2275C36F35124F00BE91D46A90CEC3B088B87B252DD84FA3A3EF315EC0AC
runtime-pack object SHA-256: 2C39E2BE97DABA72B56DBE8475826CD11CE09E654CAA222C55A1ED59572351F2
adapted Runtime.WorkstationGC.lib SHA-256: 4AD17D742C5AED40D559602C199E7808B2430BDEB86AAC4E7D697168BF349DAC
runtime-pack lock SHA-256: BBB41AFBB6B1FDD41659F77AEB2932F3CE3D3A2B9E87FD0CB1ED12D016E18D77
converter SHA-256: 5F21B87D343106120EB5CAD1F98DF524404171E084C40F4FC3AFED6BE6F84B96
final source/staged ELF SHA-256: 6FE0BC2EAB3DB9E7E5F583856776DD58B9B7B13E598AF4D678993645BD0FE4A4
```

## 14. Runtime diagnostics

The runtime-pack object exports an experimental volatile diagnostics record
with these fields:

```text
heapInitialized
allocationCount
requestedArrayLength
requestedObjectSize
outOfMemory
collectionRequested
gcSuspensionEntered
heapBase
heapSize
initialAllocationPointer
allocationPointerAfter
returnedObject
arrayData
```

The successful run records one allocation, length `24`, object size `40`, and
zero collection/suspension requests. Addresses are retained for debugger and
map inspection only; normal proof output does not print object contents as a
native fallback and does not depend on raw addresses.

## 15. First launch

The clean allocation build and static assertions passed. The first Server
launch mapped at preferred base `0x10000000`, installed the existing AMD64
trampoline, attached the current thread, entered the actual generated
`ManagedMain`, allocated the array, and invoked the host once.

```text
host log: Hello from managed heap
host callback count: 1
managed/native return code: 0
gxMain return code: 0
allocation mode: managed allocation with collection disabled
```

No access violation was suppressed and no live unresolved Windows thunk was
entered.

One fresh-output wrapper attempt exited with `0xC0000409` before the callback;
the failure was not suppressed. The same rebuilt artifact was immediately
rerun with `-SkipBuild` and passed both in-process launches and the separate
process check. The failed attempt is retained as
`out/dotnet/allocation-comparison/allocating/allocation-execution-final.log`;
the passing retry is `allocation-execution-retry1.log`.

## 16. Second in-process launch

The same Server process launched the application a second time. Runtime IDs
were `1` and `2`; both launches used valid fresh per-image allocation state,
logged the exact message once, and returned `0`.

Result: **second in-process allocation PASS**.

## 17. Cross-process result

A separately launched experimental Server process repeated the proof. It
again mapped at the preferred base, allocated one array, logged once, and
returned `0`.

Result: **second Server process PASS**.

## 18. Failure tests

The bounded failure coverage is intentionally narrow:

| Check | Result | Evidence |
| --- | --- | --- |
| heap initialization failure | BLOCKED | no safe live injection point was added to the one-allocation harness |
| heap too small / out of memory | BLOCKED | repeated allocation is explicitly the next experiment |
| allocation before runtime initialization | STATIC FAIL-FAST | wrapper requires the initialized TLS runtime cell |
| allocation before thread attachment | STATIC FAIL-FAST | current TLS block/cell validation precedes stock helper entry |
| null host context | STATIC FAIL-FAST | app-scoped host helper validates context |
| null logging function | STATIC FAIL-FAST | app-scoped host helper validates host log pointer |
| unsupported host API version | EXISTING HOST GUARD | the managed entry preserves the existing API/version guard |
| runtime-pack hash mismatch | PASS | clean staging checks lock and manifest hashes before launch |

No fatal failure is converted into a fake object or a continuing managed
return. Controlled OOM is reported as **BLOCKED** for this pass, not as a
successful collection or allocation result.

## 19. Non-allocating regression result

The original non-allocating mode was rebuilt and rerun after allocation work.
It still passes twice in one Server process and in a separate Server process,
returns `0`, logs `Hello from managed guideXOS code` once per launch, and does
not require the allocation region for its proof path. Its artifacts and a
final successful live log are archived under:

```text
out/dotnet/allocation-comparison/non-allocating-regression/
```

One transient process-exit `0xC0000374` occurred after a nonallocating smoke
had already printed successful callbacks; the exact rerun with the preserved
artifacts passed, including the separate-process check. No base runtime
regression remains.

## 20. What this proves

This proof establishes, for the locked AMD64 image and this exact bounded
entry:

- a real managed `byte[]` can be allocated through NativeAOT's allocation
  helper;
- the generated array type metadata and object layout are valid;
- managed code writes the array contents;
- a synchronous host callback can receive the array data safely while
  collection is disabled;
- return and reverse-P/Invoke state remain correct;
- fresh per-launch allocation state works for repeat and cross-process runs;
- the default application inventory, normal Server build, and experimental
  Server build remain isolated from the proof.

## 21. What remains unsupported

The following remain outside this result:

- collection, compaction, promotion, finalization, and object reclamation;
- a general managed heap or general-purpose NativeAOT GC port;
- managed exceptions and exception unwinding as a supported feature;
- managed strings except for any compiler/runtime artifacts not used by this
  method;
- user-created managed threads, thread pool, tasks, async, GUI, networking,
  filesystem, reflection, globalization, Console, COM, and broad CoreLib;
- multiple concurrent managed applications;
- a permanent public .NET runtime type;
- ARM64 support. The TLS offsets, Win64 ABI, transition frame, image layout,
  and runtime object are AMD64-specific.

The pack still contains stock Windows import entries. The allocating-versus-
nonallocating import delta is exactly `FreeLibrary` and `SetThreadErrorMode`,
introduced by the generated `__Internal` P/Invoke resolver. The live path
binds the app-scoped slot during `RhpReversePInvoke`, so neither new resolver
import is entered. Other stock PE imports, including virtual-memory,
threading, synchronization, COM, and CRT entries, remain in the generated
image and are not claimed dead in general; they were not entered by this
bounded path. No FLS import was entered or retained as a live dependency.

## 22. Exact next experiment

Because Outcome A was reached with collection disabled, the next experiment is
only:

> Perform bounded repeated managed allocation until the no-collection heap
> reaches a controlled out-of-memory condition, proving object layout and
> allocation-context stability without claiming garbage collection.

That repeated-allocation experiment is not implemented here. If real GC is
selected later, it must instead begin with the smallest matching GC
initialization and one controlled collection after an object becomes
unreachable.

## Validation matrix

| Validation | Result |
| --- | --- |
| Runtime-pack clean build | PASS |
| Runtime identity and lock hashes | PASS |
| Non-allocating regression | PASS |
| Allocation helper inspection | PASS |
| New live-import analysis | PASS; no new live import entered |
| Heap initialization | PASS |
| First allocation | PASS |
| First host callback | PASS; one |
| First return | `0` |
| Second in-process allocation | PASS |
| Second return | `0` |
| Second Server process | PASS |
| Out-of-memory behavior | BLOCKED by bounded scope |
| Default inventory isolation | PASS |
| Normal Server build | PASS |
| Experimental Server build | PASS |

## Evidence locations

The focused checks are:

- [`scripts/smoke-dotnet-managed-single-allocation-static.ps1`](../../scripts/smoke-dotnet-managed-single-allocation-static.ps1)
- [`scripts/smoke-dotnet-managed-single-allocation-execution.ps1`](../../scripts/smoke-dotnet-managed-single-allocation-execution.ps1)
- [`out/dotnet/allocation-comparison/allocating/staged/proof/proof-envelope.json`](../../out/dotnet/allocation-comparison/allocating/staged/proof/proof-envelope.json)

Generated runtime objects, libraries, PE files, and ELF files remain ignored
and are not tracked by Git.
