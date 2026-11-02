# DSBUILD1R2 — Phase 25 DeveloperStudio Compatibility Qualification

**Outcome C — incompatible with the current guideXOS Server v0.3 guest runtime for DeveloperStudio’s intended workspace and development workflows.** The historical artifact pair is verified. Its base Native ELF ABI and window lifecycle are structurally compatible, but the bare-metal runtime does not provide the workspace filesystem, build, Run, or debugger services that the Phase 25 application expects. The exact manifest also declares `minGuideXOSVersion: 0.5.0`, above the v0.3 target. Recommend Option C: defer DeveloperStudio from v0.3 pending an explicit owner decision and a later runtime/package qualification.

No production package, ESP file, release gate, or pin was changed. Static incompatibility prevented the guest-test precondition from passing, so no disposable image or QEMU guest was created. This phase is not a guest launch/close acceptance.

## 1. Actual starting state and preservation

The requested checkout was `D:\dev\guideXOSServer`.

| State | Actual at start | Last reported in DSBUILD1R |
| --- | --- | --- |
| Branch | `main` | `main` |
| HEAD | `bce4145843a8c93a4b253116e374341b51f11655` | `0513a65643f98b8c78ef709dfa7d6ea771677517` |
| Relation to `origin/main` | One commit ahead | At `origin/main` |
| Tracked worktree changes | None | Six modified paths |
| Untracked paths | 852 | Approximately 855 |

The six paths listed in DSBUILD1R are now included in local commit `bce4145843a8c93a4b253116e374341b51f11655` (`Fix default appearance persistence and release integrity`; author recorded by Git as Leon Aiossa), rather than being worktree modifications:

- `build.ps1`
- `kernel/core/desktop.cpp`
- `kernel/core/desktop_gradient_persistence.cpp`
- `kernel/core/include/kernel/desktop_gradient_persistence.h`
- `scripts/create-release-iso.ps1`
- `tests/settings_native_runtime_test.cpp`

The commit also contains the release-integrity changes and S22R evidence. The pre-existing untracked artifacts and both prior qualification reports were preserved. The active workspace `D:\dev\guideXOSServer_FONT_SUPPORT` is a separate checkout with its own eight tracked modifications and 7,748 untracked paths; it was not used or changed.

At completion, HEAD remains `bce4145843a8c93a4b253116e374341b51f11655` on `main`; tracked changes remain empty. This report is the only file added by this phase and is untracked. No files were staged and no commit was made.

## 2. Historical artifact identity and pairing

The Phase 25 sign-off record is `D:\dev\guideXOS_Developer_Studio\docs\RELEASE_CANDIDATE_STATUS.md` (2026-08-23). It identifies DeveloperStudio source baseline `715a29df535103d1d281c7181219b5f986b41f7c`, sign-off commit `bb66e11c38dffae21514384f73b1920b53ac64fe`, and paired Server commit `fb71e36c77866cdabf20873e6ad3de0ddc3bbe02`.

The exact historical pair is:

| Artifact | Authoritative bytes | SHA-256 | Verification |
| --- | ---: | --- | --- |
| Phase 25 ELF | 1,015,064 | `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7` | Raw blob from paired Server commit; the existing ESP ELF is byte-for-byte equal |
| Phase 25 `app.json` | 1,112 | `D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53B` | Raw blob from paired Server commit; `tests/fixtures/developerstudio-phase25-app.json` is byte-for-byte equal |

The complete manifest digest above was calculated from the raw `Apps/DeveloperStudio/app.json` Git blob at `fb71e36c77866cdabf20873e6ad3de0ddc3bbe02`; its size is 1,112 bytes. The Phase 25 status text transcribes a 63-character value ending in `F53`, so the authoritative final character is `B`. The current release-integrity module pins the complete value.

The existing package locations have three distinct identities:

| Location | Manifest | ELF | Meaning |
| --- | --- | --- | --- |
| `Apps/DeveloperStudio` | 2,011 bytes, `AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9` | 1,104,652 bytes, `F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6` | Current newer source package; manifest enables document activation and lists nine extensions |
| `ESP/Apps/DeveloperStudio` | 1,176 bytes, `7AF95D35F068C44F4529420922E0F4BEC5F705568514688D160E2790022ACB09` | 1,015,064 bytes, `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7` | Old ELF with a different manifest; not the signed-off pair |
| Paired Server commit `fb71e36c…` | 1,112 bytes, `D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53B` | 1,015,064 bytes, `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7` | Exact Phase 25 signed-off pair |

