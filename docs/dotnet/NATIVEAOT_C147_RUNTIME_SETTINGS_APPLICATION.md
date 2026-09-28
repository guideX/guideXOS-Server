# NativeAOT C147 — Persisted Runtime Settings Application

## Purpose and C146 baseline

C147 proves that one existing C146 setting changes behavior in a shared managed-control path after Apply and after a fresh boot. Managed Notes is launched first on each proof boot, and its real ListBox receives physical wheel events before the Settings Center is launched.

C146 persists a 25-byte `GXSC` version-1 record at `/system/apps/GXSETT.BIN`. The existing native route maps that exact path to `/GXSETT.BIN` on the writable proof ESP. The format, CRC, VFS calls, and nine-field `ManagedSettingsSnapshot` remain unchanged.

## C146 snapshot audit and selected field

| Snapshot field | C146 section | C147 classification |
|---|---|---|
| `Density` | Appearance | UI/application-only |
| `ShowStatus` | Appearance | UI/application-only |
| `ShowAdvanced` | Appearance | UI/application-only |
| `InputEnabled` | Input | UI/application-only section state |
| `NaturalScroll` | Input | Runtime-backed |
| `ScrollSpeed` | Input | UI/application-only in C147 |
| `ShowKeyboardTips` | Input | UI/application-only |
| `StatusDetail` | System | UI/application-only |
| `ReportFormat` | System | UI/application-only |

`NaturalScroll` is the safest existing runtime field: it reverses logical wheel direction, has a deterministic and reversible effect, needs no hardware or kernel transport change, and is meaningful across existing scrollable controls. C147 applies it only to logical wheel-to-scroll policy. Raw PS/2/native wheel transport and the C137 event source are unchanged. The other persisted fields continue to drive the Settings Center’s controls and preview only; C147 does not claim they alter OS-wide behavior.

## Runtime state and startup load

`GuideXosRuntimeSettings` owns the active immutable runtime snapshot. It exposes a typed `Current.NaturalScroll` read and a single wheel-delta transform; only startup initialization and a verified Settings Center Apply can publish a value. The default runtime snapshot is initialized synchronously with `NaturalScroll=false`.

`GuideXosApplication.Dispatch` calls `EnsureInitialized` before the first managed application launch or input callback. `ManagedSettingsRuntimeStartup` uses the existing `ManagedSettingsStore.Load()` parser once, accepts a validated snapshot, and publishes its supported runtime field. It retains the parsed semantic snapshot and store for Settings Center reuse. Controls do not open or parse `GXSETT.BIN` themselves, and consumers do not poll the file.

Missing files resolve to `ManagedSettingsSnapshot.Defaults`; boot does not create a file. Invalid, truncated, corrupt, or unsupported-version records are rejected by the C146 store, and the shared runtime falls back to the same defaults used by Reset. Boot continues without an early modal. If Settings Center is later opened, it hydrates from the shared startup snapshot and retains C146’s recovery warning behavior.

The invalid-startup physical proof deliberately skips only the allocation-heavy C146 Settings Center focused suite after the C147 startup/runtime/consumer suites and C146 format/store tests pass. A 64 KiB resident NativeAOT heap was exhausted when that additional Settings Center suite ran after the invalid-record startup path. Valid persistence boots still run the full 11-case C146 Settings Center suite. The recovery boot still launches the Settings Center, verifies default hydration, and checks that the real Notes behavior already used defaults before that launch.

## Shared consumer and Apply semantics

`GuideXosControlHost` applies the runtime transform once at its shared wheel boundary. Managed Notes uses its production ListBox and TextArea; the Settings Center’s ScrollView also reaches that shared boundary. No consumer reads persisted bytes directly. The C147 focused consumer cases cover standard/natural direction, boundaries, pre-Apply and immediate-Apply behavior, relaunch, TextArea, unrelated settings, invalid values, and parser rejection.

Settings Center preserves C146’s working/applied/persisted model:

1. Validate the working candidate.
2. Write it through the existing C146 store.
3. Stat, read back, deserialize, and compare the exact snapshot.
4. Publish the supported runtime snapshot through one bounded assignment.
5. Update applied/persisted state and clear dirty state.

