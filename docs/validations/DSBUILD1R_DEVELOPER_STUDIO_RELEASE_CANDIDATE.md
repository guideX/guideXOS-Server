# DSBUILD1R — DeveloperStudio v0.3 release-candidate qualification

**Outcome B — conditional.** The current package is a legitimate committed update, but exact build provenance and user-visible runtime behavior remain incomplete. No candidate correctness defect was demonstrated. The signed-off hash gate remains active; this phase changed no release artifact, manifest, pin, or source code.

**Candidate approval readiness:** No. Do not classify this pair as CANDIDATE_READY_FOR_OWNER_SIGNOFF.

**Recommendation:** Option B conditionally: retain the Phase 25 feature scope with its exact signed-off ELF and manifest, subject to owner acceptance of that reduced scope and a bounded current-Server launch/close smoke. This phase did not restore or substitute that pair. Reconcile the build-script manifest-pin discrepancy before a release build.

## 1. Repository and preservation state

- Checkout D:\dev\guideXOSServer; starting and ending branch main; starting and ending HEAD 0513a65643f98b8c78ef709dfa7d6ea771677517.
- Initial census: tracked files clean; 851 untracked paths.
- Final census: six tracked paths are modified and 855 untracked paths, including this report. Modified paths are build.ps1, kernel/core/desktop.cpp, kernel/core/desktop_gradient_persistence.cpp, kernel/core/include/kernel/desktop_gradient_persistence.h, scripts/create-release-iso.ps1, and tests/settings_native_runtime_test.cpp. These changes were not made by this qualification and were preserved. The tracked modifications appeared after the initial clean census while work was underway.
- The current build.ps1 working diff retains the expected ELF size/hash check and adds a manifest pin of 1,176 bytes / 7AF95D35F068C44F4529420922E0F4BEC5F705568514688D160E2790022ACB09. That is the existing ESP manifest, not the exact Phase 25 signed-off manifest (1,112 bytes / D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53). The modified scripts/create-release-iso.ps1 now labels the package as Phase 25 signed-off and requires its manifest/ELF records, but the working-tree manifest pin does not match the recorded Phase 25 manifest. No release image was built to validate these changes. The working-tree edits were not made by this qualification. No top-level build was run.
- No branch/history/worktree operations, push, reset, stash, cleanup, or package restoration occurred. FONT_SUPPORT, PacMan, and DeveloperStudio source checkouts were not modified. DeveloperStudio source was inspected read-only.
- Disk capacity was checked before scratch work: D: about 11.25 GB free; E: about 6.9 TB free. Scratch stayed under D:\dev\artifacts\DSBUILD1R-20261019\ and E:\bkup\artifacts\DSBUILD1R-20261019\.

## 2. Artifact identities and package integrity

| Item | Identity | Format and package details |
| --- | --- | --- |
| Phase 25 signed-off ELF | 1,015,064 bytes; SHA-256 5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7 | ELF64 little-endian AMD64 ET_EXEC; entry 0x2998f0; .symtab present; no dynamic dependencies |
| Phase 25 signed-off manifest | 1,112 bytes; SHA-256 D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53 | Exact manifest from paired Server commit fb71e36c77866cdabf20873e6ad3de0ddc3bbe02; NativeElf, amd64, gx_main, guidexos-c-abi-v1, native-elf; no document associations |
| Current candidate ELF | 1,104,652 bytes; SHA-256 F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6 | ELF64 little-endian AMD64 ET_EXEC; entry 0x500be590; sectionless; no DT_NEEDED libraries |
| Current candidate manifest | 2,011 bytes; SHA-256 AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9 | ID com.guidexos.developerstudio; gx_main; guidexos-c-abi-v1; native-elf; activation enabled; nine C/C++/text associations |

The current Apps\DeveloperStudio package contains exactly app.json and bin/amd64/developerstudio.elf. It declares no extra resource files; app.developerstudio is a symbolic icon key. Host dependencies are ABI calls and manifest permissions log, window, draw, filesystem.read, and filesystem.write.

The ESP contains the old ELF but a different 1,176-byte manifest (SHA-256 7AF95D35F068C44F4529420922E0F4BEC5F705568514688D160E2790022ACB09). Do not treat that as the exact Phase 25 pair or combine the old ELF with the current manifest.

llvm-readobj confirmed the candidate has five program headers, no section table, and no needed libraries. Its stripped form has no symbol table for post-build gx_main lookup; the manifest names gx_main and the inspected link recipe uses -e gx_main. A separate undefined-symbol table audit is unavailable after stripping. Static linking and no dynamic table/needed libraries show no dynamic runtime dependencies.

Current Server SDK appends get_document_activation_path at host-call offset 248; the table is 256 bytes. Older slots remain in order, which supports structural ABI compatibility for the old app but is not a current-Server runtime test.

## 3. Changes since Phase 25 and regression-risk matrix

