# guideXOS Native ELF Missile Command (MC5 campaign + audio)

MC5 full L1-L10 campaign for the VB6 Missile Command port, with original
`build1.gif` city art and original WAV voices through the App Model audio
call. The VB6
original (`DD.vbp`, `FrmDD.frm`, `Module1.bas`, `DD.ini`, `build1.gif`, WAVs)
stays the behavioral/artistic reference; this directory is the native
implementation. Foundation details live in
`docs/MISSILE_COMMAND_MC1_ARCHITECTURE.md`, the L1 loop in
`docs/MISSILE_COMMAND_MC2_DEFENSE_LOOP.md`, the L1-L3 campaign in
`docs/MISSILE_COMMAND_MC3_CAMPAIGN_SMARTBOMBS.md`, the full campaign,
city art, and audio findings in
`docs/MISSILE_COMMAND_MC4_CAMPAIGN_CITYART_AUDIO.md`, and the reusable App
Model audio implementation in
`docs/MISSILE_COMMAND_MC5_APP_MODEL_AUDIO.md`.

## What MC5 proves

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
- Audio: first App Model audio client. The six verified voices
  (`resources/audio/*.wav`: alarm/empty/explode/split PCM8 11025 Hz,
  ohno PCM16 22050 Hz, swoosh converted from ADPCM stereo to PCM16 mono
  22050 Hz) play through the appended `play_pcm` host call as pure side
  effects of verified `sndPlaySound` events (launch, refused, detonation,
  split, level start, campaign lost). Missing upstream files
  (Thunder/Error) and the commented-out win jingle stay silent, exactly
  like the original. Identical seed + inputs + ticks fingerprint
  identically with audio enabled, unavailable, or denied.

## Layout

```text
sdk/samples/missilecommand/
  missilecommand_state.h     pure deterministic campaign (host-testable, no ABI)
  missilecommand_city_art.h  GXIM parse + city blit sampling (host-testable)
  missilecommand_audio.h     WAV decode + tick-event sound mapping (host-testable)
  main.cpp                   gx_main: DD.ini/city.gximg/audio load, window,
                             events, fixed-step ticks, framebuffer rendering,
                             play_pcm side effects
  resources/DD.ini           original level configuration (staged resource)
  resources/city.gximg       converted build1.gif city art (staged resource)
  resources/audio/*.wav      original voices + converted swoosh (staged)
  app.json                   source manifest (com.guidexos.samples.*)
  CMakeLists.txt
```

Pure logic lives in `missilecommand_state.h` (plus `missilecommand_city_art.h`
and `missilecommand_audio.h`)
so `tests/missilecommand_state_test.cpp` can verify it with host g++.
Run it with `scripts/run-missilecommand-state-test.ps1`; run the pointer
fix test with `scripts/run-compositor-pointer-buttons-test.ps1`; run the
mixer/platform audio tests with `scripts/run-app-audio-mixer-test.ps1`.
The city art conversion is `scripts/convert-missilecommand-city.ps1`
(GIF -> app-format GXIM, deterministic, verified against a pure
byte-level decode of the original); the swoosh conversion is
`scripts/convert-missilecommand-swoosh.py` (MS-ADPCM stereo -> PCM16 mono,
deterministic, sample-verified) staged by
`scripts/stage-missilecommand-audio.ps1`.

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
