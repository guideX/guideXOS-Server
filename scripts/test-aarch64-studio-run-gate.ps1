[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourcePath = Join-Path $repoRoot 'kernel\core\native_elf_baremetal_arm64.inc'
$source = [IO.File]::ReadAllText($sourcePath)

function Get-FunctionBody([string]$Text, [string]$Name) {
    $signature = "$Name("
    $start = $Text.IndexOf($signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Production Studio Run gate source is missing $Name" }
    $open = $Text.IndexOf('{', $start)
    if ($open -lt 0) { throw "Production Studio Run gate source is missing the $Name body" }
    $depth = 0
    for ($index = $open; $index -lt $Text.Length; ++$index) {
        if ($Text[$index] -eq '{') { ++$depth }
        elseif ($Text[$index] -eq '}') {
            --$depth
            if ($depth -eq 0) { return $Text.Substring($open, $index - $open + 1) }
        }
    }
    throw "Production Studio Run gate source has an unterminated $Name body"
}

$prepare = Get-FunctionBody $source 'phase12_run_prepare'
$start = Get-FunctionBody $source 'phase12_run_start'
$loader = Get-FunctionBody $source 'phase9_load_and_run'

if (!$prepare.Contains('studio_child_decision()') -or
    $prepare.IndexOf('studio_child_decision()', [StringComparison]::Ordinal) -gt
        $prepare.IndexOf('refresh();', [StringComparison]::Ordinal) -or
    $prepare.IndexOf('studio_child_decision()', [StringComparison]::Ordinal) -gt
        $prepare.IndexOf('arm64_find_package(', [StringComparison]::Ordinal)) {
    throw 'Studio Run prepare must reject before App Model refresh and package lookup.'
}

if (!$start.Contains('studio_child_decision()') -or
    !$start.Contains('write_rejection(') -or
    !$start.Contains('GX_ERROR_UNSUPPORTED') -or
    $start.Contains('phase12_run_package(') -or
    $start.Contains('GX_DEVELOPMENT_RUN_COMPLETED') -or
    $start -match 'exitCode\s*=\s*[^;]*\b42\b') {
    throw 'Studio Run start must reject without entering a child or fabricating exit 42.'
}

if (!$loader.Contains('runtime->isPhase12Application') -or
    $loader.IndexOf('runtime->isPhase12Application', [StringComparison]::Ordinal) -gt
        $loader.IndexOf('phase9_initialize_host(runtime);', [StringComparison]::Ordinal)) {
    throw 'Phase 9 loader must block a Studio child before privileged host setup or image load.'
}

Write-Host 'AARCH64 Studio Run production gate controls: PASS (prepare, start, loader backstop, no synthetic exit 42)' -ForegroundColor Green
exit 0
