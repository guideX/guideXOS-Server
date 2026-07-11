param(
    [string[]]$Backends = @('std', 'virtio-gpu', 'virtio-gpu-modern-only', 'virtio-vga', 'qxl-vga'),
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogRoot = Join-Path $Root 'logs'
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunRoot = Join-Path $LogRoot ("qemu-display-probe-" + $stamp)
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

$AllowedBackends = @('std', 'virtio-gpu', 'virtio-gpu-modern-only', 'multimonitor', 'virtio-vga', 'virtio', 'qxl-vga', 'qxl')
foreach ($backend in $Backends) {
    if ($AllowedBackends -notcontains $backend) {
        throw "Unsupported backend '$backend'. Supported backends: $($AllowedBackends -join ', ')"
    }
}

function Assert-PathExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label missing: $Path"
    }
}

function Find-Qemu {
    $qemu = Get-Command 'qemu-system-x86_64' -ErrorAction SilentlyContinue
    if ($qemu) {
        return $qemu.Source
    }

    foreach ($candidate in @(
        'C:\Program Files\qemu\qemu-system-x86_64.exe',
        'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        'C:\qemu\qemu-system-x86_64.exe',
        'D:\qemu\qemu-system-x86_64.exe'
    )) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

$script:qemuVirtioGpuHelpText = $null

function Get-QemuVirtioGpuHelpText {
    if ($null -ne $script:qemuVirtioGpuHelpText) {
        return $script:qemuVirtioGpuHelpText
    }

    $qemu = Find-Qemu
    if (-not $qemu) {
        return $null
    }

    $script:qemuVirtioGpuHelpText = & $qemu -device virtio-gpu-pci,help 2>&1 | Out-String
    return $script:qemuVirtioGpuHelpText
}

function Test-QemuVirtioGpuModernOnlySupport {
    $helpText = Get-QemuVirtioGpuHelpText
    if ([string]::IsNullOrWhiteSpace($helpText)) {
        return $false
    }

    return $helpText -match 'disable-legacy=<OnOffAuto>'
}

function Find-Ovmf {
    foreach ($candidate in @(
        (Join-Path $Root 'OVMF.fd'),
        (Join-Path $Root 'ovmf.fd'),
        'C:\Program Files\qemu\share\edk2-x86_64-code.fd',
        'C:\Program Files (x86)\qemu\share\edk2-x86_64-code.fd',
        "$env:LOCALAPPDATA\Programs\qemu\share\edk2-x86_64-code.fd"
    )) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

function Save-EnvironmentState {
    param(
        [string[]]$Names
    )

    $state = @{}
    foreach ($name in $Names) {
        $state[$name] = (Get-Item -Path "Env:$name" -ErrorAction SilentlyContinue).Value
    }
    return $state
}

function Restore-EnvironmentState {
    param(
        [hashtable]$State
    )

    foreach ($entry in $State.GetEnumerator()) {
        if ($null -eq $entry.Value -or $entry.Value -eq '') {
            Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue
        } else {
            Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value
        }
    }
}

function Invoke-KernelBuildForSmoke {
    param(
        [string]$ExtraCFlags
    )

    $oldExtra = $env:EXTRA_CFLAGS
    $buildCode = 1
    try {
        if ([string]::IsNullOrWhiteSpace($ExtraCFlags)) {
            Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
        } else {
            $env:EXTRA_CFLAGS = $ExtraCFlags
        }

        $probeObjectCandidates = @(
            (Join-Path $Root 'build\amd64\obj\core\virtio_gpu.o'),
            (Join-Path $Root 'build\amd64\obj\core\virtio_gpu.d'),
            (Join-Path $Root 'build\amd64\obj\core\mmio.o'),
            (Join-Path $Root 'build\amd64\obj\core\mmio.d'),
            (Join-Path $Root 'kernel\build\amd64\obj\core\virtio_gpu.o'),
            (Join-Path $Root 'kernel\build\amd64\obj\core\virtio_gpu.d'),
            (Join-Path $Root 'kernel\build\amd64\obj\core\mmio.o'),
            (Join-Path $Root 'kernel\build\amd64\obj\core\mmio.d')
        )
        Remove-Item -LiteralPath $probeObjectCandidates -ErrorAction SilentlyContinue

        Push-Location $Root
        try {
            & (Join-Path $Root 'build-kernel.bat')
            $buildCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }
    } finally {
        if ($null -ne $oldExtra -and $oldExtra -ne '') {
            $env:EXTRA_CFLAGS = $oldExtra
        } else {
            Remove-Item Env:\EXTRA_CFLAGS -ErrorAction SilentlyContinue
        }
    }

    if ($buildCode -ne 0) {
        throw "Kernel build failed with exit code $buildCode."
    }
}

$script:activeSmokeBuild = $false
function Restore-NormalKernelBuild {
    if ($script:activeSmokeBuild) {
        Write-Host 'Restoring normal kernel build after virtio-gpu probe smoke...'
        Invoke-KernelBuildForSmoke -ExtraCFlags ''
        $script:activeSmokeBuild = $false
    }
}

function Read-LogText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return ''
    }

    $text = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
    if ($null -eq $text) {
        return ''
    }

    return $text
}

function Get-LogTail {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [int]$LineCount = 40
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return ''
    }

    $lines = Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue
    if ($null -eq $lines) {
        return ''
    }

    $count = $lines.Count
    if ($count -le $LineCount) {
        return ($lines -join "`n")
    }

    return (($lines | Select-Object -Last $LineCount) -join "`n")
}

function Stop-ProcessTree {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process
    )

    if ($Process.HasExited) {
        return
    }

    try {
        & taskkill.exe /T /F /PID $Process.Id | Out-Null
    } catch {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    }
}

function Start-ProbeLauncher {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Backend,
        [Parameter(Mandatory = $true)]
        [string]$SerialLog,
        [Parameter(Mandatory = $true)]
        [string]$LauncherStdOut,
        [Parameter(Mandatory = $true)]
        [string]$LauncherStdErr
    )

    $batchPath = Join-Path $Root 'scripts\run-qemu-display-probe.bat'
    Assert-PathExists -Path $batchPath -Label 'QEMU display probe launcher'

    $oldState = Save-EnvironmentState -Names @(
        'GXOS_QEMU_DISPLAY_PROBE_HEADLESS',
        'GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE',
        'GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG'
    )

    $env:GXOS_QEMU_DISPLAY_PROBE_HEADLESS = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG = $SerialLog

    $cmdArgs = @(
        '/c',
        "`"$batchPath`" $Backend"
    )

    $proc = Start-Process -FilePath 'cmd.exe' -ArgumentList $cmdArgs -PassThru -WindowStyle Hidden `
        -WorkingDirectory $Root `
        -RedirectStandardOutput $LauncherStdOut `
        -RedirectStandardError $LauncherStdErr

    return [pscustomobject]@{
        Process = $proc
        EnvironmentState = $oldState
    }
}

