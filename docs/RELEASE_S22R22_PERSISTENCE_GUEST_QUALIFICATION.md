# S22R22 — Persistent Documents Guest Qualification

**Outcome B — `PERSISTENCE_IMPLEMENTED_GUEST_PENDING`**  
**Candidate:** S22R22 Model B public-preview PacMan-only AMD64 ISO  
**Candidate ISO SHA-256:** `efa290538ef3155d264bf0b26923933bd2c9332aa5574e5be977be9d7a9c948c`

The candidate booted with a read-only system ISO and a separately attached writable FAT32 test disk. A document created by Notepad was visible on the same `/Documents` volume on subsequent cold boots, including after a guest-requested normal shutdown, and its exact 161 bytes were independently read from the backing image. QAC runner errors left the runs incomplete; therefore this report does not claim the exact candidate passed the complete planned guest acceptance sequence.

## Candidate and build identity

The candidate was constructed only after the validated source and hosted/freestanding checks passed, from a clean isolated clone at source `eb61ca10b48c0e96e1a04a302e82d6c52b828f41`, branch `main`, build ID `3b07fcc2-581e-4a02-992c-31f2862e34e0`, profile `ServerV03PublicPreviewPacManOnly`. The main checkout's pre-existing untracked wallpaper was not copied into the build inputs.

| Artifact | Identity |
|---|---|
| New ISO | 90,245,120 bytes; SHA-256 `efa290538ef3155d264bf0b26923933bd2c9332aa5574e5be977be9d7a9c948c` |
| Embedded EFI FAT32 image | 90,177,536 bytes; SHA-256 `cde8f79d2202f5e2108470500a52295321355dd03163657ddc7c7b7995686348`; file inventory matched the manifest after extraction from the ISO. |
| Kernel | 2,807,704 bytes; SHA-256 `e4ef32d04fff2a89d9344c65bf26052509e331c241d0de9d8ee7f4837bd514b2` |
| Ramdisk | 67,108,864 bytes; SHA-256 `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |
| Bootloader | 56,832 bytes; SHA-256 `b2c454e62c8a5728140dcc0f6aa36624f9cffa6aca9480e210c6171abbe16227` |
| Kernel symbol audit | SHA-256 `87a5a529ccfb7fa1eeb1461c2385e3b998950f94645d4ce935b7ac381c2ae40b` |
| PacMan ELF / manifest | `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` / `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449` |
| Security profile | `S22R18-PacMan-ReadOnly-Boot-Image-v1`; one Native ELF package, PacMan only. |
| Preserved S22R21 ISO | `86a3296adf0558fa509a044232b0086e57edd6c0bd4236677fba90d8a294d5fe` |

The new candidate is distinct from S22R21 and from the earlier S22R22 candidate `2898fe9256dea6cb5b62bdfdc7ed8843052d777b944bc9f8ffbf61b572dcdabd`. Prior candidates remain unchanged. No ISO was copied to `dist`, signed, published, promoted, or marked ready.

## QAC configuration

QAC 2.0.0 managed the guest lifecycle with QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`) and OVMF code SHA-256 `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a`. The qualified configuration used AMD64 PC/WHPX, 2 vCPUs, 2048 MiB, standard VGA, UEFI, CD-only boot order, no network, no HMP monitor, and QAC-owned loopback QMP.

The release ISO and firmware code were read-only. One 128 MiB raw `.img` under `E:\CodexArtifacts\S22R22\persistent-data\guidexos-user-data.img` was attached writable as an IDE disk with serial `GXOS-DATA-V1`; it was not bootable. The image was created as a dedicated FAT32 test volume with label `GXOSDATA`, marker `GXOSDAT.ID`, and `APPDATA`. The test disk was reused without reset or reformat. No host shared folder, USB passthrough, physical disk, network adapter, or guest COM2 host channel was attached.

## Cold-boot observations

### Initial save — run `s22r22-persistent-documents-coldboot1-final-5e7dbad0c09a4e39be87d52c94d58f91`

- QAC reservation `ff5c4a135ce6403cb3d9d2df16be3808`; QEMU PID 35404; QMP `127.0.0.1:64527`.
- Serial confirmed the immutable system root and `/system` alias, selected `ata1s` by `GXOS-DATA-V1`, and logged FAT32 label `GXOSDATA` with `/Documents` mounted and `/AppData` aliased to `/Documents/APPDATA`.
- File Explorer/Notepad Save As selected `/Documents/untitled.txt`. Notepad's title showed `/Documents/untitled.txt - Notepad`; serial logged the VFS open. The actual data image changed from SHA-256 `2e39d5b58e06881dd818e305cff3490af5d5986e96c6a5cf7e594a89665062b2` to `1b7c95dd9f49647c27b3aeac1c40576cd30dfffcc5318984d2a45530b2fd6daf`.
- A later in-memory typing step was not saved. An oversized relative mouse command caused the interactive runner to enter `RecoveryRequired`; QAC later independently reported the exact reservation `Released` with reason `Registered child process exited`, zero managed/foreign QEMU, zero reservations, and no QEMU process. This run did not complete a normal guest shutdown and is not treated as an accepted run.

