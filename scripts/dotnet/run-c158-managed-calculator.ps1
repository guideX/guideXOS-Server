param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$PythonExe = "",
    [int]$TimeoutSeconds = 900,
    [switch]$ReuseBuiltProofKernel,
    [switch]$PhaseC160,
    [switch]$PhaseC161,
    [switch]$PhaseC162,
    [switch]$PhaseC163,
    [switch]$PhaseC164
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($PhaseC164) { $PhaseC163 = $true }
if ($PhaseC163) { $PhaseC162 = $true }
if ($PhaseC162) { $PhaseC161 = $true }
if ($PhaseC161) { $PhaseC160 = $true }
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = if ($PhaseC164) {
        Join-Path $RepoRoot "out\dotnet\c164-file-activation"
    } elseif ($PhaseC163) {
        Join-Path $RepoRoot "out\dotnet\c163-managed-file-explorer"
    } elseif ($PhaseC162) {
        Join-Path $RepoRoot "out\dotnet\c162-managed-task-manager-close"
    } elseif ($PhaseC161) {
        Join-Path $RepoRoot "out\dotnet\c161-managed-task-manager"
    } elseif ($PhaseC160) {
        Join-Path $RepoRoot "out\dotnet\c160-application-snapshot"
    } else {
        Join-Path $RepoRoot "out\dotnet\c158-managed-calculator"
    }
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C158 evidence must remain under $allowedRoot"
}
if ($TimeoutSeconds -lt 30) { throw "TimeoutSeconds must be at least 30." }

$kernelPath = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
$espKernelPath = Join-Path $RepoRoot 'ESP\kernel.elf'
$protectedRamdiskPath = Join-Path $RepoRoot 'ESP\ramdisk.img'
$bootloaderPath = Join-Path $RepoRoot 'guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe'
$buildRoot = Join-Path $EvidenceRoot 'build'
$compositeRoot = Join-Path $buildRoot $(if ($PhaseC164) { 'composite-c164-proof' } elseif ($PhaseC163) { 'composite-c163-proof' } elseif ($PhaseC162) { 'composite-c162-proof' } elseif ($PhaseC161) { 'composite-c161-proof' } elseif ($PhaseC160) { 'composite-c160-proof' } else { 'composite' })
$canonicalCompositeRoot = Join-Path $buildRoot $(if ($PhaseC164) { 'composite-c164-production' } elseif ($PhaseC163) { 'composite-c163-production' } elseif ($PhaseC162) { 'composite-c162-production' } elseif ($PhaseC161) { 'composite-c161-production' } else { 'composite-c160-production' })
$canonicalRamdisk = Join-Path $EvidenceRoot $(if ($PhaseC164) { 'staging\ramdisk-c164-production.img' } elseif ($PhaseC163) { 'staging\ramdisk-c163-production.img' } elseif ($PhaseC162) { 'staging\ramdisk-c162-production.img' } elseif ($PhaseC161) { 'staging\ramdisk-c161-production.img' } else { 'staging\ramdisk-c160-production.img' })
$runtimePackOutput = Join-Path $buildRoot 'runtime-pack'
$stageRoot = Join-Path $EvidenceRoot 'staging\wallpaper-pack'
$proofRamdisk = Join-Path $EvidenceRoot $(if ($PhaseC164) { 'staging\ramdisk-c164-proof.img' } elseif ($PhaseC163) { 'staging\ramdisk-c163-proof.img' } elseif ($PhaseC162) { 'staging\ramdisk-c162-proof.img' } elseif ($PhaseC161) { 'staging\ramdisk-c161-proof.img' } elseif ($PhaseC160) { 'staging\ramdisk-c160-proof.img' } else { 'staging\ramdisk-c158.img' })
$proofKernel = Join-Path $EvidenceRoot 'proof-kernel.elf'
$proofBackup = Join-Path $EvidenceRoot 'canonical\kernel.elf'
$espKernelBackup = Join-Path $EvidenceRoot 'canonical\ESP-kernel.elf'
$ramdiskBackup = Join-Path $EvidenceRoot 'canonical\ESP-ramdisk.img'
$canonicalKernelHash = $null
$espKernelHash = $null
$ramdiskHash = $null
$restored = $false
$script:activeProcess = $null
$script:activeStderr = $null
$script:activeSerial = $null
$script:activePort = 0
$script:activeQmpLog = $null
$script:cursor = $null
$ordinaryKernelSource = $proofBackup
$ordinaryRamdiskSource = $ramdiskBackup
$ordinaryKernelHash = $null
$ordinaryRamdiskHash = $null
$postC160KernelHash = $null
$postC160RamdiskHash = $null
$canonicalCompositeElf = $null
$canonicalCompositeHash = $null

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $File $($Arguments -join ' ')"
    }
}

function Get-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-AvailableQmpPort {
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally { $listener.Stop() }
}

function Get-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $command.Source).Path
    }
    throw "Required tool '$Name' was not found."
}

function Assert-EvidencePath([string]$Path, [string]$Label) {
    $root = [System.IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    $full = [System.IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label is outside the C158 evidence folder: $full"
    }
}

function Stage-Esp([string]$Esp, [string]$Kernel, [string]$Ramdisk,
                   [string]$SettingsRecord = "") {
    Assert-EvidencePath $Esp 'QEMU ESP'
    if (Test-Path -LiteralPath $Esp) {
        $resolved = (Resolve-Path -LiteralPath $Esp).Path
        Assert-EvidencePath $resolved 'Resolved QEMU ESP'
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $Esp 'EFI\BOOT') | Out-Null
    Copy-Item -LiteralPath $bootloaderPath -Destination (Join-Path $Esp 'EFI\BOOT\BOOTX64.EFI') -Force
    Copy-Item -LiteralPath $Kernel -Destination (Join-Path $Esp 'kernel.elf') -Force
    Copy-Item -LiteralPath $Ramdisk -Destination (Join-Path $Esp 'ramdisk.img') -Force
    if ($SettingsRecord) {
        Copy-Item -LiteralPath $SettingsRecord -Destination (Join-Path $Esp 'GXSETT.BIN') -Force
    }
}

function Clear-QemuEspCopies {
    foreach ($name in @('production-boot-01', 'production-boot-02', 'production-boot-03',
                        'ordinary-boot-01', 'ordinary-boot-02', 'ordinary-boot-03')) {
        $candidate = Join-Path (Join-Path $EvidenceRoot $name) 'ESP'
        Assert-EvidencePath $candidate 'QEMU ESP'
        if (Test-Path -LiteralPath $candidate) {
            $resolved = (Resolve-Path -LiteralPath $candidate).Path
            Assert-EvidencePath $resolved 'Resolved QEMU ESP'
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}

function Read-Qmp([System.IO.Stream]$Stream, [int]$Seconds = 4) {
    $builder = [System.Text.StringBuilder]::new()
    $buffer = [byte[]]::new(4096)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        if ($Stream.DataAvailable) {
            $count = $Stream.Read($buffer, 0, $buffer.Length)
            if ($count -gt 0) {
                [void]$builder.Append([System.Text.Encoding]::ASCII.GetString($buffer, 0, $count))
                if ($builder.ToString().TrimEnd().EndsWith('}')) { break }
            }
        } else { Start-Sleep -Milliseconds 20 }
    }
    return $builder.ToString()
}

function Send-QmpEvents([object[]]$Events, [int]$DelayMilliseconds = 45) {
    if ($Events.Count -eq 0) { return }
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $script:activePort)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 200
        [void](Read-Qmp $stream)
        $capabilities = [System.Text.Encoding]::ASCII.GetBytes('{"execute":"qmp_capabilities"}' + "`n")
        $stream.Write($capabilities, 0, $capabilities.Length); $stream.Flush()
        $capResponse = Read-Qmp $stream
        if ($capResponse -match '"error"') { throw "QMP capability negotiation failed: $capResponse" }
        foreach ($event in $Events) {
            $request = [ordered]@{
                execute = 'input-send-event'
                arguments = [ordered]@{ events = @($event) }
            } | ConvertTo-Json -Compress -Depth 10
            $bytes = [System.Text.Encoding]::ASCII.GetBytes($request + "`n")
            $stream.Write($bytes, 0, $bytes.Length); $stream.Flush()
            $response = Read-Qmp $stream
            Add-Content -LiteralPath $script:activeQmpLog -Value ("event={0}`nresponse={1}" -f $request, $response) -Encoding ASCII
            if ($response -match '"error"') { throw "QMP input event failed: $response" }
            if ($DelayMilliseconds -gt 0) { Start-Sleep -Milliseconds $DelayMilliseconds }
        }
    } finally { $client.Dispose() }
}

function New-RelativeMove([int]$Dx, [int]$Dy) {
    $events = [System.Collections.Generic.List[object]]::new()
    foreach ($axis in @(@{ Name = 'x'; Delta = $Dx }, @{ Name = 'y'; Delta = $Dy })) {
        $remaining = [int]$axis.Delta
        while ($remaining -ne 0) {
            $step = [Math]::Sign($remaining) * [Math]::Min([Math]::Abs($remaining), 40)
            $events.Add([ordered]@{ type = 'rel'; data = [ordered]@{ axis = $axis.Name; value = $step } })
            $remaining -= $step
        }
    }
    return $events.ToArray()
}

function New-Button([string]$Name, [bool]$Down) {
    return [ordered]@{ type = 'btn'; data = [ordered]@{ button = $Name; down = $Down } }
}

function New-Key([string]$Code, [bool]$Down) {
    return [ordered]@{ type = 'key'; data = [ordered]@{ down = $Down; key = [ordered]@{ type = 'qcode'; data = $Code } } }
}

function New-Wheel([int]$Delta) {
    return [ordered]@{ type = 'btn'; data = [ordered]@{ button = $(if ($Delta -gt 0) { 'wheel-down' } else { 'wheel-up' }); down = $true } }
}

function Get-Serial([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $raw = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
        if ($null -eq $raw) { return [string]::Empty }
        return [string]$raw
    }
    return [string]::Empty
}

function Wait-Serial([string]$Path, [string]$Pattern, [int]$After = 0,
                     [int]$Seconds = 30) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $text = Get-Serial $Path
        if ($text -match '(?m)^(?:\[C102-MANAGED-OUTPUT\] )?C158-CALC-[^\r\n]*result=FAIL') {
            throw 'Managed Calculator reported a C158 failure marker.'
        }
        if ($text -match '(?m)^\[C158-CALC-APP-REGISTRY\][^\r\n]*result=FAIL') {
            throw 'The Native Calculator preservation or managed registration check failed.'
        }
        if ($text -match '(?m)^\[C160-NATIVE-SNAPSHOT-TESTS\][^\r\n]*result=FAIL') {
            throw "C160 native snapshot proof failed: $($matches[0])"
        }
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C160-(?:MANAGED-SNAPSHOT-TESTS|SNAPSHOT)[^\r\n]*result=FAIL') {
            throw "C160 managed snapshot proof failed: $($matches[0])"
        }
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-[^\r\n]*result=FAIL' -or
            $text -match '(?m)^\[C161-TM-[^\r\n]*result=FAIL') {
            throw "C161 Managed Task Manager proof failed: $($matches[0])"
        }
        if ($text -match '(?m)^\[C102-MANAGED-OUTPUT\] C163-FILE-EXPLORER-[^\r\n]*result=FAIL' -or
            $text -match '(?m)^\[C163-[^\r\n]*result=FAIL') {
            throw "C163 Managed File Explorer proof failed: $($matches[0])"
        }
        $tail = if ($After -le $text.Length) { $text.Substring($After) } else { '' }
        $match = [regex]::Match($tail, "(?m)$Pattern")
        if ($match.Success) { return [pscustomobject]@{ Text = $text; Match = $match; Index = $text.Length } }
        if ($script:activeProcess) {
            $script:activeProcess.Refresh()
            if ($script:activeProcess.HasExited) {
                $stderr = if ($script:activeStderr -and (Test-Path -LiteralPath $script:activeStderr)) {
                    [string](Get-Content -LiteralPath $script:activeStderr -Raw -ErrorAction SilentlyContinue)
                } else { '' }
                throw "QEMU exited with code $($script:activeProcess.ExitCode) while waiting for '$Pattern'. stderr=$stderr"
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for serial marker: $Pattern"
}

function Get-ClientPoint([string]$Serial) {
    $matches = [regex]::Matches($Serial, '(?m)^\[C138-NATIVE-INPUT\] kind=pointer-move x=([0-9A-Fa-f]+) y=([0-9A-Fa-f]+) result=PASS')
    if ($matches.Count -eq 0) { throw 'C158 could not calibrate a real QMP pointer position.' }
    $match = $matches[$matches.Count - 1]
    return [pscustomobject]@{
        X = [Convert]::ToInt32($match.Groups[1].Value, 16)
        Y = [Convert]::ToInt32($match.Groups[2].Value, 16)
    }
}

function Move-Pointer([int]$X, [int]$Y) {
    if (-not $script:cursor) { throw 'C158 QMP pointer has not been calibrated.' }
    if ($X -eq $script:cursor.X -and $Y -eq $script:cursor.Y) { return }
    Send-QmpEvents (New-RelativeMove ($X - $script:cursor.X) ($Y - $script:cursor.Y)) 30
    $script:cursor = [pscustomobject]@{ X = $X; Y = $Y }
}

function Click-Screen([int]$X, [int]$Y) {
    Move-Pointer $X $Y
    # The bare-metal input loop can poll slower than adjacent QMP events;
    # leave the button state stable long enough for each transition to arrive.
    Send-QmpEvents @((New-Button 'left' $true), (New-Button 'left' $false)) 180
}

function Press-Key([string]$Code) {
    Send-QmpEvents @((New-Key $Code $true), (New-Key $Code $false)) 180
}

function Send-CalculatorLaunch([bool]$Initial, [int]$ScreenWidth,
                               [int]$ScreenHeight, [string]$SerialPath) {
    $menuCount = if ($PhaseC163) { 22 } elseif ($PhaseC161) { 21 } else { 20 }
    $menuHeight = 30 + $menuCount * 22 + 36
    $workHeight = $ScreenHeight - 40
    $menuY = [Math]::Max(0, $workHeight - $menuHeight)
    $contentY = $menuY + 31
    Click-Screen 50 ($ScreenHeight - 20)
    if ($Initial) {
        $footerY = $menuY + 30 + $menuCount * 22
        Click-Screen 70 ($footerY + 18)
        # All Programs item 11 is Managed Calculator in the sorted, fixed list.
        $calculatorY = $contentY + 11 * 22 + 11
    } else {
        # Managed Calculator is promoted into Recent Programs after a successful
        # App Model launch, so each relaunch remains an ordinary Start-menu click.
        $calculatorY = $contentY + 11
    }
    $before = (Get-Serial $SerialPath).Length
    Click-Screen 115 $calculatorY
    [void](Wait-Serial $SerialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.calculator selector=00000006 image=/system/apps/GXOSAPP\.ELF metadata=valid' $before 35)
    [void](Wait-Serial $SerialPath '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.calculator generation=[0-9A-Fa-f]+ selector=00000006 kind=normal' $before 35)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator selector=6 controls=18 capacity=18 focus=18 fresh=PASS result=PASS' $before 35)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=0 phase=0 commands=0 result=PASS' $before 15)
}

function Switch-CalculatorToNotes([int]$ScreenWidth, [int]$ScreenHeight,
                                 [string]$SerialPath) {
    $menuCount = if ($PhaseC163) { 22 } elseif ($PhaseC161) { 21 } else { 20 }
    $menuHeight = 30 + $menuCount * 22 + 36
    $workHeight = $ScreenHeight - 40
    $menuY = [Math]::Max(0, $workHeight - $menuHeight)
    $contentY = $menuY + 31
    Click-Screen 50 ($ScreenHeight - 20)
    $footerY = $menuY + 30 + $menuCount * 22
    Click-Screen 70 ($footerY + 18)
    # Managed Notes is fixed All Programs entry 13 in C163, entry 12 earlier.
    # is active to prove ordinary one-surface replacement through the App Model.
    $notesIndex = 12
    $notesY = $contentY + $notesIndex * 22 + 11
    $before = (Get-Serial $SerialPath).Length
    Click-Screen 115 $notesY
    if ($PhaseC162) {
        [void](Wait-Serial $SerialPath '^\[C162-MANAGED-FOCUS\] appId=com\.guidexos\.apps\.managed\.notes source=3 instance=[0-9A-Fa-f]{16} selector=00000004 existing=true result=PASS' $before 35)
        return
    }
    [void](Wait-Serial $SerialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.notes selector=00000004 image=/system/apps/GXOSAPP\.ELF metadata=valid' $before 35)
    [void](Wait-Serial $SerialPath '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes generation=[0-9A-Fa-f]+ selector=00000004 kind=normal' $before 35)
    # C150 suppresses the detailed destroy marker while a surface is being
    # replaced; C111 records the close before the next C150 surface is created.
    [void](Wait-Serial $SerialPath '^\[C111-SURFACE\] action=close result=PASS' $before 20)
    [void](Wait-Serial $SerialPath '^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ result=PASS' $before 20)
    if ($PhaseC160) {
        # Notes performs its ordinary startup checks before the managed
        # dispatch returns. Wait for the snapshot proving its new logical
        # identity replaced Calculator and is the focused application.
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C160-SNAPSHOT appId=com\.guidexos\.apps\.managed\.notes source=3 instance=[0-9]+ count=[0-9]+ active=1 prev=gone distinct=1 result=PASS' $before 180)
    }
}

function Send-ManagedNotesLaunch([int]$ScreenWidth, [int]$ScreenHeight,
                                 [string]$SerialPath) {
    $menuCount = if ($PhaseC163) { 22 } elseif ($PhaseC161) { 21 } else { 20 }
    $menuHeight = 30 + $menuCount * 22 + 36
    $workHeight = $ScreenHeight - 40
    $menuY = [Math]::Max(0, $workHeight - $menuHeight)
    $contentY = $menuY + 31
    $footerY = $menuY + 30 + $menuCount * 22
    Click-Screen 50 ($ScreenHeight - 20)
    Click-Screen 70 ($footerY + 18)
    $before = (Get-Serial $SerialPath).Length
    $notesIndex = 12
    Click-Screen 115 ($contentY + $notesIndex * 22 + 11)
    if ($PhaseC162) {
        [void](Wait-Serial $SerialPath '^\[C162-MANAGED-FOCUS\] appId=com\.guidexos\.apps\.managed\.notes source=3 instance=[0-9A-Fa-f]{16} selector=00000004 existing=true result=PASS' $before 35)
        return
    }
    [void](Wait-Serial $SerialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.notes selector=00000004 image=/system/apps/GXOSAPP\.ELF metadata=valid' $before 35)
    [void](Wait-Serial $SerialPath '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes generation=[0-9A-Fa-f]+ selector=00000004 kind=normal' $before 35)
    if ($PhaseC161) {
        # The baseline marker is emitted by Ctrl+N, not by application startup.
        # The relaunch suite confirms Notes finished its bounded startup checks.
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW-STRESS cycles=25 same-instance=PASS history-reset=PASS clipboard-stable=PASS result=PASS' $before 180)
    } else {
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C160-SNAPSHOT appId=com\.guidexos\.apps\.managed\.notes source=3 instance=[0-9]+ count=[0-9]+ active=1 previous=none result=PASS' $before 30)
    }
}

function Invoke-NotesClipboardSmoke([string]$SerialPath) {
    $before = (Get-Serial $SerialPath).Length
    Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key 'n' $true),
        (New-Key 'n' $false), (New-Key 'ctrl' $false)) 90
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C157-NEW baseline=clean-untitled .*same-instance=true result=PASS' $before 20)
    Send-Text 'c161'
    Press-Key 'home'
    Send-QmpEvents @((New-Key 'shift' $true), (New-Key 'right' $true),
        (New-Key 'right' $false), (New-Key 'shift' $false)) 90
    $copyStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key 'c' $true),
        (New-Key 'c' $false), (New-Key 'ctrl' $false)) 90
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C154-COPY length=1 result=PASS' $copyStart 20)
}

