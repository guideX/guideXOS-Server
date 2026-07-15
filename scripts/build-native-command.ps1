function Invoke-GxosNativeBuildCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [string[]]$ExpectedOutputPaths = @()
    )

    $savedErrorActionPreference = $ErrorActionPreference
    $exitCode = 1
    try {
        # Native compiler stderr remains on the console. Continue is deliberate:
        # warnings are diagnostics, while the process exit code is the result.
        $ErrorActionPreference = "Continue"
        & $FilePath @ArgumentList
        $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { [int]$LASTEXITCODE }
    } catch {
        Write-Host "Native build command invocation failed: $($_.Exception.Message)" -ForegroundColor Red
        $exitCode = 1
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }

    $missingOutputs = @()
    foreach ($path in $ExpectedOutputPaths) {
        if ([string]::IsNullOrWhiteSpace($path) -or
            -not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Item -LiteralPath $path).Length -le 0) {
            $missingOutputs += $path
        }
    }

    [pscustomobject]@{
        ExitCode = $exitCode
        ExpectedOutputsPresent = ($missingOutputs.Count -eq 0)
        MissingOutputs = $missingOutputs
        Succeeded = ($exitCode -eq 0 -and $missingOutputs.Count -eq 0)
    }
}
