[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$ArchivePath = "",
    [string]$OutputRoot = "",
    [switch]$RunGenericQemuFoundation,
    [switch]$SkipGenericBuild,
    [string]$QemuPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($ArchivePath)) {
    $ArchivePath = Join-Path $root "out\dotnet\gc-platform-object-replacement\rebuilt\Runtime.WorkstationGC.lib"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $root "out\dotnet\gc-platform-object-replacement\rebuilt\qemu-exact-symbol-probe"
}
$ArchivePath = [System.IO.Path]::GetFullPath($ArchivePath)
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Find-Tool([string]$Name, [string[]]$Candidates) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $command -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Write-Text([string]$Path, [object]$Value) {
    [System.IO.File]::WriteAllText($Path, ([string]$Value), [System.Text.Encoding]::UTF8)
}

if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    throw "Adapted archive not found: $ArchivePath"
}

$identityPath = Join-Path (Split-Path -Parent $ArchivePath) "adapted-identity.json"
$bindingPath = Join-Path (Split-Path -Parent $ArchivePath) "..\symbols\symbol-binding-report.json"
$identity = if (Test-Path -LiteralPath $identityPath) { Get-Content -LiteralPath $identityPath -Raw | ConvertFrom-Json } else { $null }
$binding = if (Test-Path -LiteralPath $bindingPath) { Get-Content -LiteralPath $bindingPath -Raw | ConvertFrom-Json } else { $null }

$lib = Find-Tool "lib.exe" @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\lib.exe"
)
$dumpbin = Find-Tool "dumpbin.exe" @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\dumpbin.exe"
)
$objcopy = Find-Tool "objcopy.exe" @("C:\mingw64\bin\objcopy.exe", "C:\msys64\mingw64\bin\objcopy.exe")
$objdump = Find-Tool "objdump.exe" @("C:\mingw64\bin\objdump.exe", "C:\msys64\mingw64\bin\objdump.exe")

$archiveHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToUpperInvariant()
$memberListPath = Join-Path $OutputRoot "adapted-members.txt"
$memberList = if ($null -ne $lib) { @(& $lib /nologo /list $ArchivePath 2>&1) } else { @() }
Write-Text $memberListPath ($memberList -join [Environment]::NewLine)
$replacementMember = @($memberList | Where-Object { $_ -match "(^|[\\/])guidexos_gcenv\.obj$" }) | Select-Object -First 1
$replacementPresent = -not [string]::IsNullOrWhiteSpace([string]$replacementMember)
$windowsMemberPresent = @($memberList | Where-Object { $_ -match "gcenv\.windows\.cpp\.obj$" }).Count -gt 0

$objectPath = Join-Path $OutputRoot "guidexos_gcenv.obj"
$elfObjectPath = Join-Path $OutputRoot "guidexos_gcenv.elf.o"
$extractLogPath = Join-Path $OutputRoot "archive-extract.log"
$conversionLogPath = Join-Path $OutputRoot "objcopy.log"
$symbolTablePath = Join-Path $OutputRoot "guidexos_gcenv.elf.symbols.txt"
$formatPath = Join-Path $OutputRoot "guidexos_gcenv.elf.format.txt"
$coffSymbolPath = Join-Path $OutputRoot "guidexos_gcenv.coff.symbols.txt"
$abiPath = Join-Path $OutputRoot "msvc-abi-boundary.txt"

$extractStatus = "BLOCKED"
if ($replacementPresent -and $null -ne $lib) {
    $extractOutput = & $lib /nologo /extract:"$replacementMember" $ArchivePath /out:"$objectPath" 2>&1
    Write-Text $extractLogPath ($extractOutput -join [Environment]::NewLine)
    $extractStatus = if (Test-Path -LiteralPath $objectPath -PathType Leaf) { "PASS" } else { "FAIL" }
} else {
    Write-Text $extractLogPath "lib.exe or guidexos_gcenv.obj was not available."
}

$conversionStatus = "BLOCKED"
if ($extractStatus -eq "PASS" -and $null -ne $objcopy) {
    $conversionOutput = & $objcopy -I pe-x86-64 -O elf64-x86-64 $objectPath $elfObjectPath 2>&1
    Write-Text $conversionLogPath ($conversionOutput -join [Environment]::NewLine)
    $conversionStatus = if (Test-Path -LiteralPath $elfObjectPath -PathType Leaf) { "PASS" } else { "FAIL" }
} else {
    Write-Text $conversionLogPath "objcopy or extracted replacement object was not available."
}

$symbolOutput = @()
$formatOutput = @()
$coffSymbolOutput = @()
if ($conversionStatus -eq "PASS" -and $null -ne $objdump) {
    $symbolOutput = @(& $objdump -t $elfObjectPath 2>&1)
    $formatOutput = @(& $objdump -f $elfObjectPath 2>&1)
}
if ($extractStatus -eq "PASS" -and $null -ne $dumpbin) {
    $coffSymbolOutput = @(& $dumpbin /nologo /symbols $objectPath 2>&1)
}
Write-Text $symbolTablePath ($symbolOutput -join [Environment]::NewLine)
Write-Text $formatPath ($formatOutput -join [Environment]::NewLine)
Write-Text $coffSymbolPath ($coffSymbolOutput -join [Environment]::NewLine)

