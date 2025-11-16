#pragma once
#include "process.h"
#include "ipc_bus.h"
#include <string>

namespace gxos { namespace apps {
    
    /// <summary>
    /// Clock - Digital clock and date display
    /// Features: Real-time clock display, date display, auto-update
    /// </summary>
    class Clock {
    public:
        /// <summary>
        /// Launch a new Clock instance
        /// </summary>
        /// <returns>Process ID of the launched Clock</returns>
        static uint64_t Launch();
        
    private:
        // Main entry point for Clock process
        static int main(int argc, char** argv);
        
        // Time display
        static void updateDisplay();
        static std::string getCurrentTime();
        static std::string getCurrentDate();
        
        // State
        static uint64_t s_windowId;
        static uint64_t s_lastUpdateTicks;
    };
    
}} // namespace gxos::apps
