# S22R28R3R3 — Corrected RAMDISK Guest Acceptance

**Outcome A: Final corrected CPL3-off and CPL3-on guests passed the RAMDISK, desktop, persistence, and protected-task gates. The separate QAC-managed Navigator smoke also passed its VFS/resource checks; all of our guests shut down through the desktop and QAC released our reservations.**

## Repository and preservation

- Starting branch: `main`.
- Starting `HEAD`: `5f8720519a5b4cce735525016da9853640fd361f`.
- Starting local `HEAD` matched `origin/main`; ahead/behind was `0/0`.
- Existing `assets/Backgrounds/NEW` wallpaper files were inspected and preserved.
- The accepted S22R23 ISO remains `90,245,120` bytes with SHA-256 `8d397659e0a6ec8f1be113ae4f5be14dbcaf1ef22ccda41a6e5ee5014919cf40`; its adjacent checksum receipt matched.
- The S22R28R1 diagnostic ISO remains `3168d943af704a730110ab1e669ab482c3e93f282bbd7d2f73283f490b2d785c`.
- The S22R28R3R2 diagnostic ISO remains `cc8370414cd422e3b459378961324ccd1a50e4b2dbb09ef456444cc77fd9860e`.
- The accepted `GXOS-DATA-V1` source image remains SHA-256 `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148`. QEMU runs used fresh writable copies.

## RAMDISK mapping and first guest failure

BootInfo v3 continues to provide the RAMDISK's physical base. The kernel maps those physical frames at the separate supervisor alias `0x0000400000000000`; block reads and FAT use the alias. The complete 64 MiB extent is mapped as 16,384 4 KiB pages, including the non-page-aligned length case in the hosted regression. Kernel image and BSS mappings remain intact, and the mapping remains supervisor-only. No broad identity mapping was added.

The first corrected CPL3-off QAC run used ISO SHA-256 `8dd3bb7c5b5b6b2c65a2527f4252ba59490d4380ce21b0c9aba134d800a35dfb`. It verified the alias after CR3 activation and reported the expected source/guest sector-zero FNV-1a `0x65EC0D6EC04D02C3`. The physical base, alias base, full extent, and supervisor permissions matched. FAT32, `/`, and `/Apps` mounted, `/Apps/PacMan` was discoverable, and the desktop loop continued.

That run emitted the generic `[MEMORY-OWNERSHIP] status=overlapping-reservation` and therefore did not initialize frame ownership. An instrumented CPL3-off diagnostic separated BootInfo reservation overlap from page-table tree validation while retaining the original 512-table bound. Its QAC guest reported:

```text
[MEMORY-OWNERSHIP] status=page-table-tree-rejected reason=page-count-limit pages=0x00000201 limit=0x00000200 user-mode=disabled
```

No BootInfo-range overlap was reported. The guest's page-table walk reached page 513 and failed the 512-page ceiling. The same run independently reported the expected RAMDISK handoff fingerprint, a successful LBA 0 read with FNV-1a `0x65EC0D6EC04D02C3`, valid FAT32 BPB fields, and `final_stage=MOUNTED`. QAC used reservation `564773afd1e04435b554d304e56f60ab`; the diagnostic was stopped with owner-safe QMP `quit`, QAC observed child exit code 0 and released the reservation. Its data copy hash remained identical to the accepted source hash.

The targeted hosted regression constructs a valid 525-page page-table tree. It reproduces `PageCountLimit` at the former 512-page bound, accepts the tree at 1,024, and separately rejects protected-range overlap. The smallest justified repair is to align the kernel audit ceiling with the loader's 1,024-table tracking capacity; final corrected guest results are recorded below.

## Build and diagnostic artifacts

Both final candidates are explicitly `DIAGNOSTIC_NOT_PUBLIC_RELEASE`. They include BootInfo v3, loader RAMDISK stage fingerprints and integrity diagnostics, kernel FAT/RAMDISK diagnostics, the corrected alias, the 1,024-page loader and kernel page-table audit capacities, and the Public Preview security profile. CPL3 diagnostics are disabled in the initial candidate and enabled in the separate protected-task candidate.

