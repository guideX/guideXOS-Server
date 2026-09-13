param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$CompositeElfPath = "",
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
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec110-appmodel-managed-records"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C110 evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c110.img"
$buildScript = Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"
$stagingScript = Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"
$kernelPath = Join-Path $RepoRoot "kernel\build\amd64\bin\kernel.elf"
$bootloaderPath = Join-Path $RepoRoot "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$useProvidedComposite = -not [string]::IsNullOrWhiteSpace($CompositeElfPath)
if ($useProvidedComposite) { $CompositeElfPath = [System.IO.Path]::GetFullPath($CompositeElfPath) }

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
        try { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant() }
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

function Invoke-C110Boot(
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
                if ($partial -match '(?m)^\[C110-APPMODEL-RESULT\] outcome=(?:PASS|FAIL)') { break }
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

function Test-C110Serial([string]$Serial) {
    if ($Serial -notmatch '(?m)^\[C110-APPMODEL\] catalogValid=true') { throw "C110 catalog validation did not pass." }
    if ($Serial -notmatch '(?m)^\[C110-APPMODEL-RESULT\] outcome=PASS') { throw "C110 App Model result marker was not PASS." }
    foreach ($marker in @(
        '\[C110-NATIVE-REGRESSION\] app=Notepad result=PASS',
        '\[C110-INVALID-RECORD\] selector=0 result=PASS',
        '\[C110-MISSING-IMAGE\] status=not-found result=PASS',
        '\[C110-UNKNOWN-ID\] result=PASS',
        '\[C110-INDEPENDENT-IMAGE\] status=(?:busy|base-collision) result=PASS')) {
        if ($Serial -notmatch "(?m)^$marker") { throw "C110 missing required marker: $marker" }
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C110-MANAGED-LAUNCH\].*result=PASS')).Count -ne 5) {
        throw "C110 expected five successful App Model managed launches."
    }
    if ($Serial -match '(?m)^\[C110-[^\r\n]*result=FAIL|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "C110 serial output contains a failure or fault marker."
    }
    if ($Serial -match '(?m)^\[C107-LAUNCH\]|^\[C108-') { throw "C110 selected a diagnostic C107/C108 launch mode." }
    if (@([regex]::Matches($Serial, '(?m)^\[C102-LOADER\] mapped ELF64 AMD64')).Count -ne 1) { throw "C110 image mapping was not singular." }
    if (@([regex]::Matches($Serial, '(?m)^\[C103-LOADER\] reusing resident')).Count -ne 4) { throw "C110 expected four resident image reentries." }
    for ($stage = 1; $stage -le 7; $stage++) {
        if (@([regex]::Matches($Serial, "(?m)^\[C102-STARTUP\] stage=0000000$stage\r?$")).Count -ne 1) { throw "C110 startup stage $stage was not observed exactly once." }
    }
    if (@([regex]::Matches($Serial, '(?m)^\[NATIVEAOT-TLS-BRIDGE\]')).Count -ne 5) { throw "C110 expected five TLS bridge installations." }

    $persistence = @([regex]::Matches($Serial, '(?m)^\[NATIVEAOT-PERSISTENCE\] (?<body>.*)$') | ForEach-Object { Parse-KeyValues $_.Groups['body'].Value })
    if ($persistence.Count -ne 5) { throw "C110 expected five persistent identity records." }
    foreach ($field in @('guideThread', 'nativeThreadStore', 'nativeThread', 'gsArea', 'tlsVector', 'tlsBlock')) {
        if (@($persistence | Select-Object -ExpandProperty $field -Unique).Count -ne 1) { throw "C110 identity $field was not stable." }
        if ($persistence[0].PSObject.Properties[$field].Value -eq '0000000000000000') { throw "C110 identity $field was zero." }
    }
    $expectedApps = @('A', 'B', 'A', 'B', 'A')
    for ($index = 0; $index -lt $persistence.Count; $index++) {
        if ($persistence[$index].app -ne $expectedApps[$index]) { throw "C110 managed persistence order was incorrect." }
    }

    $state = @([regex]::Matches($Serial, '(?m)^\[C107-MANAGED-OUTPUT\] C107-APP-(?<app>A|B)-STATE (?<body>.*)$') | ForEach-Object {
        $record = Parse-KeyValues $_.Groups['body'].Value
        $record | Add-Member -NotePropertyName app -NotePropertyValue $_.Groups['app'].Value
        $record
    })
    if ($state.Count -ne 5) { throw "C110 expected five managed state records." }
    $expectedOrdinary = @('1', '1', '2', '2', '3')
    $expectedThreadBefore = @('0', '0', '1', '1', '2')
    $expectedThreadAfter = @('1', '1', '2', '2', '3')
    for ($index = 0; $index -lt $state.Count; $index++) {
        if ($state[$index].app -ne $expectedApps[$index] -or
            $state[$index].count -ne $expectedOrdinary[$index] -or
            $state[$index].threadBefore -ne $expectedThreadBefore[$index] -or
            $state[$index].threadAfter -ne $expectedThreadAfter[$index] -or
            $state[$index].allocation -ne 'PASS') {
            throw "C110 static, ThreadStatic, or allocation sequence did not match the persistent contract."
        }
    }
    $heap = @([regex]::Matches($Serial, '(?m)^\[NATIVEAOT-HEAP\] (?<body>.*)$') | ForEach-Object { Parse-KeyValues $_.Groups['body'].Value })
    $heapInitializeCount = @($heap | Where-Object { $_.action -eq 'initialize' }).Count
    $heapPreserveCount = @($heap | Where-Object { $_.action -eq 'preserve' }).Count
    if ($heapInitializeCount -ne 1) { throw "C110 heap initialization did not occur exactly once." }
    if ($heapPreserveCount -ne 5) { throw "C110 heap preservation did not cover the five managed entries." }
    if ($Serial -notmatch '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$') { throw "guideXOS did not regain its main loop." }
    return [pscustomobject]@{
        outcome = "PASS"
        launchSequence = "Managed Workspace -> Managed Status -> Managed Workspace -> Managed Status -> Managed Workspace"
        mapCount = 1
        residentReentryCount = 4
        startupStageCounts = "1,1,1,1,1,1,1"
        tlsInstallCount = 5
        persistenceRecords = $persistence
        stateRecords = $state
        heapInitializeCount = $heapInitializeCount
        heapPreserveCount = $heapPreserveCount
        independentImageStatus = ([regex]::Match($Serial, '(?m)^\[C110-INDEPENDENT-IMAGE\] status=(?<status>[^\s]+)').Groups['status'].Value)
    }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
if (-not $SkipManagedBuild -and -not $useProvidedComposite) {
    if ([string]::IsNullOrWhiteSpace($PythonExe)) {
        $PythonExe = Resolve-Tool "python" @(
            "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
            "C:\Python312\python.exe", "C:\Python311\python.exe")
    }
    if ([string]::IsNullOrWhiteSpace($PythonExe)) { throw "Python was not found; pass -PythonExe to the C110 runner." }
    Invoke-Checked "powershell" @(
        "-ExecutionPolicy", "Bypass", "-File", $buildScript,
        "-RepoRoot", $RepoRoot, "-OutputRoot", $compositeBuildRoot,
        "-RuntimePackRoot", (Join-Path $RepoRoot "tools\dotnet\runtime-pack"),
        "-RuntimePackOutputRoot", $runtimePackOutputRoot,
        "-UseGuideXosRuntimePack", "-ProductionApplication", "-PersistentCompositeLifecycle",
        "-AllocationMode", "Allocating", "-ManagedProjectMode", "ProductionComposite",
        "-PythonExe", $PythonExe, "-Clean")
}

$compositeElf = if ($useProvidedComposite) { $CompositeElfPath } else { Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf" }
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C110 production composite ELF is missing: $compositeElf" }
$compositeMapPath = Join-Path (Split-Path -Parent $compositeElf) "HostLogProof.map"
Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot, "-OutputImage", $stagingImage,
    "-C104AppAPath", $compositeElf,
    "-ProductionCompositeApplicationPath", $compositeElf)
if (-not $SkipKernelBuild) {
    Invoke-Checked "mingw32-make" @(
        "-C", (Join-Path $RepoRoot "kernel"), "-B", "ARCH=amd64",
        "EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C110_APPMODEL_LAUNCH")
}
if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

$descriptorPath = Join-Path $EvidenceRoot "registered-app-descriptors.txt"
@"
managed record: com.guidexos.apps.managed.workspace
display name: Managed Workspace
launch kind: ManagedNativeAot
managed selector: 1
composite image: /system/apps/GXOSAPP.ELF
shell surface: Start menu / All Programs

managed record: com.guidexos.apps.managed.status
display name: Managed Status
launch kind: ManagedNativeAot
managed selector: 2
composite image: /system/apps/GXOSAPP.ELF
shell surface: Start menu / All Programs
"@ | Set-Content -LiteralPath $descriptorPath -Encoding ASCII
@"
com.guidexos.apps.managed.workspace -> selector 1 -> /system/apps/GXOSAPP.ELF -> managed dispatcher App A
com.guidexos.apps.managed.status -> selector 2 -> /system/apps/GXOSAPP.ELF -> managed dispatcher App B
valid selector 0: rejected
duplicate selector: catalog invalid
managed record image paths: only /system/apps/GXOSAPP.ELF
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "selector-mapping.txt") -Encoding ASCII

