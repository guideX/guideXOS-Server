param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\.." )).Path,
    [string]$RuntimePackRoot = $PSScriptRoot,
    [string]$StockRuntimePackRoot = "",
    [string]$OutputRoot = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-WithinRoot([string]$Path, [string]$Root, [string]$Label) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escapes its allowed root: $fullPath"
    }
}

function Find-VcVars64 {
    foreach ($candidate in @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    )) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw "Visual C++ vcvars64.bat was not found."
}

function Find-Tool([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Source)) {
        return [IO.Path]::GetFullPath($command.Source)
    }
    foreach ($root in @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC",
        "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\MSVC",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Tools\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC"
    )) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $match = Get-ChildItem -LiteralPath $root -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin\Hostx64\x64\$Name" } |
            Where-Object { Test-Path -LiteralPath $_ } |
            Select-Object -First 1
        if ($null -ne $match) { return [IO.Path]::GetFullPath($match) }
    }
    throw "Required tool was not found: $Name"
}

function Invoke-VcBatch([string]$Path, [string[]]$Lines) {
    [IO.File]::WriteAllLines($Path, $Lines, [Text.ASCIIEncoding]::new())
    & $Path
    if ($LASTEXITCODE -ne 0) { throw "MSVC command failed ($LASTEXITCODE): $Path" }
}

function Get-ExactMember([string[]]$Members, [string]$Leaf) {
    $matches = @($Members | Where-Object { (Split-Path $_ -Leaf) -eq $Leaf })
    if ($matches.Count -ne 1) { throw "Expected one stock archive member named $Leaf; found $($matches.Count)." }
    return $matches[0]
}

function Get-DumpbinUndefinedSymbols([string]$Path) {
    $records = [Collections.Generic.List[string]]::new()
    $current = $null
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(?:SECT\S*|UNDEF)\b') {
            if ($null -ne $current) { $records.Add($current.Trim()) }
            $current = if ($line -match '\|\s*(.*)$') { $matches[1].Trim() } else { '' }
            if ($line -notmatch '\bUNDEF\b') { $current = $null }
        } elseif ($null -ne $current) {
            $current += ' ' + $line.Trim()
        }
    }
    if ($null -ne $current) { $records.Add($current.Trim()) }
    return $records.ToArray()
}

$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
$RuntimePackRoot = [IO.Path]::GetFullPath($RuntimePackRoot)
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $RepoRoot "out\dotnet\nativeaot-pal-runtime-replacement"
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
Assert-WithinRoot $RuntimePackRoot $RepoRoot "Runtime-pack source"
Assert-WithinRoot $OutputRoot (Join-Path $RepoRoot "out\dotnet") "Evidence output"

$lockPath = Join-Path $RuntimePackRoot "runtime-pack.lock.json"
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$nugetRoot = if ([string]::IsNullOrWhiteSpace($env:NUGET_PACKAGES)) {
    Join-Path $env:USERPROFILE ".nuget\packages"
} else { $env:NUGET_PACKAGES }
$stockRoot = if ([string]::IsNullOrWhiteSpace($StockRuntimePackRoot)) {
    Join-Path $nugetRoot "runtime.win-x64.microsoft.dotnet.ilcompiler\9.0.0"
} else { [IO.Path]::GetFullPath($StockRuntimePackRoot) }
$stockLibrary = Join-Path $stockRoot "sdk\Runtime.WorkstationGC.lib"
$expectedStockHash = $lock.runtimePack.files.'sdk/Runtime.WorkstationGC.lib'.sha256.ToUpperInvariant()
if ((Get-Hash $stockLibrary) -ne $expectedStockHash) {
    throw "Locked stock Runtime.WorkstationGC.lib hash mismatch."
}

$sourceCheckout = Join-Path $RepoRoot "out\dotnet\gc-feasibility-baseline\nativeaot-runtime"
$sourceCommit = $lock.ilCompiler.commit
if (-not (Test-Path -LiteralPath (Join-Path $sourceCheckout ".git"))) {
    throw "Locked NativeAOT source checkout is missing: $sourceCheckout"
}
$actualCommit = (& git -C $sourceCheckout rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw "Unable to read NativeAOT source checkout revision." }
if ((& git -C $sourceCheckout cat-file -e "$sourceCommit`^{commit}") -ne $null) { }
if ($LASTEXITCODE -ne 0) { throw "Locked NativeAOT commit is not available in the source checkout: $sourceCommit" }

