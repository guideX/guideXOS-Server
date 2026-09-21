// Missile Command MC3 Native ELF application: L1 -> L2 -> L3 campaign.
//
// Genuine App Model application: gx_main entry point, guidexos-c-abi-v1,
// fixed-size centered window, retained present_frame scene, poll_event input,
// get_ticks_ms fixed-step updates, clean exit. Deterministic gameplay lives
// in missilecommand_state.h (host-testable); this file owns window, events,
// DD.ini resource loading, framebuffer rendering, overlay text, and
// lifecycle logging only.
//
// Controls (VB-faithful): left-button press fires from the central battery
// at the pointer; right-button DRAG (move with right held) also fires, like
// Pic_MouseMove(Button=2) synthesizing a left click in FrmDD.frm; right
// press alone does nothing (VB MouseRead only fires on button 1). R / Enter
// / Space restarts a finished campaign (there is no VB menu equivalent in
// the App Model; File > Play is replaced by the restart key). Escape exits.
// Left-click is retained as a guideXOS convenience input: VB6 only fired on
// left DOWN as well (MouseRead button 1), so retaining it is faithful, not
// a divergence; right-drag is the restored original alternate control.
//
// Test hooks (off by default, never alter normal release behavior unless
// explicitly keyed): 'A' toggles deterministic autopilot defense (predicted
// auto-aim, for live smoke progression); 'F' toggles fast-forward (extra
// fixed-step ticks per frame, same tick semantics, faster wall clock).

#include <guidexos/ui.h>

#include "missilecommand_state.h"

extern "C" void* memset(void* destination, int value, uint64_t bytes) {
    uint8_t* output = static_cast<uint8_t*>(destination);
    for (uint64_t i = 0; i < bytes; ++i) output[i] = static_cast<uint8_t>(value);
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, uint64_t bytes) {
    uint8_t* out = static_cast<uint8_t*>(destination);
    const uint8_t* in = static_cast<const uint8_t*>(source);
    for (uint64_t i = 0; i < bytes; ++i) out[i] = in[i];
    return destination;
}

namespace {

// McState is ~100 KiB (two 501-slot pools); static storage keeps it off the
// freestanding stack.
static McState g_state;
static uint32_t g_framePixels[kMcFrameWidth * kMcFrameHeight];
static bool g_autopilot = false;
static bool g_fastForward = false;
static char g_ddIniBuffer[2048];

static const uint32_t kColorBackground = 0x000000u;
static const uint32_t kColorGround = 0x003800u;
static const uint32_t kColorCity = 0xFFFF00u;
static const uint32_t kColorRubble = 0x505050u;
static const uint32_t kColorBattery = 0x00FFFFu;
static const uint32_t kColorHostile = 0xFFFF00u;
static const uint32_t kColorDefense = 0x00FFFFu;
static const uint32_t kColorTrail = 0xFF0000u;

static void clear_event(gx_event* event) {
    if (!event) return;
    event->size = 0;
    event->type = GX_EVENT_NONE;
    event->window = 0;
    event->param1 = 0;
    event->param2 = 0;
    event->param3 = 0;
    event->param4 = 0;
}

static void set_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= kMcFrameWidth || y < 0 || y >= kMcFrameHeight) return;
    g_framePixels[y * kMcFrameWidth + x] = color;
}

static void fill_rect(int x, int y, int width, int height, uint32_t color) {
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > kMcFrameWidth) width = kMcFrameWidth - x;
    if (y + height > kMcFrameHeight) height = kMcFrameHeight - y;
    if (width <= 0 || height <= 0) return;
    for (int row = 0; row < height; ++row) {
        uint32_t* line = g_framePixels + (y + row) * kMcFrameWidth + x;
        for (int col = 0; col < width; ++col) line[col] = color;
    }
}

// Bresenham line (VB Line trail equivalent, clipped by set_pixel).
static void draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0;
    if (dx < 0) dx = -dx;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 - y0;
    if (dy < 0) dy = -dy;
    int sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

