param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$LegacyRoot = $env:GUIDEXOS_LEGACY_ROOT,
    [string]$DotNetExe = "dotnet",
    [string]$PythonExe = "",
    [string]$PeToElfScript = "",
    [string]$OutputRoot = "",
    [string]$DirectoryBuildPropsPath = "",
    [string]$RuntimePackRoot = "",
    [switch]$UseGuideXosRuntimePack,
    [ValidateSet("NonAllocating", "Allocating", "Repeated")]
    [string]$AllocationMode = "NonAllocating",
    [string]$RuntimePackOutputRoot = "",
    [switch]$ProductionApplication,
    [switch]$C185ExternalComposition,
    [switch]$ProjectArtifactIsolation,
    [switch]$C187GuestCandidate,
    [switch]$C188ExternalAppIdentity,
    [switch]$C188R3NotesStartupDiagnostics,
    [switch]$C188R4NotesStartupOperationIsolation,
    [switch]$C188R6NotesPostRenderStartup,
    [switch]$C188R7NotesProofAuthorization,
    [switch]$C188R8NotesInputRouting,
    [switch]$C188R9RegistryInputReentry,
    [switch]$PersistentCompositeLifecycle,
    [switch]$ThreadStaticLifecycleDiagnostics,
    [switch]$IncludeC134FocusedTests,
    [switch]$IncludeC135FocusedTests,
    [switch]$IncludeC136FocusedTests,
    [switch]$C155ManagedNotesSession,
    [switch]$C156ControlModifierShortcuts,
    [switch]$C157ManagedNotesNewDocument,
    [switch]$C158ManagedCalculator,
    [switch]$C160ApplicationSnapshotProof,
    [switch]$C161ManagedTaskManager,
    [switch]$C161TaskManagerProof,
    [switch]$C162ManagedTaskManagerClose,
    [switch]$C162TaskManagerCloseProof,
    [switch]$C163ManagedFileExplorer,
    [switch]$C163FileExplorerProof,
    [switch]$C163FileExplorerDiagnostics,
    [switch]$C164FileActivationProof,
    [switch]$C164FileActivationDiagnostics,
    [switch]$C166FileAssociationProof,
    [switch]$C166OrdinaryBuild,
    [switch]$C173ManagedFileExplorerResize,
    [switch]$C175ManagedNotesResize,
    [switch]$C177ManagedNotesNewlineSave,
    [switch]$C178ManagedCalculatorResize,
    [switch]$C179ManagedTaskManagerResize,
    [switch]$C167DefaultAppsProof,
    [switch]$ProofControl,
    [ValidateSet("Production", "C104AppA", "C104AppB", "C107Composite", "C108ThreadStaticLifecycle", "ProductionComposite", "C112Composite", "C113Composite", "C114Composite", "C115Composite", "C116Composite", "C117Composite", "C118Composite", "C119Composite", "C120Composite", "C121Composite", "C122Composite", "C123Composite", "C124Composite", "C125Composite", "C126Composite", "C127Composite", "C128Composite", "C129Composite", "C131Composite", "C132Composite", "C133Composite", "C134Composite", "C135Composite", "C136Composite", "C137Composite", "C138Composite", "C139Composite", "C140Composite", "C141Composite", "C142Composite", "C143Composite", "C144Composite", "C145Composite", "C146Composite", "C147Composite", "C148Composite", "C149Composite", "C150Composite", "C151Composite", "C152Composite", "C153Composite", "C154Composite", "C158Composite", "C160Composite", "C167ProofComposite")]
    [string]$ManagedProjectMode = "",
    [ValidateSet("Primary64KiB", "Primary128KiB", "Primary256KiB", "Primary4MiB", "Primary8MiB", "Small4KiB")]
    [string]$HeapConfiguration = "Primary64KiB",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "managed-hostlog-artifact-assertions.ps1")
if ($C163ManagedFileExplorer) {
    $C162ManagedTaskManagerClose = $true
}
if ($C163FileExplorerProof) {
    $C163ManagedFileExplorer = $true
}
if ($C164FileActivationProof) {
    $C163FileExplorerProof = $true
    $C163ManagedFileExplorer = $true
}
if ($C175ManagedNotesResize -and -not $C173ManagedFileExplorerResize) {
    throw 'C175 Managed Notes resizing requires the shared C172/C173 compositor resize support.'
}
if ($C177ManagedNotesNewlineSave -and -not $C175ManagedNotesResize) {
    throw 'C177 Managed Notes newline/save proof requires the C175 Notes application lane.'
}
if ($C178ManagedCalculatorResize -and
    (-not $C158ManagedCalculator -or -not $C173ManagedFileExplorerResize -or
     -not $C175ManagedNotesResize -or -not $ProofControl)) {
    throw 'C178 managed Calculator proof requires the existing C158 Calculator, shared C173/C175 resize lane, and proof control.'
}
if ($C179ManagedTaskManagerResize -and
    (-not $C161ManagedTaskManager -or -not $C161TaskManagerProof -or
     -not $C173ManagedFileExplorerResize -or -not $ProofControl)) {
    throw 'C179 managed Task Manager proof requires the existing C161 application, shared C173 resize lane, and proof control.'
}
if ($C162ManagedTaskManagerClose) {
    $C161ManagedTaskManager = $true
}
if ($C163FileExplorerProof) {
    $C161TaskManagerProof = $true
    $C162TaskManagerCloseProof = $true
}
function Resolve-AbsolutePath([string]$Path) {
    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-Sha256Hex([string]$Path) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($stream)) -replace '-', '').ToUpperInvariant()
    }
    finally {
        $stream.Dispose()
        $sha256.Dispose()
    }
}

