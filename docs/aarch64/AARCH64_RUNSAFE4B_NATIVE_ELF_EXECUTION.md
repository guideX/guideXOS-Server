# AArch64 RUNSAFE4-B Native Compiler ELF Execution

## Outcome

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

- Candidate manifest: `out/aarch64-runsafe4b-native-20261114-01/candidate-manifest.json`
- Candidate build log: `out/aarch64-runsafe4b-native-20261114-01.build.log`
- QAC preflight session: `out/aarch64-runsafe4b-native-qac-20261114-01/runsafe4b-20261114-203807-703/session.json`
- QAC console log: `out/aarch64-runsafe4b-native-qac-20261114-01.console.log`
