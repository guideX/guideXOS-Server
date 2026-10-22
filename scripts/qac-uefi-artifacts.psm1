Set-StrictMode -Version Latest

$script:AfterCopyChunkHook = $null

function Get-QacUefiFullPath {
    param([Parameter(Mandatory)][string]$Path)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($fullPath)
    if ($fullPath.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { return $root }
    return $fullPath.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Test-QacUefiPathWithin {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Root)
    $fullPath = Get-QacUefiFullPath $Path
    $fullRoot = Get-QacUefiFullPath $Root
    if ($fullPath.Equals($fullRoot, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    $prefix = $fullRoot + [IO.Path]::DirectorySeparatorChar
    return $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Get-QacUefiProperty {
    param([Parameter(Mandatory)]$InputObject, [Parameter(Mandatory)][string]$Name)
    if ($InputObject -is [System.Collections.IDictionary]) {
        if ($InputObject.Contains($Name)) { return $InputObject[$Name] }
        return $null
    }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -ne $property) { return $property.Value }
    return $null
}

function Assert-QacUefiNoReparsePath {
    param([Parameter(Mandatory)][string]$Path, [switch]$RequireExists)
    $fullPath = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($fullPath)
    if ([string]::IsNullOrWhiteSpace($root)) { throw "Path has no local filesystem root: $Path" }

    $current = $root
    $remaining = $fullPath.Substring($root.Length).Split([char[]]@('\', '/'), [StringSplitOptions]::RemoveEmptyEntries)
    foreach ($part in $remaining) {
        $current = Join-Path $current $part
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse point in protected path: $current"
            }
        } elseif ($RequireExists) {
            throw "Required path does not exist: $current"
        }
    }
}

function Assert-QacUefiTreeHasNoReparsePoints {
    param([Parameter(Mandatory)][string]$Path)
    $pending = New-Object 'System.Collections.Generic.Stack[string]'
    $pending.Push((Get-QacUefiFullPath $Path))
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in protected tree: $($item.FullName)" }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
}

function Get-QacUefiTreeInventory {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][ValidateSet('file','directory')][string]$Kind)

    $fullPath = Get-QacUefiFullPath $Path
    if (-not (Test-Path -LiteralPath $fullPath)) { throw "Source artifact does not exist: $fullPath" }
    Assert-QacUefiNoReparsePath -Path $fullPath -RequireExists
    $rootItem = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
    if ($Kind -eq 'file' -and $rootItem.PSIsContainer) { throw "Expected a file artifact: $fullPath" }
    if ($Kind -eq 'directory' -and -not $rootItem.PSIsContainer) { throw "Expected a directory artifact: $fullPath" }

    $files = New-Object 'System.Collections.Generic.List[object]'
    if ($Kind -eq 'file') {
        $files.Add([PSCustomObject]@{ RelativePath = [IO.Path]::GetFileName($fullPath); FullPath = $fullPath })
    } else {
        $pending = New-Object 'System.Collections.Generic.Stack[string]'
        $pending.Push($fullPath)
        while ($pending.Count -gt 0) {
            $directory = $pending.Pop()
            foreach ($child in @(Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop)) {
                if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw "Source artifact contains a reparse point: $($child.FullName)"
                }
                if ($child.PSIsContainer) {
                    $pending.Push($child.FullName)
                    continue
                }
                $relative = $child.FullName.Substring($fullPath.Length).TrimStart([char[]]@('\','/'))
                $files.Add([PSCustomObject]@{ RelativePath = $relative; FullPath = $child.FullName })
            }
        }
    }
    if ($files.Count -eq 0) { throw "Source artifact contains no files: $fullPath" }

    $fileArray = $files.ToArray()
    [Array]::Sort($fileArray, [System.Comparison[object]]{
        param($left, $right)
        [StringComparer]::OrdinalIgnoreCase.Compare([string]$left.RelativePath, [string]$right.RelativePath)
    })

    $records = New-Object 'System.Collections.Generic.List[object]'
    [int64]$totalBytes = 0
    foreach ($file in $fileArray) {
        $info = Get-Item -LiteralPath $file.FullPath -Force -ErrorAction Stop
        $hash = (Get-FileHash -LiteralPath $file.FullPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
        $records.Add([ordered]@{
            path = [string]$file.RelativePath.Replace('\','/')
            sizeBytes = [int64]$info.Length
            sha256 = $hash
        })
        $totalBytes += [int64]$info.Length
    }

    $recordArray = $records.ToArray()
    $identityText = ($recordArray | ForEach-Object { "{0}`t{1}`t{2}" -f $_.path, $_.sizeBytes, $_.sha256 }) -join "`n"
    $identityBytes = [Text.Encoding]::UTF8.GetBytes($identityText)
    if ($Kind -eq 'file') {
        $identitySha = [string]$recordArray[0].sha256
    } else {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $identitySha = [BitConverter]::ToString($sha.ComputeHash($identityBytes)).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
    }

    return [PSCustomObject]@{
        Path = $fullPath
        Kind = $Kind
        Files = @($recordArray)
        TotalBytes = $totalBytes
        Sha256 = $identitySha
    }
}

function Assert-QacUefiExpectedFormat {
    param([Parameter(Mandatory)]$Artifact, [Parameter(Mandatory)]$Inventory)

    $validation = Get-QacUefiProperty -InputObject $Artifact -Name 'Validation'
    if ($validation -eq 'uefi-esp-directory') {
        if ($Inventory.Kind -ne 'directory') { throw "UEFI ESP artifact '$($Artifact.Name)' must be a directory-backed FAT ESP." }
        $names = @($Inventory.Files | ForEach-Object { $_.path.ToLowerInvariant() })
        foreach ($required in @('efi/boot/bootx64.efi','kernel.elf','ramdisk.img')) {
            if ($names -notcontains $required) { throw "UEFI ESP '$($Artifact.Name)' is missing required boot input '$required'." }
        }
    } elseif ($validation -eq 'iso9660') {
        if ($Inventory.Kind -ne 'file' -or $Inventory.TotalBytes -lt 32774) { throw "ISO artifact '$($Artifact.Name)' is too small to contain an ISO-9660 volume descriptor." }
        $stream = [IO.File]::Open($Inventory.Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            [void]$stream.Seek(32769, [IO.SeekOrigin]::Begin)
            $signature = New-Object byte[] 5
            if ($stream.Read($signature, 0, $signature.Length) -ne 5 -or [Text.Encoding]::ASCII.GetString($signature) -ne 'CD001') {
                throw "ISO artifact '$($Artifact.Name)' has no ISO-9660 primary volume descriptor at sector 16."
            }
        } finally { $stream.Dispose() }
    } elseif ($validation -eq 'raw-firmware') {
        if ($Inventory.Kind -ne 'file' -or $Inventory.TotalBytes -le 0 -or ($Inventory.TotalBytes % 4096) -ne 0) {
            throw "Firmware artifact '$($Artifact.Name)' is not a non-empty, 4 KiB aligned raw image."
        }
    }
}

function Copy-QacUefiFileChecked {
    param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Destination)

    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { [void][IO.Directory]::CreateDirectory($parent) }
    Assert-QacUefiNoReparsePath -Path $parent -RequireExists
    $input = $null
    $output = $null
    try {
        $input = [IO.FileStream]::new($Source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $output = [IO.FileStream]::new($Destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $buffer = New-Object byte[] (1024 * 1024)
        [int64]$copied = 0
        $hookInvoked = $false
        while (($read = $input.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $output.Write($buffer, 0, $read)
            $copied += $read
            if (-not $hookInvoked -and $null -ne $script:AfterCopyChunkHook) {
                $hookInvoked = $true
                & $script:AfterCopyChunkHook $Source $Destination $copied
            }
        }
        $output.Flush($true)
    } finally {
        if ($null -ne $output) { $output.Dispose() }
        if ($null -ne $input) { $input.Dispose() }
    }
}

function Write-QacUefiJson {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)]$Value)
    $json = ConvertTo-Json -InputObject $Value -Depth 16
    [IO.File]::WriteAllText($Path, $json + "`n", (New-Object Text.UTF8Encoding($false)))
}

function New-QacUefiArtifactWorkspace {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$')][string]$Project,
        [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$')][string]$Workflow,
        [Parameter(Mandatory)][object[]]$Artifact,
        [string]$RunId = 'run',
        [string]$OutputRoot = $(if ($env:LOCALAPPDATA) { Join-Path $env:LOCALAPPDATA 'guideXOS\QAC\artifacts' } else { $null }),
        [hashtable]$LaunchContract,
        [ValidateRange(0, [long]::MaxValue)][long]$ReserveFreeBytes = 268435456
    )

    if ([string]::IsNullOrWhiteSpace($OutputRoot)) { throw 'OutputRoot is required when LOCALAPPDATA is unavailable.' }
    if ([string]::IsNullOrWhiteSpace($RunId) -or $RunId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$' -or $RunId -match '\.\.') {
        throw 'RunId must be a short label containing only letters, digits, dot, underscore, or hyphen; traversal is not allowed.'
    }
    if ($Artifact.Count -eq 0) { throw 'At least one source artifact is required.' }

    $root = Get-QacUefiFullPath $OutputRoot
    if ($root -notmatch '^[A-Za-z]:\\') { throw "OutputRoot must be on a local Windows volume: $root" }
    if ($root.Equals([IO.Path]::GetPathRoot($root), [StringComparison]::OrdinalIgnoreCase)) { throw 'OutputRoot must be a dedicated scratch directory, not a volume root.' }
    $sourceEntries = New-Object 'System.Collections.Generic.List[object]'
    $names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    [int64]$bytesToCopy = 0
    foreach ($entry in $Artifact) {
        if ($null -eq $entry.Name -or [string]$entry.Name -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') { throw 'Artifact names must be short safe file-name labels.' }
        if (-not $names.Add([string]$entry.Name)) { throw "Duplicate artifact name: $($entry.Name)" }
        if ($entry.Kind -notin @('file','directory')) { throw "Artifact '$($entry.Name)' Kind must be file or directory." }
        if ($entry.Access -notin @('read-only','writable')) { throw "Artifact '$($entry.Name)' Access must be read-only or writable." }
        if ($entry.Disposition -notin @('copy','share')) { throw "Artifact '$($entry.Name)' Disposition must be copy or share." }
        if ($entry.Access -eq 'writable' -and $entry.Disposition -ne 'copy') { throw "Writable artifact '$($entry.Name)' must be copied into the run workspace." }
        if ($entry.Disposition -eq 'share' -and $entry.Access -ne 'read-only') { throw "Only read-only artifacts may be shared." }

        $inventory = Get-QacUefiTreeInventory -Path ([string]$entry.Path) -Kind ([string]$entry.Kind)
        Assert-QacUefiExpectedFormat -Artifact $entry -Inventory $inventory
        $expectedSha256 = Get-QacUefiProperty -InputObject $entry -Name 'ExpectedSha256'
        if ($expectedSha256 -and $inventory.Sha256 -ne ([string]$expectedSha256).ToLowerInvariant()) {
            throw "Source identity mismatch for '$($entry.Name)': expected $expectedSha256, found $($inventory.Sha256)."
        }
        if ($entry.Disposition -eq 'copy') {
            if ($inventory.TotalBytes -gt ([long]::MaxValue - $bytesToCopy)) { throw 'Total staged artifact size exceeds the supported 64-bit byte count.' }
            $bytesToCopy += $inventory.TotalBytes
        }
        $sourceEntries.Add([PSCustomObject]@{ Definition = $entry; Inventory = $inventory })
    }

    foreach ($source in $sourceEntries) {
        if ((Test-QacUefiPathWithin -Path $root -Root $source.Inventory.Path) -or (Test-QacUefiPathWithin -Path $source.Inventory.Path -Root $root)) {
            throw "Scratch root and source artifact overlap; choose a separate local scratch path. Source: $($source.Inventory.Path)"
        }
    }

    if (-not (Test-Path -LiteralPath $root -PathType Container)) { [void][IO.Directory]::CreateDirectory($root) }
    Assert-QacUefiNoReparsePath -Path $root -RequireExists
    $driveRoot = [IO.Path]::GetPathRoot($root)
    $drive = [IO.DriveInfo]::new($driveRoot)
    if ($ReserveFreeBytes -gt $drive.AvailableFreeSpace -or $bytesToCopy -gt ($drive.AvailableFreeSpace - $ReserveFreeBytes)) {
        throw "Insufficient free space at scratch root '$root': need $bytesToCopy bytes plus $ReserveFreeBytes bytes reserve; available $($drive.AvailableFreeSpace) bytes."
    }

    $owner = if ($env:USERDOMAIN -and $env:USERNAME) { "$env:USERDOMAIN\$env:USERNAME" } else { [Environment]::UserName }
    $workspace = $null
    $nonce = $null
    $runIdentity = $null
    $ownerPath = $null
    $manifestPath = $null
    $completePath = $null
    for ($attempt = 0; $attempt -lt 5 -and $null -eq $workspace; $attempt++) {
        $nonceCandidate = [Guid]::NewGuid().ToString('N')
        $runCandidate = "$RunId-$nonceCandidate"
        $workspaceCandidate = Join-Path $root $runCandidate
        if ([IO.Directory]::Exists($workspaceCandidate)) { continue }
        [void][IO.Directory]::CreateDirectory($workspaceCandidate)
        $ownerCandidate = Join-Path $workspaceCandidate '.qac3c2-owner.json'
        $ownerRecordCandidate = [ordered]@{
            schemaVersion = 1
            runId = $runCandidate
            owner = $owner
            token = $nonceCandidate
            workspacePath = $workspaceCandidate
            outputRoot = $root
            createdAtUtc = [DateTime]::UtcNow.ToString('o')
        }
        $ownerStream = $null
        try {
            $ownerStream = [IO.FileStream]::new($ownerCandidate, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $ownerText = ConvertTo-Json -InputObject $ownerRecordCandidate -Depth 8
            $ownerBytes = (New-Object Text.UTF8Encoding($false)).GetBytes($ownerText + "`n")
            $ownerStream.Write($ownerBytes, 0, $ownerBytes.Length)
            $ownerStream.Flush($true)
            $workspace = $workspaceCandidate
            $nonce = $nonceCandidate
            $runIdentity = $runCandidate
            $ownerPath = $ownerCandidate
            $manifestPath = Join-Path $workspace 'manifest.json'
            $completePath = Join-Path $workspace '.complete'
        } catch [IO.IOException] {
            # A GUID collision or concurrent creator is never overwritten; try a fresh identity.
        } finally {
            if ($null -ne $ownerStream) { $ownerStream.Dispose() }
        }
    }
    if ($null -eq $workspace) { throw 'Could not claim a unique artifact workspace after five collision-safe attempts.' }
    Assert-QacUefiNoReparsePath -Path $workspace -RequireExists
    $outputDirectory = Join-Path $workspace 'output'
    $artifactDirectory = Join-Path $workspace 'artifacts'
    [void][IO.Directory]::CreateDirectory($outputDirectory)
    [void][IO.Directory]::CreateDirectory($artifactDirectory)

    $manifest = [ordered]@{
        schemaVersion = 1
        runId = $runIdentity
        requestedRunId = $RunId
        project = $Project
        selectedWorkflow = $Workflow
        createdAtUtc = [DateTime]::UtcNow.ToString('o')
        workspacePath = $workspace
        workspaceRoot = $root
        workspaceOwner = $owner
        ownerToken = $nonce
        completionStatus = 'staging'
        qacExecution = [ordered]@{ status = 'not-started'; reservationId = $null; childProcessId = $null; childExitVerified = $false }
        sourceConsistency = 'source hashes are checked before and after staging; this is not a transactional filesystem snapshot'
        artifacts = @()
        outputs = [ordered]@{
            directory = 'output'
            serialLog = 'output/serial.log'
            qemuDebugLog = 'output/qemu-debug.log'
            qmpTranscript = 'output/qmp-transcript.jsonl'
            screenshots = 'output/screenshots'
        }
        launchContract = $LaunchContract
    }
    Write-QacUefiJson -Path $manifestPath -Value $manifest

    try {
        $records = New-Object 'System.Collections.Generic.List[object]'
        foreach ($source in $sourceEntries) {
            $definition = $source.Definition
            $before = $source.Inventory
            $relativeTarget = Join-Path 'artifacts' ([string]$definition.Name)
            if ($definition.Disposition -eq 'copy') {
                $target = Join-Path $workspace $relativeTarget
                if ($before.Kind -eq 'directory') {
                    [void][IO.Directory]::CreateDirectory($target)
                    foreach ($file in $before.Files) {
                        $sourceFile = Join-Path $before.Path ($file.path.Replace('/','\'))
                        $targetFile = Join-Path $target ($file.path.Replace('/','\'))
                        Copy-QacUefiFileChecked -Source $sourceFile -Destination $targetFile
                    }
                } else {
                    Copy-QacUefiFileChecked -Source $before.Path -Destination $target
                }
                $staged = Get-QacUefiTreeInventory -Path $target -Kind $before.Kind
                $privatePath = $target
                $stagedHash = $staged.Sha256
                $stagedSize = $staged.TotalBytes
            } else {
                $privatePath = $before.Path
                $stagedHash = $before.Sha256
                $stagedSize = $before.TotalBytes
            }

            $after = Get-QacUefiTreeInventory -Path $before.Path -Kind $before.Kind
            if ($after.Sha256 -ne $before.Sha256 -or $after.TotalBytes -ne $before.TotalBytes) {
                throw "Source artifact changed while staging '$($definition.Name)'; expected $($before.Sha256), observed $($after.Sha256). The incomplete workspace is retained."
            }
            if ($stagedHash -ne $before.Sha256 -or $stagedSize -ne $before.TotalBytes) {
                throw "Staged artifact identity does not match source '$($definition.Name)'; the incomplete workspace is retained."
            }

            $records.Add([ordered]@{
                name = [string]$definition.Name
                role = [string]$definition.Role
                kind = [string]$definition.Kind
                access = [string]$definition.Access
                disposition = [string]$definition.Disposition
                sourcePath = $before.Path
                sourceSizeBytes = [int64]$before.TotalBytes
                sourceSha256 = $before.Sha256
                privatePath = $privatePath
                privateRelativePath = $(if ($definition.Disposition -eq 'copy') { $relativeTarget.Replace('\','/') } else { $null })
                privateSizeBytes = [int64]$stagedSize
                privateSha256 = $stagedHash
                files = @($before.Files)
            })
        }

        $manifest.artifacts = @($records.ToArray())
        $manifest.completionStatus = 'complete'
        Write-QacUefiJson -Path $manifestPath -Value $manifest
        [IO.File]::WriteAllText($completePath, "$runIdentity`n$nonce`n", (New-Object Text.UTF8Encoding($false)))
        return [PSCustomObject]@{
            RunId = $runIdentity
            WorkspacePath = $workspace
            ManifestPath = $manifestPath
            OutputDirectory = $outputDirectory
            Manifest = $manifest
        }
    } catch {
        $failureMessage = $_.Exception.Message
        try {
            $manifest.completionStatus = 'failed'
            $manifest.failure = $failureMessage
            Write-QacUefiJson -Path $manifestPath -Value $manifest
        } catch { }
        throw "Artifact staging failed. Incomplete workspace retained at '$workspace'. $failureMessage"
    }
}

function Test-QacUefiArtifactManifest {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$ManifestPath)

    $fullManifestPath = Get-QacUefiFullPath $ManifestPath
    Assert-QacUefiNoReparsePath -Path $fullManifestPath -RequireExists
    if ([IO.Path]::GetFileName($fullManifestPath) -ne 'manifest.json') { throw 'Manifest must be the workspace manifest.json file.' }
    try { $manifest = Get-Content -LiteralPath $fullManifestPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop } catch { throw "Malformed artifact manifest: $($_.Exception.Message)" }
    if ($manifest.schemaVersion -ne 1 -or $manifest.completionStatus -ne 'complete') { throw 'Manifest is not a complete supported artifact workspace.' }
    if ([string]::IsNullOrWhiteSpace([string]$manifest.runId) -or [string]::IsNullOrWhiteSpace([string]$manifest.ownerToken)) { throw 'Manifest is missing workspace ownership identity.' }

    $workspace = Get-QacUefiFullPath ([string]$manifest.workspacePath)
    $root = Get-QacUefiFullPath ([string]$manifest.workspaceRoot)
    if (-not (Test-QacUefiPathWithin -Path $workspace -Root $root) -or $workspace.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest workspace escapes its authorized scratch root.' }
    if (-not (Test-QacUefiPathWithin -Path $fullManifestPath -Root $workspace) -or (Split-Path -Parent $fullManifestPath) -ne $workspace) { throw 'Manifest path does not belong to its declared workspace.' }
    Assert-QacUefiNoReparsePath -Path $workspace -RequireExists
    $completePath = Join-Path $workspace '.complete'
    if (-not (Test-Path -LiteralPath $completePath -PathType Leaf)) { throw 'Workspace completion marker is missing.' }
    Assert-QacUefiNoReparsePath -Path $completePath -RequireExists
    $markerLines = @(Get-Content -LiteralPath $completePath -ErrorAction Stop)
    if ($markerLines.Count -lt 2 -or $markerLines[0] -ne [string]$manifest.runId -or $markerLines[1] -ne [string]$manifest.ownerToken) { throw 'Workspace completion marker does not match the manifest identity.' }

    $ownerPath = Join-Path $workspace '.qac3c2-owner.json'
    Assert-QacUefiNoReparsePath -Path $ownerPath -RequireExists
    try { $owner = Get-Content -LiteralPath $ownerPath -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop } catch { throw 'Workspace ownership marker is missing or malformed.' }
    if ($owner.runId -ne $manifest.runId -or $owner.token -ne $manifest.ownerToken -or $owner.workspacePath -ne $manifest.workspacePath -or $owner.outputRoot -ne $manifest.workspaceRoot) { throw 'Workspace ownership marker does not match the manifest.' }
    $currentOwner = if ($env:USERDOMAIN -and $env:USERNAME) { "$env:USERDOMAIN\$env:USERNAME" } else { [Environment]::UserName }
    if ([string]$owner.owner -cne $currentOwner) { throw 'Current Windows user does not own this artifact workspace.' }
    if (-not $manifest.qacExecution -or [string]$manifest.qacExecution.status -notin @('not-started','active','released','unknown')) { throw 'Manifest has a missing or invalid QAC execution ownership state.' }
    $outputDirectory = Get-QacUefiFullPath (Join-Path $workspace ([string]$manifest.outputs.directory))
    if (-not (Test-QacUefiPathWithin -Path $outputDirectory -Root $workspace) -or $outputDirectory.Equals($workspace, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest output directory escapes its workspace.' }
    Assert-QacUefiNoReparsePath -Path $outputDirectory -RequireExists
    if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) { throw 'Workspace output directory is missing.' }

    foreach ($outputName in @('serialLog','qemuDebugLog','qmpTranscript','screenshots')) {
        $relativeOutput = [string]$manifest.outputs.$outputName
        if ([string]::IsNullOrWhiteSpace($relativeOutput) -or [IO.Path]::IsPathRooted($relativeOutput) -or $relativeOutput -match '(^|[\\/])\.\.([\\/]|$)') { throw "Manifest output path '$outputName' is invalid." }
        $fullOutput = Get-QacUefiFullPath (Join-Path $workspace $relativeOutput.Replace('/','\'))
        if (-not (Test-QacUefiPathWithin -Path $fullOutput -Root $workspace) -or $fullOutput.Equals($workspace, [StringComparison]::OrdinalIgnoreCase)) { throw "Manifest output path '$outputName' escapes its workspace." }
        Assert-QacUefiNoReparsePath -Path $fullOutput
    }

    if (@($manifest.artifacts).Count -eq 0) { throw 'Manifest contains no artifacts.' }
    foreach ($artifact in @($manifest.artifacts)) {
        if ($artifact.disposition -eq 'copy') {
            $privatePath = Get-QacUefiFullPath ([string]$artifact.privatePath)
            if (-not (Test-QacUefiPathWithin -Path $privatePath -Root $workspace) -or $privatePath.Equals($workspace, [StringComparison]::OrdinalIgnoreCase)) { throw "Private artifact '$($artifact.name)' escapes the workspace." }
            $relativePath = [string]$artifact.privateRelativePath
            if ([string]::IsNullOrWhiteSpace($relativePath) -or [IO.Path]::IsPathRooted($relativePath) -or $relativePath -match '(^|[\\/])\.\.([\\/]|$)') { throw "Private artifact '$($artifact.name)' has an invalid relative path." }
            $expectedPath = Get-QacUefiFullPath (Join-Path $workspace $relativePath.Replace('/','\'))
            if (-not $expectedPath.Equals($privatePath, [StringComparison]::OrdinalIgnoreCase)) { throw "Private artifact '$($artifact.name)' path does not match its workspace-relative path." }
            $inventory = Get-QacUefiTreeInventory -Path $privatePath -Kind ([string]$artifact.kind)
            if ($inventory.Sha256 -ne [string]$artifact.privateSha256 -or $inventory.TotalBytes -ne [int64]$artifact.privateSizeBytes) { throw "Private artifact '$($artifact.name)' no longer matches its manifest identity." }
        } elseif ($artifact.disposition -eq 'share' -and $artifact.access -eq 'read-only') {
            $inventory = Get-QacUefiTreeInventory -Path ([string]$artifact.privatePath) -Kind ([string]$artifact.kind)
            if ($inventory.Sha256 -ne [string]$artifact.privateSha256 -or $inventory.TotalBytes -ne [int64]$artifact.privateSizeBytes) { throw "Shared read-only artifact '$($artifact.name)' changed since staging." }
        } else { throw "Manifest artifact '$($artifact.name)' has an unsafe disposition." }
    }
    return $manifest
}

function Remove-QacUefiArtifactWorkspace {
    [CmdletBinding(SupportsShouldProcess, ConfirmImpact='High')]
    param(
        [Parameter(Mandatory)][string]$ManifestPath,
        [Parameter(Mandatory)][string]$RunId
    )

    $manifest = Test-QacUefiArtifactManifest -ManifestPath $ManifestPath
    if ($RunId -cne [string]$manifest.runId) { throw 'RunId does not identify the supplied workspace; refusing cleanup.' }
    $state = [string]$manifest.qacExecution.status
    if ($state -eq 'active' -or $state -eq 'unknown' -or $state -notin @('not-started','released')) { throw "QAC workspace state '$state' is active or uncertain; refusing cleanup." }
    if ($state -eq 'released' -and -not [bool]$manifest.qacExecution.childExitVerified) { throw 'QAC release is not accompanied by verified child exit; refusing cleanup.' }

    $workspace = Get-QacUefiFullPath ([string]$manifest.workspacePath)
    $root = Get-QacUefiFullPath ([string]$manifest.workspaceRoot)
    if (-not (Test-QacUefiPathWithin -Path $workspace -Root $root) -or $workspace.Equals($root, [StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup target escapes the authorized scratch root.' }
    Assert-QacUefiNoReparsePath -Path $workspace -RequireExists
    Assert-QacUefiTreeHasNoReparsePoints -Path $workspace
    $ownerPath = Join-Path $workspace '.qac3c2-owner.json'
    if (-not (Test-Path -LiteralPath $ownerPath -PathType Leaf)) { throw 'Workspace owner marker is missing; refusing cleanup.' }
    if ($PSCmdlet.ShouldProcess($workspace, 'Remove verified QAC UEFI artifact workspace')) { [IO.Directory]::Delete($workspace, $true) }
}

Export-ModuleMember -Function New-QacUefiArtifactWorkspace, Test-QacUefiArtifactManifest, Remove-QacUefiArtifactWorkspace
