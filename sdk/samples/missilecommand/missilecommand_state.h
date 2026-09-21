#pragma once

// Missile Command MC3 campaign: L1 -> L2 -> L3 progression with runtime
// DD.ini, natural smart bombs, and VB-faithful split quota accounting.
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
//   MachineSpeed/InitLevels L1-L3 -> kMcL1*/kMcL2*/kMcL3* + McCampaignConfig
//   GetINI     -> mc_parse_dd_ini (freestanding, bounded, no Win32 INI APIs)
//   DoIt level loop -> levelIndex/gameComplete + mc_advance_level
//                (cities persist across levels, like the original: VB
//                ResetTargets redraws but never revives Targets())
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
// L1-L3 configuration, recovered from DD.ini l1-l3 +
// global tuning, scaled by SyncFactor = (500*0.05)/1 = 25.
//
// DD.ini l1: bMax=10, mMax=50, bDrop=5, mFire=5, bSpeed=0.05,
//            Smart=0%, Split=15%, Name "Slow and Dumb I"
// DD.ini l2: bMax=10, mMax=50, bDrop=5, mFire=10, bSpeed=0.05,
//            Smart=0%, Split=20%, Name "Slow and Dumb II"
// DD.ini l3: bMax=10, mMax=50, bDrop=5, mFire=10, bSpeed=0.07,
//            Smart=30%, Split=25%, Name "Faster and Smarter I"
// Globals : mSpeed=1.5, mRadius(mMaxStatus)=25, bRadius(bMaxStatus)=35,
//           Cities(MaxTarget)=10, bExplodeb=True
// Scaled  : L1 bSpeed 0.05*25 = 1.25 px/tick, L2 1.25, L3 1.75;
//           mSpeed 1.5*25 = 37.5 px/tick (all levels)
// Field semantics (traced through source, not inferred from names):
//   bMax   bomb quota: normal spawns allowed while bDropped < bMax.
//           Split children do NOT consume it (VB passes mFired% as the
//           Droped% argument to the bomb-pool MyShow, so the child
//           increments the missile counter; preserved quirk).
//           Pool-growth (free list exhausted at split) raises bMax by 1.
//   mMax   missile quota: defensive launches allowed while mFired < mMax.
//           Split children increment mFired (quirk above), so heavy MIRV
//           activity can exhaust defensive ammunition early.
//   bDrop  hostile in-flight pool size (BuildPool free list length).
//   mFire  defensive in-flight pool size.
//   bSpeed raw hostile speed; runtime velocity is bSpeed*SyncFactor*(1+Rnd).
//   mSpeed global raw defensive speed; runtime is mSpeed*SyncFactor.
//   Smart  % of bombs (including split children, fresh roll each) flagged
//           smart at spawn: PickRandom(1,100) <= Smart.
//   Split  % of bombs (including children) assigned a SplitY in
//           [y, 600] when y < 600 at spawn.
//   Name   level caption only (no gameplay effect).
//   mMaxStatus/bMaxStatus blast growth caps (city hits double bMaxStatus).
//   bExplodeb global: when true, hostile-vs-hostile chain runs.
//   SyncFactor scales bSpeed and mSpeed (MachineSpeed/InitLevels).
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

static const int kMcL2BombQuota = 10;
static const int kMcL2MissileQuota = 50;
static const int kMcL2BombInflightCap = 5;
static const int kMcL2MissileInflightCap = 10;
static const float kMcL2BombSpeed = 1.25f;
static const float kMcL2MissileSpeed = 37.5f;
static const int kMcL2SmartPercent = 0;
static const int kMcL2SplitPercent = 20;

static const int kMcL3BombQuota = 10;
static const int kMcL3MissileQuota = 50;
static const int kMcL3BombInflightCap = 5;
static const int kMcL3MissileInflightCap = 10;
static const float kMcL3BombSpeed = 1.75f;
static const float kMcL3MissileSpeed = 37.5f;
static const int kMcL3SmartPercent = 30;
static const int kMcL3SplitPercent = 25;

