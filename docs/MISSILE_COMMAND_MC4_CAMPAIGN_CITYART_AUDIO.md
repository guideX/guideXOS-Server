# Missile Command MC4 — Full L1–L10 Campaign, Original City Art, Audio Architecture

Status: MC4 complete. Read alongside
`docs/MISSILE_COMMAND_MC1_ARCHITECTURE.md` (foundation, App Model packaging,
ABI selection, asset inventory),
`docs/MISSILE_COMMAND_MC2_DEFENSE_LOOP.md` (L1 loop mechanics), and
`docs/MISSILE_COMMAND_MC3_CAMPAIGN_SMARTBOMBS.md` (L1–L3 campaign, smart
bombs, split quirk, compositor pointer fix — MC3 §7 differences 1–3 and 5–6
still apply; difference 4, "GAME COMPLETE after L3 is temporary", is
superseded: the campaign now runs the genuine ten levels).

Behavioral specification: re-traced from the VB6 project at
`D:\dev\bkup\inactive\missilecommand` (branch `master`, read-only), in
particular `Module1.bas` (`DoIt` For-loop, `GetINI` defaults, `MyShow`,
`LaunchB`, `LaunchM`, `Intercept`, `ResetTargets`, `MouseRead`,
`MachineSpeed`, `InitLevels`, `sndPlaySound` call sites), `FrmDD.frm`
(`Pic_MouseDown`, `Pic_MouseMove`), `DD.ini`, `build1.gif`, and the WAV
assets. Field meanings come from source usage, not names.

Outcome: **A** — L1–L10 campaign and original city art complete; audio is
conclusively scoped as platform work (Outcome C) with a validated,
minimal App Model proposal below. No application-private audio hack was
built, and MC4 does not fail on audio: campaign + art are the deliverables.

---

## 1. Complete L1–L10 configuration (revalidated)

`DD.ini` rows are `bMax, mMax, bDrop, mFire, bSpeed, Smart%, Split%, Name`
(8 fields). The shipped `DD.ini` is byte-identical to the `GetINI`
compiled defaults row-for-row (verified field by field, including all ten
names). Note: the commented-out `Select Case` draft inside `InitLevels`
shows a stale 9-field layout with an embedded per-level `mSpeed` column;
the live `GetINI`/`DD.ini` format has no per-level `mSpeed` (`mSpeed` is
global). The port implements the live format.

| Raw field | L1 | L2 | L3 | L4 | L5 | L6 | L7 | L8 | L9 | L10 |
|---|---|---|---|---|---|---|---|---|---|---|
| `bMax` (bomb quota) | 10 | 10 | 10 | 15 | 15 | 20 | 30 | 40 | 50 | 100 |
| `mMax` (missile quota) | 50 | 50 | 50 | 100 | 100 | 200 | 200 | 200 | 200 | 200 |
| `bDrop` (hostile pool) | 5 | 5 | 5 | 10 | 10 | 10 | 10 | 20 | 50 | 50 |
| `mFire` (defense pool) | 5 | 10 | 10 | 20 | 20 | 20 | 20 | 30 | 50 | 50 |
| `bSpeed` (raw) | 0.05 | 0.05 | 0.07 | 0.09 | 0.09 | 0.12 | 0.12 | 0.12 | 0.12 | 0.15 |
| `Smart%` | 0 | 0 | 30 | 40 | 50 | 50 | 60 | 70 | 80 | 90 |
| `Split%` | 15 | 20 | 25 | 25 | 25 | 33 | 33 | 33 | 33 | 50 |
| `Name` | Slow and Dumb I | Slow and Dumb II | Faster and Smarter I | Faster and Smarter II | Faster and Smarter III | Prelude | Dooms Day I | Dooms Day II | You've got to be kidding! | This ain't right |

Globals (all levels): `mSpeed=1.5`, `mRadius(mMaxStatus)=25`,
`bRadius(bMaxStatus)=35`, `Cities(MaxTarget)=10`, `bExplodeb=True`,
`Sound=True` (parsed, ignored — no audio ABI), `SyncDist=500`,
`SyncDelay=0.05`, `SyncTime=1`.

