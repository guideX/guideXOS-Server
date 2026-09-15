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
if ($FreshBootCount -lt 3) { throw "C117 requires at least three fresh boots." }
if ($TimeoutSeconds -lt 10) { throw "TimeoutSeconds must be at least 10." }

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$startingRepoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$startingRepoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$startingRepoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$startingRepoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$startingAheadBehind = if ($startingRepoUpstream) {
    (& git -C $RepoRoot rev-list --left-right --count "HEAD...$startingRepoUpstream").Trim()
} else { "" }
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec117-managed-text-area"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C117 evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c117.img"
$buildScript = Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"
$stagingScript = Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"
$kernelPath = Join-Path $RepoRoot "kernel\build\amd64\bin\kernel.elf"
$bootloaderPath = Join-Path $RepoRoot "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"
$useProvidedComposite = -not [string]::IsNullOrWhiteSpace($CompositeElfPath)
if ($useProvidedComposite) { $CompositeElfPath = [System.IO.Path]::GetFullPath($CompositeElfPath) }

function Invoke-Checked([string]$FilePath, [string[]]$ArgumentList) {
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw ("Command failed with exit code " + $LASTEXITCODE + ": " +
            $FilePath + " " + ($ArgumentList -join ' '))
    }
}

function Resolve-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Leaf)) {
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
        } catch {
            Start-Sleep -Milliseconds 250
        }
    }
    throw "Unable to hash evidence file: $Path"
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

function Invoke-C117Boot([string]$EspPath, [string]$SerialPath,
                         [string]$StdoutPath, [string]$StderrPath,
                         [string]$QemuPath, [string]$OvmfPath) {
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
                if ($partial -match '(?m)^\[C117-RESULT\] outcome=(?:PASS|FAIL)') { break }
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

function Assert-C117Serial([string]$Serial) {
    $required = @(
        '^\[C117-APPMODEL\] catalogValid=true result=PASS',
        '^\[C117-RESULT\] outcome=PASS',
        '^\[C117-MIXED\].*result=PASS',
        '^\[C112-RESULT\] outcome=PASS',
        '^\[C112-MISSING-IMAGE\] status=not-found result=PASS',
        '^\[C112-INDEPENDENT-IMAGE\] status=busy result=PASS',
        '^\[C117-MANAGED-OUTPUT\] C117-TESTS cases=35 .*result=PASS',
        '^\[C117-MANAGED-OUTPUT\] C117-NOTES initial=multiline .*result=PASS',
        '^\[C117-MANAGED-OUTPUT\] C117-TEXT-AREA focus=PASS source=pointer',
        '^\[C117-MANAGED-OUTPUT\] C117-TEXT-AREA edit=changed result=PASS',
        '^\[C117-MANAGED-OUTPUT\] C117-SELECTION navigation=shift result=PASS',
        '^\[C117-MANAGED-OUTPUT\] C117-VIEWPORT direction=down caret-visible=PASS',
        '^\[C117-MANAGED-OUTPUT\] C117-VIEWPORT direction=up caret-visible=PASS',
        '^\[C117-TRANSPORT\] kind=key-down key=.* shift=00000001 result=PASS',
        '^\[C117-VFS-VERIFY\] stage=save .*result=PASS',
        '^\[C117-VFS-VERIFY\] stage=save-as .*result=PASS',
        '^\[C117-VFS-VERIFY\] stage=reopen .*result=PASS',
        '^\[C117-VFS-VERIFY\] stage=reload .*result=PASS',
        '^\[C117-VFS-VERIFY\] stage=overwrite-confirm .*result=PASS',
        '^\[C117-C116-REGRESSION\] filename-focus-backspace-delete-left-right-enter=PASS',
        '^\[C117-FOCUS\] transient-picker-isolation=PASS',
        '^\[C117-NATIVE-REGRESSION\] app=Notepad result=PASS',
        '^\[C117-REGRESSION\] app=Counter result=PASS',
        '^\[C117-REGRESSION\] app=Status result=PASS',
        '^\[C116-MANAGED-OUTPUT\] C116-PICKER save-input=initialized',
        '^\[C116-MANAGED-OUTPUT\] C116-TEXT-INPUT focus=PASS source=pointer',
        '^\[C116-MANAGED-OUTPUT\] C116-TEXT-INPUT edit=changed result=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES save=PASS path=/system/apps/C117\.TXT',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES open=PASS source=picker',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES save=cancelled result=PASS',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES overwrite=declined result=PASS',
        '^\[C102-RUNTIME\] PAL/VM/GC startup seam ready',
        '^\[C103-RUNTIME\] resident runtime reused',
        '^\[NATIVEAOT-TLS-BRIDGE\] install=.*phase=(?:initial|resident).*result=00000001',
        '^\[NATIVEAOT-HEAP\] action=initialize',
        '^\[NATIVEAOT-HEAP\] action=preserve')
    foreach ($pattern in $required) {
        if ($Serial -notmatch "(?m)$pattern") {
            throw "C117 missing required serial marker: $pattern"
        }
    }
    if ($Serial -match '(?m)^\[C117-[^\r\n]*result=FAIL|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "C117 serial output contains a failure or fault marker."
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
        "-AllocationMode", "Allocating", "-ManagedProjectMode", "C117Composite",
        "-PythonExe", $PythonExe, "-Clean")
}
$compositeElf = if ($useProvidedComposite) { $CompositeElfPath } else {
    Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf"
}
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) {
    throw "C117 composite ELF is missing: $compositeElf"
}
Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot, "-OutputImage", $stagingImage,
    "-C104AppAPath", $compositeElf, "-ProductionCompositeApplicationPath", $compositeElf,
    "-C114ManagedDirectoryServices", "-C117ManagedTextArea")
