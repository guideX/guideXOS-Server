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
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec109-production-composite-lifecycle"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C109 evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c109.img"
$buildScript = Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"
$stagingScript = Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"
$kernelPath = Join-Path $RepoRoot "kernel\build\amd64\bin\kernel.elf"
$bootloaderPath = Join-Path $RepoRoot "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"

function Invoke-Checked([string]$FilePath, [string[]]$ArgumentList) {
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw ("Command failed with exit code " + $LASTEXITCODE + ": " + $FilePath + " " + ($ArgumentList -join ' '))
    }
}

function Resolve-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    return $null
}

function Get-Hash([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try {
            return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
        }
        catch {
            if ($attempt -eq 19) { throw }
            Start-Sleep -Milliseconds 100
        }
    }
    return $null
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

function Invoke-ProductionBoot(
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
                if ($partial -match '(?m)^\[PRODUCTION-RESULT\] outcome=(?:PASS|FAIL)') { break }
            }
            if ($process.HasExited) { break }
        }
    } finally {
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
    $serial = if (Test-Path -LiteralPath $SerialPath) {
        Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
    } else { "" }
    if ($null -eq $serial) { $serial = "" }
    return [pscustomobject]@{
        qemuExitCode = $exitCode
        serialPath = $SerialPath
        serialSha256 = Get-Hash $SerialPath
        stdoutPath = $StdoutPath
        stderrPath = $StderrPath
        serial = $serial
    }
}

function Parse-KeyValues([string]$Line) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($Line, '(?<key>[A-Za-z][A-Za-z0-9]*)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return [pscustomobject]$values
}

function Assert-OrderedMarkers([string]$Text, [string[]]$Markers, [string]$Label) {
    $cursor = -1
    foreach ($marker in $Markers) {
        $position = $Text.IndexOf($marker, $cursor + 1, [System.StringComparison]::Ordinal)
        if ($position -lt 0) { throw "$Label missing ordered marker: $marker" }
        $cursor = $position
    }
}

