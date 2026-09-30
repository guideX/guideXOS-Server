$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../scripts/Phase29J.BootEvidence.psm1') -Force

function Assert-Equal([object]$actual, [object]$expected, [string]$name) {
    if ($actual -ne $expected) {
        throw "$name expected '$expected', received '$actual'"
    }
}

$stageOne = New-P29JStageId
$stageTwo = New-P29JStageId
if ($stageOne -eq $stageTwo) { throw 'per-boot stage IDs must be unique' }

$esp = 'C:\temp\esp-boot2-stage-id'
$qemuArgs = New-P29JQemuArguments -OvmfCodePath 'D:\repo\OVMF.fd' -EspPath $esp `
    -SerialPath 'C:\temp\boot2.serial.log' -DebugconPath 'C:\temp\boot2.debugcon.log' `
    -QemuDebugPath 'C:\temp\boot2.qemu-debug.log'
$driveArg = $qemuArgs[5]
Assert-Equal $driveArg "file=fat:rw:$esp,format=raw,if=ide,index=0" 'QEMU must attach the requested staged ESP'
if ($qemuArgs -notcontains 'isa-debugcon.iobase=0xe9') { throw 'QEMU debugcon must be attached at port 0xe9' }
$debugFlagIndex = [array]::IndexOf($qemuArgs, '-d')
if ($qemuArgs[$debugFlagIndex + 1] -ne 'cpu_reset') { throw 'normal QEMU runs must retain reset-only logging' }
$diagnosticArgs = New-P29JQemuArguments -OvmfCodePath 'D:\repo\OVMF.fd' -EspPath $esp `
    -SerialPath 'C:\temp\boot2.serial.log' -DebugconPath 'C:\temp\boot2.debugcon.log' `
    -QemuDebugPath 'C:\temp\boot2.qemu-debug.log' -TraceExceptions
$diagnosticFlagIndex = [array]::IndexOf($diagnosticArgs, '-d')
if ($diagnosticArgs[$diagnosticFlagIndex + 1] -ne 'int,cpu_reset') {
    throw 'exception diagnostic runs must capture interrupts and CPU resets'
}
if ($diagnosticArgs -notcontains '-no-reboot') { throw 'diagnostic QEMU runs must preserve reset evidence without rebooting' }

function Classify([bool]$alive, [bool]$exited, [int]$code = 0, [string]$serial = '', [string]$debugcon = '', [int]$resets = 0) {
    Get-P29JBootClassification -SpawnSucceeded $true -AliveAtDeadline $alive `
        -ExitedBeforeHarnessStop $exited -ExitCode $code -SerialText $serial `
        -DebugconText $debugcon -CpuResetCount $resets
}

Assert-Equal (Classify $false $true 2) 'QEMU_EXITED_WITH_ERROR' 'empty serial and exited QEMU'
Assert-Equal (Classify $true $false) 'NO_FIRMWARE_HANDOFF_OBSERVED' 'empty serial and live QEMU'
Assert-Equal (Classify $true $false 0 '' '' 3) 'QEMU_RESETS_WITHOUT_FIRMWARE_HANDOFF' 'repeated resets without firmware evidence'
Assert-Equal (Classify $false $true 0 'BdsDxe: starting Boot BdsDxe: starting Boot' '' 3) 'QEMU_RESET_REENTRY_LOOP' 'repeated firmware entry'
Assert-Equal (Classify $false $true 0 '' 'P29J GUEST 01 uefi_loader_entry P29J GUEST 01 uefi_loader_entry' 3) 'QEMU_RESET_REENTRY_LOOP' 'repeated loader entry'
Assert-Equal (Classify $true $false 0 'BdsDxe: starting Boot') 'FIRMWARE_BOOTED_LOADER_NOT_ENTERED' 'firmware marker without loader entry'
Assert-Equal (Classify $true $false 0 '' 'P29J GUEST 01 uefi_loader_entry') 'LOADER_EARLY_HANG' 'loader marker without kernel handoff'
Assert-Equal (Classify $true $false 0 '' 'P29J GUEST 05 kernel_entry_before_uart') 'SERIAL_INITIALIZATION_OUTPUT_FAILURE' 'kernel entry without UART initialization'
Assert-Equal (Classify $true $false 0 '' 'P29J GUEST 06 kernel_uart_initialized') 'EARLY_GUEST_EXECUTION_HANG' 'UART init without native loader'
Assert-Equal (Classify $false $true 0 '' 'P29J GUEST 07 native_loader_dispatch_ready') 'NATIVE_LOADER_REACHED' 'native loader marker'
Assert-Equal (Get-P29JBootClassification -SpawnSucceeded $false -AliveAtDeadline $false -ExitedBeforeHarnessStop $false) 'QEMU_SPAWN_FAILED' 'spawn failure'

if (-not (Get-P29JLoaderMarkerObserved -DebugconText 'P29J GUEST 01 uefi_loader_entry')) {
    throw 'loader marker detection missed debugcon evidence'
}
if (Get-P29JLoaderMarkerObserved -SerialText '' -DebugconText '') {
    throw 'loader marker detection accepted empty evidence'
}

Write-Output 'Phase 29J boot evidence tests passed.'
