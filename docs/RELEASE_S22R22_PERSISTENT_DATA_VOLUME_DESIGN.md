# S22R22 — Persistent Data Volume Design

**Disposition: Outcome B — `PERSISTENCE_IMPLEMENTED_GUEST_PENDING`**  
**Assessment date:** 2026-11-12  
**Repository:** `D:\dev\guideXOSServer`, branch `main`  
**Starting HEAD:** `56057228c9b5ab1a1d313c776b0af0313e8c76e3`  
**Validated source HEAD:** `eb61ca10b48c0e96e1a04a302e82d6c52b828f41`

This phase implements real FAT32 user storage on a separately attached writable ATA disk. Hosted and freestanding checks pass. The new exact candidate booted under QAC and retained a saved document across a guest-requested shutdown and a subsequent cold boot. The interactive QAC runner ended those runs in `RecoveryRequired`, so the formal guest acceptance gate remains pending. See [guest qualification](RELEASE_S22R22_PERSISTENCE_GUEST_QUALIFICATION.md).

## Repository provenance and preservation

The initial checkout was `main` at the expected S22R21 HEAD, with no tracked changes. The existing untracked `assets/Backgrounds/troncity.png` was preserved. No reset, clean, stash, rebase, branch switch, worktree creation, merge, cherry-pick, push, publication, or readiness-marker change was performed.

Implementation was committed in three local commits after the starting HEAD:

- `573750133e906504cc67ff4368d90aad2d472cb8` — persistent data-volume support.
- `38203317d82cada40992b8a4a859a61beed090f0` — canonical build PowerShell utility import.
- `eb61ca10b48c0e96e1a04a302e82d6c52b828f41` — verify the writable test image before QEMU opens it.

At report time, tracked files were clean and the only untracked file was the preserved wallpaper. Previous candidates, reports, and external artifacts remain in place. `dist`, signatures, and publication/readiness state were not changed.

## Existing storage and VFS findings

The kernel already had ATA PIO block devices, a FAT16/FAT32 driver, block flush support, VFS mount points and aliases, and file APIs. VFS exposes open/read/write/seek/close/flush, create/append/truncate helpers, directory creation, stat, and enumeration. The appropriate change was to connect the existing stack to a specifically identified ATA data volume and improve the existing FAT/VFS write-result path; no parallel filesystem or mock was added.

S22R13/S22R14 found why the previous `/Documents` path did not work: the FAT driver creates short 8.3 names only, and `Documents` has a nine-character base. FAT returned `FAT_CREATE_INVALID_NAME`; the VFS reported the directory creation failure. This phase mounts the data volume's FAT root at the virtual mount point `/Documents`; it does not create a FAT directory named `Documents` and does not add long-filename support.

The v0.3 public-preview system root and `/Apps` remain on the immutable release ramdisk. `/system` is an alias to that root. The separate disk mounts at `/Documents`; `/AppData` is a VFS alias to `/Documents/APPDATA`. VFS resolves the most-specific mount/alias for those paths. The data mount does not overlap `/Apps`, does not replace the root, and is never accepted as the Native ELF package source.

## Data-volume contract

| Item | Contract |
|---|---|
| Trusted system | Read-only AMD64 UEFI ISO and read-only release ramdisk; `/Apps` must remain on the approved read-only ramdisk. |
| Writable disk | One separately attached ATA PIO block device with stable transport identity `GXOS-DATA-V1`; the enumerated device name/index is diagnostic only. |
| Filesystem | FAT32 across the whole virtual disk, with sector size 512, offset 0, label `GXOSDATA`, marker `GXOSDAT.ID` containing `guideXOS-user-data-v1\n`, and an `APPDATA` directory. |
| VFS paths | FAT root mounted at `/Documents`; `/AppData` aliases `/Documents/APPDATA`. |
| Write permission | Writable only on the identified data device. The system ramdisk and package source remain read-only. |
| Compatibility | The data-volume marker is independent of the ISO build ID. A compatible future system image can reuse the same v1 FAT volume without copying it into the system image. |
| Execution policy | The data volume and all its paths remain unauthorized as Native ELF sources, including aliases. |

Discovery scans active block devices for exactly one matching stable ID, excludes the trusted system device, and then requires ATA PIO, a readable/writable device, 512-byte sectors, and a flush callback. It validates device generation, whole-disk geometry, FAT32 type, label, marker bytes, and `APPDATA` before completing the mount. The selected identity, device name, generation, geometry, filesystem, and label are logged. A mount that already exists is accepted only when its mount, alias, device, generation, filesystem, and writable flags all match the expected state.

Missing or duplicate identities, a read-only/no-flush device, system-source collision, invalid or unsupported FAT, incompatible marker/label, missing `APPDATA`, path collision, or a changed device generation produces an explicit mount failure. Any newly created `/Documents` or `/AppData` mount is rolled back on failure. There is no fallback to another disk and no automatic formatting of an existing disk.

