[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$runnerPath = Join-Path $PSScriptRoot 'run-aarch64-runsafe2-qac.ps1'
$tokens = $null
$parseErrors = $null
$runnerAst = [System.Management.Automation.Language.Parser]::ParseFile(
    $runnerPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) {
    throw "RUNSAFE3-B evidence runner has PowerShell parse errors: $($parseErrors -join '; ')"
}

foreach ($functionName in @(
    'Get-RecordField', 'Get-RunSafe3BField', 'Convert-RunSafe3BHex',
    'Assert-ScheduledEl0GuestEvidence')) {
    $functionAst = $runnerAst.Find({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $functionName
    }, $true)
    if (!$functionAst) { throw "RUNSAFE3-B evidence validator is missing $functionName." }
    Invoke-Expression $functionAst.Extent.Text
}

$script:scheduledSourceRevision = '0123456789abcdef0123456789abcdef01234567'
$kernelDescriptor = '0x0060000000000487'
$deviceDescriptor = '0x0060000000000403'
$kernelProbe = '[guideXOS] AARCH64_EL0_PROBE_RECORD probe=kernel-data target=0x0000000040000000 descriptor=' +
    $kernelDescriptor + ' child=0x0000000000000001 owner=0x0000000000000002 state=faulted access=denied-or-not-entered ' +
    'read-value=0x0000000000000000 ESR=0x000000009000000f EC=0x0000000000000024 DFSC=0x000000000000000f ' +
    'WnR=0x0000000000000000 ELR=0x0000000040000000 SPSR=0x00000000000003c0 FAR=0x0000000040000000 ' +
    'kernel-root=restored cleanup=PASS released-data-pages=2 released-table-pages=4'
$deviceProbe = '[guideXOS] AARCH64_EL0_PROBE_RECORD probe=gicd-mmio target=0x0000000008000000 descriptor=' +
    $deviceDescriptor + ' child=0x0000000000000003 owner=0x0000000000000002 state=faulted access=denied-or-not-entered ' +
    'read-value=0x0000000000000000 ESR=0x000000009000000f EC=0x0000000000000024 DFSC=0x000000000000000f ' +
    'WnR=0x0000000000000000 ELR=0x0000000040000000 SPSR=0x00000000000003c0 FAR=0x0000000008000000 ' +
    'kernel-root=restored cleanup=PASS released-data-pages=2 released-table-pages=4'
$ready = @(
    '[RUNSAFE3B] CHILD READY scenario=FINITE child=11 owner=2 task=21 address_space=31 space_owner=11 user_pages=2 page_table_pages=4 kernel_stack=0x0000000050000000+8192 user_sp=0x0000000040002000 stack_separate=1',
    '[RUNSAFE3B] CHILD READY scenario=TIMEOUT child=12 owner=2 task=22 address_space=32 space_owner=12 user_pages=2 page_table_pages=4 kernel_stack=0x0000000050002000+8192 user_sp=0x0000000040002000 stack_separate=1',
    '[RUNSAFE3B] CHILD READY scenario=CANCEL child=13 owner=2 task=23 address_space=33 space_owner=13 user_pages=2 page_table_pages=4 kernel_stack=0x0000000050004000+8192 user_sp=0x0000000040002000 stack_separate=1',
    '[RUNSAFE3B] CHILD READY scenario=FAULT child=14 owner=2 task=24 address_space=34 space_owner=14 user_pages=2 page_table_pages=4 kernel_stack=0x0000000050006000+8192 user_sp=0x0000000040002000 stack_separate=1'
)
$terminal = @(
    '[RUNSAFE3B] FINITE PASS child=11 owner=2 address_space=31 state=completed exit=42 scheduled_ticks=1000 budget_ticks=312500 preemptions=2 timer_irqs=2 entries=1 resumes=2 kernel_work_delta=12 released_data_pages=2 released_table_pages=4 root_restored=1 cleanup=1 reap=1 task_record_reclaimed=1 kernel_continues=1',
    '[RUNSAFE3B] TIMEOUT PASS child=12 owner=2 address_space=32 state=timed_out result=none budget_us=2000 budget_ticks=125000 consumed_ticks=125001 entries=1 payload_x9=0x000000000000fffd preemptions=1 timer_irqs=1 kernel_work_delta=12 released_data_pages=2 released_table_pages=4 no_svc=1 kernel_root_restored=1 cleanup=1 reap=1 task_record_reclaimed=1 kernel_continues=1',
    '[RUNSAFE3B] CANCEL PASS child=13 owner=2 address_space=33 requested_by=2 state=cancelled result=none payload_x0=0x0000000000000043 scheduled_ticks=6250 budget_ticks=625000000 entries=1 resumes=0 preemptions=1 timer_irqs=1 kernel_work_delta=12 released_data_pages=2 released_table_pages=4 kernel_root_restored=1 cleanup=1 reap=1 task_record_reclaimed=1 no_svc=1 kernel_continues=1',
    '[RUNSAFE3B] FAULT PASS child=14 owner=2 address_space=34 state=faulted reason=data_abort result=none ESR=0x000000009000000f FAR=0x0000000040000000 ELR=0x0000000040000004 SPSR=0x0000000000000340 preemptions=0 timer_irqs=0 kernel_work_delta=12 released_data_pages=2 released_table_pages=4 kernel_root_restored=1 cleanup=1 reap=1 task_record_reclaimed=1 kernel_continues=1'
)
$continuation = '[RUNSAFE3B] CONTINUATION PASS timer_delta=20 context_switch_delta=40 kernel_work_delta=60 unexpected_irqs=0 exceptions=0'
$final = 'AARCH64_RUNSAFE3B_PASS source=' + $script:scheduledSourceRevision +
    ' finite_preempts=2 timeout_enforced=1 cancellation_forced=1 fault_isolated=1 cleanup=1'
$log = @(
    'AARCH64_EL0_QUALIFICATION_PASS observed-exit=2a cleanup=PASS',
    'AARCH64_EL0_NEGATIVE_KERNEL_DATA_PASS', 'AARCH64_EL0_NEGATIVE_GICD_PASS',
    'AARCH64_EL0_NEGATIVE_CONTROLS_PASS', $kernelProbe, $deviceProbe
) + $ready + $terminal + @($continuation, $final)
$evidence = Assert-ScheduledEl0GuestEvidence ($log -join "`n")
if ($evidence.SourceRevision -ne $script:scheduledSourceRevision -or
    $evidence.FinitePreemptions -ne 2 -or $evidence.TimerDelta -ne 20 -or
    $evidence.ContextSwitchDelta -ne 40 -or $evidence.ExceptionCount -ne 0) {
    throw 'RUNSAFE3-B evidence parser returned unexpected values for a complete guest record.'
}

$malformed = ($log -join "`n").Replace(
    'ESR=0x000000009000000f', 'ESR=0x0x000000009000000f')
$rejected = $false
try { $null = Assert-ScheduledEl0GuestEvidence $malformed } catch { $rejected = $true }
if (!$rejected) { throw 'RUNSAFE3-B parser accepted a duplicated hexadecimal prefix.' }

Write-Host 'RUNSAFE3-B native evidence parser: PASS (hex formatting, four child outcomes, identity and continuation)' -ForegroundColor Green
