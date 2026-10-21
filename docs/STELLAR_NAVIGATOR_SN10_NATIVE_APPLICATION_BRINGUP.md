# Stellar Navigator SN10 — Native Application Bring-up

**Current result: Outcome B — SN10R6 built and verified a raw GPT/FAT32 disk from the accepted ESP. Its single granted QAC run reproduced `Block node is read-only` because the runner attached the IDE hard disk read-only. The runner now prepares a writable, per-run disk copy while keeping the verified source image and ESP unchanged. The corrected launch has not yet been QAC-tested; no guest serial or native application evidence exists. `.phase` remains `SN10`.**

### SN10R6 qualification update (2026-10-20)

**Outcome B — validated media and runner, guest qualification incomplete.** SN10R6 started on branch `STELLAR_NAVIGATOR` at expected HEAD `cb74725bdeb299c1760bfd04729e2a5f7cccfdb3`, with `.phase` `SN10` and 241 dirty worktree entries: the 239 unrelated entries plus the existing report and runner modifications. Those unrelated entries and earlier SN10 report history were preserved. No kernel or application source changed.

#### Storage root cause and corrected disk candidate

QEMU is `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. SN10R5 had attached `file=fat:<ESP>,format=raw,if=ide,index=0,readonly=on` and QEMU exited before serial with `Block node is read-only`. The December 2025 QEMU developer discussion documents the same failure from the read-only-by-default `vvfat` backend, including raw-format auto-detection restrictions; omitting the redundant `readonly=on` does not make that backend writable ([discussion](https://www.mail-archive.com/qemu-devel@nongnu.org/msg1156649.html)).

SN10R6 replaced `vvfat` with a real raw disk image. The first QAC attempt still used `readonly=on` on the IDE hard disk and QEMU again emitted `Block node is read-only`. QEMU's own `qemu-iotests/051.pc` fixture reproduces this exact error for `-drive ... if=ide,readonly=on`; the same fixture accepts a read-only CD-ROM. The common root cause is a read-only block backend attached as an emulated IDE hard disk. The read-only `vvfat` limitation can independently trigger the same error, so simply removing the option from the old directory backend was not a sufficient repair ([QEMU IDE read-only regression fixture](https://git.zx2c4.com/qemu/tree/tests/qemu-iotests/051.pc.out?id=b300c134465465385045ab705b68a42699688332#n740), [QEMU drive documentation](https://www.qemu.org/docs/master/system/qemu-manpage.html)).

The accepted staging directory was not modified. The existing `tools/release_esp.py` formatter/verifier, using its pinned `pyfatfs==1.1.0` and `fs==2.4.16` in an isolated evidence-directory venv, built and verified a 256 MiB FAT32 volume. It was embedded at LBA 2048 in a 258 MiB raw GPT disk with a protective MBR, EFI System Partition type GUID, 512-byte sectors, and valid primary/backup GPT CRCs. The FAT32 boot sector reports 512-byte sectors and 524,288 volume sectors. All 15 staged files (104,271,128 payload bytes) were read back and SHA-256 checked, including:

- EFI bootloader: `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783`
- proof kernel: `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`
- Stellar Navigator native ELF: `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`
- application manifest: `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`
- DE440s payload: `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`

The validated raw disk is `E:\bkup\artifacts\SN10R6_20261020\media\stellar-navigator-sn10r6.raw`, SHA-256 `79dc215fb2fd6f4ade3d1a6e56eeed0297ca733adc241769b3b0a76422450b8a`. Its partition payload SHA-256 is `30257ce858c3781869331a261ca21da5edb1e89eba24bd7fd6b870fd85438315`. The complete FAT file manifest, geometry, EFI boot path `EFI/BOOT/BOOTX64.EFI`, accepted-candidate provenance, and disk hash are in `stellar-navigator-sn10r6-verification.json`; the formatter's read-back report is `fat-volume-verification.json` in the same `media` directory. The evidence directory also retains `prepare_sn10r6_disk.py`, the full-tree expected hash manifest, and the isolated pinned formatter environment for reproduction.

#### One QAC attempt and actual startup result

Candidate verification and the final offline runner self-test passed at `E:\bkup\artifacts\SN10R6_20261020\qemu\20261020T195911116\candidate-verification.json` and `E:\bkup\artifacts\SN10R6_20261020\qemu\20261020T200040460\runner-selftest.json`. One bounded QAC request was made after a fresh online/empty preflight. QAC granted reservation `6a6f3e6b28634c06867cf8e78bee9000` to runner PID `3620` and launched QEMU PID `24656`; the QEMU process exited with code 1 and QAC released that reservation. The exact argument vector and output are retained under `E:\bkup\artifacts\SN10R6_20261020\qemu\20261020T195636574\`.

That attempt used the verified GPT/FAT32 raw image through `-drive file=<image>,format=raw,if=ide,index=0,readonly=on`. QEMU connected to the serial listener but sent zero bytes, then closed the connection. The available stderr is `C:\Program Files\qemu\qemu-system-x86_64.exe: Block node is read-only`; `guest-serial.log` and `qemu.log` are empty. This is a **preboot storage/IDE initialization failure**, not a guest boot or application failure. The initial runner result missed the quick-exit race and recorded a generic QAC ownership error; the runner now classifies this early QEMU exit separately.

The runner now validates the immutable raw candidate, makes a byte-identical `qac-disposable-disk.raw` in the unique run-evidence directory, checks its initial SHA-256, and gives only that per-run copy to the QAC-owned IDE drive without `readonly=on`. QEMU can modify that disposable copy; the verified disk and original staged ESP remain unchanged. This follows QEMU's IDE hard-disk write-permission requirement without sharing the disk or granting write access to source artifacts. The corrected writable-clone launch is **not QEMU-verified** because the one bounded QAC attempt was used on the read-only variant; do not claim the raw disk has booted successfully.

No guest boot marker, desktop observation, AppModel launch, native window, real DE440s ET/TDB 0 tuple, coordinate comparison, first close, production-shell relaunch, second close, screenshot, or post-close stability evidence was collected. These checks are **unverified, not passed**. QAC contention was not the blocker: the reservation was granted and released. The next required gate is a new exclusive QAC attempt with the writable disposable raw-disk copy, then the full native guest acceptance flow. `.phase` remains `SN10`; no production artifacts were rebuilt and no push was made.

### SN10R5 qualification update (2026-10-20)

**Outcome B — guest qualification remains incomplete because the post-repair QAC acceptance lane is occupied.** SN10R5 started on branch `STELLAR_NAVIGATOR` at expected HEAD `cb74725bdeb299c1760bfd04729e2a5f7cccfdb3`, with `.phase` set to `SN10`. Preflight found 240 dirty entries: 239 unrelated pre-existing entries and this uncommitted report modification. The acceptance report remains uncommitted and `.phase` remains `SN10`. No production application or kernel source changed.

#### Candidate and first QAC-managed launch

Candidate-only verification passed at `E:\bkup\artifacts\SN10R5_20261020\qemu\20261020T143631166\candidate-verification.json`. Source and staged ESP hashes matched the accepted manifests: proof kernel `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`, native ELF `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`, DE440s data `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`, app manifest `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`, OVMF `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`, and UEFI bootloader `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783`. The existing staged ESP was reused unchanged.

A fresh QAC snapshot showed the coordinator online with zero QEMU processes, reservations, or queued requests. One managed request was made. QAC granted reservation `20e46155756b4cb383d0a7198b86b21e` to runner PID `16240` and launched QEMU PID `13280`, but QEMU 11 exited with code 1 before boot, reporting `Block node is read-only`. QAC released the reservation. The captured serial stream connected but contained zero bytes; there is no guest serial, desktop, application, ephemeris, lifecycle, or screenshot evidence. The argument vector is in `qemu-invocation.json`; QAC output and the structured result are in `E:\bkup\artifacts\SN10R5_20261020\qemu\20261020T143647060\`.

The failed option was the explicit `readonly=on` on QEMU's `file=fat:<ESP>` virtual FAT directory drive. QEMU documents that this backend is read-only by default and that `:rw:` enables writes ([QEMU virtual FAT documentation](https://www.qemu.org/docs/master/system/images.html)). The acceptance runner now omits the redundant block-level `readonly=on`, retaining the read-only FAT backend. Its offline runner self-test passed at `E:\bkup\artifacts\SN10R5_20261020\qemu\20261020T143806436\runner-selftest.json`; this validates the runner only and is not guest evidence.

#### QAC lane after runner repair

The fresh post-repair snapshot in `E:\bkup\artifacts\SN10R5_20261020\qemu\20261020T143806436\qac-status-before-retry.log` and its matching list/queue files found one active managed acceptance reservation for another `guideXOSServer` run: reservation `9a43f9515dd74529b552f4d1c0501f78`, verified owner PID `29312`, QEMU PID `18148`, and no queued reservations. That reservation was left untouched. No acceptance request or guest boot was started after this occupied-lane finding.

SN10 remains open. Native execution, interactive UI inspection, genuine DE440s ET/TDB 0 output and numerical comparison, normal close, production-shell reopen, second-instance calculation and close, and post-close guest stability are all **unverified, not failed**. The QEMU option repair has not yet been exercised against a guest. The next attempt must wait for an exclusive QAC acceptance reservation; do not queue while this lane is occupied. No push or commit was made, and `.phase` stays `SN10`.

The phase remains `SN10`. SN10R1 started on branch `STELLAR_NAVIGATOR` at the expected HEAD `b6996a4e5e24e928deec552e1a530a2f1f678add`, with `.phase` set to `SN10`. The 239 pre-existing unrelated worktree entries were preserved.

### SN10R4 qualification update (2026-10-20)

**Outcome B — the runner's close/reopen control path is implemented and offline-validated; native guest qualification remains blocked by the occupied QAC acceptance lane.** SN10R4 started on `STELLAR_NAVIGATOR` at `629ac89824fc975418e662cdd17a337227f69dc3` and ended at `cb74725bdeb299c1760bfd04729e2a5f7cccfdb3`; `.phase` was and remains `SN10`. The starting worktree had 240 dirty entries: 239 unrelated pre-existing entries and this outstanding report modification. Those unrelated entries and the earlier SN10R1–SN10R3 report history were preserved. The ending HEAD is the local runner-only commit `cb74725bdeb299c1760bfd04729e2a5f7cccfdb3`; the report update remains in the worktree.

#### Runner close/reopen path and offline checks

The previous runner stopped after the first normal close. It now opens the existing guest shell with QEMU's documented `sendkey` mechanism and submits `desktop.launch com.guidexos.stellarnavigator`. The shell command calls `desktop::launch_app`, the same production desktop/AppModel dispatch path used by the boot proof. The runner uses serial-buffer offsets so a second window-created, initial-content-drawn, and ephemeris marker must occur after the first instance exits. It then sends Escape, requires the second native loader cleanup marker, captures the returned shell and desktop, and verifies three seconds of continued guest/QAC ownership and serial-stream stability. No kernel or Stellar Navigator application source changed.

The runner now classifies fatal kernel/native serial markers and nonzero QEMU exit as guest failure, a missing marker after a clean guest exit, a live guest timeout, serial stream disconnect, no guest serial output, serial capture failure, and QAC coordination failure separately. Its mock-backed offline self-test passed after the final runner change at `E:\bkup\artifacts\SN10R4_20261020\qemu\20261020T085220323\runner-selftest.json`. It checks the shell key mapping, serial cursor handling, fatal marker recognition, bounded timeouts, stream disconnect classification, queue cancellation ownership, resource release, and argument construction. Candidate-only verification passed at `E:\bkup\artifacts\SN10R4_20261020\qemu\20261020T084757454\candidate-verification.json`.

The existing candidate was reused without rebuilding. Verified SHA-256 values remain: AMD64 kernel `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`; native ELF `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`; DE440s data `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`; app manifest `08b43a2e0ddb05550d8ee02733b0ebbcc109556a29f685d6cfe54280b6a8df7c`; OVMF `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`; and UEFI bootloader `07e5350656e93e28f30cda66bbb924aef7d3d7053498547b27cc469b88d21783`. The structured QEMU argument vector is recorded in the run evidence, but QEMU was not started during SN10R4.

#### QAC reservation and guest qualification

A fresh QAC 2.0.0 check at `2026-10-20 08:48:49 PDT` found one active exclusive acceptance reservation for project `guideXOSServer`: reservation `1951474742474bc9a6a0d222b41a491e`, verified owner PID `2780`, and QEMU PID `18800`. The coordinator reported one managed session, zero foreign QEMU processes, and no queued request before SN10R4. The documented FIFO acceptance queue supports a bounded QAC-managed request, so the runner made one `qac run` attempt with a 20-second acquisition bound. QAC assigned this attempt reservation `cae97fc2fc114387b11e18c1a90d7378` to runner CLI PID `4900` in `Waiting` at queue position 1. The lane remained occupied; the runner cancelled only that exact childless reservation through QAC and verified its terminal state was `Cancelled`. The post-run status still showed the original reservation and QEMU PID `18800`; the SN10R4 queue was empty. No foreign process or reservation was changed.

The complete structured attempt result is `E:\bkup\artifacts\SN10R4_20261020\qemu\20261020T084849892\acceptance-result.json`. It records `EXTERNAL_QEMU_COORDINATION_BLOCKER`, the effective QEMU argument vector that would have been passed to QAC, and a serial listener that received no QEMU client. No SN10R4 QEMU child or guest serial output exists. Therefore desktop boot, AppModel discovery/launch, native shell output, the ET/TDB 0 Earth coordinate tuple and tolerance comparison, unavailable-data handling, first close, second launch, second close, screenshots, and post-close guest stability remain **unverified, not failed**. No numerical parity is claimed.

The next guest session must begin only after QAC grants an exclusive acceptance reservation. The automated reopen path should be observed against that guest. If the guest does not emit the second instance's new UI markers within the run's bounded timeout, a human may use one bounded fallback in an operator-visible QAC-managed QEMU session: after the first normal close, press the guest backtick key, enter `desktop.launch com.guidexos.stellarnavigator`, and press Enter; verify the second native UI and serial ephemeris result, press Escape, verify the lifecycle and loader cleanup markers, then observe the desktop for three seconds. Stop and record the failed or missing step rather than retrying indefinitely. This procedure is a fallback only and is not SN10R4 acceptance evidence.

### SN10R3 qualification update (2026-10-20)

**Outcome B — runner repair committed; native guest qualification is blocked.** SN10R3 started on `STELLAR_NAVIGATOR` at `d5d944b69c55b0cd74dd8b5ba9a6d5ea31305467`, with `.phase` set to `SN10`. The initial worktree had 240 dirty entries: 239 unrelated pre-existing entries and this already-modified report. Those entries and the report's earlier SN10R2 content were preserved. The only commit created is `629ac89824fc975418e662cdd17a337227f69dc3` (`fix(stellar-navigator): stream acceptance serial capture`), containing the new runner script. This report remains uncommitted.

#### Runner repair and offline validation

The SN10R2 runner gave QEMU `-serial file:` and then polled that same file with `ReadAllText()` while QEMU held it open. Windows denied the read, so no serial bytes could be consumed; this was a capture failure, not evidence of a guest failure. The replacement runner uses a loopback TCP socket chardev, incrementally captures and flushes serial bytes, and distinguishes an absent connection, a connected stream with no bytes, and a stream error. It also builds QEMU arguments as a structured argument vector and doubles commas in QEMU key-value paths. The `-drive` value no longer contains literal quote characters, addressing the earlier QEMU argument parsing failure. The QEMU invocation and virtual FAT settings follow the [QEMU system invocation and chardev documentation](https://www.qemu.org/docs/master/system/qemu-manpage.html) and [virtual FAT documentation](https://www.qemu.org/docs/master/system/images.html).

PowerShell parsing, candidate-only verification, and the runner's offline self-test passed. The self-test verified argument boundaries, incremental serial recognition and trailing-byte preservation, abnormal producer exit reporting, bounded missing-marker classification, separate no-output/no-client classifications, listener cleanup, and port reuse. It launched no QEMU or QAC process. Candidate-only verification matched the recorded kernel, native ELF, DE440s data, application manifest, OVMF, and bootloader hashes. Artifacts are under `E:\bkup\artifacts\SN10R3_20261020\qemu\`, including `20261020T050640530\runner-selftest.json`, `20261020T050404681\candidate-verification.json`, and the actual preflight record at `20261020T050527100\acceptance-result.json`.

#### QAC coordination and guest gates

The fresh QAC status and reservation list showed an active exclusive acceptance reservation for another project, `guideXOSServer` (reservation `3e8f7054c5a94cdba2fa112764ade7f9`, owner PID `15388`, QEMU PID `23008`), running from `D:\dev\guideXOSServer_DiskManagerImprovements`. QAC reported zero foreign QEMU processes, one managed session and one queued session. The runner stopped before requesting a reservation or launching QEMU and recorded `EXTERNAL_QEMU_COORDINATION_BLOCKER`. The other managed run was left untouched.

No SN10R3 QEMU invocation occurred. Therefore this run produced no guest serial output, desktop screenshot, AppModel discovery or launch evidence, native execution evidence, ephemeris coordinate tuple or hosted-reference delta, or close/reopen lifecycle evidence. These guest gates remain unverified, not failed. The actual-run path can observe first launch, computation, and normal close, then returns `GUEST_REOPEN_INTERACTION_UNVERIFIED`; it captures the Start Menu but does not select the package for a second launch. That reopen interaction remains an incomplete gate even after QAC becomes available. The current result is Outcome B, `.phase` remains `SN10`, and no push was made.

### SN10R2 qualification update (2026-10-20)

**Outcome B — SN10 partial.** Starting and ending branch: `STELLAR_NAVIGATOR`. Starting and ending HEAD: `d5d944b69c55b0cd74dd8b5ba9a6d5ea31305467`. `.phase` was `SN10` at both ends and remains `SN10`. The initial worktree had 239 entries; no source or build output was changed by SN10R2, and those entries were preserved. This report is the only repository file changed for closeout. No commit was created.

The SN10R1 production guest candidate remains byte-for-byte identified by the requested hashes:

- Proof kernel: `kernel/build/amd64/bin/kernel.elf`, 2,650,304 bytes, SHA-256 `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`.
- Native package: `Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`, 15,544 bytes, SHA-256 `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`. It is ELF64 AMD64 static `ET_EXEC`, with ELF entry `0x201a40` and `gx_main` at that address. The package manifest names `gx_main` and `guidexos-c-abi-v1`; the bare-metal loader requires those conventions.
- DE440s-derived asset: `Apps/StellarNavigator/Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin`, 32,117,373 bytes, SHA-256 `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`.

The preserved SN10R2 private ESP stages the proof kernel, UEFI bootloader, `app.json`, native ELF, and data under `Apps/StellarNavigator`. The staged kernel, ELF, and data hashes match their recorded values. The kernel discovers `/Apps` after VFS mount and the existing Desktop/AppModel dispatcher launches the package. The guest did not provide serial output in the attempted run, so guest-side mounting, package discovery, and data access are **not verified**. Staging is not being reported as guest execution.

#### QAC lane and attempt result

A fresh census first found no QEMU processes. The visible QAC GUI was not treated as proof of service: the QAC 2.0.0 CLI at `D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe` returned `Coordinator: Online`, with zero QEMU processes, reservations, or managed sessions. A QAC-managed direct-child launch was used.

The initial private-runner launch exited before guest qualification. A subsequent captured QAC child run exposed a quoting error in the inherited runner copy: literal quotes inside the `-drive` value made QEMU exit with code 1 before boot. The private evidence runner removed those quotes and reused the verified kernel. The next managed QEMU start encountered a separate runner limitation: QEMU holds the `-serial file:` output exclusively while running, but the runner calls `ReadAllText` on that file to poll markers. That file-sharing error ended the attempt before any serial marker or screenshot could be collected. QAC later showed no active Stellar Navigator reservation; no guest result is claimed.

The lane was then acquired by another project's managed acceptance run. At the 2026-10-20 04:24:52 PDT census, QAC reported reservation `efd6f47b438742b1b314d99577f1d45c`, project `guideXOSServer`, mode `acceptance`, owner PID `15052`, and QEMU PID `13684` at `C:\Program Files\qemu\qemu-system-x86_64.exe`. The command line references the other checkout `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` and its `focused-attempt-03-qac-sequence` guest. This is the actual external coordination conflict. It was left untouched, no further QEMU was started, and guest qualification stopped with `EXTERNAL_QEMU_COORDINATION_BLOCKER`.

#### Guest evidence and remaining acceptance gate

SN10R2 produced no guest serial log content or application screenshot. No guest coordinate tuple or numerical delta against the hosted reference was collected. The following remain unobserved: usable desktop, AppModel discovery and launch, native window rendering, authentic guest DE440s evaluation, displayed frame and units, missing or invalid-data handling, close/reopen behavior, and desktop or lifecycle stability. No managed/hosted regressions were rerun because no source or build input changed; SN10R1's recorded 15/15 native focused tests, 323/323 Windows regression, and Release GUI build remain the available host evidence.

The smallest remaining gate is to wait for the recorded QAC acceptance reservation to release, update the preserved acceptance runner to consume serial output through a readable live stream, then rerun the complete guest acceptance flow in an exclusive QAC lane. The staged candidate, private runner, preflight and blocked QAC snapshots, staged hashes, and launch notes are preserved under `E:\bkup\artifacts\SN10R2_20261020\`. `.phase` must remain `SN10` until the guest gates pass. No commit SHA exists for SN10R2.

## SN10R1 qualification update (2026-10-20)

### Freestanding header root cause and repair

The proof-kernel include trace was `kernel/core/desktop.cpp` → `kernel/core/include/kernel/kernel_apps.h` → `guide_web_http_shared.h` → `app_model_limits.h`. The final header included `<cstddef>`, which is a C++ standard-library header unavailable under the AMD64 kernel's existing `-nostdinc++` flag. This was a hosted header dependency in a shared bounds header, not a need for C++ library functionality.

`app_model_limits.h` uses global `size_t` and namespace-scope `constexpr` bounds only. It contains no `std::` names, hosted facilities, structs, or ABI layout declarations. All seven direct references are from C++ headers or sources; the file is C++-only because it declares a namespace and `constexpr` values. The kernel's established `kernel/core/include/kernel/types.h` convention uses `<stddef.h>` for freestanding `size_t`. The compatibility fix therefore changes only the include from `<cstddef>` to `<stddef.h>`. Constant values, public names, layouts, and calling conventions are unchanged.

With the original freestanding restrictions, syntax compilation passed for `core/desktop.cpp`, `core/kernel_apps.cpp`, `core/main.cpp`, and `core/gxos_tls_foundation.cpp`. The real proof-kernel build also passed with the guest proof flag and no weakened flags:

```text
C:\mingw64\bin\mingw32-make.exe -C kernel ARCH=amd64 EXTRA_CFLAGS=-DGXOS_STELLAR_NAVIGATOR_SN10_GUEST_PROOF -B -j4
```

The build exited 0 and produced `kernel/build/amd64/bin/kernel.elf` (SHA-256 `3aea5f9f10734dc6f248c6bdc05379d3b69a8d0d4b2ad3ec689f4dc71cb9ea1a`), an ELF64 AMD64 `ET_EXEC` with entry `0x100000`. It emitted warnings in unchanged kernel sources, including unused parameters in `kernel_app.h`, an array-bounds warning in `shell.cpp`, and a type-limits warning in `tcp.cpp`; no warning points to the changed header. Full compiler and linker output is preserved at `E:\bkup\artifacts\SN10R1_20261020\proof-kernel-build.log`.

The hosted C++ AppModel consumers `app_manifest_loader.cpp` and `app_manifest_validator.cpp` also passed syntax compilation with `g++ -std=c++17`, the language standard set by `guideXOSServer.vcxproj`. This confirms the C-header include continues to compile in the hosted configuration.

### Native package and regressions

The native package build completed with the existing sample command, `powershell -ExecutionPolicy Bypass -File sdk/samples/stellarnavigator/build.ps1`. Candidate identity:

- Path: `Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`
- SHA-256: `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`
- Format: ELF64 AMD64 static `ET_EXEC`, entry symbol `gx_main` at `0x201a40`
- Dependencies: no interpreter, dynamic section, or relocations
- Manifest: `com.guidexos.stellarnavigator`, `kind: NativeElf`, `runtime: native-elf`, AMD64 entry `gx_main`, ABI `guidexos-c-abi-v1`, visible in the Start Menu

The existing launch registration resolves the validated NativeElf package ID to the bare-metal native ELF launcher through the Desktop/AppModel path. This confirms the package wiring in source and manifest; it is not guest launch evidence.

The focused native ephemeris suite passed **15/15**. It includes the fixed ET/TDB 0 Earth state, authenticated 13-segment DE440s index, deterministic repeat, invalid input, coverage, short-read, hash, size, teardown, and reinitialization checks. The current candidate matches the prior SN10 package hash. ELF inspection is recorded in `E:\bkup\artifacts\SN10R1_20261020\native-elf-inspection.txt`; native build and test logs are beside it.

The accepted SN9 Release regression was rerun from an isolated validation copy so the pre-existing build-output worktree entries stayed intact. Results were **323 passed, 0 failed, 0 skipped**. The clean test compile emitted the known inherited xUnit2002 warning at `StellarNavigator.Tests/EphemerisTests.cs:51`; that unrelated assertion was not changed. The SN9 Release GUI build passed with **0 warnings and 0 errors**. Logs are `E:\bkup\artifacts\SN10R1_20261020\SN9-Release-tests.log` and `E:\bkup\artifacts\SN10R1_20261020\SN9-GUI-Release-build.log`.

### Numerical comparison method

The trusted hosted SN4 reference for Earth (NAIF 399) at ET/TDB `0` seconds past J2000, in the J2000 SSB frame and kilometres, is `(-27566740.483, 132361381.154, 57418653.286)`. The native core test compares each computed double component against these reference coordinates with an absolute tolerance of `0.0005 km`, corresponding to the documented three-decimal display precision (`0.001 km`). The native focused test passed. There is no guest observation or guest numerical delta because QEMU was not launched.

### QEMU census and guest status

A fresh system-wide process census at `2026-10-20T10:40:40Z` found QEMU PID `2956`, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`, using firmware and guest files from `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`. No `qac` command or QAC process was available. The session was identified as another project's guest and was left untouched. The census is recorded in `E:\bkup\artifacts\SN10R1_20261020\qemu-census-blocked.json`.

