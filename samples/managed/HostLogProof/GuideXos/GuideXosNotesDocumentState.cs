using System;

namespace HostLogProof;

/// <summary>
/// Bounded Notes lifecycle state. The existing TextArea remains the sole
/// document-text buffer; this model owns only path identity and dirty state.
/// </summary>
public sealed class GuideXosNotesDocumentState
{
    private string _currentPath = string.Empty;

    public string CurrentPath => _currentPath;
    public bool HasCurrentPath => _currentPath.Length != 0;
    public bool Dirty { get; private set; }

    public bool InitializeUntitled(GuideXosTextArea document)
    {
        if (document == null || !document.SetText(string.Empty)) return false;
        _currentPath = string.Empty;
        Dirty = false;
        return true;
    }

    /// <summary>Hydrates text and identity together only after validation succeeds.</summary>
    public bool TryHydrate(
        GuideXosTextArea document, string path, ReadOnlySpan<byte> utf8)
    {
        if (document == null || !IsValidPath(path) ||
            !document.SetUtf8(utf8))
        {
            return false;
        }
        _currentPath = path;
        Dirty = false;
        return true;
    }

    public void MarkEdited() => Dirty = true;

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

    public bool MarkSaveSucceeded(string path)
    {
        if (!IsValidPath(path)) return false;
        _currentPath = path;
        Dirty = false;
        return true;
    }

    public void MarkSaveFailed() => Dirty = true;

    public void MarkClean() => Dirty = false;

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
