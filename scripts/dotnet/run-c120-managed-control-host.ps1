param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$CompositeElfPath = "",
    [string]$ProofKernelElfPath = "",
    [string]$PythonExe = "",
    [int]$FreshBootCount = 3,
    [int]$TimeoutSeconds = 360,
    [ValidateSet("C120", "C121", "C122", "C123", "C124", "C125", "C126", "C127", "C128", "C129", "C130", "C131", "C132", "C133", "C134", "C135", "C136", "C137", "C138", "C139", "C140", "C141", "C142", "C143", "C144", "C145", "C146", "C147", "C148", "C149", "C150")]
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
$isC150 = $ProofPhase -eq "C150"
$isC149 = $ProofPhase -eq "C149" -or $isC150
$isC148 = $ProofPhase -eq "C148" -or $isC149
$isC147 = $ProofPhase -eq "C147"
$isC146 = $ProofPhase -eq "C146" -or $isC147 -or $isC148
if ($isC146 -and $FreshBootCount -ne 3) { throw "C146 requires exactly three independent persistence sequences." }
if ($isC146 -and $SkipQemu) { throw "C146 acceptance requires the real write/read QEMU sequences and ordinary restoration boots." }
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
$isC142 = $ProofPhase -eq "C142"
$isC143 = $ProofPhase -eq "C143"
$isC144 = $ProofPhase -eq "C144"
$isC145 = $ProofPhase -eq "C145" -or $isC146
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
    $EvidenceRoot = if ($isC150) {
        Join-Path $RepoRoot "out\dotnet\c150-managed-app-return-relaunch"
    } elseif ($isC149) {
        Join-Path $RepoRoot "out\dotnet\c149-second-runtime-setting"
    } elseif ($isC148) {
        Join-Path $RepoRoot "out\dotnet\c148-settings-v2-scroll-amount"
    } elseif ($isC147) {
        Join-Path $RepoRoot "out\dotnet\c147-runtime-settings"
    } elseif ($isC146) {
        Join-Path $RepoRoot "out\dotnet\c146-settings-persistence"
    } elseif ($isC145) {
        Join-Path $RepoRoot "out\dotnet\c145-managed-modal-dialog"
    } elseif ($isC144) {
        Join-Path $RepoRoot "out\dotnet\c144-managed-settings-center"
    } elseif ($isC143) {
        Join-Path $RepoRoot "out\dotnet\c143-managed-groupbox"
    } elseif ($isC142) {
        Join-Path $RepoRoot "out\dotnet\c142-stack-margin-alignment"
    } elseif ($isC141) {
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

function Get-DirectoryHash([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return $null }
    $entries = [System.Collections.Generic.List[string]]::new()
    $files = @(Get-ChildItem -LiteralPath $Path -File -Recurse | Sort-Object FullName)
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($Path.TrimEnd('\', '/').Length).TrimStart('\', '/').Replace('\', '/')
        $entries.Add($relative + ':' + (Get-Hash $file.FullName))
    }
    $payload = [System.Text.Encoding]::UTF8.GetBytes([string]::Join("`n", $entries))
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try { return ([System.BitConverter]::ToString($sha256.ComputeHash($payload)) -replace '-', '').ToUpperInvariant() }
    finally { $sha256.Dispose() }
}

function Get-C146PersistedSnapshot([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "C146 settings record is missing after write: $Path" }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 12 -or [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'GXSC' -or
        $bytes[5] -ne 0 -or $bytes[7] -ne 0 -or
        $bytes[8] -ne 0 -or $bytes[9] -ne 0 -or $bytes[10] -ne 0 -or $bytes[11] -ne 0) {
        throw "Settings record has an unexpected magic or header: $Path"
    }
    $version = [int]$bytes[4]
    $payloadBytes = if ($version -eq 1) { 9 } elseif ($version -eq 2) { 10 } else { 0 }
    $checksumOffset = 12 + $payloadBytes
    if ($payloadBytes -eq 0 -or $bytes[6] -ne $payloadBytes -or
        $bytes.Length -ne $checksumOffset + 4 -or
        (Get-C147Crc32 ([byte[]]$bytes[0..($checksumOffset - 1)])) -ne
            [System.BitConverter]::ToUInt32($bytes, $checksumOffset)) {
        throw "Settings record has an invalid version, size, or checksum: $Path"
    }
    [pscustomobject]@{
        path = $Path; size = $bytes.Length; magic = 'GXSC'; version = $version
        density = [int]$bytes[12]; showStatus = [int]$bytes[13]
        advanced = [int]$bytes[14]; inputEnabled = [int]$bytes[15]
        naturalScroll = [int]$bytes[16]; speed = [int]$bytes[17]
        keyboardTips = [int]$bytes[18]; detail = [int]$bytes[19]
        reportFormat = [int]$bytes[20]
        scrollLines = if ($version -eq 1) { 3 } else { [int]$bytes[21] }
        sha256 = Get-Hash $Path
    }
}

function Get-C147Crc32([byte[]]$Bytes) {
    [uint32]$crc = [uint32]::MaxValue
    [uint32]$polynomial = [Convert]::ToUInt32("EDB88320", 16)
    foreach ($value in $Bytes) {
        $crc = [uint32]($crc -bxor [uint32]$value)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band [uint32]1) -ne 0) {
                $crc = [uint32](($crc -shr 1) -bxor $polynomial)
            } else {
                $crc = [uint32]($crc -shr 1)
            }
        }
    }
    return [uint32]($crc -bxor [uint32]::MaxValue)
}

function New-C147UnsupportedVersionRecord([string]$ValidV1Path) {
    $bytes = [System.IO.File]::ReadAllBytes($ValidV1Path)
    if ($bytes.Length -ne 25 -or
        [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne "GXSC" -or
        $bytes[4] -ne 1 -or $bytes[5] -ne 0) {
        throw "C147 could not derive a future-version fixture from a valid v1 record: $ValidV1Path"
    }
    $bytes[4] = 2
    $bytes[5] = 0
    $crc = Get-C147Crc32 ([byte[]]$bytes[0..20])
    [byte[]]$checksum = [System.BitConverter]::GetBytes([uint32]$crc)
    [Array]::Copy($checksum, 0, $bytes, 21, 4)
    return ,$bytes
}

function New-C148AuthenticV1Record() {
    [byte[]]$bytes = @(
        0x47,0x58,0x53,0x43,0x01,0x00,0x09,0x00,0x00,0x00,0x00,0x00,
        0x01,0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0xB6,0x1B,0x6A,0x36)
    $hash = [System.BitConverter]::ToString(
        [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)) -replace '-', ''
    if ($hash.ToUpperInvariant() -ne '952CA2183EE7DA2923EF76DBC121193C499750BDA7627D862D7F8B8BF5734FF5') {
        throw "Embedded authentic C147 v1 fixture hash mismatch: $hash"
    }
    return ,$bytes
}

function New-C148V2Record([byte[]]$V1Bytes, [int]$ScrollLines) {
    if ($V1Bytes.Length -ne 25 -or $V1Bytes[4] -ne 1 -or
        $ScrollLines -lt 1 -or $ScrollLines -gt 8) {
        throw 'Cannot construct C148 v2 proof record from the requested source/value.'
    }
    [byte[]]$bytes = [byte[]]::new(26)
    [Array]::Copy($V1Bytes, 0, $bytes, 0, 21)
    $bytes[4] = 2; $bytes[6] = 10; $bytes[21] = [byte]$ScrollLines
    [byte[]]$checksum = [System.BitConverter]::GetBytes(
        [uint32](Get-C147Crc32 ([byte[]]$bytes[0..21])))
    [Array]::Copy($checksum, 0, $bytes, 22, 4)
    return ,$bytes
}

function New-C149V2Record([byte[]]$V1Bytes, [int]$ScrollLines,
                          [bool]$ShowKeyboardTips) {
    [byte[]]$bytes = New-C148V2Record $V1Bytes $ScrollLines
    $bytes[18] = if ($ShowKeyboardTips) { 1 } else { 0 }
    [byte[]]$checksum = [System.BitConverter]::GetBytes(
        [uint32](Get-C147Crc32 ([byte[]]$bytes[0..21])))
    [Array]::Copy($checksum, 0, $bytes, 22, 4)
    return ,$bytes
}

function New-C149C148EraV2Fixture() {
    [byte[]]$bytes = @(
        0x47,0x58,0x53,0x43,0x02,0x00,0x0A,0x00,0x00,0x00,0x00,0x00,
        0x01,0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x07,0x1C,0x6F,0x1C,0xAD)
    $hash = [System.BitConverter]::ToString(
        [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)) -replace '-', ''
    if ($hash.ToUpperInvariant() -ne '006409696A838F8E27AEC444BB23B51A4603687961CD3A38E5ECA2DF9B46CBB9') {
        throw "C148 v2 compatibility fixture hash mismatch: $hash"
    }
    return ,$bytes
}

function New-C149UnsupportedV3Record([byte[]]$ValidV2Bytes) {
    if ($ValidV2Bytes.Length -ne 26 -or $ValidV2Bytes[4] -ne 2) {
        throw 'C149 unsupported-version fixture source is not v2.'
    }
    [byte[]]$bytes = $ValidV2Bytes.Clone()
    $bytes[4] = 3
    [byte[]]$checksum = [System.BitConverter]::GetBytes(
        [uint32](Get-C147Crc32 ([byte[]]$bytes[0..21])))
    [Array]::Copy($checksum, 0, $bytes, 22, 4)
    return ,$bytes
}

function New-C148MalformedV2Record([byte[]]$ValidV2Bytes) {
    [byte[]]$bytes = [byte[]]$ValidV2Bytes.Clone()
    if ($bytes.Length -ne 26 -or $bytes[4] -ne 2) { throw 'Malformed-v2 source is not v2.' }
    $bytes[21] = 0
    [byte[]]$checksum = [System.BitConverter]::GetBytes(
        [uint32](Get-C147Crc32 ([byte[]]$bytes[0..21])))
    [Array]::Copy($checksum, 0, $bytes, 22, 4)
    return ,$bytes
}

function Get-C146SnapshotOutputPattern([string]$StateLine, [string]$ValuesLine) {
    $marker = '^\[C102-MANAGED-OUTPUT\] '
    return '(?m)' + $marker + [regex]::Escape($StateLine) + '\r?\n' +
        $marker + [regex]::Escape($ValuesLine) + '\r?$'
}

function Test-C146SnapshotOutput([string]$Text, [string]$StateLine, [string]$ValuesLine) {
    return $Text -match (Get-C146SnapshotOutputPattern $StateLine $ValuesLine)
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
                $resultPattern = if ($isC142) {
                    '(?m)^\[C102-MANAGED-OUTPUT\] C142-TESTS .*result=PASS'
                } elseif ($isC141) {
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

function Stop-C148QemuCleanly([int]$Port, [System.Diagnostics.Process]$Process,
                              [string]$LogPath) {
    if ($Process.HasExited) { return }
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $client.Connect("127.0.0.1", $Port)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 200
        $greeting = Read-C129QmpResponse $stream 2
        $capabilities = [System.Text.Encoding]::ASCII.GetBytes(
            ('{"execute":"qmp_capabilities"}' + "`n"))
        $stream.Write($capabilities, 0, $capabilities.Length)
        $stream.Flush()
        $capabilityResponse = Read-C129QmpResponse $stream 2
        $quit = [System.Text.Encoding]::ASCII.GetBytes(
            ('{"execute":"quit"}' + "`n"))
        $stream.Write($quit, 0, $quit.Length)
        $stream.Flush()
        $quitResponse = Read-C129QmpResponse $stream 2
        Add-Content -LiteralPath $LogPath -Value (
            "shutdown=quit`ngreeting=$greeting`ncapabilities=$capabilityResponse`nresponse=$quitResponse") -Encoding ASCII
    } finally {
        if ($client) { $client.Dispose() }
    }
    Wait-Process -Id $Process.Id -Timeout 10 -ErrorAction SilentlyContinue
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

function Get-C142HexField([string]$Serial, [string]$Field) {
    $match = [regex]::Match($Serial, "(?m)^\[C142-TARGET\].*\b$Field=([0-9A-Fa-f]+)")
    if (-not $match.Success) { throw "C142 target did not contain $Field." }
    return [Convert]::ToInt32($match.Groups[1].Value, 16)
}

function Get-C143HexField([string]$Serial, [string]$Field) {
    $match = [regex]::Match($Serial, "(?m)^\[C143-TARGET\].*\b$Field=([0-9A-Fa-f]+)")
    if (-not $match.Success) { throw "C143 target did not contain $Field." }
    return [Convert]::ToInt32($match.Groups[1].Value, 16)
}

function Get-C143CurrentOffset([string]$Serial) {
    $matches = [regex]::Matches(
        $Serial,
        '(?m)^\[C102-MANAGED-OUTPUT\] C143-GEOMETRY .*\boffset=(\d+) state=')
    if ($matches.Count -eq 0) { throw "C143 geometry did not report a viewport offset." }
    return [int]$matches[$matches.Count - 1].Groups[1].Value
}

function Get-C144HexField([string]$Serial, [string]$Field) {
    $match = [regex]::Match($Serial, "(?m)^\[C144-TARGET\].*\b$Field=([0-9A-Fa-f]+)")
    if (-not $match.Success) { throw "C144 target did not contain $Field." }
    return [Convert]::ToInt32($match.Groups[1].Value, 16)
}

function Assert-C147NotesStartupBehavior([string]$Serial, [int]$Natural,
                                         [string]$BootRole) {
    $firstAfter = if ($Natural -eq 0) { 3 } else { 0 }
    $firstChanged = if ($Natural -eq 0) { 'true' } else { 'false' }
    $secondBefore = if ($Natural -eq 0) { 3 } else { 0 }
    $secondAfter = 3 - $secondBefore
    $firstPattern = '(?m)^\[C102-MANAGED-OUTPUT\] C147-CONSUMER app=ManagedNotes control=ListBox natural={0} delta=-1 before=0 after={1} changed={2} result=PASS\r?$' -f `
        $Natural, $firstAfter, $firstChanged
    $secondPattern = '(?m)^\[C102-MANAGED-OUTPUT\] C147-CONSUMER app=ManagedNotes control=ListBox natural={0} delta=1 before={1} after={2} changed=true result=PASS\r?$' -f `
        $Natural, $secondBefore, $secondAfter
    $first = [regex]::Match($Serial, $firstPattern)
    $second = [regex]::Match($Serial, $secondPattern)
    if (-not $first.Success -or -not $second.Success -or
        $second.Index -le $first.Index) {
        throw "C147 $BootRole Notes physical wheel behavior did not match Natural Scroll=$Natural before Settings Center opened."
    }
}

function Assert-C148NotesStartupBehavior([string]$Serial, [int]$Natural,
    [int]$Amount, [string]$BootRole, [bool]$NotesOnly) {
    $first = if ($Natural -eq 0) { 16 + $Amount } else { 16 - $Amount }
    $firstBack = if ($Natural -eq 0) { 16 } else { 16 }
    $firstPattern = '(?m)^\[C102-MANAGED-OUTPUT\] C148-NOTES-WHEEL normalizedNotches=-1 naturalScroll={0} scrollLines={1} firstVisible=16->{2} result=PASS\r?$' -f $Natural, $Amount, $first
    $secondPattern = '(?m)^\[C102-MANAGED-OUTPUT\] C148-NOTES-WHEEL normalizedNotches=1 naturalScroll={0} scrollLines={1} firstVisible={2}->{3} result=PASS\r?$' -f $Natural, $Amount, $first, $firstBack
    $readyPattern = '(?m)^\[C102-MANAGED-OUTPUT\] C148-NOTES-FIRST control=ListBox firstVisible=16 scrollLines={0} before-settings-center=true result=PASS\r?$' -f $Amount
    if ($Serial -notmatch $firstPattern -or $Serial -notmatch $secondPattern -or
        $Serial -notmatch $readyPattern) {
        throw "C148 $BootRole Notes ListBox movement did not match NaturalScroll=$Natural, ScrollLinesPerNotch=$Amount."
    }
    $firstIndex = $Serial.IndexOf('C148-NOTES-WHEEL')
    $secondIndex = $Serial.IndexOf('C148-NOTES-WHEEL', $firstIndex + 1)
    $settingsIndex = $Serial.IndexOf('[C144-PROOF]')
    if ($firstIndex -lt 0 -or $secondIndex -le $firstIndex -or
        ($NotesOnly -and ($Serial -notmatch '(?m)^\[C148-NOTES-ONLY\] settings-center=not-launched result=PASS' -or $settingsIndex -ge 0)) -or
        (-not $NotesOnly -and ($settingsIndex -le $secondIndex))) {
        throw "C148 $BootRole did not prove Notes-before-Settings-Center ordering."
    }
}

function Assert-C150NotesOnlyStartupState([string]$Serial, [int]$Natural,
    [int]$Amount, [string]$BootRole) {
    $runtimePattern = '(?m)^\[C102-MANAGED-OUTPUT\] C148-RUNTIME-SETTING source=(?:file|invalid) naturalScroll={0} fileVersion=\d+ scrollLines={1} ready=true before-application=true result=PASS\r?$' -f $Natural, $Amount
    $firstPattern = '(?m)^\[C102-MANAGED-OUTPUT\] C148-NOTES-FIRST control=ListBox firstVisible=16 scrollLines={0} before-settings-center=true result=PASS\r?$' -f $Amount
    if ($Serial -notmatch $runtimePattern -or $Serial -notmatch $firstPattern -or
        $Serial -notmatch '(?m)^\[C148-NOTES-ONLY\] settings-center=not-launched result=PASS\r?$' -or
        $Serial -match '(?m)^\[C144-PROOF\]') {
        throw "C150 $BootRole notes-only boot did not validate its current NaturalScroll/ScrollLines startup state without opening Settings Center."
    }
}

function Assert-C149NotesTipBehavior([string]$Serial, [bool]$Expected,
                                    [string]$BootRole) {
    $value = if ($Expected) { 'true' } else { 'false' }
    $pattern = '(?m)^\[C102-MANAGED-OUTPUT\] C149-NOTES-TIPS startup=before-settings-center visible={0} rendered=true result=PASS\r?$' -f $value
    if ($Serial -notmatch $pattern) {
        throw "C149 $BootRole Managed Notes did not render the persisted keyboard-tip value=$value before Settings Center."
    }
}

function Assert-C149AppliedConsumerBehavior([string]$Serial, [bool]$Expected,
                                             [string]$BootRole) {
    $value = if ($Expected) { '1' } else { '0' }
    $pattern = '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-APPLY oldKeyboardTips=[01] newKeyboardTips={0} persistence=verified runtime=committed result=PASS\r?$' -f $value
    if ($Serial -notmatch $pattern -or
        $Serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-CONSUMER-APPLY persistence=verified runtime=published render=fresh-notes-launch result=PASS\r?$') {
        throw "C149 $BootRole did not publish keyboardTips=$value for the next real Managed Notes launch."
    }
}

function Assert-C149SingleSurfaceBoundary([string]$Serial, [string]$BootRole) {
    $notesCreate = $Serial.IndexOf('C111-SURFACE] action=create title=Managed Notes')
    $notesClose = $Serial.IndexOf('C111-SURFACE] action=close result=PASS', $notesCreate)
    $settingsCreate = $Serial.IndexOf('C111-SURFACE] action=create title=Managed Settings Center')
    $settingsClose = $Serial.LastIndexOf('C111-SURFACE] action=close result=PASS')
    $lastNotesCreate = $Serial.LastIndexOf('C111-SURFACE] action=create title=Managed Notes')
    $lastNotesDispatch = $Serial.LastIndexOf('C149-NOTES-TIPS dispatch=runtime-current')
    $boundaryProven = $notesCreate -ge 0 -and $notesClose -gt $notesCreate -and
        $settingsCreate -gt $notesClose -and $settingsClose -gt $settingsCreate -and
        $lastNotesCreate -eq $notesCreate -and $lastNotesDispatch -lt $settingsCreate
    if (-not $boundaryProven) {
        throw "C149 $BootRole did not prove that Settings Center replaces the sole Notes surface."
    }
}

function Assert-C150ReturnBoundary([string]$Serial, [string]$BootRole,
                                   [bool]$ExpectedTips, [int]$ExpectedNatural,
                                   [int]$ExpectedLines) {
    $direct = [regex]::Match($Serial, '(?m)^\[C150-DIRECT\].*return-target=none notes-launched=false after-close=none result=PASS\r?$')
    $stress = [regex]::Match($Serial, '(?m)^\[C150-STRESS\] cycles=25 launch-generations=[0-9A-Fa-f]{8} surface-generations=[0-9A-Fa-f]{8} target=none active=Notes modal=none capture=none drag=none result=PASS\r?$')
    $consumed = [regex]::Matches($Serial, '(?m)^\[C150-RETURN-CONSUMED\] id=com\.guidexos\.apps\.managed\.notes target=cleared-before-launch\r?$')
    $returns = [regex]::Matches($Serial, '(?m)^\[C150-RETURN-RESULT\] id=com\.guidexos\.apps\.managed\.notes normal-launch=PASS target=none\r?$')
    $surfaces = [regex]::Matches($Serial, '(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.(?:notes|settingscenter) generation=[0-9A-Fa-f]{8} window=[0-9A-Fa-f]{8} result=PASS\r?$')
    $lastDestroy = $Serial.LastIndexOf('[C150-SURFACE] action=destroy appId=com.guidexos.apps.managed.settingscenter')
    $lastNotesSurface = $Serial.LastIndexOf('[C150-SURFACE] action=create appId=com.guidexos.apps.managed.notes')
    $lastReturnResult = $Serial.LastIndexOf('[C150-RETURN-RESULT] id=com.guidexos.apps.managed.notes')
    $activePassed = $Serial -match '(?m)^\[C150-RELAUNCH\] generation=[0-9A-Fa-f]{8} appId=com\.guidexos\.apps\.managed\.notes surface-generation=[0-9A-Fa-f]{8} instance=fresh target=none registration=bounded result=PASS\r?$'
    $tipValue = if ($ExpectedTips) { 'true' } else { 'false' }
    $tipRecords = [regex]::Matches($Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C149-NOTES-TIPS startup=after-return visible=(true|false) rendered=true result=PASS\r?$')
    $finalTipsMatch = $tipRecords.Count -gt 0 -and $tipRecords[$tipRecords.Count - 1].Groups[1].Value -eq $tipValue
    $naturalDownFirst = if ($ExpectedNatural -eq 0) { 16 + $ExpectedLines } else { 16 - $ExpectedLines }
    $wheelDown = '(?m)^\[C102-MANAGED-OUTPUT\] C148-NOTES-WHEEL normalizedNotches=-1 naturalScroll={0} scrollLines={1} firstVisible=16->{2} result=PASS\r?$' -f $ExpectedNatural, $ExpectedLines, $naturalDownFirst
    $wheelUp = '(?m)^\[C102-MANAGED-OUTPUT\] C148-NOTES-WHEEL normalizedNotches=1 naturalScroll={0} scrollLines={1} firstVisible={2}->16 result=PASS\r?$' -f $ExpectedNatural, $ExpectedLines, $naturalDownFirst
    $wheelContinuity = $Serial -match $wheelDown -and $Serial -match $wheelUp -and
        $Serial -match '(?m)^\[C150-RUNTIME-CONTINUITY\] app=returned-Notes naturalScroll=shared scrollLines=shared wheel=consumed result=PASS\r?$'
    $workflowPassed = $true
    $primaryIndex = $Serial.IndexOf('[C150-PRIMARY-ENTRY]')
    $afterPrimary = if ($primaryIndex -ge 0) { $Serial.Substring($primaryIndex) } else { [string]::Empty }
    $readyAfterPrimary = [regex]::Matches($afterPrimary, '(?m)^\[C150-RETURN-READY\] id=com\.guidexos\.apps\.managed\.notes close=complete-after-dispatch\r?$')
    $consumedAfterPrimary = [regex]::Matches($afterPrimary, '(?m)^\[C150-RETURN-CONSUMED\] id=com\.guidexos\.apps\.managed\.notes target=cleared-before-launch\r?$')
    $resultAfterPrimary = [regex]::Matches($afterPrimary, '(?m)^\[C150-RETURN-RESULT\] id=com\.guidexos\.apps\.managed\.notes normal-launch=PASS target=none\r?$')
    if ($BootRole -eq 'discard') {
        $workingIndex = $afterPrimary.IndexOf('C149-WORKING keyboardTips=1 runtime=preserved persisted=preserved dirty=true result=PASS')
        $discardIndex = $afterPrimary.IndexOf('C145-UNSAVED result=Discard applied=preserved closed=true result=PASS')
        $workflowPassed = $primaryIndex -ge 0 -and $workingIndex -gt 0 -and
            $discardIndex -gt $workingIndex -and $readyAfterPrimary.Count -eq 1 -and
            $consumedAfterPrimary.Count -eq 1 -and $resultAfterPrimary.Count -eq 1 -and
            $readyAfterPrimary[0].Index -gt $workingIndex -and
            $consumedAfterPrimary[0].Index -gt $readyAfterPrimary[0].Index -and
            $consumedAfterPrimary[0].Index -gt $discardIndex -and
            $resultAfterPrimary[0].Index -gt $consumedAfterPrimary[0].Index
    } elseif ($BootRole -eq 'cancel-retry') {
        $resetCancelIndex = $afterPrimary.IndexOf('C145-RESET result=Cancel working=preserved viewport=preserved focus=restored result=PASS')
        $closeCancelIndex = $afterPrimary.IndexOf('C145-UNSAVED result=Cancel dirty=preserved parent=open focus=restored result=PASS')
        $injectIndex = $afterPrimary.IndexOf('C150-MANAGED-OUTPUT] C150-FAILURE-INJECT armed=true result=PASS')
        $applyFailureIndex = $afterPrimary.IndexOf('C146-APPLY-FAIL messagebox=opened working=preserved applied=preserved persisted=preserved dirty=true result=PASS')
        $failedCloseIndex = $afterPrimary.IndexOf('C146-UNSAVED result=Apply failed=kept-open dirty=true result=PASS')
        $dismissIndex = $afterPrimary.IndexOf('C150-MANAGED-OUTPUT] C150-ERROR-DISMISSED settings=active retry=available result=PASS')
        $retryApplyIndex = $afterPrimary.IndexOf('C149-RUNTIME-APPLY oldKeyboardTips=0 newKeyboardTips=1 persistence=verified runtime=committed result=PASS')
        $closeApplyIndex = $afterPrimary.IndexOf('C145-UNSAVED result=Apply applied=committed closed=true result=PASS')
        $workflowPassed = $primaryIndex -ge 0 -and $resetCancelIndex -ge 0 -and
            $closeCancelIndex -gt $resetCancelIndex -and $injectIndex -gt $closeCancelIndex -and
            $applyFailureIndex -gt $injectIndex -and $failedCloseIndex -gt $applyFailureIndex -and
            $dismissIndex -gt $failedCloseIndex -and
            $retryApplyIndex -gt $dismissIndex -and $closeApplyIndex -gt $retryApplyIndex -and
            $readyAfterPrimary.Count -eq 1 -and $consumedAfterPrimary.Count -eq 1 -and
            $resultAfterPrimary.Count -eq 1 -and $readyAfterPrimary[0].Index -gt $retryApplyIndex -and
            $closeApplyIndex -gt $readyAfterPrimary[0].Index -and
            $consumedAfterPrimary[0].Index -gt $closeApplyIndex -and
            $resultAfterPrimary[0].Index -gt $consumedAfterPrimary[0].Index
    }
    if (-not $direct.Success -or -not $stress.Success -or $consumed.Count -lt 25 -or
        $returns.Count -lt 25 -or $surfaces.Count -lt 53 -or -not $activePassed -or
        $lastDestroy -lt 0 -or $lastNotesSurface -le $lastDestroy -or
        $lastReturnResult -lt $lastNotesSurface -or -not $finalTipsMatch -or
        -not $wheelContinuity -or -not $workflowPassed) {
        throw "C150 $BootRole did not prove one-hop relaunch, fresh settings and wheel continuity, and its required Settings Center workflow."
    }
}

function Get-C144Geometry([string]$Serial, [string]$State = "") {
    $geometryMatches = [regex]::Matches(
        $Serial,
        '(?m)^\[C102-MANAGED-OUTPUT\] C144-GEOMETRY state=(?<state>\S+) seq=(?<seq>\d+) part=(?<part>\d+) (?<fields>[^\r\n]+)')
    if ($geometryMatches.Count -eq 0) { throw "C144 geometry did not report layout state." }
    $candidateSequences = @(
        $geometryMatches |
            Where-Object { [string]::IsNullOrEmpty($State) -or $_.Groups['state'].Value -eq $State } |
            ForEach-Object { [int]$_.Groups['seq'].Value } |
            Sort-Object -Unique)
    if ($candidateSequences.Count -eq 0) { throw "C144 geometry did not report state '$State'." }
    $sequence = $candidateSequences[-1]
    $selected = @($geometryMatches | Where-Object { [int]$_.Groups['seq'].Value -eq $sequence })
    $fields = @{}
    foreach ($record in $selected) {
        foreach ($part in $record.Groups['fields'].Value.Split(' ')) {
            $pair = $part.Split('=', 2)
            if ($pair.Count -eq 2) { $fields[$pair[0]] = $pair[1] }
        }
    }
    $geometry = [ordered]@{ state = $selected[0].Groups['state'].Value; sequence = $sequence }
    foreach ($name in @("view", "bar", "density", "speed", "showStatus", "standardWheel", "naturalWheel", "showTips", "summaryMode", "detailMode", "statusCombo", "inputEnabled", "advancedToggle", "apply", "defaults", "options", "scrollLines")) {
        if (-not $fields.ContainsKey($name)) { throw "C144 geometry omitted $name." }
        $xy = $fields[$name].Split(',')
        if ($xy.Count -ne 2 -or $xy[0] -notmatch '^-?\d+$' -or
            $xy[1] -notmatch '^-?\d+$') { throw "C144 geometry field $name was malformed." }
        $geometry[$name] = [pscustomobject]@{ x = [int]$xy[0]; y = [int]$xy[1] }
    }
    foreach ($name in @("offset", "extent", "viewport", "thumbTop", "thumbHeight", "trackTop")) {
        if (-not $fields.ContainsKey($name)) { throw "C144 geometry omitted $name." }
        $geometry[$name] = [int]$fields[$name]
    }
    return [pscustomobject]$geometry
}

function Add-C144Click([System.Collections.Generic.List[object]]$Commands,
                       [string]$Target, [string]$Marker, [string]$Phase) {
    $Commands.Add([pscustomobject]@{ action = "move"; target = $Target; marker = ""; phase = "$Phase-move" })
    $Commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $true; marker = $Marker; phase = "$Phase-down" })
    $Commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $false; marker = ""; phase = "$Phase-up" })
}