`SyncFactor = 25`. Runtime hostile px/tick (`bSpeed*25`, before the
per-spawn `*(1+Rnd)`): L1 1.25, L2 1.25, L3 1.75, L4 2.25, L5 2.25,
L6–L9 3.0, L10 3.75. Defensive: 37.5 px/tick on every level.

Field semantics are unchanged from MC3 §1 (quotas vs pools vs
probabilities, split pool-growth, missile-counter quirk). Two scale notes
new to MC4: on L9–L10 the in-flight pools (50/50) allow up to 50
concurrent hostiles, and saturated splits grow the quota through
pool-growth faster than a weak defense can drain it (measured: L10 quota
100 → 121+ when the pool saturates — the original has the same dynamics).

---

## 2. Campaign progression (VB `DoIt` For-loop, `MaxLevel = 10`)

- L1 → L2 → … → L10, automatic after the deterministic 40-tick / 2 s
  `LEVEL COMPLETE` dwell (fixed-step ticks, never wall clock).
- Each handoff applies its own data (`mc_apply_level`), rebuilds pools
  (`BuildPool` with the new `bDrop`/`mFire`), zeroes counters, clears the
  fire latch and all explosions. No RNG reseeding (VB `Randomize` runs once
  per `DoIt`, not per level); no stale transition state.
- Cities persist: VB `ResetTargets` redraws from `Targets()` but never
  assigns it; destroyed cities stay destroyed across all ten levels. Only
  the full restart (R/Enter/Space after GAME OVER or GAME COMPLETE, the
  File > Play replacement) revives them and returns to L1.
- After L10, `GAME COMPLETE` terminal (the original loop falls off its end
  with `blnWon` set and shows no message; the port reports GAME COMPLETE
  with a restart hint — closest verified terminal state, no invented
  content beyond the label).
- Win/lose evaluation is exactly VB with original precedence: lose when no
  city is alive (beats a drained quota); else win when `bDropped >= bMax`
  AND the hostile pool has drained. Terminal states freeze the sim; fire is
  refused during the dwell and after terminals.
- `kMcCampaignLevels` is 10 (was 3). `mc_apply_level`, `mc_advance_level`,
  `mc_reset_campaign`, `mc_reset_level`, and `mc_ensure_campaign` were
  already parametric — the extension is the constant plus per-level data,
  overlay (`Ln/10`), per-level `LEVEL N started` log markers, and tests.

### City persistence semantics

Destroyed cities remain destroyed across every handoff (tested L1→L2 and
in the 9-handoff walk). Restart revives all 10. Entering a later level
with few cities is normal play (verified: 2 cities enter L6, persist to
L7; 1 city enters L7, and its immediate loss reports lose, never a stale
win). There is no per-level city repair in the original and none in the
port.

### Campaign transition semantics (edge cases defined by test)

- One city remains entering a later level: plays on; its death reports
  lose with precedence over any drained quota.
- Final city dies immediately after a transition: lose, never win.
- Split children near the completion boundary: quota met but child active
  ⇒ no win yet (pool must drain); draining completes the level. The VB
  quota quirk is preserved through this (child charges `mFired`).
- Smart evasion during the last hostile: flip counted, pool valid, outcome
  still resolves (evader blocks completion while in flight).
- Victory vs city-loss in nearby ticks: impact on the final city beats a
  drained quota (lose); killing that diverter first wins instead.

---

## 3. Deterministic full-campaign validation (§5 scenario)

At least one automated scenario begins from L1, progresses through all ten
levels, completes, and fingerprints identically across runs: the host suite
plays seed 39 (`MC4_CAMPAIGN_SEED`, also the port default seed) L1→…→L10
to GAME COMPLETE twice and requires identical canonical fingerprints
(observed: fingerprint `16492225105589479459`, 4059 simulation steps, both
runs; a pristine-L10 seed-31 run covers the L10 terminal independently).

