#include "compositor.h"
#include "allocator.h"
#include "desktop_state.h"
#include "icons.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#endif

namespace gxos { namespace gui {
    using namespace gxos;
    static const char* kGuiChanIn = "gui.input";
    static const char* kGuiChanOut = "gui.output";

    // Static member definitions
    std::atomic<uint64_t> Compositor::s_nextWinId{1000};
    std::unordered_map<uint64_t, WinInfo> Compositor::g_windows; std::vector<uint64_t> Compositor::g_z; std::mutex Compositor::g_lock; uint64_t Compositor::g_focus=0;
    bool Compositor::g_dragActive=false; int Compositor::g_dragOffX=0; int Compositor::g_dragOffY=0; uint64_t Compositor::g_dragWin=0; int Compositor::g_dragStartX=0; int Compositor::g_dragStartY=0;
    bool Compositor::g_resizeActive=false; int Compositor::g_resizeStartW=0; int Compositor::g_resizeStartH=0; int Compositor::g_resizeStartMX=0; int Compositor::g_resizeStartMY=0; uint64_t Compositor::g_resizeWin=0;
    bool Compositor::g_resizePreviewActive=false; int Compositor::g_resizePreviewW=0; int Compositor::g_resizePreviewH=0;
    bool Compositor::g_snapPreviewActive=false; 
#ifdef _WIN32
    RECT Compositor::g_snapPreviewRect{0,0,0,0};
    HWND Compositor::g_hwnd=nullptr;
    // Added missing static definitions declared in header
    HBITMAP Compositor::g_startBtnBmp=nullptr;
    HBITMAP Compositor::g_wallpaperBmp=nullptr;
    int Compositor::g_wallpaperW=0;
    int Compositor::g_wallpaperH=0;
    std::string Compositor::g_wallpaperPath="";
    bool Compositor::g_startMenuVisible=false;
    RECT Compositor::g_startMenuRect{0,0,0,0};
#else
    Compositor::SnapRect Compositor::g_snapPreviewRect{0,0,0,0};
#endif
    bool Compositor::g_showDesktopActive=false; std::vector<uint64_t> Compositor::g_showDesktopMinimized; uint64_t Compositor::g_lastClickTicks=0; uint64_t Compositor::g_lastClickWin=0;
    bool Compositor::g_altTabOverlayActive=false; uint64_t Compositor::g_altTabOverlayTicks=0; int Compositor::g_altTabCycleIndex=0;
    bool Compositor::g_taskbarCycleActive=false; int Compositor::g_taskbarCycleIndex=0; bool Compositor::g_keyboardMoveActive=false; bool Compositor::g_keyboardSizeActive=false; int Compositor::g_kbOrigX=0; int Compositor::g_kbOrigY=0; int Compositor::g_kbOrigW=0; int Compositor::g_kbOrigH=0;

    DesktopConfigData Compositor::g_cfg{}; std::vector<DesktopItem> Compositor::g_items; uint64_t Compositor::g_lastItemClickTicks=0; int Compositor::g_lastItemIndex=-1;

