Set-StrictMode -Version Latest

$script:ApprovedFontPackageMaxBytesPerFont = 192KB
$script:ApprovedFontPackageMaxFonts = 3
$script:ApprovedFontPackageMaxTotalBytes = 576KB
$script:ApprovedFontPackageExpectedFonts = @(
    'Roboto-v2.138-Regular.ttf',
    'Roboto-v2.138-Bold.ttf',
    'Roboto-v2.138-Italic.ttf'
)
$script:ApprovedFontPackageExpectedNotices = @(
    'LICENSE.txt',
    'NOTICE.txt'
)
$script:ApprovedFontPackageMaxDirectoryEntries = 6

function Get-BoundedApprovedFontEntries {
    param([Parameter(Mandatory = $true)][string]$Path)

    $entries = [System.Collections.Generic.List[object]]::new()
    $enumerator = [System.IO.Directory]::EnumerateFileSystemEntries($Path).GetEnumerator()
    try {
        while ($enumerator.MoveNext()) {
            if ($entries.Count -ge $script:ApprovedFontPackageMaxDirectoryEntries) {
                throw "Font package directory exceeds the fixed $($script:ApprovedFontPackageMaxDirectoryEntries)-entry bound: $Path"
            }
            $entries.Add((Get-Item -LiteralPath $enumerator.Current -Force))
        }
    } finally {
        if ($enumerator -is [System.IDisposable]) { $enumerator.Dispose() }
    }
    return $entries.ToArray()
}

