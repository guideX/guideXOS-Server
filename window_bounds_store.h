#pragma once

#include <string>

namespace gxos { namespace gui {

    struct NormalWindowBounds {
        int x{0};
        int y{0};
        int w{0};
        int h{0};
    };

    /// Small bounded user-state store for normal/restored window geometry.
    /// Keys are stable application/window identities, never transient titles.
    class WindowBoundsStore {
    public:
        static bool Load(const std::string& key, NormalWindowBounds& out);
        static bool Save(const std::string& key, const NormalWindowBounds& bounds, std::string& error);
    };

}} // namespace gxos::gui
