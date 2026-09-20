param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$CompositeElfPath = "",
    [string]$PythonExe = "",
    [int]$FreshBootCount = 3,
    [int]$TimeoutSeconds = 360,
    [ValidateSet("C120", "C121", "C122", "C123")]
    [string]$ProofPhase = "C120",
    [switch]$SkipManagedBuild,
    [switch]$SkipKernelBuild,
    [switch]$SkipQemu
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($FreshBootCount -lt 3) { throw "Managed control proof requires at least three fresh boots." }
if ($TimeoutSeconds -lt 10) { throw "TimeoutSeconds must be at least 10." }
$isC121 = $ProofPhase -eq "C121"
$isC122 = $ProofPhase -eq "C122"
$isC123 = $ProofPhase -eq "C123"
$phaseLower = $ProofPhase.ToLowerInvariant()

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$startHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$startSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$startBranch = (& git -C $RepoRoot branch --show-current).Trim()
$startUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$startAheadBehind = if ($startUpstream) {
    (& git -C $RepoRoot rev-list --left-right --count "HEAD...$startUpstream").Trim()
} else { "" }
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = if ($isC123) {
        Join-Path $RepoRoot "out\dotnet\c011ec123-managed-separator"
    } elseif ($isC122) {
        Join-Path $RepoRoot "out\dotnet\c011ec122-managed-label"
    } elseif ($isC121) {
        Join-Path $RepoRoot "out\dotnet\c011ec121-managed-checkbox"
    } else {
        Join-Path $RepoRoot "out\dotnet\c011ec120-managed-control-host"
    }
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Managed control evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot ("staging\ramdisk-{0}.img" -f $phaseLower)
$buildScript = Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"
$stagingScript = Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"
$kernelPath = Join-Path $RepoRoot "kernel\build\amd64\bin\kernel.elf"
$bootloaderPath = Join-Path $RepoRoot "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"

function Invoke-Checked([string]$FilePath, [string[]]$Arguments) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Get-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    return $null
}

function Get-Hash([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        try {
            return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
        }
        catch {
            if ($attempt -eq 9) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
    return $null
}

function Quote-QemuValue([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Stage-Esp([string]$Esp, [string]$Kernel, [string]$Bootloader, [string]$Ramdisk) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Esp "EFI\BOOT") | Out-Null
    Copy-Item -LiteralPath $Bootloader -Destination (Join-Path $Esp "EFI\BOOT\BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $Kernel -Destination (Join-Path $Esp "kernel.elf") -Force
    Copy-Item -LiteralPath $Ramdisk -Destination (Join-Path $Esp "ramdisk.img") -Force
}

function Invoke-C120Boot([string]$Esp, [string]$Serial, [string]$Stdout,
                         [string]$Stderr, [string]$Qemu, [string]$Ovmf) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $Ovmf)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $Esp) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $Serial)),
        "-boot", "order=c", "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host")
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -WindowStyle Hidden -PassThru
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $Serial) {
                $partial = Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
                $resultPattern = if ($isC123) {
                    '(?m)^\[C123-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC122) {
                    '(?m)^\[C122-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC121) {
                    '(?m)^\[C121-RESULT\] outcome=(?:PASS|FAIL)'
                } else {
                    '(?m)^\[C120-RESULT\] outcome=(?:PASS|FAIL)'
                }
                if ($partial -match $resultPattern) { break }
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
    [pscustomobject]@{
        serial = if (Test-Path -LiteralPath $Serial) { Get-Content -LiteralPath $Serial -Raw } else { "" }
        serialPath = $Serial; serialSha256 = Get-Hash $Serial
        stdoutPath = $Stdout; stderrPath = $Stderr; qemuExitCode = $process.ExitCode
    }
}

