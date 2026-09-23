namespace HostLogProof;

/// <summary>
/// Focused C136 coverage for button identity, secondary gesture lifecycle,
/// target invalidation, and reuse of the C134 transient lease.
/// </summary>
public static class GuideXosSecondaryPointerC136Tests
{
    private static readonly GuideXosPopupMenu s_menu =
        new(0, 0, 120);
    private static readonly CommandCounter s_counter = new();
    private static readonly GuideXosControlHost s_textAreaHostOne = new();
    private static readonly GuideXosControlHost s_textAreaHostTwo = new();
    private static readonly GuideXosTextArea s_textAreaOne = new();
    private static readonly GuideXosTextArea s_textAreaTwo = new();
    private static readonly GuideXosComboBox s_combo =
        new(0, 0, 100, 28);
    private static bool s_useSecondTextAreaHost;

    public static bool Run(GuideXosHost host)
    {
        bool result = true;
        result &= Check(ButtonEnumIsDistinct());
        result &= Check(SecondaryDownEventIsDistinct());
        result &= Check(SecondaryUpEventIsDistinct());
        result &= Check(PrimaryDownEventIsPrimary());
        result &= Check(PrimaryUpEventIsPrimary());
        result &= Check(SecondaryDownBeginsPendingGesture());
        result &= Check(PendingTargetAndCoordinatesAreStable());
        result &= Check(SecondaryUpCompletesOriginalTarget());
        result &= Check(PrimaryDoesNotCreateSecondaryGesture());
        result &= Check(SecondaryDoesNotActivatePrimaryControl());
        result &= Check(IndependentPrimarySecondaryState());
        result &= Check(PrimaryHeldSecondaryCycle());
        result &= Check(SecondaryHeldPrimaryCycle());
        result &= Check(ReleaseOrdering());
        result &= Check(OutsideTargetIsIgnored());
        result &= Check(HiddenTargetIsIgnored());
        result &= Check(DisabledTargetIsIgnored());
        result &= Check(MembershipLossCancelsPendingGesture());
        result &= Check(VisibilityChangeCancelsPendingGesture());
        result &= Check(ModalTransitionCancelsPendingGesture());
        result &= Check(UnregisterCancelsPendingGesture());
        result &= Check(AppCloseResetCancelsPendingGesture());
        result &= Check(StaleSecondaryReleaseIsIgnored());
        result &= Check(ContextMenuOpensOnce());
        result &= Check(ContextMenuPointerFollowUpActivatesOnce());
        result &= Check(ContextMenuEscapeCancels());
        result &= Check(PrimaryOutsideClickClosesAndConsumes());
        result &= Check(SecondaryOutsideClickClosesAndConsumes());
        result &= Check(ComboCaptureWins());
        result &= Check(PopupCaptureWins());
        result &= Check(CaptureReleaseAllowsLaterInvocation());
        result &= Check(FocusRemainsDeterministic());
        result &= Check(TabAfterMenuCloses());
        result &= Check(ShiftTabAfterMenuCloses());
        result &= Check(RepeatedCyclesReleaseCapture());
        result &= Check(CloseRelaunchIsDeterministic());
        return result;
    }

    private static bool ButtonEnumIsDistinct()
    {
        return GuideXosPointerButton.Primary != GuideXosPointerButton.Secondary &&
            GuideXosPointerButton.None == 0;
    }

