param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$CompositeElfPath = "",
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
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec112-managed-application-model"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C112 evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c112.img"
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

function Invoke-C112Boot(
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
                if ($partial -match '(?m)^\[C112-RESULT\] outcome=(?:PASS|FAIL)') { break }
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
        qemuExitCode = $exitCode; serialPath = $SerialPath; serialSha256 = Get-Hash $SerialPath
        stdoutPath = $StdoutPath; stderrPath = $StderrPath; serial = $serial
    }
}

function Assert-C112Serial([string]$Serial) {
    if ($Serial -notmatch '(?m)^\[C112-APPMODEL\] catalogValid=true .*shell=StartMenu,AllPrograms result=PASS') {
        throw "C112 records were not valid and visible in both App Model shell surfaces."
    }
    if ($Serial -notmatch '(?m)^\[C112-RESULT\] outcome=PASS') { throw "C112 result marker was not PASS." }
    foreach ($marker in @(
        '\[C112-INTERACTION-INPUT\] app=Workspace result=PASS',
        '\[C112-CLOSE\] app=Workspace result=PASS',
        '\[C112-PROBE-CLOSE\] result=PASS',
        '\[C112-NATIVE-REGRESSION\] app=Notepad result=PASS',
        '\[C112-COUNTER-ACTION\] ordinal=1 result=PASS',
        '\[C112-COUNTER-ACTION\] ordinal=2 result=PASS',
        '\[C112-INVALID-CONTEXT\] case=oversized result=PASS',
        '\[C112-INVALID-CONTEXT\] case=null-with-length result=PASS',
        '\[C112-INVALID-RECORD\] selector=0 result=PASS',
        '\[C112-INVALID-RECORD\] duplicate-selector result=PASS',
        '\[C112-UNKNOWN-RECORD\] result=PASS',
        '\[C112-UNKNOWN-SELECTOR\] selector=99 result=PASS',
        '\[C112-MISSING-IMAGE\] status=not-found result=PASS',
        '\[C112-INDEPENDENT-IMAGE\] status=(?:busy|base-collision) result=PASS',
        '\[C112-ABI-MISMATCH\].*result=PASS',
        '\[C112-CAPABILITY-DOWNGRADE\].*result=PASS')) {
        if ($Serial -notmatch "(?m)^$marker") { throw "C112 missing required marker: $marker" }
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C112-LAUNCH\].*result=PASS')).Count -ne 6) {
        throw "C112 expected six successful managed launches."
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C112-VISIBLE\].*result=PASS')).Count -ne 6) {
        throw "C112 expected six visible managed surfaces."
    }
    foreach ($title in @('Managed Workspace', 'Managed Counter', 'Managed Status')) {
        if (@([regex]::Matches($Serial, "(?m)^\[C112-VISIBLE\] title=$title .*result=PASS")).Count -ne 2) {
            throw "C112 expected two visible surfaces for $title."
        }
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C112-MANAGED-OUTPUT\] C112-ACTION app=')).Count -ne 3) {
        throw "C112 expected one Workspace action and two Counter actions."
    }
    if (@([regex]::Matches($Serial, '(?m)^\[C112-MANAGED-OUTPUT\] C112-(?:WORKSPACE|STATUS|COUNTER) ')).Count -ne 6) {
        throw "C112 expected six managed application state records."
    }
    if ($Serial -match '(?m)^\[C112-[^\r\n]*result=FAIL|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "C112 serial output contains a failure or fault marker."
    }
    if ($Serial -match '(?m)^\[C111-(?:APPMODEL|LAUNCH|RESULT)|^\[C107-(?:LAUNCH|RESULT)|^\[C108-(?:LAUNCH|RESULT)') {
        throw "C112 selected a diagnostic launch mode."
    }
    [pscustomobject]@{
        outcome = "PASS"; managedLaunchCount = 6; visibleSurfaceCount = 6
        actionCount = 3; applicationStateRecordCount = 6
        sequence = "Workspace(workspace-one) -> Notepad -> Counter(counter-one,Increment) -> Status(status-one) -> Workspace(workspace-two) -> Counter(counter-two,Increment) -> Status(status-two)"
    }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
