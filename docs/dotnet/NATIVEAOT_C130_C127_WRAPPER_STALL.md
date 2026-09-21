# NativeAOT C130: C127 mixed-wrapper stall

## Outcome and decision

C130 is **Outcome B — obsolete reverse helper retired/replaced**.

C127 originally combined two different proofs in one native wrapper:

1. a meaningful managed Panel fixture, and
2. synthetic native choreography around Managed Notes, including a private
   direct-managed reverse-traversal call.

The second portion was not a production input proof. C130 retires the entire
legacy mixed-native choreography from the retained C127 entry point. The
retained wrapper launches the managed C127 Panel fixture, records its 60-case
PASS, emits an explicit replacement marker for the obsolete path, and returns
with a bounded C127 PASS. C129 remains the authoritative physical Shift+Tab
proof.

## Exact stall reproduction

The preserved C127 diagnostic ESP was:

```text
out\dotnet\c011ec127-managed-panel\boot-01\ESP
```

It was copied to a C130-only evidence directory and booted with the ordinary
QEMU/OVMF configuration used by the managed-control runner. The serial reader
had a hard 45-second deadline and killed only that QEMU process at the
deadline. No source, credential, remote, or branch state was changed by the
reproduction. The captured serial output was:

```text
out\dotnet\c130-wrapper-stall-repro\serial.log
```

The last successful sequence was:

```text
[C127-INITIAL] panel=visible children=2 aligned=radios progress=independent result=PASS
[C102-MANAGED-OUTPUT] C121-TAB traversal=PASS       # seven forward traversals
[C116-NATIVE-INPUT] kind=key-down key=00000009 shift=00000000 result=PASS
[C102-MANAGED-OUTPUT] C121-SHIFT-TAB traversal=PASS
```

The serial file ended at 1,390 lines without `C117-TRANSPORT`,
`C127-FOCUS-ORDER`, `C127-MIXED`, or `C127-RESULT`. The operation being
awaited was the return from:

```text
invokeManagedKeyDownForProof(c127Notes->managedSelector, 9, true)
```

The managed Notes selector was the resident Managed Notes record. Managed
code was still executing: `GuideXosControlHost.Traverse(true)` logged the
reverse traversal marker. The native helper did not return to emit its
transport result. This was not a native polling loop with a missing generation
acknowledgement; it was a synchronous nested NativeAOT re-entry with no
bounded acknowledgement boundary. The helper set the C116 dispatch guard,
constructed fresh host metadata, disabled interrupts, and entered the
resident image while the mixed-wrapper function was still on the native call
stack.

The same path was nondeterministic in the older evidence: some boots returned
after the managed marker and some stopped at that boundary. That behavior is
not a valid regression contract. After the reverse call alone was removed,
the remaining synthetic native steps showed the same unbounded mixed-wrapper
character. C130 therefore retired the obsolete sequence rather than preserving
artificial production behavior around it.

## Semantic comparison with C129

The stall was not caused by Panel navigation and was not a C129 production
keyboard defect.

The old helper:

- directly invoked managed input from a native proof stack;
- did not inject physical Shift make, Tab make/break, and Shift break events;
- bypassed the PS/2 input queue and production modifier lifecycle;
- had no current-generation/result acknowledgement contract; and
- did not wait for a Tab character, although the managed host correctly treats
  Tab as key-down-only navigation.

C129 uses explicit QMP key-down/key-up events through the production PS/2,
compositor, Host ABI, and managed dispatch path. It tracks left and right
Shift independently, performs reverse traversal exactly once, releases Shift,
performs plain Tab, and verifies ordinary printable input. C130 did not change
that production path and introduced no synthetic Tab `KeyChar`.

## Bounded C130 behavior and coverage

The new wrapper is:

```text
scripts\dotnet\run-c130-c127-wrapper.ps1
```

The shared validator now has explicit per-boot timeout classification and a
portable SHA-256 fallback for child Windows PowerShell sessions that do not
autoload `Get-FileHash`. A missing result is `TIMEOUT` or `FAIL`, never an
indefinite wait.

The focused native C130 marker is:

```text
[C130-REGRESSION] legacy-helper=direct-managed-reverse-traversal status=SKIP replacement=C129-production-shift-tab result=PASS
```

The ordinary C127 entry point also emits:

```text
[C127-MIXED] sequence=obsolete-direct-managed-wrapper retired=replaced-by-managed-C127-and-C129 result=SKIP
[C127-RESULT] outcome=PASS panel=managed-fixture,non-owning,single-level lifecycle=bounded-retired-legacy-mixed-path
```

This proves that the retained C127 wrapper completes and that the obsolete
path is not invoked. Coverage is intentionally separated:

1. production input proof: C129 physical QMP Shift/Tab transport;
2. managed control proof: C127's 60 Panel cases and C128's 46 lifecycle cases;
3. legacy wrapper mechanics: C130's explicit SKIP plus bounded C127 PASS.

## Validation record

Fresh C130 managed/NativeAOT build and three fresh diagnostic QEMU boots:

```text
build manifest: out\dotnet\c130-build\c130.manifest.json
composite: 0E560AF7830C094BFF9A416002C8921E7923766FCB7E113551549036B4D75E7D
build manifest sha: 5142D94294FA0029880482FD5FD1C17AD4820D5C41E6B0F6B73953D01152BCD1
```

