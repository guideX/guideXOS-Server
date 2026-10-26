function Test-C188R11NotesReady([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$') -and
        [regex]::IsMatch($SerialText,
        '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes generation=00000001 selector=00000004 kind=normal\r?$') -and
        [regex]::IsMatch($SerialText,
        '(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=00000001 window=[0-9A-Fa-f]{8} result=PASS\r?$')
}

function Get-C188R11NativeBoundary([string]$SerialText, [string]$Route,
    [string]$Stage = 'after-managed-return') {
    $routePattern = [regex]::Escape($Route)
    $stagePattern = [regex]::Escape($Stage)
    return [regex]::Match($SerialText,
        '(?m)^\[C188R11-NATIVE\] nseq=(?<nativeSeq>[0-9A-Fa-f]{8}) stage=' + $stagePattern +
        ' route=' + $routePattern +
        ' call=(?<call>[0-9A-Fa-f]{8}) corr=(?<correlation>[0-9A-Fa-f]{8}) id=4 gen=(?<generation>[0-9A-Fa-f]{8}) input=(?<input>[0-9A-Fa-f]{8}) module=(?<module>[0-9A-Fa-f]{8}) entryOff=(?<entryOffset>[0-9A-Fa-f]{16}) segments=(?<segments>[0-9A-Fa-f]{8}) span=(?<span>[0-9A-Fa-f]{16}) rc=(?<result>[0-9A-Fa-f]{8})\r?$')
}

function Test-C188R11NativeLaunchBoundary([string]$SerialText) {
    $after = Get-C188R11NativeBoundary $SerialText 'resident-launch'
    $before = Get-C188R11NativeBoundary $SerialText 'resident-launch' 'before-managed-entry'
    return $before.Success -and $after.Success -and
        $before.Groups['generation'].Value -eq '00000001' -and
        $before.Groups['input'].Value -eq '00000000' -and
        $before.Groups['result'].Value -eq 'FFFFFFFF' -and
        $after.Groups['generation'].Value -eq '00000001' -and
        $after.Groups['input'].Value -eq '00000000' -and
        $before.Groups['call'].Value -eq $after.Groups['call'].Value -and
        $before.Groups['correlation'].Value -eq $after.Groups['correlation'].Value -and
        $before.Groups['module'].Value -eq $after.Groups['module'].Value -and
        $before.Groups['entryOffset'].Value -eq $after.Groups['entryOffset'].Value -and
        $before.Groups['segments'].Value -eq $after.Groups['segments'].Value -and
        $before.Groups['span'].Value -eq $after.Groups['span'].Value -and
        [Convert]::ToUInt32($before.Groups['nativeSeq'].Value,16) -lt
            [Convert]::ToUInt32($after.Groups['nativeSeq'].Value,16) -and
        $after.Groups['module'].Value -ne '00000000' -and
        $after.Groups['entryOffset'].Value -ne '0000000000000000' -and
        $after.Groups['segments'].Value -ne '00000000' -and
        $after.Groups['span'].Value -ne '0000000000000000' -and
        $after.Groups['result'].Value -eq '00000000'
}

function Test-C188R11NativeInputBoundary([string]$SerialText, [string]$ExpectedResult = '') {
    $after = Get-C188R11NativeBoundary $SerialText 'input'
    $before = Get-C188R11NativeBoundary $SerialText 'input' 'before-managed-entry'
    return $before.Success -and $after.Success -and
        $before.Groups['input'].Value -eq '00000001' -and
        $after.Groups['input'].Value -eq '00000001' -and
        $before.Groups['module'].Value -ne '00000000' -and
        $before.Groups['result'].Value -eq 'FFFFFFFF' -and
        $before.Groups['call'].Value -eq $after.Groups['call'].Value -and
        $before.Groups['correlation'].Value -eq $after.Groups['correlation'].Value -and
        $before.Groups['generation'].Value -eq $after.Groups['generation'].Value -and
        $before.Groups['module'].Value -eq $after.Groups['module'].Value -and
        $before.Groups['entryOffset'].Value -eq $after.Groups['entryOffset'].Value -and
        $before.Groups['segments'].Value -eq $after.Groups['segments'].Value -and
        $before.Groups['span'].Value -eq $after.Groups['span'].Value -and
        [Convert]::ToUInt32($before.Groups['nativeSeq'].Value,16) -lt
            [Convert]::ToUInt32($after.Groups['nativeSeq'].Value,16) -and
        ([string]::IsNullOrEmpty($ExpectedResult) -or
            $after.Groups['result'].Value -ceq $ExpectedResult)
}

function Get-C188R11NativeInputResult([string]$SerialText) {
    $match = Get-C188R11NativeBoundary $SerialText 'input'
    if (-not $match.Success) { return '' }
    return $match.Groups['result'].Value
}

function Test-C188R11ImageContextMatch([string]$SerialText) {
    $launch = Get-C188R11NativeBoundary $SerialText 'resident-launch'
    $input = Get-C188R11NativeBoundary $SerialText 'input' 'before-managed-entry'
    if (-not $launch.Success -or -not $input.Success) { return $false }
    foreach ($group in @('module','entryOffset','segments','span')) {
        if ($launch.Groups[$group].Value -cne $input.Groups[$group].Value) {
            return $false
        }
    }
    return $true
}

function Test-C188R11ManagedLaunchState([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=[1-9]\d* st=managed-return selector=4 ownerGen=1 rg=[1-9]\d* re=1 rr=0 rcl=0 p=[1-9]\d* pr=0 ps=[1-9]\d* psr=0 pe=1 sr=0 pc=0 reset=0 m=1 g=1 td=0 n=5 result=0 gc=\d+ w=1:1\r?$')
}

function Test-C188R11ManagedInputState([string]$SerialText, [uint32]$Count,
    [uint32]$Mask, [string]$Result) {
    $pattern = '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=[1-9]\d* st=input-dispatch-entry selector=4 ownerGen=\d+ rg=\d+ re=\d+ rr=\d+ rcl=\d+ p=\d+ pr=\d+ ps=\d+ psr=\d+ pe=\d+ sr=\d+ pc=\d+ reset=\d+ m=' +
        $Mask + ' g=\d+ td=\d+ n=' + $Count + ' result=' + [regex]::Escape($Result) + ' gc=\d+ w=\d+:\d+\r?$'
    return [regex]::IsMatch($SerialText, $pattern)
}

function Test-C188R11TryFindResult([string]$SerialText, [int]$Result) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=[1-9]\d* st=lookup selector=4 ownerGen=\d+ rg=\d+ re=\d+ rr=\d+ rcl=\d+ p=\d+ pr=\d+ ps=\d+ psr=\d+ pe=\d+ sr=\d+ pc=\d+ reset=\d+ m=\d+ g=\d+ td=\d+ n=\d+ result=' +
        [regex]::Escape([string]$Result) + ' gc=\d+ w=\d+:\d+\r?$')
}

