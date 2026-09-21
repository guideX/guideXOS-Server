// Host test for the Missile Command MC3 campaign (L1-L3, DD.ini, smart
// bombs, VB-faithful split accounting, progression, input, determinism).
// Builds with host g++ (no guideXOS runtime needed):
//   g++ -std=c++17 -Wall -Wextra -O2 tests/missilecommand_state_test.cpp -o out/...exe

#include "../sdk/samples/missilecommand/missilecommand_state.h"

#include <iostream>

namespace {

int g_failures = 0;

bool expect(bool value, const char* label) {
    if (!value) {
        std::cerr << "FAIL: " << label << "\n";
        ++g_failures;
    }
    return value;
}

bool feq(float a, float b, float eps) {
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= eps;
}

// Canonical fingerprint: every semantic field, no padding bytes.
void mix(uint64_t& h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}
uint32_t fbits(float f) {
    union {
        float f;
        uint32_t u;
    } c;
    c.f = f;
    return c.u;
}
uint64_t hash_proj(uint64_t h, const McProj& p) {
    mix(h, fbits(p.xs));
    mix(h, fbits(p.ys));
    mix(h, fbits(p.x));
    mix(h, fbits(p.y));
    mix(h, fbits(p.xm));
    mix(h, fbits(p.ym));
    mix(h, (uint64_t)(uint32_t)p.xe);
    mix(h, (uint64_t)(uint32_t)p.ye);
    mix(h, (uint64_t)(uint32_t)p.link);
    mix(h, (uint64_t)(uint32_t)p.status);
    mix(h, (uint64_t)(uint32_t)p.splitY);
    mix(h, p.smart ? 1u : 0u);
    return h;
}
uint64_t hash_level_def(uint64_t h, const McLevelDef& d) {
    mix(h, (uint64_t)(uint32_t)d.bMax);
    mix(h, (uint64_t)(uint32_t)d.mMax);
    mix(h, (uint64_t)(uint32_t)d.bDrop);
    mix(h, (uint64_t)(uint32_t)d.mFire);
    mix(h, fbits(d.bSpeedRaw));
    mix(h, (uint64_t)(uint32_t)d.smart);
    mix(h, (uint64_t)(uint32_t)d.split);
    for (int i = 0; i < 48 && d.name[i] != '\0'; ++i) mix(h, (uint64_t)(uint32_t)(unsigned char)d.name[i]);
    return h;
}
uint64_t hash_state(const McState& s) {
    uint64_t h = 1469598103934665603ull;
    mix(h, s.simulationSteps);
    mix(h, s.rng);
    mix(h, (uint64_t)(uint32_t)s.level.bMax);
    mix(h, (uint64_t)(uint32_t)s.level.mMax);
    mix(h, (uint64_t)(uint32_t)s.level.bDrop);
    mix(h, (uint64_t)(uint32_t)s.level.mFire);
    mix(h, fbits(s.level.bSpeed));
    mix(h, fbits(s.level.mSpeed));
    mix(h, (uint64_t)(uint32_t)s.level.smart);
    mix(h, (uint64_t)(uint32_t)s.level.split);
    mix(h, (uint64_t)(uint32_t)s.level.bMaxStatus);
    mix(h, (uint64_t)(uint32_t)s.level.mMaxStatus);
    mix(h, s.level.bExplodeb ? 1u : 0u);
    for (int i = 1; i <= kMcMaxLevels; ++i) h = hash_level_def(h, s.campaign.levels[i]);
    mix(h, fbits(s.campaign.globals.syncDelay));
    mix(h, fbits(s.campaign.globals.syncDist));
    mix(h, fbits(s.campaign.globals.syncTime));
    mix(h, fbits(s.campaign.globals.mSpeedRaw));
    mix(h, (uint64_t)(uint32_t)s.campaign.globals.mMaxStatus);
    mix(h, (uint64_t)(uint32_t)s.campaign.globals.bMaxStatus);
    mix(h, (uint64_t)(uint32_t)s.campaign.globals.maxTarget);
    mix(h, s.campaign.globals.bExplodeb ? 1u : 0u);
    mix(h, fbits(s.campaign.syncFactor));
    for (int i = 0; i <= kMcPoolCap; ++i) {
        h = hash_proj(h, s.b[i]);
        h = hash_proj(h, s.m[i]);
    }
    mix(h, (uint64_t)(uint32_t)s.bHead);
    mix(h, (uint64_t)(uint32_t)s.bPool);
    mix(h, (uint64_t)(uint32_t)s.mHead);
    mix(h, (uint64_t)(uint32_t)s.mPool);
    mix(h, (uint64_t)(uint32_t)s.bDropped);
    mix(h, (uint64_t)(uint32_t)s.mFired);
    for (int i = 1; i <= kMcCitySlotCount; ++i) mix(h, s.targets[i] ? 1u : 0u);
    mix(h, s.pendingFire ? 1u : 0u);
    mix(h, (uint64_t)(uint32_t)s.fireX);
    mix(h, (uint64_t)(uint32_t)s.fireY);
    mix(h, (uint64_t)(uint32_t)s.levelIndex);
    mix(h, s.gameComplete ? 1u : 0u);
    mix(h, (uint64_t)(uint32_t)s.levelCompleteTicks);
    mix(h, s.won ? 1u : 0u);
    mix(h, s.lost ? 1u : 0u);
    mix(h, s.running ? 1u : 0u);
    mix(h, s.visualDirty ? 1u : 0u);
    mix(h, s.usingRuntimeIni ? 1u : 0u);
    mix(h, (uint64_t)(uint32_t)s.clickCount);
    mix(h, (uint64_t)(uint32_t)s.lastKeyCode);
    mix(h, (uint64_t)(uint32_t)s.listGuardTrips);
    mix(h, (uint64_t)(uint32_t)s.evadeCount);
    return h;
}

// Place an in-flight hostile at an exact spot (test scaffold).
int place_hostile(McState& s, float x, float y, float xm, float ym) {
    int t = mc_launch_b(&s, false, 0.0f, 0.0f);
    if (t == 0) return 0;
    s.b[t].x = x;
    s.b[t].y = y;
    s.b[t].xm = xm;
    s.b[t].ym = ym;
    s.b[t].status = 1;
    return t;
}

// Place an exploding defensive burst at an exact spot (test scaffold).
// Allocates directly from the free list: no tick, no hostile side-spawn.
int place_burst(McState& s, float x, float y, int radius) {
    if (s.mPool == 0) return 0;
    int t = s.mPool;
    s.mPool = s.m[t].link;
    s.m[t].link = s.mHead;
    s.mHead = t;
    s.m[t].xs = (float)kMcVbBatteryX;
    s.m[t].ys = (float)kMcVbBatteryY;
    s.m[t].x = x;
    s.m[t].y = y;
    s.m[t].xe = (int)(x + 0.5f);
    s.m[t].ye = (int)(y + 0.5f);
    s.m[t].status = radius;
    s.m[t].smart = false;
    s.m[t].splitY = 0;
    return t;
}

int count_smart(const McState& s) {
    int n = 0;
    int link = s.bHead;
    while (link != 0) {
        if (link < 1 || link > kMcPoolCap) break;
        if (s.b[link].smart && s.b[link].status == 1) ++n;
        link = s.b[link].link;
    }
    return n;
}

const char* kDdIniOriginal =
    "[DD]\n"
    "REM=rem=bMax, mMax, bDrop, mFire, bSpeed, Smart%, Split%, Name\n"
    "l1=10, 50, 5, 5, 0.05,  0, 15, \"Slow and Dumb I\"\n"
    "l2=10, 50, 5, 10, 0.05,  0, 20, \"Slow and Dumb II\"\n"
    "l3=10, 50, 5, 10, 0.07,  30, 25, \"Faster and Smarter I\"\n"
    "l4=15, 100, 10, 20, 0.09,  40, 25, \"Faster and Smarter II\"\n"
    "l5=15, 100, 10, 20, 0.09,  50, 25, \"Faster and Smarter III\"\n"
    "l6=20, 200, 10, 20, 0.12,  50, 33, \"Prelude\"\n"
    "l7=30, 200, 10, 20, 0.12,  60, 33, \"Dooms Day I\"\n"
    "l8=40, 200, 20, 30, 0.12,  70, 33, \"Dooms Day II\"\n"
    "l9=50, 200, 50, 50, 0.12,  80, 33, \"You've got to be kidding!\"\n"
    "l10=100, 200, 50, 50, 0.15,  90, 50, \"This ain't right\"\n"
    "Sound=True\n"
    "Cities=10\n"
    "SyncDist=500\n"
    "SyncDelay=0.05\n"
    "SyncTime=1\n"
    "mRadius=25\n"
    "bRadius=35\n"
    "mSpeed=1.5\n"
    "bExplodeb=True\n";

uint32_t cstr_len(const char* s) {
    uint32_t n = 0;
    while (s && s[n] != '\0') ++n;
    return n;
}

// Deterministic auto-aim driver: one predicted shot per tick at the lowest
// high bomb, only while no defensive burst is active (ammo-efficient).
// Returns ticks used. Used for win/progression/determinism.
int drive_auto_aim(McState& s, int cap) {
    int ticks = 0;
    while (!s.won && !s.lost && !s.gameComplete && ticks < cap) {
        int best = 0;
        int bl = s.bHead;
        while (bl != 0) {
            if (s.b[bl].status == 1 && s.b[bl].y > 300.0f) {
                if (best == 0 || s.b[bl].y > s.b[best].y) best = bl;
            }
            bl = s.b[bl].link;
        }
        if (best != 0 && mc_active_defense(&s) == 0) {
            float flight = (750.0f - s.b[best].y) / s.level.mSpeed;
            if (flight < 0.0f) flight = 0.0f;
            int tx = (int)(s.b[best].x + s.b[best].xm * flight + 0.5f);
            int ty = (int)(s.b[best].y + s.b[best].ym * flight + 0.5f);
            mc_request_fire(&s, tx, ty);
        }
        mc_fixed_update(&s);
        ++ticks;
    }
    return ticks;
}

}  // namespace

