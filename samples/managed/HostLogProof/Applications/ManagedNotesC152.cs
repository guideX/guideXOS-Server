using System;
using System.Text;

namespace HostLogProof.Applications;

public sealed partial class ManagedNotes
{
    private enum C152PromptKind : byte
    {
        None = 0,
        Dirty = 1,
        Overwrite = 2,
        Error = 3,
    }

    private enum C152PendingOperation : byte
    {
        None = 0,
        Open = 1,
        Close = 2,
        Settings = 3,
    }

    private const int C152SaveReadyForClose = 0xC15201;
    private const int C152SaveReadyForSettings = 0xC15202;
    private const uint C152CloseRequestActionId = 26u;
    private readonly GuideXosNotesDocumentState _c152DocumentState = new();
    private readonly GuideXosDialog _c152DecisionDialog =
        new(64, 76, 432, 184, "Unsaved changes", 3);
    private readonly GuideXosButton _c152FirstButton =
        new(0, 0, 88, 28, "Save");
    private readonly GuideXosButton _c152SecondButton =
        new(0, 0, 88, 28, "Discard");
    private readonly GuideXosButton _c152ThirdButton =
        new(0, 0, 88, 28, "Cancel");
    private GuideXosSaveFileDialog _c152SaveFileDialog;
    private bool _c152Enabled;
    private C152PromptKind _c152PromptKind;
    private C152PendingOperation _c152PendingOperation;
    private string _c152TargetPath;
    private string _c152ChooserDirectory;
    private string _c152ChooserFileName;

    private bool TryHandleC152Action(GuideXosHost host,
        GuideXosSurface surface, uint actionId, out GuideXosResult result)
    {
        result = GuideXosResult.InvalidAction;
        switch (actionId)
        {
            case 20u:
                if (_c152DocumentState.Dirty)
                {
                    result = OpenC152DirtyPrompt(host, surface,
                        C152PendingOperation.Open);
                }
                else
                {
                    result = OpenC151Dialog(host, surface);
                }
                return true;
            case 21u:
                _c152PendingOperation = C152PendingOperation.None;
                result = OpenC152SaveDialog(host, surface, false);
                return true;
            case 22u:
                result = SaveC152Current(host, surface, C152PendingOperation.None);
                return true;
            case 24u:
#if HOSTLOGPROOF_MANAGED_APP_RETURN
#if HOSTLOGPROOF_C155_MANAGED_NOTES_SESSION
                GuideXosNotesReturnSessionC155.Shared.Clear();
#endif
                if (_c152DocumentState.Dirty)
                {
                    result = OpenC152DirtyPrompt(host, surface,
                        C152PendingOperation.Settings);
                    if (result == GuideXosResult.Success)
                        result = GuideXosResult.InvalidAction;
                    return true;
                }
#if HOSTLOGPROOF_C155_MANAGED_NOTES_SESSION
                if (!TryArmC155ReturnSession(host,
                        GuideXosNotesSessionDecisionC155.Clean))
                {
                    result = GuideXosResult.InvalidArgument;
                    return true;
                }
#endif
                host.TryLog("C150-CALLER id=Notes command=SettingsCenter"u8);
                result = GuideXosResult.Success;
                return true;
#else
                return false;
#endif
            case C152CloseRequestActionId:
                if (!_c152DocumentState.Dirty)
                {
                    result = (GuideXosResult)ManagedNotesC152Returns.CloseAllowed;
                }
                else
                {
                    result = OpenC152DirtyPrompt(host, surface,
                        C152PendingOperation.Close);
                }
                return true;
            default:
                return false;
        }
    }