No SN10R1 guest boot was attempted. Therefore there is no serial log, screenshot, guest AppModel launch result, guest ephemeris output, guest invalid-data behavior, close/reopen evidence, or post-exit desktop-stability evidence. The existing QEMU checks remain open until an exclusive lane is available. This is the reason for Outcome B; native build success is not being presented as native guest execution or interactive qualification.

### Outcome and remaining work

**Outcome B — SN10 partial.** The header compatibility failure is repaired; the proof kernel, native package, focused tests, SN9 regression, and SN9 GUI build pass. `.phase` remains `SN10`. Do not advance it until an exclusive guest run verifies normal desktop initialization, AppModel package discovery and launch, the visible responsive shell, the authenticated ET/TDB 0 result and units/frame, corrupt or missing-data handling, normal close and reopen, and desktop stability. Capture serial output, screenshots, candidate hashes, and measured numerical differences from that run.

After SN10 guest acceptance, suggested SN11 scope remains a read-only native viewer with user-selected target and ET epoch, coverage-aware errors, and parity checks against the managed provider.

The project phase remains `SN10`. SN9 remains accepted as the Windows-hosted functionality milestone recorded by commit `c7993bc6bc536a2c673839e20af8c8dd4eb93253`; this work does not reopen SN9.

## Phase reconciliation

