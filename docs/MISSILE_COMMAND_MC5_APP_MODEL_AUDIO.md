# Missile Command MC5 — App Model Audio Output with First Native Game Sound

Status: MC5 complete, **Outcome A**. Read alongside
`docs/MISSILE_COMMAND_MC1_ARCHITECTURE.md`,
`docs/MISSILE_COMMAND_MC2_DEFENSE_LOOP.md`,
`docs/MISSILE_COMMAND_MC3_CAMPAIGN_SMARTBOMBS.md`, and
`docs/MISSILE_COMMAND_MC4_CAMPAIGN_CITYART_AUDIO.md` (whose §7 Outcome C and
§8 proposal this phase implements, with one deliberate shape change
documented in §3: PCM-buffer `play_pcm` instead of the proposed
resource-name `play_sound`).

Outcome: a **small reusable App Model audio capability** (append-only ABI
slot + `audio.output` enforcement + mixer + hosted backend) with Missile
Command as its first real client playing the original WAV voices, validated
live against a real waveOut device. No Missile-Command-specific hack: the
game uses the same public call any future app uses.

---

## 1. Prior audio architecture discovered (revalidated, not assumed)

- App Model ABI (`sdk/include/guidexos/abi.h`): zero audio host calls
  (log, window, draw, poll, exit, file_*, build-service, dev-run,
  dev-debug only).
- `audio.output`: existed only as a known permission string in
  `app_manifest_validator.cpp`. Nothing enforced, dispatched, or honored it.
- Kernel: Intel HDA/AC'97 driver (`kernel/.../pci_audio.*`: enumeration,
  CORB/RIRB bring-up, stream-descriptor register definitions) and USB audio
  (`usb_audio*`: probe, volume/mute, `start_stream`, `playback_write` at the
  driver level). No mixer anywhere (`kernel/docs/HARDWARE_SUPPORT_REPORT.md`
  lists Mixer as MISSING), no PCM submit path from any app runtime to any
  DMA engine, no path from a hosted Native ELF app to any of it. The only
  audio consumer is the desktop volume UI
  (`controller_count`/`device_count`/`get_mute`/`set_mute`).
- Hosted/experimental runtimes: no audio at all. No packaged or sample app
  used audio. No precedent to reuse.
- Full path before MC5: `Native App -> App Model ABI -> (nothing)`.

## 2. New App Model API (append-only, generic)

One appended host call (ABI-stability rule respected, §4):

```c
gx_result play_pcm(ctx, pcmData, pcmBytes, sampleRateHz, channels, bitsPerSample);
```

- **Why PCM, not a resource name.** MC4 proposed
  `play_sound(ctx, name, offset)` with host-side WAV decoding. Against the
  actual ABI this is the weaker shape: it ties the call to packaged files,
  needs a resource-ownership protocol, and cannot synthesize sound. A PCM
  buffer call matches existing conventions (`present_frame` already passes
  pixel buffers + format scalars), serves every app (file-backed or
  synthesized — the AudioBeep sample synthesizes), and keeps exactly one
  validation point. WAV decoding stays app-side (or build-time), exactly
  like GXIM art stays app-side.
- **Format contract** (`sdk/include/guidexos/audio.h`, `GX_AUDIO_*`):
  mono (`channels == 1`), 8-bit unsigned or 16-bit signed LE,
  `sampleRateHz` in [8000, 48000], `pcmBytes` in (0, 262144] and whole
  frames. Resampled voices additionally bounded to 4 s at the mix rate.
- **Semantics:** fire-and-forget (`SND_ASYNC` heritage: overlap allowed, no
  loops, no callbacks, no queries). `GX_OK` = queued for mixing.
  `GX_ERROR_BUSY` = all mixer voices in use (drop or retry).
  `GX_ERROR_UNSUPPORTED` = oversized input or overlong voice.
  `GX_ERROR_INVALID_ARGUMENT` = malformed input. `GX_ERROR_NOT_IMPLEMENTED`
  = host predates the slot (client helper) or has no backend (bare metal).
  `GX_ERROR_PERMISSION_DENIED` = no `audio.output` (§5).