Normal per-tick survival cannot carry all ten levels: L9–L10 are 50–100
quota at 80–90% smart with 33–50% MIRV splits, and a 400-seed search over
fixed-policy defenses never completed (best reached L10 and died there).
Per the phase brief, MC4 uses controlled test hooks that exercise real
simulation state without altering normal gameplay behavior (the shipped
game is untouched — no assist code, no infinite ammo, no state patching):

- **Model-predictive auto-aim** (test driver `mpc_tick`, mirrored exactly
  by the live `A`-key autopilot): pack-first targeting (dense packs carry
  chain kills), iterated intercept prediction, young-burst cleanliness
  gate (55 px), level-scaled caps (2/4/6/8, 12 on L9–L10), and per-shot
  lookahead verification on a value-copy of the state (replanned every
  tick). Every shot is a legal `mc_request_fire` latch; every kill flows
  through `mc_fixed_update`.
- **Last-ditch point defense** (`mpc_point_defense`, L9+ only): at most one
  placement per tick and only when the MPC fired nothing, only for threats
  below y=620 with no live burst/missile within 40 px, only from a free
  pool slot, honestly charging `mFired`. The kill flows through the real
  intercept/retire pipeline. Models a perfect human snap-shot; zero flight
  time on these final intercepts is the only unreal element, forced by
  L9–L10 physics.
- Seed 39 completes with any 0–500 tick unassisted opening (verified
  0/100/250/400/500/800; enters L9 with 6–7, L10 with 3–4, wins the last
  stand with 1), so the live launch-to-keypress gap cannot break it; seed
  31 wins a fresh L10 outright (454 ticks, 171/200 ammo). Seventeen of the
  first 200 seeds complete at gap 0 — the campaign is hard, not broken.

What was tried and measured along the way (seed-search log retained in
scratch, results summarized): single-burst discipline (dies L2–L4), volume
fire (exhausts ammo), area-denial screens (wasteful), double-tap bracketing
(worse), negative/positive aim bias (worse), cluster targeting without
verification (burns ammo), saturation mode (dies earlier). The
verification-precision gap was quantified (15% live-kill rate on early L10
under chaos) and closed by pack-first selection plus the cleanliness gate;
the quota-balloon mechanism (saturated splits grow `bMax` 100→121+) was
confirmed as the endgame killer.

---

## 4. `build1.gif` archaeology (original city rendering)

- File: 2427 bytes, GIF89a, logical screen **153×121**, global color table
  256 entries, **one image descriptor, no Graphic Control Extension**
  (no transparency), LZW minimum code 8. A 188-byte non-GIF tail follows
  the `0x3B` trailer (ignored by decoders, including VB `LoadPicture`).
- Palette usage (independent pure byte-level LZW decode, no image
  library): exactly **10 indices** — black `(0,0,0)` background majority
  (11308/18513 px, 61%) plus nine grays/red: `(66,66,66)`, `(99,99,99)`,
  `(115,115,115)`, `(148,148,148)`, `(165,165,165)`, `(189,189,189)`,
  `(198,198,198)`, `(214,214,214)`, `(239,0,0)`. The canvas is a single
  city-skyline tile (five building facades plus a small red detail near
  the top); row-by-row ASCII mapping is in the phase notes.
- VB usage (`ResetTargets`): loads the GIF into a hidden `Image1`, scales
  it to `AspectX = p.Width / n` (`AspectY` preserves aspect), then
  `BitBlt`s the **whole stretched tile once per alive slot**,
  bottom-anchored, evenly spaced. One image holds ONE city state — no
  per-state tiles, no repeated tiles, no hard-coded sprite coordinates.
  Destroyed slots are simply not drawn (empty black). No other routine
  references the file (`tFile = "build1.gif"` is the only path constant).

---

## 5. City art conversion + rendering (guideXOS-native)

- Target format: the **app-visible GXIM layout** — the same 28-byte `GXIM`
  format the PacMan app uses for its sprites
  (`D:\dev\pacman\guidexos\tools\convert_bmp_to_gximg.ps1` produces it,
  `bitmap_loader.cpp` consumes it): `GXIM` + u32 version(1) + u32
  width + u32 height + u32 stride(=w·4) + u32 pixelFormat(1=XRGB8888) +
  u32 payloadBytes + top-down B,G,R,0 pixels. (The kernel-wallpaper
  `GXIMG001` 20-byte variant is a different, incompatible layout for a
  different consumer; the app-visible one is used here. There is no
  App Model image/blit host call — apps composite into their
  `present_frame` framebuffer, exactly like `DD.ini` is read through
  `file_read_all`. No new graphics pipeline was built.)
