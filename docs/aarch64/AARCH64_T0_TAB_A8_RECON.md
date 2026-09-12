# AARCH64 T0 — Samsung Galaxy Tab A8 reconnaissance

Date of this repository pass: 2026-09-12. This is an offline/host-tooling reconnaissance milestone. It does not mean guideXOS has booted on a tablet.

## Current result

The repository safety gate passed. The host inspector and synthetic DT/boot-image tests passed. No Android device was visible through ADB on this host and no matching Samsung firmware directory was supplied, so the current hardware result is **Outcome E**:

> Exact model/firmware cannot yet be obtained or correlated sufficiently for safe work.

The required marker is therefore deliberately **not** emitted. `AARCH64_TAB_A8_T0_RECON_PASS` may be emitted only by the inspector after an exact identity, usable DTB, and usable Android boot artifact have been captured and parsed. It still does not mean guideXOS ran.

## Repository safety record

| Field | T0 value |
| --- | --- |
| Repository | `D:\dev\guideXOSServer_AARCH64` |
| Branch | `AARCH64_SUPPORT` |
| Starting HEAD | `2d23abd228510f9866f3860e603239b122bd3d93` |
| Starting subject | `aarch64: prepare Raspberry Pi 4 physical boot` |
| Upstream | `origin/AARCH64_SUPPORT` (`git@github.com:guideX/guideXOS-Server.git`) |
| Ahead/behind | `0/0` at start |
| Worktree | clean at start; Pi P1 files present and untouched |
| Push | none |
| Tablet writes | none |
| Unlock/flash/AVB/SPL/trust changes | none |

The Raspberry Pi P1 candidate remains the expected commit and no Samsung code is connected to the Pi/QEMU platform path.

## How to capture the real tablet

Install ADB, enable USB debugging on the tablet, and run from the repository:

```powershell
.\scripts\inspect-galaxy-tab-a8.ps1
```

For more than one online device, pass the serial explicitly:

```powershell
.\scripts\inspect-galaxy-tab-a8.ps1 -Serial <adb-serial> -FirmwareDirectory <matching-extracted-firmware>
```

For offline firmware-only analysis:

```powershell
.\scripts\inspect-tab-a8-firmware.ps1 -FirmwareDirectory <matching-extracted-firmware>
```