function Launch-ManagedTaskManager([int]$ScreenWidth, [int]$ScreenHeight,
                                   [string]$SerialPath) {
    $menuCount = if ($PhaseC163) { 22 } else { 21 }
    $menuHeight = 30 + $menuCount * 22 + 36
    $workHeight = $ScreenHeight - 40
    $menuY = [Math]::Max(0, $workHeight - $menuHeight)
    $contentY = $menuY + 31
    $footerY = $menuY + 30 + $menuCount * 22
    Click-Screen 50 ($ScreenHeight - 20)
    Click-Screen 70 ($footerY + 18)
    $before = (Get-Serial $SerialPath).Length
    # Task Manager is All Programs index 15 in C163, 14 in C161.
    $taskManagerIndex = 14
    Click-Screen 115 ($contentY + $taskManagerIndex * 22 + 11)
    [void](Wait-Serial $SerialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.taskmanager selector=00000007 image=/system/apps/GXOSAPP\.ELF metadata=valid' $before 35)
    [void](Wait-Serial $SerialPath '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.taskmanager generation=[0-9A-Fa-f]+ selector=00000007 kind=normal' $before 35)
    $launch = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-LAUNCH id=7 reg=\d+ ctr=4 cap=4 max=20 snap=\d+ n=\d+ self=(\d+) active=1 result=PASS' $before 35
    return $launch.Match.Groups[1].Value
}

function Launch-ManagedFileExplorer([int]$ScreenWidth, [int]$ScreenHeight,
    [string]$SerialPath) {
    if (-not $PhaseC163) { throw 'Managed File Explorer launch is C163-only.' }
    $menuCount = 22
    $menuHeight = 30 + $menuCount * 22 + 36
    $workHeight = $ScreenHeight - 40
    $menuY = [Math]::Max(0, $workHeight - $menuHeight)
    $contentY = $menuY + 31
    $footerY = $menuY + 30 + $menuCount * 22
    Click-Screen 50 ($ScreenHeight - 20)
    Click-Screen 70 ($footerY + 18)
    $before = (Get-Serial $SerialPath).Length
    # C163 appears at fixed All Programs index 15 after Task Manager.
    Click-Screen 115 ($contentY + 15 * 22 + 11)
    [void](Wait-Serial $SerialPath '^\[APPMODEL-MANAGED-LAUNCH\] source=StartMenu appId=com\.guidexos\.apps\.managed\.fileexplorer selector=00000008 image=/system/apps/GXOSAPP\.ELF metadata=valid' $before 35)
    $activation = Wait-Serial $SerialPath ('^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.fileexplorer generation=([0-9A-Fa-f]+) selector=00000008 kind=normal|^\[C162-MANAGED-FOCUS\] appId=com\.guidexos\.apps\.managed\.fileexplorer source=3 instance=[0-9A-Fa-f]{16} selector=00000008 existing=true result=PASS|^\[C150-LIFETIME-START\] result=FAIL capacity=3') $before 45
    if ($activation.Match.Value.StartsWith('[C150-LIFETIME-START]', [System.StringComparison]::Ordinal)) {
        throw 'Managed File Explorer launch was rejected because the bounded App Model lifetime table is full.'
    }
    if ($activation.Match.Value.StartsWith('[C162-MANAGED-FOCUS]', [System.StringComparison]::Ordinal)) {
        # An earlier C163 pass may have left the final Explorer lifetime
        # open. App Model focuses that exact existing instance instead of
        # constructing another one; return its recorded launch generation.
        $launches = [regex]::Matches((Get-Serial $SerialPath),
            '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.fileexplorer generation=([0-9A-Fa-f]+) selector=00000008 kind=normal')
        if ($launches.Count -eq 0) {
            throw 'C163 focused an existing File Explorer without a recorded lifetime.'
        }
        return $launches[$launches.Count - 1].Groups[1].Value
    }
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-EXPLORER id=com\.guidexos\.apps\.managed\.fileexplorer sel=8 path=/system/apps n=\d+ cap=64 ctl=5 abi=3/120 ro=1 result=PASS' $before 300)
    return $activation.Match.Groups[1].Value
}

function Get-ManagedFileExplorerWindowOrigin([int]$ScreenWidth,
    [int]$ScreenHeight) {
    return [pscustomobject]@{
        X = [Math]::Max(0, [int](($ScreenWidth - 800) / 2))
        Y = [Math]::Max(0, [int](($ScreenHeight - 390) / 2))
    }
}

function Click-ManagedFileExplorerRow([int]$Row, [int]$ScreenWidth,
    [int]$ScreenHeight) {
    $origin = Get-ManagedFileExplorerWindowOrigin $ScreenWidth $ScreenHeight
    Click-Screen ($origin.X + 40) ($origin.Y + 24 + 70 + $Row * 18 + 9)
}

function Click-ManagedFileExplorerButton([string]$Name,
    [int]$ScreenWidth, [int]$ScreenHeight) {
    $origin = Get-ManagedFileExplorerWindowOrigin $ScreenWidth $ScreenHeight
    $x = switch ($Name) {
        'Up' { 66 }
        'Open' { 170 }
        'Refresh' { 284 }
        'Close' { 398 }
        default { throw "Unknown Managed File Explorer button '$Name'." }
    }
    Click-Screen ($origin.X + $x) ($origin.Y + 24 + 340)
}

function Close-ManagedFileExplorer([int]$ScreenWidth, [int]$ScreenHeight,
    [string]$SerialPath) {
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $before = (Get-Serial $SerialPath).Length
        Click-ManagedFileExplorerButton 'Close' $ScreenWidth $ScreenHeight
        try {
            [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-FILE-EXPLORER-CLOSE selector=8 controls=0 fresh-path=/system/apps result=PASS' $before 5)
            return
        } catch {
            if ($_.Exception.Message -notlike 'Timed out waiting for serial marker:*' -or
                $attempt -eq 3) { throw }
        }
    }
    throw 'Managed File Explorer did not close after three pointer activations.'
}

function Select-ManagedFileExplorerEntryByName([string]$ExpectedPath,
    [string]$ExpectedName, [string]$ExpectedType,
    [int]$ScreenWidth, [int]$ScreenHeight, [string]$SerialPath) {
    $escapedPath = [regex]::Escape($ExpectedPath)
    $escapedName = [regex]::Escape($ExpectedName)
    for ($row = 0; $row -lt 20; $row++) {
        $before = (Get-Serial $SerialPath).Length
        Click-ManagedFileExplorerRow $row $ScreenWidth $ScreenHeight
        Start-Sleep -Milliseconds 250
        $tail = (Get-Serial $SerialPath).Substring($before)
        $pattern = '(?m)^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=' +
            $escapedPath + ' n=\d+ sel=' + $escapedName + ' type=' +
            [regex]::Escape($ExpectedType) + ' size=[^ ]+ view=\d+ status='
        if ($tail -match $pattern) { return }
    }
    throw "Managed File Explorer could not select '$ExpectedName' in '$ExpectedPath'."
}

function Close-ManagedNotes([string]$SerialPath) {
    $before = (Get-Serial $SerialPath).Length
    Send-QmpEvents @((New-Key 'alt' $true), (New-Key 'f4' $true),
        (New-Key 'f4' $false), (New-Key 'alt' $false)) 110
    [void](Wait-Serial $SerialPath '^\[C150-SURFACE\] action=destroy appId=com\.guidexos\.apps\.managed\.notes generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ reason=close-or-replace' $before 20)
}

function Activate-C164Fixture([string]$FileName, [bool]$UseEnter,
    [int]$ScreenWidth, [int]$ScreenHeight, [string]$SerialPath,
    [string]$ExistingFileExplorerId = '') {
    $fileExplorerId = if ($ExistingFileExplorerId) {
        $ExistingFileExplorerId
    } else {
        Launch-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
    }
    Select-ManagedFileExplorerEntryByName '/system/apps' 'C164' 'Directory' `
        $ScreenWidth $ScreenHeight $SerialPath
    Click-ManagedFileExplorerButton 'Open' $ScreenWidth $ScreenHeight
    Start-Sleep -Milliseconds 300
    Select-ManagedFileExplorerEntryByName '/system/apps/C164' $FileName 'File' `
        $ScreenWidth $ScreenHeight $SerialPath
    $state = [regex]::Matches((Get-Serial $SerialPath),
        '(?m)^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps/C164 n=\d+ sel=' +
        [regex]::Escape($FileName) + ' type=File size=(\d+) view=\d+ status=File selected')
    if ($state.Count -eq 0) { throw "No exact selected-file detail for '$FileName'." }
    $openStart = (Get-Serial $SerialPath).Length
    if ($UseEnter) {
        Press-Key 'ret'
    } else {
        Click-ManagedFileExplorerButton 'Open' $ScreenWidth $ScreenHeight
    }
    $path = "/system/apps/C164/$FileName"
    [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C164-NOTES-ACT path=' +
        [regex]::Escape($path) + ' bytes=\d+ clean=1 undo=0 redo=0 caret=0 anchor=0 view=0 result=PASS') $openStart 90)
    [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C164-NOTES-CONTENT exact=true source=VFS result=PASS') $openStart 20)
    $target = Wait-Serial $SerialPath ('^\[C164-ACTIVATION\] source=ManagedFileExplorer instance=[0-9A-Fa-f]+ target=com\.guidexos\.apps\.managed\.notes path=' +
        [regex]::Escape($path) + ' fresh=true source-closed=true result=PASS') $openStart 180
    $launch = Wait-Serial $SerialPath '^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes generation=([0-9A-Fa-f]+) selector=00000004 kind=document' $openStart 15
    return [pscustomobject]@{
        FileExplorerId = $fileExplorerId
        NotesGeneration = $launch.Match.Groups[1].Value
        Path = $path
        SelectedSize = [int]$state[$state.Count - 1].Groups[1].Value
        Activation = $target.Match.Value
    }
}

function Click-ManagedNotesDocument([int]$ScreenWidth, [int]$ScreenHeight) {
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 600) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 360) / 2))
    Click-Screen ($windowX + 70) ($windowY + 24 + 92)
}

