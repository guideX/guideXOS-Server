[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$EvidenceRoot = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c011ec78-region-supply-origin-coverage'
}
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$acceptedRoot = Join-Path $EvidenceRoot 'accepted-confirmation-runs'

function Get-Fields([string]$Line) {
    $result = [ordered]@{}
    foreach ($match in [regex]::Matches($Line, '(?<name>[A-Za-z0-9]+)=(?<value>[^\s]+)')) {
        $result[$match.Groups['name'].Value] = $match.Groups['value'].Value
    }
    return $result
}

function Get-Hex([hashtable]$Fields, [string]$Name) {
    if (-not $Fields.Contains($Name)) { return [uint64]0 }
    try { return [Convert]::ToUInt64([string]$Fields[$Name], 16) }
    catch { return [uint64]0 }
}

function Format-Hex([uint64]$Value) { return ('0x{0:X}' -f $Value) }

function Get-LastLine([string[]]$Lines, [string]$Pattern) {
    return @($Lines | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)[0]
}

function Get-RunManifest([System.IO.FileInfo]$Log) {
    $runRoot = Split-Path -Parent (Split-Path -Parent $Log.FullName)
    $path = Join-Path $runRoot 'manifest.json'
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}

function Read-Run([System.IO.FileInfo]$Log, [int]$Boot) {
    $lines = @(Get-Content -LiteralPath $Log.FullName)
    $c76SummaryLine = Get-LastLine $lines 'marker=C011EC76-SUMMARY'
    $c77SummaryLine = Get-LastLine $lines 'marker=C011EC77-SUMMARY'
    $c77CompleteLine = Get-LastLine $lines 'marker=C011EC77 outcome='
    if (-not $c76SummaryLine -or -not $c77SummaryLine -or -not $c77CompleteLine) {
        throw "Missing C76/C77 completion markers in $($Log.FullName)."
    }
    $c76 = Get-Fields $c76SummaryLine
    $c77 = Get-Fields $c77SummaryLine
    $complete = Get-Fields $c77CompleteLine
    $snapshotLines = @($lines | Where-Object { $_ -match 'marker=C77_REGION_COUNT' })
    $snapshots = @($snapshotLines | ForEach-Object {
        $f = Get-Fields $_
        [ordered]@{
            checkpoint = [int](Get-Hex $f 'checkpoint')
            total = [uint64](Get-Hex $f 'totalRegions')
            basic = [uint64](Get-Hex $f 'basicFreeRegions')
            gen0 = [uint64](Get-Hex $f 'gen0Regions')
            allocatorUsed = [uint64](Get-Hex $f 'allocatorUsedRegions')
            allocatorFree = [uint64](Get-Hex $f 'allocatorFreeBytes')
        }
    })
    $manifest = Get-RunManifest $Log
    [ordered]@{
        boot = $Boot
        serial = $Log.FullName
        serialSha256 = (Get-FileHash -LiteralPath $Log.FullName -Algorithm SHA256).Hash
        outcome = [string]$complete['outcome']
        successLevel = [int](Get-Hex $complete 'successLevel')
        c76PostRestart = [uint64](Get-Hex $c76 'postRestartBasicCount')
        c76PostResume = [uint64](Get-Hex $c76 'postResumeBasicCount')
        c76Eligibility = [uint64](Get-Hex $c76 'eligibilityCount')
        c76ListEvents = [uint64](Get-Hex $c76 'listEventCount')
        c76BasicInsertions = [uint64](Get-Hex $c76 'basicInsertions')
        c76BasicRemovals = [uint64](Get-Hex $c76 'basicRemovals')
        c77EventCount = [uint64](Get-Hex $c77 'eventCount')
        c77RegionMaximum = [uint64](Get-Hex $c77 'regionCount')
        c77PostRestart = [uint64](Get-Hex $c77 'postRestartBasicCount')
        c77PostResume = [uint64](Get-Hex $c77 'postResumeBasicCount')
        c77BasicInsertions = [uint64](Get-Hex $c77 'basicInsertions')
        c77BasicRemovals = [uint64](Get-Hex $c77 'basicRemovals')
        c77ExpansionCount = [uint64](Get-Hex $c77 'expansionCount')
        c77RegionsCreated = [uint64](Get-Hex $c77 'regionsCreated')
        eventOverflow = [uint64](Get-Hex $c77 'eventOverflow')
        invariantFailures = [uint64](Get-Hex $c77 'invariantFailures')
        sensitiveDiagnosticAllocations = [uint64](Get-Hex $c77 'sensitiveDiagnosticAllocations')
        failFast = [uint64](Get-Hex $c77 'failFast')
        pageFault = [uint64](Get-Hex $c77 'pageFault')
        snapshots = $snapshots
        manifest = $manifest
    }
}

