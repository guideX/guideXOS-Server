#pragma once

#include "settings_center_model.h"

#include <string>

namespace gxos {
namespace apps {
namespace settings {

// This is presentation state for the Users page, not an OS identity contract.
// The current system has no human account/session owner to query.
enum class UsersInfoState : unsigned char {
    NotDefined,
    NotSupported,
    Unavailable
};

inline const char* usersInfoStateText(UsersInfoState state)
{
    switch (state) {
    case UsersInfoState::NotDefined: return "Not defined";
    case UsersInfoState::NotSupported: return "Not supported";
    case UsersInfoState::Unavailable: default: return "Unavailable";
    }
}

struct UsersPageState {
    UsersInfoState accountModel{UsersInfoState::NotSupported};
    UsersInfoState currentIdentity{UsersInfoState::NotDefined};
    UsersInfoState sessionIdentity{UsersInfoState::NotDefined};
    UsersInfoState sessionManager{UsersInfoState::NotSupported};
    UsersInfoState authentication{UsersInfoState::NotSupported};
    UsersInfoState profiles{UsersInfoState::NotSupported};
    UsersInfoState sessionActions{UsersInfoState::NotSupported};
    UsersInfoState userPermissions{UsersInfoState::NotSupported};
    UsersInfoState fileOwnership{UsersInfoState::NotSupported};
    UsersInfoState userGrantedAppPermissions{UsersInfoState::NotSupported};
    UsersInfoState servicePeerAuthentication{UsersInfoState::Unavailable};
    std::string hostedRuntime{"Unavailable"};
};

inline std::string formatUsersHostedRuntime(const std::string& runtime)
{
    const std::string trimmed = trimAscii(runtime);
    if (trimmed.empty() || lowerAscii(trimmed) == "hosted runtime details unavailable")
        return "Unavailable";
    return boundedSettingValue(trimmed, 44);
}

inline UsersPageState buildUsersPageState(const std::string& hostedRuntime)
{
    UsersPageState state;
    state.hostedRuntime = formatUsersHostedRuntime(hostedRuntime);
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
