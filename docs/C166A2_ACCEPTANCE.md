# C166A2 Acceptance Record

## Result

**Accepted — Outcome A. C166 acceptance gates passed.**

Fresh host validation, a coherent three-boot PROOF1 run, exact persistence comparison, 26 managed persisted mutations, Explorer parity in Disabled and restored-default states, affected regressions, a proof-free clean production build, and three C166-specific ordinary boots passed. The clean kernel and ramdisk were installed. The final QEMU process check found only an unrelated Phase35Q guest; all QEMU processes launched by this acceptance run had been stopped. The runner now checks only its own recorded QEMU PIDs and leaves unrelated guests alone.

## Source and protected production state

- Branch: `v1.1_DOTNET_SUPPORT`
- Acceptance source HEAD: `67354b7bc4365288378cb77a91ed456986997b35`
- Starting divergence: one commit ahead of `origin/v1.1_DOTNET_SUPPORT`, none behind.
- The C166A evidence commit and prior manifests remain preserved.
- The saved pre-acceptance kernel and ESP ramdisk backups remain under `out/dotnet/c166a2-final-acceptance/protected-media-before-c166a2/`.

## Fresh host validation

`C166R9_HOST_VALIDATION_PASS` was reproduced:

- Native host cases: 604 passed, 0 failures; 136 partial-write cases.
- Managed ABI cases: 34 passed; sizes 104/112/120/128 and callback offsets 104/112/120.
- Dual-slot agreement cases: 31 passed; stress: 1,000 transactions, 1,000 audited images, 0 CRC mismatches, 0 malformed commits, 500 commits per slot.
- Proof-disabled production gate: passed.

Evidence: `out/dotnet/c166a2-final-acceptance/host-validation/c166r9-host-manifest.json`.

## Coherent guest proof

Fresh proof products built from the acceptance source HEAD:

| Product | SHA-256 |
| --- | --- |
| Proof composite | `E545DEC347B833DCD00F4126F46CA2CAE3BD3FD94B9F68BBB18C9B899418E2BE` |
| Proof kernel | `BA76D6D039386F0B000853E40236AEC68D44DFCAE099C06B2E85F6FDE922BC80` |
| Proof ramdisk | `A0AAEEA85C71769E6A794A9DD8BF83F0F54B2F962750C4A9CD588D2F66DA4D0F` |

One coherent run completed all three guest boots. Its manifest is `out/dotnet/c166a2-final-acceptance/proof/proof1-guest-runs/proof1-manifest.json`.

| Boot | Outcome | Serial SHA-256 |
| --- | --- | --- |
| Boot 1 — SameBoot | PASS | `04F83C6C627ACBDC7BF89ACAD613246576CD695C070F170355A8C54F729825FA` |
| Boot 2 — PreReboot | PASS | `4703BFBAC0FDD47CEC10852420225CCC82421CEF73CC6A6B63B435CB45FE5458` |
| Boot 3 — PostReboot | PASS | `C76596C26830915CEB0E89E80EF2D8EC4F123CB19B33F6ABEB16F57ED36A3E70` |

Boots 2 and 3 used the same writable FAT ESP. Boot 2 directly observed Disabled in slot A, generation 3. Boot 3 reloaded the same A/3 state. Its post-reboot managed handler completed 26/26 persisted mutations, generations 4–29, alternating A/B, ending at slot A generation 29 with NoOverride; slot B held the preceding Notes override at generation 28.

### Explorer parity and Notes

- **Disabled:** In Boot 2, Managed and Native File Explorer opened `/system/apps/C164/hello.txt` and reported no associated handler. Managed Notes launch count remained 2 for both workflows. Slot images and generations were unchanged.
- **Restored default:** Boot 1 observed B/2 NoOverride with Managed Notes effective; Managed File Explorer opened the actual VFS fixture. After Boot 3 reloaded A/3 Disabled and completed mutation churn, Native File Explorer opened the fixture under restored NoOverride state.
- Both default-handler workflows activated `/system/apps/C164/hello.txt` in Managed Notes with exact content `hello from C164` (15 bytes), clean state, undo/redo 0, and successful fresh activation.
- Boot 3 interaction left the persisted A/29 NoOverride and B/28 Notes override images unchanged.

