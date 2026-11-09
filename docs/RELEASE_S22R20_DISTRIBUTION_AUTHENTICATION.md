# Server v0.3 — S22R20 Distribution Authentication

**DRAFT — PROPOSAL FOR OWNER REVIEW**

**Assessment date:** 2026-11-08
**No production key was created and no release artifact was signed.**

## Recommendation for owner consideration

Use an owner-controlled Ed25519 signing key with an OpenSSH SSHSIG detached signature over the exact UTF-8 bytes of a versioned JSON release manifest. Use the dedicated namespace `guidexos-server-release-manifest-v1` and signer principal `guideXOS-release`. Verify the signature before parsing the manifest; then enforce the manifest format, release identity, canonical encoding, file size, and SHA-256. This is a technical recommendation only. The release owner has not approved the mechanism, key custodian, trust route, publication channel, or release identity.

The inspected host has `OpenSSH_for_Windows_9.5p2, LibreSSL 3.8.2`; `ssh-keygen.exe` can create and verify SSHSIG signatures offline. A synthetic end-to-end fixture passed on this host. `minisign`, GPG, OpenSSL, Cosign, and SignTool are not installed on the inspected host. Windows OpenSSH Client may be installed as an optional feature, and Linux distributions commonly package OpenSSH; availability and minimum version must be stated in the final user guide. Microsoft documents the Windows Client installation path in [Get started with OpenSSH for Windows](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_install_firstuse?pivots=windows-11&tabs=gui).

