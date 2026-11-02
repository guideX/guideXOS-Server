[CmdletBinding()]
param(
    [string]$DeveloperStudioRoot = '',
    [string]$V05ServerRoot = '',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$manifestPath = Join-Path $PSScriptRoot 'phase12-sdk-alignment.json'
$lock = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($DeveloperStudioRoot)) {
    $DeveloperStudioRoot = Join-Path (Split-Path -Parent $repoRoot) 'guideXOS_Developer_Studio'
}
if ([string]::IsNullOrWhiteSpace($V05ServerRoot)) {
    $V05ServerRoot = Join-Path (Split-Path -Parent $repoRoot) 'guideXOSServerV0.5_PHASE4R1_QAC_ADMISSION'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\phase12-sdk-alignment'
}
$DeveloperStudioRoot = [IO.Path]::GetFullPath($DeveloperStudioRoot)
$V05ServerRoot = [IO.Path]::GetFullPath($V05ServerRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing Phase-12 SDK audit output outside repository out/: $OutputDirectory"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to overwrite existing Phase-12 SDK audit output: $OutputDirectory"
}

function Invoke-GitText([string]$Root, [string[]]$Arguments, [string]$Purpose) {
    $result = & git -C $Root @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "PHASE12_SDK_DEPENDENCY_INVALID: $Purpose; git $($Arguments -join ' ') failed: $($result -join ' ')"
    }
    return (($result -join "`n").Trim())
}

function Assert-RepositoryRevision([string]$Root, [string]$ExpectedRevision, [string]$Name) {
    if (!(Test-Path -LiteralPath (Join-Path $Root '.git'))) {
        throw "PHASE12_SDK_DEPENDENCY_MISSING: $Name repository is absent: $Root"
    }
    $actual = Invoke-GitText $Root @('rev-parse', 'HEAD') "$Name revision lookup"
    if ($actual -ne $ExpectedRevision) {
        throw "PHASE12_SDK_DEPENDENCY_REVISION_MISMATCH: $Name expected $ExpectedRevision, found $actual at $Root"
    }
    $state = Invoke-GitText $Root @('status', '--porcelain', '--untracked-files=normal') "$Name worktree check"
    if ($state.Length -ne 0) {
        throw "PHASE12_SDK_DEPENDENCY_DIRTY: $Name at $Root must be clean for the pinned integration check"
    }
}

