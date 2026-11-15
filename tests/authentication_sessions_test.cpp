#include "kernel/authentication.h"

#include <iostream>
#include <string.h>

using namespace kernel;

namespace {

struct TestStore {
    bool available{true};
    bool writable{true};
    bool failNextWrite{false};
    bool identityPresent[2]{};
    size_t identitySize[2]{};
    bool credentialPresent[2]{};
    size_t credentialSize[2]{};
    uint8_t identityBytes[2][identity::kMaximumIdentityStoreBytes]{};
    uint8_t credentialBytes[2][32u + identity::kMaximumUsers * 64u + 4u]{};
};

struct TestCrypto {
    uint32_t randomCounter{1u};
    uint64_t now{0u};
    bool entropyReady{true};
    bool deriveReady{true};
};

int failures = 0;
int checks = 0;

void check(bool condition, const char* description)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << "\n";
    }
}

identity::StoreReadResult readIdentity(void* context, uint8_t slot, uint8_t* output,
                                       size_t capacity, size_t* outBytes)
{
    TestStore& store = *static_cast<TestStore*>(context);
    if (outBytes) *outBytes = 0u;
    if (!store.available) return identity::StoreReadResult::Unavailable;
    if (slot > 1u || !output || !outBytes) return identity::StoreReadResult::IoError;
    if (!store.identityPresent[slot]) return identity::StoreReadResult::Missing;
    if (store.identitySize[slot] > capacity || store.identitySize[slot] > sizeof(store.identityBytes[slot]))
        return identity::StoreReadResult::Corrupt;
    memcpy(output, store.identityBytes[slot], store.identitySize[slot]);
    *outBytes = store.identitySize[slot];
    return identity::StoreReadResult::Ok;
}

identity::StoreWriteResult writeIdentity(void* context, uint8_t slot,
                                         const uint8_t* input, size_t inputBytes)
{
    TestStore& store = *static_cast<TestStore*>(context);
    if (!store.available) return identity::StoreWriteResult::Unavailable;
    if (!store.writable) return identity::StoreWriteResult::ReadOnly;
    if (slot > 1u || !input || inputBytes > sizeof(store.identityBytes[slot]))
        return identity::StoreWriteResult::IoError;
    if (store.failNextWrite) {
        store.failNextWrite = false;
        memcpy(store.identityBytes[slot], input, inputBytes / 2u);
        store.identitySize[slot] = inputBytes / 2u;
        store.identityPresent[slot] = true;
        return identity::StoreWriteResult::IoError;
    }
    memcpy(store.identityBytes[slot], input, inputBytes);
    store.identitySize[slot] = inputBytes;
    store.identityPresent[slot] = true;
    return identity::StoreWriteResult::Ok;
}

authentication::StoreReadResult readCredentials(void* context, uint8_t slot, uint8_t* output,
                                size_t capacity, size_t* outBytes)
{
    TestStore& store = *static_cast<TestStore*>(context);
    if (outBytes) *outBytes = 0u;
    if (!store.available) return authentication::StoreReadResult::Unavailable;
    if (slot > 1u || !output || !outBytes) return authentication::StoreReadResult::IoError;
    if (!store.credentialPresent[slot]) return authentication::StoreReadResult::Missing;
    if (store.credentialSize[slot] > capacity || store.credentialSize[slot] > sizeof(store.credentialBytes[slot]))
        return authentication::StoreReadResult::Corrupt;
    memcpy(output, store.credentialBytes[slot], store.credentialSize[slot]);
    *outBytes = store.credentialSize[slot];
    return authentication::StoreReadResult::Ok;
}

authentication::StoreWriteResult writeCredentials(void* context, uint8_t slot,
                                  const uint8_t* input, size_t inputBytes)
{
    TestStore& store = *static_cast<TestStore*>(context);
    if (!store.available) return authentication::StoreWriteResult::Unavailable;
    if (!store.writable) return authentication::StoreWriteResult::ReadOnly;
    if (slot > 1u || !input || inputBytes > sizeof(store.credentialBytes[slot]))
        return authentication::StoreWriteResult::IoError;
    if (store.failNextWrite) {
        store.failNextWrite = false;
        memcpy(store.credentialBytes[slot], input, inputBytes / 2u);
        store.credentialSize[slot] = inputBytes / 2u;
        store.credentialPresent[slot] = true;
        return authentication::StoreWriteResult::IoError;
    }
    memcpy(store.credentialBytes[slot], input, inputBytes);
    store.credentialSize[slot] = inputBytes;
    store.credentialPresent[slot] = true;
    return authentication::StoreWriteResult::Ok;
}