Final C130 diagnostic kernel/QEMU run after the ordinary C127 entry point was
also made bounded:

```text
manifest: out\dotnet\c130-final\c130.manifest.json
kernel:   742E9075DCC6FD091DBFFD94017857241CE9EA88420B8DD9EC5EF053251A91F8
ramdisk:  EFA9D867F8DC0F1A76E6A55F98FF17324089F6EA73D9ECF0400FB5002734565D
serial-01 96538E270F5BAD0FE54F2BACFC743D59520DF301B55FDC0451B81293339265EF
serial-02 BA0A2A0156237E831190C4B46405ED629F14D274383F77DE7F1DB3FD0CE3E91E
serial-03 3317170E23772768FB715DE792C2E902AD6A11E10F4272D15FE6376FE9D22213
manifest  5A31E363274D31F346D973465B46CBAD2F5049263E9773ECD380C64D5C2D8A68
```

The fresh C127 managed Panel matrix also passed three boots:

```text
manifest:  out\dotnet\c130-c127-rerun\c127.manifest.json
composite: 289A9DB2EA945C605D250409446667AE3CEEC736338FC3DD4D78193667FAFA31
kernel:    2C93519ED00F65E7390251F4BBA618B86D07FEE00FCEF12AFC9951F2EE7C7792
serial-01: E0FC91C02F657F9949EF08AB9C5A83D2003FB4110DF0E0D75E395E4D55B9CBBD
serial-02: 7FAA814CE33CA8CCA4E3CFDB7BC8C242E169DD49DC49786887233D2E653F7863
serial-03: 3C193A56EAB29024CEE6897E35D6DD96A535BBDFA74B7F7DFFF3B4DE906FBF88
manifest:  D97B0C75EBFD6AA5D0A0645FA58C14BE946E35F08B2E48ECC57C4ABC4155E4A4
```

C128 and the required three-boot C129 rerun passed:

```text
C128 manifest: out\dotnet\c130-c128-rerun\c128.manifest.json
C128 cases:    46
C128 manifest sha: 1DA0DE96B342F37EEC8C677482AA01D457F2E8BB1FA945779ED48BE15995AB48
C128 serials:  3A39C5C48FE61408626102544687DCDF5223FB6B227071682F10781F6F21A807
               47F9617D6DF09EF6BCED27639B06D49AD22F503B5D08941560820D92892C3274
               A9963DF06A9BAB679B9E30AA3FEFFD6787733ED4A22CCA2AA2CF1B62C3C13956

C129 manifest: out\dotnet\c130-c129-rerun\c129.manifest.json
C129 cases:    31
C129 manifest sha: DD992D12C1F3BF6B05919CC332A08157A5103CDD017043EFD50868B9A8F6BAB7
C129 serials:  4A7D3B30F6E454091EBF583482FA27A21AD5FD25ABBFB025EB1956FB5BDBE227
               A0604A05F5D6E87DB28BCD4CAA8B87135208D42F912C720610116E0B0972E149
               5CF38BEA91E246B1F95B8EF4A80AB4C3085746B727D86E5AEE0210E9CE05D494
```

The fresh C129 diagnostic inputs were composite
`1BC87CFC33409F48CAF55F11A93DE70D75B7CB1B343A7A67F02B2B2430624090`, kernel
`8D103A8DB87B2CB3FFB79B80F04567EF983ECA4755DB68E647BF50AA1FA8DDCA`, and
ramdisk `024F9FFC690C1185D49388F7F53157995FBE19AB8F935875F96D2EA98F41EC7F`.

The canonical ordinary kernel was restored from `ESP\kernel.elf`, and the
ordinary validator passed three fresh boots:

```text
manifest: out\dotnet\c130-ordinary-boot-final\run-20260920-195653302-d085647a\ordinary-boot.manifest.json
kernel:   A3325E6C5E62C4ECD9C463EE38DB0763DDA2A67F54D2A1AE1F86673B37457CDF
ordinary manifest sha FF9B74A15C7DE92E20FB4DA9275911E156EFD605F6114EF71B4F8E61D5F1649E
serial-01 D4DDD0A32C17C590BAA8603C76973A95E269A796AEF9BD56E42A155ADA0ED651
serial-02 B5AB49570499DE63FBD5AEE4B467FABE97A3CD97E96AD16BDDA33C1C8D365558
serial-03 353B95BFAB32C6E277392973F608AA8F17CCEA80732F2A9D1BE17C0669E416F7
```

## Architecture preserved and limitations

- GuideXos Host ABI v1 and table size 104 are unchanged.
- NativeAOT runtime architecture, GC, VFS, App Model, and lifecycle semantics
  are unchanged.
- Production Shift state remains independent for left/right keys; Tab remains
  key-down-only; no synthetic Tab `KeyChar` was introduced.
- `GuideXosPanel` remains single-level and non-owning. No nested Panel or
  owned-child hierarchy was introduced.
- Managed Notes remains at seven registrations.
- The retired helper never proved keyboard transport; that proof belongs to
  C129. Other keyboard layouts, USB HID translation, and arbitrary concurrent
  input streams remain outside these fixtures.
