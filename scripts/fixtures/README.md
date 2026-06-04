# Smoke Fixtures

These files are test-only assets for Navigator smoke coverage.

## navigator-smoke-root-ca-bundle.pem

- Purpose: smoke-only root CA bundle fixture for bare-metal TLS prerequisite and local HTTPS handshake coverage.
- Use: copied by `scripts/smoke-navigator-kernel.ps1` to the guest-visible path `/certs/ca-bundle.pem` during kernel smoke only.
- Scope: not loaded by default, not production trust, and not suitable for internet/public HTTPS browsing.
- Size: intentionally small and well below the existing 512 KiB CA bundle safety cap.
- Provenance: checked-in deterministic PEM fixture matching the `guidexos.test` bare-metal TLS smoke certificate chain material in this repository.

## navigator-smoke-localhost.crt / navigator-smoke-localhost.key

Hosted HTTPS smoke server certificate/key pair for localhost-only test coverage.

## navigator-smoke-guidexos.test.crt / navigator-smoke-guidexos.test.key

Bare-metal local HTTPS smoke server certificate/key pair for the QEMU-reachable `guidexos.test` hostname.
