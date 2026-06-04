# Normalize inherited Windows process environments that contain both `Path`
# and `PATH`, which causes Start-Process/MSBuild dictionary collisions in
# PowerShell's case-insensitive environment handling.
function Get-NormalizedPathValue {
    param(
        [System.Collections.IDictionary]$EnvironmentTable
    )

    if ($null -eq $EnvironmentTable) {
        return $null
    }

    $pathKeys = @()
    foreach ($key in $EnvironmentTable.Keys) {
        $keyText = [string]$key
        if ($keyText.Equals("Path", [System.StringComparison]::OrdinalIgnoreCase)) {
            $pathKeys += $keyText
        }
    }

    if ($pathKeys.Count -eq 0) {
        return $null
    }

    $orderedKeys = New-Object System.Collections.Generic.List[string]
    if ($pathKeys -contains "Path") {
        [void]$orderedKeys.Add("Path")
    }
    foreach ($key in $pathKeys) {
        if ($key -ne "Path" -and -not $orderedKeys.Contains($key)) {
            [void]$orderedKeys.Add($key)
        }
    }

    $segments = New-Object System.Collections.Generic.List[string]
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($key in $orderedKeys) {
        $value = [System.Environment]::GetEnvironmentVariable($key, "Process")
        if ([string]::IsNullOrWhiteSpace($value)) {
            continue
        }
        foreach ($segment in ($value -split ';')) {
            $trimmed = $segment.Trim()
            if ($trimmed.Length -eq 0) {
                continue
            }
            if ($seen.Add($trimmed)) {
                [void]$segments.Add($trimmed)
            }
        }
    }

    return [string]::Join(';', $segments)
}

function Normalize-ProcessEnvironment {
    $processEnvironment = [System.Environment]::GetEnvironmentVariables("Process")
    if ($null -eq $processEnvironment) {
        return
    }

    $normalizedPath = Get-NormalizedPathValue -EnvironmentTable $processEnvironment
    if ($null -eq $normalizedPath) {
        return
    }

    foreach ($key in @($processEnvironment.Keys)) {
        $keyText = [string]$key
        if ($keyText.Equals("Path", [System.StringComparison]::OrdinalIgnoreCase) -and
            -not $keyText.Equals("Path", [System.StringComparison]::Ordinal)) {
            [System.Environment]::SetEnvironmentVariable($keyText, $null, "Process")
        }
    }

    [System.Environment]::SetEnvironmentVariable("Path", $normalizedPath, "Process")
}
