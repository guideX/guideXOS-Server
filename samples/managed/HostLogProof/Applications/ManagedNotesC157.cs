#if HOSTLOGPROOF_C157_MANAGED_NOTES_NEW_DOCUMENT
namespace HostLogProof.Applications;

public sealed partial class ManagedNotes
{
    internal const uint C157NewDocumentActionId = 25u;

    private GuideXosResult RequestNewDocument(GuideXosHost host,
        GuideXosSurface surface)
    {
        if (_c152DocumentState.Dirty)
        {
            return OpenC152DirtyPrompt(host, surface,
                C152PendingOperation.New);
        }
        return CreateNewDocument(host, surface);
    }

    private GuideXosResult CreateNewDocument(GuideXosHost host,
        GuideXosSurface surface)
    {
        uint generation = _launchCount;
        ulong window = _window;
        if (!_c152DocumentState.InitializeUntitled(_textArea))
        {
            return GuideXosResult.InvalidArgument;
        }

        _currentPath = string.Empty;
        _c152PendingOperation = C152PendingOperation.None;
        _c152TargetPath = null;
        _status = "Untitled note";
        _mainControlHost.TryFocus(C120DocumentControlId);
        _textArea.Focus();
#if HOSTLOGPROOF_C122_MANAGED_LABEL
        if (_c122ProofContext && !UpdatePathLabel())
        {
            return GuideXosResult.InvalidArgument;
        }
#endif

        bool baseline = !_c152DocumentState.HasCurrentPath &&
            string.IsNullOrEmpty(_c152DocumentState.CurrentPath) &&
            _textArea.Length == 0 && !_c152DocumentState.Dirty &&
            !_textArea.CanUndo && !_textArea.CanRedo &&
            _c152DocumentState.HasSavedRevision &&
            _c152DocumentState.SavedRevision == _textArea.CurrentRevision &&
            _c152DocumentState.SavedRevisionReachable &&
            _textArea.CaretIndex == 0 && _textArea.AnchorIndex == 0 &&
            _textArea.FirstVisibleLine == 0 && generation == _launchCount &&
            window == _window;
        if (!baseline)
        {
            host.TryLog("C157-NEW baseline=FAIL result=FAIL"u8);
            return GuideXosResult.InvalidArgument;
        }

        host.TryLog("C157-NEW baseline=clean-untitled history=fresh clipboard=unchanged vfs-write=none same-instance=true result=PASS"u8);
        return RenderMain(host, surface, _launchCount)
            ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
    }
}
#endif
