# NativeAOT C188R11R4 — Managed Memory Ownership and Reset Attribution

**Date:** 2026-10-26
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `36247c3e3260f14f993cbc962a57ea253ab4e4e5`
**Ending audited source HEAD before this phase commit:** `36247c3e3260f14f993cbc962a57ea253ab4e4e5`
**Outcome:** **B — ROOT_CAUSE_PROVEN**
**Guest objective selected:** repair qualification, after the runtime configuration defect was identified.

## Result

The R3 state loss came from selecting the wrong lifecycle mode for a resident NativeAOT composite. The C188 builders produced a no-collection, image-backed managed arena with `persistentCompositeLifecycle=false`. Every `RhpReversePInvoke` calls `initializeRuntimeState`; without the persistent lifecycle define, that initializer zeroes the entire 64 KiB arena and rewinds the thread allocation cursor. The NativeAOT static roots still point to the same addresses, but the objects at those addresses have been cleared. This matches the R3 observations: the same descriptor-array reference reports length zero, and the same lifetime owner has a null `_entries` field and zeroed state.

The smallest correction was to enable the existing `PersistentCompositeLifecycle` runtime-pack option for both C188 diagnostic and proof-disabled production builds. The corrected manifests report the option as enabled. The one authorized QAC boot used the corrected diagnostic candidate and confirmed persistent heap initialization plus a registry with five descriptors and resident Notes at managed launch return. QAC's live `status` and `list` results disagreed at the next input gate, so the runner shut down before sending the requested `a` key. The correction therefore has hosted and build qualification, but not first-input guest qualification. No second guest was started.

## Baseline and preservation

