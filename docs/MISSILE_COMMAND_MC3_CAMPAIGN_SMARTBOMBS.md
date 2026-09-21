# Missile Command MC3 — L1–L3 Campaign, Smart Bombs, DD.ini, Pointer Fix

Status: MC3 complete. The MC2 single-level loop is now the opening level of
the genuine original campaign. Read alongside
`docs/MISSILE_COMMAND_MC1_ARCHITECTURE.md` (foundation, App Model packaging,
ABI selection, asset inventory) and
`docs/MISSILE_COMMAND_MC2_DEFENSE_LOOP.md` (L1 loop mechanics; §7 differences
1–2 and 4–6 still apply, difference 3 is superseded below).

Behavioral specification: re-traced from the VB6 project at
`D:\dev\bkup\inactive\missilecommand` (branch `master`, read-only), in
particular `Module1.bas` (`DoIt`, `MyShow`, `LaunchB`, `LaunchM`,
`Intercept`, `BuildPool`, `Nullp`, `ResetTargets`, `PickRandom`,
`PickTarget`, `x2t`, `InRange`, `MouseRead`, `MachineSpeed`, `InitLevels`,
`GetINI`), `FrmDD.frm` (`Pic_MouseDown`, `Pic_MouseMove`, `Form_Activate`),
and `DD.ini`. Field meanings below come from source usage, not names.

---

## 1. Recovered L1–L3 configuration

`DD.ini` rows are `bMax, mMax, bDrop, mFire, bSpeed, Smart%, Split%, Name`:

| Raw field | L1 | L2 | L3 |
|---|---|---|---|
| `bMax` (bomb quota) | 10 | 10 | 10 |
| `mMax` (missile quota) | 50 | 50 | 50 |
| `bDrop` (hostile in-flight pool) | 5 | 5 | 5 |
| `mFire` (defensive in-flight pool) | 5 | 10 | 10 |
| `bSpeed` (raw) | 0.05 | 0.05 | 0.07 |
| `Smart%` | 0 | 0 | 30 |
| `Split%` | 15 | 20 | 25 |
| `Name` | Slow and Dumb I | Slow and Dumb II | Faster and Smarter I |

Globals (all levels): `mSpeed=1.5`, `mRadius(mMaxStatus)=25`,
`bRadius(bMaxStatus)=35`, `Cities(MaxTarget)=10`, `bExplodeb=True`,
`Sound=True` (ignored; no audio ABI), `SyncDist=500`, `SyncDelay=0.05`,
`SyncTime=1`.

`SyncFactor = (SyncDist*SyncDelay)/SyncTime = 25` (`MachineSpeed`).
`InitLevels` scales `bSpeed` and `mSpeed` by it, so runtime px/tick:

| Level | hostile px/tick | defensive px/tick |
|---|---|---|
| L1 | 1.25 | 37.5 |
| L2 | 1.25 | 37.5 |
| L3 | 1.75 | 37.5 |

Hostile velocity is `bSpeed*(1+Rnd)` along the unit vector to the target
(`LaunchB`); defensive velocity is exactly `mSpeed` toward `(xe, ye)`
(`LaunchM`, battery `(500,750)`).

### Field semantics (traced)

- `bMax`: normal hostile spawns allowed while `bDropped < bMax`. MIRV
  children do **not** consume it (see §6 quirk). Pool-growth at split time
  (free list exhausted) raises `Level.bMax` by 1 (capped at pool bound 500).
- `mMax`: defensive launches allowed while `mFired < mMax`. MIRV children
  **do** consume it (quirk), so heavy splitting drains defensive ammo.
- `bDrop`/`mFire`: `BuildPool` free-list lengths (in-flight concurrency).
- `bSpeed`: per-level hostile base speed (scaled). `mSpeed` is global.
- `Smart`: per-spawn flag probability, including split children (fresh
  `PickRandom(1,100) <= Smart` roll each spawn; `Smart=0` disables).
- `Split`: per-spawn MIRV probability with `SplitY` uniform in
  `[y, 600]` when `y < 600` at spawn (children roll again, so chains occur).