| Candidate | ISO SHA-256 | Embedded ESP SHA-256 | Kernel ELF SHA-256 | CPL3 |
| --- | --- | --- | --- | --- |
| Final page-table-bound CPL3-off | `07185d8cfc3d90598ac2e8c3abbb540d4e4519f5c8465add2c80bef0c326720e` | `2c9356da7f6956576b09dd3a1e79eec01e140867fad147ee9ab5ef5cdaf670c2` | `651663ac917337d45c84e328b325ce2e3bb137233fb7425d061e78b2a664f914` | Off |
| Final page-table-bound CPL3-on | `30909e3ec5325fec8458a5223ffbd49863f79abe6c15b162a83bef7d7f4532dc` | `8227fcceda3ec363455790928d4da4606800f94c1aaa06c1c4fa14162b5a4103` | `0edc0b78bcf262b4ed244251cd787b37653c394e4ec0a14d32cd193e32e92b98` | On |

Both ESPs and ISOs passed the repository's independent `release_esp.py` and `release_iso.py` verification. Each ISO is UEFI El Torito bootable (platform `0xEF`, no emulation), 90,245,120 bytes; each ESP is 90,177,536 bytes. The loader SHA-256 is `ac55ce9ed59f09ab4489b944fab31176b97691f1702c5da4150655fc1455772d`; the shared RAMDISK SHA-256 is `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05`.

Candidate manifests and exact file inventories are in the external candidate directories under `E:\CodexArtifacts\S22R28R3R3\corrected-candidates`. Both manifests identify source-input fingerprint `eafa468815b38e5d27be0c62f32e0ac555683206506e95eb03d34312ac620d93`, using SHA-256 over sorted `path NUL file-SHA256 LF` records.

## Hosted and freestanding validation

- BootInfo v3, RAMDISK alias/paging, RAMDISK/FAT/VFS, frame ownership, private page tables, CPL3 entry/exit contracts, Microsoft x64 context-switch ABI, Native ELF policy/integrity, Settings, persistent Documents/AppData, and UserDataVolume mount-policy preflights passed before guest qualification.
- The updated AMD64 memory-foundation regression passed after the page-table-bound repair, including the 525-page tree and protected-range cases.
- Final CPL3-off and CPL3-on freestanding AMD64 builds passed with the Public Preview profile and RAMDISK/FAT diagnostics.
- Both exact final kernel ELF/PE pairs passed the symbol audit with no relocation or import sections: off 53 undefined rows / 22 distinct names; on 55 rows / 22 names.
- The established 16 KiB stack guard passed for both builds with 4,520 bytes minimum modeled headroom. These compiler-derived figures are modeled stack estimates, not runtime measurements or a whole-kernel stack proof.

## QAC and final guest gates

The diagnostic guests used QAC 2.0.0 with verified ownership, the read-only candidate ISO, read-only firmware code, a disposable variable-store copy, a writable data-volume copy, loopback QMP, and `-net none`. QEMU was started as QAC's direct child. Prelaunch QMP inspection confirmed the intended ISO and disk permissions before `cont`.

The final CPL3-off guest used reservation `15b5e322548148b09cde5fabdfec58ae`, QAC CLI PID 2036, and QEMU child PID 14508. It exited after the guest desktop shutdown action with QEMU exit code 0 and QAC released the reservation. QAC briefly warned that one unmanaged QEMU process was present while this run was active; no process was interrupted or changed. The final status returned to 0 QEMU processes, 0 foreign processes, and 0 reservations.

The final CPL3-on guest used reservation `823f14635ac9445b99074c16b165f554`, QAC CLI PID 23164, and QEMU child PID 25800. Its guest shutdown completed with exit code 0 at `2026-11-27T21:12:28.5810596Z`; QAC released the reservation. A subsequent status check reported the coordinator online, no managed sessions, no queued or blocked requests, and 0 QEMU / 0 foreign processes.

