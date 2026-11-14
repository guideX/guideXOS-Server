# Server v0.3 — S22R23 Application State Implementation

**Status:** implementation committed locally; hosted and freestanding validation passed; candidate built and verified; guest outcome A accepted after two QAC-managed cold boots.
**Work began from:** `main` at `760c708c0cf9a703c61a7c72ee035d98c0c9dec4`.
**Release profile:** `ServerV03PublicPreviewPacManOnly`.

## Scope selected from the inventory

The supported workflow is the built-in Modern Settings Personalization page. Its existing controls change and apply a packaged wallpaper/gradient ID and visibility for the Trash, File Explorer, System Settings, and Home system icons. The desktop owner already provides the application effect and Settings already reports save results. S22R23 redirects this specific persistence path to `/AppData` and restores it during boot after the data volume is mounted.

The implementation does not add settings, make the Legacy Display Options configuration tab supported, change Navigator bookmarks, create a Notepad preference file, or add preferences to PacMan. Notepad's existing document workflow continues to use `/Documents`. See the [state inventory](RELEASE_S22R23_APPDATA_PERSISTENCE_INVENTORY.md) for the examined applications and reasons these other items were not selected.

## `/AppData` contract

- Store: `/AppData/DESKAPP.CFG` (`DESKAPP.CFG` is FAT 8.3-compatible).
- Format header: `GUIDEXOS-DESKTOP-STATE`; schema version: `1`.
- The file is bounded to 512 bytes, with at most 511 content bytes. Only the recognized wallpaper and desktop-icon keys are interpreted. Existing unrelated lines are retained on updates.
- Wallpaper values must match an existing built-in wallpaper/gradient ID. Icon settings must be valid booleans. Duplicate, malformed, oversized, unsupported-format, or unsupported-version state is rejected as a whole; boot keeps the trusted default or legacy read-only fallback and emits a diagnostic. It does not erase or reformat the user volume.
- A missing data volume or settings file means no AppData override. The desktop remains usable with its built-in/read-only default state. Saving without writable `/AppData` returns a persistence failure; there is no in-memory “saved” fallback.
- Apply confirms the active desktop value and rereads the persisted value before reporting success. Failed saves use best-effort restoration of old bytes; an unsuccessful first save attempts to remove the newly created file. A failed restoration is surfaced as failure.
- The current FAT/VFS writer truncates and writes in place. Readback detects ordinary short, failed, or mismatched writes, but this is not an atomic replacement protocol and does not promise crash consistency or power-loss durability. A power loss during overwrite can leave invalid state, which boot will ignore rather than silently claim as restored.
- `/AppData` remains user data. Nothing in this change makes it a Native ELF package source or changes the trusted, read-only `/Apps` authorization policy.

## Source changes

- `kernel/core/desktop_gradient_persistence.cpp` and its header now parse, validate, load, and save the versioned AppData format. The writer preserves unknown lines and verifies write/readback; unavailable and read-only storage are reported distinctly from content rollback.
- `kernel/core/desktop.cpp` loads persisted wallpaper and icon overrides through the desktop owner. Wallpaper IDs are checked against the built-in catalog. Missing/invalid state retains the default/fallback. The icon reload starts from the trusted built-in defaults before overlaying valid persisted fields.
- `kernel/core/main.cpp` no longer tries to remember a writable root storage cache and invokes the persisted desktop reload after the data-volume mount is established.
- The settings persistence tests cover missing volume/file, read-only storage, format/version/content validation, duplicate keys, preserving unknown fields, repeat saves and reload, partial/readback failure, and rollback paths including a full-volume result.
- The image-backed FAT/VFS test writes a copy of the accepted S22R22 data image, verifies `/Documents` and `/AppData` coexist without namespace shadowing, reloads AppData after remount, and confirms the existing Notepad document bytes remain unchanged.
- Native ELF policy tests reject AppData configuration names and traversal paths as approved executables and reject writable data-volume sources as trusted package sources.
- The stack budget guard includes the actual Settings Apply-to-AppData paths.

