namespace HostLogProof;

/// <summary>Host-level probes for radio registration, routing, and modal scope.</summary>
public static class GuideXosRadioButtonHostTests
{
    public static bool Run(GuideXosHost host)
    {
        bool result = RegistrationAndTraversal() && DisabledSkip() &&
            PointerAndArrowRouting() && IndependentGroups() && ModalIsolation();
        host?.TryLog(result
            ? "C124-RADIO-HOST-TESTS result=PASS"u8
            : "C124-RADIO-HOST-TESTS result=FAIL"u8);
        return result;
    }

    private static bool RegistrationAndTraversal()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("First");
        GuideXosRadioButton second = New("Second");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost host = new(4);
        bool registered = host.TryRegisterRadioButton(10, first) ==
                GuideXosControlHostResult.Registered &&
            host.TryRegisterRadioButton(20, second) ==
                GuideXosControlHostResult.Registered && host.RegistrationCount == 2;
        bool forward = host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 10 &&
            host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 20;
        bool reverse = host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 10;
        bool oneFocus = first.IsFocused && !second.IsFocused;
        return registered && forward && reverse && oneFocus;
    }

    private static bool DisabledSkip()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("First");
        GuideXosRadioButton second = New("Second");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost host = new(4);
        host.TryRegisterRadioButton(10, first);
        host.TryRegisterRadioButton(20, second);
        host.TryFocus(10);
        second.SetEnabled(false);
        bool forward = host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 10;
        bool backward = host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 10;
        return forward && backward && first.IsFocused && !second.IsFocused;
    }

    private static bool PointerAndArrowRouting()
    {
        GuideXosRadioGroup group = new(3);
        GuideXosRadioButton first = New("First");
        GuideXosRadioButton second = New("Second");
        GuideXosRadioButton third = New("Third");
        group.TryRegister(first);
        group.TryRegister(second);
        group.TryRegister(third);
        GuideXosControlHost host = new(4);
        host.TryRegisterRadioButton(10, first);
        host.TryRegisterRadioButton(20, second);
        host.TryRegisterRadioButton(30, third);
        bool outside = host.FocusAndRoutePointer(10, 1, 1) ==
            GuideXosControlHostResult.Ignored && host.ActiveIndex == -1;
        bool pointer = host.FocusAndRoutePointer(20, 80, 20) ==
            GuideXosControlHostResult.Changed && host.ActiveControlId == 20 &&
            second.IsFocused && second.Selected && !first.Selected;
        bool keyDown = host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored && second.Selected;
        bool space = host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Changed && second.Selected;
        bool arrow = host.HandleKey(GuideXosTextInputKey.Right) ==
            GuideXosControlHostResult.Moved && host.ActiveControlId == 30 &&
            third.IsFocused && third.Selected && !second.Selected;
        bool wrap = host.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosControlHostResult.Moved && host.ActiveControlId == 10 &&
            first.Selected;
        third.SetEnabled(false);
        bool skipped = host.HandleKey(GuideXosTextInputKey.Right) ==
            GuideXosControlHostResult.Moved && host.ActiveControlId == 20;
        return outside && pointer && keyDown && space && arrow && wrap && skipped;
    }

    private static bool IndependentGroups()
    {
        GuideXosRadioGroup firstGroup = new(2);
        GuideXosRadioGroup secondGroup = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        GuideXosRadioButton other = New("Other");
        firstGroup.TryRegister(first);
        firstGroup.TryRegister(second);
        secondGroup.TryRegister(other);
        GuideXosControlHost host = new(4);
        host.TryRegisterRadioButton(10, first);
        host.TryRegisterRadioButton(20, second);
        host.TryRegisterRadioButton(30, other);
        host.TryFocus(10);
        host.HandleCharacter(' ');
        host.TryFocus(30);
        host.HandleCharacter(' ');
        second.SetEnabled(false);
        return firstGroup.SelectedIndex == 0 && secondGroup.SelectedIndex == 0 &&
            first.Selected && other.Selected && !second.Selected;
    }

    private static bool ModalIsolation()
    {
        GuideXosRadioGroup group = new(2);
        GuideXosRadioButton first = New("First");
        GuideXosRadioButton second = New("Second");
        group.TryRegister(first);
        group.TryRegister(second);
        GuideXosControlHost main = new(4);
        main.TryRegisterRadioButton(10, first);
        main.TryRegisterRadioButton(20, second);
        GuideXosControlHost modal = new(2);
        GuideXosListBox list = new(4, 12, 3, 16);
        list.TryAdd("one");
        modal.TryRegisterListBox(90, list);
        main.TryFocus(10);
        bool entered = main.EnterModal(modal) && main.IsModalActive &&
            !first.IsFocused && main.ActiveControlId == 0;
        bool isolated = main.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !first.Selected &&
            modal.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed;
        bool restored = main.ExitModal() && main.ActiveControlId == 10 &&
            first.IsFocused;
        return entered && isolated && restored;
    }

    private static GuideXosRadioButton New(string label)
    {
        return new GuideXosRadioButton(10, 20, 160, 28, label);
    }
}
