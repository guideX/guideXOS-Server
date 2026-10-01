# Disk Manager Phase DM20 — USB CSW Timeout Classification

## Scope and outcome

DM20 preserves the working USB write path and adds diagnostics for an active-TD timeout at the 13-byte Bulk-IN CSW. A five-run current-source lifecycle cohort ended **4/5 PASS, 1/5 FAIL**. The failing run reproduced the same guest-visible CSW timeout signature, now identified as a 512-byte WRITE(10) at BOT tag `0x1BC`. Its QEMU trace was accidentally disabled, so the failed endpoint inside the host/device/controller stack is still unclassified. The result is **Outcome C — current-source qualification failed; root cause unresolved**. No transport fix is claimed.

The work adds per-command BOT identity, CSW buffer and actual-length reporting, bounded UHCI state history, QEMU USB tracing, a small production-path BOT stress proof, controlled timeout/layout/boundary profiles, and an offline trace correlator. Diagnostics and proof-only layout perturbations are opt-in; normal production keeps the existing transfer policy.

## Starting state and preserved evidence

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `2dfc1ac1606ab1b2f1de56845be03cb3e0283af7` (`DM19: instrument USB regression and standardize proofs`)
- The tracked worktree was clean at start. Existing DM9–DM19 `out/` evidence was preserved.
- Cached divergence was 2 ahead / 0 behind. `git fetch` failed with `git@github.com: Permission denied (publickey)`, so remote freshness is unknown.
- Historical binaries and evidence were left intact. New proof output is under `out/dm20-evidence/`; it remains untracked.

## Historical DM18 signature

All three preserved DM18 failures requested a 13-byte CSW on endpoint `0x81`, received zero bytes, and expired with the UHCI TD still ACTIVE (`0x388007FF`) after 1000 frames. The reported QH element was `0x3BF606A0`; the TD link was terminated (`0x00000001`). FRNUM advanced through the timeout interval, including one 11-bit wrap. The toggle at failure differed between attempts.

The DM18 serial logs do not contain the SCSI opcode, CBW tag, expected CSW tag, buffer contents, QH/TD physical addresses, controller status/register snapshot, or QEMU trace. The exact command and failed layer therefore cannot be recovered from those logs. The same archived kernel and bootloader hashes were used across the three failures.

## Diagnostic changes

- Every BOT command gets a monotonically increasing diagnostic sequence and a bounded descriptor with opcode, CDB length, direction, transfer size, CBW/expected CSW tag, device incarnation, endpoints, and generation. Example records use `BOT#...`; this sequence is diagnostic only.
- CSW submit/completion records include expected, TD, and software toggle state; TD status; encoded maximum and decoded actual length; short-packet state; exact received length; QH/TD/buffer VA and PA; page and 64 KiB offsets; and the FRNUM at submit.
- The 13-byte CSW buffer is initialized with a recognizable sentinel. A failure record captures its bounded hex contents and classifies untouched, partial, complete-CSW, or unrelated data. Successful records retain the raw 13 bytes and classify the expected CSW.
- CSW validation reports signature, expected/actual tag, residue, and status separately. A nonzero residue is not treated as a missing CSW. The unit tests cover 13-byte actual-length decoding when max packet is 64 bytes, short packets, command/tag metadata, and CSW checks.
- Diagnostic UHCI timeout records carry BOT phase, incarnation/generation, endpoint and toggle context, QH/TD/token/status/buffer state, USBSTS/USBCMD/USBINTR/FRNUM/FLBASEADD/PORTSC, and bounded QH-element/TD-status histories. FRNUM-delta timeout accounting supports proof profiles through 65,535 frames. No production timeout policy was changed.
- The BOT-stress QEMU runner enables focused USB/UHCI events by default from VM startup and writes a DM19-style manifest with source head, kernel/UEFI/QEMU hashes, image hash/size, cache mode, machine/controller, serial, timeout, proof count, and layout parameters. The lifecycle runner supports `-TraceUsb` and the new `-StopAfterInitialize` prefix mode for fast, traced repetitions of production initialization. Existing full-lifecycle behavior remains the default.
- `scripts/classify-dm20-usb-trace.py` joins guest BOT command/tag order to QEMU Mass Storage command/status and packet events. In the traced WRITE(10) control, QEMU's `usb_uhci_packet_add` TD and QH values numerically matched guest physical addresses `0x3BF606A0` and `0x3BF60690`. The classifier still joins by BOT tag and ordered trace chronology and does not assume that address identity is universal across QEMU builds/configurations.

