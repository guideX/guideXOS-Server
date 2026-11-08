[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ArtifactDirectory,
    [string]$QacCliPath = '',
    [ValidateRange(1, 3)] [int]$Boots = 3,
    [int]$RuntimeSeconds = 900,
    [int]$PostStudioExitSeconds = 20
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
$runnerScript = Join-Path $PSScriptRoot 'test-aarch64-studio-abi3-qemu.ps1'
$inputScript = Join-Path $PSScriptRoot 'send-aarch64-qmp-studio-input.ps1'
$powerShellExe = (Get-Command powershell.exe -ErrorAction Stop).Source
$validSource = 'int gx_main(void* ctx) { return 42; }'
$invalidSource = 'int gx_main(void* ctx) { return 42 }'

function Quote-ProcessArgument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) { ++$slashes; continue }
        if ($character -eq [char]34) {
            [void]$builder.Append([char]92, (2 * $slashes + 1))
            [void]$builder.Append([char]34)
            $slashes = 0
            continue
        }
        if ($slashes -gt 0) { [void]$builder.Append([char]92, $slashes) }
        [void]$builder.Append($character)
        $slashes = 0
    }
    if ($slashes -gt 0) { [void]$builder.Append([char]92, (2 * $slashes)) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Read-GuestText([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
            try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    } catch { return '' }
}

function Wait-GuestText([string]$GuestLog, [string]$Marker, [Diagnostics.Process]$Runner,
                        [int]$TimeoutSeconds = 30) {
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $text = Read-GuestText $GuestLog
        if ($text.Contains($Marker)) { return $text }
        if ($text -match 'AARCH64_PHASE12_ERROR|\[guideXOS\].*(FATAL|ERROR)') {
            throw "Guest reported an error before '$Marker'. See $GuestLog"
        }
        if ($Runner.HasExited) { throw "Studio runner exited before '$Marker'. See $GuestLog" }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for guest evidence '$Marker'. See $GuestLog"
}

function Wait-GuestOccurrence([string]$GuestLog, [string]$Marker, [int]$Count,
                              [Diagnostics.Process]$Runner, [int]$TimeoutSeconds = 30) {
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $text = Read-GuestText $GuestLog
        if ([regex]::Matches($text, [regex]::Escape($Marker)).Count -ge $Count) { return $text }
        if ($text -match 'AARCH64_PHASE12_ERROR|\[guideXOS\].*(FATAL|ERROR)') {
            throw "Guest reported an error before occurrence $Count of '$Marker'. See $GuestLog"
        }
        if ($Runner.HasExited) { throw "Studio runner exited before occurrence $Count of '$Marker'. See $GuestLog" }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for occurrence $Count of '$Marker'. See $GuestLog"
}

function Wait-BuildTerminal([string]$GuestLog, [int]$SuccessBefore, [int]$FailureBefore,
                            [Diagnostics.Process]$Runner, [int]$TimeoutSeconds = 120) {
    $successMarker = 'GUIDEXOS_DEVELOPER_STUDIO_MARKER build_complete=SUCCEEDED'
    $failureMarker = 'GUIDEXOS_DEVELOPER_STUDIO_MARKER build_complete=FAILED'
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTimeOffset]::UtcNow -lt $deadline) {
        $text = Read-GuestText $GuestLog
        if ((Get-Count $text $successMarker) -gt $SuccessBefore -or
            (Get-Count $text $failureMarker) -gt $FailureBefore) { return $text }
        if ($text -match 'AARCH64_PHASE12_ERROR|\[guideXOS\].*(FATAL|ERROR)') {
            throw "Guest reported an error while waiting for the Build callback to complete. See $GuestLog"
        }
        if ($Runner.HasExited) { throw "Studio runner exited before the Build callback completed. See $GuestLog" }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for a new Build terminal marker. See $GuestLog"
}

function Send-StudioInput([string]$SessionPath, [string]$QacPath, [hashtable]$InputArguments) {
    $arguments = @{ SessionPath = $SessionPath }
    if (![string]::IsNullOrWhiteSpace($QacPath)) { $arguments.QacCliPath = $QacPath }
    foreach ($key in $InputArguments.Keys) { $arguments[$key] = $InputArguments[$key] }
    & $inputScript @arguments | Out-Host
    Start-Sleep -Milliseconds 300
}

