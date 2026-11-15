# SECURITY3A — Durable Credential Persistence Qualification

**Outcome B — `SECURITY3A_DURABLE_CREDENTIALS_PARTIAL`**

The central durability acceptance passed: the native identity and authentication runtimes provisioned real accounts and PBKDF2 credentials on a disposable raw FAT32 image. Five separate QEMU processes booted that image in sequence; credentials, account IDs, roles, and lifecycle changes reloaded from disk, and no authenticated session crossed a boot boundary. One additional raw-disk truncation case was prepared but could not be booted because a separate QEMU guest was active at closeout.

**Repository:** `guideXOSServer_SECURITY_SETTINGS` · **Branch:** `SECURITY_SETTINGS` · **Starting HEAD:** `03a149059fdbc2ed423e4904eaf728a3db9da0d2`

All raw disks, serial logs, QAC logs, and temporary qualification build products are confined to the ignored `out/security3a` directory. The committed documentation and source contain no test passwords or credential verifiers.

## Implementation changes

- The production identity and credential VFS adapters already use `kernel::identity::Runtime` and `kernel::authentication::Runtime`, with generation-numbered snapshots at `/.gxos-identity-{0,1}.dat` and `/.gxos-credentials-{0,1}.dat`. Cold-boot qualification exposed that the FAT writer could not create those dotted long names because it only supported 8.3 names.
- `kernel/core/fs_fat.cpp` now creates valid ASCII FAT long-name entries with a collision-checked 8.3 alias, rejects reserved or malformed components, and preserves a directory end marker. Existing short 8.3 names keep their direct path.
- `tests/fs_fat_persistence_test.cpp` covers a dotted long-name create, duplicate detection, readback after unmount/remount, and invalid FAT characters. The host test exercises the real FAT/VFS implementation on a disposable image copy.
- `kernel/core/security3a_guest_qualification.cpp` is a macro-gated guest harness. It invokes the production singleton runtimes, VFS-backed FAT storage, and PSA/PBKDF2 provider. `kernel/core/main.cpp` calls it only for qualification builds. The harness emits IDs, roles, generations, and result names, never passwords.
- `scripts/prepare-security3a-raw-disk.ps1` creates an explicitly named, owned scratch image and refuses to overwrite an existing image. `scripts/prepare-security3a-corruption-image.ps1` clones the successful image and damages only the requested slot bytes for negative cases.

## Disk and QEMU setup

The tested image is `out/security3a/security3a-credential-final.raw`, 128 MiB, SHA-256 `3d9f247b7348696b276e04709f926b06f18b9d53bc16da7ad8671f9bf58343b0`. It is a direct FAT32 superfloppy because the production boot path mounts the whole persistent ATA device as `/`; its BPB reports 512-byte sectors, one sector per cluster, two FATs, 262,144 sectors, volume label `GXOS3AQUAL`, volume serial `1F308F8B`, FAT32 type text, and the `55 AA` boot signature. The guest logged `Mounted FAT32 at '/'` and `Successfully mounted persistent storage from ata0m` on each clean boot.

Each launch used QEMU with WHPX, OVMF, a host-backed virtio RNG, the raw data image on IDE primary master (`ata0m`, `cache=directsync`), and a dedicated scratch EFI boot tree on IDE secondary master (`ata1m`). QMP sent a graceful quit only after the serial harness emitted its terminal result. The QAC-managed QEMU child exited with code 0 on all five lifecycle boots, and each reservation was explicitly released. Pre- and post-run QAC status showed zero QEMU processes, zero managed sessions, zero queued reservations, and zero blocked reservations.

| Boot | QAC reservation | QEMU PID | Launch UTC | Exit UTC | Guest phase |
|---|---|---:|---|---|---|
| 1 | `05b22745522644a290508e039624c607` | 16008 | 22:52:43 | 22:53:47 | First-boot provisioning |
| 2 | `eee74b5d761d4af7bb9d8cd2a1dfcd83` | 14492 | 22:53:55 | 22:54:36 | Cold reload and password change |
| 3 | `9bd06c376a7343b3922eb5269dfb30b6` | 22860 | 22:54:46 | 22:55:23 | Cold reload and role update |
| 4 | `fa17e70126e54afda8ddd0f8d912ae8c` | 24704 | 22:55:55 | 22:56:35 | Cold reload and account disable |
| 5 | `87d78b9f4d054f35adea93c12e16945f` | 24548 | 22:56:42 | 22:57:16 | Cold reload and disabled-account rejection |

Times are UTC on 2026-11-14. The exact launch/exit records and serial output remain in the ignored `out/security3a/final-qac-boot-*.log` and `out/security3a/final-boot-*.log` files.

## Provisioning and cold-boot results

Stable IDs were Administrator `0x1000` and Standard User `0x1001`. The first boot confirmed the registry was unprovisioned, bootstrapped the Administrator and enrolled its password through production APIs, provisioned the Standard User and credential, and verified both roles and IDs. Both valid passwords authenticated; invalid credentials were rejected without opening a session. The native runtimes reported persistent storage and durable commit results. The raw credential snapshot contained the expected PBKDF2-HMAC-SHA256 record algorithm and parameters.

Every later process started with no active authenticated session. Each reloaded both records, stable IDs, roles, account status, credential algorithm/parameters, and a credential generation directly from the FAT slots. Successful sign-ins bound a session to the expected ID and role, and each test session was terminated before QMP shutdown.