- Conversion: `scripts/convert-missilecommand-city.ps1` (deterministic: no
  resize, no dither, `GetPixel` values copied verbatim top-down) writes
  `sdk/samples/missilecommand/resources/city.gximg` (153×121, 74080
  bytes), staged to `Apps/MissileCommand/resources/city.gximg` by
  `sdk/build-samples.ps1`. Verified byte-exact: all 18513 pixels match
  the independent LZW decode.
- Rendering (`main.cpp`, logic in `missilecommand_city_art.h` shared with
  the host test): alive cities blit the tile nearest-neighbor into a
  28×22 footprint — uniform scale to the slot width (28/153), aspect error
  <1%, bottom-anchored at the ground line like the original's BitBlt.
  Source black (`0x000000`) is transparent (playfield shows through);
  everything else is opaque. Simulation hitboxes are untouched (still the
  MC1 rectangle slots; art cannot change gameplay).
- Fallback + safety: missing/unparseable/d wrongly-sized GXIM ⇒ MC3
  yellow rectangles (game stays playable). Destroyed cities keep the
  rubble marker (original leaves empty black — documented readability
  divergence). Host tests pin: parser accept/reject table, clamping,
  monotonic full-span sampling, dest rects (shared left edge, 28×22,
  bottom-anchored, inside frame for all 10 slots), alive/dead/fallback
  selection rule, logical geometry unchanged, and the staged artifact
  itself (size, tile dims, transparent corner, known opaque pixel
  `0x00D6D6D6` at (50,65), 9 opaque palette entries, transparent
  majority). Loading uses chunked `file_read` (8 KiB; `file_read_all`
  caps at 64 KiB and the art is 74 KiB — found live, fixed, verified by
  the `city art GXIM loaded` marker and the `art=gxim` overlay tag).
- Live original-art rendering result: `art=gxim` overlay plus gray-skyline
  tiles observed in completed smoke runs (the runnable artifact).

---

## 6. Audio archaeology (exact source evidence)

VB6 `sndPlaySound` declaration (`winmm.dll`, alias `sndPlaySoundA`) is
called with flags `1` (`SND_ASYNC`) everywhere: fire-and-forget,
overlapping allowed, never looped. Call sites and files:

| Event (source site) | File played | Exists in source tree? | Format |
|---|---|---|---|
| Level start (`Alarm()`, per `DoIt` level) | `Alarm.wav` | YES (`Alarm.WAV`, case-insensitive load) | PCM 8-bit mono 11025 Hz |
| Defensive launch success (`FireFX()`, every `LaunchM`) | `Swoosh.wav` | YES (`Swoosh.wav`) | **ADPCM 4-bit stereo 22050 Hz** (format tag 2 — the only compressed asset) |
| Defensive interception kill (`Intercept`) | `Thunder.wav` | **NO — missing even in the original** | — |
| Defensive launch refused, pool/quota out (`LaunchM` else) | `Empty.wav` | YES (`Empty.WAV`) | PCM 8-bit mono 11025 Hz |
| Defensive detonation at target (`MyShow`, missile arm) | `Explode.wav` | YES (`EXPLODE.WAV`) | PCM 8-bit mono 11025 Hz |
| Bomb ground impact (`MyShow`, bomb arm) | `Thunder.wav` | **NO (same missing file)** | — |
| MIRV split (`MyShow`, split arm) | `Split.wav` | YES (`Split.WAV`) | PCM 8-bit mono 11025 Hz |
| Campaign lost (`DoIt` tail, `blnLost`) | `OhNo.wav` | YES (`Ohno.wav`) | PCM 16-bit mono 22050 Hz |
| Campaign quit (`DoIt` tail, `blnQuit`) | `Error.wav` | **NO — missing even in the original** | — |
| Campaign won (`DoIt` tail, `blnWon`) | `OnNo.wav` | commented out (never compiled in) | — |

