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
$artifactDirectory = Join-Path $repoRoot 'out\aarch64-phase11'
if (!$SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-aarch64-phase11.ps1') -LlvmRoot $LlvmRoot -OutputDirectory $artifactDirectory
    if ($LASTEXITCODE -ne 0) { throw 'AArch64 Phase 11 build failed' }
}
$template = 'C:\Program Files\qemu\share\edk2-arm-vars.fd'
if (!(Test-Path -LiteralPath $template -PathType Leaf)) { throw "AArch64 UEFI variable template not found: $template" }
$logs = Join-Path $artifactDirectory 'logs'
$null = New-Item -ItemType Directory -Path $logs -Force
for ($boot = 1; $boot -le $Boots; ++$boot) {
    $vars = Join-Path $artifactDirectory ("edk2-aarch64-phase11-vars-{0}.fd" -f $boot)
    Copy-Item -LiteralPath $template -Destination $vars -Force
    $log = Join-Path $logs ("phase11-boot-{0}.log" -f $boot)
    & (Join-Path $PSScriptRoot 'run-aarch64-phase11.ps1') -ArtifactDirectory $artifactDirectory -QemuPath $QemuPath -FirmwareCode $FirmwareCode -FirmwareVars $vars -LogPath $log -TimeoutSeconds $TimeoutSeconds | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Phase 11 boot $boot failed" }
    $text = Get-Content -Raw -LiteralPath $log
    $required = @(
        '[guideXOS] Developer Studio compiler: initialized',
        '[phase11-app] ARM64 code generation: PASS',
        '[phase11-app] ARM64 ELF emission: PASS machine=EM_AARCH64',
        '[phase11-app] AMD64 ELF emission: PASS machine=EM_X86_64',
        '[phase11-app] multiarch package build: PASS',
        '[phase11-app] invalid-source recovery: PASS previous package retained',
        '[phase11-app] source rebuild freshness: PASS build=2 hash-changed',
        '[guideXOS] App Model package refresh: PASS',
        '[guideXOS] selected generated payload: bin/arm64/app.elf',
        '[phase11-app] built by Developer Studio',
        '[phase11-app] architecture: arm64',
        '[phase11-app] computation: PASS build=2',
        '[guideXOS] App Model automatic ARM64 selection: PASS',
        'AARCH64_PHASE11_PASS')
    foreach ($marker in $required) { if (!$text.Contains($marker)) { throw "Boot $boot is missing marker: $marker" } }
    if ($text -match 'AARCH64_PHASE11_ERROR|wrong architecture|unexpected-irq=[1-9]|exceptions=[1-9]') { throw "Boot $boot contains a failure marker" }
    Write-Host "Fresh Phase 11 boot $boot/${Boots}: PASS" -ForegroundColor Green
}
Write-Host "AARCH64_PHASE11_QEMU_HARNESS_PASS boots=$Boots" -ForegroundColor Green
