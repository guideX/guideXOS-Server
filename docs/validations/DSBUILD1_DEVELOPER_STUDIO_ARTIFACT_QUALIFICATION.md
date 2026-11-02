# DSBUILD1 — DeveloperStudio signed-off ELF qualification

**Outcome B — the mismatch is explained, but release approval for the current package is unresolved.** The existing hash gate is working as written. No signed-off hash was changed, no executable or manifest was replaced, and the normal AMD64 build was not rerun while the artifact approval boundary remains open.

## 1. Repository and starting state

- Authorized checkout: `D:\dev\guideXOSServer`.
- Branch: `main`.
- Actual starting and ending HEAD: `eac07e130f439b91d328558f286fa51266b9c7a0` (`fix: handle PacMan source registration in Windows PowerShell`). The requested expected starting HEAD was `192854434dfe8da4234db81af9f64d7841406bff`; the actual checkout was one commit beyond it. The PMBUILD1 commit remains an ancestor and was not changed.
- Tracked worktree state at start and end: clean. The initial census found 697 untracked paths; the final recorded census found 708, including this report. PacMan audio packages and validation evidence were preserved. This phase wrote only this report, removed or changed no untracked artifacts, and made no commit.
- No branch, history, worktree, or generated package operations were performed. The FONT_SUPPORT checkout and other development checkouts were not modified. DeveloperStudio's repository was inspected read-only.
- Capacity checked before considering builds: `D:` had 9,966,006,272 bytes free and `E:` had 6,924,544,925,696 bytes free. No artifact-heavy build was run.
- No DSBUILD1 phase marker existed before this report; the prior PMBUILD1 validation record is present.

## 2. Expected and actual ELF identity

The gate in `build.ps1` (`Stage-AppModelPackages`) pins DeveloperStudio to:

| Identity | Size | SHA-256 | ELF details |
| --- | ---: | --- | --- |
| Last signed-off artifact | 1,015,064 bytes | `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7` | ELF64, little-endian AMD64 `ET_EXEC`; `gx_main` at entry `0x2998f0`; `.symtab` present; no `.debug_*` sections |
| Current `Apps` package artifact | 1,104,652 bytes | `F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6` | ELF64, little-endian AMD64 `ET_EXEC`; ELF entry `0x500be590`; sectionless, with no dynamic section |

Current source path checked by the gate:

`D:\dev\guideXOSServer\Apps\DeveloperStudio\bin\amd64\developerstudio.elf`

The intended ESP destination is:

`D:\dev\guideXOSServer\ESP\Apps\DeveloperStudio\bin\amd64\developerstudio.elf`

The intended runtime/wallpaper package path is:

`D:\dev\guideXOSServer\out\wallpaper-pack\Apps\DeveloperStudio\bin\amd64\developerstudio.elf`, embedded in `ESP\ramdisk.img` when that image is rebuilt.

The exact failing condition is the size/hash comparison at `build.ps1:508`, which emits: `DeveloperStudio signed-off ELF size/hash changed; refusing to stage it`. The current ELF passes the ELF64/AMD64 header check; it fails the signed-off identity comparison. The check runs before the source package tree is copied into `ESP\Apps\DeveloperStudio`.

## 3. Signed-off artifact provenance

The authoritative existing approval is DeveloperStudio's Phase 25 release-candidate record in `D:\dev\guideXOS_Developer_Studio\docs\RELEASE_CANDIDATE_STATUS.md`, dated 2026-08-23. It identifies product `guideXOS Developer Studio 0.1.0`, release source baseline `main` at `715a29df535103d1d281c7181219b5f986b41f7c`, sign-off commit `bb66e11c38dffae21514384f73b1920b53ac64fe`, and paired Server commit `fb71e36c77866cdabf20873e6ad3de0ddc3bbe02`. The signed-off package was audited and staged twice, passed the recorded App Model launch and hosted workflows, and passed the recorded fast and required debugger validation tiers.

That record names the runtime package as `app.json` plus `bin/amd64/developerstudio.elf`, with App Model ID `com.guidexos.developerstudio`, kind `NativeElf`, architecture `amd64`, entry `gx_main`, ABI `guidexos-c-abi-v1`, and runtime `native-elf`. It records ELF entry `0x2998f0`, the size/hash above, and manifest SHA-256 `D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53`.

