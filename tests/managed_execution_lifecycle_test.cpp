#include "kernel/managed_execution.h"
#include "kernel/authentication.h"

#include <iostream>
#include <string.h>

using namespace kernel;

namespace {

struct TestCrypto {
    uint32_t randomCounter{1u};
    uint64_t now{0u};
};

int checks = 0;
int failures = 0;

void check(bool condition, const char* description)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << "\n";
    }
}

bool randomBytes(void* context, uint8_t* output, size_t byteCount)
{
    TestCrypto& crypto = *static_cast<TestCrypto*>(context);
    if (!output && byteCount != 0u) return false;
    for (size_t i = 0u; i < byteCount; ++i) {
        crypto.randomCounter = crypto.randomCounter * 1664525u + 1013904223u;
        output[i] = static_cast<uint8_t>(crypto.randomCounter >> 24u);
    }
    return true;
}

bool testOnlyDerive(void*, authentication::Algorithm algorithm,
                    uint32_t iterations, const uint8_t* password,
                    size_t passwordBytes, const uint8_t* salt, size_t saltBytes,
                    uint8_t* verifier, size_t verifierBytes)
{
    if (algorithm != authentication::Algorithm::Pbkdf2HmacSha256 ||
        iterations < authentication::kPbkdf2HmacSha256Iterations ||
        !password || !salt || !verifier || verifierBytes != 32u) return false;
    uint32_t state = 2166136261u ^ iterations;
    for (size_t i = 0u; i < passwordBytes; ++i)
        state = (state ^ password[i]) * 16777619u;
    for (size_t i = 0u; i < saltBytes; ++i)
        state = (state ^ salt[i]) * 16777619u;
    for (size_t i = 0u; i < verifierBytes; ++i) {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        verifier[i] = static_cast<uint8_t>(state >> ((i & 3u) * 8u));
    }
    return true;
}

uint64_t clockTicks(void* context)
{
    return static_cast<TestCrypto*>(context)->now;
}

size_t textLength(const char* text)
{
    size_t length = 0u;
    while (text[length] != '\0') ++length;
    return length;
}

} // namespace

