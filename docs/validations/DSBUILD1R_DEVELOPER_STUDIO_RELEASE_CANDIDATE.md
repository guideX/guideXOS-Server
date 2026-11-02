# DSBUILD1R — DeveloperStudio v0.3 release-candidate qualification

**Outcome B — conditional.** The current package is a legitimate committed update, but its exact build provenance and user-visible runtime behavior are not sufficiently qualified for owner approval. No correctness defect in the candidate was demonstrated. The signed-off hash gate remains active; this phase changed no release artifact, manifest, pin, or source code.

**Candidate approval readiness:** No. Do not classify the current pair as CANDIDATE_READY_FOR_OWNER_SIGNOFF.

**Recommendation:** Option B, conditionally: keep the Phase 25 feature scope for v0.3 using the exact signed-off ELF and its exact signed-off manifest, after the release owner accepts that reduced scope and the pair passes a bounded current-Server launch/close smoke. This phase did not restore or substitute that pair. The package identity discrepancy in the current working-tree build script must be reconciled before a release build.

## 1. Repository and preservation state

- Checkout: D:\dev\guideXOSServer; starting and ending branch: main.
- Starting and ending HEAD: 0513a65643f98b8c78ef709dfa7d6ea771677517.
- At the initial census, tracked files were clean and 851 untracked paths were present.
- At the final census before adding this report, five tracked paths were modified and 854 untracked paths were present. The modified paths are build.ps1, kernel/core/desktop.cpp, kernel/core/desktop_gradient_persistence.cpp, kernel/core/include/kernel/desktop_gradient_persistence.h, and tests/settings_native_runtime_test.cpp. They were not edited by this qualification and were left untouched. Adding this report brings the untracked count to 855, assuming no concurrent changes.
- The current build.ps1 working-tree diff retains the expected ELF size/hash check and adds an expected DeveloperStudio manifest pin of 1,176 bytes / 7AF95D35F068C44F4529420922E0F4BEC5F705568514688D160E2790022ACB09. That hash is the existing ESP manifest, not the exact Phase 25 signed-off manifest (D59C…F53, 1,112 bytes). The diff appeared after the initial tracked-clean census; it was not made by this qualification. No top-level build was run.
- No branch, history, reset, stash, worktree, merge, rebase, push, or cleanup operation was performed. No files were changed in D:\dev\guideXOSServer_FONT_SUPPORT, the PacMan checkout, or the DeveloperStudio source checkout. DeveloperStudio source was inspected read-only.
- Disk capacity was checked before scratch work: D: had about 11.25 GB free and E: about 6.9 TB free. Scratch source, SDK, builds, guest files, logs, and screenshots were kept outside the checkouts under D:\dev\artifacts\DSBUILD1R-20261019\ and E:\bkup\artifacts\DSBUILD1R-20261019\.

## 2. Artifact identities and package integrity

| Item | Exact identity | ELF/package details |
| --- | --- | --- |
| Phase 25 signed-off ELF | 1,015,064 bytes; SHA-256 5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7 | ELF64, little-endian AMD64, ET_EXEC, entry 0x2998f0; .symtab present; no dynamic dependencies |
| Phase 25 signed-off manifest | 1,112 bytes; SHA-256 D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53 | Exact manifest read from paired Server commit fb71e36c77866cdabf20873e6ad3de0ddc3bbe02; NativeElf, amd64, gx_main, guidexos-c-abi-v1, native-elf; no document associations |
| Current candidate ELF | 1,104,652 bytes; SHA-256 F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6 | ELF64, little-endian AMD64, ET_EXEC, entry 0x500be590; sectionless; no DT_NEEDED libraries |
| Current candidate manifest | 2,011 bytes; SHA-256 AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9 | ID com.guidexos.developerstudio; gx_main; ABI guidexos-c-abi-v1; native-elf; document activation enabled; nine C/C++/text associations |

The current candidate package at Apps\DeveloperStudio contains exactly app.json and bin/amd64/developerstudio.elf. It declares no extra package resource files; app.developerstudio is a symbolic icon key. Its host dependencies are the Native ELF ABI and the manifest permissions log, window, draw, filesystem.read, and filesystem.write.

The existing ESP has the old signed-off ELF but a 1,176-byte manifest with SHA-256 7AF95D35F068C44F4529420922E0F4BEC5F705568514688D160E2790022ACB09. This is not the exact manifest in the Phase 25 sign-off. Do not describe the ESP pair as the exact signed-off package, and do not pair the old ELF with the current candidate manifest.

