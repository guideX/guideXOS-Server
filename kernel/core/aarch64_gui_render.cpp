#include "include/kernel/aarch64_gui_render.h"

#include "bitmap_font.h"

static_assert(sizeof(int) == 4, "AARCH64 GUI callbacks require 32-bit int arguments");
static_assert(offsetof(gx_host_calls, draw_text) == 32, "AARCH64 draw_text slot moved");
static_assert(offsetof(gx_host_calls, draw_rect) == 40, "AARCH64 draw_rect slot moved");
static_assert(offsetof(gx_host_calls, guiVersion) == 240, "AARCH64 GUI version slot moved");
static_assert(offsetof(gx_host_calls, bare_metal_build_project_start) == 288,
              "AARCH64 build callback slot moved");
static_assert(offsetof(gx_host_calls, bare_metal_development_run_release) == 392,
              "AARCH64 final Run callback slot moved");
static_assert(sizeof(gx_host_calls) == 400, "AARCH64 host-call table size changed");

namespace kernel {
namespace gui_render {

static const uint32_t kMaxTextBytes = GX_ABI_MAX_STRING_BYTES;
static const uint32_t kOpaqueWhite = 0xFFFFFFFFu;
static const uint32_t kOpaqueAlpha = 0xFF000000u;
static const uint32_t kSurfaceLeadingCanary = 0x51AFC0DEu;
static const uint32_t kSurfaceTrailingCanary = 0xFACE71E5u;
static const uint64_t kMaxSurfaceBytes = UINT64_C(16) * 1024u * 1024u;

SurfaceRegistry::SurfaceRegistry() : m_entries{}
{
}

SurfaceRegistry::~SurfaceRegistry()
{
    for (uint32_t i = 0; i < kCapacity; ++i) {
        if (m_entries[i].allocation) delete[] m_entries[i].allocation;
        m_entries[i] = {};
    }
}

bool SurfaceRegistry::create(gx_handle window, uint32_t width, uint32_t height)
{
    if (window == 0 || width == 0 || height == 0) return false;
    uint32_t freeIndex = kCapacity;
    for (uint32_t i = 0; i < kCapacity; ++i) {
        if (m_entries[i].window == window) return false;
        if (!m_entries[i].allocation && freeIndex == kCapacity) freeIndex = i;
    }
    if (freeIndex == kCapacity) return false;

    const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
    if (pixelCount == 0 || pixelCount > (kMaxSurfaceBytes / sizeof(uint32_t)) ||
        pixelCount > UINT32_MAX - 2u) return false;
    uint32_t* allocation = new uint32_t[static_cast<uint32_t>(pixelCount) + 2u];
    if (!allocation) return false;
    allocation[0] = kSurfaceLeadingCanary;
    allocation[pixelCount + 1u] = kSurfaceTrailingCanary;
    for (uint64_t i = 0; i < pixelCount; ++i) allocation[i + 1u] = kOpaqueAlpha;

    Entry& entry = m_entries[freeIndex];
    entry.window = window;
    entry.allocation = allocation;
    entry.pixelCount = pixelCount;
    entry.surface.pixels = allocation + 1u;
    entry.surface.width = width;
    entry.surface.height = height;
    entry.surface.stridePixels = width;
    entry.surface.storagePixels = pixelCount;
    return true;
}

bool SurfaceRegistry::destroy(gx_handle window)
{
    if (window == 0) return false;
    for (uint32_t i = 0; i < kCapacity; ++i) {
        Entry& entry = m_entries[i];
        if (entry.window != window || !entry.allocation) continue;
        delete[] entry.allocation;
        entry = {};
        return true;
    }
    return false;
}

bool SurfaceRegistry::resolve(gx_handle window, Surface* outSurface) const
{
    if (window == 0 || !outSurface) return false;
    for (uint32_t i = 0; i < kCapacity; ++i) {
        const Entry& entry = m_entries[i];
        if (entry.window == window && entry.allocation && surface_valid(entry.surface)) {
            *outSurface = entry.surface;
            return true;
        }
    }
    return false;
}

bool SurfaceRegistry::guards_intact(gx_handle window) const
{
    for (uint32_t i = 0; i < kCapacity; ++i) {
        const Entry& entry = m_entries[i];
        if (entry.window == window && entry.allocation)
            return entry.allocation[0] == kSurfaceLeadingCanary &&
                entry.allocation[entry.pixelCount + 1u] == kSurfaceTrailingCanary;
    }
    return false;
}

bool surface_valid(const Surface& surface)
{
    if (!surface.pixels || surface.width == 0 || surface.height == 0 ||
        surface.stridePixels < surface.width) return false;
    const uint64_t lastRow = static_cast<uint64_t>(surface.height - 1u) * surface.stridePixels;
    if (lastRow > UINT64_MAX - surface.width) return false;
    return lastRow + surface.width <= surface.storagePixels;
}

bool binding_valid(const HostBinding* binding, const gx_app_context* context)
{
    return binding && binding->active != 0 && binding->runtime && context &&
        binding->context == context && binding->host && context->host == binding->host &&
        context->userData == binding && binding->owner && binding->resolveSurface &&
        binding->invalidateSurface;
}

gx_result draw_rect(Surface* surface, int x, int y, int width, int height, uint32_t color)
{
    if (!surface || !surface_valid(*surface) || width <= 0 || height <= 0)
        return GX_ERROR_INVALID_ARGUMENT;

    const int64_t right = static_cast<int64_t>(x) + static_cast<int64_t>(width);
    const int64_t bottom = static_cast<int64_t>(y) + static_cast<int64_t>(height);
    const int64_t leftClip = x < 0 ? 0 : x;
    const int64_t topClip = y < 0 ? 0 : y;
    const int64_t rightClip = right > surface->width ? surface->width : right;
    const int64_t bottomClip = bottom > surface->height ? surface->height : bottom;
    if (leftClip >= rightClip || topClip >= bottomClip) return GX_OK;

    const uint32_t pixel = kOpaqueAlpha | (color & 0x00FFFFFFu);
    for (int64_t row = topClip; row < bottomClip; ++row) {
        const uint64_t offset = static_cast<uint64_t>(row) * surface->stridePixels +
            static_cast<uint64_t>(leftClip);
        uint32_t* destination = surface->pixels + offset;
        for (int64_t column = leftClip; column < rightClip; ++column)
            *destination++ = pixel;
    }
    return GX_OK;
}

gx_result draw_text(Surface* surface, int x, int y, const char* text)
{
    if (!surface || !surface_valid(*surface) || !text) return GX_ERROR_INVALID_ARGUMENT;

    uint32_t length = 0;
    while (length < kMaxTextBytes && text[length] != '\0') ++length;
    if (length == kMaxTextBytes) return GX_ERROR_INVALID_ARGUMENT;

    int64_t cursorX = x;
    for (uint32_t index = 0; index < length; ++index) {
        const uint8_t* glyph = gxos::gui::BitmapFont::Glyph(text[index]);
        if (glyph) {
            for (int32_t column = 0; column < gxos::gui::BitmapFont::kGlyphW; ++column) {
                const uint8_t bits = glyph[column];
                for (int32_t row = 0; row < gxos::gui::BitmapFont::kGlyphH; ++row) {
                    if ((bits & (1u << row)) == 0) continue;
                    const int64_t destX = cursorX + column;
                    const int64_t destY = static_cast<int64_t>(y) + row;
                    if (destX < 0 || destY < 0 || destX >= surface->width || destY >= surface->height)
                        continue;
                    const uint64_t offset = static_cast<uint64_t>(destY) * surface->stridePixels +
                        static_cast<uint64_t>(destX);
                    surface->pixels[offset] = kOpaqueWhite;
                }
            }
        }
        cursorX += gxos::gui::BitmapFont::kGlyphW + gxos::gui::BitmapFont::kSpacing;
    }
    return GX_OK;
}

static HostBinding* host_binding(gx_app_context* context)
{
    if (!context || !context->userData) return nullptr;
    HostBinding* binding = static_cast<HostBinding*>(context->userData);
    return binding_valid(binding, context) ? binding : nullptr;
}

gx_result GX_CALL host_draw_rect(gx_app_context* context, gx_handle window,
                                 int x, int y, int width, int height, uint32_t color)
{
    HostBinding* binding = host_binding(context);
    if (!binding || window == 0) return GX_ERROR_INVALID_ARGUMENT;
    Surface surface{};
    if (!binding->resolveSurface(binding->owner, window, &surface) || !surface_valid(surface))
        return GX_ERROR_INVALID_ARGUMENT;
    const gx_result result = draw_rect(&surface, x, y, width, height, color);
    if (result == GX_OK) binding->invalidateSurface(binding->owner, window);
    return result;
}

gx_result GX_CALL host_draw_text(gx_app_context* context, gx_handle window,
                                 int x, int y, const char* text)
{
    HostBinding* binding = host_binding(context);
    if (!binding || window == 0 || !text) return GX_ERROR_INVALID_ARGUMENT;
    Surface surface{};
    if (!binding->resolveSurface(binding->owner, window, &surface) || !surface_valid(surface))
        return GX_ERROR_INVALID_ARGUMENT;
    const gx_result result = draw_text(&surface, x, y, text);
    if (result == GX_OK) binding->invalidateSurface(binding->owner, window);
    return result;
}

bool publish_host_callbacks(gx_host_calls* host, const HostBinding* binding,
                            const gx_app_context* context)
{
    if (!host || !binding_valid(binding, context) || binding->host != host) return false;
    host->draw_text = host_draw_text;
    host->draw_rect = host_draw_rect;
    return true;
}

} // namespace gui_render
} // namespace kernel
