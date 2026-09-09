[CmdletBinding()]
param(
    [string]$QemuPath = '',
    [string]$RamSizeMB = '256,512',
    [int]$TimeoutSeconds = 300,
    [switch]$SkipBuild,
    [string]$OutputRoot = '',
    [string]$NormalKernelPath = ''
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$RamSizes = @($RamSizeMB -split '[,; ]+' | Where-Object { $_ -ne '' } |
    ForEach-Object { [int]$_ })
if ($RamSizes.Count -lt 2 -or ($RamSizes | Where-Object { $_ -le 16 }).Count -ne 0) {
    throw 'RamSizeMB must contain at least two values, all greater than 16.'
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $Root 'out\dotnet\c011ec99-production-physical-frame-scaling'
}
$RunRoot = Join-Path $OutputRoot ('run-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
foreach ($name in @(
        'source-architecture-audit', 'firmware-memory-map-analysis',
        'metadata-scaling-design', 'ram-size-matrix',
        'old-boundary-allocation-proof', 'nativeaot-relevance-proof',
        'ordinary-boot-validation', 'final-classification')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $RunRoot $name) | Out-Null
}

function Find-Executable([string]$Explicit, [string]$EnvironmentName,
                         [string]$CommandName, [string[]]$CommonPaths) {
    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) { $candidates.Add($Explicit) }
    $environmentValue = [Environment]::GetEnvironmentVariable($EnvironmentName)
    if (-not [string]::IsNullOrWhiteSpace($environmentValue)) { $candidates.Add($environmentValue) }
    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -ne $command) { $candidates.Add($command.Source) }
    foreach ($path in $CommonPaths) { $candidates.Add($path) }
    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Quote-QemuValue([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Invoke-Logged([string]$Executable, [string[]]$Arguments, [string]$LogPath) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Executable @Arguments 2>&1 | Out-File -FilePath $LogPath -Append -Encoding utf8
        if ($LASTEXITCODE -ne 0) {
            throw ("Command failed ({0}): {1}" -f $LASTEXITCODE, $Executable)
        }
    }
    finally { $ErrorActionPreference = $previous }
}

function Stage-Esp([string]$EspPath, [string]$KernelPath, [string]$BootloaderPath,
                  [string]$RamdiskPath) {
    New-Item -ItemType Directory -Force -Path (Join-Path $EspPath 'EFI\BOOT') | Out-Null
    Copy-Item -LiteralPath $BootloaderPath -Destination (Join-Path $EspPath 'EFI\BOOT\BOOTX64.EFI') -Force
    Copy-Item -LiteralPath $KernelPath -Destination (Join-Path $EspPath 'kernel.elf') -Force
    Copy-Item -LiteralPath $RamdiskPath -Destination (Join-Path $EspPath 'ramdisk.img') -Force
}

