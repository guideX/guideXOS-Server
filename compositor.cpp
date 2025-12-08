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
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

    // Start menu keyboard/selection state
    int Compositor::g_startMenuSel = 0; int Compositor::g_startMenuScroll = 0;
    bool Compositor::g_startMenuAllProgs = false; // "All Programs" view toggle
    std::vector<std::string> Compositor::g_startMenuAllProgsSorted; // Alphabetically sorted app list
    bool Compositor::g_taskbarMenuVisible = false;
    RECT Compositor::g_taskbarMenuRect{0,0,0,0};
    int Compositor::g_taskbarMenuSel = 0;

    static uint64_t nowMs(){ return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    static void publishOut(MsgType type, const std::string& payload){ ipc::Message out; out.type=(uint32_t)type; out.data.assign(payload.begin(), payload.end()); ipc::Bus::publish(kGuiChanOut, std::move(out), false); }

    void Compositor::refreshDesktopItems(){ g_items.clear(); // pinned first
        for(const auto& p: g_cfg.pinned){ DesktopItem di; di.label=p; di.action=p; di.pinned=true; g_items.push_back(di);} for(const auto& r: g_cfg.recent){ if(std::find(g_cfg.pinned.begin(), g_cfg.pinned.end(), r)!=g_cfg.pinned.end()) continue; DesktopItem di; di.label=r; di.action=r; di.pinned=false; g_items.push_back(di);} }
    
    void Compositor::refreshAllProgramsList(){
        // Load all registered apps from DesktopService and sort alphabetically
        g_startMenuAllProgsSorted.clear();
        // For now, use a default list matching C# implementation
        g_startMenuAllProgsSorted.push_back("Calculator");
        g_startMenuAllProgsSorted.push_back("Clock");
        g_startMenuAllProgsSorted.push_back("Console");
        g_startMenuAllProgsSorted.push_back("Notepad");
        g_startMenuAllProgsSorted.push_back("Paint");
        g_startMenuAllProgsSorted.push_back("TaskManager");
        // Sort alphabetically (case-insensitive)
        std::sort(g_startMenuAllProgsSorted.begin(), g_startMenuAllProgsSorted.end(), 
            [](const std::string& a, const std::string& b) {
                std::string al = a, bl = b;
                std::transform(al.begin(), al.end(), al.begin(), ::tolower);
                std::transform(bl.begin(), bl.end(), bl.begin(), ::tolower);
                return al < bl;
            });
    }
    
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
    
    // helper: draw a bitmap centered in rect
    static void drawBitmapCentered(HDC dc, HBITMAP hb, RECT r){ if(!hb) return; HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,hb); BITMAP bm{}; GetObject(hb,sizeof(bm),&bm); int w=bm.bmWidth,h=bm.bmHeight; int dx = r.left + ((r.right-r.left)-w)/2; int dy = r.top + ((r.bottom-r.top)-h)/2; BitBlt(dc,dx,dy,w,h,mem,0,0,SRCCOPY); SelectObject(mem,old); DeleteDC(mem); }

    LRESULT CALLBACK Compositor::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l){
        switch(msg){
        case WM_CLOSE: PostQuitMessage(0); return 0;
        case WM_SIZE: { RECT cr; GetClientRect(h,&cr); std::lock_guard<std::mutex> lk(g_lock); int taskbarH=32; for(auto &kv: g_windows){ WinInfo &wi=kv.second; if(wi.maximized){ wi.x=cr.left; wi.y=cr.top; wi.w=cr.right-cr.left; wi.h=cr.bottom-taskbarH; wi.dirty=true; } } requestRepaint(); return 0; }
        case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT cr; GetClientRect(h,&cr); if(g_wallpaperBmp){ HDC mem=CreateCompatibleDC(dc); HGDIOBJ old=SelectObject(mem,g_wallpaperBmp); BITMAP bm{}; GetObject(g_wallpaperBmp,sizeof(bm),&bm); double sx=(double)(cr.right-cr.left)/bm.bmWidth; double sy=(double)(cr.bottom-cr.top)/bm.bmHeight; double s=sx<sy? sx: sy; int dstW=(int)(bm.bmWidth*s); int dstH=(int)(bm.bmHeight*s); int dx=(cr.right-dstW)/2; int dy=(cr.bottom-dstH)/2; HBRUSH bg=CreateSolidBrush(RGB(25,25,30)); FillRect(dc,&cr,bg); DeleteObject(bg); SetStretchBltMode(dc,HALFTONE); StretchBlt(dc,dx,dy,dstW,dstH,mem,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY); SelectObject(mem,old); DeleteDC(mem);} else { HBRUSH bg=CreateSolidBrush(RGB(25,25,30)); FillRect(dc,&cr,bg); DeleteObject(bg);} drawDesktopIcons(dc, cr);
            // Draw application windows in Z-order (bottom to top)
            const int titleBarH = 24; HFONT font=(HFONT)GetStockObject(ANSI_VAR_FONT); SelectObject(dc,font); SetBkMode(dc,TRANSPARENT);
            for(size_t i=0;i<g_z.size(); ++i){ auto it=g_windows.find(g_z[i]); if(it==g_windows.end()) continue; const WinInfo &winfo=it->second; if(winfo.minimized) continue; RECT wrect{ winfo.x, winfo.y, winfo.x + winfo.w, winfo.y + winfo.h };
                HBRUSH wbg=CreateSolidBrush(RGB(60,60,70)); FillRect(dc,&wrect,wbg); DeleteObject(wbg);
                RECT tbr{ winfo.x, winfo.y, winfo.x + winfo.w, winfo.y + titleBarH }; HBRUSH tbg=CreateSolidBrush(winfo.id==g_focus? RGB(80,110,160):RGB(70,70,85)); FillRect(dc,&tbr,tbg); DeleteObject(tbg);
                FrameRect(dc,&wrect,(HBRUSH)GetStockObject(WHITE_BRUSH));
                SetTextColor(dc, RGB(240,240,240)); TextOutA(dc, tbr.left + 8, tbr.top + 4, winfo.title.c_str(), (int)winfo.title.size());

                // Titlebar buttons (minimize, maximize/restore, close)
                // compute button geometry
                const int btnSize = 16; const int btnGap = 6;
                int closeLeft = winfo.x + winfo.w - btnGap - btnSize;
                int maxLeft = closeLeft - btnGap - btnSize;
                int minLeft = maxLeft - btnGap - btnSize;
                RECT closeR{ closeLeft, winfo.y + (titleBarH - btnSize)/2, closeLeft + btnSize, winfo.y + (titleBarH - btnSize)/2 + btnSize };
                RECT maxR{ maxLeft, winfo.y + (titleBarH - btnSize)/2, maxLeft + btnSize, winfo.y + (titleBarH - btnSize)/2 + btnSize };
                RECT minR{ minLeft, winfo.y + (titleBarH - btnSize)/2, minLeft + btnSize, winfo.y + (titleBarH - btnSize)/2 + btnSize };
                // draw each button using hover/pressed state
                // Close
                HBRUSH cbg = CreateSolidBrush(winfo.titleBtnClosePressed? RGB(180,60,60) : (winfo.titleBtnCloseHover? RGB(200,80,80) : RGB(120,120,120)));
                FillRect(dc, &closeR, cbg); DeleteObject(cbg);
                FrameRect(dc, &closeR, (HBRUSH)GetStockObject(BLACK_BRUSH));
                SetTextColor(dc, RGB(255,255,255)); TextOutA(dc, closeR.left + 4, closeR.top + 2, "x", 1);
                // Maximize
                HBRUSH mgb = CreateSolidBrush(winfo.titleBtnMaxPressed? RGB(120,140,200) : (winfo.titleBtnMaxHover? RGB(140,160,220) : RGB(100,100,120)));
                FillRect(dc, &maxR, mgb); DeleteObject(mgb);
                FrameRect(dc, &maxR, (HBRUSH)GetStockObject(BLACK_BRUSH));
                // draw square for max/restore
                SetTextColor(dc, RGB(255,255,255)); if(winfo.maximized) TextOutA(dc, maxR.left + 3, maxR.top + 2, "?", 3); else TextOutA(dc, maxR.left + 3, maxR.top + 2, "?", 3);
                // Minimize
                HBRUSH mbg = CreateSolidBrush(winfo.titleBtnMinPressed? RGB(120,140,120) : (winfo.titleBtnMinHover? RGB(140,160,140) : RGB(100,100,120)));
                FillRect(dc, &minR, mbg); DeleteObject(mbg);
                FrameRect(dc, &minR, (HBRUSH)GetStockObject(BLACK_BRUSH));
                SetTextColor(dc, RGB(255,255,255)); TextOutA(dc, minR.left + 4, minR.top + 2, "_", 1);

                for(const auto &ri : winfo.rects){ RECT rr{ winfo.x + ri.x, winfo.y + titleBarH + ri.y, winfo.x + ri.x + ri.w, winfo.y + titleBarH + ri.h }; HBRUSH rb=CreateSolidBrush(RGB(ri.r,ri.g,ri.b)); FillRect(dc,&rr,rb); DeleteObject(rb);} 
                for(const auto &wd : winfo.widgets){ RECT wr{ winfo.x + wd.x, winfo.y + titleBarH + wd.y, winfo.x + wd.x + wd.w, winfo.y + titleBarH + wd.y + wd.h }; HBRUSH wb=CreateSolidBrush(wd.pressed? RGB(40,80,140) : (wd.hover? RGB(70,90,120):RGB(90,90,100))); FillRect(dc,&wr,wb); DeleteObject(wb); FrameRect(dc,&wr,(HBRUSH)GetStockObject(WHITE_BRUSH)); SetTextColor(dc, RGB(240,240,240)); TextOutA(dc, wr.left+6, wr.top+4, wd.text.c_str(), (int)wd.text.size()); }
                int ty = winfo.y + titleBarH + 8; for(const auto &tx : winfo.texts){ TextOutA(dc, winfo.x + 8, ty, tx.c_str(), (int)tx.size()); ty += 16; }
                if(winfo.tombstoned){ const char* t="Tombstoned"; SIZE ts; GetTextExtentPoint32A(dc,t,(int)strlen(t),&ts); SetTextColor(dc, RGB(200,100,100)); TextOutA(dc, winfo.x + (winfo.w - ts.cx)/2, winfo.y + (winfo.h - ts.cy)/2, t, (int)strlen(t)); }
            }
            int taskbarH=32; RECT tb{cr.left,cr.bottom-taskbarH,cr.right,cr.bottom}; HBRUSH tbBg=CreateSolidBrush(RGB(40,40,50)); FillRect(dc,&tb,tbBg); DeleteObject(tbBg);
            RECT startBtn{8,cr.bottom-taskbarH+4,8+32,cr.bottom-4}; HBRUSH sbg=CreateSolidBrush(g_startMenuVisible? RGB(80,110,160):RGB(60,80,100)); FillRect(dc,&startBtn,sbg); DeleteObject(sbg); FrameRect(dc,&startBtn,(HBRUSH)GetStockObject(WHITE_BRUSH)); drawBitmapCentered(dc, g_startBtnBmp, startBtn);
            // Taskbar buttons
            POINT cursor; GetCursorPos(&cursor); ScreenToClient(h,&cursor); int btnX=50; for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it==g_windows.end()) continue; std::string label=it->second.title; SIZE sz; GetTextExtentPoint32A(dc,label.c_str(),(int)label.size(),&sz); int bw=sz.cx+30; RECT br{ btnX, cr.bottom-taskbarH+4, btnX+bw, cr.bottom-4}; bool hover=(cursor.x>=br.left && cursor.x<=br.right && cursor.y>=br.top && cursor.y<=br.bottom); HBRUSH bbg=CreateSolidBrush( hover? RGB(90,130,190): (id==g_focus? RGB(80,110,160):(it->second.minimized? RGB(45,45,55): (it->second.tombstoned? RGB(90,70,40):RGB(70,70,85)))) ); FillRect(dc,&br,bbg); DeleteObject(bbg); FrameRect(dc,&br,(HBRUSH)GetStockObject(WHITE_BRUSH)); RECT iconRect{ br.left+4, br.top+4, br.left+20, br.top+20 }; drawBitmapCentered(dc, it->second.taskbarIcon, iconRect); SetTextColor(dc,RGB(240,240,240)); TextOutA(dc,br.left+24,br.top+10,label.c_str(),(int)label.size()); btnX += bw + 6; }
            // Start menu popup (pinned + recent OR all programs)
            if(g_startMenuVisible){ 
                int smW=440; // wider to accommodate two columns
                int maxRows=14; 
                int rowH=20;
                int leftColW = 260; // left list column width
                int rightColW = 160; // right column for shortcuts
                int smH=maxRows*rowH + 10; 
                RECT sm{ startBtn.left, startBtn.top - smH - 6, startBtn.left+smW, startBtn.top - 6}; 
                if(sm.top<0){ sm.top=4; sm.bottom=sm.top+smH; } 
                g_startMenuRect=sm; 
                HBRUSH mBg=CreateSolidBrush(RGB(45,45,55)); 
                FillRect(dc,&sm,mBg); 
                DeleteObject(mBg); 
                FrameRect(dc,&sm,(HBRUSH)GetStockObject(WHITE_BRUSH)); 
                
                // Left column - Recent/All Programs list
                int y=sm.top+4; 
                HFONT f=(HFONT)GetStockObject(ANSI_VAR_FONT); 
                SelectObject(dc,f); 
                SetBkMode(dc,TRANSPARENT); 
                SetTextColor(dc,RGB(230,230,230)); 
                int row=0; 
                int startIndex = g_startMenuScroll;
                
                if(g_startMenuAllProgs){
                    // Show all programs alphabetically
                    for(size_t i=startIndex;i<g_startMenuAllProgsSorted.size() && row<maxRows; ++i){ 
                        RECT r{ sm.left+4, y, sm.left+leftColW-4, y+rowH}; 
                        bool isSel = ((int)i==g_startMenuSel); 
                        HBRUSH rb=CreateSolidBrush(isSel? RGB(80,100,150):(cursor.x>=r.left && cursor.x<=r.right && cursor.y>=r.top && cursor.y<=r.bottom? RGB(70,90,130):RGB(55,55,70))); 
                        FillRect(dc,&r,rb); 
                        DeleteObject(rb); 
                        std::string txt = g_startMenuAllProgsSorted[i];
                        TextOutA(dc,r.left+4,r.top+4,txt.c_str(),(int)txt.size()); 
                        y+=rowH; row++; 
                    }
                } else {
                    // Show pinned + recent
                    for(size_t i=startIndex;i<g_items.size() && row<maxRows; ++i){ 
                        RECT r{ sm.left+4, y, sm.left+leftColW-4, y+rowH}; 
                        bool isSel = ((int)i==g_startMenuSel); 
                        HBRUSH rb=CreateSolidBrush(isSel? RGB(80,100,150):(cursor.x>=r.left && cursor.x<=r.right && cursor.y>=r.top && cursor.y<=r.bottom? RGB(70,90,130):RGB(55,55,70))); 
                        FillRect(dc,&r,rb); 
                        DeleteObject(rb); 
                        std::string txt=(g_items[i].pinned? "* ":"  ")+g_items[i].label; 
                        TextOutA(dc,r.left+4,r.top+4,txt.c_str(),(int)txt.size()); 
                        y+=rowH; row++; 
                    }
                }
                
                // Right column - shortcuts
                int rcX = sm.left + leftColW + 4;
                int rcY = sm.top + 6;
                SetTextColor(dc,RGB(200,200,200));
                
                // Computer Files shortcut
                RECT rcComputer{ rcX, rcY, sm.right-6, rcY+rowH };
                bool overComp = (cursor.x>=rcComputer.left && cursor.x<=rcComputer.right && cursor.y>=rcComputer.top && cursor.y<=rcComputer.bottom);
                if(overComp){ HBRUSH hb=CreateSolidBrush(RGB(70,90,130)); FillRect(dc,&rcComputer,hb); DeleteObject(hb); }
                TextOutA(dc, rcComputer.left+6, rcComputer.top+4, "Computer Files", 14);
                rcY += rowH + 4;
                
                // Console shortcut
                RECT rcConsole{ rcX, rcY, sm.right-6, rcY+rowH };
                bool overCon = (cursor.x>=rcConsole.left && cursor.x<=rcConsole.right && cursor.y>=rcConsole.top && cursor.y<=rcConsole.bottom);
                if(overCon){ HBRUSH hb=CreateSolidBrush(RGB(70,90,130)); FillRect(dc,&rcConsole,hb); DeleteObject(hb); }
                TextOutA(dc, rcConsole.left+6, rcConsole.top+4, "Console", 7);
                rcY += rowH + 4;
                
                // Recent Documents shortcut
                RECT rcDocs{ rcX, rcY, sm.right-6, rcY+rowH };
                bool overDocs = (cursor.x>=rcDocs.left && cursor.x<=rcDocs.right && cursor.y>=rcDocs.top && cursor.y<=rcDocs.bottom);
                if(overDocs){ HBRUSH hb=CreateSolidBrush(RGB(70,90,130)); FillRect(dc,&rcDocs,hb); DeleteObject(hb); }
                TextOutA(dc, rcDocs.left+6, rcDocs.top+4, "Recent Docs", 11);
                
                // Bottom area - "All Programs" toggle button
                int btnY = sm.bottom - 30;
                RECT allProgBtn{ sm.left+6, btnY, sm.left+leftColW-6, btnY+24 };
                bool overAllProg = (cursor.x>=allProgBtn.left && cursor.x<=allProgBtn.right && cursor.y>=allProgBtn.top && cursor.y<=allProgBtn.bottom);
                HBRUSH apb = CreateSolidBrush(overAllProg ? RGB(70,80,100) : RGB(60,60,75));
                FillRect(dc, &allProgBtn, apb); DeleteObject(apb);
                FrameRect(dc, &allProgBtn, (HBRUSH)GetStockObject(WHITE_BRUSH));
                const char* btnText = g_startMenuAllProgs ? "< Back" : "All Programs >";
                TextOutA(dc, allProgBtn.left+8, allProgBtn.top+6, btnText, (int)strlen(btnText));
                
                // Power menu area (bottom-right)
                int shutdownBtnW = 80;
                int shutdownBtnH = 24;
                RECT shutdownBtn{ sm.right-shutdownBtnW-30, btnY, sm.right-30, btnY+shutdownBtnH };
                bool overShutdown = (cursor.x>=shutdownBtn.left && cursor.x<=shutdownBtn.right && cursor.y>=shutdownBtn.top && cursor.y<=shutdownBtn.bottom);
                HBRUSH sdb = CreateSolidBrush(overShutdown ? RGB(80,40,40) : RGB(60,60,75));
                FillRect(dc, &shutdownBtn, sdb); DeleteObject(sdb);
                FrameRect(dc, &shutdownBtn, (HBRUSH)GetStockObject(WHITE_BRUSH));
                TextOutA(dc, shutdownBtn.left+10, shutdownBtn.top+6, "Shutdown", 8);
            }
            
            // Capture framebuffer for VNC if server is running
            if(vnc::VncServer::IsRunning()) {
                HDC memDC = CreateCompatibleDC(dc);
                HBITMAP memBitmap = CreateCompatibleBitmap(dc, cr.right - cr.left, cr.bottom - cr.top);
                HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);
                BitBlt(memDC, 0, 0, cr.right - cr.left, cr.bottom - cr.top, dc, 0, 0, SRCCOPY);
                
                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = cr.right - cr.left;
                bmi.bmiHeader.biHeight = -(cr.bottom - cr.top); // negative for top-down
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                
                std::vector<uint8_t> pixels((cr.right - cr.left) * (cr.bottom - cr.top) * 4);
                GetDIBits(memDC, memBitmap, 0, cr.bottom - cr.top, pixels.data(), &bmi, DIB_RGB_COLORS);
                vnc::VncServer::UpdateFramebuffer(pixels.data(), cr.right - cr.left, cr.bottom - cr.top, (cr.right - cr.left) * 4);
                
                SelectObject(memDC, oldBitmap);
                DeleteObject(memBitmap);
                DeleteDC(memDC);
            }
            
            EndPaint(h,&ps); return 0; }
        case WM_LBUTTONDOWN:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); RECT cr; GetClientRect(h,&cr); int taskbarH=32; RECT startBtn{8,cr.bottom-taskbarH+4,8+32,cr.bottom-4}; // Start button toggle
            if(mx>=startBtn.left && mx<=startBtn.right && my>=startBtn.top && my<=startBtn.bottom){ 
                g_startMenuVisible=!g_startMenuVisible; 
                if(g_startMenuVisible){ 
                    g_startMenuSel=0; 
                    g_startMenuScroll=0;
                    g_startMenuAllProgs = false; // reset to recent view
                    refreshAllProgramsList(); // ensure sorted list is ready
                } 
                requestRepaint(); 
                return 0; 
            }
            // Start menu click
            if(g_startMenuVisible){ 
                // Check "All Programs" toggle button
                int smW=440;
                int leftColW = 260;
                int btnY = g_startMenuRect.bottom - 30;
                RECT allProgBtn{ g_startMenuRect.left+6, btnY, g_startMenuRect.left+leftColW-6, btnY+24 };
                if(mx>=allProgBtn.left && mx<=allProgBtn.right && my>=allProgBtn.top && my<=allProgBtn.bottom){
                    g_startMenuAllProgs = !g_startMenuAllProgs;
                    g_startMenuSel = 0;
                    g_startMenuScroll = 0;
                    requestRepaint();
                    return 0;
                }
                
                // Check Shutdown button
                int shutdownBtnW = 80;
                int shutdownBtnH = 24;
                RECT shutdownBtn{ g_startMenuRect.right-shutdownBtnW-30, btnY, g_startMenuRect.right-30, btnY+shutdownBtnH };
                if(mx>=shutdownBtn.left && mx<=shutdownBtn.right && my>=shutdownBtn.top && my<=shutdownBtn.bottom){
                    // Publish shutdown event
                    Logger::write(LogLevel::Info, "Shutdown requested from Start Menu");
                    publishOut(MsgType::MT_WidgetEvt, "SHUTDOWN");
                    g_startMenuVisible = false;
                    requestRepaint();
                    return 0;
                }
                
                // Check right column shortcuts
                int rcX = g_startMenuRect.left + leftColW + 4;
                int rcY = g_startMenuRect.top + 6;
                int rowH = 20;
                
                // Computer Files
                RECT rcComputer{ rcX, rcY, g_startMenuRect.right-6, rcY+rowH };
                if(mx>=rcComputer.left && mx<=rcComputer.right && my>=rcComputer.top && my<=rcComputer.bottom){
                    launchAction("ComputerFiles");
                    g_startMenuVisible = false;
                    requestRepaint();
                    return 0;
                }
                rcY += rowH + 4;
                
                // Console
                RECT rcConsole{ rcX, rcY, g_startMenuRect.right-6, rcY+rowH };
                if(mx>=rcConsole.left && mx<=rcConsole.right && my>=rcConsole.top && my<=rcConsole.bottom){
                    launchAction("Console");
                    g_startMenuVisible = false;
                    requestRepaint();
                    return 0;
                }
                rcY += rowH + 4;
                
                // Recent Documents - just close menu for now
                RECT rcDocs{ rcX, rcY, g_startMenuRect.right-6, rcY+rowH };
                if(mx>=rcDocs.left && mx<=rcDocs.right && my>=rcDocs.top && my<=rcDocs.bottom){
                    Logger::write(LogLevel::Info, "Recent Documents clicked (not implemented)");
                    // Future: show popout with recent documents
                    requestRepaint();
                    return 0;
                }
                
                // List item click
                int listTop = g_startMenuRect.top+4;
                int listBottom = btnY - 4; // above buttons
                if(mx>=g_startMenuRect.left && mx<=g_startMenuRect.left+leftColW && my>=listTop && my<=listBottom){ 
                    int idx=(my - listTop)/rowH + g_startMenuScroll; 
                    int itemCount = g_startMenuAllProgs ? (int)g_startMenuAllProgsSorted.size() : (int)g_items.size();
                    if(idx>=0 && idx<itemCount){ 
                        uint64_t now=nowMs(); 
                        if(g_lastItemIndex==idx && (now-g_lastItemClickTicks)<450){ 
                            // Double-click: launch
                            std::string action = g_startMenuAllProgs ? g_startMenuAllProgsSorted[idx] : g_items[idx].action;
                            launchAction(action); 
                            g_startMenuVisible=false; 
                        } else { 
                            // Single click: select
                            g_lastItemIndex=idx; 
                            g_lastItemClickTicks=now; 
                            g_startMenuSel = idx;
                        } 
                        requestRepaint(); 
                        return 0; 
                    } 
                } else { 
                    g_startMenuVisible=false; 
                }
            }
            // Desktop icon click (selection / double)
            const int margin=16; const int iconW=64; const int iconH=64; const int cellW=iconW+20; const int cellH=iconH+34; int cols=(cr.right-margin*2)/cellW; if(cols<1) cols=1; if(my < cr.bottom-taskbarH){ int col=(mx-margin)/cellW; int row=(my-margin)/cellH; if(col>=0 && row>=0){ int idx=row*cols+col; if(idx>=0 && idx<(int)g_items.size()){ uint64_t now=nowMs(); if(g_lastItemIndex==idx && (now-g_lastItemClickTicks)<450){ launchAction(g_items[idx].action); } else { for(auto &di: g_items) di.selected=false; g_items[idx].selected=true; g_lastItemIndex=idx; g_lastItemClickTicks=now; } requestRepaint(); return 0; } } }
            // Taskbar button click (minimize/restore/tombstone)
            uint64_t id=hitTestTaskbarButton(mx,my,cr,taskbarH); if(id){ std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ WinInfo &w=it->second; if(!w.minimized && !w.tombstoned){ w.tombstoned=true; w.minimized=true; if(g_focus==w.id) g_focus=0; } else { w.tombstoned=false; w.minimized=false; g_focus=w.id; for(auto itZ=g_z.begin(); itZ!=g_z.end(); ++itZ){ if(*itZ==id){ g_z.erase(itZ); break; } } g_z.push_back(id); } } requestRepaint(); return 0; }
            // pass to widget handling and general mouse handling
            Compositor::handleMouse(mx,my,true,false); publishOut(MsgType::MT_InputMouse,std::to_string(mx)+"|"+std::to_string(my)+"|1|down"); return 0; }
        case WM_RBUTTONDOWN:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); if(g_startMenuVisible && mx>=g_startMenuRect.left && mx<=g_startMenuRect.right && my>=g_startMenuRect.top && my<=g_startMenuRect.bottom){ int idx=(my - (g_startMenuRect.top+4))/20; if(idx>=0 && idx<(int)g_items.size()){ if(g_items[idx].pinned) unpinAction(g_items[idx].action); else pinAction(g_items[idx].action); requestRepaint(); return 0; } }
            // Desktop icon right-click pin/unpin
            RECT cr; GetClientRect(h,&cr); int taskbarH=32; if(my < cr.bottom-taskbarH){ const int margin=16; const int iconW=64; const int cellW=iconW+20; const int cellH=64+34; int cols=(cr.right-margin*2)/cellW; if(cols<1) cols=1; int col=(mx-margin)/cellW; int row=(my-margin)/cellH; if(col>=0 && row>=0){ int idx=row*cols+col; if(idx>=0 && idx<(int)g_items.size()){ if(g_items[idx].pinned) unpinAction(g_items[idx].action); else pinAction(g_items[idx].action); requestRepaint(); return 0; } } }
        } break;
        case WM_LBUTTONUP:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); Compositor::handleMouse(mx,my,false,true); publishOut(MsgType::MT_InputMouse,std::to_string(mx)+"|"+std::to_string(my)+"|1|up"); } break;
        case WM_MOUSEMOVE:{ int mx=GET_X_LPARAM(l); int my=GET_Y_LPARAM(l); Compositor::handleMouse(mx,my,false,false); publishOut(MsgType::MT_InputMouse,std::to_string(mx)+"|"+std::to_string(my)+"|0|move"); } break;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: {
            int key=(int)w;
            // start-menu navigation handled here
            if(g_startMenuVisible){
                int maxItems = g_startMenuAllProgs ? (int)g_startMenuAllProgsSorted.size() : (int)g_items.size();
                if(key==VK_UP){ 
                    if(g_startMenuSel>0) g_startMenuSel--; 
                    if(g_startMenuSel<g_startMenuScroll) g_startMenuScroll=g_startMenuSel; 
                    requestRepaint(); 
                    return 0; 
                }
                if(key==VK_DOWN){ 
                    if(maxItems>0 && g_startMenuSel<maxItems-1) g_startMenuSel++; 
                    const int maxRows=14; 
                    if(g_startMenuSel>=g_startMenuScroll+maxRows) g_startMenuScroll=g_startMenuSel-maxRows+1; 
                    requestRepaint(); 
                    return 0; 
                }
                if(key==VK_RETURN){ 
                    if(g_startMenuSel>=0 && g_startMenuSel<maxItems){ 
                        std::string action = g_startMenuAllProgs ? g_startMenuAllProgsSorted[g_startMenuSel] : g_items[g_startMenuSel].action;
                        launchAction(action); 
                        g_startMenuVisible=false; 
                        requestRepaint(); 
                    } 
                    return 0; 
                }
                if(key==VK_ESCAPE){ 
                    g_startMenuVisible=false; 
                    requestRepaint(); 
                    return 0; 
                }
                if(key==VK_TAB){ 
                    // Toggle between Recent and All Programs
                    g_startMenuAllProgs = !g_startMenuAllProgs;
                    g_startMenuSel = 0;
                    g_startMenuScroll = 0;
                    requestRepaint();
                    return 0;
                }
            }

            publishOut(MsgType::MT_InputKey,std::to_string(key)+"|down");
        } break;
        case WM_KEYUP:{ int key=(int)w; publishOut(MsgType::MT_InputKey,std::to_string(key)+"|up"); } break;
        }
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
        // find topmost window under cursor
        WinInfo* topW=nullptr; for(int idx=(int)g_z.size()-1; idx>=0; --idx){ auto it=g_windows.find(g_z[idx]); if(it==g_windows.end()) continue; WinInfo &w=it->second; if(w.minimized||w.tombstoned) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+w.h){ topW=&w; break; } }
        // Titlebar button handling (hover/press/click)
        if(topW){ // compute button rects for this window
            const int btnSize = 16; const int btnGap = 6;
            int closeLeft = topW->x + topW->w - btnGap - btnSize;
            int maxLeft = closeLeft - btnGap - btnSize;
            int minLeft = maxLeft - btnGap - btnSize;
            bool overClose = (mx>=closeLeft && mx < closeLeft+btnSize && my>=topW->y && my < topW->y+titleBarH);
            bool overMax = (mx>=maxLeft && mx < maxLeft+btnSize && my>=topW->y && my < topW->y+titleBarH);
            bool overMin = (mx>=minLeft && mx < minLeft+btnSize && my>=topW->y && my < topW->y+titleBarH);
            // mouse move -> update hover
            if(!down && !up){ if(topW->titleBtnCloseHover != overClose){ topW->titleBtnCloseHover = overClose; invalidate(topW->id); } if(topW->titleBtnMaxHover != overMax){ topW->titleBtnMaxHover = overMax; invalidate(topW->id); } if(topW->titleBtnMinHover != overMin){ topW->titleBtnMinHover = overMin; invalidate(topW->id); } }
            // mouse down -> set pressed if over
            if(down){ if(overClose) { topW->titleBtnClosePressed = true; invalidate(topW->id); } if(overMax){ topW->titleBtnMaxPressed = true; invalidate(topW->id); } if(overMin){ topW->titleBtnMinPressed = true; invalidate(topW->id); } }
            // mouse up -> perform action if pressed
            if(up){ if(topW->titleBtnClosePressed){ // close
                    uint64_t id = topW->id;
                    topW->titleBtnClosePressed = false; topW->titleBtnCloseHover = false; invalidate(id);
                    // remove window
                    g_windows.erase(id);
                    for(auto it=g_z.begin(); it!=g_z.end(); ++it){ if(*it==id){ g_z.erase(it); break; } }
                    if(g_focus==id) g_focus=0;
                    publishOut(MsgType::MT_Close, std::to_string(id));
                    return; }
                if(topW->titleBtnMinPressed){ // minimize
                    uint64_t id=topW->id;
                    topW->titleBtnMinPressed = false; topW->titleBtnMinHover = false; topW->minimized=true; topW->tombstoned=true; if(g_focus==id) g_focus=0; invalidate(id); return; }
                if(topW->titleBtnMaxPressed){ // maximize/restore
                    uint64_t id=topW->id;
                    // toggle maximize state
                    if(!topW->maximized){ // maximize
                        topW->prevX = topW->x; topW->prevY = topW->y; topW->prevW = topW->w; topW->prevH = topW->h;
#ifdef _WIN32
                        RECT crL{0,0,1024,768}; if(g_hwnd) GetClientRect(g_hwnd,&crL);
#else
                        RECT crL{0,0,1024,768};
#endif
                        int taskbarY = crL.bottom - taskbarH;
                        topW->x = crL.left; topW->y = crL.top; topW->w = crL.right - crL.left; topW->h = taskbarY - crL.top; topW->maximized = true; topW->snapState = 0; topW->dirty = true;
                    } else { // restore
                        topW->x = topW->prevX; topW->y = topW->prevY; topW->w = topW->prevW; topW->h = topW->prevH; topW->maximized = false; topW->dirty = true;
                    }
                    topW->titleBtnMaxPressed = false; topW->titleBtnMaxHover = false; invalidate(id); return; }
            }
        }

        if(topW){ int wx = mx - topW->x; int wy = my - topW->y - titleBarH; for(auto &wd: topW->widgets){ bool over = (wx>=wd.x && wx < wd.x+wd.w && wy>=wd.y && wy < wd.y+wd.h); if(!down && !up){ if(wd.hover!=over){ wd.hover=over; invalidate(topW->id); } } else if(down){ if(over){ wd.pressed=true; wd.hover=true; invalidate(topW->id); } } else if(up){ if(wd.pressed){ if(over){ emitWidgetEvt(topW->id, wd.id, "click", ""); Logger::write(LogLevel::Info, std::string("Widget clicked: ")+std::to_string(topW->id)+"/"+std::to_string(wd.id)); } wd.pressed=false; wd.hover=false; invalidate(topW->id); } } } }
        // move while dragging
        if(g_dragActive && !up){ auto it=g_windows.find(g_dragWin); if(it!=g_windows.end()){ WinInfo &w=it->second; if(!w.maximized && !w.minimized && !w.tombstoned){ int nx=mx-g_dragOffX; int ny=my-g_dragOffY; if(nx<cr.left) nx=cr.left; if(ny<cr.top) ny=cr.top; if(nx+w.w>cr.right) nx=cr.right-w.w; if(ny+w.h>cr.bottom-taskbarH) ny=cr.bottom-taskbarH-w.h; if(nx!=w.x || ny!=w.y){ w.x=nx; w.y=ny; w.dirty=true; invalidate(w.id); } } } }
        if(down){ uint64_t t=nowMs(); for(int idx=(int)g_z.size()-1; idx>=0; --idx){ uint64_t wid=g_z[idx]; WinInfo &w=g_windows[wid]; if(w.minimized||w.tombstoned) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+titleBarH){ if(g_lastClickWin==w.id && (t-g_lastClickTicks)<450){ if(!w.minimized){ if(!w.maximized){ w.prevX=w.x; w.prevY=w.y; w.prevW=w.w; w.prevH=w.h; w.x=0; w.y=0; w.w=cr.right; w.h=cr.bottom-taskbarH; w.maximized=true; } else { w.x=w.prevX; w.y=w.prevY; w.w=w.prevW; w.h=w.prevH; w.maximized=false; } } g_lastClickWin=0; g_lastClickTicks=0; invalidate(w.id); return; } g_lastClickWin=w.id; g_lastClickTicks=t; break; } } }
        if(down){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized||w.maximized||w.tombstoned) continue; if(mx>=w.x+w.w-gripSize && mx < w.x+w.w && my>=w.y+w.h-gripSize && my < w.y+w.h){ g_resizeActive=true; g_resizeWin=w.id; g_resizeStartW=w.w; g_resizeStartH=w.h; g_resizeStartMX=mx; g_resizeStartMY=my; break; } } }
        if(g_dragActive && up){ auto it=g_windows.find(g_dragWin); if(it!=g_windows.end()){ WinInfo &w=it->second; const int snap=16; bool nearLeft=mx<=snap, nearRight=mx>=cr.right-snap, nearTop=my<=snap; bool nearBottom=my>=cr.bottom-taskbarH - snap; if(nearTop && !(nearLeft||nearRight)){ w.prevX=w.x; w.prevY=w.y; w.prevW=w.w; w.prevH=w.h; w.x=0; w.y=0; w.w=cr.right; w.h=cr.bottom-taskbarH; w.maximized=true; w.snapState=0; } else if(nearLeft){ w.maximized=false; w.x=0; w.y=0; w.w=cr.right/2; w.h=cr.bottom-taskbarH; w.snapState=1; } else if(nearRight){ w.maximized=false; w.x=cr.right/2; w.y=0; w.w=cr.right/2; w.h=cr.bottom-taskbarH; w.snapState=2; } w.dirty=true; } g_dragActive=false; g_dragWin=0; g_snapPreviewActive=false; invalidate(0); }
        if(g_resizeActive && !up){ auto it=g_windows.find(g_resizeWin); if(it!=g_windows.end()){ int dw=mx-g_resizeStartMX; int dh=my-g_resizeStartMY; int newW=g_resizeStartW+dw; if(newW<160) newW=160; int newH=g_resizeStartH+dh; if(newH<120) newH=120; g_resizePreviewActive=true; g_resizePreviewW=newW; g_resizePreviewH=newH; } }
        if(g_resizeActive && up){ auto it=g_windows.find(g_resizeWin); if(it!=g_windows.end()){ int dw=mx-g_resizeStartMX; int dh=my-g_resizeStartMY; int newW=g_resizeStartW+dw; if(newW<160) newW=160; int newH=g_resizeStartH+dh; if(newH<120) newH=120; it->second.w=newW; it->second.h=newH; it->second.dirty=true; } g_resizeActive=false; g_resizeWin=0; g_resizePreviewActive=false; }
        if(down){ for(int idx=(int)g_z.size()-1; idx>=0; --idx){ WinInfo &w=g_windows[g_z[idx]]; if(w.minimized||w.tombstoned) continue; if(mx>=w.x && mx < w.x+w.w && my>=w.y && my < w.y+w.h){ g_focus=w.id; for(auto itZ=g_z.begin(); itZ!=g_z.end(); ++itZ){ if(*itZ==w.id){ g_z.erase(itZ); break; } } g_z.push_back(w.id); sendFocus(w.id); break; } } }
    }

    void Compositor::handleMessage(const ipc::Message& m){ std::string s(m.data.begin(), m.data.end()); switch((MsgType)m.type){
        case MsgType::MT_Create:{ std::istringstream iss(s); std::string title; std::getline(iss,title,'|'); std::string wS,hS; std::getline(iss,wS,'|'); std::getline(iss,hS,'|'); int w=320,h=200; try{ w=std::stoi(wS); h=std::stoi(hS);}catch(...){ } uint64_t id=s_nextWinId.fetch_add(1); { std::lock_guard<std::mutex> lk(g_lock); g_windows[id]=WinInfo{ id,title,60+(int)(id%7)*40,60+(int)(id%7)*40,w,h,{},{} ,{}, false,false,0,0,0,0, true }; g_z.push_back(id); g_focus=id; g_windows[id].taskbarIcon = Icons::TaskbarIcon(16); } publishOut(MsgType::MT_Create,std::to_string(id)+"|"+title); sendFocus(id); invalidate(id); } break;
        case MsgType::MT_DrawText:{ std::istringstream iss(s); std::string idS; std::getline(iss,idS,'|'); std::string text; std::getline(iss,text); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.texts.push_back(text); it->second.dirty=true; } } publishOut(MsgType::MT_DrawText,std::to_string(id)+"|"+text); invalidate(id); } break;
        case MsgType::MT_Close:{ uint64_t id=0; try{ id=std::stoull(s);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); g_windows.erase(id); auto it=std::find(g_z.begin(),g_z.end(),id); if(it!=g_z.end()) g_z.erase(it); if(g_focus==id) g_focus=0; } publishOut(MsgType::MT_Close,std::to_string(id)); invalidate(0); } break;
        case MsgType::MT_DrawRect:{ std::istringstream iss(s); std::string idS; std::getline(iss,idS,'|'); std::string xs,ys,ws,hs,rs,gs,bs; std::getline(iss,xs,'|'); std::getline(iss,ys,'|'); std::getline(iss,ws,'|'); std::getline(iss,hs,'|'); std::getline(iss,rs,'|'); std::getline(iss,gs,'|'); std::getline(iss,bs,'|'); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } DrawRectItem item{ std::stoi(xs), std::stoi(ys), std::stoi(ws), std::stoi(hs), (uint8_t)std::stoi(rs),(uint8_t)std::stoi(gs),(uint8_t)std::stoi(bs)}; { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.rects.push_back(item); it->second.dirty=true; } } publishOut(MsgType::MT_DrawRect,std::to_string(id)); invalidate(id); } break;
        case MsgType::MT_SetTitle:{ std::istringstream iss(s); std::string idS; std::getline(iss,idS,'|'); std::string title; std::getline(iss,title); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.title=title; it->second.dirty=true; } } publishOut(MsgType::MT_SetTitle,std::to_string(id)+"|"+title); invalidate(id); } break;
        case MsgType::MT_Move:{ std::istringstream iss(s); std::string idS,xs,ys; std::getline(iss,idS,'|'); std::getline(iss,xs,'|'); std::getline(iss,ys,'|'); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } int nx=std::stoi(xs), ny=std::stoi(ys); { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.maximized){ it->second.x=nx; it->second.y=ny; it->second.dirty=true; } } publishOut(MsgType::MT_Move,std::to_string(id)+"|"+xs+"|"+ys); invalidate(id); } break;
        case MsgType::MT_Resize:{ std::istringstream iss(s); std::string idS,ws,hs; std::getline(iss,idS,'|'); std::getline(iss,ws,'|'); std::getline(iss,hs,'|'); uint64_t id=0; try{ id=std::stoull(idS);}catch(...){ } int nw=std::stoi(ws), nh=std::stoi(hs); { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.maximized){ it->second.w=nw; it->second.h=nh; it->second.dirty=true; } } publishOut(MsgType::MT_Resize,std::to_string(id)+"|"+ws+"|"+hs); invalidate(id); } break;
        case MsgType::MT_WidgetAdd:{ // format: <winId>|<type>|<id>|<x>|<y>|<w>|<h>|<text>
            std::istringstream iss(s); std::string winS, typeS, idS, xs, ys, ws2, hs2; std::getline(iss,winS,'|'); std::getline(iss,typeS,'|'); std::getline(iss,idS,'|'); std::getline(iss,xs,'|'); std::getline(iss,ys,'|'); std::getline(iss,ws2,'|'); std::getline(iss,hs2,'|'); std::string rest; std::getline(iss,rest); uint64_t winId=0; try{ winId=std::stoull(winS);}catch(...){ } int wtype=0; try{ wtype=std::stoi(typeS);}catch(...){ } int wid=0; try{ wid=std::stoi(idS);}catch(...){ } int wx=0, wy=0, ww=0, wh=0; try{ wx=std::stoi(xs); wy=std::stoi(ys); ww=std::stoi(ws2); wh=std::stoi(hs2);}catch(...){ }
            Widget wd; wd.type = (WidgetType)wtype; wd.id=wid; wd.x=wx; wd.y=wy; wd.w=ww; wd.h=wh; wd.text=rest;
            { std::lock_guard<std::mutex> lk(g_lock); auto it=g_windows.find(winId); if(it!=g_windows.end()){ it->second.widgets.push_back(wd); it->second.dirty=true; Logger::write(LogLevel::Info, std::string("Widget added to ")+std::to_string(winId)+" id="+std::to_string(wid)); } }
            publishOut(MsgType::MT_WidgetAdd, std::to_string(winId)+"|"+std::to_string(wid)); invalidate(winId);
        } break;
        case MsgType::MT_WindowList:{ std::ostringstream oss; bool first=true; { std::lock_guard<std::mutex> lk(g_lock); for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it==g_windows.end()) continue; if(!first) oss<<";"; first=false; oss<<it->first<<"|"<<it->second.title<<"|"<<(it->second.minimized?1:0); } } publishOut(MsgType::MT_WindowList,oss.str()); } break;
        case MsgType::MT_Activate:{ uint64_t id=0; try{ id=std::stoull(s);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); for(auto it=g_z.begin(); it!=g_z.end(); ++it){ if(*it==id){ g_z.erase(it); break; } } auto wit=g_windows.find(id); if(wit!=g_windows.end()){ wit->second.minimized=false; wit->second.tombstoned=false; } g_z.push_back(id); g_focus=id; } sendFocus(id); invalidate(id); } break;
        case MsgType::MT_Minimize:{ uint64_t id=0; try{ id=std::stoull(s);}catch(...){ } { std::lock_guard<std::mutex> lk(g_lock); auto wit=g_windows.find(id); if(wit!=g_windows.end()){ wit->second.minimized=true; wit->second.tombstoned=true; if(g_focus==id) g_focus=0; } } invalidate(id); } break;
        case MsgType::MT_ShowDesktopToggle:{ if(!g_showDesktopActive){ g_showDesktopMinimized.clear(); for(uint64_t id: g_z){ auto it=g_windows.find(id); if(it!=g_windows.end() && !it->second.minimized){ it->second.minimized=true; it->second.tombstoned=true; g_showDesktopMinimized.push_back(id);} } g_focus=0; g_showDesktopActive=true; } else { for(uint64_t id: g_showDesktopMinimized){ auto it=g_windows.find(id); if(it!=g_windows.end()){ it->second.minimized=false; it->second.tombstoned=false; } } g_showDesktopMinimized.clear(); g_showDesktopActive=false; } invalidate(0); } break;
        case MsgType::MT_StateSave:{ std::string path=s; std::vector<SavedWindow> sw; { std::lock_guard<std::mutex> lk(g_lock); for(size_t i=0;i<g_z.size();++i){ uint64_t id=g_z[i]; auto it=g_windows.find(id); if(it==g_windows.end()) continue; const WinInfo &w=it->second; SavedWindow rec; rec.id=w.id; rec.title=w.title; rec.x=w.x; rec.y=w.y; rec.w=w.w; rec.h=w.h; rec.minimized=w.minimized; rec.maximized=w.maximized; rec.z=(int)i; rec.focused=(g_focus==w.id); rec.snap=w.snapState; sw.push_back(rec);} } std::string err; if(!DesktopState::Save(path, sw, err)) publishOut(MsgType::MT_WidgetEvt,std::string("STATE_SAVE_ERR|")+err); else publishOut(MsgType::MT_WidgetEvt,std::string("STATE_SAVE_OK|")+path); } break;
        case MsgType::MT_StateLoad:{ std::string path=s; std::vector<SavedWindow> sw; std::string err; if(!DesktopState::Load(path, sw, err)){ publishOut(MsgType::MT_WidgetEvt,std::string("STATE_LOAD_ERR|")+err); } else { { std::lock_guard<std::mutex> lk(g_lock); g_windows.clear(); g_z.clear(); g_focus=0; std::sort(sw.begin(), sw.end(), [](const SavedWindow&a,const SavedWindow&b){ return a.z<b.z; }); for(auto &w: sw){ uint64_t id=s_nextWinId.fetch_add(1); WinInfo wi{ id, w.title, w.x, w.y, w.w, w.h, {}, {}, {}, w.minimized, w.maximized, 0,0,0,0, true, w.snap }; if(wi.maximized){ RECT crL{0,0,1024,768}; if(g_hwnd) GetClientRect(g_hwnd,&crL); int taskbarY=crL.bottom-32; wi.x=crL.left; wi.y=crL.top; wi.w=crL.right-crL.left; wi.h=taskbarY-crL.top; } g_windows[id]=wi; g_z.push_back(id); if(w.focused && !wi.minimized) g_focus=id; } } publishOut(MsgType::MT_WidgetEvt,std::string("STATE_LOAD_OK|")+path); invalidate(0); } } break;
        case MsgType::MT_Invalidate:{ invalidate(0); } break;
        case MsgType::MT_Ping:{ publishOut(MsgType::MT_Ping,s); } break;
        case MsgType::MT_DesktopLaunch:{ launchAction(s); } break;
        case MsgType::MT_DesktopPins:{ std::istringstream iss(s); std::string tok; while(std::getline(iss,tok,';')){ if(tok.size()<2) continue; if(tok[0]=='+') pinAction(tok.substr(1)); else if(tok[0]=='-') unpinAction(tok.substr(1)); } } break;
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

    int Compositor::main(int argc, char** argv){ 
        Logger::write(LogLevel::Info,"Compositor service started (native window)"); 
        ipc::Bus::ensure(kGuiChanIn); 
        ipc::Bus::ensure(kGuiChanOut);
#ifdef _WIN32
        initWindow();
#endif
        DesktopConfigData cfg; std::string cfgErr; bool cfgOk = DesktopConfig::Load("desktop.json", cfg, cfgErr);
        g_cfg = cfg; // Store config
        refreshDesktopItems(); // Populate g_items from pinned/recent
        refreshAllProgramsList(); // Populate sorted all programs list
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
