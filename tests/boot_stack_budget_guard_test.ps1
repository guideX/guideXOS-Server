param(
    [Parameter(Mandatory = $true)]
    [string]$StackUsageRoot
)

$ErrorActionPreference = 'Stop'
$stackCapacity = 16384
$interruptReserve = 512
$bootEntryReserve = 40
$unmodeledCallReserve = 1024
$requiredHeadroom = 4096
$guardedPathLimit = $stackCapacity - $requiredHeadroom

if (-not (Test-Path -LiteralPath $StackUsageRoot -PathType Container)) {
    throw "Stack usage directory does not exist: $StackUsageRoot"
}

$records = @()
foreach ($file in Get-ChildItem -LiteralPath $StackUsageRoot -Filter '*.su' -File -Recurse) {
    foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
        $parts = $line -split "`t"
        if ($parts.Count -lt 2) { continue }
        $locationMatch = [regex]::Match($parts[0], '^(.*):(\d+):(\d+):(.+)$')
        if (-not $locationMatch.Success) { continue }
        $bytesMatch = [regex]::Match($parts[1], '^\s*(\d+)(?:\s|$)')
        if (-not $bytesMatch.Success) { continue }
        $records += [PSCustomObject]@{
            Function = $locationMatch.Groups[4].Value
            Bytes = [int]$bytesMatch.Groups[1].Value
            Source = $file.Name
            SourcePath = $locationMatch.Groups[1].Value
            SourceLine = [int]$locationMatch.Groups[2].Value
        }
    }
}

if ($records.Count -eq 0) { throw "No GCC .su records found under $StackUsageRoot" }

function Get-Frame([string]$Pattern) {
    $matches = @($script:records | Where-Object { $_.Function -match $Pattern })
    if ($matches.Count -eq 0) { throw "Required compiler stack record is missing: $Pattern" }
    # GCC may emit both a body and a small optimized clone with the same source
    # signature. Use the larger actual record for the guarded path.
    return [int](($matches | Measure-Object -Property Bytes -Maximum).Maximum)
}

