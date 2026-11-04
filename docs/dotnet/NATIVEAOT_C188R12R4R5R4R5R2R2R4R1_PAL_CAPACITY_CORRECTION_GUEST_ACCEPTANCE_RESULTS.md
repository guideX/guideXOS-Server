# C188R12R4R5R4R5R2R2R4R1 PAL Capacity Correction Guest Acceptance Results

**Outcome: `PAL_CAPACITY_CORRECTION_CONFIRMED; NATIVEAOT_STARTUP_ACCEPTANCE_INCOMPLETE`**

The prepared one-boot procedure ran against the exact R4 candidate. The guest crossed the old 16-slot PAL boundary, completed the previously failing 24-byte backing request, returned a valid pointer for the 8-byte `GCEvent::Impl`, completed `init_semi_shared()`, `GCHeap::Initialize()`, and `RhInitialize`. Module registration also completed. The next observed boundary was a fail-fast during the module-initialization transition; `InitializeModules`, ThreadStore handoff attachment, and managed entry did not complete. The PAL capacity result is direct guest evidence. It does not establish GC acceptance.

## Repository and candidate identity

| Item | Result |
| --- | --- |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `9e7718771270bc4328540d72adf449631d7cae29` |
| Candidate | `out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/candidate-2/` |
| Candidate manifest SHA-256 | `5CCC2ECB50ED9776B5C502AF46A08680E21C8EE70B7D723F44E1194C63607C01` |
| Integrity inventory SHA-256 | `4FF88668F6C155BC8E105421D80FEAEB52E8393EE32ED32021720C60D3ACC5B7` |
| Diagnostic NativeAOT ELF SHA-256 | `210A7EDCC916A04CFF70C8E28429113DE147684129A3A08CBC9647BCF9A3D443` |
| Diagnostic kernel ELF and staged kernel SHA-256 | `8234961C77932A0BA16173FF702A71387F440AC0FC856F91A31FC926661F0AD9` |
| Source and staged ramdisk SHA-256 | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Candidate integrity | All 54 manifest-declared hashes and all 67 inventory path/role/size/hash rows matched. |

The R4 hosted preflight was `PAL_CAPACITY_FIX_HOSTED_READY`: the 256-slot table and lifecycle checks passed, including 2,425 slot-table assertions, 1,024 synthetic reserve/release cycles, and hosted linked `operator new(std::nothrow)` success/refusal/PAL-not-ready cases. These synthetic checks are not guest-memory evidence.

The prepared runner initially rejected this exact manifest because it expected a missing `nativeAllocationLinkedDisassemblySha256` field. I made a narrow preflight correction in `scripts/dotnet/run-c188r12r4r5-gc-ownership-qac-guest.ps1`: it now checks the candidate's hash-pinned NativeAOT linker map and the manifest-pinned source/symbol route from `GCEvent::Impl` through nothrow new and startup malloc to PAL virtual allocation. The corrected runner SHA-256 was `663549551E825A189DCBFEA25BBB89037E2874EBC9ABCC71D60AE1AA8C64D2F8`. No candidate, PAL, allocator, or kernel source was changed or rebuilt for this correction.

## Concurrency gate

The source ordering supported the planned startup reservation as a single-threaded experiment. `kernel/core/main.cpp` initializes the process foundation, then enters the diagnostic startup branch before the ordinary desktop/task path. The runner used QEMU `-smp 1`. The target 24-byte PAL request occurs synchronously in the `RhInitialize`/GC initialization path at the `GCEvent::Impl` allocation site. I found no known concurrent PAL table mutation path before that request. The table remains unsynchronized; this review does not qualify later runtime allocation, collection, or other concurrent PAL operations. No allocation stress or collection was initiated as part of this qualification.

## QAC admission and one-boot guard

- Fresh preflight status/list and process census showed no QEMU owner, reservations, managed sessions, queued sessions, or blocked sessions.
- Exclusive reservation: `aecd0b1ab9964162a7fd7cdccd31de28`.
- Owned QEMU child: PID `13964`; QMP port `54549`.
- Guard: `out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/guest-launch-attempt-C188R12R4R5R4R5R2R2R4.json`, SHA-256 `DB9611F6474CAE04C1618A82A34E355E22BD7F8964D00562ED4AF57BA8B60C13`; it pins the candidate and corrected runner and sets `maxGuestBoots=1`, `automaticRetry=false`.
- Exactly one QEMU boot occurred. The runner shut down that owned child with QMP `quit`; the log records `host-qmp-quit`. The after census reported zero QEMU processes and zero reservations. QAC release was verified.

## Guest milestones

