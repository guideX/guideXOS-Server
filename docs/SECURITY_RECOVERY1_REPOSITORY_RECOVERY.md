# SECURITY-RECOVERY1 — Repository Recovery & Continuity

- **Date:** 2026-11-26
- **Project:** guideXOS Server
- **Workstream:** Security Settings
- **Outcome:** `SECURITY_RECOVERY1_PARTIAL`

## Summary

The recreated E: checkout is structurally healthy and begins at the independently confirmed GitHub `SECURITY_SETTINGS` checkpoint. All historical milestones through SECURITY3G-R1 are present. The reported SECURITY3G-R2 and SECURITY3G-R3 commit objects, R2/R3 reports, and R2–R4 qualification captures were not found in the audited recovery sources. SECURITY3G-R3's two source repairs were therefore reconstructed from the surviving implementation and ABI constraints, then committed locally with explicit recovery provenance. Offline tests and kernel builds pass; this is not guest acceptance. R4 guest qualification remains pending.

## Repository identity and non-destructive audit

- Repository root: `E:\dev\guideXOSServer_SECURITY_SETTINGS`.
- The previous checkout path was treated as unavailable; no recovery was based on reading it.
- Starting branch: `SECURITY_SETTINGS`.
- Starting HEAD: `cc8d9d0d418036041231cfc752bc07b555fda666`.
- Starting worktree: clean; no untracked or ignored files were reported inside the checkout.
- Starting tracking relationship: `SECURITY_SETTINGS` tracked `origin/SECURITY_SETTINGS` at the same commit.
- Starting local-only commits: none.
- Starting reflog: fresh clone entries only; no earlier local branch history.
- Stashes: none.
- Unreachable objects: none reported by `git fsck --full --no-reflogs --unreachable` (exit 0).
- Object connectivity/integrity: `git fsck --full --strict --connectivity-only` passed (exit 0, no diagnostics).
- Starting worktrees: only the primary checkout. No submodules were present.
- The configured remote is `git@github.com:guideX/guideXOS-Server.git`. SSH `git ls-remote` could not authenticate in this environment. A read-only GitHub API check independently reported live `SECURITY_SETTINGS` HEAD `cc8d9d0d418036041231cfc752bc07b555fda666`, matching local `origin/SECURITY_SETTINGS`.

The full audit outputs and the starting tracked-file SHA-256 inventory are preserved in `E:\dev\SECURITY_SETTINGS_RECOVERY1_EVIDENCE`. No reset, clean, prune, force-push, or destructive checkout was used.

The named backup location `E:\bkup` exists. Its top-level inventory and workstream-named entries directly under `dev`, `devgithub`, `devgitlab`, and `artifacts` contained no guideXOS Security Settings, SECURITY3G, or recovery candidates. The known `E:\dev\TASKBAR_RECOVERY1_BACKUP_20261126` bundle was inspected separately and does not contain the missing R2/R3 objects. A recursive listing under `E:\bkup` encountered an access-denied WindowsImageBackup log directory and was stopped; that disk image was not traversed or treated as evidence.

## Git history recovery matrix

| Milestone | Historical commit | Commit recovered? | Source present? | Evidence present? | Action/status |
|---|---|---:|---:|---:|---|
| SECURITY3E scheduler | `0900235076516f8ab0f813d04789b24f2b3a81ea` | Yes, reachable | Yes | Committed history/source; no phase-specific raw capture located | Preserved |
| SECURITY3F implementation | `f082f5563d555a5efa096557fdd8fc507f82ef1d` | Yes, reachable | Yes | Committed history/source | Preserved |
| SECURITY3F qualification | `f63be397b5bad9ea07d0d45feaa5ef55896d744f` | Yes, reachable | Yes | Qualification commit is reachable; original raw evidence not independently inventoried | Preserved |
| SECURITY3G implementation | `4f8d7c48c7a386f47c85123aa0402fc44b35a9bb` | Yes, reachable | Yes | Design/status documentation survives | Preserved |
| SECURITY3G status | `b9ac8943ab1fa88fb2f7b934ee09cb22ce8d9565` | Yes, reachable | Yes | Status report survives | Preserved |
| SECURITY3G-R1 | `cc8d9d0d418036041231cfc752bc07b555fda666` | Yes, reachable and at remote baseline | Yes | R1 report survives; its referenced `out/validation` captures are absent from this checkout | Preserve report; record raw evidence gap |
| SECURITY3G-R2 | `d05f61fd80665527d6907e825cd69ec286977fbd` | No; absent from audited object stores/refs | R2 report/source additions absent | No R2 candidate or qualification directory located | Do not recreate original observations; R2 provenance work remains undocumented |
| SECURITY3G-R3 | `6117da98261fff2df066c8aaf13ef7621c2c9037` | No; absent from audited object stores/refs | The two repairs were absent at baseline and reconstructed below | No R3 report or guest capture located | Recovery reconstruction committed as `1a88c70e33eff48e7e130cf529b32d06e432743e`; not the original commit |
| SECURITY3G-R4 | No completed commit found | No | No R4 report/source outcome found | No R4 guest evidence located | `SECURITY3G_R4_GUEST_QUALIFICATION_PENDING` |