    static uint64_t nowMs(){ return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    static void publishOut(MsgType type, const std::string& payload){ ipc::Message out; out.type=(uint32_t)type; out.data.assign(payload.begin(), payload.end()); ipc::Bus::publish(kGuiChanOut, std::move(out), false); }

    void Compositor::refreshDesktopItems(){ g_items.clear(); // pinned first
        for(const auto& p: g_cfg.pinned){ DesktopItem di; di.label=p; di.action=p; di.pinned=true; g_items.push_back(di);} for(const auto& r: g_cfg.recent){ if(std::find(g_cfg.pinned.begin(), g_cfg.pinned.end(), r)!=g_cfg.pinned.end()) continue; DesktopItem di; di.label=r; di.action=r; di.pinned=false; g_items.push_back(di);} }
    void Compositor::saveDesktopConfig(){ std::string err; DesktopConfig::Save("desktop.json", g_cfg, err); }
    void Compositor::addRecent(const std::string& act){ auto it=std::find(g_cfg.recent.begin(), g_cfg.recent.end(), act); if(it!=g_cfg.recent.end()) g_cfg.recent.erase(it); g_cfg.recent.insert(g_cfg.recent.begin(), act); if(g_cfg.recent.size()>20) g_cfg.recent.pop_back(); refreshDesktopItems(); saveDesktopConfig(); }
    void Compositor::pinAction(const std::string& act){ if(act.empty()) return; if(std::find(g_cfg.pinned.begin(), g_cfg.pinned.end(), act)==g_cfg.pinned.end()){ g_cfg.pinned.push_back(act); refreshDesktopItems(); saveDesktopConfig(); } }
    void Compositor::unpinAction(const std::string& act){ auto it=std::find(g_cfg.pinned.begin(), g_cfg.pinned.end(), act); if(it!=g_cfg.pinned.end()){ g_cfg.pinned.erase(it); refreshDesktopItems(); saveDesktopConfig(); } }
    void Compositor::launchAction(const std::string& act){ Logger::write(LogLevel::Info, std::string("Desktop launch: ")+act); addRecent(act); publishOut(MsgType::MT_DesktopLaunch, act); }

    WinInfo* Compositor::hitWindowAt(int mx, int my){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ uint64_t wid=g_z[idx]; auto it=g_windows.find(wid); if(it==g_windows.end()) continue; WinInfo &w=it->second; if(w.minimized || w.tombstoned) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+w.h) return &w; } return nullptr; }
#ifdef _WIN32
    uint64_t Compositor::hitTestTaskbarButton(int mx, int my, RECT cr, int taskbarH){ int taskbarTop=cr.bottom-taskbarH; if(my < taskbarTop) return 0; int btnX=50; // leave space for start button
        for(uint64_t id : g_z){ auto it=g_windows.find(id); if(it==g_windows.end()) continue; std::string label=it->second.title; SIZE sz; HDC dc=GetDC(g_hwnd); GetTextExtentPoint32A(dc,label.c_str(),(int)label.size(),&sz); ReleaseDC(g_hwnd,dc); int bw=sz.cx+30; int btnTop=taskbarTop+4; int btnBottom=cr.bottom-4; if(mx>=btnX && mx<=btnX+bw && my>=btnTop && my<=btnBottom) return id; btnX += bw + 6; } return 0; }
    void Compositor::initWindow(){ WNDCLASSA wc{}; wc.style=CS_OWNDC; wc.lpfnWndProc=Compositor::WndProc; wc.hInstance=GetModuleHandleA(nullptr); wc.lpszClassName="GXOS_COMPOSITOR"; RegisterClassA(&wc); g_hwnd=CreateWindowExA(0,wc.lpszClassName,"guideXOSCpp Compositor",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1024,768,nullptr,nullptr,wc.hInstance,nullptr); g_startBtnBmp=(HBITMAP)LoadImageA(nullptr,"assets/start_button.bmp",IMAGE_BITMAP,0,0,LR_LOADFROMFILE|LR_CREATEDIBSECTION); }
    void Compositor::shutdownWindow(){ if(g_hwnd){ DestroyWindow(g_hwnd); g_hwnd=nullptr; } }
    void Compositor::requestRepaint(){ if(g_hwnd) InvalidateRect(g_hwnd,nullptr,FALSE); }
    void Compositor::loadWallpaper(const std::string& path){ g_wallpaperPath=path; if(g_wallpaperBmp){ DeleteObject(g_wallpaperBmp); g_wallpaperBmp=nullptr; } if(path.empty()) return; HBITMAP hb=(HBITMAP)LoadImageA(nullptr,path.c_str(),IMAGE_BITMAP,0,0,LR_LOADFROMFILE|LR_CREATEDIBSECTION); if(hb){ BITMAP bm{}; GetObject(hb,sizeof(bm),&bm); g_wallpaperBmp=hb; g_wallpaperW=bm.bmWidth; g_wallpaperH=bm.bmHeight; } }
    void Compositor::freeWallpaper(){ if(g_wallpaperBmp){ DeleteObject(g_wallpaperBmp); g_wallpaperBmp=nullptr; g_wallpaperW=g_wallpaperH=0; } }

