param(
    [string[]]$Backends = @('std'),
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogRoot = Join-Path $Root 'logs'
New-Item -ItemType Directory -Force -Path $LogRoot | Out-Null

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunRoot = Join-Path $LogRoot ("qemu-display-probe-" + $stamp)
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null

$AllowedBackends = @('std', 'virtio-gpu')
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

function Invoke-QemuDisplayProbeBackend {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Backend,
        [Parameter(Mandatory = $true)]
        [int]$TimeoutSeconds
    )

    $backendRoot = Join-Path $RunRoot $Backend
    New-Item -ItemType Directory -Force -Path $backendRoot | Out-Null

    $launcherStdOut = Join-Path $backendRoot 'launcher.stdout.log'
    $launcherStdErr = Join-Path $backendRoot 'launcher.stderr.log'
    $serialLog = Join-Path $backendRoot 'serial.log'
    $summaryPath = Join-Path $backendRoot 'summary.txt'

    $qemu = Find-Qemu
    if (-not $qemu) {
        throw 'qemu-system-x86_64 not found.'
    }

    $ovmf = Find-Ovmf
    if (-not $ovmf) {
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
        $launcherState = Start-ProbeLauncher -Backend $Backend -SerialLog $serialLog -LauncherStdOut $launcherStdOut -LauncherStdErr $launcherStdErr
        $proc = $launcherState.Process

        $sentinelPattern = if ($Backend -eq 'std') { '\[KERNEL\] Framebuffer ready' } else { '\[KERNEL\] FramebufferCount=' }
        $sentinelSeen = Wait-ForGuestEvidence -Process $proc -SerialLog $serialLog -Pattern $sentinelPattern -TimeoutSeconds $TimeoutSeconds

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

    $serialText = Read-LogText -Path $serialLog
    $launcherStdOutText = Read-LogText -Path $launcherStdOut
    $launcherStdErrText = Read-LogText -Path $launcherStdErr

    if ([string]::IsNullOrWhiteSpace($serialText)) {
        $detail = "No guest serial output was captured within $TimeoutSeconds seconds. launcherStdOut=$launcherStdOut launcherStdErr=$launcherStdErr"
        Write-Host ("[{0}] no guest serial output - FAIL" -f $Backend)
        Write-Host ("       {0}" -f $detail)
        throw $detail
    }

    $bootGopLine = [regex]::Match($serialText, '\[BOOT\] GOP handles discovered: (\d+)')
    $bootSummary = Parse-FramebufferSummary -Text $serialText -Pattern '\[BOOT\] GOP FramebufferCount=(\d+) UniqueFramebufferCount=(\d+) DuplicateFramebufferCount=(\d+) SuspiciousFramebufferCount=(\d+)'
    $bootPrimaryLine = [regex]::Match($serialText, '\[BOOT\] FB\[0\].*status=.*primary.*selected')
    $bootSecondaryLine = [regex]::Match($serialText, '\[BOOT\] FB\[1\].*status=.*duplicate.*alias.*same-as-primary')
    $bootRenderTargetLine = [regex]::Match($serialText, 'Diagnostic framebuffer array exported .*primary remains render target')
    $bootInvalidFramebufferLine = [regex]::Match($serialText, '\[BOOT\] GOP selected framebuffer invalid; BootInfo array disabled')
    $kernelSummary = Parse-FramebufferSummary -Text $serialText -Pattern '\[KERNEL\] FramebufferCount=(\d+) UniqueFramebufferCount=(\d+) DuplicateFramebufferCount=(\d+) SuspiciousFramebufferCount=(\d+) ActiveFramebufferTargetCount=(\d+) DisabledDiagnosticFramebufferCandidateCount=(\d+)'
    $kernelPrimaryLine = [regex]::Match($serialText, '\[KERNEL\] Framebuffer source=UEFI BootInfo framebufferCount=\d+ index=0 status=.*primary.*selected')
    $kernelSecondaryLine = [regex]::Match($serialText, '\[KERNEL\] Framebuffer source=UEFI BootInfo framebufferCount=\d+ index=1 status=.*duplicate.*alias.*same-as-primary')
    $kernelFramebufferReady = [regex]::Match($serialText, '\[KERNEL\] Framebuffer ready')
    $desktopInventoryLine = [regex]::Match($serialText, '\[desktop\] Framebuffer candidate\[0\] enabled=true primary=true source=UEFI GOP')
    $desktopSecondaryInventoryLine = [regex]::Match($serialText, '\[desktop\] Framebuffer candidate\[1\]')

    if ($Backend -eq 'std') {
        Assert-Condition -Backend $Backend -Name 'bootloader GOP handles line' -Condition $bootGopLine.Success -Detail 'expected a GOP handle discovery line in bootloader serial output'
        Assert-Condition -Backend $Backend -Name 'bootloader framebuffer summary' -Condition ($null -ne $bootSummary) -Detail 'expected bootloader framebuffer summary line'
        Assert-Condition -Backend $Backend -Name 'bootloader primary descriptor' -Condition $bootPrimaryLine.Success -Detail 'descriptor 0 should be primary and selected'
        Assert-Condition -Backend $Backend -Name 'bootloader primary-render-target note' -Condition $bootRenderTargetLine.Success -Detail 'bootloader should say the primary remains the render target'
        Assert-Condition -Backend $Backend -Name 'kernel framebuffer summary' -Condition ($null -ne $kernelSummary) -Detail 'expected kernel framebuffer summary line'
        Assert-Condition -Backend $Backend -Name 'kernel primary descriptor' -Condition $kernelPrimaryLine.Success -Detail 'kernel should log descriptor 0 as primary and selected'
        Assert-Condition -Backend $Backend -Name 'kernel framebuffer ready' -Condition $kernelFramebufferReady.Success -Detail 'kernel should reach framebuffer-ready initialization'
        Assert-Condition -Backend $Backend -Name 'bootloader unique framebuffer count' -Condition ($bootSummary.UniqueCount -eq 1) -Detail ("line={0}" -f $bootSummary.Line)
        Assert-Condition -Backend $Backend -Name 'bootloader suspicious framebuffer count' -Condition ($bootSummary.SuspiciousCount -eq 0) -Detail ("line={0}" -f $bootSummary.Line)
        Assert-Condition -Backend $Backend -Name 'kernel unique framebuffer count' -Condition ($kernelSummary.UniqueCount -eq 1) -Detail ("line={0}" -f $kernelSummary.Line)
        Assert-Condition -Backend $Backend -Name 'kernel suspicious framebuffer count' -Condition ($kernelSummary.SuspiciousCount -eq 0) -Detail ("line={0}" -f $kernelSummary.Line)
        Assert-Condition -Backend $Backend -Name 'kernel active framebuffer target count' -Condition ($kernelSummary.ActiveRenderTargetCount -eq 1) -Detail ("line={0}" -f $kernelSummary.Line)
        Assert-Condition -Backend $Backend -Name 'kernel disabled diagnostic candidate count' -Condition ($kernelSummary.DisabledCandidateCount -eq 0) -Detail ("line={0}" -f $kernelSummary.Line)
        Assert-Condition -Backend $Backend -Name 'desktop unique candidate inventory' -Condition $desktopInventoryLine.Success -Detail 'desktop should log one enabled primary framebuffer candidate from the unique inventory'
        Assert-Condition -Backend $Backend -Name 'desktop duplicate handle not promoted' -Condition (-not $desktopSecondaryInventoryLine.Success) -Detail 'duplicate GOP handles must not become extra display candidates'
        Assert-Condition -Backend $Backend -Name 'summary counts mirror' -Condition (
            $bootSummary.RawCount -eq $kernelSummary.RawCount -and
            $bootSummary.UniqueCount -eq $kernelSummary.UniqueCount -and
            $bootSummary.DuplicateCount -eq $kernelSummary.DuplicateCount -and
            $bootSummary.SuspiciousCount -eq $kernelSummary.SuspiciousCount
        ) -Detail ("boot={0} kernel={1}" -f $bootSummary.Line, $kernelSummary.Line)

        if ($bootSummary.RawCount -eq 1) {
            Assert-Condition -Backend $Backend -Name 'single framebuffer duplicate count' -Condition ($bootSummary.DuplicateCount -eq 0) -Detail ("line={0}" -f $bootSummary.Line)
        } else {
            Assert-Condition -Backend $Backend -Name 'duplicate framebuffer count' -Condition ($bootSummary.DuplicateCount -ge 1) -Detail ("line={0}" -f $bootSummary.Line)
            Assert-Condition -Backend $Backend -Name 'kernel duplicate framebuffer count' -Condition ($kernelSummary.DuplicateCount -ge 1) -Detail ("line={0}" -f $kernelSummary.Line)
            Assert-Condition -Backend $Backend -Name 'bootloader duplicate descriptor 1' -Condition $bootSecondaryLine.Success -Detail 'descriptor 1 should be logged as duplicate alias same-as-primary'
            Assert-Condition -Backend $Backend -Name 'kernel duplicate descriptor 1' -Condition $kernelSecondaryLine.Success -Detail 'kernel should mirror descriptor 1 as duplicate alias same-as-primary'
            Assert-Condition -Backend $Backend -Name 'raw equals unique plus duplicate' -Condition (
                $bootSummary.RawCount -eq ($bootSummary.UniqueCount + $bootSummary.DuplicateCount) -and
                $kernelSummary.RawCount -eq ($kernelSummary.UniqueCount + $kernelSummary.DuplicateCount)
            ) -Detail ("boot={0} kernel={1}" -f $bootSummary.Line, $kernelSummary.Line)
        }
    } else {
        $bootSummaryLine = if ($bootSummary) { $bootSummary.Line } else { '(missing)' }
        $kernelSummaryLine = if ($kernelSummary) { $kernelSummary.Line } else { '(missing)' }
        $diagnosticStatus = 'partial'
        if ($bootGopLine.Success -and $bootSummary -and $kernelSummary) {
            $diagnosticStatus = 'complete'
        } elseif ($bootGopLine.Success -or $bootSummary -or $kernelSummary) {
            $diagnosticStatus = 'partial'
        } else {
            $diagnosticStatus = 'no-framebuffer-evidence'
        }
        Write-Host ("[{0}] diagnostic capture status: {1}" -f $Backend, $diagnosticStatus)
        if ($bootGopLine.Success) {
            Write-Host ("[{0}] bootloader GOP handles: {1}" -f $Backend, $bootGopLine.Groups[1].Value)
        }
        if ($bootSummary) {
            Write-Host ("[{0}] bootloader summary: {1}" -f $Backend, $bootSummaryLine)
        }
        if ($kernelSummary) {
            Write-Host ("[{0}] kernel summary: {1}" -f $Backend, $kernelSummaryLine)
        }
        if ($bootPrimaryLine.Success) {
            Write-Host ("[{0}] bootloader descriptor 0: primary selected" -f $Backend)
        }
        if ($kernelPrimaryLine.Success) {
            Write-Host ("[{0}] kernel descriptor 0: primary selected" -f $Backend)
        }
        if ($desktopInventoryLine.Success) {
            Write-Host ("[{0}] desktop inventory candidate 0: enabled primary" -f $Backend)
        }
        if ($bootRenderTargetLine.Success) {
            Write-Host ("[{0}] bootloader render-target note: primary remains render target" -f $Backend)
        }
        if ($bootInvalidFramebufferLine.Success) {
            Write-Host ("[{0}] bootloader framebuffer note: selected framebuffer invalid; BootInfo array disabled" -f $Backend)
        }
        if ($kernelFramebufferReady.Success) {
            Write-Host ("[{0}] kernel framebuffer-ready marker observed" -f $Backend)
        }
    }

    $launcherStdOutTail = (Get-LogTail -Path $launcherStdOut -LineCount 10) -replace "`r", ''
    $launcherStdErrTail = (Get-LogTail -Path $launcherStdErr -LineCount 10) -replace "`r", ''
    $serialTail = (Get-LogTail -Path $serialLog -LineCount 30) -replace "`r", ''

    $summaryLines = @(
        "[QemuDisplayProbeSmoke]",
        'evidenceVersion=1',
        "backend=$Backend",
        "timeoutSeconds=$TimeoutSeconds",
        "timestampUtc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "root=$Root",
        "runRoot=$RunRoot",
        "backendRoot=$backendRoot",
        "launcherStdOut=$launcherStdOut",
        "launcherStdErr=$launcherStdErr",
        "serialLog=$serialLog",
        "bootGopHandles=$($bootGopLine.Groups[1].Value)",
        "bootSummary=$($bootSummary.Line)",
        "kernelSummary=$($kernelSummary.Line)",
        "kernelActiveRenderTargetCount=$($kernelSummary.ActiveRenderTargetCount)",
        "kernelDisabledCandidateCount=$($kernelSummary.DisabledCandidateCount)",
        "bootPrimaryLine=$($bootPrimaryLine.Value)",
        "bootSecondaryLine=$($bootSecondaryLine.Value)",
        "bootRenderTargetLine=$($bootRenderTargetLine.Value)",
        "kernelPrimaryLine=$($kernelPrimaryLine.Value)",
        "kernelSecondaryLine=$($kernelSecondaryLine.Value)",
        "kernelFramebufferReady=$($kernelFramebufferReady.Value)",
        "desktopInventoryLine=$($desktopInventoryLine.Value)",
        "desktopSecondaryInventoryLine=$($desktopSecondaryInventoryLine.Value)",
        "launcherStdOutTail=$launcherStdOutTail",
        "launcherStdErrTail=$launcherStdErrTail",
        "serialTail=$serialTail"
    )
    Set-Content -LiteralPath $summaryPath -Value $summaryLines -Encoding UTF8

    return [pscustomobject]@{
        Backend = $Backend
        BackendRoot = $backendRoot
        LauncherStdOut = $launcherStdOut
        LauncherStdErr = $launcherStdErr
        SerialLog = $serialLog
        SummaryPath = $summaryPath
        BootSummary = $bootSummary
        KernelSummary = $kernelSummary
        KernelActiveRenderTargetCount = $kernelSummary.ActiveRenderTargetCount
        KernelDisabledCandidateCount = $kernelSummary.DisabledCandidateCount
        BootGopHandles = $bootGopLine.Groups[1].Value
        BootPrimaryLine = $bootPrimaryLine.Value
        BootSecondaryLine = $bootSecondaryLine.Value
        BootInvalidFramebufferLine = $bootInvalidFramebufferLine.Value
        KernelPrimaryLine = $kernelPrimaryLine.Value
        KernelSecondaryLine = $kernelSecondaryLine.Value
        KernelFramebufferReady = $kernelFramebufferReady.Value
        DesktopInventoryLine = $desktopInventoryLine.Value
        DesktopSecondaryInventoryLine = $desktopSecondaryInventoryLine.Value
        DiagnosticStatus = if ($Backend -eq 'std') { 'validated' } else { $diagnosticStatus }
        LauncherStdOutText = $launcherStdOutText
        LauncherStdErrText = $launcherStdErrText
        SerialText = $serialText
    }
}

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
    $evidenceLines += "backend=$($result.Backend)"
    $evidenceLines += "diagnosticStatus=$($result.DiagnosticStatus)"
    $evidenceLines += "bootGopHandles=$($result.BootGopHandles)"
    $evidenceLines += "bootSummary=$($result.BootSummary.Line)"
    $evidenceLines += "kernelSummary=$($result.KernelSummary.Line)"
    $evidenceLines += "kernelActiveRenderTargetCount=$($result.KernelActiveRenderTargetCount)"
    $evidenceLines += "kernelDisabledCandidateCount=$($result.KernelDisabledCandidateCount)"
    $evidenceLines += "bootPrimaryLine=$($result.BootPrimaryLine)"
    $evidenceLines += "bootSecondaryLine=$($result.BootSecondaryLine)"
    $evidenceLines += "bootInvalidFramebufferLine=$($result.BootInvalidFramebufferLine)"
    $evidenceLines += "kernelPrimaryLine=$($result.KernelPrimaryLine)"
    $evidenceLines += "kernelSecondaryLine=$($result.KernelSecondaryLine)"
    $evidenceLines += "kernelFramebufferReady=$($result.KernelFramebufferReady)"
    $evidenceLines += "desktopInventoryLine=$($result.DesktopInventoryLine)"
    $evidenceLines += "desktopSecondaryInventoryLine=$($result.DesktopSecondaryInventoryLine)"
    $evidenceLines += "launcherStdOut=$($result.LauncherStdOut)"
    $evidenceLines += "launcherStdErr=$($result.LauncherStdErr)"
    $evidenceLines += "serialLog=$($result.SerialLog)"
    $evidenceLines += "summaryPath=$($result.SummaryPath)"
}

Set-Content -LiteralPath $evidencePath -Value $evidenceLines -Encoding UTF8

Write-Host 'QEMU display probe smoke passed.'
Write-Host ("Evidence: {0}" -f $evidencePath)
foreach ($result in $results) {
    Write-Host ("[{0}] serial={1}" -f $result.Backend, $result.SerialLog)
    Write-Host ("[{0}] summary={1}" -f $result.Backend, $result.SummaryPath)
}