function Assert-WithinRoot([string]$Path, [string]$Root, [string]$Label) {
    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $resolvedRoot = [System.IO.Path]::GetFullPath($Root.TrimEnd('\', '/'))
    if (-not $resolvedRoot.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $resolvedRoot += [System.IO.Path]::DirectorySeparatorChar
    }
    if (-not $resolvedPath.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label path escapes the repository root: $resolvedPath"
    }
}

function Get-CommandPath([string]$Name) {
    $cmd = Get-Command -Name $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $cmd) {
        return $cmd.Source
    }
    return $null
}

function Find-VcVars64 {
    $candidateVcVars = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    )

    foreach ($path in $candidateVcVars) {
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    return $null
}

function Get-ImportTable([string]$Path) {
    $imports = [ordered]@{}
    $currentDll = $null

    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*DLL Name:\s+(.+)$') {
            $currentDll = $Matches[1].Trim()
            if (-not $imports.Contains($currentDll)) {
                $imports[$currentDll] = New-Object System.Collections.Generic.List[string]
            }
            continue
        }

        if ($null -ne $currentDll -and $line -match '^\s*[0-9A-Fa-f]+\s+<none>\s+[0-9A-Fa-f]+\s+([^\s]+)\s*$') {
            [void]$imports[$currentDll].Add($Matches[1].Trim())
        }
    }

    return $imports
}

function Assert-SetEquals([string[]]$Actual, [string[]]$Expected, [string]$Label) {
    $actualSet = @($Actual | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    $expectedSet = @($Expected | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    $diff = Compare-Object -ReferenceObject $expectedSet -DifferenceObject $actualSet
    if ($diff) {
        throw "$Label mismatch.`nExpected: $($expectedSet -join ', ')`nActual: $($actualSet -join ', ')"
    }
}

function Assert-FileContains([string]$Path, [string[]]$Patterns, [string]$Label) {
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($pattern in $Patterns) {
        if ($text -notmatch $pattern) {
            throw "$Label missing pattern: $pattern"
        }
    }
}

function Assert-FileNotContains([string]$Path, [string[]]$Patterns, [string]$Label) {
    $text = Get-Content -LiteralPath $Path -Raw
    foreach ($pattern in $Patterns) {
        if ($text -match $pattern) {
            throw "$Label unexpectedly contained pattern: $pattern"
        }
    }
}

$projectDir = Join-Path $RepoRoot "samples\managed\HostLogProof"
$projectFile = Join-Path $projectDir "HostLogProof.csproj"
$defaultOutputRoot = Join-Path $RepoRoot "out\dotnet\managed-hostlog"
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = $defaultOutputRoot
}

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)

Assert-WithinRoot $OutputRoot $RepoRoot "Output"

$runtimePackManifest = $null
$runtimePackObject = $null
$runtimePackSdkPath = $null
$runtimePackManifestHash = $null
$runtimePackObjectHash = $null
$startupImportsObj = $null
if ($UseGuideXosRuntimePack) {
    if ([string]::IsNullOrWhiteSpace($RuntimePackRoot)) {
        $RuntimePackRoot = Join-Path $RepoRoot "tools\dotnet\runtime-pack"
    }
    $RuntimePackRoot = [System.IO.Path]::GetFullPath($RuntimePackRoot)
    Assert-WithinRoot $RuntimePackRoot $RepoRoot "Runtime-pack source"
    $runtimePackBuild = Join-Path $RuntimePackRoot "build-runtime-pack.ps1"
    if (-not (Test-Path -LiteralPath $runtimePackBuild)) {
        throw "GuideXOS runtime-pack build script not found: $runtimePackBuild"
    }
    if ([string]::IsNullOrWhiteSpace($RuntimePackOutputRoot)) {
        $RuntimePackOutputRoot = Join-Path $RepoRoot "out\dotnet\runtime-pack"
    }
    $runtimePackOutputRoot = [System.IO.Path]::GetFullPath($RuntimePackOutputRoot)
    Assert-WithinRoot $runtimePackOutputRoot (Join-Path $RepoRoot "out\dotnet") "Runtime-pack output"
    $runtimePackBuildArguments = @(
        "-RepoRoot", $RepoRoot,
        "-RuntimePackRoot", $RuntimePackRoot,
        "-OutputRoot", $runtimePackOutputRoot
    )
    if ($Clean) { $runtimePackBuildArguments += "-Clean" }
    if ($AllocationMode -eq "Repeated") { $runtimePackBuildArguments += "-ManagedRepeatedAllocation" }
    elseif ($AllocationMode -eq "Allocating") { $runtimePackBuildArguments += "-ManagedAllocation" }
    if ($ProductionApplication) { $runtimePackBuildArguments += "-ProductionApplication" }
    if ($PersistentCompositeLifecycle) { $runtimePackBuildArguments += "-PersistentCompositeLifecycle" }
    if ($ThreadStaticLifecycleDiagnostics) { $runtimePackBuildArguments += "-ThreadStaticLifecycleDiagnostics" }
    if ($HeapConfiguration -ne "Primary64KiB") { $runtimePackBuildArguments += @("-HeapConfiguration", $HeapConfiguration) }
    & powershell -ExecutionPolicy Bypass -File $runtimePackBuild @runtimePackBuildArguments
    if ($LASTEXITCODE -ne 0) {
        throw "GuideXOS runtime-pack build failed with exit code $LASTEXITCODE"
    }
    $runtimePackManifest = Join-Path $runtimePackOutputRoot "runtime-pack.manifest.json"
    if (-not (Test-Path -LiteralPath $runtimePackManifest)) {
        throw "GuideXOS runtime-pack manifest not found: $runtimePackManifest"
    }
    $runtimePackManifestObject = Get-Content -LiteralPath $runtimePackManifest -Raw | ConvertFrom-Json
    $runtimePackObject = [string]$runtimePackManifestObject.object
    if ([string]::IsNullOrWhiteSpace($runtimePackObject) -or -not (Test-Path -LiteralPath $runtimePackObject)) {
        throw "GuideXOS runtime-pack object not found: $runtimePackObject"
    }
    $runtimePackObjectHash = Get-Sha256Hex $runtimePackObject
    if ($runtimePackObjectHash.ToUpperInvariant() -ne [string]$runtimePackManifestObject.objectSha256.ToUpperInvariant()) {
        throw "GuideXOS runtime-pack object hash does not match its manifest."
    }
    $runtimePackSdkPath = [string]$runtimePackManifestObject.sdkPath
    if ([string]::IsNullOrWhiteSpace($runtimePackSdkPath) -or -not (Test-Path -LiteralPath $runtimePackSdkPath)) {
        throw "GuideXOS runtime-pack SDK path is missing: $runtimePackSdkPath"
    }
    $runtimePackManifestHash = Get-Sha256Hex $runtimePackManifest
    if ($ProductionApplication) {
        $startupImportsObj = Join-Path $runtimePackOutputRoot "guidexos_nativeaot_startup_imports.production.obj"
        if (-not (Test-Path -LiteralPath $startupImportsObj)) {
            throw "Production NativeAOT import shim object not found: $startupImportsObj"
        }
    }
}

