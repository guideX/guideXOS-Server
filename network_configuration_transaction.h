#pragma once

#include "network_settings_contract.h"

namespace gxos {
namespace network_settings {

// Small callback-based transaction runner shared by the kernel implementation
// and deterministic fault-injection tests. State is a caller-owned bounded
// value object; the runner never allocates or retains pointers.
template <typename State>
struct ConfigurationTransactionOperations {
    void* context{nullptr};
    bool (*capture)(void* context, State* output){nullptr};
    bool (*apply)(void* context, const NetworkConfigurationCandidate& candidate){nullptr};
    bool (*verify)(void* context, const NetworkConfigurationCandidate& candidate){nullptr};
    bool (*rollback)(void* context, const State& previous){nullptr};
    bool (*verifyRollback)(void* context, const State& previous){nullptr};
};

template <typename State>
ConfigurationTransactionResult runConfigurationTransaction(
    const NetworkConfigurationCandidate& candidate,
    const ConfigurationTransactionOperations<State>& operations)
{
    ConfigurationTransactionResult result{};
    const ConfigurationField invalid = validateCandidate(candidate);
    if (invalid != ConfigurationField::None) {
        result.outcome = TransactionOutcome::ValidationFailed;
        result.field = invalid;
        return result;
    }

    // The current DHCP owner has no nonblocking start/cancel operation and
    // cannot restore an in-flight lease lifecycle. Keep it explicitly
    // unsupported until that owner gains transaction-aware lifecycle hooks.
    if (candidate.mode != NetworkMode::Static ||
        candidate.dnsMode != DnsMode::Manual) {
        result.outcome = TransactionOutcome::Unsupported;
        return result;
    }
    if (!operations.capture || !operations.apply || !operations.verify ||
        !operations.rollback || !operations.verifyRollback) {
        result.outcome = TransactionOutcome::CaptureFailed;
        return result;
    }

    State previous{};
    if (!operations.capture(operations.context, &previous)) {
        result.outcome = TransactionOutcome::CaptureFailed;
        return result;
    }

    if (!operations.apply(operations.context, candidate)) {
        const bool restored = operations.rollback(operations.context, previous) &&
            operations.verifyRollback(operations.context, previous);
        result.outcome = restored ? TransactionOutcome::ApplyFailedRolledBack :
            TransactionOutcome::ApplyFailedRollbackFailed;
        return result;
    }

    if (!operations.verify(operations.context, candidate)) {
        const bool restored = operations.rollback(operations.context, previous) &&
            operations.verifyRollback(operations.context, previous);
        result.outcome = restored ? TransactionOutcome::VerifyFailedRolledBack :
            TransactionOutcome::VerifyFailedRollbackFailed;
        return result;
    }

    result.outcome = TransactionOutcome::Success;
    return result;
}

} // namespace network_settings
} // namespace gxos
