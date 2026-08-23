# NativeAOT Workstation GC — C011EC36 Same-Handle Lifetime Transition

## Result

C011EC36 reached Outcome C / success level 3 on three fresh QEMU 11.0.0
boots. The same genuine `HNDTYPE_WEAK_SHORT` handle survived Collection 1
while its `byte[64]` target was a real NativeAOT GC-info root, survived
production compaction and relocation, and was updated to the relocated target.
After `CreateAndRunLiveCollection1` returned naturally, a later collection
condemning the target's generation found no strong root or graph path, reported
the target dead, and cleared that exact existing weak slot to null.

The central question is therefore answered yes:

```text
live root -> marked/preserved -> moved -> root and weak slot updated
          -> RestartEE and managed resume -> helper return
          -> no strong reachability -> unmarked/not promoted -> same slot cleared
```

No diagnostic code copied the object, changed a mark bit, recreated a handle,
or wrote the root or weak slot. The only weak-slot mutation was the production
short-weak clearing store.

## Locked identity and Git boundary

The runtime identity remained exactly:

| Item | Value |
|---|---|
| NativeAOT | `9.0.0` |
| architecture | AMD64 |
| GC | Workstation |
| GC interfaces | `5.3 / 2` |
| runtime source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| QEMU | `11.0.0 (v11.0.0-12122-ga4bb4b10c9)` |

The repository baseline was verified before edits and treated as authoritative:

| Item | Value |
|---|---|
| starting branch | `v1.1_DOTNET_SUPPORT` |
| starting HEAD | `9a9910e57836d004124d0a6c109bacf54cddafb7` |
| upstream | `origin/v1.1_DOTNET_SUPPORT` |
| starting divergence | ahead `1`, behind `0` |
| starting worktree | clean |
| starting untracked entries | `0` |

No reset, amend, rebase, squash, discard, or push was used. The proof build
was deliberately uncommitted while it ran; generated evidence remains outside
the source commit under
`out/dotnet/c011ec36-lifetime-transition-complete/`.

## C33 and C35 prerequisite

C33 created one managed `byte[64]`, one genuine short-weak handle, and a
no-inline `CreateAndRunLiveCollection1` helper. Collection 1 enumerated a real
GC-info stack root, marked the target, and preserved the short-weak handle.
C35 then completed the same collection authentically:

| C35 identity | Value |
|---|---:|
| original target | `0x0000000100A01F38` |
| original weak value | `0x0000000100A01F38` |
| exact weak slot | `0x00000001040213F8` |
| C35 relocated target | `0x0000000100901F50` |
| managed root after relocation | `0x0000000100901F50` |
| weak slot after relocation | `0x0000000100901F50` |
| handle type | `0` (`HNDTYPE_WEAK_SHORT`) |
| handle table | `0x0000000010242BE0` |
| handle segment | `0x0000000104020000` |

The C35 `HANDLE marker=C011EC35-HANDLE` reported the old-to-new update,
`shortWeakRewritten=1`, and the same slot/table/segment provenance. `RestartEE`
returned, and the managed continuation emitted:

```text
RESUMED marker=C011EC36-RESUMED method=CreateAndRunLiveCollection1
weakSlot=0x00000001040213F8 weakValue=0x0000000100901F50
relocatedTarget=0x0000000100901F50 sameSlot=1
```

The stale pre-relocation address was not used as the Collection-2 target.

## Collection 1

The root-owning method was `CreateAndRunLiveCollection1`; the captured managed
frame method-info identity was `0x0000000004EAD3E8`, with ControlPC
`0x0000000010001BE9`, method range
`0x0000000010001B20..0x0000000010001CC2`, and GC-info address
`0x00000000101232EF`. The genuine root slot was
`0x0000000004EADA80` and contained the original target.

