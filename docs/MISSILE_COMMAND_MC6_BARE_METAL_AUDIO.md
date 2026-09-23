# Missile Command MC6 — Bare-Metal HDA Streaming Backend

Status: MC6 complete, **Outcome A (bounded: emulated bare-metal hardware;
see §30)**. Read alongside `docs/MISSILE_COMMAND_MC5_APP_MODEL_AUDIO.md`
(MC5 established the App Model `play_pcm`, the 16-voice mixer, and the
hosted WinMM backend; bare metal returned `GX_ERROR_NOT_IMPLEMENTED`).

Outcome: a **real bare-metal audio streaming backend** underneath the
unchanged MC5 App Model/mixer, proven on bare-metal execution (QEMU HDA
hardware: real PCI discovery, real MMIO programming, real CORB/RIRB verb
traffic, real cyclic DMA with observed position motion, real codec
consumption, captured PCM containing the test tones, the real AudioBeep
application playing through it, and a permission-denied twin staying
silent). No App Model redesign: no ABI change, no `play_pcm` signature
change, no Missile Command-specific kernel path, no second mixer, no
gameplay-state change, deterministic campaign fingerprint unchanged.

---

## 1. Pre-existing audio architecture discovered (audited, not assumed)

- App Model ABI: `play_pcm` at offset 240, struct size 248
  (`sdk/include/guidexos/abi.h`, `native_app_runtime.h`, pinned by
  `tests/native_abi_layout_test.cpp`). MC6 changes nothing here.
- Hosted path (`native_app_audio.h/.cpp`): permission gate, validation,
  conversion to S16 mono @ 22050 Hz, 16-voice saturating deterministic
  mixer, `StopOwner`, WinMM `waveout-22050-mono-s16` backend with explicit
  null-sink degrade (`GXOS_AUDIO_BACKEND=null`). Untouched by MC6.
- Bare-metal `host_play_pcm` (`kernel/core/native_elf_baremetal.cpp`):
  validated + permission-checked, then returned
  `GX_ERROR_NOT_IMPLEMENTED` (no backend).
- `kernel/.../pci_audio.*`: PCI scan, BAR mapping, GCTL reset,
  CORB/RIRB bring-up, codec enumeration, stream-descriptor register
  helpers, volume/mute verbs. **Zero callers** for streaming; the only
  consumer was the desktop volume UI (mute toggle).
- `kernel/.../usb_audio*.​*`: UAC1/UAC2 probe + volume/mute +
  `playback_write` through the *bulk* path (no isochronous scheduler).
  Not a usable continuous path; MC6 does not touch USB audio.
- Bootloader (`guideXOSBootLoader/`): builds identity page tables for a
  fixed range list (low 1 MB, kernel span, allocator region, framebuffer,
  ramdisk, ACPI, NIC MMIO). **HDA MMIO was not mapped**: any UEFI-boot
  HDA register access would fault.
- Kernel memory model: the kernel links at `0x100000` but the bootloader
  loads it at an arbitrary physical base (`BootInfo.KernelPhysicalBase`;
  observed `0x19126000`, ~405 MB span with the 512 MB heap BSS).
  `nic`/`virtio`/`mmio` translate device addresses with
  `base + (virt - 0x100000)`. **The audio driver did not**: it cast
  virtual addresses to physical (correct only when load == link base).
- `kernel/docs/HARDWARE_SUPPORT_REPORT.md` listed "PCM Playback PRESENT
  via SD" with "Mixer MISSING": stale optimism. No mixer existed, no PCM
  submit path existed from any runtime to any DMA engine.

Missing link (mixer output → speaker): kernel mixer, backend bring-up,
DMA ring, refill pump, lifecycle, diagnostics. MC6 builds exactly that.

## 2. Controller/backend selected

