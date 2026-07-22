#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include "ipc.h"
#include <sstream>
namespace gxos {
    namespace gui {
        static const uint32_t kGuiProtocolVersion = 3;
        enum class MsgType : uint32_t {
            MT_None = 0,
            MT_Create = 1,
            MT_Close = 2,
            MT_DrawText = 3,
            MT_RequestFrame = 4,
            MT_Ping = 5,
            MT_Move = 6,
            MT_Resize = 7,
            MT_SetTitle = 8,
            MT_DrawRect = 9,
            MT_FramePresent = 10,
            MT_Invalidate = 11,
            MT_InputKey = 12,
            MT_InputMouse = 13,
            MT_SetFocus = 14,
            MT_WidgetAdd = 15,
            MT_WidgetEvt = 16,
            MT_WindowList = 17,
            MT_Activate = 18,
            MT_Minimize = 19,
            // New desktop parity messages
            MT_ShowDesktopToggle = 20,   // payload empty
            MT_StateSave = 21,           // force save state
            MT_StateLoad = 22,           // force reload state (will close existing and load)
            MT_DesktopLaunch = 23,       // payload: action string
            MT_DesktopPins = 24,         // payload: +ACTION;-ACTION;... (semicol separated)
            MT_DesktopWallpaperSet = 25, // payload: path string
            MT_DrawImage = 26,           // payload: <winId>|<x>|<y>|<path>
            MT_DrawTextAt = 27,          // payload: <winId>|<x>|<y>|<text>
            MT_DesktopConfigReload = 28, // payload empty; reload desktop.json and rebuild desktop items
            MT_DrawTextAtColor = 29,     // payload: <winId>|<x>|<y>|<r>|<g>|<b>|<text>
            MT_DrawImageAnimated = 30,   // payload: DrawImageSpec, path contains {frame}
            MT_WidgetSetIcon = 31,       // payload: <winId>|<widgetId>|<path>
            MT_DesktopBackgroundInventoryChanged = 32, // payload: active background id
            MT_ClearFocus = 33                 // payload: window id losing focus
        };
        struct WindowDesc { uint64_t id; std::string title; int w; int h; };
        struct Rect { int x; int y; int w; int h; };
        struct KeyEvent { int keyCode; bool down; };
        struct MouseEvent { int x; int y; int dx; int dy; uint32_t buttons; };
        struct DrawImageSpec { uint64_t winId; int x; int y; int w; int h; std::string path; };
        struct FramePresentSpec {
            uint64_t winId{0};
            int x{0};
            int y{0};
            int w{0};
            int h{0};
            uint32_t strideBytes{0};
            uint32_t pixelFormat{0};
            std::vector<uint8_t> pixels;
        };
        constexpr uint32_t kFramePresentMagic = 0x31465847u; // "GXF1"
        constexpr uint32_t kFramePresentVersion = 1u;
        constexpr uint32_t kPixelFormatXrgb8888 = 1u;
        constexpr uint32_t kWindowCreateFlagResizable = 1u << 0;
        constexpr uint32_t kWindowCreateFlagCentered = 1u << 1;
        inline std::vector<uint8_t> packString(const std::string& s) { return std::vector<uint8_t>(s.begin( ), s.end( )); }
        inline std::string unpackString(const std::vector<uint8_t>& d) { return std::string(d.begin( ), d.end( )); }