function Get-C188R11MemorySnapshot([string]$SerialText, [string]$Stage) {
    $pattern = '(?m)^\[C102-MANAGED-OUTPUT\] C188R11-MEM seq=(?<sequence>\d+) st=' +
        [regex]::Escape($Stage) +
        ' ra=(?<registry>[A-Fa-f0-9]{16}) owner=(?<owner>[A-Fa-f0-9]{16}) slot=(?<slot>[A-Fa-f0-9]{16}) live=(?<live>[A-Fa-f0-9]{16}) held=(?<held>[A-Fa-f0-9]{16}) gen=(?<generation>\d+:\d+:\d+:\d+) gc=(?<gc>\d+) heldLen=(?<heldLength>\d+)\r?$'
    return [regex]::Match($SerialText, $pattern)
}

function Test-C188R11MemoryStorageStable([string]$SerialText) {
    $initialized = Get-C188R11MemorySnapshot $SerialText 'registry-init'
    $launch = Get-C188R11MemorySnapshot $SerialText 'managed-return'
    $dispatch = Get-C188R11MemorySnapshot $SerialText 'input-dispatch-entry'
    return $initialized.Success -and $launch.Success -and $dispatch.Success -and
        $launch.Groups['registry'].Value -ne '0000000000000000' -and
        $initialized.Groups['owner'].Value -ne '0000000000000000' -and
        $launch.Groups['owner'].Value -ne '0000000000000000' -and
        $initialized.Groups['slot'].Value -ne '0000000000000000' -and
        $launch.Groups['slot'].Value -ne '0000000000000000' -and
        $initialized.Groups['live'].Value -ne '0000000000000000' -and
        $launch.Groups['live'].Value -ne '0000000000000000' -and
        $initialized.Groups['registry'].Value -eq $launch.Groups['registry'].Value -and
        $launch.Groups['registry'].Value -eq $dispatch.Groups['registry'].Value -and
        $initialized.Groups['owner'].Value -eq $launch.Groups['owner'].Value -and
        $launch.Groups['owner'].Value -eq $dispatch.Groups['owner'].Value -and
        $initialized.Groups['slot'].Value -eq $launch.Groups['slot'].Value -and
        $launch.Groups['slot'].Value -eq $dispatch.Groups['slot'].Value -and
        $initialized.Groups['live'].Value -eq $launch.Groups['live'].Value -and
        $launch.Groups['live'].Value -eq $dispatch.Groups['live'].Value -and
        $initialized.Groups['held'].Value -eq $initialized.Groups['live'].Value -and
        $launch.Groups['held'].Value -eq $launch.Groups['live'].Value -and
        $dispatch.Groups['held'].Value -eq $dispatch.Groups['live'].Value -and
        $initialized.Groups['generation'].Value -eq $launch.Groups['generation'].Value -and
        $launch.Groups['generation'].Value -eq $dispatch.Groups['generation'].Value -and
        $initialized.Groups['gc'].Value -eq $launch.Groups['gc'].Value -and
        $launch.Groups['gc'].Value -eq $dispatch.Groups['gc'].Value -and
        $initialized.Groups['heldLength'].Value -eq '5' -and
        $launch.Groups['heldLength'].Value -eq '5' -and
        $dispatch.Groups['heldLength'].Value -eq '5'
}