| Collection-1 evidence | Count/state |
|---|---:|
| target root matches | `1` logical root slot |
| target Promote | `1` |
| target marked | `1` |
| mark word / mask | `0x0000000010271711 / 0x00000001` |
| short-weak liveness | live / `IsPromoted=true` |
| short-weak preserved | `1` |
| condemned generation | `0` |
| collection reason | `5` |
| compacting / relocating | `1 / 1` |
| GC root scan entries / returns | `2 / 2` (mark and relocation passes) |
| RestartEE entries / returns | `1 / 1` |
| managed resume | `1` |

The root counter is logical per-slot: the same physical root can be reported
again during the relocation-side scan, but it is counted once. This preserves
the per-collection meaning of the root evidence while C35 separately records
the root rewrite.

EE and ThreadStore invariants were all valid: EE suspended, ThreadStore lock
held, and managed entry prohibited were each `1`. Sensitive allocations and
managed re-entry while suspended were each `0`. Collection 1 reached
compaction, `GcDone`, `RestartEE`, and managed resumption normally.

## Natural lifetime boundary

The helper returned through ordinary managed control flow. It was not
force-unwound, and stack memory was not manually nulled. Only scalar identity
fields crossed the helper boundary. The outer method reset the returned scalar
setup value before starting its post-C1 allocation workload.

The persistent handle is the original handle-table entry; the managed
`GCHandle` value was a local setup value, not a replacement reference. The
post-return workload retains only four diagnostic sentinel arrays and the
current allocation. These arrays are independently allocated objects and are
not the target; the Collection-2 root audit confirms that none matched the
relocated target. There are no caller locals, arguments, static fields,
ThreadStatic fields, normal handles, or temporary diagnostic managed references
to the target. The target's numeric identity is retained only in native scalar
diagnostic state.

The post-return workload uses ordinary 64-KiB managed allocations to reach a
real collection condemning the promoted target generation. It does not call
`GC.Collect`, `RhpCollect`, or a diagnostic scheduler path.

## Collection 2 root and graph proof

The successful Collection-2 record was the authentic collection with
`condemnedGeneration=1`; earlier normal allocation pressure produced Gen0
collections and was not misreported as the target-death collection.

| Collection-2 evidence | Value |
|---|---:|
| condemned generation | `1` |
| collection reason | `5` |
| compacting / relocating | `0 / 0` |
| EE suspended / ThreadStore lock / managed entry prohibited | `1 / 1 / 1` |
| root-owning frame matches | `0` |
| root-owning frame absent | `1` |
| managed frames observed | `5`; the C33/C35 helper frame was absent |
| stack/register target matches | `0 / 0` |
| static/ThreadStatic target matches | `0` |
| normal-handle target matches | `0` |
| ThreadAbort target matches | `0` |
| other ordinary strong-root matches | `0` |
| graph-derived promotions | `0` |
| target queue insertions | `0` |
| target child discoveries | `0` |
| target Promote count | `0` |
| target mark writes | `0` |
| mark word before / after | `0x0000000010271710 / 0x0000000010271710` |
| mark mask | `0x00000001` |
| target marked after closure | `0` |

The active frame set was enumerated through the NativeAOT code manager and
real GC-info providers. No arbitrary stack scan was used. The four unrelated
sentinel-array roots observed in the outer frame were not equal to
`0x0000000100901F50`.

## Same-slot short-weak liveness and clearing

The authentic short-weak path reached the same slot:

```text
GCScan::GcShortWeakPtrScan
 -> Ref_CheckAlive
 -> CheckPromoted
 -> GCHeap::IsPromoted
```

| Weak-liveness evidence | Value |
|---|---:|
| exact slot | `0x00000001040213F8` |
| target before callback | `0x0000000100901F50` |
| target generation | `1` |
| condemned generation | `1` |
| mark word / mask | `0x0000000010271710 / 0x00000001` |
| same-slot structural match | `1` |
| `CheckPromoted` decision address | `0x00000000100C1E6C` |
| `IsPromoted` | `false` |
| liveness result | dead / not promoted |

The production clearing store was captured as
`collection2ClearFunction=0x000000001008427C`; the same address was recorded
by the clearing-store hook. The mutation occurred once:

```text
slot 0x00000001040213F8:
    0x0000000100901F50 -> 0x0000000000000000
```