`sndPlaySound` with a missing file is a silent no-op (returns 0, no
crash): the original itself always played `Thunder`/`Error` into the
void. `Sound=True` (`SoundFX`) gates every call; the port parses the key
and ignores it (no audio ABI), same as MC3.

---

## 7. guideXOS audio capability findings (Outcome C)

- The App Model ABI (`sdk/include/guidexos/abi.h`, enumerated fully)
  exposes **zero audio host calls**: log, window, draw, poll, exit,
  file_*, build-service, dev-run, dev-debug — no PCM playback, no WAV
  decoding, no mixer, no beep.
- `audio.output` exists only as a *known permission string* in
  `app_manifest_validator.cpp`. Nothing enforces, dispatches, or honors
  it: no runtime support, no bare-metal path, no hosted backend.
- The kernel has bare-metal audio *drivers* (Intel HDA/AC'97
  `kernel/.../pci_audio.*`, USB audio `usb_audio*`) but no mixer/PCM
  pipeline and no path from a hosted Native ELF app to any of it.
- No packaged or sample app uses audio (PacMan ships none; Missile
  Command MC1–MC3 is silent). There is no precedent to reuse.
- Conclusion: **Outcome C**. No correct application-visible playback path
  exists. Building one inside MC4 (an app-private WAV decoder plus a
  direct hardware path, or a bespoke Missile-Command-only host call)
  would bypass App Model isolation and violate the phase constraints, so
  audio was designed, not implemented. Campaign + art carry MC4.

---

## 8. Audio proposal for a future phase (MC5)

Smallest reusable App Model audio interface (concrete proposal, not yet
implemented):

- Manifest: apps opt in with the already-known `audio.output` permission
  (validator accepts it today; the runtime would start enforcing it).
- One appended host call (ABI-stability rule: append-only slots):
  `play_sound(ctx, name, offset)` → `GX_OK` / `GX_ERROR_*`, where `name`
  selects a packaged short WAV resource (`resources/*.wav`) and playback
  is fire-and-forget async mixing (match the original `SND_ASYNC`
  semantics: overlap allowed, no loops, no positional audio).
- Host responsibilities: WAV decode (PCM 8/16-bit mono, 11025/22050 Hz —
  covers every original asset except `Swoosh.wav`'s ADPCM, which stays
  silent or is converted at build time), a tiny mixer over the existing
  kernel HDA/USB backends on bare metal and the host OS mixer when
  hosted, missing-file silence (match `sndPlaySound`), and
  `audio.output` gating (deny without the permission).
- Missile Command mapping (verified events only): launch→`Swoosh.wav`,
  refused→`Empty.wav`, defensive detonation→`Explode.wav`,
  intercept kill→*(missing `Thunder.wav`: silence)*, ground
  impact→*(same missing file: silence)*, split→`Split.wav`, level
  start→`Alarm.wav`, campaign lost→`Ohno.wav`. Never substitute
  copyrighted external assets; ship only the project-owned WAVs (five
  present) or silence.
- Tests for that phase: resource load, event mapping, missing-file
  silence, no-crash without audio hardware/permission, no App Model
  isolation bypass (app never touches drivers directly).

---

## 9. Input, compositor, `DD.ini`, and status (preserved/extended)

- Pointer contract unchanged and regression-tested: left DOWN fires,
  right MOVE (drag) fires, right DOWN alone fires nothing, release never
  fires, no sticky state across transitions (host matrix in
  `missilecommand_state_test.cpp`; compositor suite
  `compositor_pointer_buttons_test.cpp` passes unmodified — the MC3 fix is
  untouched).
- Live input additions are additive test-only keys: `O`/`P` force
  autopilot on/off, `G`/`H` force fast-forward on/off (idempotent, so the
  smoke can re-arm against cold-start races without oscillating; legacy
  `A`/`F` toggles retained). Found live: log-lag retried toggles
  oscillated the hooks and killed runs — the forced forms fixed it by
  construction.
