$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ScriptRoot = Join-Path $Root "scripts"
Set-Location -LiteralPath $Root

$scripts = @(
    Get-ChildItem -LiteralPath $ScriptRoot -File -Filter `
        "smoke-navigator-javascript-*.ps1" |
        Where-Object {
            $_.Name -match '^smoke-navigator-javascript-(lexer|parser|runtime|js\d+r?\d*)\.ps1$'
        } | ForEach-Object {
            $name = $_.BaseName -replace '^smoke-navigator-javascript-', ''
            if ($name -match '^js(\d+)(?:r(\d*))?$') {
                $revision = 0
                if ($Matches[2] -ne $null) {
                    $revision = if ($Matches[2] -eq "") { 1 } else {
                        [int]$Matches[2]
                    }
                }
                [pscustomobject]@{
                    Path = $_.FullName
                    Major = [int]$Matches[1]
                    Revision = $revision
                    Name = $_.Name
                }
            } else {
                [pscustomobject]@{
                    Path = $_.FullName
                    Major = -1
                    Revision = switch ($name) {
                        "lexer" { 0 }
                        "parser" { 1 }
                        "runtime" { 2 }
                    }
                    Name = $_.Name
                }
            }
        } | Sort-Object Major, Revision, Name
)

if ($scripts.Count -eq 0) {
    throw "No Navigator JavaScript smoke lanes were found."
}

$passedLanes = 0
$reportedAssertionsPassed = 0
$reportedAssertionsTotal = 0
$assertionLanes = 0
foreach ($lane in $scripts) {
    Write-Output "=== $($lane.Name) ==="
    try {
        $output = & $lane.Path 2>&1
        $exitCode = $LASTEXITCODE
        $output | ForEach-Object { Write-Output $_ }
        if ($exitCode -ne 0) {
            throw "Native smoke process exited with code $exitCode."
        }
        foreach ($line in $output) {
            if ([string]$line -match 'Navigator JavaScript .*checks:\s*(\d+)/(\d+) passed') {
                $reportedAssertionsPassed += [int]$Matches[1]
                $reportedAssertionsTotal += [int]$Matches[2]
                $assertionLanes++
            }
        }
        $passedLanes++
    } catch {
        Write-Error "FAILED: $($lane.Name): $_"
        throw
    }
}

Write-Output "Navigator JavaScript matrix: $passedLanes/$($scripts.Count) lanes passed."
if ($assertionLanes -gt 0) {
    Write-Output "Reported focused assertions: $reportedAssertionsPassed/$reportedAssertionsTotal across $assertionLanes lanes."
}
