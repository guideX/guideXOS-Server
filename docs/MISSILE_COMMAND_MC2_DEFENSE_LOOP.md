# Missile Command MC2 — First Playable Missile Defense Loop

Status: MC2 complete. The MC1 skeleton is now a genuinely playable L1-style
defense loop derived from the VB6 original. Read alongside
`docs/MISSILE_COMMAND_MC1_ARCHITECTURE.md` (foundation, still accurate for
the App Model packaging, ABI selection, and asset inventory).

Behavioral specification: the VB6 project at
`D:\dev\bkup\inactive\missilecommand` (branch `master`, read-only), in
particular `Module1.bas` (`DoIt`, `MyShow`, `LaunchB`, `LaunchM`,
`Intercept`, `BuildPool`, `Nullp`, `ResetTargets`, `PickRandom`,
`PickTarget`, `x2t`, `InRange`, `MouseRead`, `MachineSpeed`, `InitLevels`,
`GetINI`), `FrmDD.frm` (`Pic_MouseDown`, `Pic_MouseMove`, `Form_Activate`),
and `DD.ini`. Every routine below was re-traced from source for MC2; prose
summaries were not trusted where exact behavior mattered.

---

## 1. L1 configuration recovered (verified in `DD.ini` + `GetINI` defaults)

`DD.ini l1 = 10, 50, 5, 5, 0.05, 0, 15, "Slow and Dumb I"`, i.e.
`bMax, mMax, bDrop, mFire, bSpeed, Smart%, Split%, Name`:

| Field | L1 value | Native constant |
|---|---|---|
| bomb quota `bMax` | 10 | `kMcL1BombQuota` |
| missile quota `mMax` | 50 | `kMcL1MissileQuota` |
| hostile in-flight cap `bDrop` | 5 | `kMcL1BombInflightCap` |
| defensive in-flight cap `mFire` | 5 | `kMcL1MissileInflightCap` |
| hostile speed (raw) | 0.05 | scaled `kMcL1BombSpeed = 1.25 px/tick` |
| defensive speed (raw `mSpeed`) | 1.5 | scaled `kMcL1MissileSpeed = 37.5 px/tick` |
| smart % | 0 | `kMcL1SmartPercent` |
| split % | 15 | `kMcL1SplitPercent` |
| defense blast `mRadius` | 25 | `kMcL1DefenseBlastMax` |
| bomb blast `bRadius` | 35 | `kMcL1BombBlastMax` |
| cities `MaxTarget` | 10 | `kMcL1CityCount` |
| bomb chain `bExplodeb` | True | `kMcL1BombChain` |

Scaling: `SyncFactor = (SyncDist*SyncDelay)/SyncTime = (500*0.05)/1 = 25`
(`MachineSpeed`); `InitLevels` multiplies `bSpeed` and `mSpeed` by it.
The port compiles the scaled values, so one fixed-step tick equals one
original Sync-gated tick (20 ticks/s) with identical px/tick motion.

Because L1 `Split = 15`, MIRV splits are genuinely enabled at L1 and the
minimum faithful split behavior is implemented (spawn at the split point,
fresh random target, pool-growth). Because L1 `Smart = 0`, no smart bombs
spawn, but the evasion code path is implemented and unit-tested for later
levels. `bExplodeb = True`, so the hostile bomb-chain runs.

## 2. Architecture changes since MC1

```
sdk/samples/missilecommand/
  missilecommand_state.h    full deterministic game (was: sweep diagnostic)
  main.cpp                  combat rendering + firing input (was: placeholder)
  app.json                  "Missile Command (MC2)", version 0.2.0
tests/missilecommand_state_test.cpp   MC2 gameplay vectors (rewritten)
scripts/smoke-missilecommand-mc2.ps1  live combat smoke (new; MC1 kept)
```

