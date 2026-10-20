# S22R2 DeveloperStudio release-contract decision packet

**Decision: Outcome C — blocked at the DeveloperStudio release-policy and compatibility boundary.**

The Phase 25 pair passes identity checks as an exact historical package. Its Phase 25 acceptance does not authorize use in Server v0.3, and the pair has a known functional and minimum-version mismatch with the current v0.3 runtime. The current source pair has no matching release approval. The release preflight correctly rejects that source pair. Under the S22R2 instructions, no package build or QEMU run may proceed until an authorized release contract is established.

## Repository and stale-prompt guard

- Repository: D:\dev\guideXOSServer.
- Branch: main, tracking origin/main.
- Starting HEAD: bce4145843a8c93a4b253116e374341b51f11655, matching the requested expected HEAD.
- S22R2 was not already accepted: the S22R2 evidence directory and report section were absent.
- Starting Git status: 0 staged paths, 0 tracked unstaged paths, and 853 pre-existing untracked paths. Those paths include the separate DeveloperStudio compatibility note, existing evidence, and build outputs; none was modified or staged.
- No newer commit superseded the request.
- The S22R diagnosis and integrity evidence are in docs/SETTINGS_S22_V03_PACKAGE_QUALIFICATION.md and docs/evidence/settings-s22r-v03-package-20261019/.

## Candidate identities

### Phase 25 signed-off pair

| Artifact | Bytes | SHA-256 | Source |
| --- | ---: | --- | --- |
| app.json | 1,112 | D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53B | Raw Apps/DeveloperStudio/app.json from paired Server commit fb71e36c77866cdabf20873e6ad3de0ddc3bbe02; matching test fixture |
| AMD64 ELF | 1,015,064 | 5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7 | Raw Apps/DeveloperStudio/bin/amd64/developerstudio.elf from that paired Server commit; matching existing ESP ELF |

The paired Server commit stores Git blob IDs f105a984555ffd0d399009355ec5ded7bc9d6b98 for the manifest and 150b4958e569ce4d4e5f9e2c3146de94d94c879d for the ELF. These are Git object identifiers, not SHA-256 values.

The original acceptance record is D:\dev\guideXOS_Developer_Studio\docs\RELEASE_CANDIDATE_STATUS.md, dated 2026-08-23. It records DeveloperStudio sign-off commit bb66e11c38dffae21514384f73b1920b53ac64fe, source baseline 715a29df535103d1d281c7181219b5f986b41f7c, and paired Server commit fb71e36c77866cdabf20873e6ad3de0ddc3bbe02 on v0.5_DEVELOPER_STUDIO. The rehearsal accepted that package for DeveloperStudio 0.1.0 after staged-pair checks, App Model discovery and launch, hosted document workflows, debugger workflows, CTest 27/27, and eight required debugger suites passing.

That record approves a limited Phase 25 release candidate for its paired Server v0.5 rehearsal. It does not state that the pair is approved for guideXOS Server v0.3. The exact manifest declares minGuideXOSVersion 0.5.0. The Phase 25 status text transcribed a 63-character manifest digest; the full 64-character value above was measured from the paired Server commit bytes and independently matches the 1,112-byte fixture.

### Current source pair

| Artifact | Bytes | SHA-256 | Status |
| --- | ---: | --- | --- |
| Apps/DeveloperStudio/app.json | 2,011 | AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9 | Newer source candidate; no matching release approval found |
| Apps/DeveloperStudio/bin/amd64/developerstudio.elf | 1,104,652 | F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6 | Newer source candidate; no matching release approval found |

The newer manifest enables document activation and declares nine source/text extensions. The Phase 25 manifest has no document associations or activation support. No approval for the current pair was found in the inspected Server or DeveloperStudio acceptance records. The Phase 25 sign-off does not approve intervening DeveloperStudio work.

### Existing ESP pair

The preserved ESP contains the Phase 25 ELF, 1,015,064 bytes with SHA-256 5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7, but a different 1,176-byte manifest with SHA-256 7AF95D35F068C44F4529420922E0F4BEC5F705568514688D160E2790022ACB09. This is a mismatched historical pair and is not a staging source for the new package.

