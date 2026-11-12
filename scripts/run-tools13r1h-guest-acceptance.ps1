[CmdletBinding()]
param(
    [switch]$RunGuest,
    [string]$EspPath = 'D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS13R1G-final-guest-image\ESP',
    [string]$SourceRoot = 'D:\dev\guideXOS_DeveloperStudio_Tools_Source',
    [string]$ServerRoot = 'D:\dev\guideXOSServerV0.5_TOOLS13R1E_INTEGRATION',
    [string]$QacCliPath = '',
    [string]$QemuPath = '',
    [string]$CompatibilityDispositionReport = 'D:\dev\guideXOS_DeveloperStudio_Tools\docs\TOOLS13R1H_FIRST_AMD64_GUEST_ACCEPTANCE.md',
    [string]$QacAuthorizationPath = '',
    [string]$EvidenceRoot = '',
    [ValidateRange(60, 3600)][int]$AdmissionTimeoutSeconds = 1800,
    [ValidateRange(60, 3600)][int]$GuestRuntimeSeconds = 1200
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ExpectedTreeSha256 = 'DDB26BB2D986A331963971492C87EF2F5F9643AC07AB791E513DA966D2F59CDD'
$ExpectedElfSha256 = '9CBC32163417A553B38A1F01EF69CDBAF49B99C58B9D63D0F06E2E9A31C1AC16'
$ExpectedElfSize = 1262228L
$PackageRelative = 'Apps\DeveloperStudio'
$FixtureRelative = 'TOOLS13R1C\workspace'
$QacModulePath = Join-Path $ServerRoot 'scripts\DeveloperStudio.QacAcceptance.psm1'
$script:QmpClient = $null
$script:QmpReader = $null
$script:QmpWriter = $null
$script:EvidenceEvents = [System.Collections.Generic.List[object]]::new()
$script:ObservationCases = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
$script:LastInputAtUtc = [DateTime]::MinValue
$script:LastCaptureAtUtc = [DateTime]::MinValue
$script:CaptureSequence = 0

function Resolve-ExistingExecutable {
    param([string]$Path, [string]$EnvironmentName, [string]$CommandName, [string]$DiscoveredFallback)

    $candidate = $Path
    if ([string]::IsNullOrWhiteSpace($candidate) -and $EnvironmentName) { $candidate = [Environment]::GetEnvironmentVariable($EnvironmentName) }
    if ([string]::IsNullOrWhiteSpace($candidate) -and $CommandName) {
        $command = Get-Command $CommandName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command) { $candidate = $command.Source }
    }
    if ([string]::IsNullOrWhiteSpace($candidate) -and $DiscoveredFallback) { $candidate = $DiscoveredFallback }
    if ([string]::IsNullOrWhiteSpace($candidate) -or -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Required executable was not found. Pass its path or configure $EnvironmentName / PATH."
    }
    return [IO.Path]::GetFullPath($candidate)
}

function Get-CanonicalEspIdentity {
    param([Parameter(Mandatory = $true)][string]$Root)

    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $fullRoot -PathType Container)) { throw "ESP directory is missing: $fullRoot" }
    $items = @(Get-ChildItem -LiteralPath $fullRoot -File -Recurse -Force | ForEach-Object {
        [pscustomobject]@{
            Path = $_.FullName.Substring($fullRoot.Length + 1).Replace('\', '/')
            FullName = $_.FullName
            Length = [long]$_.Length
        }
    })
    $comparison = [System.Comparison[object]]{ param($a, $b) [StringComparer]::Ordinal.Compare($a.Path, $b.Path) }
    [Array]::Sort($items, $comparison)

    $utf8 = [System.Text.UTF8Encoding]::new($false)
    $buffer = [IO.MemoryStream]::new()
    try {
        foreach ($item in $items) {
            $pathBytes = $utf8.GetBytes($item.Path)
            $buffer.Write([BitConverter]::GetBytes([long]$pathBytes.Length), 0, 8)
            $buffer.Write($pathBytes, 0, $pathBytes.Length)
            $buffer.Write([BitConverter]::GetBytes([long]$item.Length), 0, 8)
            $file = [IO.File]::OpenRead($item.FullName)
            try { $file.CopyTo($buffer) } finally { $file.Dispose() }
        }
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $digest = [BitConverter]::ToString($sha.ComputeHash($buffer.ToArray())).Replace('-', '') } finally { $sha.Dispose() }
    } finally { $buffer.Dispose() }
    return [pscustomobject]@{
        FileCount = $items.Count
        PayloadBytes = [long](($items | Measure-Object Length -Sum).Sum)
        Sha256 = $digest
        Files = $items
    }
}

