[CmdletBinding()]
param(
    [string]$CxxPath = '',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($CxxPath)) {
    $CxxPath = 'C:\mingw64\bin\g++.exe'
    if (!(Test-Path -LiteralPath $CxxPath -PathType Leaf)) { $CxxPath = 'g++.exe' }
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'out\aarch64-el0-host-controls'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$outRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'out')).TrimEnd('\') + '\'
if (!$OutputDirectory.StartsWith($outRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing EL0 host-test output outside repository out/: $OutputDirectory"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Refusing to overwrite existing EL0 host-test output: $OutputDirectory"
}
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force

$testExe = Join-Path $OutputDirectory 'aarch64-el0-isolation-host-tests.exe'
$source = Join-Path $repoRoot 'tests\aarch64_el0_isolation_host_tests.cpp'
$implementation = Join-Path $repoRoot 'kernel\arch\arm64\phase2_el0.cpp'
$runtime = Join-Path $repoRoot 'kernel\arch\arm64\phase2_el0_runtime.cpp'
$studioElf = Join-Path $repoRoot 'kernel\arch\arm64\studio_elf_image.cpp'
$scheduler = Join-Path $repoRoot 'kernel\core\common_scheduler.cpp'
Write-Host '[1/2] Building production-backed AArch64 EL0 structural controls...' -ForegroundColor Yellow
& $CxxPath '-std=c++17' '-Wall' '-Wextra' '-O2' '-DGXOS_AARCH64_EL0_HOST_TEST' `
    '-I' (Join-Path $repoRoot 'kernel') `
    '-iquote' (Join-Path $repoRoot 'kernel\core\include') `
    '-iquote' (Join-Path $repoRoot 'sdk\include') `
    '-iquote' $repoRoot $source $implementation $runtime $studioElf $scheduler `
    (Join-Path $repoRoot 'kernel\core\compiler\elf_writer.cpp') `
    (Join-Path $repoRoot 'kernel\core\compiler\compiler_target.cpp') `
    (Join-Path $repoRoot 'kernel\core\native_architecture.cpp') `
    (Join-Path $repoRoot 'kernel\arch\arm64\compiler_backend.cpp') '-o' $testExe
if ($LASTEXITCODE -ne 0) { throw 'AArch64 EL0 host-test compilation failed.' }
& $testExe
if ($LASTEXITCODE -ne 0) { throw 'AArch64 EL0 host controls failed.' }

Write-Host '[2/2] Checking architectural and Studio safety source contracts...' -ForegroundColor Yellow
$mmu = [IO.File]::ReadAllText((Join-Path $repoRoot 'kernel\arch\arm64\phase2_mmu.cpp'))
$el0 = [IO.File]::ReadAllText($implementation)
$vectors = [IO.File]::ReadAllText((Join-Path $repoRoot 'kernel\arch\arm64\phase3_entry.S'))
$exceptions = [IO.File]::ReadAllText((Join-Path $repoRoot 'kernel\arch\arm64\phase3_exceptions.cpp'))
$runtimeText = [IO.File]::ReadAllText($runtime)
$el0RuntimeText = $el0 + $runtimeText
$schedulerText = [IO.File]::ReadAllText($scheduler)
$runGate = [IO.File]::ReadAllText((Join-Path $repoRoot 'kernel\core\include\kernel\aarch64_studio_run_gate.h'))
$nativeElf = [IO.File]::ReadAllText((Join-Path $repoRoot 'kernel\core\native_elf_baremetal_arm64.inc'))

foreach ($required in @('UINT64_C(16) << 16', 'T0SZ=48-bit VA policy',
        'UINT64_C(1) << 23', 'EPD1: TTBR1 is intentionally unused')) {
    if (!$mmu.Contains($required)) { throw "MMU layout contract is missing: $required" }
}
foreach ($required in @('dsb ishst', 'msr ttbr0_el1', 'tlbi vmalle1', 'dsb ish', 'isb')) {
    if (!$mmu.Contains($required)) { throw "TTBR0 full-invalidation contract is missing: $required" }
}
foreach ($required in @('kApReadOnlyUser', 'kApReadWriteUser', 'kPxn', 'kUxn',
        'GXOS_AARCH64_EL0_PAGE_STACK', 'GXOS_AARCH64_EL0_FAULT_DATA_ABORT',
        'GXOS_AARCH64_EL0_FAULT_INSTRUCTION_ABORT',
        'GXOS_AARCH64_EL0_PROBE_DEVICE_MMIO', '0xf9400020',
        'phase2_el0_run_negative_probe', 'g_kernel_data_probe_sentinel',
        '(UINT64_C(3) << 6)', 'fault_spsr == kExpectedUserPstate')) {
    if (!$el0.Contains($required)) { throw "EL0 permission or fault contract is missing: $required" }
}
$phase4 = [IO.File]::ReadAllText((Join-Path $repoRoot 'kernel\arch\arm64\phase4_main.cpp'))
foreach ($required in @('AARCH64_EL0_PROBE_RECORD',
        'AARCH64_EL0_CRITICAL_ISOLATION_FAILURE',
        'subsequent-probes=stopped')) {
    if (!$phase4.Contains($required)) { throw "EL0 negative qualification contract is missing: $required" }
}
$kernelProbeFailure = $phase4.IndexOf('if (!kernel_memory_denied)', [StringComparison]::Ordinal)
$mmioProbeStart = $phase4.IndexOf('GXOS_AARCH64_EL0_PROBE_DEVICE_MMIO', [StringComparison]::Ordinal)
if ($kernelProbeFailure -lt 0 -or $mmioProbeStart -lt 0 -or $kernelProbeFailure -gt $mmioProbeStart) {
    throw 'Kernel-data negative failure no longer gates the later MMIO child probe.'
}
foreach ($required in @('.Lsync_el0', '.Lirq_el0', 'eret',
        '.Lrestore_el0_scheduled', 'msr     sp_el0, x9',
        'GXOS_AARCH64_EL0_CONTEXT_KERNEL_SP_OFFSET',
        'str     x18, [sp, #96]', 'ldr     x18, [sp, #96]',
        'msr     sp_el0, xzr')) {
    if (!$vectors.Contains($required) -and !$el0.Contains($required)) {
        throw "Lower-EL vector/return contract is missing: $required"
    }
}
if (!$exceptions.Contains('phase2_el0_dispatch_exception') -or
    !$exceptions.Contains('phase4_irq_dispatch(irq, frame)') -or
    !$exceptions.Contains('phase3_irq_acknowledge()') -or
    !$exceptions.Contains('phase3_irq_complete(acknowledgement)') -or
    !$runGate.Contains('GX_DEVELOPMENT_RUN_ERROR_LAUNCH_UNAVAILABLE') -or
    !$nativeElf.Contains('gxos_native_call_on_stack')) {
    throw 'Exception dispatch, Studio Run fail-closed status, or legacy EL1 NativeElf path changed unexpectedly.'
}
foreach ($required in @('phase2_el0_suspend_for_schedule', 'phase2_el0_resume_for_schedule',
        'phase2_el0_task_finish_preempted', 'GXOS_AARCH64_EL0_CONTEXT_CANCELLED',
        'phase3_timer_counter()', 'phase2_el0_task_finish_exception',
        'trusted_image_valid', 'execution_consumed >= task->execution_budget')) {
    if (!$el0RuntimeText.Contains($required)) { throw "Scheduled EL0 runtime contract is missing: $required" }
}
foreach ($required in @('phase2_el0_task_create_studio_elf', 'phase2_el0_task_create_studio_build',
        'current_artifact', 'identity->source_sha256', 'artifact.el0IntegerProfileValid',
        'make_studio_return_trampoline',
        'GXOS_AARCH64_STUDIO_ELF_TRAMPOLINE_VA', 'GXOS_AARCH64_EL0_EXIT_SERVICE',
        'phase2_el0_copy_owned_memory')) {
    if (!$el0RuntimeText.Contains($required)) { throw "RUNSAFE4-A compiled ELF lifecycle contract is missing: $required" }
}
foreach ($required in @('EXECUTION_TIMED_OUT', 'execution_consumed',
        'timer_interrupt_at', 'request_el0_cancel', 'reap_task', 'PHASE_SCHEDULED',
        '64-bit wrapping counter', 'el0_preemption_count', 'el0_timer_interrupt_count',
        'el0_resume_count')) {
    if (!$schedulerText.Contains($required)) { throw "Scheduler-owned EL0 lifecycle contract is missing: $required" }
}
foreach ($required in @('scheduled_counter_ticks', 'fault_esr', 'fault_far',
        'kernel_root_restored', 'user_page_count', 'page_table_page_count',
        'address_space_owner_id')) {
    if (!$el0RuntimeText.Contains($required)) { throw "EL0 guest evidence field is missing: $required" }
}
if ($el0RuntimeText.Contains('kernel::scheduler::task_count() != 0')) {
    throw 'EL0 runtime readiness must not require a task before the boot path creates its first scheduler task.'
}
foreach ($required in @('RUNSAFE3B] FINITE PASS', 'RUNSAFE3B] TIMEOUT PASS',
        'RUNSAFE3B] CANCEL PASS', 'RUNSAFE3B] FAULT PASS',
        'AARCH64_RUNSAFE3B_PASS')) {
    if (!$phase4.Contains($required)) { throw "RUNSAFE3-B guest acceptance contract is missing: $required" }
}
if ($phase4.Contains('reason=data_abort result=none ESR=0x')) {
    throw 'RUNSAFE3-B fault output must use the serial helper prefix exactly once.'
}
Write-Host 'AARCH64_EL0_SOURCE_CONTRACT_PASS (structural; no hardware permission claim)' -ForegroundColor Green
Write-Host "Host controls: $testExe" -ForegroundColor Cyan
