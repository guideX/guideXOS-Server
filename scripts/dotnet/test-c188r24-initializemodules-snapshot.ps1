$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r24-initializemodules-predicates.ps1')

$checks = 0
function Assert-R24Snapshot([bool]$Condition, [string]$Name) {
    $script:checks++
    if (-not $Condition) { throw "R24 CPU snapshot predicate failed: $Name" }
}

$addresses = [ordered]@{
    initializeModules='1000ad660'
    rhpRegisterOsModule='1000f94d0'
    createTypeManagers='1000ad820'
    initializeModulesCallReturn='1000f3196'
    rhpRegisterCallReturn='1000ad688'
    createTypeManagersCall='1000ad6a1'
    osModuleHandle='100000000'
}
$qmp = ConvertFrom-C188R24QmpResponse '{"return":{"running":false,"status":"paused"}}'
Assert-R24Snapshot (-not [bool]$qmp.return.running -and $qmp.return.status -eq 'paused') 'QMP paused response parses'
$badQmpRejected = $false
try { $null = ConvertFrom-C188R24QmpResponse 'not-json' } catch { $badQmpRejected = $true }
Assert-R24Snapshot $badQmpRejected 'malformed QMP responses fail closed'

$entry = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000AD660 RAX=0000000100000000' $addresses
Assert-R24Snapshot ($entry.classification -eq 'initializer-entered-before-registration-return' -and $entry.initializerEntered) 'initializer entry is independently observable'
$importEntry = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000F94D0 RAX=0000000100000000' $addresses
Assert-R24Snapshot ($importEntry.rhpRegisterEntered -and -not $importEntry.registrationReturned) 'native import entry is distinguishable from its return'
$alloc = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000F94E5 RAX=0000000100000000' $addresses
Assert-R24Snapshot ($alloc.classification -eq 'rhp-register-native-allocation-in-progress') 'native allocation call boundary is classified'
$allocReturned = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000F94F2 RAX=0000000000010000' $addresses
Assert-R24Snapshot ($allocReturned.classification -eq 'rhp-register-allocation-succeeded-before-publication') 'successful allocator result precedes publication'
$publication = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000F9510 RAX=0000000000010000' $addresses
Assert-R24Snapshot ($publication.classification -eq 'rhp-register-list-publication-in-progress' -and -not $publication.registrationPublished) 'CAS publication in progress is not reported complete'
$published = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000F9522 RAX=0000000000010000' $addresses
Assert-R24Snapshot ($published.registrationPublished -and -not $published.registrationReturned) 'published list entry is separated from function return'
$allocFailure = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000F952B RAX=0000000000000000' $addresses
Assert-R24Snapshot ($allocFailure.classification -eq 'rhp-register-allocation-failure-return-path' -and -not $allocFailure.registrationPublished) 'null allocation return remains a failure'
$returnSuccess = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000AD688 RAX=0000000100000000' $addresses
Assert-R24Snapshot ($returnSuccess.registrationReturned -and $returnSuccess.registrationSuccessObserved) 'caller continuation with expected handle proves M5 and registration success'
$returnNull = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000AD688 RAX=0000000000000000' $addresses
Assert-R24Snapshot ($returnNull.registrationReturned -and -not $returnNull.registrationSuccessObserved) 'caller continuation with null return is not success'
$manager = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000AD820 RAX=0000000100000000' $addresses
Assert-R24Snapshot ($manager.classification -eq 'type-manager-helper-current' -and $manager.registrationReturned) 'type-manager entry proves initializer continued past import'
$initializerReturned = Get-C188R24CpuBoundaryEvidence 'RIP=00000001000F3196 RAX=0000000100000000' $addresses
Assert-R24Snapshot ($initializerReturned.initializerReturned -and $initializerReturned.classification -eq 'initializer-returned-to-shared-helper') 'shared helper return continuation proves M7'
$unknown = Get-C188R24CpuBoundaryEvidence 'RAX=0000000000000000' $addresses
Assert-R24Snapshot ($unknown.classification -eq 'cpu-rip-unavailable' -and -not $unknown.initializerEntered) 'missing RIP remains unclassified'

Write-Output "R24_INITIALIZEMODULES_SNAPSHOT_ASSERTIONS=$checks RESULT=PASS"