The requested starting HEAD was `c7993bc6bc536a2c673839e20af8c8dd4eb93253` on branch `STELLAR_NAVIGATOR`. The working `.phase` had regressed to `SN9`, while the accepted commit itself records `.phase` as `SN10` and accepts SN9 on Windows with 323 passing tests and live Windows UI evidence. The marker was restored to `SN10`, and the SN9R3 report was reconciled to preserve its supplemental host evidence and keep guest work under SN10.

The reconciliation was committed locally as `397a7c8d` (`docs: preserve SN9R3 evidence under SN10 scope`). The 239 unrelated pre-existing worktree entries were preserved and excluded from that commit.

## Runtime architecture discovered

The server contains a bare-metal AMD64 Native ELF package loader in `kernel/core/native_elf_baremetal.cpp`. It discovers packages from mounted `/Apps`, validates `app.json` metadata and executable paths, and accepts static ELF64 `ET_EXEC` images with the `guidexos-c-abi-v1` ABI and `gx_main` entry point. The loader rejects dynamic interpreters and relocations. It supplies app-owned windows, text and rectangle drawing, event polling, logging, file reads, and cleanup.

Packages use `kind: NativeElf`, architecture `amd64`, an ABI entry for `gx_main`, and package-relative files. The existing Desktop/App Model resolver maps a validated package ID to `desktop::launch_app`, which dispatches to the bare-metal loader. The normal hosted runtime does not execute Native ELF packages; the implemented boundary is the guideXOS bare-metal guest.

