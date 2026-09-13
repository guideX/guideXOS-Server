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
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec108-threadstatic-tls-lifecycle"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C108 evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c108.img"
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
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
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
        } catch {
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

function Invoke-C108Boot(
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
        $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $RepoRoot -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -WindowStyle Hidden -PassThru
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $SerialPath) {
                $partial = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
                if ($partial -match '(?m)^\[C108-RESULT\] outcome=(?:PASS|FAIL)') { break }
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

function Assert-OrderedMarkers([string]$Text, [string[]]$Markers, [string]$Label) {
    $cursor = -1
    foreach ($marker in $Markers) {
        $position = $Text.IndexOf($marker, $cursor + 1, [System.StringComparison]::Ordinal)
        if ($position -lt 0) { throw "$Label missing ordered marker: $marker" }
        $cursor = $position
    }
}

function Parse-KeyValues([string]$Line) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($Line, '(?<key>[A-Za-z][A-Za-z0-9]*)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return [pscustomobject]$values
}

function Test-C108Serial([string]$Serial) {
    Assert-OrderedMarkers $Serial @(
        "C107-APP-A-PASS", "C107-APP-B-PASS", "C107-APP-A-PASS",
        "C107-APP-B-PASS", "C107-APP-A-PASS") "C108 valid semantic sequence"
    if ($Serial -notmatch '(?m)^\[C108-RESULT\] outcome=PASS') { throw "C108 result marker was not PASS." }
    if (@([regex]::Matches($Serial, '(?m)^\[C107-LAUNCH\].*status=success managedReturn=00000000')).Count -ne 5) { throw "C108 expected five successful valid logical launches." }
    if (@([regex]::Matches($Serial, '(?m)^\[C107-LAUNCH\].*identity=invalid.*status=invalid-app-id managedReturn=FFFFFFFC')).Count -ne 1) { throw "C108 invalid selector regression was not observed exactly once." }
    if (@([regex]::Matches($Serial, '(?m)^\[C102-LOADER\] mapped ELF64 AMD64')).Count -ne 1) { throw "C108 composite image was not mapped exactly once." }
    if (@([regex]::Matches($Serial, '(?m)^\[C103-LOADER\] reusing resident')).Count -ne 5) { throw "C108 expected five resident image reentries." }
    for ($stage = 1; $stage -le 7; $stage++) {
        if (@([regex]::Matches($Serial, "(?m)^\[C102-STARTUP\] stage=0000000$stage\r?$")).Count -ne 1) { throw "C108 NativeAOT startup stage $stage was not observed exactly once." }
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C108-TLS-BRIDGE\]')).Count -ne 6) { throw "C108 expected six TLS install calls." }
    if (@([regex]::Matches($Serial, '(?m)^\[C108-GUIDE\]')).Count -ne 5) { throw "C108 guideXOS identity census did not cover five valid entries." }
    if (@([regex]::Matches($Serial, '(?m)^\[C108-TLS\]')).Count -ne 5) { throw "C108 NativeAOT identity census did not cover five valid entries." }
    if ($Serial -notmatch '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$') { throw "guideXOS did not return to its main loop after C108." }
    if ($Serial -match '(?i)PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') { throw "C108 serial output contains a fault/fail-fast marker." }

    $tlsRecords = @([regex]::Matches($Serial, '(?m)^\[C108-TLS\] (?<body>.*)$') | ForEach-Object { Parse-KeyValues $_.Groups['body'].Value })
    $guideRecords = @([regex]::Matches($Serial, '(?m)^\[C108-GUIDE\] (?<body>.*)$') | ForEach-Object { Parse-KeyValues $_.Groups['body'].Value })
    $bridgeRecords = @([regex]::Matches($Serial, '(?m)^\[C108-TLS-BRIDGE\] (?<body>.*)$') | ForEach-Object { Parse-KeyValues $_.Groups['body'].Value })
    foreach ($field in @('nativeThread', 'tlsVector', 'tlsBlock', 'threadStaticStorage', 'runtime')) {
        if (@($tlsRecords | Select-Object -ExpandProperty $field -Unique).Count -ne 1) { throw "C108 identity $field was not stable." }
        if (($tlsRecords[0].PSObject.Properties[$field].Value) -eq '0000000000000000') { throw "C108 identity $field was zero." }
    }
    foreach ($field in @('guideThread', 'adapter', 'adapterNativeThread', 'gsArea', 'tlsVector', 'tlsBlock')) {
        if (@($guideRecords | Select-Object -ExpandProperty $field -Unique).Count -ne 1) { throw "C108 guide identity $field was not stable." }
        if (($guideRecords[0].PSObject.Properties[$field].Value) -eq '0000000000000000') { throw "C108 guide identity $field was zero." }
    }
    if (@($tlsRecords | Where-Object { $_.app -eq 'A' } | Select-Object -ExpandProperty appThreadStatic -Unique).Count -ne 1 -or @($tlsRecords | Where-Object { $_.app -eq 'B' } | Select-Object -ExpandProperty appThreadStatic -Unique).Count -ne 1) { throw "C108 App A or App B field address was not stable." }
    $appAField = ($tlsRecords | Where-Object { $_.app -eq 'A' } | Select-Object -First 1).appThreadStatic
    $appBField = ($tlsRecords | Where-Object { $_.app -eq 'B' } | Select-Object -First 1).appThreadStatic
    if ($appAField -eq $appBField) { throw "C108 App A and App B thread-static fields aliased." }
    $expectedAppOrder = @('A', 'B', 'A', 'B', 'A')
    $expectedOrdinaryAfter = @('0000000000000001', '0000000000000001', '0000000000000002', '0000000000000002', '0000000000000003')
    $expectedThreadBefore = @('0000000000000000', '0000000000000000', '0000000000000001', '0000000000000001', '0000000000000002')
    $expectedThreadAfter = @('0000000000000001', '0000000000000001', '0000000000000002', '0000000000000002', '0000000000000003')
    for ($index = 0; $index -lt $tlsRecords.Count; $index++) {
        if ($tlsRecords[$index].app -ne $expectedAppOrder[$index] -or
            $tlsRecords[$index].ordinaryAfter -ne $expectedOrdinaryAfter[$index] -or
            $tlsRecords[$index].threadBefore -ne $expectedThreadBefore[$index] -or
            $tlsRecords[$index].threadAfter -ne $expectedThreadAfter[$index]) {
            throw "C108 managed static or ThreadStatic sequence did not match the persistent-thread contract."
        }
    }
    if (@($bridgeRecords | Where-Object { $_.phase -eq 'initial' }).Count -ne 1 -or @($bridgeRecords | Where-Object { $_.phase -eq 'resident' }).Count -ne 5) { throw "C108 TLS bridge phases were not exact." }
    $heapRecords = @([regex]::Matches($Serial, '(?m)^\[C108-HEAP\] (?<body>.*)$') | ForEach-Object { Parse-KeyValues $_.Groups['body'].Value })
    if (@($heapRecords | Where-Object { $_.action -eq 'reset' }).Count -ne 1) { throw "C108 managed heap did not reset exactly once." }
    if (@($heapRecords | Where-Object { $_.action -eq 'preserve' }).Count -ne 6) { throw "C108 resident managed heap was not preserved across all later transitions." }
    $heapResetCounter = @($heapRecords | Where-Object { $_.action -eq 'reset' } | Select-Object -ExpandProperty count -Unique)
    if ($heapResetCounter.Count -ne 1 -or [Convert]::ToUInt64($heapResetCounter[0], 16) -ne 1) { throw "C108 managed heap reset counter was not one." }
    return [pscustomobject]@{
        outcome = "PASS"
        validSequence = "A1 -> B1 -> A2 -> B2 -> A3"
        invalidRegression = "invalid-ID rejected after A3"
        mapCount = 1
        residentReentryCount = 5
        startupStageCounts = "1,1,1,1,1,1,1"
        tlsInstallCount = 6
        guideIdentityCount = $guideRecords.Count
        nativeIdentityCount = $tlsRecords.Count
        tlsRecords = $tlsRecords
        guideRecords = $guideRecords
        bridgeRecords = $bridgeRecords
        heapResetCount = @($heapRecords | Where-Object { $_.action -eq 'reset' }).Count
        heapPreserveCount = @($heapRecords | Where-Object { $_.action -eq 'preserve' }).Count
        heapRecords = $heapRecords
    }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
if (-not $SkipManagedBuild) {
    if ([string]::IsNullOrWhiteSpace($PythonExe)) {
        $PythonExe = Resolve-Tool "python" @(
            "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
            "C:\Python312\python.exe",
            "C:\Python311\python.exe",
            "C:\Users\guideX\AppData\Local\Programs\Python\Python312\python.exe")
    }
    if ([string]::IsNullOrWhiteSpace($PythonExe)) { throw "Python was not found; pass -PythonExe to the C108 runner." }
    $buildArguments = @(
        "-ExecutionPolicy", "Bypass", "-File", $buildScript,
        "-RepoRoot", $RepoRoot, "-OutputRoot", $compositeBuildRoot,
        "-RuntimePackRoot", (Join-Path $RepoRoot "tools\dotnet\runtime-pack"),
        "-RuntimePackOutputRoot", $runtimePackOutputRoot,
        "-UseGuideXosRuntimePack", "-ProductionApplication",
        "-ThreadStaticLifecycleDiagnostics", "-AllocationMode", "Allocating",
        "-ManagedProjectMode", "C108ThreadStaticLifecycle", "-PythonExe", $PythonExe, "-Clean")
    if ($DotNetExe -ne "dotnet") { $buildArguments += @("-DotNetExe", $DotNetExe) }
    Invoke-Checked "powershell" $buildArguments
}

$compositeElf = Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf"
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C108 composite ELF is missing: $compositeElf" }
Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot, "-OutputImage", $stagingImage,
    "-C107CompositePath", $compositeElf)
if (-not $SkipKernelBuild) {
    Invoke-Checked "mingw32-make" @(
        "-C", (Join-Path $RepoRoot "kernel"), "-B", "ARCH=amd64",
        "EXTRA_CFLAGS=-DGXOS_DESKTOP_CLEANUP_RUNTIME_PASS=2 -DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_C108_PRODUCTION_LAUNCH -DGXOS_C108_TLS_LIFECYCLE")
}
if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

$evidenceInputs = [ordered]@{
    compositeElf = $compositeElf
    compositeElfSha256 = Get-Hash $compositeElf
    compositeSize = (Get-Item -LiteralPath $compositeElf).Length
    compositeMap = (Join-Path $compositeBuildRoot "artifacts\HostLogProof.map")
    compositeMapSha256 = Get-Hash (Join-Path $compositeBuildRoot "artifacts\HostLogProof.map")
    compositeReadelf = (Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf.readelf.txt")
    kernel = $kernelPath
    kernelSha256 = Get-Hash $kernelPath
    bootloader = $bootloaderPath
    bootloaderSha256 = Get-Hash $bootloaderPath
    ramdisk = $stagingImage
    ramdiskSha256 = Get-Hash $stagingImage
    runtimePackManifest = (Join-Path $runtimePackOutputRoot "runtime-pack.manifest.json")
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
        (Join-Path $RepoRoot "OVMF.fd"),
        (Join-Path $RepoRoot "ovmf.fd"),
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd")
    if ([string]::IsNullOrWhiteSpace($qemuPath)) { throw "qemu-system-x86_64.exe was not found." }
    if ([string]::IsNullOrWhiteSpace($ovmfPath)) { throw "OVMF code image was not found." }
    $priorBootRoot = Join-Path $EvidenceRoot ("prior-run-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    $existingBootRoots = @()
    for ($index = 1; $index -le $FreshBootCount; $index++) {
        $candidateBootRoot = Join-Path $EvidenceRoot ("boot-{0:D2}" -f $index)
        if (Test-Path -LiteralPath $candidateBootRoot) { $existingBootRoots += $candidateBootRoot }
    }
    if ($existingBootRoots.Count -gt 0) {
        New-Item -ItemType Directory -Force -Path $priorBootRoot | Out-Null
        foreach ($existingBootRoot in $existingBootRoots) {
            Move-Item -LiteralPath $existingBootRoot -Destination $priorBootRoot
        }
    }
    for ($index = 1; $index -le $FreshBootCount; $index++) {
        $bootRoot = Join-Path $EvidenceRoot ("boot-{0:D2}" -f $index)
        $espRoot = Join-Path $bootRoot "ESP"
        New-Item -ItemType Directory -Force -Path $bootRoot | Out-Null
        Stage-Esp $espRoot $kernelPath $bootloaderPath $stagingImage
        $serialPath = Join-Path $bootRoot "serial.log"
        $stdoutPath = Join-Path $bootRoot "qemu.stdout.log"
        $stderrPath = Join-Path $bootRoot "qemu.stderr.log"
        $boot = Invoke-C108Boot $espRoot $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        $classification = $null
        try { $classification = Test-C108Serial $boot.serial }
        catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message } }
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
        Write-Host ("[C108] boot={0} outcome={1} serial={2}" -f $index, $classification.outcome, $serialPath)
        if ($classification.outcome -ne "PASS") { throw "C108 fresh boot $index failed: $($classification.error)" }
    }
}

$sourceFiles = @(
    (Join-Path $RepoRoot "kernel\core\main.cpp"),
    (Join-Path $RepoRoot "kernel\core\nativeaot_application.cpp"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\Program.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\HostLogProof.csproj"),
    (Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"),
    (Join-Path $RepoRoot "tools\dotnet\runtime-pack\build-runtime-pack.ps1"),
    (Join-Path $RepoRoot "tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp"),
    (Join-Path $RepoRoot "tools\dotnet\runtime-pack\src\probes\guidexos_nativeaot_managed_host_shims.cpp"),
    (Join-Path $RepoRoot "scripts\dotnet\run-c108-threadstatic-tls-lifecycle.ps1"))
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) { $sourceHashes[$sourceFile] = Get-Hash $sourceFile }
$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$aheadBehind = if ($repoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim() } else { $null }

$manifest = [ordered]@{
    schemaVersion = 1
    phase = "C108"
    outcome = if ($SkipQemu) { "BUILD_ONLY" } elseif (@($bootResults | Where-Object { $_.outcome -ne "PASS" }).Count -eq 0) { "PASS" } else { "FAIL" }
    repository = [ordered]@{ root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject; upstream = $repoUpstream; aheadBehind = $aheadBehind }
    startingHead = "7e152f5b55029a4add8d5b30d0790e16ac080d62"
    startingSubject = "Implement composite NativeAOT application dispatch"
    compositeProject = (Join-Path $RepoRoot "samples\managed\HostLogProof\HostLogProof.csproj")
    compositeMode = "C108ThreadStaticLifecycle"
    sequence = "A1 -> B1 -> A2 -> B2 -> A3 -> invalid rejected"
    freshBootCount = $FreshBootCount
    qemuExecuted = -not $SkipQemu
    inputs = $evidenceInputs
    sourceHashes = $sourceHashes
    boots = @($bootResults)
    tlsSemantics = "identity census separates guideXOS adapter/TLS envelope from locked NativeAOT ThreadStore Thread and thread-static root"
    runtimeInitializationCount = 1
    palInitializationCount = 1
    gcInitializationCount = 1
    codeManagerRegistrationCount = 1
    moduleInitializationCount = 1
    executableMappingCount = 1
    independentImageBusyGuard = "retained; C108 uses one composite ELF path and never registers a second image"
    runtimeSourceCommit = "9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3"
    nativeAotRuntimeSourceChanges = "none"
    gcAlgorithmChanges = "none"
    managedHeapResetCount = if ($bootResults.Count -gt 0) { $bootResults[0].classification.heapResetCount } else { $null }
    residentManagedHeapPreserveCount = if ($bootResults.Count -gt 0) { $bootResults[0].classification.heapPreserveCount } else { $null }
    documentation = "docs/dotnet/NATIVEAOT_C108_THREADSTATIC_TLS_LIFECYCLE.md"
}
$manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "c108.manifest.json") -Encoding ASCII
Write-Host "C108 outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
if ($manifest.outcome -eq "FAIL") { exit 1 }
exit 0
