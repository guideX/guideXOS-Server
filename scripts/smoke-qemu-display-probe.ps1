param(
    [string[]]$Backends = @('std', 'virtio-gpu', 'virtio-gpu-modern-only', 'virtio-vga', 'qxl-vga'),
    [int]$TimeoutSeconds = 120,
    [ValidateSet('diagnostic', 'compositorFrame')]
    [string]$Mode = 'diagnostic'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

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
        [string]$LauncherStdErr,
        [switch]$EnableVisualCapture,
        [int]$QmpPort = 0
    )

    $batchPath = Join-Path $Root 'scripts\run-qemu-display-probe.bat'
    Assert-PathExists -Path $batchPath -Label 'QEMU display probe launcher'

    $oldState = Save-EnvironmentState -Names @(
        'GXOS_QEMU_DISPLAY_PROBE_HEADLESS',
        'GXOS_QEMU_DISPLAY_PROBE_CAPTURE',
        'GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE',
        'GXOS_QEMU_DISPLAY_PROBE_QMP_PORT',
        'GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG'
    )

    $env:GXOS_QEMU_DISPLAY_PROBE_HEADLESS = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_NO_PAUSE = '1'
    $env:GXOS_QEMU_DISPLAY_PROBE_SERIAL_LOG = $SerialLog
    if ($EnableVisualCapture) {
        $env:GXOS_QEMU_DISPLAY_PROBE_CAPTURE = '1'
        if ($QmpPort -gt 0) {
            $env:GXOS_QEMU_DISPLAY_PROBE_QMP_PORT = [string]$QmpPort
        }
    } else {
        Remove-Item Env:\GXOS_QEMU_DISPLAY_PROBE_CAPTURE -ErrorAction SilentlyContinue
        Remove-Item Env:\GXOS_QEMU_DISPLAY_PROBE_QMP_PORT -ErrorAction SilentlyContinue
    }

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

function Get-TextMatchGroupValue {
    param(
        [AllowEmptyString()]
        [string]$Text,
        [Parameter(Mandatory = $true)]
        [string]$Pattern,
        [int]$GroupIndex = 1,
        [string]$Fallback = ''
    )

    $match = [regex]::Match($Text, $Pattern)
    if ($match.Success -and $match.Groups.Count -gt $GroupIndex) {
        return $match.Groups[$GroupIndex].Value
    }

    return $Fallback
}

function Get-FreeTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally {
        $listener.Stop()
    }
}

function Read-QmpMessage {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.StreamReader]$Reader,
        [int]$TimeoutSeconds = 10
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $line = $Reader.ReadLine()
        } catch {
            Start-Sleep -Milliseconds 50
            continue
        }

        if ($null -eq $line) {
            Start-Sleep -Milliseconds 50
            continue
        }

        $line = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        try {
            return $line | ConvertFrom-Json -ErrorAction Stop
        } catch {
            continue
        }
    }

    throw "Timed out waiting for QMP data."
}

function New-QmpSession {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Port,
        [int]$TimeoutSeconds = 10
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    $timeoutMs = [Math]::Max(1000, $TimeoutSeconds * 1000)
    $client.ReceiveTimeout = $timeoutMs
    $client.SendTimeout = $timeoutMs
    $client.Connect('127.0.0.1', $Port)

    $stream = $client.GetStream()
    $stream.ReadTimeout = $timeoutMs
    $stream.WriteTimeout = $timeoutMs

    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    $reader = [System.IO.StreamReader]::new($stream, $utf8NoBom, $false, 1024, $true)
    $writer = [System.IO.StreamWriter]::new($stream, $utf8NoBom, 1024, $true)
    $writer.NewLine = "`n"
    $writer.AutoFlush = $true

    $greeting = Read-QmpMessage -Reader $reader -TimeoutSeconds $TimeoutSeconds
    if (-not ($greeting.PSObject.Properties.Name -contains 'QMP')) {
        throw "QMP greeting missing from port $Port."
    }

    [void](Invoke-QmpCommand -Session ([pscustomobject]@{
        Client = $client
        Stream = $stream
        Reader = $reader
        Writer = $writer
        Port = $Port
    }) -Execute 'qmp_capabilities' -TimeoutSeconds $TimeoutSeconds)

    return [pscustomobject]@{
        Client = $client
        Stream = $stream
        Reader = $reader
        Writer = $writer
        Port = $Port
    }
}

function Close-QmpSession {
    param(
        [AllowNull()]
        [object]$Session
    )

    if ($null -eq $Session) {
        return
    }

    foreach ($name in @('Writer', 'Reader', 'Stream', 'Client')) {
        $member = $Session.$name
        if ($null -ne $member) {
            try {
                $member.Dispose()
            } catch {
            }
        }
    }
}

function Invoke-QmpCommand {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Session,
        [Parameter(Mandatory = $true)]
        [string]$Execute,
        [hashtable]$Arguments = $null,
        [int]$TimeoutSeconds = 10
    )

    $payload = [ordered]@{
        execute = $Execute
    }
    if ($null -ne $Arguments -and $Arguments.Count -gt 0) {
        $payload.arguments = $Arguments
    }

    $json = $payload | ConvertTo-Json -Compress -Depth 8
    $Session.Writer.WriteLine($json)
    $Session.Writer.Flush()

    while ($true) {
        $message = Read-QmpMessage -Reader $Session.Reader -TimeoutSeconds $TimeoutSeconds
        if ($message.PSObject.Properties.Name -contains 'event') {
            continue
        }

        if ($message.PSObject.Properties.Name -contains 'return') {
            return $message
        }

        if ($message.PSObject.Properties.Name -contains 'error') {
            $errorClass = if ($message.error.PSObject.Properties.Name -contains 'class') { $message.error.class } else { 'unknown' }
            $errorDesc = if ($message.error.PSObject.Properties.Name -contains 'desc') { $message.error.desc } else { 'unknown' }
            throw ("QMP command '{0}' failed: {1}: {2}" -f $Execute, $errorClass, $errorDesc)
        }
    }
}

function Invoke-QmpScreendump {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Session,
        [Parameter(Mandatory = $true)]
        [string]$Filename,
        [string]$DeviceId = '',
        [int]$Head = -1,
        [string]$Format = 'png'
    )

    $arguments = @{
        filename = $Filename
    }
    if (-not [string]::IsNullOrWhiteSpace($DeviceId)) {
        $arguments.device = $DeviceId
    }
    if ($Head -ge 0) {
        $arguments.head = $Head
    }
    if (-not [string]::IsNullOrWhiteSpace($Format)) {
        $arguments.format = $Format
    }

    [void](Invoke-QmpCommand -Session $Session -Execute 'screendump' -Arguments $arguments -TimeoutSeconds 10)
}

function Wait-ForCaptureFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [int]$TimeoutSeconds = 2
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $Path) {
            $fileInfo = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
            if ($fileInfo -and $fileInfo.Length -gt 0) {
                return $true
            }
        }

        Start-Sleep -Milliseconds 100
    }

    return $false
}

function Get-DiagnosticPatternSpec {
    param(
        [Parameter(Mandatory = $true)]
        [uint32]$ScanoutId
    )

    if ($ScanoutId -eq 1) {
        return [pscustomobject]@{
            Name = 'scanout1-red-orange'
            BorderDark = [System.Drawing.Color]::FromArgb(255, 0x58, 0x10, 0x00)
            BorderLight = [System.Drawing.Color]::FromArgb(255, 0xFF, 0xB0, 0x60)
            TopLeft = [System.Drawing.Color]::FromArgb(255, 0xE0, 0x40, 0x18)
            TopRight = [System.Drawing.Color]::FromArgb(255, 0xFF, 0x88, 0x18)
            BottomLeft = [System.Drawing.Color]::FromArgb(255, 0xB0, 0x18, 0x10)
            BottomRight = [System.Drawing.Color]::FromArgb(255, 0xFF, 0xC0, 0x48)
            Center = [System.Drawing.Color]::FromArgb(255, 0xFF, 0xFF, 0xFF)
        }
    }

    return [pscustomobject]@{
        Name = 'scanout0-blue-cyan'
        BorderDark = [System.Drawing.Color]::FromArgb(255, 0x10, 0x18, 0x48)
        BorderLight = [System.Drawing.Color]::FromArgb(255, 0x78, 0xD8, 0xFF)
        TopLeft = [System.Drawing.Color]::FromArgb(255, 0x20, 0x58, 0xE8)
        TopRight = [System.Drawing.Color]::FromArgb(255, 0x00, 0xD8, 0xF0)
        BottomLeft = [System.Drawing.Color]::FromArgb(255, 0x38, 0x78, 0xFF)
        BottomRight = [System.Drawing.Color]::FromArgb(255, 0x90, 0xF8, 0xFF)
        Center = [System.Drawing.Color]::FromArgb(255, 0xFF, 0xFF, 0xFF)
    }
}

function Get-DiagnosticCaptureAssessment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ImagePath,
        [Parameter(Mandatory = $true)]
        [uint32]$ScanoutId
    )

    if (-not (Test-Path -LiteralPath $ImagePath)) {
        return [pscustomobject]@{
            Status = 'failed'
            Reason = 'capture file missing'
            PatternName = (Get-DiagnosticPatternSpec -ScanoutId $ScanoutId).Name
            Width = 0
            Height = 0
            Signature = ''
            Exists = $false
            ByteCount = 0
        }
    }

    $fileInfo = Get-Item -LiteralPath $ImagePath
    if ($fileInfo.Length -le 0) {
        return [pscustomobject]@{
            Status = 'failed'
            Reason = 'capture file is empty'
            PatternName = (Get-DiagnosticPatternSpec -ScanoutId $ScanoutId).Name
            Width = 0
            Height = 0
            Signature = ''
            Exists = $true
            ByteCount = [int64]$fileInfo.Length
        }
    }

    $bitmap = $null
    try {
        $bitmap = [System.Drawing.Bitmap]::FromFile($ImagePath)
        $width = $bitmap.Width
        $height = $bitmap.Height
        $spec = Get-DiagnosticPatternSpec -ScanoutId $ScanoutId

        if ($width -lt 64 -or $height -lt 64) {
            return [pscustomobject]@{
                Status = 'manual-check-required'
                Reason = 'capture dimensions are too small for reliable pixel sampling'
                PatternName = $spec.Name
                Width = $width
                Height = $height
                Signature = ''
                Exists = $true
                ByteCount = [int64]$fileInfo.Length
            }
        }

        $sampleCoords = @(
            @{ Name = 'borderDark'; X = 4; Y = 4; Expected = $spec.BorderDark },
            @{ Name = 'borderLight'; X = 12; Y = 4; Expected = $spec.BorderLight },
            @{ Name = 'topLeft'; X = [Math]::Max(16, [int]($width / 8)); Y = [Math]::Max(16, [int]($height / 8)); Expected = $spec.TopLeft },
            @{ Name = 'topRight'; X = [Math]::Min($width - 17, [int](($width * 7) / 8)); Y = [Math]::Max(16, [int]($height / 8)); Expected = $spec.TopRight },
            @{ Name = 'bottomLeft'; X = [Math]::Max(16, [int]($width / 8)); Y = [Math]::Min($height - 17, [int](($height * 7) / 8)); Expected = $spec.BottomLeft },
            @{ Name = 'bottomRight'; X = [Math]::Min($width - 17, [int](($width * 7) / 8)); Y = [Math]::Min($height - 17, [int](($height * 7) / 8)); Expected = $spec.BottomRight },
            @{ Name = 'center'; X = [int]($width / 2); Y = [int]($height / 2); Expected = $spec.Center }
        )

        $signatureParts = New-Object System.Collections.Generic.List[string]
        $allMatched = $true
        $mismatchDetails = New-Object System.Collections.Generic.List[string]

        foreach ($sample in $sampleCoords) {
            $x = [int]$sample.X
            $y = [int]$sample.Y
            if ($x -lt 0 -or $y -lt 0 -or $x -ge $width -or $y -ge $height) {
                return [pscustomobject]@{
                    Status = 'manual-check-required'
                    Reason = "sample point $($sample.Name) falls outside capture bounds"
                    PatternName = $spec.Name
                    Width = $width
                    Height = $height
                    Signature = ''
                    Exists = $true
                    ByteCount = [int64]$fileInfo.Length
                }
            }

            $actual = $bitmap.GetPixel($x, $y)
            $expected = $sample.Expected
            $signatureParts.Add(("{0}={1:00}{2:00}{3:00}" -f $sample.Name, $actual.R, $actual.G, $actual.B))
            if ($actual.R -ne $expected.R -or $actual.G -ne $expected.G -or $actual.B -ne $expected.B) {
                $allMatched = $false
                $mismatchDetails.Add(("{0} expected={1:00}{2:00}{3:00} actual={4:00}{5:00}{6:00}" -f $sample.Name, $expected.R, $expected.G, $expected.B, $actual.R, $actual.G, $actual.B))
            }
        }

        return [pscustomobject]@{
            Status = if ($allMatched) { 'confirmed' } else { 'failed' }
            Reason = if ($allMatched) { 'pixel samples matched expected pattern' } else { ($mismatchDetails -join '; ') }
            PatternName = $spec.Name
            Width = $width
            Height = $height
            Signature = ($signatureParts -join '|')
            Exists = $true
            ByteCount = [int64]$fileInfo.Length
        }
    } finally {
        if ($bitmap) {
            $bitmap.Dispose()
        }
    }
}