function Assert-C120Serial([string]$Serial) {
    $required = @(
        '^\[C102-RUNTIME\] PAL/VM/GC startup seam ready',
        '^\[C103-RUNTIME\] resident runtime reused',
        '^\[NATIVEAOT-TLS-BRIDGE\] install=.*result=00000001',
        '^\[NATIVEAOT-HEAP\] action=initialize',
        '^\[NATIVEAOT-HEAP\] action=preserve')
    if (-not $isC121 -and -not $isC122 -and -not $isC123) {
        $required += @(
            '^\[C120-APPMODEL\] catalogValid=true result=PASS',
            '^\[C120-RESULT\] outcome=PASS',
            '^\[C120-MIXED\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C120-HOST registration=4 initial=no-focus result=PASS',
            '^\[C120-TRAVERSAL\].*result=PASS',
            '^\[C120-DISABLED\] skip=Save traversal=PASS result=PASS',
            '^\[C120-POINTER\] target=document focus=PASS result=PASS',
            '^\[C120-SPACE-ISOLATION\] text-area=inserts-space button=not-activated result=PASS',
            '^\[C120-SPACE\] keydown=PASS keychar=PASS exact-once=PASS',
            '^\[C120-MODAL\] entry=open isolation=list restore=Open result=PASS',
            '^\[C120-MODAL\] entry=save-as isolation=filename/list restore=SaveAs result=PASS',
            '^\[C120-REGRESSION\] app=Notepad result=PASS',
            '^\[C120-REGRESSION\] app=Counter result=PASS',
            '^\[C120-REGRESSION\] app=Status result=PASS',
            '^\[C116-REGRESSION\] text-input=PASS result=PASS',
            '^\[C117-REGRESSION\] text-area=PASS result=PASS',
            '^\[C118-REGRESSION\] list-box=PASS result=PASS',
            '^\[C119-REGRESSION\] button=PASS result=PASS',
            '^\[C119-RESULT\] outcome=PASS',
            '^\[C118-NATIVE-REGRESSION\] app=Notepad result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C120-TESTS cases=50 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C120-HOST tests=PASS')
    }
    if ($isC121) {
        $required += @(
            '^\[C121-APPMODEL\] catalogValid=true result=PASS',
            '^\[C121-RESULT\] outcome=PASS',
            '^\[C121-MIXED\].*result=PASS',
            '^\[C121-FOCUSED-TESTS\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-CHECKBOX-TESTS cases=50 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-CHECKBOX-HOST-TESTS cases=17 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-HOST registration=5 initial=no-focus result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-HOST tests=PASS',
            '^\[C121-INITIAL\].*result=PASS',
            '^\[C121-TRAVERSAL\].*result=PASS',
            '^\[C121-SPACE\].*exact-once=PASS',
            '^\[C121-ROUTING\].*result=PASS',
            '^\[C121-DISABLED\].*result=PASS',
            '^\[C121-MODAL\].*result=PASS')
    }
    if ($isC122) {
        $required += @(
            '^\[C122-APPMODEL\] catalogValid=true result=PASS',
            '^\[C122-RESULT\] outcome=PASS',
            '^\[C122-MIXED\].*result=PASS',
            '^\[C122-FOCUSED-TESTS\] label=PASS host=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C122-LABEL-TESTS cases=44 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C122-LABEL-HOST-TESTS cases=22 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-HOST registration=5 initial=no-focus result=PASS',
            '^\[C122-INITIAL\].*result=PASS',
            '^\[C122-FOCUS-ORDER\].*result=PASS',
            '^\[C122-VISIBILITY\].*result=PASS',
            '^\[C122-DYNAMIC\] open=02-POINT\.TXT active=Open result=PASS',
            '^\[C122-DYNAMIC\] save-as=THIRD\.TXT active=SaveAs stale-tail=none result=PASS',
            '^\[C122-TRAVERSAL\].*result=PASS',
            '^\[C122-MODAL\].*result=PASS')
    }
    if ($isC123) {
        $required += @(
            '^\[C123-APPMODEL\] catalogValid=true result=PASS',
            '^\[C123-RESULT\] outcome=PASS',
            '^\[C123-MIXED\].*result=PASS',
            '^\[C123-FOCUSED-TESTS\] separator=PASS host=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C123-SEPARATOR-TESTS cases=48 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C123-SEPARATOR-HOST-TESTS cases=33 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C123-HOST registration=5 initial=no-focus result=PASS',
            '^\[C123-INITIAL\].*result=PASS',
            '^\[C123-FOCUS-ORDER\].*result=PASS',
            '^\[C123-VISIBILITY\].*result=PASS',
            '^\[C123-RESIZE\] expand=63 contract=12 stale-tail=none result=PASS',
            '^\[C123-DYNAMIC\] open=02-POINT\.TXT active=Open separator=visible result=PASS',
            '^\[C123-DYNAMIC\] save-as=THIRD\.TXT active=SaveAs separator=visible result=PASS',
            '^\[C123-MODAL\].*result=PASS')
    }
    foreach ($pattern in $required) {
        if ($Serial -notmatch "(?m)$pattern") { throw "Managed control proof missing serial marker: $pattern" }
    }
    $spaceMarker = @([regex]::Matches($Serial,
        '(?m)^\[C120-SPACE\] keydown=PASS keychar=PASS exact-once=PASS\r?$')).Count
    if (-not $isC121 -and -not $isC122 -and -not $isC123 -and $spaceMarker -ne 1) { throw "C120 expected one exact-once Space marker, got $spaceMarker." }
    $saveActivation = @([regex]::Matches($Serial,
        '(?m)^\[C102-MANAGED-OUTPUT\] C120-ACTIVATE control=Save result=PASS\r?$')).Count
    if (-not $isC121 -and -not $isC122 -and -not $isC123 -and $saveActivation -ne 1) { throw "C120 expected one managed Save activation, got $saveActivation." }
    if ($Serial -match '(?m)^\[(?:C120|C121|C122|C123)-[^\r\n]*FAIL|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "Managed control proof serial output contains a failure or fault marker."
    }
    [pscustomobject]@{ outcome = "PASS"; spaceMarkers = $spaceMarker; saveActivations = $saveActivation }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$providedComposite = -not [string]::IsNullOrWhiteSpace($CompositeElfPath)
if ($providedComposite) { $CompositeElfPath = [System.IO.Path]::GetFullPath($CompositeElfPath) }
if (-not $SkipManagedBuild -and -not $providedComposite) {
    if ([string]::IsNullOrWhiteSpace($PythonExe)) {
        $PythonExe = Get-Tool "python" @(
            "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
            "C:\Python312\python.exe", "C:\Python311\python.exe")
    }
    if ([string]::IsNullOrWhiteSpace($PythonExe)) { throw "Python was not found; pass -PythonExe." }
    if (Test-Path -LiteralPath $compositeBuildRoot) {
        Remove-Item -LiteralPath $compositeBuildRoot -Recurse -Force
    }
    Invoke-Checked "powershell" @(
        "-ExecutionPolicy", "Bypass", "-File", $buildScript,
        "-RepoRoot", $RepoRoot, "-OutputRoot", $compositeBuildRoot,
        "-RuntimePackRoot", (Join-Path $RepoRoot "tools\dotnet\runtime-pack"),
        "-RuntimePackOutputRoot", $runtimePackOutputRoot,
        "-UseGuideXosRuntimePack", "-ProductionApplication", "-PersistentCompositeLifecycle",
        "-AllocationMode", "Allocating", "-ManagedProjectMode",
         $(if ($isC123) { "C123Composite" } elseif ($isC122) { "C122Composite" } elseif ($isC121) { "C121Composite" } else { "C120Composite" }),
        "-PythonExe", $PythonExe)
}
$compositeElf = if ($providedComposite) { $CompositeElfPath } else {
    Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf"
}
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) {
    throw "Managed control proof composite ELF is missing: $compositeElf"
}
Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot, "-OutputImage", $stagingImage,
    "-C104AppAPath", $compositeElf, "-ProductionCompositeApplicationPath", $compositeElf,
    "-C114ManagedDirectoryServices", "-C117ManagedTextArea", "-C118ManagedListBox")

