param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [ValidateSet('amd64')]
    [string]$Arch = 'amd64',

    [switch]$Clean,
    [switch]$SkipBuild,
    [switch]$RequireCleanWorktree,
    [switch]$Force,
    [switch]$BootstrapTools,
    [ValidateSet('PyCdlib', 'Oscdimg')]
    [string]$IsoBackend = 'PyCdlib',
    [string]$OscdimgPath,
    [string]$PythonPath,
    [ValidateRange(0, 8)]
    [int]$I219Phase5Stage = 8,
    [ValidateRange(0, 6)]
    [int]$I219Phase6Stage = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $ScriptRoot '..')).Path
$DistRoot = Join-Path $RepositoryRoot 'dist'
$ReleaseToolsRoot = Join-Path $RepositoryRoot 'out\release-tools'
$ReleaseWorkRoot = Join-Path $RepositoryRoot 'out\release-iso'
$BuildScript = Join-Path $RepositoryRoot 'build.ps1'
$EspRoot = Join-Path $RepositoryRoot 'ESP'
$EspHelper = Join-Path $RepositoryRoot 'tools\release_esp.py'
$IsoHelper = Join-Path $RepositoryRoot 'tools\release_iso.py'
$RequirementsFile = Join-Path $RepositoryRoot 'requirements-release.txt'
$PackageStartUtc = [DateTime]::UtcNow
$WorkDirectory = $null
$CommandLogRoot = Join-Path $ReleaseWorkRoot 'command-logs'

function Fail([string]$Message) {
    throw "[release-iso] $Message"
}

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Test-UnderRoot([string]$Path, [string]$Root) {
    $fullPath = Get-FullPath $Path
    $fullRoot = (Get-FullPath $Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith($fullRoot, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-RepositoryRelativePath([string]$Path) {
    $fullPath = Get-FullPath $Path
    if (-not (Test-UnderRoot $fullPath $RepositoryRoot)) {
        Fail "path escapes the repository: $Path"
    }
    $rootPrefix = (Get-FullPath $RepositoryRoot).TrimEnd('\') + '\'
    return $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
}

function Get-PathDisplay([string]$Path) {
    if (Test-UnderRoot $Path $RepositoryRoot) {
        return Get-RepositoryRelativePath $Path
    }
    return [System.IO.Path]::GetFileName($Path)
}

function Format-CommandLine([string]$FilePath, [string[]]$Arguments) {
    $parts = @('"' + $FilePath.Replace('"', '\"') + '"')
    foreach ($argument in $Arguments) {
        if ($null -eq $argument) {
            $parts += '""'
        } elseif ($argument -match '[\s"]') {
            $parts += '"' + $argument.Replace('"', '\"') + '"'
        } else {
            $parts += $argument
        }
    }
    return ($parts -join ' ')
}

function Format-ProcessArguments([string[]]$Arguments) {
    $parts = @()
    foreach ($argument in $Arguments) {
        if ($null -eq $argument) {
            $parts += '""'
        } elseif ($argument -match '[\s"]') {
            $parts += '"' + $argument.Replace('"', '\"') + '"'
        } else {
            $parts += $argument
        }
    }
    return ($parts -join ' ')
}

function Read-ProcessOutput([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    return [IO.File]::ReadAllText($Path)
}

function Invoke-ExternalChecked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $RepositoryRoot,
        [switch]$AllowNonZero
    )

    if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf)) {
        Fail "external tool was not found: $FilePath"
    }
    $commandId = [Guid]::NewGuid().ToString('N')
    $safeName = ([IO.Path]::GetFileNameWithoutExtension($FilePath) -replace '[^A-Za-z0-9._-]', '_')
    New-Item -ItemType Directory -Path $CommandLogRoot -Force | Out-Null
    $stdoutLog = Join-Path $CommandLogRoot ("$commandId-$safeName-stdout.log")
    $stderrLog = Join-Path $CommandLogRoot ("$commandId-$safeName-stderr.log")
    $commandLine = Format-CommandLine $FilePath $Arguments
    Write-Host ("[release-iso] " + $commandLine) -ForegroundColor DarkGray
    $process = $null
    $exitCode = $null
    try {
        $process = Start-Process -FilePath $FilePath `
            -ArgumentList (Format-ProcessArguments $Arguments) `
            -WorkingDirectory $WorkingDirectory `
            -RedirectStandardOutput $stdoutLog `
            -RedirectStandardError $stderrLog `
            -PassThru `
            -WindowStyle Hidden `
            -ErrorAction Stop
        $process.WaitForExit()
        $exitCode = [int]$process.ExitCode
    } catch {
        Fail ("failed to start external command.`n" +
            "  executable: $FilePath`n" +
            "  arguments: $(Format-ProcessArguments $Arguments)`n" +
            "  working directory: $WorkingDirectory`n" +
            "  details: $($_.Exception.Message)")
    } finally {
        if ($null -ne $process) { $process.Dispose() }
    }

    $stdout = Read-ProcessOutput $stdoutLog
    $stderr = Read-ProcessOutput $stderrLog
    if ($stdout.Length -gt 0) { [Console]::Out.Write($stdout) }
    if ($stderr.Length -gt 0) { [Console]::Error.Write($stderr) }
    $combinedOutput = $stdout
    if ($stderr.Length -gt 0) {
        if ($combinedOutput.Length -gt 0) { $combinedOutput += [Environment]::NewLine }
        $combinedOutput += $stderr
    }
    if (-not $AllowNonZero -and $exitCode -ne 0) {
        Fail ("command failed with exit code ${exitCode}.`n" +
            "  executable: $FilePath`n" +
            "  arguments: $(Format-ProcessArguments $Arguments)`n" +
            "  working directory: $WorkingDirectory`n" +
            "  stdout log: $stdoutLog`n" +
            "  stderr log: $stderrLog")
    }
    return [PSCustomObject]@{
        ExitCode = $exitCode
        Output = $combinedOutput
        Stdout = $stdout
        Stderr = $stderr
        Command = $commandLine
        FilePath = $FilePath
        Arguments = @($Arguments)
        WorkingDirectory = $WorkingDirectory
        StdoutLog = $stdoutLog
        StderrLog = $stderrLog
    }
}