The scripts write only generated reports and captured read-only text under `out\aarch64-tab-a8-t0\`. They never run `dd`, `odin`, `fastboot`, `heimdall`, a partition erase, an unlock, a flash, or an AVB/vbmeta modification. They do not require root. Firmware archives are read in place; source archive/image files are not changed or uploaded.

Generated report set:

| File | Purpose |
| --- | --- |
| `DEVICE_REPORT.md` | ADB identity, CPU, kernel, memory, AVB, slot, and OEM-state evidence |
| `BOOT_IMAGE_REPORT.md` | Android boot/vendor_boot header, section layout, ARM64 Image, DTB, DTBO, and vbmeta evidence |
| `DT_REPORT.md` | DTB hashes/counts, exact raw cells, important-node inventory, and safe `ranges` translations |
| `PARTITION_REPORT.md` | Supplied firmware metadata, archive members, and read-only by-name listings |
| `PLATFORM_MAP.md` | Normalized subsystem map and first-boot adapter boundary |

## Identity gate

The following are expectations, not constants to apply before capture:

| Field | Expected lead | Current T0 evidence |
| --- | --- | --- |
| Model | `SM-X200` | not captured |
| Device codename | `gta8wifi` | not captured |
| Product/region | build/fingerprint-dependent | not captured |
| Platform | `UMS512` | not captured |
| Board value | `ums512_25c10` | community lead only |
| Family | `sharkl5pro` | community/SoC-family lead only |
| SoC | Unisoc T618 | community/SoC-family lead only |

If the device reports `SM-X205`, another codename, or conflicting platform values, the inspector records the mismatch and does not apply SM-X200 values.

## Required final capture fields

The generated device report has a row for each requested Android property, including missing values. It also captures `uname -a`, `uname -m`, `/proc/cpuinfo`, `/proc/meminfo`, `/proc/iomem`, `/proc/interrupts`, `/proc/partitions`, `/proc/cmdline`, and read-only by-name directory listings when Android permits access.

The eventual exact report must fill these fields:

| Field | Current value | Evidence state |
| --- | --- | --- |
| Exact tablet model | not captured | unknown |
| Device codename | not captured | unknown |
| Product/region identity | not captured | unknown |
| Android version | not captured | unknown |
| Android kernel version | not captured | unknown |
| Build fingerprint | not captured | unknown |
| Bootloader version | not captured | unknown |
| SoC | not captured | unknown |
| Board/platform | not captured | unknown |
| CPU topology | not captured | unknown |
| RAM/usable RAM | not captured | unknown |
| Bootloader lock | not captured | unknown |
| Verified boot state | not captured | unknown |
| A/B/dynamic partition state | not captured | unknown |
| Boot-image header/version | no matching artifact | unknown |
| Boot-image SHA-256 | no matching artifact | unknown |
| DTB hash/count | no matching artifact | unknown |
| DTBO entries | no matching artifact | unknown |
| Kernel source provenance | community lead recorded below | community-derived |
| Boot-chain model | Android/Samsung path assumed as a design boundary | not stock-proven |
| Kernel load address | not captured | unknown |
| Entry environment | not captured | unknown |
| GIC generation/addresses | not captured | unknown |
| Timer IRQ/frequency | not captured | unknown |
| UART controller/base/IRQ | not captured | unknown |
| UART accessibility | no physical opening in T0 | unknown |
| Reserved memory | not captured | unknown |
| Display controller | not captured | unknown |
| Panel | not captured | unknown |
| Framebuffer handoff | not captured | unknown |
| Internal storage controller | not captured | unknown |
| MicroSD controller | not captured | unknown |
| USB controller | not captured | unknown |
| Touchscreen controller | not captured | unknown |
| Physical buttons | not captured | unknown |
| PSCI/SMP evidence | not captured | unknown |
| AVB implications | no changes made; exact state unknown | unknown |
| Recovery-to-stock plan | matching firmware + Download Mode + tested recovery tool required | plan only |

## Boot and DT analysis implemented

`scripts/tab_a8_recon.py` is a bounds-checked host parser. It supports:

- Android `ANDROID!` boot headers v0–v4, with v1/v2 recovery-DTBO and v2 DTB fields; v3/v4 explicitly direct DTB investigation to `vendor_boot`.
- ARM64 Linux `Image` header fields: `text_offset`, `image_size`, flags, endianness, page-size encoding, and placement bit.
- Android `VNDRBOOT` header recognition and DTB location lead.
- Android DTBO table header/entry parsing with offset, size, ID, revision, custom fields, and per-entry DTB identification.
- AVB `AVB0` metadata header inspection including algorithm, descriptor bounds, rollback index, flags, and rollback-index location. No signature or metadata is changed.
- Flattened DT v16–v19 parsing with strict total/block bounds, reserve-map capture, node/property inventory, 32-bit and 64-bit cells, empty/absent/malformed `ranges`, nested translation, and overflow rejection.
- Exact raw `reg` and interrupt cells alongside safe decoded addresses and GIC SPI/PPI interpretation.

The host parser does not add Tab A8 addresses to `kernel/arch/arm64/phase2_platform.cpp` or alter the Pi/QEMU parser contract. The C++ header at `aarch64/tab_a8/tab_a8_platform.h` contains only platform vocabulary and a loader-facing map; addresses remain DTB-derived fields.

## Hardware support matrix

| Subsystem | Identified | Address/IRQ known | guideXOS support | T1 required |
| --- | --- | --- | --- | --- |
| CPU | no live identity yet; expected heterogeneous ARM64 | no | common ARM64 reusable | one boot CPU |
| Memory | no live DTB yet | no | BootInfo memory map only | reserve secure/modem/display/DMA regions |
| Boot chain | Android/Samsung boundary only | no | adapter design only | exact image/handoff evidence |
| DTB | parser and hash tooling ready | no artifact | BootInfo DTB concept reusable | matching exact DTB |
| GIC | no live evidence; do not reuse Pi GICv2 | no | none on target | generation, distributor/redistributor, CPU interface |
| Timer | architectural timer parser ready | no live IRQ/frequency | architecture-neutral parsing | physical/virtual IRQ and CNTFRQ |
| UART | community `ttyS1` is only a lead | no live base/IRQ | none on target | controller, pinctrl, accessibility |
| Framebuffer | no evidence | no | none | preserve/handoff test |
| Display/DSI | no evidence | no | none | panel timing/power or preserved buffer |
| Backlight | no evidence | no | none | regulator/GPIO sequencing |
| Internal storage | expected SDHCI-style path is only a lead | no live evidence | none | controller, clocks, DMA/IOMMU, IRQ |
| MicroSD | not assumed | no | none | bootloader path evidence |
| USB-C | no evidence | no | none | controller, PHY, role switch, IRQ |
| Touch | no evidence | no | none | bus/address/IRQ/reset/regulators |
| Buttons | no evidence | no | none | GPIO/PMIC key evidence |
| Wi-Fi | board-specific | no | none | later |
| Audio | board-specific | no | none | later |
| Battery/PMIC | board-specific | no | none | later |
| GPU | Mali identity deferred | no | irrelevant initially | software renderer first |

## Source provenance and evidence classes

The report uses these labels:

- **stock-proven** — read-only data from the identity-verified running Android device or an artifact correlated to its exact build hash.
- **source-proven** — a source/configuration file says this, but it is not proof that the installed image matches.
- **community-derived** — community device configuration or downstream source, useful as a lead and cross-check.
- **SoC-family inference** — plausible for UMS512/T618, not board proof.
- **unknown** — not enough evidence.

Cross-reference material consulted for the design boundary:

1. [AOSP boot image header documentation](https://source.android.com/docs/core/architecture/bootloader/boot-image-header) defines v2 fields including page size, header version, DTB size, and DTB address, and explains that v3 moves DTB to vendor_boot.
2. [Linux ARM64 booting documentation](https://www.kernel.org/doc/Documentation/arm64/booting.txt) defines the DTB alignment, ARM64 Image header, MMU-off entry, x0 DTB convention, EL2/non-secure EL1 requirement, and architectural timer handoff conditions.
3. [Community `gta8wifi` BoardConfig](https://github.com/BasGame1/android_device_samsung_gta8wifi/blob/Lineage-22.2/BoardConfig.mk) is WIP/community evidence for `ums512_25c10`, `ttyS1,115200n8`, 2 KiB pages, header v2, DTB-in-boot, DTBO, dynamic partitions, and AVB settings. It is not installed-firmware proof.
4. [Community downstream kernel source](https://github.com/BasGame1/android_kernel_samsung_gta8wifi) is hardware documentation only. It is a one-branch downstream source snapshot for `gta8wifi`, not code to copy wholesale into guideXOS.
5. No authoritative `torvalds/linux` UMS512/SM-X200 DT was established in this pass. Other UMS512 boards are useful corroboration only and must not supply Tab A8 panel/GPIO/regulator constants.

## Boot-chain reconstruction

The only safe current model is:

```text
Boot ROM
  -> Unisoc/Samsung early boot stages (exact ordering unknown)
  -> Samsung bootloader / Download Mode path
  -> verified Android boot image and optional DTBO/vendor_boot
  -> ARM64 Linux Image + DTB + ramdisk/bootconfig
