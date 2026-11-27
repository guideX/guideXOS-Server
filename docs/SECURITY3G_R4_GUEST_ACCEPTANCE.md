# SECURITY3G-R4 Guest Acceptance

**Date:** 2026-11-27 (America/Los_Angeles)

**Outcome:** SECURITY3G_R4_TIMER_PREEMPTION_PARTIAL

## Result

Fresh AMD64 candidates were built from the recovered source and prepared with separate positive and negative firmware variable stores. The authoritative QAC preflight found another active guideXOSServer acceptance reservation, so no R4 guest was launched, requested, or queued. This report records offline preparation and regression evidence only; it does not claim guest qualification.

## Source and repository

- Branch: SECURITY_SETTINGS. Starting and final source HEAD: 78aaf4ff8a4e5e23067c4cc1fa540e74f9b60bc6. The GitHub branch API independently reported the same HEAD before documentation changes.
- The primary tracked source was unchanged. The detached build worktree is also clean. R4 artifacts and logs are under out/validation/security3g-r4/; the root ESP and recovery evidence were not modified.
- Reviewed the recovered Native ELF discovery bound: nine slots for the nine current packages. The public-preview policy remains PacMan-only. The pilot assembly retains its non-overlapping 112-byte frame and assembler bounds assertions.

## Fresh artifact identities

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| Normal AMD64 kernel | 2,921,920 | 965A98665910F0CA2971EC06015E5AF5656D8AAC94235402FD51D785FE77FCE7 |
| Public-preview AMD64 kernel | 2,923,968 | 67D90ADA908ABC2A690B5A0448F9096C3BF6EB35D4452AEA576B416F16CAE231 |
| Final guest-proof kernel | 2,941,120 | 2C0E466E9EFE849FCD5895B91A9EB213EC612DFF3E46D6A433CECC67843B3EE4 |
| Scheduler preemption pilot ELF | 4,120 | 5C648B30CBD50AC64D6027E4DE8726E1899ACC8B88A6FAAB5929799D3A296AC2 |
| UEFI bootloader | 56,832 | B80DB0395234B037C881A0A80BB9A12D9581ED8F112486C4C85DBFAA2C43F0EA |
| QEMU executable | 25,287,296 | A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02 |
| OVMF x64 CODE | 3,653,632 | 33090CC07675BAA5190D9F1E84BF5176B33BCBFA9BACAC522961150CDB6DBB2A |
| OVMF VARS template and each fresh copy | 540,672 | 5D2AC383371B408398ACCEE7EC27C8C09EA5B74A0DE0CEEA6513388B15BE5D1E |
| Runtime ramdisk | 67,108,864 | 8ABC0482C83C2C00D55377506BDE6C64F245BCFC2943788271CA64F7F18195FB |

The final guest-proof kernel was rebuilt with GXOS_NATIVE_SCHEDULER_SELFTEST_ACTIVE, GXOS_SECURITY3F_SCHEDULED_NATIVE_ELF_GUEST_PROOF, and GXOS_SECURITY3G_TIMER_PREEMPTION_GUEST_PROOF. An earlier G-only kernel was preserved under superseded/ after review found that it did not enable the cooperative scheduler self-test. The positive and negative ESPs now contain the full-profile kernel, nine Native ELF packages, and matching 52-file inventories. Both canonical ESP tree hashes are BB71D5BB761D5BEE31D41BD4E1B55CE474C4B6C6D3EF0F6874861EF7FFFE31F9.

QEMU reports version 11.0.0 (build v11.0.0-12122-ga4bb4b10c9). The installed x86_64 firmware descriptor (SHA-256 F6756761629339223E398BAD85CFF8DACE5310E9C88876052A0784B16BA7FF5D) pairs edk2-x86_64-code.fd with the separate edk2-i386-vars.fd NVRAM template for x86_64. The original template was preserved; separate writable copies were prepared for each candidate. This pairing matches the [QEMU 11.0.0 x86_64 firmware descriptor](https://github.com/qemu/qemu/blob/v11.0.0/pc-bios/descriptors/60-edk2-x86_64.json) and [EDK II build configuration](https://github.com/qemu/qemu/blob/v11.0.0/roms/edk2-build.config).

The planned positive CPU is max; the planned unsupported-CPU case is qemu64. Their QAC acceptance launch plans and complete per-file inventories are in out/validation/security3g-r4/. Neither plan was executed.

## Offline verification

Normal, public-preview, and full guest-proof AMD64 kernel builds passed. The pilot assembled and linked; ELF inspection confirmed AMD64 ELF64 and the expected entry points. The isolated UEFI bootloader build passed. Ten fresh focused regression scripts passed: managed execution lifecycle (48/48), Native ELF security policy (zero failures), Native ABI layout, system-service bridge (82/82 plus device and storage bridge checks), App Model identity (25/25), identity foundation (43/43), authentication sessions (60/60), Native Settings runtime (1,447/1,447), Native Settings layout (1,037/1,037), and Native filesystem contract. Per-test logs are in out/validation/security3g-r4/logs/.

## QAC and unverified guest gates

At the final captured preflight (2026-11-27 13:16:58 -08:00), QAC was online with one managed QEMU, zero foreign QEMU processes, and zero queued requests. Acceptance reservation e1b98acf06df4c1e8a94829ad302e081 was Running for guideXOSServer under another workstream. The earlier 13:08 snapshot also found QAC occupied under reservation 823f14635ac9445b99074c16b165f554; the reservation changed between observations. R4 did not request admission or interact with either guest. Both status/list/queue snapshots and process command lines are preserved in evidence/.

No positive or negative guest run occurred. There are no R4 serial/QMP logs, observed CPUID features or XCR0, PIT-driven switch evidence, alternate-context progress, register/checksum results, lifecycle cleanup, service or desktop responsiveness observations, QEMU exit status, or R4 QAC release result. No R4 source repair was made. The next phase is to obtain QAC admission when it is idle, then run the positive max candidate and the separate qemu64 negative candidate with their own VARS copies.
