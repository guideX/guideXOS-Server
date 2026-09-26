# NativeAOT C141 — Managed Vertical Stack Layout

## Scope

C141 adds `GuideXosVerticalStack`, a fixed-capacity, non-owning vertical
layout primitive for the ordinary managed leaf controls already supported by
C140. The application still creates and owns the controls, registers
interactive controls separately, and explicitly calls `PerformLayout()` when
layout-affecting state changes.

The stack only computes geometry. It does not render, register, focus,
capture input, create controls, destroy controls, or maintain a viewport. It
does not replace `GuideXosPanel` or `GuideXosScrollView`, and it does not
create a recursive ownership/layout tree.

## Membership and geometry authority

The stack has a fixed capacity of 8 references. It preserves insertion order,
rejects duplicate references, rejects overflow, and rejects controls already
owned by a `GuideXosPanel` or another `GuideXosVerticalStack`. Removing a
member detaches only the layout reference; it does not unregister or destroy
the control. `Clear()` behaves the same way for every member.

Supported members are:

- `GuideXosButton`
- `GuideXosCheckBox`
- `GuideXosLabel`
- `GuideXosSeparator`
- `GuideXosRadioButton`
- `GuideXosProgressBar`
- `GuideXosComboBox`

`TextArea`, `ListBox`, `Panel`, `ScrollView`, and another vertical stack are
rejected. Panel membership remains mutually exclusive with stack membership.
Stack plus ScrollView membership is the intentional supported composition:
the controls remain direct ScrollView members while the stack references the
same controls for placement.

The controls' existing authoritative bounds remain the only geometry store.
The small internal adapter contract exposes those bounds and delegates the
stack write through each control's existing bounded `TrySetBounds` core. No
second rectangle is cached by the layout.

## Layout semantics

`PerformLayout()` walks visible members in insertion order. Hidden members
consume neither a slot nor spacing; disabled members retain their slot.
Each visible control keeps its existing height. The vertical placement is:

```text
currentY = Stack.Y + TopPadding
for each visible member:
    member.X = Stack.X + LeftPadding
    member.Y = currentY
    member.Width = width selected by WidthPolicy
    currentY += member.Height
    if another visible member follows:
        currentY += Spacing
ContentHeight = currentY + BottomPadding - Stack.Y
```

Padding and spacing are bounded non-negative integers. The frame, member
rectangles, and content bottom are checked against the existing 12-bit
coordinate conventions before bounds are written. A failed layout leaves the
member set and prior layout properties intact.

`Stretch` uses the available inner width, with character-aligned controls
rounded down to the project’s 8-pixel text-cell boundary. `KeepWidth` retains
each member’s existing width. There is no measurement, percentage, star,
wrapping, margin, horizontal, or recursive layout policy.

## ScrollView, viewport, and input

`PerformLayout(scrollView)` is an explicit composition call. It first writes
the controls' logical content bounds, then asks the existing ScrollView to
refresh its derived member extent. ScrollView remains the owner of clipping,
logical-to-screen translation, wheel routing, pointer hit testing, focus
reveal, offset clamping, and its existing bound ScrollBar. The stack stores no
offset and processes no input.

When visibility changes, application code calls `PerformLayout(scrollView)`.
The ScrollView recomputes its existing extent from the arranged direct
members; the shared viewport clamps an out-of-range offset and synchronizes a
bound ScrollBar. Focus identity remains a ScrollView/control concern, so a
focused control remains the same object when another member moves.

ComboBox popup geometry is read from the ComboBox's current arranged bounds.
Relayout therefore moves the popup origin without adding capture or popup
ownership to the stack. Existing transient popup capture and PopupMenu/context
behavior remain outside the layout boundary.

## NativeAOT and ABI

The implementation uses fixed arrays, explicit supported-control adapters,
bounded loops, and no reflection, LINQ enumeration, dynamic discovery, or
per-input layout allocation. ABI v1 and native table size 104 are unchanged.
The NativeAOT proof enables the existing C121–C138 transport/control flags
plus the C141 application flag; it does not alter the runtime, GC, VFS,
capability, viewport, ScrollBar, popup-capture, or focus architecture.

## Proof surface and tests

`ManagedVerticalStackDemo` arranges exactly eight direct ScrollView members:
Label, CheckBox, ComboBox, Separator, two RadioButtons, ProgressBar, and
Button. The CheckBox hides the ProgressBar through a real callback and
explicit relayout. The proof also exercises wheel scrolling, translated
pointer activation after movement, ComboBox popup open/follow-up selection,
ScrollBar drag, Tab focus reveal, and deterministic relaunch state.

The focused C141 suite is divided into 20 core layout cases, 10 visibility
cases, 10 ScrollView integration cases, 10 focus cases, and 6 popup cases:
56 cases total. Earlier C137–C140 behavior and the historical C127–C136
suites remain part of the composite proof and are not loosened for C141.

The runner records the actual NativeAOT ELF, proof kernel, serial, manifest,
and ordinary-kernel hashes under the C141 evidence directory. The final
verification record is filled after the three fresh QEMU launches and the
ordinary-kernel restoration pass; no generated evidence or protected
`ESP/ramdisk.img` is a source input.

## Verification record

The clean C141 proof completed three fresh physical-QEMU boots with
`C141 outcome=PASS`. The focused serial line was:

```text
C141-TESTS core=20 visibility=10 scrollview=10 focus=10 popup=6 total=56 result=PASS
```

The production surface also reported `C141-PROOF`, `C141-LAYOUT`, popup
open/follow-up, visibility relayout, wheel translation, scrollbar drag,
moved-button pointer translation, focus reveal, and final capture/drag clear
as `PASS`.

Evidence:

- C141 manifest: `out/dotnet/c141-managed-vertical-stack/c141.manifest.json`
- C141 boots: `boot-01`, `boot-02`, `boot-03`, all `PASS`
- NativeAOT ELF SHA-256: `5199F4A96C11C8893E1E0F96E233C44745D3EA76068BB692BC42984A5B3518EE`
- C141 proof-kernel SHA-256: `F2A3EA042D8A4ADD2B01CDAC4D7127822FAB79EF5E769072068E29F2F8937ACB`
- C141 serial SHA-256: `5F92EB6A49E5962E40C7C5516761A68A44EE80E6D0B11094A488C2711B2878AF`, `B1E6EB99BD2112B4378FC44B183C6E97C481544C944065FBE09A4FB60ACBC5AA`, `EA99E2DF1DCDB4278386FC356F6C7106F8621DD3175F21CC5D36F48084EBB294`

After the proof, the ordinary kernel was rebuilt without C141 proof flags,
copied to both ordinary kernel locations, and validated by three additional
fresh QEMU boots. The C51 validator reported `outcome=PASS`, with the main
loop and Navigator smoke markers present and no page fault, fail-fast, or
semantic-proof markers. The restored ordinary kernel SHA-256 is
`9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203` at
both `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf`. The protected
`ESP/ramdisk.img` remained unchanged at
`E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE`.

Ordinary-boot evidence: `out/dotnet/c51-ordinary-boot-validator/run-20260926-084000630-2c172275/ordinary-boot.manifest.json`.

## Deferred

Horizontal stacks, wrapping, Grid/Dock/Flex behavior, proportional sizing,
automatic measurement, recursive/nested stacks, ownership trees,
virtualization, responsive breakpoints, horizontal scrolling, and a second
focus or capture system are intentionally deferred.
