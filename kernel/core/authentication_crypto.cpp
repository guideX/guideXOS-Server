#include "include/kernel/authentication.h"

#include "../../gxos_tls_foundation.h"
#include "../../gxos_tls_prerequisites.h"
#include "include/kernel/pit.h"

#if defined(GXOS_SECURITY2_AUTH_SMOKE_TEST)
#include "include/kernel/serial_debug.h"
#endif

#if defined(GXOS_BARE_METAL)
#include "psa/crypto.h"
#endif

namespace kernel {
namespace authentication {
namespace {

#if defined(GXOS_BARE_METAL)
bool s_pbkdf2KatAttempted = false;
bool s_pbkdf2KatPassed = false;

bool runPbkdf2KnownAnswerTest()
{
    if (s_pbkdf2KatAttempted) return s_pbkdf2KatPassed;
    s_pbkdf2KatAttempted = true;
    static const uint8_t password[] = { 'p','a','s','s','w','o','r','d' };
    static const uint8_t salt[] = { 's','a','l','t' };
    static const uint8_t expected[32] = {
        0x12,0x0f,0xb6,0xcf,0xfc,0xf8,0xb3,0x2c,
        0x43,0xe7,0x22,0x52,0x56,0xc4,0xf8,0x37,
        0xa8,0x65,0x48,0xc9,0x2c,0xcc,0x35,0x48,
        0x08,0x05,0x98,0x7c,0xb7,0x0b,0xe1,0x7b
    };
    uint8_t output[sizeof(expected)]{};
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t status = psa_key_derivation_setup(&operation,
        PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_integer(&operation,
            PSA_KEY_DERIVATION_INPUT_COST, 1u);
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_bytes(&operation,
            PSA_KEY_DERIVATION_INPUT_SALT, salt, sizeof(salt));
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_bytes(&operation,
            PSA_KEY_DERIVATION_INPUT_PASSWORD, password, sizeof(password));
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_output_bytes(&operation, output, sizeof(output));
    const psa_status_t abortStatus = psa_key_derivation_abort(&operation);
    uint8_t difference = 0u;
    for (size_t i = 0u; i < sizeof(output); ++i) difference |= output[i] ^ expected[i];
    volatile uint8_t* clear = output;
    for (size_t i = 0u; i < sizeof(output); ++i) clear[i] = 0u;
    s_pbkdf2KatPassed = status == PSA_SUCCESS && abortStatus == PSA_SUCCESS &&
        difference == 0u;
#if defined(GXOS_SECURITY2_AUTH_SMOKE_TEST)
    if (!s_pbkdf2KatPassed) {
        serial::puts("[AUTH_CRYPTO_SMOKE] kat_status=");
        serial::put_hex32(static_cast<uint32_t>(status));
        serial::puts(" abort_status=");
        serial::put_hex32(static_cast<uint32_t>(abortStatus));
        serial::puts(" verifier_difference=");
        serial::put_hex32(difference);
        serial::putc('\n');
    }
#endif
    return s_pbkdf2KatPassed;
}
#endif

bool platformRandom(void*, uint8_t* output, size_t byteCount)
{
    if ((!output && byteCount != 0u) ||
        gxos::gxos_random_quality() != gxos::GxosRandomQuality::Secure) return false;
    return gxos::gxos_random_bytes(output, byteCount);
}

bool platformDerive(void*, Algorithm algorithm, uint32_t iterations,
                    const uint8_t* password, size_t passwordBytes,
                    const uint8_t* salt, size_t saltBytes,
                    uint8_t* verifier, size_t verifierBytes)
{
#if defined(GXOS_BARE_METAL)
    if (algorithm != Algorithm::Pbkdf2HmacSha256 ||
        iterations < kPbkdf2HmacSha256Iterations || iterations > 1200000u ||
        !password || passwordBytes == 0u || passwordBytes > kMaximumPasswordBytes ||
        !salt || saltBytes != kCredentialSaltBytes || !verifier ||
        verifierBytes != kCredentialVerifierBytes ||
        !gxos::gxos_tls_security_crypto_initialize()) return false;
    if (!runPbkdf2KnownAnswerTest()) {
#if defined(GXOS_SECURITY2_AUTH_SMOKE_TEST)
        serial::puts("[AUTH_CRYPTO_SMOKE] production_derivation=blocked-by-kat\n");
#endif
        return false;
    }

    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t status = psa_key_derivation_setup(&operation,
        PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_integer(&operation,
            PSA_KEY_DERIVATION_INPUT_COST, iterations);
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_bytes(&operation,
            PSA_KEY_DERIVATION_INPUT_SALT, salt, saltBytes);
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_input_bytes(&operation,
            PSA_KEY_DERIVATION_INPUT_PASSWORD, password, passwordBytes);
    if (status == PSA_SUCCESS)
        status = psa_key_derivation_output_bytes(&operation, verifier, verifierBytes);
    const psa_status_t abortStatus = psa_key_derivation_abort(&operation);
    if (status != PSA_SUCCESS || abortStatus != PSA_SUCCESS) {
#if defined(GXOS_SECURITY2_AUTH_SMOKE_TEST)
        serial::puts("[AUTH_CRYPTO_SMOKE] derivation_status=");
        serial::put_hex32(static_cast<uint32_t>(status));
        serial::puts(" abort_status=");
        serial::put_hex32(static_cast<uint32_t>(abortStatus));
        serial::putc('\n');
#endif
        volatile uint8_t* output = verifier;
        for (size_t i = 0u; i < verifierBytes; ++i) output[i] = 0u;
        return false;
    }
    return true;
#else
    (void)algorithm; (void)iterations; (void)password; (void)passwordBytes;
    (void)salt; (void)saltBytes; (void)verifier; (void)verifierBytes;
    return false;
#endif
}

uint64_t platformClockTicks(void*)
{
    return kernel::pit::ticks();
}

} // namespace

CryptoProvider platformCryptoProvider()
{
    CryptoProvider provider{};
    provider.context = nullptr;
    provider.randomBytes = platformRandom;
    provider.deriveVerifier = platformDerive;
    provider.clockTicks = platformClockTicks;
    return provider;
}

} // namespace authentication
} // namespace kernel
