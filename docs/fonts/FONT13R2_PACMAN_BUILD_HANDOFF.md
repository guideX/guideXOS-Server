# FONT13R2 — PacMan Native ELF Link Handoff

- **Owner:** PacMan Native ELF package/build workstream
- **Disposition:** external, pre-existing build prerequisite; unrelated to FONT13 font packaging
- **FONT_SUPPORT baseline:** `FONT_SUPPORT` at `89a58151c19da3d6532d838ae3a29a2c0ab4711a`
- **PacMan source snapshot inspected read-only:** `D:\dev\pacman\guidexos`, `master` at `8fc2aed`

## Failure

The normal `build.ps1 -Arch amd64` flow stops in its PacMan Native ELF package step before calling the runtime-image packer. LLD reports these exact unresolved C++ symbols from `main.o`:

```text
pacman_audio_load_resources(gx_app_context*)
pacman_audio_submit(void*, PacManSoundId)
```

The package builder's link command is:

```powershell
& 'C:\Program Files\LLVM\bin\ld.lld.exe' -m elf_x86_64 -static -e gx_main `
  build\pacman-native\amd64\objects\main.o `
  build\pacman-native\amd64\objects\game.o `
  build\pacman-native\amd64\objects\level.o `
  build\pacman-native\amd64\objects\level_rules.o `
  build\pacman-native\amd64\objects\renderer.o `
  build\pacman-native\amd64\objects\bitmap_loader.o `
  -o build\pacman-native\amd64\pacman.elf
```

The exact invocation and failure are in [the existing full-build log](../../out/font13-validation/full-amd64-build.log). A fresh diagnostic compile of the current six registered sources, followed by the same static AMD64 link, reproduced both unresolved symbols. It wrote objects and the linker log only under [the FONT13R2 diagnostic output](../../out/font13-validation/font13r2-pacman-link-repro/current-source/link.log). No PacMan or guideXOS production source was edited.

## Root cause

`scripts/build-pacman-package.ps1` registers six translation units: `main.cpp`, `game.cpp`, `level.cpp`, `level_rules.cpp`, `renderer.cpp`, and `bitmap_loader.cpp`. It does not register `pacman_audio.cpp`.

The configured PacMan source has calls to both functions in `src/main.cpp`. `src/pacman_audio_runtime.h` declares the same signatures that `src/pacman_audio.cpp` defines. The expected provider object is:

```text
build/pacman-native/amd64/objects/pacman_audio.o
    compiled from D:\dev\pacman\guidexos\src\pacman_audio.cpp
```

This is a **missing source-registration/provider-object defect** in the PacMan Native ELF package build. The signatures match; the definitions exist; no separate library is needed for these two symbols; the main compile flags do not disable the calls; and the top-level order is not the cause. The PacMan package step runs before runtime-image generation, so FONT13's later font-staging changes cannot cause this failure. Do not stub the symbols or remove the audio calls to bypass the link.

The current app packaging script also stages only the generated level and sprite files. `pacman_audio.cpp` requests files under `resources/audio/`; validate those resources as a separate PacMan runtime-package concern after the link prerequisite is repaired. That does not change the link diagnosis.

## Baseline and stale-object caution

The pre-FONT13 [App Model Phase 13 build record](../APPMODEL_PHASE13_IMAGEVIEWER_JPEG_MIGRATION.md), committed on 2026-10-02, already records the same two PacMan link failures while attempting an unrelated hosted ImageViewer smoke. The tracked package-builder source list at the required FONT_SUPPORT HEAD still lacks `pacman_audio.cpp`. The link failure therefore predates the font-package changes.

Do not use the old `build/pacman-native/amd64/objects` directory as a reproduction without checking its source identity. Its `main.o` was stale during this closeout: it contained no audio references, and linking those old objects emitted the same hash as the existing PacMan ELF package. The fresh-source reproduction above is the valid current-source result; that stale-object link is not a top-level build pass.

## Required owner acceptance

1. Register and compile `src/pacman_audio.cpp` using the package's freestanding AMD64 flags, producing `pacman_audio.o` in the same fresh object directory.
2. Start from a clean PacMan object directory and verify the main object's audio references resolve to definitions in the provider object (for example, with `llvm-nm -C`).
3. Link the genuine static AMD64 ELF with the complete provider object list; retain the normal audio calls.
4. Validate the separate `resources/audio/` staging needed by the implementation if audio playback is part of the package acceptance.
5. Run `build.ps1 -Arch amd64` on the unchanged FONT_SUPPORT source state and confirm it proceeds past the PacMan package step into runtime-image generation. Record the resulting image and build logs.

Until these steps pass, classify the **top-level image-build qualification** as blocked by the PacMan workstream. The **independent font runtime packer and FAT image path** passed their separate FONT13R2 checks; that does not convert the full top-level build into a pass.

See [FONT13R2_PACKAGING_CLOSEOUT.md](FONT13R2_PACKAGING_CLOSEOUT.md) for the split release decision and the remaining input/Settings acceptance.
