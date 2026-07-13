param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$OutputRoot = "",
    [string]$StageRoot = "",
    [string]$BuildScript = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

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

function Get-FileHashHex([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
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

function Get-MapSymbolAddress([string]$Path, [string]$Symbol, [string]$Label) {
    $symbolPattern = '^\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+' + [regex]::Escape($Symbol) + '\s+([0-9A-Fa-f]{16})\s+'
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match $symbolPattern) {
            return [Convert]::ToUInt64($Matches[1], 16)
        }
    }

    throw "$Label symbol not found in map: $Symbol"
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

$repoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "out\dotnet\managed-hostlog"
}
if ([string]::IsNullOrWhiteSpace($StageRoot)) {
    $StageRoot = Join-Path $OutputRoot "stage-managed-hostlog-proof"
}
if ([string]::IsNullOrWhiteSpace($BuildScript)) {
    $BuildScript = Join-Path $repoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"
}

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$StageRoot = [System.IO.Path]::GetFullPath($StageRoot)
$BuildScript = [System.IO.Path]::GetFullPath($BuildScript)

Assert-WithinRoot $OutputRoot $repoRoot "Output"
Assert-WithinRoot $StageRoot $repoRoot "Stage"

if (-not (Test-Path -LiteralPath $BuildScript)) {
    throw "Build script not found: $BuildScript"
}

if (Test-Path -LiteralPath $StageRoot) {
    Remove-Item -LiteralPath $StageRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $StageRoot | Out-Null

Write-Host "[dotnet-proof] rebuilding NativeAOT proof from a clean output root"
& powershell -ExecutionPolicy Bypass -File $BuildScript -RepoRoot $repoRoot -OutputRoot $OutputRoot -Clean
if ($LASTEXITCODE -ne 0) {
    throw "Managed proof build failed with exit code $LASTEXITCODE"
}

$artifactRoot = Join-Path $OutputRoot "artifacts"
$sourceElf = Join-Path $artifactRoot "HostLogProof.elf"
$sourceMap = Join-Path $artifactRoot "HostLogProof.map"
$sourcePeDump = Join-Path $artifactRoot "HostLogProof.pe.objdump.txt"
$sourceElfDump = Join-Path $artifactRoot "HostLogProof.elf.objdump.txt"
$sourceElfReadelf = Join-Path $artifactRoot "HostLogProof.elf.readelf.txt"
$sourceNativeObjDump = Join-Path $artifactRoot "HostLogProof.native.objdump.txt"
$sourceNativeObjReloc = Join-Path $artifactRoot "HostLogProof.native.reloc.txt"
$sourceToolchain = Join-Path $artifactRoot "toolchain.txt"
$sourceIlcRsp = Join-Path $artifactRoot "HostLogProof.ilc.rsp"
$sourceLinkRsp = Join-Path $artifactRoot "HostLogProof.link.rsp"

foreach ($path in @($sourceElf, $sourceMap, $sourcePeDump, $sourceElfDump, $sourceElfReadelf, $sourceNativeObjDump, $sourceNativeObjReloc, $sourceToolchain, $sourceIlcRsp, $sourceLinkRsp)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Expected proof artifact missing: $path"
    }
}

$tlsIndexAddress = Get-MapSymbolAddress -Path $sourceMap -Symbol "_tls_index" -Label "TLS index"
$tlsStartAddress = Get-MapSymbolAddress -Path $sourceMap -Symbol "_tls_start" -Label "TLS start"
$tlsEndAddress = Get-MapSymbolAddress -Path $sourceMap -Symbol "_tls_end" -Label "TLS end"
if ($tlsEndAddress -le $tlsStartAddress) {
    throw ([string]::Format("TLS template bounds are invalid: start=0x{0:X} end=0x{1:X}", $tlsStartAddress, $tlsEndAddress))
}
$tlsBlockSize = $tlsEndAddress - $tlsStartAddress

