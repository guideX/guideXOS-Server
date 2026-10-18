Set-StrictMode -Version Latest

function Get-ReXXGuardManifest {
    param([Parameter(Mandatory)][string]$ManifestPath)
    $manifest = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json
    if (-not $manifest.repository -or -not $manifest.artifacts -or -not $manifest.protected_paths) {
        throw "Invalid RX12R1 protected-artifact manifest: $ManifestPath"
    }
    return $manifest
}

function Test-ReXXPathWithin {
    param([string]$Path, [string]$Parent)
    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd([char[]]@('\','/'))
    $fullParent = [System.IO.Path]::GetFullPath($Parent).TrimEnd([char[]]@('\','/'))
    return $fullPath.Equals($fullParent, [System.StringComparison]::OrdinalIgnoreCase) -or
        $fullPath.StartsWith($fullParent + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-ReXXRootArtifactState {
    param($Manifest)
    $repoRoot = [System.IO.Path]::GetFullPath([string]$Manifest.repository)
    $expected = @{}
    foreach ($artifact in $Manifest.artifacts) {
        $expected[[string]$artifact.relative_path] = $artifact
    }

    $actual = @{}
    foreach ($file in Get-ChildItem -LiteralPath $repoRoot -File | Where-Object { $_.Extension -in '.obj','.pdb' }) {
        $relative = $file.Name
        $actual[$relative] = [PSCustomObject]@{
            relative_path = $relative
            file_type = $file.Extension.TrimStart('.').ToLowerInvariant()
            size_bytes = $file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            last_write_utc = $file.LastWriteTimeUtc.ToString('o')
        }
    }

    $missing = @($expected.Keys | Where-Object { -not $actual.ContainsKey($_) } | Sort-Object)
    $unexpected = @($actual.Keys | Where-Object { -not $expected.ContainsKey($_) } | Sort-Object)
    $modified = foreach ($path in ($expected.Keys | Where-Object { $actual.ContainsKey($_) } | Sort-Object)) {
        $prior = $expected[$path]
        $now = $actual[$path]
        if ([string]$prior.sha256 -ne [string]$now.sha256 -or [long]$prior.size_bytes -ne [long]$now.size_bytes) {
            [PSCustomObject]@{ relative_path = $path; before_sha256 = [string]$prior.sha256; after_sha256 = [string]$now.sha256; before_size = [long]$prior.size_bytes; after_size = [long]$now.size_bytes }
        }
    }
    return [PSCustomObject]@{
        actual = @($actual.Values | Sort-Object relative_path)
        missing = @($missing)
        modified = @($modified)
        unexpected = @($unexpected)
    }
}

function Write-ReXXGuardReport {
    param([string]$RunRoot, [string]$Stage, $State, [bool]$Passed, [string]$Phase = 'RX12R1')
    $reports = Join-Path $RunRoot 'preservation'
    New-Item -ItemType Directory -Path $reports -Force | Out-Null
    $safeStage = $Stage -replace '[^A-Za-z0-9_.-]','_'
    $report = [PSCustomObject]@{
        schema = 'guidexos.rexx.artifact-preservation.v1'
        stage = $Stage
        verified_at_utc = [DateTime]::UtcNow.ToString('o')
        passed = $Passed
        expected_artifact_count = $State.expected_count
        actual_artifact_count = @($State.actual).Count
        missing = @($State.missing)
        modified = @($State.modified)
        unexpected = @($State.unexpected)
        artifacts = @($State.actual)
    }
    $jsonPath = Join-Path $reports ($safeStage + '.json')
    $textPath = Join-Path $reports ($safeStage + '.txt')
    $report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $jsonPath -Encoding utf8
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("$Phase artifact preservation: $(if ($Passed) {'PASS'} else {'FAIL'})")
    $lines.Add("Stage: $Stage")
    $lines.Add("Verified UTC: $($report.verified_at_utc)")
    $lines.Add("Artifacts: $($report.actual_artifact_count)/$($report.expected_artifact_count)")
    foreach ($path in $report.missing) { $lines.Add("MISSING: $path") }
    foreach ($item in $report.modified) { $lines.Add("MODIFIED: $($item.relative_path) $($item.before_sha256) -> $($item.after_sha256)") }
    foreach ($path in $report.unexpected) { $lines.Add("UNEXPECTED: $path") }
    if ($Passed) { $lines.Add('All baseline root .obj/.pdb paths and hashes match.') }
    [System.IO.File]::WriteAllLines($textPath, $lines, [System.Text.UTF8Encoding]::new($false))
    return [PSCustomObject]@{ Json = $jsonPath; Text = $textPath }
}

function Start-ReXXArtifactRun {
    param(
        [Parameter(Mandatory)][string]$ManifestPath,
        [Parameter(Mandatory)][string]$RunRoot,
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [string[]]$PlannedOutputs = @(),
        [ValidateSet('RX12R1','RX13')][string]$Phase = 'RX12R1'
    )
    $manifest = Get-ReXXGuardManifest -ManifestPath $ManifestPath
    $repoRoot = [System.IO.Path]::GetFullPath([string]$manifest.repository)
    $approvedParent = Join-Path $repoRoot ("build\rexx\" + $Phase.ToLowerInvariant())
    $runFull = [System.IO.Path]::GetFullPath($RunRoot)
    $workFull = [System.IO.Path]::GetFullPath($WorkingDirectory)

    if (-not (Test-ReXXPathWithin -Path $runFull -Parent $approvedParent) -or $runFull.Equals($approvedParent, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Phase fail-closed preflight: run root must be a new child of $approvedParent; got $runFull"
    }
    if (Test-Path -LiteralPath $runFull) {
        throw "$Phase fail-closed preflight: owned run root already exists: $runFull"
    }
    if (-not (Test-ReXXPathWithin -Path $workFull -Parent $runFull)) {
        throw "$Phase fail-closed preflight: compiler working directory is outside the owned run root: $workFull"
    }
    foreach ($output in $PlannedOutputs) {
        $outputFull = [System.IO.Path]::GetFullPath($output)
        if (-not (Test-ReXXPathWithin -Path $outputFull -Parent $runFull)) {
            throw "$Phase fail-closed preflight: planned output is outside the owned run root: $outputFull"
        }
        if (Test-Path -LiteralPath $outputFull) {
            throw "$Phase fail-closed preflight: planned output already exists: $outputFull"
        }
    }

    $state = Get-ReXXRootArtifactState -Manifest $manifest
    if ($state.missing.Count -or $state.modified.Count -or $state.unexpected.Count) {
        $null = New-Item -ItemType Directory -Path $runFull
        $state | Add-Member -NotePropertyName expected_count -NotePropertyValue @($manifest.artifacts).Count
        $reportPaths = Write-ReXXGuardReport -RunRoot $runFull -Stage 'preflight-failed' -State $state -Passed $false -Phase $Phase
        throw "$Phase fail-closed preflight: protected root artifacts differ from the baseline. See $($reportPaths.Text)"
    }
    $state | Add-Member -NotePropertyName expected_count -NotePropertyValue @($manifest.artifacts).Count

    New-Item -ItemType Directory -Path $runFull | Out-Null
    New-Item -ItemType Directory -Path $workFull | Out-Null
    $beforePath = Join-Path $runFull 'protected-artifacts-before.json'
    [PSCustomObject]@{
        schema = 'guidexos.rexx.artifact-preservation.v1'
        stage = 'before'
        verified_at_utc = [DateTime]::UtcNow.ToString('o')
        repository = $repoRoot
        run_root = $runFull
        compiler_working_directory = $workFull
        allowed_output_root = $runFull
        planned_outputs = @($PlannedOutputs | ForEach-Object { [System.IO.Path]::GetFullPath($_) })
        artifacts = @($state.actual)
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $beforePath -Encoding utf8
    $null = Write-ReXXGuardReport -RunRoot $runFull -Stage 'preflight' -State $state -Passed $true -Phase $Phase
    return [PSCustomObject]@{ RunRoot = $runFull; WorkingDirectory = $workFull; Manifest = $manifest }
}

function Test-ReXXArtifactRun {
    param(
        [Parameter(Mandatory)][string]$ManifestPath,
        [Parameter(Mandatory)][string]$RunRoot,
        [Parameter(Mandatory)][string]$Stage,
        [ValidateSet('RX12R1','RX13')][string]$Phase = 'RX12R1'
    )
    $manifest = Get-ReXXGuardManifest -ManifestPath $ManifestPath
    $runFull = [System.IO.Path]::GetFullPath($RunRoot)
    if (-not (Test-ReXXPathWithin -Path $runFull -Parent (Join-Path $manifest.repository ("build\rexx\" + $Phase.ToLowerInvariant())))) {
        throw "$Phase guard refuses to verify a path outside phase-owned output: $runFull"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $runFull 'protected-artifacts-before.json'))) {
        throw "$Phase guard has no before-run snapshot for $runFull"
    }
    $state = Get-ReXXRootArtifactState -Manifest $manifest
    $state | Add-Member -NotePropertyName expected_count -NotePropertyValue @($manifest.artifacts).Count
    $passed = -not ($state.missing.Count -or $state.modified.Count -or $state.unexpected.Count)
    $reportPaths = Write-ReXXGuardReport -RunRoot $runFull -Stage $Stage -State $state -Passed $passed -Phase $Phase
    Write-Host (Get-Content -Raw -LiteralPath $reportPaths.Text).TrimEnd()
    if (-not $passed) { throw "$Phase artifact preservation failed after '$Stage'. No automatic restoration was attempted. See $($reportPaths.Json)" }
    return $reportPaths
}

function Invoke-ReXXArtifactGuardSyntheticTest {
    param([Parameter(Mandatory)][string]$EvidenceDirectory)
    $caseRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('guidexos-rx12r1-guard-' + [Guid]::NewGuid().ToString('N'))
    $fixtureRoot = Join-Path $caseRoot 'repo'
    $runRoot = Join-Path $fixtureRoot 'build\rexx\rx12r1\guard-selftest'
    $workRoot = Join-Path $runRoot 'temp'
    New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
    try {
        $fixturePath = Join-Path $fixtureRoot 'protected.obj'
        [System.IO.File]::WriteAllText($fixturePath, 'synthetic sentinel')
        $fixtureHash = (Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash.ToLowerInvariant()
        $fixtureManifest = Join-Path $caseRoot 'manifest.json'
        [PSCustomObject]@{
            repository = $fixtureRoot
            protected_paths = @('protected.obj')
            artifacts = @([PSCustomObject]@{ relative_path='protected.obj'; file_type='obj'; size_bytes=(Get-Item $fixturePath).Length; sha256=$fixtureHash; tracked=$false; git_status='?? protected.obj' })
        } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $fixtureManifest -Encoding utf8

        $explicitRejected = $false
        try {
            Start-ReXXArtifactRun -ManifestPath $fixtureManifest -RunRoot $runRoot -WorkingDirectory $workRoot -PlannedOutputs @($fixturePath) | Out-Null
        } catch { $explicitRejected = $_.Exception.Message.Contains('planned output is outside the owned run root') }
        if (-not $explicitRejected) { throw 'Synthetic guard test did not reject an explicit protected-file collision.' }

        $unsafeWorkingRejected = $false
        try {
            Start-ReXXArtifactRun -ManifestPath $fixtureManifest -RunRoot $runRoot -WorkingDirectory $fixtureRoot | Out-Null
        } catch { $unsafeWorkingRejected = $_.Exception.Message.Contains('working directory is outside the owned run root') }
        if (-not $unsafeWorkingRejected) { throw 'Synthetic guard test did not reject an unsafe implicit-output working directory.' }

        $run = Start-ReXXArtifactRun -ManifestPath $fixtureManifest -RunRoot $runRoot -WorkingDirectory $workRoot -PlannedOutputs @((Join-Path $runRoot 'temp\safe.exe'))
        [System.IO.File]::WriteAllText($fixturePath, 'synthetic changed bytes')
        $mutationRejected = $false
        try {
            Test-ReXXArtifactRun -ManifestPath $fixtureManifest -RunRoot $runRoot -Stage 'synthetic-mutation' | Out-Null
        } catch { $mutationRejected = $_.Exception.Message.Contains('artifact preservation failed') }
        if (-not $mutationRejected) { throw 'Synthetic guard test did not report and fail on modified protected bytes.' }

        New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null
        $result = [PSCustomObject]@{
            schema = 'guidexos.rexx.artifact-guard-selftest.v1'
            ran_at_utc = [DateTime]::UtcNow.ToString('o')
            synthetic_only = $true
            protected_fixture = 'protected.obj'
            protected_fixture_initial_sha256 = $fixtureHash
            explicit_protected_output_rejected = $explicitRejected
            unsafe_implicit_output_working_directory_rejected = $unsafeWorkingRejected
            post_run_protected_mutation_detected = $mutationRejected
            passed = $true
            details = 'All cases used a disposable synthetic fixture in the system temporary directory. No repository artifact was opened for writing.'
        }
        $jsonPath = Join-Path $EvidenceDirectory 'guard-synthetic-selftest.json'
        $textPath = Join-Path $EvidenceDirectory 'guard-synthetic-selftest.txt'
        $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $jsonPath -Encoding utf8
        @('RX12R1 artifact guard synthetic self-test: PASS', "Ran UTC: $($result.ran_at_utc)", 'Explicit protected output collision: rejected', 'Unsafe implicit-output working directory: rejected', 'Changed protected bytes: detected; nonzero failure returned', "Synthetic fixture SHA-256: $fixtureHash", 'No real repository artifact was used as a fixture.') | Set-Content -LiteralPath $textPath -Encoding utf8
        return $result
    } finally {
        if (Test-Path -LiteralPath $caseRoot) { Remove-Item -LiteralPath $caseRoot -Recurse -Force }
    }
}

Export-ModuleMember -Function Start-ReXXArtifactRun, Test-ReXXArtifactRun, Invoke-ReXXArtifactGuardSyntheticTest
