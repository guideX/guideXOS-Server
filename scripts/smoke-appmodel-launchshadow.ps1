param(
    [switch]$Build,
    [int]$TimeoutSeconds = 35
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$serialLog = Join-Path $LogDir "appmodel-launchshadow-kernel-smoke-$stamp.serial.log"
$evidencePath = Join-Path $LogDir "appmodel-typed-dispatch-gate-qemu.evidence.txt"

function Write-AppModelLaunchShadowEvidence {
    param(
        [string]$Status,
        [bool]$RuntimeLaunchBehaviorUnchanged,
        [bool]$ImgViewerExpectedUnsupportedConfirmed,
        [bool]$FakeOnlyUnexpectedMismatchConfirmed,
        [bool]$FolderFileOpenShadowOnlyConfirmed,
        [bool]$TextFileOpenShadowOnlyConfirmed,
        [bool]$DesktopShortcutTextFileShadowOnlyConfirmed,
        [bool]$DesktopFilesystemTextFileShadowOnlyConfirmed,
        [bool]$RealBranchDesktopShortcutTextFileConfirmed,
        [bool]$RealBranchDesktopFilesystemTextFileConfirmed,
        [bool]$RealBranchDesktopStateRestored,
        [bool]$PersistentDesktopStorageWritesAbsent,
        [int]$UnexpectedMismatchRows,
        [string]$SerialLogPath
    )

    $runtimeChanged = if ($RuntimeLaunchBehaviorUnchanged) { "false" } else { "true" }
    $imgViewerConfirmed = if ($ImgViewerExpectedUnsupportedConfirmed) { "true" } else { "false" }
    $fakeOnlyConfirmed = if ($FakeOnlyUnexpectedMismatchConfirmed) { "true" } else { "false" }
    $folderFileOpenConfirmed = if ($FolderFileOpenShadowOnlyConfirmed) { "true" } else { "false" }
    $textFileOpenConfirmed = if ($TextFileOpenShadowOnlyConfirmed) { "true" } else { "false" }
    $desktopShortcutTextFileConfirmed = if ($DesktopShortcutTextFileShadowOnlyConfirmed) { "true" } else { "false" }
    $desktopFilesystemTextFileConfirmed = if ($DesktopFilesystemTextFileShadowOnlyConfirmed) { "true" } else { "false" }
    $realBranchDesktopShortcutTextFileConfirmed = if ($RealBranchDesktopShortcutTextFileConfirmed) { "true" } else { "false" }
    $realBranchDesktopFilesystemTextFileConfirmed = if ($RealBranchDesktopFilesystemTextFileConfirmed) { "true" } else { "false" }
    $realBranchDesktopStateRestoredFlag = if ($RealBranchDesktopStateRestored) { "true" } else { "false" }
    $persistentDesktopStorageWrites = if ($PersistentDesktopStorageWritesAbsent) { "false" } else { "true" }

    $lines = @(
        "[AppModelTypedDispatchGateEvidence]",
        "evidenceVersion=1",
        "kind=qemuLaunchShadowSmoke",
        "command=desktop.smoke.launchshadow",
        "timestampUnixMs=$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())",
        "timestampUtc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "qemuSmokeStatus=$Status",
        "runtimeLaunchBehaviorChanged=$runtimeChanged",
        "imgViewerExpectedUnsupportedConfirmed=$imgViewerConfirmed",
        "fakeLaunchShadowAppOnlyUnexpectedMismatchConfirmed=$fakeOnlyConfirmed",
        "folderFileOpenShadowOnlyConfirmed=$folderFileOpenConfirmed",
        "textFileOpenShadowOnlyConfirmed=$textFileOpenConfirmed",
        "desktopShortcutTextFileShadowOnlyConfirmed=$desktopShortcutTextFileConfirmed",
        "desktopFilesystemTextFileShadowOnlyConfirmed=$desktopFilesystemTextFileConfirmed",
        "realBranchDesktopShortcutTextFileConfirmed=$realBranchDesktopShortcutTextFileConfirmed",
        "realBranchDesktopFilesystemTextFileConfirmed=$realBranchDesktopFilesystemTextFileConfirmed",
        "realBranchDesktopStateRestored=$realBranchDesktopStateRestoredFlag",
        "persistentDesktopStorageWrites=$persistentDesktopStorageWrites",
        "unexpectedMismatchRows=$UnexpectedMismatchRows",
        "serialLogPath=$SerialLogPath",
        "nonFatal=true",
        "launchesApps=false"
    )
    Set-Content -Path $script:evidencePath -Value $lines -Encoding ASCII
}

function Invoke-KernelBuildForSmoke {
    param([string]$ExtraCFlags)

    $oldExtra = $env:EXTRA_CFLAGS
    if ($ExtraCFlags) {
        $env:EXTRA_CFLAGS = $ExtraCFlags
    } else {
        Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
    }

    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "main.o" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "app_launch_target_resolver.o" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem -Path (Join-Path $Root "kernel\build") -Recurse -Filter "desktop.o" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    & (Join-Path $Root "build-kernel.bat")
    $buildCode = $LASTEXITCODE

    if ($null -ne $oldExtra) {
        $env:EXTRA_CFLAGS = $oldExtra
    } else {
        Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
    }

    if ($buildCode -ne 0) { exit $buildCode }
}

$activeSmokeBuild = $false
function Restore-NormalKernelBuild {
    if ($script:activeSmokeBuild) {
        Write-Host "Restoring normal kernel build without app-model launch shadow smoke diagnostics..."
        Invoke-KernelBuildForSmoke ""
        $script:activeSmokeBuild = $false
    }
}

function Find-Qemu {
    $qemu = Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue
    if ($qemu) { return $qemu.Source }
    foreach ($candidate in @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "D:\qemu\qemu-system-x86_64.exe"
    )) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

if ($Build) {
    Invoke-KernelBuildForSmoke ""
}

$qemu = Find-Qemu
if (-not $qemu) { throw "qemu-system-x86_64 not found." }

Write-Host "Building kernel with active app-model launch shadow smoke diagnostics..."
Invoke-KernelBuildForSmoke "-DGXOS_APPMODEL_LAUNCHSHADOW_SMOKE_ACTIVE -DGXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY"
$activeSmokeBuild = $true

$ovmf = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
if (-not (Test-Path $ovmf)) { throw "OVMF image not found: $ovmf" }

$esp = Join-Path $Root "ESP"
$bootloader = Join-Path $esp "EFI\BOOT\BOOTX64.EFI"
if (-not (Test-Path $bootloader)) {
    throw "ESP/EFI/BOOT/BOOTX64.EFI not found. Run .\build.ps1 -Arch amd64 first or pass -Build."
}

$fat32Disk = Join-Path $Root "disks\test-fat32.img"
if (-not (Test-Path $fat32Disk)) {
    throw "FAT32 test disk not found: $fat32Disk. Run .\scripts\create-test-disks.ps1 first."
}

$startup = Join-Path $esp "startup.nsh"
$createdStartup = $false
if (-not (Test-Path $startup)) {
    "FS0:\EFI\BOOT\BOOTX64.EFI" | Set-Content -Path $startup -Encoding ASCII
    $createdStartup = $true
}

$args = @(
    "-machine", "pc",
    "-drive", "if=pflash,format=raw,readonly=on,file=`"$ovmf`"",
    "-drive", "file=fat:rw:`"$esp`",format=raw,if=ide,index=0",
    "-drive", "file=`"$fat32Disk`",format=raw,if=ide,index=1,media=disk",
    "-m", "512M",
    "-vga", "std",
    "-display", "none",
    "-serial", "file:`"$serialLog`"",
    "-no-reboot"
)

$proc = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden
try {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not $proc.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (Test-Path $serialLog) {
            $partial = Get-Content $serialLog -Raw
            if ($null -eq $partial) { $partial = "" }
            if ($partial.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] done")) { break }
        }
    }

    if (-not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
        Wait-Process -Id $proc.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
} finally {
    Start-Sleep -Milliseconds 300
    if ($createdStartup) {
        Remove-Item $startup -ErrorAction SilentlyContinue
    }
    Restore-NormalKernelBuild
}

$output = if (Test-Path $serialLog) { Get-Content $serialLog -Raw } else { "" }
Write-Host $output

$checks = @(
    "[APPMODEL-LAUNCHSHADOW-SMOKE] issuing command=desktop.smoke.launchshadow",
    "[LaunchTargetShadowSmoke]",
    "command: desktop.smoke.launchshadow",
    "mode: diagnostic-only",
    "launchesApps: false",
    "case=ImageViewerStaticAlias",
    "inputLabel=`"ImgViewer`"",
    "comparison=expected-unsupported",
    "case=UnknownProbe",
    "inputLabel=`"FakeLaunchShadowApp`"",
    "comparison=unexpected-mismatch",
    "summary: observations=8 matches=5 acceptedMismatches=1 expectedUnsupported=1 unexpectedMismatches=1 nonFatal=true",
    "runtimeLaunchBehaviorChanged: false",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] issuing folder FileOpen SHADOW_ONLY probe",
    "source=SmokeFolderFileOpen",
    "handler=Files",
    "path=/",
    "resolvedType=FileOpen",
    "adapterLegacyDispatch=Files",
    "comparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] folder FileOpen SHADOW_ONLY probe done",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] issuing text FileOpen SHADOW_ONLY probe",
    "source=SmokeTextFileOpen",
    "handler=Notepad",
    "path=/test.txt",
    "resolvedType=FileOpen",
    "adapterLegacyDispatch=Notepad",
    "comparison=match",
    "nonFatal=true shadowOnly=true",
    "source=DesktopShortcutTextFile",
    "handler=Notepad",
    "path=/test.txt",
    "resolvedType=FileOpen",
    "adapterLegacyDispatch=Notepad",
    "comparison=match",
    "nonFatal=true shadowOnly=true",
    "source=DesktopFilesystemTextFile",
    "handler=Notepad",
    "path=/test.txt",
    "resolvedType=FileOpen",
    "adapterLegacyDispatch=Notepad",
    "comparison=match",
    "nonFatal=true shadowOnly=true",
    "source=RealBranchDesktopShortcutTextFile",
    "handler=Notepad",
    "path=/test.txt",
    "resolvedType=FileOpen",
    "adapterLegacyDispatch=Notepad",
    "comparison=match",
    "nonFatal=true shadowOnly=true",
    "source=RealBranchDesktopFilesystemTextFile",
    "handler=Notepad",
    "path=/test.txt",
    "resolvedType=FileOpen",
    "adapterLegacyDispatch=Notepad",
    "comparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch helper before temporary desktop state mutation",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch helper after temporary desktop state restoration",
    "[LaunchShadowRealBranchRestore] realBranchDesktopStateRestored=true",
    "realBranchShortcutSlotRestored=true",
    "realBranchFilesystemSlotRestored=true",
    "realBranchVisibleIconStateRestored=true",
    "realBranchNotificationStateRestored=true",
    "realBranchSelectedIconStateRestored=not-checked",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] text FileOpen SHADOW_ONLY probe done",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] done"
)

