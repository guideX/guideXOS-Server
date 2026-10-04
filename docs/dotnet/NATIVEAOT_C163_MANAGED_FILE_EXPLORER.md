# NativeAOT C163 — Managed File Explorer

## Outcome

**Outcome A — production read-only Managed File Explorer validated.** C163
adds a fourth managed NativeAOT application that enumerates real VFS entries,
navigates directories transactionally, refreshes from the VFS, and reports
authoritative file metadata. It does not modify files.

Starting HEAD was `861ed2fd149ec8c9d919177265ce371251ff7210` on
`v1.1_DOTNET_SUPPORT`; the accepted C162 baseline was Outcome A. C128 remains
**unverified**. The C163 proof runner completed all three proof boots and all
three ordinary production boots.

## Native File Explorer audit and coexistence

The existing native app remains `gxos.builtin.fileexplorer`, registered as
`FileExplorerApp` and launched through the legacy `Files`/`FileExplorer`
names. Its default path is `/`; `initWithParam` also accepts a path. The app
enumerates with `vfs::opendir`, `vfs::readdir`, and `vfs::closedir`, caps its
snapshot at 128 entries, and probes once past the cap to report omitted
entries. Its path buffer is 256 bytes and entry names use
`vfs::VFS_MAX_FILENAME` storage. It sorts directories first, then uses an
ASCII case-insensitive name comparison. The mount pane reads the VFS mount
table and displays up to eight active mount paths. A missing parameterized
start path is created with `vfs::mkdir` before launch proceeds, another reason
the native app is not a read-only browser.

Native rows expose name, type, and file size; the modified-time column and
properties view show `--`. Native Open has existing text, PNG, disk-image,
and application-like dispatch. Native File Explorer also supports rename,
move-to-trash/delete, and copy/move operations. Its behavior and canonical ID
were preserved. In C163 boot 3 it was launched through its native Start entry,
found by its `gxos.builtin.fileexplorer` identity in Managed Task Manager,
and closed through the existing exact-identity C162 path. The new application
has a separate identity and selector.

## Managed identity and registration

| Item | Before C163 | After C163 |
| --- | ---: | ---: |
| Managed application descriptors | 4 | 5 |
| NativeAOT catalog entries | 7 | 8 |
| Start entries | 17 | 18 |
| Pinned entries | 16 | 17 |
| All Programs entries | 20 | 21 |

The C163 launcher audit confirmed these live counts. No dynamic registry or
capacity increase was added.

| Property | Value |
| --- | --- |
| Display name | Managed File Explorer |
| Application ID | `com.guidexos.apps.managed.fileexplorer` |
| Native File Explorer ID | `gxos.builtin.fileexplorer` |
| Managed selector | 8 |
| Interactive controls | 5: Entry ListBox, Up, Open, Refresh, Close |
| ControlHost capacity | 20 (unchanged) |
| Host ABI | v3, 120 bytes (unchanged) |
| Settings format | v2 (unchanged) |

C163 adds no host ABI call. The C160 snapshot callback remains at offset 104;
the C162 close callback remains at offset 112.

## VFS model and navigation

The app reuses the C151/C152 managed code:

- `GuideXosDirectoryListing.Load` and `GuideXosFile.TryListDirectory` for a
  real bounded directory snapshot;
- `GuideXosPickerPath.TryNormalizeDirectory`, `TryBuildPath`, and the shared
  parent helper for canonical path handling;
- `GuideXosFile.TryGetInfo` for authoritative file metadata;
- `GuideXosOpenFileDialog.SortForExplorer` for the existing deterministic
  directory-first ordering.

The current managed API proves directory enumeration under `/system/apps`,
but does not expose enumeration of VFS `/`. The canonical initial path is
therefore `/system/apps`; the UI invents no parent rows above it. Path capacity
is 96 UTF-8 bytes, entry capacity is 64, and each retained name is bounded to
127 bytes. Names and paths are validated by the shared path helpers before
navigation; traversal, separator injection, and capacity overflow are
rejected. Up stops at `/system/apps`.

