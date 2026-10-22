[CmdletBinding()]
param([switch]$KeepTestArtifacts)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$modulePath = Join-Path $repoRoot 'scripts\qac-uefi-artifacts.psm1'
$module = Import-Module $modulePath -Force -PassThru
$script:checkCount = 0
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('guidexos-qac3c2-' + [Guid]::NewGuid().ToString('N'))
$fixtureRoot = Join-Path $testRoot 'sources'
$outputRoot = Join-Path $testRoot 'workspaces'
$junctionPath = $null

function Assert-True {
    param([string]$Name, [bool]$Condition)
    $script:checkCount++
    if (-not $Condition) { throw "FAIL [$($script:checkCount)] $Name" }
    Write-Host "PASS [$($script:checkCount)] $Name"
}

function Assert-Throws {
    param([string]$Name, [scriptblock]$Action, [string]$MessagePattern = '')
    $thrown = $false
    $message = ''
    try { & $Action } catch { $thrown = $true; $message = $_.Exception.Message }
    Assert-True $Name ($thrown -and (-not $MessagePattern -or $message -match $MessagePattern))
    return $message
}

function New-FixtureSources {
    param([string]$Root)
    $esp = Join-Path $Root 'esp-source'
    [void][IO.Directory]::CreateDirectory((Join-Path $esp 'EFI\BOOT'))
    [void][IO.Directory]::CreateDirectory((Join-Path $esp 'Apps\Demo'))
    [IO.File]::WriteAllText((Join-Path $esp 'EFI\BOOT\BOOTX64.EFI'), 'fixture bootloader')
    [IO.File]::WriteAllText((Join-Path $esp 'kernel.elf'), 'fixture kernel')
    [IO.File]::WriteAllText((Join-Path $esp 'ramdisk.img'), 'fixture ramdisk')
    [IO.File]::WriteAllText((Join-Path $esp 'Apps\Demo\note.txt'), 'baseline')
    [IO.File]::WriteAllBytes((Join-Path $Root 'ovmf-firmware.fd'), (New-Object byte[] 4096))
    [IO.File]::WriteAllText((Join-Path $Root 'ovmf-vars.fd'), 'mutable firmware variable fixture')
    [IO.File]::WriteAllText((Join-Path $Root 'guest-disk.img'), 'mutable guest disk fixture')
    $isoBytes = New-Object byte[] 32774
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes('CD001'), 0, $isoBytes, 32769, 5)
    [IO.File]::WriteAllBytes((Join-Path $Root 'release.iso'), $isoBytes)
    return [PSCustomObject]@{ Esp = $esp; Firmware = (Join-Path $Root 'ovmf-firmware.fd'); Vars = (Join-Path $Root 'ovmf-vars.fd'); Disk = (Join-Path $Root 'guest-disk.img'); Iso = (Join-Path $Root 'release.iso') }
}

function New-FixtureArtifactSet {
    param($Sources)
    return @(
        @{ Name='esp'; Role='writable-uefi-esp'; Kind='directory'; Access='writable'; Disposition='copy'; Path=$Sources.Esp; Validation='uefi-esp-directory' },
        @{ Name='firmware-vars'; Role='writable-uefi-variable-store'; Kind='file'; Access='writable'; Disposition='copy'; Path=$Sources.Vars },
        @{ Name='guest-disk'; Role='writable-guest-disk'; Kind='file'; Access='writable'; Disposition='copy'; Path=$Sources.Disk },
        @{ Name='ovmf-firmware'; Role='read-only-uefi-firmware'; Kind='file'; Access='read-only'; Disposition='share'; Path=$Sources.Firmware; Validation='raw-firmware' },
        @{ Name='release-iso'; Role='read-only-boot-media'; Kind='file'; Access='read-only'; Disposition='share'; Path=$Sources.Iso; Validation='iso9660' }
    )
}

