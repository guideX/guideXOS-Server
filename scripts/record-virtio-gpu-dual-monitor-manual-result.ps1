param(
    [Parameter(Mandatory = $true)]
    [string]$ChecklistItem,

    [Parameter(Mandatory = $true)]
    [ValidateSet('PASS', 'FAIL', 'BLOCKED', 'NOT TESTED')]
    [string]$Status,

    [string]$Notes = '',
    [string]$PersistenceArtifact = '',
    [string[]]$ScreenshotPaths = @(),
    [string]$SerialLogPath = '',
    [string]$EvidenceRoot = ''
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Find-Qemu {
    $command = Get-Command 'qemu-system-x86_64' -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @(
        'C:\Program Files\qemu\qemu-system-x86_64.exe',
        'C:\Program Files (x86)\qemu\qemu-system-x86_64.exe',
        'C:\qemu\qemu-system-x86_64.exe')) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $Root "logs\manual-validation-$stamp"
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$resultPath = Join-Path $EvidenceRoot "manual-result-$stamp.txt"
$qemu = Find-Qemu
$qemuVersion = if ($qemu) { (& $qemu -version 2>&1 | Select-Object -First 1).Trim() } else { 'unavailable' }
$branch = (& git -C $Root branch --show-current 2>&1).Trim()
$head = (& git -C $Root rev-parse HEAD 2>&1).Trim()

$lines = @(
    'guideXOS Server v0.2 manual QEMU dual-monitor result',
    "dateTimeLocal=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')",
    "dateTimeUtc=$((Get-Date).ToUniversalTime().ToString('o'))",
    "qemuVersion=$qemuVersion",
    "branch=$branch",
    "HEAD=$head",
    "checklistItem=$ChecklistItem",
    "status=$Status",
    "persistenceArtifact=$PersistenceArtifact",
    "serialLogPath=$SerialLogPath",
    "evidenceRoot=$EvidenceRoot",
    'automaticResultInference=no',
    'manualOperatorConfirmationRequired=yes',
    "notes=$Notes",
    'screenshotPaths='
)
$lines += if ($ScreenshotPaths.Count -eq 0) { '  (none supplied)' } else { $ScreenshotPaths | ForEach-Object { "  $_" } }
$lines | Set-Content -LiteralPath $resultPath -Encoding UTF8

Write-Output "Manual result recorded without inference: $resultPath"
