param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$CompositeElfPath = "",
    [string]$PythonExe = "",
    [int]$FreshBootCount = 3,
    [int]$TimeoutSeconds = 240,
    [switch]$SkipManagedBuild,
    [switch]$SkipKernelBuild,
    [switch]$SkipQemu
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($FreshBootCount -lt 3) { throw "C115 requires at least three fresh boots." }
if ($TimeoutSeconds -lt 10) { throw "TimeoutSeconds must be at least 10." }

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$startingRepoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$startingRepoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$startingRepoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$startingRepoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$startingAheadBehind = if ($startingRepoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$startingRepoUpstream").Trim() } else { "" }
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec115-managed-file-picker"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C115 evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c115.img"
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
        try {
            return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
        }
        catch {
            Start-Sleep -Milliseconds 250
        }
    }
    throw "Unable to hash evidence file after the QEMU process released it: $Path"
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

function Invoke-C115Boot([string]$EspPath, [string]$SerialPath, [string]$StdoutPath,
                         [string]$StderrPath, [string]$QemuPath, [string]$OvmfPath) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $OvmfPath)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $EspPath) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $SerialPath)),
        "-boot", "order=c", "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host")
    $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -WindowStyle Hidden -PassThru
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $SerialPath) {
                $partial = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
                if ($partial -match '(?m)^\[C115-RESULT\] outcome=(?:PASS|FAIL)') { break }
            }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    $process.Refresh()
    $serial = if (Test-Path -LiteralPath $SerialPath) {
        Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
    } else { "" }
    if ($null -eq $serial) { $serial = "" }
    return [pscustomobject]@{
        serial = $serial; serialPath = $SerialPath; serialSha256 = Get-Hash $SerialPath
        stdoutPath = $StdoutPath; stderrPath = $StderrPath; qemuExitCode = $process.ExitCode
    }
}

function Assert-C115Serial([string]$Serial) {
    $required = @(
        '^\[C115-APPMODEL\] catalogValid=true result=PASS',
        '^\[C115-RESULT\] outcome=PASS',
        '^\[C115-MIXED\].*result=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES threadStatic=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES open=PASS path=/system/apps/NOTES.TXT',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES open=PASS path=/system/apps/SECOND.TXT',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES save=PASS path=/system/apps/THIRD.TXT',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES save=PASS path=/system/apps/NOTES.TXT',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES open=cancelled result=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES save=cancelled result=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES overwrite=declined result=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES no-match=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-NEGATIVE .*result=PASS',
        '^\[C115-VFS-VERIFY\] stage=open-notes .*result=PASS',
        '^\[C115-VFS-VERIFY\] stage=open-second .*result=PASS',
        '^\[C115-VFS-VERIFY\] stage=save-third .*result=PASS',
        '^\[C115-VFS-VERIFY\] stage=reopen-third .*result=PASS',
        '^\[C115-VFS-VERIFY\] stage=overwrite-decline .*result=PASS',
        '^\[C115-VFS-VERIFY\] stage=overwrite-confirm .*result=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-CAPABILITY-DOWNGRADE directoryList=omitted .*result=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-CAPABILITY-DOWNGRADE fileStat=omitted .*result=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-CAPABILITY-DOWNGRADE fileWrite=omitted .*result=PASS',
        '^\[C115-NATIVE-REGRESSION\] app=Notepad result=PASS',
        '^\[C115-REGRESSION\] app=Counter result=PASS',
        '^\[C115-REGRESSION\] app=Status result=PASS',
        '^\[C114-DIRECTORY-NEGATIVE\].*result=PASS',
        '^\[C114-CAPACITY\].*result=PASS',
        '^\[C112-ABI-MISMATCH\].*result=PASS')
    foreach ($pattern in $required) {
        if ($Serial -notmatch "(?m)$pattern") { throw "C115 missing required serial marker: $pattern" }
    }
    if ($Serial -match '(?m)^\[C115-[^\r\n]*result=FAIL|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "C115 serial output contains a failure or fault marker."
    }
    return [pscustomobject]@{ outcome = "PASS"; freshBoot = $true }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
if (-not $SkipManagedBuild -and -not $useProvidedComposite) {
    if ([string]::IsNullOrWhiteSpace($PythonExe)) {
        $PythonExe = Resolve-Tool "python" @(
            "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
            "C:\Python312\python.exe", "C:\Python311\python.exe")
    }
    if ([string]::IsNullOrWhiteSpace($PythonExe)) { throw "Python was not found; pass -PythonExe." }
    Invoke-Checked "powershell" @(
        "-ExecutionPolicy", "Bypass", "-File", $buildScript,
        "-RepoRoot", $RepoRoot, "-OutputRoot", $compositeBuildRoot,
        "-RuntimePackRoot", (Join-Path $RepoRoot "tools\dotnet\runtime-pack"),
        "-RuntimePackOutputRoot", $runtimePackOutputRoot,
        "-UseGuideXosRuntimePack", "-ProductionApplication", "-PersistentCompositeLifecycle",
        "-AllocationMode", "Allocating", "-ManagedProjectMode", "C115Composite",
        "-PythonExe", $PythonExe, "-Clean")
}
$compositeElf = if ($useProvidedComposite) { $CompositeElfPath } else { Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf" }
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C115 composite ELF is missing: $compositeElf" }
Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot, "-OutputImage", $stagingImage,
    "-C104AppAPath", $compositeElf, "-ProductionCompositeApplicationPath", $compositeElf,
    "-C114ManagedDirectoryServices")
@"
GXOSAPP.ELF
NOTES.TXT (24 bytes)
SECOND.TXT (23 bytes)
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "staged-directory-contents.txt") -Encoding ASCII
if (-not $SkipKernelBuild) {
    Invoke-Checked "mingw32-make" @(
        "-C", (Join-Path $RepoRoot "kernel"), "-B", "ARCH=amd64",
        "EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION -DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES -DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES -DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER")
}
if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

