#include "scheduler.h"
#include "logger.h"
#include <condition_variable>

namespace gxos {
    std::vector<std::thread> Scheduler::g_threads; 
    std::vector<Task> Scheduler::g_queue; 
    std::mutex Scheduler::g_lock; 
    std::atomic<bool> Scheduler::g_stop{false}; 
    std::atomic<uint64_t> Scheduler::g_executed{0};
    static std::condition_variable g_cv;

    void Scheduler::init(unsigned workers){
        g_stop=false; g_executed=0;
        for (unsigned i=0;i<workers;i++){
            g_threads.emplace_back(loop);
        }
    }
    void Scheduler::enqueue(const Task& t){ std::lock_guard<std::mutex> _g(g_lock); g_queue.push_back(t); g_cv.notify_one(); }
    void Scheduler::shutdown(){ g_stop=true; g_cv.notify_all(); for(auto& th: g_threads){ if(th.joinable()) th.join(); } g_threads.clear(); }
    uint64_t Scheduler::tasksExecuted(){ return g_executed.load(); }

    void Scheduler::loop(){
        std::unique_lock<std::mutex> lk(g_lock, std::defer_lock);
        while(!g_stop){
            lk.lock();
            g_cv.wait(lk, []{ return g_stop || !g_queue.empty(); });
            if (g_stop){ lk.unlock(); break; }
            Task t = g_queue.back(); g_queue.pop_back();
            lk.unlock();
            try{ t.fn(); g_executed++; }
            catch(...){ Logger::write(LogLevel::Error, "Task threw exception"); }
        }
    }
}
