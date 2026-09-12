# guideXOS AARCH64-P1 — Raspberry Pi 4 First Physical Boot

Status: preparatory physical-platform implementation complete; physical silicon
validation pending.  This document is deliberately evidence-first: no Raspberry
Pi was attached to this workstation during this pass, so no physical boot,
board revision, RAM size, firmware hash, UART capture, framebuffer capture, or
cold-boot result is claimed.

## Outcome and safety record

The additive Raspberry Pi 4 path is code- and staging-ready.  It does not claim
or assign any physical P1 result.  The required proof remains three independent cold power-on
boots reaching `AARCH64_P1_PASS` on a real BCM2711.

| Item | Recorded result |
|---|---|
| Repository | `D:\dev\guideXOSServer_AARCH64` |
| Branch | `AARCH64_SUPPORT` |
| Starting HEAD | `ec5ac364a48901c23a1cac0f1c2f52884086f42b1` |
| Starting subject | `developer-studio: converge SDK and enable ARM64 IDE workflow` |
| Ending HEAD | Local freeze commit; authoritative value is recorded by repository HEAD and the final report |
| Upstream/divergence | `origin/AARCH64_SUPPORT`, `0 0` at the safety gate; `1 0` after the local freeze commit |
| Push | Not performed |
| Board model/revision | Unknown; no physical board attached |
| Board RAM | Unknown; no physical board attached |
| UEFI distribution/version | Intended external PFTF Raspberry Pi 4 UEFI v1.50; not supplied or run |
| Firmware hashes/provenance | Not available; `-FirmwareDirectory` was not supplied |

The source worktree contains the requested implementation and generated local
`out/` artifacts.  No physical media, EEPROM, or firmware source directory was
modified.

## Implemented physical path

The build selects `GXOS_AARCH64_RPI4_P1`, but the loader still verifies the
booted platform from the firmware-supplied DTB.  The parser exposes bounded
`Unknown`, `QemuVirt`, and `RaspberryPi4` identities, honors parent cell widths,
and translates nested `ranges` for SoC devices.  The common kernel services are
shared with the QEMU path; P1 is not a second ARM64 kernel.

The physical profile is intentionally minimal:

- UEFI `EFI/BOOT/BOOTAA64.EFI`, with no U-Boot path.
- One CPU/core only.
- Common GICv2, architectural physical timer PPI, exception/vector, MMU, and
  scheduler services.
- No ramdisk, USB, HID, SD/eMMC, Ethernet, Wi-Fi, Bluetooth, audio, SMP, GPU
  acceleration, GPIO API, or Developer Studio physical proof.
- Optional UEFI GOP status panel; the panel is not required for P1.
- Physical scheduler gate: 1,000 preemptions and at least 100 timer IRQs.

## Loader and stage evidence

The loader emits the following physical markers through the UEFI console before
EBS where possible, then the kernel continues on the discovered PL011 after EBS:

```text
[A64 UEFI] P1-01 loader entered
[A64 UEFI] P1-02 platform discovered
[A64 UEFI] P1-03 memory map capture
[guideXOS] P1-04 ExitBootServices: PASS
[guideXOS] P1-05 physical kernel entry: PASS
[guideXOS] P1-06 owned kernel stack: PASS
[guideXOS] P1-07 MMU established: PASS
[guideXOS] P1-08 exceptions established: PASS
[guideXOS] GICv2 physical: initialized
[guideXOS] P1-10 timer IRQ observed: PASS
[guideXOS] P1-11 scheduler switch observed: PASS
[guideXOS] P1-PASS
AARCH64_P1_PASS
```

The current code prints the actual discovered UART and GIC bases in the
pre-EBS summary, captures the EBS map before the EBS call, and refuses to
continue when the DTB does not identify a supported Pi 4 platform.

## Load address, memory, and MMU policy

The kernel remains a fixed-address `ET_EXEC` image linked at and loaded at
`0x40000000`; the generated `kernel.elf` entry is `0x40000000`.  Before
`AllocateAddress`, the physical loader scans the current UEFI map and requires
the complete image range to lie in one `EfiConventionalMemory` descriptor.
`AllocateAddress` then performs the firmware reservation.  A failure is
diagnostic; the loader does not fall back to an arbitrary relocation.

