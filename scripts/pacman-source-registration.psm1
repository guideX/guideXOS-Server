function Get-PacManSourceRegistration {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot
    )

    $sourceDirectory = Join-Path $SourceRoot "src"
    $sources = @(
        @{ Name = "main"; Path = Join-Path $sourceDirectory "main.cpp" },
        @{ Name = "game"; Path = Join-Path $sourceDirectory "game.cpp" },
        @{ Name = "level"; Path = Join-Path $sourceDirectory "level.cpp" },
        @{ Name = "level_rules"; Path = Join-Path $sourceDirectory "level_rules.cpp" },
        @{ Name = "renderer"; Path = Join-Path $sourceDirectory "renderer.cpp" },
        @{ Name = "bitmap_loader"; Path = Join-Path $sourceDirectory "bitmap_loader.cpp" }
    )

    foreach ($source in $sources) {
        if (!(Test-Path -LiteralPath $source.Path -PathType Leaf)) {
            throw "Missing required PacMan source file $($source.Name): $($source.Path)"
        }
    }

    $audioSourcePath = Join-Path $sourceDirectory "pacman_audio.cpp"
    $audioReferencePattern = '\bpacman_audio_(load_resources|submit)\s*\('
    $audioReferences = @()
    foreach ($file in (Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File | Where-Object {
        ($_.Extension -eq ".cpp" -or $_.Extension -eq ".h") -and
        -not $_.FullName.Equals($audioSourcePath, [System.StringComparison]::OrdinalIgnoreCase)
    })) {
        $audioReferences += @(Select-String -LiteralPath $file.FullName -Pattern $audioReferencePattern)
    }

    $audioRequired = $audioReferences.Count -gt 0
    if ($audioRequired) {
        if (!(Test-Path -LiteralPath $audioSourcePath -PathType Leaf)) {
            throw "PacMan source references audio functions but has no implementation: $audioSourcePath"
        }
        $audioImplementation = Get-Content -LiteralPath $audioSourcePath -Raw
        if ($audioImplementation -notmatch '(?m)^\s*uint32_t\s+pacman_audio_load_resources\s*\([^{}\r\n]*\)\s*\{') {
            throw "PacMan audio implementation does not define pacman_audio_load_resources with the expected C++ signature: $audioSourcePath"
        }
        if ($audioImplementation -notmatch '(?m)^\s*PacManAudioSubmitResult\s+pacman_audio_submit\s*\([^{}\r\n]*\)\s*\{') {
            throw "PacMan audio implementation does not define pacman_audio_submit with the expected C++ signature: $audioSourcePath"
        }
        $sources += @{ Name = "pacman_audio"; Path = $audioSourcePath }
    }

    $duplicateNames = @($sources | Group-Object -Property { $_.Name } | Where-Object { $_.Count -gt 1 })
    if ($duplicateNames.Count -gt 0) {
        throw "PacMan source registration contains duplicate object names: $(($duplicateNames | ForEach-Object { $_.Name }) -join ', ')"
    }
    $resolvedPaths = @($sources | ForEach-Object { [System.IO.Path]::GetFullPath($_.Path) })
    if (@($resolvedPaths | Select-Object -Unique).Count -ne $resolvedPaths.Count) {
        throw "PacMan source registration contains duplicate source paths."
    }

    return [pscustomobject]@{
        Sources = @($sources)
        AudioRequired = $audioRequired
    }
}

Export-ModuleMember -Function Get-PacManSourceRegistration
