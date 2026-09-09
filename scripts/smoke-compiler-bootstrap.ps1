[CmdletBinding()]
param(
    [int]$BootCount = 3,
    [int]$TimeoutSeconds = 45,
    [switch]$Phase27E,
    [switch]$Phase27F,
    [switch]$Phase27G,
    [switch]$Phase27H,
    [switch]$Phase27I,
    [switch]$Phase27J,
    [switch]$Phase27K,
    [switch]$Phase27L,
    [switch]$Phase27M,
    [switch]$Phase27MOnly,
    [switch]$Phase27N,
    [switch]$Phase27NOnly,
    [switch]$Phase27O,
    [switch]$Phase27OOnly,
    [switch]$Phase27P,
    [switch]$Phase27POnly,
    [switch]$Phase27Q,
    [switch]$Phase27QOnly,
    [switch]$Phase27R,
    [switch]$Phase27ROnly,
    [switch]$Phase27S,
    [switch]$Phase27SOnly,
    [switch]$Phase27T,
    [switch]$Phase27TOnly,
    [switch]$Phase27U,
    [switch]$Phase27UOnly,
    [switch]$Phase27V,
    [switch]$Phase27VOnly,
    [switch]$Phase27W,
    [switch]$Phase27WOnly,
    [switch]$Phase27X,
    [switch]$Phase27XOnly,
    [switch]$Phase27Y,
    [switch]$Phase27YOnly,
    [switch]$Phase27Z,
    [switch]$Phase27ZOnly,
    [switch]$Phase28A,
    [switch]$Phase28AOnly,
    [switch]$Phase28B,
    [switch]$Phase28BOnly,
    [switch]$Phase28C,
    [switch]$Phase28COnly,
    [switch]$Phase28D,
    [switch]$Phase28DOnly
)

$ErrorActionPreference = "Stop"
# Phase 27G includes the complete earlier integration chain.  The focused M
# mode deliberately keeps only the baseline C/D route plus the M smoke so a
# flaky optional earlier IDE repeat cannot mask the recursion proof.
if ($Phase28DOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27U = $false; $Phase27V = $false; $Phase27W = $false; $Phase27X = $false
    $Phase27Y = $false; $Phase27Z = $false; $Phase28A = $false; $Phase28B = $false
    $Phase28C = $false; $Phase28D = $true
} elseif ($Phase28COnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27U = $false; $Phase27V = $false; $Phase27W = $false; $Phase27X = $false
    $Phase27Y = $false; $Phase27Z = $false; $Phase28A = $false; $Phase28B = $false; $Phase28C = $true; $Phase28D = $false
} elseif ($Phase28BOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27U = $false; $Phase27V = $false; $Phase27W = $false; $Phase27X = $false
    $Phase27Y = $false; $Phase27Z = $false; $Phase28A = $false; $Phase28B = $true; $Phase28C = $false; $Phase28D = $false
} elseif ($Phase28AOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27U = $false; $Phase27V = $false; $Phase27W = $false; $Phase27X = $false
    $Phase27Y = $false; $Phase27Z = $false; $Phase28A = $true; $Phase28B = $false; $Phase28C = $false; $Phase28D = $false
} elseif ($Phase27ZOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27U = $false; $Phase27V = $false; $Phase27W = $false; $Phase27X = $false
    $Phase27Y = $false; $Phase27Z = $true
} elseif ($Phase27YOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27U = $false; $Phase27V = $false; $Phase27W = $false; $Phase27X = $false
    $Phase27Y = $true
} elseif ($Phase27XOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27U = $false; $Phase27V = $false; $Phase27W = $false; $Phase27X = $true
} elseif ($Phase27WOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27U = $false; $Phase27V = $false; $Phase27W = $true; $Phase27X = $false
} elseif ($Phase27VOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27U = $false; $Phase27V = $true; $Phase27W = $false
} elseif ($Phase27UOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $false
    $Phase27V = $false; $Phase27W = $false
    $Phase27U = $true
} elseif ($Phase27TOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $false; $Phase27T = $true
} elseif ($Phase27SOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $false; $Phase27S = $true; $Phase27T = $false
} elseif ($Phase27ROnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $true
} elseif ($Phase27QOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $true; $Phase27T = $false
} elseif ($Phase27POnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false
    $Phase27P = $true
} elseif ($Phase27OOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false
    $Phase27O = $true
} elseif ($Phase27NOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false
    $Phase27N = $true
} elseif ($Phase27MOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $true
} else {
    if ($Phase27Y) {
        $Phase27X = $true
    }
    if ($Phase27Z) {
        $Phase27Y = $true
    }
    if ($Phase27X) {
        $Phase27W = $true
    }
    if ($Phase27W) {
        $Phase27V = $true
    }
    if ($Phase27V) {
        $Phase27U = $true; $Phase27T = $true; $Phase27S = $true; $Phase27R = $true
        $Phase27Q = $true; $Phase27P = $true; $Phase27O = $true; $Phase27N = $true
        $Phase27M = $true; $Phase27L = $true; $Phase27K = $true; $Phase27J = $true
        $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true
    }
    if ($Phase27G) { $Phase27F = $true; $Phase27E = $true }
    if ($Phase27H) { $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27I) { $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27J) { $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27K) { $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27L) { $Phase27K = $true; $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27M) { $Phase27L = $true; $Phase27K = $true; $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27N) { $Phase27M = $true; $Phase27L = $true; $Phase27K = $true; $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27O) { $Phase27N = $true; $Phase27M = $true; $Phase27L = $true; $Phase27K = $true; $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27P) { $Phase27O = $true; $Phase27N = $true; $Phase27M = $true; $Phase27L = $true; $Phase27K = $true; $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27Q) { $Phase27P = $true; $Phase27O = $true; $Phase27N = $true; $Phase27M = $true; $Phase27L = $true; $Phase27K = $true; $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27R) { $Phase27Q = $true; $Phase27P = $true; $Phase27O = $true; $Phase27N = $true; $Phase27M = $true; $Phase27L = $true; $Phase27K = $true; $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27S) { $Phase27R = $true; $Phase27Q = $true; $Phase27P = $true; $Phase27O = $true; $Phase27N = $true; $Phase27M = $true; $Phase27L = $true; $Phase27K = $true; $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
    if ($Phase27T) { $Phase27S = $true; $Phase27R = $true; $Phase27Q = $true; $Phase27P = $true; $Phase27O = $true; $Phase27N = $true; $Phase27M = $true; $Phase27L = $true; $Phase27K = $true; $Phase27J = $true; $Phase27I = $true; $Phase27H = $true; $Phase27G = $true; $Phase27F = $true; $Phase27E = $true }
}
# The Phase 27R IDE proof includes several guest build/run cycles. Give that
# workload enough time when callers use the script default, while preserving
# an explicitly longer timeout unchanged.
if (($Phase27R -or $Phase27S -or $Phase27T) -and $TimeoutSeconds -lt 120) { $TimeoutSeconds = 120 }
if (($Phase27U -or $Phase27V) -and $TimeoutSeconds -lt 120) { $TimeoutSeconds = 120 }
if ($Phase27W -and $TimeoutSeconds -lt 120) { $TimeoutSeconds = 120 }
if ($Phase27X -and $TimeoutSeconds -lt 120) { $TimeoutSeconds = 120 }
if ($Phase27Y -and $TimeoutSeconds -lt 120) { $TimeoutSeconds = 120 }
if ($Phase27Z -and $TimeoutSeconds -lt 120) { $TimeoutSeconds = 120 }
if (($Phase28A -or $Phase28B -or $Phase28C -or $Phase28D) -and $TimeoutSeconds -lt 120) { $TimeoutSeconds = 120 }
$root = Split-Path -Parent $PSScriptRoot
$kernelDirectory = Join-Path $root "kernel"
$espDirectory = Join-Path $root "ESP"
$fixtureDirectory = Join-Path $root "scripts/fixtures/phase27b"
$phase27dFixtureDirectory = Join-Path $root "scripts/fixtures/phase27d"
$phase27eFixtureDirectory = Join-Path $root "scripts/fixtures/phase27e"
$phase27fFixtureDirectory = Join-Path $root "scripts/fixtures/phase27f"
$phase27gFixtureDirectory = Join-Path $root "scripts/fixtures/phase27g"
$phase27hFixtureDirectory = Join-Path $root "scripts/fixtures/phase27h"
$phase27iFixtureDirectory = Join-Path $root "scripts/fixtures/phase27i"
$phase27jFixtureDirectory = Join-Path $root "scripts/fixtures/phase27j"
$phase27kFixtureDirectory = Join-Path $root "scripts/fixtures/phase27k"
$phase27lFixtureDirectory = Join-Path $root "scripts/fixtures/phase27l"
$phase27mFixtureDirectory = Join-Path $root "scripts/fixtures/phase27m"
$phase27nFixtureDirectory = Join-Path $root "scripts/fixtures/phase27n"
$phase27oFixtureDirectory = Join-Path $root "scripts/fixtures/phase27o"
$phase27pFixtureDirectory = Join-Path $root "scripts/fixtures/phase27p"
$phase27qFixtureDirectory = Join-Path $root "scripts/fixtures/phase27q"
$phase27rFixtureDirectory = Join-Path $root "scripts/fixtures/phase27r"
$phase27sFixtureDirectory = Join-Path $root "scripts/fixtures/phase27s"
$phase27tFixtureDirectory = Join-Path $root "scripts/fixtures/phase27t"
$phase27uFixtureDirectory = Join-Path $root "scripts/fixtures/phase27u"
$phase27vFixtureDirectory = Join-Path $root "scripts/fixtures/phase27v"
$phase27wFixtureDirectory = Join-Path $root "scripts/fixtures/phase27w"
$phase27xFixtureDirectory = Join-Path $root "scripts/fixtures/phase27x"
$phase27yFixtureDirectory = Join-Path $root "scripts/fixtures/phase27y"
$phase27zFixtureDirectory = Join-Path $root "scripts/fixtures/phase27z"
$phase28aFixtureDirectory = Join-Path $root "scripts/fixtures/phase28a"
$phase28bFixtureDirectory = Join-Path $root "scripts/fixtures/phase28b"
$phase28cFixtureDirectory = Join-Path $root "scripts/fixtures/phase28c"
$phase28dFixtureDirectory = Join-Path $root "scripts/fixtures/phase28d"
$developerStudioRoot = Join-Path (Split-Path -Parent $root) "guideXOS_Developer_Studio"
$phase27eAppDirectory = Join-Path $root "Apps/DS27E"
$phase27fAppDirectory = Join-Path $root "Apps/DS27F"
$phase27gAppDirectory = Join-Path $root "Apps/DS27G"
$phase27hAppDirectory = Join-Path $root "Apps/DS27H"
$phase27iAppDirectory = Join-Path $root "Apps/DS27I"
$phase27jAppDirectory = Join-Path $root "Apps/DS27J"
$phase27kAppDirectory = Join-Path $root "Apps/DS27K"
$phase27lAppDirectory = Join-Path $root "Apps/DS27L"
$phase27mAppDirectory = Join-Path $root "Apps/DS27M"
$phase27nAppDirectory = Join-Path $root "Apps/DS27N"
$phase27oAppDirectory = Join-Path $root "Apps/DS27O"
$phase27pAppDirectory = Join-Path $root "Apps/DS27P"
$phase27qAppDirectory = Join-Path $root "Apps/DS27Q"
$phase27rAppDirectory = Join-Path $root "Apps/DS27R"
$phase27sAppDirectory = Join-Path $root "Apps/DS27S"
$phase27tAppDirectory = Join-Path $root "Apps/DS27T"
$qemuPath = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$ovmfCodePath = Join-Path $root "OVMF.fd"
$tempDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("guidexos-phase27d-" + [guid]::NewGuid().ToString("N"))
$evidenceDirectory = Join-Path $tempDirectory "artifacts"
$backups = @{}
$directoryBackups = @{}
$activeEspDirectory = $espDirectory
$oldExtraCFlags = $env:EXTRA_CFLAGS

function Get-RequiredTool([string]$name, [string]$fallback) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    if ($fallback -and (Test-Path $fallback)) { return $fallback }
    throw "Required tool not found: $name"
}

function Quote-ProcessArgument([string]$value) {
    if ($value -notmatch '[\s"]') { return $value }
    return '"' + $value.Replace('"', '\"') + '"'
}

function Read-SerialText([string]$path) {
    if (!(Test-Path $path)) { return "" }
    return [System.IO.File]::ReadAllText($path)
}

function Export-SerialArtifact([string]$serial, [string]$name, [string]$destination) {
    $escapedName = [regex]::Escape($name)
    $pattern = "(?s)NativeElf: artifact_begin=$escapedName bytes=([0-9A-Fa-f]{8})\r?\nNativeElf: artifact_hex=([0-9A-Fa-f]+)\r?\nNativeElf: artifact_end=$escapedName"
    $match = [regex]::Match($serial, $pattern)
    if (!$match.Success) { throw "serial ELF evidence missing: $name" }

    $byteCount = [Convert]::ToInt32($match.Groups[1].Value, 16)
    $hex = $match.Groups[2].Value
    if ($byteCount -le 0 -or $hex.Length -ne ($byteCount * 2)) {
        throw "serial ELF evidence has invalid length: $name"
    }

    $bytes = New-Object byte[] $byteCount
    for ($index = 0; $index -lt $byteCount; ++$index) {
        $bytes[$index] = [Convert]::ToByte($hex.Substring($index * 2, 2), 16)
    }
    [System.IO.File]::WriteAllBytes($destination, $bytes)
}

function Stage-Phase27HProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27hFixtureDirectory $target -Recurse -Force
    $sourceDirectory = Join-Path $target "src"
    $testDirectory = Join-Path $target "tests"
    New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
    foreach ($sourceName in @("h27eq.c", "h27eqfalse.c", "h27cmp.c", "h27if.c", "h27suppress.c", "h27ifelse.c", "h27else.c", "h27nested.c", "h27truthy.c", "h27falsy.c", "h27assign.c", "h27missing.c", "h27invalid.c")) {
        Move-Item (Join-Path $target $sourceName) (Join-Path $testDirectory $sourceName) -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $target "out") | Out-Null
}

function Stage-Phase27IProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27iFixtureDirectory $target -Recurse -Force
    $testDirectory = Join-Path $target "tests"
    New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
    foreach ($sourceName in @(
        "i27and11.c", "i27and10.c", "i27and01.c", "i27and00.c",
        "i27or11.c", "i27or10.c", "i27or01.c", "i27or00.c",
        "i27canonicaland.c", "i27canonicalor.c", "i27preca.c", "i27precb.c", "i27precc.c",
        "i27andif.c", "i27orif.c", "i27mixed.c", "i27nested.c", "i27assign.c",
        "i27shortand.c", "i27shortor.c", "i27invalid.c", "i27singleand.c", "i27singleor.c")) {
        Move-Item (Join-Path $target $sourceName) (Join-Path $testDirectory $sourceName) -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $target "out") | Out-Null
}

function Stage-Phase27JProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27jFixtureDirectory $target -Recurse -Force
    $testDirectory = Join-Path $target "tests"
    New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
    foreach ($sourceName in @(
        "j27basic.c", "j27sum.c", "j27zero.c", "j27reeval.c", "j27logical.c", "j27logical_or.c",
        "j27ifwhile.c", "j27whileif.c", "j27nested.c", "j27bodydecl.c", "j27calls.c",
        "j27runtime1.c", "j27runtime2.c", "j27return.c", "j27invalid_empty.c",
        "j27invalid_relational.c", "j27missing.c")) {
        Move-Item (Join-Path $target $sourceName) (Join-Path $testDirectory $sourceName) -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $target "out") | Out-Null
}

function Stage-Phase27KProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27kFixtureDirectory $target -Recurse -Force
    $testDirectory = Join-Path $target "tests"
    New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
    foreach ($sourceName in @(
        "k27basic.c", "k27continue.c", "k27break_if.c", "k27continue_if.c", "k27combined.c",
        "k27skip_tail.c", "k27break_tail.c", "k27nested_break.c", "k27nested_continue.c",
        "k27host_continue.c", "k27host_break.c", "k27break_outside.c", "k27continue_outside.c",
        "k27invalid_break.c", "k27invalid_continue.c", "k27missing_break_return.c",
        "k27missing_continue_return.c", "k27capacity.c")) {
        Move-Item (Join-Path $target $sourceName) (Join-Path $testDirectory $sourceName) -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $target "out") | Out-Null
}

function Stage-Phase27LProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27lFixtureDirectory $target -Recurse -Force
    $testDirectory = Join-Path $target "tests"
    New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
    foreach ($sourceName in @(
        "l27zero.c", "l27one.c", "l27multi.c", "l27four.c", "l27nested.c", "l27expr.c",
        "l27condition.c", "l27loop.c", "l27if.c", "l27control.c", "l27forward.c", "l27backward.c",
        "l27isolation.c", "l27param.c", "l27entry.c", "l27missing.c", "l27duplicate_param.c",
        "l27duplicate_function.c", "l27param_limit.c", "l27arg_count.c", "l27unknown.c", "l27recursion.c")) {
        Move-Item (Join-Path $target $sourceName) (Join-Path $testDirectory $sourceName) -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $target "out") | Out-Null
}

