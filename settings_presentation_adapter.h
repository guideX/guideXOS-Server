#pragma once

#include "settings_presentation_core.h"

namespace gxos {
namespace apps {
namespace settings {

enum : uint32_t {
    kSettingsPresentationAdapterVersion = 1
};

enum class SettingsInputKind : unsigned char {
    None,
    Key,
    Text,
    PointerMove,
    PointerButton,
    Scroll,
    Focus,
    Close,
    Paint
};

enum class SettingsInputAction : unsigned char {
    None,
    Down,
    Up
};

struct SettingsWindowConstraints {
    uint32_t minimumWidth;
    uint32_t minimumHeight;
    uint32_t preferredWidth;
    uint32_t preferredHeight;
    uint32_t maximumWidth;
    uint32_t maximumHeight;
    uint32_t flags;
};

struct SettingsInputEvent {
    SettingsInputKind kind;
    SettingsInputAction action;
    uint32_t code;
    uint32_t modifiers;
    int32_t x;
    int32_t y;
    int32_t delta;
    char text;
};

enum class SettingsOperationCode : uint16_t {
    ApplyDisplayConfiguration = 1,
    SetEnhancedFocusPreference = 2,
    SelectDesktopBackground = 3,
    SetDefaultApplication = 4,
    RestoreDefaultApplication = 5
};

// The adapter owns context and all runtime objects behind it. The core only
// exchanges bounded values and does not retain this context pointer.
struct SettingsPresentationAdapterV1 {
    uint32_t structureSize;
    uint32_t version;
    void* context;

    bool (*getWindowConstraints)(void* context, SettingsWindowConstraints* outConstraints);
    bool (*pollInput)(void* context, SettingsInputEvent* outEvent);
    bool (*readProviderSnapshot)(void* context, CapabilityId capability,
                                 void* outSnapshot, size_t capacity,
                                 size_t* outSize, uint64_t* outRevision);
    bool (*readCapabilitySnapshot)(void* context, CapabilityId capability,
                                   CapabilitySnapshot* outSnapshot);
    bool (*submitOperation)(void* context, const OperationRequest* request);
    bool (*pollOperationResult)(void* context, OperationResult* outResult);
    bool (*present)(void* context, const SettingsPresentationCore* presentation);
};

inline bool validSettingsPresentationAdapter(const SettingsPresentationAdapterV1* adapter)
{
    return adapter &&
        adapter->structureSize >= sizeof(SettingsPresentationAdapterV1) &&
        adapter->version == kSettingsPresentationAdapterVersion &&
        adapter->getWindowConstraints && adapter->pollInput &&
        adapter->readProviderSnapshot && adapter->readCapabilitySnapshot && adapter->submitOperation &&
        adapter->pollOperationResult && adapter->present;
}

} // namespace settings
} // namespace apps
} // namespace gxos