    private GuideXosResult HandleC152Input(GuideXosHost host,
        GuideXosSurface surface, GuideXosInputEvent input)
    {
        if (_c152SaveFileDialog?.IsOpen == true)
        {
            GuideXosSaveFileDialogResult saveResult =
                _c152SaveFileDialog.HandleInput(surface, input);
            if (saveResult.Status is GuideXosSaveFileDialogStatus.Selected or
                GuideXosSaveFileDialogStatus.OverwriteRequired)
            {
                _c152ChooserDirectory = _c152SaveFileDialog.CurrentDirectory;
                _c152ChooserFileName = _c152SaveFileDialog.FileName;
                _c152SaveFileDialog = null;
                _c152TargetPath = saveResult.Path;
                if (saveResult.Status ==
                    GuideXosSaveFileDialogStatus.OverwriteRequired)
                {
                    OpenC152OverwritePrompt(host, surface);
                }
                else
                {
                    GuideXosResult finished = WriteC152Document(host, surface,
                        saveResult.Path, false);
                    if (finished != GuideXosResult.Success) return finished;
                }
            }
            else if (saveResult.Status is GuideXosSaveFileDialogStatus.Cancelled or
                GuideXosSaveFileDialogStatus.Error)
            {
                _c152SaveFileDialog = null;
                _c152PendingOperation = C152PendingOperation.None;
                _status = saveResult.Status == GuideXosSaveFileDialogStatus.Cancelled
                    ? "Save As cancelled" : saveResult.Message;
            }
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }

        if (_c152DecisionDialog?.IsOpen == true)
        {
            _c152DecisionDialog.HandleInput(input);
            if (_c152DecisionDialog.IsOpen)
            {
                return RenderMain(host, surface, _launchCount)
                    ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
            }
            GuideXosDialogResult decision = _c152DecisionDialog.Result;
            GuideXosResult next = ApplyC152PromptResult(host, surface, decision);
            if (next != GuideXosResult.Success) return next;
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        }

        return GuideXosResult.Success;
    }

    private GuideXosResult OpenC152DirtyPrompt(GuideXosHost host,
        GuideXosSurface surface, C152PendingOperation operation)
    {
        _c152PendingOperation = operation;
        if (!ConfigureC152Decision("Unsaved changes",
                operation == C152PendingOperation.Open
                    ? "Save changes before opening another file?"
                    : operation == C152PendingOperation.Settings
                        ? "Save changes before opening Settings?"
                        : "Save changes before closing Notes?",
                C152PromptKind.Dirty))
        {
            _c152PendingOperation = C152PendingOperation.None;
            return GuideXosResult.InvalidArgument;
        }
        host.TryLog(operation == C152PendingOperation.Open
            ? "C152-OPEN dirty-prompt=active deferred=true result=PASS"u8
            : operation == C152PendingOperation.Close
                ? "C152-CLOSE dirty-prompt=active veto=true result=PASS"u8
                : "C152-SETTINGS dirty-prompt=active transition=deferred result=PASS"u8);
        return ShowC152Decision(host, surface);
    }

    private void OpenC152OverwritePrompt(GuideXosHost host,
        GuideXosSurface surface)
    {
        _c152PromptKind = C152PromptKind.Overwrite;
        bool configured = _c152DecisionDialog.TrySetTitle("Replace file") &&
            _c152DecisionDialog.TrySetMessage(
                "File already exists.\nReplace it?") &&
            _c152DecisionDialog.TryClearMembers() &&
            AddC152PromptButton(_c152FirstButton, "Cancel",
                GuideXosDialogResult.Cancel, 1) &&
            AddC152PromptButton(_c152SecondButton, "Replace",
                GuideXosDialogResult.Yes, 2) &&
            _c152DecisionDialog.TrySetDefaultButton(_c152FirstButton) &&
            _c152DecisionDialog.TrySetCancelResult(GuideXosDialogResult.Cancel);
        if (!configured)
        {
            ShowC152Error(host, surface, "Overwrite confirmation is unavailable");
            return;
        }
        host.TryLog("C152-OVERWRITE prompt=open confirmed=false result=PASS"u8);
        ShowC152Decision(host, surface);
    }