function Invoke-C164ManagedFileActivationScenario([int]$Number,
    [int]$ScreenWidth, [int]$ScreenHeight, [string]$SerialPath) {
    if ($Number -eq 1) {
        $activation = Activate-C164Fixture 'hello.txt' $true `
            $ScreenWidth $ScreenHeight $SerialPath
        if ($activation.SelectedSize -ne 15) {
            throw "C164 hello fixture has unexpected VFS size $($activation.SelectedSize)."
        }
        Click-ManagedNotesDocument $ScreenWidth $ScreenHeight
        Press-Key 'end'
        $editStart = (Get-Serial $SerialPath).Length
        Send-Text 'x'
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C164-DOC event=input path=/system/apps/C164/hello\.txt len=16 dirty=1 undo=1 redo=0 caret=16 anchor=16 view=0 result=PASS' $editStart 15)
        $undoStart = (Get-Serial $SerialPath).Length
        Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key 'z' $true),
            (New-Key 'z' $false), (New-Key 'ctrl' $false)) 90
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C164-DOC event=shortcut path=/system/apps/C164/hello\.txt len=15 dirty=0 undo=0 redo=1 caret=15 anchor=15 view=0 result=PASS' $undoStart 15)
        Close-ManagedNotes $SerialPath
        return [pscustomobject]@{ Activations = 1; DistinctNotes = 1; FinalPath = $activation.Path; Save = $false; Unsupported = $false }
    }
    if ($Number -eq 2) {
        $activation = Activate-C164Fixture 'save.txt' $false `
            $ScreenWidth $ScreenHeight $SerialPath
        Click-ManagedNotesDocument $ScreenWidth $ScreenHeight
        Press-Key 'end'
        Send-Text 'x'
        $saveStart = (Get-Serial $SerialPath).Length
        Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key 's' $true),
            (New-Key 's' $false), (New-Key 'ctrl' $false)) 90
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $saveStart 20)
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C164-DOC event=shortcut path=/system/apps/C164/save\.txt len=\d+ dirty=0 undo=1 redo=0 caret=\d+ anchor=\d+ view=0 result=PASS' $saveStart 15)
        Close-ManagedNotes $SerialPath

        $reload = Activate-C164Fixture 'save.txt' $false `
            $ScreenWidth $ScreenHeight $SerialPath
        if ($reload.NotesGeneration -eq $activation.NotesGeneration) {
            throw 'C164 boot 2 reopened the file into the old Notes lifetime.'
        }
        Close-ManagedNotes $SerialPath

        $fileExplorerId = Launch-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
        Select-ManagedFileExplorerEntryByName '/system/apps' 'C164' 'Directory' `
            $ScreenWidth $ScreenHeight $SerialPath
        Click-ManagedFileExplorerButton 'Open' $ScreenWidth $ScreenHeight
        Start-Sleep -Milliseconds 300
        Select-ManagedFileExplorerEntryByName '/system/apps/C164' 'noapp.bin' 'File' `
            $ScreenWidth $ScreenHeight $SerialPath
        $unsupportedStart = (Get-Serial $SerialPath).Length
        Press-Key 'ret'
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C164-FILE-ACTIVATION path=/system/apps/C164/noapp\.bin result=unsupported source-retained=true selection=preserved' $unsupportedStart 15)
        return [pscustomobject]@{ Activations = 2; DistinctNotes = 2; FinalPath = $reload.Path; Save = $true; Unsupported = $true; FileExplorerId = $fileExplorerId }
    }
    if ($Number -eq 3) {
        # Create a real C155 return candidate from the current clean named
        # Notes document, then replace Settings Center with File Explorer.
        # This leaves the one-shot session pending when C164 explicitly opens
        # mixed.TXT, exercising the requested-path precedence at the App Model
        # boundary rather than relying on another proof's incidental state.
        $notesX = [Math]::Max(0, [int](($ScreenWidth - 600) / 2))
        $notesY = [Math]::Max(0, [int](($ScreenHeight - 360) / 2))
        $sessionStart = (Get-Serial $SerialPath).Length
        Click-Screen ($notesX + 475) ($notesY + 24 + 294)
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C155-SESSION-ARMED generation=\d+ path=named caret=\d+ anchor=\d+ viewport=\d+ fixed-bytes=118 result=PASS' $sessionStart 20)
        [void](Wait-Serial $SerialPath '^\[C155-RETURN-PAIR\] target=Notes session=armed result=PASS' $sessionStart 15)
        [void](Wait-Serial $SerialPath '^\[C150-SETTINGS-LAUNCH\] source=Notes path=AppModel result=PASS' $sessionStart 30)
        $staleSessionExplorerId = Launch-ManagedFileExplorer `
            $ScreenWidth $ScreenHeight $SerialPath

        $seenNotes = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::OrdinalIgnoreCase)
        $seenExplorers = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::OrdinalIgnoreCase)
        $lastActivation = $null
        for ($cycle = 1; $cycle -le 25; $cycle++) {
            if ($cycle -eq 1) {
                $lastActivation = Activate-C164Fixture 'mixed.TXT' $true `
                    $ScreenWidth $ScreenHeight $SerialPath `
                    $staleSessionExplorerId
            } else {
                $lastActivation = Activate-C164Fixture 'mixed.TXT' $true `
                    $ScreenWidth $ScreenHeight $SerialPath
            }
            if (-not $seenNotes.Add($lastActivation.NotesGeneration) -or
                -not $seenExplorers.Add($lastActivation.FileExplorerId)) {
                throw "C164 activation cycle $cycle reused a Notes or File Explorer lifetime."
            }
            Close-ManagedNotes $SerialPath
        }
        Launch-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath | Out-Null
        return [pscustomobject]@{
            Activations = $seenNotes.Count
            DistinctNotes = $seenNotes.Count
            DistinctExplorers = $seenExplorers.Count
            FinalPath = $lastActivation.Path
            Save = $false
            Unsupported = $false
            Precedence = (Get-Serial $SerialPath) -match '(?m)^\[C102-MANAGED-OUTPUT\] C164-PRECEDENCE explicit=document stale-session=cleared requested-path=authoritative result=PASS'
            StaleSessionSeeded = (Get-Serial $SerialPath).Substring($sessionStart) -match '(?m)^\[C102-MANAGED-OUTPUT\] C164-PRECEDENCE explicit=document stale-session=cleared requested-path=authoritative result=PASS'
        }
    }
    throw "Unknown C164 production boot number $Number."
}

function Open-ManagedFileExplorerDirectoryByKeyboard([string]$SerialPath,
    [string]$ExpectedPath) {
    $before = (Get-Serial $SerialPath).Length
    Press-Key 'home'
    Press-Key 'ret'
    return Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path={0} n=\d+ sel=none size=none view=0 status=' -f [regex]::Escape($ExpectedPath)) $before 20
}

function Invoke-C163ManagedFileExplorerScenario([int]$Number,
    [int]$ScreenWidth, [int]$ScreenHeight, [string]$SerialPath) {
    if ($Number -eq 1) {
        $identity = Launch-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-FILE-EXPLORER-TESTS cases=(\d+) refresh=1000 navigation=100 bounded=true result=PASS' 0 120)
        $rootC151 = (Get-Serial $SerialPath).Length
        Click-ManagedFileExplorerRow 0 $ScreenWidth $ScreenHeight
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps n=\d+ sel=C151 type=Directory size=none view=0 status=Directory selected' $rootC151 15)
        Click-ManagedFileExplorerButton 'Open' $ScreenWidth $ScreenHeight
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps/C151 n=15 sel=none size=none view=0 status=Select an entry' $rootC151 20)
        $alphaStart = (Get-Serial $SerialPath).Length
        Click-ManagedFileExplorerRow 1 $ScreenWidth $ScreenHeight
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps/C151 n=15 sel=alpha\.txt type=File size=56 view=0 status=File selected' $alphaStart 15)
        $wheelStart = (Get-Serial $SerialPath).Length
        $origin = Get-ManagedFileExplorerWindowOrigin $ScreenWidth $ScreenHeight
        Move-Pointer ($origin.X + 40) ($origin.Y + 24 + 120)
        Send-QmpEvents @((New-Wheel 1)) 180
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps/C151 n=15 sel=alpha\.txt type=File size=56 view=([1-9]\d*) status=File selected' $wheelStart 15)
        $refreshStart = (Get-Serial $SerialPath).Length
        Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key 'r' $true),
            (New-Key 'r' $false), (New-Key 'ctrl' $false)) 70
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps/C151 n=15 sel=alpha\.txt type=File size=56 view=\d+ status=File selected' $refreshStart 20)
        $upStart = (Get-Serial $SerialPath).Length
        Click-ManagedFileExplorerButton 'Up' $ScreenWidth $ScreenHeight
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps n=\d+ sel=none size=none view=0 status=Select an entry' $upStart 20)
        Close-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
        return [pscustomobject]@{ LaunchCount = 1; CloseCount = 1; Identity = $identity }
    }
    if ($Number -eq 2) {
        $firstIdentity = Launch-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
        [void](Open-ManagedFileExplorerDirectoryByKeyboard $SerialPath '/system/apps/C151')
        Press-Key 'tab'
        $focusStart = (Get-Serial $SerialPath).Length
        Send-QmpEvents @((New-Key 'shift' $true), (New-Key 'tab' $true),
            (New-Key 'tab' $false), (New-Key 'shift' $false)) 90
        $focusTail = (Get-Serial $SerialPath).Substring($focusStart)
        if ($focusTail -notmatch '\[C129-KEYBOARD\] shift=down side=left ' -or
            $focusTail -notmatch '\[C129-KEYBOARD\] shift=up side=left ') {
            throw 'C163 Shift+Tab did not traverse the real managed focus order.'
        }
        $refreshStart = (Get-Serial $SerialPath).Length
        Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key 'r' $true),
            (New-Key 'r' $false), (New-Key 'ctrl' $false)) 70
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps/C151 n=15 sel=none size=none view=0 status=Select an entry' $refreshStart 20)
        $upStart = (Get-Serial $SerialPath).Length
        Click-ManagedFileExplorerButton 'Up' $ScreenWidth $ScreenHeight
        [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps n=\d+ sel=none size=none view=0 status=Select an entry' $upStart 20)
        Close-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
        $secondIdentity = Launch-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
        if ($firstIdentity -eq $secondIdentity) {
            throw 'C163 relaunch reused the previous Managed File Explorer lifetime.'
        }
        if ((Get-Serial $SerialPath).Substring((Get-Serial $SerialPath).Length - 2000) -notmatch 'C163-EXPLORER id=com.guidexos.apps.managed.fileexplorer sel=8 path=/system/apps') {
            throw 'C163 relaunch did not begin at the canonical managed VFS root.'
        }
        Close-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
        return [pscustomobject]@{ LaunchCount = 2; CloseCount = 2; Identity = $secondIdentity }
    }
    if ($Number -eq 3) {
        # Free Calculator's bounded managed slot so Notes, File Explorer, and
        # Task Manager can coexist within the established capacity of three.
        Activate-ManagedTaskManager $ScreenWidth $ScreenHeight
        $calculatorId = Select-TaskManagerManagedCalculator $ScreenWidth $ScreenHeight $SerialPath
        [void](Click-TaskManagerCloseApplication $ScreenWidth $ScreenHeight $SerialPath)
        $closeStart = (Get-Serial $SerialPath).Length
        Click-TaskManagerDialogButton $true $ScreenWidth $ScreenHeight
        [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=3:{0} result=0 fresh=true remains=false result=PASS' -f [regex]::Escape($calculatorId)) $closeStart 20)
        Close-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath

        Send-ManagedNotesLaunch $ScreenWidth $ScreenHeight $SerialPath
        Invoke-NotesClipboardSmoke $SerialPath
        $seen = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::Ordinal)
        $launchCount = 0
        $closeCount = 0
        for ($cycle = 1; $cycle -le 25; $cycle++) {
            $identity = Launch-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
            if (-not $seen.Add($identity)) {
                throw "C163 lifecycle cycle $cycle reused identity $identity."
            }
            $launchCount++
            [void](Open-ManagedFileExplorerDirectoryByKeyboard $SerialPath '/system/apps/C151')
            Press-Key 'tab'
            $upStart = (Get-Serial $SerialPath).Length
            Press-Key 'ret' # The Up control navigates to the parent.
            [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C163-STATE ev=input k=\d+ path=/system/apps n=\d+ sel=none size=none view=0 status=Select an entry' $upStart 20)
            Close-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
            $closeCount++
        }
        $finalFileExplorerId = Launch-ManagedFileExplorer $ScreenWidth $ScreenHeight $SerialPath
        if (-not $seen.Add($finalFileExplorerId)) {
            throw 'C163 final File Explorer lifetime reused an earlier lifecycle identity.'
        }
        $launchCount++

        $taskManagerId = Launch-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        Focus-TaskManagerList $ScreenWidth $ScreenHeight
        $before = (Get-Serial $SerialPath).Length
        Press-Key 'end'
        $endSelection = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row=\d+ identity=3:([0-9]+) on=[01] result=PASS' $before 15
        $before = (Get-Serial $SerialPath).Length
        Press-Key 'up'
        $fileExplorerSelection = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row=\d+ identity=3:([0-9]+) on=[01] result=PASS' $before 15
        $hex = ([uint64]::Parse($fileExplorerSelection.Match.Groups[1].Value,
            [System.Globalization.CultureInfo]::InvariantCulture)).ToString('X16')
        [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C161-TM-ID 3:{0}=com\.guidexos\.apps\.managed\.fileexplorer\r?$' -f $hex) $before 15)
        $detail = Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C161-TM-DETAIL source=3 identity={0} name=Managed File Explorer state=[^ ]+ active=(Yes|No) result=PASS' -f $hex) $before 10
        Close-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath

        # The native File Explorer remains a separate KernelApp registered as
        # Files. A fresh Task Manager snapshot below must find its real ID.
        $menuCount = 22
        $menuHeight = 30 + $menuCount * 22 + 36
        $menuY = [Math]::Max(0, ($ScreenHeight - 40) - $menuHeight)
        $contentY = $menuY + 31
        $footerY = $menuY + 30 + $menuCount * 22
        Click-Screen 50 ($ScreenHeight - 20)
        Click-Screen 70 ($footerY + 18)
        $nativeStart = (Get-Serial $SerialPath).Length
        Click-Screen 115 ($contentY + 5 * 22 + 11)
        Start-Sleep -Milliseconds 900
        $nativeText = Get-Serial $SerialPath
        if ($nativeText.Substring($nativeStart) -match 'Managed application launch rejected|Not available in bare-metal mode') {
            throw 'C163 native File Explorer launcher reported a rejected or unavailable launch.'
        }
        $nativeTaskManagerId = Launch-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        $nativeExplorerIdentity = Select-TaskManagerNativeFileExplorer $ScreenWidth $ScreenHeight $SerialPath
        [void](Click-TaskManagerCloseApplication $ScreenWidth $ScreenHeight $SerialPath)
        $nativeCloseStart = (Get-Serial $SerialPath).Length
        Click-TaskManagerDialogButton $true $ScreenWidth $ScreenHeight
        [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=1:{0} result=0 fresh=true remains=false result=PASS' -f $nativeExplorerIdentity) $nativeCloseStart 20)
        Close-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        return [pscustomobject]@{ LaunchCount = $launchCount; CloseCount = $closeCount; Identity = $finalFileExplorerId; TaskManagerId = $taskManagerId; NativeTaskManagerId = $nativeTaskManagerId; NativeFileExplorerIdentity = $nativeExplorerIdentity; FileExplorerSnapshotActive = $detail.Match.Groups[1].Value }
    }
    throw "Unknown C163 production boot number $Number."
}

function Invoke-TaskManagerRefresh([int]$ScreenWidth, [int]$ScreenHeight,
                                   [string]$SerialPath) {
    $before = (Get-Serial $SerialPath).Length
    $pattern = '^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R st=\d+ n=\d+ sel=([^ ]+) v=(\d+) ap=true self=(\d+) active=([01]) result=PASS'
    Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key 'r' $true),
        (New-Key 'r' $false), (New-Key 'ctrl' $false)) 55
    try { return (Wait-Serial $SerialPath $pattern $before 3) } catch {
        if ($_.Exception.Message -notlike 'Timed out waiting for serial marker:*') { throw }
    }

    # A just-launched native app may have taken focus after Task Manager was
    # last active. Re-activate through its exposed title bar, then its exposed
    # right edge, before retrying the same read-only refresh command.
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 800) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 370) / 2))
    foreach ($point in @(
        [pscustomobject]@{ X = $windowX + 100; Y = $windowY + 12 },
        [pscustomobject]@{ X = $windowX + 760; Y = $windowY + 90 }
    )) {
        Click-Screen $point.X $point.Y
        Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key 'r' $true),
            (New-Key 'r' $false), (New-Key 'ctrl' $false)) 55
        try { return (Wait-Serial $SerialPath $pattern $before 5) } catch {
            if ($_.Exception.Message -notlike 'Timed out waiting for serial marker:*') { throw }
        }
    }
    return (Wait-Serial $SerialPath $pattern $before 2)
}

function Close-ManagedTaskManager([int]$ScreenWidth, [int]$ScreenHeight,
                                  [string]$SerialPath) {
    if ($PhaseC162) { Activate-ManagedTaskManager $ScreenWidth $ScreenHeight }
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 800) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 370) / 2))
    $before = (Get-Serial $SerialPath).Length
    Click-Screen ($windowX + 390) ($windowY + 24 + 290)
    [void](Wait-Serial $SerialPath '^\[C161-TM-CLOSE-DISPATCH\] selector=7 controls=0 result=PASS' $before 90)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-CLOSE controls=0 selection=none result=PASS' $before 15)
}

function Activate-ManagedTaskManager([int]$ScreenWidth, [int]$ScreenHeight) {
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 800) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 370) / 2))
    # The Task Manager title bar is above the centered Calculator and Notes
    # windows, so this exposed point can reliably activate it in the
    # coexistence layout.
    Click-Screen ($windowX + 100) ($windowY + 12)
}

function Focus-TaskManagerList([int]$ScreenWidth, [int]$ScreenHeight) {
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 800) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 370) / 2))
    Click-Screen ($windowX + 30) ($windowY + 24 + 64)
}

function Click-TaskManagerCloseApplication([int]$ScreenWidth,
    [int]$ScreenHeight, [string]$SerialPath) {
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 800) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 370) / 2))
    $pattern = '^\[C102-MANAGED-OUTPUT\] C162-TM-CONFIRM open=true identity=captured modal=true result=PASS'
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        if ($attempt -eq 2) {
            # A transiently dropped pointer click can leave Task Manager
            # behind another live surface. Re-activate its exposed title bar
            # before retrying the same close-button hit target.
            Activate-ManagedTaskManager $ScreenWidth $ScreenHeight
        }
        $before = (Get-Serial $SerialPath).Length
        Click-Screen ($windowX + 234) ($windowY + 24 + 290)
        try { return (Wait-Serial $SerialPath $pattern $before 20) } catch {
            if ($attempt -eq 2 -or $_.Exception.Message -notlike 'Timed out waiting for serial marker:*') { throw }
        }
    }
    throw 'Managed Task Manager did not open its close confirmation after two pointer attempts.'
}

function Click-TaskManagerDialogButton([bool]$Confirm,
    [int]$ScreenWidth, [int]$ScreenHeight) {
    if (-not $Confirm) {
        # Escape invokes the dialog's explicit cancel result and is stable
        # even when another live surface overlaps the modal button location.
        Press-Key 'esc'
        return
    } else {
        # C145 gives the confirmation's default Close button initial focus.
        # Commit through the modal keyboard path, which also proves that the
        # parent's shortcut/input routing stays behind the modal boundary.
        Press-Key 'ret'
        return
    }
}

function Select-TaskManagerManagedCalculator([int]$ScreenWidth,
    [int]$ScreenHeight, [string]$SerialPath) {
    Focus-TaskManagerList $ScreenWidth $ScreenHeight
    $before = (Get-Serial $SerialPath).Length
    Press-Key 'end'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row=\d+ identity=3:\d+ on=[01] result=PASS' $before 12)
    $before = (Get-Serial $SerialPath).Length
    Press-Key 'up'
    $selected = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row=\d+ identity=3:(\d+) on=[01] result=PASS' $before 12
    $identity = $selected.Match.Groups[1].Value
    $eligibility = Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE-ELIGIBILITY identity=3:{0} enabled=true result=PASS' -f [regex]::Escape($identity)) $before 12
    $full = Get-Serial $SerialPath
    $calculatorHex = ([uint64]::Parse($identity,
        [System.Globalization.CultureInfo]::InvariantCulture)).ToString('X16')
    $expectedCalculatorId = 'C161-TM-ID 3:{0}=com.guidexos.apps.managed.calculator' -f $calculatorHex
    if ($full.IndexOf($expectedCalculatorId, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Task Manager selected identity 3:$identity without a matching Managed Calculator application record."
    }
    return $identity
}

function Select-TaskManagerNativeFileExplorer([int]$ScreenWidth,
    [int]$ScreenHeight, [string]$SerialPath) {
    Focus-TaskManagerList $ScreenWidth $ScreenHeight
    $before = (Get-Serial $SerialPath).Length
    $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
    $rowCountMatch = [regex]::Match($refresh.Match.Value, ' n=(\d+) sel=')
    if (-not $rowCountMatch.Success) {
        throw 'C163 could not read the Task Manager row count for native File Explorer.'
    }
    $rowCount = [int]$rowCountMatch.Groups[1].Value
    for ($row = $rowCount - 1; $row -ge 0; $row--) {
        $selectionStart = (Get-Serial $SerialPath).Length
        if ($row -eq $rowCount - 1) { Press-Key 'end' } else { Press-Key 'up' }
        $selected = Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row={0} identity=(\d+):(\d+) on=[01] result=PASS' -f $row) $selectionStart 12
        if ($selected.Match.Groups[1].Value -ne '1') { continue }
        $identity = $selected.Match.Groups[2].Value
        $hex = ([uint64]::Parse($identity,
            [System.Globalization.CultureInfo]::InvariantCulture)).ToString('X16')
        $expected = '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-ID 1:{0}=gxos\.builtin\.fileexplorer\r?$' -f $hex
        $full = Get-Serial $SerialPath
        if ($full.Substring($selectionStart) -match $expected) {
            [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE-ELIGIBILITY identity=1:{0} enabled=true result=PASS' -f $identity) $selectionStart 10)
            return $identity
        }
    }
    throw 'Managed Task Manager snapshot did not contain native gxos.builtin.fileexplorer.'
}

function Invoke-C162ManagedCalculatorClose([int]$ScreenWidth,
    [int]$ScreenHeight, [string]$SerialPath) {
    $identity = Select-TaskManagerManagedCalculator $ScreenWidth $ScreenHeight $SerialPath
    [void](Click-TaskManagerCloseApplication $ScreenWidth $ScreenHeight $SerialPath)
    $cancelStart = (Get-Serial $SerialPath).Length
    Click-TaskManagerDialogButton $false $ScreenWidth $ScreenHeight
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C162-TM-CANCEL mutation=none selection=preserved result=PASS' $cancelStart 15)

    $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
    if ($refresh.Match.Groups[1].Value -ne "3:$identity") {
        throw 'C162 cancel or Refresh changed the captured Calculator selection.'
    }

    [void](Click-TaskManagerCloseApplication $ScreenWidth $ScreenHeight $SerialPath)
    $closeStart = (Get-Serial $SerialPath).Length
    Click-TaskManagerDialogButton $true $ScreenWidth $ScreenHeight
    $closePattern = '^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=3:{0} result=0 fresh=true remains=false result=PASS' -f [regex]::Escape($identity)
    [void](Wait-Serial $SerialPath $closePattern $closeStart 20)
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-DETAIL selection=none result=PASS' $closeStart 10)

    $launchStart = (Get-Serial $SerialPath).Length
    Send-CalculatorLaunch $true $ScreenWidth $ScreenHeight $SerialPath
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator selector=6 controls=18 capacity=18 focus=18 fresh=PASS result=PASS' $launchStart 35)
    $calcStart = (Get-Serial $SerialPath).Length
    Press-Key '7'
    Send-QmpEvents @((New-Key 'shift' $true), (New-Key '8' $true),
        (New-Key '8' $false), (New-Key 'shift' $false)) 90
    Press-Key '8'; Press-Key 'ret'
    [void](Wait-Display $SerialPath '56' $calcStart 20)

    Activate-ManagedTaskManager $ScreenWidth $ScreenHeight
    $snapshotStart = (Get-Serial $SerialPath).Length
    $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
    if ($refresh.Match.Groups[4].Value -ne '1') {
        throw 'Task Manager did not remain live after closing and relaunching Managed Calculator.'
    }
    Focus-TaskManagerList $ScreenWidth $ScreenHeight
    $newIdentities = [System.Collections.Generic.List[string]]::new()
    $refreshLine = [regex]::Match($refresh.Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R st=\d+ n=(\d+) sel=[^ ]+ v=\d+ ap=true self=\d+ active=1 result=PASS')
    if (-not $refreshLine.Success) {
        throw 'C162 could not recover the fresh Task Manager snapshot row count.'
    }
    $rowCount = [int]$refreshLine.Groups[1].Value
    for ($row = 0; $row -lt $rowCount; $row++) {
        $selectionStart = (Get-Serial $SerialPath).Length
        if ($row -eq 0) { Press-Key 'home' } else { Press-Key 'down' }
        $selected = Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row={0} identity=([^ ]+) on=[01] result=PASS' -f $row) $selectionStart 12
        $selectedIdentity = $selected.Match.Groups[1].Value
        $identityMatch = [regex]::Match($selectedIdentity, '^3:(?<instance>[0-9]+)$')
        if (-not $identityMatch.Success) { continue }
        $selectedHex = ([uint64]::Parse($identityMatch.Groups['instance'].Value,
            [System.Globalization.CultureInfo]::InvariantCulture)).ToString('X16')
        $detailTail = (Get-Serial $SerialPath).Substring($selectionStart)
        if ($detailTail -match ('(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-ID 3:{0}=com\.guidexos\.apps\.managed\.calculator\r?$' -f $selectedHex)) {
            $newIdentities.Add($selectedHex)
        }
    }
    $oldHex = ([uint64]::Parse($identity,
        [System.Globalization.CultureInfo]::InvariantCulture)).ToString('X16')
    if ($newIdentities.Count -ne 1 -or $newIdentities[0] -eq $oldHex) {
        throw 'Fresh Task Manager snapshot did not show exactly one replacement Calculator lifetime distinct from the closed identity.'
    }
    $newIdentity = [Convert]::ToUInt64($newIdentities[0], 16).ToString(
        [System.Globalization.CultureInfo]::InvariantCulture)
    return [pscustomobject]@{ OldIdentity = $identity; NewIdentity = $newIdentity }
}

function Launch-NativeCalculator([int]$ScreenWidth, [int]$ScreenHeight,
    [string]$SerialPath) {
    $menuCount = if ($PhaseC163) { 22 } else { 21 }
    $menuHeight = 30 + $menuCount * 22 + 36
    $menuY = [Math]::Max(0, ($ScreenHeight - 40) - $menuHeight)
    $contentY = $menuY + 31
    $footerY = $menuY + 30 + $menuCount * 22
    Click-Screen 50 ($ScreenHeight - 20)
    Click-Screen 70 ($footerY + 18)
    Click-Screen 115 ($contentY + 11)
}

function Invoke-C162NativeCalculatorClose([int]$ScreenWidth,
    [int]$ScreenHeight, [string]$SerialPath) {
    Focus-TaskManagerList $ScreenWidth $ScreenHeight
    # C160 keeps native Calculator and native Task Manager alive; C161 adds
    # seven more Calculator instances to make the real list scrollable. Remove
    # those extra proof instances from the end so their slot changes cannot
    # hide which lifetime is being targeted.
    for ($row = 8; $row -ge 2; $row--) {
        Focus-TaskManagerList $ScreenWidth $ScreenHeight
        $before = (Get-Serial $SerialPath).Length
        Press-Key 'home'
        for ($index = 0; $index -lt $row; $index++) { Press-Key 'down' }
        $selected = Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row={0} identity=1:(\d+) on=[01] result=PASS' -f $row) $before 18
        $identity = $selected.Match.Groups[1].Value
        $hex = ([uint64]::Parse($identity,
            [System.Globalization.CultureInfo]::InvariantCulture)).ToString('X16')
        if ((Get-Serial $SerialPath) -notmatch ('(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-ID 1:{0}=gxos\.builtin\.calculator\r?$' -f $hex)) {
            throw "C162 proof cleanup row $row was not a C161 wheel-proof Calculator instance."
        }
        [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE-ELIGIBILITY identity=1:{0} enabled=true result=PASS' -f [regex]::Escape($identity)) $before 12)
        [void](Click-TaskManagerCloseApplication $ScreenWidth $ScreenHeight $SerialPath)
        $closeStart = (Get-Serial $SerialPath).Length
        Click-TaskManagerDialogButton $true $ScreenWidth $ScreenHeight
        $closePattern = '^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=1:{0} result=0 fresh=true remains=false result=PASS' -f [regex]::Escape($identity)
        [void](Wait-Serial $SerialPath $closePattern $closeStart 20)
    }

    Focus-TaskManagerList $ScreenWidth $ScreenHeight
    $before = (Get-Serial $SerialPath).Length
    Press-Key 'home'
    $selected = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row=0 identity=1:(\d+) on=[01] result=PASS' $before 12
    $oldIdentity = $selected.Match.Groups[1].Value
    $oldHex = ([uint64]::Parse($oldIdentity,
        [System.Globalization.CultureInfo]::InvariantCulture)).ToString('X16')
    if ((Get-Serial $SerialPath) -notmatch ('(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-ID 1:{0}=gxos\.builtin\.calculator\r?$' -f $oldHex)) {
        throw 'C162 native close target was not the native Calculator AppManager record.'
    }
    [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE-ELIGIBILITY identity=1:{0} enabled=true result=PASS' -f [regex]::Escape($oldIdentity)) $before 12)
    [void](Click-TaskManagerCloseApplication $ScreenWidth $ScreenHeight $SerialPath)
    $closeStart = (Get-Serial $SerialPath).Length
    Click-TaskManagerDialogButton $true $ScreenWidth $ScreenHeight
    $closePattern = '^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=1:{0} result=0 fresh=true remains=false result=PASS' -f [regex]::Escape($oldIdentity)
    [void](Wait-Serial $SerialPath $closePattern $closeStart 20)

    Launch-NativeCalculator $ScreenWidth $ScreenHeight $SerialPath
    Start-Sleep -Milliseconds 600
    # Exercise the native calculator input path with the QMP shifted-8 key.
    Press-Key '7'
    Send-QmpEvents @((New-Key 'shift' $true), (New-Key '8' $true),
        (New-Key '8' $false), (New-Key 'shift' $false)) 90
    Press-Key '8'; Press-Key 'ret'
    # Native Calculator has no C158 managed display marker. Its real
    # 7*8=56 close/relaunch path is covered by the focused native AppManager
    # test emitted on every production boot; here the fresh C160 snapshot
    # below verifies that the QMP-launched replacement is live and selectable.
    Start-Sleep -Milliseconds 500
    Activate-ManagedTaskManager $ScreenWidth $ScreenHeight
    [void](Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath)
    Focus-TaskManagerList $ScreenWidth $ScreenHeight
    $before = (Get-Serial $SerialPath).Length
    Press-Key 'home'
    Press-Key 'down'
    $selected = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row=1 identity=1:(\d+) on=[01] result=PASS' $before 15
    $newIdentity = $selected.Match.Groups[1].Value
    $newHex = ([uint64]::Parse($newIdentity,
        [System.Globalization.CultureInfo]::InvariantCulture)).ToString('X16')
    $full = Get-Serial $SerialPath
    if ($newIdentity -eq $oldIdentity -or
        $full -notmatch ('(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-ID 1:{0}=gxos\.builtin\.calculator\r?$' -f $newHex)) {
        throw 'Native Calculator close/relaunch did not produce a distinct authoritative identity.'
    }

    Focus-TaskManagerList $ScreenWidth $ScreenHeight
    $before = (Get-Serial $SerialPath).Length
    Press-Key 'home'
    for ($index = 0; $index -lt 2; $index++) { Press-Key 'down' }
    $shellLine = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row=2 identity=2:(\d+) on=[01] result=PASS' $before 15
    $shellIdentity = $shellLine.Match.Groups[1].Value
    [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE-ELIGIBILITY identity=2:{0} enabled=false result=PASS' -f [regex]::Escape($shellIdentity)) $before 12)
    $before = (Get-Serial $SerialPath).Length
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 800) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 370) / 2))
    Click-Screen ($windowX + 234) ($windowY + 24 + 290)
    Start-Sleep -Milliseconds 250
    if ((Get-Serial $SerialPath).Substring($before) -match 'C162-TM-CONFIRM open=true') {
        throw 'Task Manager opened a close confirmation for protected shell.'
    }
    $full = Get-Serial $SerialPath
    if ($full -notmatch ('(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-ID 2:{0}=none\r?$' -f [regex]::Escape($shellIdentity))) {
        # Terminal has no application ID; its stable shell lifetime remains in
        # the authoritative snapshot when refreshed after the rejected click.
        [void](Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath)
    }
    Focus-TaskManagerList $ScreenWidth $ScreenHeight
    $before = (Get-Serial $SerialPath).Length
    Press-Key 'end'
    $self = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=keyboard row=5 identity=3:(\d+) on=[01] result=PASS' $before 12
    $selfIdentity = $self.Match.Groups[1].Value
    [void](Wait-Serial $SerialPath ('^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE-ELIGIBILITY identity=3:{0} enabled=false result=PASS' -f [regex]::Escape($selfIdentity)) $before 12)
    return [pscustomobject]@{ OldIdentity = $oldIdentity; NewIdentity = $newIdentity }
}

function Invoke-TaskManagerPointerWheelFocus([int]$ScreenWidth,
    [int]$ScreenHeight, [string]$SerialPath) {
    if ($PhaseC162) { Activate-ManagedTaskManager $ScreenWidth $ScreenHeight }
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 800) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 370) / 2))
    $before = (Get-Serial $SerialPath).Length
    Click-Screen ($windowX + 25) ($windowY + 24 + 58 + 9 * 18 + 9)
    $selected = Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-SELECT source=pointer row=\d+ identity=(\d+:\d+) on=[01] result=PASS' $before 20
    $wheelStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents @((New-Wheel 1)) 90
    [void](Wait-Serial $SerialPath '^\[C137-NATIVE-INPUT\] kind=wheel delta=0*1 .*buttons-preserved=true result=PASS' $wheelStart 20)
    $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
    if ($refresh.Match.Groups[1].Value -ne $selected.Match.Groups[1].Value -or
        [int]$refresh.Match.Groups[2].Value -lt 1) {
        throw 'C161 pointer/wheel refresh did not retain the selected identity and scroll the list viewport.'
    }

    $refreshBefore = (Get-Serial $SerialPath).Length
    $refreshButtonX = $windowX + 80
    $refreshButtonY = $windowY + 24 + 290
    Click-Screen $refreshButtonX $refreshButtonY
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-R s=button/keyboard st=\d+ n=\d+ sel=[^ ]+ v=\d+ ap=true self=\d+ active=1 result=PASS' $refreshBefore 20)

    $listFocusStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents @((New-Key 'shift' $true), (New-Key 'tab' $true),
        (New-Key 'tab' $false), (New-Key 'shift' $false)) 90
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-FOCUS control=1 shift=true result=PASS' $listFocusStart 15)
    $focusStart = (Get-Serial $SerialPath).Length
    Press-Key 'tab'
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-FOCUS control=2 shift=false result=PASS' $focusStart 15)
    $focusStart = (Get-Serial $SerialPath).Length
    Send-QmpEvents @((New-Key 'shift' $true), (New-Key 'tab' $true),
        (New-Key 'tab' $false), (New-Key 'shift' $false)) 90
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C161-TM-FOCUS control=1 shift=true result=PASS' $focusStart 15)
}

function Invoke-C161ProductionScenario([int]$Number, [int]$ScreenWidth,
    [int]$ScreenHeight, [string]$SerialPath) {
    $launchIds = [System.Collections.Generic.List[string]]::new()
    $refreshCount = 0
    $closeCount = 0
    $finalSelected = 'none'
    $finalViewport = -1

    if ($Number -eq 3 -and $PhaseC163) {
        # C162's authoritative baseline covers its full 25-cycle close stress.
        # C163 needs a live Task Manager plus Calculator for its real
        # File Explorer coexistence/identity checks, so exercise one ordinary
        # refresh here and leave the manager open for that workflow.
        $identity = Launch-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        $launchIds.Add($identity)
        $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
        if ($refresh.Match.Groups[3].Value -ne $identity -or
            $refresh.Match.Groups[4].Value -ne '1') {
            throw 'C163 Task Manager smoke changed or deactivated its live identity.'
        }
        $finalSelected = $refresh.Match.Groups[1].Value
        $finalViewport = [int]$refresh.Match.Groups[2].Value
        $refreshCount++
        Focus-TaskManagerList $ScreenWidth $ScreenHeight
    } elseif ($Number -eq 1) {
        Invoke-NotesClipboardSmoke $SerialPath
        $identity = Launch-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        $launchIds.Add($identity)
        Invoke-TaskManagerPointerWheelFocus $ScreenWidth $ScreenHeight $SerialPath
        $refreshCount += 2
        if ($PhaseC162) {
            $calculatorClose = Invoke-C162ManagedCalculatorClose $ScreenWidth $ScreenHeight $SerialPath
            if ($calculatorClose.NewIdentity -eq $calculatorClose.OldIdentity) {
                throw 'C162 boot 1 did not assign a fresh Managed Calculator lifetime.'
            }
        }
        for ($refreshIndex = 0; $refreshIndex -lt 5; $refreshIndex++) {
            $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
            if ($refresh.Match.Groups[3].Value -ne $identity -or
                $refresh.Match.Groups[4].Value -ne '1') {
                throw 'C161 Task Manager self identity changed or became inactive during boot 1 refresh.'
            }
            $finalSelected = $refresh.Match.Groups[1].Value
            $finalViewport = [int]$refresh.Match.Groups[2].Value
            $refreshCount++
        }
        Close-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        $closeCount++
    } elseif ($Number -eq 2) {
        # Recreate a real active Calculator surface after the keyboard suite's
        # deliberate close, then replace it with Managed Task Manager.
        Send-CalculatorLaunch $true $ScreenWidth $ScreenHeight $SerialPath
        $calcStart = (Get-Serial $SerialPath).Length
        Press-Key '7'
        Send-QmpEvents @((New-Key 'shift' $true), (New-Key '8' $true),
            (New-Key '8' $false), (New-Key 'shift' $false)) 90
        Press-Key '8'; Press-Key 'ret'
        [void](Wait-Display $SerialPath '56' $calcStart 20)
        $firstIdentity = Launch-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        $launchIds.Add($firstIdentity)
        if ($PhaseC162) {
            $nativeClose = Invoke-C162NativeCalculatorClose $ScreenWidth $ScreenHeight $SerialPath
            if ($nativeClose.NewIdentity -eq $nativeClose.OldIdentity) {
                throw 'C162 boot 2 did not assign a fresh native Calculator lifetime.'
            }
        }
        for ($refreshIndex = 0; $refreshIndex -lt 10; $refreshIndex++) {
            $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
            if ($refresh.Match.Groups[3].Value -ne $firstIdentity -or
                $refresh.Match.Groups[4].Value -ne '1') {
                throw 'C161 Task Manager self identity changed or became inactive during boot 2 refresh.'
            }
            $finalSelected = $refresh.Match.Groups[1].Value
            $refreshCount++
        }
        Close-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        $closeCount++
        $secondIdentity = Launch-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        $launchIds.Add($secondIdentity)
        if ($secondIdentity -eq $firstIdentity) {
            throw 'C161 Task Manager relaunch reused the previous lifetime identity.'
        }
        for ($refreshIndex = 0; $refreshIndex -lt 10; $refreshIndex++) {
            $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
            if ($refresh.Match.Groups[3].Value -ne $secondIdentity -or
                $refresh.Match.Groups[4].Value -ne '1') {
                throw 'C161 relaunched Task Manager self identity changed or became inactive.'
            }
            $finalSelected = $refresh.Match.Groups[1].Value
            $refreshCount++
        }
        Close-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        $closeCount++
    } elseif ($Number -eq 3) {
        $seen = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::Ordinal)
        if ($PhaseC162) {
            $closeCycleManager = Launch-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
            $launchIds.Add($closeCycleManager)
            $calculatorLifetimes = [System.Collections.Generic.HashSet[string]]::new(
                [System.StringComparer]::Ordinal)
            for ($cycle = 1; $cycle -le 25; $cycle++) {
                $closeCycle = Invoke-C162ManagedCalculatorClose $ScreenWidth $ScreenHeight $SerialPath
                if (-not $calculatorLifetimes.Add($closeCycle.NewIdentity) -or
                    $closeCycle.NewIdentity -eq $closeCycle.OldIdentity) {
                    throw "C162 Calculator lifecycle cycle $cycle reused an identity."
                }
                $refreshCount++
            }
            Close-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
            $closeCount++
        }
        for ($cycle = 1; $cycle -le 25; $cycle++) {
            $identity = Launch-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
            if (-not $seen.Add($identity)) {
                throw "C161 lifecycle cycle $cycle reused lifetime identity $identity."
            }
            $launchIds.Add($identity)
            $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
            if ($refresh.Match.Groups[3].Value -ne $identity -or
                $refresh.Match.Groups[4].Value -ne '1') {
                throw "C161 lifecycle cycle $cycle changed or deactivated its self identity."
            }
            $finalSelected = $refresh.Match.Groups[1].Value
            $refreshCount++
            Close-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
            $closeCount++
        }
        # Notes -> Task Manager proves the app registry transition after churn.
        Send-ManagedNotesLaunch $ScreenWidth $ScreenHeight $SerialPath
        $finalIdentity = Launch-ManagedTaskManager $ScreenWidth $ScreenHeight $SerialPath
        if (-not $seen.Add($finalIdentity)) {
            throw 'C161 Task Manager identity did not change after a Notes transition.'
        }
        $launchIds.Add($finalIdentity)
        for ($refreshIndex = 0; $refreshIndex -lt 100; $refreshIndex++) {
            $refresh = Invoke-TaskManagerRefresh $ScreenWidth $ScreenHeight $SerialPath
            if ($refresh.Match.Groups[3].Value -ne $finalIdentity -or
                $refresh.Match.Groups[4].Value -ne '1') {
                throw "C161 real refresh stress changed or deactivated self identity at refresh $($refreshIndex + 1)."
            }
            $finalSelected = $refresh.Match.Groups[1].Value
            $finalViewport = [int]$refresh.Match.Groups[2].Value
            $refreshCount++
        }
        # Keep the final valid Task Manager snapshot alive to capture state.
    } else {
        throw "Unknown C161 production boot number $Number."
    }

    if ($finalSelected -eq 'none') {
        $last = [regex]::Matches((Get-Serial $SerialPath),
            '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R st=\d+ n=\d+ sel=([^ ]+) v=(\d+) ap=true self=(\d+) on=1 result=PASS')
        if ($last.Count -gt 0) {
            $finalSelected = $last[$last.Count - 1].Groups[1].Value
            $finalViewport = [int]$last[$last.Count - 1].Groups[2].Value
        }
    }
    return [pscustomobject]@{
        LaunchIds = @($launchIds.ToArray())
        LaunchCount = $launchIds.Count
        CloseCount = $closeCount
        RefreshCount = $refreshCount
        FinalLifetimeId = if ($Number -eq 3) { $launchIds[$launchIds.Count - 1] } else { $null }
        FinalSelectedIdentity = $finalSelected
        FinalViewport = $finalViewport
        ActiveAtEnd = $Number -eq 3
    }
}

function Get-CalculatorScreenPoint([int]$Index, [int]$ScreenWidth,
                                   [int]$ScreenHeight) {
    $columnX = @(44, 112, 180, 248)
    $windowX = [Math]::Max(0, [int](($ScreenWidth - 292) / 2))
    $windowY = [Math]::Max(0, [int](($ScreenHeight - 270) / 2))
    $row = [int][Math]::Floor($Index / 4.0)
    $column = $Index % 4
    return [pscustomobject]@{
        X = $windowX + $columnX[$column]
        Y = $windowY + 24 + 80 + 36 * $row
    }
}

function Click-CalculatorButton([int]$Index, [int]$ScreenWidth,
                                [int]$ScreenHeight, [string]$SerialPath,
                                [string]$ExpectedDisplay) {
    $point = Get-CalculatorScreenPoint $Index $ScreenWidth $ScreenHeight
    $before = (Get-Serial $SerialPath).Length
    Click-Screen $point.X $point.Y
    $pattern = '^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=' +
        [regex]::Escape($ExpectedDisplay) + ' phase=\d+ commands=\d+ result=PASS'
    [void](Wait-Serial $SerialPath $pattern $before 20)
}

function Wait-Display([string]$SerialPath, [string]$Display, [int]$After,
                      [int]$Seconds = 20) {
    $pattern = '^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=' +
        [regex]::Escape($Display) + ' phase=\d+ commands=\d+ result=PASS'
    return Wait-Serial $SerialPath $pattern $After $Seconds
}

function Send-Text([string]$Text) {
    $events = [System.Collections.Generic.List[object]]::new()
    foreach ($character in $Text.ToCharArray()) {
        $code = [string]$character
        $events.Add((New-Key $code $true))
        $events.Add((New-Key $code $false))
    }
    if ($events.Count -gt 0) { Send-QmpEvents $events.ToArray() 110 }
}

function Send-ShiftedPlus {
    Send-QmpEvents @((New-Key 'shift' $true), (New-Key 'equal' $true),
        (New-Key 'equal' $false), (New-Key 'shift' $false)) 110
}

function Close-Calculator([string]$SerialPath) {
    $before = (Get-Serial $SerialPath).Length
    Send-QmpEvents @((New-Key 'alt' $true), (New-Key 'f4' $true),
        (New-Key 'f4' $false), (New-Key 'alt' $false)) 110
    [void](Wait-Serial $SerialPath '^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 active=none modal=none popup=none drag=none result=PASS' $before 30)
    [void](Wait-Serial $SerialPath '^\[C158-CALC-CLOSE-DISPATCH\] selector=6 controls=0 result=PASS' $before 15)
    [void](Wait-Serial $SerialPath '^\[C150-SURFACE\] action=destroy appId=com\.guidexos\.apps\.managed\.calculator generation=[0-9A-Fa-f]+ window=[0-9A-Fa-f]+ reason=close-or-replace' $before 15)
}

function Stop-Qemu([int]$Port, [System.Diagnostics.Process]$Process,
                   [string]$LogPath) {
    if ($Process.HasExited) { return }
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $Port)
        $stream = $client.GetStream()
        [void](Read-Qmp $stream)
        $caps = [System.Text.Encoding]::ASCII.GetBytes('{"execute":"qmp_capabilities"}' + "`n")
        $stream.Write($caps, 0, $caps.Length); $stream.Flush(); [void](Read-Qmp $stream)
        $quit = [System.Text.Encoding]::ASCII.GetBytes('{"execute":"quit"}' + "`n")
        $stream.Write($quit, 0, $quit.Length); $stream.Flush()
        Add-Content -LiteralPath $LogPath -Value 'shutdown=quit' -Encoding ASCII
    } finally { $client.Dispose() }
    $Process.Refresh()
    if (-not $Process.HasExited) {
        try {
            Wait-Process -Id $Process.Id -Timeout 12 -ErrorAction Stop
        } catch {
            $Process.Refresh()
            if (-not $Process.HasExited) { throw }
        }
    }
}