        // Pack a list of pin/unpin actions using the protocol: "+action;-action;..."
        inline std::string packPins(const std::vector<std::pair<bool, std::string>>& ops) { // true=pin, false=unpin
            std::ostringstream oss;
            bool first = true;
            for (auto& p : ops) {
                if (!first)
                    oss << ";"; first = false;
                oss << (p.first ? '+' : '-') << p.second;
            }
            return oss.str( );
        }
        // Unpack pins string into pairs (true=pin, false=unpin)
        inline std::vector<std::pair<bool, std::string>> unpackPins(const std::string& s) {
            std::vector<std::pair<bool, std::string>> out;
            std::istringstream iss(s);
            std::string tok;
            while (std::getline(iss, tok, ';')) {
                if (tok.size( ) < 2)
                    continue;
                bool pin = (tok[0] == '+');
                out.emplace_back(pin, tok.substr(1));
            }
            return out;
        }
        // Helper for building widget add payloads: <winId>|<type>|<id>|<x>|<y>|<w>|<h>|<text>
        inline std::string packWidgetAdd(uint64_t winId, int type, int id, int x, int y, int w, int h, const std::string& text) { std::ostringstream oss; oss << winId << "|" << type << "|" << id << "|" << x << "|" << y << "|" << w << "|" << h << "|" << text; return oss.str( ); }
        inline std::string packWidgetSetIcon(uint64_t winId, int id, const std::string& path) { std::ostringstream oss; oss << winId << "|" << id << "|" << path; return oss.str( ); }
        inline std::string packDrawImage(uint64_t winId, int x, int y, const std::string& path) { std::ostringstream oss; oss << winId << "|" << x << "|" << y << "|-1|-1|" << path; return oss.str( ); }
        inline std::string packDrawImage(uint64_t winId, int x, int y, int w, int h, const std::string& path) { std::ostringstream oss; oss << winId << "|" << x << "|" << y << "|" << w << "|" << h << "|" << path; return oss.str( ); }
        inline bool unpackDrawImage(const std::string& payload, DrawImageSpec& spec) {
            std::istringstream iss(payload);
            std::string winS, xS, yS, wS, hS;
            if (!std::getline(iss, winS, '|')) return false;
            if (!std::getline(iss, xS, '|')) return false;
            if (!std::getline(iss, yS, '|')) return false;
            if (!std::getline(iss, wS, '|')) return false;
            if (!std::getline(iss, hS, '|')) return false;
            std::string path;
            if (!std::getline(iss, path)) return false;
            try {
                spec.winId = std::stoull(winS);
                spec.x = std::stoi(xS);
                spec.y = std::stoi(yS);
                spec.w = std::stoi(wS);
                spec.h = std::stoi(hS);
                spec.path = path;
                return true;
            } catch (...) {
                return false;
            }
        }
        inline void appendFrameU32(std::vector<uint8_t>& out, uint32_t value) {
            out.push_back(static_cast<uint8_t>(value & 0xFFu));
            out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
            out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
            out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
        }
        inline void appendFrameU64(std::vector<uint8_t>& out, uint64_t value) {
            appendFrameU32(out, static_cast<uint32_t>(value & 0xFFFFFFFFull));
            appendFrameU32(out, static_cast<uint32_t>(value >> 32));
        }
        inline uint32_t readFrameU32(const std::vector<uint8_t>& data, size_t offset) {
            return static_cast<uint32_t>(data[offset]) |
                (static_cast<uint32_t>(data[offset + 1]) << 8) |
                (static_cast<uint32_t>(data[offset + 2]) << 16) |
                (static_cast<uint32_t>(data[offset + 3]) << 24);
        }
        inline uint64_t readFrameU64(const std::vector<uint8_t>& data, size_t offset) {
            return static_cast<uint64_t>(readFrameU32(data, offset)) |
                (static_cast<uint64_t>(readFrameU32(data, offset + 4)) << 32);
        }
        inline std::vector<uint8_t> packFramePresent(uint64_t winId, int x, int y, int w, int h,
                                                      uint32_t strideBytes, uint32_t pixelFormat,
                                                      const void* pixels, uint32_t pixelBytes) {
            std::vector<uint8_t> out;
            out.reserve(44u + pixelBytes);
            appendFrameU32(out, kFramePresentMagic);
            appendFrameU32(out, kFramePresentVersion);
            appendFrameU64(out, winId);
            appendFrameU32(out, static_cast<uint32_t>(x));
            appendFrameU32(out, static_cast<uint32_t>(y));
            appendFrameU32(out, static_cast<uint32_t>(w));
            appendFrameU32(out, static_cast<uint32_t>(h));
            appendFrameU32(out, strideBytes);
            appendFrameU32(out, pixelFormat);
            appendFrameU32(out, pixelBytes);
            if (pixels && pixelBytes > 0) {
                const uint8_t* bytes = static_cast<const uint8_t*>(pixels);
                out.insert(out.end(), bytes, bytes + pixelBytes);
            }
            return out;
        }
        inline bool unpackFramePresent(const std::vector<uint8_t>& data, FramePresentSpec& spec) {
            constexpr size_t headerBytes = 44u;
            if (data.size() < headerBytes || readFrameU32(data, 0) != kFramePresentMagic || readFrameU32(data, 4) != kFramePresentVersion) return false;
            const uint32_t pixelBytes = readFrameU32(data, 40);
            if (pixelBytes > data.size() - headerBytes) return false;
            spec.winId = readFrameU64(data, 8);
            spec.x = static_cast<int32_t>(readFrameU32(data, 16));
            spec.y = static_cast<int32_t>(readFrameU32(data, 20));
            spec.w = static_cast<int32_t>(readFrameU32(data, 24));
            spec.h = static_cast<int32_t>(readFrameU32(data, 28));
            spec.strideBytes = readFrameU32(data, 32);
            spec.pixelFormat = readFrameU32(data, 36);
            spec.pixels.assign(data.begin() + headerBytes, data.begin() + headerBytes + pixelBytes);
            return spec.winId != 0 && spec.w > 0 && spec.h > 0 && !spec.pixels.empty();
        }
        inline std::string packDrawTextAt(uint64_t winId, int x, int y, const std::string& text) { std::ostringstream oss; oss << winId << "|" << x << "|" << y << "|" << text; return oss.str( ); }
        inline std::string packDrawTextAtColor(uint64_t winId, int x, int y, uint8_t r, uint8_t g, uint8_t b, const std::string& text) { std::ostringstream oss; oss << winId << "|" << x << "|" << y << "|" << static_cast<int>(r) << "|" << static_cast<int>(g) << "|" << static_cast<int>(b) << "|" << text; return oss.str( ); }
    }
}