Only one current-directory snapshot is authoritative. Entries are classified
from VFS metadata as Directory or Regular. Sorting is locale-independent:
directories first, then ASCII case-insensitive names with ordinal tie
breaking. List rows use `[DIR] name` and `[FILE] name`; their label buffer is
bounded to the ListBox maximum of 127 characters, and the on-screen list is
61 characters wide. Long rows are clipped by the ListBox, while the full
bounded name remains in the current snapshot for path operations. Detail
names are shortened after 50 characters. Non-printable row characters are
shown as `?`; this display conversion does not alter the retained name.

Selecting a regular file calls `TryGetInfo` and shows its exact byte size.
Directories show their type and `(directory)` instead of a fabricated byte
size. A metadata failure is shown as unavailable. A regular-file Open or
Enter reports `File activation not implemented`; C163 does not infer file
associations from extensions.

Refresh queries the VFS again. A selected entry is matched by name plus VFS
type within the current directory; this is selection matching, not persistent
filesystem identity. The previous viewport is retained when valid and moved
only if needed to reveal a surviving selection. Missing selections are
cleared. Navigation commits the new path and snapshot only after successful
enumeration. A failed child Open or Refresh preserves the last valid path and
rows, then displays a bounded status. If a child directory disappears, Up
can still load its parent. Empty directories clear old rows and show
`Directory is empty`.

The snapshot reports `hasMore` from the bounded directory service. When
truncated, status says `Showing first 64 entries; more omitted`; the API does
not expose a truthful total count. No recursive tree or visited-directory
cache is retained. Directory handles are owned and closed synchronously
inside the existing VFS helper calls; File Explorer retains no native VFS
handle. Refresh allocates only arrays and strings bounded by the 64-entry,
127-byte-name, and 96-byte-path limits. These are bounded allocations, not a
zero-allocation claim.

## Input and UI

The five controls are registered in this order:

`Entry ListBox → Up → Open → Refresh → Close`

The ListBox handles pointer selection, Up/Down/Home/End navigation, Enter
activation, and QMP wheel events through the shared ControlHost scrolling
policy (`NaturalScroll` and `ScrollLinesPerNotch`). Tab and Shift+Tab use the
same ordered controls. Ctrl+R routes to the Refresh action, respecting the
existing capture/modal priority. Backspace is not implemented as Up. Pointer
selection and buttons use the real production input path; double-click was not
added.

## Focused and regression evidence

The focused File Explorer suite passed **48 cases**, including empty and mixed
directories, sorting, full capacity and truncation, path boundaries and
overflow, root Up, successful and failed navigation, stat failures, selection
preservation/removal/reordering, viewport bounds, long names, file/directory
metadata, Ctrl+R, Enter, file Open, and handle cleanup. Deterministic failure
injection covered listing, stat, and path overflow. The focused stress passed
**1,000 refreshes** and **100 navigation operations**. Boot 3 completed 25
close/relaunch browse cycles with unique lifetimes; the final observed
instance remained open for Task Manager inspection.

Regression evidence from the completed run:

- C151: C145 dialogs 49/49 and C151 core 10/10.
- C152: Save dialog 16/16, document state 10/10, 50 verified save operations,
  and five Save As paths.
- C137: standalone suite 46/46; real QMP wheel moved the ListBox viewport.
- C156: modifier decoder 10/10 and shortcut routing 15/15; Control and Shift
  ended released.
- C150: managed lifecycle 8/8 and canonical return-target suite 10/10.
- C160/C161: identity and snapshot suites passed; Task Manager observed the
  managed File Explorer by its exact managed identity.
- C162: accepted baseline manifest validated; boot 3 exercised exact-identity
  Managed Calculator close. Native File Explorer was separately launched and
  closed by its exact native identity.
- Notes: Ctrl+N, edit, Copy, and clipboard preservation across File Explorer
  and Task Manager passed. The shared clipboard remained independent of this
  app.
