# C188R12R4R5R4R5R2R2 Native Allocation Attribution

**Outcome A — `NATIVE_ALLOC_PROBES_HOSTED_READY`.** The linked allocation route is established, a bounded diagnostic record is in the isolated candidate, hosted tests pass, and diagnostic plus proof-disabled production artifacts are staged. No guest root cause is claimed: the exact failing kernel/PAL branch remains to be captured by a later guest run.

## Scope and preserved evidence

This phase was hosted-only. No QEMU process was launched and no QAC reservation was made. No allocator repair, heap expansion, allocation-semantic change, or resource-lifecycle change was made.

| Revision | HEAD |
| --- | --- |
| Starting repository HEAD | `49fd67dc8570c0dd1ee2c2dfca914c92426d988f` |
| Qualified source and candidate HEAD | `7fec6954bf2848627a496e16ea77e24e69efa830` |
| Branch | `v1.1_DOTNET_SUPPORT` |

The starting and final implementation HEADs were verified in the repository. This report is committed separately after the qualified source HEAD. The original source RAM disk and staged RAM disk both hash to `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The existing worktree change to `ESP/ramdisk.img` and unrelated generated/untracked outputs were preserved and excluded from commits.

The reviewed R2R1 evidence remains at `out/dotnet/c188r12r4r5r2-semishared-attribution/qac-guest-r4r5r2r1-001/serial.log`, SHA-256 `39402A5495C0DF35492C403B776EC9214A26D64D31B2D1CA558F196124905C5B`. It records `0x0120 GX_SEMISHARED_OP_BGC_DONE_EVENT`, nested failure `0x0140 GX_SEMISHARED_OP_EVENT_IMPL_ALLOC`, an 8-byte/8-aligned nothrow request returning null, no event callback, cleanup completion, `init_semi_shared()` false, and GC initialization `E_FAIL` (`0x80004005`). The evidence establishes the failure site but has no allocator/PAL status and does not establish exhaustion or another root cause. See the retained [R2R1 attribution](NATIVEAOT_C188R12R4R5R4R5R2R1_SEMISHARED_GUEST_ATTRIBUTION.md) and [R2 attribution](NATIVEAOT_C188R12R4R5R4R5R2_SEMISHARED_ATTRIBUTION.md).

## Selected `GCEvent` implementation

The candidate uses the NativeAOT Workstation GC runtime pack from runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` (runtime pack identity `guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r4r5r2r2-native-allocation-v1`). The linked `GCEvent` implementation is the guideXOS overlay at `tools/dotnet/runtime-pack/src/gcenv/guidexos_gcenv.cpp`, class `GCEvent::Impl` at line 128. It contains one `EventHandle*` initialized to null. On the selected x64 target, compile-time assertions in the diagnostic build verify `sizeof(Impl) == 8` and `alignof(Impl) == 8`.

Both `CreateOSAutoEventNoThrow` and `CreateOSManualEventNoThrow` allocate it with `std::unique_ptr<GCEvent::Impl> impl(new (std::nothrow) GCEvent::Impl())` (lines 198–235 and 241–280). C++ invokes the constructor only if the allocation function returns non-null; the diagnostic marker `ConstructorEntered` is recorded only for a non-null `impl`. On null, the function returns false before `createAutoResetEvent` / `createManualResetEvent`, before an `EventHandle` is allocated, and before the platform event callback. The existing outer cleanup and semi-shared failure propagation remain unchanged.

The pinned upstream Windows implementation (`src/coreclr/gc/windows/gcenv.windows.cpp`, `GCEvent::Impl` around line 1349) stores a Win32 `HANDLE` and calls `CreateEvent` after allocating the implementation. The upstream Unix implementation (`src/coreclr/gc/unix/events.cpp`) stores pthread mutex/condition state and initializes it after allocation. Neither is the implementation linked by this candidate. The runtime-pack manifest removes stock `gcenv.windows.cpp.obj`; the candidate map selects `guidexos_gcenv.production.obj`, which implements the guideXOS `EventHandle*` wrapper and routes event creation through the guideXOS startup bridge. Thus, the diagnostic ELF does not infer its event implementation from a platform source file merely being present.

## Resolved nothrow operator and backing allocator

The selected x64 MSVC ABI symbol is:

```text
??2@YAPEAX_KAEBUnothrow_t@std@@@Z
void* __cdecl operator new(unsigned __int64, const std::nothrow_t&)
```

The source declares this overload `noexcept`. The actual candidate map resolves it to `guidexos_nativeaot_startup_imports.production.obj`, not to the CRT or stock runtime allocator. The linked PE disassembly at `native-allocation-linked-disassembly.txt` confirms that overload calls `guidexos_nativeaot_startup_malloc` and then records the nothrow result. The map and disassembly excerpts are hash-bound in the candidate manifest.

The path is:

```text
GCEvent::Impl new(std::nothrow), size=8, alignment=8
  -> guideXOS global operator new(std::size_t, std::nothrow_t const&)
  -> guidexos_nativeaot_startup_malloc(8)
  -> 16-byte GuidexosNativeAotMallocHeader + 8 bytes = 24-byte request
  -> guidexos_nativeaot_pal_virtual_alloc(nullptr, 24, MEM_RESERVE|MEM_COMMIT,
                                          PAGE_READWRITE)
  -> installed kernel legacyVirtualAlloc hook
  -> startupReserve(24, 4096, nullptr, ...), then commit rounded to 4096
  -> true bare-metal VirtualMemoryRegion reserve and commit
  -> address-space frame allocation, zeroing, and page mapping
```

The map places the overload at `0x100050E80`, startup malloc at `0x1000508E0`, and PAL virtual allocation at `0x10004FE00`. The linked disassembly shows the overload call to startup malloc, the 24-byte PAL call with allocation type `0x3000` and protection `0x04`, and the runtime record calls. `runtime/memory/guidexos_virtual_memory_region_baremetal.cpp` defines the true backend; the reservation is an unbacked virtual interval, while commit allocates physical frames. This route does not use the production Notes persistent arena or the managed object heap.

## Initialization order and failure visibility

The kernel installs the NativeAOT PAL hooks and startup bridge before `GuideXosNativeAotGcStartupMain` calls `RhInitialize(false)` (`tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_c188r12r4r5_gc_startup_adapter.cpp:205–218`). GC initialization, including the event allocation, occurs inside `RhInitialize`. The adapter snapshots and serializes the fixed diagnostic records after `RhInitialize` returns; the new serial stage is `GX_INIT_STAGE_GC_NATIVE_ALLOC_RECORD = 0x0072`. The existing R2R1 failure returned through this path, allowing post-return emission.

The startup allocator has no prerequisite managed heap. Its immediate readiness requirement is that the PAL hook table be installed; otherwise it reports `0x0303` and returns null. The kernel backend also requires a current address space, checked at VM reserve and reported as `0x0401`/`VmResult::Unsupported` if absent. Reservation can fail before physical allocation because the virtual interval or bounded region metadata is unavailable. Commit can fail because a frame cannot be allocated or because zeroing/mapping/protection operations fail. Consequently, an 8-byte null result alone does not prove memory exhaustion.

## Stable operations and failure branches

The marker is `C188R12R4R5R4R5R2R2_NATIVE_ALLOC`. Operation IDs `0x0200`–`0x0208` identify the `GCEvent::Impl` site, nothrow new, startup malloc, PAL virtual allocation, kernel adapter, VM reserve, VM commit, rollback release, and final pointer. The source-defined IDs are in `guidexos_nativeaot_native_alloc_record.h`; the staged [failure-branch map](../../out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/candidate-20261102T022027433Z/native-allocation-failure-branch-map.json) gives names and status sources.

| Branch | Meaning and evidence |
| --- | --- |
| `0x0301` | Diagnostic scope/attempt overflow; fixed capacity is eight attempts. |
| `0x0302` | Startup malloc size plus header would overflow `uintptr_t`; returns null before PAL. |
| `0x0303` | PAL hook table is not ready; callback was not entered, native status is unavailable. |
| `0x0304` | PAL callback returned null; callback boundary exposes only a pointer, so PAL/OS error is unavailable there. |
| `0x0310` | Kernel's bounded 16-slot PAL VM tracking table has no free slot. |
| `0x0311` | Kernel adapter's VM reserve failed; backend record retains raw `VmResult`. |
| `0x0312` | Kernel adapter's VM commit failed; rollback is attempted, with commit and release results recorded. |
| `0x0313` | Rollback release failed; raw VM result is retained when available. |
| `0x03FF` | Required observation is inconsistent or incomplete; the analyzer must not infer a cause. |

