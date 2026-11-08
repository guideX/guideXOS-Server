# S22R18 Disposable VM Qualification

**Outcome A — `PUBLIC_PREVIEW_SECURITY_PROFILE_QUALIFIED`**
**Date:** 2026-11-08
**Candidate source:** `2e1c2493355bd005399977bcd82a4142c94ca2e9` (`main`)
**QAC:** guideXOS QAC 2.0.0; QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`)

This report qualifies the exact S22R18 PacMan-only candidate in the documented disposable VM profile. The result is local qualification evidence. It is not publication, signing, external-origin authentication, release-contract approval, or release-readiness authorization.

## Repository provenance and candidate artifact

The canonical `ServerV03PublicPreviewPacManOnly` build used a separate clean clone at source commit `2e1c2493355bd005399977bcd82a4142c94ca2e9`. Its manifest records `worktreeCleanAtPackagingStart: true`; the build receipt records a successful build and a clean source worktree. The untracked `assets/Backgrounds/troncity.png` remained in the user's main checkout and was not part of this candidate's wallpaper input inventory.

| Artifact | Identity |
| --- | --- |
| Candidate ISO | `E:\CodexArtifacts\S22R18\source-2e1c2493\out\release-profile\7f9173c1-550b-4dfb-84e4-b44e1e1c260a\preview-artifacts\guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso` |
| ISO bytes / SHA-256 | 90,245,120 / `4cc445e3fe33eb615c5a0434977eaa664bf9f8c0c3e46743a9e9bde578f6a92d` |
| Candidate manifest | `E:/CodexArtifacts/S22R18/source-2e1c2493/out/release-profile/7f9173c1-550b-4dfb-84e4-b44e1e1c260a/preview-artifacts/guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.manifest.json`; records `candidateOnly: true`, `canonical-build`, clean source, and ISO verification |
| ISO SHA-256 sidecar | `E:/CodexArtifacts/S22R18/source-2e1c2493/out/release-profile/7f9173c1-550b-4dfb-84e4-b44e1e1c260a/preview-artifacts/guideXOS-Server-v0.3.0-amd64-public-preview-pacman-only-candidate.iso.sha256` |
| Build id / profile | `7f9173c1-550b-4dfb-84e4-b44e1e1c260a` / `ServerV03PublicPreviewPacManOnly` |
| Kernel ELF | 2,805,584 bytes; SHA-256 `4d416905e36e77ae17314cb39d6805c4dcef3d01834a0aab00a6985f3d49369a` |
| EFI FAT image | 90,177,536 bytes; SHA-256 `ba9ad43ca8c1b8d5f675b2e20aee46f07ea7e7a0a093f9953b4c648dbe5b3fc2` |
| OVMF firmware | `C:\Program Files\qemu\share\edk2-x86_64-code.fd`; SHA-256 `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a` |
| PacMan manifest / ELF | `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449` / `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` |

The release manifest records successful bootloader, kernel, symbol-audit, RAM-disk, PacMan, and app-package stages. It records the Mbed TLS profile digest `960888bc747745d97fe104e0552c2602eeb57ef32b4f79f2d7bfa6121a945055`. The tool receipt lists MinGW GCC 15.2.0, GNU ld 2.46.0.20260210, NASM 2.16.03, Python 3.12.10, pyfatfs 1.1.0, fs 2.4.16, and pycdlib 1.16.0. The canonical builder created the bootable UEFI ISO and independently verified its ISO9660 and El Torito structure.

## Hosted and freestanding checks

All current-source checks in [the enforcement report](RELEASE_S22R18_PUBLIC_PREVIEW_SECURITY_ENFORCEMENT.md) passed. The most relevant security and package checks were 27 Native ELF policy cases, 37 release-integrity assertions, 7 public-preview QAC-profile assertions, 151 QAC launcher-contract checks, and 36 artifact-isolation checks. Native ABI, VFS mount-table, hosted VFS read, Native filesystem, Settings layout/runtime, native present, PacMan registration, and release packaging checks also passed.

The current-source freestanding AMD64 stack guard passed: Settings display/navigation worst case 11,512/16,384 bytes (4,872 bytes headroom), legacy Display Options 11,896/16,384 (4,488), and COM2 storage 7,768/16,384 (8,616). The minimum required headroom is 4,096 bytes.

## Disposable VM configuration

Both qualified candidate runs used `-PublicPreviewIsoOnly` with the exact candidate ISO and verified OVMF firmware. QAC staged only read-only release media. The observed QEMU command used:

```text
-machine pc,accel=whpx -smp 2 -m 2048M -vga std -display none
-drive if=pflash,format=raw,readonly=on,file=<verified OVMF>
-drive if=none,id=releasecdrom,file=<candidate ISO>,media=cdrom,readonly=on,format=raw
-device ide-cd,drive=releasecdrom,bootindex=1 -boot order=d -net none
-serial file:<private run serial log> -no-reboot -no-shutdown -monitor none
-qmp tcp:127.0.0.1:<QAC-assigned port>,server=on,wait=off -S
```

There was no writable guest disk, writable EFI variables disk, host filesystem share, network device, guest-accessible COM2 host channel, or HMP monitor. QEMU started paused; the runner verified the QAC reservation, exact direct child, QMP instance and loopback listener before sending `cont`. QMP was used as QAC-owned host management and was not exposed to the guest. Screenshots and serial logs were retained per run.

## Positive candidate acceptance

Two QAC executions used the exact candidate ISO and completed with guest result PASS, QEMU exit code 0, verified direct-child exit, and verified reservation release.

| Run | QAC reservation / QEMU PID | Result and evidence |
| --- | --- | --- |
| `s22r18-preview-positive-qualified-e6152cfa191a4b94b0a1d0071eebcc1a` | `d8c9f43d98e9410885217d2535a826f1` / 18268; QMP `127.0.0.1:56155` | PASS. Approved PacMan entry, 524 presented frames, 6 delivered key events, normal `GX_MAIN_RETURN`, `fault=none`, runtime cleanup PASS, and `[PACMAN-SMOKE] result=PASS`. |
| `s22r18-preview-settings-navigator-qualified-5f5f7baf13a84ade9f9bf0220ea004e3` | `71fec9379b3242d585c101daf10b4661` / 29132; QMP `127.0.0.1:57257` | PASS. Added guest Settings and Navigator evidence, then launched PacMan, delivered input, and closed it cleanly. |

The first run's serial log records `[NATIVE-ELF-PREVIEW] decision=ALLOW package=com.guidexos.pacman entry=immediate`, `GX_MAIN_ENTER`, input events, `GX_MAIN_RETURN`, and `[PACMAN-SMOKE] result=PASS proof=approved-entry-clean-return-presented-frame-key-events`. QAC observed that guest marker live before it accepted PASS.

The second run showed the Start menu and opened the built-in Settings app from its `Settings` item. The Settings System page displayed product, kernel version, and AMD64 architecture. Serial evidence records `launch identity=gxos.builtin.settings snapshot=ready revision=1 shared-core=system`, followed by Settings shutdown and focus loss. The QMP screendump is retained at `E:\CodexArtifacts\S22R18\qac-settings-navigator\s22r18-preview-settings-navigator-qualified-5f5f7baf13a84ade9f9bf0220ea004e3\output\screenshots\settings-open.ppm`; a PNG view copy is at `E:\CodexArtifacts\S22R18\qac-settings-navigator\settings-open.png`.

Navigator's boot smoke ended with `[NAVIGATOR-SMOKE] result=PASS`. It reported VFS `/`, `/system`, and `/certs` mounts and explicitly reported unsupported TLS/network and bookmark persistence capabilities as unavailable. No host network was present or required. PacMan resource reads remained under `/Apps/PacMan`; the application later closed with `fault=none` and resource/window cleanup PASS. No unexpected exception, panic, or approved-path bypass marker occurred in either positive run.

## Negative security acceptance

The diagnostic fixture is separate from the candidate and is explicitly named `diagnostic-resource-tamper-20261108.iso`. It changes one byte (the last byte) of the nested release RAM-disk package resource `Apps/PacMan/resources/audio/EatPill.wav`. The executable, manifest, kernel, and other resource bytes are unchanged. The original resource SHA-256 is `510a52e5776573197aef14d77e393185b6c7148d1b59dac535d45c5ca0498686`; the fixture resource hashes to `868ab1580302f4e88120bcb767bab796cc0ce582ec7a18db2cd80835795743e1`. The diagnostic ISO SHA-256 is `b2350439ee3b9b92a02a7baf20dfba106f344c418dea406ac0a28f46ab24d19f`.

QAC run `s22r18-preview-resource-tamper-negative-final-e1a529a351f349e894c2937a6b845b40` used reservation `af31115e5db34d9793cf50e9ccd8cbf4`, QEMU PID 12300, and loopback QMP `127.0.0.1:53187`. The guest reported the observed and expected hashes, then:

```text
[NATIVE-ELF-PREVIEW] decision=REJECT reason=package-resource-integrity-or-source-mismatch
[NATIVE-ELF-PREVIEW] decision=REJECT reason=package-or-source-changed-before-load
[NATIVE-ELF] desktop shortcut result=FAIL
```

The serial log contains no `decision=ALLOW`, `GX_MAIN_ENTER`, `GX_MAIN_RETURN`, or PacMan smoke-pass marker. The runner recorded guest result FAIL with reason `expected-resource-digest-rejection-before-entry`; that is the expected negative-test classification, not a candidate regression. QEMU exited 0 and QAC verified release. No unauthorized or malicious executable was run.

## QAC execution count and retained attempts

For the final candidate identity, there were **two completed positive QAC executions** and **one completed negative diagnostic QAC execution**. A separate diagnostic attempt launched without an interactive pseudo-terminal is retained as an incomplete transport recovery and is excluded from qualification. Its runner record remains `RecoveryRequired`; a separate recovery record documents that the exact QAC reservation, direct-child identity, read-only ISO command line, and loopback QMP listener were checked before a QAC-helper `quit`. QAC then verified that QEMU PID 15240 exited and reservation `0ae6618b18674ff2995a8ae7d166daf9` was released. That attempt has no guest result and makes no acceptance claim.

Five earlier candidate QAC attempts are retained as failed because the then-current embedded resource pins did not match the package assets. Those runs are superseded and are not counted as positive acceptance. The resource hashes were corrected, covered by the nine-resource policy regression, and the final candidate was rebuilt from the clean commit above before the two PASS runs.

After the final positive and negative executions, QAC reported 0 QEMU processes, 0 managed sessions, 0 reservations, and 0 queued or blocked work. Both positive runs and the completed negative run recorded QMP quit acknowledgment, exact child exit code 0, QAC CLI exit code 0, and reservation release verified.

## VFS, built-in applications, and read-only behavior

Guest evidence shows Navigator's expected root/system/certificates mounts, Settings launch and cleanup, and PacMan resource reads from its package path. Hosted VFS checks passed (16/16 mount-table invariants and 4/4 hosted read cases); the public-preview policy suite also rejected writable aliases, overlays, backing devices, unresolved source ownership, and generation changes without corrupting the registry. Settings layout/runtime tests passed and the freestanding Settings/COM2 stack guards retained at least 4,096 bytes headroom.

The preview media is read-only. Writable user preferences, recent-history persistence, and display topology changes are not claimed as available on this profile. Settings' supported read-only System view and Navigator's local/embedded smoke path were exercised; unavailable display configuration and unsupported network/TLS cases were not counted as features.

## Remaining risks, owner decisions, and S22R19 scope

The profile verifies exact approved bytes and an immutable package source; it does not sandbox privileged PacMan code. Kernel compromise, a compromised boot chain, hypervisor compromise, executable defects, or a modified unsigned distribution remain outside the boundary. The candidate hash is a local integrity identity, not proof of publisher origin.

Security risk acceptance, final release-contract approval, and publication authorization remain open S22R16 owner decisions. S22R18 did not sign, publish, promote, modify `dist`, change release-readiness markers, or declare public release readiness.

Recommended S22R19 scope is owner-directed distribution authenticity/signing and key lifecycle, a separate process-isolation design, and explicitly approved read-only Settings/history semantics. Do not mix unrelated features into that work.

## Final repository state

The qualification and enforcement changes are committed locally on `main`; no push was made. The docs commit leaves the tracked worktree clean. The pre-existing untracked wallpaper `assets/Backgrounds/troncity.png` is preserved. Candidate, diagnostic ISO, QAC logs, screenshots, recovery record, build receipt, and test outputs remain under `E:\CodexArtifacts\S22R18`; nothing was relocated into `dist` or used to overwrite an earlier artifact.
