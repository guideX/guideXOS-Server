#if defined(GXOS_SECURITY3A_QUALIFICATION)

#include "include/kernel/authentication.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"
#include "security3a_test_secrets.h"

#include <string.h>

namespace kernel {
namespace {

static const uint64_t kExpectedAdministratorId = 0x1000u;
static const uint64_t kExpectedStandardUserId = 0x1001u;
static const size_t kCredentialHeaderBytes = 32u;
static const size_t kCredentialRecordBytes = 64u;
static const uint32_t kCredentialFormatVersion = 1u;
static const uint32_t kCredentialAlgorithmPbkdf2Sha256 = 1u;
static const uint32_t kCredentialIterations =
    authentication::kPbkdf2HmacSha256Iterations;

static uint8_t s_identityBytes[identity::kMaximumIdentityStoreBytes];
static uint8_t s_credentialBytes[32u + identity::kMaximumUsers * 64u + 4u];
static bool s_failed;

static uint32_t read32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8u) |
        (static_cast<uint32_t>(bytes[2]) << 16u) |
        (static_cast<uint32_t>(bytes[3]) << 24u);
}

static uint64_t read64(const uint8_t* bytes)
{
    return static_cast<uint64_t>(read32(bytes)) |
        (static_cast<uint64_t>(read32(bytes + 4u)) << 32u);
}