## Clean production build and install

The clean production composite, kernel, and ramdisk passed the proof-string exclusion scan. The same candidates booted in all three ordinary runs and were installed:

| Product | SHA-256 |
| --- | --- |
| Production composite | `FBEDB86A2B1CA1B3F00915BE67F46A0B866A9F0651544F1C0404F9D7038D808C` |
| Production kernel and ESP kernel | `CD05A55D54D8AA074E3C2539370E120725A1F81E39D9DE6D517895E85C0C18BB` |
| Production ramdisk | `8927F57A0A1CE7DFF082299D23896012AD33EA68BFCCF16B7BCE4EB0CB57E5FC` |

All three products were scanned for `GUIDEXOS_PROOF_CONTROL`, PROOF1 markers, and selector strings; none were present. The installed ESP has no `gxas0.bin` or `gxas1.bin` override slots. Ordinary file activation verified the default `.txt` handler as `com.guidexos.apps.managed.notes`.

## C166-specific ordinary boots

All three fresh ordinary boots passed with the exact installed clean candidate. Evidence is in `out/dotnet/c166a2-final-acceptance/ordinary-boots/ordinary-run-20261010-232240-14a28ef6/`.

| Boot | C166 workflows | Serial SHA-256 |
| --- | --- | --- |
| 1 | Managed File Explorer opened the exact C164 fixture in Notes; clean state and content verified. C156 modifier and shortcut regressions passed. | `BF7FA5E7DD9C229E3573FFED56FA51BC119B81B517ACD559E136DAAC8DD7885E` |
| 2 | Native File Explorer opened the fixture in Notes; Managed Task Manager identified and safely closed native File Explorer; C156 regressions passed. | `12123CB55B5E5983A5C72B27DD722498690DD29D7A88EF58A668D9E0175BC1E4` |
| 3 | Managed File Explorer opened the fixture; Calculator evaluated `7*8=56`; Task Manager snapshot/close and Notes focus passed; Settings Center launched, loaded the 26-byte v2 record, reported no modal/capture/drag, and returned cleanly. C156 regressions passed. | `5D8D8D38F9E021AEE6566E5C5E3A3D4B277CBB4975F25ACFF39852E8C7B64CD3` |

Each boot had no PROOF1 output and no association override files on its ESP. Settings record SHA-256: `864F8DF20EF04113394EBFCED0015F8FAA54701FE06419EE44604530BAADA529`.

## QEMU ownership and regressions

The initial QEMU preflight was clear. An unrelated `guideXOS-Phase35Q` process (PID 27756, from `D:\dev\guideXOSUEFI_Phase35R12_SANDBOX`) appeared during ordinary Boot 3. The runner-owned guests exited normally; no unrelated process was terminated. The earlier post-run guard treated every QEMU process as runner-owned and returned an error after the candidate install. That guard now checks only PIDs created by this acceptance run.

- C164: real Notes activation passed in proof and all ordinary boots.
- C163: managed Explorer browsing/open passed in proof and ordinary boots.
- C162/C160: identity-safe Task Manager close and snapshot checks passed.
- C156: modifier decode and shortcut routing passed in all three ordinary boots.
- C128: unverified; prior C166A evidence remains preserved.

## Evidence files

- Acceptance manifest: `out/dotnet/c166a2-final-acceptance/c166a2-manifest.json`
- Fresh host manifest: `out/dotnet/c166a2-final-acceptance/host-validation/c166r9-host-manifest.json`
- Proof manifest: `out/dotnet/c166a2-final-acceptance/proof/proof1-guest-runs/proof1-manifest.json`
- Production build logs and artifacts: `out/dotnet/c166a2-final-acceptance/proof/clean-production-candidate/`