HDA first (primary on all supported PC hardware; the existing driver
targets it). Verified on QEMU 11.0.0:
`intel-hda` (Intel 8086:2668 ICH6, GCAP `0x4401` = 4 in + 4 out streams,
64-bit) at PCI 00:05.0, MMIO BAR `0x81060000` (identity-mapped by the new
bootloader range), plus `ich9-intel-hda` + `hda-output` (identical
behavior). Codec: `hda-duplex` (vendor `0x1AF40022`, AFG nid 1, DAC nid
2, line-out pin nid 3, ADC nid 4, line-in pin nid 5 — exact match to
QEMU's descriptor tables, confirmed over the wire).

## 3. Backend architecture

`App → play_pcm (unchanged signature) → permission (unchanged) →
validation/conversion (same rules, freestanding mirror) → 16-voice mixer
(same semantics, static storage) → HDA streaming backend (new) →
pci_audio discovery/verbs (reused + fixed) → DMA/device.`

New freestanding module (no STL, no exceptions, no threads, no
`<string.h>`, MSVC+GCC clean):
`kernel/core/include/kernel/app_audio_stream.h` +
`kernel/core/app_audio_stream.cpp` (auto-picked-up by
`kernel/Makefile` wildcard; registered in both `.vcxproj`s + filters).

Hardware access is fully abstracted through `HdaOps` (MMIO r/w, verb
transport, monotonic ms, log sink, virt→phys hook), so the entire
backend — bring-up sequence, ring bookkeeping, refill, underrun
recovery, lifecycle — is unit-tested on the host against a scripted
mock controller. The kernel wires real MMIO/verbs/ticks/serial.

## 4. Pre-existing driver pieces reused vs new

Reused: PCI scan, BAR enable, GCTL reset, CORB/RIRB bring-up, codec
enumeration walk, all register/verb constant definitions, volume/mute
verb encodings (mirrored for DAC/pin unmute).

Extended (in `pci_audio.*`, no parallel implementation):
- `HDACodec.afgNode` recorded during enumeration (needed for AFG power).
- `encode_hda_format` parameter `uint16_t` → `uint32_t` (cases for
  88200+ Hz were unreachable/truncated; fixes 4 freestanding warnings).
- `configure_stream` output/capture descriptor selection corrected to
  input-first ordering (§6).
- CORB/RIRB statics `alignas(128)` (HDA requires 128-byte-aligned ring
  bases; was word-aligned).
- RIRB stall fix: clear RINTFL after each consumed response (QEMU's
  engine stops producing once `rirb_count == RINTCNT`; the driver never
  cleared it, so exactly one verb ever completed) + poll timeout
  10000 → 100000 spins.
- `set_kernel_physical_base()` + slide translation for CORB/RIRB/BDL
  addresses (§7).
- Diagnostic accessors (`hda_rirb_wp/entry/size`, `hda_corb_base`,
  `hda_rirb_base`) and `hda_immediate_verb` (IC/IR doorbell, no DMA).

New: mixer mirror, 48 kHz stereo adaptation, cyclic ring, pump,
underrun/stall handling, lifecycle, counters, self-test, glue
(`native_elf_baremetal.cpp`: `HdaOps` wiring, staging buffer,
lazy `ensure_audio_backend`, rewritten `host_play_pcm`, `audioOwner`
+ `StopOwner` cleanup, `pump_desktop` hook, `app_audio_pump` for the
main loop, opt-in boot probes), boot-time detection in `main.cpp`,
bootloader HDA BAR identity-map (`pci.h/.cpp`, `main.cpp`; no BootInfo
change).

## 5. DMA/BDL design

- 8 descriptors × 1024 device frames × stereo S16 = 4096 bytes each;
  32 KiB PCM + one 128-byte-aligned BDL, all static kernel-image
  storage (lifetime = boot; never freed; never stack; never app memory).
- BDL programmed once (BDPL/BDPU/CBL=32768/LVI=7, IOC on all); refills
  only rewrite PCM + track the render window. No per-refill register
  traffic; no IRQ path (polled LPIB; GIE/CIE untouched, as before).
- Ring always holds valid data (mixed audio or silence). A late pump
  replays bounded stale audio (counted), never garbage, never a fault.
- Addresses translated with the kernel slide (§7), verified nonzero
  and < 4 GiB fail-closed. x86 DMA is coherent (no flush). No IOMMU on
  the supported path (documented limitation).

## 6. Sample/device format

Mixer format unchanged: S16 mono @ 22050 Hz. Device format: fixed
**48000 Hz stereo S16LE** (HDA-mandatory base combo; no negotiation).
Adaptation is deterministic nearest-neighbor resample
(`src = (n·22050)/48000` computed overflow-free) + channel duplication,
keyed to a global device-frame counter so phase is continuous across
chunks and idle gaps (470/471 mix frames per 1024-frame chunk, exact).
Rationale, algorithm, and vectors are unit-tested (determinism,
duplication spot-checks, silence, DC, monotonicity).

**Audit finding fixed here:** stream descriptors are input-first per
the HDA spec (confirmed in the Intel spec, OSDev, FreeBSD, ReactOS, and
QEMU sources: OSDn base = `0x80 + ISS·0x20`). The old driver used index
0 for playback — a *capture* descriptor on 4-in/4-out controllers
(ICH6/QEMU), whose LPIB never moves for playback. Both the shared
`configure_stream` and the backend now select output `ISS+n` from GCAP
(QEMU: SD4 @ `0x100`). Pinned by a dedicated host test.

## 7. DMA/memory safety (root-cause finding)

QEMU device tracing (`-device intel-hda,debug=2`) showed the controller
receiving verb `0x00000000` for every CORB entry while the driver wrote
`0x000F0000`: device DMA reads missed the CPU's writes. The kernel
links at `0x100000` but loads at `KernelPhysicalBase` (observed
`0x19126000`); the audio path handed raw virtual addresses to hardware.
Fix: translate with `base + (virt − 0x100000)` (the canonical formula
already used by `nic`/`virtio`/`mmio`), wired from BootInfo in
`main.cpp`, applied to CORB/RIRB bases, legacy BDL pointers, and the new
ring via the `HdaOps.virt_to_phys` hook. Verified on the wire: BDL
bytes land at translated addresses, codec responses arrive with exact
spec values, LPIB advances. The bootloader additionally identity-maps
the HDA BAR0 (NIC pattern, no BootInfo change) so UEFI-boot register
access cannot fault. Bootloader logs `Mapping HDA audio MMIO` on real
boots.

## 8. Refill/completion mechanism

`Backend::pump()` (task context only; never sleeps; bounded work):
reads LPIB → renders due chunks (mix exact per-chunk counts → adapt) →
tracks the render window 3 chunks ahead of the cursor. Called from
`host_play_pcm` (first-sound latency), `pump_desktop` (app frame/event
cadence), and the kernel main loop (idle sustainability; without this,
post-exit rings loop stale content). Idle flush renders trailing
silence, then the pump idles on LPIB reads only.

## 9. Underrun handling

- Cursor laps the render window → `underruns++`, resync to cursor+1,
  render 3 ahead (bounded, self-healing; voices continue, gap counted).
- Frozen cursor with pending voices (stalled device or tight pump
  loops) → mix one bounded quantum to discard per pump (real-time voice
  lifetimes, same degrade philosophy as the Failed/Unavailable paths),
  counted as an underrun. Found by unit test (steady-state deadlock at
  exactly cursor+3 with a frozen cursor).
- Stream error STS bits → stop/reset/reprogram/restart, ≤3 attempts,
  then sticky `Failed` (mixer-only degrade). Recovery path unit-tested
  to exhaustion.

## 10. Lifecycle/cleanup behavior

States: `Unavailable` (no HW / init failed; mixer-only degrade) →
`Ready` (DMA silence-or-audio loop running) → `Failed` (sticky after
exhausted recovery; mixer-only degrade). No idle teardown: silence
loops cheaply; playback resumes via cursor resync (≤43 ms). Owner =
bare-metal runtime sequence; `run_package` cleanup calls
`stop_owner()` (mirrors hosted `Cleanup → BackendStopOwner`); in-flight
DMA completes its bounded ring; the stream stays up for the next app.
One app's exit cannot disturb another owner's voices (unit-pinned).
Repeated launch/exit cycles pinned (4× + relaunch-after-reclaim).

## 11. Permission behavior

Unchanged and now live on bare metal: manifest `audio.output`
exact-token gate (`hasAudioOutput`) checked before any format work.
Proven bare-metal: permitted AudioBeep 2× `GX_OK` (plays 0x18→0x1A);
denied twin 2× `GX_ERROR_PERMISSION_DENIED`, zero voices added
(counter frozen 0x1A), clean exit, lifecycle PASS. Oversized input now
maps to `GX_ERROR_UNSUPPORTED` (was `INVALID_ARGUMENT` in the MC5 stub),
matching the hosted runtime exactly.

## 12. AudioBeep results (bare metal)

Real `audiobeep.elf` + real manifest through production
launch→`play_pcm`→mixer→DMA: discovered, ELF loaded/relocated, two
overlapping beeps `request accepted`, clean exit, lifecycle PASS,
relaunch-safe. Captured PCM contains the beeps (square-wave bursts,
stereo-duplicated, correct post-boot timeline).

## 13. Missile Command results (bare metal)

Loads ELF + DD.ini + city art + **all six audio voices** on bare metal
(`audio voices loaded`, `initial state ready`) — then fails at its
first frame: the game renders 480×360 but bare-metal `present_frame`
accepts only 448×553 (pre-existing PacMan-era desktop limitation,
unrelated to audio; no MC-specific kernel path will be carved for it
in MC6). Zero MC voices queued (counter frozen), game state untouched.
MC's audio integration is unchanged and proven through the identical
path (AudioBeep) + hosted MC (16/16) + the deterministic campaign test
below. Recommended MC7: frame-size negotiation for 480×360 windows.

## 14. Overlap results

Bare metal: AudioBeep's two overlapping beeps accepted into distinct
voices and mixed (plays +2, voice count 2 in unit/self-test mirrors).
Hosted: overlap re-proven (MC5 smoke). Unit: two-voice sum +
saturation both rails + determinism pinned.

## 15. Deterministic campaign fingerprint

`16492225105589479459` — re-verified via
`scripts/run-missilecommand-state-test.ps1` (PASS). Sound affects no
simulation state.

## 16. ABI compatibility result

`tests/native_abi_layout_test.cpp` PASS: `play_pcm` offset 240, struct
size 248, all prior offsets unchanged. **No ABI change in MC6.**

## 17. Hosted WinMM regression

- `scripts/run-app-audio-mixer-test.ps1`: PASS (MC5 suite untouched).
- `scripts/smoke-audiobeep.ps1`: 9/9 PASS.
- `scripts/smoke-missilecommand-mc5.ps1`: 16/16 PASS
  (`playAccepts=8`, `waveout-22050-mono-s16`).
- Hosted `native_app_audio.*` and `native_app_runtime.*` unmodified
  (see file list).

## 18. QEMU/build results

- Freestanding `ARCH=amd64` kernel builds clean repeatedly (normal and
  `GXOS_AUDIO_BOOT_SELFTEST` / `GXOS_AUDIO_MC_PROOF` variants); the
  MC5-noted `string.h` blocker does not reproduce on the make path
  (new files avoid `<string.h>` entirely; `cxx_runtime` provides
  `memcpy`/`memset`).
- MSVC project wiring added (sources + headers + filters for both
  kernel projects); bootloader rebuilds clean (only pre-existing
  warnings).
- New host suite `scripts/run-app-audio-stream-test.ps1`: PASS
  (validation/conversion/mixer parity, adaptation, bring-up success +
  failure paths incl. no-ops/bad-path/verb-failure/all-verb-failure,
  SD4 selection, refill, wraparound, underrun, stream-error recovery
  to exhaustion, relaunch cycles, default-route fallback ± motion,
  mock + DMA self-tests).
- QEMU 11.0.0 runs: `scripts/run-qemu-audio-proof.ps1` **PASS**
  (10/10 markers); variant/triage scripts for controller/codec
  combinations and HDA debug tracing.

## 19. Physical hardware tested

None available in this environment. All bare-metal execution is on
QEMU-emulated HDA hardware (guest driver performs real PCI/MMIO/DMA
transactions; no host OS in the loop).

## 20. Exact bare-metal playback evidence

- PCI: 8086:2668 at 00:05.0, BAR `0x81060000` mapped; `Audio
  controllers detected: 01`.
- CORB/RIRB: WP/RP track, RIRBWP advances 1/verb, responses carry exact
  spec values (`vendor=0x1AF40022`, `rev`, subnodes; AFG=1, DAC=2,
  pin=3).
- Codec bring-up on the wire (QEMU `debug=2` trace): AFG D0, DAC caps,
  `SET_CONV_FMT 0x11`, `SET_CONV_STREAM 0x10` (`dac: stream 1`),
  DAC/pin unmute, pin routing/enable/EAPD, pin readback `0xC0`.
- Stream: SD4 programmed (FMT `0x0011`, CBL 32768, LVI 7, BDL phys,
  TAG 1, RUN set), `start 1 (ring buf 32768 bytes)`.
- Continuous DMA: 1000+ `dma: entry` trace lines cycling entries 0–7
  with wraparound; LPIB advances (`dma=1` in self-test).
- Captured PCM (QEMU wav backend): test tones present (square-wave
  bursts, L/R duplicated, correct timeline); post-exit capture tail is
  digital silence (main-loop pump drains the ring).
- App level: AudioBeep 2× accepted; denied twin 2× denied + silent;
  backend stays `Ready (hda-48000-stereo-s16, codecPath=1)`.

## 21. Audibility status

**Device-level + PCM-content evidence only — explicitly NOT
human-confirmed.** Tones are proven as rendered PCM bytes in the
capture (shape, stereo, timing all match the guests transmissions).
No human listened (headless environment, no audio output device).
QEMU-side note: its mixer resamples 48 kHz → 44.1 kHz (+8.8% pitch
shift: 440→~480 Hz, 220→~240 Hz observed) and fragments delivery via
buffer overruns; these are capture artifacts, not driver defects.

## 22. Failures/limitations

- No physical (non-emulated) hardware test; no human audibility check.
- MC game sounds on bare metal blocked by the pre-existing 448×553
  `present_frame` limit (§13); MC7 candidate.
- No HDA interrupt wiring (polled LPIB by design; GIE/CIE untouched).
- No USB-audio streaming (unchanged; bulk-only `playback_write`).
- VT-d/IOMMU would need a DMA-window pass (reported limitation).
- IC/IR doorbell unimplemented by QEMU (probed, TIMEOUT; CORB/RIRB is
  the path; helper retained for hardware that implements it).
- QEMU EAPD verb unhandled by the codec model (expected; continued).
- Capture pacing/overruns are QEMU host-side artifacts (§21).

## 23. Changed files

New: `kernel/core/app_audio_stream.cpp`,
`kernel/core/include/kernel/app_audio_stream.h`,
`tests/app_audio_stream_test.cpp`,
`scripts/run-app-audio-stream-test.ps1`,
`scripts/run-qemu-audio-proof.ps1`,
`scripts/run-qemu-mc-audio.ps1`,
`scripts/run-qemu-audio-variant.ps1`, this report.
Modified: `kernel/core/pci_audio.cpp`,
`kernel/core/include/kernel/pci_audio.h`,
`kernel/core/native_elf_baremetal.cpp`,
`kernel/core/include/kernel/native_elf_baremetal.h`,
`kernel/core/main.cpp`, `guideXOSBootLoader/main.cpp`,
`guideXOSBootLoader/pci.cpp`, `guideXOSBootLoader/pci.h`,
`guideXOSKernel.vcxproj{,.filters}`,
`guideXOSServer.vcxproj{,.filters}`.
Deliberately untouched: ABI/SDK, `native_app_audio.*`,
`native_app_runtime.*`, Missile Command sources/assets, USB audio,
`docs/docs.zip`.

## 24. Tests added

`tests/app_audio_stream_test.cpp` + runner (§18). Covers new logic:
DMA ring bookkeeping, descriptor wraparound, refill state, mono/stereo
adaptation, position accounting, underrun + stall state machines,
restart/recovery to exhaustion, lifecycle transitions, default-route
fallback with/without DMA motion, SD4 selection, end-to-end self-test.

## 25. Recommended MC7 scope

1. Frame-size negotiation (480×360 present) to unlock MC on bare metal.
2. Real-hardware (non-emulated HDA) validation + human audibility check.
3. Optional HDA interrupt (IOC/BCIS) completion path to complement LPIB
   polling.
4. USB-audio isochronous scheduler (separate workstream; MC6 left USB
   untouched).
5. IOMMU/VT-d DMA-window pass for hardened platforms.
6. Keep the proof scripts green in CI (QEMU HDA + marker gates).

## 26. Outcome classification

**Outcome A (bounded).** Every MC6 architectural goal is met and proven
on bare-metal execution with two explicit bounds: (a) in-game Missile
Command sounds on bare metal are blocked by the unrelated pre-existing
448×553 presentation limit (§13) — the MC audio path itself is proven
identical via AudioBeep/hosted-MC/fingerprint; (b) audibility is
PCM-proven, not human-confirmed (§21). No serious unresolved DMA/audio
issue remains: all root causes found (SD order, DMA slide, RIRB stall,
ring alignment) were fixed, verified on the wire, and pinned by tests.
