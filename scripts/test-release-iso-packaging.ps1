param(
    [string]$PythonPath,
    [switch]$BootstrapTools
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$ReleaseToolsRoot = Join-Path $RepositoryRoot 'out\release-tools'
$ReleaseWorkRoot = Join-Path $RepositoryRoot 'out\release-iso'
$Helper = Join-Path $RepositoryRoot 'tools\release_esp.py'
$Requirements = Join-Path $RepositoryRoot 'requirements-release.txt'
$work = Join-Path $ReleaseWorkRoot ('smoke-' + [Guid]::NewGuid().ToString('N'))

function Fail([string]$Message) { throw "[release-iso-smoke] $Message" }

function Invoke-Checked([string]$FilePath, [string[]]$Arguments) {
    $output = @(& $FilePath @Arguments 2>&1)
    $exitCode = if ($null -eq $LASTEXITCODE) { if ($?) { 0 } else { 1 } } else { [int]$LASTEXITCODE }
    foreach ($line in $output) { Write-Host ([string]$line) }
    if ($exitCode -ne 0) { Fail "command failed with exit code ${exitCode}: $FilePath" }
}

function Resolve-Python {
    $venvPython = Join-Path $ReleaseToolsRoot 'venv\Scripts\python.exe'
    if ($BootstrapTools -and -not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
        $base = $PythonPath
        if ([string]::IsNullOrWhiteSpace($base)) {
            $command = Get-Command python.exe -ErrorAction SilentlyContinue
            if ($null -eq $command) { Fail 'Python 3 is required; pass -PythonPath or bootstrap the release tools venv first.' }
            $base = $command.Source
        }
        New-Item -ItemType Directory -Path $ReleaseToolsRoot -Force | Out-Null
        Invoke-Checked $base @('-m', 'venv', (Join-Path $ReleaseToolsRoot 'venv'))
        Invoke-Checked $venvPython @('-m', 'pip', 'install', '--disable-pip-version-check', '--upgrade', '--requirement', $Requirements)
    }
    if (-not [string]::IsNullOrWhiteSpace($PythonPath)) {
        if (-not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) { Fail "Python path does not exist: $PythonPath" }
        return (Get-Item -LiteralPath $PythonPath).FullName
    }
    if (Test-Path -LiteralPath $venvPython -PathType Leaf) { return (Get-Item -LiteralPath $venvPython).FullName }
    $command = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $command) { Fail 'Python 3 is required; pass -PythonPath or use -BootstrapTools.' }
    return $command.Source
}

try {
    if (-not (Test-Path -LiteralPath $Helper -PathType Leaf)) { Fail "helper is missing: $Helper" }
    $python = Resolve-Python
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    $source = Join-Path $work 'synthetic-esp'
    New-Item -ItemType Directory -Path (Join-Path $source 'EFI\BOOT') -Force | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $source 'EFI\BOOT\BOOTX64.EFI'), [byte[]](0..255))
    [IO.File]::WriteAllBytes((Join-Path $source 'kernel.elf'), [byte[]](1..64))
    [IO.File]::WriteAllBytes((Join-Path $source 'ramdisk.img'), (New-Object byte[] 4096))

    $files = @()
    foreach ($relative in @('EFI/BOOT/BOOTX64.EFI', 'kernel.elf', 'ramdisk.img')) {
        $path = Join-Path $source ($relative -replace '/', '\')
        $item = Get-Item -LiteralPath $path
        $files += [ordered]@{
            path = $relative
            size = [int64]$item.Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $expected = Join-Path $work 'expected.json'
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($expected, (@{ files = $files } | ConvertTo-Json -Depth 5) + "`n", $utf8NoBom)
    $image = Join-Path $work 'EFI-BOOT.IMG'
    $report = Join-Path $work 'report.json'
    Invoke-Checked $python @($Helper, 'create', '--source', $source, '--image', $image, '--size', '67108864', '--expected', $expected, '--report', $report, '--volume-id', '47584F53')
    if (-not (Test-Path -LiteralPath $image -PathType Leaf) -or (Get-Item -LiteralPath $image).Length -ne 67108864) { Fail 'synthetic FAT image has the wrong size.' }
    Invoke-Checked $python @($Helper, 'verify', '--image', $image, '--expected', $expected, '--report', (Join-Path $work 'verify-report.json'))
    Write-Host '[release-iso-smoke] passed: synthetic FAT packaging and read-only verification succeeded.' -ForegroundColor Green
    Write-Host '[release-iso-smoke] dummy files are not bootable and this test does not claim bare-metal or QEMU validity.' -ForegroundColor Yellow
} finally {
    if ((Test-Path -LiteralPath $work -PathType Container) -and $work -like ($ReleaseWorkRoot.TrimEnd('\') + '\*')) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
