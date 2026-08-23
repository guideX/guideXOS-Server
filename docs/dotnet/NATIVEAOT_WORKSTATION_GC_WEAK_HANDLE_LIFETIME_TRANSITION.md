# NativeAOT Workstation GC: C011EC33 weak-handle lifetime transition

## Result

C011EC33 reached Outcome D, success Level 1. One genuine short-weak handle was preserved by the production Workstation GC during Collection 1 while a managed GC-info root was live. Collection 1 then failed to return from the first relocation root scan, so the managed lifetime boundary could not be crossed and Collection 2 was not started.

The requested live-to-dead transition is therefore not claimed. In particular, `C011EC33-PREFLIGHT` and `C011EC33` were not emitted.

Evidence root for the final diagnostic boot:

`out/dotnet/c011ec33-short-weak-lifetime-transition/run-20260822-204955206`

Serial evidence:

`out/dotnet/c011ec33-short-weak-lifetime-transition/run-20260822-204955206/first-run/serial.log`

Manifest:

`out/dotnet/c011ec33-short-weak-lifetime-transition/run-20260822-204955206/manifest.json`

## Locked identity

- NativeAOT `9.0.0`
- AMD64
- Workstation GC
- GC interfaces `5.3 / 2`
- locked runtime source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- QEMU `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`

No runtime or GC-flavor change was made.

## Managed lifetime design

The C33 workload uses one no-inline helper, `CreateAndRunLiveCollection1`, which:

1. creates one `byte[64]` target;
2. allocates one real `GCHandleType.Weak` handle;
3. records scalar target/type/slot identity through the native diagnostic bridge;
4. keeps the managed target local while allocation pressure triggers Collection 1;
5. returns only scalar identity metadata after Collection 1 completes.

The outer managed frame retains no target reference and does not recreate or free the weak handle. The intended lifetime boundary is helper-frame return, not assignment of `null` in the same frame. This workload could not reach that boundary because Collection 1 did not restart the EE.

## Setup and Collection 1 evidence

The managed target type is `byte[64]`. The final C33 live marker recorded:

| Evidence | Value |
|---|---:|
| target address | `0x0000000100A01F38` |
| weak slot address | `0x00000001040213F8` |
| weak slot before | `0x0000000100A01F38` |
| weak slot after | `0x0000000100A01F38` |
| handle type | `HNDTYPE_WEAK_SHORT = 0` |
| target root matches | `1` |
| root kind | `2` (stack root) |
| root slot | `0x0000000004EA9A70` |
| root value | `0x0000000100A01F38` |
| ControlPC | `0x0000000010001BE9` |
| method interval | `0x0000000010001B20..0x0000000010001CC2` |
| method info | `0x0000000004EA93D8` |
| GC-info | `0x000000001011E6EF` |
| safe point | `0x0000000010001BE9` |
| target Promote count | `1` |
| mark word before/after | `0x000000001026D241 / 0x000000001026D241` |
| mark mask | `0x1` |
| target marked | `1` |
| liveness callbacks/decisions | `1 / 1` |
| `IsPromoted` result | live (`1`) |
| weak preserved | `1` |

The allocation path was the production path:

`GCHandle.Alloc(target, GCHandleType.Weak)` → `RhpHandleAlloc` → `CreateHandleOfType` → `HndCreateHandle`.

This is the same short-weak semantic established by C011EC31 and C011EC32. No normal handle was used for the primary C33 proof.

`C011EC33-LIVE` was emitted only after the real short-weak production callback returned live and the slot remained unchanged. The marker reported `collectionCompleted=0`, `restartReturns=0`, and `managedResume=0`.

## Collection 1 completion blocker

The post-weak phase probes are scalar, C33-only observers and do not bypass collector phases. The final run reached these boundaries:

| Phase | Meaning | Result |
|---:|---|---|
| `0x01` | short-weak scan returned | reached |
| `0x02` / `0x03` | finalization entered / returned | reached |
| `0x04` / `0x05` | long-weak scan entered / returned | reached |
| `0x06` / `0x07` | sync-block weak scan entered / returned | reached |
| `0x08` | `plan_phase` entered | reached |
| `0x0A` | `relocate_phase` entered | reached |
| `0x0D` | `GCScan::GcScanRoots(GCHeap::Relocate, ...)` entered | reached |
| `0x11` | `GCToEEInterface::GcScanRoots` entered for relocation | reached |
| `0x12` / `0x0E` | relocation root scan / EE root scan returned | not reached |