The true VM reserve branches (`runtime/memory/guidexos_virtual_memory_region_baremetal.cpp:297–431`) are:

| Branch | Condition and result |
| --- | --- |
| `0x0401` | No current address space; `Unsupported`. |
| `0x0402` | Null region or zero size; `InvalidArgument`. |
| `0x0403` | Region is already reserved; `AlreadyReserved`. |
| `0x0404` | Invalid alignment; `AlignmentError`. |
| `0x0405` | Page-rounding overflow or request exceeds backend region limit; `RangeOverflow`. |
| `0x0406` | Non-null preferred base has invalid alignment; `AlignmentError` (not used by the current null-preferred-base call). |
| `0x0407` | Preferred base falls outside the configured runtime address range; `AddressUnavailable` (not used by the current call). |
| `0x0408` | No available virtual interval; `AddressUnavailable`. |
| `0x0409` | Bounded VM region metadata is exhausted; `OutOfMemory`. |

The true VM commit branches (`:434–556`) are:

| Branch | Condition and result |
| --- | --- |
| `0x0410` | Region state/ownership is missing; `AlreadyReleased`. |
| `0x0411` | Protection is unsupported; `ProtectionUnsupported`. |
| `0x0412` | Page range, size, or alignment is invalid; `AlignmentError` or `OutOfRange`. |
| `0x0413` | `allocateFrame(FrameOwner::VmRegion)` returns zero; `OutOfMemory`. |
| `0x0414` | `zeroFrame` fails; `HostFailure`. |
| `0x0415` | Page mapping fails; `HostFailure`. |
| `0x0416` | Page-protection update fails; `HostFailure`. |

`VmResult` is captured before rollback for reserve/commit failures (status source 1). Boolean kernel failures are recorded as diagnostic status source 2; they identify the failing primitive but do not supply an OS error code. The PAL pointer-only boundary reports status source 0. The backend record also captures request and rounded size, slot and VM metadata capacity, address-space readiness, reserve/commit/rollback results, final pointer state, first failure operation/branch/status, and before/after frame and VM statistics. These measurements expose capacity limits without assuming that any one limit caused the guest failure.

## Diagnostic record and safety properties

The runtime record has eight fixed attempt slots, 25 fields per attempt, and a nine-field summary. It records the site and request size/alignment, selected nothrow operation, `_ReturnAddress()` callsite, backing allocator identity, malloc size and backing total, PAL readiness/callback entry/result, final pointer, constructor marker, and first failure operation/branch/status/source. The kernel's parallel backend record has eight fixed slots and 34 fields per attempt. Neither record allocates, formats strings, throws, logs from the allocator path, accesses a managed singleton, or changes the allocation request/result. It uses no new lock or synchronization primitive; the runtime record is scoped by explicit begin/complete calls around the event `new` expression during early `RhInitialize` startup. Capacity exhaustion and inconsistent fields are marked incomplete/overflow.

The constructor marker in the guest build is placed after the `new` expression and only recorded for non-null `impl`; the C++ expression itself controls constructor invocation. Hosted tests do not instantiate the private `GCEvent::Impl` type, so their constructor assertion is record-level evidence. The source control flow and candidate instrumentation establish the guest constructor condition.

## Hosted regressions and build qualification

The hosted runner passed and records `qemuLaunched=false`, `qacReserved=false`. It ran:

- Fixed-record unit tests for success, captured backend failure status through a modeled cleanup return, PAL-not-ready, and bounded-record overflow.
- An integration executable linked to the candidate's actual global nothrow `operator new`, startup allocator, PAL contract, and record. It verified an 8-byte request becomes 24 bytes, successful pointer propagation, null propagation, and no constructor marker on null. Its refusal case is explicitly a **synthetic PAL callback refusal**, not a guest allocator failure. It also checked the actual PAL-not-ready path.
- Analyzer fixtures classified as `SUCCESS`, `VM_COMMIT_BRANCH_0x0413`, `VM_RESERVE_BRANCH_0x0408`, `KERNEL_PAL_SLOT_CAPACITY`, `PAL_NOT_READY`, `INCOMPLETE_MISSING_RUNTIME_RECORD`, and `INCOMPLETE_DIAGNOSTIC_RECORD`. Reserve/commit/capacity guest cases are synthetic serial fixtures; they validate classification, not execution of the bare-metal VM backend.
- The original R2R1 serial hash was rechecked and unchanged.