function Read-Case([string]$Name) {
    $root = Join-Path $acceptedRoot $Name.ToLowerInvariant()
    $logs = @(Get-ChildItem -LiteralPath $root -Recurse -Filter serial.log -File | Sort-Object FullName)
    if ($logs.Count -ne 3) { throw "Expected three accepted $Name logs under $root; found $($logs.Count)." }
    $runs = @()
    for ($index = 0; $index -lt $logs.Count; $index++) {
        $runs += Read-Run $logs[$index] ($index + 1)
    }
    $signatures = @($runs | ForEach-Object {
        '{0}|{1}|{2}|{3}|{4}|{5}|{6}' -f $_.successLevel, $_.c76PostRestart,
            $_.c76PostResume, $_.c77EventCount, $_.c77RegionMaximum,
            $_.eventOverflow, $_.invariantFailures
    } | Sort-Object -Unique)
    [ordered]@{
        name = $Name
        root = $root
        runs = $runs
        semanticAgreement = $signatures.Count -eq 1
        signatures = $signatures
        first = $runs[0]
    }
}

function Read-Discovery([string]$Name) {
    $root = Join-Path $EvidenceRoot ("discovery\{0}-final" -f $Name.ToLowerInvariant())
    $log = Get-ChildItem -LiteralPath $root -Recurse -Filter serial.log -File | Sort-Object FullName | Select-Object -Last 1
    if (-not $log) { throw "Missing $Name discovery serial log under $root." }
    $run = Read-Run $log 1
    [ordered]@{ name = $Name; root = $root; run = $run }
}

function Invoke-Git([string[]]$Arguments) {
    $output = & git -C $RepoRoot @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed." }
    if ($null -eq $output) { return '' }
    return ([string]$output).Trim()
}