int main()
{
    const char password[] = "correct-horse-battery-staple";
    TestCrypto testCrypto;
    authentication::CryptoProvider crypto{};
    crypto.context = &testCrypto;
    crypto.randomBytes = randomBytes;
    crypto.deriveVerifier = testOnlyDerive;
    crypto.clockTicks = clockTicks;

    identity::Runtime identities;
    check(identities.initialize(nullptr) == identity::Result::Ok,
        "identity registry starts in the supported volatile-storage mode");
    authentication::Runtime auth;
    check(auth.initialize(&identities, nullptr, &crypto, false) ==
            authentication::Result::Ok,
        "trusted authentication runtime initializes for lifecycle tests");
    authentication::SessionReference session{};
    check(auth.bootstrapAdministrator("admin", "Administrator", password,
            textLength(password), &session) == authentication::Result::OkVolatile,
        "test administrator session is established through the authentication runtime");
    identity::UserRecordV1 admin{};
    check(identities.findByAccountName("admin", &admin) == identity::Result::Ok,
        "administrator identity is available from the authoritative registry");
    identity::UserId standardId = identity::kInvalidUserId;
    check(auth.createAccount("standard", "Standard User", &standardId) ==
            authentication::Result::OkVolatile &&
        auth.enrollCredential(standardId, password, textLength(password)) ==
            authentication::Result::OkVolatile,
        "administrator creates and enrolls a distinct standard account");
    check(auth.signOutCurrent() == authentication::Result::Ok &&
        auth.authenticate("standard", password, textLength(password), &session) ==
            authentication::Result::Ok,
        "standard user signs in through the credential verifier");

    managed_execution::Runtime executions(&auth);
    managed_execution::Handle standardHandle{};
    check(executions.beginNativeElf("com.guidexos.lifecycle-test", &standardHandle) ==
            managed_execution::Result::Ok && executions.activeCount() == 1u,
        "validated Native ELF boundary creates an execution instance without claiming a PID");
    managed_execution::SnapshotV1 snapshot{};
    check(executions.query(standardHandle, &snapshot) == managed_execution::Result::Ok &&
        snapshot.state == managed_execution::State::Created &&
        snapshot.kind == managed_execution::Kind::NativeElfManagedInstance &&
        snapshot.ownerKind == managed_execution::OwnerKind::AuthenticatedStandardUser &&
        snapshot.ownerUserId == standardId && snapshot.sessionGeneration != 0u &&
        strcmp(snapshot.appId, "com.guidexos.lifecycle-test") == 0,
        "creation captures canonical app identity and trusted session provenance");
    managed_execution::Handle concurrentHandle{};
    check(executions.markRunning(standardHandle) == managed_execution::Result::Ok &&
        executions.activeCount() == 1u &&
        executions.beginNativeElf("com.guidexos.concurrent", &concurrentHandle) ==
            managed_execution::Result::CapacityExhausted,
        "synchronous runtime refuses a second active execution record");
    check(executions.markRunning(standardHandle) ==
            managed_execution::Result::InvalidTransition,
        "lifecycle rejects duplicate Running transition");
    check(executions.requestTermination(standardHandle) ==
            managed_execution::Result::TerminationUnsupported &&
        executions.query(standardHandle, &snapshot) == managed_execution::Result::Ok &&
        snapshot.state == managed_execution::State::Running,
        "authorized own-instance termination reports unsupported without claiming a stop");
    check(executions.complete(standardHandle, false, true, 0u, 0u, 0u, true) ==
            managed_execution::Result::Ok && executions.activeCount() == 0u &&
        executions.query(standardHandle, &snapshot) == managed_execution::Result::Ok &&
        snapshot.state == managed_execution::State::Exited &&
        snapshot.hasExitStatus != 0u && snapshot.exitStatus == 0u &&
        snapshot.resourcesReleased != 0u,
        "real completion transition records the exit result after resource cleanup");
    check(executions.complete(standardHandle, false, true, 0u, 0u, 0u, true) ==
            managed_execution::Result::InvalidTransition,
        "terminal instances cannot be completed twice");

    managed_execution::Handle faultHandle{};
    check(executions.beginNativeElf("com.guidexos.fault-test", &faultHandle) ==
            managed_execution::Result::Ok &&
        executions.markRunning(faultHandle) == managed_execution::Result::Ok &&
        executions.complete(faultHandle, true, false, 0u, 6u, 0u, true) ==
            managed_execution::Result::Ok &&
        executions.query(faultHandle, &snapshot) == managed_execution::Result::Ok &&
        snapshot.state == managed_execution::State::Faulted &&
        snapshot.faultObserved != 0u && snapshot.faultVector == 6u &&
        snapshot.hasExitStatus == 0u && snapshot.resourcesReleased != 0u,
        "observed application fault is distinct from an ordinary exit");

    check(auth.signOutCurrent() == authentication::Result::Ok &&
        auth.authenticate("admin", password, textLength(password), &session) ==
            authentication::Result::Ok,
        "administrator session becomes the current query authority");
    size_t requiredCapacity = 0u;
    check(executions.query(standardHandle, &snapshot) == managed_execution::Result::Ok &&
        snapshot.ownerUserId == standardId &&
        executions.enumerate(nullptr, 0u, &requiredCapacity) ==
            managed_execution::Result::BufferTooSmall && requiredCapacity == 2u,
        "administrator can query another owner's record and enumeration reports its bounded size");
    check(auth.setAccountRole(standardId, identity::AccountRole::Administrator) ==
            authentication::Result::OkVolatile &&
        executions.query(standardHandle, &snapshot) == managed_execution::Result::Ok &&
        snapshot.ownerKind == managed_execution::OwnerKind::AuthenticatedStandardUser &&
        snapshot.ownerUserId == standardId &&
        auth.setAccountRole(standardId, identity::AccountRole::StandardUser) ==
            authentication::Result::OkVolatile &&
        auth.setAccountStatus(standardId, identity::AccountStatus::Disabled) ==
            authentication::Result::OkVolatile &&
        auth.setAccountStatus(standardId, identity::AccountStatus::Active) ==
            authentication::Result::OkVolatile &&
        executions.query(standardHandle, &snapshot) == managed_execution::Result::Ok &&
        snapshot.ownerKind == managed_execution::OwnerKind::AuthenticatedStandardUser &&
        snapshot.ownerUserId == standardId,
        "role and account-status changes leave historical ownership attribution unchanged");

    size_t adminVisibleCount = 0u;
    managed_execution::SnapshotV1 visible[managed_execution::kMaximumRecords]{};
    check(executions.enumerate(visible, managed_execution::kMaximumRecords,
            &adminVisibleCount) == managed_execution::Result::Ok &&
        adminVisibleCount == 2u,
        "administrator enumeration returns snapshots for both authenticated owners");
    managed_execution::Handle administratorHandle{};
    check(executions.beginNativeElf("com.guidexos.admin-test", &administratorHandle) ==
            managed_execution::Result::Ok &&
        executions.markRunning(administratorHandle) == managed_execution::Result::Ok &&
        executions.requestTermination(administratorHandle) ==
            managed_execution::Result::TerminationUnsupported &&
        executions.complete(administratorHandle, false, true, 7u, 0u, 0u, true) ==
            managed_execution::Result::Ok,
        "administrator-owned execution follows the same truthful lifecycle and exit-status path");

    check(auth.signOutCurrent() == authentication::Result::Ok &&
        auth.authenticate("standard", password, textLength(password), &session) ==
            authentication::Result::Ok &&
        executions.query(administratorHandle, &snapshot) ==
            managed_execution::Result::Unauthorized &&
        executions.requestTermination(administratorHandle) ==
            managed_execution::Result::Unauthorized,
        "standard user cannot query or request termination of another account's instance");
    size_t standardVisibleCount = 0u;
    check(executions.enumerate(visible, managed_execution::kMaximumRecords,
            &standardVisibleCount) == managed_execution::Result::Ok &&
        standardVisibleCount == 2u,
        "standard enumeration reveals only its own completed and faulted records");

    auth.invalidateUser(standardId);
    check(executions.query(standardHandle, &snapshot) ==
            managed_execution::Result::Unauthorized,
        "session invalidation removes future process-query authority without rewriting ownership");
    check(auth.authenticate("standard", password, textLength(password), &session) ==
            authentication::Result::Ok,
        "standard account can establish a fresh session generation after invalidation");
    authentication::ExecutionAuthorityV1 freshAuthority{};
    check(auth.queryExecutionAuthority(&freshAuthority) == authentication::Result::Ok &&
        freshAuthority.userId == standardId &&
        freshAuthority.sessionGeneration != snapshot.sessionGeneration,
        "reauthentication issues fresh session provenance");

    check(auth.signOutCurrent() == authentication::Result::Ok,
        "unassigned interactive context is restored for compatibility testing");
    managed_execution::Handle unassignedHandle{};
    check(executions.beginNativeElf("com.guidexos.unassigned-test", &unassignedHandle) ==
            managed_execution::Result::Ok &&
        executions.markRunning(unassignedHandle) == managed_execution::Result::Ok &&
        executions.query(unassignedHandle, &snapshot) == managed_execution::Result::Ok &&
        snapshot.ownerKind == managed_execution::OwnerKind::InteractiveUnassigned &&
        snapshot.ownerUserId == identity::kInvalidUserId &&
        executions.requestTermination(unassignedHandle) ==
            managed_execution::Result::Unauthorized &&
        executions.complete(unassignedHandle, false, true, 0u, 0u, 0u, true) ==
            managed_execution::Result::Ok,
        "unassigned execution remains explicitly unassigned and gains no termination authority");
    check(auth.authenticate("standard", password, textLength(password), &session) ==
            authentication::Result::Ok,
        "standard session can resume after the unassigned compatibility check");

    const uint64_t firstRuntimeId = standardHandle.runtimeId;
    managed_execution::Handle newest{};
    for (uint32_t i = 0u; i < managed_execution::kMaximumRecords + 1u; ++i) {
        check(executions.beginNativeElf("com.guidexos.repeat", &newest) ==
                managed_execution::Result::Ok &&
            executions.markRunning(newest) == managed_execution::Result::Ok &&
            executions.complete(newest, false, true, i, 0u, 0u, true) ==
                managed_execution::Result::Ok,
            "repeated execution launches complete without corrupting retained records");
    }
    check(newest.runtimeId > firstRuntimeId &&
        executions.query(standardHandle, &snapshot) ==
            managed_execution::Result::StaleHandle &&
        executions.query(newest, &snapshot) == managed_execution::Result::Ok &&
        snapshot.handle.generation != 0u && snapshot.state == managed_execution::State::Exited,
        "bounded terminal-record reuse rejects stale generation-safe handles");

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "Managed execution lifecycle: " << checks << "/" << checks
              << " checks passed\n";
    return 0;
}