function Start-Qemu([string]$SerialPath, [string]$StdoutPath,
                    [string]$StderrPath, [string]$Esp,
                    [string]$Qemu, [string]$Ovmf) {
    $port = Get-AvailableQmpPort
    $qmpLog = [System.IO.Path]::ChangeExtension($SerialPath, '.qmp.log')
    Remove-Item -LiteralPath $SerialPath,$StdoutPath,$StderrPath,$qmpLog -Force -ErrorAction SilentlyContinue
    $accelerator = if ($PhaseC160) { 'whpx' } else { 'tcg,thread=single' }
    $arguments = @(
        '-accel',$accelerator,'-machine','pc','-smp','1',
        '-drive',('if=pflash,format=raw,readonly=on,file="{0}"' -f $Ovmf),
        '-drive',('file=fat:rw:"{0}",format=raw,if=ide,index=0' -f $Esp),
        '-m','1024M','-vga','std','-display','none',
        '-serial',('file:"{0}"' -f $SerialPath),
        '-qmp',("tcp:127.0.0.1:{0},server,nowait" -f $port),
        '-boot','order=c','-no-reboot','-no-shutdown','-rtc','base=utc,clock=host')
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath -WindowStyle Hidden -PassThru
    $script:activeProcess = $process
    $script:activeStderr = $StderrPath
    $script:activeSerial = $SerialPath
    $script:activePort = $port
    $script:activeQmpLog = $qmpLog
    $script:cursor = $null
    return [pscustomobject]@{ Process = $process; Port = $port; QmpLog = $qmpLog }
}