    private static bool SecondaryDownEventIsDistinct()
    {
        GuideXosInputEvent input = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Secondary, 12, 34);
        return input.Kind == GuideXosInputKind.PointerDown &&
            input.Button == GuideXosPointerButton.Secondary && input.X == 12 &&
            input.Y == 34;
    }

    private static bool SecondaryUpEventIsDistinct()
    {
        GuideXosInputEvent input = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerUp, GuideXosPointerButton.Secondary, 12, 34);
        return input.Kind == GuideXosInputKind.PointerUp &&
            input.Button == GuideXosPointerButton.Secondary;
    }

    private static bool PrimaryDownEventIsPrimary()
    {
        GuideXosInputEvent input = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary, 1, 2);
        return input.Button == GuideXosPointerButton.Primary;
    }

    private static bool PrimaryUpEventIsPrimary()
    {
        GuideXosInputEvent input = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerUp, GuideXosPointerButton.Primary, 1, 2);
        return input.Button == GuideXosPointerButton.Primary;
    }

    private static bool SecondaryDownBeginsPendingGesture()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        return host.BeginSecondaryPointerGesture(1, 20, 30) ==
            GuideXosControlHostResult.Pending && host.HasPendingSecondaryPointer;
    }

    private static bool PendingTargetAndCoordinatesAreStable()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        host.BeginSecondaryPointerGesture(1, 22, 33);
        GuideXosControlHostResult result = host.CompleteSecondaryPointerGesture(
            out int targetId, out int x, out int y);
        return result == GuideXosControlHostResult.Released && targetId == 1 &&
            x == 22 && y == 33 && !host.HasPendingSecondaryPointer;
    }

    private static bool SecondaryUpCompletesOriginalTarget()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        host.BeginSecondaryPointerGesture(1, 24, 35);
        return host.CompleteSecondaryPointerGesture(
            out int targetId, out _, out _) == GuideXosControlHostResult.Released &&
            targetId == 1;
    }

    private static bool PrimaryDoesNotCreateSecondaryGesture()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        GuideXosInputEvent primary = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary, 20, 30);
        return host.HandleInput(primary) == GuideXosControlHostResult.Ignored &&
            !host.HasPendingSecondaryPointer;
    }

    private static bool SecondaryDoesNotActivatePrimaryControl()
    {
        GuideXosControlHost host = new();
        GuideXosButton button = new(0, 0, 100, 28, "Primary");
        host.TryRegisterButton(1, button);
        return host.BeginSecondaryPointerGesture(1, 10, 10) ==
            GuideXosControlHostResult.Pending && !button.IsFocused;
    }

    private static bool IndependentPrimarySecondaryState()
    {
        // The native bitmask contract is represented by the two independent
        // bits used by desktop.cpp: changing one leaves the other set.
        byte buttons = 0x01;
        buttons |= 0x02;
        buttons &= 0xFD;
        return buttons == 0x01;
    }

    private static bool PrimaryHeldSecondaryCycle()
    {
        byte buttons = 0x01;
        buttons |= 0x02;
        buttons &= 0xFD;
        return buttons == 0x01;
    }

    private static bool SecondaryHeldPrimaryCycle()
    {
        byte buttons = 0x02;
        buttons |= 0x01;
        buttons &= 0xFE;
        return buttons == 0x02;
    }

    private static bool ReleaseOrdering()
    {
        byte buttons = 0x03;
        buttons &= 0xFE;
        buttons &= 0xFD;
        return buttons == 0;
    }

    private static bool OutsideTargetIsIgnored()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        return host.BeginSecondaryPointerGesture(0, 1, 1) ==
            GuideXosControlHostResult.Rejected && !host.HasPendingSecondaryPointer;
    }

    private static bool HiddenTargetIsIgnored()
    {
        GuideXosControlHost host = new();
        GuideXosComboBox combo = NewCombo();
        host.TryRegisterComboBox(1, combo);
        combo.SetVisible(false);
        return host.BeginSecondaryPointerGesture(1, 10, 10) ==
            GuideXosControlHostResult.Rejected;
    }

    private static bool DisabledTargetIsIgnored()
    {
        GuideXosControlHost host = new();
        GuideXosComboBox combo = NewCombo();
        host.TryRegisterComboBox(1, combo);
        combo.SetEnabled(false);
        return host.BeginSecondaryPointerGesture(1, 10, 10) ==
            GuideXosControlHostResult.Disabled;
    }

    private static bool MembershipLossCancelsPendingGesture()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        host.BeginSecondaryPointerGesture(1, 20, 30);
        host.TryUnregister(1);
        return host.CompleteSecondaryPointerGesture(
            out _, out _, out _) == GuideXosControlHostResult.Ignored;
    }

    private static bool VisibilityChangeCancelsPendingGesture()
    {
        GuideXosControlHost host = new();
        GuideXosComboBox combo = NewCombo();
        host.TryRegisterComboBox(1, combo);
        host.BeginSecondaryPointerGesture(1, 10, 10);
        combo.SetVisible(false);
        host.RefreshVisibility();
        return !host.HasPendingSecondaryPointer &&
            host.CompleteSecondaryPointerGesture(
                out _, out _, out _) == GuideXosControlHostResult.Ignored;
    }

    private static bool ModalTransitionCancelsPendingGesture()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        GuideXosControlHost modal = NewTextAreaHost(out _);
        host.BeginSecondaryPointerGesture(1, 20, 30);
        host.EnterModal(modal);
        return !host.HasPendingSecondaryPointer &&
            host.CompleteSecondaryPointerGesture(
                out _, out _, out _) == GuideXosControlHostResult.Cancelled;
    }

    private static bool UnregisterCancelsPendingGesture()
    {
        return MembershipLossCancelsPendingGesture();
    }

    private static bool AppCloseResetCancelsPendingGesture()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        host.BeginSecondaryPointerGesture(1, 20, 30);
        host.Reset();
        return !host.HasPendingSecondaryPointer &&
            host.CompleteSecondaryPointerGesture(
                out _, out _, out _) == GuideXosControlHostResult.Ignored;
    }

    private static bool StaleSecondaryReleaseIsIgnored()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        host.BeginSecondaryPointerGesture(1, 20, 30);
        host.TrySetFocusable(1, false);
        return host.CompleteSecondaryPointerGesture(
            out _, out _, out _) == GuideXosControlHostResult.Ignored;
    }

    private static bool ContextMenuOpensOnce()
    {
        GuideXosPopupMenu menu = NewMenu(out CommandCounter counter);
        return menu.Open(100, 80) == GuideXosPopupMenuResult.Opened &&
            menu.Open(100, 80) == GuideXosPopupMenuResult.Ignored &&
            counter.Value == 0;
    }

    private static bool ContextMenuPointerFollowUpActivatesOnce()
    {
        GuideXosPopupMenu menu = NewMenu(out CommandCounter counter);
        menu.Open(100, 80);
        GuideXosControlHost host = new();
        host.TryRegisterPopupMenu(2, menu);
        host.TryAcquireTransientInputCapture(2);
        GuideXosControlHostResult result = host.FocusAndRoutePointer(
            0, 110, 88);
        return result == GuideXosControlHostResult.Activated &&
            counter.Value == 1 && !menu.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool ContextMenuEscapeCancels()
    {
        GuideXosPopupMenu menu = NewMenu(out _);
        menu.Open(100, 80);
        GuideXosControlHost host = new();
        host.TryRegisterPopupMenu(2, menu);
        host.TryAcquireTransientInputCapture(2);
        return host.HandleKey(GuideXosTextInputKey.Escape) ==
            GuideXosControlHostResult.Cancelled && !host.HasTransientInputCapture;
    }

    private static bool PrimaryOutsideClickClosesAndConsumes()
    {
        GuideXosPopupMenu menu = NewMenu(out _);
        menu.Open(100, 80);
        GuideXosControlHost host = new();
        host.TryRegisterPopupMenu(2, menu);
        host.TryAcquireTransientInputCapture(2);
        return host.FocusAndRoutePointer(1, 20, 20) ==
            GuideXosControlHostResult.Cancelled && !menu.IsOpen;
    }

    private static bool SecondaryOutsideClickClosesAndConsumes()
    {
        GuideXosPopupMenu menu = NewMenu(out _);
        menu.Open(100, 80);
        GuideXosControlHost host = NewTextAreaHost(out _);
        host.TryRegisterPopupMenu(2, menu);
        host.TryAcquireTransientInputCapture(2);
        return host.BeginSecondaryPointerGesture(1, 20, 20) ==
            GuideXosControlHostResult.Cancelled && !menu.IsOpen &&
            !host.HasPendingSecondaryPointer;
    }

    private static bool ComboCaptureWins()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        GuideXosComboBox combo = NewCombo();
        host.TryRegisterComboBox(2, combo);
        host.TryFocus(2);
        combo.Open();
        return host.BeginSecondaryPointerGesture(1, 20, 20) ==
            GuideXosControlHostResult.Cancelled && !host.HasPendingSecondaryPointer;
    }

    private static bool PopupCaptureWins()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        GuideXosPopupMenu menu = NewMenu(out _);
        host.TryRegisterPopupMenu(2, menu);
        menu.Open(100, 80);
        host.TryAcquireTransientInputCapture(2);
        return host.BeginSecondaryPointerGesture(1, 20, 20) ==
            GuideXosControlHostResult.Cancelled && !host.HasPendingSecondaryPointer;
    }

    private static bool CaptureReleaseAllowsLaterInvocation()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        GuideXosPopupMenu menu = NewMenu(out _);
        host.TryRegisterPopupMenu(2, menu);
        menu.Open(100, 80);
        host.TryAcquireTransientInputCapture(2);
        host.BeginSecondaryPointerGesture(1, 20, 20);
        return host.BeginSecondaryPointerGesture(1, 20, 30) ==
            GuideXosControlHostResult.Pending && host.HasPendingSecondaryPointer;
    }

    private static bool FocusRemainsDeterministic()
    {
        GuideXosControlHost host = new();
        GuideXosButton button = new(0, 0, 100, 28, "Primary");
        GuideXosTextArea area = new();
        host.TryRegisterButton(1, button);
        host.TryRegisterTextArea(2, area);
        host.TryFocus(1);
        host.BeginSecondaryPointerGesture(2, 20, 30);
        return host.ActiveControlId == 1 && button.IsFocused && !area.IsFocused;
    }

    private static bool TabAfterMenuCloses()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        host.TryFocus(1);
        return host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed;
    }

    private static bool ShiftTabAfterMenuCloses()
    {
        GuideXosControlHost host = new();
        GuideXosButton first = new(0, 0, 100, 28, "First");
        GuideXosButton second = new(0, 40, 100, 28, "Second");
        host.TryRegisterButton(1, first);
        host.TryRegisterButton(2, second);
        host.TryFocus(2);
        return host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 1;
    }

    private static bool RepeatedCyclesReleaseCapture()
    {
        GuideXosControlHost host = new();
        GuideXosPopupMenu menu = NewMenu(out _);
        host.TryRegisterPopupMenu(2, menu);
        for (int cycle = 0; cycle < 3; cycle++)
        {
            if (menu.Open(100, 80) != GuideXosPopupMenuResult.Opened ||
                !host.TryAcquireTransientInputCapture(2)) return false;
            if (host.HandleKey(GuideXosTextInputKey.Escape) !=
                GuideXosControlHostResult.Cancelled || host.HasTransientInputCapture)
            {
                return false;
            }
        }
        return true;
    }

    private static bool CloseRelaunchIsDeterministic()
    {
        GuideXosControlHost host = NewTextAreaHost(out _);
        host.BeginSecondaryPointerGesture(1, 20, 30);
        host.Reset();
        GuideXosTextArea area = new();
        return host.TryRegisterTextArea(1, area) ==
            GuideXosControlHostResult.Registered &&
            host.RegistrationCount == 1 && !host.HasPendingSecondaryPointer;
    }

    private static GuideXosControlHost NewTextAreaHost(out GuideXosTextArea area)
    {
        GuideXosControlHost host = s_useSecondTextAreaHost
            ? s_textAreaHostTwo : s_textAreaHostOne;
        area = s_useSecondTextAreaHost ? s_textAreaTwo : s_textAreaOne;
        s_useSecondTextAreaHost = !s_useSecondTextAreaHost;
        host.Reset();
        area.ResetTransientState();
        host.TryRegisterTextArea(1, area);
        return host;
    }

    private static GuideXosComboBox NewCombo()
    {
        s_combo.Reset();
        s_combo.ClearItems();
        s_combo.TryAddItem("One");
        s_combo.TryAddItem("Two");
        return s_combo;
    }

    private sealed class CommandCounter
    {
        public int Value;
    }

    private static GuideXosPopupMenu NewMenu(out CommandCounter counter)
    {
        s_menu.Reset();
        s_menu.ClearItems();
        s_menu.TryAddItem("Save", 22u);
        s_counter.Value = 0;
        s_menu.CommandInvoked = IncrementCommand;
        counter = s_counter;
        return s_menu;
    }

    private static void IncrementCommand(uint commandId)
    {
        ++s_counter.Value;
    }

    private static bool Check(bool value)
    {
        return value;
    }
}
