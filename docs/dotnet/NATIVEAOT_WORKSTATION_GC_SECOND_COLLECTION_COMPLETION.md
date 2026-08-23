# NativeAOT Workstation GC — C011EC37 Second Collection Completion

## Outcome

C011EC37 achieved Outcome C / Level 3. The same authentic short-weak handle
that survived and relocated during Collection 1 was cleared during the
condemned-generation-1 pass of Collection 2. Collection 2 then completed
through its remaining Workstation GC phases, returned through `RestartEE`, and
resumed managed execution for a deterministic scalar checkpoint.

This milestone removes only the C011EC36 post-clear proof stop. It does not
add a third collection, a stress loop, allocation-reuse testing, or a new weak
reference workload.

## Locked identity and C36 boundary

The locked identity remained unchanged:

- NativeAOT `9.0.0`
- AMD64
- Workstation GC
- interfaces `5.3 / 2`
- runtime source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

C36's boundary was retained. Collection 1 began with target
`0x100A01F38` and weak slot `0x1040213F8`. The target moved authentically to
`0x100901F50`, and the same weak slot was updated to that relocated value.
`CreateAndRunLiveCollection1` returned naturally. Collection 2 then observed
zero strong reachability, an unmarked target, `IsPromoted=false`, and cleared
the same slot from `0x100901F50` to `0`.

## Collection chronology

The final condemned-generation-1 Collection-2 chronology was captured with 19
bounded phase events. Phase numbers are stable IDs; serial order is the actual
chronology.

| Order | Phase | Event | Mutation |
| ---: | --- | --- | ---: |
| 1 | short-weak-return | return | 1 |
| 2–3 | finalization | entry / return | 0 / 0 |
| 4–5 | long-weak | entry / return | 0 / 0 |
| 6–7 | sync-block weak | entry / return | 0 / 0 |
| 8 | plan | entry | 0 |
| 9 | relocate | entry | 0 |
| 10–11 | root relocation | entry / return | 0 / 0 |
| 12 | handle relocation | entry | 0 |
| 13 | relocate | return | 0 |
| 14 | compact-or-sweep | entry | 0 |
| 15 | compact-or-sweep | return | 0 |
| 16 | generation bounds | complete | 1 |
| 17 | card/brick cleanup | complete | 1 |
| 18 | plan | return | 0 |
| 19 | GC done | complete | 1 |

Finalization, long-weak, and sync-block weak phases all returned normally with
no mutations and no blockers. The plan selected `compacting=0` and
`relocating=0`; therefore this was the noncompacting/sweep side of the shared
compact-or-sweep path. The shared locked hook reports the sweep entry/return
as `compact-or-sweep-entry` and `compact-or-sweep-return`. Root and handle
relocation hooks were reached, but their mutation flags were zero and no dead
target relocation or stale update occurred.

Collection 2 recorded:

- condemned generation: `1`
- reason: `5`
- target strong-root matches: `0`
- target Promote calls/matches: `0`
- target marked: `0`
- dead decisions: `1`
- clears: `1`
- same weak slot: `0x1040213F8`
- value before scan: `0x100901F50`
- value after clear and after managed resume: `0`
- stale weak pointer count: `0`
- dead-target re-root count: `0`
- phase-order errors: `0`

The workload naturally performed two preliminary gen-0 collections while
building pressure for the historically observed gen-1 condemnation. Those
were not an added proof collection; C37 withheld its managed checkpoint until
the C36-equivalent dead-clear preflight had occurred. The final C2 record and
its phase chronology describe the gen-1 collection that cleared the handle.

## RestartEE and managed continuation

`C011EC37-PREFLIGHT` was emitted immediately after the production clear and
before the remaining phase events. The final C2 record then showed:

- collection completed: yes
- `RestartEE` entry: 1
- `RestartEE` return: 1
- EE suspended before restart: 1
- ThreadStore lock held at the suspended boundary: 1
- ThreadStore lock recursion diagnostic: 1
- cooperative state: 1
- preemptive state: 0
- EE resumed after restart: 1
- sensitive allocations while suspended: 0
- managed re-entry attempts while suspended: 0
- safe-stop reason: 0

After the normal restart return, managed method
`RunC011EC37ManagedCheckpoint` executed one scalar checkpoint (`1`) without
allocating a new managed object. The checkpoint observed the same weak slot as
null and emitted:

```text
C011EC37-MANAGED
C011EC37 outcome=C
```

## Collection comparison

| Property | Collection 1 | Collection 2 |
| --- | --- | --- |
| Completed | yes | yes |
| Mode | compacting / relocating | noncompacting / sweep-side |
| Weak target | live and preserved | dead and cleared |
| Weak slot | relocated authentically | same slot remained null after clear |
| `RestartEE` | returned | returned |
| Managed resume | yes | yes |
| Post-GC checkpoint | not applicable | scalar checkpoint succeeded |

## Three-run proof

Three fresh QEMU 11.0.0 boots were validated. All three agreed on the C1
relocation/resume evidence, C2 dead clearing, 19-event post-clear chronology,
GC completion, `RestartEE` return, null weak slot, and managed checkpoint.

Evidence root:
`out/dotnet/c011ec37-second-collection-completion/run-20260823-135613689`

Serial SHA-256 values:

1. `FF1B166BB420964ADAC035FB73A7E69C95347F8D055BCE9EB2BA12AF1EAB7A1D`
2. `9209F2192E59ECB905D7AB186F9323EE28D820E0D88CA265A16E66BF4A666D04`
3. `7A67ABF4EA1D008C7089A9C67095CD8B3D0BBA4B4AC35D63D997371EB023BE3E`

Proof artifact hashes:

- specialized proof kernel: `CD26AE3D913610126BDF1C6D560BD6732DEC64B342A4F251819DDAA4E55DE5B3`
- managed PE: `74838A0802616D8EB18FEC9059DE616FB29403647B5CD12F5CC69411473EDB3B`
- ELF: `20B1C6E8FDE26E3E0F9A8D208268055F1E9E8A658C7F1E36A92689687A99CDF8`
- MAP: `20BA3A92116B23ED55FF86C6F78A9709F788FDD377C7C2774EEBA6599FA5308F`

## Regression and artifact status

C19–C36 chronology guards remain enabled, including complete stack walk,
native unwind provider, mark queue closure, C31 live semantics, C32 dead
semantics, C34 root relocation, C35 relocated weak-handle update, and C36
same-handle lifetime transition. PE→ELF conversion, linker/table validation,
PowerShell parsing, source/linker guards, ordinary boot smoke, and
`git diff --check` passed.

The ordinary kernel/ESP source-state hash before proof and after restoration
was:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

The proof payload was removed from the ordinary boot path, the ordinary
kernel and ESP were restored to that hash, and repository-owned QEMU
processes were gone after validation. Unrelated VMs were not touched.

Before C37 work, the authoritative Git baseline was branch
`v1.1_DOTNET_SUPPORT`, HEAD
`e573f58c6d6d2616d4873783dbe330bb00e38342`, upstream
`origin/v1.1_DOTNET_SUPPORT`, divergence `0 ahead / 0 behind`, clean
worktree, and zero untracked entries. The final coherent changes are limited
to the managed proof workload, C37 diagnostics, the QEMU harness, and this
document. No push was performed.

## Next smallest milestone

The next smallest milestone is independent target reclamation/reuse evidence
for the already-proven dead object. It should remain separate from this
repeated-collector-completion proof and must not be added to C011EC37.
