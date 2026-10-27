# C188R11R4R2 Managed Notes Text Entry

- **Date:** 2026-10-26 (America/Los_Angeles)
- **Branch:** `v1.1_DOTNET_SUPPORT`
- **Starting HEAD:** `a3a83a38e0924d800ffa683af10666919bfd10f2`
- **Ending tested source HEAD (before closeout commit):** `a3a83a38e0924d800ffa683af10666919bfd10f2`
- **HEAD through guest shutdown:** `a3a83a38e0924d800ffa683af10666919bfd10f2`
- **Outcome:** **D — QAC_OR_EXECUTION_BLOCKED**

## Result

One QAC-managed guest boot reached Managed Notes with five registered managed
applications and selector 4 resident. One pointer click reached the Notes
document control and its callback returned success. The guest did not receive
the requested `a`: the runner's first focus predicate covered only the direct
text-area path, while this launch used the C120 control-host route. It failed
closed and sent no key. During the same run QAC detected an unmanaged QEMU and
marked the reservation degraded. The owned guest exited with code 0 and its
reservation was released, but the run cannot qualify text entry.

After preserving that guest run, I added snapshots around the active C120 route,
accepted a nonzero established caret in the parser, and added hosted coverage
for insertion through `GuideXosControlHost`. The final source passed hosted
checks and both NativeAOT builds. A fresh staged diagnostic candidate is
available for a separately authorized follow-up; it was not booted in this
phase. No `a` insertion, post-input buffer state, or visible character is
claimed here.

## Baseline and preservation

- The requested branch and starting HEAD were verified. No branch switch,
  reset, restore, clean, stash, history rewrite, or push occurred.
- The pre-existing modified `ESP/ramdisk.img` had SHA-256
  `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` at the
  start and after qualification.
- Pre-existing generated/untracked sample paths were retained:
  `ManagedHostServicesC185/bin,obj`, `ManagedStarterC182/obj`,
  `ManagedUiRegression/obj`, `C185CounterApp/bin,obj`,
  `HostLogProof/c182-host-smoke`, and `ManagedUiContracts/bin,obj`.
- The guest, preflight, postflight, hosted logs, and isolated builds are under
  ignored `out/dotnet/c188r11-managed-state-provenance/`. The source ramdisk was
  never used as a build or staging target.
- The C188 lifetime correction was not changed. No NativeAOT allocator,
  registry, ABI, or production Notes behavior was changed. The new observations
  compile only under `HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE`.

## Focus and edit route

The compositor's active window and the editor's focus are separate states. The
guest's readiness record identified Managed Notes as the focused window. The
Notes text area still needed an in-window focus action; Tab was not needed for
the selected procedure.

This launch entered the C120 managed control-host route before the direct
`ManagedNotes.HandleInput` text-area branch. C120 hit-tested the click as
`C120DocumentControlId`, then called `GuideXosControlHost.FocusAndRoutePointer`
for the registered `_textArea`. The control host routes that pointer event to
`GuideXosTextArea.HandlePointerDown`; `C120-POINTER target=document result=PASS`
means the control-host result was `Focused`. The serial also recorded native
pointer-down at Notes-local `(24,93)` and a successful managed callback return.
Thus the click did reach the document control. The original guest candidate did
not emit the new `C188R11R4R2` editor snapshots on this earlier C120 route, so
that run has no recorded caret or text-buffer snapshot.

Keyboard source distinguishes printable `KeyChar` events from key-down
commands. The kernel queues key-down makes; it consumes key-up without creating
a second application event. Ordinary printable `a` is routed as `KeyChar`. The
text area ignores characters when unfocused; a focused supported character
replaces the current selection, advances the caret, and reports `Changed`.
Notes renders the surface before returning success, so callback success alone
is not proof that an edit occurred. No keyboard event was sent in the guest run,
and no production editing defect was demonstrated.

The source-defined click used the 600×360 centered Notes window and an in-bounds
document cell. The observed run used screen `(364,337)`, Notes-local `(24,93)`.
The final, unbooted runner uses Notes-local `(24,99)`: under the active C120
layout `(20,72)` with 8×18 cells, this is row 1, column 0; it also remains in
the direct C175 editor bounds. After the click, the runner moves the cursor to
the title bar without a button press so the mouse pointer cannot cover the
editor text in either screenshot.

## Instrumentation and hosted coverage

The diagnostic snapshots read only the text area's bounded logical state:
input kind/value, local coordinates, focused flag, buffer length, caret,
editing result, render result, and ASCII buffer contents in chunks of at most
32 characters. The copy is capped by the existing 256-character editor limit.
No memory outside the editor buffer is inspected.

The first run exposed the C120 routing gap. I added the same before/after
snapshots around C120's document pointer and key handling, and changed runner
predicates to compare the inserted character at the recorded caret instead of
assuming caret zero. Hosted tests exercise both direct text-area focus and the
C120-style control-host route inserting into existing multiline text at a
nonzero caret.

Final hosted results:

| Suite | Cases | Result |
|---|---:|---|
| C188R9 registry | 14 | PASS |
| C188R10 reentry | 14 | PASS |
| C188R11 provenance | 15 | PASS |
| C188R11 runner predicates | 30 | PASS |
| C188R11 output bounds | 6 | PASS |
| Runtime lifecycle | 7 | PASS |
| QAC ownership predicates | 15 | PASS |
| QAC stream capture | 2 | PASS |
| Keyboard contracts | 24 | PASS |
| Keyboard runner predicates | 18 | PASS |
| **C188 hosted total** | **145** | **PASS** |
| Managed UI regression aggregate | 932 across 38 suites | PASS |