The SDK contract is defined by `sdk/include/guidexos/abi.h`, `app.h`, and `ui.h`. The implementation uses the existing ABI and loader without changing the server ABI or Windows Forms application.

## Native application delivered

The first native package is in `sdk/samples/stellarnavigator/`. The app provides a read-only window for Earth (NAIF 399) at ET/TDB `0` seconds past J2000. It presents the target, epoch, J2000 SSB frame, kilometre units, position, and authenticated-data status. If the asset is missing, unreadable, corrupt, or outside coverage, it presents a bounded error state. Escape or window close triggers shutdown.

The freestanding C++ core uses fixed-size structures, caller-owned storage, 64 KiB reads, and a bounded segment index. It has no STL, heap allocation, exceptions, RTTI, or dynamic ELF dependencies. It authenticates the full asset before parsing and evaluation, validates index and center-chain bounds, evaluates the indexed Type-2 Chebyshev records, and sums Earth → Earth-Moon barycenter → solar-system barycenter states.

The package manifest and built candidate are `Apps/StellarNavigator/app.json` and `Apps/StellarNavigator/bin/amd64/stellarnavigator.elf`. `scripts/run-stellar-navigator-sn10-qemu.ps1` stages a private ESP and has an opt-in kernel launch hook compiled only with `GXOS_STELLAR_NAVIGATOR_SN10_GUEST_PROOF`. The hook calls the real Desktop/App Model dispatch after VFS mount and package discovery; ordinary kernel builds do not launch the app automatically.

