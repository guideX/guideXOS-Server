# guideXOS Server .NET Support — C187 External Managed App Guest Execution

## Objective and result

C187 qualifies a development-only NativeAOT composition that starts the
independently compiled C185 CounterApp inside a booted guideXOS guest. The
composition uses the public `GuideXos.ManagedUi.Contracts` API and the existing
C185 host adapters, C181 generation checked lifetime pool, compositor, drawing
surface, C174 resize transaction, and C180 default-deny resize path. It does not
load managed DLLs dynamically or change the standard production image.

The sample has a static `CounterApplicationFactory` in the NativeAOT host
image. The application and contracts remain separate project references; the
CounterApp source is not included in HostLogProof. The resulting ELF contains
the factory, constructor, render, input/action, resize, and teardown paths after
trimming.

**Final outcome: A — accepted.** The independently compiled CounterApp ran in a
booted guideXOS guest through its static NativeAOT factory, displayed a real
compositor window, received pointer and keyboard input, updated and redrew its
counter, resized with accepted and rejected geometry, and released its surface
and C181 slot on close. Three focused QAC boots covered launch/interaction,
resize/lifecycle, and coexistence. A supplemental QAC boot exercised a stale
input callback through the real NativeAOT entry after C181 cleared the slot and
observed its rejection before surface destruction.

## Preflight and protected state

| Item | C187 preflight |
|---|---|
| Repository / branch | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` / `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `00d967e69b45afbe11d6b2293ec2b3c2a229e086` |
| Repository `.phase` | absent |
| QAC `.phase` | `QAC1`, `QAC2`, and `QAC3A` accepted; `QAC3` open; next `QAC3C` |
| Protected `ESP\ramdisk.img` | verified SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Branch / worktree operations | no branch switch, worktree, stash, reset, clean, rebase, or history rewrite |
| Disk space | all candidate outputs fit under `out/dotnet/c187-external-managed-app-guest/`; the final D: check reported 65,266,946,048 bytes free |

The C185, C186, and C186R1 reports and the ManagedUiContracts and CounterApp
projects were reviewed. C181's existing five-slot managed residency and
generation checks remain in use. Native selector assignments 4–8 and the
standard production application composition remain unchanged. The candidate
replaces selector 4 (Notes) only in the isolated C187 image; selectors 5–8
continue through the existing static registry. Consequently, native snapshots
show the sample under the Notes selector identity even though its visible title
is `Managed Counter` and its public managed type identity is
`0xC1850001`. The candidate does not add a sixth residency slot.

The preflight inventory is retained at
`out/dotnet/c187-external-managed-app-guest/preflight/protected-before.json`.
That inventory records all pre-existing untracked paths and the protected ESP
hash. A final content comparison against the accepted C186R1 protected-after
inventories is retained at
`out/dotnet/c187-external-managed-app-guest/preflight/protected-current-comparison.json`:
all 13 inventoried roots passed with no added, removed, or changed files. It
covers C181, C182, C185, and C186 evidence and the pre-existing C185/C182 sample
build trees. C183, C184, and the C186R1 evidence directory were not part of
those 13 historical file inventories; C187 commands wrote only under the new
C187 output root. The modified ESP ramdisk remains unstaged and uncommitted.
The repository `.phase` was absent. At preflight, QAC showed QAC1, QAC2, and
QAC3A accepted, QAC3 open, and QAC3C next. Existing phase/stale-prompt guards
were left in place; guest runs used QAC's admission and release protocol rather
than altering phase state or bypassing a reservation.

## Launch architecture and Gate A

Gate A passed. The existing NativeAOT host startup and static `ManagedMain`
entry are sufficient; no loader, kernel ABI, app manager, or dispatch redesign
was needed. The C187 selector path is compiled only when
`HOSTLOGPROOF_C187_GUEST_CANDIDATE` is enabled:

1. The guest loads the isolated RAM-disk `GXOSAPP.ELF` through the existing
   NativeAOT host entry.
2. `ManagedMain` reads the existing selector from the NativeAOT context and
   sends selector 4 through `DispatchC187GuestCandidate`.
