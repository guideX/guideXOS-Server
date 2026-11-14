# NAVHTTPS14 — Multi-Server Roughtime Agreement

**Outcome A — the bounded hosted-first agreement engine, policy tests, and AMD64 freestanding build passed.** No guest run, clock setting, production HTTPS authorization, or production trust change was performed.

## 1. Starting state and preserved work

- Repository: `D:\dev\guideXOSServer_NAVIGATOR_IMPROVEMENTS`
- Branch: `NAVIGATOR_GENERAL_IMPROVEMENTS`
- Starting HEAD: `62abda7a5fde3a819aec358befb212649327dc6e`
- Preflight tracked tree: clean; pre-existing untracked Python cache and NAVHTTPS8/12/13/13R build and guest artifacts were preserved.
- I219-LM remains `last_completed=PHASE26`, `next_expected=PHASE27`. No I219-LM source, hardware setting, or phase record was changed.

NAVHTTPS13R's latest-ELF guest requalification and NAVHTTPS7 guest trust-policy acceptance remain pending. The earlier successful NAVHTTPS13 guest run covered an older ELF and does not qualify the later ELF. The intermittent Navigator compositor/CSS investigation remains separate; this NAVHTTPS14 hosted smoke passed, but that single pass does not close the historical investigation. Existing CSS assertions were left intact.

## 2. Existing evidence model

NAVHTTPS12's `gxos_roughtime_v1_verify` already produces the information this policy needs: authenticated server-key identity, successful signature/delegation/nonce/Merkle flags, signed midpoint and radius, server and transport-expanded intervals, request and receipt monotonic times, measured transport delay, request identity, and request nonce. The acquisition adapter already tracks an expiry deadline for its single-source result. NAVHTTPS14 reuses those fields and derives policy freshness from `received_monotonic_ns`; it does not add another timestamp or cryptographic verifier.

Cryptographic verification and agreement acceptance are separate. A verifier-issued opaque token records a fully verified single response. The agreement evaluator accepts only those tokens; it has no input for caller-populated evidence fields. In production, the existing UDP acquisition attaches a token only after it obtains a secure-random nonce, records PIT send/receipt times, and verifies that exact response. A raw-response issuer is compiled only into the hosted test harness. The token's opaque storage and integrity check protect the C++ API from ordinary accidental construction or modification; they are not a memory-isolation boundary against malicious kernel code that can overwrite arbitrary memory.

## 3. Source and operator identities

`gxos_roughtime_agreement_policy` carries at most eight pinned 32-byte server keys and administrator-assigned nonzero operator IDs. The engine derives each key identity using the existing RFC 10049 `SHA-512(0xff || key)` identity rule and matches it to the verifier's authenticated identity. Duplicate public keys or duplicate key identities invalidate the policy.

Operator IDs are configured assertions that must be independently researched and audited. A signature proves control of a key, not that two operators are independent. Multiple keys assigned to one operator are aggregated into one conservative interval hull and count once toward quorum. No public trust anchors or production operator set are embedded in the code.

The bounded profile supports up to eight configured keys/operators. Quorum must be at least two and no larger than the number of distinct configured operators. Hosted fixtures exercise two of three and three of three. `FIXTURE_ONLY` and `UNREVIEWED_CONFIG` are both non-production profiles; every result keeps `production_policy_accepted=0`.

## 4. Agreement, ambiguity, and rejection semantics

For a policy evaluation at monotonic time `now`, every accepted response is normalized to that instant. For elapsed nanoseconds `d = now - received`, its lower endpoint advances by `floor(d / 1 second)` and its upper endpoint advances by `ceil(d / 1 second)`. Arithmetic overflow, a future receipt timestamp, an invalid token, an unknown key identity, duplicate request identity/nonce, or an invalid source policy rejects the evaluation.

Stale responses are excluded after the configured age limit. Transport delay and per-operator uncertainty are bounded separately. Multiple fresh keys assigned to one operator are combined by taking the hull of their normalized intervals; this can only keep or widen that operator's uncertainty.

Success requires the intersection of **all supplied, fresh, recognized, policy-eligible operator intervals** to be nonempty. The returned interval is that common intersection; the midpoint is derived from its endpoints, never by averaging server estimates. A valid but conflicting eligible source therefore returns `InconsistentSources`, even if a subset would meet a numeric quorum. This prevents an evaluator from discarding a valid outlier to manufacture a quorum.

When all eligible intervals do not share one common intersection, the engine examines each bounded quorum-sized subset. No agreeing subset returns `InconsistentSources`. If multiple quorum subsets produce candidate intervals with no common intersection, it returns `Ambiguous`. A single candidate with other conflicting eligible sources is still rejected as inconsistent. Missing or stale sources may leave a usable quorum; conflicting valid sources do not.

The result includes classification, policy identity/profile, common interval, evaluation monotonic time, maximum evidence age and per-operator uncertainty, distinct operator IDs, and the key identities used. It does not assert that a quorum is correct: colluding or compromised operators can agree on false time.

## 5. Freshness and same-boot continuity

Policy bounds are explicit and finite: at most 300 seconds for evidence age (matching the existing acquisition lease), 302 seconds for per-source uncertainty, 2,000 milliseconds for accepted transport delay, and 86,400 seconds for the forward-jump allowance. The issued token carries the acquisition expiry; a policy cannot extend it. The underlying verifier retains its tighter protocol and packet bounds.

The optional caller-owned continuity state tracks the last accepted interval and monotonic instant within one boot. A result that is certainly behind the predicted prior interval returns `ClockRollback`; a result beyond the configured forward-jump allowance returns `ForwardJump`; monotonic regression or arithmetic failure returns `ContinuityFailure`. Each continuity failure latches in that state. Start with a zero-initialized state after boot. A new state has no prior trusted floor and makes no cross-boot rollback claim.

