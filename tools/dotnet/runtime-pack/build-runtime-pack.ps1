param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path,
    [string]$RuntimePackRoot = $PSScriptRoot,
    [string]$OutputRoot = "",
    [string]$StockRuntimePackRoot = "",
    [string]$ExternalRuntimeRoot = "",
    [switch]$ManagedAllocation,
    [switch]$ManagedRepeatedAllocation,
    [switch]$C188R12R4FactoryAttribution,
    [switch]$C188R12R4R5GcOwnershipProof,
    [switch]$C188R12R4R5R1InitDiagnostics,
    [switch]$ProductionApplication,
    [switch]$PersistentCompositeLifecycle,
    [switch]$ThreadStaticLifecycleDiagnostics,
    [switch]$NativeAotFpRepair,
    [ValidateSet("Primary64KiB", "Primary128KiB", "Primary256KiB", "Primary4MiB", "Primary8MiB", "Small4KiB")]
    [string]$HeapConfiguration = "Primary64KiB",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-Hash([string]$Path) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha256.ComputeHash([System.IO.File]::ReadAllBytes($Path))) -replace '-', '').ToUpperInvariant() }
    finally { $sha256.Dispose() }
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

function Replace-ExactOneSource([string]$Text, [string]$Pattern,
                                [string]$Replacement, [string]$Description) {
    $count = [System.Text.RegularExpressions.Regex]::Matches(
        $Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline).Count
    if ($count -ne 1) {
        throw "Pinned NativeAOT source did not contain exactly one $Description site (found $count)."
    }
    return [System.Text.RegularExpressions.Regex]::Replace(
        $Text, $Pattern, $Replacement,
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
}

function Assert-StockPack([string]$Root, $Lock) {
    if (-not (Test-Path -LiteralPath $Root)) { throw "Stock NativeAOT runtime pack not found: $Root" }
    $nuspec = Join-Path $Root "runtime.win-x64.microsoft.dotnet.ilcompiler.nuspec"
    if (-not (Test-Path -LiteralPath $nuspec)) { throw "Runtime-pack nuspec missing: $nuspec" }
    $nuspecText = Get-Content -LiteralPath $nuspec -Raw
    if ($nuspecText -notmatch ('<version>' + [regex]::Escape([string]$Lock.runtimePack.version) + '</version>')) { throw "Runtime-pack version does not match the lock: $nuspec" }
    if ($nuspecText -notmatch [regex]::Escape([string]$Lock.runtimePack.commit)) {
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
if ([string]$lock.architecture -ne "amd64" -or
    [string]$lock.targetFramework -ne "net9.0" -or
    [string]$lock.runtimeIdentifier -ne "win-x64" -or
    [string]$lock.ilCompiler.version -ne "9.0.0" -or
    [string]$lock.ilCompiler.commit -ne "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3" -or
    [string]$lock.runtimePack.version -ne "9.0.0" -or
    [string]$lock.runtimePack.commit -ne "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3") {
    throw "C51 runtime identity lock mismatch: expected NativeAOT 9.0.0 AMD64 Workstation GC source 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3."
}
if ($NativeAotFpRepair -and $null -eq $lock.nativeAotFpRepair) {
    throw "C51 NativeAOT FP repair lock section is missing."
}
$stockRoot = Find-StockRuntimePack $StockRuntimePackRoot
Assert-StockPack $stockRoot $lock
$lockedExternalRuntimeRoot = Join-Path $RepoRoot "out\dotnet\gc-feasibility-baseline\nativeaot-runtime"
if ($NativeAotFpRepair -and [string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) {
    $ExternalRuntimeRoot = $lockedExternalRuntimeRoot
}
$externalCommit = $null
$externalCheckoutHead = $null
$managedHeapBytes = if ($HeapConfiguration -eq "Primary64KiB") { 65536 } elseif ($HeapConfiguration -eq "Primary128KiB") { 131072 } elseif ($HeapConfiguration -eq "Primary256KiB") { 262144 } elseif ($HeapConfiguration -eq "Primary4MiB") { 4194304 } elseif ($HeapConfiguration -eq "Primary8MiB") { 8388608 } else { 4096 }
if ($ManagedRepeatedAllocation) { $ManagedAllocation = $true }
if ($C188R12R4R5GcOwnershipProof -and
    (-not $ManagedAllocation -or -not $ProductionApplication -or
     $ManagedRepeatedAllocation -or $PersistentCompositeLifecycle -or
     $C188R12R4FactoryAttribution -or $ThreadStaticLifecycleDiagnostics)) {
    throw 'C188R12R4R5 GC ownership proof requires the production Workstation GC platform with managed allocation, and excludes arena repetition, persistent composite lifecycle, factory attribution, and TLS diagnostics.'
}
if ($C188R12R4R5R1InitDiagnostics -and
    (-not $C188R12R4R5GcOwnershipProof -or -not $ProductionApplication -or
     $PersistentCompositeLifecycle)) {
    throw 'C188R12R4R5R1 initialization diagnostics are isolated to the C188R12R4R5 proof profile and cannot enter persistent production lifecycle builds.'
}
if ($C188R12R4FactoryAttribution -and
    (-not $ManagedAllocation -or -not $PersistentCompositeLifecycle)) {
    throw 'C188R12R4 factory attribution requires managed allocation and the persistent composite lifecycle.'
}

if (-not [string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) {
    $ExternalRuntimeRoot = [System.IO.Path]::GetFullPath($ExternalRuntimeRoot)
    if (-not (Test-Path -LiteralPath (Join-Path $ExternalRuntimeRoot ".git"))) {
        throw "External runtime root is not a Git checkout: $ExternalRuntimeRoot"
    }
    $externalCommit = (& git -c core.longpaths=true -C $ExternalRuntimeRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Unable to read external runtime checkout revision: $ExternalRuntimeRoot" }
    $externalCheckoutHead = $externalCommit
    $externalStatus = @(& git -c core.longpaths=true -C $ExternalRuntimeRoot status --porcelain 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "Unable to read external runtime checkout status: $ExternalRuntimeRoot" }
    if ($externalStatus.Count -ne 0) {
        throw "External runtime checkout is dirty; C51 requires a clean source acquisition checkout: $ExternalRuntimeRoot"
    }
    if (-not $NativeAotFpRepair -and $externalCommit -ne $lock.ilCompiler.commit) {
        throw "External runtime checkout revision mismatch. Expected $($lock.ilCompiler.commit), got $externalCommit"
    }
    if ($NativeAotFpRepair) {
        & git -c core.longpaths=true -C $ExternalRuntimeRoot cat-file -e "$($lock.ilCompiler.commit)`^{commit}"
        if ($LASTEXITCODE -ne 0) {
            throw "Locked NativeAOT source commit is not available in the external checkout: $($lock.ilCompiler.commit)"
        }
        $externalCommit = $lock.ilCompiler.commit
    }
}

$source = Join-Path $RuntimePackRoot $lock.platformObject.source.Replace('/', '\')
if (-not (Test-Path -LiteralPath $source)) { throw "Runtime-pack platform source not found: $source" }
$c108Includes = ""
$c108Defines = ""
$c108SourceRoot = $null
if ($ThreadStaticLifecycleDiagnostics) {
    $c108SourceRoot = Join-Path $OutputRoot "nativeaot-c108-source"
}

if ($NativeAotFpRepair -and (Test-Path -LiteralPath $OutputRoot) -and -not $Clean) {
    throw "C51 stale-artifact protection: NativeAOT FP repair output already exists. Use -Clean or a fresh isolated OutputRoot: $OutputRoot"
}
if ($Clean -and (Test-Path -LiteralPath $OutputRoot)) { Remove-Item -LiteralPath $OutputRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
if ($ThreadStaticLifecycleDiagnostics) {
    New-Item -ItemType Directory -Force -Path $c108SourceRoot | Out-Null
    $c108SourceArchive = Join-Path $OutputRoot "nativeaot-c108-source.tar"
    & git -C $lockedExternalRuntimeRoot archive --format=tar --output="$c108SourceArchive" $lock.ilCompiler.commit `
        "src/coreclr/nativeaot/Runtime" "src/coreclr/inc" "src/coreclr/gc/env" `
        "src/coreclr/pal/inc" "src/native/minipal"
    if ($LASTEXITCODE -ne 0) { throw "Unable to archive locked NativeAOT C108 diagnostic sources." }
    & tar.exe -xf $c108SourceArchive -C $c108SourceRoot
    if ($LASTEXITCODE -ne 0) { throw "Unable to extract locked NativeAOT C108 diagnostic sources." }
    $c108CoreclrRoot = Join-Path $c108SourceRoot "src\coreclr"
    $c108RuntimeRoot = Join-Path $c108CoreclrRoot "nativeaot\Runtime"
    if (-not (Test-Path -LiteralPath (Join-Path $c108RuntimeRoot "thread.h")) -or
        -not (Test-Path -LiteralPath (Join-Path $c108SourceRoot "src\native\minipal\utils.h"))) {
        throw "Locked NativeAOT C108 diagnostic sources were not extracted correctly."
    }
    $c108Defines = "/DWIN32 /D_WIN32 /D_WIN64 /DHOST_WINDOWS /DTARGET_WINDOWS /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /D_LIB /DUSE_GC_INFO_DECODER"
    $c108Includes = "/I`"$c108RuntimeRoot`" /I`"$c108RuntimeRoot\windows`" /I`"$c108CoreclrRoot`" /I`"$c108CoreclrRoot\native`" /I`"$c108CoreclrRoot\gc`" /I`"$c108CoreclrRoot\gc\env`" /I`"$c108RuntimeRoot\inc`" /I`"$c108RuntimeRoot\eventpipe`" /I`"$c108CoreclrRoot\inc`" /I`"$c108CoreclrRoot\pal\inc`" /I`"$c108SourceRoot\src\native`" /I`"$(Join-Path $RuntimePackRoot 'src\platform')`" /FI`"$c108CoreclrRoot\gc\env\common.h`""
}
$object = Join-Path $OutputRoot "guidexos_nativeaot_platform.obj"
$batch = Join-Path $OutputRoot "build-runtime-pack.bat"
$sdkOutput = Join-Path $OutputRoot "sdk"
$sdkBatch = Join-Path $OutputRoot "build-runtime-pack-sdk.bat"
$manifestPath = Join-Path $OutputRoot "runtime-pack.manifest.json"
$vcvars = Find-VcVars64
$objcopy = Find-Objcopy
$fpPatch = Join-Path $RuntimePackRoot "patches\nativeaot-amd64-fp-handoff.patch"
$fpApply = Join-Path $RuntimePackRoot "apply-nativeaot-fp-repair.ps1"
$fpSourceRoot = Join-Path $OutputRoot "nativeaot-fp-repair-source"
$fpStackSource = Join-Path $fpSourceRoot "src\coreclr\nativeaot\Runtime\StackFrameIterator.cpp"
$fpCoffSource = Join-Path $fpSourceRoot "src\coreclr\nativeaot\Runtime\windows\CoffNativeCodeManager.cpp"
$fpStackObject = Join-Path $OutputRoot "StackFrameIterator.cpp.fp-repair.obj"
$fpCoffObject = Join-Path $OutputRoot "CoffNativeCodeManager.cpp.fp-repair.obj"
$fpRepairResultPath = $null
$fpStackMember = "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\StackFrameIterator.cpp.obj"
$fpCoffMember = "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\windows\CoffNativeCodeManager.cpp.obj"
if ($NativeAotFpRepair) {
    if ([string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) {
        throw "NativeAOT FP repair requires the locked NativeAOT source checkout."
    }
    if (-not (Test-Path -LiteralPath $fpPatch -PathType Leaf) -or
        -not (Test-Path -LiteralPath $fpApply -PathType Leaf)) {
        throw "NativeAOT FP repair patch tooling is incomplete."
    }
    New-Item -ItemType Directory -Force -Path $fpSourceRoot | Out-Null
    $fpSourceArchive = Join-Path $OutputRoot "nativeaot-fp-repair-source.tar"
    & git -C $ExternalRuntimeRoot archive --format=tar --output="$fpSourceArchive" $lock.ilCompiler.commit `
        "src/coreclr/nativeaot/Runtime" "src/coreclr/gc" "src/coreclr/inc" "src/coreclr/vm" `
        "src/coreclr/pal/inc/rt" "src/coreclr/pal/src/include" "src/native/minipal"
    if ($LASTEXITCODE -ne 0) { throw "Unable to archive locked NativeAOT FP repair sources." }
    & tar.exe -xf $fpSourceArchive -C $fpSourceRoot
    if ($LASTEXITCODE -ne 0) { throw "Unable to extract locked NativeAOT FP repair sources." }
    Set-Content -LiteralPath (Join-Path $fpSourceRoot ".guidexos-runtime-source-commit") -Value $lock.ilCompiler.commit -Encoding ASCII
    $fpRepairResultPath = Join-Path $OutputRoot "nativeaot-fp-repair.result.json"
    & $fpApply -SourceRoot $fpSourceRoot -RuntimeCommit $lock.ilCompiler.commit -LockPath (Join-Path $RuntimePackRoot "runtime-pack.lock.json") -PatchPath $fpPatch -ResultPath $fpRepairResultPath
    if ($LASTEXITCODE -ne 0) { throw "C51 NativeAOT FP repair application failed with exit code $LASTEXITCODE" }
}

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
$runtimeLibraryInput = $stockRuntimeLibrary
$adaptedRuntimeLibrary = Join-Path $sdkOutput "Runtime.WorkstationGC.lib"
$removedRuntimeMembers = @(
    "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\EHHelpers.cpp.obj",
    "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\thread.cpp.obj"
)
if ($ProductionApplication) {
    $removedRuntimeMembers += @(
        "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\windows\gcenv.windows.cpp.obj",
        "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\windows\PalRedhawkMinWin.cpp.obj",
        "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\gcconfig.cpp.obj",
        "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\RhConfig.cpp.obj",
        "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\startup.cpp.obj"
    )
}
if ($C188R12R4R5R1InitDiagnostics) {
    $removedRuntimeMembers += 'nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\GCHelpers.cpp.obj'
    $removedRuntimeMembers += 'nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\RuntimeInstance.cpp.obj'
}
if ($NativeAotFpRepair) {
    $removedRuntimeMembers += @($fpStackMember, $fpCoffMember)
}
$threadMember = Join-Path $OutputRoot "thread.cpp.obj"
$ehMember = Join-Path $OutputRoot "EHHelpers.cpp.obj"
$threadRenamedMember = Join-Path $OutputRoot "thread.cpp.obj.renamed.obj"
$ehRenamedMember = Join-Path $OutputRoot "EHHelpers.cpp.obj.renamed.obj"
$allocFastMember = Join-Path $OutputRoot "AllocFast.asm.obj"
$allocFastRenamedMember = Join-Path $OutputRoot "AllocFast.asm.obj.renamed.obj"
$allocFastStockMember = "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\amd64\AllocFast.asm.obj"
if ($ManagedAllocation) {
    $removedRuntimeMembers += $allocFastStockMember
}
$fpExtractionLines = if ($NativeAotFpRepair) {
    @(
        "lib.exe /nologo /extract:`"$fpStackMember`" `"$stockRuntimeLibrary`" /out:`"$fpStackObject`"",
        "if errorlevel 1 exit /b %errorlevel%",
        "lib.exe /nologo /extract:`"$fpCoffMember`" `"$stockRuntimeLibrary`" /out:`"$fpCoffObject`"",
        "if errorlevel 1 exit /b %errorlevel%"
    )
} else { @() }
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
        "lib.exe /nologo /extract:`"$allocFastStockMember`" `"$stockRuntimeLibrary`" /out:`"$allocFastMember`"",
        "if errorlevel 1 exit /b %errorlevel%"
    )
}
$sdkLines += $fpExtractionLines
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
    $allocationRenameArguments = @(
        '--redefine-sym', 'RhpNewArray=guideXosStockRhpNewArray'
    )
    if ($C188R12R4FactoryAttribution) {
        $allocationRenameArguments += @(
            '--redefine-sym', 'RhpNewFast=guideXosStockRhpNewFast'
        )
    }
    & $objcopy @allocationRenameArguments $allocFastRenamedMember
    if ($LASTEXITCODE -ne 0) { throw "COFF symbol adaptation failed for AllocFast.asm.obj" }
}
$fpCompileBatch = Join-Path $OutputRoot "build-nativeaot-fp-repair.bat"
if ($NativeAotFpRepair) {
    $fpCoreclrRoot = Join-Path $fpSourceRoot "src\coreclr"
    $fpNativeRoot = Join-Path $fpSourceRoot "src\native"
    $nativeAotRuntimeRoot = Join-Path $fpCoreclrRoot "nativeaot\Runtime"
    $fpDefines = "/DWIN32 /D_WIN32 /D_WIN64 /DHOST_AMD64 /DTARGET_AMD64 /DTARGET_64BIT /DHOST_64BIT /DHOST_WINDOWS /DTARGET_WINDOWS /DNATIVEAOT /DFEATURE_NATIVEAOT /DFEATURE_HIJACK /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_PERFTRACING /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DVERIFY_HEAP /DUSE_GC_INFO_DECODER /D_LIB"
    $fpStackDefines = "$fpDefines /DLPVOID=void*"
    $fpIncludes = "/I`"$nativeAotRuntimeRoot`" /I`"$nativeAotRuntimeRoot\windows`" /I`"$fpCoreclrRoot`" /I`"$fpCoreclrRoot\native`" /I`"$fpCoreclrRoot\gc`" /I`"$fpCoreclrRoot\gc\env`" /I`"$nativeAotRuntimeRoot\inc`" /I`"$nativeAotRuntimeRoot\eventpipe`" /I`"$fpNativeRoot`" /I`"$(Join-Path $RuntimePackRoot 'src\platform')`" /FI`"$fpCoreclrRoot\gc\env\common.h`""
    $fpCompileLines = @(
        "@echo off",
        "setlocal",
        "call `"$vcvars`" >nul",
        "if errorlevel 1 exit /b %errorlevel%",
        "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $fpStackDefines $fpIncludes /Fo:`"$fpStackObject`" `"$fpStackSource`"",
        "if errorlevel 1 exit /b %errorlevel%",
        "cl.exe /nologo /std:c++14 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $fpDefines $fpIncludes /Fo:`"$fpCoffObject`" `"$fpCoffSource`"",
        "if errorlevel 1 exit /b %errorlevel%",
        "exit /b 0"
    )
    $fpCompileLines | Set-Content -LiteralPath $fpCompileBatch -Encoding ASCII
    & $fpCompileBatch
    if ($LASTEXITCODE -ne 0) { throw "NativeAOT FP repair source compilation failed with exit code $LASTEXITCODE" }
    foreach ($objectPath in @($fpStackObject, $fpCoffObject)) {
        if (-not (Test-Path -LiteralPath $objectPath -PathType Leaf)) { throw "NativeAOT FP repair object was not produced: $objectPath" }
    }
}
$fpArchiveObjects = if ($NativeAotFpRepair) {
    " `"$fpStackObject`" `"$fpCoffObject`""
} else {
    ""
}
$productionGcEnvironmentObjects = ""
if ($ProductionApplication) {
    # The PE-to-ELF image has no Windows loader to resolve gcenv.windows.cpp.
    # Compile the existing guideXOS-neutral GC environment and its Win64-side
    # startup bridge into the authenticated Workstation GC archive.  The
    # collector objects remain the locked NativeAOT 9.0.0 Workstation GC; only
    # the OS environment object is replaced.
    $gcSourceRoot = Join-Path $lockedExternalRuntimeRoot "src\coreclr"
    $gcEnvSource = Join-Path $RuntimePackRoot "src\gcenv\guidexos_gcenv.cpp"
    $gcBridgeSource = Join-Path $RuntimePackRoot "src\platform\guidexos_nativeaot_gcenv_startup_bridge.cpp"
    $startupImportsSource = Join-Path $RuntimePackRoot "src\platform\guidexos_nativeaot_startup_imports.cpp"
    $gcConfigSource = Join-Path $gcSourceRoot "gc\gcconfig.cpp"
    $gcConfigWrapperSource = Join-Path $OutputRoot "guidexos_nativeaot_gcconfig_replacement.cpp"
    $rhConfigSource = Join-Path $gcSourceRoot "nativeaot\Runtime\RhConfig.cpp"
    $rhConfigWrapperSource = Join-Path $OutputRoot "guidexos_nativeaot_rhconfig_replacement.cpp"
    $startupSource = Join-Path $gcSourceRoot "nativeaot\Runtime\startup.cpp"
    $runtimeInstanceSource = Join-Path $gcSourceRoot "nativeaot\Runtime\RuntimeInstance.cpp"
    $startupWrapperSource = Join-Path $OutputRoot "guidexos_nativeaot_startup_replacement.cpp"
    $gcEnvObject = Join-Path $OutputRoot "guidexos_gcenv.production.obj"
    $gcBridgeObject = Join-Path $OutputRoot "guidexos_nativeaot_gcenv_startup_bridge.production.obj"
    $startupImportsObject = Join-Path $OutputRoot "guidexos_nativeaot_startup_imports.production.obj"
    $gcConfigObject = Join-Path $OutputRoot "guidexos_nativeaot_gcconfig_replacement.production.obj"
    $rhConfigObject = Join-Path $OutputRoot "guidexos_nativeaot_rhconfig_replacement.production.obj"
    $startupObject = Join-Path $OutputRoot "guidexos_nativeaot_startup_replacement.production.obj"
    $gcSupportRoot = Join-Path $OutputRoot "gcconfig-support"
    $gcSupportMinipal = Join-Path $gcSupportRoot "minipal"
    $gcSupportUtils = Join-Path $gcSupportMinipal "utils.h"
    foreach ($path in @($gcEnvSource, $gcBridgeSource, $startupImportsSource, $gcConfigSource, $rhConfigSource, $startupSource, $runtimeInstanceSource, $gcSourceRoot)) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Production NativeAOT GC environment input is missing: $path"
        }
    }
    $gcConfigIncludePath = ([System.IO.Path]::GetFullPath($gcConfigSource)).Replace('\', '/')
    @(
        '#include <windows.h>'
        'extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetLastError();'
        'extern "C" DWORD WINAPI guidexos_nativeaot_startup_GetEnvironmentVariableW(LPCWSTR, LPWSTR, DWORD);'
        '#define GetLastError guidexos_nativeaot_startup_GetLastError'
        '#define GetEnvironmentVariableW guidexos_nativeaot_startup_GetEnvironmentVariableW'
        "#include `"$gcConfigIncludePath`""
    ) | Set-Content -LiteralPath $gcConfigWrapperSource -Encoding ASCII
    $rhConfigIncludePath = ([System.IO.Path]::GetFullPath($rhConfigSource)).Replace('\', '/')
    @(
        '#define STRING_LENGTH(value) (static_cast<uint32_t>((sizeof(value) / sizeof((value)[0])) - 1u))'
        '#define ARRAY_SIZE(value) (static_cast<uint32_t>(sizeof(value) / sizeof((value)[0])))'
        '#define GetLastError guidexos_nativeaot_startup_GetLastError'
        '#define GetEnvironmentVariableW guidexos_nativeaot_startup_GetEnvironmentVariableW'
        "#include `"$rhConfigIncludePath`""
    ) | Set-Content -LiteralPath $rhConfigWrapperSource -Encoding ASCII
    $startupIncludePath = ([System.IO.Path]::GetFullPath($startupSource)).Replace('\', '/')
    @(
        '#define GetModuleHandleW guidexos_nativeaot_startup_GetModuleHandleW'
        '#define GetProcAddress guidexos_nativeaot_startup_GetProcAddress'
        '#define atexit guidexos_nativeaot_startup_atexit'
        "#include `"$startupIncludePath`""
    ) | Set-Content -LiteralPath $startupWrapperSource -Encoding ASCII
    New-Item -ItemType Directory -Path $gcSupportMinipal -Force | Out-Null
    @(
        '#pragma once'
        '// The locked NativeAOT GC configuration source includes this header,'
        '// but its configuration object does not consume minipal utilities.'
    ) | Set-Content -LiteralPath $gcSupportUtils -Encoding ASCII
    $gcSupportCpuFeatures = Join-Path $gcSupportMinipal "cpufeatures.h"
    @(
        '#pragma once'
        'static inline int minipal_getcpufeatures() { return 0; }'
    ) | Set-Content -LiteralPath $gcSupportCpuFeatures -Encoding ASCII
    $gcDefines = "/DGXOS_BARE_METAL /DGXOS_TRUE_VIRTUAL_MEMORY /D_FEATURE_NATIVEAOT /DNATIVEAOT /DTARGET_AMD64 /DTARGET_64BIT /DHOST_AMD64 /DHOST_64BIT /D_WIN64 /D_HAS_STD_BYTE=0"
    $rhConfigDefines = "$gcDefines /DTARGET_WINDOWS /DHOST_WINDOWS /DUNICODE"
    $startupDefines = "$gcDefines /DTARGET_WINDOWS /DHOST_WINDOWS /DUNICODE"
    $gcIncludes = "/I`"$(Join-Path $gcSourceRoot 'gc')`" /I`"$(Join-Path $gcSourceRoot 'gc\env')`" /I`"$(Join-Path $gcSourceRoot 'nativeaot\Runtime')`" /I`"$(Join-Path $gcSourceRoot 'nativeaot\Runtime\inc')`" /I`"$(Join-Path $gcSourceRoot 'nativeaot\Runtime\windows')`" /I`"$(Join-Path $gcSourceRoot '..\native')`" /I`"$gcSupportRoot`" /I`"$(Join-Path $RuntimePackRoot 'src\platform')`""
    # These replacement translation units must see the NativeAOT Runtime's
    # gcenv.h before the GC's standalone gcenv.h, matching Runtime/CMakeLists.
    $runtimeDiagnosticIncludes = "/I`"$(Join-Path $gcSourceRoot 'nativeaot\Runtime\inc')`" /I`"$(Join-Path $gcSourceRoot 'nativeaot\Runtime')`" $gcIncludes"
    $runtimeDiagnosticDefines = $startupDefines
    $gcHelpersObject = $null
    $gcHelpersInstrumentedSource = $null
    $runtimeVersionHeader = $null
    $runtimeVersionHeaderSha256 = $null
    $runtimeInstanceObject = $null
    $runtimeInstanceInstrumentedSource = $null
    if ($C188R12R4R5R1InitDiagnostics) {
        $startupDefines += ' /DGUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS'
        $gcDefines += ' /DGUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS'
        $rhConfigDefines += ' /DGUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS'
        # Keep the transformed upstream translation units on the same pinned
        # NativeAOT Windows/AMD64 Release feature set used by Runtime/CMakeLists.
        # Otherwise replacing GCHelpers/startup would silently compile out
        # startup work (for example cached dispatch and EventPipe setup).
        $runtimeDiagnosticDefines = "$startupDefines /DFEATURE_PERFTRACING /DFEATURE_EVENT_TRACE /DFEATURE_HIJACK /DFEATURE_BASICFREEZE /DFEATURE_CONSERVATIVE_GC /DFEATURE_USE_SOFTWARE_WRITE_WATCH_FOR_GC_HEAP /DFEATURE_MANUALLY_MANAGED_CARD_BUNDLES /DFEATURE_CUSTOM_IMPORTS /DFEATURE_DYNAMIC_CODE /DFEATURE_CACHED_INTERFACE_DISPATCH /DFEATURE_NATIVEAOT /DVERIFY_HEAP /DNATIVEAOT /D_LIB /DFEATURE_ETW /DFEATURE_SUSPEND_REDIRECTION /DFEATURE_SPECIAL_USER_MODE_APC /DGUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS"
        $eventingIntermediate = Join-Path $OutputRoot 'nativeaot-eventing'
        $eventingIncludeDirectory = Join-Path $eventingIntermediate 'inc'
        $eventManifest = Join-Path $gcSourceRoot 'vm\ClrEtwAll.man'
        $eventExclusions = Join-Path $gcSourceRoot 'vm\ClrEtwAllMeta.lst'
        $eventInclusionFile = Join-Path $gcSourceRoot 'nativeaot\Runtime\eventpipe\gen-eventing-event-inc.lst'
        $eventingGenerator = Join-Path $gcSourceRoot 'scripts\genEventing.py'
        $etwGenerator = Join-Path $gcSourceRoot 'scripts\genEtwProvider.py'
        foreach ($path in @($eventManifest, $eventExclusions, $eventInclusionFile, $eventingGenerator, $etwGenerator)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Pinned NativeAOT eventing input is missing: $path"
            }
        }
        $python = Get-Command 'python.exe' -ErrorAction Stop
        $mc = Get-ChildItem -LiteralPath (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin') -Filter 'mc.exe' -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '[\\/]x64[\\/]mc\.exe$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($null -eq $mc) {
            throw 'Windows SDK x64 mc.exe is required to generate pinned NativeAOT ETW headers.'
        }
        New-Item -ItemType Directory -Path $eventingIncludeDirectory -Force | Out-Null
        $previousPath = $env:PATH
        try {
            $env:PATH = "$(Split-Path -Parent $mc.FullName);$previousPath"
            & $python.Source $eventingGenerator --man $eventManifest --incdir $eventingIncludeDirectory --inc $eventInclusionFile --dummy (Join-Path $eventingIncludeDirectory 'etmdummy.h') --runtimeflavor nativeaot --nonextern --noxplatheader
            if ($LASTEXITCODE -ne 0) { throw "Pinned NativeAOT event header generation failed with exit code $LASTEXITCODE" }
            & $python.Source $etwGenerator --man $eventManifest --exc $eventExclusions --intermediate $eventingIntermediate
            if ($LASTEXITCODE -ne 0) { throw "Pinned NativeAOT ETW header generation failed with exit code $LASTEXITCODE" }
        }
        finally {
            $env:PATH = $previousPath
        }
        foreach ($path in @((Join-Path $eventingIncludeDirectory 'clretwallmain.h'), (Join-Path $eventingIntermediate 'clrxplatevents.h'))) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Pinned NativeAOT event header generation omitted: $path"
            }
        }
        $runtimeDiagnosticIncludes += " /I`"$eventingIncludeDirectory`" /I`"$eventingIntermediate`""
        # The pinned source checkout is a source subset and does not carry the
        # full runtime-prereqs build output.  Recreate runtime_version.h from
        # the locked ILCompiler package identity used for this exact candidate.
        $runtimeVersion = [Version]$lock.ilCompiler.version
        $runtimeVersionRevision = if ($runtimeVersion.Revision -ge 0) { $runtimeVersion.Revision } else { 0 }
        $runtimeVersionHeader = Join-Path $eventingIntermediate 'runtime_version.h'
        @(
            '#pragma once'
            "#define RuntimeProductVersion $($lock.ilCompiler.version)"
            "#define RuntimeAssemblyMajorVersion $($runtimeVersion.Major)"
            "#define RuntimeAssemblyMinorVersion $($runtimeVersion.Minor)"
            "#define RuntimeFileMajorVersion $($runtimeVersion.Major)"
            "#define RuntimeFileMinorVersion $($runtimeVersion.Minor)"
            "#define RuntimeFileBuildVersion $($runtimeVersion.Build)"
            "#define RuntimeFileRevisionVersion $runtimeVersionRevision"
        ) | Set-Content -LiteralPath $runtimeVersionHeader -Encoding ASCII
        $runtimeVersionHeaderSha256 = Get-Hash $runtimeVersionHeader
        $runtimeDiagnosticIncludes += " /I`"$eventingIntermediate`""
        $gcHelpersSource = Join-Path $gcSourceRoot 'nativeaot\Runtime\GCHelpers.cpp'
        $gcHelpersInstrumentedSource = Join-Path $OutputRoot 'guidexos_nativeaot_gchelpers_init_diagnostics.cpp'
        $gcHelpersObject = Join-Path $OutputRoot 'guidexos_nativeaot_gchelpers_init_diagnostics.obj'
        if (-not (Test-Path -LiteralPath $gcHelpersSource -PathType Leaf)) {
            throw "Pinned GCHelpers.cpp source is missing: $gcHelpersSource"
        }

        $helperText = Get-Content -LiteralPath $gcHelpersSource -Raw
        # The freestanding replacement needs only LPVOID for daccess.h's
        # non-DAC alias; including windows.h here would suppress gcenv.base.h
        # and alter the upstream NativeAOT compile environment.
        $helperText = 'typedef void* LPVOID;' + "`r`n" + '#include "guidexos_nativeaot_init_trace.h"' + "`r`n" + $helperText
        $helperText = Replace-ExactOneSource $helperText 'HRESULT hr = GCHeapUtilities::InitializeGC\(\);\s*if \(FAILED\(hr\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_GC_HEAP_PUBLICATION);
    HRESULT hr = GCHeapUtilities::InitializeGC();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_GC_HEAP_PUBLICATION,
        SUCCEEDED(hr), static_cast<uint32_t>(hr));
    if (FAILED(hr)) {
        return false;
    }
'@ 'GC heap and handle-manager publication result'
        $helperText = Replace-ExactOneSource $helperText 'hr = g_pGCHeap->Initialize\(\);\s*if \(FAILED\(hr\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_GC_HEAP_INITIALIZE);
    hr = g_pGCHeap->Initialize();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_GC_HEAP_INITIALIZE,
        SUCCEEDED(hr), static_cast<uint32_t>(hr));
    if (FAILED(hr)) {
        return false;
    }
'@ 'GC heap Initialize result'
        $helperText = Replace-ExactOneSource $helperText 'if \(!RhInitializeFinalization\(\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_GC_FINALIZATION);
    const bool finalizationInitialized = RhInitializeFinalization();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_GC_FINALIZATION,
        finalizationInitialized);
    if (!finalizationInitialized) {
        return false;
    }
'@ 'finalization initialization result'
        $helperText = Replace-ExactOneSource $helperText 'if \(!GCHandleUtilities::GetGCHandleManager\(\)->Initialize\(\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_GC_HANDLE_MANAGER);
    const bool handleManagerInitialized =
        GCHandleUtilities::GetGCHandleManager()->Initialize();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_GC_HANDLE_MANAGER,
        handleManagerInitialized);
    if (!handleManagerInitialized) {
        return false;
    }
'@ 'GC handle-table initialization result'
        Set-Content -LiteralPath $gcHelpersInstrumentedSource -Value $helperText -Encoding ASCII

        $runtimeInstanceInstrumentedSource = Join-Path $OutputRoot 'guidexos_nativeaot_runtimeinstance_init_diagnostics.cpp'
        $runtimeInstanceObject = Join-Path $OutputRoot 'guidexos_nativeaot_runtimeinstance_init_diagnostics.obj'
        $runtimeInstanceText = 'typedef void* LPVOID;' + "`r`n" + '#include "guidexos_nativeaot_init_trace.h"' + "`r`n" +
            (Get-Content -LiteralPath $runtimeInstanceSource -Raw)
        $runtimeInstanceText = Replace-ExactOneSource $runtimeInstanceText 'NewHolder<RuntimeInstance> pRuntimeInstance = new \(nothrow\) RuntimeInstance\(\);\s*if \(NULL == pRuntimeInstance\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_RUNTIME_INSTANCE_ALLOC);
    NewHolder<RuntimeInstance> pRuntimeInstance = new (nothrow) RuntimeInstance();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_RUNTIME_INSTANCE_ALLOC,
        NULL != pRuntimeInstance, NULL != pRuntimeInstance ? 0u : 1u);
    if (NULL == pRuntimeInstance) return false;
'@ 'RuntimeInstance allocation result'
        $runtimeInstanceText = Replace-ExactOneSource $runtimeInstanceText 'CreateHolder<ThreadStore>\s+pThreadStore = ThreadStore::Create\(pRuntimeInstance\);\s*if \(NULL == pThreadStore\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_THREAD_STORE_CREATE);
    CreateHolder<ThreadStore> pThreadStore = ThreadStore::Create(pRuntimeInstance);
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_THREAD_STORE_CREATE,
        NULL != pThreadStore, NULL != pThreadStore ? 0u : 1u);
    if (NULL == pThreadStore) return false;
'@ 'ThreadStore creation result'
        $runtimeInstanceText = Replace-ExactOneSource $runtimeInstanceText 'pThreadStore\.SuppressRelease\(\);\s*pRuntimeInstance\.SuppressRelease\(\);\s*pRuntimeInstance->m_pThreadStore = pThreadStore;\s*pRuntimeInstance->m_hPalInstance = hPalInstance;\s*ASSERT_MSG\(g_pTheRuntimeInstance == NULL, "multi-instances are not supported"\);\s*g_pTheRuntimeInstance = pRuntimeInstance;' @'
    pThreadStore.SuppressRelease();
    pRuntimeInstance.SuppressRelease();

    guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_RUNTIME_INSTANCE_PUBLISH);
    pRuntimeInstance->m_pThreadStore = pThreadStore;
    pRuntimeInstance->m_hPalInstance = hPalInstance;

    ASSERT_MSG(g_pTheRuntimeInstance == NULL, "multi-instances are not supported");
    g_pTheRuntimeInstance = pRuntimeInstance;
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_RUNTIME_INSTANCE_PUBLISH, true);
'@ 'RuntimeInstance/ThreadStore publication order'
        $runtimeInstanceText += @'

#if defined(GUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS)
extern "C" uint32_t __cdecl GuideXosNativeAotCaptureRuntimeInstanceBits()
{
    uint32_t bits = 0u;
    auto* runtime = GetRuntimeInstance();
    if (runtime != nullptr)
    {
        bits |= 1u << 0;
        if (runtime->GetThreadStore() != nullptr)
        {
            bits |= 1u << 1;
            if (ThreadStore::GetCurrentThreadIfAvailable() != nullptr)
            {
                bits |= 1u << 7;
            }
        }
    }
    return bits;
}
#endif
'@
        Set-Content -LiteralPath $runtimeInstanceInstrumentedSource -Value $runtimeInstanceText -Encoding ASCII

        $startupText = Get-Content -LiteralPath $startupSource -Raw
        $startupText = '#include "guidexos_nativeaot_init_trace.h"' + "`r`n" + $startupText
        $startupText = Replace-ExactOneSource $startupText 'static bool InitDLL\(HANDLE hPalInstance\)\s*\{' @'
static bool InitDLL(HANDLE hPalInstance)
{
'@ 'InitDLL entry'
        $startupText = Replace-ExactOneSource $startupText 'if \(!InitializeInterfaceDispatch\(\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_INTERFACE_DISPATCH);
    const bool interfaceDispatchInitialized = InitializeInterfaceDispatch();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_INTERFACE_DISPATCH,
        interfaceDispatchInitialized);
    if (!interfaceDispatchInitialized) return false;
'@ 'interface-dispatch initialization result'
        $startupText = Replace-ExactOneSource $startupText '(?m)^[ \t]*InitializeGCEventLock\(\);[ \t]*$' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_GC_EVENT_LOCK);
    InitializeGCEventLock();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_GC_EVENT_LOCK, true);
'@ 'GC event-lock initialization'
        $startupText = Replace-ExactOneSource $startupText 'if \(!RestrictedCallouts::Initialize\(\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_RESTRICTED_CALLOUTS);
    const bool restrictedCalloutsInitialized = RestrictedCallouts::Initialize();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_RESTRICTED_CALLOUTS,
        restrictedCalloutsInitialized);
    if (!restrictedCalloutsInitialized) return false;
'@ 'restricted-callout initialization result'
        $startupText = Replace-ExactOneSource $startupText 'if \(!RuntimeInstance::Initialize\(hPalInstance\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_RUNTIME_INSTANCE);
    const bool runtimeInstanceInitialized = RuntimeInstance::Initialize(hPalInstance);
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_RUNTIME_INSTANCE,
        runtimeInstanceInitialized, runtimeInstanceInitialized ? 0u : 1u);
    if (!runtimeInstanceInitialized) return false;
'@ 'RuntimeInstance and ThreadStore initialization result'
        $startupText = Replace-ExactOneSource $startupText 'if \(!InitializeGC\(\)\)\s*return false;' @'
    guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_GC_INITIALIZE);
    const bool gcInitialized = InitializeGC();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_GC_INITIALIZE, gcInitialized);
    if (!gcInitialized) return false;
'@ 'GC initialization gate'
        $startupText = Replace-ExactOneSource $startupText 'if \(!DetectCPUFeatures\(\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_CPU_FEATURES);
    const bool cpuFeaturesReady = DetectCPUFeatures();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_CPU_FEATURES,
        cpuFeaturesReady);
    if (!cpuFeaturesReady) return false;
'@ 'CPU feature detection result'
        $startupText = Replace-ExactOneSource $startupText 'if \(!PalInit\(\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_PAL_INIT);
    const bool palInitialized = PalInit();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_PAL_INIT,
        palInitialized);
    if (!palInitialized) return false;
'@ 'PalInit result'
        $startupText = Replace-ExactOneSource $startupText 'PRTLDLLSHUTDOWNINPROGRESS pfn = \(PRTLDLLSHUTDOWNINPROGRESS\)GetProcAddress\(GetModuleHandleW\(W\("ntdll\.dll"\)\), "RtlDllShutdownInProgress"\);\s*if \(pfn != NULL\)\s*g_pfnRtlDllShutdownInProgress = pfn;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_STARTUP_IMPORT_LOOKUP);
    PRTLDLLSHUTDOWNINPROGRESS pfn = (PRTLDLLSHUTDOWNINPROGRESS)GetProcAddress(GetModuleHandleW(W("ntdll.dll")), "RtlDllShutdownInProgress");
    if (pfn != NULL)
        g_pfnRtlDllShutdownInProgress = pfn;
    guideXosNativeAotInitTraceRecord(GX_INIT_STAGE_STARTUP_IMPORT_LOOKUP,
        GX_INIT_EVENT_OBSERVE, pfn != NULL ? 1u : 0u, 0xFFFFFFFFu);
'@ 'optional RtlDllShutdownInProgress import lookup'
        $startupText = Replace-ExactOneSource $startupText 'if \(!InitDLL\(PalGetModuleHandleFromPointer\(\(void\*\)&RhInitialize\)\)\)\s*return false;' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_MODULE_IDENTITY);
    HANDLE startupModule = PalGetModuleHandleFromPointer((void*)&RhInitialize);
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_MODULE_IDENTITY,
        startupModule != nullptr, startupModule == nullptr ? 1u : 0u);
    guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_INIT_DLL);
    const bool initDllSucceeded = InitDLL(startupModule);
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_INIT_DLL, initDllSucceeded);
    if (!initDllSucceeded) return false;
'@ 'InitDLL result and module identity'
        $startupText = Replace-ExactOneSource $startupText '// Populate the values needed for debugging\s*PopulateDebugHeaders\(\);' @'
guideXosNativeAotInitTraceBegin(GX_INIT_STAGE_DEBUG_HEADERS);
    PopulateDebugHeaders();
    guideXosNativeAotInitTraceReturn(GX_INIT_STAGE_DEBUG_HEADERS, true);
'@ 'debug-header population after runtime startup'
        $startupWrapperPrefix = @(
            '#define GetModuleHandleW guidexos_nativeaot_startup_GetModuleHandleW'
            '#define GetProcAddress guidexos_nativeaot_startup_GetProcAddress'
            '#define atexit guidexos_nativeaot_startup_atexit'
            '#include "guidexos_nativeaot_init_trace.h"'
        ) -join "`r`n"
        Set-Content -LiteralPath $startupWrapperSource -Value ($startupWrapperPrefix + "`r`n" + $startupText) -Encoding ASCII
    }
    $startupObjectDefines = $startupDefines
    $startupObjectIncludes = $gcIncludes
    if ($C188R12R4R5R1InitDiagnostics) {
        $startupObjectDefines = $runtimeDiagnosticDefines
        $startupObjectIncludes = $runtimeDiagnosticIncludes
    }
    $gcBatch = Join-Path $OutputRoot "build-production-gc-environment.bat"
    $gcLines = @(
        "@echo off",
        "setlocal",
        "call `"$vcvars`" >nul",
        "if errorlevel 1 exit /b %errorlevel%",
        "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $gcDefines $gcIncludes /Fo:`"$gcEnvObject`" `"$gcEnvSource`"",
        "if errorlevel 1 exit /b %errorlevel%",
        "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $gcDefines $gcIncludes /Fo:`"$gcBridgeObject`" `"$gcBridgeSource`"",
        "if errorlevel 1 exit /b %errorlevel%",
        "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $gcDefines /I`"$(Join-Path $RuntimePackRoot 'src\platform')`" /Fo:`"$startupImportsObject`" `"$startupImportsSource`"",
        "if errorlevel 1 exit /b %errorlevel%",
        "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $gcDefines $gcIncludes /Fo:`"$gcConfigObject`" `"$gcConfigWrapperSource`"",
        "if errorlevel 1 exit /b %errorlevel%",
        "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $rhConfigDefines $gcIncludes /Fo:`"$rhConfigObject`" `"$rhConfigWrapperSource`"",
        "if errorlevel 1 exit /b %errorlevel%",
        "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $startupObjectDefines $startupObjectIncludes /Fo:`"$startupObject`" `"$startupWrapperSource`"",
        "if errorlevel 1 exit /b %errorlevel%",
        "exit /b 0"
    )
    $gcLines | Set-Content -LiteralPath $gcBatch -Encoding ASCII
    & $gcBatch
    if ($LASTEXITCODE -ne 0) { throw "Production NativeAOT GC environment compilation failed with exit code $LASTEXITCODE" }
    foreach ($path in @($gcEnvObject, $gcBridgeObject, $startupImportsObject, $gcConfigObject, $rhConfigObject, $startupObject)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Production NativeAOT GC environment object was not produced: $path"
        }
    }
    $productionGcEnvironmentObjects = " `"$gcEnvObject`" `"$gcBridgeObject`" `"$startupImportsObject`" `"$gcConfigObject`" `"$rhConfigObject`" `"$startupObject`""
    if ($C188R12R4R5R1InitDiagnostics) {
        $gcHelpersBatch = Join-Path $OutputRoot 'build-production-gchelpers-init-diagnostics.bat'
        @(
            '@echo off'
            'setlocal'
            "call `"$vcvars`" >nul"
            'if errorlevel 1 exit /b %errorlevel%'
            "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $runtimeDiagnosticDefines $runtimeDiagnosticIncludes /Fo:`"$gcHelpersObject`" `"$gcHelpersInstrumentedSource`""
            'if errorlevel 1 exit /b %errorlevel%'
            'exit /b 0'
        ) | Set-Content -LiteralPath $gcHelpersBatch -Encoding ASCII
        & $gcHelpersBatch
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $gcHelpersObject -PathType Leaf)) {
            throw 'Instrumented GCHelpers.cpp replacement object did not compile.'
        }
        $productionGcEnvironmentObjects += " `"$gcHelpersObject`""
        $runtimeInstanceBatch = Join-Path $OutputRoot 'build-production-runtimeinstance-init-diagnostics.bat'
        @(
            '@echo off'
            'setlocal'
            "call `"$vcvars`" >nul"
            'if errorlevel 1 exit /b %errorlevel%'
            "cl.exe /nologo /std:c++17 /TP /c /MT /GS- /GR- /EHs-c- /Zl /Oi /O2 /Zc:inline /Brepro $runtimeDiagnosticDefines $runtimeDiagnosticIncludes /Fo:`"$runtimeInstanceObject`" `"$runtimeInstanceInstrumentedSource`""
            'if errorlevel 1 exit /b %errorlevel%'
            'exit /b 0'
        ) | Set-Content -LiteralPath $runtimeInstanceBatch -Encoding ASCII
        & $runtimeInstanceBatch
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $runtimeInstanceObject -PathType Leaf)) {
            throw 'Instrumented RuntimeInstance.cpp replacement object did not compile.'
        }
        $productionGcEnvironmentObjects += " `"$runtimeInstanceObject`""
    }
}
$sdkLines = @(
    "@echo off",
    "setlocal",
    "call `"$vcvars`" >nul",
    "if errorlevel 1 exit /b %errorlevel%",
    "lib.exe /nologo /OUT:`"$adaptedRuntimeLibrary`" `"$runtimeLibraryInput`" $removeArguments `"$threadRenamedMember`" `"$ehRenamedMember`"$allocFastMemberArguments$fpArchiveObjects$productionGcEnvironmentObjects",
    "exit /b %errorlevel%"
)
$sdkLines | Set-Content -LiteralPath $sdkBatch -Encoding ASCII
& $sdkBatch
if ($LASTEXITCODE -ne 0) { throw "GuideXOS runtime-pack adapted runtime library build failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $adaptedRuntimeLibrary)) { throw "Adapted runtime library was not produced: $adaptedRuntimeLibrary" }
Normalize-CoffArchive $adaptedRuntimeLibrary
$archiveMembersPath = Join-Path $OutputRoot "Runtime.WorkstationGC.members.txt"
$archiveMembersBatch = Join-Path $OutputRoot "list-runtime-pack-members.bat"
$archiveMembersLines = @(
    "@echo off",
    "setlocal",
    "call `"$vcvars`" >nul",
    "if errorlevel 1 exit /b %errorlevel%",
    "lib.exe /nologo /list `"$adaptedRuntimeLibrary`" > `"$archiveMembersPath`"",
    "exit /b %errorlevel%"
)
$archiveMembersLines | Set-Content -LiteralPath $archiveMembersBatch -Encoding ASCII
& $archiveMembersBatch
if ($LASTEXITCODE -ne 0) { throw "C51 archive membership listing failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $archiveMembersPath -PathType Leaf)) { throw "C51 archive membership list was not produced: $archiveMembersPath" }
$archiveMembers = @((Get-Content -LiteralPath $archiveMembersPath | ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 }))
$normalizeMember = { param([string]$Name) $Name.Trim().Replace('/', '\').ToLowerInvariant() }
$expectedPatchedMembers = @()
if ($NativeAotFpRepair) { $expectedPatchedMembers = @($fpStackMember, $fpCoffMember) }
$expectedPatchedArchiveMembers = @()
if ($NativeAotFpRepair) {
    # lib.exe stores explicitly supplied replacement object paths by their
    # output leaf names rather than by the stock archive member paths.
    $expectedPatchedArchiveMembers = @(
        [System.IO.Path]::GetFileName($fpStackObject),
        [System.IO.Path]::GetFileName($fpCoffObject)
    )
}
$getMemberLeaf = { param([string]$Name) [System.IO.Path]::GetFileName($Name.Trim().Replace('/', '\')) }
$patchedMemberCounts = [ordered]@{}
for ($memberIndex = 0; $memberIndex -lt $expectedPatchedMembers.Count; $memberIndex++) {
    $member = $expectedPatchedMembers[$memberIndex]
    $normalized = & $normalizeMember $member
    $count = @($archiveMembers | Where-Object { (& $normalizeMember $_) -eq $normalized }).Count
    if ($count -eq 0) {
        $archiveLeaf = $expectedPatchedArchiveMembers[$memberIndex]
        $normalizedLeaf = & $normalizeMember $archiveLeaf
        $count = @($archiveMembers | Where-Object { (& $normalizeMember (& $getMemberLeaf $_)) -eq $normalizedLeaf }).Count
    }
    $patchedMemberCounts[$member] = $count
    if ($count -ne 1) { throw "C51 archive membership validation expected exactly one patched member '$member', got $count" }
}
$removedMemberCounts = [ordered]@{}
foreach ($member in $removedRuntimeMembers) {
    $normalized = & $normalizeMember $member
    $count = @($archiveMembers | Where-Object { (& $normalizeMember $_) -eq $normalized }).Count
    $removedMemberCounts[$member] = $count
    if ($count -ne 0) { throw "C51 archive membership validation found removed stock member '$member' $count time(s)" }
}
$archiveValidation = [ordered]@{
    result = "PASS"
    archive = $adaptedRuntimeLibrary
    archiveSha256 = Get-Hash $adaptedRuntimeLibrary
    memberList = $archiveMembersPath
    memberCount = $archiveMembers.Count
    patchedMemberCounts = $patchedMemberCounts
    removedMemberCounts = $removedMemberCounts
    duplicatePatchedMembers = (@($patchedMemberCounts.Values | Where-Object { $_ -gt 1 }).Count)
}

$lines = @(
    "@echo off",
    "setlocal",
    "call `"$vcvars`" >nul",
    "if errorlevel 1 exit /b %errorlevel%",
    "cl.exe /nologo /TP /c /GS- /GR- /EHs-c- /Zl /Oi /O2 /Brepro $(if ($ManagedAllocation) { "/DGUIDEXOS_NATIVEAOT_MANAGED_ALLOCATION /DGUIDEXOS_MANAGED_HEAP_BYTES=$managedHeapBytes $(if ($ManagedRepeatedAllocation) { '/DGUIDEXOS_NATIVEAOT_MANAGED_REPEATED_ALLOCATION' } else { '' }) $(if ($ProductionApplication) { '/DGUIDEXOS_NATIVEAOT_PRODUCTION_APPLICATION' } else { '' }) $(if ($PersistentCompositeLifecycle) { '/DGUIDEXOS_NATIVEAOT_PERSISTENT_COMPOSITE_LIFECYCLE' } else { '' }) $(if ($C188R12R4FactoryAttribution) { '/DGUIDEXOS_NATIVEAOT_C188R12R4_FACTORY_ATTRIBUTION' } else { '' }) $(if ($C188R12R4R5GcOwnershipProof) { '/DGUIDEXOS_NATIVEAOT_REAL_GC_ALLOCATION /DGUIDEXOS_NATIVEAOT_C188R12R4R5_GC_OWNERSHIP_PROOF' } else { '' }) $(if ($C188R12R4R5R1InitDiagnostics) { '/DGUIDEXOS_NATIVEAOT_C188R12R4R5R1_INIT_DIAGNOSTICS' } else { '' }) $(if ($ThreadStaticLifecycleDiagnostics) { '/DGUIDEXOS_NATIVEAOT_C108_TLS_LIFECYCLE' } else { '' })" } else { '' }) $c108Defines $c108Includes /Fo:`"$object`" `"$source`"",
    "exit /b %errorlevel%"
)
$lines | Set-Content -LiteralPath $batch -Encoding ASCII
& $batch
if ($LASTEXITCODE -ne 0) { throw "guideXOS runtime-pack platform object build failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $object)) { throw "Runtime-pack object was not produced: $object" }

$sourceHash = Get-Hash $source
$objectHash = Get-Hash $object
$lockPath = Join-Path $RuntimePackRoot "runtime-pack.lock.json"
$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw "Unable to read guideXOS repository HEAD." }
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
if ($LASTEXITCODE -ne 0) { $repoUpstream = $null }
$repoAheadBehind = $null
if ($repoUpstream) {
    $repoAheadBehind = ((& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim())
    if ($LASTEXITCODE -ne 0) { $repoAheadBehind = $null }
}
$fpRepairResult = if ($NativeAotFpRepair -and (Test-Path -LiteralPath $fpRepairResultPath -PathType Leaf)) {
    Get-Content -LiteralPath $fpRepairResultPath -Raw | ConvertFrom-Json
} else { $null }
if ($NativeAotFpRepair -and ($null -eq $fpRepairResult -or $fpRepairResult.stateAfter -ne "PATCHED_CORRECTLY")) {
    throw "C51 patch result manifest does not prove PATCHED_CORRECTLY."
}
$sourceFileHashes = [ordered]@{}
if ($NativeAotFpRepair) {
    foreach ($property in $fpRepairResult.targetFiles.PSObject.Properties) {
        $sourceFileHashes[$property.Name] = [ordered]@{
            path = $property.Value.path
            sha256 = $property.Value.sha256After
        }
    }
}
$manifest = [ordered]@{
    schemaVersion = 2
    c51Identifier = if ($NativeAotFpRepair) { "C011EC51" } else { $null }
    identity = if ($C188R12R4R5R1InitDiagnostics) { "guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5r1-init-attribution-v1" } elseif ($C188R12R4R5GcOwnershipProof) { "guidexos-nativeaot-runtime-pack-amd64-c188r12r4r5-gc-ownership-proof-v1" } elseif ($C188R12R4FactoryAttribution) { "guidexos-nativeaot-runtime-pack-amd64-c188r12r4-factory-attribution-v1" } elseif ($ThreadStaticLifecycleDiagnostics) { "guidexos-nativeaot-runtime-pack-amd64-c108-threadstatic-lifecycle-v1" } elseif ($PersistentCompositeLifecycle) { "guidexos-nativeaot-runtime-pack-amd64-persistent-composite-lifecycle-v1" } elseif ($ProductionApplication) { "guidexos-nativeaot-runtime-pack-amd64-production-application-v1" } elseif ($NativeAotFpRepair) { "guidexos-nativeaot-runtime-pack-amd64-workstationgc-fp-repair-v1" } elseif ($ManagedRepeatedAllocation) { "guidexos-nativeaot-runtime-pack-amd64-hostlog-repeated-allocation-nocollection-v1" } elseif ($ManagedAllocation) { "guidexos-nativeaot-runtime-pack-amd64-hostlog-allocating-nocollection-v1" } else { "guidexos-nativeaot-runtime-pack-amd64-hostlog-nonallocating-v1" }
    repository = [ordered]@{ root = $RepoRoot; head = $repoHead; subject = $repoSubject; branch = (& git -C $RepoRoot branch --show-current).Trim(); upstream = $repoUpstream; aheadBehind = $repoAheadBehind }
    runtimeIdentity = [ordered]@{ nativeAot = $lock.ilCompiler.version; architecture = "AMD64"; gc = "Workstation"; gcInterface = "5.3"; eeInterface = "2"; targetFramework = $lock.targetFramework; runtimeIdentifier = $lock.runtimeIdentifier; sourceCommit = $lock.ilCompiler.commit }
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
        "RhpNewFast" = if ($C188R12R4FactoryAttribution) { "guideXosStockRhpNewFast" } else { $null }
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
    externalRuntimeCheckoutHead = if ([string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) { $null } else { $externalCheckoutHead }
    externalRuntimeCommit = if ([string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) { $null } else { $externalCommit }
    nativeAotFpRepair = [bool]$NativeAotFpRepair
    nativeAotFpRepairPatchVersion = if ($NativeAotFpRepair) { $lock.nativeAotFpRepair.patchVersion } else { $null }
    nativeAotFpRepairPatch = if ($NativeAotFpRepair) { $fpPatch } else { $null }
    nativeAotFpRepairPatchSha256 = if ($NativeAotFpRepair) { Get-Hash $fpPatch } else { $null }
    nativeAotFpRepairStateBefore = if ($NativeAotFpRepair) { $fpRepairResult.stateBefore } else { $null }
    nativeAotFpRepairStateAfter = if ($NativeAotFpRepair) { $fpRepairResult.stateAfter } else { $null }
    nativeAotFpRepairAction = if ($NativeAotFpRepair) { $fpRepairResult.action } else { $null }
    nativeAotFpRepairSourceRoot = if ($NativeAotFpRepair) { $fpSourceRoot } else { $null }
    nativeAotFpRepairSourceCommit = if ($NativeAotFpRepair) { $lock.ilCompiler.commit } else { $null }
    nativeAotFpRepairSourceRevisionMarker = if ($NativeAotFpRepair) { Join-Path $fpSourceRoot ".guidexos-runtime-source-commit" } else { $null }
    nativeAotFpRepairSourceFiles = if ($NativeAotFpRepair) { $sourceFileHashes } else { $null }
    nativeAotFpRepairObjects = if ($NativeAotFpRepair) { [ordered]@{ stackFrameIterator = $fpStackObject; stackFrameIteratorSha256 = Get-Hash $fpStackObject; coffNativeCodeManager = $fpCoffObject; coffNativeCodeManagerSha256 = Get-Hash $fpCoffObject } } else { $null }
    nativeAotFpRepairMembers = if ($NativeAotFpRepair) { @($fpStackMember, $fpCoffMember) } else { @() }
    archiveMembership = $archiveValidation
    buildCommandIdentity = [ordered]@{ script = $MyInvocation.MyCommand.Path; scriptSha256 = Get-Hash $MyInvocation.MyCommand.Path; arguments = $MyInvocation.Line; freshOutputRoot = $true; cleanRequested = [bool]$Clean }
    staleArtifactProtection = [ordered]@{ result = "PASS"; ownedOutputRoot = $OutputRoot; rejectedExistingOutputWithoutClean = [bool]$NativeAotFpRepair; sourceArchiveCreatedFromLockedCommit = [bool]$NativeAotFpRepair; staleTargetedIntermediatesRemoved = [bool]$Clean }
    semanticRewriteGuard = [ordered]@{ result = "PASS"; c46SemanticCompileDefine = $false; c47SemanticCompileDefine = $false; c48SemanticCompileDefine = $false; generatedStackFrameIteratorReplacement = $false; productionizedValidationRequiresDurablePatch = [bool]$NativeAotFpRepair }
    platformObject = $lock.platformObject
    liveWindowsImportsReplaced = @("FlsGetValue", "FlsSetValue", "RhpReversePInvoke", "RhpReversePInvokeReturn")
    managedAllocation = [bool]$ManagedAllocation
    c188r12r4FactoryAttribution = [bool]$C188R12R4FactoryAttribution
    c188r12r4r5GcOwnershipProof = [bool]$C188R12R4R5GcOwnershipProof
    c188r12r4r5r1InitDiagnostics = [bool]$C188R12R4R5R1InitDiagnostics
    c188r12r4r5r1RuntimeVersionHeader = $runtimeVersionHeader
    c188r12r4r5r1RuntimeVersionHeaderSha256 = $runtimeVersionHeaderSha256
    initializationTraceRecordCapacity = if ($C188R12R4R5R1InitDiagnostics) { 64 } else { 0 }
    initializationTraceSerialLineLimit = if ($C188R12R4R5R1InitDiagnostics) { 96 } else { 0 }
    allocationMode = if ($ManagedRepeatedAllocation) { "Repeated" } elseif ($ManagedAllocation) { "Allocating" } else { "NonAllocating" }
    allocationStrategy = if ($C188R12R4R5GcOwnershipProof) { "experimental-nativeaot-workstation-gc-object-and-array-allocation-no-c188-arena" } elseif ($C188R12R4FactoryAttribution) { "bounded-static-image-backed-arrays-with-stock-object-fallback-observed" } elseif ($ManagedRepeatedAllocation) { "bounded-static-image-backed-no-collection-with-proof-specific-preflight-oom" } elseif ($ManagedAllocation) { "bounded-static-image-backed-no-collection" } else { "none" }
    objectAllocationFallback = if ($C188R12R4FactoryAttribution) { "stock-RhpNewFast-preserved-and-instrumented" } else { "stock-RhpNewFast" }
    managedHeapConfiguration = if ($ManagedAllocation -and -not $C188R12R4R5GcOwnershipProof) { $HeapConfiguration } else { "None" }
    managedHeapBytes = if ($ManagedAllocation -and -not $C188R12R4R5GcOwnershipProof) { $managedHeapBytes } else { 0 }
    imageBackedArenaPresent = -not [bool]$C188R12R4R5GcOwnershipProof
    managedArrayLength = if ($C188R12R4R5GcOwnershipProof) { 0 } elseif ($ManagedRepeatedAllocation) { 256 } elseif ($ManagedAllocation) { 24 } else { 0 }
    managedObjectSize = if ($C188R12R4R5GcOwnershipProof) { 0 } elseif ($ManagedRepeatedAllocation) { 280 } elseif ($ManagedAllocation) { 40 } else { 0 }
    managedExceptions = $false
    managedThreads = $false
    productionApplication = [bool]$ProductionApplication
    persistentCompositeLifecycle = [bool]$PersistentCompositeLifecycle
    threadStaticLifecycleDiagnostics = [bool]$ThreadStaticLifecycleDiagnostics
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding ASCII

Write-Host "[runtime-pack] built guideXOS AMD64 platform object" -ForegroundColor Green
Write-Host "[runtime-pack] object=$object" -ForegroundColor Cyan
Write-Host "[runtime-pack] sha256=$objectHash" -ForegroundColor Cyan
Write-Host "[runtime-pack] manifest=$manifestPath" -ForegroundColor Cyan
Write-Output $object