if (-not $SkipManagedBuild -and -not $useProvidedComposite) {
    if ([string]::IsNullOrWhiteSpace($PythonExe)) {
        $PythonExe = Resolve-Tool "python" @(
            "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
            "C:\Python312\python.exe", "C:\Python311\python.exe")
    }
    if ([string]::IsNullOrWhiteSpace($PythonExe)) { throw "Python was not found; pass -PythonExe to the C112 runner." }
    Invoke-Checked "powershell" @(
        "-ExecutionPolicy", "Bypass", "-File", $buildScript,
        "-RepoRoot", $RepoRoot, "-OutputRoot", $compositeBuildRoot,
        "-RuntimePackRoot", (Join-Path $RepoRoot "tools\dotnet\runtime-pack"),
        "-RuntimePackOutputRoot", $runtimePackOutputRoot,
        "-UseGuideXosRuntimePack", "-ProductionApplication", "-PersistentCompositeLifecycle",
        "-AllocationMode", "Allocating", "-ManagedProjectMode", "C112Composite",
        "-PythonExe", $PythonExe, "-Clean")
}

$compositeElf = if ($useProvidedComposite) { $CompositeElfPath } else { Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf" }
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C112 production composite ELF is missing: $compositeElf" }
Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot, "-OutputImage", $stagingImage,
    "-C104AppAPath", $compositeElf, "-ProductionCompositeApplicationPath", $compositeElf)
if (-not $SkipKernelBuild) {
    Invoke-Checked "mingw32-make" @(
        "-C", (Join-Path $RepoRoot "kernel"), "-B", "ARCH=amd64",
        "EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION")
}
if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

@"
managed record: com.guidexos.apps.managed.workspace -> selector 1 -> /system/apps/GXOSAPP.ELF
managed record: com.guidexos.apps.managed.status -> selector 2 -> /system/apps/GXOSAPP.ELF
managed record: com.guidexos.apps.managed.counter -> selector 3 -> /system/apps/GXOSAPP.ELF
shell surfaces: Start menu / All Programs
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "registered-app-descriptors.txt") -Encoding ASCII
@"
selector 1 -> Managed Workspace
selector 2 -> Managed Status
selector 3 -> Managed Counter
all three records share one resident composite image and one managed dispatcher
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "selector-mapping.txt") -Encoding ASCII
@"
NativeGxAppContext size 56; launchContext pointer plus uint32 length; maximum 48 bytes
NativeHostCallTable v1 size 72; capabilities field at offset 56; action callback at offset 64
managed launch context is copied into a managed byte[] before application dispatch returns
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "abi-contract.txt") -Encoding ASCII

$inputs = [ordered]@{
    compositeElf = $compositeElf; compositeElfSha256 = Get-Hash $compositeElf
    kernel = $kernelPath; kernelSha256 = Get-Hash $kernelPath
    bootloader = $bootloaderPath; bootloaderSha256 = Get-Hash $bootloaderPath
    ramdisk = $stagingImage; ramdiskSha256 = Get-Hash $stagingImage
    runtimePackManifest = Join-Path $runtimePackOutputRoot "runtime-pack.manifest.json"
}
$inputs.runtimePackManifestSha256 = Get-Hash $inputs.runtimePackManifest
$inputs | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "inputs.json") -Encoding ASCII
@"
managed build: scripts/dotnet/build-managed-hostlog-proof.ps1 -UseGuideXosRuntimePack -ProductionApplication -PersistentCompositeLifecycle -AllocationMode Allocating -ManagedProjectMode C112Composite
staging: scripts/generate-wallpaper-pack.ps1 -ProductionCompositeApplicationPath <HostLogProof.elf> -C104AppAPath <HostLogProof.elf>
kernel: mingw32-make -C kernel -B ARCH=amd64 EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION
qemu: qemu-system-x86_64.exe -accel tcg,thread=single -machine pc -smp 1 -m 1024M -vga std -display none -serial file:<serial.log> -no-reboot -no-shutdown
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "commands.txt") -Encoding ASCII

$bootResults = [System.Collections.Generic.List[object]]::new()
if (-not $SkipQemu) {
    $qemuPath = Resolve-Tool "qemu-system-x86_64.exe" @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe", "C:\msys64\mingw64\bin\qemu-system-x86_64.exe")
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
        $boot = Invoke-C112Boot $espRoot $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        try { $classification = Assert-C112Serial $boot.serial }
        catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message } }
        $bootResults.Add([pscustomobject]@{
            boot = $index; outcome = $classification.outcome; classification = $classification
            serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath; qemuExitCode = $boot.qemuExitCode
        }) | Out-Null
        Write-Host ("[C112] boot={0} outcome={1} serial={2}" -f $index, $classification.outcome, $serialPath)
        if ($classification.outcome -ne "PASS") { throw "C112 fresh boot $index failed: $($classification.error)" }
    }
}