llvm-readobj confirmed the candidate is a five-program-header ELF with no section table and no needed libraries. Since the candidate has no symbol table, post-build symbol lookup for gx_main is unavailable; the manifest names gx_main, the ELF entry is present, and the inspected link recipe passes -e gx_main. The stripped ELF does not permit an independent undefined-symbol table audit. The static link and absence of a dynamic table/needed libraries show no dynamic runtime dependencies.

Both package pairs use the same manifest ABI identifier. The current Server SDK appends get_document_activation_path at host-call offset 248, producing a 256-byte table. The older host-call slots remain in order. This is structural evidence of an append-only ABI extension, not a current-Server runtime test of the old app.

## 3. Changes since Phase 25 and regression risks

The explicit Phase 25 sign-off is dated 2026-08-23, with DeveloperStudio baseline 715a29df535103d1d281c7181219b5f986b41f7c, sign-off commit bb66e11c38dffae21514384f73b1920b53ac64fe, and paired Server commit fb71e36c77866cdabf20873e6ad3de0ddc3bbe02. The current package change is Server commit 81565ee227ed784862ecc04fc692cbd1904deef4 (2026-10-02), which updates the ELF and manifest together. The contemporaneous source candidate is DeveloperStudio commit 18c093dcb9c4fc0daf5a019d82283aba0ac319a4. The source and package commits are a reconstructed pairing; the binary does not carry an immutable source/build identity.

The source interval after Phase 25 includes the Phase 27 freestanding compiler and Run work (control flow, functions, recursion, multiple files, global data, arrays, pointers, and structs); Phase 28 native debugger startup and lifecycle work; Phase 29 project-loading ownership, manifest identity, hosted debugger lifecycle, and variable/watch work; and the final document-activation change. These changes are broader than the feature named in the package commit. Later Phase 30 source work in the current source repository postdates the candidate source commit and is not attributed to this ELF. Phase 25 sign-off does not accept the intervening changes.

| Area | Change / evidence | Remaining risk |
| --- | --- | --- |
| Document activation | Manifest advertises .c, .cc, .cpp, .cxx, .h, .hh, .hpp, .hxx, and .txt. The app asks for a bounded owned activation path and opens it as a standalone document. Server resolver/dispatcher tests pass. | Candidate did not launch in the guest; valid, missing, invalid, dirty-close, and repeated activation were not exercised end to end. |
| Native host calls | New activation callback is appended at offset 248; current table size is 256. Candidate source checks for the callback. | Server ABI layout script still asserts size 248 and fails at compile time. The script was not altered or weakened. |
| Window lifecycle and focus | Phase 25 records earlier launch and workflow acceptance; later source phases extend NativeElf/debugger lifecycle. | No candidate window, ownership, focus-transfer, close, or repeated-launch behavior was reached in this guest. |
| Project creation/loading | Candidate source includes later project and transactional-load work. | Current source CTest did not complete cleanly; no current candidate guest project workflow was run. |
| Startup/shutdown | Isolated guest booted and discovered the package. | No DeveloperStudio process was launched, so its startup, shutdown, and recovery remain unqualified. |
| Input | Keyboard input toggled the Start menu; the menu and desktop rendered. | Attempts to select/launch DeveloperStudio with QMP mouse and keyboard did not reach the app. This is the bounded guest blocker, not evidence of an app defect. |
| Manifest and package | Candidate manifest and ELF hashes match the committed current package; exact package tree has two files. | Candidate differs from the signed-off ELF and manifest. The current working-tree manifest pin instead names the older ESP manifest, not the exact Phase 25 manifest. |
| Filesystem | Manifest requests bounded host filesystem read/write services; document activation reuses the existing document loader. | No guest open/save or external-dependency check was completed. |
| Error and recovery paths | Source docs and model tests describe invalid/missing project/document handling. | Candidate-level invalid/missing input and recovery behavior were not run. |
| Later acceptance | Source includes features and changes through commit 18c093d; several source-only tests passed. | No later source phase is automatically treated as accepted; the source validation tier ended at 30/33, and real-guest acceptance is incomplete. |

## 4. Build provenance and reproduction