identity::StorageAdapter identityAdapter(TestStore& store)
{
    identity::StorageAdapter adapter{};
    adapter.context = &store;
    adapter.readSlot = readIdentity;
    adapter.writeSlot = writeIdentity;
    adapter.available = 1u;
    adapter.writable = 1u;
    return adapter;
}

authentication::StorageAdapter credentialAdapter(TestStore& store)
{
    authentication::StorageAdapter adapter{};
    adapter.context = &store;
    adapter.readSlot = readCredentials;
    adapter.writeSlot = writeCredentials;
    adapter.available = 1u;
    adapter.writable = 1u;
    return adapter;
}

bool randomBytes(void* context, uint8_t* output, size_t byteCount)
{
    TestCrypto& crypto = *static_cast<TestCrypto*>(context);
    if (!crypto.entropyReady || (!output && byteCount != 0u)) return false;
    for (size_t i = 0u; i < byteCount; ++i) {
        crypto.randomCounter = crypto.randomCounter * 1664525u + 1013904223u;
        output[i] = static_cast<uint8_t>(crypto.randomCounter >> 24u);
    }
    return true;
}

bool testOnlyDerive(void* context, authentication::Algorithm algorithm,
                    uint32_t iterations, const uint8_t* password, size_t passwordBytes,
                    const uint8_t* salt, size_t saltBytes,
                    uint8_t* verifier, size_t verifierBytes)
{
    TestCrypto& crypto = *static_cast<TestCrypto*>(context);
    if (!crypto.deriveReady || algorithm != authentication::Algorithm::Pbkdf2HmacSha256 ||
        iterations < authentication::kPbkdf2HmacSha256Iterations ||
        !password || !salt || !verifier || verifierBytes != 32u) return false;
    uint32_t state = 2166136261u ^ iterations;
    for (size_t i = 0u; i < passwordBytes; ++i) state = (state ^ password[i]) * 16777619u;
    for (size_t i = 0u; i < saltBytes; ++i) state = (state ^ salt[i]) * 16777619u;
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

authentication::CryptoProvider cryptoAdapter(TestCrypto& crypto)
{
    authentication::CryptoProvider provider{};
    provider.context = &crypto;
    provider.randomBytes = randomBytes;
    provider.deriveVerifier = testOnlyDerive;
    provider.clockTicks = clockTicks;
    return provider;
}

size_t textLength(const char* text)
{
    size_t size = 0u;
    while (text[size]) ++size;
    return size;
}

} // namespace

