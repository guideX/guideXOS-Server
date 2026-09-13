param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$DotNetExe = "dotnet",
    [string]$PythonExe = "",
    [int]$FreshBootCount = 3,
    [int]$TimeoutSeconds = 120,
    [switch]$SkipManagedBuild,
    [switch]$SkipKernelBuild,
    [switch]$SkipQemu
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ($FreshBootCount -lt 1) { throw "FreshBootCount must be at least 1." }
if ($TimeoutSeconds -lt 5) { throw "TimeoutSeconds must be at least 5." }

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec107-composite-application-dispatch"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C107 evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c107.img"
$buildScript = Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"
$stagingScript = Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"
$kernelPath = Join-Path $RepoRoot "kernel\build\amd64\bin\kernel.elf"
$bootloaderPath = Join-Path $RepoRoot "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"

function Invoke-Checked([string]$FilePath, [string[]]$ArgumentList) {
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($ArgumentList -join ' ')"
    }
}

function Resolve-Tool([string]$Name, [string[]]$Candidates) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
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

function Get-Hash([string]$Path) {
    $deadline = (Get-Date).AddSeconds(5)
    while ($true) {
        try {
            $stream = [System.IO.File]::Open(
                $Path,
                [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::Read,
                [System.IO.FileShare]::ReadWrite)
            try {
                $sha256 = [System.Security.Cryptography.SHA256]::Create()
                try {
                    $bytes = $sha256.ComputeHash($stream)
                    return (($bytes | ForEach-Object { $_.ToString('X2') }) -join '')
                } finally {
                    $sha256.Dispose()
                }
            } finally {
                $stream.Dispose()
            }
        } catch [System.IO.IOException] {
            if ((Get-Date) -ge $deadline) { throw }
            Start-Sleep -Milliseconds 100
        }
    }
}

function Wait-FileReady([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline) {
        try {
            $stream = [System.IO.File]::Open(
                $Path,
                [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::Read,
                [System.IO.FileShare]::ReadWrite)
            $stream.Dispose()
            return
        } catch [System.IO.IOException] {
            Start-Sleep -Milliseconds 100
        }
    }
}

function Quote-QemuValue([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Stage-Esp([string]$EspPath, [string]$Kernel, [string]$Bootloader, [string]$Ramdisk) {
    New-Item -ItemType Directory -Force -Path (Join-Path $EspPath "EFI\BOOT") | Out-Null
    Copy-Item -LiteralPath $Bootloader -Destination (Join-Path $EspPath "EFI\BOOT\BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $Kernel -Destination (Join-Path $EspPath "kernel.elf") -Force
    Copy-Item -LiteralPath $Ramdisk -Destination (Join-Path $EspPath "ramdisk.img") -Force
}

function Invoke-C107Boot(
    [string]$EspPath,
    [string]$SerialPath,
    [string]$StdoutPath,
    [string]$StderrPath,
    [string]$QemuPath,
    [string]$OvmfPath) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $OvmfPath)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $EspPath) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $SerialPath)),
        "-boot", "order=c", "-no-reboot", "-no-shutdown",
        "-rtc", "base=utc,clock=host")
    $process = $null
    try {
        $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $RepoRoot `
            -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -WindowStyle Hidden -PassThru
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $SerialPath) {
                $partial = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
                if ($partial -match '(?m)^\[C107-RESULT\] outcome=(?:PASS|FAIL)') { break }
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
    Wait-FileReady $SerialPath
    $serial = if (Test-Path -LiteralPath $SerialPath) {
        Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
    } else { "" }
    if ($null -eq $serial) { $serial = "" }
    return [pscustomobject]@{
        qemuExitCode = $exitCode
        serialPath = $SerialPath
        serialSha256 = if (Test-Path -LiteralPath $SerialPath -PathType Leaf) { Get-Hash $SerialPath } else { $null }
        stdoutPath = $StdoutPath
        stderrPath = $StderrPath
        serial = $serial
    }
}

function Assert-OrderedMarkers([string]$Text, [string[]]$Markers, [string]$Label) {
    $cursor = -1
    foreach ($marker in $Markers) {
        $position = $Text.IndexOf($marker, $cursor + 1, [System.StringComparison]::Ordinal)
        if ($position -lt 0) { throw "$Label missing ordered marker: $marker" }
        $cursor = $position
    }
}

function Test-C107Serial([string]$Serial) {
    Assert-OrderedMarkers $Serial @(
        "C107-APP-A-PASS",
        "C107-INVALID-APP-ID",
        "C107-APP-B-PASS",
        "C107-APP-A-PASS"
    ) "C107 semantic sequence"
    foreach ($line in @(
        "C107-APP-A-STATE count=1 threadBefore=0 threadAfter=1 allocation=PASS calculation=36",
        "C107-APP-B-STATE count=1 threadBefore=0 threadAfter=1 allocation=PASS calculation=150",
        "C107-APP-A-STATE count=2 threadBefore=0 threadAfter=1 allocation=PASS calculation=36")) {
        if ($Serial.IndexOf($line, [System.StringComparison]::Ordinal) -lt 0) {
            throw "C107 state evidence missing: $line"
        }
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C107-LAUNCH\].*status=success managedReturn=00000000')).Count -ne 3) {
        throw "C107 expected three successful logical launches."
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C107-LAUNCH\].*identity=invalid.*status=invalid-app-id managedReturn=FFFFFFFC')).Count -ne 1) {
        throw "C107 invalid selector status was not observed exactly once."
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C102-LOADER\] mapped ELF64 AMD64')).Count -ne 1) {
        throw "C107 composite image was not mapped exactly once."
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C103-LOADER\] reusing resident')).Count -ne 3) {
        throw "C107 expected three resident image reentries."
    }
    for ($stage = 1; $stage -le 7; $stage++) {
        if (@([regex]::Matches($Serial, "(?m)^\[C102-STARTUP\] stage=0000000$stage\r?$")).Count -ne 1) {
            throw "C107 NativeAOT startup stage $stage was not observed exactly once."
        }
    }
    if ($Serial -notmatch '(?m)^\[C107-RESULT\] outcome=PASS') {
        throw "C107 result marker was not PASS."
    }
    if ($Serial -notmatch '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$') {
        throw "guideXOS did not return to its main loop after C107."
    }
    if ($Serial -match '(?i)PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "C107 serial output contains a fault/fail-fast marker."
    }
    return [pscustomobject]@{
        outcome = "PASS"
        sequence = "A PASS -> invalid rejected -> B PASS -> A PASS"
        mapCount = 1
        residentReentryCount = 3
        startupStageCounts = "1,1,1,1,1,1,1"
        mainLoop = $true
    }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null

if (-not $SkipManagedBuild) {
    if ([string]::IsNullOrWhiteSpace($PythonExe)) {
        $PythonExe = Resolve-Tool "python" @(
            "C:\Python312\python.exe",
            "C:\Python311\python.exe",
            "C:\Users\guideX\AppData\Local\Programs\Python\Python312\python.exe")
    }
    if ([string]::IsNullOrWhiteSpace($PythonExe)) {
        throw "Python was not found; pass -PythonExe to the C107 runner."
    }
    $buildArguments = @(
        "-ExecutionPolicy", "Bypass", "-File", $buildScript,
        "-RepoRoot", $RepoRoot,
        "-OutputRoot", $compositeBuildRoot,
        "-RuntimePackRoot", (Join-Path $RepoRoot "tools\dotnet\runtime-pack"),
        "-RuntimePackOutputRoot", $runtimePackOutputRoot,
        "-UseGuideXosRuntimePack", "-ProductionApplication",
        "-AllocationMode", "Allocating",
        "-ManagedProjectMode", "C107Composite",
        "-PythonExe", $PythonExe, "-Clean")
    if ($DotNetExe -ne "dotnet") { $buildArguments += @("-DotNetExe", $DotNetExe) }
    Invoke-Checked "powershell" $buildArguments
}

$compositeElf = Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf"
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) {
    throw "C107 composite ELF is missing: $compositeElf"
}

Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot,
    "-OutputImage", $stagingImage,
    "-C107CompositePath", $compositeElf)

if (-not $SkipKernelBuild) {
    Invoke-Checked "mingw32-make" @(
        "-C", (Join-Path $RepoRoot "kernel"), "-B", "ARCH=amd64",
        "EXTRA_CFLAGS=-DGXOS_DESKTOP_CLEANUP_RUNTIME_PASS=2 -DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_C107_PRODUCTION_LAUNCH")
}

if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

$compositeMap = Join-Path $compositeBuildRoot "artifacts\HostLogProof.map"
$compositeReadelf = Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf.readelf.txt"
$evidenceInputs = [ordered]@{
    compositeElf = $compositeElf
    compositeMap = $compositeMap
    compositeReadelf = $compositeReadelf
    compositeElfSha256 = Get-Hash $compositeElf
    compositeSize = (Get-Item -LiteralPath $compositeElf).Length
    kernel = $kernelPath
    kernelSha256 = Get-Hash $kernelPath
    bootloader = $bootloaderPath
    bootloaderSha256 = Get-Hash $bootloaderPath
    ramdisk = $stagingImage
    ramdiskSha256 = Get-Hash $stagingImage
}
$evidenceInputs | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "inputs.json") -Encoding ASCII

$bootResults = [System.Collections.Generic.List[object]]::new()
if (-not $SkipQemu) {
    $qemuPath = Resolve-Tool "qemu-system-x86_64.exe" @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe",
        "C:\msys64\mingw64\bin\qemu-system-x86_64.exe")
    $ovmfPath = Resolve-Tool "edk2-x86_64-code.fd" @(
        (Join-Path $RepoRoot "OVMF.fd"),
        (Join-Path $RepoRoot "ovmf.fd"),
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd")
    if ([string]::IsNullOrWhiteSpace($qemuPath)) { throw "qemu-system-x86_64.exe was not found." }
    if ([string]::IsNullOrWhiteSpace($ovmfPath)) { throw "OVMF code image was not found." }
    for ($index = 1; $index -le $FreshBootCount; $index++) {
        $bootRoot = Join-Path $EvidenceRoot ("boot-{0:D2}" -f $index)
        $espRoot = Join-Path $bootRoot "ESP"
        New-Item -ItemType Directory -Force -Path $bootRoot | Out-Null
        Stage-Esp $espRoot $kernelPath $bootloaderPath $stagingImage
        $serialPath = Join-Path $bootRoot "serial.log"
        $stdoutPath = Join-Path $bootRoot "qemu.stdout.log"
        $stderrPath = Join-Path $bootRoot "qemu.stderr.log"
        $boot = Invoke-C107Boot $espRoot $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        $classification = $null
        try { $classification = Test-C107Serial $boot.serial } catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message; sequence = ""; mapCount = $null; residentReentryCount = $null; startupStageCounts = ""; mainLoop = $false } }
        $bootResults.Add([pscustomobject]@{
            boot = $index
            outcome = $classification.outcome
            classification = $classification
            serialPath = $boot.serialPath
            serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath
            stderrPath = $boot.stderrPath
            qemuExitCode = $boot.qemuExitCode
        }) | Out-Null
        Write-Host ("[C107] boot={0} outcome={1} serial={2}" -f $index, $classification.outcome, $serialPath)
        if ($classification.outcome -ne "PASS") { throw "C107 fresh boot $index failed: $($classification.error)" }
    }
}

$manifest = [ordered]@{
    schemaVersion = 1
    phase = "C107"
    outcome = if ($SkipQemu) { "BUILD_ONLY" } elseif (@($bootResults | Where-Object { $_.outcome -ne "PASS" }).Count -eq 0) { "PASS" } else { "FAIL" }
    repository = $RepoRoot
    compositeProject = (Join-Path $RepoRoot "samples\managed\HostLogProof\HostLogProof.csproj")
    compositeMode = "C107Composite"
    selectorAbi = "NativeGxAppContext.userData = uint32 app ID encoded as a pointer-sized scalar"
    appAId = 1
    appBId = 2
    invalidId = 0
    sequence = "A PASS -> invalid rejected -> B PASS -> A PASS"
    freshBootCount = $FreshBootCount
    qemuExecuted = -not $SkipQemu
    inputs = $evidenceInputs
    boots = @($bootResults)
    independentImageBusyGuard = "retained; C107 uses one ELF path and never registers a second image"
    runtimeChanges = "none"
    gcChanges = "none"
    documentation = "docs/dotnet/NATIVEAOT_C107_COMPOSITE_APPLICATION_DISPATCH.md"
}
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "c107.manifest.json") -Encoding ASCII
Write-Host "C107 outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
if ($manifest.outcome -eq "FAIL") { exit 1 }
exit 0
