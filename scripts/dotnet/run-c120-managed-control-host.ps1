param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$CompositeElfPath = "",
    [string]$PythonExe = "",
    [int]$FreshBootCount = 3,
    [int]$TimeoutSeconds = 360,
    [ValidateSet("C120", "C121", "C122", "C123", "C124", "C125", "C126", "C127", "C128", "C129", "C130", "C131", "C132", "C133", "C134", "C135", "C136", "C137", "C138", "C139", "C140", "C141")]
    [string]$ProofPhase = "C120",
    [ValidateSet("Production", "FocusedApi", "FocusedHost")]
    [string]$C135ProofMode = "Production",
    [switch]$SkipManagedBuild,
    [switch]$SkipKernelBuild,
    [switch]$IncrementalKernelBuild,
    [switch]$SkipFocusedTests,
    [switch]$AllowBoundedHostDefect,
    [switch]$SkipQemu
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($FreshBootCount -lt 3) { throw "Managed control proof requires at least three fresh boots." }
if ($TimeoutSeconds -lt 10) { throw "TimeoutSeconds must be at least 10." }
$isC121 = $ProofPhase -eq "C121"
$isC122 = $ProofPhase -eq "C122"
$isC123 = $ProofPhase -eq "C123"
$isC124 = $ProofPhase -eq "C124"
$isC125 = $ProofPhase -eq "C125"
$isC126 = $ProofPhase -eq "C126"
$isC127 = $ProofPhase -eq "C127"
$isC128 = $ProofPhase -eq "C128"
$isC129 = $ProofPhase -eq "C129"
$isC130 = $ProofPhase -eq "C130"
$isC131 = $ProofPhase -eq "C131"
$isC132 = $ProofPhase -eq "C132"
$isC133 = $ProofPhase -eq "C133"
$isC134 = $ProofPhase -eq "C134"
$isC135 = $ProofPhase -eq "C135"
$isC136 = $ProofPhase -eq "C136"
$isC137 = $ProofPhase -eq "C137"
$isC140 = $ProofPhase -eq "C140"
$isC141 = $ProofPhase -eq "C141"
$isC139 = $ProofPhase -eq "C139"
$isC138 = $ProofPhase -eq "C138" -or $isC139
$isC135FocusedApi = $isC135 -and $C135ProofMode -eq "FocusedApi"
$isC135FocusedHost = $isC135 -and $C135ProofMode -eq "FocusedHost"
if (-not $isC135 -and $C135ProofMode -ne "Production") {
    throw "C135ProofMode applies only to ProofPhase C135."
}
$phaseLower = $ProofPhase.ToLowerInvariant()

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$startHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$startSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$startBranch = (& git -C $RepoRoot branch --show-current).Trim()
$startUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$startAheadBehind = if ($startUpstream) {
    (& git -C $RepoRoot rev-list --left-right --count "HEAD...$startUpstream").Trim()
} else { "" }
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = if ($isC141) {
        Join-Path $RepoRoot "out\dotnet\c141-managed-vertical-stack"
    } elseif ($isC140) {
        Join-Path $RepoRoot "out\dotnet\c140-managed-scrollview"
    } elseif ($isC139) {
        Join-Path $RepoRoot "out\dotnet\c139-shared-scroll-viewport"
    } elseif ($isC138) {
        Join-Path $RepoRoot "out\dotnet\c138-managed-scrollbar"
    } elseif ($isC137) {
        Join-Path $RepoRoot "out\dotnet\c137-mouse-wheel-scrolling"
    } elseif ($isC136) {
        Join-Path $RepoRoot "out\dotnet\c136-secondary-pointer-context-menu"
    } elseif ($isC135) {
        Join-Path $RepoRoot "out\dotnet\c135-managed-popup-menu"
    } elseif ($isC134) {
        Join-Path $RepoRoot "out\dotnet\c134-transient-popup-routing"
    } elseif ($isC133) {
        Join-Path $RepoRoot "out\dotnet\c133-managed-combobox"
    } elseif ($isC132) {
        Join-Path $RepoRoot "out\dotnet\c132-managed-radiobutton"
    } elseif ($isC131) {
        Join-Path $RepoRoot "out\dotnet\c131-managed-checkbox"
    } elseif ($isC130) {
        Join-Path $RepoRoot "out\dotnet\c130-c127-wrapper"
    } elseif ($isC129) {
        Join-Path $RepoRoot "out\dotnet\c011ec129-shift-tab-input-transport"
    } elseif ($isC128) {
        Join-Path $RepoRoot "out\dotnet\c011ec128-managed-panel-lifecycle"
    } elseif ($isC127) {
        Join-Path $RepoRoot "out\dotnet\c011ec127-managed-panel"
    } elseif ($isC126) {
        Join-Path $RepoRoot "out\dotnet\c011ec126-managed-group-box"
    } elseif ($isC125) {
        Join-Path $RepoRoot "out\dotnet\c011ec125-managed-progress-bar"
    } elseif ($isC124) {
        Join-Path $RepoRoot "out\dotnet\c011ec124-managed-radio-button"
    } elseif ($isC123) {
        Join-Path $RepoRoot "out\dotnet\c011ec123-managed-separator"
    } elseif ($isC122) {
        Join-Path $RepoRoot "out\dotnet\c011ec122-managed-label"
    } elseif ($isC121) {
        Join-Path $RepoRoot "out\dotnet\c011ec121-managed-checkbox"
    } else {
        Join-Path $RepoRoot "out\dotnet\c011ec120-managed-control-host"
    }
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot "out\dotnet")).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $EvidenceRoot.StartsWith($allowedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Managed control evidence must remain under $allowedRoot"
}

$buildRoot = Join-Path $EvidenceRoot "build"
$compositeBuildRoot = Join-Path $buildRoot "composite"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot ("staging\ramdisk-{0}.img" -f $phaseLower)
$buildScript = Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"
$stagingScript = Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"
$kernelPath = Join-Path $RepoRoot "kernel\build\amd64\bin\kernel.elf"
$bootloaderPath = Join-Path $RepoRoot "guideXOSBootLoader\x64\Release\guideXOSBootLoader.exe"

function Invoke-Checked([string]$FilePath, [string[]]$Arguments) {
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
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
    return $null
}

function Get-Hash([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        try {
            $hashCommand = Get-Command Get-FileHash -ErrorAction SilentlyContinue
            if ($hashCommand) {
                return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
            }
            $sha256 = [System.Security.Cryptography.SHA256]::Create()
            $stream = [System.IO.File]::OpenRead($Path)
            try {
                return ([System.BitConverter]::ToString($sha256.ComputeHash($stream)) -replace '-', '').ToUpperInvariant()
            }
            finally {
                $stream.Dispose()
                $sha256.Dispose()
            }
        }
        catch {
            if ($attempt -eq 9) { throw }
            Start-Sleep -Milliseconds 250
        }
    }
    return $null
}

function Quote-QemuValue([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Stage-Esp([string]$Esp, [string]$Kernel, [string]$Bootloader, [string]$Ramdisk) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Esp "EFI\BOOT") | Out-Null
    Copy-Item -LiteralPath $Bootloader -Destination (Join-Path $Esp "EFI\BOOT\BOOTX64.EFI") -Force
    Copy-Item -LiteralPath $Kernel -Destination (Join-Path $Esp "kernel.elf") -Force
    Copy-Item -LiteralPath $Ramdisk -Destination (Join-Path $Esp "ramdisk.img") -Force
}

function Invoke-C120Boot([string]$Esp, [string]$Serial, [string]$Stdout,
                         [string]$Stderr, [string]$Qemu, [string]$Ovmf) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $Ovmf)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $Esp) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $Serial)),
        "-boot", "order=c", "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host")
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -WindowStyle Hidden -PassThru
    $timedOut = $false
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $Serial) {
                $partial = Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
                $resultPattern = if ($isC141) {
                    '(?m)^\[C102-MANAGED-OUTPUT\] C141-TESTS .*result=PASS'
                } elseif ($isC140) {
                    '(?m)^\[C102-MANAGED-OUTPUT\] C140-TESTS .*result=PASS'
                } elseif ($isC136) {
                    # C136 drives the proof from QMP and terminates after the
                    # bounded event sequence.  The stale-release marker is
                    # the final serial acknowledgement; there is no direct
                    # managed result shortcut.
                    '(?m)^\[C102-MANAGED-OUTPUT\].*C136-SECONDARY-UP stale=cancelled menu=none capture=none result=PASS'
                } elseif ($isC135FocusedApi -or $isC135FocusedHost) {
                    '(?m)^\[C135-FOCUSED-RESULT\] mode=(?:api|host) outcome=(?:PASS|FAIL)'
                } elseif ($isC135 -or $isC133 -or $isC134) {
                    '(?m)^\[C133-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC132) {
                    '(?m)^\[C132-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC131) {
                    '(?m)^\[C131-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC130 -or $isC127) {
                    '(?m)^\[C127-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC129) {
                    '(?m)^\[C102-MANAGED-OUTPUT\] C129-RESULT outcome=(?:PASS|FAIL)'
                } elseif ($isC128) {
                    '(?m)^\[C128-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC127) {
                    '(?m)^\[C127-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC126) {
                    '(?m)^\[C126-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC125) {
                    '(?m)^\[C125-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC124) {
                    '(?m)^\[C124-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC123) {
                    '(?m)^\[C123-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC122) {
                    '(?m)^\[C122-RESULT\] outcome=(?:PASS|FAIL)'
                } elseif ($isC121) {
                    '(?m)^\[C121-RESULT\] outcome=(?:PASS|FAIL)'
                } else {
                    '(?m)^\[C120-RESULT\] outcome=(?:PASS|FAIL)'
                }
                if ($partial -match $resultPattern) { break }
            }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        $timedOut = -not $process.HasExited -and (Get-Date) -ge $deadline
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    $process.Refresh()
    [pscustomobject]@{
        serial = if (Test-Path -LiteralPath $Serial) { Get-Content -LiteralPath $Serial -Raw } else { "" }
        serialPath = $Serial; serialSha256 = Get-Hash $Serial
        stdoutPath = $Stdout; stderrPath = $Stderr; qemuExitCode = $process.ExitCode
        timedOut = $timedOut
    }
}

function Read-C129QmpResponse([System.IO.Stream]$Stream, [int]$Seconds = 2) {
    $builder = [System.Text.StringBuilder]::new()
    $buffer = New-Object byte[] 4096
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        if ($Stream.DataAvailable) {
            $count = $Stream.Read($buffer, 0, $buffer.Length)
            if ($count -gt 0) {
                [void]$builder.Append([System.Text.Encoding]::ASCII.GetString($buffer, 0, $count))
                if ($builder.ToString().TrimEnd().EndsWith('}')) { break }
            }
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
    return $builder.ToString()
}

function Send-C129QmpEvent([int]$Port, [string]$QCode, [bool]$Down, [string]$LogPath) {
    $lastError = $null
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        $client = New-Object System.Net.Sockets.TcpClient
        try {
            $client.Connect("127.0.0.1", $Port)
            $stream = $client.GetStream()
            $stream.ReadTimeout = 100
            $greeting = Read-C129QmpResponse $stream 2
            $capabilities = '{"execute":"qmp_capabilities"}' + "`n"
            $capabilityBytes = [System.Text.Encoding]::ASCII.GetBytes($capabilities)
            $stream.Write($capabilityBytes, 0, $capabilityBytes.Length)
            $stream.Flush()
            $capabilityResponse = Read-C129QmpResponse $stream 2
            $request = [ordered]@{
                execute = "input-send-event"
                arguments = [ordered]@{
                    events = @([ordered]@{
                        type = "key"
                        data = [ordered]@{
                            down = $Down
                            key = [ordered]@{ type = "qcode"; data = $QCode }
                        }
                    })
                }
            } | ConvertTo-Json -Compress -Depth 10
            $bytes = [System.Text.Encoding]::ASCII.GetBytes($request + "`n")
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush()
            $response = Read-C129QmpResponse $stream 2
            Add-Content -LiteralPath $LogPath -Value ("event=qcode:{0}:down={1}`ngreeting={2}`ncapabilities={3}`nresponse={4}" -f $QCode, $Down, $greeting, $capabilityResponse, $response) -Encoding ASCII
            if ($response -match '"error"') {
                throw "QMP input-send-event returned an error: $response"
            }
            return
        }
        catch {
            $lastError = $_.Exception.Message
        }
        finally {
            if ($client) { $client.Dispose() }
        }
        Start-Sleep -Milliseconds 250
    }
    throw "C129 QEMU input event failed: qcode=$QCode down=$Down ($lastError)"
}

function Send-C136QmpEvents([int]$Port, [object[]]$Events, [string]$LogPath) {
    $lastError = $null
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        $client = New-Object System.Net.Sockets.TcpClient
        try {
            $client.Connect("127.0.0.1", $Port)
            $stream = $client.GetStream()
            $stream.ReadTimeout = 100
            $greeting = Read-C129QmpResponse $stream 2
            $capabilities = '{"execute":"qmp_capabilities"}' + "`n"
            $capabilityBytes = [System.Text.Encoding]::ASCII.GetBytes($capabilities)
            $stream.Write($capabilityBytes, 0, $capabilityBytes.Length)
            $stream.Flush()
            $capabilityResponse = Read-C129QmpResponse $stream 2
            foreach ($event in $Events) {
                $eventPayloadList = [System.Collections.Generic.List[object]]::new()
                if ($event -is [System.Array]) {
                    foreach ($nestedEvent in $event) { $eventPayloadList.Add($nestedEvent) }
                } else {
                    $eventPayloadList.Add($event)
                }
                $request = [ordered]@{
                    execute = "input-send-event"
                    arguments = [ordered]@{ events = $eventPayloadList.ToArray() }
                } | ConvertTo-Json -Compress -Depth 10
                $bytes = [System.Text.Encoding]::ASCII.GetBytes($request + "`n")
                $stream.Write($bytes, 0, $bytes.Length)
                $stream.Flush()
                $response = Read-C129QmpResponse $stream 2
                Add-Content -LiteralPath $LogPath -Value ("events={0}`ngreeting={1}`ncapabilities={2}`nresponse={3}" -f $request, $greeting, $capabilityResponse, $response) -Encoding ASCII
                if ($response -match '"error"') {
                    throw "QMP input-send-event returned an error: $response"
                }
                # Let the guest service each explicit PS/2 packet before the
                # next bounded event arrives. Without this small yield, a
                # long calibration move can be coalesced by QEMU before IRQ12
                # reaches the production input path.
                Start-Sleep -Milliseconds 300
            }
            return
        }
        catch {
            $lastError = $_.Exception.Message
        }
        finally {
            if ($client) { $client.Dispose() }
        }
        Start-Sleep -Milliseconds 250
    }
    throw "C136 QEMU input event failed ($lastError)"
}

function Convert-C136ScreenCoordinate([int]$Value, [int]$Maximum) {
    if ($Value -lt 0) { throw "C136 screen coordinate is invalid: $Value" }
    return [int][Math]::Round(($Value * 65535.0) / [Math]::Max(1, $Maximum - 1))
}

function New-C136RelativeMove([int]$DeltaX, [int]$DeltaY) {
    $events = [System.Collections.Generic.List[object]]::new()
    while ($DeltaX -ne 0) {
        $step = [Math]::Sign($DeltaX) * [Math]::Min([Math]::Abs($DeltaX), 40)
        $events.Add([ordered]@{
                type = "rel"; data = [ordered]@{ axis = "x"; value = $step } })
        $DeltaX -= $step
    }
    while ($DeltaY -ne 0) {
        $step = [Math]::Sign($DeltaY) * [Math]::Min([Math]::Abs($DeltaY), 40)
        $events.Add([ordered]@{
                type = "rel"; data = [ordered]@{ axis = "y"; value = $step } })
        $DeltaY -= $step
    }
    return $events.ToArray()
}

function New-C136Button([string]$Button, [bool]$Down) {
    return [ordered]@{ type = "btn"; data = [ordered]@{ button = $Button; down = $Down } }
}

