# NativeAOT C188R11R3 — Managed Registry State Loss

**Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
**Branch:** `v1.1_DOTNET_SUPPORT`
**Starting HEAD:** `7441a5d2abaa94d69d8ef965408d818c0704ec29`
**Ending audited source HEAD (before this phase commit):** `7441a5d2abaa94d69d8ef965408d818c0704ec29`
**Outcome:** **C — STATE_LOSS_LOCALIZED**
**Guest objective selected before launch:** diagnostic-only provenance; no repair.

## Finding

The unchanged registry identity and initialization epoch do not mean the storage contents stayed intact. The identity tracker compares managed references, and the epoch records the registry type-initialization event. Neither is a checksum or write monitor for the objects behind those references.

This run observed the same tracked descriptor-array reference and registry epoch at launch return and first managed input, while the same array's `Length` changed from five to zero. Separately, the same lifetime-owner object remained tracked, but its `_entries` backing reference became null and its pool epoch, generation, resident slots, and in-object write markers all read as zero. The resident mask is computed by scanning that separate pool array; it is not stored alongside the descriptor count.

No registry API clear, application teardown, descriptor replacement, second registry initialization, or recorded lifetime-pool writer explains the transition. The evidence therefore establishes an unrecorded mutation or runtime memory-state loss affecting the backing objects, but it does not identify the instruction or subsystem that performed it. No repair is justified yet.

## Baseline and scope

- Starting branch and HEAD were verified as above. The ending audited source HEAD is the same commit because the phase changes were still in the worktree at qualification time.
- The pre-existing modified `ESP/ramdisk.img` and generated untracked build outputs were preserved. No reset, clean, restore, stash, or branch switch was performed.
- Prior C188R11, R1, R2, and R2R1 evidence was reviewed. R2R1 had already localized the failure to managed registry lookup after a successful Notes launch; this phase added pool writer-site and GC observations.
- The diagnostic-only objective was fixed before QAC launch. No guest boot was repeated or retried.

## Registry storage and writer audit

### Independent storage

In `samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs`, the managed application registry uses a `private static readonly GuideXosApplicationDescriptor[] s_entries`. The five descriptor registrations are built during type initialization for selectors 4–8. `RegistrationCount` returns `s_entries.Length`, and `TryFind` scans that array. The audited source has no later descriptor-array assignment, resize, clear, or registration writer.

The resident mask comes from a different object. `s_lifetime` is a static readonly `GuideXosManagedApplicationLifetime`; its private readonly `_entries` array has capacity five. `C188R11ResidentMask` scans `_entries` and sets one bit for each non-null application. Thus `n` (descriptor-array length) and `m` (resident mask) are independent observations of independent backing storage.

### Legitimate lifetime writers

The only source writes to the lifetime pool slots are in `GuideXosManagedApplicationC150Tests.cs`:

1. `TryStart` clears and populates one entry; diagnostic site `1` is recorded after the assignment completes.
2. Generation-checked `Clear` clears one entry; site `2` is recorded after the clear.
3. Selector `Clear` clears one entry; site `3` is recorded after the clear.

The diagnostic clear, replacement, teardown, reset, and initialization counters remain zero across the failing interval. Notes does not override `OnTearingDown`; its base implementation is empty. The launch creates one resident Notes entry through `TryStart`, which accounts for the single launch-side write event.

The startup bridge in `c102_startup.c` takes a ready-state reentry path for later calls: it installs TLS and invokes `ManagedMain` without repeating hook installation or `RhInitialize`. Native logs retain module identity `1`, entry offset `0x101950`, seven load segments, and the same image span across both calls. `NativeGxAppContext` is constructed as a fresh stack context for each invocation; the C++ definition asserts a 64-byte size and tail offsets, matching the sequential managed field layout in `NativeAbi.cs`. The audit found no source-level ABI mismatch or ordinary cleanup path that accounts for this state loss.

### Added bounded provenance

