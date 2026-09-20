#pragma once

// Missile Command MC2 deterministic defense loop.
//
// Pure logic only: no guideXOS includes, no dynamic allocation, no libc,
// no libm. Shared by the Native ELF entry point
// (sdk/samples/missilecommand/main.cpp) and the host unit test
// (tests/missilecommand_state_test.cpp).
//
// Behavioral specification: the VB6 original at
// D:\dev\bkup\inactive\missilecommand, Module1.bas / FrmDD.frm / DD.ini
// (branch master, read-only reference). VB routine mapping:
//
//   LaunchB    -> mc_launch_b        (+ mc_pick_target, mc_pick_random)
//   LaunchM    -> mc_launch_m
//   Intercept  -> mc_intercept_pass  (defensive-vs-hostile, then hostile
//                bomb-chain when bExplodeb is set, exactly like DoIt)
//   MyShow     -> mc_myshow_hostiles / mc_myshow_defense
//   DoIt       -> mc_fixed_update    (one call == one VB Sync-gated tick;
//                spawn ordering LaunchB -> LaunchM -> MyShow b -> MyShow m
//                -> Intercept -> win/lose evaluation, preserved)
//   InRange    -> mc_in_range        (SQUARE box test, preserved quirk)
//   x2t        -> mc_x_to_target
//   BuildPool  -> mc_build_pool
//   Nullp      -> mc_null_proj
//   ResetTargets (state part) -> mc_reset_targets (10 slots alive)
//   MouseRead  -> pending-fire latch consumed inside mc_fixed_update
//   MachineSpeed/InitLevels L1 -> kMcL1* compiled constants below
//
// Coordinate convention: gameplay runs in VB logical units, 1000x750,
// origin top-left, Y down, single central battery at (500,750).
// The native window frame is 480x360; mc_vb_to_window_* /
// mc_window_to_vb_* translate. Gameplay code never uses window units.
//
// Tick model: one mc_fixed_update call equals one original Sync-gated tick
// (SyncDelay 0.05s -> 20 ticks/s). Speeds below are px/tick AFTER the
// original SyncFactor scaling ((SyncDist*SyncDelay)/SyncTime = 25), so the
// wall-clock pacing matches L1 on the default DD.ini tuning.

#include <stdint.h>

// ---------------------------------------------------------------------------
// VB playfield + window frame geometry (MC1-established, unchanged)
// ---------------------------------------------------------------------------

static const int kMcVbMaxX = 1000;
static const int kMcVbMaxY = 750;
static const int kMcVbBatteryX = 500;
static const int kMcVbBatteryY = 750;
static const int kMcCitySlotCount = 10;

static const int kMcFrameWidth = 480;
static const int kMcFrameHeight = 360;
static const int kMcGroundTop = 336;
static const int kMcCityWidth = 28;
static const int kMcCityHeight = 14;
static const int kMcCityGap = 18;
static const int kMcBatteryWidth = 36;
static const int kMcBatteryHeight = 16;

// ---------------------------------------------------------------------------
// Fixed-step driver (one gameplay tick per step; matches VB 20 ticks/s)
// ---------------------------------------------------------------------------

static const uint64_t kMcFixedStepMs = 50u;
static const uint64_t kMcMaxElapsedMs = 250u;
static const uint32_t kMcMaxCatchUpSteps = 8u;
static const uint64_t kMcVisualIntervalMs = 16u;

static const int kMcKeyEscape = 27;
static const int kMcKeyActionDown = 1;
static const int kMcKeyRestartR = 82;  // 'R' (Win32 VK + ASCII agree)
static const int kMcKeyRestartEnter = 13;
static const int kMcKeyRestartSpace = 32;

// ---------------------------------------------------------------------------
// L1 configuration, recovered from DD.ini l1 +
// global tuning, scaled by SyncFactor = (500*0.05)/1 = 25.
//
// DD.ini l1: bMax=10, mMax=50, bDrop=5, mFire=5, bSpeed=0.05,
//            Smart=0%, Split=15%, Name "Slow and Dumb I"
// Globals : mSpeed=1.5, mRadius(mMaxStatus)=25, bRadius(bMaxStatus)=35,
//           Cities(MaxTarget)=10, bExplodeb=True
// Scaled  : bSpeed 0.05*25 = 1.25 px/tick, mSpeed 1.5*25 = 37.5 px/tick
// ---------------------------------------------------------------------------

