param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,
    [Parameter(Mandatory = $true)]
    [string]$RuntimeCommit,
    [string]$LockPath = "",
    [string]$PatchPath = "",
    [string]$ResultPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-Hash([string]$Path) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha256.ComputeHash([System.IO.File]::ReadAllBytes($Path))) -replace '-', '').ToUpperInvariant() }
    finally { $sha256.Dispose() }
}

function Fail-Closed([string]$Category, [string]$Detail) {
    $message = "[nativeaot-fp-repair] state=FAIL category=$Category detail=$Detail"
    Write-Error $message
    throw $message
}

function Assert-WithinRoot([string]$Path, [string]$Root, [string]$Label) {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        Fail-Closed "SOURCE_SCOPE" "$Label escapes its allowed root: $fullPath"
    }
}

function Write-Result([System.Collections.IDictionary]$Result, [string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return }
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $fullPath) | Out-Null
    $Result | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $fullPath -Encoding ASCII
}

function Test-PostimageMarkers([string]$Text, [object]$Target) {
    $markers = @($Target.postimageMarkers)
    foreach ($marker in $markers) {
        if (-not $Text.Contains([string]$marker)) { return $false }
    }
    return $true
}

function Assert-PostimageStructure([string]$RelativePath, [string]$Text) {
    if ($RelativePath -like '*StackFrameIterator.cpp') {
        $publish = $Text.IndexOf('m_FramePointer = (PTR_VOID)m_RegDisplay.GetFP();', [System.StringComparison]::Ordinal)
        $rehome = $Text.IndexOf('m_RegDisplay.pRbp = (PTR_uintptr_t)&m_FramePointer;', [System.StringComparison]::Ordinal)
        $preserve = $Text.IndexOf('uintptr_t publishedFramePointer', [System.StringComparison]::Ordinal)
        $restore = $Text.IndexOf('*m_RegDisplay.pRbp = publishedFramePointer;', [System.StringComparison]::Ordinal)
        if ($publish -lt 0 -or $rehome -le $publish -or $preserve -lt 0 -or $restore -le $preserve) {
            Fail-Closed "POSTIMAGE_INVALID" "StackFrameIterator FP publication/re-home ownership order is not present in $RelativePath"
        }
        return
    }
    if ($RelativePath -like '*CoffNativeCodeManager.cpp') {
        $storage = $Text.IndexOf('PTR_uintptr_t callerRbpStorage = pRegisterSet->pRbp;', [System.StringComparison]::Ordinal)
        $publish = $Text.IndexOf('*callerRbpStorage = (uintptr_t)context.Rbp;', [System.StringComparison]::Ordinal)
        $restore = $Text.IndexOf('pRegisterSet->pRbp = callerRbpStorage;', [System.StringComparison]::Ordinal)
        if ($storage -lt 0 -or $publish -le $storage -or $restore -le $publish) {
            Fail-Closed "POSTIMAGE_INVALID" "CoffNativeCodeManager caller-FP publication/storage order is not present in $RelativePath"
        }
        return
    }
    Fail-Closed "PATCH_IDENTITY" "Unexpected patch target: $RelativePath"
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\.."))
$sourceRoot = [System.IO.Path]::GetFullPath($SourceRoot)
if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    Fail-Closed "MISSING_SOURCE" "NativeAOT source root not found: $sourceRoot"
}
Assert-WithinRoot $sourceRoot $repoRoot "NativeAOT FP repair source root"

if ([string]::IsNullOrWhiteSpace($LockPath)) {
    $LockPath = Join-Path $PSScriptRoot "runtime-pack.lock.json"
}
$LockPath = [System.IO.Path]::GetFullPath($LockPath)
if (-not (Test-Path -LiteralPath $LockPath -PathType Leaf)) {
    Fail-Closed "LOCK_MISSING" "Runtime-pack lock file not found: $LockPath"
}
$lock = Get-Content -LiteralPath $LockPath -Raw | ConvertFrom-Json
$repair = $lock.nativeAotFpRepair
if ($null -eq $repair) {
    Fail-Closed "LOCK_INVALID" "Runtime-pack lock has no nativeAotFpRepair identity."
}

