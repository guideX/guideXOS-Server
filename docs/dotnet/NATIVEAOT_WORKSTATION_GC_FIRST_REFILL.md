# NativeAOT Workstation GC first allocation-context refill

Status: 2026-08-01. This report records the bounded repeated primitive-array
experiment requested after the first real Workstation-GC allocation milestone.
It uses the locked NativeAOT/runtime-pack identity, the active PAL replacement,
fresh disposable QEMU processes, and no GC or finalizer signaling.

## Decision

**Outcome A — bounded primitive-array allocations completed through the first
subsequent allocation-context refill.**

The experiment performed 15 managed `byte[256]` allocations in each of three
fresh QEMU processes. The first allocation established the initial collector
allocation context, 13 allocations used the fast context path, and the next
allocation exercised the first later refill. The object returned from that
refill was validated before the managed loop stopped. No allocation after that
validated object was permitted.

The decision is bounded to this experiment. It does not authorize a normal
managed runtime, `GC.Collect`, finalizer signaling, same-process GC shutdown,
reinitialization, or allocations beyond the validated refill boundary.

## Outcome gates A–E

| Outcome | Meaning for this experiment | Result |
| --- | --- | --- |
| A | Primitive arrays complete through the first subsequent refill with exact counters, geometry, ownership, and no collection | **Selected** |
| B | Startup or only the initial collector allocation is proven | Superseded by A |
| C | The real allocation path does not return before the bounded timeout | Historical first-allocation result; not the current result |
| D | A collection, finalizer activity, or forbidden shutdown boundary is reached | Not reached |
| E | An exact artifact, source, harness, or reproducibility blocker prevents a valid result | Not reached |

The stopping rule was enforced at the first validated object returned after the
later context refill. The managed body did not retain the arrays and did not
attempt a third context refill.

## Locked identity and selectors

The run used:

- core baseline `95580df6872e85527d27526b78ae3cdcee25dd53`;
- closure verification `c4096f2`;
- NativeAOT source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`;
- runtime pack `9.0.0`, AMD64, Workstation GC interface 5.3, EE 2;
- one Workstation heap, with server, concurrent, and background GC disabled;
- active PAL archive SHA-256 `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`;
- PAL ABI v1 size 232, capability mask `0x1FF`;
- GC-startup extension v1 size 216, capability mask `0x7`;
- process-lifetime GC ownership, with QEMU process exit as teardown;
- `GXOS_NATIVEAOT_GC_STARTUP_QEMU_TEST=1` and
  `GXOS_NATIVEAOT_GC_FIRST_REFILL_QEMU_TEST=1`.

The normal kernel identity used for restoration was
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`.

The normalized adapted-GC identity is the authorized `A-complete-gcenv-object`
identity in
`out/dotnet/gc-first-real-allocation/identity/build1/stock/identity.json`.
It records source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, the
unchanged stock archive, the reviewed replacement-object set, and the
prohibited-inventory count. The current first-refill archive is a separate
experiment artifact because it contains the first-refill diagnostics.

## Managed experiment and source path

The managed body creates a fresh primitive array with length 256, validates
initial zeros, writes a deterministic byte pattern, and asks the native
validator to check the object. It then repeats only until the native loop
status reports the first validated object after the later refill.

The source path proven by the map and diagnostics is:

```text
ManagedMain
  -> RhpNewArray
     -> guideXosStockRhpNewArray
        -> RhpNewArrayRare              (context exhausted)
           -> RhpGcAlloc
              -> GcAllocInternal
                 -> GCHeapUtilities::GetGCHeap()->Alloc(
                      pThread->GetAllocContext(), cbSize, uFlags)
```

Fast allocations consume the current `gc_alloc_context` directly. The rare
path enters the real Workstation GC allocation interface; the wrapper records
the context before and after the call and classifies the returned object as a
real GC allocation. The old image-backed bounded heap is not used.

## Exact allocation and context accounting

The common first-run serial log is
`out/dotnet/gc-first-refill/run-20260801-110102440/first-run/serial.log`.
All three runs reported the same final counters:

| Counter | Value |
| --- | ---: |
| Managed entry | 1 |
| Allocation requests | 15 |
| `RhpNewArray` entries | 15 |
| Fast allocations | 13 |
| Expected fast allocations | 13 |
| Rare-path entries | 2 |
| Real GC allocations | 2 |
| Slow allocations | 2 |
| Allocation-context refills | 2 |
| Hard allocation limit | 15 |
| Refill 2 attempted/returned | 1 / 1 |
| New context supplied/published/changed | 1 / 1 / 1 |
| Managed stop observed/no post-refill allocation | 1 / 1 |

The two refills are the initial context establishment and the first later
refill. Therefore the exact sequence is one initial real-GC allocation, 13
fast allocations, and one real-GC allocation returning the validated refill-2
object.