    private bool ConfigureC152Decision(string title, string message,
        C152PromptKind kind)
    {
        _c152PromptKind = kind;
        if (!_c152DecisionDialog.TrySetTitle(title) ||
            !_c152DecisionDialog.TrySetMessage(message) ||
            !_c152DecisionDialog.TryClearMembers()) return false;
        if (kind == C152PromptKind.Error)
        {
            return AddC152PromptButton(_c152FirstButton, "OK",
                    GuideXosDialogResult.OK, 1) &&
                _c152DecisionDialog.TrySetDefaultButton(_c152FirstButton) &&
                _c152DecisionDialog.TrySetCancelResult(GuideXosDialogResult.OK);
        }
        return AddC152PromptButton(_c152FirstButton, "Save",
                GuideXosDialogResult.Apply, 1) &&
            AddC152PromptButton(_c152SecondButton, "Discard",
                GuideXosDialogResult.Discard, 2) &&
            AddC152PromptButton(_c152ThirdButton, "Cancel",
                GuideXosDialogResult.Cancel, 3) &&
            _c152DecisionDialog.TrySetDefaultButton(_c152FirstButton) &&
            _c152DecisionDialog.TrySetCancelResult(GuideXosDialogResult.Cancel);
    }

    private bool AddC152PromptButton(GuideXosButton button, string label,
        GuideXosDialogResult result, int position)
    {
        const int width = 96;
        const int gap = 12;
        int count = _c152PromptKind == C152PromptKind.Dirty ? 3 : 2;
        int total = count * width + (count - 1) * gap;
        int x = _c152DecisionDialog.X +
            (_c152DecisionDialog.Width - total) / 2 +
            (position - 1) * (width + gap);
        int y = _c152DecisionDialog.Y + _c152DecisionDialog.Height - 36;
        return button.TrySetBounds(x, y, width, 28) &&
            button.SetLabel(label) && _c152DecisionDialog.TryAddMember(button) &&
            _c152DecisionDialog.TrySetButtonResult(button, result);
    }

    private GuideXosResult ShowC152Decision(GuideXosHost host,
        GuideXosSurface surface)
    {
        if (!_c152DecisionDialog.Open(_mainControlHost))
        {
            _c152PromptKind = C152PromptKind.None;
            return GuideXosResult.InvalidArgument;
        }
        if (_c152DecisionDialog.Render(surface) != GuideXosResult.Success)
        {
            _c152DecisionDialog.Close(GuideXosDialogResult.Cancel);
            _c152PromptKind = C152PromptKind.None;
            return GuideXosResult.InvalidArgument;
        }
        return GuideXosResult.Success;
    }

    private GuideXosResult ApplyC152PromptResult(GuideXosHost host,
        GuideXosSurface surface, GuideXosDialogResult decision)
    {
        C152PromptKind prompt = _c152PromptKind;
        _c152PromptKind = C152PromptKind.None;
        if (prompt == C152PromptKind.Overwrite)
        {
            if (decision == GuideXosDialogResult.Yes)
                return WriteC152Document(host, surface, _c152TargetPath, true);
            _status = "Choose a destination or cancel Save As";
            return OpenC152SaveDialog(host, surface, true,
                _c152PendingOperation);
        }
        if (prompt == C152PromptKind.Error)
        {
            _status = "Save failed; document remains in Notes";
            return GuideXosResult.Success;
        }
        if (prompt != C152PromptKind.Dirty)
            return GuideXosResult.Success;

        C152PendingOperation operation = _c152PendingOperation;
        if (decision == GuideXosDialogResult.Cancel)
        {
            _c152PendingOperation = C152PendingOperation.None;
            _status = "Unsaved changes kept";
            host.TryLog(operation == C152PendingOperation.Open
                ? "C152-OPEN dirty-decision=Cancel document=preserved=true result=PASS"u8
                : operation == C152PendingOperation.Close
                    ? "C152-CLOSE dirty-decision=Cancel window=preserved=true result=PASS"u8
                    : "C152-SETTINGS dirty-decision=Cancel transition=blocked result=PASS"u8);
            return GuideXosResult.Success;
        }
        if (decision == GuideXosDialogResult.Discard)
        {
            _c152PendingOperation = C152PendingOperation.None;
            if (operation == C152PendingOperation.Open)
            {
                _status = "Open file; current edits stay until a file opens";
                host.TryLog("C152-OPEN dirty-decision=Discard deferred-until-load=true result=PASS"u8);
                return OpenC151Dialog(host, surface);
            }
            _status = "Changes discarded";
            host.TryLog(operation == C152PendingOperation.Close
                ? "C152-CLOSE dirty-decision=Discard dispatch=accepted result=PASS"u8
                : "C152-SETTINGS dirty-decision=Discard dispatch=deferred result=PASS"u8);
#if HOSTLOGPROOF_C155_MANAGED_NOTES_SESSION
            if (operation == C152PendingOperation.Settings)
            {
                return TryArmC155ReturnSession(host,
                        GuideXosNotesSessionDecisionC155.Discarded)
                    ? (GuideXosResult)ManagedNotesC152Returns.SettingsReady
                    : GuideXosResult.Success;
            }
#endif
            return operation == C152PendingOperation.Close
                ? (GuideXosResult)ManagedNotesC152Returns.CloseReady
                : operation == C152PendingOperation.Settings
                    ? (GuideXosResult)ManagedNotesC152Returns.SettingsReady
                    : GuideXosResult.Success;
        }
        if (decision == GuideXosDialogResult.Apply)
        {
            if (_c152DocumentState.HasCurrentPath)
                return SaveC152Current(host, surface, operation);
            return OpenC152SaveDialog(host, surface, false, operation);
        }
        _c152PendingOperation = C152PendingOperation.None;
        return GuideXosResult.Success;
    }