The C175 Notes resize suite passed 24 cases, including direct pointer focus and
character insertion, plus a control-host pointer focus and insertion at caret 4
in `one\ntwo`, producing `one\natwo` and advancing the caret to 5. These hosted
results validate the managed editing/control contracts; they are not guest
input evidence.

## Build and candidate integrity

Both final NativeAOT profiles passed. Their runtime-pack manifests report
`persistentCompositeLifecycle=true`, a 65,536-byte `Primary64KiB` managed heap,
and the existing allocation strategy. The production profile remains
proof-disabled.

| Artifact | Result | SHA-256 |
|---|---|---|
| Final diagnostic NativeAOT ELF | PASS | `751F6A1934CD8FF5F690ABE1692BC3130CB2B05BBC1213888BF6C2EEB8ED34BE` |
| Final proof-disabled production NativeAOT ELF | PASS | `7371BC8A1C4DB3552501E60F076EDBB5AA258BC17EDC323CD2713E04E4AD5838` |
| Reused diagnostic kernel | PASS | `0032BD297D86314D86B182C8CFDC7A15E3C12FE07BDEE5F192A2BFFD78403B93` |
| Final staged candidate ramdisk (candidate 003) | PASS | `45A64BC1719AD2EA1C6B634D87F157C475EE187ED155FBE42BCCD8862019A507` |
| Staged diagnostic application (candidate 003) | PASS | `751F6A1934CD8FF5F690ABE1692BC3130CB2B05BBC1213888BF6C2EEB8ED34BE` |

Candidate 003 was assembled from a copy of the accepted R4 ESP. Its staging
manifest reports 185 unrelated files preserved and zero other files changed.
It is prepared for follow-up but was not booted. The one guest run used candidate
001, whose staged managed ELF SHA-256 was
`C26BC5EDF0D745766AAD3BC7F9367F4CB127E39CBEB679FF53664F860BC866F7`.

## QAC run and managed state

The initial read-only preflight found a foreign QEMU (PID 14632), so I did not
launch. It exited naturally; a fresh status/list and local census then showed
zero QEMU processes and zero reservations. Exactly one QAC acceptance boot was
started afterward:

| Evidence | Observation |
|---|---|
| Run identity | `4d66da41fb3c46bea37fa1ad0f47a165` |
| QAC reservation | `03a2d3f52e844d12ad75cabe1401b20f` |
| QAC owner / QEMU child | PID 32036 / PID 18736 |
| During-run input ownership gate | Accepted before the click |
| Managed descriptors | 5, selectors 4–8 |
| Notes residency | Selector 4, resident mask 1 |
| `TryFind(4)` | Success (`0`) |
| Pointer event | Native local `(24,93)` PASS; C120 document focus PASS |
| Keyboard `a` transaction | Not sent (`inputSent=false`) |
| Owned QEMU shutdown | Exit code 0; process exited |
| Reservation after run | `Released`, owner verified, `degraded=true` |

An unmanaged foreign QEMU (PID 13196) appeared during the managed run. QAC logged
that exclusive conditions were compromised and marked the acceptance
reservation degraded. The first post-run census therefore made the runner's
strict `qacReleaseVerified` predicate false even though QAC logged the owned
QEMU exit and reservation release. A later read-only status/list, reservation
inspection, and local process census showed the reservation still `Released`
and no remaining QEMU process. The foreign process was not terminated.

The serial trace shows the persistent heap still active at pointer dispatch:
five descriptors, Notes resident, `TryFind(4)=0`, stable registry/pool backing,
`gc=0`, and managed return result 0. This qualifies the pointer callback and
preserved runtime state through that event only. It does not qualify a keyboard
callback or text edit.

## Screenshots and text-entry evidence

The first `initial-notes.png` capture occurred before the desktop had redrawn;
it is black apart from the taskbar/time. The later `editor-focused.png` shows the
desktop and Managed Notes after the click, with the pointer over the document
area. It does not show a verified caret because the pointer overlaps that area.

| Screenshot | SHA-256 | Observation |
|---|---|---|
| [Initial capture](../../out/dotnet/c188r11-managed-state-provenance/runs/notes-text-entry-001/guest-001/initial-notes.png) | `623BC0152B36BE3E064278E6FE6D8937D9C97A824F021313063DBDD156FA8CC2` | Captured before desktop redraw |
| [After click](../../out/dotnet/c188r11-managed-state-provenance/runs/notes-text-entry-001/guest-001/editor-focused.png) | `48A97D7E7849AF5E3B5629E37F92750DD5297CF0D611CA754A82F3509B19ACD5` | Notes rendered; pointer overlays document area |
| Before/after input | Not created | No key was sent |

There is no before-key or after-key buffer snapshot, no caret delta, and no
after-input screenshot. No visible `a` is demonstrated. The screenshot evidence
therefore does not distinguish an inserted glyph from caret or pointer redraw;
visible editing remains unqualified.

## Outcome and next phase

**Outcome D — QAC_OR_EXECUTION_BLOCKED.** The managed app and text-area focus
route were reached, but the runner failed closed because its initial diagnostic
predicate did not recognize the active C120 route. The key was skipped. QAC
also recorded a degraded reservation after an unmanaged process appeared.
Although the owned guest exited and the reservation was released, this run did
not qualify Notes text insertion or rendering. Outcomes A–C are not claimed.

The final source and candidate 003 now emit and evaluate the C120 document
pointer/key states, preserve arbitrary existing text and caret positions, and
separate insertion from redraw. The next bounded phase should first confirm a
fresh exclusive QAC census, then use candidate 003 for one authorized guest
run: establish the C120 text-area state, inject exactly one `a`, and require the
buffer/caret delta plus a settled screenshot showing the glyph after the pointer
has moved off the text. After that, proceed to the broader five-resident app
acceptance.

The source and report are intended for a local closeout commit only. No push was
attempted. The commit hash is reported in the task closeout.
