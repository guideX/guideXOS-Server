param(
    [string]$RepoRoot = "",
    [string]$EvidenceRoot = "",
    [int]$TimeoutSeconds = 120,
    [int]$DiscoveryFreshBootCount = 1,
    [int]$ConfirmationFreshBootCount = 3,
    [string]$RuntimePackManifest = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
$root = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $root "out\dotnet\c011ec73-promotion-positive-region-cohort"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
if ([string]::IsNullOrWhiteSpace($RuntimePackManifest)) {
    $RuntimePackManifest = Join-Path $root "out\dotnet\runtime-pack-c68\runtime-pack.manifest.json"
}
$RuntimePackManifest = [System.IO.Path]::GetFullPath($RuntimePackManifest)
$smoke = Join-Path $root "scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1"
if (-not (Test-Path -LiteralPath $smoke)) { throw "C011EC73 smoke harness was not found: $smoke" }
if (-not (Test-Path -LiteralPath $RuntimePackManifest)) { throw "C011EC73 runtime-pack manifest was not found: $RuntimePackManifest" }
if ($DiscoveryFreshBootCount -lt 1 -or $ConfirmationFreshBootCount -lt 1) { throw "Fresh-boot counts must be positive." }

function Get-NormalizedSerial([string]$Path) {
    $text = Get-Content -LiteralPath $Path -Raw
    $text = $text -replace '\[IRQ\] dispatch irq=00\s*', ''
    $text = ($text -creplace '(?<=[0-9])(?=[a-z])', ' ') -replace '[ \t]+', ' '
    $text = $text -replace '\b(c\d+)\s+(ec\d+)', '$1$2'
    return $text -replace '\s*=\s*', '='
}

function Get-LatestRun([string]$CaseRoot) {
    $runs = @(Get-ChildItem -LiteralPath $CaseRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like 'run-*' } | Sort-Object LastWriteTime | Select-Object -Last 1)
    if ($runs.Count -ne 1) { return $null }
    $serial = Join-Path $runs[0].FullName 'first-run\serial.log'
    if (-not (Test-Path -LiteralPath $serial)) { return [ordered]@{ directory=$runs[0].FullName; serial=$null } }
    return [ordered]@{ directory=$runs[0].FullName; serial=$serial }
}

function Get-MarkerLine([string]$Text, [string]$Marker) {
    $lines = @($Text -split "`n" | Where-Object { $_ -match ("marker=" + [regex]::Escape($Marker) + "(?:\s|$)") })
    if ($lines.Count -eq 0) { return $null }
    return $lines[-1].Trim()
}

function Get-Field([string]$Line, [string]$Name) {
    if ([string]::IsNullOrWhiteSpace($Line)) { return $null }
    $match = [regex]::Match($Line, '(?:^|\s)' + [regex]::Escape($Name) + '=(?<value>(?:0x)?[0-9A-Fa-f]+)')
    if (-not $match.Success) { return $null }
    return $match.Groups['value'].Value
}

function Get-Hex([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return $null }
    if ($Value.StartsWith('0x')) { return [Convert]::ToUInt64($Value.Substring(2), 16) }
    return [Convert]::ToUInt64($Value, 16)
}

