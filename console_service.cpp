#include "console_service.h"
#include "process.h"
#include "logger.h"
#include "scheduler.h"
#include <sstream>

namespace gxos { namespace svc {
    using namespace gxos;
    static const char* kInputChan = "console.input";
    static const char* kOutputChan = "console.output";

    static std::string trim(const std::string& s){ size_t a = s.find_first_not_of(" \t\r\n"); if(a==std::string::npos) return {}; size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b-a+1); }

    int ConsoleService::main(int argc, char** argv){
        Logger::write(LogLevel::Info, "ConsoleService started");
        ipc::Bus::ensure(kInputChan); ipc::Bus::ensure(kOutputChan);
        // Subscribe so the service also gets fanout publish
        // Not required as we will read from Bus::pop which reads the queue
        while(true){
            ipc::Message m; if(!ipc::Bus::pop(kInputChan, m, 1000)){ continue; }
            std::string line(m.data.begin(), m.data.end()); line = trim(line);
            if(line=="exit"||line=="quit"){ ipc::Message out; out.type=0; std::string r = "bye"; out.data.assign(r.begin(), r.end()); ipc::Bus::publish(kOutputChan, std::move(out), false); break; }
            // Basic demo: echo and simple commands
            std::string resp = "[console] " + line;
            ipc::Message out; out.type=0; out.data.assign(resp.begin(), resp.end()); ipc::Bus::publish(kOutputChan, std::move(out), false);
        }
        Logger::write(LogLevel::Info, "ConsoleService stopped");
        return 0;
    }

    uint64_t ConsoleService::start(){ ProcessSpec spec{"console", ConsoleService::main}; return ProcessTable::spawn(spec, {"console"}); }
} }