`McState` (header-only, freestanding-safe: `<stdint.h>` only, no libc/libm)
owns the whole session: seeded LCG, `McLevelRuntime` (mutable L1 copy —
`bMax` grows through split pool-growth exactly like VB `Level().bMax`),
two 1-based intrusive pools `b[501]`/`m[501]` with `bHead/bPool`,
`mHead/mPool` (the VB `Head%`/`Pool%` idiom, `UBound` 500 preserved),
`Targets()[1..10]`, quota counters `bDropped`/`mFired`, the pointer latch
(`pendingFire`, `fireX/Y`), and `won`/`lost`/`running`. `main.cpp` keeps a
static `g_state` (~100 KiB BSS, off the freestanding stack) and owns only
window/events/framebuffer/overlay/logging. `kMcFixedStepMs` is now 50 ms so
wall pacing matches the VB 20 Hz tick; accumulator/clamp/catch-up/visual
throttle infrastructure is unchanged from MC1.

## 3. Behavior mapping (actual implementation names)

| VB6 | Native MC2 |
|---|---|
| `DoIt` loop body | `mc_fixed_update` (LaunchB → latch LaunchM → `mc_myshow_hostiles` → `mc_myshow_defense` → `mc_intercept_pass` → `mc_evaluate_outcome`) |
| `MyShow` (bombs) | `mc_myshow_hostiles` (+ `mc_myshow_remove` = `RemoveIt` splice) |
| `MyShow` (defense) | `mc_myshow_defense` (forced snap preserved) |
| `LaunchB` | `mc_launch_b` (+ `mc_pick_target`, `mc_pick_random`) |
| `LaunchM` | `mc_launch_m` (battery `(500,750)`) |
| `Intercept` | `mc_intercept_list`; `mc_intercept_pass` runs defense-vs-hostile then the `bExplodeb` bomb-chain, like `DoIt` |
| `InRange` | `mc_in_range` (square box, preserved) |
| `x2t` | `mc_x_to_target` |
| `BuildPool`/`Nullp` | `mc_build_pool`/`mc_null_proj` |
| `ResetTargets` (state) | `mc_reset_targets` |
| `MouseRead` | latch consumed in `mc_fixed_update` |
| `Form_Activate`/`mnuFilePlay` replay | `mc_reset_level` via R/Enter/Space after win/lose |
| `Pic_MouseDown`/`Pic_MouseMove` | `mc_request_fire` + right-drag routing in `main.cpp` |

## 4. Spawn, trajectory, detonation, collision, cities, win/lose

- **Spawn:** `mc_launch_b` attempted once per tick while `bPool != 0` and
  quota remains (`bDropped < bMax`), refused cleanly otherwise without
  consuming quota. Random top position + random live-target selection
  (`PickTarget` loop); refused safely when no city is alive (the original
  would spin until `DoIt` exits — same outcome, no hang).
- **Trajectories:** straight-line unit vectors toward the target scaled by
  `bSpeed*(1+Rnd)` / `mSpeed`, with the explicit straight vertical
  branches kept. The VB `Atn`/`Cos`/`Sin` chain is algebraically identical
  to normalization, so the port normalizes (freestanding ELF links no
  libm; deterministic `mc_sqrt` included). Directions match; raw FP bits
  are not claimed bit-identical.
- **Detonation:** defensive shells snap exactly onto `(xe, ye)` once
  `y <= ye`, then explode (`status = 2`); blasts grow one step per tick to
  `mMaxStatus` (25) / `bMaxStatus` (35), doubled to 70 for city hits.
- **Collision:** square `|dx| <= r && |dy| <= r`, inclusive — proven by a
  corner test `(r, r)` that a Euclidean test would spare. Kills set
  `status = 2` without touching links, so iteration stays valid; retirement
  happens in the update pass. Smart evasion (wide `mMaxStatus*2` box, flip
  `xm` when above) and the bomb chain are implemented.
- **Cities:** 10 slots alive at start; ground impact kills
  `Targets(x2t(impact x))` idempotently; alive cities render yellow,
  dead ones gray rubble; survivor count drives status + lose.
- **Win:** quota exhausted (`bDropped >= bMax`) AND hostile pool drained
  (`bHead == 0`) with ≥1 city alive. **Lose:** all cities dead, with
  precedence over a stale win. Terminal states freeze the sim; post-game
  fire is refused; R/Enter/Space resets the level (quotas/pools/cities/
  outcome; RNG stream continues so replays vary, like `Randomize Timer`).

## 5. Logical coordinate mapping

