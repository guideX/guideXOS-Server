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
The manifest rotation identifier is `mozilla-2026-08-13`. A normal build also
stages `/config/navigator/https-policy.txt=production-validated` so the
kernel's generic ProductionValidated policy is selected only when this
production bundle is present and successfully verified.

The deterministic smoke roots under `scripts/fixtures/` and explicit user
roots under `/config/certs` remain separate inputs. They are not appended to
this shipped production source by a normal build. Updating this bundle must
replace the versioned PEM, update the fixed metadata in
`scripts/generate-wallpaper-pack.ps1`, rerun the manifest validator, and
record the new source date, count, size, and hash in this file.