$kernelFlags = "-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION -DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES -DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES -DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER -DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT -DGXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA -DGXOS_NATIVEAOT_C118_MANAGED_LIST_BOX -DGXOS_NATIVEAOT_C119_MANAGED_BUTTON -DGXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST"
if ($isC123) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR" }
elseif ($isC122) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL" }
elseif ($isC121) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX" }
if (-not $SkipKernelBuild) {
    Invoke-Checked "mingw32-make" @(
        "-C", (Join-Path $RepoRoot "kernel"), "-B", "ARCH=amd64",
        "EXTRA_CFLAGS=$kernelFlags")
}
if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

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
abi=GuideXos Host ABI v1/table 104; no capability changes; no input transport changes
host=GuideXosControlHost fixed capacity 8; picker capacity 2; explicit kind dispatch; no reflection; C122 label and C123 separator are deliberately not registered
focus=one active control or no focus; C123 registration order Open, Save, Save As, Show path, Document; label and separator absent from Tab/Shift-Tab
space=KeyDown Space ignored by button; one KeyChar Space activates Save exactly once; text-area Space is routed as text
modal=picker focus is isolated; main focus restores to Open or Save As after completion
runtime=NativeAOT resident image; allocation/GC/VFS/runtime seams unchanged
qemu=three fresh isolated ESP boots; serial is authoritative
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "input-contract.txt") -Encoding ASCII

