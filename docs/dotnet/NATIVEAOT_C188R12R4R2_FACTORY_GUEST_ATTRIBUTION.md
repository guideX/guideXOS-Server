# NativeAOT C188R12R4R2 — Settings Factory Guest Attribution

**Outcome A — `FACTORY_ROOT_CAUSE_PROVEN`**

One QAC-managed run of the qualified R4R1 candidate produced a direct native
array allocation failure for Settings selector 5. The array request was 88
bytes while the current thread allocation context had 64 bytes available.
The R4 array wrapper recorded `bounded-failfast` and the source immediately
calls `guideXosFailFast(6)`, which reaches `__fastfail`; this prevents the
factory from returning or reaching the managed exception handler.

The preceding Settings object allocation followed a different path: its
136-byte fast request did not fit in the remaining 104 bytes of the 64 KiB
image-backed arena, but the NativeAOT object fallback returned the object
outside that arena. The array failure happened afterward in a different
thread allocation context. This proves a bounded array capacity failure, not
that the whole 64 KiB arena was exhausted at the time of the array request.
The constructor body never began. No arena or allocator correction was made
in this phase.

## Repository and preservation

- Branch: `v1.1_DOTNET_SUPPORT`.
- Starting HEAD: `115405fc97324a863f2499b21b7066d019d60c21`.
- Candidate verification and guest-evidence ending HEAD:
  `115405fc97324a863f2499b21b7066d019d60c21`; the repository HEAD did not
  change during those steps. The documentation and parser/test closeout are
  committed separately after the evidence snapshot.
- The candidate and guest used that unchanged repository HEAD. The diagnostic
  candidate itself records its qualified source commit and exact artifact
  provenance in its manifest.
