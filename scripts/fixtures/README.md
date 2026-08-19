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

## Trust material paths

- `/certs/ca-bundle.pem`
  - Guest-visible production-side trust bundle path.
  - Used for deterministic smoke fixture coverage and `ProductionValidated` policy staging.
  - During the opt-in real public probe, the packer stages a merged bundle here and now writes `/certs/ca-bundle.manifest` beside it.
- `/config/certs/ca-bundle.pem`
  - Guest-visible user/dev trust bundle path.
  - Used only for explicit `UserTrustStoreDevMode` coverage.
  - The packer now writes `/config/certs/ca-bundle.manifest` beside it when a user/dev bundle is staged.
- `/config/navigator/https-policy.txt`
  - Policy selector for bare-metal Navigator HTTPS mode.
  - Missing or invalid policy keeps the default-safe behavior; it does not silently broaden public trust.
- `scripts/generate-wallpaper-pack.ps1`
  - Host-side staging entrypoint that decides which trust bundle lands in the ramdisk.
  - It now validates staged CA bundles and emits manifest sidecars with `bundle_type`, SHA-256, root count, and `production_ready` / `test_only` status.
- `scripts/fixtures/public-roots/ca-bundle.pem.local`
  - Ignored local convention for explicit public-root material.
  - This is for manual/dev or intentionally secret-injected proof runs only; it is not a shipped trust store.

Trust source distinction in this repository today:

- smoke/test roots
  - checked-in deterministic fixtures such as `navigator-smoke-root-ca-bundle.pem`
  - marked `test_only=yes`
- user/dev roots
  - explicit operator-supplied roots staged at `/config/certs/ca-bundle.pem`
  - not default trust and not treated as production-ready
- production/public roots
  - explicit operator-supplied roots for the dedicated public probe
  - must be validated explicitly and recorded in a manifest before proof is accepted
- shipped-root candidates
  - explicit operator-supplied root bundles proposed for later runtime shipping review
  - prepared as commit-safe metadata plus manifest/evidence records without enabling default public HTTPS
- local secret CI roots
  - materialized from `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM` into a temporary runner file
  - validated and archived as manifest evidence during the manual workflow
- future shipped roots
  - not implemented in this pass
  - would require a reviewed provisioning and lifecycle policy before default public HTTPS changes

## Opt-in real public HTTPS probe

- `scripts/smoke-navigator-kernel.ps1` now defaults to the deterministic kernel smoke lane only, so the normal hosted/kernel smoke stays internet-independent and green without any secret/public-root material.
- Use `scripts/smoke-navigator-kernel.ps1 -IncludePublicPilot` when you intentionally want the broader kernel matrix to include the public-pilot scenario lane for debugging.
- Use `scripts/smoke-navigator-kernel.ps1 -ScenarioGroup PublicPilot` when you want only the public-pilot kernel lane without the rest of the deterministic matrix.
- Use `scripts/smoke-navigator-kernel.ps1 -ScenarioGroup PublicPilot -CandidateBundlePath C:\path\to\candidate.pem -CandidateRotationId candidate-2026-06` when you intentionally want to stage a shipped-root candidate in the public-pilot lane for reviewed runtime verification.
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
- `scripts/validate-navigator-ca-bundle.ps1` is the explicit host-side validator and manifest generator for this contract. It never prints PEM bodies and writes a manifest sidecar that records the bundle SHA-256, root count, optional subject/date summary, bundle type, and `production_ready` / `test_only` flags.
- When a valid public-root bundle is provided for the real public probe, the packer appends it to the deterministic validated fixture roots for smoke coverage, stages the merged result at `/certs/ca-bundle.pem`, and writes `/certs/ca-bundle.manifest` beside the staged PEM.
- With only deterministic fixture roots staged, the optional real public probe stays `SKIP` and reports that deterministic smoke trust is not public internet trust.
- Set `GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS=1` to make probe blockers or failures fail the smoke run instead of reporting `SKIP`.
- `PASS` means DNS, TCP, TLS handshake, certificate validation, hostname validation, and the policy-validated Navigator HTTPS path all succeeded without plaintext fallback.
  - Content limitations such as unsupported `Content-Encoding`, oversized headers, or oversized bodies may still be reported separately after successful TLS.
