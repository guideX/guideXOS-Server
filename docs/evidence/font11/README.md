# FONT11 evidence index

This folder contains the deterministic cache comparison and representative screenshots from the isolated desktop guest.

## Cache workload

[`cache-workload-results.md`](cache-workload-results.md) has per-surface cold/warm hits, misses, rasterizations and evictions; clock/date measurements; resident bytes; and repeated glyph keys. Raw test output and build artifacts are preserved under `out/font11-validation/`.

## Representative guest screenshots

| Screenshot | Evidence |
|---|---|
| [`09-qemu-cold-menu.png`](09-qemu-cold-menu.png) | Cold Start Menu redraw; 48 misses/rasterizations, no evictions in the serial snapshot. |
| [`10-qemu-warm-menu-redraw.png`](10-qemu-warm-menu-redraw.png) | Unchanged redraw; no added misses or rasterizations. |
| [`03-multiple-windows-menu-clock.png`](03-multiple-windows-menu-clock.png) | Disk Manager and PacMan windows with taskbar clock/date. |
| [`11-qemu-restored-italic-settings.png`](11-qemu-restored-italic-settings.png) | Settings Fonts page after boot restored the persisted Roboto Italic 17 px preference. |
| [`16-qemu-settings-cancel-2.png`](16-qemu-settings-cancel-2.png) | Pending changes discarded; active Roboto Italic 17 px remained selected. |
| [`17-qemu-preview-regular-before-apply.png`](17-qemu-preview-regular-before-apply.png) | Regular 16 px is only a preview; current active font is still Italic 17 px. |
| [`18-qemu-apply-stalled-pending.png`](18-qemu-apply-stalled-pending.png) | Pending preview remained on screen after Apply stalled before storage commit; it does not show an applied Regular font. |

The remaining numbered images are exploratory captures from interaction calibration and intermediate states. Use the screenshots indexed above for claims in the historical FONT11 report.

## FONT11R guest Apply and restart qualification

The following captures are from the continuation's supported raw IDE/FAT32 configuration. The serial traces, process ownership manifests, preference-state inspection, fresh disk image and final build/test artifacts are preserved under `out/font11r-validation/continuation/`.

| Screenshot | Evidence |
|---|---|
| [`font11r-cancel-confirmed.png`](font11r-cancel-confirmed.png) | Cancel discarded the preview and preserved the prior active selection. |
| [`font11r-settings-and-file-explorer.png`](font11r-settings-and-file-explorer.png) | Settings and File Explorer open at the same time. |
| [`font11r-multiple-windows-clock.png`](font11r-multiple-windows-clock.png) | Start Menu and Settings with taskbar clock/date. |
| [`font11r-applied-italic-17.png`](font11r-applied-italic-17.png) | Actual Settings Apply completed for Roboto Italic 17 px. |
| [`font11r-bitmap-applied.png`](font11r-bitmap-applied.png) | Built-in Bitmap fallback applied and remained usable. |
| [`font11r-final-roboto-bold-16.png`](font11r-final-roboto-bold-16.png) | TrueType reapplied before clean shutdown. |
| [`font11r-fresh-process-restored-bold-16.png`](font11r-fresh-process-restored-bold-16.png) | Fresh QEMU process restored Roboto Bold 16 px from the same private disk. |
