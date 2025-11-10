#include "compositor.h"
#include "allocator.h"
#include "desktop_state.h"
#include "icons.h"
#include <sstream>
#include <algorithm>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#endif

namespace gxos { namespace gui {
    using namespace gxos;
    static const char* kGuiChanIn = "gui.input";
    static const char* kGuiChanOut = "gui.output";

    std::atomic<uint64_t> Compositor::s_nextWinId{1000};
    std::unordered_map<uint64_t, WinInfo> Compositor::g_windows; std::vector<uint64_t> Compositor::g_z; std::mutex Compositor::g_lock; uint64_t Compositor::g_focus=0;
    bool Compositor::g_dragActive=false; int Compositor::g_dragOffX=0; int Compositor::g_dragOffY=0; uint64_t Compositor::g_dragWin=0; int Compositor::g_dragStartX=0; int Compositor::g_dragStartY=0;
    bool Compositor::g_resizeActive=false; int Compositor::g_resizeStartW=0; int Compositor::g_resizeStartH=0; int Compositor::g_resizeStartMX=0; int Compositor::g_resizeStartMY=0; uint64_t Compositor::g_resizeWin=0;
    bool Compositor::g_resizePreviewActive=false; int Compositor::g_resizePreviewW=0; int Compositor::g_resizePreviewH=0;
    bool Compositor::g_snapPreviewActive=false; 
#ifdef _WIN32
    RECT Compositor::g_snapPreviewRect{0,0,0,0};
#else
    Compositor::SnapRect Compositor::g_snapPreviewRect{0,0,0,0};
#endif
    bool Compositor::g_showDesktopActive=false; std::vector<uint64_t> Compositor::g_showDesktopMinimized; uint64_t Compositor::g_lastClickTicks=0; uint64_t Compositor::g_lastClickWin=0;
    bool Compositor::g_altTabOverlayActive=false; uint64_t Compositor::g_altTabOverlayTicks=0;
#ifdef _WIN32
    HWND Compositor::g_hwnd = nullptr;
    HBITMAP Compositor::g_startBtnBmp = nullptr;
    std::vector<RECT> Compositor::g_monitors;
#endif

    static uint64_t nowMs(){ return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    static void publishOut(MsgType type, const std::string& payload){ ipc::Message out; out.type=(uint32_t)type; out.data.assign(payload.begin(), payload.end()); ipc::Bus::publish(kGuiChanOut, std::move(out), false); }

    WinInfo* Compositor::hitWindowAt(int mx, int my){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ uint64_t wid=g_z[idx]; auto it=g_windows.find(wid); if(it==g_windows.end()) continue; WinInfo &w=it->second; if(w.minimized) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+w.h) return &w; } return nullptr; }
#ifdef _WIN32
    uint64_t Compositor::hitTestTaskbarButton(int mx, int my, RECT cr, int taskbarH){ int taskbarTop=cr.bottom-taskbarH; if(my < taskbarTop) return 0; int btnX=8; for(uint64_t id : g_z){ auto it=g_windows.find(id); if(it==g_windows.end()) continue; std::string label=std::to_string(id)+":"+it->second.title; int bw=(int)label.size()*7+14; int btnTop=taskbarTop+4; int btnBottom=cr.bottom-4; if(mx>=btnX && mx<=btnX+bw && my>=btnTop && my<=btnBottom) return id; btnX += bw + 6; } return 0; }
    void Compositor::initWindow(){ WNDCLASSA wc{}; wc.style=CS_OWNDC; wc.lpfnWndProc=Compositor::WndProc; wc.hInstance=GetModuleHandleA(nullptr); wc.lpszClassName="GXOS_COMPOSITOR"; RegisterClassA(&wc); g_hwnd=CreateWindowExA(0,wc.lpszClassName,"guideXOSCpp Compositor",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1024,768,nullptr,nullptr,wc.hInstance,nullptr); 
#ifdef _WIN32
        // Try to load optional start button bitmap from working dir assets (ignore failure)
        g_startBtnBmp = (HBITMAP)LoadImageA(nullptr, "assets/start_button.bmp", IMAGE_BITMAP, 0,0, LR_LOADFROMFILE|LR_CREATEDIBSECTION);
#endif
    }
    void Compositor::shutdownWindow(){ if(g_hwnd){ DestroyWindow(g_hwnd); g_hwnd=nullptr; } }
    void Compositor::requestRepaint(){ if(g_hwnd) InvalidateRect(g_hwnd,nullptr,FALSE); }

