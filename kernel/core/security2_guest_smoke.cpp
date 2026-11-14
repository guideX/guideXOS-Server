#if defined(GXOS_SECURITY2_AUTH_SMOKE_TEST)

#include "include/kernel/authentication.h"
#include "include/kernel/serial_debug.h"

namespace kernel {
namespace {
identity::Runtime s_smokeIdentities;
authentication::Runtime s_smokeAuthentication;
}

void runSecurity2AuthenticationGuestSmoke()
{
    const identity::StorageAdapter volatileIdentityStorage{};
    const authentication::StorageAdapter volatileCredentialStorage{};
    authentication::CryptoProvider crypto = authentication::platformCryptoProvider();
    if (s_smokeIdentities.initialize(&volatileIdentityStorage) != identity::Result::Ok ||
        s_smokeAuthentication.initialize(&s_smokeIdentities,
            &volatileCredentialStorage, &crypto, true) !=
            authentication::Result::Ok) {
        serial::puts("[SECURITY2_SMOKE] result=FAIL stage=volatile-runtime-init\n");
        return;
    }

    uint8_t randomPassword[24]{};
    char password[49]{};
    static const char kHex[] = "0123456789abcdef";
    if (!crypto.randomBytes || !crypto.randomBytes(crypto.context, randomPassword,
            sizeof(randomPassword))) {
        serial::puts("[SECURITY2_SMOKE] result=FAIL stage=secure-random\n");
        return;
    }
    for (size_t i = 0u; i < sizeof(randomPassword); ++i) {
        password[i * 2u] = kHex[randomPassword[i] >> 4u];
        password[i * 2u + 1u] = kHex[randomPassword[i] & 0x0fu];
    }
    volatile uint8_t* clearRandom = randomPassword;
    for (size_t i = 0u; i < sizeof(randomPassword); ++i) clearRandom[i] = 0u;

    authentication::SessionReference createdSession{};
    const authentication::Result bootstrapped =
        s_smokeAuthentication.bootstrapAdministrator("security2-smoke",
            "Security2 Smoke", password, sizeof(password) - 1u, &createdSession);
    const authentication::Result bootstrapSignOut = bootstrapped ==
            authentication::Result::OkVolatile
        ? s_smokeAuthentication.signOutCurrent() : authentication::Result::SessionUnavailable;
    if (bootstrapped != authentication::Result::OkVolatile ||
        bootstrapSignOut != authentication::Result::Ok) {
        volatile char* clearPassword = password;
        for (size_t i = 0u; i < sizeof(password); ++i) clearPassword[i] = 0;
        volatile uint8_t* clearSession = createdSession.bytes;
        for (size_t i = 0u; i < sizeof(createdSession.bytes); ++i) clearSession[i] = 0u;
        serial::puts("[SECURITY2_SMOKE] result=FAIL stage=volatile-enrollment bootstrap=");
        serial::puts(authentication::resultName(bootstrapped));
        serial::puts(" signout=");
        serial::puts(authentication::resultName(bootstrapSignOut));
        serial::putc('\n');
        return;
    }
    volatile uint8_t* clearCreatedSession = createdSession.bytes;
    for (size_t i = 0u; i < sizeof(createdSession.bytes); ++i) clearCreatedSession[i] = 0u;

    identity::IdentityContextV1 context{};
    if (s_smokeIdentities.queryContext(identity::ExecutionDomain::Interactive,
            &context) != identity::Result::Ok ||
        context.kind != identity::IdentityKind::InteractiveUnassigned) {
        volatile char* clearPassword = password;
        for (size_t i = 0u; i < sizeof(password); ++i) clearPassword[i] = 0;
        serial::puts("[SECURITY2_SMOKE] result=FAIL stage=signout-context\n");
        return;
    }

    authentication::SessionReference authenticatedSession{};
    const authentication::Result authenticated = s_smokeAuthentication.authenticate(
        "security2-smoke", password, sizeof(password) - 1u, &authenticatedSession);
    volatile char* clearPassword = password;
    for (size_t i = 0u; i < sizeof(password); ++i) clearPassword[i] = 0;
    if (authenticated != authentication::Result::Ok) {
        volatile uint8_t* clearSession = authenticatedSession.bytes;
        for (size_t i = 0u; i < sizeof(authenticatedSession.bytes); ++i) clearSession[i] = 0u;
        serial::puts("[SECURITY2_SMOKE] result=FAIL stage=credential-authentication\n");
        return;
    }
    authentication::SessionSnapshotV1 session{};
    const bool validSession = s_smokeAuthentication.queryCurrentSession(&session) ==
            authentication::Result::Ok &&
        session.state == authentication::SessionState::Active &&
        session.role == identity::AccountRole::Administrator;
    const bool signedOut = s_smokeAuthentication.signOutCurrent() == authentication::Result::Ok;
    volatile uint8_t* clearSession = authenticatedSession.bytes;
    for (size_t i = 0u; i < sizeof(authenticatedSession.bytes); ++i) clearSession[i] = 0u;
    if (!validSession || !signedOut) {
        serial::puts("[SECURITY2_SMOKE] result=FAIL stage=session-lifecycle\n");
        return;
    }
    serial::puts("[SECURITY2_SMOKE] production_psa_pbkdf2=pass volatile_enrollment=pass ");
    serial::puts("password_authentication=pass session_signout=pass result=PASS\n");
}

} // namespace kernel

#endif