The separately requested Navigator check used QAC reservation `d8f75791ec4a4d2e8b6827b036a02dbb`, QAC CLI PID 20824, and QEMU child PID 23416. It queued behind another owner's acceptance reservation `e1b98acf06df4c1e8a94829ad302e081`; that guest was left untouched. After the lane became available, QAC admitted the run. The Navigator guest shut down through the desktop with exit code 0 and QAC released our reservation. The subsequent QAC list showed another verified owner running acceptance reservation `44440fc85a1342fbbc49f67682c67c29` (QEMU PID 19032, owner PID 25212); QAC reported 1 managed QEMU and 0 foreign processes. That session was left untouched, and none of our reservations remained active.

### Gate A — RAMDISK and VFS (pass)

The final off guest reported a valid BootInfo v3 handoff and loader stage fingerprint `base=0x150D7000`, `bytes=0x200`, `fnv=0x65EC0D6EC04D02C3`. After CR3 activation the kernel verified the complete 64 MiB alias at `0x0000400000000000`, mapped over all 16,384 backing pages with present, writable, supervisor, executable permissions. Its alias read and `wallimg` LBA 0 read both returned `0x65EC0D6EC04D02C3`; the BPB signature was `0xAA55`, 512 bytes/sector, 16 sectors/cluster, 32 reserved sectors, and two FATs. FAT32 mounted at `/`; `/Documents` and `/AppData` mounted, `/Apps` was exposed, and the app model discovered `/Apps/PacMan`. The desktop event loop and heartbeat continued.

The final on guest independently reported loader handoff base `0x150D6000` with the same 512-byte fingerprint. After CR3 activation its kernel alias covered the same full 64 MiB range and matched the same FNV-1a value. It reported `[MEMORY-OWNERSHIP] page-table-tree=valid pages=0x212 limit=0x400` and `status=ready arena-pages=0x1000 free-pages=0x1000`. RAMDISK attachment, LBA 0 read, FAT32 `/` mount, `/Documents`, `/AppData`, and `/Apps/PacMan` discovery all succeeded.

### Gate B — Desktop, input, Settings, Navigator, and PacMan (pass)

On the final off candidate the desktop rendered, real mouse clicks opened and closed windows, and keyboard input was delivered to the guest. Settings opened and closed; applying Personalization selected Dinos and logged `background.apply status=0 active=legacy_dinos persisted=legacy_dinos`. File Explorer displayed the mounted `/Documents` directory and the `/AppData` alias. PacMan launched from `/Apps/PacMan`; guest diagnostics showed the approved ELF path, package-relative resources, native ABI/integrity checks, a presented interactive frame, and processed key down/up events. Escape followed through `EXIT_REQUESTED`, window/resource cleanup, and `[PACMAN-SMOKE] result=PASS proof=approved-entry-clean-return-presented-frame-key-events`; the guest returned to the desktop. PacMan remained on its existing CPL0 production path. A separate relaunch was not exercised.

The separately requested Navigator check launched `guideXOS Navigator` from All Programs on the corrected CPL3-off ISO. Its guest smoke reported `[NAVIGATOR-SMOKE] result=PASS`, `current.url=about:navigator-runtime`, root `/` and `/system` FAT32 mounts, and a successful 256-byte read of `/certs/ca-bundle.pem` through the `/certs` alias. The interactive window rendered `about:navigator`, and VFS logs confirmed all six toolbar icons and twelve throbber images loaded from `/system/config/navigator`. The default Help link targets `/docs/index.html`, which this diagnostic image does not contain; the guest logged `File not found: /docs/index.html`. This missing optional Help page did not affect the corrected RAMDISK, VFS, desktop, or Native ELF acceptance. A Navigator read from the writable user-data volume was not separately demonstrated.

After the three protected-task cycles, the CPL3-on desktop continued to render the Dinos wallpaper and PIT heartbeat. Real mouse input opened File Explorer; the post-cycle view showed `/Documents` and its existing entries. A second view of `/AppData` showed `DESKAPP.CFG` (65 bytes) and the existing `UNTITLED.TXT`. The desktop Start menu also opened by keyboard, demonstrating continued input/event handling.

