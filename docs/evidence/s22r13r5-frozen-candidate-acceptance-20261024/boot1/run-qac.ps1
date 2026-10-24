$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$config = Get-Content -Raw -LiteralPath (Join-Path $root 'qemu-launch.json') | ConvertFrom-Json
$log = Join-Path $root 'qac-run.log'
"QAC runner start UTC: $([DateTime]::UtcNow.ToString('o'))" | Set-Content -LiteralPath $log -Encoding utf8
& $config.qacCliPath run --project guideXOSServer --mode acceptance -- $config.qemuPath @($config.arguments) 2>&1 |
    Tee-Object -FilePath $log -Append
$exitCode = $LASTEXITCODE
"QAC CLI exit code: $exitCode; completed UTC: $([DateTime]::UtcNow.ToString('o'))" | Add-Content -LiteralPath $log -Encoding utf8
$exitCode | Set-Content -LiteralPath (Join-Path $root 'qac-exit-code.txt') -Encoding ascii
exit $exitCode
