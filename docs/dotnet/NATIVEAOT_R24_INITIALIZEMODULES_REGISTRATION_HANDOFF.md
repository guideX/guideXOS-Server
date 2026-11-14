# NativeAOT R24 — InitializeModules Registration Handoff

**Outcome G — `INITIALIZEMODULES_EVIDENCE_BLOCKED`.** The single R24 guest boot produced a CPU snapshot at the guideXOS fatal spin loop after the R23 `C188B007` marker. This identifies an executing failure state, but the captured registers do not identify its caller. R24 therefore does not establish `InitializeModules` entry, `RhpRegisterOsModule` entry, registration publication, or registration return. Static linked-code analysis found a real implementation with the expected ABI and publication behavior; no registration defect was demonstrated.

## Repository and preserved state

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `92e9246ffc941e71659d285055e8cd50e3a53e3d`
- R24 candidate source HEAD: `6c034235824471b5f7f2efb946d80ccd1267c64f`
- Final source/test HEAD before the report commit: `6c034235824471b5f7f2efb946d80ccd1267c64f`. This report is committed as the final repository commit; its exact HEAD is included in the closeout message.
- No push was made.

R23’s report, candidate, serial log, QMP log, and consumed guard remain preserved. Its report is [NATIVEAOT_R23_SETTINGS_MODULE_PREPARATION.md](NATIVEAOT_R23_SETTINGS_MODULE_PREPARATION.md). R23’s candidate reached `C188B007` immediately before the generated call, without a CPU snapshot. R24 retained that candidate’s real selector-5 Settings factory, Workstation GC, no image-backed C188 arena, and accepted R20 unwind adapter.

The three pre-existing modified object files remain unchanged:

| File | SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## R23 call boundary reconstructed from the linked R24 image

R24’s map and disassembly put the shared caller at `initializeRuntimeState`, address `0x1000f2f80`, and generated `InitializeModules` at `0x1000ad660`. The direct call is at `0x1000f3191`, resolves to `0x1000ad660`, and returns to `0x1000f3196`. This proves the linked call target and callsite layout; it does not by itself prove execution entered the target.

At the callsite, the Windows x64 ABI arguments are:

| Location | Value | Meaning |
| --- | --- | --- |
| `RCX` | `0x100000000` | OS image module handle |
| `RDX` | `0x100157030` | Start of module-header pointer array |
| `R8D` | `2` | Module-header count; array ends at `0x100157040` |
| `R9` | Caller’s class-library function table | Fourth argument |
| `[RSP+0x20]` before call | `16` | Fifth argument, class-library function count |

The caller reserves `0x68` bytes. The generated initializer saves its nonvolatile registers and the four register arguments, then reserves `0x40` bytes, for a `0x68`-byte frame. Its fifth argument is at `[entry RSP+0x28]`, which becomes `[current RSP+0x90]` after the prologue. The code preserves the required x64 shadow space and stack alignment.

The generated entry has a reverse-P/Invoke transition call at `InitializeModules+0x1b`. Its first source-body runtime import follows at `+0x23`, address `0x1000ad683`, with return PC `0x1000ad688`. The resolved `RhpRegisterOsModule` address is `0x1000f94d0`. Type-manager creation follows at `0x1000ad6a1`, targeting `CreateTypeManagers` at `0x1000ad820`. The imported function returns the module handle in `RAX` on success or null on allocation failure. The generated managed method returns `void` and does not consume this import’s return value; consequently, an observed return to the caller alone would need to be paired with the expected module handle to prove registration success.

## Pinned NativeAOT contract and linked implementation

