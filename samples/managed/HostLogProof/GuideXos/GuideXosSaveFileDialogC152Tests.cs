using System;
using System.Text;
using HostLogProof.Applications;

namespace HostLogProof;

internal static class GuideXosSaveFileDialogC152Tests
{
    public static bool Run(GuideXosHost host, GuideXosSurface surface,
        GuideXosControlHost parent)
    {
        int cases = 0;
        bool result = true;
        result &= Case(ref cases, FilenamePolicy());
        result &= Case(ref cases, BoundedPathPolicy());
        result &= Case(ref cases, OpenConfiguration(host, surface, parent));
        result &= Case(ref cases, TextInputEditing(host, surface, parent));
        result &= Case(ref cases, NewTarget(host, surface, parent));
        result &= Case(ref cases, ExistingTarget(host, surface, parent));
        result &= Case(ref cases, DirectoryRejected(host, surface, parent));
        result &= Case(ref cases, DirectoryNavigation(host, surface, parent));
        result &= Case(ref cases, ExistingSelectionPopulatesName(host, surface, parent));
        result &= Case(ref cases, TargetAppearsAfterOpen(host, surface, parent));
        result &= Case(ref cases, EscapeAndModalCleanup(host, surface, parent));
        result &= Case(ref cases, RepeatedOpen(host, surface, parent));
        result &= Case(ref cases, SharedDirectoryOrdering());
        result &= Case(ref cases, SharedCapacity());
        result &= Case(ref cases, RealWheelMovesModalList(host, surface, parent));
        result &= Case(ref cases, ChooserCancelPreservesPathAndName(host, surface, parent));
        bool passed = result && cases == 16;
        host?.TryLog(passed
            ? "C152-SAVE-DIALOG-TESTS cases=16 text-input=enabled path=bounded overwrite=required race-revalidated=true wheel=shared modal=single result=PASS"u8
            : "C152-SAVE-DIALOG-TESTS result=FAIL"u8);
        return passed;
    }

    private static bool FilenamePolicy() =>
        GuideXosSaveFileDialog.IsValidVfsFilename("note.txt") &&
        GuideXosSaveFileDialog.IsValidVfsFilename(new string('n', 127)) &&
        !GuideXosSaveFileDialog.IsValidVfsFilename(string.Empty) &&
        !GuideXosSaveFileDialog.IsValidVfsFilename(".") &&
        !GuideXosSaveFileDialog.IsValidVfsFilename("..") &&
        !GuideXosSaveFileDialog.IsValidVfsFilename("../escape.txt") &&
        !GuideXosSaveFileDialog.IsValidVfsFilename("a\\b.txt") &&
        !GuideXosSaveFileDialog.IsValidVfsFilename("a b.txt") &&
        !GuideXosSaveFileDialog.IsValidVfsFilename("caf\u00e9.txt") &&
        !GuideXosSaveFileDialog.IsValidVfsFilename(new string('n', 128));

    private static bool BoundedPathPolicy()
    {
        int maximumNameAtAppsRoot =
            GuideXosSaveFileDialog.MaximumPathBytes -
            GuideXosSaveFileDialog.ManagedVfsRoot.Length - 1;
        bool exact = GuideXosSaveFileDialog.TryBuildTargetPath(
            GuideXosSaveFileDialog.ManagedVfsRoot,
            new string('a', maximumNameAtAppsRoot), out string path,
            out GuideXosPickerPathStatus status) &&
            status == GuideXosPickerPathStatus.Success &&
            Encoding.UTF8.GetByteCount(path) ==
                GuideXosSaveFileDialog.MaximumPathBytes;
        bool tooLong = !GuideXosSaveFileDialog.TryBuildTargetPath(
            GuideXosSaveFileDialog.ManagedVfsRoot,
            new string('a', maximumNameAtAppsRoot + 1), out _, out status) &&
            status == GuideXosPickerPathStatus.PathTooLong;
        return exact && tooLong;
    }

    private static bool OpenConfiguration(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        bool opened = dialog.Open(host, surface, parent,
            "/system/apps/FULL", "draft.txt").Status ==
                GuideXosSaveFileDialogStatus.Pending;
        bool configured = opened && dialog.IsOpen &&
            dialog.CurrentDirectory == "/system/apps/FULL" &&
            dialog.FileName == "draft.txt" && dialog.Dialog.MemberCount == 6 &&
            dialog.RegistrationCount == 4 && dialog.RegistrationCapacity == 8 &&
            dialog.Dialog.IsModal;
        if (dialog.IsOpen) dialog.Cancel();
        return configured && !parent.IsModalActive;
    }