P1 uses the UEFI map as allocator authority and intentionally limits allocator
and normal-RAM mappings to the low 2 GiB (`0x00000000`–`0x80000000`).  Device
MMIO mappings are bounded below 4 GiB so the expected BCM2711 GIC/UART windows
can be mapped.  The exact total/conventional memory, firmware-owned ranges,
stack range, DTB range, and image reservation are unknown until a physical
UEFI memory-map capture is returned.

The MMU preserves the existing ARM64 permissions and WXN policy.  P1 adds the
discovered UART/GIC device mappings and, when valid GOP is present, maps the
framebuffer with the existing non-cacheable normal-memory attribute.  The
loader cleans the image and invalidates instruction cache; table construction
uses cache clean plus DSB, and MMU/GIC/timer transitions use the existing DSB,
DMB, and ISB ordering.  No silicon cache/barrier result is claimed yet.

## DTB and expected BCM2711 platform data

The source is the EFI DTB configuration-table entry, copied into loader-owned
memory before EBS.  The loader validates the FDT header, totalsize, structure
block, strings block, and copy bounds.  No separate hard-coded Pi DTB is loaded.

The host controls cover `raspberrypi,4-model-b` and `brcm,bcm2711`, QEMU
`linux,dummy-virt`, and an unknown-board rejection.  The synthetic Pi control
also verifies a `soc` bus range translating a PL011 from `0x7e201000` to
`0xfe201000`.  These are expected/control values, not observations from a
physical board.

Expected/control values for the first test are:

| Resource | P1 handling | Physical observation |
|---|---|---|
| RAM | DTB `reg`; allocator still bounded by UEFI map and low-2-GiB policy | Pending |
| UART | DTB PL011 compatible and parent/range translation | Pending; control expects `0xfe201000` |
| GIC | DTB GICv2/GIC-400 compatible; common GICv2 driver | Pending; control expects distributor `0xff841000`, CPU interface `0xff842000` |
| Timer | DTB architectural timer physical PPI; runtime `CNTFRQ_EL0` | Pending; control expects PPI IRQ 30 |
| Framebuffer | UEFI GOP only, optional | Pending; absent is allowed |

The physical UART path does not retain QEMU’s `0x09000000` fallback after DTB
discovery.  If the actual firmware DTB exposes a different usable PL011 base,
that discovered, page-aligned base is handed to the common serial driver.

For a USB-TTL console, use 3.3-V signalling only and cross TX/RX: Pi TX to
adapter RX, Pi RX to adapter TX, and common ground.  Raspberry Pi’s official
documentation identifies GPIO14 as TX on header pin 8, GPIO15 as RX on header
pin 10, and the standard ground connection as pin 6; it also warns that the
UART operates at 3.3 V.  Configure the host adapter for 115200 8-N-1.  Do not
use an RS-232 voltage-level cable and do not have the tooling configure a host
COM port.

