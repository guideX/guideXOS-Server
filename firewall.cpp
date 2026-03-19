#include "firewall.h"
#include "logger.h"
#include <algorithm>

namespace gxos {

FirewallMode Firewall::s_mode = FirewallMode::Normal;
std::vector<std::string> Firewall::s_exceptions;
std::vector<std::string> Firewall::s_pendingAlerts;
std::mutex Firewall::s_mutex;
bool Firewall::s_initialized = false;

static bool listHas(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

void Firewall::Initialize() {
    std::lock_guard<std::mutex> lk(s_mutex);
    s_exceptions.clear();
    s_pendingAlerts.clear();
    s_mode = FirewallMode::Normal;
    s_initialized = true;
    Logger::write(LogLevel::Info, "Firewall initialized (Normal mode)");
}

bool Firewall::Check(const std::string& program, const std::string& action) {
    std::lock_guard<std::mutex> lk(s_mutex);
    if (s_mode == FirewallMode::Disabled) return true;
    if (s_mode == FirewallMode::BlockAll) { queueAlert(program, action); return false; }
    if (listHas(s_exceptions, program)) return true;
    if (s_mode == FirewallMode::Autolearn) {
        s_exceptions.push_back(program);
        Logger::write(LogLevel::Info, "Firewall autolearn: allowed " + program);
        return true;
    }
    // Normal mode — block and alert
    queueAlert(program, action);
    return false;
}

void Firewall::AddException(const std::string& name) {
    std::lock_guard<std::mutex> lk(s_mutex);
    if (!listHas(s_exceptions, name)) {
        s_exceptions.push_back(name);
        Logger::write(LogLevel::Info, "Firewall exception added: " + name);
    }
}

bool Firewall::IsException(const std::string& name) {
    std::lock_guard<std::mutex> lk(s_mutex);
    return listHas(s_exceptions, name);
}

void Firewall::RemoveAlert(const std::string& program, const std::string& action) {
    std::lock_guard<std::mutex> lk(s_mutex);
    std::string key = program + "|" + action;
    auto it = std::find(s_pendingAlerts.begin(), s_pendingAlerts.end(), key);
    if (it != s_pendingAlerts.end()) s_pendingAlerts.erase(it);
}

void Firewall::ClearAlerts() {
    std::lock_guard<std::mutex> lk(s_mutex);
    s_pendingAlerts.clear();
}

std::vector<std::string> Firewall::Exceptions() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_exceptions;
}

std::vector<std::string> Firewall::PendingAlerts() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_pendingAlerts;
}

FirewallMode Firewall::GetMode() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_mode;
}

void Firewall::SetMode(FirewallMode mode) {
    std::lock_guard<std::mutex> lk(s_mutex);
    s_mode = mode;
    const char* names[] = {"Normal", "BlockAll", "Disabled", "Autolearn"};
    Logger::write(LogLevel::Info, std::string("Firewall mode: ") + names[static_cast<int>(mode)]);
}

// ?? private ?????????????????????????????????????????????????????
void Firewall::queueAlert(const std::string& program, const std::string& action) {
    std::string key = program + "|" + action;
    if (!listHas(s_pendingAlerts, key)) {
        s_pendingAlerts.push_back(key);
        Logger::write(LogLevel::Info, "Firewall alert: " + key);
    }
}

} // namespace gxos
