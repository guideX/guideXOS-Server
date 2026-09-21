# guideXOS Native ELF Missile Command (MC4 campaign)

MC4 full L1-L10 campaign for the VB6 Missile Command port, with original
`build1.gif` city art. The VB6
original (`DD.vbp`, `FrmDD.frm`, `Module1.bas`, `DD.ini`, `build1.gif`, WAVs)
stays the behavioral/artistic reference; this directory is the native
implementation. Foundation details live in
`docs/MISSILE_COMMAND_MC1_ARCHITECTURE.md`, the L1 loop in
`docs/MISSILE_COMMAND_MC2_DEFENSE_LOOP.md`, the L1-L3 campaign in
`docs/MISSILE_COMMAND_MC3_CAMPAIGN_SMARTBOMBS.md`, and the full campaign,
city art, and audio findings in
`docs/MISSILE_COMMAND_MC4_CAMPAIGN_CITYART_AUDIO.md`.

## What MC4 proves

- Genuine L1 -> ... -> L10 progression with per-level data, deterministic
  40-tick LEVEL COMPLETE dwell, GAME COMPLETE terminal after L10, and
  restart to L1. Destroyed cities stay destroyed across levels, like the
  original.
- Runtime `DD.ini` (`resources/DD.ini`, byte-identical original) loaded via
  `file_read_all`, parsed freestanding with per-row validation and a
  deterministic compiled fallback. Overlay shows `ini=run` vs `ini=fb`.
- Original city art: `resources/city.gximg` is a deterministic build-time
  conversion of the project-owned `build1.gif` into the app-visible GXIM
  format (the same 28-byte `GXIM` layout the PacMan app uses for its
  sprites). Alive cities blit the artwork bottom-anchored into their slots;
  destroyed cities keep the rubble marker; a missing/unparseable resource
  falls back to the MC3 yellow rectangles. Simulation hitboxes are
  unchanged and independent of the artwork.
- Natural smart bombs at L3+ (`Smart=30..90`, fresh roll per spawn incl.
  split children) with VB-exact evasion; MIRV splits with the preserved
  original quota quirk (children consume the missile counter, not the bomb
  counter).
- VB-faithful input through the fixed compositor: left DOWN fires,
  right-drag fires, right DOWN alone does nothing.
- Deterministic fixed-step simulation (50 ms tick = one VB Sync tick;
  10 ms poll, 250 ms clamp, catch-up max 8, ~16 ms visual throttle).
- Readable combat scene on the 480x360 framebuffer: ground, alive cities
  (original art) vs rubble, battery, hostile/defensive warheads with red
  trails (Smart heads only, like the original), growing blast rings, and a
  status overlay (level/name, cities, bombs/missiles remaining, actives, INI
  source, outcome + hints).
- Audio: not implemented (Outcome C). The App Model exposes no audio host
  call, so there is no correct application-visible playback path yet; the
  MC4 doc records the full original WAV inventory, the platform findings,
  and the smallest reusable `audio.output` host-call proposal for MC5.

## Layout

```text
sdk/samples/missilecommand/
  missilecommand_state.h     pure deterministic campaign (host-testable, no ABI)
  missilecommand_city_art.h  GXIM parse + city blit sampling (host-testable)
  main.cpp                   gx_main: DD.ini/city.gximg load, window, events,
                             fixed-step ticks, framebuffer rendering
  resources/DD.ini           original level configuration (staged resource)
  resources/city.gximg       converted build1.gif city art (staged resource)
  app.json                   source manifest (com.guidexos.samples.*)
  CMakeLists.txt
```

Pure logic lives in `missilecommand_state.h` (plus `missilecommand_city_art.h`)
so `tests/missilecommand_state_test.cpp` can verify it with host g++.
Run it with `scripts/run-missilecommand-state-test.ps1`; run the pointer
fix test with `scripts/run-compositor-pointer-buttons-test.ps1`; run the
live campaign smoke with `scripts/smoke-missilecommand-mc4.ps1`.
The city art conversion is `scripts/convert-missilecommand-city.ps1`
(GIF -> app-format GXIM, deterministic, verified against a pure
byte-level decode of the original).

## Build

Direct LLVM path (MinGW g++ emits PE/COFF, not Native ELF):

```powershell
$clang = "C:\Program Files\LLVM\bin\clang++.exe"
$lld = "C:\Program Files\LLVM\bin\ld.lld.exe"
& $clang --target=x86_64-unknown-elf -std=c++11 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -mno-red-zone -Isdk\include -Isdk\samples\missilecommand -c sdk\samples\missilecommand\main.cpp -o out\mc4\main.o
& $lld -m elf_x86_64 -static -e gx_main out\mc4\main.o -o Apps\MissileCommand\bin\amd64\missilecommand.elf
```

Or via `sdk/build-samples.ps1`, which stages the manifest with the installed
app ID (`com.guidexos.missilecommand`), copies `resources/DD.ini` and
`resources/city.gximg`, and verifies the ELF header.
