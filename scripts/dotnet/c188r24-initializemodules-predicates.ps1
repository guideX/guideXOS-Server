Set-StrictMode -Version Latest

function ConvertFrom-C188R24QmpResponse([string]$Response) {
    try { return $Response | ConvertFrom-Json -ErrorAction Stop }
    catch { throw 'R24 QMP response was not valid JSON.' }
}

function Get-C188R24CpuBoundaryEvidence([string]$RegisterText, [object]$Addresses) {
    $match = [regex]::Match($RegisterText,'(?i)\bRIP\s*=\s*(?:0x)?([0-9a-f]{8,16})\b')
    $raxMatch = [regex]::Match($RegisterText,'(?i)\bRAX\s*=\s*(?:0x)?([0-9a-f]{1,16})\b')
    if (-not $match.Success) {
        return [ordered]@{rip=$null;classification='cpu-rip-unavailable';initializerEntered=$false
            rax=$null;rhpRegisterEntered=$false;registrationPublished=$false;registrationReturned=$false
            registrationSuccessObserved=$false
            initializerReturned=$false;registersTextAvailable=(-not [string]::IsNullOrWhiteSpace($RegisterText))}
    }
    $parseAddress = { param([string]$Value) [Convert]::ToUInt64(($Value -replace '^0[xX]',''),16) }
    $rip = [Convert]::ToUInt64($match.Groups[1].Value,16)
    $initializer = & $parseAddress ([string]$Addresses.initializeModules)
    $register = & $parseAddress ([string]$Addresses.rhpRegisterOsModule)
    $typeManagers = & $parseAddress ([string]$Addresses.createTypeManagers)
    $initializeCallReturn = & $parseAddress ([string]$Addresses.initializeModulesCallReturn)
    $registerCallReturn = & $parseAddress ([string]$Addresses.rhpRegisterCallReturn)
    $typeManagersCall = & $parseAddress ([string]$Addresses.createTypeManagersCall)
    $moduleHandle = & $parseAddress ([string]$Addresses.osModuleHandle)
    $rax = if ($raxMatch.Success) { [Convert]::ToUInt64($raxMatch.Groups[1].Value,16) } else { $null }
    $insideInitializer = $rip -ge $initializer -and $rip -lt ($initializer + 0xE1)
    $insideRegister = $rip -ge $register -and $rip -lt ($register + 0x61)
    $insideTypeManagers = $rip -ge $typeManagers -and $rip -lt ($typeManagers + 0x100)
    $afterRegisterCall = $rip -ge $registerCallReturn -and $rip -lt ($initializer + 0xE1)
    $initializerReturned = $rip -ge $initializeCallReturn -and $rip -lt ($initializeCallReturn + 0x200)
    $rhpEntered = $insideRegister -or $afterRegisterCall -or $insideTypeManagers -or $initializerReturned
    $registrationPublished = ($insideRegister -and $rip -ge ($register + 0x52) -and $rip -lt ($register + 0x5B)) -or
        ($insideInitializer -and $rip -ge $registerCallReturn -and $rip -lt $typeManagersCall -and
            $null -ne $rax -and $rax -eq $moduleHandle)
    $registrationReturned = $afterRegisterCall -or $insideTypeManagers -or $initializerReturned
    $registrationSuccessObserved = $registrationPublished -or
        ($insideInitializer -and $rip -ge $registerCallReturn -and $rip -lt $typeManagersCall -and
            $null -ne $rax -and $rax -eq $moduleHandle)
    $initializerEntered = $insideInitializer -or $insideRegister -or $insideTypeManagers -or $initializerReturned
    $classification = if ($insideRegister -and $rip -ge ($register + 0x5B)) { 'rhp-register-allocation-failure-return-path' }
        elseif ($insideRegister -and $rip -ge ($register + 0x52)) { 'rhp-register-list-published-return-pending' }
        elseif ($insideRegister -and $rip -ge ($register + 0x40)) { 'rhp-register-list-publication-in-progress' }
        elseif ($insideRegister -and $rip -ge ($register + 0x22)) { 'rhp-register-allocation-succeeded-before-publication' }
        elseif ($insideRegister -and $rip -ge ($register + 0x15)) { 'rhp-register-native-allocation-in-progress' }
        elseif ($insideRegister) { 'rhp-register-entered-before-allocation' }
        elseif ($insideTypeManagers) { 'type-manager-helper-current' }
        elseif ($insideInitializer -and $afterRegisterCall) { 'initializer-after-registration-return' }
        elseif ($insideInitializer) { 'initializer-entered-before-registration-return' }
        elseif ($initializerReturned) { 'initializer-returned-to-shared-helper' }
        else { 'cpu-rip-outside-known-initialization-ranges' }
    return [ordered]@{rip=('0x{0:X16}' -f $rip);classification=$classification
        initializerEntered=$initializerEntered;rhpRegisterEntered=$rhpEntered
        rax=if ($null -ne $rax) { '0x{0:X16}' -f $rax } else { $null }
        registrationPublished=$registrationPublished;registrationReturned=$registrationReturned
        registrationSuccessObserved=$registrationSuccessObserved
        initializerReturned=$initializerReturned;registersTextAvailable=$true}
}