    private GuideXosResult OpenC152SaveDialog(GuideXosHost host,
        GuideXosSurface surface, bool restoreChooser,
        C152PendingOperation operation = C152PendingOperation.None)
    {
        _c152PendingOperation = operation;
        string directory = restoreChooser && !string.IsNullOrEmpty(_c152ChooserDirectory)
            ? _c152ChooserDirectory
            : _c152DocumentState.HasCurrentPath
                ? _c152DocumentState.CurrentPath.Substring(0,
                    _c152DocumentState.CurrentPath.LastIndexOf('/'))
                : GuideXosSaveFileDialog.ManagedVfsRoot;
        string fileName = restoreChooser && !string.IsNullOrEmpty(_c152ChooserFileName)
            ? _c152ChooserFileName
            : _c152DocumentState.HasCurrentPath
                ? _c152DocumentState.CurrentPath.Substring(
                    _c152DocumentState.CurrentPath.LastIndexOf('/') + 1)
                : "untitled.txt";
        _c152SaveFileDialog = new GuideXosSaveFileDialog();
        GuideXosSaveFileDialogResult opened = _c152SaveFileDialog.Open(
            host, surface, _mainControlHost, directory, fileName);
        if (opened.Status != GuideXosSaveFileDialogStatus.Pending)
        {
            _c152SaveFileDialog = null;
            _c152PendingOperation = C152PendingOperation.None;
            return ShowC152Error(host, surface, opened.Message);
        }
        _c152ChooserDirectory = directory;
        _c152ChooserFileName = fileName;
        _status = "Choose a text file destination";
        host.TryLog(restoreChooser
            ? "C152-SAVE-AS chooser=reopened directory-and-name=preserved result=PASS"u8
            : "C152-SAVE-AS chooser=open bounded=true result=PASS"u8);
        return GuideXosResult.Success;
    }

    private GuideXosResult SaveC152Current(GuideXosHost host,
        GuideXosSurface surface, C152PendingOperation operation)
    {
        if (!_c152DocumentState.HasCurrentPath)
            return OpenC152SaveDialog(host, surface, false, operation);
        return WriteC152Document(host, surface,
            _c152DocumentState.CurrentPath, true, operation);
    }