### Reopen/remount and normal shutdown — run `s22r22-persistence-boot2-clean-20261112-b56e537a2b9947019ace49a5cd31295c`

- QAC reservation `ec428afb7c0b45e5b72ff43e656b8c2c`; QEMU PID 29648; QMP `127.0.0.1:49947`. The runner verified the existing data-image SHA before launch.
- The same stable ATA identity was detected and the same volume mounted at `/Documents`. Guest File Explorer showed `UNTITLED.TXT`, 161 bytes, on that volume. This is a guest-visible post-cold-boot remount of the previously saved file.
- PacMan launched through the approved `/Apps` source. Serial recorded an approved entry, loaded package resources, presented frames, processed key events, performed lifecycle cleanup, and emitted `[PACMAN-SMOKE] result=PASS proof=approved-entry-clean-return-presented-frame-key-events`.
- The user selected the guest Shutdown action and confirmed it. Serial recorded `[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop expected=1 runtime=none app=none`.
- Immediately after the guest stopped, the runner attempted a QMP screenshot and received `VM not running`; it retained the execution as `RecoveryRequired` before recording runner-verified child exit/result. QAC independently reported reservation `Released` and zero QEMU/reservations. The guest's shutdown marker is retained, but the runner result is incomplete.
- With QEMU absent, the same image was read-only inspected using the pinned `pyfatfs 1.1.0`. The FAT root listed `UNTITLED.TXT`; size was 161 bytes and contents exactly matched the bytes documented in the implementation report. This is direct backing-image evidence, not a mocked provider.

### Cold boot after guest shutdown — run `s22r22-persistence-boot3-verify-20261112-d14cad1f7ac848c7a1757f2bc2591364`

- QAC reservation `5c3e284627b5435fb3955c31784bde8c`; QEMU PID 27320; QMP `127.0.0.1:57271`.
- The exact candidate booted again, selected `GXOS-DATA-V1`, remounted `/Documents`, and File Explorer showed `UNTITLED.TXT` at 161 bytes. The image was not reset or recreated. This follows the prior guest-requested shutdown and is cross-boot persistence evidence.
- An extra interactive attempt to type an unknown runner command (`document`) caused the QAC script to retain the execution as `RecoveryRequired`. QAC later independently reported the reservation released and zero QEMU/reservations. The final backing image SHA-256 was `2207ffba77f06384698e5daa08770741212cc796f926abb46564dbe76e9c500c2`; the read-only FAT inspection again found the unchanged 161-byte document.
- This third run did not complete the requested second-file or document-modification interaction, and its runner did not record a guest result PASS.

The test image's identity changed across guest runs as FAT contents and an `NvVars` entry were written. It remained one continuous test image. The observed file contents were unchanged across the first save, the guest-requested shutdown, and later cold boots. These runs do not establish power-loss behavior.

## Security, hosted, and freestanding evidence

The candidate's logs showed `/Apps` on the approved read-only release ramdisk with only PacMan discovered. The second cold boot passed the approved PacMan entry/return/lifecycle smoke. Current-source hosted Native ELF policy tests passed 27 cases, including writable alias/source rejection before native entry, with package/resource/ABI checks retained. The exact guest did not run a synthetic writable-disk PacMan spoof attempt; that negative is hosted evidence only.

Image-backed FAT/VFS passed 32/32; user-data policy 9/9; VFS mount table 19/19; native filesystem/block generation 5/5; storage/system/device bridges 15/15, 82/82, and 14/14; focused QAC S22R22 profile 8/8; ABI layout passed; AMD64 freestanding build and symbol audit passed. Static stack estimates retained at least 4,096 bytes of 16 KiB headroom; they are not runtime high-water measurements. Full current-source results and operation semantics are in [the implementation report](RELEASE_S22R22_DOCUMENTS_IMPLEMENTATION.md).

## Limits and disposition

Observed: a real guest Save As wrote a document to the separate disk; a subsequent guest cold boot listed it at `/Documents`; a guest-requested normal shutdown was recorded; a later independent candidate cold boot listed it again; read-only inspection of the backing FAT image verified all 161 bytes. PacMan ran from the approved read-only `/Apps` source on the second boot.

Still unverified for a complete guest acceptance: the QAC runner did not record a fully completed accepted run, the full contents were not reopened and checked inside Notepad after boot, a saved modification was not demonstrated, a second file was not created, and a final guest result/child-exit lifecycle was not recorded by the runner. Therefore the outcome is **`PERSISTENCE_IMPLEMENTED_GUEST_PENDING`**, not guest-qualified Outcome A. There is no demonstrated subsystem defect requiring Outcome C.

No power loss, host crash, forced reset recovery, corruption repair, or crash-consistency guarantee was tested. FAT32 has no journal in this implementation. The application-settings handoff remains S22R23 work; recent-app history stays optional. Model B remains a draft contract, not a security/publication approval. Residual privileged PacMan risk, signing-key custody, distribution trust, and publication authorization remain open.

## QAC cleanup and repository state

After the runs, QAC 2.0.0 reported Online with 0 QEMU processes (0 foreign), 0 managed sessions, 0 queued requests, and 0 reservations. The pre-existing untracked `assets/Backgrounds/troncity.png` remains preserved. The validated source/docs are committed locally on `main`; no push or release-state change was made.