function Test-C188R11MemoryStorageInPersistentHeap([string]$SerialText) {
    $heap = [regex]::Match($SerialText,
        '(?m)^\[NATIVEAOT-HEAP\] action=initialize count=0000000000000001 base=(?<base>[A-Fa-f0-9]{16}) bytes=(?<bytes>[A-Fa-f0-9]{16}) reason=initial-managed-thread-attachment\r?$')
    $snapshot = Get-C188R11MemorySnapshot $SerialText 'managed-return'
    if (-not $heap.Success -or -not $snapshot.Success) { return $false }
    $baseAddress = [Convert]::ToUInt64($heap.Groups['base'].Value,16)
    $heapBytes = [Convert]::ToUInt64($heap.Groups['bytes'].Value,16)
    if ($heapBytes -eq 0 -or $baseAddress -gt ([uint64]::MaxValue - $heapBytes)) {
        return $false
    }
    $limitAddress = $baseAddress + $heapBytes
    foreach ($name in @('registry','owner','slot','live','held')) {
        $address = [Convert]::ToUInt64($snapshot.Groups[$name].Value,16)
        if ($address -lt $baseAddress -or $address -ge $limitAddress) {
            return $false
        }
    }
    return $true
}

function Test-C188R11InitializationEvidence([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11-INIT seq=[1-9]\d* re=1 pe=1 rg=[1-9]\d* p=[1-9]\d* ps=[1-9]\d*\r?$') -and
        [regex]::IsMatch($SerialText,
        '(?m)^\[C188R11-NATIVE\] nseq=[0-9A-Fa-f]{8} stage=managed-init-event regEpoch=00000001 poolEpoch=00000001 module=([0-9A-Fa-f]{8})\r?$')
}

function Get-C188R11ManagedSequences([string]$SerialText) {
    $pattern = '(?m)^\[C102-MANAGED-OUTPUT\] C188R11-(?:REG seq=|INIT seq=|MANAGED stage=\S+ seq=)(\d+)|^\[C102-MANAGED-OUTPUT\] C188R11 state seq=(\d+)'
    $values = [System.Collections.Generic.List[uint]]::new()
    foreach ($match in [regex]::Matches($SerialText, $pattern)) {
        $value = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
        $values.Add([uint]$value)
    }
    return ,$values.ToArray()
}

function Test-C188R11ManagedSequenceOrder([string]$SerialText) {
    [uint[]]$sequences = Get-C188R11ManagedSequences $SerialText
    if ($sequences.Count -lt 2) { return $false }
    for ($index = 1; $index -lt $sequences.Count; $index++) {
        if ($sequences[$index] -le $sequences[$index - 1]) { return $false }
    }
    return $true
}

function Test-C188R11ManagedBoundaryCorrelation([string]$SerialText) {
    $native = Get-C188R11NativeBoundary $SerialText 'resident-launch' 'before-managed-entry'
    $managed = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11-MANAGED stage=entry seq=\d+ call=(\d+) corr=(\d+) id=4 gen=(\d+) in=0 rc=-1\r?$')
    return $native.Success -and $managed.Success -and
        [Convert]::ToUInt32($native.Groups['correlation'].Value,16) -eq
            [uint32]$managed.Groups[2].Value -and
        [Convert]::ToUInt32($native.Groups['generation'].Value,16) -eq
            [uint32]$managed.Groups[3].Value
}

function Get-C188R11ReadinessFailureReason([string]$SerialText) {
    if (-not [regex]::IsMatch($SerialText,
            '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$')) {
        return 'main-loop-not-reached'
    }
    if (-not [regex]::IsMatch($SerialText,
            '(?m)^\[C150-APP-LAUNCH\] id=com\.guidexos\.apps\.managed\.notes generation=00000001 selector=00000004 kind=normal\r?$')) {
        return 'notes-launch-marker-missing'
    }
    if (-not [regex]::IsMatch($SerialText,
            '(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=00000001 window=[0-9A-Fa-f]{8} result=PASS\r?$')) {
        return 'notes-surface-not-created'
    }
    if (-not (Test-C188R11ManagedLaunchState $SerialText)) {
        return 'managed-return-state-missing-or-invalid'
    }
    if (-not (Test-C188R11InitializationEvidence $SerialText)) {
        return 'initialization-epoch-evidence-missing'
    }
    if (-not (Test-C188R11NativeLaunchBoundary $SerialText)) {
        return 'native-launch-boundary-missing-or-invalid'
    }
    return ''
}

function Assert-C188R11NativeLaunchBoundary([string]$SerialText) {
    if (-not (Test-C188R11NativeLaunchBoundary $SerialText)) {
        throw '[C188R11-FAIL] reason=native-launch-boundary-missing-or-invalid; no input may be sent.'
    }
}
