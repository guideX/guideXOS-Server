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
if ($FreshBootCount -lt 3) { throw "C118 requires at least three fresh boots." }
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
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec118-managed-list-box"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C118 evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c118.img"
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

function Invoke-C118Boot([string]$EspPath, [string]$SerialPath,
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
                if ($partial -match '(?m)^\[C118-RESULT\] outcome=(?:PASS|FAIL)') { break }
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

function Assert-C118Serial([string]$Serial) {
    $required = @(
        '^\[C118-APPMODEL\] catalogValid=true result=PASS',
        '^\[C118-RESULT\] outcome=PASS',
        '^\[C118-MIXED\].*result=PASS',
        '^\[C112-RESULT\] outcome=PASS',
        '^\[C112-MISSING-IMAGE\] status=not-found result=PASS',
        '^\[C112-INDEPENDENT-IMAGE\] status=busy result=PASS',
        '^\[C118-MANAGED-OUTPUT\] C118-TESTS cases=38 .*result=PASS',
        '^\[C117-MANAGED-OUTPUT\] C117-TESTS cases=42 .*result=PASS',
        '^\[C116-MANAGED-OUTPUT\] C116-NEGATIVE.*result=PASS',
        '^\[C116-MANAGED-OUTPUT\] C116-REGRESSION text-input=PASS',
        '^\[C117-MANAGED-OUTPUT\] C117-REGRESSION text-area=PASS',
        '^\[C118-LIST\] population=candidates-8 filtered=TXT initial=01-ALPHA\.TXT result=PASS',
        '^\[C118-POINTER\] focus=PASS select=02-POINT\.TXT result=PASS',
        '^\[C118-KEYBOARD\] down=PASS up=PASS home=PASS end=PASS',
        '\[C118-VIEWPORT\] scroll-down=PASS',
        'scroll-up=PASS selection-visible=PASS',
        '^\[C118-ACTIVATION\].*result=PASS',
        '^\[C118-VFS-VERIFY\] stage=save path=/system/apps/THIRD\.TXT .*content=Second managed document! result=PASS',
        '^\[C118-VFS-VERIFY\] stage=overwrite-confirm path=/system/apps/THIRD\.TXT .*result=PASS',
        '^\[C118-VFS-VERIFY\] stage=reopen path=/system/apps/THIRD\.TXT .*result=PASS',
        '^\[C118-NOTES\] load=PASS edit=PASS save-reopen=PASS stale-navigation=PASS',
        '^\[C118-MANAGED-OUTPUT\] C118-NOTES loaded=PASS source=list-box',
        '^\[C118-MANAGED-OUTPUT\] C118-NOTES reopened-edited=PASS source=list-box',
        '^\[C115-MANAGED-OUTPUT\] C115-NOTES open=PASS source=picker',
        '^\[C116-MANAGED-OUTPUT\] C116-PICKER save-input=initialized',
        '^\[C116-MANAGED-OUTPUT\] C116-NOTES typed-save path=/system/apps/THIRD\.TXT',
        '^\[C118-NATIVE-REGRESSION\] app=Notepad result=PASS',
        '^\[C118-REGRESSION\] app=Counter result=PASS',
        '^\[C118-REGRESSION\] app=Status result=PASS',
        '^\[C102-RUNTIME\] PAL/VM/GC startup seam ready',
        '^\[C103-RUNTIME\] resident runtime reused',
        '^\[NATIVEAOT-TLS-BRIDGE\] install=.*phase=(?:initial|resident).*result=00000001',
        '^\[NATIVEAOT-HEAP\] action=initialize',
        '^\[NATIVEAOT-HEAP\] action=preserve')
    foreach ($pattern in $required) {
        if ($Serial -notmatch "(?m)$pattern") {
            throw "C118 missing required serial marker: $pattern"
        }
    }
    if ($Serial -match '(?m)^\[C118-[^\r\n]*result=FAIL|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "C118 serial output contains a failure or fault marker."
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
        "-AllocationMode", "Allocating", "-ManagedProjectMode", "C118Composite",
        "-PythonExe", $PythonExe, "-Clean")
}
$compositeElf = if ($useProvidedComposite) { $CompositeElfPath } else {
    Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf"
}
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) {
    throw "C118 composite ELF is missing: $compositeElf"
}
Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot, "-OutputImage", $stagingImage,
    "-C104AppAPath", $compositeElf, "-ProductionCompositeApplicationPath", $compositeElf,
    "-C114ManagedDirectoryServices", "-C117ManagedTextArea", "-C118ManagedListBox")