The first authentic non-returning subsystem is the EE root enumeration invoked for relocation root updating:

`GCScan::GcScanRoots(GCHeap::Relocate, condemned_gen_number, max_generation, &sc)` → `GCToEEInterface::GcScanRoots`.

No `GcDone`, `RestartEE` entry/return, managed resume, relocation handle scan, compaction completion, or Collection 1 completion was observed. This is the exact C33 blocker, not a weak-handle predicate failure.

The retained EE/ThreadStore guards remained in the expected suspended state: EE suspended, ThreadStore lock held, cooperative mode active, preemptive mode inactive, managed entry prohibited, and no restart/resume before the stop. No managed allocation, handle allocation/free, diagnostic handle write, diagnostic mark write, or dynamic diagnostic string was introduced in the suspended path; the phase hooks publish scalar state and static serial text only.

## Collection 2 status

Collection 2 did not start. Consequently the following are intentionally `not reached`, not zero-valued proof results:

- helper/frame return after a completed Collection 1;
- structural absence of the final strong root;
- stack/register, static/ThreadStatic, normal-handle, and graph-derived target-root counts for Collection 2;
- Collection 2 mark closure;
- `C011EC33-PREFLIGHT`;
- the second `CheckPromoted` → `GCHeap::IsPromoted` decision;
- same-slot clearing and `C011EC33`.

The same weak slot was proven through Collection 1 preservation. A relocation target address update was not reached, so relocation of the target itself is undetermined by this milestone.

## Validation and regressions

Multiple bounded one-boot diagnostic attempts were used while narrowing the blocker; the final reporting-validation boot reproduced the same Level 1 sequence and phase-`0x11` stop. The full three-fresh-boot requirement applies to Outcome C and was not represented as satisfied because Outcome D blocks before Collection 1 completion.

The final proof artifacts were:

- proof kernel SHA-256: `6082BD2E1F27F943DDC35E4564284B73B53D0FA01E4434FEE8D2CF2498A8D522`;
- PE SHA-256: `202BFE8A9A6BE4D206746B71658E690209A9839BF772A8F3453ADBA9C3DE797C`;
- ELF SHA-256: `45723F1C4789E7BCE82EC5DCEF1D9D115A01947772EB1D3A07A6A2E3D18B29A6`;
- MAP SHA-256: `1F2EECB9A716A7A1619B427935E490C51570D8EF0CA6ED633ECDF218C4C11A99`;
- final serial SHA-256: `F3C3CC92325ECA3D8EFBAE2F781A0E34358EC25960DA9766DD3D05BAFCD73C6D`.

C19–C32 chronology guards, complete stack walk, native unwind provider, mark closure, graph traversal, C29 post-mark boundary, C30 handle topology, and the C31/C32 weak semantics remain guarded and preserved. C31 live-preservation and C32 dead-clearing results remain the authoritative prior evidence; this blocker pass did not silently relabel them as fresh three-boot reruns. The converter, linker/table checks, locked-source injection checks, and PowerShell parse passed. `git diff --check` passed.

## Ordinary artifacts and cleanup

The ordinary pre-proof source-state hash was recorded as:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

After the proof attempt, both `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf` were restored to that hash. Repository QEMU processes were terminated by the harness; unrelated QEMU VMs were left running.

## Git and next milestone

The task started at `1010a6fcaed3ef3ac66ccbf96fafa79b4c48bebf` on `v1.1_DOTNET_SUPPORT`, tracking `origin/v1.1_DOTNET_SUPPORT`. The actual starting divergence was `0 ahead / 0 behind`, despite the requested expected divergence of ahead 2; repository state was authoritative.

The next smallest milestone is to make the locked Workstation relocation root scan return normally—starting at `GCToEEInterface::GcScanRoots` during `relocate_phase`—while preserving the C33 Level 1 live-preservation evidence. Only after that phase completes can the helper return, Collection 1 restart the EE, and C33 proceed to the last-strong-reference Collection 2 proof.
