# S22R18 Public Preview Security Enforcement

**Outcome A — `PUBLIC_PREVIEW_SECURITY_PROFILE_QUALIFIED`**
**Assessment date:** 2026-11-08
**Repository:** `D:\dev\guideXOSServer`, branch `main`
**Candidate source commit:** `2e1c2493355bd005399977bcd82a4142c94ca2e9`

This assessment applies only to the AMD64 Server 0.3.0 `ServerV03PublicPreviewPacManOnly` build profile. The ordinary development profile is unchanged. The result establishes a narrow runtime loading policy for the exact PacMan package; it does not establish application process isolation or authenticate a public distribution.

## Repository and build provenance

The candidate was built from a separate clean source clone at the commit above. The canonical build receipt records `worktreeCleanAtPackagingStart: true` and `sourceWorktreeCleanAtBuildStart: true`. The user's untracked `assets/Backgrounds/troncity.png` remained in the main checkout and was not included in the candidate input inventory.

The profile compiles the boundary under `GXOS_PUBLIC_PREVIEW_SECURITY_PROFILE`. The trust data and source-state predicates are in `kernel/core/include/kernel/native_elf_security_policy.h`; discovery, resource verification, executable snapshotting, and the pre-entry gate are in `kernel/core/native_elf_baremetal.cpp`. Release RAM-disk read-only identity is implemented by `kernel/core/ramdisk.cpp` and consumed by the VFS source checks.

## Enforced policy

The kernel uses release-controlled identity data. It does not trust a package's editable manifest to choose its own authority.

| Boundary | Runtime enforcement |
| --- | --- |
| Package inventory | Exactly one package directory is accepted: `PacMan`. Missing, unexpected, or ambiguous package inventory fails closed. |
| Manifest identity | Exact manifest byte count and SHA-256; exact package id, display name, `NativeElf` kind, `native-elf` runtime, executable path, AMD64 architecture, `gx_main` entry point, ABI, and minimum Server version. |
| Source authority | `/Apps` must resolve to the supported root mount on the release RAM disk. The mount and block device must both be read-only; the device must be a release-image RAM disk; source generation and ownership must resolve. |
| Alternate paths | Writable aliases, nested `/Apps` mounts, overlays, writable backing devices, unresolved mount/device ownership, and other source substitutions are rejected. |
| Mutation and TOCTOU | The mount, block-device, volume, and generation identity is compared across discovery, file reads, image loading, and the final pre-entry check. A changed source invalidates verification. |
| Resources | Every consumed resource must be a regular file with the pinned size and SHA-256. Each pin is bounded to 1 MiB. The source is rechecked after each read. |
| Executable | The actual ELF bytes are checked against the trusted identity, validated for the supported AMD64 image contract and executable entry segment, copied into a verified snapshot, rehashed, and checked again immediately before entry. |
| Failure behavior | Rejection emits a specific `[NATIVE-ELF-PREVIEW] decision=REJECT` reason before native entry. Rejected inventory does not alter the registry or VFS state. |

All supported package discovery and launch paths use this gate. The public-preview image contains only the approved Native ELF package; the profile does not enable third-party Native ELF loading.

## Trusted package identity

| Item | Approved value |
| --- | --- |
| Package directory / id | `PacMan` / `com.guidexos.pacman` |
| Display name / kind / runtime | `Nexgen PacMan` / `NativeElf` / `native-elf` |
| Manifest | 956 bytes; SHA-256 `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449` |
| Executable | `bin/amd64/pacman.elf`; 63,640 bytes; SHA-256 `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` |
| Architecture / ABI / entry | `amd64` / `guidexos-c-abi-v1` / `gx_main` |
| Minimum Server version | `0.3.0` |

The nine resource identities pinned by the kernel are:

| Package-relative path | Bytes | SHA-256 |
| --- | ---: | --- |
| `resources/level1.gximg` | 888,860 | `535b37e66d7159078df8a16ce059a6f7ab038588cc9875e152033d046ff47fe1` |
| `resources/pacpics.gximg` | 360,476 | `57a605df8f24ac9641ff6c9d39df551a17ffbcd68466a561adf5f045de259d50` |
| `resources/audio/EatPill.wav` | 7,156 | `510a52e5776573197aef14d77e393185b6c7148d1b59dac535d45c5ca0498686` |
| `resources/audio/extralife.wav` | 21,248 | `767db1552130e676398c121ec605f90f20c89bbbab272305bc781adc8e91d1cd` |
| `resources/audio/fruiteat.wav` | 5,120 | `82c96a997ac2e837de4e418f29f4f4fa6142dd2a0c38f764b6d989d8bd98605d` |
| `resources/audio/ghosteat.wav` | 9,090 | `225168072b38671c89c1fc775d513ca86fbb899e3e82c8032445f6a6dc5e1b95` |
| `resources/audio/killed.wav` | 17,008 | `1c3e91df8e19041ea5783185c5a8a9f2321993b43481c215d8caf3df590290de` |
| `resources/audio/StartMusic.wav` | 68,318 | `062cdc7196c54033f8bf11878d6659fd5b0ba11cd6945640ec8fac4376919421` |
| `resources/audio/startmusicold.wav` | 46,592 | `a6e1c14f6cbc59cb5244c0119c728d412252370eea48bc574c1307a774c83caf` |

## Alternate-path and mutation analysis

The policy checks both VFS metadata and the backing block device. A read-only mount flag over a writable device is insufficient. It also rejects a writable alias or nested mount that could redirect `/Apps`, an absent or ambiguous device owner, unsupported filesystem state, and any generation or source replacement between verification phases.

Hosted regression cases exercise writable `/Apps`, writable backing storage despite a read-only mount flag, altered executable identity, unexpected and ambiguous packages, incompatible ABI/version/architecture, missing metadata and executable, aliases/overlays, unresolved ownership, missing device state, and source replacement during verification. Each rejection is checked against the native-entry sentinel. Four allowed test launches reached that sentinel; rejected cases did not. The test also checks that repeated allowed launches and rejected inventory preserve registry behavior.

## Test and freestanding evidence

Current-source results:

| Check | Result |
| --- | --- |
| Native ELF public-preview policy | PASS, 27 cases, `nativeEntryCalls=4` |
| Release build integrity | PASS, 37 assertions |
| S22R18 QAC profile | PASS, 7 assertions |
| QAC launcher contract | PASS, 151/151 |
| QAC artifact isolation | PASS, 36 assertions |
| Native ABI layout | PASS |
| VFS mount-table integrity | PASS, 16/16 |
| Hosted VFS read | PASS, 4/4 |
| Native filesystem contract | PASS |
| Settings native layout | PASS, 1,037/1,037; 1,000 resize transitions |
| Settings native runtime | PASS, 1,426 cases; 100 lifecycle repetitions, 250 provider refresh cycles, 1,000 notification/state transitions |
| Native present validation | PASS |
| PacMan source registration | PASS, pre-audio/audio-bearing/missing-implementation/missing-definition cases |
| Release ISO packaging fixture | PASS; synthetic fixture only, not boot evidence |

The AMD64 freestanding stack audit used `-DGXOS_PUBLIC_PREVIEW_SECURITY_PROFILE -fstack-usage` and passed the established 16 KiB stack contract with at least 4,096 bytes of required headroom:

| Guarded path | Worst-case estimate | Remaining headroom |
| --- | ---: | ---: |
| Settings display/navigation | 11,512 / 16,384 bytes | 4,872 bytes |
| Legacy Display Options path | 11,896 / 16,384 bytes | 4,488 bytes |
| COM2 storage path | 7,768 / 16,384 bytes | 8,616 bytes |

The canonical build separately passed the freestanding kernel build, symbol audit, package checks, FAT/ISO creation, and independent raw ISO structural verification; see [the disposable VM qualification](RELEASE_S22R18_DISPOSABLE_VM_QUALIFICATION.md).

## Remaining enforcement gaps and residual risk

- PacMan runs as native privileged kernel code. This gate selects and verifies code; it does not isolate the package from the kernel or other kernel state.
- An approved but buggy or compromised PacMan executable can still compromise the system. Integrity does not establish correctness.
- An attacker who already has kernel execution can bypass or replace these checks.
- A compromised boot chain or modified kernel can invalidate the embedded trust data.
- VM containment depends on QEMU/WHpx and host configuration. QAC isolation evidence is not a proof against hypervisor compromise.
- The ISO and its local SHA-256 receipt are not signed. A digest obtained from an untrusted channel does not authenticate publisher origin.
- This profile has no general third-party Native ELF support and no guest network or host-share integration.

S22R16 owner decisions about security risk acceptance, release-contract approval, and distribution authorization remain open. Do not publish, sign, promote, or mark this preview ready in S22R18.

Recommended S22R19 scope is owner-directed authenticity/signing and key-management design, an explicit process-isolation architecture, and separately approved read-only Settings/history behavior. Keep unrelated features out of that phase.
