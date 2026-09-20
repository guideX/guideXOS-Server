// Host test for the Missile Command MC2 deterministic defense loop.
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
    mix(h, s.won ? 1u : 0u);
    mix(h, s.lost ? 1u : 0u);
    mix(h, s.running ? 1u : 0u);
    mix(h, s.visualDirty ? 1u : 0u);
    mix(h, (uint64_t)(uint32_t)s.clickCount);
    mix(h, (uint64_t)(uint32_t)s.lastKeyCode);
    mix(h, (uint64_t)(uint32_t)s.listGuardTrips);
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

}  // namespace

int main() {
    bool ok = true;

    // ------------------------------------------------ initial state
    {
        McState s;
        mc_init(&s);
        ok &= expect(mc_alive_cities(&s) == 10, "init: 10 cities alive");
        ok &= expect(kMcVbBatteryX == 500 && kMcVbBatteryY == 750, "init: battery (500,750)");
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

    // ------------------------------------------------ smart evasion (L1-dormant path)
    {
        McState s;
        mc_init(&s);
        place_burst(s, 400.0f, 300.0f, 5);
        int hb = place_hostile(s, 390.0f, 290.0f, 1.5f, 1.0f);
        s.b[hb].smart = true;  // L1 spawns none; force the VB path.
        mc_intercept_pass(&s);
        ok &= expect(s.b[hb].status == 1, "smart: distant smart bomb not killed");
        ok &= expect(s.b[hb].xm < 0.0f, "smart: evasion flips xm away");
        // Non-smart bomb in the same spot holds course.
        McState d;
        mc_init(&d);
        place_burst(d, 400.0f, 300.0f, 5);
        int hd = place_hostile(d, 390.0f, 290.0f, 1.5f, 1.0f);
        mc_intercept_pass(&d);
        ok &= expect(d.b[hd].xm > 0.0f, "smart: dumb bomb holds course");
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

    // ------------------------------------------------ MIRV split (L1 split=15)
    {
        McState s;
        mc_init_with_seed(&s, 31337u);
        int before = s.bDropped;
        int hb = place_hostile(s, 500.0f, 99.0f, 0.0f, 2.0f);
        s.b[hb].splitY = 104;
        int activeBefore = mc_active_hostiles(&s);
        for (int i = 0; i < 4; ++i) mc_myshow_hostiles(&s);
        ok &= expect(mc_active_hostiles(&s) == activeBefore + 1, "split: child spawns at SplitY");
        ok &= expect(s.bDropped == before + 2, "split: parent+child consume bomb quota");
        // Split pool-growth: exhausted free list grows the quota.
        McState g;
        mc_init_with_seed(&g, 77u);
        for (int i = 0; i < 5; ++i) mc_fixed_update(&g);  // drain the free list
        ok &= expect(g.bPool == 0, "split: free list drained for growth test");
        int hg = g.bHead;
        g.b[hg].splitY = (int)(g.b[hg].y + 1.0f);
        int quotaBefore = g.level.bMax;
        int droppedBefore = g.bDropped;
        for (int i = 0; i < 3; ++i) mc_myshow_hostiles(&g);
        ok &= expect(g.level.bMax == quotaBefore + 1, "split: quota grows past free list");
        ok &= expect(g.bDropped == droppedBefore + 1, "split: growth child consumes quota");
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
        int ticks = 0;
        while (!f.won && !f.lost && ticks < 3000) {
            // One predicted shot per tick at the lowest high bomb.
            int best = 0;
            int bl = f.bHead;
            while (bl != 0) {
                if (f.b[bl].status == 1 && f.b[bl].y > 420.0f) {
                    if (best == 0 || f.b[bl].y > f.b[best].y) best = bl;
                }
                bl = f.b[bl].link;
            }
            if (best != 0 && mc_active_defense(&f) == 0) {
                float flight = (750.0f - f.b[best].y) / 37.5f;
                if (flight < 0.0f) flight = 0.0f;
                int tx = (int)(f.b[best].x + f.b[best].xm * flight + 0.5f);
                int ty = (int)(f.b[best].y + f.b[best].ym * flight + 0.5f);
                mc_request_fire(&f, tx, ty);
            }
            mc_fixed_update(&f);
            ++ticks;
        }
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
        // Restart key resets cleanly.
        ok &= expect(mc_handle_key(&p, kMcKeyRestartR, kMcKeyActionDown),
                     "lose: restart keeps running");
        ok &= expect(!p.lost && !p.won, "lose: restart clears outcome");
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

    // ------------------------------------------------ null safety
    {
        mc_init(nullptr);
        mc_fixed_update(nullptr);
        mc_reset_level(nullptr);
        mc_evaluate_outcome(nullptr);
        mc_request_fire(nullptr, 1, 2);
        mc_request_close(nullptr);
        ok &= expect(mc_handle_key(nullptr, 1, 1), "null: key handling stays running");
        ok &= expect(mc_alive_cities(nullptr) == 0, "null: no cities");
        ok &= expect(mc_active_hostiles(nullptr) == 0, "null: no hostiles");
        ok &= expect(mc_intercept_pass(nullptr) == 0, "null: no kills");
    }

    if (g_failures == 0) std::cout << "MissileCommand state test PASS\n";
    return g_failures == 0 ? 0 : 1;
}
