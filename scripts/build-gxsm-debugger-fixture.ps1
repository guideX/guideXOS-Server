[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourcePath,
    [Parameter(Mandatory=$true)][string]$SourceIdentity,
    [Parameter(Mandatory=$true)][string]$OutputPath
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler) { $compiler = Get-Command g++ -ErrorAction SilentlyContinue }
$compilerPath = if ($compiler) { $compiler.Source } else { "C:\mingw64\bin\g++.exe" }
if (-not $compiler -and -not (Test-Path -LiteralPath $compilerPath -PathType Leaf)) {
    throw "g++ is required to build the host adapter for the production bootstrap compiler"
}

$sourcePath = [IO.Path]::GetFullPath($SourcePath)
$outputPath = [IO.Path]::GetFullPath($OutputPath)
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "fixture source not found: $sourcePath" }
$generatorDirectory = Join-Path $repoRoot "out\validation"
New-Item -ItemType Directory -Force -Path $generatorDirectory | Out-Null
$generator = Join-Path $generatorDirectory "gxsm-debugger-fixture-generator.exe"
$sources = @(
    (Join-Path $repoRoot "tests/generate_gxsm_debugger_fixture.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_diagnostics.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_lexer.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_parser.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_module.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_object.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/compiler_linker.cpp"),
    (Join-Path $repoRoot "kernel/core/compiler/elf_writer.cpp"),
    (Join-Path $repoRoot "kernel/arch/amd64/compiler_backend.cpp"),
    (Join-Path $repoRoot "kernel/arch/amd64/arch.cpp")
)
$dependencies = @($sources) + @(
    Get-ChildItem -LiteralPath (Join-Path $repoRoot "kernel") -File -Recurse -Include *.h,*.hpp |
        ForEach-Object { $_.FullName }
)
$rebuildGenerator = -not (Test-Path -LiteralPath $generator -PathType Leaf)
if (-not $rebuildGenerator) {
    $generatorTime = (Get-Item -LiteralPath $generator).LastWriteTimeUtc
    foreach ($source in $dependencies) {
        if ((Get-Item -LiteralPath $source).LastWriteTimeUtc -gt $generatorTime) {
            $rebuildGenerator = $true
            break
        }
    }
}
if ($rebuildGenerator) {
    $arguments = @(
        "-std=c++14", "-Wall", "-Wextra", "-O2", "-DGXOS_BARE_METAL",
        "-I$repoRoot", "-I$(Join-Path $repoRoot 'kernel')",
        "-iquote$(Join-Path $repoRoot 'kernel/core/include')",
        "-I$(Join-Path $repoRoot 'kernel/arch/amd64/include')"
    ) + $sources + @("-o", $generator)
    & $compilerPath @arguments
    if ($LASTEXITCODE -ne 0) { throw "production bootstrap compiler fixture adapter build failed: $LASTEXITCODE" }
}

$outputDirectory = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
& $generator $sourcePath $SourceIdentity $outputPath
if ($LASTEXITCODE -ne 0) { throw "production bootstrap compiler fixture generation failed: $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) { throw "compiler did not create the requested ELF artifact" }
Write-Host "artifact_sha256=$((Get-FileHash -LiteralPath $outputPath -Algorithm SHA256).Hash)"
