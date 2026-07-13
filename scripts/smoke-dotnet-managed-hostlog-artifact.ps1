param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutputRoot = "",
    [string]$StageRoot = "",
    [string]$BuildScript = "",
    [string]$StageScript = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = [System.IO.Path]::GetFullPath($RepoRoot)
. (Join-Path $Root "scripts\dotnet\managed-hostlog-artifact-assertions.ps1")

function Assert-TextContains([string]$Text, [string]$Pattern, [string]$Label) {
    if ($Text -notmatch $Pattern) { throw "$Label missing pattern: $Pattern" }
}

function Assert-WithinRoot([string]$Path, [string]$RootPath, [string]$Label) {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($RootPath.TrimEnd('\', '/'))
    if (-not $fullRoot.EndsWith([System.IO.Path]::DirectorySeparatorChar)) { $fullRoot += [System.IO.Path]::DirectorySeparatorChar }
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) { throw "$Label escapes its root: $fullPath" }
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $Root "out\dotnet\managed-hostlog\static-smoke" }
if ([string]::IsNullOrWhiteSpace($StageRoot)) { $StageRoot = Join-Path $OutputRoot "stage-managed-hostlog-proof" }
if ([string]::IsNullOrWhiteSpace($BuildScript)) { $BuildScript = Join-Path $Root "scripts\dotnet\build-managed-hostlog-proof.ps1" }
if ([string]::IsNullOrWhiteSpace($StageScript)) { $StageScript = Join-Path $Root "scripts\dotnet\stage-managed-hostlog-proof.ps1" }

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$StageRoot = [System.IO.Path]::GetFullPath($StageRoot)
$BuildScript = [System.IO.Path]::GetFullPath($BuildScript)
$StageScript = [System.IO.Path]::GetFullPath($StageScript)
Assert-WithinRoot $OutputRoot $Root "Output"
Assert-WithinRoot $StageRoot $Root "Stage"

$inventoryPaths = @("desktop.json", "app_registry.cpp", "Apps", "examples\apps", "sdk\samples")
$inventoryBefore = (& git -C $Root status --porcelain=v1 -- $inventoryPaths) -join "`n"
$powershell = (Get-Command powershell -ErrorAction Stop).Source

Write-Host "[dotnet-artifact] clean build root: $OutputRoot"
& $powershell -ExecutionPolicy Bypass -File $BuildScript -RepoRoot $Root -OutputRoot $OutputRoot -Clean
if ($LASTEXITCODE -ne 0) { throw "Managed proof build failed with exit code $LASTEXITCODE" }

Write-Host "[dotnet-artifact] stage validation without rebuilding"
& $powershell -ExecutionPolicy Bypass -File $StageScript -RepoRoot $Root -OutputRoot $OutputRoot -StageRoot $StageRoot -SkipBuild
if ($LASTEXITCODE -ne 0) { throw "Managed proof stage validation failed with exit code $LASTEXITCODE" }

$artifactRoot = Join-Path $OutputRoot "artifacts"
$sourceElf = Join-Path $artifactRoot "HostLogProof.elf"
$sourcePe = Join-Path $artifactRoot "HostLogProof.exe"
$sourceMap = Join-Path $artifactRoot "HostLogProof.map"
$sourcePeDump = Join-Path $artifactRoot "HostLogProof.pe.objdump.txt"
$sourceElfDump = Join-Path $artifactRoot "HostLogProof.elf.objdump.txt"
$sourceReadelf = Join-Path $artifactRoot "HostLogProof.elf.readelf.txt"
$sourceNativeDump = Join-Path $artifactRoot "HostLogProof.native.objdump.txt"
$runtimeSupportSource = Join-Path $Root "samples\managed\HostLogProof\runtime_support.c"
$stageEnvelopePath = Join-Path $StageRoot "proof\proof-envelope.json"
$stageManifestPath = Join-Path $StageRoot "apps\ManagedHostLogProof\app.json"
$stagedElf = Join-Path $StageRoot "apps\ManagedHostLogProof\bin\amd64\HostLogProof.elf"

foreach ($path in @($sourceElf, $sourcePe, $sourceMap, $sourcePeDump, $sourceElfDump, $sourceReadelf, $sourceNativeDump, $runtimeSupportSource, $stageEnvelopePath, $stageManifestPath, $stagedElf)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Static proof output missing: $path" }
}

$expectedImports = Get-ManagedHostLogExpectedPeImports
$actualImports = Get-ManagedHostLogImportTable $sourcePeDump
Assert-ManagedHostLogSetEquals @($actualImports.Keys) @($expectedImports.Keys) "PE import DLL set"
foreach ($dll in $expectedImports.Keys) {
    Assert-ManagedHostLogSetEquals @($actualImports[$dll]) @($expectedImports[$dll]) "PE imports for $dll"
}
Assert-ManagedHostLogFileNotContains $sourcePeDump @('ucrtbase\.dll', 'msvcrt\.dll', 'ntdll\.dll') "PE forbidden imports"

$elf = Assert-ManagedHostLogElfEnvelope -ElfPath $sourceElf -PePath $sourcePe -MapPath $sourceMap -NativeObjectDumpPath $sourceNativeDump -ElfReadelfPath $sourceReadelf -ElfDumpPath $sourceElfDump -RuntimeSupportSourcePath $runtimeSupportSource
$managedMain = Get-ManagedHostLogMapSymbolAddress $sourceMap "ManagedMain" "Managed entry"
$sourceHash = (Get-FileHash -LiteralPath $sourceElf -Algorithm SHA256).Hash.ToUpperInvariant()
$stagedHash = (Get-FileHash -LiteralPath $stagedElf -Algorithm SHA256).Hash.ToUpperInvariant()
if ($sourceHash -ne $stagedHash) { throw "Source/staged ELF hash mismatch: source=$sourceHash staged=$stagedHash" }

$manifest = Get-Content -LiteralPath $stageManifestPath -Raw | ConvertFrom-Json
$envelope = Get-Content -LiteralPath $stageEnvelopePath -Raw | ConvertFrom-Json
if ($manifest.kind -ne "NativeElf") { throw "Staged manifest kind is not NativeElf." }
if ($manifest.entries[0].entryPoint -ne "ManagedMain") { throw "Staged entry point is not ManagedMain." }
if ($manifest.entries[0].abi -ne "guidexos-c-abi-v1") { throw "Staged ABI is not guidexos-c-abi-v1." }
if ([string]$envelope.expectedEntryAddress -ne ("0x{0:X}" -f $managedMain)) { throw "Stage entry metadata does not match the map." }
if ([string]$envelope.sourceElfSha256 -ne $sourceHash -or [string]$envelope.stagedElfSha256 -ne $stagedHash) { throw "Stage envelope hashes do not match the artifacts." }
if ([string]$manifest.desktopRegistryHints.'gxos.nativeelf.tlsIndexAddress' -ne ("0x{0:X}" -f (Get-ManagedHostLogMapSymbolAddress $sourceMap "_tls_index" "TLS index"))) { throw "Staged TLS index hint does not match the map." }
$tlsStart = Get-ManagedHostLogMapSymbolAddress $sourceMap "_tls_start" "TLS start"
$tlsEnd = Get-ManagedHostLogMapSymbolAddress $sourceMap "_tls_end" "TLS end"
if ([string]$manifest.desktopRegistryHints.'gxos.nativeelf.tlsBlockSize' -ne ("0x{0:X}" -f ($tlsEnd - $tlsStart))) { throw "Staged TLS block-size hint does not match the map." }

$abiSource = Get-Content -LiteralPath (Join-Path $Root "samples\managed\HostLogProof\NativeAbi.cs") -Raw
$abiHeader = Get-Content -LiteralPath (Join-Path $Root "native_app_runtime.h") -Raw
Assert-TextContains $abiSource 'StructLayout\(LayoutKind\.Sequential\)' "managed ABI layout"
Assert-TextContains $abiSource 'public uint size;\s+public uint apiVersion;\s+public NativeHostCallTable\* host;\s+public void\* userData;' "managed context field order"
Assert-TextContains $abiHeader 'struct NativeGxAppContext' "native context declaration"
Assert-TextContains $abiHeader '(?s)uint32_t size.*uint32_t apiVersion.*const NativeHostCallTable\* host.*void\* userData' "native context field order"

$inventoryAfter = (& git -C $Root status --porcelain=v1 -- $inventoryPaths) -join "`n"
if ($inventoryBefore -cne $inventoryAfter) { throw "Default application inventory changed during static proof validation." }

$checkIgnored = & git -C $Root check-ignore -q -- $OutputRoot
if ($LASTEXITCODE -ne 0) { throw "Generated output root is not ignored: $OutputRoot" }
$trackedOutput = (& git -C $Root status --porcelain=v1 --untracked-files=all -- $OutputRoot) -join "`n"
if (-not [string]::IsNullOrWhiteSpace($trackedOutput)) { throw "Generated output became tracked or visible in Git status: $trackedOutput" }

Write-Host "NativeAOT managed host-log artifact static smoke PASS" -ForegroundColor Green
Write-Host ("[dotnet-artifact] entry=0x{0:X} loadSegments={1} sourceHash={2}" -f $elf.Entry, $elf.LoadSegments.Count, $sourceHash)
