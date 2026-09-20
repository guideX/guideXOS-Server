# Missile Command MC1 — VB6 Archaeology, Port Architecture, Native Skeleton

Status: MC1 (foundation). Proves the port has a sound footing; does NOT implement
the full game. MC2 scope is proposed at the end of this document.

Source of truth for behavior/art: VB6 project at
`D:\dev\bkup\inactive\missilecommand` (`https://github.com/guideX/missilecommand`,
branch `master`, read-only reference).
Target platform: this repository, branch `MISSILE_COMMAND_APP_MODEL`.

---

## 1. Original VB6 component map

Project file `DD.vbp` (all paths relative to the VB6 repo root):

| File | Role | In build? |
|---|---|---|
| `DD.vbp` | Project. `Type=Exe`, `Startup="FrmDD"`, `ExeName32="MissileCommand.exe"`, `Name="MissileCommand"`, company `Team Nexgen` | yes |
| `FrmDD.frm` | Main form + all UI event code (load, resize, mouse, menus) | yes |
| `FrmDD.frx` | Binary form resource (window icon) | yes |
| `Module1.bas` | All game logic, INI, Win32 API declarations | yes |
| `frmOptions.frm` | Options dialog (`GetINI True` reset). **Not listed in `DD.vbp`** and its menu hook in `FrmDD.frm` is commented out — dead, unreachable code | no |
| `DD.ini` | Level table + tuning (read at startup, written if missing) | runtime data |
| `build1.gif` | City/building tile (2427 bytes), BitBlt-tiled across the playfield | runtime asset |
| `Alarm.WAV`, `Empty.WAV`, `EXPLODE.WAV`, `Ohno.wav`, `Split.WAV`, `Swoosh.wav` | Sound effects | runtime assets |
| `nexgen.ico` | Icon source | asset |
| `MissileCommand.exe` | Previously built binary (69632 bytes) — evidence the project built | artifact |
| `DD.vbw` | IDE workspace (window layout only) | no |
| `MSSCCPRJ.SCC`, `Team Nexgen Website.url`, `bkup/` | Source-control stub, URL shortcut, InstallShield installer tree + `missilecommand.rar` | no |

External dependencies (`DD.vbp`): `stdole2.tlb` (OLE Automation, benign) and
`COMDLG32.OCX` (Common Dialog). **No code references any Common Dialog control**,
so the OCX reference is unused but would still warn on project load if
unregistered. No other OCXs, no user controls, no classes, no fonts, no level
files beyond `DD.ini`.

Win32 API surface (`Module1.bas`): `GetPrivateProfileStringA` / 3 aliases,
`WritePrivateProfileStringA` / 3 aliases (all `kernel32`), `BitBlt` (`gdi32`,
used once in `ResetTargets`), `sndPlaySoundA` (`winmm.dll`, all audio).
No timers-as-controls: timing is a busy `Timer` loop (see §3).

Global state (`Module1.bas`): colors `bClr/mClr/lClr/eClr`; flags
`blnQuit/blnFirstTime/blnAllowResize`; `gstrPath`; `Level(1..10)`; `Targets()`
bool array; pools `b(1..500)` / `m(1..500)` of `pType`; INI tuning
`SyncDelay/SyncDist/SyncTime`, `l(1..10)` level strings, `SoundFX`,
`MaxTarget`, `bMaxStatus`, `mMaxStatus`, `mSpeed`, `bExplodeb`; latched mouse
`mMouseX/mMouseY/mMouseButton`.

## 2. Important routines and responsibilities

- `DoIt` — the entire game driver. `Randomize Timer`, `GetINI`,
  `MachineSpeed`, per-level setup, the tick loop, win/lose detection, level
  progression 1..10. Owns the game-state machine (implicit booleans
  `blnLost/blnWon/blnQuit`, no title screen, no pause, no score).
- `MyShow(Head, Pool, p(), Clr, MaxStat, ...)` — move + draw + explode + retire
  for one pool. Owns trail rendering, forced detonation snap, city kills,
  off-screen removal, MIRV split spawning, explosion growth, free-list return.
  Includes a `Stop` statement on cross-linked-list corruption (debug leftover).
- `LaunchB` — enemy spawn. Random top position, random live target, Smart/Split
  rolls, trig velocity (`Atn`/`Cos`/`Sin`), speed `bSpeed*(1+Rnd)`.
