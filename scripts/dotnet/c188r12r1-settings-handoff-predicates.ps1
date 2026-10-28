function Get-C188R12R1SettingsHandoffEvidence([string]$SerialText) {
    $appModelLaunch = [regex]::Match($SerialText,
        '(?m)^\[APPMODEL-MANAGED-LAUNCH\] source=DesktopLaunch appId=com\.guidexos\.apps\.managed\.settingscenter selector=00000005 image=/system/apps/GXOSAPP\.ELF metadata=valid\r?$')
    $nativeEntry = [regex]::Match($SerialText,
        '(?m)^\[C188R12R1-NATIVE\] stage=before-managed-entry selector=00000005 generation=(?<generation>[0-9A-Fa-f]{8}) call=(?<call>[0-9A-Fa-f]{8}) result=BEGIN\r?$')
    $managedMain = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-MANAGED stage=main-entry selector=5 generation=(?<generation>\d+) result=BEGIN\r?$')
    $dispatch = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-MANAGED stage=dispatch-entry selector=5 result=BEGIN\r?$')
    $appLaunch = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-MANAGED stage=application-launch-entry selector=5 result=BEGIN\r?$')
    $settingsLaunch = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-SETTINGS stage=launch-entry selector=5 generation=(?<generation>\d+)\r?$')
    $beforeSurface = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-SETTINGS stage=before-surface-request selector=5 generation=(?<generation>\d+)\r?$')
    $managedSurfaceCreated = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-SETTINGS stage=surface-created selector=5 generation=(?<generation>\d+) result=0\r?$')
    $managedSurfaceRequestReturn = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-SETTINGS stage=surface-request-return selector=5 generation=(?<generation>\d+) result=(?<result>\d+)\r?$')
    $nativeSurfaceFailure = [regex]::Match($SerialText,
        '(?m)^\[C188R12R1-SURFACE\] stage=create selector=00000005 generation=(?<generation>[0-9A-Fa-f]{8}) result=FAIL reason=(?<reason>[a-z0-9-]+)\r?$')
    $surface = [regex]::Match($SerialText,
        '(?m)^\[C188R12R1-SURFACE\] stage=created appId=com\.guidexos\.apps\.managed\.settingscenter selector=00000005 generation=(?<generation>[0-9A-Fa-f]{8}) surfaceGeneration=(?<surfaceGeneration>[0-9A-Fa-f]{8}) window=(?<window>[0-9A-Fa-f]{8}) x=[0-9A-Fa-f]{8} y=[0-9A-Fa-f]{8} width=[0-9A-Fa-f]{8} height=[0-9A-Fa-f]{8} visible=(?<visible>[01]) minimized=(?<minimized>[01]) focused=(?<focused>[01]) front=(?<front>[01]) dirty=(?<dirty>[01]) result=PASS\r?$')
    $composed = [regex]::Match($SerialText,
        '(?m)^\[C188R12R1-COMPOSITOR\] stage=composed appId=com\.guidexos\.apps\.managed\.settingscenter generation=(?<generation>[0-9A-Fa-f]{8}) surfaceGeneration=(?<surfaceGeneration>[0-9A-Fa-f]{8}) window=(?<window>[0-9A-Fa-f]{8}) visible=(?<visible>[01]) minimized=(?<minimized>[01]) focused=(?<focused>[01]) front=(?<front>[01]) result=DRAWN\r?$')
    $presented = [regex]::Match($SerialText,
        '(?m)^\[C188R12R1-COMPOSITOR\] stage=presented window=(?<window>[0-9A-Fa-f]{8}) result=PASS\r?$')
    $launchReturn = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-SETTINGS stage=launch-return selector=5 generation=(?<generation>\d+) result=0\r?$')
    $appLaunchReturn = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R1-MANAGED stage=application-launch-return selector=5 result=PASS\r?$')
    $registryEntry = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=\d+ st=settings-dispatch-entry selector=5 .* n=5 result=-1 gc=0 .*\r?$')
    $registryReturn = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=\d+ st=settings-launch-return selector=5 .* reset=(?<reset>\d+) m=(?<mask>\d+) g=\d+ td=\d+ n=(?<descriptors>\d+) result=(?<result>-?\d+) gc=(?<gc>\d+) w=\d+:\d+\r?$')
    $notesRegistryMemory = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11-MEM seq=\d+ st=instance-start ra=(?<registry>[0-9A-Fa-f]{16}) owner=(?<owner>[0-9A-Fa-f]{16}) slot=(?<slot>[0-9A-Fa-f]{16}) live=(?<live>[0-9A-Fa-f]{16}) held=(?<held>[0-9A-Fa-f]{16}) .* heldLen=5\r?$')
    $settingsRegistryMemory = [regex]::Match($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11-MEM seq=\d+ st=settings-launch-return ra=(?<registry>[0-9A-Fa-f]{16}) owner=(?<owner>[0-9A-Fa-f]{16}) slot=(?<slot>[0-9A-Fa-f]{16}) live=(?<live>[0-9A-Fa-f]{16}) held=(?<held>[0-9A-Fa-f]{16}) .* heldLen=5\r?$')
    $registryMemoryStable = $notesRegistryMemory.Success -and
        $settingsRegistryMemory.Success
    $registryMemoryMismatch = $false
    if ($registryMemoryStable) {
        foreach ($memoryField in @('registry','owner','slot','live','held')) {
            if ($notesRegistryMemory.Groups[$memoryField].Value -cne
                $settingsRegistryMemory.Groups[$memoryField].Value) {
                $registryMemoryStable = $false
                $registryMemoryMismatch = $true
                break
            }
        }
    }
    $heapInitialize = [regex]::Match($SerialText,
        '(?m)^\[NATIVEAOT-HEAP\] action=initialize count=(?<count>[0-9A-Fa-f]{16}) base=(?<base>[0-9A-Fa-f]{16}) bytes=[0-9A-Fa-f]{16} reason=initial-managed-thread-attachment\r?$')
    $heapInitializations = [regex]::Matches($SerialText,
        '(?m)^\[NATIVEAOT-HEAP\] action=initialize count=[0-9A-Fa-f]{16} base=[0-9A-Fa-f]{16} bytes=[0-9A-Fa-f]{16} reason=initial-managed-thread-attachment\r?$')
    $heapInitializationRegression = $heapInitializations.Count -gt 1 -or
        ($heapInitialize.Success -and
         $heapInitialize.Groups['count'].Value -ne '0000000000000001')
    $settingsHeapPreserved = $false
    if ($heapInitialize.Success -and $appModelLaunch.Success -and
        $heapInitializations.Count -eq 1 -and
        $heapInitialize.Groups['count'].Value -eq '0000000000000001') {
        $heapPreserves = [regex]::Matches($SerialText,
            '(?m)^\[NATIVEAOT-HEAP\] action=preserve count=[0-9A-Fa-f]{16} resetCount=(?<reset>[0-9A-Fa-f]{16}) base=(?<base>[0-9A-Fa-f]{16}) allocation=[0-9A-Fa-f]{16} reason=resident-managed-thread\r?$')
        foreach ($heapPreserve in $heapPreserves) {
            if ($heapPreserve.Index -gt $appModelLaunch.Index -and
                $heapPreserve.Groups['reset'].Value -eq '0000000000000001' -and
                $heapPreserve.Groups['base'].Value -ceq
                    $heapInitialize.Groups['base'].Value) {
                $settingsHeapPreserved = $true
                break
            }
        }
    }
    $settingsResidentMask = if ($registryReturn.Success) {
        [int]$registryReturn.Groups['mask'].Value
    } else { -1 }
    $settingsRegistryReturnStateValid = $registryReturn.Success -and
        $registryReturn.Groups['reset'].Value -eq '0' -and
        $settingsResidentMask -eq 1 -and
        $registryReturn.Groups['descriptors'].Value -eq '5' -and
        $registryReturn.Groups['result'].Value -eq '0' -and
        $registryReturn.Groups['gc'].Value -eq '0'
    $settingsRegistryPreserved = $settingsRegistryReturnStateValid -and
        $registryMemoryStable -and $settingsHeapPreserved
    $nativeReturn = [regex]::Match($SerialText,
        '(?m)^\[C188R12R1-NATIVE\] stage=after-managed-return selector=00000005 generation=(?<generation>[0-9A-Fa-f]{8}) call=(?<call>[0-9A-Fa-f]{8}) rc=00000000 result=RETURNED\r?$')
    $appModelReturn = [regex]::Match($SerialText,
        '(?m)^\[APPMODEL-MANAGED-RESULT\] appId=com\.guidexos\.apps\.managed\.settingscenter status=success result=PASS\r?$')

    $generation = ''
    if ($nativeEntry.Success) { $generation = $nativeEntry.Groups['generation'].Value.ToUpperInvariant() }
    $managedGenerationHex = ''
    if ($managedMain.Success) {
        try { $managedGenerationHex = ('{0:X8}' -f [uint32]$managedMain.Groups['generation'].Value) } catch { }
    }
    $settingsGenerationHex = ''
    if ($settingsLaunch.Success) {
        try { $settingsGenerationHex = ('{0:X8}' -f [uint32]$settingsLaunch.Groups['generation'].Value) } catch { }
    }
    $beforeSurfaceGenerationHex = ''
    if ($beforeSurface.Success) {
        try { $beforeSurfaceGenerationHex = ('{0:X8}' -f [uint32]$beforeSurface.Groups['generation'].Value) } catch { }
    }
    $launchReturnGenerationHex = ''
    if ($launchReturn.Success) {
        try { $launchReturnGenerationHex = ('{0:X8}' -f [uint32]$launchReturn.Groups['generation'].Value) } catch { }
    }

    $entryCorrelated = $nativeEntry.Success -and $managedMain.Success -and
        $generation -eq $managedGenerationHex
    $settingsCorrelated = $entryCorrelated -and $dispatch.Success -and
        $appLaunch.Success -and $settingsLaunch.Success -and
        $settingsGenerationHex -eq $generation
    $surfaceCorrelated = $settingsCorrelated -and $beforeSurface.Success -and
        $beforeSurfaceGenerationHex -eq $generation -and $surface.Success -and
        $managedSurfaceCreated.Success -and
        $managedSurfaceCreated.Groups['generation'].Value -eq $settingsLaunch.Groups['generation'].Value -and
        $surface.Groups['generation'].Value.ToUpperInvariant() -eq $generation
    $surfaceActivated = $surfaceCorrelated -and
        $surface.Groups['visible'].Value -eq '1' -and
        $surface.Groups['minimized'].Value -eq '0' -and
        $surface.Groups['focused'].Value -eq '1' -and
        $surface.Groups['front'].Value -eq '1' -and
        $surface.Groups['dirty'].Value -eq '1'
    $compositionCorrelated = $surfaceActivated -and $composed.Success -and
        $composed.Groups['generation'].Value.ToUpperInvariant() -eq $generation -and
        $composed.Groups['surfaceGeneration'].Value.ToUpperInvariant() -eq
            $surface.Groups['surfaceGeneration'].Value.ToUpperInvariant() -and
        $composed.Groups['window'].Value.ToUpperInvariant() -eq
            $surface.Groups['window'].Value.ToUpperInvariant()
    $compositionVisible = $compositionCorrelated -and
        $composed.Groups['visible'].Value -eq '1' -and
        $composed.Groups['minimized'].Value -eq '0' -and
        $composed.Groups['focused'].Value -eq '1' -and
        $composed.Groups['front'].Value -eq '1'
    $presentationCorrelated = $compositionVisible -and $presented.Success -and
        $presented.Groups['window'].Value.ToUpperInvariant() -eq
            $surface.Groups['window'].Value.ToUpperInvariant()
    $launchReturned = $presentationCorrelated -and $launchReturn.Success -and
        $launchReturnGenerationHex -eq $generation -and $appLaunchReturn.Success -and
        $nativeReturn.Success -and
        $nativeReturn.Groups['generation'].Value.ToUpperInvariant() -eq $generation -and
        $nativeReturn.Groups['call'].Value.ToUpperInvariant() -eq
            $nativeEntry.Groups['call'].Value.ToUpperInvariant() -and
        $appModelReturn.Success -and $registryEntry.Success -and
        $settingsRegistryPreserved

    $boundary = if (-not $appModelLaunch.Success) { 'appmodel-launch-request' }
        elseif (-not $nativeEntry.Success) { 'native-managed-entry' }
        elseif (-not $managedMain.Success) { 'managed-main-entry' }
        elseif (-not $dispatch.Success) { 'managed-dispatch' }
        elseif (-not $appLaunch.Success -or -not $settingsLaunch.Success) { 'settings-initialization' }
        elseif (-not $beforeSurface.Success) { 'settings-pre-surface-initialization' }
        elseif (-not $surface.Success -or $managedSurfaceRequestReturn.Success -or $nativeSurfaceFailure.Success) { 'window-creation' }
        elseif (-not $surfaceActivated) { 'surface-visibility-or-activation' }
        elseif (-not $composed.Success) { 'compositor-composition' }
        elseif (-not $compositionVisible) { 'compositor-order-or-visibility' }
        elseif (-not $presented.Success) { 'frame-presentation' }
        elseif (-not $registryEntry.Success) { 'managed-registry-entry' }
        elseif (-not $settingsRegistryReturnStateValid) { 'managed-registry-return' }
        elseif (-not $registryMemoryStable) { 'managed-registry-storage' }
        elseif (-not $settingsHeapPreserved) { 'persistent-managed-heap' }
        elseif (-not $launchReturned) { 'managed-launch-return' }
        else { 'presented-settings-surface' }

    return [ordered]@{
        accepted=[bool]$launchReturned
        earliestMissingBoundary=$boundary
        generation=$generation
        window=if ($surface.Success) { $surface.Groups['window'].Value.ToUpperInvariant() } else { '' }
        appModelLaunch=[bool]$appModelLaunch.Success
        nativeManagedEntry=[bool]$nativeEntry.Success
        managedMainEntry=[bool]$managedMain.Success
        managedDispatch=[bool]$dispatch.Success
        settingsLaunchEntry=[bool]$settingsLaunch.Success
        beforeSurfaceRequest=[bool]$beforeSurface.Success
        managedSurfaceCreated=[bool]$managedSurfaceCreated.Success
        managedSurfaceRequestReturn=[bool]$managedSurfaceRequestReturn.Success
        managedSurfaceRequestResult=if ($managedSurfaceRequestReturn.Success) { [int]$managedSurfaceRequestReturn.Groups['result'].Value } else { $null }
        nativeSurfaceFailureReason=if ($nativeSurfaceFailure.Success) { $nativeSurfaceFailure.Groups['reason'].Value } else { '' }
        surfaceCreated=[bool]$surface.Success
        surfaceVisibleAndActive=[bool]$surfaceActivated
        surfaceComposed=[bool]$composed.Success
        compositionVisibleAndFront=[bool]$compositionVisible
        framePresented=[bool]$presented.Success
        launchReturned=[bool]$launchReturned
        appModelResult=[bool]$appModelReturn.Success
        settingsRegistryEntry=[bool]$registryEntry.Success
        settingsRegistryReturn=[bool]$registryReturn.Success
        settingsRegistryReturnValid=[bool]$settingsRegistryReturnStateValid
        settingsResidentMask=$settingsResidentMask
        settingsRegistryMemoryStable=[bool]$registryMemoryStable
        settingsRegistryMemoryMismatch=[bool]$registryMemoryMismatch
        persistentHeapPreserved=[bool]$settingsHeapPreserved
        persistentHeapInitializationCount=$heapInitializations.Count
        persistentHeapInitializationRegression=[bool]$heapInitializationRegression
    }
}

function Test-C188R12R1SettingsHandoffAccepted([string]$SerialText) {
    $evidence = Get-C188R12R1SettingsHandoffEvidence $SerialText
    return [bool]$evidence.accepted
}

function Test-C188R12R1SingleClickQmpEvidence([string]$QmpControlText) {
    $downCount = [regex]::Matches($QmpControlText,
        '"type":"btn","data":\{"button":"left","down":true\}').Count
    $upCount = [regex]::Matches($QmpControlText,
        '"type":"btn","data":\{"button":"left","down":false\}').Count
    $keyCount = [regex]::Matches($QmpControlText,
        '"type":"key"').Count
    return [ordered]@{
        accepted=($downCount -eq 1 -and $upCount -eq 1 -and $keyCount -eq 0)
        leftButtonDownCount=$downCount
        leftButtonUpCount=$upCount
        keyEventCount=$keyCount
    }
}

function Test-C188R12R1PointerOnSettingsButton($PointerPosition, $ClickTarget) {
    $found = $null -ne $PointerPosition -and [bool]$PointerPosition.found -and
        [int]$PointerPosition.matches -eq 1 -and
        $null -ne $PointerPosition.x -and $null -ne $PointerPosition.y
    if (-not $found -or $null -eq $ClickTarget -or -not [bool]$ClickTarget.accepted) {
        return [ordered]@{accepted=$false;reason='unique-observed-pointer-or-source-target-missing'}
    }
    $left = [int]$ClickTarget.buttonScreenX + 6
    $top = [int]$ClickTarget.buttonScreenY + 6
    $right = [int]$ClickTarget.buttonScreenX + [int]$ClickTarget.buttonWidth - 6
    $bottom = [int]$ClickTarget.buttonScreenY + [int]$ClickTarget.buttonHeight - 6
    $inside = [int]$PointerPosition.x -ge $left -and
        [int]$PointerPosition.x -lt $right -and
        [int]$PointerPosition.y -ge $top -and
        [int]$PointerPosition.y -lt $bottom
    return [ordered]@{
        accepted=$inside
        reason=if ($inside) { 'observed-pointer-inside-settings-button-safe-inset' } else { 'observed-pointer-outside-settings-button-safe-inset' }
        x=[int]$PointerPosition.x;y=[int]$PointerPosition.y
        left=$left;top=$top;right=$right;bottom=$bottom
    }
}

function Get-C188R12R1RunnerInputEvidence(
    [string]$SerialText, [string]$QmpControlText, [bool]$ClickAttempted,
    $PointerTargetEvidence) {
    $click = Test-C188R12R1SingleClickQmpEvidence $QmpControlText
    $settingsRequest = [regex]::IsMatch($SerialText,
        '(?m)^\[APPMODEL-MANAGED-LAUNCH\] source=DesktopLaunch appId=com\.guidexos\.apps\.managed\.settingscenter selector=00000005 image=/system/apps/GXOSAPP\.ELF metadata=valid\r?$')
    $unrelatedMenu = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C135-POPUP-OPEN invoke=(?:Options|File) capture=PASS popup=open result=PASS\r?$')
    $targetVerified = $null -ne $PointerTargetEvidence -and [bool]$PointerTargetEvidence.accepted
    $boundary = if (-not $ClickAttempted) { 'runner-input-not-attempted' }
        elseif (-not [bool]$click.accepted) { 'runner-input-sequence-invalid' }
        elseif ($unrelatedMenu) { 'runner-input-target-delivery' }
        elseif (-not $targetVerified) { 'runner-pointer-position-unverified' }
        elseif ($settingsRequest) { 'settings-appmodel-request-observed' }
        else { 'appmodel-launch-request' }
    return [ordered]@{
        earliestMissingBoundary=$boundary
        clickSequenceValid=[bool]$click.accepted
        pointerTargetVerified=$targetVerified
        settingsAppModelRequestObserved=$settingsRequest
        unrelatedNotesMenuObserved=$unrelatedMenu
        click=$click
    }
}

function Get-C188R12R1NotesSettingsClickTarget(
    [string]$SerialText, [string]$RepoRoot, [int]$ScreenWidth,
    [int]$ScreenHeight) {
    $notesSource = Get-Content -LiteralPath (Join-Path $RepoRoot 'samples/managed/HostLogProof/Applications/ManagedNotes.cs') -Raw
    $layoutSource = Get-Content -LiteralPath (Join-Path $RepoRoot 'samples/managed/HostLogProof/Applications/ManagedNotesC175.cs') -Raw
    $resizeSource = Get-Content -LiteralPath (Join-Path $RepoRoot 'samples/managed/HostLogProof/GuideXos/GuideXosManagedWindowResizeC174.cs') -Raw
    $buttonDefined = [regex]::IsMatch($notesSource,
        'surface\.TryAddButton\(430,\s*_c175Layout\.SettingsY,\s*90,\s*28,\s*"Settings"u8')
    $settingsYDefined = [regex]::IsMatch($layoutSource,
        'InitialEditorLines\s*=\s*4;') -and
        [regex]::IsMatch($layoutSource, 'EditorY\s*=\s*84;') -and
        [regex]::IsMatch($layoutSource, 'EditorLineHeight\s*=\s*18;') -and
        [regex]::IsMatch($layoutSource,
            'SettingsY\s*=>\s*StatusY\s*\+\s*130;') -and
        [regex]::IsMatch($layoutSource,
            'StatusY\s*=\s*EditorY\s*\+\s*editorLines\s*\*\s*EditorLineHeight\s*\+\s*6;')
    $titlebarDefined = [regex]::IsMatch($resizeSource,
        'NativeTitlebarHeight\s*=\s*24;')
    $notesSizeDefined = [regex]::IsMatch($notesSource,
        '"Managed Notes"u8,\s*600,\s*360,\s*out\s+GuideXosSurface')
    if (-not ($buttonDefined -and $settingsYDefined -and $titlebarDefined -and $notesSizeDefined)) {
        return [ordered]@{accepted=$false;reason='source-geometry-contract-changed';x=0;y=0}
    }

    $notes = [regex]::Match($SerialText,
        '(?m)^\[C188R12R1-SURFACE\] stage=created appId=com\.guidexos\.apps\.managed\.notes selector=00000004 generation=[0-9A-Fa-f]{8} surfaceGeneration=[0-9A-Fa-f]{8} window=(?<window>[0-9A-Fa-f]{8}) x=(?<x>[0-9A-Fa-f]{8}) y=(?<y>[0-9A-Fa-f]{8}) width=(?<width>[0-9A-Fa-f]{8}) height=(?<height>[0-9A-Fa-f]{8}) visible=(?<visible>[01]) minimized=(?<minimized>[01]) focused=(?<focused>[01]) front=(?<front>[01]) dirty=[01] result=PASS\r?$')
    if (-not $notes.Success) {
        return [ordered]@{accepted=$false;reason='initial-notes-surface-geometry-missing';x=0;y=0}
    }
    if ($notes.Groups['visible'].Value -ne '1' -or
        $notes.Groups['minimized'].Value -ne '0' -or
        $notes.Groups['focused'].Value -ne '1' -or
        $notes.Groups['front'].Value -ne '1') {
        return [ordered]@{accepted=$false;reason='notes-surface-not-active';x=0;y=0}
    }
    $windowX = [Convert]::ToInt32($notes.Groups['x'].Value,16)
    $windowY = [Convert]::ToInt32($notes.Groups['y'].Value,16)
    $windowWidth = [Convert]::ToInt32($notes.Groups['width'].Value,16)
    $windowHeight = [Convert]::ToInt32($notes.Groups['height'].Value,16)
    $settingsY = 84 + 4 * 18 + 6 + 130
    $targetX = $windowX + 430 + [int][Math]::Floor(90 / 2)
    $targetY = $windowY + 24 + $settingsY + [int][Math]::Floor(28 / 2)
    $withinButton = $targetX - $windowX - 430 -ge 0 -and
        $targetX - $windowX - 430 -lt 90 -and
        $targetY - $windowY - 24 - $settingsY -ge 0 -and
        $targetY - $windowY - 24 - $settingsY -lt 28
    $withinFrame = $targetX -ge 0 -and $targetX -lt $ScreenWidth -and
        $targetY -ge 0 -and $targetY -lt $ScreenHeight -and
        $windowWidth -eq 600 -and $windowHeight -eq 360
    return [ordered]@{
        accepted=($withinButton -and $withinFrame)
        reason=if ($withinButton -and $withinFrame) { 'source-defined-settings-button-center' } else { 'target-outside-observed-notes-button-or-frame' }
        x=$targetX;y=$targetY;window=$notes.Groups['window'].Value.ToUpperInvariant()
        windowX=$windowX;windowY=$windowY;buttonX=430;buttonY=$settingsY
        buttonScreenX=$windowX + 430;buttonScreenY=$windowY + 24 + $settingsY
        buttonWidth=90;buttonHeight=28;titlebarHeight=24
    }
}
