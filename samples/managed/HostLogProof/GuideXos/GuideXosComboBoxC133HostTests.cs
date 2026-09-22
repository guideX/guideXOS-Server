namespace HostLogProof;

/// <summary>Host/lifecycle C133 probes for transient ComboBox capture.</summary>
public static class GuideXosComboBoxC133HostTests
{
    private static int s_caseCount;
    private static readonly GuideXosComboBox s_fixtureCombo =
        new(10, 20, 160, 28, 4, 8, 4);
    private static readonly GuideXosControlHost s_fixtureHost =
        new(2);
    private static readonly GuideXosButton s_fixtureButton =
        new(220, 20, 80, 28, "Next");

    public static void Prepare()
    {
        _ = s_fixtureCombo;
        _ = s_fixtureHost;
        _ = s_fixtureButton;
    }

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool result = true;
        result &= Check(RegistrationAndTraversal());
        result &= Check(PointerOpenAndSelection());
        result &= Check(CurrentItemClosesWithoutCallback());
        result &= Check(OutsideClickSuppressesUnderlying());
        result &= Check(KeyboardCommitAndCancel());
        result &= Check(ClosedTabTraversal());
        result &= Check(OpenTabTraversal());
        result &= Check(OpenReverseTabTraversal());
        result &= Check(DisabledCannotOpen());
        result &= Check(HiddenCannotOpen());
        result &= Check(NonMemberCannotOpen());
        result &= Check(VisibilityInterruption());
        result &= Check(EnabledInterruption());
        result &= Check(FocusInterruption());
        result &= Check(MembershipInterruption());
        result &= Check(ModalInterruption());
        result &= Check(StaleSpaceCancelled());
        result &= Check(StaleCharacterCancelled());
        result &= Check(StalePointerReleaseCancelled());
        result &= Check(ProgrammaticSelectionWhileOpen());
        result &= Check(ClearItemsWhileOpen());
        result &= Check(PanelRestoreAndReopen());
        result &= Check(ResetRelaunch());
        result &= Check(RegistrationAccounting());

        bool count = s_caseCount == 24;
        host?.TryLog(result && count
            ? "C133-COMBO-HOST-TESTS cases=24 result=PASS"u8
            : "C133-COMBO-HOST-TESTS cases=24 result=FAIL"u8);
        return result && count;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static GuideXosComboBox NewCombo()
    {
        s_fixtureCombo.DetachFromPanel();
        s_fixtureCombo.TrySetBounds(10, 20, 160, 28);
        s_fixtureCombo.Reset();
        s_fixtureCombo.Changed = null;
        s_fixtureCombo.ClearItems();
        s_fixtureCombo.TryAddItem("A");
        s_fixtureCombo.TryAddItem("B");
        s_fixtureCombo.TryAddItem("C");
        return s_fixtureCombo;
    }

    private static GuideXosControlHost NewHost(
        GuideXosComboBox combo,
        GuideXosButton button = null)
    {
        GuideXosControlHost host = s_fixtureHost;
        host.Reset();
        host.TryRegisterComboBox(1, combo);
        if (button != null) host.TryRegisterButton(2, button);
        return host;
    }

