# NativeAOT C188R12R3 — Managed Lifetime Start Attribution

**Outcome: A — `LIFETIME_DIAGNOSTICS_HOSTED_READY`**

**Branch:** `v1.1_DOTNET_SUPPORT`

**Starting HEAD:** `4f3e729159add01618e97715223d9d332319b10f`

**Ending analysis HEAD before closeout commit:** `4f3e729159add01618e97715223d9d332319b10f`

This is a hosted-only qualification. No QEMU guest was started, no QAC admission was requested, and no other project's QEMU session was inspected or changed. The diagnostic build and a separately staged candidate are ready for a later, separately authorized guest run. The source audit did not prove a root cause, and no runtime or allocator correction was made.

## Baseline and preserved state

The checkout was already on `v1.1_DOTNET_SUPPORT`, seven commits ahead of its upstream. The worktree already contained a modified `ESP/ramdisk.img` and unrelated untracked `bin`, `obj`, and smoke-output directories. Those pre-existing changes and outputs were retained. No reset, restore, clean, stash, branch switch, history rewrite, or push was performed.

The source ramdisk hash was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` before and after candidate staging. Only a copy under `out/dotnet/c188r12r3-lifetime-start-attribution/candidate-001/esp/ramdisk.img` was edited.

The retained guest log at `out/dotnet/c188r11-managed-state-provenance/runs/c188r12r2r1r1-settings-retry-002/guest-001/serial.log` confirms the prior boundary: five descriptors, successful selector-5 lookup and runtime-settings initialization, then `C188R12R2_LAUNCH stage=lifetime-start ... result=BEGIN` without a corresponding result during the bounded 30-second capture. No Settings application entry or surface followed. The run's QAC release was normal. This localizes the missing observation to `s_lifetime.TryStart(...)`; it does not prove an internal hang or compositor failure.

## `TryStart` implementation map

The shared implementation is `GuideXosManagedApplicationLifetime` in `samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationC150Tests.cs` (`TryStart` delegates to `TryStartCore`). The shared NativeAOT registry dispatches selector 5 through this same manager in `samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs`.

| Order | Actual operation | Result or state change |
|---|---|---|
| 1 | `FindEntry(descriptor.Selector)` | Finds an existing slot for replacement; otherwise searches for a free slot. There is no separate descriptor-validation routine in `TryStart`. |
| 2 | Capacity check | Returns `false` before factory invocation when no slot is available. |
| 3 | Generation check | Automatic generation fails before factory invocation only when `_generation == uint.MaxValue`; an explicit requested generation bypasses that check. |
| 4 | Factory selection and call | Reads `descriptor.Factory`; a missing factory returns `false`. The factory is called synchronously. A null result returns `false`. |
| 5 | Replacement teardown, if needed | Calls `OnTearingDown()` on an existing application in the selected slot. A new Settings selector does not replace the Notes selector's slot. |
| 6 | Resident registration | Clears and writes the selected entry's selector, generation, and application. This is the actual resident-pool mutation. |
| 7 | Generation commit and return | Advances `_generation` when needed, assigns the returned generation, and returns `true`. |

The normal Notes and Settings descriptors use static, non-capturing factory lambdas: `new ManagedNotes()` and `new ManagedSettingsCenter()`. Both use the same pool and synchronous factory path. Settings has a larger constructor path: its constructor builds a managed control graph, item collections, groups, handlers, and dialogs. Shared runtime Settings initialization is observed before `TryStart`; `ManagedSettingsCenter.Launch` runs only after a successful lifetime start. The Settings constructor measured 18,000 bytes of hosted .NET allocations in the new suite; that is not a measurement of its consumption in the fixed NativeAOT arena.

Descriptor display-name copying occurs at descriptor registration, outside `TryStart`. The lifetime entry contains selector, generation, and application; generation is the owner token. There is no callback-registration operation or callback-owner field inside `TryStart`. Host callback setup is performed elsewhere, so no `callback-register` marker was added. Likewise, there is no independent input-validation boundary in this method, so no `validate` marker was invented.

### Dispatch and reentrancy

The Notes action callback completes first. The kernel's `onWidgetClick` path receives its return, records the successful action, then calls `launchSettingsCenterFromNotes()`, which synchronously enters AppModel and the Settings managed dispatch. Thus Settings `TryStart` runs on the native desktop input/launch stack, but not while the Managed Notes action callback itself is active.

`GuideXosManagedApplicationLifetime` has no lock, monitor, semaphore, recursion guard, initialization-in-progress flag, asynchronous wait, or deferred-launch queue. The path does not acquire a lifetime-pool lock. The built-in factories are static constructors and do not call the lifetime manager. A hypothetical constructor-driven recursive start is not an established source contract and was not introduced as a test. No concrete double-lock, callback wait, or launch-state cycle was found.

### Persistent arena observations

Both NativeAOT runtime-pack manifests report `persistentCompositeLifecycle=true`, `managedHeapConfiguration=Primary64KiB`, `managedHeapBytes=65536`, and `allocationStrategy=bounded-static-image-backed-no-collection`. In the latest Settings attempt, the last heap-preservation marker reported base `0x00000001001F4600` and allocation cursor `0x0000000100202F88`. The 64 KiB limit is base plus `0x10000` (`0x0000000100204600`), leaving `0x1678` = **5,752 bytes** at that observation.

That headroom makes capacity a concrete concern to investigate, but the 18,000-byte hosted Settings constructor figure is a regular .NET GC allocation measurement, not a byte-for-byte prediction for NativeAOT's fixed arena. The runtime-pack's documented bound-failure behavior is an out-of-memory diagnostic followed by fail-fast; it does not return a fabricated managed reference or reclaim objects. Such a native fail-fast would not be caught as a managed factory exception. No arena increase, allocator change, or exhaustion injection was made. Notes' prior objects remain valid through the observed Settings dispatch because the existing persistent-lifecycle correction preserved the same heap and allocation cursor; the Settings launch itself did not return far enough to establish its post-start state.

## Diagnostic markers and evaluator

The proof-only `C188R12R3_LIFETIME` family is emitted at real `TryStartCore` transitions. Each line includes invocation, selector, app identity, stage, resident count and mask, generation, result code, and state. The manager traces at most eight starts; formatting uses a 224-byte stack buffer, spans, and the existing synchronous `GuideXosHost.TryLog` path. The hosted capture buffer is compiled only for the hosted lifetime-test build. The proof-disabled path continues to call the original `TryStart` entry point.

Observed stages are `enter`, `existing-resident`, `capacity-check`, `generation-check`, `factory-resolve`, `factory-enter`, `factory-return`, `factory-throw`, `resident-register-enter`, `teardown-enter`, `teardown-return`, `teardown-throw`, `resident-register-return`, `owner-assign`, `exit-success`, and `exit-failure`. Only applicable branches emit a stage. A marker is emitted immediately before and after a normal factory return and around resident registration. Exception paths emit a diagnostic and rethrow; they are not converted to success. Native fixed-arena fail-fast can terminate without a managed return marker.

The hosted evaluator in `scripts/dotnet/c188r12r3-lifetime-predicates.ps1` correlates these records with the existing `C188R12R2_LAUNCH` dispatch records. It distinguishes factory entered with no return observed, factory error or throw, factory return without registration completion, registration without subsequent owner/return completion, `TryStart` success without caller continuation, missing application/dispatch return, return without Settings surface, normal launch success, malformed markers, and incomplete evidence. A missing marker is classified as an unobserved boundary, not proof of an infinite stall; QAC process exit and release evidence remain necessary to interpret capture termination.

## Hosted qualification

Final qualification results:

- `C188R12R3-LifetimeStart`: **23/23 cases passed**. Covers first Notes resident, second Settings resident, state and generation preservation, success marker order/correlation, missing/null factory, propagated factory exception, synthetic `OutOfMemoryException` propagation, replacement teardown, capacity rejection before factory invocation, generation overflow, and Settings constructor allocation measurement. The exception case is hosted .NET behavior, not a reproduction of NativeAOT fail-fast.
- Managed UI regression gate: **39 suites, 955 cases, all passed**. This includes C181 residency (24 cases) and stress (7 cases), with hosted five-application allocation measurement 83,624 bytes and fourth/fifth-app incremental measurement 20,792 bytes.
- C188 hosted application-factory/identity suite: **25 cases passed**.
- Prior hosted regressions: C182 15, C185 28, C186 15, C187 22; all passed.
- R11/R12 hosted provenance: R9 14, R10 14, R11 15, R12R2 launch boundary 8, runner predicates 30, runtime lifecycle 7; all passed in the retained hosted run.
- Synthetic predicates: R12R3 lifetime 9, R12R2R1 dispatch 11, R12R1 handoff 37; all passed. The lifetime negative cases include factory-enter without return and registration-enter without registration return.
- Changed PowerShell runners and staging scripts passed PowerShell parser validation. The R12R3 staged candidate passed FAT32 staging validation with 185 unrelated files preserved and zero other file changes.

Two intermediate attempts at the new hosted UI suite exposed assertion mistakes: `firstFailedCase=2` checked the wrong second-resident state, and `firstFailedCase=6` expected 20 marker events instead of the actual 22. Both assertions were corrected, and the final 23-case suite and full UI gate passed. An initial diagnostic NativeAOT verifier attempt also assumed a stage label appeared as one contiguous ELF string; the verifier was changed to validate the separately emitted tokens, and the final diagnostic build passed. No unresolved test failure remains.

## NativeAOT and kernel builds

The source tree was built from the worktree based on HEAD `4f3e729159add01618e97715223d9d332319b10f`, including the uncommitted diagnostic changes described here. Both runtime manifests use identity `guidexos-nativeaot-runtime-pack-amd64-persistent-composite-lifecycle-v1`, NativeAOT 9.0.0, `net9.0` / `win-x64` AMD64, the locked guideXOS runtime pack, a 65,536-byte Primary64KiB heap, and persistent composite lifecycle.

| Configuration | Result and important flags | Artifact SHA-256 |
|---|---|---|
| Diagnostic NativeAOT | PASS; `managedStateProvenance=true`, `lifetimeStartAttribution=true`, production selectors 4–8 retained, C188 candidate selector 9 present; ELF64, 7 load segments, 0 relocations | `HostLogProof.elf`: `1EFEC6BEE84592A707217FEA273CB0384224743628ECE9CDE736FA7774871C0E` |
| Proof-disabled production NativeAOT | PASS; C160 composite lifecycle retained; proof controls, diagnostic instrumentation, and CounterApp route absent | `HostLogProof.elf`: `9FF6DF6E480B9398F52179410BB2503C37FE4BF4095EDF6EE7169691377363A9` |
| Diagnostic candidate kernel | PASS; external identity and R11 state-provenance defines; standard kernel output untouched | `kernel.elf`: `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864` |
| Production kernel | PASS; production identities selectors 4–8; development descriptor absent; standard kernel output untouched | `kernel.elf`: `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |

Manifest hashes: diagnostic build `AF23A704B77EF561D31B6B51554C8F2C458A8CA5E6F8E4F94D6704D360BC12DD`; diagnostic runtime pack `21C8AF387CD224213E7CCEDCF00A526D876D47C4EB35AAD6FAA4C981B4FDA0ED`; production qualification `732C95ECFDAD3C99E35577382558FC6899CEEAABE8325EFD0A581076A853D4D3`; production runtime pack `68BF5524056EAF85DDABF616130D1B751D1F6FE360F0737D89E674EBD0EAFCEC`. Runtime platform source hash in both manifests: `C47A6DF47EC42EFFE820E97F18D1832C4CD43F80DF2060DF4B6D1EF084544224`.

Compiler/build warnings were retained and counted; builds had no errors. Diagnostic NativeAOT logged 72 warning lines: C4005 2, CS0169 6, LNK4099 58, LNK4210 2, LNK4216 1, plus one ILCompiler package-reference advisory and two post-link symbol-prologue warnings. Production NativeAOT logged 67: C4005 2, CS0162 1, LNK4099 58, LNK4210 2, LNK4216 1, plus the same three advisory/post-link warnings. Each kernel build logged 425 C++ warning lines and zero errors, mostly unused parameters, signedness comparisons, and two narrowing-conversion warnings. The final managed UI host log contains one CS0162 unreachable-code warning in existing `Program.cs:2712`; the new diagnostic source introduced no warning.