The exact R2/R3 hashes were not in the recreated checkout's refs, reflogs, or object database, and were not found in checked E: development/recovery clones and bundles or the checked C: recovery clone. The GitHub exact-commit API lookups returned HTTP 422 for both hashes. This is evidence that those objects were unavailable in the checked sources, not proof that no other offline copy exists.

## Preserved recovery evidence

The dedicated evidence directory is outside the checkout at `E:\dev\SECURITY_SETTINGS_RECOVERY1_EVIDENCE` and is intentionally retained. It includes:

- Starting refs, branch/tracking configuration, reflog, stash, worktree, submodule, untracked/ignored, and fsck results.
- `security_settings_reachable_refs.bundle`, captured before source edits, verified successfully, containing 28 refs; SHA-256 `A10525CE444B5EDA6B66AB9B42AC4DE4F4BC06B60C396468164797EB2C4EE27`.
- A preserved copy of the pre-existing all-refs bundle `prior_all_refs_bundle_20261126.bundle`; SHA-256 `DF359C179A142465FFCDC0441F050EA8586BB829CC69418256E73AD46D1A5DAA`.
- `recovery_artifact_inventory.csv`, with paths, sizes, and SHA-256 values for relevant surviving source, docs, packages, and the pre-rebuild pilot.
- `recovery_missing_paths.csv`, recording absent R2/R3/R4 reports and validation/build candidate paths.
- The original tracked R1 pilot ELF copied before replacement: 3,592 bytes, SHA-256 `868521FC6C6F66EF5FE1BC321759CBC796A4E97C7C63EA29F3E818D1F71CD483`.
- Build/test logs, produced kernel and hosted outputs, and three detached verification worktrees at the reconstructed source commit.

`out/validation/security3g-r1` through `security3g-r4` were absent in the recreated checkout. R1's committed report is present, but its historical raw candidate manifest/captures are not. R2–R4 guest candidate ELFs, kernels, ESP images, serial logs, artifact identity manifests, and QAC captures were not recovered. No replacement qualification report has been fabricated.

## SECURITY3G-R2 status

`docs/SECURITY3G_R2_GUEST_ACCEPTANCE.md` and `out/validation/security3g-r2/` are absent. No R2 commit or candidate artifacts were recovered. The recorded R2 work description (positive/negative candidates, QEMU identity correction, x64 OVMF code plus writable VARS configuration, ESP inventory and launch plans, with no guest run) remains user-supplied historical context, not independently verifiable R2 evidence.