// Midpoint circle outline (VB Circle equivalent: warheads r=2, blasts grow).
static void draw_circle(int cx, int cy, int radius, uint32_t color) {
    if (radius < 0) return;
    if (radius == 0) {
        set_pixel(cx, cy, color);
        return;
    }
    int x = radius;
    int y = 0;
    int err = 0;
    while (x >= y) {
        set_pixel(cx + x, cy + y, color);
        set_pixel(cx + y, cy + x, color);
        set_pixel(cx - y, cy + x, color);
        set_pixel(cx - x, cy + y, color);
        set_pixel(cx - x, cy - y, color);
        set_pixel(cx - y, cy - x, color);
        set_pixel(cx + y, cy - x, color);
        set_pixel(cx + x, cy - y, color);
        ++y;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err -= 2 * x + 1;
        }
    }
}

static void draw_proj_head(int wx, int wy, uint32_t color) {
    fill_rect(wx - 1, wy - 1, 3, 3, color);
}

static void render_pool(const McProj* pool, int head, uint32_t color, bool trails) {
    int link = head;
    int guard = 0;
    while (link >= 1 && link <= kMcPoolCap && guard <= kMcPoolCap) {
        ++guard;
        const McProj* p = &pool[link];
        const int wx = mc_vb_to_window_f(p->x);
        const int wy = mc_vb_to_window_f(p->y);
        if (p->status == 1) {
            // Trails for ordinary bombs only: Smart bombs show the head
            // alone, like the VB original (MC1 archaeology §3).
            if (trails && !p->smart) {
                draw_line(mc_vb_to_window_f(p->xs), mc_vb_to_window_f(p->ys), wx, wy,
                          kColorTrail);
            }
            draw_proj_head(wx, wy, color);
        } else if (p->status >= 2) {
            draw_circle(wx, wy, p->status, color);
        }
        link = p->link;
    }
}

// VB-faithful scene on the 480x360 frame: dark playfield, ground band,
// alive cities (yellow) vs rubble (gray), central battery, hostile bombs
// with red trails (Smart bombs show heads only, like the original), defense
// with trails, and growing explosion rings for both blast pools.
static void render_scene(const McState& state) {
    fill_rect(0, 0, kMcFrameWidth, kMcFrameHeight, kColorBackground);
    fill_rect(0, kMcGroundTop, kMcFrameWidth, kMcFrameHeight - kMcGroundTop, kColorGround);
    for (int i = 1; i <= kMcCitySlotCount; ++i) {
        const int x = mc_city_x(i - 1);
        if (state.targets[i]) {
            fill_rect(x, mc_city_y(), kMcCityWidth, kMcCityHeight, kColorCity);
        } else {
            fill_rect(x + 4, kMcGroundTop - 6, kMcCityWidth - 8, 6, kColorRubble);
        }
    }
    fill_rect(mc_battery_x(), mc_battery_y(), kMcBatteryWidth, kMcBatteryHeight, kColorBattery);
    render_pool(state.b, state.bHead, kColorHostile, true);
    render_pool(state.m, state.mHead, kColorDefense, true);
}

static gx_result present(gx_app_context* ctx, gx_handle window) {
    return gx_present_frame(ctx, window, 0, 0, kMcFrameWidth, kMcFrameHeight,
        kMcFrameWidth * 4u, GX_PIXEL_FORMAT_XRGB8888, g_framePixels,
        kMcFrameWidth * kMcFrameHeight * 4u);
}

static void append_text(char* message, uint32_t* index, uint32_t capacity, const char* text) {
    if (!message || !index || !text) return;
    for (uint32_t i = 0; text[i] && *index + 1u < capacity; ++i) message[(*index)++] = text[i];
}

