# C188 — External managed application native identity

## Result

C188 adds a development-only native application descriptor for the independently compiled C185 CounterApp. The new descriptor gives CounterApp a distinct application ID and selector while keeping the five production selector mappings intact. The feature is enabled only by `GXOS_NATIVEAOT_C188_EXTERNAL_APP_IDENTITY` in the isolated candidate kernel and NativeAOT host build.

Hosted qualification passed: C188 25 focused cases; C182 15; C185 28; C186 15; C187 22; and the accepted managed UI baseline of 38 suites / 930 cases. Both the candidate and proof-disabled production NativeAOT and freestanding kernel builds passed. The final candidate uses the C160 profile with C166 ordinary application startup, so Notes can launch without running the C155 proof suites on first launch. Guest acceptance remains incomplete; the C188R1 attempts and current blocker are recorded below.

## C187 limitation and Gate A

C187 called CounterApp through selector 4. Selector 4 remains Notes in the native application registry, its window ownership metadata and task snapshots. That route could execute CounterApp but did not provide a genuine native identity; renaming its window would not have fixed the selector or ownership collision.

Gate A passed with the existing compile-time descriptor table. `BuiltInAppMetadata` already carries the application ID, display and launch names, availability, launch kind, managed selector, and composite image path. The descriptor does not carry a factory pointer, so C188 keeps factory selection in the existing statically linked NativeAOT host. No new registration API, loader, process model, ABI field, or managed residency slot was added.

| Identity layer | C188 value or owner | What it controls |
| --- | --- | --- |
| Native descriptor | `com.guidexos.apps.managed.counterapp`, display and launch name `Managed CounterApp` | Registry lookup, menu entry, shell surface name, application snapshot display and ID |
| Native selector | 9, present only under the C188 development define | Selects the CounterApp static route; selectors 4–8 remain production routes |
| Launch target | Existing composite image `/system/apps/GXOSAPP.ELF` | Supplies the NativeAOT host image; the managed CounterApp assembly is statically composed into the isolated image |
| Managed factory | `GuideXosManagedHostServicesC185Composition` calling the external `CounterApplicationFactory` | Creates the independently compiled C185 application through `ManagedUiContracts` |
| Managed application identity | Stable ID `0xC1850001` plus the C181 generation | Identifies the managed object and rejects stale actions or input |
| C181 residency | One entry in the existing five-slot bounded lifetime manager | Owns the application instance and releases it on teardown |
| Compositor window | Descriptor application ID, selector 9, snapshot instance ID and current generation | Routes focus, pointer/keyboard input, resizing, snapshots and close to the correct owner |
| Taskbar and Task Manager | Native window ownership and the managed surface snapshot resolved through the descriptor table | Presents and focuses the application by its real native identity |

The host route accepts selectors 4–8 through the existing production composite dispatcher and sends selector 9 to the C185 composition. Invalid selectors are rejected. CounterApp's project references only `ManagedUiContracts`; the HostLogProof project references the separately compiled CounterApp and Contracts projects.

## Production isolation and ABI

The C188 descriptor, menu item, host route, close branch, and development markers are guarded by the candidate define. The proof-disabled production kernel was built without that define and contains Notes, Settings, Calculator, Task Manager, and File Explorer IDs. Its ELF excludes the CounterApp ID, display name, C188 registry/launcher/stale markers, and test controls. The proof-disabled production NativeAOT build also excludes the C188 route, CounterApp assembly symbols, and CounterApp identity strings.

Production selectors remain Notes 4, Settings 5, Calculator 6, Task Manager 7, and File Explorer 8. Notes continues through the existing C152 close approval path. CounterApp's selector-9 close uses C150's normal lifetime-close action and then probes the old generation; C181 rejects the stale callback. Candidate and production builds preserve NativeAOT ABI v4, its 128-byte call table, and callback offsets 104, 112, and 120.

## Lifecycle, focus, taskbar and resizing

