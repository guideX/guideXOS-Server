param(
    [ValidateSet('RX20','RX21')][string]$Phase = 'RX20'
)

$ErrorActionPreference = 'Stop'
$rootDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ((git -C $rootDir branch --show-current).Trim() -ne 'REXX_SUPPORT') {
    throw "$Phase clock contract qualification requires branch REXX_SUPPORT."
}

$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if ($null -eq $compiler) { throw 'g++ is required for the RX20 clock contract host adapter.' }

$guardModule = Join-Path $PSScriptRoot 'rexx_artifact_guard.psm1'
Import-Module -Name $guardModule -Force
$manifest = Join-Path $rootDir "docs\rexx\evidence\$($Phase.ToLowerInvariant())\protected-artifacts-start.json"
$runId = [Guid]::NewGuid().ToString('N')
$runRoot = Join-Path $rootDir "build\rexx\$($Phase.ToLowerInvariant())\direct-$runId"
$workingDirectory = Join-Path $runRoot 'temp'
$exePath = Join-Path $runRoot 'kernel_clock_contract_test.exe'
$buildStdout = Join-Path $runRoot 'build.stdout.log'
$buildStderr = Join-Path $runRoot 'build.stderr.log'
$runStdout = Join-Path $runRoot 'run.stdout.log'
$runStderr = Join-Path $runRoot 'run.stderr.log'
$plannedOutputs = @($exePath,$buildStdout,$buildStderr,$runStdout,$runStderr)

$null = Start-ReXXArtifactRun -ManifestPath $manifest -RunRoot $runRoot `
    -WorkingDirectory $workingDirectory -PlannedOutputs $plannedOutputs -Phase $Phase
$env:TEMP = $workingDirectory
$env:TMP = $workingDirectory
$env:TMPDIR = $workingDirectory

$compilerArgs = @(
    '-std=c++14','-Wall','-Wextra','-Wpedantic','-Werror','-pthread',
    (Join-Path $rootDir 'tests\kernel_clock_contract_test.cpp'),
    (Join-Path $rootDir 'kernel\core\clock_contract.cpp'),
    '-o',$exePath
)
& $compiler.Source @compilerArgs 1> $buildStdout 2> $buildStderr
$buildExit = $LASTEXITCODE
if ($buildExit -ne 0) { throw "RX20 clock contract test build failed ($buildExit). See $runRoot." }

& $exePath 1> $runStdout 2> $runStderr
$runExit = $LASTEXITCODE
if ($runExit -ne 0) { throw "RX20 clock contract tests failed ($runExit). See $runRoot." }

$hash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash.ToLowerInvariant()
$result = [PSCustomObject]@{
    schema = "guidexos.rexx.$($Phase.ToLowerInvariant()).clock-contract-test.v1"
    run_id = $runId
    branch = (git -C $rootDir branch --show-current).Trim()
    head = (git -C $rootDir rev-parse HEAD).Trim()
    compiler = $compiler.Source
    compiler_version = (& $compiler.Source --version | Select-Object -First 1).ToString()
    command = @($compilerArgs)
    build_exit_code = $buildExit
    run_exit_code = $runExit
    test_output = (Get-Content -Raw -LiteralPath $runStdout).Trim()
    executable_sha256 = $hash
    executable = $exePath
    status = 'PASS'
}

Test-ReXXArtifactRun -ManifestPath $manifest -RunRoot $runRoot `
    -Stage "$($Phase.ToLowerInvariant())-clock-contract-test" -Phase $Phase | Out-Null
$durable = Join-Path $rootDir "docs\rexx\evidence\$($Phase.ToLowerInvariant())\direct-clock-tests\run-$runId"
New-Item -ItemType Directory -Path $durable -Force | Out-Null
Copy-Item -LiteralPath $runStdout,$runStderr,$buildStdout,$buildStderr -Destination $durable
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $durable 'result.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $runRoot 'preservation') -Destination (Join-Path $durable 'preservation') -Recurse

Write-Output $result.test_output
Write-Output "RX20 direct test evidence: $durable"
