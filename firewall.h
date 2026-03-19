#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

namespace gxos {

    enum class FirewallMode : uint8_t {
        Normal    = 0,   // Requires user approval for new connections
        BlockAll  = 1,   // Block all network connections
        Disabled  = 2,   // All connections allowed
        Autolearn = 3    // Automatically allow new connections
    };

    /// Firewall manager ported from guideXOS.Legacy Firewall.cs
    /// Manages network rules, exception list, and pending alerts.
    class Firewall {
    public:
        static void Initialize();

        /// Check if a program is allowed to perform a network action
        static bool Check(const std::string& program, const std::string& action);

        /// Add a program to the exception (allow) list
        static void AddException(const std::string& name);

        /// Check if a program is already in the exception list
        static bool IsException(const std::string& name);

        /// Remove a pending alert
        static void RemoveAlert(const std::string& program, const std::string& action);

        /// Clear all pending alerts
        static void ClearAlerts();

        /// Get a copy of the exception list
        static std::vector<std::string> Exceptions();

        /// Get a copy of the pending alerts ("program|action")
        static std::vector<std::string> PendingAlerts();

        /// Current mode
        static FirewallMode GetMode();
        static void SetMode(FirewallMode mode);

    private:
        static void queueAlert(const std::string& program, const std::string& action);

        static FirewallMode s_mode;
        static std::vector<std::string> s_exceptions;
        static std::vector<std::string> s_pendingAlerts;
        static std::mutex s_mutex;
        static bool s_initialized;
    };

} // namespace gxos