    LRESULT CALLBACK Compositor::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l){
        switch(msg){
        case WM_CLOSE: PostQuitMessage(0); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT cr; GetClientRect(h,&cr);
            HBRUSH bg=CreateSolidBrush(RGB(25,25,30)); FillRect(dc,&cr,bg); DeleteObject(bg);
            int taskbarH=32; RECT tb{cr.left,cr.bottom-taskbarH,cr.right,cr.bottom}; HBRUSH tbBg=CreateSolidBrush(RGB(40,40,50)); FillRect(dc,&tb,tbBg); DeleteObject(tbBg);
            // Start button icon from Icons class (fallback to previous)
            HBITMAP sbmp = Icons::StartIcon(32); if(!sbmp) sbmp = g_startBtnBmp; if(sbmp){ HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,sbmp); BITMAP bm{}; GetObject(sbmp,sizeof(bm),&bm); BitBlt(dc,8,cr.bottom-taskbarH+ (taskbarH-bm.bmHeight)/2, bm.bmWidth,bm.bmHeight,mem,0,0,SRCCOPY); SelectObject(mem,old); DeleteDC(mem);} else { RECT startR{8,cr.bottom-taskbarH+4,8+24,cr.bottom-4}; HBRUSH sB=CreateSolidBrush(RGB(60,140,80)); FillRect(dc,&startR,sB); DeleteObject(sB); FrameRect(dc,&startR,(HBRUSH)GetStockObject(WHITE_BRUSH)); TextOutA(dc,startR.left+4,startR.top+4,"S",1);}            
            RECT sdBtn{cr.right-28,cr.bottom-taskbarH+4,cr.right-8,cr.bottom-4}; HBRUSH sdBg=CreateSolidBrush(g_showDesktopActive? RGB(80,140,200):RGB(60,60,70)); FillRect(dc,&sdBtn,sdBg); DeleteObject(sdBg); FrameRect(dc,&sdBtn,(HBRUSH)GetStockObject(WHITE_BRUSH)); TextOutA(dc,sdBtn.left+4,sdBtn.top+6,"SD",2);
            std::vector<WinInfo> wins; { std::lock_guard<std::mutex> lk(g_lock); for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.minimized) wins.push_back(it->second); } }
            HFONT font=(HFONT)GetStockObject(ANSI_VAR_FONT); SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(230,230,230));
            for(auto &winfo: wins){ const int titleBarH=24; int x=winfo.x,y=winfo.y; RECT r{ x,y,x+winfo.w,y+winfo.h}; RECT sh{ r.left+4, r.top+4, r.right+4, r.bottom+4}; HBRUSH shB=CreateSolidBrush(RGB(15,15,20)); FillRect(dc,&sh,shB); DeleteObject(shB); HBRUSH winBg=CreateSolidBrush(RGB(60,60,80)); FillRect(dc,&r,winBg); DeleteObject(winBg); FrameRect(dc,&r,(HBRUSH)GetStockObject(WHITE_BRUSH)); RECT tr{ x,y,x+winfo.w,y+titleBarH}; HBRUSH tBrush=CreateSolidBrush(winfo.id==g_focus? RGB(90,120,170):RGB(70,70,90)); FillRect(dc,&tr,tBrush); DeleteObject(tBrush); FrameRect(dc,&tr,(HBRUSH)GetStockObject(BLACK_BRUSH)); std::string title=std::to_string(winfo.id)+" "+winfo.title; TextOutA(dc,x+6,y+6,title.c_str(),(int)title.size());
                // Titlebar button icons (size 16) at far right; simple layout
                int iconSize=16; int btnPad=4; int bx=winfo.x+winfo.w-iconSize-btnPad; auto drawBtn=[&](HBITMAP bmp){ if(!bmp){ bx-=iconSize+btnPad; return; } HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,bmp); BitBlt(dc,bx,y+4,iconSize,iconSize,mem,0,0,SRCCOPY); SelectObject(mem,old); DeleteDC(mem); bx-=iconSize+btnPad; };
                drawBtn(Icons::CloseIcon(16)); drawBtn(Icons::TombstoneIcon(16)); drawBtn(Icons::RestoreIcon(16)); drawBtn(Icons::MaximizeIcon(16)); drawBtn(Icons::MinimizeIcon(16));
                int ty=y+titleBarH+4; for(auto &t: winfo.texts){ TextOutA(dc,x+8,ty,t.c_str(),(int)t.size()); ty+=16; }
                for(auto &rrI: winfo.rects){ RECT rr{ x+rrI.x,y+titleBarH+rrI.y,x+rrI.x+rrI.w,y+titleBarH+rrI.y+rrI.h}; HBRUSH rb=CreateSolidBrush(RGB(rrI.r,rrI.g,rrI.b)); FillRect(dc,&rr,rb); DeleteObject(rb); FrameRect(dc,&rr,(HBRUSH)GetStockObject(BLACK_BRUSH)); }
            }
            // Aero peek outline for hovered taskbar button
            POINT cursor; GetCursorPos(&cursor); ScreenToClient(h,&cursor); RECT crTask; GetClientRect(h,&crTask); int taskTop=crTask.bottom-taskbarH; if(cursor.y>=taskTop){ int hoverId=hitTestTaskbarButton(cursor.x,cursor.y,crTask,taskbarH); if(hoverId){ auto it=g_windows.find(hoverId); if(it!=g_windows.end() && !it->second.minimized){ RECT outline{ it->second.x, it->second.y, it->second.x+it->second.w, it->second.y+it->second.h}; FrameRect(dc,&outline,(HBRUSH)GetStockObject(WHITE_BRUSH)); } } }
            // Taskbar buttons
            int btnX=8+ (sbmp? 40: 30); { std::lock_guard<std::mutex> lk(g_lock); RECT cr2; GetClientRect(h,&cr2); for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it==g_windows.end()) continue; std::string label=std::to_string(id)+":"+it->second.title; SIZE sz; GetTextExtentPoint32A(dc,label.c_str(),(int)label.size(),&sz); int bw=sz.cx+14; RECT br{ btnX, cr2.bottom-taskbarH+4, btnX+bw, cr2.bottom-4}; HBRUSH bbg=CreateSolidBrush(id==g_focus? RGB(90,120,170):(it->second.minimized? RGB(50,50,60):RGB(70,70,90))); FillRect(dc,&br,bbg); DeleteObject(bbg); FrameRect(dc,&br,(HBRUSH)GetStockObject(WHITE_BRUSH)); TextOutA(dc,btnX+6,cr2.bottom-taskbarH+10,label.c_str(),(int)label.size()); btnX += bw + 6; } }
            if(g_resizePreviewActive){ std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(g_resizeWin); if(it!=g_windows.end()){ RECT rr{ it->second.x, it->second.y, it->second.x+g_resizePreviewW, it->second.y+g_resizePreviewH}; FrameRect(dc,&rr,(HBRUSH)GetStockObject(WHITE_BRUSH)); } }
            if(g_snapPreviewActive){ RECT rr=g_snapPreviewRect; FrameRect(dc,&rr,(HBRUSH)GetStockObject(WHITE_BRUSH)); }
            // Alt+Tab overlay with thumbnails
            if(g_altTabOverlayActive){ int count=(int)g_z.size(); int colW=160; int colH=120; int pad=12; int totalW=count*(colW+pad)+pad; int overlayW=totalW; int overlayH=colH+60; int ox=(cr.right-overlayW)/2; int oy=(cr.bottom-overlayH)/2; HBRUSH abg=CreateSolidBrush(RGB(30,30,40)); RECT br{ox,oy,ox+overlayW,oy+overlayH}; FillRect(dc,&br,abg); DeleteObject(abg); FrameRect(dc,&br,(HBRUSH)GetStockObject(WHITE_BRUSH)); int idx=0; for(int i=(int)g_z.size()-1;i>=0;--i){ uint64_t wid=g_z[i]; auto it=g_windows.find(wid); if(it==g_windows.end()) continue; WinInfo &w=it->second; int x=ox+pad+idx*(colW+pad); int y=oy+pad; RECT thumbR{ x,y,x+colW,y+colH}; HBRUSH thB=CreateSolidBrush(wid==g_focus? RGB(70,100,160):RGB(55,55,70)); FillRect(dc,&thumbR,thB); DeleteObject(thB); FrameRect(dc,&thumbR,(HBRUSH)GetStockObject(WHITE_BRUSH)); // simple scaled content approximation: draw rects/texts
                int ty=y+6; std::string ttl=w.title; TextOutA(dc,x+6,ty,ttl.c_str(),(int)ttl.size()); ty+=18; for(size_t r=0;r<w.rects.size() && ty< y+colH-10; ++r){ int rw=w.rects[r].w/4; int rh=w.rects[r].h/4; RECT sr{ x+6, ty, x+6+rw, ty+rh}; HBRUSH rb=CreateSolidBrush(RGB(w.rects[r].r,w.rects[r].g,w.rects[r].b)); FillRect(dc,&sr,rb); DeleteObject(rb); FrameRect(dc,&sr,(HBRUSH)GetStockObject(BLACK_BRUSH)); ty+=rh+4; }
                idx++; }
            }
            EndPaint(h,&ps);
        } return 0;
        case WM_RBUTTONDOWN: {
            int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l);
            RECT cr; GetClientRect(h,&cr); const int taskbarH=32; uint64_t id=hitTestTaskbarButton(mx,my,cr,taskbarH);
            if(id!=0){ enum { CMD_RESTORE=1001, CMD_MINIMIZE=1002, CMD_CLOSE=1003 };
                HMENU m=CreatePopupMenu(); AppendMenuA(m,MF_STRING,CMD_RESTORE,"Restore"); AppendMenuA(m,MF_STRING,CMD_MINIMIZE,"Minimize"); AppendMenuA(m,MF_SEPARATOR,0,nullptr); AppendMenuA(m,MF_STRING,CMD_CLOSE,"Close"); POINT pt{mx,my}; ClientToScreen(h,&pt);
                int cmd=(int)TrackPopupMenu(m,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,pt.x,pt.y,0,h,nullptr); DestroyMenu(m);
                if(cmd){ std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){
                        if(cmd==CMD_RESTORE){ it->second.minimized=false; g_focus=id; }
                        else if(cmd==CMD_MINIMIZE){ it->second.minimized=true; if(g_focus==id) g_focus=0; }
                        else if(cmd==CMD_CLOSE){ g_windows.erase(it); for(auto itZ=g_z.begin(); itZ!=g_z.end(); ++itZ){ if(*itZ==id){ g_z.erase(itZ); break; } } if(g_focus==id) g_focus=0; publishOut(MsgType::MT_Close,std::to_string(id)); }
                    }
                    requestRepaint();
                }
            }
        } return 0;
        case WM_MBUTTONDOWN: {
            int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); RECT cr; GetClientRect(h,&cr); const int taskbarH=32; uint64_t id=hitTestTaskbarButton(mx,my,cr,taskbarH); if(id){ std::lock_guard<std::mutex> lk(g_lock); g_windows.erase(id); for(auto itZ=g_z.begin(); itZ!=g_z.end(); ++itZ){ if(*itZ==id){ g_z.erase(itZ); break; } } if(g_focus==id) g_focus=0; publishOut(MsgType::MT_Close,std::to_string(id)); requestRepaint(); } return 0; }
        case WM_KEYDOWN: case WM_SYSKEYDOWN: {
            int key=(int)w; SHORT alt=GetKeyState(VK_MENU); bool altDown=(alt & 0x8000)!=0; SHORT shiftS=GetKeyState(VK_SHIFT); bool shiftDown=(shiftS & 0x8000)!=0;
            SHORT lwinS=GetKeyState(VK_LWIN); SHORT rwinS=GetKeyState(VK_RWIN); bool winDown=((lwinS|rwinS)&0x8000)!=0;
            if(winDown && (key=='D' || key=='d')){ // Win+D show desktop toggle
                std::lock_guard<std::mutex> lk(g_lock);
                if(!g_showDesktopActive){ g_showDesktopMinimized.clear(); for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.minimized){ it->second.minimized=true; g_showDesktopMinimized.push_back(id);} } g_focus=0; g_showDesktopActive=true; }
                else { for(uint64_t id: g_showDesktopMinimized){ auto it=g_windows.find(id); if(it!=g_windows.end()) it->second.minimized=false; } g_showDesktopMinimized.clear(); g_showDesktopActive=false; }
                requestRepaint();
                return 0;
            }
            if(winDown && (key==VK_LEFT || key==VK_RIGHT || key==VK_UP || key==VK_DOWN)){
                RECT cr; GetClientRect(h,&cr); const int taskbarH=32; std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(g_focus); if(it!=g_windows.end()){ WinInfo &fw=it->second; if(key==VK_LEFT){ fw.maximized=false; fw.x=0; fw.y=0; fw.w=cr.right/2; fw.h=cr.bottom-taskbarH; }
                        else if(key==VK_RIGHT){ fw.maximized=false; fw.x=cr.right/2; fw.y=0; fw.w=cr.right/2; fw.h=cr.bottom-taskbarH; }
                        else if(key==VK_UP){ // maximize
                            if(!fw.maximized){ fw.prevX=fw.x; fw.prevY=fw.y; fw.prevW=fw.w; fw.prevH=fw.h; }
                            fw.x=0; fw.y=0; fw.w=cr.right; fw.h=cr.bottom-taskbarH; fw.maximized=true; }
                        else if(key==VK_DOWN){ // restore if maximized, else minimize
                            if(fw.maximized){ fw.x=fw.prevX; fw.y=fw.prevY; fw.w=fw.prevW; fw.h=fw.prevH; fw.maximized=false; }
                            else { fw.minimized=true; if(g_focus==fw.id) g_focus=0; }
                        }
                        fw.dirty=true; }
                requestRepaint(); return 0; }
            // Win+Shift+Arrow to move window between halves without resizing (toggle left/right)
            if(winDown && shiftDown && (key==VK_LEFT || key==VK_RIGHT)){
                RECT cr; GetClientRect(h,&cr); const int taskbarH=32; std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(g_focus); if(it!=g_windows.end()){ WinInfo &fw=it->second; if(fw.w==cr.right/2 && fw.h==cr.bottom-taskbarH){ if(key==VK_LEFT){ fw.x=0; } else if(key==VK_RIGHT){ fw.x=cr.right/2; } } else { // if not half, snap to half first
                        fw.maximized=false; fw.y=0; fw.w=cr.right/2; fw.h=cr.bottom-taskbarH; fw.x=(key==VK_LEFT)?0:(cr.right/2);
                    } fw.dirty=true; }
                requestRepaint(); return 0; }
            // Alt+Tab overlay toggle (show while Alt held and Tab cycles)
            if(key==VK_MENU){ g_altTabOverlayActive=true; requestRepaint(); }
            publishOut(MsgType::MT_InputKey,std::to_string(key)+"|down");
        } break;
        case WM_KEYUP:{ int key=(int)w; if(key==VK_MENU){ g_altTabOverlayActive=false; requestRepaint(); } publishOut(MsgType::MT_InputKey,std::to_string(key)+"|up"); } break;
        case WM_LBUTTONDOWN:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); Compositor::handleMouse(mx,my,true,false); publishOut(MsgType::MT_InputMouse,std::to_string(mx)+"|"+std::to_string(my)+"|1|down"); } break;
        case WM_LBUTTONUP:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); Compositor::handleMouse(mx,my,false,true); publishOut(MsgType::MT_InputMouse,std::to_string(mx)+"|"+std::to_string(my)+"|1|up"); } break;
        case WM_MOUSEMOVE:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); Compositor::handleMouse(mx,my,false,false); publishOut(MsgType::MT_InputMouse,std::to_string(mx)+"|"+std::to_string(my)+"|0|move"); } break;
        }
        return DefWindowProcA(h,msg,w,l);
    }
