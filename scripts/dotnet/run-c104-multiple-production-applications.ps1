param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$DotNetExe = "dotnet",
    [string]$PythonExe = "",
    [switch]$SkipManagedBuild,
    [switch]$SkipKernelBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot "out\dotnet\c011ec104-multiple-production-nativeaot-applications"
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$buildRoot = Join-Path $EvidenceRoot "build"
$runtimePackOutputRoot = Join-Path $buildRoot "runtime-pack"
$stagingRoot = Join-Path $EvidenceRoot "staging\wallpaper-pack"
$stagingImage = Join-Path $EvidenceRoot "staging\ramdisk-c104.img"
$buildScript = Join-Path $RepoRoot "scripts\dotnet\build-managed-hostlog-proof.ps1"
$stagingScript = Join-Path $RepoRoot "scripts\generate-wallpaper-pack.ps1"

function Invoke-Checked([string]$FilePath, [string[]]$ArgumentList) {
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($ArgumentList -join ' ')"
    }
}

if (-not $SkipManagedBuild) {
    $common = @(
        "-RepoRoot", $RepoRoot,
        "-RuntimePackRoot", (Join-Path $RepoRoot "tools\dotnet\runtime-pack"),
        "-RuntimePackOutputRoot", $runtimePackOutputRoot,
        "-UseGuideXosRuntimePack",
        "-ProductionApplication",
        "-AllocationMode", "Allocating"
    )
    if ($DotNetExe -ne "dotnet") { $common += @("-DotNetExe", $DotNetExe) }
    if (-not [string]::IsNullOrWhiteSpace($PythonExe)) { $common += @("-PythonExe", $PythonExe) }
    Invoke-Checked "powershell" (@("-ExecutionPolicy", "Bypass", "-File", $buildScript) + $common + @(
        "-OutputRoot", (Join-Path $buildRoot "app-a"),
        "-ManagedProjectMode", "C104AppA",
        "-Clean"
    ))
    Invoke-Checked "powershell" (@("-ExecutionPolicy", "Bypass", "-File", $buildScript) + $common + @(
        "-OutputRoot", (Join-Path $buildRoot "app-b"),
        "-ManagedProjectMode", "C104AppB",
        "-Clean"
    ))
}

$appA = Join-Path $buildRoot "app-a\artifacts\HostLogProof.elf"
$appB = Join-Path $buildRoot "app-b\artifacts\HostLogProof.elf"
if (-not (Test-Path -LiteralPath $appA) -or -not (Test-Path -LiteralPath $appB)) {
    throw "C104 app artifacts are missing. Build both apps or omit -SkipManagedBuild."
}

Invoke-Checked "powershell" @(
    "-ExecutionPolicy", "Bypass", "-File", $stagingScript,
    "-OutputDir", $stagingRoot,
    "-OutputImage", $stagingImage,
    "-C104AppAPath", $appA,
    "-C104AppBPath", $appB
)

if (-not $SkipKernelBuild) {
    Push-Location (Join-Path $RepoRoot "kernel")
    try {
        Invoke-Checked "mingw32-make" @(
            "-B", "ARCH=amd64",
            "EXTRA_CFLAGS=-DGXOS_DESKTOP_CLEANUP_RUNTIME_PASS=2 -DGXOS_NATIVEAOT_PRODUCTION_APPLICATION -DGXOS_C104_PRODUCTION_LAUNCH"
        )
    }
    finally {
        Pop-Location
    }
}

Write-Host "C104 managed artifacts and normal staging are ready." -ForegroundColor Green
Write-Host "App A: $appA" -ForegroundColor Cyan
Write-Host "App B: $appB" -ForegroundColor Cyan
Write-Host "Ramdisk: $stagingImage" -ForegroundColor Cyan
Write-Host "Kernel: $(Join-Path $RepoRoot 'kernel\build\amd64\bin\kernel.elf')" -ForegroundColor Cyan