function New-C137Wheel([int]$Delta) {
    if ($Delta -eq 0) { throw "C137 wheel delta cannot be zero." }
    return [ordered]@{
        type = "btn"
        data = [ordered]@{
            # QEMU's PS/2 wheel buttons are sign-inverted relative to the
            # normalized managed delta: wheel-down produces +1 and wheel-up
            # produces -1 in the IntelliMouse packet.
            button = if ($Delta -gt 0) { "wheel-down" } else { "wheel-up" }
            down = $true
        }
    }
}

function New-C136Key([string]$QCode, [bool]$Down) {
    return [ordered]@{ type = "key"; data = [ordered]@{ down = $Down; key = [ordered]@{ type = "qcode"; data = $QCode } } }
}

function Get-C136HexField([string]$Serial, [string]$Field) {
    $match = [regex]::Match($Serial, "(?m)^\[C136-TARGET\].*\b$Field=([0-9A-Fa-f]+)")
    if (-not $match.Success) { throw "C136 target marker did not contain $Field." }
    return [Convert]::ToInt32($match.Groups[1].Value, 16)
}

function Get-C136NativePointer([string]$Serial) {
    $matches = [regex]::Matches(
        $Serial,
        "(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down x=([0-9A-Fa-f]+) y=([0-9A-Fa-f]+)")
    if ($matches.Count -eq 0) { throw "C136 secondary native pointer marker was not observed." }
    $match = $matches[$matches.Count - 1]
    return [pscustomobject]@{
        x = [Convert]::ToInt32($match.Groups[1].Value, 16)
        y = [Convert]::ToInt32($match.Groups[2].Value, 16)
    }
}