The static C188 route is reached through the descriptor's managed selector. C181 allocates one ordinary slot and generation. The compositor associates the surface with the native descriptor ID and snapshot instance ID. Application snapshots resolve each managed surface through `FindManagedNativeAotAppByIdentity`, so the task manager receives the descriptor's display name and app ID rather than an identity inferred from the CounterApp title.

The host suite verifies the selector mapping, independent managed ID, launch, pointer and keyboard activation, close, stale-generation rejection, reopen with a new generation and counter reset, failed window initialization cleanup, 100 launch/close cycles, and five-slot residency stress. The established C181 suite additionally exercises 250 capacity-exhaustion and recovery cycles, 250 stale-generation rejections, 1,000 slot allocation/release operations, zero resize allocations, and zero geometry drift.

CounterApp uses the accepted C180/C174 resize dispatcher. Its outer size is bounded to 320×180 through 720×480. The C188 tests accept both limits, reject 721×480 and 720×481, retain the counter and identity, preserve rollback, and verify 500 valid resizes with zero hot-path allocations.

The Notes open-file dialog could not be constructed in the NativeAOT guest because each `GuideXosListBox` reserved a 127-character label buffer for every row as UTF-16 characters. Labels are validated as ASCII, so `GuideXosListBox` now stores them as bytes and copies to a character span when returning a label. This keeps the 127-character limit and UI behavior while halving that buffer; a boundary-length round-trip case covers it.

Guest evidence for Task Manager identity, taskbar selection, focus changes, concurrent residency, pointer and keyboard input, and the close/reopen path is recorded in the three QAC boot sections. Screenshots are captured after the relevant applications are resident; serial and QMP logs accompany them.

## Build and hosted evidence

All C188 outputs are below `out/dotnet/c188-external-app-identity/`, with unique attempt directories. Protected roots were inventoried before building; after-build comparisons are recorded below. The existing modified `ESP/ramdisk.img` stayed at its expected SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The staged guest images are private copies; the staging writer replaced only `/APPS/GXOSAPP.ELF`, added seven FAT clusters, and preserved the other 185 files.

The final protected inventory at `out/dotnet/c188-external-app-identity/attempt-066/protected-inventory/protected-comparison.json` reports 13 protected roots and 8,497 files before and after, with zero added, removed, or changed files. The preserved source ramdisk hash matches the expected value above. Attempt 061 contains the earlier comparison.

| Qualification | Evidence path | Result and SHA-256 |
| --- | --- | --- |
| C188 hosted identity/lifecycle | `out/dotnet/c188-external-app-identity/attempt-058/hosted-tests/` | 25 cases; identity 1,000; valid resizes 500; invalid requests 250; stale generations 250; lifecycles 100; allocation bytes 0 |
| C182/C185/C186/C187 hosted regressions | `out/dotnet/c188-external-app-identity/attempt-059/prior-host-regressions/` | 15 / 28 / 15 / 22 cases, all PASS |
| Managed UI aggregate | `out/dotnet/c188-external-app-identity/attempt-052/ui-regressions/evidence/` | 38 suites / 930 cases, all PASS |
| C188 candidate NativeAOT | `out/dotnet/c188-external-app-identity/attempt-054/build/` | ELF64 x86-64, 7 load segments, 0 relocations; all 8 required factory/dispatch/lifecycle symbols retained; ELF SHA-256 `9F84C525A3D6053EAB6D489563CD0C31B718CF082B3C0ED79544DBA6F9E3FEFC` |
| C188 candidate freestanding kernel | `out/dotnet/c188-external-app-identity/attempt-007/kernel-candidate/` | ELF SHA-256 `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` |
| Proof-disabled production NativeAOT | `out/dotnet/c188-external-app-identity/attempt-060/production-nativeaot/` | Standard C160 composite profile with production Task Manager and File Explorer; PASS; C188 identity and route absent; ELF SHA-256 `40953EEF2539484603A7CAC44255B7D47A3FCA5E8684E72FE4B7A18A60589BB8` |
| Proof-disabled production kernel | `out/dotnet/c188-external-app-identity/attempt-008/kernel-production/` | Five production IDs present; C188 identity absent; ELF SHA-256 `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` |
| Staged C188 candidate ramdisks | `out/dotnet/c188-external-app-identity/attempt-055/boot-1/` through `attempt-057/boot-3/` | 2,117,632-byte candidate ELF, SHA-256 `9F84C525A3D6053EAB6D489563CD0C31B718CF082B3C0ED79544DBA6F9E3FEFC`; each staged ramdisk SHA-256 `CC3DEB0DA01B5A33AC159062BF72A846B9CFF6FCF4511FC0A90DB8FA4309FB1D`; 185 unrelated files preserved in each copy |
| Staged proof-disabled production ramdisks | `out/dotnet/c188-external-app-identity/attempt-062/production-boot-1/` through `attempt-064/production-boot-3/` | 2,117,632-byte production ELF, SHA-256 `40953EEF2539484603A7CAC44255B7D47A3FCA5E8684E72FE4B7A18A60589BB8`; each staged ramdisk SHA-256 `CE79E453AACEE25DAA41A7F5152480CD5AC1A57A215C093EA1D7941A14D52BD3`; 185 unrelated files preserved in each copy |