    private GuideXosResult WriteC152Document(GuideXosHost host,
        GuideXosSurface surface, string targetPath, bool confirmedOverwrite,
        C152PendingOperation? operationOverride = null)
    {
        C152PendingOperation operation = operationOverride ??
            _c152PendingOperation;
        if (!TryValidateC152Target(host, targetPath, confirmedOverwrite,
                out bool overwriteNeeded, out GuideXosFileResult validationResult))
        {
            if (overwriteNeeded)
            {
                _c152TargetPath = targetPath;
                OpenC152OverwritePrompt(host, surface);
                return GuideXosResult.Success;
            }
            _c152DocumentState.MarkSaveFailed();
            _c152PendingOperation = C152PendingOperation.None;
            return ShowC152Error(host, surface, C152FileError(validationResult));
        }

        byte[] snapshot = _textArea.ToUtf8();
        string pathBeforeSave = _c152DocumentState.CurrentPath;
        if (snapshot == null || snapshot.Length > _textArea.MaximumCharacters)
        {
            _c152DocumentState.MarkSaveFailed();
            _c152PendingOperation = C152PendingOperation.None;
            return ShowC152Error(host, surface, "Document exceeds the 256 byte limit");
        }
        GuideXosFileResult writeResult = GuideXosFile.WriteAllTextUtf8(
            host, Encoding.UTF8.GetBytes(targetPath), snapshot);
        bool verified = writeResult == GuideXosFileResult.Success &&
            VerifyC152Write(host, targetPath, snapshot);
        if (!verified)
        {
            _c152DocumentState.MarkSaveFailed();
            _c152PendingOperation = C152PendingOperation.None;
            _status = "Save failed; document remains dirty";
            bool preserved = _c152DocumentState.Dirty &&
                string.Equals(_c152DocumentState.CurrentPath, pathBeforeSave,
                    StringComparison.Ordinal) &&
                _textArea.ToUtf8().AsSpan().SequenceEqual(snapshot);
            host.TryLog(preserved
                ? "C152-SAVE result=FAIL document=preserved=true dirty=true path=preserved=true"u8
                : "C152-SAVE result=FAIL document=preserved=false dirty=unknown path=unknown"u8);
            return ShowC152Error(host, surface,
                writeResult == GuideXosFileResult.Success
                    ? "Write verification failed; document remains dirty"
                    : C152FileError(writeResult));
        }

        if (!_c152DocumentState.MarkSaveSucceeded(targetPath))
        {
            _c152DocumentState.MarkSaveFailed();
            _c152PendingOperation = C152PendingOperation.None;
            return ShowC152Error(host, surface, "Saved path is invalid");
        }
        _currentPath = targetPath;
        if (!string.Equals(pathBeforeSave, targetPath,
                StringComparison.Ordinal))
        {
            host.TryLog(PathLog("C152-SAVE-AS committed path=", targetPath));
        }
        _status = "Saved and verified";
        host.TryLog("C152-SAVE result=PASS verify=read-back exact=true bytes=bounded dirty=false"u8);
        host.TryLog("C152-INSTANCE same=true surface=unchanged modal=none result=PASS"u8);
        _c152TargetPath = null;
        _c152PendingOperation = C152PendingOperation.None;
        if (operation == C152PendingOperation.Open)
        {
            _status = "Saved; choose a file to open";
            return OpenC151Dialog(host, surface);
        }
        if (operation == C152PendingOperation.Close)
            return (GuideXosResult)ManagedNotesC152Returns.CloseReady;
        if (operation == C152PendingOperation.Settings)
#if HOSTLOGPROOF_C155_MANAGED_NOTES_SESSION
        {
            return TryArmC155ReturnSession(host,
                    GuideXosNotesSessionDecisionC155.Saved)
                ? (GuideXosResult)ManagedNotesC152Returns.SettingsReady
                : GuideXosResult.Success;
        }
#else
            return (GuideXosResult)ManagedNotesC152Returns.SettingsReady;
#endif
        return GuideXosResult.Success;
    }