## Hosted validation

All artifacts were written under `E:\CodexArtifacts\S22R23`; the accepted source data image was read-only input and all FAT mutations were made on copies.

| Check | Result | Evidence/output |
|---|---:|---|
| Settings native runtime persistence | 908/908 passed | `settings-native-runtime-20261113-05` |
| Image-backed FAT/VFS and data-volume coexistence | 44/44 passed | `fat-persistence-20261113-04`; the mutated image is `hosted-data-volume-copy.img` |
| Native ELF security policy | 30 cases passed; 0 failed; 4 synthetic approved entry calls | `native-elf-policy-20261113-02` |
| QAC launcher regression | 166/166 passed | `tests/qac_s22r10_uefi_launcher_test.ps1` |
| S22R22 QAC profile contract | 8/8 passed | `tests/qac_s22r22_persistent_preview_test.ps1` |
| User-data volume mount policy | 9/9 passed | `hosted/user-data-volume-policy.exe` |
| VFS mount integrity | 19/19 passed | `hosted/vfs-mount-integrity.exe` |
| VFS hosted read | 4/4 passed | `hosted/vfs-hosted-read.exe` |
| Native ABI layout | passed | `hosted/native-abi-layout.exe` |
| Settings Native layout | 1,037/1,037 passed | `settings-native-layout` |
| Native filesystem contract | passed | `hosted/native-filesystem-contract.exe` |
| `git diff --check` | passed | clean whitespace check before commit |

The compiler emitted an existing unused-helper warning in the settings runtime translation unit; it did not fail the test build. Test executables and logs are validation evidence, not release inputs.

## Freestanding and stack validation

The AMD64 freestanding kernel was built in the ordinary clone `E:\CodexArtifacts\S22R23\precommit-validation-source`, with Mbed TLS copied from the ignored pinned dependency tree. The build used the Public Preview security profile and GCC stack-usage/callgraph reporting (`-fstack-usage -fcallgraph-info=su -Wframe-larger-than=4096 -Wstack-usage=8192`). It completed successfully. The resulting audited kernel ELF was 2,812,208 bytes and the PE image 2,936,074 bytes at this validation point; this pre-candidate clone is evidence only, not the release candidate.

The Native ELF symbol audit found 50 undefined-symbol rows and 22 distinct symbol-table entries; there were no kernel relocations or new host-runtime imports. `tests/boot_stack_budget_guard_test.ps1` passed from compiler `.su` records:

- Settings display guarded path: 11,480 / 16,384 bytes; 4,904 bytes headroom.
- Settings AppData Apply path: 8,424 / 16,384 bytes; 7,960 bytes headroom (wallpaper and icon save paths checked).
- Legacy Display Options guarded path: 11,864 / 16,384 bytes; 4,520 bytes headroom.
- COM2 guarded path: 7,736 / 16,384 bytes; 8,648 bytes headroom.

Each guarded path retains the required 4,096-byte static margin. These are compiler-derived static call-path estimates with explicit reserves; they are not runtime stack measurements.

## Candidate and guest status

