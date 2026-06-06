# Smoke Fixtures

These files are test-only assets for Navigator smoke coverage.

## navigator-smoke-root-ca-bundle.pem

- Purpose: smoke-only root CA bundle fixture for bare-metal TLS prerequisite and local HTTPS handshake coverage.
- Use: copied by `scripts/smoke-navigator-kernel.ps1` to the guest-visible path `/certs/ca-bundle.pem` during kernel smoke only.
- Scope: not loaded by default, not production trust, and not suitable for internet/public HTTPS browsing.
- Size: intentionally small and well below the existing 512 KiB CA bundle safety cap.
- Provenance: checked-in deterministic PEM fixture matching the `guidexos.test` bare-metal TLS smoke certificate chain material in this repository.

## navigator-validated-root-ca-bundle.pem

- Purpose: deterministic non-smoke CA bundle fixture for explicit `UserTrustStoreDevMode` and `ProductionValidated` policy coverage.
- Use: copied by `scripts/smoke-navigator-kernel.ps1` to `/config/certs/ca-bundle.pem` or `/certs/ca-bundle.pem` depending on the scenario under test.
- Scope: fixture-only policy coverage for repository smoke, not a public internet trust bundle.
- Provenance: dedicated validated-policy root CA for the `dev.guidexos.test` and `prod.guidexos.test` deterministic HTTPS fixtures.

## navigator-smoke-localhost.crt / navigator-smoke-localhost.key

Hosted HTTPS smoke server certificate/key pair for localhost-only test coverage.

## navigator-smoke-guidexos.test.crt / navigator-smoke-guidexos.test.key

Bare-metal local HTTPS smoke server certificate/key pair for the QEMU-reachable `guidexos.test` hostname.

## navigator-policy-dev.guidexos.test.crt / navigator-policy-dev.guidexos.test.key

Bare-metal validated HTTPS fixture certificate/key pair for `UserTrustStoreDevMode` smoke coverage against the QEMU-reachable `dev.guidexos.test` hostname.

## navigator-policy-prod.guidexos.test.crt / navigator-policy-prod.guidexos.test.key

Bare-metal validated HTTPS fixture certificate/key pair for `ProductionValidated` smoke coverage against the QEMU-reachable `prod.guidexos.test` hostname.

## navigator-public-pilot.guidexos.test.crt / navigator-public-pilot.guidexos.test.key

Bare-metal controlled public HTTPS pilot fixture certificate/key pair for deterministic `ProductionValidated + public-https-pilot=enabled` smoke coverage against the QEMU-reachable `public-pilot.guidexos.test` hostname.

## navigator-fault-untrusted-guidexos.test.crt / navigator-fault-untrusted-guidexos.test.key

Bare-metal HTTPS smoke certificate/key pair for deterministic untrusted-root validation failure coverage against the same `guidexos.test` host.

## navigator-fault-untrusted-dev.guidexos.test.crt / navigator-fault-untrusted-dev.guidexos.test.key

Bare-metal validated HTTPS fixture certificate/key pair for deterministic `UserTrustStoreDevMode` untrusted-root failure coverage against `dev.guidexos.test`.

## navigator-fault-untrusted-prod.guidexos.test.crt / navigator-fault-untrusted-prod.guidexos.test.key

Bare-metal validated HTTPS fixture certificate/key pair for deterministic `ProductionValidated` untrusted-root failure coverage against `prod.guidexos.test`.

## navigator-fault-expired-guidexos.test.crt / navigator-fault-expired-guidexos.test.key

Bare-metal HTTPS smoke certificate/key pair signed by the validated smoke CA but already expired relative to the QEMU RTC used by kernel smoke.

## navigator-fault-expired-prod.guidexos.test.crt / navigator-fault-expired-prod.guidexos.test.key

Bare-metal validated HTTPS fixture certificate/key pair signed by the validated-policy CA but already expired relative to the QEMU RTC used by kernel smoke.

## navigator-fault-future-guidexos.test.crt / navigator-fault-future-guidexos.test.key

Bare-metal HTTPS smoke certificate/key pair signed by the validated smoke CA but not yet valid. Reserved for future/not-yet-valid fault coverage.

## navigator-malformed-ca-bundle.pem / navigator-empty-ca-bundle.pem

Smoke-only malformed and empty CA bundle fixtures for deterministic fail-closed trust-store parser coverage.

## Opt-in real public HTTPS probe