$expectedCommit = ([string]$repair.sourceCommit).Trim().ToLowerInvariant()
$suppliedCommit = $RuntimeCommit.Trim().ToLowerInvariant()
if ([string]::IsNullOrWhiteSpace($suppliedCommit) -or $suppliedCommit -ne $expectedCommit) {
    Fail-Closed "WRONG_RUNTIME_IDENTITY" "Expected locked source commit $expectedCommit, got $RuntimeCommit"
}
if ($suppliedCommit -notmatch '^[0-9a-f]{40}$') {
    Fail-Closed "WRONG_RUNTIME_IDENTITY" "RuntimeCommit is not a full Git commit identity: $RuntimeCommit"
}

if ([string]::IsNullOrWhiteSpace($PatchPath)) {
    $PatchPath = Join-Path $PSScriptRoot ([string]$repair.patch).Replace('/', '\')
}
$PatchPath = [System.IO.Path]::GetFullPath($PatchPath)
if (-not (Test-Path -LiteralPath $PatchPath -PathType Leaf)) {
    Fail-Closed "PATCH_MISSING" "NativeAOT FP repair patch not found: $PatchPath"
}
$patchHash = Get-Hash $PatchPath
$expectedPatchHash = ([string]$repair.patchSha256).ToUpperInvariant()
if ($patchHash -ne $expectedPatchHash) {
    Fail-Closed "PATCH_IDENTITY" "Patch SHA-256 mismatch. Expected $expectedPatchHash, got $patchHash"
}

$targetProperties = @($repair.targetFiles.PSObject.Properties)
if ($targetProperties.Count -ne 2) {
    Fail-Closed "PATCH_IDENTITY" "Expected exactly two locked NativeAOT FP patch targets."
}

$targetDetails = [ordered]@{}
$allPreimage = $true
$allPostimage = $true
$anyPostMarker = $false
foreach ($property in $targetProperties) {
    $relativePath = ([string]$property.Name).Replace('/', '\')
    $targetPath = Join-Path $sourceRoot $relativePath
    if (-not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
        Fail-Closed "MISSING_TARGET" "NativeAOT FP repair source file not found: $targetPath"
    }
    $targetText = Get-Content -LiteralPath $targetPath -Raw
    $actualHash = Get-Hash $targetPath
    $preimageHash = ([string]$property.Value.preimageSha256).ToUpperInvariant()
    $postimageHash = ([string]$property.Value.postimageSha256).ToUpperInvariant()
    $isPreimage = $actualHash -eq $preimageHash
    $isPostimage = $actualHash -eq $postimageHash
    $markerMatches = Test-PostimageMarkers $targetText $property.Value
    $allPreimage = $allPreimage -and $isPreimage
    $allPostimage = $allPostimage -and $isPostimage -and $markerMatches
    $anyPostMarker = $anyPostMarker -or $markerMatches
    $targetDetails[$property.Name] = [ordered]@{
        path = $targetPath
        sha256Before = $actualHash
        expectedPreimageSha256 = $preimageHash
        expectedPostimageSha256 = $postimageHash
        preimageMatch = $isPreimage
        postimageMatch = $isPostimage
        postimageMarkersMatch = $markerMatches
        lengthBefore = (Get-Item -LiteralPath $targetPath).Length
    }
}

$stateBefore = if ($allPostimage) {
    "ALREADY_PATCHED_CORRECTLY"
} elseif ($allPreimage) {
    "PRISTINE_EXPECTED"
} elseif ($anyPostMarker) {
    "PARTIAL_APPLICATION"
} else {
    "SOURCE_DRIFT"
}

if ($stateBefore -eq "ALREADY_PATCHED_CORRECTLY") {
    foreach ($property in $targetProperties) {
        $relativePath = ([string]$property.Name).Replace('/', '\')
        $targetPath = Join-Path $sourceRoot $relativePath
        Assert-PostimageStructure $relativePath (Get-Content -LiteralPath $targetPath -Raw)
        $targetDetails[$property.Name].sha256After = Get-Hash $targetPath
    }
    $result = [ordered]@{
        schemaVersion = 1
        identity = "nativeaot-amd64-fp-handoff"
        patchVersion = [string]$repair.patchVersion
        sourceCommit = $expectedCommit
        patchPath = $PatchPath
        patchSha256 = $patchHash
        stateBefore = $stateBefore
        stateAfter = "ALREADY_PATCHED_CORRECTLY"
        action = "NONE"
        policy = [string]$repair.policy
        targetFiles = $targetDetails
        preimageVerification = "NOT_APPLICABLE"
        postimageVerification = "PASS"
    }
    Write-Result $result $ResultPath
    Write-Host "[nativeaot-fp-repair] state=ALREADY_PATCHED_CORRECTLY action=NONE sourceCommit=$expectedCommit patchSha256=$patchHash" -ForegroundColor Cyan
    return
}

if ($stateBefore -ne "PRISTINE_EXPECTED") {
    Fail-Closed $stateBefore "Target files are neither the exact locked preimage nor the exact verified postimage. No patch was applied."
}

$repoPrefix = $repoRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
$relativeSourceRoot = $sourceRoot.Substring($repoPrefix.Length).Replace('\', '/')
$checkOutput = @(& git -C $repoRoot apply --unidiff-zero --check --whitespace=nowarn --directory=$relativeSourceRoot $PatchPath 2>&1)
$checkExit = $LASTEXITCODE
if ($checkExit -ne 0) {
    Fail-Closed "PATCH_APPLY_CHECK" (($checkOutput -join " ").Trim())
}
$applyOutput = @(& git -C $repoRoot apply --unidiff-zero --whitespace=nowarn --directory=$relativeSourceRoot $PatchPath 2>&1)
$applyExit = $LASTEXITCODE
if ($applyExit -ne 0) {
    Fail-Closed "PATCH_APPLY" (($applyOutput -join " ").Trim())
}

foreach ($property in $targetProperties) {
    $relativePath = ([string]$property.Name).Replace('/', '\')
    $targetPath = Join-Path $sourceRoot $relativePath
    $targetText = Get-Content -LiteralPath $targetPath -Raw
    $actualHash = Get-Hash $targetPath
    $expectedPostimageHash = ([string]$property.Value.postimageSha256).ToUpperInvariant()
    if ($actualHash -ne $expectedPostimageHash -or -not (Test-PostimageMarkers $targetText $property.Value)) {
        Fail-Closed "POSTIMAGE_INVALID" "Postimage verification failed for $targetPath. Expected $expectedPostimageHash, got $actualHash"
    }
    Assert-PostimageStructure $relativePath $targetText
    $targetDetails[$property.Name].sha256After = $actualHash
    $targetDetails[$property.Name].lengthAfter = (Get-Item -LiteralPath $targetPath).Length
}

$result = [ordered]@{
    schemaVersion = 1
    identity = "nativeaot-amd64-fp-handoff"
    patchVersion = [string]$repair.patchVersion
    sourceCommit = $expectedCommit
    patchPath = $PatchPath
    patchSha256 = $patchHash
    stateBefore = $stateBefore
    stateAfter = "PATCHED_CORRECTLY"
    action = "APPLIED"
    policy = [string]$repair.policy
    targetFiles = $targetDetails
    preimageVerification = "PASS"
    postimageVerification = "PASS"
}
Write-Result $result $ResultPath
Write-Host "[nativeaot-fp-repair] state=PRISTINE_EXPECTED action=APPLIED stateAfter=PATCHED_CORRECTLY sourceCommit=$expectedCommit patchSha256=$patchHash" -ForegroundColor Green
Write-Host "[nativeaot-fp-repair] source=$sourceRoot" -ForegroundColor Cyan
Write-Host "[nativeaot-fp-repair] patch=$PatchPath" -ForegroundColor Cyan