3. The candidate branch statically invokes the C185 composition and its
   `CounterApplicationFactory` from the independently compiled assembly.
4. The C185 runtime allocates the current application generation in the
   existing C181 five-slot lifetime pool. It constructs the bridge with the
   public factory's stable managed type ID `0xC1850001`.
5. `GuideXosC185HostServicesAdapter` supplies the public managed window,
   surface, label, and button interfaces over the existing native host calls.
6. The native surface registers with the compositor. The existing pointer,
   key, redraw, and C174/C180 resize paths dispatch to the current selector and
   generation.

CounterApp is selected at the ordinary selector-4 launch route. No managed
focus state is synthesized: C185 programmatic activation still reports
`Unsupported`; focus changes use existing pointer and taskbar input.

The first attempt used the preserved C181 kernel. Closing was vetoed because
the native selector-4 route still applied Notes' C152 dirty-document action to
CounterApp. C187 now builds a separate candidate kernel with
`GXOS_NATIVEAOT_C187_GUEST_CANDIDATE`. For selector 4 in that candidate image,
the native close request skips the Notes-only decision and then runs the
existing C150 lifetime-close action from the ordinary `onWindowClose` path.
The kernel source is guarded by that compile-time symbol; normal production
builds keep the original C152 close behavior. The guard is narrow and does not
change the native ABI, selector assignments, slot count, or app loader.

The isolated kernel build uses the recorded C181 feature set, the actual
freestanding amd64 target, `-std=c++14`, `-ffreestanding`, `-fno-exceptions`,
`-fno-rtti`, `-nostdlib`, `-nostdinc++`, and `-fno-builtin`. C181 proof markers
and the C187 close-route marker are validated in the actual ELF.

## Independent build and NativeAOT evidence

The host project references `GuideXos.ManagedUi.Contracts.csproj` and
`GuideXos.C185.CounterApp.csproj` as projects. C187 builds HostLogProof and its
project references with isolated intermediates and artifacts. The ramdisk
stager replaces `/APPS/GXOSAPP.ELF` in a copy of the C181 proof RAM disk. It
verifies the guideXOS FAT media descriptor, mirrored FAT updates, staged ELF
round trip, and every unrelated file before writing each guest attempt.

The exact-source C187 NativeAOT build is in
`out/dotnet/c187-external-managed-app-guest/attempt-17/nativeaot/`. The
qualification report is
`out/dotnet/c187-external-managed-app-guest/attempt-17/nativeaot/qualification/nativeaot-qualification.json`.
It validates an ELF64 x86-64 application with seven load segments and zero
relocations, and verifies retained factory, constructor, input/action, resize,
teardown, C187 route, and entry-point symbols. Candidate ELF SHA-256:

`B52A154076881EF732046BBB3354AF1CC6F4140805112D8118D4BD1DC2C0B3E2`

The isolated C187 kernel is built at
`out/dotnet/c187-external-managed-app-guest/attempt-20/kernel-candidate/`.
Its `kernel-qualification.json` records the ELF64 x86-64 header and proof
markers. Kernel SHA-256:

`436315FEB9C1AA632B544AF22A37B260DD36B220F76CAC354D81DD2961A53143`

After the focused boots, the candidate-only kernel close callback gained one
bounded stale-generation probe. Following the ordinary C150 lifetime-close
action, it calls the managed input entry with that just-cleared generation and
requires the existing C181 lookup to reject it before destroying the surface.
This adds no host ABI or kernel allocator behavior. The actual freestanding
build and ELF validation are in
`out/dotnet/c187-external-managed-app-guest/attempt-21/kernel-candidate/`;
the final candidate kernel SHA-256 is
`A290F5438B8D6DEB42F6A3F9458ED9734B9BE9D7232B32A25C2EEE632884B793`.
The supplemental guest uses this final kernel. The three focused boots below
used the same candidate image before this diagnostic-only addition.

The candidate ESP uses the preserved C181 loader, the isolated C187 kernel,
and an isolated RAM disk. BootX64 SHA-256 is
`D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`. The
candidate C187 RAM-disk SHA-256 is
`6E8F8BCB1D87412320A142400AE5A650EFAE7C73AFD579319D2F69461023769B`; each
staging manifest records 187 unrelated files preserved and zero unrelated
file changes.