function Clear-EditorBuffer([string]$SessionPath, [string]$QacPath, [string]$CapturePath = '') {
    # The Studio editor has no select-all command and the guest does not route
    # QMP Home/End keys to its text buffer. Click below the visible lines so
    # placeCaretFromMouse clamps to the final line, then click beyond the text
    # so it clamps to that line's end. Bounded backspaces clear the whole file.
    $keys = @('Backspace') * 160
    $arguments = @{ ClickX = 790; ClickY = 200; SpecialKeys = $keys; KeyDelayMilliseconds = 25 }
    if (![string]::IsNullOrWhiteSpace($CapturePath)) { $arguments.CapturePath = $CapturePath }
    Send-StudioInput $SessionPath $QacPath $arguments
}

function Assert-SavedSource([string]$GuestText, [string]$SourcePath, [string]$ExpectedText) {
    $pattern = '(?m)^\[developer-studio-app\] ABI3_STUDIO_VFS_IO action=write path=' +
        [regex]::Escape($SourcePath) + ' bytes=(\d+)(?: datahex=([0-9a-f]+)| fnv1a32=([0-9a-f]{8}))\r?$'
    $writes = [regex]::Matches($GuestText, $pattern)
    if ($writes.Count -eq 0) { throw "Studio did not report a VFS save for $SourcePath." }
    $lastWrite = $writes[$writes.Count - 1]
    $expectedBytes = [Text.Encoding]::UTF8.GetBytes($ExpectedText)
    $expectedHex = [BitConverter]::ToString($expectedBytes).Replace('-', '').ToLowerInvariant()
    [uint64]$expectedHash = 2166136261
    foreach ($byte in $expectedBytes) {
        $expectedHash = (($expectedHash -bxor [uint64]$byte) * 16777619) -band [uint64]4294967295
    }
    $expectedFnv = $expectedHash.ToString('x8')
    $exactBytesMatch = if ($lastWrite.Groups[2].Success) {
        $lastWrite.Groups[2].Value -ceq $expectedHex
    } else {
        $lastWrite.Groups[3].Value -ceq $expectedFnv
    }
    if ([int]$lastWrite.Groups[1].Value -ne $expectedBytes.Length -or !$exactBytesMatch) {
        throw "Studio saved unexpected source bytes to $SourcePath (actual length $($lastWrite.Groups[1].Value)); expected $($expectedBytes.Length) bytes."
    }
}

