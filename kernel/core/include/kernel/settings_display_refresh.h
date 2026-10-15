#ifndef KERNEL_SETTINGS_DISPLAY_REFRESH_H
#define KERNEL_SETTINGS_DISPLAY_REFRESH_H

#include "settings_native_provider.h"

namespace kernel {
namespace settings_native {

enum class DisplaySnapshotRefreshResult : uint32_t {
    NoChange,
    Updated,
    Unavailable,
    Rejected
};

// The display snapshot revision is activeConfigurationGeneration + 1, so
// revision 1 is reserved for the zero/unavailable generation. Ordering uses
// the standard half-range rule for the provider's 32-bit source generation.
bool displaySnapshotRevisionIsNewer(uint64_t candidate, uint64_t current);
bool displayConfigurationGenerationIsNewer(uint32_t candidate, uint32_t current);
bool displaySnapshotsHaveSameValues(const DisplaySnapshotV1& left,
                                    const DisplaySnapshotV1& right);

class DisplaySnapshotRefreshState {
public:
    DisplaySnapshotRefreshState();

    void reset();
    bool hasSnapshot() const { return m_hasSnapshot; }
    uint64_t revision() const { return m_revision; }
    const DisplaySnapshotV1& snapshot() const { return m_snapshot; }

    // Keeps the accepted revision as a watermark while discarding values that
    // can no longer be represented as current after a failed provider read.
    bool markUnavailable();

    // Validates and accepts a provider result. A duplicate revision is
    // accepted only when every value also matches the already accepted data.
    DisplaySnapshotRefreshResult accept(const DisplaySnapshotV1& candidate);

private:
    DisplaySnapshotV1 m_snapshot;
    uint64_t m_revision;
    bool m_hasSnapshot;
};

// Uses the production adapter's authorized Display read callback and commits
// only a fully validated, bounded snapshot to both the cache and S10 core.
DisplaySnapshotRefreshResult refreshDisplaySnapshot(
    const gxos::apps::settings::SettingsPresentationAdapterV1* adapter,
    DisplaySnapshotRefreshState& state,
    gxos::apps::settings::SettingsPresentationCore& presentation);

// A single pending generation is enough: repeated source notifications
// coalesce until the app's normal update consumes the marker.
class DisplayConfigurationChangeSignal {
public:
    DisplayConfigurationChangeSignal();
    void reset();
    void signal(uint32_t generation);
    bool consume(uint32_t* generation);

private:
    uint32_t m_generation;
    bool m_pending;
};

} // namespace settings_native
} // namespace kernel

#endif // KERNEL_SETTINGS_DISPLAY_REFRESH_H