- Boot 2 changed the Standard User password durably and invalidated the current session. It rejected the old password before the change. Boot 3 accepted the changed password and rejected the original password.
- Boot 3 promoted the Standard User to Administrator durably. Boot 4 reloaded the role change, restored Standard User role, and disabled that account in a durable identity update.
- Boot 5 reloaded the disabled status and Standard User role, rejected that account’s authentication, and authenticated the Administrator successfully.
- Identity slot generations progressed from 3/2 after provisioning to 4 on role promotion and 5/6 after role restoration and disable. Credential generations progressed from 1/2 to 3/2 after the password change.
- All five guest phases ended with `result=PASS` and `end_session=none`. No session handle or password input buffer was serialized.

## Corruption and interrupted-update handling

Each negative image was copied from the completed raw disk. The source image remained unchanged.

| Case | Mutation | Guest result |
|---|---|---|
| Credential CRC corruption | Both credential slots had a checksum byte changed. | Identity remained readable; credential state was corrupt; authentication returned storage unavailable and opened no session. |
| Unsupported credential format | Both credential slot versions were changed to 99 and their CRCs recomputed. | Identity remained readable; credential state reported unsupported format; authentication was rejected with no session. |
| Identity CRC corruption | Both identity slots had a checksum byte changed. | Credential storage remained independently readable; authentication and administrator bootstrap failed closed; no session. |
| Unsupported identity format | Both identity versions were changed to 99 and their CRCs recomputed. | Credential storage remained independently readable; identity state reported unsupported format; authentication and administrator bootstrap failed closed. |
| Newer credential snapshot damaged | Only the newer credential slot's checksum was changed; the older snapshot remained byte-valid. | The guest verified exactly one valid credential snapshot remained, but the authentication runtime reported corruption and rejected authentication rather than silently reviving a possibly revoked or replaced password. |
| Newer credential snapshot truncated | The newest slot's FAT directory size was set to 20 bytes on a disposable copy; the older slot remained intact. | Image prepared; guest execution pending because an unrelated QEMU process was active at closeout. Hosted identity/authentication serialization tests cover bounded truncated-snapshot rejection. |

The one-slot damage case is a controlled torn/incomplete-snapshot simulation after a successful commit, not a power cut during a physical write. The observed credential behavior is intentionally fail closed: the current implementation does not authenticate from an older valid credential slot when its peer is corrupt. The raw data and statuses confirm that no credential update was fabricated, but automated rollback or recovery is not implemented.

For the final one-slot-damage run, the serial harness passed all checks and QAC recorded child QEMU exit code 0. QAC reported a transient `CoordinatorUnavailableException` while verifying reservation release, so its acceptance CLI returned 3 for that run. The subsequent final QAC status reported Online with zero QEMU processes and zero managed, queued, or blocked sessions. The five lifecycle boots and the other corruption boots had explicit successful reservation release records.

A later attempt to repeat the one-slot case was refused before launch: the global QEMU census found PID `30884` running a separate `FONTS8R2` guest under `E:\CodexArtifacts\guideXOSUEFI_FONT_SUPPORT`. That unrelated guest was left untouched. No SECURITY3A QEMU process remained; the QAC zero-session status above was captured before that later foreign process appeared.

The prepared truncated image is `out/security3a/security3a-credential-truncated-retry.raw`, SHA-256 `c459b388cb4e0dd86cd5909175cc6189e750e85d43ed082a5e807400f39af807`. Its mode-6 qualification kernel was built, but QEMU execution was correctly deferred while PID `30884` remained active.

## Regression and build results

| Check | Result |
|---|---:|
| Identity foundation | 43/43 |
| Authentication and sessions | 34/34 |
| Settings Users model, navigation, interaction/layout | 14/14 + 8/8 + 13/13 |
| Native Settings runtime | 1,441/1,441 |
| Native Settings layout | 1,037/1,037 |
| Settings Center model | 89/89 |
| Hosted VFS read | 4/4 |
| Native filesystem contract | PASS |
| Block-device generation | 5/5 |
| FAT32/VFS persistence | 36/36 |
| AMD64 freestanding production kernel | PASS; Mbed TLS profile verification passed; SHA-256 `bd55c10550b9aa68de47ceb2d57da93d1c48536d4aa54bb38910935313a10506` |
| Hosted `build.bat` | PASS; output SHA-256 `ecbe7d2ebc7a5d040f525d4d0e266dd491cd2485afccbac8428c4e72f8d0088a` |

The native filesystem contract and kernel builds emitted warnings in existing unrelated sources; neither had a build failure. The FAT persistence suite specifically passed the added long-filename checks and existing flush-error propagation checks.

Native Settings runtime, Users model, and layout regressions passed. The guest harness itself uses the supported native APIs instead of automating the graphical Settings controls, so a visual in-guest Users-page walkthrough is not claimed here.

## Durability limits and SECURITY3B

FAT32 does not provide an atomic multi-file transaction. The stores use alternating generations, close/flush through the existing block-device contract, read back and validate the written slot before reporting persistent success, and stop on unsupported or corrupt states. QEMU `directsync` and the guest flush checks qualify the software path on this test device; they do not establish physical-media power-loss guarantees. The corruption tests did not interrupt a write at the device level. The prepared truncated raw image and mode-6 kernel still need one guest run after the foreign QEMU guest has ended.

SECURITY3B should add native authorization checks at actual file, application, and process boundaries. Administrator and Standard User remain role metadata in SECURITY3A; they do not yet create a system-wide privilege or isolation boundary. Recovery for a corrupt credential peer and graphical account-management coverage also remain future work.