- `SKIP` means the probe was not enabled, the explicit public-root bundle was not staged, or the environment/network was unavailable and the probe was not required.
- `FAIL` means the probe was required or attempted and a blocker, validation error, or transport failure prevented success.
- The dedicated script uses stricter proof semantics:
  - exit `0`: the real public probe reported `PASS`;
  - exit `2`: setup/preflight blocker such as missing roots or an invalid target URL;
  - exit `3`: the guest probe reported `SKIP`, which is not accepted as proof by the dedicated entrypoint;
  - exit `1`: the guest probe reported `FAIL` or the harness could not complete.
- The dedicated script rejects non-`https://` targets, numeric-IP targets, and non-reviewed public targets before QEMU launches unless you intentionally use the explicit reviewed override path.
- The dedicated script prints a compact final summary for manual runs and CI logs, including the target, CA source resolution, a sanitized CA source marker, `public_trust_ready`, CA bytes, parsed cert count, DNS/TCP/TLS, certificate and hostname validation, compact transport diagnostics such as `tls_connect_attempts`, `tls_retry_count`, `tls_retry_reason`, `tls_bytes_written_before_retry`, `tls_handshake_error_code`, `tls_transport_error_code`, `tcp_abort_used`, `redirected_https_retry_used`, `redirect_hop_index`, and `redirect_hop_url`, plus HTTP status, compatibility-limit markers such as `header_cap_hit`, `body_cap_hit`, `downgrade_blocked`, `tls_succeeded_before_content_failure`, unsupported-content reason, `plaintext_fallback=no`, automated PASS assertion status, the final result, and a machine-checkable `result_marker`:
  - `PASS`
  - `FAIL`
  - `SKIP`
  - `SETUP_BLOCKED`
- Dedicated logs are written as:
  - `logs/navigator-public-https-<timestamp>.serial.log`
  - `logs/navigator-public-https-<timestamp>.summary.log`
- Dedicated root-manifest evidence is written as:
  - `logs/navigator-public-https-<timestamp>.ca-bundle.manifest`
- Structured evidence is promoted to:
  - `logs/navigator-public-https-<timestamp>.evidence.json`
- Reviewed real-root proof packs are written under:
  - `logs/navigator-public-https-proof-pack-<timestamp>/`
  - The pack includes the summary log, serial log, evidence JSON, candidate metadata when supplied, the CA bundle manifest when available, and any commit-safe promotion record.
- The summary log uses stable `[NAVIGATOR-PUBLIC-HTTPS] key=value` lines for machine checks. PASS-critical fields include `final_result`, `result_marker`, `target_url`, `target_host`, `public_ca_source_marker`, `public_trust_ready`, `public_ca_parsed_certs`, `trust_bundle_manifest_present`, `trust_bundle_sha256`, `trust_bundle_type`, `trust_bundle_root_count`, `trust_bundle_production_ready`, `trust_bundle_test_only`, `dns_result`, `tcp_result`, `tls_result`, `certificate_validation_result`, `hostname_validation_result`, `verify_flags`, `sni_host`, `http_status`, `header_cap_hit`, `body_cap_hit`, `downgrade_blocked`, `tls_succeeded_before_content_failure`, `plaintext_fallback`, and the automated `pass_contract_assertion_result`.
- Public pilot v0.5 classification fields separate transport proof from content/render compatibility:
  - `tls_failure_classification`
  - `tls_transport_proof_result`
  - `content_compatibility_result`
  - `page_render_result`
  - `real_world_compatibility_note`
- The summary and evidence now also record `reviewed_allowlist_name` and `reviewed_allowlist_version` so proof packs can be tied to the exact reviewed target contract.
- The reviewed-target matrix helper now emits one copied summary log and one copied evidence JSON per reviewed target, plus an aggregate allowlist summary log at `logs/navigator-public-https-allowlist-<timestamp>.summary.log`.
- If the real-root PEM is absent, the reviewed-target matrix remains `SETUP_BLOCKED` and the proof pack records that state instead of inventing a PASS.
- The automated validator lives at `scripts/assert-navigator-public-https-pass.ps1`. It verifies the PASS contract, exits `0` only for valid proof, and can be run manually against any uploaded summary artifact.
- The structured evidence exporter lives at `scripts/export-navigator-public-https-evidence.ps1`. It converts the summary artifact into JSON milestone evidence without copying PEM contents or any private material.