## Reproducer and long stress

The proof calls the normal UHCI, BOT, SCSI, and block-device paths. It backs up a single sector and a guarded 128-sector range on the disposable raw image; runs round-robin TUR, INQUIRY, READ CAPACITY, READ(10), WRITE(10), SYNCHRONIZE CACHE, and REQUEST SENSE commands; exercises sequences A–D ten times; runs the 128-sector multi-TD write/read sequence E ten times; and restores/verifies both saved ranges.

The final accepted run used the clean diagnostic build at `out/dm20-evidence/dm20-t1000-p0-c1000-20261001-075700-920`. Its serial marker reported the requested **1,000** opcode-loop commands, sequences A–E passed, and the raw image hash before and after was `33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF`. The trace/classification JSON and complete manifest are beside the serial log.

An earlier invocation labeled `c1000` reused an object compiled with a 100-command proof macro. It passed 100 commands and was excluded from the 1,000-command result; the evidence was preserved. The final proof was rebuilt in a fresh directory, and the runner now compares the guest's reported hexadecimal count with the requested count before recording PASS.

## Trace and command findings

The accepted current-source run completed every guest CSW submit with a matching 13-byte completion; the correlator found no missing command/CSW pair, no active CSW timeout, and successful tag/status joins for all post-kernel commands. The proof's ten deliberately illegal-command CSWs returned SCSI failure status as expected, followed by REQUEST SENSE and a fresh command; they were valid transport completions, not CSW timeouts.

For each completed CSW, the encoded max length `0x000C` decoded to 13 bytes even though endpoint max packet was 64 bytes. The received length and last-TD actual length were 13; the 13-byte buffer held a complete `USBS` CSW; expected and returned tags agreed; toggle progression matched the TD/software state; and short-packet was false. Since no timeout occurred, timeout-only sentinel failure classification, timeout register snapshots, and stuck-TD histories were not exercised.

## Historical binary and runner/config comparison

The exact DM18 kernel (`27F0C0ECD097404BC00C3E6FD9354C96D63F6A19CDB6313487ECB82B489E80FC`) and bootloader (`1E6623873298294D72FD9C803519BD70299B652CF8064DAFDBAB756F250B7107`) were replayed without rebuilding, on fresh blank images with the original SHA-256, using QEMU 11.0.0, WHPX, PIIX3 UHCI, the same USB serial, OVMF, and default cache behavior. Three replays, including trace-on and trace-off observations, completed the legacy proof markers without a CSW timeout. Evidence is in `E:\guideXOS-DM20\evidence\dm20-dm18-replay-*`.

| Kernel | Runner/config | Result |
| --- | --- | --- |
| DM18 | Original DM18 runs | 3/3 CSW timeout; exact opcode/tag unavailable |
| DM18 | DM20 traced replay | 3 replays; no CSW timeout |
| DM20 | DM18 runner configuration (same QEMU/UEFI/controller/serial/cache profile) | Current diagnostic long stress passed; no CSW timeout |
| DM20 | DM20 runner/config | Current traced long stress and all profile runs passed |
| DM20 | Current-source writable lifecycle cohort | 4/5 passed; run 05 timed out on WRITE(10), tag `0x1BC`; failure run had no QEMU trace |

The matrix does not establish whether the old failure was source-, timing-, or environment-dependent: the failure was not reproduced, and the DM18 runs lack traces and command identity.

## Current-source lifecycle timeout reproduction

The DM20 current-source lifecycle cohort used five fresh disposable 80 MiB images: run 01 under `out/dm20-evidence/dm20-current-lifecycle-20261001-080454-841/` and runs 02–05 under `out/dm20-evidence/dm20-current-lifecycle-completion-20261001-085004-979/`. Runs 01–04 completed their lifecycle/restart/image-verifier checks. Run 05 failed during production initialization and left the image byte-for-byte blank (`33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF`). The failed run's evidence is retained under the latter path. An earlier partial `run-02` in the first directory has no result marker and is excluded from this 4/5 count.

