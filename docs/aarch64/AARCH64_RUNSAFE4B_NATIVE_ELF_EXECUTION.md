# AArch64 RUNSAFE4-B Native Compiler ELF Execution

## Current-head follow-up — 2026-11-14

**Current outcome: `RUNSAFE4B_QAC_PENDING`.** A fresh candidate was built from the clean current source revision `88bedfe19976225445fa6866a786000d0d8a3eae` on `AARCH64_SUPPORT`. Hosted readiness passed. The final QAC inventory before admission found a different managed QEMU session occupying the coordinator, so no RUNSAFE4-B reservation was submitted and no QEMU guest was launched. The native diagnostic count remains **0**, with **0 of 3** qualifying boots. Candidate `--03` is preserved and was not frozen because its first diagnostic has not run.

The qualification checkout began and ended at source revision `88bedfe19976225445fa6866a786000d0d8a3eae`; no production source changed. The candidate was built while the worktree was clean. This follow-up report is a documentation-only worktree edit, kept uncommitted so the branch HEAD and candidate source identity remain aligned for a future QAC admission. No reset, rebase, merge, push, or branch switch was performed.

### Current candidate identity

Candidate `out/aarch64-runsafe4b-native-20261114-03` records the exact current HEAD in both `candidate-manifest.json` and the guest source marker header. Developer Studio remains at pinned revision `058c83fe1ae270a103eadc7caca961328e8abe2a`, using SDK revision `f63a7710a30b49f785aac134abf06a3a6c5b5aca` and ABI `guidexos-c-abi-v1-studio-v05-464`. The manifest profile is `runsafe4b-compiled-el0`; its Studio Run safety gate remains disabled.

| Candidate artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Candidate manifest | 4,234 | `b93f3ae8b39025503701f226942ed0c62c7e87452fe18986c93d183cd45e41ca` |
| `kernel.elf` | 51,985,752 | `4635e0bdad1b3c6a698b089de52b2300cbc5ba75833ab403ae190d65186b332a` |
| `BOOTAA64.EFI` | 14,336 | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| `esp/ramdisk.img` | 67,108,864 | `3f2a2739b67bcec06cfc59d556c2b38244ca562a2f1777099d2a6e06e321dbfa` |
| Pinned Studio ARM64 ELF | 1,327,476 | `8e4f74feebe4b3d3f245f31560584b565fdba5743be3a61f0b83b4725dbde679` |
| Studio `app.json` | 1,379 | `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c` |

The builder used Clang/LLD 22.1.8 (LLVM commit `ca7933e47d3a3451d81e72ac174dcb5aa28b59d1`); hosted compiler regressions used MinGW g++ 15.2.0. The production compiler/runtime source identity is the full source revision above. Principal source input hashes were recorded from that clean revision:

| Source input | SHA-256 |
| --- | --- |
| `kernel/core/compiler/compiler_build_service.cpp` | `f6e88e91ac48bbb8eca9ab99307a87c258e82f3aea90145492aafe873f24f724` |
| `kernel/core/compiler/compiler_driver.cpp` | `574604e7bdf05153843609b54a288561ea382d280bb71cce0357c7eabb12e33d` |
| `kernel/core/compiler/compiler_lexer.cpp` | `bb3a92671735e421dc5859b328f78f71affcd8c26aa98b0ac9f61e3221300212` |
| `kernel/core/compiler/compiler_parser.cpp` | `ff29e1954637b556c1d6369056e8a9ea5807b39efc5e4cb53041951bb047554a` |
| `kernel/core/compiler/elf_writer.cpp` | `13dd11ea97b8cc03be0c1db6ea628cb4f742262f1dc7fcf20d9845f7a9cabfc0` |
| `kernel/arch/arm64/compiler_backend.cpp` | `9058687b97a8f4d8563cf2ae8e01bf397b14cad9b9fdc192e4b777d9614ef0df` |
| `kernel/arch/arm64/studio_elf_image.cpp` | `896ce28e5781b7d71aadd8ec7de6b4d065e452bd84509c5c6c3a1f9f0ad309ec` |
| `kernel/arch/arm64/phase2_el0_runtime.cpp` | `0fedc4baf36d9f5d523996a17b82afb753d093442e8ecfe57da0c4e240dbfffe` |
| `kernel/core/common_scheduler.cpp` | `26cca28c9a8daad47b3c65c31a28e9fcb7cc09dd8e98d7e02476450f7ce85d2f` |
| `kernel/arch/arm64/phase4_main.cpp` | `6977190f7ee6655108c65327d19453098f80d37dfc4576b1ab54e465212c9e8c` |

