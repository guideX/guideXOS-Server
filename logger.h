#pragma once
#include <string>
#include <mutex>
#include <vector>
#include <cstdint>
namespace gxos {
    enum class LogLevel : uint8_t { Trace, Info, Warn, Error };
    struct LogEntry { LogLevel level; std::string msg; };
    class Logger {
    public:
        static void write(LogLevel lvl, const std::string& msg);
        static std::vector<LogEntry> snapshot();
    private:
        static std::mutex g_lock;
        static std::vector<LogEntry> g_buf;
    };
}
