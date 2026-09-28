Set-StrictMode -Version Latest

function New-P29JStageId {
    return [guid]::NewGuid().ToString('N')
}

function New-P29JQemuArguments {
    param(
        [Parameter(Mandatory = $true)][string]$OvmfCodePath,
        [Parameter(Mandatory = $true)][string]$EspPath,
        [Parameter(Mandatory = $true)][string]$SerialPath,
        [Parameter(Mandatory = $true)][string]$DebugconPath,
        [Parameter(Mandatory = $true)][string]$QemuDebugPath
    )

    return @(
        '-machine', 'pc,usb=off',
        '-drive', "if=pflash,format=raw,readonly=on,file=$OvmfCodePath",
        '-drive', "file=fat:rw:$EspPath,format=raw,if=ide,index=0",
        '-m', '4096M',
        '-vga', 'std',
        '-serial', "file:$SerialPath",
        '-debugcon', "file:$DebugconPath",
        '-global', 'isa-debugcon.iobase=0xe9',
        '-d', 'cpu_reset',
        '-D', $QemuDebugPath,
        '-display', 'none',
        '-no-reboot',
        '-no-shutdown'
    )
}

function Get-P29JBootClassification {
    param(
        [Parameter(Mandatory = $true)][bool]$SpawnSucceeded,
        [Parameter(Mandatory = $true)][bool]$AliveAtDeadline,
        [Parameter(Mandatory = $true)][bool]$ExitedBeforeHarnessStop,
        [int]$ExitCode = 0,
        [string]$SerialText = '',
        [string]$DebugconText = '',
        [int]$CpuResetCount = 0
    )

    if (-not $SpawnSucceeded) { return 'QEMU_SPAWN_FAILED' }

    $nativeLoaderMarker = $SerialText.Contains('P28Z BOOT 01 native_loader_entered') -or
        $DebugconText.Contains('P29J GUEST 07 native_loader_dispatch_ready')
    if ($nativeLoaderMarker) { return 'NATIVE_LOADER_REACHED' }

    $firmwareStartCount = [regex]::Matches($SerialText, [regex]::Escape('BdsDxe: starting Boot')).Count
    $debugconLoaderCount = [regex]::Matches($DebugconText, [regex]::Escape('P29J GUEST 01 uefi_loader_entry')).Count
    $serialLoaderCount = [regex]::Matches($SerialText, [regex]::Escape('guideXOS UEFI Bootloader')).Count
    $firmwareBoot = $firmwareStartCount -gt 0
    $loaderEntry = $SerialText.Contains('guideXOS UEFI Bootloader') -or
        $DebugconText.Contains('P29J GUEST 01 uefi_loader_entry')
    $loaderReentered = $debugconLoaderCount -gt 1 -or $serialLoaderCount -gt 1
    if ($CpuResetCount -gt 2 -and ($firmwareStartCount -gt 1 -or $loaderReentered)) {
        return 'QEMU_RESET_REENTRY_LOOP'
    }
    if ($CpuResetCount -gt 2 -and -not $firmwareBoot -and -not $loaderEntry) {
        return 'QEMU_RESETS_WITHOUT_FIRMWARE_HANDOFF'
    }
    $kernelEntry = $DebugconText.Contains('P29J GUEST 05 kernel_entry_before_uart')
    $uartInitialized = $DebugconText.Contains('P29J GUEST 06 kernel_uart_initialized')

    if ($AliveAtDeadline) {
        if ($uartInitialized) { return 'EARLY_GUEST_EXECUTION_HANG' }
        if ($kernelEntry) { return 'SERIAL_INITIALIZATION_OUTPUT_FAILURE' }
        if ($loaderEntry) { return 'LOADER_EARLY_HANG' }
        if ($firmwareBoot) { return 'FIRMWARE_BOOTED_LOADER_NOT_ENTERED' }
        return 'NO_FIRMWARE_HANDOFF_OBSERVED'
    }

    if (-not $ExitedBeforeHarnessStop) { return 'QEMU_PROCESS_STATE_UNOBSERVED' }
    if ($firmwareBoot -and -not $loaderEntry) { return 'FIRMWARE_BOOTED_LOADER_NOT_ENTERED' }
    if ($loaderEntry -and -not $kernelEntry) { return 'LOADER_EXITED_BEFORE_KERNEL_HANDOFF' }
    if ($kernelEntry -and -not $uartInitialized) { return 'SERIAL_INITIALIZATION_OUTPUT_FAILURE' }
    if ($uartInitialized) { return 'EARLY_GUEST_EXITED_BEFORE_NATIVE_LOADER' }
    if ($ExitCode -ne 0) { return 'QEMU_EXITED_WITH_ERROR' }
    if (-not $firmwareBoot -and -not $loaderEntry) { return 'QEMU_EXITED_BEFORE_GUEST_EVIDENCE' }
    return 'QEMU_EXITED_BEFORE_NATIVE_LOADER'
}

function Get-P29JLoaderMarkerObserved {
    param(
        [string]$SerialText = '',
        [string]$DebugconText = ''
    )

    return $SerialText.Contains('guideXOS UEFI Bootloader') -or
        $DebugconText.Contains('P29J GUEST 01 uefi_loader_entry')
}

Export-ModuleMember -Function New-P29JStageId, New-P29JQemuArguments, Get-P29JBootClassification, Get-P29JLoaderMarkerObserved
