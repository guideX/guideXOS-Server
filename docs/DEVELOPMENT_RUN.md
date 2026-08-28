# Hosted Development Run ABI

The hosted Server exposes Run Project only to the canonical Developer Studio
Native ELF application (`com.guidexos.developerstudio`). It is not a generic
package installer, arbitrary ELF launcher, production sandbox, or bare-metal
filesystem contract.

## ABI

The append-only `gx_host_calls` slots are:

```text
192  development_run_prepare
200  development_run_start
208  development_run_poll
216  development_run_request_close
224  development_run_release
sizeof(gx_host_calls) = 232
```

Phase 27E appends the separate bare-metal Developer Studio build/VFS block
after this hosted-development block; the full table is 312 bytes when those
slots are advertised. The hosted Run contract and its offsets remain
unchanged.

Requests and snapshots carry `size` and `version`. The request contains fixed
caller-owned strings for project root/ID/kind/target, `app/app.json`, the build
artifact path, and the build SHA-256. The snapshot returns a generation-bound
handle, state/error, child process ID, native runtime ID, owned-window counts,
exit code, cleanup status, application identity, display name, artifact hash,
and a bounded error message.

## Preparation contract

Preparation is fail-closed and revalidates all inputs. The current supported
contract is:

- absolute, non-symlink project root with bounded `guidexos.project`;
- `native-gui-application` and `guidexos.amd64.hosted.native` only;
- manifest path exactly `app/app.json` and artifact path exactly
  `build/<manifest entry path>`;
- NativeElf, one amd64 entry, `gx_main`, `guidexos-c-abi-v1`, `native-elf`;
- exactly `log`, `window`, and `draw` permissions;
- no file associations or desktop registry hints;
- non-symlink regular artifact inside the project root, at most 64 MiB;
- SHA-256 equal to the successful Build Project snapshot;
- valid ELF64 AMD64 static `ET_EXEC` image containing `gx_main`.

Installed App Model IDs, reserved Developer Studio-owned namespaces, stale
handles, owner mismatches, malformed manifests, and unsupported claims are
rejected. The temporary `RegisteredApp` uses `AppSourceKind::DevelopmentTemporary`
and remains in the in-memory registry only.

## Lifecycle

The service owns eight bounded deployment slots. A handle encodes the slot and
generation; releasing a terminal deployment increments the generation. Start
resolves the temporary record through the normal AppLaunchResolver and
DesktopService NativeElf path. Poll observes the existing ProcessTable,
NativeAppProcessTable, and compositor window ownership. Request-close publishes
`MT_Close` only for deployment-owned windows.

On child exit, cleanup unregisters the temporary App Model entry, records the
exit status, reports `cleanupComplete`, and permits release. On Developer Studio
runtime cleanup or server shutdown, owner-bound windows and temporary records
are cleaned without terminating an unrelated process. A native child handles
its own close event through the existing runtime cleanup path.

## Validation

The ABI offsets and structure sizes are asserted by
`tests/native_abi_layout_test.cpp`. The experimental full Server build includes
`development_run_service.cpp` and is run with:

```text
cmd /c .\build-native-experimental.bat
```

`nativeapp.capabilities` reports the five development-run calls and the
revalidation policy. The Studio-side controller test and package build are
maintained in the Developer Studio checkout. Interactive F5 driving remains a
separate compositor-input validation step; the experimental shell exposes
`gui.key` for focused-window checks, but it is not a replacement for visual
manual validation.
