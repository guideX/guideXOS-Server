[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$OutputRoot = '',
    [string]$DirectoryBuildPropsPath = '',
    [switch]$PreflightOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$isolationRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet\c186r1-artifact-isolation'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $isolationRoot 'nativeaot'
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$isolationPrefix = $isolationRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $OutputRoot.StartsWith($isolationPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "C186R1 NativeAOT output must be below $isolationRoot; rejected: $OutputRoot"
}

$protectedRoots = @(
    (Join-Path $RepoRoot 'out\dotnet\c185-managed-host-services'),
    (Join-Path $RepoRoot 'out\dotnet\c186-managed-host-contract'),
    (Join-Path $RepoRoot 'out\dotnet\c182-managed-application-authoring'),
    (Join-Path $RepoRoot 'out\dotnet\c181-managed-application-residency'),
    (Join-Path $RepoRoot 'samples\host\ManagedHostServicesC185\bin'),
    (Join-Path $RepoRoot 'samples\host\ManagedHostServicesC185\obj'),
    (Join-Path $RepoRoot 'samples\host\ManagedStarterC182\obj'),
    (Join-Path $RepoRoot 'samples\host\ManagedUiRegression\obj'),
    (Join-Path $RepoRoot 'samples\managed\C185CounterApp\bin'),
    (Join-Path $RepoRoot 'samples\managed\C185CounterApp\obj'),
    (Join-Path $RepoRoot 'samples\managed\HostLogProof\c182-host-smoke'),
    (Join-Path $RepoRoot 'samples\managed\ManagedUiContracts\bin'),
    (Join-Path $RepoRoot 'samples\managed\ManagedUiContracts\obj'),
    (Join-Path $RepoRoot 'ESP\ramdisk.img')
)
foreach ($protectedRoot in $protectedRoots) {
    $protectedPath = [System.IO.Path]::GetFullPath($protectedRoot).TrimEnd('\', '/')
    $outputWithSeparator = $OutputRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $protectedWithSeparator = $protectedPath + [System.IO.Path]::DirectorySeparatorChar
    if ($OutputRoot.Equals($protectedPath, [StringComparison]::OrdinalIgnoreCase) -or
        $OutputRoot.StartsWith($protectedWithSeparator, [StringComparison]::OrdinalIgnoreCase) -or
        $protectedPath.StartsWith($outputWithSeparator, [StringComparison]::OrdinalIgnoreCase)) {
        throw "C186R1 NativeAOT output overlaps protected content: $protectedPath"
    }
}

$walk = [System.IO.DirectoryInfo]$OutputRoot
while ($null -ne $walk) {
    if (Test-Path -LiteralPath $walk.FullName) {
        $item = Get-Item -LiteralPath $walk.FullName -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "C186R1 NativeAOT output traverses a reparse point: $($walk.FullName)"
        }
    }
    if ($walk.FullName.Equals($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) { break }
    $walk = $walk.Parent
}

if ([string]::IsNullOrWhiteSpace($DirectoryBuildPropsPath)) {
    $DirectoryBuildPropsPath = Join-Path $isolationRoot 'msbuild\Directory.Build.props'
}
$DirectoryBuildPropsPath = [System.IO.Path]::GetFullPath($DirectoryBuildPropsPath)
if (-not $DirectoryBuildPropsPath.StartsWith($isolationPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "C186R1 MSBuild props must be below $isolationRoot; rejected: $DirectoryBuildPropsPath"
}
if ($PreflightOnly) {
    Write-Output "C186R1-OUTPUT-PREFLIGHT root=$OutputRoot props=$DirectoryBuildPropsPath result=PASS"
    exit 0
}

$nativeOutput = Join-Path $OutputRoot 'build'
$hostBin = Join-Path $nativeOutput 'host-bin'
$hostObj = Join-Path $nativeOutput 'host-obj'
$tempDirectory = Join-Path $OutputRoot 'temp'
$propsDirectory = Split-Path -Parent $DirectoryBuildPropsPath
$propsWalk = [System.IO.DirectoryInfo]$propsDirectory
while ($null -ne $propsWalk) {
    if (Test-Path -LiteralPath $propsWalk.FullName) {
        $item = Get-Item -LiteralPath $propsWalk.FullName -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "C186R1 MSBuild props destination traverses a reparse point: $($propsWalk.FullName)"
        }
    }
    if ($propsWalk.FullName.Equals($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) { break }
    $propsWalk = $propsWalk.Parent
}
if (Test-Path -LiteralPath $DirectoryBuildPropsPath -PathType Leaf) {
    $propsFile = Get-Item -LiteralPath $DirectoryBuildPropsPath -Force
    if (($propsFile.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "C186R1 MSBuild props file is a reparse point: $DirectoryBuildPropsPath"
    }
}
New-Item -ItemType Directory -Force -Path $OutputRoot, $nativeOutput, $hostBin, $hostObj, $tempDirectory, $propsDirectory | Out-Null
$env:TEMP = $tempDirectory
$env:TMP = $tempDirectory

function ConvertTo-XmlText([string]$Value) {
    return [System.Security.SecurityElement]::Escape($Value)
}

$hostBinXml = ConvertTo-XmlText (($hostBin.TrimEnd('\', '/') + '\$(C186R1BuildScope)\'))
$hostObjXml = ConvertTo-XmlText (($hostObj.TrimEnd('\', '/') + '\$(C186R1BuildScope)\'))
$propsContent = @"
<Project>
  <PropertyGroup>
    <ResolveNuGetPackages>false</ResolveNuGetPackages>
    <C186R1BuildScope Condition="'`$(C186R1BuildScope)' == ''">nativeaot</C186R1BuildScope>
  </PropertyGroup>
  <PropertyGroup Condition="'`$(MSBuildProjectName)' == 'HostLogProof'">
    <BaseOutputPath>$hostBinXml</BaseOutputPath>
    <BaseIntermediateOutputPath>$hostObjXml</BaseIntermediateOutputPath>
    <MSBuildProjectExtensionsPath>$hostObjXml</MSBuildProjectExtensionsPath>
  </PropertyGroup>
</Project>
"@
Set-Content -LiteralPath $DirectoryBuildPropsPath -Value $propsContent -Encoding utf8

$vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    throw "Required Visual C++ environment script is missing: $vcvars"
}
$dotnet = Get-Command dotnet.exe -ErrorAction Stop | Select-Object -First 1
$python = Get-Command python.exe -ErrorAction Stop | Select-Object -First 1
$readelf = Get-Command readelf.exe -ErrorAction Stop | Select-Object -First 1

$supportSource = Join-Path $RepoRoot 'samples\managed\HostLogProof\runtime_support.c'
$supportObject = Join-Path $OutputRoot 'runtime_support.obj'
$project = Join-Path $RepoRoot 'samples\managed\HostLogProof\HostLogProof.csproj'
$artifacts = Join-Path $OutputRoot 'artifacts'
$publish = Join-Path $OutputRoot 'publish'
$map = Join-Path $OutputRoot 'HostLogProof.map'
$pe = Join-Path $publish 'HostLogProof.exe'
$elf = Join-Path $OutputRoot 'HostLogProof.elf'
$converter = Join-Path $RepoRoot 'tools\dotnet\pe_to_elf_v2_fixed_base.py'
$buildCommand = Join-Path $OutputRoot 'build-c186r1-nativeaot.cmd'
$buildLog = Join-Path $OutputRoot 'nativeaot-build.log'

$cmdContent = @"
@echo off
setlocal
set "TEMP=$tempDirectory"
set "TMP=$tempDirectory"
call "$vcvars" >nul
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /TC /c /GS- /Zl /Fo:"$supportObject" "$supportSource"
if errorlevel 1 exit /b %errorlevel%
"$($dotnet.Source)" publish "$project" --configuration Release --runtime win-x64 --self-contained true --artifacts-path "$artifacts" --output "$publish" -p:PublishAot=true -p:InvariantGlobalization=true -p:IlcUseEnvironmentalTools=true -p:HostLogProofC185Boundary=true -p:HostLogProofMode=NonAllocating -p:C186R1BuildScope=nativeaot -p:DirectoryBuildPropsPath="$DirectoryBuildPropsPath" -p:HostLogProofRuntimeSupportObj="$supportObject" -p:HostLogProofMapPath="$map"
exit /b %errorlevel%
"@
Set-Content -LiteralPath $buildCommand -Value $cmdContent -Encoding ascii
$buildOutput = @(& $env:ComSpec /d /c "`"$buildCommand`"" 2>&1)
$buildExitCode = $LASTEXITCODE
$buildOutputText = $buildOutput | Out-String
Set-Content -LiteralPath $buildLog -Value $buildOutputText -Encoding utf8
if ($buildExitCode -ne 0) {
    throw "Isolated NativeAOT publish failed with exit code $buildExitCode; see $buildLog"
}

foreach ($requiredPath in @($pe, $map, $supportObject)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "NativeAOT stage did not produce its required artifact: $requiredPath"
    }
    $fullRequiredPath = [System.IO.Path]::GetFullPath($requiredPath)
    if (-not $fullRequiredPath.StartsWith($OutputRoot.TrimEnd('\', '/') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "NativeAOT artifact escaped its output root: $fullRequiredPath"
    }
}

$peBytes = [System.IO.File]::ReadAllBytes($pe)
if ($peBytes.Length -lt 0x40 -or $peBytes[0] -ne 0x4d -or $peBytes[1] -ne 0x5a) {
    throw "NativeAOT publish output is not a PE image: $pe"
}
$peOffset = [BitConverter]::ToInt32($peBytes, 0x3c)
if ($peOffset -lt 0 -or $peOffset + 26 -gt $peBytes.Length -or
    $peBytes[$peOffset] -ne 0x50 -or $peBytes[$peOffset + 1] -ne 0x45 -or
    [BitConverter]::ToUInt16($peBytes, $peOffset + 4) -ne 0x8664 -or
    [BitConverter]::ToUInt16($peBytes, $peOffset + 24) -ne 0x20b) {
    throw "NativeAOT publish output is not an AMD64 PE32+ image: $pe"
}

$mapText = Get-Content -LiteralPath $map -Raw
$retainedSymbols = @(
    'CounterApplicationFactory__Create',
    'CounterApplication___ctor',
    'CounterApplication__HandleAction',
    'CounterApplication__HandleInput',
    'CounterApplication__HandleResize',
    'CounterApplication__OnTearingDown',
    'ManagedMain'
)
$retentionRows = foreach ($symbol in $retainedSymbols) {
    $line = ($mapText -split "`r?`n" | Where-Object { $_ -match [regex]::Escape($symbol) } | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($line)) {
        throw "NativeAOT link map does not retain required C185 composition symbol: $symbol"
    }
    [pscustomobject]@{ Symbol = $symbol; MapLine = $line.Trim() }
}
$retentionPath = Join-Path $OutputRoot 'retained-code-map-evidence.txt'
@($retentionRows | ForEach-Object { "$($_.Symbol): $($_.MapLine)" }) |
    Set-Content -LiteralPath $retentionPath -Encoding utf8

$conversionLog = Join-Path $OutputRoot 'pe-to-elf.log'
$conversionOutput = @(& $python.Source $converter $pe $elf --map $map --symbol ManagedMain 2>&1)
$conversionExitCode = $LASTEXITCODE
$conversionText = $conversionOutput | Out-String
Set-Content -LiteralPath $conversionLog -Value $conversionText -Encoding utf8
if ($conversionExitCode -ne 0 -or -not (Test-Path -LiteralPath $elf -PathType Leaf)) {
    throw "PE-to-ELF conversion failed with exit code $conversionExitCode; see $conversionLog"
}

$headerPath = Join-Path $OutputRoot 'elf-header.txt'
$programHeadersPath = Join-Path $OutputRoot 'elf-program-headers.txt'
$headerOutput = @(& $readelf.Source -h $elf 2>&1)
$headerExitCode = $LASTEXITCODE
$headerText = $headerOutput | Out-String
Set-Content -LiteralPath $headerPath -Value $headerText -Encoding utf8
$programOutput = @(& $readelf.Source -l $elf 2>&1)
$programExitCode = $LASTEXITCODE
$programText = $programOutput | Out-String
Set-Content -LiteralPath $programHeadersPath -Value $programText -Encoding utf8
$loadCount = [regex]::Matches($programText, '(?m)^\s*LOAD\s').Count
if ($headerExitCode -ne 0 -or $programExitCode -ne 0 -or
    $headerText -notmatch 'Class:\s+ELF64' -or
    $headerText -notmatch 'Type:\s+EXEC' -or
    $headerText -notmatch 'Machine:\s+Advanced Micro Devices X86-64' -or
    $headerText -notmatch 'Entry point address:\s+0x[0-9a-fA-F]+' -or
    $headerText -notmatch 'Number of section headers:\s+0' -or
    $loadCount -ne 7) {
    throw "ELF validation failed; inspect $headerPath and $programHeadersPath"
}

$outputFiles = @(Get-ChildItem -LiteralPath $OutputRoot -Recurse -File -Force | Sort-Object FullName)
$records = foreach ($file in $outputFiles) {
    $relative = $file.FullName.Substring($OutputRoot.TrimEnd('\', '/').Length + 1).Replace('\', '/')
    [pscustomobject]@{
        path = $relative
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToUpperInvariant()
    }
}
$manifest = [ordered]@{
    schemaVersion = 1
    phase = 'C186R1-nativeaot-artifact-isolation'
    outputRoot = $OutputRoot
    buildProject = $project
    configuration = 'Release'
    runtimeIdentifier = 'win-x64'
    nativeAot = $true
    outputRouting = [ordered]@{
        artifacts = $artifacts
        publish = $publish
        hostOutput = $hostBin
        hostIntermediate = $hostObj
        temporary = $tempDirectory
        runtimeSupportObject = $supportObject
        linkerMap = $map
        pe = $pe
        elf = $elf
    }
    elfValidation = [ordered]@{ class = 'ELF64'; type = 'EXEC'; machine = 'AMD x86-64'; loadSegments = $loadCount; sectionHeaders = 0 }
    retainedSymbols = @($retentionRows)
    buildLog = $buildLog
    conversionLog = $conversionLog
    files = @($records)
}
$manifestPath = Join-Path $OutputRoot 'nativeaot-artifact-manifest.json'
$manifestJson = $manifest | ConvertTo-Json -Depth 8
Set-Content -LiteralPath $manifestPath -Value $manifestJson -Encoding utf8
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
$hashLines = @($records | ForEach-Object { "$($_.sha256)  $($_.path)" })
$hashLines += "$manifestHash  nativeaot-artifact-manifest.json"
Set-Content -LiteralPath (Join-Path $OutputRoot 'artifact-sha256.txt') -Value $hashLines -Encoding ascii

Write-Output "C186R1-NATIVEAOT pe=$pe elf=$elf map=$map elfLoads=$loadCount retainedSymbols=$($retentionRows.Count) result=PASS"