function Invoke-QemuBoot([string]$EspPath, [string]$SerialPath, [string]$DebugPath,
                         [string]$StdoutPath, [string]$StderrPath, [int]$MemoryMB,
                         [string]$MarkerPattern) {
    $arguments = @(
        '-accel', 'tcg,thread=single', '-machine', 'pc',
        '-drive', ('if=pflash,format=raw,readonly=on,file=' + (Quote-QemuValue $OvmfPath)),
        '-drive', ('file=fat:rw:' + (Quote-QemuValue $EspPath) + ',format=raw,if=ide,index=0'),
        '-m', ($MemoryMB.ToString() + 'M'), '-vga', 'std', '-display', 'none',
        '-serial', ('file:' + (Quote-QemuValue $SerialPath)),
        '-no-reboot', '-no-shutdown', '-rtc', 'base=utc,clock=host',
        '-d', 'int,cpu_reset', '-D', (Quote-QemuValue $DebugPath)
    )
    $process = Start-Process -FilePath $QemuPath -ArgumentList $arguments -WorkingDirectory $Root `
        -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -WindowStyle Hidden -PassThru
    $markerFound = $false
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $SerialPath) {
                $serial = Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
                if ($serial -match $MarkerPattern) { $markerFound = $true; break }
            }
            if ($process.HasExited) { break }
        }
    }
    finally {
        $live = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
        if ($null -ne $live) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $stopDeadline = (Get-Date).AddSeconds(3)
            while ((Get-Date) -lt $stopDeadline -and
                   $null -ne (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
                Start-Sleep -Milliseconds 100
            }
        }
    }
    $serialText = if (Test-Path -LiteralPath $SerialPath) {
        Get-Content -LiteralPath $SerialPath -Raw -ErrorAction SilentlyContinue
    } else { '' }
    return [pscustomobject]@{
        MarkerFound = $markerFound
        SerialPath = $SerialPath
        DebugPath = $DebugPath
        Serial = $serialText
    }
}

function Get-HexMetric([string]$Text, [string]$Name) {
    $match = [regex]::Match($Text, ('(?m)^\[C99-PMM\] .*?{0}=([0-9A-Fa-fx]+)' -f [regex]::Escape($Name)))
    if (-not $match.Success) { return $null }
    $value = $match.Groups[1].Value
    if ($value.StartsWith('0x')) { return [Convert]::ToUInt64($value.Substring(2), 16) }
    return [Convert]::ToUInt64($value, 16)
}

$QemuPath = Find-Executable $QemuPath 'GXOS_QEMU_X64' 'qemu-system-x86_64.exe' @(
    'C:\Program Files\qemu\qemu-system-x86_64.exe',
    'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
    "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
    'C:\qemu\qemu-system-x86_64.exe', 'C:\msys64\mingw64\bin\qemu-system-x86_64.exe'
)
$make = Find-Executable '' 'GXOS_MINGW32_MAKE' 'mingw32-make.exe' @(
    'C:\mingw64\bin\mingw32-make.exe', 'C:\msys64\mingw64\bin\mingw32-make.exe'
)
$msbuild = Find-Executable '' '' 'MSBuild.exe' @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
)
$OvmfPath = $null
foreach ($candidate in @(
        (Join-Path $Root 'OVMF.fd'), (Join-Path $Root 'ovmf.fd'),
        'C:\Program Files\qemu\share\edk2-x86_64-code.fd')) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $OvmfPath = (Resolve-Path -LiteralPath $candidate).Path
        break
    }
}
$bootloaderPath = Join-Path $Root 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$ramdiskPath = Join-Path $Root 'ESP\ramdisk.img'
$kernelPath = Join-Path $Root 'kernel\build\amd64\bin\kernel.elf'
$ordinaryKernelPath = Join-Path $RunRoot 'ordinary-kernel.elf'
$c99KernelPath = Join-Path $RunRoot 'c99-kernel.elf'

@(
    ('root={0}' -f $Root), ('runRoot={0}' -f $RunRoot),
    ('requestedRamMB={0}' -f ($RamSizes -join ',')),
    ('qemu={0}' -f $QemuPath), ('ovmf={0}' -f $OvmfPath),
    ('make={0}' -f $make), ('msbuild={0}' -f $msbuild)
) | Set-Content -LiteralPath (Join-Path $RunRoot 'run-info.txt') -Encoding utf8

if (-not $SkipBuild) {
    if ($null -eq $make) { throw 'mingw32-make.exe not found.' }
    if ($null -eq $msbuild) { throw 'MSBuild.exe not found.' }
    Invoke-Logged $make @('-C', 'kernel', 'ARCH=amd64', 'clean') (Join-Path $RunRoot 'normal-kernel-build.log')
    Invoke-Logged $make @('-C', 'kernel', 'ARCH=amd64', '-j2') (Join-Path $RunRoot 'normal-kernel-build.log')
    Copy-Item -LiteralPath $kernelPath -Destination $ordinaryKernelPath -Force
    Invoke-Logged $make @('-C', 'kernel', 'ARCH=amd64', 'clean') (Join-Path $RunRoot 'c99-kernel-build.log')
    Invoke-Logged $make @('-C', 'kernel', 'ARCH=amd64', '-j2',
        'EXTRA_CFLAGS=-DGXOS_C011EC99_PHYSICAL_FRAME_SCALING_QEMU_TEST -DGXOS_NATIVE_VIRTUAL_MEMORY_QEMU_TEST') `
        (Join-Path $RunRoot 'c99-kernel-build.log')
    Copy-Item -LiteralPath $kernelPath -Destination $c99KernelPath -Force
    Invoke-Logged $msbuild @('guideXOSBootLoader\guideXOSBootLoader.vcxproj',
        '/p:Configuration=Release', '/p:Platform=x64', '/t:Rebuild', '/m', '/nologo', '/verbosity:minimal') `
        (Join-Path $RunRoot 'bootloader-build.log')
} else {
    Copy-Item -LiteralPath $kernelPath -Destination $c99KernelPath -Force
    $ordinarySource = if ([string]::IsNullOrWhiteSpace($NormalKernelPath)) {
        $kernelPath
    } else { (Resolve-Path -LiteralPath $NormalKernelPath).Path }
    Copy-Item -LiteralPath $ordinarySource -Destination $ordinaryKernelPath -Force
}

if ($null -eq $QemuPath -or $null -eq $OvmfPath -or
    -not (Test-Path -LiteralPath $bootloaderPath) -or
    -not (Test-Path -LiteralPath $ramdiskPath)) {
    throw 'QEMU, OVMF, bootloader, or ramdisk prerequisite is missing.'
}
if ($null -eq $QemuPath) { throw 'qemu-system-x86_64.exe not found.' }
@(
    ('version={0}' -f ((& $QemuPath --version 2>&1 | Out-String).Trim())),
    ('sha256={0}' -f (Get-FileHash -LiteralPath $QemuPath -Algorithm SHA256).Hash),
    ('ovmfSha256={0}' -f (Get-FileHash -LiteralPath $OvmfPath -Algorithm SHA256).Hash)
) | Set-Content -LiteralPath (Join-Path $RunRoot 'firmware-memory-map-analysis\toolchain.txt') -Encoding utf8

$matrix = New-Object System.Collections.Generic.List[object]
foreach ($memoryMB in $RamSizes) {
    $label = ('ram-{0}MB' -f $memoryMB)
    $esp = Join-Path $RunRoot ('ram-size-matrix\' + $label + '\ESP')
    New-Item -ItemType Directory -Force -Path $esp | Out-Null
    Stage-Esp $esp $c99KernelPath $bootloaderPath $ramdiskPath
    $qemu = Invoke-QemuBoot $esp `
        (Join-Path $RunRoot ('ram-size-matrix\' + $label + '\serial.log')) `
        (Join-Path $RunRoot ('ram-size-matrix\' + $label + '\qemu-debug.log')) `
        (Join-Path $RunRoot ('ram-size-matrix\' + $label + '\qemu.stdout.log')) `
        (Join-Path $RunRoot ('ram-size-matrix\' + $label + '\qemu.stderr.log')) `
        $memoryMB '\[native-virtual-memory-test\] ALL_(PASS|FAIL)'
    $tracked = Get-HexMetric $qemu.Serial 'trackedFrames'
    $discovered = Get-HexMetric $qemu.Serial 'discoveredUsablePages'
    $metadata = Get-HexMetric $qemu.Serial 'metadataPages'
    $highPhysical = Get-HexMetric $qemu.Serial 'firstAllocationBeyondOldBoundaryPhysical'
    $result = [pscustomobject]@{
        RamMB = $memoryMB
        MarkerFound = $qemu.MarkerFound
        AllPass = $qemu.Serial -match '\[C99-PMM\] ALL_PASS'
        NativeAllPass = $qemu.Serial -match '\[native-virtual-memory-test\] ALL_PASS'
        TrackedFrames = $tracked
        DiscoveredUsablePages = $discovered
        MetadataPages = $metadata
        HighPhysical = $highPhysical
    }
    $matrix.Add($result)
    @(
        ('ramMB={0}' -f $memoryMB), ('markerFound={0}' -f $result.MarkerFound),
        ('allPass={0}' -f $result.AllPass), ('nativeAllPass={0}' -f $result.NativeAllPass),
        ('trackedFrames={0}' -f $tracked),
        ('discoveredUsablePages={0}' -f $discovered), ('metadataPages={0}' -f $metadata),
        ('firstAllocationBeyondOldBoundaryPhysical={0}' -f $highPhysical)
    ) | Set-Content -LiteralPath (Join-Path $RunRoot ('ram-size-matrix\' + $label + '\summary.txt')) -Encoding utf8
}
$matrix | Export-Csv -LiteralPath (Join-Path $RunRoot 'ram-size-matrix\results.csv') -NoTypeInformation

foreach ($result in $matrix) {
    $label = ('ram-{0}MB' -f $result.RamMB)
    Copy-Item -LiteralPath (Join-Path $RunRoot ('ram-size-matrix\' + $label + '\serial.log')) `
        -Destination (Join-Path $RunRoot ('old-boundary-allocation-proof\' + $label + '-serial.log')) -Force
    Copy-Item -LiteralPath (Join-Path $RunRoot ('ram-size-matrix\' + $label + '\serial.log')) `
        -Destination (Join-Path $RunRoot ('nativeaot-relevance-proof\' + $label + '-serial.log')) -Force
}

@(
    'The production allocator consumes BootInfo.MemoryMap after ExitBootServices.',
    'It normalizes Conventional, BootServicesCode, and BootServicesData descriptors.',
    'Per-frame ownership is stored as one byte per enrolled frame; no fixed frame pool remains.',
    'The 256-entry range table bounds descriptor fragmentation only; it is not a RAM capacity.',
    'The ordinary kernel and the NativeAOT-facing PAL/VM adapter share this allocator.'
) | Set-Content -LiteralPath (Join-Path $RunRoot 'source-architecture-audit\summary.txt') -Encoding utf8
@(
    'Firmware map source: EFI memory descriptors handed through BootInfo.',
    'Usable types: 3 (BootServicesCode), 4 (BootServicesData), and 7 (Conventional).',
    'Reserved, runtime, ACPI, MMIO, and loader metadata are excluded from tracked frames.',
    ('Observed tracked frames by RAM size: {0}' -f (($matrix | ForEach-Object { '{0}MB={1}' -f $_.RamMB, $_.TrackedFrames }) -join ', ')),
    ('Observed metadata pages by RAM size: {0}' -f (($matrix | ForEach-Object { '{0}MB={1}' -f $_.RamMB, $_.MetadataPages }) -join ', '))
) | Set-Content -LiteralPath (Join-Path $RunRoot 'firmware-memory-map-analysis\summary.txt') -Encoding utf8
@(
    'Metadata allocation is ceil(discoveredUsableFrames / 4096) pages.',
    'Each metadata byte stores frame owner state plus the mapped bit.',
    'The bootloader allocates metadata as EfiLoaderData and identity-maps it.',
    'The bootloader identity-map range list is bounded bootstrap bookkeeping only.',
    ('Matrix tracked-frame values: {0}' -f (($matrix | ForEach-Object { $_.TrackedFrames }) -join ', '))
) | Set-Content -LiteralPath (Join-Path $RunRoot 'metadata-scaling-design\summary.txt') -Encoding utf8
@(
    'The C99 probe allocates every old-boundary frame through index 0xFFF.',
    'It then allocates VmRegion and PageTable owners beyond that boundary.',
    'It checks map-derived capacity, descriptor enrollment/exclusion, owner checks, rollback, double-free rejection, and final reconciliation.',
    'The serial logs in this directory are the per-RAM proof records.'
) | Set-Content -LiteralPath (Join-Path $RunRoot 'old-boundary-allocation-proof\summary.txt') -Encoding utf8
@(
    'Direct ordinary managed NativeAOT launch remains opt-in and is not wired by this repository milestone.',
    'The existing NativeAOT-facing PAL/virtual-memory adapter probe is compiled with C99 and runs against the same production allocator.',
    'The copied serial logs retain PAL reserve/commit/decommit/recommit, mapping, ownership, and leak checks.'
) | Set-Content -LiteralPath (Join-Path $RunRoot 'nativeaot-relevance-proof\summary.txt') -Encoding utf8

$ordinaryEsp = Join-Path $RunRoot 'ordinary-boot-validation\ESP'
New-Item -ItemType Directory -Force -Path $ordinaryEsp | Out-Null
Stage-Esp $ordinaryEsp $ordinaryKernelPath $bootloaderPath $ramdiskPath
$ordinaryMemoryMB = [Math]::Max(256, $RamSizes[0])
$ordinary = Invoke-QemuBoot $ordinaryEsp `
    (Join-Path $RunRoot 'ordinary-boot-validation\serial.log') `
    (Join-Path $RunRoot 'ordinary-boot-validation\qemu-debug.log') `
    (Join-Path $RunRoot 'ordinary-boot-validation\qemu.stdout.log') `
    (Join-Path $RunRoot 'ordinary-boot-validation\qemu.stderr.log') `
    $ordinaryMemoryMB '\[KERNEL\] Entering main loop \(waiting for input\)'
@(
    ('markerFound={0}' -f $ordinary.MarkerFound),
    ('ramMB={0}' -f $ordinaryMemoryMB),
    ('expectedMarker=[KERNEL] Entering main loop (waiting for input)')
) | Set-Content -LiteralPath (Join-Path $RunRoot 'ordinary-boot-validation\summary.txt') -Encoding utf8

$matrixPass = $matrix.Count -ge 2 -and
    ($matrix | Where-Object { -not $_.MarkerFound -or -not $_.AllPass -or
        -not $_.NativeAllPass -or $_.TrackedFrames -le 0x1000 }).Count -eq 0 -and
    $matrix[0].TrackedFrames -ne $matrix[$matrix.Count - 1].TrackedFrames
$classification = if ($matrixPass -and $ordinary.MarkerFound) { 'PRODUCTION_CAPACITY_FIXED' } else { 'PRODUCTION_CAPACITY_FIX_INCOMPLETE' }
@(
    ('classification={0}' -f $classification),
    ('matrixPass={0}' -f $matrixPass), ('ordinaryBootPass={0}' -f $ordinary.MarkerFound),
    ('trackedFrameValues={0}' -f (($matrix | ForEach-Object { $_.TrackedFrames }) -join ',')),
    ('evidenceRoot={0}' -f $RunRoot)
) | Set-Content -LiteralPath (Join-Path $RunRoot 'final-classification\classification.txt') -Encoding utf8

# Leave the working build artifact on the ordinary production path.  The C99
# image is retained under the run root, so this does not discard evidence.
Copy-Item -LiteralPath $ordinaryKernelPath -Destination $kernelPath -Force
('restoredKernel={0}' -f $kernelPath) | Add-Content -LiteralPath (Join-Path $RunRoot 'run-info.txt') -Encoding utf8

Copy-Item -LiteralPath (Join-Path $RunRoot 'ram-size-matrix\results.csv') `
    -Destination (Join-Path $OutputRoot 'latest-results.csv') -Force
Write-Host ('C99 classification: ' + $classification)
Write-Host ('Evidence root: ' + $RunRoot)
if ($classification -ne 'PRODUCTION_CAPACITY_FIXED') { exit 1 }
