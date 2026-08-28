# NativeAOT Runtime-Pack Aggregate CI/Release Gate

## Scope and lineage

This document defines the canonical full validation gate for the productionized guideXOS NativeAOT runtime-pack. It closes C011EC52 on top of C011EC51 (`df96f9af1a7d891d8bb2adf143db394d5f0663c4`, `C011EC51 harden runtime-pack validation`).

C50 productionized the durable C46 caller-frame-pointer repair and C48 iterator-frame-pointer ownership repair. Production mode does not rewrite the proof workload or inject C46/C47/C48 semantic behavior. C51 added fail-closed runtime identity, patch-state, fresh-build, object/archive, bounded-process, productionized-GC, and precise ordinary-boot validation. C52 composes those checks in one parent invocation.

The final C51 Tier All attempt was interrupted after Tier A/B when the host froze. The operator assessed that unrelated RDP activity was the likely cause. C52 did not reproduce a validator hang, unbounded memory condition, orphan validator QEMU, process leak, build deadlock, or other pipeline-owned failure. The earlier event therefore remains classified as an external host interruption; it is not attributed to guideXOS, Codex, QEMU, or NativeAOT without evidence.

## Locked identity

The gate rejects identity drift and continues to use the checked-in lock:

| Field | Value |
|---|---|
| NativeAOT | 9.0.0 |
| Architecture | AMD64 |
| GC | Workstation |
| GC interfaces | 5.3 / 2 |
| Runtime source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| FP patch | `tools/dotnet/runtime-pack/patches/nativeaot-amd64-fp-handoff.patch` |
| FP patch SHA-256 | `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31` |

If the default NativeAOT checkout is dirty, the gate rejects and preserves it. The successful C52 run created a clean detached worktree from the already-present locked commit object:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c52-runtime-source\source-282a5259`

That worktree was pinned to the locked commit and passed a long-path-aware clean-status check. The runtime-pack build and Tier C both used that same path. The dirty default checkout was not modified.

## Canonical command

Full release validation is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\Invoke-C011EC51RuntimePackValidation.ps1 -Tier All
```

The entrypoint name is retained for C51 compatibility; `-Tier All` is the C52 canonical release/CI gate. It runs the required sequence in one parent execution:

`Tier A -> Tier B -> Tier C -> Tier D`

Narrower developer checks remain available with `-Tier A`, `-Tier B`, `-Tier C`, or `-Tier D`; they are not substitutes for the full release gate. The default evidence root is `out\dotnet\c52-runtime-pack-validation`.

## Tier requirements

### Tier A — static and patch-state validation

Tier A validates the lock and patch identity, PowerShell parsing, the semantic-rewrite guard, fresh-build/archive guard presence, generic Native ELF source guards, PE-to-ELF regression, `git diff --check`, and all seven fail-closed patch fixtures:

1. pristine source applies exactly once;
2. already-patched source is idempotent;
3. source drift fails closed;
4. partial application fails closed;
5. wrong runtime revision fails closed;
6. missing target fails closed;
7. patch identity mismatch fails closed.

### Tier B — clean fresh runtime-pack

Tier B requires a clean source checkout at the locked commit, applies and validates the checked-in patch deterministically, and builds under a fresh run-owned output root. It validates patched source hashes, patched object hashes, the adapted `Runtime.WorkstationGC.lib`, archive membership, removed stock members, duplicate prevention, source identity, lock identity, and the runtime-pack manifest.

### Tier C — productionized Workstation GC

Tier C consumes the exact Tier B runtime-pack and runs three fresh QEMU 11.0.0 productionized boots. It requires no C46/C47/C48 semantic rewrite, C18, C26 root scanning with four promoted roots, C28 mark closure, authentic repeated Workstation GC, Collection 2 generation 1, maximum generation 2, planner `COMPACT`, compacting 1, relocating 1, sweep 0, `RestartEE`, managed resume, live-object integrity, and eight bounded post-GC allocations. Each boot must be free of page fault and fail-fast evidence, and each serial is retained by path and SHA-256 rather than embedded wholesale in the aggregate JSON.

### Tier D — precise ordinary guideXOS boot

Tier D uses `Validate-GuideXOSOrdinaryBoot.ps1`, not the generic Navigator wrapper’s broader marker set as its sole authority. It stages clean run-owned ESP state and requires three fresh ordinary boots, the exact kernel main-loop marker, Navigator PASS, no fail-fast/page fault/proof markers, and unchanged canonical kernel/ESP state.

## Authoritative C52 result

C52’s authoritative uninterrupted run was:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c52-runtime-pack-validation\run-20260827-221122765-5efc34b3`

Aggregate manifest:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c52-runtime-pack-validation\run-20260827-221122765-5efc34b3\c52.validation.manifest.json`

