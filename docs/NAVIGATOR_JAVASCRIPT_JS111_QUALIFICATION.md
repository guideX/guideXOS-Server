# Navigator JavaScript JS111 Qualification

**Date:** 2026-11-07
**Outcome C — `JS111_GUEST_BLOCKED`**

## Disposition

JS111 did not reach guest qualification. The blocker is the current guest build architecture: the accepted JavaScript runtime is compiled into the Windows-hosted Navigator executable by `build.bat`, while the freestanding kernel build compiles a separate, capability-limited Navigator adapter. The kernel build source list does not include `navigator_javascript`, the package contains no current bootable kernel/EFI set, and the documented path for loading `guideXOSServer` from the ramdisk still requires an ELF loader. There is therefore no source-matched guest candidate that can execute the JS109–JS110 runtime.

QAC was online and its read-only preflight showed no reservations, managed QEMU, foreign QEMU, or queued work. QAC availability does not solve the missing guest integration. No QAC reservation was requested and no QEMU was launched. This is `JS111_GUEST_BLOCKED`, not `JS111_DEFERRED_QAC_BUSY`.

No JSON implementation or guest packaging changes were made. This report records the blocker and the hosted/native regression evidence without claiming guest behavior.

## Repository and artifact state

| Item | Verified state |
|---|---|
| Repository | `D:\dev\guideXOSServer_NAVIGATOR_JS_SUPPORT` |
| Branch | `NAVIGATOR_JAVASCRIPT_SUPPORT` |
| Starting HEAD | `35cab1a95562744df36ed10f24567611234f792e` (JS110) |
| Starting tracked worktree | Clean; no untracked changes observed |
| Cached upstream divergence at preflight | 0 ahead / 0 behind (`HEAD...origin/NAVIGATOR_JAVASCRIPT_SUPPORT`) |
| `.phase` | Absent at preflight and after hosted qualification |
| Evidence run | `E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS111\run-20261107-065404-8f5e6110` |

The pasted baseline described the branch as two commits ahead of its cached upstream. The local preflight instead found the cached upstream at the starting HEAD. No fetch, branch change, reset, merge, rebase, push, or destructive cleanup was performed.

## Guest source-to-binary audit

- `build.bat` compiles `navigator.cpp` together with `navigator_javascript/ast.cpp`, `value.cpp`, `host.cpp`, `environment.cpp`, `runtime.cpp`, `lexer.cpp`, `parser.cpp`, and `navigator_script_host.cpp`. This produces the Windows-hosted Navigator executable.
- `kernel/Makefile` builds `kernel.elf` from `kernel/core/*.cpp` and architecture sources using C++14, `-ffreestanding`, `-nostdlib`, `-nostdinc++`, `-fno-exceptions`, and `-fno-rtti`. Its target source list does not include `navigator_javascript` or the hosted `navigator.cpp` implementation.
- `kernel/core/include/kernel/kernel_apps.h` describes bare-metal Navigator as a capability-limited adapter and identifies the hosted/compositor Navigator as the authoritative full implementation. `kernel/core/kernel_apps.cpp` does not provide the JSON JavaScript runtime.
- `docs/ARCHITECTURE.md` says loading `guideXOSServer` from the ramdisk is still pending implementation of the ELF loader.
- `ESP/build-identity.txt` identifies an older kernel source path under `D:\dev\guideXOSServerV0.2`. The current `ESP` directory has a ramdisk image but no current `kernel.elf` or `EFI/BOOT/BOOTX64.EFI`; there is no complete current boot package to hash and boot.

These are different implementations and build targets, not two packaging routes to the same runtime. The Windows `build.bat` executable cannot establish JS110 behavior in the guest kernel. A guest compile, boot package, or QEMU attempt would not provide source-matched JSON evidence, so none was attempted. Guest input, Navigator loading, rendering, serial diagnostics, and shutdown could not be exercised for this runtime.

## QAC admission

The documented QAC read-only commands (`status`, `list`, and `queue`) were used during preflight. The coordinator was online; its census showed 0 QEMU, 0 foreign, 0 managed, 0 queued, and 0 blocked sessions. No request, reservation, launch, cancellation, or release occurred.

The final read-only census at `2026-11-07T17:11:56Z` found one QAC-managed QEMU acceptance session: project `guideXOSServer-AARCH64-ABI3-Studio`, reservation `929f92fe62db4451a5b8988c85e64d94`, owner verified, QEMU PID 22964. It was not created by this qualification and was left untouched. QAC reported no foreign QEMU and no queued reservation. JS111 is not classified as QAC-busy: the source/build architecture prevents producing a valid guest candidate regardless of lane availability. The census files are stored under the run's `postflight` directory.

## Candidate and package provenance

No guest candidate or boot package was produced because the repository does not currently connect the accepted runtime to the guest target. Consequently there is no guest kernel hash, package manifest, booted package identity, or guest fixture identity for JS111.

For hosted regression only, `build.bat` succeeded at source commit `35cab1a95562744df36ed10f24567611234f792e` with MinGW-W64 GCC 15.2.0 (`x86_64-w64-mingw32`). Its output was 9,171,504 bytes, SHA-256 `4C0917C3232FF48DAA9B2094E72FE9B80A79B4533DF3BFCC073A690436174141`. The exact copy used for hosted JS109/JS110 runs is:

`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS111\run-20261107-065404-8f5e6110\hosted-build\guideXOSServer-js110-hosted.exe`

