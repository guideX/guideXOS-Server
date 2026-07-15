param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path,
    [string]$RuntimePackRoot = $PSScriptRoot,
    [string]$OutputRoot = "",
    [string]$StockRuntimePackRoot = "",
    [string]$ExternalRuntimeRoot = "",
    [switch]$ManagedAllocation,
    [switch]$ManagedRepeatedAllocation,
    [ValidateSet("Primary64KiB", "Small4KiB")]
    [string]$HeapConfiguration = "Primary64KiB",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Normalize-CoffArchive([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $signature = [System.Text.Encoding]::ASCII.GetBytes("!<arch>`n")
    $signatureMatches = $bytes.Length -ge $signature.Length
    for ($i = 0; $signatureMatches -and $i -lt $signature.Length; $i++) {
        if ($bytes[$i] -ne $signature[$i]) { $signatureMatches = $false }
    }
    if (-not $signatureMatches) {
        throw "Adapted runtime library is not an archive: $Path"
    }

    $offset = $signature.Length
    while ($offset -lt $bytes.Length) {
        if ($offset + 60 -gt $bytes.Length -or $bytes[$offset + 58] -ne 0x60 -or $bytes[$offset + 59] -ne 0x0A) {
            throw "Malformed COFF archive member header at offset ${offset}: $Path"
        }
        for ($i = 0; $i -lt 12; $i++) { $bytes[$offset + 16 + $i] = 0x20 }
        $bytes[$offset + 16] = [byte][char]'0'
        $sizeText = [System.Text.Encoding]::ASCII.GetString($bytes, $offset + 48, 10).Trim()
        $memberSize = 0
        if (-not [int]::TryParse($sizeText, [Globalization.NumberStyles]::Integer, [Globalization.CultureInfo]::InvariantCulture, [ref]$memberSize) -or $memberSize -lt 0) {
            throw "Malformed COFF archive member size at offset ${offset}: $Path"
        }
        $next = $offset + 60 + $memberSize
        if (($next % 2) -ne 0) { $next++ }
        if ($next -le $offset -or $next -gt $bytes.Length) { throw "COFF archive member extends beyond the file: $Path" }
        $offset = $next
    }
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

function Assert-WithinRoot([string]$Path, [string]$Root, [string]$Label) {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escapes its allowed root: $fullPath"
    }
}

function Find-VcVars64 {
    foreach ($path in @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    )) {
        if (Test-Path -LiteralPath $path) { return $path }
    }
    throw "Visual C++ vcvars64.bat was not found."
}

function Find-Objcopy {
    foreach ($candidate in @(
        "C:\mingw64\bin\objcopy.exe",
        (Get-Command objcopy -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
    )) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    throw "COFF objcopy was not found; the runtime-pack build needs objcopy to rename the replaced stock entry symbols."
}

function Find-StockRuntimePack([string]$RequestedRoot) {
    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return [System.IO.Path]::GetFullPath($RequestedRoot)
    }

    $nugetRoot = if ([string]::IsNullOrWhiteSpace($env:NUGET_PACKAGES)) {
        Join-Path $env:USERPROFILE ".nuget\packages"
    } else { $env:NUGET_PACKAGES }
    return Join-Path $nugetRoot "runtime.win-x64.microsoft.dotnet.ilcompiler\9.0.0"
}

function Read-Lock() {
    $lockPath = Join-Path $RuntimePackRoot "runtime-pack.lock.json"
    if (-not (Test-Path -LiteralPath $lockPath)) { throw "Runtime-pack lock file not found: $lockPath" }
    return Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
}