Manifest SHA-256 sidecar:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c52-runtime-pack-validation\run-20260827-221122765-5efc34b3\c52.validation.manifest.sha256`

The sidecar records the SHA-256 of the manifest bytes without a self-referential field. The authoritative result was `outcome=A`, success level 5, and process exit code 0.

| Tier | Result | Evidence |
|---|---|---|
| A | PASS, 7/7 fixtures | `tierResults.A` and `nativeaot-fp-repair-fixtures.json` in the aggregate root |
| B | PASS | `runtime-pack\runtime-pack.manifest.json` |
| C | PASS, 3/3 fresh GC boots | `gc-proof\run-20260827-221211801\manifest.json` |
| D | PASS, 3/3 fresh ordinary boots | `ordinary-boot\run-20260827-222009381-3e6b6acc\ordinary-boot.manifest.json` |

The Tier B manifest recorded the fresh adapted runtime library hash:

`1B86EF546AC4D634657ED81522ABD185AB34DA7EDBDB35E6A5D559EA67E221D9`

The archive contained exactly one of each patched member, zero removed stock members, and no duplicate patched members. The aggregate also captured the managed PE, ELF, MAP, proof-kernel, FP patch, ordinary kernel, and ordinary ESP hashes. The authoritative C52 aggregate manifest SHA-256 was:

`89955C3BE07928C85E88D09902AFC47DAAEC6CB3C0AC7EF8FB0604D6CDF2E0F9`

The authoritative GC serial hashes were:

```text
first-run  3C1FA9DC48616EC936611E796347E4C5E857A561C21DD2597235C7A98F6762FD
repeat-1   FAC75510C45486A9BDD20644896F181C5CA77692975A33F100C79E3122CA7E87
repeat-2   E65494E8F77340CFE42BC55A25FE3CDD533D2905F8E46223D005F7249E3E87B6
```

The Tier C retained signature was `0x00000001|0x00000001|0x00000002|0x00000008|5`. C18, C26, C28, `RestartEE`, managed resume, live-object integrity, and bounded post-GC allocation all passed on all three boots. The exact recorded values were four promoted roots, Collection 2 condemned generation 1, maximum generation 2, planner `0x00000001` (`COMPACT`), compacting `0x00000001`, relocating `0x00000001`, sweep `0x00000000`, and post-GC allocation count `0x00000008`.

The ordinary canonical kernel and ESP hashes were unchanged:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

The authoritative ordinary serial hashes are recorded in `tierResults.D.ordinaryManifest` and the precise ordinary manifest. The ordinary validator completed with main-loop and Navigator PASS for all three boots and no canonical artifact mutation.

## Exit semantics and failure handling

The top-level result is fail-closed. `-Tier All` returns exit code 0 only when every requested tier is PASS, all required manifests validate, the production semantic-rewrite guard passes, canonical ordinary artifacts remain intact, and owned-process cleanup passes. Any tier failure returns nonzero and records a deterministic `failureTier` and `failureCategory`; missing manifests are failures, not partial passes.

C52 proved this with a bounded negative Tier B invocation using a missing stock runtime-pack root. The nested top-level manifest recorded `TIER_B_FAILURE` and exit code 1, and the parent observed exit code 1. The aggregate recorded this as `negativePropagation.result=PASS` and `partialTierPassPrevention.result=PASS` without starting a failing QEMU tier.

Child output is kept in on-disk logs. The aggregate embeds only paths, hashes, bounded tails, and summaries. Per-boot and child command timeouts remain bounded. QEMU ownership is identified from command line and C51/C52 evidence roots; only confirmed stale owned processes may be terminated. In the authoritative run, no owned validator process or QEMU remained at final cleanup. An unrelated QEMU from `guideXOSServerV0.5_DEVELOPER_STUDIO` was observed outside the evidence root and preserved.

## Policy and limitations

C42 historical mode remains available for targeted historical work, but is intentionally excluded from Tier All. The later C49/C50 repeated-collection proof supersedes C42 for this production gate.

The gate proves the locked AMD64 Workstation-GC NativeAOT runtime-pack and the productionized single-thread/suspend-EE path. It does not claim Server GC, LOH, concurrent/background GC, multi-thread GC, or a new runtime feature. Windows Git long-path support is required for the clean source worktree. The aggregate manifest’s self-hash is carried in a sidecar by design.

No GC planner, root scanner, allocator, generation policy, Server GC, LOH, finalization, concurrent/background GC, or multi-thread GC semantics were changed for C52. C52 changes are orchestration, source provenance, bounded evidence, and release-gate documentation.

## Outcome

**Outcome A — Aggregate release gate complete. Success Level 5 — canonical release gate established.**

C52 closed the final C51 qualification by completing Tier A -> Tier B -> Tier C -> Tier D in one uninterrupted aggregate invocation. The same locked NativeAOT source and checked-in FP patch produced the validated runtime-pack, repeated productionized Workstation GC passed, ordinary guideXOS boot passed, and the aggregate command is now the canonical NativeAOT runtime-pack CI/release gate.

The earlier C51 host freeze was not reproduced and no evidence tied it to the NativeAOT validation pipeline; it remains classified as an external host interruption consistent with the operator’s unrelated RDP activity.

The next smallest milestone is substantive runtime capability work, not another validation-scaffolding expansion, unless a concrete release need requires a gate change.