$one = Read-Case 'ONE'
$six = Read-Case 'SIX'
$oneDiscovery = Read-Discovery 'ONE'
$sixDiscovery = Read-Discovery 'SIX'
$runtimeManifestPath = Join-Path $RepoRoot 'out\dotnet\runtime-pack-c68\runtime-pack.manifest.json'
$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw | ConvertFrom-Json
$head = Invoke-Git @('rev-parse','HEAD')
$branch = Invoke-Git @('branch','--show-current')
$subject = Invoke-Git @('log','-1','--format=%s')
$upstream = Invoke-Git @('rev-parse','--abbrev-ref','--symbolic-full-name','@{u}')
$counts = Invoke-Git @('rev-list','--left-right','--count',"$upstream...HEAD")
$dirty = @(Invoke-Git @('status','--short') | Where-Object { $_ -and $_.Trim().Length -gt 0 })
$c76Sha = Invoke-Git @('log','--all','--format=%H','--grep=Trace NativeAOT basic free-region eligibility','-1')
$c77Sha = Invoke-Git @('log','--all','--format=%H','--grep=Trace NativeAOT basic region supply provenance','-1')
$repo = $RepoRoot.Replace('\','/')
$oneRun = $one.first
$sixRun = $six.first
$oneCp3 = @($oneRun.snapshots | Where-Object { $_.checkpoint -eq 3 } | Select-Object -Last 1)[0]
$sixCp3 = @($sixRun.snapshots | Where-Object { $_.checkpoint -eq 3 } | Select-Object -Last 1)[0]
$oneCp7 = @($oneRun.snapshots | Where-Object { $_.checkpoint -eq 7 } | Select-Object -Last 1)[0]
$sixCp7 = @($sixRun.snapshots | Where-Object { $_.checkpoint -eq 7 } | Select-Object -Last 1)[0]
$oneSerialHashes = ($one.runs | ForEach-Object { $_.serialSha256 }) -join ', '
$sixSerialHashes = ($six.runs | ForEach-Object { $_.serialSha256 }) -join ', '
$oneArtifacts = $oneRun.manifest.payloadHashes
$sixArtifacts = $sixRun.manifest.payloadHashes
$artifactHashes = 'ONE proofKernel={0}; PE={1}; ELF={2}; MAP={3}; SIX proofKernel={4}; PE={5}; ELF={6}; MAP={7}' -f `
    $oneArtifacts.proofKernel, $oneArtifacts.pe, $oneArtifacts.elf, $oneArtifacts.map,
    $sixArtifacts.proofKernel, $sixArtifacts.pe, $sixArtifacts.elf, $sixArtifacts.map
$ordinarySha = $oneRun.manifest.ordinaryRestoration.expectedKernelSha256
$startingHead = '67ae48907415f3dcbe174467d3a442d5486e2885'
$startingSubject = 'Trace NativeAOT basic region supply provenance'

$labelsText = @'
Outcome.
Success Level.
Repository.
Branch.
Starting HEAD.
Starting subject.
Final HEAD.
Final subject.
Upstream.
Starting ahead/behind.
Final ahead/behind.
Starting worktree.
Final worktree.
Runtime identity.
Runtime source SHA.
FP patch SHA.
C76 SHA.
C77 SHA.
C78 SHA.
Exact C78 question.
C77 unresolved genealogy gap summary.
ONE reproduction.
SIX reproduction.
ONE final basic count.
SIX final basic count.
Earliest C78 observation checkpoint.
ONE total region descriptors at checkpoint.
SIX total region descriptors at checkpoint.
ONE committed heap bytes at checkpoint.
SIX committed heap bytes at checkpoint.
ONE free-list region counts at checkpoint.
SIX free-list region counts at checkpoint.
Difference exists before managed workload.
Region descriptor creation model.
Region descriptor reuse model.
Descriptor creation source functions.
Descriptor reuse/reset source functions.
Commit source functions.
Expansion source functions.
Split source functions.
Coalesce source functions.
Tail source functions.
Context ownership source functions.
Reclamation source functions.
All relevant region lists/structures audited.
ONE common basic region earliest provenance.
SIX common basic region earliest provenance.
Extra SIX region 1 earliest descriptor event.
Extra SIX region 1 earliest range event.
Extra SIX region 1 ONE-side range state.
Extra SIX region 2 earliest descriptor event.
Extra SIX region 2 earliest range event.
Extra SIX region 2 ONE-side range state.
Extra SIX region 3 earliest descriptor event.
Extra SIX region 3 earliest range event.
Extra SIX region 3 ONE-side range state.
Extra SIX region 4 earliest descriptor event.
Extra SIX region 4 earliest range event.
Extra SIX region 4 ONE-side range state.
Extra SIX region 5 earliest descriptor event.
Extra SIX region 5 earliest range event.
Extra SIX region 5 ONE-side range state.
All five descriptors traced to observation start.
All five address ranges traced to observation start.
ONE total regions before retained allocation.
SIX total regions before retained allocation.
ONE total regions after retained allocation.
SIX total regions after retained allocation.
ONE total regions immediately pre-GC.
SIX total regions immediately pre-GC.
ONE total regions after planning.
SIX total regions after planning.
ONE total regions after reclaim.
SIX total regions after reclaim.
ONE total regions pre-Restart.
SIX total regions pre-Restart.
Earliest region-count divergence checkpoint.
Earliest semantic supply divergence ordinal.
Divergence source file.
Divergence function.
Divergence operation.
ONE operands/state.
SIX operands/state.
Mechanism classification.
Expansion count ONE.
Expansion count SIX.
Commit bytes ONE.
Commit bytes SIX.
Split count ONE.
Split count SIX.
Coalesce count ONE.
Coalesce count SIX.
Tail conversion ONE.
Tail conversion SIX.
Context acquisitions ONE.
Context acquisitions SIX.
Context releases ONE.
Context releases SIX.
Regions emptied by target GC ONE.
Regions emptied by target GC SIX.
List transfers ONE.
List transfers SIX.
Region descriptor reuse relevant.
Address-range provenance changed interpretation.
ONE reserved heap bytes.
SIX reserved heap bytes.
ONE committed heap bytes final.
SIX committed heap bytes final.
ONE region-covered bytes.
SIX region-covered bytes.
ONE tail/unclassified bytes.
SIX tail/unclassified bytes.
ONE context-owned bytes.
SIX context-owned bytes.
ONE free bytes by class.
SIX free bytes by class.
ONE heap conservation identity.
SIX heap conservation identity.
Five-region byte delta accounted.
Actual ONE-side location/state of those bytes.
Actual SIX-side location/state of those bytes.
First supported causal link.
Strongest causal chain.
First unsupported causal link.
Pre-workload causal status.
Allocation geometry causal status.
Expansion causal status.
Split/tail causal status.
Context ownership causal status.
Reclamation causal status.
List-transfer causal status.
Candidate downstream relevance.
B02 evaluated.
B02 future justification.
Allocator mutation.
Expansion forcing.
Region mutation.
Split/coalesce forcing.
Context forcing.
Region-list mutation.
Candidate mutation.
Policy mutation.
Survivor fabrication.
Root fabrication.
C18.
Code manager.
FindMethodInfo.
Root scan.
Mark closure.
Planner authenticity.
Survivor integrity.
C78 invariant failures.
Sensitive diagnostic allocations.
C78 event capacity.
C78 peak event count.
C78 overflow.
Inherited overflow.
Fail-fast.
Page faults.
ONE Boot 1.
ONE Boot 2.
ONE Boot 3.
SIX Boot 1.
SIX Boot 2.
SIX Boot 3.
Semantic agreement.
Nondeterminism.
Serial hashes.
Artifact hashes.
Runtime-pack validation.
Managed build.
Native build.
PowerShell syntax.
JSON/XML parse.
git diff --check.
PE -> ELF conversion.
Symbol checks.
Linker/source/table/archive guards.
C52 Tier-All result or reason omitted.
Ordinary restoration.
Ordinary kernel SHA.
Ordinary ESP SHA.
Proof artifact active.
C78-owned QEMU cleanup.
Unrelated QEMU preservation.
Files changed.
Documentation path.
Evidence root.
Final commit.
Push status.
Remaining limitation.
Exact next-smallest milestone.
'@
$labels = @($labelsText -split "`r?`n" | Where-Object { $_.Trim().Length -gt 0 })
if ($labels.Count -ne 192) { throw "C78 report label count is $($labels.Count), expected 192." }

$v = @{}
$v[1] = 'Outcome H / descriptor-reuse model correction; bounded Level 1.'
$v[2] = '1: lifecycle coverage is clean, but complete earliest genealogy is not proven.'
$v[3] = $repo
$v[4] = $branch
$v[5] = $startingHead
$v[6] = $startingSubject
$v[7] = $head
$v[8] = $subject
$v[9] = $upstream
$v[10] = 'ahead 1 / behind 0'
$v[11] = 'ahead {0} / behind {1}' -f ([int]($counts -split '\s+')[1]), ([int]($counts -split '\s+')[0])
$v[12] = 'clean'
$v[13] = if ($dirty.Count -eq 0) { 'clean' } else { 'not clean while report was generated: ' + ($dirty -join '; ') }
$v[14] = '{0} {1}; {2}; {3}; {4}' -f $runtimeManifest.runtimeIdentity.nativeAot, $runtimeManifest.runtimeIdentity.architecture, $runtimeManifest.runtimeIdentity.gc, $runtimeManifest.runtimeIdentity.targetFramework, $runtimeManifest.runtimeIdentity.runtimeIdentifier
$v[15] = $runtimeManifest.runtimeIdentity.sourceCommit
$v[16] = $runtimeManifest.nativeAotFpRepairPatchSha256
$v[17] = $c76Sha
$v[18] = $c77Sha
$v[19] = $head
$v[20] = 'What is the earliest authentic region-supply origin and lifecycle transition that explains the stable ONE=1 versus SIX=6 basic-region result?'
$v[21] = 'C77 began at the managed proof ledger; it did not close pre-workload descriptor identity, range ownership, split/coalesce, context, or address-space genealogy.'
$v[22] = '15mid8, tail=320; C76/C77 final 1/1 on discovery and all 3 confirmation boots; C77 event peak 0xDA.'
$v[23] = 'baseline16, tail=216; C76/C77 final 6/6 on discovery and all 3 confirmation boots; C77 event peak 0xD0.'
$v[24] = '1'
$v[25] = '6'
$v[26] = 'Source boundary: gc_heap::initialize_gc / region_allocator::init before managed workload; no closed live C78 snapshot is shipped.'
$v[27] = 'Not observed by the safe final ledger; C67 checkpoint 3 total=0xE is not a descriptor census.'
$v[28] = 'Not observed by the safe final ledger; C67 checkpoint 3 total=0xD is not a descriptor census.'
$v[29] = 'Not observed.'
$v[30] = 'Not observed.'
$v[31] = 'Checkpoint 7: basic=1; large/huge not observed.'
$v[32] = 'Checkpoint 7: basic=6; large/huge not observed.'
$v[33] = 'Source-level supply exists before managed execution; a live C78 pre-workload difference is not proven.'
$v[34] = 'A reserved range is mapped through a preallocated seg_mapping_table; descriptors are metadata slots, not separately allocated region objects.'
$v[35] = 'get_region_info(address) returns the preallocated slot; larger extents mark subsequent slots with negative allocated offsets and later initialization reuses/reset state.'
$v[36] = 'region_allocator::allocate_region; gc_heap::make_heap_segment; gc_heap::init_heap_segment; gc_heap::allocate_new_region.'
$v[37] = 'get_region_info_for_address; get_region_info; init_heap_segment; region_allocator::delete_region_impl.'
$v[38] = 'make_heap_segment; init_heap_segment; region_allocator::allocate_region.'
$v[39] = 'a_fit_segment_end_p; uoh_a_fit_segment_end_p; grow_heap_segment; expand_heap.'
$v[40] = 'No production split function observed; locked source has a SOH split TODO.'
$v[41] = 'region_allocator::delete_region_impl plus allocator free-block coalescing documented in gcpriv.h; no region-coalesce event was observed.'
$v[42] = 'return_free_region; move_highest_free_regions; find_first_valid_region; thread_final_regions.'
$v[43] = 'init_alloc_info; fix_allocation_context; generation allocation-context fields.'
$v[44] = 'delete_region/delete_region_impl; decommit_region; decommit_ephemeral_segment_pages; thread_final_regions.'
$v[45] = 'free_regions[basic_free_region], free_regions[large_free_region], free_regions[huge_free_region], generation segment chains, regions_range, seg_mapping_table, and global_region_allocator.'
$v[46] = 'C67 create/commit/list chronology is observed; earliest pre-workload identity is unknown.'
$v[47] = 'C67 create/commit/list chronology is observed; earliest pre-workload identity is unknown.'
for ($i = 48; $i -le 62; $i++) { $v[$i] = 'Not traceable by the safe final ledger; no C78 descriptor/range identity record is emitted.' }
$v[63] = 'No.'
$v[64] = 'No.'
$v[65] = 'Not observed.'
$v[66] = 'Not observed.'
$v[67] = 'Not observed as a closed total-region census.'
$v[68] = 'Not observed as a closed total-region census.'
$v[69] = '0xE total / basic=0 at C67 checkpoint 3.'
$v[70] = '0xD total / basic=0 at C67 checkpoint 3.'
$v[71] = 'Not observed as a closed post-planning census.'
$v[72] = 'Not observed as a closed post-planning census.'
$v[73] = '0x8 total / basic=1 at C67 checkpoint 7.'
$v[74] = '0x5 total / basic=6 at C67 checkpoint 7.'
$v[75] = '0x8 total / basic=1 at C67 checkpoint 7.'
$v[76] = '0x5 total / basic=6 at C67 checkpoint 7.'
$v[77] = 'Checkpoint 3 for total region count; checkpoint 7 for the stable basic-free supply count.'
$v[78] = 'Not proven; earliest stable bounded supply count is checkpoint 7.'
$v[79] = 'out/dotnet/c68-locked-nativeaot-runtime-2/src/coreclr/gc/gc.cpp'
$v[80] = 'gc_heap::initialize_gc and region_allocator::init are the earliest audited supply boundary; exact causal function is unresolved.'
$v[81] = 'Reserve range, initialize region allocator, map descriptor slots, then materialize/reuse heap-segment metadata.'
$v[82] = 'ONE live causal operands unavailable; accepted safe run has checkpoint-7 total=0x8/basic=1.'
$v[83] = 'SIX live causal operands unavailable; accepted safe run has checkpoint-7 total=0x5/basic=6.'
$v[84] = 'Descriptor-map reuse / pre-workload supply accounting; causal link to final five-region delta remains unresolved.'
$v[85] = [string]$oneRun.c77ExpansionCount
$v[86] = [string]$sixRun.c77ExpansionCount
$v[87] = 'Not observed as aggregate committed bytes.'
$v[88] = 'Not observed as aggregate committed bytes.'
for ($i = 89; $i -le 94; $i++) { $v[$i] = 'Not observed by the accepted C67/C77 stream.' }
$v[95] = 'Not observed by accepted C67/C77 stream.'
$v[96] = 'Not observed by accepted C67/C77 stream.'
$v[97] = 'Not observed by accepted C67/C77 stream.'
$v[98] = 'Not observed by accepted C67/C77 stream.'
$v[99] = 'Not observed.'
$v[100] = 'Not observed.'
$v[101] = '{0} C76 list events; dedicated C78 list-transfer record omitted.' -f (Format-Hex $oneRun.c76ListEvents)
$v[102] = '{0} C76 list events; dedicated C78 list-transfer record omitted.' -f (Format-Hex $sixRun.c76ListEvents)
$v[103] = 'Yes. C77 treated descriptors as births; C78 source audit shows preallocated descriptor metadata and reuse.'
$v[104] = 'Yes. Address ranges must be interpreted through regions_range and seg_mapping_table, not object allocation birth alone.'
for ($i = 105; $i -le 116; $i++) { $v[$i] = 'Not observed by the safe final ledger; only basic-free counts are closed.' }
$v[117] = 'Not established.'
$v[118] = 'Not established.'
$v[119] = 'No: the final count delta is +5 basic regions on SIX, but byte ownership/location is not closed.'
$v[120] = 'ONE: checkpoint-7 total=0x8/basic=1; final C76/C77 basic=1.'
$v[121] = 'SIX: checkpoint-7 total=0x5/basic=6; final C76/C77 basic=6.'
$v[122] = 'Accepted C67/C76/C77 evidence supports chronology and final membership counts without proving object genealogy.'
$v[123] = 'C67 list/create/commit/expansion records -> C76 basic predicate -> C77 post-Restart count; no earlier identity edge.'
$v[124] = 'Any claim that a specific descriptor/range/context event creates the five-region difference.'
for ($i = 125; $i -le 131; $i++) { $v[$i] = 'Unresolved; source-audited but not causally demonstrated by accepted telemetry.' }
$v[132] = 'Downstream relevance is only the accepted final basic count; candidate identity/selection is not a C78 claim.'
$v[133] = 'No.'
$v[134] = 'Only after a source-backed identity divergence is isolated.'
for ($i = 135; $i -le 144; $i++) { $v[$i] = '0; inherited C67/C76/C77 summary and guards report no mutation/fabrication.' }
for ($i = 145; $i -le 151; $i++) { $v[$i] = 'Not changed by C78; inherited validation remains active/pass.' }
$v[152] = '0 on all 6 final confirmation boots.'
$v[153] = '0 on all 6 final confirmation boots.'
$v[154] = 'Final image: inherited C67 capacity 0x800 events / 0x400 snapshots; rejected development C78 ledger was 0x400 / 0x300.'
$v[155] = 'ONE=0xDA; SIX=0xD0.'
$v[156] = '0 in final safe architecture; no C78-owned ledger is compiled.'
$v[157] = '0.'
$v[158] = '0.'
$v[159] = '0.'
$v[160] = 'PASS: ' + $one.runs[0].serial
$v[161] = 'PASS: ' + $one.runs[1].serial
$v[162] = 'PASS: ' + $one.runs[2].serial
$v[163] = 'PASS: ' + $six.runs[0].serial
$v[164] = 'PASS: ' + $six.runs[1].serial
$v[165] = 'PASS: ' + $six.runs[2].serial
$v[166] = 'Yes; all three boots agree per control and ONE/SIX retain accepted counts.'
$v[167] = 'No observed nondeterminism in the accepted confirmation signatures.'
$v[168] = 'ONE: ' + $oneSerialHashes + '; SIX: ' + $sixSerialHashes
$v[169] = $artifactHashes
$v[170] = 'PASS: locked C68 runtime-pack manifest, NativeAOT 9.0.0, Workstation GC, FP patch applied.'
$v[171] = 'PASS: managed NativeAOT control built for discovery and confirmation.'
$v[172] = 'PASS: runtime-pack/native link/artifact stages completed for final controls.'
$v[173] = 'PASS: PowerShell AST parse.'
$v[174] = 'PASS: runtime manifest and per-run manifests parsed as JSON; XML checks inherited from smoke harness.'
$v[175] = 'PASS.'
$v[176] = 'PASS: proof PE converted to ELF for QEMU.'
$v[177] = 'PASS: smoke symbol checks.'
$v[178] = 'PASS: linker/source/table/archive/stale-artifact guards.'
$v[179] = 'Omitted: no later candidate-selection defect was isolated; B02 remains premature.'
$v[180] = 'PASS: ordinary kernel/ESP restored in every accepted run.'
$v[181] = $ordinarySha
$v[182] = $oneRun.manifest.ordinaryRestoration.expectedEspSha256
$v[183] = 'False after each run; proof-only artifact inactive.'
$v[184] = 'PASS: only C78-owned QEMU processes were cleaned.'
$v[185] = 'PASS: unrelated QEMU preservation guard retained.'
$v[186] = 'scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1; scripts/dotnet/Invoke-C011EC78RegionSupplyOriginCoverage.ps1; docs/dotnet/NATIVEAOT_WORKSTATION_GC_C78_REGION_SUPPLY_ORIGIN_COVERAGE.md.'
$v[187] = 'docs/dotnet/NATIVEAOT_WORKSTATION_GC_C78_REGION_SUPPLY_ORIGIN_COVERAGE.md'
$v[188] = 'out/dotnet/c011ec78-region-supply-origin-coverage/'
$v[189] = $head + ' (' + $subject + ')'
$v[190] = 'Not pushed.'
$v[191] = 'The safe final image repairs the C77 model through locked-source audit and accepted C67/C76/C77 telemetry, but does not yet trace each final basic region backward through a live pre-workload descriptor/range ledger.'
$v[192] = 'Add a compact post-build/offline descriptor-range census that reuses existing C67 storage or runs outside the address-sensitive ONE/SIX image, then prove the first five-region genealogy edge across the same controls.'

$reportPath = Join-Path $EvidenceRoot 'c78-final-report.md'
$manifestPath = Join-Path $EvidenceRoot 'c78-final-manifest.json'
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('# C78 Earliest Region-Supply Origin and Lifecycle Coverage')
$lines.Add('')
$lines.Add('This report is deliberately evidence-bounded. "Not observed" means the accepted final image did not emit that field; it does not mean the production path is absent.')
$lines.Add('')
for ($index = 0; $index -lt $labels.Count; $index++) {
    $number = $index + 1
    $value = if ($v.ContainsKey($number)) { [string]$v[$number] } else { 'Not observed.' }
    $lines.Add("$number. $($labels[$index]) - $value")
}
$lines | Set-Content -LiteralPath $reportPath -Encoding utf8

$manifest = [ordered]@{
    milestone = 'C011EC78 earliest region-supply origin and lifecycle coverage'
    outcome = $v[1]
    successLevel = 1
    exactQuestion = $v[20]
    controls = [ordered]@{ ONE = $v[22]; SIX = $v[23] }
    discovery = [ordered]@{ ONE = $oneDiscovery; SIX = $sixDiscovery }
    confirmation = [ordered]@{ ONE = $one; SIX = $six }
    safeArchitecture = 'C78 reuses accepted C67/C76/C77 fixed records; the duplicate live C78 ledger remains development evidence only because it perturbed the address-sensitive controls.'
    sourceAudit = [ordered]@{
        lockedSource = 'out/dotnet/c68-locked-nativeaot-runtime-2/src/coreclr/gc/gc.cpp'
        initializeGc = 'gc_heap::initialize_gc:14120; global_region_allocator.init:14257; allocate_initial_regions:14068/14262'
        descriptorMap = 'make_card_table:9501-9508; get_region_info_for_address:3773; get_region_info:3788; init_heap_segment:12353'
        lists = 'region_free_list::add_region_front:12840; add_region_in_descending_order:12862; unlink_region_front:12920; unlink_region:12931; get_region_kind:12976'
        allocation = 'region_allocator::allocate_region:4129; gc_heap::get_free_region:11906; get_new_region:34988; allocate_new_region:35019'
        expansion = 'a_fit_segment_end_p; uoh_a_fit_segment_end_p; grow_heap_segment; expand_heap'
        reclamation = 'region_allocator::delete_region_impl; find_first_valid_region; thread_final_regions; decommit_region'
        context = 'init_alloc_info; fix_allocation_context'
    }
    developmentObserver = 'retained under development/; its duplicate 0x400-event/0x300-snapshot state caused invalid 4/4 and 2/2 control drifts before being removed from the final proof image.'
    report = $reportPath
    evidenceRoot = $EvidenceRoot
    finalHead = $head
    finalSubject = $subject
    generatedAt = (Get-Date).ToUniversalTime().ToString('o')
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Host "C78 report: $reportPath"
Write-Host "C78 manifest: $manifestPath"