## Object geometry and ownership

The managed array is a non-LOH primitive array. Its source-derived size is

```text
ALIGN_UP(baseSize + componentSize * length, pointerAlignment)
= ALIGN_UP(0x18 + 1 * 0x100, 8)
= 0x118 (280) bytes
```

| Check | Refill-2 result |
| --- | --- |
| Object | `0x100A00F90` |
| Object end | `0x100A010A8` |
| Derived object size | `0x118` / 280 bytes |
| Initial available context | `0xE90` / 3728 bytes |
| `floor(0xE90 / 0x118)` | 13 fast allocations |
| Before-refill remaining context | `0x58` / 88 bytes |
| Alloc pointer before refill | `0x100A00F78` |
| New alloc pointer after refill | `0x100A010A8` |
| New alloc limit | `0x100A02FF0` |
| Segment base / allocated / reserved | `0x100A00028` / `0x100A00028` / `0x100B00000` |
| Below large-object threshold | PASS; 280 < 85,000 |

The initial and refill-2 segment addresses are the same. The collector supplied
and published a larger allocation context in that segment; it did not switch
to a private old heap or fabricate an object outside the GC-owned range.
`ownershipModel=1` records the direct-before-context-publication ownership
model used by the validator. Zero initialization, deterministic pattern,
layout, overlap, monotonicity, context geometry, primitive-array shape, and
heap ownership all passed with zero failures.

## Collection, finalization, and shutdown boundary

Each run reported:

| State | Value |
| --- | ---: |
| Collections considered | 2 |
| Collection requests / entries | 0 / 0 |
| GC count before / after | 0 / 0 |
| Finalization scans | 0 |
| Managed finalizers | 0 |
| Finalizers executed | 0 |
| Suspension requests | 0 |
| GC-lock transitions | 0 |
| Helper wakes | 0 |

The finalizer worker remained parked. Runtime-level orderly shutdown remains
**NOT SUPPORTED** by the locked source contract; the runner kills each QEMU
process after the pass and verifies process teardown. No second
`RhInitialize`, `RhShutdown`, or same-process reinitialization was attempted.

## Fresh QEMU matrix and hashes

The runner executed `first-run`, `repeat-1`, and `repeat-2` as separate fresh
QEMU processes. Each serial log contained `ALL_PASS`, exact counter PASS,
context-geometry PASS, no-collection PASS, primitive-array ownership PASS, and
process-teardown PASS.

| Artifact | SHA-256 |
| --- | --- |
| Repeated-allocation NativeAOT PE | `E565D47CC29BC9F63DDDE07F38B2F8883A3B78E32A1F56AFACB6680CED6977BE` |
| Converted ELF | `051B0A08C94EB0C6649306019FCD9565F9DE24E5F52A838C3EEFFFE3096BAB20` |
| Adapted first-refill GC archive | `6AF03BEA1611A422193A002A442C4386E4453B3E9BEE969C8E57063DC0DDA2A3` |
| Active PAL archive | `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F` |
| Embedded artifact object | `9B4D74116822EAA9FE9BA155E86D4B4548D3AB8FD1E2DB7A876D55068D35B277` |
| Experimental kernel and ESP image | `23C00DC238D1C1D07FF088270AEB02EBD94208EE1660CCB71B9413A34698CFD1` |
| Restored ordinary kernel and ESP image | `D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C` |

The three serial SHA-256 values from the authoritative rerun are:

| Process | Serial SHA-256 |
| --- | --- |
| first-run | `452B7937272A43403E574AF85545B394F38F769CBCF5F822CCA145FC45C50E48` |
| repeat-1 | `146B6FE4E9833728EECC1377D4412544745AEE6302BCCCA917D70C051BDB811B` |
| repeat-2 | `D80D5C366406228AA9D994A0F617F0C7E6BB2B769FFE8B0196CBB661F1EB7896` |

The repeat-2 serial stream has harmless timer-interrupt interleaving in one
printed counter line; the finalizer record, parsed loop status, and `ALL_PASS`
marker are intact.

The PAL replacement object hashes are preserved in the runner manifest under
`palReplacementHashes` and include `guidexos_gcenv.obj`, `guidexos_mutex.obj`,
`guidexos_event.obj`, the NativeAOT critical-section/event/FLS/thread/virtual-
memory adapters, `guidexos_native_thread.obj`,
`guidexos_virtual_memory_region.obj`, `guidexos_local_storage.obj`, and
`guidexos_gc_platform_services.obj`.

