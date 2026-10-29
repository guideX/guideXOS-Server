param(
    [Parameter(Mandatory=$true)][string]$CandidateRoot,
    [Parameter(Mandatory=$true)][string]$ManagedApp,
    [Parameter()][string]$RamdiskPath = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188 ramdisk staging requires PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$phaseRoots = @(
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188-external-app-identity')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r3-notes-startup-diagnostics')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r4-notes-startup-operation')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r5-notes-proof-invocation')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r6-notes-postrender')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r7-notes-proof-authorization')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r8-notes-input-routing')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r9-registry-input-reentry')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r10-nativeaot-state-reentry')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r11-managed-state-provenance')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r12r3-lifetime-start-attribution')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r12r4-factory-attribution')),
    [System.IO.Path]::GetFullPath((Join-Path $repo 'out/dotnet/c188r12r4r1-diagnostic-link-repair')))
$root = [System.IO.Path]::GetFullPath($CandidateRoot)
$phaseRoot = @($phaseRoots | Where-Object {
    $relative = [System.IO.Path]::GetRelativePath($_, $root)
    $relative -ne '.' -and -not $relative.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -and
        -not [System.IO.Path]::IsPathRooted($relative)
} | Select-Object -First 1)
if ($phaseRoot.Count -eq 0) {
    throw "C188 candidate root must remain below one of: $($phaseRoots -join ', ')."
}
$phaseRoot = $phaseRoot[0]
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "C188 candidate root does not exist: $root"
}
if ([string]::IsNullOrWhiteSpace($RamdiskPath)) {
    $RamdiskPath = Join-Path $root 'esp/ramdisk.img'
}
$ramdisk = [System.IO.Path]::GetFullPath($RamdiskPath)
$manifest = Join-Path $root 'ramdisk-staging.json'
foreach ($path in @($ramdisk,$manifest)) {
    $full = [System.IO.Path]::GetFullPath($path)
    if (-not $full.StartsWith($root.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "C188 staging path must remain below candidate root: $full"
    }
}
if (-not (Test-Path -LiteralPath $ramdisk -PathType Leaf)) { throw "C188 ramdisk is missing: $ramdisk" }
if (Test-Path -LiteralPath $manifest) { throw "Refusing to overwrite C188 staging evidence: $manifest" }
$app = (Resolve-Path -LiteralPath $ManagedApp).Path
if (-not $app.StartsWith($phaseRoot.TrimEnd('\','/') + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "C188 managed ELF must be an isolated build below $phaseRoot."
}
$python = Get-Command python.exe -ErrorAction Stop | Select-Object -First 1
$writer = Join-Path $PSScriptRoot 'stage-c187-ramdisk.py'
$output = (& $python.Source $writer --candidate-root $root --ramdisk $ramdisk `
    --managed-app $app --manifest $manifest 2>&1 | Out-String)
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) { throw "C188 FAT32 staging failed with exit code $exitCode.`n$output" }
$result = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json -ErrorAction Stop
if ($result.result -cne 'PASS' -or $result.targetPath -cne '/APPS/GXOSAPP.ELF' -or
    $result.changedOtherFiles -ne 0 -or $result.unrelatedFilesPreserved -lt 1 -or
    $result.stagedAppSha256 -cne (Get-FileHash -LiteralPath $app -Algorithm SHA256).Hash) {
    throw "C188 ramdisk staging manifest failed its post-write checks: $manifest"
}
Write-Output "C188_RAMDISK_STAGE=PASS TARGET=$($result.targetPath) APP_SHA256=$($result.stagedAppSha256) RAMDISK_SHA256=$($result.ramdiskSha256) PRESERVED_FILES=$($result.unrelatedFilesPreserved)"
Write-Output "C188_RAMDISK_STAGE_MANIFEST=$manifest"