    void Compositor::drawDesktopIcons(HDC dc, RECT cr){ const int margin=16; const int iconW=64; const int iconH=64; const int cellW=iconW+20; const int cellH=iconH+34; int cols = (cr.right - margin*2)/cellW; if(cols<1) cols=1; int x0=margin; int y0=margin; HFONT font=(HFONT)GetStockObject(ANSI_VAR_FONT); SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(220,220,230)); POINT cursor; GetCursorPos(&cursor); ScreenToClient(g_hwnd,&cursor); int idx=0; for(auto &it: g_items){ int col=idx%cols; int row=idx/cols; int x=x0+col*cellW; int y=y0+row*cellH; RECT cell{ x, y, x+cellW, y+cellH}; bool hover = (cursor.x>=cell.left && cursor.x<=cell.right && cursor.y>=cell.top && cursor.y<=cell.bottom); if(it.selected){ HBRUSH sel=CreateSolidBrush(RGB(50,90,160)); FillRect(dc,&cell,sel); DeleteObject(sel); FrameRect(dc,&cell,(HBRUSH)GetStockObject(WHITE_BRUSH)); } else if(hover){ HBRUSH hov=CreateSolidBrush(RGB(60,60,70)); FillRect(dc,&cell,hov); DeleteObject(hov); FrameRect(dc,&cell,(HBRUSH)GetStockObject(WHITE_BRUSH)); } RECT iconR{ x+(cellW-iconW)/2, y+4, x+(cellW-iconW)/2+iconW, y+4+iconH}; HBRUSH ib=CreateSolidBrush(it.pinned? RGB(90,140,220):RGB(110,110,130)); FillRect(dc,&iconR,ib); DeleteObject(ib); FrameRect(dc,&iconR,(HBRUSH)GetStockObject(WHITE_BRUSH)); std::string label=it.label; TextOutA(dc, x+4, iconR.bottom+4, label.c_str(), (int)label.size()); idx++; } }

    LRESULT CALLBACK Compositor::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l){
        switch(msg){
        case WM_CLOSE: PostQuitMessage(0); return 0;
        case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT cr; GetClientRect(h,&cr); if(g_wallpaperBmp){ HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,g_wallpaperBmp); BITMAP bm{}; GetObject(g_wallpaperBmp,sizeof(bm),&bm); double sx=(double)(cr.right-cr.left)/bm.bmWidth; double sy=(double)(cr.bottom-cr.top)/bm.bmHeight; double s=sx<sy? sx: sy; int dstW=(int)(bm.bmWidth*s); int dstH=(int)(bm.bmHeight*s); int dx=(cr.right-dstW)/2; int dy=(cr.bottom-dstH)/2; HBRUSH bg=CreateSolidBrush(RGB(25,25,30)); FillRect(dc,&cr,bg); DeleteObject(bg); SetStretchBltMode(dc,HALFTONE); StretchBlt(dc,dx,dy,dstW,dstH,mem,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY); SelectObject(mem,old); DeleteDC(mem);} else { HBRUSH bg=CreateSolidBrush(RGB(25,25,30)); FillRect(dc,&cr,bg); DeleteObject(bg);} drawDesktopIcons(dc, cr);
            // Draw application windows in Z-order (bottom to top)
            const int titleBarH = 24; HFONT font=(HFONT)GetStockObject(ANSI_VAR_FONT); SelectObject(dc,font); SetBkMode(dc,TRANSPARENT);
            for(size_t i=0;i<g_z.size(); ++i){ auto it=g_windows.find(g_z[i]); if(it==g_windows.end()) continue; const WinInfo &winfo=it->second; if(winfo.minimized) continue; RECT wrect{ winfo.x, winfo.y, winfo.x + winfo.w, winfo.y + winfo.h };
                // window background
                HBRUSH wbg=CreateSolidBrush(RGB(60,60,70)); FillRect(dc,&wrect,wbg); DeleteObject(wbg);
                // title bar
                RECT tbr{ winfo.x, winfo.y, winfo.x + winfo.w, winfo.y + titleBarH }; HBRUSH tbg=CreateSolidBrush(winfo.id==g_focus? RGB(80,110,160):RGB(70,70,85)); FillRect(dc,&tbr,tbg); DeleteObject(tbg);
                FrameRect(dc,&wrect,(HBRUSH)GetStockObject(WHITE_BRUSH));
                SetTextColor(dc, RGB(240,240,240)); TextOutA(dc, tbr.left + 8, tbr.top + 4, winfo.title.c_str(), (int)winfo.title.size());
                // Draw rect items
                for(const auto &ri : winfo.rects){ RECT rr{ winfo.x + ri.x, winfo.y + titleBarH + ri.y, winfo.x + ri.x + ri.w, winfo.y + titleBarH + ri.h }; HBRUSH rb=CreateSolidBrush(RGB(ri.r,ri.g,ri.b)); FillRect(dc,&rr,rb); DeleteObject(rb);} 
                // Draw text items
                int ty = winfo.y + titleBarH + 8; for(const auto &tx : winfo.texts){ TextOutA(dc, winfo.x + 8, ty, tx.c_str(), (int)tx.size()); ty += 16; }
                // Tombstoned overlay hint
                if(winfo.tombstoned){ const char* t="Tombstoned"; SIZE ts; GetTextExtentPoint32A(dc,t,(int)strlen(t),&ts); SetTextColor(dc, RGB(200,100,100)); TextOutA(dc, winfo.x + (winfo.w - ts.cx)/2, winfo.y + (winfo.h - ts.cy)/2, t, (int)strlen(t)); }
            }
            int taskbarH=32; RECT tb{cr.left,cr.bottom-taskbarH,cr.right,cr.bottom}; HBRUSH tbBg=CreateSolidBrush(RGB(40,40,50)); FillRect(dc,&tb,tbBg); DeleteObject(tbBg); // Start button
            RECT startBtn{8,cr.bottom-taskbarH+4,8+32,cr.bottom-4}; HBRUSH sbg=CreateSolidBrush(g_startMenuVisible? RGB(80,110,160):RGB(60,80,100)); FillRect(dc,&startBtn,sbg); DeleteObject(sbg); FrameRect(dc,&startBtn,(HBRUSH)GetStockObject(WHITE_BRUSH)); TextOutA(dc,startBtn.left+8,startBtn.top+8,"Start",5);
            // Taskbar buttons
            POINT cursor; GetCursorPos(&cursor); ScreenToClient(h,&cursor); int btnX=50; for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it==g_windows.end()) continue; std::string label=it->second.title; SIZE sz; GetTextExtentPoint32A(dc,label.c_str(),(int)label.size(),&sz); int bw=sz.cx+30; RECT br{ btnX, cr.bottom-taskbarH+4, btnX+bw, cr.bottom-4}; bool hover=(cursor.x>=br.left && cursor.x<=br.right && cursor.y>=br.top && cursor.y<=br.bottom); HBRUSH bbg=CreateSolidBrush( hover? RGB(90,130,190): (id==g_focus? RGB(80,110,160):(it->second.minimized? RGB(45,45,55): (it->second.tombstoned? RGB(90,70,40):RGB(70,70,85)))) ); FillRect(dc,&br,bbg); DeleteObject(bbg); FrameRect(dc,&br,(HBRUSH)GetStockObject(WHITE_BRUSH)); TextOutA(dc,br.left+10,br.top+10,label.c_str(),(int)label.size()); btnX += bw + 6; }
            // Start menu popup (pinned + recent)
            if(g_startMenuVisible){ int smW=260; int maxRows=14; int rowH=20; int smH=maxRows*rowH + 10; RECT sm{ startBtn.left, startBtn.top - smH - 6, startBtn.left+smW, startBtn.top - 6}; if(sm.top<0){ sm.top=4; sm.bottom=sm.top+smH; } g_startMenuRect=sm; HBRUSH mBg=CreateSolidBrush(RGB(45,45,55)); FillRect(dc,&sm,mBg); DeleteObject(mBg); FrameRect(dc,&sm,(HBRUSH)GetStockObject(WHITE_BRUSH)); int y=sm.top+4; HFONT f=(HFONT)GetStockObject(ANSI_VAR_FONT); SelectObject(dc,f); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(230,230,230)); int row=0; for(size_t i=0;i<g_items.size() && row<maxRows; ++i){ RECT r{ sm.left+4, y, sm.right-4, y+rowH}; bool hover=(cursor.x>=r.left && cursor.x<=r.right && cursor.y>=r.top && cursor.y<=r.bottom); HBRUSH rb=CreateSolidBrush(hover? RGB(70,90,130):RGB(55,55,70)); FillRect(dc,&r,rb); DeleteObject(rb); std::string txt=(g_items[i].pinned? "* ":"  ")+g_items[i].label; TextOutA(dc,r.left+4,r.top+4,txt.c_str(),(int)txt.size()); y+=rowH; row++; }
            }
            EndPaint(h,&ps); return 0; }
        case WM_LBUTTONDOWN:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); RECT cr; GetClientRect(h,&cr); int taskbarH=32; RECT startBtn{8,cr.bottom-taskbarH+4,8+32,cr.bottom-4}; // Start button toggle
            if(mx>=startBtn.left && mx<=startBtn.right && my>=startBtn.top && my<=startBtn.bottom){ g_startMenuVisible=!g_startMenuVisible; requestRepaint(); return 0; }
            // Start menu click
            if(g_startMenuVisible){ if(mx>=g_startMenuRect.left && mx<=g_startMenuRect.right && my>=g_startMenuRect.top && my<=g_startMenuRect.bottom){ int idx=(my - (g_startMenuRect.top+4))/20; if(idx>=0 && idx<(int)g_items.size()){ uint64_t now=nowMs(); if(g_lastItemIndex==idx && (now-g_lastItemClickTicks)<450){ launchAction(g_items[idx].action); g_startMenuVisible=false; } else { g_lastItemIndex=idx; g_lastItemClickTicks=now; } requestRepaint(); return 0; } } else { g_startMenuVisible=false; }
            }
            // Desktop icon click (selection / double)
            const int margin=16; const int iconW=64; const int iconH=64; const int cellW=iconW+20; const int cellH=iconH+34; int cols=(cr.right-margin*2)/cellW; if(cols<1) cols=1; if(my < cr.bottom-taskbarH){ int col=(mx-margin)/cellW; int row=(my-margin)/cellH; if(col>=0 && row>=0){ int idx=row*cols+col; if(idx>=0 && idx<(int)g_items.size()){ uint64_t now=nowMs(); if(g_lastItemIndex==idx && (now-g_lastItemClickTicks)<450){ launchAction(g_items[idx].action); } else { for(auto &di: g_items) di.selected=false; g_items[idx].selected=true; g_lastItemIndex=idx; g_lastItemClickTicks=now; } requestRepaint(); return 0; } } }
            // Taskbar button click (minimize/restore/tombstone)
            uint64_t id=hitTestTaskbarButton(mx,my,cr,taskbarH); if(id){ std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ WinInfo &w=it->second; if(!w.minimized && !w.tombstoned){ w.tombstoned=true; w.minimized=true; if(g_focus==w.id) g_focus=0; } else { w.tombstoned=false; w.minimized=false; g_focus=w.id; for(auto itZ=g_z.begin(); itZ!=g_z.end(); ++itZ){ if(*itZ==id){ g_z.erase(itZ); break; } } g_z.push_back(id); } } requestRepaint(); return 0; }
            Compositor::handleMouse(mx,my,true,false); publishOut(MsgType::MT_InputMouse,std::to_string(mx)+"|"+std::to_string(my)+"|1|down"); return 0; }
        case WM_RBUTTONDOWN:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); if(g_startMenuVisible && mx>=g_startMenuRect.left && mx<=g_startMenuRect.right && my>=g_startMenuRect.top && my<=g_startMenuRect.bottom){ int idx=(my - (g_startMenuRect.top+4))/20; if(idx>=0 && idx<(int)g_items.size()){ if(g_items[idx].pinned) unpinAction(g_items[idx].action); else pinAction(g_items[idx].action); requestRepaint(); return 0; } }
            // Desktop icon right-click pin/unpin
            RECT cr; GetClientRect(h,&cr); int taskbarH=32; if(my < cr.bottom-taskbarH){ const int margin=16; const int iconW=64; const int cellW=iconW+20; const int cellH=64+34; int cols=(cr.right-margin*2)/cellW; if(cols<1) cols=1; int col=(mx-margin)/cellW; int row=(my-margin)/cellH; if(col>=0 && row>=0){ int idx=row*cols+col; if(idx>=0 && idx<(int)g_items.size()){ if(g_items[idx].pinned) unpinAction(g_items[idx].action); else pinAction(g_items[idx].action); requestRepaint(); return 0; } } }
        } break;
        case WM_LBUTTONUP:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); Compositor::handleMouse(mx,my,false,true); publishOut(MsgType::MT_InputMouse,std::to_string(mx)+"|"+std::to_string(my)+"|1|up"); } break;
        case WM_MOUSEMOVE:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); Compositor::handleMouse(mx,my,false,false); publishOut(MsgType::MT_InputMouse,std::to_string(mx)+"|"+std::to_string(my)+"|0|move"); } break;
        case WM_KEYDOWN: case WM_SYSKEYDOWN:{ int key=(int)w; SHORT lwinS=GetKeyState(VK_LWIN); SHORT rwinS=GetKeyState(VK_RWIN); bool winDown=((lwinS|rwinS)&0x8000)!=0; if(winDown && (key=='D'||key=='d')){ std::lock_guard<std::mutex> lk(g_lock); if(!g_showDesktopActive){ g_showDesktopMinimized.clear(); for(uint64_t wid: g_z){ auto it=g_windows.find(wid); if(it!=g_windows.end() && !it->second.minimized){ it->second.minimized=true; it->second.tombstoned=true; g_showDesktopMinimized.push_back(wid);} } g_focus=0; g_showDesktopActive=true; } else { for(uint64_t wid: g_showDesktopMinimized){ auto it=g_windows.find(wid); if(it!=g_windows.end()){ it->second.minimized=false; it->second.tombstoned=false; } } g_showDesktopMinimized.clear(); g_showDesktopActive=false; } requestRepaint(); return 0; } publishOut(MsgType::MT_InputKey,std::to_string(key)+"|down"); } break; case WM_KEYUP:{ int key=(int)w; publishOut(MsgType::MT_InputKey,std::to_string(key)+"|up"); } break; }
        return DefWindowProcA(h,msg,w,l);
    }
