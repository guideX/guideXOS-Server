# Server v0.3 — S22R27 Memory Foundation Qualification

**Outcome A — `FRAME_OWNERSHIP_AND_ADDRESS_SPACE_FOUNDATION_VERIFIED`.** The bounded owner and private page-table implementation pass source-backed hosted tests, compile in the AMD64 freestanding kernel, and pass the kernel symbol audit. No CPL3 execution or guest memory-management diagnostic was performed.

## Provenance and preservation

- Source checkout: `D:\dev\guideXOSServer`, branch `main`, starting HEAD `50c6cff0993227de7c582b00eb1daabed62cf816`.
- Host: `LEON2_RDT`.
- Pre-existing untracked `assets/Backgrounds/troncity.png` was preserved at SHA-256 `444ac64211907d80f0266e2283960cefaa932803d0aee7e0ccfd414f6fae2fda`.
- The accepted S22R23 candidate ISO at `E:\CodexArtifacts\S22R23\candidate-source\out\release-profile\4c9988cf-5d0d-414d-afe8-81f7351c2706\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso` was preserved at SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40` (90,245,120 bytes).
- All qualification executables, kernel/loader build products, and writable FAT images were placed under `C:\Users\guideX\.codex\visualizations\2026\11\20\01a1f6ff-aad2-7b01-9cbc-33a510fa6130`. No repository `dist` output or release marker was changed.

The bootloader was rebuilt with MSBuild `Release|x64` into an external output directory. Its executable was 58,368 bytes (SHA-256 `4cd7ebc6d0f0067ddb0be481c3a04958a4a105e37124eb46e38a0e03b97f1e20`). The AMD64 kernel was built with the normal freestanding make target and additional stack-usage diagnostics. The ELF was 2,833,720 bytes (SHA-256 `a7ad0301622a9b224e85052229cfbc39dfa56ffb16935bfeac09691a66cba3bb`); its PE input was 2,960,344 bytes (SHA-256 `33974887fa29483299c5ebd428c116cd84b49c85273dc4333119ca3bbcf49787`).

`scripts/audit-kernel-symbols.ps1` passed: 52 ELF undefined-symbol rows / 22 distinct names, no ELF relocations, no PE imports, and no COFF relocations. The BootInfo framebuffer/array smoke test passed with version 3 and arena/range validation.

## Hosted and build validation

| Check | Result |
| --- | --- |
| `scripts/test-amd64-memory-foundation.ps1` | PASS: bounded ownership, allocation and mapping rollback, effective permissions, shared kernel mappings, repeated teardown |
| `scripts/test-amd64-context-switch.ps1` | PASS: Microsoft x64 voluntary switch ABI, stack alignment, shadow space, repeated switches, and nonvolatile GPRs |
| `scripts/test-native-elf-security-policy.ps1` | PASS: approved package policy and fail-closed integrity, mount, ABI, architecture, and alias cases |
| `scripts/run-native-abi-layout-test.ps1` | PASS |
| `scripts/run-vfs-mount-table-integrity-test.ps1` | PASS: 19/19 checks |
| `scripts/run-vfs-hosted-read-test.ps1` | PASS: 4/4 checks |
| `scripts/run-fs-fat-persistence-test.ps1` | PASS: 44/44 checks, including `/Documents`, `/AppData`, persistent settings, and document preservation |
| `scripts/run-native-filesystem-contract-test.ps1` | PASS |
| `scripts/run-user-data-volume-policy-test.ps1` | PASS: 9 device-selection and write-safety cases |
| `scripts/run-settings-native-layout-test.ps1` | PASS: 1,037 checks and 1,000 resize transitions |
| `scripts/run-settings-native-runtime-test.ps1` | PASS: 908 checks, 100 lifecycle repetitions, 250 provider refresh cycles, and 1,000 notification/state transitions |
| `tests/pacman_source_registration_test.ps1` | PASS: pre-audio, audio-bearing, missing-implementation, and missing-definition fixtures |

The FAT persistence run used a new 64 MiB FAT32 image formatted by the repository's `-FormatOnly` path. The external fixture was seeded with the two baseline entries the established test requires (`APPDATA` and `UNTITLED.TXT`); the test runner then made its own writable copy. The 44 production FAT/VFS checks passed on that copy. The tracked disk images and accepted ISO were not written.

The freestanding `.su` records show new S22R27 frames at or below 304 bytes: `run_task_address_space_fixture` 304 bytes, `initialize_amd64_task_memory` 272, `map_task_frame` 240, and `initialize_frame_owner` 128. The existing 16 KiB stack guard passed with at least 4,520 bytes of designated headroom. Its largest guarded path remains a pre-existing Legacy Display Options path at 11,864/16,384 bytes; Settings Display is 11,480/16,384, and the COM2 guarded storage path is 7,736/16,384. These designated guards do not establish whole-kernel stack safety.

## Guest, QAC, and CPL3 status

No diagnostic ISO or guest run was created. QAC was not used. An unrelated QEMU process was already active on the host and was left untouched. Hosted tests exercise the production owner and page-table code through callbacks; the freestanding build proves integration and linking, not runtime hardware ownership.

There is no CPL3 transition, task CR3 activation, TSS `RSP0` setup, user GDT selector, trap-based exit, user-fault recovery, timer preemption, or forced termination. The fixture never jumps into user code. S22R27 therefore does not claim application survivability or arbitrary kernel-corruption containment.

## Preserved behavior and next scope

PacMan's normal ring-0 launch and cleanup path, Native ELF policy, read-only trusted `/Apps`, persistent `/Documents` and `/AppData`, VFS behavior, and storage services were not redirected through the new task foundation. The S22R25 switch remains dormant. UEFI BootInfo advances to v3 for the optional arena; Multiboot does not initialize this owner.

S22R28 should add the minimum safe CPL3 entry and controlled exit/fault contracts on top of this foundation: user selectors, initialized TSS `RSP0`, kernel-owned entry stacks, saved user frames, controlled trap return, and off-stack reaping. Independent preemptive scheduling, SIMD/FPU state ownership, and non-cooperative application termination require separate later validation.

No signing, publication, promotion, or release authorization is implied.