Phase 25 sign-off: DeveloperStudio baseline 715a29df535103d1d281c7181219b5f986b41f7c, sign-off commit bb66e11c38dffae21514384f73b1920b53ac64fe, paired Server commit fb71e36c77866cdabf20873e6ad3de0ddc3bbe02, dated 2026-08-23. Current package update: Server commit 81565ee227ed784862ecc04fc692cbd1904deef4, dated 2026-10-02. Contemporaneous source candidate: DeveloperStudio commit 18c093dcb9c4fc0daf5a019d82283aba0ac319a4. This source/package pairing is reconstructed from history; the binary has no immutable build identity.

Since Phase 25, source history includes Phase 27 freestanding compiler/Run work (control flow, functions, recursion, multiple files, globals, arrays, pointers, structs); Phase 28 native debugger startup/lifecycle work; Phase 29 transactional project loading, manifest identity, hosted debugger lifecycle, variables/watches; and document activation. These changes exceed the document-activation commit scope. Later Phase 30 source work postdates this ELF and is not attributed to it. Phase 25 sign-off does not qualify intervening changes.

| Area | Evidence | Remaining risk |
| --- | --- | --- |
| Document activation | Manifest lists .c, .cc, .cpp, .cxx, .h, .hh, .hpp, .hxx, .txt. Source uses a bounded owned path and standalone document loader; Server resolver/dispatcher tests pass. | Candidate guest did not launch; valid/missing/invalid activation, dirty close, and repeated activation remain untested. |
| Native ABI | Activation callback appended at offset 248; current size 256. Candidate source checks the callback. | Server ABI test still asserts 248 and fails to compile. It was not weakened or changed. |
| Window/focus/lifecycle | Earlier Phase 25 launch workflows are signed off; later source extends NativeElf/debugger lifecycle. | Candidate window ownership, focus, close, and repeated launch not reached. |
| Project creation/loading | Candidate source includes later project and transactional-load work. | Current source CTest did not complete cleanly; no candidate guest project workflow. |
| Startup/shutdown/input | Guest booted; package was discovered; keyboard toggled Start menu. | No app process/window appeared; app startup, shutdown, focus, and recovery remain unqualified. |
| Manifest/filesystem | Current manifest and ELF hashes match the committed package; exact tree has two files and bounded host filesystem permissions. | No guest open/save or external filesystem-dependency check. |
| Error/recovery | Source docs and model tests cover invalid/missing project and document paths. | Candidate-level invalid/missing input and recovery not exercised. |
| Post-Phase-25 acceptance | Many source-model tests pass. | Source validation stopped at 30/33; later phases are not presumed accepted. |

## 4. Build provenance and reproduction

- Server commit 81565ee227ed784862ecc04fc692cbd1904deef4 contains the exact current package bytes and manifest. DeveloperStudio commit 18c093dcb9c4fc0daf5a019d82283aba0ac319a4 is the contemporaneous source snapshot and adds document activation. Source repo was clean at main, HEAD 38235aacbe718f0832cdc7a5dcc3364b6fd27050. An exact source archive and Server SDK snapshot were tested in scratch.
- Build recipe defaults to Debug, but no immutable record ties this ELF to a specific configuration, full input list, or compiler invocation. Package has no build identity for source commit 18c093d.
- Toolchain observed: Clang/LLD 22.1.8, LLVM objcopy 22.1.8, MinGW g++ 15.2.0 for host tests. Recipe uses --target=x86_64-unknown-elf, C++11, -ffreestanding, no exceptions/RTTI/stack protector/unwind tables, GXOS_DEVELOPER_STUDIO_BARE_METAL, and AMD64 -mno-red-zone, with Server SDK and src includes.
- Link recipe: static LLD elf_x86_64, image base 0x50000000, 4 KiB max page, entry gx_main; then strip ELF sections. It links 34 objects; runtime dependencies are host ABI calls.
- Reproduction stopped before packaging. Direct build stopped on a missing generated debugger fixture. Fast validation generated fixtures in scratch and reached CTest: 30/33 passed; three Phase 10 DWARF tests use hard-coded D:/dev/guideXOS_Developer_Studio paths while the archive was under the artifact scratch path. No tests were skipped or weakened.
- No byte-for-byte rebuild/comparison exists. Validation did not reach packaging and the binary lacks immutable compiler/configuration/input provenance. Package history strongly associates the current bytes with the source snapshot, but does not prove reproducibility.

## 5. Native package and App Model test results

| Validation | Result |
| --- | --- |
| App Model file activation | PASS 60/60 checks |
| Built-in document dispatcher | PASS 21/21 checks; 100-cycle lifecycle runs passed for each tested built-in route |
| Default-handler persistence/resolution | PASS 54/54; 100 persistence reloads, 100 ordinary Open dispatches, 100 Open With dispatches |
| Native ABI layout | FAIL at compile time: stale assertion expects sizeof(gx_host_calls)==248; current header evaluates to 256 |
| DeveloperStudio source fast tier | 30/33 CTests pass; three snapshot-path failures described above |