function Wait-ForGuestEvidence {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)]
        [string]$SerialLog,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [Parameter(Mandatory = $true)]
        [int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            break
        }

        $text = Read-LogText -Path $SerialLog
        if (-not [string]::IsNullOrWhiteSpace($text) -and $text -match $Pattern) {
            return $true
        }

        Start-Sleep -Milliseconds 250
    }

    return $false
}

function Parse-FramebufferSummary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Pattern
    )

    $match = [regex]::Match($Text, $Pattern)
    if (-not $match.Success) {
        return $null
    }

    return [pscustomobject]@{
        RawCount = [int]$match.Groups[1].Value
        UniqueCount = [int]$match.Groups[2].Value
        DuplicateCount = [int]$match.Groups[3].Value
        SuspiciousCount = [int]$match.Groups[4].Value
        ActiveRenderTargetCount = if ($match.Groups.Count -gt 5) { [int]$match.Groups[5].Value } else { 0 }
        DisabledCandidateCount = if ($match.Groups.Count -gt 6) { [int]$match.Groups[6].Value } else { 0 }
        Line = $match.Value
    }
}

function Get-MatchGroupValue {
    param(
        [Parameter(Mandatory = $true)]
        [System.Text.RegularExpressions.Match]$Match,
        [Parameter(Mandatory = $true)]
        [int]$GroupIndex,
        [string]$Fallback = ''
    )

    if ($Match -and $Match.Success -and $Match.Groups.Count -gt $GroupIndex) {
        return $Match.Groups[$GroupIndex].Value
    }

    return $Fallback
}

function Format-OptionalValue {
    param(
        [AllowNull()]
        [object]$Value
    )

    if ($null -eq $Value -or $Value -eq '') {
        return 'n/a'
    }

    return [string]$Value
}

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Backend,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [bool]$Condition,
        [Parameter(Mandatory = $true)]
        [string]$Detail
    )

    $status = if ($Condition) { 'PASS' } else { 'FAIL' }
    Write-Host ("[{0}] {1} - {2}" -f $Backend, $Name, $status)
    Write-Host ("       {0}" -f $Detail)
    if (-not $Condition) {
        throw ("[{0}] {1} failed: {2}" -f $Backend, $Name, $Detail)
    }
}

function Get-BackendSpec {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Backend
    )

    switch ($Backend.ToLowerInvariant()) {
        'std' {
            return [pscustomobject]@{
                Backend = 'std'
                Required = $true
                Supported = $true
                LauncherBackend = 'std'
                QemuArgs = '-vga std'
                ProbeNote = 'legacy VGA/Bochs-style framebuffer'
                SerialPattern = 'guideXOS UEFI Bootloader'
                WaitPattern = '\[KERNEL\] Framebuffer ready'
            }
        }
        'virtio-gpu' {
            return [pscustomobject]@{
                Backend = 'virtio-gpu'
                Required = $false
                Supported = $true
                LauncherBackend = 'virtio-gpu'
                QemuArgs = '-vga none -device virtio-gpu-pci,max_outputs=2'
                ProbeNote = 'virtio-gpu-pci diagnostic discovery probe'
                SerialPattern = 'guideXOS UEFI Bootloader'
                WaitPattern = '\[VIRTIO-GPU\] Probe complete: devices='
            }
        }
        'virtio-gpu-modern-only' {
            return [pscustomobject]@{
                Backend = 'virtio-gpu-modern-only'
                Required = $false
                Supported = (Test-QemuVirtioGpuModernOnlySupport)
                LauncherBackend = 'virtio-gpu-modern-only'
                QemuArgs = '-vga none -device virtio-gpu-pci,max_outputs=2,disable-legacy=on'
                ProbeNote = 'virtio-gpu-pci modern-only diagnostic probe (no rendering)'
                SerialPattern = 'guideXOS UEFI Bootloader'
                WaitPattern = '\[VIRTIO-GPU\] Probe complete: devices='
            }
        }
        'multimonitor' {
            return Get-BackendSpec -Backend 'virtio-gpu'
        }
        'virtio-vga' {
            return [pscustomobject]@{
                Backend = 'virtio-vga'
                Required = $false
                Supported = $true
                LauncherBackend = 'virtio-vga'
                QemuArgs = '-vga virtio'
                ProbeNote = 'virtio-vga diagnostic probe'
                SerialPattern = 'guideXOS UEFI Bootloader'
                WaitPattern = '\[KERNEL\] FramebufferCount='
            }
        }
        'virtio' {
            return Get-BackendSpec -Backend 'virtio-vga'
        }
        'qxl-vga' {
            return [pscustomobject]@{
                Backend = 'qxl-vga'
                Required = $false
                Supported = $true
                LauncherBackend = 'qxl-vga'
                QemuArgs = '-vga qxl -spice addr=127.0.0.1,port=5930,disable-ticketing=on'
                ProbeNote = 'qxl-vga diagnostic probe with SPICE server'
                SerialPattern = 'guideXOS UEFI Bootloader'
                WaitPattern = '\[KERNEL\] FramebufferCount='
            }
        }
        'qxl' {
            return Get-BackendSpec -Backend 'qxl-vga'
        }
        default {
            throw "Unsupported backend '$Backend'."
        }
    }
}

