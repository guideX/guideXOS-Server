namespace HostLogProof;

public static class GuideXosControlHostTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool empty = EmptyHost();
        bool registration = RegistrationAndOrder();
        bool capacity = CapacityAndRejection();
        bool transfer = FocusTransferInvariant();
        bool forward = ForwardTraversal();
        bool reverse = ReverseTraversal();
        bool disabled = DisabledNormalization();
        bool noFocus = NoFocusAndOneControl();
        bool keyboard = KeyboardOwnership();
        bool pointer = PointerSynchronization();
        bool modal = ModalRestoration();
        bool independent = IndependentHosts();
        bool result = empty && registration && capacity && transfer && forward &&
            reverse && disabled && noFocus && keyboard && pointer && modal &&
            independent;
        bool counted = s_caseCount == 50;
        host.TryLog(result && counted
            ? "C120-TESTS cases=50 result=PASS"u8
            : "C120-TESTS cases=50 result=FAIL"u8);
        return result && counted;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool EmptyHost()
    {
        GuideXosControlHost host = new(8);
        return Check(host.RegistrationCount == 0 && host.ActiveIndex == -1) &&
            Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Ignored) &&
            Check(host.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Ignored) &&
            Check(host.HandleCharacter('\t') == GuideXosControlHostResult.Ignored);
    }

    private static bool RegistrationAndOrder()
    {
        FourControlFixture fixture = CreateFourControlFixture();
        GuideXosControlHost host = fixture.Host;
        return Check(host.RegistrationCount == 4) &&
            Check(host.MaximumControlCount == 8) &&
            Check(host.ActiveIndex == -1 && host.ActiveControlId == 0) &&
            Check(host.TryFocus(10) == GuideXosControlHostResult.Focused &&
                host.ActiveControlId == 10 &&
                host.ActiveControlKind == GuideXosManagedControlKind.Button) &&
            Check(host.TryFocus(30) == GuideXosControlHostResult.Focused &&
                host.ActiveControlId == 30 &&
                host.ActiveControlKind == GuideXosManagedControlKind.TextArea);
    }

    private static bool CapacityAndRejection()
    {
        GuideXosControlHost host = new(8);
        for (int index = 0; index < 8; index++)
        {
            if (host.TryRegisterButton(index + 1,
                    new GuideXosButton(10, 20, 64, 24, "B")) !=
                GuideXosControlHostResult.Registered)
            {
                return Check(false);
            }
        }
        GuideXosControlHostResult overflow = host.TryRegisterButton(
            9, new GuideXosButton(10, 20, 64, 24, "B"));
        GuideXosControlHostResult duplicate = host.TryRegisterButton(
            1, new GuideXosButton(10, 20, 64, 24, "B"));
        GuideXosControlHostResult invalid = host.TryRegisterButton(
            0, new GuideXosButton(10, 20, 64, 24, "B"));
        return Check(overflow == GuideXosControlHostResult.Rejected &&
                host.RegistrationCount == 8) &&
            Check(duplicate == GuideXosControlHostResult.Rejected &&
                host.RegistrationCount == 8) &&
            Check(invalid == GuideXosControlHostResult.Rejected &&
                host.RegistrationCount == 8) &&
            Check(host.TryFocus(1) == GuideXosControlHostResult.Focused &&
                host.TryFocus(99) == GuideXosControlHostResult.Rejected &&
                host.ActiveControlId == 1) &&
            Check(host.TrySetFocusable(1, false) ==
                GuideXosControlHostResult.Focused && host.ActiveControlId == 2);
    }

    private static bool FocusTransferInvariant()
    {
        FourControlFixture fixture = CreateFourControlFixture();
        GuideXosControlHost host = fixture.Host;
        GuideXosButton button = fixture.Open;
        GuideXosButton save = fixture.Save;
        GuideXosTextArea area = fixture.Area;
        GuideXosListBox list = fixture.List;
        list.TryAdd("one");
        bool first = host.TryFocus(10) == GuideXosControlHostResult.Focused &&
            button.IsFocused && !save.IsFocused && !area.IsFocused && !list.IsFocused;
        bool second = host.TryFocus(20) == GuideXosControlHostResult.Focused &&
            !button.IsFocused && save.IsFocused && !area.IsFocused && !list.IsFocused;
        bool third = host.TryFocus(30) == GuideXosControlHostResult.Focused &&
            !button.IsFocused && !save.IsFocused && area.IsFocused && !list.IsFocused;
        bool fourth = host.TryFocus(40) == GuideXosControlHostResult.Focused &&
            !button.IsFocused && !save.IsFocused && !area.IsFocused && list.IsFocused;
        return Check(first) && Check(second) && Check(third) && Check(fourth);
    }

    private static bool ForwardTraversal()
    {
        GuideXosControlHost host = CreateFourControlFixture().Host;
        bool first = host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 10;
        bool second = host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 20;
        bool third = host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 30;
        bool fourth = host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 40;
        bool wrap = host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 10;
        return Check(first) && Check(second) && Check(third) && Check(fourth) &&
            Check(wrap);
    }

    private static bool ReverseTraversal()
    {
        GuideXosControlHost host = CreateFourControlFixture().Host;
        bool first = host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 40;
        bool second = host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 30;
        bool third = host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 20;
        bool fourth = host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 10;
        bool wrap = host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 40;
        return Check(first) && Check(second) && Check(third) && Check(fourth) &&
            Check(wrap);
    }

    private static bool DisabledNormalization()
    {
        FourControlFixture fixture = CreateFourControlFixture();
        GuideXosControlHost host = fixture.Host;
        GuideXosButton save = fixture.Save;
        host.TryFocus(10);
        save.SetEnabled(false);
        bool forward = host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 30;
        bool backward = host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 10;
        host.TrySetFocusable(10, false);
        host.TrySetFocusable(30, false);
        host.TrySetFocusable(40, false);
        bool allDisabled = host.ActiveIndex == -1 &&
            host.HandleKey(GuideXosTextInputKey.Tab) == GuideXosControlHostResult.Ignored;
        return Check(forward) && Check(backward) && Check(allDisabled);
    }

    private static bool NoFocusAndOneControl()
    {
        GuideXosControlHost empty = new(8);
        bool noFocusForward = empty.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Ignored && empty.ActiveIndex == -1;
        bool noFocusReverse = empty.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Ignored && empty.ActiveIndex == -1;
        GuideXosControlHost one = new(8);
        one.TryRegisterButton(7, new GuideXosButton(10, 20, 64, 24, "One"));
        bool first = one.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && one.ActiveControlId == 7;
        bool repeat = one.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && one.ActiveControlId == 7;
        bool reverse = one.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && one.ActiveControlId == 7;
        return Check(noFocusForward) && Check(noFocusReverse) && Check(first) &&
            Check(repeat) && Check(reverse);
    }

    private static bool KeyboardOwnership()
    {
        GuideXosControlHost buttonHost = new(2);
        GuideXosButton button = new(10, 20, 80, 24, "Run");
        buttonHost.TryRegisterButton(1, button);
        buttonHost.TryFocus(1);
        bool buttonActivation = buttonHost.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Activated &&
            buttonHost.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosControlHostResult.Ignored &&
            buttonHost.HandleCharacter(' ') == GuideXosControlHostResult.Activated;

        GuideXosControlHost inputHost = new(2);
        GuideXosTextInput input = new(16, "value");
        inputHost.TryRegisterTextInput(1, input);
        inputHost.TryFocus(1);
        bool textInput = inputHost.HandleCharacter('A') ==
            GuideXosControlHostResult.Changed && input.Value == "A" &&
            inputHost.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosControlHostResult.Submitted;

        GuideXosControlHost areaHost = new(2);
        GuideXosTextArea area = new(64, 8, 3, 16);
        area.SetText("doc");
        areaHost.TryRegisterTextArea(1, area);
        areaHost.TryFocus(1);
        bool textArea = areaHost.HandleCharacter(' ') ==
            GuideXosControlHostResult.Changed && area.Text == "doc " &&
            areaHost.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosControlHostResult.Changed && area.Text == "doc \n";
        area.SetText("doc");
        area.SetCaretToStart();
        areaHost.TryFocus(1);
        bool shiftedTextArea = areaHost.HandleKey(
                GuideXosTextInputKey.Right, true) ==
                GuideXosControlHostResult.Moved && area.HasSelection &&
            areaHost.HandleCharacter('X') == GuideXosControlHostResult.Changed &&
            area.Text == "Xoc";

        GuideXosControlHost listHost = new(2);
        GuideXosListBox list = new(4, 12, 3, 16);
        list.TryAdd("one");
        list.TryAdd("two");
        listHost.TryRegisterListBox(1, list);
        listHost.TryFocus(1);
        bool listBox = listHost.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosControlHostResult.Changed && list.SelectedIndex == 1 &&
            listHost.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosControlHostResult.Activated;

        return Check(buttonActivation) && Check(textInput) &&
            Check(textArea && shiftedTextArea) && Check(listBox);
    }

    private static bool PointerSynchronization()
    {
        GuideXosControlHost host = new(4);
        GuideXosButton button = new(10, 20, 80, 24, "Run");
        GuideXosTextArea area = new(64, 8, 3, 16);
        area.SetText("doc");
        host.TryRegisterButton(1, button);
        host.TryRegisterTextArea(2, area);
        bool buttonPointer = host.FocusAndRoutePointer(1, 20, 30) ==
            GuideXosControlHostResult.Activated && button.IsFocused &&
            !area.IsFocused;
        bool areaPointer = host.FocusAndRoutePointer(2, 1, 1) ==
            GuideXosControlHostResult.Focused && area.IsFocused &&
            !button.IsFocused;
        return Check(buttonPointer) && Check(host.ActiveControlId == 2) &&
            Check(areaPointer) && Check(host.ActiveControlId == 2);
    }

    private static bool ModalRestoration()
    {
        FourControlFixture fixture = CreateFourControlFixture();
        GuideXosControlHost main = fixture.Host;
        GuideXosControlHost modal = new(2);
        GuideXosListBox list = new(4, 12, 3, 16);
        list.TryAdd("one");
        modal.TryRegisterListBox(90, list);
        main.TryFocus(30);
        bool entered = main.EnterModal(modal) && main.IsModalActive &&
            main.ActiveScopeHost == modal && main.ActiveIndex == -1 &&
            !fixture.Area.IsFocused;
        bool isolated = modal.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && modal.ActiveControlId == 90 &&
            main.ActiveControlId == 0;
        bool restored = main.ExitModal() && !main.IsModalActive &&
            main.ActiveControlId == 30 && fixture.Area.IsFocused;
        main.TryFocus(10);
        main.EnterModal(modal);
        fixture.Open.SetEnabled(false);
        bool fallback = main.ExitModal() && main.ActiveControlId == 20;
        return Check(entered) && Check(isolated) && Check(restored) && Check(fallback);
    }

    private static bool IndependentHosts()
    {
        GuideXosControlHost first = new(2);
        GuideXosControlHost second = new(2);
        GuideXosButton firstButton = new(10, 20, 80, 24, "A");
        GuideXosButton secondButton = new(10, 20, 80, 24, "B");
        first.TryRegisterButton(1, firstButton);
        second.TryRegisterButton(1, secondButton);
        first.TryFocus(1);
        bool independent = first.ActiveControlId == 1 && firstButton.IsFocused &&
            second.ActiveIndex == -1 && !secondButton.IsFocused;
        bool rejectedStable = first.TryRegisterButton(1,
            new GuideXosButton(10, 20, 80, 24, "C")) ==
            GuideXosControlHostResult.Rejected && first.ActiveControlId == 1 &&
            firstButton.IsFocused;
        return Check(independent) && Check(rejectedStable);
    }

    private sealed class FourControlFixture
    {
        public readonly GuideXosControlHost Host = new(8);
        public readonly GuideXosButton Open =
            new(10, 20, 80, 24, "Open");
        public readonly GuideXosButton Save =
            new(100, 20, 80, 24, "Save");
        public readonly GuideXosTextInput Input =
            new(16, "value");
        public readonly GuideXosTextArea Area =
            new(64, 8, 3, 16);
        public readonly GuideXosListBox List =
            new(4, 12, 3, 16);

        public FourControlFixture()
        {
            Host.TryRegisterButton(10, Open);
            Host.TryRegisterButton(20, Save);
            Host.TryRegisterTextArea(30, Area);
            List.TryAdd("one");
            Host.TryRegisterListBox(40, List);
        }
    }

    private static FourControlFixture CreateFourControlFixture()
    {
        return new FourControlFixture();
    }
}
