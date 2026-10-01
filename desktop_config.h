#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <limits>
#include <sstream>
#include <cctype>
#include <utility>
#include "app_model_limits.h"
#include "fs.h"
#include "clock_time_settings.h"
#include "desktop_theme.h"
#include "display_model.h"

namespace gxos { namespace gui {
    constexpr uint64_t kDesktopConfigMaxBytes = 1024 * 1024;
    constexpr size_t kDesktopConfigMaxPinnedEntries = 128;
    constexpr size_t kDesktopConfigMaxRecentEntries = 10;
    constexpr size_t kDesktopConfigMaxLaunchIdBytes = apps::kAppModelMaxAppIdBytes;
    constexpr size_t kDesktopConfigMaxPinnedLabelBytes = apps::kAppModelMaxLaunchTargetBytes;
    constexpr size_t kDesktopConfigMaxRecentLabelBytes = apps::kAppModelMaxDisplayNameBytes;
    constexpr size_t kDesktopConfigMaxShortcutEntries = 128;
    constexpr size_t kDesktopConfigMaxPathBytes = apps::kAppModelMaxEntryPathBytes;

    inline bool parseDesktopEnhancedFocusJsonBoolean(const std::string& value,
                                                     bool fallback = false) {
        size_t begin = 0;
        while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\n' ||
               value[begin] == '\r' || value[begin] == '\t')) ++begin;
        size_t end = value.size();
        while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\n' ||
               value[end - 1] == '\r' || value[end - 1] == '\t')) --end;
        const std::string token = value.substr(begin, end - begin);
        if (token == "true") return true;
        if (token == "false") return false;
        return fallback;
    }

    struct DesktopWindowRec {
        uint64_t id;
        std::string title;
        int x;
        int y;
        int w;
        int h;
        bool minimized{false};
        bool maximized{false};
        int z{0};
        bool focused{false};
        int snap{0};
        int restoreX{0};
        int restoreY{0};
        int restoreW{0};
        int restoreH{0};
    };
    struct DesktopIconPos { std::string name; int x; int y; };
    struct DesktopShortcutRec {
        std::string shortcutType;
        std::string targetAppId;
        std::string targetPath;
        std::string label;
        std::string iconName;
    };
    struct DesktopConfigData {
        std::string wallpaperPath; // may be empty
        std::string wallpaperId; // stable built-in wallpaper id, e.g. desktop.wallpaper.id
        std::string desktopThemeId; // stable theme id, e.g. classic or scifi
        std::string backgroundScaleMode; // fill, fit, stretch, center, or tile
        std::string taskbarPosition; // bottom, top, left, or right
        std::string displayMode; // mirror or extend
        std::string displayPrimaryDisplayId; // stable monitor id if known
        std::string displayArrangement; // serialized monitor layout, if known
        std::string displayResolution; // virtual desktop resolution if known
        std::string timeZoneId; // pacific, mountain, central, eastern, or utc
        bool use24HourTime{false};
        std::vector<std::string> pinned;
        // Canonical App Model identities parallel to pinned. An empty identity
        // is reserved for fixed shell actions and path targets.
        std::vector<std::string> pinnedAppIds;
        std::vector<std::string> recent;
        // Canonical App Model identities parallel to recent.
        std::vector<std::string> recentAppIds;
        std::vector<DesktopShortcutRec> desktopShortcuts;
        std::vector<DesktopWindowRec> windows;
        std::vector<DesktopIconPos> iconPositions;
        bool showDesktopTrash{true};
        bool showDesktopThisSystem{true};
        bool showDesktopFileManager{true};
        bool showDesktopSystemSettings{false};
        bool smallLiveDesktopFolderIcons{true};
        bool autoArrangeDesktopIcons{false};
        bool enhancedFocusIndicator{false};
    };
    class DesktopConfig {
    public:
        // Load JSON from path. Extremely small permissive parser; expects correct schema.
        static inline bool Load(const std::string& path, DesktopConfigData& out, std::string& err){
            err.clear();
            out = DesktopConfigData{};
            std::vector<uint8_t> bytes;
            FSResult readResult = FS::readAll(path, bytes, kDesktopConfigMaxBytes);
            if (!readResult.success) { err = readResult.message.empty() ? "open fail" : readResult.message; return false; }
            std::string txt(bytes.begin(), bytes.end());
            if(txt.empty()){ err = "open fail"; return false; }
            auto trim=[](const std::string& s){ size_t a=0; while(a<s.size() && (s[a]==' '||s[a]=='\n'||s[a]=='\r'||s[a]=='\t')) ++a; size_t b=s.size(); while(b>a && (s[b-1]==' '||s[b-1]=='\n'||s[b-1]=='\r'||s[b-1]=='\t')) --b; return s.substr(a,b-a); };
            auto skipWS=[&](const std::string& s, size_t& i){ while(i<s.size() && (s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t')) ++i; };
            auto parseJSONString=[&](const std::string& src, size_t& i, std::string& outS){ outS.clear(); if(i>=src.size()||src[i]!='"') return false; ++i; while(i<src.size()){ char c=src[i++]; if(c=='\\'){ if(i>=src.size()) break; char e=src[i++]; if(e=='"'||e=='\\'||e=='/') outS.push_back(e); else if(e=='b') outS.push_back('\b'); else if(e=='f') outS.push_back('\f'); else if(e=='n') outS.push_back('\n'); else if(e=='r') outS.push_back('\r'); else if(e=='t') outS.push_back('\t'); else outS.push_back(e); }
                else if(c=='"'){ return true; } else { outS.push_back(c);} } return false; };
            auto parseJSONInt=[&](const std::string& src, size_t& i, long long& outV){ outV=0; bool neg=false; if(i<src.size() && (src[i]=='-'||src[i]=='+')){ neg=src[i]=='-'; ++i; } if(i>=src.size()|| !(src[i]>='0'&&src[i]<='9')) return false; long long v=0; while(i<src.size() && src[i]>='0' && src[i]<='9'){ v = v*10 + (src[i]-'0'); ++i; } outV = neg? -v : v; return true; };
            auto parseJSONBool=[&](const std::string& src, size_t& i, bool& outV){ if(src.compare(i,4,"true")==0){ outV=true; i+=4; return true; } if(src.compare(i,5,"false")==0){ outV=false; i+=5; return true; } return false; };
            auto extractSection=[&](const std::string& s, const std::string& key, std::string& content){ content.clear(); auto kpos = s.find('"'+key+'"'); if(kpos==std::string::npos) return false; auto colon = s.find(':', kpos); if(colon==std::string::npos) return false; size_t i=colon+1; skipWS(s,i); if(i>=s.size()) return false; if(s[i]=='"'){ size_t j=i; std::string tmp; if(!parseJSONString(s,j,tmp)) return false; content='"'+tmp+'"'; return true; } if(s[i]=='['){ int depth=0; size_t j=i; while(j<s.size()){ if(s[j]=='[') depth++; else if(s[j]==']'){ depth--; if(depth==0){ ++j; break; } } ++j; } if(depth!=0) return false; content = s.substr(i, j-i); return true; } if(s[i]=='{'){ int depth=0; size_t j=i; while(j<s.size()){ if(s[j]=='{') depth++; else if(s[j]=='}'){ depth--; if(depth==0){ ++j; break; } } ++j; } if(depth!=0) return false; content = s.substr(i, j-i); return true; } size_t j=i; while(j<s.size() && s[j]!=',' && s[j]!='}' && s[j]!=']' && s[j]!='\n' && s[j]!='\r') ++j; content = s.substr(i, j-i); return true; };
            auto parseStringArray=[&](const std::string& src, std::vector<std::string>& outArr, size_t maxCount, size_t maxEntryBytes){ outArr.clear(); size_t i=0; skipWS(src,i); if(i>=src.size()||src[i]!='[') return false; ++i; skipWS(src,i); if(i<src.size() && src[i]==']'){ ++i; return true; } while(i<src.size()){ skipWS(src,i); if(outArr.size() >= maxCount) return false; std::string val; if(!parseJSONString(src,i,val) || val.size() > maxEntryBytes || val.find('\0') != std::string::npos) return false; outArr.push_back(std::move(val)); skipWS(src,i); if(i<src.size() && src[i]==','){ ++i; continue; } if(i<src.size() && src[i]==']'){ ++i; return true; } return false; } return false; };
            auto parseWindowsArray=[&](const std::string& src, std::vector<DesktopWindowRec>& outWins){ outWins.clear(); size_t i=0; skipWS(src,i); if(i>=src.size() || src[i]!='[') return false; ++i; skipWS(src,i); if(i<src.size() && src[i]==']'){ ++i; return true; } while(i<src.size()){ skipWS(src,i); if(i>=src.size()||src[i]!='{') return false; int depth=0; size_t start=i; size_t j=i; while(j<src.size()){ if(src[j]=='{') depth++; else if(src[j]=='}'){ depth--; if(depth==0){ ++j; break; } } ++j; } if(depth!=0) return false; std::string obj = src.substr(start, j-start); DesktopWindowRec rec{}; auto findStr=[&](const char* key, std::string& outStr){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); if(ii>=obj.size()||obj[ii]!='"') return false; return parseJSONString(obj, ii, outStr); }; auto findInt=[&](const char* key, long long& outVal){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); return parseJSONInt(obj, ii, outVal); }; auto findBool=[&](const char* key, bool& outVal){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); return parseJSONBool(obj, ii, outVal); }; std::string title; long long id=0,x=0,y=0,w=0,h=0,z=0,snap=0,restoreX=0,restoreY=0,restoreW=0,restoreH=0; bool minimized=false,maximized=false,focused=false; findInt("id", id); findStr("title", title); findInt("x", x); findInt("y", y); findInt("w", w); findInt("h", h); findBool("minimized", minimized); findBool("maximized", maximized); findInt("z", z); findBool("focused", focused); findInt("snap", snap); findInt("restoreX", restoreX); findInt("restoreY", restoreY); findInt("restoreW", restoreW); findInt("restoreH", restoreH); DesktopWindowRec r; r.id=(uint64_t)id; r.title=title; r.x=(int)x; r.y=(int)y; r.w=(int)w; r.h=(int)h; r.minimized=minimized; r.maximized=maximized; r.z=(int)z; r.focused=focused; r.snap=(int)snap; r.restoreX=(int)restoreX; r.restoreY=(int)restoreY; r.restoreW=(int)restoreW; r.restoreH=(int)restoreH; if(r.restoreW <= 0 || r.restoreH <= 0){ r.restoreX = r.x; r.restoreY = r.y; r.restoreW = r.w; r.restoreH = r.h; } outWins.push_back(r); i=j; skipWS(src,i); if(i<src.size() && src[i]==','){ ++i; continue; } if(i<src.size() && src[i]==']'){ ++i; return true; } return false; } return false; };
            auto parseIconPosArray=[&](const std::string& src, std::vector<DesktopIconPos>& outPos){ outPos.clear(); size_t i=0; skipWS(src,i); if(i>=src.size() || src[i]!='[') return false; ++i; skipWS(src,i); if(i<src.size() && src[i]==']'){ ++i; return true; } while(i<src.size()){ skipWS(src,i); if(i>=src.size()||src[i]!='{') return false; int depth=0; size_t start=i; size_t j=i; while(j<src.size()){ if(src[j]=='{') depth++; else if(src[j]=='}'){ depth--; if(depth==0){ ++j; break; } } ++j; } if(depth!=0) return false; std::string obj = src.substr(start, j-start); auto findStr2=[&](const char* key, std::string& outStr){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); if(ii>=obj.size()||obj[ii]!='"') return false; return parseJSONString(obj, ii, outStr); }; auto findInt2=[&](const char* key, long long& outVal){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); return parseJSONInt(obj, ii, outVal); }; DesktopIconPos ip; long long px=0,py=0; findStr2("name", ip.name); findInt2("x", px); findInt2("y", py); ip.x=(int)px; ip.y=(int)py; outPos.push_back(ip); i=j; skipWS(src,i); if(i<src.size() && src[i]==','){ ++i; continue; } if(i<src.size() && src[i]==']'){ ++i; return true; } return false; } return false; };
            auto parseShortcutArray=[&](const std::string& src, std::vector<DesktopShortcutRec>& outShortcuts){ outShortcuts.clear(); size_t i=0; skipWS(src,i); if(i>=src.size() || src[i]!='[') return false; ++i; skipWS(src,i); if(i<src.size() && src[i]==']'){ ++i; return true; } while(i<src.size()){ skipWS(src,i); if(i>=src.size()||src[i]!='{') return false; int depth=0; size_t start=i; size_t j=i; while(j<src.size()){ if(src[j]=='{') depth++; else if(src[j]=='}'){ depth--; if(depth==0){ ++j; break; } } ++j; } if(depth!=0) return false; std::string obj = src.substr(start, j-start); auto findStr=[&](const char* key, std::string& outStr){ auto p=obj.find(std::string("\"")+key+"\""); if(p==std::string::npos) return false; auto c=obj.find(':',p); if(c==std::string::npos) return false; size_t ii=c+1; skipWS(obj,ii); if(ii>=obj.size()||obj[ii]!='"') return false; return parseJSONString(obj, ii, outStr); }; DesktopShortcutRec rec; findStr("shortcutType", rec.shortcutType); findStr("targetAppId", rec.targetAppId); findStr("targetPath", rec.targetPath); findStr("label", rec.label); findStr("iconName", rec.iconName); if(rec.shortcutType.empty()) rec.shortcutType = rec.targetPath.empty() ? "App" : "File"; if((rec.shortcutType=="App" && !rec.targetAppId.empty()) || ((rec.shortcutType=="File" || rec.shortcutType=="Folder") && !rec.targetPath.empty())) outShortcuts.push_back(rec); i=j; skipWS(src,i); if(i<src.size() && src[i]==','){ ++i; continue; } if(i<src.size() && src[i]==']'){ ++i; return true; } return false; } return false; };
            std::string section; if(extractSection(txt, "wallpaper", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.wallpaperPath); } }
            if(extractSection(txt, "desktop.wallpaper.id", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.wallpaperId); } }
            if(extractSection(txt, "desktop.theme.id", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.desktopThemeId); } }
            if(out.desktopThemeId.empty() && extractSection(txt, "desktop.theme", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.desktopThemeId); } }
            if(extractSection(txt, "desktop.background.scale", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.backgroundScaleMode); } }
            if(extractSection(txt, "desktop.taskbar.position", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.taskbarPosition); } }
            if(extractSection(txt, "desktop.display.mode", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.displayMode); } }
            if(extractSection(txt, "desktop.display.primaryId", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.displayPrimaryDisplayId); } }
            if(extractSection(txt, "desktop.display.arrangement", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.displayArrangement); } }
            if(extractSection(txt, "desktop.display.resolution", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.displayResolution); } }
            if(extractSection(txt, "desktop.clock.timeZoneId", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.timeZoneId); } }
            if(out.timeZoneId.empty() && extractSection(txt, "desktop.clock.timeZone", section)){ if(!section.empty() && section[0]=='"'){ size_t i=0; parseJSONString(section, i, out.timeZoneId); } }
            if(extractSection(txt, "desktop.clock.use24HourTime", section)){ size_t i=0; skipWS(section,i); parseJSONBool(section, i, out.use24HourTime); }
            if(!out.use24HourTime && extractSection(txt, "desktop.clock.use24Hour", section)){ size_t i=0; skipWS(section,i); parseJSONBool(section, i, out.use24HourTime); }
            if(extractSection(txt, "pinned", section) && !parseStringArray(section, out.pinned, kDesktopConfigMaxPinnedEntries, kDesktopConfigMaxPinnedLabelBytes)) { err = "invalid or oversized pinned launch list"; return false; }
            if(extractSection(txt, "pinnedAppIds", section) && !parseStringArray(section, out.pinnedAppIds, kDesktopConfigMaxPinnedEntries, kDesktopConfigMaxLaunchIdBytes)) { err = "invalid or oversized pinned App Model identity list"; return false; }
            if(extractSection(txt, "recent", section) && !parseStringArray(section, out.recent, kDesktopConfigMaxRecentEntries, kDesktopConfigMaxRecentLabelBytes)) { err = "invalid or oversized recent launch list"; return false; }
            if(extractSection(txt, "recentAppIds", section) && !parseStringArray(section, out.recentAppIds, kDesktopConfigMaxRecentEntries, kDesktopConfigMaxLaunchIdBytes)) { err = "invalid or oversized recent App Model identity list"; return false; }
            if (extractSection(txt, "pinnedAppIds", section) && out.pinnedAppIds.size() != out.pinned.size()) {
                out.pinnedAppIds.assign(out.pinned.size(), "__invalid_pinned_identity_array__");
            }
            if (extractSection(txt, "recentAppIds", section) && out.recentAppIds.size() != out.recent.size()) {
                out.recentAppIds.assign(out.recent.size(), "__invalid_recent_identity_array__");
            }
            if(extractSection(txt, "desktopShortcuts", section) && !parseShortcutArray(section, out.desktopShortcuts)) { err = "invalid or oversized desktop shortcut list"; return false; }
            if (out.desktopShortcuts.size() > kDesktopConfigMaxShortcutEntries) { err = "desktop shortcut list exceeds its entry limit"; return false; }
            for (const DesktopShortcutRec& shortcut : out.desktopShortcuts) {
                const bool appTarget = shortcut.shortcutType == "App" && !shortcut.targetAppId.empty() && shortcut.targetPath.empty();
                const bool pathTarget = (shortcut.shortcutType == "File" || shortcut.shortcutType == "Folder") && !shortcut.targetPath.empty() && shortcut.targetAppId.empty();
                const auto invalidText = [](const std::string& value, size_t maxBytes) {
                    if (value.size() > maxBytes || value.find('\0') != std::string::npos) return true;
                    for (unsigned char c : value) if (c < 0x20u || c == 0x7fu) return true;
                    return false;
                };
                if ((!appTarget && !pathTarget) ||
                    invalidText(shortcut.targetAppId, apps::kAppModelMaxAppIdBytes) ||
                    invalidText(shortcut.targetPath, kDesktopConfigMaxPathBytes) ||
                    invalidText(shortcut.label, apps::kAppModelMaxDisplayNameBytes) ||
                    invalidText(shortcut.iconName, apps::kAppModelMaxDisplayNameBytes)) {
                    err = "desktop shortcut target is invalid or exceeds its byte limit";
                    return false;
                }
            }
            if(extractSection(txt, "windows", section)) parseWindowsArray(section, out.windows);
            if(extractSection(txt, "iconPositions", section)) parseIconPosArray(section, out.iconPositions);
            if(extractSection(txt, "showDesktopTrash", section)){ size_t i=0; skipWS(section,i); parseJSONBool(section, i, out.showDesktopTrash); }
            if(extractSection(txt, "showDesktopThisSystem", section)){ size_t i=0; skipWS(section,i); parseJSONBool(section, i, out.showDesktopThisSystem); }
            if(extractSection(txt, "showDesktopFileManager", section)){ size_t i=0; skipWS(section,i); parseJSONBool(section, i, out.showDesktopFileManager); }
            if(extractSection(txt, "showDesktopSystemSettings", section)){ size_t i=0; skipWS(section,i); parseJSONBool(section, i, out.showDesktopSystemSettings); }
            if(extractSection(txt, "smallLiveDesktopFolderIcons", section)){ size_t i=0; skipWS(section,i); parseJSONBool(section, i, out.smallLiveDesktopFolderIcons); }
            if(extractSection(txt, "autoArrangeDesktopIcons", section)){ size_t i=0; skipWS(section,i); parseJSONBool(section, i, out.autoArrangeDesktopIcons); }
            if(extractSection(txt, "desktop.accessibility.enhancedFocusIndicator", section)){
                out.enhancedFocusIndicator = parseDesktopEnhancedFocusJsonBoolean(
                    section, out.enhancedFocusIndicator);
            }
            {
                DesktopThemeId themeId = DesktopThemeId::Classic;
                TryParseDesktopThemeId(out.desktopThemeId.c_str(), &themeId);
                out.desktopThemeId = DesktopThemeIdToString(themeId);
            }
            out.displayMode = normalizeDisplayModeName(out.displayMode);
            if (out.displayPrimaryDisplayId.empty()) out.displayPrimaryDisplayId = "display-1";
            out.timeZoneId = gxos::clocktime::NormalizeTimeZoneId(out.timeZoneId);
            return true;
        }
        static inline bool Save(const std::string& path, const DesktopConfigData& data, std::string& err){
            if (data.pinned.size() > kDesktopConfigMaxPinnedEntries ||
                data.recent.size() > kDesktopConfigMaxRecentEntries ||
                data.desktopShortcuts.size() > kDesktopConfigMaxShortcutEntries ||
                data.pinnedAppIds.size() > kDesktopConfigMaxPinnedEntries ||
                data.recentAppIds.size() > kDesktopConfigMaxRecentEntries ||
                (!data.pinnedAppIds.empty() && data.pinnedAppIds.size() != data.pinned.size()) ||
                (!data.recentAppIds.empty() && data.recentAppIds.size() != data.recent.size())) {
                err = "desktop config launch storage exceeds its entry capacity";
                return false;
            }
            const auto hasControl = [](const std::string& value) {
                for (unsigned char c : value) if (c < 0x20u || c == 0x7fu) return true;
                return false;
            };
            for (const std::string& value : data.pinned) if (value.size() > kDesktopConfigMaxPinnedLabelBytes || value.find('\0') != std::string::npos || hasControl(value)) { err = "pinned launch label exceeds its byte or character limit"; return false; }
            for (const std::string& value : data.recent) if (value.size() > kDesktopConfigMaxRecentLabelBytes || value.find('\0') != std::string::npos || hasControl(value)) { err = "recent launch label exceeds its byte or character limit"; return false; }
            for (const std::string& value : data.pinnedAppIds) if (value.size() > kDesktopConfigMaxLaunchIdBytes || value.find('\0') != std::string::npos || hasControl(value)) { err = "pinned App Model identity exceeds its byte or character limit"; return false; }
            for (const std::string& value : data.recentAppIds) if (value.size() > kDesktopConfigMaxLaunchIdBytes || value.find('\0') != std::string::npos || hasControl(value)) { err = "recent App Model identity exceeds its byte or character limit"; return false; }
            for (const DesktopShortcutRec& shortcut : data.desktopShortcuts) {
                const bool appTarget = shortcut.shortcutType == "App" && !shortcut.targetAppId.empty() && shortcut.targetPath.empty();
                const bool pathTarget = (shortcut.shortcutType == "File" || shortcut.shortcutType == "Folder") && !shortcut.targetPath.empty() && shortcut.targetAppId.empty();
                if ((!appTarget && !pathTarget) ||
                    shortcut.targetAppId.size() > apps::kAppModelMaxAppIdBytes ||
                    shortcut.targetPath.size() > kDesktopConfigMaxPathBytes ||
                    shortcut.label.size() > apps::kAppModelMaxDisplayNameBytes ||
                    shortcut.iconName.size() > apps::kAppModelMaxDisplayNameBytes ||
                    hasControl(shortcut.targetAppId) || hasControl(shortcut.targetPath) ||
                    hasControl(shortcut.label) || hasControl(shortcut.iconName)) {
                    err = "desktop shortcut target is invalid or exceeds its byte limit";
                    return false;
                }
            }
            auto jsonEscape=[&](const std::string& s){ std::ostringstream o; o<<'"'; for(char c: s){ switch(c){ case '"': o<<"\\\""; break; case '\\': o<<"\\\\"; break; case '\n': o<<"\\n"; break; case '\r': o<<"\\r"; break; case '\t': o<<"\\t"; break; default: o<<c; break; } } o<<'"'; return o.str(); };
            std::ostringstream f;
            f << "{\n";
            f << "  \"wallpaper\": " << jsonEscape(data.wallpaperPath) << ",\n";
            f << "  \"desktop.wallpaper.id\": " << jsonEscape(data.wallpaperId) << ",\n";
            DesktopThemeId themeId = DesktopThemeId::Classic;
            TryParseDesktopThemeId(data.desktopThemeId.c_str(), &themeId);
            f << "  \"desktop.theme.id\": " << jsonEscape(DesktopThemeIdToString(themeId)) << ",\n";
            f << "  \"desktop.background.scale\": " << jsonEscape(data.backgroundScaleMode.empty() ? "fill" : data.backgroundScaleMode) << ",\n";
            f << "  \"desktop.taskbar.position\": " << jsonEscape(data.taskbarPosition.empty() ? "bottom" : data.taskbarPosition) << ",\n";
            f << "  \"desktop.display.mode\": " << jsonEscape(normalizeDisplayModeName(data.displayMode)) << ",\n";
            f << "  \"desktop.display.primaryId\": " << jsonEscape(data.displayPrimaryDisplayId.empty() ? "display-1" : data.displayPrimaryDisplayId) << ",\n";
            f << "  \"desktop.display.arrangement\": " << jsonEscape(data.displayArrangement) << ",\n";
            f << "  \"desktop.display.resolution\": " << jsonEscape(data.displayResolution) << ",\n";
            f << "  \"desktop.clock.timeZoneId\": " << jsonEscape(gxos::clocktime::NormalizeTimeZoneId(data.timeZoneId)) << ",\n";
            f << "  \"desktop.clock.use24HourTime\": " << (data.use24HourTime ? "true" : "false") << ",\n";
            f << "  \"pinned\": ["; for(size_t i=0;i<data.pinned.size();++i){ if(i) f<<","; f<<jsonEscape(data.pinned[i]);} f << "],\n";
            f << "  \"pinnedAppIds\": ["; for(size_t i=0;i<data.pinned.size();++i){ if(i) f<<","; f<<jsonEscape(i < data.pinnedAppIds.size() ? data.pinnedAppIds[i] : std::string());} f << "],\n";
            f << "  \"recent\": ["; for(size_t i=0;i<data.recent.size();++i){ if(i) f<<","; f<<jsonEscape(data.recent[i]);} f << "],\n";
            f << "  \"recentAppIds\": ["; for(size_t i=0;i<data.recent.size();++i){ if(i) f<<","; f<<jsonEscape(i < data.recentAppIds.size() ? data.recentAppIds[i] : std::string());} f << "],\n";
            f << "  \"desktopShortcuts\": [\n";
            for(size_t i=0;i<data.desktopShortcuts.size();++i){ const auto& sc=data.desktopShortcuts[i]; f << "    {";
            f << "\"shortcutType\": " << jsonEscape(sc.shortcutType.empty() ? "App" : sc.shortcutType) << ", ";
            f << "\"targetAppId\": " << jsonEscape(sc.targetAppId) << ", ";
            f << "\"targetPath\": " << jsonEscape(sc.targetPath) << ", ";
            f << "\"label\": " << jsonEscape(sc.label) << ", ";
            f << "\"iconName\": " << jsonEscape(sc.iconName);
            f << "}"; if(i+1<data.desktopShortcuts.size()) f << ","; f << "\n"; }
            f << "  ],\n";
            f << "  \"showDesktopTrash\": " << (data.showDesktopTrash ? "true" : "false") << ",\n";
            f << "  \"showDesktopThisSystem\": " << (data.showDesktopThisSystem ? "true" : "false") << ",\n";
            f << "  \"showDesktopFileManager\": " << (data.showDesktopFileManager ? "true" : "false") << ",\n";
            f << "  \"showDesktopSystemSettings\": " << (data.showDesktopSystemSettings ? "true" : "false") << ",\n";
            f << "  \"smallLiveDesktopFolderIcons\": " << (data.smallLiveDesktopFolderIcons ? "true" : "false") << ",\n";
            f << "  \"autoArrangeDesktopIcons\": " << (data.autoArrangeDesktopIcons ? "true" : "false") << ",\n";
            f << "  \"desktop.accessibility.enhancedFocusIndicator\": " << (data.enhancedFocusIndicator ? "true" : "false") << ",\n";
            f << "  \"windows\": [\n";
            for(size_t i=0;i<data.windows.size();++i){ const auto& w=data.windows[i]; f << "    {";
            f << "\"id\": " << w.id << ", "; f << "\"title\": " << jsonEscape(w.title) << ", "; f << "\"x\": "<<w.x<<", \"y\": "<<w.y<<", \"w\": "<<w.w<<", \"h\": "<<w.h<<", ";
            f << "\"minimized\": "<<(w.minimized?"true":"false")<<", \"maximized\": "<<(w.maximized?"true":"false")<<", ";
            f << "\"z\": "<<w.z<<", \"focused\": "<<(w.focused?"true":"false")<<", \"snap\": "<<w.snap<<", ";
            f << "\"restoreX\": "<<w.restoreX<<", \"restoreY\": "<<w.restoreY<<", \"restoreW\": "<<w.restoreW<<", \"restoreH\": "<<w.restoreH;
            f << "}"; if(i+1<data.windows.size()) f << ","; f << "\n"; }
            f << "  ],\n";
            f << "  \"iconPositions\": [\n";
            for(size_t i=0;i<data.iconPositions.size();++i){ const auto& ip=data.iconPositions[i]; f << "    {";
            f << "\"name\": " << jsonEscape(ip.name) << ", \"x\": " << ip.x << ", \"y\": " << ip.y;
            f << "}"; if(i+1<data.iconPositions.size()) f << ","; f << "\n"; }
            f << "  ]\n";
            f << "}\n";
            const std::string serialized = f.str();
            if (serialized.size() > kDesktopConfigMaxBytes) { err = "desktop config exceeds its byte limit"; return false; }
            std::vector<uint8_t> bytes(serialized.begin(), serialized.end());
            if (!FS::writeAll(path, bytes)) { err = "write fail"; return false; }
            return true;
        }
    };
} }
