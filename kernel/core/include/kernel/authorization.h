#ifndef KERNEL_AUTHORIZATION_H
#define KERNEL_AUTHORIZATION_H

#include "kernel/identity.h"

namespace kernel {
namespace authorization {

enum class Operation : uint16_t {
    AccountCreate = 1,
    AccountSetEnabled,
    AccountDelete,
    AccountChangeRole,
    AccountResetCredential,
    AccountModifySecuritySettings,
    AccountChangeOwnCredential,
    AccountEnrollCredential,
    ApplicationInstallOrRemove,
    ApplicationModifySystemSettings,
    ApplicationChangeTrustPolicy,
    ApplicationRegisterSystemWide,
    ProcessTerminateOther,
    ProcessPrivilegedManagement,
    ProcessAccessProtectedControls,
    FilesystemModifyProtectedConfiguration,
    FilesystemModifyIdentityCredentialData,
    FilesystemAdministrativeChange
};

enum class ContextKind : uint8_t {
    TrustedKernelSystem,
    AuthenticatedAdministrator,
    AuthenticatedStandardUser,
    InteractiveUnassigned,
    InvalidOrRevokedSession,
    AccountDisabled,
    AccountDeleted,
    Unsupported
};

enum class Decision : uint8_t {
    Allowed,
    Denied,
    NoAuthenticatedIdentity,
    InvalidSession,
    AccountDisabledOrDeleted,
    InsufficientPrivileges,
    UnsupportedContext
};

enum class Reason : uint8_t {
    None,
    SystemAuthority,
    AdministratorRole,
    SelfService,
    NoSession,
    SessionRevoked,
    AccountDisabled,
    AccountDeleted,
    RoleInsufficient,
    TargetMismatch,
    UnsupportedOperation,
    ContextUnavailable
};

struct SecurityContext {
    ContextKind kind;
    identity::UserId subject;
};

struct DecisionResult {
    Decision decision;
    Reason reason;
    Operation operation;
    identity::UserId subject;

    bool allowed() const { return decision == Decision::Allowed; }
};

struct AuditSnapshotV1 {
    uint16_t version;
    uint16_t reserved;
    uint64_t decisions;
    uint64_t denials;
    Operation lastOperation;
    Decision lastDecision;
    Reason lastReason;
};

inline bool validOperation(Operation operation)
{
    switch (operation) {
        case Operation::AccountCreate:
        case Operation::AccountSetEnabled:
        case Operation::AccountDelete:
        case Operation::AccountChangeRole:
        case Operation::AccountResetCredential:
        case Operation::AccountModifySecuritySettings:
        case Operation::AccountChangeOwnCredential:
        case Operation::AccountEnrollCredential:
        case Operation::ApplicationInstallOrRemove:
        case Operation::ApplicationModifySystemSettings:
        case Operation::ApplicationChangeTrustPolicy:
        case Operation::ApplicationRegisterSystemWide:
        case Operation::ProcessTerminateOther:
        case Operation::ProcessPrivilegedManagement:
        case Operation::ProcessAccessProtectedControls:
        case Operation::FilesystemModifyProtectedConfiguration:
        case Operation::FilesystemModifyIdentityCredentialData:
        case Operation::FilesystemAdministrativeChange:
            return true;
        default:
            return false;
    }
}

// Policy evaluation has no authority to mint a context. Production callers
// must use authentication::Runtime::authorizeCurrent(), which builds the
// context from the session manager and authoritative identity registry.
inline DecisionResult evaluate(const SecurityContext& context,
                               Operation operation,
                               identity::UserId target = identity::kInvalidUserId)
{
    DecisionResult result{Decision::Denied, Reason::UnsupportedOperation,
                          operation, context.subject};
    if (!validOperation(operation)) {
        result.decision = Decision::UnsupportedContext;
        return result;
    }

    switch (context.kind) {
        case ContextKind::TrustedKernelSystem:
            result.decision = Decision::Allowed;
            result.reason = Reason::SystemAuthority;
            return result;
        case ContextKind::AuthenticatedAdministrator:
            result.decision = Decision::Allowed;
            result.reason = Reason::AdministratorRole;
            return result;
        case ContextKind::AuthenticatedStandardUser:
            if ((operation == Operation::AccountChangeOwnCredential ||
                 operation == Operation::AccountEnrollCredential) &&
                context.subject != identity::kInvalidUserId &&
                context.subject == target) {
                result.decision = Decision::Allowed;
                result.reason = Reason::SelfService;
                return result;
            }
            result.decision = Decision::InsufficientPrivileges;
            result.reason = (operation == Operation::AccountChangeOwnCredential ||
                operation == Operation::AccountEnrollCredential)
                ? Reason::TargetMismatch : Reason::RoleInsufficient;
            return result;
        case ContextKind::InteractiveUnassigned:
            result.decision = Decision::NoAuthenticatedIdentity;
            result.reason = Reason::NoSession;
            return result;
        case ContextKind::InvalidOrRevokedSession:
            result.decision = Decision::InvalidSession;
            result.reason = Reason::SessionRevoked;
            return result;
        case ContextKind::AccountDisabled:
        case ContextKind::AccountDeleted:
            result.decision = Decision::AccountDisabledOrDeleted;
            result.reason = context.kind == ContextKind::AccountDisabled
                ? Reason::AccountDisabled : Reason::AccountDeleted;
            return result;
        case ContextKind::Unsupported:
        default:
            result.decision = Decision::UnsupportedContext;
            result.reason = Reason::ContextUnavailable;
            return result;
    }
}

inline const char* decisionName(Decision decision)
{
    switch (decision) {
        case Decision::Allowed: return "Allowed";
        case Decision::Denied: return "Denied";
        case Decision::NoAuthenticatedIdentity: return "NoAuthenticatedIdentity";
        case Decision::InvalidSession: return "InvalidSession";
        case Decision::AccountDisabledOrDeleted: return "AccountDisabledOrDeleted";
        case Decision::InsufficientPrivileges: return "InsufficientPrivileges";
        case Decision::UnsupportedContext: return "UnsupportedContext";
        default: return "UnknownDecision";
    }
}

} // namespace authorization
} // namespace kernel

#endif