    private static bool RegistrationAndTraversal()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosButton button = s_fixtureButton;
        button.Reset();
        GuideXosControlHost host = NewHost(combo, button);
        return host.RegistrationCount == 2 && host.ActiveIndex == -1 &&
            host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 1 &&
            host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 2;
    }

    private static bool PointerOpenAndSelection()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        int callbacks = 0;
        combo.Changed = _ => ++callbacks;
        GuideXosControlHostResult openResult = host.FocusAndRoutePointer(1, 10, 20);
        bool open = openResult ==
            GuideXosControlHostResult.Activated && combo.IsOpen;
        GuideXosControlHostResult selectResult = host.FocusAndRoutePointer(1, 10, 66);
        bool select = selectResult ==
            GuideXosControlHostResult.Changed && combo.SelectedIndex == 1 &&
            callbacks == 1 && !combo.IsOpen;
        return open && select;
    }

    private static bool CurrentItemClosesWithoutCallback()
    {
        GuideXosComboBox combo = NewCombo();
        combo.SelectedIndex = 1;
        GuideXosControlHost host = NewHost(combo);
        int callbacks = 0;
        combo.Changed = _ => ++callbacks;
        host.FocusAndRoutePointer(1, 10, 20);
        bool closed = host.FocusAndRoutePointer(1, 10, 66) ==
            GuideXosControlHostResult.Cancelled;
        return closed && combo.SelectedIndex == 1 && callbacks == 0 && !combo.IsOpen;
    }

    private static bool OutsideClickSuppressesUnderlying()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(combo, button);
        host.FocusAndRoutePointer(1, 10, 20);
        GuideXosControlHostResult outsideResult =
            host.FocusAndRoutePointer(2, 220, 20);
        bool consumed = outsideResult ==
            GuideXosControlHostResult.Cancelled;
        bool buttonNotActivated = host.ActiveControlId == 1 && !combo.IsOpen;
        GuideXosControlHostResult nextResult =
            host.FocusAndRoutePointer(2, 220, 20);
        bool nextClickWorks = nextResult ==
            GuideXosControlHostResult.Activated;
        return consumed && buttonNotActivated && nextClickWorks;
    }

    private static bool KeyboardCommitAndCancel()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        bool enter = host.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Activated && combo.IsOpen;
        bool move = host.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosControlHostResult.Moved && combo.ActiveIndex == 1;
        bool commit = host.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Changed && combo.SelectedIndex == 1;
        bool spaceOpen = host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored &&
            host.HandleCharacter(' ') == GuideXosControlHostResult.Activated &&
            combo.IsOpen;
        bool escape = host.HandleKey(GuideXosTextInputKey.Escape) ==
            GuideXosControlHostResult.Cancelled && !combo.IsOpen &&
            combo.SelectedIndex == 1;
        return enter && move && commit && spaceOpen && escape;
    }

    private static bool ClosedTabTraversal()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(combo, button);
        host.TryFocus(1);
        bool forward = host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 2;
        bool reverse = host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 1;
        return forward && reverse;
    }

    private static bool OpenTabTraversal()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(combo, button);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        return host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 2 &&
            !combo.IsOpen;
    }

    private static bool OpenReverseTabTraversal()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(combo, button);
        host.TryFocus(2);
        host.HandleKey(GuideXosTextInputKey.Tab, true);
        host.HandleKey(GuideXosTextInputKey.Enter);
        return host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 2 &&
            !combo.IsOpen;
    }

    private static bool DisabledCannotOpen()
    {
        GuideXosComboBox combo = NewCombo();
        combo.SetEnabled(false);
        GuideXosControlHost host = NewHost(combo);
        return host.FocusAndRoutePointer(1, 10, 20) ==
                GuideXosControlHostResult.Disabled && !combo.IsOpen &&
            host.TryFocus(1) == GuideXosControlHostResult.Disabled;
    }

    private static bool HiddenCannotOpen()
    {
        GuideXosComboBox combo = NewCombo();
        combo.SetVisible(false);
        GuideXosControlHost host = NewHost(combo);
        return host.FocusAndRoutePointer(1, 10, 20) ==
                GuideXosControlHostResult.Rejected && !combo.IsOpen;
    }

    private static bool NonMemberCannotOpen()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = new(1);
        return host.FocusAndRoutePointer(1, 10, 20) ==
            GuideXosControlHostResult.Rejected && !combo.IsOpen;
    }

    private static bool VisibilityInterruption()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        combo.SetVisible(false);
        host.RefreshVisibility();
        combo.SetVisible(true);
        host.RefreshVisibility();
        return !combo.IsOpen && host.ActiveIndex == -1;
    }

    private static bool EnabledInterruption()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        combo.SetEnabled(false);
        host.RefreshVisibility();
        combo.SetEnabled(true);
        host.RefreshVisibility();
        return !combo.IsOpen && host.ActiveIndex == -1;
    }

    private static bool FocusInterruption()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(combo, button);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        host.TryFocus(2);
        return !combo.IsOpen && host.ActiveControlId == 2;
    }

    private static bool MembershipInterruption()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosComboBox combo = NewCombo();
        panel.TryAddChild(combo, 8, 14);
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        panel.TryRemoveChild(combo);
        host.RefreshVisibility();
        return !combo.IsOpen && combo.ParentPanel == null;
    }

    private static bool ModalInterruption()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost main = NewHost(combo);
        GuideXosControlHost modal = new(1);
        modal.TryRegisterButton(9, new GuideXosButton(220, 20, 80, 28, "Modal"));
        main.TryFocus(1);
        main.HandleKey(GuideXosTextInputKey.Enter);
        bool entered = main.EnterModal(modal);
        bool closed = !combo.IsOpen && main.ActiveIndex == -1;
        bool exited = main.ExitModal() && main.ActiveControlId == 1;
        return entered && closed && exited;
    }

    private static bool StaleSpaceCancelled()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey((GuideXosTextInputKey)' ');
        combo.SetVisible(false);
        host.RefreshVisibility();
        combo.SetVisible(true);
        host.RefreshVisibility();
        bool stale = host.HandleCharacter(' ') == GuideXosControlHostResult.Ignored;
        return stale && !combo.IsOpen;
    }

    private static bool StaleCharacterCancelled()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        combo.SetVisible(false);
        host.RefreshVisibility();
        combo.SetVisible(true);
        host.RefreshVisibility();
        return host.HandleCharacter(' ') == GuideXosControlHostResult.Ignored &&
            !combo.IsOpen && combo.SelectedIndex == -1;
    }

    private static bool StalePointerReleaseCancelled()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        GuideXosControlHostResult openResult =
            host.FocusAndRoutePointer(1, 10, 20);
        GuideXosControlHostResult outsideResult =
            host.FocusAndRoutePointer(1, 400, 400);
        bool outside = outsideResult ==
            GuideXosControlHostResult.Cancelled;
        return outside && !combo.IsOpen && combo.SelectedIndex == -1;
    }

    private static bool ProgrammaticSelectionWhileOpen()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        combo.SelectedIndex = 2;
        return combo.SelectedIndex == 2 && !combo.IsOpen;
    }

    private static bool ClearItemsWhileOpen()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        combo.ClearItems();
        return combo.ItemCount == 0 && combo.SelectedIndex == -1 && !combo.IsOpen;
    }

    private static bool PanelRestoreAndReopen()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosComboBox combo = NewCombo();
        bool attached = panel.TryAddChild(combo, 8, 14) ==
            GuideXosPanelResult.Added;
        GuideXosControlHost host = NewHost(combo);
        bool initialFocused = host.TryFocus(1) == GuideXosControlHostResult.Focused;
        bool opened = host.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Activated;
        panel.SetVisible(false);
        host.RefreshVisibility();
        panel.SetVisible(true);
        host.RefreshVisibility();
        bool focused = host.TryFocus(1) == GuideXosControlHostResult.Focused;
        bool reopened =
            host.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosControlHostResult.Activated && combo.IsOpen;
        return attached && opened && focused && reopened;
    }

    private static bool ResetRelaunch()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        host.Reset();
        bool reset = host.RegistrationCount == 0 && !combo.IsOpen;
        return reset && host.TryRegisterComboBox(1, combo) ==
            GuideXosControlHostResult.Registered && host.RegistrationCount == 1;
    }

    private static bool RegistrationAccounting()
    {
        GuideXosControlHost host = new(2);
        GuideXosComboBox first = NewCombo();
        GuideXosComboBox second = NewCombo();
        bool firstRegistered = host.TryRegisterComboBox(1, first) ==
            GuideXosControlHostResult.Registered;
        bool secondRegistered = host.TryRegisterComboBox(2, second) ==
            GuideXosControlHostResult.Registered;
        bool overflow = host.TryRegisterComboBox(3, NewCombo()) ==
            GuideXosControlHostResult.Rejected;
        return firstRegistered && secondRegistered && overflow &&
            host.RegistrationCount == 2;
    }
}