function Assert-GitBlob([string]$Root, [string]$Revision, [string]$RelativePath, [string]$ExpectedBlob, [string]$Name) {
    $path = Join-Path $Root ($RelativePath.Replace('/', '\'))
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "PHASE12_SDK_DEPENDENCY_MISSING: $Name file is absent: $path"
    }
    $sourceSpec = $Revision + ':' + $RelativePath
    $pinnedBlob = Invoke-GitText $Root @('rev-parse', $sourceSpec) "$Name pinned source lookup"
    $workingBlob = Invoke-GitText $Root @('hash-object', "--path=$RelativePath", '--', $RelativePath) "$Name selected source hash"
    if ($pinnedBlob -ne $ExpectedBlob -or $workingBlob -ne $ExpectedBlob) {
        throw "PHASE12_SDK_DEPENDENCY_CONTENT_MISMATCH: $Name expected git blob $ExpectedBlob from $sourceSpec; pinned=$pinnedBlob working=$workingBlob"
    }
}

if (!(Test-Path -LiteralPath $DeveloperStudioRoot -PathType Container)) {
    throw "PHASE12_SDK_DEPENDENCY_MISSING: clone $($lock.developerStudio.repository) at $($lock.developerStudio.revision); expected Developer Studio root: $DeveloperStudioRoot"
}
Assert-RepositoryRevision $DeveloperStudioRoot $lock.developerStudio.revision 'Developer Studio'
foreach ($source in $lock.developerStudio.sources) {
    Assert-GitBlob $DeveloperStudioRoot $lock.developerStudio.revision $source.path $source.blob 'Developer Studio source'
}

if (!(Test-Path -LiteralPath $V05ServerRoot -PathType Container)) {
    throw "PHASE12_SDK_DEPENDENCY_MISSING: clone $($lock.intendedV05Sdk.repository), fetch $($lock.intendedV05Sdk.ref), and make commit $($lock.intendedV05Sdk.contractRevision) available; expected v0.5 SDK root: $V05ServerRoot"
}
$null = Invoke-GitText $V05ServerRoot @('cat-file', '-e', "$($lock.intendedV05Sdk.contractRevision)^{commit}") 'v0.5 SDK pinned commit check'
foreach ($source in $lock.intendedV05Sdk.headers) {
    Assert-GitBlob $V05ServerRoot $lock.intendedV05Sdk.contractRevision $source.path $source.blob 'v0.5 SDK header'
}
Assert-GitBlob $V05ServerRoot $lock.intendedV05Sdk.contractRevision `
    $lock.intendedV05Sdk.abiLayoutTest.path $lock.intendedV05Sdk.abiLayoutTest.blob 'v0.5 SDK ABI test'

$aarch64AbiPath = Join-Path $repoRoot ($lock.aarch64ServerSdk.abiHeaderPath.Replace('/', '\'))
$aarch64PinnedBlob = Invoke-GitText $repoRoot @('rev-parse', "$($lock.aarch64ServerSdk.baselineRevision):$($lock.aarch64ServerSdk.abiHeaderPath)") 'AARCH64 SDK baseline source'
$aarch64WorkingBlob = Invoke-GitText $repoRoot @('hash-object', "--path=$($lock.aarch64ServerSdk.abiHeaderPath)", '--', $lock.aarch64ServerSdk.abiHeaderPath) 'AARCH64 SDK selected source'
if ($aarch64PinnedBlob -ne $lock.aarch64ServerSdk.abiHeaderBlob -or $aarch64WorkingBlob -ne $lock.aarch64ServerSdk.abiHeaderBlob) {
    throw "PHASE12_AARCH64_ABI_BASELINE_CHANGED: expected Git blob $($lock.aarch64ServerSdk.abiHeaderBlob), baseline=$aarch64PinnedBlob selected=$aarch64WorkingBlob; review and update the ABI contract lock explicitly"
}

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (!$compiler -and (Test-Path -LiteralPath 'C:\mingw64\bin\g++.exe')) {
    $compiler = Get-Item -LiteralPath 'C:\mingw64\bin\g++.exe'
}
if (!$compiler) { throw 'g++.exe was not found; the pinned Developer Studio ABI test uses the repository hosted-test compiler.' }

$studioSourceRoot = Join-Path $DeveloperStudioRoot 'src'
$studioActivationSource = Join-Path $studioSourceRoot 'developer_studio_app_activation.cpp'
$studioActivationHeader = Join-Path $studioSourceRoot 'developer_studio_app_activation.h'
$studioAbiTest = Join-Path $DeveloperStudioRoot 'tests\document_activation_abi_test.cpp'
$v05SdkInclude = Join-Path $V05ServerRoot ($lock.intendedV05Sdk.includeRoot.Replace('/', '\'))
$aarch64SdkInclude = Join-Path $repoRoot ($lock.aarch64ServerSdk.includeRoot.Replace('/', '\'))
$aarch64ContractProbe = Join-Path $repoRoot 'tests\phase12_aarch64_sdk_layout_test.cpp'
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force

# Match Developer Studio's include order: the selected SDK precedes its source tree.
# Record and verify the compiler-resolved SDK path before linking the genuine test.
$depfile = Join-Path $OutputDirectory 'v05-document-activation.d'
$dependencyList = & $compiler.Source -std=c++11 ("-I" + $v05SdkInclude) ("-I" + $studioSourceRoot) `
    -MM -MF $depfile $studioActivationSource $studioAbiTest 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "v0.5 include-path resolution failed: $($dependencyList -join ' ')"
}
$resolvedDependencies = Get-Content -Raw -LiteralPath $depfile
$normalizedDependencies = $resolvedDependencies.Replace('\', '/')
$expectedAbiPath = (Join-Path $v05SdkInclude 'guidexos\abi.h').Replace('\', '/')
if (!$normalizedDependencies.Contains($expectedAbiPath)) {
    throw "The v0.5 include path did not resolve guidexos/abi.h from the pinned SDK: $expectedAbiPath"
}
$wrongAbiPath = (Join-Path $aarch64SdkInclude 'guidexos\abi.h').Replace('\', '/')
if ($normalizedDependencies.Contains($wrongAbiPath)) {
    throw "AARCH64 SDK leaked into the v0.5 include graph: $wrongAbiPath"
}
Write-Host "[phase12-sdk] include resolution: $expectedAbiPath" -ForegroundColor Cyan

# Confirm the Server-side contract probe also resolves the selected AARCH64
# SDK header, with the same SDK-first include ordering used by the integration.
$aarch64Depfile = Join-Path $OutputDirectory 'aarch64-sdk-layout.d'
$aarch64DependencyList = & $compiler.Source -std=c++11 ("-I" + $aarch64SdkInclude) `
    -MM -MF $aarch64Depfile $aarch64ContractProbe 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "AARCH64 include-path resolution failed: $($aarch64DependencyList -join ' ')"
}
$aarch64ResolvedDependencies = (Get-Content -Raw -LiteralPath $aarch64Depfile).Replace('\', '/')
$aarch64ExpectedAbiPath = (Join-Path $aarch64SdkInclude 'guidexos\abi.h').Replace('\', '/')
if (!$aarch64ResolvedDependencies.Contains($aarch64ExpectedAbiPath)) {
    throw "The AARCH64 include path did not resolve guidexos/abi.h from the selected Server SDK: $aarch64ExpectedAbiPath"
}
if ($aarch64ResolvedDependencies.Contains($expectedAbiPath)) {
    throw "The v0.5 SDK leaked into the AARCH64 include graph: $expectedAbiPath"
}
Write-Host "[phase12-sdk] AARCH64 include resolution: $aarch64ExpectedAbiPath" -ForegroundColor Cyan

$v05LayoutExe = Join-Path $OutputDirectory 'v05-native-abi-layout.exe'
& $compiler.Source -std=c++17 -Wall -Wextra -O2 ("-I" + $v05SdkInclude) `
    (Join-Path $V05ServerRoot ($lock.intendedV05Sdk.abiLayoutTest.path.Replace('/', '\'))) `
    -o $v05LayoutExe
if ($LASTEXITCODE -ne 0) { throw 'Pinned v0.5 native ABI layout test did not compile.' }
& $v05LayoutExe
if ($LASTEXITCODE -ne 0) { throw 'Pinned v0.5 native ABI layout test failed.' }

$contractProbe = Join-Path $repoRoot 'tests\phase12_v05_sdk_layout_test.cpp'
$v05ContractExe = Join-Path $OutputDirectory 'v05-sdk-contract.exe'
& $compiler.Source -std=c++11 -Wall -Wextra -pedantic ("-I" + $v05SdkInclude) $contractProbe -o $v05ContractExe
if ($LASTEXITCODE -ne 0) { throw 'v0.5 SDK size, offset, alignment, and architecture checks did not compile.' }
& $v05ContractExe
if ($LASTEXITCODE -ne 0) { throw 'v0.5 SDK contract probe failed.' }

$clangCommand = Get-Command clang++.exe -ErrorAction SilentlyContinue
$clangPath = if ($clangCommand) { $clangCommand.Source } else { 'C:\Program Files\LLVM\bin\clang++.exe' }
if (!(Test-Path -LiteralPath $clangPath -PathType Leaf)) {
    throw 'clang++.exe was not found; the v0.5 contract probe must also be checked for AArch64.'
}
& $clangPath --target=aarch64-none-elf -std=c++11 -ffreestanding -nostdlib -nostdinc++ `
    -fsyntax-only ("-I" + $v05SdkInclude) $contractProbe
if ($LASTEXITCODE -ne 0) { throw 'Pinned v0.5 SDK layout does not satisfy the AArch64 64-bit ABI assumptions.' }
Write-Host '[phase12-sdk] v0.5 contract layout under aarch64-none-elf: PASS' -ForegroundColor Green

$v05ActivationExe = Join-Path $OutputDirectory 'v05-document-activation-abi.exe'
& $compiler.Source -std=c++11 -Wall -Wextra -pedantic ("-I" + $v05SdkInclude) ("-I" + $studioSourceRoot) `
    $studioActivationSource $studioAbiTest -o $v05ActivationExe
if ($LASTEXITCODE -ne 0) { throw 'Pinned v0.5 document activation ABI test did not compile and link.' }
$activationOutput = & $v05ActivationExe
if ($LASTEXITCODE -ne 0 -or (($activationOutput -join "`n") -notmatch 'developerStudioDocumentActivationAbiChecks=9/9')) {
    throw "Pinned v0.5 document activation ABI test failed: $($activationOutput -join ' ')"
}
Write-Output $activationOutput

$aarch64LayoutExe = Join-Path $OutputDirectory 'aarch64-native-abi-layout.exe'
& (Join-Path $PSScriptRoot 'run-native-abi-layout-test.ps1') -OutputPath $aarch64LayoutExe

$aarch64ContractExe = Join-Path $OutputDirectory 'aarch64-sdk-contract.exe'
& $compiler.Source -std=c++11 -Wall -Wextra -pedantic ("-I" + $aarch64SdkInclude) `
    $aarch64ContractProbe -o $aarch64ContractExe
if ($LASTEXITCODE -ne 0) { throw 'AARCH64 SDK size, offset, alignment, and architecture checks did not compile.' }
& $aarch64ContractExe
if ($LASTEXITCODE -ne 0) { throw 'AARCH64 SDK contract probe failed.' }
& $clangPath --target=aarch64-none-elf -std=c++11 -ffreestanding -nostdlib -nostdinc++ `
    -fsyntax-only ("-I" + $aarch64SdkInclude) $aarch64ContractProbe
if ($LASTEXITCODE -ne 0) { throw 'AARCH64 SDK layout does not match the target AArch64 ABI assumptions.' }
Write-Host '[phase12-sdk] 400-byte contract layout under aarch64-none-elf: PASS' -ForegroundColor Green

# The AARCH64 headers must reject the v0.5 consumer at the missing field.
$negativeExe = Join-Path $OutputDirectory 'aarch64-must-reject-v05-activation.exe'
$savedErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    $negativeDiagnostics = & $compiler.Source -std=c++11 -Wall -Wextra -pedantic `
        ("-I" + $aarch64SdkInclude) ("-I" + $studioSourceRoot) `
        $studioActivationSource $studioAbiTest -o $negativeExe 2>&1
    $negativeExit = $LASTEXITCODE
}
finally { $ErrorActionPreference = $savedErrorActionPreference }
$negativeText = $negativeDiagnostics -join "`n"
if ($negativeExit -eq 0) {
    throw 'The 400-byte AARCH64 SDK unexpectedly compiled the v0.5 document activation test.'
}
if ($negativeText -notmatch 'get_document_activation_path' -or $negativeText -notmatch 'no member named') {
    throw "AARCH64 negative control failed for an unexpected reason: $negativeText"
}
if (Test-Path -LiteralPath $negativeExe) {
    throw 'The incompatible SDK negative control left a linked executable.'
}
Write-Host '[phase12-sdk] incompatible AARCH64 SDK rejected the v0.5 document activation consumer: PASS' -ForegroundColor Green
Write-Host '[phase12-sdk] both SDK contracts verified; no ABI definition was substituted.' -ForegroundColor Green