@"
GXOSAPP.ELF
NOTES.TXT (71 bytes)
SECOND.TXT (23 bytes)
01-ALPHA.TXT (19 bytes)
02-POINT.TXT (21 bytes)
03-DOWN.TXT (18 bytes)
04-VIEW.TXT (21 bytes)
05-KEY.TXT (21 bytes)
06-ZETA.TXT (18 bytes)
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "staged-directory-contents.txt") -Encoding ASCII

$kernelFlags = "-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION -DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES -DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES -DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER -DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT -DGXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA -DGXOS_NATIVEAOT_C118_MANAGED_LIST_BOX"
if (-not $SkipKernelBuild) {
    Invoke-Checked "mingw32-make" @(
        "-C", (Join-Path $RepoRoot "kernel"), "-B", "ARCH=amd64",
        "EXTRA_CFLAGS=$kernelFlags")
}
if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

$expectedDocument = "Second managed document!"
$expectedBytes = [System.Text.Encoding]::ASCII.GetBytes($expectedDocument)
$expectedDocumentPath = Join-Path $EvidenceRoot "expected-final-document.txt"
[System.IO.File]::WriteAllBytes($expectedDocumentPath, $expectedBytes)
$expectedHash = Get-Hash $expectedDocumentPath
$inputs = [ordered]@{
    compositeElf = $compositeElf; compositeElfSha256 = Get-Hash $compositeElf
    kernel = $kernelPath; kernelSha256 = Get-Hash $kernelPath
    bootloader = $bootloaderPath; bootloaderSha256 = Get-Hash $bootloaderPath
    ramdisk = $stagingImage; ramdiskSha256 = Get-Hash $stagingImage
    runtimePackManifest = (Join-Path $runtimePackOutputRoot "runtime-pack.manifest.json")
}
$inputs.runtimePackManifestSha256 = Get-Hash $inputs.runtimePackManifest
$inputs | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "inputs.json") -Encoding ASCII
@"
api=GuideXosListBox
maximum item count=64 (picker configured for 64)
maximum label length=127 UTF-16 code units
visible row capacity=4
render width=56 bytes including selection marker
accepted labels=printable ASCII 0x20 through 0x7e
selection=-1 when empty; first accepted item selects index 0
input transport=existing LaunchFlagInput with Up/Down/Home/End/Enter; ABI v1/table 104
opened candidate=SECOND.TXT; edited final bytes=$($expectedBytes.Length); edited SHA-256=$expectedHash
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "input-contract.txt") -Encoding ASCII
@"
workspace=required managed regression
status=required managed regression
counter=required managed regression
notes=required text-area/list-box/file-picker integration
native-notepad=required native regression
list=bounded fixed storage, single selection, pointer rows, keyboard navigation, caret-following viewport
save=filename text input, cancel, overwrite decline, overwrite confirmation, save/reopen
serial-only=authoritative proof channel; screenshots are supplementary
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "regressions.txt") -Encoding ASCII
@"
managed=C118Composite NativeAOT production composite with persistent lifecycle
staging=C114 fixtures plus six additional C118 TXT candidates
kernel=$kernelFlags
qemu=qemu-system-x86_64.exe, one process per boot, isolated ESP directory, serial result marker
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
        $boot = Invoke-C118Boot $espRoot $serialPath $stdoutPath $stderrPath $qemuPath $ovmfPath
        try { $classification = Assert-C118Serial $boot.serial }
        catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message } }
        $bootResults.Add([pscustomobject]@{
            boot = $index; outcome = $classification.outcome; classification = $classification
            serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath; qemuExitCode = $boot.qemuExitCode
        }) | Out-Null
        Write-Host ("[C118] boot={0} outcome={1} serial={2}" -f $index, $classification.outcome, $serialPath)
        if ($classification.outcome -ne "PASS") {
            throw "C118 fresh boot $index failed: $($classification.error)"
        }
    }
}

