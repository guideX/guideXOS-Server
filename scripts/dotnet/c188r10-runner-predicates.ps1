function Test-C188R10NotesReady([string]$SerialText) {
    $loop = [regex]::IsMatch($SerialText,
        '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\r?$')
    $launch = [regex]::IsMatch($SerialText,
        '(?m)^\[C150-APP-LAUNCH\].*selector=(?:4|00000004) kind=normal\r?$')
    $surface = [regex]::IsMatch($SerialText,
        '(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.notes generation=(?:1|00000001)(?:\s|$)')
    return $loop -and $launch -and $surface
}

function Test-C188R10InputManagedEntry([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R10-MANAGED stage=entry call=\d+ id=4 gen=\d+ in=1 rc=-?\d+\r?$')
}

function Test-C188R10InputRegistrySnapshot([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R10 state stage=dispatch .*n=5 .*mask=\d+ .*id=4 gen=\d+ valid=1 .*rc=-?\d+\r?$')
}

function Test-C188R10InputHandler([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R10 state stage=handler-enter .*n=5 .*id=4 gen=\d+ valid=1 .*rc=-?\d+\r?$')
}

function Test-C188R10InputReturn([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R10 state stage=input-return .*id=4 gen=\d+ valid=1 .*rc=0\r?$')
}

function Test-C188R10ManagedLaunchExit([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R10-MANAGED stage=exit call=\d+ id=4 gen=1 in=0 rc=0\r?$')
}

function Test-C188R10NativeLaunchReturn([string]$SerialText) {
    return [regex]::IsMatch($SerialText,
        '(?m)^\[C188R10-NATIVE\] stage=after-managed-return call=[0-9A-Fa-f]{8} id=4 gen=00000001 input=00000000 image=[0-9A-Fa-f]{8} rc=00000000\r?$')
}