```

This is deliberately not described as SPL/U-Boot/TrustZone ordering because the actual firmware has not been correlated. The tablet is not treated as a Pi-style UEFI machine; `EFI/BOOT/BOOTAA64.EFI` is not the expected route without firmware evidence.

The eventual adapter should normalize the stock handoff at one small boundary:

```text
Samsung/Unisoc bootloader
        -> Android-compatible guideXOS boot image
        -> small ARM64 SamsungGalaxyTabA8 adapter
        -> normalized common BootInfo
        -> common ARM64 kernel
```

The adapter must construct memory map, DTB, framebuffer-if-preserved, ramdisk-if-provided, and platform identity. The common kernel must not consume Android bootloader quirks directly.

## UART, USB, display, storage, and input plan

The likely first diagnostic channel is ranked only after exact DT/source capture:

1. preserved bootloader framebuffer, if the stock handoff leaves a valid buffer;
2. externally accessible stock UART, if its controller, pinmux, base, IRQ, and production enablement are proven;
3. USB gadget serial, if the DWC3/PHY/role-switch path can be brought up early.

No USB stack, native panel initialization, storage driver, touchscreen driver, GPU driver, or SMP scheduling is implemented in T0. The initial T1 target is one boot CPU and one serial/USB/framebuffer marker. The CPU topology, PSCI method, and Cortex-A75/A55 MPIDRs are recorded only when stock DT/kernel evidence is available.

## Risk register

| Risk | Rank | Current treatment |
| --- | --- | --- |
| bootloader authorization | high | do not unlock; require normal OEM-supported path later |
| AVB/vbmeta policy | high | inspect only; do not patch/disable/alter rollback metadata |
| no UEFI handoff | high | Android boot-image adapter boundary |
| exact model/region mismatch | high | identity gate blocks SM-X200 assumptions |
| serial inaccessible | medium | rank preserved framebuffer and USB after evidence |
| display requires complete DSI bring-up | high | preserve-buffer path investigated first |
| early USB unavailable | high | do not assume gadget diagnostics |
| limited SoC documentation | high | triangulate stock DT, Samsung/downstream source, community source |
| dynamic clocks/regulators | high | inventory only before any driver work |
| DTB/DTBO base-overlay pairing | high | enumerate and correlate; never apply blindly |
| storage/DMA/IOMMU coupling | medium | no implementation in T0 |
| heterogeneous SMP | medium | single boot CPU initially |
| microSD pre-kernel access | unknown | require actual bootloader evidence |
| bootloader framebuffer state | unknown | require physical/stock handoff evidence |

## Recovery prerequisites before T1/T2

Before any image touches the tablet, require all of the following:

- exact model and exact installed firmware/build identity;
- complete matching stock firmware, with filenames and SHA-256 recorded;
- tested Samsung Download Mode access;
- a working host recovery/flashing tool tested against stock recovery, without using it for T0;
- battery sufficiently charged;
- known return-to-stock procedure and verified recovery artifacts;
- a documented authorization decision. If the normal OEM-supported path cannot authorize a custom kernel, classify that as a later platform blocker; do not investigate exploits or bypasses.

## Host test result and committed files

The host suite is run with `scripts\test-aarch64-tab-a8-t0.ps1` and currently passes five Python DT/boot tests plus the native platform-map control test. It does not access a device. The baseline reports were generated locally under `out\aarch64-tab-a8-t0\` with Outcome E and are not firmware binaries.

Files added by this reconnaissance implementation:

| File | Role |
| --- | --- |
| `aarch64/tab_a8/tab_a8_platform.h` | SamsungGalaxyTabA8 vocabulary and DTB-derived map contract |
| `scripts/tab_a8_recon.py` | read-only ADB/firmware/boot/DTB/DTBO/vbmeta inspector |
| `scripts/inspect-galaxy-tab-a8.ps1` | live ADB capture wrapper |
| `scripts/inspect-tab-a8-firmware.ps1` | firmware-only wrapper |
| `scripts/test-aarch64-tab-a8-t0.ps1` | host test wrapper |
| `tests/tab_a8_recon_tests.py` | synthetic DT/boot/DTBO safety tests |
| `tests/aarch64_tab_a8_platform_host_tests.cpp` | native platform-map identity/contract test |
| `docs/aarch64/AARCH64_T0_TAB_A8_RECON.md` | this milestone record |

No Samsung firmware binary is committed. The final commit hash is recorded in the delivery message after the implementation is committed; this document itself does not claim a completed physical T0 pass.

## Recommended T1 strategy

Do not flash yet. First obtain a read-only ADB capture and exact matching firmware, run both inspectors, correlate the running build fingerprint with the supplied boot/DTB/DTBO/vbmeta hashes, and resolve GIC/timer/UART/framebuffer/storage/USB evidence. Then build a non-flashing adapter validation artifact or an OEM-authorized test route that preserves a known return to stock. The first physical acceptance should be:

```text
Samsung bootloader
  -> authorized Android-compatible guideXOS boot image
  -> Tab A8 adapter
  -> common ARM64 BootInfo/kernel entry
  -> one diagnostic marker
```

Only after that marker is repeatable should MMU, GIC, timer, scheduler, storage, display, USB, input, and SMP work proceed. No T0 result implies that guideXOS has booted on the tablet.