The installed QEMU executable was independently hashed at `C:\Program Files\qemu\qemu-system-x86_64.exe`: 25,287,296 bytes, SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`, matching the corrected R1 identity. Installed OVMF x64 code at `C:\Program Files\qemu\share\edk2-x86_64-code.fd` is 3,653,632 bytes, SHA-256 `33090CC07675BAA5190D9F1E84BF5176B33BCBFA9BACAC522961150CDB6DBB2A`, matching R1. No x86_64 writable VARS template was found in the installed QEMU tree during the bounded inspection; R4 must identify and hash a suitable writable x64 VARS image before launch.

## SECURITY3G-R3 source reconstruction

The baseline had nine package directories with `app.json` files, while `kernel/core/native_elf_baremetal.cpp` bounded discovery at eight packages. The fixed capacity is now nine, matching the current inventory. It remains bounded, and the existing public-preview single-PacMan package restriction remains in place. The loader's existing fixed-table bounds and manifest limits were left intact.

In `sdk/samples/schedulerpreemptionpilot/workload.S`, the baseline stored the XSTATE output pointer at stack offset 16, then stored the XSTATE mask at offset 20, overwriting the upper half of that pointer. The reconstructed frame uses explicit named offsets within a 112-byte frame. It keeps checksum, XMM6 pointer, and XSTATE pointer slots separate from saved XMM6/YMM6 state, YMM6 proof bytes, the AVX flag, and the XSTATE proof mask. The frame remains 16-byte aligned after saving RBX; it does not write into the caller's home/shadow area. GNU assembler `.if` assertions fail the assembly if these ranges overlap or exceed the frame. Existing checksum and CPU-state checks were retained.

The pilot assembly compiled with those assertions and linked. The tracked pilot ELF was rebuilt as a 4,120-byte AMD64 ELF64 executable (entry `0x2013e0`), SHA-256 `5C648B30CBD50AC64D6027E4DE8726E1899ACC8B88A6FAAB5929799D3A296AC2`. The old R1 ELF remains preserved separately. The reconstruction is committed locally as:

`1a88c70e33eff48e7e130cf529b32d06e432743e — SECURITY_RECOVERY1: Reconstruct SECURITY3G-R3 repairs`

This commit is a recovery reconstruction; the missing historical R3 commit was not restored. It changes only the bounded discovery capacity, pilot stack layout/assertions, and rebuilt pilot ELF. The pilot-only timer-preemption eligibility restriction is unchanged. Native ELF remains kernel-privileged; this work does not add user-mode or address-space isolation.

## SECURITY3G-R4 qualification boundary

No R4 report, R4 artifact directory, or R4 execution evidence survived the bounded search. Status is **`SECURITY3G_R4_GUEST_QUALIFICATION_PENDING`**. QEMU was not launched during recovery, and the QAC coordinator was not modified or restarted. Source compilation does not establish PIT-driven switching, non-yielding workload preemption, CPU state across hardware-timer switches, desktop progress, lifecycle completion in a repaired guest, or negative guest acceptance.

SECURITY3G-R4 can resume from the reconstructed source state. Its next action is to prepare fresh, separately identified guest candidates from this commit, establish an x64 writable OVMF VARS image and positive/negative ESP provenance, then qualify only after QAC admission. Capture actual guest serial/QAC evidence before making any preemption acceptance claim.

## Verification performed

These are fresh offline checks run against commit `1a88c70e33eff48e7e130cf529b32d06e432743e`; they are not historical results and no guest was run.

| Check | Result |
|---|---|
| Git full fsck and strict connectivity check | Pass; no missing or unreachable objects reported |
| Captured reachable-refs bundle verification | Pass |
| Mbed TLS profile verification against the available sibling dependency | Pass: Mbed TLS 4.1.0, TF-PSA Crypto 1.1.0, three patches, normalized UTF-8/LF |
| Focused regressions | 9/9 pass: identity foundation; authentication sessions; managed lifecycle; Native ELF security policy; App Model identity; Native ELF ABI layout; system-service bridge; Native Settings runtime; Native Settings layout |
| Scheduler preemption pilot assembly/link | Pass; stack assertions assembled; rebuilt ELF identity recorded above |
| Normal AMD64 kernel build | Pass; `kernel.elf` SHA-256 `965A98665910F0CA2971EC06015E5AF5656D8AAC94235402FD51D785FE77FCE7` |
| Public-preview AMD64 kernel build | Pass; `kernel.elf` SHA-256 `67D90ADA908ABC2A690B5A0448F9096C3BF6EB35D4452AEA576B416F16CAE231` |
| Guest-proof AMD64 kernel build | Pass; `kernel.elf` SHA-256 `2C0E466E9EFE849FCD5895B91A9EB213EC612DFF3E46D6A433CECC67843B3EE4` |
| Hosted `build.bat` build | Pass; `guideXOSServer.exe` is 8,816,266 bytes, SHA-256 `534D9B2588A69700410ECA537DB6A9F389300F92E1E7C904479156104738900B` |
| Full public-preview release packaging pipeline | Blocked before kernel build because its configured external PacMan source root `D:\dev\pacman\guidexos` was unavailable to the build script. No attempt was made to recover or modify that dependency. |

Kernel checks ran in detached verification worktrees under the evidence directory, so generated build outputs did not dirty the primary checkout. The Mbed TLS source dependency was read through a junction to the existing `E:\dev\guideXOSServer\third_party\mbedtls`; it was not modified.

## Lineage, final state, and next action

Recovery implementation is committed locally; this report is recorded in a separate local commit immediately after it. A follow-up audit note records the bounded `E:\bkup` search above. After the follow-up note commit, branch `SECURITY_SETTINGS` points at that commit and the primary working tree is clean. The live remote baseline remains `cc8d9d0d418036041231cfc752bc07b555fda666`; no push or merge was performed. The roadmap item `SECURITY-VNC1 — Authenticated VNC & Configurable Remote Login` remains future work and no VNC code was changed.

**Recommended next prompt:** “Continue SECURITY3G-R4 guest qualification from the reconstructed SECURITY_RECOVERY1 state. Prepare and hash fresh positive/negative candidates, locate a valid writable x64 OVMF VARS image, wait for QAC admission without modifying the coordinator, and record actual guest evidence before any acceptance claim.”
