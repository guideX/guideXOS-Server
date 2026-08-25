# NativeAOT Workstation GC: Second-Collection Continuation

Date: 2026-08-25
Branch: `v1.1_DOTNET_SUPPORT`
Predecessor: C48 commit `0580522e5e3528a031d16e5e2a99686dcb078e2a` (`Repair NativeAOT iterator FP ownership handoff`)

## Result

C011EC49 completed as Outcome A on three fresh QEMU boots. The continuation was observed through the production NativeAOT Workstation GC planner and collection path; the planner result was captured, not forced.

Runtime identity remained locked to:

- NativeAOT 9.0.0, AMD64
- Workstation GC, one heap
- server/background/concurrent GC disabled
- GC interfaces 5.3 / EE 2
- locked runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

The three runs produced the same semantic signature: `0x00000001|0x00000001|0x00000002|0x00000008|5`.

## Production observation path

The C49 observer is attached to the existing production seams:

1. C39 planner entry captures the second-collection plan inputs.
2. C39 final decision captures the actual compact/sweep result.
3. C39 phase entry and C40 compaction callbacks observe relocation, compaction, or sweep.
4. C37 managed completion observes collection completion, RestartEE, and managed resume.
5. C41 records the bounded post-GC allocation sequence; C49 records the final frontier and root-update state.

The successful compacting path emitted, in order, the C49 collection/preflight/planner, relocate, compact, collection-done, restart, resume, and completion markers. The observed decision was compacting (`plannerDecision=1`, `compacting=1`, `relocating=1`, `compactBranch=1`, `sweepBranch=0`).

Representative stable fields on all three boots:

- collection ordinal `2`, condemned generation `1`, maximum generation `2`
- planner-entry collection count `4`
- promoted roots `4`
- relocation callbacks `0x18`, root-update callbacks `0x19`
- root rewrites `0x0C`, unchanged roots `0x0D`
- live plugs `6`, dead gaps `5`
- bounded post-GC allocations `8`
- invariant failures `0`, sensitive diagnostic allocations `0`, safe-stop reason `0`
- nonzero heap, active segment, frontiers, and first post-update object/segment/generation

C26 promotion evidence is snapshotted once when its completion marker is emitted. This prevents the later live C19 counter from changing the immutable collection-1 promotion value used by C49 validation.

## Regression matrix

The C49 manifest classified these predecessor checks as passing on every fresh boot:

| Checkpoint | Evidence |
|---|---|
| C18 | valid manager/FindMethodInfo path |
| C26 | promoted roots exactly 4 |
| C28 | mark-queue closure |
| C34 | relocation preflight and root callbacks |
| C39 | planner decision observed without forcing the result |
| C40 | production compact/sweep branch |
| C41 | eight bounded post-GC allocations |
| C44 | malformed transition provenance |
| C45 | reverse-P/Invoke slot provenance |
| C46 | REGDISPLAY handoff |
| C47 | no former invalid root base/slot; no former CR2 fault |
| C48 | iterator FP ownership markers |

C42 remains retained as historical third-collection lifecycle evidence and is intentionally not enabled by C49.

The ordinary kernel and ESP artifacts were restored after the specialized run. Both matched the normal hash:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

As a separate ordinary-boot smoke, the restored kernel reached `[KERNEL] Entering main loop` and emitted `[NAVIGATOR-SMOKE] result=PASS` on all three disposable boots. Those serial logs are retained under `out/dotnet/c011ec49-second-collection-continuation/ordinary-boot-smoke/`. The generic startup-helper classifier expects a startup-test marker that the normal kernel does not emit, so its marker field is not used as the ordinary-boot result.

## Evidence and harness

The exact three-boot evidence is retained at:

`out/dotnet/c011ec49-second-collection-continuation/run-20260825-124404812/`

The handoff manifest is:

`out/dotnet/c011ec49-second-collection-continuation/run-20260825-124404812/manifest.json`

The reusable harness mode is:

```powershell
pwsh -NoProfile -File scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1 `
  -ProofMode second-collection-continuation -FreshBootCount 3
```

No managed `GC.Collect`, planner-result override, fabricated root slot/object, policy change, or third-collection enablement was added.