### CI and manual-secret contract

- Normal deterministic CI should keep using `scripts/smoke-navigator-kernel.ps1` and `scripts/smoke-navigator-hosted.ps1`; it should not make the dedicated public probe a required step.
- CI with no secret public-root bundle should expect `scripts/smoke-navigator-public-https.ps1` to exit `2` for setup/preflight blockers such as missing roots, so that step must stay optional unless secret material is injected intentionally.
- CI or manual runs that do want required proof of the public HTTPS path must provide real public-root PEM input through `GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE` or the explicit local fallback `scripts/fixtures/public-roots/ca-bundle.pem.local`.
- When both the env var path and the ignored local fallback exist, the env var wins and the dedicated script prints that precedence explicitly.
- Public roots are never downloaded by the harness and the `.local` fallback remains ignored by git.
- Deterministic fixture roots in this repository do not count as public trust and must never be treated as sufficient for the real public probe.
- This repository now includes an opt-in GitHub Actions workflow at `.github/workflows/navigator-public-https-probe.yml` for approved environments that can inject the repository secret `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM`.
- The workflow is manual-only via `workflow_dispatch`, not push-triggered, and uploads:
  - `logs/navigator-public-https-workflow-input.ca-bundle.manifest`
  - `logs/navigator-public-https-*.summary.log`
  - `logs/navigator-public-https-*.serial.log`
  - `logs/navigator-public-https-*.ca-bundle.manifest`
  - `logs/navigator-public-https-*.evidence.json`
- The workflow writes the secret to a temporary runner file, validates it immediately with `scripts/validate-navigator-ca-bundle.ps1`, points `GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE` at that file, and removes the file after the run where practical.
- The reviewed target allowlist for public-pilot hardening v0.5 currently contains:
  - `https://sha256.badssl.com/`
  - `https://example.com/`
  - `https://www.gnu.org/`
  - `https://news.ycombinator.com/`
  - `https://en.wikipedia.org/`
    Stable badssl DNS-hosted HTTPS endpoint used to prove real-world DNS, TCP, TLS, certificate, and hostname validation without opening arbitrary browsing.
- The workflow accepts `target_url` only when it matches the approved reviewed allowlist.
- If you need another public target in GitHub Actions, update the reviewed allowlist intentionally instead of bypassing the guard.
- If `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM` is missing, the workflow fails clearly before the probe runs.
- When the dedicated probe exits `0`, the workflow replays `scripts/assert-navigator-public-https-pass.ps1` against the latest summary artifact and adds the assertion report to `GITHUB_STEP_SUMMARY`.
- The workflow also surfaces the workflow input manifest plus the latest evidence JSON path and contents in `GITHUB_STEP_SUMMARY` when present.
- Evidence promotion is intentionally strict: it only accepts `PASS` evidence, requires the PASS assertion, validates the candidate hash/rotation linkage, and records the reviewed allowlist name/version in a commit-safe promotion record.

### Public HTTPS PASS Artifact Checklist

Review the uploaded `navigator-public-https-*.summary.log` and treat the run as proof only when all of the following are true:

- `result_marker=PASS`
- `final_result=PASS`
- `target_url=` is the expected `https://` URL for the run.
- `public_trust_ready=yes`
- `public_ca_source_marker=` shows an explicit source such as `env-var`, `env-var-preferred-over-local`, or `local-fallback-file`.
- `public_trust_reason`, `public_trust_blocker`, `public_trust_source_allowed`, `public_trust_manifest_ready`, `public_trust_runtime_hash_match`, `public_trust_test_only`, and `public_trust_lane` explain why the dedicated public proof did or did not reach trust readiness.
- `public_ca_parsed_certs=` is greater than `0`.
- `trust_bundle_manifest_present=yes`
- `trust_bundle_sha256=` is a 64-character lowercase hex digest.
- `trust_bundle_type=production-public-probe-merged`
- `trust_bundle_root_count=` is greater than `0`.
- `trust_bundle_production_ready=yes`
- `trust_bundle_test_only=no`
- `dns_result=PASS`
- `tcp_result=PASS`
- `tls_result=PASS`
- `certificate_validation_result=PASS`
- `hostname_validation_result=PASS`
- `verify_flags=0`
- `sni_host=` matches the target hostname.
- `http_status=` shows that an HTTPS response was actually received.
- `plaintext_fallback=no`
- `pass_contract_assertion_result=PASS`

