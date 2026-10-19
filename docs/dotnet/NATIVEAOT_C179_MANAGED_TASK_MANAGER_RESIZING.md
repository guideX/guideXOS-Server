# NativeAOT C179 — Managed Task Manager Resizing

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `6c3b3c5965ccc41104cf2253cab8b0ff19ec7f15`
**Outcome:** A — Accepted. Focused host and guest qualification and all three
proof-disabled production boots passed.

## Preflight and accepted baseline

The repository, branch, and starting HEAD matched the C179 request. The only
tracked pre-existing modification was `ESP/ramdisk.img`; its SHA-256 remains
`AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The
pre-existing untracked `samples/host/ManagedUiRegression/obj/` directory and
historical evidence were preserved. No `.phase` file was present. Existing
stale-prompt protection checks the source HEAD and evidence hashes. The
accepted C174–C178 reports were reviewed; C178's **32 suites / 846 cases**
are retained in the consolidated host run.

QEMU ownership was checked by PID and command line. Runs used isolated
workspaces under `out/dotnet/c179-managed-task-manager-resize/`. Foreign QEMU
processes were left running and untouched.

The protected ramdisk hash was rechecked before and after qualification and
remained `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
The pre-existing `obj/` directory and failed-attempt evidence remain present.
Completed evidence directories were NTFS-compressed in place to control disk
use; sampled SHA-256 values were unchanged.

## Architecture audit

Task Manager is already the managed C# NativeAOT application
`com.guidexos.apps.managed.taskmanager`, selector 7, implemented by the C161
application and controller. It is dispatched by the existing managed action
dispatcher. The initial outer size is **800×370**. The app creates its own
surface, takes an application snapshot, lays out its existing controls, and
renders through the shared managed controls framework.

The application shows a bounded application list and selected-item details.
The current displayed snapshot and row strings are owned by the Task Manager
controller, with capacity for 20 snapshot records. System state can change
between snapshots; refresh occurs only through the existing Refresh button or
Ctrl+R. There is no background polling or resize-triggered refresh. The UI
thread applies refresh and resize synchronously through the existing action
dispatcher, so those mutations do not interleave and require no new locking.

The existing selected row is identified by application identity, not only by
its row number. Refresh preserves that identity if it remains in the new
snapshot. If it disappears, the existing policy clears selection and details
rather than attaching selection to a replacement row. An initial non-empty
snapshot selects its first row. The list viewport clamps its offset when the
row count or visible-row count changes.

The list, details, status, and buttons are the only content. Resizing increases
the list viewport from 10 to 16 visible rows and widens its rendered text from
45 to 48 characters; the details and status areas move with the existing
layout. No new views, metrics, charts, or process operations were added.

## C174 resize route and bounds

The kernel's existing compositor resize request reaches the existing
NativeAOT resize callback and selector-7 managed action. Task Manager decodes
the existing C172 geometry action, constructs the shared C174 transaction,
recalculates its own control layout, renders, validates state, and reports the
transaction result. Rejected geometry restores the prior app layout. The
existing callback gained a selector/title check for Task Manager and routes
the same tagged action. Proof-only serial markers identify its focused resize
results. No compositor ABI, callback offsets, C172 routing, geometry encoding,
or 24-pixel titlebar convention changed.

The chosen inclusive **outer bounds are 800×344 through 960×452**. The minimum
width preserves a 45-character list and leaves the existing details column
within the client width. At minimum height, the list has 10 rows and the
existing button row ends with 16 pixels of bottom margin. At maximum size the
list has 16 rows, 48 rendered characters, and the details column begins at
x=424. C174 supplies the 24-pixel titlebar subtraction, candidate validation,
transaction acceptance, rejection mapping, and rollback decision.

## State, rendering, and interaction invariants

Each accepted resize recalculates positions and clipping from the absolute
candidate geometry. It does not read a new system snapshot. The controller
retains its snapshot generation, selected application identity, active
control, and dialog/capture state. The scroll offset is clamped and adjusted
only as needed to keep the selected row visible. Fixed-capacity controls and
snapshot records remain app-owned value data, so rendering cannot retain a
reference into a replaced refresh buffer.

Refresh creates and validates the next bounded snapshot before applying it to
the controller. Resize reads only the currently owned snapshot and lays out
its rows. Because both paths are synchronous on the same existing dispatch
thread, there is no concurrent mutation window. A failed resize restores the
old geometry and viewport before rendering. Existing Task Manager close
confirmation and application-close semantics are unchanged.

## Host regressions and stress

The consolidated managed UI run passed **34 suites / 871 cases**. This
preserves the C178 baseline of 32 suites / 846 cases and adds C179's 20 focused
cases plus five stress checks. C172–C178 and the Settings C167/C168 lanes were
rerun.