- `scripts/smoke-navigator-kernel.ps1` keeps the real public HTTPS probe off by default so the normal deterministic hosted/kernel smoke stays internet-independent and green without any secret/public-root material.
- `scripts/smoke-navigator-public-https.ps1` is the dedicated single-purpose entrypoint when you want to prove the real public HTTPS probe path itself.
- `scripts/smoke-navigator-public-https.bat` is the Windows convenience wrapper; it forwards to the PowerShell entrypoint with `-NoProfile -ExecutionPolicy Bypass` and does not duplicate logic.
- Set `GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS=1` to enable the optional bare-metal public probe.
- Optionally set `GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL` to override the default target `https://sha256.badssl.com/`. The older `GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_TARGET` name is still accepted for compatibility.
- Provide a public-root PEM bundle explicitly through either:
  - `GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE=C:\path\to\ca-bundle.pem`, or
  - the conventional ignored local file `scripts/fixtures/public-roots/ca-bundle.pem.local` (copy `scripts/fixtures/public-roots/ca-bundle.pem.example` and replace its placeholder text with real roots).
- The dedicated script prefers `GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE` when both the env var path and `scripts/fixtures/public-roots/ca-bundle.pem.local` are present, and it reports that choice clearly.
- The staging flow validates the explicit public-root PEM locally before building the ramdisk:
  - the file must exist and be readable;
  - it must stay within the existing 512 KiB CA bundle safety cap;
  - it must contain at least one PEM certificate;
  - each PEM certificate must decode as an X.509 certificate.
- When a valid public-root bundle is provided for the real public probe, the packer appends it to the deterministic validated fixture roots for smoke coverage, stages the merged result at `/certs/ca-bundle.pem`, and writes companion `/config/navigator/real-public-https-ca-bundle-*.txt` metadata so the guest only marks `public_trust_ready=yes` for the explicit opt-in path.
- With only deterministic fixture roots staged, the optional real public probe stays `SKIP` and reports that deterministic smoke trust is not public internet trust.
- Set `GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS=1` to make probe blockers or failures fail the smoke run instead of reporting `SKIP`.
- `PASS` means DNS, TCP, TLS handshake, certificate validation, hostname validation, and the policy-validated Navigator HTTPS path all succeeded without plaintext fallback.
- `SKIP` means the probe was not enabled, the explicit public-root bundle was not staged, or the environment/network was unavailable and the probe was not required.
- `FAIL` means the probe was required or attempted and a blocker, validation error, or transport failure prevented success.
- The dedicated script uses stricter proof semantics:
  - exit `0`: the real public probe reported `PASS`;
  - exit `2`: setup/preflight blocker such as missing roots or an invalid target URL;
  - exit `3`: the guest probe reported `SKIP`, which is not accepted as proof by the dedicated entrypoint;
  - exit `1`: the guest probe reported `FAIL` or the harness could not complete.
- The dedicated script rejects non-`https://` targets and numeric-IP targets before QEMU launches.
- The dedicated script prints a compact final summary for manual runs and CI logs, including the target, CA source resolution, a sanitized CA source marker, CA bytes, parsed cert count, DNS/TCP/TLS, certificate and hostname validation, HTTP status, unsupported-content reason, `plaintext_fallback=no`, the final result, and a machine-checkable `result_marker`:
  - `PASS`
  - `FAIL`
  - `SKIP`
  - `SETUP_BLOCKED`
- Dedicated logs are written as:
  - `logs/navigator-public-https-<timestamp>.serial.log`
  - `logs/navigator-public-https-<timestamp>.summary.log`
- The summary log records the target URL, public CA source path, CA bytes, parsed cert count, DNS/TCP/TLS results, certificate and hostname validation results, verify flags, SNI host, HTTP status, content type, content encoding, unsupported-content reason, `plaintext_fallback=no`, and the final `PASS`/`SKIP`/`FAIL` outcome.

### CI and manual-secret contract

- Normal deterministic CI should keep using `scripts/smoke-navigator-kernel.ps1` and `scripts/smoke-navigator-hosted.ps1`; it should not make the dedicated public probe a required step.
- CI with no secret public-root bundle should expect `scripts/smoke-navigator-public-https.ps1` to exit `2` for setup/preflight blockers such as missing roots, so that step must stay optional unless secret material is injected intentionally.
- CI or manual runs that do want required proof of the public HTTPS path must provide real public-root PEM input through `GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE` or the ignored local fallback `scripts/fixtures/public-roots/ca-bundle.pem.local`.
- When both the env var path and the ignored local fallback exist, the env var wins and the dedicated script prints that precedence explicitly.
- Public roots are never downloaded by the harness and the `.local` fallback remains ignored by git.
- Deterministic fixture roots in this repository do not count as public trust and must never be treated as sufficient for the real public probe.
- This repository now includes an opt-in GitHub Actions workflow at `.github/workflows/navigator-public-https-probe.yml` for approved environments that can inject the repository secret `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM`.
- The workflow is manual-only via `workflow_dispatch`, not push-triggered, and uploads:
  - `logs/navigator-public-https-*.summary.log`
  - `logs/navigator-public-https-*.serial.log`