ManagedUiContracts and CounterApp were each restored and built independently into the isolated C188 artifact root before host composition. The NativeAOT link map retains the CounterApp factory, constructor, action, input, resize, and teardown methods plus `DispatchC188ExternalAppIdentity`. PE-to-ELF validation confirmed the candidate is a freestanding ELF64 x86-64 image with no relocation entries.

The earlier C186R1 audit documented 212 C185-generated files whose pre-C186 byte identities are unavailable. C188 does not claim those historical bytes were retroactively verified.

## QAC acceptance boots

QAC is the only guest launcher used. The original ramdisk is not supplied to QEMU; each run uses an isolated candidate ESP under `out/dotnet/c188-external-app-identity/attempt-055/` through `attempt-057/`, with the identity retry under `attempt-065/` and proof-disabled production compatibility copies under `attempt-062/` through `attempt-064/`. Reservation IDs, QEMU child IDs, QAC status/list before and after, serial logs, QMP logs, screenshots, media hashes, and explicit release checks are stored per boot.

### Boot 1 — Identity and launch

The first request, reservation `71efb4e6618f4e3abbab2312373a69b9` from attempt 055, expired before launch after QAC's 1,800-second exclusive-slot wait; QAC stderr records `Reservation wait timed out.` The unrelated acceptance guest remained active and untouched. The identity retry from attempt 065 also expired before launch after 1,800 seconds; QAC stderr records reservation `b40679a6886749f6a626bf6280fc8425` timing out. The retry from isolated attempt 067 expired before launch after the same 1,800-second wait; QAC stderr records reservation `cc775d6911d844ba9d4d870a14f8a797` timing out. QAC's final list and inspect evidence in `attempt-067/boot-1/` show the other acceptance guest still Running and owner-verified (reservation `2c060850d2f640428021acbcc836ece5`, QEMU PID 25532) after 5,894 seconds, as of 2026-10-23T00:25:44Z. No C188 QEMU guest launched. Each staged image uses candidate ELF SHA-256 `9F84C525A3D6053EAB6D489563CD0C31B718CF082B3C0ED79544DBA6F9E3FEFC` and preserves the 185 unrelated ramdisk files. The remaining C188 and ordinary production acceptance boots require the active reservation to be released. Once admitted, the C188 identity sequence launches CounterApp by selector 9, exercises its button and keyboard input, opens Task Manager, and captures the snapshot while CounterApp remains resident.

### Boot 2 — Resize and lifecycle

Result and evidence: pending Boot 1 and the QAC reservation. The acceptance sequence checks both accepted bounds, attempts 721×480 and 720×481, closes through the window lifecycle, reopens, and verifies a new generation and stale-generation rejection.

### Boot 3 — Coexistence and reporting

