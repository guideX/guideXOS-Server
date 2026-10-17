# Folder-backed FAT comparison

FONT8's earlier folder-backed attempt wrote and verified `/fontsel.tmp`, then issued a rename to the already-existing `/fontsel0.cfg`. The guest serial log records `LFPASTE_RENAME_BEGIN old=/fontsel.tmp new=/fontsel0.cfg`; QEMU's host stderr records `Error handling renames (-2)`. That is the observed backend failure. It does not establish a guest VFS return code for the rename.

FONT9 removes rename from the preference commit path. The isolated raw FAT32 block-image run performed direct `write_file` overwrites on alternating existing slots and recorded successful `VFS_OK` results. A separate `fat:rw:` folder-backed probe mounted FAT32, initialized the service, and opened both slot files, but QEMU input did not reach the desktop reliably enough to invoke Apply. The probe therefore says nothing about VVFAT direct-overwrite support. No VFS/FAT change or emulator-only production behavior was introduced.

Evidence: complete folder serial/launch/stderr are adjacent. The captured FONT8 host stderr and serial excerpt are also adjacent; the full original log remains in the pre-existing untracked output tree. Background: `docs/fonts/FONT8_NATIVE_DESKTOP_ADOPTION.md`.