$expectedPeImports = [ordered]@{
    "ADVAPI32.dll" = @(
        "RegisterEventSourceW",
        "ReportEventW",
        "DeregisterEventSource"
    )
    "bcrypt.dll" = @(
        "BCryptGenRandom"
    )
    "KERNEL32.dll" = @(
        "CloseHandle",
        "CreateEventExW",
        "DuplicateHandle",
        "FormatMessageW",
        "GetConsoleOutputCP",
        "GetCurrentProcess",
        "GetCurrentProcessorNumberEx",
        "GetCurrentThread",
        "GetEnvironmentVariableW",
        "GetLastError",
        "GetModuleFileNameW",
        "GetStdHandle",
        "GetThreadPriority",
        "GetTickCount64",
        "IsDebuggerPresent",
        "LocalFree",
        "MultiByteToWideChar",
        "QueryPerformanceCounter",
        "QueryPerformanceFrequency",
        "RaiseFailFastException",
        "SetEvent",
        "SetLastError",
        "Sleep",
        "VirtualAlloc",
        "VirtualFree",
        "WaitForMultipleObjectsEx",
        "WideCharToMultiByte",
        "WriteFile",
        "RtlCaptureContext",
        "FlsGetValue",
        "FlsSetValue",
        "SwitchToThread",
        "GetCurrentThreadId",
        "VirtualQuery",
        "EnterCriticalSection",
        "LeaveCriticalSection"
    )
    "ole32.dll" = @(
        "CoGetApartmentType",
        "CoInitializeEx",
        "CoUninitialize",
        "CoWaitForMultipleHandles"
    )
    "api-ms-win-crt-heap-l1-1-0.dll" = @(
        "free",
        "_callnewh",
        "malloc"
    )
}

$actualImports = Get-ImportTable $sourcePeDump
Assert-SetEquals -Actual @($actualImports.Keys) -Expected @($expectedPeImports.Keys) -Label "PE import DLL set"
foreach ($dll in $expectedPeImports.Keys) {
    Assert-SetEquals -Actual @($actualImports[$dll]) -Expected @($expectedPeImports[$dll]) -Label "PE imports for $dll"
}

Assert-FileContains -Path $sourceNativeObjDump -Patterns @(
    'HostLogProof_HostLogProof_Program__ManagedMain>',
    'mov\s+0x8\(%rbx\),%rcx',
    'mov\s+0x8\(%rcx\),%rsi',
    'call\s+\*%rsi'
) -Label "Native object ManagedMain disassembly"

Assert-FileContains -Path $sourceMap -Patterns @(
    'ManagedMain\s+0000000010001900',
    'HostLogProof__Module___MainMethodWrapper',
    'HostLogProof__Module___StartupCodeMain'
) -Label "Link map"

Assert-FileContains -Path $sourceElfReadelf -Patterns @(
    'ELF64',
    'Type:\s+EXEC',
    'Machine:\s+Advanced Micro Devices X86-64',
    'Entry point address:\s+0x10001900',
    'Number of program headers:\s+7',
    'There is no dynamic section in this file\.',
    'There are no relocations in this file\.',
    'There are no sections in this file\.'
) -Label "Final ELF"

Assert-FileContains -Path $sourceElfDump -Patterns @(
    'flags r-x',
    'flags rw-'
) -Label "Final ELF segment flags"

Assert-FileNotContains -Path $sourceElfReadelf -Patterns @(
    'PT_INTERP',
    'NEEDED',
    'libc',
    'libpthread',
    'libdl',
    'libm',
    'rwx'
) -Label "Final ELF dependency scan"

Assert-FileNotContains -Path $sourcePeDump -Patterns @(
    'ucrtbase\.dll',
    'msvcrt\.dll',
    'ntdll\.dll'
) -Label "Intermediate PE forbidden imports"