if (-not (Test-Path -LiteralPath $projectFile)) {
    throw "Sample project not found: $projectFile"
}

if ([string]::IsNullOrWhiteSpace($LegacyRoot)) {
    $LegacyRoot = "D:\dev\guideXOSUEFI"
}

$LegacyRoot = [System.IO.Path]::GetFullPath($LegacyRoot)
$peToElfDefault = Join-Path $RepoRoot "tools\dotnet\pe_to_elf_v2_fixed_base.py"
if ([string]::IsNullOrWhiteSpace($PeToElfScript)) {
    $PeToElfScript = $peToElfDefault
}

if (-not (Test-Path -LiteralPath $PeToElfScript)) {
    throw "PE-to-ELF converter not found: $PeToElfScript"
}

$expectedPeToElfSha256 = "5BED5BBE8883EDD700ED2406FA43A7B26F762212D06898BB75B540E07B2F5621"
$actualPeToElfSha256 = Get-Sha256Hex $PeToElfScript
if ($actualPeToElfSha256 -ne $expectedPeToElfSha256) {
    throw "PE-to-ELF converter hash mismatch. Expected $expectedPeToElfSha256, got ${actualPeToElfSha256}: $PeToElfScript"
}

$bundledPython = Join-Path $env:USERPROFILE ".cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if ([string]::IsNullOrWhiteSpace($PythonExe)) {
    if (Test-Path -LiteralPath $bundledPython) {
        $PythonExe = $bundledPython
    } elseif (Get-CommandPath "py") {
        $PythonExe = "py"
    } elseif (Get-CommandPath "python") {
        $PythonExe = "python"
    }
}

if ([string]::IsNullOrWhiteSpace($PythonExe)) {
    throw "Python was not found. Set -PythonExe or install the bundled runtime Python."
}

$readelfExe = Get-CommandPath "readelf"
if ([string]::IsNullOrWhiteSpace($readelfExe)) {
    $readelfExe = Get-CommandPath "llvm-readelf"
}
if ([string]::IsNullOrWhiteSpace($readelfExe)) {
    throw "Neither readelf nor llvm-readelf was found."
}

$objdumpExe = Get-CommandPath "objdump"
if ([string]::IsNullOrWhiteSpace($objdumpExe)) {
    $objdumpExe = Get-CommandPath "llvm-objdump"
}
if ([string]::IsNullOrWhiteSpace($objdumpExe)) {
    throw "Neither objdump nor llvm-objdump was found."
}

$vcvars64 = Find-VcVars64
if ([string]::IsNullOrWhiteSpace($vcvars64)) {
    throw "Visual C++ build environment not found. Install the Desktop Development with C++ workload and vcvars64.bat."
}

$binRoot = Join-Path $OutputRoot "bin"
$objRoot = Join-Path $OutputRoot "obj"
$artifactRoot = Join-Path $OutputRoot "artifacts"
$publishExe = $null
$publishMap = $null
$publishIlcRsp = $null
$publishLinkRsp = $null
$nativeHostLogObj = $null
$artifactExe = Join-Path $artifactRoot "HostLogProof.exe"
$artifactMap = Join-Path $artifactRoot "HostLogProof.map"
$artifactElf = Join-Path $artifactRoot "HostLogProof.elf"
$artifactPeDump = Join-Path $artifactRoot "HostLogProof.pe.objdump.txt"
$artifactElfDump = Join-Path $artifactRoot "HostLogProof.elf.objdump.txt"
$artifactElfReadelf = Join-Path $artifactRoot "HostLogProof.elf.readelf.txt"
$artifactElfDisasm = Join-Path $artifactRoot "HostLogProof.elf.disasm.txt"
$artifactNativeObjDump = Join-Path $artifactRoot "HostLogProof.native.objdump.txt"
$artifactNativeObjReloc = Join-Path $artifactRoot "HostLogProof.native.reloc.txt"
$artifactRuntimePackObjDump = Join-Path $artifactRoot "guidexos_nativeaot_platform.objdump.txt"
$artifactRuntimePackObjReloc = Join-Path $artifactRoot "guidexos_nativeaot_platform.reloc.txt"
$artifactIlcRsp = Join-Path $artifactRoot "HostLogProof.ilc.rsp"
$artifactLinkRsp = Join-Path $artifactRoot "HostLogProof.link.rsp"
$artifactToolchain = Join-Path $artifactRoot "toolchain.txt"
$artifactDotNetInfo = Join-Path $artifactRoot "dotnet-info.txt"
$artifactPythonInfo = Join-Path $artifactRoot "python-info.txt"
$buildBatch = Join-Path $artifactRoot "build-native-hostlog.bat"
$runtimeSupportSource = Join-Path $projectDir "runtime_support.c"
$runtimeSupportObj = Join-Path $artifactRoot "runtime_support.obj"
$c102StartupSource = Join-Path $projectDir "c102_startup.c"
$c102StartupObj = Join-Path $artifactRoot "c102_startup.obj"
$palMinWinSource = Join-Path $RuntimePackRoot "src\platform\guidexos_nativeaot_pal_minwin_replacement.cpp"
$palMinWinObj = Join-Path $artifactRoot "guidexos_nativeaot_pal_minwin_replacement.obj"
$palContractSource = Join-Path $RuntimePackRoot "src\platform\guidexos_nativeaot_pal_contract.cpp"
$palContractObj = Join-Path $artifactRoot "guidexos_nativeaot_pal_contract.obj"
$gcStartupContractSource = Join-Path $RuntimePackRoot "src\platform\guidexos_nativeaot_gc_startup_platform_contract.cpp"
$gcStartupContractObj = Join-Path $artifactRoot "guidexos_nativeaot_gc_startup_platform_contract.obj"
$managedHostShimsSource = Join-Path $RuntimePackRoot "src\probes\guidexos_nativeaot_managed_host_shims.cpp"
$managedHostShimsObj = Join-Path $artifactRoot "guidexos_nativeaot_managed_host_shims.obj"

