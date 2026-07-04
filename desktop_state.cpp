#include "desktop_state.h"
#include "fs.h"
#include <limits>
#include <sstream>

namespace gxos { namespace gui {
    static std::string esc(const std::string& s){ std::string o; for(char c: s){ if(c=='|'||c=='\n' || c=='\r') o.push_back('_'); else o.push_back(c);} return o; }
    bool DesktopState::Save(const std::string& path, const std::vector<SavedWindow>& wins, std::string& err){
        std::ostringstream oss;
        oss << "GXOSSTATE\n";
        for(auto& w: wins){ oss<<w.id<<"|"<<esc(w.title)<<"|"<<w.x<<"|"<<w.y<<"|"<<w.w<<"|"<<w.h<<"|"<<(w.minimized?1:0)<<"|"<<(w.maximized?1:0)<<"|"<<w.z<<"|"<<(w.focused?1:0)<<"|"<<w.snap<<"|"<<w.restoreX<<"|"<<w.restoreY<<"|"<<w.restoreW<<"|"<<w.restoreH<<"\n"; }
        const std::string serialized = oss.str();
        std::vector<uint8_t> bytes(serialized.begin(), serialized.end());
        if (!FS::writeAll(path, bytes)) { err="write fail"; return false; }
        return true;
    }
    bool DesktopState::Load(const std::string& path, std::vector<SavedWindow>& wins, std::string& err){
        std::vector<uint8_t> bytes;
        FSResult readResult = FS::readAll(path, bytes, std::numeric_limits<uint64_t>::max());
        if (!readResult.success) { err = readResult.message.empty() ? "open fail" : readResult.message; return false; }
        std::string text(bytes.begin(), bytes.end());
        std::istringstream f(text);
        std::string line; if(!std::getline(f,line)){ err="empty"; return false; } if(line!="GXOSSTATE"){ err="bad magic"; return false; }
        while(std::getline(f,line)){
            if(line.empty()) continue; std::istringstream iss(line); std::string idS,title,xs,ys,ws,hs,ms,maxS,zS,fS,snapS,restoreXs,restoreYs,restoreWs,restoreHs; std::getline(iss,idS,'|'); std::getline(iss,title,'|'); std::getline(iss,xs,'|'); std::getline(iss,ys,'|'); std::getline(iss,ws,'|'); std::getline(iss,hs,'|'); std::getline(iss,ms,'|'); std::getline(iss,maxS,'|'); std::getline(iss,zS,'|'); std::getline(iss,fS,'|'); std::getline(iss,snapS,'|'); std::getline(iss,restoreXs,'|'); std::getline(iss,restoreYs,'|'); std::getline(iss,restoreWs,'|'); std::getline(iss,restoreHs,'|'); SavedWindow sw{}; try{ sw.id=std::stoull(idS); sw.title=title; sw.x=std::stoi(xs); sw.y=std::stoi(ys); sw.w=std::stoi(ws); sw.h=std::stoi(hs); sw.minimized=(ms=="1"); sw.maximized=(maxS=="1"); sw.z=std::stoi(zS); sw.focused=(fS=="1"); sw.snap=std::stoi(snapS); sw.restoreX=restoreXs.empty()?sw.x:std::stoi(restoreXs); sw.restoreY=restoreYs.empty()?sw.y:std::stoi(restoreYs); sw.restoreW=restoreWs.empty()?sw.w:std::stoi(restoreWs); sw.restoreH=restoreHs.empty()?sw.h:std::stoi(restoreHs); }catch(...){ continue; } wins.push_back(sw);
        }
        return true;
    }
} }