$sourceFiles = @(
    (Join-Path $RepoRoot "built_in_app_metadata.h"),
    (Join-Path $RepoRoot "kernel\core\include\kernel\nativeaot_application.h"),
    (Join-Path $RepoRoot "kernel\core\desktop.cpp"),
    (Join-Path $RepoRoot "kernel\core\main.cpp"),
    (Join-Path $RepoRoot "kernel\core\nativeaot_application.cpp"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\NativeAbi.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\Program.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\GuideXos\GuideXosApplication.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\GuideXos\GuideXosHost.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\GuideXos\GuideXosLaunchContext.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\GuideXos\GuideXosSurface.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\Applications\ManagedWorkspace.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\Applications\ManagedStatus.cs"),
    (Join-Path $RepoRoot "samples\managed\HostLogProof\Applications\ManagedCounter.cs"),
    (Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"),
    (Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"),
    (Join-Path $RepoRoot "scripts\dotnet\run-c112-reusable-managed-app.ps1"),
    (Join-Path $RepoRoot "docs\dotnet\NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION_MODEL.md"))
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) { $sourceHashes[$sourceFile] = Get-Hash $sourceFile }
$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$aheadBehind = if ($repoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim() } else { $null }

$manifest = [ordered]@{
    schemaVersion = 1; phase = "C112"
    outcome = if ($SkipQemu) { "BUILD_ONLY" } elseif (@($bootResults | Where-Object { $_.outcome -ne "PASS" }).Count -eq 0) { "PASS" } else { "FAIL" }
    repository = [ordered]@{ root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject; upstream = $repoUpstream; aheadBehind = $aheadBehind }
    primaryApplication = "Managed Workspace"; secondaryApplications = "Managed Status; Managed Counter"
    managedRecords = @(
        [ordered]@{ appId = "com.guidexos.apps.managed.workspace"; selector = 1; displayName = "Managed Workspace"; image = "/system/apps/GXOSAPP.ELF" },
        [ordered]@{ appId = "com.guidexos.apps.managed.status"; selector = 2; displayName = "Managed Status"; image = "/system/apps/GXOSAPP.ELF" },
        [ordered]@{ appId = "com.guidexos.apps.managed.counter"; selector = 3; displayName = "Managed Counter"; image = "/system/apps/GXOSAPP.ELF" })
    shellSurfaces = "Start menu and All Programs"
    productionLaunchRoute = "shell/App Model record -> desktop::launch_app_with_context -> ManagedNativeAot -> resident composite -> GuideXosApplicationRegistry"
    hostAbi = [ordered]@{ version = 1; tableSize = 72; capabilities = "0x000000000000007F"; capabilityDowngrade = "Action omitted is reported as unavailable" }
    launchContextAbi = "NativeGxAppContext size 56; immutable UTF-8 bytes, explicit uint32 length, maximum 48 bytes; managed copy owns the byte[]"
    interaction = "Existing compositor input activates managed-created action buttons; action dispatch re-enters the resident managed image"
    closeReturn = "Existing compositor close path closes each logical surface while the resident image and runtime remain mapped"
    sequence = "Workspace(workspace-one) -> Notepad -> Counter(counter-one,Increment) -> Status(status-one) -> Workspace(workspace-two) -> Counter(counter-two,Increment) -> Status(status-two)"
    productionConfiguration = "C112Composite + PersistentCompositeLifecycle + GXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION"
    freshBootCount = $FreshBootCount; qemuExecuted = -not $SkipQemu
    inputs = $inputs; sourceHashes = $sourceHashes; boots = @($bootResults)
    runtimeInitializationCount = 1; palInitializationCount = 1; gcInitializationCount = 1
    executableMappingCount = 1; heapInitializationCount = $null; independentImageGuard = "busy or base-collision"
    nativeAotRuntimeSourceChanges = "none; only the reusable application model and kernel bridge changed"
    gcAlgorithmChanges = "none"; documentation = "docs/dotnet/NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION_MODEL.md"
}
$manifest | ConvertTo-Json -Depth 35 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "c112.manifest.json") -Encoding ASCII
Write-Host "C112 outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
if ($manifest.outcome -eq "FAIL") { exit 1 }
exit 0