- The starting branch and HEAD matched the request. No branch switch, reset, clean, restore, or stash was performed.
- The pre-existing source `ESP/ramdisk.img` hash was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` at baseline and after the run. It was not modified.
- The diagnostic application was staged into isolated copies under `out/dotnet/c188r11-managed-state-provenance/candidate/`. Only those candidate ramdisks were changed.
- Existing untracked build outputs were preserved. They are not included in the phase commit.

## Ownership and allocation map

| Object or storage | Allocation and owner | Lifetime and roots | Movement or release |
|---|---|---|---|
| `GuideXosApplicationRegistry.s_entries` | Its generated type initializer creates the five descriptor entries with NativeAOT `newarr`/`RhpNewArray`, using the current thread allocation context in the runtime TLS block. | `s_entries` is a static readonly field in the loaded NativeAOT image; NativeAOT module initialization publishes its static base. | The C188 configuration uses the runtime pack's bounded image-backed no-collection arena. It is not moved by GC. The old profile nevertheless zeroed the arena on every reverse entry. |
| Five-element descriptor array | Allocated from `g_guideXosManagedHeap`, a 64 KiB static BSS arena in the runtime-pack image. | Rooted through `s_entries`; ordinary reads use `s_entries.Length` and descriptor indexing. | No collection or free-list reuse is configured for this allocation mode. Its contents were erased by the wrong per-entry reset. |
| `GuideXosManagedApplicationLifetime` (`s_lifetime`) | Constructed by the registry type initializer from the same runtime allocation context. | Rooted by static readonly `s_lifetime`. | Same bounded arena; no GC relocation. |
| Resident pool `_entries` | Five-element `Entry[]` allocated by the owner's field initializer. The owner object contains the only `_entries` reference. | Rooted through `s_lifetime`; the `_entries` slot is read by the existing lifetime methods. | Same arena; the old reset cleared both the field slot and pool array contents. |
| Runtime allocation context | `initializeRuntimeState` stores the arena base and limit in the current image TLS/runtime cell at `+0x30` and `+0x38`. `RhpNewArray` uses that allocation context. | Runtime TLS and the resident managed thread remain attached for the composite image lifetime. | The old nonpersistent branch reset the allocation pointer to the base on each reverse entry. The persistent branch initializes once and preserves the cursor on later entries. |
| Native invocation context | `NativeGxAppContext` is a stack context containing value fields; it does not retain managed object references. | Exists for one native-to-managed call. | No raw managed-object pointer is retained across returns. The C188 call and module identity remain the same across launch and input boundaries. |

This C188 allocation mode is **not** the real moving Workstation GC heap. The current runtime-pack manifest says `allocationStrategy=bounded-static-image-backed-no-collection`; the corrected diagnostic and production builds both report a 65,536-byte heap. `GC.CollectionCount(0)` remained zero. `GC.GetGeneration` emitted `2147483647` for these objects, which is outside the supported GC generations and is treated by the diagnostic test as the non-GC-arena sentinel. No managed raw address is dereferenced or retained by the new instrumentation; each is emitted as a scalar from a live reference, and the old pool array is held in a local through the snapshot.

## Root cause and layout audit

The relevant runtime path is `RhpReversePInvoke` → `initializeRuntimeState(block)` in `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`. In the build configuration used by the original C188 candidates, neither `GUIDEXOS_NATIVEAOT_C108_TLS_LIFECYCLE` nor `GUIDEXOS_NATIVEAOT_PERSISTENT_COMPOSITE_LIFECYCLE` was defined. The remaining managed-allocation branch writes zero to each byte of `g_guideXosManagedHeap`, resets the TLS allocation pointer to `heapBase`, and resets the limit. The old diagnostic and production runtime-pack manifests both recorded `persistentCompositeLifecycle=false`.

That is a bounded arena reset, not evidence of an out-of-bounds native write, GC reclamation, or a native/managed ABI mismatch. It is a valid reset policy for a disposable launch profile, but incompatible with C188's resident composite whose static roots are used by later input callbacks. The registered static roots survive while the objects they address are cleared. This explains why logical identities and the registry epoch stayed stable even as `Length`, `_entries`, and pool fields read as zero.

The managed array length is read through normal C# access. The debugger target for its header uses the existing NativeAOT array-layout evidence (`length` at object offset `+0x08`; data at `+0x10`) recorded in `docs/dotnet/MANAGED_BOUNDED_ALLOCATION_OOM_PROOF.md`. The pool watch target is the address of the `_entries` reference field, obtained without reading through that address in managed code. The hardware watchpoints were attempted only through the QAC-owned QEMU GDB stub. The guest run did not confirm that GDB armed them, so no hit or write instruction is claimed.

## Memory snapshots

The corrected one-boot run recorded:

| Checkpoint | Registry (`ra`) | Pool owner (`owner`) | `_entries` slot (`slot`) | Pool array (`live` / `held`) | Other state |
|---|---:|---:|---:|---:|---|
| Registry initialization, seq 7 | `0x00000001001F4F50` | `0x00000001001F51E0` | `0x00000001001F51E8` | `0x00000001001F5218` | GC count 0; held length 5 |
| Managed Notes launch return, seq 13 | `0x00000001001F4F50` | `0x00000001001F51E0` | `0x00000001001F51E8` | `0x00000001001F5218` | descriptor count 5; resident mask 1; pool writes `1:1`; held length 5 |

The runtime reported heap base `0x00000001001F1600`, size `0x10000`, and initialization followed by a preserve event. All five captured addresses above lie inside that range. The equality of logical identities and physical addresses is therefore separately established at these checkpoints. There is no input-dispatch snapshot from this boot because the keyboard transaction was not sent.

R3's retained guest log remains the prior failure evidence: `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-r11r3-001/guest-001/serial.log`, SHA-256 `5E6287481B04DE375B4FBFF545C917B6531A240A5397EE6D4D3E6089CA909A00`. At its first input entry the descriptor length and resident mask were zero, the `_entries` reference was null, and lookup returned `-4`. The R4 source/configuration audit identifies the arena reset that produces that state.

## Correction

`build-c188-nativeaot-candidate.ps1` and `build-c188-production-nativeaot.ps1` now pass `PersistentCompositeLifecycle = $true` and reject runtime-pack manifests that do not report it. The existing persistent runtime-pack branch zero-initializes the arena once, then preserves the arena and allocation cursor on later reverse entries. No kernel ABI, managed ABI, registry semantics, runtime implementation, or application model was changed.

The focused regression checks cover the two C188 builders, shared flag forwarding, one-time initialization, preservation on later entry, and manifest enforcement. The R11 physical-storage predicates also require stable addresses and confirm they fall within the initialized arena. The hosted test accepts `int.MaxValue` as the non-GC-arena generation sentinel and rejects other out-of-range values.

## Qualification

| Validation | Result |
|---|---|
| C188R11 hosted registry suites | PASS — R9 14 cases, R10 14 cases, R11 15 cases |
| R11 runner predicates / output bounds / runtime lifecycle | PASS — 30 / 6 / 7 cases |
| R2 keyboard contracts / predicates | PASS — 18 / 9 cases |
| Prior C188 host regressions | PASS — C182 15, C185 28, C186 15, C187 22 cases |
| Managed UI regression aggregate, including Notes | PASS — 38 suites, 930 cases |
| Corrected diagnostic NativeAOT candidate | PASS — ELF SHA-256 `15ABAAC80ED7C306D54572A701D6075D237D14C4F4C1AC5B04636820C376809D`; persistent lifecycle true |
| Diagnostic kernel | PASS — SHA-256 `0032BD297D86314D86B182C8CFDC7A15E3C12FE07BDEE5F192A2BFFD78403B93` |
| Corrected proof-disabled production NativeAOT | PASS — five production selectors 4–8; ELF SHA-256 `F8F7B89F6E79A242A96215F2164C1D290D0A8810EE3735B7E9045D1D14069732`; persistent lifecycle true |
| Production kernel | PASS — SHA-256 `2BFCB9D3BEDEA1C4C161B5978EC1A71B33BCF11FAC846815B6D9EE49986465BF` |
| `git diff --check` | PASS |
| Source ramdisk preservation | PASS — unchanged hash `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

