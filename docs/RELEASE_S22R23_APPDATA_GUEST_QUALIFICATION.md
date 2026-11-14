# Server v0.3 — S22R23 AppData Guest Qualification

**Outcome A — `APPDATA_PERSISTENCE_GUEST_ACCEPTED`.** Modern Settings restored and applied a saved wallpaper/gradient across two independent QAC-managed cold boots on the same persistent FAT32 volume. The existing document files remained byte-for-byte unchanged, and the PacMan-only Native ELF path passed in both guests.

## Candidate provenance

- Repository: `D:\dev\guideXOSServer`, branch `main`; candidate source commit `67dc2e111ea4d6e7208a8d5fac7dbd1a389d8491`. The isolated candidate build and packaging both report a clean source worktree.
- Release profile: `ServerV03PublicPreviewPacManOnly`; build ID `4c9988cf-5d0d-414d-afe8-81f7351c2706`; security policy `S22R18-PacMan-ReadOnly-Boot-Image-v1`. The canonical command was `pwsh -NoProfile -File scripts/create-release-iso.ps1 -Version 0.3.0 -Arch amd64 -ReleaseProfile ServerV03PublicPreviewPacManOnly -BootstrapTools`.
- Candidate ISO: `E:\CodexArtifacts\S22R23\candidate-source\out\release-profile\4c9988cf-5d0d-414d-afe8-81f7351c2706\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso`; 90,245,120 bytes; SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`.
- Embedded `UEFI_BOOT.IMG`: 90,177,536 bytes; SHA-256 `e5e48c21df052c38887291f8beb20b55f69ed7adde24667fc70d0d9d16a57101`. `tools/release_esp.py verify` passed for all 16 manifest inputs extracted from this exact ISO.
- Embedded kernel ELF: 2,812,208 bytes; SHA-256 `9d2f17c33f157a566b6ee513196b5b74831bf8532699be78538b5979886fc166`. Symbol-audit file SHA-256 `ee8826759ddfd369beb97a5150a13bf94a2a24f0d54289e1ffbf082795163eac`.
- Package manifest contains only PacMan. `Apps/PacMan/app.json` SHA-256 `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449`; ELF SHA-256 `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc`. Build stages bootloader, kernel, symbol audit, ramdisk, PacMan, and app packages passed; DeveloperStudio was not selected.
- Build receipt records Git 2.54.0, PowerShell 7.6.9, Python 3.12.10 with pyfatfs 1.1.0, fs 2.4.16, and pycdlib 1.16.0; GCC 15.2.0, GNU ld 2.46.0.20260210, NASM 2.16.03; Mbed TLS profile SHA-256 `960888bc747745d97fe104e0552c2602eeb57ef32b4f79f2d7bfa6121a945055`.

## QAC setup and preserved baseline

The original accepted S22R22 ISO remains identified by SHA-256 `efa290538ef3155d264bf0b26923933bd2c9332aa5574e5be977be9d7a9c948c`. Its accepted 128 MiB data image, `E:\CodexArtifacts\S22R22\persistent-data\guidexos-user-data.img`, remains SHA-256 `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148`.

Both guests used the new candidate ISO, QEMU 11.0.0, QAC 2.0.0, and read-only OVMF firmware SHA-256 `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a`. The exact candidate ISO and firmware were read-only, guest networking was disabled, and each boot used the same writable raw test image with ATA serial `GXOS-DATA-V1`. The test image was a copy of the accepted source, initially byte-identical; the accepted source was not attached for writing. No writable firmware-variable store or host filesystem integration was attached.

QAC was online before launch with zero managed guests, sessions, queued requests, or blocked requests. No foreign QEMU process was present. Each run used the S22R22R1 ownership-checked QAC launcher, its private QMP endpoint, its own evidence workspace, and normal shutdown through the guest desktop. Both executions recorded `guestResult=PASS`, verified QEMU child exit code 0 and QAC release.

## Boot 1 — apply and reopen in the same boot

- Run `s22r23-appdata-coldboot1-20261113-80feedebbe874a76a1f35cd57d639af7`; reservation `08af1e49d3ee46e995e4d4162b714b53`; QEMU PID 5884; private QMP `127.0.0.1:64857`.
- Before launch the writable test copy was 134,217,728 bytes and matched the accepted source SHA-256 `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148`.
- Serial reported `/Documents` and `/AppData` mounted from the same FAT32 volume, ATA device `ata1s`, stable ID `GXOS-DATA-V1`, label `GXOSDATA`. The initial desktop used the packaged Blue Flower default.
- In Modern Settings > Personalization, Ocean was selected and applied. Serial reported `[settings-native] background.apply status=0 active=gradient_ocean persisted=gradient_ocean`; the Ocean desktop was visible. Settings was closed and reopened in the same boot and again showed Ocean selected. Screenshots include `ocean-applied.png`, `ocean-reopen.png`, and the corresponding settings views.
- File Explorer displayed the existing `/Documents` entries, and the built-in Notepad window was opened. No user document was edited. PacMan was launched from the Start menu, received guest keyboard input, returned through Escape, cleaned up, and emitted `[PACMAN-SMOKE] result=PASS proof=approved-entry-clean-return-presented-frame-key-events`. The runner observed the marker live before recording PASS.
- The desktop shutdown confirmation was accepted. Serial ended with `[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop expected=1 runtime=none app=none`. QAC verified child exit and released the reservation. The stopped test-image SHA-256 became `4a697c43b93d2ab49630182cd13a6f360b0cb2e6f2fd529caa95317c00d049b9`.

## Boot 2 — independent cold-boot restore

- Run `s22r23-appdata-coldboot2-20261113-e153bb16aa9548ee88a3d68f9a8f8758`; reservation `c481bdaac93f4e09b7bd8a7ab4e040a1`; QEMU PID 24060; private QMP `127.0.0.1:50513`.
- The same test image was reused without reset. Its exact prelaunch SHA-256 was `4a697c43b93d2ab49630182cd13a6f360b0cb2e6f2fd529caa95317c00d049b9`, matching the stopped Boot 1 image.
- Serial again reported the expected `/Documents`, `/AppData`, FAT32, and `GXOS-DATA-V1` mount. Before opening Settings, the desktop restored Ocean; serial recorded `[desktop] wallpaper load id=gradient_ocean source=AppData` and `[desktop] wallpaper apply source=persisted id=gradient_ocean`. Modern Settings then showed Ocean as the selected value. Evidence includes `restored-desktop.png` and `restored-settings.png`.
- Forest was selected and applied during Boot 2. Serial reported `[settings-native] background.apply status=0 active=gradient_forest persisted=gradient_forest`; the Forest desktop rendered. This exercised a second save after cold-boot restore.
- PacMan again launched from Start, presented its game, accepted keyboard input, returned cleanly, and emitted the live PASS marker. Serial also recorded `[NATIVE-ELF-PREVIEW] check=PASS package=com.guidexos.pacman identity=sha256 source=readonly-boot-ramdisk snapshot=verified`. Thus writable `/AppData` did not become an executable source and approved `/Apps` remained the read-only boot image.
- Guest desktop shutdown completed normally with exit code 0. QAC verified child exit and released the reservation. The coordinator then reported online, zero QEMU processes (zero foreign), zero sessions, zero queued and blocked requests; the inspected reservation state was `Released`, `degraded=false`, owner liveness `Verified`, reason `Managed child exited.`

## Offline stopped-image inspection

The stopped 134,217,728-byte test image was opened read-only. `/APPDATA/DESKAPP.CFG` is 68 bytes with SHA-256 `83ae95ae4bdb79042289c1340361bc6965d1f86673f22f3dd3a0dca2ddb0cf79` and exact ASCII contents:

```text
format=GUIDEXOS-DESKTOP-STATE
version=1
wallpaperId=gradient_forest
```

All five pre-existing files matched their pre-run sizes and SHA-256 values:

| Path | Bytes | SHA-256 | Result |
|---|---:|---|---|
| `/SECOND.TXT` | 40 | `fd9672170e1bf867eff060be3d5980e7a9efbe1fde7bdec8058b78b6aaebea34` | unchanged |
| `/SECOND2.TXT` | 38 | `72f7ee87bad8ef3a2076dbf18e9ed9cc168f6fcc144b52594481a54bbd34de1b` | unchanged |
| `/SECOND3.TXT` | 38 | `8dbf3b7aba257e9b090dea1e9337857a2ee93eb494aa860584ff27be0790c165` | unchanged |
| `/UNTITLED.TXT` | 49 | `780851faf6dcc16eef51b63dd46d7351ac8558b0db31a71be3b7744c6f7612c2` | unchanged |
| `/APPDATA/UNTITLED.TXT` | 40 | `fd9672170e1bf867eff060be3d5980e7a9efbe1fde7bdec8058b78b6aaebea34` | unchanged |

The final whole-image hash of the dedicated writable test copy is `a07ae96fb401e7abf6405e8c8e9d944bf56ca2aa62f4e7f031947d76f5c35b7c`. The whole-image hash changed as filesystem metadata and the settings file were written; the file-level document checks above establish that existing document contents did not change.

## Results and scope

| Evidence | Result |
|---|---|
| New candidate ISO, embedded ESP, package set, kernel identity | pass; exact hashes above |
| QAC preflight and two independent managed cold boots | pass; both guests exited 0 and reservations released |
| Settings Apply, same-boot reopen, and Boot 2 restored desktop/selection | pass; Ocean restored and used after cold boot |
| Second Boot 2 setting save and offline AppData readback | pass; final Forest value is valid and persisted |
| PacMan execution integrity and behavior | pass in both boots; live marker, clean return, input and rendered frames |
| Existing documents and `/Documents`/`/AppData` coexistence | pass; five file sizes and hashes unchanged |
| Preserved accepted data image and source wallpaper | pass; original data image and `assets/Backgrounds/troncity.png` retain their recorded source hashes |
| QAC final coordinator/ownership state | pass; online and idle, both reservations released, no QEMU children remain |

The guest setting demonstrated across cold boots is the desktop wallpaper/gradient ID. The implementation and hosted persistence tests also cover visibility of Trash, File Explorer, System Settings, and Home icons, but icon-visibility restore was not separately exercised in these guest runs. Navigator bookmark persistence, Notepad preferences, PacMan preferences/scores, recent-app history, theme, taskbar layout, and other listed non-selected state are not claimed. The Legacy Display Options configuration tab remains unsupported.

The write path truncates and rewrites the FAT file in place. Readback and best-effort rollback handle ordinary write failures, but this is not an atomic replacement protocol and does not establish crash consistency or power-loss durability. Corrupt or unsupported state is rejected in favor of a trusted default/read-only fallback.

The evidence workspaces are under `E:\CodexArtifacts\S22R23\qac\s22r23-appdata-coldboot1-20261113-80feedebbe874a76a1f35cd57d639af7` and `E:\CodexArtifacts\S22R23\qac\s22r23-appdata-coldboot2-20261113-e153bb16aa9548ee88a3d68f9a8f8758`; each retains its manifest, execution record, QAC logs, QMP transcript, serial and QEMU logs, and screenshots.

## Owner decisions and next phase

Model B Public Technical Preview remains the selected target; QEMU remains the formal reference qualification environment. OpenSSH SSHSIG over a signed manifest remains the planned signing approach, not an approved final release contract. The environment is disposable/rebuildable while persistent documents, user files, and supported application settings/data remain required. No signing, publication channel, production key custody, publisher-key distribution, residual Native ELF risk acceptance, or public-release authorization was approved by this work. No signing, publishing, promotion, readiness change, or `dist` modification was performed.

The owner’s very-high-priority Native ELF survivability requirement needs a separate architecture phase covering cooperative shutdown, CPU-bound loops, preemptive scheduling, forced termination, resource cleanup, and memory-corruption protection. Schedule that phase before production-readiness decisions; PacMan-only integrity does not provide process isolation, and a watchdog alone would not provide memory safety. Keep single-monitor resolution controls deferred and multi-monitor work in v0.3.1 or v0.4 as previously decided.
