namespace HostLogProof;

/// <summary>Focused Dialog, MessageBox, and modal-host regression cases for C145.</summary>
public static class GuideXosDialogC145Tests
{
    private sealed class Fixture
    {
        public readonly GuideXosControlHost Parent;
        public readonly GuideXosButton ParentA;
        public readonly GuideXosCheckBox ParentB;
        public readonly GuideXosDialog Dialog;
        public readonly GuideXosButton Cancel;
        public readonly GuideXosButton Accept;
        public readonly GuideXosButton Third;
        public readonly GuideXosLabel MemberLabel;
        public readonly GuideXosCheckBox MemberCheck;
        public readonly GuideXosComboBox MemberCombo;
        public readonly GuideXosSeparator MemberSeparator;
        public readonly GuideXosLabel ExtraLabel;
        public readonly GuideXosScrollView ParentView;
        public readonly GuideXosScrollBar ParentBar;
        public int CallbackCount;
        public GuideXosDialogResult CallbackResult;

        public Fixture()
            : this(new GuideXosControlHost(4),
                new GuideXosButton(16, 20, 96, 18, "Parent A"),
                new GuideXosCheckBox(16, 48, 128, 18, "Parent B"), true)
        {
        }

        public Fixture(GuideXosControlHost parent,
            GuideXosButton parentA, GuideXosCheckBox parentB,
            bool registerParent)
        {
            Parent = parent;
            ParentA = parentA;
            ParentB = parentB;
            if (registerParent)
            {
                Parent.TryRegisterButton(1, ParentA);
                if (ParentB != null) Parent.TryRegisterCheckBox(2, ParentB);
                Parent.TryFocus(1);
            }
            Dialog = new GuideXosDialog(100, 88, 360, 160, "Confirm");
            Cancel = new GuideXosButton(132, 218, 88, 18, "Cancel");
            Accept = new GuideXosButton(244, 218, 88, 18, "Yes");
            Third = new GuideXosButton(344, 218, 80, 18, "No");
            MemberLabel = new GuideXosLabel(116, 132, 192, "Prompt");
            MemberCheck = new GuideXosCheckBox(116, 154, 128, 18, "Remember");
            MemberCombo = new GuideXosComboBox(116, 178, 128, 18, 4, 16, 2);
            MemberCombo.TryAddItem("First");
            MemberCombo.TryAddItem("Second");
            MemberSeparator = new GuideXosSeparator(116, 202, 192);
            ExtraLabel = new GuideXosLabel(116, 104, 168, "Bounded");
            ParentView = new GuideXosScrollView(16, 60, 120, 72);
            ParentBar = new GuideXosScrollBar(20, 20, 16, 128)
            {
                Maximum = 100,
                PageSize = 20,
            };
            Dialog.TryAddMember(Cancel);
            Dialog.TryAddMember(Accept);
            Dialog.TrySetButtonResult(Cancel, GuideXosDialogResult.Cancel);
            Dialog.TrySetButtonResult(Accept, GuideXosDialogResult.Yes);
            Dialog.TrySetDefaultButton(Accept);
            Dialog.TrySetCancelResult(GuideXosDialogResult.Cancel);
            Dialog.TrySetMessage("Discard the working settings?");
            Dialog.Closed = result =>
            {
                CallbackCount++;
                CallbackResult = result;
            };
        }
    }

    public static bool Run(GuideXosHost host, GuideXosDialog nestedCandidate)
    {
        int coreCases = 0;
        int memberCases = 0;
        int messageBoxCases = 0;
        int routingCases = 0;
        Fixture fixture = new();

        bool core = Core(host, fixture, nestedCandidate, ref coreCases);
        bool members = Membership(host, fixture, ref memberCases);
        bool messageBox = MessageBox(host, fixture, ref messageBoxCases);
        bool routing = ModalRouting(host, fixture, ref routingCases);

        host?.TryLog(core && coreCases == 15
            ? "C145-DIALOG-CORE cases=15 result=PASS"u8
            : "C145-DIALOG-CORE result=FAIL"u8);
        host?.TryLog(members && memberCases == 10
            ? "C145-DIALOG-MEMBERS cases=10 result=PASS"u8
            : "C145-DIALOG-MEMBERS result=FAIL"u8);
        host?.TryLog(messageBox && messageBoxCases == 11
            ? "C145-MESSAGEBOX cases=11 result=PASS"u8
            : "C145-MESSAGEBOX result=FAIL"u8);
        host?.TryLog(routing && routingCases == 13
            ? "C145-MODAL-ROUTING cases=13 result=PASS"u8
            : "C145-MODAL-ROUTING result=FAIL"u8);
        bool result = core && members && messageBox && routing &&
            coreCases == 15 && memberCases == 10 &&
            messageBoxCases == 11 && routingCases == 13;
        host?.TryLog(result
            ? "C145-DIALOG-TESTS core=15 members=10 messagebox=11 routing=13 total=49 result=PASS"u8
            : "C145-DIALOG-TESTS result=FAIL"u8);
        return result;
    }