$failed = @()
foreach ($check in $checks) {
    if (-not $output.Contains($check)) { $failed += $check }
}

$unexpectedRows = [regex]::Matches($output, '^\s*case=.*comparison=unexpected-mismatch.*$', [System.Text.RegularExpressions.RegexOptions]::Multiline)
foreach ($row in $unexpectedRows) {
    if (-not $row.Value.Contains('case=UnknownProbe') -or -not $row.Value.Contains('inputLabel="FakeLaunchShadowApp"')) {
        $failed += "Unexpected mismatch row was not the intentional fake probe: $($row.Value)"
    }
}

$runtimeLaunchBehaviorUnchanged = $output.Contains("runtimeLaunchBehaviorChanged: false")
$imgViewerExpectedUnsupportedConfirmed =
    $output.Contains("case=ImageViewerStaticAlias") -and
    $output.Contains("inputLabel=`"ImgViewer`"") -and
    $output.Contains("comparison=expected-unsupported")
$fakeOnlyUnexpectedMismatchConfirmed =
    $unexpectedRows.Count -eq 1 -and
    $unexpectedRows[0].Value.Contains("case=UnknownProbe") -and
    $unexpectedRows[0].Value.Contains('inputLabel="FakeLaunchShadowApp"')
$folderFileOpenShadowOnlyConfirmed =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] issuing folder FileOpen SHADOW_ONLY probe") -and
    $output.Contains("source=SmokeFolderFileOpen") -and
    $output.Contains("handler=Files") -and
    $output.Contains("path=/") -and
    $output.Contains("resolvedType=FileOpen") -and
    $output.Contains("adapterLegacyDispatch=Files") -and
    $output.Contains("comparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$textFileOpenShadowOnlyConfirmed =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] issuing text FileOpen SHADOW_ONLY probe") -and
    $output.Contains("source=SmokeTextFileOpen") -and
    $output.Contains("handler=Notepad") -and
    $output.Contains("path=/test.txt") -and
    $output.Contains("resolvedType=FileOpen") -and
    $output.Contains("adapterLegacyDispatch=Notepad") -and
    $output.Contains("comparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$desktopShortcutTextFileShadowOnlyConfirmed =
    $output.Contains("source=DesktopShortcutTextFile") -and
    $output.Contains("handler=Notepad") -and
    $output.Contains("path=/test.txt") -and
    $output.Contains("resolvedType=FileOpen") -and
    $output.Contains("adapterLegacyDispatch=Notepad") -and
    $output.Contains("comparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$desktopFilesystemTextFileShadowOnlyConfirmed =
    $output.Contains("source=DesktopFilesystemTextFile") -and
    $output.Contains("handler=Notepad") -and
    $output.Contains("path=/test.txt") -and
    $output.Contains("resolvedType=FileOpen") -and
    $output.Contains("adapterLegacyDispatch=Notepad") -and
    $output.Contains("comparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$realBranchDesktopShortcutTextFileConfirmed =
    $output.Contains("source=RealBranchDesktopShortcutTextFile") -and
    $output.Contains("handler=Notepad") -and
    $output.Contains("path=/test.txt") -and
    $output.Contains("resolvedType=FileOpen") -and
    $output.Contains("adapterLegacyDispatch=Notepad") -and
    $output.Contains("comparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$realBranchDesktopFilesystemTextFileConfirmed =
    $output.Contains("source=RealBranchDesktopFilesystemTextFile") -and
    $output.Contains("handler=Notepad") -and
    $output.Contains("path=/test.txt") -and
    $output.Contains("resolvedType=FileOpen") -and
    $output.Contains("adapterLegacyDispatch=Notepad") -and
    $output.Contains("comparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$realBranchDesktopStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch helper before temporary desktop state mutation") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch helper after temporary desktop state restoration") -and
    $output.Contains("[LaunchShadowRealBranchRestore] realBranchDesktopStateRestored=true") -and
    $output.Contains("realBranchShortcutSlotRestored=true") -and
    $output.Contains("realBranchFilesystemSlotRestored=true") -and
    $output.Contains("realBranchVisibleIconStateRestored=true") -and
    $output.Contains("realBranchNotificationStateRestored=true") -and
    $output.Contains("realBranchSelectedIconStateRestored=not-checked")
$persistentDesktopStorageWritesAbsent =
    $output.Contains("persistentDesktopStorageWrites=false")

if ($unexpectedRows.Count -ne 1) {
    $failed += "Expected exactly one unexpected-mismatch row, found $($unexpectedRows.Count)"
}

if ($failed.Count -eq 0) {
    Write-AppModelLaunchShadowEvidence -Status "PASS" `
        -RuntimeLaunchBehaviorUnchanged $runtimeLaunchBehaviorUnchanged `
        -ImgViewerExpectedUnsupportedConfirmed $imgViewerExpectedUnsupportedConfirmed `
        -FakeOnlyUnexpectedMismatchConfirmed $fakeOnlyUnexpectedMismatchConfirmed `
        -FolderFileOpenShadowOnlyConfirmed $folderFileOpenShadowOnlyConfirmed `
        -TextFileOpenShadowOnlyConfirmed $textFileOpenShadowOnlyConfirmed `
        -DesktopShortcutTextFileShadowOnlyConfirmed $desktopShortcutTextFileShadowOnlyConfirmed `
        -DesktopFilesystemTextFileShadowOnlyConfirmed $desktopFilesystemTextFileShadowOnlyConfirmed `
        -RealBranchDesktopShortcutTextFileConfirmed $realBranchDesktopShortcutTextFileConfirmed `
        -RealBranchDesktopFilesystemTextFileConfirmed $realBranchDesktopFilesystemTextFileConfirmed `
        -RealBranchDesktopStateRestored $realBranchDesktopStateRestored `
        -PersistentDesktopStorageWritesAbsent $persistentDesktopStorageWritesAbsent `
        -UnexpectedMismatchRows $unexpectedRows.Count `
        -SerialLogPath $serialLog
    Write-Host "App-model launch shadow kernel smoke PASS. Serial log: $serialLog"
    Write-Host "App-model typed-dispatch gate evidence: $evidencePath"
    exit 0
}

