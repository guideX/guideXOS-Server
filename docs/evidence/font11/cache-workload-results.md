# FONT11 deterministic cache workload evidence

The harness reads the same Roboto-Italic TrueType source, uses FONT10 screenshot 01 strings and the effective 13 px title, 12 px taskbar/menu, and 17 px profile sizes. It runs the same 17 surfaces through the same layout and framebuffer draw path before and after the cache change. `H/M/R/E` means hits, misses, rasterization attempts, and evictions for that one surface call.

## Per-surface comparison

| Surface / label | px | codepoints | Before cold H/M/R/E | Before warm H/M/R/E | After cold H/M/R/E | After warm H/M/R/E |
|---|---:|---:|---:|---:|---:|---:|
| start-menu-footer / `All Programs` | 12 | 12 | 8/4/4/4 | 8/4/4/4 | 11/1/1/0 | 12/0/0/0 |
| start-menu-footer / `Restart` | 12 | 7 | 6/1/1/1 | 6/1/1/1 | 6/1/1/0 | 7/0/0/0 |
| start-menu-footer / `Shut Down` | 12 | 9 | 4/5/5/5 | 4/5/5/5 | 8/1/1/0 | 9/0/0/0 |
| start-menu-footer / `Sleep` | 12 | 5 | 3/2/2/2 | 3/2/2/2 | 5/0/0/0 | 5/0/0/0 |
| start-menu-header / `User` | 17 | 4 | 0/4/4/4 | 0/4/4/4 | 0/4/4/0 | 4/0/0/0 |
| start-menu-left / `Console` | 12 | 7 | 2/5/5/5 | 2/5/5/5 | 5/2/2/0 | 7/0/0/0 |
| start-menu-left / `guideXOS Navigator` | 12 | 18 | 7/11/11/11 | 7/11/11/11 | 18/0/0/0 | 18/0/0/0 |
| start-menu-right / `Computer` | 12 | 8 | 5/3/3/3 | 5/3/3/3 | 7/1/1/0 | 8/0/0/0 |
| start-menu-right / `Control Panel` | 12 | 13 | 8/5/5/5 | 8/5/5/5 | 13/0/0/0 | 13/0/0/0 |
| start-menu-right / `Documents` | 12 | 9 | 7/2/2/2 | 7/2/2/2 | 7/2/2/0 | 9/0/0/0 |
| start-menu-right / `Music` | 12 | 5 | 4/1/1/1 | 4/1/1/1 | 4/1/1/0 | 5/0/0/0 |
| start-menu-right / `Network` | 12 | 7 | 4/3/3/3 | 4/3/3/3 | 5/2/2/0 | 7/0/0/0 |
| start-menu-right / `Pictures` | 12 | 8 | 6/2/2/2 | 6/2/2/2 | 7/1/1/0 | 8/0/0/0 |
| taskbar-label / `Terminal` | 12 | 8 | 4/4/4/4 | 4/4/4/4 | 4/4/4/0 | 8/0/0/0 |
| taskbar-label / `guideXOS Navigator` | 12 | 18 | 3/15/15/15 | 3/15/15/15 | 3/15/15/0 | 18/0/0/0 |
| window-title / `Terminal - guideXOS Shell` | 13 | 25 | 14/11/11/10 | 14/11/11/11 | 19/6/6/0 | 25/0/0/0 |
| window-title / `guideXOS Navigator` | 13 | 18 | 3/15/15/0 | 3/15/15/15 | 3/15/15/0 | 18/0/0/0 |

## Added clock/date surfaces

| Surface | Cold text | Warm text | Changed text | px | cold H/M/R/E | warm H/M/R/E | changed-text H/M/R/E |
|---|---|---|---:|---:|---:|---:|---:|
| taskbar-clock-date | `10/17/2026` | `10/17/2026` | `10/18/2026` | 12 | 6/4/4/0 | 10/0/0/0 | 10/0/0/0 |
| taskbar-clock-time | `19:08` | `19:08` | `19:09` | 12 | 0/5/5/0 | 5/0/0/0 | 5/0/0/0 |

## Frame totals and repeated keys

```text
cold frame draws=17 fitted_codepoints=181 hits=88 misses=93 rasters=93 evictions=77 entries=16 bytes=981 peak=1064 max_entries=16 coverage_capacity=65536 cache_bytes=66896
warm frame draws=17 fitted_codepoints=181 hits=176 misses=186 rasters=186 evictions=170 entries=16 bytes=981 peak=1238 max_entries=16 coverage_capacity=65536 cache_bytes=66896
cold frame draws=17 fitted_codepoints=181 unique_keys=56 repeated_key_occurrences=125 hits=125 misses=56 rasters=56 evictions=0 entries=56 bytes=3512 peak=3512 entry_evictions=0 coverage_evictions=0 compactions=0 bypasses=0 oversized_bypasses=0 admission_bypasses=0 raster_failures=0 max_entries=72 coverage_capacity=65536 cache_bytes=69128 warm_reuse=yes
warm frame draws=17 fitted_codepoints=181 unique_keys=56 repeated_key_occurrences=125 hits=312 misses=65 rasters=65 evictions=0 entries=65 bytes=4038 peak=4038 entry_evictions=0 coverage_evictions=0 compactions=0 bypasses=0 oversized_bypasses=0 admission_bypasses=0 raster_failures=0 max_entries=72 coverage_capacity=65536 cache_bytes=69128 warm_reuse=yes
```

Repeated glyph keys in the cold font surfaces:

| glyph ID | pixel size | occurrences |
|---:|---:|---:|
| 75 | 13 | 3 |
| 89 | 13 | 2 |
| 77 | 13 | 4 |
| 72 | 13 | 2 |
| 73 | 13 | 4 |
| 60 | 13 | 2 |
| 51 | 13 | 2 |
| 55 | 13 | 3 |
| 4 | 13 | 4 |
| 69 | 13 | 3 |
| 86 | 13 | 2 |
| 80 | 13 | 3 |
| 75 | 12 | 5 |
| 89 | 12 | 7 |
| 77 | 12 | 7 |
| 72 | 12 | 2 |
| 73 | 12 | 12 |
| 60 | 12 | 2 |
| 51 | 12 | 2 |
| 55 | 12 | 4 |
| 4 | 12 | 5 |
| 50 | 12 | 3 |
| 69 | 12 | 8 |
| 90 | 12 | 2 |
| 88 | 12 | 10 |
| 83 | 12 | 11 |
| 86 | 12 | 10 |
| 81 | 12 | 4 |
| 82 | 12 | 6 |
| 80 | 12 | 7 |
| 39 | 12 | 3 |
| 87 | 12 | 6 |
| 84 | 12 | 2 |
| 40 | 12 | 2 |
| 71 | 12 | 3 |
| 52 | 12 | 3 |
| 91 | 12 | 2 |

Clock behavior is separately measured after the 17-surface frame. The cold time/date rows add 9 new misses and 9 rasterizations, ending at 65 entries and 4,038 coverage bytes. The unchanged rows and the changed `19:09` / `10/18/2026` rows each add hits only. The final cache snapshot is 342 hits, 65 misses/rasterizations, 0 evictions, 65 entries, and 4,038 bytes resident / peak. The after cache reserves the existing 65,536-byte coverage capacity and has 69,128 bytes total static cache memory.