function Assert-StockPack([string]$Root, $Lock) {
    if (-not (Test-Path -LiteralPath $Root)) { throw "Stock NativeAOT runtime pack not found: $Root" }
    $nuspec = Join-Path $Root "runtime.win-x64.microsoft.dotnet.ilcompiler.nuspec"
    if (-not (Test-Path -LiteralPath $nuspec)) { throw "Runtime-pack nuspec missing: $nuspec" }
    $nuspecText = Get-Content -LiteralPath $nuspec -Raw
    if ($nuspecText -notmatch '<version>9\.0\.0</version>') { throw "Runtime-pack version is not 9.0.0: $nuspec" }
    if ($nuspecText -notmatch '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3') {
        throw "Runtime-pack source commit does not match the lock file: $nuspec"
    }

    foreach ($property in $Lock.runtimePack.files.PSObject.Properties) {
        $relative = $property.Name.Replace('/', '\')
        $path = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $path)) { throw "Locked runtime-pack input is missing: $path" }
        $actual = Get-Hash $path
        if ($actual -ne $property.Value.sha256.ToUpperInvariant()) {
            throw "Locked runtime-pack hash mismatch for $relative. Expected $($property.Value.sha256), got $actual"
        }
        $lengthProperty = $property.Value.PSObject.Properties['length']
        if ($null -ne $lengthProperty -and $null -ne $lengthProperty.Value -and (Get-Item -LiteralPath $path).Length -ne [int64]$lengthProperty.Value) {
            throw "Locked runtime-pack length mismatch for $relative"
        }
    }
}

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$RuntimePackRoot = [System.IO.Path]::GetFullPath($RuntimePackRoot)
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $RepoRoot "out\dotnet\runtime-pack" }
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
Assert-WithinRoot $RuntimePackRoot $RepoRoot "Runtime-pack source"
Assert-WithinRoot $OutputRoot (Join-Path $RepoRoot "out\dotnet") "Runtime-pack output"

$lock = Read-Lock
$stockRoot = Find-StockRuntimePack $StockRuntimePackRoot
Assert-StockPack $stockRoot $lock
$externalCommit = $null
$managedHeapBytes = if ($HeapConfiguration -eq "Primary64KiB") { 65536 } else { 4096 }
if ($ManagedRepeatedAllocation) { $ManagedAllocation = $true }

