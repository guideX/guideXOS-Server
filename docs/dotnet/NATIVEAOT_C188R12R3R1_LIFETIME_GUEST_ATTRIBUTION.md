# NativeAOT C188R12R3R1 — Lifetime Guest Attribution

**Classification: Outcome B — LIFETIME_FAILURE_LOCALIZED**

## Result

One fresh QAC-authorized boot of the unchanged diagnostic candidate reached the selector-5 lifetime factory boundary. The verified complete serial trace emitted factory-enter for invocation 2, after successful resident lookup, capacity, generation, and factory-resolution checks. It contains no factory return or throw, resident-registration marker, lifetime return, or Settings application-entry marker.

This localizes the first unobserved work to the synchronous selector-5 factory invocation that calls new ManagedSettingsCenter(). It does not identify where inside that constructor the execution stopped, and it does not prove a permanent hang, allocation failure, deadlock, recursion, or other root cause. No allocator or runtime correction is justified by this evidence.

## Repository, HEAD, and preservation

- Date: 2026-10-29 (America/Los_Angeles).
- Branch: v1.1_DOTNET_SUPPORT.
- HEAD reported when this mission was first received: 3b6e489bca7b2625d1094ab8a70a84c31e6df450.
- HEAD used for the fresh candidate run and evidence collection: 75c5674f935c96e6533fe034a4b7de54b8ac392a.
- Ending investigation HEAD before this follow-up closeout commit: 75c5674f935c96e6533fe034a4b7de54b8ac392a.
- The earlier blocked admission attempt launched no guest. On the user's retry, QAC became available and exactly one guest boot was performed.
- The worktree already contained a modified ESP/ramdisk.img and untracked build/smoke-output directories. They were preserved. The source ramdisk SHA-256 before and after the guest run is AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8.
- No reset, clean, restore, stash, branch switch, history rewrite, foreign-process termination, source ramdisk change, runtime or allocator behavior change, or push was performed.
- The intended follow-up source changes are limited to the Settings-dispatch runner predicate, its focused regression test, and this report.

## Candidate and runtime configuration

The existing candidate at out/dotnet/c188r12r3-lifetime-start-attribution/candidate-001 was reused without rebuilding.

| Artifact | SHA-256 |
|---|---|
| Candidate kernel | 568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864 |
| Candidate staged ramdisk | 8398FC1EDC5A05FD1FA97EB1BF261D9B185F6E48D3F7E895EFEFCD4EF738D667 |
| Candidate UEFI loader | D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B |
| Staged /APPS/GXOSAPP.ELF payload | 1EFEC6BEE84592A707217FEA273CB0384224743628ECE9CDE736FA7774871C0E |
| Candidate ramdisk-staging.json | 6A1C2671FD962DC624A1B8EB8857D21CBEEC299BFC0E1642463DD15FE1F41C31 |
| Source ESP/ramdisk.img | AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8 |

The staging manifest result was PASS. It reported 185 unrelated files preserved and changedOtherFiles=0; the staged app payload hash matched the diagnostic build manifest. Runtime manifests retained persistentCompositeLifecycle=true, Primary64KiB, 65,536 managed heap bytes, and bounded-static-image-backed-no-collection. The production selector catalog remains 4–8, with Settings at selector 5.

## Hosted preflight

The candidate and source identities were revalidated before execution. The accepted persistent lifecycle configuration and existing source-defined Settings route were checked. The one-run command selected the candidate root above and required the full kernel hash shown above.

Focused predicate checks run after the follow-up parser correction:

- C188R12R3 lifetime predicates: **13/13 passed**.
- C188R12R2R1 Settings-dispatch predicates: **12/12 passed**.
- C188R12R1 Settings handoff and pointer predicates: **37/37 passed**.
- C188R11R4 QAC ownership predicates: **15/15 passed**.
- PowerShell parser validation for the modified runner and regression test: **zero errors**.
- git diff --check: **passed**.

Retained hosted qualification from the preceding phase remains applicable because no managed-runtime or allocator source changed: the C188R12R3 lifetime suite passed **23/23**, managed UI qualification passed **39 suites / 955 cases**, and the retained registry executable passed R10 **14**, R11 **15**, and R12R2 selector-5 dispatch/factory **8** cases. Those retained suites were not rerun for this runner-only correction.

The Settings-dispatch runner previously required the one-time managed lifecycle test marker for every Settings dispatch. That suite runs during initial Notes startup; it is not expected to run again during the later Settings transition. The runner now passes RequireLifecycleTests=false when parsing the later Settings dispatch at initialization, during live observation, and for final evidence. A focused regression case covers this route. This corrects the runner's dispatch summary; it does not alter the candidate or guest behavior.

The raw run JSON was produced before this correction and labels the outer dispatch summary with the stale lifecycle requirement. The actual run serial was separately re-evaluated with the corrected predicate and saved at out/dotnet/c188r12r3-lifetime-start-attribution/runs/settings-lifetime-002/r12r2r1-settings-dispatch-corrected-analysis.json. That verified-complete analysis reports seven ordered dispatch markers, no malformed markers, lifecycleMarkerRequired=false, and the first unobserved outer boundary lifetime-start-result. The nested lifetime result in the original run JSON independently records the same factory boundary.

## QAC admission, ownership, and execution

