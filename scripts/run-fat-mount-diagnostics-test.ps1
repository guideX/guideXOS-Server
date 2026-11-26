[CmdletBinding()]
param(
    [string]$OutputDirectory = "out\validation\fat-mount-diagnostics"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
if (-not $compiler -and (Test-Path -LiteralPath "C:\mingw64\bin\g++.exe")) {
    $compiler = Get-Item -LiteralPath "C:\mingw64\bin\g++.exe"
}
if (-not $compiler) { throw "g++ was not found." }

$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$commonArguments = @(
    "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-O2",
    "-iquote", "kernel/core/include",
    "-I", "kernel/arch/amd64/include",
    "tests/fat_mount_diagnostics_test.cpp",
    "kernel/core/block_device.cpp",
    "kernel/core/fs_fat.cpp"
)

foreach ($configuration in @(
    @{ Name = "diagnostics-on"; Define = "-DGXOS_FAT_MOUNT_DIAGNOSTICS" },
    @{ Name = "diagnostics-off"; Define = $null }
)) {
    $executable = Join-Path $outputRoot ("fat_mount_" + $configuration.Name + ".exe")
    $arguments = @($commonArguments)
    if ($configuration.Define) { $arguments += $configuration.Define }
    $arguments += @("-o", $executable)

    Write-Host ("Building FAT mount regression ({0}) with {1}" -f
        $configuration.Name, $compiler.Source)
    & $compiler.Source @arguments
    if ($LASTEXITCODE -ne 0) {
        throw ("FAT mount regression ({0}) build failed with exit code {1}." -f
            $configuration.Name, $LASTEXITCODE)
    }

    & $executable
    if ($LASTEXITCODE -ne 0) {
        throw ("FAT mount regression ({0}) failed with exit code {1}." -f
            $configuration.Name, $LASTEXITCODE)
    }
}

Write-Host "FAT mount diagnostics hosted regressions PASS in both configurations."
