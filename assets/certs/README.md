# guideXOS production CA bundle

`mozilla-cacert-2026-08-13.pem` is the single checked-in production trust
source used by the normal wallpaper/ramdisk build. It is the PEM conversion
published by curl's CA Extract service from Mozilla's public root store:

- Source: https://curl.se/ca/cacert.pem
- Source documentation: https://curl.se/docs/caextract.html
- Mozilla source date in the bundle: 2026-08-13 03:12:01 UTC
- Representation: ASCII PEM, one X.509 root certificate per block
- Certificate count: 121
- Source size: 191,850 bytes
- SHA-256: `303daa9461b9617eb8e6209b272613fcf2923959ff32e9422eaaae195c55c780`
- License: Mozilla source terms as documented by curl; the converted bundle
  is distributed under MPL 2.0.

The build validates the PEM, generates the bounded runtime manifest with the
fixed source timestamp above, and packages one copy at `/certs/ca-bundle.pem`.
The manifest rotation identifier is `mozilla-2026-08-13`. A normal build stages
`/config/navigator/https-policy.txt=disabled`. The bundle remains packaged for
review and diagnostics, but it does not enable public HTTPS. The manifest's
legacy `production_ready=yes` label is not proof that Mozilla's out-of-band
root constraints are preserved or that the unsigned manifest is authentic.
An explicit `production-validated` policy requires a separate trust-profile
review and authorization; it is not a normal-build default.

The deterministic smoke roots under `scripts/fixtures/` and explicit user
roots under `/config/certs` remain separate inputs. They are not appended to
this shipped production source by a normal build. Updating this bundle must
replace the versioned PEM, update the fixed metadata in
`scripts/generate-wallpaper-pack.ps1`, rerun the manifest validator, and
record the new source date, count, size, and hash in this file.

Review-only source candidates belong under `assets/certs/candidates/`. They
are not inputs to the normal package generator until their trust constraints,
provenance, and production profile have been reviewed and explicitly
promoted. See the candidate folder README for the 2026-11-05 curl extract and
its non-production manifest.