- The pre-existing modified `ESP/ramdisk.img` and all unrelated untracked
  build/smoke outputs were preserved. The source ramdisk SHA-256 before and
  after is `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.
- No reset, restore, clean, stash, branch switch, history rewrite, source
  ramdisk modification, foreign-process termination, or push was performed.
- A post-guest parser correction was needed because managed factory markers
  use decimal IDs while native allocator markers use fixed-width hexadecimal
  IDs. The correction and its regression fixture change only evidence parsing;
  the candidate and guest runtime were not rebuilt or changed.

The first runner invocation created `runs/settings-factory-001` and stopped
before QAC admission because the initial empty serial capture was rejected by
PowerShell's mandatory parameter validation. Its partial evidence was
preserved. It launched no QAC process and no guest. The corrected runner was
used with the fresh `runs/settings-factory-002` directory. Exactly one QAC
guest boot and one Settings click occurred in total.

## Candidate identity and configuration

The existing candidate at
`out/dotnet/c188r12r4r1-diagnostic-link-repair/candidate-001/` was reused
without rebuilding. Hashes were checked directly, including extraction and
hashing of `/APPS/GXOSAPP.ELF` from the staged FAT32 ramdisk.

| Artifact | SHA-256 | Verification |
|---|---|---|
| Diagnostic NativeAOT ELF | `953E4489B9158317BD8D975C995FFE661B3431855075EE27C893838028487C10` | ELF64 x86-64; seven LOAD segments; zero relocations |
| Diagnostic kernel | `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864` | ELF64 x86-64 |
| Staged diagnostic ramdisk | `67315FC61B90E162587D3471AE84F0C0C527B19E325897E020A75C5CAE6DE7D4` | Embedded app hash matches the qualified ELF |
| Staged `BOOTX64.EFI` | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` | Candidate loader |
| Source `ESP/ramdisk.img` | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` | Preserved |

The candidate manifest records `persistentCompositeLifecycle=true`, a 65,536
byte managed arena, five production descriptors at selectors 4–8, and Settings
at selector 5. The diagnostic selector 9 remains development-only. The
proof-disabled production manifest retains selectors 4–8 and has R4 factory
instrumentation disabled. The diagnostic `__Internal` binding fix and linked
factory, lifetime, dispatch, and allocator markers are present; the candidate
manifest records no unsupported Windows loader imports.

See [candidate-qualification.json](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/candidate-001/candidate-qualification.json)
and [ramdisk-staging.json](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/candidate-001/ramdisk-staging.json).

## Diagnostic coverage

The managed factory trace in
[C188R12R4FactoryDiagnostics.cs](../../samples/managed/HostLogProof/GuideXos/C188R12R4FactoryDiagnostics.cs)
uses the `C188R12R4_FACTORY` family with invocation, selector, operation,
stage, code, and result. The observed stages distinguish factory wrapper
entry, the explicit Settings type-access bracket, the object `new` request,
base-constructor entry, constructor entry, model/control/dialog setup, object
construction result, factory return/failure, and downstream resident
registration. The type bracket proves that `EnsureFactoryTypeInitialized...`
returned; it cannot prove when NativeAOT first ran an eager module initializer.

The native `C188R12R4_ALLOC` family correlates records to selector 5 and the
active factory invocation. It logs array context/capacity failures before
fail-fast. For objects, it logs a fast-capacity miss and the result of the
stock `RhpNewFast` fallback. Successful allocations are counted but are not
individually printed; the aggregate cursor/count summary is emitted only when
the factory calls `EndFactory`. The failing factory never reached that point,
so no factory summary or final cursor was emitted. The current allocator
trace contains no EEType or array-length field, so it cannot prove which
managed field initializer issued the 88-byte array request.

Diagnostic output does not allocate through the managed allocator under
investigation. The C# factory logger formats into a 192-byte stack buffer and
uses the existing text/UART route; the native allocator emitter uses fixed
serial primitives. Test-only captured lines and `ToArray()` are excluded from
the NativeAOT build. The link-boundary preflight also passed its bounded logger
check.

The last factory operation before failure was `object-allocation-request`
(operation 2). There is no `base-constructor-enter` or
`constructor-enter` marker. This localizes the failure to allocations made
while evaluating `new ManagedSettingsCenter()` and its transitive instance
field initializers, before the constructor body. `model-init-enter`, control
and dialog stages, constructor return, object construction result, factory
return/failure, resident registration, lifetime return, and
`ManagedSettingsCenter.Launch` were not reached.

## Hosted preflight

The required focused checks passed before QAC execution. After the parser
correction, the directly affected suite was rerun and passed again.

| Qualification | Result |
|---|---|
| Settings factory cases | 14/14 pass |
| Factory marker predicates | 9/9 pass, including fixed-width native hexadecimal IDs |
| Diagnostic link/runner boundary cases | 9/9 pass |
| Settings selector-5 dispatch predicates | 12/12 pass |
| Lifetime attribution predicates | 13/13 pass |
| Settings handoff predicates | 37/37 pass |
| QAC ownership predicates | 15/15 pass |
| QAC/readiness runner predicates | 30/30 pass |
| `git diff --check` | Pass |

The retained managed UI qualification remains 40 suites / 969 cases. It was
not rerun because this phase changed no managed application or allocator
behavior. The corrected parser was also run offline against the preserved
complete guest serial log and now returns `EXPLICIT_ALLOCATION_FAILURE` with
three invocation-2 allocation events. The runner's original embedded factory
summary missed these records due to the decimal/hex ID formatting difference;
the original summary is preserved alongside the corrected offline analysis.
The post-correction hosted run is recorded in
[hosted-qualification.json](../../out/dotnet/c188r12r4r2/hosted/20261029-163752-047/hosted-qualification.json).

## QAC admission, ownership, and input

Before launch, QAC 2.0.0 reported Online, zero QEMU processes (zero foreign),
zero reservations, zero managed sessions, zero queued work, and zero blocked
work. An independent Windows process census found zero QEMU processes. The
runner then acquired acceptance reservation
`4c202499a9364c22b33f934f780c346d` and verified the owner/runner/QEMU child
identity chain. The QEMU child was PID 17108 at
`C:\Program Files\qemu\qemu-system-x86_64.exe`; every saved ownership check
accepted the reservation. The pre-click QAC inspect reported `Running` and
`degraded=false`, and the pre-click list showed exactly one managed QEMU and no
foreign process.

The runner confirmed managed runtime initialization, five registered
descriptors (`n=5`), and Notes resident at selector 4. Notes action `0x18`
returned PASS. Settings selector 5 lookup, runtime Settings initialization,
resident miss, capacity check, generation check, and factory resolution all
passed.

The pre-transition screenshot showed the source-defined Settings button in
Notes window `000003E8`. At framebuffer size 1280×800, the target and verified
pointer were `(815,550)`; the screenshot cursor probe found one match. The
runner sent exactly one left-button down/up transaction and zero key events.
The post-transition screenshot still shows the Notes image. Serial records the
Notes close and surface destruction before Settings dispatch; no Settings
surface marker was observed, so the screenshot is not treated as proof that a
live Notes window remained.

QAC stderr contains one transient warning that the reservation was degraded
because its QEMU process census was incomplete. The runner's saved QAC
ownership checkpoints, including the immediate pre-click checkpoint, all
report `degraded=false`, zero foreign QEMU processes, matching status/list
counts, and accepted owner/runner/child identity. The warning's underlying
transient trigger is not recorded, so it remains an admission-quality caveat.
At shutdown QAC verified the owned QEMU exit (exit code 0 after the runner's
controlled QMP quit), released the reservation, and returned to zero QEMU
processes, reservations, and sessions. The exit code is controlled shutdown
evidence, not evidence of a successful Settings launch.

## Ordered Settings trace and allocation evidence

Settings was invocation 2, selector 5, generation 2. The complete raw serial
trace is preserved at
[serial.log](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/runs/settings-factory-002/serial.log)
with SHA-256
`78E90A5E59C9832CCA966C0A96833CB30E69B0FA47F3D5BAE1C8076138619488`.

| Order | Marker / operation | Evidence |
|---:|---|---|
| 1 | Notes action `0x18` | Returned PASS |
| 2 | Settings dispatch | Selector 5; registration lookup PASS; runtime Settings initialization PASS |
| 3 | Lifetime preflight | Resident miss; capacity available; generation valid; factory resolved |
| 4 | Factory wrapper | `factory-invoke`, `type-init-enter`, `type-init-return`, `object-allocation-request` |
| 5 | Settings object allocation | `object`, `fast-capacity-miss`, size `0x88` (136), available `0x68` (104), result `RhpGcAlloc-fallback-entered` |
| 6 | Object fallback result | Same request returned `returned-outside-arena` |
| 7 | Array allocation | `array`, `fast-capacity-miss`, size `0x58` (88), available `0x40` (64), result `bounded-failfast` |

The first object request's cursor was `0x1002085A8`. The linked map places the
64 KiB image-backed arena at `0x1001F8610`, ending at `0x100208610`; therefore
104 bytes remained at that object request. This is 5,520 bytes less than the
prior phase's 5,624-byte pre-factory observation. The current value is a
direct request-time observation after the type-access bracket; it is not a
separately emitted `BeginFactory` snapshot. The diagnostic logger itself uses
stack/native serial output, but the evidence does not identify why the
pre-construction cursor differs from the earlier run.

The array failure cursor was `0x100D00F90`, outside the 64 KiB arena range,
with only 64 bytes in its current thread allocation context. Source in
[guidexos_nativeaot_platform.cpp](../../tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp)
shows that the array wrapper checks `objectSize > allocationLimit - allocationPointer`
and calls `guideXosFailFast(6)` on a miss before invoking the stock array
allocator. The fail-fast routine is `[[noreturn]]`, calls `__fastfail`, and
does not return to the managed factory. The object wrapper has a distinct
behavior: on its fast miss it calls the stock `RhpNewFast` fallback, which
returned an object outside the persistent arena in this run. The trace thus
proves the array-capacity miss/fail-fast path; it does not prove that a
stock array slow path or the entire general GC heap was exhausted.

The 88-byte request is compatible with an eight-reference array such as
`GuideXosGroupBox._members`, but there are other eight-entry arrays in the
Settings object graph. Because the native marker omits EEType and array
length, the exact field initializer remains unproven. No final registry,
resident-pool, or factory-summary snapshot exists: the array path failed
before registration and before `EndFactory`.

## Classification and next action

**`FACTORY_ROOT_CAUSE_PROVEN`.** A selector-5 array allocation request was
rejected by the bounded NativeAOT array fast-path capacity check, which then
invoked fail-fast before the base constructor or factory could return. This
distinguishes the actual mechanism from a generic factory stall, a failed
object allocation, or a proven exhaustion of the entire 64 KiB arena.

The smallest evidence-supported follow-up is a focused correction to the
array allocation miss policy: establish a safe refill/stock fallback or a
contained managed failure for an array request that exceeds the current
thread allocation context. Preserve the object/array path distinction and
keep the 64 KiB size unchanged until total demand is measured. Add native
predicates for a capacity miss after an object fallback, and qualify the
result in diagnostic and proof-disabled production builds before running the
multi-resident launch and lifetime regressions.

| Capacity question | Evidence and limit |
|---|---|
| Direct demand observed | One 88-byte array request failed with 64 bytes available; a preceding 136-byte object request had 104 bytes available and succeeded outside the arena. |
| Persistent allocation total | Not available; factory termination prevented the aggregate summary and constructor completion. |
| Other applications / simultaneous residents | Not measured by this single transition; five descriptors were present and Notes was the only resident before the switch. |
| Metadata and alignment | The reported sizes are NativeAOT EEType-derived object sizes with the runtime's array header/component sizing and 8-byte rounding; the trace does not expose the failing EEType or cumulative metadata cost. |
| Post-construction headroom | Unknown because construction did not complete. |
| Failure behavior | Array capacity miss calls fail-fast; object capacity miss can use a stock fallback outside the arena. |
| Image layout and build parity | The current image-backed arena is 64 KiB. A future size or allocator change must revalidate ELF LOAD/BSS layout and both diagnostic and production NativeAOT profiles. |

## Retained guest evidence

All outputs are preserved under
`out/dotnet/c188r12r4r1-diagnostic-link-repair/runs/settings-factory-002/`:

- [acceptance-manifest.json](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/runs/settings-factory-002/acceptance-manifest.json)
- [before-settings.png](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/runs/settings-factory-002/before-settings.png)
- [settings-check.png](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/runs/settings-factory-002/settings-check.png)
- [r12r1-interaction-plan.json](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/runs/settings-factory-002/r12r1-interaction-plan.json)
- [settings-pointer-probe-001-cursor.json](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/runs/settings-factory-002/settings-pointer-probe-001-cursor.json)
- [r12r2r1-settings-dispatch-evidence.json](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/runs/settings-factory-002/r12r2r1-settings-dispatch-evidence.json)
- [r12r4-native-hex-corrected-analysis.json](../../out/dotnet/c188r12r4r1-diagnostic-link-repair/runs/settings-factory-002/r12r4-native-hex-corrected-analysis.json)
- QAC before/during/pre-click/pre-shutdown/after status, list, inspect, ownership, and stderr logs, plus QMP input and shutdown logs
- Preserved partial preflight artifacts under `runs/settings-factory-001/`; no guest was started there

The source ramdisk remains unchanged at SHA-256
`AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.