## Data and numerical conventions

The application reads the existing `ephemeris_de440s_chebyshev.sn4bin` at package-relative path `Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin`. The expected size is 32,117,373 bytes and the trusted SHA-256 is `d52b91573c29e6a4e5b6dc19c4af4d9747f6da2122f5b3aff4503684a96d2c0a`. The native parser validates the SNEPH04B v1 index (13 segments in the bundled asset) and does not extrapolate outside segment coverage.

The state is geometric position in the J2000 frame with SSB origin, in km, at ET/TDB zero. The native test matches the existing managed SN4 provider output to 0.001 km, which is the provider's displayed three-decimal precision. The expected display is:

```text
(-27566740.483, 132361381.154, 57418653.286) km
```

The Windows Forms application and its host-only behavior were not changed.

## Prior SN10 report evidence (before SN10R1)

- Native package build passed using LLVM `clang++` and `ld.lld`. The output is ELF64 AMD64 static `ET_EXEC`, has the `gx_main` entry point, and has no `PT_INTERP` or relocations. Candidate SHA-256: `6d5f1ca1b04a9ddbaf08e2eed8600ffaf776c06b6e45aab435f61fda9c2e66eb`.
- Focused portable native-core tests: **15 passed, 0 failed**. They cover authenticated open and index validation, the fixed-ET Earth state and managed parity, deterministic repetition, invalid and unsupported inputs, coverage bounds, short reads, hash and size failures, and reinitialization after teardown.
- At the time of the earlier SN10 report, the accepted SN9 regression baseline was documented as **323 passed, 0 failed, 0 skipped** and had not been rerun for that native-only slice. SN10R1 reran it; see the current results above.
- The repository’s pinned Mbed TLS/TF-PSA dependency bootstrap completed and `verify-mbedtls-profile.ps1` passed for the locked Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0 revisions. This populated the ignored `third_party/mbedtls` dependency tree.
- In the earlier SN10 attempt, the proof-kernel build stopped at `<cstddef>` under `-nostdinc++`; SN10R1 resolves this failure as recorded above. The historical guest census found another project's QEMU session and did not interact with it.

The QEMU runner uses OVMF and the repository's UEFI bootloader path, an isolated writable FAT ESP, serial logs, and the AppModel proof hook. The earlier report had no guest evidence because the proof kernel had not built and another project's QEMU occupied the lane. SN10R1 fixes the kernel build; the foreign QEMU session still prevents this task from launching a guest.

## Remaining SN10 gates

1. Run the already-built proof kernel and native package through an available exclusive QEMU lane.
2. Confirm package discovery and AppModel dispatch, authentic guest ephemeris evaluation and numerical parity, successful first draw, controlled invalid-data behavior, close/reopen, loader cleanup, and desktop stability from guest evidence.

Keep `.phase` at `SN10` until these gates pass. Once SN10 is accepted, a suitable SN11 scope is adding user-selected targets and ET epochs to the native read-only viewer, with coverage-aware errors and parity checks against the managed provider.