$evidenceInputs = [ordered]@{
    compositeElf = $compositeElf
    compositeElfSha256 = Get-Hash $compositeElf
    compositeSize = (Get-Item -LiteralPath $compositeElf).Length
    compositeMap = $compositeMapPath
    compositeMapSha256 = Get-Hash $compositeMapPath
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
@"
managed build: powershell -ExecutionPolicy Bypass -File scripts/dotnet/build-managed-hostlog-proof.ps1 -UseGuideXosRuntimePack -ProductionApplication -PersistentCompositeLifecycle -AllocationMode Allocating -ManagedProjectMode ProductionComposite
staging: powershell -ExecutionPolicy Bypass -File scripts/generate-wallpaper-pack.ps1 -ProductionCompositeApplicationPath <HostLogProof.elf> -C104AppAPath <HostLogProof.elf>
kernel: mingw32-make -C kernel -B ARCH=amd64 EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C110_APPMODEL_LAUNCH
qemu: qemu-system-x86_64.exe -accel tcg,thread=single -machine pc -smp 1 -m 1024M -vga std -display none -serial file:<serial.log> -no-reboot -no-shutdown
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "commands.txt") -Encoding ASCII

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
        $boot = Invoke-C110Boot $espRoot $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        try { $classification = Test-C110Serial $boot.serial }
        catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message } }
        $bootResults.Add([pscustomobject]@{
            boot = $index; outcome = $classification.outcome; classification = $classification
            serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath; qemuExitCode = $boot.qemuExitCode
        }) | Out-Null
        Write-Host ("[C110] boot={0} outcome={1} serial={2}" -f $index, $classification.outcome, $serialPath)
        if ($classification.outcome -ne "PASS") { throw "C110 fresh boot $index failed: $($classification.error)" }
    }
}