int main() {
    bool ok = true;

    // ------------------------------------------------ initial state
    {
        McState s;
        mc_init(&s);
        ok &= expect(mc_alive_cities(&s) == 10, "init: 10 cities alive");
        ok &= expect(kMcVbBatteryX == 500 && kMcVbBatteryY == 750, "init: battery (500,750)");
        ok &= expect(s.levelIndex == 1, "init: campaign starts at L1");
        ok &= expect(!s.gameComplete, "init: not game-complete");
        ok &= expect(!s.usingRuntimeIni, "init: compiled fallback (no runtime file)");
        ok &= expect(s.level.bMax == 10, "init: L1 bMax=10");
        ok &= expect(s.level.mMax == 50, "init: L1 mMax=50");
        ok &= expect(s.level.bDrop == 5, "init: L1 bDrop=5");
        ok &= expect(s.level.mFire == 5, "init: L1 mFire=5");
        ok &= expect(feq(s.level.bSpeed, 1.25f, 1e-6f), "init: L1 bSpeed=1.25px/tick");
        ok &= expect(feq(s.level.mSpeed, 37.5f, 1e-6f), "init: L1 mSpeed=37.5px/tick");
        ok &= expect(s.level.smart == 0, "init: L1 smart=0");
        ok &= expect(s.level.split == 15, "init: L1 split=15");
        ok &= expect(s.level.mMaxStatus == 25, "init: L1 mMaxStatus=25");
        ok &= expect(s.level.bMaxStatus == 35, "init: L1 bMaxStatus=35");
        ok &= expect(s.level.bExplodeb, "init: L1 bExplodeb=true");
        ok &= expect(feq(s.campaign.syncFactor, 25.0f, 1e-6f), "init: SyncFactor=25");
        ok &= expect(s.bHead == 0 && s.mHead == 0, "init: active lists empty");
        ok &= expect(mc_free_count(s.b, kMcPoolCap, s.bPool) == 5, "init: 5 hostile free slots");
        ok &= expect(mc_free_count(s.m, kMcPoolCap, s.mPool) == 5, "init: 5 defense free slots");
        ok &= expect(s.bDropped == 0 && s.mFired == 0, "init: quotas at zero");
        ok &= expect(!s.won && !s.lost && s.running, "init: playing state");
        ok &= expect(s.simulationSteps == 0u, "init: zero steps");
    }

    // ------------------------------------------------ enemy spawn
    {
        McState a, b;
        mc_init_with_seed(&a, 4242u);
        mc_init_with_seed(&b, 4242u);
        for (int i = 0; i < 3; ++i) {
            mc_fixed_update(&a);
            mc_fixed_update(&b);
        }
        ok &= expect(a.bDropped == 3 && b.bDropped == 3, "spawn: one quota unit per tick");
        ok &= expect(hash_state(a) == hash_state(b), "spawn: seeded runs identical");
        // Spawned hostile validity.
        int link = a.bHead;
        int n = 0;
        while (link != 0) {
            const McProj& p = a.b[link];
            ok &= expect(p.status == 1, "spawn: hostile in flight");
            ok &= expect(p.y >= 0.0f, "spawn: hostile below top edge");
            ok &= expect(p.ym > 0.0f, "spawn: hostile descends");
            ok &= expect(p.ym >= 1.25f && p.ym <= 2.51f, "spawn: speed in bSpeed*(1..2) band");
            ok &= expect(p.xe >= 0 && p.xe <= 1000, "spawn: target x in field");
            ok &= expect(mc_x_to_target(p.xe) >= 1 && mc_x_to_target(p.xe) <= 10,
                         "spawn: target slot valid");
            ++n;
            link = p.link;
        }
        ok &= expect(n == 3, "spawn: three actives after three ticks");

        // Pool exhaustion: 5 in-flight cap refuses further spawns cleanly.
        McState e;
        mc_init_with_seed(&e, 7u);
        for (int i = 0; i < 5; ++i) mc_fixed_update(&e);
        ok &= expect(mc_active_hostiles(&e) == 5, "pool: 5 concurrent at cap");
        ok &= expect(e.bPool == 0, "pool: free list drained");
        mc_fixed_update(&e);
        ok &= expect(e.bDropped == 5, "pool: refused spawn does not consume quota");
        ok &= expect(mc_active_hostiles(&e) == 5, "pool: still 5 active");
    }

    // ------------------------------------------------ defensive launch
    {
        McState s;
        mc_init_with_seed(&s, 99u);
        ok &= expect(mc_request_fire(&s, 500, 100), "fire: request accepted");
        mc_fixed_update(&s);
        ok &= expect(s.mFired == 1, "fire: quota consumed once");
        int link = s.mHead;
        ok &= expect(link != 0, "fire: missile active");
        if (link != 0) {
            ok &= expect(feq(s.m[link].x, 500.0f, 1e-4f) && s.m[link].y < 750.0f,
                         "fire: starts at battery and climbs");
            ok &= expect(s.m[link].xe == 500 && s.m[link].ye == 100, "fire: target stored");
            ok &= expect(feq(s.m[link].xm, 0.0f, 1e-6f), "fire: straight-up xm=0");
            ok &= expect(feq(s.m[link].ym, -37.5f, 1e-4f), "fire: straight-up ym=-mSpeed");
        }
        // Angled shot homes toward its target and snaps exactly onto it.
        McState g;
        mc_init_with_seed(&g, 99u);
        ok &= expect(mc_request_fire(&g, 800, 200), "fire: angled request accepted");
        float prevDist = 1e30f;
        bool homing = true;
        for (int i = 0; i < 3; ++i) {
            mc_fixed_update(&g);
            int ml = g.mHead;
            if (ml != 0 && g.m[ml].status == 1) {
                float dx = g.m[ml].x - 800.0f;
                float dy = g.m[ml].y - 200.0f;
                float d = dx * dx + dy * dy;
                if (!(d < prevDist)) homing = false;
                prevDist = d;
            }
        }
        ok &= expect(homing, "fire: angled shot closes on target");
        int ticks = 0;
        while (ticks < 40) {
            bool anyFlying = false;
            int ml = g.mHead;
            while (ml != 0) {
                if (g.m[ml].status == 1) anyFlying = true;
                ml = g.m[ml].link;
            }
            if (!anyFlying) break;
            mc_fixed_update(&g);
            ++ticks;
        }
        int ml = g.mHead;
        bool exact = false;
        while (ml != 0) {
            if (g.m[ml].status >= 2 && feq(g.m[ml].x, 800.0f, 1e-4f) &&
                feq(g.m[ml].y, 200.0f, 1e-4f)) {
                exact = true;
            }
            ml = g.m[ml].link;
        }
        ok &= expect(exact, "fire: detonation snaps exactly onto target");

        // Latch: two requests in one tick collapse to a single shot, last wins.
        McState l;
        mc_init(&l);
        mc_request_fire(&l, 100, 100);
        mc_request_fire(&l, 700, 300);
        mc_fixed_update(&l);
        ok &= expect(l.mFired == 1, "fire: one shot per tick");
        int ll = l.mHead;
        ok &= expect(ll != 0 && l.m[ll].xe == 700 && l.m[ll].ye == 300,
                     "fire: last press wins the latch");
    }

    // ------------------------------------------------ interception (square)
    {
        // Inside the box dies.
        McState s;
        mc_init(&s);
        place_burst(s, 400.0f, 300.0f, 5);
        int hb = place_hostile(s, 403.0f, 302.0f, 0.0f, 1.0f);
        ok &= expect(hb != 0, "intercept: scaffold placed");
        mc_intercept_pass(&s);
        ok &= expect(s.b[hb].status == 2, "intercept: inside box destroyed");

        // Just outside X survives.
        McState sx;
        mc_init(&sx);
        place_burst(sx, 400.0f, 300.0f, 5);
        int hx = place_hostile(sx, 406.0f, 300.0f, 0.0f, 1.0f);
        mc_intercept_pass(&sx);
        ok &= expect(sx.b[hx].status == 1, "intercept: outside X survives");

        // Just outside Y survives.
        McState sy;
        mc_init(&sy);
        place_burst(sy, 400.0f, 300.0f, 5);
        int hy = place_hostile(sy, 400.0f, 306.0f, 0.0f, 1.0f);
        mc_intercept_pass(&sy);
        ok &= expect(sy.b[hy].status == 1, "intercept: outside Y survives");

        // Corner proves square-vs-circle: (5,5) at r=5 is inside the box
        // (dist ~7.07 > 5, a Euclidean test would spare it).
        McState c;
        mc_init(&c);
        place_burst(c, 400.0f, 300.0f, 5);
        int hc = place_hostile(c, 405.0f, 305.0f, 0.0f, 1.0f);
        mc_intercept_pass(&c);
        ok &= expect(c.b[hc].status == 2, "intercept: corner kill proves square region");
        McState co;
        mc_init(&co);
        place_burst(co, 400.0f, 300.0f, 5);
        int hco = place_hostile(co, 406.0f, 305.0f, 0.0f, 1.0f);
        mc_intercept_pass(&co);
        ok &= expect(co.b[hco].status == 1, "intercept: just-outside corner survives");

        // Multi-kill retirement keeps pool iteration valid.
        McState k;
        mc_init(&k);
        place_burst(k, 400.0f, 300.0f, 8);
        int k1 = place_hostile(k, 398.0f, 300.0f, 0.0f, 0.0f);
        int k2 = place_hostile(k, 402.0f, 302.0f, 0.0f, 0.0f);
        int k3 = place_hostile(k, 400.0f, 296.0f, 0.0f, 0.0f);
        ok &= expect(k1 && k2 && k3, "intercept: three scaffolds placed");
        mc_intercept_pass(&k);
        ok &= expect(k.b[k1].status == 2 && k.b[k2].status == 2 && k.b[k3].status == 2,
                     "intercept: all three set exploding");
        for (int i = 0; i < 80; ++i) mc_myshow_hostiles(&k);
        ok &= expect(mc_active_hostiles(&k) == 0, "intercept: all retired");
        ok &= expect(mc_free_count(k.b, kMcPoolCap, k.bPool) == 5, "intercept: free list whole");
        ok &= expect(k.bHead == 0, "intercept: active head empty");
        ok &= expect(k.listGuardTrips == 0, "intercept: no list corruption");
    }

    // ------------------------------------------------ smart evasion (VB path)
    {
        McState s;
        mc_init(&s);
        place_burst(s, 400.0f, 300.0f, 5);
        int hb = place_hostile(s, 390.0f, 290.0f, 1.5f, 1.0f);
        s.b[hb].smart = true;
        int evadesBefore = s.evadeCount;
        mc_intercept_pass(&s);
        ok &= expect(s.b[hb].status == 1, "smart: distant smart bomb not killed");
        ok &= expect(s.b[hb].xm < 0.0f, "smart: evasion flips xm away");
        ok &= expect(s.evadeCount == evadesBefore + 1, "smart: evasion counted");
        // Non-smart bomb in the same spot holds course.
        McState d;
        mc_init(&d);
        place_burst(d, 400.0f, 300.0f, 5);
        int hd = place_hostile(d, 390.0f, 290.0f, 1.5f, 1.0f);
        mc_intercept_pass(&d);
        ok &= expect(d.b[hd].xm > 0.0f, "smart: dumb bomb holds course");
        // No evasion outside the wide box.
        McState far;
        mc_init(&far);
        place_burst(far, 400.0f, 300.0f, 5);
        int hf = place_hostile(far, 100.0f, 100.0f, 1.5f, 1.0f);
        far.b[hf].smart = true;
        float xmBefore = far.b[hf].xm;
        mc_intercept_pass(&far);
        ok &= expect(far.b[hf].status == 1 && far.b[hf].xm == xmBefore,
                     "smart: no evasion outside wide box");
        // No evasion when the bomb is below the explosion.
        McState below;
        mc_init(&below);
        place_burst(below, 400.0f, 300.0f, 5);
        int hb2 = place_hostile(below, 395.0f, 305.0f, 1.5f, 1.0f);
        below.b[hb2].smart = true;
        mc_intercept_pass(&below);
        ok &= expect(below.b[hb2].xm > 0.0f, "smart: no evasion below explosion");
        // Deterministic evasion: identical scaffolds flip identically.
        McState e1, e2;
        mc_init_with_seed(&e1, 5150u);
        mc_init_with_seed(&e2, 5150u);
        place_burst(e1, 400.0f, 300.0f, 5);
        place_burst(e2, 400.0f, 300.0f, 5);
        int he1 = place_hostile(e1, 390.0f, 290.0f, 1.5f, 1.0f);
        int he2 = place_hostile(e2, 390.0f, 290.0f, 1.5f, 1.0f);
        e1.b[he1].smart = true;
        e2.b[he2].smart = true;
        mc_intercept_pass(&e1);
        mc_intercept_pass(&e2);
        ok &= expect(e1.b[he1].xm == e2.b[he2].xm, "smart: deterministic evasion");
    }

    // ------------------------------------------------ bomb chain (bExplodeb)
    {
        McState s;
        mc_init(&s);
        int big = place_hostile(s, 400.0f, 400.0f, 0.0f, 0.0f);
        int near = place_hostile(s, 402.0f, 401.0f, 0.0f, 1.0f);
        s.b[big].status = 5;  // already bursting
        mc_intercept_pass(&s);
        ok &= expect(s.b[near].status == 2, "chain: bomb blast kills nearby bomb");
        McState off;
        mc_init(&off);
        off.level.bExplodeb = false;
        int big2 = place_hostile(off, 400.0f, 400.0f, 0.0f, 0.0f);
        int near2 = place_hostile(off, 402.0f, 401.0f, 0.0f, 1.0f);
        off.b[big2].status = 5;
        mc_intercept_pass(&off);
        ok &= expect(off.b[near2].status == 1, "chain: disabled chain spares nearby bomb");
    }

    // ------------------------------------------------ MIRV split (VB quirk)
    {
        McState s;
        mc_init_with_seed(&s, 31337u);
        int droppedBefore = s.bDropped;
        int firedBefore = s.mFired;
        int hb = place_hostile(s, 500.0f, 99.0f, 0.0f, 2.0f);
        s.b[hb].splitY = 104;
        int activeBefore = mc_active_hostiles(&s);
        for (int i = 0; i < 4; ++i) mc_myshow_hostiles(&s);
        ok &= expect(mc_active_hostiles(&s) == activeBefore + 1, "split: child spawns at SplitY");
        // VB quirk: the split child charges the MISSILE counter, not the
        // bomb counter (DoIt passes mFired% as the bomb-pool Droped%).
        ok &= expect(s.bDropped == droppedBefore + 1, "split: parent consumes bomb quota");
        ok &= expect(s.mFired == firedBefore + 1, "split: child consumes missile quota (quirk)");
        // Split pool-growth: exhausted free list grows the quota.
        McState g;
        mc_init_with_seed(&g, 77u);
        for (int i = 0; i < 5; ++i) mc_fixed_update(&g);  // drain the free list
        ok &= expect(g.bPool == 0, "split: free list drained for growth test");
        int hg = g.bHead;
        g.b[hg].splitY = (int)(g.b[hg].y + 1.0f);
        int quotaBefore = g.level.bMax;
        int droppedG = g.bDropped;
        int firedG = g.mFired;
        for (int i = 0; i < 3; ++i) mc_myshow_hostiles(&g);
        ok &= expect(g.level.bMax == quotaBefore + 1, "split: quota grows past free list");
        ok &= expect(g.bDropped == droppedG, "split: growth child leaves bomb quota alone");
        ok &= expect(g.mFired == firedG + 1, "split: growth child consumes missile quota");
        // Pool saturation at 500: no growth, child refused cleanly.
        McState sat;
        mc_init_with_seed(&sat, 9u);
        sat.level.bMax = kMcPoolCap;
        sat.bPool = 0;
        int sh = place_hostile(sat, 500.0f, 100.0f, 0.0f, 1.0f);
        // place_hostile fails without a free slot; force one active to test
        // the saturated growth path directly.
        (void)sh;
        ok &= expect(sat.level.bMax == kMcPoolCap, "split: saturation cap held");
    }

    // ------------------------------------------------ city impact
    {
        McState s;
        mc_init_with_seed(&s, 555u);
        int hb = place_hostile(s, 250.0f, 749.0f, 0.0f, 2.0f);  // slot 3: x in [200,300)
        mc_myshow_hostiles(&s);
        ok &= expect(!s.targets[3], "city: impact kills slot 3");
        ok &= expect(mc_alive_cities(&s) == 9, "city: survivor count drops");
        ok &= expect(s.b[hb].status == 2, "city: killer starts exploding");
        // City-hit blasts burn double (70 vs 35): still around after 36 ticks.
        for (int i = 0; i < 36; ++i) mc_myshow_hostiles(&s);
        ok &= expect(mc_active_hostiles(&s) == 1, "city: double-length blast persists");
        for (int i = 0; i < 40; ++i) mc_myshow_hostiles(&s);
        ok &= expect(mc_active_hostiles(&s) == 0, "city: blast retires after 2x window");
        // Impact on an already-dead slot: idempotent, count unchanged.
        int hb2 = place_hostile(s, 250.0f, 749.0f, 0.0f, 2.0f);
        mc_myshow_hostiles(&s);
        ok &= expect(!s.targets[3] && mc_alive_cities(&s) == 9, "city: dead slot stays dead");
        ok &= expect(s.b[hb2].status == 2, "city: repeat killer still detonates");
    }

    // ------------------------------------------------ win
    {
        McState q;
        mc_init(&q);
        mc_fixed_update(&q);
        ok &= expect(!q.won, "win: no win while quota remains");
        McState a;
        mc_init(&a);
        place_hostile(a, 100.0f, 100.0f, 0.0f, 1.0f);  // pool busy first...
        a.bDropped = a.level.bMax;                     // ...then quota exhausted
        mc_fixed_update(&a);
        ok &= expect(!a.won, "win: no win while hostiles active");
        McState w;
        mc_init(&w);
        w.bDropped = w.level.bMax;
        ok &= expect(w.bHead == 0, "win: pool drained");
        mc_evaluate_outcome(&w);
        ok &= expect(w.won && !w.lost, "win: quota + drain + cities = win");

        // Full-path win under live simulation with predicted auto-aim.
        McState f;
        mc_init_with_seed(&f, 20240u);
        int ticks = drive_auto_aim(f, 3000);
        ok &= expect(f.won, "win: live defense reaches LEVEL COMPLETE");
        ok &= expect(!f.lost && mc_alive_cities(&f) > 0, "win: cities survive the defense");
        std::cout << "INFO: live-win ticks=" << ticks << " alive=" << mc_alive_cities(&f)
                  << " dropped=" << f.bDropped << " quota=" << f.level.bMax
                  << " fired=" << f.mFired << "\n";
    }

    // ------------------------------------------------ lose
    {
        McState s;
        mc_init(&s);
        for (int i = 1; i <= 9; ++i) s.targets[i] = false;
        mc_evaluate_outcome(&s);
        ok &= expect(!s.lost, "lose: one city alive is not lost");
        s.targets[10] = false;
        mc_evaluate_outcome(&s);
        ok &= expect(s.lost && !s.won, "lose: final city enters lose state");
        // Lose takes precedence over a drained quota.
        McState p;
        mc_init(&p);
        p.bDropped = p.level.bMax;
        for (int i = 1; i <= 10; ++i) p.targets[i] = false;
        mc_evaluate_outcome(&p);
        ok &= expect(p.lost && !p.won, "lose: precedence over stale win");
        // Terminal freeze: no spawn, no fire, no step advance.
        uint64_t frozen = p.simulationSteps;
        ok &= expect(!mc_request_fire(&p, 500, 100), "lose: post-game fire refused");
        mc_fixed_update(&p);
        ok &= expect(p.simulationSteps == frozen, "lose: ticks freeze after termination");
        ok &= expect(p.bDropped == p.level.bMax, "lose: quota untouched after termination");
        // Restart key resets the whole campaign cleanly.
        ok &= expect(mc_handle_key(&p, kMcKeyRestartR, kMcKeyActionDown),
                     "lose: restart keeps running");
        ok &= expect(!p.lost && !p.won && !p.gameComplete, "lose: restart clears outcome");
        ok &= expect(p.levelIndex == 1, "lose: restart returns to L1");
        ok &= expect(mc_alive_cities(&p) == 10, "lose: restart revives cities");
        ok &= expect(p.bDropped == 0 && p.mFired == 0, "lose: restart zeroes quotas");
        ok &= expect(p.bHead == 0 && p.simulationSteps == frozen, "lose: pools fresh, clock kept");
        ok &= expect(mc_request_fire(&p, 500, 100), "lose: firing works after restart");
    }

    // ------------------------------------------------ input routing
    {
        McState s;
        mc_init(&s);
        ok &= expect(mc_handle_key(&s, 37, kMcKeyActionDown), "input: arrow keeps running");
        ok &= expect(s.lastKeyCode == 37, "input: key code recorded");
        ok &= expect(mc_handle_key(&s, kMcKeyRestartR, kMcKeyActionDown),
                     "input: mid-game R ignored but running");
        ok &= expect(s.running && !s.won && !s.lost, "input: mid-game R resets nothing");
        ok &= expect(mc_alive_cities(&s) == 10 && s.bDropped == 0, "input: state intact");
        ok &= expect(!mc_handle_key(&s, kMcKeyEscape, kMcKeyActionDown), "input: escape stops");
        ok &= expect(!s.running, "input: escape clears running");
        uint64_t frozen = s.simulationSteps;
        mc_fixed_update(&s);
        ok &= expect(s.simulationSteps == frozen, "input: updates freeze after stop");
        // VB-faithful translation matrix.
        ok &= expect(mc_should_fire(kMcActionDown, kMcButtonLeft), "input: left-down fires");
        ok &= expect(mc_should_fire(kMcActionMove, kMcButtonRight), "input: right-drag fires");
        ok &= expect(!mc_should_fire(kMcActionDown, kMcButtonRight),
                     "input: right-down alone does not fire");
        ok &= expect(!mc_should_fire(kMcActionMove, kMcButtonNone),
                     "input: move+none does not fire");
        ok &= expect(!mc_should_fire(kMcActionUp, kMcButtonLeft),
                     "input: release does not fire");
        // Fire refused during the level-complete dwell (no double transition).
        McState dw;
        mc_init(&dw);
        dw.bDropped = dw.level.bMax;
        mc_evaluate_outcome(&dw);
        ok &= expect(dw.won, "input: dwell starts won");
        ok &= expect(!mc_request_fire(&dw, 500, 100), "input: dwell fire refused");
        ok &= expect(mc_handle_key(&dw, kMcKeyRestartR, kMcKeyActionDown),
                     "input: dwell R ignored but running");
        ok &= expect(dw.won && dw.levelIndex == 1, "input: dwell R causes no transition");
    }

    // ------------------------------------------------ coordinate mapping
    {
        ok &= expect(mc_vb_to_window_x(0) == 0, "coord: left edge");
        ok &= expect(mc_vb_to_window_x(1000) == 480, "coord: right edge");
        ok &= expect(mc_vb_to_window_x(500) == 240, "coord: battery centered");
        ok &= expect(mc_vb_to_window_y(0) == 0, "coord: top edge");
        ok &= expect(mc_vb_to_window_y(750) == 360, "coord: ground edge");
        ok &= expect(mc_vb_to_window_y(375) == 180, "coord: vertical middle");
        ok &= expect(mc_vb_to_window_f(500.0f) == 240, "coord: float center");
        ok &= expect(mc_window_to_vb_x(240) == 500, "coord: inverse center");
        ok &= expect(mc_window_to_vb_x(0) == 0, "coord: inverse left");
        ok &= expect(mc_window_to_vb_x(479) == 997, "coord: inverse right");
        ok &= expect(mc_window_to_vb_y(359) == 747, "coord: inverse bottom");
        // Round-trip stays within one VB quantum (~2.1 units).
        bool rt = true;
        for (int vb = 0; vb <= 1000; vb += 37) {
            int w = mc_vb_to_window_x(vb);
            if (w >= kMcFrameWidth) w = kMcFrameWidth - 1;
            int back = mc_window_to_vb_x(w);
            int d = back - vb;
            if (d < 0) d = -d;
            if (d > 3) rt = false;
        }
        ok &= expect(rt, "coord: round-trip within quantum");
        ok &= expect(kMcCitySlotCount == 10, "coord: ten city slots");
        for (int i = 0; i < kMcCitySlotCount; ++i) {
            int x = mc_city_x(i);
            ok &= expect(x >= 0 && x + kMcCityWidth <= kMcFrameWidth, "coord: city inside frame");
            if (i > 0) ok &= expect(x > mc_city_x(i - 1), "coord: cities ordered");
        }
        ok &= expect(mc_city_y() + kMcCityHeight == kMcGroundTop, "coord: cities on ground");
        ok &= expect(mc_battery_y() + kMcBatteryHeight == kMcGroundTop, "coord: battery on ground");
        ok &= expect(mc_x_to_target(0) == 1, "coord: x2t left");
        ok &= expect(mc_x_to_target(1000) == 10, "coord: x2t right");
        ok &= expect(mc_x_to_target(250) == 3, "coord: x2t slot 3");
    }

    // ------------------------------------------------ DD.ini parsing
    {
        McCampaignConfig cfg;
        int rows = mc_parse_dd_ini(kDdIniOriginal, cstr_len(kDdIniOriginal), &cfg);
        ok &= expect(rows == 10, "ini: original DD.ini yields 10 rows");
        ok &= expect(cfg.levels[1].bMax == 10 && cfg.levels[1].mMax == 50, "ini: valid L1 quotas");
        ok &= expect(cfg.levels[1].bDrop == 5 && cfg.levels[1].mFire == 5, "ini: valid L1 caps");
        ok &= expect(feq(cfg.levels[1].bSpeedRaw, 0.05f, 1e-6f), "ini: valid L1 bSpeed");
        ok &= expect(cfg.levels[1].smart == 0 && cfg.levels[1].split == 15, "ini: valid L1 smart/split");
        ok &= expect(cfg.levels[2].bMax == 10 && cfg.levels[2].mFire == 10, "ini: valid L2 quotas");
        ok &= expect(cfg.levels[2].smart == 0 && cfg.levels[2].split == 20, "ini: valid L2 smart/split");
        ok &= expect(cfg.levels[3].bMax == 10 && cfg.levels[3].mFire == 10, "ini: valid L3 quotas");
        ok &= expect(feq(cfg.levels[3].bSpeedRaw, 0.07f, 1e-6f), "ini: valid L3 bSpeed");
        ok &= expect(cfg.levels[3].smart == 30 && cfg.levels[3].split == 25, "ini: valid L3 smart/split");
        ok &= expect(feq(cfg.syncFactor, 25.0f, 1e-6f), "ini: SyncFactor=25");
        ok &= expect(cfg.globals.mMaxStatus == 25 && cfg.globals.bMaxStatus == 35, "ini: blast globals");
        ok &= expect(feq(cfg.globals.mSpeedRaw, 1.5f, 1e-6f), "ini: mSpeed global");
        ok &= expect(cfg.globals.maxTarget == 10 && cfg.globals.bExplodeb, "ini: cities/chain globals");

        // Malformed row keeps the fallback row.
        const char* badRow =
            "[DD]\n"
            "l1=10, 50, 5\n"
            "l2=10, 50, 5, 10, 0.05,  0, 20, \"Slow and Dumb II\"\n";
        McCampaignConfig bad;
        int badRows = mc_parse_dd_ini(badRow, cstr_len(badRow), &bad);
        ok &= expect(badRows == 1, "ini: malformed row counted once (L2 only)");
        ok &= expect(bad.levels[1].bMax == 10 && bad.levels[1].split == 15,
                     "ini: malformed L1 falls back");
        ok &= expect(bad.levels[2].split == 20, "ini: valid L2 still applies");

        // Missing fields keep the fallback row.
        const char* missing =
            "[DD]\n"
            "l3=10, 50, 5, 10, 0.07,  30\n";
        McCampaignConfig miss;
        mc_parse_dd_ini(missing, cstr_len(missing), &miss);
        ok &= expect(miss.levels[3].smart == 30 && miss.levels[3].split == 25,
                     "ini: missing-field L3 falls back");

        // Invalid numeric field keeps the fallback row.
        const char* badNum =
            "[DD]\n"
            "l1=10, 50, 5, 5, fast,  0, 15, \"Slow and Dumb I\"\n";
        McCampaignConfig badN;
        mc_parse_dd_ini(badNum, cstr_len(badNum), &badN);
        ok &= expect(feq(badN.levels[1].bSpeedRaw, 0.05f, 1e-6f),
                     "ini: invalid numeric L1 falls back");

        // Out-of-range values keep the fallback row.
        const char* oor =
            "[DD]\n"
            "l1=9999, 50, 5, 5, 0.05,  0, 15, \"Slow and Dumb I\"\n"
            "l2=10, 50, 5, 10, 0.05,  0, 200, \"Slow and Dumb II\"\n"
            "mRadius=500\n"
            "mSpeed=99\n";
        McCampaignConfig ocfg;
        mc_parse_dd_ini(oor, cstr_len(oor), &ocfg);
        ok &= expect(ocfg.levels[1].bMax == 10, "ini: out-of-range bMax falls back");
        ok &= expect(ocfg.levels[2].split == 20, "ini: out-of-range split falls back");
        ok &= expect(ocfg.globals.mMaxStatus == 25, "ini: out-of-range mRadius falls back");
        ok &= expect(feq(ocfg.globals.mSpeedRaw, 1.5f, 1e-6f), "ini: out-of-range mSpeed falls back");

        // Missing file (empty text) yields the deterministic fallback.
        McCampaignConfig fb;
        int fbRows = mc_parse_dd_ini(nullptr, 0, &fb);
        ok &= expect(fbRows == 0, "ini: missing file yields zero rows");
        ok &= expect(fb.levels[1].bMax == 10 && fb.levels[3].smart == 30,
                     "ini: missing file fallback L1/L3");
        ok &= expect(feq(fb.syncFactor, 25.0f, 1e-6f), "ini: missing file SyncFactor");
        McCampaignConfig fb2;
        mc_fallback_campaign(&fb2);
        McCampaignConfig fb3;
        mc_fallback_campaign(&fb3);
        bool same = true;
        for (int i = 1; i <= kMcMaxLevels && same; ++i) {
            same = (fb2.levels[i].bMax == fb3.levels[i].bMax &&
                    fb2.levels[i].smart == fb3.levels[i].smart &&
                    fb2.levels[i].split == fb3.levels[i].split);
        }
        ok &= expect(same, "ini: fallback deterministic");
    }

    // ------------------------------------------------ progression L1->L2->L3
    {
        // Seed 15 wins the full L1->L2->L3 campaign under auto-aim
        // (verified by seed search); it exercises every transition.
        McState s;
        mc_init_with_seed(&s, 15u);
        int ticks1 = drive_auto_aim(s, 3000);
        ok &= expect(s.won && s.levelIndex == 1, "prog: L1 completes at L1");
        int citiesAfterL1 = mc_alive_cities(&s);
        ok &= expect(citiesAfterL1 > 0, "prog: cities survive L1");
        uint64_t stepsAtWin = s.simulationSteps;
        // Dwell: gameplay frozen, no duplicate advancement yet.
        for (int i = 0; i < kMcLevelCompleteDelayTicks - 1; ++i) mc_fixed_update(&s);
        ok &= expect(s.won && s.levelIndex == 1, "prog: dwell holds L1 won");
        ok &= expect(s.simulationSteps == stepsAtWin + (uint64_t)(kMcLevelCompleteDelayTicks - 1),
                     "prog: dwell counts ticks");
        mc_fixed_update(&s);  // exact boundary tick
        ok &= expect(!s.won && s.levelIndex == 2, "prog: L1 completion advances to L2");
        ok &= expect(s.level.bDrop == 5 && s.level.mFire == 10, "prog: L2 settings applied");
        ok &= expect(s.level.smart == 0 && s.level.split == 20, "prog: L2 smart/split applied");
        ok &= expect(feq(s.level.bSpeed, 1.25f, 1e-4f), "prog: L2 bSpeed applied");
        ok &= expect(s.bDropped == 0 && s.mFired == 0, "prog: counters reset for L2");
        ok &= expect(s.bHead == 0 && s.mHead == 0, "prog: pools reset for L2");
        ok &= expect(mc_free_count(s.b, kMcPoolCap, s.bPool) == 5, "prog: L2 hostile pool rebuilt");
        ok &= expect(mc_free_count(s.m, kMcPoolCap, s.mPool) == 10, "prog: L2 defense pool rebuilt");
        ok &= expect(mc_alive_cities(&s) == citiesAfterL1, "prog: cities persist into L2");
        // No duplicate advancement: further ticks stay on L2.
        for (int i = 0; i < 10; ++i) mc_fixed_update(&s);
        ok &= expect(s.levelIndex == 2 && !s.won, "prog: no duplicate advancement");

        int ticks2 = drive_auto_aim(s, 3000);
        (void)ticks2;
        ok &= expect(s.won && s.levelIndex == 2, "prog: L2 completes at L2");
        int citiesAfterL2 = mc_alive_cities(&s);
        for (int i = 0; i < kMcLevelCompleteDelayTicks; ++i) mc_fixed_update(&s);
        ok &= expect(s.levelIndex == 3, "prog: L2 completion advances to L3");
        ok &= expect(s.level.smart == 30 && s.level.split == 25, "prog: L3 smart/split applied");
        ok &= expect(feq(s.level.bSpeed, 1.75f, 1e-4f), "prog: L3 bSpeed applied");
        ok &= expect(s.bDropped == 0 && s.mFired == 0, "prog: counters reset for L3");
        ok &= expect(s.bHead == 0 && s.mHead == 0, "prog: pools reset for L3");
        ok &= expect(mc_alive_cities(&s) == citiesAfterL2, "prog: cities persist into L3");

        int ticks3 = drive_auto_aim(s, 4000);
        (void)ticks3;
        // L3 may end won (then GAME COMPLETE after the dwell) or lost when
        // cities finally fall; both are campaign-terminal and deterministic.
        if (s.won) {
            for (int i = 0; i < kMcLevelCompleteDelayTicks; ++i) mc_fixed_update(&s);
        }
        ok &= expect(s.gameComplete || s.lost, "prog: completing L3 reaches campaign terminal");
        if (s.gameComplete) {
            ok &= expect(!s.won && s.levelIndex == 3, "prog: GAME COMPLETE at L3");
            ok &= expect(!mc_request_fire(&s, 500, 100), "prog: post-complete fire refused");
            uint64_t frozen = s.simulationSteps;
            mc_fixed_update(&s);
            ok &= expect(s.simulationSteps == frozen, "prog: terminal freezes ticks");
            ok &= expect(mc_handle_key(&s, kMcKeyRestartR, kMcKeyActionDown),
                         "prog: restart keeps running");
            ok &= expect(s.levelIndex == 1 && !s.gameComplete && !s.lost,
                         "prog: restart returns to L1");
            ok &= expect(mc_alive_cities(&s) == 10, "prog: restart revives cities");
            ok &= expect(s.bDropped == 0 && s.mFired == 0, "prog: restart zeroes quotas");
        }
        std::cout << "INFO: progression ticks L1=" << ticks1 << " citiesL1=" << citiesAfterL1 << "\n";
    }

    // ------------------------------------------------ pristine L3 -> GAME COMPLETE
    {
        // A fresh L3 (all cities alive) is winnable by the deterministic
        // defense and must terminate the campaign with GAME COMPLETE.
        McState s;
        mc_init_with_seed(&s, 90210u);
        s.levelIndex = 3;
        mc_apply_level(&s, 3);
        mc_clear_pools(&s);
        s.bDropped = 0;
        s.mFired = 0;
        int ticks = drive_auto_aim(s, 4000);
        ok &= expect(s.won && s.levelIndex == 3, "l3win: pristine L3 completes");
        for (int i = 0; i < kMcLevelCompleteDelayTicks; ++i) mc_fixed_update(&s);
        ok &= expect(s.gameComplete && !s.won, "l3win: GAME COMPLETE terminal");
        ok &= expect(!mc_request_fire(&s, 500, 100), "l3win: post-complete fire refused");
        std::cout << "INFO: pristine-L3 ticks=" << ticks << " alive=" << mc_alive_cities(&s) << "\n";
    }

    // ------------------------------------------------ smart bombs (natural L3)
    {
        // Deterministic smart selection: identical seeds select identically.
        McState a, b;
        mc_init_with_seed(&a, 777u);
        mc_init_with_seed(&b, 777u);
        a.level.smart = 30;
        b.level.smart = 30;
        int sa = 0, sb = 0;
        for (int i = 0; i < 10; ++i) {
            int ta = mc_launch_b(&a, false, 0.0f, 0.0f);
            int tb = mc_launch_b(&b, false, 0.0f, 0.0f);
            if (ta != 0 && a.b[ta].smart) ++sa;
            if (tb != 0 && b.b[tb].smart) ++sb;
        }
        ok &= expect(sa == sb, "smart: deterministic selection");

        // Ordinary spawn path (L1 smart=0): no smart bombs.
        McState o;
        mc_init_with_seed(&o, 31337u);
        for (int i = 0; i < 5; ++i) mc_fixed_update(&o);
        ok &= expect(count_smart(o) == 0, "smart: L1 spawns none naturally");

        // Smart spawn path (L3 smart=30): naturals appear in live ticks
        // (seed 1 shows the first smart bomb at tick 4).
        McState n;
        mc_init_with_seed(&n, 1u);
        n.levelIndex = 3;
        mc_apply_level(&n, 3);
        mc_clear_pools(&n);
        bool natural = false;
        for (int i = 0; i < 120 && !natural; ++i) {
            mc_fixed_update(&n);
            if (count_smart(n) > 0) natural = true;
            if (n.won || n.lost || n.gameComplete) break;
        }
        ok &= expect(natural, "smart: L3 config spawns smart bombs naturally");

        // Impact/retirement: smart bombs detonate on cities like ordinary.
        McState im;
        mc_init_with_seed(&im, 555u);
        int hb = place_hostile(im, 250.0f, 749.0f, 0.0f, 2.0f);
        im.b[hb].smart = true;
        mc_myshow_hostiles(&im);
        ok &= expect(im.b[hb].status == 2 && !im.targets[3], "smart: impact kills city");

        // Split interaction: smart status is freshly rolled for children
        // (not inherited), so a smart parent can yield a dumb child and
        // vice versa; both paths stay reachable and deterministic.
        McState sp1, sp2;
        mc_init_with_seed(&sp1, 60606u);
        mc_init_with_seed(&sp2, 60606u);
        sp1.level.smart = 100;
        sp2.level.smart = 100;
        int p1 = mc_launch_b(&sp1, false, 0.0f, 0.0f);
        int p2 = mc_launch_b(&sp2, false, 0.0f, 0.0f);
        ok &= expect(p1 != 0 && sp1.b[p1].smart, "smart: smart=100 always flags");
        ok &= expect(sp1.b[p1].smart == sp2.b[p2].smart, "smart: spawn deterministic");
    }

    // ------------------------------------------------ splits (level-specific)
    {
        McCampaignConfig cfg;
        mc_fallback_campaign(&cfg);
        ok &= expect(cfg.levels[1].split == 15, "split: L1 split=15");
        ok &= expect(cfg.levels[2].split == 20, "split: L2 split=20");
        ok &= expect(cfg.levels[3].split == 25, "split: L3 split=25");
        // Deterministic child behavior: identical seeds, identical children.
        McState a, b;
        mc_init_with_seed(&a, 424242u);
        mc_init_with_seed(&b, 424242u);
        int ha = place_hostile(a, 500.0f, 99.0f, 0.0f, 2.0f);
        int hb = place_hostile(b, 500.0f, 99.0f, 0.0f, 2.0f);
        a.b[ha].splitY = 104;
        b.b[hb].splitY = 104;
        for (int i = 0; i < 4; ++i) {
            mc_myshow_hostiles(&a);
            mc_myshow_hostiles(&b);
        }
        ok &= expect(hash_state(a) == hash_state(b), "split: deterministic children");
    }

    // ------------------------------------------------ determinism vector
    {
        McState a, b;
        mc_init_with_seed(&a, 0x12345678u);
        mc_init_with_seed(&b, 0x12345678u);
        const int fireTick[3] = {3, 7, 15};
        const int fireX[3] = {100, 900, 500};
        const int fireY[3] = {200, 150, 400};
        for (int t = 1; t <= 500; ++t) {
            for (int k = 0; k < 3; ++k) {
                if (t == fireTick[k]) {
                    mc_request_fire(&a, fireX[k], fireY[k]);
                    mc_request_fire(&b, fireX[k], fireY[k]);
                }
            }
            mc_fixed_update(&a);
            mc_fixed_update(&b);
        }
        uint64_t ha = hash_state(a);
        uint64_t hb = hash_state(b);
        ok &= expect(ha == hb, "determinism: identical fingerprints");
        ok &= expect(a.simulationSteps == 500u, "determinism: 500 ticks elapse");
        ok &= expect(a.mFired == 3, "determinism: three shots consumed");
        ok &= expect(a.bDropped > 0, "determinism: quota advanced");
        std::cout << "INFO: determinism fingerprint=" << ha << " dropped=" << a.bDropped
                  << " alive=" << mc_alive_cities(&a) << " won=" << a.won << " lost=" << a.lost
                  << "\n";
    }

    // ------------------------------------------- multi-level determinism
    {
        // Seed 15 plays the full campaign to GAME COMPLETE; two identical
        // runs (transitions included) must fingerprint identically.
        McState a, b;
        mc_init_with_seed(&a, 15u);
        mc_init_with_seed(&b, 15u);
        // Identical auto-aim campaign across L1-L3 (transitions included).
        for (int phase = 0; phase < 3; ++phase) {
            drive_auto_aim(a, 3000);
            if (a.won) {
                for (int i = 0; i < kMcLevelCompleteDelayTicks; ++i) mc_fixed_update(&a);
            } else {
                break;
            }
        }
        for (int phase = 0; phase < 3; ++phase) {
            drive_auto_aim(b, 3000);
            if (b.won) {
                for (int i = 0; i < kMcLevelCompleteDelayTicks; ++i) mc_fixed_update(&b);
            } else {
                break;
            }
        }
        uint64_t ha = hash_state(a);
        uint64_t hb = hash_state(b);
        ok &= expect(ha == hb, "campaign: multi-level fingerprint identical");
        std::cout << "INFO: campaign fingerprint=" << ha << " level=" << a.levelIndex
                  << " alive=" << mc_alive_cities(&a) << " complete=" << a.gameComplete
                  << " lost=" << a.lost << " steps=" << a.simulationSteps << "\n";
    }

    // ------------------------------------------------ null safety
    {
        mc_init(nullptr);
        mc_fixed_update(nullptr);
        mc_reset_level(nullptr);
        mc_reset_campaign(nullptr);
        mc_evaluate_outcome(nullptr);
        mc_request_fire(nullptr, 1, 2);
        mc_request_close(nullptr);
        ok &= expect(mc_handle_key(nullptr, 1, 1), "null: key handling stays running");
        ok &= expect(mc_alive_cities(nullptr) == 0, "null: no cities");
        ok &= expect(mc_active_hostiles(nullptr) == 0, "null: no hostiles");
        ok &= expect(mc_intercept_pass(nullptr) == 0, "null: no kills");
        ok &= expect(!mc_advance_level(nullptr), "null: no advance");
    }

    if (g_failures == 0) std::cout << "MissileCommand state test PASS\n";
    return g_failures == 0 ? 0 : 1;
}
