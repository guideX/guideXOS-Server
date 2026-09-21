using System;

namespace HostLogProof;

/// <summary>
/// C132 focused probes for Checked/Changed semantics, fixed-capacity group
/// coordination, and lifecycle cancellation. The case count is intentionally
/// explicit so the NativeAOT serial proof cannot silently lose coverage.
/// </summary>
public static class GuideXosRadioButtonC132Tests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        bool result = true;
        result &= Check(InitialUnchecked());
        result &= Check(InitialChecked());
        result &= Check(TwoMembersSameGroup());
        result &= Check(SeparateGroups());
        result &= Check(SelectA());
        result &= Check(SelectBDeselectsA());
        result &= Check(SelectedIsIdempotent());
        result &= Check(PointerSelection());
        result &= Check(SpaceSelection());
        result &= Check(ExactlyOneTransaction());
        result &= Check(CallbackCount());
        result &= Check(CallbackOrdering());
        result &= Check(ProgrammaticTrue());
        result &= Check(ProgrammaticFalse());
        result &= Check(ProgrammaticReplacement());
        result &= Check(DuplicateInitialCheckedNormalization());
        result &= Check(DisabledPointerIgnored());
        result &= Check(DisabledKeyboardIgnored());
        result &= Check(HiddenInputIgnored());
        result &= Check(NonMemberIgnored());
        result &= Check(TabTraversal());
        result &= Check(ShiftTabTraversal());
        result &= Check(PanelMembership());
        result &= Check(SelectedRemovedFromPanel());
        result &= Check(SelectedHidden());
        result &= Check(SelectedDisabled());
        result &= Check(UnregisterReregister());
        result &= Check(CloseRelaunch());
        result &= Check(StaleSpaceVisibility());
        result &= Check(StaleSpaceEnabled());
        result &= Check(StaleSpaceFocus());
        result &= Check(StaleSpaceMembership());
        result &= Check(StaleSpaceModal());
        result &= Check(StaleKeyCharCannotSelectFallback());
        result &= Check(GroupsDoNotCrossAffect());
        result &= Check(CallbackInspectionStable());
        result &= Check(CallbackReselectionBounded());
        result &= Check(RegistrationCountDeterministic());

        bool count = s_caseCount == 38;
        host?.TryLog(result && count
            ? "C132-RADIO-TESTS cases=38 result=PASS"u8
            : "C132-RADIO-TESTS cases=38 result=FAIL"u8);
        return result && count && surface != null;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static GuideXosRadioButton New(
        string label, bool isChecked = false)
    {
        return new GuideXosRadioButton(10, 20, 160, 28, label, isChecked);
    }

    private static bool InitialUnchecked()
    {
        GuideXosRadioButton radio = New("Basic");
        return !radio.Checked && !radio.Selected &&
            radio.Text == "Basic" && radio.Enabled && radio.Visible;
    }

    private static bool InitialChecked()
    {
        GuideXosRadioButton radio = New("Detailed", true);
        return radio.Checked && radio.Selected;
    }

    private static bool TwoMembersSameGroup()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        return group.TryRegister(first) && group.TryRegister(second) &&
            first.Group == group && second.Group == group &&
            group.MemberCount == 2;
    }

    private static bool SeparateGroups()
    {
        GuideXosRadioGroup firstGroup = new(2);
        GuideXosRadioGroup secondGroup = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        firstGroup.TryRegister(first);
        secondGroup.TryRegister(second);
        first.Checked = true;
        second.Checked = true;
        return first.Checked && second.Checked &&
            firstGroup.SelectedIndex == 0 && secondGroup.SelectedIndex == 0;
    }

    private static bool SelectA()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        first.Checked = true;
        return first.Checked && !second.Checked && group.SelectedMember == first;
    }

    private static bool SelectBDeselectsA()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        first.Checked = true;
        second.Checked = true;
        return !first.Checked && second.Checked && group.SelectedMember == second;
    }

    private static bool SelectedIsIdempotent()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        int callbackCount = 0;
        second.Changed = _ => ++callbackCount;
        group.TryRegister(first);
        group.TryRegister(second);
        second.Checked = true;
        callbackCount = 0;
        second.Checked = true;
        return second.Checked && !first.Checked && callbackCount == 0;
    }

    private static bool PointerSelection()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = new(180, 20, 160, 28, "B");
        group.TryRegister(first);
        group.TryRegister(second);
        first.Checked = true;
        GuideXosRadioButtonResult result = second.HandlePointerDown(180, 20);
        return result == GuideXosRadioButtonResult.Selected &&
            second.Checked && !first.Checked && second.IsFocused;
    }

    private static bool SpaceSelection()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        second.Focus();
        bool keyDown = second.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosRadioButtonResult.Ignored && !second.Checked;
        bool keyChar = second.HandleCharacter(' ') ==
            GuideXosRadioButtonResult.Selected && second.Checked;
        return keyDown && keyChar;
    }

    private static bool ExactlyOneTransaction()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        first.Checked = true;
        uint before = group.SelectionTransactionCount;
        second.Checked = true;
        return group.SelectionTransactionCount == before + 1u &&
            !first.Checked && second.Checked;
    }

    private static bool CallbackCount()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        int firstCount = 0;
        int secondCount = 0;
        first.Changed = _ => ++firstCount;
        second.Changed = _ => ++secondCount;
        group.TryRegister(first);
        group.TryRegister(second);
        first.Checked = true;
        firstCount = 0;
        secondCount = 0;
        second.Checked = true;
        return firstCount == 1 && secondCount == 1;
    }

    private static bool CallbackOrdering()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        char[] order = new char[2];
        int count = 0;
        bool firstSawNone = false;
        bool secondSawSecond = false;
        first.Changed = value =>
        {
            if (!value)
            {
                order[count++] = 'A';
                firstSawNone = group.SelectedIndex == -1 && !first.Checked &&
                    !second.Checked;
            }
        };
        second.Changed = value =>
        {
            if (value)
            {
                order[count++] = 'B';
                secondSawSecond = group.SelectedIndex == 1 &&
                    !first.Checked && second.Checked;
            }
        };
        group.TryRegister(first);
        group.TryRegister(second);
        first.Checked = true;
        count = 0;
        firstSawNone = false;
        secondSawSecond = false;
        second.Checked = true;
        return count == 2 && order[0] == 'A' && order[1] == 'B' &&
            firstSawNone && secondSawSecond;
    }

    private static bool ProgrammaticTrue()
    {
        GuideXosRadioButton radio = New("A");
        radio.Checked = true;
        return radio.Checked;
    }

    private static bool ProgrammaticFalse()
    {
        GuideXosRadioButton radio = New("A", true);
        radio.Checked = false;
        return !radio.Checked;
    }

    private static bool ProgrammaticReplacement()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A", true);
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        second.Checked = true;
        return !first.Checked && second.Checked;
    }

    private static bool DuplicateInitialCheckedNormalization()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A", true);
        GuideXosRadioButton second = New("B", true);
        group.TryRegister(first);
        group.TryRegister(second);
        return !first.Checked && second.Checked && group.SelectedIndex == 1;
    }

    private static bool DisabledPointerIgnored()
    {
        GuideXosRadioButton radio = New("A");
        radio.SetEnabled(false);
        return radio.HandlePointerDown(10, 20) ==
            GuideXosRadioButtonResult.Disabled && !radio.Checked;
    }

    private static bool DisabledKeyboardIgnored()
    {
        GuideXosRadioButton radio = New("A");
        radio.SetEnabled(false);
        return radio.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosRadioButtonResult.Disabled &&
            radio.HandleCharacter(' ') == GuideXosRadioButtonResult.Disabled &&
            !radio.Checked;
    }

    private static bool HiddenInputIgnored()
    {
        GuideXosRadioButton radio = New("A");
        radio.SetVisible(false);
        return radio.HandlePointerDown(10, 20) ==
                GuideXosRadioButtonResult.Ignored &&
            radio.HandleCharacter(' ') == GuideXosRadioButtonResult.Ignored &&
            !radio.Checked;
    }

    private static bool NonMemberIgnored()
    {
        GuideXosControlHost host = new(1);
        GuideXosRadioButton radio = New("A");
        return host.FocusAndRoutePointer(9, 10, 20) ==
            GuideXosControlHostResult.Rejected && !radio.Checked;
    }

    private static GuideXosControlHost MakeTraversalHost(
        out GuideXosRadioButton first, out GuideXosRadioButton second)
    {
        GuideXosRadioGroup group = new(2);
        first = New("A");
        second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        return host;
    }

    private static bool TabTraversal()
    {
        GuideXosControlHost host = MakeTraversalHost(out _, out GuideXosRadioButton second);
        return host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 1 &&
            host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 2 &&
            second.IsFocused;
    }

    private static bool ShiftTabTraversal()
    {
        GuideXosControlHost host = MakeTraversalHost(out GuideXosRadioButton first, out _);
        host.HandleKey(GuideXosTextInputKey.Tab);
        host.HandleKey(GuideXosTextInputKey.Tab);
        return host.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 1 &&
            first.IsFocused;
    }

    private static bool PanelMembership()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 2);
        GuideXosRadioButton radio = New("A");
        return panel.TryAddChild(radio, 8, 14) == GuideXosPanelResult.Added &&
            radio.ParentPanel == panel && panel.ContainsChild(radio);
    }

    private static bool SelectedRemovedFromPanel()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 2);
        GuideXosRadioButton radio = New("A");
        panel.TryAddChild(radio, 8, 14);
        radio.Checked = true;
        panel.TryRemoveChild(radio);
        return radio.ParentPanel == null && radio.Checked;
    }

    private static bool SelectedHidden()
    {
        GuideXosRadioButton radio = New("A", true);
        radio.SetVisible(false);
        return radio.Checked && !radio.EffectiveVisible;
    }

    private static bool SelectedDisabled()
    {
        GuideXosRadioButton radio = New("A", true);
        radio.SetEnabled(false);
        return radio.Checked && !radio.Enabled;
    }

    private static bool UnregisterReregister()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A", true);
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        bool removed = host.TryUnregister(1) == GuideXosControlHostResult.Unregistered &&
            group.MemberCount == 1 && first.Group == null && !first.Checked;
        bool reregistered = group.TryRegister(first) &&
            host.TryRegisterRadioButton(3, first) == GuideXosControlHostResult.Registered &&
            group.MemberCount == 2;
        return removed && reregistered;
    }

    private static bool CloseRelaunch()
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
        bool closed = host.RegistrationCount == 0 && group.MemberCount == 0 &&
            !first.Checked && !second.Checked;
        group.TryRegister(first);
        group.TryRegister(second);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        return closed && host.RegistrationCount == 2 && group.MemberCount == 2;
    }

    private static bool StaleSpaceVisibility()
    {
        return StaleSpaceTransition(0);
    }

    private static bool StaleSpaceEnabled()
    {
        return StaleSpaceTransition(1);
    }

    private static bool StaleSpaceFocus()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton radio = New("A");
        group.TryRegister(radio);
        GuideXosButton fallback = new(200, 20, 80, 24, "Fallback");
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(2, radio);
        host.TryRegisterButton(1, fallback);
        host.TryFocus(2);
        host.HandleKey((GuideXosTextInputKey)' ');
        host.TryFocus(1);
        return host.HandleCharacter(' ') == GuideXosControlHostResult.Ignored &&
            !radio.Checked;
    }

    private static bool StaleSpaceMembership()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton radio = New("A");
        group.TryRegister(radio);
        panel.TryAddChild(radio, 8, 14);
        GuideXosControlHost host = new(1);
        host.TryRegisterRadioButton(1, radio);
        host.TryFocus(1);
        host.HandleKey((GuideXosTextInputKey)' ');
        panel.TryRemoveChild(radio);
        host.RefreshVisibility();
        return host.HandleCharacter(' ') == GuideXosControlHostResult.Ignored &&
            !radio.Checked;
    }

    private static bool StaleSpaceModal()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton radio = New("A");
        group.TryRegister(radio);
        GuideXosControlHost host = new(1);
        GuideXosControlHost modal = new(1);
        GuideXosButton modalButton = new(200, 20, 80, 24, "Modal");
        host.TryRegisterRadioButton(1, radio);
        modal.TryRegisterButton(9, modalButton);
        host.TryFocus(1);
        host.HandleKey((GuideXosTextInputKey)' ');
        host.EnterModal(modal);
        bool consumed = host.HandleCharacter(' ') == GuideXosControlHostResult.Ignored;
        host.ExitModal();
        return consumed && !radio.Checked;
    }

    private static bool StaleSpaceTransition(int kind)
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton radio = New("A");
        group.TryRegister(radio);
        GuideXosButton fallback = new(200, 20, 80, 24, "Fallback");
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(2, radio);
        host.TryRegisterButton(1, fallback);
        host.TryFocus(2);
        host.HandleKey((GuideXosTextInputKey)' ');
        if (kind == 0)
        {
            radio.SetVisible(false);
            host.RefreshVisibility();
            radio.SetVisible(true);
            host.RefreshVisibility();
        }
        else
        {
            radio.SetEnabled(false);
            host.RefreshVisibility();
            radio.SetEnabled(true);
            host.RefreshVisibility();
        }
        return host.HandleCharacter(' ') == GuideXosControlHostResult.Ignored &&
            !radio.Checked;
    }

    private static bool StaleKeyCharCannotSelectFallback()
    {
        GuideXosControlHost host = new(1);
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton radio = New("A");
        group.TryRegister(radio);
        host.TryRegisterRadioButton(1, radio);
        host.TryFocus(1);
        host.HandleKey((GuideXosTextInputKey)' ');
        radio.SetVisible(false);
        host.RefreshVisibility();
        radio.SetVisible(true);
        host.RefreshVisibility();
        return host.HandleCharacter(' ') == GuideXosControlHostResult.Ignored &&
            !radio.Checked;
    }

    private static bool GroupsDoNotCrossAffect()
    {
        GuideXosRadioGroup firstGroup = new(2);
        GuideXosRadioGroup secondGroup = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        GuideXosRadioButton other = New("Other");
        firstGroup.TryRegister(first);
        firstGroup.TryRegister(second);
        secondGroup.TryRegister(other);
        first.Checked = true;
        other.Checked = true;
        second.Checked = true;
        return !first.Checked && second.Checked && other.Checked;
    }

    private static bool CallbackInspectionStable()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        bool stable = true;
        first.Changed = value =>
        {
            stable &= !value && group.SelectedIndex == -1 &&
                !first.Checked && !second.Checked;
        };
        second.Changed = value =>
        {
            stable &= value && group.SelectedMember == second &&
                !first.Checked && second.Checked;
        };
        group.TryRegister(first);
        group.TryRegister(second);
        first.Checked = true;
        stable = true;
        second.Checked = true;
        return stable;
    }

    private static bool CallbackReselectionBounded()
    {
        GuideXosRadioGroup group = new(3);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        GuideXosRadioButton third = New("C");
        group.TryRegister(first);
        group.TryRegister(second);
        group.TryRegister(third);
        first.Checked = true;
        int callbackCount = 0;
        second.Changed = value =>
        {
            ++callbackCount;
            if (value) third.Checked = true;
        };
        third.Changed = _ => ++callbackCount;
        second.Checked = true;
        return callbackCount <= 8 && third.Checked && !first.Checked &&
            !second.Checked && group.SelectedMember == third &&
            group.SelectionTransactionCount <= 8u;
    }

    private static bool RegistrationCountDeterministic()
    {
        GuideXosControlHost host = new(3);
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        group.TryRegister(first);
        group.TryRegister(second);
        return host.TryRegisterRadioButton(10, first) ==
                GuideXosControlHostResult.Registered &&
            host.TryRegisterRadioButton(20, second) ==
                GuideXosControlHostResult.Registered &&
            host.TryRegisterRadioButton(10, New("Duplicate")) ==
                GuideXosControlHostResult.Rejected && host.RegistrationCount == 2 &&
            host.TryRegisterRadioButton(30, New("Third")) ==
                GuideXosControlHostResult.Registered && host.RegistrationCount == 3;
    }
}