All candidate kernel, managed intermediate, NativeAOT, linker, ELF, RAM-disk,
ESP, serial, QMP, screenshot, and acceptance-manifest outputs stay under
`out/dotnet/c187-external-managed-app-guest/` in unique attempt directories.
No ISO, ESP, RAM disk, or production media is committed.

## Hosted qualification

The corrected aggregate regression wrapper passed with exit code 0:

| Qualification | Result | Evidence |
|---|---:|---|
| Managed UI aggregate | 38 suites / 930 cases | `attempt-15/aggregate/aggregate-wrapper.log` and `aggregate/evidence/managed-ui-host-manifest.json` |
| C182 starter | 15 checks; 100 resize transactions; 0 resize allocation bytes | `attempt-13/c182/qualification.log` |
| C185 host boundary | 28 checks | `attempt-14/hosted/qualification.log` |
| C186 lifecycle | 15 checks | `attempt-14/hosted/qualification.log` |
| C187 guest candidate | 22 checks; 100 resize stress cycles; 0 resize allocation bytes | `attempt-14/hosted/qualification.log` |

C187 hosted checks cover the candidate selector and external project
references, static factory registration and invocation, host-service
acquisition, pointer and keyboard routing, counter state, redraw, accepted
and rejected resize transactions, identity stability, window/surface lifetime,
slot reclamation and reuse, stale-generation rejection, failed initialization,
and deterministic repeated operation. Resize stress reports zero hot-path
allocations and zero cumulative geometry drift.

These hosted checks supplement the guest evidence; they do not substitute for
it.

## Focused guest qualification

Every QEMU invocation uses QAC's `guideXOSServer` acceptance reservation. The
runner waits for QAC admission before it lets QEMU start, refuses foreign QEMU
ownership, captures the reservation identity and child PID, and requires QAC
to confirm process exit and reservation release. The first C187 run (attempt
18) was safely stopped when the runner could not read a locked serial log; no
unrelated QEMU was terminated. The corrected runner acquired and released
reservation `fe0b0dd7cfa742caba55a587da06b069` for attempt 19 (QEMU exit code
0). That run proved boot, CounterApp rendering, pointer/keyboard input,
focus transitions, and resize behavior, then exposed the Notes close guard.

The retained attempt-20 screenshot shows the actual guest CounterApp window
after pointer, keyboard, and focus-return interactions (`Activations: 5`):

![CounterApp running in the guideXOS guest](../../out/dotnet/c187-external-managed-app-guest/attempt-20/guest/boot-01/boot1-counter-5.png)

### Boot 1 — launch and interaction

**Status: PASS.** Candidate media is in
`attempt-20/guest/boot-01/`. It uses kernel SHA
`436315FEB9C1AA632B544AF22A37B260DD36B220F76CAC354D81DD2961A53143`, RAM disk
SHA `6E8F8BCB1D87412320A142400AE5A650EFAE7C73AFD579319D2F69461023769B`, and
BootX64 SHA `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`.
QAC granted reservation `955f2372648a4d64acaca15f84adeea9` after a 247.8-second
queue wait. QEMU PID `18148` exited with code 0, and QAC confirmed reservation
release with no remaining QEMU process or reservation. The acceptance manifest
records boot-ready, two static factory invocations, two successful CounterApp
windows, five input callbacks, six candidate close-route markers, and five
surface create/destroy markers. Serial SHA-256 is
`DE43E5556DA25A04F6E812A0E462D09A9EC82E6C99D06B89444F533882256027`.

The first CounterApp launch used native selector 4, generation 1, native
application/window identity `com.guidexos.apps.managed.notes` / `0x3E8`, and
public managed identity `(0xC1850001, generation 1, window ordinal 1)`. The
sample rendered `Activations: 0`; pointer, Enter, and Space input produced 4.
Calculator then launched as selector 6, generation 2, window `0x3E9`. Returning
to CounterApp via its taskbar tab retained 4; another pointer activation
produced 5. The user-driven close released generation 1 and destroyed window
`0x3E8`. Reopening through the existing Notes menu route constructed a fresh
CounterApp at generation 3, window `0x3EA`, with `Activations: 0`; it also
closed normally. The native Notes identity shown in snapshots is the temporary
selector alias described above; the visible title and managed factory identify
CounterApp.