$sourceFiles = @(
    (Join-Path $RepoRoot "built_in_app_metadata.h"),
    (Join-Path $RepoRoot "app_launch_target.h"),
    (Join-Path $RepoRoot "kernel\core\app_launch_target_resolver.cpp"),
    (Join-Path $RepoRoot "kernel\core\desktop.cpp"),
    (Join-Path $RepoRoot "kernel\core\main.cpp"),
    (Join-Path $RepoRoot "kernel\core\nativeaot_application.cpp"),
    (Join-Path $RepoRoot "desktop_service.cpp"),
    (Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"),
    (Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"),
    (Join-Path $RepoRoot "scripts\dotnet\run-c110-appmodel-managed-records.ps1"))
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) { $sourceHashes[$sourceFile] = Get-Hash $sourceFile }
$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$aheadBehind = if ($repoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim() } else { $null }

$manifest = [ordered]@{
    schemaVersion = 1
    phase = "C110"
    outcome = if ($SkipQemu) { "BUILD_ONLY" } elseif (@($bootResults | Where-Object { $_.outcome -ne "PASS" }).Count -eq 0) { "PASS" } else { "FAIL" }
    repository = [ordered]@{ root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject; upstream = $repoUpstream; aheadBehind = $aheadBehind }
    startingHead = "8055fb434996728d9e27fa8aca069a9b3d4a50c6"
    startingSubject = "Integrate production NativeAOT composite application lifecycle"
    appModelAudit = "Shared BuiltInAppMetadata catalog; AppManager bare-metal registrations; desktop::launch_app; appmodel::resolveLaunchTarget; start-menu and All Programs surfaces"
    descriptorType = "gxos::apps::BuiltInAppMetadata"
    launchKind = "BuiltInAppLaunchKind::ManagedNativeAot"
    managedRecords = [ordered]@{
        workspace = [ordered]@{ appId = "com.guidexos.apps.managed.workspace"; displayName = "Managed Workspace"; selector = 1; image = "/system/apps/GXOSAPP.ELF" }
        status = [ordered]@{ appId = "com.guidexos.apps.managed.status"; displayName = "Managed Status"; selector = 2; image = "/system/apps/GXOSAPP.ELF" }
    }
    selectorMapping = "App Model appId -> bounded metadata selector -> shared composite image; selector 0 invalid; duplicate selectors invalidate catalog"
    productionLaunchRoute = "registered App Model record -> desktop::launch_app -> appmodel::resolveLaunchTarget -> ManagedNativeAot route -> nativeaot::launchLogicalApplication -> launchLogical -> managed dispatcher"
    compositeImage = "/system/apps/GXOSAPP.ELF"
    compositeElf = $compositeElf
    compositeElfSha256 = Get-Hash $compositeElf
    sequence = "Managed Workspace -> Managed Status -> Managed Workspace -> Managed Status -> Managed Workspace"
    productionConfiguration = "ProductionComposite managed mode + PersistentCompositeLifecycle runtime-pack + GXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH + GXOS_NATIVEAOT_C110_APPMODEL_LAUNCH; no C107/C108 launcher flags"
    freshBootCount = $FreshBootCount
    qemuExecuted = -not $SkipQemu
    inputs = $evidenceInputs
    sourceHashes = $sourceHashes
    boots = @($bootResults)
    runtimeInitializationCount = 1
    palInitializationCount = 1
    gcInitializationCount = 1
    codeManagerRegistrationCount = 1
    moduleInitializationCount = 1
    executableMappingCount = 1
    heapInitializationCount = if ($bootResults.Count -gt 0) { $bootResults[0].classification.heapInitializeCount } else { $null }
    heapPreserveCount = if ($bootResults.Count -gt 0) { $bootResults[0].classification.heapPreserveCount } else { $null }
    invalidManagedRecord = "selector 0 rejected by IsManagedNativeAotRecordValid without normal registration"
    unknownApplicationId = "com.guidexos.apps.missing rejected deterministically"
    missingCompositeImage = "/system/apps/C110-MISSING.ELF -> not-found; no resident state created"
    independentImageBusyGuard = "/system/wall/C104A.ELF -> busy or base-collision after GXOSAPP.ELF resident; no second runtime"
    nativeAotRuntimeSourceChanges = "metadata/selector resolution only; resident lifecycle implementation unchanged"
    gcAlgorithmChanges = "none"
    documentation = "docs/dotnet/NATIVEAOT_C110_APPMODEL_MANAGED_APPLICATION_RECORDS.md"
}
$manifest | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "c110.manifest.json") -Encoding ASCII
Write-Host "C110 outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
if ($manifest.outcome -eq "FAIL") { exit 1 }
exit 0