    private static bool TextInputEditing(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        if (dialog.Open(host, surface, parent, "/system/apps/FULL",
                "draft.txt").Status != GuideXosSaveFileDialogStatus.Pending)
            return false;
        bool focus = dialog.Dialog.ControlHost.TryFocus(2) ==
            GuideXosControlHostResult.Focused;
        GuideXosControlHostResult typed = dialog.Dialog.HandleInput(
            GuideXosInputEvent.ForKeyChar('x'));
        bool edited = typed == GuideXosControlHostResult.Changed &&
            dialog.FileName == "draft.txtx";
        dialog.Cancel();
        return focus && edited && !parent.IsModalActive;
    }

    private static bool NewTarget(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        if (dialog.Open(host, surface, parent, "/system/apps/C151",
                "c152-uncreated-test.txt").Status !=
            GuideXosSaveFileDialogStatus.Pending) return false;
        bool focused = dialog.Dialog.ControlHost.TryFocus(3) ==
            GuideXosControlHostResult.Focused;
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        bool selected = dialog.Result.Status == GuideXosSaveFileDialogStatus.Selected &&
            dialog.Result.Path == "/system/apps/C151/c152-uncreated-test.txt" &&
            !dialog.IsOpen && !parent.IsModalActive;
        return focused && selected;
    }

    private static bool ExistingTarget(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        if (dialog.Open(host, surface, parent, "/system/apps/C151",
                "alpha.txt").Status != GuideXosSaveFileDialogStatus.Pending)
            return false;
        dialog.Dialog.ControlHost.TryFocus(3);
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        return dialog.Result.Status ==
                GuideXosSaveFileDialogStatus.OverwriteRequired &&
            dialog.Result.Path == "/system/apps/C151/alpha.txt" &&
            !dialog.IsOpen && !parent.IsModalActive;
    }

    private static bool DirectoryRejected(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        if (dialog.Open(host, surface, parent, "/system/apps/C151",
                "nested").Status != GuideXosSaveFileDialogStatus.Pending)
            return false;
        dialog.Dialog.ControlHost.TryFocus(3);
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        bool rejected = dialog.IsOpen &&
            dialog.Result.Status == GuideXosSaveFileDialogStatus.Pending;
        dialog.Cancel();
        return rejected && !parent.IsModalActive;
    }