function Stage-Phase27MProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27mFixtureDirectory $target -Recurse -Force
    $testDirectory = Join-Path $target "tests"
    New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
    foreach ($sourceName in @(
        "m27recursive.c", "m27local.c", "m27param.c", "m27control.c", "m27loop.c",
        "m27nested.c", "m27expression.c", "m27mutual.c", "m27boundary.c",
        "m27overboundary.c", "m27deep.c")) {
        Move-Item (Join-Path $target $sourceName) (Join-Path $testDirectory $sourceName) -Force
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $target "out") | Out-Null
}

function Stage-Phase27NProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27nFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27OProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27oFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27PProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27pFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27QProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27qFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27RProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27rFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27SProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27sFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27TProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27tFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27UProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27uFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27VProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27vFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27WProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27wFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27XProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27xFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27YProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27yFixtureDirectory $target -Recurse -Force
}

function Stage-Phase27ZProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase27zFixtureDirectory $target -Recurse -Force
}

function Stage-Phase28AProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase28aFixtureDirectory $target -Recurse -Force
}

function Stage-Phase28BProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase28bFixtureDirectory $target -Recurse -Force
}

function Stage-Phase28CProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase28cFixtureDirectory $target -Recurse -Force
}

function Stage-Phase28DProject([string]$target) {
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    Copy-Item $phase28dFixtureDirectory $target -Recurse -Force
}