function Invoke-QemuDisplayProbeBackend {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Backend,
        [Parameter(Mandatory = $true)]
        [int]$TimeoutSeconds
    )

    $spec = Get-BackendSpec -Backend $Backend
    $backendName = $spec.Backend
    $backendRoot = Join-Path $RunRoot $backendName
    New-Item -ItemType Directory -Force -Path $backendRoot | Out-Null

    $launcherStdOut = Join-Path $backendRoot 'launcher.stdout.log'
    $launcherStdErr = Join-Path $backendRoot 'launcher.stderr.log'
    $serialLog = Join-Path $backendRoot 'serial.log'
    $summaryPath = Join-Path $backendRoot 'summary.txt'

    if (-not (Find-Qemu)) {
        throw 'qemu-system-x86_64 not found.'
    }

    if ($spec.PSObject.Properties.Name -contains 'Supported' -and -not $spec.Supported) {
        $supportReason = 'QEMU does not advertise disable-legacy=on for virtio-gpu-pci'
        $summaryLines = @(
            '[QemuDisplayProbeBackend]'
            'evidenceVersion=2'
            "backend=$backendName"
            "required=$($spec.Required.ToString().ToLowerInvariant())"
            "supported=false"
            "launched=false"
            "bootloaderSerialAppeared=false"
            "launcherExitCode=n/a"
            "timeoutSeconds=$TimeoutSeconds"
            "timestampUtc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
            "qemuArgs=$($spec.QemuArgs)"
            "probeNote=$($spec.ProbeNote)"
            "backendStatus=unsupported"
            "interpretation=$supportReason"
        )
        Set-Content -LiteralPath $summaryPath -Value $summaryLines -Encoding UTF8

        Write-Host ("[{0}] unsupported - {1}" -f $backendName, $supportReason)
        return [pscustomobject]@{
            Backend = $backendName
            BackendRoot = $backendRoot
            LauncherStdOut = $launcherStdOut
            LauncherStdErr = $launcherStdErr
            SerialLog = $serialLog
            SummaryPath = $summaryPath
            QemuArgs = $spec.QemuArgs
            ProbeNote = $spec.ProbeNote
            Required = $spec.Required
            Supported = $false
            Launched = $false
            BootloaderSerialAppeared = $false
            LauncherExitCode = $null
            BootSummary = $null
            KernelSummary = $null
            KernelActiveRenderTargetCount = $null
            KernelDisabledCandidateCount = $null
            BootGopHandles = 'n/a'
            BootFramebufferCount = $null
            BootUniqueFramebufferCount = $null
            BootDuplicateFramebufferCount = $null
            BootSuspiciousFramebufferCount = $null
            KernelFramebufferCount = $null
            KernelUniqueFramebufferCount = $null
            KernelDuplicateFramebufferCount = $null
            KernelSuspiciousFramebufferCount = $null
            BootPrimaryLine = ''
            BootSecondaryLine = ''
            BootRenderTargetLine = ''
            BootInvalidFramebufferLine = ''
            BootInvalidReason = 'n/a'
            KernelPrimaryLine = ''
            KernelSecondaryLine = ''
            KernelFramebufferReady = ''
            FramebufferReady = $false
            DesktopInventoryLine = ''
            DesktopSecondaryInventoryLine = ''
            GpuDiagnosticsCaptured = $false
            GpuProbeEnabledLine = ''
            GpuProbeStartLine = ''
            GpuCandidateLine = ''
            GpuCapabilityLine = ''
            GpuInventoryLine = ''
            GpuMmioReportLine = ''
            GpuMmioBlockedLine = ''
            GpuMmioSummaryLine = ''
            GpuMmioMappedLine = ''
            GpuResetStepLine = ''
            GpuGetDisplayInfoStepLine = ''
            GpuQueueLine = ''
            GpuDisplayInfoLine = ''
            GpuScanoutLine = ''
            GpuProbeCompleteLine = ''
            GpuTransportLine = ''
            GpuFeatureNegotiationLine = ''
            GpuQueueCountLine = ''
            GpuCapabilityWalkLine = ''
            DiagnosticStatus = 'unsupported'
            Interpretation = $supportReason
            LauncherStdOutText = ''
            LauncherStdErrText = ''
            SerialText = ''
        }
    }

    if (-not (Find-Ovmf)) {
        throw 'OVMF image not found.'
    }

    $esp = Join-Path $Root 'ESP'
    Assert-PathExists -Path $esp -Label 'ESP directory'
    Assert-PathExists -Path (Join-Path $esp 'EFI\BOOT\BOOTX64.EFI') -Label 'ESP\EFI\BOOT\BOOTX64.EFI'
    Assert-PathExists -Path (Join-Path $esp 'kernel.elf') -Label 'ESP\kernel.elf'
    Assert-PathExists -Path (Join-Path $esp 'ramdisk.img') -Label 'ESP\ramdisk.img'

    $envState = Save-EnvironmentState -Names @(
        'GXOS_QEMU_DISPLAY_PROBE_HEADLESS',
        'GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE',
        'GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG'
    )

    $proc = $null
    $launcherState = $null
    $sentinelSeen = $false
    try {
        $launcherState = Start-ProbeLauncher -Backend $spec.LauncherBackend -SerialLog $serialLog -LauncherStdOut $launcherStdOut -LauncherStdErr $launcherStdErr
        $proc = $launcherState.Process

        $sentinelSeen = Wait-ForGuestEvidence -Process $proc -SerialLog $serialLog -Pattern $spec.WaitPattern -TimeoutSeconds $TimeoutSeconds

        if (-not $sentinelSeen) {
            if (-not $proc.HasExited) {
                Stop-ProcessTree -Process $proc
                [void]$proc.WaitForExit(5000)
            }
        } else {
            Start-Sleep -Seconds 1
            if (-not $proc.HasExited) {
                Stop-ProcessTree -Process $proc
                [void]$proc.WaitForExit(5000)
            }
        }
    } finally {
        if ($launcherState -and $launcherState.EnvironmentState) {
            Restore-EnvironmentState -State $launcherState.EnvironmentState
        } elseif ($envState) {
            Restore-EnvironmentState -State $envState
        }

        if ($proc -and -not $proc.HasExited) {
            Stop-ProcessTree -Process $proc
            [void]$proc.WaitForExit(5000)
        }
    }

    $launcherExitCode = $null
    if ($proc) {
        try {
            $launcherExitCode = $proc.ExitCode
        } catch {
            $launcherExitCode = $null
        }
    }

    $serialText = Read-LogText -Path $serialLog
    $launcherStdOutText = Read-LogText -Path $launcherStdOut
    $launcherStdErrText = Read-LogText -Path $launcherStdErr

    $launchRecorded = (-not [string]::IsNullOrWhiteSpace($launcherStdOutText)) -and ($launcherStdOutText -match 'QEMU launch:')
    $bootloaderSerialAppeared = (-not [string]::IsNullOrWhiteSpace($serialText)) -and ($serialText -match $spec.SerialPattern)

    $bootGopLine = [regex]::Match($serialText, '\[BOOT\] GOP handles discovered: (\d+)')
    $bootSummary = Parse-FramebufferSummary -Text $serialText -Pattern '\[BOOT\] GOP FramebufferCount=(\d+) UniqueFramebufferCount=(\d+) DuplicateFramebufferCount=(\d+) SuspiciousFramebufferCount=(\d+)'
    $bootPrimaryLine = [regex]::Match($serialText, '\[BOOT\] FB\[0\].*status=.*primary.*selected')
    $bootSecondaryLine = [regex]::Match($serialText, '\[BOOT\] FB\[1\].*status=.*duplicate.*alias.*same-as-primary')
    $bootRenderTargetLine = [regex]::Match($serialText, 'Diagnostic framebuffer array exported .*primary remains render target')
    $bootInvalidFramebufferLine = [regex]::Match($serialText, '\[BOOT\] GOP selected framebuffer invalid; BootInfo array disabled')
    $bootInvalidReasonLine = [regex]::Match($serialText, '\[BOOT\] GOP selected framebuffer failure reason=([^\r\n]+)')
    $kernelSummary = Parse-FramebufferSummary -Text $serialText -Pattern '\[KERNEL\] FramebufferCount=(\d+) UniqueFramebufferCount=(\d+) DuplicateFramebufferCount=(\d+) SuspiciousFramebufferCount=(\d+) ActiveFramebufferTargetCount=(\d+) DisabledDiagnosticFramebufferCandidateCount=(\d+)'
    $kernelPrimaryLine = [regex]::Match($serialText, '\[KERNEL\] Framebuffer source=UEFI BootInfo framebufferCount=\d+ index=0 status=.*primary.*selected')
    $kernelSecondaryLine = [regex]::Match($serialText, '\[KERNEL\] Framebuffer source=UEFI BootInfo framebufferCount=\d+ index=1 status=.*duplicate.*alias.*same-as-primary')
    $kernelFramebufferReady = [regex]::Match($serialText, '\[KERNEL\] Framebuffer ready')
    $desktopInventoryLine = [regex]::Match($serialText, '\[desktop\] Framebuffer candidate\[0\] enabled=true primary=true source=UEFI GOP')
    $desktopSecondaryInventoryLine = [regex]::Match($serialText, '\[desktop\] Framebuffer candidate\[1\]')
    $gpuProbeEnabledLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Diagnostic probe enabled for QEMU virtio-gpu discovery')
    $gpuProbeStartLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Probing PCI bus for virtio-gpu devices')
    $gpuCandidateLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] PCI candidate [^\r\n]+')
    $gpuCapabilityWalkLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] PCI capability walk complete caps=\d+ vendorSpecific=\d+')
    $gpuInventoryLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Capability inventory common=[^\r\n]+')
    $gpuTransportLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Transport type detected: [^\r\n]+')
    $gpuMmioReportLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] MMIO mapping report common [^\r\n]+')
    $gpuMmioSummaryLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] MMIO transport summary mmioMapped=yes mappingVirtual=0x[0-9A-Fa-f]+ pageCount=\d+ cacheMode=uc\(pcd\+pwt\) sanityReads=ok stopReason=transport writes intentionally disabled')
    $gpuMmioMappedLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] MMIO transport mapped; read-only sanity reads complete; GET_DISPLAY_INFO remains disabled in this diagnostic pass')
    $gpuMmioBlockedLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] MMIO mapping blocked: [^\r\n]+')
    $gpuResetStepLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Init step: reset_device begin')
    $gpuGetDisplayInfoStepLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Init step: GET_DISPLAY_INFO begin')
    $gpuFeatureNegotiationLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Feature negotiation status=(ok|failed) negotiated=0x[0-9A-Fa-f]+ deviceFeatures=0x[0-9A-Fa-f]+')
    $gpuQueueCountLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Common config queueCount=\d+ controlQueueAvailable=(yes|no)')
    $gpuQueueLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Control queue ready size=[^\r\n]+')
    $gpuDisplayInfoLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Display info scanouts=\d+')
    $gpuScanoutLine = [regex]::Match($serialText, '\[VIRTIO-GPU\]\s+scanout\[\d+\].*')
    $gpuProbeCompleteLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Probe complete: devices=\d+ initialized=\d+ transport=[^\r\n]+ mmioMapped=yes mappingVirtual=0x[0-9A-Fa-f]+ pageCount=\d+ cacheMode=uc\(pcd\+pwt\) sanityReads=ok caps=[^\r\n]+ displayInfo=not-queried reason=transport writes intentionally disabled')

    $bootGopHandles = Format-OptionalValue -Value (Get-MatchGroupValue -Match $bootGopLine -GroupIndex 1)
    $bootFramebufferCount = if ($bootSummary) { $bootSummary.RawCount } else { $null }
    $bootUniqueFramebufferCount = if ($bootSummary) { $bootSummary.UniqueCount } else { $null }
    $bootDuplicateFramebufferCount = if ($bootSummary) { $bootSummary.DuplicateCount } else { $null }
    $bootSuspiciousFramebufferCount = if ($bootSummary) { $bootSummary.SuspiciousCount } else { $null }
    $kernelFramebufferCount = if ($kernelSummary) { $kernelSummary.RawCount } else { $null }
    $kernelUniqueFramebufferCount = if ($kernelSummary) { $kernelSummary.UniqueCount } else { $null }
    $kernelDuplicateFramebufferCount = if ($kernelSummary) { $kernelSummary.DuplicateCount } else { $null }
    $kernelSuspiciousFramebufferCount = if ($kernelSummary) { $kernelSummary.SuspiciousCount } else { $null }
    $kernelActiveRenderTargetCount = if ($kernelSummary) { $kernelSummary.ActiveRenderTargetCount } else { $null }
    $kernelDisabledCandidateCount = if ($kernelSummary) { $kernelSummary.DisabledCandidateCount } else { $null }
    $bootInvalidReason = Format-OptionalValue -Value (Get-MatchGroupValue -Match $bootInvalidReasonLine -GroupIndex 1)
    $gpuDiagnosticsCaptured = $gpuProbeEnabledLine.Success -or $gpuProbeStartLine.Success -or $gpuCandidateLine.Success -or $gpuCapabilityWalkLine.Success -or $gpuInventoryLine.Success -or $gpuTransportLine.Success -or $gpuMmioReportLine.Success -or $gpuMmioSummaryLine.Success -or $gpuMmioMappedLine.Success -or $gpuMmioBlockedLine.Success -or $gpuResetStepLine.Success -or $gpuGetDisplayInfoStepLine.Success -or $gpuFeatureNegotiationLine.Success -or $gpuQueueCountLine.Success -or $gpuQueueLine.Success -or $gpuDisplayInfoLine.Success -or $gpuScanoutLine.Success -or $gpuProbeCompleteLine.Success

    if ($spec.Required) {
        if ([string]::IsNullOrWhiteSpace($serialText)) {
            $detail = "No guest serial output was captured within $TimeoutSeconds seconds. launcherStdOut=$launcherStdOut launcherStdErr=$launcherStdErr"
            Write-Host ("[{0}] no guest serial output - FAIL" -f $backendName)
            Write-Host ("       {0}" -f $detail)
            throw $detail
        }

        Assert-Condition -Backend $backendName -Name 'bootloader GOP handles line' -Condition $bootGopLine.Success -Detail 'expected a GOP handle discovery line in bootloader serial output'
        Assert-Condition -Backend $backendName -Name 'bootloader framebuffer summary' -Condition ($null -ne $bootSummary) -Detail 'expected bootloader framebuffer summary line'
        Assert-Condition -Backend $backendName -Name 'bootloader primary descriptor' -Condition $bootPrimaryLine.Success -Detail 'descriptor 0 should be primary and selected'
        Assert-Condition -Backend $backendName -Name 'bootloader primary-render-target note' -Condition $bootRenderTargetLine.Success -Detail 'bootloader should say the primary remains the render target'
        Assert-Condition -Backend $backendName -Name 'kernel framebuffer summary' -Condition ($null -ne $kernelSummary) -Detail 'expected kernel framebuffer summary line'
        Assert-Condition -Backend $backendName -Name 'kernel primary descriptor' -Condition $kernelPrimaryLine.Success -Detail 'kernel should log descriptor 0 as primary and selected'
        Assert-Condition -Backend $backendName -Name 'kernel framebuffer ready' -Condition $kernelFramebufferReady.Success -Detail 'kernel should reach framebuffer-ready initialization'
        Assert-Condition -Backend $backendName -Name 'bootloader unique framebuffer count' -Condition ($bootSummary.UniqueCount -eq 1) -Detail ("line={0}" -f $bootSummary.Line)
        Assert-Condition -Backend $backendName -Name 'bootloader suspicious framebuffer count' -Condition ($bootSummary.SuspiciousCount -eq 0) -Detail ("line={0}" -f $bootSummary.Line)
        Assert-Condition -Backend $backendName -Name 'kernel unique framebuffer count' -Condition ($kernelSummary.UniqueCount -eq 1) -Detail ("line={0}" -f $kernelSummary.Line)
        Assert-Condition -Backend $backendName -Name 'kernel suspicious framebuffer count' -Condition ($kernelSummary.SuspiciousCount -eq 0) -Detail ("line={0}" -f $kernelSummary.Line)
        Assert-Condition -Backend $backendName -Name 'kernel active framebuffer target count' -Condition ($kernelSummary.ActiveRenderTargetCount -eq 1) -Detail ("line={0}" -f $kernelSummary.Line)
        Assert-Condition -Backend $backendName -Name 'kernel disabled diagnostic candidate count' -Condition ($kernelSummary.DisabledCandidateCount -eq 0) -Detail ("line={0}" -f $kernelSummary.Line)
        Assert-Condition -Backend $backendName -Name 'desktop unique candidate inventory' -Condition $desktopInventoryLine.Success -Detail 'desktop should log one enabled primary framebuffer candidate from the unique inventory'
        Assert-Condition -Backend $backendName -Name 'desktop duplicate handle not promoted' -Condition (-not $desktopSecondaryInventoryLine.Success) -Detail 'duplicate GOP handles must not become extra display candidates'
        Assert-Condition -Backend $backendName -Name 'summary counts mirror' -Condition (
            $bootSummary.RawCount -eq $kernelSummary.RawCount -and
            $bootSummary.UniqueCount -eq $kernelSummary.UniqueCount -and
            $bootSummary.DuplicateCount -eq $kernelSummary.DuplicateCount -and
            $bootSummary.SuspiciousCount -eq $kernelSummary.SuspiciousCount
        ) -Detail ("boot={0} kernel={1}" -f $bootSummary.Line, $kernelSummary.Line)

        if ($bootSummary.RawCount -eq 1) {
            Assert-Condition -Backend $backendName -Name 'single framebuffer duplicate count' -Condition ($bootSummary.DuplicateCount -eq 0) -Detail ("line={0}" -f $bootSummary.Line)
        } else {
            Assert-Condition -Backend $backendName -Name 'duplicate framebuffer count' -Condition ($bootSummary.DuplicateCount -ge 1) -Detail ("line={0}" -f $bootSummary.Line)
            Assert-Condition -Backend $backendName -Name 'kernel duplicate framebuffer count' -Condition ($kernelSummary.DuplicateCount -ge 1) -Detail ("line={0}" -f $kernelSummary.Line)
            Assert-Condition -Backend $backendName -Name 'bootloader duplicate descriptor 1' -Condition $bootSecondaryLine.Success -Detail 'descriptor 1 should be logged as duplicate alias same-as-primary'
            Assert-Condition -Backend $backendName -Name 'kernel duplicate descriptor 1' -Condition $kernelSecondaryLine.Success -Detail 'kernel should mirror descriptor 1 as duplicate alias same-as-primary'
            Assert-Condition -Backend $backendName -Name 'raw equals unique plus duplicate' -Condition (
                $bootSummary.RawCount -eq ($bootSummary.UniqueCount + $bootSummary.DuplicateCount) -and
                $kernelSummary.RawCount -eq ($kernelSummary.UniqueCount + $kernelSummary.DuplicateCount)
            ) -Detail ("boot={0} kernel={1}" -f $bootSummary.Line, $kernelSummary.Line)
        }
    }

    if ($backendName -like 'virtio-gpu*') {
        Assert-Condition -Backend $backendName -Name 'virtio-gpu probe enabled line' -Condition $gpuProbeEnabledLine.Success -Detail 'expected the diagnostic probe gate to be announced'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu probe start line' -Condition $gpuProbeStartLine.Success -Detail 'expected PCI probing to begin'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu PCI candidate line' -Condition $gpuCandidateLine.Success -Detail 'expected the QEMU virtio-gpu PCI function to be detected'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu capability walk line' -Condition $gpuCapabilityWalkLine.Success -Detail 'expected the full PCI capability walk to complete'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu inventory line' -Condition $gpuInventoryLine.Success -Detail 'expected the modern capability inventory summary'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu transport line' -Condition $gpuTransportLine.Success -Detail 'expected the transport type to be logged before MMIO mapping'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu MMIO report line' -Condition $gpuMmioReportLine.Success -Detail 'expected a compact MMIO mapping report for the common config region'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu MMIO summary line' -Condition $gpuMmioSummaryLine.Success -Detail 'expected the mapped MMIO transport summary in the serial log'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu MMIO mapped milestone line' -Condition $gpuMmioMappedLine.Success -Detail 'expected the read-only MMIO milestone line in the serial log'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu MMIO report fields' -Condition (
            $gpuMmioReportLine.Success -and
            $gpuMmioReportLine.Value -match 'requestBase=0x[0-9A-Fa-f]+' -and
            $gpuMmioReportLine.Value -match 'requestLength=0x[0-9A-Fa-f]+' -and
            $gpuMmioReportLine.Value -match 'alignedBase=0x[0-9A-Fa-f]+' -and
            $gpuMmioReportLine.Value -match 'alignedLength=0x[0-9A-Fa-f]+' -and
            $gpuMmioReportLine.Value -match 'mappedLength=0x[0-9A-Fa-f]+' -and
            $gpuMmioReportLine.Value -match 'pages=\d+' -and
            $gpuMmioReportLine.Value -match 'kernelVirtualBase=(n/a|0x[0-9A-Fa-f]+)' -and
            $gpuMmioReportLine.Value -match 'mappedVirtual=(n/a|0x[0-9A-Fa-f]+)' -and
            $gpuMmioReportLine.Value -match 'flags=0x[0-9A-Fa-f]+' -and
            $gpuMmioReportLine.Value -match 'nonUser=yes' -and
            $gpuMmioReportLine.Value -match 'noExec=yes' -and
            $gpuMmioReportLine.Value -match 'uncached=yes' -and
            $gpuMmioReportLine.Value -match 'cacheAttrs=ok' -and
            $gpuMmioReportLine.Value -match 'cacheMode=uc\(pcd\+pwt\)' -and
            $gpuMmioReportLine.Value -match 'qemuProbeOnly=yes' -and
            $gpuMmioReportLine.Value -match 'pageAligned=(yes|no)' -and
            $gpuMmioReportLine.Value -match 'windowEligible=yes' -and
            $gpuMmioReportLine.Value -match 'requiresNewPageTableEntries=(yes|no)' -and
            $gpuMmioReportLine.Value -match 'success=yes' -and
            $gpuMmioReportLine.Value -match 'reason=mapped into reserved kernel MMIO window' -and
            $gpuMmioReportLine.Value -match 'nextFeature=controlled feature negotiation'
        ) -Detail ($gpuMmioReportLine.Value)
        Assert-Condition -Backend $backendName -Name 'virtio-gpu blocker absent' -Condition (-not $gpuMmioBlockedLine.Success) -Detail 'probe should not emit a blocker line once the transport mapping succeeds'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu transport reset suppressed' -Condition (-not $gpuResetStepLine.Success) -Detail 'probe should not write the transport reset register'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu feature negotiation suppressed' -Condition (-not $gpuFeatureNegotiationLine.Success) -Detail 'probe must stop before feature negotiation'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu queue setup suppressed' -Condition (-not $gpuQueueCountLine.Success -and -not $gpuQueueLine.Success) -Detail 'probe must stop before control-queue layout'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu GET_DISPLAY_INFO suppressed' -Condition (-not $gpuGetDisplayInfoStepLine.Success -and -not $gpuDisplayInfoLine.Success -and -not $gpuScanoutLine.Success) -Detail 'probe must stop before display-info and scanout activity'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu mapping milestone stop' -Condition ($gpuMmioSummaryLine.Success -and $gpuMmioMappedLine.Success) -Detail 'probe should end at the mapped MMIO transport milestone'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu probe completion line' -Condition $gpuProbeCompleteLine.Success -Detail 'expected the final probe summary line'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu probe completion fields' -Condition (
            $gpuProbeCompleteLine.Value -match 'mmioMapped=yes' -and
            $gpuProbeCompleteLine.Value -match 'mappingVirtual=0x[0-9A-Fa-f]+' -and
            $gpuProbeCompleteLine.Value -match 'pageCount=\d+' -and
            $gpuProbeCompleteLine.Value -match 'cacheMode=uc\(pcd\+pwt\)' -and
            $gpuProbeCompleteLine.Value -match 'sanityReads=ok' -and
            $gpuProbeCompleteLine.Value -match 'displayInfo=not-queried' -and
            $gpuProbeCompleteLine.Value -match 'reason=transport writes intentionally disabled'
        ) -Detail $gpuProbeCompleteLine.Value
        $backendStatus = 'complete'
        $interpretation = 'QEMU virtio-gpu MMIO transport mapped and read-only sanity reads completed'
    }

    $launched = $launchRecorded
    $bootloaderSerialCaptured = $bootloaderSerialAppeared
    $framebufferReady = $kernelFramebufferReady.Success

    $backendStatus = 'unsupported'
    if ($spec.Required) {
        $backendStatus = 'validated'
    } elseif ($bootloaderSerialCaptured) {
        if ($bootSummary -and $kernelSummary) {
            $backendStatus = if ($framebufferReady) { 'complete' } else { 'partial' }
        } elseif ($bootSummary -or $kernelSummary) {
            $backendStatus = 'partial'
        } else {
            $backendStatus = 'partial'
        }
    }

    $interpretation = 'no framebuffer evidence captured'
    if ($bootSummary) {
        if ($bootSummary.UniqueCount -gt 1) {
            $interpretation = 'more than one unique framebuffer candidate exposed through current GOP handoff'
        } elseif ($bootSummary.UniqueCount -eq 1 -and $bootSummary.DuplicateCount -ge 1) {
            $interpretation = 'single unique framebuffer candidate with duplicate aliases'
        } elseif ($bootSummary.UniqueCount -eq 1) {
            $interpretation = 'single unique framebuffer candidate and no duplicates'
        } elseif ($bootSummary.RawCount -eq 0) {
            $interpretation = 'GOP framebuffer export disabled'
        }
    } elseif ($bootInvalidReason -ne 'n/a') {
        $interpretation = "selected GOP framebuffer rejected: $bootInvalidReason"
    } elseif ($bootloaderSerialCaptured) {
        $interpretation = 'bootloader reached serial output but no framebuffer summary was captured'
    }

    $launcherStdOutTail = (Get-LogTail -Path $launcherStdOut -LineCount 12) -replace "`r", ''
    $launcherStdErrTail = (Get-LogTail -Path $launcherStdErr -LineCount 12) -replace "`r", ''
    $serialTail = (Get-LogTail -Path $serialLog -LineCount 40) -replace "`r", ''

    $summaryLines = @(
        '[QemuDisplayProbeBackend]'
        'evidenceVersion=2'
        "backend=$backendName"
        "required=$($spec.Required.ToString().ToLowerInvariant())"
        "supported=$($spec.Supported.ToString().ToLowerInvariant())"
        "launched=$($launched.ToString().ToLowerInvariant())"
        "bootloaderSerialAppeared=$($bootloaderSerialCaptured.ToString().ToLowerInvariant())"
        "launcherExitCode=$(Format-OptionalValue -Value $launcherExitCode)"
        "timeoutSeconds=$TimeoutSeconds"
        "timestampUtc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
        "qemuArgs=$($spec.QemuArgs)"
        "probeNote=$($spec.ProbeNote)"
        "bootGopHandles=$bootGopHandles"
        "bootFramebufferCount=$(Format-OptionalValue -Value $bootFramebufferCount)"
        "bootUniqueFramebufferCount=$(Format-OptionalValue -Value $bootUniqueFramebufferCount)"
        "bootDuplicateFramebufferCount=$(Format-OptionalValue -Value $bootDuplicateFramebufferCount)"
        "bootSuspiciousFramebufferCount=$(Format-OptionalValue -Value $bootSuspiciousFramebufferCount)"
        "kernelFramebufferCount=$(Format-OptionalValue -Value $kernelFramebufferCount)"
        "kernelUniqueFramebufferCount=$(Format-OptionalValue -Value $kernelUniqueFramebufferCount)"
        "kernelDuplicateFramebufferCount=$(Format-OptionalValue -Value $kernelDuplicateFramebufferCount)"
        "kernelSuspiciousFramebufferCount=$(Format-OptionalValue -Value $kernelSuspiciousFramebufferCount)"
        "kernelActiveRenderTargetCount=$(Format-OptionalValue -Value $kernelActiveRenderTargetCount)"
        "kernelDisabledCandidateCount=$(Format-OptionalValue -Value $kernelDisabledCandidateCount)"
        "framebufferReady=$($framebufferReady.ToString().ToLowerInvariant())"
        "status=$backendStatus"
        "interpretation=$interpretation"
        "bootInvalidReason=$bootInvalidReason"
        "bootPrimaryLine=$($bootPrimaryLine.Value)"
        "bootSecondaryLine=$($bootSecondaryLine.Value)"
        "bootRenderTargetLine=$($bootRenderTargetLine.Value)"
        "bootInvalidFramebufferLine=$($bootInvalidFramebufferLine.Value)"
        "kernelPrimaryLine=$($kernelPrimaryLine.Value)"
        "kernelSecondaryLine=$($kernelSecondaryLine.Value)"
        "desktopInventoryLine=$($desktopInventoryLine.Value)"
        "desktopSecondaryInventoryLine=$($desktopSecondaryInventoryLine.Value)"
        "gpuDiagnosticsCaptured=$($gpuDiagnosticsCaptured.ToString().ToLowerInvariant())"
        "gpuProbeEnabledLine=$($gpuProbeEnabledLine.Value)"
        "gpuProbeStartLine=$($gpuProbeStartLine.Value)"
        "gpuCandidateLine=$($gpuCandidateLine.Value)"
        "gpuCapabilityWalkLine=$($gpuCapabilityWalkLine.Value)"
        "gpuInventoryLine=$($gpuInventoryLine.Value)"
        "gpuTransportLine=$($gpuTransportLine.Value)"
        "gpuMmioReportLine=$($gpuMmioReportLine.Value)"
        "gpuMmioSummaryLine=$($gpuMmioSummaryLine.Value)"
        "gpuMmioMappedLine=$($gpuMmioMappedLine.Value)"
        "gpuMmioBlockedLine=$($gpuMmioBlockedLine.Value)"
        "gpuResetStepLine=$($gpuResetStepLine.Value)"
        "gpuGetDisplayInfoStepLine=$($gpuGetDisplayInfoStepLine.Value)"
        "gpuFeatureNegotiationLine=$($gpuFeatureNegotiationLine.Value)"
        "gpuQueueCountLine=$($gpuQueueCountLine.Value)"
        "gpuQueueLine=$($gpuQueueLine.Value)"
        "gpuDisplayInfoLine=$($gpuDisplayInfoLine.Value)"
        "gpuScanoutLine=$($gpuScanoutLine.Value)"
        "gpuProbeCompleteLine=$($gpuProbeCompleteLine.Value)"
        "launcherStdOut=$launcherStdOut"
        "launcherStdErr=$launcherStdErr"
        "serialLog=$serialLog"
        "launcherStdOutTail=$launcherStdOutTail"
        "launcherStdErrTail=$launcherStdErrTail"
        "serialTail=$serialTail"
    )
    Set-Content -LiteralPath $summaryPath -Value $summaryLines -Encoding UTF8

    Write-Host ("[{0}] launched={1} serial={2} status={3}" -f $backendName, $launched, $bootloaderSerialCaptured, $backendStatus)
    if ($bootSummary) {
        Write-Host ("[{0}] boot summary: {1}" -f $backendName, $bootSummary.Line)
    }
    if ($kernelSummary) {
        Write-Host ("[{0}] kernel summary: {1}" -f $backendName, $kernelSummary.Line)
    }
    if ($bootInvalidReason -ne 'n/a') {
        Write-Host ("[{0}] bootloader framebuffer rejection: {1}" -f $backendName, $bootInvalidReason)
    }
    if ($framebufferReady) {
        Write-Host ("[{0}] kernel framebuffer-ready marker observed" -f $backendName)
    }
    if ($backendName -like 'virtio-gpu*' -and $gpuDiagnosticsCaptured) {
        Write-Host ("[{0}] virtio-gpu diagnostics observed in serial log" -f $backendName)
    }
    if ($interpretation) {
        Write-Host ("[{0}] interpretation: {1}" -f $backendName, $interpretation)
    }

    return [pscustomobject]@{
        Backend = $backendName
        BackendRoot = $backendRoot
        LauncherStdOut = $launcherStdOut
        LauncherStdErr = $launcherStdErr
        SerialLog = $serialLog
        SummaryPath = $summaryPath
        QemuArgs = $spec.QemuArgs
        ProbeNote = $spec.ProbeNote
        Required = $spec.Required
        Launched = $launched
        BootloaderSerialAppeared = $bootloaderSerialCaptured
        LauncherExitCode = $launcherExitCode
        BootSummary = $bootSummary
        KernelSummary = $kernelSummary
        KernelActiveRenderTargetCount = $kernelActiveRenderTargetCount
        KernelDisabledCandidateCount = $kernelDisabledCandidateCount
        BootGopHandles = $bootGopHandles
        BootFramebufferCount = $bootFramebufferCount
        BootUniqueFramebufferCount = $bootUniqueFramebufferCount
        BootDuplicateFramebufferCount = $bootDuplicateFramebufferCount
        BootSuspiciousFramebufferCount = $bootSuspiciousFramebufferCount
        KernelFramebufferCount = $kernelFramebufferCount
        KernelUniqueFramebufferCount = $kernelUniqueFramebufferCount
        KernelDuplicateFramebufferCount = $kernelDuplicateFramebufferCount
        KernelSuspiciousFramebufferCount = $kernelSuspiciousFramebufferCount
        BootPrimaryLine = $bootPrimaryLine.Value
        BootSecondaryLine = $bootSecondaryLine.Value
        BootRenderTargetLine = $bootRenderTargetLine.Value
        BootInvalidFramebufferLine = $bootInvalidFramebufferLine.Value
        BootInvalidReason = $bootInvalidReason
        KernelPrimaryLine = $kernelPrimaryLine.Value
        KernelSecondaryLine = $kernelSecondaryLine.Value
        KernelFramebufferReady = $kernelFramebufferReady.Value
        FramebufferReady = $framebufferReady
        DesktopInventoryLine = $desktopInventoryLine.Value
        DesktopSecondaryInventoryLine = $desktopSecondaryInventoryLine.Value
        GpuDiagnosticsCaptured = $gpuDiagnosticsCaptured
        GpuProbeEnabledLine = $gpuProbeEnabledLine.Value
        GpuProbeStartLine = $gpuProbeStartLine.Value
        GpuCandidateLine = $gpuCandidateLine.Value
        GpuCapabilityWalkLine = $gpuCapabilityWalkLine.Value
        GpuInventoryLine = $gpuInventoryLine.Value
        GpuTransportLine = $gpuTransportLine.Value
        GpuMmioReportLine = $gpuMmioReportLine.Value
        GpuMmioSummaryLine = $gpuMmioSummaryLine.Value
        GpuMmioMappedLine = $gpuMmioMappedLine.Value
        GpuMmioBlockedLine = $gpuMmioBlockedLine.Value
        GpuResetStepLine = $gpuResetStepLine.Value
        GpuGetDisplayInfoStepLine = $gpuGetDisplayInfoStepLine.Value
        GpuFeatureNegotiationLine = $gpuFeatureNegotiationLine.Value
        GpuQueueCountLine = $gpuQueueCountLine.Value
        GpuQueueLine = $gpuQueueLine.Value
        GpuDisplayInfoLine = $gpuDisplayInfoLine.Value
        GpuScanoutLine = $gpuScanoutLine.Value
        GpuProbeCompleteLine = $gpuProbeCompleteLine.Value
        Supported = $spec.Supported
        DiagnosticStatus = $backendStatus
        Interpretation = $interpretation
        LauncherStdOutText = $launcherStdOutText
        LauncherStdErrText = $launcherStdErrText
        SerialText = $serialText
    }
}