Review notes:

- `SETUP_BLOCKED`, `SKIP`, and `FAIL` are useful diagnostics but are not proof of a working public HTTPS path.
- `tls_transport_proof_result=PASS` means Navigator completed DNS, TCP, TLS, certificate, and hostname validation successfully for the reviewed public target.
- `tls_failure_classification` makes failures more specific than a generic TLS miss. Expect values such as `DNS_FAILURE`, `TCP_FAILURE`, `TLS_HANDSHAKE_FAILURE`, `CERTIFICATE_VERIFICATION_FAILURE`, `HOSTNAME_FAILURE`, `POLICY_OR_SETUP_BLOCKED`, or `ENVIRONMENT_UNAVAILABLE`.
- `page_render_result=RENDERED` is stricter than transport proof; unsupported compression, unsupported content, redirect policy blocks, downgrade blocks, or response caps can keep rendering from succeeding after TLS proof has already succeeded.
- A content/browser limitation after TLS success, such as unsupported content encoding or unsupported compression handling, must not be mistaken for a CA, certificate, hostname, or TLS transport failure.
- Deterministic fixture roots still do not count as public trust, even if another field in the same log looks healthy.
- Automated PASS assertion failing is also not proof, even if some individual fields look healthy in the same artifact.
- The evidence JSON is a structured promotion of the same proof and is useful for milestone tracking, but the PASS artifact checklist still applies to the underlying summary log.

### CA bundle validation and rotation

- Manual validator command:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\validate-navigator-ca-bundle.ps1 -BundlePath C:\path\to\ca-bundle.pem -BundleType production-public-source`
- Explicit output path example:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\validate-navigator-ca-bundle.ps1 -BundlePath C:\path\to\ca-bundle.pem -BundleType production-public-source -OutputManifestPath .\logs\candidate-public-ca.manifest`
- Current manifest fields:
  - `schema_version`
  - `bundle_type`
  - `source`
  - `generated_utc`
  - `root_count`
  - `pem_bytes`
  - `sha256`
  - `subject_summary`
  - `not_before_min`
  - `not_after_max`
  - `production_ready`
  - `test_only`
  - `rotation_id`
- Rotation policy for v0.1:
  - supply a new public-root bundle explicitly
  - validate it and generate a manifest
  - run the dedicated public HTTPS probe and require PASS
  - archive the summary, manifest, serial log, and evidence JSON together
  - record the outgoing manifest hash or retain the prior manifest in notes
  - do not enable default public HTTPS browsing as part of this rotation step
- The helper does not download roots, does not normalize PEM contents, and does not mark deterministic fixtures as production-ready.

## Shipped-root candidate review workflow

- Candidate metadata is kept separate from the runtime CA manifest so reviewer-only fields do not expand the runtime parsing contract.
- The candidate metadata schema is `guidexos.navigator.shipped-root-candidate.v0.1`.
- Commit-safe candidate metadata may be archived under `scripts/fixtures/public-roots/candidates/`.
- The helper `scripts/prepare-navigator-shipped-root-candidate.ps1` defaults to writing local preparation artifacts under `logs/navigator-shipped-root-candidates/<candidate-id>/`.
- Candidate metadata records:
  - `candidate_id`
  - `rotation_id`
  - `bundle_sha256`
  - `root_count`
  - `pem_bytes`
  - `bundle_type=shipped-root-candidate`
  - `production_ready`
  - `test_only`
  - `proposed_utc`
  - `reviewed_utc`
  - `reviewer`
  - `source_description`
  - `evidence_required`
  - `evidence_status`
  - `last_public_probe_target`
  - `last_public_probe_utc`
  - `last_public_probe_result`
  - `notes`
