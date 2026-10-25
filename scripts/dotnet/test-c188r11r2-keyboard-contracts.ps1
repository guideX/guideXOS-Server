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

Assert-Contract 'keyboard trace is bounded and serial output is outside the IRQ handler' (
    $ps2.Contains('s_c188r11r2TraceRecordsWritten >= 16u') -and
    $ps2.Contains('s_c188r11r2TraceRecordsPrinted < 16u') -and
    $ps2.Contains('void flush_c188r11r2_key_traces()') -and
    -not $ps2.Substring($ps2.IndexOf('void irq_handler()'),
        $ps2.IndexOf('bool has_key()') - $ps2.IndexOf('void irq_handler()')).Contains('serial::puts("[C188R11R2_KEY]'))

Write-Output "C188R11R2-KEYBOARD-CONTRACTS cases=$script:caseCount result=PASS"
