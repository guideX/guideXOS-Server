# guideXOS bare-metal Mbed TLS build plan.
#
# kernel/Makefile includes this only after the official upstream source tree is
# imported under third_party/mbedtls. Keeping the source list here lets the repo
# describe the intended minimal freestanding subset without pretending the
# source drop or handshake wiring is already complete.

MBEDTLS_GUIDEXOS_IMPORT_ROOT := ../third_party/mbedtls
MBEDTLS_GUIDEXOS_INCLUDE_DIR := $(MBEDTLS_GUIDEXOS_IMPORT_ROOT)/include
MBEDTLS_GUIDEXOS_LIBRARY_DIR := $(MBEDTLS_GUIDEXOS_IMPORT_ROOT)/library
MBEDTLS_GUIDEXOS_CONFIG_HEADER := third_party/mbedtls/guidexos/mbedtls_config.h

MBEDTLS_GUIDEXOS_CFLAGS := -I.. -I$(MBEDTLS_GUIDEXOS_INCLUDE_DIR)
MBEDTLS_GUIDEXOS_CFLAGS += -DMBEDTLS_CONFIG_FILE='\"$(MBEDTLS_GUIDEXOS_CONFIG_HEADER)\"'

# Planned first-pass subset for TLS 1.2 client + X.509/PEM + bounded entropy /
# DRBG + common RSA/ECC public Web PKI chains. Public bare-metal HTTPS stays
# disabled until this compiles cleanly and a real validated handshake path is
# proven.
MBEDTLS_GUIDEXOS_C_SOURCES := \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/aes.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/asn1parse.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/asn1write.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/base64.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/bignum.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/cipher.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/cipher_wrap.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/constant_time.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/ctr_drbg.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/debug.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/entropy.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/gcm.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/md.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/memory_buffer_alloc.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/oid.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/pem.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/pk.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/pk_wrap.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/pkparse.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/platform.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/platform_util.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/rsa.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/rsa_internal.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/sha256.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/sha512.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/ssl_ciphersuites.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/ssl_cli.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/ssl_msg.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/ssl_tls.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/timing.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/version.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/x509.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/x509_crt.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/ecp.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/ecp_curves.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/ecdh.c \
    $(MBEDTLS_GUIDEXOS_LIBRARY_DIR)/ecdsa.c
