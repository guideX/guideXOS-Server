$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($PSVersionTable.PSEdition -ne 'Core') {
    throw 'C188R11R2 keyboard contract tests require PowerShell Core.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$ps2 = Get-Content -LiteralPath (Join-Path $repo 'kernel/core/ps2keyboard.cpp') -Raw
$desktop = Get-Content -LiteralPath (Join-Path $repo 'kernel/core/desktop.cpp') -Raw
$compositor = Get-Content -LiteralPath (Join-Path $repo 'kernel/core/kernel_compositor.cpp') -Raw
$native = Get-Content -LiteralPath (Join-Path $repo 'kernel/core/nativeaot_application.cpp') -Raw
$managed = Get-Content -LiteralPath (Join-Path $repo 'samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs') -Raw
$notes = Get-Content -LiteralPath (Join-Path $repo 'samples/managed/HostLogProof/Applications/ManagedNotes.cs') -Raw
$textArea = Get-Content -LiteralPath (Join-Path $repo 'samples/managed/HostLogProof/GuideXos/GuideXosTextArea.cs') -Raw
$main = Get-Content -LiteralPath (Join-Path $repo 'kernel/core/main.cpp') -Raw
$r2Runner = Get-Content -LiteralPath (Join-Path $repo 'scripts/dotnet/run-c188r11r2-keyboard-dispatch.ps1') -Raw
$r4r2Predicates = Get-Content -LiteralPath (Join-Path $repo 'scripts/dotnet/c188r11r4r2-runner-predicates.ps1') -Raw
$qacRunner = Get-Content -LiteralPath (Join-Path $repo 'scripts/dotnet/run-c188-qac-guest.ps1') -Raw
$script:caseCount = 0

function Assert-Contract([string]$Name, [bool]$Condition) {
    if (-not $Condition) { throw "C188R11R2 keyboard contract failed: $Name" }
    $script:caseCount++
}

function Get-CppByteTable([string]$Source, [string]$Name) {
    $pattern = 'static const char ' + [regex]::Escape($Name) +
        '\[256\]\s*=\s*\{(?<body>[\s\S]*?)\};'
    $match = [regex]::Match($Source, $pattern)
    if (-not $match.Success) { throw "Could not locate the $Name[256] table." }
    $body = [regex]::Replace($match.Groups['body'].Value, '//[^\r\n]*', '')
    $tokens = [regex]::Matches($body, "'(?:\\.|[^'\\])*'|(?<![A-Za-z0-9_])\d+")
    if ($tokens.Count -ne 256) {
        throw "The $Name table has $($tokens.Count) explicit entries; expected 256."
    }
    return ,@($tokens | ForEach-Object { $_.Value })
}

$plain = Get-CppByteTable $ps2 's_scancodeToAscii'
$shifted = Get-CppByteTable $ps2 's_scancodeToAsciiShift'
Assert-Contract 'set-2 scancode 0x1c maps to lowercase a' ($plain[0x1C] -ceq "'a'")
Assert-Contract 'set-2 scancode 0x1c maps to uppercase A with Shift' ($shifted[0x1C] -ceq "'A'")

$shiftStart = $ps2.IndexOf('if (scancode == SC2_LSHIFT || scancode == SC2_RSHIFT)')
$keyStart = $ps2.IndexOf('uint32_t key = 0;', $shiftStart)
$shiftBlock = if ($shiftStart -ge 0 -and $keyStart -gt $shiftStart) {
    $ps2.Substring($shiftStart, $keyStart - $shiftStart)
} else { '' }
Assert-Contract 'Shift updates modifier state and returns before key decoding' (
    $shiftBlock.Contains('s_leftShiftDown = !keyUp') -and
    $shiftBlock.Contains('s_rightShiftDown = !keyUp') -and
    $shiftBlock.Contains('return;'))

$releaseStart = $ps2.IndexOf('// Only process key presses, not releases')
$releaseEnd = $ps2.IndexOf('uint32_t key = 0;', $releaseStart)
$releaseBlock = if ($releaseStart -ge 0 -and $releaseEnd -gt $releaseStart) {
    $ps2.Substring($releaseStart, $releaseEnd - $releaseStart)
} else { '' }
Assert-Contract 'key-up is consumed without creating a queued application event' (
    $releaseBlock.Contains('if (keyUp)') -and $releaseBlock.Contains('return;'))

Assert-Contract 'recognized makes use the bounded event id and pending key slot' (
    $ps2.Contains('if (s_c188r11r2EventSequence >= 32u)') -and
    $ps2.Contains('if (key != 0)') -and $ps2.Contains('s_hasKey = true;') -and
    $ps2.Contains('s_c188r11r2PendingEventId = c188r11r2EventId;'))
Assert-Contract 'pending-slot overwrite is counted without changing slot behavior' (
    $ps2.Contains('if (s_hasKey && s_c188r11r2PendingOverwrites < 255u)') -and
    $ps2.Contains('++s_c188r11r2PendingOverwrites;'))

$controlRoute = $desktop.IndexOf('if (key >= 32 && key < 127 && ps2keyboard::last_key_ctrl_down())')
$charRoute = $desktop.IndexOf('if (key >= 32 && key < 127)', $controlRoute + 1)
$controlEnd = $desktop.IndexOf('// Route ordinary printable character keys', $controlRoute)
$charEnd = $desktop.IndexOf('// Route control characters and special keys', $charRoute)
Assert-Contract 'Ctrl printable chords use KeyDown while ordinary ASCII uses KeyChar' (
    $controlRoute -ge 0 -and $controlEnd -gt $controlRoute -and
    $charRoute -gt $controlEnd -and $charEnd -gt $charRoute -and
    $desktop.Substring($controlRoute, $controlEnd - $controlRoute).Contains('handleKeyDown(key)') -and
    $desktop.Substring($charRoute, $charEnd - $charRoute).Contains('handleKeyChar((char)key)'))

$compositorKeyChar = [regex]::Match($compositor,
    'void KernelCompositor::handleKeyChar\(char c\)\s*\{(?<body>[\s\S]*?)\n\}')
Assert-Contract 'compositor sends characters only to the focused window owner' (
    $compositorKeyChar.Success -and
    $compositorKeyChar.Groups['body'].Value.Contains('getFocusedWindow()') -and
    $compositorKeyChar.Groups['body'].Value.Contains('focused->owner->onKeyChar(c)'))

$invokeStart = $native.IndexOf('int32_t invokeManagedInput(uint32_t selector, uint32_t inputFlags)')
$invokeEnd = $native.IndexOf('int32_t invokeManagedKeyDownForProof(', $invokeStart)
$invokeBlock = if ($invokeStart -ge 0 -and $invokeEnd -gt $invokeStart) {
    $native.Substring($invokeStart, $invokeEnd - $invokeStart)
} else { '' }
Assert-Contract 'native bridge accepts KeyChar and rejects invalid selector, kind, or payload' (
    $invokeBlock.Contains('selector == 0u') -and
    $invokeBlock.Contains('kind != kLaunchFlagInputKeyChar') -and
    $invokeBlock.Contains('payload > kLaunchFlagInputPayloadMask') -and
    $invokeBlock.Contains('return -2;'))

$registerStart = $native.IndexOf('if (!compositor::KernelCompositor::registerWindow(window))')
$focusStart = $native.IndexOf('compositor::KernelCompositor::setFocus(m_window->id);', $registerStart)
Assert-Contract 'normal managed window creation registers and focuses its window' (
    $registerStart -ge 0 -and $focusStart -gt $registerStart)

$dispatchStart = $managed.IndexOf('public static int Dispatch(NativeGxAppContext* context)')
$tryFindStart = $managed.IndexOf('bool descriptorFound = TryFind(selector,', $dispatchStart)
$callbackStart = $managed.IndexOf('result = application.HandleInput(host, input);', $tryFindStart)
$dispatchBlock = if ($dispatchStart -ge 0 -and $tryFindStart -gt $dispatchStart) {
    $managed.Substring($dispatchStart, $tryFindStart - $dispatchStart)
} else { '' }
Assert-Contract 'managed dispatch records its registry snapshot before descriptor lookup' (
    $dispatchBlock.Contains('input-dispatch-entry') -and
    $dispatchBlock.Contains('LogC188R11R2Keyboard(host, "managed-dispatch-entry"u8, -1)'))
Assert-Contract 'Notes callback entry is marked and the state is returned after HandleInput' (
    $notes.Contains('HostLogProof.GuideXosApplicationRegistry.LogC188R11R2Keyboard(') -and
    $notes.Contains('"notes-callback-entry"u8') -and
    $callbackStart -gt $tryFindStart -and
    $managed.Contains('LogC188R11R2Keyboard(host, "managed-callback-return"u8'))

$unfocused = $textArea.IndexOf('if (!_isFocused) return GuideXosTextAreaEditResult.Ignored;',
    $textArea.IndexOf('GuideXosTextAreaEditResult HandleCharacter'))
$pointerFocus = $textArea.IndexOf('Focus();',
    $textArea.IndexOf('GuideXosTextAreaEditResult HandlePointerDown'))
Assert-Contract 'Notes text insertion requires editor focus established by pointer-down' (
    $unfocused -ge 0 -and $pointerFocus -gt $textArea.IndexOf('GuideXosTextAreaEditResult HandlePointerDown'))

$editorTraceStart = $notes.IndexOf('private void LogC188R11R4R2EditorState(')
$editorTraceEnd = $notes.IndexOf('#endif', $editorTraceStart)
$editorTrace = if ($editorTraceStart -ge 0 -and $editorTraceEnd -gt $editorTraceStart) {
    $notes.Substring($editorTraceStart, $editorTraceEnd - $editorTraceStart)
} else { '' }
Assert-Contract 'Notes editor observations are compile-gated, bounded to 256 characters, and hex encoded' (
    $notes.Contains('#if HOSTLOGPROOF_C188R11_MANAGED_STATE_PROVENANCE') -and
    $editorTrace.Contains('GuideXosTextArea.DefaultMaximumCharacters') -and
    $editorTrace.Contains('int chunkCount = Math.Max(1, (text.Length + 31) / 32)') -and
    $editorTrace.Contains('C188R11R4R2-TEXT stage=') -and
    $editorTrace.Contains('C188R11R4R2-NOTES stage=') -and
    -not $editorTrace.Contains('unsafe'))

$editorPointerDispatch = $notes.IndexOf('if (input.Kind == GuideXosInputKind.PointerDown)',
    $notes.IndexOf('public override GuideXosResult HandleInput', $notes.IndexOf('public sealed partial class ManagedNotes')))
$pointerHandled = $notes.IndexOf('_textArea.HandlePointerDown(', $editorPointerDispatch)
$focusSnapshot = $notes.IndexOf('"after-pointer"u8', $pointerHandled)
$keySnapshot = $notes.IndexOf('LogC188R11R4R2EditorState(host, "after-key"u8', $focusSnapshot)
Assert-Contract 'diagnostic snapshots straddle the source focus/edit call and completed Notes redraw' (
    $editorPointerDispatch -ge 0 -and $pointerHandled -gt $editorPointerDispatch -and
    $focusSnapshot -gt $pointerHandled -and $keySnapshot -gt $focusSnapshot -and
    $editorTrace.Contains('renderSucceeded ? 1u : 0u'))

$c120HandlerStart = $notes.IndexOf('private GuideXosResult HandleC120Input(')
$c120FocusCall = $notes.IndexOf('_mainControlHost.FocusAndRoutePointer(',
    $c120HandlerStart)
$c120PointerSnapshot = $notes.IndexOf('"after-pointer"u8', $c120FocusCall)
$c120KeyCall = $notes.IndexOf('_mainControlHost.HandleInput(input)',
    $c120HandlerStart)
$c120KeySnapshot = $notes.IndexOf('"after-key"u8', $c120KeyCall)
Assert-Contract 'C120 control-host document focus and key routes emit the same bounded editor snapshots' (
    $c120HandlerStart -ge 0 -and $c120FocusCall -gt $c120HandlerStart -and
    $c120PointerSnapshot -gt $c120FocusCall -and
    $c120KeyCall -gt $c120PointerSnapshot -and
    $c120KeySnapshot -gt $c120KeyCall -and
    $notes.Contains('traceC188R4R2DocumentPointer') -and
    $notes.Contains('traceC188R4R2DocumentKey'))

Assert-Contract 'guest runner performs one deterministic editor click, clears the pointer, then sends one a transaction' (
    ([regex]::Matches($r2Runner, "Invoke-C188R11R2Control @\('click'")).Count -eq 1 -and
    ([regex]::Matches($r2Runner, "Invoke-C188R11R2Control @\('key','a'")).Count -eq 1 -and
    $r2Runner.IndexOf("'r2-qmp-click-notes-editor'") -lt
        $r2Runner.IndexOf("'r2-qmp-move-pointer-clear'") -and
    $r2Runner.IndexOf("'r2-qmp-move-pointer-clear'") -lt
        $r2Runner.IndexOf("'r2-qmp-key-a-down-up'") -and
    $r2Runner.Contains('localEditorY=99') -and
    $r2Runner.Contains('y=00000063 result=PASS') -and
    $r2Runner.Contains("@('capture','before-input')") -and
    $r2Runner.Contains("@('capture','after-input')"))

Assert-Contract 'text-entry acceptance requires buffer content at the previous caret, length and caret increments, focus, and redraw' (
    $r4r2Predicates.Contains('$afterKey.text -ceq $beforeKey.text.Insert($beforeKey.caret, ''a'')') -and
    $r4r2Predicates.Contains('$afterKey.length -eq $beforeKey.length + 1') -and
    $r4r2Predicates.Contains('$afterKey.caret -eq $beforeKey.caret + 1') -and
    $r4r2Predicates.Contains('$afterPointer.focused') -and
    $r4r2Predicates.Contains('$afterKey.render') -and
    $r4r2Predicates.Contains('documentControlRouteEstablished') -and
    $r4r2Predicates.Contains('documentRouteCorrelatedToPointer') -and
    $r4r2Predicates.Contains('C120-POINTER target=document result=PASS'))

Assert-Contract 'runner separates active Notes window, C120 route, editable document focus, unrelated control focus, and unknown focus' (
    $r4r2Predicates.Contains('notesWindowActive=') -and
    $r4r2Predicates.Contains('documentControlRouteEstablished=') -and
    $r4r2Predicates.Contains('editableDocumentWidgetFocused=') -and
    $r4r2Predicates.Contains('unrelatedControlFocused=') -and
    $r4r2Predicates.Contains('focusUnknown=') -and
    $r2Runner.Contains('$focusEvidence.documentControlRouteEstablished') -and
    $r2Runner.Contains('$focusEvidence.editableDocumentWidgetFocused'))

Assert-Contract 'runner repeats full QAC ownership proof after before-input capture and before the only key transaction' (
    $r2Runner.IndexOf("@('capture','before-input')") -lt
        $r2Runner.IndexOf('$preKeyQacStatus =') -and
    $r2Runner.IndexOf('$preKeyQacStatus =') -lt
        $r2Runner.IndexOf('$preKeyOwnership = Get-C188R11R4QacOwnershipEvidence') -and
    $r2Runner.IndexOf('$preKeyOwnership.accepted') -lt
        $r2Runner.IndexOf("@('key','a')") -and
    $r2Runner.Contains('qac-prekey-ownership.json') -and
    $r2Runner.Contains('inspect ([string]$active.qacReservationId)'))

Assert-Contract 'after-input capture waits for Notes redraw evidence and a bounded settling interval' (
    $r2Runner.Contains('$afterKeySnapshot.render') -and
    $r2Runner.Contains('Start-Sleep -Milliseconds 250') -and
    $r2Runner.IndexOf('Start-Sleep -Milliseconds 250') -lt
        $r2Runner.IndexOf("@('capture','after-input')"))

Assert-Contract 'keyboard trace is bounded and serial output is outside the IRQ handler' (
    $ps2.Contains('s_c188r11r2TraceRecordsWritten >= 16u') -and
    $ps2.Contains('s_c188r11r2TraceRecordsPrinted < 16u') -and
    $ps2.Contains('void flush_c188r11r2_key_traces()') -and
    -not $ps2.Substring($ps2.IndexOf('void irq_handler()'),
        $ps2.IndexOf('bool has_key()') - $ps2.IndexOf('void irq_handler()')).Contains('serial::puts("[C188R11R2_KEY]'))
Assert-Contract 'R2 runner accepts a unique isolated candidate below its phase root' (
    $r2Runner.Contains("[string]`$CandidateRoot = ''") -and
    $r2Runner.Contains('[System.IO.Path]::GetFullPath($CandidateRoot)') -and
    $r2Runner.Contains('[System.IO.Path]::GetRelativePath($phaseRoot, $candidateRoot)') -and
    $r2Runner.Contains('$candidateRelative.StartsWith'))

Assert-Contract 'memory watchpoints attach only to the QAC-owned loopback GDB stub' (
    $r2Runner.Contains('[switch]$MemoryWatchpoints') -and
    $r2Runner.Contains("if (`$MemoryWatchpoints) { `$runnerArguments += '-EnableGdbStub' }") -and
    $r2Runner.Contains('target remote 127.0.0.1:$GdbPort') -and
    $qacRunner.Contains("if (`$EnableGdbStub -and -not `$isC188R11Boot)"))
Assert-Contract 'memory watchpoints cover only the selected fields and stop after sixteen hits' (
    $r2Runner.Contains('watch *`$c188_len_addr') -and
    $r2Runner.Contains('watch *`$c188_slot_addr') -and
    $r2Runner.Contains('if `$c188_hits >= 16'))
Assert-Contract 'input gate requires a fresh inspected reservation and correlated runner/child process identities' (
    $r2Runner.Contains('$duringCensusDeadline') -and
    $r2Runner.Contains('inspect ([string]$active.qacReservationId)') -and
    $r2Runner.Contains('Get-C188R11R4QacOwnershipEvidence') -and
    $r2Runner.Contains('$inputOwnership.accepted') -and
    $r2Runner.Contains("'GuideXos.C188.LiveProcessCapture' -as [type]") -and
    $r2Runner.Contains('ReadAllTextShared($qacStderrPath)') -and
    $qacRunner.Contains('Get-C188R11R4QacOwnershipEvidence') -and
    $qacRunner.Contains('QEMU processes:\s+1 \(0 foreign\)') -and
    $qacRunner.Contains('Managed sessions:\s+1; queued:\s+0; blocked:\s+0') -and
    $qacRunner.Contains('qacOwnershipRunIdentity=$qacOwnershipRunIdentity'))
Assert-Contract 'QAC census and ownership snapshots are persisted before any fail-closed gate throws' (
    $qacRunner.IndexOf('Set-Content -LiteralPath $qacStatusDuringPath') -ge 0 -and
    $qacRunner.IndexOf('Set-Content -LiteralPath $qacStatusDuringPath') -lt
        $qacRunner.IndexOf('$ownership = Get-C188R11R4QacOwnershipEvidence') -and
    $qacRunner.IndexOf('$ownership | ConvertTo-Json -Depth 5') -lt
        $qacRunner.IndexOf('if (-not [bool]$ownership.accepted)'))

Write-Output "C188R11R2-KEYBOARD-CONTRACTS cases=$script:caseCount result=PASS"
