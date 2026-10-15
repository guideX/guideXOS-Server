#include "include/kernel/settings_display_refresh.h"

namespace kernel {
namespace settings_native {

namespace {

static bool textEquals(const char* left, const char* right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        if (*left != *right) return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static void applyDisplayCapability(
    gxos::apps::settings::SettingsPresentationCore& presentation,
    const DisplaySnapshotRefreshState& state)
{
    gxos::apps::settings::CapabilitySnapshot capability{};
    capability.id = gxos::apps::settings::CapabilityId::Display;
    capability.availability = state.hasSnapshot() &&
            state.snapshot().status == DisplayInformationStatus::Available
        ? gxos::apps::settings::CapabilityAvailability::ReadOnly
        : gxos::apps::settings::CapabilityAvailability::Unavailable;
    capability.revision = state.revision();
    presentation.applyCapabilitySnapshot(capability);
}

} // namespace

bool displayConfigurationGenerationIsNewer(uint32_t candidate, uint32_t current)
{
    const uint32_t difference = candidate - current;
    return difference != 0u && difference < 0x80000000u;
}

bool displaySnapshotRevisionIsNewer(uint64_t candidate, uint64_t current)
{
    if (candidate == 0u) return false;
    if (current == 0u) return true;
    if (candidate == current) return false;

    const uint32_t candidateGeneration = static_cast<uint32_t>(candidate - 1u);
    const uint32_t currentGeneration = static_cast<uint32_t>(current - 1u);
    const uint32_t difference = candidateGeneration - currentGeneration;
    return difference != 0u && difference < 0x80000000u;
}

bool displaySnapshotsHaveSameValues(const DisplaySnapshotV1& left,
                                    const DisplaySnapshotV1& right)
{
    return left.structureSize == right.structureSize &&
        left.version == right.version && left.status == right.status &&
        left.flags == right.flags &&
        left.framebufferWidth == right.framebufferWidth &&
        left.framebufferHeight == right.framebufferHeight &&
        left.pixelFormat == right.pixelFormat &&
        left.bitsPerPixel == right.bitsPerPixel &&
        left.bytesPerScanline == right.bytesPerScanline &&
        left.logicalDesktopWidth == right.logicalDesktopWidth &&
        left.logicalDesktopHeight == right.logicalDesktopHeight &&
        left.activeLayoutMode == right.activeLayoutMode &&
        textEquals(left.primaryModeId, right.primaryModeId);
}

DisplaySnapshotRefreshState::DisplaySnapshotRefreshState()
    : m_snapshot{}, m_revision(0u), m_hasSnapshot(false)
{
}

void DisplaySnapshotRefreshState::reset()
{
    m_snapshot = DisplaySnapshotV1{};
    m_revision = 0u;
    m_hasSnapshot = false;
}

bool DisplaySnapshotRefreshState::markUnavailable()
{
    const bool changed = m_hasSnapshot;
    m_snapshot = DisplaySnapshotV1{};
    m_hasSnapshot = false;
    return changed;
}

DisplaySnapshotRefreshResult DisplaySnapshotRefreshState::accept(
    const DisplaySnapshotV1& candidate)
{
    if (!validDisplaySnapshot(candidate) || candidate.revision > 0x100000000ull) {
        return DisplaySnapshotRefreshResult::Rejected;
    }

    if (m_revision != 0u) {
        if (candidate.revision == m_revision) {
            if (!m_hasSnapshot) {
                m_snapshot = candidate;
                m_hasSnapshot = true;
                return DisplaySnapshotRefreshResult::Updated;
            }
            return displaySnapshotsHaveSameValues(candidate, m_snapshot)
                ? DisplaySnapshotRefreshResult::NoChange
                : DisplaySnapshotRefreshResult::Rejected;
        }
        if (!displaySnapshotRevisionIsNewer(candidate.revision, m_revision)) {
            return DisplaySnapshotRefreshResult::Rejected;
        }
    }

    const bool changed = !m_hasSnapshot ||
        !displaySnapshotsHaveSameValues(candidate, m_snapshot);
    m_snapshot = candidate;
    m_revision = candidate.revision;
    m_hasSnapshot = true;
    return changed ? DisplaySnapshotRefreshResult::Updated
                   : DisplaySnapshotRefreshResult::NoChange;
}

DisplaySnapshotRefreshResult refreshDisplaySnapshot(
    const gxos::apps::settings::SettingsPresentationAdapterV1* adapter,
    DisplaySnapshotRefreshState& state,
    gxos::apps::settings::SettingsPresentationCore& presentation)
{
    if (!adapter || !adapter->readProviderSnapshot) {
        const bool changed = state.markUnavailable();
        applyDisplayCapability(presentation, state);
        return changed ? DisplaySnapshotRefreshResult::Unavailable
                       : DisplaySnapshotRefreshResult::NoChange;
    }

    DisplaySnapshotV1 candidate{};
    size_t snapshotSize = 0u;
    uint64_t snapshotRevision = 0u;
    const bool read = adapter->readProviderSnapshot(
        adapter->context,
        gxos::apps::settings::CapabilityId::Display,
        &candidate,
        sizeof(candidate),
        &snapshotSize,
        &snapshotRevision);
    if (!read || snapshotSize != sizeof(candidate) ||
        snapshotRevision != candidate.revision ||
        !validDisplaySnapshot(candidate)) {
        const bool changed = state.markUnavailable();
        applyDisplayCapability(presentation, state);
        return changed ? DisplaySnapshotRefreshResult::Unavailable
                       : DisplaySnapshotRefreshResult::Rejected;
    }

    const DisplaySnapshotRefreshResult result = state.accept(candidate);
    if (result == DisplaySnapshotRefreshResult::Updated ||
        result == DisplaySnapshotRefreshResult::NoChange) {
        applyDisplayCapability(presentation, state);
    }
    return result;
}

DisplayConfigurationChangeSignal::DisplayConfigurationChangeSignal()
    : m_generation(0u), m_pending(false)
{
}

void DisplayConfigurationChangeSignal::reset()
{
    m_generation = 0u;
    m_pending = false;
}

void DisplayConfigurationChangeSignal::signal(uint32_t generation)
{
    if (generation == 0u) return;
    if (!m_pending || displayConfigurationGenerationIsNewer(generation, m_generation)) {
        m_generation = generation;
        m_pending = true;
    }
}

bool DisplayConfigurationChangeSignal::consume(uint32_t* generation)
{
    if (!m_pending || !generation) return false;
    *generation = m_generation;
    m_generation = 0u;
    m_pending = false;
    return true;
}

} // namespace settings_native
} // namespace kernel
