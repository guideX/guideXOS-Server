param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$RuntimePackRoot = "",
    [string]$OutputRoot = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($RuntimePackRoot)) { $RuntimePackRoot = Join-Path $root "tools\dotnet\runtime-pack" }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $root "out\dotnet\managed-single-allocation" }
$RuntimePackRoot = [System.IO.Path]::GetFullPath($RuntimePackRoot)
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$build = Join-Path $root "scripts\dotnet\build-managed-hostlog-proof.ps1"
$artifact = Join-Path $OutputRoot "artifacts"
$manifestPath = Join-Path $root "out\dotnet\runtime-pack\runtime-pack.manifest.json"

if (-not $SkipBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $build -RepoRoot $root -RuntimePackRoot $RuntimePackRoot -OutputRoot $OutputRoot -UseGuideXosRuntimePack -AllocationMode Allocating -Clean
    if ($LASTEXITCODE -ne 0) { throw "Single-allocation static build failed with exit code $LASTEXITCODE" }
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.identity -ne "guidexos-nativeaot-runtime-pack-amd64-hostlog-allocating-nocollection-v1") { throw "Allocation runtime-pack identity mismatch." }
if (-not [bool]$manifest.managedAllocation) { throw "Allocation runtime-pack is not marked managedAllocation=true." }
$lockHash = (Get-FileHash -LiteralPath ([string]$manifest.lockFile) -Algorithm SHA256).Hash.ToUpperInvariant()
if ($lockHash -ne ([string]$manifest.lockFileSha256).ToUpperInvariant()) { throw "Runtime-pack lock hash mismatch." }

$mapPath = Join-Path $artifact "HostLogProof.map"
$peDumpPath = Join-Path $artifact "HostLogProof.pe.objdump.txt"
$nativeDumpPath = Join-Path $artifact "HostLogProof.native.objdump.txt"
$runtimeObjectDumpPath = Join-Path $artifact "guidexos_nativeaot_platform.objdump.txt"
$linkPath = Join-Path $artifact "HostLogProof.link.rsp"
$elfPath = Join-Path $artifact "HostLogProof.elf"
foreach ($path in @($mapPath, $peDumpPath, $nativeDumpPath, $runtimeObjectDumpPath, $linkPath, $elfPath)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Allocation static artifact is missing: $path" }
}

$mapText = Get-Content -LiteralPath $mapPath -Raw
foreach ($pattern in @(
    'RhpNewArray\s+[0-9A-Fa-f]{16}',
    'guideXosStockRhpNewArray',
    'RhpNewFast',
    'guideXosManagedHeap',
    'g_guideXosAllocationDiagnostics',
    'guideXosManagedArrayHostLog',
    '__pinvoke_HostLogProof__Module____Internal__guideXosManagedArrayHostLog__Ansi',
    'g_ephemeral_low',
    'g_ephemeral_high'
)) {
    if ($mapText -notmatch $pattern) { throw "Missing allocation evidence in map: $pattern" }
}

$peText = Get-Content -LiteralPath $peDumpPath -Raw
foreach ($name in @("FlsGetValue", "FlsSetValue")) {
    if ($peText -match "\b$name\b") { throw "Allocation image still contains a live $name import." }
}
$linkText = Get-Content -LiteralPath $linkPath -Raw
if ($linkText -notmatch 'Runtime\.WorkstationGC\.lib') { throw "Allocation link did not include the workstation GC archive." }
if ($mapText -notmatch 'Runtime\.WorkstationGC:AllocFast\.asm\.obj\.renamed\.obj') { throw "Allocation link did not consume the renamed stock array helper." }

$linkedDisasmPath = Join-Path $artifact "HostLogProof.linked.disasm.txt"
$managedMainMatch = [regex]::Match($mapText, '(?m)^\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+ManagedMain\s+([0-9A-Fa-f]{16})\s+')
$newArrayMatch = [regex]::Match($mapText, '(?m)^\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+RhpNewArray\s+([0-9A-Fa-f]{16})\s+')
$stockArrayMatch = [regex]::Match($mapText, '(?m)^\s+[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+guideXosStockRhpNewArray\s+([0-9A-Fa-f]{16})\s+')
if (-not $managedMainMatch.Success -or -not $newArrayMatch.Success -or -not $stockArrayMatch.Success) { throw "Unable to resolve allocation call-path symbols." }
$managedMain = [Convert]::ToUInt64($managedMainMatch.Groups[1].Value, 16)
$newArray = [Convert]::ToUInt64($newArrayMatch.Groups[1].Value, 16)
$stockArray = [Convert]::ToUInt64($stockArrayMatch.Groups[1].Value, 16)
$imagePath = Join-Path $artifact "HostLogProof.exe"
$managedMainStart = "0x{0:X}" -f $managedMain
$managedMainStop = "0x{0:X}" -f ($managedMain + 0x110)
$newArrayStart = "0x{0:X}" -f $newArray
$newArrayStop = "0x{0:X}" -f ($newArray + 0x100)
& objdump -d "--start-address=$managedMainStart" "--stop-address=$managedMainStop" $imagePath | Set-Content -LiteralPath $linkedDisasmPath -Encoding ASCII
$disasm = Get-Content -LiteralPath $linkedDisasmPath -Raw
if ($disasm -notmatch ("call\s+0x{0:x}" -f $newArray)) { throw "ManagedMain does not call the linked RhpNewArray wrapper." }
& objdump -d "--start-address=$newArrayStart" "--stop-address=$newArrayStop" $imagePath | Add-Content -LiteralPath $linkedDisasmPath -Encoding ASCII
$disasm = Get-Content -LiteralPath $linkedDisasmPath -Raw
if ($disasm -notmatch ("call\s+0x{0:x}" -f $stockArray)) { throw "RhpNewArray wrapper does not call the renamed stock helper." }