if ($Clean) {
    Assert-WithinRoot $OutputRoot $RepoRoot "Output"
    if (Test-Path -LiteralPath $OutputRoot) {
        Remove-Item -LiteralPath $OutputRoot -Recurse -Force
    }
}

New-Item -ItemType Directory -Force -Path $artifactRoot | Out-Null
New-Item -ItemType Directory -Force -Path $binRoot | Out-Null
New-Item -ItemType Directory -Force -Path $objRoot | Out-Null

$dotnetVersion = & $DotNetExe --version
if ($LASTEXITCODE -ne 0) {
    throw "dotnet --version failed."
}

$dotnetInfo = & $DotNetExe --info
if ($LASTEXITCODE -ne 0) {
    throw "dotnet --info failed."
}

$pythonVersion = & $PythonExe --version
if ($LASTEXITCODE -ne 0) {
    throw "python --version failed."
}

$toolchainLines = @(
    "RepoRoot=$RepoRoot"
    "LegacyRoot=$LegacyRoot"
    "OutputRoot=$OutputRoot"
    "ArtifactRoot=$artifactRoot"
    "ArtifactExe=$artifactExe"
    "ArtifactMap=$artifactMap"
    "PeToElfScript=$PeToElfScript"
    "PeToElfSha256=$actualPeToElfSha256"
    "PythonExe=$PythonExe"
    "ReadelfExe=$readelfExe"
    "ObjdumpExe=$objdumpExe"
    "VcVars64=$vcvars64"
    "RuntimeSupportSource=$runtimeSupportSource"
    "RuntimeSupportObj=$runtimeSupportObj"
    "C102StartupSource=$c102StartupSource"
    "C102StartupObj=$c102StartupObj"
    "PalMinWinSource=$palMinWinSource"
    "PalMinWinObj=$palMinWinObj"
    "PalContractSource=$palContractSource"
    "PalContractObj=$palContractObj"
    "GcStartupContractSource=$gcStartupContractSource"
    "GcStartupContractObj=$gcStartupContractObj"
    "ManagedHostShimsSource=$managedHostShimsSource"
    "ManagedHostShimsObj=$managedHostShimsObj"
    "NativeImportObj=$startupImportsObj"
    "ProductionApplication=$ProductionApplication"
    "PersistentCompositeLifecycle=$PersistentCompositeLifecycle"
        "UseGuideXosRuntimePack=$UseGuideXosRuntimePack"
        "AllocationMode=$AllocationMode"
    "RuntimePackRoot=$RuntimePackRoot"
    "RuntimePackOutputRoot=$RuntimePackOutputRoot"
    "HeapConfiguration=$HeapConfiguration"
    "RuntimePackManifest=$runtimePackManifest"
    "RuntimePackObject=$runtimePackObject"
    "RuntimePackSdkPath=$runtimePackSdkPath"
    "RuntimePackManifestSha256=$runtimePackManifestHash"
    "RuntimePackObjectSha256=$runtimePackObjectHash"
    "DotNetVersion=$dotnetVersion"
    "PythonVersion=$pythonVersion"
)
$toolchainLines | Set-Content -LiteralPath $artifactToolchain -Encoding ASCII
$dotnetInfo | Set-Content -LiteralPath $artifactDotNetInfo -Encoding ASCII
$pythonVersion | Set-Content -LiteralPath $artifactPythonInfo -Encoding ASCII

