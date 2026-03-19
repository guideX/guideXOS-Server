#include "notification_manager.h"
#include "logger.h"
#include <algorithm>

namespace gxos { namespace gui {

std::vector<Notification> NotificationManager::s_notifications;
std::mutex NotificationManager::s_mutex;
bool NotificationManager::s_initialized = false;

void NotificationManager::Initialize() {
    std::lock_guard<std::mutex> lk(s_mutex);
    s_notifications.clear();
    s_initialized = true;
    Logger::write(LogLevel::Info, "NotificationManager initialized");
}

void NotificationManager::Add(const std::string& message, NotificationLevel level) {
    std::lock_guard<std::mutex> lk(s_mutex);
    Notification n;
    n.message = message;
    n.level = level;
    n.createdTicks = 0;
    n.dismissAfterTicks = 0;
    // Approximate text width: message length * average glyph width (~8px)
    n.maxSlide = kThreshold + static_cast<int>(message.size()) * 8;
    n.slideProgress = 0;
    n.dismissed = false;
    s_notifications.push_back(std::move(n));
    Logger::write(LogLevel::Info, std::string("Notification: ") + message);
}

void NotificationManager::Update(uint64_t currentTicks) {
    std::lock_guard<std::mutex> lk(s_mutex);
    if (s_notifications.empty()) return;

    // Animate the first notification that hasn't fully slid in yet
    for (auto& n : s_notifications) {
        if (n.dismissed) continue;
        if (n.slideProgress < n.maxSlide) {
            // Slide speed: ~4px per tick (adjust to taste)
            n.slideProgress = std::min(n.slideProgress + 4, n.maxSlide);
            break; // only animate one at a time (queued slide-in)
        }
    }

    // Mark fully-slid notifications for dismissal after timeout
    for (auto& n : s_notifications) {
        if (n.dismissed) continue;
        if (n.slideProgress >= n.maxSlide) {
            if (n.dismissAfterTicks == 0) {
                n.dismissAfterTicks = currentTicks + kDismissMs;
            }
            if (currentTicks > n.dismissAfterTicks) {
                n.dismissed = true;
                break; // remove one per tick to keep animation smooth
            }
        }
    }

    // Erase dismissed
    s_notifications.erase(
        std::remove_if(s_notifications.begin(), s_notifications.end(),
                       [](const Notification& n){ return n.dismissed; }),
        s_notifications.end());
}

std::vector<Notification> NotificationManager::Snapshot() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_notifications;
}

void NotificationManager::Clear() {
    std::lock_guard<std::mutex> lk(s_mutex);
    s_notifications.clear();
}

size_t NotificationManager::Count() {
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_notifications.size();
}

}} // namespace gxos::gui
