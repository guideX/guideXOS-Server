[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ArtifactDirectory,
    [string]$QacCliPath = '',
    [ValidateRange(1, 3)] [int]$Boots = 1,
    [int]$RuntimeSeconds = 900,
    [int]$PostStudioExitSeconds = 20
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ArtifactDirectory = [IO.Path]::GetFullPath($ArtifactDirectory)
$runnerScript = Join-Path $PSScriptRoot 'test-aarch64-studio-abi3-qemu.ps1'
$inputScript = Join-Path $PSScriptRoot 'send-aarch64-qmp-studio-input.ps1'
$powerShellExe = (Get-Command powershell.exe -ErrorAction Stop).Source

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
                        [int]$TimeoutSeconds = 20) {
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
                              [Diagnostics.Process]$Runner, [int]$TimeoutSeconds = 20) {
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

function Send-StudioInput([string]$SessionPath, [string]$QacPath, [hashtable]$InputArguments) {
    $arguments = @{ SessionPath = $SessionPath; QacCliPath = $QacPath }
    foreach ($key in $InputArguments.Keys) { $arguments[$key] = $InputArguments[$key] }
    & $inputScript @arguments | Out-Host
    Start-Sleep -Milliseconds 350
}

function Save-R2ARecord([string]$Path, [hashtable]$Record) {
    $Record.updated_utc = [DateTimeOffset]::UtcNow.ToString('o')
    $Record | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Path -Encoding utf8
}

function Invoke-StudioBoot([int]$BootNumber) {
    $existing = @{}
    Get-ChildItem -LiteralPath $ArtifactDirectory -Directory -Filter 'abi3-guest-*' -ErrorAction SilentlyContinue |
        ForEach-Object { $existing[$_.FullName] = $true }

    $runnerArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runnerScript,
        '-ArtifactDirectory', $ArtifactDirectory, '-RuntimeSeconds', [string]$RuntimeSeconds,
        '-PostStudioExitSeconds', [string]$PostStudioExitSeconds)
    if (![string]::IsNullOrWhiteSpace($QacCliPath)) {
        $runnerArguments += @('-QacCliPath', [IO.Path]::GetFullPath($QacCliPath))
    }
    $argumentString = ($runnerArguments | ForEach-Object { Quote-ProcessArgument ([string]$_) }) -join ' '
    $runner = Start-Process -FilePath $powerShellExe -ArgumentList $argumentString -PassThru -WindowStyle Hidden
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
                try { $candidateSession = Get-Content -LiteralPath $candidateSessionPath -Raw | ConvertFrom-Json }
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
        throw "Boot $BootNumber did not produce an in-progress QAC session record."
    }

    $sessionDirectory = Split-Path -Parent $sessionPath
    $guestLog = [string]$session.guest_log
    $qacPath = if (![string]::IsNullOrWhiteSpace($QacCliPath)) { [IO.Path]::GetFullPath($QacCliPath) } else { '' }
    $workflowPath = Join-Path $sessionDirectory 'r2a-workflow.json'
    $record = [ordered]@{
        outcome = 'STUDIO_R2A_GUEST_IN_PROGRESS'
        boot_number = $BootNumber
        qac_reservation_id = [string]$session.reservation_id
        session_record = $sessionPath
        guest_log = $guestLog
        candidate_source_head = [string]$session.candidate_source_head
        candidate_manifest_sha256 = [string]$session.candidate_manifest_sha256
        candidate_kernel_sha256 = [string]$session.candidate_kernel_sha256
        candidate_efi_sha256 = [string]$session.candidate_efi_sha256
        candidate_ramdisk_sha256 = [string]$session.candidate_ramdisk_sha256
        source_text = 'int gx_main(void* ctx) { return 42; }'
        source_bytes_hex = [BitConverter]::ToString([Text.Encoding]::ASCII.GetBytes('int gx_main(void* ctx) { return 42; }')).Replace('-', '').ToLowerInvariant()
        screenshots = @()
        transitions = [ordered]@{}
    }
    Save-R2ARecord $workflowPath $record

    try {
        $null = Wait-GuestText $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER initial_render=PASS' $runner 120
        $record.transitions.studio_window_visible = $true
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlN'); CapturePath = (Join-Path $sessionDirectory 'r2a-new-project-dialog.png') }
        $null = Wait-GuestText $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_create_request=PASS' $runner
        Start-Sleep -Milliseconds 700
        $record.transitions.new_project_action = $true
        $record.screenshots += 'r2a-new-project-dialog.png'

        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Tab') }
        Send-StudioInput $sessionPath $qacPath @{ Text = '/Apps'; CapturePath = (Join-Path $sessionDirectory 'r2a-project-parent.png') }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Tab') }
        Send-StudioInput $sessionPath $qacPath @{ Text = 'R2ATST'; CapturePath = (Join-Path $sessionDirectory 'r2a-project-folder.png') }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Tab') }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Enter'); CapturePath = (Join-Path $sessionDirectory 'r2a-project-created.png') }
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_create=PASS' 1 $runner 30
        $null = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_open=PASS' 1 $runner 30
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_metadata_parse=PASS' 1 $runner 30
        $record.transitions.project_created_and_active = $true
        $record.transitions.project_metadata_parsed = $true
        $record.screenshots += @('r2a-project-parent.png', 'r2a-project-folder.png', 'r2a-project-created.png')
        Save-R2ARecord $workflowPath $record

        Send-StudioInput $sessionPath $qacPath @{ ClickX = 300; ClickY = 57; CapturePath = (Join-Path $sessionDirectory 'r2a-file-menu.png') }
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 330; ClickY = 242; CapturePath = (Join-Path $sessionDirectory 'r2a-new-file-prompt.png') }
        Send-StudioInput $sessionPath $qacPath @{ Text = 'r2a.c' }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Enter'); CapturePath = (Join-Path $sessionDirectory 'r2a-source-created.png') }
        $null = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER file_create=PASS name=r2a.c' 1 $runner 30
        $record.transitions.source_file_created_and_active = $true
        $record.screenshots += @('r2a-file-menu.png', 'r2a-new-file-prompt.png', 'r2a-source-created.png')
        Save-R2ARecord $workflowPath $record

        Send-StudioInput $sessionPath $qacPath @{ Text = 'int gx_main(void* ctx) { return 42; }x' }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Backspace'); CapturePath = (Join-Path $sessionDirectory 'r2a-editor-buffer.png') }
        $record.transitions.editor_text_entered_and_backspace = $true
        $record.screenshots += 'r2a-editor-buffer.png'
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlS'); CapturePath = (Join-Path $sessionDirectory 'r2a-source-saved.png') }
        $text = Wait-GuestText $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER document_save=PASS name=r2a.c' $runner 30
        $expectedHex = [string]$record.source_bytes_hex
        $writeEvidence = [regex]::Match($text, '(?m)^.*ABI3_STUDIO_VFS_IO action=write path=/Apps/R2ATST/src/r2a\.c bytes=\d+ datahex=' + [regex]::Escape($expectedHex) + '\r?$')
        if (!$writeEvidence.Success) { throw 'Save did not produce a guest VFS write with the exact expected source bytes.' }
        $record.transitions.source_saved_to_real_vfs = $true
        $record.source_write_evidence = $writeEvidence.Value.Trim()
        $record.screenshots += 'r2a-source-saved.png'
        Save-R2ARecord $workflowPath $record

        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlW'); CapturePath = (Join-Path $sessionDirectory 'r2a-source-closed.png') }
        $null = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER document_close=PASS' 1 $runner 45
        $record.transitions.source_document_closed = $true
        Send-StudioInput $sessionPath $qacPath @{ CapturePath = (Join-Path $sessionDirectory 'r2a-src-directory.png') }
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 50; ClickY = 180; CapturePath = (Join-Path $sessionDirectory 'r2a-source-reopen-selected.png') }
        Start-Sleep -Milliseconds 500
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Enter') }
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER document_open=PASS name=R2A.C' 1 $runner 30
        Start-Sleep -Milliseconds 500
        Send-StudioInput $sessionPath $qacPath @{ CapturePath = (Join-Path $sessionDirectory 'r2a-source-reopened.png') }
        $readEvidence = [regex]::Match($text, '(?im)^.*ABI3_STUDIO_VFS_IO action=read path=/Apps/R2ATST/src/R2A\.C bytes=\d+ datahex=' + [regex]::Escape($expectedHex) + '\r?$')
        if (!$readEvidence.Success) { throw 'Reopening r2a.c did not produce a guest VFS read with the exact saved source bytes.' }
        $record.transitions.first_source_readback = $true
        $record.first_read_evidence = $readEvidence.Value.Trim()
        $record.screenshots += 'r2a-source-reopened.png'
        Save-R2ARecord $workflowPath $record

        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlW'); CapturePath = (Join-Path $sessionDirectory 'r2a-source-closed-before-project-reopen.png') }
        $null = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER document_close=PASS' 2 $runner 45
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 300; ClickY = 57; CapturePath = (Join-Path $sessionDirectory 'r2a-close-menu.png') }
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 330; ClickY = 177; CapturePath = (Join-Path $sessionDirectory 'r2a-workspace-closed.png') }
        $text = Wait-GuestText $guestLog 'ABI3_STUDIO_VFS_IO action=write path=/Apps/R2ATST/GUIDEDB.BAK' $runner 20
        $text = Wait-GuestText $guestLog 'ABI3_STUDIO_VFS_IO action=write path=/Apps/R2ATST/GUIDEDBG.JSN' $runner 20
        if ($text -match 'FAT_FILE_WRITE_INVALID_NAME.*guidexos\.debugger\.json') {
            throw 'Project close rejected the debugger workspace metadata filename.'
        }
        $record.transitions.debugger_workspace_backup_saved = $true
        $record.transitions.debugger_workspace_state_saved = $true
        $closedWorkspaceCapture = Join-Path $sessionDirectory 'r2a-workspace-closed.png'
        $record.transitions.project_closed = Test-Path -LiteralPath $closedWorkspaceCapture -PathType Leaf
        if (!$record.transitions.project_closed) { throw 'Studio did not produce a workspace-closed framebuffer capture.' }
        $record.screenshots += 'r2a-workspace-closed.png'
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlShiftO'); CapturePath = (Join-Path $sessionDirectory 'r2a-project-open-prompt.png') }
        Send-StudioInput $sessionPath $qacPath @{ Text = '/Apps/R2ATST' }
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Enter'); CapturePath = (Join-Path $sessionDirectory 'r2a-project-reopened.png') }
        $null = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_open=PASS' 2 $runner 30
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_metadata_parse=PASS' 2 $runner 30
        $record.transitions.project_reopened_in_same_boot = $true
        $record.transitions.project_metadata_reparsed = $true
        $record.screenshots += @('r2a-source-closed-before-project-reopen.png', 'r2a-close-menu.png', 'r2a-workspace-closed.png', 'r2a-project-open-prompt.png', 'r2a-project-reopened.png')
        Save-R2ARecord $workflowPath $record

        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlW'); CapturePath = (Join-Path $sessionDirectory 'r2a-main-document-closed.png') }
        $null = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER document_close=PASS' 3 $runner 45
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 50; ClickY = 162; CapturePath = (Join-Path $sessionDirectory 'r2a-source-directory-selected-after-project.png') }
        Start-Sleep -Milliseconds 500
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Enter'); CapturePath = (Join-Path $sessionDirectory 'r2a-source-directory-open-after-project.png') }
        Start-Sleep -Milliseconds 500
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 50; ClickY = 180; CapturePath = (Join-Path $sessionDirectory 'r2a-source-reopen-selected-after-project.png') }
        Start-Sleep -Milliseconds 500
        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('Enter') }
        $text = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER document_open=PASS name=R2A.C' 2 $runner 30
        Start-Sleep -Milliseconds 500
        Send-StudioInput $sessionPath $qacPath @{ CapturePath = (Join-Path $sessionDirectory 'r2a-source-reopened-after-project.png') }
        $readEvidence2 = [regex]::Match($text, '(?im)^.*ABI3_STUDIO_VFS_IO action=read path=/Apps/R2ATST/src/R2A\.C bytes=\d+ datahex=' + [regex]::Escape($expectedHex) + '\r?$')
        if (!$readEvidence2.Success) { throw 'After project reopen, Studio did not read r2a.c back with the exact expected bytes.' }
        $record.transitions.second_source_readback = $true
        $record.second_read_evidence = $readEvidence2.Value.Trim()
        $record.screenshots += @('r2a-main-document-closed.png', 'r2a-source-directory-selected-after-project.png',
            'r2a-source-directory-open-after-project.png', 'r2a-source-reopen-selected-after-project.png',
            'r2a-source-reopened-after-project.png')
        Save-R2ARecord $workflowPath $record

        Send-StudioInput $sessionPath $qacPath @{ SpecialKeys = @('ControlW') }
        $null = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER document_close=PASS' 4 $runner 45
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 300; ClickY = 57 }
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 330; ClickY = 177; CapturePath = (Join-Path $sessionDirectory 'r2a-workspace-closed-finally.png') }
        $null = Wait-GuestOccurrence $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER project_open=PASS' 2 $runner 10
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 300; ClickY = 57; CapturePath = (Join-Path $sessionDirectory 'r2a-exit-menu.png') }
        Send-StudioInput $sessionPath $qacPath @{ ClickX = 330; ClickY = 286; CapturePath = (Join-Path $sessionDirectory 'r2a-exit-requested.png') }
        $text = Wait-GuestText $guestLog 'GUIDEXOS_DEVELOPER_STUDIO_MARKER clean_close=PASS' $runner 20
        $record.transitions.project_closed_finally = Test-Path -LiteralPath (Join-Path $sessionDirectory 'r2a-workspace-closed-finally.png') -PathType Leaf
        $record.transitions.studio_clean_close = $true
        $record.screenshots += @('r2a-exit-menu.png', 'r2a-exit-requested.png')
        Save-R2ARecord $workflowPath $record

        if (!$runner.WaitForExit(($RuntimeSeconds + 120) * 1000)) { throw 'The QAC runner did not complete its bounded cleanup window.' }
        $record.runner_exit_code = $runner.ExitCode
        $text = Read-GuestText $guestLog
        $record.transitions.phase12_cleanup_pass = [regex]::Matches($text, 'AARCH64_PHASE12_PASS').Count -ge 2
        $record.transitions.gui_cleanup_pass = $text.Contains('NativeElf GUI cleanup: PASS mappings=0 windows=0 widgets=0')
        $record.transitions.no_unexpected_irqs_or_exceptions = $text -match 'unexpected-irq=0 exceptions=0'
        $finalSession = Get-Content -LiteralPath $sessionPath -Raw | ConvertFrom-Json
        $record.qac_exclusive_grant = [bool]$finalSession.qac_exclusive_grant
        $record.qac_reservation_released = [bool]$finalSession.qac_released_reservation
        $record.no_remaining_qemu = [int]$finalSession.qemu_process_count_after_cleanup -eq 0
        $record.transitions.desktop_redraw_captured = [bool]$finalSession.desktop_after_close_screendump_captured -and
            [bool]$finalSession.desktop_after_close_500ms_screendump_captured
        $record.screenshots = @($record.screenshots | Select-Object -Unique | ForEach-Object {
            $screenshotPath = Join-Path $sessionDirectory $_
            [pscustomobject]@{
                path = $_
                exists = Test-Path -LiteralPath $screenshotPath -PathType Leaf
                sha256 = if (Test-Path -LiteralPath $screenshotPath -PathType Leaf) {
                    (Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
                } else { '' }
            }
        })
        $allTransitions = @($record.transitions.Values | Where-Object { $_ -ne $true }).Count -eq 0
        $record.outcome = if ($allTransitions -and $record.qac_exclusive_grant -and $record.qac_reservation_released -and $record.no_remaining_qemu) {
            'STUDIO_ARM64_PROJECT_EDITOR_VERIFIED'
        } else { 'STUDIO_PROJECT_EDITOR_PARTIAL' }
        Save-R2ARecord $workflowPath $record
        Write-Host "R2A boot $BootNumber outcome: $($record.outcome) ($workflowPath)" -ForegroundColor $(if ($record.outcome -eq 'STUDIO_ARM64_PROJECT_EDITOR_VERIFIED') { 'Green' } else { 'Yellow' })
        if ($record.outcome -ne 'STUDIO_ARM64_PROJECT_EDITOR_VERIFIED') { throw "R2A boot $BootNumber did not satisfy every native transition." }
        return [pscustomobject]@{ SessionPath = $sessionPath; WorkflowPath = $workflowPath; ReservationId = $session.reservation_id }
    } catch {
        $record.outcome = 'STUDIO_PROJECT_EDITOR_PARTIAL'
        $record.failure = $_.Exception.Message
        Save-R2ARecord $workflowPath $record
        throw "R2A boot $BootNumber failed at the earliest unverified transition: $($_.Exception.Message) See $workflowPath"
    }
}

if (!(Test-Path -LiteralPath $runnerScript -PathType Leaf) -or !(Test-Path -LiteralPath $inputScript -PathType Leaf)) {
    throw 'The existing QAC runner or QMP Studio input helper is missing.'
}
if (!(Test-Path -LiteralPath $ArtifactDirectory -PathType Container)) { throw "Candidate artifact directory is missing: $ArtifactDirectory" }

$runs = @()
for ($boot = 1; $boot -le $Boots; ++$boot) {
    $runs += Invoke-StudioBoot $boot
}
Write-Host "AARCH64_STUDIO_R2A_QAC_PASS boots=$($runs.Count)" -ForegroundColor Green
