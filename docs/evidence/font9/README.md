# FONT9 evidence

The raw files in `block-storage/` contain the four complete serial logs, exact per-process QEMU launch manifests, offline FAT snapshots, the injected scratch-record manifest, and Settings captures from the isolated writable FAT32 block-image run. The guest processes used a private 512 MiB image, private OVMF copy, 1 GiB RAM, disabled networking, and serially reused the image only after guest shutdown and process exit.

`folder-backend/` contains the complete serial log and launch details for the folder-backed FAT read/boot probe. It reached Settings and opened both existing slot files; UI clicks did not reach the desktop, so it did not perform a preference write and was closed through its own QMP connection. It is not write qualification.

`kernel-build-forced.log` is the complete output from `mingw32-make ARCH=amd64 -B`. The original private disk image, full prelaunch censuses, VNC PPM captures, and working scripts remain untracked under `out/font9-validation/` and were preserved.