Push-Location $projectDir
try {
    $dotnetExePath = $DotNetExe
    if ($DotNetExe -eq "dotnet") {
        $dotnetExePath = Get-CommandPath "dotnet"
    }
    if ([string]::IsNullOrWhiteSpace($dotnetExePath)) {
        throw "dotnet executable not found."
    }

    $managedProjectMode = if (-not [string]::IsNullOrWhiteSpace($ManagedProjectMode)) {
        $ManagedProjectMode
    } elseif ($ProductionApplication) {
        "Production"
    } else {
        $AllocationMode
    }
    if ($C187GuestCandidate -and
        (-not $C185ExternalComposition -or -not $ProductionApplication -or
         $managedProjectMode -ne 'C167ProofComposite')) {
        throw 'C187 guest candidate requires C185 external composition and a production C167 proof composite.'
    }
    if ($C188ExternalAppIdentity -and
        ($C187GuestCandidate -or -not $C185ExternalComposition -or
         -not $ProductionApplication -or
         $managedProjectMode -notin @('C160Composite','C167ProofComposite'))) {
        throw 'C188 external identity requires the isolated C185 composition and a production C160 or C167 composite; it cannot share the C187 selector-4 candidate profile.'
    }
    if ($C188R3NotesStartupDiagnostics -and
        (-not $C188ExternalAppIdentity -or -not $ProductionApplication -or
         $managedProjectMode -ne 'C160Composite' -or $C187GuestCandidate)) {
        throw 'C188R3 Notes startup diagnostics require the isolated C188 external-identity production C160 candidate.'
    }
    if ($C188R4NotesStartupOperationIsolation -and
        (-not $C188ExternalAppIdentity -or -not $ProductionApplication -or
         $managedProjectMode -ne 'C160Composite' -or $C187GuestCandidate)) {
        throw 'C188R4 Notes startup operation isolation requires the isolated C188 external-identity production C160 candidate.'
    }
    if ($C188R6NotesPostRenderStartup -and
        (-not $C188R3NotesStartupDiagnostics -or -not $C188R4NotesStartupOperationIsolation -or
         -not $C188ExternalAppIdentity -or -not $ProductionApplication -or
         $managedProjectMode -ne 'C160Composite' -or $C187GuestCandidate)) {
        throw 'C188R6 Notes post-render startup diagnostics require the isolated C188 external-identity production C160 candidate with R3 and R4 instrumentation.'
    }
    if ($C188R7NotesProofAuthorization -and
        (-not $C188ExternalAppIdentity -or -not $ProductionApplication -or
         $managedProjectMode -ne 'C160Composite' -or $C187GuestCandidate -or
         -not $C166OrdinaryBuild -or $C188R3NotesStartupDiagnostics -or
         $C188R4NotesStartupOperationIsolation -or
         $C188R6NotesPostRenderStartup)) {
        throw 'C188R7 proof-authorization diagnostics require an isolated ordinary C160 C188 candidate and cannot include the larger R3/R4/R6 marker sets.'
    }
    if ($C188R8NotesInputRouting -and
        (-not $C188ExternalAppIdentity -or -not $ProductionApplication -or
         $managedProjectMode -ne 'C160Composite' -or $C187GuestCandidate -or
         -not $C166OrdinaryBuild -or -not $C188R7NotesProofAuthorization -or
         $C188R3NotesStartupDiagnostics -or
         $C188R4NotesStartupOperationIsolation -or
         $C188R6NotesPostRenderStartup)) {
        throw 'C188R8 input diagnostics require the isolated ordinary C160 C188 candidate with R7 startup authorization and cannot include the larger R3/R4/R6 marker sets.'
    }
    if ($C188R9RegistryInputReentry -and
        (-not $C188R8NotesInputRouting -or
         -not $C188ExternalAppIdentity -or -not $ProductionApplication -or
         $managedProjectMode -ne 'C160Composite' -or $C187GuestCandidate -or
         -not $C166OrdinaryBuild -or $C188R3NotesStartupDiagnostics -or
         $C188R4NotesStartupOperationIsolation -or
         $C188R6NotesPostRenderStartup)) {
        throw 'C188R9 registry diagnostics require the isolated ordinary C160 C188 candidate with R7/R8 routing instrumentation and cannot include the larger R3/R4/R6 marker sets.'
    }
    $publishProperties = @(
        "-p:HostLogProofRuntimeSupportObj=$runtimeSupportObj",
        "-p:HostLogProofMapPath=$artifactMap",
        "-p:HostLogProofMode=$managedProjectMode",
        "-p:BaseOutputPath=$binRoot\",
        "-p:BaseIntermediateOutputPath=$objRoot\"
    )
    if (-not [string]::IsNullOrWhiteSpace($DirectoryBuildPropsPath)) {
        $DirectoryBuildPropsPath = [System.IO.Path]::GetFullPath($DirectoryBuildPropsPath)
        Assert-WithinRoot $DirectoryBuildPropsPath (Join-Path $RepoRoot 'out\dotnet') 'Directory.Build.props'
        $publishProperties += "-p:DirectoryBuildPropsPath=$DirectoryBuildPropsPath"
    }
    if ($C185ExternalComposition) {
        $publishProperties += '-p:HostLogProofC185Boundary=true'
    }
    if ($C187GuestCandidate) {
        $publishProperties += '-p:HostLogProofC187GuestCandidate=true'
        $publishProperties += "-p:C187BuildScope=nativeaot"
        $publishProperties += "-p:C187HostOutputRoot=$OutputRoot"
    }
    if ($C188ExternalAppIdentity) {
        $publishProperties += '-p:HostLogProofC188ExternalAppIdentity=true'
        $publishProperties += "-p:C188BuildScope=nativeaot"
        $publishProperties += "-p:C188HostOutputRoot=$OutputRoot"
    }
    if ($C188R3NotesStartupDiagnostics) {
        $publishProperties += '-p:HostLogProofC188R3NotesStartupDiagnostics=true'
    }
    if ($C188R4NotesStartupOperationIsolation) {
        $publishProperties += '-p:HostLogProofC188R3NotesStartupDiagnostics=true'
        $publishProperties += '-p:HostLogProofC188R4NotesStartupOperationIsolation=true'
    }
    if ($C188R6NotesPostRenderStartup) {
        $publishProperties += '-p:HostLogProofC188R6NotesPostRenderStartup=true'
    }
    if ($C188R7NotesProofAuthorization) {
        $publishProperties += '-p:HostLogProofC188R7NotesProofAuthorization=true'
    }
    if ($C188R8NotesInputRouting) {
        $publishProperties += '-p:HostLogProofC188R8NotesInputRouting=true'
    }
    if ($C188R9RegistryInputReentry) {
        $publishProperties += '-p:HostLogProofC188R9RegistryInputReentry=true'
    }
    if ($managedProjectMode -eq "C167ProofComposite") {
        $publishProperties += "-p:HostLogProofC167UiFocusedBuild=true"
    }
    if ($C166OrdinaryBuild) {
        if ($managedProjectMode -ne "C160Composite" -or -not $ProductionApplication) {
            throw "C166 ordinary build requires a ProductionApplication C160Composite build."
        }
        $publishProperties += "-p:HostLogProofC166OrdinaryBuild=true"
    }
    if ($UseGuideXosRuntimePack) {
        $publishProperties += "-p:HostLogProofRuntimePackObj=$runtimePackObject"
        $publishProperties += "-p:IlcSdkPath=$runtimePackSdkPath\"
    }
    if ($ProductionApplication) {
        $publishProperties += "-p:HostLogProofPalContractObj=$palContractObj"
        $publishProperties += "-p:HostLogProofGcStartupContractObj=$gcStartupContractObj"
        $publishProperties += "-p:HostLogProofC102StartupObj=$c102StartupObj"
        $publishProperties += "-p:HostLogProofPalMinWinObj=$palMinWinObj"
        $publishProperties += "-p:HostLogProofNativeImportObj=$startupImportsObj"
    }
    if ($ThreadStaticLifecycleDiagnostics) {
        $publishProperties += "-p:HostLogProofManagedHostShimsObj=$managedHostShimsObj"
    }
    if ($IncludeC134FocusedTests) {
        $publishProperties += "-p:HostLogProofC134IncludeFocusedTests=true"
    }
    if ($IncludeC135FocusedTests) {
        $publishProperties += "-p:HostLogProofC135IncludeFocusedTests=true"
    }
    if ($IncludeC136FocusedTests) {
        $publishProperties += "-p:HostLogProofC136IncludeFocusedTests=true"
    }
    if ($C155ManagedNotesSession) {
        $publishProperties += "-p:HostLogProofC155ManagedNotesSession=true"
    }
    if ($C156ControlModifierShortcuts) {
        $publishProperties += "-p:HostLogProofC156ControlModifierShortcuts=true"
    }
    if ($C157ManagedNotesNewDocument) {
        $publishProperties += "-p:HostLogProofC157ManagedNotesNewDocument=true"
    }
    if ($C158ManagedCalculator) {
        if (-not $C155ManagedNotesSession -or -not $C156ControlModifierShortcuts -or -not $C157ManagedNotesNewDocument) {
            throw "C158 Managed Calculator requires the C155 session, C156 shortcut, and C157 Notes features in the composite."
        }
        $publishProperties += "-p:HostLogProofC158ManagedCalculator=true"
    }
    if ($C160ApplicationSnapshotProof) {
        if ($managedProjectMode -ne "C160Composite" -or -not $ProductionApplication) {
            throw "C160 snapshot proof requires a ProductionApplication C160Composite build."
        }
        $publishProperties += "-p:HostLogProofC160ApplicationSnapshotProof=true"
    }
    if ($C161ManagedTaskManager) {
        $publishProperties += "-p:HostLogProofC161ManagedTaskManager=true"
    }
    if ($C161TaskManagerProof) {
        if ($managedProjectMode -notin @("C160Composite", "C167ProofComposite") -or -not $ProductionApplication -or
            -not $C161ManagedTaskManager) {
            throw "C161 Task Manager proof requires a production C160 or C167 proof composite with the C161 app enabled."
        }
        $publishProperties += "-p:HostLogProofC161TaskManagerProof=true"
    }
    if ($C162ManagedTaskManagerClose) {
        if ($managedProjectMode -notin @("C160Composite", "C167ProofComposite") -or -not $ProductionApplication -or
            -not $C161ManagedTaskManager) {
            throw "C162 Managed Task Manager Close requires a production C160 or C167 proof composite with the C161 Task Manager."
        }
        $publishProperties += "-p:HostLogProofC162ManagedTaskManagerClose=true"
    }
    if ($C162TaskManagerCloseProof) {
        if (-not $C162ManagedTaskManagerClose -or -not $C161TaskManagerProof) {
            throw "C162 close proof requires the close feature and C161 proof instrumentation."
        }
        $publishProperties += "-p:HostLogProofC162TaskManagerCloseProof=true"
    }
    if ($C163ManagedFileExplorer) {
        if ($managedProjectMode -notin @("C160Composite", "C167ProofComposite") -or -not $ProductionApplication -or
            -not $C162ManagedTaskManagerClose) {
            throw "C163 Managed File Explorer requires a production C160 or C167 proof composite, C162 close lineage, and C161 Task Manager."
        }
        $publishProperties += "-p:HostLogProofC163ManagedFileExplorer=true"
    }
    if ($C163FileExplorerProof) {
        if (-not $C163ManagedFileExplorer) {
            throw "C163 focused proof requires the Managed File Explorer app."
        }
        $publishProperties += "-p:HostLogProofC163FileExplorerProof=true"
    }
    if ($C163FileExplorerDiagnostics) {
        if (-not $C163ManagedFileExplorer) {
            throw "C163 state diagnostics require the Managed File Explorer app."
        }
        $publishProperties += "-p:HostLogProofC163FileExplorerDiagnostics=true"
    }
    if ($C173ManagedFileExplorerResize) {
        if (-not $ProductionApplication -or
            $managedProjectMode -notin @("C160Composite", "C167ProofComposite") -or
            -not $C163ManagedFileExplorer -or -not $C163FileExplorerDiagnostics) {
            throw "C173 focused proof requires a production C160 or C167 composite with the C163 app and state diagnostics."
        }
        $publishProperties += "-p:HostLogProofC173IncludeFocusedTests=true"
    }
    if ($C175ManagedNotesResize) {
        if (-not $ProofControl -or -not $ProductionApplication -or
            $managedProjectMode -notin @("C160Composite", "C167ProofComposite")) {
            throw "C175 focused proof requires a proof-controlled production C160 or C167 composite."
        }
        $publishProperties += "-p:HostLogProofC175IncludeFocusedTests=true"
        $publishProperties += "-p:HostLogProofC176IncludeFocusedTests=true"
        if ($C177ManagedNotesNewlineSave) {
            $publishProperties += "-p:HostLogProofC177IncludeFocusedTests=true"
        }
    }
    if ($C178ManagedCalculatorResize) {
        if (-not $ProductionApplication -or
            $managedProjectMode -ne 'C167ProofComposite') {
            throw 'C178 focused proof requires a production C167 proof composite.'
        }
        $publishProperties += "-p:HostLogProofC178IncludeFocusedTests=true"
    }
    if ($C179ManagedTaskManagerResize) {
        if (-not $ProductionApplication -or
            $managedProjectMode -ne 'C167ProofComposite') {
            throw 'C179 focused proof requires a production C167 proof composite.'
        }
        $publishProperties += "-p:HostLogProofC179IncludeFocusedTests=true"
    }
    if ($C164FileActivationProof) {
        $publishProperties += "-p:HostLogProofC164FileActivationProof=true"
    }
    if ($C164FileActivationDiagnostics) {
        if (-not $C163ManagedFileExplorer) {
            throw "C164 activation diagnostics require the Managed File Explorer app."
        }
        $publishProperties += "-p:HostLogProofC164FileActivationDiagnostics=true"
    }
    if ($C166FileAssociationProof) {
        $publishProperties += "-p:HostLogProofC166FileAssociationProof=true"
    }
    if ($C167DefaultAppsProof) {
        $publishProperties += "-p:HostLogProofC167DefaultAppsProof=true"
    }
    if ($ProofControl) {
        $publishProperties += "-p:HostLogProofProofControl=true"
    }
    $publishBatch = @(
        "@echo off"
        "setlocal"
        "call `"$vcvars64`" >nul"
        "if errorlevel 1 exit /b %errorlevel%"
        "where link.exe"
        "where cl.exe"
        "cl.exe /nologo /TC /c /GS- /Zl /Fo:`"$runtimeSupportObj`" `"$runtimeSupportSource`""
        "if errorlevel 1 exit /b %errorlevel%"
        $(if ($ProductionApplication) { "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /DWIN32 /D_WIN32 /D_WIN64 /DHOST_WINDOWS /DTARGET_WINDOWS /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DGUIDEXOS_NATIVEAOT_RUNTIME_STARTUP /I`"$(Join-Path $RuntimePackRoot 'src\platform')`" /Fo:`"$palMinWinObj`" `"$palMinWinSource`""; "if errorlevel 1 exit /b %errorlevel%"; "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /I`"$(Join-Path $RuntimePackRoot 'src\platform')`" /Fo:`"$palContractObj`" `"$palContractSource`""; "if errorlevel 1 exit /b %errorlevel%"; "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /I`"$(Join-Path $RuntimePackRoot 'src\platform')`" /Fo:`"$gcStartupContractObj`" `"$gcStartupContractSource`""; "if errorlevel 1 exit /b %errorlevel%" } else { $null })
        $(if ($ThreadStaticLifecycleDiagnostics) { "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro /Fo:`"$managedHostShimsObj`" `"$managedHostShimsSource`""; "if errorlevel 1 exit /b %errorlevel%" } else { $null })
        $(if ($ProductionApplication) { "cl.exe /nologo /TC /c /GS- /Zl /Fo:`"$c102StartupObj`" `"$c102StartupSource`""; "if errorlevel 1 exit /b %errorlevel%" } else { $null })
        "`"$dotnetExePath`" publish `"$projectFile`" -c Release -r win-x64 --self-contained true $(if ($ProjectArtifactIsolation -or $C187GuestCandidate -or $C188ExternalAppIdentity) { '--artifacts-path `"' + (Join-Path $OutputRoot 'project-artifacts') + '`"' }) -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcGenerateStackTraceData=false -p:IlcUseEnvironmentalTools=true $($publishProperties -join ' ')"
        "exit /b %errorlevel%"
    )
    $publishBatch | Set-Content -LiteralPath $buildBatch -Encoding ASCII
    $buildBatchLog = Join-Path $artifactRoot 'build-native-hostlog.log'
    & $buildBatch *>&1 | Tee-Object -FilePath $buildBatchLog
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet publish failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

if ([string]::IsNullOrWhiteSpace($publishExe)) {
    $publishExe = Get-ChildItem -Path $binRoot -Recurse -Filter HostLogProof.exe -File -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if ([string]::IsNullOrWhiteSpace($publishExe) -or -not (Test-Path -LiteralPath $publishExe)) {
    throw "Published native executable not found under: $binRoot"
}

Copy-Item -LiteralPath $publishExe -Destination $artifactExe -Force
if ([string]::IsNullOrWhiteSpace($publishMap)) {
    $publishMap = Get-ChildItem -Path $binRoot -Recurse -Filter HostLogProof.map -File -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not [string]::IsNullOrWhiteSpace($publishMap) -and (Test-Path -LiteralPath $publishMap)) {
    Copy-Item -LiteralPath $publishMap -Destination $artifactMap -Force
}

$mapArg = @()
if (Test-Path -LiteralPath $artifactMap) {
    $mapSymbol = if ($ProductionApplication) { "GuideXosNativeAotApplicationEntry" } else { "ManagedMain" }
    $mapArg = @("--map", $artifactMap, "--symbol", $mapSymbol)
}

& $PythonExe $PeToElfScript $artifactExe $artifactElf @mapArg
if ($LASTEXITCODE -ne 0) {
    throw "PE-to-ELF conversion failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $artifactElf)) {
    throw "ELF output not found: $artifactElf"
}
if ($C173ManagedFileExplorerResize -and $ProofControl) {
    $artifactText = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes($artifactElf))
    foreach ($requiredMarker in @('C172-MANAGED-GEOMETRY', 'C173-MANAGED-GEOMETRY')) {
        if (-not $artifactText.Contains($requiredMarker)) {
            throw "C173 focused composite is missing managed proof-control marker '$requiredMarker'."
        }
    }
    if ($C175ManagedNotesResize -and -not $artifactText.Contains('C175-MANAGED-GEOMETRY')) {
        throw "C175 focused composite is missing managed proof-control marker 'C175-MANAGED-GEOMETRY'."
    }
    if ($C175ManagedNotesResize -and -not $artifactText.Contains('C176-NOTES-NAVIGATION')) {
        throw "C176 focused composite is missing managed navigation marker 'C176-NOTES-NAVIGATION'."
    }
    if ($C177ManagedNotesNewlineSave) {
        foreach ($requiredMarker in @('C177-NOTES-NEWLINE', 'C177-SAVE-PROOF')) {
            if (-not $artifactText.Contains($requiredMarker)) {
                throw "C177 focused composite is missing required newline/save marker '$requiredMarker'."
            }
        }
    }
    if ($C178ManagedCalculatorResize) {
        foreach ($requiredMarker in @('C178-CALCULATOR-LAUNCH', 'C178-CALC-GEOMETRY')) {
            if (-not $artifactText.Contains($requiredMarker)) {
                throw "C178 focused composite is missing required Calculator marker '$requiredMarker'."
            }
        }
    }
    if ($C179ManagedTaskManagerResize) {
        foreach ($requiredMarker in @('C179-TM-LAUNCH', 'C179-TM-GEOMETRY')) {
            if (-not $artifactText.Contains($requiredMarker)) {
                throw "C179 focused composite is missing required Task Manager marker '$requiredMarker'."
            }
        }
    }
}

if ([string]::IsNullOrWhiteSpace($nativeHostLogObj)) {
    $nativeHostLogObj = Get-ChildItem -Path $objRoot -Recurse -Filter HostLogProof.obj -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\native\\HostLogProof\.obj$' } |
        Select-Object -First 1 -ExpandProperty FullName
}
if ([string]::IsNullOrWhiteSpace($nativeHostLogObj) -or -not (Test-Path -LiteralPath $nativeHostLogObj)) {
    throw "Native HostLogProof object not found under: $objRoot"
}

if ([string]::IsNullOrWhiteSpace($publishIlcRsp)) {
    $publishIlcRsp = Get-ChildItem -Path $objRoot -Recurse -Filter HostLogProof.ilc.rsp -File -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not [string]::IsNullOrWhiteSpace($publishIlcRsp) -and (Test-Path -LiteralPath $publishIlcRsp)) {
    Copy-Item -LiteralPath $publishIlcRsp -Destination $artifactIlcRsp -Force
}

if ([string]::IsNullOrWhiteSpace($publishLinkRsp)) {
    $publishLinkRsp = Get-ChildItem -Path $objRoot -Recurse -Filter link.rsp -File -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not [string]::IsNullOrWhiteSpace($publishLinkRsp) -and (Test-Path -LiteralPath $publishLinkRsp)) {
    Copy-Item -LiteralPath $publishLinkRsp -Destination $artifactLinkRsp -Force
}

& $objdumpExe -p $artifactExe | Set-Content -LiteralPath $artifactPeDump -Encoding ASCII
& $objdumpExe -d $nativeHostLogObj | Set-Content -LiteralPath $artifactNativeObjDump -Encoding ASCII
& $objdumpExe -r $nativeHostLogObj | Set-Content -LiteralPath $artifactNativeObjReloc -Encoding ASCII
if ($UseGuideXosRuntimePack) {
    & $objdumpExe -d $runtimePackObject | Set-Content -LiteralPath $artifactRuntimePackObjDump -Encoding ASCII
    & $objdumpExe -r $runtimePackObject | Set-Content -LiteralPath $artifactRuntimePackObjReloc -Encoding ASCII
}
& $objdumpExe -p -d $artifactElf | Set-Content -LiteralPath $artifactElfDump -Encoding ASCII
& $readelfExe -h -l -S -r -s -d $artifactElf | Set-Content -LiteralPath $artifactElfReadelf -Encoding ASCII
& $objdumpExe -d $artifactElf | Set-Content -LiteralPath $artifactElfDisasm -Encoding ASCII

$actualImports = Get-ManagedHostLogImportTable $artifactPeDump
if ($UseGuideXosRuntimePack) {
    $liveFlsImports = @()
    foreach ($dll in $actualImports.Keys) {
        $liveFlsImports += @($actualImports[$dll] | Where-Object { $_ -in @("FlsGetValue", "FlsSetValue") })
    }
    if ($liveFlsImports.Count -ne 0) {
        throw "GuideXOS runtime-pack image still imports stock FLS entry points: $($liveFlsImports -join ', ')"
    }
    Assert-ManagedHostLogFileContains $artifactMap @('guidexos_nativeaot_platform\.obj', 'RhpReversePInvoke', 'RhpReversePInvokeReturn') "GuideXOS runtime-pack map evidence"
} else {
    $expectedPeImports = Get-ManagedHostLogExpectedPeImports
    Assert-ManagedHostLogSetEquals -Actual @($actualImports.Keys) -Expected @($expectedPeImports.Keys) -Label "PE import DLL set"
    foreach ($dll in $expectedPeImports.Keys) {
        Assert-ManagedHostLogSetEquals -Actual @($actualImports[$dll]) -Expected @($expectedPeImports[$dll]) -Label "PE imports for $dll"
    }
}
Assert-ManagedHostLogFileNotContains $artifactPeDump @('ucrtbase\.dll', 'msvcrt\.dll', 'ntdll\.dll') "Intermediate PE forbidden imports"
Assert-ManagedHostLogElfEnvelope -ElfPath $artifactElf -PePath $artifactExe -PeDumpPath $artifactPeDump -MapPath $artifactMap -NativeObjectDumpPath $artifactNativeObjDump -ElfReadelfPath $artifactElfReadelf -ElfDumpPath $artifactElfDump -RuntimeSupportSourcePath $runtimeSupportSource -GuideXosRuntimePack:$UseGuideXosRuntimePack -ManagedAllocation:($AllocationMode -in @("Allocating", "Repeated") -or $ProductionApplication) -RepeatedAllocation:($AllocationMode -eq "Repeated") -ProductionApplication:$ProductionApplication | Out-Null

Write-Host "Managed host-log proof built successfully." -ForegroundColor Green
Write-Host "Output root: $OutputRoot" -ForegroundColor Cyan
Write-Host "ELF artifact: $artifactElf" -ForegroundColor Cyan