- `LaunchM` — defensive fire from fixed `(MaxX/2, MaxY)`, trig velocity at
  `mSpeed`, `Empty.wav` when exhausted.
- `Intercept(mHead, m(), bHead, b(), ...)` — square-radius kills of bombs inside
  defensive explosions; smart-bomb evasion (flip `xm`); optionally called as
  bomb-vs-bomb chain when `bExplodeb` is true.
- `PickTarget` / `PickRandom` / `x2t` — targeting helpers. `PickRandom` spins
  `Rnd*1000` until in range (biased, preserved quirk); `x2t` maps logical x to a
  target index.
- `BuildPool` / `Nullp` — intrusive singly-linked free list via `Link`,
  zeroed by `Nullp`.
- `ResetTargets` — tiles `build1.gif` across the PictureBox with `BitBlt`,
  sets `ScaleWidth/Height = 1000x750`.
- `MachineSpeed` / `InitLevels` — `SyncFactor=(SyncDist*SyncDelay)/SyncTime`
  scales CSV speeds from `l(1..10)` into `Level()`.
- `GetINI` / `PutINI` / `VBGetPrivateProfileString` / `Exist` — `DD.ini`
  persistence (level table + tuning). Defaults are hardcoded in `GetINI` and
  match the shipped `DD.ini`.
- `MouseRead` — consumes the latched click (`mMouseButton=0` after read).
- `Alarm` / `FireFX` — one-shot sounds. `Delay` — busy `Timer` wait (calibration
  leftover, unused by the main loop).