// Campaign: genuine L1 -> L2 -> L3 progression. After L3 a temporary
// GAME COMPLETE terminal state is reported (full L4-L10 deferred to MC4).
static const int kMcCampaignLevels = 3;
static const int kMcMaxLevels = 10;
// Level-complete dwell: won freezes gameplay for 40 ticks (2s at 20Hz)
// so the LEVEL COMPLETE state is visible, then auto-advances.
// Deterministic: counted in fixed-step ticks, never wall clock.
static const int kMcLevelCompleteDelayTicks = 40;

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

// ---------------------------------------------------------------------------
// Campaign configuration (DD.ini L1-L10 + globals, freestanding-safe)
// ---------------------------------------------------------------------------

// Raw per-level row (bSpeed unscaled; scaled at apply time by SyncFactor).
struct McLevelDef {
    int bMax;
    int mMax;
    int bDrop;
    int mFire;
    float bSpeedRaw;
    int smart;
    int split;
    char name[48];
};

struct McGlobals {
    float syncDelay;
    float syncDist;
    float syncTime;
    float mSpeedRaw;
    int mMaxStatus;
    int bMaxStatus;
    int maxTarget;
    bool bExplodeb;
};

struct McCampaignConfig {
    McLevelDef levels[kMcMaxLevels + 1];  // 1-based; [0] unused
    McGlobals globals;
    float syncFactor;
};

struct McState {
    uint64_t simulationSteps;
    uint32_t rng;  // deterministic LCG state

    McLevelRuntime level;
    McCampaignConfig campaign;

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

    // Campaign progression (VB DoIt For l% = 1 To MaxLevel loop).
    // levelIndex is the current 1-based level (1..kMcCampaignLevels).
    // won = current level complete, pending auto-advance after
    // kMcLevelCompleteDelayTicks. gameComplete = L3 won and advanced
    // (temporary MC3 campaign terminal). lost = all cities dead (whole
    // campaign over, like VB blnLost exiting the For loop).
    int levelIndex;
    bool gameComplete;
    int levelCompleteTicks;

    bool won;
    bool lost;
    bool running;
    bool visualDirty;
    bool usingRuntimeIni;  // true when DD.ini was loaded at runtime

