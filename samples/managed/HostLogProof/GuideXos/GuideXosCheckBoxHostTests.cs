namespace HostLogProof;

/// <summary>Focused host integration probes for the reusable checkbox.</summary>
public static class GuideXosCheckBoxHostTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool registration = Registration();
        bool traversal = Traversal();
        bool disabled = DisabledSkip();
        bool routing = ActiveRoutingAndPointer();
        bool modal = ModalIsolationAndRestoration();
        bool result = registration && traversal && disabled && routing && modal;
        if (host != null)
        {
            host.TryLog(result
                ? "C121-CHECKBOX-HOST-TESTS cases=17 result=PASS"u8
                : "C121-CHECKBOX-HOST-TESTS cases=17 result=FAIL"u8);
        }
        return result && s_caseCount == 17;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static GuideXosControlHost CreateHost(
        out GuideXosButton button,
        out GuideXosCheckBox checkBox,
        out GuideXosTextArea area)
    {
        GuideXosControlHost host = new(4);
        button = new GuideXosButton(10, 20, 80, 24, "Open");
        checkBox = new GuideXosCheckBox(100, 20, 128, 24, "Show path");
        area = new GuideXosTextArea(64, 8, 3, 16);
        area.SetText("doc");
        host.TryRegisterButton(1, button);
        host.TryRegisterCheckBox(2, checkBox);
        host.TryRegisterTextArea(3, area);
        return host;
    }

    private static bool Registration()
    {
        GuideXosCheckBox checkBox = new(10, 20, 128, 24, "Show path");
        GuideXosControlHost host = new(1);
        return Check(host.TryRegisterCheckBox(7, checkBox) ==
                GuideXosControlHostResult.Registered) &&
            Check(host.RegistrationCount == 1 &&
                host.ActiveControlKind == GuideXosManagedControlKind.None) &&
            Check(host.TryRegisterCheckBox(8,
                new GuideXosCheckBox(10, 20, 128, 24, "Other")) ==
                GuideXosControlHostResult.Rejected);
    }

    private static bool Traversal()
    {
        GuideXosControlHost host = CreateHost(
            out _, out GuideXosCheckBox checkBox, out _);
        bool forward = Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 1) &&
            Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 2 &&
                checkBox.IsFocused) &&
            Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 3);
        bool reverse = Check(host.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 2) &&
            Check(host.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && host.ActiveControlId == 1);
        return forward && reverse;
    }

    private static bool DisabledSkip()
    {
        GuideXosControlHost host = CreateHost(
            out _, out GuideXosCheckBox checkBox, out _);
        host.TryFocus(1);
        checkBox.SetEnabled(false);
        bool skippedForward = Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 3);
        bool skippedReverse = Check(host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 1);
        return skippedForward && skippedReverse;
    }

    private static bool ActiveRoutingAndPointer()
    {
        GuideXosControlHost host = CreateHost(
            out _, out GuideXosCheckBox checkBox, out GuideXosTextArea area);
        host.TryFocus(2);
        bool keyChar = Check(host.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosControlHostResult.Ignored && !checkBox.Checked) &&
            Check(host.HandleCharacter(' ') == GuideXosControlHostResult.Toggled &&
                checkBox.Checked && area.Text == "doc");
        bool pointer = Check(host.FocusAndRoutePointer(2, 100, 20) ==
            GuideXosControlHostResult.Toggled && host.ActiveControlId == 2 &&
            checkBox.IsFocused && !checkBox.Checked);
        return keyChar && pointer;
    }

    private static bool ModalIsolationAndRestoration()
    {
        GuideXosControlHost main = CreateHost(
            out _, out GuideXosCheckBox checkBox, out _);
        GuideXosControlHost modal = new(1);
        GuideXosListBox list = new(4, 12, 3, 16);
        list.TryAdd("one");
        modal.TryRegisterListBox(90, list);
        main.TryFocus(2);
        checkBox.SetChecked(false);
        bool entered = Check(main.EnterModal(modal) && main.ActiveIndex == -1 &&
            !checkBox.IsFocused);
        bool isolated = Check(main.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked) &&
            Check(main.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed &&
                modal.ActiveControlId == 90);
        bool restored = Check(main.ExitModal() && main.ActiveControlId == 2 &&
            checkBox.IsFocused);
        return entered && isolated && restored;
    }
}