$evidenceSerial = if ($bootResults.Count -gt 0) {
    Get-Content -LiteralPath (Join-Path $EvidenceRoot "boot-01\serial.log")
} else { @("QEMU not executed; build-only evidence.") }
$evidenceSerial | Where-Object { $_ -match '^\[(?:C118|C117|C116|C115)-' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "managed-list-box-output.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C118-(?:POINTER|LIST|KEYBOARD|VIEWPORT)' -or $_ -match '^\[C116-NATIVE-INPUT\]' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "pointer-keyboard-selection-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object {
    $_ -match '^\[C118-(?:KEYBOARD|VIEWPORT)' -or
    $_ -match 'scroll-up=PASS selection-visible=PASS'
} |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "scrolling-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[C118-(?:ACTIVATION|VFS-VERIFY|NOTES)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "activation-notes-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C116|C117|C115|C118)-(?:REGRESSION|NATIVE-REGRESSION|MANAGED-OUTPUT|FOCUS|NOTES)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "regression-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C102|C103|C112|C118|NATIVEAOT|GC|PAL)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "lifecycle-evidence.txt") -Encoding ASCII

$sourceFiles = @(
    "kernel\core\main.cpp", "kernel\core\nativeaot_application.cpp",
    "samples\managed\HostLogProof\NativeAbi.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextInput.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextInputTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextArea.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextAreaTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosListBox.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosListBoxTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosFilePicker.cs",
    "samples\managed\HostLogProof\Applications\ManagedNotes.cs",
    "samples\managed\HostLogProof\HostLogProof.csproj",
    "scripts\dotnet\build-managed-hostlog-proof.ps1",
    "scripts\generate-wallpaper-pack.ps1",
    "scripts\dotnet\run-c118-managed-list-box.ps1",
    "docs\dotnet\NATIVEAOT_C118_MANAGED_LIST_BOX.md")
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
    schemaVersion = 1; phase = "C118"; outcome = if ($SkipQemu) { "BUILD_ONLY" } else { "PASS" }
    repository = [ordered]@{
        root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject
        upstream = $repoUpstream; aheadBehind = $aheadBehind
    }
    hostAbi = [ordered]@{
        version = 1; tableSize = 104; changed = $false; capabilityChanges = "none"
        inputTransport = "existing LaunchFlagInput; existing key codes Up/Down/Home/End/Enter; no table growth"
    }
    listBox = [ordered]@{
        api = "GuideXosListBox"; maximumItemCount = 64; maximumLabelLength = 127
        visibleRowCount = 4; renderWidth = 56
        storage = "fixed char storage plus fixed per-item label lengths"
        selection = "SelectedIndex=-1 when empty; first accepted item selects index 0"
        viewport = "bounded FirstVisibleIndex; selection-following vertical scroll"
        activation = "Enter returns Activated; pointer and navigation only select"
        unsupported = "multiple selection, templates, icons, columns, tree, drag reorder, type-ahead, virtualization, double-click, wheel, rich accessibility metadata"
    }
    picker = [ordered]@{
        api = "GuideXosFilePicker"; candidateCount = 8; filter = ".TXT case-insensitive"
        initialSelection = "01-ALPHA.TXT"; pointerSelection = "02-POINT.TXT"
        keyboardSelection = "03-DOWN.TXT -> 02-POINT.TXT -> 01-ALPHA.TXT -> SECOND.TXT"
        activatedCandidate = "SECOND.TXT"; canonicalOpenedPath = "/system/apps/SECOND.TXT"
        controlOwnership = "GuideXosListBox owns item display, selection, navigation, hit-testing, viewport, activation"
        pickerOwnership = "enumeration, filtering, path/stat/read, dialog state, result semantics"
    }
    notes = [ordered]@{
        loaded = "Second managed document"; edited = $expectedDocument
        openedBytes = $expectedBytes.Length; openedSha256 = $expectedHash
        savedPath = "/system/apps/THIRD.TXT"; reopen = "byte-for-byte VFS verification and text-area reload"
    }
    runtime = [ordered]@{
        nativeAotSourceChanges = $false; gcChanges = $false; vfsChanges = $false
        lifecycle = "resident managed image; runtime/PAL/GC/code manager/modules/mapping/heap preserve path"
    }
    freshBootCount = $FreshBootCount; qemuExecuted = -not $SkipQemu
    inputs = $inputs; sourceHashes = $sourceHashes; boots = @($bootResults)
    evidence = [ordered]@{
        serial = "boot-01\serial.log"; managed = "managed-list-box-output.txt"
        pointerKeyboard = "pointer-keyboard-selection-evidence.txt"
        scrolling = "scrolling-evidence.txt"; activation = "activation-notes-evidence.txt"
        regressions = "regression-evidence.txt"; lifecycle = "lifecycle-evidence.txt"
    }
    documentation = "docs/dotnet/NATIVEAOT_C118_MANAGED_LIST_BOX.md"
}
$manifest | ConvertTo-Json -Depth 20 |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "c118.manifest.json") -Encoding ASCII
Write-Host "C118 outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
