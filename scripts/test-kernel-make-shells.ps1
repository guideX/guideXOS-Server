[CmdletBinding()]
param(
    [string]$OutputRoot = "out\validation\kernel-make-shells"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$make = Get-Command mingw32-make.exe -ErrorAction SilentlyContinue
if (-not $make -and (Test-Path -LiteralPath "C:\mingw64\bin\mingw32-make.exe")) {
    $make = Get-Item -LiteralPath "C:\mingw64\bin\mingw32-make.exe"
}
if (-not $make) { throw "mingw32-make.exe was not found." }

$gitSh = Get-Command sh.exe -ErrorAction SilentlyContinue
if (-not $gitSh) {
    foreach ($candidate in @("C:\Program Files\Git\bin\sh.exe", "C:\Program Files\Git\usr\bin\sh.exe")) {
        if (Test-Path -LiteralPath $candidate) {
            $gitSh = Get-Item -LiteralPath $candidate
            break
        }
    }
}
if (-not $gitSh) { throw "Git POSIX sh.exe was not found; the original failing shell path cannot be verified." }

$outputPath = if ([IO.Path]::IsPathRooted($OutputRoot)) {
    [IO.Path]::GetFullPath($OutputRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputRoot))
}
$validationRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "out\validation"))
if (-not $outputPath.StartsWith($validationRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be a child of out\validation."
}
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$harness = Join-Path $outputPath "kernel-make-shells.mk"
@'
include kernel/makefile-platform.mk

.PHONY: macro-create macro-remove
macro-create:
	@$(call MKDIR,$(TEST_DIR))

macro-remove:
	@$(call RM,$(TEST_DIR))
'@ | Set-Content -LiteralPath $harness -Encoding Ascii

function Invoke-MacroCheck {
    param(
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][string]$Shell,
        [Parameter(Mandatory)][string]$ShellFlags
    )

    $testPath = [IO.Path]::GetFullPath((Join-Path $outputPath ("macro target " + $Label)))
    if (-not $testPath.StartsWith($validationRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing macro target outside out\validation."
    }
    $shellArgument = "SHELL=$Shell"
    $flagsArgument = ".SHELLFLAGS=$ShellFlags"
    $testArgument = "TEST_DIR=$testPath"
    & $make.Source -f $harness macro-create "OS=Windows_NT" $testArgument $shellArgument $flagsArgument
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $testPath -PathType Container)) {
        throw "MKDIR recipe failed under $Label (exit $LASTEXITCODE)."
    }
    & $make.Source -f $harness macro-remove "OS=Windows_NT" $testArgument $shellArgument $flagsArgument
    if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $testPath)) {
        throw "RM recipe failed under $Label (exit $LASTEXITCODE)."
    }
    Write-Host "MKDIR/RM recipe PASS under $Label."
}

Invoke-MacroCheck -Label "git-posix-sh" -Shell $gitSh.Source -ShellFlags "-c"
Invoke-MacroCheck -Label "cmd" -Shell "cmd.exe" -ShellFlags "/d /c"