Diagnostic-only instrumentation records saturating pool write counts and last-writer site IDs (`1=TryStart`, `2=Clear(selector,generation)`, `3=Clear(selector)`) plus `GC.CollectionCount(0)`. Existing bounded state output appends `gc=` and `w=<count>:<site>`. Writer instrumentation is compiled only with `HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE`; the managed ABI was not changed. Snapshot output remains bounded by the existing 48 registry records and 16 keyboard records.

The snapshot reads application state and updates only its diagnostic identity trackers when an object reference changes. It does not initialize or mutate registry descriptors or lifetime entries.

## Guest state comparison

The retained serial log is `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-r11r3-001/guest-001/serial.log` (SHA-256 `5E6287481B04DE375B4FBFF545C917B6531A240A5397EE6D4D3E6089CA909A00`). Initialization logged all five descriptors, selectors 4 through 8. The table compares the launch-return snapshot at sequence 13 with the first input-entry snapshot at sequence 16.

| Observation | Launch return, seq 13 | First managed input, seq 16 |
|---|---:|---:|
| Registry identity / epoch (`rg/re`) | `1 / 1` | `1 / 1` |
| Descriptor length (`n`) | `5` | `0` |
| Lifetime owner identity (`p`) | `1` | `1` |
| Pool storage identity (`ps`) | `1` | `0` (null) |
| Pool initialization epoch (`pe`) | `1` | `0` |
| Notes owner generation / pool generation (`ownerGen/g`) | `1 / 1` | `0 / 0` |
| Resident mask (`m`) | `1` | `0` |
| Clear / replace / teardown / reset counts | all `0` | all `0` |
| GC collection count (`gc`) | `0` | `0` |
| Pool writer count / last site (`w`) | `1 / 1` | `0 / 0` |

At sequence 19, `TryFind(4)` returned `-4` with `n=0`, `m=0`, and `w=0:0`. The QMP `a` transaction reached PS/2 decoding, the ordinary-key queue, kernel dequeue, focused Notes target, native managed entry, and managed dispatch. Notes' callback was not reached. The before- and after-input screenshots have identical SHA-256 `E4FBDD78149B5524B885486FDE1C13DD6DF62BE2429C7B34421906459296EFEF`, so no text insertion is evidenced.

The earliest safe managed snapshot remains input-dispatch entry. The kernel's existing main-loop focus record proves Notes focus, but this source set exposes no safe managed registry snapshot between launch return and first input; no polling interface or ABI change was added. The precise mutation remains somewhere after sequence 13 and before or during sequence 16. The counters show that no Gen0 collection was reported and no tracked pool API writer ran, but the writer counters reside in the affected pool object and were themselves lost, so they cannot rule out a write that bypasses those APIs.

## Hosted qualification

- Isolated managed provenance host suite: **PASS** — R9 14 cases, R10 14 cases, R11 13 cases. Manifest: `out/dotnet/c188r11-managed-state-provenance/hosted/state-provenance-017/manifest.json` (SHA-256 `9CE01E0780651FE73C782B3AD305C793E85495B6205545EDD6B893518E9B74CC`).
- Existing managed UI regression aggregate: **PASS**, 38 suites / 930 cases. Manifest: `out/dotnet/c188r11r3/hosted/managed-ui-001/managed-ui-host-manifest.json` (SHA-256 `3E5931F8D239AB973068ABBC6A9CF41621446013853C6C8B69C08D8AF70BB5FB`).
- R11 runner predicates: **26 PASS**; output bounds: **5 PASS**, largest fixture 168 bytes.
- R2 runner predicates: **9 PASS**; keyboard source contracts: **15 PASS**, including the isolated-candidate path check.
- `git diff --check`: **PASS** (Git emitted only its LF-to-CRLF normalization notices).

## Builds