- Server commit 81565ee227ed784862ecc04fc692cbd1904deef4 contains the exact current package bytes and manifest. DeveloperStudio commit 18c093dcb9c4fc0daf5a019d82283aba0ac319a4 is the contemporaneous source snapshot and adds document activation. The source repo was inspected at clean main, HEAD 38235aacbe718f0832cdc7a5dcc3364b6fd27050; an exact 18c093d archive was used for scratch validation.
- The candidate build recipe defaults to Debug, but no immutable record ties this ELF to a particular configuration, full input list, or compiler invocation. The package itself contains no build identity tying bytes to source commit 18c093d.
- Toolchain observed on this host: Clang and LLD 22.1.8, LLVM objcopy 22.1.8, and MinGW g++ 15.2.0 for host tests. The freestanding source recipe uses --target=x86_64-unknown-elf, C++11, -ffreestanding, disables exceptions, RTTI, stack protector, and unwind tables, defines GXOS_DEVELOPER_STUDIO_BARE_METAL, and uses -mno-red-zone for AMD64. It includes the Server SDK and src.
- The inspected link command is static LLD for elf_x86_64, image base 0x50000000, 4 KiB maximum page size, and entry gx_main; the script then strips ELF sections. It links 34 object files. Runtime dependencies are the ABI host calls rather than shared libraries.
- Candidate source archive and the Server SDK snapshot were placed outside both repositories under D:\dev\artifacts\DSBUILD1R-20261019\. Reproduction did not reach the package build: the direct build script stopped on a missing generated debugger fixture. The fast validation tier built the fixtures in scratch and then reached CTest, where 30/33 passed and three Phase 10 DWARF tests failed because their assertions use the original absolute checkout path D:/dev/guideXOS_Developer_Studio/... while the source was under the artifact snapshot. No test was skipped or weakened.
- No byte-for-byte candidate rebuild or comparison was produced. The exact limitation is that validation stopped before packaging, and the binary has no immutable record of compiler version, configuration, or complete build inputs. The source/Server commit relationship is strong package-history evidence, but not reproducible-build evidence.

## 5. Native package and App Model validation

Server-side focused results were run with outputs directed to scratch:

| Validation | Result |
| --- | --- |
| App Model file activation | PASS, 60/60 checks: manifest associations, capability validation, path bounds, routing, and explicit activation model |
| Built-in document dispatcher | PASS, 21/21 checks; 100/100 lifecycle cycles for each tested built-in route |
| Default-handler persistence/resolution | PASS, 54/54 checks; 100 persistence reloads, 100 ordinary Open dispatches, and 100 one-time Open With dispatches |
| Native ABI layout | FAIL at compile time: stale assertion expects sizeof(gx_host_calls) == 248; current header evaluates to 256 with the appended activation callback |
| DeveloperStudio source fast tier | 30/33 CTest cases passed. Three Phase 10 DWARF tests failed on the hard-coded original checkout path in the detached snapshot. No product failure was established, but the tier did not pass. |

Logs are preserved at E:\bkup\artifacts\DSBUILD1R-20261019\server-appmodel-tests.log and E:\bkup\artifacts\DSBUILD1R-20261019\fast-validation-rerun.log. The direct candidate-source build attempt and fixture-build logs are in the same evidence directory. DeveloperStudio source unit tests reached the missing-fixture stop before packaging.

These results validate App Model behavior and DeveloperStudio source models; they do not execute the current packaged ELF. No current-candidate hosted presentation test completed. The production package gate was not bypassed, and the normal AMD64 top-level build was intentionally not run.

## 6. Guest qualification

An ownership census preceded QEMU launch. An unrelated QEMU process, PID 1872, was using QMP port 50620 and an external task-manager fixture. It was not contacted or terminated.

The isolated guest used private firmware, ESP copy, serial output, debug log, display capture, and QMP at loopback port 55997. The guest booted a development fixture dated 2026-10-05 with kernel hash 1C0FA432…; it is not the current release image. Its private ESP contained the current candidate ELF and manifest at the verified hashes. Serial evidence shows the kernel opened /Apps/DeveloperStudio/app.json and discovered package ID com.guidexos.developerstudio with ABI guidexos-c-abi-v1.

The desktop rendered and keyboard input toggled the Start menu. A bounded set of QMP/HMP mouse and keyboard attempts did not select and launch DeveloperStudio. No application window appeared. Therefore launch, document activation, focus, close, project workflows, and repeated cycles were not accepted. The attempt stopped at this input blocker as directed. The isolated QEMU process was closed; PID 1872 remained untouched.