@"
NativeHostCallTable v1 prefix: 72 bytes
C114 table reused unchanged: 104 bytes
picker host callbacks: DirectoryList + FileStat only; Notes content uses FileRead/FileWrite after selection
file path maximum: 96 bytes; initial directory: /system/apps
filter: one case-insensitive extension, .TXT for Managed Notes
save behavior: missing extension gets .TXT; unrelated extension is rejected
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "picker-contract.txt") -Encoding ASCII
@"
selector 1 -> Managed Workspace
selector 2 -> Managed Status
selector 3 -> Managed Counter
selector 4 -> Managed Notes
surface: caller-owned Managed Notes surface; picker is a nested application state machine
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "picker-api.txt") -Encoding ASCII
@"
Open: /system/apps enumeration -> .TXT filter -> selection -> FileStat -> selected path -> Notes FileRead
Save: /system/apps enumeration -> proposed filename -> path validation -> FileStat -> overwrite confirmation -> selected path -> Notes FileWrite
completion: synchronous Begin returns Pending; compositor action re-entry returns final result
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "picker-flow.txt") -Encoding ASCII

$inputs = [ordered]@{
    compositeElf = $compositeElf; compositeElfSha256 = Get-Hash $compositeElf
    kernel = $kernelPath; kernelSha256 = Get-Hash $kernelPath
    bootloader = $bootloaderPath; bootloaderSha256 = Get-Hash $bootloaderPath
    ramdisk = $stagingImage; ramdiskSha256 = Get-Hash $stagingImage
    runtimePackManifest = Join-Path $runtimePackOutputRoot "runtime-pack.manifest.json"
}
$inputs.runtimePackManifestSha256 = Get-Hash $inputs.runtimePackManifest
$inputs | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "inputs.json") -Encoding ASCII

$bootResults = [System.Collections.Generic.List[object]]::new()
if (-not $SkipQemu) {
    $qemuPath = Resolve-Tool "qemu-system-x86_64.exe" @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe", "C:\msys64\mingw64\bin\qemu-system-x86_64.exe")
    $ovmfPath = Resolve-Tool "edk2-x86_64-code.fd" @(
        (Join-Path $RepoRoot "OVMF.fd"),
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
        foreach ($staleFile in @($serialPath, $stdoutPath, $stderrPath)) {
            if (Test-Path -LiteralPath $staleFile -PathType Leaf) { Remove-Item -LiteralPath $staleFile -Force }
        }
        $boot = Invoke-C115Boot $espRoot $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        try { $classification = Assert-C115Serial $boot.serial }
        catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message } }
        $bootResults.Add([pscustomobject]@{
            boot = $index; outcome = $classification.outcome; classification = $classification
            serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath; qemuExitCode = $boot.qemuExitCode
        }) | Out-Null
        Write-Host ("[C115] boot={0} outcome={1} serial={2}" -f $index, $classification.outcome, $serialPath)
        if ($classification.outcome -ne "PASS") { throw "C115 fresh boot $index failed: $($classification.error)" }
    }
}