function Invoke-GitChecked([string[]]$Arguments) {
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($null -eq $git) { $git = Get-Command git -ErrorAction SilentlyContinue }
    if ($null -eq $git) { Fail 'git.exe is required to record the source commit.' }
    $result = Invoke-ExternalChecked -FilePath $git.Source -Arguments (@('-C', $RepositoryRoot) + $Arguments)
    return $result.Output.Trim()
}

function Get-ExistingFileCandidates([string[]]$Candidates) {
    $result = @()
    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $result += (Get-Item -LiteralPath $candidate).FullName
        }
    }
    return @($result | Sort-Object -Unique)
}

function Resolve-Oscdimg {
    if (-not [string]::IsNullOrWhiteSpace($OscdimgPath)) {
        if (-not (Test-Path -LiteralPath $OscdimgPath -PathType Leaf)) {
            Fail "-OscdimgPath does not name a file: $OscdimgPath"
        }
        return (Get-Item -LiteralPath $OscdimgPath).FullName
    }

    $candidates = @()
    $command = Get-Command oscdimg.exe -ErrorAction SilentlyContinue
    $adkRoots = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Assessment and Deployment Kit\Deployment Tools\amd64\Oscdimg'),
        (Join-Path $env:ProgramFiles 'Windows Kits\10\Assessment and Deployment Kit\Deployment Tools\amd64\Oscdimg'),
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Assessment and Deployment Kit\Deployment Tools\x86\Oscdimg')
    )
    if ($null -ne $command) { $candidates += $command.Source }
    foreach ($root in $adkRoots) {
        $candidates += (Join-Path $root 'oscdimg.exe')
    }
    $resolved = @(Get-ExistingFileCandidates $candidates)
    if ($resolved.Count -eq 0) {
        Fail "oscdimg.exe was not found. Install the Windows ADK Deployment Tools component (Deployment Tools only), or pass -OscdimgPath with the full path to oscdimg.exe. The script never downloads or installs the ADK."
    }
    if ([Environment]::Is64BitOperatingSystem) {
        $amd64 = @($resolved | Where-Object { $_ -match '(?i)[\\/]amd64[\\/]Oscdimg[\\/]oscdimg\.exe$' })
        if ($amd64.Count -gt 0) { $resolved = $amd64 }
    } else {
        $x86 = @($resolved | Where-Object { $_ -match '(?i)[\\/]x86[\\/]Oscdimg[\\/]oscdimg\.exe$' })
        if ($x86.Count -gt 0) { $resolved = $x86 }
    }
    if ($resolved.Count -gt 1) {
        Fail ("multiple oscdimg.exe candidates were found; pass -OscdimgPath explicitly:`n" + ($resolved -join "`n"))
    }
    return $resolved[0]
}

