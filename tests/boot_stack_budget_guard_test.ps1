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
        $locationMatch = [regex]::Match($parts[0], ':(\d+):(\d+):(.+)$')
        if (-not $locationMatch.Success) { continue }
        $bytesMatch = [regex]::Match($parts[1], '^\s*(\d+)(?:\s|$)')
        if (-not $bytesMatch.Success) { continue }
        $records += [PSCustomObject]@{
            Function = $locationMatch.Groups[3].Value
            Bytes = [int]$bytesMatch.Groups[1].Value
            Source = $file.Name
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

$frame = [ordered]@{
    kernelMain = Get-Frame 'kernel_main\(void\*, uint32_t\)'
    desktopMouse = Get-Frame 'kernel::desktop::handle_mouse\('
    desktopLaunch = Get-Frame 'kernel::desktop::launch_app\('
    appManagerLaunch = Get-Frame 'kernel::app::AppManager::launchApp\('
    settingsInit = Get-Frame 'SettingsNativeApp::init\('
    settingsMouse = Get-Frame 'SettingsNativeApp::onMouseDown\('
    settingsRefresh = Get-Frame 'settings_native::refreshDisplaySnapshot\('
    settingsProviderCallback = Get-Frame 'SettingsNativeApp::readProviderSnapshot\('
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
}

$settingsLaunchPath = $frame.kernelMain + $frame.desktopMouse + $frame.desktopLaunch +
    $frame.appManagerLaunch + $frame.settingsInit + $frame.settingsRefresh +
    $frame.settingsProviderCallback + $frame.readDisplaySnapshot + $frame.displaySubmit
$settingsNavigationPath = $frame.kernelMain + $frame.desktopMouse +
    $frame.compositorMouseDown + $frame.settingsMouse + $frame.settingsRefresh +
    $frame.settingsProviderCallback + $frame.readDisplaySnapshot + $frame.displaySubmit
$settingsWorst = [Math]::Max($settingsLaunchPath, $settingsNavigationPath) +
    $unmodeledCallReserve + $interruptReserve + $bootEntryReserve
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
if ([Math]::Max($settingsWorst, $legacyDisplayWorst) -gt $guardedPathLimit) {
    throw "Display budget is $([Math]::Max($settingsWorst, $legacyDisplayWorst)) bytes, above the $guardedPathLimit-byte guarded limit."
}
if ($com2Worst -gt $guardedPathLimit) {
    throw "COM2 service budget is $com2Worst bytes, above the $guardedPathLimit-byte guarded limit."
}

Write-Output ("Settings Display: launch={0} navigation={1} guarded={2}/{3} bytes; headroom={4} bytes." -f `
    $settingsLaunchPath, $settingsNavigationPath, $settingsWorst, $stackCapacity,
    ($stackCapacity - $settingsWorst))
Write-Output ("Legacy Display Options: init={0} query={1} apply={2} refresh={3}; guarded={4}/{5} bytes; headroom={6} bytes." -f `
    $legacyDisplayInitPath, $legacyDisplayQueryPath, $legacyDisplayApplyPath,
    $legacyDisplayRefreshPath, $legacyDisplayWorst, $stackCapacity,
    ($stackCapacity - $legacyDisplayWorst))
Write-Output ("COM2: storage-dispatch={0} storage-decode={1} device-dispatch={2} device-decode={3} guarded={4}/{5} bytes; headroom={6} bytes." -f `
    $com2StorageDispatch, $com2StorageDecode, $com2DeviceDispatch, $com2DeviceDecode,
    $com2Worst, $stackCapacity, ($stackCapacity - $com2Worst))
Write-Output ("GCC .su budget guard passed; minimum required headroom is {0} bytes." -f $requiredHeadroom)
