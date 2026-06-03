guideXOS bare-metal TLS expects the official Mbed TLS source tree to be unpacked at:

`third_party/mbedtls`

The minimum expected layout is:

- `third_party/mbedtls/include/mbedtls/version.h`
- `third_party/mbedtls/include/mbedtls/ssl.h`
- `third_party/mbedtls/include/mbedtls/x509_crt.h`
- `third_party/mbedtls/library/`
- `third_party/mbedtls/guidexos/mbedtls_config.h`

Recommended initial import:

- Official Mbed TLS `2.28.9` LTS source release

Why this target:

- guideXOS only needs a conservative TLS 1.2 client bring-up first
- 2.28 LTS is a smaller and lower-risk starting point than a broader 3.x upgrade
- the current Navigator milestone keeps bare-metal `https://` disabled while CA parsing, hostname validation, memory bounds, and handshake wiring are proven

Import steps for a future local source drop:

1. Unpack the official release so `third_party/mbedtls/include/mbedtls/version.h` exists.
2. Preserve `third_party/mbedtls/guidexos/mbedtls_config.h`.
3. Keep the upstream tree isolated under `third_party/mbedtls`; do not scatter files into `kernel/` or shared app code.
4. Keep the guideXOS build plan in `third_party/mbedtls/guidexos/mbedtls_sources.mk`; `kernel/Makefile` consumes it only after `include/mbedtls/version.h` is present.
5. Wire only the required library subset for TLS client, X.509, PEM, entropy, time, and bounded allocator hooks.
6. Keep public bare-metal HTTPS navigation disabled until local handshake and validation smokes pass deterministically.