- Candidate metadata must not include PEM contents, private keys, or secret local paths.
- Candidate metadata does not authorize default public HTTPS browsing.
- The helper `scripts/promote-navigator-public-https-evidence.ps1` links PASS public HTTPS evidence to candidate metadata without mutating the source metadata destructively.
- Evidence linking verifies:
  - `evidence_status=PASS`
  - `pass_contract_assertion_result=PASS`
  - approved target URL
  - trust bundle type compatibility
  - candidate hash direct match when the evidence trust bundle is the candidate itself
  - rotation-ID lineage when the evidence trust bundle is the merged public-probe bundle
- If the public probe used a merged candidate-plus-public bundle, direct SHA-256 equality is not expected; the review link falls back to `runtime_manifest_rotation_id` lineage and records that the merged bundle was not directly hash-comparable to the base candidate.
- The public proof path itself stays opt-in and still requires explicit public-root input even when a shipped-root candidate is under review.

### First-Run Operator Checklist

Use this when collecting the first GitHub-hosted PASS artifact for a branch, release note, or milestone checkpoint:

1. Push the branch that contains `.github/workflows/navigator-public-https-probe.yml`.
2. Confirm the `Navigator Public HTTPS Probe` workflow is visible in GitHub Actions for that branch or the default branch workflow view.
3. Add or confirm the repository secret `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM`.
4. Use a real public-root PEM bundle that is appropriate for the chosen target URL.
5. Start the workflow manually with `workflow_dispatch`.
6. Leave the default target `https://sha256.badssl.com/` unless you intentionally need another HTTPS URL.
7. Download or inspect the uploaded workflow input manifest plus the `navigator-public-https-*.summary.log`, `navigator-public-https-*.serial.log`, `navigator-public-https-*.ca-bundle.manifest`, and `navigator-public-https-*.evidence.json` artifacts.
8. Confirm the full PASS artifact checklist above.
9. Archive or attach the manifest hash and the summary/evidence artifacts in release notes, development notes, or the milestone thread.

Operator warnings:

- Do not paste the secret PEM into logs, issues, pull requests, or release notes.
- Do not commit the PEM or any derived local `.local` bundle.
- Do not treat `SETUP_BLOCKED` or `SKIP` as evidence that public HTTPS is working.
- Do not enable default public bare-metal HTTPS based on a single PASS artifact alone.

### Automated PASS Assertion

- Manual command:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\assert-navigator-public-https-pass.ps1 -SummaryPath .\logs\navigator-public-https-<timestamp>.summary.log`
- Self-test command:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\assert-navigator-public-https-pass.ps1 -SelfTest`
- The assertion helper fails proof when required PASS fields are missing, wrong, or misleading. Common failures include:
  - missing `result_marker=PASS`
  - `final_result` not equal to `PASS`
  - non-HTTPS or numeric-IP `target_url`
  - `public_trust_ready` not equal to `yes`
  - misleading `public_ca_source_marker` such as deterministic-only or legacy `ignored-local-file`
  - `public_ca_parsed_certs <= 0`
  - missing or malformed `trust_bundle_sha256`
  - `trust_bundle_manifest_present` not equal to `yes`
  - `trust_bundle_type` not equal to `production-public-probe-merged`
  - `trust_bundle_production_ready` not equal to `yes`
  - `trust_bundle_test_only` not equal to `no`
  - any non-`PASS` DNS/TCP/TLS/certificate/hostname result
  - `verify_flags` not equal to `0`
  - missing or mismatched `sni_host`
  - non-numeric `http_status`
  - `plaintext_fallback` not equal to `no`
- Content limitations do not invalidate TLS proof by themselves:
  - `content_encoding` may still be unsupported after successful TLS proof.
  - `unsupported_reason` may still be populated after successful TLS proof.
  - `header_cap_hit`, `body_cap_hit`, and `tls_succeeded_before_content_failure` can record that TLS succeeded before Navigator stopped on a compatibility limit.
  - `tls_retry_count`, `tls_retry_reason`, `tls_bytes_written_before_retry`, `redirected_https_retry_used`, and `redirect_hop_url` can show that a redirected HTTPS hop retried once after a pre-write transport-open failure without weakening certificate or hostname validation.

