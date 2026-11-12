param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path $RepoRoot).Path
$ManifestPath = Join-Path $RepoRoot "guidexos\libsodium_ed25519_sources.mk"
$InventoryPath = Join-Path $RepoRoot "guidexos\libsodium_ed25519_sources.sha256"
$VendorRoot = Join-Path $RepoRoot "third_party\libsodium"

function Get-Sha256Hex([string]$Path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try {
        return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $InventoryPath -PathType Leaf)) {
    throw "Pinned libsodium Ed25519 manifest or SHA-256 inventory is missing."
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw
foreach ($required in @(
    "GUIDEXOS_ED25519_PROVIDER := libsodium",
    "GUIDEXOS_ED25519_VERSION := 1.0.22",
    "GUIDEXOS_ED25519_COMMIT := 77e1ce5d6dee871c49ef211222ba18ef0c486bda"
)) {
    if (-not $manifest.Contains($required)) {
        throw "Pinned libsodium profile does not contain: $required"
    }
}

$expected = @{}
foreach ($line in Get-Content -LiteralPath $InventoryPath) {
    if ($line -notmatch '^([0-9a-f]{64})  (third_party/libsodium/.+)$') {
        throw "Malformed libsodium SHA-256 inventory row: $line"
    }
    $relative = $Matches[2]
    if ($expected.ContainsKey($relative)) {
        throw "Duplicate libsodium SHA-256 inventory path: $relative"
    }
    $expected[$relative] = $Matches[1]
    $fullPath = Join-Path $RepoRoot ($relative.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Pinned libsodium source file is missing: $relative"
    }
    $actual = Get-Sha256Hex $fullPath
    if ($actual -ne $Matches[1]) {
        throw "Pinned libsodium SHA-256 mismatch for $relative"
    }
}

$actualFiles = @(Get-ChildItem -LiteralPath $VendorRoot -File -Recurse |
    ForEach-Object { $_.FullName.Substring($RepoRoot.Length + 1).Replace('\', '/') } |
    Sort-Object)
$expectedFiles = @($expected.Keys | Sort-Object)
if (($actualFiles -join "`n") -ne ($expectedFiles -join "`n")) {
    throw "libsodium vendor tree differs from its pinned SHA-256 inventory."
}

$manifestSources = [regex]::Matches($manifest, '\$\(GUIDEXOS_ED25519_ROOT\)/([^\s\\]+\.c)') |
    ForEach-Object { "third_party/libsodium/src/$($_.Groups[1].Value)" } |
    Sort-Object
if ($manifestSources.Count -ne 9) {
    throw "Expected 9 selected Ed25519 C sources, found $($manifestSources.Count)."
}
foreach ($source in $manifestSources) {
    if (-not $expected.ContainsKey($source)) {
        throw "Build source is not pinned in the SHA-256 inventory: $source"
    }
}

Write-Output "Pinned libsodium Ed25519 profile verified: 1.0.22 ($($expected.Count) files)."