| Milestone | Observation |
| --- | --- |
| PAL occupancy and former boundary | Backend attempt 6 had capacity `0x100` (256), `activeSlots=0x10` (16), and `freeSlotIndex=0x10` (slot 16). Attempt 7 recorded 17 active slots and free-slot index 17. The guest therefore allocated beyond the former 16-slot limit. |
| Previously failing backing request | Backend attempt 6 recorded `requestSize=0x18` (24 bytes), rounded size `0x1000`, reserve result `0`, commit size `0x1000`, commit result `0`, and final pointer non-null `1`. The target request succeeded through reserve and commit. |
| `GCEvent::Impl` allocation | Runtime attempt 0 recorded site `0x0200`, request size/alignment 8, PAL ready, callback entered, nonzero backend pointer, final pointer non-null `1`, and constructor entered `1`. The pointer fields combine to `0x000000010461B000`. |
| `init_semi_shared()` | Complete record; 31 operations, overflow 0, final result `1`, and cleanup completed successfully. Analyzer outcome: `init_semi_shared_succeeded`. |
| `GCHeap::Initialize()` | Complete internal record; final heap HRESULT `0x00000000`, `InitializeGC` HRESULT `0x00000000`, wait-event allocation/create succeeded, and first failure site was 0. Analyzer outcome: `gc_heap_initialize_succeeded`. |
| `RhInitialize` | Native boolean `1`, adapter status `0`, both reported success. The separate init trace reached its 64-record capacity and recorded overflow `0x19` (25 events); serial bytes were not dropped. |
| ThreadStore state and handoff | Runtime instance, ThreadStore, current-thread TLS, and current-thread state markers were observed. The later PAL/thread transition attach completion was not observed; readiness analysis reports `threadStoreHandoffReady=false`. |
| Module registration | `module_registration_enter` and `module_registration_complete` were observed; the completion marker reports success. |
| Module initialization and first later failure | `module_initialize_enter` was observed, followed by `transition_enter` and `[nativeaot-pal-qemu-test] FAIL_FAST reason=50414C06 detail=0000000000000000`. The thread-store analyzer classifies the boundary as `InitializeModules` / `MODULE_INITIALIZATION_INCOMPLETE`. No cause is assigned to the fail-fast code here. No module-initialization completion or transition-attach completion marker followed. |
| Managed entry | The gate opened, and the kernel issued `managed_main_call ordinal=00000001` and a `managed_dispatch` marker. The managed entry marker was absent; analysis reports `managedEntryReached=false`. Thus dispatch was attempted, but `ManagedMain` body entry was not reached. |
| GC acceptance | No GC ownership proof or collection record was produced. No stress/collection was started by the qualification. The startup and PAL results do not establish successful managed GC execution. |

The native-allocation record captured eight runtime and eight backend attempts; the backend summary reports overflow `1`. Its analyzer therefore classifies the whole allocation diagnostic as `INCOMPLETE_DIAGNOSTIC_RECORD`, even though the requested 24-byte operation and the `GCEvent::Impl` pointer/constructor success are individually present in the raw records. The higher-level runner result is `DIAGNOSTIC_INCONCLUSIVE`. The serial initialization trace also overflowed. These limitations and the later module-transition fail-fast keep overall NativeAOT startup acceptance incomplete.

The actual serial trace shows the gate opening and a managed dispatch attempt, despite the runner's pre-boot expectation that the candidate would remain behind the established thread-attachment gate. The module transition then stopped before managed entry. This behavior was not retried; the single-boot guard was consumed.

## Evidence and preservation

Run evidence root:

```text
out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/qac-guest-r2r2r4-001/
```

| Evidence | SHA-256 |
| --- | --- |
| Raw `serial.log` | `D9F14AC9DB9E39868D26F2CDBA798815D6A33C89A475DC9BEAFE998FFAD28FBF` |
| `native-allocation-diagnostic-record.raw.txt` | `A30C088981F6409D010667737F89906C4B1ABE3D216892CD1A4BAE0B9A8BD0CF` |
| `semishared-diagnostic-record.raw.txt` | `8BDCCAA6662345841846943AC426972289835F07B4C5F1C54D4A8DABFE71E0B8` |
| `gc-init-diagnostic-record.raw.txt` | `FD12632115CD104E39F23A8373BE6FBEAB69CD3569FF78241170074337AC6E63` |
| `qmp.log` | `9261D1BF5211A44D7C81D480F8C3D47C039A1EDFA4BEC5B5A3CD25166475F87A` |

The run root also retains the serial analyses, guest manifest, QAC before/during/after status and lists, complete QAC inspection/ownership evidence, process census and child identity, runner arguments, QMP log, and stdout/stderr. The serial hash before and after analysis is identical.

`ESP/ramdisk.img` retained the required SHA-256 above. The three previously modified tracked `.obj` outputs were left unstaged and unchanged from their recorded starting hashes:

- `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` — `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD`
- `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` — `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3`
- `guidexos_nativeaot_native_alloc_record.obj` — `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183`

## Closeout

The capacity correction passes its specific guest acceptance: occupancy reached and passed 16, the 24-byte backing request reserved and committed successfully, and `GCEvent::Impl` returned a valid pointer and entered construction. `init_semi_shared()`, `GCHeap::Initialize()`, `RhInitialize`, and module registration also passed their recorded milestones. The next authentic observed boundary is the `InitializeModules` transition fail-fast `0x50414C06`; its cause remains undetermined from this evidence. ThreadStore handoff readiness, managed entry, and GC acceptance remain unproven.

The smallest next action is to correct the diagnostic runner/profile so that successful `RhInitialize` stops before automatic managed dispatch unless the transition, ThreadStore attachment, module initialization, and root/suspension prerequisites have been independently qualified. Then prepare a new candidate/run identity and obtain separate authorization for any additional guest boot. Do not reuse this consumed one-boot guard.

The final repository HEAD after the documentation and narrow runner-preflight closeout is recorded in the task closeout and the generated run-root closeout metadata.