The signed-off ELF is still represented in the paired Server commit's Git tree as blob `150b4958e569ce4d4e5f9e2c3146de94d94c879d`. The file under the existing ESP has the same Git blob ID and the signed-off SHA-256. The ESP copy is useful corroborating evidence, but it is an old staged copy, not the current package source.

No release-candidate record or explicit approval for the current `F072…` artifact was found in the inspected DeveloperStudio release records. The current DeveloperStudio repository was clean at `main`, HEAD `38235aacbe718f0832cdc7a5dcc3364b6fd27050` (two commits ahead of its upstream); its Phase 25 status remains the explicit release sign-off found.

## 4. Root cause

The current package change is legitimate and committed, rather than a corrupted or accidentally selected build output. Server commit `81565ee227ed784862ecc04fc692cbd1904deef4`, dated 2026-10-02 and titled `Add Developer Studio document activation`, changed the tracked ELF from 1,015,064 to 1,104,652 bytes and updated `Apps/DeveloperStudio/app.json` in the same commit. The changed manifest adds `supportsDocumentActivation` and the C/C++/text file associations.

The later package revision has not been explicitly signed off as a release artifact. The 2026-08-26 pin in `build.ps1` still represents the last approved ELF. It is stale relative to the October package, but it is not an obsolete approval that can be silently replaced. The primary classification is **incomplete DeveloperStudio release qualification after a legitimate binary/manifest update**.

This is not a wrong-source-path issue or a build script overwriting the executable. The build gate reads the current tracked `Apps/DeveloperStudio` package. The earlier ESP and `out/wallpaper-pack` copies are stale relative to that source: both contain the old signed-off ELF and a 1,176-byte manifest with SHA-256 `7AF95D35F068C44F4529420922E0F4BEC5F705568514688D160E2790022ACB09`. The ESP build identity dates from 2026-08-27. A later `out/v03-amd64-scifi-final-candidate\qemu-fixture-1\ESP` copy instead matches the current source manifest and ELF hashes. These mixed historical copies do not change which source path the normal build validates.

## 5. Packaging and manifest analysis

The top-level build orders relevant stages as follows:

1. Build the PacMan package.
2. Build or reuse the wallpaper/runtime image; when rebuilt, `generate-wallpaper-pack.ps1` receives `Apps\PacMan` and `Apps\DeveloperStudio` and copies their package files into `out\wallpaper-pack\Apps` and `ESP\ramdisk.img`.
3. Build the UEFI bootloader and AMD64 kernel.
4. Stage App Model packages into `ESP\Apps`. For DeveloperStudio, the source is `Apps\DeveloperStudio`; the script validates its manifest, AMD64 ELF header, and signed-off size/hash before copying the package tree.

The wallpaper packer copies package files and does not modify the source ELF. `Stage-AppModelPackages` also copies files without rebuilding them. Its hash gate runs before that ESP package copy. In the preserved PMBUILD1 run, the gate therefore stopped ESP staging; the currently present ESP package is older output, not output from that failed copy attempt.

The current source manifest is 2,011 bytes, SHA-256 `AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9`. It declares:

- ID `com.guidexos.developerstudio` and kind `NativeElf`.
- AMD64 path `bin/amd64/developerstudio.elf`.
- Manifest entry point `gx_main`, ABI `guidexos-c-abi-v1`, runtime `native-elf`.
- `supportsDocumentActivation: true` and the post-October document associations.
- `minGuideXOSVersion: 0.5.0`; the Server loader reads this metadata, while the inspected manifest validator and build gate do not enforce a minimum-version comparison.
- No additional package resource paths. The exact package file allowlist is `app.json` and the AMD64 ELF; the icon is the symbolic key `app.developerstudio`.

The current source manifest passes the top-level manifest identity check. Its ELF passes the top-level ELF64/little-endian/AMD64 check. `readelf` confirms the current ELF is `ET_EXEC`, sectionless, and has no dynamic section (no `DT_NEEDED` dependencies). Its sections are stripped, so a post-hoc symbol-table lookup for `gx_main` is unavailable; the manifest names `gx_main`, and the inspected DeveloperStudio build recipe links with `-e gx_main` before stripping sections. This does not constitute release approval for the new bytes.

The current package tree contains exactly the two expected files. Existing later fixture staging matches the current source hashes byte-for-byte. No fresh staging was run in this phase because the artifact identity remains unresolved.

## 6. Corrective action and approval boundary

No source, manifest, hash pin, or generated package was changed.