function Invoke-C73Case([string]$Name, [string]$C73Case, [int]$TailAllocations, [int]$FreshBootCount, [uint64]$ExpectedObjectSize, [uint64]$ExpectedLiveBytes, [uint64]$ExpectedPostRestart) {
    $caseRoot = Join-Path $EvidenceRoot $Name
    New-Item -ItemType Directory -Force -Path $caseRoot | Out-Null
    $arguments = @{
        RepoRoot=$root; EvidenceRoot=$caseRoot; TimeoutSeconds=$TimeoutSeconds; FreshBootCount=$FreshBootCount
        ProofMode='promotion-positive-region-cohort'; C71Case=$C73Case; C66Strategy='P2'
        C66TailAllocations=$TailAllocations; RuntimePackManifest=$RuntimePackManifest
    }
    $harnessError = $null
    try {
        $null = & $smoke @arguments *>&1 | Out-String
    } catch {
        $harnessError = $_.Exception.Message
    }
    $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
    $latest = Get-LatestRun $caseRoot
    $record = [ordered]@{ name=$Name; c73Case=$C73Case; tailAllocations=$TailAllocations; freshBootCount=$FreshBootCount; expectedObjectSize=('0x{0:X}' -f $ExpectedObjectSize); expectedLiveBytes=('0x{0:X}' -f $ExpectedLiveBytes); expectedPostRestart=('0x{0:X}' -f $ExpectedPostRestart); exitCode=$exitCode; harnessError=$harnessError; runDirectory=if($null -ne $latest){$latest.directory}else{$null}; status='invalid' }
    if ($null -eq $latest -or [string]::IsNullOrWhiteSpace($latest.serial) -or -not (Test-Path -LiteralPath $latest.serial)) {
        $record.status = 'baseline-divergence-no-serial'
        return $record
    }
    $text = Get-NormalizedSerial $latest.serial
    $complete = Get-MarkerLine $text 'C011EC73'
    $summary = Get-MarkerLine $text 'C73_SUMMARY'
    $state = Get-MarkerLine $text 'C73_FIRST_PRODUCTION_STATE'
    $postRestart = Get-MarkerLine $text 'C73_POST_RESTART_BASIC_COUNT'
    $resume = Get-MarkerLine $text 'C73_MANAGED_RESUME_BASIC_COUNT'
    if ($null -eq $complete -or $null -eq $summary -or $null -eq $state -or $null -eq $postRestart -or $null -eq $resume) {
        $record.status = 'baseline-divergence-incomplete-markers'
        $record.serial=$latest.serial; $record.complete=$complete; $record.summary=$summary; $record.state=$state; $record.postRestart=$postRestart; $record.resume=$resume
        return $record
    }
    $actualObjectSize = Get-Hex (Get-Field $summary 'objectSize')
    $actualLiveBytes = Get-Hex (Get-Field $summary 'retainedLiveBytes')
    $actualPostRestart = Get-Hex (Get-Field $summary 'postRestartBasicCount')
    $record.serial=$latest.serial; $record.serialSha256=(Get-FileHash -LiteralPath $latest.serial -Algorithm SHA256).Hash
    $record.complete=$complete; $record.summary=$summary; $record.state=$state; $record.postRestart=$postRestart; $record.resume=$resume
    $record.actualObjectSize=('0x{0:X}' -f $actualObjectSize); $record.actualLiveBytes=('0x{0:X}' -f $actualLiveBytes); $record.actualPostRestart=('0x{0:X}' -f $actualPostRestart)
    $record.promotionObserved=Get-Field $complete 'promotionObserved'; $record.invariantFailures=Get-Field $complete 'invariantFailures'; $record.eventOverflow=Get-Field $complete 'eventOverflow'; $record.regionOverflow=Get-Field $complete 'regionOverflow'
    if ($actualObjectSize -ne $ExpectedObjectSize -or $actualLiveBytes -ne $ExpectedLiveBytes -or $actualPostRestart -ne $ExpectedPostRestart -or (Get-Hex $record.promotionObserved) -ne 1 -or (Get-Hex $record.invariantFailures) -ne 0 -or (Get-Hex $record.eventOverflow) -ne 0 -or (Get-Hex $record.regionOverflow) -ne 0) {
        $record.status='baseline-divergence-semantic-mismatch'
        return $record
    }
    $record.status='pass'
    return $record
}

New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$records = @()
$six = Invoke-C73Case 'baseline-six' 'baseline16' 216 $DiscoveryFreshBootCount 0x10018 0x100180 6
$records += $six
$matrixPath = Join-Path $EvidenceRoot 'c73-matrix.json'
$matrix = [ordered]@{ milestone='C011EC73 Promotion-Positive Region-Cohort Determinant'; gate='Baseline SIX and Baseline ONE must pass before Phase 1-3'; records=$records; phase='baseline' }
$matrix | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $matrixPath -Encoding ASCII
if ($six.status -ne 'pass') { throw "C011EC73 stopped at Baseline SIX divergence: $($six.status). Matrix: $matrixPath" }

$one = Invoke-C73Case 'baseline-one' '15mid8' 320 $DiscoveryFreshBootCount 0x10E80 0xFD980 1
$records += $one
$matrix.records=$records
$matrix | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $matrixPath -Encoding ASCII
if ($one.status -ne 'pass') { throw "C011EC73 stopped at Baseline ONE divergence: $($one.status). Matrix: $matrixPath" }

$candidates = @(
    [ordered]@{ name='15matchlow'; case='15matchlow'; object=0x11128; live=0x100158 },
    [ordered]@{ name='15matchhigh'; case='15matchhigh'; object=0x11130; live=0x1001D0 },
    [ordered]@{ name='16below'; case='16below'; object=0xFFF8; live=0xFFF80 }
)
foreach ($candidate in $candidates) {
    $records += Invoke-C73Case $candidate.name $candidate.case 320 $DiscoveryFreshBootCount $candidate.object $candidate.live 0
}
$matrix.records=$records; $matrix.phase='discovery'; $matrix | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $matrixPath -Encoding ASCII

$final = @($records | Where-Object { $_.status -eq 'pass' -and $_.name -in @('baseline-six','baseline-one','15matchlow','15matchhigh','16below') })
foreach ($record in $final) {
    $confirmation = Invoke-C73Case $record.name $record.c73Case $record.tailAllocations $ConfirmationFreshBootCount (Get-Hex $record.expectedObjectSize) (Get-Hex $record.expectedLiveBytes) (Get-Hex $record.expectedPostRestart)
    $records += $confirmation
}
$matrix.records=$records; $matrix.phase='confirmation'; $matrix | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $matrixPath -Encoding ASCII
Write-Host "C011EC73 matrix manifest: $matrixPath" -ForegroundColor Green