if (-not [string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) {
    $ExternalRuntimeRoot = [System.IO.Path]::GetFullPath($ExternalRuntimeRoot)
    if (-not (Test-Path -LiteralPath (Join-Path $ExternalRuntimeRoot ".git"))) {
        throw "External runtime root is not a Git checkout: $ExternalRuntimeRoot"
    }
    $externalCommit = (& git -C $ExternalRuntimeRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Unable to read external runtime checkout revision: $ExternalRuntimeRoot" }
    if ($externalCommit -ne $lock.ilCompiler.commit) {
        throw "External runtime checkout revision mismatch. Expected $($lock.ilCompiler.commit), got $externalCommit"
    }
}

$source = Join-Path $RuntimePackRoot $lock.platformObject.source.Replace('/', '\')
if (-not (Test-Path -LiteralPath $source)) { throw "Runtime-pack platform source not found: $source" }

if ($Clean -and (Test-Path -LiteralPath $OutputRoot)) { Remove-Item -LiteralPath $OutputRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$object = Join-Path $OutputRoot "guidexos_nativeaot_platform.obj"
$batch = Join-Path $OutputRoot "build-runtime-pack.bat"
$sdkOutput = Join-Path $OutputRoot "sdk"
$sdkBatch = Join-Path $OutputRoot "build-runtime-pack-sdk.bat"
$manifestPath = Join-Path $OutputRoot "runtime-pack.manifest.json"
$vcvars = Find-VcVars64
$objcopy = Find-Objcopy

$stockSdk = Join-Path $stockRoot "sdk"
New-Item -ItemType Directory -Force -Path $sdkOutput | Out-Null
$sdkFiles = @(
    "bootstrapper.obj",
    "eventpipe-disabled.lib",
    "Runtime.VxsortEnabled.lib",
    "standalonegc-disabled.lib",
    "System.Globalization.Native.Aot.lib",
    "System.IO.Compression.Native.Aot.lib",
    "zlibstatic.lib"
)
foreach ($fileName in $sdkFiles) {
    Copy-Item -LiteralPath (Join-Path $stockSdk $fileName) -Destination (Join-Path $sdkOutput $fileName) -Force
}
Get-ChildItem -LiteralPath $stockSdk -Filter "*.dll" -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $sdkOutput $_.Name) -Force
}
$stockRuntimeLibrary = Join-Path $stockSdk "Runtime.WorkstationGC.lib"
$adaptedRuntimeLibrary = Join-Path $sdkOutput "Runtime.WorkstationGC.lib"
$removedRuntimeMembers = @(
    "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\EHHelpers.cpp.obj",
    "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\thread.cpp.obj"
)
$threadMember = Join-Path $OutputRoot "thread.cpp.obj"
$ehMember = Join-Path $OutputRoot "EHHelpers.cpp.obj"
$threadRenamedMember = Join-Path $OutputRoot "thread.cpp.obj.renamed.obj"
$ehRenamedMember = Join-Path $OutputRoot "EHHelpers.cpp.obj.renamed.obj"
$allocFastMember = Join-Path $OutputRoot "AllocFast.asm.obj"
$allocFastRenamedMember = Join-Path $OutputRoot "AllocFast.asm.obj.renamed.obj"
if ($ManagedAllocation) {
    $removedRuntimeMembers += "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\amd64\AllocFast.asm.obj"
}
$threadMemberArguments = "`"$threadMember`""
$ehMemberArguments = "`"$ehMember`""
$allocFastMemberArguments = if ($ManagedAllocation) { " `"$allocFastRenamedMember`"" } else { "" }
$removeArguments = ($removedRuntimeMembers | ForEach-Object { "/REMOVE:`"$_`"" }) -join " "
$sdkLines = @(
    "@echo off",
    "setlocal",
    "call `"$vcvars`" >nul",
    "if errorlevel 1 exit /b %errorlevel%",
    "lib.exe /nologo /extract:`"$($removedRuntimeMembers[0])`" `"$stockRuntimeLibrary`" /out:$ehMemberArguments",
    "if errorlevel 1 exit /b %errorlevel%",
    "lib.exe /nologo /extract:`"$($removedRuntimeMembers[1])`" `"$stockRuntimeLibrary`" /out:$threadMemberArguments",
    "if errorlevel 1 exit /b %errorlevel%"
)
if ($ManagedAllocation) {
    $sdkLines += @(
        "lib.exe /nologo /extract:`"$($removedRuntimeMembers[2])`" `"$stockRuntimeLibrary`" /out:`"$allocFastMember`"",
        "if errorlevel 1 exit /b %errorlevel%"
    )
}
$sdkLines += "exit /b %errorlevel%"
$sdkLines | Set-Content -LiteralPath $sdkBatch -Encoding ASCII
& $sdkBatch
if ($LASTEXITCODE -ne 0) { throw "GuideXOS runtime-pack runtime-member extraction failed with exit code $LASTEXITCODE" }
Copy-Item -LiteralPath $threadMember -Destination $threadRenamedMember -Force
Copy-Item -LiteralPath $ehMember -Destination $ehRenamedMember -Force
if ($ManagedAllocation) {
    Copy-Item -LiteralPath $allocFastMember -Destination $allocFastRenamedMember -Force
}
& $objcopy --redefine-sym RhpReversePInvoke=guideXosStockRhpReversePInvoke --redefine-sym RhpReversePInvokeReturn=guideXosStockRhpReversePInvokeReturn --redefine-sym RhpReversePInvokeAttachOrTrapThread2=guideXosStockRhpReversePInvokeAttachOrTrapThread2 $threadRenamedMember
if ($LASTEXITCODE -ne 0) { throw "COFF symbol adaptation failed for thread.cpp.obj" }
& $objcopy --redefine-sym RhpFallbackFailFast=guideXosStockRhpFallbackFailFast $ehRenamedMember
if ($LASTEXITCODE -ne 0) { throw "COFF symbol adaptation failed for EHHelpers.cpp.obj" }
if ($ManagedAllocation) {
    & $objcopy --redefine-sym RhpNewArray=guideXosStockRhpNewArray $allocFastRenamedMember
    if ($LASTEXITCODE -ne 0) { throw "COFF symbol adaptation failed for AllocFast.asm.obj" }
}
$sdkLines = @(
    "@echo off",
    "setlocal",
    "call `"$vcvars`" >nul",
    "if errorlevel 1 exit /b %errorlevel%",
    "lib.exe /nologo /OUT:`"$adaptedRuntimeLibrary`" `"$stockRuntimeLibrary`" $removeArguments `"$threadRenamedMember`" `"$ehRenamedMember`"$allocFastMemberArguments",
    "exit /b %errorlevel%"
)
$sdkLines | Set-Content -LiteralPath $sdkBatch -Encoding ASCII
& $sdkBatch
if ($LASTEXITCODE -ne 0) { throw "GuideXOS runtime-pack adapted runtime library build failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $adaptedRuntimeLibrary)) { throw "Adapted runtime library was not produced: $adaptedRuntimeLibrary" }
Normalize-CoffArchive $adaptedRuntimeLibrary

$lines = @(
    "@echo off",
    "setlocal",
    "call `"$vcvars`" >nul",
    "if errorlevel 1 exit /b %errorlevel%",
    "cl.exe /nologo /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro $(if ($ManagedAllocation) { "/DGUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION /DGUIDEXOS_MANAGED_HEAP_BYTES=$managedHeapBytes $(if ($ManagedRepeatedAllocation) { '/DGUIDEXOS_NATIVEAOT_MANAGED_REPEATED_ALLOCATION' } else { '' })" } else { '' }) /Fo:`"$object`" `"$source`"",
    "exit /b %errorlevel%"
)
$lines | Set-Content -LiteralPath $batch -Encoding ASCII
& $batch
if ($LASTEXITCODE -ne 0) { throw "guideXOS runtime-pack platform object build failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $object)) { throw "Runtime-pack object was not produced: $object" }

$sourceHash = Get-Hash $source
$objectHash = Get-Hash $object
$lockPath = Join-Path $RuntimePackRoot "runtime-pack.lock.json"
$manifest = [ordered]@{
    schemaVersion = 1
    identity = if ($ManagedRepeatedAllocation) { "guidexos-nativeaot-runtime-pack-amd64-hostlog-repeated-allocation-nocollection-v1" } elseif ($ManagedAllocation) { "guidexos-nativeaot-runtime-pack-amd64-hostlog-allocating-nocollection-v1" } else { "guidexos-nativeaot-runtime-pack-amd64-hostlog-nonallocating-v1" }
    architecture = $lock.architecture
    targetFramework = $lock.targetFramework
    runtimeIdentifier = $lock.runtimeIdentifier
    sourceRoot = $RuntimePackRoot
    source = $source
    sourceSha256 = $sourceHash
    object = $object
    objectSha256 = $objectHash
    sdkPath = $sdkOutput
    sdkManagedAssembliesCopied = $true
    adaptedRuntimeLibrary = $adaptedRuntimeLibrary
    adaptedRuntimeLibrarySha256 = Get-Hash $adaptedRuntimeLibrary
    removedRuntimeMembers = $removedRuntimeMembers
    renamedRuntimeSymbols = [ordered]@{
        "RhpReversePInvoke" = "guideXosStockRhpReversePInvoke"
        "RhpReversePInvokeReturn" = "guideXosStockRhpReversePInvokeReturn"
        "RhpReversePInvokeAttachOrTrapThread2" = "guideXosStockRhpReversePInvokeAttachOrTrapThread2"
        "RhpFallbackFailFast" = "guideXosStockRhpFallbackFailFast"
        "RhpNewArray" = if ($ManagedAllocation) { "guideXosStockRhpNewArray" } else { $null }
    }
    objcopy = $objcopy
    objcopySha256 = Get-Hash $objcopy
    lockFile = $lockPath
    lockFileSha256 = Get-Hash $lockPath
    stockRuntimePackRoot = $stockRoot
    stockRuntimePackPackage = $lock.runtimePack.package
    stockRuntimePackVersion = $lock.runtimePack.version
    stockRuntimePackSourceCommit = $lock.runtimePack.commit
    externalRuntimeRoot = if ([string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) { $null } else { $ExternalRuntimeRoot }
    externalRuntimeCommit = if ([string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) { $null } else { $externalCommit }
    platformObject = $lock.platformObject
    liveWindowsImportsReplaced = @("FlsGetValue", "FlsSetValue", "RhpReversePInvoke", "RhpReversePInvokeReturn")
    managedAllocation = [bool]$ManagedAllocation
    allocationMode = if ($ManagedRepeatedAllocation) { "Repeated" } elseif ($ManagedAllocation) { "Allocating" } else { "NonAllocating" }
    allocationStrategy = if ($ManagedRepeatedAllocation) { "bounded-static-image-backed-no-collection-with-proof-specific-preflight-oom" } elseif ($ManagedAllocation) { "bounded-static-image-backed-no-collection" } else { "none" }
    managedHeapConfiguration = if ($ManagedAllocation) { $HeapConfiguration } else { "None" }
    managedHeapBytes = if ($ManagedAllocation) { $managedHeapBytes } else { 0 }
    managedArrayLength = if ($ManagedRepeatedAllocation) { 256 } elseif ($ManagedAllocation) { 24 } else { 0 }
    managedObjectSize = if ($ManagedRepeatedAllocation) { 280 } elseif ($ManagedAllocation) { 40 } else { 0 }
    managedExceptions = $false
    managedThreads = $false
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding ASCII

Write-Host "[runtime-pack] built guideXOS AMD64 platform object" -ForegroundColor Green
Write-Host "[runtime-pack] object=$object" -ForegroundColor Cyan
Write-Host "[runtime-pack] sha256=$objectHash" -ForegroundColor Cyan
Write-Host "[runtime-pack] manifest=$manifestPath" -ForegroundColor Cyan
Write-Output $object