The recorded provenance explicitly marks `qemuBuilt: false` and `guestTarget: false`. This host executable is not a guest candidate.

## Native and hosted results

| Qualification | Result |
|---|---|
| JS110 focused native suite | 28/28 passed; strict compile passed |
| JS109 focused native suite | 86/86 passed; strict compile passed |
| Full JavaScript matrix | 111/111 lanes passed; 17,656/17,656 reported assertions across 70 lanes |
| Hosted JS109 fixture | PASS: HTTP 200, requested URL retained, 5 document blocks, no inline JavaScript error |
| Hosted JS110 fixture | PASS: HTTP 200, requested URL retained, 6 document blocks, no inline JavaScript error |
| Full hosted Navigator aggregate | 767 checks passed, 7 failed, 774 total; all seven are the established CSS failures listed below |

The hosted fixture scripts establish page load, requested URL, document block count, and the absence of a reported inline JavaScript error. The native JS110 lane is the evidence for the deeper hosted-document/event-callback checks; hosted page-load output alone is not treated as proof of each JSON result.

The seven full-aggregate failures match the established CSS baseline: phase 3C baselines/clipping/hit targets/focus; phase 3G parser/containing-block diagnostics; phase 6A nested scrolling/fixed descendant invariance; phase 6B typed sticky diagnostics; phase 6B local/nested sticky scrollports; phase 6B fixed-descendant viewport invariance; and phase 6C sticky/fixed/clipping/flex/extent neutrality. No JavaScript aggregate assertion failed.

### Guest acceptance behavior not established

There was no guest execution of `JSON.parse`, reviver callback ordering, nested traversal, deletion or replacement, root replacement, `JSON.stringify`, sparse-array behavior, DOM mutation, or rendering. The JS110 native suite and fixture exercise portions of these behaviors in the hosted runtime, but they do not establish guest behavior. Guest serializer/error categories, guest error recovery, guest navigation or realm lifecycle, and post-error guest memory validity were not measured.

## Runtime bounds and safety evidence

The JS109–JS110 source has bounded JSON/runtime limits: traversal depth 32, strings up to 64 KiB (256 KiB aggregate runtime string payload), up to 4,096 runtime string values, 100,000 execution steps per budget, up to 4,096 functions, 1,024 objects, 256 properties per object and 4,096 aggregate properties, 256 KiB property-key bytes, and up to 1,024 array elements per array / 4,096 total. These are source/runtime bounds, not guest measurements.

Existing hosted static-stack evidence includes `parseJson` at 1,504 bytes, a recursive strict-parser helper at 384 bytes, `stringifyJson` at 848 bytes, its emitter at 400 bytes, value preparation at 416 bytes, `invokeFunction` at 768 bytes, and `invokeNative` at 736 bytes. The reviver walk is iterative; serialization remains recursive. The JS110 documentation's conservative maximal-reviver counted-payload estimate is about 1.66 MiB and excludes allocator/container overhead, callback-created values, and other runtime state. These are hosted compiler frame measurements and a source-based payload estimate; there is no guest stack or peak-memory evidence.

Guest allocator capacity, stack headroom, lifecycle cleanup, and runtime-pool behavior therefore remain unqualified. No guest safety issue was observed because no guest runtime was run; this is not evidence that guest execution is safe.

## Preservation and cleanup

The original `guideXOSServer.exe` was restored and verified at 8,398,274 bytes, SHA-256 `A5E439455E4D3E735DFB848EC296A3EC4B347822B21F5B1B736D51227046A971`. The saved manifest comparison confirmed all 42 `navigator-smoke/generated` fixtures (458,372 bytes total) unchanged, with no missing or changed files. `.phase` remained absent. The fresh hosted candidate and logs remain in the external evidence directory; no build output or large logs were added to the repository.

No QAC reservation was held, so none required release. No QEMU was launched by this qualification. The unrelated managed acceptance session found in the final census was preserved; its observed identity and state are recorded in the run's `postflight` evidence. The final preservation check reverified the protected executable and all 42 generated fixtures, with no changes or missing/extra files.

## JS112 recommendation

Before expanding JSON functionality, define and implement a supported guest path for the Navigator JavaScript runtime: target-compatible source/build integration, explicit runtime and memory configuration, boot packaging and loading, and observable guest execution evidence. Then qualify the existing JSON pipeline in a source-matched guest, including rendering, bounded errors, stack, memory, and lifecycle evidence. Keep the JS109–JS110 feature scope unchanged until the guest runtime is actually present.

## Evidence index

All logs and manifests are under:

`E:\CodexArtifacts\guideXOSServer_NAVIGATOR_JS_SUPPORT\JS111\run-20261107-065404-8f5e6110`

Notable files: `preflight/baseline.json`, `preflight/navigator-smoke-generated-manifest.json`, `preflight/generated-fixture-manifest-comparison.json`, `preflight/qac-status.txt`, `preflight/qac-list.txt`, `preflight/qac-queue.txt`, `hosted-build/provenance.json`, `hosted-build/build.log`, `native-js109`, `native-js110`, `javascript-matrix-output.txt`, `hosted-js109`, `hosted-js110`, `hosted-aggregate/navigator-hosted-smoke-20261107-083109.log`, `post-hosted-restoration.json`, and `postflight` (final QAC census, `qemu-processes.json`, and `preservation-check.json`).
