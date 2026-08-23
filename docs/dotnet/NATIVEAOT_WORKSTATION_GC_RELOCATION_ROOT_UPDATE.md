# NativeAOT Workstation GC relocation-root update — C011EC34

Date: 2026-08-23  
Result: **Outcome B — the authentic relocation root callback returned and rewrote the root slot**

## Scope and identity

This checkpoint continues C011EC33. It uses the locked NativeAOT 9.0.0 AMD64 Workstation GC identity, with GC interfaces `5.3 / 2`; it does not replace the runtime, introduce a synthetic root, skip `GCToEEInterface::GcScanRoots`, or call a manual relocation helper.

The locked runtime source commit is `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The repository was clean with no ahead/behind divergence at the start of the focused implementation run: branch `v1.1_DOTNET_SUPPORT`, upstream `origin/v1.1_DOTNET_SUPPORT`, HEAD `7bfe83da75b07a2dff3785c832283b2cbfeac15d`.

## Authentic source path

The proof follows the locked source contracts:

* `src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:94-113` — `GCToEEInterface::GcScanRoots` enumerates the ThreadStore and calls `Thread::GcScanRoots`.
* `src/coreclr/nativeaot/Runtime/GcEnum.cpp` — the managed root callback reports the slot and invokes the supplied `ScanFunc`.
* `src/coreclr/gc/gc.cpp:49546-49596` — `GCHeap::Relocate(Object**, ScanContext*, uint32_t)` receives the root slot and delegates to `gc_heap::relocate_address`.
* `src/coreclr/gc/gc.cpp:35907-35972` — the brick-table/tree relocation lookup computes the new address and updates the slot.

The C34 additions are proof-only and bounded. They record the scan context, callback addresses, first target root, relocation mode, callback entry/return balance, lookup entry/return balance, brick-table metadata, and the before/after slot values. The C34-only unwind accommodations suppress earlier C026/C025 diagnostic safe-stops during the already-entered relocation walk; they do not alter GC root enumeration, relocation decisions, or managed object data.

## Fresh QEMU evidence

Command:

```powershell
& .\scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1 `
  -ProofMode relocation-root-update -FreshBootCount 3 -TimeoutSeconds 90
```

QEMU: `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

Evidence root: `out/dotnet/c011ec34-relocation-root-update/run-20260823-002450290`

Manifest: `out/dotnet/c011ec34-relocation-root-update/run-20260823-002450290/manifest.json`

All three fresh boots emitted `C011EC34 outcome=B successLevel=1`. The semantic fields were identical across boots:

| Field | Observed value |
|---|---:|
| `condemnedGeneration` | `0` |
| `maximumGeneration` | `2` |
| `compacting` / `relocating` / `promotion` | `1 / 1 / 0` |
| `gcScanRootsEntries` / `gcScanRootsReturns` | `1 / 1` |
| first root kind | `2` (stack/managed callback classification used by the preceding proof) |
| old root | `0x0000000100A01F38` |
| new root | `0x0000000100901F50` |
| root before / after | `0x0000000100A01F38 / 0x0000000100901F50` |
| planned-to-move / rewritten | `1 / 1` |
| callback entries / returns | `4 / 4` |
| unchanged / rewritten roots | `3 / 1` |
| relocation lookup entries / returns | `1 / 1` |
| lookup successes / failures | `1 / 0` |
| EE suspended / ThreadStore lock held / managed entry prohibited | `1 / 1 / 1` |
| safe-stop reason | `0` |

The first-target root callback returned normally. The C34 completion marker was emitted only after `GCToEEInterface::GcScanRoots` returned; the harness then terminated QEMU at the proof marker.

Serial SHA-256 values:

* first-run: `D672B9E09D380DA4B84AC77550EFAF4A3F62EC727711757ACC2CC5BF7E8063DC`
* repeat-1: `7F2EC68104C2790D14570518BBDD8970E6E65FF3FC50D36E4FF91C96C978DA0B`
* repeat-2: `CEA8CE99C32F005EF07C6160EE4D57310F28CC2E3008498351DB9CC68C1D596C`

Proof payload hashes:

* kernel: `E0BCF719F8094BFF5234ECD6B4FE8198476F7B4C55DB78C413AE467777A3810C`
* PE: `33EDAE55F9D5F22FEB099BF549182C75FF0BE375820780C7FB5A1D0972A545C5`
* ELF: `77521CE0B6F60FF57FACDC407903F7035828E50CD27F4D3AE6878C158A820291`
* MAP: `B71A5901B0977D2295C74CBBD6C40EBF14214B260D4E077E554D7B3E5184C3CA`

## Regression and restoration notes

The run retained the C011EC33 live predecessor chronology and reached the real C34 relocation phase. The PE-to-ELF converter, locked-source guards, symbol audit, ordinary restoration, and `git diff --check` passed. The ordinary kernel was restored in both build and ESP locations with SHA-256 `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.

The three earlier one-boot attempts are retained under the same evidence root as development diagnostics: source-boundary/compiler corrections, C026/C025 proof-only safe-stop isolation, and serializer/assertion corrections. The final manifest is the authoritative focused result.