## Isolated staged candidate

Candidate root: `out/dotnet/c188r12r3-lifetime-start-attribution/candidate-001`. Its source was a copy of the preserved source ramdisk and the diagnostic kernel, boot EFI, and diagnostic NativeAOT `HostLogProof.elf`. `stage-c188-ramdisk.ps1` replaced only `/APPS/GXOSAPP.ELF`; it added nine clusters, preserved 185 unrelated files, and reported `changedOtherFiles=0`.

| Candidate file | SHA-256 |
|---|---|
| `esp/kernel.elf` | `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864` |
| `esp/ramdisk.img` | `8398FC1EDC5A05FD1FA97EB1BF261D9B185F6E48D3F7E895EFEFCD4EF738D667` |
| `esp/EFI/BOOT/BOOTX64.EFI` | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| staged `/APPS/GXOSAPP.ELF` | `1EFEC6BEE84592A707217FEA273CB0384224743628ECE9CDE736FA7774871C0E` |
| `ramdisk-staging.json` | `6A1C2671FD962DC624A1B8EB8857D21CBEEC299BFC0E1642463DD15FE1F41C31` |

## Future one-guest plan

The future run is a single QAC-owned Settings handoff using the staged diagnostic candidate, not the production candidate. The base guest runner and `run-c188r12r1-settings-handoff.ps1` now accept the R12R3 output root; the outer runner records the R12R3 lifetime evaluator output alongside R12R2 and QAC ownership/release evidence. No guest was started in this phase.