function Save-Workflow([string]$Path, [hashtable]$Record) {
    $Record.updated_utc = [DateTimeOffset]::UtcNow.ToString('o')
    $Record | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Get-Count([string]$Text, [string]$Marker) {
    return [regex]::Matches($Text, [regex]::Escape($Marker)).Count
}

function Invoke-StudioBoot([int]$BootNumber) {
    $existing = @{}
    Get-ChildItem -LiteralPath $ArtifactDirectory -Directory -Filter 'abi3-guest-*' -ErrorAction SilentlyContinue |
        ForEach-Object { $existing[$_.FullName] = $true }

    $runnerArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runnerScript,
        '-ArtifactDirectory', $ArtifactDirectory, '-RuntimeSeconds', [string]$RuntimeSeconds,
        '-PostStudioExitSeconds', [string]$PostStudioExitSeconds, '-R2B1ExternalWorkflow')
    if (![string]::IsNullOrWhiteSpace($QacCliPath)) {
        $runnerArguments += @('-QacCliPath', [IO.Path]::GetFullPath($QacCliPath))
    }
    $argumentString = ($runnerArguments | ForEach-Object { Quote-ProcessArgument ([string]$_) }) -join ' '
    $launchToken = [guid]::NewGuid().ToString('N')
    $runnerStdout = Join-Path $ArtifactDirectory ("r2b1-launch-boot-{0}-{1}.stdout.log" -f $BootNumber, $launchToken)
    $runnerStderr = Join-Path $ArtifactDirectory ("r2b1-launch-boot-{0}-{1}.stderr.log" -f $BootNumber, $launchToken)
    $runner = Start-Process -FilePath $powerShellExe -ArgumentList $argumentString -PassThru -WindowStyle Hidden `
        -WorkingDirectory $repoRoot -RedirectStandardOutput $runnerStdout -RedirectStandardError $runnerStderr
    $deadline = [DateTimeOffset]::UtcNow.AddSeconds(180)
    $sessionPath = ''
    $session = $null
    while ([DateTimeOffset]::UtcNow -lt $deadline -and !$runner.HasExited) {
        $newDirectory = Get-ChildItem -LiteralPath $ArtifactDirectory -Directory -Filter 'abi3-guest-*' -ErrorAction SilentlyContinue |
            Where-Object { !$existing.ContainsKey($_.FullName) } |
            Sort-Object CreationTimeUtc -Descending | Select-Object -First 1
        if ($newDirectory) {
            $candidateSessionPath = Join-Path $newDirectory.FullName 'session.json'
            if (Test-Path -LiteralPath $candidateSessionPath -PathType Leaf) {
                # The managed runner updates this record while starting QAC.
                # Read with FileShare.ReadWrite so observing it never blocks
                # the runner's Set-Content write on Windows.
                try { $candidateSession = (Read-GuestText $candidateSessionPath) | ConvertFrom-Json }
                catch { $candidateSession = $null }
                if ($candidateSession -and $candidateSession.outcome -eq 'ABI3_STUDIO_GUEST_IN_PROGRESS') {
                    $sessionPath = $candidateSessionPath
                    $session = $candidateSession
                    break
                }
            }
        }
        Start-Sleep -Milliseconds 200
    }
    if (!$session) {
        if (!$runner.HasExited) { $runner.WaitForExit(1000) | Out-Null }
        $stdoutText = if (Test-Path -LiteralPath $runnerStdout -PathType Leaf) { Get-Content -LiteralPath $runnerStdout -Raw } else { '' }
        $stderrText = if (Test-Path -LiteralPath $runnerStderr -PathType Leaf) { Get-Content -LiteralPath $runnerStderr -Raw } else { '' }
        throw "Boot $BootNumber did not produce an in-progress QAC session record. Runner stdout: $stdoutText Runner stderr: $stderrText"
    }

    $sessionDirectory = Split-Path -Parent $sessionPath
    $guestLog = [string]$session.guest_log
    $qacPath = if (![string]::IsNullOrWhiteSpace($QacCliPath)) { [IO.Path]::GetFullPath($QacCliPath) } else { '' }
    $workflowPath = Join-Path $sessionDirectory 'r2b1-workflow.json'
    $projectFolder = 'R2B1B{0:d2}' -f $BootNumber
    $outputName = "$($projectFolder.ToLowerInvariant())-guidexos"
    $sourcePath = "/Apps/$projectFolder/src/main.cpp"
    $expectedHex = [BitConverter]::ToString([Text.Encoding]::ASCII.GetBytes($validSource)).Replace('-', '').ToLowerInvariant()
    $record = [ordered]@{
        outcome = 'STUDIO_R2B1_GUEST_IN_PROGRESS'
        boot_number = $BootNumber
        project_folder = $projectFolder
        project_output_name = $outputName
        project_source_path = $sourcePath
        qac_reservation_id = [string]$session.reservation_id
        session_record = $sessionPath
        guest_log = $guestLog
        candidate_source_head = [string]$session.candidate_source_head
        candidate_manifest_sha256 = [string]$session.candidate_manifest_sha256
        candidate_kernel_sha256 = [string]$session.candidate_kernel_sha256
        candidate_efi_sha256 = [string]$session.candidate_efi_sha256
        candidate_ramdisk_sha256 = [string]$session.candidate_ramdisk_sha256
        source_text = $validSource
        source_write_bytes = [Text.Encoding]::ASCII.GetByteCount($validSource)
        source_write_datahex = $expectedHex
        transitions = [ordered]@{}
        screenshots = @()
    }
    Save-Workflow $workflowPath $record

    $exitRequested = $false
    try {
        $null = Wait-GuestText $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER initial_render=PASS' $runner 120
        $record.transitions.studio_initial_render = $true
        $text = Read-GuestText $guestLog
        $projectCreateBefore = Get-Count $text 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_create=PASS'
        $projectOpenBefore = Get-Count $text 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_open=PASS'
        $metadataBefore = Get-Count $text 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_metadata_parse=PASS'
        $createRequestBefore = Get-Count $text 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_create_request=PASS'

        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlN'); CapturePath = (Join-Path $sessionDirectory 'r2b1-new-project.png') }
        $null = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_create_request=PASS' ($createRequestBefore + 1) $runner
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Tab') }
        Send-StudioInput $sessionPath $qacPath @{ Text = '/Apps' }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Tab') }
        Send-StudioInput $sessionPath $qacPath @{ Text = $projectFolder }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Tab') }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Enter'); CapturePath = (Join-Path $sessionDirectory 'r2b1-project-created.png') }
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_create=PASS' ($projectCreateBefore + 1) $runner 45
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_open=PASS' ($projectOpenBefore + 1) $runner 45
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_metadata_parse=PASS' ($metadataBefore + 1) $runner 45
        $record.transitions.authentic_studio_project_create_open = $true
        $record.screenshots += @('r2b1-new-project.png', 'r2b1-project-created.png')

        # Studio creates and opens src/main.cpp with its NativeElf starter.
        # Replace that editor buffer through normal keyboard input and save it
        # through the pinned Studio VFS callback before invoking Build.
        Clear-EditorBuffer $sessionPath $qacPath (Join-Path $sessionDirectory 'r2b1-source-cleared-initial.png')
        $record.screenshots += 'r2b1-source-cleared-initial.png'
        Send-StudioInput $sessionPath $qacPath @{ Text = $validSource }
        $saveMarker = 'GUIDEXOS_DEVELOPER_STUDIO_MARKER document_save=PASS name=main.cpp'
        $saveBefore = Get-Count (Read-GuestText $guestLog) $saveMarker
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlS'); CapturePath = (Join-Path $sessionDirectory 'r2b1-source-saved.png') }
        $text = Wait-GuestOccurrence $guestLog $saveMarker ($saveBefore + 1) $runner 45
        Assert-SavedSource $text $sourcePath $validSource
        $sourceWritePattern = '(?m)^.*ABI3_STUDIO_VFS_IO action=write path=' + [regex]::Escape($sourcePath) +
            ' bytes=' + $record.source_write_bytes + ' datahex=' + [regex]::Escape($expectedHex) + '\r?$'
        $sourceWrite = [regex]::Match($text, $sourceWritePattern)
        if (!$sourceWrite.Success) { throw 'Studio save did not write the exact compiler source bytes into the guest VFS.' }
        $record.transitions.exact_source_saved_to_guest_vfs = $true
        $record.source_write_evidence = $sourceWrite.Value.Trim()
        $record.screenshots += 'r2b1-source-saved.png'

        $buildComplete = 'GUIDEXOS_DEVELOPER_STUDIO_MARKER build_complete=SUCCEEDED'
        $buildFailed = 'GUIDEXOS_DEVELOPER_STUDIO_MARKER build_complete=FAILED'
        $artifactValidation = 'GUIDEXOS_DEVELOPER_STUDIO_MARKER build_artifact_validation=PASS'
        $text = Read-GuestText $guestLog
        $buildSuccessBefore = Get-Count $text $buildComplete
        $buildFailureBefore = Get-Count $text $buildFailed
        $artifactValidationBefore = Get-Count $text $artifactValidation
        $artifactDigestBefore = Get-Count $text 'BareMetalBuild: artifact SHA-256='
        $storagePathMarker = 'BareMetalBuild: artifact storage path='
        $storagePathBefore = Get-Count $text $storagePathMarker
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlShiftB'); CapturePath = (Join-Path $sessionDirectory 'r2b1-build-started.png') }
        $text = Wait-BuildTerminal $guestLog $buildSuccessBefore $buildFailureBefore $runner 120
        if ((Get-Count $text $buildComplete) -le $buildSuccessBefore) {
            $serviceError = [regex]::Match($text, '(?m)^BareMetalBuild: error=.*$').Value
            throw "Authentic Studio Build returned FAILED. $serviceError"
        }
        $expectedArtifact = "/Apps/$projectFolder/build/bin/arm64/$outputName.elf"
        $expectedStorageDirectory = "/Apps/$projectFolder/build/bin/arm64/"
        $storagePathMatches = [regex]::Matches($text, '(?m)^BareMetalBuild: artifact storage path=(/Apps/[^\r\n]+)\r?$')
        if ($storagePathMatches.Count -le $storagePathBefore) {
            throw 'Authentic Studio Build did not report a fresh FAT storage path for the logical ELF artifact.'
        }
        $expectedStorageArtifact = $storagePathMatches[$storagePathMatches.Count - 1].Groups[1].Value
        $storageBasename = [IO.Path]::GetFileName($expectedStorageArtifact)
        if (!$expectedStorageArtifact.StartsWith($expectedStorageDirectory, [StringComparison]::OrdinalIgnoreCase) -or
            $storageBasename -notmatch '^[0-9A-F]{8}\.ELF$') {
            throw "Compiler storage path was not the bounded per-project FAT alias: $expectedStorageArtifact"
        }
        $digestMatches = [regex]::Matches($text, 'BareMetalBuild: artifact SHA-256=([0-9a-f]{64})')
        if ((Get-Count $text $artifactValidation) -le $artifactValidationBefore -or
            $text -notmatch ('Compiler: source=' + [regex]::Escape($sourcePath) + ' output=' + [regex]::Escape($expectedStorageArtifact)) -or
            $text -notmatch 'Compiler: produced ELF validation PASS entry=0x(?:0x)?0000000051001000' -or
            $text -notmatch 'Compiler: reopened_bytes=\d+ reopened_hash=fnv1a64:(?:0x)?[0-9a-f]{16} readback ELF validation PASS' -or
            $text -notmatch ('\[VFS_WRITE_FILE\] path=' + [regex]::Escape($expectedStorageArtifact) + ' operation=(create|overwrite)') -or
            $text -notmatch ('BareMetalBuild: artifact=' + [regex]::Escape("build/bin/arm64/$outputName.elf")) -or
            $digestMatches.Count -le $artifactDigestBefore) {
            throw 'Authentic Studio Build completed without the required ARM64 compiler, VFS readback, artifact path, and SHA-256 evidence.'
        }
        $record.first_artifact_sha256 = $digestMatches[$digestMatches.Count - 1].Groups[1].Value
        $record.first_artifact_path = $expectedArtifact
        $record.first_artifact_storage_path = $expectedStorageArtifact
        $record.transitions.authentic_studio_arm64_build = $true
        $record.transitions.arm64_entry_and_strict_base = $true
        $record.transitions.artifact_vfs_readback = $true
        $record.transitions.artifact_sha256_reported = $true
        $record.screenshots += 'r2b1-build-started.png'
        Save-Workflow $workflowPath $record

        # A malformed rebuild must fail with a source location and remove the
        # stale project-local ELF while leaving the last package untouched.
        Clear-EditorBuffer $sessionPath $qacPath (Join-Path $sessionDirectory 'r2b1-source-cleared-malformed.png')
        $record.screenshots += 'r2b1-source-cleared-malformed.png'
        Send-StudioInput $sessionPath $qacPath @{ Text = $invalidSource }
        $saveBefore = Get-Count (Read-GuestText $guestLog) $saveMarker
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlS') }
        $text = Wait-GuestOccurrence $guestLog $saveMarker ($saveBefore + 1) $runner 45
        Assert-SavedSource $text $sourcePath $invalidSource
        $buildFailureBefore = Get-Count (Read-GuestText $guestLog) $buildFailed
        $text = Read-GuestText $guestLog
        $validationBefore = Get-Count $text $artifactValidation
        $staleCleanupBefore = Get-Count $text 'BareMetalBuild: stale target outputs removed before rebuild'
        $malformedErrorMarker = 'BareMetalBuild: error=ARM64: unsupported source, encoding, or ELF emission failure'
        $malformedErrorBefore = Get-Count $text $malformedErrorMarker
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlShiftB') }
        $text = Wait-GuestOccurrence $guestLog $buildFailed ($buildFailureBefore + 1) $runner 120
        $text = Wait-GuestOccurrence $guestLog 'BareMetalBuild: stale target outputs removed before rebuild' ($staleCleanupBefore + 1) $runner 30
        $text = Wait-GuestOccurrence $guestLog $malformedErrorMarker ($malformedErrorBefore + 1) $runner 30
        if ($text -notmatch '(?m)^error: line \d+, column \d+, offset \d+:') {
            throw 'Malformed-source build failed without a compiler source-location diagnostic.'
        }
        if ((Get-Count $text $artifactValidation) -ne $validationBefore) {
            throw 'Malformed-source build reported a successful artifact validation after failure.'
        }
        $record.transitions.malformed_source_rejected_with_location = $true
        $record.transitions.stale_output_removed = $true
        $record.transitions.no_false_artifact_success = $true

        # Restore valid source, rebuild, and verify the artifact digest changes.
        $recoveredSource = 'int gx_main(void* ctx) { return 43; }'
        Clear-EditorBuffer $sessionPath $qacPath (Join-Path $sessionDirectory 'r2b1-source-cleared-recovery.png')
        $record.screenshots += 'r2b1-source-cleared-recovery.png'
        Send-StudioInput $sessionPath $qacPath @{ Text = $recoveredSource }
        $saveBefore = Get-Count (Read-GuestText $guestLog) $saveMarker
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlS') }
        $text = Wait-GuestOccurrence $guestLog $saveMarker ($saveBefore + 1) $runner 45
        Assert-SavedSource $text $sourcePath $recoveredSource
        $text = Read-GuestText $guestLog
        $buildSuccessBefore = Get-Count $text $buildComplete
        $buildFailureBefore = Get-Count $text $buildFailed
        $digestBeforeRecovery = Get-Count $text 'BareMetalBuild: artifact SHA-256='
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlShiftB') }
        $text = Wait-BuildTerminal $guestLog $buildSuccessBefore $buildFailureBefore $runner 120
        if ((Get-Count $text $buildComplete) -le $buildSuccessBefore) {
            $serviceError = [regex]::Match($text, '(?m)^BareMetalBuild: error=.*$').Value
            throw "Recovery Build returned FAILED. $serviceError"
        }
        $digestMatches = [regex]::Matches($text, 'BareMetalBuild: artifact SHA-256=([0-9a-f]{64})')
        if ($digestMatches.Count -le $digestBeforeRecovery) { throw 'Successful rebuild did not report a new artifact SHA-256.' }
        $digestBeforeRecoveryMatches = [regex]::Matches($text, 'BareMetalBuild: artifact SHA-256=([0-9a-f]{64})')
        if ($digestBeforeRecovery -lt 1) { throw 'Initial successful build digest was not present in the guest log.' }
        $firstDigest = $digestBeforeRecoveryMatches[$digestBeforeRecovery - 1].Groups[1].Value
        $recoveredDigest = $digestMatches[$digestMatches.Count - 1].Groups[1].Value
        if ($firstDigest -eq $recoveredDigest) { throw 'Changing the application return value did not change the ARM64 artifact SHA-256.' }
        $record.recovered_source_text = $recoveredSource
        $record.recovered_artifact_sha256 = $recoveredDigest
        $record.transitions.rebuild_after_failure = $true
        $record.transitions.changed_source_changes_artifact_digest = $true
        Save-Workflow $workflowPath $record

        # Close this project and open the staged multi-target fixture whose
        # missing package parent forces a meaningful guest VFS publish error.
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 300; ClickY = 57; CapturePath = (Join-Path $sessionDirectory 'r2b1-close-project-menu.png') }
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 330; ClickY = 177; CapturePath = (Join-Path $sessionDirectory 'r2b1-project-closed.png') }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlShiftO'); CapturePath = (Join-Path $sessionDirectory 'r2b1-open-vfs-project.png') }
        $text = Read-GuestText $guestLog
        $projectOpenBefore = Get-Count $text 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_open=PASS'
        $metadataBefore = Get-Count $text 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_metadata_parse=PASS'
        Send-StudioInput $sessionPath $qacPath @{ Text = '/Phase12VfsProof' }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Enter') }
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_open=PASS' ($projectOpenBefore + 1) $runner 45
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_metadata_parse=PASS' ($metadataBefore + 1) $runner 45
        $record.transitions.staged_vfs_failure_project_open = $true
        $vfsSourceMarker = 'ABI3_STUDIO_VFS_IO action=write path=/Phase12VfsProof/src/main.cpp'
        $vfsWriteBefore = Get-Count (Read-GuestText $guestLog) $vfsSourceMarker
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlS') }
        $null = Wait-GuestOccurrence $guestLog $vfsSourceMarker ($vfsWriteBefore + 1) $runner 30
        $text = Read-GuestText $guestLog
        $failedBefore = Get-Count $text $buildComplete
        $failureBefore = Get-Count $text $buildFailed
        $vfsErrorMarker = 'BareMetalBuild: error=VFS package output path was unavailable'
        $vfsErrorBefore = Get-Count $text $vfsErrorMarker
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlShiftB'); CapturePath = (Join-Path $sessionDirectory 'r2b1-vfs-build.png') }
        $text = Wait-GuestOccurrence $guestLog $buildFailed ($failureBefore + 1) $runner 120
        $text = Wait-GuestOccurrence $guestLog $vfsErrorMarker ($vfsErrorBefore + 1) $runner 30
        if ($text -notmatch 'BareMetalBuild: VFS package output path unavailable; package not published' -or
            (Get-Count $text 'GUIDEXOS_DEVELOPER_STUDIO_MARKER build_complete=SUCCEEDED') -ne $failedBefore) {
            throw 'Unavailable VFS package parent did not fail closed without a false successful build marker.'
        }
        $record.transitions.vfs_package_publication_failure = $true
        $record.transitions.no_false_build_success_for_vfs_failure = $true
        $record.screenshots += @('r2b1-close-project-menu.png', 'r2b1-project-closed.png',
            'r2b1-open-vfs-project.png', 'r2b1-vfs-build.png')

        # Close through the actual Studio window control so the guest delivers
        # the ABI close event and the kernel can verify the exposed desktop
        # redraw and post-close pointer/keyboard path.
        $cleanCloseMarker = 'GUIDEXOS_DEVELOPER_STUDIO_MARKER clean_close=PASS'
        $closeEventMarker = 'ABI3 guest observed Studio close event through v0.5 poll_event'
        $cleanCloseBefore = Get-Count (Read-GuestText $guestLog) $cleanCloseMarker
        $closeEventBefore = Get-Count (Read-GuestText $guestLog) $closeEventMarker
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 786; ClickY = 10; CapturePath = (Join-Path $sessionDirectory 'r2b1-exit-requested.png') }
        $exitRequested = $true
        $text = Wait-GuestOccurrence $guestLog $closeEventMarker ($closeEventBefore + 1) $runner 30
        $text = Wait-GuestOccurrence $guestLog $cleanCloseMarker ($cleanCloseBefore + 1) $runner 30
        $text = Wait-GuestOccurrence $guestLog 'NativeElf GUI cleanup: exposed desktop redraw=PASS' 1 $runner 30
        $text = Wait-GuestOccurrence $guestLog '[guideXOS] Studio post-close input probe: armed' 1 $runner 30
        $record.transitions.studio_clean_close = $true
        $record.screenshots += 'r2b1-exit-requested.png'
        Save-Workflow $workflowPath $record

        if (!$runner.WaitForExit(($RuntimeSeconds + 120) * 1000)) { throw 'The QAC runner did not complete its bounded cleanup window.' }
        $record.runner_exit_code = $runner.ExitCode
        $finalSession = (Read-GuestText $sessionPath) | ConvertFrom-Json
        $record.runner_guest_outcome = [string]$finalSession.outcome
        $record.managed_qemu_exit_code = $finalSession.qemu_exit_code
        $record.qac_child_exit_verified = [bool]$finalSession.qac_child_exit_verified
        $record.qac_session_reservation_released = [bool]$finalSession.qac_released_reservation
        $record.qac_final_snapshot_available = [bool]$finalSession.qac_final_snapshot_available
        $record.post_studio_pointer_responsive = [bool]$finalSession.post_studio_pointer_responsive
        $record.post_studio_keyboard_responsive = [bool]$finalSession.post_studio_keyboard_responsive
        $record.r2b1_workflow_validation_pass = [bool]$finalSession.r2b1_workflow_pass
        $text = Read-GuestText $guestLog
        $record.phase12_cleanup_pass = [regex]::Matches($text, 'AARCH64_PHASE12_PASS').Count -ge 2
        $record.gui_cleanup_pass = $text.Contains('NativeElf GUI cleanup: PASS mappings=0 windows=0 widgets=0')
        $record.no_unexpected_irqs_or_exceptions = $text -match 'unexpected-irq=0 exceptions=0'
        $record.qac_exclusive_grant = [bool]$finalSession.qac_exclusive_grant
        $record.qac_reservation_released = [bool]$finalSession.qac_released_reservation
        $record.no_remaining_qemu = [int]$finalSession.qemu_process_count_after_cleanup -eq 0
        $record.desktop_redraw_captured = [bool]$finalSession.desktop_after_close_screendump_captured -and
            [bool]$finalSession.desktop_after_close_500ms_screendump_captured
        $record.screenshots = @($record.screenshots | Select-Object -Unique | ForEach-Object {
            $path = Join-Path $sessionDirectory $_
            [pscustomobject]@{
                path = $_
                exists = Test-Path -LiteralPath $path -PathType Leaf
                sha256 = if (Test-Path -LiteralPath $path -PathType Leaf) {
                    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
                } else { '' }
            }
        })
        $requiredTrue = @($record.transitions.Values | Where-Object { $_ -ne $true }).Count -eq 0
        $record.outcome = if ($requiredTrue -and $record.phase12_cleanup_pass -and $record.gui_cleanup_pass -and
            $record.no_unexpected_irqs_or_exceptions -and $record.qac_exclusive_grant -and
            $record.qac_reservation_released -and $record.no_remaining_qemu -and $record.desktop_redraw_captured -and
            $record.runner_guest_outcome -eq 'ABI3_STUDIO_GUEST_BOOT_AND_CLEANUP_PASS' -and
            $record.managed_qemu_exit_code -eq 0 -and $record.qac_child_exit_verified -and
            $record.qac_session_reservation_released -and $record.qac_final_snapshot_available -and
            $record.post_studio_pointer_responsive -and $record.post_studio_keyboard_responsive -and
            $record.r2b1_workflow_validation_pass) { 'STUDIO_R2B1_NATIVE_BUILD_VERIFIED' } else { 'STUDIO_R2B1_PARTIAL' }
        Save-Workflow $workflowPath $record
        Write-Host "R2B1 boot $BootNumber outcome: $($record.outcome) ($workflowPath)" -ForegroundColor $(if ($record.outcome -eq 'STUDIO_R2B1_NATIVE_BUILD_VERIFIED') { 'Green' } else { 'Yellow' })
        if ($record.outcome -ne 'STUDIO_R2B1_NATIVE_BUILD_VERIFIED') { throw "R2B1 boot $BootNumber did not satisfy every guest or QAC cleanup check." }
        return [pscustomobject]@{ SessionPath = $sessionPath; WorkflowPath = $workflowPath; ReservationId = $session.reservation_id }
    } catch {
        $record.outcome = 'STUDIO_R2B1_PARTIAL'
        $record.failure = $_.Exception.Message
        Save-Workflow $workflowPath $record
        # If a step fails while Studio is open, request its normal Exit through
        # the already verified QAC-owned QMP endpoint and allow the managed
        # runner to perform its own bounded cleanup.
        try {
            $failureText = Read-GuestText $guestLog
            if ($failureText.Contains('GUIDEXOS_DEVELOPER_STUDIO_MARKER initial_render=PASS') -and
                !$exitRequested -and !$runner.HasExited) {
                Send-StudioInput $sessionPath $qacPath @{ ClickX = 300; ClickY = 57 }
                Send-StudioInput $sessionPath $qacPath @{ ClickX = 330; ClickY = 286 }
            }
        } catch {}
        throw "R2B1 boot $BootNumber failed at the earliest unverified transition: $($_.Exception.Message) See $workflowPath"
    }
}

if (!(Test-Path -LiteralPath $runnerScript -PathType Leaf) -or !(Test-Path -LiteralPath $inputScript -PathType Leaf)) {
    throw 'The existing QAC runner or QMP Studio input helper is missing.'
}
if (!(Test-Path -LiteralPath $ArtifactDirectory -PathType Container)) { throw "Candidate artifact directory is missing: $ArtifactDirectory" }
foreach ($required in @('esp\EFI\BOOT\BOOTAA64.EFI','esp\kernel.elf','esp\ramdisk.img','candidate-manifest.json')) {
    if (!(Test-Path -LiteralPath (Join-Path $ArtifactDirectory $required) -PathType Leaf)) {
        throw "Candidate artifact directory is missing ${required}: $ArtifactDirectory"
    }
}
if ([string]::IsNullOrWhiteSpace($QacCliPath)) { $QacCliPath = $env:GUIDEXOS_QAC_CLI }
if ([string]::IsNullOrWhiteSpace($QacCliPath)) {
    $QacCliPath = Join-Path (Split-Path -Parent $repoRoot) 'guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe'
}
if (!(Test-Path -LiteralPath $QacCliPath -PathType Leaf)) { throw "QAC CLI not found: $QacCliPath" }

$runs = @()
for ($boot = 1; $boot -le $Boots; ++$boot) {
    $runs += Invoke-StudioBoot $boot
}
Write-Host "AARCH64_STUDIO_R2B1_QAC_PASS boots=$($runs.Count)" -ForegroundColor Green