function Invoke-QemuProofBoot([int]$runNumber, [string]$qemu) {
    $serialPath = Join-Path $tempDirectory ("boot{0}.serial.log" -f $runNumber)
    $stderrPath = Join-Path $tempDirectory ("boot{0}.stderr.log" -f $runNumber)
    $qemuArguments = @(
        "-machine", "pc,usb=off",
        "-drive", "if=pflash,format=raw,readonly=on,file=$ovmfCodePath",
        "-drive", "file=fat:rw:$activeEspDirectory,format=raw,if=ide,index=0",
        "-m", "1024M",
        "-vga", "std",
        "-serial", "file:$serialPath",
        "-display", "none",
        "-no-reboot",
        "-no-shutdown"
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $qemu
    $startInfo.Arguments = (($qemuArguments | ForEach-Object { Quote-ProcessArgument $_ }) -join " ")
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (!$process.Start()) { throw "QEMU did not start for boot $runNumber" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)

    try {
        while (!$process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (Test-Path -LiteralPath $serialPath) {
                try {
                    $serialProbe = Get-Content -LiteralPath $serialPath -Raw -ErrorAction Stop
                    if ($serialProbe -and $serialProbe.Contains("[KERNEL] Entering main loop (waiting for input)...")) {
                        $process.Kill()
                        break
                    }
                } catch [System.IO.IOException] {
                    # QEMU may hold the serial file during a flush; the next
                    # poll will retry without affecting the proof result.
                }
            }
        }
        if (!$process.HasExited) {
            Start-Sleep -Milliseconds 750
            if (!$process.HasExited) { $process.Kill() }
        }
        $process.WaitForExit()
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        $serial = Read-SerialText $serialPath
        if ($stderr) { [System.IO.File]::WriteAllText($stderrPath, $stderr) }

        $requiredMarkers = @(
            "Compiler: Phase 27B smoke PASS",
            "phase27c_compile42=PASS",
            "phase27c_execute42=PASS",
            "phase27c_compile41=PASS",
            "phase27c_execute41=PASS",
            "phase27c_repeat_execution=PASS",
            "phase27c_invalid_elf=PASS",
            "phase27c_alternate_build_run=PASS",
            "phase27c_kernel_survival=PASS",
            "phase27c=PASS",
            "ELF Loader: Phase 27C smoke PASS",
            "NativeElf host log: Hello from guideXOS!",
            "NativeElf host log: Developer Studio native build works!",
            "phase27d_dedicated_stack=PASS",
            "phase27d_app_context=PASS",
            "phase27d_host_log=PASS",
            "phase27d_source_driven_host_call=PASS",
            "phase27d_return_value=PASS",
            "phase27d_repeat_lifecycle=PASS",
            "phase27d_host_call_validation=PASS",
            "phase27d_kernel_survival=PASS",
            "phase27d=PASS",
            "ELF Loader: Phase 27D smoke PASS"
        )
        if ($Phase27VOnly -or $Phase27WOnly -or $Phase27XOnly -or $Phase27YOnly -or $Phase27ZOnly -or $Phase28AOnly -or $Phase28BOnly -or $Phase28COnly -or $Phase28DOnly) { $requiredMarkers = @() }
        if ($Phase27E -or $Phase27F) {
            $requiredMarkers += @(
                "phase27e_build_backend=PASS",
                "phase27e_ide_build=PASS",
                "phase27e_source_edit_build=PASS",
                "phase27e_ide_diagnostics=PASS",
                "phase27e_rebuild_after_failure=PASS",
                "phase27e_kernel_survival=PASS",
                "phase27e=PASS",
                "phase27e_app_launch=PASS",
                "ELF Loader: Phase 27E smoke PASS"
            )
        }
        if ($Phase27F) {
            $requiredMarkers += @(
                "phase27f_run_backend=PASS",
                "phase27f_ide_run=PASS",
                "phase27f_source_edit_run=PASS",
                "phase27f_build_failure_blocks_run=PASS",
                "phase27f_recovery=PASS",
                "phase27f_run_recovery=PASS",
                "phase27f_repeat=PASS",
                "phase27f_repeat_run=PASS",
                "phase27f_output_isolation=PASS",
                "phase27f_exit_code=PASS",
                "phase27f_artifact_identity=PASS",
                "phase27f_kernel_survival=PASS",
                "phase27f=PASS",
                "phase27f_app_launch=PASS",
                "ELF Loader: Phase 27F smoke PASS"
            )
        }
        if ($Phase27G) {
            $requiredMarkers += @(
                "phase27g_expression=PASS",
                "phase27g_locals=PASS",
                "phase27g_assignment=PASS",
                "phase27g_precedence=PASS",
                "phase27g_unary=PASS",
                "phase27g_multiple_host_calls=PASS",
                "phase27g_ide_program=PASS",
                "phase27g_source_edit=PASS",
                "phase27g_unknown_identifier=PASS",
                "phase27g_duplicate_local=PASS",
                "phase27g_failure_recovery=PASS",
                "phase27g_deterministic=PASS",
                "phase27g_kernel_survival=PASS",
                "phase27g=PASS",
                "ELF Loader: Phase 27G bootstrap language smoke PASS"
            )
        }
        if ($Phase27H) {
            $requiredMarkers += @(
                "phase27h_equality=PASS",
                "phase27h_comparisons=PASS",
                "phase27h_if=PASS",
                "phase27h_branch_suppression=PASS",
                "phase27h_if_else=PASS",
                "phase27h_else_branch=PASS",
                "phase27h_nested_if=PASS",
                "phase27h_truthiness=PASS",
                "phase27h_branch_assignment=PASS",
                "phase27h_missing_return=PASS",
                "phase27h_invalid_condition=PASS",
                "phase27h_artifact=PASS",
                "phase27h_ide_program=PASS",
                "phase27h_source_edit=PASS",
                "phase27h_failure_recovery=PASS",
                "phase27h_deterministic=PASS",
                "phase27h_kernel_survival=PASS",
                "phase27h=PASS",
                "ELF Loader: Phase 27H bootstrap language smoke PASS"
            )
        }
        if ($Phase27I) {
            $requiredMarkers += @(
                "phase27i_and_truth_table=PASS",
                "phase27i_or_truth_table=PASS",
                "phase27i_canonical_boolean=PASS",
                "phase27i_precedence=PASS",
                "phase27i_and_if=PASS",
                "phase27i_or_if=PASS",
                "phase27i_mixed_logical=PASS",
                "phase27i_nested_logical=PASS",
                "phase27i_logical_assignment=PASS",
                "phase27i_ide_program=PASS",
                "phase27i_source_edit=PASS",
                "phase27i_short_circuit_and=PASS",
                "phase27i_short_circuit_or=PASS",
                "phase27i_invalid_logical=PASS",
                "phase27i_single_operator_rejection=PASS",
                "phase27i_failure_recovery=PASS",
                "phase27i_deterministic=PASS",
                "phase27i_kernel_survival=PASS",
                "phase27i=PASS",
                "ELF Loader: Phase 27I short-circuit logical-operator smoke PASS"
            )
        }
        if ($Phase27J) {
            $requiredMarkers += @(
                "phase27j_basic_while=PASS",
                "phase27j_sum_loop=PASS",
                "phase27j_zero_iteration=PASS",
                "phase27j_condition_reevaluation=PASS",
                "phase27j_logical_condition=PASS",
                "phase27j_if_inside_while=PASS",
                "phase27j_while_inside_if=PASS",
                "phase27j_nested_while=PASS",
                "phase27j_loop_body_declaration=PASS",
                "phase27j_loop_host_calls=PASS",
                "phase27j_runtime_state=PASS",
                "phase27j_return_inside_loop=PASS",
                "phase27j_missing_return=PASS",
                "phase27j_ide_program=PASS",
                "phase27j_source_edit=PASS",
                "phase27j_invalid_while=PASS",
                "phase27j_failure_recovery=PASS",
                "phase27j_backward_branch=PASS",
                "phase27j_deterministic=PASS",
                "phase27j_kernel_survival=PASS",
                "phase27j=PASS",
                "ELF Loader: Phase 27J while-loop smoke PASS"
            )
        }
        if ($Phase27K) {
            $requiredMarkers += @(
                "phase27k_break_basic=PASS",
                "phase27k_continue_basic=PASS",
                "phase27k_break_inside_if=PASS",
                "phase27k_continue_inside_if=PASS",
                "phase27k_break_continue=PASS",
                "phase27k_continue_skips_tail=PASS",
                "phase27k_break_skips_tail=PASS",
                "phase27k_nested_break=PASS",
                "phase27k_nested_continue=PASS",
                "phase27k_continue_host_calls=PASS",
                "phase27k_break_host_calls=PASS",
                "phase27k_break_target=PASS",
                "phase27k_continue_target=PASS",
                "phase27k_innermost_targeting=PASS",
                "phase27k_break_outside_loop=PASS",
                "phase27k_continue_outside_loop=PASS",
                "phase27k_invalid_syntax=PASS",
                "phase27k_loop_stack_reset=PASS",
                "phase27k_ide_program=PASS",
                "phase27k_source_edit=PASS",
                "phase27k_failure_recovery=PASS",
                "phase27k_deterministic=PASS",
                "phase27k_kernel_survival=PASS",
                "phase27k=PASS",
                "ELF Loader: Phase 27K break/continue smoke PASS"
            )
        }
        if ($Phase27L) {
            $requiredMarkers += @(
                "phase27l_zero_arg_function=PASS",
                "phase27l_one_arg_function=PASS",
                "phase27l_multi_arg_function=PASS",
                "phase27l_four_arg_function=PASS",
                "phase27l_nested_calls=PASS",
                "phase27l_call_expression=PASS",
                "phase27l_call_condition=PASS",
                "phase27l_function_with_loop=PASS",
                "phase27l_function_with_if=PASS",
                "phase27l_function_loop_control=PASS",
                "phase27l_forward_call=PASS",
                "phase27l_backward_call=PASS",
                "phase27l_call_opcode=PASS",
                "phase27l_local_isolation=PASS",
                "phase27l_parameter_isolation=PASS",
                "phase27l_function_missing_return=PASS",
                "phase27l_duplicate_parameter=PASS",
                "phase27l_duplicate_function=PASS",
                "phase27l_parameter_limit=PASS",
                "phase27l_argument_count=PASS",
                "phase27l_unknown_function=PASS",
                "phase27l_recursion_accepted=PASS",
                "phase27l_gx_main_entry=PASS",
                "phase27l_host_integration=PASS",
                "phase27l_ide_program=PASS",
                "phase27l_source_edit=PASS",
                "phase27l_failure_recovery=PASS",
                "phase27l_deterministic=PASS",
                "phase27l_kernel_survival=PASS",
                "phase27l=PASS",
                "ELF Loader: Phase 27L user functions smoke PASS"
            )
        }
        if ($Phase27M) {
            $requiredMarkers += @(
                "phase27m_recursion_policy_migrated=PASS",
                "phase27m_direct_recursion=PASS",
                "phase27m_recursive_local_isolation=PASS",
                "phase27m_recursive_parameter_isolation=PASS",
                "phase27m_mutual_recursion=PASS",
                "phase27m_mutual_recursion_rel32=PASS",
                "phase27m_mutual_rel32=PASS",
                "phase27m_recursive_control_flow=PASS",
                "phase27m_recursion_with_loop=PASS",
                "phase27m_recursive_nested_calls=PASS",
                "phase27m_recursive_call_expression=PASS",
                "phase27m_stack_accounting=PASS",
                "Compiler: stack_policy frame_bytes=576 transient_bytes=128 activation_bytes=760 max_depth=75 reserve_bytes=8192",
                "phase27m_call_guard_opcode=PASS",
                "phase27m_recursive_call_opcode=PASS",
                "phase27m_recursive_rel32=PASS",
                "phase27m_no_unbounded_unroll=PASS",
                "phase27m_no_recursion_unrolling=PASS",
                "phase27m_deterministic=PASS",
                "phase27m_depth_boundary=PASS",
                "phase27m_depth_exhaustion_safe=PASS",
                "phase27m_runtime_failure=PASS",
                "phase27m_propagation=PASS",
                "phase27m_diagnostic=PASS",
                "phase27m_depth_diagnostic=PASS",
                "phase27m_runtime_recovery=PASS",
                "phase27m_stack_recovery=PASS",
                "phase27m_repeat_recursion=PASS",
                "phase27m_artifact_evidence=PASS",
                "phase27m_ide_program=PASS",
                "phase27m_host_integration=PASS",
                "phase27m_source_edit=PASS",
                "phase27m_ide_depth_failure=PASS",
                "phase27m_ide_recovery=PASS",
                "phase27m_stack_bounds=PASS",
                "phase27m_kernel_survival=PASS",
                "phase27m_repeated_runs=PASS",
                "phase27m=PASS",
                "ELF Loader: Phase 27M recursion-safe call-stack hardening smoke PASS"
            )
        }
        if ($Phase27N) {
            $requiredMarkers += @(
                "phase27n_run_backend=PASS",
                "phase27n_project_open=PASS",
                "phase27n_source_enumeration=PASS",
                "phase27n_multi_file_documents=PASS",
                "phase27n_multi_file_compile=PASS",
                "phase27n_internal_link=PASS",
                "phase27n_execute_initial=PASS",
                "phase27n_source_edit=PASS",
                "phase27n_artifact_changed=PASS",
                "phase27n_source_failure=PASS",
                "phase27n_diagnostic_source=PASS",
                "phase27n_no_artifact_on_failure=PASS",
                "phase27n_undefined_external=PASS",
                "phase27n_arity_mismatch=PASS",
                "phase27n_duplicate_definition=PASS",
                "phase27n_translation_unit_isolation=PASS",
                "phase27n_cross_file_call=PASS",
                "phase27n_cross_file_arguments=PASS",
                "phase27n_three_file_calls=PASS",
                "phase27n_cross_file_control_flow=PASS",
                "phase27n_cross_file_recursion=PASS",
                "phase27n_cross_file_mutual_recursion=PASS",
                "phase27n_recursive_call_guard=PASS",
                "phase27n_cross_file_call_guard=PASS",
                "phase27n_cross_file_depth_guard=PASS",
                "phase27n_signature_mismatch=PASS",
                "phase27n_undefined_symbol=PASS",
                "phase27n_prototype_arity=PASS",
                "phase27n_missing_entry=PASS",
                "phase27n_duplicate_entry=PASS",
                "phase27n_linked_entry=PASS",
                "phase27n_artifact_evidence=PASS",
                "phase27n_link_failure_blocks_run=PASS",
                "phase27n_multifile_diagnostics=PASS",
                "phase27n_compile_recovery=PASS",
                "phase27n_link_recovery=PASS",
                "phase27n_ide_multifile=PASS",
                "phase27n_cross_file_edit=PASS",
                "phase27n_ide_compile_diagnostic=PASS",
                "phase27n_ide_link_diagnostic=PASS",
                "phase27n_single_file_regression=PASS",
                "phase27n_recursion_guard_regression=PASS",
                "phase27n_linked_call_opcode=PASS",
                "phase27n_no_source_concatenation=PASS",
                "phase27n_deterministic_link=PASS",
                "phase27n_order_independent_determinism=PASS",
                "phase27n_linker_reset=PASS",
                "phase27n_recovery=PASS",
                "phase27n_deterministic=PASS",
                "phase27n_app_launch=PASS",
                "phase27n_kernel_survival=PASS",
                "phase27n=PASS",
                "ELF Loader: Phase 27N smoke PASS"
            )
        }
        if ($Phase27O) {
            $requiredMarkers += @(
                "phase27o_run_backend=PASS",
                "phase27o_project_open=PASS",
                "phase27o_source_enumeration=PASS",
                "phase27o_multi_file_documents=PASS",
                "phase27o_app_launch=PASS",
                "phase27o_artifact_evidence=PASS",
                "phase27o_ide_globals=PASS",
                "phase27o_global_source_edit=PASS",
                "phase27o_global_read=PASS",
                "phase27o_global_write=PASS",
                "phase27o_zero_initialized_global=PASS",
                "phase27o_initialized_global=PASS",
                "phase27o_runtime_global_store=PASS",
                "phase27o_cross_file_global_read=PASS",
                "phase27o_cross_file_global_write=PASS",
                "phase27o_cross_file_function_global=PASS",
                "phase27o_shared_global_state=PASS",
                "phase27o_global_loop_state=PASS",
                "phase27o_global_condition=PASS",
                "phase27o_global_recursion=PASS",
                "phase27o_global_depth_failure_recovery=PASS",
                "phase27o_global_reinitialization=PASS",
                "phase27o_invalid_initializer=PASS",
                "phase27o_duplicate_global=PASS",
                "phase27o_undefined_global=PASS",
                "phase27o_symbol_kind_conflict=PASS",
                "phase27o_local_shadows_global=PASS",
                "phase27o_global_relocation=PASS",
                "phase27o_global_address=PASS",
                "phase27o_rw_data_segment=PASS",
                "phase27o_rx_code_segment=PASS",
                "phase27o_no_rwx_segment=PASS",
                "phase27o_segment_permissions=PASS",
                "phase27o_rodata_regression=PASS",
                "phase27o_ide_undefined_global=PASS",
                "phase27o_ide_duplicate_global=PASS",
                "phase27o_compile_recovery=PASS",
                "phase27o_link_recovery=PASS",
                "phase27o_failure_blocks_run=PASS",
                "phase27o_post_failure_global_reset=PASS",
                "phase27o_linker_data_reset=PASS",
                "phase27o_single_file_regression=PASS",
                "phase27o_function_link_regression=PASS",
                "phase27o_data_deterministic=PASS",
                "phase27o_data_order_independent=PASS",
                "phase27o_kernel_survival=PASS",
                "phase27o=PASS",
                "ELF Loader: Phase 27O cross-file global data smoke PASS"
            )
        }
        if ($Phase27P) {
            $requiredMarkers += @(
                "phase27p_object_format=PASS",
                "phase27p_persistent_objects=PASS",
                "phase27p_reopen=PASS",
                "phase27p_noop_build=PASS",
                "phase27p_single_source_edit=PASS",
                "phase27p_global_initializer=PASS",
                "phase27p_source_restore=PASS",
                "phase27p_restore=PASS",
                "phase27p_stale_symbol=PASS",
                "phase27p_failed_build_preserves=PASS",
                "phase27p_recovery=PASS",
                "phase27p_missing_object=PASS",
                "phase27p_corrupt_object=PASS",
                "phase27p_metadata_corruption=PASS",
                "phase27p_compile_failure_preserves=PASS",
                "phase27p_service_recreation=PASS",
                "phase27p_object_deterministic=PASS",
                "phase27p_native_execution=PASS",
                "phase27p_app_launch=PASS",
                "phase27p_artifact_evidence=PASS",
                "phase27p_kernel_survival=PASS",
                "phase27p=PASS",
                "ELF Loader: Phase 27P persistent object smoke PASS"
            )
        }
        if ($Phase27Q) {
            $requiredMarkers += @(
                "phase27q_run_backend=PASS",
                "phase27q_project_open=PASS",
                "phase27q_source_enumeration=PASS",
                "phase27q_multi_file_documents=PASS",
                "phase27q_ide_cold_array=PASS",
                "phase27q_ide_warm_array=PASS",
                "phase27q_ide_partial_array=PASS",
                "phase27q_ide_bounds_failure=PASS",
                "phase27q_array_failure_blocks_run=PASS",
                "phase27q_bounds_recovery=PASS",
                "phase27q_runtime_status_reset=PASS",
                "phase27q_post_failure_array_reset=PASS",
                "phase27q_local_array=PASS",
                "phase27q_array_loop=PASS",
                "phase27q_dynamic_store=PASS",
                "phase27q_local_array_isolation=PASS",
                "phase27q_recursive_local_array=PASS",
                "phase27q_array_recursion_guard=PASS",
                "phase27q_global_array=PASS",
                "phase27q_cross_file_array=PASS",
                "phase27q_array_signature_mismatch=PASS",
                "phase27q_scalar_array_conflict=PASS",
                "phase27q_cached_array=PASS",
                "phase27q_array_incremental_edit=PASS",
                "phase27q_array_reinitialization=PASS",
                "phase27q_bounds_failure=PASS",
                "phase27q_array_length_validation=PASS",
                "phase27q_array_requires_index=PASS",
                "phase27q_constant_oob_rejected=PASS",
                "phase27q_array_parameter_rejected=PASS",
                "phase27q_array_assignment_rejected=PASS",
                "phase27q_indexed_addressing=PASS",
                "phase27q_indexed_load_opcode=PASS",
                "phase27q_indexed_store_opcode=PASS",
                "phase27q_bounds_guard_opcode=PASS",
                "phase27q_scaled_index_opcode=PASS",
                "phase27q_global_bounds_failure=PASS",
                "phase27q_negative_index_guard=PASS",
                "phase27q_upper_index_guard=PASS",
                "phase27q_last_index_valid=PASS",
                "phase27q_zero_index_valid=PASS",
                "phase27q_nested_bounds_failure=PASS",
                "phase27q_runtime_status_reset=PASS",
                "phase27q_old_object_invalidated=PASS",
                "phase27q_shared_array_storage=PASS",
                "phase27q_array_object_roundtrip=PASS",
                "phase27q_cached_global_array=PASS",
                "phase27q_cached_local_array=PASS",
                "phase27q_cached_array_signature_validation=PASS",
                "phase27q_array_object_deterministic=PASS",
                "phase27q_array_cold_warm_identical=PASS",
                "phase27q_array_relocation=PASS",
                "phase27q_array_rw_segment=PASS",
                "phase27q_no_rwx_regression=PASS",
                "phase27q_scalar_global_regression=PASS",
                "phase27q_array_linker_reset=PASS",
                "phase27q_artifact_evidence=PASS",
                "phase27q_app_launch=PASS",
                "phase27q_kernel_survival=PASS",
                "phase27q=PASS",
                "ELF Loader: Phase 27Q bounded array smoke PASS"
            )
        }
        if ($Phase27R) {
            $requiredMarkers += @(
                "phase27r_address_local=PASS",
                "phase27r_local_pointer_write=PASS",
                "phase27r_address_global=PASS",
                "phase27r_address_array_element=PASS",
                "phase27r_dynamic_element_address=PASS",
                "phase27r_oob_address_rejected=PASS",
                "phase27r_pointer_copy=PASS",
                "phase27r_pointer_assignment=PASS",
                "phase27r_pointer_type_mismatch=PASS",
                "phase27r_array_decay=PASS",
                "phase27r_pointer_parameter=PASS",
                "phase27r_pointer_argument_alias=PASS",
                "phase27r_cross_function_pointer=PASS",
                "phase27r_cross_file_global_pointer=PASS",
                "phase27r_cross_file_pointer_parameter=PASS",
                "phase27r_pointer_signature_mismatch=PASS",
                "phase27r_recursive_local_pointer=PASS",
                "phase27r_global_pointer_rejected=PASS",
                "phase27r_invalid_address_of=PASS",
                "phase27r_nonpointer_dereference=PASS",
                "phase27r_pointer_arithmetic_supported=PASS",
                "phase27r_integer_pointer_cast_rejected=PASS",
                "phase27r_invalid_pointer_runtime=PASS",
                "phase27r_uninitialized_pointer=PASS",
                "phase27r_dereference_load_opcode=PASS",
                "phase27r_dereference_store_opcode=PASS",
                "phase27r_address_local_opcode=PASS",
                "phase27r_address_global_relocation=PASS",
                "phase27r_address_indexed_opcode=PASS",
                "phase27r_pointer_call_guard=PASS",
                "phase27r_cross_file_pointer_relocation=PASS",
                "phase27r_old_object_invalidated=PASS",
                "phase27r_pointer_object_roundtrip=PASS",
                "phase27r_cached_pointer=PASS",
                "phase27r_pointer_incremental_edit=PASS",
                "phase27r_cached_pointer_signature=PASS",
                "phase27r_pointer_object_deterministic=PASS",
                "phase27r_pointer_cold_warm_identical=PASS",
                "phase27r_pointer_failure_recovery=PASS",
                "phase27r_runtime_status_reset=PASS",
                "phase27r_pointer_global_reinitialization=PASS",
                "phase27r_no_rwx_regression=PASS",
                "phase27r_ide_cold_pointer=PASS",
                "phase27r_ide_warm_pointer=PASS",
                "phase27r_ide_partial_pointer=PASS",
                "phase27r_ide_signature_failure=PASS",
                "phase27r_ide_invalid_pointer=PASS",
                "phase27r_pointer_failure_blocks_run=PASS",
                "phase27r_pointer_linker_reset=PASS",
                "phase27r_kernel_survival=PASS",
                "phase27r=PASS",
                "ELF Loader: Phase 27R typed pointer smoke PASS"
            )
        }
        if ($Phase27S) {
            $requiredMarkers += @(
                "phase27s_one_past_representation=PASS",
                "phase27s_beyond_one_past_rejected=PASS",
                "phase27s_before_begin_rejected=PASS",
                "phase27s_address_of_element=PASS",
                "phase27s_scalar_pointer_extent=PASS",
                "phase27s_pointer_bounds_failure=PASS",
                "phase27s_one_past_deref_rejected=PASS",
                "phase27s_pointer_array_walk=PASS",
                "phase27s_pointer_store_walk=PASS",
                "phase27s_pointer_retreat=PASS",
                "phase27s_middle_element_provenance=PASS",
                "phase27s_pointer_equality=PASS",
                "phase27s_pointer_copy=PASS",
                "phase27s_pointer_parameter_walk=PASS",
                "phase27s_pointer_parameter_isolation=PASS",
                "phase27s_cross_file_pointer_walk=PASS",
                "phase27s_pointer_signature_validation=PASS",
                "phase27s_cached_pointer_signature=PASS",
                "phase27s_pointer_object_roundtrip=PASS",
                "phase27s_cached_pointer_execution=PASS",
                "phase27s_pointer_incremental_edit=PASS",
                "phase27s_cold_warm_identical=PASS",
                "phase27s_pointer_object_deterministic=PASS",
                "phase27s_pointer_arithmetic_opcode=PASS",
                "phase27s_deref_guard_opcode=PASS",
                "phase27s_no_raw_pointer_escape=PASS",
                "phase27s_pointer_integer_type_safety=PASS",
                "phase27s_pointer_add_pointer_rejected=PASS",
                "phase27s_address_of_rvalue_rejected=PASS",
                "phase27s_offset_pointer_store=PASS",
                "phase27s_pointer_loop=PASS",
                "phase27s_pointer_condition=PASS",
                "phase27s_pointer_recursion=PASS",
                "phase27s_pointer_recursion_guard=PASS",
                "phase27s_runtime_status_recovery=PASS",
                "phase27s_pointer_failure_recovery=PASS",
                "phase27s_global_array_pointer=PASS",
                "phase27s_cross_file_global_pointer=PASS",
                "phase27s_pointer_global_relocation=PASS",
                "phase27s_provenance_isolation=PASS",
                "phase27s_descriptor_integrity=PASS",
                "phase27s_adjacent_object_escape_rejected=PASS",
                "phase27s_scalar_adjacent_deref_rejected=PASS",
                "phase27s_array_decay=PASS",
                "phase27s_ide_cold_pointer=PASS",
                "phase27s_ide_warm_pointer=PASS",
                "phase27s_ide_partial_pointer=PASS",
                "phase27s_ide_pointer_failure=PASS",
                "phase27s_pointer_failure_blocks_run=PASS",
                "phase27s_pointer_linker_reset=PASS",
                "phase27s_no_rwx_regression=PASS",
                "phase27s_kernel_survival=PASS",
                "phase27s=PASS",
                "ELF Loader: Phase 27S provenance-preserving pointer arithmetic smoke PASS"
            )
        }
        if ($Phase27T) {
            $requiredMarkers += @(
                "phase27t_duplicate_field=PASS",
                "phase27t_duplicate_struct=PASS",
                "phase27t_local_struct=PASS",
                "phase27t_global_struct=PASS",
                "phase27t_field_store=PASS",
                "phase27t_arrow_access=PASS",
                "phase27t_field_subobject_provenance=PASS",
                "phase27t_adjacent_field_escape_rejected=PASS",
                "phase27t_field_pointer=PASS",
                "phase27t_struct_address_of=PASS",
                "phase27t_struct_pointer_parameter=PASS",
                "phase27t_struct_pointer_parameter_isolation=PASS",
                "phase27t_struct_pointer_type_safety=PASS",
                "phase27t_cross_file_struct_pointer=PASS",
                "phase27t_struct_signature_mismatch=PASS",
                "phase27t_field_order_mismatch=PASS",
                "phase27t_unknown_field=PASS",
                "phase27t_dot_type_error=PASS",
                "phase27t_arrow_type_error=PASS",
                "phase27t_struct_assignment_rejected=PASS",
                "phase27t_struct_by_value_parameter_rejected=PASS",
                "phase27t_struct_pointer=PASS",
                "phase27t_arrow_store=PASS",
                "phase27t_struct_reinitialization=PASS",
                "phase27t_field_addressing=PASS",
                "phase27t_field_load_opcode=PASS",
                "phase27t_field_store_opcode=PASS",
                "phase27t_arrow_guard_opcode=PASS",
                "phase27t_struct_object_roundtrip=PASS",
                "phase27t_object_version_migration=PASS",
                "phase27t_cached_struct_execution=PASS",
                "phase27t_struct_incremental_edit=PASS",
                "phase27t_cached_struct_signature_validation=PASS",
                "phase27t_struct_object_deterministic=PASS",
                "phase27t_struct_cold_warm_identical=PASS",
                "phase27t_struct_layout_deterministic=PASS",
                "phase27t_ide_cold_struct=PASS",
                "phase27t_ide_warm_struct=PASS",
                "phase27t_ide_partial_struct=PASS",
                "phase27t_ide_struct_type_failure=PASS",
                "phase27t_struct_failure_blocks_run=PASS",
                "phase27t_field_pointer_failure_recovery=PASS",
                "phase27t_struct_pointer_recursion=PASS",
                "phase27t_struct_recursion_guard=PASS",
                "phase27t_recursion_stack_accounting=PASS",
                "phase27t_runtime_status_recovery=PASS",
                "phase27t_struct_linker_reset=PASS",
                "phase27t_no_rwx_regression=PASS",
                "phase27t_kernel_survival=PASS",
                "phase27t=PASS",
                "ELF Loader: Phase 27T structs and field addressing smoke PASS"
            )
        }
        if ($Phase27U) {
            $requiredMarkers += @(
                "phase27u_struct_array_layout=PASS",
                "phase27u_indexed_fields=PASS",
                "phase27u_struct_pointer=PASS",
                "phase27u_pointer_scaling=PASS",
                "phase27u_arrow_load_store=PASS",
                "phase27u_element_isolation=PASS",
                "phase27u_address_equivalence=PASS",
                "phase27u_nested_field_address=PASS",
                "phase27u_global_struct_array=PASS",
                "phase27u_cross_file_struct_pointer=PASS",
                "phase27u_object_reopen=PASS",
                "phase27u_incremental_reuse=PASS",
                "phase27u_incremental_edit=PASS",
                "phase27u_failed_rebuild_recovery=PASS",
                "phase27u_object_deterministic=PASS",
                "phase27u_native_execution=PASS",
                "phase27u_artifact=PASS",
                "phase27u=PASS",
                "DEVELOPER_STUDIO_PHASE27U_PASS",
                "ELF Loader: Phase 27U arrays of structs and struct-pointer traversal smoke PASS"
            )
        }
        if ($Phase27V) {
            $requiredMarkers += @(
                "phase27v_shared_declarations=PASS",
                "phase27v_header_dependencies=PASS",
                "phase27v_shared_struct_identity=PASS",
                "phase27v_selective_invalidation=PASS",
                "phase27v_native_execution=PASS",
                "phase27v_external_abi_validation=PASS",
                "phase27v_type_mismatch_rejection=PASS",
                "phase27v_object_reopen=PASS",
                "phase27v_build_service_recreation=PASS",
                "phase27v_missing_header=PASS",
                "phase27v_malformed_header=PASS",
                "phase27v_corrupt_metadata_recovery=PASS",
                "phase27v_include_cycle=PASS",
                "phase27v_path_validation=PASS",
                "phase27v=PASS",
                "DEVELOPER_STUDIO_PHASE27V_PASS",
                "ELF Loader: Phase 27V shared declarations and dependency smoke PASS"
            )
        }
        if ($Phase27W) {
            $requiredMarkers += @(
                "phase27w_build_pass=PASS",
                "phase27w_deploy_pass=PASS",
                "phase27w_launch_pass=PASS",
                "phase27w_render_pass=PASS",
                "phase27w_close_pass=PASS",
                "phase27w_cleanup_pass=PASS",
                "phase27w_already_running_rejected=PASS",
                "phase27w_rerun_pass=PASS",
                "phase27w_changed_artifact_pass=PASS",
                "phase27w_stale_block_pass=PASS",
                "phase27w_link_failure_stale_block_pass=PASS",
                "phase27w_missing_artifact_rejected=PASS",
                "phase27w_launch_failure_cleanup_pass=PASS",
                "phase27w_negative_pass=PASS",
                "phase27w_artifact=PASS",
                "phase27w=PASS",
                "DEVELOPER_STUDIO_PHASE27W_BEGIN",
                "DEVELOPER_STUDIO_PHASE27W_BUILD_PASS",
                "DEVELOPER_STUDIO_PHASE27W_DEPLOY_PASS",
                "DEVELOPER_STUDIO_PHASE27W_LAUNCH_PASS",
                "DEVELOPER_STUDIO_PHASE27W_RENDER_PASS",
                "DEVELOPER_STUDIO_PHASE27W_CLOSE_PASS",
                "DEVELOPER_STUDIO_PHASE27W_CLEANUP_PASS",
                "DEVELOPER_STUDIO_PHASE27W_RERUN_PASS",
                "DEVELOPER_STUDIO_PHASE27W_STALE_BLOCK_PASS",
                "DEVELOPER_STUDIO_PHASE27W_NEGATIVE_PASS",
                "DEVELOPER_STUDIO_PHASE27W_PASS",
                "ELF Loader: Phase 27W Run Project smoke PASS"
            )
        }
        if ($Phase27X) {
            $requiredMarkers += @(
                "DEVELOPER_STUDIO_PHASE27X_BEGIN",
                "DEVELOPER_STUDIO_PHASE27X_BUILD_PASS",
                "DEVELOPER_STUDIO_PHASE27X_DEPLOY_PASS",
                "DEVELOPER_STUDIO_PHASE27X_APP_CREATE_PASS",
                "DEVELOPER_STUDIO_PHASE27X_WINDOW_CREATE_PASS",
                "DEVELOPER_STUDIO_PHASE27X_RENDER_27_PASS",
                "DEVELOPER_STUDIO_PHASE27X_RUNNING_PASS",
                "DEVELOPER_STUDIO_PHASE27X_CLOSE_PASS",
                "DEVELOPER_STUDIO_PHASE27X_CLEANUP_PASS",
                "DEVELOPER_STUDIO_PHASE27X_REBUILD_PASS",
                "DEVELOPER_STUDIO_PHASE27X_RENDER_28_PASS",
                "DEVELOPER_STUDIO_PHASE27X_RERUN_PASS",
                "DEVELOPER_STUDIO_PHASE27X_STALE_BLOCK_PASS",
                "DEVELOPER_STUDIO_PHASE27X_NEGATIVE_PASS",
                "DEVELOPER_STUDIO_PHASE27X_PASS"
            )
        }
        if ($Phase27Y) {
            $requiredMarkers += @(
                "DEVELOPER_STUDIO_PHASE27Y_BEGIN",
                "DEVELOPER_STUDIO_PHASE27Y_BUILD_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_DEPLOY_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_START_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_START_RETURNED",
                "DEVELOPER_STUDIO_PHASE27Y_RUNNING_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_RENDER_27_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_OWNER_ACTIVE_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_BUSY_REJECT_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_CLOSE_REQUEST_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_COMPLETION_BEFORE_POLL_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_CLOSE_COMPLETE_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_CLEANUP_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_REBUILD_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_RENDER_28_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_STALE_HANDLE_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_RERUN_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_CANCEL_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_STALE_BUILD_BLOCK_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_NEGATIVE_PASS",
                "DEVELOPER_STUDIO_PHASE27Y_PASS"
            )
        }
        if ($Phase27Z) {
            $requiredMarkers += @(
                "DEVELOPER_STUDIO_PHASE27Z_BEGIN",
                "DEVELOPER_STUDIO_PHASE27Z_BUILD_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_STALE_ARTIFACT_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_MISSING_METADATA_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_NORMAL_RUN_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_DEBUG_START",
                "DEVELOPER_STUDIO_PHASE27Z_BREAKPOINT_INSTALLED",
                "DEVELOPER_STUDIO_PHASE27Z_BREAKPOINT_HIT",
                "DEVELOPER_STUDIO_PHASE27Z_RUN_PAUSED_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_ENTRY_IDENTITY_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_PRE_USER_CODE_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_NO_GUI_BEFORE_RESUME_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_OWNER_ACTIVE_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_STALE_IDENTITY_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_RESUME_GUI_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_RESUME_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_CANCEL_PAUSED_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_CLEANUP_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_POST_CANCEL_RUN_PASS",
                "DEVELOPER_STUDIO_PHASE27Z_PASS"
            )
        }
        if ($Phase28A) {
            $requiredMarkers += @(
                "DEVELOPER_STUDIO_PHASE28A_BEGIN",
                "DEVELOPER_STUDIO_PHASE28A_BUILD_PASS",
                "DEVELOPER_STUDIO_PHASE28A_PRE_BREAKPOINT_PASS",
                "DEVELOPER_STUDIO_PHASE28A_SNAPSHOT_PASS",
                "DEVELOPER_STUDIO_PHASE28A_NO_GUI_BEFORE_RESUME_PASS",
                "DEVELOPER_STUDIO_PHASE28A_RESUME_GUI_PASS",
                "DEVELOPER_STUDIO_PHASE28A_RESUME_PASS",
                "DEVELOPER_STUDIO_PHASE28A_CLEANUP_PASS",
                "DEVELOPER_STUDIO_PHASE28A_NEGATIVE_LINE_PASS",
                "DEVELOPER_STUDIO_PHASE28A_STALE_SOURCE_PASS",
                "phase28a_build_pass=PASS",
                "phase28a_negative_line_pass=PASS",
                "phase28a_stale_source_pass=PASS",
                "phase28a_normal_run_pass=PASS",
                "phase28a_debug_pass=PASS",
                "phase28a_artifact=PASS",
                "phase28a=PASS",
                "DEVELOPER_STUDIO_PHASE28A_PASS",
                "DEVELOPER_STUDIO_PHASE28A_RUN_REGRESSION_PASS",
                "ELF Loader: Phase 28A source breakpoint smoke PASS"
            )
        }
        if ($Phase28B) {
            $requiredMarkers += @(
                "DEVELOPER_STUDIO_PHASE28B_BEGIN",
                "DEVELOPER_STUDIO_PHASE28B_BUILD_PASS",
                "DEVELOPER_STUDIO_PHASE28B_SOURCE_BREAKPOINT_PASS",
                "DEVELOPER_STUDIO_PHASE28B_PAUSED_INITIAL_PASS",
                "DEVELOPER_STUDIO_PHASE28B_STEP_REQUEST_PASS",
                "DEVELOPER_STUDIO_PHASE28B_TF_SET_PASS",
                "DEVELOPER_STUDIO_PHASE28B_SINGLE_STEP_TRAP",
                "DEVELOPER_STUDIO_PHASE28B_PAUSED_AFTER_STEP_PASS",
                "DEVELOPER_STUDIO_PHASE28B_ONE_INSTRUCTION_PASS",
                "DEVELOPER_STUDIO_PHASE28B_OWNER_ACTIVE_PASS",
                "DEVELOPER_STUDIO_PHASE28B_TF_CLEAR_PASS",
                "DEVELOPER_STUDIO_PHASE28B_THREE_STEPS_PASS",
                "DEVELOPER_STUDIO_PHASE28B_RESUME_PASS",
                "DEVELOPER_STUDIO_PHASE28B_RENDER_PASS",
                "DEVELOPER_STUDIO_PHASE28B_CLOSE_PASS",
                "DEVELOPER_STUDIO_PHASE28B_CLEANUP_PASS",
                "DEVELOPER_STUDIO_PHASE28B_STALE_STEP_PASS",
                "DEVELOPER_STUDIO_PHASE28B_DEBUG_EXCEPTION_NEGATIVE_PASS",
                "DEVELOPER_STUDIO_PHASE28B_RUN_REGRESSION_PASS",
                "DEVELOPER_STUDIO_PHASE28B_WARM_CACHE_PASS",
                "phase28b_build_pass=PASS",
                "phase28b_debug_pass=PASS",
                "phase28b_three_steps_pass=PASS",
                "phase28b_run_pass=PASS",
                "phase28b=PASS",
                "DEVELOPER_STUDIO_PHASE28B_PASS",
                "ELF Loader: Phase 28B single-instruction step smoke PASS"
            )
        }
        if ($Phase28C) {
            $requiredMarkers += @(
                "DEVELOPER_STUDIO_PHASE28C_BEGIN",
                "DEVELOPER_STUDIO_PHASE28C_BUILD_PASS",
                "DEVELOPER_STUDIO_PHASE28C_BREAKPOINT_PASS",
                "DEVELOPER_STUDIO_PHASE28C_SOURCE_STEP_REQUEST_PASS",
                "DEVELOPER_STUDIO_PHASE28C_INTERNAL_STEP_PASS",
                "DEVELOPER_STUDIO_PHASE28C_NEXT_SOURCE_PASS",
                "DEVELOPER_STUDIO_PHASE28C_SAME_FUNCTION_PASS",
                "DEVELOPER_STUDIO_PHASE28C_THREE_SOURCE_STEPS_PASS",
                "DEVELOPER_STUDIO_PHASE28C_CALL_INTO_PASS",
                "DEVELOPER_STUDIO_PHASE28C_RUNTIME_BOUNDARY_PASS",
                "DEVELOPER_STUDIO_PHASE28C_OWNER_ACTIVE_PASS",
                "DEVELOPER_STUDIO_PHASE28C_RESUME_PASS",
                "DEVELOPER_STUDIO_PHASE28C_RENDER_PASS",
                "DEVELOPER_STUDIO_PHASE28C_CLOSE_PASS",
                "DEVELOPER_STUDIO_PHASE28C_CLEANUP_PASS",
                "DEVELOPER_STUDIO_PHASE28C_STALE_PASS",
                "DEVELOPER_STUDIO_PHASE28C_CANCEL_PASS",
                "DEVELOPER_STUDIO_PHASE28C_RUN_REGRESSION_PASS",
                "phase28c_build_pass=PASS",
                "phase28c_debug_pass=PASS",
                "phase28c_source_step_pass=PASS",
                "phase28c_run_pass=PASS",
                "phase28c=PASS",
                "DEVELOPER_STUDIO_PHASE28C_PASS",
                "ELF Loader: Phase 28C source-aware step smoke PASS"
            )
        }
        if ($Phase28D) {
            $requiredMarkers += @(
                "DEVELOPER_STUDIO_PHASE28D_BEGIN",
                "DEVELOPER_STUDIO_PHASE28D_BUILD_PASS",
                "DEVELOPER_STUDIO_PHASE28D_BREAKPOINT_PASS",
                "DEVELOPER_STUDIO_PHASE28D_STEP_INTO_CONTROL_PASS",
                "DEVELOPER_STUDIO_PHASE28D_STEP_INTO_PASS",
                "DEVELOPER_STUDIO_PHASE28D_STEP_OVER_REQUEST_PASS",
                "DEVELOPER_STUDIO_PHASE28D_CALL_CLASSIFY_PASS",
                "DEVELOPER_STUDIO_PHASE28D_RETURN_TARGET_PASS",
                "DEVELOPER_STUDIO_PHASE28D_TEMP_BREAKPOINT_PASS",
                "DEVELOPER_STUDIO_PHASE28D_CALLEE_EXECUTED_PASS",
                "DEVELOPER_STUDIO_PHASE28D_CALLEE_NOT_PAUSED_PASS",
                "DEVELOPER_STUDIO_PHASE28D_RETURN_TRAP",
                "DEVELOPER_STUDIO_PHASE28D_CALLER_FRAME_PASS",
                "DEVELOPER_STUDIO_PHASE28D_NEXT_SOURCE_PASS",
                "DEVELOPER_STUDIO_PHASE28D_THREE_SOURCE_STEPS_PASS",
                "DEVELOPER_STUDIO_PHASE28D_FALLBACK_PASS",
                "DEVELOPER_STUDIO_PHASE28D_RESUME_PASS",
                "DEVELOPER_STUDIO_PHASE28D_RENDER_PASS",
                "DEVELOPER_STUDIO_PHASE28D_CLOSE_PASS",
                "DEVELOPER_STUDIO_PHASE28D_CLEANUP_PASS",
                "DEVELOPER_STUDIO_PHASE28D_RUN_REGRESSION_PASS",
                "phase28d_build_pass=PASS",
                "phase28d_debug_pass=PASS",
                "phase28d_step_over_pass=PASS",
                "phase28d_run_pass=PASS",
                "phase28d=PASS",
                "DEVELOPER_STUDIO_PHASE28D_PASS",
                "ELF Loader: Phase 28D source-aware step over smoke PASS"
            )
        }
        $missingMarkers = @($requiredMarkers | Where-Object { $serial -notmatch [regex]::Escape($_) })
        if ($missingMarkers.Count -ne 0) {
            Write-Host "QEMU boot $runNumber missed required compiler/IDE markers: $($missingMarkers -join ', ')" -ForegroundColor Red
            if ($Phase27E -or $Phase27F) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase27e|phase27f|phase27g|phase27h|phase27i|phase27j|phase27k|phase27l|phase27m|phase27n|phase27o|phase27p|phase27q|phase27r|Phase 27E|Phase 27F|Phase 27G|Phase 27H|Phase 27I|Phase 27J|Phase 27K|Phase 27L|Phase 27M|Phase 27N|Phase 27O|Phase 27P|Phase 27Q|Phase 27R" } | ForEach-Object { Write-Host $_ }
            }
            if ($Phase27U) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase27u|Phase 27U|u27_|DEVELOPER_STUDIO_PHASE27U" } | ForEach-Object { Write-Host $_ }
            }
            if ($Phase27V) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase27v|Phase 27V|DEVELOPER_STUDIO_PHASE27V" } | ForEach-Object { Write-Host $_ }
            }
            if ($Phase27W) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase27w|Phase 27W|DEVELOPER_STUDIO_PHASE27W" } | ForEach-Object { Write-Host $_ }
            }
            if ($Phase27Z) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase27z|Phase 27Z|DEVELOPER_STUDIO_PHASE27Z" } | ForEach-Object { Write-Host $_ }
            }
            if ($Phase27X) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase27x|Phase 27X|DEVELOPER_STUDIO_PHASE27X" } | ForEach-Object { Write-Host $_ }
            }
            if ($Phase28A) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase28a|Phase 28A|DEVELOPER_STUDIO_PHASE28A" } | ForEach-Object { Write-Host $_ }
            }
            if ($Phase28B) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase28b|Phase 28B|DEVELOPER_STUDIO_PHASE28B" } | ForEach-Object { Write-Host $_ }
            }
            if ($Phase27Y) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase27y|Phase 27Y|DEVELOPER_STUDIO_PHASE27Y" } | ForEach-Object { Write-Host $_ }
            }
            if (-not $Phase27U -and $serial) { Write-Host $serial }
            if ($stderr) { Write-Host $stderr }
            throw "QEMU compiler/IDE proof failed on boot $runNumber (exit $($process.ExitCode))"
        }

        Write-Host "--- QEMU bare-metal compiler proof boot $runNumber ---" -ForegroundColor Cyan
        $serial -split "`r?`n" |
            Where-Object { $_ -notmatch "NativeElf: artifact_hex=" -and
                $_ -match "Compiler:|ELF Loader:|NativeElf:|phase27c|phase27d|phase27e|phase27f|phase27g|phase27h|phase27i|phase27j|phase27k|phase27l|phase27m|phase27n|phase27o|phase27p|phase27q|phase27r|phase27s|phase27w|phase27x|phase27y|phase27z|phase28a|phase28A|phase28b|Phase28B|^error:" } |
            ForEach-Object { Write-Host $_ }
    }
    finally {
        if (!$process.HasExited) { $process.Kill() }
        $process.Dispose()
        # The directory-backed FAT drive can release its last handle slightly
        # after QEMU exits. Let the host-side reset/restage below observe a
        # fully closed image before starting the next fresh boot.
        Start-Sleep -Milliseconds 1000
    }
}

