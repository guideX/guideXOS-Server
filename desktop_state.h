#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace gxos { namespace gui {
    // snap: 0=none,1=left,2=right,3=tl,4=tr,5=bl,6=br
    struct SavedWindow { uint64_t id; std::string title; int x; int y; int w; int h; bool minimized; bool maximized{false}; int z{0}; bool focused{false}; int snap{0}; };
    class DesktopState {
    public:
        static bool Save(const std::string& path, const std::vector<SavedWindow>& wins, std::string& err);
        static bool Load(const std::string& path, std::vector<SavedWindow>& wins, std::string& err);
    };
} }
