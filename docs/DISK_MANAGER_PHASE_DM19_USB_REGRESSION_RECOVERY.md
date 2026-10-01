# Disk Manager Phase DM19 — USB Regression Recovery

## Scope and outcome

DM19 investigates the three DM18 writable USB lifecycle failures reported as UHCI bulk-IN/CSW timeouts. The current-source 5/5 lifecycle cohort and the required follow-up regressions passed, but the historical timeout did not recur. The old failures did not record the BOT opcode, CBW tag, CSW buffer contents, or QEMU UHCI trace, so the triggering command and cause remain unknown. DM19 therefore remains **Outcome C**: the retry gate is green, but the historical regression is not decisively classified.

USB write completion, Sync Cache, and error handling remain unchanged. The source changes add bounded failure diagnostics and standardize transport evidence in proof manifests. The committed DM18 lifecycle runner already waited for explicit lifecycle/restart markers; DM19 did not identify or repair a runner synchronization defect. The storage tests add exact 13-byte CSW token, actual-length, and short-packet coverage; they do not reproduce the historic timeout state.

## Starting state and preserved evidence

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `587be00eec0389b4136b51be669decfdc3afe102` (DM18)
- The tracked worktree was clean at start. Existing `out/` evidence was left in place, including failed DM18 USB runs and NVMe backing-store evidence.
- The cached branch reference showed 1 ahead / 0 behind at start. `git fetch origin` failed with `git@github.com: Permission denied (publickey)`, so remote freshness is unknown.
- No QEMU run was given a host physical disk. The lifecycle cohort uses new repository-owned 80 MiB raw images.

## Historic DM18 failures

DM18 used QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`), WHPX, `pc,usb=off`, PIIX3 UHCI, QEMU's default cache, and no explicit `-cpu`. Each attempt started from a fresh 80 MiB zero raw image with initial SHA-256 `33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF`. The UEFI image hash was `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`; the bootloader hash was `1E6623873298294D72FD9C803519BD70299B652CF8064DAFDBAB756F250B7107`; the kernel hash was `27F0C0ECD097404BC00C3E6FD9354C96D63F6A19CDB6313487ECB82B489E80FC`.

All three failures were on endpoint `0x81`, requesting a 13-byte Bulk-IN CSW and receiving zero bytes. The TD was still ACTIVE (`td-status=0x388007FF`) at the 1000-frame bulk deadline, with a terminated TD link (`0x00000001`). The QH element was `0x3BF606A0`; FRNUM advanced approximately one timeout interval, including across its 11-bit wrap. Controller status and root-port state were not captured in the old serial logs. One attempt ran with no other QEMU process.

| Attempt | FRNUM start → current | Toggle at failed CSW | Other QEMU processes | Result |
| --- | --- | --- | ---: | --- |
| DM18 r1 | `02EB` → `06D3` | DATA1 → DATA1 | Not recorded | CSW timeout |
| DM18 r2 | `030A` → `06FA` | DATA0 → DATA0 | Not recorded | CSW timeout |
| DM18 r3 | `07F7` → `03E7` (wrap) | DATA0 → DATA0 | 0 | CSW timeout |

The original serial output contains no BOT opcode or CBW tag. The lifecycle runner created a trace event list but did not pass `-trace` to QEMU, so no transport event trace exists for these failures. The exact BOT command and expected CSW tag therefore cannot be recovered from DM18 evidence. The known stage is CSW reception after an earlier BOT command; whether that command was INQUIRY, READ CAPACITY, READ/WRITE, Sync Cache, or another command is unknown.

## Known-good and current-source comparison

The comparison below uses the preserved pass/fail manifests and the corresponding versioned lifecycle runners. All listed lifecycle runs used QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`), WHPX, `pc,usb=off`, and PIIX3 UHCI on `uhci.0`. The lifecycle argument lists omit `-cpu` and omit an explicit cache option. Old manifests did not consistently record QEMU/UEFI hashes, complete argv, host process state, or timeout values; those omissions are called out rather than inferred as proof that the environments were identical.