Logs: E:\bkup\artifacts\DSBUILD1R-20261019\server-appmodel-tests.log, fast-validation-rerun.log, candidate-reproduction-build-rerun.log, and fixture-build logs. These test App Model behavior and source models, not execution of the current packaged ELF. No current-candidate hosted presentation test completed. The production gate was not bypassed; normal AMD64 top-level build was not run.

## 6. Guest qualification

Ownership census came first. Unrelated QEMU PID 1872 used QMP 127.0.0.1:50620 and an external task-manager fixture; it was not contacted or stopped.

An isolated guest used private firmware, ESP copy, serial, debug log, display capture, and QMP at 127.0.0.1:55997. It booted an October 5 development fixture with kernel hash 1C0FA432…; this was not the current release image. The private ESP had the verified current candidate pair. Serial shows /Apps/DeveloperStudio/app.json opened and package ID com.guidexos.developerstudio discovered with ABI guidexos-c-abi-v1.

Desktop rendered; keyboard toggled Start menu. Bounded QMP/HMP input attempts did not select or launch DeveloperStudio. No app window appeared, so launch, document activation, focus, close, project workflows, and repeated cycles were not accepted. The attempt stopped at the input blocker as directed. Our QEMU process was closed; PID 1872 remained untouched.

Evidence: E:\bkup\artifacts\DSBUILD1R-20261019\guest, including candidate-guest.serial.log, QEMU debug output, and boot/menu PNG captures.

## 7. Remaining acceptance gaps

1. Tie candidate ELF to immutable source, build configuration, complete toolchain/input record, and successful reproducible build.
2. Complete the fast source tier with fixture paths that work in an archived snapshot; retain every assertion.
3. Bring the ABI layout test up to the production append-only contract and pass it without weakening checks.
4. Run a current-Server guest launch/close smoke for the owner-selected exact package pair.
5. For the candidate, capture guest evidence for valid/missing/invalid document activation, focus/window ownership, project startup, close, and repeated launch/close.
6. Reconcile Phase 25 manifest D59…F53 with the current working-tree build.ps1 pin 7AF…B09 before Option B; do not misidentify the ESP manifest.
7. No complete v0.3 image was built; the current candidate still differs from the expected ELF pin.

## 8. Release options

| Option | Assessment |
| --- | --- |
| A — Approve candidate | Not recommended. Pair is ELF F07270…01A6 + manifest AB1DB7…488E9. Static/package checks are positive, but source reproduction stopped before packaging, three source CTests remain unresolved in the snapshot, ABI layout test fails, and guest did not launch the app. Not ready for owner sign-off. |
| B — Retain Phase 25 artifact | Conditional recommendation. Exact signed-off pair is ELF 5343F5…CAC7 + manifest D59C12…F53 from paired Server commit fb71e36…. Same ABI v1 and package ID; current host-call changes append after old slots and no structural break was found. Current-Server launch was not tested. Loses document activation and C/C++ source associations. Existing ESP manifest 7AF95…ACB09 is a different file and is not the Phase 25 manifest. |
| C — Defer from v0.3 | Technically feasible but unimplemented/unvalidated. Desktop Start menu discovers validated packages; built-ins register separately. DeveloperStudio is hard-required in release staging and wallpaper-image verification. A future explicit package inventory must omit it from staging/image checks, preserve the hash gate for any path that stages DeveloperStudio, update package/build-identity validation, and verify startup, other built-ins, and handlers. Its nine associations disappear; Notepad text defaults remain. |

## 9. Owner decision and blockers

Recommended owner decision: authorize the reduced Phase 25 scope with the exact pair 5343…CAC7 + D59…F53, then require one bounded current-Server launch/close smoke. Reconcile the current build.ps1 manifest-pin discrepancy through the owner workflow; this phase did not change it.

If retaining the activation candidate, the DeveloperStudio release owner must explicitly approve the exact current manifest and ELF identities after closing the provenance and acceptance gaps. Do not change the hash pin as part of this qualification. If neither pair is accepted, authorize Option C and package-inventory work as a separate phase.

No owner approval was inferred, no artifact or pin was changed, and Outcome B findings were preserved without a commit.

## 10. Evidence references

- Prior Server qualification: docs/validations/DSBUILD1_DEVELOPER_STUDIO_ARTIFACT_QUALIFICATION.md
- Accepted PacMan evidence: docs/validations/PMBUILD1_PACMAN_AUDIO_LINK_REPAIR.md
- Phase 25 sign-off: D:\dev\guideXOS_Developer_Studio\docs\RELEASE_CANDIDATE_STATUS.md
- Phase 25 paired package: Server commit fb71e36c77866cdabf20873e6ad3de0ddc3bbe02
- Current package update: Server commit 81565ee227ed784862ecc04fc692cbd1904deef4
- Candidate source snapshot: DeveloperStudio commit 18c093dcb9c4fc0daf5a019d82283aba0ac319a4
- Scratch logs and guest evidence: E:\bkup\artifacts\DSBUILD1R-20261019