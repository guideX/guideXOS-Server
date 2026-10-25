$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r8-notes-input-probe.ps1')

$normalNotes = Get-C188R8NotesInputProbeResult -SerialText @'
C117-TEXT-AREA focus=PASS source=pointer
C117-TEXT-AREA edit=changed result=PASS
'@
if (-not $normalNotes.PointerAccepted -or -not $normalNotes.KeyboardAccepted) {
    throw 'Normal Notes pointer-focus and document-edit markers must pass.'
}

$proofOnly = Get-C188R8NotesInputProbeResult -SerialText @'
C120-POINTER target=document result=PASS
'@
if ($proofOnly.PointerAccepted -or $proofOnly.KeyboardAccepted) {
    throw 'Proof-only C120 markers must not qualify ordinary Notes input.'
}

$unfocused = Get-C188R8NotesInputProbeResult -SerialText @'
C117-TEXT-AREA edit=changed result=PASS
'@
if ($unfocused.PointerAccepted -or -not $unfocused.KeyboardAccepted) {
    throw 'Keyboard edit evidence must not imply pointer-focus evidence.'
}

Write-Output 'C188R8-NOTES-INPUT-PROBE-TEST cases=3 result=PASS'
