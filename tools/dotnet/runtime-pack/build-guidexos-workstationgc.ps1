param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path,
    [string]$OutputRoot = "",
    [string]$StockRuntimePackRoot = "",
    [string]$ExternalRuntimeRoot = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-Hash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-WithinRoot([string]$Path, [string]$Root, [string]$Label) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escapes its allowed root: $fullPath"
    }
}

function Find-Tool([string]$Name) {
    $candidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\$Name",
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\$Name"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw "MSVC tool not found: $Name"
}

function Find-VcVars64 {
    foreach ($candidate in @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    )) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw "Visual C++ vcvars64.bat was not found."
}

function Normalize-CoffArchive([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $signature = [Text.Encoding]::ASCII.GetBytes("!<arch>`n")
    if ($bytes.Length -lt $signature.Length) { throw "Not a COFF archive: $Path" }
    for ($index = 0; $index -lt $signature.Length; $index++) {
        if ($bytes[$index] -ne $signature[$index]) { throw "Not a COFF archive: $Path" }
    }
    $offset = $signature.Length
    while ($offset -lt $bytes.Length) {
        if ($offset + 60 -gt $bytes.Length -or
            $bytes[$offset + 58] -ne 0x60 -or $bytes[$offset + 59] -ne 0x0A) {
            throw "Malformed COFF archive member at offset ${offset}: $Path"
        }
        for ($index = 0; $index -lt 12; $index++) { $bytes[$offset + 16 + $index] = 0x20 }
        $bytes[$offset + 16] = [byte][char]'0'
        $sizeText = [Text.Encoding]::ASCII.GetString($bytes, $offset + 48, 10).Trim()
        $memberSize = 0
        if (-not [int]::TryParse($sizeText, [Globalization.NumberStyles]::Integer,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$memberSize)) {
            throw "Malformed COFF archive member size at offset ${offset}: $Path"
        }
        $next = $offset + 60 + $memberSize
        if (($next % 2) -ne 0) { $next++ }
        if ($next -le $offset -or $next -gt $bytes.Length) {
            throw "COFF archive member extends beyond file: $Path"
        }
        $offset = $next
    }
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Get-DumpbinDefinedSymbols([string]$Path) {
    $symbols = [Collections.Generic.List[string]]::new()
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\S+\s+\S+\s+SECT\S*\s+.*\|\s+(.+)$') {
            $value = $matches[1].Trim()
            $value = ($value -split '\s+\(')[0].Trim()
            if ($value.Length -gt 0) { $symbols.Add($value) }
        }
    }
    return $symbols.ToArray()
}

function Get-ExactGcenvSymbols([string]$Path) {
    $symbols = Get-DumpbinDefinedSymbols $Path
    return @($symbols | Where-Object {
        $_ -match '^\?g_SystemInfo@@' -or
        $_ -match '^\?[^@]+@(?:GCToOSInterface|GCEvent|CLRCriticalSection)@@' -or
        $_ -match '^\?\?[01](?:GCEvent|CLRCriticalSection)@@'
    } | Sort-Object -Unique)
}

function Get-DumpbinUndefinedSymbols([string]$Path) {
    $records = [Collections.Generic.List[string]]::new()
    $current = $null
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(?:SECT\S*|UNDEF)\b') {
            if ($null -ne $current) { $records.Add($current.Trim()) }
            if ($line -match '\|\s*(.*)$') {
                $current = $matches[1].Trim()
            } else {
                $current = ''
            }
            if ($line -notmatch '\bUNDEF\b') { $current = $null }
        } elseif ($null -ne $current) {
            $current += ' ' + $line.Trim()
        }
    }
    if ($null -ne $current) { $records.Add($current.Trim()) }
    return $records.ToArray()
}

