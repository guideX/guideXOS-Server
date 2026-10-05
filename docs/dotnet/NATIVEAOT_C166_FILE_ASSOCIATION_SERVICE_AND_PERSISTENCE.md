# NativeAOT C166 — File Association Service and Persistence

## Status

Implementation work is present, but C166 acceptance is **not yet validated**.
No Outcome A claim is made. The existing C164 runner stops at its C163 product
hash gate before compiling or booting C166. The required mutation, reboot,
failure, ordinary-boot, stress, and regression evidence remains outstanding.

Starting repository HEAD was `49986ddcd14801c526d774676fc362648d653da9`,
which is the C165 audit-only commit on `v1.1_DOTNET_SUPPORT`. This is newer
than the prompt's stated C164 HEAD. Its only change from C164 is the C165 audit
document; that document remains intact and committed. The worktree was clean
at the start.

## Authority and behavior

`kernel::appmodel` in `kernel/core/app_launch_target_resolver.cpp` remains the
single association authority. Its immutable compiled table remains capacity
16 with one entry: `.txt` to `com.guidexos.apps.managed.notes`. Overrides use a
separate one-record fixed array, since `.txt` is the only known configurable
key. The record is 116 bytes: extension[16], state (u32), and app ID[96].
Runtime override storage therefore costs 116 bytes, excluding alignment.

The three states are `NoOverride`, `ApplicationOverride`, and `Disabled`.
Extensions are restricted to known compiled keys, normalized to lower-case
ASCII, and matched case-insensitively by the existing final-suffix resolver.
The only eligible pair is `.txt` plus the canonical Managed Notes ID. Other
registered apps are not accepted. Native Notepad remains audit-only and is not
eligible for C164 document activation.

The resolver checks a valid override before the compiled entry. Disabled
returns no association. Reset writes an empty override image, restoring the
compiled Notes default. Both File Explorers still use this same resolver; the
managed Explorer continues to request native file activation.

## Managed boundary and ABI

The C164 App Model request context is activation-specific and is not repurposed.
The managed host table advances from v3/120 bytes to v4/128 bytes by appending
one `associationService(request, response)` callback at offset 120. v1 (104),
v2 (112), v3 (120), snapshot offset 104, and close offset 112 remain preserved.
Managed code checks both version >= 4 and size >= 128 before reading the tail;
ABI v3 therefore reports `NotSupported` without dereferencing it. The callback
uses fixed request/response buffers and operation codes Query, SetOverride,
Disable, and Reset. The reusable `GuideXosFileAssociations` wrapper exposes
bounded calls and does not access the backing file.

Response fields contain normalized extension, compiled default, override
state, override ID, effective ID, and association-presence flag. Service
statuses distinguish success, unsupported, invalid argument, unknown
extension, ineligible handler, capacity exceeded, persistence failure, and
invalid persisted state.

## Persistence

Persistence belongs to the native association authority. The backing path is
`/GXASSOC.BIN` on the writable root FAT VFS mount. Settings persistence stays
format v2; association persistence has its own v1 format. The image is fixed at
124 bytes: 8-byte header (magic `GSA1`, version u16, count u16) and one 116-byte
record. The record key is the normalized extension; it contains no path,
filename, or display name. Application identity is stored canonically.

Missing file means no override. Invalid size, magic, version, count, state,
extension, or app identity rejects the complete image and falls back to
compiled defaults. Startup loads the image after VFS mount and before desktop
activation; invalid stored state emits the bounded
`[APP-MODEL] association-persistence=invalid-ignored` diagnostic. An app that
is missing or ineligible is rejected on load, so it cannot be launched.

Mutation validates the operation, key, and handler, serializes a complete
candidate, writes it through native VFS, reads it back, validates it, and only
then installs runtime state. A write or read-back failure leaves the current
runtime override unchanged and returns `PersistenceFailed`. VFS `write_file`
is synchronous in this repository; it has no separate flush callback.

## Verification recorded so far

- Managed HostLogProof build: succeeded, 0 errors; existing MSBuild path and
  Notes analyzer warnings remain.
- Kernel AMD64 link with the production C164 phase defines: succeeded.
- `git diff --check`: succeeded.
- C164 phase runner: stopped before build/boot because current ESP/kernel
  hashes differ from the C163 manifest's expected restored hashes. No C166
  proof or ordinary boot was run.
- No C166 service, ABI, persistence corruption, failure injection, 1,000
  transaction, or managed wrapper test suite has been run yet.
- C164/C163/C162/C160/C156 regression suites, Settings smoke, Notes proof, and
  native/managed File Explorer mutation parity are therefore unverified for
  this implementation. C128 remains unverified.

No Settings UI was added. No proof-only association override was installed.
The production `.txt` state has not been proven by a fresh boot; with no
association image, compiled Managed Notes remains the intended default.
