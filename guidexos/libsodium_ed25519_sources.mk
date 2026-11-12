# guideXOS verification-only Ed25519 source profile.
# Upstream: libsodium 1.0.22, release commit 77e1ce5d6dee871c49ef211222ba18ef0c486bda.
# The selected subset and local extraction helpers are described in
# third_party/libsodium/README.md and pinned by libsodium_ed25519_sources.sha256.

GUIDEXOS_ED25519_LAYOUT_READY := 1
GUIDEXOS_ED25519_PROVIDER := libsodium
GUIDEXOS_ED25519_VERSION := 1.0.22
GUIDEXOS_ED25519_COMMIT := 77e1ce5d6dee871c49ef211222ba18ef0c486bda
GUIDEXOS_ED25519_ROOT := $(GUIDEXOS_REPO_ROOT)/third_party/libsodium/src

GUIDEXOS_ED25519_C_SOURCES := \
	$(GUIDEXOS_ED25519_ROOT)/crypto_sign/ed25519/ref10/open.c \
	$(GUIDEXOS_ED25519_ROOT)/crypto_core/ed25519/ref10/ed25519_ref10.c \
	$(GUIDEXOS_ED25519_ROOT)/crypto_hash/sha512/hash_sha512.c \
	$(GUIDEXOS_ED25519_ROOT)/crypto_hash/sha512/cp/hash_sha512_cp.c \
	$(GUIDEXOS_ED25519_ROOT)/crypto_verify/verify.c \
	$(GUIDEXOS_ED25519_ROOT)/guidexos/sign_hinit.c \
	$(GUIDEXOS_ED25519_ROOT)/guidexos/point_validate.c \
	$(GUIDEXOS_ED25519_ROOT)/guidexos/sodium_small_utils.c \
	$(GUIDEXOS_ED25519_ROOT)/guidexos/strict_wrapper.c

GUIDEXOS_ED25519_CFLAGS := \
	-I$(GUIDEXOS_REPO_ROOT)/kernel/core/include \
	-I$(GUIDEXOS_ED25519_ROOT)/include \
	-I$(GUIDEXOS_ED25519_ROOT)/include/sodium \
	-I$(GUIDEXOS_ED25519_ROOT)/crypto_hash/sha512 \
	-I$(GUIDEXOS_ED25519_ROOT)/crypto_hash/sha512/cp \
	-I$(GUIDEXOS_ED25519_ROOT)/crypto_sign/ed25519/ref10 \
	-I$(GUIDEXOS_ED25519_ROOT)/crypto_verify \
	-I$(GUIDEXOS_ED25519_ROOT)/crypto_core/ed25519 \
	-I$(GUIDEXOS_ED25519_ROOT)/crypto_core/ed25519/ref10 \
	-DCONFIGURED=1 -DSODIUM_STATIC -DHAVE_TI_MODE=1 -Dabort=__builtin_trap \
	-Wno-unused-function