CMOS plausibility alone is not promoted to authenticated time. NAVHTTPS8's existing rollback latching remains in place. The new `AgreementEvidence` handoff to clock assurance validates the structured result but does not transition the OS clock to `Authenticated`.

## 6. Hosted tests and regression results

`python -B -m unittest discover -s tests -p 'test_roughtime*.py' -v`: **PASS, 64/64** (the earlier NAVHTTPS13R baseline was 46). This includes 18 policy-specific cases covering two and three independent operators, a single source, same-operator keys, duplicate keys/responses, invalid source metadata, unrecognized identities, unissued caller values, a compile-time check that production callers cannot pass raw evidence or access a raw issuer, stale/expired and future-monotonic evidence, excessive uncertainty and delay, conflicting sources, incompatible quorum candidates, normalization across monotonic receipt times, time boundaries and arithmetic, invalid thresholds, profile separation, rollback/forward jump/latching, and reboot without a persistent time floor. A captured independently signed Roughenough 2.2.0 exchange is used alongside deterministic signed test responses. The captured fixture is replayed evidence, not fresh live time.

Other requested regressions:

| Check | Result |
|---|---|
| NAVHTTPS12 Roughtime differential and portable verifier suite | PASS as part of the 64/64 run |
| Strict Ed25519 corpus | PASS, 238 hosted and 238 freestanding cases; 117 valid accepted, 64 invalid and 29 acceptable cases rejected |
| Clock assurance | PASS, including the new multi-source evidence freshness, quorum, metadata, and uncertainty contract |
| HTTPS trust profile | PASS |
| Secure randomness | PASS |
| DNS resolution | PASS, 13 protocol scenarios and 6 controlled-fixture checks |
| UDP/socket bounds | PASS, 6 cases |
| TCP connect/cleanup and IPv4 attribution | PASS; the existing `ephemeral > 65535` type-limits warning remains |
| HTTPS qualification contract | PASS, 7 cases including blocked/not-run/fail classifications |
| Hosted Navigator smoke | PASS on this run; historical intermittent compositor/CSS investigation remains open |

The tests use deterministic signatures and monotonic values; no live Roughtime query or system-clock adjustment was made.

## 7. Freestanding build and resource accounting

The AMD64 kernel compiled and linked successfully with `-fstack-usage`. Compared with the preserved NAVHTTPS13R ELF, the new ELF is 4,728 bytes larger (2,892,712 bytes versus 2,887,984 bytes). The new agreement object is 7,305 bytes before link-time elimination.

Measured new stack frames are 2,496 bytes for `gxos_roughtime_agreement_evaluate` and 160 bytes for the internal acquisition-token seal. The existing verifier frame is 912 bytes and the existing UDP acquisition frame is 3,664 bytes. Agreement evaluation runs after acquisition returns, so networking, Ed25519 verification, and multi-source evaluation are not combined on one kernel stack.

Caller-owned storage sizes are: policy 316 bytes, each opaque evidence token 256 bytes (up to 2,048 bytes for eight), result 360 bytes, optional continuity 32 bytes, and the existing verifier workspace 1,216 bytes. The acquisition result now carries one additional 256-byte token. The evaluator uses fixed bounded local arrays, no heap allocation, and no global mutable state. The crypto/network work remains in the existing verifier and acquisition path.

## 8. Clock integration boundary

The evaluator exposes `AgreementSatisfied`, `InsufficientSources`, `InconsistentSources`, `StaleEvidence`, `ExcessiveUncertainty`, `InvalidSourcePolicy`, `InvalidEvidence`, `UnrecognizedSource`, `DuplicateResponse`, `Ambiguous`, rollback, forward-jump, continuity, and verifier-rejected results. Its NAVHTTPS8 adapter is data-only. No background time service, wall-clock write, or automatic transition to `Authenticated` was introduced. Normal CMOS assurance remains `Plausible`.

## 9. Guest qualification status

No QEMU run was needed for deterministic NAVHTTPS14 policy acceptance. NAVHTTPS13R's latest-ELF guest checks and NAVHTTPS7's guest trust-policy acceptance remain pending exclusive QAC admission. They are not inferred from this hosted test or kernel build. The prior NAVHTTPS13 guest result applies only to its older ELF.

## 10. Remaining production prerequisites

- Review and provision production keys, operator identities, quorum, freshness, uncertainty, and disagreement policy.
- Establish operator independence and key lifecycle, rotation, revocation, and authenticated policy-update procedures.
- Decide how authenticated time is activated and whether protected persistent time is required across boots.
- Complete NAVHTTPS13R latest-ELF and NAVHTTPS7 guest acceptance separately under QAC.
- Complete release approval for production HTTPS trust roots, certificate policy, revocation, and clock activation.

Multi-source agreement reduces reliance on one response but cannot defend against a colluding or compromised quorum. The experimental two-of-three profile is not production approval. NAVHTTPS14 did not enable public HTTPS, approve the public CA candidate, change TLS verification, or set the system clock.

## 11. Recommended NAVHTTPS15 scope

Select a reviewable operator/key set and a versioned, authenticated configuration and revocation format. Add configuration validation and key-rotation fixtures, then exercise a narrow sequential acquisition-to-agreement handoff with independently operated test endpoints under hosted and exclusive-QAC lanes. Keep clock activation and public HTTPS disabled until the release owner separately approves source independence, persistence/boot policy, trust roots, revocation, and guest qualification.
