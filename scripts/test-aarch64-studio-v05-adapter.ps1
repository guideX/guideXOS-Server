[CmdletBinding()]
param(
    [string]$DeveloperStudioRoot = '',
    [string]$V05ServerRoot = '',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lock = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'phase12-sdk-alignment.json') | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($DeveloperStudioRoot)) {
    $DeveloperStudioRoot = Join-Path (Split-Path -Parent $repoRoot) 'guideXOS_Developer_Studio'
}
if ([string]::IsNullOrWhiteSpace($V05ServerRoot)) {
    $V05ServerRoot = Join-Path (Split-Path -Parent $repoRoot) 'guideXOSServerV0.5_PHASE4R1_QAC_ADMISSION'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-studio-v05-adapter-host'
}
$DeveloperStudioRoot = [IO.Path]::GetFullPath($DeveloperStudioRoot)
$V05ServerRoot = [IO.Path]::GetFullPath($V05ServerRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outPrefix = $repoRoot.TrimEnd('\') + '\out\'
if (!$OutputDirectory.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing adapter-test output outside repository out/: $OutputDirectory"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to overwrite existing adapter-test output: $OutputDirectory"
}

function Invoke-GitText([string]$Root, [string[]]$Arguments, [string]$Purpose) {
    $result = & git -C $Root @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "PHASE12_ADAPTER_DEPENDENCY_INVALID: $Purpose; git $($Arguments -join ' ') failed: $($result -join ' ')"
    }
    return (($result -join "`n").Trim())
}

if (!(Test-Path -LiteralPath $DeveloperStudioRoot -PathType Container)) {
    throw "Pinned Developer Studio checkout is missing: $DeveloperStudioRoot"
}
$studioRevision = Invoke-GitText $DeveloperStudioRoot @('rev-parse','HEAD') 'Developer Studio revision'
$studioState = Invoke-GitText $DeveloperStudioRoot @('status','--porcelain','--untracked-files=normal') 'Developer Studio clean state'
if ($studioRevision -ne $lock.developerStudio.revision -or $studioState.Length -ne 0) {
    throw "PHASE12_ADAPTER_STUDIO_MISMATCH: expected clean $($lock.developerStudio.revision), found $studioRevision; state=$studioState"
}
foreach ($source in $lock.developerStudio.sources) {
    $pinned = Invoke-GitText $DeveloperStudioRoot @('rev-parse',"$($lock.developerStudio.revision):$($source.path)") 'Studio locked source'
    $working = Invoke-GitText $DeveloperStudioRoot @('hash-object',"--path=$($source.path)",'--',$source.path) 'Studio selected source'
    if ($pinned -ne $source.blob -or $working -ne $source.blob) {
        throw "PHASE12_ADAPTER_STUDIO_SOURCE_MISMATCH: $($source.path) expected blob $($source.blob), pinned=$pinned selected=$working"
    }
}

if (!(Test-Path -LiteralPath $V05ServerRoot -PathType Container)) {
    throw "Pinned v0.5 SDK checkout is missing: $V05ServerRoot"
}
$null = Invoke-GitText $V05ServerRoot @('cat-file','-e',"$($lock.intendedV05Sdk.contractRevision)^{commit}") 'v0.5 SDK pinned commit'
foreach ($source in $lock.intendedV05Sdk.headers) {
    $pinned = Invoke-GitText $V05ServerRoot @('rev-parse',"$($lock.intendedV05Sdk.contractRevision):$($source.path)") 'v0.5 SDK locked header'
    $working = Invoke-GitText $V05ServerRoot @('hash-object',"--path=$($source.path)",'--',$source.path) 'v0.5 SDK selected header'
    if ($pinned -ne $source.blob -or $working -ne $source.blob) {
        throw "PHASE12_ADAPTER_SDK_HEADER_MISMATCH: $($source.path) expected blob $($source.blob), pinned=$pinned selected=$working"
    }
}

$aarch64AbiPath = Join-Path $repoRoot ($lock.aarch64ServerSdk.abiHeaderPath.Replace('/','\'))
$aarch64Pinned = Invoke-GitText $repoRoot @('rev-parse',"$($lock.aarch64ServerSdk.baselineRevision):$($lock.aarch64ServerSdk.abiHeaderPath)") 'AARCH64 ABI baseline'
$aarch64Selected = Invoke-GitText $repoRoot @('hash-object',"--path=$($lock.aarch64ServerSdk.abiHeaderPath)",'--',$lock.aarch64ServerSdk.abiHeaderPath) 'AARCH64 ABI selected header'
if ($aarch64Pinned -ne $lock.aarch64ServerSdk.abiHeaderBlob -or $aarch64Selected -ne $lock.aarch64ServerSdk.abiHeaderBlob) {
    throw 'PHASE12_ADAPTER_AARCH64_ABI_CHANGED: the 400-byte AARCH64 baseline differs from the pinned ABI1 lock.'
}
$manifest = Get-Content -Raw -LiteralPath (Join-Path $repoRoot 'Apps\DeveloperStudio\app.json') | ConvertFrom-Json
$arm64Entry = @($manifest.entries | Where-Object { $_.architecture -eq 'arm64' })
if ($arm64Entry.Count -ne 1 -or $arm64Entry[0].abi -ne 'guidexos-c-abi-v1-studio-v05-464') {
    throw 'PHASE12_ADAPTER_IDENTITY_INVALID: ARM64 Developer Studio manifest must select the exact versioned adapter identity.'
}

$compilerCommand = Get-Command g++.exe -ErrorAction SilentlyContinue
$compiler = if ($compilerCommand) { $compilerCommand.Source } else { 'C:\mingw64\bin\g++.exe' }
if (!(Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'g++.exe was not found for the hosted adapter proof.' }
$v05Include = Join-Path $V05ServerRoot ($lock.intendedV05Sdk.includeRoot.Replace('/','\'))
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$exe = Join-Path $OutputDirectory 'aarch64-studio-v05-adapter-tests.exe'
$abiHeader = (Join-Path $v05Include 'guidexos\abi.h').Replace('\','/')
$appHeader = (Join-Path $v05Include 'guidexos\app.h').Replace('\','/')
$configHeader = Join-Path $OutputDirectory 'studio_v05_header_paths.h'
$configContents = "#define GXOS_STUDIO_V05_ABI_HEADER `"$abiHeader`"`r`n#define GXOS_STUDIO_V05_APP_HEADER `"$appHeader`"`r`n"
[IO.File]::WriteAllText($configHeader, $configContents, [Text.UTF8Encoding]::new($false))
$compileArguments = @(
    '-std=c++14','-Wall','-Wextra','-pedantic','-O2',
    '-I',$repoRoot,'-I',(Join-Path $repoRoot 'kernel\core'),
    '-idirafter',(Join-Path $repoRoot 'kernel\core\include'),
    '-I',(Join-Path $repoRoot 'sdk\include'),'-I',$v05Include,
    '-include',$configHeader
) + @(
    (Join-Path $repoRoot 'tests\aarch64_studio_v05_abi_adapter_tests.cpp'),
    (Join-Path $repoRoot 'kernel\core\aarch64_studio_v05_abi_adapter.cpp'),
    (Join-Path $repoRoot 'kernel\core\aarch64_gui_render.cpp'),
    '-o',$exe
)
Write-Host '[studio-v05-adapter] compiling independent AARCH64+v0.5 hosted adapter proof...' -ForegroundColor Yellow
& $compiler @compileArguments
if ($LASTEXITCODE -ne 0) { throw 'Hosted Studio v0.5 adapter compile failed.' }
$testOutput = & $exe 2>&1
if ($LASTEXITCODE -ne 0) { throw "Hosted Studio v0.5 adapter tests failed: $($testOutput -join ' ')" }
$testText = $testOutput -join "`n"
if ($testText -notmatch 'aarch64StudioV05AdapterChecks=104/104') {
    throw "Hosted adapter test count is unexpected: $testText"
}
Write-Output $testOutput
Write-Host '[studio-v05-adapter] pinned source selection and 104 hosted checks: PASS' -ForegroundColor Green