- `InRange` — **square** (box) proximity test, not circular. Preserved quirk.
- `FrmDD` handlers — `Form_Load` (center ~7x4" window, `Show`), `Form_Activate`
  (runs `DoIt` once), `mnuFilePlay_Click` (re-runs `DoIt`, reentrant),
  `Pic_MouseDown` (latch click), `Pic_MouseMove` with `Button=2` synthesizing a
  left click (right-drag fires — preserved quirk), `ReSize` (reset scale +
  `ResetTargets`), `Form_Unload` (sets `blnQuit`).

## 3. Gameplay / state-flow overview

Startup: `Form_Load` → `Form_Activate` → `DoIt`. No menu/title flow; Play menu
re-enters `DoIt`.

Per level L (`1..MaxLevel=10`): counters zeroed; pools built with **in-flight
caps** `bDrop`/`mFire` (not the 500-element array bounds); `ResetTargets`;
`Alarm`; then the gated loop: wait until `Sync` (`SyncDelay` gate, default
`0.05` s, with `DoEvents`), `Sync = Timer + SyncDelay`, `MouseRead`,
`LaunchB` **every tick** (one bomb per tick while quota/cap allow), `LaunchM`
on click, `MyShow` both pools, `Intercept` (plus bomb-chain if `bExplodeb`).

Win level: all bombs dropped (`bDroped = bMax`) AND no active bombs
(`bHead = 0`) with ≥1 city alive. Lose: all `Targets()` false. After level 10:
win branch is a no-op comment; lose plays `OhNo.wav`. Game-over/restart: window
stays on last frame; restart via File > Play. **No scoring, no bonus, no
high-score, no ammo dumps, no panic button** (all listed as future comments).

Timing model: **Timer-gated, frame-dependent**. `SyncFactor` calibrates logical
speed to the gate: defaults `SyncDist=500, SyncTime=1, SyncDelay=0.05` give
factor 25, so e.g. level-1 `bSpeed 0.05 → 1.25 px/tick` (20 ticks/s) and
`mSpeed 1.5 → 37.5 px/tick`. The port must NOT copy wall-clock dependence;
MC1 establishes a fixed-step equivalent (§6).

Rendering model: `PictureBox` (`AutoRedraw=True`), logical `1000x750`, origin
top-left, Y down. Warheads: `Circle r=2`; trails: red `Line` from source
(skipped for Smart bombs — they are invisible except the head, preserved
quirk); explosions: growing `Circle` radius `Status`, caps `mMaxStatus=25` /
`bMaxStatus=35` (doubled to 70 for city hits); erase by overdraw in black.

Coordinate/combat facts: battery is a **single central site** `(500,750)`
(not the classic three); cities default to **10 slots** (`MaxTarget`, INI
range 5..20, not the classic six); enemy velocity is straight-line trig from
`(x,0)` to `(tx,750)`; defensive snap forces the intended detonation point
despite sync error; bombs leaving x-range are silently retired; splits spawn a
child at `(sx,sy)` and can **grow** `Level.bMax` past the free list.

Difficulty (`DD.ini`, `bMax,mMax,bDrop,mFire,bSpeed,Smart%,Split%,Name`):
L1 `10,50,5,5,0.05,0,15,"Slow and Dumb I"` … L10
`100,200,50,50,0.15,90,50,"This ain't right"`. Full table in `DD.ini`/§2.

Input: left click fires at cursor (latched, consumed per tick); right-button
motion also fires (§2 quirk). No keyboard input at all in the original.

Audio: `Alarm` (level start), `Swoosh` (fire), `Explode` (defensive burst),
`Thunder` (bomb burst — **file missing from repo**, plays silent),
`Split`, `Empty` (fire with no missile), `OhNo` (loss), `Error` (quit —
**file missing**). All gated by `SoundFX`.

Random: `Randomize Timer` once; `Rnd` in `LaunchB` speed/spread, Smart/Split
rolls, `PickRandom`/`PickTarget`.

## 4. Assets / resources inventory

`build1.gif` (city tile), 6 WAVs (see §1; `Thunder.wav` + `Error.wav`
referenced but absent), `nexgen.ico`, `FrmDD.frx` icon. guideXOS cannot consume
`.gif`/`.wav`/VB6 `.frx` directly: MC2 will convert `build1.gif` to the
reproducible `GXIM` (`GXIM` header + XRGB8888, same approach as the Pac-Man
port's `convert_bmp_to_gximg.ps1`) and re-author/stub audio (no audio host call
exists yet — §7). Originals stay untouched; conversions are generated artifacts.

## 5. VB6 buildability (time-boxed verdict)

Project appears **complete and probably faithfully buildable** in a legacy VB6
IDE: all referenced source files exist, a prior `MissileCommand.exe` is
present, no third-party controls beyond the (unused) `COMDLG32.OCX` reference.
Known defects: unused OCX reference (load warning if unregistered),
`frmOptions.frm` orphaned, `Thunder.wav`/`Error.wav` missing (silent at
runtime). No VB6 resurrection is attempted in MC1; the existing `.exe` plus
code trace is sufficient reference.

## 6. guideXOS APIs selected for the port

Authoritative current model (see `docs/APP_MODEL_CURRENT_STATE.md`,
`docs/GXAPP_FORMAT_SPEC.md`, `sdk/README.md`):

- Package: `Apps/<Name>/app.json` (`NativeElf`, `gx_main`, `guidexos-c-abi-v1`,
  per-arch `bin/amd64/*.elf`) discovered by `AppRegistry`; launch via
  `DesktopService::LaunchApp` → `NativeElfLaunchPipeline` (execution is
  experimental: normal `build.bat` stops at the executor gate;
  `build-native-experimental.bat` runs trusted ELFs locally).
- ABI (`sdk/include/guidexos/`, append-only): `request_window_ex`
  (`FIXED_SIZE|CENTERED`), `poll_event` (events `WINDOW_CLOSE/FOCUS/BLUR/KEY/
  MOUSE/WINDOW_PAINT`; mouse packed button/action, `param1/2` = x/y; key
  `param1` = code, `param2` = up/down; `ESC=27`, arrows 37–40), `draw_text`,
  `present_frame` (retained XRGB8888 surface, host copies, 16 MiB cap),
  `get_ticks_ms` (monotonic ms), `log`, `exit`. **No audio call exists.**
- Canonical game pattern (Pac-Man sibling port `D:\dev\pacman\guidexos`, staged
  to `Apps/PacMan`): pure deterministic logic (`game.*`) unit-tested apart from
  rendering; `renderer.*` builds an XRGB8888 frame; `main.cpp` owns
  resource load → `request_window_ex` → initial present → loop
  `poll_event(10ms)` + fixed-step accumulator (`10 ms` step, `250 ms` clamp,
  `8` catch-up max, `~16 ms` visual throttle, `visualDirty` flag, focus
  handling, `exit`). SDK samples (`sdk/samples/helloworld`) show the minimal
  manifest/build/stage shape this skeleton follows.
- Build: `clang++ --target=x86_64-unknown-elf -ffreestanding -fno-exceptions
  -fno-rtti -fno-stack-protector -mno-red-zone` + `ld.lld -static -e gx_main`
  (MinGW g++ emits PE/COFF and cannot be used for the ELF).

VB6 → guideXOS mapping (from real findings, not analogy):

| VB6 | guideXOS |
|---|---|
| `FrmDD` form + `Pic` PictureBox | `request_window_ex` window + retained `present_frame` surface |
| `Circle`/`Line`/`BitBlt` drawing | CPU framebuffer fills in `renderer` + `present_frame`; host `draw_text` for MC1 status/title overlay |
| `SyncDelay` Timer gate + `DoEvents` | `poll_event(10ms)` + `get_ticks_ms` fixed-step accumulator (deterministic; wall-clock only drives step count) |
| `Pic_MouseDown/Move` latch | `GX_EVENT_MOUSE` (left-down recorded; right-drag quirk deferred to MC2) |
| (no keyboard) | `GX_EVENT_KEY`: `ESC` closes; arrows reserved for MC2 aiming |
| `sndPlaySound` | **no equivalent** — silent in MC1, gap logged (§7) |
| `DD.ini` + profile APIs | future: packaged resource read via `file_read_all`; MC1 uses compiled defaults |
| `ScaleWidth/Height=1000x750` | logical VB units kept in game state; scale `0.48` → `480x360` window frame |
| `DoIt`/`MyShow`/`LaunchB`/`LaunchM`/`Intercept` | `mc_*` state + update functions (`sdk/samples/missilecommand/`), rendering kept separate |

## 7. Architecture chosen (MC1 skeleton)

```
sdk/samples/missilecommand/
  missilecommand_state.h    pure logic, no guideXOS includes (host-testable)
  main.cpp                  gx_main: window, events, fixed-step, present
  app.json                  source manifest (com.guidexos.samples.missilecommand)
  CMakeLists.txt / README.md
Apps/MissileCommand/
  app.json                  staged manifest (com.guidexos.missilecommand)
  bin/amd64/missilecommand.elf   staged Native ELF (reproducible build)
tests/missilecommand_state_test.cpp  g++ host test of the pure logic
scripts/run-missilecommand-state-test.ps1
```

`MissileCommandApp` (in `main.cpp`) owns the window + frame buffers;
`McState` (in `missilecommand_state.h`) owns deterministic session state
(`simulationSteps`, sweep diagnostic, click record, running flag); the renderer
section of `main.cpp` owns VB→window mapping and the recognizable placeholder
scene (dark playfield, ground band, 10 VB-faithful city slots, central battery,
title/status text). Fixed-step `10 ms` mirrors the Pac-Man precedent; the
visible sweep dot is a **diagnostic** proving `update → state → invalidate →
render` and is flagged for removal when real missiles land in MC2. Bounded
static storage only; no dynamic allocation; no new framework.

Known incompatibilities/gaps: no audio host call (game is silent); no high
resolution text shaping (`draw_text` only); bare-metal kernel registry is a
separate dispatch path — MC1 targets the hosted manifest path like other
Native ELF apps; `Thunder.wav`/`Error.wav` absent upstream so MC2 audio is
stubbed regardless; `present_frame` 16 MiB cap is ample (480x360x4 ≈ 0.66 MiB).

## 8. Suggested implementation sequence (post-MC1)

1. MC2 playable loop (§9). 2. `build1.gif` → `GXIM` conversion tooling +
   staged resource. 3. `DD.ini` table as packaged resource via `file_read_all`.
   4. Audio stubs behind a narrow host-call proposal (reusable, not game-owned).
   5. High-score persistence proposal (workspace calls are hosted-only today).

## 9. Proposed MC2 scope — First Playable Missile Defense Loop

Single level (L1 table values compiled in): fixed-step enemy spawn
(`LaunchB` trig + `bSpeed*(1+Rnd)` with a seeded, test-visible RNG),
player fire from the central battery (`LaunchM`), forced-snap detonation,
square-radius `Intercept` kills, city loss on ground impact, win = quota
dropped + pool drained, lose = all cities dead. Keep Smart-evasion and MIRV
split behind the L1-zero defaults initially, then enable per `DD.ini` rows.
Tests: deterministic spawn/kill/city-loss/win/lose vectors in the host test.
Out of MC2: audio, menus, persistence, multi-wave polish, visual parity.