function Invoke-ProductionBoot([int]$Number, [string]$Scenario,
                               [string]$Qemu, [string]$Ovmf,
                               [string]$SettingsRecord) {
    $root = Join-Path $EvidenceRoot ("production-boot-{0:D2}" -f $Number)
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $esp = Join-Path $root 'ESP'
    Stage-Esp $esp $proofKernel $proofRamdisk $SettingsRecord
    $serial = Join-Path $root 'serial.log'
    $session = Start-Qemu $serial (Join-Path $root 'qemu.stdout.log') `
        (Join-Path $root 'qemu.stderr.log') $esp $Qemu $Ovmf
    $process = $session.Process
    $c161Scenario = $null
    $c164Scenario = $null
    try {
        Write-Host ("C158 production boot {0}/3: waiting for the Notes baseline." -f $Number)
        [void](Wait-Serial $serial '^\[desktop\] bare-metal desktop icon init completed' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[NATIVEAOT-PRODUCTION-LAUNCH\] applicationId=com\.guidexos\.apps\.managed\.notes recordId=com\.guidexos\.apps\.managed\.notes image=/system/apps/GXOSAPP\.ELF selector=00000004' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C158-CALC-APP-REGISTRY\] identity=com\.guidexos\.apps\.managed\.calculator selector=6 display=Managed-Calculator native-calculator=preserved result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C156-MODIFIER-DECODE cases=10 .*result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C156-SHORTCUT-ROUTING cases=15 .*result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C150-MANAGED-OUTPUT\] C150-MANAGED-LIFECYCLE-TESTS cases=8 coexist=true capacity=3 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C150-RETURN-TARGET-TESTS\] cases=10 capacity=1 identity=canonical self=reject invalid=reject result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C157-NEW-TESTS cases=\d+ result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C154-REGRESSIONS clipboard=PASS result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C151-REGRESSIONS C145=49/49 C151-core=10/10 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-REGRESSIONS save-dialog=16/16 document-state=10/10 result=PASS' 0 $TimeoutSeconds)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C137-TESTS transport=12 text-area=16 list-box=18 cases=46 result=PASS' 0 $TimeoutSeconds)
        if ($PhaseC160) {
            [void](Wait-Serial $serial '^\[C160-APP-IDENTITY-TESTS\] cases=[0-9A-Fa-f]+ max-live=16 reuse=distinct wrap=fail-closed result=PASS' 0 $TimeoutSeconds)
            [void](Wait-Serial $serial '^\[C160-APPMANAGER-PROOF-APPS\] calculator=PASS taskmanager=PASS recentWrites=none result=PASS' 0 $TimeoutSeconds)
            [void](Wait-Serial $serial '^\[C160-TASKMANAGER-REGRESSION\] renderedRows=[0-9A-Fa-f]+ nativeCalculatorRow=true result=PASS' 0 $TimeoutSeconds)
            [void](Wait-Serial $serial '^\[C160-NATIVE-SNAPSHOT-TESTS\] cases=[0-9A-Fa-f]+ stress=1000 failed=[0-9A-Fa-f]{8}:[0-9A-Fa-f]{8} real-records=PASS identity=PASS bounded=PASS read-only=PASS result=PASS' 0 $TimeoutSeconds)
            [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C160-MANAGED-SNAPSHOT-TESTS cases=[0-9]+ v1=NotSupported malformed=Rejected layout=PASS identity=PASS stress=1000 result=PASS' 0 $TimeoutSeconds)
            [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C160-SNAPSHOT appId=com\.guidexos\.apps\.managed\.notes source=3 instance=[0-9]+ count=[0-9]+ active=1 previous=none result=PASS' 0 $TimeoutSeconds)
        }
        if ($PhaseC164) {
            $associationTests = Wait-Serial $serial '^\[C164-FILE-ASSOCIATION-TESTS\] cases=([0-9A-Fa-f]{8}) failed=([0-9A-Fa-f]{8}) used=00000001 capacity=00000010 bytes=([0-9A-Fa-f]{8}) resolver-stress=1000 result=(PASS|FAIL)' 0 $TimeoutSeconds
            $script:c164AssociationCases = [Convert]::ToInt32(
                $associationTests.Match.Groups[1].Value, 16)
            $script:c164AssociationTableBytes = [Convert]::ToInt32(
                $associationTests.Match.Groups[3].Value, 16)
            if ($script:c164AssociationCases -lt 10 -or
                $script:c164AssociationTableBytes -le 0 -or
                $associationTests.Match.Groups[2].Value -ne '00000000' -or
                $associationTests.Match.Groups[4].Value -ne 'PASS') {
                throw 'C164 fixed association table or resolver suite did not pass its bounds and stress checks.'
            }
        }
        if ($PhaseC161) {
            if ($PhaseC163) {
                [void](Wait-Serial $serial '^\[C163-TM-REGISTRY\] identity=com\.guidexos\.apps\.managed\.taskmanager selector=7 catalog=8 display=Managed-Task-Manager native-taskmanager=preserved native-calculator=preserved managed-calculator=distinct result=PASS' 0 $TimeoutSeconds)
                [void](Wait-Serial $serial '^\[C163-FILE-EXPLORER-REGISTRY\] managed=com\.guidexos\.apps\.managed\.fileexplorer selector=8 catalog=8 native=gxos\.builtin\.fileexplorer distinct=true result=PASS' 0 30)
                [void](Wait-Serial $serial '^\[C163-LAUNCHER-COUNTS\] catalog=00000008 startEntries=00000012 pinned=00000011 allPrograms=00000015 native-taskmanager=preserved result=PASS' 0 30)
            } else {
                [void](Wait-Serial $serial '^\[C161-TM-REGISTRY\] identity=com\.guidexos\.apps\.managed\.taskmanager selector=7 catalog=7 display=Managed-Task-Manager native-taskmanager=preserved native-calculator=preserved managed-calculator=distinct result=PASS' 0 $TimeoutSeconds)
                [void](Wait-Serial $serial '^\[C161-LAUNCHER-COUNTS\] catalog=00000007 startEntries=00000011 pinned=00000010 allPrograms=00000014 native-taskmanager=preserved result=PASS' 0 30)
            }
        }
        $screen = Get-Serial $serial
        $widthMatch = [regex]::Match($screen, '(?m)^\[DESKTOP CAP\] framebuffer_width=0x([0-9A-Fa-f]+)')
        $heightMatch = [regex]::Match($screen, '(?m)^\[DESKTOP CAP\] framebuffer_height=0x([0-9A-Fa-f]+)')
        if (-not $widthMatch.Success -or -not $heightMatch.Success) { throw 'C158 could not read the real desktop dimensions.' }
        $screenWidth = [Convert]::ToInt32($widthMatch.Groups[1].Value, 16)
        $screenHeight = [Convert]::ToInt32($heightMatch.Groups[1].Value, 16)
        $calibrationStart = (Get-Serial $serial).Length
        Send-QmpEvents (New-RelativeMove 1 1) 50
        $calibrated = Wait-Serial $serial '^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS' $calibrationStart 20
        $notesClientPoint = Get-ClientPoint $calibrated.Text
        # C138 reports client-local pointer coordinates. Translate them once to
        # framebuffer coordinates so later moves may cross out of the Notes
        # window to the Start menu and back into the centered Calculator.
        $notesWindowX = [Math]::Max(0, [int](($screenWidth - 600) / 2))
        $notesWindowY = [Math]::Max(0, [int](($screenHeight - 360) / 2))
        $script:cursor = [pscustomobject]@{
            X = $notesClientPoint.X + $notesWindowX
            Y = $notesClientPoint.Y + $notesWindowY + 24
        }
        Write-Host ("C158 production boot {0}/3: launching Calculator through Start menu." -f $Number)
        Send-CalculatorLaunch $true $screenWidth $screenHeight $serial
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-CORE cases=\d+ state-bytes=21 result=PASS' 0 30)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-UI cases=\d+ pointer=PASS keyboard=PASS focus=PASS exact-once=PASS result=PASS' 0 30)
        $registryEntriesExpected = if ($PhaseC163) { 5 } elseif ($PhaseC161) { 4 } else { 3 }
        [void](Wait-Serial $serial ('^\[C102-MANAGED-OUTPUT\] C158-CALC-REGISTRY cases=\d+ entries={0} controls=18 cap=18 teardown=PASS relaunch=PASS result=PASS' -f $registryEntriesExpected) 0 30)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-LIFECYCLE cycles=25 fresh=PASS one-surface=PASS registration=PASS capture=none clipboard=unchanged result=PASS' 0 30)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-STRESS commands=\d+ bounded=PASS no-wrap=PASS result=PASS' 0 30)
        [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-FOCUS events=\d+ index=valid exact-once=PASS modifiers=clear result=PASS' 0 30)

        if ($Scenario -eq 'pointer-arithmetic') {
            Click-CalculatorButton 12 $screenWidth $screenHeight $serial '1'
            Click-CalculatorButton 13 $screenWidth $screenHeight $serial '12'
            Click-CalculatorButton 15 $screenWidth $screenHeight $serial '12'
            Click-CalculatorButton 14 $screenWidth $screenHeight $serial '3'
            Click-CalculatorButton 8 $screenWidth $screenHeight $serial '34'
            Click-CalculatorButton 17 $screenWidth $screenHeight $serial '46'
            Click-CalculatorButton 0 $screenWidth $screenHeight $serial '0'
            Click-CalculatorButton 10 $screenWidth $screenHeight $serial '6'
            Click-CalculatorButton 7 $screenWidth $screenHeight $serial '6'
            Click-CalculatorButton 4 $screenWidth $screenHeight $serial '7'
            Click-CalculatorButton 17 $screenWidth $screenHeight $serial '42'
            Switch-CalculatorToNotes $screenWidth $screenHeight $serial
        } elseif ($Scenario -eq 'keyboard-focus') {
            $before = (Get-Serial $serial).Length
            Press-Key '7'; Send-QmpEvents @((New-Key 'shift' $true),
                (New-Key '8' $true), (New-Key '8' $false), (New-Key 'shift' $false)) 110
            Press-Key '8'; Press-Key 'ret'
            [void](Wait-Display $serial '56' $before 20)
            $before = (Get-Serial $serial).Length
            Press-Key 'esc'
            [void](Wait-Display $serial '0' $before 12)
            $before = (Get-Serial $serial).Length
            Press-Key '9'; Press-Key 'slash'; Press-Key '2'; Press-Key 'ret'
            [void](Wait-Display $serial '4' $before 20)
            $before = (Get-Serial $serial).Length
            Press-Key '1'; Press-Key '2'; Press-Key '3'; Press-Key 'backspace'
            [void](Wait-Display $serial '12' $before 20)
            Click-CalculatorButton 2 $screenWidth $screenHeight $serial '-12'
            $before = (Get-Serial $serial).Length
            Press-Key 'tab'
            [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-FOCUS active=4 shift=false result=PASS' $before 12)
            $before = (Get-Serial $serial).Length
            Send-QmpEvents @((New-Key 'shift' $true), (New-Key 'tab' $true),
                (New-Key 'tab' $false), (New-Key 'shift' $false)) 110
            [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C158-CALC-FOCUS active=3 shift=true result=PASS' $before 12)
            $before = (Get-Serial $serial).Length
            Press-Key 'spc'
            [void](Wait-Display $serial '12' $before 12)
            # C156 Control+9 is transported but has no Calculator command.
            $before = (Get-Serial $serial).Length
            Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key '9' $true),
                (New-Key '9' $false), (New-Key 'ctrl' $false)) 110
            Start-Sleep -Milliseconds 250
            $tail = (Get-Serial $serial).Substring($before)
            if ($tail -notmatch '(?m)^\[C156-NATIVE-INPUT\] kind=key-down code=00000039 control=1' -or
                $tail -match '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE ') {
                throw 'Unsupported Ctrl+9 produced a Calculator command or was not transported.'
            }
            Close-Calculator $serial
        } elseif ($Scenario -eq 'error-recovery-lifecycle') {
            $before = (Get-Serial $serial).Length
            Press-Key '7'; Press-Key 'slash'; Press-Key '0'; Press-Key 'ret'
            [void](Wait-Display $serial 'Divide by zero' $before 20)
            $before = (Get-Serial $serial).Length
            Press-Key '3'
            [void](Wait-Display $serial '3' $before 12)
            $before = (Get-Serial $serial).Length
            Press-Key 'esc'; Send-Text '9223372036854775807'
            Send-ShiftedPlus; Press-Key '1'; Press-Key 'ret'
            [void](Wait-Display $serial 'Overflow' $before 30)
            # Keep Equals focused for the keyboard Enter check below; boot 1
            # already exercises the pointer Clear button and its focus change.
            $before = (Get-Serial $serial).Length
            Press-Key 'esc'
            [void](Wait-Display $serial '0' $before 12)
            $before = (Get-Serial $serial).Length
            Press-Key '2'; Send-ShiftedPlus; Press-Key '3'; Press-Key 'ret'
            [void](Wait-Display $serial '5' $before 20)
            Close-Calculator $serial
            Send-CalculatorLaunch $false $screenWidth $screenHeight $serial

            if ($PhaseC163) {
                # C162's accepted manifest already records its 25-cycle
                # Calculator lifecycle stress. Keep this C163 coexistence boot
                # to the requested live Calculator smoke so its bounded heap
                # remains available for File Explorer's 25 VFS browse/close
                # cycles below.
                $before = (Get-Serial $serial).Length
                Press-Key '7'
                Send-QmpEvents @((New-Key 'shift' $true), (New-Key '8' $true),
                    (New-Key '8' $false), (New-Key 'shift' $false)) 90
                Press-Key '8'; Press-Key 'ret'
                [void](Wait-Display $serial '56' $before 20)
            } else {
                # Twenty-five real launch/use/close cycles. Each new object
                # begins with 18 registrations, zero state, and one window.
                for ($cycle = 1; $cycle -le 25; $cycle++) {
                    $digit = [string](($cycle - 1) % 9 + 1)
                    $before = (Get-Serial $serial).Length
                    Press-Key $digit
                    [void](Wait-Display $serial $digit $before 12)
                    Close-Calculator $serial
                    if ($cycle -lt 25) {
                        Send-CalculatorLaunch $false $screenWidth $screenHeight $serial
                    }
                }
                Send-CalculatorLaunch $false $screenWidth $screenHeight $serial
                $full = Get-Serial $serial
                $launchCount = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator ').Count
                $closeCount = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 ').Count
                if ($launchCount -lt 27 -or $closeCount -lt 26) {
                    throw "C158 actual lifecycle stress was incomplete: launches=$launchCount closes=$closeCount."
                }
            }
        } else {
            throw "Unknown C158 scenario '$Scenario'."
        }

        if ($PhaseC161) {
            Write-Host ("{0} production boot {1}/3: exercising real Managed Task Manager lifecycle and input." -f $(if ($PhaseC162) { 'C162' } else { 'C161' }), $Number)
            $c161Scenario = Invoke-C161ProductionScenario $Number $screenWidth $screenHeight $serial
            $all = Get-Serial $serial
            $taskManagerTests = [regex]::Matches($all,
                '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-TESTS cases=(\d+) initial=PASS selection=identity stress=1000 result=PASS')
            $taskManagerRefreshes = [regex]::Matches($all,
                '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R [^\r\n]*result=PASS')
            $taskManagerLaunches = [regex]::Matches($all,
                '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-LAUNCH id=7 reg=\d+ ctr=4 cap=4 max=20 [^\r\n]*result=PASS')
            $taskManagerCloses = [regex]::Matches($all,
                '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-CLOSE controls=0 selection=none result=PASS')
            $taskManagerClipboard = [regex]::Matches($all,
                '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-CLIPBOARD stage=launch preserved=true result=PASS')
            if ($taskManagerTests.Count -ne 1 -or
                [int]$taskManagerTests[0].Groups[1].Value -lt 20 -or
                $taskManagerLaunches.Count -lt $c161Scenario.LaunchCount -or
                $taskManagerClipboard.Count -lt $c161Scenario.LaunchCount -or
                $taskManagerCloses.Count -lt $c161Scenario.CloseCount -or
                $taskManagerRefreshes.Count -lt 1) {
                throw "C161 $Scenario proof lacks focused, registration, refresh, or lifecycle evidence."
            }
            if ($Number -eq 3 -and -not $PhaseC163 -and $taskManagerRefreshes.Count -lt 100) {
                throw "C161 boot 3 performed only $($taskManagerRefreshes.Count) Ctrl+R refreshes; 100 are required."
            }
            if ($all -match '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-[^\r\n]*result=FAIL' -or
                $all -match '(?m)^\[C161-TM-[^\r\n]*result=FAIL' -or
                $all -match '(?m)^\[C162-[^\r\n]*result=FAIL') {
                throw "C161 $Scenario boot reported failed Task Manager evidence."
            }
            if ($PhaseC162 -and
                ($all -notmatch '(?m)^\[C162-APP-CLOSE-TESTS\] cases=[0-9A-F]{8} [^\r\n]*result=PASS' -or
                 $all -notmatch '(?m)^\[C162-NATIVE-CALCULATOR\] close=relaunch identities=distinct operation=7\*8=56 result=PASS' -or
                 $all -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C162-CLOSE-ABI cases=18 v2=NotSupported identity=source\+u64 result=PASS' -or
                 $all -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C162-CLOSE-NATIVE-BOUNDARY cases=3 self=Protected shell=Protected unknown=NotFound result=PASS')) {
                throw "C162 $Scenario boot lacks native close and ABI boundary proof."
            }
        }
        $c163Scenario = $null
        if ($PhaseC163) {
            Write-Host ("C163 production boot {0}/3: exercising real VFS navigation, refresh, and coexistence." -f $Number)
            $c163Scenario = Invoke-C163ManagedFileExplorerScenario $Number $screenWidth $screenHeight $serial
            $all = Get-Serial $serial
            if ($Number -eq 1) {
                $focusedExplorerTests = [regex]::Match($all,
                    '(?m)^\[C102-MANAGED-OUTPUT\] C163-FILE-EXPLORER-TESTS cases=(\d+) refresh=1000 navigation=100 bounded=true result=PASS')
                if (-not $focusedExplorerTests.Success -or
                    [int]$focusedExplorerTests.Groups[1].Value -lt 25) {
                    throw 'C163 focused VFS/browser suite did not pass at 25 or more cases.'
                }
            }
            if ($Number -eq 3) {
                $explorerLaunches = [regex]::Matches($all,
                    '(?m)^\[C102-MANAGED-OUTPUT\] C163-EXPLORER id=com\.guidexos\.apps\.managed\.fileexplorer sel=8 path=/system/apps n=\d+ cap=64 ctl=5 abi=3/120 ro=1 result=PASS')
                $explorerCloses = [regex]::Matches($all,
                    '(?m)^\[C102-MANAGED-OUTPUT\] C163-FILE-EXPLORER-CLOSE selector=8 controls=0 fresh-path=/system/apps result=PASS')
                $uniqueExplorerLifetimes = [System.Collections.Generic.HashSet[string]]::new(
                    [System.StringComparer]::Ordinal)
                foreach ($explorerLaunch in [regex]::Matches($all,
                    '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.fileexplorer generation=([0-9A-Fa-f]+) selector=00000008 kind=normal')) {
                    [void]$uniqueExplorerLifetimes.Add($explorerLaunch.Groups[1].Value)
                }
                $observedExplorer = $all -match ('(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-ID 3:[0-9A-F]{16}=com\.guidexos\.apps\.managed\.fileexplorer\r?$')
                $nativeExplorerClosed = $all -match ('(?m)^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=1:{0} result=0 fresh=true remains=false result=PASS' -f [regex]::Escape($c163Scenario.NativeFileExplorerIdentity))
                $clipboardPreserved = $all -match '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-CLIPBOARD stage=launch preserved=true result=PASS'
                if ($explorerLaunches.Count -lt 26 -or $explorerCloses.Count -lt 25 -or
                    $uniqueExplorerLifetimes.Count -lt 26 -or -not $observedExplorer -or
                    -not $nativeExplorerClosed -or -not $clipboardPreserved) {
                    throw "C163 boot 3 lacks 25 unique browse/close lifetimes, Task Manager observation, native File Explorer, or clipboard evidence (launch=$($explorerLaunches.Count), close=$($explorerCloses.Count), unique=$($uniqueExplorerLifetimes.Count))."
                }
            }
            if ($all -match '(?m)^\[C102-MANAGED-OUTPUT\] C163-FILE-EXPLORER-[^\r\n]*result=FAIL' -or
                $all -match '(?m)^\[C163-[^\r\n]*result=FAIL') {
                throw "C163 $Number production boot contains failed File Explorer evidence."
            }
        }
        if ($PhaseC164) {
            # Earlier proof suites may leave the live Notes document dirty.
            # Focus its real App Model surface and save through C152 so the
            # subsequent fresh document activation respects the normal close
            # veto and does not discard that document.
            Send-ManagedNotesLaunch $screenWidth $screenHeight $serial
            $cleanNotesStart = (Get-Serial $serial).Length
            Send-QmpEvents @((New-Key 'ctrl' $true), (New-Key 's' $true),
                (New-Key 's' $false), (New-Key 'ctrl' $false)) 90
            [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS chooser=open bounded=true result=PASS' $cleanNotesStart 15)
            # The regression setup starts Notes untitled. Use its existing
            # bounded Save As dialog and a fresh 8.3-compatible proof name.
            Press-Key 'tab'
            Press-Key 'end'
            for ($index = 0; $index -lt 12; $index++) { Press-Key 'backspace' }
            Send-Text 'c164a'
            Press-Key 'tab'
            $saveCommitStart = (Get-Serial $serial).Length
            Press-Key 'ret'
            [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-SAVE-AS committed path=/system/apps/c164a\r?$' $saveCommitStart 20)
            [void](Wait-Serial $serial '^\[C102-MANAGED-OUTPUT\] C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false' $cleanNotesStart 25)
        }
        if ($PhaseC164) {
            Write-Host ("C164 production boot {0}/3: activating real VFS documents through Managed File Explorer." -f $Number)
            $c164Scenario = Invoke-C164ManagedFileActivationScenario $Number $screenWidth $screenHeight $serial
            $all = Get-Serial $serial
            $contextTests = [regex]::Match($all,
                '(?m)^\[C102-MANAGED-OUTPUT\] C164-ACTIVATION-CONTEXT-TESTS cases=(\d+) result=PASS')
            $notesStateTests = [regex]::Matches($all,
                '(?m)^\[C102-MANAGED-OUTPUT\] C164-NOTES-STATE-TESTS cases=(\d+) activation-clean=PASS dirty-undo-redo=PASS stress=25 result=PASS')
            if (-not $contextTests.Success -or [int]$contextTests.Groups[1].Value -lt 20) {
                throw 'C164 bounded activation-context suite did not pass at 20 or more cases.'
            }
            $script:c164ActivationContextCases = [int]$contextTests.Groups[1].Value
            if ($notesStateTests.Count -lt $c164Scenario.Activations -or
                [int]$notesStateTests[0].Groups[1].Value -lt 20) {
                throw 'C164 Notes activation/document suite is missing for one or more fresh activations.'
            }
            $script:c164NotesStateCases = [int]$notesStateTests[0].Groups[1].Value
            $documentActivations = [regex]::Matches($all,
                '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes generation=([0-9A-Fa-f]+) selector=00000004 kind=document\r?$')
            $sourceTransitions = [regex]::Matches($all,
                '(?m)^\[C164-ACTIVATION\] source=ManagedFileExplorer instance=([0-9A-Fa-f]+) target=com\.guidexos\.apps\.managed\.notes path=/system/apps/C164/[^ ]+ fresh=true source-closed=true result=PASS\r?$')
            $minimumActivations = [int]$c164Scenario.Activations
            if ($documentActivations.Count -lt $minimumActivations -or
                $sourceTransitions.Count -lt $minimumActivations) {
                throw "C164 boot $Number lacks fresh App Model activation/source-close evidence (launches=$($documentActivations.Count), transitions=$($sourceTransitions.Count), required=$minimumActivations)."
            }
            $distinctNotes = [System.Collections.Generic.HashSet[string]]::new(
                [System.StringComparer]::OrdinalIgnoreCase)
            foreach ($documentActivation in $documentActivations) {
                [void]$distinctNotes.Add($documentActivation.Groups[1].Value)
            }
            if ($distinctNotes.Count -lt $minimumActivations) {
                throw "C164 boot $Number reused a Notes application lifetime across document activations."
            }
            if ($Number -eq 1 -and
                ($c164Scenario.FinalPath -ne '/system/apps/C164/hello.txt' -or
                 $all -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C164-DOC event=input path=/system/apps/C164/hello\.txt len=16 dirty=1 undo=1 redo=0 caret=16 anchor=16 view=0 result=PASS' -or
                 $all -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C164-DOC event=shortcut path=/system/apps/C164/hello\.txt len=15 dirty=0 undo=0 redo=1 caret=15 anchor=15 view=0 result=PASS')) {
                throw 'C164 boot 1 did not prove real hello.txt activation, clean baseline, edit, and Undo restoration.'
            }
            if ($Number -eq 2 -and
                (-not $c164Scenario.Save -or -not $c164Scenario.Unsupported -or
                 $all -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C164-FILE-ACTIVATION path=/system/apps/C164/noapp\.bin result=unsupported source-retained=true selection=preserved')) {
                throw 'C164 boot 2 did not prove save/reopen and safe unsupported-file handling.'
            }
            if ($Number -eq 3 -and
                ($c164Scenario.DistinctNotes -lt 25 -or
                 $c164Scenario.DistinctExplorers -lt 25 -or
                 -not $c164Scenario.Precedence)) {
                throw 'C164 boot 3 did not prove precedence and 25 distinct File Explorer/Notes activation lifetimes.'
            }
            if ($all -match '(?m)^\[C102-MANAGED-OUTPUT\] C164-[^\r\n]*result=FAIL' -or
                $all -match '(?m)^\[C164-[^\r\n]*result=FAIL') {
                throw "C164 production boot $Number contains failed file activation evidence."
            }
        }

        $full = Get-Serial $serial
        if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-[^\r\n]*result=FAIL' -or
            $full -match '(?m)^\[C158-CALC-APP-REGISTRY\][^\r\n]*result=FAIL') {
            throw "C158 $Scenario boot contains failed Calculator proof evidence."
        }
        if ($PhaseC160) {
            if ($full -match '(?m)^\[C102-MANAGED-OUTPUT\] C160-(?:MANAGED-SNAPSHOT-TESTS|SNAPSHOT)[^\r\n]*result=FAIL' -or
                $full -match '(?m)^\[C160-(?:APP-IDENTITY-TESTS|APPMANAGER-PROOF-APPS|TASKMANAGER-REGRESSION|NATIVE-SNAPSHOT-TESTS)[^\r\n]*result=FAIL') {
                throw "C160 $Scenario boot contains failed identity, snapshot, or Task Manager evidence."
            }
            $noteSnapshots = [regex]::Matches($full,
                '(?m)^\[C102-MANAGED-OUTPUT\] C160-SNAPSHOT appId=com\.guidexos\.apps\.managed\.notes source=3 instance=[0-9]+ count=[0-9]+ active=1 [^\r\n]*result=PASS')
            $calculatorSnapshots = [regex]::Matches($full,
                '(?m)^\[C102-MANAGED-OUTPUT\] C160-SNAPSHOT appId=com\.guidexos\.apps\.managed\.calculator source=3 instance=[0-9]+ count=[0-9]+ active=1 [^\r\n]*result=PASS')
            if ($noteSnapshots.Count -lt 1 -or $calculatorSnapshots.Count -lt 1) {
                throw "C160 $Scenario boot did not capture active Notes and Calculator lifetimes."
            }
            if ($Number -eq 1 -and $PhaseC162 -and
                $full -notmatch '(?m)^\[C162-MANAGED-FOCUS\] appId=com\.guidexos\.apps\.managed\.notes source=3 instance=[0-9A-Fa-f]{16} selector=00000004 existing=true result=PASS') {
                throw 'C162 boot 1 did not verify Notes stayed live and was focused again after Calculator use.'
            }
            if ($Number -eq 1 -and -not $PhaseC162 -and $noteSnapshots.Count -lt 2) {
                throw 'C160 boot 1 did not capture the Notes lifetime both before and after the Calculator transition.'
            }
            if ($Number -eq 3 -and -not $PhaseC161 -and
                $calculatorSnapshots.Count -lt 27) {
                throw "C160 boot 3 did not verify a new snapshot identity for each lifecycle cycle: $($calculatorSnapshots.Count)."
            }
        }
        # Serial writers can append keyboard markers to desktop output lines.
        # Count marker tokens independently of line starts.
        $controlDown = [regex]::Matches($full, '\[C156-KEYBOARD\] event=control-left-down ').Count
        $controlUp = [regex]::Matches($full, '\[C156-KEYBOARD\] event=control-left-up ').Count
        $shiftDown = [regex]::Matches($full, '\[C129-KEYBOARD\] shift=down side=left ').Count
        $shiftUp = [regex]::Matches($full, '\[C129-KEYBOARD\] shift=up side=left ').Count
        if ($controlDown -ne $controlUp -or $shiftDown -ne $shiftUp) {
            throw "C158 $Scenario boot left a keyboard modifier unbalanced."
        }
        Stop-Qemu $session.Port $process $session.QmpLog
        $process.Refresh()
        $result = [pscustomobject]@{
            Boot = $Number
            Scenario = $Scenario
            Status = 'PASS'
            QemuAccelerator = if ($PhaseC160) { 'whpx' } else { 'tcg,thread=single' }
            SerialPath = $serial
            SerialSha256 = Get-Hash $serial
            ProofKernelSha256 = Get-Hash $proofKernel
            ProofRamdiskSha256 = Get-Hash $proofRamdisk
            CalculatorLaunches = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator ').Count
            CalculatorCloses = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 ').Count
            TaskManagerScenario = $c161Scenario
            TaskManagerLaunches = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-LAUNCH id=7 ').Count
            TaskManagerCloses = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-CLOSE controls=0 ').Count
            TaskManagerRefreshes = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R ').Count
            FileExplorerScenario = $c163Scenario
            FileExplorerLaunches = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C163-EXPLORER ').Count
            FileExplorerCloses = [regex]::Matches($full, '(?m)^\[C102-MANAGED-OUTPUT\] C163-FILE-EXPLORER-CLOSE ').Count
            FileActivationScenario = $c164Scenario
            FileActivationTransitions = [regex]::Matches($full, '(?m)^\[C164-ACTIVATION\] source=ManagedFileExplorer instance=').Count
            ControlDown = $controlDown
            ControlUp = $controlUp
            ShiftDown = $shiftDown
            ShiftUp = $shiftUp
            ExitCode = $process.ExitCode
        }
        Remove-Item -LiteralPath $esp -Recurse -Force
        return $result
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
        $script:activeProcess = $null
        $script:activeStderr = $null
        $script:activeSerial = $null
        $script:activePort = 0
        $script:activeQmpLog = $null
    }
}

function Invoke-OrdinaryBoot([int]$Number, [string]$Qemu, [string]$Ovmf) {
    $root = Join-Path $EvidenceRoot ("ordinary-boot-{0:D2}" -f $Number)
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $esp = Join-Path $root 'ESP'
    Stage-Esp $esp $ordinaryKernelSource $ordinaryRamdiskSource
    $serial = Join-Path $root 'serial.log'
    $session = Start-Qemu $serial (Join-Path $root 'qemu.stdout.log') `
        (Join-Path $root 'qemu.stderr.log') $esp $Qemu $Ovmf
    $process = $session.Process
    $ordinaryTaskManagerId = $null
    $ordinaryFileExplorerId = $null
    try {
        [void](Wait-Serial $serial '^\[desktop\] bare-metal desktop icon init completed' 0 $TimeoutSeconds)
        if ($PhaseC161) {
            [void](Wait-Serial $serial '^\[NATIVEAOT-PRODUCTION-LAUNCH\] applicationId=com\.guidexos\.apps\.managed\.notes recordId=com\.guidexos\.apps\.managed\.notes image=/system/apps/GXOSAPP\.ELF selector=00000004' 0 $TimeoutSeconds)
            if ($PhaseC163) {
                [void](Wait-Serial $serial '^\[C163-TM-REGISTRY\] identity=com\.guidexos\.apps\.managed\.taskmanager selector=7 catalog=8 display=Managed-Task-Manager native-taskmanager=preserved native-calculator=preserved managed-calculator=distinct result=PASS' 0 30)
            } else {
                [void](Wait-Serial $serial '^\[C161-TM-REGISTRY\] identity=com\.guidexos\.apps\.managed\.taskmanager selector=7 catalog=7 display=Managed-Task-Manager native-taskmanager=preserved native-calculator=preserved managed-calculator=distinct result=PASS' 0 30)
            }
            $screen = Get-Serial $serial
            $widthMatch = [regex]::Match($screen, '(?m)^\[DESKTOP CAP\] framebuffer_width=0x([0-9A-Fa-f]+)')
            $heightMatch = [regex]::Match($screen, '(?m)^\[DESKTOP CAP\] framebuffer_height=0x([0-9A-Fa-f]+)')
            if (-not $widthMatch.Success -or -not $heightMatch.Success) {
                throw 'C161 ordinary boot could not read the production desktop dimensions.'
            }
            $screenWidth = [Convert]::ToInt32($widthMatch.Groups[1].Value, 16)
            $screenHeight = [Convert]::ToInt32($heightMatch.Groups[1].Value, 16)
            $calibrationStart = (Get-Serial $serial).Length
            Send-QmpEvents (New-RelativeMove 1 1) 50
            $calibrated = Wait-Serial $serial '^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS' $calibrationStart 20
            $notesPoint = Get-ClientPoint $calibrated.Text
            $notesWindowX = [Math]::Max(0, [int](($screenWidth - 600) / 2))
            $notesWindowY = [Math]::Max(0, [int](($screenHeight - 360) / 2))
            $script:cursor = [pscustomobject]@{
                X = $notesPoint.X + $notesWindowX
                Y = $notesPoint.Y + $notesWindowY + 24
            }
            $ordinaryTaskManagerId = Launch-ManagedTaskManager $screenWidth $screenHeight $serial
            if ($PhaseC163) {
                $ordinaryFileExplorerId = Launch-ManagedFileExplorer $screenWidth $screenHeight $serial
            }
        }
        $text = Get-Serial $serial
        if ($text -notmatch '(?m)^\[KERNEL\] Boot method: UEFI BootInfo' -or
            $text -match 'PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure|C158-CALC-(?!APP-REGISTRY\][^\r\n]*result=PASS)|C160-(?:APP-IDENTITY|NATIVE-SNAPSHOT|MANAGED-SNAPSHOT|SNAPSHOT|TASKMANAGER|APPMANAGER)') {
            throw "Ordinary boot $Number failed or contains phase proof output."
        }
        if ($PhaseC161 -and ($text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-LAUNCH id=7 reg=\d+ ctr=4 cap=4 max=20 snap=\d+ n=\d+ self=\d+ active=1 result=PASS' -or
                $text -match '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-TESTS ')) {
            throw "C161 ordinary boot $Number did not keep the production Task Manager active or included proof-only instrumentation."
        }
        if ($PhaseC163 -and ($text -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C163-EXPLORER id=com\.guidexos\.apps\.managed\.fileexplorer sel=8 path=/system/apps n=\d+ cap=64 ctl=5 abi=3/120 ro=1 result=PASS' -or
                $text -match '(?m)^\[C102-MANAGED-OUTPUT\] C163-FILE-EXPLORER-TESTS ' -or
                $text -match '(?m)^\[C102-MANAGED-OUTPUT\] C163-STATE ' -or
                $text -match '(?m)^\[C102-MANAGED-OUTPUT\] C160-MANAGED-SNAPSHOT-TESTS ' -or
                $text -match '(?m)^\[C102-MANAGED-OUTPUT\] C162-CLOSE-ABI ')) {
            throw "C163 ordinary boot $Number did not launch the clean browser or retained proof-only managed hooks."
        }
        Stop-Qemu $session.Port $process $session.QmpLog
        $process.Refresh()
        $result = [pscustomobject]@{
            Boot = $Number
            Status = 'PASS'
            SerialPath = $serial
            SerialSha256 = Get-Hash $serial
            KernelSha256 = Get-Hash (Join-Path $esp 'kernel.elf')
            RamdiskSha256 = Get-Hash (Join-Path $esp 'ramdisk.img')
            TaskManagerLifetimeId = $ordinaryTaskManagerId
            FileExplorerLifetimeId = $ordinaryFileExplorerId
        ActiveApplication = if ($PhaseC163) { 'com.guidexos.apps.managed.fileexplorer' } elseif ($PhaseC161) { 'com.guidexos.apps.managed.taskmanager' } else { $null }
            ExitCode = $process.ExitCode
        }
        Remove-Item -LiteralPath $esp -Recurse -Force
        return $result
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
        $script:activeProcess = $null
        $script:activeStderr = $null
        $script:activeSerial = $null
        $script:activePort = 0
        $script:activeQmpLog = $null
    }
}

function Restore-CanonicalFiles {
    if ($proofBackup -and (Test-Path -LiteralPath $proofBackup -PathType Leaf)) {
        Copy-Item -LiteralPath $proofBackup -Destination $kernelPath -Force
    }
    if ($espKernelBackup -and (Test-Path -LiteralPath $espKernelBackup -PathType Leaf)) {
        Copy-Item -LiteralPath $espKernelBackup -Destination $espKernelPath -Force
    }
    if ($ramdiskBackup -and (Test-Path -LiteralPath $ramdiskBackup -PathType Leaf)) {
        Copy-Item -LiteralPath $ramdiskBackup -Destination $protectedRamdiskPath -Force
    }
    if ($proofBackup -and (Test-Path -LiteralPath $proofBackup -PathType Leaf) -and
        $espKernelBackup -and (Test-Path -LiteralPath $espKernelBackup -PathType Leaf) -and
        $ramdiskBackup -and (Test-Path -LiteralPath $ramdiskBackup -PathType Leaf)) {
        $script:restored = (Get-Hash $kernelPath) -eq $canonicalKernelHash -and
            (Get-Hash $espKernelPath) -eq $espKernelHash -and
            (Get-Hash $protectedRamdiskPath) -eq $ramdiskHash
    }
}