Restoring only the old `5343…` ELF would pair it with the October manifest that advertises document activation and associations. Restoring the old manifest too would roll back that package capability. Neither action is an authorized fix for this phase. Updating the pin to `F072…` would authorize a new artifact without DeveloperStudio owner sign-off, which this task expressly prohibits.

The approval needed to continue is an explicit DeveloperStudio release qualification for the current package (or another approved package revision), including its exact manifest and ELF hashes and corresponding package/runtime test evidence. The current candidate identity to review is:

| Item | Current candidate |
| --- | --- |
| Package source | `Apps/DeveloperStudio` in the authorized Server checkout, from commit `81565ee2` |
| Manifest | 2,011 bytes; `AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9` |
| AMD64 ELF | 1,104,652 bytes; `F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6` |
| ELF shape | ELF64, little-endian AMD64 `ET_EXEC`; `e_entry=0x500be590`; no section or dynamic tables |
| Manifest contract | `com.guidexos.developerstudio`, `NativeElf`, `gx_main`, `guidexos-c-abi-v1`, `native-elf`, document activation enabled |

## 7. Hash verification evidence

A read-only reproduction of the package gate reported:

```text
manifest_validation=PASS
elf_header_validation=PASS
package_tree=PASS
actual_bytes=1104652
actual_sha256=F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6
expected_bytes=1015064
expected_sha256=5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7
signedoff_hash_gate=FAIL: DeveloperStudio signed-off ELF size/hash changed; refusing to stage it
```

The previous run's transcript and partial candidate files remain under `E:\bkup\artifacts\PMBUILD1-20261018\`. `PMBUILD1_PACMAN_AUDIO_LINK_REPAIR.md` records the exact same expected/actual identities and failure. Its transcript preserves the command and time; the validation record carries the detailed stage result.

## 8. DeveloperStudio package tests

The static App Model manifest, package-tree, and ELF-header checks for the current source package pass. The signed-off artifact identity check fails as shown above. The signed-off Phase 25 record supplies package audit, two staging passes, App Model launch, rebuild reproducibility, and hosted workflow evidence for the old `5343…` release.

No new DeveloperStudio runtime/presentation or App Model launch suite was run: the current `F072…` package has no explicit release sign-off, and the old signed-off binary cannot be substituted under the current document-activation manifest without a compatibility decision. The standalone DeveloperStudio package audit script requires both AMD64 and ARM64 payloads, while this Server package intentionally contains only its AMD64 entry; that two-architecture audit is not applicable to this package. No QEMU guest was started.

## 9. PMBUILD1 regression status

The prior normal AMD64 run recorded in [PMBUILD1](PMBUILD1_PACMAN_AUDIO_LINK_REPAIR.md) passed the PacMan package stage, including ELF `EEB544BBB2E2574EEAED7BB7F0BAD61EA24F6945231B7757223A225B30B8E1CC`, manifest `3F3592E6C76053E323F16D48073E98C67D7F5702B8E47F0B507D6BFB3D126449`, and all seven audio files. PMBUILD1 remains intact. PacMan packages and unrelated validation artifacts were not changed.

## 10. Top-level build result

The preserved prior `build.ps1 -Arch amd64` run passed PacMan, then built the bootloader and kernel and stopped at the DeveloperStudio signed-off ELF gate before ESP package staging. No full release image was produced. Its partial 64 MiB runtime-image candidate is recorded as SHA-256 `31A83A3263846ABEFFD890AD34DB482145E88DCA2BAF96837B1425CB1C8263A0` under `E:\bkup\artifacts\PMBUILD1-20261018\top-level-candidate`.

This phase did not rerun the normal build: the artifact issue was not resolved through an authorized approval or artifact restoration. No new unrelated blocker was observed, and no complete image or release qualification is claimed.

## 11. Additional blockers

- Owner approval and qualification for the post-October 2 DeveloperStudio package remain outstanding.
- Runtime App Model and native presentation tests for that exact package remain outstanding.
- No final AMD64 image exists from this phase.

## 12. Integration recommendation

Keep the exact SHA-256 gate. Do not stage the old ESP copy as a source and do not re-pin the current `F072…` hash without explicit DeveloperStudio release approval. After the owner signs off the exact package revision and manifest, rerun the focused package/runtime qualification, then rerun `build.ps1 -Arch amd64` and record the complete image-stage result. Until then DSBUILD1 remains **Outcome B**.