SSHSIG is a good fit for this phase because it is available on the host, works offline, uses standard OpenSSH keys, signs raw file bytes, and checks a purpose namespace during verification. The namespace separates this release-manifest use from SSH authentication or signatures made for another purpose. OpenSSH documents signing, verification, namespaces, and the allowed-signers format in its [`ssh-keygen` manual](https://man.openbsd.org/ssh-keygen).

## Mechanism comparison

| Mechanism | Security and offline behavior | Public verification and platform burden | Key/recovery burden | S22R20 assessment |
|---|---|---|---|---|
| OpenSSH SSHSIG (Ed25519) | Detached signature over exact bytes; purpose namespace; offline verification against an allowed-signers trust file. | `ssh-keygen -Y verify` works on Windows OpenSSH and Linux OpenSSH. Windows Client is optional on some supported Windows versions; verify minimum versions before release. | Project must protect and back up private key, publish trusted public key/fingerprint, and maintain principal, namespace, rotation, and revocation instructions. | Recommended proposal; available locally and validated with a throwaway key. |
| Minisign (Ed25519) | Detached Ed25519 signatures; simple offline file verification. | Very short verification command; official project documents Windows Scoop/Chocolatey packages and cross-platform builds. It is not installed on this host. See [minisign](https://github.com/jedisct1/minisign). | Simpler public-key distribution, but project still owns custody, backup, rotation, revocation, and independent fingerprint publication. | Credible alternative if the owner prioritizes a small, purpose-built verifier over existing OpenSSH availability. |
| GPG detached signature | Mature public-key signature and offline verification; broad feature set. | Windows requires a GPG distribution; common on Linux, but end users must manage keyrings and identity trust. | Keyring and identity model adds policy and support complexity. | More machinery than needed for this preview. |
| Sigstore Cosign | Identity-based signing and transparency evidence; current bundle format can carry data for offline verification. | Requires users to understand identity/issuer policy and bundle verification; signing commonly depends on OIDC and transparency infrastructure. See [Cosign verification](https://docs.sigstore.dev/cosign/verifying/verify/). | Reduces long-lived private-key custody for keyless signing but adds account identity, log, bundle, and recovery-policy dependencies. | Worth revisiting if the project later adopts a managed release identity; unnecessary infrastructure for the current owner decision. |

The alternatives all need a trustworthy key or publisher-identity bootstrap. HTTPS and a checksum file can help transport integrity but do not replace signature verification. Signature format alone does not make the source, build, operating system, or application secure.

## Signed manifest contract

The signature must cover the **complete manifest byte stream**, with the ISO digest and byte size inside that signed content. A detached signature next to an unsigned hash file is not sufficient. The current candidate's unsigned `.sha256` sidecar and manifest prove only local agreement between files; they do not prove who produced them.

Proposed format version 1 is UTF-8 JSON serialized as [RFC 8785 JSON Canonicalization Scheme (JCS)](https://www.rfc-editor.org/rfc/rfc8785.html), without a BOM or trailing newline. The signer must reject duplicate keys, non-finite numbers, unsupported fields/formats, invalid Unicode, and noncanonical source bytes. Sign the exact canonical bytes; verify the raw signature before parsing; after signature verification, require that bytes are valid JCS and contain no duplicate keys. RFC 8785 specifies deterministic property ordering and number/string representation; the Ed25519 signature algorithm is defined in [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032.html).

The schema proposal requires:

- `format`, `formatVersion`, product, version, channel, architecture, and exact supported disposable-VM profile identifier.
- ISO filename, byte size, and lowercase SHA-256.
- Source commit and source branch; build ID; build-receipt filename and SHA-256; build mode; and a clean-source-state assertion.
- Kernel identity and SHA-256; bootloader identity and SHA-256; ramdisk identity and SHA-256; the ESP image identity/hash; PacMan package identity, manifest hash, ELF hash, and resource path/hash inventory.
- Signing format, key algorithm, key identifier (SHA-256 public-key fingerprint), and SSHSIG namespace. The detached `.sig` file is not a field in the signed manifest.
- Qualification record identifiers and the approved runtime profile reference, without making security claims beyond the signed evidence.

Illustrative shape only; placeholders are not release values and this is not a valid published manifest:

```json
{
  "architecture": "amd64",
  "build": {
    "buildId": "REPLACE-WITH-APPROVED-BUILD-ID",
    "receiptSha256": "REPLACE-WITH-64-LOWERCASE-HEX",
    "sourceCommit": "REPLACE-WITH-40-HEX-COMMIT"
  },
  "channel": "public-technical-preview",
  "components": {
    "bootloader": { "sha256": "REPLACE-WITH-64-LOWERCASE-HEX" },
    "kernel": { "fileName": "kernel.elf", "sha256": "REPLACE-WITH-64-LOWERCASE-HEX" },
    "pacman": {
      "elfSha256": "REPLACE-WITH-64-LOWERCASE-HEX",
      "manifestSha256": "REPLACE-WITH-64-LOWERCASE-HEX",
      "resources": []
    },
    "ramdisk": { "sha256": "REPLACE-WITH-64-LOWERCASE-HEX" }
  },
  "format": "guideXOS-release-manifest",
  "formatVersion": 1,
  "iso": {
    "fileName": "REPLACE-WITH-EXACT-ISO-NAME.iso",
    "sha256": "REPLACE-WITH-64-LOWERCASE-HEX",
    "size": 1
  },
  "product": "guideXOS Server",
  "releaseProfile": "ServerV03PublicPreviewPacManOnly",
  "signing": {
    "algorithm": "ssh-ed25519",
    "format": "OpenSSH-SSHSIG",
    "keyFingerprint": "SHA256:REPLACE-WITH-APPROVED-FINGERPRINT",
    "namespace": "guidexos-server-release-manifest-v1"
  },
  "supportedVmProfile": "REPLACE-WITH-OWNER-APPROVED-PROFILE-ID",
  "version": "0.3.0"
}
```

The complete manifest must add the build receipt name/hash, bootloader and ESP hashes, PacMan manifest/ELF/resource inventory, qualification references, and exact source/build metadata. `iso.size: 1` is a schema-shape placeholder only. The final signer must enforce schema constraints, exact names, and digest lengths before signing.

Signing the manifest establishes a binding from an approved signing key to the manifest's asserted artifact digest. It does not prove the assertions are true; build provenance and qualification must be reviewed separately. A signed older manifest is still authentic, so users must compare the version/channel to the release they intended to fetch. A future `latest` or release index must itself be signed and monotonic or use an explicit user-selected version to prevent silent rollback to an older valid signature.

## Trust establishment and public verification

There is no production key or trusted guideXOS release public key yet. A public key downloaded beside an ISO is not a trust root. Before publication, the owner must name the signer/custodian and publish the key fingerprint through a separately authenticated, owner-controlled route distinct from the file download route. The user must compare the downloaded key's fingerprint to that independently published value. The owner must also define an emergency key-revocation and replacement route that does not depend only on the compromised key.

Proposed OpenSSH principal and namespace:

```text
principal: guideXOS-release
namespace: guidexos-server-release-manifest-v1
```

An approved key's line in `allowed_signers` would use the exact principal, namespace, and public key. The user should inspect its fingerprint with `ssh-keygen -lf -E sha256 <public-key-file>` and compare it through the independent route before trusting the signer entry.

### Windows verification flow

This is a future procedure after a production key, `allowed_signers` file, manifest, signature, and strict verifier have been approved and published. It does not verify the existing candidate against a production key.

1. Obtain the ISO, JCS manifest, `.sig`, public key, and verification instructions from the approved channel. Independently establish the public-key fingerprint before using it.
2. Confirm that the signed version, channel, AMD64 architecture, filename, and VM profile are the release intended. Stop if they differ.
3. Verify the detached signature over the manifest bytes before parsing it:

   ```powershell
   $namespace = 'guidexos-server-release-manifest-v1'
   cmd.exe /d /c "type `"release.manifest.json`" | ssh-keygen -Y verify -f `"allowed_signers`" -I guideXOS-release -n $namespace -s `"release.manifest.json.sig`""
   if ($LASTEXITCODE -ne 0) { throw 'Release manifest signature verification failed; do not boot the ISO.' }
   ```

4. Run the approved cross-platform manifest verifier to reject noncanonical JSON, duplicate keys, malformed fields, wrong release identity, and invalid hashes. It must compare the ISO byte size and SHA-256 to the **signed** fields. On Windows, after that strict validation, the checks are also visible with:

   ```powershell
   $manifest = Get-Content -Raw .\release.manifest.json | ConvertFrom-Json
   $iso = Get-Item -LiteralPath (Join-Path $PWD $manifest.iso.fileName)
   $sha256 = (Get-FileHash -LiteralPath $iso.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
   if ($iso.Length -ne [long]$manifest.iso.size -or $sha256 -cne $manifest.iso.sha256) {
       throw 'ISO size or SHA-256 does not match the signed manifest; do not boot the ISO.'
   }
   ```

   Do not treat a separately downloaded sidecar as an authority.
5. Stop without booting on a missing file, wrong key/fingerprint, unsupported signature, parse/schema failure, identity mismatch, or hash/size mismatch. Preserve the files and report the error through the owner's stated support route.

Windows OpenSSH is an optional feature on some Windows versions; the official OpenSSH Client may need installation through Optional Features or the supported Windows feature-management process. The final user guide must state the tested minimum client version and show a verifiable failure exit status.

### Linux verification flow

The Linux procedure is equivalent and fully offline once the trusted key is established:

```sh
ssh-keygen -lf -E sha256 release-signing-key.pub
ssh-keygen -Y verify \
  -f allowed_signers \
  -I guideXOS-release \
  -n guidexos-server-release-manifest-v1 \
  -s release.manifest.json.sig \
  < release.manifest.json
```

Require a zero exit status. Then run the same approved strict manifest verifier used on Windows, compare the expected version/channel/architecture, and hash the exact ISO. A failed command, absent key, wrong fingerprint, unsupported format, or any identity/digest mismatch is a stop condition. The release still needs a maintained public verifier implementation; the fixture script is a hosted contract test, not a general user-facing verifier.

After that strict validation, compare the signed fields with `stat -c '%s' <iso>` and `sha256sum <iso>`. The printed byte count and digest must exactly equal `iso.size` and `iso.sha256` in the already verified manifest. Do not use an unsigned adjacent checksum file as the trusted value.

## Synthetic validation performed

Run the hosted fixture on Windows with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\test-release-authentication.ps1`. It creates temporary Ed25519 keys and a tiny non-release ISO-shaped byte fixture, signs only the synthetic manifest, and removes its GUID-scoped temporary directory. On Windows OpenSSH 9.5p2, ten total cases passed: one accepted signature/identity/digest and nine rejected cases covering modified artifact, modified manifest, incorrect public key, missing signature, unsupported signature bytes, incorrect signed release identity, invalid hash, duplicate-key JSON, and signed truncated/malformed JSON. The script's fixed-schema fixture checks canonical key ordering and duplicate/format ambiguity; it is not a general-purpose RFC 8785 implementation.

No real release key, production credentials, candidate ISO, or guideXOS publisher identity was used. These results validate the proposed protocol shape and negative expectations only. They do not authenticate the S22R18 ISO or authorize any release process.

## Public-channel minimum requirements

The owner should choose an official channel only after the custodian and publication owner are named. The channel must provide:

- Identifiable owner control, least-privilege publishing access, multifactor authentication where available, and a documented recovery contact.
- Versioned, immutable release paths or equivalent retention protections; no silent file replacement under the same version identity.
- ISO, signed canonical manifest, detached signature, verification instructions, and approved key fingerprint route.
- A separately authenticated key/fingerprint publication path and an authenticated incident/revocation route.
- HTTPS for transport availability and privacy; signed content remains the authenticity control.
- Old signed releases remain identifiable with their original identity; corrections receive a new version/manifest and an explicit notice.
- Mirrors are permitted only if they serve byte-identical signed files. Mirror checks verify the signature and hashes after download.
- Security/correction notices identify affected versions and exact digests. If the project offers a “latest” pointer, it is an authenticated, signed index with anti-rollback rules or a clear version-pinning policy.

No website, code-host release area, package registry, or social account is selected here. Channel, key fingerprint route, mirror policy, publisher identities, and support/incident owners remain explicit release-owner decisions.

## Limits

The proposed signature gives origin authentication only after the user trusts the key independently. SHA-256 detects byte changes relative to the signed manifest. Neither mechanism makes privileged PacMan code safe, changes the host/hypervisor trust boundary, provides guest process isolation, or prevents an authenticated but vulnerable release. Existing S22R18 residual risks and pending contract decisions remain unchanged.
