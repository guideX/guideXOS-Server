param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path,
    [string]$RuntimePackRoot = $PSScriptRoot,
    [string]$OutputRoot = "",
    [string]$StockRuntimePackRoot = "",
    [string]$ExternalRuntimeRoot = "",
    [switch]$ManagedAllocation,
    [switch]$ManagedRepeatedAllocation,
    [switch]$NativeAotFpRepair,
    [ValidateSet("Primary64KiB", "Small4KiB")]
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
$managedHeapBytes = if ($HeapConfiguration -eq "Primary64KiB") { 65536 } else { 4096 }
if ($ManagedRepeatedAllocation) { $ManagedAllocation = $true }

if (-not [string]::IsNullOrWhiteSpace($ExternalRuntimeRoot)) {
    $ExternalRuntimeRoot = [System.IO.Path]::GetFullPath($ExternalRuntimeRoot)
    if (-not (Test-Path -LiteralPath (Join-Path $ExternalRuntimeRoot ".git"))) {
        throw "External runtime root is not a Git checkout: $ExternalRuntimeRoot"
    }
    $externalCommit = (& git -C $ExternalRuntimeRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Unable to read external runtime checkout revision: $ExternalRuntimeRoot" }
    $externalCheckoutHead = $externalCommit
    $externalStatus = @(& git -C $ExternalRuntimeRoot status --porcelain 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "Unable to read external runtime checkout status: $ExternalRuntimeRoot" }
    if ($externalStatus.Count -ne 0) {
        throw "External runtime checkout is dirty; C51 requires a clean source acquisition checkout: $ExternalRuntimeRoot"
    }
    if (-not $NativeAotFpRepair -and $externalCommit -ne $lock.ilCompiler.commit) {
        throw "External runtime checkout revision mismatch. Expected $($lock.ilCompiler.commit), got $externalCommit"
    }
    if ($NativeAotFpRepair) {
        & git -C $ExternalRuntimeRoot cat-file -e "$($lock.ilCompiler.commit)`^{commit}"
        if ($LASTEXITCODE -ne 0) {
            throw "Locked NativeAOT source commit is not available in the external checkout: $($lock.ilCompiler.commit)"
        }
        $externalCommit = $lock.ilCompiler.commit
    }
}

$source = Join-Path $RuntimePackRoot $lock.platformObject.source.Replace('/', '\')
if (-not (Test-Path -LiteralPath $source)) { throw "Runtime-pack platform source not found: $source" }

if ($NativeAotFpRepair -and (Test-Path -LiteralPath $OutputRoot) -and -not $Clean) {
    throw "C51 stale-artifact protection: NativeAOT FP repair output already exists. Use -Clean or a fresh isolated OutputRoot: $OutputRoot"
}
if ($Clean -and (Test-Path -LiteralPath $OutputRoot)) { Remove-Item -LiteralPath $OutputRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
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
$adaptedRuntimeLibrary = Join-Path $sdkOutput "Runtime.WorkstationGC.lib"
$removedRuntimeMembers = @(
    "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\EHHelpers.cpp.obj",
    "nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\thread.cpp.obj"
)
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
    & $objcopy --redefine-sym RhpNewArray=guideXosStockRhpNewArray $allocFastRenamedMember
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
$sdkLines = @(
    "@echo off",
    "setlocal",
    "call `"$vcvars`" >nul",
    "if errorlevel 1 exit /b %errorlevel%",
    "lib.exe /nologo /OUT:`"$adaptedRuntimeLibrary`" `"$stockRuntimeLibrary`" $removeArguments `"$threadRenamedMember`" `"$ehRenamedMember`"$allocFastMemberArguments$fpArchiveObjects",
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
    identity = if ($NativeAotFpRepair) { "guidexos-nativeaot-runtime-pack-amd64-workstationgc-fp-repair-v1" } elseif ($ManagedRepeatedAllocation) { "guidexos-nativeaot-runtime-pack-amd64-hostlog-repeated-allocation-nocollection-v1" } elseif ($ManagedAllocation) { "guidexos-nativeaot-runtime-pack-amd64-hostlog-allocating-nocollection-v1" } else { "guidexos-nativeaot-runtime-pack-amd64-hostlog-nonallocating-v1" }
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
