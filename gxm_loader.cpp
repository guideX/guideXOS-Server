#include "gxm_loader.h"
#include "fs.h"
#include "ipc_bus.h"
#include "gui_protocol.h"
#include <vector>
#include <sstream>

namespace gxos { namespace gui {
    static std::string trim(const std::string& s){ size_t a = s.find_first_not_of(" \t\r\n"); if(a==std::string::npos) return {}; size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b-a+1); }
    static void busSend(MsgType t, const std::string& payload){ ipc::Message m; m.type=(uint32_t)t; m.data.assign(payload.begin(), payload.end()); ipc::Bus::publish("gui.input", std::move(m), false); }

    static bool tryExecuteBinaryGxm(const std::vector<uint8_t>& img, std::string& error){
        if(img.size()<16) { error = "image too small"; return false; }
        bool sigGXM = img[0]=='G' && img[1]=='X' && img[2]=='M' && img[3]==0;
        bool sigMUE = img[0]=='M' && img[1]=='U' && img[2]=='E' && img[3]==0;
        if(!sigGXM && !sigMUE){ error="bad sig"; return false; }
        if(img.size()>=20 && img[16]=='G' && img[17]=='U' && img[18]=='I' && img[19]==0){
            size_t pos = 20; std::string line;
            for(; pos<img.size(); ++pos){ uint8_t c = img[pos]; if(c==0 || c=='\n'){ if(!line.empty()){ // interpret
                        std::string l = trim(line);
                        if(!l.empty()){
                            // simple DSL: WIN <title>|<w>|<h> | TEXT <id>|<text> | RECT <id>|x|y|w|h|r|g|b | BTN <id>|x|y|w|h|text
                            if(l.rfind("WIN ",0)==0){ auto rest=l.substr(4); busSend(MsgType::MT_Create, rest); }
                            else if(l.rfind("TEXT ",0)==0){ busSend(MsgType::MT_DrawText, l.substr(5)); }
                            else if(l.rfind("RECT ",0)==0){ busSend(MsgType::MT_DrawRect, l.substr(5)); }
                            else if(l.rfind("BTN ",0)==0){ busSend(MsgType::MT_WidgetAdd, l.substr(4)); }
                        }
                    }
                    line.clear(); if(c==0) break; }
                else line.push_back((char)c);
            }
            return true;
        }
        error = "no GUI section"; return false;
    }

    static bool executePlainText(const std::string& text, std::string& error){
        std::istringstream iss(text); std::string line; while(std::getline(iss,line)){ line = trim(line); if(line.empty()) continue; if(line.rfind("WIN ",0)==0){ busSend(MsgType::MT_Create, line.substr(4)); } else if(line.rfind("TEXT ",0)==0){ busSend(MsgType::MT_DrawText, line.substr(5)); } else if(line.rfind("RECT ",0)==0){ busSend(MsgType::MT_DrawRect, line.substr(5)); } else if(line.rfind("BTN ",0)==0){ busSend(MsgType::MT_WidgetAdd, line.substr(4)); } }
        return true;
    }

    bool GxmLoader::ExecuteFile(const std::string& path, std::string& error){
        std::vector<uint8_t> data; if(!FS::readAll(path, data)) { error = "read fail"; return false; }
        std::string dummy; if(tryExecuteBinaryGxm(data,error)) return true; // if GUI embedded
        // else try treat as utf-8 text
        std::string asText(data.begin(), data.end()); return executePlainText(asText, error);
    }

    bool GxmLoader::ExecuteText(const std::string& text, std::string& error){ return executePlainText(text,error); }
} }
