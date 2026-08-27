[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 120,
    [int]$FreshBootCount = 3
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($FreshBootCount -lt 1) { throw "FreshBootCount must be at least 1." }
if ($TimeoutSeconds -lt 5) { throw "TimeoutSeconds must be at least 5." }

if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.." )).Path }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c51-ordinary-boot-validator"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedEvidenceRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedEvidenceRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Ordinary-boot evidence must remain under $allowedEvidenceRoot"
}

function Get-Hash([string]$Path) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try { return ([BitConverter]::ToString($sha256.ComputeHash($stream)) -replace '-', '').ToUpperInvariant() }
    finally {
        $stream.Dispose()
        $sha256.Dispose()
    }
}

function Require-File([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label is missing: $Path" }
}

function Find-Executable([string]$CommandName, [string[]]$Candidates) {
    $command = Get-Command $CommandName -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) { return $command.Source }
    foreach ($candidate in $Candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    return $null
}

function Quote-QemuValue([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Stage-Esp([string]$EspPath, [string]$KernelPath, [string]$BootloaderPath, [string]$RamdiskPath) {
    New-Item -ItemType Directory -Force -Path (Join-Path $EspPath "EFI\BOOT") | Out-Null
    Copy-Item -LiteralPath $BootloaderPath -Destination (Join-Path $EspPath "EFI\BOOT\BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $KernelPath -Destination (Join-Path $EspPath "kernel.elf") -Force
    Copy-Item -LiteralPath $RamdiskPath -Destination (Join-Path $EspPath "ramdisk.img") -Force
}

function Invoke-OrdinaryBoot {
    param(
        [string]$EspPath,
        [string]$SerialPath,
        [string]$StdoutPath,
        [string]$StderrPath,
        [string]$QemuPath,
        [string]$OvmfPath
    )
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $OvmfPath)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $EspPath) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $SerialPath)),
        "-boot", "order=c",
        "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host"
    )
    $process = $null
    try {
        $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $RepoRoot `
            -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -WindowStyle Hidden -PassThru
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $SerialPath) {
                $partial = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
                if ($partial -match '(?m)^\[NAVIGATOR-SMOKE\] result=(?:PASS|FAIL)\s*$') { break }
            }
            if ($process.HasExited) { break }
        }
    }
    finally {
        if ($null -ne $process) {
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
            }
        }
    }

    $exitCode = $null
    if ($null -ne $process) {
        $process.Refresh()
        $exitCode = $process.ExitCode
    }
    $serial = if (Test-Path -LiteralPath $SerialPath) { Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue } else { "" }
    if ($null -eq $serial) { $serial = "" }
    $navigatorPass = $serial -match '(?m)^\[NAVIGATOR-SMOKE\] result=PASS\s*$'
    $navigatorFail = $serial -match '(?m)^\[NAVIGATOR-SMOKE\] result=FAIL\s*$'
    $mainLoop = $serial -match '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\s*$'
    $pageFault = $serial -match '(?im)\[PageFault\]|page.?fault|triple.?fault'
    $failFast = $serial -match '(?im)FAIL[_ -]?FAST|fatal kernel failure|boot failure'
    $proofRewrite = $serial -match '(?im)C011EC46|C011EC47|C011EC48|productionized-second-collection'
    $result = $mainLoop -and $navigatorPass -and -not $navigatorFail -and -not $pageFault -and -not $failFast -and -not $proofRewrite
    [pscustomobject]@{
        result = if ($result) { "PASS" } else { "FAIL" }
        mainLoopMarker = [bool]$mainLoop
        navigatorPassMarker = [bool]$navigatorPass
        navigatorFailMarker = [bool]$navigatorFail
        pageFaultAbsent = -not $pageFault
        failFastAbsent = -not $failFast
        semanticProofMarkersAbsent = -not $proofRewrite
        qemuExitCode = $exitCode
        serialPath = $SerialPath
        serialSha256 = if (Test-Path -LiteralPath $SerialPath -PathType Leaf) { Get-Hash $SerialPath } else { $null }
        stdoutPath = $StdoutPath
        stderrPath = $StderrPath
    }
}

$runId = "run-" + (Get-Date -Format "yyyyMMdd-HHmmssfff") + "-" + ([guid]::NewGuid().ToString("N").Substring(0, 8))
$runRoot = Join-Path $EvidenceRoot $runId
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null
$manifestPath = Join-Path $runRoot "ordinary-boot.manifest.json"
$kernelPath = Join-Path $RepoRoot "kernel\build\amd64\bin\kernel.elf"
$espPath = Join-Path $RepoRoot "ESP\kernel.elf"
$bootloaderPath = Join-Path $RepoRoot "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$ramdiskPath = Join-Path $RepoRoot "ESP\ramdisk.img"
$qemuPath = Find-Executable "qemu-system-x86_64.exe" @(
    "C:\Program Files\qemu\qemu-system-x86_64.exe",
    "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
    "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
    "C:\qemu\qemu-system-x86_64.exe",
    "C:\msys64\mingw64\bin\qemu-system-x86_64.exe"
)
$ovmfPath = $null
foreach ($candidate in @(
    (Join-Path $RepoRoot "OVMF.fd"),
    (Join-Path $RepoRoot "ovmf.fd"),
    "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
    "C:\Program Files\qemu\share\edk2-x86_64-vars.fd"
)) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $ovmfPath = (Resolve-Path -LiteralPath $candidate).Path; break }
}
$requiredInputs = [ordered]@{
    kernel = $kernelPath
    espKernel = $espPath
    bootloader = $bootloaderPath
    ramdisk = $ramdiskPath
    qemu = $qemuPath
    ovmf = $ovmfPath
}
$bootResults = [System.Collections.Generic.List[object]]::new()
$failure = $null
$beforeHashes = $null
$afterHashes = $null

try {
    foreach ($entry in $requiredInputs.GetEnumerator()) {
        if ([string]::IsNullOrWhiteSpace([string]$entry.Value) -or -not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
            throw "Ordinary-boot input missing: $($entry.Key) = $($entry.Value)"
        }
    }
    $beforeHashes = [ordered]@{
        kernel = Get-Hash $kernelPath
        espKernel = Get-Hash $espPath
        bootloader = Get-Hash $bootloaderPath
        ramdisk = Get-Hash $ramdiskPath
    }
    if ($beforeHashes.kernel -ne $beforeHashes.espKernel) {
        throw "Ordinary kernel and ESP kernel differ before validation."
    }
    $qemuVersion = (Get-Item -LiteralPath $qemuPath).VersionInfo.FileVersion
    $qemuVersionText = if ([string]::IsNullOrWhiteSpace($qemuVersion)) { $qemuPath } else { $qemuVersion }
    Set-Content -LiteralPath (Join-Path $runRoot "qemu-version.txt") -Value $qemuVersionText -Encoding ASCII

    for ($index = 1; $index -le $FreshBootCount; $index++) {
        $bootRoot = Join-Path $runRoot ("boot-{0:D2}" -f $index)
        $bootEsp = Join-Path $bootRoot "ESP"
        New-Item -ItemType Directory -Force -Path $bootRoot | Out-Null
        Stage-Esp $bootEsp $kernelPath $bootloaderPath $ramdiskPath
        $serialPath = Join-Path $bootRoot "serial.log"
        $stdoutPath = Join-Path $bootRoot "qemu.stdout.log"
        $stderrPath = Join-Path $bootRoot "qemu.stderr.log"
        $boot = Invoke-OrdinaryBoot $bootEsp $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        $bootResults.Add($boot) | Out-Null
        Write-Host ("[C51 ordinary] boot={0} result={1} mainLoop={2} navigator={3}" -f $index, $boot.result, $boot.mainLoopMarker, $boot.navigatorPassMarker)
    }
    $afterHashes = [ordered]@{
        kernel = Get-Hash $kernelPath
        espKernel = Get-Hash $espPath
        bootloader = Get-Hash $bootloaderPath
        ramdisk = Get-Hash $ramdiskPath
    }
    if ($beforeHashes.kernel -ne $afterHashes.kernel -or $beforeHashes.espKernel -ne $afterHashes.espKernel) {
        throw "Ordinary kernel/ESP hashes changed during validator execution."
    }
}
catch {
    $failure = $_.Exception.Message
    Write-Host "[C51 ordinary] FAIL: $failure" -ForegroundColor Red
}

$overallPass = $null -eq $failure -and $bootResults.Count -eq $FreshBootCount -and
    @($bootResults | Where-Object { $_.result -ne "PASS" }).Count -eq 0
$manifest = [ordered]@{
    schemaVersion = 1
    c51Identifier = "C011EC51"
    validator = "precise-ordinary-boot"
    outcome = if ($overallPass) { "PASS" } else { "FAIL" }
    failure = $failure
    freshBootCount = $FreshBootCount
    timeoutSeconds = $TimeoutSeconds
    repositoryRoot = $RepoRoot
    inputPaths = $requiredInputs
    inputHashesBefore = $beforeHashes
    inputHashesAfter = $afterHashes
    noCanonicalKernelMutation = ($null -ne $beforeHashes -and $null -ne $afterHashes -and $beforeHashes.kernel -eq $afterHashes.kernel -and $beforeHashes.espKernel -eq $afterHashes.espKernel)
    requiredMarkers = @(
        "[KERNEL] Entering main loop (waiting for input)...",
        "[NAVIGATOR-SMOKE] result=PASS"
    )
    forbiddenMarkers = @("PageFault/triple-fault", "FAIL_FAST/fatal kernel failure", "C011EC46/C011EC47/C011EC48/productionized-second-collection")
    boots = @($bootResults)
    evidenceRoot = $runRoot
    manifestPath = $manifestPath
}
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
Write-Host "[C51 ordinary] outcome=$($manifest.outcome) boots=$($bootResults.Count) manifest=$manifestPath"
if (-not $overallPass) { exit 1 }
exit 0