Gameplay is VB floats in 1000×750. Window 480×360, scale 48/100:
`mc_vb_to_window_x/y` (integer, MC1-compatible), `mc_vb_to_window_f`
(rounded, for rendering), `mc_window_to_vb_x/y` (clamped inverse for
pointer input; 240 → 500 exactly, round-trip within ~3 units). Tests cover
edges, center, inverse, and round-trip. No window-space constants leak
into gameplay.

## 6. Determinism and pools

Seeded LCG (`kMcDefaultSeed`; `mc_init_with_seed` for tests); no
wall-clock inside gameplay; one tick == one VB tick. Same seed + inputs +
tick count ⇒ identical canonical state (field-wise fingerprint test).
Pools are bounded (500, the VB `UBound`), reuse slots deterministically,
refuse cleanly when exhausted, and convert the VB `Stop` on cross-linked
corruption into a counted guard trip + clean break (`listGuardTrips`).

## 7. Known differences from VB6 (deliberate, all documented)

1. **RNG:** LCG vs VB `Rnd`/`Randomize Timer`. Distribution differs;
   determinism is the requirement and it holds.
2. **Trig → normalization:** identical geometry, no libm; FP bits differ.
3. **Split quota accounting:** `DoIt` passes `mFired%` (not `bDroped%`) as
   the `Droped%` argument to the bomb-pool `MyShow`, so in the original a
   MIRV child increments the *missile* counter (apparent argument swap).
   The port charges the child to the bomb quota (intended semantics).
4. **Right-drag delivery:** the app implements VB right-drag fire per the
   ABI (`GX_MOUSE_BUTTON_RIGHT` + `GX_MOUSE_ACTION_MOVE`), but the hosted
   compositor only forwards moves as button 0 (`compositor.cpp:6910-6926`,
   no button-2 move branch), so right-drag cannot reach the app at runtime.
   Left-click (the primary VB fire) works live; right `DOWN` alone is
   correctly ignored, matching `MouseRead`. Fix belongs in the compositor,
   not the game.
5. **Float positions:** spawn/target x are integers (VB-faithful); flight
   positions are full floats (VB `pType` Singles already were).
6. **No audio** (no host call exists), **no `DD.ini` file IO yet** (L1
   compiled in with provenance comments), **single level only**, city art
   is rectangles (`build1.gif` conversion deferred), **R restart** replaces
   File > Play, **50 ms fixed step** replaces the `SyncDelay` busy gate.

## 8. Validation

- Host suite `tests/missilecommand_state_test.cpp` via
  `scripts/run-missilecommand-state-test.ps1`: **PASS** (init, spawn
  determinism/quota/validity, exhaustion, launch/trajectory/snap, latch,
  square intercept incl. corner proof, retirement integrity, smart
  evasion, bomb chain on/off, MIRV split + pool-growth, city impact incl.
  double blast + idempotence, win ×3 + live 958-tick auto-aim win,
  lose + precedence + freeze + restart, input, coordinates, 500-tick
  determinism fingerprint, null safety).
- `sdk/build-samples.ps1`: all three samples **Success**.
- `scripts/smoke-missilecommand-mc2.ps1`: **12/12 PASS** — registration,
  launch, window, hostile spawn, defensive launch, detonation, intercept +
  city loss, live **LEVEL COMPLETE**, Escape, clean exit.
- `scripts/run-native-abi-layout-test.ps1`: fails identically to MC1
  (`sizeof(gx_host_calls) == 120` vs 240, `native_abi_layout_test.cpp:14`)
  — pre-existing, unrelated, untouched.

## 9. Remaining gaps → recommended MC3 scope

Smart-bomb spawning/levels (L2+: `Smart%` 30–90), full MIRV chains,
`DD.ini` as a packaged resource via `file_read_all`, L1–L10 progression
with per-level setup, `build1.gif` → GXIM city art, audio host-call
proposal (game stays silent until one exists), compositor right-button
move forwarding (to unlock VB-exact right-drag), high scores/persistence
proposal. MC3 should start with L2–L3 smart/split behavior + level
progression, since the L1 baseline, pools, and tests already carry the
mechanisms.
