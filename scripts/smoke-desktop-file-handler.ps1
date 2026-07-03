param(
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

if ($Build) {
    & (Join-Path $Root "build.bat")
    if ($LASTEXITCODE -ne 0) {
        throw "build.bat failed with exit code $LASTEXITCODE."
    }
}

$exe = Join-Path $Root "guideXOSServer.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "guideXOSServer.exe not found. Run .\build.bat first or pass -Build."
}

function Invoke-ServerCommands {
    param([string[]]$Commands)

    $inputText = (($Commands + @("exit")) -join [Environment]::NewLine) + [Environment]::NewLine
    return $inputText | & $exe 2>&1
}

function Assert-OutputContains {
    param(
        [string]$Output,
        [string]$Needle,
        [string]$Message
    )

    if (-not $Output.Contains($Needle)) {
        throw "$Message`nMissing: $Needle`n`nOutput:`n$Output"
    }
}

$cases = @(
    @{ Path = "/Desktop/smoke-lower.txt"; Target = "Notepad"; Status = "supported" },
    @{ Path = "/Desktop/smoke-upper.TXT"; Target = "Notepad"; Status = "supported" },
    @{ Path = "/Desktop/smoke-mixed.Txt"; Target = "Notepad"; Status = "supported" },
    @{ Path = "/Desktop/smoke-unsupported.md"; Target = "Unsupported"; Status = "unsupported" }
)

$commands = foreach ($case in $cases) {
    "desktop.open.resolve $($case.Path)"
}

$commands = @("pbytes") + @($commands)

$output = Invoke-ServerCommands -Commands $commands
Write-Host $output

foreach ($case in $cases) {
    Assert-OutputContains -Output $output -Needle "path: $($case.Path)" -Message "Missing resolver output for $($case.Path)"
    Assert-OutputContains -Output $output -Needle "launchTarget: $($case.Target)" -Message "Unexpected launch target for $($case.Path)"
    Assert-OutputContains -Output $output -Needle "status: $($case.Status)" -Message "Unexpected status for $($case.Path)"
}

Write-Host "Desktop file handler smoke PASS."