### Structured Evidence JSON

- Manual command:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\export-navigator-public-https-evidence.ps1 -SummaryPath .\logs\navigator-public-https-<timestamp>.summary.log`
- Optional explicit output path:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\export-navigator-public-https-evidence.ps1 -SummaryPath .\logs\navigator-public-https-<timestamp>.summary.log -OutputPath .\logs\navigator-public-https-custom.evidence.json`
- The JSON schema currently includes:
  - `schema_version`
  - `generated_utc`
  - `source_summary`
  - `result_marker`
  - `final_result`
  - `target_url`
  - `target_host`
  - `reviewed_target_policy`
  - `reviewed_target_allowlist`
  - `reviewed_target_match`
  - `reviewed_target_override`
  - `reviewed_target_reason`
  - `public_trust_ready`
  - `public_ca_source_marker`
  - `public_ca_bytes`
  - `public_ca_parsed_certs`
  - `trust_bundle_manifest_present`
  - `trust_bundle_sha256`
  - `trust_bundle_type`
  - `trust_bundle_root_count`
  - `trust_bundle_production_ready`
  - `trust_bundle_test_only`
  - `dns_result`
  - `tcp_result`
  - `tls_result`
  - `transport_selection`
  - `transport_policy_reason`
  - `tls_status`
  - `tls_connect_attempts`
  - `tls_retry_count`
  - `tls_retry_reason`
  - `tls_bytes_written_before_retry`
  - `tls_handshake_error_code`
  - `tls_transport_error_code`
  - `tls_request_bytes_written`
  - `tls_response_bytes_read`
  - `tls_failure_classification`
  - `tcp_abort_used`
  - `redirected_https_retry_used`
  - `redirect_hop_index`
  - `redirect_hop_url`
  - `certificate_validation_result`
  - `hostname_validation_result`
  - `verify_flags`
  - `sni_host`
  - `source_type`
  - `requested_url`
  - `final_url`
  - `redirect_count`
  - `http_status`
  - `content_type`
  - `content_encoding`
  - `header_cap_hit`
  - `body_cap_hit`
  - `downgrade_blocked`
  - `tls_succeeded_before_content_failure`
  - `unsupported_reason`
  - `tls_transport_proof_result`
  - `content_compatibility_result`
  - `page_render_result`
  - `real_world_compatibility_note`
  - `plaintext_fallback`
  - `pass_contract_assertion_result`
  - `pass_contract_assertion_exit_code`
  - `evidence_status`
- `evidence_status=PASS` only when the summary itself is PASS and the automated PASS contract assertion succeeded.
- The evidence JSON is not a secret, but it must never include PEM contents, raw CA bundle material, or private keys.

### Example commands

- Deterministic smoke only, still internet-independent:
  - `.\scripts\smoke-navigator-kernel.ps1`
- Deterministic kernel smoke plus the explicit public-pilot lane:
  - `.\scripts\smoke-navigator-kernel.ps1 -IncludePublicPilot`
- Public-pilot kernel lane only:
  - `.\scripts\smoke-navigator-kernel.ps1 -ScenarioGroup PublicPilot`
- Dedicated public probe smoke with the conventional ignored local bundle path:
  - `Copy-Item .\scripts\fixtures\public-roots\ca-bundle.pem.example .\scripts\fixtures\public-roots\ca-bundle.pem.local`
  - Replace the placeholder contents in `scripts\fixtures\public-roots\ca-bundle.pem.local` with real public roots.
  - `.\scripts\smoke-navigator-public-https.ps1`
- Dedicated public probe smoke through the wrapper:
  - `.\scripts\smoke-navigator-public-https.bat`
