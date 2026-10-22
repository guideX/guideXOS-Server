# NativeAOT C186R1 — Build Isolation and Historical Evidence

## Outcome

**Outcome A — Containment accepted.** The corrected validation used dedicated
C186R1 build, intermediate, publish, linker, conversion, temporary, and
manifest destinations. All required hosted and NativeAOT checks passed, and
all 13 inventoried protected trees remained byte-identical during the run.

This proves preservation during C186R1. It does not restore or certify original
C185 bytes that were not hashed before the first C186 invocation. The original
C185 acceptance report is unchanged; the original C186 report remains Outcome
B for that run.

## Preflight and lineage

| Item | Recorded state |
|---|---|
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Qualification start and end HEAD | `417d5168bfa3aeca331e7111b6e915fb001ae69f` |
| C185 boundary commit | `8d332b8f1b490b4667200b6f6461985a83fc7efa` |
| First C186 commit | `d14846a5eda85a59b4fdd05492b74ae11bfd9d39` |
| C186 report clarification commit | `417d5168bfa3aeca331e7111b6e915fb001ae69f` |
| `.phase` files | None found in the repository |
| D: free space before validation | 10.62 GiB |
| Protected ramdisk | SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` before and after |

At preflight, `ESP/ramdisk.img` was an existing worktree modification. The
existing untracked `bin`, `obj`, and C182 smoke directories were also present.
They were not reset, cleaned, or staged. QAC and QEMU were not modified; no
guest run was performed.

The C186 commits are direct ancestors on the current branch. C186R1 wrote new
evidence under `out/dotnet/c186r1-artifact-isolation/`.

## Original C186 routing failure

The first C186 command passed `--artifacts-path` and `--output` under
`out/dotnet/c186-managed-host-contract/nativeaot/`. It also passed explicit
C186 paths for `runtime_support.obj` and the linker map. However, the
repository root `Directory.Build.props` has a condition for
`MSBuildProjectName == HostLogProof` and `HostLogProofC185Boundary == true`
that sets these properties into the C185 tree:

- `BaseOutputPath` → `out/dotnet/c185-managed-host-services/host-bin/`
- `BaseIntermediateOutputPath` → `out/dotnet/c185-managed-host-services/host-obj/`
- `MSBuildProjectExtensionsPath` → the same C185 `host-obj/` root

`ManagedHostServicesC185.csproj` sets `HostLogProofC185Boundary=true` on its
HostLogProof project reference. The C186 NativeAOT command used that same
property. The root props therefore overrode the HostLogProof project output
and intermediate locations despite the command's `--artifacts-path` argument.
The preserved C186 build log confirms `HostLogProof.dll` was written to
`out/dotnet/c185-managed-host-services/host-bin/release_win-x64/`.

The C186 command's explicit native `--output`, runtime support object, and map
were in the C186 directory. The saved first and isolated invocation commands
and logs are in `out/dotnet/c186-managed-host-contract/nativeaot/`. The early
spill identified by those logs and inherited MSBuild properties is the pair of
lowercase `release_win-x64` subtrees below.

The audit also found project-level fallbacks in `HostLogProof.csproj`: when
`ArtifactsPath` is empty and the C185 boundary is off, the default bin and obj
roots are `out/dotnet/managed-hostlog/bin/` and `obj/`, with the map under
`managed-hostlog/artifacts/`. When the C185 boundary is on and no explicit
`HostLogProofMapPath` is passed, the map defaults to
`out/dotnet/c185-managed-host-services/HostLogProof.map`. The failed C186
command explicitly routed its publish directory, runtime support object, and
map into C186, while the root `Directory.Build.props` still redirected
HostLogProof's project output, intermediates, and native PE copy into the
affected C185 subtrees. The C186R1 invocation replaces the inherited props and
supplies every path explicitly.

The general `scripts/dotnet/build-managed-hostlog-proof.ps1` wrapper defaults
to `out/dotnet/managed-hostlog/` and passes explicit bin, obj, map, PE, and ELF
paths. The older NativeAOT smoke wrappers likewise pass phase-scoped output,
intermediate, runtime-object, and map paths; they do not set
`HostLogProofC185Boundary`. Those wrappers were audited but not run. The
C185-specific spill came from the inherited root props combined with the C185
project reference and C186's boundary property.

## C185 artifact impact

The first C186 invocation may have created or overwritten every current file
under the two destinations below. Without a pre-run inventory, it is not
possible to tell which individual path was new and which may have overwritten
an earlier file. These are project build outputs and intermediates, not the
specifically recorded C185 qualification artifacts. The C185 report's named
PE, ELF, map, and logs are outside these two subtrees and their documented
hashes still match.

| Potentially affected path | Purpose | Current inventory | Historical identity |
|---|---|---:|---|
| `out/dotnet/c185-managed-host-services/host-bin/release_win-x64/**` | HostLogProof build output, framework dependencies, managed assemblies, NativeAOT PE/debug output | 194 files, 83,596,510 bytes | **Historical contents unverified** |
| `out/dotnet/c185-managed-host-services/host-obj/release_win-x64/**` | MSBuild intermediates, generated/reference assemblies, ILC object, linker response and definition files | 18 files, 4,319,622 bytes | **Historical contents unverified** |

The per-file impact matrix lists all 212 current paths with purpose, current
SHA-256, historical SHA-256 when available, retained-copy comparison,
confidence, and limitation:

- `out/dotnet/c186r1-artifact-isolation/evidence/run-20261021-182526-821/c185-artifact-impact-matrix.csv`
- SHA-256: `8CD66A417F0023B1E4D53A58C038DFCDB940080E85E500A0471B6DF51C48B6B5`

Representative current hashes in the affected trees are:

| Path | Current SHA-256 | Historical comparison |
|---|---|---|
| `out/dotnet/c185-managed-host-services/host-bin/release_win-x64/HostLogProof.dll` | `0F1A5F84E64258728F9DCFE3308177883FE9D2A2C469B1E07F10F29EB2EEB681` | Historical contents unverified |
| `out/dotnet/c185-managed-host-services/host-bin/release_win-x64/native/HostLogProof.exe` | `D252E930B7CDFCBE386A37DED4C51B5EF6D1AD5297CF25D1AE9314A0964DC37B` | Historical contents unverified |
| `out/dotnet/c185-managed-host-services/host-obj/release_win-x64/native/HostLogProof.obj` | `CA1827139D9BA0BD52BB33922E2851A7AF4BA5DD3B21B337417B8386572B358C` | Historical contents unverified |

The original C186 isolated build retained corresponding files for 210 of the
212 current paths: 195 have the same current hash, 15 have different current
hashes, and 2 have no corresponding retained copy. These later C186 copies
cannot establish original C185 bytes. The comparison is retained at
`out/dotnet/c186r1-artifact-isolation/evidence/run-20261021-182526-821/c185-retained-c186-counterpart-comparison.csv`
(SHA-256 `249EAE7CE7D3C3840321B29CC2362F8EB51EBA54B9358885063F987CF9ADD43C`).

### C185 hashes recorded before the C186 incident

The C185 acceptance report records 12 artifact hashes. Each current file still
matches its documented SHA-256. This verifies only the specific recorded paths;
it is not a baseline for the 212 affected `release_win-x64` files.

| C185 documented path | Historical SHA-256 | Current result |
|---|---|---|
| `out/dotnet/c185-managed-host-services/c182-starter-tests.log` | `993D51862059EAB8516DBE6A84556C4C61192105DBEC2604120A6289F2FFA8F4` | MATCH |
| `out/dotnet/c185-managed-host-services/c185-host-tests.log` | `8FC537FC76922C292EC62E4E4C2F83FBB30A4C34F9FAF914F64CAD24985021F7` | MATCH |
| `out/dotnet/c185-managed-host-services/counterapp-independent-build.log` | `952EF98212B846FFD43CA20301BDD144843A1756578035A6F6C2028F383FE481` | MATCH |
| `out/dotnet/c185-managed-host-services/host-bin/x64/Release/net9.0/win-x64/publish/HostLogProof.exe` | `7A70C1F8D6B9561220FEFE741D5AEA997A33B3EF0B7942A95C15505173EA033C` | MATCH |
| `out/dotnet/c185-managed-host-services/HostLogProof.elf` | `7D408CCC7ED260EDD172E8D729DAD7C138DE61C0326ABC72257A166FE620BB49` | MATCH |
| `out/dotnet/c185-managed-host-services/HostLogProof.map` | `2F2842B989E68EA981CFD470E8F8D49250FD75151F89E936D2BCEF9BA95F424E` | MATCH |
| `out/dotnet/c185-managed-host-services/legacy-evidence/c135-focused-host-build.txt` | `C63490D4047B799E126B60AC5602A6CC28935C277313A5A09FEBBCAEBEA8410F` | MATCH |
| `out/dotnet/c185-managed-host-services/legacy-evidence/c167-c168-settings-host-suites.txt` | `A93E53E5929EC5F79AD71252BEDB519FA8DFD03DD40C3F5FFB9A5B3FEAAC4C72` | MATCH |
| `out/dotnet/c185-managed-host-services/legacy-evidence/managed-ui-host-suites.txt` | `E2F0947C031B721BC37D39F52C7B1744A395FBC22E867D249972ECE4C02BC08C` | MATCH |
| `out/dotnet/c185-managed-host-services/publish.log` | `6806CF3B065601617427FDF5D4FC23D4EA420DD5E34543A3573F8DE6C4BCDF54` | MATCH |
| `samples/managed/C185CounterApp/bin/x64/Release/net9.0/GuideXos.C185.CounterApp.dll` | `836E5323F43514C9DEA23EF9781E8A45F54E8E4A6D8FE7B5B35C0535EF4E0814` | MATCH |
| `samples/managed/ManagedUiContracts/bin/x64/Release/net9.0/GuideXos.ManagedUi.Contracts.dll` | `1F9F4F3D5087AC96C670F148ABC56F19AC2EFEC48ACCA1F89981154C71F94830` | MATCH |

The comparison CSV contains the current hash next to each documented hash:
`out/dotnet/c186r1-artifact-isolation/evidence/run-20261021-182526-821/c185-documented-hash-comparison.csv`
(SHA-256 `A84FC72B44BB28ED9B672E304F8C0CD2CD85C02D498954E6DE9384EA0D8BBDE9`).

## Bounded output isolation

The new C186R1 runner reuses the existing .NET build, managed regression,
NativeAOT publish, PE-to-ELF converter, and `readelf` tools. It writes all
validation output below `out/dotnet/c186r1-artifact-isolation/` and routes
HostLogProof `BaseOutputPath`, `BaseIntermediateOutputPath`, and
`MSBuildProjectExtensionsPath` to scope-specific directories there. It gives
each build a distinct `C186R1BuildScope`, so hosted qualification and
NativeAOT do not share project intermediates.

The NativeAOT command explicitly routes the .NET artifacts root, publish
directory, runtime support object (`/Fo`), linker map
(`HostLogProofMapPath`), PE, ELF, readelf records, manifest, and temporary
directory under the C186R1 root. The project-level C185 linker-map fallback is
overridden. The root `Directory.Build.props` is replaced for the invocation by
the generated C186R1 props file.

The default NativeAOT output root is fixed at
`out/dotnet/c186r1-artifact-isolation/nativeaot`. Before creating directories,
the runner rejects destinations outside the dedicated C186R1 root, paths that
overlap protected C185/C182/C181/sample locations, and existing reparse-point
components. Non-destructive negative checks accepted the C186R1 root, rejected
three unsafe roots, and created no probe paths.

The scripts are:

- `scripts/dotnet/run-c186r1-artifact-isolation.ps1`
- `scripts/dotnet/run-c186r1-nativeaot.ps1`
- `scripts/dotnet/test-c186r1-output-routing.ps1`

## Prospective preservation evidence

Before the final controlled run, C186R1 recorded a complete path, size, and
SHA-256 inventory of the C185 tree and the other named protected trees. It
recomputed all inventories after qualification and compared exact paths,
sizes, and content hashes. The final run is
`run-20261021-182526-821`:

- Before C185 inventory: `.../protected-before/c185-managed-host-services.json`,
  SHA-256 `E3DC6E1510A76D2934B44A4E481D8A2C88EA84DDCF15D6B3AA1415295F2D1AEC`
- After C185 inventory: `.../protected-after/c185-managed-host-services.json`,
  SHA-256 `27D5A75517F9218C221A7C8957C35BE576704D97DB320C95C616DEA224FDD1E4`
- C185 comparison: 1,201 files before and after; 0 added, 0 removed, 0 changed
- Protected tree comparison: 13 of 13 PASS, including C185, previous C186
  evidence, C182 and C181 evidence, and the pre-existing sample `bin`, `obj`,
  and C182 smoke directories
- Sample directories included: `samples/host/ManagedHostServicesC185/{bin,obj}`,
  `samples/host/ManagedStarterC182/obj`,
  `samples/host/ManagedUiRegression/obj`,
  `samples/managed/C185CounterApp/{bin,obj}`,
  `samples/managed/HostLogProof/c182-host-smoke`, and
  `samples/managed/ManagedUiContracts/{bin,obj}`
- Ramdisk: before and after SHA-256
  `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`
- Validation summary SHA-256:
  `EEA2D807082A9CFB72136B084F0E3E13A8E3CC7D0800B3ADD98CEAA44764520C`

The full inventories are retained under
`out/dotnet/c186r1-artifact-isolation/evidence/run-20261021-182526-821/`.
They prove preservation during this run only. They do not create a historical
C185 baseline.

## Hosted and NativeAOT qualification

| Qualification | Result |
|---|---|
| Independent ManagedUiContracts build | PASS, 0 warnings, 0 errors |
| Independent CounterApp build | PASS, 0 warnings, 0 errors |
| C182 managed starter | 15 checks PASS; 100 resize transactions; 0 resize allocation bytes |
| C185 hosted service qualification | 28 cases PASS |
| C186 hosted lifecycle qualification | 15 cases PASS: 9 focused checks and 6 stress checks |
| Managed UI aggregate | Wrapper exit 0; 38 suites / 930 cases PASS |
| C167/C168 Settings host suites | PASS inside the aggregate wrapper |
| Aggregate manifest validator | Correct manifest accepted; all 6 deliberately wrong or incomplete manifests rejected, including missing-suite and failed-suite cases |
| Unsafe output path checks | 3 unsafe destinations rejected; 0 paths created |
| NativeAOT publish | PASS; static C185 composition retained |
| PE-to-ELF conversion | PASS |
| ELF validation | ELF64, EXEC, AMD64, entry `0x10008600`, 7 load segments, 0 section headers |

Two earlier harness attempts remain in the evidence directory with FAIL status.
The first stopped after C182 because the orchestration wrapper returned a log
line together with its result object. The next exposed missing restore outputs
for the isolated C185 composition and an overly strict `readelf -l` section
header check. The wrapper now builds the composition dependencies into the
same isolated artifacts root and checks the section count from `readelf -h`.
Both preliminary attempts also recorded unchanged protected-tree comparisons;
the timestamped run above is the accepted complete qualification.

The link map retains `CounterApplicationFactory.Create`, the application
constructor, `HandleAction`, `HandleInput`, `HandleResize`, `OnTearingDown`,
and `ManagedMain`. Their map addresses are included in
`nativeaot/retained-code-map-evidence.txt`.

Final artifacts, all relative to the repository root:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `out/dotnet/c186r1-artifact-isolation/qualifications/contracts/artifacts/bin/GuideXos.ManagedUi.Contracts/release/GuideXos.ManagedUi.Contracts.dll` | 11,264 | `3FC881BE4EC503301368742057B42A9D9FD49CD0222FDB2190D5089B2A6AB3A1` |
| `out/dotnet/c186r1-artifact-isolation/qualifications/counterapp/artifacts/bin/GuideXos.C185.CounterApp/release/GuideXos.C185.CounterApp.dll` | 9,728 | `0F94DAC9C95E647C075D0722382F5C60A778526B1566474AB780D64AAEEE0BA0` |
| `out/dotnet/c186r1-artifact-isolation/nativeaot/publish/HostLogProof.exe` | 649,728 | `F182AAB6BC1CCD589875161D1C548BF4B5100C3BCAF97B299815D91AA4D5E094` |
| `out/dotnet/c186r1-artifact-isolation/nativeaot/HostLogProof.map` | 1,083,032 | `854137505B2B8CB274D9052A4F6C4250E42B84988FEF5875A491146B7F3DCA9D` |
| `out/dotnet/c186r1-artifact-isolation/nativeaot/HostLogProof.elf` | 671,744 | `4A8E6D0D9342032315B2ADAF223953A72ECC1BAAAAA545B9D1DA68CE2F4536EF` |
| `out/dotnet/c186r1-artifact-isolation/nativeaot/runtime_support.obj` | 5,449 | `B2BB0F3F7665BA640D56817FD0016582892985714EB9BA63957C555F1DD6A4D8` |
| `out/dotnet/c186r1-artifact-isolation/nativeaot/nativeaot-artifact-manifest.json` | 99,160 | `D364C8CD6C33E35A0FFDB7A41DFB1D10E2E82FC840D8FDB863ADED97D5896BCE` |
| `out/dotnet/c186r1-artifact-isolation/nativeaot/artifact-sha256.txt` | 44,805 | `8462691A13F6D3667F32C14CC602AD4FA1900766C358F44D0271DD86A04EBCD8` |

NativeAOT warnings were the existing `CA2014` in unchanged
`ManagedNotes.cs:322`, the explicit ILCompiler package-version warning,
`LNK4237` for `advapi32.lib` and `kernel32.lib`, `LNK4281` for the fixed proof
image base, and two `LNK4210` CRT section warnings. No new source warning was
reported. The run did not change application, kernel, selector, ABI, or
production registration code, so no guest qualification was needed.

## Worktree and C187 handoff

The qualification began and ended at `417d5168bfa3aeca331e7111b6e915fb001ae69f`
on `v1.1_DOTNET_SUPPORT`. The C186R1 scripts and documentation are committed
locally with this follow-up. The pre-existing modified ramdisk and untracked
sample output trees remain outside that commit. The ramdisk retains its
recorded SHA-256 above.

C187 is ready for a handoff on this isolated baseline. Carry the historical
C185 uncertainty forward explicitly. The recommended next work remains to
specify and qualify native close notification and persistent close-failure
semantics for a real guest application. Dynamic loading, extra application
slots, and broad framework extraction stay deferred unless separately
accepted. C187 was not started.