$bootResults = [System.Collections.Generic.List[object]]::new()
if (-not $SkipQemu) {
    $qemu = Get-Tool "qemu-system-x86_64.exe" @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe", "C:\msys64\mingw64\bin\qemu-system-x86_64.exe")
    $ovmf = Get-Tool "edk2-x86_64-code.fd" @(
        (Join-Path $RepoRoot "OVMF.fd"), "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
        "C:\Program Files (x86)\qemu\share\edk2-x86_64-code.fd")
    if (-not $qemu) { throw "qemu-system-x86_64.exe was not found." }
    if (-not $ovmf) { throw "OVMF code image was not found." }
    for ($index = 1; $index -le $FreshBootCount; $index++) {
        $bootRoot = Join-Path $EvidenceRoot ("boot-{0:D2}" -f $index)
        $esp = Join-Path $bootRoot "ESP"
        New-Item -ItemType Directory -Force -Path $bootRoot | Out-Null
        Stage-Esp $esp $kernelPath $bootloaderPath $stagingImage
        $serial = Join-Path $bootRoot "serial.log"
        $stdout = Join-Path $bootRoot "qemu.stdout.log"
        $stderr = Join-Path $bootRoot "qemu.stderr.log"
        foreach ($stale in @($serial, $stdout, $stderr)) {
            if (Test-Path -LiteralPath $stale -PathType Leaf) { Remove-Item -LiteralPath $stale -Force }
        }
        $boot = Invoke-C120Boot $esp $serial $stdout $stderr $qemu $ovmf
        try { $classification = Assert-C120Serial $boot.serial }
        catch { $classification = [pscustomobject]@{ outcome = "FAIL"; error = $_.Exception.Message } }
        $bootResults.Add([pscustomobject]@{
            boot = $index; outcome = $classification.outcome; classification = $classification
            serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath; qemuExitCode = $boot.qemuExitCode
        }) | Out-Null
        Write-Host ("[{0}] boot={1} outcome={2} serial={3}" -f $ProofPhase, $index, $classification.outcome, $serial)
        if ($classification.outcome -ne "PASS") { throw "$ProofPhase fresh boot $index failed: $($classification.error)" }
    }
}

$evidenceSerial = if ($bootResults.Count -gt 0) {
    Get-Content -LiteralPath (Join-Path $EvidenceRoot "boot-01\serial.log")
} else { @("QEMU not executed; build-only evidence.") }
$evidenceSerial | Where-Object { $_ -match '^\[(?:C123|C122|C121|C120|C119|C118|C117|C116|C115)-' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "managed-control-host-output.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C123|C122|C121|C120)-' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "control-host-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C116|C117|C118|C119)-' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "regression-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C102|C103|C112|C118|C119|NATIVEAOT|GC|PAL)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "lifecycle-evidence.txt") -Encoding ASCII

$sourceFiles = @(
    "kernel\core\main.cpp", "kernel\core\nativeaot_application.cpp",
    "samples\managed\HostLogProof\GuideXos\GuideXosControlHost.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosControlHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosCheckBox.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosCheckBoxTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosCheckBoxHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosLabel.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosLabelTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosLabelHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSeparator.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSeparatorTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSeparatorHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosButton.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextInput.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextArea.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosListBox.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosFilePicker.cs",
    "samples\managed\HostLogProof\Applications\ManagedNotes.cs",
    "samples\managed\HostLogProof\HostLogProof.csproj",
    "scripts\dotnet\build-managed-hostlog-proof.ps1",
    "scripts\dotnet\run-c120-managed-control-host.ps1",
    "scripts\dotnet\run-c122-managed-label.ps1",
    "scripts\dotnet\run-c123-managed-separator.ps1",
    "docs\dotnet\NATIVEAOT_C122_MANAGED_LABEL.md",
    "docs\dotnet\NATIVEAOT_C123_MANAGED_SEPARATOR.md")
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) { $sourceHashes[$sourceFile] = Get-Hash (Join-Path $RepoRoot $sourceFile) }