- Reviewed allowlist matrix helper:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-navigator-public-https-allowlist.ps1`
- Reviewed allowlist matrix helper for one approved target only:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-navigator-public-https-allowlist.ps1 -TargetUrl https://sha256.badssl.com/`
- Manual GitHub Actions execution:
  - Open the `Navigator Public HTTPS Probe` workflow in GitHub Actions.
  - Push the branch first if the workflow is new on that branch.
  - Add or confirm the repository secret `GXOS_NAVIGATOR_PUBLIC_CA_BUNDLE_PEM`.
  - Use a real public-root PEM bundle suitable for the selected approved target.
  - The reviewed allowlist currently approves the v0.5 target matrix listed above.
  - Start the manual run, download the uploaded summary/serial/evidence artifacts, and confirm the PASS artifact checklist.
- Dedicated public probe smoke with an explicit bundle path:
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE="C:\path\to\ca-bundle.pem"`
  - `.\scripts\smoke-navigator-public-https.ps1`
- Dedicated public probe smoke with an explicit bundle path and target override:
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE="C:\path\to\ca-bundle.pem"`
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL="https://sha256.badssl.com/"`
  - `.\scripts\smoke-navigator-public-https.ps1`
- Dedicated public probe smoke with an explicit reviewed override for a one-off target:
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE="C:\path\to\ca-bundle.pem"`
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL="https://example.com/"`
  - `.\scripts\smoke-navigator-public-https.ps1 -ReviewedTargetOverride`
- Manual PASS artifact assertion against a saved summary:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\assert-navigator-public-https-pass.ps1 -SummaryPath .\logs\navigator-public-https-<timestamp>.summary.log`
- Manual PASS assertion self-test:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\assert-navigator-public-https-pass.ps1 -SelfTest`
- Manual evidence promotion against a saved summary:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\export-navigator-public-https-evidence.ps1 -SummaryPath .\logs\navigator-public-https-<timestamp>.summary.log`
- Manual CA bundle validation and manifest generation:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\validate-navigator-ca-bundle.ps1 -BundlePath C:\path\to\ca-bundle.pem -BundleType production-public-source`
- Candidate preparation:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\prepare-navigator-shipped-root-candidate.ps1 -BundlePath C:\path\to\candidate.pem -CandidateId candidate-2026-06 -SourceDescription manual-review-bundle`
- Dedicated public probe smoke against a shipped-root candidate:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-navigator-public-https.ps1 -CandidateBundlePath C:\path\to\candidate.pem -CandidateRotationId candidate-2026-06`
- Candidate evidence linking after a PASS public proof:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\promote-navigator-public-https-evidence.ps1 -CandidateMetadataPath .\logs\navigator-shipped-root-candidates\candidate-2026-06\candidate-2026-06.candidate.json -EvidencePath .\logs\navigator-public-https-<timestamp>.evidence.json`
- Manual kernel smoke with the public probe enabled inside the broader deterministic suite:
  - `$env:GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS="1"`
  - `$env:GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS="1"`
  - `.\scripts\smoke-navigator-kernel.ps1 -IncludePublicPilot`
- Manual kernel smoke with an explicit bundle path:
  - `$env:GXOS_NAVIGATOR_SMOKE_ENABLE_REAL_PUBLIC_HTTPS="1"`
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_CA_BUNDLE_SOURCE="C:\path\to\ca-bundle.pem"`
  - `$env:GXOS_NAVIGATOR_SMOKE_REQUIRE_REAL_PUBLIC_HTTPS="1"`
  - `.\scripts\smoke-navigator-kernel.ps1 -IncludePublicPilot`
- Optional target override:
  - `$env:GXOS_NAVIGATOR_SMOKE_REAL_PUBLIC_HTTPS_URL="https://sha256.badssl.com/"`

This probe remains smoke/manual validation only. It does not enable default public HTTPS browsing in bare-metal Navigator, and it does not download public roots for you.

Candidate rotation process in this pass:

1. Obtain the candidate bundle locally and keep the PEM outside git.
2. Validate and fingerprint it with `scripts/prepare-navigator-shipped-root-candidate.ps1`.
3. Run the dedicated public HTTPS proof path with explicit public roots and the candidate bundle only when intentional.
4. Export or review the resulting `navigator-public-https-*.evidence.json`.
5. Link PASS evidence to the candidate with `scripts/promote-navigator-public-https-evidence.ps1`.
6. Archive the candidate metadata, manifest, summary, serial log, and evidence link together.
7. Make any future shipped-root or default-policy decision separately from this evidence workflow.