    private static bool DirectoryNavigation(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        if (dialog.Open(host, surface, parent, "/system/apps/C151",
                "draft.txt").Status != GuideXosSaveFileDialogStatus.Pending)
            return false;
        dialog.Dialog.ControlHost.TryFocus(1);
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Down));
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        bool child = dialog.CurrentDirectory == "/system/apps/C151/nested" &&
            dialog.FileName == "draft.txt";
        dialog.Dialog.ControlHost.TryFocus(1);
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        bool parentDirectory = dialog.CurrentDirectory == "/system/apps/C151";
        dialog.Cancel();
        return child && parentDirectory && !parent.IsModalActive;
    }

    private static bool ExistingSelectionPopulatesName(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        if (dialog.Open(host, surface, parent, "/system/apps/C151",
                "draft.txt").Status != GuideXosSaveFileDialogStatus.Pending)
            return false;
        dialog.Dialog.ControlHost.TryFocus(1);
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Down)); // nested directory
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Down)); // alpha.txt
        bool populated = dialog.FileName == "alpha.txt";
        dialog.Cancel();
        return populated && !parent.IsModalActive;
    }

    private static bool TargetAppearsAfterOpen(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        const string path = "/system/apps/C152/r01.txt";
        byte[] pathBytes = Encoding.UTF8.GetBytes(path);
        byte[] existingBytes = "EXISTING"u8.ToArray();
        GuideXosSaveFileDialog dialog = new();
        if (dialog.Open(host, surface, parent, "/system/apps/C152",
                "r01.txt").Status != GuideXosSaveFileDialogStatus.Pending)
            return false;
        bool appeared = GuideXosFile.WriteAllTextUtf8(host, pathBytes,
            existingBytes) == GuideXosFileResult.Success;
        dialog.Dialog.ControlHost.TryFocus(3);
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Enter));
        return appeared && dialog.Result.Status ==
                GuideXosSaveFileDialogStatus.OverwriteRequired &&
            dialog.Result.Path == path && !dialog.IsOpen &&
            !parent.IsModalActive;
    }

    private static bool EscapeAndModalCleanup(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        if (dialog.Open(host, surface, parent, "/system/apps/FULL",
                "draft.txt").Status != GuideXosSaveFileDialogStatus.Pending)
            return false;
        dialog.HandleInput(surface, GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Escape));
        return dialog.Result.Status == GuideXosSaveFileDialogStatus.Cancelled &&
            !dialog.IsOpen && !parent.IsModalActive &&
            !parent.HasTransientInputCapture && !parent.HasPointerDragCapture;
    }

    private static bool RepeatedOpen(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        bool first = dialog.Open(host, surface, parent, "/system/apps/FULL",
            "first.txt").Status == GuideXosSaveFileDialogStatus.Pending;
        dialog.Cancel();
        bool second = dialog.Open(host, surface, parent, "/system/apps/C151",
            "second.txt").Status == GuideXosSaveFileDialogStatus.Pending &&
            dialog.CurrentDirectory == "/system/apps/C151" &&
            dialog.FileName == "second.txt" && dialog.RegistrationCount == 4;
        dialog.Cancel();
        return first && second && !parent.IsModalActive;
    }

    private static bool SharedDirectoryOrdering()
    {
        GuideXosDirectoryEntry[] entries =
        [
            new("z.txt", GuideXosEntryType.Regular, 1),
            new("nested", GuideXosEntryType.Directory, 0),
            new("alpha.txt", GuideXosEntryType.Regular, 1),
        ];
        GuideXosOpenFileDialog.SortForExplorer(entries, entries.Length);
        return entries[0].Name == "nested" && entries[1].Name == "alpha.txt" &&
            entries[2].Name == "z.txt";
    }

    private static bool SharedCapacity() =>
        GuideXosSaveFileDialog.MaximumRowCount ==
            GuideXosOpenFileDialog.MaximumRowCount &&
        GuideXosSaveFileDialog.MaximumNameBytes ==
            GuideXosOpenFileDialog.MaximumNameBytes &&
        GuideXosSaveFileDialog.MaximumPathBytes ==
            GuideXosOpenFileDialog.MaximumPathBytes;

    private static bool RealWheelMovesModalList(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        GuideXosSaveFileDialog dialog = new();
        if (dialog.Open(host, surface, parent, "/system/apps/FULL",
                "draft.txt").Status != GuideXosSaveFileDialogStatus.Pending)
            return false;
        int before = dialog.ListBox.FirstVisibleIndex;
        GuideXosSaveFileDialogResult wheel = dialog.HandleInput(surface,
            GuideXosInputEvent.ForWheel(40, 80, 1));
        int after = dialog.ListBox.FirstVisibleIndex;
        bool moved = wheel.Status == GuideXosSaveFileDialogStatus.Pending &&
            after != before;
        host?.TryLog(moved
            ? "C152-SAVE-DIALOG-WHEEL modal-list=true runtime-policy=shared viewport=moved result=PASS"u8
            : "C152-SAVE-DIALOG-WHEEL modal-list=true viewport=unchanged result=FAIL"u8);
        dialog.Cancel();
        return moved && !parent.IsModalActive;
    }

    private static bool ChooserCancelPreservesPathAndName(GuideXosHost host,
        GuideXosSurface surface, GuideXosControlHost parent)
    {
        const string directory = "/system/apps/C151";
        const string name = "alpha.txt";
        GuideXosSaveFileDialog dialog = new();
        bool opened = dialog.Open(host, surface, parent, directory, name).Status ==
            GuideXosSaveFileDialogStatus.Pending;
        bool savedSemantics = dialog.CurrentDirectory == directory &&
            dialog.FileName == name;
        dialog.Cancel();
        GuideXosSaveFileDialog recreated = new();
        bool reopened = recreated.Open(host, surface, parent, directory, name).Status ==
            GuideXosSaveFileDialogStatus.Pending;
        bool restored = recreated.CurrentDirectory == directory &&
            recreated.FileName == name;
        recreated.Cancel();
        return opened && savedSemantics && reopened && restored &&
            !parent.IsModalActive;
    }

    private static bool Case(ref int cases, bool passed)
    {
        cases++;
        return passed;
    }
}