function Add-C144Move([System.Collections.Generic.List[object]]$Commands,
                      [string]$Target, [string]$Phase) {
    $Commands.Add([pscustomobject]@{ action = "move"; target = $Target; marker = ""; phase = $Phase })
}

function Add-C144Key([System.Collections.Generic.List[object]]$Commands,
                     [string]$Code, [bool]$Down, [string]$Marker, [string]$Phase) {
    $Commands.Add([pscustomobject]@{ action = "key"; code = $Code; down = $Down; marker = $Marker; phase = $Phase })
}

function Add-C144Wheel([System.Collections.Generic.List[object]]$Commands,
                        [int]$Count, [string]$Marker, [string]$Phase,
                        [int]$Direction = -1) {
    $events = [System.Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $Count; $index++) { $events.Add((New-C137Wheel $Direction)) }
    $Commands.Add([pscustomobject]@{ action = "events"; events = $events.ToArray(); marker = $Marker; phase = $Phase })
}

function Get-C145DialogPoint([string]$Serial, [string]$Kind, [string]$Field) {
    $records = [regex]::Matches($Serial,
        '(?m)^\[C102-MANAGED-OUTPUT\] C145-DIALOG-GEOMETRY kind=(?<kind>\w+) bounds=[^\r\n]+')
    $record = $null
    foreach ($candidate in $records) {
        if ($candidate.Groups['kind'].Value -eq $Kind) { $record = $candidate }
    }
    if ($null -eq $record) { throw "C145 $Kind dialog geometry was not reported." }
    $point = [regex]::Match($record.Value, "\b$Field=(?<x>\d+),(?<y>\d+)")
    if (-not $point.Success) { throw "C145 $Kind dialog geometry omitted $Field." }
    return [pscustomobject]@{ x = [int]$point.Groups['x'].Value; y = [int]$point.Groups['y'].Value }
}