The completion record reports `mutationAttempted=1`, `cleared=1`,
`collection2Dead=1`, `collection2Live=0`, and `collection2IsPromoted=0`.
No diagnostic slot write, handle allocation/free, or replacement handle was
observed.

## Per-collection accounting

| Counter family | Collection 1 | Collection 2 |
|---|---:|---:|
| target root matches | `1` | `0` |
| target Promote | `1` | `0` |
| target marked | `1` | `0` |
| graph-derived promotions | — | `0` |
| target queue insertions | — | `0` |
| short-weak callbacks | `1` | `1` |
| liveness decisions | `1` live | `1` dead |
| weak preserved | `1` | `0` |
| weak cleared | `0` | `1` |

The counters are stored in separate C1/C2 records. The harness does not reuse
cumulative counters as per-collection evidence.

## Three-run proof

Evidence root:
`out/dotnet/c011ec36-lifetime-transition-complete/run-20260823-121929358`

| Run | Result | Serial SHA-256 |
|---|---|---|
| first-run | Outcome C / level 3 | `A68A5F62E4621AC6F9B637C14122828E076A285E75D75A0914177F6EF7A890D0` |
| repeat-1 | Outcome C / level 3 | `ABB4B1BFE407D302FB722E5AD05CB9023D258DBFA298DABA9784757585E5E328` |
| repeat-2 | Outcome C / level 3 | `903237CA7CA0CC21EEDDE1FD82C8852CCB6F6DCEB897176593CEDFF38D03DE2D` |

Serial hashes differ because boot-time addresses and diagnostic chronology are
serialized, but all semantic fields agreed across the three runs.

Proof artifact hashes:

| Artifact | SHA-256 |
|---|---|
| proof kernel | `14B624CD75C50FA6FFD00E813179BC872AADF6E8E10FEE67CE0E419A1C8DF82E` |
| PE payload | `9E17246BFA1F0849824BF4909305AB4FB848B492F732091F841A9C56A25CE9C4` |
| ELF payload | `BFDB9E735AC991BE63E6B8AEBECCD10DD2CF727A6DB313DF0B14801BCD1A1E31` |
| MAP | `1DA0A22A0B16C65374AB702D1DD0327D77F3536D5144A93478501BABF73F4FEE` |

## Regressions and validation

The C36 harness retains the C19-C35 chronology and guards, including:

- authentic NativeAOT frame and GC-info stack enumeration;
- native unwind provider and normal stack-walk completion;
- mark-queue closure and graph accounting;
- C31 live-short-weak preservation and C32 dead-short-weak clearing;
- C34 managed-root relocation;
- C35 relocated weak-handle update, `RestartEE`, and managed resume;
- PE-to-ELF conversion, linker/table checks, locked-source guards, and
  PowerShell parsing;
- ordinary boot smoke and `git diff --check`.

C36 does not replace C31/C32 semantics: the historical rule remains
marked/live short weak -> preserve and unmarked/dead short weak -> clear. The
new proof applies both outcomes to the same allocated object and same handle
across consecutive collections.

## Restoration and handoff

The proof payload was removed from the ordinary deployment after testing. Both
ordinary source-state kernel destinations were restored and matched the locked
ordinary hash:

```text
kernel/build/amd64/bin/kernel.elf
ESP/kernel.elf
75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6
```

The proof kernel hash was retained only in the evidence manifest and was not
left as the ordinary kernel. Repository-owned QEMU processes were terminated
by the harness; unrelated QEMU instances from other workspaces were preserved.

The intended source changes are the C36 managed workload, diagnostics header
and implementation, QEMU harness, and this document. Generated `out` evidence
is not part of the source commit. Push remains unauthorized and was not done.

## Next smallest milestone

Use the closed C36 result as the baseline for the next NativeAOT Workstation
GC milestone: preserve the same per-collection accounting while expanding the
post-clear boundary only if a later collector phase is needed. The core
live -> moved -> resumed -> root-ended -> dead -> same-handle-cleared proof is
complete.