Key screenshots are retained in `attempt-20/guest/boot-01/`:

- `boot1-start.png`: visible CounterApp window and initial value 0.
- `boot1-after-keyboard.png`: pointer, Enter, and Space produced value 4.
- `boot1-counter-5.png`: state survived focus-away and focus-return, then a
  pointer activation produced value 5.
- `boot1-reopened.png`: fresh generation with value 0.

The serial also records the C187 candidate close route, C150 lifetime close,
and C150 surface destruction for generations 1 and 3. Hosted stale-generation
tests are recorded above; no guest-side stale-callback injection hook exists.

### Boot 2 — resize and lifecycle

**Status: PASS.** Candidate files and evidence are in
`attempt-20/guest/boot-02/`. QAC reservation
`c709aa3959ca4cc9839c4a13074f2dd7` granted in 0.9 seconds; QEMU PID `16608`
exited with code 0 and QAC confirmed release. The manifest records one static
factory/window success for each launch, three input callbacks, six candidate
close-route markers, and four surface create/destroy markers. Serial SHA-256:
`4AF1EFD170423763F9A8C2FF60F45CAC83051102137B3326F87BDB7A88819727`.

The app began at 360×200 in generation 1/window `0x3E8`. The guest accepted
320×180 and then 720×480, preserving the same window and `Activations: 2`.
The Add one button remained hit-testable at maximum geometry and incremented
the counter to 3. Requests for 721×480 and 720×481 produced native and managed
`REJECT` results, retained 720×480, and kept the counter at 3. The visible
window after invalid requests is retained as
`boot2-reject-720x481.png` (SHA-256
`7D41F4966AEFF3676DDE894954DC72B27F3239952B4C0E8739323AB156DEAF2F`).

Closing invoked the C187 candidate route, C150 lifetime close, and surface
destruction for generation 1/window `0x3E8`. Reopening through the existing
selector route created generation 2/window `0x3E9` with a fresh value of 0;
the new instance also closed normally. The current C187 hosted suite rejects
callbacks for the closed generation and verifies slot reuse; there is no
guest-side stale-callback injection hook.

![CounterApp at 720×480 with state preserved](../../out/dotnet/c187-external-managed-app-guest/attempt-20/guest/boot-02/boot2-resize-max-720x480.png)

### Boot 3 — coexistence

**Status: PASS.** Candidate media and evidence are in
`attempt-20/guest/boot-03/`. QAC reservation
`a9a9ca24e49041c290b65f418e50e06a` admitted QEMU PID `20948` after a 1.0-second
wait; QEMU exited 0 and QAC confirmed release. The manifest reports boot-ready,
one CounterApp factory/window launch, two CounterApp input callbacks, successful
close, five surface lifecycle markers, and serial SHA-256
`0826D9785BAF2B26D602AA6C6BEA8E75235DB8F7A06A008759457F844836C764`.

The guest ran CounterApp selector 4/generation 1/window `0x3E8`, Calculator
selector 6/generation 2/window `0x3E9`, Task Manager selector 7/generation
3/window `0x3EA`, and File Explorer selector 8/generation 4/window `0x3EB`
concurrently. Four of five managed slots were occupied; there was no sixth
resident. Notes is replaced by CounterApp in this isolated image. The Task
Manager screenshot was captured before File Explorer launched and therefore
lists three apps; the later guest screenshot and serial launch/surface records
show File Explorer as the fourth resident.

Taskbar input returned focus to CounterApp. Its counter remained at 1 after the
focus transition and accepted maximum-size resize to 720×480. The button
remained hit-testable at the new geometry and incremented the visible counter
to 2. Window identity `0x3E8` and generation 1 remained stable through resize.
Closing CounterApp ran the C187 route, C150 lifetime cleanup, and surface
destruction for generation 1/window `0x3E8`; the captured post-close screen
still shows Task Manager and File Explorer. A scripted post-close Start-menu
reopen attempt did not produce a new factory invocation, so Boot 3 makes no
reopen claim. Boots 1 and 2 demonstrate successful reopen and fresh generation
ownership.

