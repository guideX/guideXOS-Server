#include "process.h"
#include "scheduler.h"
#include "logger.h"
#include "allocator.h"
#include <thread>

namespace gxos {
    std::unordered_map<uint64_t, std::shared_ptr<Process>> ProcessTable::g_proc; 
    uint64_t ProcessTable::g_nextPid = 10; 
    std::mutex ProcessTable::g_lock;

    static std::vector<char*> make_argv(const std::vector<std::string>& args, std::vector<std::unique_ptr<char[]>>& hold){
        std::vector<char*> argv; argv.reserve(args.size()+1);
        for (auto& s: args){ auto buf = std::unique_ptr<char[]>(new char[s.size()+1]); std::memcpy(buf.get(), s.c_str(), s.size()+1); argv.push_back(buf.get()); hold.push_back(std::move(buf)); }
        argv.push_back(nullptr); return argv;
    }

    uint64_t ProcessTable::spawn(const ProcessSpec& spec, const std::vector<std::string>& args){
        std::lock_guard<std::mutex> _g(g_lock);
        uint64_t pid = g_nextPid++;
        auto p = std::make_shared<Process>(pid, spec.name, spec.entry);
        g_proc[pid] = p;
        // run in scheduler thread
        Scheduler::enqueue({ [pid,args]{
            auto it = g_proc.find(pid); if (it==g_proc.end()) return; auto proc = it->second; 
            {
                std::lock_guard<std::mutex> lk(proc->mu);
                proc->running = true; 
                proc->finished = false;
            }
            Allocator::setCurrentPid(pid);
            std::vector<std::unique_ptr<char[]>> hold; auto argv = make_argv(args, hold); int argc = (int)args.size();
            try { proc->exitCode = proc->entry(argc, argv.data()); }
            catch(...) { Logger::write(LogLevel::Error, "Process crashed: "+proc->name); proc->exitCode = -1; }
            Allocator::setCurrentPid(0);
            {
                std::lock_guard<std::mutex> lk(proc->mu);
                proc->running=false; proc->finished=true; 
            }
            proc->cv.notify_all();
        }});
        return pid;
    }

    bool ProcessTable::send(uint64_t dstPid, ipc::Message&& msg){ std::lock_guard<std::mutex> _g(g_lock); auto it=g_proc.find(dstPid); if (it==g_proc.end()) return false; it->second->mbox.push(std::move(msg)); return true; }
    bool ProcessTable::try_recv(uint64_t pid, ipc::Message& out){ std::lock_guard<std::mutex> _g(g_lock); auto it=g_proc.find(pid); if (it==g_proc.end()) return false; return it->second->mbox.try_pop(out); }
    bool ProcessTable::wait_recv(uint64_t pid, ipc::Message& out, uint64_t timeoutMs){ std::lock_guard<std::mutex> _g(g_lock); auto it=g_proc.find(pid); if (it==g_proc.end()) return false; return it->second->mbox.pop(out, timeoutMs); }
    bool ProcessTable::terminate(uint64_t pid){ std::lock_guard<std::mutex> _g(g_lock); return g_proc.erase(pid)>0; }
    std::vector<uint64_t> ProcessTable::list(){ std::lock_guard<std::mutex> _g(g_lock); std::vector<uint64_t> v; v.reserve(g_proc.size()); for(auto& kv: g_proc) v.push_back(kv.first); return v; }

    bool ProcessTable::wait(uint64_t pid, uint64_t timeoutMs, int* exitCodeOut){
        std::shared_ptr<Process> proc;
        {
            std::lock_guard<std::mutex> _g(g_lock);
            auto it = g_proc.find(pid); if(it==g_proc.end()) return false; proc = it->second;
        }
        std::unique_lock<std::mutex> lk(proc->mu);
        if(!proc->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&]{ return proc->finished; })) return false;
        if(exitCodeOut) *exitCodeOut = proc->exitCode; return true;
    }

    bool ProcessTable::getStatus(uint64_t pid, bool& runningOut, int& exitCodeOut){
        std::lock_guard<std::mutex> _g(g_lock);
        auto it = g_proc.find(pid); if (it==g_proc.end()) return false; runningOut = it->second->running; exitCodeOut = it->second->exitCode; return true;
    }
}
