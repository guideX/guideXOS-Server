namespace HostLogProof;

/// <summary>Focused bounded probes for the reusable managed radio button.</summary>
public static class GuideXosRadioButtonTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        bool result = Construction() && StateAndFocus() && Pointer() &&
            Keyboard() && Rendering(surface) && ResetAndIndependence();
        host?.TryLog(result
            ? "C124-RADIO-BUTTON-TESTS result=PASS"u8
            : "C124-RADIO-BUTTON-TESTS result=FAIL"u8);
        return result;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool Construction()
    {
        GuideXosRadioButton radio =
            new(10, 20, 160, 28, "Compact");
        bool construction = Check(radio.X == 10 && radio.Y == 20 &&
            radio.Width == 160 && radio.Height == 28 &&
            radio.Label == "Compact" && !radio.Selected &&
            radio.Enabled && !radio.IsFocused);
        bool validLabel = Check(radio.SetLabel("Full path") &&
            radio.Label == "Full path");
        bool emptyRejected = Check(!radio.SetLabel(string.Empty) &&
            radio.Label == "Full path");
        string maximum = new('A', radio.MaximumLabelLength);
        bool maximumAccepted = Check(radio.SetLabel(maximum) &&
            radio.Label == maximum);
        bool overlongRejected = Check(!radio.SetLabel(
            new('B', radio.MaximumLabelLength + 1)) &&
            radio.Label == maximum);
        bool validGeometry = Check(radio.TrySetBounds(32, 40, 192, 32) &&
            radio.X == 32 && radio.Y == 40 && radio.Width == 192 &&
            radio.Height == 32);
        int oldX = radio.X;
        bool invalidGeometry = Check(!radio.TrySetBounds(
            4090, 4090, 32, 18) && radio.X == oldX);
        return construction && validLabel && emptyRejected &&
            maximumAccepted && overlongRejected && validGeometry &&
            invalidGeometry;
    }

    private static bool StateAndFocus()
    {
        GuideXosRadioButton radio =
            new(10, 20, 160, 28, "Full path");
        bool enabledDefault = Check(radio.Enabled);
        radio.Focus();
        bool focus = Check(radio.IsFocused);
        radio.Blur();
        bool blur = Check(!radio.IsFocused);
        radio.Focus();
        radio.SetEnabled(false);
        bool disabled = Check(!radio.Enabled && !radio.IsFocused);
        radio.SetEnabled(true);
        radio.Focus();
        bool reenabled = Check(radio.Enabled && radio.IsFocused);
        return enabledDefault && focus && blur && disabled && reenabled;
    }

    private static bool Pointer()
    {
        GuideXosRadioButton radio =
            new(10, 20, 160, 28, "Full path");
        bool outside = Check(radio.HandlePointerDown(9, 20) ==
            GuideXosRadioButtonResult.Ignored && !radio.IsFocused &&
            !radio.Selected);
        bool inside = Check(radio.HandlePointerDown(10, 20) ==
            GuideXosRadioButtonResult.Selected && radio.IsFocused &&
            radio.Selected);
        radio.SetEnabled(false);
        bool disabled = Check(radio.HandlePointerDown(10, 20) ==
            GuideXosRadioButtonResult.Disabled && radio.Selected);
        return outside && inside && disabled;
    }

    private static bool Keyboard()
    {
        GuideXosRadioButton radio =
            new(10, 20, 160, 28, "Full path");
        bool unfocused = Check(radio.HandleCharacter(' ') ==
            GuideXosRadioButtonResult.Ignored && !radio.Selected);
        radio.Focus();
        bool keyDownIgnored = Check(radio.HandleKey(
            (GuideXosTextInputKey)' ') == GuideXosRadioButtonResult.Ignored &&
            !radio.Selected);
        bool selected = Check(radio.HandleCharacter(' ') ==
            GuideXosRadioButtonResult.Selected && radio.Selected);
        bool exactlyOnce = Check(radio.Selected &&
            radio.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosRadioButtonResult.Ignored && radio.Selected);
        bool enterIgnored = Check(radio.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosRadioButtonResult.Ignored && radio.Selected);
        bool unrelatedIgnored = Check(radio.HandleCharacter('A') ==
            GuideXosRadioButtonResult.Ignored &&
            radio.HandleKey(GuideXosTextInputKey.Home) ==
                GuideXosRadioButtonResult.Ignored && radio.Selected);
        radio.SetEnabled(false);
        bool disabledKey = Check(radio.HandleCharacter(' ') ==
            GuideXosRadioButtonResult.Disabled && radio.Selected);
        return unfocused && keyDownIgnored && selected && exactlyOnce &&
            enterIgnored && unrelatedIgnored && disabledKey;
    }

    private static bool Rendering(GuideXosSurface surface)
    {
        GuideXosRadioButton unselected =
            new(10, 20, 160, 28, "Compact");
        GuideXosRadioButton selected =
            new(180, 20, 160, 28, "Compact");
        GuideXosRadioButton focusedUnselected =
            new(10, 50, 160, 28, "Compact");
        GuideXosRadioButton focusedSelected =
            new(180, 50, 160, 28, "Compact");
        GuideXosRadioButton disabledUnselected =
            new(10, 80, 160, 28, "Compact");
        GuideXosRadioButton disabledSelected =
            new(180, 80, 160, 28, "Compact");
        selected.TrySelect();
        focusedUnselected.Focus();
        focusedSelected.TrySelect();
        focusedSelected.Focus();
        disabledUnselected.SetEnabled(false);
        disabledSelected.TrySelect();
        disabledSelected.SetEnabled(false);
        bool rendered = Check(surface != null) &&
            Check(unselected.Render(surface) == GuideXosResult.Success) &&
            Check(selected.Render(surface) == GuideXosResult.Success) &&
            Check(focusedUnselected.Render(surface) == GuideXosResult.Success) &&
            Check(focusedSelected.Render(surface) == GuideXosResult.Success) &&
            Check(disabledUnselected.Render(surface) == GuideXosResult.Success) &&
            Check(disabledSelected.Render(surface) == GuideXosResult.Success);
        return rendered;
    }

    private static bool ResetAndIndependence()
    {
        GuideXosRadioButton first =
            new(10, 20, 160, 28, "First");
        GuideXosRadioButton second =
            new(180, 20, 160, 28, "Second");
        first.TrySelect();
        second.Focus();
        second.SetLabel(string.Empty);
        bool rejectedStable = Check(first.Selected && !second.Selected &&
            second.Label == "Second" && second.IsFocused);
        first.Reset();
        bool reset = Check(!first.Selected && first.Enabled &&
            !first.IsFocused && first.RejectedInputCount == 0u);
        second.TrySelect();
        bool independent = Check(!first.Selected && second.Selected);
        return rejectedStable && reset && independent;
    }
}