References: [Raspberry Pi hardware/serial documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html),
[Raspberry Pi UART and voltage documentation](https://www.raspberrypi.com/documentation/computers/configuration.html),
[BCM2711 peripherals reference](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf),
[PFTF RPi4 releases, including v1.50](https://github.com/pftf/RPi4/releases).

## Boot-tree staging

The deterministic build command is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-rpi4-p1.ps1
```

It produces:

```text
out/aarch64-rpi4-p1/
  boot/
    EFI/BOOT/BOOTAA64.EFI
    kernel.elf
  manifest.txt
  hashes.txt
  firmware-provenance.txt
  PHYSICAL_TEST_CHECKLIST.md
```

The current artifact details are:

| Artifact | Size | SHA-256 |
|---|---:|---|
| `boot/EFI/BOOT/BOOTAA64.EFI` | 23,552 bytes | `3a1467bb3e0de9770d942412fc01b4a66f565b690f9212899bd4f08fc0f83a3f` |
| `boot/kernel.elf` | 120,888 bytes | `a595064dcf0da7a12b42d781691c152408512f6fa3454e34c05400a703f79584` |

The EFI image was verified as PE/COFF ARM64 and the kernel as ELF64
`EM_AARCH64`.  The copy manifest currently contains only those two guideXOS
files.  To combine externally supplied firmware into a separate staging tree:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\stage-aarch64-rpi4-p1.ps1 `
  -ArtifactDirectory .\out\aarch64-rpi4-p1 `
  -OutputDirectory .\out\aarch64-rpi4-p1 `
  -FirmwareDirectory <path-to-external-pftf-tree>
```

`FirmwareDirectory` is optional, never downloaded implicitly, and is copied
without modifying the source.  The scripts only create/update repository-local
generated output.  They do not format, partition, overwrite, or write a
removable disk and do not update Pi EEPROM.

## Verification performed in this session

| Check | Result |
|---|---|
| P1 host platform controls | PASS: Pi/QEMU/unknown identity, Pi `ranges`, UART, GIC, timer, overflow, fixed-load availability, framebuffer bounds |
| P1 ARM64 cross-build and staging | PASS |
| Phase-2 host controls + three fresh QEMU boots | PASS; each reached `AARCH64_PHASE2_PASS`, including timer IRQ count 1 |
| Historical Phase-3 host contract | PASS during build |
| Historical Phase-3 QEMU | Foundation, DTB, MMU, exceptions, memory, GIC, timer, and cooperative marker reached; existing 10,000-preemption gate did not complete within 120 seconds |
| Parser isolation | The same Phase-3 stall occurred with the pre-change parser, so it was not introduced by the P1 DTB parser change |
| Phase-12 QEMU path | PASS: one fresh boot reached `AARCH64_PHASE12_PASS` and completed the existing IDE/build/run and scheduler/VFS checks |
| Physical Pi boot | Not run: no board/media/firmware tree available |

The Phase-3 QEMU timeout is not reported as a pass.  Its deepest observed
marker was `cooperative scheduling: PASS switches=10004`; no `AARCH64_PHASE3_PASS`
was emitted in the bounded run.  The pre-existing repository documentation
records earlier Phase-3 QEMU passes, but this session’s current runtime result
is the evidence used here.

## Physical test checklist

1. Power the Pi off.
2. Prepare a FAT32 UEFI boot filesystem independently.
3. Install/preserve the selected external PFTF firmware; do not update Pi EEPROM.
4. Copy the files listed in `manifest.txt` to the filesystem root, preserving
   `EFI/BOOT/BOOTAA64.EFI` and `kernel.elf`.
5. Connect HDMI.
6. Optionally connect a 3.3-V USB-TTL adapter as documented above.
7. Insert the boot medium and power on.
8. Record the deepest exact stage reached; photograph a stopped display and save
   the complete serial log.
9. Do not repeat a cold boot until the prior evidence is recorded.
10. Repeat for three independent cold power-on boots only after each prior
    result is captured.

For each boot, record board model/revision, RAM, UEFI implementation/version,
firmware hashes, actual entry EL, UEFI map totals/conventional ranges, DTB
compatible/model, RAM regions, UART/GIC/timer values, framebuffer geometry,
MMU/cache/barrier observations, exception self-test, timer count, scheduler
preemptions, and the exact final marker.

## Acceptance status, gaps, and P2

P1 is currently classified as “code/staging ready; physical outcome pending,”
not P1-A/P1-B/P1-C/P1-D/P1-E.  There is no observed physical last-stage failure
to classify.  The remaining blockers are a real BCM2711 board, an externally
selected and hashed PFTF tree, a prepared FAT32 medium, serial/HDMI evidence,
and three cold boots.

Recommended P2 scope after physical P1 classification: preserve the single-core proof while adding
physical input/storage only as separately gated work, then SMP/per-CPU bring-up
and board-specific peripheral policy.  USB, SD/eMMC, networking, Bluetooth,
audio, GPU acceleration, GPIO APIs, and the full desktop remain out of P1.

## Commit status

Local freeze commit subject: `aarch64: prepare Raspberry Pi 4 physical boot`.
No physical boot result is implied by the commit, and nothing is pushed.
