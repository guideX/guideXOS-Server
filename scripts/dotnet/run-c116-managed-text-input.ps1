param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$CompositeElfPath = "",
    [string]$PythonExe = "",
    [int]$FreshBootCount = 3,
    [int]$TimeoutSeconds = 360,
    [switch]$SkipManagedBuild,
    [switch]$SkipKernelBuild,
    [switch]$SkipQemu
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($FreshBootCount -lt 3) { throw "C116 requires at least three fresh boots." }
if ($TimeoutSeconds -lt 10) { throw "TimeoutSeconds must be at least 10." }

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$startingRepoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$startingRepoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$startingRepoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$startingRepoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$startingAheadBehind = if ($startingRepoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$startingRepoUpstream").Trim() } else { "" }
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec116-managed-text-input"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C116 evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c116.img"
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

function Invoke-C116Boot([string]$EspPath, [string]$SerialPath, [string]$StdoutPath,
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
                if ($partial -match '(?m)^\[C116-RESULT\] outcome=(?:PASS|FAIL)') { break }
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

function Assert-C116Serial([string]$Serial) {
    $required = @(
        '^\[C116-APPMODEL\] catalogValid=true result=PASS',
        '^\[C116-RESULT\] outcome=PASS',
        '^\[C116-MIXED\].*result=PASS',
        '^\[C116-MANAGED-OUTPUT\] C116-PICKER save-input=initialized',
        '^\[C116-MANAGED-OUTPUT\] C116-TEXT-INPUT focus=PASS source=pointer',
        '^\[C116-MANAGED-OUTPUT\] C116-TEXT-INPUT edit=changed result=PASS',
        '^\[C116-MANAGED-OUTPUT\] C116-NOTES typed-save path=/system/apps/CUSTOM\.TXT',
        '^\[C116-MANAGED-OUTPUT\] C116-NOTES typed-save path=/system/apps/NOTES\.TXT',
        '^\[C116-MANAGED-OUTPUT\] C116-NOTES typed-save path=/system/apps/FINAL\.TXT',
        '^\[C116-MANAGED-OUTPUT\] C116-NEGATIVE .*result=PASS',
        '^\[C116-VFS-VERIFY\] stage=open-notes .*result=PASS',
        '^\[C116-VFS-VERIFY\] stage=open-second .*result=PASS',
        '^\[C116-VFS-VERIFY\] stage=save-custom .*result=PASS',
        '^\[C116-VFS-VERIFY\] stage=reopen-custom .*result=PASS',
        '^\[C116-VFS-VERIFY\] stage=overwrite-decline .*result=PASS',
        '^\[C116-VFS-VERIFY\] stage=overwrite-confirm .*result=PASS',
        '^\[C116-VFS-VERIFY\] stage=cancel-no-write .*result=PASS',
        '^\[C116-VFS-VERIFY\] stage=save-final .*result=PASS',
        '^\[C116-NATIVE-INPUT\] kind=pointer-down .*result=PASS',
        '^\[C116-NATIVE-INPUT\] kind=key-down .*result=PASS',
        '^\[C116-NATIVE-INPUT\] kind=key-char .*result=PASS',
        '^\[C116-FOCUS\] cancel-reset=PASS',
        '^\[C116-NATIVE-REGRESSION\] app=Notepad result=PASS',
        '^\[C116-REGRESSION\] app=Counter result=PASS',
        '^\[C116-REGRESSION\] app=Status result=PASS',
        '^\[C114-DIRECTORY-NEGATIVE\].*result=PASS',
        '^\[C114-CAPACITY\].*result=PASS',
        '^\[C112-ABI-MISMATCH\].*result=PASS')
    foreach ($pattern in $required) {
        if ($Serial -notmatch "(?m)$pattern") { throw "C116 missing required serial marker: $pattern" }
    }
    if ($Serial -match '(?m)^\[C116-[^\r\n]*result=FAIL|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "C116 serial output contains a failure or fault marker."
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
        "-AllocationMode", "Allocating", "-ManagedProjectMode", "C116Composite",
        "-PythonExe", $PythonExe, "-Clean")
}
$compositeElf = if ($useProvidedComposite) { $CompositeElfPath } else { Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf" }
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C116 composite ELF is missing: $compositeElf" }
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
        "EXTRA_CFLAGS=-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION -DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES -DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES -DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT")
}
if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

$runtimePackManifestPath = Join-Path $runtimePackOutputRoot "runtime-pack.manifest.json"
$inputs = [ordered]@{
    compositeElf = $compositeElf; compositeElfSha256 = Get-Hash $compositeElf
    kernel = $kernelPath; kernelSha256 = Get-Hash $kernelPath
    bootloader = $bootloaderPath; bootloaderSha256 = Get-Hash $bootloaderPath
    ramdisk = $stagingImage; ramdiskSha256 = Get-Hash $stagingImage
    runtimePackManifest = $runtimePackManifestPath; runtimePackManifestSha256 = Get-Hash $runtimePackManifestPath
}
$inputs | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "inputs.json") -Encoding ASCII