The pinned runtime is `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. In the pinned [`StartupCodeHelpers.InitializeModules`](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Common/src/Internal/Runtime/CompilerHelpers/StartupCodeHelpers.cs#L910-L917), the method’s first managed body operation calls `RuntimeImports.RhpRegisterOsModule(osModule)`, followed by type-manager creation. The pinned [`RuntimeImports` declaration](https://raw.githubusercontent.com/dotnet/runtime/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/System.Private.CoreLib/src/System/Runtime/RuntimeImports.cs#L467-L469) declares an internal-call returning `IntPtr`. The pinned [`RhpRegisterOsModule` implementation](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/RuntimeInstance.cpp#L1370-L1385) allocates an `OsModuleEntry`, returns null on allocation failure, assigns the module handle, publishes the entry through the runtime instance’s interlocked list, and returns the handle. The [`OsModuleEntry` and list definition](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/RuntimeInstance.h#L456-L469) use an add-only list.

The R24 linked PE has no import-directory entries for this runtime import. Its call is a direct relative call to `0x1000f94d0`, whose map provenance is `Runtime.WorkstationGC:RuntimeInstance.cpp.obj`; the linked implementation was inspected as machine code, not inferred from the symbol name. It allocates a 16-byte entry by calling linked `malloc` at `0x1000f6640`, tests the returned pointer, writes the next and module-handle fields, obtains the runtime instance, and publishes with `lock cmpxchg`. The success path returns the original module handle in `RAX`; the null-allocation path returns zero. The linked allocator comes from `guidexos_nativeaot_startup_imports.production.obj`, obtains memory through the linked PAL virtual-allocation provider at `0x1000f55d0`, and requests `MEM_RESERVE | MEM_COMMIT` with `PAGE_READWRITE`.

Static checks found the expected direct call target, object provenance, register/stack arguments, saved-register behavior, allocator dependency, publication sequence, and return convention. The hosted ELF qualification reports 37 assertions passed. No dummy success path, import stub, ABI mismatch, invalidation of list publication, or proven source repair was found. These findings establish linked-code shape. They do not establish that the guest executed this function.

## CPU and serial evidence

The R24 serial log SHA-256 is `2EC81622655DC2B02DDC692CB498BC276504C7E5F7E3435DAF98DAC90B4E6344`. It contains `C188B006`, `C188B007`, and `[C188R11-HOST] stage=initialize-modules-call`. It contains no `C188B005`, `C188B002`, or Settings factory marker. `C188B007` remains callsite evidence. The last serial marker is not treated as the last executed instruction.

On readiness timeout, the runner captured QMP status as running, queried the single x86-64 CPU, stopped the VM, verified paused state, and captured HMP registers. QEMU reported `HLT=0`. At the paused snapshot:

| Register | Value |
| --- | --- |
| `RIP` | `0x1000f2f77` |
| `RCX` | `7` |
| `RAX` | `0` |
| `RSP` | `0x517f058` |

The linked map places `guideXosFailFast` at `0x1000f2f70`. Its body loads reason 7, executes `int 0x29`, and then loops at `0x1000f2f77`. The snapshot therefore identifies a fatal spin loop, with the CPU not halted. It does not include stack words or a return address. The shared fail-fast entry overwrites `RCX` with its reason, so the captured `RCX=7` cannot identify the caller. Source contains reason-7 validation paths in the managed allocation-report and array-host-log helpers; this snapshot does not attribute the failure to either path.

As a result, the captured CPU position is outside `InitializeModules` and `RhpRegisterOsModule`, and the snapshot cannot tell whether those functions were reached earlier, whether registration returned, or whether the loop came from a later startup callback. The archived wrapper classifier reports the RIP as outside its then-known initialization ranges; that is a capture-time classification gap for the fail-fast symbol, not proof that the initializer was skipped. No stack trace or managed factory execution can be recovered from the preserved snapshot.

## Native milestones

| Milestone | R24 result | Evidence |
| --- | --- | --- |
| M1 — runtime and code-manager preparation reached | Proven | `C188B006` and subsequent `C188B007` |
| M2 — generated `InitializeModules` entered | Not established | Callsite marker and later fail-fast RIP do not record entry or return address |
| M3 — `RhpRegisterOsModule` entered | Not established | No CPU snapshot inside the import |
| M4 — OS-module registration completed | Not established | No observed allocation and list-publication completion |
| M5 — registration returned to generated code | Not established | No captured caller continuation with module handle in `RAX` |
| M6–M11 — type managers, initializer return, managed handoff, Settings factory, runtime invariants | Not reached in evidence | No corresponding CPU/serial/factory evidence |
| M12 — R24 QEMU shutdown and reservation release | Proven for R24’s reservation | QEMU PID `28480` exited with code `0`; reservation `ac4a0572332c4bbca1cc64059ff61ce8` is logged released |

R24 meets outcome G because the only native CPU boundary cannot be attributed to `InitializeModules` or its import, and M2–M5 remain unresolved. It does not meet the registration-cause outcome: the guest has no evidence of an entry, ABI, allocation, or publication failure. It also does not meet the boundary-advanced outcome, which requires a concrete initializer milestone.

## Candidate, guard, and QAC

The booted candidate is:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r24-initializemodules-registration\candidate-20261114T201159975Z-141494eeb9f64abb942a6b979d59bfa5
```

| Item | SHA-256 |
| --- | --- |
| Candidate manifest | `26292FB8221C8198C0DC985C447DDA54920CB3AAE10702FA51D326254E1EC755` |
| Candidate integrity inventory | `8577B4D685A13423B165A862CF6BD228C925A860D72F61C1899D5652B9F8EE0A` |
| Kernel | `9931827D7F6186FF98E1DA580A0B3ED2EAAF725359A7EC096F0ED16ADF495D6B` |
| Managed ELF | `4A5A7C429D91C008B0AFFEE27BAF284EE281137A28B1F251E6AB2E5B2AC66A10` |

