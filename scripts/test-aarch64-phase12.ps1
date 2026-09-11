[CmdletBinding()]
param(
    [string]$LlvmRoot = 'C:\Program Files\LLVM\bin',
    [string]$QemuPath = 'C:\Program Files\qemu\qemu-system-aarch64.exe',
    [string]$FirmwareCode = 'C:\Program Files\qemu\share\edk2-aarch64-code.fd',
    [int]$Boots = 3,
    [int]$TimeoutSeconds = 900,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase12'
if (!$SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-aarch64-phase12.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
    if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 12 build failed' }
}
$template = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'
if (!(Test-Path -LiteralPath $template -PathType Leaf)) { throw "AArch64 UEFI variable template not found: $template" }
$logs = Join-Path $artifactDirectory 'logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
for ($boot = 1; $boot -le $Boots; ++$boot) {
    $vars = Join-Path $artifactDirectory ("edk2-aarch64-phase12-vars-{0}.fd" -f $boot)
    Copy-Item -LiteralPath $template -Destination $vars -Force
    $log = Join-Path $logs ("phase12-boot-{0}.log" -f $boot)
    & (Join-Path $PSScriptRoot 'run-aarch64-phase12.ps1') -ArtifactDirectory $artifactDirectory -QemuPath $QemuPath -FirmwareCode $FirmwareCode -FirmwareVars $vars -LogPath $log -TimeoutSeconds $TimeoutSeconds | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Phase 12 boot $boot failed" }
    $text = Get-Content -Raw -LiteralPath $log
    $required = @(
        '[guideXOS] Developer Studio SDK compatibility: PASS',
        '[guideXOS] Developer Studio ARM64 launch: PASS',
        '[developer-studio] project opened: Phase12IDEProof',
        '[developer-studio] target: multi',
        '[developer-studio] build result: arm64 PASS',
        '[developer-studio] build result: amd64 PASS',
        '[guideXOS] resident compiler target: arm64',
        '[guideXOS] resident compiler target: amd64',
        '[guideXOS] ARM64 artifact: valid',
        '[guideXOS] AMD64 sibling: valid',
        '[guideXOS] generated ELF: EM_AARCH64',
        '[guideXOS] generated ELF: EM_X86_64',
        '[developer-studio] multiarch package: PASS',
        '[developer-studio] source edited: invalid',
        '[developer-studio] invalid source diagnostics: PASS',
        '[developer-studio] previous package survived failure: PASS',
        '[developer-studio] recovery rebuild: PASS',
        '[developer-studio] Run: ARM64 payload selected',
        '[developer-studio] Run output: PASS',
        '[developer-studio] Run cleanup: PASS',
        '[developer-studio] source edit: build=2',
        '[developer-studio] rebuild freshness: PASS',
        '[developer-studio] compile failure recovery: PASS',
        '[guideXOS] Developer Studio cleanup: PASS',
        '[phase12-app] build=2',
        'AARCH64_PHASE12_PASS')
    foreach ($marker in $required) { if (!$text.Contains($marker)) { throw "Boot $boot is missing marker: $marker" } }
    if ($text -match 'AARCH64_PHASE12_ERROR|unexpected-irq=[1-9]|exceptions=[1-9]') { throw "Boot $boot contains a failure marker" }
    Write-Host "Fresh Phase 12 boot $boot/${Boots}: PASS" -ForegroundColor Green
}
Write-Host "AARCH64_PHASE12_QEMU_HARNESS_PASS boots=$Boots" -ForegroundColor Green