try {
    Write-Host '[build] rebuilding kernel with virtio-gpu diagnostic probe enabled'
    Invoke-KernelBuildForSmoke -ExtraCFlags '-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE'
    $script:activeSmokeBuild = $true

    $results = @()
    foreach ($backend in $Backends) {
        Write-Host ("[{0}] launching display probe smoke" -f $backend)
        $result = Invoke-QemuDisplayProbeBackend -Backend $backend -TimeoutSeconds $TimeoutSeconds
        $results += $result
        Write-Host ("[{0}] completed. serial={1}" -f $backend, $result.SerialLog)
    }

    $evidencePath = Join-Path $RunRoot 'qemu-display-probe.evidence.txt'
    $evidenceLines = @(
        '[QemuDisplayProbeSmoke]',
        'evidenceVersion=1',
        "timestampUtc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "repoRoot=$Root",
        "runRoot=$RunRoot",
        "backends=$($Backends -join ',')"
    )

    foreach ($result in $results) {
        $evidenceLines += ''
        $evidenceLines += "[backend $($result.Backend)]"
        $evidenceLines += "required=$($result.Required.ToString().ToLowerInvariant())"
        $evidenceLines += "supported=$($result.Supported.ToString().ToLowerInvariant())"
        $evidenceLines += "qemuArgs=$($result.QemuArgs)"
        $evidenceLines += "probeNote=$($result.ProbeNote)"
        $evidenceLines += "launched=$($result.Launched.ToString().ToLowerInvariant())"
        $evidenceLines += "bootloaderSerialAppeared=$($result.BootloaderSerialAppeared.ToString().ToLowerInvariant())"
        $evidenceLines += "launcherExitCode=$(Format-OptionalValue -Value $result.LauncherExitCode)"
        $evidenceLines += "diagnosticStatus=$($result.DiagnosticStatus)"
        $evidenceLines += "interpretation=$($result.Interpretation)"
        $evidenceLines += "bootGopHandles=$($result.BootGopHandles)"
        $evidenceLines += "bootFramebufferCount=$(Format-OptionalValue -Value $result.BootFramebufferCount)"
        $evidenceLines += "bootUniqueFramebufferCount=$(Format-OptionalValue -Value $result.BootUniqueFramebufferCount)"
        $evidenceLines += "bootDuplicateFramebufferCount=$(Format-OptionalValue -Value $result.BootDuplicateFramebufferCount)"
        $evidenceLines += "bootSuspiciousFramebufferCount=$(Format-OptionalValue -Value $result.BootSuspiciousFramebufferCount)"
        $evidenceLines += "kernelFramebufferCount=$(Format-OptionalValue -Value $result.KernelFramebufferCount)"
        $evidenceLines += "kernelUniqueFramebufferCount=$(Format-OptionalValue -Value $result.KernelUniqueFramebufferCount)"
        $evidenceLines += "kernelDuplicateFramebufferCount=$(Format-OptionalValue -Value $result.KernelDuplicateFramebufferCount)"
        $evidenceLines += "kernelSuspiciousFramebufferCount=$(Format-OptionalValue -Value $result.KernelSuspiciousFramebufferCount)"
        $evidenceLines += "kernelActiveRenderTargetCount=$(Format-OptionalValue -Value $result.KernelActiveRenderTargetCount)"
        $evidenceLines += "kernelDisabledCandidateCount=$(Format-OptionalValue -Value $result.KernelDisabledCandidateCount)"
        $evidenceLines += "framebufferReady=$($result.FramebufferReady.ToString().ToLowerInvariant())"
        $evidenceLines += "bootInvalidReason=$($result.BootInvalidReason)"
        $evidenceLines += "bootSummaryLine=$(Format-OptionalValue -Value ($result.BootSummary.Line))"
        $evidenceLines += "kernelSummaryLine=$(Format-OptionalValue -Value ($result.KernelSummary.Line))"
        $evidenceLines += "bootPrimaryLine=$($result.BootPrimaryLine)"
        $evidenceLines += "bootSecondaryLine=$($result.BootSecondaryLine)"
        $evidenceLines += "bootRenderTargetLine=$($result.BootRenderTargetLine)"
        $evidenceLines += "bootInvalidFramebufferLine=$($result.BootInvalidFramebufferLine)"
        $evidenceLines += "kernelPrimaryLine=$($result.KernelPrimaryLine)"
        $evidenceLines += "kernelSecondaryLine=$($result.KernelSecondaryLine)"
        $evidenceLines += "desktopInventoryLine=$($result.DesktopInventoryLine)"
        $evidenceLines += "desktopSecondaryInventoryLine=$($result.DesktopSecondaryInventoryLine)"
        $evidenceLines += "gpuDiagnosticsCaptured=$($result.GpuDiagnosticsCaptured.ToString().ToLowerInvariant())"
        $evidenceLines += "gpuProbeEnabledLine=$($result.GpuProbeEnabledLine)"
        $evidenceLines += "gpuProbeStartLine=$($result.GpuProbeStartLine)"
        $evidenceLines += "gpuCandidateLine=$($result.GpuCandidateLine)"
        $evidenceLines += "gpuCapabilityWalkLine=$($result.GpuCapabilityWalkLine)"
        $evidenceLines += "gpuInventoryLine=$($result.GpuInventoryLine)"
        $evidenceLines += "gpuTransportLine=$($result.GpuTransportLine)"
        $evidenceLines += "gpuMmioReportLine=$($result.GpuMmioReportLine)"
        $evidenceLines += "gpuMmioSummaryLine=$($result.GpuMmioSummaryLine)"
        $evidenceLines += "gpuMmioMappedLine=$($result.GpuMmioMappedLine)"
        $evidenceLines += "gpuMmioBlockedLine=$($result.GpuMmioBlockedLine)"
        $evidenceLines += "gpuResetStepLine=$($result.GpuResetStepLine)"
        $evidenceLines += "gpuGetDisplayInfoStepLine=$($result.GpuGetDisplayInfoStepLine)"
        $evidenceLines += "gpuFeatureNegotiationLine=$($result.GpuFeatureNegotiationLine)"
        $evidenceLines += "gpuQueueCountLine=$($result.GpuQueueCountLine)"
        $evidenceLines += "gpuQueueLine=$($result.GpuQueueLine)"
        $evidenceLines += "gpuDisplayInfoLine=$($result.GpuDisplayInfoLine)"
        $evidenceLines += "gpuScanoutLine=$($result.GpuScanoutLine)"
        $evidenceLines += "gpuProbeCompleteLine=$($result.GpuProbeCompleteLine)"
        $evidenceLines += "launcherStdOut=$($result.LauncherStdOut)"
        $evidenceLines += "launcherStdErr=$($result.LauncherStdErr)"
        $evidenceLines += "serialLog=$($result.SerialLog)"
        $evidenceLines += "summaryPath=$($result.SummaryPath)"
    }

    Set-Content -LiteralPath $evidencePath -Value $evidenceLines -Encoding UTF8

    Write-Host 'QEMU display probe smoke passed.'
    Write-Host ("Evidence: {0}" -f $evidencePath)
    foreach ($result in $results) {
        Write-Host ("[{0}] status={1} launched={2} serial={3}" -f $result.Backend, $result.DiagnosticStatus, $result.Launched, $result.BootloaderSerialAppeared)
        Write-Host ("[{0}] summary={1}" -f $result.Backend, $result.SummaryPath)
    }
} finally {
    Restore-NormalKernelBuild
}