$stagedAppsRoot = Join-Path $StageRoot "apps"
$stagedProofRoot = Join-Path $StageRoot "proof"
$stagedAppRoot = Join-Path $stagedAppsRoot "ManagedHostLogProof"
$stagedBinRoot = Join-Path $stagedAppRoot "bin\amd64"
$stagedElf = Join-Path $stagedBinRoot "HostLogProof.elf"
$stagedManifest = Join-Path $stagedAppRoot "app.json"
$stageEnvelope = Join-Path $stagedProofRoot "proof-envelope.json"

New-Item -ItemType Directory -Force -Path $stagedBinRoot | Out-Null
New-Item -ItemType Directory -Force -Path $stagedProofRoot | Out-Null

Copy-Item -LiteralPath $sourceElf -Destination $stagedElf -Force

$sourceHash = Get-FileHashHex $sourceElf
$stagedHash = Get-FileHashHex $stagedElf
if ($sourceHash -ne $stagedHash) {
    throw "Staged ELF hash mismatch: source=$sourceHash staged=$stagedHash"
}

$manifest = [ordered]@{
    schemaVersion = 1
    id = "com.guidexos.experimental.nativeaot.hostlogproof"
    displayName = "Managed HostLogProof"
    version = "0.1.0"
    publisher = "guideXOS Experimental"
    description = "NativeAOT managed-code execution proof"
    category = "Experimental"
    kind = "NativeElf"
    icon = ""
    minGuideXOSVersion = "0.1.0"
    supportedArchitectures = @("amd64")
    desktopRegistryHints = [ordered]@{
        "gxos.nativeaot.tlsIndexAddress" = ("0x{0:X}" -f $tlsIndexAddress)
        "gxos.nativeaot.tlsBlockSize" = ("0x{0:X}" -f $tlsBlockSize)
    }
    entries = @(
        [ordered]@{
            architecture = "amd64"
            path = "bin/amd64/HostLogProof.elf"
            entryPoint = "ManagedMain"
            abi = "guidexos-c-abi-v1"
            runtime = "native-elf"
        }
    )
    permissions = @("log")
    fileAssociations = @()
}

$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $stagedManifest -Encoding ASCII

$envelope = [ordered]@{
    proofLabel = "NativeAOT managed-code execution proof"
    sourceOutputRoot = $OutputRoot
    stageRoot = $StageRoot
    stageAppsRoot = $stagedAppsRoot
    stageProofRoot = $stagedProofRoot
    stageAppDirectory = $stagedAppRoot
    stageManifest = $stagedManifest
    manifestId = $manifest.id
    manifestDisplayName = $manifest.displayName
    manifestEntryPoint = $manifest.entries[0].entryPoint
    tlsIndexAddress = ("0x{0:X}" -f $tlsIndexAddress)
    tlsStartAddress = ("0x{0:X}" -f $tlsStartAddress)
    tlsEndAddress = ("0x{0:X}" -f $tlsEndAddress)
    tlsBlockSize = ("0x{0:X}" -f $tlsBlockSize)
    stagedElf = $stagedElf
    sourceElf = $sourceElf
    sourceElfSha256 = $sourceHash
    stagedElfSha256 = $stagedHash
    expectedEntryAddress = "0x10001900"
    expectedManagedSymbol = "ManagedMain"
    expectedMessage = "Hello from managed guideXOS code"
    expectedReturnCode = 0
    architecture = "amd64"
    abi = "guidexos-c-abi-v1"
    registrySourceEnvironment = "GXOS_NATIVE_ELF_STAGE_ROOT"
}

$envelope | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $stageEnvelope -Encoding ASCII

Write-Host "[dotnet-proof] artifact accepted"
Write-Host "[dotnet-proof] source=$sourceElf"
Write-Host "[dotnet-proof] staged=$stagedElf"
Write-Host "[dotnet-proof] hash=$stagedHash"
Write-Host "[dotnet-proof] stage-root=$StageRoot"
Write-Host "[dotnet-proof] app-source-root=$stagedAppsRoot"
Write-Host "[dotnet-proof] entry=0x10001900"
Write-Host "[dotnet-proof] envelope=$stageEnvelope"

Write-Output $stageEnvelope

