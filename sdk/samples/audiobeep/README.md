# AudioBeep sample (MC5)

Minimal proof of the App Model audio API, independent of Missile Command:

- manifest requests `audio.output` (and only `log` besides it)
- synthesizes 0.30 s of 440 Hz S16 mono 22050 Hz PCM in code (no resources)
- calls the appended `play_pcm` host call twice back-to-back (overlap)
- exits cleanly; stays silent but functional when the host predates the
  audio slot, denies the permission, or has no audible backend

Build and stage with `sdk/build-samples.ps1` (staged to `Apps/AudioBeep`).
See `docs/MISSILE_COMMAND_MC5_APP_MODEL_AUDIO.md` for the full audio
architecture, PCM contract, and permission semantics.