function Invoke-C142Boot([string]$Esp, [string]$Serial, [string]$Stdout,
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
                    '(?m)^\[C142-PROOF\].*transport=physical-qemu result=PASS') {
                $proofStarted = $true
            }
            if ($proofStarted -and -not $targetReady -and $partial -match
                    '(?m)^\[C142-TARGET\].*result=PASS') {
                $targetReady = $true
            }
            if ($proofStarted -and $targetReady -and $null -eq $commandList) {
                $viewX = Get-C142HexField $partial "viewX"
                $viewY = Get-C142HexField $partial "viewY"
                $barX = Get-C142HexField $partial "scrollBarX"
                $barY = Get-C142HexField $partial "scrollBarY"
                $comboX = $viewX + 80
                $comboY = $viewY + 75
                $checkX = $viewX + 80
                $checkY = $viewY + 41
                $barTargetX = $barX + 8
                $barTargetY = $barY + 20
                $buttonX = $viewX + 180
                $buttonY = $viewY + 90
                $tabEvents = [System.Collections.Generic.List[object]]::new()
                for ($tab = 0; $tab -lt 5; $tab++) {
                    $tabEvents.Add((New-C136Key 'tab' $true))
                    $tabEvents.Add((New-C136Key 'tab' $false))
                }
                $commandList = @(
                    [pscustomobject]@{ events = (New-C136RelativeMove 1 1); marker = ''; phase = 'c142-calibration' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C142-POPUP open=PASS arranged-geometry=PASS after-relayout=PASS capture=none result=PASS'; phase = 'combo-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'combo-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 18); marker = ''; phase = 'popup-row-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C142-POPUP follow-up=PASS capture=none result=PASS'; phase = 'popup-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'popup-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($checkX - $comboX) ($checkY - ($comboY + 18))); marker = ''; phase = 'checkbox-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C142-VISIBILITY checkbox=PASS progress=hidden margins=removed button=moved content=shrunk relayout=PASS result=PASS'; phase = 'checkbox-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'checkbox-up' },
                    [pscustomobject]@{ events = @(New-C137Wheel -1); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C142-WHEEL viewport=changed stack-translated=PASS result=PASS'; phase = 'wheel-down' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($barTargetX - $checkX) ($barTargetY - $checkY)); marker = ''; phase = 'scrollbar-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C142-DRAG press=PASS owner=scrollbar result=PASS'; phase = 'scrollbar-down' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 70); marker = ''; phase = 'scrollbar-drag' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C142-DRAG release=PASS owner=none result=PASS'; phase = 'scrollbar-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($buttonX - $barTargetX) ($buttonY - ($barTargetY + 70))); marker = ''; phase = 'moved-button-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C142-POINTER button=right-aligned hit=PASS translated=PASS result=PASS'; phase = 'moved-button-down' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'moved-button-up' },
                    [pscustomobject]@{ events = $tabEvents.ToArray(); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C142-FOCUS tab=revealed margins=accounted result=PASS'; phase = 'tab-sequence' })
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

function Invoke-C143Boot([string]$Esp, [string]$Serial, [string]$Stdout,
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
                    '(?m)^\[C143-PROOF\] managed-proof-started context=c143-native transport=physical-qemu result=PASS') {
                $proofStarted = $true
            }
            if ($proofStarted -and -not $targetReady -and $partial -match
                    '(?m)^\[C143-TARGET\].*result=PASS') {
                $targetReady = $true
            }
            if ($proofStarted -and $targetReady -and $null -eq $commandList) {
                $viewX = Get-C143HexField $partial "viewX"
                $viewY = Get-C143HexField $partial "viewY"
                $barX = Get-C143HexField $partial "scrollBarX"
                $barY = Get-C143HexField $partial "scrollBarY"
                $comboX = Get-C143HexField $partial "comboX"
                $comboY = Get-C143HexField $partial "comboY"
                $checkX = Get-C143HexField $partial "checkboxX"
                $checkY = Get-C143HexField $partial "checkboxY"
                $radioOneX = Get-C143HexField $partial "radioOneX"
                $radioOneY = Get-C143HexField $partial "radioOneY"
                $radioTwoY = Get-C143HexField $partial "radioTwoY"
                $buttonX = Get-C143HexField $partial "buttonX"
                $buttonY = Get-C143HexField $partial "buttonY"
                $visibleToggleX = Get-C143HexField $partial "visibleToggleX"
                $visibleToggleY = Get-C143HexField $partial "visibleToggleY"
                $enabledToggleY = Get-C143HexField $partial "enabledToggleY"
                $wheelEvents = [System.Collections.Generic.List[object]]::new()
                for ($wheel = 0; $wheel -lt 26; $wheel++) {
                    $wheelEvents.Add((New-C137Wheel -1))
                }
                $tabEvents = [System.Collections.Generic.List[object]]::new()
                for ($tab = 0; $tab -lt 8; $tab++) {
                    $tabEvents.Add((New-C136Key 'tab' $true))
                    $tabEvents.Add((New-C136Key 'tab' $false))
                }
                $tabEvents.Add((New-C136Key 'shift' $true))
                $tabEvents.Add((New-C136Key 'tab' $true))
                $tabEvents.Add((New-C136Key 'tab' $false))
                $tabEvents.Add((New-C136Key 'shift' $false))
                $commandList = @(
                    [pscustomobject]@{ events = (New-C136RelativeMove 1 1); marker = ''; phase = 'c143-calibration' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-COMBO open=PASS arranged=PASS capture=combo result=PASS'; phase = 'combo-open' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'combo-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 27); marker = ''; phase = 'combo-row-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-COMBO commit=PASS translated-origin=PASS result=PASS'; phase = 'combo-commit' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'combo-commit-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 -27); marker = ''; phase = 'combo-reopen-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-COMBO open=PASS arranged=PASS capture=combo result=PASS'; phase = 'combo-reopen' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'combo-reopen-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove (($visibleToggleX + 8) - $comboX) (($visibleToggleY + 9) - $comboY)); marker = ''; phase = 'group-hide-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-CAPTURE invalidated=cancelled owner=none result=PASS'; phase = 'group-hide-cancel-combo' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'group-hide-release' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-GROUP visible=true effective-members=restored result=PASS'; phase = 'group-show' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'group-show-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove ($comboX - ($visibleToggleX + 8)) ($comboY - ($visibleToggleY + 9))); marker = ''; phase = 'combo-disable-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-COMBO open=PASS arranged=PASS capture=combo result=PASS'; phase = 'combo-open-for-disable' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'combo-disable-open-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove (($visibleToggleX + 8) - $comboX) (($enabledToggleY + 9) - $comboY)); marker = ''; phase = 'group-disable-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-CAPTURE invalidated=cancelled owner=none result=PASS'; phase = 'group-disable-cancel-combo' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'group-disable-release' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-GROUP enabled=true own-state-preserved result=PASS'; phase = 'group-enable' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'group-enable-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove (($checkX + 8) - ($visibleToggleX + 8)) (($checkY + 9) - ($enabledToggleY + 9))); marker = ''; phase = 'progress-toggle-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-DYNAMIC hide=PASS margins=removed button=moved frame-stable=PASS result=PASS'; phase = 'progress-hide' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'progress-toggle-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove (($viewX + 150) - ($checkX + 8)) (($viewY + 60) - ($checkY + 9))); marker = ''; phase = 'wheel-target-move' },
                    [pscustomobject]@{ events = $wheelEvents.ToArray(); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-WHEEL viewport=changed frame-and-members-translated=PASS result=PASS'; phase = 'wheel-scroll' },
                    [pscustomobject]@{ events = (New-C136RelativeMove (($radioOneX + 8) - ($viewX + 150)) (($radioTwoY - 78 + 9) - ($viewY + 60))); marker = ''; phase = 'radio-two-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-RADIO explicit-group=PASS selected=two result=PASS'; phase = 'radio-two' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'radio-two-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 ($radioOneY - $radioTwoY)); marker = ''; phase = 'radio-one-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-RADIO explicit-group=PASS selected=one result=PASS'; phase = 'radio-one' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'radio-one-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove (($barX + 8) - ($radioOneX + 8)) (($barY + 39 + 1) - ($radioOneY - 78 + 9))); marker = ''; phase = 'scrollbar-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-DRAG press=PASS owner=scrollbar result=PASS'; phase = 'scrollbar-down' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 70); marker = ''; phase = 'scrollbar-drag' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-DRAG release=PASS owner=none result=PASS'; phase = 'scrollbar-up' },
                    [pscustomobject]@{ events = (New-C136RelativeMove (($buttonX + 64) - ($barX + 8)) (($buttonY - 25 + 9 - 162) - ($barY + 39 + 71))); marker = ''; phase = 'translated-button-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-MENU open=PASS button-invoked=PASS capture=menu result=PASS'; phase = 'menu-open' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'menu-open-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove -54 1); marker = ''; phase = 'menu-first-item-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-MENU commit=PASS button-invoked=PASS result=PASS'; phase = 'menu-commit' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'menu-commit-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 54 -1); marker = ''; phase = 'menu-reopen-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-MENU open=PASS button-invoked=PASS capture=menu result=PASS'; phase = 'menu-reopen' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'menu-reopen-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove (($visibleToggleX + 8) - ($buttonX + 64)) (($enabledToggleY + 9) - ($buttonY - 25 + 9 - 162))); marker = ''; phase = 'group-disable-menu-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-CAPTURE invalidated=cancelled owner=none result=PASS'; phase = 'group-disable-cancel-menu' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'group-disable-menu-release' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-GROUP enabled=true own-state-preserved result=PASS'; phase = 'group-enable-after-menu' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = ''; phase = 'group-enable-menu-release' },
                    [pscustomobject]@{ events = (New-C136RelativeMove (($barX + 8) - ($visibleToggleX + 8)) (($barY + 69) - ($enabledToggleY + 9))); marker = ''; phase = 'scrollbar-top-move' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $true); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-DRAG press=PASS owner=scrollbar result=PASS'; phase = 'scrollbar-top-down' },
                    [pscustomobject]@{ events = (New-C136RelativeMove 0 -70); marker = ''; phase = 'scrollbar-top-drag' },
                    [pscustomobject]@{ events = @(New-C136Button 'left' $false); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-DRAG release=PASS owner=none result=PASS'; phase = 'scrollbar-top-up' },
                    [pscustomobject]@{ events = $tabEvents.ToArray(); marker = '(?m)^\[C102-MANAGED-OUTPUT\] C143-FOCUS shift-tab=handled earlier-member=PASS result=PASS'; phase = 'tab-shift-tab' })
                $calibrating = $true
            }
            if ($calibrating -and $commandIndex -ge 1) {
                $start = [Math]::Min($previousSerialLength, $partial.Length)
                $newSerial = $partial.Substring($start)
                if ($newSerial -match '(?m)^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS') {
                    $pointer = Get-C138NativePointer $partial
                    $commandList[0].events = New-C136RelativeMove (
                        $comboX - $pointer.x) ($comboY - $pointer.y)
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
                    if ($command.phase -eq 'translated-button-move') {
                        $offset = Get-C143CurrentOffset $partial
                        $buttonScreenY = $buttonY - 25 + 9 - $offset
                        $command.events = New-C136RelativeMove (
                            ($buttonX + 64) - ($barX + 8)) (
                            $buttonScreenY - ($barY + 39 + 71))
                    } elseif ($command.phase -eq 'group-disable-menu-move') {
                        $command.events = New-C136RelativeMove (
                            ($visibleToggleX + 8) - ($buttonX + 64)) (
                            ($enabledToggleY + 9) - $buttonScreenY)
                    }
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

function Invoke-C144Boot([string]$Esp, [string]$Serial, [string]$Stdout,
                          [string]$Stderr, [string]$MonitorLog, [int]$MonitorPort,
                          [string]$Qemu, [string]$Ovmf,
                          [string]$C146BootRole = "") {
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
    $cursorX = 0
    $cursorY = 0
    $timedOut = $false
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 150
            $partial = if (Test-Path -LiteralPath $Serial) {
                [string](Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue)
            } else { [string]::Empty }
            if ($isC148 -and $script:C148NotesOnlyBoot -and -not $proofStarted -and $partial -match
                    '(?m)^\[C102-MANAGED-OUTPUT\] C148-NOTES-FIRST control=ListBox') {
                $proofStarted = $true
            } elseif ($isC145 -and -not $isC147 -and -not $proofStarted -and $partial -match
                    '(?m)^\[C145-PROOF\] managed-proof-started context=c145-native transport=physical-qemu result=PASS') {
                $proofStarted = $true
            } elseif (-not $proofStarted -and $partial -match
                    '(?m)^\[C144-PROOF\] managed-proof-started context=c144-native transport=physical-qemu result=PASS') {
                $proofStarted = $true
            }
            if ($isC148 -and $script:C148NotesOnlyBoot -and $proofStarted -and -not $targetReady -and $partial -match
                    '(?m)^\[C148-NOTES-ONLY\] settings-center=not-launched result=PASS') {
                $targetReady = $true
            } elseif ($isC145 -and -not $isC147 -and $proofStarted -and -not $targetReady -and $partial -match
                    '(?m)^\[C145-TARGET\].*result=PASS') {
                $targetReady = $true
            } elseif ($proofStarted -and -not $targetReady -and $partial -match
                    '(?m)^\[C144-TARGET\].*result=PASS') {
                $targetReady = $true
            }
            if ($proofStarted -and $targetReady -and $null -eq $commandList) {
                if ($isC146) {
                    $commands = [System.Collections.Generic.List[object]]::new()
                    $commands.Add([pscustomobject]@{ action = "calibrate"; marker = ""; phase = "c146-calibration" })
                    if ($isC148 -and $script:C148NotesOnlyBoot) {
                        $commands.Add([pscustomobject]@{ action = "done"; marker = ""; phase = "c148-notes-only-complete" })
                    } elseif ($isC147 -and $C146BootRole -in @('corrupt', 'future-version')) {
                        $commands.Add([pscustomobject]@{ action = "done"; marker = ""; phase = "c147-startup-only" })
                    } else {
                    if ($isC148 -and $script:C148StartingFileHash -and
                        (Get-Hash (Join-Path $Esp 'GXSETT.BIN')) -ne $script:C148StartingFileHash) {
                        throw "C148 settings load or Settings Center open rewrote the seeded file before explicit Apply ($C146BootRole)."
                    }
                    switch -Exact ($C146BootRole) {
                        "write" {
                            if ($isC148) {
                                if ($partial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-LOAD-META path=/system/apps/GXSETT\.BIN v=1 size=25 migrate=memory unchanged=yes viewport=0 focus=normal result=PASS' -or
                                    $partial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-SETTINGS-SYNC naturalScroll=1 scrollLines=3 dirty=false runtime=agrees result=PASS') {
                                    throw 'C148 authentic v1 did not hydrate the legacy snapshot plus the default scroll amount.'
                                }
                                Add-C144Move $commands "wheelPoint" "c148-reveal-scroll-amount-move"
                                for ($notch = 0; $notch -lt 60; $notch++) {
                                    $revealMarker = if ($notch -eq 59) {
                                        '(?m)^\[C102-MANAGED-OUTPUT\] C144-GEOMETRY state=wheel seq=\d+ part=6 .*\boffset=[1-9]\d*'
                                    } else { "" }
                                    $commands.Add([pscustomobject]@{
                                        action = "events"; events = @(New-C137Wheel 1)
                                        marker = $revealMarker; phase = "c148-reveal-scroll-amount-$notch"
                                    })
                                }
                                Add-C144Click $commands "scrollLines" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO open=PASS capture=combo result=PASS' "c148-scroll-lines-open"
                                foreach ($step in 1..2) {
                                    Add-C144Key $commands "down" $true "" "c148-scroll-lines-down-$step"
                                    Add-C144Key $commands "down" $false "" "c148-scroll-lines-down-up-$step"
                                }
                                Add-C144Key $commands "ret" $true '(?m)^\[C102-MANAGED-OUTPUT\] C148-WORKING scrollLines=5 applied=3 runtime=3 persisted=3 dirty=true result=PASS' "c148-scroll-lines-commit"
                                Add-C144Key $commands "ret" $false "" "c148-scroll-lines-commit-up"
                                if ($isC149) {
                                    Add-C144Click $commands "showTips" '(?m)^\[C102-MANAGED-OUTPUT\] C149-WORKING keyboardTips=0 runtime=preserved persisted=preserved dirty=true result=PASS' "c149-keyboard-tips-edit"
                                }
                                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c148-apply-menu-open"
                                $applyMarker = if ($isC149) {
                                    '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-APPLY oldKeyboardTips=1 newKeyboardTips=0 persistence=verified runtime=committed result=PASS'
                                } else {
                                    '(?m)^\[C102-MANAGED-OUTPUT\] C148-RUNTIME-APPLY oldLines=3 newLines=5 persistence=verified runtime=committed result=PASS'
                                }
                                Add-C144Click $commands "menuApply" $applyMarker "c148-apply-write"
                                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c148-clean-close-menu-open"
                                Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C144-CLOSE popup=closed capture=none registration=bounded result=PASS' "c148-clean-close"
                            } else {
                            if (-not (Test-C146SnapshotOutput $partial `
                                'C146-LOAD source=missing result=PASS working=applied persisted=defaults dirty=false' `
                                'C146-VALUES density=0 showStatus=1 advanced=0 inputEnabled=1 naturalScroll=0 speed=1 keyboardTips=1 detail=0 reportFormat=0')) {
                                throw "C146 Boot A did not start from the verified missing-file defaults state."
                            }
                            Add-C144Click $commands "density" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO open=PASS capture=combo result=PASS' "c146-density-open"
                            Add-C144Click $commands "densityRow2" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO commit=PASS translated-origin=PASS result=PASS' "c146-density-commit"
                            Add-C144Click $commands "showStatus" '(?m)^\[C102-MANAGED-OUTPUT\] C144-SYSTEM visible=false extent=updated result=PASS' "c146-checkbox-edit"
                            Add-C144Move $commands "scrollbarPageBottom" "c146-radio-scroll-move"
                            $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $true; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-SCROLL page=PASS offset=changed result=PASS'; phase = "c146-radio-scroll-down" })
                            $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $false; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-GEOMETRY state=pointer-up seq=\d+ part=6 .*\boffset=[1-9]\d+.*$'; phase = "c146-radio-scroll-up-layout" })
                            Add-C144Click $commands "naturalWheel" '(?m)^\[C102-MANAGED-OUTPUT\] C144-RADIO wheel-group=independent result=PASS' "c146-radio-edit"
                            Add-C144Click $commands "advancedToggle" '(?m)^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=true extent=grown scrollbar=synced result=PASS' "c146-advanced-edit"
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c146-apply-menu-open"
                            Add-C144Click $commands "menuApply" (Get-C146SnapshotOutputPattern `
                                'C146-SAVE result=PASS write=verified readback=PASS dirty=false' `
                                'C146-VALUES density=1 showStatus=0 advanced=1 inputEnabled=1 naturalScroll=1 speed=1 keyboardTips=1 detail=0 reportFormat=0') "c146-apply-write"
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c146-options-close"
                            Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C144-CLOSE popup=closed capture=none registration=bounded result=PASS' "c146-clean-close"
                            }
                        }
                        "discard" {
                            if ($isC150) {
                                if ($partial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-SETTINGS-SYNC naturalScroll=1 scrollLines=7 dirty=false runtime=agrees result=PASS' -or
                                    $partial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-SETTINGS-SYNC keyboardTips=0 dirty=false runtime=agrees result=PASS') {
                                    throw 'C150 Discard did not start with the hydrated false keyboard-tip snapshot.'
                                }
                                Add-C144Move $commands "wheelPoint" "c150-discard-reveal-tips-move"
                                Add-C144Wheel $commands 24 '(?m)^\[C102-MANAGED-OUTPUT\] C144-GEOMETRY state=wheel seq=\d+ part=6 .*\boffset=[1-9]\d*' "c150-discard-reveal-tips" 1
                                Add-C144Click $commands "showTips" '(?m)^\[C102-MANAGED-OUTPUT\] C149-WORKING keyboardTips=1 runtime=preserved persisted=preserved dirty=true result=PASS' "c150-discard-edit-tips"
                                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c150-discard-menu"
                                Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS' "c150-discard-close-prompt"
                                Add-C144Click $commands "dirtyDiscard" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED result=Discard applied=preserved closed=true result=PASS' "c150-discard-return"
                            } else {
                            if (-not (Test-C146SnapshotOutput $partial `
                                'C146-LOAD source=file result=PASS working=applied persisted=loaded dirty=false' `
                                'C146-VALUES density=1 showStatus=0 advanced=1 inputEnabled=1 naturalScroll=1 speed=1 keyboardTips=1 detail=0 reportFormat=0')) {
                                throw "C146 discard boot did not hydrate the exact Boot A snapshot."
                            }
                            Add-C144Click $commands "showStatus" '(?m)^\[C102-MANAGED-OUTPUT\] C144-DIRTY working=changed applied=preserved result=PASS' "c146-discard-edit"
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c146-discard-menu"
                            Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS' "c146-discard-close-prompt"
                            Add-C144Click $commands "dirtyDiscard" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED result=Discard applied=preserved closed=true result=PASS' "c146-discard"
                            }
                        }
                        "cancel-retry" {
                            if (-not $isC150 -or
                                $partial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-SETTINGS-SYNC naturalScroll=1 scrollLines=5 dirty=false runtime=agrees result=PASS' -or
                                $partial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-SETTINGS-SYNC keyboardTips=0 dirty=false runtime=agrees result=PASS') {
                                throw 'C150 Cancel/retry did not start with the false keyboard-tip runtime snapshot.'
                            }
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c150-reset-menu-open"
                            Add-C144Click $commands "menuDefaults" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS' "c150-reset-dialog-open"
                            Add-C144Click $commands "resetCancel" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Cancel working=preserved viewport=preserved focus=restored result=PASS' "c150-reset-cancel-target-retained"
                            Add-C144Move $commands "wheelPoint" "c150-cancel-retry-reveal-tips-move"
                            Add-C144Wheel $commands 33 '(?m)^\[C102-MANAGED-OUTPUT\] C144-GEOMETRY state=wheel seq=\d+ part=6 .*\boffset=[1-9]\d*' "c150-cancel-retry-reveal-tips" 1
                            Add-C144Click $commands "showTips" '(?m)^\[C102-MANAGED-OUTPUT\] C149-WORKING keyboardTips=1 runtime=preserved persisted=preserved dirty=true result=PASS' "c150-cancel-retry-edit-tips"
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c150-cancel-menu"
                            Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS' "c150-cancel-close-prompt"
                            Add-C144Click $commands "dirtyCancel" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED result=Cancel dirty=preserved parent=open focus=restored result=PASS' "c150-cancel-retains-settings"
                            Add-C144Key $commands "shift" $true "" "c150-failure-shortcut-shift-down"
                            Add-C144Key $commands "f" $true '(?m)^\[C150-MANAGED-OUTPUT\] C150-FAILURE-INJECT armed=true result=PASS' "c150-failure-shortcut"
                            Add-C144Key $commands "f" $false "" "c150-failure-shortcut-key-up"
                            Add-C144Key $commands "shift" $false "" "c150-failure-shortcut-shift-up"
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c150-failed-apply-menu"
                            Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS' "c150-failed-apply-close-prompt"
                            Add-C144Click $commands "dirtyApply" '(?m)^\[C102-MANAGED-OUTPUT\] C146-UNSAVED result=Apply failed=kept-open dirty=true result=PASS' "c150-apply-failure-keeps-settings"
                            Add-C144Click $commands "persistenceOk" '(?m)^\[C150-MANAGED-OUTPUT\] C150-ERROR-DISMISSED settings=active retry=available result=PASS' "c150-apply-error-dismiss"
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c150-retry-menu"
                            Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS' "c150-retry-close-prompt"
                            Add-C144Click $commands "dirtyApply" '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-APPLY oldKeyboardTips=0 newKeyboardTips=1 persistence=verified runtime=committed result=PASS' "c150-retry-apply-return"
                        }
                        "cancel" {
                            if (-not (Test-C146SnapshotOutput $partial `
                                'C146-LOAD source=file result=PASS working=applied persisted=loaded dirty=false' `
                                'C146-VALUES density=1 showStatus=0 advanced=1 inputEnabled=1 naturalScroll=1 speed=1 keyboardTips=1 detail=0 reportFormat=0')) {
                                throw "C147 Cancel boot did not hydrate the previously applied snapshot."
                            }
                            Add-C144Click $commands "showStatus" '(?m)^\[C102-MANAGED-OUTPUT\] C144-DIRTY working=changed applied=preserved result=PASS' "c147-cancel-edit"
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c147-cancel-menu"
                            Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS' "c147-cancel-prompt"
                            Add-C144Click $commands "dirtyCancel" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED result=Cancel dirty=preserved parent=open focus=restored result=PASS' "c147-cancel-confirm"
                        }
                        "verify" {
                            if ($isC148) {
                                $syncMatch = [regex]::Match($partial, '(?m)^\[C102-MANAGED-OUTPUT\] C148-SETTINGS-SYNC naturalScroll=([01]) scrollLines=([1-8]) dirty=false runtime=agrees result=PASS')
                                if (-not $syncMatch.Success) { throw 'C148 verification Settings Center did not report hydrated v2 values.' }
                                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c148-verify-clean-close-menu"
                                Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C144-CLOSE popup=closed capture=none registration=bounded result=PASS' "c148-verify-clean-close"
                            } else {
                            if (-not (Test-C146SnapshotOutput $partial `
                                'C146-LOAD source=file result=PASS working=applied persisted=loaded dirty=false' `
                                'C146-VALUES density=1 showStatus=0 advanced=1 inputEnabled=1 naturalScroll=1 speed=1 keyboardTips=1 detail=0 reportFormat=0')) {
                                throw "C146 post-discard boot did not retain the previous persisted snapshot."
                            }
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c146-verify-clean-close-menu"
                            Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C144-CLOSE popup=closed capture=none registration=bounded result=PASS' "c146-verify-clean-close"
                            }
                        }
                        "reset" {
                            if ($isC148) {
                                if ($partial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-SETTINGS-SYNC naturalScroll=1 scrollLines=5 dirty=false runtime=agrees result=PASS') {
                                    throw 'C148 Reset sequence did not start from its persisted custom v2 state.'
                                }
                                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c148-reset-menu"
                                Add-C144Click $commands "menuDefaults" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS' "c148-reset-open"
                                $resetMarker = if ($isC149) {
                                    '(?m)^\[C102-MANAGED-OUTPUT\] C149-RESET keyboardTips=working-default runtime=preserved persisted=preserved result=PASS'
                                } else {
                                    '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults dirty=updated viewport=valid focus=restored result=PASS'
                                }
                                Add-C144Click $commands "resetConfirm" $resetMarker "c148-reset-confirm"
                                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c148-reset-apply-menu-open"
                                $resetApplyMarker = if ($isC149) {
                                    '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-APPLY oldKeyboardTips=0 newKeyboardTips=1 persistence=verified runtime=committed result=PASS'
                                } else {
                                    '(?m)^\[C102-MANAGED-OUTPUT\] C148-RUNTIME-APPLY oldLines=5 newLines=3 persistence=verified runtime=committed result=PASS'
                                }
                                Add-C144Click $commands "menuApply" $resetApplyMarker "c148-reset-apply"
                                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c148-reset-close-menu-open"
                                Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C144-CLOSE popup=closed capture=none registration=bounded result=PASS' "c148-reset-clean-close"
                            } else {
                            if (-not (Test-C146SnapshotOutput $partial `
                                'C146-LOAD source=file result=PASS working=applied persisted=loaded dirty=false' `
                                'C146-VALUES density=1 showStatus=0 advanced=1 inputEnabled=1 naturalScroll=1 speed=1 keyboardTips=1 detail=0 reportFormat=0')) {
                                throw "C146 Reset boot did not hydrate the expected applied snapshot."
                            }
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c146-reset-menu"
                            Add-C144Click $commands "menuDefaults" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS' "c146-reset-open"
                            Add-C144Click $commands "resetConfirm" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults dirty=updated viewport=valid focus=restored result=PASS' "c146-reset-confirm"
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c146-reset-apply-menu-open"
                            Add-C144Click $commands "menuApply" (Get-C146SnapshotOutputPattern `
                                'C146-SAVE result=PASS write=verified readback=PASS dirty=false' `
                                'C146-VALUES density=0 showStatus=1 advanced=0 inputEnabled=1 naturalScroll=0 speed=1 keyboardTips=1 detail=0 reportFormat=0') "c146-reset-apply"
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c146-reset-clean-close-menu"
                            Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C144-CLOSE popup=closed capture=none registration=bounded result=PASS' "c146-reset-clean-close"
                            }
                        }
                        "defaults" {
                            if (-not (Test-C146SnapshotOutput $partial `
                                'C146-LOAD source=file result=PASS working=applied persisted=loaded dirty=false' `
                                'C146-VALUES density=0 showStatus=1 advanced=0 inputEnabled=1 naturalScroll=0 speed=1 keyboardTips=1 detail=0 reportFormat=0')) {
                                throw "C146 Defaults reboot did not load the confirmed persisted defaults."
                            }
                            Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "c146-defaults-clean-close-menu"
                            Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C144-CLOSE popup=closed capture=none registration=bounded result=PASS' "c146-defaults-clean-close"
                        }
                        "corrupt" { }
                        "future-version" { }
                        default { throw "Unknown C146 persistence boot role '$C146BootRole'." }
                    }
                    }
                    $commands.Add([pscustomobject]@{ action = "done"; marker = ""; phase = "complete" })
                    $commandList = $commands.ToArray()
                    $calibrating = $true
                    Send-C136QmpEvents $MonitorPort (New-C136RelativeMove 1 1) $MonitorLog
                    $previousSerialLength = $partial.Length
                    $commandIndex = 1
                } elseif ($isC145) {
                    $geometry = Get-C144Geometry $partial "initial"
                    $commands = [System.Collections.Generic.List[object]]::new()
                    $commands.Add([pscustomobject]@{ action = "calibrate"; marker = ""; phase = "c145-calibration" })
                    Add-C144Click $commands "showStatus" '(?m)^\[C102-MANAGED-OUTPUT\] C144-DIRTY working=changed applied=preserved result=PASS' "dirty-before-reset"
                    Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "options-reset-escape"
                    Add-C144Click $commands "menuDefaults" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS' "reset-open-escape"
                    Add-C144Key $commands "tab" $true '(?m)^\[C102-MANAGED-OUTPUT\] C145-FOCUS tab=contained result=PASS' "dialog-tab"
                    Add-C144Key $commands "tab" $false "" "dialog-tab-up"
                    Add-C144Key $commands "shift" $true "" "dialog-shift-down"
                    Add-C144Key $commands "tab" $true '(?m)^\[C102-MANAGED-OUTPUT\] C145-FOCUS shift-tab=contained result=PASS' "dialog-shift-tab"
                    Add-C144Key $commands "tab" $false "" "dialog-shift-tab-up"
                    Add-C144Key $commands "shift" $false "" "dialog-shift-up"
                    Add-C144Key $commands "esc" $true '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Cancel working=preserved viewport=preserved focus=restored result=PASS' "reset-escape"
                    Add-C144Key $commands "esc" $false "" "reset-escape-up"
                    Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "options-reset-pointer-cancel"
                    Add-C144Click $commands "menuDefaults" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS' "reset-open-pointer-cancel"
                    Add-C144Click $commands "outsideOptions" '(?m)^\[C102-MANAGED-OUTPUT\] C145-MODAL outside=consumed parent=blocked result=PASS' "reset-outside-click"
                    Add-C144Click $commands "resetCancel" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Cancel working=preserved viewport=preserved focus=restored result=PASS' "reset-pointer-cancel"
                    Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "options-reset-enter"
                    Add-C144Click $commands "menuDefaults" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS' "reset-open-enter"
                    Add-C144Key $commands "ret" $true '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults dirty=updated viewport=valid focus=restored result=PASS' "reset-enter-default"
                    Add-C144Key $commands "ret" $false "" "reset-enter-up"
                    Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "options-reset-pointer"
                    Add-C144Click $commands "menuDefaults" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS' "reset-open-pointer"
                    Add-C144Click $commands "resetConfirm" '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults dirty=updated viewport=valid focus=restored result=PASS' "reset-pointer-confirm"
                    Add-C144Click $commands "showStatus" '(?m)^\[C102-MANAGED-OUTPUT\] C144-DIRTY working=changed applied=preserved result=PASS' "dirty-before-close"
                    Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "options-close-cancel"
                    Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS' "close-open-cancel"
                    Add-C144Move $commands "wheelPoint" "modal-wheel-point"
                    Add-C144Wheel $commands 1 '(?m)^\[C102-MANAGED-OUTPUT\] C145-MODAL wheel=parent-blocked viewport=preserved result=PASS' "modal-wheel"
                    Add-C144Click $commands "outsideOptions" '(?m)^\[C102-MANAGED-OUTPUT\] C145-MODAL outside=consumed parent=blocked result=PASS' "close-outside-click"
                    Add-C144Click $commands "dirtyCancel" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED result=Cancel dirty=preserved parent=open focus=restored result=PASS' "close-cancel"
                    Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "options-close-apply"
                    Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS' "close-open-apply"
                    Add-C144Click $commands "dirtyApply" '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED result=Apply applied=committed closed=true result=PASS' "close-apply"
                    $commands.Add([pscustomobject]@{ action = "done"; marker = ""; phase = "complete" })
                    $commandList = $commands.ToArray()
                    $calibrating = $true
                    Send-C136QmpEvents $MonitorPort (New-C136RelativeMove 1 1) $MonitorLog
                    $previousSerialLength = $partial.Length
                    $commandIndex = 1
                } else {
                $geometry = Get-C144Geometry $partial
                $commands = [System.Collections.Generic.List[object]]::new()
                $commands.Add([pscustomobject]@{ action = "calibrate"; marker = ""; phase = "c144-calibration" })
                Add-C144Click $commands "density" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO open=PASS capture=combo result=PASS' "density-open"
                Add-C144Click $commands "densityRow2" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO commit=PASS translated-origin=PASS result=PASS' "density-commit"
                Add-C144Click $commands "showStatus" '(?m)^\[C102-MANAGED-OUTPUT\] C144-SYSTEM visible=false extent=updated result=PASS' "status-hide"
                Add-C144Click $commands "showStatus" '(?m)^\[C102-MANAGED-OUTPUT\] C144-SYSTEM visible=true extent=updated result=PASS' "status-show"
                Add-C144Move $commands "wheelPoint" "wheel-target"
                Add-C144Wheel $commands 68 '(?m)^\[C102-MANAGED-OUTPUT\] C144-WHEEL viewport=changed sections-translated=PASS result=PASS' "wheel-sections"
                Add-C144Click $commands "speed" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO open=PASS capture=combo result=PASS' "speed-open"
                Add-C144Click $commands "speedRow3" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO commit=PASS translated-origin=PASS result=PASS' "speed-commit"
                Add-C144Click $commands "speed" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO open=PASS capture=combo result=PASS' "speed-reopen"
                Add-C144Click $commands "inputEnabled" '(?m)^\[C102-MANAGED-OUTPUT\] C144-CAPTURE group-disabled=cancelled owner=none result=PASS' "group-disable-cancel-popup"
                Add-C144Click $commands "inputEnabled" '(?m)^\[C102-MANAGED-OUTPUT\] C144-GROUP input=enabled own-member-state=preserved result=PASS' "group-enable"
                Add-C144Click $commands "naturalWheel" '(?m)^\[C102-MANAGED-OUTPUT\] C144-RADIO wheel-group=independent result=PASS' "natural-scroll"
                Add-C144Move $commands "scrollbarThumb" "scrollbar-thumb-move"
                $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $true; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-DRAG press=PASS owner=scrollbar result=PASS'; phase = "scrollbar-down" })
                Add-C144Move $commands "scrollbarBottom" "scrollbar-bottom-drag"
                $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $false; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-DRAG release=PASS owner=none result=PASS'; phase = "scrollbar-up" })
                Add-C144Click $commands "scrollbarPageTop" '(?m)^\[C102-MANAGED-OUTPUT\] C144-SCROLL page=PASS offset=changed result=PASS' "scrollbar-page-up"
                Add-C144Click $commands "scrollbarPageBottom" '(?m)^\[C102-MANAGED-OUTPUT\] C144-SCROLL page=PASS offset=changed result=PASS' "scrollbar-page-down"
                Add-C144Click $commands "statusCombo" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO open=PASS capture=combo result=PASS' "status-combo-open"
                Add-C144Click $commands "statusComboRow2" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO commit=PASS translated-origin=PASS result=PASS' "status-combo-commit"
                for ($cycle = 0; $cycle -lt 5; $cycle++) {
                    Add-C144Click $commands "statusCombo" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO open=PASS capture=combo result=PASS' "popup-cancel-open-$cycle"
                    Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO popup=closed capture=none result=PASS' "popup-outside-cancel-$cycle"
                }
                Add-C144Click $commands "detailMode" '(?m)^\[C102-MANAGED-OUTPUT\] C144-RADIO status-group=independent result=PASS' "detailed-status"
                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "options-open"
                Add-C144Click $commands "menuApply" '(?m)^\[C102-MANAGED-OUTPUT\] C144-APPLY menu=PASS dirty=cleared viewport=preserved result=PASS' "menu-apply"
                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "options-open-defaults"
                Add-C144Click $commands "menuDefaults" '(?m)^\[C102-MANAGED-OUTPUT\] C144-DEFAULTS menu=PASS controls=converged viewport=preserved result=PASS' "menu-defaults"
                Add-C144Click $commands "advancedToggle" '(?m)^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=true extent=grown scrollbar=synced result=PASS' "advanced-show"
                Add-C144Move $commands "scrollbarThumb" "advanced-scrollbar-thumb-move"
                $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $true; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-DRAG press=PASS owner=scrollbar result=PASS'; phase = "advanced-scrollbar-down" })
                Add-C144Move $commands "scrollbarBottom" "advanced-scrollbar-bottom-drag"
                $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $false; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-DRAG release=PASS owner=none result=PASS'; phase = "advanced-scrollbar-up" })
                Add-C144Click $commands "apply" '(?m)^\[C102-MANAGED-OUTPUT\] C144-APPLY pointer=PASS dirty=cleared viewport=preserved result=PASS' "advanced-apply"
                Add-C144Click $commands "inputEnabled" '(?m)^\[C102-MANAGED-OUTPUT\] C144-GROUP input=disabled pointer-tab-popup=gated result=PASS' "input-disable"
                Add-C144Click $commands "defaults" '(?m)^\[C102-MANAGED-OUTPUT\] C144-DEFAULTS pointer=PASS controls=converged result=PASS' "advanced-defaults"
                Add-C144Click $commands "advancedToggle" '(?m)^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=true extent=grown scrollbar=synced result=PASS' "advanced-restore-show"
                Add-C144Move $commands "scrollbarThumb" "restore-scrollbar-thumb-move"
                $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $true; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-DRAG press=PASS owner=scrollbar result=PASS'; phase = "restore-scrollbar-down" })
                Add-C144Move $commands "scrollbarBottom" "restore-scrollbar-bottom-drag"
                $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $false; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-DRAG release=PASS owner=none result=PASS'; phase = "restore-scrollbar-up" })
                for ($cycle = 0; $cycle -lt 10; $cycle++) {
                    Add-C144Click $commands "advancedToggle" '(?m)^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=false extent=shrunk viewport=clamped result=PASS' "advanced-stress-hide-$cycle"
                    Add-C144Click $commands "advancedToggle" '(?m)^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=true extent=grown scrollbar=synced result=PASS' "advanced-stress-show-$cycle"
                }
                Add-C144Move $commands "scrollbarThumb" "keyboard-scrollbar-thumb-move"
                $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $true; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-DRAG press=PASS owner=scrollbar result=PASS'; phase = "keyboard-scrollbar-down" })
                Add-C144Move $commands "scrollbarTop" "keyboard-scrollbar-top-drag"
                $commands.Add([pscustomobject]@{ action = "button"; button = "left"; down = $false; marker = '(?m)^\[C102-MANAGED-OUTPUT\] C144-DRAG release=PASS owner=none result=PASS'; phase = "keyboard-scrollbar-up" })
                for ($tab = 0; $tab -lt 5; $tab++) {
                    Add-C144Key $commands "tab" $true "" "keyboard-tab-$tab-down"
                    Add-C144Key $commands "tab" $false "" "keyboard-tab-$tab-up"
                }
                Add-C144Key $commands "ret" $true '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO keyboard-open=PASS capture=combo result=PASS' "keyboard-combo-open"
                Add-C144Key $commands "ret" $false "" "keyboard-combo-open-up"
                Add-C144Key $commands "down" $true "" "keyboard-combo-down"
                Add-C144Key $commands "down" $false "" "keyboard-combo-down-up"
                Add-C144Key $commands "ret" $true '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO keyboard-commit=PASS capture=none result=PASS' "keyboard-combo-commit"
                Add-C144Key $commands "ret" $false "" "keyboard-combo-commit-up"
                Add-C144Key $commands "tab" $true '(?m)^\[C102-MANAGED-OUTPUT\] C144-FOCUS tab=offscreen-revealed result=PASS' "keyboard-checkbox-tab"
                Add-C144Key $commands "tab" $false "" "keyboard-checkbox-tab-up"
                Add-C144Key $commands "spc" $true '(?m)^\[C102-MANAGED-OUTPUT\] C144-KEYBOARD space=activated working=changed result=PASS' "keyboard-checkbox-space"
                Add-C144Key $commands "spc" $false "" "keyboard-checkbox-space-up"
                Add-C144Key $commands "tab" $true "" "keyboard-radio-tab"
                Add-C144Key $commands "tab" $false "" "keyboard-radio-tab-up"
                Add-C144Key $commands "down" $true '(?m)^\[C102-MANAGED-OUTPUT\] C144-KEYBOARD radio=arrow-selection result=PASS' "keyboard-radio-arrow"
                Add-C144Key $commands "down" $false "" "keyboard-radio-arrow-up"
                for ($tab = 0; $tab -lt 3; $tab++) {
                    Add-C144Key $commands "tab" $true "" "keyboard-apply-tab-$tab-down"
                    Add-C144Key $commands "tab" $false "" "keyboard-apply-tab-$tab-up"
                }
                Add-C144Key $commands "ret" $true '(?m)^\[C102-MANAGED-OUTPUT\] C144-KEYBOARD button=apply result=PASS' "keyboard-apply"
                Add-C144Key $commands "ret" $false "" "keyboard-apply-up"
                Add-C144Key $commands "shift" $true "" "keyboard-shift-down"
                Add-C144Key $commands "tab" $true '(?m)^\[C102-MANAGED-OUTPUT\] C144-FOCUS shift-tab=handled earlier-member=PASS result=PASS' "keyboard-shift-tab"
                Add-C144Key $commands "tab" $false "" "keyboard-shift-tab-up"
                Add-C144Key $commands "shift" $false "" "keyboard-shift-up"
                Add-C144Click $commands "options" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU open=PASS capture=menu result=PASS' "final-menu-open"
                Add-C144Click $commands "menuClose" '(?m)^\[C102-MANAGED-OUTPUT\] C144-MENU command=close result=PASS' "final-menu-close"
                $commands.Add([pscustomobject]@{ action = "done"; marker = ""; phase = "complete" })
                $commandList = $commands.ToArray()
                $calibrating = $true
                Send-C136QmpEvents $MonitorPort (New-C136RelativeMove 1 1) $MonitorLog
                $previousSerialLength = $partial.Length
                $commandIndex = 1
                }
            }
            if ($calibrating -and $commandIndex -ge 1) {
                $start = [Math]::Min($previousSerialLength, $partial.Length)
                if ($partial.Substring($start) -match '(?m)^\[C138-NATIVE-INPUT\] kind=pointer-move .*result=PASS') {
                    $pointer = Get-C138NativePointer $partial
                    $cursorX = $pointer.x
                    $cursorY = $pointer.y
                    $previousSerialLength = $partial.Length
                    $calibrating = $false
                }
            }
            if ($null -ne $commandList -and -not $calibrating -and
                    $commandIndex -lt $commandList.Count) {
                $command = $commandList[$commandIndex]
                $markerSatisfied = [string]::IsNullOrEmpty($previousMarker)
                if (-not $markerSatisfied) {
                    $start = [Math]::Min($previousSerialLength, $partial.Length)
                    $markerSatisfied = $partial.Substring($start) -match $previousMarker
                }
                if ($markerSatisfied) {
                    switch ($command.action) {
                        "calibrate" { $events = New-C136RelativeMove 1 1 }
                        "move" {
                            $geometry = if ($isC145 -and -not $isC146) { Get-C144Geometry $partial "initial" } else { Get-C144Geometry $partial }
                            $point = switch -Exact ($command.target) {
                                "density" { $geometry.density }
                                "densityRow2" { [pscustomobject]@{ x = $geometry.density.x + 12; y = $geometry.density.y + 45 } }
                                "speed" { $geometry.speed }
                                "speedRow3" { [pscustomobject]@{ x = $geometry.speed.x + 12; y = $geometry.speed.y + 63 } }
                                "scrollLines" { $geometry.scrollLines }
                                "showStatus" { $geometry.showStatus }
                                "naturalWheel" { $geometry.naturalWheel }
                                "showTips" { $geometry.showTips }
                                "detailMode" { $geometry.detailMode }
                                "statusCombo" { $geometry.statusCombo }
                                "statusComboRow2" { [pscustomobject]@{ x = $geometry.statusCombo.x + 12; y = $geometry.statusCombo.y + 45 } }
                                "inputEnabled" { $geometry.inputEnabled }
                                "advancedToggle" { $geometry.advancedToggle }
                                "apply" { $geometry.apply }
                                "defaults" { $geometry.defaults }
                                "options" { $geometry.options }
                                "outsideOptions" { $geometry.options }
                                "menuApply" { [pscustomobject]@{ x = $geometry.options.x + 20; y = $geometry.options.y + 32 } }
                                "menuDefaults" { [pscustomobject]@{ x = $geometry.options.x + 20; y = $geometry.options.y + 52 } }
                                "menuClose" { [pscustomobject]@{ x = $geometry.options.x + 20; y = $geometry.options.y + 92 } }
                                "resetCancel" { Get-C145DialogPoint $partial "reset" "cancel" }
                                "resetConfirm" { Get-C145DialogPoint $partial "reset" "confirm" }
                                "dirtyCancel" { Get-C145DialogPoint $partial "close" "cancel" }
                                "dirtyApply" { Get-C145DialogPoint $partial "close" "apply" }
                                "dirtyDiscard" { Get-C145DialogPoint $partial "close" "discard" }
                                "persistenceOk" { Get-C145DialogPoint $partial "persistence" "ok" }
                                "wheelPoint" { [pscustomobject]@{ x = $geometry.view.x + 140; y = $geometry.view.y + 100 } }
                                "scrollbarThumb" { [pscustomobject]@{ x = $geometry.bar.x + 8; y = $geometry.thumbTop + [Math]::Floor($geometry.thumbHeight / 2) } }
                                "scrollbarPageTop" { [pscustomobject]@{ x = $geometry.bar.x + 8; y = $geometry.trackTop + 10 } }
                                "scrollbarPageBottom" { [pscustomobject]@{ x = $geometry.bar.x + 8; y = $geometry.thumbTop + $geometry.thumbHeight + 10 } }
                                "scrollbarBottom" { [pscustomobject]@{ x = $geometry.bar.x + 8; y = $geometry.bar.y + 198 } }
                                "scrollbarTop" { [pscustomobject]@{ x = $geometry.bar.x + 8; y = $geometry.trackTop + [Math]::Floor($geometry.thumbHeight / 2) } }
                                default { throw "Unknown C144 pointer target $($command.target)." }
                            }
                            $events = New-C136RelativeMove ($point.x - $cursorX) ($point.y - $cursorY)
                            $cursorX = $point.x
                            $cursorY = $point.y
                        }
                        "button" { $events = @(New-C136Button $command.button $command.down) }
                        "key" { $events = @(New-C136Key $command.code $command.down) }
                        "events" { $events = $command.events }
                        "done" { $commandIndex = $commandList.Count; break }
                        default { throw "Unknown C144 QMP action $($command.action)." }
                    }
                    if ($command.action -ne "done") {
                        Send-C136QmpEvents $MonitorPort $events $MonitorLog
                        if ($command.action -eq "move") {
                            $previousMarker = ""
                        } else { $previousMarker = $command.marker }
                        $previousSerialLength = $partial.Length
                        $commandIndex++
                    }
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
            if ($isC148) {
                try { Stop-C148QemuCleanly $MonitorPort $process $MonitorLog }
                catch { Add-Content -LiteralPath $MonitorLog -Value ("shutdown=quit error=" + $_.Exception.Message) -Encoding ASCII }
                $process.Refresh()
            }
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
            }
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

function Invoke-C146OrdinaryBoot([string]$Esp, [string]$Serial, [string]$Stdout,
                                [string]$Stderr, [int]$BootNumber,
                                [string]$Qemu, [string]$Ovmf) {
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
        $desktopReady = $false
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            $partial = if (Test-Path -LiteralPath $Serial) {
                Get-Content -LiteralPath $Serial -Raw -ErrorAction SilentlyContinue
            } else { "" }
            if ($partial -match '(?m)^\[desktop\] bare-metal desktop icon init completed') {
                $desktopReady = $true
                break
            }
            $process.Refresh()
            if ($process.HasExited) { break }
        }
        $deadlineExpired = -not $desktopReady -and -not $process.HasExited -and (Get-Date) -ge $deadline
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        } else {
            Wait-Process -Id $process.Id -ErrorAction SilentlyContinue
        }
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        $serialText = if (Test-Path -LiteralPath $Serial) { Get-Content -LiteralPath $Serial -Raw } else { "" }
        $desktopReady = $serialText -match '(?m)^\[desktop\] bare-metal desktop icon init completed'
        $timedOut = $deadlineExpired -and -not $desktopReady
        if (-not $desktopReady -or $serialText -notmatch '(?m)^\[KERNEL\] Boot method: UEFI BootInfo') {
            throw "C146 ordinary boot $BootNumber did not reach the bare-metal desktop initialization marker."
        }
        if ($serialText -match 'PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
            throw "C146 ordinary boot $BootNumber serial contains a kernel failure marker."
        }
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
        stdoutPath = $Stdout; stderrPath = $Stderr
        qemuExitCode = $process.ExitCode; timedOut = $timedOut
    }
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
    if ($isC148 -and $Serial -match '(?m)^\[C148-NOTES-ONLY\] settings-center=not-launched result=PASS') {
        $required = @($required | Where-Object {
            $_ -ne '^\[C103-RUNTIME\] resident runtime reused'
        })
    }
    if (-not $isC145 -and -not $isC140 -and -not $isC141 -and -not $isC142 -and -not $isC143 -and -not $isC144 -and -not $isC121 -and -not $isC122 -and -not $isC123 -and -not $isC124 -and -not $isC125 -and -not $isC126 -and -not $isC127 -and -not $isC128 -and -not $isC129 -and -not $isC130 -and -not $isC131 -and -not $isC132 -and -not $isC133 -and -not $isC134 -and -not $isC135 -and -not $isC136 -and -not $isC137 -and -not $isC138) {
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
    if ($isC148) {
        $notesOnlyBoot = $Serial -match '(?m)^\[C148-NOTES-ONLY\] settings-center=not-launched result=PASS'
        $required += @(
            '^\[C147-APPMODEL\] catalogValid=true result=PASS',
            '^\[C147-NOTES-FIRST\] launch=PASS runtime-effect-before-settings-center=PASS result=PASS',
            '^\[C147-NOTES-TARGET\] screenListX=[0-9A-Fa-f]+ screenListY=[0-9A-Fa-f]+ result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C147-NOTES-HOST registration=5 list=registered initial=no-focus result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C148-NOTES-CONTEXT settings=v2-migration-proof source=shared-runtime result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C148-NOTES-FIRST control=ListBox firstVisible=16 scrollLines=[1-8] before-settings-center=true result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C148-NOTES-WHEEL normalizedNotches=-1 naturalScroll=[01] scrollLines=[1-8] firstVisible=16->\d+ result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C148-NOTES-WHEEL normalizedNotches=1 naturalScroll=[01] scrollLines=[1-8] firstVisible=\d+->16 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C147-RUNTIME-SETTING source=(file|missing|invalid|io-failure) naturalScroll=[01] ready=true before-application=true result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C148-RUNTIME-SETTING source=(file|missing|invalid|io-failure) naturalScroll=[01] fileVersion=[0-2] scrollLines=[1-8] ready=true before-application=true result=PASS')
        if ($notesOnlyBoot) {
            $required += '^\[C148-NOTES-ONLY\] settings-center=not-launched result=PASS'
        } else {
            $required += @(
                '^\[C147-PROOF\] managed-proof-started context=c147-runtime-settings notes-first=true settings-after-notes=true result=PASS',
                '^\[C144-PROOF\] managed-proof-started context=c144-native transport=physical-qemu result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C148-FORMAT-TESTS cases=23 v1-migration=PASS v2=PASS result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C148-RUNTIME-STATE-TESTS cases=17 startup=PASS transactional-apply=PASS result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C148-CONSUMER-TESTS cases=15 ListBox=PASS TextArea=PASS clamp=PASS result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C148-FOCUSED-TESTS format=PASS runtime=PASS consumer=PASS result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C148-SETTINGS-SYNC naturalScroll=[01] scrollLines=[1-8] dirty=false runtime=agrees result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C148-COMPOSITION launch=PASS reg=10 host=10 groups=4 leaves=18 view=22 input=6 capacity=24 layout=valid result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C148-PROOF launch=PASS registration=10 hostCapacity=10 dialogCapacity=8 resetRegistrations=2 closeRegistrations=3 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-FORMAT-TESTS cases=13 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-STORE-TESTS cases=10 stress=50 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-SETTINGS-TESTS cases=11 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-FOCUSED-SUITES result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-FINAL viewport=valid registration=10 modal=none capture=none drag=none result=PASS')
            if ($Serial -notmatch '(?m)^\[C148-LOAD-META\]') {
                $required += '^\[C102-MANAGED-OUTPUT\] C148-LOAD-META path=/system/apps/GXSETT\.BIN v=[12] size=(25|26) migrate=(memory|no) unchanged=yes viewport=0 focus=normal result=PASS'
            }
        }
        if ($isC149) {
            $required += @(
                '^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-SETTING source=(file|missing|invalid|io-failure) keyboardTips=[01] ready=true before-application=true result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C149-NOTES-TIPS startup=before-settings-center visible=(true|false) rendered=true result=PASS')
            if (-not $notesOnlyBoot) {
                $required += @(
                    '^\[C102-MANAGED-OUTPUT\] C149-FIELD-AUDIT cases=14 fields=10 runtime-backed=3 v1-v2=PASS result=PASS',
                    '^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-TESTS cases=14 startup=PASS apply=PASS failure=PASS reset-discard-cancel=PASS result=PASS',
                    '^\[C102-MANAGED-OUTPUT\] C149-CONSUMER-TESTS cases=14 tips-visible-hidden=PASS runtime-transition=PASS startup-fallback=PASS result=PASS',
                    '^\[C102-MANAGED-OUTPUT\] C149-SETTINGS-SYNC keyboardTips=[01] dirty=false runtime=agrees result=PASS')
            }
        }
        if ($isC150) {
            $required = @($required | Where-Object { $_ -notmatch 'C147-APPMODEL|C147-NOTES-FIRST|C147-NOTES-TARGET' -and -not ($notesOnlyBoot -and $_ -match 'C148-NOTES-WHEEL') })
            $required += @(
                '^\[C150-RETURN-TARGET-TESTS\] cases=10 capacity=1 identity=canonical self=reject invalid=reject result=PASS',
                '^\[C150-MANAGED-OUTPUT\] C150-MANAGED-LIFECYCLE-TESTS cases=8 fresh=PASS active=one result=PASS',
                '^\[C150-APPMODEL\] catalog=notes\+settings identity=canonical result=PASS')
            if (-not $notesOnlyBoot) {
                $required += @(
                    '^\[C150-PRIMARY-ENTRY\] caller=Notes settings=active return-target=Notes surface-handoff=true result=PASS')
            }
        }
        foreach ($pattern in $required) {
            if ($Serial -notmatch "(?m)$pattern") { throw "C148 managed proof missing serial marker: $pattern" }
        }
        if ($Serial -match '(?m)^\[(?:C149|C148|C147)-[^\r\n]*result=FAIL|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
            throw 'C148 serial output contains a runtime, migration, or real-consumer failure marker.'
        }
        $settingsIndex = $Serial.IndexOf('[C144-PROOF]')
        if ($notesOnlyBoot -and $settingsIndex -ge 0) { throw 'C148 Notes-only boot unexpectedly opened Settings Center.' }
        if (-not $notesOnlyBoot -and $settingsIndex -le $Serial.IndexOf('C148-NOTES-WHEEL')) {
            throw 'C148 full boot must prove real Notes behavior before Settings Center.'
        }
    } elseif ($isC147) {
        $invalidStartupRecovery = $Serial -match '(?m)^\[C102-MANAGED-OUTPUT\] C147-RUNTIME-SETTING source=invalid naturalScroll=0 ready=true before-application=true result=PASS'
        $required += @(
            '^\[C147-APPMODEL\] catalogValid=true result=PASS',
            '^\[C147-PROOF\] managed-proof-started context=c147-runtime-settings notes-first=true settings-after-notes=true result=PASS',
            '^\[C147-NOTES-FIRST\] launch=PASS runtime-effect-before-settings-center=PASS result=PASS',
            '^\[C147-NOTES-TARGET\] screenListX=[0-9A-Fa-f]+ screenListY=[0-9A-Fa-f]+ result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C147-NOTES-HOST registration=5 list=registered initial=no-focus result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C147-STARTUP-TESTS cases=10 missing=corrupt-version-truncated once=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C147-RUNTIME-STATE-TESTS cases=15 defaults=commit-reject-apply-failure-reset-discard result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C147-CONSUMER-TESTS cases=10 TextArea=PASS ListBox=PASS lifecycle=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C147-FOCUSED-TESTS startup=PASS runtime=PASS consumer=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C146-FORMAT-TESTS cases=13 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C146-STORE-TESTS cases=10 stress=50 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C146-PROOF launch=PASS registration=9 hostCapacity=10 dialogCapacity=8 resetRegistrations=2 closeRegistrations=3 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C147-SETTINGS-SYNC source=shared-runtime snapshot=agrees dirty=false result=PASS')
        if ($invalidStartupRecovery) {
            $required += @(
                '^\[C102-MANAGED-OUTPUT\] C147-STARTUP-RECOVERY settings-center=launchable c146-settings=skipped-invalid-startup result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C147-REGRESSION-SCOPE c144=separate c145-dialog=separate c146-store=PASS c146-settings=SKIPPED-invalid-startup result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-FOCUSED-SUITES result=SKIPPED-invalid-startup')
        } else {
            $required += @(
                '^\[C102-MANAGED-OUTPUT\] C147-REGRESSION-SCOPE c144=separate c145-dialog=separate c146-store=PASS c146-settings=PASS result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-SETTINGS-TESTS cases=11 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C147-FAILURE-INJECTION persistence=failed runtime=preserved persisted=preserved working=preserved dirty=true result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-FOCUSED-SUITES result=PASS')
        }
        if ($Serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-RUNTIME-SETTING source=(file|missing|invalid|io-failure) naturalScroll=[01] ready=true before-application=true result=PASS') {
            throw 'C147 startup did not publish a valid runtime snapshot before the managed app ran.'
        }
        if ($Serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-CONSUMER app=ManagedNotes control=ListBox natural=[01] delta=[+-]?1 before=\d+ after=\d+ changed=true result=PASS') {
            throw 'C147 real Notes ListBox behavior did not change under the startup runtime setting.'
        }
        $runtimeIndex = $Serial.IndexOf('C147-RUNTIME-SETTING')
        $consumerIndex = $Serial.IndexOf('C147-CONSUMER')
        $settingsIndex = $Serial.IndexOf('[C144-PROOF]')
        if ($runtimeIndex -lt 0 -or $consumerIndex -le $runtimeIndex -or
            $settingsIndex -le $consumerIndex) {
            throw 'C147 runtime load and Notes consumer effect must precede Settings Center launch.'
        }
    } elseif ($isC145) {
        $required += @(
            '^\[C145-APPMODEL\] catalogValid=true result=PASS',
            '^\[C145-PROOF\] managed-proof-started context=c145-native transport=physical-qemu result=PASS',
            '^\[C145-RELAUNCH\] close=PASS relaunch=PASS registration=9 result=PASS')
        if ($isC146) {
            $required += @(
                '^\[C102-MANAGED-OUTPUT\] C146-REGRESSION-SCOPE c128-c131=prior-c145-proof c145-dialog=historical c145-settings=covered-by-c146 result=START',
                '^\[C102-MANAGED-OUTPUT\] C146-PROOF launch=PASS registration=9 hostCapacity=10 dialogCapacity=8 resetRegistrations=2 closeRegistrations=3 result=PASS')
        } else {
            $required += @(
                '^\[C145-TARGET\].*result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-DIALOG-TESTS core=15 members=10 messagebox=11 routing=13 total=49 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-SETTINGS-TESTS cases=18 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-FOCUSED-SUITES result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-PROOF launch=PASS registration=9 hostCapacity=10 dialogCapacity=8 resetRegistrations=2 closeRegistrations=3 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-RESET open=PASS modal=active focus=default parent=blocked result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-RESET result=Cancel working=preserved viewport=preserved focus=restored result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults dirty=updated viewport=valid focus=restored result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-UNSAVED open=PASS modal=active parent=blocked buttons=3 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-UNSAVED result=Cancel dirty=preserved parent=open focus=restored result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-UNSAVED result=Apply applied=committed closed=true result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-MODAL wheel=parent-blocked viewport=preserved result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-MODAL outside=consumed parent=blocked result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-FOCUS tab=contained result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-FOCUS shift-tab=contained result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-FOCUS reverse=advanced-within-modal result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-KEYBOARD default=activated once result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-FINAL viewport=valid registration=9 modal=none capture=none drag=none result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C145-LOWER-REGRESSIONS c128=PASS c131=57/57 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C128-PANEL-LIFECYCLE-TESTS cases=\d+ result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C131-CHECKBOX-TESTS cases=34 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C131-CHECKBOX-HOST-TESTS cases=23 result=PASS')
        }
        if (-not $isC146) {
            if ([regex]::Matches($Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Cancel working=preserved').Count -lt 2) {
                $required += '^\[C145-ASSERT\] expected-two-reset-cancellations=PASS'
            }
            if ([regex]::Matches($Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults').Count -lt 2) {
                $required += '^\[C145-ASSERT\] expected-keyboard-and-pointer-reset-confirmations=PASS'
            }
        }
        if ($isC146) {
            $required += @(
                '^\[C102-MANAGED-OUTPUT\] C146-FORMAT-TESTS cases=13 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-STORE-TESTS cases=10 stress=50 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-SETTINGS-TESTS cases=11 result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-FOCUSED-SUITES result=PASS',
                '^\[C102-MANAGED-OUTPUT\] C146-APPLY-FAIL messagebox=opened working=preserved applied=preserved persisted=preserved dirty=true result=PASS')
        }
    } elseif ($isC144) {
        $required += @(
            '^\[C144-APPMODEL\] catalogValid=true result=PASS',
            '^\[C144-PROOF\] managed-proof-started context=c144-native transport=physical-qemu result=PASS',
            '^\[C144-RELAUNCH\] close=PASS relaunch=PASS registration=9 result=PASS',
            '^\[C144-TARGET\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-STATE-TESTS cases=22 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-COMPOSITION-TESTS cases=21 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-LIFECYCLE-TESTS cases=13 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-TESTS state=22 composition=21 lifecycle=13 total=56 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-PROOF launch=PASS registration=9 hostCapacity=10 groupBoxes=4 leaves=17 viewMembers=21 stacks=4 layout=valid result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-COMBO open=PASS capture=combo result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-COMBO commit=PASS translated-origin=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-WHEEL viewport=changed sections-translated=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-DRAG press=PASS owner=scrollbar result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-DRAG release=PASS owner=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-SCROLL page=PASS offset=changed result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-CAPTURE group-disabled=cancelled owner=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-GROUP input=disabled pointer-tab-popup=gated result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-GROUP input=enabled own-member-state=preserved result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=true extent=grown scrollbar=synced result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=false extent=shrunk viewport=clamped result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-APPLY menu=PASS dirty=cleared viewport=preserved result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-APPLY pointer=PASS dirty=cleared viewport=preserved result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-DEFAULTS menu=PASS controls=converged viewport=preserved result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-COMBO keyboard-open=PASS capture=combo result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-COMBO keyboard-commit=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-KEYBOARD space=activated working=changed result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-KEYBOARD radio=arrow-selection result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-KEYBOARD button=apply result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-FOCUS shift-tab=handled earlier-member=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-FINAL viewport=valid layout=valid membership=valid capture=none drag=none registration=9 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-CLOSE popup=closed capture=none registration=bounded result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-MENU command=close result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-DEFAULTS pointer=PASS controls=converged result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-COMBO keyboard-open=PASS capture=combo result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-COMBO keyboard-commit=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-KEYBOARD space=activated working=changed result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-KEYBOARD radio=arrow-selection result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-KEYBOARD button=apply result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-FOCUS tab=offscreen-revealed result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-FOCUS shift-tab=handled earlier-member=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-MENU command=close result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-CLOSE popup=closed capture=none registration=bounded result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C144-FINAL viewport=valid layout=valid membership=valid capture=none drag=none registration=9 result=PASS')
        if ([regex]::Matches($Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO popup=closed capture=none result=PASS').Count -lt 5) {
            $required += '^\[C144-ASSERT\] expected-five-outside-popup-cancellations=PASS'
        }
        if ([regex]::Matches($Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=false extent=shrunk viewport=clamped result=PASS').Count -lt 10) {
            $required += '^\[C144-ASSERT\] expected-repeated-dynamic-shrink=PASS'
        }
    } elseif ($isC143) {
        $required += @(
            '^\[C143-APPMODEL\] catalogValid=true result=PASS',
            '^\[C143-PROOF\] managed-proof-started context=c143-native transport=physical-qemu result=PASS',
            '^\[C143-RELAUNCH\] close=PASS relaunch=PASS registration=6 result=PASS',
            '^\[C143-TARGET\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C126-RETAINED groupbox=60 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-TESTS core=20 visibility=10 scrollview=10 focus=10 popup=6 total=56 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-TESTS metadata=20 spacing=12 horizontal=12 scrollview=14 popup=10 total=68 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-TESTS core=20 composition=10 rendering=10 focus=10 popup=8 dynamic=10 total=68 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-PROOF launch=PASS registration=6 viewMembers=9 groupMembers=8 stackMembers=8 layout=valid result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-CONTENT rect=valid frame=first responsibilities=separate result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-COMBO open=PASS arranged=PASS capture=combo result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-COMBO commit=PASS translated-origin=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-RADIO explicit-group=PASS selected=two result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-RADIO explicit-group=PASS selected=one result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-CHECKBOX callback=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-DYNAMIC hide=PASS margins=removed button=moved frame-stable=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-WHEEL viewport=changed frame-and-members-translated=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-DRAG press=PASS owner=scrollbar result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-DRAG release=PASS owner=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-MENU open=PASS button-invoked=PASS capture=menu result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-MENU commit=PASS button-invoked=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-GROUP visible=false effective-members=hidden result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-GROUP visible=true effective-members=restored result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-GROUP enabled=false input-gated result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-GROUP enabled=true own-state-preserved result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-CAPTURE invalidated=cancelled owner=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-FOCUS tab=offscreen-revealed result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-FOCUS shift-tab=handled earlier-member=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C143-FINAL viewport=valid layout=valid capture=none drag=none result=PASS')
        if ([regex]::Matches($Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C143-CAPTURE invalidated=cancelled owner=none result=PASS').Count -lt 3) {
            $required += '^\[C143-ASSERT\] expected-three-group-state-capture-cancellations=PASS'
        }
    } elseif ($isC142) {
        $required += @(
            '^\[C142-APPMODEL\] catalogValid=true result=PASS',
            '^\[C142-PROOF\] managed-proof-started context=c142-native transport=physical-qemu result=PASS',
            '^\[C142-TARGET\].*result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-FOCUSED metadata=20 spacing=12 horizontal=12 scrollview=14 popup=10 total=68 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C141-FOCUSED core=20 visibility=10 scrollview=10 focus=10 popup=6 total=56 result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-PROOF launch=PASS registration=2 members=8 layout=valid alignment=valid result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-LAYOUT initial=PASS content-taller-than-viewport=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-ALIGN left=PASS center=PASS right=PASS stretch=PASS pointer-ready=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-POPUP open=PASS arranged-geometry=PASS after-relayout=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-POPUP follow-up=PASS capture=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-VISIBILITY checkbox=PASS progress=hidden margins=removed button=moved content=shrunk relayout=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-WHEEL viewport=changed stack-translated=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-DRAG press=PASS owner=scrollbar result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-DRAG release=PASS owner=none result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-POINTER button=right-aligned hit=PASS translated=PASS result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-FOCUS tab=revealed margins=accounted result=PASS',
            '^\[C102-MANAGED-OUTPUT\] C142-FINAL viewport=valid layout=valid capture=none drag=none result=PASS')
    } elseif ($isC141) {
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
    if (-not $isC145 -and -not $isC143 -and -not $isC142 -and -not $isC140 -and -not $isC141 -and -not $isC144 -and -not $isC136 -and -not $isC137 -and -not $isC138 -and -not $isC121 -and -not $isC122 -and -not $isC123 -and -not $isC124 -and -not $isC125 -and -not $isC126 -and -not $isC127 -and -not $isC128 -and -not $isC129 -and -not $isC130 -and -not $isC131 -and -not $isC132 -and -not $isC133 -and -not $isC134 -and -not $isC135 -and $spaceMarker -ne 1) { throw "C120 expected one exact-once Space marker, got $spaceMarker." }
    $saveActivation = @([regex]::Matches($Serial,
        '(?m)^\[C102-MANAGED-OUTPUT\] C120-ACTIVATE control=Save result=PASS\r?$')).Count
    if (-not $isC145 -and -not $isC143 -and -not $isC142 -and -not $isC140 -and -not $isC141 -and -not $isC144 -and -not $isC136 -and -not $isC137 -and -not $isC138 -and -not $isC121 -and -not $isC122 -and -not $isC123 -and -not $isC124 -and -not $isC125 -and -not $isC126 -and -not $isC127 -and -not $isC128 -and -not $isC129 -and -not $isC130 -and -not $isC131 -and -not $isC132 -and -not $isC133 -and -not $isC134 -and -not $isC135 -and $saveActivation -ne 1) { throw "C120 expected one managed Save activation, got $saveActivation." }
    $failureCheckSerial = $Serial
    if ($isC150) {
        $expectedFailureLines = @(
            '[C102-MANAGED-OUTPUT] C146-SAVE result=FAIL working=preserved applied=preserved persisted=preserved dirty=true',
            '[C102-MANAGED-OUTPUT] C147-SAVE-FAILURE status=io-failure injected=true store=present result=FAIL',
            '[C102-MANAGED-OUTPUT] C147-RUNTIME-APPLY result=SKIPPED reason=persistence-failed active=preserved persisted=preserved',
            '[C102-MANAGED-OUTPUT] C146-APPLY-FAIL messagebox=opened working=preserved applied=preserved persisted=preserved dirty=true result=PASS',
            '[C102-MANAGED-OUTPUT] C146-UNSAVED result=Apply failed=kept-open dirty=true result=PASS',
            '[C150-MANAGED-OUTPUT] C150-FAILURE-INJECT armed=true result=PASS',
            '[C150-MANAGED-OUTPUT] C150-ERROR-DISMISSED settings=active retry=available result=PASS',
            '[C102-MANAGED-OUTPUT] C149-RUNTIME-APPLY oldKeyboardTips=0 newKeyboardTips=1 persistence=verified runtime=committed result=PASS',
            '[C102-MANAGED-OUTPUT] C145-UNSAVED result=Apply applied=committed closed=true result=PASS')
        $expectedFailureScenario = $true
        foreach ($line in $expectedFailureLines) {
            $linePattern = '(?m)^' + [regex]::Escape($line) + '\r?$'
            if ([regex]::Matches($Serial, $linePattern).Count -lt 1) {
                $expectedFailureScenario = $false
                break
            }
        }
        if ($expectedFailureScenario) {
            foreach ($line in $expectedFailureLines) {
                $linePattern = '(?m)^' + [regex]::Escape($line) + '\r?\n?'
                $failureCheckSerial = [regex]::Replace($failureCheckSerial, $linePattern, '')
            }
        }
    }
    if ($failureCheckSerial -match '(?m)^\[(?:C146|C145|C144|C143|C142|C141|C140|C138|C137|C136|C135|C134|C132|C131|C130|C129|C120|C121|C122|C123|C124|C125|C126|C127|C128)-[^\r\n]*FAIL|C147-[^\r\n]*\bresult=FAIL\b|PageFault|triple.?fault|FAIL_FAST|fatal kernel failure|boot failure') {
        throw "Managed control proof serial output contains a failure or fault marker."
    }
    [pscustomobject]@{
        outcome = if ($AllowBoundedHostDefect) { "BOUNDED-HOST-DEFECT" } else { "PASS" }
        spaceMarkers = $spaceMarker; saveActivations = $saveActivation
    }
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$c146PersistenceSequences = [System.Collections.Generic.List[object]]::new()
$c147StartupProofs = [System.Collections.Generic.List[object]]::new()
$c146OrdinaryBoots = [System.Collections.Generic.List[object]]::new()
$c146ProofKernelSha256 = $null
$c146CanonicalKernelSha256 = $null
$c146EspKernelSha256 = $null
$c146ProtectedRamdiskSha256 = $null
$c146CanonicalKernelBackup = $null
$c146EspKernelBackup = $null
$c148PersistenceSequences = [System.Collections.Generic.List[object]]::new()
$c149StartupMatrix = [System.Collections.Generic.List[object]]::new()
$script:C148NotesOnlyBoot = $false
$script:C148StartingFileHash = ""
if ($isC146) {
    $canonicalKernel = Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf'
    $espKernel = Join-Path $RepoRoot 'ESP\kernel.elf'
    $protectedRamdisk = Join-Path $RepoRoot 'ESP\ramdisk.img'
    foreach ($requiredFile in @($canonicalKernel, $espKernel, $protectedRamdisk)) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) { throw "C146 protected ordinary-boot input is missing: $requiredFile" }
    }
    $c146CanonicalKernelSha256 = Get-Hash $canonicalKernel
    $c146EspKernelSha256 = Get-Hash $espKernel
    $c146ProtectedRamdiskSha256 = Get-Hash $protectedRamdisk
    $c146CanonicalKernelBackup = Join-Path $EvidenceRoot 'canonical\kernel.elf'
    $c146EspKernelBackup = Join-Path $EvidenceRoot 'canonical\ESP-kernel.elf'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $c146CanonicalKernelBackup) | Out-Null
    Copy-Item -LiteralPath $canonicalKernel -Destination $c146CanonicalKernelBackup -Force
    Copy-Item -LiteralPath $espKernel -Destination $c146EspKernelBackup -Force
    if ((Get-Hash $c146CanonicalKernelBackup) -ne $c146CanonicalKernelSha256 -or
        (Get-Hash $c146EspKernelBackup) -ne $c146EspKernelSha256) {
        throw 'C146 could not preserve byte-identical backups of both ordinary kernels.'
    }
    if ($c146CanonicalKernelSha256 -ne $c146EspKernelSha256) {
        throw 'C146 starting canonical kernel and ESP kernel differ; preserving legitimate newer state and stopping before proof build.'
    }
}
trap {
    if ($isC146 -and $c146CanonicalKernelBackup -and $c146EspKernelBackup) {
        try {
            if (Test-Path -LiteralPath $c146CanonicalKernelBackup -PathType Leaf) {
                Copy-Item -LiteralPath $c146CanonicalKernelBackup -Destination (Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf') -Force
            }
            if (Test-Path -LiteralPath $c146EspKernelBackup -PathType Leaf) {
                Copy-Item -LiteralPath $c146EspKernelBackup -Destination (Join-Path $RepoRoot 'ESP\kernel.elf') -Force
            }
        } catch { Write-Warning "C146 automatic kernel restoration retry failed: $($_.Exception.Message)" }
    }
    throw $_
}
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
        $(if ($isC150) { "C150Composite" } elseif ($isC149) { "C149Composite" } elseif ($isC148) { "C148Composite" } elseif ($isC147) { "C147Composite" } elseif ($isC146) { "C146Composite" } elseif ($isC145) { "C145Composite" } elseif ($isC144) { "C144Composite" } elseif ($isC143) { "C143Composite" } elseif ($isC142) { "C142Composite" } elseif ($isC141) { "C141Composite" } elseif ($isC140) { "C140Composite" } elseif ($isC139) { "C139Composite" } elseif ($isC138) { "C138Composite" } elseif ($isC137) { "C137Composite" } elseif ($isC136) { "C136Composite" } elseif ($isC135) { "C135Composite" } elseif ($isC134) { "C134Composite" } elseif ($isC133) { "C133Composite" } elseif ($isC132) { "C132Composite" } elseif ($isC131) { "C131Composite" } elseif ($isC129) { "C129Composite" } elseif ($isC128) { "C128Composite" } elseif ($isC130 -or $isC127) { "C127Composite" } elseif ($isC126) { "C126Composite" } elseif ($isC125) { "C125Composite" } elseif ($isC124) { "C124Composite" } elseif ($isC123) { "C123Composite" } elseif ($isC122) { "C122Composite" } elseif ($isC121) { "C121Composite" } else { "C120Composite" }),
        "-PythonExe", $PythonExe)
    if ($isC150) { $managedBuildArguments += @("-HeapConfiguration", "Primary4MiB") }
    elseif ($isC148) { $managedBuildArguments += @("-HeapConfiguration", "Primary256KiB") }
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
if ($isC145) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU -DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU -DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING -DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR -DGXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT -DGXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW -DGXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK -DGXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK -DGXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C144_MANAGED_SETTINGS_CENTER -DGXOS_NATIVEAOT_C145_MANAGED_MODAL_DIALOG"
    if ($isC146) { $kernelFlags += " -DGXOS_NATIVEAOT_C146_SETTINGS_PERSISTENCE" }
    if ($isC147 -or $isC148) { $kernelFlags += " -DGXOS_NATIVEAOT_C147_RUNTIME_SETTINGS" }
    if ($isC148) { $kernelFlags += " -DGXOS_NATIVEAOT_C148_SETTINGS_V2" }
    if ($isC149) { $kernelFlags += " -DGXOS_NATIVEAOT_C149_SECOND_RUNTIME_SETTING" }
    if ($isC150) { $kernelFlags += " -DGXOS_NATIVEAOT_C150_MANAGED_APP_RETURN" }
}
elseif ($isC144) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU -DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU -DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING -DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR -DGXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT -DGXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW -DGXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK -DGXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK -DGXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C144_MANAGED_SETTINGS_CENTER"
}
elseif ($isC143) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU -DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU -DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING -DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR -DGXOS_NATIVEAOT_C139_SHARED_SCROLL_VIEWPORT -DGXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW -DGXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK -DGXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK -DGXOS_NATIVEAOT_C143_MANAGED_GROUP_BOX"
}
elseif ($isC142) {
    $kernelFlags += " -DGXOS_NATIVEAOT_C121_MANAGED_CHECKBOX -DGXOS_NATIVEAOT_C122_MANAGED_LABEL -DGXOS_NATIVEAOT_C123_MANAGED_SEPARATOR -DGXOS_NATIVEAOT_C124_MANAGED_RADIO_BUTTON -DGXOS_NATIVEAOT_C125_MANAGED_PROGRESS_BAR -DGXOS_NATIVEAOT_C126_MANAGED_GROUP_BOX -DGXOS_NATIVEAOT_C127_MANAGED_PANEL -DGXOS_NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE -DGXOS_NATIVEAOT_C131_REUSABLE_CHECKBOX -DGXOS_NATIVEAOT_C132_REUSABLE_RADIO_BUTTON -DGXOS_NATIVEAOT_C133_REUSABLE_COMBOBOX -DGXOS_NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING -DGXOS_NATIVEAOT_C135_REUSABLE_POPUP_MENU -DGXOS_NATIVEAOT_C136_SECONDARY_POINTER_CONTEXT_MENU -DGXOS_NATIVEAOT_C137_MOUSE_WHEEL_SCROLLING -DGXOS_NATIVEAOT_C138_REUSABLE_SCROLLBAR -DGXOS_NATIVEAOT_C142_MANAGED_VERTICAL_STACK"
}
elseif ($isC141) {
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
$proofKernelPath = if ($isC146 -and -not [string]::IsNullOrWhiteSpace($ProofKernelElfPath)) {
    [System.IO.Path]::GetFullPath($ProofKernelElfPath)
} else { $kernelPath }
if ($isC146 -and -not (Test-Path -LiteralPath $proofKernelPath -PathType Leaf)) {
    throw "C146 proof kernel is missing: $proofKernelPath"
}
if ($isC146 -and -not $SkipKernelBuild -and $proofKernelPath -ne $kernelPath) {
    throw 'Pass -SkipKernelBuild when supplying an external C146 proof kernel.'
}
$c148NotesOnlyKernelPath = $null
$c148UiKernelPath = $null
$c148KernelVariantsReused = $false
if ($isC148) {
    $kernelEvidenceRoot = Join-Path $EvidenceRoot 'kernels'
    New-Item -ItemType Directory -Force -Path $kernelEvidenceRoot | Out-Null
    $kernelVariantPhase = if ($isC150) { 'c150' } elseif ($isC149) { 'c149' } else { 'c148' }
    $c148UiKernelPath = Join-Path $kernelEvidenceRoot "kernel-$kernelVariantPhase-ui.elf"
    $c148NotesOnlyKernelPath = Join-Path $kernelEvidenceRoot "kernel-$kernelVariantPhase-notes-only.elf"
    if ($SkipKernelBuild) {
        foreach ($variant in @($c148UiKernelPath, $c148NotesOnlyKernelPath)) {
            if (-not (Test-Path -LiteralPath $variant -PathType Leaf)) {
                throw "$ProofPhase cached kernel variant is missing: $variant"
            }
        }
        $variantTimes = @(
            (Get-Item -LiteralPath $c148UiKernelPath).LastWriteTimeUtc,
            (Get-Item -LiteralPath $c148NotesOnlyKernelPath).LastWriteTimeUtc)
        $variantTime = $variantTimes | Sort-Object | Select-Object -First 1
        $kernelSourceRoot = Join-Path $RepoRoot 'kernel'
        $kernelSourceInputs = @(Get-ChildItem -LiteralPath $kernelSourceRoot -Recurse -File |
            Where-Object {
                $_.FullName -notmatch '\\build\\' -and
                ($_.Extension -in @('.c', '.cc', '.cpp', '.h', '.hpp', '.s', '.S', '.asm', '.inc', '.ld', '.mk') -or
                    $_.Name -eq 'Makefile')
            })
        $kernelSourceInputs += @(Get-Item -LiteralPath (Join-Path $RepoRoot 'compositor.cpp'),
            (Join-Path $RepoRoot 'built_in_app_metadata.h'))
        $newerInputs = @($kernelSourceInputs | Where-Object { $_.LastWriteTimeUtc -gt $variantTime })
        if ($newerInputs.Count -gt 0) {
            throw "$ProofPhase cached proof kernels are stale; newer kernel input exists: $($newerInputs[0].FullName)"
        }
        $uiBytes = [System.IO.File]::ReadAllBytes($c148UiKernelPath)
        $notesOnlyBytes = [System.IO.File]::ReadAllBytes($c148NotesOnlyKernelPath)
        $uiText = [System.Text.Encoding]::ASCII.GetString($uiBytes)
        $notesOnlyText = [System.Text.Encoding]::ASCII.GetString($notesOnlyBytes)
        if ((Get-Hash $c148UiKernelPath) -eq (Get-Hash $c148NotesOnlyKernelPath) -or
            -not $uiText.Contains('[C147-PROOF]') -or
            -not $uiText.Contains('[C144-PROOF]') -or
            $uiText.Contains('[C148-NOTES-ONLY]') -or
            -not $notesOnlyText.Contains('[C148-NOTES-ONLY]') -or
            $notesOnlyText.Contains('[C144-PROOF]') -or
            $notesOnlyText.Contains('[C147-PROOF]')) {
            throw "$ProofPhase cached proof kernels do not contain distinct, expected launch variants."
        }
        $proofKernelPath = $c148UiKernelPath
        $c148KernelVariantsReused = $true
        Write-Host "[$ProofPhase] reused source-freshness-verified UI and Notes-only kernel variants."
    } else {
        if ($proofKernelPath -ne $kernelPath) {
            throw "$ProofPhase proof variants must be built from the canonical kernel source in this run."
        }
        Copy-Item -LiteralPath $proofKernelPath -Destination $c148UiKernelPath -Force
        $notesOnlyFlags = $kernelFlags + ' -DGXOS_NATIVEAOT_C148_NOTES_ONLY'
        Invoke-Checked 'mingw32-make' @('-C', (Join-Path $RepoRoot 'kernel'), 'ARCH=amd64', "EXTRA_CFLAGS=$notesOnlyFlags", '-B')
        Copy-Item -LiteralPath $kernelPath -Destination $c148NotesOnlyKernelPath -Force
        Copy-Item -LiteralPath $c148UiKernelPath -Destination $kernelPath -Force
        if ((Get-Hash $kernelPath) -ne (Get-Hash $c148UiKernelPath)) {
            throw "$ProofPhase could not restore the full Settings Center proof kernel after building Notes-only variant."
        }
    }
}

$inputs = [ordered]@{
    compositeElf = $compositeElf; compositeElfSha256 = Get-Hash $compositeElf
    kernel = $proofKernelPath; kernelSha256 = Get-Hash $proofKernelPath
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
$c148ProofKernelSha256 = $null
$script:C148NotesOnlyBoot = $false
$script:C148StartingFileHash = $null
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
    if ($isC148) {
        $authenticV1 = New-C148AuthenticV1Record
        $legacySnapshot = [ordered]@{ density = 1; showStatus = 0; advanced = 1; inputEnabled = 1; naturalScroll = 1; speed = 1; keyboardTips = 1; detail = 0; reportFormat = 0 }
        $defaultSnapshotV2 = [ordered]@{ density = 0; showStatus = 1; advanced = 0; inputEnabled = 1; naturalScroll = 0; speed = 1; keyboardTips = 1; detail = 0; reportFormat = 0; scrollLines = 3 }
        $c148ProofRoot = Join-Path $EvidenceRoot 'proof-media'
        New-Item -ItemType Directory -Force -Path $c148ProofRoot | Out-Null

        for ($sequence = 1; $sequence -le 3; $sequence++) {
            $sequenceRoot = Join-Path $EvidenceRoot ("sequence-{0:D2}" -f $sequence)
            $esp = Join-Path $sequenceRoot 'test-media\ESP'
            New-Item -ItemType Directory -Force -Path $sequenceRoot | Out-Null
            if (Test-Path -LiteralPath $esp) {
                $resolvedEsp = [System.IO.Path]::GetFullPath($esp)
                $resolvedEvidence = [System.IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
                if (-not $resolvedEsp.StartsWith($resolvedEvidence, [System.StringComparison]::OrdinalIgnoreCase)) {
                    throw "C148 sequence ESP cleanup target escaped evidence root: $resolvedEsp"
                }
                Remove-Item -LiteralPath $resolvedEsp -Recurse -Force
            }
            Stage-Esp $esp $c148UiKernelPath $bootloaderPath $stagingImage
            $settingsPath = Join-Path $esp 'GXSETT.BIN'
            [byte[]]$startingBytes = if ($sequence -eq 1) {
                $authenticV1
            } elseif ($sequence -eq 2) {
                if ($isC149) { New-C149V2Record $authenticV1 7 $false }
                else { New-C148V2Record $authenticV1 7 }
            } else {
                if ($isC149) { New-C149V2Record $authenticV1 5 $false }
                else { New-C148V2Record $authenticV1 5 }
            }
            [System.IO.File]::WriteAllBytes($settingsPath, $startingBytes)
            $startingFile = Get-C146PersistedSnapshot $settingsPath
            $startingMediaHash = Get-DirectoryHash $esp
            if (($sequence -eq 1 -and ($startingFile.version -ne 1 -or $startingFile.size -ne 25 -or $startingFile.sha256 -ne '952CA2183EE7DA2923EF76DBC121193C499750BDA7627D862D7F8B8BF5734FF5')) -or
                ($sequence -eq 2 -and ($startingFile.version -ne 2 -or $startingFile.scrollLines -ne 7 -or ($isC149 -and $startingFile.keyboardTips -ne 0))) -or
                ($sequence -eq 3 -and ($startingFile.version -ne 2 -or $startingFile.scrollLines -ne 5 -or ($isC149 -and $startingFile.keyboardTips -ne 0)))) {
                throw "C148 sequence $sequence starting proof record did not match its required authentic format and settings."
            }
            $roles = if ($isC150) {
                if ($sequence -eq 1) { @('v1-readonly', 'write', 'verify') }
                elseif ($sequence -eq 2) { @('discard', 'verify') }
                else { @('cancel-retry', 'verify') }
            } elseif ($sequence -eq 1) { @('v1-readonly', 'write', 'verify') }
            elseif ($sequence -eq 2) { @('verify') }
            else { @('reset', 'verify') }
            $expectedByRole = @{}
            if ($sequence -eq 1) { $expectedByRole['v1-readonly'] = @{ natural = 1; amount = 3; keyboardTips = 1 }; $expectedByRole['write'] = @{ natural = 1; amount = 3; keyboardTips = 0 }; $expectedByRole['verify'] = @{ natural = 1; amount = 5; keyboardTips = 0 } }
            elseif ($sequence -eq 2) {
                if ($isC150) { $expectedByRole['discard'] = @{ natural = 1; amount = 7; keyboardTips = 0 } }
                $expectedByRole['verify'] = @{ natural = 1; amount = 7; keyboardTips = if ($isC149) { 0 } else { 1 } }
            } elseif ($isC150) {
                $expectedByRole['cancel-retry'] = @{ natural = 1; amount = 5; keyboardTips = 1 }
                $expectedByRole['verify'] = @{ natural = 1; amount = 5; keyboardTips = 1 }
            } else {
                $expectedByRole['reset'] = @{ natural = 1; amount = 5; keyboardTips = if ($isC149) { 0 } else { 1 } }
                $expectedByRole['verify'] = @{ natural = 0; amount = 3; keyboardTips = 1 }
            }
            $bootEvidence = [ordered]@{}
            $postApplyFile = $null
            foreach ($roleIndex in 0..($roles.Count - 1)) {
                $role = $roles[$roleIndex]
                $script:C148NotesOnlyBoot = $role -eq 'v1-readonly' -or ($role -eq 'verify' -and $sequence -eq 1) -or
                    (-not $isC150 -and $role -eq 'verify' -and $sequence -eq 3)
                $bootKernel = if ($script:C148NotesOnlyBoot) { $c148NotesOnlyKernelPath } else { $c148UiKernelPath }
                Stage-Esp $esp $bootKernel $bootloaderPath $stagingImage
                $script:C148StartingFileHash = Get-Hash $settingsPath
                $bootRoot = Join-Path $sequenceRoot ("boot-{0}" -f $role)
                New-Item -ItemType Directory -Force -Path $bootRoot | Out-Null
                Start-Sleep -Seconds 2
                $serial = Join-Path $bootRoot 'serial.log'
                $stdout = Join-Path $bootRoot 'qemu.stdout.log'
                $stderr = Join-Path $bootRoot 'qemu.stderr.log'
                $monitorLog = Join-Path $bootRoot 'qemu-monitor.log'
                $monitorPort = 46800 + ($sequence * 10) + $roleIndex
                foreach ($bootOutput in @($serial, $stdout, $stderr, $monitorLog)) {
                    if (Test-Path -LiteralPath $bootOutput) { Remove-Item -LiteralPath $bootOutput -Force }
                }
                $boot = Invoke-C144Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf $role
                try { $classification = Assert-C120Serial $boot.serial }
                catch { $classification = [pscustomobject]@{ outcome = if ($boot.timedOut) { 'TIMEOUT' } else { 'FAIL' }; error = $_.Exception.Message } }
                if ($classification.outcome -eq 'PASS') {
                    $expected = $expectedByRole[$role]
                    if (-not ($isC150 -and $script:C148NotesOnlyBoot)) {
                        Assert-C148NotesStartupBehavior $boot.serial $expected.natural $expected.amount $role $script:C148NotesOnlyBoot
                    } elseif ($boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-NOTES-FIRST control=ListBox firstVisible=16 scrollLines=[1-8] before-settings-center=true result=PASS\r?$') {
                        throw "C150 $role Notes-only boot did not prove the shared runtime ListBox startup state."
                    }
                    if ($isC149) {
                        $expectedStartupTips = $expected.keyboardTips
                        if ($isC150 -and $role -eq 'write') {
                            $expectedStartupTips = 1
                        } elseif ($isC150 -and $role -eq 'cancel-retry') {
                            $expectedStartupTips = 0
                        }
                        Assert-C149NotesTipBehavior $boot.serial ($expectedStartupTips -eq 1) $role
                    }
                    if ($isC150 -and -not $script:C148NotesOnlyBoot) {
                        Assert-C150ReturnBoundary $boot.serial $role ($expected.keyboardTips -eq 1) $expected.natural $expected.amount
                    }
                }
                $bootResults.Add([pscustomobject]@{
                    boot = ("sequence-{0:D2}-{1}" -f $sequence, $role); sequence = $sequence; role = $role
                    outcome = $classification.outcome; classification = $classification
                    serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
                    stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath
                    monitorPath = $boot.monitorPath; qemuExitCode = $boot.qemuExitCode; timedOut = $boot.timedOut
                }) | Out-Null
                $bootEvidence[$role] = [ordered]@{
                    serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
                    outcome = $classification.outcome; settingsFileSha256BeforeBoot = $script:C148StartingFileHash
                    kernelSha256 = Get-Hash $bootKernel; notesOnly = $script:C148NotesOnlyBoot
                    naturalScroll = $expectedByRole[$role].natural; scrollLines = $expectedByRole[$role].amount
                    keyboardTips = $expectedByRole[$role].keyboardTips
                    noteWheelMarkers = @([regex]::Matches($boot.serial, '(?m)^\[C102-MANAGED-OUTPUT\] C148-NOTES-WHEEL .+result=PASS')).Count
                }
                Write-Host ("[C148] sequence={0} boot={1} outcome={2} serial={3}" -f $sequence, $role, $classification.outcome, $serial)
                if ($classification.outcome -ne 'PASS') { throw "C148 sequence $sequence role '$role' failed: $($classification.error)" }

                $current = Get-C146PersistedSnapshot $settingsPath
                if ($role -eq 'v1-readonly') {
                    if ($current.version -ne 1 -or $current.size -ne 25 -or
                        $current.sha256 -ne $startingFile.sha256) {
                        throw 'C148 Notes-only v1 boot changed the legacy settings file without Apply.'
                    }
                    $bootEvidence[$role].settingsFileVersionAfterBoot = $current.version
                    $bootEvidence[$role].settingsFileSizeAfterBoot = $current.size
                    $bootEvidence[$role].settingsFileSha256AfterBoot = $current.sha256
                } elseif ($role -eq 'write') {
                    if ($current.version -ne 2 -or $current.size -ne 26 -or
                        $current.scrollLines -ne 5 -or $current.naturalScroll -ne 1 -or
                        ($isC149 -and $current.keyboardTips -ne 0)) {
                        throw 'C148 v1 Apply did not produce the expected v2 custom record.'
                    }
                    foreach ($key in $legacySnapshot.Keys) {
                        $expectedLegacyValue = if ($isC149 -and $key -eq 'keyboardTips') { 0 } else { $legacySnapshot[$key] }
                        if ($current.$key -ne $expectedLegacyValue) { throw "C148 v1 migration changed legacy field '$key'." }
                    }
                    if ($boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-RUNTIME-APPLY oldLines=3 newLines=5 persistence=verified runtime=committed result=PASS' -or
                        $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-WORKING scrollLines=5 applied=3 runtime=3 persisted=3 dirty=true result=PASS') {
                        throw 'C148 Apply did not prove working isolation followed by a verified runtime commit.'
                    }
                    if ($isC149) {
                        Assert-C149AppliedConsumerBehavior $boot.serial $false 'sequence-01-Apply'
                        if ($boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-RETENTION settings-center=closed snapshot=unchanged result=PASS') {
                            throw 'C149 sequence-01 runtime snapshot changed when Settings Center closed.'
                        }
                        if (-not $isC150) { Assert-C149SingleSurfaceBoundary $boot.serial 'sequence-01-Apply' }
                    }
                    $postApplyFile = $current
                    $bootEvidence[$role].settingsFileVersionAfterApply = $current.version
                    $bootEvidence[$role].settingsFileSizeAfterApply = $current.size
                    $bootEvidence[$role].settingsFileSha256AfterApply = $current.sha256
                    $bootEvidence[$role].migratedNaturalScroll = $current.naturalScroll
                    $bootEvidence[$role].migratedScrollLines = $current.scrollLines
                } elseif ($role -eq 'reset') {
                    if ($current.version -ne 2 -or $current.size -ne 26 -or
                        $current.naturalScroll -ne 0 -or $current.scrollLines -ne 3 -or
                        $current.keyboardTips -ne 1) {
                        throw 'C148 Reset+Apply did not persist the canonical default v2 snapshot.'
                    }
                    foreach ($key in $defaultSnapshotV2.Keys) {
                        if ($current.$key -ne $defaultSnapshotV2[$key]) { throw "C148 Reset persisted nondefault '$key'." }
                    }
                    if ($boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-RESET scrollLines=working-default runtime=preserved persisted=preserved result=PASS' -or
                        $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-RUNTIME-APPLY oldLines=5 newLines=3 persistence=verified runtime=committed result=PASS') {
                        throw 'C148 Reset did not preserve custom runtime until explicit Apply.'
                    }
                    if ($isC149 -and ($boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-RESET keyboardTips=working-default runtime=preserved persisted=preserved result=PASS' -or
                        $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-APPLY oldKeyboardTips=0 newKeyboardTips=1 persistence=verified runtime=committed result=PASS')) {
                        throw 'C149 Reset changed keyboard-tip behavior before Apply or failed to publish the defaults after verified persistence.'
                    }
                    if ($isC149) {
                        Assert-C149AppliedConsumerBehavior $boot.serial $true 'sequence-03-Reset-Apply'
                        if ($boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-RETENTION settings-center=closed snapshot=unchanged result=PASS') {
                            throw 'C149 sequence-03 runtime snapshot changed when Settings Center closed.'
                        }
                        if (-not $isC150) { Assert-C149SingleSurfaceBoundary $boot.serial 'sequence-03-Reset-Apply' }
                    }
                    $postApplyFile = $current
                } elseif ($role -eq 'discard' -and $isC150) {
                    if ($current.sha256 -ne $script:C148StartingFileHash -or
                        $current.version -ne 2 -or $current.naturalScroll -ne 1 -or
                        $current.scrollLines -ne 7 -or $current.keyboardTips -ne 0) {
                        throw 'C150 Discard changed the persisted or runtime ShowKeyboardTips baseline.'
                    }
                } elseif ($role -eq 'cancel-retry' -and $isC150) {
                    if ($current.version -ne 2 -or $current.size -ne 26 -or
                        $current.naturalScroll -ne 1 -or $current.scrollLines -ne 5 -or
                        $current.keyboardTips -ne 1) {
                        throw 'C150 successful retry did not persist the keyboard-tip edit.'
                    }
                    $postApplyFile = $current
                } elseif ($role -eq 'verify') {
                    if (($current.sha256 -ne $script:C148StartingFileHash -and $sequence -eq 2) -or
                        ($isC150 -and $sequence -eq 3 -and $null -ne $postApplyFile -and
                            $current.sha256 -ne $postApplyFile.sha256)) {
                        throw 'C148 v2 hydration or clean close unexpectedly rewrote the sequence-2 file.'
                    }
                    $expectedFile = if ($sequence -eq 1) { @{ version = 2; amount = 5; natural = 1; keyboardTips = 0 } } elseif ($sequence -eq 2) { @{ version = 2; amount = 7; natural = 1; keyboardTips = if ($isC149) { 0 } else { 1 } } } elseif ($isC150) { @{ version = 2; amount = 5; natural = 1; keyboardTips = 1 } } else { @{ version = 2; amount = 3; natural = 0; keyboardTips = 1 } }
                    if ($current.version -ne $expectedFile.version -or $current.scrollLines -ne $expectedFile.amount -or $current.naturalScroll -ne $expectedFile.natural -or ($isC149 -and $current.keyboardTips -ne $expectedFile.keyboardTips)) {
                        throw "C148 sequence $sequence fresh Notes boot read unexpected persisted values."
                    }
                    if (-not $script:C148NotesOnlyBoot -and $boot.serial -notmatch "(?m)^\[C102-MANAGED-OUTPUT\] C148-SETTINGS-SYNC naturalScroll=$($expectedFile.natural) scrollLines=$($expectedFile.amount) dirty=false runtime=agrees result=PASS") {
                        throw 'C148 sequence-2 Settings Center did not hydrate both v2 settings cleanly.'
                    }
                    if ($isC149 -and -not $script:C148NotesOnlyBoot -and
                        $boot.serial -notmatch "(?m)^\[C102-MANAGED-OUTPUT\] C149-SETTINGS-SYNC keyboardTips=$($expectedFile.keyboardTips) dirty=false runtime=agrees result=PASS") {
                        throw 'C149 sequence-2 Settings Center did not hydrate ShowKeyboardTips cleanly.'
                    }
                }
                if ($script:C148NotesOnlyBoot -and $boot.serial -notmatch '(?m)^\[C148-NOTES-ONLY\] settings-center=not-launched result=PASS') {
                    throw "C148 sequence $sequence verification boot opened Settings Center."
                }
            }
            $finalFile = Get-C146PersistedSnapshot $settingsPath
            if ($isC150 -and $sequence -eq 3 -and
                ($finalFile.version -ne 2 -or $finalFile.size -ne 26 -or
                    $finalFile.naturalScroll -ne 1 -or $finalFile.scrollLines -ne 5 -or
                    $finalFile.keyboardTips -ne 1)) {
                throw 'C150 sequence 3 did not retain the verified retry Apply snapshot.'
            }
            $expectedFinal = if ($isC150 -and $sequence -eq 3) {
                @{ version = 2; amount = 5; natural = 1; keyboardTips = 1 }
            } elseif ($sequence -eq 3) { $defaultSnapshotV2 } else { $finalFile }
            if ($sequence -eq 1 -and ($startingFile.version -ne 1 -or $postApplyFile.version -ne 2)) { throw 'C148 sequence 1 failed v1-to-v2 migration.' }
            if ($sequence -eq 2 -and ($startingFile.version -ne 2 -or $finalFile.sha256 -ne $startingFile.sha256)) { throw 'C148 sequence 2 did not retain its seeded v2 settings.' }
            $c148PersistenceSequences.Add([ordered]@{
                sequence = $sequence; outcome = 'PASS'; testMediaPath = $esp
                startingTestMediaSha256 = $startingMediaHash
                startingSettingsFileVersion = $startingFile.version
                startingSettingsFileSize = $startingFile.size
                startingSettingsFileSha256 = $startingFile.sha256
                startingSnapshot = $startingFile
                postApplySettingsFile = $postApplyFile
                finalSettingsFile = $finalFile
                finalSettingsFileSha256 = $finalFile.sha256
                boots = $bootEvidence
            }) | Out-Null
        }

        $corruptRoot = Join-Path $EvidenceRoot 'startup-matrix\malformed-v2'
        $corruptEsp = Join-Path $corruptRoot 'test-media\ESP'
        New-Item -ItemType Directory -Force -Path $corruptRoot | Out-Null
        Stage-Esp $corruptEsp $c148NotesOnlyKernelPath $bootloaderPath $stagingImage
        $corruptV2 = New-C148MalformedV2Record (New-C148V2Record $authenticV1 5)
        $corruptPath = Join-Path $corruptEsp 'GXSETT.BIN'
        [System.IO.File]::WriteAllBytes($corruptPath, $corruptV2)
        $corruptHash = Get-Hash $corruptPath
        $script:C148StartingFileHash = $corruptHash; $script:C148NotesOnlyBoot = $true
        $corruptSerial = Join-Path $corruptRoot 'serial.log'
        $corruptStdout = Join-Path $corruptRoot 'qemu.stdout.log'
        $corruptStderr = Join-Path $corruptRoot 'qemu.stderr.log'
        $corruptMonitor = Join-Path $corruptRoot 'qemu-monitor.log'
        $corruptBoot = Invoke-C144Boot $corruptEsp $corruptSerial $corruptStdout $corruptStderr $corruptMonitor 46991 $qemu $ovmf 'corrupt-v2'
        $corruptClassification = Assert-C120Serial $corruptBoot.serial
        if ($isC150) { Assert-C150NotesOnlyStartupState $corruptBoot.serial 0 3 'malformed-v2' }
        else { Assert-C148NotesStartupBehavior $corruptBoot.serial 0 3 'malformed-v2' $true }
        if ($isC149) { Assert-C149NotesTipBehavior $corruptBoot.serial $true 'malformed-v2' }
        if ($corruptClassification.outcome -ne 'PASS' -or
            $corruptBoot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C148-RUNTIME-SETTING source=invalid naturalScroll=0 fileVersion=2 scrollLines=3 ready=true before-application=true result=PASS' -or
            ($isC149 -and $corruptBoot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-SETTING source=invalid keyboardTips=1 ready=true before-application=true result=PASS') -or
            (Get-Hash $corruptPath) -ne $corruptHash) {
            throw 'C148 malformed v2 did not fall back to the default runtime without rewriting the bad file.'
        }
        $bootResults.Add([pscustomobject]@{ boot = 'startup-matrix-malformed-v2'; sequence = 0; role = 'malformed-v2'; outcome = 'PASS'; classification = $corruptClassification; serialPath = $corruptBoot.serialPath; serialSha256 = $corruptBoot.serialSha256; stdoutPath = $corruptBoot.stdoutPath; stderrPath = $corruptBoot.stderrPath; monitorPath = $corruptBoot.monitorPath; qemuExitCode = $corruptBoot.qemuExitCode; timedOut = $corruptBoot.timedOut }) | Out-Null
        $c149StartupMatrix = [System.Collections.Generic.List[object]]::new()
        if ($isC149) {
            $c149StartupMatrix.Add([ordered]@{ fixture = 'malformed-v2'; outcome = 'PASS'; fileSha256 = $corruptHash; keyboardTips = 1; runtimeSource = 'invalid'; serialSha256 = $corruptBoot.serialSha256 }) | Out-Null
            $fixtureRoot = Join-Path $EvidenceRoot 'startup-matrix\c148-v2-compat'
            $fixtureEsp = Join-Path $fixtureRoot 'test-media\ESP'
            New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null
            Stage-Esp $fixtureEsp $c148NotesOnlyKernelPath $bootloaderPath $stagingImage
            $fixturePath = Join-Path $fixtureEsp 'GXSETT.BIN'
            [byte[]]$fixtureBytes = New-C149C148EraV2Fixture
            [System.IO.File]::WriteAllBytes($fixturePath, $fixtureBytes)
            $fixtureHash = Get-Hash $fixturePath
            $script:C148StartingFileHash = $fixtureHash; $script:C148NotesOnlyBoot = $true
            $fixtureBoot = Invoke-C144Boot $fixtureEsp (Join-Path $fixtureRoot 'serial.log') (Join-Path $fixtureRoot 'qemu.stdout.log') (Join-Path $fixtureRoot 'qemu.stderr.log') (Join-Path $fixtureRoot 'qemu-monitor.log') 46992 $qemu $ovmf 'c149-c148-v2-compat'
            $fixtureClassification = Assert-C120Serial $fixtureBoot.serial
            if ($isC150) { Assert-C150NotesOnlyStartupState $fixtureBoot.serial 1 7 'C148-v2-compat' }
            else { Assert-C148NotesStartupBehavior $fixtureBoot.serial 1 7 'C148-v2-compat' $true }
            Assert-C149NotesTipBehavior $fixtureBoot.serial $true 'C148-v2-compat'
            if ($fixtureClassification.outcome -ne 'PASS' -or
                $fixtureBoot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-SETTING source=file keyboardTips=1 ready=true before-application=true result=PASS' -or
                (Get-Hash $fixturePath) -ne $fixtureHash) {
                throw 'C149 did not load the exact C148 v2 fixture unchanged with its serialized keyboard-tip value.'
            }
            $c149StartupMatrix.Add([ordered]@{ fixture = 'c148-v2'; outcome = 'PASS'; fileSha256 = $fixtureHash; keyboardTips = 1; serialSha256 = $fixtureBoot.serialSha256 }) | Out-Null
            $bootResults.Add([pscustomobject]@{ boot = 'startup-matrix-c148-v2-compat'; sequence = 0; role = 'c148-v2-compat'; outcome = 'PASS'; classification = $fixtureClassification; serialPath = $fixtureBoot.serialPath; serialSha256 = $fixtureBoot.serialSha256; stdoutPath = $fixtureBoot.stdoutPath; stderrPath = $fixtureBoot.stderrPath; monitorPath = $fixtureBoot.monitorPath; qemuExitCode = $fixtureBoot.qemuExitCode; timedOut = $fixtureBoot.timedOut }) | Out-Null

            $futureRoot = Join-Path $EvidenceRoot 'startup-matrix\unsupported-v3'
            $futureEsp = Join-Path $futureRoot 'test-media\ESP'
            New-Item -ItemType Directory -Force -Path $futureRoot | Out-Null
            Stage-Esp $futureEsp $c148NotesOnlyKernelPath $bootloaderPath $stagingImage
            $futurePath = Join-Path $futureEsp 'GXSETT.BIN'
            [byte[]]$futureBytes = New-C149UnsupportedV3Record $fixtureBytes
            [System.IO.File]::WriteAllBytes($futurePath, $futureBytes)
            $futureHash = Get-Hash $futurePath
            $script:C148StartingFileHash = $futureHash; $script:C148NotesOnlyBoot = $true
            $futureBoot = Invoke-C144Boot $futureEsp (Join-Path $futureRoot 'serial.log') (Join-Path $futureRoot 'qemu.stdout.log') (Join-Path $futureRoot 'qemu.stderr.log') (Join-Path $futureRoot 'qemu-monitor.log') 46993 $qemu $ovmf 'c149-unsupported-v3'
            $futureClassification = Assert-C120Serial $futureBoot.serial
            if ($isC150) { Assert-C150NotesOnlyStartupState $futureBoot.serial 0 3 'unsupported-v3' }
            else { Assert-C148NotesStartupBehavior $futureBoot.serial 0 3 'unsupported-v3' $true }
            Assert-C149NotesTipBehavior $futureBoot.serial $true 'unsupported-v3'
            if ($futureClassification.outcome -ne 'PASS' -or
                $futureBoot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C149-RUNTIME-SETTING source=invalid keyboardTips=1 ready=true before-application=true result=PASS' -or
                (Get-Hash $futurePath) -ne $futureHash) {
                throw 'C149 unsupported v3 did not fall back to canonical default behavior without rewriting the record.'
            }
            $c149StartupMatrix.Add([ordered]@{ fixture = 'unsupported-v3'; outcome = 'PASS'; fileSha256 = $futureHash; keyboardTips = 1; serialSha256 = $futureBoot.serialSha256 }) | Out-Null
            $bootResults.Add([pscustomobject]@{ boot = 'startup-matrix-unsupported-v3'; sequence = 0; role = 'unsupported-v3'; outcome = 'PASS'; classification = $futureClassification; serialPath = $futureBoot.serialPath; serialSha256 = $futureBoot.serialSha256; stdoutPath = $futureBoot.stdoutPath; stderrPath = $futureBoot.stderrPath; monitorPath = $futureBoot.monitorPath; qemuExitCode = $futureBoot.qemuExitCode; timedOut = $futureBoot.timedOut }) | Out-Null
        }
        $c148ProofKernelSha256 = Get-Hash $c148UiKernelPath
        Copy-Item -LiteralPath $c146CanonicalKernelBackup -Destination $kernelPath -Force
        Copy-Item -LiteralPath $c146EspKernelBackup -Destination (Join-Path $RepoRoot 'ESP\kernel.elf') -Force
        if ((Get-Hash $kernelPath) -ne $c146CanonicalKernelSha256 -or
            (Get-Hash (Join-Path $RepoRoot 'ESP\kernel.elf')) -ne $c146EspKernelSha256 -or
            (Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')) -ne $c146ProtectedRamdiskSha256) {
            throw 'C148 could not restore canonical kernels and preserve the protected ramdisk.'
        }
        for ($ordinaryBoot = 1; $ordinaryBoot -le 3; $ordinaryBoot++) {
            $ordinaryRoot = Join-Path $EvidenceRoot ("ordinary-boot-{0:D2}" -f $ordinaryBoot)
            $ordinaryEsp = Join-Path $ordinaryRoot 'ESP'
            New-Item -ItemType Directory -Force -Path $ordinaryRoot | Out-Null
            Stage-Esp $ordinaryEsp $kernelPath $bootloaderPath (Join-Path $RepoRoot 'ESP\ramdisk.img')
            $serial = Join-Path $ordinaryRoot 'serial.log'
            $stdout = Join-Path $ordinaryRoot 'qemu.stdout.log'
            $stderr = Join-Path $ordinaryRoot 'qemu.stderr.log'
            $ordinary = Invoke-C146OrdinaryBoot $ordinaryEsp $serial $stdout $stderr $ordinaryBoot $qemu $ovmf
            if ($ordinary.serial -match 'C150-|C149-|C148-|C147-RUNTIME-SETTING') { throw "C150/C149/C148 proof marker leaked into ordinary boot $ordinaryBoot." }
            $c146OrdinaryBoots.Add([ordered]@{ boot = $ordinaryBoot; outcome = 'PASS'; serialPath = $ordinary.serialPath; serialSha256 = $ordinary.serialSha256; kernelSha256 = Get-Hash (Join-Path $ordinaryEsp 'kernel.elf'); ramdiskSha256 = Get-Hash (Join-Path $ordinaryEsp 'ramdisk.img') }) | Out-Null
            Write-Host ("[C148] ordinary-boot={0} outcome=PASS serial={1}" -f $ordinaryBoot, $ordinary.serialPath)
        }
        if ((Get-Hash $kernelPath) -ne $c146CanonicalKernelSha256 -or
            (Get-Hash (Join-Path $RepoRoot 'ESP\kernel.elf')) -ne $c146EspKernelSha256 -or
            (Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')) -ne $c146ProtectedRamdiskSha256) {
            throw 'C148 final ordinary artifacts differ from their original protected hashes.'
        }
    } elseif ($isC146) {
        $customSnapshot = [ordered]@{ density = 1; showStatus = 0; advanced = 1; inputEnabled = 1; naturalScroll = 1; speed = 1; keyboardTips = 1; detail = 0; reportFormat = 0 }
        $defaultSnapshot = [ordered]@{ density = 0; showStatus = 1; advanced = 0; inputEnabled = 1; naturalScroll = 0; speed = 1; keyboardTips = 1; detail = 0; reportFormat = 0 }
        for ($sequence = 1; $sequence -le 3; $sequence++) {
            $sequenceRoot = Join-Path $EvidenceRoot ("sequence-{0:D2}" -f $sequence)
            $esp = Join-Path $sequenceRoot 'test-media\ESP'
            New-Item -ItemType Directory -Force -Path $sequenceRoot | Out-Null
            if (Test-Path -LiteralPath $esp) {
                $resolvedEsp = [System.IO.Path]::GetFullPath($esp)
                $resolvedEvidence = [System.IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
                if (-not $resolvedEsp.StartsWith($resolvedEvidence, [System.StringComparison]::OrdinalIgnoreCase)) {
                    throw "Sequence ESP cleanup target escaped the evidence root: $resolvedEsp"
                }
                Remove-Item -LiteralPath $resolvedEsp -Recurse -Force
            }
            Stage-Esp $esp $proofKernelPath $bootloaderPath $stagingImage
            $startingMediaHash = Get-DirectoryHash $esp
            if (Test-Path -LiteralPath (Join-Path $esp 'GXSETT.BIN')) { throw "C146 sequence $sequence did not start with fresh settings media." }
            $roles = if ($isC147) {
                if ($sequence -eq 1) { @('write', 'discard', 'cancel', 'verify', 'reset', 'defaults') }
                else { @('write', 'discard', 'cancel', 'verify') }
            } elseif ($sequence -eq 1) { @('write', 'discard', 'verify', 'reset', 'defaults') }
            else { @('write', 'discard', 'verify') }
            $bootEvidence = [ordered]@{}
            $postWriteMediaHash = $null
            $persistedAfterWrite = $null
            $finalFileHash = $null
            foreach ($roleIndex in 0..($roles.Count - 1)) {
                $role = $roles[$roleIndex]
                $bootRoot = Join-Path $sequenceRoot ("boot-{0}" -f $role)
                New-Item -ItemType Directory -Force -Path $bootRoot | Out-Null
                Start-Sleep -Seconds 2
                $serial = Join-Path $bootRoot 'serial.log'
                $stdout = Join-Path $bootRoot 'qemu.stdout.log'
                $stderr = Join-Path $bootRoot 'qemu.stderr.log'
                $monitorLog = Join-Path $bootRoot 'qemu-monitor.log'
                $monitorPort = 46300 + ($sequence * 10) + $roleIndex + 1
                foreach ($bootOutput in @($serial, $stdout, $stderr, $monitorLog)) {
                    if (Test-Path -LiteralPath $bootOutput) {
                        Remove-Item -LiteralPath $bootOutput -Force
                    }
                }
                $boot = Invoke-C144Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf $role
                try { $classification = Assert-C120Serial $boot.serial }
                catch {
                    $classification = [pscustomobject]@{
                        outcome = if ($boot.timedOut) { 'TIMEOUT' } else { 'FAIL' }
                        error = $_.Exception.Message
                    }
                }
                $bootResults.Add([pscustomobject]@{
                    boot = ("sequence-{0:D2}-{1}" -f $sequence, $role); sequence = $sequence; role = $role
                    outcome = $classification.outcome; classification = $classification
                    serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256
                    stdoutPath = $boot.stdoutPath; stderrPath = $boot.stderrPath
                    monitorPath = $boot.monitorPath; qemuExitCode = $boot.qemuExitCode; timedOut = $boot.timedOut
                }) | Out-Null
                $bootEvidence[$role] = [ordered]@{ serialPath = $boot.serialPath; serialSha256 = $boot.serialSha256; outcome = $classification.outcome }
                Write-Host ("[{0}] sequence={1} boot={2} outcome={3} serial={4}" -f $ProofPhase, $sequence, $role, $classification.outcome, $serial)
                if ($classification.outcome -ne 'PASS') { throw "$ProofPhase sequence $sequence boot role '$role' failed: $($classification.error)" }
                if ($isC147) {
                    $expectedNatural = if ($role -in @('discard', 'cancel', 'verify', 'reset')) { 1 } else { 0 }
                    Assert-C147NotesStartupBehavior $boot.serial $expectedNatural $role
                }

                $settingsPath = Join-Path $esp 'GXSETT.BIN'
                if ($role -eq 'write') {
                    if ($boot.serial -notmatch '(?m)^\[C146-VFS-STORE\] operation=write virtual=/system/apps/GXSETT\.BIN backing=/GXSETT\.BIN result=PASS' -or
                        -not (Test-C146SnapshotOutput $boot.serial `
                            'C146-SAVE result=PASS write=verified readback=PASS dirty=false' `
                            'C146-VALUES density=1 showStatus=0 advanced=1 inputEnabled=1 naturalScroll=1 speed=1 keyboardTips=1 detail=0 reportFormat=0')) {
                        throw "C146 sequence $sequence Boot A lacks physical Apply/read-back VFS evidence."
                    }
                    if ($isC147 -and ($boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-RUNTIME-SETTING source=missing naturalScroll=0 .*before-application=true result=PASS' -or
                        $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-RUNTIME-APPLY source=SettingsCenter oldNatural=0 newNatural=1 persistence=verified runtime=committed result=PASS' -or
                        $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-CONSUMER-CASE name=natural result=PASS' -or
                        $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-CONSUMER-CASE name=immediate-apply result=PASS')) {
                        throw "C147 sequence $sequence failed the default-to-natural immediate Apply behavior proof."
                    }
                    $persistedAfterWrite = Get-C146PersistedSnapshot $settingsPath
                    foreach ($key in $customSnapshot.Keys) {
                        if ($persistedAfterWrite.$key -ne $customSnapshot[$key]) { throw "C146 sequence $sequence persisted field '$key' did not match the physical edits." }
                    }
                    $postWriteMediaHash = Get-DirectoryHash $esp
                    if ($startingMediaHash -eq $postWriteMediaHash) { throw "C146 sequence $sequence test media hash did not change after Apply." }
                    $finalFileHash = $persistedAfterWrite.sha256
                } elseif ($role -in @('discard', 'cancel', 'verify', 'reset')) {
                    if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) { throw "C146 sequence $sequence settings file disappeared before role '$role'." }
                    $currentFile = Get-C146PersistedSnapshot $settingsPath
                    if ($role -in @('discard', 'verify') -and $currentFile.sha256 -ne $persistedAfterWrite.sha256) {
                        throw "C146 sequence $sequence role '$role' changed the durable settings record without an Apply."
                    }
                } elseif ($role -eq 'defaults') {
                    $currentFile = Get-C146PersistedSnapshot $settingsPath
                    foreach ($key in $defaultSnapshot.Keys) {
                        if ($currentFile.$key -ne $defaultSnapshot[$key]) { throw "C146 sequence $sequence Defaults reboot field '$key' did not match canonical defaults." }
                    }
                    if (-not (Test-C146SnapshotOutput $boot.serial `
                        'C146-LOAD source=file result=PASS working=applied persisted=loaded dirty=false' `
                        'C146-VALUES density=0 showStatus=1 advanced=0 inputEnabled=1 naturalScroll=0 speed=1 keyboardTips=1 detail=0 reportFormat=0')) {
                        throw "C146 sequence $sequence reboot did not hydrate the Reset/Apply defaults snapshot."
                    }
                    $finalFileHash = $currentFile.sha256
                }
                if ($role -eq 'discard' -and $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C145-UNSAVED result=Discard applied=preserved closed=true result=PASS') {
                    throw "C146 sequence $sequence did not exercise dirty-close Discard."
                }
                if ($isC147 -and $role -eq 'discard' -and (
                    $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-DISCARD runtime=preserved persisted=preserved closed=true result=PASS')) {
                    throw "C147 sequence $sequence Discard changed the active runtime or persisted setting."
                }
                if ($isC147 -and $role -eq 'cancel' -and (
                    $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-CANCEL runtime=preserved persisted=preserved parent=open result=PASS')) {
                    throw "C147 sequence $sequence Cancel changed the active runtime or persisted setting."
                }
                if ($isC147 -and $role -eq 'verify' -and (
                    $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-RUNTIME-SETTING source=file naturalScroll=1 .*before-application=true result=PASS')) {
                    throw "C147 sequence $sequence fresh boot did not prove Natural Scroll before Settings Center opened."
                }
                if ($role -eq 'reset' -and ($boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults dirty=updated viewport=valid focus=restored result=PASS' -or
                    -not (Test-C146SnapshotOutput $boot.serial `
                        'C146-SAVE result=PASS write=verified readback=PASS dirty=false' `
                        'C146-VALUES density=0 showStatus=1 advanced=0 inputEnabled=1 naturalScroll=0 speed=1 keyboardTips=1 detail=0 reportFormat=0'))) {
                    throw "C146 sequence $sequence did not prove Reset changes working state and only Apply persists defaults."
                }
                if ($isC147 -and $role -eq 'reset' -and (
                    $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-RESET working-only=true runtime=preserved persisted=preserved result=PASS' -or
                    $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-RUNTIME-APPLY source=SettingsCenter oldNatural=1 newNatural=0 persistence=verified runtime=committed result=PASS' -or
                    $boot.serial -notmatch '(?m)^\[C102-MANAGED-OUTPUT\] C147-CONSUMER-CASE name=immediate-apply result=PASS')) {
                    throw "C147 sequence $sequence Reset did not preserve runtime until Apply and then restore default behavior."
                }
            }
            $finalSettings = Get-C146PersistedSnapshot (Join-Path $esp 'GXSETT.BIN')
            $expectedFinal = if ($sequence -eq 1) { $defaultSnapshot } else { $customSnapshot }
            foreach ($key in $expectedFinal.Keys) {
                if ($finalSettings.$key -ne $expectedFinal[$key]) { throw "C146 sequence $sequence final durable field '$key' is incorrect." }
            }
            $c146PersistenceSequences.Add([ordered]@{
                sequence = $sequence; outcome = 'PASS'; testMediaPath = $esp
                startingTestMediaSha256 = $startingMediaHash; postWriteTestMediaSha256 = $postWriteMediaHash
                settingsFile = '/GXSETT.BIN'; settingsFileSize = $finalSettings.size
                formatVersion = $finalSettings.version; writtenSnapshot = $customSnapshot
                loadedSnapshotAfterWrite = $customSnapshot
                finalPersistedSnapshot = if ($sequence -eq 1) { $defaultSnapshot } else { $customSnapshot }
                finalSettingsFileSha256 = $finalSettings.sha256; boots = $bootEvidence
            }) | Out-Null
        }

        if ($isC147) {
            $startupMatrixRoot = Join-Path $EvidenceRoot "startup-matrix"
            $validV1Path = Join-Path $EvidenceRoot "sequence-01\test-media\ESP\GXSETT.BIN"
            $futureVersionBytes = New-C147UnsupportedVersionRecord $validV1Path
            $startupCases = @(
                [pscustomobject]@{ name = "corrupt"; bytes = [System.Text.Encoding]::ASCII.GetBytes("BAD"); source = "invalid"; fileSha256 = $null },
                [pscustomobject]@{ name = "future-version"; bytes = [byte[]]$futureVersionBytes; source = "invalid"; fileSha256 = $null })
            $caseIndex = 0
            foreach ($startupCase in $startupCases) {
                $caseRoot = Join-Path $startupMatrixRoot $startupCase.name
                $caseEsp = Join-Path $caseRoot "test-media\ESP"
                New-Item -ItemType Directory -Force -Path $caseRoot | Out-Null
                Stage-Esp $caseEsp $proofKernelPath $bootloaderPath $stagingImage
                $settingsRecord = Join-Path $caseEsp "GXSETT.BIN"
                [System.IO.File]::WriteAllBytes($settingsRecord, $startupCase.bytes)
                $caseMediaHash = Get-DirectoryHash $caseEsp
                $caseRecordHash = Get-Hash $settingsRecord
                $serial = Join-Path $caseRoot "serial.log"
                $stdout = Join-Path $caseRoot "qemu.stdout.log"
                $stderr = Join-Path $caseRoot "qemu.stderr.log"
                $monitorLog = Join-Path $caseRoot "qemu-monitor.log"
                $monitorPort = 46400 + $caseIndex
                Start-Sleep -Seconds 2
                $startupBoot = Invoke-C144Boot $caseEsp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf $startupCase.name
                $startupClassification = Assert-C120Serial $startupBoot.serial
                $expectedRuntime = '(?m)^\[C102-MANAGED-OUTPUT\] C147-RUNTIME-SETTING source=invalid naturalScroll=0 ready=true before-application=true result=PASS'
                $expectedDefaultEffect = '(?m)^\[C102-MANAGED-OUTPUT\] C147-CONSUMER app=ManagedNotes control=ListBox natural=0 delta=-1 before=0 after=3 changed=true result=PASS'
                if ($startupClassification.outcome -ne "PASS" -or
                    $startupBoot.serial -notmatch $expectedRuntime -or
                    $startupBoot.serial -notmatch $expectedDefaultEffect) {
                    throw "C147 $($startupCase.name) startup did not fall back to default Natural Scroll behavior."
                }
                Assert-C147NotesStartupBehavior $startupBoot.serial 0 $startupCase.name
                if ($startupCase.name -eq "future-version" -and
                    ($startupCase.bytes.Length -ne 25 -or $startupCase.bytes[4] -ne 2 -or
                     (Get-C147Crc32 ([byte[]]$startupCase.bytes[0..20])) -ne
                        [System.BitConverter]::ToUInt32($startupCase.bytes, 21))) {
                    throw "C147 future-version fixture did not retain a valid v2-header checksum."
                }
                $bootResults.Add([pscustomobject]@{
                    boot = "startup-matrix-$($startupCase.name)"; sequence = 0; role = $startupCase.name
                    outcome = $startupClassification.outcome; classification = $startupClassification
                    serialPath = $startupBoot.serialPath; serialSha256 = $startupBoot.serialSha256
                    stdoutPath = $startupBoot.stdoutPath; stderrPath = $startupBoot.stderrPath
                    monitorPath = $startupBoot.monitorPath; qemuExitCode = $startupBoot.qemuExitCode; timedOut = $startupBoot.timedOut
                }) | Out-Null
                $c147StartupProofs.Add([ordered]@{
                    case = $startupCase.name; outcome = "PASS"; runtimeSource = "invalid"
                    effectiveNaturalScroll = 0; fileSha256 = $caseRecordHash
                    mediaSha256 = $caseMediaHash; serialPath = $startupBoot.serialPath
                    serialSha256 = $startupBoot.serialSha256; userAppEffectBeforeSettingsCenter = $true
                    settingsCenterModalAfterStartup = $startupBoot.serial.Contains("C146-LOAD source=invalid recovery=defaults")
                }) | Out-Null
                Write-Host ("[C147] startup-case={0} outcome=PASS serial={1}" -f $startupCase.name, $startupBoot.serialPath)
                $caseIndex++
            }
        }

        $c146ProofKernelSha256 = Get-Hash $proofKernelPath
        Copy-Item -LiteralPath $c146CanonicalKernelBackup -Destination $kernelPath -Force
        Copy-Item -LiteralPath $c146EspKernelBackup -Destination (Join-Path $RepoRoot 'ESP\kernel.elf') -Force
        if ((Get-Hash $kernelPath) -ne $c146CanonicalKernelSha256 -or
            (Get-Hash (Join-Path $RepoRoot 'ESP\kernel.elf')) -ne $c146EspKernelSha256) {
            throw 'C146 could not restore both canonical ordinary kernels byte-for-byte.'
        }
        if ((Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')) -ne $c146ProtectedRamdiskSha256) {
            throw 'C146 protected canonical ESP/ramdisk.img changed during persistence proof.'
        }

        for ($ordinaryBoot = 1; $ordinaryBoot -le 3; $ordinaryBoot++) {
            $ordinaryRoot = Join-Path $EvidenceRoot ("ordinary-boot-{0:D2}" -f $ordinaryBoot)
            $ordinaryEsp = Join-Path $ordinaryRoot 'ESP'
            New-Item -ItemType Directory -Force -Path $ordinaryRoot | Out-Null
            Stage-Esp $ordinaryEsp $kernelPath $bootloaderPath (Join-Path $RepoRoot 'ESP\ramdisk.img')
            $serial = Join-Path $ordinaryRoot 'serial.log'
            $stdout = Join-Path $ordinaryRoot 'qemu.stdout.log'
            $stderr = Join-Path $ordinaryRoot 'qemu.stderr.log'
            $ordinary = Invoke-C146OrdinaryBoot $ordinaryEsp $serial $stdout $stderr $ordinaryBoot $qemu $ovmf
            $c146OrdinaryBoots.Add([ordered]@{
                boot = $ordinaryBoot; outcome = 'PASS'; serialPath = $ordinary.serialPath
                serialSha256 = $ordinary.serialSha256; kernelSha256 = Get-Hash (Join-Path $ordinaryEsp 'kernel.elf')
                ramdiskSha256 = Get-Hash (Join-Path $ordinaryEsp 'ramdisk.img')
            }) | Out-Null
            Write-Host ("[C146] ordinary-boot={0} outcome=PASS serial={1}" -f $ordinaryBoot, $ordinary.serialPath)
        }
        if ((Get-Hash $kernelPath) -ne $c146CanonicalKernelSha256 -or
            (Get-Hash (Join-Path $RepoRoot 'ESP\kernel.elf')) -ne $c146EspKernelSha256 -or
            (Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')) -ne $c146ProtectedRamdiskSha256) {
            throw 'C146 final canonical kernel or protected ramdisk hash differs from the pre-proof baseline.'
        }
    } else {
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
            $boot = if ($isC145) {
                Invoke-C144Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
            } elseif ($isC144) {
                Invoke-C144Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
            } elseif ($isC143) {
                Invoke-C143Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
            } elseif ($isC142) {
                Invoke-C142Boot $esp $serial $stdout $stderr $monitorLog $monitorPort $qemu $ovmf
            } elseif ($isC141) {
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
            if ($index -lt $FreshBootCount) { Start-Sleep -Seconds 2 }
        }
    }
}

$primarySerialRelative = if ($isC146) { "sequence-01\boot-write\serial.log" } else { "boot-01\serial.log" }
$evidenceSerial = if ($bootResults.Count -gt 0) {
    Get-Content -LiteralPath (Join-Path $EvidenceRoot $primarySerialRelative)
} else { @("QEMU not executed; build-only evidence.") }
$evidenceSerial | Where-Object { $_ -match '^\[(?:C148|C147|C146|C145|C144|C143|C142|C141|C140|C138|C137|C136|C135|C134|C133|C132|C131|C130|C129|C128|C127|C126|C125|C124|C123|C122|C121|C120|C119|C118|C117|C116|C115)-' } |
    Set-Content -LiteralPath (Join-Path $EvidenceRoot "managed-control-host-output.txt") -Encoding ASCII
$evidenceSerial | Where-Object { $_ -match '^\[(?:C148|C147|C146|C145|C144|C143|C142|C141|C140|C138|C137|C136|C135|C134|C133|C132|C131|C130|C129|C128|C127|C126|C124|C123|C122|C121|C120)-' } |
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
    "samples\managed\HostLogProof\GuideXos\GuideXosApplication.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioButton.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioGroup.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioButtonTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioGroupTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioButtonHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosControlHostTests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosGroupBoxC143Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSettingsCenterC144Tests.cs",
    "samples\managed\HostLogProof\GuideXos\ManagedSettingsStoreC146Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSettingsCenterC146Tests.cs",
    "samples\managed\HostLogProof\Applications\ManagedSettingsStore.cs",
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
    "samples\managed\HostLogProof\GuideXos\GuideXosPanel.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosButton.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosComboBox.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRadioGroup.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosVerticalStack.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosScrollView.cs",
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
    "samples\managed\HostLogProof\GuideXos\GuideXosSettingsV2C148Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSettingsRuntimeC149Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosRuntimeSettingsC147Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosVerticalStackMember.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosVerticalStack.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosVerticalStackC141Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosVerticalStackC142Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosGroupBoxC143Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSurface.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosMouseWheelC137Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosFilePicker.cs",
    "samples\managed\HostLogProof\Applications\ManagedNotes.cs",
    "samples\managed\HostLogProof\Applications\ManagedScrollViewDemo.cs",
    "samples\managed\HostLogProof\Applications\ManagedVerticalStackDemo.cs",
    "samples\managed\HostLogProof\Applications\ManagedGroupBoxDemo.cs",
    "samples\managed\HostLogProof\Applications\ManagedSettingsCenter.cs",
    "samples\managed\HostLogProof\Applications\ManagedSettingsRuntime.cs",
    "samples\managed\HostLogProof\Applications\ManagedSettingsStore.cs",
    "samples\managed\HostLogProof\Applications\ManagedGroupBoxDemo.cs",
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
    "scripts\dotnet\run-c144-managed-settings-center.ps1",
    "scripts\dotnet\run-c145-managed-modal-dialog.ps1",
    "scripts\dotnet\run-c146-settings-persistence.ps1",
    "scripts\dotnet\run-c148-settings-v2-scroll-amount.ps1",
    "scripts\dotnet\run-c149-second-runtime-setting.ps1",
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
    "docs\dotnet\NATIVEAOT_C141_MANAGED_VERTICAL_STACK_LAYOUT.md",
    "docs\dotnet\NATIVEAOT_C142_STACK_MARGIN_ALIGNMENT.md",
    "docs\dotnet\NATIVEAOT_C143_MANAGED_GROUPBOX.md",
    "docs\dotnet\NATIVEAOT_C144_MANAGED_SETTINGS_CENTER.md",
    "docs\dotnet\NATIVEAOT_C145_MANAGED_MODAL_DIALOG.md",
    "docs\dotnet\NATIVEAOT_C146_SETTINGS_PERSISTENCE.md",
    "docs\dotnet\NATIVEAOT_C148_SETTINGS_V2_SCROLL_AMOUNT.md",
    "docs\dotnet\NATIVEAOT_C149_SECOND_RUNTIME_SETTING.md",
    "samples\managed\HostLogProof\GuideXos\GuideXosDialog.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosMessageBox.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosDialogC145Tests.cs",
    "samples\managed\HostLogProof\GuideXos\GuideXosSettingsCenterC145Tests.cs")
$sourceHashes = [ordered]@{}
foreach ($sourceFile in $sourceFiles) { $sourceHashes[$sourceFile] = Get-Hash (Join-Path $RepoRoot $sourceFile) }

$repoHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
$repoSubject = (& git -C $RepoRoot log -1 --format=%s).Trim()
$repoBranch = (& git -C $RepoRoot branch --show-current).Trim()
$repoUpstream = (& git -C $RepoRoot rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null).Trim()
$aheadBehind = if ($repoUpstream) { (& git -C $RepoRoot rev-list --left-right --count "HEAD...$repoUpstream").Trim() } else { "" }
$c143FinalOffset = if ($isC143 -and $bootResults.Count -gt 0) {
    Get-C143CurrentOffset (Get-Content -LiteralPath (Join-Path $EvidenceRoot "boot-01\serial.log") -Raw)
} else { $null }
$c144InitialGeometry = $null
$c144FinalGeometry = $null
$c144Serial = ""
if (($isC144 -or $isC145) -and $bootResults.Count -gt 0) {
    $c144Serial = Get-Content -LiteralPath (Join-Path $EvidenceRoot $primarySerialRelative) -Raw
    $c144InitialGeometry = Get-C144Geometry $c144Serial "initial"
    if ($isC144) { $c144FinalGeometry = Get-C144Geometry $c144Serial "final" }
}
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
    controlHost = [ordered]@{ api = "GuideXosControlHost"; capacity = 8; pickerCapacity = 2; tests = if($isC142){"C142 metadata 20; spacing 12; horizontal 12; ScrollView 14; popup/control 10; total 68; C141 56 retained"}elseif($isC141){"C141 production stack host: registration 2; direct ScrollView members 8; C141 focused layout/visibility/ScrollView/focus/popup suite total 56"}elseif($isC137){"C137 wheel transport: 46 focused API/host cases; TextArea/ListBox routing, capture, modal, lifecycle, and burst coverage"}elseif($isC135){"C135 popup menu: 40 API cases, 40 host cases, shared one-owner capture, and retained C134 routing"}elseif($isC134){"C134 transient-capture contract: 30 focused host cases plus production ComboBox routing"}elseif($isC133){"C133 ComboBox API, transient capture, lifecycle, Panel, modal, and host routing"}elseif($isC132){"C132 RadioButton API, group coordination, callbacks, Panel, modal, and host routing"}elseif($isC131){"C131 checkbox API, callback, Panel, lifecycle, modal, and host routing"}elseif($isC129){"C129 direct Shift/Tab transport fixture plus existing host coverage"}elseif($isC130){"C127 Panel wrapper with obsolete direct-managed reverse helper retired"}elseif($isC128){"C128 Panel visibility, membership, activation cancellation, modal, and relaunch lifecycle"}elseif($isC127){"C127 Panel visibility/focus integration plus C126 and earlier regressions"}elseif($isC126){"C126 GroupBox passive integration plus C124/C125 regressions"}elseif($isC125){"C124 interoperability plus passive progress"}elseif($isC124){"radio host focused suite"}elseif($isC123){33}elseif($isC122){22}elseif($isC121){17}else{50}; legacyC120HostSuite = if($isC121 -or $isC122 -or $isC123 -or $isC124 -or $isC125 -or $isC126 -or $isC127 -or $isC128 -or $isC129 -or $isC130 -or $isC131 -or $isC132 -or $isC133 -or $isC134 -or $isC135 -or $isC137){"separate C120 runner"}else{"same image"}; modal = "one shallow picker scope with saved-ID restoration and forward fallback" }
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
    documentation = if ($isC146) { "docs\dotnet\NATIVEAOT_C146_SETTINGS_PERSISTENCE.md" } elseif ($isC145) { "docs\dotnet\NATIVEAOT_C145_MANAGED_MODAL_DIALOG.md" } elseif ($isC144) { "docs\dotnet\NATIVEAOT_C144_MANAGED_SETTINGS_CENTER.md" } elseif ($isC143) { "docs\dotnet\NATIVEAOT_C143_MANAGED_GROUPBOX.md" } elseif ($isC142) { "docs\dotnet\NATIVEAOT_C142_STACK_MARGIN_ALIGNMENT.md" } elseif ($isC141) { "docs\dotnet\NATIVEAOT_C141_MANAGED_VERTICAL_STACK_LAYOUT.md" } elseif ($isC140) { "docs\dotnet\NATIVEAOT_C140_MANAGED_SCROLLVIEW.md" } elseif ($isC135) { "docs\dotnet\NATIVEAOT_C135_MANAGED_POPUP_MENU.md" } elseif ($isC134) { "docs\dotnet\NATIVEAOT_C134_TRANSIENT_POPUP_ROUTING.md" } elseif ($isC132) { "docs\dotnet\NATIVEAOT_C132_MANAGED_RADIOBUTTON.md" } elseif ($isC131) { "docs\dotnet\NATIVEAOT_C131_MANAGED_CHECKBOX.md" } elseif ($isC130) { "docs\dotnet\NATIVEAOT_C130_C127_WRAPPER_STALL.md" } elseif ($isC129) { "docs\dotnet\NATIVEAOT_C129_SHIFT_TAB_INPUT_TRANSPORT.md" } elseif ($isC128) { "docs\dotnet\NATIVEAOT_C128_MANAGED_PANEL_LIFECYCLE.md" } elseif ($isC127) { "docs\dotnet\NATIVEAOT_C127_MANAGED_PANEL.md" } elseif ($isC126) { "docs\dotnet\NATIVEAOT_C126_MANAGED_GROUP_BOX.md" } elseif ($isC125) { "docs\dotnet\NATIVEAOT_C125_MANAGED_PROGRESS_BAR.md" } elseif ($isC124) { "docs\dotnet\NATIVEAOT_C124_MANAGED_RADIO_BUTTON.md" } elseif ($isC123) { "docs\dotnet\NATIVEAOT_C123_MANAGED_SEPARATOR.md" } elseif ($isC122) { "docs\dotnet\NATIVEAOT_C122_MANAGED_LABEL.md" } elseif ($isC121) { "docs\dotnet\NATIVEAOT_C121_MANAGED_CHECKBOX.md" } else { "docs\dotnet\NATIVEAOT_C120_MANAGED_CONTROL_HOST.md" }
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
if ($isC145) {
    $c145ResetCancel = Get-C145DialogPoint $c144Serial "reset" "cancel"
    $c145ResetConfirm = Get-C145DialogPoint $c144Serial "reset" "confirm"
    $c145CloseApply = Get-C145DialogPoint $c144Serial "close" "apply"
    $c145CloseDiscard = Get-C145DialogPoint $c144Serial "close" "discard"
    $c145CloseCancel = Get-C145DialogPoint $c144Serial "close" "cancel"
    $manifest.hostAbi.inputTransport = "existing pointer, wheel, KeyDown, and Shift payload transport; ABI v1/table 104 unchanged"
    $manifest.controlHost.capacity = 10
    $manifest.controlHost.registrationCount = 9
    $manifest.controlHost.tests = "C145 Dialog core 15; membership 10; MessageBox 11; modal routing 13; total 49; C145 Settings Center 18; C128 46; C131 34 API + 23 host cases; C144 56-case prior evidence referenced"
    $manifest.controlHost.modalOwner = "existing GuideXosControlHost single child scope; no modal stack or parallel focus system"
    $manifest.controlHost.registrationPolicy = "pre-register application-owned Dialog controls in two separate fixed-capacity child hosts; 14 registrations allocated total; parent stays 9/10; reset 2 and dirty-close 3; parent plus the active modal child is at most 12"
    $manifest.dialog = [ordered]@{
        capacity = 8
        titleMaximumLength = 32
        messageMaximumLength = 112
        messageMaximumLines = 4
        supportedMembers = @("Label", "Button", "CheckBox", "ComboBox", "Separator")
        unsupportedMembers = @("RadioButton", "Panel", "GroupBox", "ScrollView", "Dialog", "other host controls")
        defaultAndCancel = "configured Button result; Enter routes normal Button activation; Escape closes with cancel result; OK-only MessageBox Escape maps to OK"
        focusEntry = "eligible configured default Button, otherwise first eligible focusable member"
        traversal = "existing child GuideXosControlHost cyclic Tab/Shift+Tab order"
        restoration = "existing parent host restores saved stable ID; if hidden/disabled, its eligible next-member fallback is used"
        callbackOrder = "set Result, leave modal scope and restore parent focus, then call Closed once; callback may reopen only after the current modal exit completes"
        membership = "fixed direct references; duplicate and layout-owned members rejected; no lifetime ownership; overflow returns false"
        geometry = [ordered]@{ resetCancel = $c145ResetCancel; resetConfirm = $c145ResetConfirm; closeApply = $c145CloseApply; closeDiscard = $c145CloseDiscard; closeCancel = $c145CloseCancel }
    }
    $manifest.messageBox = [ordered]@{
        api = "GuideXosMessageBox configures the same GuideXosDialog engine"
        buttonSets = @("OK", "OKCancel", "YesNo", "YesNoCancel")
        dynamicButtonArrays = $false
        escape = "OK-only maps to OK; OKCancel/YesNoCancel map to Cancel; YesNo maps to No"
    }
    $manifest.settingsCenter = [ordered]@{
        application = "Managed Settings Center"
        parentRegistrations = 9
        parentCapacity = 10
        resetDialogRegistrations = 2
        dirtyCloseDialogRegistrations = 3
        dialogChildCapacity = 8
        cleanClose = "direct; no confirmation"
        reset = "Cancel preserves working/applied snapshot and viewport; Reset updates working defaults and normal dirty/dynamic-layout state"
        dirtyClose = "Cancel keeps parent open and dirty; Discard closes without applying; Apply commits and closes"
        scroll = "parent viewport remains unchanged through modal open/cancel; confirmed content change uses existing clamp"
        ownership = "application owns controls; dialogs keep non-owning references; GroupBox, ScrollView and VerticalStack remain non-owning/geometry-only"
    }
    $manifest.productionInput = [ordered]@{
        resetCancelEscape = $c144Serial.Contains("C145-RESET result=Cancel working=preserved")
        resetCancelPointer = ([regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Cancel working=preserved').Count -ge 2)
        resetConfirmEnter = $c144Serial.Contains("C145-KEYBOARD default=activated once result=PASS")
        resetConfirmPointer = ([regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C145-RESET result=Reset working=defaults').Count -ge 2)
        dirtyCloseCancel = $c144Serial.Contains("C145-UNSAVED result=Cancel dirty=preserved parent=open focus=restored result=PASS")
        dirtyCloseApply = $c144Serial.Contains("C145-UNSAVED result=Apply applied=committed closed=true result=PASS")
        modalWheelBlocksParent = $c144Serial.Contains("C145-MODAL wheel=parent-blocked viewport=preserved result=PASS")
        outsideClickBlocksParent = $c144Serial.Contains("C145-MODAL outside=consumed parent=blocked result=PASS")
    }
    $manifest.stress = [ordered]@{ messageBoxOpenCloseCycles = 25; resetConfirmationCycles = 25; dirtyCloseConfirmationCycles = 25; closeChoices = "Cancel, Discard, Apply; repeated fixed registrations and no modal/capture/drag leakage verified each cycle" }
    $manifest.finalState = [ordered]@{ modalOwner = "none"; transientCaptureOwner = "none"; pointerDragOwner = "none"; viewport = "valid"; registrationCount = 9; parentCapacity = 10 }
    $manifest.nativeAot = [ordered]@{ compositeElfSha256 = Get-Hash $compositeElf; proofKernelSha256 = Get-Hash $kernelPath; boots = @($bootResults | ForEach-Object { [ordered]@{ boot = $_.boot; outcome = $_.outcome; serialSha256 = $_.serialSha256 } }) }
    $manifest.retainedSuites = [ordered]@{ c144 = "56/56 prior evidence referenced; C145 reruns 18 integration cases against updated Reset/Close behavior"; c143 = "68/68 historical evidence referenced"; c142 = "68/68 historical evidence referenced"; c141 = "56/56 historical evidence referenced"; c140 = "58/58 historical evidence referenced"; c139 = "54/54 historical evidence referenced"; c138 = "62/62 historical evidence referenced"; c137 = "46/46 historical evidence referenced"; c128 = "46/46 Panel lifecycle rerun"; c129 = "31/31 Shift/Tab cases rerun; three fresh boots PASS"; c131 = "34/34 CheckBox API and 23/23 host cases rerun"; c133 = "Cumulative ComboBox production routing PASS in three C134 boots; standalone C133 proof timed out before its focused-suite marker"; c134 = "Transient-capture 30/30 and ComboBox production route rerun; three fresh boots PASS (focused C133 44/24 suites not emitted in this launch context)"; c135 = "Production 3/3 PASS; focused API 40/40 plus transient 30/30, 3/3 PASS; focused host emitted 40/40 PASS but timed out before close/result and did not emit transient 30/30" }
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C145_MANAGED_MODAL_DIALOG.md"
} elseif ($isC144) {
    $manifest.hostAbi.inputTransport = "existing pointer, wheel, scrollbar drag, KeyDown, KeyChar, and Shift payload transport; ABI v1/table 104 unchanged"
    $manifest.controlHost.capacity = 10
    $manifest.controlHost.legacyC120HostSuite = "not rerun by C144; only the 56 focused C144 application cases are claimed as rerun"
    $manifest.controlHost.registrationCount = 9
    $manifest.controlHost.safetyMargin = 1
    $manifest.controlHost.tests = "C144 state 22, composition 21, lifecycle 13, total 56 rerun; C143 68, C142 68, C141 56, C140 58, C139 54, C138 62, C137 46, C136 36, and C126 60 prior focused evidence referenced"
    $manifest.settingsCenter = [ordered]@{
        application = "Managed Settings Center"
        sections = @("Appearance", "Input", "System", "Advanced")
        groupBoxCount = 4
        groupedLeafControlCount = 17
        directScrollViewMemberCount = 21
        hostRegistrationCount = 9
        hostRegistrationCapacity = 10
        registrationSafetyMargin = 1
        radioGroups = 2
        comboBoxes = 3
        settingsModel = "bounded value snapshots; working and applied copies; no persistence"
        dirtyState = "working snapshot differs from applied snapshot"
        apply = "copies working to applied; preserves viewport"
        defaults = "updates all working controls deterministically; keeps viewport unless content shrink requires clamp"
        advancedVisibility = "non-destructive GroupBox visibility changes extent, synchronizes scrollbar, and clamps offset"
        inputEnabled = "Input GroupBox gates effective member state; own Enabled values remain unchanged"
        ownership = "application owns controls; GroupBox, VerticalStack, ScrollView, RadioGroup, and popup capture do not own control lifetime"
        focusOrder = "ScrollView member order across visible sections, followed by Options and header toggles; hidden/disabled members are skipped"
        stress = "20 Advanced toggles; 10 ComboBox capture open/commit/cancel cycles; 10 radio changes; 20 wheel events; four scrollbar value transitions; four Defaults/Apply cycles"
    }
    $manifest.groupBox = [ordered]@{
        api = "GuideXosGroupBox"
        count = 4
        sectionTitles = "Appearance, Input, System, Advanced"
        fixedCapacityPerGroup = 8
        membership = "each of 17 leaves is associated with exactly one GroupBox; all references remain non-owning"
        effectiveState = "member own visibility/enabled AND associated GroupBox visibility/enabled"
        focusable = $false
        layoutOwnership = "none; one VerticalStack arranges each section"
    }
    $manifest.verticalStack = [ordered]@{
        api = "GuideXosVerticalStack"
        count = 4
        capacityPerStack = 8
        memberships = "17 leaf references partitioned 3,5,6,3; non-owning geometry only"
        margins = "C142 member margins used for spacing and inset"
        alignment = "Left for labels/checks/radios, Stretch for ComboBoxes/ProgressBar, Right for action buttons"
        layoutOwnership = "geometry only; no recursive child/layout tree"
    }
    $manifest.scrollView = [ordered]@{
        api = "GuideXosScrollView"
        maximumMemberCount = 24
        previousDefaultMemberCount = 8
        previousMaximumMemberCount = 9
        capacityChange = "fixed maximum raised from 9 to 24 after auditing 4 frames + 17 leaves; old default-eight behavior retained"
        capacityProof = "fixed array; app occupies 21/24; focused tests accept capacity-1 and capacity then reject capacity+1"
        membership = "4 GroupBox frames plus 17 ordinary direct leaves; no recursive/nested ScrollView members"
        extent = [ordered]@{ initial = if ($null -ne $c144InitialGeometry) { $c144InitialGeometry.extent } else { $null }; final = if ($null -ne $c144FinalGeometry) { $c144FinalGeometry.extent } else { $null } }
        viewport = [ordered]@{ initial = if ($null -ne $c144InitialGeometry) { $c144InitialGeometry.viewport } else { $null }; final = if ($null -ne $c144FinalGeometry) { $c144FinalGeometry.viewport } else { $null }; initialOffset = if ($null -ne $c144InitialGeometry) { $c144InitialGeometry.offset } else { $null }; finalOffset = if ($null -ne $c144FinalGeometry) { $c144FinalGeometry.offset } else { $null } }
        binding = "one shared ScrollBar; wheel, drag, page, focus reveal, dynamic grow/shrink, clamp, and translated hit testing"
        allocationImpact = "fixed MemberEntry array changes from 9 to 24 entries (15 additional entries per ScrollView); no dynamic membership allocation introduced"
    }
    $manifest.productionInput = [ordered]@{
        wheelEvents = [regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-WHEEL viewport=changed').Count
        scrollbarDrags = [regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-DRAG press=PASS owner=scrollbar').Count
        scrollbarPages = [regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-SCROLL page=PASS offset=changed result=PASS').Count
        popupOutsideCancels = [regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-COMBO popup=closed capture=none').Count
        advancedShows = [regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=true').Count
        advancedHides = [regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-ADVANCED visible=false').Count
        comboKeyboardOpen = $c144Serial.Contains("C144-COMBO keyboard-open=PASS")
        comboKeyboardCommit = $c144Serial.Contains("C144-COMBO keyboard-commit=PASS")
        checkboxSpace = $c144Serial.Contains("C144-KEYBOARD space=activated working=changed result=PASS")
        radioArrow = $c144Serial.Contains("C144-KEYBOARD radio=arrow-selection result=PASS")
        keyboardButton = $c144Serial.Contains("C144-KEYBOARD button=apply result=PASS")
        menuCloseWhileCaptured = $c144Serial.Contains("C144-CLOSE popup=closed capture=none registration=bounded result=PASS")
    }
    $manifest.initialGeometry = $c144InitialGeometry
    $manifest.finalGeometry = $c144FinalGeometry
    $manifest.dirtyState = [ordered]@{
        changedTransitions = [regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-DIRTY working=changed').Count
        cleanTransitions = [regex]::Matches($c144Serial, '(?m)^\[C102-MANAGED-OUTPUT\] C144-DIRTY working=applied').Count
        menuApply = $c144Serial.Contains("C144-APPLY menu=PASS dirty=cleared viewport=preserved result=PASS")
        buttonApply = $c144Serial.Contains("C144-APPLY pointer=PASS dirty=cleared viewport=preserved result=PASS")
        menuDefaults = $c144Serial.Contains("C144-DEFAULTS menu=PASS controls=converged viewport=preserved result=PASS")
        buttonDefaults = $c144Serial.Contains("C144-DEFAULTS pointer=PASS controls=converged result=PASS")
    }
    $manifest.lifecycle = [ordered]@{ closeRelaunch = "native close then relaunch; registration count 9 on relaunch"; popupInvalidation = "section disable cancels ComboBox capture; final Options menu Close releases capture before surface close"; finalCapture = "none"; finalDragOwner = "none"; finalViewport = "valid"; finalLayout = "valid"; finalMembership = "valid" }
    $manifest.retainedSuites = [ordered]@{ c143 = "68/68 historical evidence referenced"; c142 = "68/68 historical evidence referenced"; c141 = "56/56 historical evidence referenced"; c140 = "58/58 historical evidence referenced"; c139 = "54/54 historical evidence referenced"; c138 = "62/62 historical evidence referenced"; c137 = "46/46 historical evidence referenced"; c136 = "36/36 historical evidence referenced"; c126 = "60/60 historical evidence referenced" }
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C144_MANAGED_SETTINGS_CENTER.md"
} elseif ($isC143) {
    $manifest.hostAbi.inputTransport = "existing pointer, wheel, button, keyboard, and Shift payload transport; ABI v1/table 104 unchanged"
    $manifest.controlHost.capacity = 8
    $manifest.controlHost.registrationCount = 6
    $manifest.controlHost.tests = "C143 core 20; composition 10; rendering/scroll 10; focus/lifecycle 10; popup 8; dynamic layout 10; total 68; retained C142 68, C141 56, C126 60"
    $manifest.groupBox = [ordered]@{
        api = "GuideXosGroupBox"
        title = "Server Options"
        capacity = 8
        membership = "fixed non-owning leaf association; duplicates, overflow, unsupported containers, and second GroupBox rejected"
        supportedMembers = "Button, CheckBox, Label, Separator, RadioButton, ProgressBar, ComboBox"
        bounds = "local 29,100,272,270 at initial offset 0"
        contentRectangle = "local 45,126,240,218; default content padding 8"
        focusable = $false
        layoutOwnership = "none; VerticalStack arranges controls"
        clipping = "none; ScrollView owns viewport clipping and translation"
        effectiveState = "member own visibility/enabled AND the associated GroupBox visibility/enabled"
        radioSemantics = "explicit GuideXosRadioGroup; no implicit GroupBox grouping"
        focusedTests = "core 20; composition 10; rendering/scroll 10; focus/lifecycle 10; popup 8; dynamic layout 10; total 68"
    }
    $manifest.verticalStack = [ordered]@{
        api = "GuideXosVerticalStack"
        capacity = 8
        bounds = "GroupBox content rectangle"
        membership = "same eight leaf references as GroupBox and ScrollView; geometry-only, non-owning"
        margins = "C142 per-member margins retained"
        alignment = "Left, Center, Right, Stretch retained"
        visibility = "hidden members release margins, spacing, and height; explicit PerformLayout"
    }
    $manifest.scrollView = [ordered]@{
        api = "GuideXosScrollView"
        maximumMemberCount = 9
        membership = "one non-focusable GroupBox frame plus eight direct leaf members; fixed and non-owning"
        clipping = "shared inner viewport; frame/title and leaves translate and clip together"
        input = "wheel, translated hit-test, Tab focus reveal, and ScrollBar drag remain ScrollView-owned"
        initialOffset = 0
        finalOffset = $c143FinalOffset
        finalInvariant = "0 <= Offset <= MaximumOffset; layout valid; capture none; drag owner none"
    }
    $manifest.notes.order = "Managed GroupBox Server Options frame is rendered first; Label, CheckBox, ComboBox, Separator, explicit two-member RadioGroup, ProgressBar, and Button are direct ScrollView members plus GroupBox associations and one Stack reference each"
    $manifest.notes.commands = "physical QEMU ComboBox open/commit, hide and disable cancellation of open ComboBox/Menu, CheckBox visibility relayout, 26 wheel inputs, RadioGroup switching, bottom ScrollBar drag, translated Apply button and PopupMenu, Tab/Shift+Tab, and clean transient state"
    $manifest.notes.capture = "final ComboBox/Menu transient capture none; final ScrollBar pointer drag owner none"
    $manifest.c143FocusedTests = [ordered]@{ core = 20; composition = 10; renderingAndScroll = 10; focusAndLifecycle = 10; popup = 8; dynamicLayout = 10; total = 68 }
} elseif ($isC142) {
    $manifest.hostAbi.inputTransport = "existing pointer, wheel, button, keyboard, and Shift payload transport; ABI v1/table 104 unchanged"
    $manifest.controlHost.capacity = 8
    $manifest.controlHost.tests = "C142 member metadata 20; vertical spacing 12; horizontal layout 12; ScrollView integration 14; popup/control integration 10; total 68; C141 56 and C140 58/C139 54/C138 62/C137 46 retained"
    $manifest.verticalStack = [ordered]@{
        api = "GuideXosVerticalStack"
        capacity = 8
        membership = "fixed insertion-order references; non-owning; duplicate, overflow, unsupported type, Panel conflict, and second-stack conflict rejected"
        supportedMembers = "Button, CheckBox, Label, Separator, RadioButton, ProgressBar, ComboBox"
        metadata = "member-owned bounded MarginLeft/Top/Right/Bottom and GuideXosVerticalStackHorizontalAlignment"
        alignments = "Stretch, Left, Center, Right; default Stretch"
        stretch = "inner width minus left/right member margins; character-aligned controls floor to 8-pixel cells"
        fixed = "desired width captured before Stack arrangement; Left/Center/Right preserve it"
        centerRounding = "floor half of remaining usable width; an odd spare pixel biases to the right"
        verticalFormula = "top padding + member top margin + height + bottom margin + spacing between visible members + bottom padding"
        hidden = "hidden members consume no height, margin, or adjacent spacing; disabled members retain layout"
        overflow = "negative Stretch width rejects the layout without writing negative geometry; fixed controls may exceed the usable region only while their bounded coordinates remain valid"
        composition = "same direct controls may be registered with ScrollView and referenced by one stack; Stack has no registration or viewport"
        focus = "ScrollView remains authoritative for traversal and reveal; margins are outside the control rectangle"
        popup = "ComboBox popup reads current arranged bounds; popup/capture state remains outside the stack"
    }
    $manifest.notes.order = "C142 production ScrollView: Label, CheckBox, stretched ComboBox, inset Separator, centered/left RadioButtons, stretched ProgressBar, right Button"
    $manifest.notes.commands = "physical QEMU ComboBox open/follow-up, checkbox visibility relayout, wheel, ScrollBar drag, translated alignment hits, Tab/Shift+Tab reveal, and relaunch/reset"
    $manifest.notes.space = "stack changes geometry only; no synthetic Tab KeyChar, capture stack, second focus system, recursive ownership, or wheel handling"
    $manifest.notes.capture = "final ComboBox transient capture none; final ScrollBar pointer drag owner none"
    $manifest.documentation = "docs\dotnet\NATIVEAOT_C142_STACK_MARGIN_ALIGNMENT.md"
} elseif ($isC141) {
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
if ($isC146 -and -not $SkipQemu) {
    $lastUiSerialPath = if ($isC148) {
        Join-Path $EvidenceRoot 'sequence-02\boot-verify\serial.log'
    } else {
        Join-Path $EvidenceRoot 'sequence-03\boot-verify\serial.log'
    }
    $lastUiSerial = Get-Content -LiteralPath $lastUiSerialPath -Raw
    $expectedRegistration = if ($isC148) { 10 } else { 9 }
    $finalUiMarker = $lastUiSerial -match ("(?m)^\[C102-MANAGED-OUTPUT\] C145-FINAL viewport=valid registration={0} modal=none capture=none drag=none result=PASS" -f $expectedRegistration)
    if (-not $finalUiMarker) { throw 'C146 final managed UI state did not report modal none, capture none, drag none, and a valid viewport.' }
    $manifest.outcome = 'Outcome A - durable Settings Center persistence validated'
    $manifest.hostAbi = [ordered]@{ version = 1; tableSize = 104; changed = $false; capabilityChanges = 'none'; fileCapabilities = 'existing FileRead, FileWrite, and FileStat' }
    $manifest.controlHost = [ordered]@{ capacity = 10; registrationCount = 9; ownership = 'application owns controls; containers and dialogs retain non-owning references'; modalOwnerModel = 'one active modal child scope; no modal stack' }
    $manifest.persistenceStore = [ordered]@{
        managedPath = '/system/apps/GXSETT.BIN'; backingPath = '/GXSETT.BIN'
        rationale = 'The existing /system wallpaper pack is a writable boot-time RAM copy. Exact-path native routing preserves the existing managed app path policy and reaches the writable root FAT volume for cross-boot state.'
        APIs = @('GuideXosFile.TryGetInfo via fileStat', 'GuideXosFile.ReadAllTextUtf8 byte-buffer bridge via fileReadAll', 'GuideXosFile.WriteAllTextUtf8 byte-buffer bridge via fileWriteAll')
        capability = 'existing managed App Model FileRead, FileWrite, and FileStat; ABI v1/table 104 unchanged'
        format = [ordered]@{ magic = 'GXSC'; version = 1; byteOrder = 'little-endian'; headerBytes = 12; payloadBytes = 9; totalBytes = 25; maximumAcceptedBytes = 64; fields = @('density', 'showStatus', 'showAdvanced', 'inputEnabled', 'naturalScroll', 'scrollSpeed', 'showKeyboardTips', 'statusDetail', 'reportFormat'); flags = 'u32 zero'; checksum = 'CRC-32/IEEE over header and payload; stored little-endian' }
        replacement = 'direct bounded overwrite; no managed flush, rename/replace, delete, or atomic update callback exists; synchronous write is followed by stat, full read-back, parser validation, and exact snapshot comparison'
        missing = 'load canonical defaults into working/applied/persisted; no write on open'
        corrupt = 'reject without partial state; load defaults and defer/show bounded OK MessageBox; dirty remains false'
        saveOrder = 'snapshot working candidate, serialize, overwrite, stat/read back, parse and compare, then update applied and persisted; failed save preserves prior applied/persisted and dirty working state'
        failureDialog = 'existing GuideXosMessageBox / GuideXosDialog; Settings Center remains open; one active modal scope'
        reset = 'confirmation changes working controls only; explicit Apply performs persistence'
        dirtyClose = 'Apply persists and closes only on success; Discard closes without writing; Cancel remains open'
        transientStatePersisted = $false
        startupCallbackSuppression = '_syncing guard suppresses callbacks during hydration; controls are populated after the file is validated; viewport starts at zero and normal focus policy runs'
    }
    $manifest.settingsState = [ordered]@{ working = 'in-memory edits'; applied = 'last accepted running configuration'; persisted = 'last successfully written and read-back-validated snapshot'; dirty = 'working differs from applied'; noOpApply = 'suppressed when working=applied=persisted' }
    $manifest.tests = [ordered]@{
        format = '13/13 PASS: defaults/non-default, round-trip, bad magic/version, header/payload truncation, oversized, invalid bool/enum, trailing bytes, checksum, bound, deterministic output'
        store = '10/10 PASS including missing, save/load/overwrite, read-back, corrupt fallback, write/read failures, handle cleanup, 50 repeated saves, and reentrancy guard'
        settingsCenter = '11/11 PASS: default hydration, edits, Reset working-only, Reset+Apply, Discard, Cancel, failed dirty-close Apply with MessageBox, dismiss/retry, no-op Apply, focus/viewport/capture/drag cleanup'
        settingsCenterTests = 'samples/managed/HostLogProof/GuideXos/GuideXosSettingsCenterC146Tests.cs'
        c145 = 'Full C145 Dialog (49) and Settings Center (18) suites are prior evidence and were not rerun; C146 reran its 11-case Settings Center suite and physical Reset, Discard, Close, and persistence-error paths'
        c144 = '56/56 prior evidence referenced; C144 focused suite not separately rerun in C146'
        lowerLevel = 'C128 46, C131 34 API + 23 host, plus C133/C134/C135 historical evidence referenced; no new independent lower-level runs claimed'
        failureInjection = 'C146 focused Settings Center test injected one save failure; MessageBox opened, working remained dirty, applied/persisted stayed unchanged; dismiss and retry succeeded'
    }
    $manifest.controlHost.tests = 'C146 focused reruns: format 13, store 10, Settings Center 11; no standalone C144/C145 suite rerun'
    $manifest.regressions = [ordered]@{
        c144 = '56/56 prior evidence; standalone suite not rerun in C146'
        c145 = '49 Dialog and 18 Settings Center cases are prior evidence; standalone suites not rerun in C146'
        lowerLevel = 'C128, C131, C133, C134, and C135 are prior evidence; no new independent lower-level runs claimed'
        c146 = 'format 13/13, store 10/10 with 50-save stress, Settings Center 11/11'
    }
    $manifest.retainedSuites = [ordered]@{
        c144 = '56/56 prior evidence referenced; standalone C144 suite not rerun in C146'
        c145 = '49 Dialog and 18 Settings Center cases are historical prior evidence; standalone C145 suites not rerun in C146'
        lowerLevel = 'C128 46, C131 34 API + 23 host, and C133/C134/C135 historical evidence referenced; no new independent runs claimed'
    }
    $manifest.stress = [ordered]@{
        repeatedStoreSaves = 50
        injectedPersistenceFailures = 1
        noOpApplyWrites = 0
        modalStressCycles = 'not separately rerun in C146'
    }
    $manifest.persistenceSequences = @($c146PersistenceSequences.ToArray())
    $manifest.persistenceSequenceOutcome = if ($c146PersistenceSequences.Count -eq 3 -and @($c146PersistenceSequences | Where-Object { $_.outcome -ne 'PASS' }).Count -eq 0) { 'PASS / PASS / PASS' } else { 'FAIL' }
    $manifest.nativeAot = [ordered]@{
        compositeElfPath = $compositeElf; compositeElfSha256 = Get-Hash $compositeElf
        proofKernelPath = $proofKernelPath; proofKernelSha256 = $c146ProofKernelSha256
        proofBoots = @($bootResults.ToArray()); ordinaryBoots = @($c146OrdinaryBoots.ToArray())
    }
    $manifest.ordinaryRestoration = [ordered]@{
        ordinaryKernelSha256Before = $c146CanonicalKernelSha256
        ordinaryEspKernelSha256Before = $c146EspKernelSha256
        ordinaryKernelSha256After = Get-Hash $kernelPath
        ordinaryEspKernelSha256After = Get-Hash (Join-Path $RepoRoot 'ESP\kernel.elf')
        protectedRamdiskSha256Before = $c146ProtectedRamdiskSha256
        protectedRamdiskSha256After = Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')
        protectedRamdiskUnchanged = ((Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')) -eq $c146ProtectedRamdiskSha256)
        ordinaryBootCount = $c146OrdinaryBoots.Count
    }
    $manifest.finalManagedUi = [ordered]@{ modalOwner = 'none'; popupCapture = 'none'; pointerDragOwner = 'none'; viewport = 'valid'; registrationCount = 9; parentCapacity = 10; verifiedBy = 'C145-FINAL marker after genuine fresh reboot and clean Close' }
    $manifest.architectureConstraints = [ordered]@{
        applicationOwnsControls = $true; newDaemonOrRegistry = $false; recursiveOwnershipTree = $false
        modalStack = $false; captureStack = $false; secondFocusSystem = $false
        syntheticTabKeyChar = $false; abiVersion = 1; abiTableSize = 104
    }
    $manifest.documentation = 'docs\dotnet\NATIVEAOT_C146_SETTINGS_PERSISTENCE.md'
    $manifest.evidence = [ordered]@{
        C146 = (Join-Path $EvidenceRoot 'c146.manifest.json')
        sequences = 'sequence-01 through sequence-03; each contains a shared test-media ESP across fresh QEMU boots'
        ordinary = 'ordinary.manifest.json'
        canonicalBackups = 'canonical\kernel.elf and canonical\ESP-kernel.elf'
    }
    $ordinaryManifest = [ordered]@{
        outcome = if ($c146OrdinaryBoots.Count -eq 3) { 'PASS' } else { 'FAIL' }
        kernelSha256 = Get-Hash $kernelPath; espKernelSha256 = Get-Hash (Join-Path $RepoRoot 'ESP\kernel.elf')
        protectedRamdiskSha256 = Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')
        protectedRamdiskMatchesStartingHash = ((Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')) -eq $c146ProtectedRamdiskSha256)
        boots = @($c146OrdinaryBoots.ToArray())
    }
    $ordinaryManifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'ordinary.manifest.json') -Encoding ASCII
}
if ($isC148) {
    $manifest.outcome = if ($SkipQemu) { 'BUILD_ONLY' } elseif ($isC149) { 'Outcome A - existing ShowKeyboardTips v2 field drives shared Managed Notes behavior' } else { 'Outcome A - v2 migration and persisted scroll amount validated' }
    $manifest.controlHost = [ordered]@{
        api = 'GuideXosControlHost'; capacity = 8; pickerCapacity = 2
        settingsCenterRegistrations = 10; settingsCenterRegistrationCapacity = 10
        settingsInputGroupMembers = 6; settingsInputGroupCapacity = 8
        settingsScrollViewMembers = 22; settingsScrollViewCapacity = 24
        ownership = 'application owns controls; containers and dialogs retain non-owning references'
        modalOwnerModel = 'one active modal child scope; no modal stack'
        tests = if ($isC149) { 'C149 field audit 14, runtime 14, consumer 14; current C148 format 23, runtime 17, consumer 15; C147 startup 10, runtime 15, consumer 10; C146 format 13, store 10 with 50-save stress, Settings Center 11' } else { 'C148 format 23, runtime 17, consumer 15; C147 startup 10, runtime 15, consumer 10; C146 format 13, store 10 with 50-save stress, Settings Center 11' }
    }
    $manifest.hostAbi = [ordered]@{
        version = 1; tableSize = 104; changed = $false; capabilityChanges = 'none'
        fileCapabilities = 'existing FileRead, FileWrite, and FileStat'
        inputTransport = 'existing PS/2 IntelliMouse normalization and logical wheel event; no raw transport changes'
    }
    $manifest.persistenceStore = [ordered]@{
        managedPath = '/system/apps/GXSETT.BIN'; backingPath = '/GXSETT.BIN'
        supportedVersions = @(1, 2); maximumAcceptedBytes = 64
        byteOrder = 'little-endian'; magic = 'GXSC'; headerBytes = 12
        v1 = [ordered]@{ version = 1; payloadBytes = 9; totalBytes = 25; semanticOffsets = '12..20'; checksumOffset = 21; checksum = 'CRC-32/IEEE over bytes 0..20' }
        v2 = [ordered]@{ version = 2; payloadBytes = 10; totalBytes = 26; legacySemanticOffsets = '12..20 unchanged'; keyboardTipsOffset = 18; scrollLinesOffset = 21; checksumOffset = 22; checksum = 'CRC-32/IEEE over bytes 0..21' }
        flags = 'u32 zero; no reserved semantic or payload bytes in v1'
        migration = 'valid v1 preserves all nine semantic values, supplies ScrollLinesPerNotch=3 in memory, and does not write on startup or Settings Center open'
        writePolicy = 'explicit successful Apply serializes v2, overwrites bounded file, verifies stat and full read-back, reparses and compares candidate, then commits complete runtime/applied/persisted snapshot'
        invalid = 'unknown version, wrong length, nonzero flags, invalid enum/boolean/amount, oversized file, and CRC mismatch are rejected without partial state; runtime defaults are initialized before app dispatch'
    }
    $manifest.scrollAmount = [ordered]@{
        setting = 'ScrollLinesPerNotch'; minimum = 1; maximum = 8; default = 3
        control = 'eight-choice Input ComboBox; explicit selection index 0..7 maps to semantic amount 1..8'
        runtimeSnapshot = 'NaturalScroll and ScrollLinesPerNotch committed together as one validated immutable GuideXosRuntimeSettingsSnapshot'
        wheelPolicy = 'C137 normalized signed notch delta; NaturalScroll transforms direction; amount multiplies each notch; existing viewport clamps'
        deltaBound = 'logical delta clamped to -8..+8 before safe multiplication; target consumers clamp their viewport'
        physicalTransportChanged = $false
    }
    $manifest.settingsState = [ordered]@{
        working = 'in-memory ComboBox edits'; applied = 'last accepted complete configuration'
        runtime = 'shared C147 runtime snapshot; remains unchanged before successful Apply'
        persisted = 'last successfully written and verified complete file snapshot'
        reset = 'changes working controls only until explicit Apply'; failedApply = 'previous runtime/applied/persisted preserved; working remains dirty for retry'
    }
    $manifest.tests = [ordered]@{
        c148Format = '23/23 expected from production NativeAOT serial marker: authentic v1 migration, no read rewrite, v2 format/CRC/bounds, invalid/corrupt/truncated/future records'
        c148Runtime = '17/17 expected: whole-snapshot validation, amount bounds, NaturalScroll independence, working isolation, apply, reset, failed save, v1/v2 startup'
        c148Consumer = '15/15 expected: ListBox and TextArea movement, one/default/larger amount, both directions, multi-notch, boundary/short-content clamp, apply and relaunch'
        c146 = 'format 13/13; store 10/10 including 50-save stress; Settings Center 11/11; physical Apply/Reset/clean close paths integrated in C148 boots'
        c147 = 'startup 10/10; runtime 15/15; consumer 10/10 and persistence error/reset/discard semantics rerun in Settings Center integrated path'
        c137 = 'fresh focused C137 suite 46/46 PASS; three fresh physical transport/Notes boots PASS; serial hashes recorded in out/dotnet/c137-mouse-wheel-scrolling/c137.manifest.json; C148 changes only logical policy'
        c144 = 'integrated Input ComboBox, hydration, focus, Apply, and clean-close paths; standalone C144 suite not claimed as rerun'
        c145 = 'integrated Reset confirmation, dirty close, and MessageBox failure/retry coverage; standalone full C145 suites not claimed as rerun'
        failureInjection = 'runtime and Settings Center suites verify failed persistence preserves prior applied/runtime/persisted values and keeps the new working value dirty for retry'
    }
    $manifest.regressions = [ordered]@{
        c146 = 'format 13/13; store 10/10; Settings Center 11/11'
        c147 = 'startup 10/10; runtime 15/15; consumer 10/10; startup precedes Managed Notes and Settings Center'
        c137 = 'fresh C137 focused 46/46 and three boots PASS; see out/dotnet/c137-mouse-wheel-scrolling/c137.manifest.json and boot-01..03 serial logs'
        c144 = 'integrated C148 Settings Center paths only; standalone result remains historical'
        c145 = 'integrated C148 Dialog paths only; standalone full-suite result remains historical'
    }
    $manifest.persistenceSequences = @($c148PersistenceSequences.ToArray())
    $manifest.persistenceSequenceOutcome = if ($c148PersistenceSequences.Count -eq 3 -and @($c148PersistenceSequences | Where-Object { $_.outcome -ne 'PASS' }).Count -eq 0) { 'PASS / PASS / PASS' } else { 'FAIL' }
    $manifest.startupMatrix = [ordered]@{
        malformedV2 = [ordered]@{
            sourcePath = 'startup-matrix\malformed-v2\test-media\ESP\GXSETT.BIN'
            sourceFileSha256 = if (Test-Path -LiteralPath (Join-Path $EvidenceRoot 'startup-matrix\malformed-v2\test-media\ESP\GXSETT.BIN')) { Get-Hash (Join-Path $EvidenceRoot 'startup-matrix\malformed-v2\test-media\ESP\GXSETT.BIN') } else { $null }
            sourceVersion = 2; invalidScrollLines = 0; checksum = 'validly recomputed'
            boot = @($bootResults.ToArray() | Where-Object { $_.role -eq 'malformed-v2' })
            result = if ($SkipQemu) { 'NOT-RUN' } else { 'PASS; defaults applied before Notes; real default ListBox movement; invalid file unchanged' }
        }
    }
    if ($isC149) {
        $manifest.outcome = if ($SkipQemu) { 'BUILD_ONLY' } else { 'Outcome D - verified runtime publication and fresh-launch behavior; immediate live consumer update requires a native multi-surface/repaint boundary' }
        $manifest.fieldClassification = [ordered]@{
            persistedSemanticFields = 10
            runtimeBacked = @('NaturalScroll', 'ScrollLinesPerNotch', 'ShowKeyboardTips')
            settingsCenterOrApplicationOnly = @('Density', 'ShowStatus', 'ShowAdvanced', 'InputEnabled', 'ScrollSpeed', 'StatusDetail', 'ReportFormat')
            presentationOnlyOrTransient = @()
            selected = 'ShowKeyboardTips at v2 byte offset 18; an existing Input checkbox whose shared runtime value directly controls the actual Managed Notes keyboard instruction line'
            v1Migration = 'ShowKeyboardTips remains its canonical default true because authentic v1 already stores this semantic at byte 18; ScrollLinesPerNotch remains the v2-only default 3'
        }
        $manifest.selectedRuntimeSetting = [ordered]@{
            name = 'ShowKeyboardTips'; default = $true; alternate = $false; existingV2ByteOffset = 18
            consumer = 'built-in Managed Notes'; metric = 'successful TrySetText at (20,198) writes the exact instruction line when true and clears it when false'
            runtimeOwner = 'GuideXosRuntimeSettings immutable shared snapshot'
            apply = 'after successful v2 save/read-back, commit the full snapshot; the existing Notes surface is closed when Settings Center opens, so a new Notes launch reads and renders the published value'
            effect = 'true => instruction line visible; false => instruction line empty'
            fileFormat = 'v2 unchanged; payload and checksum offsets unchanged; no ABI change'
        }
        $manifest.tests.c149FieldAudit = '14/14 current: ten persisted semantics classified; authentic v1 and exact C148-era v2 parse without rewrite; offset 18 Boolean validation, v2 round-trip, equality/dirty participation, canonical default true'
        $manifest.tests.c149Runtime = '14/14 current: defaults, loaded A/B, working isolation, persistence-first commit, failure, retry, Reset staging, Discard, Cancel, same-value and unrelated whole-snapshot updates'
        $manifest.tests.c149Consumer = '14/14 current: visible/hidden render mapping, pre-Apply stability, true/false runtime transition, Reset transition, new runtime initialization, corrupt/future fallback, independent wheel settings; physical same-window Apply repaint is unavailable because Settings Center replaces the sole native surface'
        $manifest.tests.currentC148 = '23 format / 17 runtime / 15 consumer cases executed in the C149 NativeAOT UI boot; v1 migration and v2 format remain covered'
        $manifest.tests.currentC147 = '10 startup / 15 runtime / 10 consumer cases executed in the C149 NativeAOT UI boot; runtime bootstrap precedes Managed Notes and Settings Center'
        $manifest.tests.currentC146 = '13 format / 10 store cases including 50-save stress / 11 Settings Center cases executed in the C149 NativeAOT UI boot; C149 field is included in Apply, failure, Reset, Cancel, Discard, retry assertions'
        $manifest.tests.currentInputRegression = 'Physical Managed Notes wheel direction and magnitude were checked in each C149 boot; C148 wheel consumer fixtures ran; standalone C137 46-case result remains historical, not rerun here'
        $manifest.tests.integratedC144C145 = 'Current QEMU sequences exercised checkbox edit, clean close, Reset confirmation, persistence error MessageBox/retry in the focused Settings Center suite, and focus/capture restoration; standalone C144/C145 phase suites were not rerun'
        $manifest.outcomeD = [ordered]@{
            reason = 'NativeAotManagedSurface owns one window; opening Managed Settings Center closes Managed Notes before creating the Settings Center surface. Closing Settings Center does not recreate Notes.'
            activeContextRule = 'native drawing requires context == g_activeManagedContext; a Settings Center callback cannot draw into a Notes-owned surface'
            evidence = 'physical QEMU serial orders Notes window close, Settings Center window create, successful runtime Apply, and Settings Center close with no Notes window recreation; native open() calls requestClose() when m_window is already present'
            requiredFutureBoundary = 'native multi-surface ownership/relaunch or a bounded cross-application repaint/invalidation service; not added to ABI v1 in C149'
        }
        $manifest.evidenceHistory = [ordered]@{
            currentC149Reruns = 'C149 field/runtime/consumer tests; three persistence sequences; v1 readonly, v1 Apply upgrade, v2 hydration, Reset+Apply reboot, malformed v2, exact C148 v2, unsupported v3; three ordinary boots'
            integratedC149Paths = 'C144 edit/hydration/Apply/clean close; C145 Reset confirmation and persistence error MessageBox; C146 failure/retry/discard/cancel; C147-before-app startup; C148 v1/v2 and wheel consumer paths'
            historicalOnly = 'standalone C137 focused 46-case suite and standalone C144/C145 full phase suites were not rerun; prior phase evidence is referenced by their existing manifests'
        }
        $manifest.startupMatrix.c149Fixtures = @($c149StartupMatrix.ToArray())
        $manifest.persistenceSequences = @($c148PersistenceSequences.ToArray())
        $manifest.persistenceSequenceOutcome = if ($c148PersistenceSequences.Count -eq 3 -and @($c148PersistenceSequences | Where-Object { $_.outcome -ne 'PASS' }).Count -eq 0) { 'PASS / PASS / PASS' } else { 'FAIL' }
    }
    if ($isC150) {
        $manifest.outcome = if ($SkipQemu) { 'BUILD_ONLY' } else { 'Outcome A - bounded Managed Notes return and fresh App Model relaunch validated' }
        $manifest.phase = 'C150'
        [void]$manifest.Remove('outcomeD')
        $manifest.managedApplicationReturn = [ordered]@{
            architecture = 'one active managed graphical surface; replacement closes and unregisters the old surface before opening the next'
            identity = 'canonical built-in App Model appId, bounded to 95 printable ASCII bytes in one fixed 96-byte slot'
            capacity = 1; stack = $false; pointerOrInstanceRetention = $false
            capture = 'Managed Notes action validates Notes and Settings Center catalog identities, then copies Notes appId before normal App Model launch closes its surface'
            eligibleCaller = 'Managed Notes (com.guidexos.apps.managed.notes); Settings Center self-target and occupied caller slot are rejected'
            directSettingsLaunch = 'no caller target; ordinary Settings Center close preserves shell behavior'
            clearing = 'close hook marks one deferred return; after window unregistration and active managed dispatch, the kernel takes and clears identity before resolving and launching'
            relaunch = 'desktop::launch_app_with_context through existing built-in App Model metadata and launch-context setup; creates a fresh Notes instance and surface'
            failure = 'resolution or launch failure clears the target, closes any failed surface, opens terminal fallback, and does not retry'
            abi = 'v1, 104 entries; unchanged'; settings = 'fresh Notes reads the shared runtime snapshot; no repaint or saved managed object graph'
        }
        $manifest.tests.c150ReturnTarget = '10/10 native focused cases: empty, copied canonical identity, occupied slot, invalid/unresolvable ID, self-target, consume once and clear, explicit clear, Notes/Settings resolution, unknown identity rejection'
        $manifest.tests.c150ManagedLifetime = '8/8 managed focused cases: empty, fresh launch, active dispatch, wrong identity, replacement, prior instance released, unrelated clear ignored, idempotent clear'
        $manifest.tests.c150ProductionIntegration = 'direct launch isolation; 25 return cycles; sequence-01 authentic v1 read-only plus ShowKeyboardTips=false Apply/return; sequence-02 ShowKeyboardTips edit plus dirty-close Discard/return; sequence-03 Reset Cancel, dirty-close Cancel, injected Apply failure/dismissal, successful retry Apply/return; each full UI boot checks fresh Notes tip rendering'
        $manifest.tests.c150OtherWorkflows = 'production Settings Center paths cover Reset Cancel, dirty-close Cancel and Discard, failed persistence MessageBox/dismissal/retry, successful Apply, and close; standalone full C145 suite remains historical'
        $manifest.tests.currentC148 = '23 format / 17 runtime / 15 consumer cases and fresh returned-Notes NaturalScroll/ScrollLines wheel checks executed in the C150 NativeAOT UI boots'
        $manifest.tests.currentC147 = '10 startup / 15 runtime / 10 consumer cases executed in the C150 NativeAOT UI boots; runtime bootstrap precedes Managed Notes and Settings Center'
        $manifest.tests.currentC146 = '13 format / 10 store cases including 50-save stress / 11 Settings Center cases executed in the C150 NativeAOT UI boots; failure/retry/discard/cancel paths include ShowKeyboardTips'
        $manifest.tests.currentInputRegression = 'Physical Managed Notes wheel direction and magnitude are checked in each C150 boot, including a returned-Notes event; standalone C137 46-case result remains historical'
        $manifest.tests.integratedC144C145 = 'Current QEMU sequences exercise checkbox edit, Reset Cancel, dirty-close Apply/Discard/Cancel, persistence-error MessageBox/retry, and focus/capture restoration; standalone C144/C145 suites were not rerun'
        $manifest.tests.historicalEvidence = 'standalone C137 46-case suite and standalone C144/C145 phase suites are referenced by prior manifests, not rerun as standalone phases'
        $manifest.evidenceHistory = [ordered]@{ currentC150Reruns = 'return-target and managed-lifetime focused suites; direct launch; 25-cycle stress; production return sequences; startup matrix; three ordinary boots'; integratedC150Paths = $manifest.tests.c150ProductionIntegration; historicalOnly = $manifest.tests.historicalEvidence }
        $manifest.documentation = 'docs\dotnet\NATIVEAOT_C150_MANAGED_APP_RETURN_RELAUNCH.md'
    }
    $manifest.nativeAot = [ordered]@{
        compositeElfPath = $compositeElf; compositeElfSha256 = Get-Hash $compositeElf
        uiProofKernelPath = $c148UiKernelPath; uiProofKernelSha256 = Get-Hash $c148UiKernelPath
        notesOnlyProofKernelPath = $c148NotesOnlyKernelPath; notesOnlyProofKernelSha256 = Get-Hash $c148NotesOnlyKernelPath
        proofKernelSha256 = if ($c148ProofKernelSha256) { $c148ProofKernelSha256 } else { Get-Hash $c148UiKernelPath }
        kernelVariants = [ordered]@{
            runAction = if ($c148KernelVariantsReused) { "reused previously built exact $ProofPhase variants after kernel-source freshness and binary-marker checks" } else { "built $ProofPhase UI and Notes-only variants from current sources" }
            sourceFreshnessVerified = $true
            uiVariantHasSettingsAndC147Markers = $true
            notesOnlyVariantHasNoSettingsCenterMarker = $true
        }
        proofBoots = @($bootResults.ToArray())
        nativeAot = $true; abiVersion = 1; abiTableSize = 104
    }
    $manifest.ordinaryRestoration = [ordered]@{
        ordinaryKernelSha256Before = $c146CanonicalKernelSha256
        ordinaryEspKernelSha256Before = $c146EspKernelSha256
        ordinaryKernelSha256After = Get-Hash $kernelPath
        ordinaryEspKernelSha256After = Get-Hash (Join-Path $RepoRoot 'ESP\kernel.elf')
        protectedRamdiskSha256Before = $c146ProtectedRamdiskSha256
        protectedRamdiskSha256After = Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')
        protectedRamdiskUnchanged = ((Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')) -eq $c146ProtectedRamdiskSha256)
        proofMarkerAbsentFromCanonicalKernel = ((Get-Hash $kernelPath) -eq $c146CanonicalKernelSha256)
        ordinaryBootCount = $c146OrdinaryBoots.Count
        ordinaryBoots = @($c146OrdinaryBoots.ToArray())
    }
    $manifest.finalManagedUi = [ordered]@{
        modalOwner = 'none'; popupCapture = 'none'; pointerDragOwner = 'none'; viewport = 'valid'
        registrationCount = 10; parentCapacity = 10
        verifiedBy = 'C145-FINAL marker after sequence-03 successful retry Apply and before Settings Center teardown/Notes relaunch'
    }
    if ($isC150) {
        $manifest.finalReturnState = [ordered]@{
            activeApplication = 'Managed Notes'; returnTarget = 'none'; activeSurfaceCount = 1
            freshInstance = $true; keyboardTips = if ($c148PersistenceSequences.Count -ge 3) { $c148PersistenceSequences[2].finalSettingsFile.keyboardTips -eq 1 } else { $null }
            verifiedBy = 'sequence-03 C150-RETURN-RESULT and C150-RELAUNCH after successful retry Apply'
        }
    }
    $manifest.architectureConstraints = [ordered]@{
        applicationOwnsControls = $true; newDaemonOrRegistry = $false; genericSchemaFramework = $false
        recursiveOwnershipTree = $false; modalStack = $false; captureStack = $false
        secondFocusSystem = $false; syntheticTabKeyChar = $false
        abiVersion = 1; abiTableSize = 104
    }
    $manifest.documentation = if ($isC149) { 'docs\dotnet\NATIVEAOT_C149_SECOND_RUNTIME_SETTING.md' } else { 'docs\dotnet\NATIVEAOT_C148_SETTINGS_V2_SCROLL_AMOUNT.md' }
    $manifest.evidence = [ordered]@{
        sequences = if ($isC149) { 'sequence-01 through sequence-03; sequence-01 first boots authentic v1 in Notes-only mode, then verifies Apply publication and the fresh Notes launch sees the hidden setting; sequence-03 verifies Reset+Apply and default behavior on fresh launch; same-instance live repaint is unavailable at the native single-surface boundary' } else { 'sequence-01 through sequence-03; sequence-01 first boots authentic v1 in Notes-only mode and proves the v1 file hash is unchanged after clean QEMU shutdown' }
        startupMatrix = 'startup-matrix\malformed-v2'
        kernels = if ($isC149) { 'kernels\kernel-c149-ui.elf and kernels\kernel-c149-notes-only.elf' } else { 'kernels\kernel-c148-ui.elf and kernels\kernel-c148-notes-only.elf' }
        ordinary = 'ordinary.manifest.json'
        canonicalBackups = 'canonical\kernel.elf and canonical\ESP-kernel.elf'
        serials = if ($isC149) { 'each sequence boot-role serial.log including sequence-01 boot-v1-readonly; malformed-v2, exact-C148-v2, unsupported-v3, and ordinary-boot-01 through ordinary-boot-03 serial.log' } else { 'each sequence boot-role serial.log including sequence-01 boot-v1-readonly; malformed-v2 serial.log; ordinary-boot-01 through ordinary-boot-03 serial.log' }
    }
    if ($isC150) {
        $manifest.phase = 'C150'
        $manifest.outcome = if ($SkipQemu) { 'BUILD_ONLY' } else { 'Outcome A - bounded Managed Notes return and fresh App Model relaunch validated' }
        $manifest.documentation = 'docs\dotnet\NATIVEAOT_C150_MANAGED_APP_RETURN_RELAUNCH.md'
        $manifest.evidence.sequences = 'sequence-01 authentic v1 read-only plus Apply/return, sequence-02 dirty-close Discard/return, sequence-03 Reset Cancel and dirty-close Cancel followed by failed Apply/dismissal and successful retry/return, direct launch, and 25-cycle return stress'
        $manifest.evidence.kernels = 'kernels\kernel-c150-ui.elf and kernels\kernel-c150-notes-only.elf'
        $manifest.evidence.serials = 'C150 sequence boot-role serial.log files, C150 startup matrix serial.log files, and ordinary-boot-01 through ordinary-boot-03 serial.log'
        $manifest.evidence.returnTarget = 'C150-RETURN-ARMED, C150-RETURN-CONSUMED, C150-RETURN-RESULT, C150-RELAUNCH, and C150-SURFACE lifecycle markers'
        $manifest.evidence.applyReturn = 'sequence-01 commits ShowKeyboardTips=false and verifies the newly launched Notes surface renders tips hidden'
        $manifest.evidence.discardReturn = 'sequence-02 edits ShowKeyboardTips=true, Discards, and verifies fresh Notes retains false with the original settings file hash'
        $manifest.evidence.cancelRetryReturn = 'sequence-03 cancels Reset and dirty close, injects and dismisses a persistence failure, then retries Apply and verifies fresh Notes renders tips visible'
    }
    $ordinaryManifest = [ordered]@{
        outcome = if ($c146OrdinaryBoots.Count -eq 3) { 'PASS' } else { 'FAIL' }
        phase = if ($isC149) { 'C149' } else { 'C148' }; kernelSha256 = Get-Hash $kernelPath
        espKernelSha256 = Get-Hash (Join-Path $RepoRoot 'ESP\kernel.elf')
        protectedRamdiskSha256 = Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')
        protectedRamdiskMatchesStartingHash = ((Get-Hash (Join-Path $RepoRoot 'ESP\ramdisk.img')) -eq $c146ProtectedRamdiskSha256)
        boots = @($c146OrdinaryBoots.ToArray())
    }
    if ($isC150) { $ordinaryManifest.phase = 'C150' }
    $ordinaryManifest | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'ordinary.manifest.json') -Encoding ASCII
}
$manifest | ConvertTo-Json -Depth 24 | Set-Content -LiteralPath (Join-Path $EvidenceRoot ("{0}.manifest.json" -f $phaseLower)) -Encoding ASCII
Write-Host "$ProofPhase outcome=$($manifest.outcome) evidence=$EvidenceRoot" -ForegroundColor Green
