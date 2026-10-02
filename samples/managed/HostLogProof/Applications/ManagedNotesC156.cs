#if HOSTLOGPROOF_C156_CONTROL_MODIFIER_SHORTCUTS
using System;

namespace HostLogProof.Applications;

public sealed partial class ManagedNotes
{
    private bool _c156ShortcutDispatch;

    internal static uint MapC156Shortcut(GuideXosInputEvent input)
    {
        if (input.Kind != GuideXosInputKind.KeyDown || !input.Control)
            return 0u;

        uint key = input.KeyCode;
        if (key >= (uint)'A' && key <= (uint)'Z') key += (uint)('a' - 'A');
        return key switch
        {
            (uint)'z' => C153UndoActionId,
            (uint)'y' => C153RedoActionId,
            (uint)'c' => C154CopyActionId,
            (uint)'x' => C154CutActionId,
            (uint)'v' => C154PasteActionId,
            (uint)'s' when input.Shift => 21u,
            (uint)'s' => 22u,
            (uint)'o' => 20u,
            _ => 0u,
        };
    }

    private bool TryHandleC156Shortcut(GuideXosHost host,
        GuideXosSurface surface, GuideXosInputEvent input,
        out GuideXosResult result)
    {
        result = GuideXosResult.Success;
        if (!input.Control || input.Kind is not
                (GuideXosInputKind.KeyDown or GuideXosInputKind.KeyChar))
            return false;

        // Dialogs and the file picker are handled before this method. An open
        // popup/transient control owns keyboard input and keeps its existing
        // navigation and capture rules.
        if (_picker.IsActive || _c151OpenFileDialog.IsOpen ||
            _c152SaveFileDialog?.IsOpen == true || _c152DecisionDialog.IsOpen ||
            _mainControlHost?.IsModalActive == true ||
            _mainControlHost?.HasTransientInputCapture == true ||
            _c135Menu.IsOpen)
        {
            return false;
        }

        if (input.Kind == GuideXosInputKind.KeyChar)
        {
            // Defensive managed boundary: a split KeyChar carrying Control is
            // consumed once and never reaches TextArea or a filename field.
            host.TryLog("C156-SHORTCUT event=key-char control=true consumed=true command=none text-leak=false result=PASS"u8);
            return true;
        }

        uint actionId = MapC156Shortcut(input);
        bool enabled = actionId switch
        {
            C153UndoActionId => _textArea.CanUndo,
            C153RedoActionId => _textArea.CanRedo,
            C154CopyActionId => _textArea.CanCopySelection,
            C154CutActionId => _textArea.CanCutSelection,
            C154PasteActionId => GuideXosClipboard.Shared.HasText &&
                _textArea.CanPasteText(GuideXosClipboard.Shared.TextSpan),
            20u or 21u or 22u => true,
            _ => false,
        };

        if (actionId != 0u && enabled)
        {
            _c156ShortcutDispatch = true;
            try
            {
                result = HandleAction(host, actionId);
            }
            finally
            {
                _c156ShortcutDispatch = false;
            }
        }
        else if (!RenderMain(host, surface, _launchCount))
        {
            result = GuideXosResult.InvalidArgument;
        }

        LogC156Shortcut(host, input, actionId, enabled,
            result == GuideXosResult.Success);
        return true;
    }

    private void LogC156Shortcut(GuideXosHost host,
        GuideXosInputEvent input, uint actionId, bool enabled, bool succeeded)
    {
        uint key = input.KeyCode;
        if (key >= (uint)'A' && key <= (uint)'Z') key += (uint)('a' - 'A');
        ReadOnlySpan<byte> keyName = key switch
        {
            (uint)'z' => "z"u8,
            (uint)'y' => "y"u8,
            (uint)'x' => "x"u8,
            (uint)'c' => "c"u8,
            (uint)'v' => "v"u8,
            (uint)'s' => "s"u8,
            (uint)'o' => "o"u8,
            _ => "other"u8,
        };
        ReadOnlySpan<byte> command = actionId switch
        {
            C153UndoActionId => "Undo"u8,
            C153RedoActionId => "Redo"u8,
            C154CutActionId => "Cut"u8,
            C154CopyActionId => "Copy"u8,
            C154PasteActionId => "Paste"u8,
            20u => "Open"u8,
            21u => "SaveAs"u8,
            22u => "Save"u8,
            _ => "none"u8,
        };
        Span<byte> line = stackalloc byte[160];
        int position = 0;
        if (GuideXosText.Append(line, ref position,
                "C156-SHORTCUT key="u8) &&
            GuideXosText.Append(line, ref position, keyName) &&
            GuideXosText.Append(line, ref position, " command="u8) &&
            GuideXosText.Append(line, ref position, command) &&
            GuideXosText.Append(line, ref position,
                input.Shift ? " shift=true"u8 : " shift=false"u8) &&
            GuideXosText.Append(line, ref position,
                enabled ? " enabled=true"u8 : " enabled=false"u8) &&
            GuideXosText.Append(line, ref position,
                " consumed=true keychar=none text-leak=false result="u8) &&
            GuideXosText.Append(line, ref position,
                succeeded ? "PASS"u8 : "FAIL"u8) &&
            GuideXosText.Append(line, ref position, " dirty="u8) &&
            GuideXosText.Append(line, ref position,
                _c152DocumentState.Dirty ? "true"u8 : "false"u8))
        {
            host.TryLog(line[..position]);
        }
    }
}

internal static class GuideXosNotesShortcutC156Tests
{
    private static int s_cases;

    internal static bool Run(GuideXosHost host)
    {
        s_cases = 0;
        bool result = Check(Map('z', true) == 0xC15301u) &&
            Check(Map('Y', true) == 0xC15302u) &&
            Check(Map('c', true) == 0xC15404u) &&
            Check(Map('x', true) == 0xC15403u) &&
            Check(Map('v', true) == 0xC15405u) &&
            Check(Map('s', true) == 22u) &&
            Check(Map('S', true, shift: true) == 21u) &&
            Check(Map('o', true) == 20u) &&
            Check(Map('q', true) == 0u) &&
            Check(Map('z', false) == 0u) &&
            Check(Map('c', true, kind: GuideXosInputKind.KeyChar) == 0u) &&
            Check(Map('\t', true) == 0u) &&
            Check(Map(' ', true) == 0u);
        host.TryLog(result && s_cases == 13
            ? "C156-SHORTCUT-ROUTING cases=13 recognized=7 unsupported=consumed repeat=platform result=PASS"u8
            : "C156-SHORTCUT-ROUTING result=FAIL"u8);
        return result && s_cases == 13;
    }

    private static uint Map(char key, bool control, bool shift = false,
        GuideXosInputKind kind = GuideXosInputKind.KeyDown)
    {
        GuideXosInputEvent input = kind == GuideXosInputKind.KeyChar
            ? GuideXosInputEvent.ForKeyChar(key, shift, control)
            : GuideXosInputEvent.ForKeyDown((GuideXosTextInputKey)key,
                shift, control);
        return ManagedNotes.MapC156Shortcut(input);
    }

    private static bool Check(bool condition)
    {
        ++s_cases;
        return condition;
    }
}
#endif