Runtime publication cannot reject a semantically valid snapshot after the store checks. If persistence fails, runtime and persisted/applied values remain unchanged, the working edit stays dirty, and the existing persistence MessageBox is shown. The C146 failure-injection case proves this ordering.

Reset confirmation changes only the working snapshot. Runtime and persistence change only after Apply. Dirty-close Discard closes without saving or publishing the working value. Cancel keeps the Settings Center open and leaves runtime and persisted state unchanged. Closing or relaunching Settings Center does not own or reset runtime state; the new launch hydrates from the shared persisted startup snapshot.

## File compatibility and architecture limits

The v1 format remains byte-for-byte compatible: version 1, 25 bytes, existing nine fields, and existing CRC-32/IEEE rules. No new field or migration behavior was added. The managed host ABI remains v1 with a 104-byte table. C147 adds no settings daemon, registry, service, file watcher, generic property bag, subscription framework, reflection-based serializer, focus system, ownership tree, modal stack, or capture stack. Applications retain control ownership and the existing one-modal, one-transient-capture, and one-drag-owner policies.

## Proof media and test coverage

`scripts/dotnet/run-c147-runtime-settings.ps1` drives the existing NativeAOT composite and dedicated proof kernel over writable test ESP directories. Each independent persistence sequence starts from a fresh missing-file image, applies `NaturalScroll=true` through physical Settings Center input, verifies immediate real Notes behavior and durable read-back, then boots fresh to verify the saved value before Settings Center opens. The sequences also cover dirty-close Discard and Cancel; sequence 1 additionally covers Reset followed by Apply. Corrupt and valid-header/unsupported-version images prove default behavior. The proof uses dedicated evidence media and does not write to the protected ordinary ramdisk.

Focused test totals:

- C147 startup integration: 10 cases for valid, missing, corrupt, unsupported-version, truncated, one-time initialization, ready state, defaults, and VFS-handle cleanup.
- C147 runtime state: 15 cases for defaults, load/commit, same-value and invalid commits, whole-snapshot consistency, Apply failure, Reset, Discard, and runtime preservation.
- C147 consumer integration: 10 cases, including real ListBox and TextArea behavior, immediate Apply, relaunch, and unrelated settings.
- C146 format: 13 cases; C146 store: 10 cases including the 50-save stress pass; C146 Settings Center: 11 cases on valid persistence boots.
- The C147 physical Settings Center sequences exercise the relevant C144 working/applied, editing, scrolling, Apply, close, and relaunch paths. They also exercise the relevant C145 Reset confirmation, dirty-close Apply/Discard/Cancel, focus restoration, and persistence-error MessageBox paths.
- Standalone C144 full-phase rerun was attempted after C147: boot 1 timed out at 360 seconds before the C144 proof marker. Its serial log ended after C128 lifecycle and C131 checkbox suites passed; no C144 or C145 focused suite result was emitted. Standalone C145 full-phase was not separately rerun. These are not reported as passing suite totals.
- The selected lower-level wheel subsystem was independently rerun with the C137 phase runner: 46/46 cases passed on each of 3 fresh QEMU boots (transport 12, TextArea 16, ListBox 18), with relaunch and capture cleanup also passing.

The C147 runner distinguishes suites executed on valid persistence boots from the C146 Settings Center suite skipped only on invalid-startup recovery boots. The invalid and future-version recovery boots still launch Settings Center and verify default hydration; only the allocation-heavy C146 focused suite is skipped on those two boots.

## Restoration and measured evidence

The C147 runner saves the starting ordinary kernel files, restores `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf` byte-for-byte after proof boots, checks the protected `ESP/ramdisk.img` hash, and runs three fresh ordinary QEMU boots. Proof and ordinary manifests are written below `out/dotnet/c147-runtime-settings`; mutated proof ESPs are evidence only and are not committed.

Measured acceptance evidence:

