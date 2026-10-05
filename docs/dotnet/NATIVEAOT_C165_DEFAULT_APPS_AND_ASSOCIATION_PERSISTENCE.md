# NativeAOT C165 — Default Apps and Association Persistence

## Outcome

**Outcome C — managed Settings lacks a safe association-control boundary.**

C165 was audited from the clean C164 baseline at
`5ae2d0608e770b9b6a2c1999c58ac6562124aa52` on
`v1.1_DOTNET_SUPPORT`. No C165 implementation or later phase marker was
present. This phase makes no source/runtime changes because a safe managed
query and mutation contract is not present in this baseline. The Default Apps
page, persistence, and production boot claims in the request therefore remain
unimplemented and unverified.

## C164 resolver audit

The authoritative owner is `kernel/core/app_launch_target_resolver.cpp`, in
`kernel::appmodel`. Its header is
`kernel/core/include/kernel/app_launch_target_resolver.h`. The resolver uses a
fixed array of 16 records. A record contains an extension pointer and canonical
application-ID pointer; on the 64-bit target the table occupies 256 bytes. One
record is populated: `.txt` to
`com.guidexos.apps.managed.notes`. The compiled table is `constexpr` and
immutable at runtime.

Lookup finds the final dot in the leaf and compares the suffix using bounded
ASCII case-insensitive comparison. Thus `.txt`, `.TXT`, and `.TxT` match. The
resolver also enforces the 96-byte absolute canonical VFS path bound and
returns distinct malformed path, directory, non-regular, missing, I/O, and
unsupported statuses. The resolver itself does not stat arbitrary display
text; `resolveFileAssociationFromVfs` validates the path and uses VFS stat.

Native File Explorer's `.txt` branch in
`kernel/core/kernel_apps.cpp::FileExplorerApp::openSelected` calls
`resolveFileAssociationFromVfs`, then launches the returned App Model identity
with document activation and the exact joined VFS path. Managed File Explorer
in `GuideXosManagedFileExplorerC163.OpenSelected` calls the native
`requestFileActivation` operation; the native callback resolves through the
same resolver before launching. App Model target resolution uses the returned
canonical application ID and its registered managed application metadata.
Neither File Explorer has an independent `.txt` association table.

The repository's C164 documentation reports 28 resolver cases and 1,000
resolution calls. Those are historical C164 evidence; C165 did not rerun the
proof runner. The source currently reports table capacity 16, used count 1,
and 256 bytes.

## Settings, persistence, and ABI boundary

Managed Settings is `ManagedSettingsCenter`; its existing C146 store and C147–
C149 runtime path own Settings-format-v2 preferences such as NaturalScroll,
ScrollLinesPerNotch, and ShowKeyboardTips. C165 does not change that format.
The existing managed host table is ABI v3 / 120 bytes. Its available bounded
services include VFS operations, application snapshots, and identity-safe
application close. The App Model context's file-activation callback accepts a
path and requests activation; it is not an association query or mutation
service.

The generic managed `fileWriteAll` callback is not a safe substitute: using it
to edit an association file from Settings would bypass resolver-owned
validation and would create a second authority. The resolver currently has no
override record model, persistence loader/store, query snapshot API, mutation
operation, eligible-handler metadata, or transactional read-back path. Its
compiled `.txt` entry cannot be changed at runtime. Consequently there is no
safe way for Settings to distinguish NoOverride, ApplicationOverride, and
Disabled or make a successful change visible to both File Explorers in the
same boot.

The native Notepad equivalent is dispatched for `.log`, `.cfg`, `.ini`, and
`.md` by legacy native File Explorer routes. C164 intentionally routes only
`.txt` through managed Notes activation; no evidence was found that native
Notepad accepts the C164 managed document-activation contract. It is not an
eligible `.txt` handler for this phase, and it was not modified.

## Required follow-up contract

A future bounded implementation must add an App Model-owned fixed override
store and expose value-only query/set/disable/reset operations to managed
Settings. It must validate application eligibility and the normalized
extension in the resolver owner, write and read back a bounded candidate
before committing runtime state, and make both native and managed Explorer
observe the same committed resolver state. If the existing ABI cannot carry
that request without repurposing a callback, the host ABI must be explicitly
advanced with the v3 prefix and offsets preserved. The Default Apps UI must
report unavailable to an older ABI rather than editing persistence directly.

No persistence path or format, capacity usage beyond the compiled entry,
override recovery policy, transaction semantics, reboot behavior, or Settings
controls are claimed by C165. C128 remains **unverified**.

## Verification and Git

The initial repository gate found the expected C164 commit, clean worktree,
branch `v1.1_DOTNET_SUPPORT`, upstream `origin/v1.1_DOTNET_SUPPORT`, and
tracking state 0 ahead / 0 behind. The prompt described an earlier 1-ahead
state; the current repository had since synchronized. No tests, builds, QEMU
boots, or push probes were run because no implementation was made. The only
file change is this audit document. The worktree is expected to contain this
single intended addition until committed; C165 has no implementation commit.