| Phase / evidence | Source revision | Kernel SHA-256 | Bootloader SHA-256 | USB fixture | Result and evidence limits |
| --- | --- | --- | --- | --- | --- |
| DM13 writable lifecycle, run 07 | `723ac561ee89f19dcae3c037b24de716438f9e70` | `ADCFE44E76276D785BC8C756B2F50092A5B38738758CB61B431556927A035514` | `D737C8CF9B29476716EE17232C99AAFF079A44A386DA8B61EEFDD0023ED08BCE` | Fresh 68 MiB raw; initial SHA `2DF1381A5F3A765B5A90CA39CC9793CC42BE473FBA459D1A5B64E1B49C04EA9D` | Full lifecycle/restart/verifier pass; earlier attempt in the series had a timeout. Legacy manifest omits UEFI hash, full argv, QEMU hash, and host process state. |
| DM14 writable lifecycle, run 03 | `5a55cc70a36c56302bb96d2b10ee9af3cc12bb9b` | `19A3EB9C970F779F2E6AA37A622867D5B9B85B458F90266997AB162B07BEC750` | `FB30589854FA0A62AC496BBAF292C4D0B6CE197A53C59B4B7575E5301F2702D1` | Fresh 68 MiB raw; same initial SHA as DM13 | Lifecycle/restart/verifier pass. DM14 separately qualified hotplug. Legacy lifecycle manifest has the same environment-field gaps as DM13. |
| DM17 writable lifecycle | `c452b891c7090e02ff8dd07bd3ef236492639b80` | `FE4800082355738AE3C21FF1FFAB3727875B840B0FA5BAB8C3B54ACE162B48B5` | `FB30589854FA0A62AC496BBAF292C4D0B6CE197A53C59B4B7575E5301F2702D1` | Fresh 80 MiB raw; initial SHA `33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF` | Lifecycle/restart/verifier pass. The stored manifest identifies the kernel/bootloader hashes and QEMU version, but does not carry all DM19 fields. |
| DM18 writable lifecycle, r1–r3 | `587be00eec0389b4136b51be669decfdc3afe102` | `27F0C0ECD097404BC00C3E6FD9354C96D63F6A19CDB6313487ECB82B489E80FC` | `1E6623873298294D72FD9C803519BD70299B652CF8064DAFDBAB756F250B7107` | Three fresh 80 MiB raw images; each initial SHA `33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF` | All three failed at CSW Bulk-IN. r3 records zero other QEMU processes. Runner supplied a trace event list but omitted QEMU `-trace`; no UHCI trace was captured. |
| DM19 unchanged DM18 kernel rerun | Same `587be00eec0389b4136b51be669decfdc3afe102`, before instrumentation | `27F0C0ECD097404BC00C3E6FD9354C96D63F6A19CDB6313487ECB82B489E80FC` | `1E6623873298294D72FD9C803519BD70299B652CF8064DAFDBAB756F250B7107` | Fresh 80 MiB raw; initial SHA `33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF` | One full lifecycle and 100-cycle write/Sync Cache/readback/restore passed; final image SHA `3D414EB5C3BC11E6A22D9852526ACCF3CCBF3132F67CA5B182B654A25EDC4BFA`. This rerun did not reproduce the DM18 failure. Evidence: `out/dm19-current-r1/`. |
| DM19 writable lifecycle, r1–r5 | `587be00eec0389b4136b51be669decfdc3afe102` plus DM19 diagnostics/tests | `4AC61050B03103E90E02224BFC30BF8C6BCA52273CB4AAD2C823487DFDD8AC72` | `1E6623873298294D72FD9C803519BD70299B652CF8064DAFDBAB756F250B7107` | Five fresh 80 MiB raw images; each initial SHA `33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF` | 5/5 passed lifecycle, restart, 10 mount/write/read/unmount iterations, and independent verification. One run traced UHCI; four repeat runs had tracing disabled. Each first/restart boot recorded zero other QEMU processes. |

DM13 and DM14 used a 68 MiB image and different kernel/bootloader hashes from DM17/DM18. DM17 and DM18 shared the 80 MiB blank-image size/hash and the QEMU machine/controller profile, but their kernel and bootloader hashes differ. Thus these results are useful historical baselines, not perfectly artifact-matched comparisons. DM18 recorded the full QEMU argv and it confirms `-machine pc,usb=off`, `-device piix3-usb-uhci,id=uhci`, no `-cpu`, and no explicit USB-image cache mode. DM19 records those details, QEMU and UEFI hashes, image access/hash, timeout settings, per-boot process state, HMP monitor ports, and result in `DM19-TRANSPORT-1` manifests.

The committed DM18 lifecycle runner already required the lifecycle PASS marker plus the kernel main-loop marker, then required restart persistence on the second boot. The DM19 runner changes add trace opt-in with separate first/restart trace paths, artifact/process/argv fields, and a normalized result key. They do not constitute a synchronization fix and are not a demonstrated cause of the DM18 timeouts.

DM17's known-good writable lifecycle passed with QH physical address `0x3BF60690`; the failed DM18 QH element pointed at `0x3BF606A0`, the adjacent first TD address. This 16-byte address difference is not itself evidence of a bad descriptor. DM18 did not record the active TD physical address or CSW buffer mapping, so a meaningful historical address/boundary comparison is unavailable.