@"
GXOSAPP.ELF
NOTES.TXT (71 bytes)
SECOND.TXT (23 bytes)
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "staged-directory-contents.txt") -Encoding ASCII

$kernelFlags = "-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION -DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES -DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES -DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER -DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT -DGXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA"
if (-not $SkipKernelBuild) {
    Invoke-Checked "mingw32-make" @(
        "-C", (Join-Path $RepoRoot "kernel"), "-B", "ARCH=amd64",
        "EXTRA_CFLAGS=$kernelFlags")
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

$expectedDocument = "AFirst!`n line`nSecond? ROW`nThird line`nFourth line`nFifth line`nSixth line!"
$expectedBytes = [System.Text.Encoding]::ASCII.GetBytes($expectedDocument)
$finalDocumentPath = Join-Path $EvidenceRoot "final-document.txt"
[System.IO.File]::WriteAllBytes($finalDocumentPath, $expectedBytes)
$expectedHash = Get-Hash $finalDocumentPath
@"
maximum characters: 256 UTF-16 code units
maximum lines: 32 logical lines
maximum renderable columns: 48; visible line count: 4
accepted characters: printable ASCII 0x20 through 0x7e plus LF (0x0a)
input transport: LaunchFlagInputShift=0x00800000; key value mask=0x007fffff; Host ABI remains v1/table 104
expected final bytes: $($expectedBytes.Length)
expected final SHA-256: $expectedHash
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "input-contract.txt") -Encoding ASCII
@"
workspace=required managed regression
status=required managed regression
counter=required managed regression
notes=required multiline editor integration
native-notepad=required native regression
selection=reusable GuideXosTextArea anchor/active model with Shift transport
viewport=first-visible-line follows caret; horizontal scrolling intentionally omitted
runtime=resident managed image; input re-entry preserves runtime foundations and mappings
serial-only=authoritative proof channel; screenshots are not required for acceptance
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "regressions.txt") -Encoding ASCII
@"
managed: C117Composite NativeAOT production composite with persistent lifecycle
staging: C114 directory fixtures plus C117 multiline NOTES.TXT
kernel: $kernelFlags
qemu: qemu-system-x86_64.exe, one process per boot, isolated ESP directory, serial result marker
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
        "C:\Program Files (x86)\qemu\share\edk2-x86_64-code.fd")
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
            if (Test-Path -LiteralPath $staleFile -PathType Leaf) {
                Remove-Item -LiteralPath $staleFile -Force
            }
        }
        $boot = Invoke-C117Boot $espRoot $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        try { $classification = Assert-C117Serial $boot.serial }
        catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message } }
        $bootResults.Add([pscustomobject]@{
            boot = $index; outcome = $classification.outcome; classification = $classification
            serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath; qemuExitCode = $boot.qemuExitCode
        }) | Out-Null
        Write-Host ("[C117] boot={0} outcome={1} serial={2}" -f $index, $classification.outcome, $serialPath)
        if ($classification.outcome -ne "PASS") {
            throw "C117 fresh boot $index failed: $($classification.error)"
        }
    }
}