function Invoke-VcBatch([string]$Path, [string[]]$Lines) {
    [IO.File]::WriteAllLines($Path, $Lines, [Text.ASCIIEncoding]::new())
    & $Path
    if ($LASTEXITCODE -ne 0) { throw "Build command failed ($LASTEXITCODE): $Path" }
}

$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $RepoRoot "out\dotnet\gc-platform-object-replacement"
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
Assert-WithinRoot $OutputRoot (Join-Path $RepoRoot "out\dotnet") "Evidence output"

$runtimePackRoot = Join-Path $RepoRoot "tools\dotnet\runtime-pack"
$lockPath = Join-Path $runtimePackRoot "runtime-pack.lock.json"
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
    throw "Stock Runtime.WorkstationGC.lib hash mismatch before replacement build."
}

$sourceRoot = if ([string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) {
    Join-Path $RepoRoot "out\dotnet\gc-feasibility-baseline\source-extract"
} else { [IO.Path]::GetFullPath($ExternalRuntimeRoot) }
$headerRoot = if ([string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) {
    Join-Path $RepoRoot "out\dotnet\gc-feasibility-baseline\nativeaot-runtime"
} else { $sourceRoot }
$sourceCommit = $lock.ilCompiler.commit
if (-not [string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) {
    $sourceCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $sourceCommit -ne $lock.ilCompiler.commit) {
        throw "NativeAOT source checkout does not match locked commit $($lock.ilCompiler.commit)."
    }
}

$stockEvidence = Join-Path $OutputRoot "stock"
$extracted = Join-Path $OutputRoot "extracted\all"
$symbols = Join-Path $OutputRoot "symbols\all"
$imports = Join-Path $OutputRoot "imports\all"
$rebuilt = Join-Path $OutputRoot "rebuilt"
if ($Clean -and (Test-Path -LiteralPath $OutputRoot)) {
    Remove-Item -LiteralPath $OutputRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stockEvidence,$extracted,$symbols,$imports,$rebuilt | Out-Null

$vcvars = Find-VcVars64
$lib = Find-Tool "lib.exe"
$dumpbin = Find-Tool "dumpbin.exe"
$stockCopy = Join-Path $stockEvidence "Runtime.WorkstationGC.stock.lib"
Copy-Item -LiteralPath $stockLibrary -Destination $stockCopy -Force
$stockMembers = @(& $lib /nologo /list $stockLibrary)
$stockMembers | Set-Content -LiteralPath (Join-Path $stockEvidence "stock-members.txt") -Encoding ASCII
$gcenvMembers = @($stockMembers | Where-Object { $_ -match 'gcenv\.windows\.cpp\.obj$' })
if ($gcenvMembers.Count -ne 1) { throw "Expected one gcenv.windows.cpp.obj member; found $($gcenvMembers.Count)." }
$gcenvMember = $gcenvMembers[0]

foreach ($member in $stockMembers) {
    $leaf = Split-Path $member -Leaf
    $destination = Join-Path $extracted $leaf
    & $lib /nologo /extract:"$member" "$stockLibrary" /out:"$destination" *> $null
    if ($LASTEXITCODE -ne 0) { throw "Unable to extract stock archive member: $member" }
}
foreach ($obj in Get-ChildItem -LiteralPath $extracted -Filter "*.obj") {
    & $dumpbin /nologo /symbols $obj.FullName | Set-Content (Join-Path $symbols ($obj.Name + ".txt")) -Encoding ASCII
    & $dumpbin /nologo /imports $obj.FullName | Set-Content (Join-Path $imports ($obj.Name + ".txt")) -Encoding ASCII
}
$gcenvObj = Join-Path $extracted (Split-Path $gcenvMember -Leaf)
$gcenvHeadersPath = Join-Path $OutputRoot "symbols\gcenv.windows.cpp.obj.dumpbin-headers.txt"
$gcenvDirectivesPath = Join-Path $OutputRoot "symbols\gcenv.windows.cpp.obj.dumpbin-directives.txt"
$gcenvSymbolsPath = Join-Path $OutputRoot "symbols\gcenv.windows.cpp.obj.dumpbin-symbols.txt"
& $dumpbin /nologo /headers $gcenvObj | Set-Content $gcenvHeadersPath -Encoding ASCII
& $dumpbin /nologo /directives $gcenvObj | Set-Content $gcenvDirectivesPath -Encoding ASCII
& $dumpbin /nologo /symbols $gcenvObj | Set-Content $gcenvSymbolsPath -Encoding ASCII
$gcenvDefinedSymbols = @(Get-DumpbinDefinedSymbols $gcenvSymbolsPath)
$gcenvUndefinedSymbols = @(Get-DumpbinUndefinedSymbols $gcenvSymbolsPath)
[ordered]@{
    schemaVersion = 1
    object = $gcenvObj
    definedCount = $gcenvDefinedSymbols.Count
    defined = $gcenvDefinedSymbols
    undefinedCount = $gcenvUndefinedSymbols.Count
    undefined = $gcenvUndefinedSymbols
} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutputRoot "symbols\gcenv.windows.cpp.obj.inventory.json") -Encoding UTF8

$common = "/nologo /std:c++17 /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro"
$gcDefines = "/DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /D_FEATURE_NATIVEAOT /DNATIVEAOT /DTARGET_AMD64 /DHOST_AMD64 /DHOST_64BIT /D_WIN64"
$gcIncludes = "/I`"$(Join-Path $headerRoot 'src\coreclr\gc')`" /I`"$(Join-Path $headerRoot 'src\coreclr\gc\env')`" /I`"$(Join-Path $headerRoot 'src\coreclr\nativeaot\Runtime')`" /I`"$(Join-Path $headerRoot 'src\native')`""
$kernelInclude = "/I`"$(Join-Path $RepoRoot 'kernel\core\include')`""
$obj = @{}
$obj.guidexos_gcenv = Join-Path $rebuilt "guidexos_gcenv.obj"
$obj.guidexos_gc_platform_services = Join-Path $rebuilt "guidexos_gc_platform_services.obj"
$obj.guidexos_virtual_memory_region = Join-Path $rebuilt "guidexos_virtual_memory_region.obj"
$obj.guidexos_nativeaot_virtual_memory_adapter = Join-Path $rebuilt "guidexos_nativeaot_virtual_memory_adapter.obj"
$obj.guidexos_event = Join-Path $rebuilt "guidexos_event.obj"
$obj.guidexos_nativeaot_event_adapter = Join-Path $rebuilt "guidexos_nativeaot_event_adapter.obj"
$obj.guidexos_mutex = Join-Path $rebuilt "guidexos_mutex.obj"
$obj.guidexos_nativeaot_critical_section_adapter = Join-Path $rebuilt "guidexos_nativeaot_critical_section_adapter.obj"
$obj.guidexos_native_thread = Join-Path $rebuilt "guidexos_native_thread.obj"
$obj.guidexos_local_storage = Join-Path $rebuilt "guidexos_local_storage.obj"
$obj.guidexos_nativeaot_fls_adapter = Join-Path $rebuilt "guidexos_nativeaot_fls_adapter.obj"
$obj.guidexos_nativeaot_thread_adapter = Join-Path $rebuilt "guidexos_nativeaot_thread_adapter.obj"

$compileBatch = Join-Path $rebuilt "build-guidexos-workstationgc-objects.bat"
$compileLines = @("@echo off", "setlocal", "call `"$vcvars`" >nul", "if errorlevel 1 exit /b %errorlevel%")
function Add-Compile([string]$Output, [string]$Source, [string]$Flags) {
    $script:compileLines += "cl.exe $common $Flags /Fo:`"$Output`" `"$Source`""
    $script:compileLines += "if errorlevel 1 exit /b %errorlevel%"
}
Add-Compile $obj.guidexos_gcenv (Join-Path $runtimePackRoot "src\gcenv\guidexos_gcenv.cpp") "$gcDefines $gcIncludes"
Add-Compile $obj.guidexos_gc_platform_services (Join-Path $runtimePackRoot "src\gcenv\guidexos_gc_platform_services.cpp") "/DGXOS_BARE_METAL"
Add-Compile $obj.guidexos_virtual_memory_region (Join-Path $RepoRoot "runtime\memory\guidexos_virtual_memory_region_baremetal.cpp") "/DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY $kernelInclude"
Add-Compile $obj.guidexos_nativeaot_virtual_memory_adapter (Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_virtual_memory_adapter.cpp") "$gcDefines"
Add-Compile $obj.guidexos_event (Join-Path $RepoRoot "runtime\synchronization\guidexos_event_baremetal.cpp") "/DGXOS_BARE_METAL"
Add-Compile $obj.guidexos_nativeaot_event_adapter (Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_event_adapter.cpp") "$gcDefines"
Add-Compile $obj.guidexos_mutex (Join-Path $RepoRoot "runtime\synchronization\guidexos_mutex_baremetal.cpp") "/DGXOS_BARE_METAL"
Add-Compile $obj.guidexos_nativeaot_critical_section_adapter (Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_critical_section_adapter.cpp") "$gcDefines"
Add-Compile $obj.guidexos_native_thread (Join-Path $RepoRoot "runtime\thread\guidexos_native_thread_baremetal.cpp") "/DGXOS_BARE_METAL"
Add-Compile $obj.guidexos_local_storage (Join-Path $RepoRoot "runtime\local_storage\guidexos_local_storage.cpp") "/DGXOS_BARE_METAL"
Add-Compile $obj.guidexos_nativeaot_fls_adapter (Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_fls_adapter.cpp") "/DGXOS_BARE_METAL"
Add-Compile $obj.guidexos_nativeaot_thread_adapter (Join-Path $runtimePackRoot "src\platform\guidexos_nativeaot_thread_adapter.cpp") "/DGXOS_BARE_METAL"
$compileLines += "exit /b 0"
Invoke-VcBatch $compileBatch $compileLines

$adaptedLibrary = Join-Path $rebuilt "Runtime.WorkstationGC.lib"
$archiveBatch = Join-Path $rebuilt "build-guidexos-workstationgc-archive.bat"
$archiveObjects = ($obj.Values | ForEach-Object { "`"$_`"" }) -join " "
$archiveLines = @(
    "@echo off", "setlocal", "call `"$vcvars`" >nul", "if errorlevel 1 exit /b %errorlevel%",
    "lib.exe /nologo /OUT:`"$adaptedLibrary`" `"$stockLibrary`" /REMOVE:`"$gcenvMember`" $archiveObjects",
    "exit /b %errorlevel%"
)
Invoke-VcBatch $archiveBatch $archiveLines
Normalize-CoffArchive $adaptedLibrary
if ((Get-Hash $stockLibrary) -ne $expectedStockHash) { throw "Stock archive changed during reconstruction." }
$adaptedMembers = @(& $lib /nologo /list $adaptedLibrary)
$adaptedMembers | Set-Content -LiteralPath (Join-Path $rebuilt "adapted-members.txt") -Encoding ASCII

$rebuiltSymbolDir = Join-Path $OutputRoot "symbols\rebuilt"
$rebuiltImportDir = Join-Path $OutputRoot "imports\rebuilt"
New-Item -ItemType Directory -Force -Path $rebuiltSymbolDir,$rebuiltImportDir | Out-Null
foreach ($path in $obj.Values) {
    $leaf = Split-Path $path -Leaf
    & $dumpbin /nologo /symbols $path | Set-Content (Join-Path $rebuiltSymbolDir ($leaf + ".txt")) -Encoding ASCII
    & $dumpbin /nologo /imports $path | Set-Content (Join-Path $rebuiltImportDir ($leaf + ".txt")) -Encoding ASCII
}

$stockGcenvSymbols = Get-ExactGcenvSymbols (Join-Path $OutputRoot "symbols\gcenv.windows.cpp.obj.dumpbin-symbols.txt")
$requiredSymbols = @($stockGcenvSymbols)
$replacementSymbols = Get-ExactGcenvSymbols (Join-Path $rebuiltSymbolDir "guidexos_gcenv.obj.txt")
$bindings = foreach ($symbol in $requiredSymbols) {
    $present = $replacementSymbols -contains $symbol
    [ordered]@{
        symbol = $symbol
        stockMember = $gcenvMember
        replacementObject = Split-Path $obj.guidexos_gcenv -Leaf
        status = if ($present) { "replaced" } else { "missing" }
    }
}
$bindings | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutputRoot "symbols\symbol-binding.json") -Encoding UTF8

$providerMap = @{}
foreach ($file in Get-ChildItem (Join-Path $OutputRoot "symbols") -Recurse -Filter "*.txt") {
    if ($file.FullName -match 'gcenv\.windows\.cpp\.obj') { continue }
    foreach ($symbol in Get-DumpbinDefinedSymbols $file.FullName) {
        if (-not $providerMap.ContainsKey($symbol)) { $providerMap[$symbol] = [Collections.Generic.List[string]]::new() }
        if (-not $providerMap[$symbol].Contains($file.FullName.Substring($OutputRoot.Length + 1))) {
            $providerMap[$symbol].Add($file.FullName.Substring($OutputRoot.Length + 1))
        }
    }
}
$duplicateDefinitions = @()
foreach ($binding in $bindings) {
    $providers = @($providerMap[$binding.symbol] | Sort-Object)
    if ($providers.Count -gt 1) {
        $duplicateDefinitions += [ordered]@{ symbol = $binding.symbol; providers = $providers }
    }
}
$missingDefinitions = @($bindings | Where-Object status -eq "missing")
[ordered]@{
    requiredSymbolCount = $requiredSymbols.Count
    bindings = $bindings
    duplicateStrongDefinitions = $duplicateDefinitions
    missingDefinitions = $missingDefinitions
    windowsGcenvMemberPresent = @($adaptedMembers | Where-Object { $_ -match 'gcenv\.windows\.cpp\.obj$' }).Count -ne 0
} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputRoot "symbols\symbol-binding-report.json") -Encoding UTF8

$forbidden = @(
    "VirtualAlloc", "VirtualFree", "VirtualQuery", "VirtualProtect", "DiscardVirtualMemory",
    "VirtualAllocExNuma", "CreateEventExW", "CreateEventW", "PalCreateEventW", "PalCompatibleWaitAny",
    "CloseHandle", "SetEvent", "ResetEvent", "WaitForSingleObjectEx", "WaitForSingleObject",
    "CreateThread", "FlsAlloc", "FlsFree", "FlsGetValue", "FlsSetValue",
    "InitializeCriticalSection", "InitializeCriticalSectionEx", "EnterCriticalSection", "LeaveCriticalSection", "DeleteCriticalSection",
    "QueryPerformanceCounter", "QueryPerformanceFrequency", "GetCurrentThreadId", "GetCurrentThread",
    "GetCurrentProcessorNumberEx", "GetLogicalProcessorInformation", "GetLogicalProcessorInformationEx",
    "GetSystemInfo", "GlobalMemoryStatusEx", "GetTickCount64", "GetLastError", "GetCurrentProcess",
    "GetCurrentProcessId", "SwitchToThread", "SleepEx"
)
$importFindings = foreach ($file in Get-ChildItem (Join-Path $OutputRoot "symbols") -Recurse -Filter "*.txt" | Where-Object {
    $_.FullName -notmatch 'gcenv\.windows\.cpp\.obj'
}) {
    $undefinedRecords = @(Get-DumpbinUndefinedSymbols $file.FullName)
    foreach ($name in $forbidden) {
        $escapedName = [Regex]::Escape($name)
        $hits = @($undefinedRecords | Where-Object {
            $_ -match "__imp_$escapedName(?:$|[^A-Za-z0-9_])" -or
            $_ -match "(?<![A-Za-z0-9_])$escapedName(?![A-Za-z0-9_])"
        })
        $win32Hits = @($hits | Where-Object { $_ -match "__imp_$escapedName(?:$|[^A-Za-z0-9_])" })
        if ($win32Hits.Count -gt 0) {
            [ordered]@{
                symbol = $name
                contributingInventory = $file.FullName.Substring($OutputRoot.Length + 1)
                kind = if ($name.StartsWith('Pal', [StringComparison]::Ordinal)) { "NativeAOT PAL dependency" } else { "Win32 import candidate" }
                importLike = $true
                undefinedRecords = $win32Hits
            }
        }
    }
}
$importFindings | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutputRoot "imports\remaining-windows-platform-symbols.json") -Encoding UTF8

$identity = [ordered]@{
    schemaVersion = 1
    strategy = "A-complete-gcenv-object"
    stockPath = $stockLibrary
    stockSha256 = Get-Hash $stockLibrary
    stockLength = (Get-Item -LiteralPath $stockLibrary).Length
    adaptedPath = $adaptedLibrary
    adaptedSha256 = Get-Hash $adaptedLibrary
    adaptedLength = (Get-Item -LiteralPath $adaptedLibrary).Length
    archiveFormat = "COFF archive / !<arch>`n"
    archiveTool = $lib
    compiler = (Find-Tool "cl.exe")
    compilerVersion = (cmd.exe /d /c "`"$(Find-Tool 'cl.exe')`" 2>&1" | Select-Object -First 1).ToString()
    sourceCommit = $sourceCommit
    gcenvMemberRemoved = $gcenvMember
    replacementObject = $obj.guidexos_gcenv
    replacementObjectSha256 = Get-Hash $obj.guidexos_gcenv
    replacementObjects = @($obj.Values | ForEach-Object {
        [ordered]@{ path = $_; sha256 = Get-Hash $_ }
    })
    prohibitedInventoryCount = @($importFindings).Count
    stockUnchanged = ((Get-Hash $stockLibrary) -eq $expectedStockHash)
}
$identity | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputRoot "rebuilt\adapted-identity.json") -Encoding UTF8
$identity | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputRoot "stock\identity.json") -Encoding UTF8

[ordered]@{
    compiler = (Find-Tool "cl.exe")
    linker = $lib
    dumpbin = $dumpbin
    target = "AMD64 COFF/MSVC NativeAOT object ABI"
    runtimeLibrary = "MT_StaticRelease"
    exception = "EHs-c-"
    rtti = "GR-"
    securityCookie = "GS-"
    optimization = "O2 Oi Brepro"
    cxx = "c++17"
    defines = @("FEATURE_NATIVEAOT", "NATIVEAOT", "TARGET_AMD64", "HOST_AMD64", "HOST_64BIT", "_WIN64", "GXOS_BARE_METAL", "GXOS_TRUE_VIRTUAL_MEMORY")
} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $rebuilt "build-configuration.json") -Encoding UTF8

Write-Output ("Adapted Workstation GC: {0}" -f $adaptedLibrary)
Write-Output ("Adapted SHA-256: {0}" -f $identity.adaptedSha256)
Write-Output ("Replacement SHA-256: {0}" -f $identity.replacementObjectSha256)
Write-Output ("Required exact symbols: {0}; missing: {1}; duplicates: {2}" -f $requiredSymbols.Count, $missingDefinitions.Count, $duplicateDefinitions.Count)
Write-Output ("Windows platform inventory findings: {0}" -f @($importFindings).Count)
