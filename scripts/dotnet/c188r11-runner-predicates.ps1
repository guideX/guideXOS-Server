function Test-C188R11NotesReady([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$') -and
        [regex]::IsMatch($SerialText,
        '(?m)^\[C150-APP-LAUNCH\].*selector=(?:4|00000004) kind=normal\r?$') -and
        [regex]::IsMatch($SerialText,
        '(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=(?:1|00000001)(?:\s|$)')
}

function Get-C188R11NativeBoundary([string]$SerialText, [string]$Route,
    [string]$Stage = 'after-managed-return') {
    $routePattern = [regex]::Escape($Route)
    $stagePattern = [regex]::Escape($Stage)
    return [regex]::Match($SerialText,
        '(?m)^\[C188R11-NATIVE\] stage=' + $stagePattern + ' route=' +
        $routePattern +
        ' call=([0-9A-Fa-f]{8}) id=4 gen=([0-9A-Fa-f]{8}) input=([0-9A-Fa-f]{8}) module=([0-9A-Fa-f]{8}) entryOff=([0-9A-Fa-f]{16}) segments=([0-9A-Fa-f]{8}) span=([0-9A-Fa-f]{16}) rc=([0-9A-Fa-f]{8})\r?$')
}

function Test-C188R11NativeLaunchBoundary([string]$SerialText) {
    $match = Get-C188R11NativeBoundary $SerialText 'resident-launch'
    $entry = Get-C188R11NativeBoundary $SerialText 'resident-launch' 'before-managed-entry'
    return $entry.Success -and $entry.Groups[2].Value -eq '00000001' -and
        $entry.Groups[3].Value -eq '00000000' -and
        $entry.Groups[8].Value -eq 'FFFFFFFF' -and
        $match.Success -and $match.Groups[2].Value -eq '00000001' -and
        $match.Groups[3].Value -eq '00000000' -and
        $entry.Groups[1].Value -eq $match.Groups[1].Value -and
        $entry.Groups[4].Value -eq $match.Groups[4].Value -and
        $entry.Groups[5].Value -eq $match.Groups[5].Value -and
        $entry.Groups[6].Value -eq $match.Groups[6].Value -and
        $entry.Groups[7].Value -eq $match.Groups[7].Value -and
        $match.Groups[4].Value -ne '00000000' -and
        $match.Groups[5].Value -ne '0000000000000000' -and
        $match.Groups[6].Value -ne '00000000' -and
        $match.Groups[7].Value -ne '0000000000000000' -and
        $match.Groups[8].Value -eq '00000000'
}

function Test-C188R11NativeInputBoundary([string]$SerialText, [string]$ExpectedResult = '') {
    $match = Get-C188R11NativeBoundary $SerialText 'input'
    $entry = Get-C188R11NativeBoundary $SerialText 'input' 'before-managed-entry'
    return $entry.Success -and $entry.Groups[3].Value -eq '00000001' -and
        $entry.Groups[4].Value -ne '00000000' -and
        $entry.Groups[8].Value -eq 'FFFFFFFF' -and
        $match.Success -and $match.Groups[3].Value -eq '00000001' -and
        $entry.Groups[1].Value -eq $match.Groups[1].Value -and
        $entry.Groups[4].Value -eq $match.Groups[4].Value -and
        $entry.Groups[5].Value -eq $match.Groups[5].Value -and
        $entry.Groups[6].Value -eq $match.Groups[6].Value -and
        $entry.Groups[7].Value -eq $match.Groups[7].Value -and
        $match.Groups[4].Value -ne '00000000' -and
        $match.Groups[8].Value -match '^[0-9A-Fa-f]{8}$' -and
        ([string]::IsNullOrEmpty($ExpectedResult) -or
            $match.Groups[8].Value -ceq $ExpectedResult)
}

function Get-C188R11NativeInputResult([string]$SerialText) {
    $match = Get-C188R11NativeBoundary $SerialText 'input'
    if (-not $match.Success) { return '' }
    return $match.Groups[8].Value
}

function Test-C188R11ImageContextMatch([string]$SerialText) {
    $launch = Get-C188R11NativeBoundary $SerialText 'resident-launch'
    $input = Get-C188R11NativeBoundary $SerialText 'input' 'before-managed-entry'
    if (-not $launch.Success -or -not $input.Success) { return $false }
    foreach ($group in @(4,5,6,7)) {
        if ($launch.Groups[$group].Value -cne $input.Groups[$group].Value) {
            return $false
        }
    }
    return $true
}

function Assert-C188R11NativeLaunchBoundary([string]$SerialText) {
    if (-not (Test-C188R11NativeLaunchBoundary $SerialText)) {
        throw '[C188R11-FAIL] reason=native-launch-boundary-missing-or-invalid; no input may be sent.'
    }
}

function Test-C188R11ManagedLaunchState([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state st=managed-return rg=\d+ re=1 rr=0 rcl=0 p=\d+ pr=0 ps=\d+ psr=0 pe=1 sr=0 pc=0 m=1 g=1 td=0 n=5 result=0\r?$')
}

function Test-C188R11ManagedInputState([string]$SerialText, [uint32]$Count,
    [uint32]$Mask, [string]$Result) {
    $pattern = '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state st=dispatch-entry rg=\d+ re=\d+ rr=\d+ rcl=\d+ p=\d+ pr=\d+ ps=\d+ psr=\d+ pe=\d+ sr=\d+ pc=\d+ m=' +
        $Mask + ' g=\d+ td=\d+ n=' + $Count + ' result=' + $Result + '\r?$'
    return [regex]::IsMatch($SerialText, $pattern)
}

function Test-C188R11TryFindResult([string]$SerialText, [int]$Result) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state st=lookup rg=\d+ re=\d+ rr=\d+ rcl=\d+ p=\d+ pr=\d+ ps=\d+ psr=\d+ pe=\d+ sr=\d+ pc=\d+ m=\d+ g=\d+ td=\d+ n=\d+ result=' +
        $Result + '\r?$')
}

function Test-C188R11InitializationEvidence([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11-INIT re=1 pe=1 rg=\d+ p=\d+ ps=\d+\r?$') -and
        [regex]::IsMatch($SerialText,
        '(?m)^\[C188R11-NATIVE\] stage=managed-init-event regEpoch=00000001 poolEpoch=00000001 module=([0-9A-Fa-f]{8})\r?$')
}