- `Name`: caption only.
- `mMaxStatus`/`bMaxStatus`: blast growth caps; city-hit bomb blasts burn
  `2x`. `bExplodeb`: hostile-vs-hostile chain enabled.

Counter vs probability vs pool-size: `bMax`/`mMax` are quota counters,
`bDrop`/`mFire` are pool sizes, `Smart`/`Split` are probabilities.

---

## 2. DD.ini mapping and runtime loading

`sdk/samples/missilecommand/resources/DD.ini` is the byte-identical original
file, staged to `Apps/MissileCommand/resources/DD.ini` by
`sdk/build-samples.ps1` (`Resources = @('resources/DD.ini')`) and read at
runtime with the existing `file_read_all(ctx, "resources/DD.ini", …)`
resource API (no Win32 INI APIs, no general INI framework; the
`file.read` permission was added to both manifests).

Parsing (`mc_parse_dd_ini`, freestanding, bounded, no libc): starts from the
verified fallback (the `GetINI` defaults, byte-equal to the shipped file for
L1–L10 and globals), then overrides per line. Level rows need all 8 fields
with `bMax/mMax/bDrop/mFire` in 1..500, `bSpeed` in (0, 5], `Smart/Split` in
0..100; globals follow `GetINI` bounds (`Cities` 5..20, radii 5..50,
`mSpeed` 0.1..5, positive sync terms, `True/False/1/0` chain flag).
Any missing/malformed/out-of-range row or scalar keeps its fallback value;
the app never crashes on a bad file. At least L1–L3 are parsed (L1–L10 rows
are all accepted; the campaign plays L1–L3).

Fallback: compiled verified values (identical to the table above plus the
L4–L10 `GetINI` defaults for future use). The app logs
`MissileCommand DD.ini runtime loaded` or
`MissileCommand DD.ini fallback selected`, and the overlay shows `ini=run`
vs `ini=fb`.

---

## 3. Progression rules (VB `DoIt` For-loop)

- Completing L1 enters L2; completing L2 enters L3 (automatic after a
  deterministic 40-tick / 2 s `LEVEL COMPLETE` dwell counted in fixed-step
  ticks, never wall clock).
- Each level applies its own data (`mc_apply_level`).
- Old projectiles never leak: pools are rebuilt (`BuildPool` with the new
  `bDrop`/`mFire`), counters zeroed, latch cleared, explosions gone.
- Restart (R/Enter/Space after GAME OVER or GAME COMPLETE) returns to L1
  with cities revived, quotas/pools fresh, outcome cleared.
- After L3, temporary `GAME COMPLETE` terminal state (L4–L10 deferred).
- Level-complete evaluation is exactly VB: lose when no city alive
  (precedence over win); else win when `bDropped >= bMax` AND hostile pool
  drained. Terminal states freeze the sim; fire is refused during the dwell
  and after terminals, so input cannot double-transition.
- RNG is seeded once (`Randomize Timer` equivalent) and never reseeded at
  level boundaries. File-load timing never affects the sim (config is fully
  loaded before `mc_init_campaign_with_seed`).
- Transition display: `LEVEL COMPLETE - advancing...` dwell, then the next
  level caption (`MC3 campaign Ln/3 <Name>`); `GAME COMPLETE` / `GAME OVER`
  terminals with restart hint.

### City persistence

Destroyed cities **remain destroyed** across levels. VB `ResetTargets`
redraws from `Targets()` but never assigns it; only the pre-loop init (and
our full restart) revives cities. Tests kill a city on L1 and assert it is
still dead on L2/L3.

---

## 4. Smart-bomb spawning and evasion

Spawning (`LaunchB`): every spawn (normal and split-child) rolls
`PickRandom(1,100) <= Level.Smart` when `Smart != 0`, else dumb. Smart status
does **not** inherit — children re-roll, so smart parents can yield dumb
children and vice versa. L1/L2 (`Smart=0`) spawn none; L3 (`Smart=30`)
spawns naturals (first observed at tick 4 with seed 1; host suite asserts
natural L3 spawns).