if ($Clean -and (Test-Path -LiteralPath $OutputRoot)) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
$extractRoot = Join-Path $OutputRoot "stock\extracted"
$dumpRoot = Join-Path $OutputRoot "stock\dumpbin"
$buildRoot = Join-Path $OutputRoot "replacement-contract"
New-Item -ItemType Directory -Force -Path $extractRoot,$dumpRoot,$buildRoot | Out-Null

$lib = Find-Tool "lib.exe"
$dumpbin = Find-Tool "dumpbin.exe"
$vcvars = Find-VcVars64
$stockMembers = @(& $lib /nologo /list $stockLibrary)
$stockMembers | Set-Content -LiteralPath (Join-Path $OutputRoot "stock\members.txt") -Encoding ASCII

$objectDefinitions = @(
    [ordered]@{
        object = "PalRedhawkCommon.cpp.obj"
        source = "src/coreclr/nativeaot/Runtime/windows/PalRedhawkCommon.cpp"
        role = "common PAL: stack bounds, module bounds/name, tick count"
        startup = "future RhInitialize/thread attach"
    },
    [ordered]@{
        object = "PalRedhawkMinWin.cpp.obj"
        source = "src/coreclr/nativeaot/Runtime/windows/PalRedhawkMinWin.cpp"
        role = "Windows PAL: FLS, VM, events, current thread, thread starts, resolver/fail-fast"
        startup = "future RhInitialize; optional paths must remain unreachable"
    },
    [ordered]@{
        object = "thread.cpp.obj"
        source = "src/coreclr/nativeaot/Runtime/thread.cpp"
        role = "NativeAOT Thread and ThreadStore implementation"
        startup = "future ThreadStore attach/current lookup"
    },
    [ordered]@{
        object = "time.c.obj"
        source = "src/native/minipal/time.c"
        role = "minipal high-resolution counter and delay"
        startup = "future ThreadStore/yield/timing paths"
    }
)

$objectEvidence = [Collections.Generic.List[object]]::new()
foreach ($definition in $objectDefinitions) {
    $member = Get-ExactMember $stockMembers $definition.object
    $destination = Join-Path $extractRoot $definition.object
    & $lib /nologo /extract:"$member" "$stockLibrary" /out:"$destination" *> $null
    if ($LASTEXITCODE -ne 0) { throw "Unable to extract $member" }
    $symbolPath = Join-Path $dumpRoot ($definition.object + ".symbols.txt")
    $importPath = Join-Path $dumpRoot ($definition.object + ".imports.txt")
    $headerPath = Join-Path $dumpRoot ($definition.object + ".headers.txt")
    $directivePath = Join-Path $dumpRoot ($definition.object + ".directives.txt")
    & $dumpbin /nologo /symbols $destination | Set-Content $symbolPath -Encoding ASCII
    & $dumpbin /nologo /imports $destination | Set-Content $importPath -Encoding ASCII
    & $dumpbin /nologo /headers $destination | Set-Content $headerPath -Encoding ASCII
    & $dumpbin /nologo /directives $destination | Set-Content $directivePath -Encoding ASCII
    $blob = (& git -C $sourceCheckout rev-parse "$sourceCommit`:$($definition.source)").Trim()
    if ($LASTEXITCODE -ne 0) { throw "Locked source path is missing: $($definition.source)" }
    $objectEvidence.Add([ordered]@{
        object = $definition.object
        member = $member
        source = $definition.source
        sourceCommit = $sourceCommit
        sourceBlob = $blob
        role = $definition.role
        startupReachability = $definition.startup
        sha256 = Get-Hash $destination
        length = (Get-Item -LiteralPath $destination).Length
        symbols = $symbolPath
        imports = $importPath
    })
}

