# S22R3 — DeveloperStudio v0.2.1 Compatibility and v0.3 Package Qualification

**Date:** 2026-10-19

**Outcome:** **C — Blocked at the v0.3 runtime compatibility boundary.**

**Repository:** `D:\dev\guideXOSServer`, branch `main`

**Starting HEAD:** `79e446781b219188831682708cb79d0ceb37a585`

The owner selected the DeveloperStudio package present on Server v0.2.1 media as the preferred v0.3 candidate. The preserved v0.2.1 ISO confirms that a Native ELF DeveloperStudio package was included. Its manifest declares a Server 0.5.0 minimum, however, and its intended workspace, build, Run, and debugger workflows depend on host services absent from the current Server v0.3 bare-metal runtime. Integrating those services would exceed the bounded release-integration scope. No DeveloperStudio candidate is qualified for v0.3, and no v0.3 ISO was built.

This is a component decision. It does not approve publication of Server v0.3 and does not qualify Native Settings.

## 1. Historical v0.2.1 release and exact package

There is no `v0.2.1` Git tag in this checkout. The repository has the `v0.2` tag, the release-preparation commit `3daba9c3261e1c04058626d6f4a5ee0d43dc4` (“Prepare guideXOS Server v0.2.1 release artifacts”), and the later identity commit `9a11b3251004bfeb935cc85b7a1d2f8943387951` (“Regenerate v0.2.1 release identity”). The strongest release artifact is the preserved ISO at `dist/guideXOS-Server-v0.2.1-amd64.iso`; its sidecar names source commit `0e6039c0e3f1fd218424ba7367c9e83bd8d73223`.

The ISO is 90,245,120 bytes with SHA-256 `985b524a4c281ed3d00060df97adda38c99965c8d03a5d11f69db793e8174c86`. Its UEFI boot image is 90,177,536 bytes with SHA-256 `2867edb7dde1468c0f8ae2a73d4c09d386184a97a94295fb3e1f2874266b335e`. Both hashes match the release sidecar. The FAT image was read in place with a read-only filesystem view; the historical ISO and its embedded image were not modified or fully extracted.

The DeveloperStudio files actually present in that ISO are:

| Media path | Bytes | SHA-256 |
| --- | ---: | --- |
| `Apps/DeveloperStudio/app.json` | 1,175 | `4DEDC783EFD160A6B13D5AF9A9F87ABB6608004BB10CA8F42ABE6B306EE33701` |
| `Apps/DeveloperStudio/bin/amd64/developerstudio.elf` | 1,015,064 | `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7` |

The on-media manifest is version `0.1.0`, ID `com.guidexos.developerstudio`, kind `NativeElf`, AMD64, entry point `gx_main`, ABI `guidexos-c-abi-v1`, and runtime `native-elf`. It declares `minGuideXOSVersion: 0.5.0`, read/write filesystem permissions, a 960×640 resizable window, and Start-menu registration. `fileAssociations` is empty. The package contains the manifest and ELF; no separate application assets are declared.

The manifest in the ISO matches the Server source tree file at `0e6039c0…` after CRLF-to-LF normalization; the ELF matches the exact source-tree bytes. Their Git blob IDs are `8b2b8267a1e9d1fb6720b579eeabb23f2f441109` and `150b4958e569ce4d4e5f9e2c3146de94d94c879d`. This accounts for the raw manifest hash difference caused by Windows line endings and ties the on-media pair to the release source commit.

The historical package source location in the Server tree is `Apps/DeveloperStudio`; the release image stages it under `ESP/Apps/DeveloperStudio`. The DeveloperStudio implementation source was maintained separately at `D:\dev\guideXOS_Developer_Studio`. The v0.2.1 ISO does not record a DeveloperStudio source-repository commit. Its ELF hash is byte-for-byte equal to the Phase 25 signed-off ELF. That separate record identifies source baseline `715a29df535103d1d281c7181219b5f986b41f7c`, sign-off commit `bb66e11c38dffae21514384f73b1920b53ac64fe`, and paired Server v0.5 commit `fb71e36c77866cdabf20873e6ad3de0ddc3bbe02`. This hash match is useful binary lineage evidence; it does not make the Phase 25 manifest the v0.2.1 manifest or approve the binary for Server v0.3.

The Phase 25 manifest (`D59C1226…F53B`, 1,112 bytes) is distinct from the actual v0.2.1 media manifest (`4DEDC783…3701`, 1,175 bytes). The newer current-source pair is also distinct. No files from the newer DeveloperStudio source were changed. The separate DeveloperStudio checkout was inspected read-only; its `main` HEAD was `1e9c527806e204c755d167044b51d684645f053c`, and it remained untouched.

## 2. Historical feature contract and acceptance evidence

The v0.2.1 media manifest describes a bounded native workspace and source editor and grants window, drawing, logging, and filesystem read/write permissions. It registers the app in Start and declares no document associations. Those declarations establish package intent, not successful operation on the v0.2.1 runtime.

The v0.2.1 release sidecar verifies ISO layout, UEFI boot metadata, and input hashes. No v0.2.1 DeveloperStudio launch, interaction, or workflow log was found in the release evidence. The separate Phase 25 rehearsal for the same ELF hash on its paired Server v0.5 hosted runtime records App Model discovery, visible launch and clean close, opening a fixture project, edit/save, document switching with dirty-state preservation, completion interaction, successful and failed/recovery builds, Run, debugger workflows, and input delivery. Those results apply to the paired v0.5 environment; they are not v0.2.1 or Server v0.3 guest evidence.

The release record does not verify file dialogs, project creation, syntax highlighting, or a complete mouse-driven editor sequence. These are not advertised as qualified v0.3 capabilities. On v0.3, workspace file operations and build/run/debug are unavailable regardless of the capabilities demonstrated by the same ELF in the v0.5 rehearsal.

