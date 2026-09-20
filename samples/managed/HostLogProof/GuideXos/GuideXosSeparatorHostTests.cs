namespace HostLogProof;

/// <summary>
/// Host probes proving that a separator remains outside interactive
/// registration, routing, focus traversal, and modal scope ownership.
/// </summary>
public static class GuideXosSeparatorHostTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        GuideXosSeparator separator = new(20, 200, 480);
        GuideXosButton open = new(20, 220, 90, 28, "Open");
        GuideXosButton save = new(120, 220, 90, 28, "Save");
        GuideXosButton saveAs = new(220, 220, 100, 28, "Save As");
        GuideXosCheckBox checkBox = new(330, 220, 128, 28, "Show path", true);
        GuideXosTextArea document = new(20, 72, 4, 48);
        document.SetText("doc");
        GuideXosControlHost controlHost = new(5);
        bool registered = Check(controlHost.TryRegisterButton(1, open) ==
                GuideXosControlHostResult.Registered) &&
            Check(controlHost.TryRegisterButton(2, save) ==
                GuideXosControlHostResult.Registered) &&
            Check(controlHost.TryRegisterButton(3, saveAs) ==
                GuideXosControlHostResult.Registered) &&
            Check(controlHost.TryRegisterCheckBox(4, checkBox) ==
                GuideXosControlHostResult.Registered) &&
            Check(controlHost.TryRegisterTextArea(5, document) ==
                GuideXosControlHostResult.Registered);
        bool countUnchanged = Check(registered && controlHost.RegistrationCount == 5);
        bool noInitialFocus = Check(controlHost.ActiveIndex == -1 &&
            controlHost.ActiveControlId == 0);
        bool forward = Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 1) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 2) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 3) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 4) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 5);
        bool forwardWrap = Check(controlHost.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 1);
        bool reverse = Check(controlHost.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 5) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 4) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 3) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 2) &&
            Check(controlHost.HandleKey(GuideXosTextInputKey.Tab, true) ==
                GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 1);
        bool reverseWrap = Check(controlHost.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && controlHost.ActiveControlId == 5);

        bool activeFocus = Check(controlHost.TryFocus(5) ==
            GuideXosControlHostResult.Focused && document.IsFocused);
        separator.SetVisible(false);
        bool hiddenPreservesFocus = Check(!separator.Visible &&
            controlHost.ActiveControlId == 5 && document.IsFocused);
        separator.SetVisible(true);
        bool shownPreservesFocus = Check(separator.Visible &&
            controlHost.ActiveControlId == 5 && document.IsFocused);
        bool geometryPreservesFocus = Check(separator.TrySetBounds(40, 204, 320) &&
            controlHost.ActiveControlId == 5 && document.IsFocused);
        separator.SetVisible(false);
        bool hiddenGeometry = Check(separator.TrySetBounds(80, 204, 96) &&
            !separator.Visible && separator.Width == 96 &&
            controlHost.ActiveControlId == 5);
        separator.SetVisible(true);
        bool showUpdated = Check(separator.Visible && separator.Width == 96 &&
            controlHost.ActiveControlId == 5 && document.IsFocused);
        bool pointerIsolation = Check(controlHost.FocusAndRoutePointer(
            99, separator.X, separator.Y) == GuideXosControlHostResult.Rejected &&
            controlHost.ActiveControlId == 5);
        bool keyboardIsolation = Check(controlHost.HandleKey(
            GuideXosTextInputKey.Tab) == GuideXosControlHostResult.Traversed &&
            controlHost.ActiveControlId == 1);

        GuideXosControlHost modal = new(1);
        GuideXosButton modalButton = new(10, 10, 64, 24, "Modal");
        bool modalRegistered = Check(modal.TryRegisterButton(90, modalButton) ==
            GuideXosControlHostResult.Registered);
        bool modalEntered = Check(controlHost.EnterModal(modal) &&
            controlHost.IsModalActive && controlHost.ActiveScopeHost == modal);
        bool modalFocused = Check(modal.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && modal.ActiveControlId == 90);
        separator.SetVisible(false);
        bool modalPresentationIsolation = Check(!separator.Visible &&
            controlHost.ActiveScopeHost == modal && modal.ActiveControlId == 90);
        separator.SetVisible(true);
        bool modalShownIsolation = Check(separator.Visible &&
            controlHost.ActiveScopeHost == modal && modal.ActiveControlId == 90);
        bool modalExited = Check(controlHost.ExitModal() &&
            !controlHost.IsModalActive && controlHost.ActiveControlId == 1);

        bool result = registered && countUnchanged && noInitialFocus && forward &&
            forwardWrap && reverse && reverseWrap && activeFocus &&
            hiddenPreservesFocus && shownPreservesFocus && geometryPreservesFocus &&
            hiddenGeometry && showUpdated && pointerIsolation && keyboardIsolation &&
            modalRegistered && modalEntered && modalFocused &&
            modalPresentationIsolation && modalShownIsolation && modalExited &&
            s_caseCount == 33;
        if (host != null)
        {
            host.TryLog(result
                ? "C123-SEPARATOR-HOST-TESTS cases=33 result=PASS"u8
                : "C123-SEPARATOR-HOST-TESTS cases=33 result=FAIL"u8);
        }
        return result;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }
}
