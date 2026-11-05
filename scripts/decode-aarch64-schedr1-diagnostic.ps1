[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$InputPath,
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$InputPath = [IO.Path]::GetFullPath($InputPath)
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path (Split-Path -Parent $InputPath) 'diagnostic-record.json'
} else {
    $OutputPath = [IO.Path]::GetFullPath($OutputPath)
}
$bytes = [IO.File]::ReadAllBytes($InputPath)
if ($bytes.Length -ne 216) { throw "Expected a 216-byte SCHEDR1 record; found $($bytes.Length)." }
function U32([int]$Offset) { return [BitConverter]::ToUInt32($bytes, $Offset) }
function U64([int]$Offset) { return [BitConverter]::ToUInt64($bytes, $Offset) }

$magic = U64 0
$version = U32 8
$declaredSize = U32 12
if ($magic -ne [UInt64]0x4758534348445231 -or $version -ne 1 -or $declaredSize -ne 216) {
    throw 'SCHEDR1 diagnostic magic, version, or declared size is invalid.'
}
function StageName([uint32]$Value) {
    switch ($Value) {
        0 { return 'NONE' }
        1 { return 'SCHED_START_ENTER' }
        2 { return 'SCHED_START_PRE_IRQ_RESTORE' }
        3 { return 'SCHED_START_POST_IRQ_RESTORE' }
        4 { return 'SCHED_START_RETURN' }
        5 { return 'TIMER_ENABLED' }
        10 { return 'FS_OPEN_BEGIN' }
        11 { return 'FS_OPEN_LOGGED' }
        12 { return 'FS_HANDLE_PUBLISHED' }
        13 { return 'FS_OPEN_RETURNED' }
        20 { return 'IRQ_ENTRY' }
        21 { return 'IRQ_ACKED' }
        22 { return 'IRQ_CALLBACK_DONE' }
        23 { return 'IRQ_EOI_DONE' }
        30 { return 'CONTINUATION_RESUMED' }
        default { return 'UNKNOWN' }
    }
}
$stage = U32 20
$originStage = U32 24
$record = [ordered]@{
    magic = ('0x{0:x16}' -f $magic)
    version = $version
    size = $declaredSize
    sequence = U32 16
    sequence_consistent = ((U32 16) % 2) -eq 0
    stage = $stage
    stage_name = StageName $stage
    irq_origin_stage = $originStage
    irq_origin_stage_name = StageName $originStage
    irq_count = U32 28
    valid_acknowledgements = U32 32
    EOIs = U32 36
    invalid_acknowledgements = U32 40
    last_irq_id = U32 44
    last_acknowledgement = ('0x{0:x}' -f (U32 48))
    interrupted_task_id = U32 52
    current_task_id = U32 56
    current_task_state = U32 60
    runnable_tasks = U32 64
    task_count = U32 68
    destination_valid = [bool](U32 72)
    current_task_pointer = ('0x{0:x}' -f (U64 80))
    interrupted_ELR = ('0x{0:x}' -f (U64 88))
    interrupted_SPSR = ('0x{0:x}' -f (U64 96))
    interrupted_SP = ('0x{0:x}' -f (U64 104))
    DAIF = ('0x{0:x}' -f (U64 112))
    destination_SP = ('0x{0:x}' -f (U64 120))
    destination_PC = ('0x{0:x}' -f (U64 128))
    destination_PSTATE = ('0x{0:x}' -f (U64 136))
    timer_ticks = U64 144
    context_switches = U64 152
    preemptions = U64 160
    architectural_timer_count = U64 168
    filesystem_reads = U64 176
    filesystem_enumerations = U64 184
    filesystem_failures = U64 192
    filesystem_stage = U32 200
    filesystem_stage_name = StageName (U32 200)
    filesystem_handle = U32 204
    unexpected_irqs = U32 208
    exceptions = U32 212
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Get-Content -LiteralPath $OutputPath -Raw
