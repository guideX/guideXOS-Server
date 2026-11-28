/*
 * Hosted regression for the guideXOS Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0
 * profile (guidexos/mbedtls_config.h + guidexos/crypto_config.h + the tracked
 * patches/mbedtls series).
 *
 * The same 57 sources the kernel links are compiled for the Windows host with
 * the same guideXOS configuration pair; only the platform hooks the kernel
 * supplies (allocator, time, RNG, zeroize, gmtime) are replaced with host
 * equivalents below.  Modes:
 *
 *   crypto <data-dir>   ECDSA vectors, ECDSA sign/verify, OpenSSL cross-check,
 *                       RSA PKCS#1 v1.5 / PSS verification and X.509 chain
 *                       validation against files produced by the runner.
 *   tls <port> <host> <ca.pem> <ok|fail> [flags-hex]
 *                       One TLS 1.2 client handshake to a local server.
 */
#define _CRT_RAND_S
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mbedtls/build_info.h"
#include "mbedtls/platform.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ssl.h"
#include "mbedtls/error.h"
#include "psa/crypto.h"

/* The shared vector header is written for C++; give its struct a C name. */
typedef struct GxosPhase8jEcdsaVector GxosPhase8jEcdsaVector;
#include "../phase8j_ecdsa_vectors.h"

/* ---- platform hooks normally provided by gxos_tls_foundation.cpp ---- */
void* gxos_mbedtls_platform_calloc_uninit(size_t n, size_t s) { return calloc(n, s); }
void gxos_mbedtls_platform_free_uninit(void* p) { free(p); }
int gxos_mbedtls_platform_snprintf_noop(char* s, size_t n, const char* f, ...)
{
    va_list a; va_start(a, f); int r = vsnprintf(s, n, f, a); va_end(a); return r;
}
int gxos_mbedtls_platform_vsnprintf_noop(char* s, size_t n, const char* f, va_list a)
{
    return vsnprintf(s, n, f, a);
}
int gxos_mbedtls_platform_fprintf_noop(FILE* st, const char* f, ...)
{
    va_list a; va_start(a, f); int r = vfprintf(st, f, a); va_end(a); return r;
}
void gxos_mbedtls_platform_exit_noop(int status) { exit(status); }
int64_t gxos_mbedtls_time_callback(int64_t* timer)
{
    int64_t now = (int64_t)time(NULL);
    if (timer) *timer = now;
    return now;
}
mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t)GetTickCount(); } /* profile sets _WIN32_WINNT=0x0500 */
void mbedtls_platform_zeroize(void* buf, size_t len)
{
    if (buf && len) SecureZeroMemory(buf, len);
}
void mbedtls_zeroize_and_free(void* buf, size_t len)
{
    if (buf) { mbedtls_platform_zeroize(buf, len); mbedtls_free(buf); }
}
struct tm* mbedtls_platform_gmtime_r(const mbedtls_time_t* tt, struct tm* tm_buf)
{
    time_t t = (time_t)*tt;
    return gmtime_s(tm_buf, &t) == 0 ? tm_buf : NULL;
}
psa_status_t mbedtls_psa_external_get_random(mbedtls_psa_external_random_context_t* ctx,
                                             uint8_t* out, size_t size, size_t* out_len)
{
    (void)ctx;
    for (size_t i = 0; i < size; i += 4) {
        unsigned int v;
        if (rand_s(&v) != 0) return PSA_ERROR_INSUFFICIENT_ENTROPY;
        for (size_t k = 0; k < 4 && i + k < size; ++k) out[i + k] = (uint8_t)(v >> (8 * k));
    }
    *out_len = size;
    return PSA_SUCCESS;
}
/* Trace hooks referenced by the guideXOS patch series when smoke tracing is on. */
void gxos_tls_smoke_debug_trace(const char* event, uint32_t value) { (void)event; (void)value; }

/* ---- test bookkeeping ---- */
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned char* read_file(const char* dir, const char* name, size_t* len, int nul)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s\\%s", dir, name);
    FILE* f = fopen(path, "rb");
    if (!f) { printf("missing input %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* b = (unsigned char*)malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    b[n] = 0;
    *len = (size_t)n + (nul ? 1 : 0);
    return b;
}

/* ---- 1. Main's fixed ECDSA vectors through PSA (p256-m + built-in fallback) ---- */
static psa_key_id_t import_ec_public(const uint8_t* key, size_t len, size_t bits, psa_algorithm_t alg)
{
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&a, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&a, bits);
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&a, alg);
    psa_key_id_t id = 0;
    return psa_import_key(&a, key, len, &id) == PSA_SUCCESS ? id : 0;
}

