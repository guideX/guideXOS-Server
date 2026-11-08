[CmdletBinding()]
param(
    [string]$OutputDirectory = 'E:\CodexArtifacts\S22R18\tests',
    [string]$Compiler = 'g++'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$compilerCommand = Get-Command $Compiler -ErrorAction Stop
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
}
$testSource = Join-Path $repoRoot 'tests\native_elf_security_policy_test.cpp'
$testBinary = Join-Path $OutputDirectory 'native-elf-security-policy-test.exe'
$compileArguments = @('-std=c++17','-Wall','-Wextra','-Werror',$testSource,'-o',$testBinary)
& $compilerCommand.Source @compileArguments
if ($LASTEXITCODE -ne 0) { throw "Security policy hosted test compilation failed with exit code $LASTEXITCODE." }
& $testBinary
if ($LASTEXITCODE -ne 0) { throw "Security policy hosted regressions failed with exit code $LASTEXITCODE." }
