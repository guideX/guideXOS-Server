#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "../kernel/core/include/kernel/aarch64_gui_render.h"

static_assert(alignof(gx_host_calls) == 8, "AARCH64 host-call alignment changed");
static_assert(offsetof(gx_host_calls, draw_text) == 32, "AARCH64 draw_text offset changed");
static_assert(offsetof(gx_host_calls, draw_rect) == 40, "AARCH64 draw_rect offset changed");
static_assert(offsetof(gx_host_calls, guiVersion) == 240, "AARCH64 guiVersion offset changed");
static_assert(offsetof(gx_host_calls, bare_metal_build_project_start) == 288,
              "AARCH64 Build-start offset changed");
static_assert(offsetof(gx_host_calls, bare_metal_development_run_release) == 392,
              "AARCH64 final Run offset changed");
static_assert(sizeof(gx_host_calls) == 400, "AARCH64 host-call table size changed");

namespace {

static const uint32_t kCanary = 0xA55A3CC3u;
static const uint32_t kPadding = 0xD15EA5E5u;

struct Checks {
    uint32_t total;
    uint32_t failed;
    Checks() : total(0), failed(0) {}
    void expect(bool result, const char* message) {
        ++total;
        if (!result) {
            ++failed;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

struct TestSurface {
    std::vector<uint32_t> allocation;
    kernel::gui_render::Surface view;

    TestSurface(uint32_t width, uint32_t height, uint32_t stride)
        : allocation(static_cast<size_t>(stride) * height + 2u, kCanary), view{} {
        view.pixels = allocation.data() + 1;
        view.width = width;
        view.height = height;
        view.stridePixels = stride;
        view.storagePixels = static_cast<uint64_t>(stride) * height;
        clear(0);
    }

    void clear(uint32_t color) {
        for (uint32_t row = 0; row < view.height; ++row) {
            for (uint32_t column = 0; column < view.width; ++column)
                view.pixels[static_cast<uint64_t>(row) * view.stridePixels + column] = color;
            for (uint32_t column = view.width; column < view.stridePixels; ++column)
                view.pixels[static_cast<uint64_t>(row) * view.stridePixels + column] = kPadding;
        }
    }

    uint32_t at(uint32_t x, uint32_t y) const {
        return view.pixels[static_cast<uint64_t>(y) * view.stridePixels + x];
    }

    bool guards_intact() const {
        if (allocation.front() != kCanary || allocation.back() != kCanary) return false;
        for (uint32_t row = 0; row < view.height; ++row)
            for (uint32_t column = view.width; column < view.stridePixels; ++column)
                if (view.pixels[static_cast<uint64_t>(row) * view.stridePixels + column] != kPadding)
                    return false;
        return true;
    }

    bool all_pixels(uint32_t color) const {
        for (uint32_t row = 0; row < view.height; ++row)
            for (uint32_t column = 0; column < view.width; ++column)
                if (at(column, row) != color) return false;
        return true;
    }
};

struct Owner {
    gx_handle handles[2];
    TestSurface* surfaces[2];
    kernel::gui_render::SurfaceRegistry* registry;
    uint32_t count;
    uint32_t invalidations;
    Owner() : handles{}, surfaces{}, registry(nullptr), count(0), invalidations(0) {}
};

bool resolve_surface(void* opaque, gx_handle handle, kernel::gui_render::Surface* output) {
    Owner* owner = static_cast<Owner*>(opaque);
    if (!owner || !output) return false;
    for (uint32_t i = 0; i < owner->count; ++i) {
        if (owner->handles[i] != handle) continue;
        if (owner->registry) return owner->registry->resolve(handle, output);
        if (owner->surfaces[i]) {
            *output = owner->surfaces[i]->view;
            return true;
        }
    }
    return false;
}

void invalidate_surface(void* opaque, gx_handle) {
    Owner* owner = static_cast<Owner*>(opaque);
    if (owner) ++owner->invalidations;
}

void bind_context(kernel::gui_render::HostBinding* binding, gx_app_context* context,
                  gx_host_calls* host, Owner* owner, void* runtime) {
    binding->runtime = runtime;
    binding->context = context;
    binding->host = host;
    binding->owner = owner;
    binding->resolveSurface = resolve_surface;
    binding->invalidateSurface = invalidate_surface;
    binding->active = 1;
    context->size = sizeof(*context);
    context->apiVersion = GX_API_VERSION;
    context->host = host;
    context->userData = binding;
}

gx_result GX_CALL sentinel_wait(gx_app_context*, gx_event*) { return GX_OK; }
gx_result GX_CALL sentinel_build(gx_app_context*, const gx_build_request*, gx_build_handle*) { return GX_OK; }
gx_result GX_CALL sentinel_run_release(gx_app_context*, gx_development_run_handle) { return GX_OK; }

uint64_t buffer_hash(const TestSurface& surface) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (uint32_t row = 0; row < surface.view.height; ++row) {
        for (uint32_t column = 0; column < surface.view.width; ++column) {
            const uint32_t pixel = surface.at(column, row);
            for (uint32_t byte = 0; byte < 4; ++byte) {
                hash ^= static_cast<uint8_t>(pixel >> (byte * 8));
                hash *= UINT64_C(1099511628211);
            }
        }
    }
    return hash;
}

bool write_ppm(const std::string& path, const TestSurface& surface) {
    std::ofstream output(path.c_str(), std::ios::binary);
    if (!output) return false;
    output << "P6\n" << surface.view.width << ' ' << surface.view.height << "\n255\n";
    for (uint32_t row = 0; row < surface.view.height; ++row) {
        for (uint32_t column = 0; column < surface.view.width; ++column) {
            const uint32_t pixel = surface.at(column, row);
            const char rgb[3] = {
                static_cast<char>((pixel >> 16) & 0xFF),
                static_cast<char>((pixel >> 8) & 0xFF),
                static_cast<char>(pixel & 0xFF)
            };
            output.write(rgb, sizeof(rgb));
        }
    }
    return output.good();
}

bool test_rectangles(Checks& checks) {
    TestSurface surface(12, 10, 17);
    auto draw = [&](int x, int y, int w, int h, uint32_t c) {
        return kernel::gui_render::draw_rect(&surface.view, x, y, w, h, c);
    };
    const uint32_t red = 0x00123456u;
    checks.expect(draw(2, 2, 6, 4, red) == GX_OK, "full rectangle accepted");
    checks.expect(surface.at(2, 2) == 0xFF123456u && surface.at(7, 5) == 0xFF123456u,
                  "full rectangle corner pixels rendered in logical color order");
    checks.expect(surface.at(8, 5) == 0, "rectangle right edge is exclusive");
    checks.expect(draw(0, 0, 1, 1, 0x00FF0000u) == GX_OK && surface.at(0, 0) == 0xFFFF0000u,
                  "single pixel rectangle rendered");
    checks.expect(draw(8, 0, 2, 2, 0x0000FF00u) == GX_OK &&
                  draw(10, 0, 2, 2, 0x000000FFu) == GX_OK,
                  "adjacent rectangles accepted");
    checks.expect(surface.at(9, 1) == 0xFF00FF00u && surface.at(10, 1) == 0xFF0000FFu,
                  "adjacent rectangles retain their separate colors");
    checks.expect(draw(3, 3, 4, 3, 0x00A0B0C0u) == GX_OK && surface.at(4, 4) == 0xFFA0B0C0u,
                  "overlapping rectangle overwrites only covered pixels");
    checks.expect(surface.at(7, 4) == 0xFF123456u, "overlap leaves neighboring pixel intact");
    checks.expect(draw(0, 0, 0, 2, red) == GX_ERROR_INVALID_ARGUMENT, "zero width rejected");
    checks.expect(draw(0, 0, 2, 0, red) == GX_ERROR_INVALID_ARGUMENT, "zero height rejected");
    checks.expect(draw(0, 0, -1, 2, red) == GX_ERROR_INVALID_ARGUMENT, "negative width rejected");
    checks.expect(draw(0, 0, 2, -1, red) == GX_ERROR_INVALID_ARGUMENT, "negative height rejected");

    surface.clear(0);
    checks.expect(draw(-2, 3, 5, 2, red) == GX_OK, "left-clipped rectangle accepted");
    checks.expect(surface.at(0, 3) == 0xFF123456u && surface.at(2, 3) == 0xFF123456u &&
                  surface.at(3, 3) == 0, "left clipping preserves the expected two columns");
    checks.expect(draw(10, 3, 8, 2, 0x0000FF00u) == GX_OK, "right-clipped rectangle accepted");
    checks.expect(surface.at(11, 3) == 0xFF00FF00u && surface.at(9, 3) == 0,
                  "right clipping stops at surface width");
    checks.expect(draw(4, -2, 2, 5, 0x000000FFu) == GX_OK, "top-clipped rectangle accepted");
    checks.expect(surface.at(4, 0) == 0xFF0000FFu && surface.at(4, 2) == 0xFF0000FFu &&
                  surface.at(4, 3) == 0, "top clipping preserves the visible extent");
    checks.expect(draw(4, 8, 2, 5, 0x00FFFF00u) == GX_OK, "bottom-clipped rectangle accepted");
    checks.expect(surface.at(4, 9) == 0xFFFFFF00u && surface.at(4, 7) == 0,
                  "bottom clipping stops at surface height");
    const std::vector<uint32_t> before = surface.allocation;
    checks.expect(draw(-9, -4, 2, 2, red) == GX_OK && draw(12, 10, 4, 4, red) == GX_OK,
                  "fully clipped rectangles are harmless no-ops");
    checks.expect(surface.allocation == before, "fully clipped rectangles write no pixels");
    checks.expect(draw(std::numeric_limits<int>::max(), 0, std::numeric_limits<int>::max(), 1, red) == GX_OK,
                  "large positive coordinate arithmetic remains bounded");
    checks.expect(draw(std::numeric_limits<int>::min(), 0, std::numeric_limits<int>::max(), 1, red) == GX_OK,
                  "large negative coordinate arithmetic remains bounded");
    checks.expect(surface.guards_intact(), "rectangle rendering preserves allocation canaries and pitch padding");

    kernel::gui_render::Surface invalid = surface.view;
    invalid.stridePixels = invalid.width - 1u;
    checks.expect(kernel::gui_render::draw_rect(&invalid, 0, 0, 1, 1, red) == GX_ERROR_INVALID_ARGUMENT,
                  "invalid surface stride rejected");
    invalid = surface.view;
    invalid.storagePixels = 1;
    checks.expect(kernel::gui_render::draw_rect(&invalid, 0, 0, 1, 1, red) == GX_ERROR_INVALID_ARGUMENT,
                  "surface capacity shorter than geometry rejected");
    return checks.failed == 0;
}

bool test_text(Checks& checks) {
    TestSurface surface(36, 14, 43);
    const uint32_t background = 0xFF274563u;
    surface.clear(background);
    checks.expect(kernel::gui_render::draw_text(&surface.view, 2, 2, "A") == GX_OK,
                  "single ASCII character accepted");
    const uint8_t expectedA[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
    uint32_t observedGlyphPixels = 0;
    bool glyphMatches = true;
    for (uint32_t column = 0; column < 5; ++column) {
        for (uint32_t row = 0; row < 7; ++row) {
            const bool expected = (expectedA[column] & (1u << row)) != 0;
            const bool actual = surface.at(2 + column, 2 + row) == 0xFFFFFFFFu;
            glyphMatches &= expected == actual;
            if (actual) ++observedGlyphPixels;
        }
    }
    checks.expect(glyphMatches && observedGlyphPixels == 18,
                  "A glyph has the independent 5x7 bitmap footprint");
    checks.expect(surface.at(7, 2) == background && surface.at(1, 2) == background,
                  "text preserves bytes outside glyph coverage");
    checks.expect(kernel::gui_render::draw_text(&surface.view, 10, 1, "Az09") == GX_OK,
                  "multiple letters and digits accepted");
    checks.expect(surface.at(10, 2) == 0xFFFFFFFFu || surface.at(10, 1) == 0xFFFFFFFFu,
                  "multi-character text paints visible pixels");

    surface.clear(background);
    checks.expect(kernel::gui_render::draw_text(&surface.view, 1, 3, " ") == GX_OK &&
                  surface.all_pixels(background), "whitespace advances without painting pixels");
    checks.expect(kernel::gui_render::draw_text(&surface.view, 1, 3, "  A") == GX_OK &&
                  surface.at(13, 4) == 0xFFFFFFFFu,
                  "spaces preserve the six-pixel glyph advance");
    surface.clear(background);
    checks.expect(kernel::gui_render::draw_text(&surface.view, 0, 0, "") == GX_OK &&
                  surface.all_pixels(background), "empty string is a safe no-op");
    checks.expect(kernel::gui_render::draw_text(&surface.view, 0, 0, nullptr) == GX_ERROR_INVALID_ARGUMENT,
                  "null text pointer rejected");

    surface.clear(background);
    checks.expect(kernel::gui_render::draw_text(&surface.view, -3, 2, "A") == GX_OK,
                  "left-clipped text accepted");
    checks.expect(surface.at(0, 2) == 0xFFFFFFFFu && surface.at(1, 2) == background,
                  "left text clipping keeps only in-bounds glyph pixels");
    surface.clear(background);
    checks.expect(kernel::gui_render::draw_text(&surface.view, 34, 2, "A") == GX_OK,
                  "right-clipped text accepted");
    checks.expect(surface.at(34, 3) == 0xFFFFFFFFu && surface.at(35, 2) == 0xFFFFFFFFu,
                  "right text clipping writes the visible glyph fragment");
    surface.clear(background);
    checks.expect(kernel::gui_render::draw_text(&surface.view, 2, -4, "A") == GX_OK &&
                  surface.at(2, 0) == 0xFFFFFFFFu, "top-clipped text accepted");
    surface.clear(background);
    checks.expect(kernel::gui_render::draw_text(&surface.view, 2, 12, "A") == GX_OK &&
                  surface.at(4, 12) == 0xFFFFFFFFu, "bottom-clipped text accepted");
    surface.clear(background);
    checks.expect(kernel::gui_render::draw_text(&surface.view, 100, 2, "A") == GX_OK &&
                  surface.all_pixels(background), "fully outside text is a safe no-op");

    std::string longText(255, 'A');
    TestSurface longSurface(1531, 8, 1537);
    checks.expect(kernel::gui_render::draw_text(&longSurface.view, 0, 0, longText.c_str()) == GX_OK,
                  "maximum bounded ASCII string accepted");
    checks.expect(longSurface.at(1524, 1) == 0xFFFFFFFFu,
                  "long string draws its final bounded glyph");
    std::vector<char> unterminated(256, 'A');
    checks.expect(kernel::gui_render::draw_text(&longSurface.view, 0, 0, unterminated.data()) ==
                  GX_ERROR_INVALID_ARGUMENT, "unterminated maximum-length string rejected");

    TestSurface first(36, 14, 40);
    TestSurface second(36, 14, 40);
    first.clear(0xFF203040u);
    second.clear(0xFF203040u);
    kernel::gui_render::draw_text(&first.view, 1, 1, "Text 42");
    kernel::gui_render::draw_text(&second.view, 1, 1, "Text 42");
    checks.expect(first.allocation == second.allocation, "repeated text rendering is deterministic");
    checks.expect(first.at(2, 1) == 0xFFFFFFFFu && first.at(0, 1) == 0xFF203040u,
                  "text uses opaque white and does not alter neighboring background");
    checks.expect(first.guards_intact() && second.guards_intact() && longSurface.guards_intact(),
                  "text preserves surface canaries and row padding");
    return checks.failed == 0;
}

bool test_host_publication_and_isolation(Checks& checks) {
    kernel::gui_render::SurfaceRegistry registry;
    const gx_handle firstHandle = 0x101;
    const gx_handle secondHandle = 0x202;
    checks.expect(registry.create(firstHandle, 20, 12) && registry.create(secondHandle, 20, 12),
                  "production surface registry creates independent app windows");
    kernel::gui_render::Surface first{};
    kernel::gui_render::Surface second{};
    registry.resolve(firstHandle, &first);
    registry.resolve(secondHandle, &second);
    kernel::gui_render::draw_rect(&first, 0, 0, 20, 12, 0x00101010u);
    kernel::gui_render::draw_rect(&second, 0, 0, 20, 12, 0x00202020u);
    Owner owner;
    owner.handles[0] = firstHandle;
    owner.handles[1] = secondHandle;
    owner.registry = &registry;
    owner.count = 2;

    gx_host_calls host{};
    host.size = sizeof(host);
    host.version = GX_API_VERSION;
    host.wait_event = sentinel_wait;
    host.bare_metal_build_project_start = sentinel_build;
    host.bare_metal_development_run_release = sentinel_run_release;
    host.guiVersion = GX_GUI_ABI_VERSION;
    gx_app_context context{};
    kernel::gui_render::HostBinding binding{};
    checks.expect(!kernel::gui_render::publish_host_callbacks(&host, &binding, &context) &&
                  host.draw_rect == nullptr && host.draw_text == nullptr,
                  "publisher withholds callbacks until a valid app context exists");
    bind_context(&binding, &context, &host, &owner, &owner);
    checks.expect(kernel::gui_render::publish_host_callbacks(&host, &binding, &context) &&
                  host.draw_rect != nullptr && host.draw_text != nullptr,
                  "production publisher populates callbacks for a valid app context");
    checks.expect(host.wait_event == sentinel_wait && host.bare_metal_build_project_start == sentinel_build &&
                  host.bare_metal_development_run_release == sentinel_run_release && host.guiVersion == GX_GUI_ABI_VERSION,
                  "drawing callback publication preserves adjacent Phase 11, Phase 12 and GUI fields");
    checks.expect(kernel::gui_render::binding_valid(&binding, &context), "valid drawing context accepted");
    checks.expect(host.draw_rect(&context, firstHandle, 2, 3, 5, 4, 0x00C04020u) == GX_OK,
                  "published rect callback changes first application surface");
    checks.expect(host.draw_text(&context, firstHandle, 3, 4, "IDE") == GX_OK,
                  "published text callback changes first application surface");
    checks.expect(first.pixels[3 * first.stridePixels + 2] == 0xFFC04020u &&
                  first.pixels[4 * first.stridePixels + 4] == 0xFFFFFFFFu,
                  "host table callbacks produce expected rectangle and glyph pixels");
    bool secondUntouched = true;
    for (uint32_t y = 0; y < second.height; ++y)
        for (uint32_t x = 0; x < second.width; ++x)
            secondUntouched &= second.pixels[y * second.stridePixels + x] == 0xFF202020u;
    checks.expect(secondUntouched, "first application's callbacks do not modify second surface");
    checks.expect(host.draw_rect(&context, secondHandle, 1, 1, 2, 2, 0x0000FF00u) == GX_OK &&
                  second.pixels[second.stridePixels + 1] == 0xFF00FF00u &&
                  first.pixels[first.stridePixels + 1] == 0xFF101010u,
                  "second application draws only into its own surface");
    checks.expect(host.draw_rect(&context, 0x303, 0, 0, 2, 2, 0x00FFFFFFu) == GX_ERROR_INVALID_ARGUMENT,
                  "foreign or stale window handle rejected");
    checks.expect(host.draw_rect(nullptr, owner.handles[0], 0, 0, 1, 1, 0) == GX_ERROR_INVALID_ARGUMENT,
                  "missing application context rejected");
    checks.expect(host.draw_rect(&context, owner.handles[0], 0, 0, 0, 1, 0) == GX_ERROR_INVALID_ARGUMENT,
                  "published callback rejects invalid rectangle dimensions");
    checks.expect(host.draw_text(&context, owner.handles[0], 0, 0, nullptr) == GX_ERROR_INVALID_ARGUMENT,
                  "published callback rejects null text");
    binding.active = 0;
    checks.expect(host.draw_rect(&context, owner.handles[0], 0, 0, 1, 1, 0) == GX_ERROR_INVALID_ARGUMENT,
                  "inactive application context cannot reuse published callbacks");
    checks.expect(owner.invalidations >= 3, "successful callbacks request compositor repaint");
    checks.expect(registry.guards_intact(firstHandle) && registry.guards_intact(secondHandle),
                  "host path preserves independent production surface guards");
    checks.expect(registry.destroy(firstHandle) && registry.destroy(secondHandle) &&
                  !registry.resolve(firstHandle, &first) && !registry.resolve(secondHandle, &second),
                  "production registry invalidates destroyed window surfaces");
    return checks.failed == 0;
}

bool test_lifecycle(Checks& checks) {
    gx_host_calls host{};
    host.size = sizeof(host);
    gx_app_context context{};
    kernel::gui_render::HostBinding binding{};
    for (uint32_t iteration = 1; iteration <= 100; ++iteration) {
        kernel::gui_render::SurfaceRegistry registry;
        Owner owner;
        const gx_handle handle = (static_cast<gx_handle>(iteration) << 32) | 1u;
        checks.expect(registry.create(handle, 18, 10), "production lifecycle surface created");
        kernel::gui_render::Surface surface{};
        registry.resolve(handle, &surface);
        kernel::gui_render::draw_rect(&surface, 0, 0, 18, 10, 0x00080808u);
        owner.handles[0] = handle;
        owner.registry = &registry;
        owner.count = 1;
        bind_context(&binding, &context, &host, &owner, &owner);
        checks.expect(kernel::gui_render::publish_host_callbacks(&host, &binding, &context),
                      "lifecycle callbacks publish only after app context setup");
        checks.expect(host.draw_rect(&context, handle, 1, 1, 4, 3, 0x0000A0F0u) == GX_OK,
                      "lifecycle rectangle callback succeeds");
        checks.expect(host.draw_text(&context, handle, 2, 2, "R1") == GX_OK,
                      "lifecycle text callback succeeds");
        checks.expect(surface.pixels[surface.stridePixels + 1] == 0xFF00A0F0u &&
                      surface.pixels[2 * surface.stridePixels + 2] == 0xFFFFFFFFu,
                      "lifecycle pixels verified before context destruction");
        checks.expect(registry.guards_intact(handle) && registry.destroy(handle),
                      "production lifecycle surface destroys with intact guards");
        binding.active = 0;
        checks.expect(host.draw_rect(&context, handle, 0, 0, 2, 2, 0x00FFFFFFu) == GX_ERROR_INVALID_ARGUMENT,
                      "destroyed lifecycle context rejected");
        checks.expect(!registry.resolve(handle, &surface), "destroyed production surface has no stale lookup");

        kernel::gui_render::SurfaceRegistry replacementRegistry;
        Owner replacementOwner;
        const gx_handle replacementHandle = (static_cast<gx_handle>(iteration + 100) << 32) | 1u;
        checks.expect(replacementRegistry.create(replacementHandle, 18, 10),
                      "recreated production lifecycle surface allocated");
        kernel::gui_render::Surface replacement{};
        replacementRegistry.resolve(replacementHandle, &replacement);
        kernel::gui_render::draw_rect(&replacement, 0, 0, 18, 10, 0x00080808u);
        replacementOwner.handles[0] = replacementHandle;
        replacementOwner.registry = &replacementRegistry;
        replacementOwner.count = 1;
        bind_context(&binding, &context, &host, &replacementOwner, &replacementOwner);
        checks.expect(host.draw_rect(&context, replacementHandle, 2, 3, 2, 2, 0x0000FF00u) == GX_OK &&
                      host.draw_text(&context, replacementHandle, 5, 1, "A") == GX_OK,
                      "recreated lifecycle context draws through published table");
        checks.expect(replacement.pixels[3 * replacement.stridePixels + 2] == 0xFF00FF00u &&
                      replacement.pixels[replacement.stridePixels + 7] == 0xFFFFFFFFu,
                      "recreated context writes replacement pixels");
        checks.expect(host.draw_rect(&context, handle, 0, 0, 2, 2, 0x00FFFFFFu) == GX_ERROR_INVALID_ARGUMENT,
                      "previous generation handle remains stale after recreation");
        checks.expect(replacementRegistry.guards_intact(replacementHandle),
                      "recreated production surface retains allocation guards");
        binding.active = 0;
    }
    return checks.failed == 0;
}

bool test_demo(Checks& checks, const std::string& outputPath, uint64_t* outHash) {
    TestSurface surface(320, 200, 327);
    surface.clear(0xFF141E2Au);
    Owner owner;
    owner.handles[0] = 0x4455;
    owner.surfaces[0] = &surface;
    owner.count = 1;
    gx_host_calls host{};
    host.size = sizeof(host);
    gx_app_context context{};
    kernel::gui_render::HostBinding binding{};
    bind_context(&binding, &context, &host, &owner, &owner);
    bool pass = kernel::gui_render::publish_host_callbacks(&host, &binding, &context);
    pass &= host.draw_rect(&context, 0x4455, 0, 0, 320, 200, 0x00141E2Au) == GX_OK;
    pass &= host.draw_rect(&context, 0x4455, 20, 28, 280, 154, 0x002D394Cu) == GX_OK;
    pass &= host.draw_rect(&context, 0x4455, 20, 28, 280, 26, 0x00436D9Bu) == GX_OK;
    pass &= host.draw_rect(&context, 0x4455, 188, 143, 92, 27, 0x005A8CCBu) == GX_OK;
    pass &= host.draw_text(&context, 0x4455, 31, 37, "Developer Studio") == GX_OK;
    pass &= host.draw_text(&context, 0x4455, 35, 72, "Project: Render Demo") == GX_OK;
    pass &= host.draw_text(&context, 0x4455, 35, 98, "Target: ARM64") == GX_OK;
    pass &= host.draw_text(&context, 0x4455, 213, 152, "Build") == GX_OK;
    checks.expect(pass, "IDE-like demonstration rendered using published host callbacks");
    checks.expect(surface.at(0, 0) == 0xFF141E2Au, "demo background pixel probe");
    checks.expect(surface.at(25, 60) == 0xFF2D394Cu, "demo panel pixel probe");
    checks.expect(surface.at(25, 30) == 0xFF436D9Bu, "demo title-bar pixel probe");
    checks.expect(surface.at(190, 145) == 0xFF5A8CCBu, "demo button pixel probe");
    checks.expect(surface.at(31, 37) == 0xFFFFFFFFu, "demo title glyph footprint probe");
    checks.expect(surface.at(35, 72) == 0xFFFFFFFFu && surface.at(35, 98) == 0xFFFFFFFFu,
                  "demo label glyph probes");

    uint32_t panelPixels = 0;
    uint32_t glyphPixels = 0;
    for (uint32_t y = 28; y < 182; ++y) {
        for (uint32_t x = 20; x < 300; ++x) {
            panelPixels += surface.at(x, y) == 0xFF2D394Cu;
            glyphPixels += surface.at(x, y) == 0xFFFFFFFFu;
        }
    }
    checks.expect(panelPixels > 30000, "demo panel region has expected coverage");
    checks.expect(glyphPixels > 150, "demo contains multiple non-block glyph footprints");
    checks.expect(surface.guards_intact(), "demo preserves surface canaries and pitch padding");

    TestSurface repeat(320, 200, 327);
    repeat.clear(0xFF141E2Au);
    Owner repeatOwner;
    repeatOwner.handles[0] = 0x4455;
    repeatOwner.surfaces[0] = &repeat;
    repeatOwner.count = 1;
    gx_host_calls repeatHost{};
    repeatHost.size = sizeof(repeatHost);
    gx_app_context repeatContext{};
    kernel::gui_render::HostBinding repeatBinding{};
    bind_context(&repeatBinding, &repeatContext, &repeatHost, &repeatOwner, &repeatOwner);
    pass &= kernel::gui_render::publish_host_callbacks(&repeatHost, &repeatBinding, &repeatContext);
    const int rects[][6] = {
        {0, 0, 320, 200, 0x141E2A, 0}, {20, 28, 280, 154, 0x2D394C, 0},
        {20, 28, 280, 26, 0x436D9B, 0}, {188, 143, 92, 27, 0x5A8CCB, 0}
    };
    for (const auto& rect : rects)
        repeatHost.draw_rect(&repeatContext, 0x4455, rect[0], rect[1], rect[2], rect[3],
                             static_cast<uint32_t>(rect[4]));
    repeatHost.draw_text(&repeatContext, 0x4455, 31, 37, "Developer Studio");
    repeatHost.draw_text(&repeatContext, 0x4455, 35, 72, "Project: Render Demo");
    repeatHost.draw_text(&repeatContext, 0x4455, 35, 98, "Target: ARM64");
    repeatHost.draw_text(&repeatContext, 0x4455, 213, 152, "Build");
    const uint64_t hash = buffer_hash(surface);
    checks.expect(hash == buffer_hash(repeat), "demo full-buffer hash is deterministic across redraw");
    checks.expect(hash != buffer_hash(TestSurface(320, 200, 327)), "demo hash differs from an untouched buffer");
    if (outHash) *outHash = hash;
    if (!outputPath.empty()) checks.expect(write_ppm(outputPath, surface), "demo PPM artifact written");
    return checks.failed == 0;
}

} // namespace

int main(int argc, char** argv) {
    Checks checks;
    test_rectangles(checks);
    test_text(checks);
    test_host_publication_and_isolation(checks);
    test_lifecycle(checks);
    uint64_t demoHash = 0;
    const std::string outputPath = argc > 1 ? argv[1] : std::string();
    test_demo(checks, outputPath, &demoHash);
    if (checks.failed != 0) {
        std::cerr << "AARCH64_RENDER_HOST_FAIL checks=" << checks.total << " failures=" << checks.failed << '\n';
        return 1;
    }
    std::cout << "AARCH64_RENDER_HOST_PASS checks=" << checks.total
              << " lifecycle=100/100 surfaces=independent callbacks=published\n";
    std::cout << "demo=320x200 RGB888 P6 fnv1a64=0x" << std::hex << demoHash << std::dec;
    if (!outputPath.empty()) std::cout << " path=" << outputPath;
    std::cout << '\n';
    return 0;
}