function Get-FrameAtSourceLine([string]$SourceName, [int]$SourceLine) {
    $matches = @($script:records | Where-Object {
        $_.SourcePath -match [regex]::Escape($SourceName) -and
        $_.SourceLine -eq $SourceLine
    })
    if ($matches.Count -eq 0) {
        throw "Required compiler stack record is missing at ${SourceName}:${SourceLine}."
    }
    return [int](($matches | Measure-Object -Property Bytes -Maximum).Maximum)
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$taskAddressSpaceSource = [IO.File]::ReadAllLines(
    (Join-Path $repoRoot 'kernel\core\task_address_space.cpp'))
$walkDeclaration = -1
for ($i = 0; $i -lt $taskAddressSpaceSource.Length; ++$i) {
    if ($taskAddressSpaceSource[$i] -match '^\s*static\s+AddressSpaceStatus\s+walk_private_tree\(') {
        $walkDeclaration = $i + 1
        break
    }
}
if ($walkDeclaration -lt 1) { throw 'Could not locate the private page-table walk declaration.' }

$frame = [ordered]@{
    kernelMain = Get-Frame 'kernel_main\(void\*, uint32_t\)'
    desktopMouse = Get-Frame 'kernel::desktop::handle_mouse\('
    desktopTick = Get-Frame 'kernel::desktop::tick\('
    desktopLaunch = Get-Frame 'kernel::desktop::launch_app\('
    appManagerUpdate = Get-Frame 'kernel::app::AppManager::update\('
    appManagerLaunch = Get-Frame 'kernel::app::AppManager::launchApp\('
    settingsInit = Get-Frame 'SettingsNativeApp::init\('
    settingsMouse = Get-Frame 'SettingsNativeApp::onMouseDown\('
    settingsUpdate = Get-Frame 'SettingsNativeApp::update\('
    settingsPersonalizationActivate = Get-Frame 'SettingsNativeApp::activateFocusedPersonalizationControl\('
    settingsPersonalizationClick = Get-Frame 'SettingsNativeApp::handlePersonalizationClick\('
    settingsApplyWallpaper = Get-Frame 'SettingsNativeApp::applyPendingBackground\('
    settingsApplyDesktopIcons = Get-Frame 'SettingsNativeApp::applyPendingDesktopIcons\('
    settingsRefresh = Get-Frame 'settings_native::refreshDisplaySnapshot\('
    settingsProviderCallback = Get-Frame 'SettingsNativeApp::readProviderSnapshot\('
    settingsApplyWallpaperProvider = Get-Frame 'settings_native::applyBuiltInWallpaper\('
    settingsApplyIconsProvider = Get-Frame 'settings_native::applyDesktopIconVisibility\('
    desktopApplyWallpaper = Get-Frame 'kernel::desktop::apply_builtin_background\('
    desktopApplyIcons = Get-Frame 'kernel::desktop::apply_system_icon_visibility\('
    persistWallpaper = Get-Frame 'kernel::desktop::persistBuiltInBackgroundId\('
    persistDesktopIcons = Get-Frame 'kernel::desktop::persistDesktopIconVisibility\('
    appDataWriteAndVerify = Get-Frame '::writeAndVerify\('
    appDataReadConfig = Get-Frame '::readAppDataConfig\('
    readDisplaySnapshot = Get-Frame 'settings_native::readDisplaySnapshot\('
    displaySubmit = Get-Frame 'DisplayConfigurationService::submit\('
    compositorMouseDown = Get-Frame 'KernelCompositor::handleMouseDown\('
    bridgePoll = Get-Frame 'system_service_bridge::poll\('
    bridgeServe = Get-Frame 'system_service_bridge::.*serveRequest\('
    readDeviceSnapshot = Get-Frame 'settings_inventory_provider::readDeviceSnapshot\('
    readStorageSnapshot = Get-Frame 'settings_inventory_provider::readStorageSnapshot\('
    readMbr = Get-Frame 'settings_inventory_provider::.*readMbr\('
    networkReadSnapshot = Get-Frame 'network_settings_provider::readSnapshot\('
    blockReadSectors = Get-Frame 'kernel::block::Status kernel::block::read_sectors\('
    decodeDeviceSnapshot = Get-Frame 'system_service::decodeDeviceSnapshot\('
    decodeStorageSnapshot = Get-Frame 'system_service::decodeStorageSnapshot\('
    validateDeviceSnapshot = Get-Frame 'settings_inventory::validDeviceSnapshot\('
    validateStorageSnapshot = Get-Frame 'settings_inventory::validStorageSnapshot\('
    displayOptionsInit = Get-Frame 'DisplayOptionsApp::init\('
    displayOptionsLoad = Get-Frame 'DisplayOptionsApp::loadSelection\('
    displayOptionsQuery = Get-Frame 'DisplayOptionsApp::queryDisplayConfiguration\('
    displayOptionsPendingQuery = Get-Frame 'DisplayOptionsApp::queryPendingTopologyChange\('
    displayOptionsRefresh = Get-Frame 'DisplayOptionsApp::refreshPendingTopologyChange\('
    displayOptionsSubmit = Get-Frame 'DisplayOptionsApp::submitDisplayConfiguration\('
    displayOptionsMouse = Get-Frame 'DisplayOptionsApp::onMouseDown\('
    exceptionDispatch = Get-Frame 'gxos_native_exception_dispatch\('
    recordUserFault = Get-Frame 'gxos_amd64_record_user_fault\('
    runOneProtectedTask = Get-Frame '::run_one_cycle\('
    runFaultRecoveryCycles = Get-Frame '::run_fault_recovery_cycles\('
    reapFaultedTask = Get-Frame '::cleanup_task\('
    destroyPrivateAddressSpace = Get-Frame 'destroy_task_address_space\('
    privatePageTableWalk = Get-FrameAtSourceLine 'task_address_space.cpp' $walkDeclaration
    releasePrivatePageTables = Get-Frame 'release_private_tables\('
}

$settingsLaunchPath = $frame.kernelMain + $frame.desktopMouse + $frame.desktopLaunch +
    $frame.appManagerLaunch + $frame.settingsInit + $frame.settingsRefresh +
    $frame.settingsProviderCallback + $frame.readDisplaySnapshot + $frame.displaySubmit
$settingsNavigationPath = $frame.kernelMain + $frame.desktopMouse +
    $frame.compositorMouseDown + $frame.settingsMouse + $frame.settingsRefresh +
    $frame.settingsProviderCallback + $frame.readDisplaySnapshot + $frame.displaySubmit
$settingsApplyBasePath = $frame.kernelMain + $frame.desktopTick + $frame.appManagerUpdate +
    $frame.settingsUpdate + $frame.settingsPersonalizationActivate +
    $frame.settingsPersonalizationClick
$settingsWallpaperApplyPath = $settingsApplyBasePath + $frame.settingsApplyWallpaper +
    $frame.settingsApplyWallpaperProvider + $frame.desktopApplyWallpaper +
    $frame.persistWallpaper + $frame.appDataWriteAndVerify + $frame.appDataReadConfig
$settingsIconApplyPath = $settingsApplyBasePath + $frame.settingsApplyDesktopIcons +
    $frame.settingsApplyIconsProvider + $frame.desktopApplyIcons +
    $frame.persistDesktopIcons + $frame.appDataWriteAndVerify + $frame.appDataReadConfig
$settingsWorst = [Math]::Max($settingsLaunchPath, $settingsNavigationPath) +
    $unmodeledCallReserve + $interruptReserve + $bootEntryReserve
$settingsPersistenceWorst = (@($settingsWallpaperApplyPath, $settingsIconApplyPath) |
    Measure-Object -Maximum).Maximum + $unmodeledCallReserve + $interruptReserve + $bootEntryReserve
$legacyDisplayInitPath = $frame.kernelMain + $frame.desktopMouse + $frame.desktopLaunch +
    $frame.appManagerLaunch + $frame.displayOptionsInit + $frame.displayOptionsLoad +
    $frame.displayOptionsQuery + $frame.displaySubmit
$legacyDisplayQueryPath = $frame.kernelMain + $frame.desktopMouse +
    $frame.compositorMouseDown + $frame.displayOptionsMouse + $frame.displayOptionsQuery +
    $frame.displaySubmit
$legacyDisplayApplyPath = $frame.kernelMain + $frame.desktopMouse +
    $frame.compositorMouseDown + $frame.displayOptionsMouse + $frame.displayOptionsSubmit +
    $frame.displaySubmit
$legacyDisplayRefreshPath = $frame.kernelMain + $frame.desktopMouse +
    $frame.compositorMouseDown + $frame.displayOptionsMouse + $frame.displayOptionsRefresh +
    $frame.displayOptionsPendingQuery + $frame.displaySubmit
$legacyDisplayWorst = (@($legacyDisplayInitPath, $legacyDisplayQueryPath,
    $legacyDisplayApplyPath, $legacyDisplayRefreshPath) | Measure-Object -Maximum).Maximum +
    $unmodeledCallReserve + $interruptReserve + $bootEntryReserve

$com2StorageDispatch = $frame.kernelMain + $frame.bridgePoll + $frame.bridgeServe +
    $frame.readStorageSnapshot + $frame.readMbr + $frame.blockReadSectors +
    $frame.validateStorageSnapshot + $unmodeledCallReserve + $interruptReserve + $bootEntryReserve
$com2StorageDecode = $frame.kernelMain + $frame.bridgePoll + $frame.bridgeServe +
    $frame.decodeStorageSnapshot + $frame.validateStorageSnapshot + $unmodeledCallReserve +
    $interruptReserve + $bootEntryReserve
$com2DeviceDispatch = $frame.kernelMain + $frame.bridgePoll + $frame.bridgeServe +
    $frame.readDeviceSnapshot + $frame.networkReadSnapshot + $frame.validateDeviceSnapshot +
    $unmodeledCallReserve + $interruptReserve + $bootEntryReserve
$com2DeviceDecode = $frame.kernelMain + $frame.bridgePoll + $frame.bridgeServe +
    $frame.decodeDeviceSnapshot + $frame.validateDeviceSnapshot + $unmodeledCallReserve +
    $interruptReserve + $bootEntryReserve
$com2Worst = (@($com2StorageDispatch, $com2StorageDecode, $com2DeviceDispatch,
    $com2DeviceDecode) | Measure-Object -Maximum).Maximum

# Fault diagnostics use separate static kernel stacks. The privilege-entry
# stack holds 15 saved GPRs, the normalized vector/error pair, and the CPL3
# processor frame (176 bytes for error-code and no-error-code exceptions).
# Dispatch then moves to the independent 16 KiB exception stack. Its C++ frame
# calls the strict task recorder synchronously after reserving Win64 shadow
# space and a return address.
$privilegeEntryBytes = 15 * 8 + 2 * 8 + 5 * 8
$exceptionDispatchStack = 40 + $frame.exceptionDispatch + $frame.recordUserFault

# The lifecycle stack includes a 40-byte callback call frame. The user-entry
# routine adds eight saved nonvolatile registers and a return address (72
# bytes) while the user task is live. The continuation pops that frame before
# cleanup. Address-space walks recurse through at most three private levels;
# validation, invalidation, and release are sequential, so the deepest single
# walk bounds the destruction path.
$lifecycleEntryPath = 40 + $frame.runFaultRecoveryCycles + $frame.runOneProtectedTask +
    72 + $unmodeledCallReserve + $interruptReserve + $bootEntryReserve
$lifecycleReaperPath = 40 + $frame.runFaultRecoveryCycles + $frame.runOneProtectedTask +
    $frame.reapFaultedTask + $frame.destroyPrivateAddressSpace +
    (3 * [Math]::Max($frame.privatePageTableWalk, $frame.releasePrivatePageTables)) +
    $unmodeledCallReserve + $interruptReserve + $bootEntryReserve
$faultRecoveryLifecycleWorst = [Math]::Max($lifecycleEntryPath, $lifecycleReaperPath)

foreach ($method in @('displayOptionsQuery', 'displayOptionsRefresh', 'displayOptionsSubmit')) {
    if ($frame[$method] -gt 2048) {
        throw "Legacy Display Options frame $method is $($frame[$method]) bytes; limit is 2048."
    }
}

if ($frame.displaySubmit -gt 7168) { throw "Display submit frame is too large: $($frame.displaySubmit) bytes." }
if ($frame.readDisplaySnapshot -gt 1536) { throw "Display provider frame is too large: $($frame.readDisplaySnapshot) bytes." }
if ($frame.bridgeServe -gt 1024) { throw "COM2 inlined dispatcher frame is too large: $($frame.bridgeServe) bytes." }
if ($frame.readStorageSnapshot -gt 1024) { throw "Storage provider frame is too large: $($frame.readStorageSnapshot) bytes." }
if ($frame.readMbr -gt 4608) { throw "MBR reader frame is too large: $($frame.readMbr) bytes." }
if ($frame.decodeStorageSnapshot -gt 6144) { throw "Storage diagnostic decoder frame is too large: $($frame.decodeStorageSnapshot) bytes." }
if ([Math]::Max([Math]::Max($settingsWorst, $settingsPersistenceWorst), $legacyDisplayWorst) -gt $guardedPathLimit) {
    throw "Display/Settings budget is $([Math]::Max([Math]::Max($settingsWorst, $settingsPersistenceWorst), $legacyDisplayWorst)) bytes, above the $guardedPathLimit-byte guarded limit."
}
if ($com2Worst -gt $guardedPathLimit) {
    throw "COM2 service budget is $com2Worst bytes, above the $guardedPathLimit-byte guarded limit."
}
if ($privilegeEntryBytes -gt $guardedPathLimit) {
    throw "CPL3 privilege-entry frame is $privilegeEntryBytes bytes, above the $guardedPathLimit-byte guarded limit."
}
if ($exceptionDispatchStack -gt $guardedPathLimit) {
    throw "CPL3 exception dispatch stack is $exceptionDispatchStack bytes, above the $guardedPathLimit-byte guarded limit."
}
if ($faultRecoveryLifecycleWorst -gt $guardedPathLimit) {
    throw "CPL3 recovery lifecycle path is $faultRecoveryLifecycleWorst bytes, above the $guardedPathLimit-byte guarded limit."
}

Write-Output ("Settings Display: launch={0} navigation={1} guarded={2}/{3} bytes; headroom={4} bytes." -f `
    $settingsLaunchPath, $settingsNavigationPath, $settingsWorst, $stackCapacity,
    ($stackCapacity - $settingsWorst))
Write-Output ("Settings AppData Apply: wallpaper={0} icons={1}; guarded={2}/{3} bytes; headroom={4} bytes." -f `
    $settingsWallpaperApplyPath, $settingsIconApplyPath, $settingsPersistenceWorst,
    $stackCapacity, ($stackCapacity - $settingsPersistenceWorst))
Write-Output ("Legacy Display Options: init={0} query={1} apply={2} refresh={3}; guarded={4}/{5} bytes; headroom={6} bytes." -f `
    $legacyDisplayInitPath, $legacyDisplayQueryPath, $legacyDisplayApplyPath,
    $legacyDisplayRefreshPath, $legacyDisplayWorst, $stackCapacity,
    ($stackCapacity - $legacyDisplayWorst))
Write-Output ("COM2: storage-dispatch={0} storage-decode={1} device-dispatch={2} device-decode={3} guarded={4}/{5} bytes; headroom={6} bytes." -f `
    $com2StorageDispatch, $com2StorageDecode, $com2DeviceDispatch, $com2DeviceDecode,
    $com2Worst, $stackCapacity, ($stackCapacity - $com2Worst))
Write-Output ("CPL3 fault: entry-stack={0}/{1} exception-dispatch={2}/{1} lifecycle-entry={3} lifecycle-reaper={4} guarded-worst={5}/{1} bytes; headroom={6} bytes." -f `
    $privilegeEntryBytes, $stackCapacity, $exceptionDispatchStack, $lifecycleEntryPath,
    $lifecycleReaperPath, $faultRecoveryLifecycleWorst, ($stackCapacity - $faultRecoveryLifecycleWorst))
Write-Output ("GCC .su budget guard passed; minimum required headroom is {0} bytes." -f $requiredHeadroom)