function Install-C160CanonicalProducts([string]$Python) {
    $canonicalStageRoot = Join-Path $EvidenceRoot $(if ($PhaseC164) { 'staging\wallpaper-pack-c164-production' } elseif ($PhaseC163) { 'staging\wallpaper-pack-c163-production' } elseif ($PhaseC162) { 'staging\wallpaper-pack-c162-production' } elseif ($PhaseC161) { 'staging\wallpaper-pack-c161-production' } else { 'staging\wallpaper-pack-c160-production' })
    $canonicalCompositeElfPath = Join-Path $canonicalCompositeRoot 'artifacts\HostLogProof.elf'
    $managedBuildArguments = @('-ExecutionPolicy','Bypass','-File',$managedBuild,
        '-RepoRoot',$RepoRoot,'-OutputRoot',$canonicalCompositeRoot,
        '-RuntimePackRoot',(Join-Path $RepoRoot 'tools\dotnet\runtime-pack'),
        '-RuntimePackOutputRoot',$runtimePackOutput,'-UseGuideXosRuntimePack',
        '-ProductionApplication','-PersistentCompositeLifecycle','-AllocationMode','Allocating',
        '-ManagedProjectMode','C160Composite','-C155ManagedNotesSession',
        '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument',
        '-C158ManagedCalculator','-HeapConfiguration',$(if ($PhaseC164) { 'Primary8MiB' } else { 'Primary4MiB' }),'-PythonExe',$Python)
    if ($PhaseC161) { $managedBuildArguments += '-C161ManagedTaskManager' }
    if ($PhaseC162) { $managedBuildArguments += '-C162ManagedTaskManagerClose' }
    if ($PhaseC163) { $managedBuildArguments += '-C163ManagedFileExplorer' }
    Invoke-Checked 'powershell' $managedBuildArguments
    if (-not (Test-Path -LiteralPath $canonicalCompositeElfPath -PathType Leaf)) {
        throw "Canonical NativeAOT composite missing: $canonicalCompositeElfPath"
    }
    $script:canonicalCompositeElf = $canonicalCompositeElfPath
    $script:canonicalCompositeHash = Get-Hash $canonicalCompositeElfPath
    $generator = Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
    Invoke-Checked 'powershell' @('-ExecutionPolicy','Bypass','-File',$generator,
        '-OutputDir',$canonicalStageRoot,'-OutputImage',$canonicalRamdisk,
        '-C104AppAPath',$canonicalCompositeElfPath,
        '-ProductionCompositeApplicationPath',$canonicalCompositeElfPath,
        '-C114ManagedDirectoryServices','-C117ManagedTextArea','-C118ManagedListBox',
        '-C152ManagedNotesSaveWorkflow',
        '-C155ManagedNotesSession','-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument')
    if (-not (Test-Path -LiteralPath $canonicalRamdisk -PathType Leaf)) {
        throw 'Canonical production ramdisk was not generated.'
    }
    Invoke-Checked $make @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',
        "EXTRA_CFLAGS=$canonicalKernelFlags",'-B')
    if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) {
        throw 'Canonical kernel build did not produce kernel.elf.'
    }
    $script:postC160KernelHash = Get-Hash $kernelPath
    Copy-Item -LiteralPath $kernelPath -Destination $espKernelPath -Force
    Copy-Item -LiteralPath $canonicalRamdisk -Destination $protectedRamdiskPath -Force
    $script:postC160RamdiskHash = Get-Hash $protectedRamdiskPath
    $script:ordinaryKernelSource = $kernelPath
    $script:ordinaryRamdiskSource = $protectedRamdiskPath
    $script:ordinaryKernelHash = $script:postC160KernelHash
    $script:ordinaryRamdiskHash = $script:postC160RamdiskHash
    $script:restored = (Get-Hash $espKernelPath) -eq $script:postC160KernelHash -and
        (Get-Hash $protectedRamdiskPath) -eq $script:postC160RamdiskHash
    if (-not $script:restored) {
        throw 'Post-phase kernel/ESP/ramdisk hashes do not match the source build.'
    }
}

function New-C151SettingsRecord([string]$Path) {
    [byte[]]$bytes = [byte[]]::new(26)
    [byte[]]$magic = [System.Text.Encoding]::ASCII.GetBytes('GXSC')
    [Array]::Copy($magic, 0, $bytes, 0, 4)
    $bytes[4] = 2; $bytes[6] = 10
    $bytes[12] = 0; $bytes[13] = 1; $bytes[14] = 0; $bytes[15] = 1
    $bytes[16] = 1; $bytes[17] = 1; $bytes[18] = 1; $bytes[19] = 0
    $bytes[20] = 0; $bytes[21] = 5
    [long]$crc = 4294967295L
    foreach ($value in $bytes[0..21]) {
        $crc = ($crc -bxor [long]$value) -band 4294967295L
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 1) -ne 0) { $crc = (($crc -shr 1) -bxor 3988292384L) -band 4294967295L }
            else { $crc = ($crc -shr 1) -band 4294967295L }
        }
    }
    $crc = ($crc -bxor 4294967295L) -band 4294967295L
    for ($index = 0; $index -lt 4; $index++) {
        $bytes[22 + $index] = [byte](($crc -shr ($index * 8)) -band 255)
    }
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot, (Split-Path -Parent $proofBackup) | Out-Null
foreach ($path in @($kernelPath, $espKernelPath, $protectedRamdiskPath, $bootloaderPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "C158 required protected input is missing: $path" }
}
$canonicalKernelHash = Get-Hash $kernelPath
$espKernelHash = Get-Hash $espKernelPath
$ramdiskHash = Get-Hash $protectedRamdiskPath
if ($PhaseC161) {
    $sourceBranch = (& git -C $RepoRoot branch --show-current).Trim()
    if ($sourceBranch -ne 'v1.1_DOTNET_SUPPORT') {
        throw "C161 requires the audited v1.1_DOTNET_SUPPORT branch, found '$sourceBranch'."
    }
}
if ($PhaseC164) {
    $c163BaselineManifestPath = Join-Path $RepoRoot 'out\dotnet\c163-managed-file-explorer\c163-proof-manifest.json'
    if (-not (Test-Path -LiteralPath $c163BaselineManifestPath -PathType Leaf)) {
        throw "C164 requires the accepted C163 proof manifest: $c163BaselineManifestPath"
    }
    $c163BaselineManifest = Get-Content -LiteralPath $c163BaselineManifestPath -Raw | ConvertFrom-Json
    $c162BaselineManifestPath = [string]$c163BaselineManifest.c163Contract.c162BaselineManifest
    if ([string]::IsNullOrWhiteSpace($c162BaselineManifestPath) -or
        -not (Test-Path -LiteralPath $c162BaselineManifestPath -PathType Leaf)) {
        throw "C164 requires the C162 lineage manifest recorded by C163: $c162BaselineManifestPath"
    }
    $c163BootsPassed = @($c163BaselineManifest.productionBoots).Count -eq 3 -and
        @($c163BaselineManifest.productionBoots | Where-Object { $_.Status -ne 'PASS' }).Count -eq 0 -and
        @($c163BaselineManifest.ordinaryBoots).Count -eq 3 -and
        @($c163BaselineManifest.ordinaryBoots | Where-Object { $_.Status -ne 'PASS' }).Count -eq 0
    $expectedC163KernelHash = [string]$c163BaselineManifest.restoration.canonicalKernelAfter
    $expectedC163RamdiskHash = [string]$c163BaselineManifest.restoration.protectedRamdiskAfter
    if ($c163BaselineManifest.phase -ne 'C163' -or
        $c163BaselineManifest.outcome -ne 'A' -or
        $c163BaselineManifest.nativeAot.abiVersion -ne 3 -or
        $c163BaselineManifest.nativeAot.abiTableBytes -ne 120 -or
        -not $c163BaselineManifest.restoration.canonicalPostPhaseProductsVerified -or
        -not $c163BootsPassed -or
        $canonicalKernelHash -ne $expectedC163KernelHash -or
        $espKernelHash -ne $expectedC163KernelHash -or
        $ramdiskHash -ne $expectedC163RamdiskHash) {
        throw ("C164 starting products differ from C163: phase={0} outcome={1} abi={2}/{3} verified={4} boots={5} kernel={6}/{7} esp={8}/{7} ramdisk={9}/{10}" -f
            $c163BaselineManifest.phase, $c163BaselineManifest.outcome,
            $c163BaselineManifest.nativeAot.abiVersion,
            $c163BaselineManifest.nativeAot.abiTableBytes,
            $c163BaselineManifest.restoration.canonicalPostPhaseProductsVerified,
            $c163BootsPassed, $canonicalKernelHash, $expectedC163KernelHash,
            $espKernelHash, $ramdiskHash, $expectedC163RamdiskHash)
    }
} elseif ($PhaseC163) {
    $c162BaselineManifestPath = Join-Path $RepoRoot 'out\dotnet\c162-managed-task-manager-close\c162-proof-manifest.json'
    if (-not (Test-Path -LiteralPath $c162BaselineManifestPath -PathType Leaf)) {
        throw "C163 requires the accepted C162 baseline manifest: $c162BaselineManifestPath"
    }
    $c162BaselineManifest = Get-Content -LiteralPath $c162BaselineManifestPath -Raw | ConvertFrom-Json
    $c162BootsPassed = @($c162BaselineManifest.productionBoots).Count -eq 3 -and
        @($c162BaselineManifest.productionBoots | Where-Object { $_.Status -ne 'PASS' }).Count -eq 0 -and
        @($c162BaselineManifest.ordinaryBoots).Count -eq 3 -and
        @($c162BaselineManifest.ordinaryBoots | Where-Object { $_.Status -ne 'PASS' }).Count -eq 0
    $expectedC162KernelHash = [string]$c162BaselineManifest.restoration.canonicalKernelAfter
    $expectedC162RamdiskHash = [string]$c162BaselineManifest.restoration.protectedRamdiskAfter
    if ($c162BaselineManifest.phase -ne 'C162' -or
        $c162BaselineManifest.outcome -ne 'A' -or
        $c162BaselineManifest.nativeAot.abiVersion -ne 3 -or
        $c162BaselineManifest.nativeAot.abiTableBytes -ne 120 -or
        -not $c162BaselineManifest.restoration.canonicalPostPhaseProductsVerified -or
        -not $c162BootsPassed -or
        $canonicalKernelHash -ne $expectedC162KernelHash -or
        $espKernelHash -ne $expectedC162KernelHash -or
        $ramdiskHash -ne $expectedC162RamdiskHash) {
        throw 'C163 starting kernel/ESP/ramdisk do not match the completed C162 ABI-v3 manifest and its six passing boots.'
    }
} elseif ($PhaseC162) {
    $c161BaselineManifestPath = Join-Path $RepoRoot 'out\dotnet\c161-managed-task-manager\c161-proof-manifest.json'
    if (-not (Test-Path -LiteralPath $c161BaselineManifestPath -PathType Leaf)) {
        throw "C162 requires the accepted C161 baseline manifest: $c161BaselineManifestPath"
    }
    $c161BaselineManifest = Get-Content -LiteralPath $c161BaselineManifestPath -Raw | ConvertFrom-Json
    $c161BootsPassed = @($c161BaselineManifest.productionBoots).Count -eq 3 -and
        @($c161BaselineManifest.productionBoots | Where-Object { $_.Status -ne 'PASS' }).Count -eq 0 -and
        @($c161BaselineManifest.ordinaryBoots).Count -eq 3 -and
        @($c161BaselineManifest.ordinaryBoots | Where-Object { $_.Status -ne 'PASS' }).Count -eq 0
    $expectedC161KernelHash = [string]$c161BaselineManifest.restoration.canonicalKernelAfter
    $expectedC161RamdiskHash = [string]$c161BaselineManifest.restoration.protectedRamdiskAfter
    if ($c161BaselineManifest.phase -ne 'C161' -or
        $c161BaselineManifest.outcome -ne 'A' -or
        $c161BaselineManifest.nativeAot.abiVersion -ne 2 -or
        $c161BaselineManifest.nativeAot.abiTableBytes -ne 112 -or
        -not $c161BaselineManifest.restoration.canonicalPostPhaseProductsVerified -or
        -not $c161BootsPassed -or
        $canonicalKernelHash -ne $expectedC161KernelHash -or
        $espKernelHash -ne $expectedC161KernelHash -or
        $ramdiskHash -ne $expectedC161RamdiskHash) {
        throw 'C162 starting kernel/ESP/ramdisk do not match the completed C161 ABI-v2 manifest and its six passing boots.'
    }
} elseif ($PhaseC161) {
    if ($canonicalKernelHash -ne '0BA852C64EA33D30EAE57C5E612A25B8F4358FF85D460F23A402E41C5BD220F1' -or
        $espKernelHash -ne $canonicalKernelHash -or
        $ramdiskHash -ne '32B8D57DBD47E5DC9B95E97204A255D95593FCF4C38430C39CE812FA9F7B23D0') {
        throw 'C161 starting kernel/ESP/ramdisk hashes differ from the authoritative C160 ABI-v2 production artifacts.'
    }
}
if ($PhaseC161 -and -not $PhaseC162 -and $ReuseBuiltProofKernel) {
    throw 'C161 cannot reuse a proof kernel from another phase; its Task Manager registration must be built here.'
}
if (-not $PhaseC160) {
    $ordinaryKernelHash = $canonicalKernelHash
    $ordinaryRamdiskHash = $ramdiskHash
}
if ($canonicalKernelHash -ne $espKernelHash) {
    throw 'Canonical kernel and ESP kernel differ; no proof assets were changed.'
}
Copy-Item -LiteralPath $kernelPath -Destination $proofBackup -Force
Copy-Item -LiteralPath $espKernelPath -Destination $espKernelBackup -Force
Copy-Item -LiteralPath $protectedRamdiskPath -Destination $ramdiskBackup -Force
if ((Get-Hash $proofBackup) -ne $canonicalKernelHash -or
    (Get-Hash $espKernelBackup) -ne $espKernelHash -or
    (Get-Hash $ramdiskBackup) -ne $ramdiskHash) {
    throw 'C158 could not verify byte-identical protected backups.'
}
Clear-QemuEspCopies

trap {
    if ($script:activeProcess) {
        $script:activeProcess.Refresh()
        if (-not $script:activeProcess.HasExited) {
            Stop-Process -Id $script:activeProcess.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $script:activeProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    Restore-CanonicalFiles
    throw $_
}

$python = if ($PythonExe) { $PythonExe } else {
    Get-Tool 'python' @('C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe', 'C:\Python312\python.exe', 'C:\Python311\python.exe')
}
$qemu = Get-Tool 'qemu-system-x86_64' @('C:\Program Files\qemu\qemu-system-x86_64.exe')
$ovmf = Get-Tool 'ovmf' @('C:\Program Files\qemu\share\edk2-x86_64-code.fd')
$make = Get-Tool 'mingw32-make' @('C:\mingw64\bin\mingw32-make.exe')

$managedBuild = Join-Path $RepoRoot 'scripts\dotnet\build-managed-hostlog-proof.ps1'
$managedBuildArguments = @('-ExecutionPolicy','Bypass','-File',$managedBuild,
    '-RepoRoot',$RepoRoot,'-OutputRoot',$compositeRoot,
    '-RuntimePackRoot',(Join-Path $RepoRoot 'tools\dotnet\runtime-pack'),
    '-RuntimePackOutputRoot',$runtimePackOutput,'-UseGuideXosRuntimePack',
    '-ProductionApplication','-PersistentCompositeLifecycle','-AllocationMode','Allocating',
    '-ManagedProjectMode',$(if ($PhaseC160) { 'C160Composite' } else { 'C154Composite' }),'-C155ManagedNotesSession',
    '-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument',
    '-C158ManagedCalculator','-HeapConfiguration',$(if ($PhaseC164) { 'Primary8MiB' } else { 'Primary4MiB' }),'-PythonExe',$python)
if ($PhaseC160) { $managedBuildArguments += '-C160ApplicationSnapshotProof' }
if ($PhaseC161) {
    $managedBuildArguments += '-C161ManagedTaskManager'
    $managedBuildArguments += '-C161TaskManagerProof'
}
if ($PhaseC162) {
    $managedBuildArguments += '-C162ManagedTaskManagerClose'
    $managedBuildArguments += '-C162TaskManagerCloseProof'
}
if ($PhaseC163) {
    $managedBuildArguments += '-C163ManagedFileExplorer'
    $managedBuildArguments += '-C163FileExplorerProof'
}
if ($PhaseC164) { $managedBuildArguments += '-C164FileActivationProof' }
Invoke-Checked 'powershell' $managedBuildArguments
$compositeElf = Join-Path $compositeRoot 'artifacts\HostLogProof.elf'
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) { throw "C158 NativeAOT ELF missing: $compositeElf" }
$compositeHash = Get-Hash $compositeElf
Write-Host ("{0} proof NativeAOT composite built: {1}" -f $(if ($PhaseC163) { 'C163' } elseif ($PhaseC162) { 'C162' } elseif ($PhaseC161) { 'C161' } elseif ($PhaseC160) { 'C160' } else { 'C158' }), $compositeHash) -ForegroundColor Green

$generator = Join-Path $RepoRoot 'scripts\generate-wallpaper-pack.ps1'
$proofMediaArguments = @('-ExecutionPolicy','Bypass','-File',$generator,
    '-OutputDir',$stageRoot,'-OutputImage',$proofRamdisk,
    '-C104AppAPath',$compositeElf,'-ProductionCompositeApplicationPath',$compositeElf,
    '-C114ManagedDirectoryServices','-C117ManagedTextArea','-C118ManagedListBox',
    '-C151ManagedOpenFileDialog','-C152ManagedNotesSaveWorkflow',
    '-C155ManagedNotesSession','-C156ControlModifierShortcuts','-C157ManagedNotesNewDocument')
if ($PhaseC164) { $proofMediaArguments += '-C164FileActivation' }
Invoke-Checked 'powershell' $proofMediaArguments
if (-not (Test-Path -LiteralPath $proofRamdisk -PathType Leaf)) { throw 'C158 proof ramdisk was not generated.' }
$proofRamdiskHash = Get-Hash $proofRamdisk

$flags = @(
    '-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION','-DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH',
    '-DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION','-DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES',
    '-DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES','-DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER',
    '-DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT','-DGXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA',
    '-DGXOS_NATIVEAOT_C118_MANAGED_LIST_BOX','-DGXOS_NATIVEAOT_C119_MANAGED_BUTTON',
    '-DGXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST','-DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX',
    '-DGXOS_NATIVEAOT_C122_MANAGED_LABEL','-DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR',
    '-DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON','-DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR',
    '-DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX','-DGXOS_NATIVEAOT_C127_MANAGED_PANEL',
    '-DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE','-DGXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT',
    '-DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX','-DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON',
    '-DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX','-DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING',
    '-DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU','-DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU',
    '-DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING','-DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR',
    '-DGXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT','-DGXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW',
    '-DGXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK','-DGXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK',
    '-DGXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX','-DGXOS_NATIVEAOT_C144_MANAGED_SETTINGS_CENTER',
    '-DGXOS_NATIVEAOT_C145_MANAGED_MODAL_DIALOG','-DGXOS_NATIVEAOT_C146_SETTINGS_PERSISTENCE',
    '-DGXOS_NATIVEAOT_C147_RUNTIME_SETTINGS','-DGXOS_NATIVEAOT_C148_SETTINGS_V2',
    '-DGXOS_NATIVEAOT_C149_SECOND_RUNTIME_SETTING','-DGXOS_NATIVEAOT_C150_MANAGED_APP_RETURN',
    '-DGXOS_NATIVEAOT_C151_MANAGED_OPEN_FILE_DIALOG',
    '-DGXOS_NATIVEAOT_C152_MANAGED_NOTES_SAVE_WORKFLOW',
    '-DGXOS_NATIVEAOT_C155_MANAGED_NOTES_SESSION',
    '-DGXOS_NATIVEAOT_C156_CONTROL_MODIFIER_SHORTCUTS',
    '-DGXOS_NATIVEAOT_C157_MANAGED_NOTES_NEW_DOCUMENT',
    '-DGXOS_NATIVEAOT_C158_MANAGED_CALCULATOR')
if ($PhaseC161) { $flags += '-DGXOS_NATIVEAOT_C161_MANAGED_TASK_MANAGER' }
if ($PhaseC162) { $flags += '-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE' }
if ($PhaseC163) { $flags += '-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER' }
$canonicalKernelFlags = $flags -join ' '
if ($PhaseC160) { $flags += '-DGXOS_NATIVEAOT_C160_APPLICATION_SNAPSHOT_PROOF' }
if ($PhaseC161) { $flags += '-DGXOS_NATIVEAOT_C161_TASK_MANAGER_PROOF' }
if ($PhaseC162) { $flags += '-DGXOS_NATIVEAOT_C162_MANAGED_TASK_MANAGER_CLOSE_PROOF' }
if ($PhaseC163) { $flags += '-DGXOS_NATIVEAOT_C163_MANAGED_FILE_EXPLORER_PROOF' }
if ($PhaseC164) { $flags += '-DGXOS_NATIVEAOT_C164_FILE_ACTIVATION_PROOF' }
$flags = $flags -join ' '
if ($ReuseBuiltProofKernel) {
    if (-not (Test-Path -LiteralPath $proofKernel -PathType Leaf)) {
        throw "-ReuseBuiltProofKernel requires a prior proof kernel at $proofKernel"
    }
    Write-Host ("{0} reusing the already built and preserved proof kernel." -f $(if ($PhaseC163) { 'C163' } else { 'C158' }))
} else {
    Write-Host $(if ($PhaseC163) { 'C163 building the proof kernel with managed File Explorer registration and routing.' } elseif ($PhaseC162) { 'C162 building the proof kernel with application close and Managed Task Manager metadata.' } elseif ($PhaseC161) { 'C161 building the proof kernel with Managed Task Manager metadata and routing.' } elseif ($PhaseC160) { 'C160 building the proof kernel with ABI-v2 snapshot instrumentation.' } else { 'C158 building the production kernel with managed Calculator metadata and launch routing.' })
    Invoke-Checked $make @('-C',(Join-Path $RepoRoot 'kernel'),'ARCH=amd64',"EXTRA_CFLAGS=$flags",'-B')
    if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw 'Proof kernel build did not produce kernel.elf.' }
    Copy-Item -LiteralPath $kernelPath -Destination $proofKernel -Force
}
$proofKernelHash = Get-Hash $proofKernel
$settingsRecord = Join-Path $EvidenceRoot 'GXSETT.BIN'
New-C151SettingsRecord $settingsRecord
Write-Host ("{0} proof kernel SHA-256: {1}" -f $(if ($PhaseC163) { 'C163' } elseif ($PhaseC162) { 'C162' } elseif ($PhaseC161) { 'C161' } elseif ($PhaseC160) { 'C160' } else { 'C158' }), $proofKernelHash)

$production = [System.Collections.Generic.List[object]]::new()
$production.Add((Invoke-ProductionBoot 1 'pointer-arithmetic' $qemu $ovmf $settingsRecord)) | Out-Null
$production.Add((Invoke-ProductionBoot 2 'keyboard-focus' $qemu $ovmf $settingsRecord)) | Out-Null
$production.Add((Invoke-ProductionBoot 3 'error-recovery-lifecycle' $qemu $ovmf $settingsRecord)) | Out-Null