The integrity inventory contains 347 files. The manifest retains `GuideXosApplicationRegistry` selector 5 to the real `ManagedSettingsCenter` and `ManagedSettingsCenter___ctor_0`, with Workstation GC, no image-backed arena, and the accepted R20 unwind adapter. Candidate construction and runner-package rehearsal passed before the sole boot. The rehearsal validated 370 assertions, all 347 candidate files, and seven packaged runner/test sources without launching QEMU. The earlier R24 candidate at `candidate-20261114T200501465Z-9eba589bd22042bc89959c801fffcb21` was preserved and never booted.

The consumed guard is `one-boot-guards/attempt-r24-fda6296dbc6e47b1a134ea9f8c99a0f1.json`. It records `attemptNumber=1`, `maxGuestBoots=1`, `automaticRetry=false`, `retryAllowed=false`, and `consumed=true`. The guard binds the booted candidate’s manifest, inventory, kernel, and managed ELF hashes. No retry or second guest boot was made.

QAC preflight was clear before launch. R24’s own reservation `ac4a0572332c4bbca1cc64059ff61ce8` was granted, its QEMU child exited with code zero, and the QAC child log records release of that same ID. The wrapper’s global post-run census returned `qacReleaseVerified=false` because another development reservation was active by then. The preserved post-run status/list identify that different reservation as `c5af4eb3d4e8473dab1926310214b49c`, project `guideXOSServer`, mode `development`, PID `27764`. It arrived after R24 preflight and was left untouched. R24’s own QAC shutdown and release are independently verified; global QAC state was not empty after the run.

## Hosted qualification

All requested focused R24 and regression checks passed. Reported totals:

| Suite | Result |
| --- | --- |
| R24 linked ELF reconstruction and ABI/publication assertions | 37 passed |
| R24 CPU snapshot predicates | 14 passed |
| R23 preparation-boundary regression | 12 passed |
| R22 bootstrap handoff regression | 29 passed |
| R21 Settings factory linkage | 9 passed |
| R20 unwind regression | 31 passed |
| R11 lifecycle, runner predicates, output bounds | 7, 30, 6 passed |
| R11R2 runner; R11R4 QAC ownership; QAC stream capture | 24, 15, 2 passed |
| R12R1 handoff; R12R2R1 dispatch; R12R3 lifetime | 37, 12, 13 passed |
| R12R4 factory; R12R4R1 link boundary | 9, 9 passed |
| R12R4R5R4 QAC admission | 35 passed |
| ThreadStore readiness mode and telemetry | 7 and 6 passed |
| Semi-shared contract | 30 passed |
| R12R4R5 GC ownership, R12R4R5R1 init attribution, R12R4R5R2R2R4 safe handoff, R12R4R5R3 return contract, GC-init probes | Passed; scripts did not all print totals |

Runner/package rehearsal also passed as described above. Two optional historical checks were not valid R24 gates: a managed-entry probe was invoked without its required inputs, and a transition-runner check had no matching historical candidate fixture. They are not reported as passing tests. No large unrelated matrix was run.

## Changes and next phase

R24 adds a timeout-only, read-only QEMU CPU snapshot path: status and CPU topology are recorded, the VM is paused and pause verified, and HMP registers are saved before the existing bounded shutdown. The R12R1 handoff runner propagates the opt-in capture and records the snapshot hash. New R24 predicates, hosted tests, candidate construction, runner guards, and packaging checks are in `scripts/dotnet/`. No managed allocation, serial logging, Thread field mutation, transition-frame mutation, or object-header change was added to the module-publication path. Runtime module registration semantics were left intact.

The next useful native observation is the return address and a small stack window for the captured fail-fast CPU state. The guest must be paused, and memory reads should remain read-only. This can attribute the shared fail-fast loop to its immediate caller before any source change is considered. A future candidate should also classify `guideXosFailFast` explicitly in its runtime-address map. Keep the real five-argument generated ABI, registration failure behavior, R20 foundation, and real selector-5 factory unchanged until the caller is identified. Preserve the consumed R24 guard and do not retry this candidate.

The QEMU snapshot sequence uses `query-status` and `query-cpus-fast`, then HMP `info registers`; the QMP commands expose runstate and CPU topology metadata, while the register values come from HMP. See the [QEMU QMP reference for query-status](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#command-query-status) and [query-cpus-fast](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#command-query-cpus-fast).