The intended runtime is `virt,gic-version=2,acpi=off`, Cortex-A53, AArch64 UEFI. The locally installed QEMU was version 11.0.0, SHA-256 `8d2183e745155a303ffb9fd861c5da31c5e6c19fcbfd082081cc3e4bcdb0e748`; the code firmware hash was `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d`, and the vars template hash was `b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f`. These are input identities only; this follow-up did not start QEMU.

### Current compiler fixture provenance

The current production Build-service host harness compiled both required sources and emitted the actual artifact bytes:

| Source | Source SHA-256 | Current ELF | ELF SHA-256 |
| --- | --- | --- | --- |
| `int gx_main(void* ctx) { return 42; }` | `79ed63fbc4052d8b92cc7ef065ecaf599e4c8409bd3e0dd31be877286acf3c97` | 4,108-byte AArch64 ELF, base `0x51000000`, entry `0x51001000` | `76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2` |
| `int gx_main(void* ctx) { return 43; }` | `b402cc0125e3dc51fcfaf7d902c5556d346bcd91159e3cdd77af877d04f8b933` | 4,108-byte AArch64 ELF, base `0x51000000`, entry `0x51001000` | `e636e87fca022d1e2d4b4068067e2a7d19f59a626d24c28872c4fe23ebecc8d6` |

These hashes were verified from bytes produced by the current production compiler/build-service source, rather than accepted from the historical report. They establish compiler-output provenance only. They do not establish a fresh guest Studio Build, EL0 entry, execution, SVC completion, or cleanup; those observations remain pending.

### Hosted readiness on the current source

| Gate | Current result |
| --- | --- |
| RUNSAFE4 EL0 loader, mapping, and scheduler host suite | PASS, 254 assertions; scheduler/exception behavior is hosted simulation |
| RUNSAFE4-A Build identity and artifact snapshot controls | PASS; production service exported the return-42 ELF hash above |
| Studio v0.5 adapter | PASS, 181 checks |
| Studio Run fail-closed controls | PASS; Run remains disabled |
| Phase 10 ELF/resolver controls | PASS, including wrong-machine and malformed inputs |
| Resident ARM64 compiler and bootstrap regressions | PASS (`compiler_arm64_host_test` and `compiler_bootstrap_host_test`) |
| Current resident Build-service ELF fixtures | PASS; return 42 and 43 hashes and fixed ELF profile verified above |
| GICv3 hosted suite | PASS, 62 assertions |
| QAC supervisor state-machine suite | PASS, 60 assertions |
| Captured firmware-source DTB using the production Phase 2 parser | PASS, 52 assertions; fixture SHA-256 `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311` |
| Native filesystem contract | PASS |
| Phase 12 source-matched ARM64/EFI build, link, and package | PASS; candidate `--03` above |

The RUNSAFE4/4-A host controls exercised failed-Build invalidation, stale or mismatched Build identities, VFS path replacement after snapshot capture, ELF rejection, private mappings, permissions, guards, and lifecycle validation. The source review confirmed that the candidate’s RUNSAFE4-B branch uses the immutable Build snapshot, calls the scheduled EL0 loader, validates RX image/trampoline and RW/NX stack mappings with both guards absent, and checks scheduler state, compiled entry, saved x0, SVC exception class/result, resource cleanup, reap, and kernel continuation. A failed qualification path enters its fail-closed loop; it does not fall through to the privileged Phase 9 NativeElf route. The guest-side RUNSAFE4-B controls themselves have not run.

### QAC status and native counts

The initial QAC inventory after candidate preparation was idle. Immediately before admission, the final status/list showed QAC online with one managed session, one reservation, and one QEMU process. The occupied session was reservation `114ccc93f66a4d8690ea0101b80e6d24`, owned by the separate `guideXOSServer` workstream (QAC CLI PID 34276, managed child PID 22832, `qemu-system-x86_64.exe`). No reservation was submitted for RUNSAFE4-B, no process was controlled, no QEMU child or QMP endpoint was created, and the prior `--01` session record was left unchanged.

