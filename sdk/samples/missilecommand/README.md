# guideXOS Native ELF Missile Command (MC2 playable)

MC2 first playable defense loop for the VB6 Missile Command port. The VB6
original (`DD.vbp`, `FrmDD.frm`, `Module1.bas`, `DD.ini`, `build1.gif`, WAVs)
stays the behavioral/artistic reference; this directory is the native
implementation. Foundation details live in
`docs/MISSILE_COMMAND_MC1_ARCHITECTURE.md`; MC2 gameplay in
`docs/MISSILE_COMMAND_MC2_DEFENSE_LOOP.md`.

## What MC2 proves

- Full L1 combat loop derived from re-traced VB6 source: hostile spawn
  quota (10, in-flight cap 5), central-battery defensive fire (quota 50),
  forced-snap detonation, square interception, MIRV splits (15%), hostile
  bomb-chain (`bExplodeb`), city loss, win (quota + pool drain) and lose
  (all cities dead) with restart.
- Deterministic fixed-step simulation (50 ms tick = one VB Sync tick;
  10 ms poll, 250 ms clamp, 8 catch-up max, ~16 ms visual throttle).
- Faithful input: left-click fires; right-drag fires per the ABI (the
  hosted compositor currently drops button-2 moves — documented game-side
  difference); right press alone correctly does nothing; R/Enter/Space
  restarts a finished level; ESC/close exits cleanly.
- Readable combat scene on the 480x360 framebuffer: ground, alive cities
  vs rubble, battery, hostile/defensive warheads with red trails (Smart
  heads only, like the original), growing blast rings, and a status
  overlay (cities, bombs/missiles remaining, actives, outcome + hints).

## Layout

```text
sdk/samples/missilecommand/
  missilecommand_state.h  pure deterministic game (host-testable, no ABI)
  main.cpp                gx_main: window, events, framebuffer, fixed-step
  app.json                source manifest (com.guidexos.samples.*)
  CMakeLists.txt
```

Pure logic lives in `missilecommand_state.h` so
`tests/missilecommand_state_test.cpp` can verify it with host g++.
Run it with `scripts/run-missilecommand-state-test.ps1`; run the live
combat smoke with `scripts/smoke-missilecommand-mc2.ps1`.

## Build

Direct LLVM path (MinGW g++ emits PE/COFF, not Native ELF):

```powershell
$clang = "C:\Program Files\LLVM\bin\clang++.exe"
$lld = "C:\Program Files\LLVM\bin\ld.lld.exe"
& $clang --target=x86_64-unknown-elf -std=c++11 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -mno-red-zone -Isdk\include -Isdk\samples\missilecommand -c sdk\samples\missilecommand\main.cpp -o out\mc2\main.o
& $lld -m elf_x86_64 -static -e gx_main out\mc2\main.o -o Apps\MissileCommand\bin\amd64\missilecommand.elf
```

Or via `sdk/build-samples.ps1`, which stages the manifest with the installed
app ID (`com.guidexos.missilecommand`) and verifies the ELF header.