    int clickCount;   // fire requests received (diagnostic/status)
    int lastKeyCode;  // last key seen (diagnostic/status)
    int listGuardTrips;  // cross-linked-list guard trips (VB Stop equivalent)
    int evadeCount;  // smart-bomb evasion flips observed (diagnostic)
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
// Pointer translation (VB Pic_MouseDown/Pic_MouseMove -> MouseRead).
// VB MouseRead only fires on button 1 (left). FrmDD synthesizes a left
// press when the pointer moves with Button=2 held, so:
//   left DOWN fires, right MOVE (drag) fires, right DOWN alone does not.
// Action codes match the guideXOS ABI: 0=move, 1=down, 2=up.
// Button codes: 0=none, 1=left, 2=right, 3=middle.
// ---------------------------------------------------------------------------

static const int kMcActionMove = 0;
static const int kMcActionDown = 1;
static const int kMcActionUp = 2;
static const int kMcButtonNone = 0;
static const int kMcButtonLeft = 1;
static const int kMcButtonRight = 2;
static const int kMcButtonMiddle = 3;

inline bool mc_should_fire(int action, int button) {
    if (action == kMcActionDown && button == kMcButtonLeft) return true;
    if (action == kMcActionMove && button == kMcButtonRight) return true;
    return false;
}

// ---------------------------------------------------------------------------
// DD.ini campaign data (freestanding: no libc, no Win32 INI APIs).
// Minimal line parser: handles [DD], REM/comments, l1..l10 rows of the
// form bMax,mMax,bDrop,mFire,bSpeed,Smart,Split,"Name", and scalar globals.
// Bounds follow Module1.bas GetINI where it validates; level rows with any
// missing/invalid/out-of-range field keep the fallback row (never crash).
// ---------------------------------------------------------------------------

inline void mc_copy_name(char* dst, const char* src, int cap) {
    if (!dst || cap <= 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    int i = 0;
    while (i + 1 < cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

inline void mc_set_level_name(McLevelDef* def, const char* text, int len) {
    if (!def) return;
    // Strip surrounding whitespace and one pair of double quotes.
    int s = 0;
    int e = len;
    while (s < e && (text[s] == ' ' || text[s] == '\t' || text[s] == '\r')) ++s;
    while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r')) --e;
    if (e - s >= 2 && text[s] == '"' && text[e - 1] == '"') {
        ++s;
        --e;
    }
    int n = e - s;
    if (n < 0) n = 0;
    if (n > 47) n = 47;
    for (int i = 0; i < n; ++i) def->name[i] = text[s + i];
    def->name[n] = '\0';
    if (n == 0) {
        // Keep a visible placeholder rather than an empty caption.
        mc_copy_name(def->name, "Level", 48);
    }
}

inline bool mc_is_digit(char c) { return c >= '0' && c <= '9'; }

inline bool mc_parse_int_n(const char* text, int len, int* out) {
    if (!text || len <= 0 || !out) return false;
    int i = 0;
    while (i < len && (text[i] == ' ' || text[i] == '\t')) ++i;
    bool neg = false;
    if (i < len && (text[i] == '+' || text[i] == '-')) {
        neg = (text[i] == '-');
        ++i;
    }
    if (i >= len || !mc_is_digit(text[i])) return false;
    long acc = 0;
    int digits = 0;
    while (i < len && mc_is_digit(text[i])) {
        acc = acc * 10 + (text[i] - '0');
        ++i;
        ++digits;
        if (acc > 1000000L) break;
    }
    while (i < len && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i != len) return false;
    if (digits == 0) return false;
    if (neg) acc = -acc;
    if (acc > 2000000000L || acc < -2000000000L) return false;
    *out = (int)acc;
    return true;
}

inline bool mc_parse_float_n(const char* text, int len, float* out) {
    if (!text || len <= 0 || !out) return false;
    int i = 0;
    while (i < len && (text[i] == ' ' || text[i] == '\t')) ++i;
    bool neg = false;
    if (i < len && (text[i] == '+' || text[i] == '-')) {
        neg = (text[i] == '-');
        ++i;
    }
    double intPart = 0.0;
    int digits = 0;
    while (i < len && mc_is_digit(text[i])) {
        intPart = intPart * 10.0 + (text[i] - '0');
        ++i;
        ++digits;
    }
    double fracPart = 0.0;
    double fracDiv = 1.0;
    if (i < len && text[i] == '.') {
        ++i;
        while (i < len && mc_is_digit(text[i])) {
            fracPart = fracPart * 10.0 + (text[i] - '0');
            fracDiv *= 10.0;
            ++i;
            ++digits;
        }
    }
    if (digits == 0) return false;
    while (i < len && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i != len) return false;
    double v = intPart + fracPart / fracDiv;
    if (neg) v = -v;
    *out = (float)v;
    return true;
}

inline bool mc_key_eq_n(const char* text, int len, const char* key) {
    if (!text || !key) return false;
    int i = 0;
    while (i < len && (text[i] == ' ' || text[i] == '\t')) ++i;
    int k = 0;
    while (key[k] != '\0') {
        if (i >= len) return false;
        char a = text[i];
        char b = key[k];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
        ++i;
        ++k;
    }
    while (i < len && (text[i] == ' ' || text[i] == '\t')) ++i;
    return i == len;
}

// Deterministic fallback campaign: verified GetINI defaults, which match
// the shipped DD.ini for L1-L10 and globals.
inline void mc_fallback_campaign(McCampaignConfig* cfg) {
    if (!cfg) return;
    static const int bMaxV[11] = {0, 10, 10, 10, 15, 15, 20, 30, 40, 50, 100};
    static const int mMaxV[11] = {0, 50, 50, 50, 100, 100, 200, 200, 200, 200, 200};
    static const int bDropV[11] = {0, 5, 5, 5, 10, 10, 10, 10, 20, 50, 50};
    static const int mFireV[11] = {0, 5, 10, 10, 20, 20, 20, 20, 30, 50, 50};
    static const float bSpdV[11] = {0.0f, 0.05f, 0.05f, 0.07f, 0.09f, 0.09f,
                                    0.12f, 0.12f, 0.12f, 0.12f, 0.15f};
    static const int smartV[11] = {0, 0, 0, 30, 40, 50, 50, 60, 70, 80, 90};
    static const int splitV[11] = {0, 15, 20, 25, 25, 25, 33, 33, 33, 33, 50};
    static const char* nameV[11] = {"", "Slow and Dumb I", "Slow and Dumb II",
        "Faster and Smarter I", "Faster and Smarter II", "Faster and Smarter III",
        "Prelude", "Dooms Day I", "Dooms Day II", "You've got to be kidding!",
        "This ain't right"};
    for (int i = 1; i <= kMcMaxLevels; ++i) {
        cfg->levels[i].bMax = bMaxV[i];
        cfg->levels[i].mMax = mMaxV[i];
        cfg->levels[i].bDrop = bDropV[i];
        cfg->levels[i].mFire = mFireV[i];
        cfg->levels[i].bSpeedRaw = bSpdV[i];
        cfg->levels[i].smart = smartV[i];
        cfg->levels[i].split = splitV[i];
        mc_copy_name(cfg->levels[i].name, nameV[i], 48);
    }
    cfg->globals.syncDelay = 0.05f;
    cfg->globals.syncDist = 500.0f;
    cfg->globals.syncTime = 1.0f;
    cfg->globals.mSpeedRaw = 1.5f;
    cfg->globals.mMaxStatus = 25;
    cfg->globals.bMaxStatus = 35;
    cfg->globals.maxTarget = 10;
    cfg->globals.bExplodeb = true;
    cfg->syncFactor = (cfg->globals.syncDist * cfg->globals.syncDelay) / cfg->globals.syncTime;
}

// Parse one level row value (after "lN="): 8 comma fields.
// Returns true only when every gameplay field validates; the name is
// accepted leniently (quoted or bare) and never fails the row.
inline bool mc_parse_level_row(const char* text, int len, McLevelDef* out) {
    if (!text || len <= 0 || !out) return false;
    // Split on commas at depth 0 (names are quoted; commas inside quotes
    // must not split). L1-L10 names contain no commas, but be safe.
    const char* f[8];
    int fl[8];
    int count = 0;
    int start = 0;
    bool inQuotes = false;
    for (int i = 0; i <= len; ++i) {
        char c = (i < len) ? text[i] : ',';
        if (c == '"') inQuotes = !inQuotes;
        if ((c == ',' && !inQuotes) || i == len) {
            if (count < 8) {
                f[count] = text + start;
                fl[count] = i - start;
                ++count;
            }
            start = i + 1;
        }
    }
    // Trailing loop above yields exactly field count; require 8.
    if (count != 8) return false;
    int bMax = 0, mMax = 0, bDrop = 0, mFire = 0, smart = 0, split = 0;
    float bSpeed = 0.0f;
    if (!mc_parse_int_n(f[0], fl[0], &bMax)) return false;
    if (!mc_parse_int_n(f[1], fl[1], &mMax)) return false;
    if (!mc_parse_int_n(f[2], fl[2], &bDrop)) return false;
    if (!mc_parse_int_n(f[3], fl[3], &mFire)) return false;
    if (!mc_parse_float_n(f[4], fl[4], &bSpeed)) return false;
    if (!mc_parse_int_n(f[5], fl[5], &smart)) return false;
    if (!mc_parse_int_n(f[6], fl[6], &split)) return false;
    if (bMax < 1 || bMax > kMcPoolCap) return false;
    if (mMax < 1 || mMax > kMcPoolCap) return false;
    if (bDrop < 1 || bDrop > kMcPoolCap) return false;
    if (mFire < 1 || mFire > kMcPoolCap) return false;
    if (!(bSpeed > 0.0f && bSpeed <= 5.0f)) return false;
    if (smart < 0 || smart > 100) return false;
    if (split < 0 || split > 100) return false;
    out->bMax = bMax;
    out->mMax = mMax;
    out->bDrop = bDrop;
    out->mFire = mFire;
    out->bSpeedRaw = bSpeed;
    out->smart = smart;
    out->split = split;
    mc_set_level_name(out, f[7], fl[7]);
    return true;
}

inline bool mc_parse_bool_value(const char* text, int len, bool* out) {
    if (!text || !out) return false;
    // Trim.
    int s = 0;
    int e = len;
    while (s < e && (text[s] == ' ' || text[s] == '\t' || text[s] == '\r')) ++s;
    while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\r')) --e;
    int n = e - s;
    if (n == 1 && (text[s] == '1' || text[s] == '0')) {
        *out = (text[s] == '1');
        return true;
    }
    if (n == 4) {
        char a = text[s], b = text[s + 1], c = text[s + 2], d = text[s + 3];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (d >= 'A' && d <= 'Z') d = (char)(d + 32);
        if (a == 't' && b == 'r' && c == 'u' && d == 'e') {
            *out = true;
            return true;
        }
    }
    if (n == 5) {
        char a = text[s], b = text[s + 1], c = text[s + 2], d = text[s + 3], e2 = text[s + 4];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (d >= 'A' && d <= 'Z') d = (char)(d + 32);
        if (e2 >= 'A' && e2 <= 'Z') e2 = (char)(e2 + 32);
        if (a == 'f' && b == 'a' && c == 'l' && d == 's' && e2 == 'e') {
            *out = false;
            return true;
        }
    }
    return false;
}

// Parse full DD.ini text into cfg (starting from fallback; invalid lines
// keep fallback values). Returns the number of level rows overridden.
inline int mc_parse_dd_ini(const char* text, uint32_t len, McCampaignConfig* cfg) {
    if (!cfg) return 0;
    mc_fallback_campaign(cfg);
    if (!text || len == 0) return 0;
    int rowsOk = 0;
    bool inDD = false;
    uint32_t pos = 0;
    while (pos < len) {
        uint32_t eol = pos;
        while (eol < len && text[eol] != '\n') ++eol;
        int ll = (int)(eol - pos);
        while (ll > 0 && (text[pos + ll - 1] == '\r' || text[pos + ll - 1] == '\n')) --ll;
        int s = 0;
        while (s < ll && (text[pos + s] == ' ' || text[pos + s] == '\t')) ++s;
        if (s < ll && text[pos + s] != ';') {
            if (text[pos + s] == '[') {
                int ce = s + 1;
                while (ce < ll && text[pos + ce] != ']') ++ce;
                if (ce < ll) {
                    int nl = ce - (s + 1);
                    if (nl == 2) {
                        char a = text[pos + s + 1], b = text[pos + s + 2];
                        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                        inDD = (a == 'd' && b == 'd');
                    } else {
                        inDD = false;
                    }
                }
            } else if (inDD) {
                // key = value
                int eq = s;
                while (eq < ll && text[pos + eq] != '=') ++eq;
                if (eq < ll) {
                    int kl = eq - s;
                    while (kl > 0 && (text[pos + s + kl - 1] == ' ' || text[pos + s + kl - 1] == '\t')) --kl;
                    const char* kp = text + pos + s;
                    int vs = eq + 1;
                    while (vs < ll && (text[pos + vs] == ' ' || text[pos + vs] == '\t')) ++vs;
                    int vl = ll - vs;
                    const char* vp = text + pos + vs;
                    // Level rows l1..l10.
                    if (kl == 2 && (kp[0] == 'l' || kp[0] == 'L') && kp[1] >= '1' && kp[1] <= '9') {
                        int idx = kp[0 + 1] - '0';
                        // l10 is two chars.
                        if (kl > 2) {
                            // Not expected; ignore.
                        } else {
                            McLevelDef tmp = cfg->levels[idx];
                            if (mc_parse_level_row(vp, vl, &tmp)) {
                                cfg->levels[idx] = tmp;
                                ++rowsOk;
                            }
                        }
                    } else if (kl == 3 && (kp[0] == 'l' || kp[0] == 'L') && kp[1] == '1' && kp[2] == '0') {
                        McLevelDef tmp = cfg->levels[10];
                        if (mc_parse_level_row(vp, vl, &tmp)) {
                            cfg->levels[10] = tmp;
                            ++rowsOk;
                        }
                    } else if (mc_key_eq_n(kp, kl, "sound")) {
                        bool b = false;
                        if (mc_parse_bool_value(vp, vl, &b)) {
                            (void)b;  // audio out of scope; parsed + ignored
                        }
                    } else if (mc_key_eq_n(kp, kl, "cities")) {
                        int v = 0;
                        if (mc_parse_int_n(vp, vl, &v) && v >= 5 && v <= 20) cfg->globals.maxTarget = v;
                    } else if (mc_key_eq_n(kp, kl, "syncdelay")) {
                        float v = 0.0f;
                        if (mc_parse_float_n(vp, vl, &v) && v > 0.0f && v <= 1.0f) cfg->globals.syncDelay = v;
                    } else if (mc_key_eq_n(kp, kl, "syncdist")) {
                        float v = 0.0f;
                        if (mc_parse_float_n(vp, vl, &v) && v > 0.0f && v <= 5000.0f) cfg->globals.syncDist = v;
                    } else if (mc_key_eq_n(kp, kl, "synctime")) {
                        float v = 0.0f;
                        if (mc_parse_float_n(vp, vl, &v) && v > 0.0f && v <= 10.0f) cfg->globals.syncTime = v;
                    } else if (mc_key_eq_n(kp, kl, "mradius")) {
                        int v = 0;
                        if (mc_parse_int_n(vp, vl, &v) && v >= 5 && v <= 50) cfg->globals.mMaxStatus = v;
                    } else if (mc_key_eq_n(kp, kl, "bradius")) {
                        int v = 0;
                        if (mc_parse_int_n(vp, vl, &v) && v >= 5 && v <= 50) cfg->globals.bMaxStatus = v;
                    } else if (mc_key_eq_n(kp, kl, "mspeed")) {
                        float v = 0.0f;
                        if (mc_parse_float_n(vp, vl, &v) && v >= 0.1f && v <= 5.0f) cfg->globals.mSpeedRaw = v;
                    } else if (mc_key_eq_n(kp, kl, "bexplodeb")) {
                        bool b = false;
                        if (mc_parse_bool_value(vp, vl, &b)) cfg->globals.bExplodeb = b;
                    } else {
                        // REM and unknown keys: ignored (REM documents columns).
                    }
                }
            }
        }
        pos = (eol < len) ? (eol + 1) : len;
    }
    if (cfg->globals.syncTime == 0.0f) cfg->globals.syncTime = 1.0f;
    cfg->syncFactor = (cfg->globals.syncDist * cfg->globals.syncDelay) / cfg->globals.syncTime;
    if (!(cfg->syncFactor > 0.0f) || cfg->syncFactor > 1000.0f) {
        cfg->globals.syncDelay = 0.05f;
        cfg->globals.syncDist = 500.0f;
        cfg->globals.syncTime = 1.0f;
        cfg->syncFactor = 25.0f;
    }
    return rowsOk;
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

// Apply campaign level `index` (1-based, clamped to 1..kMcCampaignLevels)
// to the mutable runtime. Scaling follows InitLevels:
// bSpeed = raw*SyncFactor, mSpeed = globals.mSpeedRaw*SyncFactor.
inline void mc_apply_level(McState* state, int index) {
    if (!state) return;
    if (index < 1) index = 1;
    if (index > kMcCampaignLevels) index = kMcCampaignLevels;
    const McLevelDef* def = &state->campaign.levels[index];
    state->level.bMax = def->bMax;
    state->level.mMax = def->mMax;
    state->level.bDrop = def->bDrop;
    state->level.mFire = def->mFire;
    state->level.bSpeed = def->bSpeedRaw * state->campaign.syncFactor;
    state->level.mSpeed = state->campaign.globals.mSpeedRaw * state->campaign.syncFactor;
    state->level.smart = def->smart;
    state->level.split = def->split;
    state->level.bMaxStatus = state->campaign.globals.bMaxStatus;
    state->level.mMaxStatus = state->campaign.globals.mMaxStatus;
    state->level.bExplodeb = state->campaign.globals.bExplodeb;
}

inline void mc_ensure_campaign(McState* state) {
    if (!state) return;
    const McLevelDef* l1 = &state->campaign.levels[1];
    if (l1->bMax < 1 || l1->bMax > kMcPoolCap || l1->mMax < 1 || l1->mMax > kMcPoolCap ||
        !(state->campaign.syncFactor > 0.0f) || state->campaign.syncFactor > 1000.0f) {
        McCampaignConfig fb;
        mc_fallback_campaign(&fb);
        state->campaign = fb;
    }
    if (state->levelIndex < 1 || state->levelIndex > kMcCampaignLevels) state->levelIndex = 1;
}

inline void mc_apply_l1(McState* state) {
    if (!state) return;
    // Compatibility shim: L1 of the current campaign (fallback when the
    // campaign was never loaded, e.g. legacy callers).
    mc_ensure_campaign(state);
    int saved = state->levelIndex;
    if (saved < 1 || saved > kMcCampaignLevels) saved = 1;
    // mc_apply_l1 historically meant "L1"; keep that meaning for callers
    // that expect L1 values (MC2 tests start at L1).
    mc_apply_level(state, 1);
    state->levelIndex = saved;
}

inline void mc_clear_pools(McState* state) {
    if (!state) return;
    for (int i = 0; i <= kMcPoolCap; ++i) {
        mc_null_proj(&state->b[i]);
        state->b[i].link = 0;
        mc_null_proj(&state->m[i]);
        state->m[i].link = 0;
    }
    mc_build_pool(state->b, kMcPoolCap, &state->bHead, &state->bPool, state->level.bDrop);
    mc_build_pool(state->m, kMcPoolCap, &state->mHead, &state->mPool, state->level.mFire);
}

// Full campaign init with an explicit config (runtime DD.ini or fallback).
inline void mc_init_campaign_with_seed(McState* state, uint32_t seed,
                                        const McCampaignConfig* cfg, bool usingRuntime) {
    if (!state) return;
    state->simulationSteps = 0u;
    state->rng = seed;
    if (cfg) {
        state->campaign = *cfg;
    } else {
        mc_fallback_campaign(&state->campaign);
    }
    state->levelIndex = 1;
    state->gameComplete = false;
    state->levelCompleteTicks = 0;
    state->usingRuntimeIni = usingRuntime;
    mc_apply_level(state, 1);
    mc_clear_pools(state);
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
    state->evadeCount = 0;
}

inline void mc_init_with_seed(McState* state, uint32_t seed) {
    if (!state) return;
    McCampaignConfig fb;
    mc_fallback_campaign(&fb);
    mc_init_campaign_with_seed(state, seed, &fb, false);
}

inline void mc_init(McState* state) { mc_init_with_seed(state, kMcDefaultSeed); }

// Explicit level advancement (VB DoIt For-loop step). Requires won (level
// complete) and no terminal state. Cities persist (VB ResetTargets never
// revives); pools/counters are rebuilt for the next level; the RNG stream
// continues (VB Randomize runs once per DoIt, not per level).
// Returns true when the transition happened. After the final campaign
// level, sets gameComplete instead of advancing.
inline bool mc_advance_level(McState* state) {
    if (!state) return false;
    if (!state->won || state->lost || state->gameComplete || !state->running) return false;
    if (state->levelIndex >= kMcCampaignLevels) {
        state->won = false;
        state->gameComplete = true;
        state->levelCompleteTicks = 0;
        state->pendingFire = false;
        state->visualDirty = true;
        return true;
    }
    state->levelIndex += 1;
    state->won = false;
    state->levelCompleteTicks = 0;
    state->pendingFire = false;
    mc_apply_level(state, state->levelIndex);
    mc_clear_pools(state);
    state->bDropped = 0;
    state->mFired = 0;
    // Targets intentionally NOT reset: destroyed cities stay destroyed.
    state->visualDirty = true;
    return true;
}

// Full campaign restart (File > Play / R after a terminal state).
// Returns to L1 with cities revived, quotas/pools fresh, outcome cleared.
// RNG continues (matches mc_reset_level MC2 semantics).
inline void mc_reset_campaign(McState* state) {
    if (!state) return;
    state->levelIndex = 1;
    state->gameComplete = false;
    state->levelCompleteTicks = 0;
    mc_apply_level(state, 1);
    mc_clear_pools(state);
    mc_reset_targets(state);
    state->bDropped = 0;
    state->mFired = 0;
    state->pendingFire = false;
    state->won = false;
    state->lost = false;
    state->evadeCount = 0;
    state->visualDirty = true;
}

// Clean level reset (restart key after win/lose). Restores the CURRENT
// level quota (Level.bMax may have grown through split pool-growth),
// rebuilds the in-flight pools, revives all cities, clears the outcome.
// The RNG stream is deliberately NOT reseeded so replays do not repeat
// identically (the original re-randomized via Randomize Timer each DoIt).
// Campaign fields are also cleared so a stale gameComplete cannot survive.
inline void mc_reset_level(McState* state) {
    if (!state) return;
    mc_ensure_campaign(state);
    mc_apply_level(state, (state->levelIndex >= 1 && state->levelIndex <= kMcCampaignLevels)
                               ? state->levelIndex
                               : 1);
    mc_clear_pools(state);
    mc_reset_targets(state);
    state->bDropped = 0;
    state->mFired = 0;
    state->pendingFire = false;
    state->won = false;
    state->lost = false;
    state->gameComplete = false;
    state->levelCompleteTicks = 0;
    state->evadeCount = 0;
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
    if (state->won || state->lost || state->gameComplete || !state->running) return 0;
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
    if (state->won || state->lost || state->gameComplete || !state->running) return 0;
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
                // VB QUIRK (preserved): DoIt calls the bomb-pool MyShow with
                // mFired% as the Droped% argument, so the split child
                // increments the MISSILE counter, not the bomb counter.
                // mc_launch_b charges bDropped; transfer the charge here so
                // bDropped is untouched and mFired grows, exactly like VB.
                p->splitY = 0;
                if (state->bPool == 0 && state->level.bMax < kMcPoolCap) {
                    state->level.bMax += 1;
                    state->bPool = state->level.bMax;
                    mc_null_proj(&state->b[state->bPool]);
                    state->b[state->bPool].link = 0;
                }
                int droppedBefore = state->bDropped;
                int child = mc_launch_b(state, true, p->x, p->y);
                if (child != 0 && state->bDropped == droppedBefore + 1) {
                    state->bDropped = droppedBefore;
                    state->mFired += 1;
                }
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
                                    if (bi->x < mo->x) {
                                        bi->xm = -bi->xm;
                                        state->evadeCount += 1;
                                    }
                                } else {
                                    if (bi->x > mo->x) {
                                        bi->xm = -bi->xm;
                                        state->evadeCount += 1;
                                    }
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
    if (!state || state->won || state->lost || state->gameComplete) return;
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
        state->levelCompleteTicks = 0;
        state->visualDirty = true;
    }
}

// ---------------------------------------------------------------------------
// One deterministic gameplay tick (VB DoIt loop body, fixed-step form).
// Ordering matches the original: LaunchB every tick while quota/cap allow,
// defensive fire on the consumed latch, MyShow hostiles then defense,
// Intercept (+ bomb chain), then win/lose evaluation. Terminal states
// freeze the simulation (the original exits the loop instead).
// Level-complete (won) freezes gameplay for kMcLevelCompleteDelayTicks
// then auto-advances via mc_advance_level (L1->L2->L3, then GAME COMPLETE).
// The delay is counted in fixed-step ticks, never wall clock.
// ---------------------------------------------------------------------------

inline void mc_fixed_update(McState* state) {
    if (!state || !state->running) return;
    if (state->lost || state->gameComplete) return;
    if (state->won) {
        ++state->simulationSteps;
        ++state->levelCompleteTicks;
        state->visualDirty = true;
        if (state->levelCompleteTicks >= kMcLevelCompleteDelayTicks) {
            mc_advance_level(state);
        }
        return;
    }
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
// per tick. Ignored (not queued) after win/lose/game-complete so post-game
// input cannot corrupt the terminal state. Level-complete dwell also
// ignores fire so input cannot cause a double transition.
inline bool mc_request_fire(McState* state, int vbX, int vbY) {
    if (!state || !state->running || state->won || state->lost || state->gameComplete) return false;
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
// campaign (returns true). Restart mid-game and during the level-complete
// dwell is ignored so a stray keypress cannot wipe a live defense or cause
// a double transition.
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
            if (state->lost || state->gameComplete) mc_reset_campaign(state);
        }
    }
    return true;
}

inline void mc_request_close(McState* state) {
    if (!state) return;
    state->running = false;
    state->visualDirty = true;
}