- The workflow writes the secret to a temporary runner file, points `GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE` at that file, and removes the file after the run where practical.
- The workflow can accept an optional manual `target_url` override; otherwise it uses `https://sha256.badssl.com/`.
- If `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM` is missing, the workflow fails clearly before the probe runs.

### Public HTTPS PASS Artifact Checklist

Review the uploaded `navigator-public-https-*.summary.log` and treat the run as proof only when all of the following are true:

- `result_marker=PASS`
- `final_result=PASS`
- `target_url=` is the expected `https://` URL for the run.
- `public_trust_ready=yes`
- `public_ca_source_marker=` shows an explicit source such as `env-var` or `env-var-preferred-over-local`.
- `public_ca_parsed_certs=` is greater than `0`.
- `dns_result=PASS`
- `tcp_result=PASS`
- `tls_result=PASS`
- `certificate_validation_result=PASS`
- `hostname_validation_result=PASS`
- `verify_flags=0`
- `sni_host=` matches the target hostname.
- `http_status=` shows that an HTTPS response was actually received.
- `plaintext_fallback=no`

Review notes:

- `SETUP_BLOCKED`, `SKIP`, and `FAIL` are useful diagnostics but are not proof of a working public HTTPS path.
- A content/browser limitation after TLS success, such as unsupported content encoding or unsupported compression handling, must not be mistaken for a CA, certificate, hostname, or TLS transport failure.
- Deterministic fixture roots still do not count as public trust, even if another field in the same log looks healthy.

### First-Run Operator Checklist

Use this when collecting the first GitHub-hosted PASS artifact for a branch, release note, or milestone checkpoint:

1. Push the branch that contains `.github/workflows/navigator-public-https-probe.yml`.
2. Confirm the `Navigator Public HTTPS Probe` workflow is visible in GitHub Actions for that branch or the default branch workflow view.
3. Add or confirm the repository secret `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM`.
4. Use a real public-root PEM bundle that is appropriate for the chosen target URL.
5. Start the workflow manually with `workflow_dispatch`.
6. Leave the default target `https://sha256.badssl.com/` unless you intentionally need another HTTPS URL.
7. Download or inspect the uploaded `navigator-public-https-*.summary.log` and `navigator-public-https-*.serial.log` artifacts.
8. Confirm the full PASS artifact checklist above.
9. Archive or attach the summary artifact in release notes, development notes, or the milestone thread.

Operator warnings:

- Do not paste the secret PEM into logs, issues, pull requests, or release notes.
- Do not commit the PEM or any derived local `.local` bundle.
- Do not treat `SETUP_BLOCKED` or `SKIP` as evidence that public HTTPS is working.
- Do not enable default public bare-metal HTTPS based on a single PASS artifact alone.

### Example commands

- Deterministic smoke only, still internet-independent:
  - `.\scripts\smoke-navigator-kernel.ps1`
- Dedicated public probe smoke with the conventional ignored local bundle path:
  - `Copy-Item .\scripts\fixtures\public-roots\ca-bundle.pem.example .\scripts\fixtures\public-roots\ca-bundle.pem.local`
  - Replace the placeholder contents in `scripts\fixtures\public-roots\ca-bundle.pem.local` with real public roots.
  - `.\scripts\smoke-navigator-public-https.ps1`
- Dedicated public probe smoke through the wrapper:
  - `.\scripts\smoke-navigator-public-https.bat`
- Manual GitHub Actions execution:
  - Open the `Navigator Public HTTPS Probe` workflow in GitHub Actions.
  - Push the branch first if the workflow is new on that branch.
  - Add or confirm the repository secret `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM`.
  - Use a real public-root PEM bundle suitable for the selected target.
  - Optionally override `target_url`.
  - Start the manual run, download the uploaded summary/serial logs, and confirm the PASS artifact checklist.
- Dedicated public probe smoke with an explicit bundle path:
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE="C:\path\to\ca-bundle.pem"`
  - `.\scripts\smoke-navigator-public-https.ps1`
- Dedicated public probe smoke with an explicit bundle path and target override:
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE="C:\path\to\ca-bundle.pem"`
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL="https://sha256.badssl.com/"`
  - `.\scripts\smoke-navigator-public-https.ps1`
- Manual kernel smoke with the public probe enabled inside the broader deterministic suite:
  - `$env:GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS="1"`
  - `$env:GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS="1"`
  - `.\scripts\smoke-navigator-kernel.ps1`
- Manual kernel smoke with an explicit bundle path:
  - `$env:GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS="1"`
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE="C:\path\to\ca-bundle.pem"`
  - `$env:GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS="1"`
  - `.\scripts\smoke-navigator-kernel.ps1`
- Optional target override:
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL="https://sha256.badssl.com/"`

This probe remains smoke/manual validation only. It does not enable default public HTTPS browsing in bare-metal Navigator, and it does not download public roots for you.