![CounterApp receives input after coexistence and maximum resize](../../out/dotnet/c187-external-managed-app-guest/attempt-20/guest/boot-03/boot3-counter-resized-hit.png)

Retained Boot 3 screenshots include `boot3-with-explorer.png`,
`boot3-counter-resized-hit.png`, and `boot3-after-counter-close.png` under the
boot evidence directory. Their SHA-256 values are respectively
`D9163DEB1254918A9E16E240F86B94F9F6BCED2CB9F1FAE58551446635566CCF`,
`8D7E5413CE96C3689D48ED58AB786BBEA20C014B3E1BD02D5EF535FC2D2DDF4E`, and
`F3571DCA10D70B7B73036849E66271D396A00564F8FB7F335F4BB24841997A09`.

### Supplemental guest check — stale generation

**Status: PASS.** The final candidate kernel and unchanged managed ELF were
booted under QAC at `attempt-22/guest/stale-generation/`. Reservation
`a1fc20800a7c43fe94db6bb736364f3f` admitted QEMU PID `3080`; QEMU exited 0 and
release was verified. The guest displayed CounterApp at generation 1/window
`0x3E8`; a real pointer activation changed the label from `Activations: 0` to
`Activations: 1`. On ordinary window close, C150 cleared the managed instance,
then the candidate issued an input callback carrying generation 1. The existing
NativeAOT managed dispatch rejected it, logging
`C187-STALE-GENERATION ... callback=input result=REJECTED proof=PASS`, before
C150 destroyed the surface. The acceptance manifest records one factory,
window, input, close, and stale-rejection result; serial SHA-256 is
`4C28D04034FC0158007DF99373AA457687DF1854C5280AFE345B85451D5F3107`.
The pre-close screenshot SHA-256 is
`7D540D2618AC75503E614C04E9BA5D730A559601A5AF280B986D78F9AA479793`; the
isolated media manifest SHA-256 is
`DA42CC35DB273EE20B296FA617F1BFC56192598453B28C7A091E52D8A9A1CAD6`.

The prior probe-only attempt 21 exercised the stale rejection but did not send
a user input before close, so its acceptance manifest is `FAIL`; QAC still
released the reservation and reported zero remaining processes and leases.
Attempt 22 repeated the close with a pointer activation and passed the runner's
full launch/input/close/stale-rejection gate.

![Guest CounterApp state before stale-generation teardown](../../out/dotnet/c187-external-managed-app-guest/attempt-22/guest/stale-generation/c187-stale-generation-activations-1.png)

## Production compatibility

C187 registration and the selector dispatcher are compile-time gated to the
isolated NativeAOT host composition. The standard production selector meanings
4–8 remain Notes, Settings Center, Calculator, Task Manager, and File Explorer.
The two shared native source paths are guarded by
`GXOS_NATIVEAOT_C187_GUEST_CANDIDATE`: selector-4 close routing around the
Notes-only C152 prompt, and the one stale-input probe after C150 clears
CounterApp's generation. Without that define, the standard C152 close behavior
is compiled unchanged and the probe is absent. The host ABI remains v4 / 128
bytes, callback offsets 104, 112, and 120 remain unchanged, and managed
residency remains five slots. The kernel change uses existing bounded native
state and introduces no STL, exceptions, RTTI, dynamic loader, or ABI field.
The C187 kernel, NativeAOT host, and media are isolated qualification products
and are not installed or packaged as the production image. No standard
production application registration changed, so a full production boot lane
was not triggered. This work does not claim a production CounterApp
installation, dynamic loading, a production SDK installer, or Server v0.3
integration.

## Recommended C188 direction

Give a development composition a native-owned application identity and an
ordinary host launch descriptor that does not alias a production selector.
Keep launch registration static and bounded. Define the native metadata and
close policy for that identity before evaluating installation, packaging, or
runtime discovery. C187 establishes that the existing public service boundary
can execute an external managed assembly in a real guest without a dynamic
loader.