Write-AppModelLaunchShadowEvidence -Status "FAIL" `
    -RuntimeLaunchBehaviorUnchanged $runtimeLaunchBehaviorUnchanged `
    -ImgViewerExpectedUnsupportedConfirmed $imgViewerExpectedUnsupportedConfirmed `
    -FakeOnlyUnexpectedMismatchConfirmed $fakeOnlyUnexpectedMismatchConfirmed `
    -FolderFileOpenShadowOnlyConfirmed $folderFileOpenShadowOnlyConfirmed `
    -TextFileOpenShadowOnlyConfirmed $textFileOpenShadowOnlyConfirmed `
    -DesktopShortcutTextFileShadowOnlyConfirmed $desktopShortcutTextFileShadowOnlyConfirmed `
    -DesktopFilesystemTextFileShadowOnlyConfirmed $desktopFilesystemTextFileShadowOnlyConfirmed `
    -RealBranchDesktopShortcutTextFileConfirmed $realBranchDesktopShortcutTextFileConfirmed `
    -RealBranchDesktopFilesystemTextFileConfirmed $realBranchDesktopFilesystemTextFileConfirmed `
    -RealBranchDesktopStateRestored $realBranchDesktopStateRestored `
    -PersistentDesktopStorageWritesAbsent $persistentDesktopStorageWritesAbsent `
    -UnexpectedMismatchRows $unexpectedRows.Count `
    -SerialLogPath $serialLog
Write-Host "App-model launch shadow kernel smoke FAIL. Serial log: $serialLog" -ForegroundColor Red
Write-Host "App-model typed-dispatch gate evidence: $evidencePath" -ForegroundColor Red
foreach ($item in $failed) { Write-Host "Missing/failed: $item" -ForegroundColor Red }
exit 1