function Invoke-C136Boot([string]$Esp, [string]$Serial, [string]$Stdout,
                          [string]$Stderr, [string]$MonitorLog, [int]$MonitorPort,
                          [string]$Qemu, [string]$Ovmf) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $Ovmf)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $Esp) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $Serial)),
        "-qmp", ("tcp:127.0.0.1:{0},server,nowait" -f $MonitorPort),
        "-boot", "order=c", "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host")
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -WindowStyle Hidden -PassThru
    $commandIndex = 0
    $previousMarker = ""
    $proofStarted = $false
    $targetReady = $false
    $commandList = $null
    $previousSerialLength = 0
    $c136CalibrationAttempt = 0
    $timedOut = $false
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 150
            $partial = if (Test-Path -LiteralPath $Serial) {
                Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
            } else { "" }
            if (-not $proofStarted -and $partial -match '(?m)^\[C136-PROOF\].*transport=physical-qemu result=PASS') {
                $proofStarted = $true
            }
            if ($proofStarted -and -not $targetReady -and $partial -match '(?m)^\[C136-TARGET\].*result=PASS') {
                $targetReady = $true
            }
            if ($proofStarted -and $targetReady -and $commandIndex -eq 0 -and
                    $null -eq $commandList) {
                # Native pointer acknowledgements are window-local, while
                # QMP relative packets move the same pointer in screen
                # space.  Relative deltas are invariant under the window
                # origin, so keep the proof geometry in local coordinates.
                $targetLocalX = Get-C136HexField $partial "localX"
                $targetLocalY = Get-C136HexField $partial "localY"
                $menuOffsetX = (Get-C136HexField $partial "menuItemX") -
                    (Get-C136HexField $partial "screenX")
                $menuOffsetY = (Get-C136HexField $partial "menuItemY") -
                    (Get-C136HexField $partial "screenY")
                $outsideX = $targetLocalX + 50
                $outsideY = $targetLocalY + 80
                $commands = @(
                    # A short explicit relative move is reliable on this
                    # QEMU PS/2 source. The first secondary click observes
                    # the resulting guest coordinate; later attempts correct
                    # from that observed coordinate rather than assuming the
                    # host cursor starts at framebuffer center.
                    [pscustomobject]@{ events = (New-C136RelativeMove -92 -40); marker = ''; phase = 'calibration-move' },
                    [pscustomobject]@{ events = @(New-C136Button "right" $true); marker = '(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS'; phase = 'calibration-down' },
                    [pscustomobject]@{ events = @(New-C136Button "right" $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-SECONDARY-UP.*result=PASS'; phase = 'calibration-up' })
                $commandList = $commands
            }
            if ($proofStarted -and $targetReady -and $null -ne $commandList -and $commandIndex -lt $commandList.Count) {
                $command = $commandList[$commandIndex]
                $markerSatisfied = [string]::IsNullOrEmpty($previousMarker)
                if (-not $markerSatisfied) {
                    if ($isC136) {
                        if ($partial.Length -gt $previousSerialLength) {
                            $newSerial = $partial.Substring($previousSerialLength)
                            $markerSatisfied = $newSerial -match $previousMarker
                        }
                    } else {
                        $markerSatisfied = $partial -match $previousMarker
                    }
                }
                if ($markerSatisfied) {
                    Send-C136QmpEvents $MonitorPort $command.events $MonitorLog
                    $previousMarker = $command.marker
                    $previousSerialLength = $partial.Length
                    $commandIndex++

                    if ($isC136 -and $command.phase -eq 'calibration-up') {
                        $afterInput = ""
                        for ($ackWait = 0; $ackWait -lt 12; $ackWait++) {
                            $afterInput = if (Test-Path -LiteralPath $Serial) {
                                Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
                            } else { "" }
                            if ($afterInput -match
                                    '(?m)^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary.*result=PASS') {
                                break
                            }
                            Start-Sleep -Milliseconds 100
                        }
                        $pointer = Get-C136NativePointer $afterInput
                        $contextOpen = $afterInput -match
                            '(?m)^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary.*result=PASS'
                        if ($contextOpen) {
                            $commandList = @(
                                [pscustomobject]@{ events = (New-C136RelativeMove $menuOffsetX $menuOffsetY); marker = ''; phase = 'menu-move' },
                                 [pscustomobject]@{ events = @(New-C136Button "left" $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-COMMAND command=Save count=1 result=PASS'; phase = 'menu-down' },
                                [pscustomobject]@{ events = @(New-C136Button "left" $false); marker = '(?m)^\[C136-NATIVE-INPUT\] button=primary phase=up.*result=PASS'; phase = 'menu-up' },
                                [pscustomobject]@{ events = (New-C136RelativeMove (-$menuOffsetX) (-$menuOffsetY)); marker = ''; phase = 'return-target' },
                                [pscustomobject]@{ events = @(New-C136Button "right" $true); marker = '(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS'; phase = 'target-down' },
                                 [pscustomobject]@{ events = @(New-C136Button "right" $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary.*result=PASS'; phase = 'target-up' },
                                 [pscustomobject]@{ events = @(New-C136Key "down" $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C135-KEYBOARD.*highlight=PASS disabled-skipped=PASS result=PASS'; phase = 'keyboard-down' },
                                [pscustomobject]@{ events = @(New-C136Key "down" $false); marker = ''; phase = 'keyboard-up' },
                                 [pscustomobject]@{ events = @(New-C136Key "ret" $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-COMMAND command=Save count=2 result=PASS'; phase = 'keyboard-commit' },
                                 [pscustomobject]@{ events = @(New-C136Key "ret" $false); marker = ''; phase = 'keyboard-release' },
                                [pscustomobject]@{ events = @(New-C136Button "right" $true); marker = '(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS'; phase = 'escape-down' },
                                 [pscustomobject]@{ events = @(New-C136Button "right" $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary.*result=PASS'; phase = 'escape-open' },
                                 [pscustomobject]@{ events = @(New-C136Key "esc" $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C135-ESCAPE.*cancel=PASS capture=none result=PASS'; phase = 'escape' },
                                 [pscustomobject]@{ events = @(New-C136Key "esc" $false); marker = ''; phase = 'escape-release' },
                                [pscustomobject]@{ events = @(New-C136Button "right" $true); marker = '(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS'; phase = 'outside-down' },
                                 [pscustomobject]@{ events = @(New-C136Button "right" $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary.*result=PASS'; phase = 'outside-open' },
                                [pscustomobject]@{ events = (New-C136RelativeMove ($outsideX - $targetLocalX) ($outsideY - $targetLocalY)); marker = ''; phase = 'outside-move' },
                                 [pscustomobject]@{ events = @(New-C136Button "left" $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C135-OUTSIDE.*close=PASS consumed=PASS underlying=inactive result=PASS'; phase = 'outside-primary-down' },
                                [pscustomobject]@{ events = @(New-C136Button "left" $false); marker = '(?m)^\[C136-NATIVE-INPUT\] button=primary phase=up.*result=PASS'; phase = 'outside-primary-up' },
                                 [pscustomobject]@{ events = (New-C136RelativeMove ($targetLocalX - $outsideX) ($targetLocalY - $outsideY)); marker = ''; phase = 'stale-return' },
                                 [pscustomobject]@{ events = @(New-C136Button "right" $true); marker = '(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS'; phase = 'stale-down' },
                                 [pscustomobject]@{ events = (New-C136RelativeMove ($outsideX - $targetLocalX) ($outsideY - $targetLocalY)); marker = ''; phase = 'stale-move' },
                                 [pscustomobject]@{ events = @(New-C136Button "right" $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-SECONDARY-UP stale=cancelled menu=none capture=none result=PASS'; phase = 'stale-up' })
                            $commandIndex = 0
                            $previousMarker = ''
                            $previousSerialLength = $afterInput.Length
                        } elseif ($c136CalibrationAttempt -lt 6) {
                            $c136CalibrationAttempt++
                            $commandList = @(
                                [pscustomobject]@{ events = (New-C136RelativeMove ($targetLocalX - $pointer.x) ($targetLocalY - $pointer.y)); marker = ''; phase = 'target-move' },
                                [pscustomobject]@{ events = @(New-C136Button "right" $true); marker = '(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS'; phase = 'target-down' },
                                 [pscustomobject]@{ events = @(New-C136Button "right" $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-SECONDARY-UP.*result=PASS'; phase = 'target-up' })
                            $previousMarker = ''
                            $previousSerialLength = $afterInput.Length
                            $commandIndex = 0
                        } else {
                            throw "C136 QMP could not calibrate a secondary pointer position inside the Notes target."
                        }
                    }
                }
            }
            if ($null -ne $commandList -and $commandIndex -ge $commandList.Count) { break }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        $timedOut = -not $process.HasExited -and (Get-Date) -ge $deadline
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    $process.Refresh()
    [pscustomobject]@{
        serial = if (Test-Path -LiteralPath $Serial) { Get-Content -LiteralPath $Serial -Raw } else { "" }
        serialPath = $Serial; serialSha256 = Get-Hash $Serial
        stdoutPath = $Stdout; stderrPath = $Stderr; monitorPath = $MonitorLog
        qemuExitCode = $process.ExitCode; monitorPort = $MonitorPort; timedOut = $timedOut
    }
}

function Get-C137HexField([string]$Serial, [string]$Field) {
    $match = [regex]::Match($Serial, "(?m)^\[C137-TARGET\].*\b$Field=([0-9A-Fa-f]+)")
    if (-not $match.Success) { throw "C137 target marker did not contain $Field." }
    return [Convert]::ToInt32($match.Groups[1].Value, 16)
}

function Invoke-C137Boot([string]$Esp, [string]$Serial, [string]$Stdout,
                          [string]$Stderr, [string]$MonitorLog, [int]$MonitorPort,
                          [string]$Qemu, [string]$Ovmf) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $Ovmf)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $Esp) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $Serial)),
        "-qmp", ("tcp:127.0.0.1:{0},server,nowait" -f $MonitorPort),
        "-boot", "order=c", "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host")
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -WindowStyle Hidden -PassThru
    $commandIndex = 0
    $previousMarker = ""
    $previousSerialLength = 0
    $proofStarted = $false
    $targetReady = $false
    $commandList = $null
    $timedOut = $false
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 150
            $partial = if (Test-Path -LiteralPath $Serial) {
                Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
            } else { "" }
            if (-not $proofStarted -and $partial -match
                    '(?m)^\[C137-PROOF\].*transport=physical-qemu result=PASS') {
                $proofStarted = $true
            }
            if ($proofStarted -and -not $targetReady -and $partial -match
                    '(?m)^\[C137-TARGET\].*result=PASS') {
                $targetReady = $true
            }
            if ($proofStarted -and $targetReady -and $null -eq $commandList) {
                $textX = Get-C137HexField $partial "screenTextX"
                $textY = Get-C137HexField $partial "screenTextY"
                $listX = Get-C137HexField $partial "screenListX"
                $listY = Get-C137HexField $partial "screenListY"
                $moveToText = New-C136RelativeMove ($textX - 512) ($textY - 384)
                $moveToList = New-C136RelativeMove ($listX - $textX) ($listY - $textY)
                $moveToListFirstRow = New-C136RelativeMove 0 -44
                $burst = [System.Collections.Generic.List[object]]::new()
                for ($burstIndex = 0; $burstIndex -lt 100; $burstIndex++) {
                    $burst.Add((New-C137Wheel $(if (($burstIndex % 2) -eq 0) { -1 } else { 1 })))
                }
                $commandList = @(
                    [pscustomobject]@{ events = $moveToText; marker = ''; phase = 'text-move' },
                    [pscustomobject]@{ events = @(New-C137Wheel -1); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=-1 before=0 after=3 result=PASS'; phase = 'text-down' },
                    [pscustomobject]@{ events = @(New-C137Wheel 1); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=1 before=3 after=0 result=PASS'; phase = 'text-up' },
                    [pscustomobject]@{ events = @(New-C136Button 'right' $true); marker = '(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS'; phase = 'secondary-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'right' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary.*result=PASS'; phase = 'secondary-open' },
                    [pscustomobject]@{ events = @(New-C137Wheel -1); marker = ''; phase = 'popup-wheel-swallowed' },
                    [pscustomobject]@{ events = @(New-C136Key 'esc' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C135-ESCAPE.*cancel=PASS capture=none result=PASS'; phase = 'popup-close' },
                    [pscustomobject]@{ events = @(New-C136Key 'esc' $false); marker = ''; phase = 'popup-close-release' },
                    [pscustomobject]@{ events = @(New-C137Wheel -1); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=-1 before=0 after=3 result=PASS'; phase = 'text-resume' },
                    [pscustomobject]@{ events = @(New-C136Button 'right' $true); marker = '(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS'; phase = 'secondary-held-down' },
                    [pscustomobject]@{ events = @(New-C137Wheel -1); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=-1 before=3 after=6 result=PASS'; phase = 'secondary-held-wheel' },
                    [pscustomobject]@{ events = @(New-C136Button 'right' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary.*result=PASS'; phase = 'secondary-held-up' },
                    [pscustomobject]@{ events = @(New-C136Key 'esc' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C135-ESCAPE.*cancel=PASS capture=none result=PASS'; phase = 'second-popup-close' },
                    [pscustomobject]@{ events = @(New-C136Key 'esc' $false); marker = ''; phase = 'second-popup-close-release' },
                    [pscustomobject]@{ events = $moveToList; marker = ''; phase = 'list-move' },
                    [pscustomobject]@{ events = @(New-C137Wheel -1); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=ListBox delta=-1 before=0 after=3 selection=0 result=PASS'; phase = 'list-down' },
                    [pscustomobject]@{ events = $moveToListFirstRow; marker = ''; phase = 'list-pointer-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-LIST-POINTER selected=3 viewport=3 result=PASS'; phase = 'list-pointer-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'list-pointer-up' },
                    [pscustomobject]@{ events = $burst.ToArray(); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=ListBox delta=1 before=6 after=3 selection=3 result=PASS'; phase = 'list-burst' },
                    [pscustomobject]@{ events = @(New-C136Key 'f12' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-CLOSE request=PASS capture=none result=PASS'; phase = 'relaunch-down' },
                    [pscustomobject]@{ events = @(New-C136Key 'f12' $false); marker = '(?m)^\[C137-RELAUNCH\] close=PASS relaunch=PASS capture=none result=PASS'; phase = 'relaunch-up' })
            }
            if ($null -ne $commandList -and $commandIndex -lt $commandList.Count) {
                $command = $commandList[$commandIndex]
                $markerSatisfied = [string]::IsNullOrEmpty($previousMarker)
                if (-not $markerSatisfied) {
                    $start = [Math]::Min($previousSerialLength, $partial.Length)
                    $newSerial = $partial.Substring($start)
                    $markerSatisfied = $newSerial -match $previousMarker
                }
                if ($markerSatisfied) {
                    Send-C136QmpEvents $MonitorPort $command.events $MonitorLog
                    $previousMarker = $command.marker
                    $previousSerialLength = $partial.Length
                    $commandIndex++
                }
            }
            if ($null -ne $commandList -and $commandIndex -ge $commandList.Count) {
                break
            }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        $timedOut = -not $process.HasExited -and (Get-Date) -ge $deadline
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    $process.Refresh()
    [pscustomobject]@{
        serial = if (Test-Path -LiteralPath $Serial) { Get-Content -LiteralPath $Serial -Raw } else { "" }
        serialPath = $Serial; serialSha256 = Get-Hash $Serial
        stdoutPath = $Stdout; stderrPath = $Stderr; monitorPath = $MonitorLog
        qemuExitCode = $process.ExitCode; monitorPort = $MonitorPort; timedOut = $timedOut
    }
}

function Get-C138HexField([string]$Serial, [string]$Field) {
    $match = [regex]::Match($Serial, "(?m)^\[C138-TARGET\].*\b$Field=([0-9A-Fa-f]+)")
    if (-not $match.Success) { throw "C138 target marker did not contain $Field." }
    return [Convert]::ToInt32($match.Groups[1].Value, 16)
}

function Get-C138NativePointer([string]$Serial) {
    $matches = [regex]::Matches(
        $Serial,
        "(?m)^\[C138-NATIVE-INPUT\] kind=pointer-move x=([0-9A-Fa-f]+) y=([0-9A-Fa-f]+)")
    if ($matches.Count -eq 0) { throw "C138 native pointer move marker was not observed." }
    $match = $matches[$matches.Count - 1]
    return [pscustomobject]@{
        x = [Convert]::ToInt32($match.Groups[1].Value, 16)
        y = [Convert]::ToInt32($match.Groups[2].Value, 16)
    }
}

function Invoke-C138Boot([string]$Esp, [string]$Serial, [string]$Stdout,
                          [string]$Stderr, [string]$MonitorLog, [int]$MonitorPort,
                          [string]$Qemu, [string]$Ovmf) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $Ovmf)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $Esp) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $Serial)),
        "-qmp", ("tcp:127.0.0.1:{0},server,nowait" -f $MonitorPort),
        "-boot", "order=c", "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host")
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -WindowStyle Hidden -PassThru
    $commandIndex = 0
    $previousMarker = ""
    $previousSerialLength = 0
    $proofStarted = $false
    $targetReady = $false
    $commandList = $null
    $c138Calibrating = $false
    $timedOut = $false
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 150
            $partial = if (Test-Path -LiteralPath $Serial) {
                Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
            } else { "" }
            if (-not $proofStarted -and $partial -match
                    '(?m)^\[C138-PROOF\].*transport=physical-qemu result=PASS') {
                $proofStarted = $true
            }
            if ($proofStarted -and -not $targetReady -and $partial -match
                    '(?m)^\[C138-TARGET\].*result=PASS') {
                $targetReady = $true
            }
            if ($proofStarted -and $targetReady -and $null -eq $commandList) {
                # Managed applications receive client-local coordinates after
                # compositor hit testing.  The proof marker also records
                # screen coordinates for auditability, but QMP deltas must
                # converge on the local control geometry.
                $textX = Get-C138HexField $partial "textBarX"
                $textY = Get-C138HexField $partial "textBarPressY"
                $listX = Get-C138HexField $partial "listBarX"
                $listY = Get-C138HexField $partial "listBarPressY"
                $docX = Get-C138HexField $partial "documentX"
                $docY = Get-C138HexField $partial "documentY"
                $outsideY = Get-C138HexField $partial "outsideY"
                $outsideScreenY = $textY + ($outsideY - $textY)
                $commandList = @(
                    [pscustomobject]@{ events = (New-C136RelativeMove 1 1); marker = ''; phase = 'c138-calibration' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C138-DRAG press=PASS capture=owned result=PASS'; phase = 'text-down' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 ($outsideScreenY - $textY)); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C138-DRAG move=PASS capture=owned result=PASS'; phase = 'text-drag-outside' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C138-DRAG release=PASS capture=none result=PASS'; phase = 'text-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 ($textY - $outsideScreenY)); marker = ''; phase = 'text-return' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C138-TRACK page=PASS result=PASS'; phase = 'track-page' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'track-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($docX - $textX) ($docY - $textY)); marker = ''; phase = 'popup-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'right' $true); marker = '(?m)^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS'; phase = 'popup-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'right' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary.*result=PASS'; phase = 'popup-open' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($textX - $docX) ($textY - $docY)); marker = ''; phase = 'popup-return' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C138-POPUP blocked=PASS scrollbar=inactive result=PASS'; phase = 'popup-block' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'popup-block-release' },
                    [pscustomobject]@{ events = @(New-C136Key 'esc' $true); marker = ''; phase = 'popup-escape' },
                    [pscustomobject]@{ events = @(New-C136Key 'esc' $false); marker = ''; phase = 'popup-escape-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($listX - $textX) 0); marker = ''; phase = 'list-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C138-DRAG press=PASS capture=owned result=PASS'; phase = 'list-down' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 ($outsideScreenY - $textY)); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C138-DRAG move=PASS capture=owned result=PASS'; phase = 'list-drag-outside' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C138-LIST-DRAG release=PASS viewport=bottom selection=preserved result=PASS'; phase = 'list-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove -188 -140); marker = ''; phase = 'list-first-row-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-LIST-POINTER selected=8 viewport=8 result=PASS'; phase = 'list-pointer-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'list-pointer-up' },
                    [pscustomobject]@{ events = @(New-C137Wheel 1); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=ListBox delta=1 before=8 after=5 selection=8 result=PASS'; phase = 'list-wheel' },
                    [pscustomobject]@{ events = @(New-C136Key 'f12' $true); marker = '(?m)^\[C138-RELAUNCH\] close=PASS relaunch=PASS result=PASS'; phase = 'relaunch-down' },
                    [pscustomobject]@{ events = @(New-C136Key 'f12' $false); marker = ''; phase = 'relaunch-up' })
                $c138Calibrating = $true
            }
            if ($c138Calibrating -and $commandIndex -ge 1) {
                $start = [Math]::Min($previousSerialLength, $partial.Length)
                $newSerial = $partial.Substring($start)
                if ($newSerial -match '(?m)^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS') {
                    $pointer = Get-C138NativePointer $partial
                    $commandList[0].events = New-C136RelativeMove (
                        $textX - $pointer.x) ($textY - $pointer.y)
                    $commandList[0].phase = 'text-move'
                    $commandIndex = 0
                    $previousMarker = ''
                    $previousSerialLength = $partial.Length
                    $c138Calibrating = $false
                }
            }
            if ($null -ne $commandList -and $commandIndex -lt $commandList.Count) {
                $command = $commandList[$commandIndex]
                $markerSatisfied = [string]::IsNullOrEmpty($previousMarker)
                if (-not $markerSatisfied) {
                    $start = [Math]::Min($previousSerialLength, $partial.Length)
                    $markerSatisfied = $partial.Substring($start) -match $previousMarker
                }
                if ($markerSatisfied) {
                    Send-C136QmpEvents $MonitorPort $command.events $MonitorLog
                    $previousMarker = $command.marker
                    $previousSerialLength = $partial.Length
                    $commandIndex++
                }
            }
            if ($null -ne $commandList -and -not $c138Calibrating -and
                    $commandIndex -ge $commandList.Count) { break }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        $timedOut = -not $process.HasExited -and (Get-Date) -ge $deadline
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    $process.Refresh()
    [pscustomobject]@{
        serial = if (Test-Path -LiteralPath $Serial) { Get-Content -LiteralPath $Serial -Raw } else { "" }
        serialPath = $Serial; serialSha256 = Get-Hash $Serial
        stdoutPath = $Stdout; stderrPath = $Stderr; monitorPath = $MonitorLog
        qemuExitCode = $process.ExitCode; monitorPort = $MonitorPort; timedOut = $timedOut
    }
}

function Get-C140HexField([string]$Serial, [string]$Field) {
    $match = [regex]::Match($Serial, "(?m)^\[C140-TARGET\].*\b$Field=([0-9A-Fa-f]+)")
    if (-not $match.Success) { throw "C140 target marker did not contain $Field." }
    return [Convert]::ToInt32($match.Groups[1].Value, 16)
}

function Get-C141HexField([string]$Serial, [string]$Field) {
    $match = [regex]::Match($Serial, "(?m)^\[C141-TARGET\].*\b$Field=([0-9A-Fa-f]+)")
    if (-not $match.Success) { throw "C141 target marker did not contain $Field." }
    return [Convert]::ToInt32($match.Groups[1].Value, 16)
}

function Invoke-C141Boot([string]$Esp, [string]$Serial, [string]$Stdout,
                          [string]$Stderr, [string]$MonitorLog, [int]$MonitorPort,
                          [string]$Qemu, [string]$Ovmf) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $Ovmf)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $Esp) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $Serial)),
        "-qmp", ("tcp:127.0.0.1:{0},server,nowait" -f $MonitorPort),
        "-boot", "order=c", "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host")
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -WindowStyle Hidden -PassThru
    $commandIndex = 0
    $previousMarker = ""
    $previousSerialLength = 0
    $proofStarted = $false
    $targetReady = $false
    $commandList = $null
    $calibrating = $false
    $timedOut = $false
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 150
            $partial = if (Test-Path -LiteralPath $Serial) {
                Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
            } else { "" }
            if (-not $proofStarted -and $partial -match
                    '(?m)^\[C141-PROOF\].*transport=physical-qemu result=PASS') {
                $proofStarted = $true
            }
            if ($proofStarted -and -not $targetReady -and $partial -match
                    '(?m)^\[C141-TARGET\].*result=PASS') {
                $targetReady = $true
            }
            if ($proofStarted -and $targetReady -and $null -eq $commandList) {
                $viewX = Get-C141HexField $partial "viewX"
                $viewY = Get-C141HexField $partial "viewY"
                $barX = Get-C141HexField $partial "scrollBarX"
                $barY = Get-C141HexField $partial "scrollBarY"
                $comboX = $viewX + 80
                $comboY = $viewY + 60
                $checkX = $viewX + 80
                $checkY = $viewY + 38
                $barTargetX = $barX + 8
                $barTargetY = $barY + 20
                $buttonX = $viewX + 80
                $buttonY = $viewY + 118
                $tabEvents = [System.Collections.Generic.List[object]]::new()
                for ($tab = 0; $tab -lt 5; $tab++) {
                    $tabEvents.Add((New-C136Key 'tab' $true))
                    $tabEvents.Add((New-C136Key 'tab' $false))
                }
                $commandList = @(
                    [pscustomobject]@{ events = (New-C136RelativeMove 1 1); marker = ''; phase = 'c141-calibration' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C141-POPUP open=PASS arranged-geometry=PASS capture=none result=PASS'; phase = 'combo-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'combo-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 18); marker = ''; phase = 'popup-row-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C141-POPUP follow-up=PASS capture=none result=PASS'; phase = 'popup-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'popup-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($checkX - $comboX) ($checkY - ($comboY + 18))); marker = ''; phase = 'checkbox-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C141-VISIBILITY checkbox=PASS progress=hidden relayout=PASS result=PASS'; phase = 'checkbox-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'checkbox-up' },
                    [pscustomobject]@{ events = @(New-C137Wheel -1); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C141-WHEEL viewport=changed stack-translated=PASS result=PASS'; phase = 'wheel-down' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($barTargetX - $checkX) ($barTargetY - $checkY)); marker = ''; phase = 'scrollbar-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C141-DRAG press=PASS owner=scrollbar result=PASS'; phase = 'scrollbar-down' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 70); marker = ''; phase = 'scrollbar-drag' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C141-DRAG release=PASS owner=none result=PASS'; phase = 'scrollbar-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($buttonX - $barTargetX) ($buttonY - ($barTargetY + 70))); marker = ''; phase = 'moved-button-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C141-POINTER moved-button=PASS translated=PASS result=PASS'; phase = 'moved-button-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'moved-button-up' },
                    [pscustomobject]@{ events = $tabEvents.ToArray(); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C141-FOCUS tab=revealed result=PASS'; phase = 'tab-sequence' })
                $calibrating = $true
            }
            if ($calibrating -and $commandIndex -ge 1) {
                $start = [Math]::Min($previousSerialLength, $partial.Length)
                $newSerial = $partial.Substring($start)
                if ($newSerial -match '(?m)^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS') {
                    $pointer = Get-C138NativePointer $partial
                    $commandList[0].events = New-C136RelativeMove ($comboX - $pointer.x) ($comboY - $pointer.y)
                    $commandIndex = 0
                    $previousMarker = ''
                    $previousSerialLength = $partial.Length
                    $calibrating = $false
                }
            }
            if ($null -ne $commandList -and $commandIndex -lt $commandList.Count) {
                $command = $commandList[$commandIndex]
                $markerSatisfied = [string]::IsNullOrEmpty($previousMarker)
                if (-not $markerSatisfied) {
                    $start = [Math]::Min($previousSerialLength, $partial.Length)
                    $markerSatisfied = $partial.Substring($start) -match $previousMarker
                }
                if ($markerSatisfied) {
                    Send-C136QmpEvents $MonitorPort $command.events $MonitorLog
                    $previousMarker = $command.marker
                    $previousSerialLength = $partial.Length
                    $commandIndex++
                }
            }
            if ($null -ne $commandList -and -not $calibrating -and
                    $commandIndex -ge $commandList.Count) { break }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        $timedOut = -not $process.HasExited -and (Get-Date) -ge $deadline
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    $process.Refresh()
    [pscustomobject]@{
        serial = if (Test-Path -LiteralPath $Serial) { Get-Content -LiteralPath $Serial -Raw } else { "" }
        serialPath = $Serial; serialSha256 = Get-Hash $Serial
        stdoutPath = $Stdout; stderrPath = $Stderr; monitorPath = $MonitorLog
        qemuExitCode = $process.ExitCode; monitorPort = $MonitorPort; timedOut = $timedOut
    }
}

function Invoke-C140Boot([string]$Esp, [string]$Serial, [string]$Stdout,
                          [string]$Stderr, [string]$MonitorLog, [int]$MonitorPort,
                          [string]$Qemu, [string]$Ovmf) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $Ovmf)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $Esp) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $Serial)),
        "-qmp", ("tcp:127.0.0.1:{0},server,nowait" -f $MonitorPort),
        "-boot", "order=c", "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host")
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -WindowStyle Hidden -PassThru
    $commandIndex = 0
    $previousMarker = ""
    $previousSerialLength = 0
    $proofStarted = $false
    $targetReady = $false
    $commandList = $null
    $calibrating = $false
    $timedOut = $false
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 150
            $partial = if (Test-Path -LiteralPath $Serial) {
                Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
            } else { "" }
            if (-not $proofStarted -and $partial -match
                    '(?m)^\[C140-PROOF\].*transport=physical-qemu result=PASS') {
                $proofStarted = $true
            }
            if ($proofStarted -and -not $targetReady -and $partial -match
                    '(?m)^\[C140-TARGET\].*result=PASS') {
                $targetReady = $true
            }
            if ($proofStarted -and $targetReady -and $null -eq $commandList) {
                $viewX = Get-C140HexField $partial "viewX"
                $viewY = Get-C140HexField $partial "viewY"
                $barX = Get-C140HexField $partial "scrollBarX"
                $barY = Get-C140HexField $partial "scrollBarY"
                $memberX = $viewX + 24
                $memberY = $viewY + 40
                $screenBarX = $barX
                $screenBarY = $barY
                $commandList = @(
                    [pscustomobject]@{ events = (New-C136RelativeMove 1 1); marker = ''; phase = 'c140-calibration' },
                    [pscustomobject]@{ events = @(New-C137Wheel -1); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C140-WHEEL viewport=changed thumb=synchronized result=PASS'; phase = 'wheel-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C140-POINTER logical=.*translated=PASS result=PASS'; phase = 'member-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = '(?m)^\[C136-NATIVE-INPUT\] button=primary phase=up.*result=PASS'; phase = 'member-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($screenBarX - $memberX) (($screenBarY + 20) - $memberY)); marker = ''; phase = 'scrollbar-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C140-DRAG press=PASS owner=scrollbar result=PASS'; phase = 'scrollbar-down' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 70); marker = ''; phase = 'scrollbar-drag' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C140-DRAG release=PASS owner=none result=PASS'; phase = 'scrollbar-up' },
                    [pscustomobject]@{ events = @(New-C136Key 'tab' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C140-FOCUS tab=revealed result=PASS'; phase = 'tab-down' },
                    [pscustomobject]@{ events = @(New-C136Key 'tab' $false); marker = ''; phase = 'tab-up' })
                $calibrating = $true
            }
            if ($calibrating -and $commandIndex -ge 1) {
                $start = [Math]::Min($previousSerialLength, $partial.Length)
                $newSerial = $partial.Substring($start)
                if ($newSerial -match '(?m)^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS') {
                    $pointer = Get-C138NativePointer $partial
                    $commandList[0].events = New-C136RelativeMove (
                        $memberX - $pointer.x) ($memberY - $pointer.y)
                    $commandList[0].phase = 'member-move'
                    $commandIndex = 0
                    $previousMarker = ''
                    $previousSerialLength = $partial.Length
                    $calibrating = $false
                }
            }
            if ($null -ne $commandList -and $commandIndex -lt $commandList.Count) {
                $command = $commandList[$commandIndex]
                $markerSatisfied = [string]::IsNullOrEmpty($previousMarker)
                if (-not $markerSatisfied) {
                    $start = [Math]::Min($previousSerialLength, $partial.Length)
                    $markerSatisfied = $partial.Substring($start) -match $previousMarker
                }
                if ($markerSatisfied) {
                    Send-C136QmpEvents $MonitorPort $command.events $MonitorLog
                    $previousMarker = $command.marker
                    $previousSerialLength = $partial.Length
                    $commandIndex++
                }
            }
            if ($null -ne $commandList -and -not $calibrating -and
                    $commandIndex -ge $commandList.Count) { break }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        $timedOut = -not $process.HasExited -and (Get-Date) -ge $deadline
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    $process.Refresh()
    [pscustomobject]@{
        serial = if (Test-Path -LiteralPath $Serial) { Get-Content -LiteralPath $Serial -Raw } else { "" }
        serialPath = $Serial; serialSha256 = Get-Hash $Serial
        stdoutPath = $Stdout; stderrPath = $Stderr; monitorPath = $MonitorLog
        qemuExitCode = $process.ExitCode; monitorPort = $MonitorPort; timedOut = $timedOut
    }
}

function Invoke-C129Boot([string]$Esp, [string]$Serial, [string]$Stdout,
                          [string]$Stderr, [string]$MonitorLog, [int]$MonitorPort,
                          [string]$Qemu, [string]$Ovmf) {
    $arguments = @(
        "-accel", "tcg,thread=single", "-machine", "pc", "-smp", "1",
        "-drive", ("if=pflash,format=raw,readonly=on,file=" + (Quote-QemuValue $Ovmf)),
        "-drive", ("file=fat:rw:" + (Quote-QemuValue $Esp) + ",format=raw,if=ide,index=0"),
        "-m", "1024M", "-vga", "std", "-display", "none",
        "-serial", ("file:" + (Quote-QemuValue $Serial)),
        "-qmp", ("tcp:127.0.0.1:{0},server,nowait" -f $MonitorPort),
        "-boot", "order=c", "-no-reboot", "-no-shutdown", "-rtc", "base=utc,clock=host")
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr -WindowStyle Hidden -PassThru
    $commands = @(
        [pscustomobject]@{
            qcode = "shift"; down = $true
            marker = '(?m)^\[C129-KEYBOARD\] shift=down side=(?:left|right) aggregate=1 result=PASS'
        },
        [pscustomobject]@{
            qcode = "tab"; down = $true
            marker = '(?m)^\[C102-MANAGED-OUTPUT\] C129-MANAGED reverse=Document count=1 modifier=shift keychar=none result=PASS'
        },
        [pscustomobject]@{
            qcode = "tab"; down = $false; marker = ""
        },
        [pscustomobject]@{
            qcode = "shift"; down = $false
            marker = '(?m)^\[C129-KEYBOARD\] shift=up side=(?:left|right) aggregate=0 result=PASS'
        },
        [pscustomobject]@{
            qcode = "tab"; down = $true
            marker = '(?m)^\[C102-MANAGED-OUTPUT\] C129-MANAGED plain-tab=Document->Open count=1 modifier=none result=PASS'
        },
        [pscustomobject]@{
            qcode = "tab"; down = $false; marker = ""
        },
        [pscustomobject]@{
            qcode = "a"; down = $true
            marker = '(?m)^\[C102-MANAGED-OUTPUT\] C129-MANAGED ordinary-char=a focused=Open activation=none result=PASS'
        },
        [pscustomobject]@{
            qcode = "a"; down = $false; marker = ""
        },
        [pscustomobject]@{
            qcode = "shift_r"; down = $true
            marker = '(?m)^\[C129-KEYBOARD\] shift=down side=right aggregate=1 result=PASS'
        },
        [pscustomobject]@{
            qcode = "shift"; down = $true
            marker = '(?m)^\[C129-KEYBOARD\] shift=down side=left aggregate=1 result=PASS'
        },
        [pscustomobject]@{
            qcode = "shift_r"; down = $false
            marker = '(?m)^\[C129-KEYBOARD\] shift=up side=right aggregate=1 result=PASS'
        },
        [pscustomobject]@{
            qcode = "shift"; down = $false
            marker = '(?m)^\[C129-KEYBOARD\] shift=up side=left aggregate=0 result=PASS'
        })
    $commandIndex = 0
    $previousMarker = ""
    $proofStarted = $false
    $timedOut = $false
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            $partial = if (Test-Path -LiteralPath $Serial) {
                Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
            } else { "" }
            if (-not $proofStarted -and $partial -match '(?m)^\[C129-PROOF\] managed-proof-started context=c129-shift-tab-proof transport=physical-qemu result=PASS') {
                $proofStarted = $true
            }
            if ($proofStarted -and $commandIndex -lt $commands.Count) {
                $command = $commands[$commandIndex]
                if ([string]::IsNullOrEmpty($previousMarker) -or $partial -match $previousMarker) {
                    Send-C129QmpEvent $MonitorPort $command.qcode $command.down $MonitorLog
                    $previousMarker = $command.marker
                    $commandIndex++
                }
            }
            if ($commandIndex -ge $commands.Count -and $partial -match '(?m)^\[C102-MANAGED-OUTPUT\] C129-RESULT outcome=(?:PASS|FAIL)') { break }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        $timedOut = -not $process.HasExited -and (Get-Date) -ge $deadline
    }
    finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    $process.Refresh()
    [pscustomobject]@{
        serial = if (Test-Path -LiteralPath $Serial) { Get-Content -LiteralPath $Serial -Raw } else { "" }
        serialPath = $Serial; serialSha256 = Get-Hash $Serial
        stdoutPath = $Stdout; stderrPath = $Stderr; monitorPath = $MonitorLog
        qemuExitCode = $process.ExitCode; monitorPort = $MonitorPort; timedOut = $timedOut
    }
}

function Assert-C120Serial([string]$Serial) {
    $required = @(
        '^\[C102-RUNTIME\] PAL/VM/GC startup seam ready',
        '^\[C103-RUNTIME\] resident runtime reused',
        '^\[NATIVEAOT-TLS-BRIDGE\] install=.*result=00000001',
        '^\[NATIVEAOT-HEAP\] action=initialize',
        '^\[NATIVEAOT-HEAP\] action=preserve')
    if (-not $isC140 -and -not $isC141 -and -not $isC121 -and -not $isC122 -and -not $isC123 -and -not $isC124 -and -not $isC125 -and -not $isC126 -and -not $isC127 -and -not $isC128 -and -not $isC129 -and -not $isC130 -and -not $isC131 -and -not $isC132 -and -not $isC133 -and -not $isC134 -and -not $isC135 -and -not $isC136 -and -not $isC137 -and -not $isC138) {
        $required += @(
            '^\[C120-APPMODEL\] catalogValid=true result=PASS',
            '^\[C120-RESULT\] outcome=PASS',
            '^\[C120-MIXED\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C120-HOST registration=4 initial=no-focus result=PASS',
            '^\[C120-TRAVERSAL\].*result=PASS',
            '^\[C120-DISABLED\] skip=Save traversal=PASS result=PASS',
            '^\[C120-POINTER\] target=document focus=PASS result=PASS',
            '^\[C120-SPACE-ISOLATION\] text-area=inserts-space button=not-activated result=PASS',
            '^\[C120-SPACE\] keydown=PASS keychar=PASS exact-once=PASS',
            '^\[C120-MODAL\] entry=open isolation=list restore=Open result=PASS',
            '^\[C120-MODAL\] entry=save-as isolation=filename/list restore=SaveAs result=PASS',
            '^\[C120-REGRESSION\] app=Notepad result=PASS',
            '^\[C120-REGRESSION\] app=Counter result=PASS',
            '^\[C120-REGRESSION\] app=Status result=PASS',
            '^\[C116-REGRESSION\] text-input=PASS result=PASS',
            '^\[C117-REGRESSION\] text-area=PASS result=PASS',
            '^\[C118-REGRESSION\] list-box=PASS result=PASS',
            '^\[C119-REGRESSION\] button=PASS result=PASS',
            '^\[C119-RESULT\] outcome=PASS',
            '^\[C118-NATIVE-REGRESSION\] app=Notepad result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C120-TESTS cases=50 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C120-HOST tests=PASS')
    }
    if ($isC141) {
        $required += @(
            '^\[C141-APPMODEL\] catalogValid=true result=PASS',
            '^\[C141-PROOF\] managed-proof-started context=c141-native transport=physical-qemu result=PASS',
            '^\[C141-TARGET\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-TESTS core=20 visibility=10 scrollview=10 focus=10 popup=6 total=56 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-PROOF launch=PASS registration=2 members=8 layout=valid result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-LAYOUT initial=PASS content-taller-than-viewport=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-POPUP open=PASS arranged-geometry=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-POPUP follow-up=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-VISIBILITY checkbox=PASS progress=hidden relayout=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-WHEEL viewport=changed stack-translated=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-DRAG press=PASS owner=scrollbar result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-DRAG release=PASS owner=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-POINTER moved-button=PASS translated=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-FOCUS tab=revealed result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-FINAL viewport=valid layout=valid capture=none drag=none result=PASS')
    }
    if ($isC140) {
        $required += @(
            '^\[C140-APPMODEL\] catalogValid=true result=PASS',
            '^\[C140-PROOF\] managed-proof-started context=c140-native transport=physical-qemu result=PASS',
            '^\[C140-TARGET\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C140-TESTS core=20 clipping=9 hit=10 focus=9 scrollbar=10 cases=58 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C140-PROOF launch=PASS registration=2 initial-viewport=0 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C140-WHEEL viewport=changed thumb=synchronized result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C140-POINTER logical=.*translated=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C140-DRAG press=PASS owner=scrollbar result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C140-DRAG release=PASS owner=none result=PASS')
    } elseif ($isC138) {
        $required += @(
            '^\[C138-APPMODEL\] catalogValid=true result=PASS',
            '^\[C138-PROOF\] managed-proof-started context=c138-native transport=physical-qemu result=PASS',
            '^\[C138-TARGET\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C138-HOST registration=10 scrollbars=2 initial=no-focus drag=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C138-TESTS api=22 drag-host=20 textarea=10 listbox=10 cases=62 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-TESTS transport=12 text-area=16 list-box=18 cases=46 result=PASS',
            '^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C138-DRAG press=PASS capture=owned result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C138-DRAG move=PASS capture=owned result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C138-DRAG release=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C138-TRACK page=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C138-POPUP blocked=PASS scrollbar=inactive result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C138-LIST-DRAG release=PASS viewport=bottom selection=preserved result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary.*result=PASS',
            '^\[C138-RELAUNCH\] close=PASS relaunch=PASS result=PASS')
        if ($isC139) {
            $required += @(
                '^\[C102-MANAGED-OUTPUT\] C139-TESTS viewport=30 textarea=10 listbox=10 cross=4 cases=54 result=PASS')
        }
    } elseif ($isC137) {
        $required += @(
            '^\[C137-APPMODEL\] catalogValid=true result=PASS',
            '^\[C137-PROOF\] managed-proof-started context=c137-native transport=physical-qemu result=PASS',
            '^\[C137-TARGET\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-TESTS transport=12 text-area=16 list-box=18 cases=46 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-HOST registration=8 list=registered initial=viewport-zero result=PASS',
            '^\[C137-NATIVE-INPUT\] kind=wheel delta=-0*1 .*buttons-preserved=true result=PASS',
            '^\[C137-NATIVE-INPUT\] kind=wheel delta=\+?0*1 .*buttons-preserved=true result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=-1 before=0 after=3 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=1 before=3 after=0 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=TextArea delta=-1 before=0 after=0 result=IGNORED',
            '^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=ListBox delta=-1 before=0 after=3 selection=0 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-LIST-POINTER selected=3 viewport=3 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-WHEEL target=ListBox delta=1 before=6 after=3 selection=3 result=PASS',
            '^\[C136-NATIVE-INPUT\] button=secondary phase=down .*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C136-CONTEXT-OPEN invoke=Secondary.*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C135-ESCAPE cancel=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-CLOSE request=PASS capture=none result=PASS',
            '^\[C137-RELAUNCH\] close=PASS relaunch=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C137-RELAUNCH registration=8 text-viewport=0 list-viewport=0 selection=0 capture=none result=PASS')
    } elseif ($isC121) {
        $required += @(
            '^\[C121-APPMODEL\] catalogValid=true result=PASS',
            '^\[C121-RESULT\] outcome=PASS',
            '^\[C121-MIXED\].*result=PASS',
            '^\[C121-FOCUSED-TESTS\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-CHECKBOX-TESTS cases=50 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-CHECKBOX-HOST-TESTS cases=17 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-HOST registration=5 initial=no-focus result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-HOST tests=PASS',
            '^\[C121-INITIAL\].*result=PASS',
            '^\[C121-TRAVERSAL\].*result=PASS',
            '^\[C121-SPACE\].*exact-once=PASS',
            '^\[C121-ROUTING\].*result=PASS',
            '^\[C121-DISABLED\].*result=PASS',
            '^\[C121-MODAL\].*result=PASS')
    }
    if ($isC122) {
        $required += @(
            '^\[C122-APPMODEL\] catalogValid=true result=PASS',
            '^\[C122-RESULT\] outcome=PASS',
            '^\[C122-MIXED\].*result=PASS',
            '^\[C122-FOCUSED-TESTS\] label=PASS host=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C122-LABEL-TESTS cases=44 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C122-LABEL-HOST-TESTS cases=22 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C121-HOST registration=5 initial=no-focus result=PASS',
            '^\[C122-INITIAL\].*result=PASS',
            '^\[C122-FOCUS-ORDER\].*result=PASS',
            '^\[C122-VISIBILITY\].*result=PASS',
            '^\[C122-DYNAMIC\] open=02-POINT\.TXT active=Open result=PASS',
            '^\[C122-DYNAMIC\] save-as=THIRD\.TXT active=SaveAs stale-tail=none result=PASS',
            '^\[C122-TRAVERSAL\].*result=PASS',
            '^\[C122-MODAL\].*result=PASS')
    }
    if ($isC123) {
        $required += @(
            '^\[C123-APPMODEL\] catalogValid=true result=PASS',
            '^\[C123-RESULT\] outcome=PASS',
            '^\[C123-MIXED\].*result=PASS',
            '^\[C123-FOCUSED-TESTS\] separator=PASS host=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C123-SEPARATOR-TESTS cases=48 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C123-SEPARATOR-HOST-TESTS cases=33 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C123-HOST registration=5 initial=no-focus result=PASS',
            '^\[C123-INITIAL\].*result=PASS',
            '^\[C123-FOCUS-ORDER\].*result=PASS',
            '^\[C123-VISIBILITY\].*result=PASS',
            '^\[C123-RESIZE\] expand=63 contract=12 stale-tail=none result=PASS',
            '^\[C123-DYNAMIC\] open=02-POINT\.TXT active=Open separator=visible result=PASS',
            '^\[C123-DYNAMIC\] save-as=THIRD\.TXT active=SaveAs separator=visible result=PASS',
            '^\[C123-MODAL\].*result=PASS')
    }
    if ($isC136) {
        $required += @(
            '^\[C136-APPMODEL\] catalogValid=true result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C136-POINTER-TESTS cases=36 result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C136-NOTES initial=registration=8 target=document menu=reused result=PASS',
            '^\[C136-PROOF\] managed-proof-started context=c136-native transport=physical-qemu result=PASS',
            '^\[C136-TARGET\].*result=PASS',
            '^\[C136-NATIVE-INPUT\] button=secondary phase=down.*result=PASS',
            '^\[C136-NATIVE-INPUT\] button=secondary phase=up.*result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C136-SECONDARY-DOWN target=Document pending=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C136-SECONDARY-UP target=Document release=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C136-CONTEXT-OPEN invoke=Secondary target=Document capture=PASS popup=open result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C136-COMMAND command=Save count=1 result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C136-COMMAND command=Save count=2 result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C135-KEYBOARD highlight=PASS disabled-skipped=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C135-ENTER command=PASS callback=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C135-ESCAPE cancel=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C135-OUTSIDE close=PASS consumed=PASS underlying=inactive result=PASS',
            '^\[C102-MANAGED-OUTPUT\].*C136-SECONDARY-UP stale=cancelled menu=none capture=none result=PASS')
    } elseif ($isC135) {
        if ($isC135FocusedApi) {
            $required += @(
                '^\[C133-APPMODEL\] catalogValid=true result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C135-POPUP-TESTS cases=40 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C134-TRANSIENT-HOST-TESTS cases=30 result=PASS',
                '^\[C135-FOCUSED-RESULT\] mode=api outcome=PASS')
        } elseif ($isC135FocusedHost) {
            $required += @(
                '^\[C133-APPMODEL\] catalogValid=true result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C135-POPUP-HOST-TESTS cases=40 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C134-TRANSIENT-HOST-TESTS cases=30 result=PASS',
                '^\[C135-FOCUSED-RESULT\] mode=host outcome=PASS')
        } else {
            $required += @(
            '^\[C133-APPMODEL\] catalogValid=true result=PASS',
            '^\[C135-INITIAL\].*result=PASS',
            '^\[C135-POPUP\].*result=PASS',
            '^\[C135-POINTER\].*result=PASS',
            '^\[C135-KEYBOARD\].*result=PASS',
            '^\[C135-ESCAPE\].*result=PASS',
            '^\[C135-OUTSIDE\].*result=PASS',
            '^\[C135-TRAVERSAL\].*result=PASS',
            '^\[C135-LIFECYCLE\].*result=PASS',
            '^\[C135-RELAUNCH\].*result=PASS',
            '^\[C134-POPUP-OPEN\].*result=PASS',
            '^\[C134-FOLLOWUP-ROUTED\].*result=PASS',
            '^\[C134-COMMIT\].*result=PASS',
            '^\[C135-MIXED\].*result=PASS',
            '^\[C135-RESULT\] outcome=PASS',
            '^\[C133-MIXED\].*result=PASS',
            '^\[C133-RESULT\] outcome=PASS')
        }
    } elseif ($isC134) {
        $required += @(
            '^\[C133-APPMODEL\] catalogValid=true result=PASS',
            '^\[C133-RESULT\] outcome=PASS',
            '^\[C133-MIXED\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C133-HOST registration=7 combo=path-display initial=no-focus result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C133-NOTES initial=registration=7 selection=full-path callbacks=0 result=PASS',
            '^\[C134-POPUP-OPEN\].*result=PASS',
            '^\[C134-FOLLOWUP-ROUTED\].*result=PASS',
            '^\[C134-COMMIT\].*result=PASS',
            '^\[C134-CANCEL\].*result=PASS',
            '^\[C134-OUTSIDE\].*result=PASS',
            '^\[C134-TRAVERSAL\].*result=PASS',
            '^\[C134-LIFECYCLE\].*result=PASS',
            '^\[C134-RESULT\] outcome=PASS')
        if (-not $SkipFocusedTests) {
            $required += @(
                '^\[C133-FOCUSED-TESTS\] combo=PASS host=PASS result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C133-COMBO-TESTS cases=44 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C133-COMBO-HOST-TESTS cases=24 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C134-TRANSIENT-HOST-TESTS cases=30 result=PASS')
        }
    } elseif ($isC133) {
        if ($AllowBoundedHostDefect) {
            $required += @(
                '^\[C133-APPMODEL\] catalogValid=true result=PASS',
                '^\[C133-RESULT\] outcome=BLOCKED',
                '^\[C133-MIXED\].*result=BLOCKED',
                '^\[C102-MANAGED-OUTPUT\] C133-HOST registration=7 combo=path-display initial=no-focus result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C133-NOTES initial=registration=7 selection=full-path callbacks=0 result=PASS',
                '^\[C133-INITIAL\].*result=PASS',
                '^\[C133-POINTER\] open=PASS row-followup=BLOCKED result=PASS')
        } else {
        $required += @(
            '^\[C133-APPMODEL\] catalogValid=true result=PASS',
            '^\[C133-RESULT\] outcome=PASS',
            '^\[C133-MIXED\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C133-HOST registration=7 combo=path-display initial=no-focus result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C133-NOTES initial=registration=7 selection=full-path callbacks=0 result=PASS',
            '^\[C133-INITIAL\].*result=PASS',
            '^\[C133-POINTER\].*result=PASS',
            '^\[C133-SPACE\].*result=PASS',
            '^\[C133-ARROW\].*result=PASS',
            '^\[C133-ENTER\].*result=PASS',
            '^\[C133-ESCAPE\].*result=PASS',
            '^\[C133-TAB\].*result=PASS',
            '^\[C133-SHIFT-TAB\].*result=PASS',
            '^\[C133-OUTSIDE\].*result=PASS',
            '^\[C133-LIFECYCLE\].*result=PASS',
            '^\[C133-DISABLED\].*result=PASS',
            '^\[C133-MODAL\].*result=PASS',
            '^\[C133-RELAUNCH\].*result=PASS')
        if (-not $SkipFocusedTests) {
            $required += @(
                '^\[C133-FOCUSED-TESTS\] combo=PASS host=PASS result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C133-COMBO-TESTS cases=44 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C133-COMBO-HOST-TESTS cases=24 result=PASS')
        }
        }
    } elseif ($isC132) {
        $required += @(
            '^\[C132-APPMODEL\] catalogValid=true result=PASS',
            '^\[C132-RESULT\] outcome=PASS',
            '^\[C132-MIXED\].*result=PASS',
            '^\[C132-FOCUSED-TESTS\] radio=PASS host=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C132-RADIO-TESTS cases=38 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C132-RADIO-HOST-TESTS cases=12 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C132-HOST tests=PASS',
            '^\[C102-MANAGED-OUTPUT\] C132-HOST registration=8 group=path-display initial=no-focus result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C132-NOTES initial=registration=8 selection=full-path callbacks=0 result=PASS',
            '^\[C132-INITIAL\].*result=PASS',
            '^\[C132-POINTER\].*result=PASS',
            '^\[C132-SPACE\].*result=PASS',
            '^\[C132-TAB\].*result=PASS',
            '^\[C132-SHIFT-TAB\].*result=PASS',
            '^\[C132-ARROW\].*result=PASS',
            '^\[C132-LIFECYCLE\].*result=PASS',
            '^\[C132-DISABLED\].*result=PASS',
            '^\[C132-MODAL\].*result=PASS',
            '^\[C132-CROSS-GROUP\].*result=PASS',
            '^\[C132-RELAUNCH\].*result=PASS')
    } elseif ($isC131) {
        $required += @(
            '^\[C131-APPMODEL\] catalogValid=true result=PASS',
            '^\[C131-RESULT\] outcome=PASS',
            '^\[C131-MIXED\].*result=PASS',
            '^\[C131-FOCUSED-TESTS\] checkbox=PASS host=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C131-CHECKBOX-TESTS cases=34 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C131-CHECKBOX-HOST-TESTS cases=23 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C131-HOST tests=PASS',
            '^\[C102-MANAGED-OUTPUT\] C131-NOTES initial=registration=8 show-status=checked result=PASS',
            '^\[C131-INITIAL\].*result=PASS',
            '^\[C131-POINTER\].*result=PASS',
            '^\[C131-SPACE\].*exact-once=PASS',
            '^\[C131-TAB\].*result=PASS',
            '^\[C131-SHIFT-TAB\].*result=PASS',
            '^\[C131-LIFECYCLE\].*result=PASS',
            '^\[C131-RELAUNCH\].*result=PASS')
    } elseif ($isC129) {
        $required += @(
            '^\[C129-APPMODEL\] catalogValid=true result=PASS',
            '^\[C129-FOCUSED-TESTS\] shift-tab=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C129-SHIFT-TAB-TESTS cases=31 result=PASS',
            '^\[C129-PROOF\] managed-proof-started context=c129-shift-tab-proof transport=physical-qemu result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C129-MANAGED proof-started initial-focus=none registration=4 tab=key-down-only result=PASS',
            '^\[C129-KEYBOARD\] shift=down side=(?:left|right) aggregate=1 result=PASS',
            '^\[C129-NATIVE\] tab-keydown shift=1 transport=production result=PASS',
            '^\[C129-NATIVE-INPUT\] kind=key-down key=00000009 shift=00000001 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C129-MANAGED reverse=Document count=1 modifier=shift keychar=none result=PASS',
            '^\[C129-KEYBOARD\] shift=up side=(?:left|right) aggregate=0 result=PASS',
            '^\[C129-KEYBOARD\] shift=down side=right aggregate=1 result=PASS',
            '^\[C129-KEYBOARD\] shift=down side=left aggregate=1 result=PASS',
            '^\[C129-KEYBOARD\] shift=up side=right aggregate=1 result=PASS',
            '^\[C129-KEYBOARD\] shift=up side=left aggregate=0 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C129-MANAGED plain-tab=Document->Open count=1 modifier=none result=PASS',
            '^\[C129-NATIVE-INPUT\] kind=key-down key=00000009 shift=00000000 result=PASS',
            '^\[C129-NATIVE-INPUT\] kind=key-char value=00000061 shift=00000000 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C129-MANAGED ordinary-char=a focused=Open activation=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C129-RESULT outcome=PASS transport=production')
    } elseif ($isC128) {
        $required += @(
            '^\[C128-APPMODEL\] catalogValid=true result=PASS',
            '^\[C128-RESULT\] outcome=PASS',
            '^\[C128-MIXED\].*result=PASS',
            '^\[C128-FOCUSED-TESTS\] panel-lifecycle=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C128-PANEL-LIFECYCLE-TESTS cases=\d+ result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C128-NOTES initial=registration=7 result=PASS',
            '^\[C128-VISIBILITY-LIFECYCLE\].*result=PASS',
            '^\[C128-ACTIVATION-CANCEL\].*result=PASS',
            '^\[C128-FOCUS-LIFECYCLE\].*result=PASS',
            '^\[C128-RELAUNCH\].*result=PASS',
            '^\[C116-C127-REGRESSION\].*result=PASS')
    } elseif ($isC130 -or $isC127) {
        $required += @(
            '^\[C127-APPMODEL\] catalogValid=true result=PASS',
            '^\[C127-RESULT\] outcome=PASS',
            '^\[C127-MIXED\].*result=SKIP',
            '^\[C127-FOCUSED-TESTS\] panel=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C127-PANEL-TESTS cases=60 result=PASS',
            '^\[C127-FOCUS-ORDER\].*retired-direct-managed-helper.*result=PASS')
        if ($isC130) {
            $required += '^\[C130-REGRESSION\] legacy-helper=direct-managed-reverse-traversal status=SKIP replacement=C129-production-shift-tab result=PASS'
        }
    } elseif ($isC126) {
        $required += @(
            '^\[C126-APPMODEL\] catalogValid=true result=PASS',
            '^\[C126-RESULT\] outcome=PASS',
            '^\[C126-MIXED\].*result=PASS',
            '^\[C126-FOCUSED-TESTS\] group-box=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C126-GROUP-BOX-TESTS cases=60 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C126-HOST registration=7 group-box=absent initial=no-focus result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C126-NOTES initial=PathDisplay caption=PathDisplay bounds=12,264,456,90 radios=independent registration=7 result=PASS',
            '^\[C126-INITIAL\].*result=PASS',
            '^\[C126-FOCUS-ORDER\].*result=PASS',
            '^\[C126-VISIBILITY\].*result=PASS',
            '^\[C126-MOVE\].*result=PASS',
            '^\[C126-RESIZE\].*result=PASS',
            '^\[C124-REGRESSION\].*result=PASS',
            '^\[C125-REGRESSION\] progress=independent shift-routing=preserved shift-right=PASS result=PASS',
            '^\[C126-MODAL\].*result=PASS',
            '^\[C116-C125-REGRESSION\].*result=PASS')
    } elseif ($isC125) {
        $required += @(
            '^\[C125-APPMODEL\] catalogValid=true result=PASS',
            '^\[C125-RESULT\] outcome=PASS',
            '^\[C125-MIXED\].*result=PASS',
            '^\[C125-FOCUSED-TESTS\] progress-bar=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C125-PROGRESS-TESTS cases=50 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C125-HOST registration=7 initial=no-focus result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C125-NOTES initial=authoritative-length capacity=256 registration=7 result=PASS',
            '^\[C125-INITIAL\].*result=PASS',
            '^\[C125-FOCUS-ORDER\].*result=PASS',
            '^\[C125-DYNAMIC\] insertion=.*result=PASS',
            '^\[C125-DYNAMIC\] newline=.*result=PASS',
            '^\[C125-DYNAMIC\] deletion=.*result=PASS',
            '^\[C125-DYNAMIC\] selection-replacement=.*result=PASS',
            '^\[C125-VISIBILITY\].*result=PASS',
            '^\[C125-DYNAMIC\] open=02-POINT\.TXT.*result=PASS',
            '^\[C125-PRESENTATION\].*result=PASS',
            '^\[C125-SAVE\].*result=PASS',
            '^\[C125-CAPACITY\] value=256 maximum=256 result=PASS',
            '^\[C125-OVERFLOW\].*result=PASS',
            '^\[C125-MODAL\].*result=PASS',
            '^\[C124-REGRESSION\].*result=PASS')
    } elseif ($isC124) {
        $required += @(
            '^\[C124-APPMODEL\] catalogValid=true result=PASS',
            '^\[C124-RESULT\] outcome=PASS',
            '^\[C124-MIXED\].*result=PASS',
            '^\[C124-FOCUSED-TESTS\] radio-button=PASS group=PASS host=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C124-RADIO-BUTTON-TESTS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C124-RADIO-GROUP-TESTS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C124-RADIO-HOST-TESTS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C124-HOST registration=7 initial=no-focus result=PASS',
            '^\[C124-INITIAL\].*result=PASS',
            '^\[C124-FOCUS-ORDER\].*result=PASS',
            '^\[C124-SPACE\].*result=PASS',
            '^\[C124-ARROW\].*result=PASS',
            '^\[C124-POINTER\].*result=PASS',
            '^\[C124-DISABLED\].*result=PASS',
            '^\[C124-SHOW-PATH\].*result=PASS',
            '^\[C124-DYNAMIC\] open=02-POINT\.TXT.*result=PASS',
            '^\[C124-DYNAMIC\] save-as=THIRD\.TXT.*result=PASS',
            '^\[C124-MODAL\].*result=PASS')
    }
    foreach ($pattern in $required) {
        if ($Serial -notmatch "(?m)$pattern") { throw "Managed control proof missing serial marker: $pattern" }
    }
    $spaceMarker = @([regex]::Matches($Serial,
        '(?m)^\[C120-SPACE\] keydown=PASS keychar=PASS exact-once=PASS\r?$')).Count
    if (-not $isC140 -and -not $isC141 -and -not $isC136 -and -not $isC137 -and -not $isC138 -and -not $isC121 -and -not $isC122 -and -not $isC123 -and -not $isC124 -and -not $isC125 -and -not $isC126 -and -not $isC127 -and -not $isC128 -and -not $isC129 -and -not $isC130 -and -not $isC131 -and -not $isC132 -and -not $isC133 -and -not $isC134 -and -not $isC135 -and $spaceMarker -ne 1) { throw "C120 expected one exact-once Space marker, got $spaceMarker." }
    $saveActivation = @([regex]::Matches($Serial,
        '(?m)^\[C102-MANAGED-OUTPUT\] C120-ACTIVATE control=Save result=PASS\r?$')).Count
    if (-not $isC140 -and -not $isC141 -and -not $isC136 -and -not $isC137 -and -not $isC138 -and -not $isC121 -and -not $isC122 -and -not $isC123 -and -not $isC124 -and -not $isC125 -and -not $isC126 -and -not $isC127 -and -not $isC128 -and -not $isC129 -and -not $isC130 -and -not $isC131 -and -not $isC132 -and -not $isC133 -and -not $isC134 -and -not $isC135 -and $saveActivation -ne 1) { throw "C120 expected one managed Save activation, got $saveActivation." }
    if ($Serial -match '(?m)^\[(?:C141|C140|C138|C137|C136|C135|C134|C132|C131|C130|C129|C120|C121|C122|C123|C124|C125|C126|C127|C128)-[^\r\n]*FAIL|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "Managed control proof serial output contains a failure or fault marker."
    }
    [pscustomobject]@{
        outcome = if ($AllowBoundedHostDefect) { "BOUNDED-HOST-DEFECT" } else { "PASS" }
        spaceMarkers = $spaceMarker; saveActivations = $saveActivation
    }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$providedComposite = -not [string]::IsNullOrWhiteSpace($CompositeElfPath)
if ($providedComposite) { $CompositeElfPath = [System.IO.Path]::GetFullPath($CompositeElfPath) }
if (-not $SkipManagedBuild -and -not $providedComposite) {
    if ([string]::IsNullOrWhiteSpace($PythonExe)) {
        $PythonExe = Get-Tool "python" @(
            "C:\Users\guideX\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
            "C:\Python312\python.exe", "C:\Python311\python.exe")
    }
    if ([string]::IsNullOrWhiteSpace($PythonExe)) { throw "Python was not found; pass -PythonExe." }
    if (Test-Path -LiteralPath $compositeBuildRoot) {
        Remove-Item -LiteralPath $compositeBuildRoot -Recurse -Force
    }
    $managedBuildArguments = @(
        "-ExecutionPolicy", "Bypass", "-File", $buildScript,
        "-RepoRoot", $RepoRoot, "-OutputRoot", $compositeBuildRoot,
        "-RuntimePackRoot", (Join-Path $RepoRoot "tools\dotnet\runtime-pack"),
        "-RuntimePackOutputRoot", $runtimePackOutputRoot,
        "-UseGuideXosRuntimePack", "-ProductionApplication", "-PersistentCompositeLifecycle",
        "-AllocationMode", "Allocating", "-ManagedProjectMode",
        $(if ($isC141) { "C141Composite" } elseif ($isC140) { "C140Composite" } elseif ($isC139) { "C139Composite" } elseif ($isC138) { "C138Composite" } elseif ($isC137) { "C137Composite" } elseif ($isC136) { "C136Composite" } elseif ($isC135) { "C135Composite" } elseif ($isC134) { "C134Composite" } elseif ($isC133) { "C133Composite" } elseif ($isC132) { "C132Composite" } elseif ($isC131) { "C131Composite" } elseif ($isC129) { "C129Composite" } elseif ($isC128) { "C128Composite" } elseif ($isC130 -or $isC127) { "C127Composite" } elseif ($isC126) { "C126Composite" } elseif ($isC125) { "C125Composite" } elseif ($isC124) { "C124Composite" } elseif ($isC123) { "C123Composite" } elseif ($isC122) { "C122Composite" } elseif ($isC121) { "C121Composite" } else { "C120Composite" }),
        "-PythonExe", $PythonExe)
    if ($isC134) { $managedBuildArguments += "-IncludeC134FocusedTests" }
    if ($isC135) { $managedBuildArguments += "-IncludeC135FocusedTests" }
    if ($isC136) { $managedBuildArguments += "-IncludeC136FocusedTests" }
    Invoke-Checked "powershell" $managedBuildArguments
}
$compositeElf = if ($providedComposite) { $CompositeElfPath } else {
    Join-Path $compositeBuildRoot "artifacts\HostLogProof.elf"
}
if (-not (Test-Path -LiteralPath $compositeElf -PathType Leaf)) {
    throw "Managed control proof composite ELF is missing: $compositeElf"
}
Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot, "-OutputImage", $stagingImage,
    "-C104AppAPath", $compositeElf, "-ProductionCompositeApplicationPath", $compositeElf,
    "-C114ManagedDirectoryServices", "-C117ManagedTextArea", "-C118ManagedListBox")

$kernelFlags = "-DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_NATIVEAOT_PRODUCTION_COMPOSITE_LAUNCH -DGXOS_NATIVEAOT_C112_REUSABLE_MANAGED_APPLICATION -DGXOS_NATIVEAOT_C113_MANAGED_FILE_SERVICES -DGXOS_NATIVEAOT_C114_MANAGED_DIRECTORY_SERVICES -DGXOS_NATIVEAOT_C115_MANAGED_FILE_PICKER -DGXOS_NATIVEAOT_C116_MANAGED_TEXT_INPUT -DGXOS_NATIVEAOT_C117_MANAGED_TEXT_AREA -DGXOS_NATIVEAOT_C118_MANAGED_LIST_BOX -DGXOS_NATIVEAOT_C119_MANAGED_BUTTON -DGXOS_NATIVEAOT_C120_MANAGED_CONTROL_HOST"
if ($isC141) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU -DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU -DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING -DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR -DGXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK"
}
elseif ($isC140) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU -DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU -DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING -DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR -DGXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW"
}
elseif ($isC138) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU -DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU -DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING -DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR"
}
elseif ($isC137) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU -DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU -DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING"
}
elseif ($isC136) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU -DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU"
}
elseif ($isC135) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU"
    if ($isC135FocusedApi) { $kernelFlags += " -DGXOS_NATIVEAOT_C135_FOCUSED_API" }
    if ($isC135FocusedHost) { $kernelFlags += " -DGXOS_NATIVEAOT_C135_FOCUSED_HOST" }
}
elseif ($isC134) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING" }
elseif ($isC133) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX" }
elseif ($isC132) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON" }
elseif ($isC131) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX" }
elseif ($isC129) { $kernelFlags += " -DGXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT" }
elseif ($isC128) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE" }
elseif ($isC130 -or $isC127) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL" }
if ($isC130) { $kernelFlags += " -DGXOS_NATIVEAOT_C130_C127_WRAPPER" }
elseif ($isC126) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX" }
elseif ($isC125) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR" }
elseif ($isC124) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON" }
elseif ($isC123) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR" }
elseif ($isC122) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL" }
elseif ($isC121) { $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX" }
if (-not $SkipKernelBuild) {
    $kernelBuildArguments = @(
        "-C", (Join-Path $RepoRoot "kernel"), "ARCH=amd64",
        "EXTRA_CFLAGS=$kernelFlags")
    if (-not $IncrementalKernelBuild) {
        $kernelBuildArguments += "-B"
    }
    Invoke-Checked "mingw32-make" $kernelBuildArguments
}
if (-not (Test-Path -LiteralPath $kernelPath -PathType Leaf)) { throw "Kernel is missing: $kernelPath" }
if (-not (Test-Path -LiteralPath $bootloaderPath -PathType Leaf)) { throw "Bootloader is missing: $bootloaderPath" }

$inputs = [ordered]@{
    compositeElf = $compositeElf; compositeElfSha256 = Get-Hash $compositeElf
    kernel = $kernelPath; kernelSha256 = Get-Hash $kernelPath
    bootloader = $bootloaderPath; bootloaderSha256 = Get-Hash $bootloaderPath
    ramdisk = $stagingImage; ramdiskSha256 = Get-Hash $stagingImage
    runtimePackManifest = Join-Path $runtimePackOutputRoot "runtime-pack.manifest.json"
}
$inputs.runtimePackManifestSha256 = Get-Hash $inputs.runtimePackManifest
$inputs | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot "inputs.json") -Encoding ASCII
@"
abi=GuideXos Host ABI v1/table 104; no capability changes; no ABI input transport changes
host=GuideXosControlHost fixed capacity 8; picker capacity 2; explicit kind dispatch; no reflection; presentation-only C122 label and C123 separator are not registered
focus=one active control or no focus; C124 registration order Open, Save, Save As, Show path, Full path, File name, Document; label and separator absent from Tab/Shift-Tab
space=KeyDown Space ignored by button; one KeyChar Space activates Save exactly once; text-area Space is routed as text
modal=picker focus is isolated; main focus restores to Open or Save As after completion
runtime=NativeAOT resident image; allocation/GC/VFS/runtime seams unchanged
qemu=three fresh isolated ESP boots; serial is authoritative
progress=GuideXosProgressBar bounded range 0..65535; Notes mirrors TextArea.Length with maximum TextArea.MaximumCharacters=256; 32 configured fill cells; no ControlHost registration
rendering=text-backed bracket/fill/percentage with floor integer arithmetic; complete frame redraw removes shortened tails
groupBox=GuideXosGroupBox bounded text-backed frame; Path Display caption; 12,264,456,90; half-open containment and relative-coordinate helper; non-focusable; no child ownership or routing
panelLifecycle=C128 cancels split Space activation after Panel/child visibility, membership, enabled, focus, and modal transitions; ControlHost registration remains distinct from Panel membership; Notes relaunch is bounded and registration-stable
shiftTab=C129 sends explicit QMP input-send-event Shift/Tab transitions, then plain tab and printable a through PS/2 IRQ, native compositor dispatch, managed Host ABI, and bounded serial acknowledgements; Tab is KeyDown-only and Shift is tracked per physical side
c131=GuideXosCheckBox uses the existing host registration, pointer-down, KeyDown/KeyChar Space split, Tab/Shift+Tab, Panel membership, modal routing, and lifecycle cancellation; Notes adds Show status as registration 8 and the callback controls the status line
c133=GuideXosComboBox uses fixed item storage, retains host focus while its transient below-control list is open, captures outside clicks, commits only on Enter/Space or item pointer selection, and cancels on Escape/lifecycle interruption; Notes replaces the two path RadioButtons with one registration
c134=GuideXosControlHost owns one bounded transient-input lease; the registered ComboBox remains focused while the open popup receives first refusal for pointer and keyboard follow-up events, returns consumed/not-consumed deterministically, and releases capture on commit, cancel, lifecycle, modal, membership, unregister, application close, or callback mutation
c137=PS/2 IntelliMouse wheel packets use the existing launch-flags transport: wheel kind values 0x07..0x0F encode normalized signed deltas -4..+4 while the existing 12-bit X/Y payload and button identity remain intact; positive is up and negative is down; the eligible control under the pointer receives wheel unless one-owner transient capture swallows it, with no wheel-through
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "input-contract.txt") -Encoding ASCII
if ($isC135) {
    Add-Content -LiteralPath (Join-Path $EvidenceRoot "input-contract.txt") -Value "c135=GuideXosPopupMenu is a fixed-capacity non-focusable registered transient owner using the same one-owner lease; Options invokes it through the existing production action path; secondary-click remains deferred because current transport proves pointer-down only"
}
if ($isC136) {
    Add-Content -LiteralPath (Join-Path $EvidenceRoot "input-contract.txt") -Value "c136=physical QEMU secondary button down/up uses the existing launch-flags field with new semantic event kinds; coordinates remain the existing 12-bit x/y payload; no host-table or ABI expansion"
}

$bootResults = [System.Collections.Generic.List[object]]::new()
if (-not $SkipQemu) {
    $qemu = Get-Tool "qemu-system-x86_64.exe" @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "C:\qemu\qemu-system-x86_64.exe", "C:\msys64\mingw64\bin\qemu-system-x86_64.exe")
    $ovmf = Get-Tool "edk2-x86_64-code.fd" @(
        (Join-Path $RepoRoot "OVMF.fd"), "C:\Program Files\qemu\share\edk2-x86_64-code.fd",
        "C:\Program Files (x86)\qemu\share\edk2-x86_64-code.fd")
    if (-not $qemu) { throw "qemu-system-x86_64.exe was not found." }
    if (-not $ovmf) { throw "OVMF code image was not found." }
    for ($index = 1; $index -le $FreshBootCount; $index++) {
        $bootRoot = Join-Path $EvidenceRoot ("boot-{0:D2}" -f $index)
        $esp = Join-Path $bootRoot "ESP"
        New-Item -ItemType Directory -Force -Path $bootRoot | Out-Null
        Stage-Esp $esp $kernelPath $bootloaderPath $stagingImage
        Start-Sleep -Seconds 5
        $serial = Join-Path $bootRoot "serial.log"
        $stdout = Join-Path $bootRoot "qemu.stdout.log"
        $stderr = Join-Path $bootRoot "qemu.stderr.log"
        $monitorLog = Join-Path $bootRoot "qemu-monitor.log"
        $monitorPort = 46300 + $index
        foreach ($stale in @($serial, $stdout, $stderr, $monitorLog)) {
            if (Test-Path -LiteralPath $stale -PathType Leaf) { Remove-Item -LiteralPath $stale -Force }
        }
        $boot = if ($isC141) {
            Invoke-C141Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
        } elseif ($isC140) {
            Invoke-C140Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
        } elseif ($isC138) {
            Invoke-C138Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
        } elseif ($isC137) {
            Invoke-C137Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
        } elseif ($isC136) {
            Invoke-C136Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
        } elseif ($isC129) {
            Invoke-C129Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
        } else {
            Invoke-C120Boot $esp $serial $stdout $stderr $qemu $ovmf
        }
        try { $classification = Assert-C120Serial $boot.serial }
        catch {
            $classification = [pscustomobject]@{
                outcome = if ($boot.timedOut) { "TIMEOUT" } else { "FAIL" }
                error = $_.Exception.Message
            }
        }
        $bootResults.Add([pscustomobject]@{
            boot = $index; outcome = $classification.outcome; classification = $classification
            serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
            stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath
            monitorPath = if ($isC129) { $boot.monitorPath } else { $null }
            qemuExitCode = $boot.qemuExitCode; timedOut = $boot.timedOut
        }) | Out-Null
        Write-Host ("[{0}] boot={1} outcome={2} serial={3}" -f $ProofPhase, $index, $classification.outcome, $serial)
        if ($classification.outcome -ne "PASS" -and
            -not ($AllowBoundedHostDefect -and
                $classification.outcome -eq "BOUNDED-HOST-DEFECT")) {
            throw "$ProofPhase fresh boot $index failed: $($classification.error)"
        }
        if ($index -lt $FreshBootCount) {
            Start-Sleep -Seconds 2
        }
    }
}

$evidenceSerial = if ($bootResults.Count -gt 0) {
    Get-Content -LiteralPath (Join-Path $EvidenceRoot "boot-01\serial.log")
} else { @("QEMU not executed; build-only evidence.") }
$evidenceSerial | Where-Object { $_ -match '^\[(?:C141|C140|C138|C137|C136|C135|C134|C133|C132|C131|C130|C129|C128|C127|C126|C125|C124|C123|C122|C121|C120|C119|C118|C117|C116|C115)-' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "managed-control-host-output.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C141|C140|C138|C137|C136|C135|C134|C133|C132|C131|C130|C129|C128|C127|C126|C124|C123|C122|C121|C120)-' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "control-host-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C116|C117|C118|C119)-' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "regression-evidence.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C102|C103|C112|C118|C119|NATIVEAOT|GC|PAL)' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "lifecycle-evidence.txt") -Encoding ASCII

$sourceFiles = @(
    "kernel\core\main.cpp", "kernel\core\nativeaot_application.cpp", "kernel\core\ps2mouse.cpp",
    "built_in_app_metadata.h",
    "kernel\core\kernel_compositor.cpp", "compositor.cpp",
    "kernel\core\ps2keyboard.cpp",
    "samples\managed\HostLogProof\GuideXos\GuideXosControlHost.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioButton.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioGroup.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioButtonTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioGroupTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioButtonHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosControlHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosCheckBox.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosCheckBoxTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosCheckBoxHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosCheckBoxC131Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosCheckBoxC131HostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioButtonC132Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioButtonC132HostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosComboBox.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosComboBoxC133Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosComboBoxC133HostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosComboBoxC134HostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosPopupMenu.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosPopupMenuC135Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosPopupMenuC135HostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosLaunchContext.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextInput.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSecondaryPointerC136Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosHost.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosLabel.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosLabelTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosLabelHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSeparator.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSeparatorTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSeparatorHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosProgressBar.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosProgressBarTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosGroupBox.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosGroupBoxTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosPanel.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosPanelTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosPanelLifecycleTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosShiftTabTransportTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosButton.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextInput.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosTextArea.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosListBox.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosScrollBar.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosScrollBarC138Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosVerticalViewport.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSharedScrollViewportC139Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosScrollView.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosScrollViewC140Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosVerticalStackMember.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosVerticalStack.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosVerticalStackC141Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSurface.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosMouseWheelC137Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosFilePicker.cs",
    "samples\managed\HostLogProof\Applications\ManagedNotes.cs",
    "samples\managed\HostLogProof\Applications\ManagedScrollViewDemo.cs",
    "samples\managed\HostLogProof\Applications\ManagedVerticalStackDemo.cs",
    "samples\managed\HostLogProof\HostLogProof.csproj",
    "samples\managed\HostLogProof\NativeAbi.cs",
    "scripts\dotnet\build-managed-hostlog-proof.ps1",
    "scripts\dotnet\run-c120-managed-control-host.ps1",
    "scripts\dotnet\run-c122-managed-label.ps1",
    "scripts\dotnet\run-c123-managed-separator.ps1",
    "scripts\dotnet\run-c124-managed-radio-button.ps1",
    "scripts\dotnet\run-c125-managed-progress-bar.ps1",
    "scripts\dotnet\run-c126-managed-group-box.ps1",
    "scripts\dotnet\run-c127-managed-panel.ps1",
    "scripts\dotnet\run-c129-shift-tab-input-transport.ps1",
    "scripts\dotnet\run-c130-c127-wrapper.ps1",
    "scripts\dotnet\run-c131-managed-checkbox.ps1",
    "scripts\dotnet\run-c132-managed-radiobutton.ps1",
    "scripts\dotnet\run-c133-managed-combobox.ps1",
    "scripts\dotnet\run-c135-managed-popup-menu.ps1",
    "scripts\dotnet\run-c136-secondary-pointer-context-menu.ps1",
    "scripts\dotnet\run-c139-shared-scroll-viewport.ps1",
    "docs\dotnet\NATIVEAOT_C122_MANAGED_LABEL.md",
    "docs\dotnet\NATIVEAOT_C123_MANAGED_SEPARATOR.md",
    "docs\dotnet\NATIVEAOT_C124_MANAGED_RADIO_BUTTON.md",
    "docs\dotnet\NATIVEAOT_C125_MANAGED_PROGRESS_BAR.md",
    "docs\dotnet\NATIVEAOT_C126_MANAGED_GROUP_BOX.md",
    "docs\dotnet\NATIVEAOT_C127_MANAGED_PANEL.md",
    "docs\dotnet\NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE.md",
    "docs\dotnet\NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT.md",
    "docs\dotnet\NATIVEAOT_C130_C127_WRAPPER_STALL.md",
    "docs\dotnet\NATIVEAOT_C131_MANAGED_CHECKBOX.md",
    "docs\dotnet\NATIVEAOT_C132_MANAGED_RADIOBUTTON.md",
    "docs\dotnet\NATIVEAOT_C133_MANAGED_COMBOBOX.md",
    "docs\dotnet\NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING.md",
    "docs\dotnet\NATIVEAOT_C135_MANAGED_POPUP_MENU.md",
    "docs\dotnet\NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU.md",
    "docs\dotnet\NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING.md",
    "docs\dotnet\NATIVEAOT_C138_MANAGED_SCROLLBAR.md",
    "docs\dotnet\NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT.md",
    "docs\dotnet\NATIVEAOT_C140_MANAGED_SCROLLVIEW.md",
    "docs\dotnet\NATIVEAOT_C141_MANAGED_VERTICAL_STACK_LAYOUT.md")
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) { $sourceHashes[$sourceFile] = Get-Hash (Join-Path $RepoRoot $sourceFile) }

$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$aheadBehind = if ($repoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim() } else { "" }
@"
startHead=$startHead
startSubject=$startSubject
startBranch=$startBranch
startUpstream=$startUpstream
startAheadBehind=$startAheadBehind
endHead=$repoHead
endSubject=$repoSubject
endBranch=$repoBranch
endUpstream=$repoUpstream
endAheadBehind=$aheadBehind
"@ | Set-Content -LiteralPath (Join-Path $EvidenceRoot "repository-state.txt") -Encoding ASCII

$manifest = [ordered]@{
    schemaVersion = 1; phase = $ProofPhase; c135ProofMode = if ($isC135) { $C135ProofMode } else { "Production" }; outcome = if ($SkipQemu) { "BUILD_ONLY" } elseif ($AllowBoundedHostDefect) { "BOUNDED-HOST-DEFECT" } else { "PASS" }
    repository = [ordered]@{ root = $RepoRoot; branch = $repoBranch; head = $repoHead; subject = $repoSubject; upstream = $repoUpstream; aheadBehind = $aheadBehind }
    hostAbi = [ordered]@{ version = 1; tableSize = 104; changed = $false; capabilityChanges = "none"; inputTransport = "existing pointer-down, KeyDown, KeyChar and Shift payload" }
    controlHost = [ordered]@{ api = "GuideXosControlHost"; capacity = 8; pickerCapacity = 2; tests = if($isC141){"C141 production stack host: registration 2; direct ScrollView members 8; C141 focused layout/visibility/ScrollView/focus/popup suite total 56"}elseif($isC137){"C137 wheel transport: 46 focused API/host cases; TextArea/ListBox routing, capture, modal, lifecycle, and burst coverage"}elseif($isC135){"C135 popup menu: 40 API cases, 40 host cases, shared one-owner capture, and retained C134 routing"}elseif($isC134){"C134 transient-capture contract: 30 focused host cases plus production ComboBox routing"}elseif($isC133){"C133 ComboBox API, transient capture, lifecycle, Panel, modal, and host routing"}elseif($isC132){"C132 RadioButton API, group coordination, callbacks, Panel, lifecycle, modal, and host routing"}elseif($isC131){"C131 checkbox API, callback, Panel, lifecycle, modal, and host routing"}elseif($isC129){"C129 direct Shift/Tab transport fixture plus existing host coverage"}elseif($isC130){"C127 Panel wrapper with obsolete direct-managed reverse helper retired"}elseif($isC128){"C128 Panel visibility, membership, activation cancellation, modal, and relaunch lifecycle"}elseif($isC127){"C127 Panel visibility/focus integration plus C126 and earlier regressions"}elseif($isC126){"C126 GroupBox passive integration plus C124/C125 regressions"}elseif($isC125){"C124 interoperability plus passive progress"}elseif($isC124){"radio host focused suite"}elseif($isC123){33}elseif($isC122){22}elseif($isC121){17}else{50}; legacyC120HostSuite = if($isC121 -or $isC122 -or $isC123 -or $isC124 -or $isC125 -or $isC126 -or $isC127 -or $isC128 -or $isC129 -or $isC130 -or $isC131 -or $isC132 -or $isC133 -or $isC134 -or $isC135 -or $isC137){"separate C120 runner"}else{"same image"}; modal = "one shallow picker scope with saved-ID restoration and forward fallback" }
    comboBox = [ordered]@{ api = "GuideXosComboBox"; maximumItemCount = 16; maximumItemTextLength = 48; visibleRows = 4; popup = "transient below-control state; no child registration"; focusedTests = if($isC135){"C133 retained: 44"}elseif($isC134){"C133 retained: 44"}elseif($isC133){44}else{"not part of this phase"}; hostTests = if($isC135){"C135 retains C134: 30 and C133: 24"}elseif($isC134){"C134: 30; C133 retained: 24"}elseif($isC133){24}else{"not part of this phase"} }
    progressBar = [ordered]@{ api = "GuideXosProgressBar"; minimum = 0; maximum = 65535; notesMinimum = 0; notesMaximum = 256; notesWidth = 312; maximumFillCells = 48; rendering = "bounded text-backed [fill-empty] percentage; floor integer arithmetic"; focusable = $false; controlHostRegistration = "absent"; focusedTests = if($isC125){50}elseif($isC126){"C125 regression in C126 image"}else{"not part of this phase"} }
    groupBox = [ordered]@{ api = "GuideXosGroupBox"; x = 12; y = 264; width = 456; height = 90; minimumWidth = 64; maximumWidth = 504; minimumHeight = 54; maximumHeight = 288; maximumCaptionLength = 48; caption = "Path Display"; render = "bounded text frame with clipped caption"; containment = "half-open"; relativeCoordinates = "TryResolvePoint"; focusable = $false; input = "none"; childOwnership = "none"; focusedTests = if($isC126){60}elseif($isC130 -or $isC127){"C126 regression image"}else{"not part of this phase"} }
    panel = [ordered]@{ api = "GuideXosPanel"; x = 12; y = 264; width = 456; height = 90; capacity = 4; supportedChildren = "Button, CheckBox, Label, Separator, RadioButton, ProgressBar, ComboBox"; membership = "fixed non-owning single-level insertion array; duplicate and cross-panel membership rejected"; coordinates = "local pixel positions; complete child rectangle must remain inside half-open panel bounds"; visibility = "panel visibility AND child-local visibility; hidden child is not focusable or activatable"; rendering = "delegates existing child renderers; no clipping claimed"; focusable = $false; controlHostRegistration = "absent"; focusedTests = if($isC135){"C135 retains C133 Panel semantics; menu is not a Panel child"}elseif($isC134){"C133 retained Panel semantics; C134 lifecycle and membership termination"}elseif($isC133){"C133 ComboBox membership, popup cancellation, and recovery"}elseif($isC128){"C127 membership plus C128 lifecycle cancellation and recovery"}elseif($isC130){"C127 managed Panel membership plus bounded wrapper retirement"}elseif($isC127){"managed panel membership and host integration"}else{"not part of this phase"}; interruptedActivation = if($isC135){"menu invoker invalidation, hide, disable, modal, unregister, and close release the shared lease; menu is outside Panel ownership"}elseif($isC134){"open ComboBox capture is cancelled on panel visibility, membership, enabled, focus, modal, unregister, and close transitions; stale input is consumed"}elseif($isC133){"open ComboBox popup is cancelled on panel visibility, membership, enabled, focus, and modal transitions; stale input is consumed"}elseif($isC128){"pending Space target is cancelled on visibility, membership, enabled, focus, and modal transitions; stale KeyChar is consumed exactly once"}else{"not part of this phase"} }
    radio = [ordered]@{ button = "GuideXosRadioButton"; group = "GuideXosRadioGroup"; labelMaximum = 48; groupCapacity = 4; focusedTests = "button, group, host"; registration = "explicit fixed array; duplicate and overflow rejected"; noSelection = -1; navigation = "Left/Up previous, Right/Down next, enabled-only, wrapping" }
    separator = [ordered]@{ api = "GuideXosSeparator"; orientation = "horizontal"; minimumWidth = 8; maximumWidth = 504; configuredNotesWidth = 480; renderColumns = 63; focusedTests = 48; hostTests = 33; controlHostRegistration = "absent" }
    notes = [ordered]@{ order = if ($isC132) { "Open, Save, Save As, Show Path, Full Path, File Name, Show Status, Document; C132 reuses the existing Full Path/File Name radio pair and keeps registration at 8" } elseif ($isC131) { "Open, Save, Save As, Show Path, Full Path, File Name, Show Status, Document; status checkbox is registered and controls only the existing status presentation" } elseif ($isC129) { "Open, Save, Save As, Document; C129 proof uses the existing four-control host and keeps presentation-only controls out of transport" } elseif ($isC130 -or $isC128 -or $isC127 -or $isC126 -or $isC125 -or $isC124) { "Open, Save, Save As, Show Path, Full Path, File Name, Document; Panel, GroupBox, label, separator, and progress bar are not registered" } elseif ($isC121 -or $isC122 -or $isC123) { "Open, Save, Save As, Show Path, Document; label and separator are not registered" } else { "Open, Save, Save As, Document" }; initialFocus = "none"; commands = if ($isC132) { "managed Notes Full Path/File Name radio pair uses pointer activation, KeyDown/KeyChar Space, Left/Right/Up/Down group navigation, forward Tab, reverse Shift+Tab, lifecycle cancellation, callback-driven path rendering, modal isolation, and close/relaunch checks" } elseif ($isC131) { "managed Notes Show status checkbox uses pointer activation, KeyDown/KeyChar Space, forward Tab, reverse Shift+Tab, callback-driven status rendering, and close/relaunch registration checks" } elseif ($isC129) { "physical QEMU QMP input-send-event Shift/Tab transitions, then plain tab, then printable a; Tab is KeyDown-only and the managed proof requires the production native-to-managed acknowledgements" } elseif ($isC130) { "managed C127 Panel fixture; obsolete direct-managed reverse helper is retired and C129 production Shift+Tab owns keyboard transport proof; native Reload retained" } elseif ($isC128) { "C128 repeats Notes hide/show and closes/relaunches Notes while preserving seven registrations and path display; C127 Panel remains non-owning and native Reload retained" } elseif ($isC127) { "managed Open/Save/Save As; Path Display Panel owns two radio memberships while GroupBox remains decorative; progress mirrors GuideXosTextArea.Length; native Reload retained" } elseif ($isC126) { "managed Open/Save/Save As; Path Display GroupBox is presentation-only around explicit C124 radios; progress mirrors GuideXosTextArea.Length; native Reload retained" } elseif ($isC125) { "managed Open/Save/Save As; progress mirrors GuideXosTextArea.Length; checkbox and radio presentation remain independent; native Reload retained" } elseif ($isC124) { "managed Open/Save/Save As; checkbox controls path-label visibility; radio group controls full-path/file-name presentation; native Reload retained" } elseif ($isC123) { "managed Open/Save/Save As; GuideXosLabel controls Path presentation; GuideXosSeparator divides content/status from command controls; native Reload retained" } elseif ($isC122) { "managed Open/Save/Save As; GuideXosLabel controls Path presentation; native Reload retained" } elseif ($isC121) { "managed Open/Save/Save As; checkbox controls Path presentation; native Reload retained" } else { "managed Open/Save/Save As; native Reload retained" }; space = if ($isC132) { "RadioButton selects only on KeyChar Space; KeyDown Space is ignored and pending gestures are cancelled by visibility, enabled, focus, Panel membership, and modal transitions" } elseif ($isC131) { "Show status toggles only on KeyChar Space; KeyDown Space is ignored and pending gestures are cancelled by visibility, enabled, focus, Panel membership, and modal transitions" } elseif ($isC129) { "not part of C129; existing C128 split-Space lifecycle behavior remains covered by the C128 fixture" } elseif ($isC130 -or $isC128 -or $isC127 -or $isC126 -or $isC125 -or $isC124) { "radio selection commits only on KeyChar Space; cancelled lifecycle gestures do not activate a recovered target; Panel and GroupBox remain outside the host" } elseif ($isC123 -or $isC122) { "checkbox toggles label visibility only from KeyChar Space; label and separator have no input API" } elseif ($isC121) { "checkbox toggles only from KeyChar Space; text/button/list/input routing remains isolated" } else { "exactly one Save activation from KeyChar Space" } }
    regressions = [ordered]@{ c116 = $true; c117 = $true; c118 = $true; c119 = $true; nativeNotepad = $true; counter = $true; status = $true }
    runtime = [ordered]@{ nativeAotSourceChanges = $false; gcChanges = $false; vfsChanges = $false; lifecycle = "resident managed image; runtime/PAL/GC/code manager/modules/mapping/heap preserve path" }
    freshBootCount = $FreshBootCount; qemuExecuted = -not $SkipQemu
    inputs = $inputs; sourceHashes = $sourceHashes; boots = @($bootResults)
    evidence = [ordered]@{ serial = "boot-01\serial.log"; managed = "managed-control-host-output.txt"; controlHost = "control-host-evidence.txt"; regressions = "regression-evidence.txt"; lifecycle = "lifecycle-evidence.txt" }
    documentation = if ($isC141) { "docs\dotnet\NATIVEAOT_C141_MANAGED_VERTICAL_STACK_LAYOUT.md" } elseif ($isC140) { "docs\dotnet\NATIVEAOT_C140_MANAGED_SCROLLVIEW.md" } elseif ($isC135) { "docs\dotnet\NATIVEAOT_C135_MANAGED_POPUP_MENU.md" } elseif ($isC134) { "docs\dotnet\NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING.md" } elseif ($isC132) { "docs\dotnet\NATIVEAOT_C132_MANAGED_RADIOBUTTON.md" } elseif ($isC131) { "docs\dotnet\NATIVEAOT_C131_MANAGED_CHECKBOX.md" } elseif ($isC130) { "docs\dotnet\NATIVEAOT_C130_C127_WRAPPER_STALL.md" } elseif ($isC129) { "docs\dotnet\NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT.md" } elseif ($isC128) { "docs\dotnet\NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE.md" } elseif ($isC127) { "docs\dotnet\NATIVEAOT_C127_MANAGED_PANEL.md" } elseif ($isC126) { "docs\dotnet\NATIVEAOT_C126_MANAGED_GROUP_BOX.md" } elseif ($isC125) { "docs\dotnet\NATIVEAOT_C125_MANAGED_PROGRESS_BAR.md" } elseif ($isC124) { "docs\dotnet\NATIVEAOT_C124_MANAGED_RADIO_BUTTON.md" } elseif ($isC123) { "docs\dotnet\NATIVEAOT_C123_MANAGED_SEPARATOR.md" } elseif ($isC122) { "docs\dotnet\NATIVEAOT_C122_MANAGED_LABEL.md" } elseif ($isC121) { "docs\dotnet\NATIVEAOT_C121_MANAGED_CHECKBOX.md" } else { "docs\dotnet\NATIVEAOT_C120_MANAGED_CONTROL_HOST.md" }
}
if ($isC133) {
    $manifest.notes.order = "Open, Save, Save As, Show Path, Status display ComboBox, Document; C133 replaces the Full Path/File Name radio pair with one registered non-editable ComboBox and changes registration from 8 to 7"
    $manifest.notes.commands = "managed Notes ComboBox uses pointer item selection, KeyDown/KeyChar Space, Up/Down highlight navigation, Enter/Space commit, Escape cancel, forward Tab, reverse Shift+Tab, transient outside-click capture, lifecycle cancellation, modal isolation, and close/relaunch checks"
    $manifest.notes.space = "ComboBox opens from KeyDown/KeyChar Space and commits the active row from KeyChar Space; pending gestures are cancelled by visibility, enabled, focus, Panel membership, and modal transitions"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C133_MANAGED_COMBOBOX.md"
}
if ($isC134) {
    $manifest.notes.order = "Open, Save, Save As, Status display, Full Path/File Name ComboBox, Document; C134 retains seven registrations and exercises production transient capture"
    $manifest.notes.commands = "production pointer opens the ComboBox, pointer item selection commits File Name, keyboard Down/Up/Enter/Space/Escape route through transient capture, outside clicks are consumed, Tab/Shift+Tab restore normal routing, lifecycle and modal transitions cancel capture, and close/relaunch restores seven registrations"
    $manifest.notes.space = "ComboBox opens from KeyDown/KeyChar Space; Down/Up move highlight; Enter/KeyChar Space commit; Escape, outside click, Tab, Shift+Tab, lifecycle, modal, and close cancel and release the single capture lease; no synthetic Tab KeyChar"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING.md"
}
if ($isC135) {
    $manifest.notes.order = "Open, Save, Save As, Status display, Full Path/File Name ComboBox, Document, Options popup; popup is one non-focusable registered transient owner"
    $manifest.notes.commands = "Options invokes a fixed four-row managed popup with Open, Save, separator, and Reload; pointer selection, Down/Up navigation, Enter/Space activation, Escape, outside-click consumption, Tab/Shift+Tab close-and-traverse, lifecycle cancellation, and close/relaunch are proven"
    $manifest.notes.space = "Popup Space activation is KeyDown-only; no synthetic KeyChar is required; Tab remains KeyDown-only and no popup child focus target is added"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C135_MANAGED_POPUP_MENU.md"
}
if ($isC136) {
    $manifest.hostAbi.inputTransport = "existing launchFlags field; PointerDown/PointerUp and Primary/Secondary identity are semantic event kinds; ABI v1/table 104 preserved"
    $manifest.controlHost.tests = "C136 secondary-pointer host lifecycle: 36 focused cases; C135 popup 40 API/40 host and C134 transient capture 30 retained"
    $manifest.notes.order = "Open, Save, Save As, Status display, Full Path/File Name ComboBox, Document, one reused Options popup; registration remains 8"
    $manifest.notes.commands = "secondary down records the Notes Document target; secondary up opens the existing popup at the pointer with bounded clamping; Save is activated by primary follow-up, Down/Enter, Escape, outside primary, and outside secondary"
    $manifest.notes.space = "C135 keyboard semantics remain unchanged; context invocation does not add focus targets, a capture stack, or synthetic Tab KeyChar"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU.md"
}
if ($isC137) {
    $manifest.hostAbi.inputTransport = "existing launchFlags field; wheel kind values 0x07..0x0F encode signed deltas -4..+4; 12-bit X/Y payload and primary/secondary button identity preserved; ABI v1/table 104 unchanged"
    $manifest.controlHost.tests = "C137 transport 12; TextArea 16; ListBox 18; total 46; C136 secondary context-menu behavior retained"
    $manifest.textArea = [ordered]@{ viewport = "first visible logical line; visible line count 4; max first line = total - visible"; wheelIncrement = 3; direction = "positive up, negative down"; caret = "logical caret unchanged; keyboard navigation restores visibility" }
    $manifest.listBox = [ordered]@{ viewport = "first visible logical item; visible rows 4; max first item = count - rows"; wheelIncrement = 3; selection = "wheel leaves selected index unchanged; keyboard/pointer navigation reconciles visibility" }
    $manifest.notes.order = "Open, Save, Save As, Show Path, Status display, Document, C137 TextArea, C137 ListBox; registration remains 8"
    $manifest.notes.commands = "physical QEMU wheel events route by pointer location to TextArea/ListBox; popup capture swallows wheel-through; secondary context menu and relaunch remain active"
    $manifest.notes.space = "C135/C136 keyboard and pointer semantics remain unchanged; wheel is an independent event and never synthesizes KeyDown or KeyChar"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING.md"
}
if ($isC138) {
    $manifest.hostAbi.inputTransport = "existing launchFlags field; pointer motion uses semantic kind 0 with the existing 12-bit X/Y payload; ABI v1/table 104 unchanged"
    $manifest.controlHost.capacity = 10
    $manifest.controlHost.tests = "C138 ScrollBar API 22; drag/host 20; TextArea binding 10; ListBox binding 10; total 62; C137 wheel 46 retained"
    $manifest.controlHost.pointerDrag = "one owner; primary thumb drag captures until primary release or lifecycle cancellation; transient popup lease and drag are mutually exclusive"
    $manifest.scrollBar = [ordered]@{ api = "GuideXosScrollBar"; orientation = "vertical"; range = "Minimum <= Value <= Maximum"; pageSize = "visible logical extent"; thumb = "floor(track * page / (range + page)), clamped to track and minimum 8 pixels"; mapping = "floor((Value-Minimum) * available / range), inverse rounded integer arithmetic with exact endpoints"; interaction = "arrow step, track page, primary thumb drag, Up/Down/Home/End, wheel; wheel ignored during drag"; rendering = "managed FillRect track and thumb; focused and disabled colors"; binding = "explicit Changed callback; TextArea FirstVisibleLine and ListBox FirstVisibleIndex remain authoritative" }
    $manifest.textArea = [ordered]@{ viewport = "FirstVisibleLine"; scrollbar = "control 10 at local 274,72,16,96"; initial = 0; final = 0; binding = "MaximumFirstVisibleLine and VisibleLineCount synchronize the scrollbar" }
    $manifest.listBox = [ordered]@{ viewport = "FirstVisibleIndex"; scrollbar = "control 11 at local 500,72,16,72"; initial = 0; final = 0; selection = "drag preserves selection; pointer click after bottom drag selects logical row 8"; binding = "MaximumFirstVisibleIndex and VisibleRowCount synchronize the scrollbar" }
    $manifest.notes.order = "Open, Save, Save As, Show Path, Status display, Document, C138 TextArea, C137 ListBox, C138 TextArea ScrollBar, C138 ListBox ScrollBar; registration changes 8 -> 10"
    $manifest.notes.commands = "physical QEMU pointer motion/down/up proves TextArea thumb drag outside bounds, track paging, popup blocking, ListBox drag with selection preservation, pointer row mapping, wheel after drag, and close/relaunch"
    $manifest.notes.capture = "final transient popup capture none; final pointer drag owner none"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C138_MANAGED_SCROLLBAR.md"
}
if ($isC139) {
    $manifest.controlHost.tests = "C139 viewport 30; TextArea migration 10; ListBox migration 10; cross-control 4; total 54; C138 62 and C137 46 retained"
    $manifest.scrollViewport = [ordered]@{ api = "GuideXosVerticalViewport"; invariants = "ContentExtent >= 0; VisibleExtent >= 0; 0 <= Offset <= MaximumOffset; MaximumOffset=max(0, ContentExtent-VisibleExtent)"; smallChange = 1; largeChange = "max(1, VisibleExtent-1)"; notification = "Changed fires once only for effective Offset changes; bounded reentrant dispatch"; binding = "ScrollBar derives range/page/value from the shared viewport" }
    $manifest.notes.order = "Open, Save, Save As, Show Path, Status display, Document, C139 TextArea, C137 ListBox, C139 TextArea ScrollBar, C139 ListBox ScrollBar; registration remains 10"
    $manifest.notes.commands = "C139 reuses the C138 physical wheel, ScrollBar drag, track paging, popup conflict, ListBox selection, pointer mapping, and close/relaunch proof while exercising shared viewport state"
    $manifest.notes.capture = "final transient popup capture none; final pointer drag owner none"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT.md"
}
if ($isC140) {
    $manifest.hostAbi.inputTransport = "existing pointer, wheel, button, and keyboard launchFlags transport; ABI v1/table 104 unchanged"
    $manifest.controlHost.capacity = 10
    $manifest.controlHost.tests = "C140 ScrollView core 20; clipping 9; translated hit-test 10; focus 9; ScrollBar binding 10; total 58; C139/C138/C137 regressions retained"
    $manifest.scrollView = [ordered]@{ api = "GuideXosScrollView"; bounds = "outer frame plus 1-pixel inner viewport"; maximumMemberCount = 8; membership = "fixed non-owning direct ordinary controls; duplicate, nested container, and cross-owner membership rejected"; supportedMembers = "Button, CheckBox, Label, Separator, RadioButton, ProgressBar, ComboBox"; unsupportedMembers = "TextArea, ListBox, Panel, ScrollView"; coordinates = "stable logical content-space positions; child bounds are translated to screen coordinates only at membership/resize"; extent = "maximum visible member bottom; hidden members contribute no extent; shared GuideXosVerticalViewport clamps offset"; clipping = "surface clip plus y translation; rectangles exact, text rows crossing a vertical clip edge suppressed conservatively"; hitTest = "visible inner rectangle only; reverse insertion order; screen-to-content offset mapping; hidden skipped; disabled targeted without activation"; input = "wheel consumed only inside viewport; no wheel-through; pointer/focus routed to translated ordinary members; Tab enters/leaves host scope cleanly"; nesting = "intentional single-level policy; Panel remains a separate non-owning direct container"; scrollbar = "explicit GuideXosScrollBar binding to the same viewport; direct value, wheel, page, drag, shrink, and unbind covered" }
    $manifest.notes.order = "C140 Managed ScrollView proof application; title/ordinary controls live inside one registered ScrollView scope and one bound ScrollBar"
    $manifest.notes.commands = "physical QEMU wheel, translated member pointer activation, ScrollBar thumb drag, Tab focus reveal, three fresh launches, and managed relaunch/reset checks"
    $manifest.notes.capture = "final pointer drag owner none; ScrollView has no transient popup lease"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C140_MANAGED_SCROLLVIEW.md"
}
if ($isC141) {
    $manifest.hostAbi.inputTransport = "existing pointer, wheel, button, keyboard, and Shift payload transport; ABI v1/table 104 unchanged"
    $manifest.controlHost.capacity = 8
    $manifest.controlHost.tests = "C141 core layout 20; visibility 10; ScrollView integration 10; focus 10; popup 6; total 56; C140 58/C139 54/C138 62/C137 46 retained"
    $manifest.verticalStack = [ordered]@{
        api = "GuideXosVerticalStack"
        capacity = 8
        membership = "fixed insertion-order references; non-owning; duplicate, overflow, unsupported type, Panel conflict, and second-stack conflict rejected"
        supportedMembers = "Button, CheckBox, Label, Separator, RadioButton, ProgressBar, ComboBox"
        rejectedMembers = "TextArea, ListBox, Panel, ScrollView, nested GuideXosVerticalStack"
        widthPolicy = "Stretch or KeepWidth; character-aligned controls round stretched width down to 8-pixel cells"
        heightPolicy = "existing control height is preserved"
        padding = "bounded Top/Bottom/Left/Right integer padding"
        spacing = "bounded non-negative integer between visible members only"
        visibility = "hidden members consume no space; disabled members retain space"
        contentHeight = "arranged visible member bottoms plus bottom padding, measured from Stack.Y"
        relayout = "explicit PerformLayout(); add/remove also relayout; visibility callback calls PerformLayout(scrollView)"
        composition = "same direct controls may be registered with ScrollView and referenced by one stack; Stack has no registration or viewport"
        focus = "ScrollView remains authoritative for traversal and reveal; stack is not focusable"
        popup = "ComboBox popup reads current arranged bounds; popup/capture state remains outside the stack"
    }
    $manifest.notes.order = "C141 production ScrollView: Label, CheckBox, ComboBox, Separator, two RadioButtons, ProgressBar, Button; all eight are direct ScrollView members and one non-owning stack reference"
    $manifest.notes.commands = "physical QEMU ComboBox open/follow-up, checkbox visibility relayout, wheel, ScrollBar drag, translated moved-button activation, five forward Tab events, and relaunch/reset"
    $manifest.notes.space = "stack changes geometry only; no synthetic Tab KeyChar, capture stack, second focus system, recursive ownership, or wheel handling"
    $manifest.notes.capture = "final ComboBox transient capture none; final ScrollBar pointer drag owner none"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C141_MANAGED_VERTICAL_STACK_LAYOUT.md"
}
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $EvidenceRoot ("{0}.manifest.json" -f $phaseLower)) -Encoding ASCII
Write-Host "$ProofPhase outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