function Get-FixtureTreeHash {
    param([string]$Path)
    $records = @(Get-ChildItem -LiteralPath $Path -File -Recurse -Force | ForEach-Object {
        [PSCustomObject]@{ path=$_.FullName.Substring($Path.Length).TrimStart('\').Replace('\','/'); size=$_.Length; sha=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    } | Sort-Object path)
    $text = ($records | ForEach-Object { "{0}`t{1}`t{2}" -f $_.path, $_.size, $_.sha }) -join "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}

function Set-ManifestState {
    param([string]$Path, [string]$State)
    $manifest = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $manifest.qacExecution.status = $State
    $json = ConvertTo-Json -InputObject $manifest -Depth 16
    [IO.File]::WriteAllText($Path, $json + "`n", (New-Object Text.UTF8Encoding($false)))
}

[void][IO.Directory]::CreateDirectory($fixtureRoot)
[void][IO.Directory]::CreateDirectory($outputRoot)
$sources = New-FixtureSources -Root $fixtureRoot
$artifacts = New-FixtureArtifactSet -Sources $sources
$sourceEspHash = Get-FixtureTreeHash $sources.Esp
$sourceIsoHash = (Get-FileHash -LiteralPath $sources.Iso -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceVarsHash = (Get-FileHash -LiteralPath $sources.Vars -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceDiskHash = (Get-FileHash -LiteralPath $sources.Disk -Algorithm SHA256).Hash.ToLowerInvariant()

try {
    $fixtureLaunchContract = [ordered]@{ executableFamily='qemu-test.exe'; arguments=@('-drive','file={esp}','-qmp','tcp:127.0.0.1:{QAC3C3-allocated-port}'); qmp=[ordered]@{ fixedPortReusable=$false } }
    $first = New-QacUefiArtifactWorkspace -Project 'guideXOSServer' -Workflow 'fixture-uefi' -RunId 'first' -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $artifacts -LaunchContract $fixtureLaunchContract
    Assert-True 'valid source artifacts produce a unique private workspace' ((Test-Path -LiteralPath $first.WorkspacePath -PathType Container) -and $first.WorkspacePath -ne $sources.Esp)
    $validated = Test-QacUefiArtifactManifest -ManifestPath $first.ManifestPath
    Assert-True 'manifest records run, project, workflow, completion, and owner identity' ($validated.schemaVersion -eq 1 -and $validated.project -eq 'guideXOSServer' -and $validated.selectedWorkflow -eq 'fixture-uefi' -and $validated.completionStatus -eq 'complete' -and $validated.workspaceOwner -and $validated.ownerToken)
    Assert-True 'manifest carries a future QAC3C3 launch contract with a dynamically allocated QMP placeholder' ($validated.launchContract.executableFamily -eq 'qemu-test.exe' -and $validated.launchContract.arguments -contains 'tcp:127.0.0.1:{QAC3C3-allocated-port}' -and -not $validated.launchContract.qmp.fixedPortReusable)
    $manifestEsp = $validated.artifacts | Where-Object { $_.name -eq 'esp' }
    Assert-True 'manifest contains source and private ESP identities' ($manifestEsp.sourcePath -eq $sources.Esp -and $manifestEsp.sourceSha256 -eq $sourceEspHash -and $manifestEsp.privateSha256 -eq $sourceEspHash -and $manifestEsp.access -eq 'writable')
    $privateEspHash = Get-FixtureTreeHash $manifestEsp.privatePath
    Assert-True 'private ESP tree exactly matches all source file bytes at staging completion' ($privateEspHash -eq $sourceEspHash)
    Assert-True 'canonical ESP and other source artifacts remain unchanged after staging' ((Get-FixtureTreeHash $sources.Esp) -eq $sourceEspHash -and (Get-FileHash $sources.Iso -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceIsoHash -and (Get-FileHash $sources.Vars -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceVarsHash -and (Get-FileHash $sources.Disk -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceDiskHash)
    Assert-True 'read-only firmware and ISO remain shared source paths with verified identities' (($validated.artifacts | Where-Object name -eq 'ovmf-firmware').privatePath -eq $sources.Firmware -and ($validated.artifacts | Where-Object name -eq 'release-iso').privatePath -eq $sources.Iso)
    Assert-True 'workspace outputs resolve to distinct per-run paths' ((Join-Path $first.WorkspacePath 'output') -eq (Join-Path $validated.workspacePath $validated.outputs.directory) -and (Test-Path -LiteralPath (Join-Path $first.WorkspacePath 'output') -PathType Container))

    # Same caller label must never reuse a previous workspace.
    $repeatA = New-QacUefiArtifactWorkspace -Project 'guideXOSServer' -Workflow 'fixture-uefi' -RunId 'repeat' -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $artifacts
    $repeatB = New-QacUefiArtifactWorkspace -Project 'guideXOSServer' -Workflow 'fixture-uefi' -RunId 'repeat' -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $artifacts
    Assert-True 'repeated requested run IDs produce different workspaces and do not overwrite' ($repeatA.RunId -ne $repeatB.RunId -and $repeatA.WorkspacePath -ne $repeatB.WorkspacePath -and (Test-Path -LiteralPath $repeatA.ManifestPath) -and (Test-Path -LiteralPath $repeatB.ManifestPath))

    # Independent PowerShell processes exercise simultaneous staging and destination creation.
    $concurrentSourcesRoot = Join-Path $testRoot 'concurrent-sources'
    $concurrentOutput = Join-Path $testRoot 'concurrent-workspaces'
    [void][IO.Directory]::CreateDirectory($concurrentSourcesRoot)
    [void][IO.Directory]::CreateDirectory($concurrentOutput)
    $parallelSources = New-FixtureSources -Root $concurrentSourcesRoot
    $parallelJobScript = {
        param($ModulePath, $TargetRoot, $RunId, $Esp, $Firmware, $Vars, $Disk, $Iso)
        Import-Module $ModulePath -Force
        $set = @(
            @{ Name='esp'; Role='writable-uefi-esp'; Kind='directory'; Access='writable'; Disposition='copy'; Path=$Esp; Validation='uefi-esp-directory' },
            @{ Name='firmware-vars'; Role='writable-uefi-variable-store'; Kind='file'; Access='writable'; Disposition='copy'; Path=$Vars },
            @{ Name='guest-disk'; Role='writable-guest-disk'; Kind='file'; Access='writable'; Disposition='copy'; Path=$Disk },
            @{ Name='ovmf-firmware'; Role='read-only-uefi-firmware'; Kind='file'; Access='read-only'; Disposition='share'; Path=$Firmware; Validation='raw-firmware' },
            @{ Name='release-iso'; Role='read-only-boot-media'; Kind='file'; Access='read-only'; Disposition='share'; Path=$Iso; Validation='iso9660' }
        )
        $staged = New-QacUefiArtifactWorkspace -Project 'guideXOSServer' -Workflow 'fixture-uefi' -RunId $RunId -OutputRoot $TargetRoot -ReserveFreeBytes 0 -Artifact $set
        [PSCustomObject]@{ RunId=$staged.RunId; WorkspacePath=$staged.WorkspacePath; ManifestPath=$staged.ManifestPath }
    }
    $jobArgs = @($modulePath, $concurrentOutput, 'parallel', $parallelSources.Esp, $parallelSources.Firmware, $parallelSources.Vars, $parallelSources.Disk, $parallelSources.Iso)
    $job1 = Start-Job -ScriptBlock $parallelJobScript -ArgumentList $jobArgs
    $job2 = Start-Job -ScriptBlock $parallelJobScript -ArgumentList $jobArgs
    Wait-Job -Job @($job1,$job2) | Out-Null
    $parallel = @((Receive-Job -Job $job1 -ErrorAction Stop), (Receive-Job -Job $job2 -ErrorAction Stop))
    $jobsSucceeded = $job1.State -eq 'Completed' -and $job2.State -eq 'Completed'
    Remove-Job -Job @($job1,$job2) -Force
    Assert-True 'two simultaneous staging requests finish successfully' ($jobsSucceeded -and $parallel.Count -eq 2 -and $parallel[0].RunId -ne $parallel[1].RunId)
    $parallelManifests = @($parallel | ForEach-Object { Test-QacUefiArtifactManifest -ManifestPath $_.ManifestPath })
    $parallelEsp = @($parallelManifests | ForEach-Object { $_.artifacts | Where-Object name -eq 'esp' })
    $parallelVars = @($parallelManifests | ForEach-Object { $_.artifacts | Where-Object name -eq 'firmware-vars' })
    $parallelDisks = @($parallelManifests | ForEach-Object { $_.artifacts | Where-Object name -eq 'guest-disk' })
    $parallelLogs = @($parallelManifests | ForEach-Object { Join-Path $_.workspacePath $_.outputs.directory })
    Assert-True 'concurrent requests have independent writable ESP, VARS, guest disk, and output paths' ($parallelEsp[0].privatePath -ne $parallelEsp[1].privatePath -and $parallelVars[0].privatePath -ne $parallelVars[1].privatePath -and $parallelDisks[0].privatePath -ne $parallelDisks[1].privatePath -and $parallelLogs[0] -ne $parallelLogs[1])
    [IO.File]::WriteAllText((Join-Path $parallelEsp[0].privatePath 'Apps\Demo\concurrency-marker.txt'), 'run one only')
    [IO.File]::WriteAllText($parallelVars[0].privatePath, 'run one VARS only')
    [IO.File]::WriteAllText($parallelDisks[0].privatePath, 'run one disk only')
    Assert-True 'private artifact writes in one run do not affect its concurrent peer or source' (-not (Test-Path (Join-Path $parallelEsp[1].privatePath 'Apps\Demo\concurrency-marker.txt')) -and (Get-Content $parallelVars[1].privatePath -Raw) -eq 'mutable firmware variable fixture' -and (Get-Content $parallelDisks[1].privatePath -Raw) -eq 'mutable guest disk fixture' -and (Get-FixtureTreeHash $sources.Esp) -eq $sourceEspHash -and (Get-FileHash $sources.Vars -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceVarsHash -and (Get-FileHash $sources.Disk -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceDiskHash)

    $beforeMissing = @(Get-ChildItem -LiteralPath $outputRoot -Directory -Force).Count
    $missingSet = @($artifacts | Where-Object { $_.Name -ne 'release-iso' }) + @(@{ Name='missing'; Role='test'; Kind='file'; Access='read-only'; Disposition='share'; Path=(Join-Path $fixtureRoot 'absent.bin') })
    [void](Assert-Throws 'missing source artifacts fail safely before workspace creation' { New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId x -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $missingSet } 'does not exist')
    Assert-True 'missing source failure creates no new workspace' (@(Get-ChildItem -LiteralPath $outputRoot -Directory -Force).Count -eq $beforeMissing)

    $invalidEsp = Join-Path $fixtureRoot 'invalid-esp'
    [void][IO.Directory]::CreateDirectory($invalidEsp)
    [IO.File]::WriteAllText((Join-Path $invalidEsp 'kernel.elf'), 'kernel only')
    $invalidSet = @(@{ Name='esp'; Role='uefi-esp'; Kind='directory'; Access='writable'; Disposition='copy'; Path=$invalidEsp; Validation='uefi-esp-directory' })
    [void](Assert-Throws 'invalid UEFI ESP layout fails safely' { New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId invalid -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $invalidSet } 'missing required boot input')

    [void](Assert-Throws 'insufficient scratch space is reported before staging' { New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId space -OutputRoot $outputRoot -ReserveFreeBytes 9000000000000000000 -Artifact $artifacts } 'Insufficient free space')
    $mismatchSet = @(@{ Name='disk'; Role='test'; Kind='file'; Access='writable'; Disposition='copy'; Path=$sources.Disk; ExpectedSha256=('0' * 64) })
    [void](Assert-Throws 'source identity mismatch is rejected' { New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId mismatch -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $mismatchSet } 'identity mismatch')
    [void](Assert-Throws 'path traversal in requested run ID is rejected' { New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId '..\escape' -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $artifacts } 'RunId')

    $largeMutationSource = Join-Path $fixtureRoot 'mutation-source.bin'
    $largeBytes = New-Object byte[] (3 * 1024 * 1024)
    [IO.File]::WriteAllBytes($largeMutationSource, $largeBytes)
    $mutationSet = @(@{ Name='mutation'; Role='test'; Kind='file'; Access='writable'; Disposition='copy'; Path=$largeMutationSource })
    $mutationTargetPath = $largeMutationSource
    $mutationHook = {
        param($sourcePath, $destinationPath, $copiedBytes)
        if ([IO.Path]::GetFullPath($sourcePath) -eq [IO.Path]::GetFullPath($mutationTargetPath)) {
            $bytes = [IO.File]::ReadAllBytes($sourcePath)
            $bytes[$bytes.Length - 1] = 1
            [IO.File]::WriteAllBytes($sourcePath, $bytes)
        }
    }.GetNewClosure()
    & $module { param($hook) $script:AfterCopyChunkHook = $hook } $mutationHook
    try {
        [void](Assert-Throws 'source mutation during artifact copy is detected and rejected' { New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId mutation -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $mutationSet } 'changed while staging')
    } finally { & $module { $script:AfterCopyChunkHook = $null } }

    $partialSource = Join-Path $fixtureRoot 'partial.bin'
    [IO.File]::WriteAllBytes($partialSource, (New-Object byte[] (2 * 1024 * 1024)))
    $partialSet = @(@{ Name='partial'; Role='test'; Kind='file'; Access='writable'; Disposition='copy'; Path=$partialSource })
    $partialHook = { param($sourcePath,$destinationPath,$copiedBytes) throw 'controlled fixture copy failure' }.GetNewClosure()
    & $module { param($hook) $script:AfterCopyChunkHook = $hook } $partialHook
    $partialFailure = ''
    try { $partialFailure = Assert-Throws 'controlled partial staging failure is explicit and retained' { New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId partial -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $partialSet } 'incomplete workspace retained' }
    finally { & $module { $script:AfterCopyChunkHook = $null } }
    $partialMatch = [regex]::Match($partialFailure, "retained at '([^']+)'")
    if (-not $partialMatch.Success) { throw "Could not locate the retained partial workspace in staging diagnostic: $partialFailure" }
    $partialPath = $partialMatch.Groups[1].Value
    $partialManifest = Join-Path $partialPath 'manifest.json'
    Assert-True 'partial staging is not marked complete and retains failure evidence' ((Test-Path $partialManifest) -and (Get-Content $partialManifest -Raw | ConvertFrom-Json).completionStatus -eq 'failed' -and -not (Test-Path (Join-Path $partialPath '.complete')))

    $junctionTarget = Join-Path $fixtureRoot 'junction-target'
    $junctionEsp = Join-Path $fixtureRoot 'junction-esp'
    [void][IO.Directory]::CreateDirectory($junctionTarget)
    [void][IO.Directory]::CreateDirectory((Join-Path $junctionEsp 'EFI\BOOT'))
    foreach ($name in @('kernel.elf','ramdisk.img')) { [IO.File]::WriteAllText((Join-Path $junctionEsp $name), $name) }
    [IO.File]::WriteAllText((Join-Path $junctionEsp 'EFI\BOOT\BOOTX64.EFI'), 'boot')
    $junctionPath = Join-Path $junctionEsp 'redirected'
    try {
        [void](New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget -ErrorAction Stop)
        $junctionSet = @(@{ Name='esp'; Role='test'; Kind='directory'; Access='writable'; Disposition='copy'; Path=$junctionEsp; Validation='uefi-esp-directory' })
        [void](Assert-Throws 'unsafe junction or reparse-point redirection is rejected' { New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId junction -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact $junctionSet } 'reparse point')
    } catch {
        if ($_.Exception.Message -like 'FAIL*') { throw }
        throw "Junction fixture could not be created for the required reparse-point check: $($_.Exception.Message)"
    }

    $malformedPath = $first.ManifestPath
    $goodManifestText = Get-Content -LiteralPath $malformedPath -Raw
    [IO.File]::WriteAllText($malformedPath, '{ this is not JSON')
    [void](Assert-Throws 'malformed manifests are rejected' { Test-QacUefiArtifactManifest -ManifestPath $malformedPath } 'Malformed artifact manifest')
    [IO.File]::WriteAllText($malformedPath, $goodManifestText)

    $outside = Join-Path $testRoot 'outside-sentinel.txt'
    [IO.File]::WriteAllText($outside, 'preserve me')
    $goodManifest = Get-Content -LiteralPath $malformedPath -Raw | ConvertFrom-Json
    $goodWorkspace = $goodManifest.workspacePath
    $goodManifest.workspacePath = $testRoot
    [IO.File]::WriteAllText($malformedPath, (ConvertTo-Json -InputObject $goodManifest -Depth 16))
    [void](Assert-Throws 'manifest path traversal cannot redirect cleanup outside scratch root' { Remove-QacUefiArtifactWorkspace -ManifestPath $malformedPath -RunId $goodManifest.runId -Confirm:$false } 'escapes')
    Assert-True 'cleanup path validation preserves outside sentinel and workspace' ((Get-Content $outside -Raw) -eq 'preserve me' -and (Test-Path $goodWorkspace))
    $goodManifest.workspacePath = $goodWorkspace
    [IO.File]::WriteAllText($malformedPath, (ConvertTo-Json -InputObject $goodManifest -Depth 16))

    $goodManifest.outputs.serialLog = '..\..\outside-sentinel.txt'
    [IO.File]::WriteAllText($malformedPath, (ConvertTo-Json -InputObject $goodManifest -Depth 16))
    [void](Assert-Throws 'manifest output traversal is rejected before future QEMU writes' { Test-QacUefiArtifactManifest -ManifestPath $malformedPath } 'output path')
    $goodManifest.outputs.serialLog = 'output/serial.log'
    $goodEspRecord = $goodManifest.artifacts | Where-Object name -eq 'esp'
    $savedPrivatePath = $goodEspRecord.privatePath
    $savedPrivateRelativePath = $goodEspRecord.privateRelativePath
    $goodEspRecord.privatePath = $outside
    $goodEspRecord.privateRelativePath = '../../outside-sentinel.txt'
    [IO.File]::WriteAllText($malformedPath, (ConvertTo-Json -InputObject $goodManifest -Depth 16))
    [void](Assert-Throws 'manifest private artifact traversal is rejected' { Test-QacUefiArtifactManifest -ManifestPath $malformedPath } 'escapes the workspace')
    $goodEspRecord.privatePath = $savedPrivatePath
    $goodEspRecord.privateRelativePath = $savedPrivateRelativePath
    [IO.File]::WriteAllText($malformedPath, (ConvertTo-Json -InputObject $goodManifest -Depth 16))

    [void](Assert-Throws 'cleanup cannot remove another run when the run identity differs' { Remove-QacUefiArtifactWorkspace -ManifestPath $malformedPath -RunId 'other-run' -Confirm:$false } 'does not identify')
    Assert-True 'wrong-run cleanup leaves the target workspace intact' (Test-Path $goodWorkspace)
    Set-ManifestState -Path $malformedPath -State 'active'
    [void](Assert-Throws 'active workspace ownership prevents cleanup' { Remove-QacUefiArtifactWorkspace -ManifestPath $malformedPath -RunId $goodManifest.runId -Confirm:$false } 'active or uncertain')
    Assert-True 'active cleanup refusal leaves workspace intact' (Test-Path $goodWorkspace)
    Set-ManifestState -Path $malformedPath -State 'unknown'
    [void](Assert-Throws 'uncertain workspace ownership prevents cleanup' { Remove-QacUefiArtifactWorkspace -ManifestPath $malformedPath -RunId $goodManifest.runId -Confirm:$false } 'active or uncertain')
    Set-ManifestState -Path $malformedPath -State 'not-started'

    $cleanupWorkspace = New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId cleanup -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact @($artifacts[0])
    Remove-QacUefiArtifactWorkspace -ManifestPath $cleanupWorkspace.ManifestPath -RunId $cleanupWorkspace.RunId -Confirm:$false
    Assert-True 'cleanup removes only the exact verified never-launched workspace' (-not (Test-Path $cleanupWorkspace.WorkspacePath) -and (Test-Path $outputRoot))

    $releasedWorkspace = New-QacUefiArtifactWorkspace -Project p -Workflow w -RunId released -OutputRoot $outputRoot -ReserveFreeBytes 0 -Artifact @($artifacts[0])
    Set-ManifestState -Path $releasedWorkspace.ManifestPath -State 'released'
    [void](Assert-Throws 'released state without verified child exit prevents cleanup' { Remove-QacUefiArtifactWorkspace -ManifestPath $releasedWorkspace.ManifestPath -RunId $releasedWorkspace.RunId -Confirm:$false } 'verified child exit')
    $releasedManifest = Get-Content -LiteralPath $releasedWorkspace.ManifestPath -Raw | ConvertFrom-Json
    $releasedManifest.qacExecution.childExitVerified = $true
    [IO.File]::WriteAllText($releasedWorkspace.ManifestPath, (ConvertTo-Json -InputObject $releasedManifest -Depth 16))
    Remove-QacUefiArtifactWorkspace -ManifestPath $releasedWorkspace.ManifestPath -RunId $releasedWorkspace.RunId -Confirm:$false
    Assert-True 'cleanup permits a released workspace after verified child exit' (-not (Test-Path $releasedWorkspace.WorkspacePath))

    Assert-True 'canonical artifacts remain unchanged after the full fixture suite' ((Get-FixtureTreeHash $sources.Esp) -eq $sourceEspHash -and (Get-FileHash $sources.Iso -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceIsoHash -and (Get-FileHash $sources.Vars -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceVarsHash -and (Get-FileHash $sources.Disk -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceDiskHash)
    Write-Host "PASS: $script:checkCount QAC UEFI artifact-isolation assertions"
} finally {
    if (-not $KeepTestArtifacts -and (Test-Path -LiteralPath $testRoot -PathType Container)) {
        # This path was generated uniquely by this test and contains only test fixtures.
        if ($junctionPath -and (Test-Path -LiteralPath $junctionPath)) { [IO.Directory]::Delete($junctionPath, $false) }
        [IO.Directory]::Delete($testRoot, $true)
    } else {
        Write-Host "Test artifacts retained at: $testRoot"
    }
}