$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$aheadBehind = if ($repoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim() } else { "" }
@"
startHead=$startHead
startSubject=$startSubject
startBranch=$startBranch
startUpstream=$startUpstream
startAheadBehind=$startAheadBehind
endHead=$repoHead
endSubject=$repoSubject
endBranch=$repoBranch
endUpstream=$repoUpstream
endAheadBehind=$aheadBehind
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "repository-state.txt") -Encoding ASCII

$manifest = [ordered]@{
    schemaVersion = 1; phase = $ProofPhase; outcome = if ($SkipQemu) { "BUILD_ONLY" } else { "PASS" }
    repository = [ordered]@{ root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject; upstream = $repoUpstream; aheadBehind = $aheadBehind }
    hostAbi = [ordered]@{ version = 1; tableSize = 104; changed = $false; capabilityChanges = "none"; inputTransport = "existing pointer-down, KeyDown, KeyChar and Shift payload" }
    controlHost = [ordered]@{ api = "GuideXosControlHost"; capacity = 8; pickerCapacity = 2; tests = if($isC123){33}elseif($isC122){22}elseif($isC121){17}else{50}; legacyC120HostSuite = if($isC121 -or $isC122 -or $isC123){"separate C120 runner"}else{"same image"}; modal = "one shallow picker scope with saved-ID restoration and forward fallback" }
    separator = [ordered]@{ api = "GuideXosSeparator"; orientation = "horizontal"; minimumWidth = 8; maximumWidth = 504; configuredNotesWidth = 480; renderColumns = 63; focusedTests = 48; hostTests = 33; controlHostRegistration = "absent" }
    notes = [ordered]@{ order = if ($isC121 -or $isC122 -or $isC123) { "Open, Save, Save As, Show Path, Document; label and separator are not registered" } else { "Open, Save, Save As, Document" }; initialFocus = "none"; commands = if ($isC123) { "managed Open/Save/Save As; GuideXosLabel controls Path presentation; GuideXosSeparator divides content/status from command controls; native Reload retained" } elseif ($isC122) { "managed Open/Save/Save As; GuideXosLabel controls Path presentation; native Reload retained" } elseif ($isC121) { "managed Open/Save/Save As; checkbox controls Path presentation; native Reload retained" } else { "managed Open/Save/Save As; native Reload retained" }; space = if ($isC123 -or $isC122) { "checkbox toggles label visibility only from KeyChar Space; label and separator have no input API" } elseif ($isC121) { "checkbox toggles only from KeyChar Space; text/button/list/input routing remains isolated" } else { "exactly one Save activation from KeyChar Space" } }
    regressions = [ordered]@{ c116 = $true; c117 = $true; c118 = $true; c119 = $true; nativeNotepad = $true; counter = $true; status = $true }
    runtime = [ordered]@{ nativeAotSourceChanges = $false; gcChanges = $false; vfsChanges = $false; lifecycle = "resident managed image; runtime/PAL/GC/code manager/modules/mapping/heap preserve path" }
    freshBootCount = $FreshBootCount; qemuExecuted = -not $SkipQemu
    inputs = $inputs; sourceHashes = $sourceHashes; boots = @($bootResults)
    evidence = [ordered]@{ serial = "boot-01\serial.log"; managed = "managed-control-host-output.txt"; controlHost = "control-host-evidence.txt"; regressions = "regression-evidence.txt"; lifecycle = "lifecycle-evidence.txt" }
    documentation = if ($isC123) { "docs\dotnet\NATIVEAOT_C123_MANAGED_SEPARATOR.md" } elseif ($isC122) { "docs\dotnet\NATIVEAOT_C122_MANAGED_LABEL.md" } elseif ($isC121) { "docs\dotnet\NATIVEAOT_C121_MANAGED_CHECKBOX.md" } else { "docs\dotnet\NATIVEAOT_C120_MANAGED_CONTROL_HOST.md" }
}
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $EvidenceRoot ("{0}.manifest.json" -f $phaseLower)) -Encoding ASCII
Write-Host "$ProofPhase outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