function Get-WorkingPythonCandidate {
    if (-not [string]::IsNullOrWhiteSpace($PythonPath)) {
        if (-not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) { Fail "-PythonPath does not name a file: $PythonPath" }
        return (Get-Item -LiteralPath $PythonPath).FullName
    }
    $localVenvPython = Join-Path $ReleaseToolsRoot 'venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $localVenvPython -PathType Leaf) { return (Get-Item -LiteralPath $localVenvPython).FullName }

    $candidate = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $candidate) { $candidate = Get-Command python3.exe -ErrorAction SilentlyContinue }
    if ($null -eq $candidate) {
        Fail "Python 3 was not found. Install Python 3 for the current user or use -PythonPath; then run with -BootstrapTools to create the repository-local release venv. No global package is installed by this script."
    }
    $candidatePath = $candidate.Source
    $probe = Invoke-ExternalChecked -FilePath $candidatePath -Arguments @('--version') -AllowNonZero
    if ($probe.ExitCode -ne 0) {
        Fail "the discovered Python command is not usable: $candidatePath. Pass -PythonPath to a real Python 3 executable."
    }
    if ($probe.Output -match '(?im)^Python was not found.*Microsoft Store') {
        Fail "the discovered Python command is the Microsoft Store execution-alias shim: $candidatePath. Pass -PythonPath to a real Python 3 executable."
    }
    return $candidatePath
}

function Resolve-ReleasePython {
    $venvPython = Join-Path $ReleaseToolsRoot 'venv\Scripts\python.exe'
    if ($BootstrapTools -and -not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
        $basePython = Get-WorkingPythonCandidate
        New-Item -ItemType Directory -Path $ReleaseToolsRoot -Force | Out-Null
        Invoke-ExternalChecked -FilePath $basePython -Arguments @('-m', 'venv', (Join-Path $ReleaseToolsRoot 'venv')) | Out-Null
    }
    if (-not [string]::IsNullOrWhiteSpace($PythonPath) -and -not $BootstrapTools) {
        $python = Get-WorkingPythonCandidate
    } elseif (Test-Path -LiteralPath $venvPython -PathType Leaf) {
        $python = (Get-Item -LiteralPath $venvPython).FullName
    } else {
        $python = Get-WorkingPythonCandidate
    }
    if ($BootstrapTools) {
        New-Item -ItemType Directory -Path $ReleaseToolsRoot -Force | Out-Null
        Invoke-ExternalChecked -FilePath $python -Arguments @('-m', 'pip', 'install', '--disable-pip-version-check', '--upgrade', '--requirement', $RequirementsFile) | Out-Null
    }
    $probeCode = "import importlib.metadata as m; assert m.version('pyfatfs') == '1.1.0'; assert m.version('fs') == '2.4.16'; assert m.version('pycdlib') == '1.16.0'; print('pyfatfs=' + m.version('pyfatfs')); print('fs=' + m.version('fs')); print('pycdlib=' + m.version('pycdlib'))"
    $probe = Invoke-ExternalChecked -FilePath $python -Arguments @('-c', $probeCode) -AllowNonZero
    if ($probe.ExitCode -ne 0) {
        Fail "the required local Python dependencies are missing or unpinned. Run again with -BootstrapTools, which installs only into out\release-tools\venv."
    }
    return $python
}

