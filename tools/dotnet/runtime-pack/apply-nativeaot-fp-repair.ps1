param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$sourceRoot = [System.IO.Path]::GetFullPath($SourceRoot)
$patchPath = Join-Path $PSScriptRoot "patches\nativeaot-amd64-fp-handoff.patch"
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "NativeAOT source root not found: $sourceRoot"
}
if (-not (Test-Path -LiteralPath $patchPath -PathType Leaf)) {
    throw "NativeAOT FP repair patch not found: $patchPath"
}

$stackPath = Join-Path $sourceRoot "src\coreclr\nativeaot\Runtime\StackFrameIterator.cpp"
$coffPath = Join-Path $sourceRoot "src\coreclr\nativeaot\Runtime\windows\CoffNativeCodeManager.cpp"
foreach ($path in @($stackPath, $coffPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "NativeAOT FP repair source file not found: $path"
    }
}

$repoPrefix = $repoRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $sourceRoot.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "NativeAOT FP repair source root must be below the repository: $sourceRoot"
}
$relativeSourceRoot = $sourceRoot.Substring($repoPrefix.Length).Replace('\', '/')
& git -C $repoRoot apply --unidiff-zero --check --whitespace=nowarn --directory="$relativeSourceRoot" "$patchPath"
if ($LASTEXITCODE -ne 0) {
    throw "NativeAOT FP repair patch does not apply cleanly to $sourceRoot"
}
& git -C $repoRoot apply --unidiff-zero --whitespace=nowarn --directory="$relativeSourceRoot" "$patchPath"
if ($LASTEXITCODE -ne 0) {
    throw "NativeAOT FP repair patch application failed for $sourceRoot"
}

Write-Host "[nativeaot-fp-repair] applied durable AMD64 caller-FP and iterator ownership patch" -ForegroundColor Green
Write-Host "[nativeaot-fp-repair] source=$sourceRoot" -ForegroundColor Cyan
Write-Host "[nativeaot-fp-repair] patch=$patchPath" -ForegroundColor Cyan