#endif

    void Compositor::sendFocus(uint64_t winId){ publishOut(MsgType::MT_SetFocus,std::to_string(winId)); }
    void Compositor::invalidate(uint64_t){
#ifdef _WIN32
        requestRepaint();
#endif
    }
    void Compositor::emitWidgetEvt(uint64_t winId, int wid, const std::string& evt, const std::string& value){ publishOut(MsgType::MT_WidgetEvt,std::to_string(winId)+"|"+std::to_string(wid)+"|"+evt+"|"+value); }

    void Compositor::handleMouse(int mx, int my, bool down, bool up){ std::lock_guard<std::mutex> lk(g_lock); const int titleBarH=24; const int gripSize=12; const int taskbarH=32; RECT cr{0,0,1024,768};
#ifdef _WIN32
        if(g_hwnd) GetClientRect(g_hwnd,&cr);
#endif
        if(down){ g_dragStartX=mx; g_dragStartY=my; }
        if(down && !g_dragActive){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized||w.maximized||w.tombstoned) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+titleBarH){ if(std::abs(mx-g_dragStartX)>=4 || std::abs(my-g_dragStartY)>=4){ g_dragActive=true; g_dragWin=w.id; g_dragOffX=mx-w.x; g_dragOffY=my-w.y; break; } } } }
        if(down){ uint64_t t=nowMs(); for(int idx=(int)g_z.size()-1; idx>=0; --idx){ uint64_t wid=g_z[idx]; WinInfo &w=g_windows[wid]; if(w.minimized||w.tombstoned) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+titleBarH){ if(g_lastClickWin==w.id && (t-g_lastClickTicks)<450){ if(!w.minimized){ if(!w.maximized){ w.prevX=w.x; w.prevY=w.y; w.prevW=w.w; w.prevH=w.h; w.x=0; w.y=0; w.w=cr.right; w.h=cr.bottom-taskbarH; w.maximized=true; } else { w.x=w.prevX; w.y=w.prevY; w.w=w.prevW; w.h=w.prevH; w.maximized=false; } } g_lastClickWin=0; g_lastClickTicks=0; invalidate(w.id); return; } g_lastClickWin=w.id; g_lastClickTicks=t; break; } } }
        if(down){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized||w.maximized||w.tombstoned) continue; if(mx>=w.x+w.w-gripSize && mx < w.x+w.w && my>=w.y+w.h-gripSize && my < w.y+w.h){ g_resizeActive=true; g_resizeWin=w.id; g_resizeStartW=w.w; g_resizeStartH=w.h; g_resizeStartMX=mx; g_resizeStartMY=my; break; } } }
        if(g_dragActive && up){ auto it=g_windows.find(g_dragWin); if(it!=g_windows.end()){ WinInfo &w=it->second; const int snap=16; bool nearLeft=mx<=snap, nearRight=mx>=cr.right-snap, nearTop=my<=snap; bool nearBottom=my>=cr.bottom-taskbarH - snap; if(nearTop && !(nearLeft||nearRight)){ w.prevX=w.x; w.prevY=w.y; w.prevW=w.w; w.prevH=w.h; w.x=0; w.y=0; w.w=cr.right; w.h=cr.bottom-taskbarH; w.maximized=true; w.snapState=0; } else if(nearLeft){ w.maximized=false; w.x=0; w.y=0; w.w=cr.right/2; w.h=cr.bottom-taskbarH; w.snapState=1; } else if(nearRight){ w.maximized=false; w.x=cr.right/2; w.y=0; w.w=cr.right/2; w.h=cr.bottom-taskbarH; w.snapState=2; } w.dirty=true; } g_dragActive=false; g_dragWin=0; g_snapPreviewActive=false; invalidate(0); }
        if(g_resizeActive && !up){ auto it=g_windows.find(g_resizeWin); if(it!=g_windows.end()){ int dw=mx-g_resizeStartMX; int dh=my-g_resizeStartMY; int newW=g_resizeStartW+dw; if(newW<160) newW=160; int newH=g_resizeStartH+dh; if(newH<120) newH=120; g_resizePreviewActive=true; g_resizePreviewW=newW; g_resizePreviewH=newH; } }
        if(g_resizeActive && up){ auto it=g_windows.find(g_resizeWin); if(it!=g_windows.end()){ int dw=mx-g_resizeStartMX; int dh=my-g_resizeStartMY; int newW=g_resizeStartW+dw; if(newW<160) newW=160; int newH=g_resizeStartH+dh; if(newH<120) newH=120; it->second.w=newW; it->second.h=newH; it->second.dirty=true; } g_resizeActive=false; g_resizeWin=0; g_resizePreviewActive=false; }
        if(down){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized||w.tombstoned) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+w.h){ g_focus=w.id; for(auto itZ=g_z.begin(); itZ!=g_z.end(); ++itZ){ if(*itZ==w.id){ g_z.erase(itZ); break; } } g_z.push_back(w.id); sendFocus(w.id); break; } } }
    }

    void Compositor::handleMessage(const ipc::Message& m){ std::string s(m.data.begin(), m.data.end()); switch((MsgType)m.type){ case MsgType::MT_Create:{ std::istringstream iss(s); std::string title; std::getline(iss,title,'|'); std::string wS,hS; std::getline(iss,wS,'|'); std::getline(iss,hS,'|'); int w=320,h=200; try{ w=std::stoi(wS); h=std::stoi(hS);}catch(...){ } uint64_t id=s_nextWinId.fetch_add(1); { std::lock_guard<std::mutex> lk(g_lock); g_windows[id]=WinInfo{ id,title,60+(int)(id%7)*40,60+(int)(id%7)*40,w,h,{},{} ,{}, false,false,0,0,0,0, true }; g_z.push_back(id); g_focus=id; } publishOut(MsgType::MT_Create,std::to_string(id)+"|"+title); sendFocus(id); invalidate(id); } break; case MsgType::MT_DrawText:{ std::istringstream iss(s); std::string idS; std::getline(iss,idS,'|'); std::string text; std::getline(iss,text); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.texts.push_back(text); it->second.dirty=true; } } publishOut(MsgType::MT_DrawText,std::to_string(id)+"|"+text); invalidate(id); } break; case MsgType::MT_Close:{ uint64_t id=0; try{ id=std::stoull(s);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); g_windows.erase(id); auto it=std::find(g_z.begin(),g_z.end(),id); if(it!=g_z.end()) g_z.erase(it); if(g_focus==id) g_focus=0; } publishOut(MsgType::MT_Close,std::to_string(id)); invalidate(0); } break; case MsgType::MT_DrawRect:{ std::istringstream iss(s); std::string idS; std::getline(iss,idS,'|'); std::string xs,ys,ws,hs,rs,gs,bs; std::getline(iss,xs,'|'); std::getline(iss,ys,'|'); std::getline(iss,ws,'|'); std::getline(iss,hs,'|'); std::getline(iss,rs,'|'); std::getline(iss,gs,'|'); std::getline(iss,bs,'|'); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } DrawRectItem item{ std::stoi(xs), std::stoi(ys), std::stoi(ws), std::stoi(hs), (uint8_t)std::stoi(rs),(uint8_t)std::stoi(gs),(uint8_t)std::stoi(bs)}; { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.rects.push_back(item); it->second.dirty=true; } } publishOut(MsgType::MT_DrawRect,std::to_string(id)); invalidate(id); } break; case MsgType::MT_SetTitle:{ std::istringstream iss(s); std::string idS; std::getline(iss,idS,'|'); std::string title; std::getline(iss,title); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.title=title; it->second.dirty=true; } } publishOut(MsgType::MT_SetTitle,std::to_string(id)+"|"+title); invalidate(id); } break; case MsgType::MT_Move:{ std::istringstream iss(s); std::string idS,xs,ys; std::getline(iss,idS,'|'); std::getline(iss,xs,'|'); std::getline(iss,ys,'|'); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } int nx=std::stoi(xs), ny=std::stoi(ys); { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.maximized){ it->second.x=nx; it->second.y=ny; it->second.dirty=true; } } publishOut(MsgType::MT_Move,std::to_string(id)+"|"+xs+"|"+ys); invalidate(id); } break; case MsgType::MT_Resize:{ std::istringstream iss(s); std::string idS,ws,hs; std::getline(iss,idS,'|'); std::getline(iss,ws,'|'); std::getline(iss,hs,'|'); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } int nw=std::stoi(ws), nh=std::stoi(hs); { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.maximized){ it->second.w=nw; it->second.h=nh; it->second.dirty=true; } } publishOut(MsgType::MT_Resize,std::to_string(id)+"|"+ws+"|"+hs); invalidate(id); } break; case MsgType::MT_WidgetAdd:{ /* omitted */ } break; case MsgType::MT_WindowList:{ std::ostringstream oss; bool first=true; { std::lock_guard<std::mutex> lk(g_lock); for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it==g_windows.end()) continue; if(!first) oss<<";"; first=false; oss<<it->first<<"|"<<it->second.title<<"|"<<(it->second.minimized?1:0); } } publishOut(MsgType::MT_WindowList,oss.str()); } break; case MsgType::MT_Activate:{ uint64_t id=0; try{ id=std::stoull(s);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); for(auto it=g_z.begin(); it!=g_z.end(); ++it){ if(*it==id){ g_z.erase(it); break; } } auto wit=g_windows.find(id); if(wit!=g_windows.end()){ wit->second.minimized=false; wit->second.tombstoned=false; } g_z.push_back(id); g_focus=id; } sendFocus(id); invalidate(id); } break; case MsgType::MT_Minimize:{ uint64_t id=0; try{ id=std::stoull(s);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); auto wit=g_windows.find(id); if(wit!=g_windows.end()){ wit->second.minimized=true; wit->second.tombstoned=true; if(g_focus==id) g_focus=0; } } invalidate(id); } break; case MsgType::MT_ShowDesktopToggle:{ if(!g_showDesktopActive){ g_showDesktopMinimized.clear(); for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.minimized){ it->second.minimized=true; it->second.tombstoned=true; g_showDesktopMinimized.push_back(id);} } g_focus=0; g_showDesktopActive=true; } else { for(uint64_t id: g_showDesktopMinimized){ auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.minimized=false; it->second.tombstoned=false; } } g_showDesktopMinimized.clear(); g_showDesktopActive=false; } invalidate(0); } break;
        case MsgType::MT_StateSave:{ std::string path=s; std::vector<SavedWindow> sw; { std::lock_guard<std::mutex> lk(g_lock); for(size_t i=0;i<g_z.size();++i){ uint64_t id=g_z[i]; auto it=g_windows.find(id); if(it==g_windows.end()) continue; const WinInfo &w=it->second; SavedWindow rec; rec.id=w.id; rec.title=w.title; rec.x=w.x; rec.y=w.y; rec.w=w.w; rec.h=w.h; rec.minimized=w.minimized; rec.maximized=w.maximized; rec.z=(int)i; rec.focused=(g_focus==w.id); rec.snap=w.snapState; sw.push_back(rec);} } std::string err; if(!DesktopState::Save(path, sw, err)) publishOut(MsgType::MT_WidgetEvt,std::string("STATE_SAVE_ERR|")+err); else publishOut(MsgType::MT_WidgetEvt,std::string("STATE_SAVE_OK|")+path); } break;
        case MsgType::MT_StateLoad:{ std::string path=s; std::vector<SavedWindow> sw; std::string err; if(!DesktopState::Load(path, sw, err)){ publishOut(MsgType::MT_WidgetEvt,std::string("STATE_LOAD_ERR|")+err); } else { { std::lock_guard<std::mutex> lk(g_lock); g_windows.clear(); g_z.clear(); g_focus=0; std::sort(sw.begin(), sw.end(), [](const SavedWindow&a,const SavedWindow&b){ return a.z<b.z; }); for(auto &w: sw){ uint64_t id=s_nextWinId.fetch_add(1); WinInfo wi{ id, w.title, w.x, w.y, w.w, w.h, {}, {}, {}, w.minimized, w.maximized, 0,0,0,0, true, w.snap }; if(wi.maximized){ wi.x=0; wi.y=0; wi.w=1024; wi.h=768-32; } g_windows[id]=wi; g_z.push_back(id); if(w.focused && !wi.minimized) g_focus=id; } } publishOut(MsgType::MT_WidgetEvt,std::string("STATE_LOAD_OK|")+path); invalidate(0); } } break;
        case MsgType::MT_Invalidate:{ invalidate(0); } break;
        case MsgType::MT_Ping:{ publishOut(MsgType::MT_Ping,s); } break;
        case MsgType::MT_DesktopLaunch:{ launchAction(s); } break;
        case MsgType::MT_DesktopPins:{ // format: +ACTION;-ACTION;...
            std::istringstream iss(s); std::string tok; while(std::getline(iss,tok,';')){ if(tok.size()<2) continue; if(tok[0]=='+') pinAction(tok.substr(1)); else if(tok[0]=='-') unpinAction(tok.substr(1)); }
        } break;
        case MsgType::MT_DesktopWallpaperSet:{ loadWallpaper(s); g_cfg.wallpaperPath=s; saveDesktopConfig(); invalidate(0); } break;
        default: break; }
    }

    void Compositor::drawAll(){
#ifdef _WIN32
        requestRepaint();
#endif
    }
    void Compositor::pumpEvents(){
#ifdef _WIN32
        MSG msg; while(PeekMessageA(&msg,nullptr,0,0,PM_REMOVE)){ TranslateMessage(&msg); DispatchMessageA(&msg); if(msg.message==WM_QUIT) break; }
#endif
    }

    int Compositor::main(int argc, char** argv){ Logger::write(LogLevel::Info,"Compositor service started (native window)"); ipc::Bus::ensure(kGuiChanIn); ipc::Bus::ensure(kGuiChanOut);
#ifdef _WIN32
        initWindow();
#endif
        // Load desktop config (JSON). Fallback to legacy desktop.state if present/loaded.
        DesktopConfigData cfg; std::string cfgErr; bool cfgOk = DesktopConfig::Load("desktop.json", cfg, cfgErr);
#ifdef _WIN32
        if(cfgOk) loadWallpaper(cfg.wallpaperPath);
#endif
        bool legacyLoaded=false; if(!cfgOk || cfg.windows.empty()){ std::vector<SavedWindow> sw; std::string err; if(DesktopState::Load("desktop.state", sw, err)){ std::lock_guard<std::mutex> lk(g_lock); g_windows.clear(); g_z.clear(); g_focus=0; std::sort(sw.begin(), sw.end(), [](const SavedWindow&a,const SavedWindow&b){ return a.z<b.z; }); for(auto &w: sw){ uint64_t id=s_nextWinId.fetch_add(1); WinInfo wi{ id,w.title,w.x,w.y,w.w,w.h, {}, {}, {}, w.minimized, w.maximized, 0,0,0,0, true, w.snap }; if(wi.maximized){ RECT crL{0,0,1024,768};
#ifdef _WIN32
                    if(g_hwnd) GetClientRect(g_hwnd,&crL);
#endif
                    int taskbarY=crL.bottom-32; wi.x=crL.left; wi.y=crL.top; wi.w=crL.right-crL.left; wi.h=taskbarY-crL.top; } g_windows[id]=wi; g_z.push_back(id); if(w.focused && !wi.minimized) g_focus=id; } legacyLoaded=true; } }
        bool running=true; while(running){ pumpEvents(); ipc::Message m; if(ipc::Bus::pop(kGuiChanIn,m,30)){ if(m.type==(uint32_t)MsgType::MT_Ping && m.data.size()==3 && std::string(m.data.begin(),m.data.end())=="bye") running=false; else handleMessage(m); } }
        DesktopConfigData outCfg=g_cfg; { std::lock_guard<std::mutex> lk(g_lock); outCfg.windows.clear(); for(size_t i=0;i<g_z.size();++i){ uint64_t id=g_z[i]; auto it=g_windows.find(id); if(it==g_windows.end()) continue; const WinInfo &w=it->second; DesktopWindowRec rec; rec.id=w.id; rec.title=w.title; rec.x=w.x; rec.y=w.y; rec.w=w.w; rec.h=w.h; rec.minimized=w.minimized; rec.maximized=w.maximized; rec.z=(int)i; rec.focused=(g_focus==w.id); rec.snap=w.snapState; outCfg.windows.push_back(rec);} }
        std::string cerr; DesktopConfig::Save("desktop.json", outCfg, cerr); if(!legacyLoaded){ std::vector<SavedWindow> sw; { std::lock_guard<std::mutex> lk(g_lock); for(auto &kv: g_windows){ sw.push_back(SavedWindow{ kv.second.id, kv.second.title, kv.second.x, kv.second.y, kv.second.w, kv.second.h, kv.second.minimized, kv.second.maximized }); } } std::string err; DesktopState::Save("desktop.state", sw, err); }
#ifdef _WIN32
        shutdownWindow();
#endif
        Logger::write(LogLevel::Info,"Compositor service stopping"); return 0; }
    uint64_t Compositor::start(){ ProcessSpec spec{"compositor", Compositor::main}; return ProcessTable::spawn(spec,{"compositor"}); }
}
} // namespace gxos::gui
