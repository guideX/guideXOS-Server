# S14 test and validation index

Result: Outcome B. The code, focused tests, regression suites, clean production build, and native Display guest page passed. The S14 guest follow-on sequence for System/About, close/reopen, and legacy Control Panel/Display Options remains unverified.

## Focused suites

| Suite | Result |
| --- | ---: |
| Native Settings provider/runtime | 828/828 passed |
| S10 presentation core | 2,647/2,647 passed |
| Hosted Settings/model total | 359/359 passed |

Native runtime coverage included 100 create/close/reopen repetitions, 250 deterministic provider refresh cycles, and 1,000 notification/state transitions. The presentation suite included Display generation wrap ordering and preserved navigation/focus behavior.

Hosted Settings/model breakdown:

| Area | Result |
| --- | ---: |
| Settings Center | 89/89 |
| Inventory and Storage | 45/45 |
| Default Apps | 48/48 |
| S6 | 42/42 |
| S7 | 58/58 |
| Users | 32/32 |
| Network | 45/45 |

## Adjacent regressions

| Suite | Result |
| --- | ---: |
| App Model identity | 25/25 |
| App Model application actions | 55/55 |
| App Model URI protocol | 41/41; persistence, activation, and no-handler cycles each 100/100 |
| App Model folder activation | 26/26 |
| App Model file activation | 60/60 |
| Compositor pointer-button regression | Passed |
| Compositor resize notification direct compile/run | Passed |
| Display/framebuffer and display-configuration source/runtime smoke checks | Passed |
| Freestanding display-refresh helper compile/link and unresolved-reference checks | Passed |

The raw hosted test output remains in the ignored S14 validation tree under out/validation/settings-s14-20261015. No test executable or build output is included in this committed evidence directory.

## Clean production build

The normal AMD64 build completed with 155 compile commands, one link, zero errors, and 233 warning lines. The S13 historical baseline was 154 compile commands, zero errors, and 409 warning lines. No warning originated in the new S14 display-refresh implementation files. The ELF undefined-name inventory has 20 unique names; the added display callback is defined in the final image. Two names remain absent from the defined-symbol inventory, matching the same two names at S13.

Kernel ELF SHA-256: 465F7C86957B4E41E1FC8A3392177875167F9850C048348F9BB03BEADB842102

Kernel PE SHA-256: A429049F16ECAA5E70739C0F5416954E11BAAA7D599D4078EBC33CB92CECCFA5

## Production guest

The isolated ordinary QEMU guest booted the matching kernel and reached the desktop. Start → Settings → Display showed 1280 x 800, 32 BPP, B8G8R8A8, and 5120 bytes per line. Logical desktop, display layout, and primary output mode remained Unavailable, as expected while the ordinary production topology backend is gated off.

The QEMU launch manifest, QMP transcript, serial output, framebuffer screenshot, initial desktop, and Settings launch screenshot are committed beside this file. The guest was shut down through its own QMP session. A preexisting foreign QEMU process observed during the accepted run was not touched.

The full S14 guest navigation and legacy Control Panel sequence was not captured. S13 historical QEMU evidence and the passing S14 App Model, lifecycle, navigation, and compositor tests provide regression context but do not replace that missing S14 guest observation.