int main()
{
    const authorization::Operation adminOperation = authorization::Operation::AccountCreate;
    authorization::SecurityContext policyContext{};
    policyContext.subject = 0x1000u;
    policyContext.kind = authorization::ContextKind::AuthenticatedAdministrator;
    check(authorization::evaluate(policyContext, adminOperation).decision ==
            authorization::Decision::Allowed,
        "policy permits an authenticated administrator for account administration");
    policyContext.kind = authorization::ContextKind::AuthenticatedStandardUser;
    check(authorization::evaluate(policyContext, adminOperation).decision ==
            authorization::Decision::InsufficientPrivileges,
        "policy denies administrator-only operations to a standard user");
    check(authorization::evaluate(policyContext,
            authorization::Operation::AccountChangeOwnCredential, policyContext.subject).decision ==
            authorization::Decision::Allowed &&
        authorization::evaluate(policyContext,
            authorization::Operation::AccountChangeOwnCredential, policyContext.subject + 1u).decision ==
            authorization::Decision::InsufficientPrivileges,
        "self-service credential policy requires the authenticated target identity");
    policyContext.kind = authorization::ContextKind::InteractiveUnassigned;
    check(authorization::evaluate(policyContext, adminOperation).decision ==
            authorization::Decision::NoAuthenticatedIdentity,
        "unassigned interactive execution receives no administrator authority");
    policyContext.kind = authorization::ContextKind::InvalidOrRevokedSession;
    check(authorization::evaluate(policyContext, adminOperation).decision ==
            authorization::Decision::InvalidSession,
        "revoked session context is denied explicitly");
    policyContext.kind = authorization::ContextKind::AccountDisabled;
    check(authorization::evaluate(policyContext, adminOperation).decision ==
            authorization::Decision::AccountDisabledOrDeleted,
        "disabled account context is denied explicitly");
    policyContext.kind = authorization::ContextKind::AccountDeleted;
    check(authorization::evaluate(policyContext, adminOperation).decision ==
            authorization::Decision::AccountDisabledOrDeleted,
        "deleted account context is denied explicitly");
    policyContext.kind = authorization::ContextKind::Unsupported;
    check(authorization::evaluate(policyContext, adminOperation).decision ==
            authorization::Decision::UnsupportedContext &&
        authorization::evaluate(policyContext,
            static_cast<authorization::Operation>(0xFFFFu)).decision ==
            authorization::Decision::UnsupportedContext,
        "unsupported contexts and operation identifiers fail closed");

    TestStore store;
    TestCrypto testCrypto;
    identity::Runtime identities;
    identity::StorageAdapter identityStorage = identityAdapter(store);
    check(identities.initialize(&identityStorage) == identity::Result::Ok,
        "identity registry loads an empty persistent dual-slot store");
    authentication::Runtime auth;
    authentication::StorageAdapter credentialStorage = credentialAdapter(store);
    authentication::CryptoProvider crypto = cryptoAdapter(testCrypto);
    check(auth.initialize(&identities, &credentialStorage, &crypto, true) ==
            authentication::Result::Ok,
        "native authentication runtime loads its credential store and binds current-session context");
    authentication::Runtime secondAuth;
    check(secondAuth.initialize(&identities, &credentialStorage, &crypto, true) ==
            authentication::Result::Unauthorized &&
        identities.provision("second-owner", "Second Owner", nullptr) ==
            identity::Result::Unauthorized,
        "a second authentication runtime cannot take ownership of a bound identity registry");

    const char initialPassword[] = "correct-horse-battery-staple";
    authentication::SessionReference reference{};
    check(auth.bootstrapAdministrator("admin", "System Administrator", initialPassword,
            textLength(initialPassword), &reference) == authentication::Result::OkPersistent,
        "fresh local bootstrap creates a credentialed administrator and opens a session");
    identity::UserRecordV1 admin{};
    check(identities.findByAccountName("ADMIN", &admin) == identity::Result::Ok &&
        admin.role == identity::AccountRole::Administrator && auth.credentialEligible(admin.id),
        "administrator role and credential eligibility are associated with the stable user ID");
    authentication::SessionSnapshotV1 session{};
    check(auth.bootstrapAdministrator("second-admin", "Second Administrator",
            initialPassword, textLength(initialPassword), &reference) ==
            authentication::Result::Unauthorized &&
        auth.queryCurrentSession(&session) == authentication::Result::Ok &&
        session.state == authentication::SessionState::Active,
        "bootstrap is unavailable after an administrator account has been provisioned");
    check(auth.queryCurrentSession(&session) == authentication::Result::Ok &&
        session.state == authentication::SessionState::Active && session.userId == admin.id &&
        session.role == identity::AccountRole::Administrator,
        "successful credential verification establishes an active administrator session");
    identity::IdentityContextV1 context{};
    check(identities.queryContext(identity::ExecutionDomain::Interactive, &context) ==
            identity::Result::Ok && context.kind == identity::IdentityKind::AuthenticatedHuman &&
            context.userId == admin.id,
        "identity context reports the manager-owned authenticated user");
    check(auth.authorizeCurrent(authorization::Operation::AccountCreate).decision ==
            authorization::Decision::Allowed,
        "production authorization derives administrator authority from the active session");
    check(auth.initialize(&identities, &credentialStorage, &crypto, true) ==
            authentication::Result::Unauthorized &&
        auth.authorizeCurrent(authorization::Operation::AccountCreate).decision ==
            authorization::Decision::Allowed,
        "initialized authorization runtime cannot be rebound to caller-selected identity state");

    identity::UserId managedId = identity::kInvalidUserId;
    check(auth.createAccount("managed", "Managed Account", &managedId) ==
            authentication::Result::OkPersistent && managedId != identity::kInvalidUserId,
        "administrator can create an ordinary account through the authorization service");
    check(auth.resetCredential(managedId, initialPassword, textLength(initialPassword)) ==
            authentication::Result::OkPersistent && auth.credentialEligible(managedId),
        "administrator can establish a credential for an additional account");
    authorization::AuditSnapshotV1 authorizationAudit{};
    check(auth.queryAuthorizationAudit(&authorizationAudit) == authentication::Result::Ok &&
            authorizationAudit.version == 1u && authorizationAudit.decisions >= 3u &&
            authorizationAudit.denials == 0u &&
            authorizationAudit.lastOperation == authorization::Operation::AccountResetCredential &&
            authorizationAudit.lastDecision == authorization::Decision::Allowed,
        "authorization audit keeps bounded nonsecret decision diagnostics");

    identity::UserId disposableId = identity::kInvalidUserId;
    identity::UserRecordV1 disposableRecord{};
    check(auth.createAccount("disposable", "Disposable Account", &disposableId) ==
            authentication::Result::OkPersistent &&
        auth.setAccountStatus(disposableId, identity::AccountStatus::Disabled) ==
            authentication::Result::OkPersistent &&
        auth.setAccountStatus(disposableId, identity::AccountStatus::Active) ==
            authentication::Result::OkPersistent &&
        auth.eraseAccount(disposableId) == authentication::Result::OkPersistent &&
        identities.findRecord(disposableId, &disposableRecord) == identity::Result::NotFound,
        "administrator can disable, enable, and delete another account without residual records");

    identity::UserId unassignedId = 0u;
    check(auth.signOutCurrent() == authentication::Result::Ok &&
        auth.authorizeCurrent(authorization::Operation::AccountCreate).decision ==
            authorization::Decision::NoAuthenticatedIdentity &&
        identities.initialize(&identityStorage) == identity::Result::Unauthorized &&
        identities.provision("unassigned", "Unassigned", &unassignedId) ==
            identity::Result::Unauthorized && unassignedId == identity::kInvalidUserId &&
        identities.queryContext(identity::ExecutionDomain::Interactive, &context) ==
            identity::Result::Ok && context.kind == identity::IdentityKind::InteractiveUnassigned &&
        auth.signOutCurrent() == authentication::Result::SessionUnavailable,
        "sign-out invalidates the current session and restores explicit compatibility context");

    authentication::SessionReference failedReference{};
    check(auth.authenticate("admin", "incorrect-password", textLength("incorrect-password"),
            &failedReference) == authentication::Result::InvalidCredentials &&
        auth.queryCurrentSession(&session) == authentication::Result::Ok &&
        session.state == authentication::SessionState::None,
        "incorrect credentials never create an interactive session");
    check(auth.authenticate("missing", initialPassword, textLength(initialPassword),
            &failedReference) == authentication::Result::InvalidCredentials,
        "unknown and wrong-password results do not reveal account existence");

    identity::UserId standardId = identity::kInvalidUserId;
    check(auth.authenticate("admin", initialPassword, textLength(initialPassword), &reference) ==
            authentication::Result::Ok &&
        auth.createAccount("standard", "Standard User", &standardId) ==
            authentication::Result::OkPersistent &&
        auth.enrollCredential(standardId, initialPassword, textLength(initialPassword)) ==
            authentication::Result::OkPersistent,
        "authenticated administrator creates a standard account and enrolls its credential durably");
    const uint8_t* cred = store.credentialBytes[1];
    check(store.credentialSize[1] >= 32u + 2u * 64u + 4u &&
        memcmp(cred, "GXOSCRD1", 8u) == 0 &&
        memcmp(cred + 32u + 16u, cred + 32u + 64u + 16u, 16u) != 0,
        "credential serialization stores unique per-account salts and no password field");

    check(auth.signOutCurrent() == authentication::Result::Ok &&
        auth.authenticate("standard", initialPassword, textLength(initialPassword), &reference) ==
            authentication::Result::Ok &&
        auth.queryCurrentSession(&session) == authentication::Result::Ok &&
        session.role == identity::AccountRole::StandardUser,
        "standard-user authentication opens a session without administrator designation");
    check(auth.authorizeCurrent(authorization::Operation::AccountCreate).decision ==
            authorization::Decision::InsufficientPrivileges &&
        auth.authorizeCurrent(authorization::Operation::AccountChangeOwnCredential,
            standardId).decision == authorization::Decision::Allowed &&
        auth.authorizeCurrent(authorization::Operation::AccountChangeOwnCredential,
            managedId).decision == authorization::Decision::InsufficientPrivileges,
        "standard session permits only target-matching credential self-service");
    identity::UserId forgedId = 0u;
    check(auth.createAccount("forged", "Forged Account", &forgedId) ==
            authentication::Result::Unauthorized && forgedId == identity::kInvalidUserId &&
        identities.provision("registry-bypass", "Registry Bypass", &forgedId) ==
            identity::Result::Unauthorized && forgedId == identity::kInvalidUserId &&
        identities.setRole(standardId, identity::AccountRole::Administrator) ==
            identity::Result::Unauthorized &&
        identities.setStatus(managedId, identity::AccountStatus::Disabled) ==
            identity::Result::Unauthorized &&
        identities.erase(managedId) == identity::Result::Unauthorized &&
        auth.resetCredential(managedId, "replacement-password-123",
            textLength("replacement-password-123")) == authentication::Result::Unauthorized &&
        auth.setAccountRole(standardId, identity::AccountRole::Administrator) ==
            authentication::Result::Unauthorized &&
        auth.setAccountStatus(managedId, identity::AccountStatus::Disabled) ==
            authentication::Result::Unauthorized &&
        auth.eraseAccount(managedId) == authentication::Result::Unauthorized,
        "standard user cannot create accounts, reset another credential, promote itself, disable, or delete");
    identity::UserRecordV1 managedRecord{};
    check(identities.findRecord(managedId, &managedRecord) == identity::Result::Ok &&
        managedRecord.status == identity::AccountStatus::Active &&
        managedRecord.role == identity::AccountRole::StandardUser &&
        auth.credentialEligible(managedId),
        "rejected standard-user account operations leave the target unmodified");
    check(auth.setAccountRole(standardId, identity::AccountRole::Administrator) ==
            authentication::Result::Unauthorized,
        "a standard-user session cannot change account roles");
    check(auth.signOutCurrent() == authentication::Result::Ok &&
        auth.authenticate("admin", initialPassword, textLength(initialPassword), &reference) ==
            authentication::Result::Ok &&
        auth.setAccountRole(standardId, identity::AccountRole::Administrator) ==
            authentication::Result::OkPersistent,
        "administrator session can promote a second account through the trusted runtime");
    check(auth.setAccountRole(admin.id, identity::AccountRole::StandardUser) ==
            authentication::Result::OkPersistent &&
        auth.authorizeCurrent(authorization::Operation::AccountCreate).decision ==
            authorization::Decision::InsufficientPrivileges,
        "trusted administrator can promote a second usable administrator before demotion");

    const char changedPassword[] = "new-correct-horse-battery";
    check(auth.signOutCurrent() == authentication::Result::Ok &&
        auth.authenticate("standard", initialPassword, textLength(initialPassword), &reference) ==
            authentication::Result::Ok &&
        auth.changeCredential(standardId, "wrong-current-password",
            textLength("wrong-current-password"), changedPassword,
            textLength(changedPassword)) == authentication::Result::InvalidCredentials,
        "password replacement requires the current verifier to match");
    check(auth.queryCurrentSession(&session) == authentication::Result::Ok &&
        session.state == authentication::SessionState::Active &&
        session.role == identity::AccountRole::Administrator &&
        auth.authorizeCurrent(authorization::Operation::AccountCreate).decision ==
            authorization::Decision::Allowed,
        "approved role promotion changes the authenticated user's next authorization decision");
    store.failNextWrite = true;
    check(auth.changeCredential(standardId, initialPassword, textLength(initialPassword),
            changedPassword, textLength(changedPassword)) == authentication::Result::IoError,
        "failed credential persistence does not report a password change");
    check(auth.signOutCurrent() == authentication::Result::Ok &&
        auth.authenticate("standard", initialPassword, textLength(initialPassword), &reference) ==
            authentication::Result::Ok,
        "old durable credential remains authoritative after failed replacement");
    check(auth.changeCredential(standardId, initialPassword, textLength(initialPassword),
            changedPassword, textLength(changedPassword)) == authentication::Result::OkPersistent &&
        auth.queryCurrentSession(&session) == authentication::Result::Ok &&
        session.state == authentication::SessionState::None &&
        auth.authorizeCurrent(authorization::Operation::AccountCreate).decision ==
            authorization::Decision::InvalidSession,
        "successful durable password change invalidates the active session");
    check(auth.authenticate("standard", initialPassword, textLength(initialPassword), &reference) ==
            authentication::Result::InvalidCredentials &&
        auth.authenticate("standard", changedPassword, textLength(changedPassword), &reference) ==
            authentication::Result::Ok,
        "new password is active only after the verified durable commit");

    check(auth.setAccountStatus(standardId, identity::AccountStatus::Disabled) ==
            authentication::Result::LastAdministrator,
        "runtime prevents disabling the last usable administrator");
    check(auth.signOutCurrent() == authentication::Result::Ok &&
        auth.authenticate("standard", changedPassword, textLength(changedPassword), &reference) ==
            authentication::Result::Ok &&
        auth.revokeCredential(standardId) == authentication::Result::LastAdministrator,
        "credential revocation cannot remove the last usable administrator credential");

    check(auth.signOutCurrent() == authentication::Result::Ok,
        "administrator can end its active session");
    testCrypto.now = 0u;
    for (uint32_t i = 0u; i < 5u; ++i)
        (void)auth.authenticate("admin", "wrong-password-attempt", 21u, &failedReference);
    check(auth.authenticate("admin", initialPassword, textLength(initialPassword), &failedReference) ==
            authentication::Result::Throttled,
        "bounded failed-attempt backoff applies to an account name");
    testCrypto.now = 500u;
    check(auth.authenticate("admin", "wrong-password-attempt", 21u, &failedReference) ==
            authentication::Result::InvalidCredentials &&
        auth.authenticate("admin", initialPassword, textLength(initialPassword), &failedReference) ==
            authentication::Result::Throttled,
        "attempt backoff expires and escalates without permanently disabling the account");

    TestCrypto failedEntropy;
    failedEntropy.entropyReady = false;
    identity::Runtime noEntropyIdentities;
    TestStore noEntropyIdentityStore;
    identity::StorageAdapter noEntropyIdentityStorage = identityAdapter(noEntropyIdentityStore);
    check(noEntropyIdentities.initialize(&noEntropyIdentityStorage) == identity::Result::Ok,
        "fresh identity registry loads for entropy-failure setup");
    TestStore noEntropyStore;
    authentication::Runtime noEntropyAuth;
    authentication::StorageAdapter noEntropyCredentialStorage = credentialAdapter(noEntropyStore);
    authentication::CryptoProvider noEntropyCrypto = cryptoAdapter(failedEntropy);
    check(noEntropyAuth.initialize(&noEntropyIdentities, &noEntropyCredentialStorage,
            &noEntropyCrypto, false) == authentication::Result::Ok &&
        noEntropyAuth.bootstrapAdministrator("nobody", "Nobody", initialPassword,
            textLength(initialPassword), &failedReference) == authentication::Result::EntropyUnavailable,
        "failed cryptographic entropy prevents first-admin provisioning without fallback salts");
    size_t noEntropyCount = 0u;
    check(noEntropyIdentities.enumerate(nullptr, 0u, &noEntropyCount) == identity::Result::Ok &&
        noEntropyCount == 0u,
        "entropy failure leaves the account registry empty");

    TestCrypto volatileCryptoState;
    identity::Runtime volatileIdentities;
    check(volatileIdentities.initialize(nullptr) == identity::Result::Ok,
        "identity registry supports the documented unavailable-storage compatibility mode");
    authentication::Runtime volatileAuth;
    authentication::CryptoProvider volatileProvider = cryptoAdapter(volatileCryptoState);
    authentication::RuntimeSnapshotV1 volatileSnapshot{};
    check(volatileAuth.initialize(&volatileIdentities, nullptr, &volatileProvider, false) ==
            authentication::Result::Ok &&
        volatileAuth.bootstrapAdministrator("volatile-admin", "Volatile Administrator",
            initialPassword, textLength(initialPassword), &failedReference) ==
            authentication::Result::OkVolatile &&
        volatileAuth.snapshot(&volatileSnapshot) == authentication::Result::Ok &&
        volatileSnapshot.bootstrapRequired == 0u &&
        volatileSnapshot.allCredentialsDurable == 0u &&
        volatileAuth.queryCurrentSession(&session) == authentication::Result::Ok &&
        session.state == authentication::SessionState::Active &&
        session.role == identity::AccountRole::Administrator,
        "SECURITY2 local bootstrap remains available without storage and reports volatile credentials");

    TestStore failedIdentityCommitStore;
    failedIdentityCommitStore.failNextWrite = true;
    TestCrypto failedIdentityCommitCrypto;
    identity::Runtime failedIdentityCommitIdentities;
    identity::StorageAdapter failedIdentityCommitIdentityStorage =
        identityAdapter(failedIdentityCommitStore);
    check(failedIdentityCommitIdentities.initialize(&failedIdentityCommitIdentityStorage) ==
            identity::Result::Ok,
        "identity registry initializes before bootstrap write-failure check");
    authentication::Runtime failedIdentityCommitAuth;
    authentication::StorageAdapter failedIdentityCommitCredentialStorage =
        credentialAdapter(failedIdentityCommitStore);
    authentication::CryptoProvider failedIdentityCommitProvider =
        cryptoAdapter(failedIdentityCommitCrypto);
    check(failedIdentityCommitAuth.initialize(&failedIdentityCommitIdentities,
            &failedIdentityCommitCredentialStorage, &failedIdentityCommitProvider, false) ==
            authentication::Result::Ok &&
        failedIdentityCommitAuth.bootstrapAdministrator("storage-admin", "Storage Admin",
            initialPassword, textLength(initialPassword), &failedReference) ==
            authentication::Result::IoError,
        "failed identity persistence is reported as I/O failure during administrator bootstrap");
    size_t failedIdentityCount = 0u;
    check(failedIdentityCommitIdentities.enumerate(nullptr, 0u, &failedIdentityCount) ==
            identity::Result::Ok && failedIdentityCount == 0u &&
        !failedIdentityCommitStore.credentialPresent[0] &&
        !failedIdentityCommitStore.credentialPresent[1],
        "failed identity commit leaves no account or orphan credential active");

    // Reload a clean persistent credential set to verify association and
    // interrupted credential-store updates without relying on live sessions.
    TestCrypto reloadCrypto;
    identity::Runtime reloadedIdentities;
    identity::StorageAdapter reloadIdentityStorage = identityAdapter(store);
    check(reloadedIdentities.initialize(&reloadIdentityStorage) == identity::Result::Ok,
        "identity account and stable ID survive a simulated restart");
    authentication::Runtime reloadedAuth;
    authentication::StorageAdapter reloadCredentialStorage = credentialAdapter(store);
    authentication::CryptoProvider reloadCryptoProvider = cryptoAdapter(reloadCrypto);
    check(reloadedAuth.initialize(&reloadedIdentities, &reloadCredentialStorage,
            &reloadCryptoProvider, false) == authentication::Result::Ok &&
        reloadedAuth.authenticate("admin", initialPassword, textLength(initialPassword),
            &failedReference) == authentication::Result::Ok,
        "durable credential record reloads and authenticates its original account after restart");

    TestStore disabledAdministratorStore;
    TestCrypto disabledAdministratorCrypto;
    identity::Runtime disabledAdministratorIdentities;
    identity::StorageAdapter disabledAdministratorIdentityStorage =
        identityAdapter(disabledAdministratorStore);
    authentication::Runtime disabledAdministratorAuth;
    authentication::StorageAdapter disabledAdministratorCredentialStorage =
        credentialAdapter(disabledAdministratorStore);
    authentication::CryptoProvider disabledAdministratorProvider =
        cryptoAdapter(disabledAdministratorCrypto);
    check(disabledAdministratorIdentities.initialize(
            &disabledAdministratorIdentityStorage) == identity::Result::Ok &&
        disabledAdministratorAuth.initialize(&disabledAdministratorIdentities,
            &disabledAdministratorCredentialStorage, &disabledAdministratorProvider, false) ==
            authentication::Result::Ok &&
        disabledAdministratorAuth.bootstrapAdministrator("disabled-admin", "Disabled Admin",
            initialPassword, textLength(initialPassword), &failedReference) ==
            authentication::Result::OkPersistent,
        "isolated bootstrap fixture creates an initial administrator");
    identity::UserRecordV1 disabledAdministrator{};
    check(disabledAdministratorIdentities.findByAccountName("disabled-admin",
            &disabledAdministrator) == identity::Result::Ok &&
        disabledAdministratorAuth.signOutCurrent() == authentication::Result::Ok &&
        disabledAdministratorIdentities.setStatus(disabledAdministrator.id,
            identity::AccountStatus::Disabled) == identity::Result::Unauthorized &&
        disabledAdministratorAuth.bootstrapAdministrator("replacement-admin",
            "Replacement Admin", initialPassword, textLength(initialPassword),
            &failedReference) == authentication::Result::Unauthorized,
        "bound identity registry rejects direct mutation and keeps anonymous bootstrap closed");

    std::cout << "Authentication and session checks: " << (checks - failures) << "/"
              << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