The production UHCI/BOT source behavior did not change between DM17 and DM18. DM19's driver changes only collect failure state before descriptor teardown. They record QH/TD virtual and physical addresses, QH head and element, TD token/status/link/buffer, staging-buffer VA/PA and first 13 bytes, max packet size, decoded actual length, FRNUM, USBSTS, USBCMD, both PORTSC registers, endpoint, requested/actual lengths, toggle, and the 1000-frame timeout. The BOT failure line adds CDB opcode, CBW/expected CSW tag, direction, expected and completed data lengths, expected/received CSW length, status, and received CSW bytes.

With matched QEMU profile and a 100-cycle write/Sync Cache/readback/restore stress harness, both a clean DM17-source kernel and a current-source kernel passed. The DM17-source kernel hash was `4BA275915FBB546B4D5E0DF7A86F637200D53603AFD647CAD4C2848052D818EB`; the current-source stress kernel hash was `CBD7AE5CE58BB75D41C240492FCCA216B07674BAF0FC36F8B2840F2E56B976CF`. The current-source trace recorded 1,474 `usb_msd_cmd_submit` events and 1,474 `usb_msd_send_status` events, with zero `usb_uhci_packet_complete_error` events. This matched stress A/B found no deterministic source-revision failure, but does not explain the historic lifecycle timeout.

## Root cause and repair status

No root cause was identified and no transfer-behavior repair was made. The DM17-to-DM18 source comparison found no production UHCI/BOT behavior change, and the unchanged DM18 kernel passed one same-profile lifecycle plus 100-cycle stress rerun. The current diagnostic build then passed 5/5 fresh lifecycle attempts. These passes show the prior timeout is not reproduced deterministically in these attempts; they do not establish whether it was timing-sensitive, command-history-dependent, image-dependent, or due to an unrecorded artifact/environment difference. The historic records lack the opcode/tag, CSW buffer and TD-buffer physical address, controller registers, and QEMU TD processing needed to separate those cases.

DM19 adds bounded failure diagnostics and the focused exact-13-byte CSW tests, but no test reproduces the missing-CSW state or is known to fail under DM18. The implementation preserves the existing USB error behavior. This is not enough to classify the timeout as harness-only or to claim a fix, so the phase outcome remains **C**.

## UHCI, BOT, short-packet, toggle, and DMA audit

- Control and bulk transfers both use the frame-aware `wait_td` loop, volatile ACTIVE polling, USBSTS error checks, a bounded fallback, and scheduler yielding (`sti; hlt` with interrupts enabled, otherwise `pause`). Control timeout is 250 frames; bulk timeout is 1000 frames.
- Bulk chains are limited to 13 TDs, each with a separate 64-byte staging buffer. The endpoint toggle advances only after a completed TD and by the number of packets represented by its actual length.
- CSW reception requests exactly 13 bytes from the Bulk-IN endpoint. UHCI's max-length token encodes 12 for 13 bytes. The actual-length helper decodes `N-1` back to `N`; a completed short-packet TD succeeds when its error bits are clear.
- The 13-byte token uses endpoint address and the current data toggle. The CSW TD is linked and published through the existing descriptor barrier before the QH is scheduled. QH reuse occurs after synchronous TD completion or after timeout teardown.
- The bounded CSW buffer snapshot and exact VA-to-PA-to-TD mapping will be available if the new diagnostics capture a failure. No QEMU trace or CSW bytes exist for the historic failures, so DMA visibility and device emission cannot be distinguished retrospectively.
- The current focused tests cover zero, one, eight, thirteen, full-packet, and maximum actual lengths, plus a short 13-byte CSW completion. No historic faulty state was found for a test that fails under the old behavior.

DM12 previously fixed a real 32-byte software TD stride versus 16-byte UHCI descriptor mismatch, QH layout/alignment, descriptor barriers, and a too-small bootstrap stack. DM19's audit found those fixes present and unchanged. The current control and bulk wait loops share the completion-visibility logic; the historic CSW timeout does not yet establish a recurrence of the DM12 bug.

## Runner and manifest changes

The DM13 writable lifecycle/private-write, DM14 hotplug, DM9 ATA, and DM15 AHCI runners emit the shared `DM19-TRANSPORT-1` manifest contract. Common fields record QEMU version/hash, machine and CPU option, controller arguments, image path/size/hash/access/cache mode, kernel size/hash, UEFI and bootloader hashes, timeouts, QEMU process concurrency, command line, and result. Controller-specific and per-boot details remain alongside those common keys. The lifecycle runner can enable QEMU tracing and records separate first/restart trace files. DM19 r1 captured a 287,928,580-byte first-boot trace; the four later cohort runs left tracing disabled to keep repeat runs practical.

## DM19 regression and build results