try {
    if ($BootCount -lt 1) { throw "BootCount must be at least 1" }
    if (!(Test-Path $espDirectory)) { throw "ESP directory is missing: $espDirectory" }
    if (!(Test-Path $ovmfCodePath)) { throw "Repository OVMF firmware is missing" }
    New-Item -ItemType Directory -Force -Path $tempDirectory, $evidenceDirectory | Out-Null
    $qemu = Get-RequiredTool "qemu-system-x86_64" $qemuPath

    $makeFallback = "C:\mingw64\bin\mingw32-make.exe"
    $make = Get-RequiredTool "mingw32-make" $makeFallback
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path $vswhere)) { throw "Visual Studio vswhere.exe is missing" }
    $visualStudioPath = (& $vswhere -latest -property installationPath | Select-Object -First 1).Trim()
    $msbuild = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
    if (!(Test-Path $msbuild)) { throw "MSBuild is missing: $msbuild" }

    # The host toolchain only builds the kernel/bootloader test harness. It is
    # never called by the guest compiler while it reads and emits the ELF.
    if ($Phase27E -or $Phase27F) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27e.ps1"))) {
            throw "Developer Studio Phase 27E build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27e.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27E proof app build failed" }
    }
    if ($Phase27F) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27f.ps1"))) {
            throw "Developer Studio Phase 27F build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27f.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27F proof app build failed" }
    }
    if ($Phase27G) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27g.ps1"))) {
            throw "Developer Studio Phase 27G build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27g.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27G proof app build failed" }
    }
    if ($Phase27H) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27h.ps1"))) {
            throw "Developer Studio Phase 27H build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27h.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27H proof app build failed" }
    }
    if ($Phase27I) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27i.ps1"))) {
            throw "Developer Studio Phase 27I build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27i.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27I proof app build failed" }
    }
    if ($Phase27J) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27j.ps1"))) {
            throw "Developer Studio Phase 27J build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27j.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27J proof app build failed" }
    }
    if ($Phase27K) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27k.ps1"))) {
            throw "Developer Studio Phase 27K build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27k.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27K proof app build failed" }
    }
    if ($Phase27L) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27l.ps1"))) {
            throw "Developer Studio Phase 27L build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27l.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27L proof app build failed" }
    }
    if ($Phase27M) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27m.ps1"))) {
            throw "Developer Studio Phase 27M build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27m.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27M proof app build failed" }
    }
    if ($Phase27N) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27n.ps1"))) {
            throw "Developer Studio Phase 27N build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27n.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27N proof app build failed" }
    }
    if ($Phase27O) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27o.ps1"))) {
            throw "Developer Studio Phase 27O build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27o.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27O proof app build failed" }
    }
    if ($Phase27P) {
        Write-Host "Phase 27P proof application is built by the in-OS Developer Studio backend during guest execution."
    }
    if ($Phase27Q) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27q.ps1"))) {
            throw "Developer Studio Phase 27Q build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27q.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27Q proof app build failed" }
    }
    if ($Phase27R) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27r.ps1"))) {
            throw "Developer Studio Phase 27R build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27r.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27R proof app build failed" }
    }
    if ($Phase27S) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27s.ps1"))) {
            throw "Developer Studio Phase 27S build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27s.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27S proof app build failed" }
    }
    if ($Phase27T) {
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27t.ps1"))) {
            throw "Developer Studio Phase 27T build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27t.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27T proof app build failed" }
    }
    $env:EXTRA_CFLAGS = "-DGXOS_COMPILER_BOOTSTRAP_SMOKE_ACTIVE"
    if ($Phase27E -or $Phase27F) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27E_SMOKE" }
    if ($Phase27F) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27F_SMOKE" }
    if ($Phase27G) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27G_SMOKE" }
    if ($Phase27H) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27H_SMOKE" }
    if ($Phase27I) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27I_SMOKE" }
    if ($Phase27J) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27J_SMOKE" }
    if ($Phase27K) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27K_SMOKE" }
    if ($Phase27L) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27L_SMOKE" }
    if ($Phase27M) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27M_SMOKE" }
    if ($Phase27N) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27N_SMOKE" }
    if ($Phase27O) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27O_SMOKE" }
    if ($Phase27P) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27P_SMOKE" }
    if ($Phase27Q) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27Q_SMOKE" }
    if ($Phase27R) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27R_SMOKE" }
    if ($Phase27S) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27S_SMOKE" }
    if ($Phase27T) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27T_SMOKE" }
    if ($Phase27U) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27U_SMOKE" }
    if ($Phase27V) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27V_SMOKE" }
    if ($Phase27W) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27W_SMOKE" }
    if ($Phase27X) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27X_SMOKE" }
    if ($Phase27Y) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27Y_SMOKE" }
    if ($Phase27Z) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE27Z_SMOKE" }
    if ($Phase28A) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE28A_SMOKE" }
    if ($Phase28B) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE28B_SMOKE" }
    if ($Phase28C) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE28C_SMOKE" }
    if ($Phase28D) { $env:EXTRA_CFLAGS += " -DGXOS_PHASE28D_SMOKE" }
    Push-Location $kernelDirectory
    try {
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $kernelDirectory "build/amd64/obj/core/main.o")
        if ($Phase27E -or $Phase27F -or $Phase27M -or $Phase27O -or $Phase27P -or $Phase27Q -or $Phase27R -or $Phase27S -or $Phase27T -or $Phase27U -or $Phase27V -or $Phase27W -or $Phase27X -or $Phase27Y -or $Phase27Z -or $Phase28A -or $Phase28B -or $Phase28C -or $Phase28D) {
            Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $kernelDirectory "build/amd64/obj/core/native_elf/native_elf_smoke.o")
        }
        $savedErrorActionPreference = $ErrorActionPreference
        try {
            # GCC writes warnings to stderr.  Keep those visible without
            # letting PowerShell's Stop policy abort a successful make.
            $ErrorActionPreference = "Continue"
            $makeArguments = @(
                "all", "ARCH=amd64", "EXTRA_CFLAGS=$env:EXTRA_CFLAGS",
                "MBEDTLS_GUIDEXOS_IMPORT_STATE_DEPS="
            )
            # This checkout may intentionally omit the optional third-party
            # mbedTLS tree.  The Makefile's object-specific import markers
            # still name those absent directories, so mark only those known
            # absent paths as old; never synthesize or modify the dependency.
            if (!(Test-Path (Join-Path $root "third_party/mbedtls"))) {
                $makeArguments += @(
                    "-o", "../third_party/mbedtls",
                    "-o", "../third_party/mbedtls/guidexos",
                    "-o", "../third_party/mbedtls/include",
                    "-o", "../third_party/mbedtls/include/mbedtls",
                    "-o", "../third_party/mbedtls/library",
                    "-o", "../third_party/mbedtls/tf-psa-crypto"
                )
            }
            & $make @makeArguments
            $makeExitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $savedErrorActionPreference
        }
        if ($makeExitCode -ne 0) { throw "kernel build failed" }
    }
    finally {
        Pop-Location
    }

    & $msbuild (Join-Path $root "guideXOSBootLoader/guideXOSBootLoader.vcxproj") `
        /p:Configuration=Release /p:Platform=x64 /t:Rebuild /m /nologo /verbosity:minimal
    if ($LASTEXITCODE -ne 0) { throw "bootloader build failed" }

    $kernelBinary = Join-Path $kernelDirectory "build/amd64/bin/kernel.elf"
    $bootloaderBinary = Join-Path $root "guideXOSBootLoader/x64/Release/guideXOSBootLoader.exe"
    if (!(Test-Path $kernelBinary) -or !(Test-Path $bootloaderBinary)) {
        throw "fresh kernel or bootloader output is missing"
    }

    $managedFiles = @(
        "r42.c", "r41.c", "bad.c", "r42.elf", "r42b.elf", "r41.elf", "bad.elf",
        "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
        "p27bnd.elf", "p27addr.elf",
        "d27a.c", "d27b.c", "d27c.c", "d27a.elf", "d27b.elf", "d27c.elf",
        "g27expr.c", "g27local.c", "g27assn.c", "g27preca.c", "g27precb.c", "g27unary.c", "g27logs.c", "g27unknown.c", "g27duplicate.c",
        "h27eq.c", "h27eqfalse.c", "h27cmp.c", "h27if.c", "h27suppress.c", "h27ifelse.c", "h27else.c", "h27nested.c", "h27truthy.c", "h27falsy.c", "h27assign.c", "h27missing.c", "h27invalid.c",
        "i27and11.c", "i27and10.c", "i27and01.c", "i27and00.c", "i27or11.c", "i27or10.c", "i27or01.c", "i27or00.c", "i27canonicaland.c", "i27canonicalor.c", "i27preca.c", "i27precb.c", "i27precc.c", "i27andif.c", "i27orif.c", "i27mixed.c", "i27nested.c", "i27assign.c", "i27shortand.c", "i27shortor.c", "i27invalid.c", "i27singleand.c", "i27singleor.c",
        "j27basic.c", "j27sum.c", "j27zero.c", "j27reeval.c", "j27logical.c", "j27logical_or.c", "j27ifwhile.c", "j27whileif.c", "j27nested.c", "j27bodydecl.c", "j27calls.c", "j27runtime1.c", "j27runtime2.c", "j27return.c", "j27invalid_empty.c", "j27invalid_relational.c", "j27missing.c",
        "k27basic.c", "k27continue.c", "k27break_if.c", "k27continue_if.c", "k27combined.c", "k27skip_tail.c", "k27break_tail.c", "k27nested_break.c", "k27nested_continue.c", "k27host_continue.c", "k27host_break.c", "k27break_outside.c", "k27continue_outside.c", "k27invalid_break.c", "k27invalid_continue.c", "k27missing_break_return.c", "k27missing_continue_return.c", "k27capacity.c",
         "l27zero.c", "l27one.c", "l27multi.c", "l27four.c", "l27nested.c", "l27expr.c", "l27condition.c", "l27loop.c", "l27if.c", "l27control.c", "l27forward.c", "l27backward.c", "l27isolation.c", "l27param.c", "l27entry.c", "l27missing.c", "l27duplicate_param.c", "l27duplicate_function.c", "l27param_limit.c", "l27arg_count.c", "l27unknown.c", "l27recursion.c", "m27recursive.c", "m27local.c", "m27param.c", "m27control.c", "m27loop.c", "m27nested.c", "m27expression.c", "m27mutual.c", "m27boundary.c", "m27overboundary.c", "m27deep.c",
        "g27expr.elf", "g27local.elf", "g27assn.elf", "g27preca.elf", "g27precb.elf", "g27unary.elf", "g27logs.elf", "g27unknown.elf", "g27duplicate.elf", "g27deta.elf", "g27detb.elf", "g27reco.elf",
        "h27eq.elf", "h27eqfalse.elf", "h27cmp.elf", "h27if.elf", "h27suppress.elf", "h27ifelse.elf", "h27else.elf", "h27nested.elf", "h27truthy.elf", "h27falsy.elf", "h27assign.elf", "h27missing.elf", "h27invalid.elf", "h27deta.elf", "h27detb.elf", "h27reco.elf",
        "i27and11.elf", "i27and10.elf", "i27and01.elf", "i27and00.elf", "i27or11.elf", "i27or10.elf", "i27or01.elf", "i27or00.elf", "i27canonicaland.elf", "i27canonicalor.elf", "i27preca.elf", "i27precb.elf", "i27precc.elf", "i27andif.elf", "i27orif.elf", "i27mixed.elf", "i27nested.elf", "i27assign.elf", "i27shortand.elf", "i27shortor.elf", "i27invalid.elf", "i27singleand.elf", "i27singleor.elf", "i27and.elf", "i27or.elf", "deta.elf", "detb.elf",
        "j27basic.elf", "j27sum.elf", "j27zero.elf", "j27reeval.elf", "j27logical.elf", "j27logical_or.elf", "j27ifwhile.elf", "j27whileif.elf", "j27nested.elf", "j27bodydecl.elf", "j27calls.elf", "j27runtime1.elf", "j27runtime2.elf", "j27return.elf", "j27invalid_empty.elf", "j27invalid_relational.elf", "j27missing.elf", "j27deta.elf", "j27detb.elf", "j27reco.elf",
        "k27basic.elf", "k27cont.elf", "k27continue.elf", "k27break.elf", "k27combined.elf", "k27deta.elf", "k27detb.elf",
        "l27primary.elf", "l27deta.elf", "l27detb.elf", "m27primary.elf", "m27deta.elf", "m27detb.elf",
        "r27local.c", "r27global.c", "r27array.c", "r27dynamic.c", "r27oob.c", "r27copy.c", "r27assign.c", "r27param.c", "r27recursive.c", "r27invalid_address.c", "r27invalid_deref.c", "r27invalid_decay.c", "r27invalid_arithmetic.c", "r27invalid_type.c", "r27invalid_uninitialized.c", "r27invalid_global.c", "r27sig_main.c", "r27sig_math.c",
        "r27local.elf", "r27global.elf", "r27array.elf", "r27dynamic.elf", "r27oob.elf", "r27copy.elf", "r27assign.elf", "r27param.elf", "r27recursive.elf", "r27sig.elf", "r27main.elf",
        "s27walk.c", "s27store.c", "s27retreat.c", "s27middle.c", "s27equality.c", "s27copy.c", "s27param_isolation.c", "s27condition.c", "s27onepast.c", "s27onepast_deref.c", "s27beyond.c", "s27before.c", "s27scalar.c", "s27adjacent.c", "s27scalar_adjacent.c", "s27overflow.c", "s27type.c", "s27raw.c", "s27rvalue.c", "s27recursion.c", "s27deep.c", "s27global.c", "s27sig_main.c", "s27sig_math.c",
        "s27walk.elf", "s27store.elf", "s27retreat.elf", "s27middle.elf", "s27onepast.elf", "s27global.elf", "s27main.elf",
        "kernel.elf", "EFI/BOOT/BOOTX64.EFI", "NvVars"
    )
    foreach ($relativePath in $managedFiles) {
        $target = Join-Path $espDirectory $relativePath
        if (Test-Path $target -PathType Container) { throw "ESP target is a directory: $target" }
        if (Test-Path $target) {
            $backup = Join-Path $tempDirectory ("backup-" + ($relativePath -replace '[/\\]', '-'))
            Copy-Item $target $backup -Force
            $backups[$relativePath] = $backup
        }
    }
    if ($Phase27E -or $Phase27F) {
        foreach ($relativeDirectory in @("P27E", "Apps/DS27E")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27F) {
        foreach ($relativeDirectory in @("P27F", "Apps/DS27F")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27G) {
        foreach ($relativeDirectory in @("P27G", "Apps/DS27G")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27H) {
        foreach ($relativeDirectory in @("P27H", "Apps/DS27H")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27I) {
        foreach ($relativeDirectory in @("P27I", "Apps/DS27I")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27J) {
        foreach ($relativeDirectory in @("P27J", "Apps/DS27J")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27K) {
        foreach ($relativeDirectory in @("P27K", "Apps/DS27K")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27L) {
        foreach ($relativeDirectory in @("P27L", "Apps/DS27L")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27M) {
        foreach ($relativeDirectory in @("P27M", "Apps/DS27M")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27N) {
        foreach ($relativeDirectory in @("P27N", "Apps/DS27N")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27O) {
        foreach ($relativeDirectory in @("P27O", "Apps/DS27O")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27P) {
        foreach ($relativeDirectory in @("P27P", "Apps/DS27P")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27Q) {
        foreach ($relativeDirectory in @("P27Q", "Apps/DS27Q")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27R) {
        foreach ($relativeDirectory in @("P27R", "Apps/DS27R")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27S) {
        foreach ($relativeDirectory in @("P27S", "Apps/DS27S")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27T) {
        foreach ($relativeDirectory in @("P27T", "Apps/DS27T")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
            if (Test-Path $target -PathType Container) {
                $backup = Join-Path $tempDirectory ("backup-directory-" + ($relativeDirectory -replace '[/\\]', '-'))
                Copy-Item $target $backup -Recurse -Force
                $directoryBackups[$relativeDirectory] = $backup
            }
        }
    }
    if ($Phase27U) {
        $target = Join-Path $espDirectory "P27U"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P27U"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P27U"] = $backup
        }
    }
    if ($Phase27V) {
        $target = Join-Path $espDirectory "P27V"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P27V"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P27V"] = $backup
        }
    }
    if ($Phase27W) {
        $target = Join-Path $espDirectory "P27W"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P27W"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P27W"] = $backup
        }
    }
    if ($Phase27X) {
        $target = Join-Path $espDirectory "P27X"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P27X"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P27X"] = $backup
        }
    }
    if ($Phase27Y) {
        $target = Join-Path $espDirectory "P27Y"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P27Y"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P27Y"] = $backup
        }
    }
    if ($Phase27Z) {
        $target = Join-Path $espDirectory "P27Z"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P27Z"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P27Z"] = $backup
        }
    }
    if ($Phase28A) {
        $target = Join-Path $espDirectory "P28A"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P28A"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P28A"] = $backup
        }
    }
    if ($Phase28B) {
        $target = Join-Path $espDirectory "P28B"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P28B"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P28B"] = $backup
        }
    }
    if ($Phase28C) {
        $target = Join-Path $espDirectory "P28C"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P28C"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P28C"] = $backup
        }
    }
    if ($Phase28D) {
        $target = Join-Path $espDirectory "P28D"
        if (Test-Path $target -PathType Leaf) { throw "ESP target is a file: $target" }
        if (Test-Path $target -PathType Container) {
            $backup = Join-Path $tempDirectory "backup-directory-P28D"
            Copy-Item $target $backup -Recurse -Force
            $directoryBackups["P28D"] = $backup
        }
    }
    if ($Phase27R) {
        $phase27rEspDirectory = Join-Path $espDirectory "P27R"
        $phase27rEspAppDirectory = Join-Path $espDirectory "Apps/DS27R"
        Stage-Phase27RProject $phase27rEspDirectory
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $phase27rEspAppDirectory) | Out-Null
        if (Test-Path -LiteralPath $phase27rEspAppDirectory) {
            Remove-Item -LiteralPath $phase27rEspAppDirectory -Recurse -Force
        }
        Copy-Item $phase27rAppDirectory $phase27rEspAppDirectory -Recurse -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27R/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27R/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27R/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27R project fixture was not staged into ESP"
        }
    }
    if ($Phase27S) {
        $phase27sEspDirectory = Join-Path $espDirectory "P27S"
        $phase27sEspAppDirectory = Join-Path $espDirectory "Apps/DS27S"
        Stage-Phase27SProject $phase27sEspDirectory
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $phase27sEspAppDirectory) | Out-Null
        if (Test-Path -LiteralPath $phase27sEspAppDirectory) { Remove-Item $phase27sEspAppDirectory -Recurse -Force }
        Copy-Item $phase27sAppDirectory $phase27sEspAppDirectory -Recurse -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27S/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27S/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27S/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27S project fixture was not staged into ESP"
        }
    }
    if ($Phase27T) {
        $phase27tEspDirectory = Join-Path $espDirectory "P27T"
        $phase27tEspAppDirectory = Join-Path $espDirectory "Apps/DS27T"
        Stage-Phase27TProject $phase27tEspDirectory
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $phase27tEspAppDirectory) | Out-Null
        if (Test-Path -LiteralPath $phase27tEspAppDirectory) { Remove-Item $phase27tEspAppDirectory -Recurse -Force }
        Copy-Item $phase27tAppDirectory $phase27tEspAppDirectory -Recurse -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27T/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27T/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27T/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27T project fixture was not staged into ESP"
        }
    }
    if ($Phase27U) {
        $phase27uEspDirectory = Join-Path $espDirectory "P27U"
        Stage-Phase27UProject $phase27uEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27U/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27U/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27U/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27U project fixture was not staged into ESP"
        }
    }
    if ($Phase27V) {
        $phase27vEspDirectory = Join-Path $espDirectory "P27V"
        Stage-Phase27VProject $phase27vEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27V/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27V/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27V/app/app.json") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27V/include/point.h") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27V/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27V project fixture was not staged into ESP"
        }
    }
    if ($Phase27W) {
        $phase27wEspDirectory = Join-Path $espDirectory "P27W"
        Stage-Phase27WProject $phase27wEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27W/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27W/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27W/app/app.json") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27W/include/phase27w_value.h") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27W/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27W project fixture was not staged into ESP"
        }
    }
    if ($Phase27X) {
        $phase27xEspDirectory = Join-Path $espDirectory "P27X"
        Stage-Phase27XProject $phase27xEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27X/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27X/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27X/app/app.json") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27X/include/guidexos_app.h") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27X/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27X project fixture was not staged into ESP"
        }
    }
    if ($Phase27Y) {
        $phase27yEspDirectory = Join-Path $espDirectory "P27Y"
        Stage-Phase27YProject $phase27yEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27Y/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27Y/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27Y/app/app.json") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27Y/include/guidexos_app.h") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27Y/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27Y project fixture was not staged into ESP"
        }
    }
    if ($Phase27Z) {
        $phase27zEspDirectory = Join-Path $espDirectory "P27Z"
        Stage-Phase27ZProject $phase27zEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27Z/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27Z/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27Z/app/app.json") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27Z/include/guidexos_app.h") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27Z/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27Z project fixture was not staged into ESP"
        }
    }
    if ($Phase28A) {
        $phase28aEspDirectory = Join-Path $espDirectory "P28A"
        Stage-Phase28AProject $phase28aEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P28A/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P28A/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P28A/app/app.json") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P28A/src/main.cpp") -PathType Leaf)) {
            throw "Phase 28A project fixture was not staged into ESP"
        }
    }
    if ($Phase28B) {
        $phase28bEspDirectory = Join-Path $espDirectory "P28B"
        Stage-Phase28BProject $phase28bEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P28B/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P28B/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P28B/app/app.json") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P28B/src/main.cpp") -PathType Leaf)) {
            throw "Phase 28B project fixture was not staged into ESP"
        }
    }
    if ($Phase28C) {
        $phase28cEspDirectory = Join-Path $espDirectory "P28C"
        Stage-Phase28CProject $phase28cEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P28C/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P28C/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P28C/app/app.json") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P28C/src/main.cpp") -PathType Leaf)) {
            throw "Phase 28C project fixture was not staged into ESP"
        }
    }
    if ($Phase28D) {
        $phase28dEspDirectory = Join-Path $espDirectory "P28D"
        Stage-Phase28DProject $phase28dEspDirectory
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P28D/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P28D/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P28D/app/app.json") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P28D/src/main.cpp") -PathType Leaf)) {
            throw "Phase 28D project fixture was not staged into ESP"
        }
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "EFI/BOOT") | Out-Null
    Copy-Item (Join-Path $fixtureDirectory "r42.c") (Join-Path $espDirectory "r42.c") -Force
    Copy-Item (Join-Path $fixtureDirectory "r41.c") (Join-Path $espDirectory "r41.c") -Force
    Copy-Item (Join-Path $fixtureDirectory "bad.c") (Join-Path $espDirectory "bad.c") -Force
    Copy-Item (Join-Path $phase27dFixtureDirectory "d27a.c") (Join-Path $espDirectory "d27a.c") -Force
    Copy-Item (Join-Path $phase27dFixtureDirectory "d27b.c") (Join-Path $espDirectory "d27b.c") -Force
    Copy-Item (Join-Path $phase27dFixtureDirectory "d27c.c") (Join-Path $espDirectory "d27c.c") -Force
    Copy-Item (Join-Path $phase27gFixtureDirectory "g27expr.c") (Join-Path $espDirectory "g27expr.c") -Force
    Copy-Item (Join-Path $phase27gFixtureDirectory "g27local.c") (Join-Path $espDirectory "g27local.c") -Force
    Copy-Item (Join-Path $phase27gFixtureDirectory "g27assn.c") (Join-Path $espDirectory "g27assn.c") -Force
    Copy-Item (Join-Path $phase27gFixtureDirectory "g27preca.c") (Join-Path $espDirectory "g27preca.c") -Force
    Copy-Item (Join-Path $phase27gFixtureDirectory "g27precb.c") (Join-Path $espDirectory "g27precb.c") -Force
    Copy-Item (Join-Path $phase27gFixtureDirectory "g27unary.c") (Join-Path $espDirectory "g27unary.c") -Force
    Copy-Item (Join-Path $phase27gFixtureDirectory "g27logs.c") (Join-Path $espDirectory "g27logs.c") -Force
    Copy-Item (Join-Path $phase27gFixtureDirectory "g27unknown.c") (Join-Path $espDirectory "g27unknown.c") -Force
    Copy-Item (Join-Path $phase27gFixtureDirectory "g27duplicate.c") (Join-Path $espDirectory "g27duplicate.c") -Force
    if ($Phase27R) {
        foreach ($sourceName in @(
            "r27local.c", "r27global.c", "r27array.c", "r27dynamic.c", "r27oob.c", "r27copy.c",
            "r27assign.c", "r27param.c", "r27recursive.c", "r27invalid_address.c", "r27invalid_deref.c",
            "r27invalid_decay.c", "r27invalid_arithmetic.c", "r27invalid_type.c", "r27invalid_uninitialized.c", "r27invalid_global.c",
            "r27sig_main.c", "r27sig_math.c")) {
            Copy-Item (Join-Path $phase27rFixtureDirectory $sourceName) (Join-Path $espDirectory $sourceName) -Force
        }
    }
    if ($Phase27S) {
        foreach ($sourceName in @(
            "s27walk.c", "s27store.c", "s27retreat.c", "s27middle.c", "s27equality.c", "s27copy.c", "s27param_isolation.c", "s27condition.c", "s27onepast.c", "s27onepast_deref.c",
            "s27beyond.c", "s27before.c", "s27scalar.c", "s27adjacent.c", "s27scalar_adjacent.c",
            "s27overflow.c", "s27type.c", "s27raw.c", "s27rvalue.c", "s27recursion.c", "s27deep.c", "s27global.c",
            "s27sig_main.c", "s27sig_math.c")) {
            Copy-Item (Join-Path $phase27sFixtureDirectory $sourceName) (Join-Path $espDirectory $sourceName) -Force
        }
    }
    Copy-Item $kernelBinary (Join-Path $espDirectory "kernel.elf") -Force
    Copy-Item $bootloaderBinary (Join-Path $espDirectory "EFI/BOOT/BOOTX64.EFI") -Force
    if ($Phase27E -or $Phase27F) {
        Copy-Item $phase27eFixtureDirectory (Join-Path $espDirectory "P27E") -Recurse -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "Apps") | Out-Null
        Copy-Item $phase27eAppDirectory (Join-Path $espDirectory "Apps/DS27E") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27E/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27E/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27E project fixture was not staged into ESP"
        }
    }
    if ($Phase27F) {
        Copy-Item $phase27fFixtureDirectory (Join-Path $espDirectory "P27F") -Recurse -Force
        Copy-Item $phase27fAppDirectory (Join-Path $espDirectory "Apps/DS27F") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27F/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27F/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27F project fixture was not staged into ESP"
        }
    }
    if ($Phase27G) {
        Copy-Item $phase27gFixtureDirectory (Join-Path $espDirectory "P27G") -Recurse -Force
        Copy-Item $phase27gAppDirectory (Join-Path $espDirectory "Apps/DS27G") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27G/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27G/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27G project fixture was not staged into ESP"
        }
    }
    if ($Phase27H) {
        Stage-Phase27HProject (Join-Path $espDirectory "P27H")
        Copy-Item $phase27hAppDirectory (Join-Path $espDirectory "Apps/DS27H") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27H/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27H/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27H project fixture was not staged into ESP"
        }
    }
    if ($Phase27I) {
        Stage-Phase27IProject (Join-Path $espDirectory "P27I")
        Copy-Item $phase27iAppDirectory (Join-Path $espDirectory "Apps/DS27I") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27I/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27I/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27I project fixture was not staged into ESP"
        }
    }
    if ($Phase27J) {
        Stage-Phase27JProject (Join-Path $espDirectory "P27J")
        Copy-Item $phase27jAppDirectory (Join-Path $espDirectory "Apps/DS27J") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27J/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27J/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27J project fixture was not staged into ESP"
        }
    }
    if ($Phase27K) {
        Stage-Phase27KProject (Join-Path $espDirectory "P27K")
        Copy-Item $phase27kAppDirectory (Join-Path $espDirectory "Apps/DS27K") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27K/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27K/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27K project fixture was not staged into ESP"
        }
    }
    if ($Phase27L) {
        Stage-Phase27LProject (Join-Path $espDirectory "P27L")
        Copy-Item $phase27lAppDirectory (Join-Path $espDirectory "Apps/DS27L") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27L/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27L/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27L project fixture was not staged into ESP"
        }
    }
    if ($Phase27M) {
        Stage-Phase27MProject (Join-Path $espDirectory "P27M")
        Copy-Item $phase27mAppDirectory (Join-Path $espDirectory "Apps/DS27M") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27M/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27M/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27M project fixture was not staged into ESP"
        }
    }
    if ($Phase27N) {
        Stage-Phase27NProject (Join-Path $espDirectory "P27N")
        Copy-Item $phase27nAppDirectory (Join-Path $espDirectory "Apps/DS27N") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27N/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27N/src/helpers.cpp") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27N/src/main.cpp") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27N/src/math.cpp") -PathType Leaf)) {
            throw "Phase 27N project fixture was not staged into ESP"
        }
    }
    if ($Phase27O) {
        Stage-Phase27OProject (Join-Path $espDirectory "P27O")
        Copy-Item $phase27oAppDirectory (Join-Path $espDirectory "Apps/DS27O") -Recurse -Force
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27O/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27O/src/main.cpp") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27O/src/math.cpp") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27O/src/state.cpp") -PathType Leaf)) {
            throw "Phase 27O project fixture was not staged into ESP"
        }
    }
    if ($Phase27P) {
        Stage-Phase27PProject (Join-Path $espDirectory "P27P")
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27P/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27P/src/main.cpp") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27P/src/math.cpp") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27P/src/state.cpp") -PathType Leaf)) {
            throw "Phase 27P project fixture was not staged into ESP"
        }
    }
    if ($Phase27Q) {
        $phase27qEspDirectory = Join-Path $espDirectory "P27Q"
        $phase27qEspAppDirectory = Join-Path $espDirectory "Apps/DS27Q"
        if (Test-Path $phase27qEspDirectory) {
            Remove-Item -LiteralPath $phase27qEspDirectory -Recurse -Force
        }
        if (Test-Path $phase27qEspAppDirectory) {
            Remove-Item -LiteralPath $phase27qEspAppDirectory -Recurse -Force
        }
        Copy-Item $phase27qFixtureDirectory $phase27qEspDirectory -Recurse -Force
        Copy-Item $phase27qAppDirectory $phase27qEspAppDirectory -Recurse -Force
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27Q/out") | Out-Null
        if (!(Test-Path -LiteralPath (Join-Path $espDirectory "P27Q/guidexos.project") -PathType Leaf) -or
            !(Test-Path -LiteralPath (Join-Path $espDirectory "P27Q/src/main.cpp") -PathType Leaf)) {
            throw "Phase 27Q project fixture was not staged into ESP"
        }
    }

    # Each boot receives a clean guest output namespace. The compiler itself
    # creates/replaces these files through guideXOS VFS path-level operations.
    foreach ($relativePath in @(
        "r42.elf", "r42b.elf", "r41.elf", "bad.elf",
        "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
        "p27bnd.elf", "p27addr.elf", "d27a.elf", "d27b.elf", "d27c.elf",
        "g27expr.elf", "g27local.elf", "g27assn.elf", "g27preca.elf", "g27precb.elf", "g27unary.elf", "g27logs.elf", "g27unknown.elf", "g27duplicate.elf", "g27deta.elf", "g27detb.elf", "g27reco.elf",
        "h27eq.elf", "h27eqfalse.elf", "h27cmp.elf", "h27if.elf", "h27suppress.elf", "h27ifelse.elf", "h27else.elf", "h27nested.elf", "h27truthy.elf", "h27falsy.elf", "h27assign.elf", "h27missing.elf", "h27invalid.elf", "h27deta.elf", "h27detb.elf", "h27reco.elf",
        "i27and11.elf", "i27and10.elf", "i27and01.elf", "i27and00.elf", "i27or11.elf", "i27or10.elf", "i27or01.elf", "i27or00.elf", "i27canonicaland.elf", "i27canonicalor.elf", "i27preca.elf", "i27precb.elf", "i27precc.elf", "i27andif.elf", "i27orif.elf", "i27mixed.elf", "i27nested.elf", "i27assign.elf", "i27shortand.elf", "i27shortor.elf", "i27invalid.elf", "i27singleand.elf", "i27singleor.elf", "i27and.elf", "i27or.elf", "deta.elf", "detb.elf",
        "j27basic.elf", "j27sum.elf", "j27zero.elf", "j27reeval.elf", "j27logical.elf", "j27logical_or.elf", "j27ifwhile.elf", "j27whileif.elf", "j27nested.elf", "j27bodydecl.elf", "j27calls.elf", "j27runtime1.elf", "j27runtime2.elf", "j27return.elf", "j27invalid_empty.elf", "j27invalid_relational.elf", "j27missing.elf", "j27deta.elf", "j27detb.elf", "j27reco.elf",
        "k27basic.elf", "k27cont.elf", "k27continue.elf", "k27break.elf", "k27combined.elf", "k27deta.elf", "k27detb.elf",
        "l27primary.elf", "l27deta.elf", "l27detb.elf", "m27primary.elf")) {
        $target = Join-Path $espDirectory $relativePath
        if (Test-Path $target) { Remove-Item -LiteralPath $target -Force }
    }

    for ($run = 1; $run -le $BootCount; ++$run) {
        if ($run -gt 1) {
            foreach ($source in @(
                @{ Fixture = (Join-Path $fixtureDirectory "r42.c"); Target = "r42.c" },
                @{ Fixture = (Join-Path $fixtureDirectory "r41.c"); Target = "r41.c" },
                @{ Fixture = (Join-Path $fixtureDirectory "bad.c"); Target = "bad.c" },
                @{ Fixture = (Join-Path $phase27dFixtureDirectory "d27a.c"); Target = "d27a.c" },
                @{ Fixture = (Join-Path $phase27dFixtureDirectory "d27b.c"); Target = "d27b.c" },
                @{ Fixture = (Join-Path $phase27dFixtureDirectory "d27c.c"); Target = "d27c.c" },
                @{ Fixture = (Join-Path $phase27gFixtureDirectory "g27expr.c"); Target = "g27expr.c" },
                @{ Fixture = (Join-Path $phase27gFixtureDirectory "g27local.c"); Target = "g27local.c" },
                @{ Fixture = (Join-Path $phase27gFixtureDirectory "g27assn.c"); Target = "g27assn.c" },
                @{ Fixture = (Join-Path $phase27gFixtureDirectory "g27preca.c"); Target = "g27preca.c" },
                @{ Fixture = (Join-Path $phase27gFixtureDirectory "g27precb.c"); Target = "g27precb.c" },
                @{ Fixture = (Join-Path $phase27gFixtureDirectory "g27unary.c"); Target = "g27unary.c" },
                @{ Fixture = (Join-Path $phase27gFixtureDirectory "g27logs.c"); Target = "g27logs.c" },
                @{ Fixture = (Join-Path $phase27gFixtureDirectory "g27unknown.c"); Target = "g27unknown.c" },
                @{ Fixture = (Join-Path $phase27gFixtureDirectory "g27duplicate.c"); Target = "g27duplicate.c" }
            )) {
                Copy-Item $source.Fixture (Join-Path $espDirectory $source.Target) -Force
            }
            if ($Phase27K) {
                foreach ($sourceName in @(
                    "k27basic.c", "k27continue.c", "k27break_if.c", "k27continue_if.c", "k27combined.c",
                    "k27skip_tail.c", "k27break_tail.c", "k27nested_break.c", "k27nested_continue.c",
                    "k27host_continue.c", "k27host_break.c", "k27break_outside.c", "k27continue_outside.c",
                    "k27invalid_break.c", "k27invalid_continue.c", "k27missing_break_return.c",
                    "k27missing_continue_return.c", "k27capacity.c")) {
                    Copy-Item (Join-Path $phase27kFixtureDirectory $sourceName) (Join-Path $espDirectory $sourceName) -Force
                }
            }
            if ($Phase27L) {
                foreach ($sourceName in @(
                    "l27zero.c", "l27one.c", "l27multi.c", "l27four.c", "l27nested.c", "l27expr.c",
                    "l27condition.c", "l27loop.c", "l27if.c", "l27control.c", "l27forward.c", "l27backward.c",
                    "l27isolation.c", "l27param.c", "l27entry.c", "l27missing.c", "l27duplicate_param.c",
                    "l27duplicate_function.c", "l27param_limit.c", "l27arg_count.c", "l27unknown.c", "l27recursion.c")) {
                    Copy-Item (Join-Path $phase27lFixtureDirectory $sourceName) (Join-Path $espDirectory $sourceName) -Force
                }
            }
            if ($Phase27M) {
                foreach ($sourceName in @(
                    "m27recursive.c", "m27local.c", "m27param.c", "m27control.c", "m27loop.c",
                    "m27nested.c", "m27expression.c", "m27mutual.c", "m27boundary.c",
                    "m27overboundary.c", "m27deep.c")) {
                    Copy-Item (Join-Path $phase27mFixtureDirectory $sourceName) (Join-Path $espDirectory $sourceName) -Force
                }
            }
            if ($Phase27R) {
                foreach ($sourceName in @(
                    "r27local.c", "r27global.c", "r27array.c", "r27dynamic.c", "r27oob.c", "r27copy.c",
                    "r27assign.c", "r27param.c", "r27recursive.c", "r27invalid_address.c", "r27invalid_deref.c",
                    "r27invalid_decay.c", "r27invalid_arithmetic.c", "r27invalid_type.c", "r27invalid_uninitialized.c", "r27invalid_global.c",
                    "r27sig_main.c", "r27sig_math.c")) {
                    Copy-Item (Join-Path $phase27rFixtureDirectory $sourceName) (Join-Path $espDirectory $sourceName) -Force
                }
            }
            foreach ($relativePath in @(
                "r42.elf", "r42b.elf", "r41.elf", "bad.elf",
                "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
                "p27bnd.elf", "p27addr.elf", "d27a.elf", "d27b.elf", "d27c.elf",
                "g27expr.elf", "g27local.elf", "g27assn.elf", "g27preca.elf", "g27precb.elf", "g27unary.elf", "g27logs.elf", "g27unknown.elf", "g27duplicate.elf", "g27deta.elf", "g27detb.elf", "g27reco.elf",
                "h27eq.elf", "h27eqfalse.elf", "h27cmp.elf", "h27if.elf", "h27suppress.elf", "h27ifelse.elf", "h27else.elf", "h27nested.elf", "h27truthy.elf", "h27falsy.elf", "h27assign.elf", "h27missing.elf", "h27invalid.elf", "h27deta.elf", "h27detb.elf", "h27reco.elf",
                "i27and11.elf", "i27and10.elf", "i27and01.elf", "i27and00.elf", "i27or11.elf", "i27or10.elf", "i27or01.elf", "i27or00.elf", "i27canonicaland.elf", "i27canonicalor.elf", "i27preca.elf", "i27precb.elf", "i27precc.elf", "i27andif.elf", "i27orif.elf", "i27mixed.elf", "i27nested.elf", "i27assign.elf", "i27shortand.elf", "i27shortor.elf", "i27invalid.elf", "i27singleand.elf", "i27singleor.elf", "i27and.elf", "i27or.elf", "deta.elf", "detb.elf",
                "j27basic.elf", "j27sum.elf", "j27zero.elf", "j27reeval.elf", "j27logical.elf", "j27logical_or.elf", "j27ifwhile.elf", "j27whileif.elf", "j27nested.elf", "j27bodydecl.elf", "j27calls.elf", "j27runtime1.elf", "j27runtime2.elf", "j27return.elf", "j27invalid_empty.elf", "j27invalid_relational.elf", "j27missing.elf", "j27deta.elf", "j27detb.elf", "j27reco.elf",
                "k27basic.elf", "k27cont.elf", "k27continue.elf", "k27break.elf", "k27combined.elf", "k27deta.elf", "k27detb.elf",
                "l27primary.elf", "l27deta.elf", "l27detb.elf", "m27primary.elf", "m27deta.elf", "m27detb.elf",
                "r27local.elf", "r27global.elf", "r27array.elf", "r27dynamic.elf", "r27oob.elf", "r27copy.elf", "r27assign.elf", "r27param.elf", "r27recursive.elf", "r27sig.elf", "r27main.elf")) {
                $target = Join-Path $espDirectory $relativePath
                if (Test-Path $target) { Remove-Item -LiteralPath $target -Force }
            }
        }
        if (($Phase27E -or $Phase27F) -and $run -gt 1) {
            $projectTarget = Join-Path $espDirectory "P27E"
            if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
            Copy-Item $phase27eFixtureDirectory $projectTarget -Recurse -Force
        }
        if ($Phase27F -and $run -gt 1) {
            $projectTarget = Join-Path $espDirectory "P27F"
            if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
            Copy-Item $phase27fFixtureDirectory $projectTarget -Recurse -Force
        }
        if ($Phase27G -and $run -gt 1) {
            $projectTarget = Join-Path $espDirectory "P27G"
            if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
            Copy-Item $phase27gFixtureDirectory $projectTarget -Recurse -Force
        }
        if ($Phase27H -and $run -gt 1) {
            $projectTarget = Join-Path $espDirectory "P27H"
            if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
            Stage-Phase27HProject $projectTarget
        }
        if ($Phase27I -and $run -gt 1) {
            $projectTarget = Join-Path $espDirectory "P27I"
            if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
            Stage-Phase27IProject $projectTarget
        }
        if ($Phase27J -and $run -gt 1) {
            $projectTarget = Join-Path $espDirectory "P27J"
            if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
            Stage-Phase27JProject $projectTarget
        }
        if ($Phase27K -and $run -gt 1) {
            $projectTarget = Join-Path $espDirectory "P27K"
            if (Test-Path $projectTarget) { Remove-Item $projectTarget -Recurse -Force }
            Stage-Phase27KProject $projectTarget
        }
            if ($Phase27L -and $run -gt 1) {
                $projectTarget = Join-Path $espDirectory "P27L"
                if (Test-Path $projectTarget) { Remove-Item $projectTarget -Recurse -Force }
                Stage-Phase27LProject $projectTarget
            }
            if ($Phase27M -and $run -gt 1) {
                $projectTarget = Join-Path $espDirectory "P27M"
                if (Test-Path $projectTarget) { Remove-Item $projectTarget -Recurse -Force }
                Stage-Phase27MProject $projectTarget
            }
            if ($Phase27N -and $run -gt 1) {
                $projectTarget = Join-Path $espDirectory "P27N"
                if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
                Stage-Phase27NProject $projectTarget
            }
            if ($Phase27O -and $run -gt 1) {
                $projectTarget = Join-Path $espDirectory "P27O"
                if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
                Stage-Phase27OProject $projectTarget
            }
            if ($Phase27P -and $run -gt 1) {
                $projectTarget = Join-Path $espDirectory "P27P"
                if (Test-Path $projectTarget) { Remove-Item -LiteralPath $projectTarget -Recurse -Force }
                Stage-Phase27PProject $projectTarget
            }
            if ($Phase27R -and $run -gt 1) {
                Stage-Phase27RProject (Join-Path $espDirectory "P27R")
                New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27R/out") | Out-Null
            }
            if ($Phase27S -and $run -gt 1) {
                Stage-Phase27SProject (Join-Path $espDirectory "P27S")
                New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27S/out") | Out-Null
            }
    if ($Phase27T -and $run -gt 1) {
        Stage-Phase27TProject (Join-Path $espDirectory "P27T")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27T/out") | Out-Null
    }
    if ($Phase27U -and $run -gt 1) {
        Stage-Phase27UProject (Join-Path $espDirectory "P27U")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27U/out") | Out-Null
    }
    if ($Phase27W -and $run -gt 1) {
        Stage-Phase27WProject (Join-Path $espDirectory "P27W")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27W/out") | Out-Null
    }
    if ($Phase27X -and $run -gt 1) {
        Stage-Phase27XProject (Join-Path $espDirectory "P27X")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27X/out") | Out-Null
    }
    if ($Phase27Y -and $run -gt 1) {
        Stage-Phase27YProject (Join-Path $espDirectory "P27Y")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27Y/out") | Out-Null
    }
    if ($Phase27Z -and $run -gt 1) {
        Stage-Phase27ZProject (Join-Path $espDirectory "P27Z")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P27Z/out") | Out-Null
    }
    if ($Phase28A -and $run -gt 1) {
        Stage-Phase28AProject (Join-Path $espDirectory "P28A")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P28A/out") | Out-Null
    }
    if ($Phase28B -and $run -gt 1) {
        Stage-Phase28BProject (Join-Path $espDirectory "P28B")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P28B/out") | Out-Null
    }
    if ($Phase28C -and $run -gt 1) {
        Stage-Phase28CProject (Join-Path $espDirectory "P28C")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P28C/out") | Out-Null
    }
    if ($Phase28D -and $run -gt 1) {
        Stage-Phase28DProject (Join-Path $espDirectory "P28D")
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "P28D/out") | Out-Null
    }
        # Every QEMU invocation gets its own disposable directory-backed FAT
        # image. Guest writes must not become the input state of the next
        # requested fresh boot.
        $activeEspDirectory = Join-Path $tempDirectory ("esp-boot{0}" -f $run)
        Copy-Item $espDirectory $activeEspDirectory -Recurse -Force
        Invoke-QemuProofBoot $run $qemu
    }

    # The guest compiler writes its artifacts through the boot-time VFS.  The
    # default smoke image is memory-backed during a boot, so the guest emits
    # exact generated bytes over serial for independent host-side inspection.
    $finalSerial = Read-SerialText (Join-Path $tempDirectory ("boot{0}.serial.log" -f $BootCount))
    $serialArtifacts = @("r42")
    foreach ($candidate in @("d27a", "d27b", "d27c")) {
        if ($finalSerial -match [regex]::Escape("artifact_begin=$candidate")) {
            $serialArtifacts += $candidate
        }
    }
    foreach ($artifact in $serialArtifacts) {
        Export-SerialArtifact $finalSerial $artifact (Join-Path $evidenceDirectory ($artifact + ".elf"))
    }
    if ($Phase27G) {
        Export-SerialArtifact $finalSerial "g27local" (Join-Path $evidenceDirectory "g27local.elf")
    }
    if ($Phase27H) {
        Export-SerialArtifact $finalSerial "h27ifelse" (Join-Path $evidenceDirectory "h27ifelse.elf")
    }
    if ($Phase27I) {
        Export-SerialArtifact $finalSerial "i27and" (Join-Path $evidenceDirectory "i27and.elf")
        Export-SerialArtifact $finalSerial "i27or" (Join-Path $evidenceDirectory "i27or.elf")
    }
    if ($Phase27J) {
        Export-SerialArtifact $finalSerial "j27sum" (Join-Path $evidenceDirectory "j27sum.elf")
    }
    if ($Phase27K) {
        Export-SerialArtifact $finalSerial "k27break" (Join-Path $evidenceDirectory "k27break.elf")
        Export-SerialArtifact $finalSerial "k27continue" (Join-Path $evidenceDirectory "k27continue.elf")
        Export-SerialArtifact $finalSerial "k27combined" (Join-Path $evidenceDirectory "k27combined.elf")
    }
    if ($Phase27L) {
        Export-SerialArtifact $finalSerial "l27primary" (Join-Path $evidenceDirectory "l27primary.elf")
    }
    if ($Phase27M) {
        Export-SerialArtifact $finalSerial "m27primary" (Join-Path $evidenceDirectory "m27primary.elf")
    }
    if ($Phase27N) {
        Export-SerialArtifact $finalSerial "n27primary" (Join-Path $evidenceDirectory "n27primary.elf")
    }
    if ($Phase27O) {
        Export-SerialArtifact $finalSerial "o27primary" (Join-Path $evidenceDirectory "o27primary.elf")
    }
    if ($Phase27Q) {
        Export-SerialArtifact $finalSerial "q27main" (Join-Path $evidenceDirectory "q27main.elf")
    }
    if ($Phase27R) {
        Export-SerialArtifact $finalSerial "r27main" (Join-Path $evidenceDirectory "r27main.elf")
    }
    if ($Phase27S) {
        Export-SerialArtifact $finalSerial "s27main" (Join-Path $evidenceDirectory "s27main.elf")
    }
    if ($Phase27T) {
        Export-SerialArtifact $finalSerial "t27glob" (Join-Path $evidenceDirectory "t27glob.elf")
    }
    if ($Phase27T) {
        Export-SerialArtifact $finalSerial "t27main" (Join-Path $evidenceDirectory "t27main.elf")
    }
    if ($Phase27U) {
        Export-SerialArtifact $finalSerial "u27main" (Join-Path $evidenceDirectory "u27main.elf")
    }
    if ($Phase27V) {
        Export-SerialArtifact $finalSerial "v27main" (Join-Path $evidenceDirectory "v27main.elf")
    }
    if ($Phase27W) {
        Export-SerialArtifact $finalSerial "w27main" (Join-Path $evidenceDirectory "w27main.elf")
    }
    if ($Phase27X) {
        Export-SerialArtifact $finalSerial "x27main" (Join-Path $evidenceDirectory "x27main.elf")
    }
    if ($Phase27Y) {
        Export-SerialArtifact $finalSerial "y27main" (Join-Path $evidenceDirectory "y27main.elf")
    }
    if ($Phase27Z) {
        Export-SerialArtifact $finalSerial "z27main" (Join-Path $evidenceDirectory "z27main.elf")
    }
    if ($Phase28A) {
        Export-SerialArtifact $finalSerial "a28main" (Join-Path $evidenceDirectory "a28main.elf")
    }
    if ($Phase28B) {
        Export-SerialArtifact $finalSerial "b28main" (Join-Path $evidenceDirectory "b28main.elf")
    }
    if ($Phase28C) {
        Export-SerialArtifact $finalSerial "c28main" (Join-Path $evidenceDirectory "c28main.elf")
    }
    if ($Phase28D) {
        Export-SerialArtifact $finalSerial "d28main" (Join-Path $evidenceDirectory "d28main.elf")
    }

    $readelf = Get-RequiredTool "readelf" ""
    $objdump = Get-RequiredTool "objdump" ""
    Write-Host "--- external audit of guest-generated r42.elf ---" -ForegroundColor Cyan
    & $readelf -h -l (Join-Path $evidenceDirectory "r42.elf")
    # The bootstrap intentionally omits section metadata. Ask objdump to audit
    # the ELF's known file-backed code range as a raw AMD64 view.
    & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
        --start-address=0x10001000 --stop-address=0x10001006 (Join-Path $evidenceDirectory "r42.elf")
    if ($LASTEXITCODE -ne 0) { throw "external ELF inspection failed" }
    Write-Host "--- external audit of guest-generated d27a.elf ---" -ForegroundColor Cyan
    & $readelf -h -l (Join-Path $evidenceDirectory "d27a.elf")
    & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
        --start-address=0x10001000 --stop-address=0x10001022 (Join-Path $evidenceDirectory "d27a.elf")
    if ($LASTEXITCODE -ne 0) { throw "external Phase 27D ELF inspection failed" }
    if ($Phase27Q) {
        Write-Host "--- external audit of guest-generated q27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "q27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "q27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27Q ELF inspection failed" }
    }
    if ($Phase27R) {
        Write-Host "--- external audit of guest-generated r27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "r27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "r27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27R ELF inspection failed" }
    }
    if ($Phase27S) {
        Write-Host "--- external audit of guest-generated s27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "s27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "s27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27S ELF inspection failed" }
    }
    if ($Phase27T) {
        Write-Host "--- external audit of guest-generated t27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "t27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "t27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27T ELF inspection failed" }
    }
    if ($Phase27U) {
        Write-Host "--- external audit of guest-generated u27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "u27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "u27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27U ELF inspection failed" }
    }
    if ($Phase27V) {
        Write-Host "--- external audit of guest-generated v27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "v27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "v27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27V ELF inspection failed" }
    }
    if ($Phase27W) {
        Write-Host "--- external audit of guest-generated w27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "w27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "w27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27W ELF inspection failed" }
    }
    if ($Phase27X) {
        Write-Host "--- external audit of guest-generated x27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "x27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "x27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27X ELF inspection failed" }
    }
    if ($Phase27Y) {
        Write-Host "--- external audit of guest-generated y27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "y27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "y27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27Y ELF inspection failed" }
    }
    if ($Phase27Z) {
        Write-Host "--- external audit of guest-generated z27main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "z27main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "z27main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27Z ELF inspection failed" }
    }
    if ($Phase28A) {
        Write-Host "--- external audit of guest-generated a28main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "a28main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "a28main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 28A ELF inspection failed" }
    }
    if ($Phase28B) {
        Write-Host "--- external audit of guest-generated b28main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "b28main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "b28main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 28B ELF inspection failed" }
    }
    if ($Phase28C) {
        Write-Host "--- external audit of guest-generated c28main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "c28main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "c28main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 28C ELF inspection failed" }
    }
    if ($Phase28D) {
        Write-Host "--- external audit of guest-generated d28main.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "d28main.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "d28main.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 28D ELF inspection failed" }
    }
    if ($Phase27N) {
        Write-Host "--- external audit of guest-generated n27primary.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "n27primary.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "n27primary.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27N ELF inspection failed" }
    }
    if ($Phase27O) {
        Write-Host "--- external audit of guest-generated o27primary.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "o27primary.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10004000 (Join-Path $evidenceDirectory "o27primary.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27O ELF inspection failed" }
    }
    if ($Phase28DOnly) {
        Write-Host "Phase 28D focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase28COnly) {
        Write-Host "Phase 28C focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase28BOnly) {
        Write-Host "Phase 28B focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase28AOnly) {
        Write-Host "Phase 28A focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27ZOnly) {
        Write-Host "Phase 27Z focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27YOnly) {
        Write-Host "Phase 27Y focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27XOnly) {
        Write-Host "Phase 27X focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27WOnly) {
        Write-Host "Phase 27W focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27VOnly) {
        Write-Host "Phase 27V focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27UOnly) {
        Write-Host "Phase 27U focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27Z) {
        Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O/27P/27Q/27R/27S/27T/27U/27V/27W/27X/27Y/27Z QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27Y) {
        Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O/27P/27Q/27R/27S/27T/27U/27V/27W/27X/27Y QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27X) {
        Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O/27P/27Q/27R/27S/27T/27U/27V/27W/27X QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27W) {
        Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O/27P/27Q/27R/27S/27T/27U/27V/27W QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27V) {
        Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O/27P/27Q/27R/27S/27T/27U/27V QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27U) {
        Write-Host "Phase 27U arrays-of-structs QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27T -and -not $Phase27TOnly) {
        Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O/27P/27Q/27R/27S/27T QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27TOnly) {
        Write-Host "Phase 27T focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27S -and -not $Phase27SOnly) {
        Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O/27P/27Q/27R/27S QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27ROnly) {
        Write-Host "Phase 27R focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27SOnly) {
        Write-Host "Phase 27S focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27QOnly) {
        Write-Host "Phase 27Q focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27OOnly) {
        Write-Host "Phase 27O focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27POnly) {
        Write-Host "Phase 27P focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27NOnly) {
        Write-Host "Phase 27B/27C/27D/27N focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27MOnly) {
        Write-Host "--- external audit of guest-generated m27primary.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "m27primary.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10001600 (Join-Path $evidenceDirectory "m27primary.elf")
        if ($LASTEXITCODE -ne 0) { throw "external focused Phase 27M ELF inspection failed" }
        Write-Host "Phase 27B/27C/27D/27M focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27G) {
        Write-Host "--- external audit of guest-generated g27local.elf ---" -ForegroundColor Cyan
        & $readelf -h -l (Join-Path $evidenceDirectory "g27local.elf")
        & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
            --start-address=0x10001000 --stop-address=0x10001180 (Join-Path $evidenceDirectory "g27local.elf")
        if ($LASTEXITCODE -ne 0) { throw "external Phase 27G ELF inspection failed" }
        if ($Phase27H) {
            Write-Host "--- external audit of guest-generated h27ifelse.elf ---" -ForegroundColor Cyan
            & $readelf -h -l (Join-Path $evidenceDirectory "h27ifelse.elf")
            & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
                --start-address=0x10001000 --stop-address=0x10001400 (Join-Path $evidenceDirectory "h27ifelse.elf")
            if ($LASTEXITCODE -ne 0) { throw "external Phase 27H ELF inspection failed" }
            if ($Phase27I) {
                Write-Host "--- external audit of guest-generated i27and.elf ---" -ForegroundColor Cyan
                & $readelf -h -l (Join-Path $evidenceDirectory "i27and.elf")
                & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
                    --start-address=0x10001000 --stop-address=0x10001200 (Join-Path $evidenceDirectory "i27and.elf")
                if ($LASTEXITCODE -ne 0) { throw "external Phase 27I AND inspection failed" }
                Write-Host "--- external audit of guest-generated i27or.elf ---" -ForegroundColor Cyan
                & $readelf -h -l (Join-Path $evidenceDirectory "i27or.elf")
                & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
                    --start-address=0x10001000 --stop-address=0x10001200 (Join-Path $evidenceDirectory "i27or.elf")
                if ($LASTEXITCODE -ne 0) { throw "external Phase 27I OR inspection failed" }
                if ($Phase27J) {
                    Write-Host "--- external audit of guest-generated j27sum.elf ---" -ForegroundColor Cyan
                    & $readelf -h -l (Join-Path $evidenceDirectory "j27sum.elf")
                    & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
                        --start-address=0x10001000 --stop-address=0x10001400 (Join-Path $evidenceDirectory "j27sum.elf")
                    if ($LASTEXITCODE -ne 0) { throw "external Phase 27J loop inspection failed" }
                    if ($Phase27K) {
                        foreach ($artifact in @("k27break.elf", "k27continue.elf", "k27combined.elf")) {
                            Write-Host "--- external audit of guest-generated $artifact ---" -ForegroundColor Cyan
                            & $readelf -h -l (Join-Path $evidenceDirectory $artifact)
                            & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
                                --start-address=0x10001000 --stop-address=0x10001600 (Join-Path $evidenceDirectory $artifact)
                            if ($LASTEXITCODE -ne 0) { throw "external Phase 27K ELF inspection failed: $artifact" }
                        }
                        if ($Phase27L) {
                            Write-Host "--- external audit of guest-generated l27primary.elf ---" -ForegroundColor Cyan
                            & $readelf -h -l (Join-Path $evidenceDirectory "l27primary.elf")
                            & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
                                --start-address=0x10001000 --stop-address=0x10001600 (Join-Path $evidenceDirectory "l27primary.elf")
                            if ($LASTEXITCODE -ne 0) { throw "external Phase 27L ELF inspection failed" }
                            if ($Phase27M) {
                                Write-Host "--- external audit of guest-generated m27primary.elf ---" -ForegroundColor Cyan
                                & $readelf -h -l (Join-Path $evidenceDirectory "m27primary.elf")
                                & $objdump -D -Mintel -b binary -m i386:x86-64 --adjust-vma=0x10000000 `
                                    --start-address=0x10001000 --stop-address=0x10001600 (Join-Path $evidenceDirectory "m27primary.elf")
                                if ($LASTEXITCODE -ne 0) { throw "external Phase 27M ELF inspection failed" }
                                if ($Phase27R) {
                                    Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O/27P/27Q/27R QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
                                } elseif ($Phase27Q) {
                                    Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O/27P/27Q QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
                                } elseif ($Phase27O) {
                                    Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N/27O QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
                                } elseif ($Phase27N) {
                                    Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M/27N QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
                                } else {
                                    Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L/27M QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
                                }
                            } else {
                                Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K/27L QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
                            }
                        } else {
                            Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J/27K QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
                        }
                    } else {
                        Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I/27J QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
                    }
                } else {
                    Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H/27I QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
                }
            } else {
                Write-Host "Phase 27B/27C/27D/27E/27F/27G/27H QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
            }
        } else {
            Write-Host "Phase 27B/27C/27D/27E/27F/27G QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
        }
    } elseif ($Phase27F) {
        Write-Host "Phase 27B/27C/27D/27E/27F QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } elseif ($Phase27E) {
        Write-Host "Phase 27B/27C/27D/27E QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    } else {
        Write-Host "Phase 27B/27C/27D QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
    }
}
finally {
    if ($null -eq $oldExtraCFlags) { Remove-Item Env:EXTRA_CFLAGS -ErrorAction SilentlyContinue }
    else { $env:EXTRA_CFLAGS = $oldExtraCFlags }

    # QEMU may release the FAT image handles just after it exits. Retry the
    # exact generated paths so a proof run never leaves build artifacts in ESP.
    Start-Sleep -Milliseconds 250
    foreach ($relativePath in @(
        "r42.c", "r41.c", "bad.c", "d27a.c", "d27b.c", "d27c.c", "r42.elf", "r42b.elf", "r41.elf", "bad.elf", "d27a.elf", "d27b.elf", "d27c.elf",
        "p27magic.elf", "p27arch.elf", "p27entry.elf", "p27out.elf", "p27trunc.elf",
        "p27bnd.elf", "p27addr.elf",
        "g27expr.c", "g27local.c", "g27assn.c", "g27preca.c", "g27precb.c", "g27unary.c", "g27logs.c", "g27unknown.c", "g27duplicate.c",
        "h27eq.c", "h27eqfalse.c", "h27cmp.c", "h27if.c", "h27suppress.c", "h27ifelse.c", "h27else.c", "h27nested.c", "h27truthy.c", "h27falsy.c", "h27assign.c", "h27missing.c", "h27invalid.c",
        "i27and11.c", "i27and10.c", "i27and01.c", "i27and00.c", "i27or11.c", "i27or10.c", "i27or01.c", "i27or00.c", "i27canonicaland.c", "i27canonicalor.c", "i27preca.c", "i27precb.c", "i27precc.c", "i27andif.c", "i27orif.c", "i27mixed.c", "i27nested.c", "i27assign.c", "i27shortand.c", "i27shortor.c", "i27invalid.c", "i27singleand.c", "i27singleor.c",
        "j27basic.c", "j27sum.c", "j27zero.c", "j27reeval.c", "j27logical.c", "j27logical_or.c", "j27ifwhile.c", "j27whileif.c", "j27nested.c", "j27bodydecl.c", "j27calls.c", "j27runtime1.c", "j27runtime2.c", "j27return.c", "j27invalid_empty.c", "j27invalid_relational.c", "j27missing.c",
        "k27basic.c", "k27continue.c", "k27break_if.c", "k27continue_if.c", "k27combined.c", "k27skip_tail.c", "k27break_tail.c", "k27nested_break.c", "k27nested_continue.c", "k27host_continue.c", "k27host_break.c", "k27break_outside.c", "k27continue_outside.c", "k27invalid_break.c", "k27invalid_continue.c", "k27missing_break_return.c", "k27missing_continue_return.c", "k27capacity.c",
        "l27zero.c", "l27one.c", "l27multi.c", "l27four.c", "l27nested.c", "l27expr.c", "l27condition.c", "l27loop.c", "l27if.c", "l27control.c", "l27forward.c", "l27backward.c", "l27isolation.c", "l27param.c", "l27entry.c", "l27missing.c", "l27duplicate_param.c", "l27duplicate_function.c", "l27param_limit.c", "l27arg_count.c", "l27unknown.c", "l27recursion.c",
        "m27recursive.c", "m27local.c", "m27param.c", "m27control.c", "m27loop.c", "m27nested.c", "m27expression.c", "m27mutual.c", "m27boundary.c", "m27overboundary.c", "m27deep.c",
        "r27local.c", "r27global.c", "r27array.c", "r27dynamic.c", "r27oob.c", "r27copy.c", "r27assign.c", "r27param.c", "r27recursive.c", "r27invalid_address.c", "r27invalid_deref.c", "r27invalid_decay.c", "r27invalid_arithmetic.c", "r27invalid_type.c", "r27invalid_uninitialized.c", "r27invalid_global.c", "r27sig_main.c", "r27sig_math.c",
        "s27walk.c", "s27store.c", "s27retreat.c", "s27middle.c", "s27equality.c", "s27copy.c", "s27param_isolation.c", "s27condition.c", "s27onepast.c", "s27onepast_deref.c", "s27beyond.c", "s27before.c", "s27scalar.c", "s27adjacent.c", "s27scalar_adjacent.c", "s27overflow.c", "s27type.c", "s27raw.c", "s27rvalue.c", "s27recursion.c", "s27deep.c", "s27global.c", "s27sig_main.c", "s27sig_math.c",
        "l27primary.elf", "l27deta.elf", "l27detb.elf", "m27primary.elf", "m27deta.elf", "m27detb.elf",
        "g27expr.elf", "g27local.elf", "g27assn.elf", "g27preca.elf", "g27precb.elf", "g27unary.elf", "g27logs.elf", "g27unknown.elf", "g27duplicate.elf", "g27deta.elf", "g27detb.elf", "g27reco.elf",
        "h27eq.elf", "h27eqfalse.elf", "h27cmp.elf", "h27if.elf", "h27suppress.elf", "h27ifelse.elf", "h27else.elf", "h27nested.elf", "h27truthy.elf", "h27falsy.elf", "h27assign.elf", "h27missing.elf", "h27invalid.elf", "h27deta.elf", "h27detb.elf", "h27reco.elf",
        "i27and11.elf", "i27and10.elf", "i27and01.elf", "i27and00.elf", "i27or11.elf", "i27or10.elf", "i27or01.elf", "i27or00.elf", "i27canonicaland.elf", "i27canonicalor.elf", "i27preca.elf", "i27precb.elf", "i27precc.elf", "i27andif.elf", "i27orif.elf", "i27mixed.elf", "i27nested.elf", "i27assign.elf", "i27shortand.elf", "i27shortor.elf", "i27invalid.elf", "i27singleand.elf", "i27singleor.elf", "i27and.elf", "i27or.elf", "deta.elf", "detb.elf",
        "kernel.elf", "EFI/BOOT/BOOTX64.EFI", "NvVars")) {
        $target = Join-Path $espDirectory $relativePath
        for ($attempt = 0; $attempt -lt 5 -and (Test-Path -LiteralPath $target); ++$attempt) {
            Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue
            if (Test-Path -LiteralPath $target) { Start-Sleep -Milliseconds 100 }
        }
        if ($backups.ContainsKey($relativePath)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $backups[$relativePath] $target -Force
        }
    }
    if ($Phase27E -or $Phase27F) {
        foreach ($relativeDirectory in @("P27E", "Apps/DS27E")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27F) {
        foreach ($relativeDirectory in @("P27F", "Apps/DS27F")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27G) {
        foreach ($relativeDirectory in @("P27G", "Apps/DS27G")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27H) {
        foreach ($relativeDirectory in @("P27H", "Apps/DS27H")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27I) {
        foreach ($relativeDirectory in @("P27I", "Apps/DS27I")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27J) {
        foreach ($relativeDirectory in @("P27J", "Apps/DS27J")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27K) {
        foreach ($relativeDirectory in @("P27K", "Apps/DS27K")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27L) {
        foreach ($relativeDirectory in @("P27L", "Apps/DS27L")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27M) {
        foreach ($relativeDirectory in @("P27M", "Apps/DS27M")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27N) {
        foreach ($relativeDirectory in @("P27N", "Apps/DS27N")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27O) {
        foreach ($relativeDirectory in @("P27O", "Apps/DS27O")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27P) {
        foreach ($relativeDirectory in @("P27P", "Apps/DS27P")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27Q) {
        foreach ($relativeDirectory in @("P27Q", "Apps/DS27Q")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27R) {
        foreach ($relativeDirectory in @("P27R", "Apps/DS27R")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27S) {
        foreach ($relativeDirectory in @("P27S", "Apps/DS27S")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27T) {
        foreach ($relativeDirectory in @("P27T", "Apps/DS27T")) {
            $target = Join-Path $espDirectory $relativeDirectory
            if (Test-Path -LiteralPath $target -PathType Container) {
                Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
            }
            if ($directoryBackups.ContainsKey($relativeDirectory)) {
                New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
                Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
            }
        }
    }
    if ($Phase27U) {
        $relativeDirectory = "P27U"
        $target = Join-Path $espDirectory $relativeDirectory
        if (Test-Path -LiteralPath $target -PathType Container) {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($directoryBackups.ContainsKey($relativeDirectory)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
        }
    }
    if ($Phase27W) {
        $relativeDirectory = "P27W"
        $target = Join-Path $espDirectory $relativeDirectory
        if (Test-Path -LiteralPath $target -PathType Container) {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($directoryBackups.ContainsKey($relativeDirectory)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
        }
    }
    if ($Phase27X) {
        $relativeDirectory = "P27X"
        $target = Join-Path $espDirectory $relativeDirectory
        if (Test-Path -LiteralPath $target -PathType Container) {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($directoryBackups.ContainsKey($relativeDirectory)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
        }
    }
    if ($Phase27Y) {
        $relativeDirectory = "P27Y"
        $target = Join-Path $espDirectory $relativeDirectory
        if (Test-Path -LiteralPath $target -PathType Container) {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($directoryBackups.ContainsKey($relativeDirectory)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
        }
    }
    if ($Phase27Z) {
        $relativeDirectory = "P27Z"
        $target = Join-Path $espDirectory $relativeDirectory
        if (Test-Path -LiteralPath $target -PathType Container) {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($directoryBackups.ContainsKey($relativeDirectory)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
        }
    }
    if ($Phase28A) {
        $relativeDirectory = "P28A"
        $target = Join-Path $espDirectory $relativeDirectory
        if (Test-Path -LiteralPath $target -PathType Container) {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($directoryBackups.ContainsKey($relativeDirectory)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
        }
    }
    if ($Phase28B) {
        $relativeDirectory = "P28B"
        $target = Join-Path $espDirectory $relativeDirectory
        if (Test-Path -LiteralPath $target -PathType Container) {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($directoryBackups.ContainsKey($relativeDirectory)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
        }
    }
    if ($Phase28C) {
        $relativeDirectory = "P28C"
        $target = Join-Path $espDirectory $relativeDirectory
        if (Test-Path -LiteralPath $target -PathType Container) {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($directoryBackups.ContainsKey($relativeDirectory)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
        }
    }
    if ($Phase28D) {
        $relativeDirectory = "P28D"
        $target = Join-Path $espDirectory $relativeDirectory
        if (Test-Path -LiteralPath $target -PathType Container) {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        }
        if ($directoryBackups.ContainsKey($relativeDirectory)) {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item $directoryBackups[$relativeDirectory] $target -Recurse -Force
        }
    }
    if (Test-Path $tempDirectory) { Remove-Item -LiteralPath $tempDirectory -Recurse -Force -ErrorAction SilentlyContinue }
}