After explicit authorization for the guest phase, use one fresh evidence directory and this bounded invocation:

```powershell
pwsh -NoProfile -File scripts/dotnet/run-c188r12r1-settings-handoff.ps1 `
  -CandidateRoot out/dotnet/c188r12r3-lifetime-start-attribution/candidate-001 `
  -ExpectedKernelSha256 568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864 `
  -BootEvidencePath out/dotnet/c188r12r3-lifetime-start-attribution/runs/settings-lifetime-001 `
  -MaxRuntimeSeconds 120
```

The runner must first observe an empty QAC census and obtain exclusive admission, then qualify the owned child/session identity. It copies and re-hashes the candidate media into the fresh run directory, boots Notes normally, and confirms the five-descriptor registry and selector-4 state. It source-verifies and screenshot-verifies the Settings button at `(815,550)`, confirms pointer location and no prior QMP input, then sends exactly one Settings click pair and no key events. It captures the R12R2 dispatch markers and R12R3 internal markers during the bounded 30-second observation, plus application entry and surface/presentation evidence if reached. Before shutdown it rechecks QAC ownership, requests QMP quit through that owned run, and requires QAC to report the child exited and reservation released with an empty post-run census.

Interpret the last complete lifetime stage with the hosted evaluator. In particular, `factory-enter` without `factory-return` means the return was not observed; it does not alone prove the factory stalled. `factory-return` with an error is distinct from a missing return. A successful factory without `resident-register-return` localizes the next missing boundary to registration. Registration without owner/exit completion localizes later work. `exit-success` without Settings `application-entry` localizes the missing observation to the caller boundary. A completed Settings dispatch without surface evidence is a downstream launch/surface result, while correlated application return and Settings surface/presentation markers qualify the full launch. Invalid or absent trace data remains incomplete evidence.

## Finding and next action

No source defect was proven. The fixed arena's observed 5,752 bytes of remaining space and the hosted Settings constructor's 18,000-byte allocation figure make allocation pressure worth measuring in the authorized diagnostic guest, but they do not establish that Settings exhausts the NativeAOT arena. The smallest justified next action is one bounded, QAC-owned run of the staged candidate and evaluation of the first missing `C188R12R3_LIFETIME` boundary. No correction is proposed before that evidence.