function Get-FileRecord([System.IO.FileInfo]$File) {
    if ($File.Length -le 0) { Fail "empty ESP file is not allowed in a release: $($File.FullName)" }
    $relative = Get-RepositoryRelativePath $File.FullName
    return [ordered]@{
        path = $relative
        size = [int64]$File.Length
        sha256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Get-NewestSource([string]$Root, [string[]]$Extensions, [string[]]$ExcludedDirectoryNames) {
    $files = @(Get-ChildItem -LiteralPath $Root -File -Recurse -Force | Where-Object {
        if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { return $false }
        if ($_.Name -ne 'Makefile' -and $Extensions -notcontains $_.Extension.ToLowerInvariant()) { return $false }
        foreach ($excluded in $ExcludedDirectoryNames) {
            if ($_.FullName -match ('[\\/]' + [regex]::Escape($excluded) + '[\\/]')) { return $false }
        }
        return $true
    })
    if ($files.Count -eq 0) { return $null }
    return ($files | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
}

function Assert-NotStale([System.IO.FileInfo]$Artifact, [System.IO.FileInfo]$NewestSource, [string]$Description) {
    if ($null -ne $NewestSource -and $Artifact.LastWriteTimeUtc -lt $NewestSource.LastWriteTimeUtc) {
        Fail "$Description is stale: $($Artifact.FullName) is older than $($NewestSource.FullName). Run the canonical build instead of -SkipBuild."
    }
}

function Get-EspContents {
    if (-not (Test-Path -LiteralPath $EspRoot -PathType Container)) { Fail "ESP directory is missing: $EspRoot" }
    $espItem = Get-Item -LiteralPath $EspRoot
    if ($espItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { Fail "ESP is a reparse point; refusing ambiguous input: $EspRoot" }
    $all = @(Get-ChildItem -LiteralPath $EspRoot -Recurse -Force)
    foreach ($item in $all) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { Fail "ESP contains a reparse point: $($item.FullName)" }
    }
    $files = @($all | Where-Object { -not $_.PSIsContainer } | Sort-Object FullName)
    if ($files.Count -eq 0) { Fail "ESP contains no files: $EspRoot" }
    $records = @()
    foreach ($file in $files) { $records += (Get-FileRecord $file) }

    $requiredPaths = @('EFI\BOOT\BOOTX64.EFI', 'kernel.elf', 'ramdisk.img')
    $required = @()
    foreach ($requiredPath in $requiredPaths) {
        $path = Join-Path $EspRoot $requiredPath
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { Fail "required ESP input is missing: $(Get-RepositoryRelativePath $path)" }
        $item = Get-Item -LiteralPath $path
        if ($item.Length -le 0) { Fail "required ESP input is empty: $(Get-RepositoryRelativePath $path)" }
        $required += (Get-FileRecord $item)
    }
    $totalBytes = [int64](($records | ForEach-Object { [int64]$_.size } | Measure-Object -Sum).Sum)
    return [PSCustomObject]@{ Files = $records; Required = $required; TotalBytes = $totalBytes }
}

function Get-EspPackageRecords($EspRecords) {
    $repositoryPrefix = 'ESP/'
    $records = @()
    foreach ($record in $EspRecords) {
        if (-not $record.path.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            Fail "ESP record is not rooted under ESP: $($record.path)"
        }
        $records += [ordered]@{
            path = $record.path.Substring($repositoryPrefix.Length)
            size = [int64]$record.size
            sha256 = $record.sha256
        }
    }
    return @($records)
}

function Get-ToolRecord([string]$Path, [string]$VersionText) {
    $fileInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    return [ordered]@{
        fileVersion = if ([string]::IsNullOrWhiteSpace($fileInfo.FileVersion)) { 'unknown' } else { $fileInfo.FileVersion }
        productVersion = if ([string]::IsNullOrWhiteSpace($fileInfo.ProductVersion)) { 'unknown' } else { $fileInfo.ProductVersion }
        reportedVersion = $VersionText.Trim()
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Get-PythonVersion([string]$Python) {
    return (Invoke-ExternalChecked -FilePath $Python -Arguments @('--version')).Output.Trim()
}

function Get-LocalFileRecord([System.IO.FileInfo]$File, [string]$IsoPath) {
    if ($File.Length -le 0) { Fail "empty ISO input file is not allowed: $($File.FullName)" }
    return [ordered]@{
        path = $IsoPath
        size = [int64]$File.Length
        sha256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Get-IsoExpectedRecords([System.IO.FileInfo]$FatImage, [System.IO.FileInfo]$Readme, [string]$BootImageIsoPath) {
    $naturalSectorCount = [int64][Math]::Ceiling([double]$FatImage.Length / 512.0)
    $sectorCount = if ($naturalSectorCount -gt 65535) { 0 } else { [int]$naturalSectorCount }
    return [ordered]@{
        files = @((Get-LocalFileRecord $Readme 'README.TXT'))
        bootImage = [ordered]@{
            path = $BootImageIsoPath
            size = [int64]$FatImage.Length
            sha256 = (Get-FileHash -LiteralPath $FatImage.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            sectorCount = $sectorCount
        }
    }
}

function Assert-Fat32Image([System.IO.FileInfo]$Image) {
    if ($Image.Length -lt 1048576 -or $Image.Length % 512 -ne 0) { Fail "generated EFI image has an implausible size: $($Image.Length) bytes" }
    $stream = [IO.File]::OpenRead($Image.FullName)
    try {
        $header = New-Object byte[] 512
        if ($stream.Read($header, 0, $header.Length) -ne $header.Length) { Fail 'could not read the generated EFI image boot sector' }
    } finally { $stream.Dispose() }
    $fsType = [Text.Encoding]::ASCII.GetString($header, 82, 8).Trim()
    $bytesPerSector = [BitConverter]::ToUInt16($header, 11)
    if ($fsType -ne 'FAT32' -or $bytesPerSector -ne 512 -or $header[510] -ne 0x55 -or $header[511] -ne 0xAA) {
        Fail "generated EFI image is not a valid FAT32 volume (type=$fsType, sector=$bytesPerSector)"
    }
}

function Copy-EspToStaging($Records, [string]$StagingRoot) {
    New-Item -ItemType Directory -Path $StagingRoot -Force | Out-Null
    foreach ($record in $Records) {
        $source = Join-Path $RepositoryRoot ($record.path -replace '/', '\')
        $repositoryPrefix = 'ESP/'
        if (-not $record.path.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            Fail "ESP record is not rooted under ESP: $($record.path)"
        }
        $packagePath = $record.path.Substring($repositoryPrefix.Length)
        $destination = Join-Path $StagingRoot ($packagePath -replace '/', '\')
        $parent = Split-Path -Parent $destination
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
}

function Invoke-CanonicalBuild {
    if (-not (Test-Path -LiteralPath $BuildScript -PathType Leaf)) { Fail "canonical build script is missing: $BuildScript" }
    $powershell = Get-Command powershell.exe -ErrorAction SilentlyContinue
    if ($null -eq $powershell) { Fail 'Windows PowerShell (powershell.exe) is required to invoke the canonical build.' }
    $buildArgs = @('-Arch', $Arch, '-I219Phase5Stage', [string]$I219Phase5Stage,
                   '-I219Phase6Stage', [string]$I219Phase6Stage)
    if ($Clean) { $buildArgs = @('-Clean', '-Arch', $Arch,
                                  '-I219Phase5Stage', [string]$I219Phase5Stage,
                                  '-I219Phase6Stage', [string]$I219Phase6Stage) }
    Write-Host "[release-iso] invoking canonical build.ps1 with supported arguments" -ForegroundColor Cyan
    Invoke-ExternalChecked -FilePath $powershell.Source -Arguments (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $BuildScript) + $buildArgs) | Out-Null
}

if ($Version -notmatch '^(0|[1-9][0-9]*)\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$') {
    Fail "invalid version '$Version'. Use a conservative semantic version such as 0.1.0 or 0.1.0-rc1."
}
if ($SkipBuild -and $Clean) { Fail '-Clean cannot be combined with -SkipBuild because it removes the existing ESP.' }
if ($IsoBackend -eq 'PyCdlib' -and -not [string]::IsNullOrWhiteSpace($OscdimgPath)) {
    Fail '-OscdimgPath is only valid with -IsoBackend Oscdimg. PyCdlib is the default backend and does not require the ADK.'
}

$sourceStatusLines = @(Invoke-GitChecked @('status', '--porcelain=v1', '--untracked-files=all') -split "`r?`n" | Where-Object { $_ -ne '' })
$sourceCommit = Invoke-GitChecked @('rev-parse', 'HEAD')
$sourceBranch = Invoke-GitChecked @('branch', '--show-current')
$worktreeClean = ($sourceStatusLines.Count -eq 0)
if (-not $worktreeClean) {
    Write-Host 'WARNING: worktree is dirty; the release manifest will record this and the artifact is not reproducible from HEAD alone.' -ForegroundColor Yellow
    if ($RequireCleanWorktree) { Fail 'worktree is dirty and -RequireCleanWorktree was supplied.' }
}

$artifactBase = "guideXOS-Server-v$Version-$Arch"
$isoName = "$artifactBase.iso"
$shaName = "$isoName.sha256"
$manifestName = "$artifactBase.manifest.json"
$finalIso = Join-Path $DistRoot $isoName
$finalSha = Join-Path $DistRoot $shaName
$finalManifest = Join-Path $DistRoot $manifestName
foreach ($existing in @($finalIso, $finalSha, $finalManifest)) {
    if (Test-Path -LiteralPath $existing) {
        if (-not $Force) { Fail "output already exists: $existing. Use -Force to replace it after successful validation." }
    }
}

$oscdimg = $null
$oscdimgVersion = $null
if ($IsoBackend -eq 'Oscdimg') {
    $oscdimg = Resolve-Oscdimg
    $oscdimgVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($oscdimg).ProductVersion
    if ([string]::IsNullOrWhiteSpace($oscdimgVersion)) { $oscdimgVersion = 'unknown' }
}
$python = $null
$fatImage = $null
$isoTemp = $null
$published = $false
try {
    if (-not (Test-Path -LiteralPath $EspHelper -PathType Leaf)) { Fail "FAT image helper is missing: $EspHelper" }
    if (-not (Test-Path -LiteralPath $IsoHelper -PathType Leaf)) { Fail "ISO verification helper is missing: $IsoHelper" }
    $python = Resolve-ReleasePython
    New-Item -ItemType Directory -Path $DistRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $ReleaseWorkRoot -Force | Out-Null
    $WorkDirectory = Join-Path $ReleaseWorkRoot ("$artifactBase-" + [Guid]::NewGuid().ToString('N'))
    if (-not (Test-UnderRoot $WorkDirectory $ReleaseWorkRoot)) { Fail 'internal work directory escaped the approved release work root.' }
    New-Item -ItemType Directory -Path $WorkDirectory -Force | Out-Null

    if (-not $SkipBuild) { Invoke-CanonicalBuild }
    $esp = Get-EspContents

    $identityPath = Join-Path $EspRoot 'build-identity.txt'
    if (Test-Path -LiteralPath $identityPath -PathType Leaf) {
        $identityText = [IO.File]::ReadAllText($identityPath)
        $identityMatch = [regex]::Match($identityText, '(?m)^phase5I219Stage=(\d+)\s*$')
        if ($identityMatch.Success -and [int]$identityMatch.Groups[1].Value -ne $I219Phase5Stage) {
            Fail "ESP build identity stage $($identityMatch.Groups[1].Value) does not match requested I219 Phase 5 stage $I219Phase5Stage. Rebuild without -SkipBuild."
        }
        $microStageMatch = [regex]::Match($identityText, '(?m)^phase6I219Stage=(\d+)\s*$')
        if ($microStageMatch.Success -and [int]$microStageMatch.Groups[1].Value -ne $I219Phase6Stage) {
            Fail "ESP build identity micro-stage $($microStageMatch.Groups[1].Value) does not match requested I219 Phase 6 micro-stage $I219Phase6Stage. Rebuild without -SkipBuild."
        }
    }

    $bootloader = Get-Item -LiteralPath (Join-Path $EspRoot 'EFI\BOOT\BOOTX64.EFI')
    $kernel = Get-Item -LiteralPath (Join-Path $EspRoot 'kernel.elf')
    $ramdisk = Get-Item -LiteralPath (Join-Path $EspRoot 'ramdisk.img')
    Assert-NotStale $bootloader (Get-NewestSource (Join-Path $RepositoryRoot 'guideXOSBootLoader') @('.cpp', '.h', '.asm', '.vcxproj', '.vcxproj.filters') @('x64', 'build', 'guideXOS.1fedf2ad')) 'BOOTX64.EFI'
    Assert-NotStale $kernel (Get-NewestSource (Join-Path $RepositoryRoot 'kernel') @('.cpp', '.h', '.asm', '.s', '.makefile', '.arch') @('build')) 'kernel.elf'
    $ramdiskSources = @(
        (Get-Item -LiteralPath (Join-Path $RepositoryRoot 'scripts\generate-wallpaper-pack.ps1') -ErrorAction SilentlyContinue),
        (Get-NewestSource (Join-Path $RepositoryRoot 'assets\Backgrounds') @('.png', '.jpg', '.jpeg', '.gif') @())
    ) | Where-Object { $null -ne $_ } | Sort-Object LastWriteTimeUtc -Descending
    if ($ramdiskSources.Count -gt 0) { Assert-NotStale $ramdisk $ramdiskSources[0] 'ramdisk.img' }

    $expectedPath = Join-Path $WorkDirectory 'esp-expected.json'
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($expectedPath, (@{ files = @(Get-EspPackageRecords $esp.Files) } | ConvertTo-Json -Depth 5) + "`n", $utf8NoBom)
    $fatPayloadReserve = [Math]::Max(8388608, [Math]::Ceiling([double]$esp.TotalBytes * 0.25))
    $directoryReserve = [Math]::Max(4096, $esp.Files.Count * 128)
    $calculatedSize = [int64]($esp.TotalBytes + $fatPayloadReserve + $directoryReserve)
    $calculatedSize = [int64]([Math]::Ceiling($calculatedSize / 1048576.0) * 1048576)
    $calculatedSize = [int64][Math]::Max(67108864, $calculatedSize)
    $fatImagePath = Join-Path $WorkDirectory 'EFI-BOOT.IMG'

    $fatArgs = @('create', '--source', $EspRoot, '--image', $fatImagePath, '--size', [string]$calculatedSize, '--expected', $expectedPath, '--report', (Join-Path $WorkDirectory 'fat-report.json'), '--volume-id', $sourceCommit.Substring(0, 8))
    Invoke-ExternalChecked -FilePath $python -Arguments (@($EspHelper) + $fatArgs) | Out-Null
    $fatImage = Get-Item -LiteralPath $fatImagePath
    Assert-Fat32Image $fatImage
    $fatReport = Get-Content -LiteralPath (Join-Path $WorkDirectory 'fat-report.json') -Raw | ConvertFrom-Json
    if ([int64]$fatReport.imageBytes -ne [int64]$fatImage.Length) { Fail 'FAT helper report does not match the generated image size.' }

    $readmePath = Join-Path $WorkDirectory 'release-readme.txt'
    $bootImageIsoPath = if ($IsoBackend -eq 'PyCdlib') { 'UEFI_BOOT.IMG' } else { 'EFI-BOOT.IMG' }
    $readmeText = @(
        'guideXOS Server release media',
        ("Version: $Version"),
        ("Architecture: $Arch"),
        ("Source commit: $sourceCommit"),
        ("I219 Phase 5 stage selector: $I219Phase5Stage"),
        ("I219 Phase 6 micro-stage selector: $I219Phase6Stage"),
        '',
        ("The bootable UEFI FAT image is $bootImageIsoPath."),
        'It contains the complete guideXOS Server EFI payload.',
        'This ISO is read-only release media.'
    ) -join "`n"
    [IO.File]::WriteAllText($readmePath, $readmeText + "`n", $utf8NoBom)
    $readme = Get-Item -LiteralPath $readmePath
    $isoExpectedPath = Join-Path $WorkDirectory 'iso-expected.json'
    [IO.File]::WriteAllText($isoExpectedPath, (Get-IsoExpectedRecords $fatImage $readme $bootImageIsoPath | ConvertTo-Json -Depth 5) + "`n", $utf8NoBom)
    $isoTemp = Join-Path $WorkDirectory 'artifact.iso'
    $isoReportPath = Join-Path $WorkDirectory 'iso-report.json'
    if ($IsoBackend -eq 'PyCdlib') {
        $isoArgs = @(
            $IsoHelper, 'create',
            '--boot-image', $fatImage.FullName,
            '--readme', $readme.FullName,
            '--output', $isoTemp,
            '--expected', $isoExpectedPath,
            '--report', $isoReportPath,
            '--volume-id', 'GUIDEXOS',
            '--oversized-sentinel', '0'
        )
        Invoke-ExternalChecked -FilePath $python -Arguments $isoArgs | Out-Null
    } else {
        $stagingRoot = Join-Path $WorkDirectory 'iso-root'
        Copy-EspToStaging $esp.Files $stagingRoot
        Copy-Item -LiteralPath $readme.FullName -Destination (Join-Path $stagingRoot 'README.TXT')
        Copy-Item -LiteralPath $fatImage.FullName -Destination (Join-Path $stagingRoot 'EFI-BOOT.IMG')
        $isoArgs = @('-m', '-n', '-j1', '-u2', '-udfver102', '-pEF', '-e', '-lGUIDEXOS', ("-b" + $fatImage.FullName), $stagingRoot, $isoTemp)
        $oscdimgResult = Invoke-ExternalChecked -FilePath $oscdimg -Arguments $isoArgs
        if ($oscdimgResult.ExitCode -eq 0 -and $oscdimgResult.Output -match '(?im)^\s*ERROR:\s*Boot sector file ".*" size is too large\s*$') {
            Fail ("oscdimg reported that the EFI boot image is too large despite exit code 0. The current EFI image requires the PyCdlib backend.`n" +
                "  stdout log: $($oscdimgResult.StdoutLog)`n" +
                "  stderr log: $($oscdimgResult.StderrLog)")
        }
        if (-not (Test-Path -LiteralPath $isoTemp -PathType Leaf)) { Fail 'oscdimg reported success but did not create the temporary ISO.' }
    }
    $isoInfo = Get-Item -LiteralPath $isoTemp
    if ($isoInfo.Length -le 0 -or $isoInfo.Length -lt $fatImage.Length) { Fail "generated ISO is empty or smaller than its EFI boot image: $($isoInfo.Length) bytes" }
    Invoke-ExternalChecked -FilePath $python -Arguments @($IsoHelper, 'verify', '--iso', $isoTemp, '--expected', $isoExpectedPath, '--report', $isoReportPath) | Out-Null
    $isoReport = Get-Content -LiteralPath $isoReportPath -Raw | ConvertFrom-Json

    $isoHash = (Get-FileHash -LiteralPath $isoTemp -Algorithm SHA256).Hash.ToLowerInvariant()
    $shaTemp = Join-Path $WorkDirectory $shaName
    [IO.File]::WriteAllText($shaTemp, "$isoHash  $isoName`n", $utf8NoBom)
    $gitVersion = Invoke-GitChecked @('--version')
    $toolRecords = [ordered]@{
        git = $gitVersion
        powershell = $PSVersionTable.PSVersion.ToString()
        python = Get-PythonVersion $python
        pyfatfs = '1.1.0'
        fs = '2.4.16'
        pycdlib = '1.16.0'
        isoBackend = $IsoBackend
        canonicalBuild = 'build.ps1 -Arch amd64 -I219Phase5Stage ' + $I219Phase5Stage + ' -I219Phase6Stage ' + $I219Phase6Stage + $(if ($Clean) { ' -Clean' } else { '' })
    }
    if ($IsoBackend -eq 'Oscdimg') {
        $toolRecords.oscdimg = Get-ToolRecord $oscdimg $oscdimgVersion
    }
    $manifest = [ordered]@{
        schemaVersion = 1
        product = 'guideXOS Server'
        version = $Version
        architecture = $Arch
        artifactType = 'bootable-uefi-iso'
        i219Phase5Stage = $I219Phase5Stage
        i219Phase6Stage = $I219Phase6Stage
        isoBackend = $IsoBackend
        isoFilename = $isoName
        isoByteSize = [int64]$isoInfo.Length
        sha256 = $isoHash
        sourceGitCommit = $sourceCommit
        sourceGitBranch = $sourceBranch
        worktreeCleanAtPackagingStart = [bool]$worktreeClean
        worktreeStatusAtPackagingStart = @($sourceStatusLines)
        buildTimestampUtc = $PackageStartUtc.ToString('o')
        requiredInputFiles = @($esp.Required)
        inputFiles = @($esp.Files)
        efiImage = [ordered]@{
            filename = 'EFI-BOOT.IMG'
            isoPath = $bootImageIsoPath
            byteSize = [int64]$fatImage.Length
            sha256 = (Get-FileHash -LiteralPath $fatImage.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            calculatedCapacityBytes = [int64]$calculatedSize
            calculatedPayloadBytes = [int64]$esp.TotalBytes
            safetyReserveBytes = [int64]($fatPayloadReserve + $directoryReserve)
            fatType = 'FAT32'
        }
        isoVerification = $isoReport
        tools = $toolRecords
        notes = @(
            'AMD64 UEFI only; no legacy BIOS El Torito entry is generated.',
            'The ISO and EFI image are read-only release media.',
            'Persistent user data must live on separate writable storage; this script never writes physical USB media.'
        )
    }
    $manifestTemp = Join-Path $WorkDirectory $manifestName
    [IO.File]::WriteAllText($manifestTemp, ($manifest | ConvertTo-Json -Depth 12) + "`n", $utf8NoBom)

    $publishedPaths = @()
    $backups = @()
    try {
        if ($Force) {
            $backupDirectory = Join-Path $WorkDirectory 'previous-artifacts'
            New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
            foreach ($existing in @($finalIso, $finalSha, $finalManifest)) {
                if (Test-Path -LiteralPath $existing) {
                    $backupPath = Join-Path $backupDirectory ([IO.Path]::GetFileName($existing))
                    Move-Item -LiteralPath $existing -Destination $backupPath
                    $backups += [PSCustomObject]@{ Original = $existing; Backup = $backupPath }
                }
            }
        }
        Move-Item -LiteralPath $isoTemp -Destination $finalIso
        $publishedPaths += $finalIso
        Move-Item -LiteralPath $shaTemp -Destination $finalSha
        $publishedPaths += $finalSha
        Move-Item -LiteralPath $manifestTemp -Destination $finalManifest
        $publishedPaths += $finalManifest
        $published = $true
    } catch {
        foreach ($publishedPath in $publishedPaths) {
            if (Test-Path -LiteralPath $publishedPath) {
                Remove-Item -LiteralPath $publishedPath -Force -ErrorAction SilentlyContinue
            }
        }
        foreach ($backup in ($backups | Sort-Object Original)) {
            if ((Test-Path -LiteralPath $backup.Backup) -and -not (Test-Path -LiteralPath $backup.Original)) {
                Move-Item -LiteralPath $backup.Backup -Destination $backup.Original -Force -ErrorAction SilentlyContinue
            }
        }
        throw
    }

    Write-Host ''
    Write-Host '[release-iso] release artifact created and structurally verified.' -ForegroundColor Green
    Write-Host ("  ISO:      " + $finalIso)
    Write-Host ("  SHA-256:  " + $finalSha)
    Write-Host ("  Manifest: " + $finalManifest)
    Write-Host ("  Size:     " + $isoInfo.Length + ' bytes')
    Write-Host ("  SHA-256:  " + $isoHash)
    Write-Host ''
    Write-Host 'Verify the checksum:' -ForegroundColor Cyan
    Write-Host ("  `$expected = (Get-Content -LiteralPath '" + $finalSha + "').Split()[0]; `$actual = (Get-FileHash -LiteralPath '" + $finalIso + "' -Algorithm SHA256).Hash; if (`$expected -ne `$actual) { throw 'SHA-256 mismatch' } else { 'SHA-256 OK' }")
    Write-Host 'Test the exact ISO in QEMU:' -ForegroundColor Cyan
    Write-Host ("  .\scripts\test-release-iso.ps1 -IsoPath '.\dist\$isoName'")
} finally {
    if ($null -ne $WorkDirectory -and (Test-Path -LiteralPath $WorkDirectory -PathType Container) -and (Test-UnderRoot $WorkDirectory $ReleaseWorkRoot)) {
        # WorkDirectory is a freshly-created child of the approved ignored root.
        # It is the only generated path this script ever removes.
        Remove-Item -LiteralPath $WorkDirectory -Recurse -Force -ErrorAction SilentlyContinue
    }
}