function Assert-StaticInputs {
    param([string]$Root)

    $identity = Get-CanonicalEspIdentity -Root $Root
    if ($identity.FileCount -ne 33 -or $identity.PayloadBytes -ne 72774398L -or $identity.Sha256 -cne $ExpectedTreeSha256) {
        throw "ESP identity mismatch: files=$($identity.FileCount) bytes=$($identity.PayloadBytes) sha256=$($identity.Sha256)"
    }

    $required = @(
        'EFI\BOOT\BOOTX64.EFI', 'kernel.elf', 'ramdisk.img', 'desktop.cfg', 'startup.nsh',
        "$PackageRelative\app.json",
        "$PackageRelative\bin\amd64\developerstudio.elf",
        "$PackageRelative\bin\arm64\developerstudio.elf",
        "$FixtureRelative\README.md", "$FixtureRelative\main.cpp",
        "$FixtureRelative\helpers.cpp", "$FixtureRelative\helpers.h"
    )
    foreach ($relative in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $relative) -PathType Leaf)) { throw "Required ESP file is missing: $relative" }
    }

    $manifestPath = Join-Path $Root "$PackageRelative\app.json"
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    if ($manifest.version -ne '0.1.0' -or $manifest.minGuideXOSVersion -ne '0.5.0') { throw 'The staged Developer Studio version fields changed.' }
    $amd64 = Join-Path $Root "$PackageRelative\bin\amd64\developerstudio.elf"
    $elfItem = Get-Item -LiteralPath $amd64
    $elfHash = (Get-FileHash -LiteralPath $amd64 -Algorithm SHA256).Hash
    if ($elfItem.Length -ne $ExpectedElfSize -or $elfHash -cne $ExpectedElfSha256) { throw 'Accepted Developer Studio AMD64 ELF identity mismatch.' }

    $packageFiles = @(Get-ChildItem -LiteralPath (Join-Path $Root $PackageRelative) -File -Recurse -Force | ForEach-Object {
        $_.FullName.Substring((Join-Path $Root $PackageRelative).Length + 1).Replace('\', '/')
    } | Sort-Object -CaseSensitive)
    $expectedPackageFiles = @('app.json', 'bin/amd64/developerstudio.elf', 'bin/arm64/developerstudio.elf')
    if (($packageFiles -join '|') -cne ($expectedPackageFiles -join '|')) { throw "Unexpected Developer Studio package file set: $($packageFiles -join ',')" }
    $validator = Join-Path $SourceRoot 'tests\validate-developer-studio-package.ps1'
    if (-not (Test-Path -LiteralPath $validator -PathType Leaf)) { throw "Developer Studio package validator is missing: $validator" }
    & $validator -ServerRoot ([IO.Directory]::GetParent([IO.Path]::GetFullPath($Root)).FullName) `
        -PackageRoot (Join-Path $Root $PackageRelative) | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Developer Studio package validator failed with exit code $LASTEXITCODE." }

    return [pscustomobject]@{
        Identity = $identity
        ManifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
        AcceptedElfSize = $elfItem.Length
        AcceptedElfSha256 = $elfHash
        PackageFiles = $packageFiles
        Manifest = $manifest
    }
}

function Assert-GuestAuthorizations {
    if (-not (Test-Path -LiteralPath $CompatibilityDispositionReport -PathType Leaf)) { throw 'Compatibility disposition report is missing.' }
    $report = Get-Content -Raw -LiteralPath $CompatibilityDispositionReport
    if ($report -notmatch 'TOOLS13R1H_EXPERIMENTAL_NONRELEASE_APPROVED') {
        throw 'Guest launch is blocked: the report does not record the unresolved-version experimental disposition.'
    }
    if ([string]::IsNullOrWhiteSpace($QacAuthorizationPath) -or -not (Test-Path -LiteralPath $QacAuthorizationPath -PathType Leaf)) {
        throw 'Guest launch is blocked: provide a recorded TOOLS13R1H QAC authorization JSON before reservation.'
    }
    $authorization = Get-Content -Raw -LiteralPath $QacAuthorizationPath | ConvertFrom-Json
    if ($authorization.scope -ne 'TOOLS13R1H' -or $authorization.status -ne 'approved' -or
        [string]::IsNullOrWhiteSpace([string]$authorization.authorizedBy) -or
        [string]::IsNullOrWhiteSpace([string]$authorization.date) -or
        $authorization.basis -notin @('net-support-handoff', 'independent-tools-lane') -or
        [string]::IsNullOrWhiteSpace([string]$authorization.evidence)) {
        throw 'QAC authorization record is incomplete. Required: scope, approved status, author, date, handoff/independent lane basis, and evidence reference.'
    }
    try { [void][DateTime]::Parse([string]$authorization.date) } catch { throw 'QAC authorization date is invalid.' }
    return [pscustomobject]@{
        Authorization = $authorization
        Sha256 = (Get-FileHash -LiteralPath $QacAuthorizationPath -Algorithm SHA256).Hash
    }
}

function Write-JsonLine {
    param([string]$Path, [object]$Value)
    $line = ConvertTo-Json -InputObject $Value -Depth 12 -Compress
    [IO.File]::AppendAllText($Path, $line + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}

function Connect-Qmp {
    param([int]$Port, [int]$TimeoutSeconds = 3)

    $client = [Net.Sockets.TcpClient]::new()
    $connect = $client.BeginConnect([Net.IPAddress]::Loopback, $Port, $null, $null)
    if (-not $connect.AsyncWaitHandle.WaitOne($TimeoutSeconds * 1000)) { $client.Dispose(); throw "QMP connection timed out on port $Port." }
    $client.EndConnect($connect)
    $stream = $client.GetStream()
    $stream.ReadTimeout = $TimeoutSeconds * 1000
    $stream.WriteTimeout = $TimeoutSeconds * 1000
    $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
    $writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::ASCII)
    $writer.NewLine = "`n"
    $writer.AutoFlush = $true
    $script:QmpClient = $client
    $script:QmpReader = $reader
    $script:QmpWriter = $writer
    $greeting = $reader.ReadLine()
    if ([string]::IsNullOrWhiteSpace($greeting) -or $greeting -notmatch '"QMP"') { throw 'QEMU did not return a QMP greeting.' }
    [void](Invoke-Qmp -Execute 'qmp_capabilities' -TimeoutSeconds $TimeoutSeconds)
    return $greeting
}

function Invoke-Qmp {
    param([Parameter(Mandatory = $true)][string]$Execute, [object]$Arguments = $null, [int]$TimeoutSeconds = 5)

    $request = [ordered]@{ execute = $Execute }
    if ($null -ne $Arguments) { $request.arguments = $Arguments }
    $script:QmpWriter.WriteLine((ConvertTo-Json -InputObject $request -Depth 12 -Compress))
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { $line = $script:QmpReader.ReadLine() } catch [IO.IOException] { continue }
        if ($null -eq $line) { throw "QMP closed during command $Execute." }
        $response = $line | ConvertFrom-Json
        if ($response.event) { continue }
        if ($response.error) { throw "QMP $Execute failed: $($response.error | ConvertTo-Json -Compress)" }
        if ($response.PSObject.Properties.Name -contains 'return') {
            return [pscustomobject]@{ Response = $response; Raw = $line }
        }
    }
    throw "QMP command timed out: $Execute"
}

function Write-QmpEvent {
    param([string]$Name, [string]$Request, [object]$Result, [string]$ErrorText = '')
    $record = [pscustomobject]@{
        utc = [DateTime]::UtcNow.ToString('o')
        action = $Name
        request = $Request
        response = if ($Result) { $Result.Raw } else { $null }
        error = $ErrorText
    }
    Write-JsonLine -Path $script:TranscriptPath -Value $record
    $script:EvidenceEvents.Add($record)
    $script:LastScreenshotPath = $path
    $script:LastScreenshotSha256 = $hash
    $script:LastScreenshotPath = $path
    $script:LastScreenshotSha256 = $hash
}

