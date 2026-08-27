param(
    [Parameter(Mandatory = $true)]
    [string]$IsoPath,

    [ValidateRange(5, 600)]
    [int]$TimeoutSeconds = 60,

    [string]$QemuPath,
    [string]$OvmfPath,
    [string]$OvmfVarsPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $ScriptRoot '..')).Path
$ReleaseWorkRoot = Join-Path $RepositoryRoot 'out\release-iso'

function Fail([string]$Message) {
    throw "[test-release-iso] $Message"
}

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Test-UnderRoot([string]$Path, [string]$Root) {
    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

function Quote-ProcessArgument([string]$Value) {
    if ($Value -notmatch '[\s"]') { return $Value }
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Format-CommandLine([string]$FilePath, [string[]]$Arguments) {
    return ((@('"' + $FilePath + '"') + @($Arguments | ForEach-Object { Quote-ProcessArgument $_ })) -join ' ')
}

function Resolve-SingleFile([string]$ExplicitPath, [string[]]$Candidates, [string]$Description) {
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (-not (Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            Fail "-$Description does not name a file: $ExplicitPath"
        }
        return (Get-Item -LiteralPath $ExplicitPath).FullName
    }

    $existing = @($Candidates | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        ForEach-Object { (Get-Item -LiteralPath $_).FullName } |
        Sort-Object -Unique)
    if ($existing.Count -eq 0) { Fail "$Description was not found. Pass -$Description with an explicit path." }
    if ($existing.Count -gt 1) { Fail "multiple $($Description) candidates were found; pass -$Description explicitly:`n$($existing -join "`n")" }
    return $existing[0]
}

function Resolve-Qemu {
    if (-not [string]::IsNullOrWhiteSpace($QemuPath)) {
        if (-not (Test-Path -LiteralPath $QemuPath -PathType Leaf)) { Fail "-QemuPath does not name a file: $QemuPath" }
        return (Get-Item -LiteralPath $QemuPath).FullName
    }
    $candidates = @()
    $command = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) { $candidates += $command.Source }
    $candidates += @(
        (Join-Path $env:ProgramFiles 'qemu\qemu-system-x86_64.exe'),
        'C:\qemu\qemu-system-x86_64.exe',
        (Join-Path $env:USERPROFILE 'qemu\qemu-system-x86_64.exe')
    )
    return Resolve-SingleFile $null $candidates 'QemuPath'
}

function Resolve-CombinedOvmf {
    if (-not [string]::IsNullOrWhiteSpace($OvmfPath)) {
        if (-not (Test-Path -LiteralPath $OvmfPath -PathType Leaf)) { Fail "-OvmfPath does not name a file: $OvmfPath" }
        return (Get-Item -LiteralPath $OvmfPath).FullName
    }
    $upper = Join-Path $RepositoryRoot 'OVMF.fd'
    $lower = Join-Path $RepositoryRoot 'ovmf.fd'
    $hasUpper = Test-Path -LiteralPath $upper -PathType Leaf
    $hasLower = Test-Path -LiteralPath $lower -PathType Leaf
    if ($hasUpper -and $hasLower) {
        $upperHash = (Get-FileHash -LiteralPath $upper -Algorithm SHA256).Hash
        $lowerHash = (Get-FileHash -LiteralPath $lower -Algorithm SHA256).Hash
        if ($upperHash -ne $lowerHash) { Fail 'OVMF.fd and ovmf.fd both exist but differ; pass -OvmfPath explicitly.' }
        return (Get-Item -LiteralPath $upper).FullName
    }
    if ($hasUpper) { return (Get-Item -LiteralPath $upper).FullName }
    if ($hasLower) { return (Get-Item -LiteralPath $lower).FullName }
    return $null
}

function Resolve-SplitOvmf {
    $codeCandidates = @(
        (Join-Path $env:ProgramFiles 'qemu\share\edk2-x86_64-code.fd'),
        'C:\Program Files\qemu\share\edk2-x86_64-code.fd'
    )
    $varsCandidates = @(
        (Join-Path $env:ProgramFiles 'qemu\share\edk2-x86_64-vars.fd'),
        'C:\Program Files\qemu\share\edk2-x86_64-vars.fd'
    )
    $code = Resolve-SingleFile $null $codeCandidates 'OvmfPath'
    $vars = Resolve-SingleFile $OvmfVarsPath $varsCandidates 'OvmfVarsPath'
    return [PSCustomObject]@{ Code = $code; Vars = $vars }
}

$resolvedIso = Get-FullPath $IsoPath
if (-not (Test-UnderRoot $resolvedIso $RepositoryRoot)) { Fail "ISO path must remain under the repository: $resolvedIso" }
if (-not (Test-Path -LiteralPath $resolvedIso -PathType Leaf)) { Fail "ISO does not exist: $resolvedIso" }
$isoInfo = Get-Item -LiteralPath $resolvedIso
if ($isoInfo.Length -le 0 -or $isoInfo.Length % 2048 -ne 0) { Fail "ISO is empty or not sector-aligned: $resolvedIso ($($isoInfo.Length) bytes)" }

$qemu = Resolve-Qemu
$combinedOvmf = Resolve-CombinedOvmf
$workDirectory = Join-Path $ReleaseWorkRoot ('qemu-test-' + [Guid]::NewGuid().ToString('N'))
if (-not (Test-UnderRoot $workDirectory $ReleaseWorkRoot)) { Fail 'internal QEMU work directory escaped the approved release root.' }
New-Item -ItemType Directory -Path $workDirectory -Force | Out-Null
$serialLog = Join-Path $workDirectory 'serial.log'
$qemuStdoutLog = Join-Path $workDirectory 'qemu.stdout.log'
$qemuStderrLog = Join-Path $workDirectory 'qemu.stderr.log'
$qemuDebugLog = Join-Path $workDirectory 'qemu.debug.log'
$qemuArgs = @('-machine', 'pc,usb=off')

if ($null -ne $combinedOvmf) {
    $qemuArgs += @('-drive', "if=pflash,format=raw,readonly=on,file=$combinedOvmf")
    Write-Host "[test-release-iso] using combined OVMF: $combinedOvmf" -ForegroundColor DarkGray
} else {
    $split = Resolve-SplitOvmf
    $varsCopy = Join-Path $workDirectory 'OVMF_VARS.fd'
    Copy-Item -LiteralPath $split.Vars -Destination $varsCopy -Force
    $qemuArgs += @(
        '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$($split.Code)",
        '-drive', "if=pflash,format=raw,unit=1,file=$varsCopy"
    )
    Write-Host "[test-release-iso] using split OVMF code: $($split.Code)" -ForegroundColor DarkGray
    Write-Host "[test-release-iso] copied writable OVMF variables to: $varsCopy" -ForegroundColor DarkGray
}

$qemuArgs += @(
    # Attach the ISO as an explicit IDE CD device.  With the current QEMU/OVMF
    # combination, the shorthand if=ide drive can fall through to PXE even
    # when the El Torito UEFI entry is valid.
    '-drive', "if=none,id=releasecdrom,file=$resolvedIso,media=cdrom,readonly=on,format=raw",
    '-device', 'ide-cd,drive=releasecdrom,bootindex=1',
    '-boot', 'order=d',
    '-m', '1024M',
    '-vga', 'std',
    '-display', 'gtk',
    '-vnc', ':0',
    '-serial', "file:$serialLog",
    '-D', $qemuDebugLog,
    '-d', 'guest_errors',
    '-rtc', 'base=utc,clock=host',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-no-reboot'
)

$startArguments = @($qemuArgs | ForEach-Object { Quote-ProcessArgument $_ })
Write-Host '[test-release-iso] reproducible QEMU command:' -ForegroundColor Cyan
Write-Host ('  ' + (Format-CommandLine $qemu $qemuArgs))
Write-Host "[test-release-iso] serial log: $serialLog" -ForegroundColor DarkGray
Write-Host "[test-release-iso] QEMU stderr log: $qemuStderrLog" -ForegroundColor DarkGray
Write-Host "[test-release-iso] QEMU debug log: $qemuDebugLog" -ForegroundColor DarkGray

$process = $null
try {
    $process = Start-Process -FilePath $qemu -ArgumentList $startArguments -WorkingDirectory $RepositoryRoot `
        -RedirectStandardOutput $qemuStdoutLog -RedirectStandardError $qemuStderrLog -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $evidencePatterns = [ordered]@{
        firmwareBootEntry = 'BdsDxe:\s+starting Boot\d+ .*DVD-ROM'
        bootloader = 'guideXOS UEFI Bootloader'
        kernelLoaded = 'Kernel loaded at:'
        ramdiskSize = 'Ramdisk size:\s+67108864 bytes'
        ramdiskLoaded = 'Ramdisk loaded at'
        desktopReady = '\[DESKTOP CAP\]\s+desktop_event_loop_active=true'
        kernelMainLoop = '\[KERNEL\]\s+Entering main loop'
    }
    $evidenceFound = [ordered]@{}
    foreach ($name in $evidencePatterns.Keys) { $evidenceFound[$name] = $false }
    $readyObservedAt = $null
    $stoppedAfterReady = $false
    while (-not $process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $serialLog -PathType Leaf) {
            try {
                $serialText = Get-Content -LiteralPath $serialLog -Raw -ErrorAction Stop
                foreach ($name in $evidencePatterns.Keys) {
                    if (-not $evidenceFound[$name] -and $serialText -match $evidencePatterns[$name]) {
                        $evidenceFound[$name] = $true
                    }
                }
            } catch [IO.IOException] {
                # QEMU may still be writing the serial file; retry on the next poll.
            }
        }
        $allEvidenceFound = $true
        foreach ($name in $evidenceFound.Keys) {
            if (-not $evidenceFound[$name]) { $allEvidenceFound = $false }
        }
        if ($allEvidenceFound) {
            if ($null -eq $readyObservedAt) {
                $readyObservedAt = [DateTime]::UtcNow
                Write-Host '[test-release-iso] all boot and desktop readiness markers observed.' -ForegroundColor Green
            } elseif ([DateTime]::UtcNow -ge $readyObservedAt.AddSeconds(2)) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                $process.WaitForExit()
                $stoppedAfterReady = $true
                break
            }
        }
        Start-Sleep -Milliseconds 500
        $process.Refresh()
    }
    if (-not $stoppedAfterReady -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Fail "QEMU did not exit within $TimeoutSeconds seconds; it was stopped. Review serial=$serialLog, stdout=$qemuStdoutLog, stderr=$qemuStderrLog, debug=$qemuDebugLog"
    }
    $missingEvidence = @($evidenceFound.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object { $_.Key })
    if ($missingEvidence.Count -gt 0) {
        Fail "QEMU stopped without complete boot evidence. Missing markers: $($missingEvidence -join ', '). Review serial=$serialLog, stdout=$qemuStdoutLog, stderr=$qemuStderrLog, debug=$qemuDebugLog"
    }
    foreach ($name in $evidenceFound.Keys) {
        Write-Host "[test-release-iso] evidence: $name" -ForegroundColor DarkGray
    }
    $exitCode = $process.ExitCode
    if (-not $stoppedAfterReady -and $exitCode -ne 0) {
        $stderr = if (Test-Path -LiteralPath $qemuStderrLog) { Get-Content -LiteralPath $qemuStderrLog -Raw } else { '' }
        Fail "QEMU exited with code $exitCode. Review serial=$serialLog, stdout=$qemuStdoutLog, stderr=$qemuStderrLog, debug=$qemuDebugLog.`n$stderr"
    }
    if ($stoppedAfterReady) {
        Write-Host "[test-release-iso] QEMU reached the ready state and was stopped after the bounded grace period; serial evidence: $serialLog" -ForegroundColor Green
    } else {
        Write-Host "[test-release-iso] QEMU exited cleanly; serial evidence: $serialLog" -ForegroundColor Green
    }
    if (Test-Path -LiteralPath $serialLog -PathType Leaf) {
        $logInfo = Get-Item -LiteralPath $serialLog
        Write-Host "[test-release-iso] serial log size: $($logInfo.Length) bytes" -ForegroundColor DarkGray
    }
    Write-Host '[test-release-iso] QEMU success is virtual-machine evidence only; bare-metal testing remains required.' -ForegroundColor Yellow
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}