The local implementation commit `67dc2e111ea4d6e7208a8d5fac7dbd1a389d8491` was built in an isolated clone with `ServerV03PublicPreviewPacManOnly`; build ID `4c9988cf-5d0d-414d-afe8-81f7351c2706`; source worktree clean at build and packaging. The candidate is `E:\CodexArtifacts\S22R23\candidate-source\out\release-profile\4c9988cf-5d0d-414d-afe8-81f7351c2706\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso`, 90,245,120 bytes, SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`. The embedded ESP is 90,177,536 bytes, SHA-256 `e5e48c21df052c38887291f8beb20b55f69ed7adde24667fc70d0d9d16a57101`; all 16 manifest inputs passed the release ESP verifier. Embedded kernel ELF is 2,812,208 bytes, SHA-256 `9d2f17c33f157a566b6ee513196b5b74831bf8532699be78538b5979886fc166`. The release manifest contains only PacMan under `S22R18-PacMan-ReadOnly-Boot-Image-v1`; the build identity and tool versions are recorded in [guest qualification](RELEASE_S22R23_APPDATA_GUEST_QUALIFICATION.md).

**Guest outcome A — `APPDATA_PERSISTENCE_GUEST_ACCEPTED`.** Two separate QAC 2.0.0 reservations booted the exact candidate under QEMU 11.0.0 with the same dedicated writable copy of the accepted data volume. Modern Settings saved Ocean, reopened with Ocean selected in the first boot, and the second cold boot restored Ocean in both the desktop and Settings before a second save changed it to Forest. Offline read-only inspection confirmed the final `DESKAPP.CFG` contents and that all five existing document files retained their original sizes and hashes. PacMan passed the approved read-only Native ELF launch, input, frame, clean-return, and live QAC smoke-marker path in both boots. Both guests shut down normally, exited QEMU with code 0, and released their reservations. The accepted S22R22 ISO (`efa290538ef3155d264bf0b26923933bd2c9332aa5574e5be977be9d7a9c948c`) and original S22R22 data image (`3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148`) remain preserved; the writable test copy and run evidence remain under `E:\CodexArtifacts\S22R23`.

The guest run demonstrates wallpaper/gradient persistence across cold boots. Icon-visibility paths are covered by hosted persistence tests, but were not independently toggled and restored in the guest. No durability claim is made for sudden power loss: the FAT write is in-place, with readback and best-effort rollback rather than atomic replacement. See [the guest qualification record](RELEASE_S22R23_APPDATA_GUEST_QUALIFICATION.md) for QAC identities, serial markers, data-image hashes, file-level results, candidate provenance, and the remaining scope.

## Coverage limits and owner decisions

Persistent settings coverage is wallpaper/gradient ID and the four desktop system-icon visibility flags only. Legacy Display configuration is explicitly unsupported. Navigator bookmark persistence is not claimed: the current nine-character base filename is incompatible with FAT 8.3, its parent-directory handling is incomplete, and the UI can mask a failed save. Hosted Settings writes host-relative `desktop.json`, not guest `/AppData`. Notepad has no separate preference state; its documents remain under `/Documents`. PacMan has no implemented persistent settings or scores. Taskbar layout, desktop shortcuts, theme, recent-app history, accounts, resolution controls, and multi-monitor state are outside this selected workflow.

Model B Public Technical Preview remains the target. OpenSSH SSHSIG over a signed manifest is the planned signing method, not an approved final contract. The system environment is disposable/rebuildable. Persistent documents, user files, and supported application settings/data remain required. Unqualified hardware is not arbitrarily blocked, while real security and destructive-storage safeguards remain. Single-monitor resolution controls are deferred; multiple monitors are deferred to v0.3.1 or v0.4. QEMU is the formal reference environment. Prior VirtualBox use and bare-metal USB boot remain separate evidence; physical-disk installation and Disk Manager behavior are not qualified.

Residual Native ELF security risk acceptance, production signing-key creation/custody, publisher-key distribution, official publication channel, final release contract, and public release authorization remain unapproved. No signing, publication, promotion, release-readiness change, or `dist` modification is part of this implementation.

## Kernel survivability handoff

The owner identifies recovery from an unresponsive Native ELF app as very high priority. This needs a dedicated architectural phase that distinguishes cooperative shutdown, CPU-bound infinite loops, preemptive scheduling, forced termination, resource cleanup, and arbitrary memory-corruption protection. The PacMan-only integrity policy is not application isolation, and a watchdog alone would not establish memory safety. Schedule architecture discovery and a concrete v0.3 plan before production-readiness decisions; do not expand this bounded S22R23 persistence change into a scheduler or process-isolation redesign.
