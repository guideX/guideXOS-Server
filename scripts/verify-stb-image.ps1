param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$headerPath = Join-Path $Root "third_party\stb\stb_image.h"
$expectedHash = "1F8C1B6B408F26E3B20CBFBBD4758AFB3DC9B837FF1E17C258928F406148A87C"

if (-not (Test-Path -LiteralPath $headerPath -PathType Leaf)) {
    throw "Missing reproducible stb_image dependency: $headerPath"
}

$sha256 = New-Object System.Security.Cryptography.SHA256Managed
$fileStream = [System.IO.File]::OpenRead($headerPath)
try {
    $hashBytes = $sha256.ComputeHash($fileStream)
}
finally {
    $fileStream.Dispose()
    $sha256.Dispose()
}
$hash = [BitConverter]::ToString($hashBytes).Replace("-", "").ToUpperInvariant()
if ($hash -ne $expectedHash) {
    throw "stb_image.h SHA-256 mismatch. Expected v2.30 hash $expectedHash, got $hash."
}

$firstLine = (Get-Content -LiteralPath $headerPath -TotalCount 1)
if ($firstLine -notmatch "stb_image\s+-\s+v2\.30") {
    throw "stb_image.h is not the pinned v2.30 dependency."
}

Write-Output "stb_image.h reproducibility check PASS (v2.30, SHA-256 $hash)"
