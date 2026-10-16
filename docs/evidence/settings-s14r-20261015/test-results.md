# S14R test results

The focused native runtime, shared presentation core, hosted Settings/model, and App Model suites below were rerun from the committed S14 implementation at `d7c0f83aceeca722a45b5ec84a9c74bad2f27b94` into the isolated `out/validation/settings-s14r-20261015` directory. Their raw text output is copied under `tests/`. The compositor, display/provider smoke, and freestanding checks retain their passing S14 evidence; no source changed during S14R, so those unrelated build/compile checks were not rerun.

| Suite | Result |
| --- | ---: |
| Native Settings provider/runtime | 828/828 passed |
| Native lifecycle repetitions | 100/100 |
| Deterministic provider refresh cycles | 250/250 |
| Bounded notification/state transitions | 1,000/1,000 |
| Shared Settings presentation core | 2,647/2,647 passed |
| Hosted Settings/model regressions | 359/359 passed |
| Settings Center | 89/89 |
| Inventory and Storage | 45/45 |
| Default Apps | 48/48 (39 model, 9 interaction; lifecycle 100) |
| S6 | 42/42 |
| S7 | 58/58 |
| Users | 32/32 |
| Network | 45/45 |
| App Model identity | 25/25 |
| App Model application actions | 55/55 |
| App Model URI protocol | 41/41; persistence, activation, and no-handler cycles each 100/100 |
| App Model folder activation | 26/26 |
| App Model file activation | 60/60 |
| Compositor pointer-button regression (S14 baseline) | Passed |
| Compositor resize-notification direct compile/run (S14 baseline) | Passed |
| Display/framebuffer, configuration, and VirtIO GPU source/runtime checks (S14 baseline) | Passed |
| Freestanding display-refresh helper compile/link and unresolved-reference checks (S14 baseline) | Passed |

The deterministic tests exercise controlled source-state transitions through the production provider adapter and the shared presentation state. They cover accepted changed snapshots, coalescing/bounded transitions, duplicate and stale notifications/revisions, navigation/focus preservation, unavailable or malformed snapshots, and close/reopen lifecycle safety. They establish notification → provider snapshot → validated shared-state update → repaint request behavior; they do not claim that an ordinary production QEMU guest changed display mode live.

The ordinary AMD64 production build was not repeated because the S14 final production artifact remained available and its kernel, bootloader, and OVMF hashes were independently matched against the isolated S14R guest media. The original clean-build log and build details remain in [the S14 evidence directory](../settings-s14-20261015/amd64-kernel-build.log).

The test executables and original run directory remain preserved under the ignored `out/validation/settings-s14r-20261015/` path. This committed directory contains the raw textual test output and this count index, not executable build products.