- **Client helper** `gx_play_pcm()` (`audio.h`) checks the table size and
  slot pointer before dispatch, so a new app on an old host fails
  explicitly instead of reading out of bounds.
- **Lifetime:** the host copies + resamples at queue time; the app may
  free its buffer on return. Completion/timing never feeds back.

## 3. ABI compatibility (append-only proof)

- `gx_host_calls`: 240 -> **248 bytes**, `play_pcm` at offset **240**, every
  prior offset unchanged. `NativeHostCallTable` mirrors it exactly.
- `GX_API_VERSION` stays 0; the `size` field carries the growth, as with
  every prior era.
- `tests/native_abi_layout_test.cpp` **repaired properly** (it failed to
  compile from MC2 through MC4): the stale `sizeof == 120` pin (true only
  in the v1 base era, invalidated when `file_stat` was appended at 120)
  was removed with the history traced in-comment (120 -> 240 -> 248), every
  slot offset is now pinned individually (previously-unpinned
  draw_text/draw_rect/wait_for_close/poll_event/exit included), ordering
  asserts prove append-only growth, and `play_pcm == 240` / `sizeof == 248`
  pin the MC5 era. Old applications run unchanged (they only touch old
  slots); new applications detect old hosts via `gx_play_pcm`.

## 4. `audio.output` permission (now real)

