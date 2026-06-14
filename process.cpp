#include "process.h"
#include "process.h"
#include "scheduler.h"
#include "logger.h"
#include "allocator.h"
#include <cstring>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <time.h>
#endif

namespace gxos {
    std::unordered_map<uint64_t, std::shared_ptr<Process>> ProcessTable::g_proc; 
    uint64_t ProcessTable::g_nextPid = 10; 
    std::mutex ProcessTable::g_lock;

    Process::~Process() {
#if defined(_WIN32)
        if (cpuThreadHandle) {
            CloseHandle(reinterpret_cast<HANDLE>(cpuThreadHandle));
            cpuThreadHandle = nullptr;
        }
#endif
    }

    namespace {
        static uint64_t steady_clock_micros() {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

#if defined(_WIN32)
        static bool query_thread_cpu_micros(ProcessThreadHandle handle, uint64_t& outMicros) {
            if (!handle) return false;

            FILETIME creation{};
            FILETIME exit{};
            FILETIME kernel{};
            FILETIME user{};
            if (!GetThreadTimes(reinterpret_cast<HANDLE>(handle), &creation, &exit, &kernel, &user)) {
                return false;
            }

            ULARGE_INTEGER kernelTime{};
            kernelTime.LowPart = kernel.dwLowDateTime;
            kernelTime.HighPart = kernel.dwHighDateTime;
            ULARGE_INTEGER userTime{};
            userTime.LowPart = user.dwLowDateTime;
            userTime.HighPart = user.dwHighDateTime;
            outMicros = (kernelTime.QuadPart + userTime.QuadPart) / 10ULL;
            return true;
        }

        static ProcessThreadHandle current_thread_handle() {
            return reinterpret_cast<ProcessThreadHandle>(GetCurrentThread());
        }

        static ProcessThreadHandle duplicate_thread_handle(ProcessThreadHandle handle) {
            if (!handle) return nullptr;

            HANDLE duplicate = nullptr;
            if (!DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(handle), GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                return nullptr;
            }
            return reinterpret_cast<ProcessThreadHandle>(duplicate);
        }
#elif defined(__unix__) || defined(__APPLE__)
        static bool query_thread_cpu_micros(ProcessThreadHandle handle, uint64_t& outMicros) {
            clockid_t clockId;
            if (pthread_getcpuclockid(handle, &clockId) != 0) {
                return false;
            }

            timespec ts{};
            if (clock_gettime(clockId, &ts) != 0) {
                return false;
            }

            outMicros = static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec / 1000ULL);
            return true;
        }

        static ProcessThreadHandle current_thread_handle() {
            return pthread_self();
        }
#endif
    } // namespace

    static std::vector<char*> make_argv(const std::vector<std::string>& args, std::vector<std::unique_ptr<char[]>>& hold){
        std::vector<char*> argv; argv.reserve(args.size()+1);
        for (auto& s: args){ auto buf = std::unique_ptr<char[]>(new char[s.size()+1]); std::memcpy(buf.get(), s.c_str(), s.size()+1); argv.push_back(buf.get()); hold.push_back(std::move(buf)); }
        argv.push_back(nullptr); return argv;
    }

    uint64_t ProcessTable::spawn(const ProcessSpec& spec, const std::vector<std::string>& args){
        uint64_t pid;
        std::shared_ptr<Process> p;
        {
            std::lock_guard<std::mutex> _g(g_lock);
            pid = g_nextPid++;
            p = std::make_shared<Process>(pid, spec.name, spec.entry);
            p->startWallMicros = steady_clock_micros();
            g_proc[pid] = p;
        }
        // Run on a dedicated thread instead of the scheduler to avoid blocking
        // when the single scheduler worker is occupied (e.g., by the compositor)
        std::thread t([pid, args, p]{
            p->running.store(true, std::memory_order_release);
            p->finished.store(false, std::memory_order_release);
            Allocator::setCurrentPid(pid);
            std::vector<std::unique_ptr<char[]>> hold; 
            auto argv = make_argv(args, hold); 
            int argc = (int)args.size();
            try { 
                p->exitCode = p->entry(argc, argv.data()); 
            }
            catch(...) { 
                Logger::write(LogLevel::Error, "Process crashed: "+p->name); 
                p->exitCode = -1; 
            }
            Allocator::setCurrentPid(0);
            uint64_t cpuMicros = 0;
            if (query_thread_cpu_micros(current_thread_handle(), cpuMicros)) {
                p->cpuFinalMicros.store(cpuMicros, std::memory_order_release);
                p->cpuSampleValid.store(true, std::memory_order_release);
            }
            p->running.store(false, std::memory_order_release);
            p->finished.store(true, std::memory_order_release);
            p->cv.notify_all();
        });
#if defined(_WIN32)
        p->cpuThreadHandle = duplicate_thread_handle(reinterpret_cast<ProcessThreadHandle>(static_cast<uintptr_t>(t.native_handle())));
#elif defined(__unix__) || defined(__APPLE__)
        p->cpuThreadHandle = t.native_handle();
#endif
        t.detach();
        return pid;
    }

