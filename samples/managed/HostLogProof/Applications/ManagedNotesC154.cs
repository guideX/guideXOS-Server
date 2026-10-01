#if HOSTLOGPROOF_C154_MANAGED_CLIPBOARD
using System;

namespace HostLogProof.Applications;

public sealed partial class ManagedNotes
{
    internal const uint C154CutActionId = 0xC15403u;
    internal const uint C154CopyActionId = 0xC15404u;
    internal const uint C154PasteActionId = 0xC15405u;

    private static bool s_c154ClipboardTestsRun;

    internal static bool RefreshC154MenuStates(GuideXosPopupMenu menu,
        GuideXosTextArea textArea, GuideXosClipboard clipboard)
    {
        if (menu == null || textArea == null || clipboard == null ||
            menu.ItemCount != 12)
        {
            return false;
        }
        return menu.SetItemEnabled(0, textArea.CanUndo) &&
            menu.SetItemEnabled(1, textArea.CanRedo) &&
            menu.SetItemEnabled(3, textArea.CanCutSelection) &&
            menu.SetItemEnabled(4, textArea.CanCopySelection) &&
            menu.SetItemEnabled(5, clipboard.HasText &&
                textArea.CanPasteText(clipboard.TextSpan));
    }

    private bool TryHandleC154Action(GuideXosHost host,
        GuideXosSurface surface, uint actionId, out GuideXosResult result)
    {
        result = GuideXosResult.InvalidAction;
        GuideXosClipboard clipboard = GuideXosClipboard.Shared;
        if (actionId == C154CopyActionId)
        {
            bool hadSelection = _textArea.HasSelection;
            bool copied = clipboard.TryCopySelectionFrom(_textArea);
            LogClipboardOperation(host, "C154-COPY"u8,
                clipboard.Length,
                copied ? " result=PASS"u8 : hadSelection
                    ? " result=REJECTED"u8 : " result=NOOP"u8);
            result = RenderC154Action(host, surface);
            return true;
        }

        if (actionId == C154CutActionId)
        {
            GuideXosTextAreaEditResult edit = _textArea.CutSelection(clipboard);
            LogClipboardOperation(host, "C154-CUT"u8,
                clipboard.Length,
                edit == GuideXosTextAreaEditResult.Changed
                    ? " content-change=once result=PASS"u8
                    : edit == GuideXosTextAreaEditResult.Ignored
                        ? " content-change=none result=NOOP"u8
                        : " content-change=none result=REJECTED"u8);
            if (edit == GuideXosTextAreaEditResult.Changed)
                FocusC154Document();
            result = RenderC154Action(host, surface);
            return true;
        }

        if (actionId == C154PasteActionId)
        {
            GuideXosTextAreaEditResult edit = clipboard.HasText
                ? _textArea.PasteText(clipboard.TextSpan)
                : GuideXosTextAreaEditResult.Ignored;
            LogClipboardOperation(host, "C154-PASTE"u8,
                clipboard.Length,
                edit == GuideXosTextAreaEditResult.Changed
                    ? " content-change=once result=PASS"u8
                    : edit == GuideXosTextAreaEditResult.Ignored
                        ? " content-change=none result=NOOP"u8
                        : " content-change=none result=REJECTED"u8);
            if (edit == GuideXosTextAreaEditResult.Changed)
                FocusC154Document();
            result = RenderC154Action(host, surface);
            return true;
        }

        return false;
    }

    private GuideXosResult RenderC154Action(GuideXosHost host,
        GuideXosSurface surface)
    {
        return RenderMain(host, surface, _launchCount)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }

    private void FocusC154Document()
    {
        _mainControlHost.TryFocus(C120DocumentControlId);
        _textArea.Focus();
        _status = _c152DocumentState.Dirty ? "Modified" : "Saved revision restored";
    }

    private static void LogClipboardOperation(GuideXosHost host,
        ReadOnlySpan<byte> operation, int length, ReadOnlySpan<byte> outcome)
    {
        Span<byte> line = stackalloc byte[96];
        int position = 0;
        if (!GuideXosText.Append(line, ref position, operation) ||
            !GuideXosText.Append(line, ref position, " length="u8) ||
            !GuideXosText.AppendUnsigned(line, ref position, (uint)length) ||
            !GuideXosText.Append(line, ref position, outcome)) return;
        host.TryLog(line[..position]);
    }
}
#endif