## Server v0.3 compatibility

The Phase 25 manifest matches the loader's basic Native ELF identity contract: package ID com.guidexos.developerstudio, kind NativeElf, AMD64 entry, package-relative ELF path, gx_main entry point, guidexos-c-abi-v1, and native-elf runtime. The ELF is a static AMD64 ET_EXEC with no dynamic dependencies. The base x86-64 calling convention and pre-existing host-call offsets remain stable; current host-call entries were appended after the Phase 25 slots.

This establishes structural ABI compatibility only. The manifest's minimum version is 0.5.0, above this Server v0.3 target; the fact that the inspected loader does not enforce that field is not proof the version contract is satisfied. The v0.3 bare-metal host does not supply the workspace file operations or build_project, development_run, and development_debug services that Phase 25 DeveloperStudio expects. Its intended workspace, source editing, project creation, build, Run, and debugger workflows therefore cannot operate. A package discovery or window launch would not establish intended-product compatibility.

Selecting Phase 25 would also remove the current candidate's document activation support and nine source/text routes. The current server can boot without DeveloperStudio at runtime, but the release pipeline currently requires the package in staging, the wallpaper runtime image, build identity, and release manifest. Deferral is technically possible but requires a separately authorized package-inventory change and focused validation.

The read-only compatibility findings are documented in docs/validations/DSBUILD1R2_PHASE25_COMPATIBILITY.md, a pre-existing unstaged note preserved by this task. That note recommends deferral and states that Phase 25 inclusion requires explicit approval from both the DeveloperStudio and Server v0.3 release owners, resolution of the 0.5.0 minimum-version contract, and a runtime-owner decision about absent services. The earlier release-candidate assessment also says no owner approval was inferred.

## Preflight policy and result

scripts/release-build-integrity.psm1 pins the exact Phase 25 ELF and manifest identity. build.ps1 always checks Apps/DeveloperStudio against that pin before build cleanup; there is no separate selector that stages the approved pin independently from the current source tree. The S22R code labels the pin as signed off and records its provenance, while its explicit diagnosis requires owner authorization before a different package may be included. Identity pinning and Phase 25 acceptance do not, by themselves, establish v0.3 release authorization.

The preserved S22R preflight ran build.ps1 -Arch amd64 against the current source pair and exited 1 with the diagnostic that the source pair does not match the exact Phase 25 pair and owner acceptance is required. It stopped before replacing build outputs. The 13-assertion release-integrity fixture harness passed historically; its Phase 25 success fixture demonstrates exact identity validation in the tooling, not cross-release owner authorization. Its rejection of the newer unapproved pair remains correct.

No release-selection change was made. No negative test was changed to accept an unauthorized pair. The current unapproved candidate remains rejected.

## Required owner decision and smallest next action

No Server v0.3 release authorization for either exact pair was found. The Phase 25 pair also fails the intended-workflow compatibility gate for the current guest and declares a newer minimum Server version. The current pair is unapproved. The pipeline's mandatory-DeveloperStudio contract cannot be safely satisfied from the available evidence.

The next action is a written decision by the Server v0.3 release owner, with the DeveloperStudio release owner and runtime owner as required by the selected option:

1. **Recommended: authorize deferral of DeveloperStudio from Server v0.3.** Then implement a narrow explicit package-inventory path, keep exact identity checks whenever DeveloperStudio is included, test the remaining application packages and handlers, and build/verify a new package.
2. **Alternatively: authorize the exact Phase 25 pair only after resolving its 0.5.0 minimum-version declaration and the missing guest services.** Then package those exact hashes, run a current-Server launch and intended-scope smoke, and document the reduced feature contract.
3. **Alternatively: independently accept the current source pair** only after its own release provenance, compatibility, and acceptance gates pass.

Until that decision exists, do not select or stage either DeveloperStudio pair for a new Server v0.3 ISO.
