function Get-C188R11R2LineMatch([string]$SerialText, [string]$Pattern) {
    return [regex]::Match($SerialText, '(?m)^' + $Pattern + '\r?$')
}

function Get-C188R11R2KeyboardEvidence([string]$SerialText) {
    $focus = Get-C188R11R2LineMatch $SerialText `
        '\[C188R11R2_KEY\] stage=main-loop-focus window=(?<window>[A-Fa-f0-9]{8}) title=Managed Notes owner=\S+ result=focused'
    $irq = Get-C188R11R2LineMatch $SerialText `
        '\[C188R11R2_KEY\] stage=irq-scan seq=[A-Fa-f0-9]{8} eid=(?<eid>[A-Fa-f0-9]{8}) sc=0000001C ext=0 phase=key-queued key=00000061 transition=down shift=0 ctrl=0 route=queue'
    $dequeue = Get-C188R11R2LineMatch $SerialText `
        '\[C188R11R2_KEY\] stage=kernel-dequeue source=ps2-irq eid=(?<eid>[A-Fa-f0-9]{8}) sc=0000001C key=00000061 class=printable shift=0 ctrl=0 overwritten=00000000 result=PASS'
    $target = Get-C188R11R2LineMatch $SerialText `
        '\[C188R11R2_KEY\] stage=focus-target eid=(?<eid>[A-Fa-f0-9]{8}) window=(?<window>[A-Fa-f0-9]{8}) title=Managed Notes owner=\S+ route=focused-window'
    $nativeEntry = Get-C188R11R2LineMatch $SerialText `
        '\[C188R11R2_KEY\] stage=native-managed-entry eid=(?<eid>[A-Fa-f0-9]{8}) call=(?<call>[A-Fa-f0-9]{8}) corr=(?<corr>[A-Fa-f0-9]{8}) selector=00000004 kind=03000000 value=00000061 shift=0 ctrl=0 rc=FFFFFFFF'
    $nativeReturn = Get-C188R11R2LineMatch $SerialText `
        '\[C188R11R2_KEY\] stage=native-managed-return eid=(?<eid>[A-Fa-f0-9]{8}) call=(?<call>[A-Fa-f0-9]{8}) corr=(?<corr>[A-Fa-f0-9]{8}) selector=00000004 kind=03000000 value=00000061 shift=0 ctrl=0 rc=00000000'
    $managedCall = Get-C188R11R2LineMatch $SerialText `
        '\[C102-MANAGED-OUTPUT\] C188R11-MANAGED stage=entry seq=\d+ call=(?<call>\d+) corr=(?<corr>\d+) id=4 gen=\d+ in=1 rc=-1'
    $dispatch = Get-C188R11R2LineMatch $SerialText `
        '\[C102-MANAGED-OUTPUT\] C188R11R2_KEY seq=(?<seq>\d+) stage=managed-dispatch-entry selector=4 kind=key-char value=97 shift=0 ctrl=0 result=-1'
    $callback = Get-C188R11R2LineMatch $SerialText `
        '\[C102-MANAGED-OUTPUT\] C188R11R2_KEY seq=(?<seq>\d+) stage=notes-callback-entry selector=4 kind=key-char value=97 shift=0 ctrl=0 result=-1'
    $callbackReturn = Get-C188R11R2LineMatch $SerialText `
        '\[C102-MANAGED-OUTPUT\] C188R11R2_KEY seq=(?<seq>\d+) stage=managed-callback-return selector=4 kind=key-char value=97 shift=0 ctrl=0 result=(?<result>-?\d+)'
    $inputState = Get-C188R11R2LineMatch $SerialText `
        '\[C102-MANAGED-OUTPUT\] C188R11 state seq=(?<seq>\d+) st=input-dispatch-entry selector=4 ownerGen=\d+ rg=\d+ re=\d+ rr=\d+ rcl=\d+ p=\d+ pr=\d+ ps=\d+ psr=\d+ pe=\d+ sr=\d+ pc=\d+ reset=\d+ m=(?<mask>\d+) g=\d+ td=\d+ n=(?<count>\d+) result=-1'
    $lookup = Get-C188R11R2LineMatch $SerialText `
        '\[C102-MANAGED-OUTPUT\] C188R11 state seq=(?<seq>\d+) st=lookup selector=4 ownerGen=\d+ rg=\d+ re=\d+ rr=\d+ rcl=\d+ p=\d+ pr=\d+ ps=\d+ psr=\d+ pe=\d+ sr=\d+ pc=\d+ reset=\d+ m=(?<mask>\d+) g=\d+ td=\d+ n=(?<count>\d+) result=(?<result>-?\d+)'

    $eventId = if ($dequeue.Success) { $dequeue.Groups['eid'].Value } else { '' }
    $nativeCall = if ($nativeEntry.Success) { $nativeEntry.Groups['call'].Value } else { '' }
    $managedCallValue = if ($managedCall.Success) { $managedCall.Groups['call'].Value } else { '' }
    $nativeCallDecimal = if ($nativeCall) { [Convert]::ToUInt32($nativeCall, 16) } else { 0u }
    $populated = $inputState.Success -and
        [uint32]$inputState.Groups['count'].Value -gt 0u -and
        (([uint32]$inputState.Groups['mask'].Value -band 1u) -ne 0u)
    $managedStateQualified = $focus.Success -and $irq.Success -and
        $dequeue.Success -and $target.Success -and $nativeEntry.Success -and
        $managedCall.Success -and $dispatch.Success -and $inputState.Success -and
        $lookup.Success -and
        $eventId -eq $irq.Groups['eid'].Value -and
        $eventId -eq $target.Groups['eid'].Value -and
        $eventId -eq $nativeEntry.Groups['eid'].Value -and
        $target.Groups['window'].Value -eq $focus.Groups['window'].Value -and
        $nativeCall -eq $nativeEntry.Groups['corr'].Value -and
        $nativeCallDecimal -eq [uint32]$managedCallValue -and
        $managedCall.Groups['call'].Value -eq $managedCall.Groups['corr'].Value
    $traceComplete = $managedStateQualified -and $nativeReturn.Success -and
        $callback.Success -and $callbackReturn.Success -and
        $eventId -eq $nativeReturn.Groups['eid'].Value -and
        $nativeCall -eq $nativeReturn.Groups['call'].Value -and
        $nativeCall -eq $nativeReturn.Groups['corr'].Value

    return [ordered]@{
        traceComplete=[bool]$traceComplete
        managedStateQualified=[bool]$managedStateQualified
        focusVerified=[bool]$focus.Success
        irqScancodeReceived=[bool]$irq.Success
        ordinaryKeyQueued=[bool]$irq.Success
        kernelDequeued=[bool]$dequeue.Success
        focusedNotesTarget=[bool]$target.Success
        nativeManagedEntry=[bool]$nativeEntry.Success
        managedDispatchEntry=[bool]$dispatch.Success
        managedInputSnapshot=[bool]$inputState.Success
        managedRegistryLookup=[bool]$lookup.Success
        notesCallbackEntry=[bool]$callback.Success
        notesCallbackReturn=[bool]$callbackReturn.Success
        registryCount=$(if ($inputState.Success) { [uint32]$inputState.Groups['count'].Value } else { $null })
        residentMask=$(if ($inputState.Success) { [uint32]$inputState.Groups['mask'].Value } else { $null })
        registryPopulated=[bool]$populated
        inputStateSequence=$(if ($inputState.Success) { [uint32]$inputState.Groups['seq'].Value } else { $null })
        lookupResult=$(if ($lookup.Success) { [int]$lookup.Groups['result'].Value } else { $null })
        eventId=$eventId
        nativeCall=$nativeCall
        managedCall=$managedCallValue
    }
}

function Test-C188R11R2KeyboardTrace([string]$SerialText) {
    $evidence = Get-C188R11R2KeyboardEvidence $SerialText
    return [bool]$evidence.traceComplete
}