Evidence is preserved under E:\bkup\artifacts\DSBUILD1R-20261019\guest\, including candidate-guest.serial.log, QEMU debug output, and the boot/menu PNG captures.

## 7. Remaining acceptance gaps

1. Tie the current ELF to an immutable source revision, build configuration, complete toolchain/input record, and successful reproducible build.
2. Complete the DeveloperStudio fast source tier after making its fixture paths work in an archived source snapshot; retain all checks.
3. Update the native ABI layout test to reflect the production append-only callback contract, then pass the test without reducing its assertions.
4. Execute a bounded current-Server guest launch/close test for whichever exact package pair the owner selects.
5. For the current candidate, add actual guest evidence for valid and missing/invalid document activation, focus/window ownership, project startup, close, and repeated launch/close.
6. Reconcile the exact Phase 25 manifest hash (D59…F53) with the current working-tree build-script manifest pin (7AF…B09) before using Option B. Do not label the existing ESP manifest as the exact Phase 25 manifest.
7. Do not claim that a complete v0.3 image passed: no top-level build was run, and the current candidate still differs from the existing ELF pin.

## 8. Release options

| Option | Assessment |
| --- | --- |
| A — Approve current candidate | Not recommended now. The exact pair is ELF F07270…01A6 plus manifest AB1DB7…488E9. Package shape, manifest, and App Model unit coverage are positive, but source reproduction stopped before packaging, three source CTests remain unresolved in the snapshot environment, the ABI layout test fails, and the guest did not launch the app. It is not ready for owner sign-off. |
| B — Retain Phase 25 artifact | Conditional recommendation. The exact signed-off pair is ELF 5343F5…CAC7 plus the Phase 25 manifest D59C12…F53, both present in paired Server commit fb71e36…. It uses the same ABI v1 and required package identity; current host-call changes are appended after the old slots, and no breaking change was found by inspection. Full launch compatibility with the current Server was not tested. This option drops document activation and C/C++ source associations. The existing ESP manifest (7AF95…ACB09) is a different file and must not be substituted as the exact Phase 25 manifest. |
| C — Defer DeveloperStudio from v0.3 | Technically feasible, not implemented or release-validated. Desktop Start menu entries are discovered from validated packages and built-in applications are registered separately. The package is nevertheless hard-required by current release staging and wallpaper-image verification. A future explicit package inventory would need to omit it in all staging and image checks, preserve the hash gate for any path that stages DeveloperStudio, update package/build-identity validation, and test desktop startup plus the remaining built-in apps and handlers. DeveloperStudio’s nine associations would disappear; Notepad’s existing text defaults remain. |

## 9. Owner decision and release blockers

The release owner must choose the exact v0.3 package scope:

1. **Recommended:** authorize the reduced Phase 25 DeveloperStudio feature set using the exact signed-off ELF and exact signed-off manifest (5343…CAC7 + D59…F53), then require one current-Server guest launch/close smoke before release. Resolve the current build.ps1 manifest-pin discrepancy through the normal owner workflow; this phase made no change to it.
2. If retaining the document-activation candidate, explicitly approve the exact current manifest and ELF hashes only after the provenance and acceptance gaps above are closed. Do not update a hash pin as part of this qualification.
3. If neither pair is accepted for v0.3, authorize Option C and its package-inventory validation work as a separate implementation phase.

No owner approval was inferred. No artifact or pin was changed. Outcome B findings were preserved without committing, as required for this phase.

## 10. Evidence references

- Prior Server qualification: docs/validations/DSBUILD1_DEVELOPER_STUDIO_ARTIFACT_QUALIFICATION.md
- Accepted PacMan evidence: docs/validations/PMBUILD1_PACMAN_AUDIO_LINK_REPAIR.md
- Phase 25 source sign-off: D:\dev\guideXOS_Developer_Studio\docs\RELEASE_CANDIDATE_STATUS.md
- Phase 25 paired package: Server commit fb71e36c77866cdabf20873e6ad3de0ddc3bbe02
- Current candidate package update: Server commit 81565ee227ed784862ecc04fc692cbd1904deef4
- Current candidate source snapshot: DeveloperStudio commit 18c093dcb9c4fc0daf5a019d82283aba0ac319a4
- Scratch test and guest logs/screenshots: E:\bkup\artifacts\DSBUILD1R-20261019\