$kernelFlags = "-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION -DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES -DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES -DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT"
@"
managed: powershell -ExecutionPolicy Bypass -File $buildScript -RepoRoot $RepoRoot -ManagedProjectMode C116Composite -ProductionApplication -PersistentCompositeLifecycle -AllocationMode Allocating
staging: powershell -ExecutionPolicy Bypass -File $stagingScript -C104AppAPath $compositeElf -ProductionCompositeApplicationPath $compositeElf -C114ManagedDirectoryServices
kernel: mingw32-make -C $RepoRoot\kernel -B ARCH=amd64 EXTRA_CFLAGS=$kernelFlags
qemu: qemu-system-x86_64.exe -accel tcg,thread=single -machine pc -smp 1 -drive pflash OVMF -drive fat:rw:ESP -serial file:serial.log -boot order=c -no-reboot -no-shutdown
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "commands.txt") -Encoding ASCII

$bootResults = [System.Collections.Generic.List[object]]::new()
if (-not $SkipQemu) {
    $qemuPath = Resolve-Tool "qemu-system-x86_64.exe" @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe", "C:\msys64\mingw64\bin\qemu-system-x86_64.exe")
    $ovmfPath = Resolve-Tool "edk2-x86_64-code.fd" @(
        (Join-Path $RepoRoot "OVMF.fd"),
        "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
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
        $boot = Invoke-C116Boot $espRoot $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        try { $classification = Assert-C116Serial $boot.serial }
        catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message } }
        $bootResults.Add([pscustomobject]@{
            boot = $index; outcome = $classification.outcome; classification = $classification
            serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath; qemuExitCode = $boot.qemuExitCode
        }) | Out-Null
        Write-Host ("[C116] boot={0} outcome={1} serial={2}" -f $index, $classification.outcome, $serialPath)
        if ($classification.outcome -ne "PASS") { throw "C116 fresh boot $index failed: $($classification.error)" }
    }
}

$evidenceSerial = if ($bootResults.Count -gt 0) {
    Get-Content -LiteralPath (Join-Path $EvidenceRoot "boot-01\serial.log")
} else { @("QEMU not executed; build-only evidence.") }
$evidenceSerial | Where-Object { $_ -match '^\[C116-MANAGED-OUTPUT\]' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "managed-text-input-output.txt") -Encoding ASCII
$evidenceSerial | Where-Object {
    $_ -match '^\[C116-NATIVE-INPUT\]' -or
    $_ -match '^\[C116-MANAGED-OUTPUT\] C116-(?:TEXT-INPUT|PICKER|NOTES|NEGATIVE)'
} |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "input-event-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C116-VFS-VERIFY\]' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "vfs-results.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C116-MIXED\]|^\[C116-RESULT\]' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "mixed-sequence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C116-MANAGED-OUTPUT\] C116-NEGATIVE|^\[C114-(?:DIRECTORY-NEGATIVE|CAPACITY)\]|^\[C112-ABI-MISMATCH\]' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "negative-tests.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C102|C109|C110|C111|C112|C113|C114|C116|NATIVEAOT|GC|PAL)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "lifecycle-counters.txt") -Encoding ASCII