function Test-ApprovedFontSafeName {
    param([Parameter(Mandatory = $true)][string]$Name)

    if ([string]::IsNullOrWhiteSpace($Name) -or
        $Name.Length -gt 127 -or
        [IO.Path]::GetFileName($Name) -cne $Name -or
        $Name.Contains('/') -or $Name.Contains('\') -or
        $Name.Contains(':') -or $Name.Contains('..')) {
        return $false
    }
    return $true
}

function Get-ApprovedFontPackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$PackageRoot,
        [switch]$AllowMissingFonts
    )

    $root = [IO.Path]::GetFullPath($PackageRoot)
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw "Approved font package directory is missing: $root"
    }
    $rootItem = Get-Item -LiteralPath $root -Force
    if ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Approved font package directory is a reparse point: $root"
    }

    $manifestPath = Join-Path $root 'ApprovedFontManifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Approved font manifest is missing: $manifestPath"
    }
    $manifestItem = Get-Item -LiteralPath $manifestPath -Force
    if ($manifestItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Approved font manifest is a reparse point: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([int]$manifest.schemaVersion -ne 1 -or
        [string]$manifest.packageId -cne 'googlefonts-roboto-v2.138-subset' -or
        [string]$manifest.runtimeDirectory -cne '/system/fonts' -or
        [string]$manifest.upstream.release -cne 'v2.138' -or
        [string]$manifest.upstream.archiveName -cne 'roboto-unhinted.zip' -or
        [string]$manifest.upstream.licenseName -cne 'Apache License 2.0') {
        throw 'Approved font manifest identity, release, runtime path, or license is not recognized.'
    }

    $declaredFonts = @($manifest.fonts)
    if ($declaredFonts.Count -eq 0 -or $declaredFonts.Count -gt $script:ApprovedFontPackageMaxFonts) {
        throw "Approved font manifest exceeds the registry capacity of $($script:ApprovedFontPackageMaxFonts) faces."
    }
    if ($declaredFonts.Count -ne $script:ApprovedFontPackageExpectedFonts.Count) {
        throw 'Approved font manifest does not declare the fixed v2.138 face set.'
    }

    $allowed = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($name in @('ApprovedFontManifest.json') + $script:ApprovedFontPackageExpectedNotices + $script:ApprovedFontPackageExpectedFonts) {
        [void]$allowed.Add($name)
    }
    $entries = @(Get-BoundedApprovedFontEntries -Path $root)
    foreach ($entry in $entries) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Approved font package contains a reparse point: $($entry.Name)"
        }
        if ($entry.PSIsContainer) {
            throw "Approved font package contains an unexpected directory: $($entry.Name)"
        }
        if (-not $allowed.Contains($entry.Name)) {
            throw "Approved font package contains an unlisted file: $($entry.Name)"
        }
    }

    $manifestNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $identityNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $declaredTotal = [int64]0
    foreach ($font in $declaredFonts) {
        $name = [string]$font.fileName
        if (-not (Test-ApprovedFontSafeName $name) -or
            -not $script:ApprovedFontPackageExpectedFonts.Contains($name)) {
            throw "Approved font manifest contains an invalid or unapproved filename: $name"
        }
        if (-not $manifestNames.Add($name)) {
            throw "Approved font manifest contains a duplicate filename: $name"
        }
        if ([string]::IsNullOrWhiteSpace([string]$font.family) -or
            [string]::IsNullOrWhiteSpace([string]$font.style)) {
            throw "Approved font manifest is missing family/style metadata for $name."
        }
        if (-not $identityNames.Add(([string]$font.family + '|' + [string]$font.style))) {
            throw "Approved font manifest contains duplicate family/style identities: $($font.family) $($font.style)"
        }
        if ([int64]$font.bytes -le 0 -or [int64]$font.bytes -gt $script:ApprovedFontPackageMaxBytesPerFont) {
            throw "Approved font exceeds the per-file size limit: $name ($($font.bytes) bytes)."
        }
        if ([string]$font.sha256 -notmatch '^[0-9a-f]{64}$') {
            throw "Approved font manifest has an invalid SHA-256 value for $name."
        }
        if ([string]$font.outline -cne 'glyf' -or
            (@($font.cmapFormats).Count -ne 1) -or
            [int]@($font.cmapFormats)[0] -ne 4 -or
            [int]$font.unicodeMappings -ne 922 -or
            [bool]$font.modified -ne $true) {
            throw "Approved font format or subset metadata is not recognized for $name."
        }
        $declaredTotal += [int64]$font.bytes
    }
    if ($manifestNames.Count -ne $script:ApprovedFontPackageExpectedFonts.Count) {
        throw 'Approved font manifest does not contain every approved face exactly once.'
    }
    if ($declaredTotal -gt $script:ApprovedFontPackageMaxTotalBytes -or
        [int64]$manifest.limits.packagedFontBytes -ne $declaredTotal -or
        [int64]$manifest.limits.maxBytesPerFont -ne $script:ApprovedFontPackageMaxBytesPerFont -or
        [int]$manifest.limits.maxFonts -ne $script:ApprovedFontPackageMaxFonts -or
        [int64]$manifest.limits.maxTotalFontBytes -ne $script:ApprovedFontPackageMaxTotalBytes) {
        throw 'Approved font package byte totals or configured limits do not match.'
    }

    $noticeRecords = @($manifest.notices)
    if ($noticeRecords.Count -ne $script:ApprovedFontPackageExpectedNotices.Count) {
        throw 'Approved font manifest must declare exactly the complete license and attribution notice.'
    }
    foreach ($expectedNotice in $script:ApprovedFontPackageExpectedNotices) {
        $records = @($noticeRecords | Where-Object { [string]$_.fileName -ceq $expectedNotice })
        if ($records.Count -ne 1) {
            throw "Approved font manifest is missing or duplicates notice record: $expectedNotice"
        }
        $record = $records[0]
        if (-not (Test-ApprovedFontSafeName ([string]$record.fileName)) -or
            [int64]$record.bytes -le 0 -or
            [string]$record.sha256 -notmatch '^[0-9a-f]{64}$') {
            throw "Approved font license/notice record is invalid: $expectedNotice"
        }
        $noticePath = Join-Path $root $expectedNotice
        if (-not (Test-Path -LiteralPath $noticePath -PathType Leaf)) {
            throw "Required font license/notice file is missing: $noticePath"
        }
        $noticeInfo = Get-Item -LiteralPath $noticePath -Force
        if (($noticeInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            [int64]$noticeInfo.Length -ne [int64]$record.bytes -or
            (Get-FileHash -LiteralPath $noticePath -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$record.sha256) {
            throw "Font license/notice size or SHA-256 mismatch: $expectedNotice"
        }
    }
    if ([string]$manifest.upstream.licenseFile -cne 'LICENSE.txt' -or
        [string]$manifest.upstream.licenseSha256 -cne [string](@($noticeRecords | Where-Object { $_.fileName -ceq 'LICENSE.txt' })[0].sha256)) {
        throw 'Font package manifest does not bind the complete upstream license file.'
    }

    $fontFiles = @()
    $presentTotal = [int64]0
    foreach ($font in $declaredFonts) {
        $name = [string]$font.fileName
        $path = Join-Path $root $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            if ($AllowMissingFonts) { continue }
            throw "Approved optional font file is missing: $path"
        }
        $fileInfo = Get-Item -LiteralPath $path -Force
        if ($fileInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Approved font file is a reparse point: $path"
        }
        if ([int64]$fileInfo.Length -gt $script:ApprovedFontPackageMaxBytesPerFont) {
            throw "Approved font candidate exceeds the per-file limit: $name ($($fileInfo.Length) bytes)."
        }
        if ([int64]$fileInfo.Length -ne [int64]$font.bytes) {
            throw "Approved font size mismatch: $name expected $($font.bytes), found $($fileInfo.Length)."
        }
        $stream = [IO.File]::OpenRead($path)
        try {
            $magic = New-Object byte[] 4
            if ($stream.Read($magic, 0, 4) -ne 4 -or
                $magic[0] -ne 0x00 -or $magic[1] -ne 0x01 -or
                $magic[2] -ne 0x00 -or $magic[3] -ne 0x00) {
                throw "Unsupported or non-standalone TrueType signature: $name"
            }
        } finally {
            $stream.Dispose()
        }
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -cne [string]$font.sha256) {
            throw "Approved font SHA-256 mismatch: $name"
        }
        $fontFiles += [pscustomobject]@{
            FileName = $name
            Path = $path
            Bytes = [int64]$fileInfo.Length
            Sha256 = $hash
            Family = [string]$font.family
            Style = [string]$font.style
        }
        $presentTotal += [int64]$fileInfo.Length
    }
    if ($presentTotal -gt $script:ApprovedFontPackageMaxTotalBytes) {
        throw 'Present approved font files exceed the total source-storage limit.'
    }

    return [pscustomobject]@{
        Root = $root
        Manifest = $manifest
        ManifestPath = $manifestPath
        FontFiles = @($fontFiles)
        FontBytes = $presentTotal
        DeclaredFontCount = $declaredFonts.Count
    }
}