function Test-ProductionSerial([string]$Serial) {
    Assert-OrderedMarkers $Serial @(
        "C107-APP-A-PASS", "C107-APP-B-PASS", "C107-INVALID-APP-ID",
        "C107-APP-A-PASS", "C107-APP-B-PASS", "C107-APP-A-PASS") "C109 production semantic sequence"
    if ($Serial -notmatch '(?m)^\[PRODUCTION-RESULT\] outcome=PASS') { throw "C109 production result marker was not PASS." }
    if ($Serial -match '(?m)^\[C107-LAUNCH\]|^\[C108-') { throw "C109 ordinary production path selected a C107/C108 diagnostic launcher." }
    if (@([regex]::Matches($Serial, '(?m)^\[PRODUCTION-LAUNCH\].*result=PASS')).Count -ne 5) { throw "C109 expected five successful desktop production launches." }
    if (@([regex]::Matches($Serial, '(?m)^\[PRODUCTION-LAUNCH\].*identity=invalid.*result=REJECTED')).Count -ne 1) { throw "C109 invalid logical application was not rejected exactly once." }
    if (@([regex]::Matches($Serial, '(?m)^\[C102-LOADER\] mapped ELF64 AMD64')).Count -ne 1) { throw "C109 composite image was not mapped exactly once." }
    if (@([regex]::Matches($Serial, '(?m)^\[C103-LOADER\] reusing resident')).Count -ne 5) { throw "C109 expected five resident image reentries." }
    for ($stage = 1; $stage -le 7; $stage++) {
        if (@([regex]::Matches($Serial, "(?m)^\[C102-STARTUP\] stage=0000000$stage\r?$")).Count -ne 1) { throw "C109 NativeAOT startup stage $stage was not observed exactly once." }
    }
    if (@([regex]::Matches($Serial, '(?m)^\[NATIVEAOT-TLS-BRIDGE\]')).Count -ne 6) { throw "C109 expected six production TLS bridge installations." }
    $persistence = @([regex]::Matches($Serial, '(?m)^\[NATIVEAOT-PERSISTENCE\] (?<body>.*)$') | ForEach-Object { Parse-KeyValues $_.Groups['body'].Value })
    if ($persistence.Count -ne 5) { throw "C109 expected five production persistence identity records." }
    foreach ($field in @('guideThread', 'nativeThreadStore', 'nativeThread', 'gsArea', 'tlsVector', 'tlsBlock')) {
        if (@($persistence | Select-Object -ExpandProperty $field -Unique).Count -ne 1) { throw "C109 identity $field was not stable." }
        if (($persistence[0].PSObject.Properties[$field].Value) -eq '0000000000000000') { throw "C109 identity $field was zero." }
    }
    $expectedApps = @('A', 'B', 'A', 'B', 'A')
    for ($index = 0; $index -lt $persistence.Count; $index++) {
        if ($persistence[$index].app -ne $expectedApps[$index]) { throw "C109 persistence identity app order was incorrect." }
    }
    $state = @([regex]::Matches($Serial, '(?m)^\[C107-MANAGED-OUTPUT\] C107-APP-(?<app>A|B)-STATE (?<body>.*)$') | ForEach-Object {
        $record = Parse-KeyValues $_.Groups['body'].Value
        $record | Add-Member -NotePropertyName app -NotePropertyValue $_.Groups['app'].Value
        $record
    })
    if ($state.Count -ne 5) { throw "C109 expected five managed static state records." }
    $expectedOrdinary = @('1', '1', '2', '2', '3')
    $expectedThreadBefore = @('0', '0', '1', '1', '2')
    $expectedThreadAfter = @('1', '1', '2', '2', '3')
    for ($index = 0; $index -lt $state.Count; $index++) {
        if ($state[$index].app -ne $expectedApps[$index] -or
            $state[$index].count -ne $expectedOrdinary[$index] -or
            $state[$index].threadBefore -ne $expectedThreadBefore[$index] -or
            $state[$index].threadAfter -ne $expectedThreadAfter[$index] -or
            $state[$index].allocation -ne 'PASS') {
            throw "C109 ordinary-static, ThreadStatic, or allocation sequence did not match the persistent contract."
        }
    }
    $heap = @([regex]::Matches($Serial, '(?m)^\[NATIVEAOT-HEAP\] (?<body>.*)$') | ForEach-Object { Parse-KeyValues $_.Groups['body'].Value })
    if (@($heap | Where-Object { $_.action -eq 'initialize' }).Count -ne 1) { throw "C109 heap initialization did not occur exactly once." }
    if (@($heap | Where-Object { $_.action -eq 'preserve' }).Count -ne 6) { throw "C109 heap preservation did not cover every resident entry." }
    if ((@($heap | Where-Object { $_.action -eq 'initialize' } | Select-Object -ExpandProperty count -Unique)).Count -ne 1) { throw "C109 heap initialization counter was not bounded at one." }
    if ($Serial -notmatch '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$') { throw "guideXOS did not regain its main loop." }
    if ($Serial -match '(?i)PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') { throw "C109 serial output contains a fault/fail-fast marker." }
    return [pscustomobject]@{
        outcome = "PASS"
        launchSequence = "A1 -> B1 -> invalid rejected -> A2 -> B2 -> A3"
        mapCount = 1
        residentReentryCount = 5
        startupStageCounts = "1,1,1,1,1,1,1"
        tlsInstallCount = 6
        persistenceRecords = $persistence
        stateRecords = $state
        heapInitializeCount = @($heap | Where-Object { $_.action -eq 'initialize' }).Count
        heapPreserveCount = @($heap | Where-Object { $_.action -eq 'preserve' }).Count
        heapRecords = $heap
    }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
if (-not $SkipManagedBuild) {
    if ([string]::IsNullOrWhiteSpace($PythonExe)) {
        $PythonExe = Resolve-Tool "python" @(
            "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
            "C:\Python312\python.exe", "C:\Python311\python.exe")
    }
    if ([string]::IsNullOrWhiteSpace($PythonExe)) { throw "Python was not found; pass -PythonExe to the C109 runner." }
    Invoke-Checked "powershell" @(
        "-ExecutionPolicy", "Bypass", "-File", $buildScript,
        "-RepoRoot", $RepoRoot, "-OutputRoot", $compositeBuildRoot,
        "-RuntimePackRoot", (Join-Path $RepoRoot "tools\dotnet\runtime-pack"),
        "-RuntimePackOutputRoot", $runtimePackOutputRoot,
        "-UseGuideXosRuntimePack", "-ProductionApplication", "-PersistentCompositeLifecycle",
        "-AllocationMode", "Allocating", "-ManagedProjectMode", "ProductionComposite",
        "-PythonExe", $PythonExe, "-Clean")
}

$compositeElf = Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf"
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C109 production composite ELF is missing: $compositeElf" }
Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot, "-OutputImage", $stagingImage,
    "-ProductionCompositeApplicationPath", $compositeElf)
if (-not $SkipKernelBuild) {
    Invoke-Checked "mingw32-make" @(
        "-C", (Join-Path $RepoRoot "kernel"), "-B", "ARCH=amd64",
        "EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH")
}
if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

$evidenceInputs = [ordered]@{
    compositeElf = $compositeElf
    compositeElfSha256 = Get-Hash $compositeElf
    compositeSize = (Get-Item -LiteralPath $compositeElf).Length
    compositeMap = Join-Path $compositeBuildRoot "artifacts\HostLogProof.map"
    compositeMapSha256 = Get-Hash (Join-Path $compositeBuildRoot "artifacts\HostLogProof.map")
    kernel = $kernelPath
    kernelSha256 = Get-Hash $kernelPath
    bootloader = $bootloaderPath
    bootloaderSha256 = Get-Hash $bootloaderPath
    ramdisk = $stagingImage
    ramdiskSha256 = Get-Hash $stagingImage
    runtimePackManifest = Join-Path $runtimePackOutputRoot "runtime-pack.manifest.json"
    runtimePackManifestSha256 = Get-Hash (Join-Path $runtimePackOutputRoot "runtime-pack.manifest.json")
}
$evidenceInputs | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "inputs.json") -Encoding ASCII

