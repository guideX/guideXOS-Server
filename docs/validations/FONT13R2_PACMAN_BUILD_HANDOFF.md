# FONT13R2 — PacMan Build Handoff

**Owner:** PacMan Native ELF package/build workstream
**Disposition:** PMBUILD1 PacMan package repair accepted; integrated full-build result remains blocked later by the DeveloperStudio signed-off ELF hash gate.

After the owner integration workflow makes PMBUILD1 available to the FONT_SUPPORT integration baseline, rerun the ordinary top-level AMD64 build from that checkout:

```powershell
.\build.ps1 -Arch amd64
```

Use the normal package/build paths with no `GUIDEXOS_PACMAN_ARTIFACT_ROOT` override. Confirm the pinned PacMan revision, `pacman_audio.o`, all seven `resources/audio/*.wav` files, and the PacMan manifest/ELF are staged, then record whether the build reaches runtime-image and ESP staging.

The PMBUILD1 run in `D:\dev\guideXOSServer` passed PacMan but then stopped because `Apps/DeveloperStudio/bin/amd64/developerstudio.elf` was 1,104,652 bytes / SHA-256 `F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6`; the top-level gate expects 1,015,064 bytes / `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7`. If that gate persists, report it as a separate integration blocker. Do not treat a passing PacMan stage as completion of FONT13R2's clean-install font or live Settings acceptance.

Keep the FONT_SUPPORT checkout unchanged until the owner workflow supplies the repair. This handoff records requalification steps; it does not advance FONT13R2.