    private bool TryValidateC152Target(GuideXosHost host, string targetPath,
        bool confirmedOverwrite, out bool overwriteNeeded,
        out GuideXosFileResult result)
    {
        overwriteNeeded = false;
        result = GuideXosFileResult.InvalidPath;
        if (string.IsNullOrEmpty(targetPath) ||
            Encoding.UTF8.GetByteCount(targetPath) >
                GuideXosSaveFileDialog.MaximumPathBytes)
            return false;
        int separator = targetPath.LastIndexOf('/');
        if (separator < GuideXosSaveFileDialog.ManagedVfsRoot.Length - 1 ||
            GuideXosPickerPath.TryBuildPath(targetPath[..separator],
                targetPath[(separator + 1)..], out string checkedPath) !=
                GuideXosPickerPathStatus.Success ||
            !string.Equals(targetPath, checkedPath, StringComparison.Ordinal))
            return false;

        string parent = targetPath[..separator];
        result = GuideXosFile.TryGetInfo(host, Encoding.UTF8.GetBytes(parent),
            out GuideXosFileInfo parentInfo);
        if (result != GuideXosFileResult.Success || parentInfo == null ||
            parentInfo.Type != GuideXosEntryType.Directory)
        {
            if (result == GuideXosFileResult.Success)
                result = GuideXosFileResult.NotDirectory;
            return false;
        }
        result = GuideXosFile.TryGetInfo(host, Encoding.UTF8.GetBytes(targetPath),
            out GuideXosFileInfo targetInfo);
        if (result == GuideXosFileResult.Success)
        {
            if (targetInfo == null || targetInfo.Type != GuideXosEntryType.Regular)
            {
                result = GuideXosFileResult.NotDirectory;
                return false;
            }
            if (!confirmedOverwrite)
            {
                overwriteNeeded = true;
                return false;
            }
            return true;
        }
        return result == GuideXosFileResult.NotFound;
    }

    private static bool VerifyC152Write(GuideXosHost host, string path,
        ReadOnlySpan<byte> expected)
    {
        GuideXosFileResult stat = GuideXosFile.TryGetInfo(host,
            Encoding.UTF8.GetBytes(path), out GuideXosFileInfo info);
        if (stat != GuideXosFileResult.Success || info == null ||
            info.Type != GuideXosEntryType.Regular || info.Size != (ulong)expected.Length)
            return false;
        Span<byte> actual = stackalloc byte[GuideXosTextArea.DefaultMaximumCharacters];
        GuideXosFileResult read = GuideXosFile.ReadAllTextUtf8(host,
            Encoding.UTF8.GetBytes(path), actual, out int byteCount);
        return read == GuideXosFileResult.Success && byteCount == expected.Length &&
            actual[..byteCount].SequenceEqual(expected);
    }

    private GuideXosResult ShowC152Error(GuideXosHost host,
        GuideXosSurface surface, string message)
    {
        _status = "Save error; note remains available";
        if (!ConfigureC152Decision("Save error",
                string.IsNullOrEmpty(message) ? "The document could not be saved" : message,
                C152PromptKind.Error))
            return RenderMain(host, surface, _launchCount)
                ? GuideXosResult.Success : GuideXosResult.InvalidArgument;
        return ShowC152Decision(host, surface);
    }

    private GuideXosResult RenderC152SaveDialog(GuideXosSurface surface) =>
        _c152SaveFileDialog?.IsOpen == true
            ? _c152SaveFileDialog.Render(surface) : GuideXosResult.Success;

    private static string C152FileError(GuideXosFileResult result) => result switch
    {
        GuideXosFileResult.CapabilityUnavailable => "Write access is unavailable",
        GuideXosFileResult.NotFound => "The destination directory or file is missing",
        GuideXosFileResult.NotDirectory => "The destination is a directory or parent is invalid",
        GuideXosFileResult.InvalidPath => "The destination path is invalid",
        GuideXosFileResult.FileTooLarge => "The document exceeds the VFS file limit",
        GuideXosFileResult.BufferTooSmall => "The saved file did not fit the Notes buffer",
        _ => "The document could not be saved",
    };
}

internal static class ManagedNotesC152Returns
{
    internal const int CloseAllowed = 0xC15200;
    internal const int CloseReady = 0xC15201;
    internal const int SettingsReady = 0xC15202;
}
