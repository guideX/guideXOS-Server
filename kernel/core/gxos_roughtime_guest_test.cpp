#include "include/gxos_roughtime_acquisition.h"
#include "include/gxos_roughtime_guest_test.h"

#include "include/kernel/serial_debug.h"
#include "roughtime_guest_vectors.h"

namespace {

static const uint32_t kFixtureAddress = 0x0a000202U; // QEMU user-net host alias 10.0.2.2
static const uint16_t kFixturePort = 5319U;

static bool check_vector(const uint8_t* response,
                         size_t response_length,
                         const uint8_t* expected_key,
                         const uint8_t* expected_nonce,
                         gxos_roughtime_v1_workspace* workspace,
                         gxos_roughtime_v1_evidence* evidence)
{
    const gxos_roughtime_acquisition_status status =
        gxos_roughtime_v1_dispatch_response(
            response, response_length,
            kFixtureAddress, kFixturePort,
            kFixtureAddress, kFixturePort,
            gxos_roughtime_fixture_request,
            sizeof(gxos_roughtime_fixture_request),
            expected_nonce, expected_key,
            10000000000ULL, 10100000000ULL,
            workspace, evidence);
    return status == GXOS_ROUGHTIME_ACQUISITION_SUCCESS;
}

static void write_case(const char* name, bool passed)
{
    kernel::serial::puts("[NAVHTTPS13-GUEST-A] case=");
    kernel::serial::puts(name);
    kernel::serial::puts(passed ? " result=PASS\n" : " result=FAIL\n");
}

} // namespace

extern "C" int gxos_roughtime_guest_selftest()
{
    gxos_roughtime_v1_workspace workspace;
    gxos_roughtime_v1_evidence evidence;
    uint8_t modified_response[sizeof(gxos_roughtime_fixture_response)];
    uint8_t wrong_key[32];
    uint8_t wrong_nonce[32];
    for (size_t i = 0; i < sizeof(wrong_key); ++i) wrong_key[i] = 0;
    for (size_t i = 0; i < sizeof(wrong_nonce); ++i) wrong_nonce[i] = 0xa5;

    const bool valid = check_vector(
        gxos_roughtime_fixture_response, sizeof(gxos_roughtime_fixture_response),
        gxos_roughtime_fixture_public_key, gxos_roughtime_fixture_nonce,
        &workspace, &evidence) &&
        evidence.signature_verified && evidence.nonce_matched &&
        evidence.merkle_proof_verified && !evidence.production_policy_accepted;
    write_case("independent-valid", valid);

    const bool wrong_key_rejected = !check_vector(
        gxos_roughtime_fixture_response, sizeof(gxos_roughtime_fixture_response),
        wrong_key, gxos_roughtime_fixture_nonce, &workspace, &evidence);
    write_case("wrong-pinned-key", wrong_key_rejected);

    for (size_t i = 0; i < sizeof(modified_response); ++i) {
        modified_response[i] = gxos_roughtime_fixture_response[i];
    }
    modified_response[68] ^= 1U; // First byte of the signed response signature.
    const bool bad_signature_rejected = !check_vector(
        modified_response, sizeof(modified_response),
        gxos_roughtime_fixture_public_key, gxos_roughtime_fixture_nonce,
        &workspace, &evidence);
    write_case("bad-signature", bad_signature_rejected);

    for (size_t i = 0; i < sizeof(modified_response); ++i) {
        modified_response[i] = gxos_roughtime_fixture_response[i];
    }
    modified_response[8] ^= 1U; // Corrupt the framed message length.
    const bool malformed_rejected = !check_vector(
        modified_response, sizeof(modified_response),
        gxos_roughtime_fixture_public_key, gxos_roughtime_fixture_nonce,
        &workspace, &evidence);
    write_case("malformed-framing", malformed_rejected);

    const bool wrong_nonce_rejected = !check_vector(
        gxos_roughtime_fixture_response, sizeof(gxos_roughtime_fixture_response),
        gxos_roughtime_fixture_public_key, wrong_nonce, &workspace, &evidence);
    write_case("nonce-mismatch", wrong_nonce_rejected);

    const bool passed = valid && wrong_key_rejected && bad_signature_rejected &&
        malformed_rejected && wrong_nonce_rejected;
    kernel::serial::puts("[NAVHTTPS13-GUEST-A] result=");
    kernel::serial::puts(passed ? "PASS" : "FAIL");
    kernel::serial::puts(" vectors=5 clock_policy=not-applied production_policy_accepted=0\n");
    return passed ? 0 : 1;
}