Rendering: smart bombs show the warhead head only, never the red trail
(erase and draw both skip the `Line` for `Smart`, like `MyShow`).

Evasion (`Intercept`, exact port): for each exploding defensive burst, every
in-flight hostile inside the square kill box (`Status` radius) dies
(`Status=2`); otherwise, if it is smart and inside the wide square box
(`mMaxStatus*2`) and above the explosion (`b.Y < m.Y`), its `xm` flips away
from the burst (`xm>0` and `b.x<m.x` → negate; `xm<=0` and `b.x>m.x` →
negate). Bounds/repeat behavior: no position clamp; the flip rule is stable
(already-moving-away bombs hold course), so repeated ticks do not oscillate.
Chain-explosion interaction: the `bExplodeb` bomb-chain runs the same
routine hostile-vs-hostile, so smart bombs evade bomb blasts too. Square
interception rules are unchanged (corner `(r,r)` dies).

---

## 5. Split/MIRV behavior (with original quirk preserved)

Split condition: in-flight, on-screen, `SplitY != 0`, `y >= SplitY`.
Child creation: `LaunchB(..., Split=True, sx=x, sy=y)` — spawns at the split
point with a fresh random live target, fresh smart roll, fresh split roll,
and `bSpeed*(1+Rnd)` velocity. Parent retires its `SplitY` (fires once) and
continues flying.

**Original quota quirk (verified, preserved):** `DoIt` calls the bomb-pool
`MyShow` with `mFired%` as the `Droped%` argument, so the child increments
the **missile** counter. The port transfers the charge
(`bDropped` restored, `mFired += 1`); `bDropped` counts normal parents only.
Consequences: the win check (`bDropped >= bMax` + pool drained) ignores
children, while heavy MIRV activity visibly drains defensive ammo.

Pool mutation: if the free list is exhausted at split time and
`bMax < 500`, `bMax += 1` and the new slot becomes the free head before the
child consumes it (VB pool-growth, capped at 500). At saturation (500) the
child is refused cleanly. Interaction with smart bombs: orthogonal (fresh
rolls); pool exhaustion handling never hangs.

---

## 6. RNG lifecycle

Seeded LCG (`kMcDefaultSeed`; `mc_init_with_seed` / `mc_init_campaign_with_seed`
for tests). `Randomize Timer` runs once per campaign; level transitions and
restarts never reseed (matches VB per-`DoIt`, not per-level). Same
seed + DD.ini + inputs + tick sequence ⇒ identical canonical state
(multi-level fingerprint test: seed 15 plays L1→L2→L3 to GAME COMPLETE,
fingerprint `10209848825990380524`, steps 2651, both runs identical).

---

## 7. Pointer-button compositor defect and fix

Two related defects, one narrow fix each, no game logic in the compositor:

1. Moves dropped held buttons: the hosted compositor published every
   pointer-move with button 0 — `compositor.cpp` `WM_MOUSEMOVE` ignored
   `wParam` (`MK_*`), and the synthetic `MT_InputMouse` path only forwarded
   `0/move` while dropping `2/move` (right-drag) entirely; no
   `WM_RBUTTONUP` handler existed, so right-up never cleared anything. The
   game already implemented VB right-drag
   (`GX_MOUSE_BUTTON_RIGHT` + `GX_MOUSE_ACTION_MOVE`), but the event never
   arrived; left-click worked (DOWN path was correct).
2. Targeted keys lost their window: `gui.keyto` delivers
   `windowId|keyCode|action|modifiers`, but the compositor forwarded only
   `keyCode|action|modifiers` to the runtime, whose parser reads the window
   as the TRAILING field (same convention as mouse payloads:
   `x|y|button|action|modifiers|window`). Every targeted key therefore
   degraded to focus attribution and was dropped by apps whose window was
   not focused — mouse input (explicit window) worked while keys sent the
   same way silently died. Found live: A/F hook keys never took effect
   while R/Escape sent later (after clicks had set focus) worked.