if ($PhaseC160) {
    Write-Host $(if ($PhaseC163) { 'C163 installing the clean ABI-v3 production kernel and matching composite/ramdisk.' } elseif ($PhaseC162) { 'C162 installing the clean ABI-v3 production kernel and matching composite/ramdisk.' } elseif ($PhaseC161) { 'C161 installing the clean ABI-v2 production kernel and matching composite/ramdisk.' } else { 'C160 installing the clean ABI-v2 production kernel and matching composite/ramdisk.' }) -ForegroundColor Yellow
    Install-C160CanonicalProducts $python
    Write-Host $(if ($PhaseC163) { 'C163 post-phase production hashes verified; beginning ordinary boots.' } elseif ($PhaseC162) { 'C162 post-phase production hashes verified; beginning ordinary boots.' } elseif ($PhaseC161) { 'C161 post-phase production hashes verified; beginning ordinary boots.' } else { 'C160 post-phase production hashes verified; beginning ordinary boots.' }) -ForegroundColor Green
} else {
    Restore-CanonicalFiles
    if (-not $script:restored) { throw 'C158 failed byte-for-byte restoration before ordinary boots.' }
    Write-Host 'C158 protected kernel and ramdisk hashes restored; beginning ordinary boots.' -ForegroundColor Green
}
$ordinary = [System.Collections.Generic.List[object]]::new()
for ($boot = 1; $boot -le 3; $boot++) {
    Write-Host ("{0} ordinary production boot {1}/3." -f $(if ($PhaseC163) { 'C163' } elseif ($PhaseC162) { 'C162' } elseif ($PhaseC161) { 'C161' } elseif ($PhaseC160) { 'C160' } else { 'C158' }), $boot)
    $ordinary.Add((Invoke-OrdinaryBoot $boot $qemu $ovmf)) | Out-Null
}
if ($PhaseC160) {
    if ((Get-Hash $kernelPath) -ne $postC160KernelHash -or
        (Get-Hash $espKernelPath) -ne $postC160KernelHash -or
        (Get-Hash $protectedRamdiskPath) -ne $postC160RamdiskHash) {
        throw 'C160 ordinary boots changed the new canonical production artifacts.'
    }
} elseif ((Get-Hash $kernelPath) -ne $canonicalKernelHash -or
    (Get-Hash $espKernelPath) -ne $espKernelHash -or
    (Get-Hash $protectedRamdiskPath) -ne $ramdiskHash) {
    throw 'C158 ordinary boots changed a restored protected artifact.'
}

$productionRecords = [System.Collections.Generic.List[object]]::new()
for ($boot = 1; $boot -le 3; $boot++) {
    $serial = Join-Path $EvidenceRoot ("production-boot-{0:D2}\serial.log" -f $boot)
    $text = Get-Serial $serial
    if ($boot -eq 3) {
        $launchMatches = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator ')
        $closeMatches = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 ')
        $instances = [regex]::Matches($text, '(?m)^\[C150-MANAGED-OUTPUT\] C150-APP-INSTANCE id=6 generation=(\d+)')
        $launches = [regex]::Matches($text, '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.calculator generation=([0-9A-Fa-f]+) selector=00000006 kind=normal')
        $lastState = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-STATE display=([^\r\n]+)')
        $expectedFinalState = if ($PhaseC162) {
            # C162 ends boot 3 with a real 7*8 calculation in the freshly
            # relaunched Managed Calculator and a live Task Manager snapshot.
            # Earlier phases intentionally finish this sequence at zero.
            '^56 phase=3 commands=4 result=PASS$'
        } else {
            '^0 phase=0 commands=0 result=PASS$'
        }
        $minimumCalculatorLaunches = if ($PhaseC163) { 2 } else { 27 }
        $minimumCalculatorCloses = if ($PhaseC163) { 1 } else { 26 }
        if ($launchMatches.Count -lt $minimumCalculatorLaunches -or
            $closeMatches.Count -lt $minimumCalculatorCloses -or
            $instances.Count -lt $minimumCalculatorLaunches -or
            $launches.Count -lt $minimumCalculatorLaunches -or
            $lastState.Count -eq 0 -or $lastState[$lastState.Count - 1].Groups[1].Value -notmatch $expectedFinalState) {
            throw 'Final boot is missing its required Calculator smoke, lifecycle evidence, or phase-specific final Calculator state.'
        }
        $script:finalCalculatorGeneration = [uint32]$instances[$instances.Count - 1].Groups[1].Value
        $script:finalNativeLaunchGeneration = $launches[$launches.Count - 1].Groups[1].Value
    }
    $productionRecords.Add([ordered]@{
        Boot = $boot
        Scenario = @('pointer-arithmetic','keyboard-focus','error-recovery-lifecycle')[$boot - 1]
        Status = 'PASS'
        QemuAccelerator = if ($PhaseC160) { 'whpx' } else { 'tcg,thread=single' }
        SerialPath = $serial
        SerialSha256 = Get-Hash $serial
        CalculatorLaunches = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-LAUNCH id=managed-calculator ').Count
        CalculatorCloses = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C158-CALC-CLOSE controls=0 ').Count
        TaskManagerScenario = if ($PhaseC161) { $production[$boot - 1].TaskManagerScenario } else { $null }
        TaskManagerLaunches = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-LAUNCH id=7 ').Count
        TaskManagerCloses = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-CLOSE controls=0 ').Count
        TaskManagerCtrlRRefreshes = [regex]::Matches($text, '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R ').Count
        ProofKernelSha256 = $proofKernelHash
        ProofRamdiskSha256 = $proofRamdiskHash
    }) | Out-Null
}
$ordinaryRecords = [System.Collections.Generic.List[object]]::new()
for ($boot = 1; $boot -le 3; $boot++) {
    $serial = Join-Path $EvidenceRoot ("ordinary-boot-{0:D2}\serial.log" -f $boot)
    $ordinaryRecords.Add([ordered]@{
        Boot = $boot
        Status = 'PASS'
        QemuAccelerator = if ($PhaseC160) { 'whpx' } else { 'tcg,thread=single' }
        SerialPath = $serial
        SerialSha256 = Get-Hash $serial
        KernelSha256 = $ordinaryKernelHash
        RamdiskSha256 = $ordinaryRamdiskHash
        TaskManagerLifetimeId = $ordinary[$boot - 1].TaskManagerLifetimeId
        ActiveApplication = $ordinary[$boot - 1].ActiveApplication
    }) | Out-Null
}

$boot1Text = Get-Serial (Join-Path $EvidenceRoot 'production-boot-01\serial.log')
$boot2Text = Get-Serial (Join-Path $EvidenceRoot 'production-boot-02\serial.log')
$boot3Text = Get-Serial (Join-Path $EvidenceRoot 'production-boot-03\serial.log')
$controlDownEvents = [regex]::Matches($boot2Text, '\[C156-KEYBOARD\] event=control-(?:left|right)-down ')
$controlUpEvents = [regex]::Matches($boot2Text, '\[C156-KEYBOARD\] event=control-(?:left|right)-up ')
$lastControlAggregate = [regex]::Matches($boot2Text,
    '\[C156-KEYBOARD\] event=control-(?:left|right)-(?:down|up) [^\r\n]*aggregate=([01])')
$shiftDownEvents = [regex]::Matches($boot3Text, '\[C129-KEYBOARD\] shift=down side=left ')
$shiftUpEvents = [regex]::Matches($boot3Text, '\[C129-KEYBOARD\] shift=up side=left ')
$lastShiftAggregate = [regex]::Matches($boot3Text,
    '\[C129-KEYBOARD\] shift=(?:down|up) side=left aggregate=([01])')
$controlBalanced = $controlDownEvents.Count -gt 0 -and
    $controlDownEvents.Count -eq $controlUpEvents.Count -and
    $lastControlAggregate.Count -gt 0 -and
    $lastControlAggregate[$lastControlAggregate.Count - 1].Groups[1].Value -eq '0'
$shiftBalanced = $shiftDownEvents.Count -gt 0 -and
    $shiftDownEvents.Count -eq $shiftUpEvents.Count -and
    $lastShiftAggregate.Count -gt 0 -and
    $lastShiftAggregate[$lastShiftAggregate.Count - 1].Groups[1].Value -eq '0'