- Diagnostic NativeAOT candidate: **PASS**. ELF64 validation reported 7 load segments, 0 relocations, and 8 retained symbols. ELF SHA-256: `9DADC8E4A97B270799481E067B9036EDB6A357A3C24D0B49AA71A4E6B426DE47`. Qualification manifest: `out/dotnet/c188r11-managed-state-provenance/build/diagnostic-candidate-001/nativeaot/qualification/nativeaot-qualification.json`.
- Diagnostic kernel: **PASS**, SHA-256 `0032BD297D86314D86B182C8CFDC7A15E3C12FE07BDEE5F192A2BFFD78403B93`.
- Proof-disabled production NativeAOT: **PASS**. Qualification confirms the standard five production selectors and absence of the development CounterApp descriptor/route. ELF SHA-256: `5F6E28E42F28B31F8528A4E46F14B34E80663C1AC871AC589C52F1B68DA880DE`.
- Production kernel: **PASS**, SHA-256 `2BFCB9D3BEDEA1C4C161B5978EC1A71B33BCF11FAC846815B6D9EE49986465BF`.
- No managed ABI or startup contract was changed.

## QAC admission, boot, and cleanup

QAC was online at admission with zero QEMU processes (zero foreign), zero reservations, and zero managed sessions. One owned acceptance reservation (`5d2a0698d9f649bba997d511178657c6`) launched QEMU PID `18972`. Exactly one guest boot was used and exactly one ordinary ASCII `a` down/up transaction was sent.

The guest outcome was `MANAGED_DISPATCH_QUALIFIED`: native and managed dispatch were reached, but the lookup saw `n=0`, `m=0`, returned `-4`, and did not call Notes. QAC verified release and process exit; postflight returned online with zero QEMU processes, reservations, or managed sessions and no degraded-census warning. The QAC keyboard manifest is `out/dotnet/c188r11-managed-state-provenance/runs/diagnostic-r11r3-001/guest-001/r2-keyboard-dispatch.json` (SHA-256 `2A634A5BC1F6B0754E7968BE172E167AE1C456B6047386849B559A3EC97936BB`). Acceptance manifest SHA-256 is `1663C4F2D9C720FD3741B8FFAA30419A022836B003F6D173F56F0AEA086B63CE`.

The source ESP ramdisk was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` before copying and had the same hash after the guest run. Only the isolated candidate copy was changed: its staged ramdisk hash is `5A769B0486993B0444AA3EB3E25EBF7D983980D04FBEF303350A3AD8C1713DD8`. Staging reports 185 unrelated files preserved and zero other files changed. Staging manifest SHA-256 is `F5897DE60F85D1DCDDB4D122C0E61534780769759353449197D77865A83BCFD6`.

## Files changed and Git disposition

Intended tracked changes for this phase:

- `samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs`
- `samples/managed/HostLogProof/GuideXos/GuideXosApplicationRegistryC188R11Tests.cs`
- `samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationC150Tests.cs`
- `scripts/dotnet/c188r11-runner-predicates.ps1`
- `scripts/dotnet/c188r11r2-runner-predicates.ps1`
- `scripts/dotnet/fixtures/c188r11/state-provenance.serial.txt`
- `scripts/dotnet/run-c188r11-managed-provenance-tests.ps1`
- `scripts/dotnet/run-c188r11r2-keyboard-dispatch.ps1`
- `scripts/dotnet/test-c188r11-output-bounds.ps1`
- `scripts/dotnet/test-c188r11r2-keyboard-contracts.ps1`
- `scripts/dotnet/test-c188r11r2-runner-predicates.ps1`
- `docs/dotnet/NATIVEAOT_C188R11R3_REGISTRY_STATE_LOSS.md`

The pre-existing `ESP/ramdisk.img` modification and unrelated generated untracked directories are excluded from the phase commit and remain untouched. Build and guest evidence remain under `out/dotnet/c188r11-managed-state-provenance/` and `out/dotnet/c188r11r3/`.

The intended phase changes are committed locally. Consistent with the existing C188 phase policy, no remote push is attempted. The resulting commit and final `HEAD` are reported in the task handoff.

## Next action

Use a future authorized diagnostic run to identify the write instruction, with a narrowly scoped data watchpoint on the descriptor array's length/header and the lifetime object's `_entries` field, after exporting their addresses at launch return and before the first input dispatch. Keep the run single-boot and do not alter registry semantics until that writer is proven.