Fix (`compositor.cpp` + new testable
`compositor_pointer_buttons.h`):
pointer-down sets the held bit, pointer-up clears it, pointer-move exposes
the held button(s) — left stays correct, right works, middle behaves.
Multiple held buttons publish one move per button (left/right/middle order;
the ABI carries a single button per event). No-buttons moves still report 0,
so existing apps are unchanged. New `WM_RBUTTONUP` handler forwards
right-up. Cleanup: held state clears on capture loss,
window close/minimize, and focus change, so no stuck drag survives.
Targeted `MT_InputKey` now appends the validated `|windowId`, which the
runtime parser already accepts; untargeted keys are byte-identical to
before.

---

## 8. Exact current control scheme

- Left-button DOWN fires at the pointer (retained guideXOS convenience;
  VB6-compatible: `MouseRead` fires on button 1, so this is faithful).
- Right-button DOWN alone does nothing (VB-faithful).
- Right-button MOVE (drag with right held) fires (restored original).
- R / Enter / Space restarts a finished campaign (replaces File > Play).
- Escape / close exits cleanly.
- Test-only hooks (off by default): `A` autopilot defense, `F`
  fast-forward. They never alter normal play unless keyed.

Live-loop notes (all in `main.cpp`, game semantics untouched): the frame
drains up to 16 queued events per iteration (FIFO preserved) so input
cannot lag minutes behind frame traffic, and paints only flag the scene
dirty for the single throttled present; autopilot decides once per
fixed-step tick (never per frame) so assisted live runs reproduce host
runs tick-for-tick; fast-forward bursts apply to L1/L2 while L3 plays at
wall rate for observability. Evasion is counted in-state (`evadeCount`)
and logged live as `smart bomb evaded`.

Difference from VB6 if left-click remains: none semantically — VB6 fired on
left DOWN too; left-click is the primary VB control, right-drag the
alternate. Both now work live.

---

## 9. Remaining behavioral differences from VB6 (deliberate)

1. RNG: LCG vs `Rnd`/`Randomize Timer` (determinism required).
2. Trig → normalization: identical geometry, no libm; FP bits differ.
3. Fixed 10 city slots: `Cities` parses 5..20 but gameplay uses 10
   (L1–L3 use 10, so no campaign effect).
4. No audio (no host call), city art is rectangles (`build1.gif` deferred).
5. `GAME COMPLETE` after L3 is temporary (L4–L10 deferred to MC4).
6. 50 ms fixed step replaces the `SyncDelay` busy gate; restart key replaces
   File > Play; autopilot/fast-forward hooks are test-only additions.

---

## 10. Validation

- `tests/missilecommand_state_test.cpp` via
  `scripts/run-missilecommand-state-test.ps1`: PASS (all MC2 vectors updated
  for the split quirk + DD.ini ×8, progression ×~20, smart ×~12, splits,
  input matrix, 500-tick + full-campaign fingerprints, null safety).
- `tests/compositor_pointer_buttons_test.cpp` via
  `scripts/run-compositor-pointer-buttons-test.ps1`: PASS (11 platform cases
  + 8 translation cases + latch release).
- `sdk/build-samples.ps1`: all three samples Success.
- `scripts/smoke-missilecommand-mc3.ps1`: 18/18 PASS (registration, launch,
  DD.ini runtime, window, spawn, launch, detonation, hooks, live right-drag,
  L2 start, natural smart spawn, smart evasion, combat, L3 start, Escape,
  clean exit). The qualifying run played L1 → L2 → L3 live with natural L3
  smart bombs (first at L3+1, as the host reference predicts) and evasion
  under autopilot defense, then GAME OVER at L3 — the same outcome as the
  host reference run on the default seed — followed by restart, live
  right-drag routing, Escape, and clean exit.
- `scripts/run-native-abi-layout-test.ps1`: unchanged pre-existing failure
  (`sizeof(gx_host_calls) == 120` vs 240) — unrelated, untouched.

## 11. Recommended MC4 scope

L4–L10 campaign data (already parsed/staged) with balance validation,
`build1.gif` → GXIM city art, audio host-call proposal, high scores /
persistence proposal. No engine, resource-system, or compositor redesign.
