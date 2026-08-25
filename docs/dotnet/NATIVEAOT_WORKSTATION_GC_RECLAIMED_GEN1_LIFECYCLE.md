# NativeAOT Workstation GC reclaimed generation-1 lifecycle

## Result

C011EC42 is classified as **Outcome D — further natural proof blocked**, with
**success level 1**. The bounded experiment tracked the C40 recovered tail into
the next authentic allocation-pressure collection entry, but the existing
C011EC18 stack-safety boundary stopped execution before planner, phase,
`RestartEE`, allocator-eligibility, or reuse evidence could be safely claimed.

No collection was forced, no allocation pointer or generation boundary was
edited, and no `C011EC42-REUSE` marker was emitted.

## Locked identity and predecessor facts

- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `4c52c3e2f3baa929368d3d72bb49326246107c95`
- NativeAOT: `9.0.0`, AMD64, Workstation GC
- GC interfaces: `5.3 / 2`
- Locked runtime source: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- C40 recovered tail: `[0x100900028, 0x100943000)` (`0x42FD8` bytes)
- C40 owning segment/generation: `0x104010668` / generation 1
- C41 resumed allocation domain: generation 0, ephemeral segment `0x104010710`

The authoritative three-boot serial streams retained the C37, C40, and C41
predecessor markers on every boot. Regression runs on this tree also passed:

- C37 repeated-collection proof: Outcome C / Level 3
- C39 C37-variant planner proof: PASS, final COMPACT branch
- C40 compaction/reclamation proof: PASS / Level 1
- C41 post-GC allocator provenance: Outcome C / Level 2

## C42 natural workload and evidence

The managed continuation uses a capped `new byte[65536]` workload. It stops at
the bounded callback/finish path; it does not call `GC.Collect`, internal GC
entrypoints, allocator routines, or recovered-tail address selection logic.

Each fresh boot produced this semantic sequence:

1. `C011EC42-PREFLIGHT` captured the C40 tail and the C41 gen-0 context.
2. Two rare/refill-path 64-KiB allocations were observed in generation 0 on
   segment `0x104010710`; neither object overlapped the C40 tail.
3. `C011EC42-LIFECYCLE` and `C011EC42-COLLECTION` recorded the next authentic
   collection entry.
4. The source-backed ephemeral boundary callback observed
   `[0x100900000, 0x100B00000)` on segment `0x104010710`.
5. During root scanning, C011EC18 reached control PC `0x10A8BDF0` with no
   registered code manager. `C011EC42-BLOCKED` recorded `safeStopReason=C0420010`
   immediately before the existing `RaiseFailFastException` path emitted
   `FAIL_FAST reason=47435354`.

Planner decision, collection phase, `RestartEE`, post-collection allocations,
tail consideration, tail consumption, `C011EC42-ELIGIBILITY`, and reuse were
not observed. They are intentionally not inferred from the boundary snapshot.

The C42-only stack-safety hook is observational: it records the precise
blocking boundary and then preserves the locked C011EC18 fail-fast behavior.
The startup PAL registry remains fixed-size; released single-thread test-shim
slots are recycled, with owner and recursion validation unchanged.

## Three fresh QEMU boots

QEMU: `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`

Evidence root:
`out/dotnet/c011ec42-reclaimed-gen1-lifecycle/run-20260824-165101512`

Proof-kernel SHA-256:
`AB536BC51A0A2866AA6A1826CE8EF806003A36093DCD5DE58C26E34300F88E05`

| Boot | Result | Serial SHA-256 |
| --- | --- | --- |
| first-run | D / C011EC42-BLOCKED | `D3067D8547C58D77107DA3927F20A45A4573F1477F63CFDEDB54B337A8EE9495` |
| repeat-1 | D / C011EC42-BLOCKED | `1DE139A35F1376B4141F955A3328B26AADFB336BBE8017D288436E35415060BB` |
| repeat-2 | D / C011EC42-BLOCKED | `AE3B2BED78E5A07D19C00D52B07BC46D405980B46F660DFCA6051D47B88E030B` |

The byte streams differ as expected for independent boots, but the semantic
markers and blocker fields agree across all three.

## Artifact restoration

The ordinary artifacts were restored in the harness `finally` path and verified
after the final regression run:

- `kernel/build/amd64/bin/kernel.elf`:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`
- `ESP/kernel.elf`:
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

The exact C42 commands, manifests, serial logs, converted ELF, linker map,
and restoration record are retained below the evidence root above.
