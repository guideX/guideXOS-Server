param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$LegacyRoot = $env:GUIDEXOS_LEGACY_ROOT,
    [string]$DotNetExe = "dotnet",
    [string]$PythonExe = "",
    [string]$PeToElfScript = "",
    [string]$OutputRoot = "",
    [string]$RuntimePackRoot = "",
    [switch]$UseGuideXosRuntimePack,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "managed-hostlog-artifact-assertions.ps1")

function Resolve-AbsolutePath([string]$Path) {
    return (Resolve-Path -LiteralPath $Path).Path
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
    $runtimePackOutputRoot = Join-Path $RepoRoot "out\dotnet\runtime-pack"
    $runtimePackBuildArguments = @(
        "-RepoRoot", $RepoRoot,
        "-RuntimePackRoot", $RuntimePackRoot,
        "-OutputRoot", $runtimePackOutputRoot
    )
    if ($Clean) { $runtimePackBuildArguments += "-Clean" }
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
    $runtimePackObjectHash = Get-FileHash -LiteralPath $runtimePackObject -Algorithm SHA256 | Select-Object -ExpandProperty Hash
    if ($runtimePackObjectHash.ToUpperInvariant() -ne [string]$runtimePackManifestObject.objectSha256.ToUpperInvariant()) {
        throw "GuideXOS runtime-pack object hash does not match its manifest."
    }
    $runtimePackSdkPath = [string]$runtimePackManifestObject.sdkPath
    if ([string]::IsNullOrWhiteSpace($runtimePackSdkPath) -or -not (Test-Path -LiteralPath $runtimePackSdkPath)) {
        throw "GuideXOS runtime-pack SDK path is missing: $runtimePackSdkPath"
    }
    $runtimePackManifestHash = (Get-FileHash -LiteralPath $runtimePackManifest -Algorithm SHA256).Hash.ToUpperInvariant()
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

$expectedPeToElfSha256 = "EAAEFBC8862D6E1A4AC1A679073AF5311A3AF9CE96F45D258617BD4FE0977434"
$actualPeToElfSha256 = (Get-FileHash -LiteralPath $PeToElfScript -Algorithm SHA256).Hash.ToUpperInvariant()
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
    "UseGuideXosRuntimePack=$UseGuideXosRuntimePack"
    "RuntimePackRoot=$RuntimePackRoot"
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

    $publishProperties = @(
        "-p:HostLogProofRuntimeSupportObj=$runtimeSupportObj",
        "-p:HostLogProofMapPath=$artifactMap",
        "-p:BaseOutputPath=$binRoot\",
        "-p:BaseIntermediateOutputPath=$objRoot\"
    )
    if ($UseGuideXosRuntimePack) {
        $publishProperties += "-p:HostLogProofRuntimePackObj=$runtimePackObject"
        $publishProperties += "-p:IlcSdkPath=$runtimePackSdkPath\"
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
        "`"$dotnetExePath`" publish `"$projectFile`" -c Release -r win-x64 --self-contained true -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcGenerateStackTraceData=false -p:IlcUseEnvironmentalTools=true $($publishProperties -join ' ')"
        "exit /b %errorlevel%"
    )
    $publishBatch | Set-Content -LiteralPath $buildBatch -Encoding ASCII
    & $buildBatch
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
    $mapArg = @("--map", $artifactMap, "--symbol", "ManagedMain")
}

& $PythonExe $PeToElfScript $artifactExe $artifactElf @mapArg
if ($LASTEXITCODE -ne 0) {
    throw "PE-to-ELF conversion failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $artifactElf)) {
    throw "ELF output not found: $artifactElf"
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
Assert-ManagedHostLogElfEnvelope -ElfPath $artifactElf -PePath $artifactExe -PeDumpPath $artifactPeDump -MapPath $artifactMap -NativeObjectDumpPath $artifactNativeObjDump -ElfReadelfPath $artifactElfReadelf -ElfDumpPath $artifactElfDump -RuntimeSupportSourcePath $runtimeSupportSource -GuideXosRuntimePack:$UseGuideXosRuntimePack | Out-Null

Write-Host "Managed host-log proof built successfully." -ForegroundColor Green
Write-Host "Output root: $OutputRoot" -ForegroundColor Cyan
Write-Host "ELF artifact: $artifactElf" -ForegroundColor Cyan

