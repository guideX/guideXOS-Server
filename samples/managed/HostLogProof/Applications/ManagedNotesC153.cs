using System;

namespace HostLogProof.Applications;

#if HOSTLOGPROOF_C153_MANAGED_TEXT_UNDO_REDO
public sealed partial class ManagedNotes
{
    private const uint C153UndoActionId = 0xC15301u;
    private const uint C153RedoActionId = 0xC15302u;

    private bool TryHandleC153Action(GuideXosHost host,
        GuideXosSurface surface, uint actionId, out GuideXosResult result)
    {
        result = GuideXosResult.InvalidAction;
        GuideXosTextAreaEditResult editResult;
        if (actionId == C153UndoActionId)
        {
            editResult = _textArea.Undo();
        }
        else if (actionId == C153RedoActionId)
        {
            editResult = _textArea.Redo();
        }
        else
        {
            return false;
        }

        result = GuideXosResult.Success;
        if (editResult == GuideXosTextAreaEditResult.Changed)
        {
            _mainControlHost.TryFocus(C120DocumentControlId);
            _textArea.Focus();
            _status = _c152DocumentState.Dirty
                ? "Modified" : "Saved revision restored";
            host.TryLog(actionId == C153UndoActionId
                ? _c156ShortcutDispatch
                    ? "C153-UNDO source=keyboard content-change=once result=PASS"u8
                    : "C153-UNDO source=menu content-change=once result=PASS"u8
                : _c156ShortcutDispatch
                    ? "C153-REDO source=keyboard content-change=once result=PASS"u8
                    : "C153-REDO source=menu content-change=once result=PASS"u8);
        }
        if (!RenderMain(host, surface, _launchCount))
        {
            result = GuideXosResult.InvalidArgument;
        }
        return true;
    }
}
#endif