The ESP manifest’s JSON has `icon: "app.developerstudio"`; the signed-off manifest has an empty icon. Both have no file associations and both declare minimum version `0.5.0`, but their raw bytes and hashes differ. The ESP manifest is not the current newer manifest either: the current source manifest adds `supportsDocumentActivation: true` and `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, `.hxx`, and `.txt` associations. The old ELF was not paired with either newer manifest for this assessment.

The prior DSBUILD1R report captured an earlier working-tree `build.ps1` pin of `7AF95…`. The actual current checkout is one commit newer: `build.ps1` loads `Get-DeveloperStudioSignedOffIdentity` from `scripts/release-build-integrity.psm1`, which pins the exact Phase 25 `D59C…F53B` manifest and `5343…CAC7` ELF. The ESP’s `7AF95…` file is a stale staged manifest, not the active source pin. The current source package still fails the exact-pair preflight.

## 3. Static compatibility audit

### ELF, entry point, and package contract

`readelf` reports the historical binary as ELF64, little-endian AMD64 `ET_EXEC`, with entry address `0x2998f0`, five program headers, and no dynamic section or shared-library dependencies. The symbol table contains `gx_main` at the entry address. It is a static executable; no external resource file is named by the Phase 25 package. The signed-off package contains only `app.json` and `bin/amd64/developerstudio.elf`.

The historical manifest contract matches the current loader’s identity checks: ID `com.guidexos.developerstudio`, kind `NativeElf`, one AMD64 entry, package-relative ELF path, `gx_main`, `guidexos-c-abi-v1`, and `native-elf`. Its minimum version is `0.5.0`, while this assessment targets Server v0.3. The inspected manifest loader and bare-metal package parser do not enforce `minGuideXOSVersion`; this is an unchecked manifest/runtime contract mismatch, not evidence that v0.3 satisfies the declared minimum.

### Host-call ABI and runtime services

The x86-64 calling convention and all pre-existing host-call offsets remain stable. In the Phase 25 SDK table, `development_debug` is the last slot at offset 232 and the table is 240 bytes. The current SDK appends `play_pcm` at 240 and `get_document_activation_path` at 248, for a 256-byte table. The current bare-metal runtime initializes a 256-byte table, so these later appends do not move the historical app’s slots. The historical app predates document activation and its manifest has no associations; it does not need the new document-path call.

The current bare-metal runtime provides the basic calls used to construct and display a window: `log`, `get_api_version`, `request_window`, `request_window_ex`, drawing calls, `poll_event`, `exit`, and package-relative `file_exists`/`file_read` services. Its poll loop maps current close, keyboard, and window-owned mouse events into Native ABI events. The Phase 25 `gx_main` checks the required context, logging, API-version, and window calls; it creates a window, renders, processes events, and handles close before returning through `exit`. This supports structural startup and lifecycle compatibility, but it is not guest evidence.

The application’s intended filesystem and development services are missing from the current bare-metal host table. `initialize_host_table` does not populate `file_stat`, `file_read_workspace`, `file_list`, `file_write_all`, `file_create_directory`, or `file_remove`. It also does not populate `build_project_*`, `development_run_*`, or `development_debug`. The Phase 25 application source constructs its workspace filesystem from these APIs; its adapters explicitly fail when the pointers are absent. Thus project browsing/loading, source read/write, project creation, build, Run, and debugger operations are unavailable in this runtime. The manifest’s `filesystem.read` and `filesystem.write` permissions do not implement those services.

This is a release-critical functional mismatch for DeveloperStudio, which the Phase 25 record describes as a hosted-development studio. The base ABI is append-compatible, but the v0.3 guest host does not provide the app’s core workspace and hosted-development behavior. A successful package discovery or window launch would not make that package compatible for its intended use.

The current native ABI layout test also has a stale `sizeof(gx_host_calls) == 248` assertion. The current header is 256 bytes after the document-activation append, so that compile-time assertion remains a v0.3 validation gate to reconcile and pass; it does not change the offset-preservation finding above.

## 4. Disposable guest qualification

**Not run.** Static compatibility did not pass because the current guest host omits DeveloperStudio’s required workspace and development services, and the signed-off manifest declares a higher minimum runtime version. Under the task’s gate, no disposable package/image was built and no fresh QEMU ownership census or guest interaction was started. No serial log, screenshot, input trace, application identity log, or window lifecycle log was produced in this phase.

DSBUILD1R’s earlier guest attempt is not transferable: it booted an October 5 development fixture with the newer `F072…` / `AB1…` candidate pair. It discovered that different package, but bounded input did not launch it and no app window appeared. That run did not exercise the Phase 25 pair or the current release runtime. Its prior QEMU census is historical and was not treated as a fresh census for this assessment.

## 5. Release-option assessment

### Option B — exact Phase 25 pair

The exact pair is reproducibly identifiable, and the current source pin already records its ELF and manifest hashes. Shipping it would roll back document activation and all nine source/text associations present in the current candidate. Its manifest still declares minimum Server version `0.5.0`, and the current guest host still lacks its workspace, build, Run, and debugger services. I do not recommend Option B for v0.3 on the evidence available.

Option B requires explicit authorization from both the DeveloperStudio release owner and the Server v0.3 release owner to select the Phase 25 scope for v0.3 and to resolve the `0.5.0` minimum-version contract. The runtime owner must also provide or explicitly accept the absent guest services before claiming DeveloperStudio compatibility. After that decision, packaging would need to place the exact 1,112-byte `D59C…F53B` manifest and 1,015,064-byte `5343…CAC7` ELF in the source package, regenerate the ESP and ramdisk from that package, and verify the release manifest records both exact identities. No package changes are authorized in this phase.

### Option C — defer from v0.3

Deferral is technically feasible at runtime: bare-metal App Model discovery scans package directories under `/Apps` and does not require DeveloperStudio to exist. With other packages and the `/Apps` directory retained, omitting DeveloperStudio should not prevent desktop boot or discovery of remaining packages. It removes DeveloperStudio from the Start menu and removes its nine current candidate document routes. Built-in Notepad remains the `.txt` default (and handles `.log`, `.ini`, and `.cfg`); the C-family routes have no DeveloperStudio handler while it is absent and become unsupported unless another capable handler is installed. Other applications’ packages are not coupled to this package in the runtime discovery loop.

The release pipeline currently makes DeveloperStudio mandatory. The v0.3 owner must authorize deferral before a separate packaging change updates the early source preflight, `Stage-AppModelPackages`, the NativePackageDirs passed to the wallpaper packer, the required `developerStudio` stage/artifacts in `scripts/release-build-integrity.psm1`, and the required DeveloperStudio records in `scripts/create-release-iso.ps1`. The integrity tests and package inventory checks must distinguish an explicitly deferred package from an accidentally missing required package. Keep exact identity checks whenever DeveloperStudio is included in a future build.

After those authorized changes, validate that the other packages stage and discover normally, the desktop boots, file association/default-handler behavior is correct without DeveloperStudio, and the exact new v0.3 ISO passes the existing release-stage checks and guest acceptance. No such changes were implemented here.

## 6. Recommendation and remaining v0.3 gates

Recommend **Option C**, subject to explicit Server v0.3 release-owner authorization and notice to the DeveloperStudio release owner. The exact Phase 25 identity is established, but its stated minimum version and missing guest services prevent recommending it for this v0.3 runtime. Option B should be reconsidered only after the service/runtime gap and manifest-version contract are resolved and a fresh exact-pair guest launch, basic interaction, clean close, and stable-desktop acceptance pass.

Remaining v0.3 release gates recorded by the inspected reports and current source are:

- Record the owner’s choice of an exact DeveloperStudio pair or explicit deferral. The current `Apps/DeveloperStudio` pair does not match the active Phase 25 gate, so the current production preflight fails before build outputs are replaced.
- If deferring, authorize and implement the package-inventory and release-integrity changes listed above, then validate a newly built exact ISO. If including DeveloperStudio, first qualify the selected package against the actual guest runtime.
- Reconcile and pass the native ABI layout test’s stale 248-byte total-size assertion against the current 256-byte ABI table.
- S22R records that the appearance persistence repair and build-integrity fixture harness passed, and that a fresh isolated kernel symbol audit classified all 21 undefined names. It also records no fresh production ISO or guest boot because the DeveloperStudio preflight blocked the build. Run the remaining exact-package release and guest gates against the newly selected inventory; the prior S22 ISO/guest is not a substitute.
- The S22 qualification still records Native Settings as unqualified for v0.3 inclusion pending a new complete package and exact-image guest qualification, including its persistence and legacy Settings checks.

### Evidence reviewed

- `docs/validations/DSBUILD1_DEVELOPER_STUDIO_ARTIFACT_QUALIFICATION.md` and `docs/validations/DSBUILD1R_DEVELOPER_STUDIO_RELEASE_CANDIDATE.md` for the prior artifact and guest records.
- `D:\dev\guideXOS_Developer_Studio\docs\RELEASE_CANDIDATE_STATUS.md` and the raw manifest/ELF blobs at paired Server commit `fb71e36c77866cdabf20873e6ad3de0ddc3bbe02` for Phase 25 provenance and identity.
- `scripts/release-build-integrity.psm1`, `build.ps1`, and `scripts/create-release-iso.ps1` for active pins, required package stages, and release manifest validation.
- `sdk/include/guidexos/abi.h`, `kernel/core/native_elf_baremetal.cpp`, `app_manifest_loader.cpp`, and the Phase 25 source baseline `715a29df535103d1d281c7181219b5f986b41f7c` for ABI, manifest, application entry, filesystem, and lifecycle behavior.
- `tests/fixtures/developerstudio-phase25-app.json`, `tests/native_abi_layout_test.cpp`, and `tests/appmodel_default_handlers_test.cpp` for the exact manifest fixture, current ABI size assertion, and `.txt` default-handler behavior.
- `docs/SETTINGS_S22_V03_PACKAGE_QUALIFICATION.md` and `docs/evidence/settings-s22r-v03-package-20261019/manifest.json` for remaining v0.3 package and guest gates.

No owner choice was inferred, no release pin or package was changed, and no evidence-only commit was made. DSBUILD1R2 stops at this compatibility and release-option assessment.