    private static bool Core(GuideXosHost host, Fixture fixture,
        GuideXosDialog nestedCandidate, ref int count)
    {
        bool result = true;
        result &= Case(ref count, !fixture.Dialog.IsOpen &&
            fixture.Dialog.Result == GuideXosDialogResult.None &&
            fixture.Dialog.MaximumMemberCount == 8);
        result &= Case(ref count, fixture.Parent.TryFocus(1) ==
            GuideXosControlHostResult.Focused);
        result &= Case(ref count, fixture.Dialog.Open(fixture.Parent) &&
            fixture.Dialog.IsOpen && fixture.Dialog.IsModal &&
            fixture.Parent.IsModalActive && fixture.Parent.ActiveIndex == -1);
        result &= Case(ref count, !fixture.Dialog.Open(fixture.Parent));

        GuideXosDialog other = nestedCandidate;
        result &= Case(ref count, other != null && !other.Open(fixture.Parent));
        result &= Case(ref count, !fixture.ParentA.IsFocused &&
            fixture.Dialog.ControlHost.ActiveControlId == 2);
        result &= Case(ref count, fixture.Dialog.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab)) ==
                GuideXosControlHostResult.Traversed &&
            fixture.Dialog.ControlHost.ActiveControlId == 1);
        result &= Case(ref count, fixture.Dialog.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Tab, true)) ==
                GuideXosControlHostResult.Traversed &&
            fixture.Dialog.ControlHost.ActiveControlId == 2);
        result &= Case(ref count, fixture.Dialog.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Enter)) ==
                GuideXosControlHostResult.Activated && !fixture.Dialog.IsOpen &&
            fixture.Dialog.Result == GuideXosDialogResult.Yes);
        result &= Case(ref count, fixture.CallbackCount == 1 &&
            fixture.CallbackResult == GuideXosDialogResult.Yes);
        result &= Case(ref count, !fixture.Parent.IsModalActive &&
            fixture.Parent.ActiveControlId == 1 && fixture.ParentA.IsFocused);

        result &= Case(ref count, fixture.Dialog.Open(fixture.Parent) &&
            fixture.Dialog.HandleInput(
                GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape)) ==
                GuideXosControlHostResult.Cancelled && !fixture.Dialog.IsOpen &&
            fixture.Dialog.Result == GuideXosDialogResult.Cancel);
        result &= Case(ref count, fixture.CallbackCount == 2 &&
            fixture.Parent.ActiveControlId == 1 && fixture.ParentA.IsFocused);

        result &= Case(ref count, fixture.Dialog.Open(fixture.Parent) &&
            fixture.Dialog.HandleInput(GuideXosInputEvent.ForPointer(
                GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary,
                30, 28)) == GuideXosControlHostResult.Ignored &&
            fixture.Dialog.IsOpen && !fixture.ParentA.IsFocused);
        result &= Case(ref count, fixture.Dialog.HandleInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary, fixture.Accept.X + 4,
                fixture.Accept.Y + 4)) == GuideXosControlHostResult.Activated &&
            !fixture.Dialog.IsOpen && fixture.Dialog.Result == GuideXosDialogResult.Yes);
        return result && count == 15;
    }

    private static bool Membership(GuideXosHost host, Fixture fixture,
        ref int count)
    {
        bool result = true;
        GuideXosDialog dialog = fixture.Dialog;
        dialog.TryClearMembers();
        dialog.TrySetTitle("Members");
        GuideXosLabel label = fixture.MemberLabel;
        GuideXosCheckBox check = fixture.MemberCheck;
        GuideXosComboBox combo = fixture.MemberCombo;
        GuideXosSeparator separator = fixture.MemberSeparator;
        GuideXosButton button = fixture.Accept;
        result &= Case(ref count, dialog.TryAddMember(label) &&
            dialog.TryAddMember(button) && dialog.TryAddMember(check) &&
            dialog.TryAddMember(combo) && dialog.TryAddMember(separator) &&
            dialog.MemberCount == 5 && dialog.RegistrationCount == 3);
        result &= Case(ref count, !dialog.TryAddMember(button) &&
            !dialog.TryAddMember(new GuideXosPanel(0, 0, 240, 120)) &&
            !dialog.TryAddMember(new GuideXosGroupBox(0, 0, 240, 120, "No")) &&
            !dialog.TryAddMember(fixture.ParentView) &&
            !dialog.TryAddMember(dialog));
        result &= Case(ref count, !dialog.TryAddMember(new GuideXosRadioButton(
            116, 178, 128, 18, "Radio")));
        result &= Case(ref count, !dialog.IsOpen && dialog.TryRemoveMember(label) &&
            dialog.MemberCount == 4 && dialog.RegistrationCount == 3);
        result &= Case(ref count, button.Enabled && button.Visible &&
            check.Enabled && check.Visible && combo.Enabled && combo.Visible);
        result &= Case(ref count, dialog.TryRemoveMember(check) &&
            dialog.MemberCount == 3 && dialog.RegistrationCount == 2 &&
            check.Enabled && check.Visible);

        dialog.TryClearMembers();
        GuideXosLabel extra1 = fixture.ExtraLabel;
        bool added = dialog.TryAddMember(label) && dialog.TryAddMember(button) &&
            dialog.TryAddMember(check) && dialog.TryAddMember(combo) &&
            dialog.TryAddMember(separator) && dialog.TryAddMember(fixture.Cancel) &&
            dialog.TryAddMember(fixture.Third) && dialog.TryAddMember(extra1);
        result &= Case(ref count, added && dialog.MemberCount == 8 &&
            dialog.MaximumMemberCount == 8);
        result &= Case(ref count, !dialog.TryAddMember(label));
        result &= Case(ref count, !dialog.TryRemoveMember(null));
        result &= Case(ref count, dialog.TryClearMembers() &&
            dialog.MemberCount == 0 && dialog.RegistrationCount == 0);
        return result && count == 10;
    }

    private static bool MessageBox(GuideXosHost host, Fixture fixture,
        ref int count)
    {
        bool result = true;
        result &= MessageBoxConfiguration(ref count, fixture,
            GuideXosMessageBoxButtons.OK, GuideXosDialogResult.OK,
            GuideXosDialogResult.OK);
        result &= MessageBoxConfiguration(ref count, fixture,
            GuideXosMessageBoxButtons.OKCancel, GuideXosDialogResult.OK,
            GuideXosDialogResult.Cancel);
        result &= MessageBoxConfiguration(ref count, fixture,
            GuideXosMessageBoxButtons.YesNo, GuideXosDialogResult.Yes,
            GuideXosDialogResult.No);
        result &= MessageBoxConfiguration(ref count, fixture,
            GuideXosMessageBoxButtons.YesNoCancel, GuideXosDialogResult.No,
            GuideXosDialogResult.Cancel);

        GuideXosDialog dialog = fixture.Dialog;
        GuideXosButton first = fixture.Accept;
        GuideXosButton second = fixture.Cancel;
        GuideXosButton third = fixture.Third;
        result &= Case(ref count, !GuideXosMessageBox.TryConfigure(dialog,
            GuideXosMessageBoxButtons.OK, "TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT", "Valid", first,
            null, null, GuideXosDialogResult.OK));
        result &= Case(ref count, !GuideXosMessageBox.TryConfigure(dialog,
            GuideXosMessageBoxButtons.OK, "Title", "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", first,
            null, null, GuideXosDialogResult.OK));
        result &= Case(ref count, !GuideXosMessageBox.TryConfigure(dialog,
            (GuideXosMessageBoxButtons)99, "Title", "Text", first, second, third,
            GuideXosDialogResult.OK));
        result &= Case(ref count, !GuideXosMessageBox.TryConfigure(dialog,
            GuideXosMessageBoxButtons.OKCancel, "Title", "Text", first, second,
            null, GuideXosDialogResult.Yes));
        result &= Case(ref count, !GuideXosMessageBox.TryConfigure(dialog,
            GuideXosMessageBoxButtons.OKCancel, "Title", "Text", first, second,
            null, GuideXosDialogResult.OK, GuideXosDialogResult.Yes));

        bool repeated = GuideXosMessageBox.TryConfigure(dialog,
            GuideXosMessageBoxButtons.OKCancel, "Repeat", "Bounded message",
            first, second, null, GuideXosDialogResult.OK) &&
            dialog.RegistrationCount == 2;
        GuideXosControlHost parent = fixture.Parent;
        parent.TryFocus(1);
        for (int cycle = 0; cycle < 25; cycle++)
        {
            GuideXosDialogResult close = (cycle & 1) == 0
                ? GuideXosDialogResult.OK : GuideXosDialogResult.Cancel;
            repeated &= dialog.Open(parent) && dialog.Close(close) &&
                dialog.Result == close && dialog.RegistrationCount == 2 &&
                !parent.IsModalActive && dialog.ControlHost.ActiveControlId == 0 &&
                parent.ActiveControlId == 1;
        }
        result &= Case(ref count, repeated);
        result &= Case(ref count, MessageBoxDefaultEnter(fixture));
        return result && count == 11;
    }

    private static bool MessageBoxDefaultEnter(Fixture fixture)
    {
        GuideXosDialog dialog = fixture.Dialog;
        GuideXosButton ok = fixture.Accept;
        if (!GuideXosMessageBox.TryConfigure(dialog,
                GuideXosMessageBoxButtons.OK, "Information", "Applied",
                ok, null, null, GuideXosDialogResult.OK)) return false;
        GuideXosControlHost parent = fixture.Parent;
        parent.TryFocus(1);
        return dialog.Open(parent) &&
            dialog.HandleInput(GuideXosInputEvent.ForKeyDown(
                GuideXosTextInputKey.Enter)) == GuideXosControlHostResult.Activated &&
            dialog.Result == GuideXosDialogResult.OK && !parent.IsModalActive;
    }

    private static bool MessageBoxConfiguration(ref int count, Fixture fixture,
        GuideXosMessageBoxButtons set, GuideXosDialogResult defaultResult,
        GuideXosDialogResult escapeResult)
    {
        GuideXosDialog dialog = fixture.Dialog;
        GuideXosButton first = fixture.Accept;
        GuideXosButton secondArg = set == GuideXosMessageBoxButtons.OK
            ? null : set == GuideXosMessageBoxButtons.OKCancel
                ? fixture.Cancel : fixture.Third;
        GuideXosButton thirdArg = set == GuideXosMessageBoxButtons.YesNoCancel
            ? fixture.Cancel : null;
        bool configured = GuideXosMessageBox.TryConfigure(dialog, set,
            "Confirm", "A bounded message", first, secondArg, thirdArg,
            defaultResult) && dialog.MaximumMemberCount == 8 &&
            dialog.RegistrationCount == (set == GuideXosMessageBoxButtons.OK ? 1 :
                set == GuideXosMessageBoxButtons.YesNoCancel ? 3 : 2) &&
            dialog.DefaultButtonId > 0 && dialog.CancelResult == escapeResult;

        GuideXosControlHost parent = fixture.Parent;
        parent.TryFocus(1);
        bool opened = configured && dialog.Open(parent);
        bool escaped = opened && dialog.HandleInput(
            GuideXosInputEvent.ForKeyDown(GuideXosTextInputKey.Escape)) ==
                GuideXosControlHostResult.Cancelled &&
            dialog.Result == escapeResult && parent.ActiveControlId == 1;
        if (!opened) dialog.Close(GuideXosDialogResult.None);
        bool check = configured && escaped;
        return Case(ref count, check);
    }

    private static bool ModalRouting(GuideXosHost host, Fixture fixture,
        ref int count)
    {
        bool result = true;
        GuideXosDialog dialog = fixture.Dialog;
        GuideXosControlHost parent = fixture.Parent;
        GuideXosButton parentButton = fixture.ParentA;
        GuideXosScrollView view = fixture.ParentView;
        GuideXosComboBox combo = fixture.MemberCombo;
        dialog.TryClearMembers();
        dialog.TrySetTitle("Modal");
        dialog.TryAddMember(fixture.Cancel);
        dialog.TryAddMember(fixture.Accept);
        dialog.TrySetButtonResult(fixture.Cancel, GuideXosDialogResult.Cancel);
        dialog.TrySetButtonResult(fixture.Accept, GuideXosDialogResult.Yes);
        dialog.TrySetDefaultButton(fixture.Accept);
        dialog.TrySetCancelResult(GuideXosDialogResult.Cancel);
        view.TryAddMember(fixture.MemberLabel, 0, 0);
        view.TryAddMember(fixture.ExtraLabel, 0, 24);
        combo.TrySetBounds(16, 48, 128, 18);
        parent.TryRegisterScrollView(3, view);
        parent.TryRegisterComboBox(4, combo);
        parent.TryFocus(1);

        result &= Case(ref count, dialog.Open(parent) &&
            parent.ActiveIndex == -1 && !parentButton.IsFocused);
        int offset = view.Offset;
        result &= Case(ref count, dialog.HandleInput(
            GuideXosInputEvent.ForWheel(28, 82, 1)) ==
                GuideXosControlHostResult.Ignored && view.Offset == offset);
        result &= Case(ref count, dialog.HandleInput(
            GuideXosInputEvent.ForPointer(GuideXosInputKind.PointerDown,
                GuideXosPointerButton.Primary, 24, 26)) ==
                GuideXosControlHostResult.Ignored && !parentButton.IsFocused);
        result &= Case(ref count, parentButton.Visible && parentButton.Enabled);
        result &= Case(ref count, dialog.Close(GuideXosDialogResult.None) &&
            parent.ActiveControlId == 1 && parentButton.IsFocused);

        parent.TryFocus(4);
        combo.Open();
        bool acquired = parent.TryAcquireTransientInputCapture(4);
        bool popupOpened = dialog.Open(parent);
        result &= Case(ref count, acquired && popupOpened && !combo.IsOpen &&
            !parent.HasTransientInputCapture && !dialog.HasTransientInputCapture);
        result &= Case(ref count, dialog.Close(GuideXosDialogResult.None) &&
            !parent.IsModalActive && combo.Open() == GuideXosComboBoxResult.Opened &&
            parent.TryAcquireTransientInputCapture(4));
        combo.Close();
        parent.RefreshVisibility();

        parent.Reset();
        parent.TryRegisterScrollBar(1, fixture.ParentBar, false);
        bool dragging = parent.FocusAndRoutePointer(1, fixture.ParentBar.X + 2,
            fixture.ParentBar.ThumbTop + 2) == GuideXosControlHostResult.DragStarted &&
            parent.HasPointerDragCapture;
        bool openedDuringDrag = dialog.Open(parent);
        result &= Case(ref count, dragging && openedDuringDrag &&
            !parent.HasPointerDragCapture && !fixture.ParentBar.IsDragging);
        result &= Case(ref count, dialog.Close(GuideXosDialogResult.None) &&
            !parent.IsModalActive && !parent.HasPointerDragCapture);

        parent.Reset();
        parent.TryRegisterButton(1, parentButton);
        parent.TryRegisterCheckBox(2, fixture.ParentB);
        parent.TryFocus(1);
        result &= Case(ref count, dialog.Open(parent));
        parentButton.SetEnabled(false);
        result &= Case(ref count, dialog.Close(GuideXosDialogResult.Cancel) &&
            parent.ActiveControlId == 2 && fixture.ParentB.IsFocused);
        parentButton.SetEnabled(true);

        parent.Reset();
        parent.TryRegisterButton(1, parentButton);
        parent.TryRegisterCheckBox(2, fixture.ParentB);
        parent.TryFocus(1);
        parent.HandleKey((GuideXosTextInputKey)' ');
        bool staleOpen = dialog.Open(parent);
        GuideXosControlHostResult staleCharacter = parent.HandleCharacter(' ');
        result &= Case(ref count, staleOpen && staleCharacter ==
                GuideXosControlHostResult.Ignored && dialog.IsOpen &&
            dialog.ControlHost.ActiveControlId == 2);
        result &= Case(ref count, dialog.Close(GuideXosDialogResult.Cancel) &&
            !parent.IsModalActive && !dialog.HasTransientInputCapture &&
            !dialog.HasPointerDragCapture);
        return result && count == 13;
    }

    private static bool Case(ref int count, bool value)
    {
        count++;
        return value;
    }
}