$evidenceSerial = if ($bootResults.Count -gt 0) {
    Get-Content -LiteralPath (Join-Path $EvidenceRoot "boot-01\serial.log")
} else { @("QEMU not executed; build-only evidence.") }
$evidenceSerial | Where-Object { $_ -match '^\[C117-MANAGED-OUTPUT\]' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "managed-text-area-output.txt") -Encoding ASCII
$evidenceSerial | Where-Object {
    $_ -match '^\[C117-TRANSPORT\]' -or $_ -match '^\[C117-NATIVE-INPUT\]' -or
    $_ -match '^\[C117-MANAGED-OUTPUT\] C117-(?:TEXT-AREA|SELECTION|VIEWPORT|PICKER)'
} | Set-Content -LiteralPath (Join-Path $EvidenceRoot "input-event-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C117-MANAGED-OUTPUT\] C117-SELECTION|^\[C117-MANAGED-OUTPUT\].*Editor:' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "selection-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C117-MANAGED-OUTPUT\] C117-VIEWPORT|^\[C117-MANAGED-OUTPUT\].*\[.*\]' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "scrolling-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C117-VFS-VERIFY\]|^\[C117-MANAGED-OUTPUT\] C117-NOTES (?:saved|actual|reload)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "vfs-results.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C117-C116-REGRESSION\]|^\[C117-FOCUS\]|^\[C117-(?:NATIVE-)?REGRESSION\]|^\[C116-MANAGED-OUTPUT\]|^\[C115-MANAGED-OUTPUT\]' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "c116-regression-results.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C102|C103|C112|C113|C114|C117|NATIVEAOT|GC|PAL)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "lifecycle-counters.txt") -Encoding ASCII

$sourceFiles = @(
    "kernel\core\desktop.cpp", "kernel\core\main.cpp", "kernel\core\nativeaot_application.cpp",
    "kernel\core\include\kernel\nativeaot_application.h",
    "samples\managed\HostLogProof\NativeAbi.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosLaunchContext.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosApplication.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextInput.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextArea.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextAreaTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosFilePicker.cs",
    "samples\managed\HostLogProof\Applications\ManagedNotes.cs",
    "samples\managed\HostLogProof\HostLogProof.csproj",
    "scripts\dotnet\build-managed-hostlog-proof.ps1",
    "scripts\generate-wallpaper-pack.ps1",
    "scripts\dotnet\run-c117-managed-text-area.ps1",
    "docs\dotnet\NATIVEAOT_C117_MANAGED_TEXT_AREA.md")
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) {
    $sourceHashes[$sourceFile] = Get-Hash (Join-Path $RepoRoot $sourceFile)
}

$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$aheadBehind = if ($repoUpstream) {
    (& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim()
} else { "" }
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
    schemaVersion = 1; phase = "C117"; outcome = if ($SkipQemu) { "BUILD_ONLY" } else { "PASS" }
    repository = [ordered]@{
        root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject
        upstream = $repoUpstream; aheadBehind = $aheadBehind
    }
    hostAbi = [ordered]@{
        version = 1; tableSize = 104; changed = $false; capabilityChanges = "none"
        inputTransport = "LaunchFlagInputShift=0x00800000; key payload mask=0x007fffff; no table growth"
    }
    textArea = [ordered]@{
        api = "GuideXosTextArea"; maximumCharacters = 256; maximumLines = 32
        maximumRenderableColumns = 48; visibleLineCount = 4
        storage = "fixed char[256] LF-separated document; line count scanned deterministically"
        selection = "anchor plus active caret; normalized SelectionStart/SelectionEnd; Shift navigation"
        viewport = "bounded first visible line; caret-following vertical scroll; no horizontal scroll"
        acceptedCharacters = "ASCII printable 0x20-0x7e plus LF"
    }
    notes = [ordered]@{
        initialDocument = "First line|Second line|Third line|Fourth line|Fifth line|Sixth line"
        finalDocument = $expectedDocument; finalBytes = $expectedBytes.Length; finalSha256 = $expectedHash
        savedPath = "/system/apps/C117.TXT"; reopen = "byte-for-byte VFS read and text-area reload"
    }
    runtime = [ordered]@{
        nativeAotSourceChanges = $false; gcChanges = $false; vfsChanges = $false
        lifecycle = "resident managed image; runtime/PAL/GC/code manager/modules/mapping/heap preserve path"
    }
    freshBootCount = $FreshBootCount; qemuExecuted = -not $SkipQemu
    inputs = $inputs; sourceHashes = $sourceHashes; boots = @($bootResults)
    documentation = "docs/dotnet/NATIVEAOT_C117_MANAGED_TEXT_AREA.md"
}
$manifest | ConvertTo-Json -Depth 20 |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "c117.manifest.json") -Encoding ASCII
Write-Host "C117 outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