if (-not $controlBalanced -or -not $shiftBalanced) {
    throw 'C158 production keyboard modifier events are unbalanced or finish pressed.'
}
$c162CloseProof = $null
if ($PhaseC162) {
    $nativeFocused = [regex]::Match($boot1Text,
        '(?m)^\[C162-APP-CLOSE-TESTS\] cases=([0-9A-F]{8}) stress=100 identity-revalidation=checked lifecycle=checked result=PASS')
    $wrapperAbi = [regex]::Match($boot1Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C162-CLOSE-ABI cases=(\d+) v2=NotSupported identity=source\+u64 result=PASS')
    $nativeBoundary = [regex]::Match($boot1Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C162-CLOSE-NATIVE-BOUNDARY cases=(\d+) self=Protected shell=Protected unknown=NotFound result=PASS')
    $boot1ManagedCloses = [regex]::Matches($boot1Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=3:\d+ result=0 fresh=true remains=false result=PASS')
    $boot1Cancels = [regex]::Matches($boot1Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C162-TM-CANCEL mutation=none selection=preserved result=PASS')
    $boot2NativeCloses = [regex]::Matches($boot2Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=1:\d+ result=0 fresh=true remains=false result=PASS')
    $boot2ShellDisabled = [regex]::Matches($boot2Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE-ELIGIBILITY identity=2:\d+ enabled=false result=PASS')
    $boot2SelfDisabled = [regex]::Matches($boot2Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE-ELIGIBILITY identity=3:\d+ enabled=false result=PASS')
    $boot3ManagedCloses = [regex]::Matches($boot3Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C162-TM-CLOSE identity=3:\d+ result=0 fresh=true remains=false result=PASS')
    $minimumBoot3ManagedCloses = if ($PhaseC163) { 1 } else { 25 }
    if (-not $nativeFocused.Success -or [Convert]::ToInt32($nativeFocused.Groups[1].Value, 16) -lt 18 -or
        -not $wrapperAbi.Success -or [int]$wrapperAbi.Groups[1].Value -ne 18 -or
        -not $nativeBoundary.Success -or [int]$nativeBoundary.Groups[1].Value -ne 3 -or
        $boot1ManagedCloses.Count -lt 1 -or $boot1Cancels.Count -lt 1 -or
        $boot2NativeCloses.Count -lt 8 -or $boot2ShellDisabled.Count -lt 1 -or
        $boot2SelfDisabled.Count -lt 1 -or $boot3ManagedCloses.Count -lt $minimumBoot3ManagedCloses) {
        throw 'C162 focused, managed-boundary, Cancel, protected-target, or managed close smoke evidence is incomplete.'
    }
    foreach ($bootText in @($boot1Text, $boot2Text, $boot3Text)) {
        if ($bootText -notmatch '(?m)^\[C162-NATIVE-CALCULATOR\] close=relaunch identities=distinct operation=7\*8=56 result=PASS') {
            throw 'C162 focused native Calculator close/relaunch calculation evidence is missing from a production boot.'
        }
    }
    $c162CloseProof = [ordered]@{
        nativeAppManagerCloseCases = [Convert]::ToInt32($nativeFocused.Groups[1].Value, 16)
        closeStressRequests = 100
        staleIdentityRevalidation = $true
        sameSlotReplacementSurvives = $true
        managedWrapperCases = [int]$wrapperAbi.Groups[1].Value
        nativeManagedBoundaryCases = [int]$nativeBoundary.Groups[1].Value
        abiV2Close = 'NotSupported without callback access'
        managedCalculatorCloseCancelAndConfirmBoot1 = $true
        managedCalculatorCloseCyclesBoot1 = $boot1ManagedCloses.Count
        cancelRequestsBoot1 = $boot1Cancels.Count
        nativeCalculatorCloseRequestsBoot2 = $boot2NativeCloses.Count
        shellCloseDisabled = $true
        taskManagerSelfCloseDisabled = $true
        managedCalculatorCloseCyclesBoot3 = $boot3ManagedCloses.Count
        historicalC162BaselineManifestValidated = [bool]$PhaseC163
        distinctCalculatorRelaunchIdentitiesBoot3 = $true
        nativeCalculatorCloseAndRelaunchCalculation = '7*8=56, distinct identities, all 3 boots'
        postMutationSnapshotIsAuthoritative = $true
        forceKillAuthority = $false
        processOrThreadSemanticsAdded = $false
    }
}
$phaseLabel = if ($PhaseC164) { 'C164' } elseif ($PhaseC163) { 'C163' } elseif ($PhaseC162) { 'C162' } elseif ($PhaseC161) { 'C161' } elseif ($PhaseC160) { 'C160' } else { 'C158' }
$manifestFile = if ($PhaseC164) { 'c164-proof-manifest.json' } elseif ($PhaseC163) { 'c163-proof-manifest.json' } elseif ($PhaseC162) { 'c162-proof-manifest.json' } elseif ($PhaseC161) { 'c161-proof-manifest.json' } elseif ($PhaseC160) { 'c160-proof-manifest.json' } else { 'c158-proof-manifest.json' }
$ordinaryManifestFile = if ($PhaseC164) { 'c164-ordinary-manifest.json' } elseif ($PhaseC163) { 'c163-ordinary-manifest.json' } elseif ($PhaseC162) { 'c162-ordinary-manifest.json' } elseif ($PhaseC161) { 'c161-ordinary-manifest.json' } elseif ($PhaseC160) { 'c160-ordinary-manifest.json' } else { 'c158-ordinary-restoration-manifest.json' }
$reportedCompositeElf = if ($PhaseC164 -or $PhaseC160) { $canonicalCompositeElf } else { $compositeElf }
$reportedCompositeHash = if ($PhaseC164 -or $PhaseC160) { $canonicalCompositeHash } else { $compositeHash }
$reportedAbiVersion = if ($PhaseC163 -or $PhaseC162) { 3 } elseif ($PhaseC160) { 2 } else { 1 }
$reportedAbiSize = if ($PhaseC163 -or $PhaseC162) { 120 } elseif ($PhaseC160) { 112 } else { 104 }
$c160FinalSnapshot = $null
if ($PhaseC160) {
    $finalCalculatorSnapshots = [regex]::Matches($boot3Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C160-SNAPSHOT appId=com\.guidexos\.apps\.managed\.calculator source=3 instance=([0-9]+) count=([0-9]+) active=1 [^\r\n]*result=PASS')
    $requiredCalculatorSnapshots = if ($PhaseC161) { 1 } else { 27 }
    if ($finalCalculatorSnapshots.Count -lt $requiredCalculatorSnapshots) {
        throw 'C160 final stable Calculator identity evidence is incomplete.'
    }
    $lastSnapshot = $finalCalculatorSnapshots[$finalCalculatorSnapshots.Count - 1]
    $c160FinalSnapshot = [ordered]@{
        applicationId = 'com.guidexos.apps.managed.calculator'
        source = 'ManagedLogicalApplication'
        instanceId = [uint64]$lastSnapshot.Groups[1].Value
        totalCount = [uint32]$lastSnapshot.Groups[2].Value
        activeCount = 1
        capturedAt = 'C160 managed Calculator identity verification; the final Task Manager snapshot is reported separately'
    }
}
$c161FinalSnapshot = $null
$c161FocusedTestCases = 0
$c161ProductionRefreshes = 0
$c161ManagerLaunches = 0
$c161ManagerCloses = 0
if ($PhaseC161) {
    $boot1Text = Get-Serial (Join-Path $EvidenceRoot 'production-boot-01\serial.log')
    $managerTests = [regex]::Match($boot1Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-TESTS cases=(\d+) initial=PASS selection=identity stress=1000 result=PASS')
    if (-not $managerTests.Success -or [int]$managerTests.Groups[1].Value -lt 20) {
        throw 'C161 focused UI/snapshot suite did not pass at 20 or more cases with 1000 wrapper calls.'
    }
    $c161FocusedTestCases = [int]$managerTests.Groups[1].Value

    $boot3TaskLaunches = [regex]::Matches($boot3Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-LAUNCH id=7 reg=\d+ ctr=4 cap=4 max=20 snap=\d+ n=(\d+) self=(\d+) active=1 result=PASS')
    $boot3TaskCloses = [regex]::Matches($boot3Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-CLOSE controls=0 selection=none result=PASS')
    $boot3TaskRefreshes = [regex]::Matches($boot3Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-R s=Ctrl\+R st=\d+ n=\d+ sel=([^ ]+) v=(\d+) ap=true self=(\d+) active=1 result=PASS')
    $minimumBoot3TaskLaunches = if ($PhaseC163) { 2 } else { 26 }
    $minimumBoot3TaskCloses = if ($PhaseC163) { 1 } else { 25 }
    $minimumBoot3TaskRefreshes = if ($PhaseC163) { 1 } else { 100 }
    if ($boot3TaskLaunches.Count -lt $minimumBoot3TaskLaunches -or
        $boot3TaskCloses.Count -lt $minimumBoot3TaskCloses -or
        $boot3TaskRefreshes.Count -lt $minimumBoot3TaskRefreshes) {
        throw 'C161 boot 3 lacks either the C163 Task Manager observation smoke or the standalone C161 lifecycle/refresh stress.'
    }
    $boot3LifetimeIds = @($boot3TaskLaunches | ForEach-Object {
        $_.Groups[2].Value
    })
    $uniqueBoot3LifetimeIds = @($boot3LifetimeIds | Select-Object -Unique)
    if ($uniqueBoot3LifetimeIds.Count -ne $boot3LifetimeIds.Count) {
        throw 'C161 boot 3 reused a Task Manager lifetime identity across launches.'
    }

    $lastManagerLaunch = $boot3TaskLaunches[$boot3TaskLaunches.Count - 1]
    $lastManagerRefresh = $boot3TaskRefreshes[$boot3TaskRefreshes.Count - 1]
    $managerLifetime = $lastManagerLaunch.Groups[2].Value
    if ($lastManagerRefresh.Groups[3].Value -ne $managerLifetime -or
        $lastManagerRefresh.Groups[1].Value -eq 'none') {
        throw 'C161 final refresh did not retain the active final Task Manager identity and a selected row.'
    }
    $selectedParts = $lastManagerRefresh.Groups[1].Value.Split(':')
    if ($selectedParts.Length -ne 2) {
        throw 'C161 final selected identity is malformed.'
    }
    [uint64]$selectedNumeric = [uint64]::Parse($selectedParts[1],
        [System.Globalization.CultureInfo]::InvariantCulture)
    $selectedHex = $selectedNumeric.ToString('X16')
    $detailMatches = [regex]::Matches($boot3Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-DETAIL source=(\d+) identity=([0-9A-F]{16}) name=(.*?) state=([^ ]+) active=(Yes|No) result=PASS')
    $appIdMatches = [regex]::Matches($boot3Text,
        '(?m)^\[C102-MANAGED-OUTPUT\] C161-TM-ID (\d+):([0-9A-F]{16})=(\S+)')
    if ($detailMatches.Count -eq 0 -or $appIdMatches.Count -eq 0) {
        throw 'C161 final detail pane evidence is missing.'
    }
    $matchingDetailMatches = @($detailMatches | Where-Object {
        $_.Groups[1].Value -eq $selectedParts[0] -and
        $_.Groups[2].Value -eq $selectedHex
    })
    $matchingAppIdMatches = @($appIdMatches | Where-Object {
        $_.Groups[1].Value -eq $selectedParts[0] -and
        $_.Groups[2].Value -eq $selectedHex
    })
    if ($matchingDetailMatches.Count -eq 0 -or $matchingAppIdMatches.Count -eq 0) {
        throw 'C161 selected refresh identity has no matching detail pane and app ID evidence.'
    }
    $finalDetail = $matchingDetailMatches[$matchingDetailMatches.Count - 1]
    $finalAppId = $matchingAppIdMatches[$matchingAppIdMatches.Count - 1]
    if ($finalDetail.Groups[1].Value -ne $selectedParts[0] -or
        $finalDetail.Groups[2].Value -ne $selectedHex -or
        $finalAppId.Groups[1].Value -ne $selectedParts[0] -or
        $finalAppId.Groups[2].Value -ne $selectedHex) {
        throw 'C161 final detail pane identity does not match the selected C160 lifetime identity.'
    }
    $c161ProductionRefreshes = $boot3TaskRefreshes.Count
    $c161ManagerLaunches = $boot3TaskLaunches.Count
    $c161ManagerCloses = $boot3TaskCloses.Count
    $c161FinalSnapshot = [ordered]@{
        applicationId = 'com.guidexos.apps.managed.taskmanager'
        lifetimeId = [uint64]$managerLifetime
        lifetimeHex = ([uint64]$managerLifetime).ToString('X16')
        snapshotRecordCount = [uint32]$lastManagerLaunch.Groups[1].Value
        activeRecordCount = 1
        activeApplicationId = 'com.guidexos.apps.managed.taskmanager'
        selectedIdentitySource = [uint32]$selectedParts[0]
        selectedLifetimeId = $selectedNumeric
        selectedLifetimeHex = $selectedHex
        selectedName = $finalDetail.Groups[3].Value
        selectedApplicationId = $finalAppId.Groups[3].Value
        selectedState = $finalDetail.Groups[4].Value
        selectedActive = $finalDetail.Groups[5].Value
        viewport = [uint32]$lastManagerRefresh.Groups[2].Value
        detailMatchesSelection = $true
        selfLifetimeStableFor100Refreshes = -not $PhaseC163
        C162BaselineCoversHistoricalStress = [bool]$PhaseC163
    }
}
$applicationManifest = if ($PhaseC163) {
    [ordered]@{
        displayName = 'Managed File Explorer'
        applicationId = 'com.guidexos.apps.managed.fileexplorer'
        selector = 8
        nativeFileExplorerId = 'gxos.builtin.fileexplorer'
        nativeFileExplorerPreserved = $true
        initialPath = '/system/apps'
        rootLimitation = 'VFS / enumeration is not exposed by the existing proven managed API'
        pathCapacityBytes = 96
        directoryEntryCapacity = 64
        maximumNameBytes = 127
        controlCount = 5
        controls = @('Entry ListBox','Up','Open','Refresh','Close')
        controlHostMaximum = 20
        hostAbiVersion = 3
        hostTableBytes = 120
        newHostCallAdded = $false
        vfsReuse = @('GuideXosDirectoryListing.Load','GuideXosPickerPath','GuideXosFile.TryListDirectory','GuideXosFile.TryGetInfo')
        entryClassification = 'authoritative VFS Directory or Regular only'
        sorting = 'existing deterministic directory-first/name ordering'
        truncation = 'bounded snapshot sets hasMore and displays a status'
        selectionMatching = 'entry name plus VFS type within the current directory; not filesystem identity'
        refreshBehavior = 'preserve selection by name and type and retain a valid viewport; otherwise clear selection'
        navigation = 'transactional child enumeration; Up stops at /system/apps; failed enumeration retains old path'
        regularFileOpen = 'selection and metadata only; activation not implemented'
        readOnly = $true
        filesystemMutation = $false
        persistentDirectoryTree = $false
    }
} elseif ($PhaseC162) {
    [ordered]@{
        displayName = 'Managed Task Manager'
        applicationId = 'com.guidexos.apps.managed.taskmanager'
        selector = 7
        managedLogicalSurfaceCapacity = 3
        simultaneousCalculatorSurfaceClaim = $true
        simultaneousManagedApplications = @(
            'com.guidexos.apps.managed.notes',
            'com.guidexos.apps.managed.calculator',
            'com.guidexos.apps.managed.taskmanager')
        nativeTaskManagerId = 'gxos.builtin.taskmanager'
        nativeTaskManagerPreserved = $true
        nativeCalculatorId = 'gxos.builtin.calculator'
        managedCalculatorId = 'com.guidexos.apps.managed.calculator'
        appManagerCapacity = 16
        shellRecords = 1
        managedLogicalSurfaceRecords = 3
        c160SnapshotRecordCapacity = 20
        listCapacity = 20
        controlCount = 4
        controlOrder = @('Application list','Refresh','Close Application','Close Task Manager')
        closeIdentity = 'GuideXosApplicationInstanceId: source plus full 64-bit lifetime ID'
        confirmation = 'C145 modal captures identity and display text; parent list and refresh are blocked'
        cancelBehavior = 'clears pending identity; performs no host mutation; selection remains'
        successBehavior = 'canonical normal close followed by a fresh authoritative C160 snapshot'
        staleBehavior = 'exact old identity returns NotFound or StaleIdentity; replacement is untouched'
        protectedTargets = @('ShellSurface','Managed Task Manager self','managed logical apps other than Managed Calculator')
        otherAppManagerApplications = 'live AppManager source and full lifetime identity required; target close-veto contract remains active'
        authorization = 'trusted system host table; no per-user or per-application privilege model exists'
        mutationAuthority = 'one AppManager application lifetime through normal KernelApp close lifecycle; no force kill'
        settingsVersion = 2
        persistentSettingsAdded = $false
    }
} elseif ($PhaseC161) {
    [ordered]@{
        displayName = 'Managed Task Manager'
        applicationId = 'com.guidexos.apps.managed.taskmanager'
        selector = 7
        runtimeManagedDescriptorsBeforeAfterC161 = '3 -> 4 fixed descriptors'
        nativeAotMetadataCatalogBeforeAfterC161 = '6 -> 7 fixed descriptors'
        startMenuEntriesBeforeAfterC161 = '16 -> 17'
        startMenuPinnedEntriesBeforeAfterC161 = '15 -> 16'
        allProgramsEntriesBeforeAfterC161 = '19 -> 20'
        nativeTaskManagerId = 'gxos.builtin.taskmanager'
        nativeTaskManagerPreserved = $true
        nativeCalculatorId = 'gxos.builtin.calculator'
        managedCalculatorId = 'com.guidexos.apps.managed.calculator'
        nativeAndManagedCalculatorIdentityDistinct = $true
        simultaneousCalculatorSurfaceClaim = $false
        appManagerCapacity = 16
        shellRecords = 1
        managedLogicalSurfaceRecords = 1
        c160SnapshotRecordCapacity = 18
        listCapacity = 18
        listLabelCapacityCharacters = 48
        listLabelStorageCharacters = 864
        listLabelStorageBytes = 1728
        applicationIdDisplayed = $true
        lifetimeIdDisplayed = 'full 64-bit fixed-width hexadecimal'
        stateField = 'C160 AppState enum'
        activeField = 'C160 active flag'
        sourceField = 'AppManagerInstance, ShellSurface, ManagedLogicalApplication'
        shellDisplay = 'Terminal; no application ID; shown as shell surface'
        classificationField = 'C160 source only; no invented kind field'
        omittedFields = @('CPU usage', 'memory usage', 'process/thread view')
        ordering = 'preserves C160 snapshot order'
        selectionIdentity = 'GuideXosApplicationInstanceId: source plus 64-bit lifetime ID'
        refresh = 'one new GuideXosHost.TryGetApplicationSnapshot wrapper call; preserve selection only by C160 identity'
        ctrlR = 'same RefreshAndRender action; split key character suppressed'
        disappearingSelection = 'clear selection/details; do not bind to a replacement lifetime'
        viewport = 'surviving selection is revealed; cleared selection resets to valid origin'
        failureBehavior = 'bounded status; retain last valid snapshot and rows; first failure stays empty'
        truncationBehavior = 'visible Showing copied of total applications status'
        focusOrder = 'Application list -> Refresh -> Close; three registered controls'
        refreshAllocation = 'fixed 18-record snapshot and 18 x 48-character row storage; spans and fixed labels; no growing collection or per-refresh row strings'
        mutationAuthority = 'none; no end-task, kill, suspend, resume, or activate action'
        settingsVersion = 2
        persistentSettingsAdded = $false
    }
} else {
    [ordered]@{
        displayName = 'Managed Calculator'
        applicationId = 'com.guidexos.apps.managed.calculator'
        selector = 6
        managedRegistryCount = '2 -> 3 fixed descriptors'
        startMenuPinnedEntries = '15 -> 16'
        allProgramsEntries = '18 -> 19'
        nativeCalculatorId = 'gxos.builtin.calculator'
        nativeCalculatorPreserved = $true
        managedAppSwitch = 'Notes -> Calculator -> Notes through Start Menu; Calculator surface replaced'
        controlCount = 18
        perAppControlCapacity = 18
        sharedControlHostMaximum = '10 -> 20'
        statePayloadBytes = 21
        numericType = 'signed Int64 only'
        maximumEntryDigits = 19
        division = 'integer truncation toward zero'
        precedence = 'left-to-right immediate evaluation'
        repeatedEquals = 'no-op; retain current result'
        clipboard = 'GuideXosClipboard.Shared unchanged across Calculator test, launch, and teardown'
        lifecycleCloseAction = 'existing Host ABI action dispatch; ABI unchanged'
    }
}
$manifest = [ordered]@{
    schemaVersion = 1
    phase = $phaseLabel
    outcome = 'A'
    branch = 'v1.1_DOTNET_SUPPORT'
    c159BlockersResolved = if ($PhaseC161) { @('missing managed snapshot API: resolved by C160 ABI v2 wrapper', 'missing stable instance identity: resolved by C160 source plus 64-bit lifetime token', 'row-index selection ambiguity: resolved by identity-based selection') } else { @() }
    c161Contract = if ($PhaseC161) { [ordered]@{
        snapshotPath = 'Task Manager UI -> GuideXosHost.TryGetApplicationSnapshot -> ABI v2 callback -> AppManager'
        usesAuthoritativeC160Wrapper = $true
        directNativeCallbackFromApplication = $false
        duplicateSnapshotModel = $false
        secondApplicationRegistry = $false
        rowIndexIsIdentity = $false
        readOnly = $true
        killOrEndTaskAuthority = $false
        fakeCpuOrMemoryMetrics = $false
        hostAbiVersion = 2
        hostTableBytes = 112
        v1PrefixBytes = 104
        snapshotCallbackOffset = 104
        recordBytes = 168
        snapshotRecordCapacity = 18
        snapshotBufferBytes = 3024
        appManagerSlots = 16
        settingsFormatVersion = 2
        nativeTaskManagerPreserved = $true
        nativeCalculatorPreserved = $true
        shellTruth = 'Terminal is a ShellSurface record with no application ID; it is not described as a process'
        managedSurfaceTruth = 'one active managed logical surface; replaced surfaces are not claimed to remain running'
    } } else { $null }
    c162Contract = if ($PhaseC162) { [ordered]@{
        architectureOverride = 'Managed Calculator and Managed Task Manager coexist as separate managed logical surfaces'
        managedSurfaceTruth = 'Notes, Managed Calculator, and Managed Task Manager can remain live concurrently'
        mutation = 'Close one exact C160 application lifetime through its canonical normal-close lifecycle'
        identity = 'snapshot source plus full 64-bit lifetime ID; no row, slot, display-name, or app-ID mutation key'
        revalidateAtMutation = $true
        staleOrGone = 'NotFound or StaleIdentity; a replacement lifetime is untouched'
        applicationVeto = 'preserved; normal close failure returns CloseFailed and leaves the app alive'
        confirmation = 'C145 modal, captured identity, Cancel performs no mutation'
        refresh = 'fresh C160 snapshot supplies the only post-close row and details state'
        shell = 'Protected natively and disabled in the UI'
        taskManagerSelf = 'Protected natively and disabled in the UI; own Close remains available'
        compositorZOrder = 'focused windows move above overlapping windows even when the registry contains reused-slot holes'
        otherManagedLogicalApps = 'Protected from remote close; Managed Calculator is the managed close proof target'
        authorization = 'host mutation surface is trusted system infrastructure; no managed-app privilege tiers exist'
        forceKill = $false
        processOrThreadSemantics = $false
        hostAbiVersion = 3
        hostTableBytes = 120
        v1PrefixBytes = 104
        v2PrefixBytes = 112
        snapshotCallbackOffset = 104
        closeCallbackOffset = 112
        abiV2Fallback = 'NotSupported before reading the appended callback'
        settingsVersion = 2
    } } else { $null }
    c163Contract = if ($PhaseC163) { [ordered]@{
        nativeFileExplorerId = 'gxos.builtin.fileexplorer'
        managedFileExplorerId = 'com.guidexos.apps.managed.fileexplorer'
        selector = 8
        nativeApplicationPreserved = $true
        currentHostAbiVersion = 3
        currentHostTableBytes = 120
        abiExtendedByC163 = $false
        settingsFormatVersion = 2
        filesystemMutation = $false
        fakeDirectoryRows = $false
        persistentDirectoryTree = $false
        proofCyclesBoot3 = $production[2].FileExplorerScenario.LaunchCount - 1
        uniqueFileExplorerLifetimesBoot3 = $true
        controlsReleasedAtClose = $true
        canonicalInitialPathOnRelaunch = '/system/apps'
        c162BaselineManifest = $c162BaselineManifestPath
        historicalC162ProofBoots = 3
        historicalC162OrdinaryBoots = 3
    } } else { $null }
    c164Contract = if ($PhaseC164) { [ordered]@{
        nativeFileExplorerDispatch = 'shared bounded resolver; .txt launches Managed Notes with document activation; .log/.cfg/.ini Notepad, .png Image Viewer, and .img disk-image branches remain in native File Explorer'
        managedFileExplorerId = 'com.guidexos.apps.managed.fileexplorer'
        managedNotesId = 'com.guidexos.apps.managed.notes'
        associationUsed = 1
        associationCapacity = 16
        associationTableBytes = $script:c164AssociationTableBytes
        managedHeap = if ($PhaseC164) { 'Primary8MiB fixed' } else { $null }
        associationFocusedCases = $script:c164AssociationCases
        extensionMatching = 'ASCII case-insensitive; last dot in basename; .TXT equals .txt; trailing dot/no extension unsupported; leading dot alone is not an extension'
        activationKind = 'None=0, Document=1; launch context is copied into the bounded managed app context and consumed at Notes launch'
        activationPathCapacityBytes = 96
        explicitDocumentOverridesC155Restore = $true
        notesExternalOpen = 'existing bounded C151 VFS read and C152 document state; clean saved revision; no undo/redo; caret, anchor and viewport reset'
        sourceLifecycle = 'one active managed surface; successful fresh Notes launch closes Managed File Explorer; failed launch retains source and selection'
        unsupportedFileBehavior = 'no launch; bounded status; source and selection retained'
        activationContextFocusedCases = $script:c164ActivationContextCases
        notesDocumentFocusedCases = $script:c164NotesStateCases
        activationStressBoot3 = $production[2].FileActivationScenario.Activations
        distinctNotesLifetimesBoot3 = $production[2].FileActivationScenario.DistinctNotes
        distinctExplorerLifetimesBoot3 = $production[2].FileActivationScenario.DistinctExplorers
        precedenceBoot3 = $production[2].FileActivationScenario.Precedence
        boot1EditUndoRestoresClean = $production[0].FileActivationScenario.Activations -eq 1
        boot2SaveReopenAndUnsupported = $production[1].FileActivationScenario.Save -and $production[1].FileActivationScenario.Unsupported
        nativeFileExplorerLaunchPreserved = $true
        abiVersion = 3
        hostTableBytes = 120
        settingsFormatVersion = 2
        proofBoots = @($production | ForEach-Object { $_.Status })
        ordinaryBoots = @($ordinaryRecords.ToArray() | ForEach-Object { $_.Status })
        c163BaselineManifest = $c163BaselineManifestPath
    } } else { $null }
    application = $applicationManifest
    taskManager = if ($PhaseC162) { [ordered]@{
        focusedC161Cases = $c161FocusedTestCases
        wrapperSnapshotCallsPerFocusedStress = 1000
        productionRefreshCountBoot3 = $c161ProductionRefreshes
        lifecycleLaunchCountBoot3 = $c161ManagerLaunches
        lifecycleCloseCountBoot3 = $c161ManagerCloses
        lifecycleUniqueIdentitiesBoot3 = $true
        finalSnapshot = $c161FinalSnapshot
        closeApplication = $c162CloseProof
    } } elseif ($PhaseC161) { [ordered]@{
        focusedCases = $c161FocusedTestCases
        wrapperSnapshotCallsPerFocusedStress = 1000
        productionRefreshCountBoot3 = $c161ProductionRefreshes
        lifecycleLaunchCountBoot3 = $c161ManagerLaunches
        lifecycleCloseCountBoot3 = $c161ManagerCloses
        lifecycleUniqueIdentitiesBoot3 = $true
        finalSnapshot = $c161FinalSnapshot
    } } else { $null }
    nativeAot = [ordered]@{
        compositeElf = $reportedCompositeElf
        compositeSha256 = $reportedCompositeHash
        cleanProductionCompositeSha256 = if ($PhaseC164) { $canonicalCompositeHash } else { $null }
        cleanProductionKernelSha256 = if ($PhaseC164) { $postC160KernelHash } else { $null }
        cleanProductionRamdiskSha256 = if ($PhaseC164) { $postC160RamdiskHash } else { $null }
        proofCompositeElf = $compositeElf
        proofCompositeSha256 = $compositeHash
        proofKernel = $proofKernel
        proofKernelSha256 = $proofKernelHash
        proofRamdisk = $proofRamdisk
        proofRamdiskSha256 = $proofRamdiskHash
        c160StartingKernelSha256 = if ($PhaseC161) { $canonicalKernelHash } else { $null }
        c160StartingEspKernelSha256 = if ($PhaseC161) { $espKernelHash } else { $null }
        c160StartingRamdiskSha256 = if ($PhaseC161) { $ramdiskHash } else { $null }
        c161CleanProductionKernelSha256 = if ($PhaseC161) { $postC160KernelHash } else { $null }
        c161CleanProductionRamdiskSha256 = if ($PhaseC161) { $postC160RamdiskHash } else { $null }
        c162CleanProductionKernelSha256 = if ($PhaseC162) { $postC160KernelHash } else { $null }
        c162CleanProductionRamdiskSha256 = if ($PhaseC162) { $postC160RamdiskHash } else { $null }
        heap = if ($PhaseC164) { 'Primary8MiB' } else { 'Primary4MiB' }
        abiVersion = $reportedAbiVersion
        abiTableBytes = $reportedAbiSize
        legacyPrefixBytes = 104
        snapshotCallbackOffset = 104
        settingsFormatVersion = 2
        newHostCallAdded = [bool]($PhaseC162 -and -not $PhaseC163)
        applicationCloseCallbackOffset = if ($PhaseC162) { 112 } else { $null }
        v2PrefixBytes = if ($PhaseC162) { 112 } else { $null }
        reflectionAdded = $false
        proofOnlyInstrumentationExcludedFromCleanCompositeAndKernel = if ($PhaseC161) { $true } else { $null }
        floatingPointCalculatorSupport = $false
        c128LifecycleSuite = 'unverified; no result claimed'
    }
    regressions = [ordered]@{
        C156 = 'modifier decode 10/10; shortcut routing 15/15; live Calculator Ctrl+9 ignored; modifier balance verified'
        C129 = 'production Calculator and Task Manager Tab/Shift+Tab passed with Shift released; standalone C129 run not claimed by this runner'
        C150 = if ($PhaseC162) { 'managed lifecycle 8/8; canonical return target 10/10; Notes, Managed Calculator, and Managed Task Manager coexist in the bounded three-surface model' } else { 'managed lifecycle 8/8; canonical return target 10/10; Calculator clean close/fresh launch and Notes -> Calculator -> Notes replacement verified' }
        C154 = 'clipboard test PASS; Calculator lifetime kept shared clipboard unchanged'
        C157 = if ($PhaseC161) { 'Notes New-document suite plus production Ctrl+N, edit, Copy, Task Manager launch/refresh clipboard-preservation checks PASS; C154 Paste suite PASS separately' } else { 'Notes New-document suite PASS before App Model transition' }
        ButtonFocus = 'C158 focused routing suite covers all 18 controls, exact-once Space/Enter behavior, Tab and Shift+Tab'
        C160 = if ($PhaseC160) { 'AppManager identity 29/29; native snapshot 34/34 and 1000-call stress; managed snapshot 22/22 and 1000-call stress; ABI v1 prefix and malformed-table fixtures PASS; shell, Calculator, Task Manager, Notes and managed logical application records verified' } else { 'not run' }
        C161 = if ($PhaseC163) { "focused UI/snapshot $c161FocusedTestCases cases and 1000 wrapper calls PASS; live boot 3 Task Manager refresh and File Explorer identity observation PASS; accepted C162 baseline retains the prior 25-cycle/100-refresh Task Manager stress" } elseif ($PhaseC161) { "focused UI/snapshot $c161FocusedTestCases cases PASS; 1000 wrapper calls PASS; boot 3 $c161ProductionRefreshes real Ctrl+R refreshes PASS; 25 unique Task Manager launches and 25 closes PASS; self identity stable and active; pointer selection, real wheel, Refresh button, Ctrl+R, Tab/Shift+Tab, close and relaunch PASS" } else { 'not run' }
        C162 = if ($PhaseC163) { "accepted C162 baseline manifest validated with all 3 proof and 3 ordinary boots; current C163 boot 3 exercised managed Calculator close by exact C160 identity; ABI v3 callback offset 112, v2 NotSupported; no force-kill or process/thread semantics" } elseif ($PhaseC162) { "AppManager close $($c162CloseProof.nativeAppManagerCloseCases) focused cases and 100 mixed close requests PASS; wrapper $($c162CloseProof.managedWrapperCases) cases; native boundary 3 cases; boot 1 Managed Calculator cancel/close/relaunch PASS; boot 2 native Calculator close/relaunch plus shell/self protection PASS; boot 3 $($c162CloseProof.managedCalculatorCloseCyclesBoot3) Calculator close/relaunch cycles PASS; ABI v3 callback offset 112, v2 NotSupported; no force-kill or process/thread semantics" } else { 'not run' }
        C163 = if ($PhaseC163) { "real VFS browsing PASS; 48 focused cases, 1000 refreshes and 100 navigation iterations PASS; boot 3 $($production[2].FileExplorerScenario.LaunchCount - 1) unique launch/browse/close cycles PASS; three proof boots and three ordinary boots PASS; no filesystem mutation" } else { 'not run' }
        C164 = if ($PhaseC164) { "bounded 16-entry file association table ($($script:c164AssociationTableBytes) bytes); $($script:c164AssociationCases) native resolver cases plus 1000 resolver calls; $($script:c164ActivationContextCases) activation-context cases; $($script:c164NotesStateCases) Notes/VFS/document-state cases; boot 1 real load/edit/undo PASS; boot 2 save/reopen/unsupported PASS; boot 3 $($production[2].FileActivationScenario.Activations) distinct fresh File Explorer-to-Notes activations with explicit-session precedence PASS; proof and clean ordinary boots 3/3 each" } else { 'not run' }
        C137 = if ($PhaseC161) { 'standalone suite 46/46 PASS; real QMP wheel-down moved the Task Manager ListBox viewport; shared NaturalScroll and ScrollLinesPerNotch policy used' } else { 'not run' }
    }
    productionBoots = @($productionRecords.ToArray())
    ordinaryBoots = @($ordinaryRecords.ToArray())
    restoration = [ordered]@{
        canonicalKernelBefore = $canonicalKernelHash
        espKernelBefore = $espKernelHash
        protectedRamdiskBefore = $ramdiskHash
        canonicalKernelAfter = Get-Hash $kernelPath
        espKernelAfter = Get-Hash $espKernelPath
        protectedRamdiskAfter = Get-Hash $protectedRamdiskPath
        restoredByteForByte = if ($PhaseC160) { $false } else { $script:restored }
        canonicalPostPhaseProductsVerified = if ($PhaseC160) { $script:restored } else { $null }
        preC160KernelSha256 = if ($PhaseC160) { $canonicalKernelHash } else { $null }
        postC160KernelSha256 = if ($PhaseC160) { $postC160KernelHash } else { $null }
        preC161KernelSha256 = if ($PhaseC161) { $canonicalKernelHash } else { $null }
        finalPostC161KernelSha256 = if ($PhaseC161) { $postC160KernelHash } else { $null }
        preC160RamdiskSha256 = if ($PhaseC160) { $ramdiskHash } else { $null }
        postC160RamdiskSha256 = if ($PhaseC160) { $postC160RamdiskHash } else { $null }
        preC161RamdiskSha256 = if ($PhaseC161) { $ramdiskHash } else { $null }
        finalPostC161RamdiskSha256 = if ($PhaseC161) { $postC160RamdiskHash } else { $null }
        startingC162KernelSha256 = if ($PhaseC162) { $canonicalKernelHash } else { $null }
        finalPostC162KernelSha256 = if ($PhaseC162) { $postC160KernelHash } else { $null }
        startingC162RamdiskSha256 = if ($PhaseC162) { $ramdiskHash } else { $null }
        finalPostC162RamdiskSha256 = if ($PhaseC162) { $postC160RamdiskHash } else { $null }
        espKernelMatchesPostC160 = if ($PhaseC160) { (Get-Hash $espKernelPath) -eq $postC160KernelHash } else { $null }
        espKernelMatchesPostC161 = if ($PhaseC161) { (Get-Hash $espKernelPath) -eq $postC160KernelHash } else { $null }
        protectedRamdiskUpdatedForAbiV2 = if ($PhaseC160) { $postC160RamdiskHash -ne $ramdiskHash } else { $null }
        protectedRamdiskUpdatedForC161Composite = if ($PhaseC161 -and -not $PhaseC162) { $postC160RamdiskHash -ne $ramdiskHash } else { $null }
        protectedRamdiskUpdatedForC162Composite = if ($PhaseC162) { $postC160RamdiskHash -ne $ramdiskHash } else { $null }
        proofMediaIsolated = $true
    }
    finalState = [ordered]@{
        calculatorManagedGeneration = $script:finalCalculatorGeneration
        nativeLaunchGenerationHex = $script:finalNativeLaunchGeneration
        activeApplicationId = if ($PhaseC163) { 'com.guidexos.apps.managed.fileexplorer' } elseif ($PhaseC161) { 'com.guidexos.apps.managed.taskmanager' } else { 'com.guidexos.apps.managed.calculator' }
        display = if ($PhaseC163) { $null } elseif ($PhaseC162) { '56' } else { '0' }
        phase = if ($PhaseC163) { 'Browsing' } elseif ($PhaseC162) { 'ResultDisplayed' } else { 'EnteringLeft' }
        registeredControls = if ($PhaseC163) { 5 } elseif ($PhaseC161) { 4 } else { 18 }
        control = 'released'
        controlEventsBalanced = $controlBalanced
        shift = 'released'
        shiftEventsBalanced = $shiftBalanced
        modalOwner = 'none'
        popupCapture = 'none'
        dragOwner = 'none'
        settingsFormatVersion = 2
        hostAbiVersion = $reportedAbiVersion
        hostAbiTableBytes = $reportedAbiSize
        applicationSnapshot = $c160FinalSnapshot
        managedTaskManagerSnapshot = $c161FinalSnapshot
        taskManagerControlCapacity = if ($PhaseC161) { 4 } else { $null }
        sharedControlHostMaximum = if ($PhaseC161) { 20 } else { $null }
        managedFileExplorer = if ($PhaseC163) { [ordered]@{
            applicationId = 'com.guidexos.apps.managed.fileexplorer'
            lifetimeId = $production[2].FileExplorerScenario.Identity
            path = '/system/apps'
            selectedEntry = $null
            viewport = 0
            controlCount = 5
            status = 'Select an entry'
        } } else { $null }
    }
}
$manifestPath = Join-Path $EvidenceRoot $manifestFile
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding ASCII
$ordinaryManifest = [ordered]@{
    schemaVersion = 1
    phase = if ($PhaseC164) { 'C164-ordinary-post-phase' } elseif ($PhaseC163) { 'C163-ordinary-post-phase' } elseif ($PhaseC162) { 'C162-ordinary-post-phase' } elseif ($PhaseC161) { 'C161-ordinary-post-phase' } elseif ($PhaseC160) { 'C160-ordinary-post-phase' } else { 'C158-ordinary-restoration' }
    status = 'PASS'
    protected = $manifest.restoration
    ordinaryBoots = @($ordinaryRecords.ToArray())
}
$ordinaryManifestPath = Join-Path $EvidenceRoot $ordinaryManifestFile
$ordinaryManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ordinaryManifestPath -Encoding ASCII
Write-Host "$phaseLabel outcome=A production=3/3 ordinary=3/3 evidence=$EvidenceRoot manifest=$manifestPath ordinaryManifest=$ordinaryManifestPath" -ForegroundColor Green