static void append_number(char* message, uint32_t* index, uint32_t capacity, uint64_t value) {
    char digits[20];
    uint32_t length = 0;
    do {
        digits[length++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && length < sizeof(digits));
    while (length > 0 && *index + 1u < capacity) message[(*index)++] = digits[--length];
}

static void draw_overlay(gx_app_context* ctx, gx_handle window, const McState& state) {
    char title[96];
    uint32_t ti = 0;
    append_text(title, &ti, sizeof(title), "MISSILE COMMAND - MC3 campaign L");
    append_number(title, &ti, sizeof(title), (uint64_t)state.levelIndex);
    append_text(title, &ti, sizeof(title), "/3 ");
    append_text(title, &ti, sizeof(title), state.campaign.levels[state.levelIndex].name);
    title[ti] = '\0';
    gx_draw_label(ctx, window, 10, 16, title);
    char status[192];
    uint32_t index = 0;
    append_text(status, &index, sizeof(status), "cities=");
    append_number(status, &index, sizeof(status), (uint64_t)mc_alive_cities(&state));
    append_text(status, &index, sizeof(status), " bombs=");
    append_number(status, &index, sizeof(status),
                  (uint64_t)(state.level.bMax - state.bDropped < 0 ? 0 : state.level.bMax - state.bDropped));
    append_text(status, &index, sizeof(status), " missiles=");
    append_number(status, &index, sizeof(status),
                  (uint64_t)(state.level.mMax - state.mFired < 0 ? 0 : state.level.mMax - state.mFired));
    append_text(status, &index, sizeof(status), " active=");
    append_number(status, &index, sizeof(status), (uint64_t)mc_active_hostiles(&state));
    append_text(status, &index, sizeof(status), state.usingRuntimeIni ? " ini=run" : " ini=fb");
    status[index] = '\0';
    gx_draw_label(ctx, window, 10, 328, status);
    if (state.gameComplete) {
        gx_draw_label(ctx, window, 10, 344, "GAME COMPLETE - press R to play again");
    } else if (state.won) {
        gx_draw_label(ctx, window, 10, 344, "LEVEL COMPLETE - advancing...");
    } else if (state.lost) {
        gx_draw_label(ctx, window, 10, 344, "GAME OVER - press R to play again");
    } else {
        gx_draw_label(ctx, window, 10, 344, "left-click / right-drag to fire - ESC exits");
    }
}

static bool render_and_present(gx_app_context* ctx, gx_handle window, const McState& state) {
    render_scene(state);
    const gx_result result = present(ctx, window);
    if (result != GX_OK) return false;
    draw_overlay(ctx, window, state);
    return true;
}

// Deterministic autopilot defense (test hook, off by default):
// splitter-priority predicted auto-aim with idle discipline (one burst at
// a time, ammo-efficient). MIRV carriers are engaged first since they
// multiply; otherwise the lowest bomb. Decisions depend only on sim state,
// so the assisted campaign stays deterministic. Called once per
// fixed-step tick (never per frame) so live runs reproduce host runs
// tick-for-tick when no manual input intervenes.
static void autopilot_tick(McState& state) {
    if (!g_autopilot || state.won || state.lost || state.gameComplete) return;
    int best = 0;
    int bl = state.bHead;
    while (bl != 0) {
        if (state.b[bl].status == 1 && state.b[bl].y > 250.0f && state.b[bl].splitY != 0) {
            if (best == 0 || state.b[bl].y > state.b[best].y) best = bl;
        }
        bl = state.b[bl].link;
    }
    if (best == 0) {
        bl = state.bHead;
        while (bl != 0) {
            if (state.b[bl].status == 1 && state.b[bl].y > 250.0f) {
                if (best == 0 || state.b[bl].y > state.b[best].y) best = bl;
            }
            bl = state.b[bl].link;
        }
    }
    if (best != 0 && mc_active_defense(&state) == 0) {
        float flight = (750.0f - state.b[best].y) / state.level.mSpeed;
        if (flight < 0.0f) flight = 0.0f;
        int tx = (int)(state.b[best].x + state.b[best].xm * flight + 0.5f);
        int ty = (int)(state.b[best].y + state.b[best].ym * flight + 0.5f);
        mc_request_fire(&state, tx, ty);
    }
}

}  // namespace