- Validator still accepts the string (unchanged); both runtimes now
  **enforce** it with no identity special-casing (pure "list contains the
  exact token" checks).
- Hosted (`native_app_runtime.cpp::hostPlayPcm`): missing permission ->
  `GX_ERROR_PERMISSION_DENIED` + warn log, before any format work.
- Bare metal (`native_elf_baremetal.cpp`): manifest `permissions` array is
  parsed boundedly at discovery (`json_array_contains`, exact-token match;
  new `PackageInfo.hasAudioOutput` field); missing permission -> DENIED,
  present permission + valid args -> NOT_IMPLEMENTED (explicit, §7).
- Tested live: AudioBeep twin without the permission is denied twice and
  still exits silently and cleanly (§10).

## 5. PCM/sample format decision

Host carries exactly one format family (decoded PCM, mono, 8/16-bit,
8-48 kHz) and converges everything to **S16 mono @ 22050 Hz** at queue
time (nearest-neighbor resample; 8-bit widened `(b-128)*256`). Rationale:
22050 Hz covers the 11025/22050 Hz heritage assets without waste; one
mixer format keeps the mixer deterministic and unit-testable; apps convert
once (or at build time, like GXIM). No WAV/ADPCM parsing in the host.

## 6. Mixer/output path (`native_app_audio.h/.cpp`)

- `Mixer`: 16 bounded voices, saturating S16 add (both rails proven),
  deterministic `Mix()` advance with completion reclaim, `StopOwner()` /
  `StopAll()`. No threads, no OS calls.
- Validation (`ValidatePlayRequest`) and conversion (`ConvertToMixFormat`)
  are pure and shared by runtime and tests.
- Ownership: voices tagged by hosted runtime id; `NativeAppRuntime::Cleanup`
  calls `BackendStopOwner()`, so exit (clean or abrupt) leaves no stale
  voices and relaunch starts clean. One noisy app can at most hold 16
  short voices; the 17th request gets `BUSY`.
- Malformed data cannot corrupt the mixer: validation precedes all copies;
  overlong voices are refused before allocation.

## 7. Hosted vs bare-metal behavior

- **Hosted:** shared mixer + render thread + WinMM `waveOut` (22050 Hz
  mono S16, 4x1024-frame buffers). Opens lazily on first play; write
  errors degrade explicitly to a **null sink** (same mixer semantics,
  output discarded, identity logged — never pretended success). No device
  at all behaves the same way. `GXOS_AUDIO_BACKEND=null` forces the sink
  (deterministic CI). Requires `-lwinmm` (added to both `build*.bat`).
- **Bare metal:** validates + permission-checks, then returns
  `GX_ERROR_NOT_IMPLEMENTED` with a serial line. Wiring an HDA BDL/DMA
  streaming path under the mixer is a larger project (recommended MC6
  scope, §12); games stay fully playable and silent there.
- Apps never know which backend is active (only the backend-name log
  differs).

## 8. WAV inventory / formats (reconfirmed from source)

| File (original) | Exists | Codec | Channels | Rate | Width | Data | Duration |
|---|---|---|---|---|---|---|---|
| `Alarm.WAV` | yes | PCM 8-bit | mono | 11025 | 8 | 7498 B | 0.680 s |
| `Swoosh.wav` | yes | **MS-ADPCM** (tag 2) | stereo | 22050 | 4-bit | 6740 B | fact 6656 |
| `Empty.WAV` | yes | PCM 8-bit | mono | 11025 | 8 | 2862 B | 0.260 s |
| `EXPLODE.WAV` | yes | PCM 8-bit | mono | 11025 | 8 | 23540 B | 2.135 s |
| `Split.WAV` | yes | PCM 8-bit | mono | 11025 | 8 | 1380 B | 0.125 s |
| `Ohno.wav` | yes | PCM 16-bit | mono | 22050 | 16 | 73984 B | 1.678 s |
| `Thunder.wav` | **NO (upstream)** | — | — | — | — | — | silence |
| `Error.wav` | **NO (upstream)** | — | — | — | — | — | silence |
| `OnNo.wav` | commented out | — | — | — | — | — | silence |

Swoosh fmt extra: `samplesPerBlock=1012`, 7 standard coefficient pairs
(256,0 / 512,-256 / 0,0 / 192,64 / 240,0 / 460,-208 / 392,-232), blockAlign
1024, 6 full blocks + a 596-byte partial final block.

## 9. Conversion / decoder result

- `scripts/convert-missilecommand-swoosh.py`: deterministic stdlib-only
  MS-ADPCM decode (sequential per-channel nibbles, partial final block cut
  by the fact count 6656), stereo downmix `(L+R)//2`, emits PCM16 mono
  22050 Hz. Re-run reproduces the staged file byte-for-byte (SHA256
  `761e5997...a92e`), peak 28936 (no clipping), frame0/frame1 match the
  block-0 header samples exactly (-10880, 387).
- `scripts/stage-missilecommand-audio.ps1`: copies the five PCM originals
  byte-identical + runs the conversion into
  `sdk/samples/missilecommand/resources/audio/*.wav` (lowercase staged
  names); `sdk/build-samples.ps1` stages them to `Apps/MissileCommand`.
- Runtime WAV decoding is app-side only
  (`missilecommand_audio.h::mc_wav_decode`: tag-1 mono 8/16-bit, strict
  chunk walk, ADPCM/stereo/rates/truncation rejected). The host never
  parses WAV.

## 10. Missile Command event mapping (verified call sites)

| VB site | Sound | Staged voice | Trigger in port |
|---|---|---|---|
| `Alarm()` per level | Alarm | alarm.wav | L1 startup, restart, every `mc_advance_level` |
| `FireFX()` per `LaunchM` success | Swoosh | swoosh.wav (converted) | `LaunchM` gate passes on a latched tick |
| `LaunchM` else (pool/quota out) | Empty | empty.wav | latched tick, gate fails |
| `MyShow` missile arm | Explode | explode.wav | each fresh `status == 2` defensive burst |
| `MyShow` split arm | Split | split.wav | `mFired` delta minus launches (VB quota quirk) |
| `DoIt` tail `blnLost` | OhNo | ohno.wav | `lost` false->true transition |
| `Intercept` kill | Thunder (missing) | silence | no identity, no request |
| `MyShow` bomb arm (impact) | Thunder (missing) | silence | no identity, no request |
| `DoIt` tail `blnQuit` | Error (missing) | silence | no identity, no request |
| `DoIt` tail `blnWon` | OnNo (commented) | silence | no identity, no request |

Detection lives in `missilecommand_audio.h` (`mc_tick_pre` /
`mc_tick_sounds`: gate replication, fresh-burst scan, split algebra,
level/lost edges) and is host-tested; `main.cpp` only snapshots, advances,
and requests. Sounds added neither fields to `McState` nor changes to any
existing function.

## 11. Overlap, lifecycle, failure behavior

- `SND_ASYNC` heritage: launches, bursts, and splits each queue their own
  voice in the same tick (live log shows swoosh+explode+explode accepted
  back-to-back while gameplay continues).
- 17th concurrent voice -> `BUSY`, absorbed silently by the game.
- Audio startup/permission/backend failure never blocks launch: voices
  load best-effort (`snd=6/6` overlay tag, `audio voices loaded` or
  explicit partial log), every `play_pcm` result is absorbed after a few
  diagnostic logs.
- Exit (Escape or close) runs runtime `Cleanup` -> `BackendStopOwner`;
  relaunch starts clean (proven by the AudioBeep double-launch + denied
  twin run).

## 12. Tests

- `scripts/run-native-abi-layout-test.ps1`: **PASS** (repaired, §3).
- `scripts/run-app-audio-mixer-test.ps1` (new): **PASS** — validation
  matrix, 8/16-bit conversion values, upsample shape, overlong refusal,
  single/overlap/clipping vectors, mixer determinism, exhaustion + owner
  reclaim, policy allow/deny/malformed, backend accept/busy/reclaim
  counters.
- `scripts/run-missilecommand-state-test.ps1`: **PASS** — full MC1-MC4
  suite untouched (golden full-campaign fingerprint
  `16492225105589479459`, 4059 steps, identical across runs) plus new MC5
  block: decoder unit vectors + rejections, all six staged assets decode
  to exact specs, missing-source absence (no thunder/error/onno
  identity), launch-gate unit, fresh-burst scan, split algebra,
  level/lost edges, and the determinism boundary (same seed + inputs:
  identical fingerprint with the sound sink on vs off;
  `launches=3 bursts=3`).
- `scripts/run-compositor-pointer-buttons-test.ps1`: unmodified (input
  contract untouched).
- Live: `scripts/smoke-missilecommand-mc5.ps1` **16/16 PASS** (registration,
  `audio.output` recognized, launch, DD.ini, GXIM art, 6/6 voices, window,
  spawn, click, **9 play_pcm accepts through `waveout-22050-mono-s16`**,
  overlap, detonation, right-drag, continued gameplay, Escape, clean exit);
  `scripts/smoke-audiobeep.ps1` **9/9 PASS** (independent synthesized
  overlap + live permission denial, twin removed afterwards).

## 13. Known limitations

- Bare metal is explicitly silent (`NOT_IMPLEMENTED`); an HDA BDL/DMA
  streaming backend under the same mixer is future work (no app changes
  needed when it lands).
- The bare-metal kernel build cannot be compile-verified in this checkout:
  the i686-elf toolchain has no freestanding libc `string.h`, so even
  untouched files including `bitmap_font.h` fail here (pre-existing,
  proven with `system_font.cpp`). The MC5 bare-metal additions were
  syntax+logic verified via an extracted host harness (exact permission
  matching incl. prefix rejection).
- Audibility itself is evidenced by waveOut device presence (1 device),
  successful `waveOutOpen`, and error-free `waveOutWrite` submissions with
  mixer-accepted ids — no human ear was in the loop.
- The long MC4 live-campaign vehicle (`smoke-missilecommand-mc4.ps1`) was
  not re-run: its MC4 display-name strings are superseded by the MC5
  binaries, and the phase brief keeps the campaign regression host-side
  (golden fingerprint above). Prior MC4 live evidence stands.

## 14. Recommended MC6 scope

Bare-metal HDA streaming backend behind the same mixer (no ABI change);
optional menu polish / high-score persistence proposal. No music
streaming, codecs, MIDI, capture, spatial audio, or media framework
(MC5 scope exclusions held).