Result and evidence: pending Boot 1 and the QAC reservation. CounterApp remains resident with Notes, Calculator, Task Manager, and File Explorer (five managed residents total). Task Manager is captured after all five launches; focus and taskbar transitions are exercised, then CounterApp is closed while the other applications remain open. Settings remains part of the proof-disabled production registration check, not a sixth simultaneous guest resident.

### Ordinary production compatibility boots

The shared `GuideXosListBox` storage representation changed to make the Notes open-file dialog construct successfully while retaining the public behavior and supported label boundary. The proof-disabled production build therefore receives three ordinary guest boots in addition to the three C188 candidate scenarios. The test image uses production selectors 4–8 and the proof-disabled NativeAOT image; its kernel and NativeAOT qualification manifests report no CounterApp descriptor or C188 selector route. Production QAC runs will capture the ordinary Notes launch and file dialog, all five managed application identities resident together, and a Notes close/reopen lifecycle. Reservations, serial/QMP logs, screenshots, and release checks will be recorded under attempts 062–064.

## Limitations and C189 direction

C188 is a compile-time development descriptor for one statically composed CounterApp. It does not provide general third-party installation, a dynamic loader, an installer, or a separate application process. A production deployment still needs an explicit supported packaging and trust model; the C188 candidate must not be placed in Server v0.3 release media.

C189 should first define a versioned external application package and identity contract, including ownership, signing or trust, upgrade/removal, and failure behavior. It should preserve native identity and bounded residency guarantees demonstrated here before considering broader dynamic registration.

## Outcome

The implementation checkpoint is committed locally as `5b023b39ef42317a9f6fb7c75a7efc1da3f28294`; protected inventory and QAC admission evidence are in follow-up documentation commits. At the original C188 closeout, the three focused QAC boots and three ordinary production compatibility boots remained pending because QAC's acceptance reservation persisted through three 1,800-second admission windows. The final protected-tree comparison passed with zero changes. C189 had not been started. The later C188R1 outcome is below.

## C188R1 qualification closeout — 2026-10-22

C188R1 continued C188 on branch `v1.1_DOTNET_SUPPORT`, starting at `9a651829a559e0e254f1c6d0dfea99a71f290c5b`. The current classification is **Outcome D — Blocked**. The continuation reused the retained development and proof-disabled production candidates; no product source or qualification script was changed and no candidate was rebuilt.

### Preflight and candidate reuse

The QAC CLI was installed at `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`; its supported `status`, `list`, `inspect`, and `run` workflow is captured in `qac-cli-help.txt`. The fresh QAC census before the first boot showed the coordinator online, zero reservations, zero managed sessions, and zero QEMU processes. The initial worktree snapshot contained 136 status entries, including the original modified ramdisk and existing untracked build outputs. D: had 50.90 GiB free. The development kernel and NativeAOT ELF matched their retained manifests: kernel SHA-256 `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA`, composite ELF SHA-256 `9F84C525A3D6053EAB6D489563CD0C31B718CF082B3C0ED79544DBA6F9E3FEFC`, independently compiled CounterApp assembly SHA-256 `169309B2045D8DDE6E2C7B58AF9C7B0A1D3028B18EA18822156B67E5CB1D4F4C`, and ManagedUiContracts SHA-256 `E2EB06B22BBEE40AED4742DBC25C00F186BDB1A9A9E1AA7CE946A8E4446018A8`.

The production kernel and composite ELF matched SHA-256 `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` and `40953EEF2539484603A7CAC44255B7D47A3FCA5E8684E72FE4B7A18A60589BB8`. Its candidate manifest retains selectors 4–8 and reports no CounterApp descriptor or C188 selector route. Development selector 9 remains the unique CounterApp route. The ABI remains v4 / 128 bytes, with callbacks at offsets 104, 112, and 120, and residency remains limited to five applications. The retained NativeAOT validation reports ELF64 x86-64, seven load segments, zero relocations, and the required static factory and lifecycle symbols.