| Measure | Result |
| --- | ---: |
| Layout updates | 1,000 |
| Alternating resize operations | 500 |
| Refresh/resize combinations | 250 |
| Rejected geometry checks | 250 |
| Application lifecycle cycles | 100 |
| Resize hot-path allocations | 0 bytes |
| Refresh allocation measurement | 0 bytes |
| Cumulative geometry drift | 0 |

The C179 tests cover initial/minimum/maximum geometry, one-pixel boundary
rejection, transaction rollback, empty/single/full-capacity snapshots,
selection retention and invalidation, refresh generation changes, scrolling
and clamping, focus, and lifecycle. Test snapshot capacity is 20 records; the
actual guest's live snapshot is smaller and is reported separately. No timing
threshold was defined; stress measurements report operation counts and
allocation bytes separately for resize and refresh.

The accepted C178 baseline remains 32 suites / 846 cases. C179 adds two suites
and 25 cases, producing 34 suites / 871 cases. The fresh C166R9 prerequisite
also passed 604 native cases, 34 managed cases, 31 dual-slot persistence
cases, and 1,000 persistence stress transactions. NativeAOT ABI v4 / 128
bytes and callback offsets 104 (snapshot), 112 (close), and 120 (association)
were retained.

Host regression artifacts:

- Manifest: `out/dotnet/c179-managed-task-manager-resize/host/managed-ui-host-manifest.json`, SHA-256 `F2C585A7F119B2EB9AED4A297C7C6B6D3A32D5CA8D94320F5D08C6B2CE9D31E5`
- Suite log: `out/dotnet/c179-managed-task-manager-resize/host-regression-run/managed-ui-host-suites.txt`, SHA-256 `1C46FE2F5D9F83F006EB24842371DF614EF2AC00024F0F9A55F01C83C6709FA1`

## Focused QEMU qualification

All three isolated focused guest boots passed using the existing compositor,
NativeAOT callback, action dispatcher, and C174 transaction path. The manifest
records geometry transitions, window generations, QMP input, serial logs,
screenshots, and SHA-256 values.

| Boot | Scenario | Result |
| --- | --- | --- |
| 1 | Initial 800×370, maximum 960×452, minimum 800×344, reject 799×344 with rollback, close/reopen | PASS; identity 00000006 / 000003ED remained stable through resize |
| 2 | Refresh button and Ctrl+R, selected item `1:1`, resize after refresh, focus away and return | PASS; identity 00000009 / 000003F1 remained stable |
| 3 | Task Manager resize while Explorer and Notes remained resident | PASS; identity 0000000C / 000003F3 remained stable |

The focused proof observed button and keyboard refresh, pointer selection,
keyboard navigation, and focus return. It directly checked the selected
identity and geometry after refresh. The guest list contained two or three
live items, so maximum-capacity and full 20-row scrolling remain host-tested
behaviors rather than guest observations.

## Proof-disabled production acceptance

The clean proof-disabled candidate booted from isolated media in three
ordinary boots. The general C166A2, C170, C172, and C173 manifests and the
C179 manifest all report PASS. The final C179 manifest was assembled from the
completed boot records after correcting two test-harness assumptions: closed
Task Manager history is ignored when checking for a currently resident
surface, and production Notes state checks use the captured UI when focused
diagnostics are absent. A later Notes-to-Settings click uses the measured
post-resize Notes origin. These changes affect the acceptance runner only;
the focused-proof application and kernel source hashes still match.

| Boot | Scenario | Result |
| --- | --- | --- |
| 1 | Task Manager initial/maximum/minimum, rejected width rollback, stable identity, close/reopen | PASS; 800×370 → 960×452 → 800×344; 799×344 was rejected |
| 2 | Refresh and selection-preserving resize, focus return | PASS; explicit refresh retained selected identity and geometry |
| 3 | Task Manager with resident Explorer and Notes; resize both residents; broader Settings/Explorer/Notes/Calculator smoke | PASS; Task Manager identity remained stable and existing state checks passed |

The ordinary candidate strings and serial logs contain no
`GUIDEXOS_PROOF_CONTROL`, proof scenario markers, `GXPF`, or C179 proof-only
geometry/launch markers. Ordinary production has no C179 internal layout
diagnostics, so production conclusions are based on visible screenshots,
measured window bounds, QMP input logs, and the existing application lifecycle
and refresh logs. The focused proof supplies the internal selection and
transaction assertions.

The focused manifest records runner SHA-256
`DFD9E3A6CCDB5B8828765579EF81379A6D3D20AA8D48320D0C734381B9CF91C7`; the
ordinary acceptance runner after its harness-only fixes is
`EBEF80A2739368B436CB6E13F62C785E41183CF03399CDABD3CAB406ED462061`. The
production manifest records both values and the scope of that change. Every
other application, kernel, ABI, and host-regression source listed by focused
proof matched its recorded hash during production manifest assembly.