### Gate C — Persistent data (pass)

Both guests used writable disposable data-volume copies. The accepted source image remains SHA-256 `3ee67553ab9bece4130e4aa84ec4ed0d341cb29d7fd78798884c5ae3b0b7b148`. After normal shutdown, a read-only FAT32 comparison of the guest-written copy against the accepted source found exactly one added path, `APPDATA/DESKAPP.CFG`; `changedPaths=[]` and `missingPaths=[]`. Existing files, including `/Documents` entries `GXOSDAT.ID`, `SECOND.TXT`, `SECOND2.TXT`, `SECOND3.TXT`, and `UNTITLED.TXT`, retained their original sizes and SHA-256 values.

`APPDATA/DESKAPP.CFG` is 65 bytes with SHA-256 `731aa9a16876d099cd0c7065f15838b68fd6ecf7234330fc3cfb5c9749e129d2` and content `format=GUIDEXOS-DESKTOP-STATE\nversion=1\nwallpaperId=legacy_dinos\n`. On the following cold boot, the CPL3-on guest opened `/AppData/DESKAPP.CFG` during desktop startup and restored `legacy_dinos`. The original accepted image remained unchanged. No executable was launched from writable user data; the Native ELF guest launch was the approved `/Apps/PacMan` package.

### Gate D — CPL3 cycles and post-cycle operation (pass)

The CPL3-enabled candidate completed these three bounded cycles:

| Task | Actual CS | Trap / return | Result | Cleanup |
| --- | --- | --- | --- | --- |
| `0x00522800` | `0x23` | `int 0x80`; kernel CR3 restored | `42` | `cleanup=verified state=reaped` |
| `0x00522801` | `0x23` | `int 0x80`; kernel CR3 restored | `42` | `cleanup=verified state=reaped` |
| `0x00522802` | `0x23` | `int 0x80`; kernel CR3 restored | `42` | `cleanup=verified state=reaped` |

The guest concluded `[CPL3] status=passed cycles=3 nonfaulting=1 preemption=0`. The protected-task cleanup path compares frame accounting before and after teardown and printed `cleanup=verified` for each task. The PIT heartbeat continued, the desktop remained responsive to keyboard and mouse, and `/Documents` and `/AppData` remained accessible after the cycles. Timer preemption was not enabled or claimed.

Both final guests shut down through the desktop's confirmation flow. The final serial line was `[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop expected=1 runtime=none app=none`; QAC observed the QEMU child exit code 0 and released the reservation. No coordinator restart, forced reservation clear, or foreign process termination occurred.

## Remaining unqualified capabilities and next milestone

This work does not qualify CPL3 fault recovery, timer-driven preemption, safe forced termination, kernel survivability from non-cooperative applications, or application migration of PacMan. No release-readiness marker was changed; no signing, promotion, or publication was performed.

After corrected filesystem and CPL3 integration are accepted, the recommended next milestone is **S22R29 — Recoverable CPL3 Fault Handling**: preserve exception frames, classify user-origin faults, keep the kernel continuation alive, track faulted-task lifecycle state, reclaim frames off-stack, preserve accounting/page-table safety, and determine timer-interrupt and later scheduler requirements. The owner requirement remains that an application can stall or fault while guideXOS Server stays usable and the application can eventually be terminated without rebooting the OS.

## Final outcome

Outcome A, `CPL3_RAMDISK_DESKTOP_GUEST_ACCEPTED`, passed: the corrected RAMDISK/VFS, desktop and supported app operation, persistent data, three CPL3 task lifecycles, continued kernel operation, and Navigator's QAC VFS/resource smoke all completed. The diagnostic image's Help target `/docs/index.html` is missing, and Navigator access to a file on writable user data was not separately demonstrated. Fault recovery, timer-driven preemption, safe forced termination, residual security-risk acceptance, signing, promotion, and publication remain outside this acceptance.