#endif

    void Compositor::sendFocus(uint64_t winId){ publishOut(MsgType::MT_SetFocus,std::to_string(winId)); }
    void Compositor::invalidate(uint64_t /*winId*/){
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
        // Drag threshold of 4px before activating move
        if(down && !g_dragActive){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized||w.maximized) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+titleBarH){ if(std::abs(mx-g_dragStartX)>=4 || std::abs(my-g_dragStartY)>=4){ g_dragActive=true; g_dragWin=w.id; g_dragOffX=mx-w.x; g_dragOffY=my-w.y; break; } } } }
        if(down){ uint64_t t=nowMs(); for(int idx=(int)g_z.size()-1; idx>=0; --idx){ uint64_t wid=g_z[idx]; WinInfo &w=g_windows[wid]; if(w.minimized) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+titleBarH){ // double-click maximize/restore (if not minimized)
                if(g_lastClickWin==w.id && (t-g_lastClickTicks)<450){ if(!w.minimized){ if(!w.maximized){ w.prevX=w.x; w.prevY=w.y; w.prevW=w.w; w.prevH=w.h; w.x=0; w.y=0; w.w=cr.right; w.h=cr.bottom-taskbarH; w.maximized=true; } else { w.x=w.prevX; w.y=w.prevY; w.w=w.prevW; w.h=w.prevH; w.maximized=false; } } g_lastClickWin=0; g_lastClickTicks=0; invalidate(w.id); return; } g_lastClickWin=w.id; g_lastClickTicks=t; break; } } }
        if(down){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized||w.maximized) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+titleBarH){ g_dragActive=true; g_dragWin=w.id; g_dragOffX=mx-w.x; g_dragOffY=my-w.y; break; } } }
        if(down){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized||w.maximized) continue; if(mx>=w.x+w.w-gripSize && mx < w.x+w.w && my>=w.y+w.h-gripSize && my < w.y+w.h){ g_resizeActive=true; g_resizeWin=w.id; g_resizeStartW=w.w; g_resizeStartH=w.h; g_resizeStartMX=mx; g_resizeStartMY=my; break; } } }
        if(g_dragActive && up){ // Snap on release (edges + corners)
            auto it=g_windows.find(g_dragWin); if(it!=g_windows.end()){
                WinInfo &w=it->second; const int snap=16; bool nearLeft=false,nearRight=false,nearTop=false,nearBottom=false; RECT mon{0,0,cr.right,cr.bottom};
#ifdef _WIN32
                // Decide monitor containing cursor
                for(auto &m: g_monitors){ if(mx>=m.left && mx<m.right && my>=m.top && my<m.bottom){ mon=m; break; } }
#endif
                int taskbarY = mon.bottom - taskbarH; nearLeft = mx <= mon.left + snap; nearRight = mx >= mon.right - snap; nearTop = my <= mon.top + snap; nearBottom = my >= taskbarY - snap; if(nearTop && !(nearLeft||nearRight)){ w.prevX=w.x; w.prevY=w.y; w.prevW=w.w; w.prevH=w.h; w.x=mon.left; w.y=mon.top; w.w=mon.right-mon.left; w.h=taskbarY-mon.top; w.maximized=true; w.snapState=0; }
                else if(nearLeft && nearTop){ w.maximized=false; w.x=mon.left; w.y=mon.top; w.w=(mon.right-mon.left)/2; w.h=(taskbarY-mon.top)/2; w.snapState=3; }
                else if(nearRight && nearTop){ w.maximized=false; w.x=mon.left+(mon.right-mon.left)/2; w.y=mon.top; w.w=(mon.right-mon.left)/2; w.h=(taskbarY-mon.top)/2; w.snapState=4; }
                else if(nearLeft && nearBottom){ w.maximized=false; w.x=mon.left; w.y=mon.top + (taskbarY-mon.top)/2; w.w=(mon.right-mon.left)/2; w.h=(taskbarY-mon.top)/2; w.snapState=5; }
                else if(nearRight && nearBottom){ w.maximized=false; w.x=mon.left+(mon.right-mon.left)/2; w.y=mon.top + (taskbarY-mon.top)/2; w.w=(mon.right-mon.left)/2; w.h=(taskbarY-mon.top)/2; w.snapState=6; }
                else if(nearLeft){ w.maximized=false; w.x=mon.left; w.y=mon.top; w.w=(mon.right-mon.left)/2; w.h=taskbarY-mon.top; w.snapState=1; }
                else if(nearRight){ w.maximized=false; w.x=mon.left+(mon.right-mon.left)/2; w.y=mon.top; w.w=(mon.right-mon.left)/2; w.h=taskbarY-mon.top; w.snapState=2; }
                w.dirty=true; }
            g_dragActive=false; g_dragWin=0; g_snapPreviewActive=false; invalidate(0);
        }
        if(g_resizeActive && up){ g_resizeActive=false; g_resizeWin=0; }
        if(g_dragActive && !up){ auto it=g_windows.find(g_dragWin); if(it!=g_windows.end()){ it->second.x=mx-g_dragOffX; it->second.y=my-g_dragOffY; it->second.dirty=true; } }
        if(g_resizeActive && !up){ auto it=g_windows.find(g_resizeWin); if(it!=g_windows.end()){ int dw=mx-g_resizeStartMX; int dh=my-g_resizeStartMY; int newW=g_resizeStartW+dw; if(newW<120) newW=120; int newH=g_resizeStartH+dh; if(newH<80) newH=80; g_resizePreviewActive=true; g_resizePreviewW=newW; g_resizePreviewH=newH; } }
        if(g_resizeActive && up){ auto it=g_windows.find(g_resizeWin); if(it!=g_windows.end()){ int dw=mx-g_resizeStartMX; int dh=my-g_resizeStartMY; int newW=g_resizeStartW+dw; if(newW<120) newW=120; int newH=g_resizeStartH+dh; if(newH<80) newH=80; it->second.w=newW; it->second.h=newH; it->second.dirty=true; } g_resizeActive=false; g_resizeWin=0; g_resizePreviewActive=false; }
        if(g_dragActive && !up){ // show snap preview
            const int snap=16; int taskbarTop=cr.bottom-taskbarH; bool nearLeft = mx <= snap; bool nearRight = mx >= cr.right - snap; bool nearTop = my <= snap; bool nearBottom = my >= taskbarTop - snap; if(nearTop && !(nearLeft||nearRight)){ g_snapPreviewActive=true; g_snapPreviewRect={0,0,cr.right,taskbarTop}; }
            else if(nearLeft && nearTop){ g_snapPreviewActive=true; g_snapPreviewRect={0,0,cr.right/2,(taskbarTop)/2}; }
            else if(nearRight && nearTop){ g_snapPreviewActive=true; g_snapPreviewRect={cr.right/2,0,cr.right,(taskbarTop)/2}; }
            else if(nearLeft && nearBottom){ g_snapPreviewActive=true; g_snapPreviewRect={0,(taskbarTop)/2,cr.right/2,taskbarTop}; }
            else if(nearRight && nearBottom){ g_snapPreviewActive=true; g_snapPreviewRect={cr.right/2,(taskbarTop)/2,cr.right,taskbarTop}; }
            else if(nearLeft){ g_snapPreviewActive=true; g_snapPreviewRect={0,0,cr.right/2,taskbarTop}; }
            else if(nearRight){ g_snapPreviewActive=true; g_snapPreviewRect={cr.right/2,0,cr.right,taskbarTop}; }
        }
        if(g_dragActive && up){ g_snapPreviewActive=false; /* snapping handled below (existing) */ }
        if(g_resizeActive && !up){ auto it=g_windows.find(g_resizeWin); if(it!=g_windows.end()){ int dw=mx-g_resizeStartMX; int dh=my-g_resizeStartMY; int newW=g_resizeStartW+dw; if(newW<120) newW=120; int newH=g_resizeStartH+dh; if(newH<80) newH=80; it->second.w=newW; it->second.h=newH; it->second.dirty=true; } }
        if(down){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+titleBarH){ int btnW=18; int minL=w.x+w.w-3*btnW-16; int maxL=w.x+w.w-2*btnW-13; int closeL=w.x+w.w-btnW-4; if(mx>=minL && mx<=minL+btnW){ w.minimized=true; if(g_focus==w.id) g_focus=0; invalidate(w.id); return; } if(mx>=maxL && mx<=maxL+btnW){ if(!w.maximized){ w.prevX=w.x; w.prevY=w.y; w.prevW=w.w; w.prevH=w.h; w.x=0; w.y=0; w.w=cr.right; w.h=cr.bottom-taskbarH; w.maximized=true; } else { w.x=w.prevX; w.y=w.prevY; w.w=w.prevW; w.h=w.prevH; w.maximized=false; } invalidate(w.id); return; } if(mx>=closeL && mx<=closeL+btnW){ uint64_t cid=w.id; g_windows.erase(cid); for(auto itZ=g_z.begin(); itZ!=g_z.end(); ++itZ){ if(*itZ==cid){ g_z.erase(itZ); break; } } if(g_focus==cid) g_focus=0; publishOut(MsgType::MT_Close,std::to_string(cid)); invalidate(0); return; } } } }
        if(down){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+w.h){ g_focus=w.id; for(auto itZ=g_z.begin(); itZ!=g_z.end(); ++itZ){ if(*itZ==w.id){ g_z.erase(itZ); break; } } g_z.push_back(w.id); sendFocus(w.id); break; } } }
        WinInfo* wHit=hitWindowAt(mx,my); if(wHit){ int baseY=wHit->y+titleBarH; for(auto &wg: wHit->widgets){ bool inside=(mx>=wHit->x+wg.x && mx < wHit->x+wg.x+wg.w && my>=baseY+wg.y && my < baseY+wg.y+wg.h); if(!down && !up) wg.hover=inside; if(down && inside){ wg.pressed=true; } if(up){ if(wg.pressed && inside){ emitWidgetEvt(wHit->id,wg.id,"click",wg.text); } wg.pressed=false; } } wHit->dirty=true; }
    }

    void Compositor::handleMessage(const ipc::Message& m){ std::string s(m.data.begin(), m.data.end()); switch((MsgType)m.type){ case MsgType::MT_Create:{ std::istringstream iss(s); std::string title; std::getline(iss,title,'|'); std::string wS,hS; std::getline(iss,wS,'|'); std::getline(iss,hS,'|'); int w=320,h=200; try{ w=std::stoi(wS); h=std::stoi(hS);}catch(...){ } uint64_t id=s_nextWinId.fetch_add(1); { std::lock_guard<std::mutex> lk(g_lock); g_windows[id]=WinInfo{ id,title,40+(int)(id%5)*30,40+(int)(id%5)*30,w,h,{},{} ,{}, false,false,0,0,0,0, true }; g_z.push_back(id); g_focus=id; } publishOut(MsgType::MT_Create,std::to_string(id)+"|"+title); sendFocus(id); invalidate(id); } break; case MsgType::MT_DrawText:{ std::istringstream iss(s); std::string idS; std::getline(iss,idS,'|'); std::string text; std::getline(iss,text); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.texts.push_back(text); it->second.dirty=true; } } publishOut(MsgType::MT_DrawText,std::to_string(id)+"|"+text); invalidate(id); } break; case MsgType::MT_Close:{ uint64_t id=0; try{ id=std::stoull(s);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); g_windows.erase(id); auto it=std::find(g_z.begin(),g_z.end(),id); if(it!=g_z.end()) g_z.erase(it); if(g_focus==id) g_focus=0; } publishOut(MsgType::MT_Close,std::to_string(id)); invalidate(0); } break; case MsgType::MT_DrawRect:{ std::istringstream iss(s); std::string idS; std::getline(iss,idS,'|'); std::string xs,ys,ws,hs,rs,gs,bs; std::getline(iss,xs,'|'); std::getline(iss,ys,'|'); std::getline(iss,ws,'|'); std::getline(iss,hs,'|'); std::getline(iss,rs,'|'); std::getline(iss,gs,'|'); std::getline(iss,bs,'|'); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } DrawRectItem item{ std::stoi(xs), std::stoi(ys), std::stoi(ws), std::stoi(hs), (uint8_t)std::stoi(rs),(uint8_t)std::stoi(gs),(uint8_t)std::stoi(bs)}; { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.rects.push_back(item); it->second.dirty=true; } } publishOut(MsgType::MT_DrawRect,std::to_string(id)); invalidate(id); } break; case MsgType::MT_SetTitle:{ std::istringstream iss(s); std::string idS; std::getline(iss,idS,'|'); std::string title; std::getline(iss,title); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.title=title; it->second.dirty=true; } } publishOut(MsgType::MT_SetTitle,std::to_string(id)+"|"+title); invalidate(id); } break; case MsgType::MT_Move:{ std::istringstream iss(s); std::string idS,xs,ys; std::getline(iss,idS,'|'); std::getline(iss,xs,'|'); std::getline(iss,ys,'|'); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } int nx=std::stoi(xs), ny=std::stoi(ys); { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.maximized){ it->second.x=nx; it->second.y=ny; it->second.dirty=true; } } publishOut(MsgType::MT_Move,std::to_string(id)+"|"+xs+"|"+ys); invalidate(id); } break; case MsgType::MT_Resize:{ std::istringstream iss(s); std::string idS,ws,hs; std::getline(iss,idS,'|'); std::getline(iss,ws,'|'); std::getline(iss,hs,'|'); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } int nw=std::stoi(ws), nh=std::stoi(hs); { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.maximized){ it->second.w=nw; it->second.h=nh; it->second.dirty=true; } } publishOut(MsgType::MT_Resize,std::to_string(id)+"|"+ws+"|"+hs); invalidate(id); } break; case MsgType::MT_WidgetAdd:{ std::istringstream iss(s); std::string idS,typeS,widS,xS,yS,wS,hS,text; std::getline(iss,idS,'|'); std::getline(iss,typeS,'|'); std::getline(iss,widS,'|'); std::getline(iss,xS,'|'); std::getline(iss,yS,'|'); std::getline(iss,wS,'|'); std::getline(iss,hS,'|'); std::getline(iss,text); uint64_t winId=0; try{ winId=std::stoull(idS);}catch(...){ } int typeI=std::stoi(typeS); int wid=std::stoi(widS); int x=std::stoi(xS); int y=std::stoi(yS); int wv=std::stoi(wS); int hv=std::stoi(hS); { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(winId); if(it!=g_windows.end()){ Widget wgt{ (WidgetType)typeI, wid, x,y,wv,hv, text,false,false }; it->second.widgets.push_back(wgt); it->second.dirty=true; } } publishOut(MsgType::MT_WidgetAdd,std::to_string(winId)+"|"+widS); invalidate(winId); } break; case MsgType::MT_WindowList:{ std::ostringstream oss; bool first=true; { std::lock_guard<std::mutex> lk(g_lock); for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it==g_windows.end()) continue; if(!first) oss<<";"; first=false; oss<<it->first<<"|"<<it->second.title<<"|"<<(it->second.minimized?1:0); } } publishOut(MsgType::MT_WindowList,oss.str()); } break; case MsgType::MT_Activate:{ uint64_t id=0; try{ id=std::stoull(s);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); for(auto it=g_z.begin(); it!=g_z.end(); ++it){ if(*it==id){ g_z.erase(it); break; } } auto wit=g_windows.find(id); if(wit!=g_windows.end()){ wit->second.minimized=false; } g_z.push_back(id); g_focus=id; } sendFocus(id); invalidate(id); } break; case MsgType::MT_Minimize:{ uint64_t id=0; try{ id=std::stoull(s);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); auto wit=g_windows.find(id); if(wit!=g_windows.end()){ wit->second.minimized=true; if(g_focus==id) g_focus=0; } } invalidate(id); } break; case MsgType::MT_ShowDesktopToggle:{ if(!g_showDesktopActive){ g_showDesktopMinimized.clear(); for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.minimized){ it->second.minimized=true; g_showDesktopMinimized.push_back(id);} } g_focus=0; g_showDesktopActive=true; } else { for(uint64_t id: g_showDesktopMinimized){ auto it=g_windows.find(id); if(it!=g_windows.end()) it->second.minimized=false; } g_showDesktopMinimized.clear(); g_showDesktopActive=false; } invalidate(0); } break; case MsgType::MT_StateSave:{ std::string path=s; std::vector<SavedWindow> sw; { std::lock_guard<std::mutex> lk(g_lock); for(size_t i=0;i<g_z.size();++i){ uint64_t id=g_z[i]; auto it=g_windows.find(id); if(it==g_windows.end()) continue; const WinInfo &w=it->second; SavedWindow rec; rec.id=w.id; rec.title=w.title; rec.x=w.x; rec.y=w.y; rec.w=w.w; rec.h=w.h; rec.minimized=w.minimized; rec.maximized=w.maximized; rec.z=(int)i; rec.focused=(g_focus==w.id); rec.snap=w.snapState; sw.push_back(rec);} } std::string err; if(!DesktopState::Save(path, sw, err)) publishOut(MsgType::MT_WidgetEvt,std::string("STATE_SAVE_ERR|")+err); else publishOut(MsgType::MT_WidgetEvt,std::string("STATE_SAVE_OK|")+path); } break; case MsgType::MT_StateLoad:{ std::string path=s; std::vector<SavedWindow> sw; std::string err; if(!DesktopState::Load(path, sw, err)){ publishOut(MsgType::MT_WidgetEvt,std::string("STATE_LOAD_ERR|")+err); } else { { std::lock_guard<std::mutex> lk(g_lock); g_windows.clear(); g_z.clear(); g_focus=0; std::sort(sw.begin(), sw.end(), [](const SavedWindow&a,const SavedWindow&b){ return a.z<b.z; }); for(auto &w: sw){ uint64_t id=s_nextWinId.fetch_add(1); WinInfo wi{ id, w.title, w.x, w.y, w.w, w.h, {}, {}, {}, w.minimized, w.maximized, 0,0,0,0, true, w.snap }; if(wi.maximized){ RECT crL{0,0,1024,768};
#ifdef _WIN32
                            if(g_hwnd) GetClientRect(g_hwnd,&crL);
#endif
                            int taskbarY=crL.bottom-32; wi.x=crL.left; wi.y=crL.top; wi.w=crL.right-crL.left; wi.h=taskbarY-crL.top; }
                        g_windows[id]=wi; g_z.push_back(id); if(w.focused && !wi.minimized) g_focus=id; } } publishOut(MsgType::MT_WidgetEvt,std::string("STATE_LOAD_OK|")+path); invalidate(0); } } break; case MsgType::MT_Invalidate:{
#ifdef _WIN32
        requestRepaint();
#endif
    } break; case MsgType::MT_Ping:{ publishOut(MsgType::MT_Ping,s); } break; default: break; } }

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
        { std::vector<SavedWindow> sw; std::string err; if(DesktopState::Load("desktop.state", sw, err)){ std::lock_guard<std::mutex> lk(g_lock); g_windows.clear(); g_z.clear(); g_focus=0; std::sort(sw.begin(), sw.end(), [](const SavedWindow&a,const SavedWindow&b){ return a.z<b.z; }); for(auto &w: sw){ uint64_t id=s_nextWinId.fetch_add(1); WinInfo wi{ id,w.title,w.x,w.y,w.w,w.h, {}, {}, {}, w.minimized, w.maximized, 0,0,0,0, true, w.snap }; if(wi.maximized){ RECT crL{0,0,1024,768};
#ifdef _WIN32
                            if(g_hwnd) GetClientRect(g_hwnd,&crL);
#endif
                            int taskbarY=crL.bottom-32; wi.x=crL.left; wi.y=crL.top; wi.w=crL.right-crL.left; wi.h=taskbarY-crL.top; } g_windows[id]=wi; g_z.push_back(id); if(w.focused && !wi.minimized) g_focus=id; } } }
        bool running=true; while(running){ pumpEvents(); ipc::Message m; if(ipc::Bus::pop(kGuiChanIn,m,30)){ if(m.type==(uint32_t)MsgType::MT_Ping && m.data.size()==3 && std::string(m.data.begin(),m.data.end())=="bye") running=false; else handleMessage(m); } }
        { std::vector<SavedWindow> sw; { std::lock_guard<std::mutex> lk(g_lock); for(auto &kv: g_windows){ sw.push_back(SavedWindow{ kv.second.id, kv.second.title, kv.second.x, kv.second.y, kv.second.w, kv.second.h, kv.second.minimized, kv.second.maximized }); } } std::string err; DesktopState::Save("desktop.state", sw, err); }
#ifdef _WIN32
        shutdownWindow();
#endif
        Logger::write(LogLevel::Info,"Compositor service stopping"); return 0; }
    uint64_t Compositor::start(){ ProcessSpec spec{"compositor", Compositor::main}; return ProcessTable::spawn(spec,{"compositor"}); }
}
} // namespace gxos::gui