- Managed Calculator: `7 * 8 = 56` passed.
- Native File Explorer remained registered, separately identified, and
  launchable.
- C128 remains **unverified**; no C128 suite result is claimed.

The proof-only C151/C152 directory fixture is generated as real VFS media and
is isolated from the ordinary production ramdisk. Managed File Explorer does
not synthesize rows in either build.

## QEMU boots and artifacts

The proof kernel was built with the proof composite. The clean production
composite/kernel/ramdisk were then installed and verified before three
ordinary boots. Both proof and ordinary boot sets passed 3/3.

| Proof boot | Scenario | Serial SHA-256 |
| --- | --- | --- |
| 1 | Pointer navigation, file details, wheel, Ctrl+R, Up, close | `73095D556A1F9CA83165447A2AE2953F036ACBC69EF9B9F91F4FA00403496C8D` |
| 2 | Keyboard navigation, Tab/Shift+Tab, Refresh, relaunch | `A9002A4430ED034735F76C320D3CFD0DA6E1DC5E8FC77A649DAED1BEA8C98F56` |
| 3 | Error recovery, 25-cycle lifecycle stress, Task Manager and native app | `E23C49DC34D07AE17FB3333651C0C05FCB6B7510D7F9731033B08C2C98EDD94E` |

| Ordinary boot | Status | Serial SHA-256 |
| --- | --- | --- |
| 1 | PASS | `2157C865658E3F72337A4666B6252F9FC5D2AE4B806B8A70BAD79D3B133C1339` |
| 2 | PASS | `B8A19D430E41CE37E1D4ADD89D4DBE9A36A3A1137B8F7584C5859775547A587A` |
| 3 | PASS | `14506D0D55840BBC55B420397E9ACC37389342B2502A786DBA661BEB80C6F385` |

| Built artifact | SHA-256 |
| --- | --- |
| Proof NativeAOT composite ELF | `BBFFDE2A060D5902907462A2BCB637F4537E23087E8ACCF9D661CC04E9AFDC8F` |
| Clean production NativeAOT composite ELF | `4D01C66408534A7BB3D91806B58CC5F9698D3A431FA11E5CC86EB1A0E18B0111` |
| Proof kernel | `D7D03F7F005C8CA03DB56ACF189456792B6367B8415D50514190C2DC57AF6F5F` |
| Proof ramdisk | `71A9DC2E0B8DE05D4C1529297937C6E36621E5EF84987A3B5DD171DCF34CD6D4` |
| Clean production kernel and ESP kernel | `02817323329C5CA7A6B0DAB67C8709CD60CDED9B3A4F2B096C9D1194E503BE4B` |
| Clean production ramdisk and ESP ramdisk | `7D8EB9AABC6016BD2C8EBC2F33F5805BEDEE3F15368F9F31C4335C3D80E43266` |

The build completed successfully. It emitted existing NativeAOT/PDB,
unused-code, and compiler warnings; no build errors occurred. Full proof and
ordinary manifests and serial logs are under
`out/dotnet/c163-managed-file-explorer/`:
[`c163-proof-manifest.json`](../../out/dotnet/c163-managed-file-explorer/c163-proof-manifest.json)
and
[`c163-ordinary-manifest.json`](../../out/dotnet/c163-managed-file-explorer/c163-ordinary-manifest.json).

Final proof state: Managed File Explorer active, lifetime `0000001E`, path
`/system/apps`, no selected entry, viewport 0, 5 controls. Control and Shift
were released and balanced; modal owner, popup capture, and drag owner were
all none. Settings stayed v2 and host ABI stayed v3/120 bytes.

## Git transport

Repository remote: `origin` = `git@github.com:guideX/guideXOS-Server.git`.
`core.sshCommand` is unset. After committing, the required exact Git transport
probe (`git ls-remote origin`) and the single ordinary `git push` were run.
Their exact results are recorded in the C163 closeout report for this change,
because the push result is only knowable after the commit exists. No remotes,
SSH settings, keys, credentials, or Git identity were changed.