static uint32_t crc32(const uint8_t* bytes, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(0u - (crc & 1u));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void check(const char* name, bool passed)
{
    serial::puts("[SECURITY3A] check=");
    serial::puts(name);
    serial::puts(passed ? " result=PASS\n" : " result=FAIL\n");
    if (!passed) s_failed = true;
}

static void printId(const char* name, identity::UserId id)
{
    serial::puts("[SECURITY3A] ");
    serial::puts(name);
    serial::puts("=0x");
    serial::put_hex64(id);
    serial::putc('\n');
}

static bool signOutIfActive()
{
    authentication::SessionSnapshotV1 session{};
    if (authentication::runtime().queryCurrentSession(&session) !=
        authentication::Result::Ok) return false;
    if (session.state == authentication::SessionState::None) return true;
    return authentication::runtime().signOutCurrent() == authentication::Result::Ok;
}

static bool readSlot(const char* path, uint8_t* output, size_t capacity,
                     size_t* outBytes)
{
    if (outBytes) *outBytes = 0u;
    if (!path || !output || !outBytes) return false;
    vfs::FileInfo info{};
    if (vfs::stat(path, &info) != vfs::VFS_OK ||
        info.type != vfs::FILE_TYPE_REGULAR || info.size == 0u ||
        info.size > capacity) return false;
    const int32_t read = vfs::read_file(path, output,
        static_cast<uint32_t>(capacity));
    if (read < 0 || static_cast<uint32_t>(read) != info.size) return false;
    *outBytes = static_cast<size_t>(read);
    return true;
}

static uint64_t readIdentityGeneration(uint8_t slot)
{
    const char* path = slot == 0u ? "/.gxos-identity-0.dat" :
        "/.gxos-identity-1.dat";
    size_t byteCount = 0u;
    if (!readSlot(path, s_identityBytes, sizeof(s_identityBytes), &byteCount) ||
        byteCount < identity::kIdentityHeaderBytes + 4u ||
        memcmp(s_identityBytes, "GXOSID01", 8u) != 0 ||
        read32(s_identityBytes + 8u) != identity::kUserRecordVersion) return 0u;
    return read64(s_identityBytes + 16u);
}

static uint64_t readCredentialGeneration(uint8_t slot)
{
    const char* path = slot == 0u ? "/.gxos-credentials-0.dat" :
        "/.gxos-credentials-1.dat";
    size_t byteCount = 0u;
    if (!readSlot(path, s_credentialBytes, sizeof(s_credentialBytes), &byteCount) ||
        byteCount < kCredentialHeaderBytes + 4u ||
        memcmp(s_credentialBytes, "GXOSCRD1", 8u) != 0 ||
        read32(s_credentialBytes + 8u) != kCredentialFormatVersion) return 0u;
    return read64(s_credentialBytes + 16u);
}

static bool rawCredentialSlotValid(uint8_t slot)
{
    const char* path = slot == 0u ? "/.gxos-credentials-0.dat" :
        "/.gxos-credentials-1.dat";
    size_t byteCount = 0u;
    if (!readSlot(path, s_credentialBytes, sizeof(s_credentialBytes), &byteCount) ||
        byteCount < kCredentialHeaderBytes + 4u ||
        memcmp(s_credentialBytes, "GXOSCRD1", 8u) != 0 ||
        read32(s_credentialBytes + 8u) != kCredentialFormatVersion ||
        read32(s_credentialBytes + 12u) != kCredentialHeaderBytes ||
        read32(s_credentialBytes + 28u) != kCredentialRecordBytes) return false;
    const uint32_t count = read32(s_credentialBytes + 24u);
    const size_t expectedBytes = kCredentialHeaderBytes +
        static_cast<size_t>(count) * kCredentialRecordBytes + 4u;
    return count <= identity::kMaximumUsers && byteCount == expectedBytes &&
        read32(s_credentialBytes + byteCount - 4u) ==
            crc32(s_credentialBytes, byteCount - 4u);
}

static void logStorageGenerations()
{
    const uint64_t identity0 = readIdentityGeneration(0u);
    const uint64_t identity1 = readIdentityGeneration(1u);
    const uint64_t credential0 = readCredentialGeneration(0u);
    const uint64_t credential1 = readCredentialGeneration(1u);
    serial::puts("[SECURITY3A] identity_slot0_generation=");
    serial::put_hex64(identity0);
    serial::puts(" identity_slot1_generation=");
    serial::put_hex64(identity1);
    serial::puts(" credential_slot0_generation=");
    serial::put_hex64(credential0);
    serial::puts(" credential_slot1_generation=");
    serial::put_hex64(credential1);
    serial::putc('\n');
}

static bool latestIdentityBytes(uint64_t* outGeneration, size_t* outBytes)
{
    const uint64_t generation0 = readIdentityGeneration(0u);
    const uint64_t generation1 = readIdentityGeneration(1u);
    if (generation0 == 0u && generation1 == 0u) return false;
    const uint8_t slot = generation1 > generation0 ? 1u : 0u;
    const char* path = slot == 0u ? "/.gxos-identity-0.dat" :
        "/.gxos-identity-1.dat";
    if (!readSlot(path, s_identityBytes, sizeof(s_identityBytes), outBytes)) return false;
    *outGeneration = read64(s_identityBytes + 16u);
    return true;
}

static bool latestCredentialBytes(uint64_t* outGeneration, size_t* outBytes)
{
    const uint64_t generation0 = readCredentialGeneration(0u);
    const uint64_t generation1 = readCredentialGeneration(1u);
    if (generation0 == 0u && generation1 == 0u) return false;
    const uint8_t slot = generation1 > generation0 ? 1u : 0u;
    const char* path = slot == 0u ? "/.gxos-credentials-0.dat" :
        "/.gxos-credentials-1.dat";
    if (!readSlot(path, s_credentialBytes, sizeof(s_credentialBytes), outBytes)) return false;
    *outGeneration = read64(s_credentialBytes + 16u);
    return true;
}

static bool rawIdentityRecordsMatch(identity::AccountStatus standardStatus,
                                     identity::AccountRole standardRole)
{
    uint64_t generation = 0u;
    size_t byteCount = 0u;
    if (!latestIdentityBytes(&generation, &byteCount) || generation == 0u ||
        read32(s_identityBytes + 32u) != 2u ||
        read32(s_identityBytes + 36u) != identity::kIdentityRecordBytes ||
        byteCount != identity::kIdentityHeaderBytes +
            2u * identity::kIdentityRecordBytes + 4u) return false;
    const size_t first = identity::kIdentityHeaderBytes;
    const size_t second = first + identity::kIdentityRecordBytes;
    const bool adminMatches = read64(s_identityBytes + first) ==
            kExpectedAdministratorId &&
        s_identityBytes[first + 8u] == static_cast<uint8_t>(identity::AccountStatus::Active) &&
        s_identityBytes[first + 9u] == static_cast<uint8_t>(identity::AccountRole::Administrator);
    const bool standardMatches = read64(s_identityBytes + second) ==
            kExpectedStandardUserId &&
        s_identityBytes[second + 8u] == static_cast<uint8_t>(standardStatus) &&
        s_identityBytes[second + 9u] == static_cast<uint8_t>(standardRole);
    return adminMatches && standardMatches;
}

static bool rawCredentialRecordsMatch()
{
    uint64_t generation = 0u;
    size_t byteCount = 0u;
    if (!latestCredentialBytes(&generation, &byteCount) || generation == 0u ||
        read32(s_credentialBytes + 12u) != kCredentialHeaderBytes ||
        read32(s_credentialBytes + 24u) != 2u ||
        read32(s_credentialBytes + 28u) != kCredentialRecordBytes ||
        byteCount != kCredentialHeaderBytes + 2u * kCredentialRecordBytes + 4u) return false;
    bool adminPresent = false;
    bool standardPresent = false;
    size_t offset = kCredentialHeaderBytes;
    for (uint32_t i = 0u; i < 2u; ++i, offset += kCredentialRecordBytes) {
        const uint64_t id = read64(s_credentialBytes + offset);
        const bool formatMatches = read32(s_credentialBytes + offset + 12u) ==
            kCredentialIterations &&
            static_cast<uint32_t>(s_credentialBytes[offset + 8u] |
                (static_cast<uint16_t>(s_credentialBytes[offset + 9u]) << 8u)) ==
                kCredentialFormatVersion &&
            static_cast<uint32_t>(s_credentialBytes[offset + 10u] |
                (static_cast<uint16_t>(s_credentialBytes[offset + 11u]) << 8u)) ==
                kCredentialAlgorithmPbkdf2Sha256;
        if (id == kExpectedAdministratorId) adminPresent = formatMatches;
        if (id == kExpectedStandardUserId) standardPresent = formatMatches;
    }
    return adminPresent && standardPresent;
}

static bool enumerateExpected(identity::UserRecordV1* administrator,
                              identity::UserRecordV1* standardUser)
{
    if (!administrator || !standardUser) return false;
    const identity::Result adminResult = identity::runtime().findByAccountName(
        SECURITY3A_ADMIN_ACCOUNT, administrator);
    const identity::Result standardResult = identity::runtime().findRecord(
        kExpectedStandardUserId, standardUser);
    return adminResult == identity::Result::Ok &&
        (standardResult == identity::Result::Ok ||
         standardResult == identity::Result::Inactive);
}

static authentication::Result authenticate(
    const char* account, const char* password, size_t passwordBytes,
    authentication::Result expected, identity::UserId expectedUser,
    identity::AccountRole expectedRole, bool keepSession)
{
    if (!signOutIfActive()) return authentication::Result::SessionUnavailable;
    authentication::SessionReference reference{};
    authentication::Result result = authentication::runtime().authenticate(
        account, password, passwordBytes, &reference);
    for (uint32_t retry = 1u; retry <= 2u && expected == authentication::Result::Ok &&
         result == authentication::Result::EntropyUnavailable; ++retry) {
        serial::puts("[SECURITY3A] auth_entropy_retry=");
        serial::put_hex32(retry);
        serial::putc('\n');
        result = authentication::runtime().authenticate(
            account, password, passwordBytes, &reference);
    }
    serial::puts("[SECURITY3A] auth_result actual=");
    serial::puts(authentication::resultName(result));
    serial::puts(" expected=");
    serial::puts(authentication::resultName(expected));
    serial::putc('\n');
    check("authentication_result", result == expected);
    if (result == authentication::Result::Ok) {
        authentication::SessionSnapshotV1 session{};
        const bool sessionMatches = authentication::runtime().queryCurrentSession(&session) ==
                authentication::Result::Ok &&
            session.state == authentication::SessionState::Active &&
            session.userId == expectedUser && session.role == expectedRole;
        check("session_binds_stable_id_and_role", sessionMatches);
        if (!keepSession) check("session_terminated", signOutIfActive());
    } else {
        authentication::SessionSnapshotV1 session{};
        check("invalid_auth_opens_no_session",
            authentication::runtime().queryCurrentSession(&session) ==
                authentication::Result::Ok && session.state == authentication::SessionState::None);
    }
    volatile uint8_t* clearReference = reference.bytes;
    for (size_t i = 0u; i < sizeof(reference.bytes); ++i) clearReference[i] = 0u;
    return result;
}

static void finish(const char* phase)
{
    if (!signOutIfActive()) s_failed = true;
    authentication::SessionSnapshotV1 session{};
    const bool noSession = authentication::runtime().queryCurrentSession(&session) ==
            authentication::Result::Ok &&
        session.state == authentication::SessionState::None;
    if (!noSession) s_failed = true;
    serial::puts("[SECURITY3A] phase=");
    serial::puts(phase);
    serial::puts(" end_session=none result=");
    serial::puts(s_failed ? "FAIL\n" : "PASS\n");
}

static void runExpectedCorruption()
{
    identity::RuntimeSnapshotV1 identitySnapshot{};
    authentication::RuntimeSnapshotV1 authSnapshot{};
    (void)identity::runtime().snapshot(&identitySnapshot);
    (void)authentication::runtime().snapshot(&authSnapshot);
    logStorageGenerations();

#if GXOS_SECURITY3A_EXPECT_MODE == 1 || GXOS_SECURITY3A_EXPECT_MODE == 2 || \
    GXOS_SECURITY3A_EXPECT_MODE == 5 || GXOS_SECURITY3A_EXPECT_MODE == 6
    const identity::PersistenceState expectedState =
        GXOS_SECURITY3A_EXPECT_MODE == 2 ? identity::PersistenceState::Unsupported :
            identity::PersistenceState::Corrupt;
    const bool credentialReadable = authSnapshot.credentialStoreAvailable != 0u;
    check("identity_store_remains_readable",
        identitySnapshot.accountCountAvailable != 0u &&
        identitySnapshot.persistence == identity::PersistenceState::Persistent);
    check("credential_store_rejects_damaged_snapshot",
        !credentialReadable &&
        authSnapshot.persistence == expectedState);
    if (GXOS_SECURITY3A_EXPECT_MODE == 5 || GXOS_SECURITY3A_EXPECT_MODE == 6) {
        const bool slot0Valid = rawCredentialSlotValid(0u);
        const bool slot1Valid = rawCredentialSlotValid(1u);
        check("last_good_credential_snapshot_retained",
            slot0Valid != slot1Valid);
    }
    const authentication::Result authResult = authenticate(
        SECURITY3A_ADMIN_ACCOUNT, SECURITY3A_ADMIN_PASSWORD,
        sizeof(SECURITY3A_ADMIN_PASSWORD) - 1u,
        GXOS_SECURITY3A_EXPECT_MODE == 2
            ? authentication::Result::UnsupportedCredentialFormat
            : authentication::Result::StorageUnavailable,
        kExpectedAdministratorId, identity::AccountRole::Administrator, false);
    check(GXOS_SECURITY3A_EXPECT_MODE == 5
            ? "damaged_newer_credentials_fail_closed"
            : (GXOS_SECURITY3A_EXPECT_MODE == 6
                ? "truncated_newer_credentials_fail_closed"
                : "damaged_credentials_do_not_authenticate"),
        authResult != authentication::Result::Ok);
#elif GXOS_SECURITY3A_EXPECT_MODE == 3 || GXOS_SECURITY3A_EXPECT_MODE == 4
    const identity::PersistenceState expectedState =
        GXOS_SECURITY3A_EXPECT_MODE == 3 ? identity::PersistenceState::Corrupt :
            identity::PersistenceState::Unsupported;
    check("identity_store_reports_damaged_snapshot",
        identitySnapshot.accountCountAvailable == 0u &&
        identitySnapshot.persistence == expectedState);
    check("credential_store_remains_independently_readable",
        authSnapshot.credentialStoreAvailable != 0u &&
        authSnapshot.persistence == identity::PersistenceState::Persistent);
    const authentication::Result authResult = authenticate(
        SECURITY3A_ADMIN_ACCOUNT, SECURITY3A_ADMIN_PASSWORD,
        sizeof(SECURITY3A_ADMIN_PASSWORD) - 1u,
        authentication::Result::AccountUnavailable,
        kExpectedAdministratorId, identity::AccountRole::Administrator, false);
    check("damaged_identity_does_not_authenticate", authResult != authentication::Result::Ok);
    authentication::SessionReference bootstrapSession{};
    const authentication::Result bootstrap = authentication::runtime().bootstrapAdministrator(
        SECURITY3A_ADMIN_ACCOUNT, SECURITY3A_ADMIN_DISPLAY_NAME,
        SECURITY3A_ADMIN_PASSWORD, sizeof(SECURITY3A_ADMIN_PASSWORD) - 1u,
        &bootstrapSession);
    check("damaged_identity_does_not_bootstrap_admin",
        bootstrap == authentication::Result::AccountUnavailable);
    volatile uint8_t* clearBootstrap = bootstrapSession.bytes;
    for (size_t i = 0u; i < sizeof(bootstrapSession.bytes); ++i) clearBootstrap[i] = 0u;
#else
    check("expected_corruption_mode_configured", false);
#endif
    finish("corruption-negative");
}

static void runProvisioningAndReloadQualification()
{
    identity::RuntimeSnapshotV1 identitySnapshot{};
    authentication::RuntimeSnapshotV1 authSnapshot{};
    const identity::Result identitySnapshotResult = identity::runtime().snapshot(&identitySnapshot);
    const authentication::Result authSnapshotResult = authentication::runtime().snapshot(&authSnapshot);
    check("native_identity_and_authentication_initialized",
        identitySnapshotResult == identity::Result::Ok &&
        authSnapshotResult == authentication::Result::Ok &&
        identitySnapshot.initialized != 0u && authSnapshot.initialized != 0u);
    check("identity_and_credential_storage_available_and_writable",
        identitySnapshot.accountCountAvailable != 0u &&
        identitySnapshot.persistence == identity::PersistenceState::Persistent &&
        identitySnapshot.allRecordsDurable != 0u &&
        authSnapshot.credentialStoreAvailable != 0u &&
        authSnapshot.persistence == identity::PersistenceState::Persistent &&
        authSnapshot.allCredentialsDurable != 0u);

    authentication::SessionSnapshotV1 initialSession{};
    identity::IdentityContextV1 initialContext{};
    const bool coldStartUnassigned =
        authentication::runtime().queryCurrentSession(&initialSession) ==
            authentication::Result::Ok && initialSession.state == authentication::SessionState::None &&
        authentication::runtime().queryContext(identity::ExecutionDomain::Interactive,
            &initialContext) == authentication::Result::Ok &&
        initialContext.kind == identity::IdentityKind::InteractiveUnassigned &&
        initialContext.userId == identity::kInvalidUserId;
    check("cold_boot_has_no_authenticated_session", coldStartUnassigned);

    if (!identitySnapshot.accountCountAvailable ||
        identitySnapshot.persistence != identity::PersistenceState::Persistent ||
        authSnapshot.credentialStoreAvailable == 0u ||
        authSnapshot.persistence != identity::PersistenceState::Persistent) {
        finish("storage-unavailable");
        return;
    }

    if (identitySnapshot.accountCount == 0u) {
        check("first_boot_registry_is_unprovisioned", authSnapshot.bootstrapRequired != 0u &&
            authSnapshot.credentialCount == 0u);
        if (identitySnapshot.accountCount != 0u || authSnapshot.bootstrapRequired == 0u) {
            finish("first-boot-state-mismatch");
            return;
        }

        authentication::SessionReference bootstrapSession{};
        const authentication::Result bootstrap = authentication::runtime().bootstrapAdministrator(
            SECURITY3A_ADMIN_ACCOUNT, SECURITY3A_ADMIN_DISPLAY_NAME,
            SECURITY3A_ADMIN_PASSWORD, sizeof(SECURITY3A_ADMIN_PASSWORD) - 1u,
            &bootstrapSession);
        serial::puts("[SECURITY3A] bootstrap_result=");
        serial::puts(authentication::resultName(bootstrap));
        serial::putc('\n');
        check("production_bootstrap_admin_commits_durably",
            bootstrap == authentication::Result::OkPersistent);
        volatile uint8_t* clearBootstrap = bootstrapSession.bytes;
        for (size_t i = 0u; i < sizeof(bootstrapSession.bytes); ++i) clearBootstrap[i] = 0u;

        identity::UserId standardId = identity::kInvalidUserId;
        const authentication::Result created = authentication::runtime().createAccount(
            SECURITY3A_STANDARD_ACCOUNT, SECURITY3A_STANDARD_DISPLAY_NAME, &standardId);
        serial::puts("[SECURITY3A] standard_account_create_result=");
        serial::puts(authentication::resultName(created));
        serial::putc('\n');
        check("standard_user_identity_commits_durably",
            created == authentication::Result::OkPersistent &&
            standardId == kExpectedStandardUserId);
        const authentication::Result enrolled = authentication::runtime().enrollCredential(
            standardId, SECURITY3A_STANDARD_PASSWORD,
            sizeof(SECURITY3A_STANDARD_PASSWORD) - 1u);
        serial::puts("[SECURITY3A] standard_credential_enroll_result=");
        serial::puts(authentication::resultName(enrolled));
        serial::putc('\n');
        check("standard_user_credential_commits_durably",
            enrolled == authentication::Result::OkPersistent);

        identity::UserRecordV1 admin{};
        identity::UserRecordV1 standard{};
        const bool recordsMatch = enumerateExpected(&admin, &standard) &&
            admin.id == kExpectedAdministratorId &&
            admin.role == identity::AccountRole::Administrator &&
            admin.status == identity::AccountStatus::Active &&
            standard.id == kExpectedStandardUserId &&
            standard.role == identity::AccountRole::StandardUser &&
            standard.status == identity::AccountStatus::Active;
        check("both_accounts_have_expected_stable_ids_and_roles", recordsMatch);
        if (recordsMatch) {
            printId("administrator_id", admin.id);
            printId("standard_user_id", standard.id);
        }
        check("initial_passwords_authenticate_and_invalid_is_rejected",
            authenticate(SECURITY3A_ADMIN_ACCOUNT, "bad-security3a-password", 23u,
                authentication::Result::InvalidCredentials, kExpectedAdministratorId,
                identity::AccountRole::Administrator, false) ==
                    authentication::Result::InvalidCredentials &&
            authenticate(SECURITY3A_ADMIN_ACCOUNT, SECURITY3A_ADMIN_PASSWORD,
                sizeof(SECURITY3A_ADMIN_PASSWORD) - 1u, authentication::Result::Ok,
                kExpectedAdministratorId, identity::AccountRole::Administrator, false) ==
                    authentication::Result::Ok &&
            authenticate(SECURITY3A_STANDARD_ACCOUNT, SECURITY3A_STANDARD_PASSWORD,
                sizeof(SECURITY3A_STANDARD_PASSWORD) - 1u, authentication::Result::Ok,
                kExpectedStandardUserId, identity::AccountRole::StandardUser, false) ==
                    authentication::Result::Ok &&
            authenticate(SECURITY3A_STANDARD_ACCOUNT, "bad-security3a-password", 23u,
                authentication::Result::InvalidCredentials, kExpectedStandardUserId,
                identity::AccountRole::StandardUser, false) ==
                    authentication::Result::InvalidCredentials);
        check("committed_disk_bytes_contain_expected_records_and_kdf",
            rawIdentityRecordsMatch(identity::AccountStatus::Active,
                identity::AccountRole::StandardUser) && rawCredentialRecordsMatch());
        logStorageGenerations();
        finish("first-boot-provisioning");
        return;
    }

    identity::UserRecordV1 admin{};
    identity::UserRecordV1 standard{};
    const bool recordsFound = enumerateExpected(&admin, &standard);
    check("cold_reload_returns_both_records", recordsFound &&
        identitySnapshot.accountCount == 2u);
    if (!recordsFound) {
        finish("account-reload-failed");
        return;
    }
    printId("administrator_id", admin.id);
    printId("standard_user_id", standard.id);
    check("stable_ids_roles_and_lifecycle_reloaded", admin.id == kExpectedAdministratorId &&
        admin.role == identity::AccountRole::Administrator &&
        admin.status == identity::AccountStatus::Active &&
        standard.id == kExpectedStandardUserId);
    check("credential_algorithms_reloaded_from_raw_slots", rawCredentialRecordsMatch());
    check("administrator_credential_authenticates_after_cold_boot",
        authenticate(SECURITY3A_ADMIN_ACCOUNT, SECURITY3A_ADMIN_PASSWORD,
            sizeof(SECURITY3A_ADMIN_PASSWORD) - 1u, authentication::Result::Ok,
            kExpectedAdministratorId, identity::AccountRole::Administrator, false) ==
                authentication::Result::Ok);

    if (standard.status == identity::AccountStatus::Disabled) {
        check("disabled_user_status_and_role_survived_cold_boot",
            standard.role == identity::AccountRole::StandardUser &&
            rawIdentityRecordsMatch(identity::AccountStatus::Disabled,
                identity::AccountRole::StandardUser));
        const authentication::Result disabledAuth = authenticate(
            SECURITY3A_STANDARD_ACCOUNT, SECURITY3A_STANDARD_CHANGED_PASSWORD,
            sizeof(SECURITY3A_STANDARD_CHANGED_PASSWORD) - 1u,
            authentication::Result::InvalidCredentials, kExpectedStandardUserId,
            identity::AccountRole::StandardUser, false);
        check("disabled_account_cannot_authenticate",
            disabledAuth == authentication::Result::InvalidCredentials);
        check("administrator_still_authenticates_after_cold_reload",
            authenticate(SECURITY3A_ADMIN_ACCOUNT, SECURITY3A_ADMIN_PASSWORD,
                sizeof(SECURITY3A_ADMIN_PASSWORD) - 1u, authentication::Result::Ok,
                kExpectedAdministratorId, identity::AccountRole::Administrator, false) ==
                    authentication::Result::Ok);
        logStorageGenerations();
        finish("cold-reload-revocation");
        return;
    }

    if (standard.role == identity::AccountRole::Administrator) {
        check("role_change_survived_cold_boot",
            standard.status == identity::AccountStatus::Active &&
            rawIdentityRecordsMatch(identity::AccountStatus::Active,
                identity::AccountRole::Administrator));
        check("changed_password_authenticates_after_cold_boot",
            authenticate(SECURITY3A_STANDARD_ACCOUNT,
                SECURITY3A_STANDARD_CHANGED_PASSWORD,
                sizeof(SECURITY3A_STANDARD_CHANGED_PASSWORD) - 1u,
                authentication::Result::Ok, kExpectedStandardUserId,
                identity::AccountRole::Administrator, false) == authentication::Result::Ok);
        check("administrator_signin_for_lifecycle_update",
            authenticate(SECURITY3A_ADMIN_ACCOUNT, SECURITY3A_ADMIN_PASSWORD,
                sizeof(SECURITY3A_ADMIN_PASSWORD) - 1u, authentication::Result::Ok,
                kExpectedAdministratorId, identity::AccountRole::Administrator, true) ==
                    authentication::Result::Ok);
        const authentication::Result roleChanged = authentication::runtime().setAccountRole(
            standard.id, identity::AccountRole::StandardUser);
        const authentication::Result disabled = authentication::runtime().setAccountStatus(
            standard.id, identity::AccountStatus::Disabled);
        check("role_reversion_and_account_disable_commit_durably",
            roleChanged == authentication::Result::OkPersistent &&
            disabled == authentication::Result::OkPersistent);
        logStorageGenerations();
        finish("disable-account");
        return;
    }

    check("active_standard_role_reloaded", standard.status == identity::AccountStatus::Active &&
        standard.role == identity::AccountRole::StandardUser &&
        rawIdentityRecordsMatch(identity::AccountStatus::Active,
            identity::AccountRole::StandardUser));
    uint64_t credentialGeneration = 0u;
    size_t credentialBytes = 0u;
    const bool credentialSnapshotReadable = latestCredentialBytes(
        &credentialGeneration, &credentialBytes);
    check("credential_snapshot_generation_read_from_vfs",
        credentialSnapshotReadable && credentialGeneration >= 2u);
    if (!credentialSnapshotReadable) {
        finish("credential-snapshot-unreadable");
        return;
    }

    if (credentialGeneration == 2u) {
        check("initial_password_authenticates_after_cold_boot",
            authenticate(SECURITY3A_STANDARD_ACCOUNT, SECURITY3A_STANDARD_PASSWORD,
                sizeof(SECURITY3A_STANDARD_PASSWORD) - 1u,
                authentication::Result::Ok, kExpectedStandardUserId,
                identity::AccountRole::StandardUser, false) == authentication::Result::Ok);
        check("not_yet_changed_password_is_rejected",
            authenticate(SECURITY3A_STANDARD_ACCOUNT,
                SECURITY3A_STANDARD_CHANGED_PASSWORD,
                sizeof(SECURITY3A_STANDARD_CHANGED_PASSWORD) - 1u,
                authentication::Result::InvalidCredentials, kExpectedStandardUserId,
                identity::AccountRole::StandardUser, false) ==
                    authentication::Result::InvalidCredentials);
        if (authenticate(SECURITY3A_STANDARD_ACCOUNT, SECURITY3A_STANDARD_PASSWORD,
                sizeof(SECURITY3A_STANDARD_PASSWORD) - 1u,
                authentication::Result::Ok, kExpectedStandardUserId,
                identity::AccountRole::StandardUser, true) == authentication::Result::Ok) {
            const authentication::Result changed = authentication::runtime().changeCredential(
                standard.id, SECURITY3A_STANDARD_PASSWORD,
                sizeof(SECURITY3A_STANDARD_PASSWORD) - 1u,
                SECURITY3A_STANDARD_CHANGED_PASSWORD,
                sizeof(SECURITY3A_STANDARD_CHANGED_PASSWORD) - 1u);
            check("password_change_commits_durably", changed ==
                authentication::Result::OkPersistent);
            authentication::SessionSnapshotV1 afterChange{};
            check("password_change_terminates_current_session",
                authentication::runtime().queryCurrentSession(&afterChange) ==
                    authentication::Result::Ok &&
                afterChange.state == authentication::SessionState::None);
        } else {
            check("password_change_owner_signin", false);
        }
        logStorageGenerations();
        finish("password-change");
        return;
    }

    check("new_password_authenticates_after_cold_boot",
        authenticate(SECURITY3A_STANDARD_ACCOUNT,
            SECURITY3A_STANDARD_CHANGED_PASSWORD,
            sizeof(SECURITY3A_STANDARD_CHANGED_PASSWORD) - 1u,
            authentication::Result::Ok, kExpectedStandardUserId,
            identity::AccountRole::StandardUser, false) == authentication::Result::Ok);
    check("old_password_rejected_after_cold_boot",
        authenticate(SECURITY3A_STANDARD_ACCOUNT, SECURITY3A_STANDARD_PASSWORD,
            sizeof(SECURITY3A_STANDARD_PASSWORD) - 1u,
            authentication::Result::InvalidCredentials, kExpectedStandardUserId,
            identity::AccountRole::StandardUser, false) ==
                authentication::Result::InvalidCredentials);
    check("administrator_signin_for_role_update",
        authenticate(SECURITY3A_ADMIN_ACCOUNT, SECURITY3A_ADMIN_PASSWORD,
            sizeof(SECURITY3A_ADMIN_PASSWORD) - 1u, authentication::Result::Ok,
            kExpectedAdministratorId, identity::AccountRole::Administrator, true) ==
                authentication::Result::Ok);
    const authentication::Result roleChanged = authentication::runtime().setAccountRole(
        standard.id, identity::AccountRole::Administrator);
    check("standard_to_administrator_role_change_commits_durably",
        roleChanged == authentication::Result::OkPersistent);
    logStorageGenerations();
    finish("role-update");
}

} // namespace

void runSecurity3aCredentialQualification()
{
    s_failed = false;
    serial::puts("[SECURITY3A] harness=production-runtime mode=");
    serial::put_hex32(GXOS_SECURITY3A_EXPECT_MODE);
    serial::puts(" storage=real-vfs-fat32\n");
#if GXOS_SECURITY3A_EXPECT_MODE == 0
    runProvisioningAndReloadQualification();
#else
    runExpectedCorruption();
#endif
}

} // namespace kernel

#endif