The fresh retry found QAC 2.0.0 online. Before admission, its status reported zero QEMU processes (zero foreign), zero managed sessions, zero queued or blocked work; its list reported zero reservations and sessions. One reservation was obtained, ID 5aeff7083cd24a0996d21bf2c8d72b6b.

QAC accepted the owner/runner/child identity chain: owner PID 10444, runner PID 18000, QEMU child PID 23556 at C:\Program Files\qemu\qemu-system-x86_64.exe. The reservation was not degraded. The manifest records accepted ownership proof, confirmed admission, and verified release. QEMU exited with code 0. After release, QAC again reported zero QEMU processes (zero foreign), zero reservations, and zero managed sessions, queued, or blocked work.

The single bounded run used the staged kernel hash above and MaxRuntimeSeconds=120. NativeAOT initialization completed, five descriptors were registered, and Notes became resident. There was exactly one guest boot, one Settings click, and zero keyboard events. No retry or second click occurred. Run artifacts are retained in out/dotnet/c188r12r3-lifetime-start-attribution/runs/settings-lifetime-002; the verified-complete serial.log SHA-256 is 5F0C5E3EFA56415E83F0E31DBB6BEF0395FF24B16F441EAF2B4147E7B256A159. before-settings.png and settings-check.png contain the pre-transition and settled post-transition captures.

## Input and Settings dispatch

The pre-transition screenshot showed the source-defined Managed Notes Settings button in window 000003E8. The target was (815,550). One cursor probe found exactly one match at that coordinate, inside the verified safe inset x=[776,854), y=[542,558). The interaction plan records sourceValidated=true, pointerPositionVerified=true, one click, and zero key input.

Notes action 0x18 returned managed result 0 with PASS. The AppModel then requested Settings selector 5. Registration lookup returned PASS code=0, and runtime-settings initialization returned PASS code=0. The Notes lifetime had been cleanly closed before the Settings dispatch; its resident count of zero at Settings lifetime entry is therefore consistent with the close/replace path.

The screenshot after the bounded observation still displayed the Notes image. Since the serial trace records Notes close approval and surface destruction before the Settings launch request, that image does not prove that a live Notes window remained. No Settings surface was observed.

## Ordered lifetime trace

Notes startup was invocation 1, selector 4, generation 1. Its trace completed enter, resident miss, capacity available, generation valid, factory resolved, factory entered and returned code=0 state=created, resident registration entered and returned count=1 mask=1 code=0 state=registered, owner assignment, and exit-success. The startup diagnostic reported five registered descriptors (n=5).

Settings was invocation 2, selector 5, generation 2. The complete ordered trace was:

| Order | Stage | Result |
|---:|---|---|
| 1 | enter | count=0 mask=0 state=preflight |
| 2 | existing-resident | state=miss |
| 3 | capacity-check | code=0 state=available |
| 4 | generation-check | code=0 state=valid |
| 5 | factory-resolve | code=0 state=resolved |
| 6 | factory-enter | code=-1 state=invoking |

The parser found zero malformed markers and valid ordering. Its verified-complete result is FACTORY_ENTERED_NO_RETURN; the last completed operation is factory resolution. No factory return or throw, registration entry, owner assignment, or lifetime result followed. Since factory-enter is emitted immediately before application = factory(), the trace localizes the missing completion to that call boundary but does not show a constructor sub-operation.

In source, TryStartCore emits factory-enter immediately before the synchronous application = factory() call in samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationC150Tests.cs. The selector-5 descriptor's static factory constructs ManagedSettingsCenter in samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs. There are no markers inside that constructor, so the trace cannot narrow the interval to a constructor sub-operation.

No ManagedSettingsCenter.Launch entry, Settings application return, or Settings surface marker was emitted. Settings registry return is false and invalid because execution did not reach a return; the Settings resident mask is unavailable (-1). These are missing observations, not evidence of registry corruption.

## Arena and integrity evidence

The last safe heap cursor before Settings dispatch was base 0x00000001001F5600, allocation cursor 0x0000000100204008, with a configured size of 65,536 bytes and end 0x0000000100205600. Remaining capacity at that observation was 0x15F8 (**5,624 bytes**). This was measured before the Settings factory invocation.

The guest trace contains no per-allocation request size, allocation result, allocator failure record, or fail-fast marker associated with Settings construction. The hosted constructor measurement of 18,000 bytes is not a NativeAOT arena measurement and is not used to infer exhaustion. Arena capacity is therefore not established as the cause.

The acceptance evidence reports persistent heap preserved, initialization count 1, and no demonstrated runtime regression. Settings registry-memory stability and resident-mask post-return observations are unavailable because registration was not reached. Notes action 0x18 succeeded; no Notes callback regression was demonstrated.

## Classification and next phase

**Outcome B — LIFETIME_FAILURE_LOCALIZED.** The retry materially narrows the boundary to the Settings factory invocation after all preceding lifetime preflight operations succeed. It does not prove a root cause.

The smallest justified next qualification is to add narrowly scoped proof-only visibility inside the Settings constructor and, if available without changing allocator behavior, capture the native allocation request/result at the existing safe diagnostic boundary. Then rerun focused hosted predicates before any separately authorized guest attempt. Do not enlarge the arena or change runtime/allocator behavior based on the current evidence.