    bool ProcessTable::send(uint64_t dstPid, ipc::Message&& msg){ std::lock_guard<std::mutex> _g(g_lock); auto it=g_proc.find(dstPid); if (it==g_proc.end()) return false; it->second->mbox.push(std::move(msg)); return true; }
    bool ProcessTable::try_recv(uint64_t pid, ipc::Message& out){ std::lock_guard<std::mutex> _g(g_lock); auto it=g_proc.find(pid); if (it==g_proc.end()) return false; return it->second->mbox.try_pop(out); }
    bool ProcessTable::wait_recv(uint64_t pid, ipc::Message& out, uint64_t timeoutMs){
        std::shared_ptr<Process> proc;
        {
            std::lock_guard<std::mutex> _g(g_lock);
            auto it=g_proc.find(pid); if (it==g_proc.end()) return false; proc = it->second;
        }
        return proc->mbox.pop(out, timeoutMs);
    }
    bool ProcessTable::terminate(uint64_t pid){ std::lock_guard<std::mutex> _g(g_lock); return g_proc.erase(pid)>0; }
    std::vector<uint64_t> ProcessTable::list(){ std::lock_guard<std::mutex> _g(g_lock); std::vector<uint64_t> v; v.reserve(g_proc.size()); for(auto& kv: g_proc) v.push_back(kv.first); return v; }

    bool ProcessTable::wait(uint64_t pid, uint64_t timeoutMs, int* exitCodeOut){
        std::shared_ptr<Process> proc;
        {
            std::lock_guard<std::mutex> _g(g_lock);
            auto it = g_proc.find(pid); if(it==g_proc.end()) return false; proc = it->second;
        }
        std::unique_lock<std::mutex> lk(proc->mu);
        if(!proc->cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&]{ return proc->finished.load(std::memory_order_acquire); })) return false;
        if(exitCodeOut) *exitCodeOut = proc->exitCode; return true;
    }

    bool ProcessTable::getStatus(uint64_t pid, bool& runningOut, int& exitCodeOut){
        std::lock_guard<std::mutex> _g(g_lock);
        auto it = g_proc.find(pid); if (it==g_proc.end()) return false; runningOut = it->second->running.load(std::memory_order_acquire); exitCodeOut = it->second->exitCode; return true;
    }

    bool ProcessTable::cpuTelemetry(uint64_t pid, ProcessCpuTelemetry& out){
        std::shared_ptr<Process> proc;
        {
            std::lock_guard<std::mutex> _g(g_lock);
            auto it = g_proc.find(pid);
            if (it == g_proc.end()) return false;
            proc = it->second;
        }

        out = ProcessCpuTelemetry{};
        out.running = proc->running.load(std::memory_order_acquire);
        out.startWallMicros = proc->startWallMicros;
        uint64_t cpuMicros = 0;
        bool haveCpu = false;

#if defined(_WIN32) || defined(__unix__) || defined(__APPLE__)
        if (proc->cpuThreadHandle) {
            haveCpu = query_thread_cpu_micros(proc->cpuThreadHandle, cpuMicros);
        }
#endif

        if (!haveCpu && proc->cpuSampleValid.load(std::memory_order_acquire)) {
            cpuMicros = proc->cpuFinalMicros.load(std::memory_order_relaxed);
            haveCpu = true;
        }

        if (haveCpu) {
            out.available = true;
            out.cpuMicros = cpuMicros;
        }
        return true;
    }
}