## 3. v0.3 compatibility assessment

| Dependency or workflow | Server v0.3 assessment | Classification |
| --- | --- | --- |
| Native ELF identity, AMD64 entry, `gx_main`, base C ABI | Loader contract and base call offsets are structurally compatible | Supported unchanged at the ABI level; not guest-smoke qualified |
| Window creation, drawing, event polling, close, keyboard and window-owned pointer events | Bare-metal host supplies these base calls and maps current input/close events | Supported unchanged at the interface level; not guest-smoke qualified |
| Package-relative `file_exists` / `file_read` | Existing compatibility calls are available | Existing compatibility interface; insufficient for workspace editing |
| Declared minimum Server version | Historical v0.2.1 manifest says `0.5.0`; target is `0.3.0` | Unsupported runtime requirement |
| Workspace stat/list/read/write/create/remove | Current bare-metal host table does not populate `file_stat`, `file_read_workspace`, `file_list`, `file_write_all`, `file_create_directory`, or `file_remove` | Missing; requires workspace-service work beyond bounded integration |
| Build Project | `build_project_*` host services are absent | Missing; requires the v0.5 development-service architecture |
| Run and debugger | `development_run_*` and `development_debug` host services are absent | Missing; requires the v0.5 development-service architecture |
| External assets | The released package is the manifest and static ELF only | No additional asset staging dependency found |

The v0.3 loader does not enforce `minGuideXOSVersion` by itself. That omission is not compatibility evidence. A successful discovery or window launch would still leave the app's central editing and development workflows unavailable. Reconstructing or backporting the missing workspace/build/run/debug services would turn this release phase into a major runtime project, so Gate B fails.

## 4. Release preflight correction and selection boundary

The existing Server pin was the Phase 25 manifest/ELF pair, not the historical v0.2.1 media pair. This phase did not retarget the package selector because the historical implementation is incompatible. It added a fail-closed check in `build.ps1` and `scripts/release-build-integrity.psm1` that rejects any DeveloperStudio manifest whose minimum Server version exceeds the v0.3 target. The check runs before build cleanup and is repeated against the final build identity. It does not change either application package or lower any identity/hash check.

Results:

- The current newer source pair was rejected by `build.ps1 -Arch amd64` with exit code 1 because it does not match the exact pinned manifest/ELF. The command stopped before cleanup. The preserved S22 ISO hash was unchanged at `B303C4C1A1CD31E500353CBD2DD7E11A8FD438B81AC9306285A6CD20EA1B1A3F`.
- The v0.2.1 on-media manifest and Phase 25 manifest both declare `0.5.0`; the new v0.3 runtime check rejects each with “release target 0.3.0 is unsupported.”
- A complete receipt containing the exact Phase 25 pair is rejected for target v0.3 and accepted only when the test target is v0.5.0.
- The exact v0.2.1 media manifest is rejected by the existing pinned-pair identity check; the current selector does not accept it.
- The PowerShell 5.1 release-integrity harness passed 18 assertions, covering stale/missing receipts, required stages and files, child failure propagation, error output with zero exit, current/newer and historical package rejection, exact hash enforcement, and target-version checks.

There is no selected DeveloperStudio package for a v0.3 production build. The old and current app pairs remain rejected; the Phase 25 identity is also rejected for v0.3 on its declared minimum. No package inventory was silently removed and no fallback binary was staged. A dedicated historical selector should be added only after a compatible runtime/package contract is established.

## 5. Build, QEMU, and Native Settings disposition

No production kernel build, release package, fresh ISO, package-content audit, or new QEMU guest was created because the compatibility gate failed. The current kernel received no changes, so the prior S22R classification of 21 undefined symbol names was not re-run or represented as new evidence. The preserved S22 ISO and all prior candidates were left intact.

One pre-existing QEMU process was observed during the later ownership census: PID `25192`, owner `LEON2_RDT\guideX`, launched from the separate checkout `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`. It was treated as a foreign guest and was not attached to, stopped, or modified. An earlier PID `12596` disappeared before its owner could be queried. No QEMU process was started by S22R3.

The S22R empty-wallpaper default repair remains in the current source, and its prior Native Settings runtime suite passed `1,426/1,426`. This phase did not assemble a fresh ISO, so it did not repeat the four-page Settings inspection, Blue Flower-to-Dinos Apply/readback/reopen sequence, or legacy Control Panel smoke. The Apply fix has not received the required exact-package guest acceptance. Native Settings remains unqualified for Server v0.3.

## 6. Outcome and next step

**DeveloperStudio v0.2.1-compatible edition — not qualified for Server v0.3.** The original media proves which package was included, but its declared runtime minimum and missing v0.3 workspace/development services block safe inclusion. The older manifest does not make its ELF compatible with v0.3.

**Native Settings — not qualified for Server v0.3.** Exact-package Apply acceptance is still outstanding.

**Overall Server v0.3 — not approved or published.** The package pipeline remains fail-closed; the release inventory still treats DeveloperStudio as required.

Recommended next step: the Server v0.3 owner should decide whether to defer DeveloperStudio through a separate package-inventory qualification, or authorize a dedicated compatibility project with DeveloperStudio and runtime owners. The minimum corrective project would need to resolve the `0.5.0` contract and supply or replace workspace file, build, Run, and debugger services before a fresh exact-package launch/workflow smoke. Do not relabel the Phase 25 pair or change the current application manifest to claim v0.3 compatibility.

Curated logs and exact media evidence are in [`docs/evidence/settings-s22r3-v03-package-20261019/`](evidence/settings-s22r3-v03-package-20261019/README.md). The implementation and evidence are committed locally on `main`; nothing was pushed or published.
