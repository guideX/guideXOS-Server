#include "ephemeris_core.h"

#include <stddef.h>
#include <stdint.h>

#include <guidexos/ui.h>

namespace {

static const char kDataPath[] =
    "Astronomy.Ephemeris/Data/ephemeris_de440s_chebyshev.sn4bin";
static const int32_t kTargetEarth = 399;
static const double kFixedEpochEt = 0.0;
static const int kWindowWidth = 560;
static const int kWindowHeight = 340;

struct AppData {
    bool available;
    stellar_native::Error error;
    stellar_native::Vector3 positionKm;
    char status[112];
};

struct FileSource {
    gx_app_context* app;
    const char* path;
};

struct ProgressContext {
    gx_app_context* app;
};

static stellar_native::Context g_ephemeris;

static void copy_text(char* destination, uint32_t capacity, const char* source) {
    if (!destination || capacity == 0u) return;
    uint32_t index = 0;
    if (source) {
        while (index + 1u < capacity && source[index]) {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

static bool append_text(char* destination, uint32_t capacity, uint32_t* length,
                        const char* source) {
    if (!destination || !length || !source || *length >= capacity) return false;
    uint32_t index = 0;
    while (source[index]) {
        if (*length + 1u >= capacity) return false;
        destination[(*length)++] = source[index++];
    }
    destination[*length] = '\0';
    return true;
}

static bool append_uint64(char* destination, uint32_t capacity, uint32_t* length,
                          uint64_t value) {
    char reversed[24];
    uint32_t digits = 0;
    do {
        reversed[digits++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && digits < sizeof(reversed));
    while (digits > 0u) {
        char digit[2] = { reversed[--digits], '\0' };
        if (!append_text(destination, capacity, length, digit)) return false;
    }
    return true;
}

static uint64_t app_ticks_ms(gx_app_context* app) {
    if (!app || !app->host) return 0u;
    const gx_host_calls* host = app->host;
    if (host->size < offsetof(gx_host_calls, get_ticks_ms) + sizeof(host->get_ticks_ms) ||
        !host->get_ticks_ms) return 0u;
    return host->get_ticks_ms(app);
}

static void emit_stage(gx_app_context* app, const char* name, bool hasValue, uint64_t value) {
    if (!app || !app->host || !app->host->log || !name) return;
    char record[192];
    record[0] = '\0';
    uint32_t length = 0u;
    if (!append_text(record, sizeof(record), &length, "STELLAR_NAVIGATOR_STAGE name=") ||
        !append_text(record, sizeof(record), &length, name) ||
        !append_text(record, sizeof(record), &length, " ticks_ms=") ||
        !append_uint64(record, sizeof(record), &length, app_ticks_ms(app)) ||
        !append_text(record, sizeof(record), &length, " clock=pit-10ms")) return;
    if (hasValue && (!append_text(record, sizeof(record), &length, " value=") ||
        !append_uint64(record, sizeof(record), &length, value))) return;
    app->host->log(app, record);
}

static const char* progress_stage_name(stellar_native::ProgressStage stage) {
    switch (stage) {
    case stellar_native::ProgressStage::AuthenticationStarted: return "authentication-start";
    case stellar_native::ProgressStage::AuthenticationProgress: return "authentication-progress-bytes";
    case stellar_native::ProgressStage::AuthenticationComplete: return "authentication-complete-bytes";
    case stellar_native::ProgressStage::IndexValidationStarted: return "index-validation-start";
    case stellar_native::ProgressStage::IndexValidationComplete: return "index-validation-complete-segments";
    case stellar_native::ProgressStage::TargetRecordSelected: return "target-record-selected-offset";
    case stellar_native::ProgressStage::CoefficientEvaluationStarted: return "coefficient-evaluation-start-offset";
    case stellar_native::ProgressStage::CoefficientEvaluationComplete: return "coefficient-evaluation-complete-offset";
    }
    return "unknown";
}

static void native_progress(void* user, stellar_native::ProgressStage stage, uint64_t value) {
    ProgressContext* progress = static_cast<ProgressContext*>(user);
    if (!progress) return;
    emit_stage(progress->app, progress_stage_name(stage), true, value);
}

static bool format_fixed3(double value, char* destination, uint32_t capacity) {
    if (!destination || capacity < 8u || value != value ||
        value > 1.0e12 || value < -1.0e12) return false;
    const bool negative = value < 0.0;
    const double magnitude = negative ? -value : value;
    const uint64_t scaled = static_cast<uint64_t>(magnitude * 1000.0 + 0.5);
    const uint64_t integerPart = scaled / 1000u;
    const uint32_t fractionalPart = static_cast<uint32_t>(scaled % 1000u);
    uint64_t remaining = integerPart;
    char reversed[24];
    uint32_t digits = 0;
    do {
        reversed[digits++] = static_cast<char>('0' + (remaining % 10u));
        remaining /= 10u;
    } while (remaining != 0u && digits < sizeof(reversed));

    uint32_t output = 0;
    if (negative) destination[output++] = '-';
    while (digits > 0u) destination[output++] = reversed[--digits];
    if (output + 5u > capacity) return false;
    destination[output++] = '.';
    destination[output++] = static_cast<char>('0' + (fractionalPart / 100u));
    destination[output++] = static_cast<char>('0' + ((fractionalPart / 10u) % 10u));
    destination[output++] = static_cast<char>('0' + (fractionalPart % 10u));
    destination[output] = '\0';
    return true;
}

static bool native_read_at(void* user, uint64_t offset, void* buffer,
                           uint32_t capacity, uint32_t* bytesRead) {
    FileSource* source = static_cast<FileSource*>(user);
    if (bytesRead) *bytesRead = 0;
    if (!source || !source->app || !source->app->host || !buffer || !bytesRead || capacity == 0u)
        return false;
    const gx_host_calls* host = source->app->host;
    if (host->size < offsetof(gx_host_calls, file_read) + sizeof(host->file_read) || !host->file_read)
        return false;
    return host->file_read(source->app, source->path, offset, buffer, capacity, bytesRead) == GX_OK;
}

static gx_result native_file_exists(gx_app_context* app, const char* path, uint32_t* exists) {
    if (!app || !app->host || !exists) return GX_ERROR_INVALID_ARGUMENT;
    const gx_host_calls* host = app->host;
    if (host->size < offsetof(gx_host_calls, file_exists) + sizeof(host->file_exists) || !host->file_exists)
        return GX_ERROR_UNSUPPORTED;
    return host->file_exists(app, path, exists);
}

static void clear_event(gx_event* event) {
    if (!event) return;
    event->size = sizeof(*event);
    event->type = GX_EVENT_NONE;
    event->window = 0;
    event->param1 = 0;
    event->param2 = 0;
    event->param3 = 0;
    event->param4 = 0;
}

static void make_result_log(const AppData& data, char* output, uint32_t capacity) {
    if (!output || capacity == 0u) return;
    output[0] = '\0';
    uint32_t length = 0;
    if (!append_text(output, capacity, &length, "STELLAR_NAVIGATOR_EPHEMERIS status=") ||
        !append_text(output, capacity, &length, data.available ? "available" : "unavailable") ||
        !append_text(output, capacity, &length, " body=earth target_naif=399 et_tdb_s=0.000000 frame=J2000_SSB units=km"))
        return;
    if (data.available) {
        char x[48], y[48], z[48];
        if (!format_fixed3(data.positionKm.x, x, sizeof(x)) ||
            !format_fixed3(data.positionKm.y, y, sizeof(y)) ||
            !format_fixed3(data.positionKm.z, z, sizeof(z))) return;
        if (!append_text(output, capacity, &length, " position_km=(") ||
            !append_text(output, capacity, &length, x) ||
            !append_text(output, capacity, &length, ",") ||
            !append_text(output, capacity, &length, y) ||
            !append_text(output, capacity, &length, ",") ||
            !append_text(output, capacity, &length, z) ||
            !append_text(output, capacity, &length, ") sha256=") ||
            !append_text(output, capacity, &length, stellar_native::datasetSha256())) return;
    } else {
        append_text(output, capacity, &length, " error=");
        append_text(output, capacity, &length, stellar_native::errorText(data.error));
    }
}

static bool draw_label(gx_app_context* app, gx_handle window, int x, int y, const char* text) {
    return gx_draw_label(app, window, x, y, text) == GX_OK;
}

static bool draw_panel(gx_app_context* app, gx_handle window, gx_rect rect, gx_color color) {
    return gx_draw_panel(app, window, rect, color) == GX_OK;
}

static bool draw_content(gx_app_context* app, gx_handle window, const AppData& data) {
    const gx_rect background = { 8, 8, 544, 324 };
    const gx_rect statePanel = { 18, 94, 524, 176 };
    bool success = true;
    if (!draw_panel(app, window, background, 0x142033u)) success = false;
    if (!draw_label(app, window, 24, 31, "STELLAR NAVIGATOR SN10")) success = false;
    if (!draw_label(app, window, 24, 58, "Native guideXOS ephemeris viewer")) success = false;
    if (!draw_panel(app, window, statePanel, 0x202E42u)) success = false;
    if (!draw_label(app, window, 34, 112, "Target: Earth (NAIF 399)")) success = false;
    if (!draw_label(app, window, 34, 139, "Epoch: ET 0.000000 s past J2000 (TDB)")) success = false;
    if (!draw_label(app, window, 34, 166, "Frame: J2000, solar-system barycentric origin (SSB)")) success = false;
    if (!draw_label(app, window, 34, 193, "Position units: kilometres")) success = false;

    if (data.available) {
        char line[64];
        char number[48];
        if (format_fixed3(data.positionKm.x, number, sizeof(number))) {
            copy_text(line, sizeof(line), "X: ");
            uint32_t used = 3u;
            append_text(line, sizeof(line), &used, number);
            append_text(line, sizeof(line), &used, " km");
            if (!draw_label(app, window, 34, 222, line)) success = false;
        }
        if (format_fixed3(data.positionKm.y, number, sizeof(number))) {
            copy_text(line, sizeof(line), "Y: ");
            uint32_t used = 3u;
            append_text(line, sizeof(line), &used, number);
            append_text(line, sizeof(line), &used, " km");
            if (!draw_label(app, window, 220, 222, line)) success = false;
        }
        if (format_fixed3(data.positionKm.z, number, sizeof(number))) {
            copy_text(line, sizeof(line), "Z: ");
            uint32_t used = 3u;
            append_text(line, sizeof(line), &used, number);
            append_text(line, sizeof(line), &used, " km");
            if (!draw_label(app, window, 406, 222, line)) success = false;
        }
        if (!draw_label(app, window, 24, 290, "Data: DE440s SNEPH04B authenticated (SHA-256)")) success = false;
    } else {
        if (!draw_label(app, window, 34, 224, "Position: unavailable")) success = false;
        if (!draw_label(app, window, 24, 285, "Data error:")) success = false;
        if (!draw_label(app, window, 104, 285, data.status)) success = false;
    }
    if (!draw_label(app, window, 24, 315, "Close the window or press Escape to exit.")) success = false;
    return success;
}

static void evaluate_fixed_state(gx_app_context* app, AppData* data, FileSource* source,
                                 ProgressContext* progress) {
    data->available = false;
    data->error = stellar_native::Error::ReadFailure;
    data->positionKm = { 0.0, 0.0, 0.0 };
    copy_text(data->status, sizeof(data->status), "DE440s data file could not be read.");

    uint32_t exists = 0;
    emit_stage(app, "ephemeris-file-check-start", false, 0u);
    const gx_result existsResult = native_file_exists(app, kDataPath, &exists);
    if (existsResult == GX_OK && exists == 0u) {
        data->error = stellar_native::Error::ReadFailure;
        copy_text(data->status, sizeof(data->status), "Required DE440s data file is missing.");
        return;
    }
    if (existsResult != GX_OK && existsResult != GX_ERROR_UNSUPPORTED) {
        copy_text(data->status, sizeof(data->status), "DE440s data availability check failed.");
        return;
    }
    emit_stage(app, "ephemeris-file-present", false, 0u);

    source->app = app;
    source->path = kDataPath;
    progress->app = app;
    const stellar_native::Reader reader = {
        native_read_at, source, stellar_native::kDatasetBytes, native_progress, progress
    };
    const uint64_t initializeStart = app_ticks_ms(app);
    emit_stage(app, "ephemeris-initialize-start", false, 0u);
    data->error = stellar_native::initialize(&g_ephemeris, &reader);
    const uint64_t initializeEnd = app_ticks_ms(app);
    emit_stage(app, "ephemeris-initialize-complete-error-code", true,
        static_cast<uint64_t>(data->error));
    emit_stage(app, "ephemeris-initialize-duration-ms", true,
        initializeEnd - initializeStart);
    if (data->error != stellar_native::Error::Ok) {
        copy_text(data->status, sizeof(data->status), stellar_native::errorText(data->error));
        return;
    }
    const uint64_t evaluationStart = app_ticks_ms(app);
    emit_stage(app, "ephemeris-evaluation-start", false, 0u);
    data->error = stellar_native::evaluatePosition(&g_ephemeris, kTargetEarth,
        kFixedEpochEt, &data->positionKm);
    const uint64_t evaluationEnd = app_ticks_ms(app);
    emit_stage(app, "ephemeris-evaluation-complete-error-code", true,
        static_cast<uint64_t>(data->error));
    emit_stage(app, "ephemeris-evaluation-duration-ms", true,
        evaluationEnd - evaluationStart);
    if (data->error != stellar_native::Error::Ok) {
        copy_text(data->status, sizeof(data->status), stellar_native::errorText(data->error));
        stellar_native::shutdown(&g_ephemeris);
        return;
    }
    data->available = true;
    copy_text(data->status, sizeof(data->status), "authenticated and evaluated");
}

} // namespace

extern "C" gx_result GX_CALL gx_main(gx_app_context* app) {
    if (!app || !app->host || app->apiVersion != GX_API_VERSION) return GX_ERROR_INVALID_ARGUMENT;
    const gx_host_calls* host = app->host;
    if (host->size < offsetof(gx_host_calls, file_read) + sizeof(host->file_read) ||
        !host->log || !host->request_window || !host->draw_text || !host->draw_rect ||
        !host->poll_event || !host->file_read) return GX_ERROR_UNSUPPORTED;

    emit_stage(app, "application-entry", false, 0u);
    AppData data = {};
    FileSource source = {};
    ProgressContext progress = {};
    evaluate_fixed_state(app, &data, &source, &progress);
    char resultLog[512];
    make_result_log(data, resultLog, sizeof(resultLog));
    emit_stage(app, "ephemeris-result-formatted", false, 0u);
    host->log(app, resultLog[0] ? resultLog : "STELLAR_NAVIGATOR_EPHEMERIS status=log-format-error");

    gx_handle window = 0;
    emit_stage(app, "window-creation-start", false, 0u);
    const gx_result windowResult = host->request_window(app, "Stellar Navigator SN10", kWindowWidth,
        kWindowHeight, &window);
    if (windowResult != GX_OK) {
        stellar_native::shutdown(&g_ephemeris);
        host->log(app, "STELLAR_NAVIGATOR_LIFECYCLE status=window-create-failed");
        return windowResult;
    }
    host->log(app, "STELLAR_NAVIGATOR_UI status=window-created");

    emit_stage(app, "initial-content-render-start", false, 0u);
    const bool firstDrawSucceeded = draw_content(app, window, data);
    host->log(app, firstDrawSucceeded
        ? "STELLAR_NAVIGATOR_UI status=initial-content-drawn"
        : "STELLAR_NAVIGATOR_UI status=initial-draw-failed");
    if (firstDrawSucceeded) emit_stage(app, "application-ready", false, 0u);
    bool done = false;
    while (!done) {
        gx_event event;
        clear_event(&event);
        const gx_result eventResult = host->poll_event(app, &event, 500);
        if (eventResult == GX_OK && event.window == window) {
            if (gx_event_is_paint(&event)) {
                if (!draw_content(app, window, data))
                    host->log(app, "STELLAR_NAVIGATOR_UI status=redraw-failed");
            } else if (gx_event_is_close(&event) || gx_event_is_escape_down(&event)) {
                done = true;
            }
        } else if (eventResult != GX_OK && eventResult != GX_ERROR_TIMEOUT) {
            host->log(app, "STELLAR_NAVIGATOR_LIFECYCLE status=input-error");
            done = true;
        }
    }

    stellar_native::shutdown(&g_ephemeris);
    host->log(app, "STELLAR_NAVIGATOR_LIFECYCLE status=initialized-evaluated-shutdown");
    if (host->exit) return host->exit(app, GX_OK);
    return GX_OK;
}
