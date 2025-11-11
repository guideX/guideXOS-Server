#include "allocator.h"
#include "logger.h"
#include "scheduler.h"
#include "fs.h"
#include "platform.h"
#include "process.h"
#include "ipc.h"
#include "ipc_bus.h"
#include "console_service.h"
#include "compositor.h"
#include "gui_protocol.h"
#include "vfs.h"
#include "gxm_loader.h"
#include "desktop_config.h"
#include <iostream>
#include <chrono>
#include <sstream>
#include <vector>
#include <iomanip>
#include <unordered_map>
#include <thread>

namespace gxos {
    PlatformInfo queryPlatform(){ PlatformInfo pi{}; pi.cpuCount = std::thread::hardware_concurrency(); pi.totalMemBytes = 512ull*1024*1024; pi.startTicks = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); return pi; }
    uint64_t ticks(){ return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

// Sample process entries
static int echoProc(int argc, char** argv){ gxos::Logger::write(gxos::LogLevel::Info, "echoProc start"); for(int i=1;i<argc;i++){ gxos::Logger::write(gxos::LogLevel::Info, std::string("ARG ")+argv[i]); } return 0; }
static int workerProc(int argc, char** argv){ int loops=5; if(argc>1) loops=std::atoi(argv[1]); for(int i=0;i<loops;i++){ gxos::Logger::write(gxos::LogLevel::Info, "worker tick "+std::to_string(i)); std::this_thread::sleep_for(std::chrono::milliseconds(100)); } return loops; }

static void help(){
    std::cout << "Commands:\n"
                 " mem | alloc <n> | tasks | log\n"
                 " spawn <echo|worker> [args...] | plist\n"
                 " send <pid> <text> | recv <pid>\n"
                 " bus.sub <chan> <pid> | bus.unsub <chan> <pid>\n"
                 " bus.pub <chan> <text> [fanout] | bus.pop <chan> [timeoutMs]\n"
                 " bus.cap <chan> <cap> | bus.stats <chan>\n"
                 " console.start | console.send <text> | console.pop [timeoutMs]\n"
                 " gui.start | gui.win <title> [w h] | gui.text <id> <text> | gui.close <id>\n"
                 " gui.rect <id> <x> <y> <w> <h> <r> <g> <b> | gui.move <id> <x> <y> | gui.resize <id> <w> <h> | gui.title <id> <title>\n"
                 " gui.btn <win> <id> <x> <y> <w> <h> <text> | gui.pop | gui.wlist | gui.activate <id> | gui.min <id>\n"
                 " gxm.load <path> | gxm.sample | gui.save <path> | gui.load <path>\n"
                 " desktop.wallpaper <path> | desktop.launch <action> | desktop.pin <action> | desktop.unpin <action> | desktop.showconfig\n"
                 " taskbar.list | taskbar.activate <id> | taskbar.min <id> | taskbar.close <id>\n"
                 " proc.wait <pid> [timeoutMs] | proc.status <pid>\n"
                 " vfs.mkdir <path> | vfs.write <path> <text> | vfs.read <path> | vfs.ls <path>\n"
                 " pbytes | help | quit/exit\n"; }

int main(){
    using namespace gxos;
    Logger::write(LogLevel::Info, "guideXOSServer server starting...");
    auto pi = queryPlatform();
    Allocator::init(512ull*1024*1024); // 512MB simulated heap
    Scheduler::init(pi.cpuCount>0? pi.cpuCount:2);

    // Registerable specs
    std::unordered_map<std::string, ProcessSpec> specs{
        {"echo", ProcessSpec{"echo", echoProc}},
        {"worker", ProcessSpec{"worker", workerProc}},
    };

    uint64_t consolePid = 0; // console service pid
    uint64_t compositorPid = 0; // compositor service pid

    std::string line; help();
    while (std::getline(std::cin, line)){
        if (line=="quit"||line=="exit") break;
        std::istringstream iss(line); std::string cmd; iss>>cmd;
        if (cmd=="gui.save"){
            std::string path; iss>>path; if(path.empty()){ std::cout<<"gui.save <path>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_StateSave; m.data.assign(path.begin(), path.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Save requested"<<std::endl; continue; }
        if (cmd=="gui.load"){
            std::string path; iss>>path; if(path.empty()){ std::cout<<"gui.load <path>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_StateLoad; m.data.assign(path.begin(), path.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Load requested"<<std::endl; continue; }
        if (cmd=="gui.btn"){
            std::string winS; int id,x,y,w,h; iss>>winS>>id>>x>>y>>w>>h; std::string rest; std::getline(iss,rest); if(!rest.empty() && rest[0]==' ') rest.erase(0,1); if(winS.empty()){ std::cout<<"Usage: gui.btn <win> <id> <x> <y> <w> <h> <text>"<<std::endl; continue; }
            std::ostringstream oss; oss<<winS<<"|"<<1 /*Button*/<<"|"<<id<<"|"<<x<<"|"<<y<<"|"<<w<<"|"<<h<<"|"<<rest; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WidgetAdd; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Button queued"<<std::endl; continue; }
        if (cmd=="gui.wlist"){ ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WindowList; ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Requested window list (use gui.pop)"<<std::endl; continue; }
        if (cmd=="gui.activate"){ std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"gui.activate <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Activate; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Activate sent"<<std::endl; continue; }
        if (cmd=="gui.min"){ std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"gui.min <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Minimize; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Minimize sent"<<std::endl; continue; }
        if (cmd=="gxm.load"){
            std::string path; iss>>path; if(path.empty()){ std::cout<<"Usage: gxm.load <path>"<<std::endl; continue; } std::string err; if(gui::GxmLoader::ExecuteFile(path, err)) std::cout<<"GXM executed"<<std::endl; else std::cout<<"GXM error: "<<err<<std::endl; continue; }
        if (cmd=="gxm.sample"){
            std::string script =
                "WIN Sample|420|300\n"
                "TEXT 1000|Welcome to GXM Sample\n"
                "RECT 1000|20|60|120|40|180|60|60\n"
                "BTN 1000|1|20|120|110|32|Click Me\n";
            std::string err; if(gui::GxmLoader::ExecuteText(script, err)) std::cout<<"Sample executed"<<std::endl; else std::cout<<"Sample error: "<<err<<std::endl; continue; }
        if (cmd=="gui.start"){
            if(compositorPid==0){ compositorPid = gui::Compositor::start(); std::cout<<"Compositor pid="<<compositorPid<<" (proto="<<gui::kGuiProtocolVersion<<")"<<std::endl; }
            else std::cout<<"Compositor already running pid="<<compositorPid<<std::endl;
        } else if (cmd=="gui.win"){
            std::string title; iss>>title; int w=640,h=480; iss>>w>>h; if(title.empty()){ std::cout<<"Usage: gui.win <title> [w h]"<<std::endl; continue; }
            ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Create; std::string payload = title+"|"+std::to_string(w)+"|"+std::to_string(h); m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Requested window: "<<title<<std::endl;
        } else if (cmd=="gui.text"){
            std::string idS; iss>>idS; std::string rest; std::getline(iss, rest); if(rest.size()>0 && rest[0]==' ') rest.erase(0,1); ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DrawText; std::string payload = idS+"|"+rest; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Text queued"<<std::endl;
        } else if (cmd=="gui.close"){ std::string idS; iss>>idS; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Close; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Close requested"<<std::endl;
        } else if (cmd=="gui.rect"){ std::string idS; int x,y,w,h,r,g,b; iss>>idS>>x>>y>>w>>h>>r>>g>>b; std::ostringstream oss; oss<<idS<<"|"<<x<<"|"<<y<<"|"<<w<<"|"<<h<<"|"<<r<<"|"<<g<<"|"<<b; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DrawRect; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Rect queued"<<std::endl;
        } else if (cmd=="gui.move"){ std::string idS; int x,y; iss>>idS>>x>>y; std::ostringstream oss; oss<<idS<<"|"<<x<<"|"<<y; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Move; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Move queued"<<std::endl;
        } else if (cmd=="gui.resize"){ std::string idS; int w,h; iss>>idS>>w>>h; std::ostringstream oss; oss<<idS<<"|"<<w<<"|"<<h; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Resize; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Resize queued"<<std::endl;
        } else if (cmd=="gui.title"){ std::string idS; std::string rest; iss>>idS; std::getline(iss,rest); if(!rest.empty() && rest[0]==' ') rest.erase(0,1); std::ostringstream oss; oss<<idS<<"|"<<rest; ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_SetTitle; auto s=oss.str(); m.data.assign(s.begin(), s.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Title queued"<<std::endl;
        } else if (cmd=="gui.pop"){ ipc::Message m; if(ipc::Bus::pop("gui.output", m, 200)){ std::string s(m.data.begin(), m.data.end()); std::cout<<"GUI: type="<<m.type<<" payload="<<s<<std::endl; } else std::cout<<"No GUI events"<<std::endl;
        } else if (cmd=="mem"){ std::cout << "mem in use=" << Allocator::bytesInUse()/1024 << " KB peak=" << Allocator::peakBytes()/1024 << " KB" << std::endl;
        } else if (cmd=="alloc"){ size_t sz; if(!(iss>>sz)) sz=1; void* p = Allocator::alloc(sz, AllocTag::Temp); std::cout << "Allocated " << sz << " bytes ptr=" << p << std::endl;
        } else if (cmd=="tasks"){ std::cout << "tasks executed=" << Scheduler::tasksExecuted() << std::endl;
        } else if (cmd=="log"){ auto snap = Logger::snapshot(); for(auto& e: snap){ std::cout << (int)e.level << ": " << e.msg << std::endl; }
        } else if (cmd=="spawn"){ std::string name; iss>>name; if(name.empty()){ std::cout<<"Usage: spawn <spec> [args...]"<<std::endl; continue; } auto it = specs.find(name); if(it==specs.end()){ std::cout<<"No spec"<<std::endl; continue; } std::vector<std::string> args; std::string a; while(iss>>a) args.push_back(a); uint64_t pid = ProcessTable::spawn(it->second, args); std::cout<<"Spawned pid="<<pid<<std::endl;
        } else if (cmd=="plist"){ auto list = ProcessTable::list(); std::cout<<"Processes:"; for(auto id:list) std::cout<<" "<<id; std::cout<<std::endl;
        } else if (cmd=="send"){ uint64_t pid; iss>>pid; std::string payload; std::getline(iss, payload); if(payload.size()>0 && payload[0]==' ') payload.erase(0,1);
            ipc::Message m; m.srcPid=0; m.dstPid=pid; m.type=1; m.data.assign(payload.begin(), payload.end()); if(ProcessTable::send(pid, std::move(m))) std::cout<<"Sent"<<std::endl; else std::cout<<"Send failed"<<std::endl;
        } else if (cmd=="recv"){ uint64_t pid; iss>>pid; ipc::Message m; if(ProcessTable::try_recv(pid,m)){ std::string s(m.data.begin(), m.data.end()); std::cout<<"Message from "<<m.srcPid<<": "<<s<<std::endl; } else std::cout<<"No message"<<std::endl;
        } else if (cmd=="bus.sub"){ std::string chan; uint64_t pid; iss>>chan>>pid; if(chan.empty()){ std::cout<<"bus.sub <chan> <pid>"<<std::endl; continue; } ipc::Bus::subscribe(chan,pid); std::cout<<"Subscribed"<<std::endl;
        } else if (cmd=="bus.unsub"){ std::string chan; uint64_t pid; iss>>chan>>pid; ipc::Bus::unsubscribe(chan,pid); std::cout<<"Unsubscribed"<<std::endl;
        } else if (cmd=="bus.pub"){ std::string chan; iss>>chan; std::string payload; std::getline(iss,payload); if(payload.size()>0 && payload[0]==' ') payload.erase(0,1); bool fanout=false; std::string f; iss>>f; if(f=="fanout") fanout=true; ipc::Message m; m.srcPid=0; m.type=2; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish(chan, std::move(m), fanout); std::cout<<"Published"<<(fanout?" (fanout)":"")<<std::endl;
        } else if (cmd=="bus.pop"){ std::string chan; iss>>chan; uint64_t to=0; iss>>to; if(to==0) to=100; ipc::Message m; if(ipc::Bus::pop(chan,m,to)){ std::string s(m.data.begin(), m.data.end()); std::cout<<"POP["<<chan<<"] type="<<m.type<<" payload="<<s<<std::endl; } else std::cout<<"Timeout"<<std::endl;
        } else if (cmd=="bus.cap"){ std::string chan; size_t cap; iss>>chan>>cap; if(chan.empty()||cap==0){ std::cout<<"bus.cap <chan> <cap>"<<std::endl; continue; } ipc::Bus::setCapacity(chan, cap); std::cout<<"Capacity set"<<std::endl;
        } else if (cmd=="bus.stats"){ std::string chan; iss>>chan; std::cout<<"chan="<<chan<<" pending="<<ipc::Bus::pending(chan)<<" cap="<<ipc::Bus::capacity(chan)<<std::endl;
        } else if (cmd=="console.start"){ if(consolePid==0){ consolePid = svc::ConsoleService::start(); std::cout<<"Console pid="<<consolePid<<std::endl; }
            else std::cout<<"Console already running pid="<<consolePid<<std::endl;
        } else if (cmd=="console.send"){ std::string payload; std::getline(iss,payload); if(payload.size()>0 && payload[0]==' ') payload.erase(0,1); ipc::Message m; m.srcPid=0; m.type=10; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("console.input", std::move(m), false); std::cout<<"Sent to console"<<std::endl;
        } else if (cmd=="console.pop"){ uint64_t to=0; iss>>to; if(to==0) to=200; ipc::Message m; if(ipc::Bus::pop("console.output", m, to)){ std::string s(m.data.begin(), m.data.end()); std::cout<<"Console: "<<s<<std::endl; } else std::cout<<"No console output"<<std::endl;
        } else if (cmd=="mem"){ std::cout << "mem in use=" << Allocator::bytesInUse()/1024 << " KB peak=" << Allocator::peakBytes()/1024 << " KB" << std::endl;
        } else if (cmd=="pbytes"){ auto list = Allocator::listPidBytes(); std::cout<<"PID   BYTES"<<std::endl; for(auto& pr:list){ std::cout<<std::setw(5)<<pr.first<<" "<<pr.second<<std::endl; }
        } else if (cmd=="help"){ help();
        }
        // Desktop and Taskbar convenience commands
        else if (cmd=="desktop.wallpaper"){
            std::string path; iss>>path; if(path.empty()){ std::cout<<"desktop.wallpaper <path>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DesktopWallpaperSet; m.data.assign(path.begin(), path.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Desktop wallpaper set request sent: "<<path<<std::endl; }
        else if (cmd=="desktop.launch"){
            std::string action; std::getline(iss, action); if(action.size()>0 && action[0]==' ') action.erase(0,1); if(action.empty()){ std::cout<<"desktop.launch <action>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DesktopLaunch; m.data.assign(action.begin(), action.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Desktop launch requested: "<<action<<std::endl; }
        else if (cmd=="desktop.pin" || cmd=="desktop.unpin"){
            std::string action; std::getline(iss, action); if(action.size()>0 && action[0]==' ') action.erase(0,1); if(action.empty()){ std::cout<< (cmd=="desktop.pin"?"desktop.pin <action>":"desktop.unpin <action>") << std::endl; continue; }
            std::vector<std::pair<bool,std::string>> ops; ops.emplace_back(cmd=="desktop.pin", action);
            std::string payload = gui::packPins(ops);
            ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_DesktopPins; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Desktop pin/unpin request sent: "<<payload<<std::endl; }
        else if (cmd=="desktop.showconfig"){
            gxos::gui::DesktopConfigData cfg; std::string err; if(!gxos::gui::DesktopConfig::Load("desktop.json", cfg, err)){ std::cout<<"Failed to load desktop.json: "<<err<<std::endl; } else { std::cout<<"Wallpaper: "<<cfg.wallpaperPath<<std::endl; std::cout<<"Pinned:\n"; for(auto &p: cfg.pinned) std::cout<<"  "<<p<<std::endl; std::cout<<"Recent:\n"; for(auto &r: cfg.recent) std::cout<<"  "<<r<<std::endl; }
        }
        else if (cmd=="taskbar.list"){
            ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_WindowList; ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Requested window list (use gui.pop)"<<std::endl; }
        else if (cmd=="taskbar.activate"){
            std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"taskbar.activate <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Activate; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Activate sent"<<std::endl; }
        else if (cmd=="taskbar.min"){
            std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"taskbar.min <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Minimize; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Minimize sent"<<std::endl; }
        else if (cmd=="taskbar.close"){
            std::string idS; iss>>idS; if(idS.empty()){ std::cout<<"taskbar.close <id>"<<std::endl; continue; } ipc::Message m; m.type=(uint32_t)gui::MsgType::MT_Close; m.data.assign(idS.begin(), idS.end()); ipc::Bus::publish("gui.input", std::move(m), false); std::cout<<"Close requested"<<std::endl; }
        else {
            std::cout << "Unknown command (help for list)" << std::endl;
        }
    }
    Scheduler::shutdown();
    Logger::write(LogLevel::Info, "guideXOSServer server exiting.");
    return 0;
}