$runtimeObjectText = Get-Content -LiteralPath $runtimeObjectDumpPath -Raw
foreach ($pattern in @('guideXosManagedArrayHostLog')) {
    if ($runtimeObjectText -notmatch [regex]::Escape($pattern)) {
        throw "Missing runtime helper binding evidence: $pattern"
    }
}

$programText = Get-Content -LiteralPath (Join-Path $root "samples\managed\HostLogProof\Program.cs") -Raw
if ($programText -notmatch 'HOSTLOGPROOF_ALLOCATING') { throw "Managed source does not preserve the selectable allocation mode." }
if ($programText -notmatch 'byte\[\] messageBuffer = new byte\[\]') { throw "Managed allocation source is missing." }
if (([regex]::Matches($programText, 'byte\[\] messageBuffer = new byte\[\]').Count) -ne 1) { throw "Expected exactly one intentional managed byte-array allocation." }
if ($programText -notmatch 'Unsafe\.As<byte\[\], nint>') { throw "Managed source does not pass the array as an opaque runtime reference." }
if ($programText -notmatch 'GC\.KeepAlive\(messageBuffer\)') { throw "Managed source does not keep the array live through the synchronous helper call." }
if ((Get-Content -LiteralPath (Join-Path $root "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp") -Raw) -match 'Hello from managed heap') { throw "Success message leaked into native runtime support." }

$baselinePeDump = Join-Path $root "out\dotnet\allocation-comparison\non-allocating\managed-hostlog\artifacts\HostLogProof.pe.objdump.txt"
if (Test-Path -LiteralPath $baselinePeDump) {
    $baselineText = Get-Content -LiteralPath $baselinePeDump -Raw
    $baselineImports = @([regex]::Matches($baselineText, '(?m)^\s*[0-9A-Fa-f]+\s+<none>\s+[0-9A-Fa-f]+\s+([^\s]+)\s*$') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $allocationImports = @([regex]::Matches($peText, '(?m)^\s*[0-9A-Fa-f]+\s+<none>\s+[0-9A-Fa-f]+\s+([^\s]+)\s*$') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $importDiff = Compare-Object -ReferenceObject $baselineImports -DifferenceObject $allocationImports
    $allowedResolverImports = @('FreeLibrary', 'SetThreadErrorMode')
    $unexpectedImportDiff = @($importDiff | Where-Object { $_.SideIndicator -eq '=>' -and $_.InputObject -notin $allowedResolverImports })
    if ($unexpectedImportDiff) { throw "Allocation introduced an unexpected PE import diff: $($unexpectedImportDiff | Out-String)" }
    $missingExpectedResolverImports = @($allowedResolverImports | Where-Object { $_ -notin $allocationImports })
    if ($missingExpectedResolverImports) { throw "Expected generated __Internal resolver imports are missing: $($missingExpectedResolverImports -join ', ')" }
    Write-Host "Static resolver imports added: $($allowedResolverImports -join ', ') (not entered on the guideXOS live path)" -ForegroundColor Yellow
}

Write-Host "Managed single-allocation static smoke PASS" -ForegroundColor Green
Write-Host "Allocation helper: ManagedMain -> RhpNewArray -> guideXosStockRhpNewArray -> RhpNewFast" -ForegroundColor Cyan
Write-Host "Host exposure: opaque array reference -> bound __Internal runtime helper -> existing host Log ABI" -ForegroundColor Cyan
Write-Host "Requested array length: 24 bytes (23 message bytes plus managed NUL)" -ForegroundColor Cyan
Write-Host "Computed object size: 40 bytes, 8-byte aligned" -ForegroundColor Cyan
Write-Host "Collection: disabled; bounded image-backed no-collection context" -ForegroundColor Cyan
Write-Host "New live Windows imports: none; static resolver imports present but not entered: FreeLibrary, SetThreadErrorMode" -ForegroundColor Cyan