function Get-CompositorCaptureAssessment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ImagePath,
        [Parameter(Mandatory = $true)]
        [uint32]$ScanoutId
    )

    if (-not (Test-Path -LiteralPath $ImagePath)) {
        return [pscustomobject]@{
            Status = 'failed'
            Reason = 'capture file missing'
            PatternName = "compositor-frame-scanout$ScanoutId"
            Width = 0
            Height = 0
            Signature = ''
            Exists = $false
            ByteCount = 0
            ContentConfirmed = $false
            TaskbarConfirmed = $false
            PrimaryTitleBarConfirmed = $false
            SecondaryBannerConfirmed = $false
            BackgroundSignature = ''
        }
    }

    $fileInfo = Get-Item -LiteralPath $ImagePath
    if ($fileInfo.Length -le 0) {
        return [pscustomobject]@{
            Status = 'failed'
            Reason = 'capture file is empty'
            PatternName = "compositor-frame-scanout$ScanoutId"
            Width = 0
            Height = 0
            Signature = ''
            Exists = $true
            ByteCount = [int64]$fileInfo.Length
            ContentConfirmed = $false
            TaskbarConfirmed = $false
            PrimaryTitleBarConfirmed = $false
            SecondaryBannerConfirmed = $false
            BackgroundSignature = ''
        }
    }

    $bitmap = $null
    try {
        $bitmap = [System.Drawing.Bitmap]::FromFile($ImagePath)
        $width = $bitmap.Width
        $height = $bitmap.Height
        if ($width -lt 64 -or $height -lt 64) {
            return [pscustomobject]@{
                Status = 'manual-check-required'
                Reason = 'capture dimensions are too small for compositor sampling'
                PatternName = "compositor-frame-scanout$ScanoutId"
                Width = $width
                Height = $height
                Signature = ''
                Exists = $true
                ByteCount = [int64]$fileInfo.Length
                ContentConfirmed = $false
                TaskbarConfirmed = $false
                PrimaryTitleBarConfirmed = $false
                SecondaryBannerConfirmed = $false
                BackgroundSignature = ''
            }
        }

        function Get-ExpectedCompositorBackgroundColor {
            param(
                [Parameter(Mandatory = $true)]
                [int]$GlobalX,
                [Parameter(Mandatory = $true)]
                [int]$GlobalY,
                [Parameter(Mandatory = $true)]
                [int]$DesktopWidth,
                [Parameter(Mandatory = $true)]
                [int]$DesktopHeight
            )

            $leftRed = 0x26
            $leftGreen = 0x40
            $leftBlue = 0x56
            $rightRed = 0x52
            $rightGreen = 0x70
            $rightBlue = 0x88
            $denomX = [Math]::Max(1, $DesktopWidth - 1)
            $denomY = [Math]::Max(1, $DesktopHeight - 1)
            $shade = [int](($GlobalY * 18) / $denomY)
            $blend = [int](($GlobalX * 255) / $denomX)
            $red = $leftRed + [int](($rightRed - $leftRed) * $blend / 255)
            $green = $leftGreen + [int](($rightGreen - $leftGreen) * $blend / 255)
            $blue = $leftBlue + [int](($rightBlue - $leftBlue) * $blend / 255)

            return [pscustomobject]@{
                R = [byte][Math]::Max(0, $red - $shade)
                G = [byte][Math]::Max(0, $green - $shade)
                B = [byte][Math]::Max(0, $blue - $shade)
            }
        }

        function Get-RgbSample {
            param(
                [Parameter(Mandatory = $true)]
                [System.Drawing.Bitmap]$Bitmap,
                [Parameter(Mandatory = $true)]
                [int]$X,
                [Parameter(Mandatory = $true)]
                [int]$Y
            )

            $pixel = $Bitmap.GetPixel($X, $Y)
            return [pscustomobject]@{ R = $pixel.R; G = $pixel.G; B = $pixel.B }
        }

        function Test-RgbEquals {
            param(
                [Parameter(Mandatory = $true)]
                $Actual,
                [Parameter(Mandatory = $true)]
                $Expected
            )

            return $Actual.R -eq $Expected.R -and $Actual.G -eq $Expected.G -and $Actual.B -eq $Expected.B
        }

        $desktopWidth = [int]($width * 2)
        $desktopHeight = [int]$height
        $viewportOriginX = if ($ScanoutId -eq 0) { 0 } else { $width }
        $backgroundSample = Get-RgbSample -Bitmap $bitmap -X 20 -Y 20
        $backgroundExpected = Get-ExpectedCompositorBackgroundColor -GlobalX ($viewportOriginX + 20) -GlobalY 20 -DesktopWidth $desktopWidth -DesktopHeight $desktopHeight
        $titleBarSample = Get-RgbSample -Bitmap $bitmap -X 100 -Y 135
        $titleBarExpected = [pscustomobject]@{ R = 0x4B; G = 0x77; B = 0xA4 }
        $taskbarSample = Get-RgbSample -Bitmap $bitmap -X 300 -Y ([Math]::Max(0, $height - 20))
        $taskbarExpected = [pscustomobject]@{ R = 0x31; G = 0x35; B = 0x44 }
        $secondaryBannerSample = Get-RgbSample -Bitmap $bitmap -X 250 -Y 70
        $secondaryBannerExpected = [pscustomobject]@{ R = 0xA1; G = 0x6C; B = 0x2C }

        $backgroundMatches = Test-RgbEquals -Actual $backgroundSample -Expected $backgroundExpected
        $titleBarMatches = if ($ScanoutId -eq 0) {
            Test-RgbEquals -Actual $titleBarSample -Expected $titleBarExpected
        } else {
            -not (Test-RgbEquals -Actual $titleBarSample -Expected $titleBarExpected)
        }
        $taskbarMatches = if ($ScanoutId -eq 0) {
            Test-RgbEquals -Actual $taskbarSample -Expected $taskbarExpected
        } else {
            -not (Test-RgbEquals -Actual $taskbarSample -Expected $taskbarExpected)
        }
        $secondaryBannerMatches = if ($ScanoutId -eq 1) {
            Test-RgbEquals -Actual $secondaryBannerSample -Expected $secondaryBannerExpected
        } else {
            -not (Test-RgbEquals -Actual $secondaryBannerSample -Expected $secondaryBannerExpected)
        }

        $signature = @(
            ('bg={0:00}{1:00}{2:00}' -f $backgroundSample.R, $backgroundSample.G, $backgroundSample.B)
            ('title={0:00}{1:00}{2:00}' -f $titleBarSample.R, $titleBarSample.G, $titleBarSample.B)
            ('taskbar={0:00}{1:00}{2:00}' -f $taskbarSample.R, $taskbarSample.G, $taskbarSample.B)
            ('banner={0:00}{1:00}{2:00}' -f $secondaryBannerSample.R, $secondaryBannerSample.G, $secondaryBannerSample.B)
        ) -join '|'

        $mismatchDetails = New-Object System.Collections.Generic.List[string]
        if (-not $backgroundMatches) {
            $mismatchDetails.Add(('background expected={0:00}{1:00}{2:00} actual={3:00}{4:00}{5:00}' -f $backgroundExpected.R, $backgroundExpected.G, $backgroundExpected.B, $backgroundSample.R, $backgroundSample.G, $backgroundSample.B))
        }
        if (-not $titleBarMatches) {
            $mismatchDetails.Add(('title expected-match={0} actual={1:00}{2:00}{3:00}' -f ($(if ($ScanoutId -eq 0) { 'yes' } else { 'no' })), $titleBarSample.R, $titleBarSample.G, $titleBarSample.B))
        }
        if (-not $taskbarMatches) {
            $mismatchDetails.Add(('taskbar expected-match={0} actual={1:00}{2:00}{3:00}' -f ($(if ($ScanoutId -eq 0) { 'yes' } else { 'no' })), $taskbarSample.R, $taskbarSample.G, $taskbarSample.B))
        }
        if (-not $secondaryBannerMatches) {
            $mismatchDetails.Add(('secondaryBanner expected-match={0} actual={1:00}{2:00}{3:00}' -f ($(if ($ScanoutId -eq 1) { 'yes' } else { 'no' })), $secondaryBannerSample.R, $secondaryBannerSample.G, $secondaryBannerSample.B))
        }

        $contentConfirmed = $backgroundMatches -and $titleBarMatches -and $taskbarMatches -and $secondaryBannerMatches

        return [pscustomobject]@{
            Status = if ($contentConfirmed) { 'confirmed' } else { 'failed' }
            Reason = if ($contentConfirmed) { 'desktop compositor samples matched expected layout' } else { ($mismatchDetails -join '; ') }
            PatternName = "compositor-frame-scanout$ScanoutId"
            Width = $width
            Height = $height
            Signature = $signature
            Exists = $true
            ByteCount = [int64]$fileInfo.Length
            ContentConfirmed = $contentConfirmed
            TaskbarConfirmed = $taskbarMatches
            PrimaryTitleBarConfirmed = $titleBarMatches
            SecondaryBannerConfirmed = $secondaryBannerMatches
            BackgroundSignature = ('bg={0:00}{1:00}{2:00}' -f $backgroundSample.R, $backgroundSample.G, $backgroundSample.B)
        }
    } finally {
        if ($bitmap) {
            $bitmap.Dispose()
        }
    }
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
                QemuArgs = '-vga none -device virtio-gpu-pci,id=gpu0,max_outputs=2'
                ProbeNote = 'virtio-gpu-pci diagnostic 2D test-pattern probe (QEMU-only dual-output inventory bridge)'
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
                QemuArgs = '-vga none -device virtio-gpu-pci,id=gpu0,max_outputs=2,disable-legacy=on'
                ProbeNote = 'virtio-gpu-pci modern-only diagnostic 2D test-pattern probe (QEMU-only dual-output inventory bridge)'
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
        [int]$TimeoutSeconds,
        [string]$ProbeStage = 'stageA',
        [switch]$EnableVisualCapture
    )

    $spec = Get-BackendSpec -Backend $Backend
    $backendName = $spec.Backend
    $stageLabel = if ([string]::IsNullOrWhiteSpace($ProbeStage)) { 'stageA' } else { $ProbeStage }
    $backendRoot = Join-Path $RunRoot ("{0}-{1}" -f $backendName, $stageLabel)
    New-Item -ItemType Directory -Force -Path $backendRoot | Out-Null

    $launcherStdOut = Join-Path $backendRoot 'launcher.stdout.log'
    $launcherStdErr = Join-Path $backendRoot 'launcher.stderr.log'
    $serialLog = Join-Path $backendRoot 'serial.log'
    $summaryPath = Join-Path $backendRoot ("summary-{0}.txt" -f $stageLabel)
    $captureRoot = Join-Path $backendRoot 'captures'
    if ($EnableVisualCapture) {
        New-Item -ItemType Directory -Force -Path $captureRoot | Out-Null
    }
    $qmpPort = 0
    if ($EnableVisualCapture) {
        $qmpPort = Get-FreeTcpPort
    }
    $visualCaptureStatus = 'disabled'
    $visualScanout0 = 'not-attempted'
    $visualScanout1 = 'not-attempted'
    $visualScanout0Path = ''
    $visualScanout1Path = ''
    $visualScanout0Signature = ''
    $visualScanout1Signature = ''
    $visualScanout0Assessment = $null
    $visualScanout1Assessment = $null
    $distinctPatternsConfirmed = $false
    $compositorContentConfirmed0 = $false
    $compositorContentConfirmed1 = $false
    $taskbarPrimaryOnlyConfirmed = $false
    $viewportSplitConfirmed = $false
    $dualOutputVisualProof = 'not-confirmed'
    $captureReason = 'manual-check-required'
    $visualCaptureReason = ''

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
            ProbeStage = $stageLabel
            LauncherStdOut = $launcherStdOut
            LauncherStdErr = $launcherStdErr
            SerialLog = $serialLog
            SummaryPath = $summaryPath
            CaptureRoot = $captureRoot
            VisualCaptureStatus = 'unsupported'
            VisualScanout0 = 'not-attempted'
            VisualScanout1 = 'not-attempted'
            DistinctPatternsConfirmed = $false
            DualOutputVisualProof = 'not-attempted'
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
            GpuPreRenderDeviceConfigLine = ''
            GpuPreRenderDisplayInfoBeginLine = ''
            GpuPreRenderCompletionLine = ''
            GpuPreRenderDisplayInfoSummaryLine = ''
            GpuDiagnosticTargetLine = ''
            GpuBackingLayoutLine = ''
            GpuResourceCreateLine = ''
            GpuAttachLine = ''
            GpuSetScanoutLine = ''
            GpuTransferLine = ''
            GpuFlushLine = ''
            GpuPostRenderDisplayInfoBeginLine = ''
            GpuPostRenderCompletionLine = ''
            GpuPostRenderDisplayInfoSummaryLine = ''
            GpuPostRenderScanout0Line = ''
            GpuPostRenderScanout1Line = ''
            GpuCompositorTarget0PlanLine = ''
            GpuCompositorTarget0ResultLine = ''
            GpuCompositorTarget1PlanLine = ''
            GpuCompositorTarget1ResultLine = ''
            GpuCompositorProofLine = ''
            GpuProbeCompleteContentMode = ''
            GpuProbeCompleteFrameMode = ''
            GpuProbeCompleteContinuousPresentation = ''
            CompositorContentConfirmed0 = $false
            CompositorContentConfirmed1 = $false
            TaskbarPrimaryOnlyConfirmed = $false
            ViewportSplitConfirmed = $false
            DiagnosticStatus = 'unsupported'
            Interpretation = $supportReason
            LauncherStdOutText = ''
            LauncherStdErrText = ''
            SerialText = ''
        }

        foreach ($propertyName in @(
            'GpuAckLine',
            'GpuAttachSecondaryLine',
            'GpuBackingContiguousRunCount',
            'GpuBackingCoveredBytes',
            'GpuBackingMemEntryCount',
            'GpuBackingPhysicalCoverageValid',
            'GpuDisplayInfoResponseLine',
            'GpuDriverLine',
            'GpuDriverOkStatusLine',
            'GpuEnabledScanoutCount',
            'GpuFeatureBitmapLine',
            'GpuFeaturesOkStatusLine',
            'GpuFlush1Line',
            'GpuMmioCacheAttrs',
            'GpuMmioFlags',
            'GpuMmioKernelVirtualBase',
            'GpuMmioMappedLength',
            'GpuMmioMappedVirtual',
            'GpuMmioNoExec',
            'GpuMmioNonUser',
            'GpuMmioPages',
            'GpuMmioQemuProbeOnly',
            'GpuMmioRequestBase',
            'GpuMmioRequestLength',
            'GpuMmioUncached',
            'GpuMonitor0Line',
            'GpuMonitor1Line',
            'GpuOutput0Line',
            'GpuOutput1Line',
            'GpuOutputBackedTargetCount',
            'GpuOutputConfiguredCount',
            'GpuOutputConnectorEnabledCount',
            'GpuOutputInventoryLine',
            'GpuOutputOperationalCount',
            'GpuOutputOperationalOutputCount',
            'GpuOutputPresentationConfirmedCount',
            'GpuOutputPresentationConfirmedCountDetailed',
            'GpuOutputPrimaryOutput',
            'GpuOutputProtocolConnectorEnabledCount',
            'GpuOutputTargetCount',
            'GpuOutputVirtualDesktopHeight',
            'GpuOutputVirtualDesktopWidth',
            'GpuPostRenderDeviceConfigNumScanouts',
            'GpuPostRenderDisabledScanouts',
            'GpuPostRenderEnabledScanouts',
            'GpuPostRenderQemuMaxOutputsIntent',
            'GpuPreRenderDeviceConfigNumScanouts',
            'GpuPreRenderDisabledScanouts',
            'GpuPreRenderEnabledScanouts',
            'GpuPreRenderQemuMaxOutputsIntent',
            'GpuPrimaryPatternChecksum',
            'GpuPrimaryPatternChecksumLine',
            'GpuProbeCompleteDeviceConfigNumScanouts',
            'GpuProbeCompleteDisabledScanoutsBefore',
            'GpuProbeCompleteDistinctPatterns',
            'GpuProbeCompleteEnabledScanoutsAfter',
            'GpuProbeCompleteEnabledScanoutsBefore',
            'GpuProbeCompleteQemuTwoUsableScanouts',
            'GpuResourceCreateSecondaryLine',
            'GpuSecondaryPatternChecksum',
            'GpuSecondaryPatternChecksumLine',
            'GpuSetScanout1Line',
            'GpuStageBCapacityLine',
            'GpuStageBInitialScanoutLine',
            'GpuStatusResetLine',
            'GpuTarget0Line',
            'GpuTarget1Line',
            'GpuTransfer1Line',
            'GpuSingleOutputProofLine',
            'GpuDualOutputProofLine',
            'VisualCaptureReason',
            'VisualScanout0Path',
            'VisualScanout0Signature',
            'VisualScanout1Path',
            'VisualScanout1Signature'
        )) {
            if (-not ($result.PSObject.Properties.Name -contains $propertyName)) {
                $result | Add-Member -NotePropertyName $propertyName -NotePropertyValue ''
            }
        }

        return $result
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
        $launcherState = Start-ProbeLauncher -Backend $spec.LauncherBackend -SerialLog $serialLog -LauncherStdOut $launcherStdOut -LauncherStdErr $launcherStdErr -EnableVisualCapture:$EnableVisualCapture -QmpPort $qmpPort
        $proc = $launcherState.Process

        $sentinelSeen = Wait-ForGuestEvidence -Process $proc -SerialLog $serialLog -Pattern $spec.WaitPattern -TimeoutSeconds $TimeoutSeconds

        if (-not $sentinelSeen) {
            if (-not $proc.HasExited) {
                Stop-ProcessTree -Process $proc
                [void]$proc.WaitForExit(5000)
            }
        } else {
            Start-Sleep -Seconds 1
            if ($EnableVisualCapture -and -not $proc.HasExited) {
                try {
                    $qmpSession = New-QmpSession -Port $qmpPort -TimeoutSeconds ([Math]::Min([Math]::Max($TimeoutSeconds, 10), 30))
                    try {
                        $captureAttemptNotes = New-Object System.Collections.Generic.List[string]
                        if ($stageLabel -eq 'stageA') {
                            $captureAttempts = @(
                                [pscustomobject]@{
                                    Name = 'gpu0-head0'
                                    Path = (Join-Path $captureRoot ("scanout0-{0}-gpu0-head0.png" -f $stageLabel))
                                    DeviceId = 'gpu0'
                                    Head = 0
                                    ScanoutId = 0
                                },
                                [pscustomobject]@{
                                    Name = 'primary'
                                    Path = (Join-Path $captureRoot ("scanout0-{0}-primary.png" -f $stageLabel))
                                    DeviceId = ''
                                    Head = -1
                                    ScanoutId = 0
                                }
                            )
                            $stageAFailedAssessment = $null

                            foreach ($attempt in $captureAttempts) {
                                try {
                                    Invoke-QmpScreendump -Session $qmpSession -Filename $attempt.Path -DeviceId $attempt.DeviceId -Head $attempt.Head
                                    if (-not (Wait-ForCaptureFile -Path $attempt.Path -TimeoutSeconds 2)) {
                                        $captureAttemptNotes.Add(("{0}=missing" -f $attempt.Name))
                                        continue
                                    }

                                    $visualScanout0Path = $attempt.Path
                                    $visualScanout0Assessment = Get-DiagnosticCaptureAssessment -ImagePath $attempt.Path -ScanoutId 0
                                    $visualScanout0 = $visualScanout0Assessment.Status
                                    $visualScanout0Signature = $visualScanout0Assessment.Signature
                                    $captureAttemptNotes.Add(("{0}={1}:{2}" -f $attempt.Name, $visualScanout0Assessment.Status, $visualScanout0Assessment.Reason))

                                    if ($visualScanout0 -eq 'confirmed') {
                                        $captureReason = 'scanout0 capture matched the expected diagnostic pattern'
                                        $visualCaptureStatus = 'captured'
                                        break
                                    }

                                    if ($visualScanout0 -eq 'failed' -and $stageAFailedAssessment -eq $null) {
                                        $stageAFailedAssessment = $visualScanout0Assessment
                                    }
                                } catch {
                                    $captureAttemptNotes.Add(("{0}=manual-check-required:{1}" -f $attempt.Name, $_.Exception.Message))
                                }
                            }

                            if ($visualScanout0 -eq 'confirmed') {
                                $captureReason = if ([string]::IsNullOrWhiteSpace($captureReason)) { 'scanout0 capture matched the expected diagnostic pattern' } else { $captureReason }
                            } elseif ($stageAFailedAssessment) {
                                $captureReason = $stageAFailedAssessment.Reason
                                $visualCaptureStatus = 'failed'
                                $visualScanout0 = 'failed'
                            } else {
                                $captureReason = if ($captureAttemptNotes.Count -gt 0) { $captureAttemptNotes -join ' | ' } else { 'capture assessment unavailable' }
                                $visualCaptureStatus = 'manual-check-required'
                                $visualScanout0 = 'manual-check-required'
                            }
                        } elseif ($stageLabel -eq 'stageB') {
                            $captureHeads = @(0, 1)
                            foreach ($head in $captureHeads) {
                                $capturePath = Join-Path $captureRoot ("scanout{0}-{1}.png" -f $head, $stageLabel)
                                Invoke-QmpScreendump -Session $qmpSession -Filename $capturePath -DeviceId 'gpu0' -Head $head
                                if ($head -eq 0) {
                                    $visualScanout0Path = $capturePath
                                    $visualScanout0Assessment = Get-DiagnosticCaptureAssessment -ImagePath $capturePath -ScanoutId 0
                                    $visualScanout0 = $visualScanout0Assessment.Status
                                    $visualScanout0Signature = $visualScanout0Assessment.Signature
                                } elseif ($head -eq 1) {
                                    $visualScanout1Path = $capturePath
                                    $visualScanout1Assessment = Get-DiagnosticCaptureAssessment -ImagePath $capturePath -ScanoutId 1
                                    $visualScanout1 = $visualScanout1Assessment.Status
                                    $visualScanout1Signature = $visualScanout1Assessment.Signature
                                }
                            }

                            if ($visualScanout0 -eq 'confirmed' -and $visualScanout1 -eq 'confirmed') {
                                $distinctPatternsConfirmed = ($visualScanout0Signature -ne $visualScanout1Signature)
                                $captureReason = if ($distinctPatternsConfirmed) { 'scanout0 and scanout1 captures differed as expected' } else { 'scanout0 and scanout1 captures matched unexpectedly' }
                                $visualCaptureStatus = if ($distinctPatternsConfirmed) { 'captured' } else { 'failed' }
                            } else {
                                $captureReason = if ($visualScanout0Assessment -and $visualScanout1Assessment) {
                                    "scanout0=$($visualScanout0Assessment.Status) scanout1=$($visualScanout1Assessment.Status)"
                                } else {
                                    'capture assessment unavailable'
                                }
                                $visualCaptureStatus = 'manual-check-required'
                            }
                        } elseif ($stageLabel -eq 'compositorFrame') {
                            $captureHeads = @(0, 1)
                            foreach ($head in $captureHeads) {
                                $capturePath = Join-Path $captureRoot ("scanout{0}-{1}.png" -f $head, $stageLabel)
                                Invoke-QmpScreendump -Session $qmpSession -Filename $capturePath -DeviceId 'gpu0' -Head $head
                                if ($head -eq 0) {
                                    $visualScanout0Path = $capturePath
                                    $visualScanout0Assessment = Get-CompositorCaptureAssessment -ImagePath $capturePath -ScanoutId 0
                                    $visualScanout0 = $visualScanout0Assessment.Status
                                    $visualScanout0Signature = $visualScanout0Assessment.Signature
                                    $compositorContentConfirmed0 = $visualScanout0Assessment.ContentConfirmed
                                } elseif ($head -eq 1) {
                                    $visualScanout1Path = $capturePath
                                    $visualScanout1Assessment = Get-CompositorCaptureAssessment -ImagePath $capturePath -ScanoutId 1
                                    $visualScanout1 = $visualScanout1Assessment.Status
                                    $visualScanout1Signature = $visualScanout1Assessment.Signature
                                    $compositorContentConfirmed1 = $visualScanout1Assessment.ContentConfirmed
                                }
                            }

                            $taskbarPrimaryOnlyConfirmed = ($visualScanout0Assessment -and $visualScanout0Assessment.TaskbarConfirmed -and $visualScanout1Assessment -and $visualScanout1Assessment.TaskbarConfirmed)
                            $viewportSplitConfirmed = ($visualScanout0Assessment -and $visualScanout1Assessment -and $visualScanout0Assessment.BackgroundSignature -ne $visualScanout1Assessment.BackgroundSignature)
                            $distinctPatternsConfirmed = $viewportSplitConfirmed

                            if ($visualScanout0 -eq 'confirmed' -and $visualScanout1 -eq 'confirmed' -and $taskbarPrimaryOnlyConfirmed -and $viewportSplitConfirmed) {
                                $captureReason = 'scanout0 and scanout1 compositor captures matched expected desktop layout'
                                $visualCaptureStatus = 'captured'
                            } elseif ($visualScanout0Assessment -and $visualScanout1Assessment) {
                                $captureReason = "scanout0=$($visualScanout0Assessment.Status) scanout1=$($visualScanout1Assessment.Status) taskbarPrimaryOnly=$taskbarPrimaryOnlyConfirmed viewportSplit=$viewportSplitConfirmed"
                                $visualCaptureStatus = 'manual-check-required'
                            } else {
                                $captureReason = 'capture assessment unavailable'
                                $visualCaptureStatus = 'manual-check-required'
                            }
                        }
                        $visualCaptureReason = $captureReason
                    } finally {
                        Close-QmpSession -Session $qmpSession
                    }
                } catch {
                    $captureReason = $_.Exception.Message
                    if ($stageLabel -eq 'stageA') {
                        $visualScanout0 = 'manual-check-required'
                    } else {
                        $visualScanout1 = 'manual-check-required'
                    }
                    $visualCaptureStatus = 'manual-check-required'
                    $visualCaptureReason = $captureReason
                }
            }
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
    $gpuMmioMappedLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] MMIO transport mapped; read-only sanity reads complete; controlled transport initialization begins')
    $gpuMmioBlockedLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] MMIO mapping blocked: [^\r\n]+')
    $gpuResetStepLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Init step: reset_device begin')
    $gpuStatusResetLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Status reset write=0x00 readback=0x00')
    $gpuAckLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Status ACKNOWLEDGE write=0x[0-9A-Fa-f]+ readback=0x[0-9A-Fa-f]+')
    $gpuDriverLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Status DRIVER write=0x[0-9A-Fa-f]+ readback=0x[0-9A-Fa-f]+')
    $gpuFeaturesOkStatusLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Status FEATURES_OK write=0x[0-9A-Fa-f]+ readback=0x[0-9A-Fa-f]+')
    $gpuDriverOkStatusLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Status DRIVER_OK write=0x[0-9A-Fa-f]+ readback=0x[0-9A-Fa-f]+')
    $gpuFeatureBitmapLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Feature bitmap rawLow=0x[0-9A-Fa-f]+ rawHigh=0x[0-9A-Fa-f]+ raw=0x[0-9A-Fa-f]+')
    $gpuPreRenderDeviceConfigLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] pre-render Device config numScanouts=\d+ numCapsets=\d+')
    $gpuPreRenderDisplayInfoBeginLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] pre-render GET_DISPLAY_INFO begin')
    $gpuPreRenderCompletionLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] pre-render completion usedIdx=\d+ usedLen=\d+ headDescriptor=\d+')
    $gpuPreRenderDisplayInfoSummaryLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] pre-render GET_DISPLAY_INFO protocolSlots=\d+ enabledScanouts=\d+ disabledScanouts=\d+ deviceConfigNumScanouts=\d+ qemuMaxOutputsIntent=\d+')
    $gpuDiagnosticTargetLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Diagnostic test pattern format=B8G8R8X8_UNORM selectedWidth=\d+ selectedHeight=\d+ bytesPerPixel=\d+ stride=\d+ totalBackingBytes=\d+ pageCount=\d+ entryCount<=\s+\d+ fallbackGeometry=(yes|no)')
    $gpuBackingLayoutLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Diagnostic backing layout backingVirtualBase=0x[0-9A-Fa-f]+ totalBackingBytes=\d+ totalPages=\d+ totalMemEntries=\d+ contiguousRunCount=\d+ coveredBytes=\d+ physicalCoverageValid=(yes|no) firstPhysRange=0x[0-9A-Fa-f]+-0x[0-9A-Fa-f]+ lastPhysRange=0x[0-9A-Fa-f]+-0x[0-9A-Fa-f]+')
    $gpuResourceCreateLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] RESOURCE_CREATE_2D result=ready resourceId=0x[0-9A-Fa-f]+ format=B8G8R8X8_UNORM width=\d+ height=\d+')
    $gpuResourceCreateMatches = [regex]::Matches($serialText, '\[VIRTIO-GPU\] RESOURCE_CREATE_2D result=ready resourceId=0x[0-9A-Fa-f]+ format=B8G8R8X8_UNORM width=\d+ height=\d+')
    $gpuAttachLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] RESOURCE_ATTACH_BACKING result=attached resourceId=0x[0-9A-Fa-f]+ backingVirtualBase=0x[0-9A-Fa-f]+ totalBackingBytes=\d+ totalPages=\d+ totalMemEntries=\d+ contiguousRunCount=\d+ coveredBytes=\d+ physicalCoverageValid=(yes|no) firstPhysRange=0x[0-9A-Fa-f]+-0x[0-9A-Fa-f]+ lastPhysRange=0x[0-9A-Fa-f]+-0x[0-9A-Fa-f]+')
    $gpuAttachMatches = [regex]::Matches($serialText, '\[VIRTIO-GPU\] RESOURCE_ATTACH_BACKING result=attached resourceId=0x[0-9A-Fa-f]+ backingVirtualBase=0x[0-9A-Fa-f]+ totalBackingBytes=\d+ totalPages=\d+ totalMemEntries=\d+ contiguousRunCount=\d+ coveredBytes=\d+ physicalCoverageValid=(yes|no) firstPhysRange=0x[0-9A-Fa-f]+-0x[0-9A-Fa-f]+ lastPhysRange=0x[0-9A-Fa-f]+-0x[0-9A-Fa-f]+')
    $gpuSecondaryPatternChecksumLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Diagnostic pattern name=scanout1-red-orange width=\d+ height=\d+ stride=\d+ byteCount=\d+ checksum=0x[0-9A-Fa-f]+')
    $gpuPrimaryPatternChecksumLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Diagnostic pattern name=scanout0-blue-cyan width=\d+ height=\d+ stride=\d+ byteCount=\d+ checksum=0x[0-9A-Fa-f]+')
    $gpuSetScanout0Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] SET_SCANOUT result=set scanoutId=0 resourceId=0x[0-9A-Fa-f]+ rect=0,0 \d+x\d+')
    $gpuSetScanout1Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] SET_SCANOUT result=set scanoutId=1 resourceId=0x[0-9A-Fa-f]+ rect=0,0 \d+x\d+')
    $gpuTransferLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] TRANSFER_TO_HOST_2D result=ok scanoutId=0 resourceId=0x[0-9A-Fa-f]+ rect=0,0 \d+x\d+ offset=0')
    $gpuTransfer1Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] TRANSFER_TO_HOST_2D result=ok scanoutId=1 resourceId=0x[0-9A-Fa-f]+ rect=0,0 \d+x\d+ offset=0')
    $gpuFlushLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] RESOURCE_FLUSH result=ok scanoutId=0 resourceId=0x[0-9A-Fa-f]+ rect=0,0 \d+x\d+')
    $gpuFlush1Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] RESOURCE_FLUSH result=ok scanoutId=1 resourceId=0x[0-9A-Fa-f]+ rect=0,0 \d+x\d+')
    $gpuPostRenderDisplayInfoBeginLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] post-render GET_DISPLAY_INFO begin')
    $gpuPostRenderCompletionLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] post-render completion usedIdx=\d+ usedLen=\d+ headDescriptor=\d+')
    $gpuPostRenderDisplayInfoSummaryLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] post-render GET_DISPLAY_INFO protocolSlots=\d+ enabledScanouts=\d+ disabledScanouts=\d+ deviceConfigNumScanouts=\d+ qemuMaxOutputsIntent=\d+')
    $gpuPostRenderScanout0Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] post-render scanout\[0\] enabled=yes x=0 y=0 width=\d+ height=\d+')
    $gpuPostRenderScanout1Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] post-render scanout\[1\] enabled=(yes|no) x=\d+ y=\d+ width=\d+ height=\d+')
    $gpuStageBCapacityLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Stage B scanout capacity deviceConfigNumScanouts=\d+ scanout1InitialEnabled=(yes|no) qemuMaxOutputsIntent=\d+')
    $gpuStageBInitialScanoutLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Diagnostic scanout 1 initial enabled=(yes|no) x=\d+ y=\d+ width=\d+ height=\d+')
    $gpuSingleOutputProofLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Single-output proof: resource1=ready backing1=valid scanout0=set transfer0=ok flush0=ok patternChecksum=0x[0-9A-Fa-f]+')
    $gpuDualOutputProofLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Dual-output proof: resource1=ready resource2=ready scanout0=set scanout1=set transfer0=ok transfer1=ok flush0=ok flush1=ok enabledScanoutsAfter=\d+ distinctPatterns=yes')
    $gpuCompositorTarget0PlanLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] compositor target plan target=1 scanoutId=0 resourceId=0x[0-9A-Fa-f]+ viewport=0,0 \d+x\d+ compositorPixelFormat=B8G8R8X8_UNORM resourcePixelFormat=B8G8R8X8_UNORM conversionRequired=(yes|no) stride=\d+ totalBytes=\d+ backingBytes=\d+ backingValid=(yes|no)')
    $gpuCompositorTarget0ResultLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] compositor target result target=1 scanoutId=0 resourceId=0x[0-9A-Fa-f]+ viewport=0,0 \d+x\d+ renderedByteCount=\d+ checksum=0x[0-9A-Fa-f]+ transfer=(ok|failed) flush=(ok|failed) contentMode=(compositor-single-frame|fallback-patterns-after-failure) fallbackPatterns=(yes|no)( reason=[^\r\n]+)?')
    $gpuCompositorTarget1PlanLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] compositor target plan target=2 scanoutId=1 resourceId=0x[0-9A-Fa-f]+ viewport=\d+,0 \d+x\d+ compositorPixelFormat=B8G8R8X8_UNORM resourcePixelFormat=B8G8R8X8_UNORM conversionRequired=(yes|no) stride=\d+ totalBytes=\d+ backingBytes=\d+ backingValid=(yes|no)')
    $gpuCompositorTarget1ResultLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] compositor target result target=2 scanoutId=1 resourceId=0x[0-9A-Fa-f]+ viewport=\d+,0 \d+x\d+ renderedByteCount=\d+ checksum=0x[0-9A-Fa-f]+ transfer=(ok|failed) flush=(ok|failed) contentMode=(compositor-single-frame|fallback-patterns-after-failure) fallbackPatterns=(yes|no)( reason=[^\r\n]+)?')
    $gpuCompositorProofLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] VirtioGPU compositor proof: outputs=2 targets=2 frameMode=single-shot target0Render=(ok|failed) target0Transfer=(ok|failed) target0Flush=(ok|failed) target1Render=(ok|failed) target1Transfer=(ok|failed) target1Flush=(ok|failed) virtualDesktop=\d+x\d+ taskbarPrimaryOnly=(yes|no) continuousPresentation=disabled')
    $gpuOutputInventoryLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] VirtioGPU outputs: configured=\d+ operational=\d+ connectorEnabled=\d+ presentationConfirmed=\d+ virtualDesktop=\d+x\d+ targets=\d+ backed=\d+ primaryOutput=\d+ protocolConnectorEnabledCount=\d+ operationalOutputCount=\d+ presentationConfirmedCount=\d+')
    $gpuOutputMatches = [regex]::Matches($serialText, '\[VIRTIO-GPU\] output\[\d+\]: source=virtio-gpu .*')
    $gpuMonitorMatches = [regex]::Matches($serialText, '\[VIRTIO-GPU\] monitor\[\d+\]: source=virtio-gpu .*')
    $gpuTargetMatches = [regex]::Matches($serialText, '\[VIRTIO-GPU\] target\[\d+\]: source=virtio-gpu .*')
    $gpuOutput0Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] output\[0\]: source=virtio-gpu scanout=0 resource=\d+ connectorEnabled=yes resourceBound=yes backingAttached=yes transferReady=yes presentReady=yes confirmed=yes operational=yes preferred=\d+,\d+ \d+x\d+ assigned=\d+,\d+ \d+x\d+ virtual=0,0 primary=yes active=yes')
    $gpuOutput1Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] output\[1\]: source=virtio-gpu scanout=1 resource=\d+ connectorEnabled=no resourceBound=yes backingAttached=yes transferReady=yes presentReady=yes confirmed=yes operational=yes preferred=\d+,\d+ \d+x\d+ assigned=\d+,\d+ \d+x\d+ virtual=\d+,\d+ primary=no active=yes')
    $gpuMonitor0Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] monitor\[1\]: source=virtio-gpu scanout=0 resource=\d+ primary=yes enabled=yes operational=yes connectorEnabled=yes resourceBound=yes backingAttached=yes transferReady=yes presentReady=yes confirmed=yes preferred=\d+,\d+ \d+x\d+ assigned=\d+,\d+ \d+x\d+ virtual=0,0')
    $gpuMonitor1Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] monitor\[2\]: source=virtio-gpu scanout=1 resource=\d+ primary=no enabled=yes operational=yes connectorEnabled=no resourceBound=yes backingAttached=yes transferReady=yes presentReady=yes confirmed=yes preferred=\d+,\d+ \d+x\d+ assigned=\d+,\d+ \d+x\d+ virtual=\d+,\d+')
    $gpuTarget0Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] target\[1\]: source=virtio-gpu .*scanout=0.*resource=\d+.*backed=yes')
    $gpuTarget1Line = [regex]::Match($serialText, '\[VIRTIO-GPU\] target\[2\]: source=virtio-gpu .*scanout=1.*resource=\d+.*backed=yes')
    $gpuFeatureNegotiationLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Feature negotiation status=(ok|failed) negotiated=0x[0-9A-Fa-f]+ deviceFeatures=0x[0-9A-Fa-f]+ rejected=0x[0-9A-Fa-f]+')
    $gpuQueueCountLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Common config queueCount=\d+ queueMax=\d+')
    $gpuQueueLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Control queue ready size=\d+ queueEnable=yes queueNotifyOff=\d+ notifyOffMultiplier=\d+ notifyOffsetBytes=\d+ notifyAddr=0x[0-9A-Fa-f]+ descVirt=0x[0-9A-Fa-f]+ desc=0x[0-9A-Fa-f]+ availVirt=0x[0-9A-Fa-f]+ avail=0x[0-9A-Fa-f]+ usedVirt=0x[0-9A-Fa-f]+ used=0x[0-9A-Fa-f]+ alignment=4096')
    $gpuDisplayInfoResponseLine = [regex]::Match($serialText, '\[VIRTIO-GPU\]\s+(pre-render|post-render)\s+GET_DISPLAY_INFO response type=0x[0-9A-Fa-f]+')
    $gpuScanoutLine = [regex]::Match($serialText, '\[VIRTIO-GPU\]\s+(pre-render|post-render)\s+scanout\[\d+\].*')
    $gpuEnabledScanoutMatches = [regex]::Matches($serialText, '\[VIRTIO-GPU\]\s+post-render\s+scanout\[\d+\] enabled=yes')
    $gpuProbeCompleteLine = [regex]::Match($serialText, '\[VIRTIO-GPU\] Probe complete: [^\r\n]+')
    $gpuOutputConfiguredCount = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'configured=(\d+)' -GroupIndex 1
    $gpuOutputOperationalCount = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'operational=(\d+)' -GroupIndex 1
    $gpuOutputConnectorEnabledCount = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'connectorEnabled=(\d+)' -GroupIndex 1
    $gpuOutputPresentationConfirmedCount = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'presentationConfirmed=(\d+)' -GroupIndex 1
    $gpuOutputVirtualDesktopWidth = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'virtualDesktop=(\d+)x(\d+)' -GroupIndex 1
    $gpuOutputVirtualDesktopHeight = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'virtualDesktop=(\d+)x(\d+)' -GroupIndex 2
    $gpuOutputTargetCount = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'targets=(\d+)' -GroupIndex 1
    $gpuOutputBackedTargetCount = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'backed=(\d+)' -GroupIndex 1
    $gpuOutputPrimaryOutput = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'primaryOutput=(\d+)' -GroupIndex 1
    $gpuOutputProtocolConnectorEnabledCount = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'protocolConnectorEnabledCount=(\d+)' -GroupIndex 1
    $gpuOutputOperationalOutputCount = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'operationalOutputCount=(\d+)' -GroupIndex 1
    $gpuOutputPresentationConfirmedCountDetailed = Get-TextMatchGroupValue -Text $gpuOutputInventoryLine.Value -Pattern 'presentationConfirmedCount=(\d+)' -GroupIndex 1
    $gpuOutput0ResourceId = Get-TextMatchGroupValue -Text $gpuOutput0Line.Value -Pattern 'resource=(\d+)' -GroupIndex 1
    $gpuOutput1ResourceId = Get-TextMatchGroupValue -Text $gpuOutput1Line.Value -Pattern 'resource=(\d+)' -GroupIndex 1
    $gpuOutput0AssignedWidth = Get-TextMatchGroupValue -Text $gpuOutput0Line.Value -Pattern 'assigned=\d+,\d+ (\d+)x(\d+)' -GroupIndex 1
    $gpuOutput0AssignedHeight = Get-TextMatchGroupValue -Text $gpuOutput0Line.Value -Pattern 'assigned=\d+,\d+ (\d+)x(\d+)' -GroupIndex 2
    $gpuOutput1AssignedWidth = Get-TextMatchGroupValue -Text $gpuOutput1Line.Value -Pattern 'assigned=\d+,\d+ (\d+)x(\d+)' -GroupIndex 1
    $gpuOutput1AssignedHeight = Get-TextMatchGroupValue -Text $gpuOutput1Line.Value -Pattern 'assigned=\d+,\d+ (\d+)x(\d+)' -GroupIndex 2
    $gpuOutput0VirtualX = Get-TextMatchGroupValue -Text $gpuOutput0Line.Value -Pattern 'virtual=(\d+),(\d+)' -GroupIndex 1
    $gpuOutput0VirtualY = Get-TextMatchGroupValue -Text $gpuOutput0Line.Value -Pattern 'virtual=(\d+),(\d+)' -GroupIndex 2
    $gpuOutput1VirtualX = Get-TextMatchGroupValue -Text $gpuOutput1Line.Value -Pattern 'virtual=(\d+),(\d+)' -GroupIndex 1
    $gpuOutput1VirtualY = Get-TextMatchGroupValue -Text $gpuOutput1Line.Value -Pattern 'virtual=(\d+),(\d+)' -GroupIndex 2
    $gpuOutput1PreferredWidth = Get-TextMatchGroupValue -Text $gpuOutput1Line.Value -Pattern 'preferred=\d+,\d+ (\d+)x(\d+)' -GroupIndex 1
    $gpuOutput1PreferredHeight = Get-TextMatchGroupValue -Text $gpuOutput1Line.Value -Pattern 'preferred=\d+,\d+ (\d+)x(\d+)' -GroupIndex 2
    $gpuMmioRequestBase = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'requestBase=0x([0-9A-Fa-f]+)' -GroupIndex 1
    $gpuMmioRequestLength = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'requestLength=0x([0-9A-Fa-f]+)' -GroupIndex 1
    $gpuMmioKernelVirtualBase = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'kernelVirtualBase=([^\s]+)' -GroupIndex 1
    $gpuMmioMappedVirtual = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'mappedVirtual=([^\s]+)' -GroupIndex 1
    $gpuMmioMappedLength = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'mappedLength=([^\s]+)' -GroupIndex 1
    $gpuMmioPages = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'pages=(\d+)' -GroupIndex 1
    $gpuMmioFlags = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'flags=0x([0-9A-Fa-f]+)' -GroupIndex 1
    $gpuMmioNonUser = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'nonUser=(yes|no)' -GroupIndex 1
    $gpuMmioNoExec = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'noExec=(yes|no)' -GroupIndex 1
    $gpuMmioUncached = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'uncached=(yes|no)' -GroupIndex 1
    $gpuMmioCacheAttrs = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'cacheAttrs=([^\s]+)' -GroupIndex 1
    $gpuMmioQemuProbeOnly = Get-TextMatchGroupValue -Text $gpuMmioReportLine.Value -Pattern 'qemuProbeOnly=(yes|no)' -GroupIndex 1

    $gpuResourceCreateSecondaryLine = if ($gpuResourceCreateMatches.Count -ge 2) { $gpuResourceCreateMatches[1].Value } else { '' }
    $gpuAttachSecondaryLine = if ($gpuAttachMatches.Count -ge 2) { $gpuAttachMatches[1].Value } else { '' }
    $gpuBackingPhysicalCoverageValid = Get-TextMatchGroupValue -Text $gpuBackingLayoutLine.Value -Pattern 'physicalCoverageValid=(yes|no)' -GroupIndex 1
    $gpuBackingMemEntryCount = Get-TextMatchGroupValue -Text $gpuBackingLayoutLine.Value -Pattern 'totalMemEntries=(\d+)' -GroupIndex 1
    $gpuBackingContiguousRunCount = Get-TextMatchGroupValue -Text $gpuBackingLayoutLine.Value -Pattern 'contiguousRunCount=(\d+)' -GroupIndex 1
    $gpuBackingCoveredBytes = Get-TextMatchGroupValue -Text $gpuBackingLayoutLine.Value -Pattern 'coveredBytes=(\d+)' -GroupIndex 1
    $gpuPrimaryPatternChecksum = Get-TextMatchGroupValue -Text $gpuPrimaryPatternChecksumLine.Value -Pattern 'checksum=(0x[0-9A-Fa-f]+)' -GroupIndex 1
    $gpuSecondaryPatternChecksum = Get-TextMatchGroupValue -Text $gpuSecondaryPatternChecksumLine.Value -Pattern 'checksum=(0x[0-9A-Fa-f]+)' -GroupIndex 1
    $gpuPreRenderEnabledScanouts = Get-TextMatchGroupValue -Text $gpuPreRenderDisplayInfoSummaryLine.Value -Pattern 'enabledScanouts=(\d+)' -GroupIndex 1
    $gpuPreRenderDisabledScanouts = Get-TextMatchGroupValue -Text $gpuPreRenderDisplayInfoSummaryLine.Value -Pattern 'disabledScanouts=(\d+)' -GroupIndex 1
    $gpuPreRenderDeviceConfigNumScanouts = Get-TextMatchGroupValue -Text $gpuPreRenderDisplayInfoSummaryLine.Value -Pattern 'deviceConfigNumScanouts=(\d+)' -GroupIndex 1
    $gpuPreRenderQemuMaxOutputsIntent = Get-TextMatchGroupValue -Text $gpuPreRenderDisplayInfoSummaryLine.Value -Pattern 'qemuMaxOutputsIntent=(\d+)' -GroupIndex 1
    $gpuPostRenderEnabledScanouts = Get-TextMatchGroupValue -Text $gpuPostRenderDisplayInfoSummaryLine.Value -Pattern 'enabledScanouts=(\d+)' -GroupIndex 1
    $gpuPostRenderDisabledScanouts = Get-TextMatchGroupValue -Text $gpuPostRenderDisplayInfoSummaryLine.Value -Pattern 'disabledScanouts=(\d+)' -GroupIndex 1
    $gpuPostRenderDeviceConfigNumScanouts = Get-TextMatchGroupValue -Text $gpuPostRenderDisplayInfoSummaryLine.Value -Pattern 'deviceConfigNumScanouts=(\d+)' -GroupIndex 1
    $gpuPostRenderQemuMaxOutputsIntent = Get-TextMatchGroupValue -Text $gpuPostRenderDisplayInfoSummaryLine.Value -Pattern 'qemuMaxOutputsIntent=(\d+)' -GroupIndex 1
    $gpuProbeCompleteDeviceConfigNumScanouts = Get-TextMatchGroupValue -Text $gpuProbeCompleteLine.Value -Pattern 'deviceConfigNumScanouts=(\d+)' -GroupIndex 1
    $gpuProbeCompleteEnabledScanoutsBefore = Get-TextMatchGroupValue -Text $gpuProbeCompleteLine.Value -Pattern 'enabledScanoutsBefore=(\d+)' -GroupIndex 1
    $gpuProbeCompleteDisabledScanoutsBefore = Get-TextMatchGroupValue -Text $gpuProbeCompleteLine.Value -Pattern 'disabledScanoutsBefore=(\d+)' -GroupIndex 1
    $gpuProbeCompleteEnabledScanoutsAfter = Get-TextMatchGroupValue -Text $gpuProbeCompleteLine.Value -Pattern 'enabledScanoutsAfter=(\d+)' -GroupIndex 1
    $gpuProbeCompleteDistinctPatterns = Get-TextMatchGroupValue -Text $gpuProbeCompleteLine.Value -Pattern 'distinctPatterns=([a-z]+)' -GroupIndex 1
    $gpuProbeCompleteQemuTwoUsableScanouts = Get-TextMatchGroupValue -Text $gpuProbeCompleteLine.Value -Pattern 'qemuTwoUsableScanouts=([a-z]+)' -GroupIndex 1
    $gpuProbeCompleteContentMode = Get-TextMatchGroupValue -Text $gpuProbeCompleteLine.Value -Pattern 'contentMode=([^\s]+)' -GroupIndex 1
    $gpuProbeCompleteFrameMode = Get-TextMatchGroupValue -Text $gpuProbeCompleteLine.Value -Pattern 'frameMode=([^\s]+)' -GroupIndex 1
    $gpuProbeCompleteContinuousPresentation = Get-TextMatchGroupValue -Text $gpuProbeCompleteLine.Value -Pattern 'continuousPresentation=([^\s]+)' -GroupIndex 1
    $gpuCompositorTarget0PlanSummary = if ($gpuCompositorTarget0PlanLine.Success) { $gpuCompositorTarget0PlanLine.Value } else { '' }
    $gpuCompositorTarget0ResultSummary = if ($gpuCompositorTarget0ResultLine.Success) { $gpuCompositorTarget0ResultLine.Value } else { '' }
    $gpuCompositorTarget1PlanSummary = if ($gpuCompositorTarget1PlanLine.Success) { $gpuCompositorTarget1PlanLine.Value } else { '' }
    $gpuCompositorTarget1ResultSummary = if ($gpuCompositorTarget1ResultLine.Success) { $gpuCompositorTarget1ResultLine.Value } else { '' }
    $gpuCompositorProofSummary = if ($gpuCompositorProofLine.Success) { $gpuCompositorProofLine.Value } else { '' }
    if ($stageLabel -eq 'stageB' -and $distinctPatternsConfirmed) {
        $dualOutputVisualProof = 'confirmed'
    } elseif ($stageLabel -eq 'stageA' -and $visualScanout0 -eq 'confirmed') {
        $dualOutputVisualProof = 'confirmed'
    } elseif ($stageLabel -eq 'compositorFrame' -and $compositorContentConfirmed0 -and $compositorContentConfirmed1 -and $taskbarPrimaryOnlyConfirmed -and $viewportSplitConfirmed) {
        $dualOutputVisualProof = 'confirmed'
    }

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
    $gpuDiagnosticsCaptured = $gpuProbeEnabledLine.Success -or $gpuProbeStartLine.Success -or $gpuCandidateLine.Success -or $gpuCapabilityWalkLine.Success -or $gpuInventoryLine.Success -or $gpuTransportLine.Success -or $gpuMmioReportLine.Success -or $gpuMmioSummaryLine.Success -or $gpuMmioMappedLine.Success -or $gpuMmioBlockedLine.Success -or $gpuResetStepLine.Success -or $gpuStatusResetLine.Success -or $gpuAckLine.Success -or $gpuDriverLine.Success -or $gpuFeaturesOkStatusLine.Success -or $gpuDriverOkStatusLine.Success -or $gpuFeatureBitmapLine.Success -or $gpuPreRenderDeviceConfigLine.Success -or $gpuPreRenderDisplayInfoBeginLine.Success -or $gpuPreRenderCompletionLine.Success -or $gpuPreRenderDisplayInfoSummaryLine.Success -or $gpuDiagnosticTargetLine.Success -or $gpuBackingLayoutLine.Success -or $gpuResourceCreateLine.Success -or $gpuAttachLine.Success -or $gpuPrimaryPatternChecksumLine.Success -or $gpuSetScanout0Line.Success -or $gpuTransferLine.Success -or $gpuFlushLine.Success -or $gpuPostRenderDisplayInfoBeginLine.Success -or $gpuPostRenderCompletionLine.Success -or $gpuPostRenderDisplayInfoSummaryLine.Success -or $gpuPostRenderScanout0Line.Success -or $gpuPostRenderScanout1Line.Success -or $gpuStageBCapacityLine.Success -or $gpuStageBInitialScanoutLine.Success -or $gpuSecondaryPatternChecksumLine.Success -or $gpuSetScanout1Line.Success -or $gpuTransfer1Line.Success -or $gpuFlush1Line.Success -or $gpuSingleOutputProofLine.Success -or $gpuDualOutputProofLine.Success -or $gpuCompositorTarget0PlanLine.Success -or $gpuCompositorTarget0ResultLine.Success -or $gpuCompositorTarget1PlanLine.Success -or $gpuCompositorTarget1ResultLine.Success -or $gpuCompositorProofLine.Success -or $gpuOutputInventoryLine.Success -or $gpuOutput0Line.Success -or $gpuOutput1Line.Success -or $gpuMonitor0Line.Success -or $gpuMonitor1Line.Success -or $gpuTarget0Line.Success -or $gpuTarget1Line.Success -or $gpuFeatureNegotiationLine.Success -or $gpuQueueCountLine.Success -or $gpuQueueLine.Success -or $gpuDisplayInfoResponseLine.Success -or $gpuScanoutLine.Success -or $gpuProbeCompleteLine.Success

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
        Assert-Condition -Backend $backendName -Name 'virtio-gpu reset step line' -Condition $gpuResetStepLine.Success -Detail 'expected the transport reset step to be logged'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu reset result line' -Condition $gpuStatusResetLine.Success -Detail 'expected bounded reset polling and a zero readback'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu acknowledge status line' -Condition $gpuAckLine.Success -Detail 'expected the ACKNOWLEDGE status transition'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu driver status line' -Condition $gpuDriverLine.Success -Detail 'expected the DRIVER status transition'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu features-ok status line' -Condition $gpuFeaturesOkStatusLine.Success -Detail 'expected the FEATURES_OK status transition'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu driver-ok status line' -Condition $gpuDriverOkStatusLine.Success -Detail 'expected the DRIVER_OK status transition'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu feature bitmap line' -Condition $gpuFeatureBitmapLine.Success -Detail 'expected raw feature bitmap logging'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu feature negotiation line' -Condition $gpuFeatureNegotiationLine.Success -Detail 'expected feature negotiation with VERSION_1'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu queue count line' -Condition $gpuQueueCountLine.Success -Detail 'expected control queue sizing diagnostics'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu queue setup line' -Condition $gpuQueueLine.Success -Detail 'expected the control queue layout and enablement log'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu pre-render config line' -Condition $gpuPreRenderDeviceConfigLine.Success -Detail 'expected the pre-render device-config snapshot'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu pre-render GET_DISPLAY_INFO begin line' -Condition $gpuPreRenderDisplayInfoBeginLine.Success -Detail 'expected the pre-render GET_DISPLAY_INFO submission'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu pre-render completion line' -Condition $gpuPreRenderCompletionLine.Success -Detail 'expected the pre-render used-ring completion log'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu GET_DISPLAY_INFO response line' -Condition $gpuDisplayInfoResponseLine.Success -Detail 'expected the response type to be logged'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu pre-render display-info summary line' -Condition $gpuPreRenderDisplayInfoSummaryLine.Success -Detail 'expected the pre-render scanout summary line'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu diagnostic target line' -Condition $gpuDiagnosticTargetLine.Success -Detail 'expected the selected diagnostic test pattern geometry to be logged'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu primary pattern checksum line' -Condition $gpuPrimaryPatternChecksumLine.Success -Detail 'expected the primary diagnostic checksum to be logged'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu backing layout line' -Condition $gpuBackingLayoutLine.Success -Detail 'expected the diagnostic backing layout line'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu output inventory line' -Condition $gpuOutputInventoryLine.Success -Detail 'expected the virtio-gpu output inventory summary'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu output 0 line' -Condition $gpuOutput0Line.Success -Detail 'expected the first operational output descriptor'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu monitor 0 line' -Condition $gpuMonitor0Line.Success -Detail 'expected the first DisplayMonitor descriptor'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu render target 0 line' -Condition $gpuTarget0Line.Success -Detail 'expected the first DisplayRenderTarget descriptor'
        Assert-Condition -Backend $backendName -Name 'virtio-gpu output inventory descriptor counts' -Condition ($gpuOutputMatches.Count -ge 1 -and $gpuMonitorMatches.Count -ge 1 -and $gpuTargetMatches.Count -ge 1) -Detail ("outputs={0} monitors={1} targets={2}" -f $gpuOutputMatches.Count, $gpuMonitorMatches.Count, $gpuTargetMatches.Count)
        if ($stageLabel -eq 'stageA') {
            Assert-Condition -Backend $backendName -Name 'virtio-gpu resource create count' -Condition ($gpuResourceCreateMatches.Count -eq 1) -Detail ("count={0}" -f $gpuResourceCreateMatches.Count)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu attach backing count' -Condition ($gpuAttachMatches.Count -eq 1) -Detail ("count={0}" -f $gpuAttachMatches.Count)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu set scanout0 line' -Condition $gpuSetScanout0Line.Success -Detail 'expected SET_SCANOUT for scanout 0 to succeed'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu transfer0 line' -Condition $gpuTransferLine.Success -Detail 'expected TRANSFER_TO_HOST_2D for scanout 0 to succeed'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu flush0 line' -Condition $gpuFlushLine.Success -Detail 'expected RESOURCE_FLUSH for scanout 0 to succeed'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu post-render GET_DISPLAY_INFO begin line' -Condition $gpuPostRenderDisplayInfoBeginLine.Success -Detail 'expected the post-render GET_DISPLAY_INFO submission'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu post-render completion line' -Condition $gpuPostRenderCompletionLine.Success -Detail 'expected the post-render used-ring completion log'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu post-render display-info summary line' -Condition $gpuPostRenderDisplayInfoSummaryLine.Success -Detail 'expected the post-render scanout summary line'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu scanout geometry lines' -Condition ($gpuScanoutLine.Success -and $gpuPostRenderScanout0Line.Success -and $gpuPostRenderScanout1Line.Success) -Detail 'expected pre- and post-render scanout geometry for the primary output'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu post-render scanout 1 disabled' -Condition ($gpuPostRenderScanout1Line.Success -and $gpuPostRenderScanout1Line.Value -match 'enabled=no') -Detail $gpuPostRenderScanout1Line.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu stage A output inventory counts' -Condition (
                $gpuOutputConfiguredCount -eq '1' -and
                $gpuOutputOperationalCount -eq '1' -and
                $gpuOutputConnectorEnabledCount -eq '1' -and
                $gpuOutputPresentationConfirmedCount -eq '1' -and
                $gpuOutputTargetCount -eq '1' -and
                $gpuOutputBackedTargetCount -eq '1' -and
                $gpuOutputPrimaryOutput -eq '0' -and
                $gpuOutputProtocolConnectorEnabledCount -eq '1' -and
                $gpuOutputOperationalOutputCount -eq '1' -and
                $gpuOutputPresentationConfirmedCountDetailed -eq '1'
            ) -Detail $gpuOutputInventoryLine.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu stage A output 1 absent' -Condition (-not $gpuOutput1Line.Success -and -not $gpuMonitor1Line.Success -and -not $gpuTarget1Line.Success) -Detail 'scanout 1 inventory should remain absent in the stage A build'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu stage A output 0 operational' -Condition ($gpuOutput0Line.Value -match 'connectorEnabled=yes' -and $gpuOutput0Line.Value -match 'resourceBound=yes' -and $gpuOutput0Line.Value -match 'presentReady=yes' -and $gpuOutput0Line.Value -match 'confirmed=yes' -and $gpuOutput0Line.Value -match 'operational=yes' -and $gpuOutput0VirtualX -eq '0' -and $gpuOutput0VirtualY -eq '0') -Detail $gpuOutput0Line.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu stage B gate absent' -Condition (-not $gpuStageBCapacityLine.Success -and -not $gpuSetScanout1Line.Success -and -not $gpuTransfer1Line.Success -and -not $gpuFlush1Line.Success -and -not $gpuSecondaryPatternChecksumLine.Success -and -not $gpuDualOutputProofLine.Success) -Detail 'stage B activation markers must remain absent from the stage A build'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu single-output proof line' -Condition $gpuSingleOutputProofLine.Success -Detail 'expected the single-output proof line after scanout 0 completes'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu probe completion line' -Condition $gpuProbeCompleteLine.Success -Detail 'expected the final probe summary line'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu probe completion fields' -Condition (
                $gpuProbeCompleteLine.Value -match 'mmioMapped=yes' -and
                $gpuProbeCompleteLine.Value -match 'mappingVirtual=0x[0-9A-Fa-f]+' -and
                $gpuProbeCompleteLine.Value -match 'pageCount=\d+' -and
                $gpuProbeCompleteLine.Value -match 'cacheMode=uc\(pcd\+pwt\)' -and
                $gpuProbeCompleteLine.Value -match 'sanityReads=ok' -and
                $gpuProbeCompleteLine.Value -match 'featuresOk=yes' -and
                $gpuProbeCompleteLine.Value -match 'controlq=ready' -and
                $gpuProbeCompleteLine.Value -match 'displayInfo=ok' -and
                $gpuProbeCompleteLine.Value -match 'scanoutSlots=16' -and
                $gpuProbeCompleteLine.Value -match 'deviceConfigNumScanouts=\d+' -and
                $gpuProbeCompleteLine.Value -match 'qemuMaxOutputsIntent=\d+' -and
                $gpuProbeCompleteLine.Value -match 'enabledScanoutsBefore=\d+' -and
                $gpuProbeCompleteLine.Value -match 'disabledScanoutsBefore=\d+' -and
                $gpuProbeCompleteLine.Value -match 'enabledScanoutsAfter=\d+' -and
                $gpuProbeCompleteLine.Value -match 'resource2d=ready' -and
                $gpuProbeCompleteLine.Value -match 'backing=attached' -and
                $gpuProbeCompleteLine.Value -match 'scanout0=set' -and
                $gpuProbeCompleteLine.Value -match 'transfer=ok' -and
                $gpuProbeCompleteLine.Value -match 'flush=ok' -and
                $gpuProbeCompleteLine.Value -match 'resource2dSecondary=blocked' -and
                $gpuProbeCompleteLine.Value -match 'backingSecondary=blocked' -and
                $gpuProbeCompleteLine.Value -match 'scanout1=blocked' -and
                $gpuProbeCompleteLine.Value -match 'transfer1=blocked' -and
                $gpuProbeCompleteLine.Value -match 'flush1=blocked' -and
                $gpuProbeCompleteLine.Value -match 'distinctPatterns=no' -and
                $gpuProbeCompleteLine.Value -match 'qemuTwoUsableScanouts=no' -and
                $gpuProbeCompleteLine.Value -match 'rendering=test-pattern-single-output' -and
                $gpuProbeCompleteLine.Value -match 'reason=dual-output scanout 1 test pattern milestone complete'
            ) -Detail $gpuProbeCompleteLine.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu visual scanout0 proof' -Condition ($EnableVisualCapture -and (($visualScanout0 -eq 'confirmed' -and (Test-Path -LiteralPath $visualScanout0Path) -and ((Get-Item -LiteralPath $visualScanout0Path).Length -gt 0)) -or $visualScanout0 -eq 'manual-check-required' -or $visualScanout0 -eq 'failed')) -Detail ("status={0} path={1}" -f $visualScanout0, $visualScanout0Path)
            $backendStatus = 'complete'
            $interpretation = 'QEMU virtio-gpu MMIO transport mapped, controlq initialized, scanout 0 test pattern rendered, and post-render display-info completed'
        } elseif ($stageLabel -eq 'compositorFrame') {
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor target 0 plan line' -Condition $gpuCompositorTarget0PlanLine.Success -Detail 'expected the primary compositor target plan'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor target 1 plan line' -Condition $gpuCompositorTarget1PlanLine.Success -Detail 'expected the secondary compositor target plan'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor target 0 result line' -Condition $gpuCompositorTarget0ResultLine.Success -Detail 'expected the primary compositor target result'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor target 1 result line' -Condition $gpuCompositorTarget1ResultLine.Success -Detail 'expected the secondary compositor target result'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor proof line' -Condition $gpuCompositorProofLine.Success -Detail 'expected the compositor proof line'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor content confirmations' -Condition ($compositorContentConfirmed0 -and $compositorContentConfirmed1) -Detail ("scanout0={0} scanout1={1}" -f $compositorContentConfirmed0, $compositorContentConfirmed1)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor taskbar primary only' -Condition $taskbarPrimaryOnlyConfirmed -Detail ("taskbarPrimaryOnly={0}" -f $taskbarPrimaryOnlyConfirmed)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor viewport split' -Condition $viewportSplitConfirmed -Detail ("viewportSplit={0}" -f $viewportSplitConfirmed)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor capture files' -Condition ($EnableVisualCapture -and (Test-Path -LiteralPath $visualScanout0Path) -and (Test-Path -LiteralPath $visualScanout1Path) -and ((Get-Item -LiteralPath $visualScanout0Path).Length -gt 0) -and ((Get-Item -LiteralPath $visualScanout1Path).Length -gt 0)) -Detail ("scanout0={0} scanout1={1}" -f $visualScanout0Path, $visualScanout1Path)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor scanout 0 confirmed' -Condition ($visualScanout0 -eq 'confirmed') -Detail $visualScanout0Assessment.Reason
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor scanout 1 confirmed' -Condition ($visualScanout1 -eq 'confirmed') -Detail $visualScanout1Assessment.Reason
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor output inventory counts' -Condition (
                $gpuOutputConfiguredCount -eq '2' -and
                $gpuOutputOperationalCount -eq '2' -and
                $gpuOutputConnectorEnabledCount -eq '1' -and
                $gpuOutputPresentationConfirmedCount -eq '2' -and
                $gpuOutputTargetCount -eq '2' -and
                $gpuOutputBackedTargetCount -eq '2' -and
                $gpuOutputPrimaryOutput -eq '0' -and
                $gpuOutputProtocolConnectorEnabledCount -eq '1' -and
                $gpuOutputOperationalOutputCount -eq '2' -and
                $gpuOutputPresentationConfirmedCountDetailed -eq '2'
            ) -Detail $gpuOutputInventoryLine.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor output descriptors present' -Condition ($gpuOutputMatches.Count -eq 2 -and $gpuMonitorMatches.Count -eq 2 -and $gpuTargetMatches.Count -eq 2) -Detail ("outputs={0} monitors={1} targets={2}" -f $gpuOutputMatches.Count, $gpuMonitorMatches.Count, $gpuTargetMatches.Count)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor output 0' -Condition ($gpuOutput0Line.Value -match 'connectorEnabled=yes' -and $gpuOutput0Line.Value -match 'resourceBound=yes' -and $gpuOutput0Line.Value -match 'backingAttached=yes' -and $gpuOutput0Line.Value -match 'transferReady=yes' -and $gpuOutput0Line.Value -match 'presentReady=yes' -and $gpuOutput0Line.Value -match 'confirmed=yes' -and $gpuOutput0Line.Value -match 'operational=yes' -and $gpuOutput0VirtualX -eq '0' -and $gpuOutput0VirtualY -eq '0') -Detail $gpuOutput0Line.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor output 1' -Condition ($gpuOutput1Line.Value -match 'connectorEnabled=no' -and $gpuOutput1Line.Value -match 'resourceBound=yes' -and $gpuOutput1Line.Value -match 'backingAttached=yes' -and $gpuOutput1Line.Value -match 'transferReady=yes' -and $gpuOutput1Line.Value -match 'presentReady=yes' -and $gpuOutput1Line.Value -match 'confirmed=yes' -and $gpuOutput1Line.Value -match 'operational=yes' -and $gpuOutput1Line.Value -match 'primary=no' -and $gpuOutput1Line.Value -match 'active=yes' -and $gpuOutput1VirtualX -eq $gpuOutput0AssignedWidth -and $gpuOutput1VirtualY -eq '0') -Detail $gpuOutput1Line.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor target lines' -Condition ($gpuTarget0Line.Success -and $gpuTarget1Line.Success) -Detail ("target0={0} target1={1}" -f $gpuTarget0Line.Value, $gpuTarget1Line.Value)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor probe completion line' -Condition $gpuProbeCompleteLine.Success -Detail 'expected the final probe summary line'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu compositor probe completion fields' -Condition (
                $gpuProbeCompleteLine.Value -match 'mmioMapped=yes' -and
                $gpuProbeCompleteLine.Value -match 'mappingVirtual=0x[0-9A-Fa-f]+' -and
                $gpuProbeCompleteLine.Value -match 'pageCount=\d+' -and
                $gpuProbeCompleteLine.Value -match 'cacheMode=uc\(pcd\+pwt\)' -and
                $gpuProbeCompleteLine.Value -match 'sanityReads=ok' -and
                $gpuProbeCompleteLine.Value -match 'featuresOk=yes' -and
                $gpuProbeCompleteLine.Value -match 'controlq=ready' -and
                $gpuProbeCompleteLine.Value -match 'displayInfo=ok' -and
                $gpuProbeCompleteLine.Value -match 'scanoutSlots=16' -and
                $gpuProbeCompleteLine.Value -match 'deviceConfigNumScanouts=\d+' -and
                $gpuProbeCompleteLine.Value -match 'qemuMaxOutputsIntent=\d+' -and
                $gpuProbeCompleteLine.Value -match 'enabledScanoutsBefore=\d+' -and
                $gpuProbeCompleteLine.Value -match 'disabledScanoutsBefore=\d+' -and
                $gpuProbeCompleteLine.Value -match 'enabledScanoutsAfter=\d+' -and
                $gpuProbeCompleteLine.Value -match 'resource2d=ready' -and
                $gpuProbeCompleteLine.Value -match 'backing=attached' -and
                $gpuProbeCompleteLine.Value -match 'scanout0=set' -and
                $gpuProbeCompleteLine.Value -match 'transfer=ok' -and
                $gpuProbeCompleteLine.Value -match 'flush=ok' -and
                $gpuProbeCompleteLine.Value -match 'resource2dSecondary=ready' -and
                $gpuProbeCompleteLine.Value -match 'backingSecondary=attached' -and
                $gpuProbeCompleteLine.Value -match 'scanout1=set' -and
                $gpuProbeCompleteLine.Value -match 'transfer1=ok' -and
                $gpuProbeCompleteLine.Value -match 'flush1=ok' -and
                $gpuProbeCompleteLine.Value -match 'distinctPatterns=yes' -and
                $gpuProbeCompleteLine.Value -match 'qemuTwoUsableScanouts=no' -and
                $gpuProbeCompleteLine.Value -match 'contentMode=compositor-single-frame' -and
                $gpuProbeCompleteLine.Value -match 'frameMode=single-shot' -and
                $gpuProbeCompleteLine.Value -match 'continuousPresentation=disabled' -and
                $gpuProbeCompleteLine.Value -match 'rendering=dual-output-test-pattern'
            ) -Detail $gpuProbeCompleteLine.Value
            $backendStatus = 'complete'
            $interpretation = 'QEMU virtio-gpu compositor frame rendered once into both scanouts and post-render display-info completed'
        } else {
            Assert-Condition -Backend $backendName -Name 'virtio-gpu stage B capacity line' -Condition $gpuStageBCapacityLine.Success -Detail 'expected Stage B capacity evidence'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu stage B initial scanout line' -Condition $gpuStageBInitialScanoutLine.Success -Detail 'expected scanout 1 initial state evidence'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu secondary pattern checksum line' -Condition $gpuSecondaryPatternChecksumLine.Success -Detail 'expected the secondary diagnostic checksum to be logged'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu resource create count' -Condition ($gpuResourceCreateMatches.Count -ge 2) -Detail ("count={0}" -f $gpuResourceCreateMatches.Count)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu attach backing count' -Condition ($gpuAttachMatches.Count -ge 2) -Detail ("count={0}" -f $gpuAttachMatches.Count)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu set scanout0 line' -Condition $gpuSetScanout0Line.Success -Detail 'expected SET_SCANOUT for scanout 0 to succeed'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu set scanout1 line' -Condition $gpuSetScanout1Line.Success -Detail 'expected SET_SCANOUT for scanout 1 to succeed'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu transfer0 line' -Condition $gpuTransferLine.Success -Detail 'expected TRANSFER_TO_HOST_2D for scanout 0 to succeed'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu transfer1 line' -Condition $gpuTransfer1Line.Success -Detail 'expected TRANSFER_TO_HOST_2D for scanout 1 to succeed'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu flush0 line' -Condition $gpuFlushLine.Success -Detail 'expected RESOURCE_FLUSH for scanout 0 to succeed'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu flush1 line' -Condition $gpuFlush1Line.Success -Detail 'expected RESOURCE_FLUSH for scanout 1 to succeed'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu post-render GET_DISPLAY_INFO begin line' -Condition $gpuPostRenderDisplayInfoBeginLine.Success -Detail 'expected the post-render GET_DISPLAY_INFO submission'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu post-render completion line' -Condition $gpuPostRenderCompletionLine.Success -Detail 'expected the post-render used-ring completion log'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu post-render display-info summary line' -Condition $gpuPostRenderDisplayInfoSummaryLine.Success -Detail 'expected the post-render scanout summary line'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu scanout geometry lines' -Condition ($gpuScanoutLine.Success -and $gpuPostRenderScanout0Line.Success -and $gpuPostRenderScanout1Line.Success) -Detail 'expected pre- and post-render scanout geometry for both outputs'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu post-render scanout 1 connector disabled' -Condition ($gpuPostRenderScanout1Line.Success -and $gpuPostRenderScanout1Line.Value -match 'enabled=no') -Detail $gpuPostRenderScanout1Line.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu single-output proof line' -Condition $gpuSingleOutputProofLine.Success -Detail 'expected the single-output proof line to remain present before the dual-output proof'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu dual-output proof line' -Condition $gpuDualOutputProofLine.Success -Detail 'expected the dual-output proof line after scanout 1 completes'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu output inventory counts' -Condition (
                $gpuOutputConfiguredCount -eq '2' -and
                $gpuOutputOperationalCount -eq '2' -and
                $gpuOutputConnectorEnabledCount -eq '1' -and
                $gpuOutputPresentationConfirmedCount -eq '2' -and
                $gpuOutputTargetCount -eq '2' -and
                $gpuOutputBackedTargetCount -eq '2' -and
                $gpuOutputPrimaryOutput -eq '0' -and
                $gpuOutputProtocolConnectorEnabledCount -eq '1' -and
                $gpuOutputOperationalOutputCount -eq '2' -and
                $gpuOutputPresentationConfirmedCountDetailed -eq '2'
            ) -Detail $gpuOutputInventoryLine.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu output descriptors present' -Condition ($gpuOutputMatches.Count -eq 2 -and $gpuMonitorMatches.Count -eq 2 -and $gpuTargetMatches.Count -eq 2) -Detail ("outputs={0} monitors={1} targets={2}" -f $gpuOutputMatches.Count, $gpuMonitorMatches.Count, $gpuTargetMatches.Count)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu output resources distinct' -Condition ($gpuOutput0ResourceId -ne $gpuOutput1ResourceId) -Detail ("resource0={0} resource1={1}" -f $gpuOutput0ResourceId, $gpuOutput1ResourceId)
            Assert-Condition -Backend $backendName -Name 'virtio-gpu output 1 operational' -Condition ($gpuOutput1Line.Value -match 'connectorEnabled=no' -and $gpuOutput1Line.Value -match 'resourceBound=yes' -and $gpuOutput1Line.Value -match 'backingAttached=yes' -and $gpuOutput1Line.Value -match 'transferReady=yes' -and $gpuOutput1Line.Value -match 'presentReady=yes' -and $gpuOutput1Line.Value -match 'confirmed=yes' -and $gpuOutput1Line.Value -match 'operational=yes' -and $gpuOutput1Line.Value -match 'primary=no' -and $gpuOutput1Line.Value -match 'active=yes') -Detail $gpuOutput1Line.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu output 1 preferred geometry separate' -Condition ($gpuOutput1PreferredWidth -eq '0' -and $gpuOutput1PreferredHeight -eq '0') -Detail $gpuOutput1Line.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu output 1 virtual origin' -Condition ($gpuOutput1VirtualX -eq $gpuOutput0AssignedWidth -and $gpuOutput1VirtualY -eq '0') -Detail $gpuOutput1Line.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu virtual desktop bounds' -Condition ($gpuOutputInventoryLine.Value -match ('virtualDesktop={0}x{1}' -f ([int]$gpuOutput0AssignedWidth + [int]$gpuOutput1AssignedWidth), [Math]::Max([int]$gpuOutput0AssignedHeight, [int]$gpuOutput1AssignedHeight))) -Detail $gpuOutputInventoryLine.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu probe completion line' -Condition $gpuProbeCompleteLine.Success -Detail 'expected the final probe summary line'
            Assert-Condition -Backend $backendName -Name 'virtio-gpu probe completion fields' -Condition (
                $gpuProbeCompleteLine.Value -match 'mmioMapped=yes' -and
                $gpuProbeCompleteLine.Value -match 'mappingVirtual=0x[0-9A-Fa-f]+' -and
                $gpuProbeCompleteLine.Value -match 'pageCount=\d+' -and
                $gpuProbeCompleteLine.Value -match 'cacheMode=uc\(pcd\+pwt\)' -and
                $gpuProbeCompleteLine.Value -match 'sanityReads=ok' -and
                $gpuProbeCompleteLine.Value -match 'featuresOk=yes' -and
                $gpuProbeCompleteLine.Value -match 'controlq=ready' -and
                $gpuProbeCompleteLine.Value -match 'displayInfo=ok' -and
                $gpuProbeCompleteLine.Value -match 'scanoutSlots=16' -and
                $gpuProbeCompleteLine.Value -match 'deviceConfigNumScanouts=\d+' -and
                $gpuProbeCompleteLine.Value -match 'qemuMaxOutputsIntent=\d+' -and
                $gpuProbeCompleteLine.Value -match 'enabledScanoutsBefore=\d+' -and
                $gpuProbeCompleteLine.Value -match 'disabledScanoutsBefore=\d+' -and
                $gpuProbeCompleteLine.Value -match 'enabledScanoutsAfter=\d+' -and
                $gpuProbeCompleteLine.Value -match 'resource2d=ready' -and
                $gpuProbeCompleteLine.Value -match 'backing=attached' -and
                $gpuProbeCompleteLine.Value -match 'scanout0=set' -and
                $gpuProbeCompleteLine.Value -match 'transfer=ok' -and
                $gpuProbeCompleteLine.Value -match 'flush=ok' -and
                $gpuProbeCompleteLine.Value -match 'resource2dSecondary=ready' -and
                $gpuProbeCompleteLine.Value -match 'backingSecondary=attached' -and
                $gpuProbeCompleteLine.Value -match 'scanout1=set' -and
                $gpuProbeCompleteLine.Value -match 'transfer1=ok' -and
                $gpuProbeCompleteLine.Value -match 'flush1=ok' -and
                $gpuProbeCompleteLine.Value -match 'distinctPatterns=yes' -and
                $gpuProbeCompleteLine.Value -match 'qemuTwoUsableScanouts=no' -and
                $gpuProbeCompleteLine.Value -match 'rendering=dual-output-test-pattern' -and
                $gpuProbeCompleteLine.Value -match 'reason=dual-output scanout 1 test pattern milestone complete'
            ) -Detail $gpuProbeCompleteLine.Value
            Assert-Condition -Backend $backendName -Name 'virtio-gpu distinct visual capture' -Condition ($EnableVisualCapture -and $visualScanout0 -eq 'confirmed' -and $visualScanout1 -eq 'confirmed' -and $distinctPatternsConfirmed -and (Test-Path -LiteralPath $visualScanout0Path) -and (Test-Path -LiteralPath $visualScanout1Path) -and ((Get-Item -LiteralPath $visualScanout0Path).Length -gt 0) -and ((Get-Item -LiteralPath $visualScanout1Path).Length -gt 0)) -Detail ("scanout0={0} scanout1={1} distinct={2}" -f $visualScanout0, $visualScanout1, $distinctPatternsConfirmed)
            $backendStatus = 'complete'
            $interpretation = 'QEMU virtio-gpu MMIO transport mapped, controlq initialized, scanout 0 and scanout 1 test patterns rendered, and post-render display-info completed'
        }
    }

    $launched = $launchRecorded
    $bootloaderSerialCaptured = $bootloaderSerialAppeared
    $framebufferReady = $kernelFramebufferReady.Success

    if ($backendName -notlike 'virtio-gpu*') {
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
    } else {
        if ([string]::IsNullOrWhiteSpace($backendStatus)) {
            $backendStatus = 'complete'
        }
        if ([string]::IsNullOrWhiteSpace($interpretation)) {
            $interpretation = 'QEMU virtio-gpu diagnostics captured'
        }
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
        "gpuStatusResetLine=$($gpuStatusResetLine.Value)"
        "gpuAckLine=$($gpuAckLine.Value)"
        "gpuDriverLine=$($gpuDriverLine.Value)"
        "gpuFeaturesOkStatusLine=$($gpuFeaturesOkStatusLine.Value)"
        "gpuDriverOkStatusLine=$($gpuDriverOkStatusLine.Value)"
        "gpuFeatureBitmapLine=$($gpuFeatureBitmapLine.Value)"
        "gpuPreRenderDeviceConfigLine=$($gpuPreRenderDeviceConfigLine.Value)"
        "gpuPreRenderDisplayInfoBeginLine=$($gpuPreRenderDisplayInfoBeginLine.Value)"
        "gpuPreRenderCompletionLine=$($gpuPreRenderCompletionLine.Value)"
        "gpuPreRenderDisplayInfoSummaryLine=$($gpuPreRenderDisplayInfoSummaryLine.Value)"
        "gpuDiagnosticTargetLine=$($gpuDiagnosticTargetLine.Value)"
        "gpuBackingLayoutLine=$($gpuBackingLayoutLine.Value)"
        "gpuResourceCreateLine=$($gpuResourceCreateLine.Value)"
        "gpuAttachLine=$($gpuAttachLine.Value)"
        "gpuSetScanoutLine=$($gpuSetScanoutLine.Value)"
        "gpuTransferLine=$($gpuTransferLine.Value)"
        "gpuFlushLine=$($gpuFlushLine.Value)"
        "gpuPostRenderDisplayInfoBeginLine=$($gpuPostRenderDisplayInfoBeginLine.Value)"
        "gpuPostRenderCompletionLine=$($gpuPostRenderCompletionLine.Value)"
        "gpuPostRenderDisplayInfoSummaryLine=$($gpuPostRenderDisplayInfoSummaryLine.Value)"
        "gpuPostRenderScanout0Line=$($gpuPostRenderScanout0Line.Value)"
        "gpuPostRenderScanout1Line=$($gpuPostRenderScanout1Line.Value)"
        "gpuFeatureNegotiationLine=$($gpuFeatureNegotiationLine.Value)"
        "gpuQueueCountLine=$($gpuQueueCountLine.Value)"
        "gpuQueueLine=$($gpuQueueLine.Value)"
        "gpuDisplayInfoResponseLine=$($gpuDisplayInfoResponseLine.Value)"
        "gpuScanoutLine=$($gpuScanoutLine.Value)"
        "gpuEnabledScanoutCount=$($gpuEnabledScanoutMatches.Count)"
        "gpuProbeCompleteLine=$($gpuProbeCompleteLine.Value)"
        "gpuProbeCompleteContentMode=$gpuProbeCompleteContentMode"
        "gpuProbeCompleteFrameMode=$gpuProbeCompleteFrameMode"
        "gpuProbeCompleteContinuousPresentation=$gpuProbeCompleteContinuousPresentation"
        "gpuOutputInventoryLine=$($gpuOutputInventoryLine.Value)"
        "gpuOutputConfiguredCount=$gpuOutputConfiguredCount"
        "gpuOutputOperationalCount=$gpuOutputOperationalCount"
        "gpuOutputConnectorEnabledCount=$gpuOutputConnectorEnabledCount"
        "gpuOutputPresentationConfirmedCount=$gpuOutputPresentationConfirmedCount"
        "gpuOutputVirtualDesktopWidth=$gpuOutputVirtualDesktopWidth"
        "gpuOutputVirtualDesktopHeight=$gpuOutputVirtualDesktopHeight"
        "gpuOutputTargetCount=$gpuOutputTargetCount"
        "gpuOutputBackedTargetCount=$gpuOutputBackedTargetCount"
        "gpuOutputPrimaryOutput=$gpuOutputPrimaryOutput"
        "gpuOutputProtocolConnectorEnabledCount=$gpuOutputProtocolConnectorEnabledCount"
        "gpuOutputOperationalOutputCount=$gpuOutputOperationalOutputCount"
        "gpuOutputPresentationConfirmedCountDetailed=$gpuOutputPresentationConfirmedCountDetailed"
        "gpuOutput0Line=$($gpuOutput0Line.Value)"
        "gpuOutput1Line=$($gpuOutput1Line.Value)"
        "gpuMonitor0Line=$($gpuMonitor0Line.Value)"
        "gpuMonitor1Line=$($gpuMonitor1Line.Value)"
        "gpuTarget0Line=$($gpuTarget0Line.Value)"
        "gpuTarget1Line=$($gpuTarget1Line.Value)"
        "gpuCompositorTarget0PlanLine=$gpuCompositorTarget0PlanSummary"
        "gpuCompositorTarget0ResultLine=$gpuCompositorTarget0ResultSummary"
        "gpuCompositorTarget1PlanLine=$gpuCompositorTarget1PlanSummary"
        "gpuCompositorTarget1ResultLine=$gpuCompositorTarget1ResultSummary"
        "gpuCompositorProofLine=$gpuCompositorProofSummary"
        "compositorContentConfirmed0=$compositorContentConfirmed0"
        "compositorContentConfirmed1=$compositorContentConfirmed1"
        "taskbarPrimaryOnlyConfirmed=$taskbarPrimaryOnlyConfirmed"
        "viewportSplitConfirmed=$viewportSplitConfirmed"
        "probeStage=$stageLabel"
        "probeMode=$Mode"
        "visualCaptureStatus=$visualCaptureStatus"
        "visualScanout0=$visualScanout0"
        "visualScanout1=$visualScanout1"
        "visualScanout0Path=$visualScanout0Path"
        "visualScanout1Path=$visualScanout1Path"
        "visualScanout0Signature=$visualScanout0Signature"
        "visualScanout1Signature=$visualScanout1Signature"
        "visualCaptureReason=$visualCaptureReason"
        "distinctPatternsConfirmed=$distinctPatternsConfirmed"
        "dualOutputVisualProof=$dualOutputVisualProof"
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
            ProbeStage = $stageLabel
            BackendRoot = $backendRoot
            LauncherStdOut = $launcherStdOut
            LauncherStdErr = $launcherStdErr
            SerialLog = $serialLog
            SummaryPath = $summaryPath
            CaptureRoot = $captureRoot
            VisualCaptureStatus = $visualCaptureStatus
            VisualScanout0 = $visualScanout0
            VisualScanout1 = $visualScanout1
            VisualScanout0Path = $visualScanout0Path
            VisualScanout1Path = $visualScanout1Path
            VisualScanout0Signature = $visualScanout0Signature
            VisualScanout1Signature = $visualScanout1Signature
            VisualCaptureReason = $visualCaptureReason
            DistinctPatternsConfirmed = $distinctPatternsConfirmed
            DualOutputVisualProof = $dualOutputVisualProof
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
        GpuMmioRequestBase = $gpuMmioRequestBase
        GpuMmioRequestLength = $gpuMmioRequestLength
        GpuMmioKernelVirtualBase = $gpuMmioKernelVirtualBase
        GpuMmioMappedVirtual = $gpuMmioMappedVirtual
        GpuMmioMappedLength = $gpuMmioMappedLength
        GpuMmioPages = $gpuMmioPages
        GpuMmioFlags = $gpuMmioFlags
        GpuMmioNonUser = $gpuMmioNonUser
        GpuMmioNoExec = $gpuMmioNoExec
        GpuMmioUncached = $gpuMmioUncached
        GpuMmioCacheAttrs = $gpuMmioCacheAttrs
        GpuMmioQemuProbeOnly = $gpuMmioQemuProbeOnly
        GpuMmioSummaryLine = $gpuMmioSummaryLine.Value
        GpuMmioMappedLine = $gpuMmioMappedLine.Value
        GpuMmioBlockedLine = $gpuMmioBlockedLine.Value
        GpuResetStepLine = $gpuResetStepLine.Value
        GpuStatusResetLine = $gpuStatusResetLine.Value
        GpuAckLine = $gpuAckLine.Value
        GpuDriverLine = $gpuDriverLine.Value
        GpuFeaturesOkStatusLine = $gpuFeaturesOkStatusLine.Value
        GpuDriverOkStatusLine = $gpuDriverOkStatusLine.Value
        GpuFeatureBitmapLine = $gpuFeatureBitmapLine.Value
        GpuPreRenderDeviceConfigLine = $gpuPreRenderDeviceConfigLine.Value
        GpuPreRenderDisplayInfoBeginLine = $gpuPreRenderDisplayInfoBeginLine.Value
        GpuPreRenderCompletionLine = $gpuPreRenderCompletionLine.Value
        GpuPreRenderDisplayInfoSummaryLine = $gpuPreRenderDisplayInfoSummaryLine.Value
        GpuDiagnosticTargetLine = $gpuDiagnosticTargetLine.Value
        GpuBackingLayoutLine = $gpuBackingLayoutLine.Value
        GpuBackingPhysicalCoverageValid = $gpuBackingPhysicalCoverageValid
        GpuBackingMemEntryCount = $gpuBackingMemEntryCount
        GpuBackingContiguousRunCount = $gpuBackingContiguousRunCount
        GpuBackingCoveredBytes = $gpuBackingCoveredBytes
        GpuPrimaryPatternChecksumLine = $gpuPrimaryPatternChecksumLine.Value
        GpuPrimaryPatternChecksum = $gpuPrimaryPatternChecksum
        GpuSecondaryPatternChecksumLine = $gpuSecondaryPatternChecksumLine.Value
        GpuSecondaryPatternChecksum = $gpuSecondaryPatternChecksum
        GpuStageBCapacityLine = $gpuStageBCapacityLine.Value
        GpuStageBInitialScanoutLine = $gpuStageBInitialScanoutLine.Value
        GpuResourceCreateLine = $gpuResourceCreateLine.Value
        GpuResourceCreateSecondaryLine = $gpuResourceCreateSecondaryLine
        GpuAttachLine = $gpuAttachLine.Value
        GpuAttachSecondaryLine = $gpuAttachSecondaryLine
        GpuSetScanoutLine = $gpuSetScanoutLine.Value
        GpuSetScanout1Line = $gpuSetScanout1Line.Value
        GpuTransferLine = $gpuTransferLine.Value
        GpuTransfer1Line = $gpuTransfer1Line.Value
        GpuFlushLine = $gpuFlushLine.Value
        GpuFlush1Line = $gpuFlush1Line.Value
        GpuPostRenderDisplayInfoBeginLine = $gpuPostRenderDisplayInfoBeginLine.Value
        GpuPostRenderCompletionLine = $gpuPostRenderCompletionLine.Value
        GpuPostRenderDisplayInfoSummaryLine = $gpuPostRenderDisplayInfoSummaryLine.Value
        GpuPostRenderScanout0Line = $gpuPostRenderScanout0Line.Value
        GpuPostRenderScanout1Line = $gpuPostRenderScanout1Line.Value
        GpuPreRenderEnabledScanouts = $gpuPreRenderEnabledScanouts
        GpuPreRenderDisabledScanouts = $gpuPreRenderDisabledScanouts
        GpuPreRenderDeviceConfigNumScanouts = $gpuPreRenderDeviceConfigNumScanouts
        GpuPreRenderQemuMaxOutputsIntent = $gpuPreRenderQemuMaxOutputsIntent
        GpuPostRenderEnabledScanouts = $gpuPostRenderEnabledScanouts
        GpuPostRenderDisabledScanouts = $gpuPostRenderDisabledScanouts
        GpuPostRenderDeviceConfigNumScanouts = $gpuPostRenderDeviceConfigNumScanouts
        GpuPostRenderQemuMaxOutputsIntent = $gpuPostRenderQemuMaxOutputsIntent
        GpuFeatureNegotiationLine = $gpuFeatureNegotiationLine.Value
        GpuQueueCountLine = $gpuQueueCountLine.Value
        GpuQueueLine = $gpuQueueLine.Value
        GpuDisplayInfoResponseLine = $gpuDisplayInfoResponseLine.Value
        GpuScanoutLine = $gpuScanoutLine.Value
        GpuEnabledScanoutCount = $gpuEnabledScanoutMatches.Count
        GpuProbeCompleteLine = $gpuProbeCompleteLine.Value
        GpuProbeCompleteDeviceConfigNumScanouts = $gpuProbeCompleteDeviceConfigNumScanouts
        GpuProbeCompleteEnabledScanoutsBefore = $gpuProbeCompleteEnabledScanoutsBefore
        GpuProbeCompleteDisabledScanoutsBefore = $gpuProbeCompleteDisabledScanoutsBefore
        GpuProbeCompleteEnabledScanoutsAfter = $gpuProbeCompleteEnabledScanoutsAfter
        GpuProbeCompleteDistinctPatterns = $gpuProbeCompleteDistinctPatterns
        GpuProbeCompleteQemuTwoUsableScanouts = $gpuProbeCompleteQemuTwoUsableScanouts
        GpuOutputInventoryLine = $gpuOutputInventoryLine.Value
        GpuOutputConfiguredCount = $gpuOutputConfiguredCount
        GpuOutputOperationalCount = $gpuOutputOperationalCount
        GpuOutputConnectorEnabledCount = $gpuOutputConnectorEnabledCount
        GpuOutputPresentationConfirmedCount = $gpuOutputPresentationConfirmedCount
        GpuOutputVirtualDesktopWidth = $gpuOutputVirtualDesktopWidth
        GpuOutputVirtualDesktopHeight = $gpuOutputVirtualDesktopHeight
        GpuOutputTargetCount = $gpuOutputTargetCount
        GpuOutputBackedTargetCount = $gpuOutputBackedTargetCount
        GpuOutputPrimaryOutput = $gpuOutputPrimaryOutput
        GpuOutputProtocolConnectorEnabledCount = $gpuOutputProtocolConnectorEnabledCount
        GpuOutputOperationalOutputCount = $gpuOutputOperationalOutputCount
        GpuOutputPresentationConfirmedCountDetailed = $gpuOutputPresentationConfirmedCountDetailed
        GpuOutput0Line = $gpuOutput0Line.Value
        GpuOutput1Line = $gpuOutput1Line.Value
        GpuMonitor0Line = $gpuMonitor0Line.Value
        GpuMonitor1Line = $gpuMonitor1Line.Value
        GpuTarget0Line = $gpuTarget0Line.Value
        GpuTarget1Line = $gpuTarget1Line.Value
        GpuCompositorTarget0PlanLine = $gpuCompositorTarget0PlanSummary
        GpuCompositorTarget0ResultLine = $gpuCompositorTarget0ResultSummary
        GpuCompositorTarget1PlanLine = $gpuCompositorTarget1PlanSummary
        GpuCompositorTarget1ResultLine = $gpuCompositorTarget1ResultSummary
        GpuCompositorProofLine = $gpuCompositorProofSummary
        GpuProbeCompleteContentMode = $gpuProbeCompleteContentMode
        GpuProbeCompleteFrameMode = $gpuProbeCompleteFrameMode
        GpuProbeCompleteContinuousPresentation = $gpuProbeCompleteContinuousPresentation
        CompositorContentConfirmed0 = $compositorContentConfirmed0
        CompositorContentConfirmed1 = $compositorContentConfirmed1
        TaskbarPrimaryOnlyConfirmed = $taskbarPrimaryOnlyConfirmed
        ViewportSplitConfirmed = $viewportSplitConfirmed
        GpuSingleOutputProofLine = $gpuSingleOutputProofLine.Value
        GpuDualOutputProofLine = $gpuDualOutputProofLine.Value
        Supported = $spec.Supported
        DiagnosticStatus = $backendStatus
        Interpretation = $interpretation
        LauncherStdOutText = $launcherStdOutText
        LauncherStdErrText = $launcherStdErrText
        SerialText = $serialText
    }
}

