function Save-NavigatorSmokeDirectoryState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LiteralPath
    )

    $fullPath = [System.IO.Path]::GetFullPath($LiteralPath)
    $snapshotRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("guidexos-smoke-" + [Guid]::NewGuid().ToString("N"))
    $snapshotPath = Join-Path $snapshotRoot "saved"
    $exists = Test-Path -LiteralPath $fullPath

    New-Item -ItemType Directory -Force -Path $snapshotRoot | Out-Null
    if ($exists) {
        New-Item -ItemType Directory -Force -Path $snapshotPath | Out-Null
        Get-ChildItem -LiteralPath $fullPath -Force -ErrorAction SilentlyContinue | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination $snapshotPath -Recurse -Force
        }
    }

    return [PSCustomObject]@{
        Path = $fullPath
        Exists = $exists
        SnapshotRoot = $snapshotRoot
        SnapshotPath = $snapshotPath
    }
}

function Restore-NavigatorSmokeDirectoryState {
    param(
        [Parameter(Mandatory = $true)]
        $State
    )

    if (Test-Path -LiteralPath $State.Path) {
        Get-ChildItem -LiteralPath $State.Path -Force -ErrorAction SilentlyContinue | ForEach-Object {
            Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    if ($State.Exists) {
        New-Item -ItemType Directory -Force -Path $State.Path | Out-Null
        if (Test-Path -LiteralPath $State.SnapshotPath) {
            Get-ChildItem -LiteralPath $State.SnapshotPath -Force -ErrorAction SilentlyContinue | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $State.Path -Recurse -Force
            }
        }
    } elseif (Test-Path -LiteralPath $State.Path) {
        Remove-Item -LiteralPath $State.Path -Recurse -Force -ErrorAction SilentlyContinue
    }

    if (Test-Path -LiteralPath $State.SnapshotRoot) {
        Remove-Item -LiteralPath $State.SnapshotRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Save-NavigatorSmokeFileState {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LiteralPath
    )

    $fullPath = [System.IO.Path]::GetFullPath($LiteralPath)
    $snapshotRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("guidexos-smoke-file-" + [Guid]::NewGuid().ToString("N"))
    $snapshotPath = Join-Path $snapshotRoot "saved.bin"
    $exists = Test-Path -LiteralPath $fullPath

    New-Item -ItemType Directory -Force -Path $snapshotRoot | Out-Null
    if ($exists) {
        Copy-Item -LiteralPath $fullPath -Destination $snapshotPath -Force
    }

    return [PSCustomObject]@{
        Path = $fullPath
        Exists = $exists
        SnapshotRoot = $snapshotRoot
        SnapshotPath = $snapshotPath
    }
}

function Copy-NavigatorSmokeFileWithRetry {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,
        [Parameter(Mandatory = $true)]
        [string]$Destination,
        [int]$TimeoutMilliseconds = 10000
    )

    $deadline = (Get-Date).AddMilliseconds($TimeoutMilliseconds)
    do {
        try {
            Copy-Item -LiteralPath $Source -Destination $Destination -Force
            return
        } catch [System.IO.IOException] {
            if ((Get-Date) -ge $deadline) {
                throw
            }
            Start-Sleep -Milliseconds 200
        }
    } while ($true)
}

function Restore-NavigatorSmokeFileState {
    param(
        [Parameter(Mandatory = $true)]
        $State
    )

    if ($State.Exists) {
        $parent = Split-Path -Parent $State.Path
        if ($parent -and -not (Test-Path -LiteralPath $parent)) {
            New-Item -ItemType Directory -Force -Path $parent | Out-Null
        }
        if (Test-Path -LiteralPath $State.SnapshotPath) {
            Copy-NavigatorSmokeFileWithRetry -Source $State.SnapshotPath -Destination $State.Path
        }
    } elseif (Test-Path -LiteralPath $State.Path) {
        Remove-Item -LiteralPath $State.Path -Force -ErrorAction SilentlyContinue
    }

    if (Test-Path -LiteralPath $State.SnapshotRoot) {
        Remove-Item -LiteralPath $State.SnapshotRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