Accordingly, this follow-up has **0 diagnostic boots and 0 of 3 qualifying boots**. This is an availability blocker, not a guest test failure or loader defect. When QAC is available, use the exact candidate `--03` for one bounded diagnostic run; freeze it only after the complete 42/43 execution evidence passes, then use that same candidate for two fresh authorized repeat boots.

### Studio and hardware boundary

Studio Run remains disabled. The public Studio Run contract still lacks the Build generation/owner/artifact identity and a safe asynchronous execution handle with queryable terminal result delivery; integration must connect those identities to the internal scheduled EL0 loader and fail closed on every mismatch. No fresh in-guest Studio Build or Studio Build-to-Run interaction is claimed. Legacy Phase 9 NativeElf remains privileged EL1, and no physical ARM64 hardware qualification was performed.

## Preserved prior attempt — candidate --01

**`RUNSAFE4B_QAC_PENDING`** — the source-matched native candidate and hosted gates passed, but the supervised runner classified QAC as occupied before granting a reservation. No guest boot or QEMU launch occurred for this qualification. Native EL0 execution of returns 42 and 43, and the three-boot repeatability target, remain unverified.

## Revisions and candidate

The starting checkout was clean on `AARCH64_SUPPORT` at `d0feb820ff3d78f3bc49f5ff493a73526902aa30`, one commit ahead of its upstream tracking branch. The implementation is committed locally as `a3102c003cf7e7f8f429f9f6466b1c38d70afba0` (`aarch64: qualify compiler-generated ELF execution at EL0`). No reset, rebase, push, or change to another branch was made. The preserved RUNSAFE3-B acceptance artifacts were not modified.

Candidate `out/aarch64-runsafe4b-native-20261114-01` was built from that exact clean revision. Its manifest profile is `runsafe4b-compiled-el0`; Studio Run remains disabled. The pinned Developer Studio source revision is `058c83fe1ae270a103eadc7caca961328e8abe2a`.

| Candidate artifact | SHA-256 |
| --- | --- |
| `kernel.elf` | `aac847a5a1f50d1128cac67cbce1ac70cd1d336c7f34f351e6669053e5e627b8` |
| `BOOTAA64.EFI` | `1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42` |
| `esp/ramdisk.img` | `493d120125296fc926a5ca71b66647d127d0cdfd99f1ff1f95f444d80ac7cc3c` |
| Pinned Studio ELF | `8e4f74feebe4b3d3f245f31560584b565fdba5743be3a61f0b83b4725dbde679` |
| Studio `app.json` | `62af7dce96baa5605092bb561e6c4bfe5428367bff8bbb229d4f01d41691ad4c` |
| Candidate manifest | `b14a3912a15c2a429196a3f3ebcedd627ea64cd3e070e269e72d82c3ca8c836f` |

The candidate build used QEMU 11.0.0. The recorded QEMU binary hash is `8d2183e745155a303ffb9fd861c5da31c5e6c19fcbfd082081cc3e4bcdb0e748`; the AArch64 code firmware hash is `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d`; and the vars template hash is `b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f`.

## Compiler artifact provenance

The candidate manifest binds the diagnostic to these source and historical artifact identities:

| Program | Source identity | Historical compiler ELF identity |
| --- | --- | --- |
| `int gx_main(void* ctx) { return 42; }` | `79ed63fbc4052d8b92cc7ef065ecaf599e4c8409bd3e0dd31be877286acf3c97` | `76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2` |
| `int gx_main(void* ctx) { return 43; }` | `b402cc0125e3dc51fcfaf7d902c5556d346bcd91159e3cdd77af877d04f8b933` | `e636e87fca022d1e2d4b4068067e2a7d19f59a626d24c28872c4fe23ebecc8d6` |

The historical return-42 artifact is the R2B1 resident-compiler Build result; the RUNSAFE4-A hosted Build fixture reproduced its digest. The return-43 digest is the historical recovery artifact. Both are compiler-generated provenance fixtures, not synthetic valid ELFs. The diagnostic code is set up to run fresh resident Builds in the guest: return 42 from the staged project source, then return 43 after writing its distinct C source into guest VFS. **That fresh guest compilation did not occur in this attempt.** Hosted fixture checks and matching manifest values do not establish a fresh guest Build or native execution.

## Implementation and intended evidence