Historical 30-assertion semi-shared record tests, 11 prior branch fixtures, GC initialization probes, ThreadStore/PAL, QAC/QMP, C188 lifecycle/factory, and 40-suite/969-case UI results remain preserved in their earlier qualification evidence. They were not rerun in this hosted-only allocation phase; no QAC/QEMU-dependent regression was run.

Diagnostic and proof-disabled production NativeAOT and kernel builds succeeded. The final logs contain 62 diagnostic NativeAOT warnings, 220 diagnostic kernel warnings, 64 production NativeAOT warnings, and 425 production kernel warnings; none contains compiler error diagnostics. The hosted integration compile log has no warning or error diagnostics. The build's PE import assertions passed, including the forbidden `ucrtbase.dll`, `msvcrt.dll`, and `ntdll.dll` check. The ELF inspection reports no dynamic section and no relocations. Production isolation checks report diagnostic and production outputs separate, diagnostic instrumentation absent from production, no proof-only runtime profiles in production, and `persistentCompositeLifecycle=true`. The selected operator symbol/signature is unchanged.

## Staged candidate

Candidate directory:

```text
out/dotnet/c188r12r4r5r4r5r2r2-native-allocation-attribution/candidate-20261102T022027433Z
```

The authoritative candidate manifest is `candidate-manifest.json`; it binds the source HEAD, runtime commit, diagnostic/production provenance, test results, hashes, isolation checks, and hosted-only state. Principal SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | `B356F869D3E29187857F4FCAE50A2F3C11EEDAAD9A9361D515E15A057FDF4A16` |
| Diagnostic NativeAOT PE | `1CB1061DD6F22126A87F00904E946927D1F3D5E699C4221B0782817ECE24F2D4` |
| Diagnostic link map | `5EAD3EA8DBBCE742051B47DF493D080FF5A1FF7B8AD888537B598C1052B9DF50` |
| Diagnostic kernel ELF | `CBB363EA24C1346DE885B8A1D408B1AA043539934C37D63D43E3E5420C3F79B8` |
| Production NativeAOT ELF | `F0E32D79CA5A4DB7FBF85F115690B84BB46D77517F4C5A27C9575F12886DAC0B` |
| Production kernel ELF | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Staged/source RAM disk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Linked allocation disassembly excerpt | `79F52044AD4EC0E8C9059F6CFCBA5BB7C8BED3927265CAA0AB28AA18671CC4D6` |
| Hosted fixed-record test executable | `5E70ABC9ACA717B8B3771EA6598C4C6C62A576B96DE06B8D4569ED2E4BC82C35` |
| Hosted actual-operator integration executable | `D3F51885734050AB712D00716F458ABE02C75E606AEFF6F0AB87830417BC0AEA` |

The candidate includes the analyzer, branch map, linked disassembly excerpt, hosted test output, runtime pack provenance, diagnostic kernel/EFI artifacts, production qualification outputs, and a complete artifact manifest. It is staged for review only and was not booted.

## Next guest phase and conclusion

The next phase is **`C188R12R4R5R4R5R2R2R1_Native_Allocation_Failure_Guest_Attribution`**. It must first perform fresh QAC admission and ownership checks, create a new one-boot guard, and boot this exact candidate once. Capture and preserve raw serial separately from analysis; correlate the runtime first-failure operation/branch/status with the kernel reserve/commit/rollback record; preserve the existing semi-shared and GC-init records; verify null propagation and cleanup or report successful initialization; then shut down only the owned guest and verify QAC release. Do not retry automatically or change the allocator during that run.

The smallest justified correction in this hosted phase is **no allocator correction**. The symbol and full backing route are proven, and the new record distinguishes PAL readiness, callback null, kernel slot capacity, virtual-range/metadata failures, physical frame exhaustion, and page/zero/protection failures. Which branch explains the original guest null remains unknown until a fresh one-boot guest capture.