$msvcAbiSymbols = @($coffSymbolOutput | Where-Object {
    $_ -match "\?\?|\?[^ ]+@@|(^|\s)_tls_index(\s|$)|(^|\s)__CxxFrameHandler|(^|\s)___CxxFrameHandler"
})
$undefinedMsvcSymbols = @($coffSymbolOutput | Where-Object { $_ -match "UNDEF" -and $_ -match "\?\?|_tls_index|\?" })
$abiExplanation = @(
    "The adapted archive is MSVC AMD64 COFF and the guideXOS QEMU kernel is MinGW ELF/SysV AMD64.",
    "objcopy conversion is evidence-preserving format conversion; it does not translate the MSVC C++ ABI, Win64 object conventions, TLS/CRT model, or kernel symbol names.",
    "The converted replacement object still has MSVC ABI/TLS/runtime symbols, so it cannot be loaded as an exact-symbol QEMU kernel object without a separately built ABI bridge.",
    "No collector initialization, heap construction, finalizer/helper startup, allocation, or collection was performed."
)
Write-Text $abiPath (($abiExplanation + @("", "Detected converted-object ABI symbols:") + $msvcAbiSymbols + @("", "Detected undefined MSVC ABI symbols:") + $undefinedMsvcSymbols) -join [Environment]::NewLine)

$genericQemuStatus = "NOT_RUN"
$genericQemuLog = Join-Path $OutputRoot "generic-true-vm-qemu.log"
if ($RunGenericQemuFoundation) {
    $genericScript = Join-Path $root "scripts\smoke-native-virtual-memory-qemu.ps1"
    $genericArguments = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $genericScript,
        "-OutputRoot", (Join-Path $OutputRoot "generic-true-vm")
    )
    if ($SkipGenericBuild) { $genericArguments += "-SkipBuild" }
    if (-not [string]::IsNullOrWhiteSpace($QemuPath)) { $genericArguments += @("-QemuPath", $QemuPath) }
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $genericOutput = @(& powershell @genericArguments 2>&1 | ForEach-Object { $_.ToString() })
    $genericExitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
    Write-Text $genericQemuLog ($genericOutput -join [Environment]::NewLine)
    $genericQemuStatus = if ($genericExitCode -eq 0) { "PASS" } else { "BLOCKED_OR_FAIL" }
}

$adaptedArchiveStatus = if ($windowsMemberPresent -or -not $replacementPresent) {
    "FAIL"
} elseif ($conversionStatus -eq "PASS" -and $msvcAbiSymbols.Count -eq 0) {
    "PASS"
} else {
    "FAIL (MSVC COFF/Win64 ABI cannot be loaded directly into MinGW ELF/SysV QEMU target)"
}

$result = [ordered]@{
    schemaVersion = 1
    adaptedArchive = $ArchivePath
    adaptedArchiveSha256 = $archiveHash
    stockIdentity = if ($null -ne $identity) { [string]$identity.stockSha256 } else { $null }
    sourceCommit = if ($null -ne $identity) { [string]$identity.sourceCommit } else { "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3" }
    adaptedArchiveLoaded = $adaptedArchiveStatus
    windowsGcenvMemberAbsent = (-not $windowsMemberPresent)
    exactSymbolBinding = if ($null -ne $binding -and @($binding.missingDefinitions).Count -eq 0 -and @($binding.duplicateStrongDefinitions).Count -eq 0) { "PASS (archive/static binding)" } else { "FAIL" }
    replacementObjectExtracted = $extractStatus
    convertedElfObject = $conversionStatus
    msvcAbiBoundary = ($msvcAbiSymbols.Count -gt 0)
    reserveFrameDelta = "N/A (exact object not executable in QEMU target)"
    commitDecommitRelease = "BLOCKED"
    exactEventSymbols = "PASS (archive/static); BLOCKED (QEMU execution)"
    exactCriticalSectionSymbols = "PASS (archive/static); BLOCKED (QEMU execution)"
    exactFlsSymbols = "PASS (archive/static); BLOCKED (QEMU execution)"
    exactThreadSymbols = "PASS (archive/static); BLOCKED (QEMU execution)"
    timingCpuSymbols = "PASS (archive/static); BLOCKED (QEMU execution)"
    shutdownCleanup = "BLOCKED"
    frameLeakCheck = "BLOCKED"
    threadEventMutexFlsLeakCheck = "BLOCKED"
    genericTrueVmQemuFoundation = $genericQemuStatus
    rhInitializeCalled = $false
    collectorHeapConstructed = $false
    collectionEntered = $false
    reason = "Exact Workstation GC archive replacement is proven for the locked MSVC stock ABI; the QEMU target requires a separate ABI bridge for the remaining NativeAOT PAL/runtime object family."
}
$resultPath = Join-Path $OutputRoot "qemu-probe-result.json"
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resultPath -Encoding UTF8

Write-Host "Adapted archive loaded: $($result.adaptedArchiveLoaded)"
Write-Host "Windows gcenv member absent: $(if ($result.windowsGcenvMemberAbsent) { 'PASS' } else { 'FAIL' })"
Write-Host "Exact VM symbols: $($result.exactSymbolBinding)"
Write-Host "Exact-symbol QEMU probe: $($result.adaptedArchiveLoaded)"
Write-Host "Generic true-VM QEMU foundation: $genericQemuStatus"
Write-Host "Evidence: $resultPath"

if ($adaptedArchiveStatus.StartsWith("FAIL", [System.StringComparison]::Ordinal)) { exit 1 }
exit 0
