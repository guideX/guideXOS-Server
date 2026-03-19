#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

namespace gxos { namespace gui {

    enum class NotificationLevel : uint8_t {
        Info = 0,
        Error = 1
    };

    struct Notification {
        std::string message;
        NotificationLevel level;
        uint64_t createdTicks;
        uint64_t dismissAfterTicks;   // absolute tick at which to auto-dismiss
        int slideProgress;            // animation progress (0..maxSlide)
        int maxSlide;                 // threshold + measured text width
        bool dismissed;
    };

    /// Notification toast manager ported from guideXOS.Legacy NotificationManager.cs
    /// Toasts slide in from the right edge and auto-dismiss after a timeout.
    class NotificationManager {
    public:
        static void Initialize();

        /// Enqueue a toast
        static void Add(const std::string& message, NotificationLevel level = NotificationLevel::Info);

        /// Tick the animation / auto-dismiss logic (call once per frame)
        static void Update(uint64_t currentTicks);

        /// Return the current list of visible notifications (for rendering)
        static std::vector<Notification> Snapshot();

        /// Clear all notifications immediately
        static void Clear();

        /// Number of pending notifications
        static size_t Count();

        // Tuning constants (match C# legacy)
        static constexpr int kDivide    = 30;
        static constexpr int kThreshold = 50;
        static constexpr uint64_t kDismissMs = 1000;  // auto-dismiss delay after fully slid in
        static constexpr int kFontSize  = 16;         // approximate glyph height
    private:
        static std::vector<Notification> s_notifications;
        static std::mutex s_mutex;
        static bool s_initialized;
    };

}} // namespace gxos::gui