## Evidence locations and hashes

Evidence root:
`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c179-managed-task-manager-resize`

| Artifact | SHA-256 |
| --- | --- |
| `c179-production-acceptance-manifest.json` | `1948FCA7E7FAF9202E3954DEACC62371F6AC5BB103307F5FEB76FA724D9DBA62` |
| `c179-focused-proof-manifest.json` | `F6BF92E47EA8CB02E8C2AD8EF24957E7124B57FA99E9894B310308698A6748AD` |
| `host/managed-ui-host-manifest.json` | `F2C585A7F119B2EB9AED4A297C7C6B6D3A32D5CA8D94320F5D08C6B2CE9D31E5` |
| `host-regression-run/managed-ui-host-suites.txt` | `1C46FE2F5D9F83F006EB24842371DF614EF2AC00024F0F9A55F01C83C6709FA1` |
| `production-acceptance/c166a2-manifest.json` | `8D813F848168426C28A597DFBA5127007DF5C7E66CF7678C2BA4027B812E0F15` |

The proof-disabled candidate is located at `clean-production-candidate/`:
composite `build/composite-c166-production/artifacts/HostLogProof.elf`,
`kernel.elf`, `ramdisk.img`, and `GXSETT.BIN`. Its hashes are composite
`3C634B1C83D6D36D0BB509121D22229F7AF1917CA5D5AAD578136B4F3A02A172`, kernel
`6811D7F3EA3057BE17327356F5D1603F8C8B79FFFD10B1D9073BA925CEBF1060`, and
64 MiB ramdisk
`0BCB08595228213EB7025B4C3AA5471B30D659E52B07B627433DA1F8AA3FB0FA`.
`GXSETT.BIN` remains v2 / 26 bytes, SHA-256
`864F8DF20EF04113394EBFCED0015F8FAA54701FE06419EE44604530BAADA529`.
The protected canonical ramdisk was not replaced.

The focused run is
`settings-ui-proof-runs/20261019-171130-b18c07d5/`; the proof-disabled run is
`production-acceptance/ordinary-boots/ordinary-run-20261019-184220-05a8b572/`.
Each manifest contains every referenced screenshot path and hash. Serial-log
and QMP-input hashes are:

| Run | Serial SHA-256 | QMP SHA-256 |
| --- | --- | --- |
| Focused boot 1 | `DB8E135CE745CCB97113EE382FE8D3B31B291D83DA462D42966A870209770DEE` | `1DAC4324EC62A6F4EEACD740C14ECF40C8B6CA112F2652518C301DF82D8DCC54` |
| Focused boot 2 | `E8DE4ED2300323367D8C1B14F953F1D7311B7A8A7EB71E153941E2E361CB62B3` | `03C77DFA29B3C75F9F8315C4D87D9A2528F2B71201ED5CC86D067BF23747104C` |
| Focused boot 3 | `55A23D26F696CCD4A02B0F8FBCA1C5CBE30C438E05BB014CDCFB03734F659D84` | `E853E34A1660787300647EEB220A342929552B3C43C2B9CCB08AEC6604611B5D` |
| Production boot 1 | `2C093EE1FCD57FB6FACD7373844427EE1C915AAA9AFB29999138F0EC062977C5` | `096073F4713F5461AAB0CD5B7BA8B67BE5C50044877A6A38DECAA065BF0316C4` |
| Production boot 2 | `F26BE27D6DAC3E316651D7C62E3B519A23D54776ADD7DAE6401EFA4DD58B1F45` | `86FFD5C6B63131E2F7479BEDB0F7DF1D20724F0D8AB8A33F8D8F92C46B49249C` |
| Production boot 3 | `A1469A049842436AD6796F7CF208A340BC358FA79E7E108E9AC4F89E8EA0294A` | `847E5D5D18BB7B6641B2A3D80CC874CF89BA06D1E4AA9EDB9AC69E97B2798E29` |

Three earlier production attempts stopped on harness-only checks before the
final run passed. Their logs/screenshots and the replaced incomplete summary
were preserved under `production-acceptance/ordinary-boots/` and
`production-acceptance/failed-attempt-manifests/`; none are cited as accepted
evidence.

## Limits and C180 handoff

The guest has three resident managed-app slots. The concurrent qualification
therefore uses Explorer, Notes, and Task Manager; no five-app pass is claimed.
The guest test data exposes two or three live Task Manager rows. The host
regressions cover empty, single-row, multiple-row, full 20-row, selection
invalidation, and scroll clamping cases. The existing list is fixed-capacity:
expansion increases visible rows from 10 to 16 and list text width from 45 to
48 characters.

C180 should first audit for another existing managed application whose
application-owned snapshot changes while resident, then reuse C174 and verify
its state through refresh and resize. Do not add polling or a second resize
path. C180 is not started as part of C179.