try {
    if ($Mode -eq 'compositorFrame') {
        Write-Host '[build] rebuilding kernel with virtio-gpu compositor-frame probe enabled'
        Invoke-KernelBuildForSmoke -ExtraCFlags '-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_COMPOSITOR_FRAME_ACTIVE'
        $script:activeSmokeBuild = $true

        $results = @()
        if ($Backends.Count -ne 1 -or $Backends[0] -notlike 'virtio-gpu*') {
            throw 'Compositor frame mode only supports a single virtio-gpu backend.'
        }

        $backend = $Backends[0]
        Write-Host ("[{0} compositorFrame] launching display probe smoke" -f $backend)
        $result = Invoke-QemuDisplayProbeBackend -Backend $backend -TimeoutSeconds $TimeoutSeconds -ProbeStage 'compositorFrame' -EnableVisualCapture
        $results += $result
        Write-Host ("[{0} compositorFrame] completed. serial={1}" -f $backend, $result.SerialLog)
        $stageBResult = $result
        $stageBResultComplete = $result.DiagnosticStatus -eq 'complete'
        $stageBBlockerReason = if ($stageBResultComplete) {
            ''
        } elseif (-not [string]::IsNullOrWhiteSpace($result.Interpretation)) {
            $result.Interpretation
        } else {
            'Compositor-frame probe failed without a blocker reason'
        }
    } else {
        Write-Host '[build] rebuilding kernel with virtio-gpu diagnostic probe enabled'
        Invoke-KernelBuildForSmoke -ExtraCFlags '-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE'
        $script:activeSmokeBuild = $true

        $results = @()
        foreach ($backend in $Backends) {
            Write-Host ("[{0} stageA] launching display probe smoke" -f $backend)
            $result = Invoke-QemuDisplayProbeBackend -Backend $backend -TimeoutSeconds $TimeoutSeconds -ProbeStage 'stageA' -EnableVisualCapture:($backend -like 'virtio-gpu*')
            $results += $result
            Write-Host ("[{0} stageA] completed. serial={1}" -f $backend, $result.SerialLog)
        }

        $virtioGpuStageAResult = $results | Where-Object { $_.Backend -eq 'virtio-gpu' -and $_.ProbeStage -eq 'stageA' } | Select-Object -First 1
        $stageBResult = $null
        $stageBBlockerReason = ''

        if ($virtioGpuStageAResult -and $virtioGpuStageAResult.VisualScanout0 -eq 'confirmed' -and $virtioGpuStageAResult.VisualCaptureStatus -eq 'captured') {
            Write-Host '[build] rebuilding kernel with virtio-gpu dual-scanout probe enabled'
            Invoke-KernelBuildForSmoke -ExtraCFlags '-DGXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE -DGXOS_QEMU_VIRTIO_GPU_SCANOUT1_ACTIVE'
            $script:activeSmokeBuild = $true
            try {
                $stageBResult = Invoke-QemuDisplayProbeBackend -Backend 'virtio-gpu' -TimeoutSeconds $TimeoutSeconds -ProbeStage 'stageB' -EnableVisualCapture
                $results += $stageBResult
                Write-Host ("[virtio-gpu stageB] completed. serial={0}" -f $stageBResult.SerialLog)
            } catch {
                $stageBBlockerReason = $_.Exception.Message
                Write-Host ("[virtio-gpu stageB] blocker: {0}" -f $stageBBlockerReason)
            }
        } else {
            $stageBBlockerReason = if ($virtioGpuStageAResult) {
                "Stage A visual proof not confirmed: visualScanout0=$($virtioGpuStageAResult.VisualScanout0) capture=$($virtioGpuStageAResult.VisualCaptureStatus)"
            } else {
                'Stage A virtio-gpu result missing'
            }
            Write-Host ("[virtio-gpu stageB] skipped: {0}" -f $stageBBlockerReason)
        }

        $stageBResultComplete = $stageBResult -and $stageBResult.DiagnosticStatus -eq 'complete'
        if ($stageBResult -and -not $stageBResultComplete -and [string]::IsNullOrWhiteSpace($stageBBlockerReason)) {
            $stageBBlockerReason = if (-not [string]::IsNullOrWhiteSpace($stageBResult.Interpretation)) {
                $stageBResult.Interpretation
            } elseif (-not [string]::IsNullOrWhiteSpace($stageBResult.DiagnosticStatus)) {
                "Stage B diagnostic status=$($stageBResult.DiagnosticStatus)"
            } else {
                'Stage B failed without a blocker reason'
            }
        }
    }

    $evidencePath = Join-Path $RunRoot 'qemu-display-probe.evidence.txt'
    $evidenceLines = @(
        '[QemuDisplayProbeSmoke]',
        'evidenceVersion=1',
        "timestampUtc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "repoRoot=$Root",
        "runRoot=$RunRoot",
        "backends=$($Backends -join ',')",
        "probeMode=$Mode",
        "stageBBlockerReason=$stageBBlockerReason"
    )

    foreach ($result in $results) {
        $evidenceLines += ''
        $evidenceLines += "[backend $($result.Backend) stage=$($result.ProbeStage)]"
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
        $evidenceLines += "gpuMmioRequestBase=$($result.GpuMmioRequestBase)"
        $evidenceLines += "gpuMmioRequestLength=$($result.GpuMmioRequestLength)"
        $evidenceLines += "gpuMmioKernelVirtualBase=$($result.GpuMmioKernelVirtualBase)"
        $evidenceLines += "gpuMmioMappedVirtual=$($result.GpuMmioMappedVirtual)"
        $evidenceLines += "gpuMmioMappedLength=$($result.GpuMmioMappedLength)"
        $evidenceLines += "gpuMmioPages=$($result.GpuMmioPages)"
        $evidenceLines += "gpuMmioFlags=$($result.GpuMmioFlags)"
        $evidenceLines += "gpuMmioNonUser=$($result.GpuMmioNonUser)"
        $evidenceLines += "gpuMmioNoExec=$($result.GpuMmioNoExec)"
        $evidenceLines += "gpuMmioUncached=$($result.GpuMmioUncached)"
        $evidenceLines += "gpuMmioCacheAttrs=$($result.GpuMmioCacheAttrs)"
        $evidenceLines += "gpuMmioQemuProbeOnly=$($result.GpuMmioQemuProbeOnly)"
        $evidenceLines += "gpuMmioSummaryLine=$($result.GpuMmioSummaryLine)"
        $evidenceLines += "gpuMmioMappedLine=$($result.GpuMmioMappedLine)"
        $evidenceLines += "gpuMmioBlockedLine=$($result.GpuMmioBlockedLine)"
        $evidenceLines += "gpuResetStepLine=$($result.GpuResetStepLine)"
        $evidenceLines += "gpuStatusResetLine=$($result.GpuStatusResetLine)"
        $evidenceLines += "gpuAckLine=$($result.GpuAckLine)"
        $evidenceLines += "gpuDriverLine=$($result.GpuDriverLine)"
        $evidenceLines += "gpuFeaturesOkStatusLine=$($result.GpuFeaturesOkStatusLine)"
        $evidenceLines += "gpuDriverOkStatusLine=$($result.GpuDriverOkStatusLine)"
        $evidenceLines += "gpuFeatureBitmapLine=$($result.GpuFeatureBitmapLine)"
        $evidenceLines += "gpuPreRenderDeviceConfigLine=$($result.GpuPreRenderDeviceConfigLine)"
        $evidenceLines += "gpuPreRenderDisplayInfoBeginLine=$($result.GpuPreRenderDisplayInfoBeginLine)"
        $evidenceLines += "gpuPreRenderCompletionLine=$($result.GpuPreRenderCompletionLine)"
        $evidenceLines += "gpuPreRenderDisplayInfoSummaryLine=$($result.GpuPreRenderDisplayInfoSummaryLine)"
        $evidenceLines += "gpuDiagnosticTargetLine=$($result.GpuDiagnosticTargetLine)"
        $evidenceLines += "gpuBackingLayoutLine=$($result.GpuBackingLayoutLine)"
        $evidenceLines += "gpuBackingPhysicalCoverageValid=$($result.GpuBackingPhysicalCoverageValid)"
        $evidenceLines += "gpuBackingMemEntryCount=$($result.GpuBackingMemEntryCount)"
        $evidenceLines += "gpuBackingContiguousRunCount=$($result.GpuBackingContiguousRunCount)"
        $evidenceLines += "gpuBackingCoveredBytes=$($result.GpuBackingCoveredBytes)"
        $evidenceLines += "gpuResourceCreateLine=$($result.GpuResourceCreateLine)"
        $evidenceLines += "gpuResourceCreateSecondaryLine=$($result.GpuResourceCreateSecondaryLine)"
        $evidenceLines += "gpuAttachLine=$($result.GpuAttachLine)"
        $evidenceLines += "gpuAttachSecondaryLine=$($result.GpuAttachSecondaryLine)"
        $evidenceLines += "gpuSetScanoutLine=$($result.GpuSetScanoutLine)"
        $evidenceLines += "gpuSetScanout1Line=$($result.GpuSetScanout1Line)"
        $evidenceLines += "gpuTransferLine=$($result.GpuTransferLine)"
        $evidenceLines += "gpuTransfer1Line=$($result.GpuTransfer1Line)"
        $evidenceLines += "gpuFlushLine=$($result.GpuFlushLine)"
        $evidenceLines += "gpuFlush1Line=$($result.GpuFlush1Line)"
        $evidenceLines += "gpuPostRenderDisplayInfoBeginLine=$($result.GpuPostRenderDisplayInfoBeginLine)"
        $evidenceLines += "gpuPostRenderCompletionLine=$($result.GpuPostRenderCompletionLine)"
        $evidenceLines += "gpuPostRenderDisplayInfoSummaryLine=$($result.GpuPostRenderDisplayInfoSummaryLine)"
        $evidenceLines += "gpuPreRenderEnabledScanouts=$($result.GpuPreRenderEnabledScanouts)"
        $evidenceLines += "gpuPreRenderDisabledScanouts=$($result.GpuPreRenderDisabledScanouts)"
        $evidenceLines += "gpuPreRenderDeviceConfigNumScanouts=$($result.GpuPreRenderDeviceConfigNumScanouts)"
        $evidenceLines += "gpuPreRenderQemuMaxOutputsIntent=$($result.GpuPreRenderQemuMaxOutputsIntent)"
        $evidenceLines += "gpuPostRenderEnabledScanouts=$($result.GpuPostRenderEnabledScanouts)"
        $evidenceLines += "gpuPostRenderDisabledScanouts=$($result.GpuPostRenderDisabledScanouts)"
        $evidenceLines += "gpuPostRenderDeviceConfigNumScanouts=$($result.GpuPostRenderDeviceConfigNumScanouts)"
        $evidenceLines += "gpuPostRenderQemuMaxOutputsIntent=$($result.GpuPostRenderQemuMaxOutputsIntent)"
        $evidenceLines += "gpuPostRenderScanout0Line=$($result.GpuPostRenderScanout0Line)"
        $evidenceLines += "gpuPostRenderScanout1Line=$($result.GpuPostRenderScanout1Line)"
        $evidenceLines += "gpuFeatureNegotiationLine=$($result.GpuFeatureNegotiationLine)"
        $evidenceLines += "gpuQueueCountLine=$($result.GpuQueueCountLine)"
        $evidenceLines += "gpuQueueLine=$($result.GpuQueueLine)"
        $evidenceLines += "gpuDisplayInfoResponseLine=$($result.GpuDisplayInfoResponseLine)"
        $evidenceLines += "gpuScanoutLine=$($result.GpuScanoutLine)"
        $evidenceLines += "gpuEnabledScanoutCount=$($result.GpuEnabledScanoutCount)"
        $evidenceLines += "gpuProbeCompleteLine=$($result.GpuProbeCompleteLine)"
        $evidenceLines += "gpuProbeCompleteContentMode=$($result.GpuProbeCompleteContentMode)"
        $evidenceLines += "gpuProbeCompleteFrameMode=$($result.GpuProbeCompleteFrameMode)"
        $evidenceLines += "gpuProbeCompleteContinuousPresentation=$($result.GpuProbeCompleteContinuousPresentation)"
        $evidenceLines += "gpuOutputInventoryLine=$($result.GpuOutputInventoryLine)"
        $evidenceLines += "gpuOutputConfiguredCount=$($result.GpuOutputConfiguredCount)"
        $evidenceLines += "gpuOutputOperationalCount=$($result.GpuOutputOperationalCount)"
        $evidenceLines += "gpuOutputConnectorEnabledCount=$($result.GpuOutputConnectorEnabledCount)"
        $evidenceLines += "gpuOutputPresentationConfirmedCount=$($result.GpuOutputPresentationConfirmedCount)"
        $evidenceLines += "gpuOutputVirtualDesktopWidth=$($result.GpuOutputVirtualDesktopWidth)"
        $evidenceLines += "gpuOutputVirtualDesktopHeight=$($result.GpuOutputVirtualDesktopHeight)"
        $evidenceLines += "gpuOutputTargetCount=$($result.GpuOutputTargetCount)"
        $evidenceLines += "gpuOutputBackedTargetCount=$($result.GpuOutputBackedTargetCount)"
        $evidenceLines += "gpuOutputPrimaryOutput=$($result.GpuOutputPrimaryOutput)"
        $evidenceLines += "gpuOutputProtocolConnectorEnabledCount=$($result.GpuOutputProtocolConnectorEnabledCount)"
        $evidenceLines += "gpuOutputOperationalOutputCount=$($result.GpuOutputOperationalOutputCount)"
        $evidenceLines += "gpuOutputPresentationConfirmedCountDetailed=$($result.GpuOutputPresentationConfirmedCountDetailed)"
        $evidenceLines += "gpuOutput0Line=$($result.GpuOutput0Line)"
        $evidenceLines += "gpuOutput1Line=$($result.GpuOutput1Line)"
        $evidenceLines += "gpuMonitor0Line=$($result.GpuMonitor0Line)"
        $evidenceLines += "gpuMonitor1Line=$($result.GpuMonitor1Line)"
        $evidenceLines += "gpuTarget0Line=$($result.GpuTarget0Line)"
        $evidenceLines += "gpuTarget1Line=$($result.GpuTarget1Line)"
        $evidenceLines += "gpuCompositorTarget0PlanLine=$($result.GpuCompositorTarget0PlanLine)"
        $evidenceLines += "gpuCompositorTarget0ResultLine=$($result.GpuCompositorTarget0ResultLine)"
        $evidenceLines += "gpuCompositorTarget1PlanLine=$($result.GpuCompositorTarget1PlanLine)"
        $evidenceLines += "gpuCompositorTarget1ResultLine=$($result.GpuCompositorTarget1ResultLine)"
        $evidenceLines += "gpuCompositorProofLine=$($result.GpuCompositorProofLine)"
        $evidenceLines += "compositorContentConfirmed0=$($result.CompositorContentConfirmed0)"
        $evidenceLines += "compositorContentConfirmed1=$($result.CompositorContentConfirmed1)"
        $evidenceLines += "taskbarPrimaryOnlyConfirmed=$($result.TaskbarPrimaryOnlyConfirmed)"
        $evidenceLines += "viewportSplitConfirmed=$($result.ViewportSplitConfirmed)"
        $evidenceLines += "gpuProbeCompleteDeviceConfigNumScanouts=$($result.GpuProbeCompleteDeviceConfigNumScanouts)"
        $evidenceLines += "gpuProbeCompleteEnabledScanoutsBefore=$($result.GpuProbeCompleteEnabledScanoutsBefore)"
        $evidenceLines += "gpuProbeCompleteDisabledScanoutsBefore=$($result.GpuProbeCompleteDisabledScanoutsBefore)"
        $evidenceLines += "gpuProbeCompleteEnabledScanoutsAfter=$($result.GpuProbeCompleteEnabledScanoutsAfter)"
        $evidenceLines += "gpuProbeCompleteDistinctPatterns=$($result.GpuProbeCompleteDistinctPatterns)"
        $evidenceLines += "gpuProbeCompleteQemuTwoUsableScanouts=$($result.GpuProbeCompleteQemuTwoUsableScanouts)"
        $evidenceLines += "gpuPrimaryPatternChecksum=$($result.GpuPrimaryPatternChecksum)"
        $evidenceLines += "gpuSecondaryPatternChecksum=$($result.GpuSecondaryPatternChecksum)"
        $evidenceLines += "gpuSingleOutputProofLine=$($result.GpuSingleOutputProofLine)"
        $evidenceLines += "gpuDualOutputProofLine=$($result.GpuDualOutputProofLine)"
        $evidenceLines += "visualCaptureStatus=$($result.VisualCaptureStatus)"
        $evidenceLines += "visualScanout0=$($result.VisualScanout0)"
        $evidenceLines += "visualScanout1=$($result.VisualScanout1)"
        $evidenceLines += "visualScanout0Path=$($result.VisualScanout0Path)"
        $evidenceLines += "visualScanout1Path=$($result.VisualScanout1Path)"
        $evidenceLines += "visualScanout0Signature=$($result.VisualScanout0Signature)"
        $evidenceLines += "visualScanout1Signature=$($result.VisualScanout1Signature)"
        $evidenceLines += "visualCaptureReason=$($result.VisualCaptureReason)"
        $evidenceLines += "distinctPatternsConfirmed=$($result.DistinctPatternsConfirmed)"
        $evidenceLines += "dualOutputVisualProof=$($result.DualOutputVisualProof)"
        $evidenceLines += "launcherStdOut=$($result.LauncherStdOut)"
        $evidenceLines += "launcherStdErr=$($result.LauncherStdErr)"
        $evidenceLines += "serialLog=$($result.SerialLog)"
        $evidenceLines += "summaryPath=$($result.SummaryPath)"
    }

    if (-not [string]::IsNullOrWhiteSpace($stageBBlockerReason) -and $stageBResult -eq $null) {
        $evidenceLines += ''
        $evidenceLines += '[virtio-gpu stageB]'
        $evidenceLines += 'required=false'
        $evidenceLines += 'supported=true'
        $evidenceLines += 'diagnosticStatus=blocked'
        $evidenceLines += "interpretation=$stageBBlockerReason"
    }

    Set-Content -LiteralPath $evidencePath -Value $evidenceLines -Encoding UTF8

    if ($stageBResultComplete) {
        Write-Host 'QEMU display probe smoke passed.'
    } else {
        Write-Host 'QEMU display probe smoke completed with a Stage B blocker.'
    }
    Write-Host ("Evidence: {0}" -f $evidencePath)
    foreach ($result in $results) {
        Write-Host ("[{0} stage={1}] status={2} launched={3} serial={4}" -f $result.Backend, $result.ProbeStage, $result.DiagnosticStatus, $result.Launched, $result.BootloaderSerialAppeared)
        Write-Host ("[{0} stage={1}] summary={2}" -f $result.Backend, $result.ProbeStage, $result.SummaryPath)
    }
    if (-not $stageBResultComplete) {
        if ([string]::IsNullOrWhiteSpace($stageBBlockerReason)) {
            $stageBBlockerReason = 'Stage B failed without a blocker reason'
        }
        throw $stageBBlockerReason
    }
} finally {
    Restore-NormalKernelBuild
}