The kernel-internal task result now exposes the compiled ELF entry, initial EL0 trampoline PC, and descriptors for the entry page, trampoline, user stack, and both stack guard addresses. These fields are not part of the public Studio ABI. Hosted production-backed controls check RX image/trampoline mappings, an RW/NX stack, and unmapped guards.

The RUNSAFE4-B guest diagnostic and evidence parser are prepared to require two distinct successful resident Builds and generations, exact source/artifact identities, snapshot immutability after VFS path replacement, and actual observed entry and SVC results. The guest path also checks Build identity rejection, malformed ELF rejection before start, duplicate and wrong-owner lifecycle calls, stale generations, cleanup, root restoration, reap, scheduler progress, and final exception counters. The runner requires actual saved x0, SVC EC/ELR, terminal state, and cleanup fields before accepting `EXEC42`, `EXEC43`, or the overall PASS marker.

These are diagnostic requirements and hosted checks, not native observations from this attempt. There is no guest evidence for image/stack permissions, EL0 entry, compiled instructions, trampoline return, SVC delivery, asynchronous completion, resource reaping, or kernel continuation in this report.

## Hosted and build verification

| Gate | Result |
| --- | --- |
| Phase 2 host controls | PASS, 51 assertions; preserved firmware-source DTB PASS, 52 assertions, SHA-256 `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311` |
| Phase 3 host scheduler contract | PASS |
| Phase 4 allocator/VFS and scheduler controls | PASS |
| Phase 9 application lifecycle build and host controls | PASS |
| Phase 10 ELF/resolver controls | PASS, including missing, unknown, wrong-machine, traversal, malformed, oversized, duplicate, and legacy cases |
| Phase 11 cross-architecture build | PASS; ARM64 Developer Studio client and AMD64 control payload linked |
| ARM64 compiler and bootstrap host regressions | PASS |
| Phase 12 package and freestanding ARM64/EFI candidate build | PASS |
| RUNSAFE4-A Build identity and EL0 host controls | PASS; EL0 host suite reported 254 assertions |
| Studio v0.5 adapter | PASS, 181 assertions |
| Studio Run fail-closed gate | PASS |
| GICv3 host suite | PASS, 62 assertions |
| Native filesystem contract | PASS |
| QAC supervisor state-machine regressions | PASS, 60 assertions |
| PowerShell parser and `git diff --check` | PASS |

These checks validate hosted behavior, source contracts, and build/link outputs. They do not qualify EL0 execution on the AArch64 guest. The existing Phase 7 exploratory compiler-reference failure remains separate and was not used by this candidate path.

## QAC admission record

The only QAC attempt used `scripts/run-aarch64-runsafe4b-qac.ps1` with the candidate above. Its session record is preserved at `out/aarch64-runsafe4b-native-qac-20261114-01/runsafe4b-20261114-203807-703/session.json`.

At preflight, QAC was online with one managed QEMU session and no foreign QEMU process. The existing reservation was `1ab43779720c484abb3fc2a7dfc9d9f5`; its managed child PID was 12384 (`qemu-system-x86_64.exe`) and QAC CLI PID was 27392. The RUNSAFE4-B runner recorded `qac_preflight_status=OCCUPIED`, `qac_exclusive_grant=false`, no reservation for this diagnostic, and `qac_qemu_launch_marker=false`. Its failure was `QAC_SLOT_OCCUPIED: QAC preflight=OCCUPIED. No guest was started.` No existing process or reservation was controlled or changed.

Therefore the native diagnostic count is **0**, and complete qualifying boots are **0 of 3**. This is QAC pending, not a loader defect or a native partial pass. The preflight attempt has no guest log and supplies no native result.

## Lifecycle, Studio, and remaining scope

Studio Run stayed fail-closed throughout. The v0.5 Studio ABI layouts are unchanged. No Studio GUI Build-to-Run interaction or privileged Phase 9 fallback is claimed. The candidate's internal diagnostic is not the Studio Run integration gate.

The supported ELF profile remains integer-only. This work does not claim SIMD/FPU state support, dynamic linking, a broader runtime, strict real-time timeout latency, or physical-board qualification. Physical ARM64 hardware was not tested.

To complete the milestone, build a clean source-matched candidate when QAC admission is available, obtain a genuine grant through the supervisor, complete one diagnostic boot, and only after its full evidence passes run two more fresh QAC sessions with that same frozen candidate. Record each reservation, managed child, guest log, cleanup, and QAC release. Outcome A requires all three complete runs.

