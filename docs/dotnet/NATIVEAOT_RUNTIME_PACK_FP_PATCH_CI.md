# C011EC51 NativeAOT Runtime-Pack FP Patch and CI Validation

The C011EC52 aggregate release-gate closure supersedes this document’s historical C51 `-Tier All` result as the authoritative release gate. Use [NATIVEAOT_RUNTIME_PACK_AGGREGATE_RELEASE_GATE.md](NATIVEAOT_RUNTIME_PACK_AGGREGATE_RELEASE_GATE.md) for the complete A -> B -> C -> D release proof and current evidence.

C011EC51 continues the C50 runtime-pack work from commit `733ffb793b91477d3b959441829cbfbd1511cbd2` on branch `v1.1_DOTNET_SUPPORT`. It makes the NativeAOT AMD64 Workstation-GC frame-pointer handoff a reproducible, locked, fail-closed build input.

## Locked runtime identity

The lock is deliberately narrow:

- NativeAOT runtime pack `9.0.0`
- target framework `net9.0`, runtime identifier `win-x64`
- AMD64, Workstation GC, GC interfaces `5.3 / 2`
- source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- patch `tools/dotnet/runtime-pack/patches/nativeaot-amd64-fp-handoff.patch`
- patch SHA-256 `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`

The lock also records the expected pre-patch and post-patch file hashes, lengths, and semantic markers for `StackFrameIterator.cpp` and `CoffNativeCodeManager.cpp`.

## Patch state machine

`apply-nativeaot-fp-repair.ps1` accepts only the locked source revision, patch identity, and exact source bytes. Its states are:

| State | Behavior |
| --- | --- |
| `PRISTINE_EXPECTED` | Apply once, then validate exact post-patch bytes and ordered semantics. |
| `ALREADY_PATCHED_CORRECTLY` | No-op; validate the same exact post-patch bytes and markers. |
| `SOURCE_DRIFT` | Fail closed. |
| `PARTIAL_APPLICATION` | Fail closed when post-patch markers are incomplete or inconsistent. |
| wrong revision, identity, target, or patch hash | Fail closed with a diagnostic category. |

The helper produces a result manifest containing the source commit, patch hash, before/after hashes, state transition, and marker checks. The fixture suite exercises all seven paths: pristine apply, idempotent rerun, source drift, partial application, wrong revision, missing target, and patch identity mismatch.

## Runtime-pack construction

`build-runtime-pack.ps1 -NativeAotFpRepair -Clean` performs a fresh-output build. The external runtime checkout must be clean and at the locked commit. The stock runtime pack is checked against the lock before use; only the locked stock members are removed, and the patched `StackFrameIterator.cpp` and `CoffNativeCodeManager.cpp` objects are compiled from the durable patched source checkout.

The adapted `Runtime.WorkstationGC.lib` is normalized for deterministic archive headers and checked with `lib.exe /list`. The manifest records the lock and patch hashes, source and object hashes, archive hash, concrete archive membership, repository identity, and stale-output guard. A pre-existing output directory is rejected unless `-Clean` explicitly makes the build fresh.

The patch semantics are intentionally small and ordered:

- `StackFrameIterator.cpp` publishes the frame pointer returned by `GetFP()` before rehoming the register-display `pRbp` pointer.
- `CoffNativeCodeManager.cpp` snapshots the caller RBP storage, writes the context RBP through that storage, and restores the register-set pointer.

## Bounded validation tiers

The CI entrypoint is `scripts/dotnet/Invoke-C011EC51RuntimePackValidation.ps1`. It creates a unique evidence directory, uses fresh child processes, applies command timeouts, and kills only the timed-out child process tree.

| Tier | Proof |
| --- | --- |
| A | Windows PowerShell parse checks, lock/patch identity checks, semantic/stale/archive guard checks, and the seven-case fixture suite. |
| B | Fresh locked-source NativeAOT runtime-pack build and exact runtime-pack manifest validation. |
| C | C50 `productionized-second-collection` proof using that manifest, with three fresh QEMU boots and retained C49 semantic assertions. |
| D | Three fresh ordinary boots using exact normal markers, with canonical kernel/ESP hashes verified unchanged afterward. |

Run the complete gate on a Windows host with Visual C++, the locked source checkout, QEMU/OVMF, and the existing C50 boot prerequisites:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\Invoke-C011EC51RuntimePackValidation.ps1 -Tier All
```

For a separately acquired clean locked source checkout, pass `-ExternalRuntimeRoot <path>`. The entrypoint also accepts `-StockRuntimePackRoot`, `-CommandTimeoutSeconds`, and `-BootTimeoutSeconds` for controlled CI environments. Historical C51 evidence is below `out\dotnet\c51-runtime-pack-validation`; the current C52 aggregate evidence is below `out\dotnet\c52-runtime-pack-validation` and uses `c52.validation.manifest.json`.

The full `-Tier All` result is `outcome=A`, success level 5. Tier A alone is a static/fixture gate and reports success level 1; its top outcome remains `D` because no runtime-pack or boot proof was requested. Levels 2, 4, and 5 correspond to the fresh runtime pack, three-boot C50 proof, and complete ordinary-boot restoration proof respectively.

## Semantic rewrite guard and historical scope

Production C50 execution must use the durable patched source/runtime-pack manifest. The validator rejects C46, C47, and C48 semantic compile defines and rejects generated replacement-source injection. The old harness branches remain available for their historical scenarios, but they are not allowed to provide production C50 semantics.

C42 is explicitly excluded from C011EC51. Its third-collection lifecycle remains historical and available for reference; C49, C50, and C51 validate the second collection and ordinary boot without enabling that lifecycle.

The generic `scripts/smoke-navigator-kernel.ps1 -ScenarioFilter no_policy` wrapper is broader than the precise ordinary-boot proof and may require unrelated marker scenarios. A failure of those extra checks is not evidence that the exact C011EC51 ordinary-boot criteria failed. Use `Validate-GuideXOSOrdinaryBoot.ps1` for the dedicated three-boot check.

## Evidence interpretation

Every tier is fail-closed: missing manifests, unexpected source state, a non-clean checkout, stale output, wrong archive membership, semantic guard failure, timeout, missing boot marker, fatal boot text, or canonical artifact mutation makes the requested validation fail. A failed run preserves its bounded logs and top-level manifest so the next action is identifiable.

The runtime-pack and boot tiers are environment-sensitive by design. A blocked build caused by a dirty or unavailable external checkout is not converted into a static pass; acquire a clean checkout at the locked commit and rerun the same bounded entrypoint.