## Provisioning and recovery boundaries

[`scripts/provision-user-data-volume.py`](../scripts/provision-user-data-volume.py) provisions only a new regular `.img` file beneath `CodexArtifacts`. It uses exclusive creation, accepts 64–2048 MiB (128 MiB default), and writes the FAT32 label, marker, and `APPDATA` directory. It refuses existing paths and raw-device/GlobalRoot paths. On a provisioning error it removes only the file created by that invocation. The guest mount path never formats or repairs a volume.

For an absent volume, create a new image with the helper and attach it explicitly. For an existing volume, keep a separate backup before any external repair attempt. A corrupt, read-only, or incompatible volume is reported as unavailable; recovery is an operator action. There is no journal, automatic repair, or supported filesystem migration in this phase.

## Write and commit semantics

Notepad and other callers use the existing VFS/FAT path. `write_file` opens with create+truncate, writes bounded 4096-byte chunks, rejects errors and short writes, and returns the requested byte count only when the write and close/flush both succeed. FAT directory/cluster/size metadata updates use the existing block write path. VFS `close` flushes writable FAT32 mounts; flush failures map to I/O or timeout errors. `create_file` is exclusive, `append_file` appends, and `mkdir` reports the underlying FAT creation status.

These calls provide ordinary guest write/close semantics and were observed persisting through a guest-requested shutdown and a fresh guest boot. They do not make FAT32 transactions atomic. There is no claim of power-loss durability, crash consistency, journal recovery, or recovery from a torn metadata update.

Filenames use the current driver's short-name rules: 8.3 form, no silent truncation, and case-insensitive FAT lookup. Long names, names with unsupported punctuation, and base names longer than eight characters are rejected. The data-volume mount point itself is virtual, so `/Documents` works despite the existing 8.3 restriction.

## Native ELF boundary

S22R18/S22R21 policy remains in force: the release profile contains only the approved PacMan Native ELF package; `/Apps` is accepted only when it resolves to the read-only release ramdisk and its verified identity. The loader rechecks package identity, resources, source mount/device/generation, and ABI compatibility before entry. The data volume does not get an executable authorization token or trusted-source identity. Writable aliases and path normalization do not authorize it as `/Apps`.

The focused hosted policy suite still rejects writable aliases, source substitution, and mismatched devices/generations before a native-entry sentinel is called. The guest logs show `/Apps` on the approved system source and PacMan's normal approved execution. A synthetic matching PacMan package was not staged on the writable disk for a live guest rejection attempt; that negative case is hosted evidence. The kernel-privileged PacMan residual risk remains unchanged.

## Application settings handoff

S22R22 adds a storage foundation, not a settings migration. The current desktop shell has filesystem persistence code, but its configured paths are under the read-only public-preview root, so its write attempts are not persistent on this profile. The established paths include `/desktop.cfg`, `/display-options.cfg`, `/wallpaper.id`, `/desktop.wallpaper.id`, `/desktop.taskbar.position`, `/desktop.background.scale`, `/desktop.system.icons`, `/desktop.shortcuts`, `/.desktop_icons`, and `/.startmenu_recent`. They store text/configuration, shortcut records, icon layout, or recent-app data. Recent-app history remains optional and was not moved or made a release requirement.

Notepad's document contents are user files, saved explicitly through Save/Save As to `/Documents`; the guest Save As created `/Documents/untitled.txt`. Settings/Control Panel currently has no validated per-profile persistence path for the public preview's read-only System view, and S22R21 explicitly marks legacy display configuration unsupported. No PacMan score/settings save path was identified as a supported v0.3 feature. See [the implementation and settings audit](RELEASE_S22R22_DOCUMENTS_IMPLEMENTATION.md).

S22R23 should route only supported shell/application settings to valid 8.3 paths under `/AppData`, report write failures to the user, and define migration from existing root paths without changing `/Apps`. It should test reload after a full shutdown/cold boot. Recent history remains outside scope unless the owner changes its status.

## Model B contract amendment

The draft contract now needs to describe a disposable, replaceable system VM with a separately attached persistent user-data volume, immutable read-only trusted system media, writable `/Documents` and only explicitly supported `/AppData` state, no writable Native ELF source, no host shared folders or USB passthrough, and no third-party native application support. It must explain explicit volume initialization, missing/corrupt/incompatible-volume behavior, backup/recovery limits, and the absence of crash/power-loss durability claims. This design is not yet an approved final deployment profile.

Pending owner decisions remain: privileged Native ELF risk acceptance, signing-key custody and verification-key distribution, distribution trust/channel, final deployment-contract approval, and publication authorization naming the exact final artifact identity.
