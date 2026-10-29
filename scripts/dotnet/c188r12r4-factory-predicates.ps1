function Get-C188R12R4FactoryEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$SerialText,
        [Parameter()][ValidateSet('complete','incomplete','live')][string]$CaptureState = 'incomplete'
    )

    $factoryLines = @([regex]::Matches($SerialText,
        '(?m)^.*C188R12R4_FACTORY invocation=(?<invocation>\d+) selector=(?<selector>\d+) operation=(?<operation>\d+) stage=(?<stage>[a-z-]+) code=(?<code>-?\d+) result=(?<result>\S+).*$'))
    $lifetimeLines = @([regex]::Matches($SerialText,
        '(?m)^.*C188R12R3_LIFETIME invocation=(?<invocation>\d+) stage=(?<stage>[a-z-]+) selector=(?<selector>\d+).*?$'))
    $allocationLines = @([regex]::Matches($SerialText,
        '(?m)^.*C188R12R4_ALLOC invocation=(?<invocation>\d+) selector=(?<selector>\d+) operation=(?<operation>\d+) kind=(?<kind>\S+) stage=(?<stage>[a-z-]+).*?result=(?<result>\S+).*$'))

    $factoryInvoke = $factoryLines | Where-Object { $_.Groups['stage'].Value -eq 'factory-invoke' } | Select-Object -First 1
    $oldFactoryEnter = $lifetimeLines | Where-Object { $_.Groups['stage'].Value -eq 'factory-enter' } | Select-Object -First 1
    if ($null -eq $factoryInvoke) {
        return [pscustomobject]@{
            outcome = if ($null -ne $oldFactoryEnter) { 'INTERNAL_TRACE_MISSING' } else { 'NO_RELEVANT_FACTORY_TRACE' }
            invocationId = if ($null -ne $oldFactoryEnter) { $oldFactoryEnter.Groups['invocation'].Value } else { $null }
            lastFactoryStage = $null
            captureState = $CaptureState
            rootCauseProven = $false
        }
    }

    $invocation = $factoryInvoke.Groups['invocation'].Value
    $factoryLines = @($factoryLines | Where-Object { $_.Groups['invocation'].Value -eq $invocation })
    $lifetimeLines = @($lifetimeLines | Where-Object { $_.Groups['invocation'].Value -eq $invocation })
    $allocationLines = @($allocationLines | Where-Object { $_.Groups['invocation'].Value -eq $invocation })
    $stages = @($factoryLines | ForEach-Object { $_.Groups['stage'].Value })
    $lifetimeStages = @($lifetimeLines | ForEach-Object { $_.Groups['stage'].Value })
    $lastStage = if ($stages.Count -gt 0) { $stages[-1] } else { $null }

    if ($allocationLines | Where-Object {
        $_.Groups['stage'].Value -eq 'fast-capacity-miss' -and
        $_.Groups['result'].Value -eq 'bounded-failfast'
    } | Select-Object -First 1) {
        $outcome = 'EXPLICIT_ALLOCATION_FAILURE'
    } elseif ('factory-failure' -in $stages) {
        $outcome = 'EXPLICIT_FACTORY_FAILURE'
    } elseif ('exit-success' -in $lifetimeStages -and
        $SerialText.Contains('C188R12R1-MANAGED stage=application-launch-return selector=5 result=PASS') -and
        $SerialText.Contains('C150-SURFACE action=create') -and
        $SerialText.Contains('C102-LAUNCH-RETURN')) {
        $outcome = 'SETTINGS_STARTUP_SUCCESS'
    } elseif ($CaptureState -ne 'complete') {
        $outcome = 'CAPTURE_INCOMPLETE'
    } elseif (($allocationLines | Where-Object {
        $_.Groups['kind'].Value -eq 'object' -and
        $_.Groups['stage'].Value -eq 'fast-capacity-miss' -and
        $_.Groups['result'].Value -eq 'RhpGcAlloc-fallback-entered'
    } | Select-Object -First 1) -and
        -not ($allocationLines | Where-Object {
            $_.Groups['kind'].Value -eq 'object' -and
            $_.Groups['stage'].Value -eq 'allocation-result'
        } | Select-Object -First 1) -and
        'constructor-enter' -notin $stages) {
        $outcome = 'ALLOCATION_REQUEST_NO_RESULT'
    } elseif ('type-init-enter' -in $stages -and 'type-init-return' -notin $stages) {
        $outcome = 'TYPE_INITIALIZATION_NO_RETURN'
    } elseif ('constructor-enter' -in $stages -and 'constructor-return' -notin $stages) {
        $outcome = 'CONSTRUCTOR_ENTRY_NO_RETURN'
    } elseif ('constructor-return' -in $stages -and 'factory-return' -notin $stages) {
        $outcome = 'CONSTRUCTOR_SUCCESS_FACTORY_NONRETURN'
    } elseif ('factory-return' -in $stages -and
        ('resident-register-return' -notin $lifetimeStages -or
         'exit-success' -notin $lifetimeStages)) {
        $outcome = 'FACTORY_SUCCESS_REGISTRATION_FAILURE'
    } elseif ('object-allocation-request' -in $stages -and
        'base-constructor-enter' -notin $stages -and
        'constructor-enter' -notin $stages) {
        $outcome = 'PRE_CONSTRUCTOR_NONRETURN'
    } elseif ('factory-invoke' -in $stages -and
        'constructor-enter' -notin $stages) {
        $outcome = 'FACTORY_ENTRY_NO_CONSTRUCTOR_ENTRY'
    } elseif ('exit-success' -in $lifetimeStages) {
        $outcome = 'FACTORY_AND_REGISTRATION_SUCCESS'
    } else {
        $outcome = 'FACTORY_ENTRY_NO_CONSTRUCTOR_ENTRY'
    }

    [pscustomobject]@{
        outcome = $outcome
        invocationId = $invocation
        lastFactoryStage = $lastStage
        captureState = $CaptureState
        rootCauseProven = $outcome -eq 'EXPLICIT_ALLOCATION_FAILURE'
    }
}