The guest classified the failed command as `USB_CSW_TIMEOUT_ACTIVE_TD`: BOT sequence/tag `0x1BC`, WRITE(10) (`0x2A`), data-out length 512, expected 13-byte CSW on endpoint `0x81`, and zero bytes received. The QH and TD were at physical `0x3BF60690` and `0x3BF606A0`; the QH element still named that TD and the TD link was terminated. Its status remained ACTIVE (`0x388007FF`), max length decoded to 13, actual length decoded to zero, and the CSW sentinel (`A5` repeated) was untouched. Expected, TD, and software toggles all agreed at 1. FRNUM advanced from `0x019D` to `0x0592` during the 1000-frame timeout window. USBSTS was zero and USBCMD remained running at `0x0081`; the kernel subsequently performed BOT recovery and later commands completed. This localizes the observed failure to a CSW TD that received no completion as seen by the guest, but does not identify whether QEMU failed to schedule/send it, UHCI emulation lost it, or the guest missed the completion.

The failed run's manifest says `qemuTraceEnabled=no`; its trace event list and expected trace path do not mean a trace was captured. A subsequent trace-enabled run used the same diagnostic kernel hash, QEMU, UEFI, controller, image size/cache profile, and a fresh blank clone. It passed the full lifecycle, ten-cycle mount/write/read/unmount proof, cold restart, and independent image verifier. Across that first boot, 12,599 guest commands had 12,599 CSW submissions and completions; the correlator found no timeout, no missing pair, and 12,599 matching 13-byte status/tag joins to QEMU. All joined QEMU status values were 0.

For tag `0x1BC`, QEMU trace line 20,750 records a 512-byte command; eight 64-byte data-out packets completed, followed by command status 0 and a 13-byte CSW. QEMU's status packet used token `0x8269` and TD `0x3BF606A0` (line 20,786); packet completion succeeded on that TD (line 20,788), and QEMU logged QH `0x3BF60690` completing TD `0x3BF606A0` (line 20,789). The guest used the same QH/TD physical addresses and completed the expected 13-byte CSW with tag `0x1BC`. This is direct evidence that QEMU processed that exact descriptor in a successful control run, not a trace of the failed attempt and not a root-cause determination.

An opt-in `-StopAfterInitialize -TraceUsb` mode was added to the existing lifecycle runner to enable repeated traced initialization-prefix trials without the subsequent ten-cycle VFS workload. This creates one new disposable image and evidence directory per attempt; no result from these trials can retroactively convert the failed 4/5 cohort into a pass.

The first prefix attempt exposed that the runner's shared marker helper still waited for the later main-loop marker, so that attempt continued into the lifecycle proof. Its guest initialization marker passed; the owned QEMU instance was cleanly stopped through its recorded HMP port, and the attempt is excluded. The helper was fixed so only prefix mode may stop at the initialization marker while full lifecycle mode retains its main-loop requirement. Five corrected fresh-image traced prefixes then passed. In every run, tag `0x1BC` completed a matching 13-byte CSW; QEMU reported status length 13, the same TD `0x3BF606A0`, packet completion success, and QH `0x3BF60690` completing that TD. No run emitted `USB_CSW_TIMEOUT_ACTIVE_TD`. The host deliberately stops each VM at the prefix marker; the correlator therefore shows at most one unfinished tail command/CSW record in four runs. Those shutdown-cutoff records are not timeout classifications, and these five prefixes are not full lifecycle passes.

## Timeout, frame, and memory-layout experiments

All profiled runs passed. The timeout sweep used 500, 1000, 2000, and 5000 frames. FRNUM advanced during successful operation. None reached a timeout, so the campaign did not measure a valid CSW completing after the old deadline or a TD remaining stuck.

Proof-only padding moved the QH/TD/buffer addresses while preserving the protocol sequence. A 4096-byte pad produced QH/TD at `0x3BF5F690/0x3BF5F6A0`; a 65536-byte pad produced `0x3BF50690/0x3BF506A0`. The corresponding bulk buffers moved to `0x3BF5F000` and `0x3BF50000`. The bootloader rebased the kernel, so the frame-list physical base stayed fixed; the logged DMA objects did move.

The 13-byte CSW buffer passed at page offset `0x000`, at `0xFF3` without crossing, and at 64 KiB offset `0xFFF3` without crossing. At offset `0xFFF4`, its 13 bytes crossed both the 4 KiB page and 64 KiB boundary by one byte; all CSWs still completed. A TD at physical `0x3BF5FFF0` ended exactly at the page edge, with its QH at `0x3BF600F0` on a different page; that profile also passed. These QEMU results are evidence for these tested layouts, not a universal UHCI boundary guarantee.

## Qualification and builds