static const int kMcL1BombQuota = 10;
static const int kMcL1MissileQuota = 50;
static const int kMcL1BombInflightCap = 5;
static const int kMcL1MissileInflightCap = 5;
static const float kMcL1BombSpeed = 1.25f;
static const float kMcL1MissileSpeed = 37.5f;
static const int kMcL1SmartPercent = 0;
static const int kMcL1SplitPercent = 15;
static const int kMcL1DefenseBlastMax = 25;  // mMaxStatus
static const int kMcL1BombBlastMax = 35;     // bMaxStatus
static const int kMcL1CityCount = 10;
static const bool kMcL1BombChain = true;  // bExplodeb

// Pool bound: VB arrays are 1..500 (bMax/mMax constants in Module1.bas).
// Lists are 1-based; slot 0 means null. In-flight concurrency is capped by
// the free list built with bDrop/mFire; split growth appends high slots.
static const int kMcPoolCap = 500;

// Deterministic RNG default seed. The original uses Randomize Timer
// (non-deterministic); the port uses a seeded LCG so seed + inputs + ticks
// fully determine the state. Documented difference, required for tests.
static const uint32_t kMcDefaultSeed = 987654321u;

// ---------------------------------------------------------------------------
// Projectile (VB pType, 1-based pool slot)
// ---------------------------------------------------------------------------

struct McProj {
    float xs;      // source x (trail start)
    float ys;      // source y
    float x;       // current x
    float y;       // current y (VB field is Y)
    float xm;      // per-tick x velocity
    float ym;      // per-tick y velocity (negative = defensive, going up)
    int xe;        // detonation/impact x (defensive target / hostile target)
    int ye;        // detonation/impact y (defensive target / always 750)
    int link;      // intrusive free/active list link (0 = null)
    int status;    // 0 = pooled, 1 = in flight, >=2 = explosion radius
    int splitY;    // 0 = no split, else y at which this bomb splits (MIRV)
    bool smart;    // smart-bomb evasion flag
};

struct McLevelRuntime {
    int bMax;      // bomb quota (grows on split pool-growth, like VB Level.bMax)
    int mMax;      // missile quota
    int bDrop;     // hostile in-flight pool size
    int mFire;     // defensive in-flight pool size
    float bSpeed;  // hostile px/tick (scaled)
    float mSpeed;  // defensive px/tick (scaled)
    int smart;     // smart %
    int split;     // split %
    int bMaxStatus;
    int mMaxStatus;
    bool bExplodeb;
};

struct McState {
    uint64_t simulationSteps;
    uint32_t rng;  // deterministic LCG state

    McLevelRuntime level;

    // 1-based pools: index 0 is the null link. bHead/mHead are active-list
    // heads, bPool/mPool are free-list heads (VB Head%/Pool% idiom).
    McProj b[kMcPoolCap + 1];
    McProj m[kMcPoolCap + 1];
    int bHead;
    int bPool;
    int mHead;
    int mPool;
    int bDropped;  // quota progress (VB bDroped%)
    int mFired;    // quota progress (VB mFired%)

    bool targets[kMcCitySlotCount + 1];  // 1..10 alive flags (VB Targets())

    // Pointer latch (VB mMouseX/mMouseY/mMouseButton + MouseRead): at most
    // one shot per tick; a second press within the same tick overwrites.
    // Stored as integer VB units, like the VB Integer mouse coordinates.
    bool pendingFire;
    int fireX;
    int fireY;

    bool won;
    bool lost;
    bool running;
    bool visualDirty;

    int clickCount;   // fire requests received (diagnostic/status)
    int lastKeyCode;  // last key seen (diagnostic/status)
    int listGuardTrips;  // cross-linked-list guard trips (VB Stop equivalent)
};

// ---------------------------------------------------------------------------
// Small deterministic helpers (no libm: freestanding ELF links no libm)
// ---------------------------------------------------------------------------

inline float mc_fabs(float v) { return v < 0.0f ? -v : v; }

// Deterministic square root (Newton iteration, fixed count). Replaces the
// VB Atn/Cos/Sin trig chain: LaunchB/LaunchM directions are exactly the
// unit vector from source to target, which needs only a length. Geometry
// matches the original; raw FP bits are not claimed bit-identical.
inline float mc_sqrt(float v) {
    if (v <= 0.0f) return 0.0f;
    float x = v * 0.5f + 0.5f;
    for (int i = 0; i < 24; ++i) x = 0.5f * (x + v / x);
    return x;
}

