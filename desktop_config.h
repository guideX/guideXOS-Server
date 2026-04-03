#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace gxos { namespace gui {
    struct DesktopWindowRec { uint64_t id; std::string title; int x; int y; int w; int h; bool minimized{false}; bool maximized{false}; int z{0}; bool focused{false}; int snap{0}; };
    struct DesktopIconPos { std::string name; int x; int y; };
    struct DesktopConfigData {
        std::string wallpaperPath; // may be empty
        std::vector<std::string> pinned;
        std::vector<std::string> recent;
        std::vector<DesktopWindowRec> windows;
        std::vector<DesktopIconPos> iconPositions;
    };
    class DesktopConfig {
    public:
        // Load JSON from path. Extremely small permissive parser; expects correct schema.
        static inline bool Load(const std::string& path, DesktopConfigData& out, std::string& err){
            err.clear();
            auto readAll=[&](const std::string& p){ std::ifstream f(p, std::ios::binary); if(!f) return std::string(); std::ostringstream ss; ss<<f.rdbuf(); return ss.str(); };
            auto txt = readAll(path); if(txt.empty()){ err = "open fail"; return false; }
            auto trim=[](const std::string& s){ size_t a=0; while(a<s.size() && (s[a]==' '||s[a]=='\n'||s[a]=='\r'||s[a]=='\t')) ++a; size_t b=s.size(); while(b>a && (s[b-1]==' '||s[b-1]=='\n'||s[b-1]=='\r'||s[b-1]=='\t')) --b; return s.substr(a,b-a); };
            auto skipWS=[&](const std::string& s, size_t& i){ while(i<s.size() && (s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t')) ++i; };
            auto parseJSONString=[&](const std::string& src, size_t& i, std::string& outS){ outS.clear(); if(i>=src.size()||src[i]!='"') return false; ++i; while(i<src.size()){ char c=src[i++]; if(c=='\\'){ if(i>=src.size()) break; char e=src[i++]; if(e=='"'||e=='\\'||e=='/') outS.push_back(e); else if(e=='b') outS.push_back('\b'); else if(e=='f') outS.push_back('\f'); else if(e=='n') outS.push_back('\n'); else if(e=='r') outS.push_back('\r'); else if(e=='t') outS.push_back('\t'); else outS.push_back(e); }
                else if(c=='"'){ return true; } else { outS.push_back(c);} } return false; };
            auto parseJSONInt=[&](const std::string& src, size_t& i, long long& outV){ outV=0; bool neg=false; if(i<src.size() && (src[i]=='-'||src[i]=='+')){ neg=src[i]=='-'; ++i; } if(i>=src.size()|| !(src[i]>='0'&&src[i]<='9')) return false; long long v=0; while(i<src.size() && src[i]>='0' && src[i]<='9'){ v = v*10 + (src[i]-'0'); ++i; } outV = neg? -v : v; return true; };
            auto parseJSONBool=[&](const std::string& src, size_t& i, bool& outV){ if(src.compare(i,4,"true")==0){ outV=true; i+=4; return true; } if(src.compare(i,5,"false")==0){ outV=false; i+=5; return true; } return false; };
            auto extractSection=[&](const std::string& s, const std::string& key, std::string& content){ content.clear(); auto kpos = s.find('"'+key+'"'); if(kpos==std::string::npos) return false; auto colon = s.find(':', kpos); if(colon==std::string::npos) return false; size_t i=colon+1; skipWS(s,i); if(i>=s.size()) return false; if(s[i]=='"'){ size_t j=i; std::string tmp; if(!parseJSONString(s,j,tmp)) return false; content='"'+tmp+'"'; return true; } if(s[i]=='['){ int depth=0; size_t j=i; while(j<s.size()){ if(s[j]=='[') depth++; else if(s[j]==']'){ depth--; if(depth==0){ ++j; break; } } ++j; } if(depth!=0) return false; content = s.substr(i, j-i); return true; } if(s[i]=='{'){ int depth=0; size_t j=i; while(j<s.size()){ if(s[j]=='{') depth++; else if(s[j]=='}'){ depth--; if(depth==0){ ++j; break; } } ++j; } if(depth!=0) return false; content = s.substr(i, j-i); return true; } size_t j=i; while(j<s.size() && s[j]!=',' && s[j]!='}' && s[j]!=']' && s[j]!='\n' && s[j]!='\r') ++j; content = s.substr(i, j-i); return true; };
            auto parseStringArray=[&](const std::string& src, std::vector<std::string>& outArr){ outArr.clear(); size_t i=0; skipWS(src,i); if(i>=src.size()||src[i]!='[') return false; ++i; skipWS(src,i); if(i<src.size() && src[i]==']'){ ++i; return true; } while(i<src.size()){ skipWS(src,i); std::string val; if(!parseJSONString(src,i,val)) return false; outArr.push_back(val); skipWS(src,i); if(i<src.size() && src[i]==','){ ++i; continue; } if(i<src.size() && src[i]==']'){ ++i; return true; } return false; } return false; };
            auto parseWindowsArray=[&](const std::string& src, std::vector<DesktopWindowRec>& outWins){ outWins.clear(); size_t i=0; skipWS(src,i); if(i>=src.size() || src[i]!='[') return false; ++i; skipWS(src,i); if(i<src.size() && src[i]==']'){ ++i; return true; } while(i<src.size()){ skipWS(src,i); if(i>=src.size()||src[i]!='{') return false; int depth=0; size_t start=i; size_t j=i; while(j<src.size()){ if(src[j]=='{') depth++; else if(src[j]=='}'){ depth--; if(depth==0){ ++j; break; } } ++j; } if(depth!=0) return false; std::string obj = src.substr(start, j-start); DesktopWindowRec rec{}; auto findStr=[&](const char* key, std::string& outStr){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); if(ii>=obj.size()||obj[ii]!='"') return false; return parseJSONString(obj, ii, outStr); }; auto findInt=[&](const char* key, long long& outVal){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); return parseJSONInt(obj, ii, outVal); }; auto findBool=[&](const char* key, bool& outVal){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); return parseJSONBool(obj, ii, outVal); }; std::string title; long long id=0,x=0,y=0,w=0,h=0,z=0,snap=0; bool minimized=false,maximized=false,focused=false; findInt("id", id); findStr("title", title); findInt("x", x); findInt("y", y); findInt("w", w); findInt("h", h); findBool("minimized", minimized); findBool("maximized", maximized); findInt("z", z); findBool("focused", focused); findInt("snap", snap); DesktopWindowRec r; r.id=(uint64_t)id; r.title=title; r.x=(int)x; r.y=(int)y; r.w=(int)w; r.h=(int)h; r.minimized=minimized; r.maximized=maximized; r.z=(int)z; r.focused=focused; r.snap=(int)snap; outWins.push_back(r); i=j; skipWS(src,i); if(i<src.size() && src[i]==','){ ++i; continue; } if(i<src.size() && src[i]==']'){ ++i; return true; } return false; } return false; };
            auto parseIconPosArray=[&](const std::string& src, std::vector<DesktopIconPos>& outPos){ outPos.clear(); size_t i=0; skipWS(src,i); if(i>=src.size() || src[i]!='[') return false; ++i; skipWS(src,i); if(i<src.size() && src[i]==']'){ ++i; return true; } while(i<src.size()){ skipWS(src,i); if(i>=src.size()||src[i]!='{') return false; int depth=0; size_t start=i; size_t j=i; while(j<src.size()){ if(src[j]=='{') depth++; else if(src[j]=='}'){ depth--; if(depth==0){ ++j; break; } } ++j; } if(depth!=0) return false; std::string obj = src.substr(start, j-start); auto findStr2=[&](const char* key, std::string& outStr){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); if(ii>=obj.size()||obj[ii]!='"') return false; return parseJSONString(obj, ii, outStr); }; auto findInt2=[&](const char* key, long long& outVal){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); return parseJSONInt(obj, ii, outVal); }; DesktopIconPos ip; long long px=0,py=0; findStr2("name", ip.name); findInt2("x", px); findInt2("y", py); ip.x=(int)px; ip.y=(int)py; outPos.push_back(ip); i=j; skipWS(src,i); if(i<src.size() && src[i]==','){ ++i; continue; } if(i<src.size() && src[i]==']'){ ++i; return true; } return false; } return false; };
            std::string section; if(extractSection(txt, "wallpaper", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.wallpaperPath); } }
            if(extractSection(txt, "pinned", section)) parseStringArray(section, out.pinned);
            if(extractSection(txt, "recent", section)) parseStringArray(section, out.recent);
            if(extractSection(txt, "windows", section)) parseWindowsArray(section, out.windows);
            if(extractSection(txt, "iconPositions", section)) parseIconPosArray(section, out.iconPositions);
            return true;
        }
        static inline bool Save(const std::string& path, const DesktopConfigData& data, std::string& err){
            std::ofstream f(path, std::ios::binary|std::ios::trunc); if(!f){ err="open fail"; return false; }
            auto jsonEscape=[&](const std::string& s){ std::ostringstream o; o<<'"'; for(char c: s){ switch(c){ case '"': o<<"\\\""; break; case '\\': o<<"\\\\"; break; case '\n': o<<"\\n"; break; case '\r': o<<"\\r"; break; case '\t': o<<"\\t"; break; default: o<<c; break; } } o<<'"'; return o.str(); };
            f << "{\n";
            f << "  \"wallpaper\": " << jsonEscape(data.wallpaperPath) << ",\n";
            f << "  \"pinned\": ["; for(size_t i=0;i<data.pinned.size();++i){ if(i) f<<","; f<<jsonEscape(data.pinned[i]);} f << "],\n";
            f << "  \"recent\": ["; for(size_t i=0;i<data.recent.size();++i){ if(i) f<<","; f<<jsonEscape(data.recent[i]);} f << "],\n";
            f << "  \"windows\": [\n";
            for(size_t i=0;i<data.windows.size();++i){ const auto& w=data.windows[i]; f << "    {";
            f << "\"id\": " << w.id << ", "; f << "\"title\": " << jsonEscape(w.title) << ", "; f << "\"x\": "<<w.x<<", \"y\": "<<w.y<<", \"w\": "<<w.w<<", \"h\": "<<w.h<<", ";
            f << "\"minimized\": "<<(w.minimized?"true":"false")<<", \"maximized\": "<<(w.maximized?"true":"false")<<", ";
            f << "\"z\": "<<w.z<<", \"focused\": "<<(w.focused?"true":"false")<<", \"snap\": "<<w.snap;
            f << "}"; if(i+1<data.windows.size()) f << ","; f << "\n"; }
            f << "  ],\n";
            f << "  \"iconPositions\": [\n";
            for(size_t i=0;i<data.iconPositions.size();++i){ const auto& ip=data.iconPositions[i]; f << "    {";
            f << "\"name\": " << jsonEscape(ip.name) << ", \"x\": " << ip.x << ", \"y\": " << ip.y;
            f << "}"; if(i+1<data.iconPositions.size()) f << ","; f << "\n"; }
            f << "  ]\n";
            f << "}\n";
            return true;
        }
    };
} }
