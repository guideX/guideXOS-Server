namespace HostLogProof;

/// <summary>Host-level C132 probes for ordinary registered radio controls.</summary>
public static class GuideXosRadioButtonC132HostTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool result = true;
        result &= Check(RegistrationAndTraversal());
        result &= Check(PointerAndSpace());
        result &= Check(AlreadySelectedIsIdempotent());
        result &= Check(ArrowNavigation());
        result &= Check(DisabledAndHiddenSkip());
        result &= Check(IndependentGroups());
        result &= Check(PanelAndModalIsolation());
        result &= Check(UnregisterRemovesGroupMember());
        result &= Check(InitialConflictIsDeterministic());
        result &= Check(FocusRecoveryAfterLifecycle());
        result &= Check(RegistrationCapacityBounded());
        result &= Check(ResetRelaunch());
        bool count = s_caseCount == 12;
        host?.TryLog(result && count
            ? "C132-RADIO-HOST-TESTS cases=12 result=PASS"u8
            : "C132-RADIO-HOST-TESTS cases=12 result=FAIL"u8);
        return result && count;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static GuideXosRadioButton New(string label, bool selected = false)
    {
        return new GuideXosRadioButton(10, 20, 160, 28, label, selected);
    }

    private static bool RegistrationAndTraversal()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost host = new(2);
        return host.TryRegisterRadioButton(10, first) ==
                GuideXosControlHostResult.Registered &&
            host.TryRegisterRadioButton(20, second) ==
                GuideXosControlHostResult.Registered &&
            host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 10 &&
            host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 20 &&
            host.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 10;
    }

    private static bool PointerAndSpace()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = new(200, 20, 160, 28, "B");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        bool pointer = host.FocusAndRoutePointer(2, 200, 20) ==
            GuideXosControlHostResult.Changed && second.Checked && !first.Checked;
        host.TryFocus(1);
        bool keyDown = host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored;
        bool keyChar = host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Changed && first.Checked && !second.Checked;
        return pointer && keyDown && keyChar;
    }

    private static bool AlreadySelectedIsIdempotent()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton radio = New("A");
        int callbacks = 0;
        radio.Changed = _ => ++callbacks;
        group.TryRegister(radio);
        GuideXosControlHost host = new(1);
        host.TryRegisterRadioButton(1, radio);
        host.TryFocus(1);
        host.HandleCharacter(' ');
        callbacks = 0;
        GuideXosControlHostResult result = host.HandleCharacter(' ');
        return result == GuideXosControlHostResult.Changed && radio.Checked &&
            callbacks == 0;
    }

    private static bool ArrowNavigation()
    {
        GuideXosRadioGroup group = new(3);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        GuideXosRadioButton third = New("C");
        group.TryRegister(first);
        group.TryRegister(second);
        group.TryRegister(third);
        GuideXosControlHost host = new(3);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        host.TryRegisterRadioButton(3, third);
        host.TryFocus(1);
        bool next = host.HandleKey(GuideXosTextInputKey.Right) ==
            GuideXosControlHostResult.Moved && host.ActiveControlId == 2 &&
            second.Checked && !first.Checked;
        bool previous = host.HandleKey(GuideXosTextInputKey.Left) ==
            GuideXosControlHostResult.Moved && host.ActiveControlId == 1 &&
            first.Checked && !second.Checked;
        return next && previous;
    }

    private static bool DisabledAndHiddenSkip()
    {
        GuideXosRadioGroup group = new(3);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        GuideXosRadioButton third = New("C");
        group.TryRegister(first);
        group.TryRegister(second);
        group.TryRegister(third);
        GuideXosControlHost host = new(3);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        host.TryRegisterRadioButton(3, third);
        second.SetEnabled(false);
        third.SetVisible(false);
        bool traversal = host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 1;
        bool pointer = host.FocusAndRoutePointer(2, 10, 20) ==
            GuideXosControlHostResult.Disabled;
        return traversal && pointer && !second.Checked && !third.Checked;
    }

    private static bool IndependentGroups()
    {
        GuideXosRadioGroup firstGroup = new(2);
        GuideXosRadioGroup secondGroup = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton other = New("Other");
        firstGroup.TryRegister(first);
        secondGroup.TryRegister(other);
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, other);
        host.TryFocus(1);
        host.HandleCharacter(' ');
        host.TryFocus(2);
        host.HandleCharacter(' ');
        return first.Checked && other.Checked;
    }

    private static bool PanelAndModalIsolation()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton radio = New("A");
        group.TryRegister(radio);
        panel.TryAddChild(radio, 8, 14);
        GuideXosControlHost main = new(1);
        GuideXosControlHost modal = new(1);
        main.TryRegisterRadioButton(1, radio);
        modal.TryRegisterButton(9, new GuideXosButton(200, 20, 80, 24, "Modal"));
        main.TryFocus(1);
        bool entered = main.EnterModal(modal) && !radio.IsFocused;
        bool isolated = main.HandleCharacter(' ') == GuideXosControlHostResult.Ignored &&
            !radio.Checked;
        bool exited = main.ExitModal() && main.ActiveControlId == 1;
        panel.SetVisible(false);
        main.RefreshVisibility();
        return entered && isolated && exited && !radio.EffectiveVisible;
    }

    private static bool UnregisterRemovesGroupMember()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A", true);
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        return host.TryUnregister(1) == GuideXosControlHostResult.Unregistered &&
            group.MemberCount == 1 && first.Group == null && !first.Checked;
    }

    private static bool InitialConflictIsDeterministic()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A", true);
        GuideXosRadioButton second = New("B", true);
        group.TryRegister(first);
        group.TryRegister(second);
        return group.SelectedIndex == 1 && !first.Checked && second.Checked;
    }

    private static bool FocusRecoveryAfterLifecycle()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        host.TryFocus(1);
        first.SetVisible(false);
        return host.RefreshVisibility() == GuideXosControlHostResult.Focused &&
            host.ActiveControlId == 2 && second.IsFocused;
    }

    private static bool RegistrationCapacityBounded()
    {
        GuideXosControlHost host = new(2);
        return host.TryRegisterRadioButton(1, New("A")) ==
                GuideXosControlHostResult.Registered &&
            host.TryRegisterRadioButton(2, New("B")) ==
                GuideXosControlHostResult.Registered &&
            host.TryRegisterRadioButton(3, New("C")) ==
                GuideXosControlHostResult.Rejected && host.RegistrationCount == 2;
    }

    private static bool ResetRelaunch()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A", true);
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        host.Reset();
        bool reset = host.RegistrationCount == 0 && group.MemberCount == 0;
        group.TryRegister(first);
        group.TryRegister(second);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        return reset && host.RegistrationCount == 2 && group.MemberCount == 2;
    }
}