The current-source writable lifecycle cohort, read-only USB regression, DM14 hotplug proof, ATA lifecycle, AHCI lifecycle, 100-cycle stress, storage suite, and production builds completed. Each proof manifest and serial log is retained under `out/dm19-*`; no existing `out/` evidence was removed.

| Gate | Result | Evidence |
| --- | --- | --- |
| Current-source writable lifecycle, fresh images | PASS, 5/5; all first/restart boots and independent verifiers passed | `out/dm19-lifecycle-r1/` through `r5/`; summary `out/dm19-lifecycle-r2-r5-cohort-summary.txt` |
| 100-cycle write/Sync Cache/readback/restore after cohort | PASS; zero-filled disposable image SHA remained `33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF` | `out/dm19-postcohort-stress/` and `out/dm19-postcohort-stress.raw` |
| Immutable read-only USB reference | PASS; private/shared writes rejected, reads passed, before/after SHA remained `EF75FE75254EEE78E3E7DA54556CB358F83B1E54AFB5770D8CF9D1AC13D89DAE` | `out/dm19-usb-readonly-regression-final/`; original reference at `out/dm10-qemu-repeatability-final-340/attempt-01/secondary-600m.raw` |
| DM14 QEMU hotplug | PASS; unplug, stale cleanup, reinsertion, replacement, active-I/O interruption and final image inspection completed | `out/dm19-hotplug-regression/` |
| ATA lifecycle and restart | PASS; fresh 600 MiB image, restart rediscovery, independent GPT/FAT32 verifier | `out/dm19-ata-regression/` |
| AHCI lifecycle and restart | PASS; fresh 600 MiB image, restart rediscovery, independent GPT/FAT32 verifier | `out/dm19-ahci-regression/` |
| Storage suite | PASS; 755 checks, 0 failures, 247 USB checks | `out/dm19-storage-suite-final.log` |
| Production AMD64 kernel | PASS; `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`; SHA-256 `123402130FE00EF350003D132B73568C508589632AA40AF7AF8079D866C33D17` | `out/dm19-amd64-production-build.log` |
| UEFI x64 Release | PASS; SHA-256 `1E6623873298294D72FD9C803519BD70299B652CF8064DAFDBAB756F250B7107` | `out/dm19-uefi-x64-release-build.log` |

The five completed lifecycle images all began at the same verified blank-image hash. Their independently verified final SHA-256 values were: r1 `87750DD8BB50CB42F6FF8507FBED478256C23371AA427509EFB95A67D0C166AE`, r2 `E6AA0B7E008FA63A3B4957CC2FD359D4FB16C848D5359452973E7C88103E79C3`, r3 `17D47A813EDBEF03540517C3A52EEB6887B73B98CC62F1304B37F5530F2E8A4E`, r4 `E82503088753848CE4729F517F7749D665954B8F2E7E0B97A17858790E7EE1A0`, and r5 `6B6AEB34EA034392B8E8D51F3C2F0D4ECBCB35A25D1B8D346D30653AA986F6BC`. The final hashes differ because each proof wrote and verified its own filesystem; every image passed the independent verifier.

The current USB lifecycle, post-cohort stress, and read-only runs used QEMU's default storage cache with no explicit `cache=` argument. The DM14 hotplug runner used its existing writeback profile for media A/B and QMP `device_del`/`device_add` plus HMP `drive_add`; the transcript is preserved in `out/dm19-hotplug-regression/qmp-transcript.jsonl`. Lifecycle and private/read-only runners use an HMP TCP loopback monitor for proof-owned shutdown. Their manifests record the exact argv and process state. No host USB device or physical disk was passed through.

The production AMD64 build was made with no proof macros and `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`; the Makefile defaults both gates to zero and the NVMe callback assignment remains behind those compile-time gates. The final storage suite includes the default-read-only NVMe descriptor fallback check. No NVMe QEMU workload or graduation was run during DM19.

## NVMe policy and next work

The default production NVMe configuration remains read-only. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`; the shared NVMe write and Flush callbacks remain disabled and destructive eligibility remains blocked. DM18 sparse/dense backing evidence is preserved and no DM18 NVMe graduation work is repeated here.

Because DM19 has not recovered the exact historic command and cause, it does not satisfy Outcome A despite the fresh 5/5 lifecycle cohort. The current unchanged DM18 kernel passed one full lifecycle plus the 100-cycle stress run, and both the historical DM17-source and instrumented current-source matched stress runs passed; these are evidence that the timeout is not deterministic in the reproduced profile, but do not show whether it depends on timing, command history, image state, or an unrecorded environment difference. The old records do not allow those possibilities to be separated. Do not begin NVMe graduation or FAT32 scalability on the strength of passing retries alone. Continue focused USB classification until the historic failure is understood; until then the phase result is Outcome C.
