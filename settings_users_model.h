#pragma once

#include "settings_center_model.h"

#include <string>

namespace gxos {
namespace apps {
namespace settings {

// This is presentation state for the Users page, not an OS identity contract.
enum class UsersInfoState : unsigned char {
    NotDefined,
    NotSupported,
    Unavailable,
    Available,
    Present,
    Absent,
    Assigned,
    Unassigned,
    NotImplemented,
    Persistent,
    Volatile,
    ReadOnly,
    Corrupt,
    Unsupported
};

inline const char* usersInfoStateText(UsersInfoState state)
{
    switch (state) {
    case UsersInfoState::NotDefined: return "Not defined";
    case UsersInfoState::NotSupported: return "Not supported";
    case UsersInfoState::Available: return "Available";
    case UsersInfoState::Present: return "Present";
    case UsersInfoState::Absent: return "Absent";
    case UsersInfoState::Assigned: return "Assigned";
    case UsersInfoState::Unassigned: return "Unassigned";
    case UsersInfoState::NotImplemented: return "Not implemented";
    case UsersInfoState::Persistent: return "Persistent";
    case UsersInfoState::Volatile: return "In memory only";
    case UsersInfoState::ReadOnly: return "Read-only";
    case UsersInfoState::Corrupt: return "Corrupt";
    case UsersInfoState::Unsupported: return "Unsupported version";
    case UsersInfoState::Unavailable: default: return "Unavailable";
    }
}

enum class UsersPersistenceInfo : unsigned char {
    Persistent,
    Volatile,
    ReadOnly,
    Unavailable,
    Corrupt,
    Unsupported
};

// Value-only presentation input. It can be populated only from an actual
// runtime provider; hosted Settings without that provider stays unavailable.
struct UsersIdentityInfo {
    bool available{false};
    bool accountCountAvailable{false};
    unsigned accountCount{0};
    bool interactiveContextAvailable{false};
    bool authenticatedHumanAssigned{false};
    bool authenticationAvailable{false};
    bool sessionManagerAvailable{false};
    bool signOutAvailable{false};
    bool currentRoleAvailable{false};
    unsigned currentRole{0};
    std::string currentAccountName;
    std::string currentDisplayName;
    UsersPersistenceInfo persistence{UsersPersistenceInfo::Unavailable};
    bool allRecordsDurable{false};
};

struct UsersPageState {
    UsersInfoState accountModel{UsersInfoState::Unavailable};
    UsersInfoState accountRecords{UsersInfoState::Unavailable};
    UsersInfoState currentIdentity{UsersInfoState::Unavailable};
    UsersInfoState sessionIdentity{UsersInfoState::Unavailable};
    UsersInfoState authentication{UsersInfoState::Unavailable};
    UsersInfoState profiles{UsersInfoState::NotImplemented};
    UsersInfoState sessionManager{UsersInfoState::Unavailable};
    UsersInfoState sessionActions{UsersInfoState::Unavailable};
    UsersInfoState userPermissions{UsersInfoState::NotImplemented};
    UsersInfoState fileOwnership{UsersInfoState::NotImplemented};
    UsersInfoState userGrantedAppPermissions{UsersInfoState::NotImplemented};
    UsersInfoState persistence{UsersInfoState::Unavailable};
    UsersInfoState servicePeerAuthentication{UsersInfoState::Unavailable};
    bool currentRoleAvailable{false};
    unsigned currentRole{0};
    std::string currentAccountName;
    std::string currentDisplayName;
    std::string hostedRuntime{"Unavailable"};
};

inline std::string formatUsersHostedRuntime(const std::string& runtime)
{
    const std::string trimmed = trimAscii(runtime);
    if (trimmed.empty() || lowerAscii(trimmed) == "hosted runtime details unavailable")
        return "Unavailable";
    return boundedSettingValue(trimmed, 44);
}

inline UsersPageState buildUsersPageState(
    const std::string& hostedRuntime,
    const UsersIdentityInfo* identityProvider = nullptr)
{
    UsersPageState state;
    state.hostedRuntime = formatUsersHostedRuntime(hostedRuntime);
    if (!identityProvider || !identityProvider->available) return state;

    state.accountModel = UsersInfoState::Available;
    state.accountRecords = !identityProvider->accountCountAvailable
        ? UsersInfoState::Unavailable
        : (identityProvider->accountCount == 0u
            ? UsersInfoState::Absent : UsersInfoState::Present);
    state.currentIdentity = !identityProvider->interactiveContextAvailable
        ? UsersInfoState::Unavailable
        : (identityProvider->authenticatedHumanAssigned
            ? UsersInfoState::Assigned : UsersInfoState::Unassigned);
    state.sessionIdentity = state.currentIdentity;
    state.authentication = identityProvider->authenticationAvailable
        ? UsersInfoState::Available : UsersInfoState::Unavailable;
    state.sessionManager = identityProvider->sessionManagerAvailable
        ? UsersInfoState::Available : UsersInfoState::Unavailable;
    state.sessionActions = identityProvider->signOutAvailable &&
        identityProvider->authenticatedHumanAssigned
        ? UsersInfoState::Available : UsersInfoState::Unavailable;
    state.currentRoleAvailable = identityProvider->currentRoleAvailable &&
        identityProvider->authenticatedHumanAssigned;
    state.currentRole = state.currentRoleAvailable ? identityProvider->currentRole : 0u;
    if (identityProvider->authenticatedHumanAssigned) {
        state.currentAccountName = boundedSettingValue(identityProvider->currentAccountName, 48);
        state.currentDisplayName = boundedSettingValue(identityProvider->currentDisplayName, 48);
    }
    switch (identityProvider->persistence) {
    case UsersPersistenceInfo::Persistent:
        state.persistence = identityProvider->allRecordsDurable
            ? UsersInfoState::Persistent : UsersInfoState::Volatile;
        break;
    case UsersPersistenceInfo::Volatile: state.persistence = UsersInfoState::Volatile; break;
    case UsersPersistenceInfo::ReadOnly: state.persistence = UsersInfoState::ReadOnly; break;
    case UsersPersistenceInfo::Corrupt: state.persistence = UsersInfoState::Corrupt; break;
    case UsersPersistenceInfo::Unsupported: state.persistence = UsersInfoState::Unsupported; break;
    case UsersPersistenceInfo::Unavailable: default:
        state.persistence = identityProvider->allRecordsDurable
            ? UsersInfoState::Unavailable : UsersInfoState::Volatile;
        break;
    }
    return state;
}

inline int usersPageRowCount(TargetId target)
{
    switch (target) {
    case TargetId::Page: return 8;
    case TargetId::UsersSession: return 8;
    case TargetId::UsersSecurity: return 7;
    default: return 0;
    }
}

} // namespace settings
} // namespace apps
} // namespace gxos