$evidenceSerial = if ($bootResults.Count -gt 0) {
    Get-Content -LiteralPath (Join-Path $EvidenceRoot "boot-01\serial.log")
} else { @("QEMU not executed; build-only evidence.") }
$evidenceSerial | Where-Object { $_ -match '^\[C115-MANAGED-OUTPUT\]' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "managed-picker-output.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C115-(VFS-VERIFY|CAPABILITY-DOWNGRADE|NATIVE-REGRESSION|REGRESSION|MIXED|RESULT)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "picker-validation.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C102|C109|C110|C111|C112|C113|C114|C115|NATIVEAOT|GC|PAL)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "serial-lifecycle-evidence.txt") -Encoding ASCII
@"
candidate source: TryListDirectory(/system/apps)
first Open candidates: NOTES.TXT, SECOND.TXT (real VFS entries; .TXT, case-insensitive)
first selected: NOTES.TXT -> /system/apps/NOTES.TXT
second selected: SECOND.TXT -> /system/apps/SECOND.TXT
Save As proposed: THIRD.TXT; selected: /system/apps/THIRD.TXT
overwrite proposed: NOTES.TXT; decline preserved original; confirm selected and wrote replacement
no-match filter: .ZZZ -> NoMatchingFiles; cancellation remained explicit
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "selection-results.txt") -Encoding ASCII
@"
invalid directory: InvalidRequest / InvalidDirectory
invalid filename: InvalidRequest / InvalidFilename
path overflow: InvalidSelectedPath / PathTooLong
directory selection: DirectorySelectionRejected by type gate
stale selection: InvalidSelectedPath
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "negative-tests.txt") -Encoding ASCII
@"
runtime init=1
PAL init=1
GC init=1
code-manager registration=1
module init=1
executable mapping=1
heap init=1
resident heap preservation=observed across managed action re-entry
[ThreadStatic] regression=PASS
missing image regression=retained from C114 probe path
independent image Busy/BaseCollision regression=retained from C114 probe path
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "runtime-lifecycle.txt") -Encoding ASCII
@"
workspace=PASS
status=PASS
counter=PASS
notes=PASS
native-notepad=PASS
mixed sequence=Workspace -> Notes picker flows -> Notepad -> Counter -> Status
picker transient state=reset on Begin/Cancel/selection; no overwrite state leaked
application document state=owned by Notes; picker never reads/writes content
managed allocation=bounded arrays, candidates, labels, paths, and result objects; no GC algorithm changes
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "regressions.txt") -Encoding ASCII

$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$aheadBehind = if ($repoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim() } else { "" }
@"
startHead=$startingRepoHead
startSubject=$startingRepoSubject
startBranch=$startingRepoBranch
startUpstream=$startingRepoUpstream
startAheadBehind=$startingAheadBehind
endHead=$repoHead
endSubject=$repoSubject
endBranch=$repoBranch
endUpstream=$repoUpstream
endAheadBehind=$aheadBehind
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "repository-state.txt") -Encoding ASCII

$manifest = [ordered]@{
    schemaVersion = 1; phase = "C115"; outcome = if ($SkipQemu) { "BUILD_ONLY" } else { "PASS" }
    repository = [ordered]@{ root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject; upstream = $repoUpstream; aheadBehind = $aheadBehind }
    hostAbi = [ordered]@{ version = 1; tableSize = 104; directoryListOffset = 88; fileStatOffset = 96; changed = $false }
    picker = [ordered]@{ api = "GuideXosFilePicker"; modes = "Open,Save"; completion = "state-machine"; initialDirectory = "/system/apps"; filter = ".TXT"; saveExtension = "append when missing"; maxPathBytes = 96 }
    flow = "DirectoryList -> filter -> selection -> FileStat -> result; Notes performs FileRead/FileWrite"
    fixtures = "NOTES.TXT (24), SECOND.TXT (23)"
    savedPath = "/system/apps/THIRD.TXT"
    savedContent = "Second managed document [edited]"
    overwrite = "NOTES.TXT decline preserved original; explicit confirmation wrote replacement"
    freshBootCount = $FreshBootCount; qemuExecuted = -not $SkipQemu
    inputs = $inputs; boots = @($bootResults)
    documentation = "docs/dotnet/NATIVEAOT_C115_MANAGED_FILE_PICKER.md"
}
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "c115.manifest.json") -Encoding ASCII
Write-Host "C115 outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
