using System;

namespace HostLogProof;

/// <summary>
/// Bounded Notes lifecycle state. TextArea owns text and edit history; this
/// model owns the current path and the identity of the last saved revision.
/// </summary>
public sealed class GuideXosNotesDocumentState
{
    private string _currentPath = string.Empty;
    private GuideXosTextArea _document;
    private GuideXosTextRevision _savedRevision;
    private bool _hasSavedRevision;

    public string CurrentPath => _currentPath;
    public bool HasCurrentPath => _currentPath.Length != 0;
    public GuideXosTextRevision SavedRevision => _savedRevision;
    public bool HasSavedRevision => _hasSavedRevision;
    public bool SavedRevisionReachable => _hasSavedRevision &&
        _document != null && _document.IsRevisionReachable(_savedRevision);
    public bool Dirty => _document != null &&
        (!_hasSavedRevision || !_savedRevision.IsValid ||
            !_document.IsRevisionReachable(_savedRevision) ||
            _document.CurrentRevision != _savedRevision);

    public bool InitializeUntitled(GuideXosTextArea document)
    {
        if (document == null || !document.SetText(string.Empty)) return false;
        Attach(document);
        _currentPath = string.Empty;
        MarkCurrentRevisionSaved();
        return true;
    }

    /// <summary>
    /// Hydrates text and identity together only after validation succeeds. The
    /// TextArea replacement establishes the sole baseline history state.
    /// </summary>
    public bool TryHydrate(
        GuideXosTextArea document, string path, ReadOnlySpan<byte> utf8)
    {
        if (document == null || !IsValidPath(path) ||
            !document.SetUtf8(utf8))
        {
            return false;
        }
        Attach(document);
        _currentPath = path;
        MarkCurrentRevisionSaved();
        return true;
    }

    public bool TrySetCurrentPath(string path)
    {
        if (string.IsNullOrEmpty(path))
        {
            _currentPath = string.Empty;
            return true;
        }
        if (!IsValidPath(path)) return false;
        _currentPath = path;
        return true;
    }

    /// <summary>Marks the displayed revision saved without clearing history.</summary>
    public bool MarkSaveSucceeded(string path)
    {
        if (!IsValidPath(path)) return false;
        _currentPath = path;
        MarkCurrentRevisionSaved();
        return true;
    }

    /// <summary>A failed write leaves both the checkpoint and history untouched.</summary>
    public void MarkSaveFailed()
    {
    }

    private void Attach(GuideXosTextArea document)
    {
        if (ReferenceEquals(_document, document)) return;
        if (_document != null)
        {
            _document.BaselineEstablished -= OnBaselineEstablished;
        }
        _document = document;
        _document.BaselineEstablished += OnBaselineEstablished;
    }

    private void OnBaselineEstablished(GuideXosTextRevision revision)
    {
        _savedRevision = revision;
        _hasSavedRevision = revision.IsValid;
    }

    private void MarkCurrentRevisionSaved()
    {
        if (_document == null)
        {
            _savedRevision = default;
            _hasSavedRevision = false;
            return;
        }
        _savedRevision = _document.CurrentRevision;
        _hasSavedRevision = _savedRevision.IsValid;
    }

    private static bool IsValidPath(string path)
    {
        if (string.IsNullOrEmpty(path)) return false;
        int separator = path.LastIndexOf('/');
        return separator >= GuideXosOpenFileDialog.ManagedVfsRoot.Length - 1 &&
            GuideXosPickerPath.TryBuildPath(path.Substring(0, separator),
                path.Substring(separator + 1), out string normalized) ==
            GuideXosPickerPathStatus.Success &&
            string.Equals(path, normalized, StringComparison.Ordinal);
    }
}
