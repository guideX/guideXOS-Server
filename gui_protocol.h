#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ipc.h"
#include <sstream>

namespace gxos { namespace gui {
    static const uint32_t kGuiProtocolVersion = 2;

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
        MT_StateLoad=22,           // force reload state (will close existing and load)
        MT_DesktopLaunch=23,       // payload: action string
        MT_DesktopPins=24,         // payload: +ACTION;-ACTION;... (semicol separated)
        MT_DesktopWallpaperSet=25  // payload: path string
    };

    struct WindowDesc { uint64_t id; std::string title; int w; int h; };
    struct Rect { int x; int y; int w; int h; };
    struct KeyEvent { int keyCode; bool down; };
    struct MouseEvent { int x; int y; int dx; int dy; uint32_t buttons; };

    inline std::vector<uint8_t> packString(const std::string& s){ return std::vector<uint8_t>(s.begin(), s.end()); }
    inline std::string unpackString(const std::vector<uint8_t>& d){ return std::string(d.begin(), d.end()); }

    // Pack a list of pin/unpin actions using the protocol: "+action;-action;..."
    inline std::string packPins(const std::vector<std::pair<bool,std::string>>& ops){ // true=pin, false=unpin
        std::ostringstream oss;
        bool first=true;
        for(auto &p: ops){ if(!first) oss << ";"; first=false; oss << (p.first?'+':'-') << p.second; }
        return oss.str();
    }
    // Unpack pins string into pairs (true=pin, false=unpin)
    inline std::vector<std::pair<bool,std::string>> unpackPins(const std::string& s){ std::vector<std::pair<bool,std::string>> out; std::istringstream iss(s); std::string tok; while(std::getline(iss,tok,';')){ if(tok.size()<2) continue; bool pin = (tok[0]=='+'); out.emplace_back(pin, tok.substr(1)); } return out; }

    // Helper for building widget add payloads: <winId>|<type>|<id>|<x>|<y>|<w>|<h>|<text>
    inline std::string packWidgetAdd(uint64_t winId, int type, int id, int x, int y, int w, int h, const std::string& text){ std::ostringstream oss; oss<<winId<<"|"<<type<<"|"<<id<<"|"<<x<<"|"<<y<<"|"<<w<<"|"<<h<<"|"<<text; return oss.str(); }

} }