static void test_fixed_vectors(void)
{
    for (size_t i = 0; i < kGxosPhase8jEcdsaVectorCount; ++i) {
        const GxosPhase8jEcdsaVector* v = &kGxosPhase8jEcdsaVectors[i];
        psa_algorithm_t alg = PSA_ALG_ECDSA(v->bits == 256 ? PSA_ALG_SHA_256 : PSA_ALG_SHA_384);
        psa_key_id_t id = import_ec_public(v->publicKey, v->publicKeyLength, v->bits, alg);
        CHECK(id != 0, "%s public key import", v->name);
        if (!id) continue;
        CHECK(psa_verify_hash(id, alg, v->hash, v->hashLength, v->signature, v->signatureLength) == PSA_SUCCESS,
              "%s valid vector accepted", v->name);
        uint8_t sig[132], hash[64];
        memcpy(sig, v->signature, v->signatureLength);
        memcpy(hash, v->hash, v->hashLength);
        sig[v->signatureLength / 4] ^= 0x01;
        CHECK(psa_verify_hash(id, alg, hash, v->hashLength, sig, v->signatureLength) == PSA_ERROR_INVALID_SIGNATURE,
              "%s corrupted r rejected", v->name);
        memcpy(sig, v->signature, v->signatureLength);
        sig[v->signatureLength - 1] ^= 0x80;
        CHECK(psa_verify_hash(id, alg, hash, v->hashLength, sig, v->signatureLength) == PSA_ERROR_INVALID_SIGNATURE,
              "%s corrupted s rejected", v->name);
        memcpy(sig, v->signature, v->signatureLength);
        hash[0] ^= 0x01;
        CHECK(psa_verify_hash(id, alg, hash, v->hashLength, sig, v->signatureLength) == PSA_ERROR_INVALID_SIGNATURE,
              "%s modified hash rejected", v->name);
        /* r = 0 and s = 0 must never verify. */
        memset(sig, 0, v->signatureLength);
        CHECK(psa_verify_hash(id, alg, v->hash, v->hashLength, sig, v->signatureLength) != PSA_SUCCESS,
              "%s zero signature rejected", v->name);
        psa_destroy_key(id);
    }
}

