#include "logger.h"
#include <iostream>
namespace gxos {
    std::mutex Logger::g_lock; std::vector<LogEntry> Logger::g_buf;
    void Logger::write(LogLevel lvl, const std::string& msg){
        std::lock_guard<std::mutex> _g(g_lock);
        g_buf.push_back({lvl,msg});
        std::cout << msg << std::endl;
    }
    std::vector<LogEntry> Logger::snapshot(){ std::lock_guard<std::mutex> _g(g_lock); return g_buf; }
}
