# guideXOS Native ELF Missile Command (MC3 campaign)

MC3 L1-L3 campaign for the VB6 Missile Command port. The VB6
original (`DD.vbp`, `FrmDD.frm`, `Module1.bas`, `DD.ini`, `build1.gif`, WAVs)
stays the behavioral/artistic reference; this directory is the native
implementation. Foundation details live in
`docs/MISSILE_COMMAND_MC1_ARCHITECTURE.md`, the L1 loop in
`docs/MISSILE_COMMAND_MC2_DEFENSE_LOOP.md`, and the campaign in
`docs/MISSILE_COMMAND_MC3_CAMPAIGN_SMARTBOMBS.md`.

## What MC3 proves

- Genuine L1 -> L2 -> L3 progression with per-level data, deterministic
  40-tick LEVEL COMPLETE dwell, GAME COMPLETE terminal, and restart to L1.
  Destroyed cities stay destroyed across levels, like the original.
- Runtime `DD.ini` (`resources/DD.ini`, byte-identical original) loaded via
  `file_read_all`, parsed freestanding with per-row validation and a
  deterministic compiled fallback. Overlay shows `ini=run` vs `ini=fb`.
- Natural smart bombs at L3 (`Smart=30`, fresh roll per spawn incl. split
  children) with VB-exact evasion; MIRV splits with the preserved original
  quota quirk (children consume the missile counter, not the bomb counter).
- VB-faithful input through the fixed compositor: left DOWN fires,
  right-drag fires, right DOWN alone does nothing.
- Deterministic fixed-step simulation (50 ms tick = one VB Sync tick;
  10 ms poll, 250 ms clamp, catch-up max 8, ~16 ms visual throttle).
- Readable combat scene on the 480x360 framebuffer: ground, alive cities
  vs rubble, battery, hostile/defensive warheads with red trails (Smart
  heads only, like the original), growing blast rings, and a status
  overlay (level/name, cities, bombs/missiles remaining, actives, INI
  source, outcome + hints).

## Layout

```text
sdk/samples/missilecommand/
  missilecommand_state.h  pure deterministic campaign (host-testable, no ABI)
  main.cpp                gx_main: DD.ini load, window, events, fixed-step
  resources/DD.ini        original level configuration (staged resource)
  app.json                source manifest (com.guidexos.samples.*)
  CMakeLists.txt
```

Pure logic lives in `missilecommand_state.h` so
`tests/missilecommand_state_test.cpp` can verify it with host g++.
Run it with `scripts/run-missilecommand-state-test.ps1`; run the pointer
fix test with `scripts/run-compositor-pointer-buttons-test.ps1`; run the
live campaign smoke with `scripts/smoke-missilecommand-mc3.ps1`.

## Build

Direct LLVM path (MinGW g++ emits PE/COFF, not Native ELF):

```powershell
$clang = "C:\Program Files\LLVM\bin\clang++.exe"
$lld = "C:\Program Files\LLVM\bin\ld.lld.exe"
& $clang --target=x86_64-unknown-elf -std=c++11 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -mno-red-zone -Isdk\include -Isdk\samples\missilecommand -c sdk\samples\missilecommand\main.cpp -o out\mc3\main.o
& $lld -m elf_x86_64 -static -e gx_main out\mc3\main.o -o Apps\MissileCommand\bin\amd64\missilecommand.elf
```

Or via `sdk/build-samples.ps1`, which stages the manifest with the installed
app ID (`com.guidexos.missilecommand`), copies `resources/DD.ini`, and
verifies the ELF header.