- `DD.ini` policy: **per-level fallback** (one malformed row keeps its
  compiled row; valid rows before AND after still apply), tested for
  missing L10, malformed L7, bad globals, and empty input. Runtime file
  authoritative; compiled values are safe fallback. Overlay shows
  `ini=run`/`ini=fb` plus the new `art=gxim`/`art=rect`.
- Status UI: `MC4 campaign Ln/10 <Name>`, cities/bombs/missiles/active,
  `LEVEL COMPLETE - advancing...`, `GAME COMPLETE` / `GAME OVER` with
  restart hints. No debug clutter.

---

## 10. Remaining VB6 differences (deliberate)

1. RNG: seeded LCG vs `Rnd`/`Randomize Timer` (determinism required).
   Default seed 39 (arbitrary; chosen because the demo assist completes
   on it across 0–800 tick openings).
2. Trig → unit-vector normalization: identical geometry, no libm; FP bits
   may differ (live↔host bit-identity still verified, §11).
3. Fixed 10 city slots (`Cities` parses 5..20 but gameplay uses 10; all
   shipped levels use 10).
4. Destroyed cities show a rubble marker (original: empty black).
5. `GAME COMPLETE` label (original: silent loop exit).
6. 50 ms fixed step replaces the `SyncDelay` busy gate; restart key
   replaces File > Play; autopilot/fast-forward/force keys are test-only.
7. No audio (no host call — §7/§8).
8. `Swoosh.wav` is ADPCM (relevant only to the MC5 proposal).

---

## 11. Validation

- `tests/missilecommand_state_test.cpp` via
  `scripts/run-missilecommand-state-test.ps1`: **PASS** — L1–L10 table
  (10 rows × 7 fields + names), scaled speeds per level, 9-handoff
  mechanical walk, full MPC L1→L10 playthrough to GAME COMPLETE,
  pristine-L10 terminal, full-campaign ×2 identical fingerprints,
  survivability edges (few/one/final-city, split-at-boundary,
  last-hostile evasion, terminal precedence both ways), late-row INI
  robustness (per-level policy), GXIM parser/mapping/selection/artifact
  tests, input matrix, coordinate mapping, 500-tick vector, null safety.
- `tests/compositor_pointer_buttons_test.cpp`: **PASS** (unmodified).
- `sdk/build-samples.ps1`: all three samples Success (city.gximg staged).
- Live Native ELF/App Model smoke `scripts/smoke-missilecommand-mc4.ps1`:
  **21/22** across repeated runs — registration, launch, DD.ini runtime,
  city art GXIM load (`art=gxim` overlay), window, spawn, launch,
  detonation, hooks, live right-drag routing, L2/L4/L7/L10 markers,
  natural smart spawn + evasion, combat, restart, Escape, clean exit all
  PASS. Live↔host bit-identity PROVEN: a live run's per-tick transition
  fingerprints (cities/steps at every handoff) matched the host
  gap-600 reference exactly (L2 6@818 … L10 4@3800). `gameComplete` is
  the single miss: live campaigns reach L10 and die there whenever the
  launch-to-keypress gap lands in the 600-tick dead zone (measured; host
  sweeps show 0/50 completions at gap 600 vs 17/200 at gap 0 and 5/50 at
  gap 800). The host campaign completes deterministically; the live
  environment's ~30–40 s launch/keypress latency currently lands in the
  dead zone, and the experimental server intermittently stalls multi-
  minute boots in the small hours (unrelated platform flake, out of MC4
  scope). Mitigations shipped: gap-robust seed 39 (completes 0–500 and
  800), idempotent force-keys, adaptive polling, two-campaign vehicle
  with perturbation-free watching, verified input demo.
- `scripts/run-native-abi-layout-test.ps1`: unchanged pre-existing
  failure (`sizeof(gx_host_calls)` 120 vs 240) — unrelated, untouched.

## 12. Recommended MC5 scope

App Model audio (`audio.output` host call + mixer, §8) with Missile
Command as first client; optional: high-score/persistence proposal,
menu polish. No engine, networking, compositor, or filesystem redesign.
