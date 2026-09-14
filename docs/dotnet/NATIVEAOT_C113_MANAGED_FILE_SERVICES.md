# NativeAOT C113 Managed File Services

## Scope

C113 extends the reusable C112 managed application model with a bounded,
capability-gated file service. The proof application is selector 4,
`Managed Notes`, launched from both the Start menu and All Programs. Its
native image is the reusable composite image at `/system/apps/GXOSAPP.ELF`.

The implementation starts from C112 commit
`ec51c5da563d604b964b9ec3d404f47e361b8c45` (`Add reusable NativeAOT managed
application model`). C113 preserves the C112 host-table prefix and adds only
append-only file-service fields.

## Managed ABI

The host table remains ABI version 1:

| Contract | Value |
| --- | ---: |
| C112 prefix size | 72 bytes |
| C113 full size | 88 bytes |
| `fileReadAll` offset | 72 bytes |
| `fileWriteAll` offset | 80 bytes |
| maximum path length | 96 bytes |
| maximum file length | 16 KiB |

The managed facade checks the table size before reading optional fields and
checks the corresponding capability bit before invoking a callback. File
buffers are managed-owned. Native code copies paths and file contents only
during the synchronous callback; it retains no managed pointer after return.

Reads use a bounded two-call protocol. The first callback call asks for the
file length with a zero-capacity buffer. A `BufferTooSmall` result reports the
required length, after which managed code allocates exactly that bounded size
and performs the read. This avoids an unnecessary fixed 16 KiB allocation on
resident re-entry while retaining the 16 KiB service limit.

The stable file results are:

| Result | Value |
| --- | ---: |
| Success | 0 |
| NotFound | -10 |
| InvalidPath | -11 |
| BufferTooSmall | -12 |
| FileTooLarge | -13 |
| IoFailure | -14 |
| CapabilityUnavailable | -15 |
| InvalidArgument | -16 |

The native path policy accepts printable ASCII canonical paths rooted at
`/system/apps/`, rejects backslashes, repeated separators, and `..`, and
limits the path to 96 bytes. The service exposes regular-file reads and
creates or overwrites bounded regular files through the kernel VFS.

## Managed Notes proof flow

`ManagedNotes` uses `/system/apps/NOTES.TXT` and follows this flow:

1. First launch observes `NotFound` and displays the managed initial note.
2. The Append action changes the managed byte array to
   `Hello from Managed Notes [edited]`.
3. The Save action writes the bytes through the capability-gated VFS service.
4. Native code independently reads the VFS file and verifies the exact text
   and FNV-1a hash `AC3E5E4C`.
5. A fresh selector-4 launch reads the saved content from VFS.
6. Reload reads the file again through the managed service.
7. A capability-downgraded selector-4 launch omits `fileWriteAll`; the managed
   wrapper returns `CapabilityUnavailable` and the application completes a
   safe read-only downgrade.

The mixed regression sequence also launches native Notepad, managed Counter,
and managed Status between Notes launches. ABI mismatch, missing capability,
invalid paths, missing files, oversized files, invalid read buffers, and
invalid write data are exercised without changing the normal C112 behavior.
Managed Notes also increments a small `[ThreadStatic]` launch sentinel and
checks it against its per-application launch count on every resident launch.

## Persistence semantics

The writable `/system` image is a RAMDISK-backed FAT filesystem attached from
the staged wallpaper pack. File writes persist for the lifetime of one boot
and are independently readable by native VFS code and later managed launches
within that boot. Each fresh QEMU boot recreates the image, so C113 makes no
cross-boot persistence claim (`persistenceBootCount=0`).

## Build and proof commands

From the repository root:

```powershell
powershell -File scripts/dotnet/build-managed-hostlog-proof.ps1 `
  -UseGuideXosRuntimePack -ProductionApplication `
  -PersistentCompositeLifecycle -AllocationMode Allocating `
  -ManagedProjectMode C113Composite

mingw32-make -C kernel -B ARCH=amd64 `
  "EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION -DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES"

powershell -ExecutionPolicy Bypass -File scripts/dotnet/run-c113-managed-file-services.ps1 `
  -FreshBootCount 3
```

The runner records the exact repository/build inputs, ABI contract, command
line, per-boot serial output and hashes, mixed-sequence classification, and
`c113.manifest.json` under:

`out/dotnet/c011ec113-managed-file-services/`

The manifest is the source of truth for the completed fresh-boot count and
the recorded serial hashes.