Key retained evidence hashes:

| Evidence | SHA-256 |
|---|---|
| Candidate qualification manifest | `2D75552838E5A1C4D00FDBC74C997EDD9355CA8792E8286F34C636C5839031CE` |
| Candidate ramdisk staging manifest | `5672E84181C20216E5D75F71C151A5C09023F420176C3D448EB4A775C6FA5896` |
| Acceptance manifest | `69AE46AEFC37D40C7E0B62E4A7BEDF48B092CB5E4FF01623571D32538EE409BB` |
| Raw serial log | `78E90A5E59C9832CCA966C0A96833CB30E69B0FA47F3D5BAE1C8076138619488` |
| Pre-transition screenshot | `EB53206A37BF59B873600AE2A713946401B42EF4BF8372F9B27582B72249102C` |
| Post-transition screenshot | `32F7B8B277F25C375C40D62E0D314A7A2ADD2092722EE75912529F80678CE739` |
| Interaction plan | `26395E08D17C0C7FA6A240BB8A506DA8F3E25C8F0DB73931EE29C5608BD68FA4` |
| Verified pointer probe | `D66E99B3A7558D435B4F928B4D65FC85D6C6F73C4635E3703DCA0E53A9904926` |
| Corrected offline factory analysis | `AC94E82E964F35F801DF29486CA88DE2CE4ACE48D4A449211B421795C439DCE6` |
| Post-correction hosted qualification | `0A31AE051C70867C25111830AF505D4FCAD77D68A2D5947A0F33330E09E17872` |
| QAC during-run ownership | `6F2F0B28D195908DDDFD723455CF47869CC6994128FBD597570B40BD6B735ADF` |
| QAC release inspection | `CC425B62883F60743EC041F23DBA3E33DE1A30CF4F02424F64417EDA1E41C40C` |
| QAC stderr, including transient census warning | `E68D3D0EF111EB8BEE290FA1CDD0CB0F69F45FD1ADD50A489F23A745035C0674` |
| QMP input and shutdown log | `30C4A484D1041B5182A4B0EC59F7F35FFC7410DD9C7CA62DEB66FB2A13CD9BE8` |
