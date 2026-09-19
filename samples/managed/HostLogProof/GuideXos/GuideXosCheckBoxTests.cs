namespace HostLogProof;

/// <summary>Deterministic focused probes for the reusable managed checkbox.</summary>
public static class GuideXosCheckBoxTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        bool construction = ConstructionAndBounds();
        bool state = CheckedState();
        bool focus = FocusAndReset();
        bool pointer = PointerBehavior();
        bool keyboard = KeyboardBehavior();
        bool rendering = Rendering(surface);
        bool independent = IndependentInstances();
        bool result = construction && state && focus && pointer && keyboard &&
            rendering && independent;

        if (host != null)
        {
            host.TryLog(result
                ? "C121-CHECKBOX-TESTS cases=50 result=PASS"u8
                : "C121-CHECKBOX-TESTS cases=50 result=FAIL"u8);
        }
        return result && s_caseCount == 50;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool ConstructionAndBounds()
    {
        GuideXosCheckBox checkBox = new(10, 20, 128, 24, "Show path");
        bool construction = Check(checkBox.X == 10 && checkBox.Y == 20) &&
            Check(checkBox.Width == 128 && checkBox.Height == 24) &&
            Check(checkBox.Label == "Show path") &&
            Check(!checkBox.Checked && checkBox.Enabled && !checkBox.IsFocused);

        bool validLabel = Check(checkBox.SetLabel("Show status") &&
            checkBox.Label == "Show status");
        bool emptyRejected = Check(!checkBox.SetLabel(string.Empty) &&
            checkBox.Label == "Show status");
        string maximum = new('A', checkBox.MaximumLabelLength);
        bool maximumAccepted = Check(checkBox.SetLabel(maximum) &&
            checkBox.Label == maximum);
        string overlong = new('B', checkBox.MaximumLabelLength + 1);
        bool overlongRejected = Check(!checkBox.SetLabel(overlong) &&
            checkBox.Label == maximum);

        bool validGeometry = Check(checkBox.TrySetBounds(32, 40, 160, 28) &&
            checkBox.X == 32 && checkBox.Y == 40 && checkBox.Width == 160 &&
            checkBox.Height == 28);
        int oldX = checkBox.X;
        bool invalidGeometry = Check(!checkBox.TrySetBounds(
            4090, 4090, 32, 18) && checkBox.X == oldX);
        bool rejectedState = Check(!checkBox.SetLabel("") &&
            !checkBox.TrySetBounds(-1, 40, 160, 28) &&
            checkBox.Label == maximum && checkBox.X == oldX &&
            !checkBox.Checked && checkBox.Enabled && !checkBox.IsFocused);
        return construction && validLabel && emptyRejected && maximumAccepted &&
            overlongRejected && validGeometry && invalidGeometry && rejectedState;
    }

    private static bool CheckedState()
    {
        GuideXosCheckBox initial = new(10, 20, 128, 24, "Show path", true);
        bool initialChecked = Check(initial.Checked);
        initial.Focus();
        bool programmaticUnchangedFocus = Check(initial.IsFocused);
        initial.SetChecked(false);
        bool programmaticUncheck = Check(!initial.Checked && initial.IsFocused);
        initial.SetChecked(true);
        bool programmaticCheck = Check(initial.Checked && initial.IsFocused);
        bool toggleOff = Check(initial.Toggle() == GuideXosCheckBoxResult.Toggled &&
            !initial.Checked);
        bool toggleOn = Check(initial.Toggle() == GuideXosCheckBoxResult.Toggled &&
            initial.Checked);
        return initialChecked && programmaticUnchangedFocus &&
            programmaticUncheck && programmaticCheck && toggleOff && toggleOn;
    }

    private static bool FocusAndReset()
    {
        GuideXosCheckBox checkBox = new(10, 20, 128, 24, "Show path", true);
        bool initial = Check(!checkBox.IsFocused);
        checkBox.Focus();
        bool focused = Check(checkBox.IsFocused);
        checkBox.Blur();
        bool blurred = Check(!checkBox.IsFocused);
        checkBox.Focus();
        checkBox.SetEnabled(false);
        bool disabledBlur = Check(!checkBox.Enabled && !checkBox.IsFocused);
        checkBox.SetChecked(false);
        checkBox.Reset();
        bool reset = Check(checkBox.Enabled && !checkBox.IsFocused &&
            !checkBox.Checked && checkBox.RejectedInputCount == 0u);
        return initial && focused && blurred && disabledBlur && reset;
    }

    private static bool PointerBehavior()
    {
        GuideXosCheckBox checkBox = new(10, 20, 128, 24, "Show path");
        bool outside = Check(checkBox.HandlePointerDown(9, 20) ==
            GuideXosCheckBoxResult.Ignored && !checkBox.IsFocused &&
            !checkBox.Checked);
        bool left = Check(checkBox.HandlePointerDown(10, 24) ==
            GuideXosCheckBoxResult.Toggled && checkBox.Checked &&
            checkBox.IsFocused);
        bool right = Check(checkBox.HandlePointerDown(137, 43) ==
            GuideXosCheckBoxResult.Toggled && !checkBox.Checked);
        bool rightOutside = Check(checkBox.HandlePointerDown(138, 43) ==
            GuideXosCheckBoxResult.Ignored && !checkBox.Checked);
        bool top = Check(checkBox.HandlePointerDown(64, 20) ==
            GuideXosCheckBoxResult.Toggled && checkBox.Checked);
        bool bottom = Check(checkBox.HandlePointerDown(64, 43) ==
            GuideXosCheckBoxResult.Toggled && !checkBox.Checked);
        checkBox.SetEnabled(false);
        bool disabled = Check(checkBox.HandlePointerDown(10, 20) ==
            GuideXosCheckBoxResult.Disabled && !checkBox.Checked &&
            !checkBox.IsFocused);
        return outside && left && right && rightOutside && top && bottom &&
            disabled;
    }

    private static bool KeyboardBehavior()
    {
        GuideXosCheckBox checkBox = new(10, 20, 128, 24, "Show path");
        bool unfocused = Check(checkBox.HandleCharacter(' ') ==
            GuideXosCheckBoxResult.Ignored && !checkBox.Checked);
        checkBox.Focus();
        bool keyDownIgnored = Check(checkBox.HandleKey(
            (GuideXosTextInputKey)' ') == GuideXosCheckBoxResult.Ignored &&
            !checkBox.Checked);
        bool keyCharToggled = Check(checkBox.HandleCharacter(' ') ==
            GuideXosCheckBoxResult.Toggled && checkBox.Checked);
        bool exactlyOnce = Check(checkBox.Checked);
        bool enterIgnored = Check(checkBox.HandleKey(
            GuideXosTextInputKey.Enter) == GuideXosCheckBoxResult.Ignored &&
            checkBox.Checked);
        bool ordinaryIgnored = Check(checkBox.HandleCharacter('A') ==
            GuideXosCheckBoxResult.Ignored && checkBox.Checked &&
            checkBox.HandleKey(GuideXosTextInputKey.Up) ==
                GuideXosCheckBoxResult.Ignored);
        checkBox.SetEnabled(false);
        bool disabledKeyDown = Check(checkBox.HandleKey(
            (GuideXosTextInputKey)' ') == GuideXosCheckBoxResult.Disabled &&
            checkBox.Checked);
        bool disabledKeyChar = Check(checkBox.HandleCharacter(' ') ==
            GuideXosCheckBoxResult.Disabled && checkBox.Checked);
        return unfocused && keyDownIgnored && keyCharToggled && exactlyOnce &&
            enterIgnored && ordinaryIgnored && disabledKeyDown && disabledKeyChar;
    }

    private static bool Rendering(GuideXosSurface surface)
    {
        GuideXosCheckBox uncheckedBox = new(10, 20, 160, 24, "Show path");
        GuideXosCheckBox checkedBox = new(180, 20, 160, 24, "Show path", true);
        GuideXosCheckBox focusedUnchecked = new(10, 50, 160, 24, "Show path");
        GuideXosCheckBox focusedChecked = new(180, 50, 160, 24, "Show path", true);
        GuideXosCheckBox disabledUnchecked = new(10, 80, 160, 24, "Show path");
        GuideXosCheckBox disabledChecked = new(180, 80, 160, 24, "Show path", true);
        focusedUnchecked.Focus();
        focusedChecked.Focus();
        disabledUnchecked.SetEnabled(false);
        disabledChecked.SetEnabled(false);
        bool rendered = Check(surface != null) &&
            Check(uncheckedBox.Render(surface) == GuideXosResult.Success) &&
            Check(checkedBox.Render(surface) == GuideXosResult.Success) &&
            Check(focusedUnchecked.Render(surface) == GuideXosResult.Success) &&
            Check(focusedChecked.Render(surface) == GuideXosResult.Success) &&
            Check(disabledUnchecked.Render(surface) == GuideXosResult.Success) &&
            Check(disabledChecked.Render(surface) == GuideXosResult.Success);
        return rendered;
    }

    private static bool IndependentInstances()
    {
        GuideXosCheckBox first = new(10, 20, 128, 24, "First");
        GuideXosCheckBox second = new(150, 20, 128, 24, "Second", true);
        bool labels = Check(first.Label == "First" && second.Label == "Second");
        bool checkedState = Check(!first.Checked && second.Checked);
        first.SetEnabled(false);
        bool enabledState = Check(!first.Enabled && second.Enabled);
        second.Focus();
        bool focusState = Check(!first.IsFocused && second.IsFocused);
        bool toggleIsolation = Check(second.Toggle() ==
            GuideXosCheckBoxResult.Toggled && !second.Checked && !first.Checked);
        first.SetChecked(true);
        bool checkedIsolation = Check(first.Checked && !second.Checked);
        return labels && checkedState && enabledState && focusState &&
            toggleIsolation && checkedIsolation;
    }
}