/* ---- 2. Generated keys: sign/verify round trips and mutations ---- */
static void test_sign_verify_round_trips(int iterations)
{
    const size_t curves[2] = { 256, 384 };
    for (int c = 0; c < 2; ++c) {
        size_t bits = curves[c];
        psa_algorithm_t halg = bits == 256 ? PSA_ALG_SHA_256 : PSA_ALG_SHA_384;
        psa_algorithm_t alg = PSA_ALG_ECDSA(halg);
        psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(&a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
        psa_set_key_bits(&a, bits);
        psa_set_key_usage_flags(&a, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH);
        psa_set_key_algorithm(&a, alg);
        int ok = 0, rejected = 0, keys = 0;
        for (int k = 0; k < 8; ++k) {
            psa_key_id_t id = 0;
            if (psa_generate_key(&a, &id) != PSA_SUCCESS) { CHECK(0, "P-%u key generation", (unsigned)bits); break; }
            ++keys;
            /* Verify through a separately imported public key so the
             * verification path does not share state with signing. */
            uint8_t pub[200]; size_t pub_len = 0;
            psa_export_public_key(id, pub, sizeof(pub), &pub_len);
            psa_key_id_t vid = import_ec_public(pub, pub_len, bits, alg);
            for (int i = 0; i < iterations; ++i) {
                uint8_t msg[64], hash[64], sig[132];
                size_t hlen = 0, slen = 0;
                mbedtls_psa_external_get_random(NULL, msg, sizeof(msg), &hlen);
                psa_hash_compute(halg, msg, sizeof(msg), hash, sizeof(hash), &hlen);
                if (psa_sign_hash(id, alg, hash, hlen, sig, sizeof(sig), &slen) != PSA_SUCCESS) {
                    CHECK(0, "P-%u sign", (unsigned)bits); continue;
                }
                if (psa_verify_hash(vid, alg, hash, hlen, sig, slen) == PSA_SUCCESS) ++ok;
                sig[(size_t)i % slen] ^= (uint8_t)(1u << (i % 8));
                if (psa_verify_hash(vid, alg, hash, hlen, sig, slen) == PSA_ERROR_INVALID_SIGNATURE) ++rejected;
            }
            psa_destroy_key(vid);
            psa_destroy_key(id);
        }
        CHECK(ok == keys * iterations, "P-%u: %d/%d valid signatures accepted", (unsigned)bits, ok, keys * iterations);
        CHECK(rejected == keys * iterations, "P-%u: %d/%d mutated signatures rejected", (unsigned)bits, rejected, keys * iterations);
        printf("ROUNDTRIP P-%u keys=%d valid=%d rejected=%d\n", (unsigned)bits, keys, ok, rejected);
    }
}

/* ---- 3. OpenSSL-produced signatures through the pk layer (independent signer) ---- */
static void test_openssl_cross_check(const char* dir)
{
    struct { const char* key; const char* list; mbedtls_md_type_t md; psa_algorithm_t halg; } sets[] = {
        { "ec256-pub.der", "ec256-sigs.txt", MBEDTLS_MD_SHA256, PSA_ALG_SHA_256 },
        { "ec384-pub.der", "ec384-sigs.txt", MBEDTLS_MD_SHA384, PSA_ALG_SHA_384 },
        { "rsa-pub.der",   "rsa-pkcs1-sigs.txt", MBEDTLS_MD_SHA256, PSA_ALG_SHA_256 },
    };
    for (size_t s = 0; s < sizeof(sets) / sizeof(sets[0]); ++s) {
        size_t klen = 0, llen = 0;
        unsigned char* key = read_file(dir, sets[s].key, &klen, 0);
        unsigned char* list = read_file(dir, sets[s].list, &llen, 1);
        CHECK(key && list, "cross-check inputs %s", sets[s].key);
        if (!key || !list) continue;
        mbedtls_pk_context pk; mbedtls_pk_init(&pk);
        int pr = mbedtls_pk_parse_public_key(&pk, key, klen);
        CHECK(pr == 0, "parse %s (-0x%04x)", sets[s].key, (unsigned)-pr);
        int good = 0, bad = 0, total = 0;
        char* save = NULL;
        for (char* line = strtok_s((char*)list, "\r\n", &save); line; line = strtok_s(NULL, "\r\n", &save)) {
            /* line: <message-file> <signature-file> */
            char mname[256], sname[256];
            if (sscanf(line, "%255s %255s", mname, sname) != 2) continue;
            size_t mlen = 0, siglen = 0, hlen = 0;
            unsigned char* m = read_file(dir, mname, &mlen, 0);
            unsigned char* sig = read_file(dir, sname, &siglen, 0);
            if (!m || !sig) { free(m); free(sig); continue; }
            uint8_t hash[64];
            psa_hash_compute(sets[s].halg, m, mlen, hash, sizeof(hash), &hlen);
            ++total;
            if (mbedtls_pk_verify(&pk, sets[s].md, hash, hlen, sig, siglen) == 0) ++good;
            hash[hlen - 1] ^= 0x01;
            if (mbedtls_pk_verify(&pk, sets[s].md, hash, hlen, sig, siglen) != 0) ++bad;
            free(m); free(sig);
        }
        CHECK(total > 0 && good == total, "%s: %d/%d OpenSSL signatures verified", sets[s].list, good, total);
        CHECK(total > 0 && bad == total, "%s: %d/%d wrong-hash checks rejected", sets[s].list, bad, total);
        printf("CROSSCHECK %s verified=%d rejected=%d total=%d\n", sets[s].list, good, bad, total);
        mbedtls_pk_free(&pk);
        free(key); free(list);
    }

    /* RSA-PSS (the scheme the guideXOS TLS 1.2 compatibility exception admits). */
    size_t klen = 0, mlen = 0, slen = 0, hlen = 0;
    unsigned char* key = read_file(dir, "rsa-pub.der", &klen, 0);
    unsigned char* m = read_file(dir, "msg-00.bin", &mlen, 0);
    unsigned char* sig = read_file(dir, "rsa-pss-00.sig", &slen, 0);
    if (key && m && sig) {
        psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(&a, PSA_KEY_TYPE_RSA_PUBLIC_KEY);
        psa_set_key_usage_flags(&a, PSA_KEY_USAGE_VERIFY_HASH);
        psa_set_key_algorithm(&a, PSA_ALG_RSA_PSS_ANY_SALT(PSA_ALG_SHA_256));
        /* PSA wants the RSAPublicKey; the SubjectPublicKeyInfo header of a
         * 2048-bit key is 24 bytes. */
        psa_key_id_t id = 0;
        psa_status_t st = psa_import_key(&a, key + 24, klen - 24, &id);
        CHECK(st == PSA_SUCCESS, "RSA-PSS key import (%d)", (int)st);
        uint8_t hash[32];
        psa_hash_compute(PSA_ALG_SHA_256, m, mlen, hash, sizeof(hash), &hlen);
        CHECK(psa_verify_hash(id, PSA_ALG_RSA_PSS_ANY_SALT(PSA_ALG_SHA_256), hash, hlen, sig, slen) == PSA_SUCCESS,
              "OpenSSL RSA-PSS signature verified");
        sig[slen / 2] ^= 0x04;
        CHECK(psa_verify_hash(id, PSA_ALG_RSA_PSS_ANY_SALT(PSA_ALG_SHA_256), hash, hlen, sig, slen) == PSA_ERROR_INVALID_SIGNATURE,
              "corrupted RSA-PSS signature rejected");
        psa_destroy_key(id);
    } else {
        CHECK(0, "RSA-PSS inputs present");
    }
    free(key); free(m); free(sig);
}

/* ---- 4. X.509 chain validation ---- */
static int load_crt(mbedtls_x509_crt* crt, const char* dir, const char* name)
{
    size_t len = 0;
    unsigned char* b = read_file(dir, name, &len, 1);
    if (!b) return -1;
    int r = mbedtls_x509_crt_parse(crt, b, len);
    free(b);
    return r;
}

static void expect_verify(const char* dir, const char* chain_file, const char* ca_file,
                          const char* host, uint32_t expected_flags, const char* label)
{
    mbedtls_x509_crt chain, ca;
    mbedtls_x509_crt_init(&chain); mbedtls_x509_crt_init(&ca);
    int lr = load_crt(&chain, dir, chain_file);
    int cr = load_crt(&ca, dir, ca_file);
    CHECK(lr == 0 && cr == 0, "%s: parse inputs (%d,%d)", label, lr, cr);
    uint32_t flags = 0;
    int r = mbedtls_x509_crt_verify(&chain, &ca, NULL, host, &flags, NULL, NULL);
    if (expected_flags == 0) {
        CHECK(r == 0 && flags == 0, "%s: expected success, ret=-0x%04x flags=0x%08x", label, (unsigned)-r, flags);
    } else {
        CHECK(r != 0 && (flags & expected_flags) == expected_flags,
              "%s: expected flags 0x%08x, ret=-0x%04x flags=0x%08x", label, expected_flags, (unsigned)-r, flags);
    }
    printf("X509 %-34s ret=-0x%04x flags=0x%08x\n", label, (unsigned)-r, flags);
    mbedtls_x509_crt_free(&chain); mbedtls_x509_crt_free(&ca);
}

static void test_x509(const char* dir)
{
    expect_verify(dir, "ec-leaf-chain.pem", "root.pem", "tls.guidexos.test", 0, "EC leaf via EC intermediate");
    expect_verify(dir, "rsa-leaf-chain.pem", "root.pem", "rsa.guidexos.test", 0, "RSA leaf via RSA root");
    expect_verify(dir, "ec-leaf-chain.pem", "root.pem", "wrong.guidexos.test", MBEDTLS_X509_BADCERT_CN_MISMATCH, "hostname mismatch");
    expect_verify(dir, "ec-leaf-chain.pem", "other-root.pem", "tls.guidexos.test", MBEDTLS_X509_BADCERT_NOT_TRUSTED, "untrusted root");
    expect_verify(dir, "expired-leaf-chain.pem", "root.pem", "expired.guidexos.test", MBEDTLS_X509_BADCERT_EXPIRED, "expired leaf");
    expect_verify(dir, "tampered-leaf-chain.pem", "root.pem", "tls.guidexos.test", MBEDTLS_X509_BADCERT_NOT_TRUSTED, "tampered leaf signature");
    expect_verify(dir, "ec-leaf-only.pem", "root.pem", "tls.guidexos.test", MBEDTLS_X509_BADCERT_NOT_TRUSTED, "missing intermediate");
    expect_verify(dir, "noca-intermediate-chain.pem", "root.pem", "noca.guidexos.test", MBEDTLS_X509_BADCERT_NOT_TRUSTED, "intermediate without CA bit");
}

/* ---- 5. TLS 1.2 client handshake ---- */
static int net_send(void* ctx, const unsigned char* buf, size_t len)
{
    int r = send(*(SOCKET*)ctx, (const char*)buf, (int)len, 0);
    return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : r;
}
static int net_recv(void* ctx, unsigned char* buf, size_t len)
{
    int r = recv(*(SOCKET*)ctx, (char*)buf, (int)len, 0);
    if (r == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    return r < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : r;
}

static int run_tls(const char* port, const char* host, const char* ca_path, int expect_ok, uint32_t expect_flags)
{
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    getaddrinfo("127.0.0.1", port, &hints, &res);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!res || connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) { printf("TLS connect failed\n"); return 2; }
    freeaddrinfo(res);

    char dir[1024]; strncpy(dir, ca_path, sizeof(dir) - 1); dir[sizeof(dir) - 1] = 0;
    char* slash = strrchr(dir, '\\'); const char* name = ca_path;
    if (slash) { *slash = 0; name = slash + 1; } else { strcpy(dir, "."); }
    mbedtls_x509_crt ca; mbedtls_x509_crt_init(&ca);
    if (load_crt(&ca, dir, name) != 0) { printf("TLS CA load failed\n"); return 2; }

    mbedtls_ssl_config conf; mbedtls_ssl_context ssl;
    mbedtls_ssl_config_init(&conf); mbedtls_ssl_init(&ssl);
    int r = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &ca, NULL);
    if (r == 0) r = mbedtls_ssl_setup(&ssl, &conf);
    if (r == 0) r = mbedtls_ssl_set_hostname(&ssl, host);
    mbedtls_ssl_set_bio(&ssl, &s, net_send, net_recv, NULL);
    if (r == 0) r = mbedtls_ssl_handshake(&ssl);
    uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
    int ok = 0;
    if (r == 0) {
        const unsigned char ping[] = "guidexos-recovery2-ping\n";
        int wr = mbedtls_ssl_write(&ssl, ping, sizeof(ping) - 1);
        ok = wr == (int)(sizeof(ping) - 1);
        printf("TLS handshake=OK suite=%s version=%s write=%d\n",
               mbedtls_ssl_get_ciphersuite(&ssl), mbedtls_ssl_get_version(&ssl), wr);
        mbedtls_ssl_close_notify(&ssl);
    } else {
        printf("TLS handshake=FAIL ret=-0x%04x verify_flags=0x%08x\n", (unsigned)-r, flags);
    }
    int pass = expect_ok ? ok : (r != 0 && (flags & expect_flags) == expect_flags);
    printf("TLS_CASE_RESULT=%s\n", pass ? "PASS" : "FAIL");
    mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&ca);
    closesocket(s); WSACleanup();
    return pass ? 0 : 1;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (psa_crypto_init() != PSA_SUCCESS) { printf("psa_crypto_init failed\n"); return 2; }
    if (argc >= 3 && strcmp(argv[1], "crypto") == 0) {
        test_fixed_vectors();
        test_sign_verify_round_trips(64);
        test_openssl_cross_check(argv[2]);
        test_x509(argv[2]);
        printf("MBEDTLS_PROFILE_HOSTED_CHECKS=%d FAILURES=%d RESULT=%s\n",
               g_pass + g_fail, g_fail, g_fail == 0 ? "PASS" : "FAIL");
        return g_fail == 0 ? 0 : 1;
    }
    if (argc >= 6 && strcmp(argv[1], "tls") == 0) {
        uint32_t f = argc >= 7 ? (uint32_t)strtoul(argv[6], NULL, 16) : 0;
        return run_tls(argv[2], argv[3], argv[4], strcmp(argv[5], "ok") == 0, f);
    }
    printf("usage: crypto <dir> | tls <port> <host> <ca.pem> <ok|fail> [flags]\n");
    return 2;
}