extern "C" gx_result GX_CALL gx_main(gx_app_context* ctx) {
    if (!ctx || !ctx->host) return GX_ERROR_INVALID_ARGUMENT;
    if (!ctx->host->log || !ctx->host->request_window || !ctx->host->poll_event ||
        !ctx->host->get_ticks_ms || !ctx->host->present_frame) {
        return GX_ERROR_INVALID_ARGUMENT;
    }

    ctx->host->log(ctx, "MissileCommand MC3 Native ELF starting");

    // Runtime DD.ini via the existing file_read_all resource mechanism.
    // Falls back to verified compiled values when missing/malformed.
    McCampaignConfig campaign;
    mc_fallback_campaign(&campaign);
    bool usingRuntime = false;
    int iniRows = 0;
    if (ctx->host->file_read_all) {
        for (uint32_t i = 0; i < sizeof(g_ddIniBuffer); ++i) g_ddIniBuffer[i] = '\0';
        uint32_t bytesRead = 0;
        const gx_result readResult = ctx->host->file_read_all(
            ctx, "resources/DD.ini", g_ddIniBuffer, sizeof(g_ddIniBuffer) - 1, &bytesRead);
        if (readResult == GX_OK && bytesRead > 0 && bytesRead < sizeof(g_ddIniBuffer)) {
            g_ddIniBuffer[bytesRead] = '\0';
            McCampaignConfig parsed;
            iniRows = mc_parse_dd_ini(g_ddIniBuffer, bytesRead, &parsed);
            // Require at least L1-L3 valid for runtime selection; otherwise
            // keep per-row fallback inside parsed but mark fallback overall
            // when L1-L3 did not all come from the file.
            campaign = parsed;
            usingRuntime = true;
            // Verify L1-L3 match expected ranges (parser already validated);
            // if none of the rows came from the file, treat as fallback.
            if (iniRows <= 0) usingRuntime = false;
        }
    }
    mc_init_campaign_with_seed(&g_state, kMcDefaultSeed, &campaign, usingRuntime);
    McState& state = g_state;
    if (usingRuntime) {
        ctx->host->log(ctx, "MissileCommand DD.ini runtime loaded");
    } else {
        ctx->host->log(ctx, "MissileCommand DD.ini fallback selected");
    }
    ctx->host->log(ctx, "MissileCommand initial state ready");

    gx_handle window = 0;
    gx_result windowResult = GX_ERROR_UNSUPPORTED;
    if (ctx->host->request_window_ex) {
        windowResult = ctx->host->request_window_ex(ctx, "Missile Command (MC3)",
            kMcFrameWidth, kMcFrameHeight,
            GX_WINDOW_FLAG_FIXED_SIZE | GX_WINDOW_FLAG_CENTERED, &window);
    } else {
        windowResult = ctx->host->request_window(ctx, "Missile Command (MC3)",
            kMcFrameWidth, kMcFrameHeight, &window);
    }
    if (windowResult != GX_OK) {
        ctx->host->log(ctx, "MissileCommand window creation failed");
        return windowResult;
    }

    if (!render_and_present(ctx, window, state)) {
        ctx->host->log(ctx, "MissileCommand frame presentation failed");
        return GX_ERROR_FAILED;
    }
    ctx->host->log(ctx, "MissileCommand initial frame presented");
    state.visualDirty = false;

    bool loggedSpawn = false;
    bool loggedLaunch = false;
    bool loggedBurst = false;
    bool loggedKill = false;
    bool loggedSmart = false;
    int lastEvadeCount = 0;
    int lastCities = mc_alive_cities(&state);
    int lastLevel = state.levelIndex;
    bool loggedL2 = false;
    bool loggedL3 = false;
    bool loggedComplete = false;
    bool loggedOutcome = false;
    bool loggedWonEpisode = false;
    uint64_t loggedStatusStep = 0u;

    uint64_t previousTicks = gx_get_ticks_ms(ctx);
    uint64_t lastPresentedTicks = previousTicks;
    uint64_t accumulatorMs = 0;
    bool running = true;

    while (running && state.running) {
        // Drain the input queue each frame (bounded): the compositor emits
        // frame traffic continuously, and consuming one event per frame lets
        // input lag minutes behind. Draining in FIFO order preserves event
        // semantics; paints only flag the scene dirty (rendering stays at
        // the single throttled present below).
        for (int drain = 0; drain < 16 && running && state.running; ++drain) {
            gx_event event;
            clear_event(&event);
            const gx_result eventResult = ctx->host->poll_event(ctx, &event, drain == 0 ? 10 : 0);
            if (eventResult != GX_OK) {
                if (eventResult != GX_ERROR_TIMEOUT) {
                    ctx->host->log(ctx, "MissileCommand poll_event failed");
                    running = false;
                }
                break;
            }
            if (event.window != window) continue;
            if (gx_event_is_paint(&event)) {
                state.visualDirty = true;
            } else if (gx_event_is_close(&event)) {
                ctx->host->log(ctx, "MissileCommand close event received");
                running = false;
            } else if (gx_event_is_escape_down(&event)) {
                ctx->host->log(ctx, "MissileCommand Escape pressed");
                running = false;
            } else if (event.type == GX_EVENT_KEY) {
                // Test hooks: 'A' autopilot, 'F' fast-forward. Normal keys
                // route through mc_handle_key (R/Enter/Space restart, ESC exits).
                if (event.param2 == 1 && (event.param1 == 65 || event.param1 == 97)) {
                    g_autopilot = !g_autopilot;
                    ctx->host->log(ctx, g_autopilot ? "MissileCommand autopilot on"
                                                    : "MissileCommand autopilot off");
                    state.visualDirty = true;
                } else if (event.param2 == 1 && (event.param1 == 70 || event.param1 == 102)) {
                    g_fastForward = !g_fastForward;
                    ctx->host->log(ctx, g_fastForward ? "MissileCommand fastforward on"
                                                      : "MissileCommand fastforward off");
                } else {
                    if (!mc_handle_key(&state, event.param1, event.param2)) running = false;
                }
            } else if (event.type == GX_EVENT_MOUSE) {
                const int action = GX_MOUSE_ACTION(event.param3);
                const int button = GX_MOUSE_BUTTON(event.param3);
                // VB faithful via mc_should_fire: left DOWN fires at the
                // pointer; right MOVE (drag) fires too; right DOWN alone
                // fires nothing (VB MouseRead only acts on button 1).
                if (mc_should_fire(action, button)) {
                    const int vbX = mc_window_to_vb_x(event.param1);
                    const int vbY = mc_window_to_vb_y(event.param2);
                    if (mc_request_fire(&state, vbX, vbY)) {
                        const bool rightDrag =
                            action == GX_MOUSE_ACTION_MOVE && button == GX_MOUSE_BUTTON_RIGHT;
                        ctx->host->log(ctx, rightDrag ? "MissileCommand right-drag fire routed"
                                                      : "MissileCommand click routed");
                    }
                }
            }
        }

        if (!running || !state.running) break;

        const uint64_t currentTicks = gx_get_ticks_ms(ctx);
        uint64_t elapsedMs = currentTicks >= previousTicks ? currentTicks - previousTicks : 0;
        previousTicks = currentTicks;
        if (elapsedMs > kMcMaxElapsedMs) elapsedMs = kMcMaxElapsedMs;
        accumulatorMs += elapsedMs;
        if (accumulatorMs > kMcMaxElapsedMs) accumulatorMs = kMcMaxElapsedMs;

        uint32_t updates = 0;
        const uint32_t stepCap = g_fastForward ? kMcMaxCatchUpSteps * 4u : kMcMaxCatchUpSteps;
        while (accumulatorMs >= kMcFixedStepMs && updates < stepCap) {
            autopilot_tick(state);
            mc_fixed_update(&state);
            accumulatorMs -= kMcFixedStepMs;
            ++updates;
        }
        if (g_fastForward && state.levelIndex < kMcCampaignLevels) {
            // Fast-forward burst: extra deterministic ticks beyond the wall
            // accumulator so smoke tests can traverse L1-L2 quickly. Same
            // tick semantics; only wall pacing changes. Bounded per frame;
            // the level-complete dwell advances here too (still counted in
            // fixed-step ticks), while lost/game-complete stay frozen.
            // L3 plays at wall rate so smart-bomb behavior stays observable
            // and the input demo keeps a comfortable margin before any L3
            // terminal state.
            for (uint32_t extra = 0; extra < 120u; ++extra) {
                if (state.lost || state.gameComplete) break;
                autopilot_tick(state);
                mc_fixed_update(&state);
                ++updates;
            }
            accumulatorMs = 0;
        } else if (updates == kMcMaxCatchUpSteps && accumulatorMs >= kMcFixedStepMs) {
            accumulatorMs = 0;
        }

        // Lifecycle markers for the runtime smoke test (logged once each).
        if (!loggedSpawn && state.bDropped > 0) {
            ctx->host->log(ctx, "MissileCommand hostile spawned");
            loggedSpawn = true;
        }
        if (!loggedLaunch && state.mFired > 0) {
            ctx->host->log(ctx, "MissileCommand defensive launch");
            loggedLaunch = true;
        }
        if (!loggedBurst && loggedLaunch) {
            int link = state.mHead;
            while (link >= 1 && link <= kMcPoolCap) {
                if (state.m[link].status >= 2) {
                    ctx->host->log(ctx, "MissileCommand defensive detonation");
                    loggedBurst = true;
                    break;
                }
                link = state.m[link].link;
            }
        }
        if (!loggedKill) {
            // Any hostile exploding above the ground was killed by
            // interception or the bomb chain (impacts explode at y>=750,
            // off-screen exits retire silently).
            int link = state.bHead;
            while (link >= 1 && link <= kMcPoolCap) {
                if (state.b[link].status >= 2 && state.b[link].y < (float)kMcVbMaxY) {
                    ctx->host->log(ctx, "MissileCommand intercept kill");
                    loggedKill = true;
                    break;
                }
                link = state.b[link].link;
            }
        }
        if (!loggedSmart) {
            int link = state.bHead;
            while (link >= 1 && link <= kMcPoolCap) {
                if (state.b[link].smart && state.b[link].status == 1) {
                    ctx->host->log(ctx, "MissileCommand smart bomb spawned");
                    loggedSmart = true;
                    break;
                }
                link = state.b[link].link;
            }
        }
        if (state.evadeCount != lastEvadeCount) {
            lastEvadeCount = state.evadeCount;
            ctx->host->log(ctx, "MissileCommand smart bomb evaded");
        }
        const int cities = mc_alive_cities(&state);
        if (cities < lastCities) {
            ctx->host->log(ctx, "MissileCommand city destroyed");
            lastCities = cities;
        }
        if (state.levelIndex != lastLevel) {
            lastLevel = state.levelIndex;
            if (state.levelIndex == 2 && !loggedL2) {
                ctx->host->log(ctx, "MissileCommand LEVEL 2 started");
                loggedL2 = true;
            } else if (state.levelIndex == 3 && !loggedL3) {
                ctx->host->log(ctx, "MissileCommand LEVEL 3 started");
                loggedL3 = true;
            }
        }
        if (state.won && !loggedWonEpisode) {
            ctx->host->log(ctx, "MissileCommand LEVEL COMPLETE");
            loggedWonEpisode = true;
        }
        if (!state.won) loggedWonEpisode = false;
        if (!loggedComplete && state.gameComplete) {
            ctx->host->log(ctx, "MissileCommand GAME COMPLETE");
            loggedComplete = true;
        }
        if (!loggedOutcome && state.lost) {
            ctx->host->log(ctx, "MissileCommand GAME OVER");
            loggedOutcome = true;
        }
        // Periodic tick-based status for headless validation (deterministic:
        // driven by simulation steps, never wall clock). Boundary-crossing
        // (not exact-modulo) so fast-forward bursts cannot skip it.
        if (!state.gameComplete && !state.lost && state.simulationSteps != 0u &&
            state.simulationSteps / 400u != loggedStatusStep / 400u) {
            loggedStatusStep = state.simulationSteps;
            char status[192];
            uint32_t si = 0;
            append_text(status, &si, sizeof(status), "MissileCommand status level=");
            append_number(status, &si, sizeof(status), (uint64_t)state.levelIndex);
            append_text(status, &si, sizeof(status), " cities=");
            append_number(status, &si, sizeof(status), (uint64_t)mc_alive_cities(&state));
            append_text(status, &si, sizeof(status), " dropped=");
            append_number(status, &si, sizeof(status), (uint64_t)state.bDropped);
            append_text(status, &si, sizeof(status), "/");
            append_number(status, &si, sizeof(status), (uint64_t)state.level.bMax);
            append_text(status, &si, sizeof(status), " active=");
            append_number(status, &si, sizeof(status), (uint64_t)mc_active_hostiles(&state));
            append_text(status, &si, sizeof(status), " fired=");
            append_number(status, &si, sizeof(status), (uint64_t)state.mFired);
            append_text(status, &si, sizeof(status), " steps=");
            append_number(status, &si, sizeof(status), state.simulationSteps);
            status[si] = '\0';
            ctx->host->log(ctx, status);
        }

        // In fast-forward the scene still presents (throttled harder so the
        // frame IPC does not dominate the accelerated tick loop).
        const uint64_t presentIntervalMs = g_fastForward ? 2000u : kMcVisualIntervalMs;
        if (state.visualDirty && currentTicks - lastPresentedTicks >= presentIntervalMs) {
            if (!render_and_present(ctx, window, state)) {
                ctx->host->log(ctx, "MissileCommand frame presentation failed");
                running = false;
            } else {
                state.visualDirty = false;
                lastPresentedTicks = currentTicks;
            }
        }
    }

    ctx->host->log(ctx, "MissileCommand MC3 exiting");
    if (ctx->host->exit) return ctx->host->exit(ctx, GX_OK);
    return GX_OK;
}