function Copy-ApprovedFontPackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$PackageRoot,
        [Parameter(Mandatory = $true)][string]$Destination,
        [switch]$AllowMissingFonts
    )

    $package = Get-ApprovedFontPackage -PackageRoot $PackageRoot -AllowMissingFonts:$AllowMissingFonts
    $destinationRoot = [IO.Path]::GetFullPath($Destination)
    if (Test-Path -LiteralPath $destinationRoot) {
        $destinationItem = Get-Item -LiteralPath $destinationRoot -Force
        if (-not $destinationItem.PSIsContainer -or
            ($destinationItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Font staging destination must be a normal directory: $destinationRoot"
        }
        if (@(Get-BoundedApprovedFontEntries -Path $destinationRoot).Count -ne 0) {
            throw "Font staging destination is not empty: $destinationRoot"
        }
    } else {
        New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
    }

    foreach ($font in $package.FontFiles) {
        Copy-Item -LiteralPath $font.Path -Destination (Join-Path $destinationRoot $font.FileName) | Out-Null
    }
    foreach ($name in $script:ApprovedFontPackageExpectedNotices + @('ApprovedFontManifest.json')) {
        Copy-Item -LiteralPath (Join-Path $package.Root $name) -Destination (Join-Path $destinationRoot $name) | Out-Null
    }

    $copied = @(Get-BoundedApprovedFontEntries -Path $destinationRoot)
    $expectedNames = @($package.FontFiles | ForEach-Object { $_.FileName }) +
        $script:ApprovedFontPackageExpectedNotices + @('ApprovedFontManifest.json')
    if ($copied.Count -ne $expectedNames.Count) {
        throw 'Staged font package contains an unexpected file count.'
    }
    foreach ($item in $copied) {
        if ($item.PSIsContainer -or -not $expectedNames.Contains($item.Name)) {
            throw "Staged font package contains an unexpected destination entry: $($item.Name)"
        }
    }
    foreach ($font in $package.FontFiles) {
        $target = Join-Path $destinationRoot $font.FileName
        if ([int64](Get-Item -LiteralPath $target).Length -ne $font.Bytes -or
            (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -cne $font.Sha256) {
            throw "Staged font bytes differ from the approved source: $($font.FileName)"
        }
    }
    return $package
}

Export-ModuleMember -Function Get-ApprovedFontPackage, Copy-ApprovedFontPackage