Six isolated ESP copies were staged below `out/dotnet/c188-external-app-identity/continuation-C188R1-20261022/`. Focused media use ramdisk SHA-256 `CC3DEB0DA01B5A33AC159062BF72A846B9CFF6FCF4511FC0A90DB8FA4309FB1D` with the development kernel and app hashes above. Production media use ramdisk SHA-256 `CE79E453AACEE25DAA41A7F5152480CD5AC1A57A215C093EA1D7941A14D52BD3` with the proof-disabled production kernel and composite hashes above. The recorded staging manifests report 185 unrelated ramdisk files preserved and zero changes to other files. The original modified `ESP/ramdisk.img` still hashes to `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`.

No standalone repository `.phase` or stale-prompt guard file was present. The existing C188 runner constrains evidence paths to the C188 output root and refuses to overwrite existing serial, QMP, or acceptance evidence.

### QAC and guest attempts

The first focused QAC run acquired reservation `19031b98d7a342ec9b99b870b1fcf85d` and QEMU child PID 12656. QAC verified its release after the runner's 180-second guest-readiness timeout. The kernel printed the C188 identity-registry PASS marker, but the serial log stopped during the initial Notes selector-4 launch after `C148-NOTES-CONTEXT`; the runner never observed the input loop. CounterApp was not launched, so this is not a focused identity or interaction pass. The screenshot, serial log, QMP log, and runner failure are preserved in `focused-boot-1/`.

The first production run acquired reservation `942f45b2ef35478d9de5deb9ee44fb73` and QEMU child PID 22344. Its prelaunch census was clear, but QAC later reported that the reservation was degraded because one unmanaged QEMU process had appeared, compromising exclusive conditions. The runner captured that warning in QAC stderr, which it writes after the child exits. The guest printed input-manager initialization and began the Notes selector-4 launch, but the runner did not observe the main input loop within 180 seconds. QAC verified release. No all-five-app registration, behavior, or lifecycle gate passed in this run; the screenshot and raw logs are preserved in `production-boot-1/`.

Immediately after that release, QAC admitted another acceptance reservation. The fresh blocking census shows reservation `4f17779acbd14810b4deb3d26a3a20a3`, owner `LEON2_RDT\guideX` (verified owner process PID 21640), and managed QEMU child PID 10628. Its command line identifies a separate Server v0.3 package acceptance run under `D:\dev\guideXOSServer`, with serial output in `docs/evidence/s22r13r1-pacman-guest-acceptance-20261022/boot1/serial.log`. This session was not touched. No additional C188R1 guest was launched. The remaining two focused boots and two production boots are unrun.

### Regression and preservation evidence

No source changed, so hosted tests and builds were not repeated. The retained evidence remains: C188 25 cases; C182 15; C185 28; C186 15; C187 22; and the managed UI aggregate of 38 suites / 930 cases, all previously passing. These results do not substitute for guest acceptance.

The fresh C188R1 protected-tree comparison covered the actual current inventories: 13 protected roots and 8,497 files before and after, with zero added, removed, or changed files. The original ramdisk hash remained unchanged. Worktree comparison found no missing or changed pre-existing paths; the only additional status entry was this documentation update. Detailed candidate, QAC, workspace, guest, and protected-tree evidence is under `out/dotnet/c188-external-app-identity/continuation-C188R1-20261022/`; the raw historical attempts 001–067 remain intact. The older C185 artifact caveat is unchanged.

**Outcome D — Blocked.** The two attempted boots did not reach the runner's input-loop gate. At the blocking census captured at `2026-10-23T02:35:41Z`, QAC showed owner-verified reservation `4f17779acbd14810b4deb3d26a3a20a3` in `Running`, owned by `LEON2_RDT\guideX` (owner PID 21640) with QEMU child PID 10628. Its Server v0.3 acceptance workspace and full command line are preserved in `qac-blocking-census.json`. Static checks and prior hosted evidence do not establish production guest acceptance. C188 remains open, and C189 has not been started.