## Evidence locations

- Current candidate manifest: `out/aarch64-runsafe4b-native-20261114-03/candidate-manifest.json`
- Current candidate readiness and final QAC snapshot: `out/aarch64-runsafe4b-native-20261114-03/follow-up-readiness.json`
- Current return-42 compiler ELF bytes: `out/aarch64-runsafe4b-native-20261114-03-runsafe4a-host/build-identity/compiled-arm64.elf`
- Current return-43 compiler ELF bytes: `out/aarch64-runsafe4b-native-20261114-03-runsafe4a-host/build-service-return43/compiled-arm64.elf`
- Current EL0 host suite: `out/aarch64-runsafe4b-native-20261114-03-runsafe4a-host/el0-loader/`
- Current captured-DTB host suite: `out/aarch64-runsafe4b-native-20261114-03-phase2-host/aarch64-phase2-host-tests.exe`; fixture `out/aarch64-bootdtb2-minimal-20261102/bootdtb2-guest-20261102-153806-117/firmware-source.dtb`
- Current GICv3 hosted suite: `out/aarch64-runsafe4b-native-20261114-03-gic-host/`
- Current native filesystem contract executable: `out/aarch64-runsafe4b-native-20261114-03-native-filesystem/native-filesystem-contract-test.exe`
- Candidate manifest: `out/aarch64-runsafe4b-native-20261114-01/candidate-manifest.json`
- Candidate build log: `out/aarch64-runsafe4b-native-20261114-01.build.log`
- QAC preflight session: `out/aarch64-runsafe4b-native-qac-20261114-01/runsafe4b-20261114-203807-703/session.json`
- QAC console log: `out/aarch64-runsafe4b-native-qac-20261114-01.console.log`

## Bounded admission follow-up — 2026-11-15

**Outcome: `RUNSAFE4B_QAC_PENDING` (Outcome D).** The preserved candidate `--03` was reverified for one supervised admission preflight. QAC was online but occupied by an unrelated managed session, so the runner stopped before requesting a RUNSAFE4-B reservation. No guest or QEMU child was launched, and no native execution is claimed.

The checkout was still on `AARCH64_SUPPORT` at `88bedfe19976225445fa6866a786000d0d8a3eae`. The existing report edit was preserved; no production source changed and no candidate rebuild occurred. The runner revalidated the candidate manifest (`b93f3ae8b39025503701f226942ed0c62c7e87452fe18986c93d183cd45e41ca`), kernel (`4635e0bdad1b3c6a698b089de52b2300cbc5ba75833ab403ae190d65186b332a`), EFI (`1605c55a4d7c0c7b0589bd181e28d2e32f7ef9da31a6aeb23695c237ff7bfe42`), ramdisk (`3f2a2739b67bcec06cfc59d556c2b38244ca562a2f1777099d2a6e06e321dbfa`), Studio ELF/package manifest, and both distinct 4,108-byte compiler fixtures (return 42: `76c5a81849c6f9372432a5e164563d10226a5c8ae211f4a16a8e70b5f09e48b2`; return 43: `e636e87fca022d1e2d4b4068067e2a7d19f59a626d24c28872c4fe23ebecc8d6`). All matched their recorded identities.

The single preflight reported one managed QEMU process and one `Running` reservation, with zero foreign QEMU processes. The unrelated reservation was `1252f51696a348d3bb3bcdf1df31294a`, owned by `guideXOSServer` / `LEON2_RDT\guideX`, with managed child PID `24348` (`qemu-system-x86_64.exe`). No request was submitted, the unrelated session was not controlled, and no guest QMP endpoint was opened. The runner was configured with a 60-second admission bound, 300-second guest runtime bound, 45-second child cleanup bound, and 20-second reservation release bound.

The preflight session record is `out/aarch64-runsafe4b-native-qac-20261115-01/runsafe4b-20261115-113214-438/session.json`; it records `OCCUPIED`, `QAC_SLOT_OCCUPIED`, no guest log bytes, and no exclusive grant. Diagnostic boots remain **0** and qualifying boots remain **0 of 3**. Candidate `--03` is not frozen; `follow-up-readiness.json` remains unchanged with `RUNSAFE4B_QAC_PENDING` and Studio Run disabled. This admission-only record does not change or supersede either earlier occupied-QAC record above.