- Outcome A. C147 startup: 10/10; runtime state: 15/15; consumer integration: 10/10; C146 format: 13/13; C146 store: 10/10 including 50-save stress; C146 Settings Center: 11/11 on valid persistence boots.
- Three independent persistence sequences: PASS / PASS / PASS. Each applied and loaded the same custom snapshot with `naturalScroll=1`. Sequence 1 then Reset+Apply persisted defaults (`naturalScroll=0`); sequences 2 and 3 retained the custom value (`naturalScroll=1`).
- Real pre-Settings-Center behavior: on default boots, Managed Notes ListBox wheel delta `-1` moves from item 0 to 3; with persisted natural scroll, delta `-1` stays at item 0 and delta `+1` moves to item 3. This was observed before Settings Center launch on fresh verify boots.
- Corrupt-record and unsupported-version startup both passed default recovery and launched Settings Center. The invalid-startup boots skipped only the allocation-heavy C146 Settings Center focused suite because the fixed 64 KiB resident NativeAOT heap was exhausted when that suite was appended after the recovery path; valid boots ran all 11 cases.
- C147 composite ELF SHA-256: `2E4C26E76E6D2631F2D36932A65C23D4AE31E72B84C249FC7FC42A247051D52C`. C147 proof kernel SHA-256: `294AA1F1825BEFC3EE0309A63FD5143FBA31EA2565A1FA1C9282D334F55B3490`.
- C147 starting test-media directory SHA-256: `118A42A2978FBA05F8164225DAC944EBEFBC2F162514344F959CE61D07682F26` for all three independent sequences. Post-write media hashes: sequence 1 `CF1514048D3E865315332E611819E44DCB8F46113A671ABF838B76C5A038BBD7`; sequence 2 `E6225B144CE9FBA5FA0BDCFCC402EFF3E92FEE176F36FF1A5BDBFCC2A83A638F`; sequence 3 `E6051A1E345BC1CEF3E074FF72F758C18B5200784161D0927543DE25E107A464`.
- The three C137 regression serial SHA-256 values are `45C02BD51515A963FD193A7C6F11E5744DFD944A54E94A8FBA73083D6C483B71`, `AA6C1603D035179AC4A9698288D7900DCAF0E274D3AD07C5B378DD2724643E9D`, and `3F3217EB3DB074AFE2F8F99AEF64E63D71096447FDECA80D0597761F8889F8BB`.
- The three ordinary QEMU boots passed. Their serial SHA-256 values are `A49E4A88A895DB443523F99F69A4B0A73F280ECE5B0F4093037330D1908184AB`, `9A2064BF03546D1B5D4E77F74467A586FE1D315DFEF338D505455E1779DBB59A`, and `1B7154A4CED86BD36180F2FF9D5C14E500059CB87D00CCEAD1E57F3A0C53E04F`.
- Ordinary kernel and ESP kernel were restored and hash-verified at `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203`; protected `ESP/ramdisk.img` remained unchanged at `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE`.
- Final UI marker: modal owner none, popup capture none, drag owner none, valid viewport, 9 registrations of capacity 10. ABI remains v1/table size 104.
- C147 run manifest: `out/dotnet/c147-runtime-settings/c147.manifest.json`; ordinary boot manifest: `out/dotnet/c147-runtime-settings/ordinary.manifest.json`; C137 independent regression manifest: `out/dotnet/c147-runtime-settings/regression-c137/c137.manifest.json`. The standalone C144 timeout evidence is under `out/dotnet/c147-runtime-settings/regression-c144/boot-01/`.
- Starting HEAD: `af1f04776b7a0bde78499c2744cf4856af9a9218`; final source is one local commit on `v1.1_DOTNET_SUPPORT`, upstream `origin/v1.1_DOTNET_SUPPORT`. The configured push was rejected with `git@github.com: Permission denied (publickey)`; no credentials, remotes, or authentication settings were changed.

## Deferred work

C147 deliberately supports one shared runtime-backed field. Mapping the remaining persisted fields to system-wide behavior, adding profiles or policies, supporting live file watching, adding migrations or atomic file replacement, and introducing cross-application notifications remain future work.
