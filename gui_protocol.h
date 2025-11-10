#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ipc.h"

namespace gxos { namespace gui {
    static const uint32_t kGuiProtocolVersion = 1;

    enum class MsgType : uint32_t {
        MT_None=0,
        MT_Create=1,
        MT_Close=2,
        MT_DrawText=3,
        MT_RequestFrame=4,
        MT_Ping=5,
        MT_Move=6,
        MT_Resize=7,
        MT_SetTitle=8,
        MT_DrawRect=9,
        MT_FramePresent=10,
        MT_Invalidate=11,
        MT_InputKey=12,
        MT_InputMouse=13,
        MT_SetFocus=14,
        MT_WidgetAdd=15,
        MT_WidgetEvt=16,
        MT_WindowList=17,
        MT_Activate=18,
        MT_Minimize=19,
        // New desktop parity messages
        MT_ShowDesktopToggle=20,   // payload empty
        MT_StateSave=21,           // force save state
        MT_StateLoad=22            // force reload state (will close existing and load)
    };

    struct WindowDesc { uint64_t id; std::string title; int w; int h; };
    struct Rect { int x; int y; int w; int h; };
    struct KeyEvent { int keyCode; bool down; };
    struct MouseEvent { int x; int y; int dx; int dy; uint32_t buttons; };

    inline std::vector<uint8_t> packString(const std::string& s){ return std::vector<uint8_t>(s.begin(), s.end()); }
    inline std::string unpackString(const std::vector<uint8_t>& d){ return std::string(d.begin(), d.end()); }
} }