function Capture-Screenshot {
    param([string]$Label)

    $safe = [regex]::Replace($Label.ToLowerInvariant(), '[^a-z0-9_-]+', '-')
    if ([string]::IsNullOrWhiteSpace($safe)) { $safe = 'transition' }
    $script:CaptureSequence++
    $name = '{0:D3}-{1}.ppm' -f $script:CaptureSequence, $safe
    $path = Join-Path $script:RunEvidenceDirectory $name
    $hmpPath = $path.Replace('\', '/')
    $request = "human-monitor-command screendump $path"
    $result = Invoke-Qmp -Execute 'human-monitor-command' -Arguments @{ 'command-line' = "screendump `"$hmpPath`"" }
    Start-Sleep -Milliseconds 250
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "QMP screenshot was acknowledged but no screenshot file exists: $path" }
    $file = Get-Item -LiteralPath $path
    if ($file.Length -lt 32) { throw "QMP screenshot is unexpectedly small: $path" }
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $status = Invoke-Qmp -Execute 'query-status'
    $script:LastCaptureAtUtc = [DateTime]::UtcNow
    $record = [pscustomobject]@{
        utc = $script:LastCaptureAtUtc.ToString('o')
        action = 'screenshot'
        label = $Label
        path = $path
        bytes = $file.Length
        sha256 = $hash
        qmpStatus = $status.Response.PSObject.Properties['return'].Value
        qmpAcknowledgment = $result.Raw
    }
    Write-JsonLine -Path $script:TranscriptPath -Value $record
    $script:EvidenceEvents.Add($record)
    Write-Host "Captured $path ($($file.Length) bytes, SHA256 $hash)"
    return $record
}

function Send-QmpKeys {
    param([string[]]$Keys, [string]$Label)

    if ($Keys.Count -lt 1 -or $Keys.Count -gt 4) { throw 'Provide one to four QEMU qcode names.' }
    $values = @($Keys | ForEach-Object { @{ type = 'qcode'; data = $_.ToLowerInvariant() } })
    $request = @{ keys = $values; 'hold-time' = 80 }
    $result = Invoke-Qmp -Execute 'send-key' -Arguments $request
    Write-QmpEvent -Name $Label -Request ((ConvertTo-Json $request -Compress)) -Result $result
    $script:LastInputAtUtc = [DateTime]::UtcNow
    Start-Sleep -Milliseconds 180
    [void](Capture-Screenshot -Label "input-$Label")
}

function ConvertTo-QcodeChord {
    param([string]$Character)

    if ($Character -cmatch '^[a-z0-9]$') { return ,@($Character) }
    if ($Character -cmatch '^[A-Z]$') { return ,@('shift', $Character.ToLowerInvariant()) }
    $map = @{
        ' ' = @('spc'); '/' = @('slash'); '\' = @('backslash'); '.' = @('dot'); ',' = @('comma')
        '-' = @('minus'); '_' = @('shift','minus'); ':' = @('shift','semicolon'); ';' = @('semicolon')
        '=' = @('equal'); '+' = @('shift','equal'); '[' = @('bracket_left'); ']' = @('bracket_right')
    }
    if ($map.ContainsKey($Character)) { return ,$map[$Character] }
    throw "Unsupported printable character for QMP type: $Character"
}

function Send-GuestText {
    param([string]$Text)
    foreach ($character in $Text.ToCharArray()) {
        $chord = ConvertTo-QcodeChord -Character ([string]$character)
        $keys = @($chord | ForEach-Object { @{ type = 'qcode'; data = $_ } })
        $request = @{ keys = $keys; 'hold-time' = 70 }
        $result = Invoke-Qmp -Execute 'send-key' -Arguments $request
        Write-QmpEvent -Name 'type-character' -Request ((ConvertTo-Json $request -Compress)) -Result $result
        Start-Sleep -Milliseconds 100
    }
    $script:LastInputAtUtc = [DateTime]::UtcNow
    Start-Sleep -Milliseconds 200
    [void](Capture-Screenshot -Label 'input-typed-text')
}

function Send-GuestMouse {
    param([int]$DeltaX, [int]$DeltaY, [switch]$Click)

    if ([Math]::Abs($DeltaX) -gt 5000 -or [Math]::Abs($DeltaY) -gt 5000) { throw 'Mouse movement exceeds the bounded delta of 5000.' }
    $events = [System.Collections.Generic.List[object]]::new()
    if ($DeltaX -ne 0) { $events.Add(@{ type = 'rel'; data = @{ axis = 'x'; value = $DeltaX } }) }
    if ($DeltaY -ne 0) { $events.Add(@{ type = 'rel'; data = @{ axis = 'y'; value = $DeltaY } }) }
    if ($Click) {
        $events.Add(@{ type = 'btn'; data = @{ button = 'left'; down = $true } })
        $events.Add(@{ type = 'btn'; data = @{ button = 'left'; down = $false } })
    }
    if ($events.Count -eq 0) { throw 'Mouse action has no movement or click.' }
    $request = @{ events = @($events.ToArray()) }
    $result = Invoke-Qmp -Execute 'input-send-event' -Arguments $request
    Write-QmpEvent -Name $(if ($Click) { 'mouse-click' } else { 'mouse-move' }) -Request ((ConvertTo-Json $request -Compress)) -Result $result
    $script:LastInputAtUtc = [DateTime]::UtcNow
    Start-Sleep -Milliseconds 220
    [void](Capture-Screenshot -Label 'input-mouse')
}

function Read-SerialDelta {
    param([string]$Path, [hashtable]$State)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $share = [IO.FileShare]([int][IO.FileShare]::ReadWrite -bor [int][IO.FileShare]::Delete)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
    try {
        if ($stream.Length -lt [long]$State.Offset) { $State.Offset = [long]0 }
        $stream.Position = [long]$State.Offset
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true, 4096, $true)
        $text = $reader.ReadToEnd()
        $State.Offset = $stream.Position
        return $text
    } finally { $stream.Dispose() }
}

function Get-QacSnapshot {
    param([string]$CliPath, [string]$Prefix, [string]$Directory)

    $result = [ordered]@{}
    foreach ($command in @('status', 'list', 'queue')) {
        $text = (& $CliPath $command 2>&1 | Out-String)
        $exit = $LASTEXITCODE
        $path = Join-Path $Directory "$Prefix-qac-$command.txt"
        [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
        $result[$command] = [pscustomobject]@{ ExitCode = $exit; Path = $path; Text = $text }
    }
    return $result
}

function Register-QacOutputLine {
    param([string]$Line, [string]$StreamName, [System.Text.StringBuilder]$Buffer, [hashtable]$State)

    [void]$Buffer.AppendLine($Line)
    $destination = if ($StreamName -eq 'stdout') { $State.StdoutPath } else { $State.StderrPath }
    [IO.File]::AppendAllText($destination, $Line + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    if ($StreamName -eq 'stderr') {
        if ($Line -match '^QAC reservation requested: id=([^ ]+)') { $State.ReservationId = $Matches[1] }
        if ($Line -match '^QAC QEMU launched: reservation=([^ ]+) pid=(\d+)') {
            $State.ReservationId = $Matches[1]
            $State.QemuPid = [int]$Matches[2]
            $State.Launched = $true
        }
        if ($Line -match '^QAC QEMU exited: .*exitCode=(-?\d+)') { $State.QemuExitCode = [int]$Matches[1] }
        if ($Line -match '^QAC reservation released:') { $State.Released = $true }
        if ($Line -match '^QAC queued reservation cancelled:|^QAC reservation cancelled before launch:') {
            $State.Cancelled = $true
            $State.QueueCancellationConfirmed = $true
        }
        if ($Line -match '^QAC Recovery Required|^QAC reservation release is not verified:') { $State.RecoveryRequired = $true }
    }
    if ($Line.StartsWith('{', [StringComparison]::Ordinal)) {
        try {
            $event = $Line | ConvertFrom-Json -ErrorAction Stop
            if ($event.reservationId) { $State.ReservationId = [string]$event.reservationId }
            if ($event.state -eq 'Running' -and $event.childProcessId) {
                $State.QemuPid = [int]$event.childProcessId
                $State.ProcessIdentity = [string]$event.processIdentity
                $State.Launched = $true
                $State.ProcessStartTimeUtc = [string]$event.processStartTimeUtc
            }
        } catch { }
    }
    if ($StreamName -eq 'stderr' -and $Line.StartsWith('QAC ', [StringComparison]::Ordinal)) {
        Write-Host $Line -ForegroundColor DarkCyan
        Write-JsonLine -Path $State.HostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'qac-output'; line = $Line })
    }
}

function Receive-QacProcessOutputLine {
    param([System.Threading.Tasks.Task[string]]$PendingLine, [IO.StreamReader]$Reader,
        [System.Text.StringBuilder]$Buffer, [hashtable]$State, [string]$StreamName)

    if ($null -eq $PendingLine) { return [pscustomobject]@{ PendingLine = $null; Finished = $true } }
    if (-not $PendingLine.IsCompleted) { return [pscustomobject]@{ PendingLine = $PendingLine; Finished = $false } }
    $line = $PendingLine.GetAwaiter().GetResult()
    if ($null -eq $line) { return [pscustomobject]@{ PendingLine = $null; Finished = $true } }
    Register-QacOutputLine -Line $line -StreamName $StreamName -Buffer $Buffer -State $State
    return [pscustomobject]@{ PendingLine = $Reader.ReadLineAsync(); Finished = $false }
}

function Assert-ManagedChildOwnership {
    param([string]$ListText, [int]$QacPid, [string]$QemuExecutable)

    $pattern = '(?m)^\s*(?<id>[0-9a-f]{32})\s*\|\s*DeveloperStudio\s*\|\s*acceptance\s*\|\s*Running\s*\|.*?\(PID\s+(?<owner>\d+)\)\s*\|\s*owner Verified\s*\|\s*child PID\s+(?<child>\d+)\s*$'
    $match = [regex]::Match($ListText, $pattern)
    if (-not $match.Success) { throw 'QAC did not report the exact running DeveloperStudio acceptance reservation with verified owner and child.' }
    $reservationId = $match.Groups['id'].Value
    $ownerPid = [int]$match.Groups['owner'].Value
    $childPid = [int]$match.Groups['child'].Value
    if ($ownerPid -ne $QacPid) { throw "QAC owner PID $ownerPid does not match this runner's QAC CLI PID $QacPid." }

    $child = Get-CimInstance Win32_Process -Filter "ProcessId = $childPid"
    if (-not $child -or [int]$child.ParentProcessId -ne $QacPid -or
        [IO.Path]::GetFullPath([string]$child.ExecutablePath) -ine [IO.Path]::GetFullPath($QemuExecutable)) {
        throw 'QEMU is not the expected executable as this QAC CLI process direct child.'
    }
    return [pscustomobject]@{
        ReservationId = $reservationId
        QacCliPid = $QacPid
        QemuPid = $childPid
        QemuPath = [IO.Path]::GetFullPath($QemuExecutable)
        QemuCreationDate = $child.CreationDate
        OwnerVerified = $true
        DirectChildVerified = $true
    }
}

function Read-ConsoleCommand {
    param([string]$Buffer)

    if (-not [Console]::KeyAvailable) { return $null }
    $key = [Console]::ReadKey($true)
    if ($key.Key -eq [ConsoleKey]::Enter) {
        Write-Host ''
        return [pscustomobject]@{ Complete = $true; Text = $Buffer }
    }
    if ($key.Key -eq [ConsoleKey]::Backspace) {
        if ($Buffer.Length -gt 0) { $Buffer = $Buffer.Substring(0, $Buffer.Length - 1); Write-Host "`b `b" -NoNewline }
        return [pscustomobject]@{ Complete = $false; Text = $Buffer }
    }
    if (-not [char]::IsControl($key.KeyChar) -and $Buffer.Length -lt 1024) {
        $Buffer += $key.KeyChar
        Write-Host $key.KeyChar -NoNewline
    }
    return [pscustomobject]@{ Complete = $false; Text = $Buffer }
}

function Show-GuestHelp {
    Write-Host 'Commands: capture <label> | key <qcode[+qcode...]> | type <text> | mouse <dx> <dy> | click <dx> <dy>'
    Write-Host '          observe <bootloader|kernel-startup|desktop-compositor|fat-filesystem|native-elf-loader|package-discovery|native-elf-launch|window-created|initial-frame|mouse-keyboard-input|tools-menu|go-to-file|go-to-line|task-list|navigation-history|clean-app-close|runtime-diagnostics> <PASS|FAIL|UNVERIFIED> <evidence note>'
    Write-Host '          shutdown (after closing Developer Studio normally) | help'
    Write-Host 'QMP replies confirm transport only. Inspect each saved screenshot before recording an observed result.'
}

function Add-OperatorObservation {
    param([string[]]$Words)

    if ($Words.Count -lt 4) { throw 'observe requires a case, result, and a concise visual evidence note.' }
    $case = $Words[1].ToLowerInvariant()
    $allowedCases = @('bootloader', 'kernel-startup', 'desktop-compositor', 'fat-filesystem', 'native-elf-loader',
        'package-discovery', 'native-elf-launch', 'window-created', 'initial-frame', 'mouse-keyboard-input',
        'tools-menu', 'go-to-file', 'go-to-line', 'task-list', 'navigation-history', 'clean-app-close', 'runtime-diagnostics')
    if ($case -notin $allowedCases) { throw "Unknown observation case: $case" }
    $result = $Words[2].ToUpperInvariant()
    if ($result -notin @('PASS', 'FAIL', 'UNVERIFIED')) { throw 'Observation result must be PASS, FAIL, or UNVERIFIED.' }
    if ($script:LastCaptureAtUtc -eq [DateTime]::MinValue -or $script:LastCaptureAtUtc -lt $script:LastInputAtUtc) {
        throw 'Capture a screenshot after the most recent input before recording an observation.'
    }
    $note = ($Words[3..($Words.Count - 1)] -join ' ').Trim()
    if ([string]::IsNullOrWhiteSpace($note)) { throw 'Observation note cannot be empty.' }
    $script:ObservationCases[$case] = [pscustomobject]@{
        case = $case
        result = $result
        note = $note
        screenshot = $script:LastScreenshotPath
        screenshotSha256 = $script:LastScreenshotSha256
        utc = [DateTime]::UtcNow.ToString('o')
        serialEvidence = (Join-Path $script:RunEvidenceDirectory 'serial-observed.log')
        source = 'operator visual/serial observation; QMP response is not treated as guest acceptance'
    }
    $script:EvidenceEvents.Add($script:ObservationCases[$case])
    Write-JsonLine -Path $script:TranscriptPath -Value $script:ObservationCases[$case]
    Write-Host "Recorded $case=$result with screenshot $script:LastScreenshotPath"
}

function Invoke-OperatorCommand {
    param([string]$Line)

    $words = @([regex]::Matches($Line, '"([^"]*)"|\S+') | ForEach-Object {
        if ($_.Groups[1].Success) { $_.Groups[1].Value } else { $_.Value }
    })
    if ($words.Count -eq 0) { return $false }
    switch ($words[0].ToLowerInvariant()) {
        'help' { Show-GuestHelp; return $false }
        'capture' {
            if ($words.Count -lt 2) { throw 'capture requires a short label.' }
            $shot = Capture-Screenshot -Label ($words[1..($words.Count - 1)] -join '-')
            $script:LastScreenshotPath = $shot.path
            $script:LastScreenshotSha256 = $shot.sha256
            return $false
        }
        'key' {
            if ($words.Count -ne 2) { throw 'key requires a QEMU qcode chord such as ctrl+p or enter.' }
            $keys = @($words[1].Split('+'))
            Send-QmpKeys -Keys $keys -Label $words[1]
            return $false
        }
        'type' {
            if ($words.Count -lt 2) { throw 'type requires text.' }
            Send-GuestText -Text ($words[1..($words.Count - 1)] -join ' ')
            return $false
        }
        'mouse' {
            if ($words.Count -ne 3) { throw 'mouse requires bounded relative deltas: mouse <dx> <dy>.' }
            Send-GuestMouse -DeltaX ([int]$words[1]) -DeltaY ([int]$words[2])
            return $false
        }
        'click' {
            if ($words.Count -ne 3) { throw 'click requires bounded relative deltas: click <dx> <dy>.' }
            Send-GuestMouse -DeltaX ([int]$words[1]) -DeltaY ([int]$words[2]) -Click
            return $false
        }
        'observe' { Add-OperatorObservation -Words $words; return $false }
        'shutdown' { return $true }
        default { throw "Unknown command: $($words[0])" }
    }
}

function Close-Qmp {
    if ($script:QmpWriter) { $script:QmpWriter.Dispose(); $script:QmpWriter = $null }
    if ($script:QmpReader) { $script:QmpReader.Dispose(); $script:QmpReader = $null }
    if ($script:QmpClient) { $script:QmpClient.Dispose(); $script:QmpClient = $null }
}

function Test-OwnedQemuGone {
    param([int]$ProcessId)
    if ($ProcessId -le 0) { return $true }
    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId"
    return $null -eq $process
}

$resolvedQac = Resolve-ExistingExecutable -Path $QacCliPath -EnvironmentName 'GUIDEXOS_QAC_CLI' -CommandName 'qac.exe' `
    -DiscoveredFallback 'D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
$resolvedQemu = Resolve-ExistingExecutable -Path $QemuPath -EnvironmentName '' -CommandName 'qemu-system-x86_64.exe' `
    -DiscoveredFallback 'C:\Program Files\qemu\qemu-system-x86_64.exe'
$EspPath = [IO.Path]::GetFullPath($EspPath)
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$ServerRoot = [IO.Path]::GetFullPath($ServerRoot)
$FirmwarePath = Join-Path $ServerRoot 'OVMF.fd'
if ([IO.Path]::GetExtension($resolvedQac) -ine '.exe') { throw "QAC must be a native CLI executable: $resolvedQac" }
if (-not (Test-Path -LiteralPath $FirmwarePath -PathType Leaf)) { throw "Server OVMF firmware is missing: $FirmwarePath" }
if (-not (Test-Path -LiteralPath $QacModulePath -PathType Leaf)) { throw "Established QAC acceptance helper is missing: $QacModulePath" }

$static = Assert-StaticInputs -Root $EspPath
Write-Host "ESP files=$($static.Identity.FileCount) bytes=$($static.Identity.PayloadBytes) sha256=$($static.Identity.Sha256)"
Write-Host "Accepted AMD64 ELF bytes=$($static.AcceptedElfSize) sha256=$($static.AcceptedElfSha256)"
Write-Host "QAC CLI=$resolvedQac"
Write-Host "QEMU=$resolvedQemu"
Write-Host "Firmware=$FirmwarePath"
Write-Host "Fixture=/TOOLS13R1C/workspace (host path: $(Join-Path $EspPath $FixtureRelative))"

if (-not $RunGuest) {
    Write-Host 'PREPARE_ONLY: no QAC reservation requested; no QEMU process launched.'
    exit 0
}

$authorization = Assert-GuestAuthorizations
if ([Console]::IsInputRedirected -or [Console]::IsOutputRedirected) { throw 'RunGuest requires an interactive console for paced, observed guest input.' }
Import-Module $QacModulePath -Force

if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) { $EvidenceRoot = Join-Path ([IO.Path]::GetTempPath()) 'guidexos-tools13r1h' }
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
[void](New-Item -ItemType Directory -Force -Path $EvidenceRoot)
$runId = [Guid]::NewGuid().ToString('N')
$script:RunEvidenceDirectory = Join-Path $EvidenceRoot $runId
$runEspPath = Join-Path $script:RunEvidenceDirectory 'ESP'
[void](New-Item -ItemType Directory -Force -Path $script:RunEvidenceDirectory)
$script:TranscriptPath = Join-Path $script:RunEvidenceDirectory 'qmp-and-observations.jsonl'
$serialPath = Join-Path $script:RunEvidenceDirectory 'serial.log'
$debugconPath = Join-Path $script:RunEvidenceDirectory 'debugcon.log'
$qacStdoutPath = Join-Path $script:RunEvidenceDirectory 'qac.stdout.log'
$qacStderrPath = Join-Path $script:RunEvidenceDirectory 'qac.stderr.log'
$hostTracePath = Join-Path $script:RunEvidenceDirectory 'host-trace.jsonl'
$manifestPath = Join-Path $script:RunEvidenceDirectory 'run-manifest.json'

Write-Host "Copying audited input to run-private FAT staging: $runEspPath"
Copy-Item -LiteralPath $EspPath -Destination $runEspPath -Recurse
$privateIdentity = Get-CanonicalEspIdentity -Root $runEspPath
if ($privateIdentity.Sha256 -cne $ExpectedTreeSha256) { throw 'Run-private ESP copy does not match the audited source.' }

$qmpPort = New-QacLoopbackQmpPort
$qemuArguments = @(
    '-machine', 'pc,usb=off',
    '-drive', "if=pflash,format=raw,readonly=on,file=$FirmwarePath",
    '-drive', "file=fat:rw:$runEspPath,format=raw,if=ide,index=0",
    '-m', '4096M', '-vga', 'std',
    '-serial', "file:$serialPath", '-debugcon', "file:$debugconPath",
    '-display', 'none', '-no-reboot', '-no-shutdown',
    '-qmp', "tcp:127.0.0.1:$qmpPort,server=on,wait=off"
)
$runArguments = New-QacManagedRunArgumentString -Project 'DeveloperStudio' -Mode 'acceptance' -QemuExecutable $resolvedQemu -QemuArguments $qemuArguments
$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $resolvedQac
$startInfo.Arguments = $runArguments
$startInfo.WorkingDirectory = $ServerRoot
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$qacProcess = [Diagnostics.Process]::new()
$qacProcess.StartInfo = $startInfo
$qacStarted = $false
$ownership = $null
$qmpReady = $false
$guestResult = 'INCOMPLETE'
$cleanupVerified = $false
$shutdownReason = 'operator-requested'
$qemuExitCode = $null
$qacExitCode = $null
$reservationId = ''
$qemuPid = 0
$serialState = @{ Offset = [long]0 }
$stdoutBuffer = [Text.StringBuilder]::new()
$stderrBuffer = [Text.StringBuilder]::new()
$qacState = @{
    ReservationId = ''
    QemuPid = 0
    QemuExitCode = $null
    ProcessIdentity = ''
    ProcessStartTimeUtc = ''
    Launched = $false
    Released = $false
    Cancelled = $false
    QueueCancellationConfirmed = $false
    RecoveryRequired = $false
    StdoutPath = $qacStdoutPath
    StderrPath = $qacStderrPath
    HostTracePath = $hostTracePath
}
$stdoutPending = $null
$stderrPending = $null
$stdoutFinished = $false
$stderrFinished = $false
$cancelHandled = $false
$cancelVerificationDeadline = $null
$qmpConnectDeadline = $null
$serialTranscript = [IO.File]::CreateText((Join-Path $script:RunEvidenceDirectory 'serial-observed.log'))
$startedAtUtc = [DateTime]::UtcNow

Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{
    utc = $startedAtUtc.ToString('o'); event = 'launch-prepared';
    inputEspSha256 = $static.Identity.Sha256; privateEspSha256 = $privateIdentity.Sha256;
    qacCli = $resolvedQac; qemu = $resolvedQemu; qacAuthorizationSha256 = $authorization.Sha256;
    qacAuthorization = $authorization.Authorization; qmpPort = $qmpPort; bootCount = 1
})

try {
    if (-not $qacProcess.Start()) { throw 'QAC CLI did not start.' }
    $qacStarted = $true
    $stdoutPending = $qacProcess.StandardOutput.ReadLineAsync()
    $stderrPending = $qacProcess.StandardError.ReadLineAsync()
    Write-Host "QAC CLI PID $($qacProcess.Id) started the single authorized acceptance request."
    $admissionDeadline = [DateTime]::UtcNow.AddSeconds($AdmissionTimeoutSeconds)
    while (-not $qacProcess.HasExited -and -not $qmpReady) {
        $stdoutResult = Receive-QacProcessOutputLine -PendingLine $stdoutPending -Reader $qacProcess.StandardOutput `
            -Buffer $stdoutBuffer -State $qacState -StreamName 'stdout'
        $stdoutPending = $stdoutResult.PendingLine; $stdoutFinished = $stdoutResult.Finished
        $stderrResult = Receive-QacProcessOutputLine -PendingLine $stderrPending -Reader $qacProcess.StandardError `
            -Buffer $stderrBuffer -State $qacState -StreamName 'stderr'
        $stderrPending = $stderrResult.PendingLine; $stderrFinished = $stderrResult.Finished

        if ($qacState.Launched) {
            if ($null -eq $qmpConnectDeadline) { $qmpConnectDeadline = [DateTime]::UtcNow.AddSeconds(30) }
            try { [void](Connect-Qmp -Port $qmpPort -TimeoutSeconds 2); $qmpReady = $true }
            catch { Close-Qmp }
            if (-not $qmpReady -and [DateTime]::UtcNow -ge $qmpConnectDeadline) {
                $shutdownReason = 'qmp-connection-timeout-after-launch'
                break
            }
        }
        if (-not $qmpReady -and -not $qacState.Launched -and [DateTime]::UtcNow -ge $admissionDeadline -and -not $cancelHandled) {
            $shutdownReason = 'qac-admission-timeout'
            if ($qacState.ReservationId) {
                $cancelHandled = $true
                Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'qac-queued-cancel-request'; reservationId = $qacState.ReservationId; reason = $shutdownReason })
                $cancelResult = Invoke-QacCancelReservation -QacCliPath $resolvedQac -ReservationId $qacState.ReservationId -TimeoutSeconds 10
                [IO.File]::WriteAllText((Join-Path $script:RunEvidenceDirectory 'qac-cancel-result.txt'),
                    "exit=$($cancelResult.ExitCode) completed=$($cancelResult.Completed)`n$($cancelResult.Output)`n$($cancelResult.Error)", [Text.UTF8Encoding]::new($false))
                if ($cancelResult.Output) {
                    try {
                        $cancelState = $cancelResult.Output | ConvertFrom-Json -ErrorAction Stop
                        if ($cancelState.state -eq 'Cancelled') { $qacState.Cancelled = $true; $qacState.QueueCancellationConfirmed = $true }
                        if ($cancelState.childProcessId) {
                            $qacState.QemuPid = [int]$cancelState.childProcessId
                            $qacState.ProcessIdentity = [string]$cancelState.processIdentity
                            $qacState.Launched = $true
                        }
                    } catch { }
                }
                $cancelVerificationDeadline = [DateTime]::UtcNow.AddSeconds(15)
            } else {
                $cancelHandled = $true
                Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'qac-admission-unverified'; reason = 'No reservation ID observed; no cancel command issued.' })
                break
            }
        }
        if ($cancelHandled -and -not $qacState.Launched -and $null -ne $cancelVerificationDeadline -and
            [DateTime]::UtcNow -ge $cancelVerificationDeadline -and -not $qacProcess.HasExited) {
            $shutdownReason = 'qac-queued-cancellation-unverified'
            Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'qac-cancellation-unverified'; reservationId = $qacState.ReservationId })
            break
        }
        if (-not $qmpReady) { Start-Sleep -Milliseconds 100 }
    }
    if (-not $qmpReady) {
        if ($qacProcess.HasExited) { $shutdownReason = if ($qacState.QueueCancellationConfirmed) { 'qac-queued-request-cancelled' } else { 'qac-ended-before-qemu-ready' } }
        elseif ($qacState.Launched) { $shutdownReason = 'qmp-connection-failed-after-qemu-launch' }
        throw "QAC did not produce an owned QEMU QMP endpoint ($shutdownReason)."
    }

    $snapshot = $null
    for ($attempt = 0; $attempt -lt 8 -and -not $snapshot; $attempt++) {
        $qacSnapshot = Get-QacSnapshot -CliPath $resolvedQac -Prefix 'owned' -Directory $script:RunEvidenceDirectory
        try { $ownership = Assert-ManagedChildOwnership -ListText $qacSnapshot.list.Text -QacPid $qacProcess.Id -QemuExecutable $resolvedQemu; $snapshot = $qacSnapshot }
        catch { if ($attempt -eq 7) { throw }; Start-Sleep -Milliseconds 300 }
    }
    $reservationId = $ownership.ReservationId
    $qemuPid = $ownership.QemuPid
    Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'ownership-verified'; ownership = $ownership })
    $qmpStatus = Invoke-Qmp -Execute 'query-status'
    Write-QmpEvent -Name 'initial-query-status' -Request '{"execute":"query-status"}' -Result $qmpStatus
    $initialShot = Capture-Screenshot -Label 'qmp-ready'
    $script:LastScreenshotPath = $initialShot.path
    $script:LastScreenshotSha256 = $initialShot.sha256
    Show-GuestHelp

    $runtimeDeadline = [DateTime]::UtcNow.AddSeconds($GuestRuntimeSeconds)
    $lineBuffer = ''
    while (-not $qacProcess.HasExited -and [DateTime]::UtcNow -lt $runtimeDeadline) {
        $stdoutResult = Receive-QacProcessOutputLine -PendingLine $stdoutPending -Reader $qacProcess.StandardOutput `
            -Buffer $stdoutBuffer -State $qacState -StreamName 'stdout'
        $stdoutPending = $stdoutResult.PendingLine; $stdoutFinished = $stdoutResult.Finished
        $stderrResult = Receive-QacProcessOutputLine -PendingLine $stderrPending -Reader $qacProcess.StandardError `
            -Buffer $stderrBuffer -State $qacState -StreamName 'stderr'
        $stderrPending = $stderrResult.PendingLine; $stderrFinished = $stderrResult.Finished

        $delta = Read-SerialDelta -Path $serialPath -State $serialState
        if ($delta.Length -gt 0) {
            $serialTranscript.Write($delta)
            $serialTranscript.Flush()
            if ($delta -match '(?i)(kernel panic|page fault|general protection fault|unhandled exception|native elf.*(exception|terminated)|stack.*(overflow|failure|corruption|guard)|heap (allocation|corruption).*(failed|error|fault)|allocation.*failed|file open.*(failed|error))') {
                $shutdownReason = 'fatal-serial-diagnostic'
                Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'fatal-serial-diagnostic'; text = $delta })
                break
            }
        }
        if ([DateTime]::UtcNow -ge $runtimeDeadline) { $shutdownReason = 'guest-runtime-timeout'; break }
        if ([Console]::KeyAvailable) {
            $entry = Read-ConsoleCommand -Buffer $lineBuffer
            if ($null -ne $entry) {
                $lineBuffer = $entry.Text
                if ($entry.Complete) {
                    $commandLine = $lineBuffer.Trim()
                    $lineBuffer = ''
                    try {
                        $stop = Invoke-OperatorCommand -Line $commandLine
                        if ($stop) { break }
                    } catch {
                        Write-Host "Command rejected: $($_.Exception.Message)" -ForegroundColor Yellow
                        Write-JsonLine -Path $script:TranscriptPath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); action = 'operator-command-error'; command = $commandLine; error = $_.Exception.Message })
                    }
                }
            }
        }
        Start-Sleep -Milliseconds 100
    }

    if ($qacStarted -and $qacProcess.HasExited -and $shutdownReason -eq 'operator-requested') { $shutdownReason = 'qemu-exited-before-runner-shutdown' }
    if ($qacStarted -and -not $qacProcess.HasExited -and $script:QmpWriter) {
        try {
            $quit = Invoke-Qmp -Execute 'quit' -TimeoutSeconds 5
            Write-QmpEvent -Name 'qmp-quit' -Request '{"execute":"quit"}' -Result $quit
            Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'qmp-quit-sent'; reason = $shutdownReason })
        } catch {
            Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'qmp-quit-failed'; reason = $shutdownReason; error = $_.Exception.Message })
        }
    }
    Close-Qmp
    if ($qacStarted -and -not $qacProcess.HasExited) { [void]$qacProcess.WaitForExit(15000) }
    if ($qacStarted -and $qacProcess.HasExited) {
        $qacProcess.WaitForExit()
        $qacExitCode = $qacProcess.ExitCode
        $qemuExitCode = $qacState.QemuExitCode
        $cleanupVerified = $qacExitCode -eq 0 -and $qacState.Released -and -not $qacState.RecoveryRequired
    }
    $guestResult = 'INCOMPLETE'
} catch {
    $failure = $_.Exception.Message
    Write-Host "Runner stopped: $failure" -ForegroundColor Yellow
    if ($script:QmpWriter) {
        try { $quit = Invoke-Qmp -Execute 'quit' -TimeoutSeconds 5; Write-QmpEvent -Name 'qmp-quit-after-failure' -Request '{"execute":"quit"}' -Result $quit } catch { }
    } elseif ($qacStarted -and $qacState.Launched -and -not $qacProcess.HasExited) {
        try {
            [void](Send-QacQmpQuit -Port $qmpPort -TimeoutSeconds 3)
            Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'qmp-quit-after-qmp-connect-failure'; qemuPid = $qacState.QemuPid })
        } catch {
            Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'qmp-quit-failed-after-connect-failure'; qemuPid = $qacState.QemuPid; error = $_.Exception.Message })
        }
    }
    Close-Qmp
    if ($qacStarted -and -not $qacProcess.HasExited) { [void]$qacProcess.WaitForExit(15000) }
    if ($qacStarted -and $qacProcess.HasExited) {
        $qacProcess.WaitForExit()
        $qacExitCode = $qacProcess.ExitCode
        $qemuExitCode = $qacState.QemuExitCode
        $cleanupVerified = $qacExitCode -eq 0 -and $qacState.Released -and -not $qacState.RecoveryRequired
    }
    $guestResult = 'INCOMPLETE'
    Write-JsonLine -Path $hostTracePath -Value ([pscustomobject]@{ utc = [DateTime]::UtcNow.ToString('o'); event = 'runner-failure'; error = $failure })
} finally {
    try { $serialTranscript.Dispose() } catch { }
    if ($qacStarted -and $qacProcess.HasExited) {
        try {
            $qacProcess.WaitForExit()
            for ($drain = 0; $drain -lt 200 -and (-not $stdoutFinished -or -not $stderrFinished); $drain++) {
                $stdoutResult = Receive-QacProcessOutputLine -PendingLine $stdoutPending -Reader $qacProcess.StandardOutput `
                    -Buffer $stdoutBuffer -State $qacState -StreamName 'stdout'
                $stdoutPending = $stdoutResult.PendingLine; $stdoutFinished = $stdoutResult.Finished
                $stderrResult = Receive-QacProcessOutputLine -PendingLine $stderrPending -Reader $qacProcess.StandardError `
                    -Buffer $stderrBuffer -State $qacState -StreamName 'stderr'
                $stderrPending = $stderrResult.PendingLine; $stderrFinished = $stderrResult.Finished
                Start-Sleep -Milliseconds 10
            }
            $qacExitCode = $qacProcess.ExitCode
            $qemuExitCode = $qacState.QemuExitCode
        } catch { }
    }
}

$reservationId = if ($qacState.ReservationId) { $qacState.ReservationId } else { $reservationId }
$qemuPid = if ($qemuPid -gt 0) { $qemuPid } else { [int]$qacState.QemuPid }
$finalCensus = Get-QacSnapshot -CliPath $resolvedQac -Prefix 'final' -Directory $script:RunEvidenceDirectory
$reservationInspection = $null
if ($reservationId) {
    $inspectText = (& $resolvedQac inspect $reservationId 2>&1 | Out-String)
    $inspectExit = $LASTEXITCODE
    $inspectPath = Join-Path $script:RunEvidenceDirectory 'final-qac-inspect.json'
    [IO.File]::WriteAllText($inspectPath, $inspectText, [Text.UTF8Encoding]::new($false))
    try { $reservationInspection = $inspectText | ConvertFrom-Json } catch { }
    $reservationTerminal = $false
    if ($inspectExit -eq 0 -and $reservationInspection -and
        ($reservationInspection.PSObject.Properties.Name -contains 'state')) {
        $stateName = [string]$reservationInspection.state
        $reservationTerminal = if ($qacState.Launched) { $stateName -eq 'Released' } else { $stateName -in @('Cancelled', 'Released', 'Expired') }
    }
    $cleanupVerified = $reservationTerminal -and -not $qacState.RecoveryRequired -and
        $finalCensus.list.Text -notmatch [regex]::Escape($reservationId) -and (Test-OwnedQemuGone -ProcessId $qemuPid)
} elseif ($qacStarted -and $qacProcess.HasExited -and -not $qacState.Launched -and -not $qacState.ReservationId) {
    $cleanupVerified = $true
}
$observations = @($script:ObservationCases.Values | Sort-Object case)
$manifest = [pscustomobject]@{
    task = 'TOOLS13R1H'
    runId = $runId
    startedAtUtc = $startedAtUtc.ToString('o')
    completedAtUtc = [DateTime]::UtcNow.ToString('o')
    bootCount = 1
    inputEspPath = $EspPath
    inputEspSha256 = $static.Identity.Sha256
    privateEspPath = $runEspPath
    preSpawnPrivateEspSha256 = $privateIdentity.Sha256
    acceptedElfSize = $static.AcceptedElfSize
    acceptedElfSha256 = $static.AcceptedElfSha256
    qacCli = $resolvedQac
    qacCliPid = if ($qacStarted) { $qacProcess.Id } else { $null }
    qacReservationId = $reservationId
    qemuPath = $resolvedQemu
    qemuPid = $qemuPid
    qemuExitCode = $qemuExitCode
    qacExitCode = $qacExitCode
    shutdownReason = $shutdownReason
    reservationInspection = $reservationInspection
    cleanupVerified = $cleanupVerified
    operatorObservations = $observations
    qualificationOutcome = if ($qacState.Launched) { 'INCOMPLETE_REQUIRES_SCREENSHOT_AND_SERIAL_REVIEW' } else { 'NO_GUEST_RUN' }
    optionalTestsAttempted = $false
}
[IO.File]::WriteAllText($manifestPath, (ConvertTo-Json -InputObject $manifest -Depth 12), [Text.UTF8Encoding]::new($false))

if ($qacStarted -and -not $qacProcess.HasExited) {
    Write-Host "CLEANUP INCOMPLETE: QAC CLI PID $($qacProcess.Id) remains under QAC ownership. No process was forcibly terminated. Evidence: $script:RunEvidenceDirectory" -ForegroundColor Red
} else {
    Write-Host "QAC CLI exit=$qacExitCode; reservation=$reservationId; cleanupVerified=$cleanupVerified"
    if ($qacState.Launched) { Write-Host "Guest outcome remains INCOMPLETE until screenshots and serial evidence are reviewed: $script:RunEvidenceDirectory" }
    else { Write-Host "No guest boot occurred; QAC admission result and cleanup evidence are retained at $script:RunEvidenceDirectory" }
}

if ($qacStarted) { $qacProcess.Dispose() }
if (-not $cleanupVerified) { exit 2 }
exit 0
