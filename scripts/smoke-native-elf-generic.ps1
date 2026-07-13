param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = [System.IO.Path]::GetFullPath($RepoRoot)

function Assert-SourcePattern([string]$Path, [string]$Pattern, [string]$Label) {
    $text = Get-Content -LiteralPath $Path -Raw
    if ($text -notmatch $Pattern) { throw "$Label missing pattern: $Pattern" }
}

function Assert-SourceAbsent([string]$Path, [string]$Pattern, [string]$Label) {
    $text = Get-Content -LiteralPath $Path -Raw
    if ($text -match $Pattern) { throw "$Label unexpectedly contains: $Pattern" }
}

$executor = Join-Path $Root "native_elf_executor.cpp"
$runtime = Join-Path $Root "native_app_runtime.cpp"
$registry = Join-Path $Root "app_registry.cpp"
$loader = Join-Path $Root "native_elf_image_loader.cpp"

foreach ($path in @($executor, $runtime, $registry, $loader)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Generic Native ELF source missing: $path" }
}

Assert-SourcePattern $loader 'image\.preferredBaseAddress\s*=\s*image\.isPositionIndependent\s*\?\s*0\s*:\s*minVirtualAddress' "ET_EXEC preferred base derives from PT_LOAD range"
Assert-SourcePattern $executor 'if \(image\.preferredBaseAddress != minVirtualAddress\)' "preferred-base mismatch rejection"
Assert-SourcePattern $executor 'ExecutableMemory::AllocateAt\(preferredBase' "fixed-address mapping attempt"
Assert-SourcePattern $executor 'relocations are not supported' "no-relocation fallback rejection"
Assert-SourcePattern $executor 'if \(!hasIndexHint && !hasBlockHint\) return true;' "optional TLS absence preserves native path"
Assert-SourcePattern $executor 'TlsFree\(slot\)' "TLS slot release"
Assert-SourcePattern $executor 'block\.clear\(\)' "TLS block reset"
Assert-SourcePattern $runtime 'copyManifestHintToEnvironment' "manifest hint propagation"
Assert-SourcePattern $runtime 'GX_NATIVE_ELF_TLS_INDEX_ADDRESS' "generic TLS environment key"
Assert-SourcePattern $runtime 'GX_NATIVE_ELF_TLS_BLOCK_SIZE' "generic TLS block-size environment key"
Assert-SourcePattern $registry 'GXOS_NATIVE_ELF_STAGE_ROOT' "opt-in stage-root gate"
Assert-SourcePattern $registry 'sources\.insert\(sources\.end\(\), stagedSources\.begin\(\), stagedSources\.end\(\)\)' "staged sources append without changing defaults"
Assert-SourceAbsent $executor 'NativeAot|Native AOT|HostLogProof|\.NET' "generic executor naming"
Assert-SourceAbsent $runtime 'NativeAot|Native AOT|HostLogProof|\.NET' "generic runtime naming"
Assert-SourceAbsent $registry 'NativeAot|Native AOT|HostLogProof|\.NET' "generic registry naming"

Write-Host "Generic Native ELF regression smoke PASS" -ForegroundColor Green