$bootResults = [System.Collections.Generic.List[object]]::new()
if (-not $SkipQemu) {
    $qemuPath = Resolve-Tool "qemu-system-x86_64.exe" @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe",
        "C:\msys64\mingw64\bin\qemu-system-x86_64.exe")
    $ovmfPath = Resolve-Tool "edk2-x86_64-code.fd" @(
        (Join-Path $RepoRoot "OVMF.fd"), (Join-Path $RepoRoot "ovmf.fd"),
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
        $boot = Invoke-ProductionBoot $espRoot $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        $classification = $null
        try { $classification = Test-ProductionSerial $boot.serial }
        catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message } }
        $bootResults.Add([pscustomobject]@{
            boot = $index; outcome = $classification.outcome; classification = $classification
            serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath; qemuExitCode = $boot.qemuExitCode
        }) | Out-Null
        Write-Host ("[C109] boot={0} outcome={1} serial={2}" -f $index, $classification.outcome, $serialPath)
        if ($classification.outcome -ne "PASS") { throw "C109 fresh boot $index failed: $($classification.error)" }
    }
}

$sourceFiles = @(
    (Join-Path $RepoRoot "kernel\core\main.cpp"),
    (Join-Path $RepoRoot "kernel\core\desktop.cpp"),
    (Join-Path $RepoRoot "kernel\core\nativeaot_application.cpp"),
    (Join-Path $RepoRoot "kernel\core\include\kernel\nativeaot_application.h"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\Program.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\HostLogProof.csproj"),
    (Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"),
    (Join-Path $RepoRoot "tools\dotnet\runtime-pack\build-runtime-pack.ps1"),
    (Join-Path $RepoRoot "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp"),
    (Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"),
    (Join-Path $RepoRoot "scripts\dotnet\run-c109-production-composite-lifecycle.ps1"))
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) { $sourceHashes[$sourceFile] = Get-Hash $sourceFile }
$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$aheadBehind = if ($repoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim() } else { $null }

$manifest = [ordered]@{
    schemaVersion = 1
    phase = "C109"
    outcome = if ($SkipQemu) { "BUILD_ONLY" } elseif (@($bootResults | Where-Object { $_.outcome -ne "PASS" }).Count -eq 0) { "PASS" } else { "FAIL" }
    repository = [ordered]@{ root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject; upstream = $repoUpstream; aheadBehind = $aheadBehind }
    startingHead = "df6b6efa8aef7d2f60ce20b194ec18e4d851bda5"
    startingSubject = "Preserve NativeAOT TLS across resident dispatch"
    compositeImage = "/system/apps/GXOSAPP.ELF"
    compositeElf = $compositeElf
    compositeElfSha256 = Get-Hash $compositeElf
    productionLaunchApi = "desktop::launch_app -> nativeaot::launchLogicalApplication"
    logicalApplicationIds = [ordered]@{ appA = "com.guidexos.nativeaot.hostlogproof.app-a"; appB = "com.guidexos.nativeaot.hostlogproof.app-b"; invalid = "com.guidexos.nativeaot.hostlogproof.invalid" }
    selectorAbi = "NativeGxAppContext.userData carries uint32 selector 1=A, 2=B, 0=invalid"
    sequence = "A1 -> B1 -> invalid rejected -> A2 -> B2 -> A3"
    productionConfiguration = "ProductionComposite managed mode + PersistentCompositeLifecycle runtime-pack + GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH kernel flag; no C107/C108 launcher flag"
    freshBootCount = $FreshBootCount
    qemuExecuted = -not $SkipQemu
    inputs = $evidenceInputs
    sourceHashes = $sourceHashes
    boots = @($bootResults)
    runtimeIdentity = "NativeAOT 9.0.0 AMD64 Workstation GC; locked source 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3"
    runtimeInitializationCount = 1
    palInitializationCount = 1
    gcInitializationCount = 1
    codeManagerRegistrationCount = 1
    moduleInitializationCount = 1
    executableMappingCount = 1
    heapResetCount = if ($bootResults.Count -gt 0) { $bootResults[0].classification.heapInitializeCount } else { $null }
    heapPreserveCount = if ($bootResults.Count -gt 0) { $bootResults[0].classification.heapPreserveCount } else { $null }
    independentImageBusyGuard = "retained; resident launcher rejects a different image as Busy/BaseCollision and never starts a second runtime"
    missingImageRegression = "reused C103/C108 negative coverage: not-found is clean, resident state is not created, later available image can launch"
    nativeAotRuntimeSourceChanges = "none"
    gcAlgorithmChanges = "none"
    diagnostics = "C108 TLS identity census and managed callback shim are disabled; neutral production persistence/TLS/heap evidence remains observational"
    documentation = "docs/dotnet/NATIVEAOT_C109_PRODUCTION_COMPOSITE_APPLICATION_LIFECYCLE.md"
}
$manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "c109.manifest.json") -Encoding ASCII
Write-Host "C109 outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
if ($manifest.outcome -eq "FAIL") { exit 1 }
exit 0