The DM20 source contains opt-in diagnostic/layout/stress behavior; no USB transport fix, common block/VFS change, or NVMe write enablement was made. DM20's current-source USB lifecycle cohort is **4/5** because run 05 timed out. Separately, DM20 passed 100 write/Sync Cache/read/restore cycles with exact image restoration, the read-only reference-image gate, DM14's same-media reinsert/replacement-media hotplug proof, and fresh ATA and AHCI lifecycle/restart/independent-verifier regressions. The long BOT stress and ten-profile matrix also passed, but none clears the lifecycle regression. No physical host disk was attached.

- Storage-manager suite: **764 checks, 0 failures**, including **249 USB checks** (up from DM19's 755 / 247).
- AMD64 production kernel: final clean build with `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`.
- Diagnostic and proof kernels: fresh builds with BOT stress and extended UHCI diagnostics, 100-cycle writable USB proof, and USB hotplug proof.
- UEFI x64 Release: built successfully.
- USB write stress: **100/100** write/Sync Cache/read/restore cycles passed; the disposable image's SHA-256 returned to its original value.
- USB read-only reference: read passed, private/common writes were blocked, and the image remained unchanged.
- USB hotplug: same-media reinsert and replacement media passed with fresh device identity.
- ATA and AHCI: current-source full lifecycle, restart rediscovery, and independent GPT/FAT32 image inspection passed.
- NVMe read callback remains available; write and Flush callbacks remain absent by default, and unsupported writes fail closed. No NVMe write campaign was run.
- `git diff --check`, PowerShell parser, and Python AST checks pass.

## Classification and DM21 recommendation

**Outcome C.** The active CSW timeout is no longer merely historical: DM20 reproduced it once during current-source lifecycle initialization at WRITE(10)/tag `0x1BC`. The failed run captured detailed guest state, but no QEMU events because tracing was disabled. A traced control completed the same opcode/tag successfully. This proves an intermittent guest-visible timeout and narrows its command identity, but does not establish the failing component or a code defect. Do not claim the USB lifecycle gate is green or apply an unproven transport fix.

Retain the diagnostics. DM21 should continue USB recovery and capture QEMU tracing on a failed run before considering a transport change. Do not advance NVMe production graduation or FAT32 scalability based on this DM20 result; both remain outside this phase's scope.

## Evidence index

- Accepted current-source long stress: `out/dm20-evidence/dm20-t1000-p0-c1000-20261001-075700-920/`
- Timeout/layout/boundary matrix: `out/dm20-evidence/profile-matrix-runtime-results-r4.csv`
- Clean diagnostic build: `out/dm20-diagnostic-kernel-build-clean-final.log`
- Production build: `out/dm20-production-kernel-build-latest.log`
- UEFI x64 Release build: `E:\guideXOS-DM20\uefi-x64-release-build.log`
- Exact archived DM18 replays: `E:\guideXOS-DM20\evidence\dm20-dm18-replay-*\manifest.txt`
- Current-source 100-cycle USB write/restore proof: `out/dm20-evidence/dm20-usb-write100/`.
- Current-source USB read-only reference proof: `out/dm20-evidence/dm20-usb-readonly/`.
- Current-source USB same-media/replacement-media hotplug proof: `out/dm20-evidence/dm20-usb-hotplug/`.
- Current-source ATA lifecycle/restart/inspection proof: `out/dm20-evidence/dm20-ata-lifecycle/`.
- Current-source AHCI lifecycle/restart/inspection proof: `out/dm20-evidence/dm20-ahci-lifecycle/`.
- Current-source 4/5 lifecycle cohort and failed signature: `out/dm20-evidence/dm20-current-lifecycle-completion-20261001-085004-979/`.
- First accepted current-source lifecycle run: `out/dm20-evidence/dm20-current-lifecycle-20261001-080454-841/run-01/`.
- Structured guest-only classification of run 05 (QEMU trace unavailable): `out/dm20-evidence/dm20-failed-run-05-classification.json`.
- Successful traced control and complete first-boot correlation: `out/dm20-evidence/dm20-current-lifecycle-completion-20261001-085004-979/run-05-traced/`.
- Five corrected traced initialization prefixes and their per-run correlations: `out/dm20-evidence/dm20-current-traced-init-prefix-r2-20261001/`.
- Excluded first prefix attempt and runner-wait diagnosis: `out/dm20-evidence/dm20-current-traced-init-prefix-20261001/run-01/prefix-attempt-interpretation.txt`.