| PAL replacement object | SHA-256 |
| --- | --- |
| `guidexos_gcenv.obj` | `5C01020E9E81077A51703903EECF929F7CC27F39D1EA844BC8713B15B6C706D6` |
| `guidexos_mutex.obj` | `D630A1D3CEFF6C191CF98D1278473C060861682010F078539478D6C1055B68C1` |
| `guidexos_event.obj` | `EE74472760E8384739EC133EC451068F70F5449F956CB1898DB87F14CB507AF5` |
| `guidexos_nativeaot_critical_section_adapter.obj` | `D38007ACDA613520AB276F3A9A1F6C6451E526929D003691E2D7D060A32A098C` |
| `guidexos_nativeaot_virtual_memory_adapter.obj` | `3CB12F7DF624F53D469C485D2AB230F86846BF9AC5D93206E80DBAAADABC76A7` |
| `guidexos_nativeaot_event_adapter.obj` | `22BD28A6017692D265276DE014A3972AEFF022DDA50D2B009A3BF101BCAB3DDA` |
| `guidexos_nativeaot_fls_adapter.obj` | `A66952D40DABC7848470E849BB170A2578565BA120F8946C86EDF39F1704DBEF` |
| `guidexos_nativeaot_thread_adapter.obj` | `7E393B188832231AC189A64128BA752520F6659EE0D444CDDD04B6F15C32EB73` |
| `guidexos_native_thread.obj` | `4ADC494189FA5AC6F572A4D7C83668CC1C37EB52A3ED6BF5D5534CC56FD6C2C8` |
| `guidexos_virtual_memory_region.obj` | `3068B95E35570150E80634789CC68E13A80138566E33A37CEB1C88976464F185` |
| `guidexos_local_storage.obj` | `ED7E36941EB925C24AE51F6793248343006C32563AB717088A43F0908689DB4C` |
| `guidexos_gc_platform_services.obj` | `545CCAE753B90094F973C32C19CE4590C8A3DC3B97E736C3C1212AD9B241BE45` |

## Historical no-GC proofs and regressions

The existing image-backed managed proofs remain separate regression evidence:

- 64 KiB repeated allocations: 234;
- 4 KiB repeated allocations: 14;
- controlled OOM: PASS;
- collections: 0;
- GC-backed allocations: 0;
- heap expansion: 0.

Those proofs are not substituted for the real collector result. The dedicated
first-allocation runner remains the one-object regression baseline, while this
runner adds the first-refill selectors and exact refill diagnostics. The
ordinary kernel is restored in `finally` and its SHA-256 is checked against the
normal identity above.

The post-change regression matrix also passed the startup-only three-process
QEMU probe, the prior first-allocation three-process QEMU probe, the current
64 KiB repeated-allocation execution/OOM probe, and the generic managed
host-log artifact/execution baseline. The current 4 KiB artifact rebuilt and
passed static envelope validation (`4096` heap bytes, 280-byte object, 14
expected arrays, 176 bytes remaining). Its live server session exited with
Windows status `0xC0000419` before managed diagnostics; the preserved
historical 4 KiB execution evidence remains the bounded-mode PASS and this
unrelated live revalidation is not used to promote or demote the real-GC
first-refill decision.

## Reproduction and evidence

Run from the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-nativeaot-gc-first-refill-qemu.ps1 `
  -RepoRoot (Get-Location).Path `
  -EvidenceRoot .\out\dotnet\gc-first-refill `
  -TimeoutSeconds 45
```

The authoritative run manifest is
`out/dotnet/gc-first-refill/run-20260801-110102440/manifest.json`. It records
the normalized adapted-GC identity, PAL replacement hashes, repeated PE,
converted ELF, experimental kernel/ESP hashes, three fresh-process serial
hashes, and the restored ordinary-kernel hash. Evidence under `out/` is
ignored/local; the source runner and this report are the reproducible inputs.

## 4 KiB exit regression closure audit

The initially reported live bounded 4 KiB rerun exited through the opaque
value `0xC0000419` before managed diagnostics. That observation remains part
of the historical record; it was not reinterpreted as a Windows exception and
was not erased.

The immutable 4 KiB PE/ELF, preserved runtime pack, preserved kernel/ESP, and
the source-rebuilt Server were then exercised through raw Server capture and
three fresh runner processes. Every captured layer returned `0x00000000`;
every positive Server session reported 14 successful arrays, controlled OOM,
zero collections, zero GC-backed allocations, zero expansion, and `Exited`
cleanup. The 64 KiB proof remained at 234 allocations.

No current source, managed marker, Server marker, serial log, raw process
capture, or PowerShell capture generated `0xC0000419`. The first producing
layer therefore remains unobserved, and no correction was justified. The full
classification is documented in
[NATIVEAOT_4K_PROOF_EXIT_REGRESSION.md](NATIVEAOT_4K_PROOF_EXIT_REGRESSION.md).

The first-refill Outcome A result remains technically valid. First-refill
milestone closure is pending this separate 4 KiB exit classification; multiple
subsequent refills, segment transitions, new page commitments, collection, and
allocation beyond the established first-refill boundary remain unauthorized.