$imports = @(
    @{ symbol="VirtualQuery"; object="PalRedhawkCommon.cpp.obj"; caller="PalGetMaximumStackBounds"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_hooks.stack_bounds"; strategy="source replacement; no Windows memory query" },
    @{ symbol="GetTickCount64"; object="PalRedhawkCommon.cpp.obj"; caller="PalGetTickCount64"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_hooks.counter/frequency"; strategy="source replacement over guideXOS monotonic clock" },
    @{ symbol="VirtualAlloc"; object="PalRedhawkMinWin.cpp.obj"; caller="PalVirtualAlloc"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_hooks.virtual_alloc"; strategy="opaque VM adapter; exact reserve/commit contract required" },
    @{ symbol="VirtualFree"; object="PalRedhawkMinWin.cpp.obj"; caller="PalVirtualFree"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_hooks.virtual_free"; strategy="opaque VM adapter" },
    @{ symbol="VirtualProtect"; object="PalRedhawkMinWin.cpp.obj"; caller="PalVirtualProtect"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_hooks.virtual_protect"; strategy="opaque VM protection adapter" },
    @{ symbol="CreateEventW"; object="PalRedhawkMinWin.cpp.obj"; caller="PalCreateEventW"; before="No"; initialize="Later"; managed="Later"; collection="Yes"; adapter="pending opaque event ABI"; strategy="not replaced until handle/wait ownership is proven" },
    @{ symbol="CloseHandle"; object="PalRedhawkMinWin.cpp.obj"; caller="PalStartBackgroundWork / PalAllocateThunksFromTemplate"; before="No"; initialize="No"; managed="Later"; collection="Later"; adapter="pending opaque handle ABI"; strategy="optional thread/thunk paths" },
    @{ symbol="CreateThread"; object="PalRedhawkMinWin.cpp.obj"; caller="PalStartBackgroundWork"; before="No"; initialize="No"; managed="Later"; collection="Later"; adapter="guidexos native-thread C bridge"; strategy="plain worker probe only; no helper thread this pass" },
    @{ symbol="FlsAlloc"; object="PalRedhawkMinWin.cpp.obj"; caller="PalInit"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_fls_alloc"; strategy="completed FLS contract, exact PAL object not yet rebuilt" },
    @{ symbol="FlsGetValue"; object="PalRedhawkMinWin.cpp.obj"; caller="FiberDetachCallback / PalAttachThread / PalDetachThread"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_fls_get"; strategy="completed FLS contract, exact PAL object not yet rebuilt" },
    @{ symbol="FlsSetValue"; object="PalRedhawkMinWin.cpp.obj"; caller="PalAttachThread / PalDetachThread"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_fls_set"; strategy="completed FLS contract, exact PAL object not yet rebuilt" },
    @{ symbol="GetCurrentThreadId"; object="PalRedhawkMinWin.cpp.obj"; caller="PalGetCurrentOSThreadId"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_hooks.current_thread_id"; strategy="native-thread identity callback" },
    @{ symbol="GetLastError"; object="PalRedhawkMinWin.cpp.obj"; caller="PalCompatibleWaitAny / PalHijack"; before="No"; initialize="No"; managed="Later"; collection="Later"; adapter="pending per-thread error slot"; strategy="optional COM/hijack paths" },
    @{ symbol="GetCurrentProcess"; object="PalRedhawkMinWin.cpp.obj"; caller="InitializeCurrentProcessCpuCount / PalFlushInstructionCache"; before="No"; initialize="Conditional"; managed="Later"; collection="Later"; adapter="pending process capability ABI"; strategy="not part of bounded startup contract" },
    @{ symbol="SwitchToThread"; object="PalRedhawkMinWin.cpp.obj"; caller="PalSwitchToThread"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_yield"; strategy="source replacement over scheduler yield" },
    @{ symbol="GetLastError"; object="thread.cpp.obj"; caller="Thread::WaitForGC / error-state helpers"; before="No"; initialize="Conditional"; managed="Later"; collection="Later"; adapter="pending per-thread error slot"; strategy="raw object has an additional SetLastError pair" },
    @{ symbol="QueryPerformanceCounter"; object="time.c.obj"; caller="minipal_hires_ticks"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_counter"; strategy="source replacement over monotonic callback" },
    @{ symbol="QueryPerformanceFrequency"; object="time.c.obj"; caller="minipal_hires_tick_frequency"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_frequency"; strategy="source replacement over monotonic callback" },
    @{ symbol="SleepEx"; object="time.c.obj"; caller="minipal_microdelay"; before="No"; initialize="Yes"; managed="Yes"; collection="Yes"; adapter="guidexos_nativeaot_pal_sleep"; strategy="source replacement over scheduler sleep" }
)

$forbidden = @(
    "VirtualAlloc", "VirtualFree", "VirtualQuery", "VirtualProtect", "CreateEventW", "CloseHandle",
    "CreateThread", "FlsAlloc", "FlsGetValue", "FlsSetValue", "GetCurrentThreadId", "GetLastError",
    "GetCurrentProcess", "SwitchToThread", "GetTickCount64", "QueryPerformanceCounter",
    "QueryPerformanceFrequency", "SleepEx"
)
$importEvidence = foreach ($evidence in $objectEvidence) {
    $textPath = [IO.Path]::GetFullPath($evidence.symbols)
    $text = Get-Content -LiteralPath $textPath -Raw
    foreach ($symbol in $forbidden | Select-Object -Unique) {
        if ($text -match "__imp_$([Regex]::Escape($symbol))(?:\b|\s)") {
            [ordered]@{ object = $evidence.object; symbol = $symbol; importDump = $textPath }
        }
    }
}
New-Item -ItemType Directory -Force -Path (Join-Path $OutputRoot "imports") | Out-Null
$imports | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputRoot "imports\inventory.json") -Encoding UTF8
$importEvidence | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputRoot "imports\raw-candidate-evidence.json") -Encoding UTF8

$contractSource = Join-Path $RuntimePackRoot "src\platform\guidexos_nativeaot_pal_contract.cpp"
$contractObject = Join-Path $buildRoot "guidexos_nativeaot_pal_contract.obj"
$buildBatch = Join-Path $buildRoot "build-guidexos-nativeaot-pal-contract.bat"
$commonFlags = "/nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro /DGUIDEXOS_NATIVEAOT_PAL_CONTRACT"
$buildLines = @(
    "@echo off", "setlocal", "call `"$vcvars`" >nul", "if errorlevel 1 exit /b %errorlevel%",
    "cl.exe $commonFlags /Fo:`"$contractObject`" `"$contractSource`"",
    "exit /b %errorlevel%"
)
Invoke-VcBatch $buildBatch $buildLines
if (-not (Test-Path -LiteralPath $contractObject)) { throw "PAL contract object was not produced." }
$contractSymbols = Join-Path $buildRoot "guidexos_nativeaot_pal_contract.symbols.txt"
$contractImports = Join-Path $buildRoot "guidexos_nativeaot_pal_contract.imports.txt"
& $dumpbin /nologo /symbols $contractObject | Set-Content $contractSymbols -Encoding ASCII
& $dumpbin /nologo /imports $contractObject | Set-Content $contractImports -Encoding ASCII

$manifest = [ordered]@{
    schemaVersion = 1
    status = "ABI-contract-compiled; active-archive-replacement-not-authorized"
    reason = "PalRedhawkMinWin.cpp.obj and thread.cpp.obj contain source families beyond the 19 candidate imports; a partial object surgery would leave duplicate platform state or unresolved optional imports."
    lockFile = $lockPath
    lockFileSha256 = Get-Hash $lockPath
    stockLibrary = $stockLibrary
    stockLibrarySha256 = Get-Hash $stockLibrary
    expectedStockLibrarySha256 = $expectedStockHash
    nativeAotSourceCheckout = $sourceCheckout
    nativeAotSourceHead = $actualCommit
    lockedSourceCommit = $sourceCommit
    objects = $objectEvidence
    imports = $imports
    rawCandidateEvidence = $importEvidence
    contractSource = $contractSource
    contractObject = $contractObject
    contractObjectSha256 = Get-Hash $contractObject
    compiler = (Find-Tool "cl.exe")
    compilerVersion = (cmd.exe /d /c "`"$(Find-Tool 'cl.exe')`" 2>&1" | Select-Object -First 1).ToString()
    compilerFlags = @("/MT", "/GR-", "/EHs-c-", "/GS-", "/O2", "/Brepro", "/std:c++17", "/DGUIDEXOS_NATIVEAOT_PAL_CONTRACT")
    abi = "MSVC AMD64 / Win64; contract callbacks are C-compatible and pointer-width explicit"
    activeArchive = $null
    rhInitializeCalled = $false
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $OutputRoot "pal-replacement-manifest.json") -Encoding UTF8
$objectEvidence | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputRoot "object-provenance.json") -Encoding UTF8

Write-Output "PAL provenance and ABI contract evidence: $OutputRoot"
Write-Output "Stock Runtime.WorkstationGC.lib SHA-256: $($manifest.stockLibrarySha256)"
Write-Output "Contract object SHA-256: $($manifest.contractObjectSha256)"
Write-Output "Active PAL archive replacement: not authorized"