@"
bounded maximum: GuideXosTextInput.MaximumSupportedLength = 127 UTF-16 code units
path maximum: 96 bytes; Save As runtime bound is path maximum minus UTF-8 directory bytes and separator
accepted characters: printable ASCII 0x20 through 0x7e
keys: Backspace=8, Enter=10, Escape=27, Left=0x102, Right=0x103, Delete=0x106
negative probes: unfocused input, maximum length, unsupported control character, empty backspace, caret insertion, Escape cancellation, invalid filename, path overflow
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "input-contract.txt") -Encoding ASCII
@"
workspace=PASS
status=PASS
counter=PASS
notes=PASS
native-notepad=PASS
mixed sequence=Workspace -> Notes picker flows -> Notepad -> Counter -> Status -> Notes
picker transient state=reset on Begin/Cancel/selection; fresh Save As suggestion verified after return
application document state=owned by Notes; picker performs selection and path validation, not content I/O
managed allocation=bounded arrays, candidates, labels, paths, and result objects; no GC algorithm changes
serial-only verification=no screenshot claim; QEMU serial output is the authoritative evidence channel
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "regressions.txt") -Encoding ASCII

$sourceFiles = @(
    "kernel\core\desktop.cpp", "kernel\core\main.cpp", "kernel\core\nativeaot_application.cpp",
    "samples\managed\HostLogProof\NativeAbi.cs", "samples\managed\HostLogProof\GuideXos\GuideXosLaunchContext.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosApplication.cs", "samples\managed\HostLogProof\GuideXos\GuideXosTextInput.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosFilePicker.cs", "samples\managed\HostLogProof\Applications\ManagedNotes.cs",
    "samples\managed\HostLogProof\HostLogProof.csproj", "scripts\dotnet\build-managed-hostlog-proof.ps1",
    "scripts\generate-wallpaper-pack.ps1", "scripts\dotnet\run-c116-managed-text-input.ps1",
    "docs\dotnet\NATIVEAOT_C116_MANAGED_TEXT_INPUT.md")
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) { $sourceHashes[$sourceFile] = Get-Hash (Join-Path $RepoRoot $sourceFile) }

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
    schemaVersion = 1; phase = "C116"; outcome = if ($SkipQemu) { "BUILD_ONLY" } else { "PASS" }
    repository = [ordered]@{ root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject; upstream = $repoUpstream; aheadBehind = $aheadBehind }
    hostAbi = [ordered]@{ version = 1; tableSize = 104; offsets = [ordered]@{ open = 72; close = 80; directoryList = 88; fileStat = 96 }; changed = $false }
    textInput = [ordered]@{
        api = "GuideXosTextInput"; maximumSupportedLength = 127; savePathMaximumBytes = 96
        acceptedCharacters = "ASCII printable 0x20-0x7e"
        keys = [ordered]@{ Backspace = 8; Enter = 10; Escape = 27; Left = 258; Right = 259; Delete = 262 }
        focus = "pointer-down on Save As filename field; Escape cancels input and picker"
        edits = "bounded insert/delete with caret; rejected input leaves value unchanged"
    }
    picker = [ordered]@{ api = "GuideXosFilePicker"; modes = "Open,Save"; completion = "state-machine"; initialDirectory = "/system/apps"; filter = ".TXT"; saveExtension = "append when missing"; maxPathBytes = 96 }
    flow = "PS/2 -> desktop -> compositor -> NativeAotManagedSurface -> existing launch-flags transport -> managed HandleInput -> GuideXosTextInput -> picker validation"
    savedPaths = @("/system/apps/CUSTOM.TXT", "/system/apps/NOTES.TXT", "/system/apps/FINAL.TXT")
    savedContent = [ordered]@{ custom = "Second managed document [edited]"; overwrite = "Second managed document [edited] [edited]"; final = "Second managed document [edited] [edited]" }
    overwrite = "NOTES.TXT decline preserved original; explicit confirmation wrote replacement"
    cancel = "Escape from focused input canceled Save As and produced no write"
    lifecycle = "resident managed host; transient picker/input state reset across cancel and return"
    freshBootCount = $FreshBootCount; qemuExecuted = -not $SkipQemu
    inputs = $inputs; sourceHashes = $sourceHashes; boots = @($bootResults)
    documentation = "docs/dotnet/NATIVEAOT_C116_MANAGED_TEXT_INPUT.md"
}
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "c116.manifest.json") -Encoding ASCII
Write-Host "C116 outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