// VB Rnd() replacement: LCG, returns [0,1).
inline float mc_rnd(McState* state) {
    state->rng = state->rng * 1664525u + 1013904223u;
    return (float)(state->rng >> 8) * (1.0f / 16777216.0f);
}

// VB PickRandom%(l,u): spin Rnd*1000 until inside [l,u]; else l.
// Biased rejection quirk preserved. Iteration cap is a hang guard that
// never triggers for the ranges the game uses; it keeps the port total.
inline int mc_pick_random(McState* state, int l, int u) {
    if (!(l < u && l <= 1000 && u >= 0)) return l;
    float t = (float)l;
    for (int i = 0; i < 4096; ++i) {
        t = mc_rnd(state) * 1000.0f;
        if (t >= (float)l && t <= (float)u) break;
    }
    if (t < (float)l) t = (float)l;
    if (t > (float)u) t = (float)u;
    return (int)(t + 0.5f);
}

// VB x2t: map logical x onto target slot [1, MaxTarget].
inline int mc_x_to_target(int x) {
    int i = (kMcCitySlotCount * x) / kMcVbMaxX + 1;
    if (i < 1) i = 1;
    if (i > kMcCitySlotCount) i = kMcCitySlotCount;
    return i;
}

// VB InRange%: SQUARE (box) proximity test, not a circle. Preserved quirk:
// a hostile at corner (dx=r, dy=r) is inside (dist r*sqrt(2) > r) while a
// Euclidean test would let it survive.
inline bool mc_in_range(float x1, float y1, int r, float x2, float y2) {
    if (x2 >= x1 - (float)r && x2 <= x1 + (float)r) {
        if (y2 >= y1 - (float)r && y2 <= y1 + (float)r) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Coordinate mapping: VB logical <-> window frame (scale 48/100)
// ---------------------------------------------------------------------------

inline int mc_vb_to_window_x(int vb) { return (vb * 48) / 100; }
inline int mc_vb_to_window_y(int vb) { return (vb * 48) / 100; }
// Rounded float variant for rendering sub-integer warhead positions.
inline int mc_vb_to_window_f(float v) { return (int)(v * 0.48f + 0.5f); }
// Inverse (pointer input): window pixel -> VB units, clamped to the field.
inline int mc_window_to_vb_x(int w) {
    if (w < 0) w = 0;
    if (w >= kMcFrameWidth) w = kMcFrameWidth - 1;
    return (w * 100) / 48;
}
inline int mc_window_to_vb_y(int w) {
    if (w < 0) w = 0;
    if (w >= kMcFrameHeight) w = kMcFrameHeight - 1;
    return (w * 100) / 48;
}

// Window x of the left edge of city slot [0, kMcCitySlotCount) (MC1 layout).
inline int mc_city_x(int index) {
    if (index < 0) index = 0;
    if (index >= kMcCitySlotCount) index = kMcCitySlotCount - 1;
    return kMcCityGap + index * (kMcCityWidth + kMcCityGap);
}

inline int mc_city_y() { return kMcGroundTop - kMcCityHeight; }
inline int mc_battery_x() { return (kMcFrameWidth - kMcBatteryWidth) / 2; }
inline int mc_battery_y() { return kMcGroundTop - kMcBatteryHeight; }

// ---------------------------------------------------------------------------
// Pool + level setup
// ---------------------------------------------------------------------------

inline void mc_null_proj(McProj* p) {
    if (!p) return;
    // VB Nullp zeroes everything except xe/Link; the port also clears xe
    // (every live read follows a write, so no behavioral effect) while the
    // caller owns Link, exactly like RemoveIt/BuildPool do.
    p->xs = 0.0f;
    p->ys = 0.0f;
    p->x = 0.0f;
    p->y = 0.0f;
    p->xm = 0.0f;
    p->ym = 0.0f;
    p->xe = 0;
    p->ye = 0;
    p->status = 0;
    p->smart = false;
    p->splitY = 0;
}

// VB BuildPool: free list of Max slots (1..Max), active head empty.
inline void mc_build_pool(McProj* pool, int cap, int* head, int* freeHead, int max) {
    if (!pool || !head || !freeHead) return;
    if (max < 0) max = 0;
    if (max > cap) max = cap;
    *head = 0;
    *freeHead = (max > 0) ? 1 : 0;
    for (int i = 1; i <= max; ++i) {
        mc_null_proj(&pool[i]);
        pool[i].link = (i < max) ? (i + 1) : 0;
    }
}

inline void mc_reset_targets(McState* state) {
    if (!state) return;
    for (int i = 1; i <= kMcCitySlotCount; ++i) state->targets[i] = true;
}

inline void mc_apply_l1(McState* state) {
    if (!state) return;
    state->level.bMax = kMcL1BombQuota;
    state->level.mMax = kMcL1MissileQuota;
    state->level.bDrop = kMcL1BombInflightCap;
    state->level.mFire = kMcL1MissileInflightCap;
    state->level.bSpeed = kMcL1BombSpeed;
    state->level.mSpeed = kMcL1MissileSpeed;
    state->level.smart = kMcL1SmartPercent;
    state->level.split = kMcL1SplitPercent;
    state->level.bMaxStatus = kMcL1BombBlastMax;
    state->level.mMaxStatus = kMcL1DefenseBlastMax;
    state->level.bExplodeb = kMcL1BombChain;
}

inline void mc_init_with_seed(McState* state, uint32_t seed) {
    if (!state) return;
    state->simulationSteps = 0u;
    state->rng = seed;
    mc_apply_l1(state);
    for (int i = 0; i <= kMcPoolCap; ++i) {
        mc_null_proj(&state->b[i]);
        state->b[i].link = 0;
        mc_null_proj(&state->m[i]);
        state->m[i].link = 0;
    }
    mc_build_pool(state->b, kMcPoolCap, &state->bHead, &state->bPool, state->level.bDrop);
    mc_build_pool(state->m, kMcPoolCap, &state->mHead, &state->mPool, state->level.mFire);
    mc_reset_targets(state);
    state->bDropped = 0;
    state->mFired = 0;
    state->pendingFire = false;
    state->fireX = 0;
    state->fireY = 0;
    state->won = false;
    state->lost = false;
    state->running = true;
    state->visualDirty = true;
    state->clickCount = 0;
    state->lastKeyCode = 0;
    state->listGuardTrips = 0;
}

inline void mc_init(McState* state) { mc_init_with_seed(state, kMcDefaultSeed); }

// Clean level reset (restart key after win/lose). Restores L1 quota
// (Level.bMax may have grown through split pool-growth), rebuilds the
// in-flight pools, revives all cities, clears the outcome. The RNG stream
// is deliberately NOT reseeded so replays do not repeat identically
// (the original re-randomized via Randomize Timer each DoIt).
inline void mc_reset_level(McState* state) {
    if (!state) return;
    mc_apply_l1(state);
    for (int i = 0; i <= kMcPoolCap; ++i) {
        mc_null_proj(&state->b[i]);
        state->b[i].link = 0;
        mc_null_proj(&state->m[i]);
        state->m[i].link = 0;
    }
    mc_build_pool(state->b, kMcPoolCap, &state->bHead, &state->bPool, state->level.bDrop);
    mc_build_pool(state->m, kMcPoolCap, &state->mHead, &state->mPool, state->level.mFire);
    mc_reset_targets(state);
    state->bDropped = 0;
    state->mFired = 0;
    state->pendingFire = false;
    state->won = false;
    state->lost = false;
    state->visualDirty = true;
}

// ---------------------------------------------------------------------------
// Queries (status overlay + tests)
// ---------------------------------------------------------------------------

inline int mc_alive_cities(const McState* state) {
    if (!state) return 0;
    int n = 0;
    for (int i = 1; i <= kMcCitySlotCount; ++i) {
        if (state->targets[i]) ++n;
    }
    return n;
}

inline int mc_active_count(const McProj* pool, int cap, int head) {
    int n = 0;
    int link = head;
    int guard = 0;
    while (link != 0 && guard <= cap) {
        if (link < 1 || link > cap) break;
        ++n;
        link = pool[link].link;
        ++guard;
    }
    return n;
}

inline int mc_active_hostiles(const McState* state) {
    if (!state) return 0;
    return mc_active_count(state->b, kMcPoolCap, state->bHead);
}

inline int mc_active_defense(const McState* state) {
    if (!state) return 0;
    return mc_active_count(state->m, kMcPoolCap, state->mHead);
}

inline int mc_free_count(const McProj* pool, int cap, int freeHead) {
    int n = 0;
    int link = freeHead;
    int guard = 0;
    while (link != 0 && guard <= cap) {
        if (link < 1 || link > cap) break;
        ++n;
        link = pool[link].link;
        ++guard;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Targeting (VB PickTarget)
// ---------------------------------------------------------------------------

inline void mc_pick_target(McState* state, int* tx, int* x) {
    // Caller guarantees at least one city is alive (mc_launch_b checks);
    // otherwise the original spins here until DoIt exits via blnLost.
    *x = mc_pick_random(state, 0, kMcVbMaxX);
    int guard = 0;
    do {
        *tx = mc_pick_random(state, 0, kMcVbMaxX);
        ++guard;
    } while (!state->targets[mc_x_to_target(*tx)] && guard < 4096);
    if (guard >= 4096) *tx = *x;
}

// ---------------------------------------------------------------------------
// Enemy spawn (VB LaunchB)
// ---------------------------------------------------------------------------
// splitChild/sx/sy carry the MIRV path (VB Optional Split/sx/sy).
// Returns the slot index, or 0 when the spawn was refused (pool exhausted).
inline int mc_launch_b(McState* state, bool splitChild, float sx, float sy) {
    if (!state) return 0;
    if (state->won || state->lost || !state->running) return 0;
    if (state->bPool == 0) return 0;
    if (!(state->bDropped < state->level.bMax || splitChild)) return 0;
    if (!splitChild && mc_alive_cities(state) == 0) return 0;

    int tx = 0;
    float px = 0.0f;
    float py = 0.0f;
    if (splitChild) {
        // Override the random pick with the split point, like VB.
        // A fresh random live target is still rolled below.
        int anyTx = 0;
        int anyX = 0;
        if (mc_alive_cities(state) > 0) {
            mc_pick_target(state, &anyTx, &anyX);
            tx = anyTx;
        } else {
            tx = (int)sx;
        }
        px = sx;
        py = sy;
    } else {
        int xi = 0;
        mc_pick_target(state, &tx, &xi);
        px = (float)xi;
        py = 0.0f;
    }

    state->bDropped += 1;

    int t = state->bPool;
    state->bPool = state->b[t].link;
    state->b[t].link = state->bHead;
    state->bHead = t;

    state->b[t].x = px;
    state->b[t].y = py;
    state->b[t].xs = px;
    state->b[t].ys = py;
    state->b[t].xe = tx;
    state->b[t].ye = kMcVbMaxY;

    if (state->level.smart != 0) {
        state->b[t].smart = mc_pick_random(state, 1, 100) <= state->level.smart;
    } else {
        state->b[t].smart = false;
    }

    state->b[t].splitY = 0;
    if (state->level.split != 0) {
        if (mc_pick_random(state, 1, 100) <= state->level.split) {
            if (state->b[t].y < (float)kMcVbMaxY * 0.8f) {
                int lo = (int)(state->b[t].y + 0.5f);
                state->b[t].splitY = mc_pick_random(state, lo, (int)((float)kMcVbMaxY * 0.8f));
            }
        }
    }

    state->b[t].status = 1;

    // Straight-line trig velocity (VB Atn/Cos/Sin), expressed as the unit
    // vector toward (tx, MaxY) scaled by s = bSpeed*(1+Rnd). The straight
    // drop branch (x == tx) is kept explicit, like the original.
    float dx = (float)tx - px;
    float dy = (float)kMcVbMaxY - py;
    float s = state->level.bSpeed + state->level.bSpeed * mc_rnd(state);
    if (dx == 0.0f) {
        state->b[t].xm = 0.0f;
        state->b[t].ym = s;
    } else {
        float len = mc_sqrt(dx * dx + dy * dy);
        if (len <= 0.0f) {
            state->b[t].xm = 0.0f;
            state->b[t].ym = s;
        } else {
            state->b[t].xm = dx / len * s;
            state->b[t].ym = dy / len * s;
            if (state->b[t].ym < 0.0f) state->b[t].ym = -state->b[t].ym;
        }
    }
    return t;
}

// ---------------------------------------------------------------------------
// Defensive fire (VB LaunchM): single central battery at (500,750)
// ---------------------------------------------------------------------------
// Returns the slot index, or 0 when no missile was available/launched.
inline int mc_launch_m(McState* state, int fx, int fy) {
    if (!state) return 0;
    if (state->won || state->lost || !state->running) return 0;
    if (state->mPool == 0) return 0;
    if (!(state->mFired < state->level.mMax)) return 0;

    if (fx < 0) fx = 0;
    if (fx > kMcVbMaxX) fx = kMcVbMaxX;
    if (fy < 0) fy = 0;
    if (fy > kMcVbMaxY) fy = kMcVbMaxY;

    state->mFired += 1;

    int t = state->mPool;
    state->mPool = state->m[t].link;
    state->m[t].link = state->mHead;
    state->mHead = t;

    const float xs = (float)kMcVbBatteryX;
    const float ys = (float)kMcVbBatteryY;
    state->m[t].x = xs;
    state->m[t].y = ys;
    state->m[t].xs = xs;
    state->m[t].ys = ys;
    state->m[t].xe = fx;
    state->m[t].ye = fy;
    state->m[t].status = 1;
    state->m[t].smart = false;
    state->m[t].splitY = 0;

    // VB trig velocity toward (fx, fy), kept with the explicit straight-up
    // branch (x == xs). ym is always <= 0 (target never below the battery
    // after clamping), so detonation below always terminates the flight.
    float dx = (float)fx - xs;
    float dy = (float)fy - ys;
    if (fx == kMcVbBatteryX) {
        state->m[t].xm = 0.0f;
        state->m[t].ym = -state->level.mSpeed;
    } else {
        float len = mc_sqrt(dx * dx + dy * dy);
        if (len <= 0.0f) {
            state->m[t].xm = 0.0f;
            state->m[t].ym = -state->level.mSpeed;
        } else {
            state->m[t].xm = dx / len * state->level.mSpeed;
            state->m[t].ym = dy / len * state->level.mSpeed;
            if (state->m[t].ym > 0.0f) state->m[t].ym = -state->m[t].ym;
        }
    }
    return t;
}

// ---------------------------------------------------------------------------
// MyShow equivalent: move + detonate + retire one pool.
// VB RemoveIt splice is transliterated, including the advance-after-remove
// shape (an object landing right after a head removal waits one extra tick).
// The VB Stop-on-corruption becomes a deterministic guard trip + clean break.
// ---------------------------------------------------------------------------

inline void mc_myshow_remove(McProj* pool, int* head, int* freeHead, int prev, int cur, int* link) {
    int t = pool[cur].link;
    pool[cur].link = *freeHead;
    *freeHead = cur;
    mc_null_proj(pool + *freeHead);
    if (prev != 0) {
        pool[prev].link = t;
        *link = prev;
    } else {
        *head = t;
        *link = *head;
    }
}

// Hostile pool update (VB MyShow over b()).
// Returns the number of city kills this tick (0+).
inline int mc_myshow_hostiles(McState* state) {
    if (!state) return 0;
    int kills = 0;
    int prev = 0;
    int link = state->bHead;
    int guard = 0;
    while (link != 0 && guard <= kMcPoolCap + 2) {
        ++guard;
        if (link < 1 || link > kMcPoolCap) break;
        McProj* p = &state->b[link];
        if (p->status == 1) {
            // In flight: advance.
            p->x += p->xm;
            p->y += p->ym;

            if (p->y >= (float)p->ye) {
                // Ground impact: detonate + kill the city under impact x.
                p->status = 2;
                int xi = (int)(p->x + 0.5f);
                int slot = mc_x_to_target(xi);
                if (state->targets[slot]) {
                    state->targets[slot] = false;
                    ++kills;
                }
            } else if (p->x < 0.0f || p->x > (float)kMcVbMaxX) {
                // Off-screen retire (silent, no explosion), like VB.
                mc_myshow_remove(state->b, &state->bHead, &state->bPool, prev, link, &link);
            } else if (p->splitY != 0 && p->y >= (float)p->splitY) {
                // MIRV split: child spawns at (x, y); pool-growth can raise
                // the quota past the free list, like VB (capped at 500).
                p->splitY = 0;
                if (state->bPool == 0 && state->level.bMax < kMcPoolCap) {
                    state->level.bMax += 1;
                    state->bPool = state->level.bMax;
                    mc_null_proj(&state->b[state->bPool]);
                    state->b[state->bPool].link = 0;
                }
                mc_launch_b(state, true, p->x, p->y);
            }
        } else {
            // Exploding: grow one step per tick; city hits burn twice as
            // long (VB tmpMaxStat = MaxStat*2).
            int tmpMax = state->level.bMaxStatus;
            if (p->ym > 0.0f && p->y >= (float)p->ye) tmpMax = state->level.bMaxStatus * 2;
            p->status += 1;
            if (p->status > tmpMax) {
                mc_myshow_remove(state->b, &state->bHead, &state->bPool, prev, link, &link);
            }
        }

        prev = link;
        if (link != 0) {
            if (link < 1 || link > kMcPoolCap) break;
            if (link == state->b[link].link) {
                // Cross-linked list corruption (VB Stop). Deterministic:
                // count it and stop this pool pass cleanly.
                state->listGuardTrips += 1;
                break;
            }
            link = state->b[link].link;
        }
    }
    return kills;
}

// Defensive pool update (VB MyShow over m()).
// Returns the number of fresh detonations this tick (0+).
inline int mc_myshow_defense(McState* state) {
    if (!state) return 0;
    int bursts = 0;
    int prev = 0;
    int link = state->mHead;
    int guard = 0;
    while (link != 0 && guard <= kMcPoolCap + 2) {
        ++guard;
        if (link < 1 || link > kMcPoolCap) break;
        McProj* p = &state->m[link];
        if (p->status == 1) {
            p->x += p->xm;
            p->y += p->ym;
            // Forced destination snap: detonate exactly where the user
            // aimed despite per-tick motion error (VB MyShow quirk).
            if (p->ym < 0.0f && p->y <= (float)p->ye) {
                p->x = (float)p->xe;
                p->y = (float)p->ye;
            }
            if (p->ym <= 0.0f && p->y <= (float)p->ye) {
                p->status = 2;
                ++bursts;
            } else if (p->x < 0.0f || p->x > (float)kMcVbMaxX || p->y < 0.0f) {
                // Unreachable with clamped targets (VB has no such case);
                // retire defensively so a flight can never stick the pool.
                mc_myshow_remove(state->m, &state->mHead, &state->mPool, prev, link, &link);
            }
        } else {
            p->status += 1;
            if (p->status > state->level.mMaxStatus) {
                mc_myshow_remove(state->m, &state->mHead, &state->mPool, prev, link, &link);
            }
        }

        prev = link;
        if (link != 0) {
            if (link < 1 || link > kMcPoolCap) break;
            if (link == state->m[link].link) {
                state->listGuardTrips += 1;
                break;
            }
            link = state->m[link].link;
        }
    }
    return bursts;
}

// ---------------------------------------------------------------------------
// Interception (VB Intercept): SQUARE kills of in-flight hostiles inside
// defensive explosions, plus smart-bomb evasion, plus the optional
// hostile-vs-hostile bomb chain (VB bExplodeb path). Only Status fields
// change, so pool iteration stays valid while objects retire next update.
// Returns the number of hostiles set exploding this pass.
// ---------------------------------------------------------------------------

inline int mc_intercept_list(McState* state, McProj* outerPool, int outerHead, McProj* innerPool,
                             int innerHead, bool isChain) {
    if (!state || !outerPool || !innerPool) return 0;
    (void)isChain;
    int kills = 0;
    int mLink = outerHead;
    int guard = 0;
    while (mLink != 0 && guard <= kMcPoolCap + 2) {
        ++guard;
        if (mLink < 1 || mLink > kMcPoolCap) break;
        McProj* mo = &outerPool[mLink];
        if (mo->status != 1) {
            int bLink = innerHead;
            int guard2 = 0;
            while (bLink != 0 && guard2 <= kMcPoolCap + 2) {
                ++guard2;
                if (bLink < 1 || bLink > kMcPoolCap) break;
                McProj* bi = &innerPool[bLink];
                if (bi->status == 1) {
                    if (mc_in_range(mo->x, mo->y, mo->status, bi->x, bi->y)) {
                        bi->status = 2;
                        ++kills;
                    } else if (bi->smart) {
                        // Smart-bomb evasion: flip xm when inside the wide
                        // (mMaxStatus*2) box and above the explosion.
                        if (mc_in_range(mo->x, mo->y, state->level.mMaxStatus * 2, bi->x, bi->y)) {
                            if (bi->y < mo->y) {
                                if (bi->xm > 0.0f) {
                                    if (bi->x < mo->x) bi->xm = -bi->xm;
                                } else {
                                    if (bi->x > mo->x) bi->xm = -bi->xm;
                                }
                            }
                        }
                    }
                }
                bLink = bi->link;
            }
        }
        mLink = mo->link;
    }
    return kills;
}

// One full DoIt interception step: defensive explosions vs hostiles, then
// the hostile bomb-chain when bExplodeb is set (L1 default True).
inline int mc_intercept_pass(McState* state) {
    if (!state) return 0;
    int kills = mc_intercept_list(state, state->m, state->mHead, state->b, state->bHead, false);
    if (state->level.bExplodeb) {
        kills += mc_intercept_list(state, state->b, state->bHead, state->b, state->bHead, true);
    }
    return kills;
}

// ---------------------------------------------------------------------------
// Win/lose evaluation (VB DoIt tail): lose when every city is dead;
// otherwise win when the hostile quota is exhausted AND the hostile pool
// has drained. Lose takes precedence (the original's stale-blnWon edge
// can never report a win over rubble).
// ---------------------------------------------------------------------------

inline void mc_evaluate_outcome(McState* state) {
    if (!state || state->won || state->lost) return;
    bool anyAlive = false;
    for (int i = 1; i <= kMcCitySlotCount; ++i) {
        if (state->targets[i]) {
            anyAlive = true;
            break;
        }
    }
    if (!anyAlive) {
        state->lost = true;
        state->visualDirty = true;
        return;
    }
    if (state->bDropped >= state->level.bMax && state->bHead == 0) {
        state->won = true;
        state->visualDirty = true;
    }
}

// ---------------------------------------------------------------------------
// One deterministic gameplay tick (VB DoIt loop body, fixed-step form).
// Ordering matches the original: LaunchB every tick while quota/cap allow,
// defensive fire on the consumed latch, MyShow hostiles then defense,
// Intercept (+ bomb chain), then win/lose evaluation. Terminal states
// freeze the simulation (the original exits the loop instead).
// ---------------------------------------------------------------------------

inline void mc_fixed_update(McState* state) {
    if (!state || !state->running) return;
    if (state->won || state->lost) return;
    ++state->simulationSteps;

    bool fire = state->pendingFire;
    int fx = state->fireX;
    int fy = state->fireY;
    state->pendingFire = false;

    mc_launch_b(state, false, 0.0f, 0.0f);
    if (fire) mc_launch_m(state, fx, fy);

    mc_myshow_hostiles(state);
    mc_myshow_defense(state);

    mc_intercept_pass(state);
    mc_evaluate_outcome(state);

    state->visualDirty = true;
}

// ---------------------------------------------------------------------------
// Input routing
// ---------------------------------------------------------------------------

// Pointer press in VB units (main.cpp converts window pixels first).
// VB latch semantics: overwrites any unconsumed press; at most one shot
// per tick. Ignored (not queued) after win/lose so post-game input cannot
// corrupt the terminal state.
inline bool mc_request_fire(McState* state, int vbX, int vbY) {
    if (!state || !state->running || state->won || state->lost) return false;
    if (vbX < 0) vbX = 0;
    if (vbX > kMcVbMaxX) vbX = kMcVbMaxX;
    if (vbY < 0) vbY = 0;
    if (vbY > kMcVbMaxY) vbY = kMcVbMaxY;
    state->fireX = vbX;
    state->fireY = vbY;
    state->pendingFire = true;
    ++state->clickCount;
    state->visualDirty = true;
    return true;
}

// Key routing. Escape stops (returns false); restart keys reset a finished
// level (returns true). Restart mid-game is ignored so a stray keypress
// cannot wipe a live defense.
inline bool mc_handle_key(McState* state, int keyCode, int action) {
    if (!state) return true;
    if (action == kMcKeyActionDown) {
        state->lastKeyCode = keyCode;
        state->visualDirty = true;
        if (keyCode == kMcKeyEscape) {
            state->running = false;
            return false;
        }
        if (keyCode == kMcKeyRestartR || keyCode == kMcKeyRestartEnter ||
            keyCode == kMcKeyRestartSpace) {
            if (state->won || state->lost) mc_reset_level(state);
        }
    }
    return true;
}

inline void mc_request_close(McState* state) {
    if (!state) return;
    state->running = false;
    state->visualDirty = true;
}