Hosted manifest: `out/dotnet/c188r11-managed-state-provenance/hosted/memory-provenance-006/manifest.json`. Managed UI manifest: `out/dotnet/c188r11-managed-state-provenance/hosted/managed-ui-regression-001/managed-ui-host-manifest.json`. Prior host manifest: `out/dotnet/c188r11-managed-state-provenance/prior-host-regressions/memory-provenance-001/prior-host-regressions.json`.

The diagnostic ELF is `out/dotnet/c188r11-managed-state-provenance/nativeaot/memory-provenance-002/nativeaot/artifacts/HostLogProof.elf`; its runtime-pack manifest is in the same build root. The production ELF and runtime-pack manifest are under `out/dotnet/c188r11-managed-state-provenance/production-nativeaot/memory-provenance-002/`. Kernel ELFs are under the corresponding `kernel/memory-provenance-001/` and `production-kernel/memory-provenance-001/` roots.

## QAC run and remaining qualification

Exclusive preflight passed: QAC was online with zero QEMU processes, zero foreign processes, zero reservations, and zero managed sessions. One QAC-owned QEMU process (PID `33672`, reservation `c0be56c5ea3d4ed2ac53f2ea3229bae3`) booted the corrected isolated candidate. QAC logged exit code 0 and released that reservation. Post-run status returned to zero QEMU processes, reservations, and sessions.

At the input gate, QAC `status` reported one QEMU process, zero foreign, and one managed session, while `list` reported zero reservations. The runner required the list row and stopped before sending the `a` down/up transaction. The wrapper produced no acceptance manifest, input screenshot, callback evidence, or after-input screenshot. No text edit is claimed. The GDB log only contains the architecture-selection line; no `watchpoints-armed` marker or hit was recorded. The updated gate now waits for both census files and accepts a stale list only when the prior admission plus the live status prove one non-foreign owned session. That end-to-end retry was not run because this task allowed at most one guest boot.

The retained corrected boot artifacts are under `out/dotnet/c188r11-managed-state-provenance/runs/r4-repair-qualification-001/guest-001/`. `serial.log` SHA-256 is `D90595ADB57ACDBD0018F45C521E17297AA407AA2C30F4C9151DBCB3B529BF34`; `qac.stderr.log` SHA-256 is `DA87CD31B2CCC608143260A299D158851D62B3E97759059B26F021871020F051`; `r4-gdb.stdout.log` SHA-256 is `7D8D8B792F679C04B2394740F6031D240E784FD42055AE575D45453B700A9529`. The QAC stderr log records the reservation request, launch, exit, and release. The reservation ID was `c0be56c5ea3d4ed2ac53f2ea3229bae3`, and the QEMU child PID was `33672`.

The concrete reset mechanism and correction are established by source, build configuration, and the corrected launch snapshots. Outcome A is not claimed because Notes' first keyboard callback and registry state at input were not guest-qualified. The smallest next action is one separately authorized QAC boot of the corrected candidate to verify the first input snapshot, successful lookup, Notes callback, and after-input screenshot.

## Intended files

- `samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs`
- `samples/managed/HostLogProof/GuideXos/GuideXosApplicationRegistryC188R11Tests.cs`
- `samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationC150Tests.cs`
- `scripts/dotnet/build-c188-nativeaot-candidate.ps1`
- `scripts/dotnet/build-c188-production-nativeaot.ps1`
- `scripts/dotnet/c188r11-runner-predicates.ps1`
- `scripts/dotnet/fixtures/c188r11/state-provenance.serial.txt`
- `scripts/dotnet/run-c188-qac-guest.ps1`
- `scripts/dotnet/run-c188r11-managed-provenance-tests.ps1`
- `scripts/dotnet/run-c188r11r2-keyboard-dispatch.ps1`
- `scripts/dotnet/test-c188r11-output-bounds.ps1`
- `scripts/dotnet/test-c188r11-runner-predicates.ps1`
- `scripts/dotnet/test-c188r11-runtime-lifecycle.ps1`
- `scripts/dotnet/test-c188r11r2-keyboard-contracts.ps1`
- `docs/dotnet/NATIVEAOT_C188R11R4_MEMORY_OWNERSHIP.md`

Only these intended source, runner, test, and report files are to be committed. The pre-existing modified source ramdisk and unrelated generated outputs remain excluded. The phase commit is local; no remote push is attempted under the existing C188 phase policy.
