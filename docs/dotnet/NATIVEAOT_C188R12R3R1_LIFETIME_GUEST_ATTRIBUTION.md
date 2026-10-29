# NativeAOT C188R12R3R1 — Lifetime Guest Attribution

**Classification: Outcome D — `QAC_OR_EXECUTION_BLOCKED`**

## Repository and preservation

- Date: 2026-10-29 (America/Los_Angeles).
- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD: `3b6e489bca7b2625d1094ab8a70a84c31e6df450`.
- Ending investigation HEAD before this closeout commit: `3b6e489bca7b2625d1094ab8a70a84c31e6df450`.
- The worktree already had a modified `ESP/ramdisk.img` and untracked host build and smoke-output directories. They were preserved. The source ramdisk SHA-256 before and after this work is `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
- No reset, clean, restore, stash, branch switch, history rewrite, QEMU termination, runtime change, allocator change, source ramdisk change, or push was made.
- The only phase edits are the lifetime evidence parser, its focused fixtures, runner integration for capture-completeness state, and this report.

## Diagnostic candidate

The staged C188R12R3 candidate was reused without rebuilding from `out/dotnet/c188r12r3-lifetime-start-attribution/candidate-001`.

| Artifact | SHA-256 | Verification |
|---|---|---|
| Candidate kernel | `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864` | Matches the R3 report and current candidate file |
| Candidate staged ramdisk | `8398FC1EDC5A05FD1FA97EB1BF261D9B185F6E48D3F7E895EFEFCD4EF738D667` | Matches the staging manifest and current candidate file |
| Candidate UEFI loader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` | Matches the R3 report and current candidate file |
| Staged `/APPS/GXOSAPP.ELF` payload | `1EFEC6BEE84592A707217FEA273CB0384224743628ECE9CDE736FA7774871C0E` | Matches the build manifest and `ramdisk-staging.json` |
| `ramdisk-staging.json` | `6A1C2671FD962DC624A1B8EB8857D21CBEEC299BFC0E1642463DD15FE1F41C31` | Matches the R3 report and current file |
| Source `ESP/ramdisk.img` | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` | Required preserved hash |

The candidate staging manifest reports `PASS`, 185 unrelated files preserved, and `changedOtherFiles=0`. Its staged-app hash matches the diagnostic NativeAOT build manifest. The runtime-pack source hash also matches the current source. The accepted settings remain `persistentCompositeLifecycle=true`, `Primary64KiB`, 65,536 bytes, `bounded-static-image-backed-no-collection`; production selectors 4–8 are retained and Settings is selector 5.

The handoff runner accepts this candidate root and verifies its kernel and ramdisk hashes before copying media. The intended candidate root and expected kernel hash are the values above. The runner was not invoked because fresh QAC admission failed.

## Hosted preflight

Focused checks run during this phase:

- C188R12R3 lifetime parser: **13/13 cases passed**.
- C188R12R2R1 dispatch predicates: **11/11 passed**.
- C188R12R1 Settings handoff and pointer predicates: **37/37 passed**.
- C188R11R4 QAC ownership predicates: **15/15 passed**.
- Retained C188 registry host executable: R10 **14**, R11 **15**, R12R2 launch-boundary **8** cases passed; the R12R2 suite exercises selector-5 factory and dispatch behavior.
- PowerShell parser validation for the edited parser, tests, and runners: **zero errors**.
- `git diff --check`: **passed**.

The retained C188R12R3 hosted lifetime suite is **23/23 passed** and the broader managed UI qualification is **39 suites / 955 cases passed**. No source change invalidated those results, so the broader suite and NativeAOT candidate were not rebuilt.

The lifetime evaluator now receives an explicit capture state. Live observation, a verified complete serial capture, and an incomplete or truncated capture produce distinct results. A factory entry without a return is only localized as an interval when the full serial file is verified against the released QAC run; an incomplete capture reports `INSUFFICIENT_CAPTURE` and preserves the observed boundary without claiming execution stopped there. `factory-enter` is never treated as a successful factory return.

## Existing guest evidence retained from C188R12R2R1R1

The last retained serial log is `out/dotnet/c188r11-managed-state-provenance/runs/c188r12r2r1r1-settings-retry-002/guest-001/serial.log`, SHA-256 `29F83812DFB00A081AF38C3286E54E6040AB04CFABD5C363CD8B37E37228BC2B`.

- The pre-transition screenshot and cursor probe confirmed the source-defined Settings button at `(815,550)` in Notes window `000003E8`; the cursor finder reported one unique match at that point. The screenshot and probe are retained at `.../guest-001/before-settings.png` and `.../guest-001/settings-pointer-probe-001-cursor.json`.
- The previous run sent one Settings click and no keyboard input. Notes action `0x18` succeeded, the Settings AppModel request used selector 5, registration lookup returned `PASS code=0`, and runtime-settings initialization returned `PASS code=0`.
- The last outer launch marker is `C188R12R2_LAUNCH stage=lifetime-start ... result=BEGIN`. No matching result or `C188R12R3_LIFETIME` marker appears in that older log; the older candidate predated the internal lifetime markers. No Settings application entry, return, or surface was observed.
- The last completed operation in that trace was runtime-settings initialization. The lifetime-start call was entered, but the previous trace did not identify an operation inside `TryStart`.
- Settings resident registration did not return in that trace. The Settings registry-return state and resident mask were unavailable. Notes retained five descriptors (`n=5`); no registry, heap-persistence, or Notes callback regression was demonstrated.
- The prior owned guest exited with code 0 and QAC verified release. This is retained prior-run evidence, not admission for the current phase.

## Arena and allocation instrumentation

The retained pre-transition `NATIVEAOT-HEAP` observation has base `0x00000001001F4600`, 65,536 bytes, and allocation cursor `0x0000000100202F88`. The corresponding end is `0x0000000100204600`; the observed remaining capacity is `0x1678` (**5,752 bytes**). This is the last safe cursor snapshot before the prior Settings lifetime-start marker, not a measurement made inside the Settings factory.

The candidate can report arena base and size at initialization and allocation cursor at the existing managed-thread preservation boundary. The lifetime markers report invocation, selector, resident count/mask, generation, result code, and stage. They do not report each Settings constructor allocation request or an allocator result. The bounded allocator records failure fields internally and immediately fail-fasts on an unsatisfied request; the current Settings route does not invoke the separate managed-allocation diagnostic report. Therefore the 5,752-byte remainder and hosted 18,000-byte constructor measurement do not prove NativeAOT exhaustion. No safe existing guest-visible operation provides the actual constructor request size or failure record for this route.

## Fresh QAC admission and guest result

During the fresh admission check on 2026-10-29, direct read-only QAC CLI `status` and `list` calls both exited 1 with:

```text
Coordinator unavailable: Coordinator connection timed out or is unavailable.
```

QAC online state, sessions, reservations, and a complete QEMU process census could not be verified. No reservation was requested, no guest runner was started, and no host process snapshot was treated as a substitute for QAC's census.

| Requested observation for this phase | Result |
|---|---|
| NativeAOT initialization, five descriptors, Notes residency | Not observed; no guest boot |
| Screenshot-confirmed pointer and Settings click | Not repeated; no guest boot. Prior screenshot evidence is recorded above |
| Notes action `0x18` and selector-5 lookup | Not repeated; no guest boot. Prior run passed both |
| Ordered `C188R12R3_LIFETIME` trace and last completed internal operation | No R3 guest trace; no internal operation observed |
| Native allocation request, failure, factory return, resident registration | Not observed |
| Settings application entry, return, or surface | Not observed |
| Registry/resident-pool post-transition integrity | Not observed in this phase |
| Current guest exit and QAC release | Not applicable; no admission or guest was acquired |

## Classification and next action

**Outcome D — `QAC_OR_EXECUTION_BLOCKED`.** The candidate and hosted preflight are valid, but QAC did not provide the fresh complete census and exclusive admission required for a guest. This phase establishes no new runtime failure or success and does not prove an allocator defect.

The smallest next phase is to restore QAC coordinator responsiveness, take one fresh complete and consistent census, and—only if the lane is clear—run one bounded boot of this unchanged candidate with one screenshot-verified Settings click. The verified-complete serial capture can then distinguish factory entry/return, registration, owner assignment, lifetime return, and caller continuation. No runtime or allocator correction is justified before that evidence.
