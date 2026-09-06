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
    [switch]$Phase27ROnly
)

$ErrorActionPreference = "Stop"
# Phase 27G includes the complete earlier integration chain.  The focused M
# mode deliberately keeps only the baseline C/D route plus the M smoke so a
# flaky optional earlier IDE repeat cannot mask the recursion proof.
if ($Phase27ROnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $false; $Phase27R = $true
} elseif ($Phase27QOnly) {
    $Phase27E = $false; $Phase27F = $false; $Phase27G = $false; $Phase27H = $false
    $Phase27I = $false; $Phase27J = $false; $Phase27K = $false; $Phase27L = $false
    $Phase27M = $false; $Phase27N = $false; $Phase27O = $false; $Phase27P = $false
    $Phase27Q = $true
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
}
# The Phase 27R IDE proof includes several guest build/run cycles. Give that
# workload enough time when callers use the script default, while preserving
# an explicitly longer timeout unchanged.
if ($Phase27R -and $TimeoutSeconds -lt 120) { $TimeoutSeconds = 120 }
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
                "phase27p_run_backend=PASS",
                "phase27p_project_open=PASS",
                "phase27p_source_enumeration=PASS",
                "phase27p_multi_file_documents=PASS",
                "phase27p_cold_build=PASS",
                "phase27p_no_change_build=PASS",
                "phase27p_single_file_invalidation=PASS",
                "phase27p_incremental_source_edit=PASS",
                "phase27p_source_restore=PASS",
                "phase27p_multi_file_invalidation=PASS",
                "phase27p_added_source=PASS",
                "phase27p_removed_source=PASS",
                "phase27p_orphan_object_ignored=PASS",
                "phase27p_source_rename=PASS",
                "phase27p_same_size_edit_invalidates=PASS",
                "phase27p_missing_object_rebuild=PASS",
                "phase27p_corrupt_object_rebuild=PASS",
                "phase27p_corrupt_code_rebuild=PASS",
                "phase27p_corrupt_relocation_rebuild=PASS",
                "phase27p_cold_counts=PASS",
                "phase27p_warm_counts=PASS",
                "phase27p_partial_counts=PASS",
                "phase27p_compile_skipped_on_hit=PASS",
                "phase27p_cached_undefined_symbol=PASS",
                "phase27p_cached_signature_validation=PASS",
                "phase27p_cached_link_failure_blocks_run=PASS",
                "phase27p_compile_failure_recovery=PASS",
                "phase27p_link_failure_recovery=PASS",
                "phase27p_link_from_persisted_objects=PASS",
                "phase27p_full_cache_execution=PASS",
                "phase27p_ide_cold_build=PASS",
                "phase27p_ide_warm_build=PASS",
                "phase27p_ide_partial_rebuild=PASS",
                "phase27p_ide_restore=PASS",
                "phase27p_ide_corrupt_cache_recovery=PASS",
                "phase27p_changed_source_never_uses_stale_object=PASS",
                "phase27p_cached_shared_global=PASS",
                "phase27p_cached_recursion=PASS",
                "phase27p_cached_mutual_recursion=PASS",
                "phase27p_cached_depth_guard=PASS",
                "phase27p_cached_segment_permissions=PASS",
                "phase27p_object_deterministic=PASS",
                "phase27p_cold_warm_elf_identical=PASS",
                "phase27p_restore_deterministic=PASS",
                "phase27p_object_order_deterministic=PASS",
                "phase27p_object_header=PASS",
                "phase27p_target_identity=PASS",
                "phase27p_source_hash_validation=PASS",
                "phase27p_compiler_version_invalidation=PASS",
                "phase27p_object_path_identity=PASS",
                "phase27p_object_roundtrip=PASS",
                "phase27p_object_version_invalidation=PASS",
                "phase27p_wrong_arch_rebuild=PASS",
                "phase27p_wrong_abi_rebuild=PASS",
                "phase27p_single_file_cache=PASS",
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
                "phase27r_no_array_decay=PASS",
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
                "phase27r_pointer_arithmetic_rejected=PASS",
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
        $missingMarkers = @($requiredMarkers | Where-Object { $serial -notmatch [regex]::Escape($_) })
        if ($missingMarkers.Count -ne 0) {
            Write-Host "QEMU boot $runNumber missed required compiler/IDE markers: $($missingMarkers -join ', ')" -ForegroundColor Red
            if ($Phase27E -or $Phase27F) {
                $serial -split "`r?`n" | Where-Object { $_ -match "phase27e|phase27f|phase27g|phase27h|phase27i|phase27j|phase27k|phase27l|phase27m|phase27n|phase27o|phase27p|phase27q|phase27r|Phase 27E|Phase 27F|Phase 27G|Phase 27H|Phase 27I|Phase 27J|Phase 27K|Phase 27L|Phase 27M|Phase 27N|Phase 27O|Phase 27P|Phase 27Q|Phase 27R" } | ForEach-Object { Write-Host $_ }
            }
            if ($serial) { Write-Host $serial }
            if ($stderr) { Write-Host $stderr }
            throw "QEMU compiler/IDE proof failed on boot $runNumber (exit $($process.ExitCode))"
        }

        Write-Host "--- QEMU bare-metal compiler proof boot $runNumber ---" -ForegroundColor Cyan
        $serial -split "`r?`n" |
            Where-Object { $_ -notmatch "NativeElf: artifact_hex=" -and
                $_ -match "Compiler:|ELF Loader:|NativeElf:|phase27c|phase27d|phase27e|phase27f|phase27g|phase27h|phase27i|phase27j|phase27k|phase27l|phase27m|phase27n|phase27o|phase27p|phase27q|phase27r|^error:" } |
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
        if (!(Test-Path (Join-Path $developerStudioRoot "scripts/build-phase27p.ps1"))) {
            throw "Developer Studio Phase 27P build script is missing: $developerStudioRoot"
        }
        & powershell -ExecutionPolicy Bypass -File (Join-Path $developerStudioRoot "scripts/build-phase27p.ps1") -ServerRoot $root
        if ($LASTEXITCODE -ne 0) { throw "Developer Studio Phase 27P proof app build failed" }
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
    Push-Location $kernelDirectory
    try {
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $kernelDirectory "build/amd64/obj/core/main.o")
        if ($Phase27E -or $Phase27F -or $Phase27M -or $Phase27O -or $Phase27P -or $Phase27Q -or $Phase27R) {
            Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $kernelDirectory "build/amd64/obj/core/native_elf/native_elf_smoke.o")
        }
        & $make all ARCH=amd64 "EXTRA_CFLAGS=$env:EXTRA_CFLAGS" "MBEDTLS_GUIDEXOS_IMPORT_STATE_DEPS="
        if ($LASTEXITCODE -ne 0) { throw "kernel build failed" }
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
        New-Item -ItemType Directory -Force -Path (Join-Path $espDirectory "Apps") | Out-Null
        Copy-Item $phase27pAppDirectory (Join-Path $espDirectory "Apps/DS27P") -Recurse -Force
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
    foreach ($artifact in @("r42", "d27a", "d27b", "d27c")) {
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
    if ($Phase27ROnly) {
        Write-Host "Phase 27R focused QEMU proof completed across $BootCount fresh boot(s)." -ForegroundColor Green
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
    if (Test-Path $tempDirectory) { Remove-Item -LiteralPath $tempDirectory -Recurse -Force -ErrorAction SilentlyContinue }
}
