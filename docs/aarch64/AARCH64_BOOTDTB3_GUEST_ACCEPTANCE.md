# AARCH64 BOOTDTB3 Repaired DTB Guest Acceptance

**Outcome: `BOOTDTB3_QAC_PENDING`**

**Acceptance source HEAD:** `5cac2b843582b34e6177cb41174838a9cd882e02`

**Branch:** `AARCH64_SUPPORT` tracking `origin/AARCH64_SUPPORT`

The repaired parser and minimal GICv2 candidate pass the hosted regression and freestanding build gates. The genuine guest acceptance did not run: QAC left the BOOTDTB3 request queued for the bounded 12-second admission window, so the runner cancelled it before launch. No guest boot, interrupt, or scheduler result is claimed.

## Repository and candidate

At preflight, the repository was at the expected HEAD on `AARCH64_SUPPORT`, with a clean tracked and untracked worktree and zero commits ahead of or behind `origin/AARCH64_SUPPORT`. The BOOTDTB1 diagnosis and BOOTDTB2 property attribution records were reviewed. Existing candidates and evidence were retained.

The preserved minimal no-Studio candidate is `out/aarch64-bootdtb2-repaired-20261102`. Its requested artifact hashes match exactly:

| Artifact | SHA-256 |
| --- | --- |
| `BOOTAA64.EFI` | `6465fbebf0427557623feff5e7d07b11f679dedb6ea72692b49db176ea2a9b2d` |
| `kernel.elf` | `fb0908e8511369284b440c054a946560d76b149903d57adb5c2590b75f006f3a` |
| `esp/ramdisk.img` | `74452c6109212bff33a73f2315e5aa029b44bff3cf8bb24257cd9850be5c22b9` |

The staged EFI and kernel copies have the same hashes as the candidate root files. A fresh Phase 4 build from the acceptance source HEAD under `out/bootdtb3-gate-phase4-20261104` reproduced all three hashes, including the ramdisk. The manifest's repaired parser, Phase 4 kernel, and parser-test source fingerprints match the current files. The Phase 4 build includes the architectural timer, common IRQ registry, scheduler, and bounded preemptive worker checks; no Studio guest was staged.

The preserved firmware-source fixture is `out/aarch64-bootdtb2-minimal-20261102/bootdtb2-guest-20261102-153806-117/firmware-source.dtb`: 1,052,672 bytes, SHA-256 `5aec32b925cb7259d651aca355106e58d77f7f1c553d80f0420c5b3e3036f311`. It is the actual firmware blob captured from the earlier authorized diagnostic guest, not a generated host fixture. Its loader-copy counterpart was not captured in that run.

## Hosted regressions and builds

- The Phase 2 production parser suite passed **51 assertions**. It includes malformed FDT header/structure controls, rejects a malformed `#address-cells` value length, accepts the synthetic PCI host's legitimate `#address-cells = <3>`, and preserves GICv2/timer discovery.
- The same current parser suite with the captured firmware DTB passed **52 assertions**. The external blob result was `parsed=1`, diagnostic `0`, parser stage `4` (`COMPLETE`), token `9` (`FDT_END`), structure offset `7096`.
- A negative control compiled the pre-repair parser from `45add80b6ac02238feee8e955690828d478d228c`. Against the captured firmware DTB it rejected the original property with diagnostic `2`, stage `2` (`STRUCTURE`), token `3` (`FDT_PROP`), and structure offset `5572`. The pre-repair source retains the `cells > 2` property predicate. The output is preserved at `out/bootdtb3-old-parser-control-20261104/negative-control.log`.
- A focused unsupported-resource-width control passed by rejecting the synthetic three-cell `reg` decode at `REG_RANGE` (diagnostic `2`, stage `2`, error `9`). Its source and executable are under `out/bootdtb3-resource-width-control-20261104/`. This checks resource decoding separately from generic FDT structure parsing; it does not broaden resource acceptance.
- Phase 4 allocator/VFS hosted controls passed.
- The GICv3 hosted suite passed **62 assertions**. This is hosted coverage only, not GICv3 guest qualification.
- The ABI2 adapter suite passed **104/104 checks**. This does not qualify Developer Studio guest behavior.
- Freestanding Phase 2 and Phase 4 AArch64 loader/kernel builds linked successfully. The Phase 4 rebuild's hashes match the preserved candidate as listed above.

Build and test outputs are preserved under `out/bootdtb3-gate-phase2-20261104`, `out/bootdtb3-gate-phase4-20261104`, `out/bootdtb3-gate-gicv3-20261104`, and `out/bootdtb3-gate-abi2-20261104`.

## Guest configuration and QAC admission

The existing runner was reviewed and used with its established guest profile:

| Setting | Value |
| --- | --- |
| QEMU | QEMU 11; executable SHA-256 `8d2183e745155a303ffb9fd861c5da31c5e6c19fcbfd082081cc3e4bcdb0e748` |
| Machine | `virt,gic-version=2,acpi=off` |
| CPU / RAM | Cortex-A53 / 512 MiB |
| Firmware | EDK2 AArch64 UEFI; code SHA-256 `47765fe344818cbc464b1c14ae658fb4b854f5c2ceffa982411731eb4865594d` |
| Variables | Fresh copy of `edk2-arm-vars.fd` template per run; template SHA-256 `b3b855c5a80310168051164986855692d1bdb06e67619856177965cd87c6774f` |
| Image | Preserved repaired Phase 4 candidate above; no Studio payload |

The existing BOOTDTB2 runner now accepts an optional `QacProject` parameter while retaining its BOOTDTB2 default. For this milestone it invoked the real QAC CLI as project `guideXOSServer-AARCH64-BOOTDTB3`, mode `acceptance`, with a 12-second admission bound and 120-second guest runtime bound. QAC assigned reservation `039272c5e27a42dbb61bf56fb4e9bb7f`, reported queue position 1, and did not grant the exclusive slot before the deadline. The runner cancelled that request before launch. Its record says `qemu_launched=false`; no guest serial log was created. After cancellation, QAC reported zero reservations, zero managed sessions, and zero queued requests. No retry was submitted.

The attempt record and QAC log are preserved at:

- `out/aarch64-bootdtb2-repaired-20261102/bootdtb2-guest-20261104-151308-246/session.json`
- `out/aarch64-bootdtb2-repaired-20261102/bootdtb2-guest-20261104-151308-246/qac.log`

The runner prepared a fresh variables-file copy before admission. Since QAC never granted the slot, firmware did not start and that copy contains no boot result.

## Guest observations and remaining gates

No BOOTDTB3 guest-originated stage marker was observed. Therefore the repaired guest's `FDT_END`, platform discovery, GICv2 initialization, timer PPI selection, timer initialization, actual IRQ count, common IRQ dispatch count, acknowledgement/EOI path, exception return, scheduler tasks, unexpected IRQs, and unexpected exceptions remain **unobserved**. The host acceptance of the captured DTB is not guest evidence. There is no new guest failure to localize because QEMU was not launched.

Qualifying fresh boots: **0**. The repeatability plan did not proceed because the first bounded QAC request received no grant. The outcome remains `BOOTDTB3_QAC_PENDING`; retain the candidate for a future authorized run.

GICv3 guest qualification remains pending. Developer Studio ABI3 remains at zero qualifying Studio boots; Studio was not launched. No Samsung tablet or Raspberry Pi hardware was accessed.
