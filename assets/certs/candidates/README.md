# CA bundle review candidate

`mozilla-cacert-2026-11-05.pem` is a review-only copy of curl's CA Extract,
which is generated from Mozilla's public root store. The downloaded bundle and
its official sidecar checksum matched before it was copied into this folder.

- Source: [curl CA Extract](https://curl.se/ca/cacert.pem)
- Source documentation and update history: [curl CA Extract docs](https://curl.se/docs/caextract.html)
- Mozilla source date: 2026-11-05
- Format: PEM, 118 certificates, 200,116 bytes
- SHA-256: `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285`
- Distribution license: MPL 2.0, as documented by curl.
- Manifest profile: `production-source`, `production_ready=no`, `test_only=no`
- Rotation id: `mozilla-2026-11-05-review`

The candidate is not wired into the wallpaper pack, ramdisk, or runtime trust
selection. Its manifest and [trust profile](mozilla-cacert-2026-11-05.trust-profile.json)
record integrity and policy metadata; neither is signed and neither
authenticates the upstream source or approval. The deterministic
[policy-accounting report](mozilla-cacert-2026-11-05.accounting.json) binds
records to the candidate SHA-256 and lists all 118 certificates as excluded.
No public roots were included because the curl PEM does not carry the Mozilla
purpose/domain restrictions and this verifier cannot enforce name constraints
or certificate-policy processing. Regenerating the accounting with the pinned
Windows PowerShell 5.1.26100.9818 / .NET 4.0.30319.42000 X509Certificate2
toolchain produced byte-identical output in NAVHTTPS7.

The validator checks PEM structure and bundle metadata, not root authorization
policy. Historical `production_ready=yes` metadata remains preserved in the
shipped source sidecar, but this field is not runtime authorization. Newly
generated shipped-bundle manifests use `production_ready=no` until an
authenticated approval mechanism exists.

curl documents that browser-specific trust constraints are not represented in
its PEM extract. Therefore this file is useful for source review and packaging
checks, but it is not approved for public HTTPS. Review Mozilla's root
constraints and provenance, the target system's time source, manifest
integrity/authenticity, and revocation policy before considering a separate
production profile. Keep test roots in their separate fixture directory.
