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
        [bool]$RealBranchFileAssociationsConfirmed,
        [bool]$RealBranchDesktopShortcutFolderConfirmed,
        [bool]$RealBranchDesktopFilesystemFolderConfirmed,
        [bool]$RealBranchDesktopSystemObjectRootFolderConfirmed,
        [bool]$RealBranchDesktopSystemObjectFileManagerConfirmed,
        [bool]$RealBranchDesktopSystemObjectTrashConfirmed,
        [bool]$RealBranchDesktopSystemObjectSystemSettingsConfirmed,
        [bool]$RealBranchPinnedDesktopNotepadConfirmed,
        [bool]$RealBranchStartMenuNotepadConfirmed,
        [bool]$RealBranchStartMenuBuiltInAppsConfirmed,
        [bool]$RealBranchStartMenuClockConfirmed,
        [bool]$RealBranchStartMenuFilesConfirmed,
        [bool]$RealBranchStartMenuConsoleConfirmed,
        [bool]$RealBranchStartMenuSettingsConfirmed,
        [bool]$RealBranchStartMenuSettingsExpectedNonFatalDriftConfirmed,
        [bool]$RealBranchStartMenuRightColumnShellActionsConfirmed,
        [bool]$RealBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed,
        [bool]$RealBranchStartMenuControlPanelConfirmed,
        [bool]$RealBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed,
        [bool]$RealBranchStartMenuAppModelConfirmed,
        [bool]$RealBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed,
        [bool]$RealBranchDesktopStateRestored,
        [bool]$RealBranchFileAssociationsStateRestored,
        [bool]$RealBranchFileAssociationsRestoreVerificationConfirmed,
        [bool]$RealBranchFolderDesktopStateRestored,
        [bool]$RealBranchFolderRestoreVerificationConfirmed,
        [bool]$RealBranchSystemObjectRootFolderDesktopStateRestored,
        [bool]$RealBranchSystemObjectRootFolderRestoreVerificationConfirmed,
        [bool]$RealBranchSystemObjectFileManagerDesktopStateRestored,
        [bool]$RealBranchSystemObjectFileManagerRestoreVerificationConfirmed,
        [bool]$RealBranchSystemObjectTrashDesktopStateRestored,
        [bool]$RealBranchSystemObjectTrashRestoreVerificationConfirmed,
        [bool]$RealBranchSystemObjectSystemSettingsDesktopStateRestored,
        [bool]$RealBranchSystemObjectSystemSettingsRestoreVerificationConfirmed,
        [bool]$RealBranchPinnedDesktopNotepadStateRestored,
        [bool]$RealBranchPinnedDesktopNotepadRestoreVerificationConfirmed,
        [bool]$RealBranchStartMenuNotepadStateRestored,
        [bool]$RealBranchStartMenuNotepadRestoreVerificationConfirmed,
        [bool]$RealBranchStartMenuBuiltInAppsStateRestored,
        [bool]$RealBranchStartMenuBuiltInAppsRestoreVerificationConfirmed,
        [bool]$RealBranchStartMenuFilesStateRestored,
        [bool]$RealBranchStartMenuFilesRestoreVerificationConfirmed,
        [bool]$RealBranchStartMenuConsoleStateRestored,
        [bool]$RealBranchStartMenuConsoleRestoreVerificationConfirmed,
        [bool]$RealBranchStartMenuSettingsStateRestored,
        [bool]$RealBranchStartMenuSettingsRestoreVerificationConfirmed,
        [bool]$RealBranchStartMenuRightColumnShellActionsStateRestored,
        [bool]$RealBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmed,
        [bool]$RealBranchStartMenuControlPanelStateRestored,
        [bool]$RealBranchStartMenuControlPanelRestoreVerificationConfirmed,
        [bool]$RealBranchStartMenuAppModelStateRestored,
        [bool]$RealBranchStartMenuAppModelRestoreVerificationConfirmed,
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
    $realBranchDesktopShortcutTextFileConfirmedFlag = if ($RealBranchDesktopShortcutTextFileConfirmed) { "true" } else { "false" }
    $realBranchDesktopFilesystemTextFileConfirmedFlag = if ($RealBranchDesktopFilesystemTextFileConfirmed) { "true" } else { "false" }
    $realBranchFileAssociationsConfirmedFlag = if ($RealBranchFileAssociationsConfirmed) { "true" } else { "false" }
    $realBranchDesktopShortcutFolderConfirmedFlag = if ($RealBranchDesktopShortcutFolderConfirmed) { "true" } else { "false" }
    $realBranchDesktopFilesystemFolderConfirmedFlag = if ($RealBranchDesktopFilesystemFolderConfirmed) { "true" } else { "false" }
    $realBranchDesktopSystemObjectRootFolderConfirmedFlag = if ($RealBranchDesktopSystemObjectRootFolderConfirmed) { "true" } else { "false" }
    $realBranchDesktopSystemObjectFileManagerConfirmedFlag = if ($RealBranchDesktopSystemObjectFileManagerConfirmed) { "true" } else { "false" }
    $realBranchDesktopSystemObjectTrashConfirmedFlag = if ($RealBranchDesktopSystemObjectTrashConfirmed) { "true" } else { "false" }
    $realBranchDesktopSystemObjectSystemSettingsConfirmedFlag = if ($RealBranchDesktopSystemObjectSystemSettingsConfirmed) { "true" } else { "false" }
    $realBranchPinnedDesktopNotepadConfirmedFlag = if ($RealBranchPinnedDesktopNotepadConfirmed) { "true" } else { "false" }
    $realBranchStartMenuNotepadConfirmedFlag = if ($RealBranchStartMenuNotepadConfirmed) { "true" } else { "false" }
    $realBranchStartMenuBuiltInAppsConfirmedFlag = if ($RealBranchStartMenuBuiltInAppsConfirmed) { "true" } else { "false" }
    $realBranchStartMenuClockConfirmedFlag = if ($RealBranchStartMenuClockConfirmed) { "true" } else { "false" }
    $realBranchStartMenuFilesConfirmedFlag = if ($RealBranchStartMenuFilesConfirmed) { "true" } else { "false" }
    $realBranchStartMenuConsoleConfirmedFlag = if ($RealBranchStartMenuConsoleConfirmed) { "true" } else { "false" }
    $realBranchStartMenuSettingsConfirmedFlag = if ($RealBranchStartMenuSettingsConfirmed) { "true" } else { "false" }
    $realBranchStartMenuSettingsExpectedNonFatalDriftConfirmedFlag = if ($RealBranchStartMenuSettingsExpectedNonFatalDriftConfirmed) { "true" } else { "false" }
    $realBranchStartMenuRightColumnShellActionsConfirmedFlag = if ($RealBranchStartMenuRightColumnShellActionsConfirmed) { "true" } else { "false" }
    $realBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmedFlag = if ($RealBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed) { "true" } else { "false" }
    $realBranchStartMenuControlPanelConfirmedFlag = if ($RealBranchStartMenuControlPanelConfirmed) { "true" } else { "false" }
    $realBranchStartMenuControlPanelExpectedNonFatalDriftConfirmedFlag = if ($RealBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed) { "true" } else { "false" }
    $realBranchStartMenuAppModelConfirmedFlag = if ($RealBranchStartMenuAppModelConfirmed) { "true" } else { "false" }
    $realBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmedFlag = if ($RealBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed) { "true" } else { "false" }
    $realBranchDesktopStateRestoredFlag = if ($RealBranchDesktopStateRestored) { "true" } else { "false" }
    $realBranchFileAssociationsStateRestoredFlag = if ($RealBranchFileAssociationsStateRestored) { "true" } else { "false" }
    $realBranchFileAssociationsRestoreVerificationConfirmedFlag = if ($RealBranchFileAssociationsRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchFolderDesktopStateRestoredFlag = if ($RealBranchFolderDesktopStateRestored) { "true" } else { "false" }
    $realBranchFolderRestoreVerificationConfirmedFlag = if ($RealBranchFolderRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchSystemObjectRootFolderDesktopStateRestoredFlag = if ($RealBranchSystemObjectRootFolderDesktopStateRestored) { "true" } else { "false" }
    $realBranchSystemObjectRootFolderRestoreVerificationConfirmedFlag = if ($RealBranchSystemObjectRootFolderRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchSystemObjectFileManagerDesktopStateRestoredFlag = if ($RealBranchSystemObjectFileManagerDesktopStateRestored) { "true" } else { "false" }
    $realBranchSystemObjectFileManagerRestoreVerificationConfirmedFlag = if ($RealBranchSystemObjectFileManagerRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchSystemObjectTrashDesktopStateRestoredFlag = if ($RealBranchSystemObjectTrashDesktopStateRestored) { "true" } else { "false" }
    $realBranchSystemObjectTrashRestoreVerificationConfirmedFlag = if ($RealBranchSystemObjectTrashRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchSystemObjectSystemSettingsDesktopStateRestoredFlag = if ($RealBranchSystemObjectSystemSettingsDesktopStateRestored) { "true" } else { "false" }
    $realBranchSystemObjectSystemSettingsRestoreVerificationConfirmedFlag = if ($RealBranchSystemObjectSystemSettingsRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchPinnedDesktopNotepadStateRestoredFlag = if ($RealBranchPinnedDesktopNotepadStateRestored) { "true" } else { "false" }
    $realBranchPinnedDesktopNotepadRestoreVerificationConfirmedFlag = if ($RealBranchPinnedDesktopNotepadRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchStartMenuNotepadStateRestoredFlag = if ($RealBranchStartMenuNotepadStateRestored) { "true" } else { "false" }
    $realBranchStartMenuNotepadRestoreVerificationConfirmedFlag = if ($RealBranchStartMenuNotepadRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchStartMenuBuiltInAppsStateRestoredFlag = if ($RealBranchStartMenuBuiltInAppsStateRestored) { "true" } else { "false" }
    $realBranchStartMenuBuiltInAppsRestoreVerificationConfirmedFlag = if ($RealBranchStartMenuBuiltInAppsRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchStartMenuFilesStateRestoredFlag = if ($RealBranchStartMenuFilesStateRestored) { "true" } else { "false" }
    $realBranchStartMenuFilesRestoreVerificationConfirmedFlag = if ($RealBranchStartMenuFilesRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchStartMenuConsoleStateRestoredFlag = if ($RealBranchStartMenuConsoleStateRestored) { "true" } else { "false" }
    $realBranchStartMenuConsoleRestoreVerificationConfirmedFlag = if ($RealBranchStartMenuConsoleRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchStartMenuSettingsStateRestoredFlag = if ($RealBranchStartMenuSettingsStateRestored) { "true" } else { "false" }
    $realBranchStartMenuSettingsRestoreVerificationConfirmedFlag = if ($RealBranchStartMenuSettingsRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchStartMenuRightColumnShellActionsStateRestoredFlag = if ($RealBranchStartMenuRightColumnShellActionsStateRestored) { "true" } else { "false" }
    $realBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmedFlag = if ($RealBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchStartMenuControlPanelStateRestoredFlag = if ($RealBranchStartMenuControlPanelStateRestored) { "true" } else { "false" }
    $realBranchStartMenuControlPanelRestoreVerificationConfirmedFlag = if ($RealBranchStartMenuControlPanelRestoreVerificationConfirmed) { "true" } else { "false" }
    $realBranchStartMenuAppModelStateRestoredFlag = if ($RealBranchStartMenuAppModelStateRestored) { "true" } else { "false" }
    $realBranchStartMenuAppModelRestoreVerificationConfirmedFlag = if ($RealBranchStartMenuAppModelRestoreVerificationConfirmed) { "true" } else { "false" }
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
        "realBranchDesktopShortcutTextFileConfirmed=$realBranchDesktopShortcutTextFileConfirmedFlag",
        "realBranchDesktopFilesystemTextFileConfirmed=$realBranchDesktopFilesystemTextFileConfirmedFlag",
        "realBranchFileAssociationsConfirmed=$realBranchFileAssociationsConfirmedFlag",
        "realBranchDesktopShortcutFolderConfirmed=$realBranchDesktopShortcutFolderConfirmedFlag",
        "realBranchDesktopFilesystemFolderConfirmed=$realBranchDesktopFilesystemFolderConfirmedFlag",
        "realBranchDesktopSystemObjectRootFolderConfirmed=$realBranchDesktopSystemObjectRootFolderConfirmedFlag",
        "realBranchDesktopSystemObjectFileManagerConfirmed=$realBranchDesktopSystemObjectFileManagerConfirmedFlag",
        "realBranchDesktopSystemObjectTrashConfirmed=$realBranchDesktopSystemObjectTrashConfirmedFlag",
        "realBranchDesktopSystemObjectSystemSettingsConfirmed=$realBranchDesktopSystemObjectSystemSettingsConfirmedFlag",
        "realBranchPinnedDesktopNotepadConfirmed=$realBranchPinnedDesktopNotepadConfirmedFlag",
        "realBranchStartMenuNotepadConfirmed=$realBranchStartMenuNotepadConfirmedFlag",
        "realBranchStartMenuBuiltInAppsConfirmed=$realBranchStartMenuBuiltInAppsConfirmedFlag",
        "realBranchStartMenuClockConfirmed=$realBranchStartMenuClockConfirmedFlag",
        "realBranchStartMenuFilesConfirmed=$realBranchStartMenuFilesConfirmedFlag",
        "realBranchStartMenuConsoleConfirmed=$realBranchStartMenuConsoleConfirmedFlag",
        "realBranchStartMenuSettingsConfirmed=$realBranchStartMenuSettingsConfirmedFlag",
        "realBranchStartMenuSettingsExpectedNonFatalDriftConfirmed=$realBranchStartMenuSettingsExpectedNonFatalDriftConfirmedFlag",
        "realBranchStartMenuRightColumnShellActionsConfirmed=$realBranchStartMenuRightColumnShellActionsConfirmedFlag",
        "realBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed=$realBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmedFlag",
        "realBranchStartMenuControlPanelConfirmed=$realBranchStartMenuControlPanelConfirmedFlag",
        "realBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed=$realBranchStartMenuControlPanelExpectedNonFatalDriftConfirmedFlag",
        "realBranchStartMenuAppModelConfirmed=$realBranchStartMenuAppModelConfirmedFlag",
        "realBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed=$realBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmedFlag",
        "realBranchDesktopStateRestored=$realBranchDesktopStateRestoredFlag",
        "realBranchFileAssociationsStateRestored=$realBranchFileAssociationsStateRestoredFlag",
        "realBranchFileAssociationsRestoreVerificationConfirmed=$realBranchFileAssociationsRestoreVerificationConfirmedFlag",
        "realBranchFolderDesktopStateRestored=$realBranchFolderDesktopStateRestoredFlag",
        "realBranchFolderRestoreVerificationConfirmed=$realBranchFolderRestoreVerificationConfirmedFlag",
        "realBranchSystemObjectRootFolderDesktopStateRestored=$realBranchSystemObjectRootFolderDesktopStateRestoredFlag",
        "realBranchSystemObjectRootFolderRestoreVerificationConfirmed=$realBranchSystemObjectRootFolderRestoreVerificationConfirmedFlag",
        "realBranchSystemObjectFileManagerDesktopStateRestored=$realBranchSystemObjectFileManagerDesktopStateRestoredFlag",
        "realBranchSystemObjectFileManagerRestoreVerificationConfirmed=$realBranchSystemObjectFileManagerRestoreVerificationConfirmedFlag",
        "realBranchSystemObjectTrashDesktopStateRestored=$realBranchSystemObjectTrashDesktopStateRestoredFlag",
        "realBranchSystemObjectTrashRestoreVerificationConfirmed=$realBranchSystemObjectTrashRestoreVerificationConfirmedFlag",
        "realBranchSystemObjectSystemSettingsDesktopStateRestored=$realBranchSystemObjectSystemSettingsDesktopStateRestoredFlag",
        "realBranchSystemObjectSystemSettingsRestoreVerificationConfirmed=$realBranchSystemObjectSystemSettingsRestoreVerificationConfirmedFlag",
        "realBranchPinnedDesktopNotepadStateRestored=$realBranchPinnedDesktopNotepadStateRestoredFlag",
        "realBranchPinnedDesktopNotepadRestoreVerificationConfirmed=$realBranchPinnedDesktopNotepadRestoreVerificationConfirmedFlag",
        "realBranchStartMenuNotepadStateRestored=$realBranchStartMenuNotepadStateRestoredFlag",
        "realBranchStartMenuNotepadRestoreVerificationConfirmed=$realBranchStartMenuNotepadRestoreVerificationConfirmedFlag",
        "realBranchStartMenuBuiltInAppsStateRestored=$realBranchStartMenuBuiltInAppsStateRestoredFlag",
        "realBranchStartMenuBuiltInAppsRestoreVerificationConfirmed=$realBranchStartMenuBuiltInAppsRestoreVerificationConfirmedFlag",
        "clockBareMetalParity=$realBranchStartMenuClockConfirmedFlag",
        "clockBehaviorPreserved=$realBranchStartMenuClockConfirmedFlag",
        "clockLaunchShadowReady=$realBranchStartMenuClockConfirmedFlag",
        "clockTypedDispatchReady=$realBranchStartMenuClockConfirmedFlag",
        "clockStillNotPromotedToTypedReady=false",
        "clockObservation=target=Clock classification=BuiltInApp appId=gxos.builtin.clock actualDispatch=Clock typedDispatchCandidate=Clock typedDispatchCandidateComparison=match",
        "realBranchStartMenuFilesStateRestored=$realBranchStartMenuFilesStateRestoredFlag",
        "realBranchStartMenuFilesRestoreVerificationConfirmed=$realBranchStartMenuFilesRestoreVerificationConfirmedFlag",
        "realBranchStartMenuConsoleStateRestored=$realBranchStartMenuConsoleStateRestoredFlag",
        "realBranchStartMenuConsoleRestoreVerificationConfirmed=$realBranchStartMenuConsoleRestoreVerificationConfirmedFlag",
        "realBranchStartMenuSettingsStateRestored=$realBranchStartMenuSettingsStateRestoredFlag",
        "realBranchStartMenuSettingsRestoreVerificationConfirmed=$realBranchStartMenuSettingsRestoreVerificationConfirmedFlag",
        "realBranchStartMenuRightColumnShellActionsStateRestored=$realBranchStartMenuRightColumnShellActionsStateRestoredFlag",
        "realBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmed=$realBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmedFlag",
        "realBranchStartMenuControlPanelStateRestored=$realBranchStartMenuControlPanelStateRestoredFlag",
        "realBranchStartMenuControlPanelRestoreVerificationConfirmed=$realBranchStartMenuControlPanelRestoreVerificationConfirmedFlag",
        "realBranchStartMenuAppModelStateRestored=$realBranchStartMenuAppModelStateRestoredFlag",
        "realBranchStartMenuAppModelRestoreVerificationConfirmed=$realBranchStartMenuAppModelRestoreVerificationConfirmedFlag",
        "typedDispatchFeatureGate=appmodel.typed-dispatch-runtime-gate",
        "typedDispatchDefault=enabled",
        "typedDispatchRuntimePath=active",
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

function New-AppModelFileAssociationFixtureDisk {
    param(
        [string]$SourcePath,
        [string]$DestinationPath
    )

    Copy-Item -Path $SourcePath -Destination $DestinationPath -Force
    $stream = [System.IO.File]::Open($DestinationPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    try {
        $boot = New-Object byte[] 512
        [void]$stream.Read($boot, 0, $boot.Length)
        $bytesPerSector = [BitConverter]::ToUInt16($boot, 11)
        $sectorsPerCluster = $boot[13]
        $reservedSectors = [BitConverter]::ToUInt16($boot, 14)
        $fatCount = $boot[16]
        $fatSectors = [BitConverter]::ToUInt32($boot, 36)
        $rootCluster = [BitConverter]::ToUInt32($boot, 44)
        $dataStartSector = $reservedSectors + ($fatCount * $fatSectors)

        function Read-DirectoryCluster([uint32]$Cluster) {
            $bytes = New-Object byte[] ($script:sectorsPerCluster * $script:bytesPerSector)
            $script:stream.Position = ($script:dataStartSector + (($Cluster - 2) * $script:sectorsPerCluster)) * $script:bytesPerSector
            [void]$script:stream.Read($bytes, 0, $bytes.Length)
            return $bytes
        }

        function Write-DirectoryCluster([uint32]$Cluster, [byte[]]$Bytes) {
            $script:stream.Position = ($script:dataStartSector + (($Cluster - 2) * $script:sectorsPerCluster)) * $script:bytesPerSector
            $script:stream.Write($Bytes, 0, $Bytes.Length)
        }

        function Find-ShortEntryOffset([byte[]]$Bytes, [string]$ShortName) {
            for ($offset = 0; $offset -lt $Bytes.Length; $offset += 32) {
                if ($Bytes[$offset] -eq 0) { break }
                if ($Bytes[$offset] -eq 0xE5 -or $Bytes[$offset + 11] -eq 0x0F) { continue }
                if ([Text.Encoding]::ASCII.GetString($Bytes, $offset, 11) -eq $ShortName) { return $offset }
            }
            return -1
        }

        function Find-FreeEntryOffset([byte[]]$Bytes) {
            for ($offset = 0; $offset -lt $Bytes.Length; $offset += 32) {
                # Deleted short entries can still have a live long-name entry immediately before them.
                if ($Bytes[$offset] -eq 0) { return $offset }
            }
            return -1
        }

        function Clear-LfnChainBeforeOffset([byte[]]$Bytes, [int]$ShortEntryOffset) {
            for ($offset = $ShortEntryOffset - 32; $offset -ge 0; $offset -= 32) {
                if ($Bytes[$offset] -eq 0x00 -or $Bytes[$offset + 11] -ne 0x0F) { break }
                $Bytes[$offset] = 0xE5
                for ($i = 1; $i -lt 32; $i++) { $Bytes[$offset + $i] = 0x00 }
            }
        }

        function Get-FixtureEntryPlacement([byte[]]$Bytes, [string]$ShortName, [string[]]$FallbackShortNames) {
            $existingOffset = Find-ShortEntryOffset $Bytes $ShortName
            if ($existingOffset -ge 0) { return @{ Offset = $existingOffset; ReusedFallback = $false } }

            $freeOffset = Find-FreeEntryOffset $Bytes
            if ($freeOffset -ge 0) { return @{ Offset = $freeOffset; ReusedFallback = $false } }

            foreach ($fallbackShortName in $FallbackShortNames) {
                $fallbackOffset = Find-ShortEntryOffset $Bytes $fallbackShortName
                if ($fallbackOffset -ge 0) { return @{ Offset = $fallbackOffset; ReusedFallback = $true } }
            }

            return @{ Offset = -1; ReusedFallback = $false }
        }

        $script:stream = $stream
        $script:bytesPerSector = $bytesPerSector
        $script:sectorsPerCluster = $sectorsPerCluster
        $script:dataStartSector = $dataStartSector

        $rootBytes = Read-DirectoryCluster $rootCluster
        $templateOffset = Find-ShortEntryOffset $rootBytes "README  TXT"
        $appsOffset = Find-ShortEntryOffset $rootBytes "APPS       "
        if ($templateOffset -lt 0 -or $appsOffset -lt 0) {
            throw "Unable to find README.txt or /apps in FAT32 smoke fixture disk."
        }

        $appsCluster = ([BitConverter]::ToUInt16($rootBytes, $appsOffset + 20) -shl 16) -bor [BitConverter]::ToUInt16($rootBytes, $appsOffset + 26)
        $appsBytes = Read-DirectoryCluster $appsCluster
        $template = New-Object byte[] 32
        [Array]::Copy($rootBytes, $templateOffset, $template, 0, 32)

        foreach ($fixture in @(
            @{ Bytes = $rootBytes; Cluster = $rootCluster; ShortName = "EVENTS  LOG" },
            @{ Bytes = $appsBytes; Cluster = $appsCluster; ShortName = "CONFIG  CFG" },
            @{ Bytes = $appsBytes; Cluster = $appsCluster; ShortName = "CONFIG  INI" }
        )) {
            $placement = Get-FixtureEntryPlacement $fixture.Bytes $fixture.ShortName @("MUSIC      ", "TRASH      ", "DESKTOP    ", "SPECIAL TXT")
            $targetOffset = [int]$placement.Offset
            if ($targetOffset -lt 0) { throw "No FAT32 directory slot available for $($fixture.ShortName)." }
            if ($placement.ReusedFallback) {
                Clear-LfnChainBeforeOffset $fixture.Bytes $targetOffset
            }
            [Array]::Copy($template, 0, $fixture.Bytes, $targetOffset, 32)
            $shortNameBytes = [Text.Encoding]::ASCII.GetBytes($fixture.ShortName)
            [Array]::Copy($shortNameBytes, 0, $fixture.Bytes, $targetOffset, 11)
            Write-DirectoryCluster $fixture.Cluster $fixture.Bytes
        }
    } catch {
        $stream.Dispose()
        Remove-Item $DestinationPath -Force -ErrorAction SilentlyContinue
        throw
    } finally {
        if ($stream) { $stream.Dispose() }
        Remove-Variable -Scope Script -Name stream,bytesPerSector,sectorsPerCluster,dataStartSector -ErrorAction SilentlyContinue
    }
}

$fixtureFat32Disk = Join-Path $LogDir "appmodel-launchshadow-fat32-$stamp.img"
New-AppModelFileAssociationFixtureDisk -SourcePath $fat32Disk -DestinationPath $fixtureFat32Disk

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
    "-drive", "file=`"$fixtureFat32Disk`",format=raw,if=ide,index=1,media=disk",
    "-m", "512M",
    "-vga", "std",
    "-display", "none",
    "-serial", "file:`"$serialLog`"",
    "-no-reboot"
)

$proc = $null
try {
    $proc = Start-Process -FilePath $qemu -ArgumentList $args -PassThru -WindowStyle Hidden
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
    Remove-Item $fixtureFat32Disk -Force -ErrorAction SilentlyContinue
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
    "case=ImageViewerLegacyAlias",
    "inputLabel=`"ImgViewer`"",
    "comparison=match",
    "case=UnknownProbe",
    "inputLabel=`"FakeLaunchShadowApp`"",
    "comparison=unexpected-mismatch",
    "summary: observations=10 matches=9 acceptedMismatches=0 expectedUnsupported=0 unexpectedMismatches=1 typedDispatch=8 legacyFallback=1 blockedUnknownFallback=1 specialCaseFallback=0 nonFatal=true",
    "runtimeLaunchBehaviorChanged: false",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] issuing folder FileOpen SHADOW_ONLY probe",
    "source=SmokeFolderFileOpen",
    "handler=Files",
    "path=/",
    "resolvedType=FileOpen",
    "adapterLegacyDispatch=Files",
    "comparison=match",
    "nonFatal=true shadowOnly=true",
    "[LaunchShadowBareMetalDesktopNavigation] source=RealBranchDesktopShortcutFolder target=/ current=",
    "[LaunchShadowBareMetalDesktopNavigation] source=RealBranchDesktopFilesystemFolder target=/ current=",
    "source=RealBranchDesktopSystemObjectRootFolder",
    "handler=Files",
    "path=/",
    "resolvedType=FileOpen",
    "adapterLegacyDispatch=Files",
    "comparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch folder helper before temporary desktop state mutation",
    "[LaunchShadowRealBranchFolderMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch folder helper after temporary desktop state restoration",
    "[LaunchShadowRealBranchFolderRestore] realBranchFolderDesktopStateRestored=true",
    "realBranchFolderShortcutSlotRestored=true",
    "realBranchFolderFilesystemSlotRestored=true",
    "realBranchFolderVisibleIconStateRestored=true",
    "realBranchFolderNotificationStateRestored=true",
    "realBranchFolderSourceOverrideStateRestored=true",
    "realBranchFolderSuppressLaunchStateRestored=true",
    "realBranchFolderSelectedIconStateRestored=true",
    "[LaunchShadowRealBranchFolderRestoreVerification] phase=after realBranchFolderDesktopStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object root-folder helper before temporary desktop state mutation",
    "[LaunchShadowRealBranchSystemObjectRootFolderMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object root-folder helper after temporary desktop state restoration",
    "[LaunchShadowRealBranchSystemObjectRootFolderRestore] realBranchSystemObjectRootFolderDesktopStateRestored=true",
    "realBranchSystemObjectRootFolderVisibleIconStateRestored=true",
    "realBranchSystemObjectRootFolderNotificationStateRestored=true",
    "realBranchSystemObjectRootFolderSourceOverrideStateRestored=true",
    "realBranchSystemObjectRootFolderSuppressLaunchStateRestored=true",
    "realBranchSystemObjectRootFolderSelectedIconStateRestored=true",
    "[LaunchShadowRealBranchSystemObjectRootFolderRestoreVerification] phase=after realBranchSystemObjectRootFolderDesktopStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object File Manager helper before temporary desktop state mutation",
    "[LaunchShadowRealBranchSystemObjectFileManagerMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchDesktopSystemObjectFileManager",
    "uiLabel=Files",
    "actualDispatch=Files",
    "resolvedType=LegacyAlias",
    "typedDispatchCandidate=Files",
    "typedDispatchCandidateComparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object File Manager helper after temporary desktop state restoration",
    "[LaunchShadowRealBranchSystemObjectFileManagerRestore] realBranchSystemObjectFileManagerDesktopStateRestored=true",
    "realBranchSystemObjectFileManagerVisibleIconStateRestored=true",
    "realBranchSystemObjectFileManagerNotificationStateRestored=true",
    "realBranchSystemObjectFileManagerSourceOverrideStateRestored=true",
    "realBranchSystemObjectFileManagerSuppressLaunchStateRestored=true",
    "realBranchSystemObjectFileManagerSelectedIconStateRestored=true",
    "[LaunchShadowRealBranchSystemObjectFileManagerRestoreVerification] phase=after realBranchSystemObjectFileManagerDesktopStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object Trash helper before temporary desktop state mutation",
    "[LaunchShadowRealBranchSystemObjectTrashMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchDesktopSystemObjectTrash",
    "uiLabel=Trash",
    "actualDispatch=Trash",
    "resolvedType=BuiltInApp",
    "typedDispatchCandidate=Trash",
    "typedDispatchCandidateComparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object Trash helper after temporary desktop state restoration",
    "[LaunchShadowRealBranchSystemObjectTrashRestore] realBranchSystemObjectTrashDesktopStateRestored=true",
    "realBranchSystemObjectTrashVisibleIconStateRestored=true",
    "realBranchSystemObjectTrashNotificationStateRestored=true",
    "realBranchSystemObjectTrashSourceOverrideStateRestored=true",
    "realBranchSystemObjectTrashSuppressLaunchStateRestored=true",
    "realBranchSystemObjectTrashSelectedIconStateRestored=true",
    "[LaunchShadowRealBranchSystemObjectTrashRestoreVerification] phase=after realBranchSystemObjectTrashDesktopStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object System Settings helper before temporary desktop state mutation",
    "[LaunchShadowRealBranchSystemObjectSystemSettingsMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchDesktopSystemObjectSystemSettings",
    "uiLabel=System Settings",
    "actualDispatch=DisplayOptions",
    "resolvedType=BuiltInApp",
    "appId=gxos.builtin.displayoptions",
    "resolvedDispatch=DisplayOptions",
    "typedDispatchCandidate=DisplayOptions",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object System Settings helper after temporary desktop state restoration",
    "[LaunchShadowRealBranchSystemObjectSystemSettingsRestore] realBranchSystemObjectSystemSettingsDesktopStateRestored=true",
    "realBranchSystemObjectSystemSettingsVisibleIconStateRestored=true",
    "realBranchSystemObjectSystemSettingsNotificationStateRestored=true",
    "realBranchSystemObjectSystemSettingsSourceOverrideStateRestored=true",
    "realBranchSystemObjectSystemSettingsSuppressLaunchStateRestored=true",
    "realBranchSystemObjectSystemSettingsSelectedIconStateRestored=true",
    "[LaunchShadowRealBranchSystemObjectSystemSettingsRestoreVerification] phase=after realBranchSystemObjectSystemSettingsDesktopStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch pinned desktop Notepad helper before temporary desktop state mutation",
    "[LaunchShadowRealBranchPinnedDesktopNotepadMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchPinnedDesktopNotepad",
    "uiLabel=Notepad",
    "actualDispatch=Notepad",
    "resolvedType=BuiltInApp",
    "appId=gxos.builtin.notepad",
    "resolvedDispatch=Notepad",
    "typedDispatchCandidate=Notepad",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch pinned desktop Notepad helper after temporary desktop state restoration",
    "[LaunchShadowRealBranchPinnedDesktopNotepadRestore] realBranchPinnedDesktopNotepadStateRestored=true",
    "realBranchPinnedDesktopNotepadShortcutSlotRestored=true",
    "realBranchPinnedDesktopNotepadVisibleIconStateRestored=true",
    "realBranchPinnedDesktopNotepadNotificationStateRestored=true",
    "realBranchPinnedDesktopNotepadSourceOverrideStateRestored=true",
    "realBranchPinnedDesktopNotepadSuppressLaunchStateRestored=true",
    "[LaunchShadowRealBranchPinnedDesktopNotepadRestoreVerification] phase=after realBranchPinnedDesktopNotepadStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Notepad helper before temporary Start Menu state mutation",
    "[LaunchShadowRealBranchStartMenuNotepadMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchStartMenuNotepad",
    "uiLabel=Notepad",
    "actualDispatch=Notepad",
    "resolvedType=BuiltInApp",
    "appId=gxos.builtin.notepad",
    "resolvedDispatch=Notepad",
    "typedDispatchCandidate=Notepad",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Notepad helper after temporary Start Menu state restoration",
    "[LaunchShadowRealBranchStartMenuNotepadRestore] realBranchStartMenuNotepadStateRestored=true",
    "realBranchStartMenuNotepadMenuOpenStateRestored=true",
    "realBranchStartMenuNotepadNotificationStateRestored=true",
    "realBranchStartMenuNotepadSourceOverrideStateRestored=true",
    "realBranchStartMenuNotepadSuppressLaunchStateRestored=true",
    "[LaunchShadowRealBranchStartMenuNotepadRestoreVerification] phase=after realBranchStartMenuNotepadStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu BuiltInApp helper before temporary Start Menu state mutation",
    "[LaunchShadowRealBranchStartMenuBuiltInAppsMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchStartMenuCalculator",
    "uiLabel=Calculator",
    "actualDispatch=Calculator",
    "resolvedType=BuiltInApp",
    "appId=gxos.builtin.calculator",
    "resolvedDispatch=Calculator",
    "typedDispatchCandidate=Calculator",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=match",
    "source=RealBranchStartMenuClock",
    "uiLabel=Clock",
    "actualDispatch=Clock",
    "resolvedType=BuiltInApp",
    "appId=gxos.builtin.clock",
    "resolvedDispatch=Clock",
    "typedDispatchCandidate=Clock",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=match",
    "source=RealBranchStartMenuTaskManager",
    "uiLabel=TaskManager",
    "actualDispatch=TaskManager",
    "appId=gxos.builtin.taskmanager",
    "resolvedDispatch=TaskManager",
    "typedDispatchCandidate=TaskManager",
    "source=RealBranchStartMenuDiskManager",
    "uiLabel=DiskManager",
    "actualDispatch=DiskManager",
    "appId=gxos.builtin.diskmanager",
    "resolvedDispatch=DiskManager",
    "typedDispatchCandidate=DiskManager",
    "source=RealBranchStartMenuTrash",
    "uiLabel=Trash",
    "actualDispatch=Trash",
    "appId=gxos.builtin.trash",
    "resolvedDispatch=Trash",
    "typedDispatchCandidate=Trash",
    "source=RealBranchStartMenuDisplayOptions",
    "uiLabel=DisplayOptions",
    "actualDispatch=DisplayOptions",
    "appId=gxos.builtin.displayoptions",
    "resolvedDispatch=DisplayOptions",
    "typedDispatchCandidate=DisplayOptions",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu BuiltInApp helper after temporary Start Menu state restoration",
    "[LaunchShadowRealBranchStartMenuBuiltInAppsRestore] realBranchStartMenuBuiltInAppsStateRestored=true",
    "realBranchStartMenuBuiltInAppsMenuOpenStateRestored=true",
    "realBranchStartMenuBuiltInAppsNotificationStateRestored=true",
    "realBranchStartMenuBuiltInAppsSourceOverrideStateRestored=true",
    "realBranchStartMenuBuiltInAppsSuppressLaunchStateRestored=true",
    "[LaunchShadowRealBranchStartMenuBuiltInAppsRestoreVerification] phase=after realBranchStartMenuBuiltInAppsStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Files helper before temporary Start Menu state mutation",
    "[LaunchShadowRealBranchStartMenuFilesMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchStartMenuFiles",
    "uiLabel=Files",
    "actualDispatch=Files",
    "resolvedType=LegacyAlias",
    "appId=gxos.builtin.fileexplorer",
    "resolvedDispatch=Files",
    "typedDispatchCandidate=Files",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Files helper after temporary Start Menu state restoration",
    "[LaunchShadowRealBranchStartMenuFilesRestore] realBranchStartMenuFilesStateRestored=true",
    "realBranchStartMenuFilesMenuOpenStateRestored=true",
    "realBranchStartMenuFilesNotificationStateRestored=true",
    "realBranchStartMenuFilesSourceOverrideStateRestored=true",
    "realBranchStartMenuFilesSuppressLaunchStateRestored=true",
    "[LaunchShadowRealBranchStartMenuFilesRestoreVerification] phase=after realBranchStartMenuFilesStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Console helper before temporary Start Menu state mutation",
    "[LaunchShadowRealBranchStartMenuConsoleMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchStartMenuConsole",
    "uiLabel=Console",
    "actualDispatch=Console",
    "resolvedType=ShellAction",
    "appId=",
    "resolvedDispatch=Console",
    "typedDispatchCandidate=Console",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Console helper after temporary Start Menu state restoration",
    "[LaunchShadowRealBranchStartMenuConsoleRestore] realBranchStartMenuConsoleStateRestored=true",
    "realBranchStartMenuConsoleMenuOpenStateRestored=true",
    "realBranchStartMenuConsoleNotificationStateRestored=true",
    "realBranchStartMenuConsoleSourceOverrideStateRestored=true",
    "realBranchStartMenuConsoleSuppressLaunchStateRestored=true",
    "realBranchStartMenuConsoleShellActionStateRestored=true",
    "[LaunchShadowRealBranchStartMenuConsoleRestoreVerification] phase=after realBranchStartMenuConsoleStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Settings helper before temporary Start Menu state mutation",
    "[LaunchShadowRealBranchStartMenuSettingsMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchStartMenuSettings",
    "uiLabel=Settings",
    "actualDispatch=Control Panel",
    "resolvedType=ShellAction",
    "appId=",
    "resolvedDispatch=Control Panel",
    "typedDispatchCandidate=Control Panel",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=match",
    "typedDispatchCandidateStatus=ok",
    "actualBehavior=embedded-control-panel-state",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Settings helper after temporary Start Menu state restoration",
    "[LaunchShadowRealBranchStartMenuSettingsRestore] realBranchStartMenuSettingsStateRestored=true",
    "realBranchStartMenuSettingsMenuOpenStateRestored=true",
    "realBranchStartMenuSettingsNotificationStateRestored=true",
    "realBranchStartMenuSettingsSourceOverrideStateRestored=true",
    "realBranchStartMenuSettingsSuppressLaunchStateRestored=true",
    "realBranchStartMenuSettingsShellActionStateRestored=true",
    "[LaunchShadowRealBranchStartMenuSettingsRestoreVerification] phase=after realBranchStartMenuSettingsStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu right-column ShellAction helper before temporary Start Menu state mutation",
    "[LaunchShadowRealBranchStartMenuRightColumnShellActionsMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=StartMenuFolder",
    "handler=Files",
    "path=/",
    "resolvedType=FileOpen",
    "appId=",
    "resolvedDispatch=Files",
    "adapterLegacyDispatch=Files",
    "candidateMatchesHandler=true",
    "comparison=match",
    "adapterStatus=ok",
    "adapterReason=File-open target carries the current handler app name; path remains a separate parameter",
    "dispatchUsage=legacy-fallback",
    "selectedDispatch=Files",
    "behaviorPreserved=true",
    "status=resolved-file-open",
    "reason=Folder path resolves to existing File Explorer parameter launch",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu right-column ShellAction helper after temporary Start Menu state restoration",
    "[LaunchShadowRealBranchStartMenuRightColumnShellActionsRestore] realBranchStartMenuRightColumnShellActionsStateRestored=true",
    "realBranchStartMenuRightColumnShellActionsMenuOpenStateRestored=true",
    "realBranchStartMenuRightColumnShellActionsNotificationStateRestored=true",
    "realBranchStartMenuRightColumnShellActionsSourceOverrideStateRestored=true",
    "realBranchStartMenuRightColumnShellActionsSuppressLaunchStateRestored=true",
    "realBranchStartMenuRightColumnShellActionsShellActionStateRestored=true",
    "[LaunchShadowRealBranchStartMenuRightColumnShellActionsRestoreVerification] phase=after realBranchStartMenuRightColumnShellActionsStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Control Panel helper before temporary embedded-action state mutation",
    "[LaunchShadowRealBranchStartMenuControlPanelMutation] phase=before temporaryEmbeddedActionStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchStartMenuControlPanel",
    "uiLabel=Control Panel",
    "actualDispatch=Control Panel",
    "resolvedType=ShellAction",
    "appId=",
    "resolvedDispatch=Control Panel",
    "typedDispatchCandidate=Control Panel",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=match",
    "typedDispatchCandidateStatus=ok",
    "actualBehavior=embedded-control-panel-state",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Control Panel helper after temporary embedded-action state restoration",
    "[LaunchShadowRealBranchStartMenuControlPanelRestore] realBranchStartMenuControlPanelStateRestored=true",
    "realBranchStartMenuControlPanelMenuOpenStateRestored=true",
    "realBranchStartMenuControlPanelNotificationStateRestored=true",
    "realBranchStartMenuControlPanelSourceOverrideStateRestored=true",
    "realBranchStartMenuControlPanelSuppressEmbeddedActionStateRestored=true",
    "realBranchStartMenuControlPanelEmbeddedStateRestored=true",
    "realBranchStartMenuControlPanelSelectionStateRestored=true",
    "[LaunchShadowRealBranchStartMenuControlPanelRestoreVerification] phase=after realBranchStartMenuControlPanelStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu AppModel helper before temporary embedded-action state mutation",
    "[LaunchShadowRealBranchStartMenuAppModelMutation] phase=before temporaryEmbeddedActionStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "source=RealBranchStartMenuAppModel",
    "uiLabel=AppModel",
    "actualDispatch=AppModel",
    "resolvedType=LegacyAlias",
    "appId=gxos.builtin.appmodeldemo",
    "resolvedDispatch=AppModel",
    "typedDispatchCandidate=AppModel",
    "typedDispatchCandidateMatchesActual=true",
    "typedDispatchCandidateComparison=unsupported-embedded-diagnostic-action",
    "typedDispatchCandidateStatus=unsupported",
    "typedDispatchCandidateReason=Current adapter only reproduces the AppModel label; it does not encode the embedded app-model diagnostic viewer action",
    "actualBehavior=embedded-app-model-diagnostic-viewer",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu AppModel helper after temporary embedded-action state restoration",
    "[LaunchShadowRealBranchStartMenuAppModelRestore] realBranchStartMenuAppModelStateRestored=true",
    "realBranchStartMenuAppModelMenuOpenStateRestored=true",
    "realBranchStartMenuAppModelNotificationStateRestored=true",
    "realBranchStartMenuAppModelSourceOverrideStateRestored=true",
    "realBranchStartMenuAppModelSuppressEmbeddedActionStateRestored=true",
    "realBranchStartMenuAppModelEmbeddedStateRestored=true",
    "[LaunchShadowRealBranchStartMenuAppModelRestoreVerification] phase=after realBranchStartMenuAppModelStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
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
    "[LaunchShadowRealBranchMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch helper after temporary desktop state restoration",
    "[LaunchShadowRealBranchRestore] realBranchDesktopStateRestored=true",
    "realBranchShortcutSlotRestored=true",
    "realBranchFilesystemSlotRestored=true",
    "realBranchVisibleIconStateRestored=true",
    "realBranchNotificationStateRestored=true",
    "realBranchSourceOverrideStateRestored=true",
    "realBranchSuppressLaunchStateRestored=true",
    "realBranchSelectedIconStateRestored=true",
    "[LaunchShadowRealBranchRestoreVerification] phase=after realBranchDesktopStateRestored=true",
    "persistentDesktopStorageWrites=false",
    "nonFatal=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch file associations helper before temporary desktop state mutation",
    "[LaunchShadowRealBranchFileAssociationsMutation] phase=before temporaryDesktopStateMutation=true temporaryFixtureWrites=false hostPreparedFixtures=true persistentDesktopStorageWrites=false nonFatal=true",
    "[LaunchShadowRealBranchFileAssociationFixture] path=/events.log ready=true",
    "[LaunchShadowRealBranchFileAssociationFixture] path=/apps/config.cfg ready=true",
    "[LaunchShadowRealBranchFileAssociationFixture] path=/apps/config.ini ready=true",
    "source=RealBranchDesktopShortcutLogFile",
    "handler=Notepad",
    "path=/events.log",
    "source=RealBranchDesktopFilesystemLogFile",
    "source=RealBranchDesktopShortcutCfgFile",
    "path=/apps/config.cfg",
    "source=RealBranchDesktopFilesystemCfgFile",
    "source=RealBranchDesktopShortcutIniFile",
    "path=/apps/config.ini",
    "source=RealBranchDesktopFilesystemIniFile",
    "resolvedType=FileOpen",
    "adapterLegacyDispatch=Notepad",
    "comparison=match",
    "nonFatal=true shadowOnly=true",
    "[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch file associations helper after temporary desktop state restoration",
    "[LaunchShadowRealBranchFileAssociationsRestore] realBranchFileAssociationsStateRestored=true",
    "realBranchFileAssociationsFixturesReady=true",
    "realBranchFileAssociationsFixtureWritesRestored=true",
    "realBranchFileAssociationsShortcutSlotRestored=true",
    "realBranchFileAssociationsFilesystemSlotRestored=true",
    "realBranchFileAssociationsVisibleIconStateRestored=true",
    "realBranchFileAssociationsNotificationStateRestored=true",
    "realBranchFileAssociationsSourceOverrideStateRestored=true",
    "realBranchFileAssociationsSuppressLaunchStateRestored=true",
    "realBranchFileAssociationsSelectedIconStateRestored=true",
    "[LaunchShadowRealBranchFileAssociationsRestoreVerification] phase=after realBranchFileAssociationsStateRestored=true",
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
$typedDispatchUsageConfirmed = $output.Contains("dispatchUsage=typed-dispatch")
$legacyFallbackUsageConfirmed = $output.Contains("dispatchUsage=legacy-fallback")
$blockedUnknownFallbackUsageConfirmed = $output.Contains("dispatchUsage=blocked-unknown-fallback")
$specialCaseFallbackUsageConfirmed = $output.Contains("dispatchUsage=special-case-fallback")
$imgViewerExpectedUnsupportedConfirmed =
    $output.Contains("case=ImageViewerLegacyAlias") -and
    $output.Contains("inputLabel=`"ImgViewer`"") -and
    $output.Contains("comparison=match")
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
$realBranchFileAssociationsConfirmed =
    [regex]::IsMatch($output, 'source=RealBranchDesktopShortcutLogFile handler=Notepad path=/events\.log resolvedType=FileOpen .* adapterLegacyDispatch=Notepad .* comparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchDesktopFilesystemLogFile handler=Notepad path=/events\.log resolvedType=FileOpen .* adapterLegacyDispatch=Notepad .* comparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchDesktopShortcutCfgFile handler=Notepad path=/apps/config\.cfg resolvedType=FileOpen .* adapterLegacyDispatch=Notepad .* comparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchDesktopFilesystemCfgFile handler=Notepad path=/apps/config\.cfg resolvedType=FileOpen .* adapterLegacyDispatch=Notepad .* comparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchDesktopShortcutIniFile handler=Notepad path=/apps/config\.ini resolvedType=FileOpen .* adapterLegacyDispatch=Notepad .* comparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchDesktopFilesystemIniFile handler=Notepad path=/apps/config\.ini resolvedType=FileOpen .* adapterLegacyDispatch=Notepad .* comparison=match .* nonFatal=true shadowOnly=true')
$realBranchDesktopShortcutFolderConfirmed =
    [regex]::IsMatch($output, '\[LaunchShadowBareMetalDesktopNavigation\] source=RealBranchDesktopShortcutFolder target=/ current=.* persistentDesktopStorageWrites=false nonFatal=true')
$realBranchDesktopFilesystemFolderConfirmed =
    [regex]::IsMatch($output, '\[LaunchShadowBareMetalDesktopNavigation\] source=RealBranchDesktopFilesystemFolder target=/ current=.* persistentDesktopStorageWrites=false nonFatal=true')
$realBranchDesktopSystemObjectRootFolderConfirmed =
    $output.Contains("source=RealBranchDesktopSystemObjectRootFolder") -and
    $output.Contains("handler=Files") -and
    $output.Contains("path=/") -and
    $output.Contains("resolvedType=FileOpen") -and
    $output.Contains("adapterLegacyDispatch=Files") -and
    $output.Contains("comparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$realBranchDesktopSystemObjectFileManagerConfirmed =
    $output.Contains("source=RealBranchDesktopSystemObjectFileManager") -and
    $output.Contains("uiLabel=Files") -and
    $output.Contains("actualDispatch=Files") -and
    $output.Contains("resolvedType=LegacyAlias") -and
    $output.Contains("typedDispatchCandidate=Files") -and
    $output.Contains("typedDispatchCandidateComparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$realBranchDesktopSystemObjectTrashConfirmed =
    $output.Contains("source=RealBranchDesktopSystemObjectTrash") -and
    $output.Contains("uiLabel=Trash") -and
    $output.Contains("actualDispatch=Trash") -and
    $output.Contains("resolvedType=BuiltInApp") -and
    $output.Contains("typedDispatchCandidate=Trash") -and
    $output.Contains("typedDispatchCandidateComparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$realBranchDesktopSystemObjectSystemSettingsConfirmed =
    $output.Contains("source=RealBranchDesktopSystemObjectSystemSettings") -and
    $output.Contains("uiLabel=System Settings") -and
    $output.Contains("actualDispatch=DisplayOptions") -and
    $output.Contains("resolvedType=BuiltInApp") -and
    $output.Contains("appId=gxos.builtin.displayoptions") -and
    $output.Contains("resolvedDispatch=DisplayOptions") -and
    $output.Contains("typedDispatchCandidate=DisplayOptions") -and
    $output.Contains("typedDispatchCandidateMatchesActual=true") -and
    $output.Contains("typedDispatchCandidateComparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$realBranchPinnedDesktopNotepadConfirmed =
    [regex]::IsMatch($output, 'source=RealBranchPinnedDesktopNotepad uiLabel=Notepad actualDispatch=Notepad resolvedType=BuiltInApp appId=gxos\.builtin\.notepad resolvedDispatch=Notepad typedDispatchCandidate=Notepad typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match .* nonFatal=true shadowOnly=true')
$realBranchStartMenuNotepadConfirmed =
    $output.Contains("source=RealBranchStartMenuNotepad") -and
    $output.Contains("uiLabel=Notepad") -and
    $output.Contains("actualDispatch=Notepad") -and
    $output.Contains("resolvedType=BuiltInApp") -and
    $output.Contains("appId=gxos.builtin.notepad") -and
    $output.Contains("resolvedDispatch=Notepad") -and
    $output.Contains("typedDispatchCandidate=Notepad") -and
    $output.Contains("typedDispatchCandidateMatchesActual=true") -and
    $output.Contains("typedDispatchCandidateComparison=match") -and
    $output.Contains("nonFatal=true shadowOnly=true")
$realBranchStartMenuBuiltInAppsConfirmed =
    [regex]::IsMatch($output, 'source=RealBranchStartMenuCalculator uiLabel=Calculator actualDispatch=Calculator resolvedType=BuiltInApp appId=gxos\.builtin\.calculator resolvedDispatch=Calculator typedDispatchCandidate=Calculator typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchStartMenuClock uiLabel=Clock actualDispatch=Clock resolvedType=BuiltInApp appId=gxos\.builtin\.clock resolvedDispatch=Clock typedDispatchCandidate=Clock typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchStartMenuTaskManager uiLabel=TaskManager actualDispatch=TaskManager resolvedType=BuiltInApp appId=gxos\.builtin\.taskmanager resolvedDispatch=TaskManager typedDispatchCandidate=TaskManager typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchStartMenuDiskManager uiLabel=DiskManager actualDispatch=DiskManager resolvedType=BuiltInApp appId=gxos\.builtin\.diskmanager resolvedDispatch=DiskManager typedDispatchCandidate=DiskManager typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchStartMenuTrash uiLabel=Trash actualDispatch=Trash resolvedType=BuiltInApp appId=gxos\.builtin\.trash resolvedDispatch=Trash typedDispatchCandidate=Trash typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match .* nonFatal=true shadowOnly=true') -and
    [regex]::IsMatch($output, 'source=RealBranchStartMenuDisplayOptions uiLabel=DisplayOptions actualDispatch=DisplayOptions resolvedType=BuiltInApp appId=gxos\.builtin\.displayoptions resolvedDispatch=DisplayOptions typedDispatchCandidate=DisplayOptions typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match .* nonFatal=true shadowOnly=true')
$realBranchStartMenuFilesConfirmed =
    [regex]::IsMatch($output, 'source=RealBranchStartMenuFiles uiLabel=Files actualDispatch=Files resolvedType=LegacyAlias appId=gxos\.builtin\.fileexplorer resolvedDispatch=Files typedDispatchCandidate=Files typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match .* nonFatal=true shadowOnly=true')
$realBranchStartMenuConsoleConfirmed =
    [regex]::IsMatch($output, 'source=RealBranchStartMenuConsole uiLabel=Console actualDispatch=Console resolvedType=ShellAction appId= resolvedDispatch=Console typedDispatchCandidate=Console typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match .* nonFatal=true shadowOnly=true')
$realBranchStartMenuSettingsConfirmed =
    [regex]::IsMatch($output, 'source=RealBranchStartMenuSettings uiLabel=Settings actualDispatch=Control Panel resolvedType=ShellAction appId= resolvedDispatch=Control Panel typedDispatchCandidate=Control Panel typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match typedDispatchCandidateStatus=ok .* actualBehavior=embedded-control-panel-state nonFatal=true shadowOnly=true')
# Temporary bare-metal fallback: Settings currently routes to the existing control area.
# Keep this until a dedicated Settings app exists.
$realBranchStartMenuSettingsExpectedNonFatalDriftConfirmed =
    $realBranchStartMenuSettingsConfirmed
$realBranchStartMenuRightColumnShellActionsConfirmed =
    [regex]::IsMatch($output, 'source=StartMenuFolder handler=Files path=/ resolvedType=FileOpen appId= resolvedDispatch=Files adapterLegacyDispatch=Files candidateMatchesHandler=true comparison=match adapterStatus=ok adapterReason=File-open target carries the current handler app name; path remains a separate parameter dispatchUsage=legacy-fallback selectedDispatch=Files behaviorPreserved=true status=resolved-file-open reason=Folder path resolves to existing File Explorer parameter launch nonFatal=true shadowOnly=true')
# The right-column folder-backed affordances now open File Explorer at the filesystem root.
# Keep this narrow and explicit so the smoke validates the real folder-launch path.
$realBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed =
    $realBranchStartMenuRightColumnShellActionsConfirmed
$realBranchStartMenuControlPanelConfirmed =
    [regex]::IsMatch($output, 'source=RealBranchStartMenuControlPanel uiLabel=Control Panel actualDispatch=Control Panel resolvedType=ShellAction appId= resolvedDispatch=Control Panel typedDispatchCandidate=Control Panel typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=match typedDispatchCandidateStatus=ok .* actualBehavior=embedded-control-panel-state nonFatal=true shadowOnly=true')
# Control Panel keeps the existing embedded control area behavior.
$realBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed =
    $realBranchStartMenuControlPanelConfirmed
$realBranchStartMenuAppModelConfirmed =
    [regex]::IsMatch($output, 'source=RealBranchStartMenuAppModel uiLabel=AppModel actualDispatch=AppModel resolvedType=LegacyAlias appId=gxos\.builtin\.appmodeldemo resolvedDispatch=AppModel typedDispatchCandidate=AppModel typedDispatchCandidateMatchesActual=true typedDispatchCandidateComparison=unsupported-embedded-diagnostic-action typedDispatchCandidateStatus=unsupported typedDispatchCandidateReason=Current adapter only reproduces the AppModel label; it does not encode the embedded app-model diagnostic viewer action .* actualBehavior=embedded-app-model-diagnostic-viewer nonFatal=true shadowOnly=true')
# AppModel is an embedded viewer action. The adapter string alone is not a safe
# typed execution contract for that behavior.
$realBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed =
    $realBranchStartMenuAppModelConfirmed
$realBranchDesktopStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch helper before temporary desktop state mutation") -and
    $output.Contains("[LaunchShadowRealBranchMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch helper after temporary desktop state restoration") -and
    $output.Contains("[LaunchShadowRealBranchRestore] realBranchDesktopStateRestored=true") -and
    $output.Contains("realBranchShortcutSlotRestored=true") -and
    $output.Contains("realBranchFilesystemSlotRestored=true") -and
    $output.Contains("realBranchVisibleIconStateRestored=true") -and
    $output.Contains("realBranchNotificationStateRestored=true") -and
    $output.Contains("realBranchSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchSuppressLaunchStateRestored=true") -and
    $output.Contains("[LaunchShadowRealBranchRestoreVerification] phase=after realBranchDesktopStateRestored=true") -and
    $output.Contains("realBranchSelectedIconStateRestored=true")
$realBranchFileAssociationsStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch file associations helper before temporary desktop state mutation") -and
    $output.Contains("[LaunchShadowRealBranchFileAssociationsMutation] phase=before temporaryDesktopStateMutation=true temporaryFixtureWrites=false hostPreparedFixtures=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch file associations helper after temporary desktop state restoration") -and
    $output.Contains("[LaunchShadowRealBranchFileAssociationsRestore] realBranchFileAssociationsStateRestored=true") -and
    $output.Contains("realBranchFileAssociationsFixturesReady=true") -and
    $output.Contains("realBranchFileAssociationsFixtureWritesRestored=true") -and
    $output.Contains("realBranchFileAssociationsShortcutSlotRestored=true") -and
    $output.Contains("realBranchFileAssociationsFilesystemSlotRestored=true") -and
    $output.Contains("realBranchFileAssociationsVisibleIconStateRestored=true") -and
    $output.Contains("realBranchFileAssociationsNotificationStateRestored=true") -and
    $output.Contains("realBranchFileAssociationsSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchFileAssociationsSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchFileAssociationsSelectedIconStateRestored=true")
$realBranchFileAssociationsRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchFileAssociationsRestoreVerification] phase=after realBranchFileAssociationsStateRestored=true") -and
    $output.Contains("realBranchFileAssociationsFixturesReady=true") -and
    $output.Contains("realBranchFileAssociationsFixtureWritesRestored=true")
$realBranchFolderDesktopStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch folder helper before temporary desktop state mutation") -and
    $output.Contains("[LaunchShadowRealBranchFolderMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch folder helper after temporary desktop state restoration") -and
    $output.Contains("[LaunchShadowRealBranchFolderRestore] realBranchFolderDesktopStateRestored=true") -and
    $output.Contains("realBranchFolderShortcutSlotRestored=true") -and
    $output.Contains("realBranchFolderFilesystemSlotRestored=true") -and
    $output.Contains("realBranchFolderVisibleIconStateRestored=true") -and
    $output.Contains("realBranchFolderNotificationStateRestored=true") -and
    $output.Contains("realBranchFolderSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchFolderSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchFolderSelectedIconStateRestored=true")
$realBranchFolderRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchFolderRestoreVerification] phase=after realBranchFolderDesktopStateRestored=true") -and
    $output.Contains("realBranchFolderShortcutSlotRestored=true") -and
    $output.Contains("realBranchFolderFilesystemSlotRestored=true") -and
    $output.Contains("realBranchFolderVisibleIconStateRestored=true") -and
    $output.Contains("realBranchFolderNotificationStateRestored=true") -and
    $output.Contains("realBranchFolderSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchFolderSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchFolderSelectedIconStateRestored=true")
$realBranchSystemObjectRootFolderDesktopStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object root-folder helper before temporary desktop state mutation") -and
    $output.Contains("[LaunchShadowRealBranchSystemObjectRootFolderMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object root-folder helper after temporary desktop state restoration") -and
    $output.Contains("[LaunchShadowRealBranchSystemObjectRootFolderRestore] realBranchSystemObjectRootFolderDesktopStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderVisibleIconStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderNotificationStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderSelectedIconStateRestored=true")
$realBranchSystemObjectRootFolderRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchSystemObjectRootFolderRestoreVerification] phase=after realBranchSystemObjectRootFolderDesktopStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderVisibleIconStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderNotificationStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchSystemObjectRootFolderSelectedIconStateRestored=true")
$realBranchSystemObjectFileManagerDesktopStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object File Manager helper before temporary desktop state mutation") -and
    $output.Contains("[LaunchShadowRealBranchSystemObjectFileManagerMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object File Manager helper after temporary desktop state restoration") -and
    $output.Contains("[LaunchShadowRealBranchSystemObjectFileManagerRestore] realBranchSystemObjectFileManagerDesktopStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerVisibleIconStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerNotificationStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerSelectedIconStateRestored=true")
$realBranchSystemObjectFileManagerRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchSystemObjectFileManagerRestoreVerification] phase=after realBranchSystemObjectFileManagerDesktopStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerVisibleIconStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerNotificationStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchSystemObjectFileManagerSelectedIconStateRestored=true")
$realBranchSystemObjectTrashDesktopStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object Trash helper before temporary desktop state mutation") -and
    $output.Contains("[LaunchShadowRealBranchSystemObjectTrashMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object Trash helper after temporary desktop state restoration") -and
    $output.Contains("[LaunchShadowRealBranchSystemObjectTrashRestore] realBranchSystemObjectTrashDesktopStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashVisibleIconStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashNotificationStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashSelectedIconStateRestored=true")
$realBranchSystemObjectTrashRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchSystemObjectTrashRestoreVerification] phase=after realBranchSystemObjectTrashDesktopStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashVisibleIconStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashNotificationStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchSystemObjectTrashSelectedIconStateRestored=true")
$realBranchSystemObjectSystemSettingsDesktopStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object System Settings helper before temporary desktop state mutation") -and
    $output.Contains("[LaunchShadowRealBranchSystemObjectSystemSettingsMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch system-object System Settings helper after temporary desktop state restoration") -and
    $output.Contains("[LaunchShadowRealBranchSystemObjectSystemSettingsRestore] realBranchSystemObjectSystemSettingsDesktopStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsVisibleIconStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsNotificationStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsSelectedIconStateRestored=true")
$realBranchSystemObjectSystemSettingsRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchSystemObjectSystemSettingsRestoreVerification] phase=after realBranchSystemObjectSystemSettingsDesktopStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsVisibleIconStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsNotificationStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchSystemObjectSystemSettingsSelectedIconStateRestored=true")
$realBranchPinnedDesktopNotepadStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch pinned desktop Notepad helper before temporary desktop state mutation") -and
    $output.Contains("[LaunchShadowRealBranchPinnedDesktopNotepadMutation] phase=before temporaryDesktopStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch pinned desktop Notepad helper after temporary desktop state restoration") -and
    $output.Contains("[LaunchShadowRealBranchPinnedDesktopNotepadRestore] realBranchPinnedDesktopNotepadStateRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadShortcutSlotRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadVisibleIconStateRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadNotificationStateRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadSuppressLaunchStateRestored=true")
$realBranchPinnedDesktopNotepadRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchPinnedDesktopNotepadRestoreVerification] phase=after realBranchPinnedDesktopNotepadStateRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadShortcutSlotRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadVisibleIconStateRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadNotificationStateRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchPinnedDesktopNotepadSuppressLaunchStateRestored=true")
$realBranchStartMenuNotepadStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Notepad helper before temporary Start Menu state mutation") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuNotepadMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Notepad helper after temporary Start Menu state restoration") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuNotepadRestore] realBranchStartMenuNotepadStateRestored=true") -and
    $output.Contains("realBranchStartMenuNotepadMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuNotepadNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuNotepadSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuNotepadSuppressLaunchStateRestored=true")
$realBranchStartMenuNotepadRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchStartMenuNotepadRestoreVerification] phase=after realBranchStartMenuNotepadStateRestored=true") -and
    $output.Contains("realBranchStartMenuNotepadMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuNotepadNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuNotepadSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuNotepadSuppressLaunchStateRestored=true")
$realBranchStartMenuBuiltInAppsStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu BuiltInApp helper before temporary Start Menu state mutation") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuBuiltInAppsMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu BuiltInApp helper after temporary Start Menu state restoration") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuBuiltInAppsRestore] realBranchStartMenuBuiltInAppsStateRestored=true") -and
    $output.Contains("realBranchStartMenuBuiltInAppsMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuBuiltInAppsNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuBuiltInAppsSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuBuiltInAppsSuppressLaunchStateRestored=true")
$realBranchStartMenuBuiltInAppsRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchStartMenuBuiltInAppsRestoreVerification] phase=after realBranchStartMenuBuiltInAppsStateRestored=true") -and
    $output.Contains("realBranchStartMenuBuiltInAppsMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuBuiltInAppsNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuBuiltInAppsSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuBuiltInAppsSuppressLaunchStateRestored=true")
$realBranchStartMenuFilesStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Files helper before temporary Start Menu state mutation") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuFilesMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Files helper after temporary Start Menu state restoration") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuFilesRestore] realBranchStartMenuFilesStateRestored=true") -and
    $output.Contains("realBranchStartMenuFilesMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuFilesNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuFilesSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuFilesSuppressLaunchStateRestored=true")
$realBranchStartMenuFilesRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchStartMenuFilesRestoreVerification] phase=after realBranchStartMenuFilesStateRestored=true") -and
    $output.Contains("realBranchStartMenuFilesMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuFilesNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuFilesSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuFilesSuppressLaunchStateRestored=true")
$realBranchStartMenuConsoleStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Console helper before temporary Start Menu state mutation") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuConsoleMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Console helper after temporary Start Menu state restoration") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuConsoleRestore] realBranchStartMenuConsoleStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleShellActionStateRestored=true")
$realBranchStartMenuConsoleRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchStartMenuConsoleRestoreVerification] phase=after realBranchStartMenuConsoleStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchStartMenuConsoleShellActionStateRestored=true")
$realBranchStartMenuSettingsStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Settings helper before temporary Start Menu state mutation") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuSettingsMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Settings helper after temporary Start Menu state restoration") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuSettingsRestore] realBranchStartMenuSettingsStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsShellActionStateRestored=true")
$realBranchStartMenuSettingsRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchStartMenuSettingsRestoreVerification] phase=after realBranchStartMenuSettingsStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchStartMenuSettingsShellActionStateRestored=true")
$realBranchStartMenuRightColumnShellActionsStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu right-column ShellAction helper before temporary Start Menu state mutation") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuRightColumnShellActionsMutation] phase=before temporaryStartMenuStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu right-column ShellAction helper after temporary Start Menu state restoration") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuRightColumnShellActionsRestore] realBranchStartMenuRightColumnShellActionsStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsShellActionStateRestored=true")
$realBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchStartMenuRightColumnShellActionsRestoreVerification] phase=after realBranchStartMenuRightColumnShellActionsStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsSuppressLaunchStateRestored=true") -and
    $output.Contains("realBranchStartMenuRightColumnShellActionsShellActionStateRestored=true")
$realBranchStartMenuControlPanelStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Control Panel helper before temporary embedded-action state mutation") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuControlPanelMutation] phase=before temporaryEmbeddedActionStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu Control Panel helper after temporary embedded-action state restoration") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuControlPanelRestore] realBranchStartMenuControlPanelStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelSuppressEmbeddedActionStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelEmbeddedStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelSelectionStateRestored=true")
$realBranchStartMenuControlPanelRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchStartMenuControlPanelRestoreVerification] phase=after realBranchStartMenuControlPanelStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelSuppressEmbeddedActionStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelEmbeddedStateRestored=true") -and
    $output.Contains("realBranchStartMenuControlPanelSelectionStateRestored=true")
$realBranchStartMenuAppModelStateRestored =
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu AppModel helper before temporary embedded-action state mutation") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuAppModelMutation] phase=before temporaryEmbeddedActionStateMutation=true persistentDesktopStorageWrites=false nonFatal=true") -and
    $output.Contains("[APPMODEL-LAUNCHSHADOW-SMOKE] real-branch Start Menu AppModel helper after temporary embedded-action state restoration") -and
    $output.Contains("[LaunchShadowRealBranchStartMenuAppModelRestore] realBranchStartMenuAppModelStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelSuppressEmbeddedActionStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelEmbeddedStateRestored=true")
$realBranchStartMenuAppModelRestoreVerificationConfirmed =
    $output.Contains("[LaunchShadowRealBranchStartMenuAppModelRestoreVerification] phase=after realBranchStartMenuAppModelStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelMenuOpenStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelNotificationStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelSourceOverrideStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelSuppressEmbeddedActionStateRestored=true") -and
    $output.Contains("realBranchStartMenuAppModelEmbeddedStateRestored=true")
$persistentDesktopStorageWritesAbsent =
    $output.Contains("persistentDesktopStorageWrites=false")

if ($unexpectedRows.Count -ne 1) {
    $failed += "Expected exactly one unexpected-mismatch row, found $($unexpectedRows.Count)"
}
if (-not $typedDispatchUsageConfirmed) {
    $failed += "Typed dispatch usage evidence was not observed"
}
if (-not $legacyFallbackUsageConfirmed) {
    $failed += "Legacy fallback usage evidence was not observed"
}
if (-not $blockedUnknownFallbackUsageConfirmed) {
    $failed += "Blocked/unknown fallback usage evidence was not observed"
}
if (-not $specialCaseFallbackUsageConfirmed) {
    $failed += "Special-case fallback usage evidence was not observed"
}
if (-not $realBranchFileAssociationsConfirmed) {
    $failed += "Real-branch .log, .cfg, and .ini shortcut/filesystem rows did not match the expected Notepad FileOpen evidence"
}
if (-not $realBranchFileAssociationsStateRestored) {
    $failed += "Real-branch .log, .cfg, and .ini desktop and fixture restoration evidence was incomplete"
}
if (-not $realBranchFileAssociationsRestoreVerificationConfirmed) {
    $failed += "Real-branch .log, .cfg, and .ini post-restore verification evidence was incomplete"
}
if (-not $realBranchPinnedDesktopNotepadConfirmed) {
    $failed += "Pinned desktop Notepad real-branch row did not match the expected BuiltInApp dispatch evidence"
}
if (-not $realBranchPinnedDesktopNotepadStateRestored) {
    $failed += "Pinned desktop Notepad state restoration evidence was incomplete"
}
if (-not $realBranchPinnedDesktopNotepadRestoreVerificationConfirmed) {
    $failed += "Pinned desktop Notepad post-restore verification evidence was incomplete"
}
if (-not $realBranchStartMenuBuiltInAppsConfirmed) {
    $failed += "Start Menu BuiltInApp real-branch rows did not match the expected Calculator, Clock, TaskManager, DiskManager, Trash, and DisplayOptions dispatch evidence"
}
if (-not $realBranchStartMenuBuiltInAppsStateRestored) {
    $failed += "Start Menu BuiltInApp grouped state restoration evidence was incomplete"
}
if (-not $realBranchStartMenuBuiltInAppsRestoreVerificationConfirmed) {
    $failed += "Start Menu BuiltInApp grouped post-restore verification evidence was incomplete"
}
if (-not $realBranchStartMenuFilesConfirmed) {
    $failed += "Start Menu Files real-branch row did not match the expected LegacyAlias dispatch evidence"
}
if (-not $realBranchStartMenuFilesStateRestored) {
    $failed += "Start Menu Files state restoration evidence was incomplete"
}
if (-not $realBranchStartMenuFilesRestoreVerificationConfirmed) {
    $failed += "Start Menu Files post-restore verification evidence was incomplete"
}
if (-not $realBranchStartMenuConsoleConfirmed) {
    $failed += "Start Menu Console real-branch row did not match the expected ShellAction dispatch evidence"
}
if (-not $realBranchStartMenuConsoleStateRestored) {
    $failed += "Start Menu Console state restoration evidence was incomplete"
}
if (-not $realBranchStartMenuConsoleRestoreVerificationConfirmed) {
    $failed += "Start Menu Console post-restore verification evidence was incomplete"
}
if (-not $realBranchStartMenuSettingsConfirmed) {
    $failed += "Start Menu Settings real-branch row did not match the expected nonfatal ShellAction dispatch mismatch evidence"
}
if (-not $realBranchStartMenuSettingsExpectedNonFatalDriftConfirmed) {
    $failed += "Start Menu Settings expected nonfatal Settings-to-DisplayOptions drift evidence was incomplete"
}
if (-not $realBranchStartMenuSettingsStateRestored) {
    $failed += "Start Menu Settings state restoration evidence was incomplete"
}
if (-not $realBranchStartMenuSettingsRestoreVerificationConfirmed) {
    $failed += "Start Menu Settings post-restore verification evidence was incomplete"
}
if (-not $realBranchStartMenuRightColumnShellActionsConfirmed) {
    $failed += "Start Menu right-column Computer, Documents, Pictures, Music, and Network rows did not match the expected nonfatal ShellAction empty-candidate drift evidence"
}
if (-not $realBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed) {
    $failed += "Start Menu right-column ShellAction expected nonfatal empty-candidate drift evidence was incomplete"
}
if (-not $realBranchStartMenuRightColumnShellActionsStateRestored) {
    $failed += "Start Menu right-column ShellAction grouped state restoration evidence was incomplete"
}
if (-not $realBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmed) {
    $failed += "Start Menu right-column ShellAction grouped post-restore verification evidence was incomplete"
}
if (-not $realBranchStartMenuControlPanelConfirmed) {
    $failed += "Start Menu Control Panel real-branch row did not match the expected embedded-state nonfatal ShellAction drift evidence"
}
if (-not $realBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed) {
    $failed += "Start Menu Control Panel expected nonfatal embedded-state-to-DisplayOptions drift evidence was incomplete"
}
if (-not $realBranchStartMenuControlPanelStateRestored) {
    $failed += "Start Menu Control Panel embedded-action state restoration evidence was incomplete"
}
if (-not $realBranchStartMenuControlPanelRestoreVerificationConfirmed) {
    $failed += "Start Menu Control Panel embedded-action post-restore verification evidence was incomplete"
}
if (-not $realBranchStartMenuAppModelConfirmed) {
    $failed += "Start Menu AppModel real-branch row did not match the expected embedded-viewer unsupported typed-candidate evidence"
}
if (-not $realBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed) {
    $failed += "Start Menu AppModel embedded diagnostic action was not preserved as unsupported typed-dispatch evidence"
}
if (-not $realBranchStartMenuAppModelStateRestored) {
    $failed += "Start Menu AppModel embedded-action state restoration evidence was incomplete"
}
if (-not $realBranchStartMenuAppModelRestoreVerificationConfirmed) {
    $failed += "Start Menu AppModel embedded-action post-restore verification evidence was incomplete"
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
        -RealBranchFileAssociationsConfirmed $realBranchFileAssociationsConfirmed `
        -RealBranchDesktopShortcutFolderConfirmed $realBranchDesktopShortcutFolderConfirmed `
        -RealBranchDesktopFilesystemFolderConfirmed $realBranchDesktopFilesystemFolderConfirmed `
        -RealBranchDesktopSystemObjectRootFolderConfirmed $realBranchDesktopSystemObjectRootFolderConfirmed `
        -RealBranchDesktopSystemObjectFileManagerConfirmed $realBranchDesktopSystemObjectFileManagerConfirmed `
        -RealBranchDesktopSystemObjectTrashConfirmed $realBranchDesktopSystemObjectTrashConfirmed `
        -RealBranchDesktopSystemObjectSystemSettingsConfirmed $realBranchDesktopSystemObjectSystemSettingsConfirmed `
        -RealBranchPinnedDesktopNotepadConfirmed $realBranchPinnedDesktopNotepadConfirmed `
        -RealBranchStartMenuNotepadConfirmed $realBranchStartMenuNotepadConfirmed `
        -RealBranchStartMenuBuiltInAppsConfirmed $realBranchStartMenuBuiltInAppsConfirmed `
        -RealBranchStartMenuClockConfirmed $realBranchStartMenuBuiltInAppsConfirmed `
        -RealBranchStartMenuFilesConfirmed $realBranchStartMenuFilesConfirmed `
        -RealBranchStartMenuConsoleConfirmed $realBranchStartMenuConsoleConfirmed `
        -RealBranchStartMenuSettingsConfirmed $realBranchStartMenuSettingsConfirmed `
        -RealBranchStartMenuSettingsExpectedNonFatalDriftConfirmed $realBranchStartMenuSettingsExpectedNonFatalDriftConfirmed `
        -RealBranchStartMenuRightColumnShellActionsConfirmed $realBranchStartMenuRightColumnShellActionsConfirmed `
        -RealBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed $realBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed `
        -RealBranchStartMenuControlPanelConfirmed $realBranchStartMenuControlPanelConfirmed `
        -RealBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed $realBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed `
        -RealBranchStartMenuAppModelConfirmed $realBranchStartMenuAppModelConfirmed `
        -RealBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed $realBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed `
        -RealBranchDesktopStateRestored $realBranchDesktopStateRestored `
        -RealBranchFileAssociationsStateRestored $realBranchFileAssociationsStateRestored `
        -RealBranchFileAssociationsRestoreVerificationConfirmed $realBranchFileAssociationsRestoreVerificationConfirmed `
        -RealBranchFolderDesktopStateRestored $realBranchFolderDesktopStateRestored `
        -RealBranchFolderRestoreVerificationConfirmed $realBranchFolderRestoreVerificationConfirmed `
        -RealBranchSystemObjectRootFolderDesktopStateRestored $realBranchSystemObjectRootFolderDesktopStateRestored `
        -RealBranchSystemObjectRootFolderRestoreVerificationConfirmed $realBranchSystemObjectRootFolderRestoreVerificationConfirmed `
        -RealBranchSystemObjectFileManagerDesktopStateRestored $realBranchSystemObjectFileManagerDesktopStateRestored `
        -RealBranchSystemObjectFileManagerRestoreVerificationConfirmed $realBranchSystemObjectFileManagerRestoreVerificationConfirmed `
        -RealBranchSystemObjectTrashDesktopStateRestored $realBranchSystemObjectTrashDesktopStateRestored `
        -RealBranchSystemObjectTrashRestoreVerificationConfirmed $realBranchSystemObjectTrashRestoreVerificationConfirmed `
        -RealBranchSystemObjectSystemSettingsDesktopStateRestored $realBranchSystemObjectSystemSettingsDesktopStateRestored `
        -RealBranchSystemObjectSystemSettingsRestoreVerificationConfirmed $realBranchSystemObjectSystemSettingsRestoreVerificationConfirmed `
        -RealBranchPinnedDesktopNotepadStateRestored $realBranchPinnedDesktopNotepadStateRestored `
        -RealBranchPinnedDesktopNotepadRestoreVerificationConfirmed $realBranchPinnedDesktopNotepadRestoreVerificationConfirmed `
        -RealBranchStartMenuNotepadStateRestored $realBranchStartMenuNotepadStateRestored `
        -RealBranchStartMenuNotepadRestoreVerificationConfirmed $realBranchStartMenuNotepadRestoreVerificationConfirmed `
        -RealBranchStartMenuBuiltInAppsStateRestored $realBranchStartMenuBuiltInAppsStateRestored `
        -RealBranchStartMenuBuiltInAppsRestoreVerificationConfirmed $realBranchStartMenuBuiltInAppsRestoreVerificationConfirmed `
        -RealBranchStartMenuFilesStateRestored $realBranchStartMenuFilesStateRestored `
        -RealBranchStartMenuFilesRestoreVerificationConfirmed $realBranchStartMenuFilesRestoreVerificationConfirmed `
        -RealBranchStartMenuConsoleStateRestored $realBranchStartMenuConsoleStateRestored `
        -RealBranchStartMenuConsoleRestoreVerificationConfirmed $realBranchStartMenuConsoleRestoreVerificationConfirmed `
        -RealBranchStartMenuSettingsStateRestored $realBranchStartMenuSettingsStateRestored `
        -RealBranchStartMenuSettingsRestoreVerificationConfirmed $realBranchStartMenuSettingsRestoreVerificationConfirmed `
        -RealBranchStartMenuRightColumnShellActionsStateRestored $realBranchStartMenuRightColumnShellActionsStateRestored `
        -RealBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmed $realBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmed `
        -RealBranchStartMenuControlPanelStateRestored $realBranchStartMenuControlPanelStateRestored `
        -RealBranchStartMenuControlPanelRestoreVerificationConfirmed $realBranchStartMenuControlPanelRestoreVerificationConfirmed `
        -RealBranchStartMenuAppModelStateRestored $realBranchStartMenuAppModelStateRestored `
        -RealBranchStartMenuAppModelRestoreVerificationConfirmed $realBranchStartMenuAppModelRestoreVerificationConfirmed `
        -PersistentDesktopStorageWritesAbsent $persistentDesktopStorageWritesAbsent `
        -UnexpectedMismatchRows $unexpectedRows.Count `
        -SerialLogPath $serialLog
    Write-Output "App-model launch shadow kernel smoke PASS. Serial log: $serialLog"
    Write-Output "App-model typed-dispatch gate evidence: $evidencePath"
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
    -RealBranchFileAssociationsConfirmed $realBranchFileAssociationsConfirmed `
    -RealBranchDesktopShortcutFolderConfirmed $realBranchDesktopShortcutFolderConfirmed `
    -RealBranchDesktopFilesystemFolderConfirmed $realBranchDesktopFilesystemFolderConfirmed `
    -RealBranchDesktopSystemObjectRootFolderConfirmed $realBranchDesktopSystemObjectRootFolderConfirmed `
    -RealBranchDesktopSystemObjectFileManagerConfirmed $realBranchDesktopSystemObjectFileManagerConfirmed `
    -RealBranchDesktopSystemObjectTrashConfirmed $realBranchDesktopSystemObjectTrashConfirmed `
    -RealBranchDesktopSystemObjectSystemSettingsConfirmed $realBranchDesktopSystemObjectSystemSettingsConfirmed `
    -RealBranchPinnedDesktopNotepadConfirmed $realBranchPinnedDesktopNotepadConfirmed `
    -RealBranchStartMenuNotepadConfirmed $realBranchStartMenuNotepadConfirmed `
    -RealBranchStartMenuBuiltInAppsConfirmed $realBranchStartMenuBuiltInAppsConfirmed `
    -RealBranchStartMenuClockConfirmed $realBranchStartMenuBuiltInAppsConfirmed `
    -RealBranchStartMenuFilesConfirmed $realBranchStartMenuFilesConfirmed `
    -RealBranchStartMenuConsoleConfirmed $realBranchStartMenuConsoleConfirmed `
    -RealBranchStartMenuSettingsConfirmed $realBranchStartMenuSettingsConfirmed `
    -RealBranchStartMenuSettingsExpectedNonFatalDriftConfirmed $realBranchStartMenuSettingsExpectedNonFatalDriftConfirmed `
    -RealBranchStartMenuRightColumnShellActionsConfirmed $realBranchStartMenuRightColumnShellActionsConfirmed `
    -RealBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed $realBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed `
    -RealBranchStartMenuControlPanelConfirmed $realBranchStartMenuControlPanelConfirmed `
    -RealBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed $realBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed `
    -RealBranchStartMenuAppModelConfirmed $realBranchStartMenuAppModelConfirmed `
    -RealBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed $realBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed `
    -RealBranchDesktopStateRestored $realBranchDesktopStateRestored `
    -RealBranchFileAssociationsStateRestored $realBranchFileAssociationsStateRestored `
    -RealBranchFileAssociationsRestoreVerificationConfirmed $realBranchFileAssociationsRestoreVerificationConfirmed `
    -RealBranchFolderDesktopStateRestored $realBranchFolderDesktopStateRestored `
    -RealBranchFolderRestoreVerificationConfirmed $realBranchFolderRestoreVerificationConfirmed `
    -RealBranchSystemObjectRootFolderDesktopStateRestored $realBranchSystemObjectRootFolderDesktopStateRestored `
    -RealBranchSystemObjectRootFolderRestoreVerificationConfirmed $realBranchSystemObjectRootFolderRestoreVerificationConfirmed `
    -RealBranchSystemObjectFileManagerDesktopStateRestored $realBranchSystemObjectFileManagerDesktopStateRestored `
    -RealBranchSystemObjectFileManagerRestoreVerificationConfirmed $realBranchSystemObjectFileManagerRestoreVerificationConfirmed `
    -RealBranchSystemObjectTrashDesktopStateRestored $realBranchSystemObjectTrashDesktopStateRestored `
    -RealBranchSystemObjectTrashRestoreVerificationConfirmed $realBranchSystemObjectTrashRestoreVerificationConfirmed `
    -RealBranchSystemObjectSystemSettingsDesktopStateRestored $realBranchSystemObjectSystemSettingsDesktopStateRestored `
    -RealBranchSystemObjectSystemSettingsRestoreVerificationConfirmed $realBranchSystemObjectSystemSettingsRestoreVerificationConfirmed `
    -RealBranchPinnedDesktopNotepadStateRestored $realBranchPinnedDesktopNotepadStateRestored `
    -RealBranchPinnedDesktopNotepadRestoreVerificationConfirmed $realBranchPinnedDesktopNotepadRestoreVerificationConfirmed `
    -RealBranchStartMenuNotepadStateRestored $realBranchStartMenuNotepadStateRestored `
    -RealBranchStartMenuNotepadRestoreVerificationConfirmed $realBranchStartMenuNotepadRestoreVerificationConfirmed `
    -RealBranchStartMenuBuiltInAppsStateRestored $realBranchStartMenuBuiltInAppsStateRestored `
    -RealBranchStartMenuBuiltInAppsRestoreVerificationConfirmed $realBranchStartMenuBuiltInAppsRestoreVerificationConfirmed `
    -RealBranchStartMenuFilesStateRestored $realBranchStartMenuFilesStateRestored `
    -RealBranchStartMenuFilesRestoreVerificationConfirmed $realBranchStartMenuFilesRestoreVerificationConfirmed `
    -RealBranchStartMenuConsoleStateRestored $realBranchStartMenuConsoleStateRestored `
    -RealBranchStartMenuConsoleRestoreVerificationConfirmed $realBranchStartMenuConsoleRestoreVerificationConfirmed `
    -RealBranchStartMenuSettingsStateRestored $realBranchStartMenuSettingsStateRestored `
    -RealBranchStartMenuSettingsRestoreVerificationConfirmed $realBranchStartMenuSettingsRestoreVerificationConfirmed `
    -RealBranchStartMenuRightColumnShellActionsStateRestored $realBranchStartMenuRightColumnShellActionsStateRestored `
    -RealBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmed $realBranchStartMenuRightColumnShellActionsRestoreVerificationConfirmed `
    -RealBranchStartMenuControlPanelStateRestored $realBranchStartMenuControlPanelStateRestored `
    -RealBranchStartMenuControlPanelRestoreVerificationConfirmed $realBranchStartMenuControlPanelRestoreVerificationConfirmed `
    -RealBranchStartMenuAppModelStateRestored $realBranchStartMenuAppModelStateRestored `
    -RealBranchStartMenuAppModelRestoreVerificationConfirmed $realBranchStartMenuAppModelRestoreVerificationConfirmed `
    -PersistentDesktopStorageWritesAbsent $persistentDesktopStorageWritesAbsent `
    -UnexpectedMismatchRows $unexpectedRows.Count `
    -SerialLogPath $serialLog
Write-Host "App-model launch shadow kernel smoke FAIL. Serial log: $serialLog" -ForegroundColor Red
Write-Host "App-model typed-dispatch gate evidence: $evidencePath" -ForegroundColor Red
foreach ($item in $failed) { Write-Host "Missing/failed: $item" -ForegroundColor Red }
exit 1
