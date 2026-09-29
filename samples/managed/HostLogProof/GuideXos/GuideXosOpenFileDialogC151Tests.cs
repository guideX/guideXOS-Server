using System;
using HostLogProof.Applications;

namespace HostLogProof;

/// <summary>Bounded C151 model and Dialog/ListBox routing regressions.</summary>
internal static class GuideXosOpenFileDialogC151Tests
{
    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool result = true;
        result &= Case(host, ref cases, PathBoundsAndRoot(), "PathBoundsAndRoot"u8);
        result &= Case(host, ref cases, DirectoryFirstOrdering(), "DirectoryFirstOrdering"u8);
        result &= Case(host, ref cases, CaseFoldedStableOrdering(), "CaseFoldedStableOrdering"u8);
        result &= Case(host, ref cases, RootCapacityDoesNotTruncate(), "RootCapacityDoesNotTruncate"u8);
        result &= Case(host, ref cases, ChildCapacityShowsTruncation(), "ChildCapacityShowsTruncation"u8);
        result &= Case(host, ref cases, DialogAdmitsAndFocusesListBox(), "DialogAdmitsAndFocusesListBox"u8);
        result &= Case(host, ref cases, KeyboardRemainsInsideModal(), "KeyboardRemainsInsideModal"u8);
        result &= Case(host, ref cases, RuntimeWheelPolicyMovesOnlyModalList(host), "RuntimeWheelPolicyMovesOnlyModalList"u8);
        result &= Case(host, ref cases, PointerAndOutsideClickAreModal(), "PointerAndOutsideClickAreModal"u8);
        result &= Case(host, ref cases, EscapeRestoresParentFocus(), "EscapeRestoresParentFocus"u8);
        bool passed = result && cases == 10;
        host?.TryLog(passed
            ? "C151-CORE-TESTS cases=10 path=bounded ordering=deterministic capacity=explicit dialog-listbox=modal wheel=shared result=PASS"u8
            : "C151-CORE-TESTS result=FAIL"u8);
        return passed;
    }

    private static bool PathBoundsAndRoot()
    {
        bool root = GuideXosPickerPath.TryNormalizeDirectory(
            "/system/apps/", out string normalized) ==
                GuideXosPickerPathStatus.Success && normalized == "/system/apps";
        int fileNameAtPathLimit = (int)GxAbi.FilePathMaxBytes -
            normalized.Length - 1;
        bool maximumPath = GuideXosPickerPath.TryBuildPath(normalized,
            new string('a', fileNameAtPathLimit), out string path) ==
                GuideXosPickerPathStatus.Success &&
            path.Length == (int)GxAbi.FilePathMaxBytes;
        bool overPath = GuideXosPickerPath.TryBuildPath(normalized,
            new string('a', fileNameAtPathLimit + 1), out _) ==
                GuideXosPickerPathStatus.PathTooLong;
        bool traversal = GuideXosPickerPath.TryBuildPath(normalized,
            "../escape.txt", out _) == GuideXosPickerPathStatus.InvalidFilename;
        return root && maximumPath && overPath && traversal;
    }

    private static bool DirectoryFirstOrdering()
    {
        GuideXosDirectoryEntry[] entries =
        [
            new("z.txt", GuideXosEntryType.Regular, 1),
            new("beta", GuideXosEntryType.Directory, 0),
            new("alpha.txt", GuideXosEntryType.Regular, 1),
            new("Alpha", GuideXosEntryType.Directory, 0),
        ];
        GuideXosOpenFileDialog.SortForExplorer(entries, entries.Length);
        return entries[0].Name == "Alpha" &&
            entries[0].Type == GuideXosEntryType.Directory &&
            entries[1].Name == "beta" &&
            entries[2].Name == "alpha.txt" &&
            entries[3].Name == "z.txt";
    }

    private static bool CaseFoldedStableOrdering()
    {
        GuideXosDirectoryEntry upper =
            new("Report.TXT", GuideXosEntryType.Regular, 1);
        GuideXosDirectoryEntry lower =
            new("report.txt", GuideXosEntryType.Regular, 1);
        return GuideXosOpenFileDialog.CompareForExplorer(upper, lower) < 0 &&
            GuideXosOpenFileDialog.CompareForExplorer(lower, upper) > 0;
    }

    private static bool RootCapacityDoesNotTruncate()
    {
        int limit = GuideXosOpenFileDialog.ComputeEntryLimit(
            GuideXosOpenFileDialog.MaximumRowCount, false, false,
            GuideXosOpenFileDialog.MaximumRowCount, out bool truncated);
        return !truncated && limit == GuideXosOpenFileDialog.MaximumRowCount;
    }

    private static bool ChildCapacityShowsTruncation()
    {
        int limit = GuideXosOpenFileDialog.ComputeEntryLimit(
            GuideXosOpenFileDialog.MaximumDirectoryEntries, true, true,
            GuideXosOpenFileDialog.MaximumRowCount, out bool truncated);
        return truncated && limit == GuideXosOpenFileDialog.MaximumRowCount - 2;
    }

    private static bool DialogAdmitsAndFocusesListBox()
    {
        DialogFixture fixture = new();
        bool open = fixture.Dialog.Open(fixture.Parent) && fixture.Dialog.IsModal;
        bool passed = open && fixture.Dialog.MemberCount == 3 &&
            fixture.Dialog.RegistrationCount == 3 &&
            fixture.Dialog.ControlHost.ActiveControlId == 1 &&
            fixture.List.IsFocused;
        if (fixture.Dialog.IsOpen)
            fixture.Dialog.Close(GuideXosDialogResult.Cancel);
        return passed;
    }

    private static bool KeyboardRemainsInsideModal()
    {
        DialogFixture fixture = new();
        if (!fixture.Dialog.Open(fixture.Parent)) return false;
        fixture.Dialog.HandleInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Down));
        bool down = fixture.List.SelectedIndex == 1;
        fixture.Dialog.HandleInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Up));
        bool up = fixture.List.SelectedIndex == 0;
        fixture.Dialog.HandleInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Tab));
        bool tab = fixture.Dialog.ControlHost.ActiveControlId == 3;
        fixture.Dialog.HandleInput(GuideXosInputEvent.ForKeyDown(
            GuideXosTextInputKey.Tab, shift: true));
        bool shiftTab = fixture.Dialog.ControlHost.ActiveControlId == 1;
        fixture.Dialog.Close(GuideXosDialogResult.Cancel);
        return down && up && tab && shiftTab;
    }

    private static bool RuntimeWheelPolicyMovesOnlyModalList(GuideXosHost host)
    {
        DialogFixture fixture = new();
        GuideXosRuntimeSettingsState runtime = GuideXosRuntimeSettings.Active;
        RuntimeSettingsTestCapture prior = runtime.CaptureForTests();
        try
        {
            ManagedSettingsSnapshot settings = ManagedSettingsSnapshot.Defaults;
            settings.NaturalScroll = true;
            settings.ScrollLinesPerNotch = 5;
            bool settingsReady = runtime.TryCommit(settings);
            bool dialogOpened = settingsReady && fixture.Dialog.Open(fixture.Parent);
            if (!dialogOpened)
            {
                LogWheelFailure(host, settingsReady, false,
                    GuideXosControlHostResult.Rejected, 0,
                    GuideXosControlHostResult.Rejected, 0);
                return false;
            }
            GuideXosControlHostResult wheel = fixture.Dialog.HandleInput(
                GuideXosInputEvent.ForWheel(116, 98, 1));
            bool modalMovedFive = wheel == GuideXosControlHostResult.Scrolled &&
                fixture.List.FirstVisibleIndex == 5 &&
                fixture.BackgroundList.FirstVisibleIndex == 0;
            // A background target cannot receive a second delivery while the
            // parent host is delegated to its modal child.
            GuideXosControlHostResult background = fixture.Parent.HandleWheel(
                2, 116, 98, 1, 0, 0, 8, 18);
            // ID 2 aliases the disabled Open button inside the modal child;
            // the background ListBox must remain untouched while that child
            // consumes the routed request.
            bool backgroundBlocked = background == GuideXosControlHostResult.Disabled &&
                fixture.BackgroundList.FirstVisibleIndex == 0;
            fixture.Dialog.Close(GuideXosDialogResult.Cancel);
            if (!modalMovedFive || !backgroundBlocked)
            {
                LogWheelFailure(host, settingsReady, dialogOpened, wheel,
                    fixture.List.FirstVisibleIndex, background,
                    fixture.BackgroundList.FirstVisibleIndex);
            }
            return modalMovedFive && backgroundBlocked;
        }
        finally
        {
            if (fixture.Dialog.IsOpen)
                fixture.Dialog.Close(GuideXosDialogResult.Cancel);
            runtime.RestoreForTests(prior);
        }
    }

    private static bool PointerAndOutsideClickAreModal()
    {
        DialogFixture fixture = new();
        if (!fixture.Dialog.Open(fixture.Parent)) return false;
        GuideXosControlHostResult row = fixture.Dialog.HandleInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary, 116, 132));
        bool selected = row == GuideXosControlHostResult.Changed &&
            fixture.List.SelectedIndex == 2;
        GuideXosControlHostResult outside = fixture.Dialog.HandleInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary, 12, 12));
        bool blocked = outside == GuideXosControlHostResult.Ignored &&
            fixture.Parent.ActiveIndex == -1 && fixture.Dialog.IsOpen;
        fixture.Dialog.Close(GuideXosDialogResult.Cancel);
        return selected && blocked;
    }

    private static bool EscapeRestoresParentFocus()
    {
        DialogFixture fixture = new();
        if (!fixture.Dialog.Open(fixture.Parent)) return false;
        GuideXosControlHostResult escaped = fixture.Dialog.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape));
        return escaped == GuideXosControlHostResult.Cancelled &&
            !fixture.Dialog.IsOpen && !fixture.Parent.IsModalActive &&
            fixture.Parent.ActiveControlId == 1 && fixture.ParentButton.IsFocused &&
            fixture.Dialog.RegistrationCount == 3;
    }

    private static bool Case(GuideXosHost host, ref int cases, bool passed,
        ReadOnlySpan<byte> name)
    {
        cases++;
        if (!passed)
        {
            Span<byte> line = stackalloc byte[72];
            int position = 0;
            if (GuideXosText.Append(line, ref position,
                    "C151-TEST-FAIL case="u8) &&
                GuideXosText.Append(line, ref position, name))
            {
                host?.TryLog(line[..position]);
            }
        }
        return passed;
    }

    private static void LogWheelFailure(GuideXosHost host,
        bool settingsReady, bool dialogOpened,
        GuideXosControlHostResult wheel, int modalOffset,
        GuideXosControlHostResult background, int backgroundOffset)
    {
        Span<byte> line = stackalloc byte[160];
        int position = 0;
        bool written = GuideXosText.Append(line, ref position,
            "C151-TEST-DETAIL settings="u8) &&
            GuideXosText.Append(line, ref position,
                settingsReady ? "ready"u8 : "failed"u8) &&
            GuideXosText.Append(line, ref position, " open="u8) &&
            GuideXosText.Append(line, ref position,
                dialogOpened ? "true"u8 : "false"u8) &&
            GuideXosText.Append(line, ref position, " wheel="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, (uint)wheel) &&
            GuideXosText.Append(line, ref position, " modal-offset="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, (uint)modalOffset) &&
            GuideXosText.Append(line, ref position, " background="u8) &&
            GuideXosText.AppendUnsigned(line, ref position, (uint)background) &&
            GuideXosText.Append(line, ref position, " background-offset="u8) &&
            GuideXosText.AppendUnsigned(line, ref position,
                (uint)backgroundOffset);
        if (written) host?.TryLog(line[..position]);
    }

    private sealed class DialogFixture
    {
        public readonly GuideXosControlHost Parent = new(2);
        public readonly GuideXosButton ParentButton =
            new(8, 8, 80, 20, "Parent");
        public readonly GuideXosListBox BackgroundList =
            new(16, 24, 4, 16, 0, 0);
        public readonly GuideXosDialog Dialog =
            new(100, 60, 320, 180, "List Test", 3);
        public readonly GuideXosListBox List =
            new(16, 20, 4, 16, 112, 92, 18);
        public readonly GuideXosButton OpenButton =
            new(112, 200, 84, 24, "Open");
        public readonly GuideXosButton CancelButton =
            new(208, 200, 84, 24, "Cancel");

        public DialogFixture()
        {
            Parent.TryRegisterButton(1, ParentButton);
            Parent.TryRegisterListBox(2, BackgroundList);
            Parent.TryFocus(1);
            for (int index = 0; index < 12; index++)
            {
                List.TryAdd("Row" + index.ToString("D2"));
                BackgroundList.TryAdd("Bg" + index.ToString("D2"));
            }
            Dialog.TryAddMember(List);
            Dialog.TryAddMember(OpenButton);
            Dialog.TryAddMember(CancelButton);
            Dialog.TrySetDefaultButton(OpenButton);
            OpenButton.SetEnabled(false);
        }
    }
}
