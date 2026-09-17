namespace HostLogProof;

/// <summary>Deterministic focused probes for the reusable managed button.</summary>
public static class GuideXosButtonTests
{
    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        bool construction = ConstructionAndLabels();
        bool enabled = EnabledState();
        bool focus = FocusState();
        bool pointer = PointerBoundsAndDisabledState();
        bool keyboard = KeyboardActivation();
        bool lifecycle = LifecycleAndIndependence();
        bool rendering = Rendering(surface);
        bool result = construction && enabled && focus && pointer && keyboard &&
            lifecycle && rendering;

        if (host != null)
        {
            if (!construction) host.TryLog("C119-TEST-GROUP construction=FAIL"u8);
            if (!enabled) host.TryLog("C119-TEST-GROUP enabled=FAIL"u8);
            if (!focus) host.TryLog("C119-TEST-GROUP focus=FAIL"u8);
            if (!pointer) host.TryLog("C119-TEST-GROUP pointer=FAIL"u8);
            if (!keyboard) host.TryLog("C119-TEST-GROUP keyboard=FAIL"u8);
            if (!lifecycle) host.TryLog("C119-TEST-GROUP lifecycle=FAIL"u8);
            if (!rendering) host.TryLog("C119-TEST-GROUP rendering=FAIL"u8);
            host.TryLog(result
                ? "C119-TESTS cases=36 construct=PASS enabled=PASS focus=PASS pointer=PASS keyboard=PASS life=PASS render=PASS result=PASS"u8
                : "C119-TESTS cases=36 result=FAIL"u8);
        }
        return result;
    }

    private static bool ConstructionAndLabels()
    {
        GuideXosButton button = new(10, 20, 96, 28, "Open");
        bool construction = button.X == 10 && button.Y == 20 &&
            button.Width == 96 && button.Height == 28 &&
            button.Label == "Open" && button.Enabled && !button.IsFocused;

        bool validLabel = button.SetLabel("Save As") && button.Label == "Save As";
        bool emptyRejected = !button.SetLabel(string.Empty) &&
            button.Label == "Save As";
        string maximum = new('A', button.MaximumLabelLength);
        bool maximumAccepted = button.SetLabel(maximum) && button.Label == maximum;
        string overlong = new('B', button.MaximumLabelLength + 1);
        bool overlongRejected = !button.SetLabel(overlong) &&
            button.Label == maximum;
        return construction && validLabel && emptyRejected && maximumAccepted &&
            overlongRejected;
    }

    private static bool EnabledState()
    {
        GuideXosButton button = new(10, 20, 96, 28, "Open");
        bool defaultEnabled = button.Enabled;
        button.Focus();
        button.SetEnabled(false);
        bool disabled = !button.Enabled && !button.IsFocused;
        button.SetEnabled(true);
        bool reenabled = button.Enabled && !button.IsFocused;
        return defaultEnabled && disabled && reenabled;
    }

    private static bool FocusState()
    {
        GuideXosButton button = new(10, 20, 96, 28, "Open");
        button.Focus();
        bool focused = button.IsFocused;
        bool focusIsNotActivation = button.HandleKey(GuideXosTextInputKey.Up) ==
            GuideXosButtonResult.Ignored && button.IsFocused;
        button.Blur();
        bool blurred = !button.IsFocused;
        bool unfocusedEnter = button.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosButtonResult.Ignored;
        return focused && focusIsNotActivation && blurred && unfocusedEnter;
    }

    private static bool PointerBoundsAndDisabledState()
    {
        GuideXosButton outside = new(10, 20, 96, 28, "Open");
        bool pointerOutside = outside.HandlePointerDown(9, 20) ==
            GuideXosButtonResult.Ignored && !outside.IsFocused;

        GuideXosButton edges = new(10, 20, 96, 28, "Open");
        bool left = edges.HandlePointerDown(10, 25) ==
            GuideXosButtonResult.Activated;
        edges.Blur();
        bool right = edges.HandlePointerDown(105, 25) ==
            GuideXosButtonResult.Activated;
        edges.Blur();
        bool rightOutside = edges.HandlePointerDown(106, 25) ==
            GuideXosButtonResult.Ignored && !edges.IsFocused;
        bool top = edges.HandlePointerDown(25, 20) ==
            GuideXosButtonResult.Activated;
        edges.Blur();
        bool bottom = edges.HandlePointerDown(25, 47) ==
            GuideXosButtonResult.Activated;
        edges.Blur();
        bool bottomOutside = edges.HandlePointerDown(25, 48) ==
            GuideXosButtonResult.Ignored && !edges.IsFocused;

        GuideXosButton disabled = new(10, 20, 96, 28, "Open");
        disabled.Focus();
        disabled.SetEnabled(false);
        bool disabledPointer = disabled.HandlePointerDown(10, 20) ==
            GuideXosButtonResult.Disabled && !disabled.IsFocused;

        bool safeInitial = disabled.TrySetBounds(4063, 4063, 32, 32);
        int oldX = disabled.X;
        bool overflowRejected = !disabled.TrySetBounds(4090, 4090, 32, 18) &&
            disabled.X == oldX;
        bool extremeIgnored = disabled.HandlePointerDown(int.MaxValue, int.MaxValue) ==
            GuideXosButtonResult.Ignored;
        return pointerOutside && left && right && rightOutside && top && bottom &&
            bottomOutside && disabledPointer && safeInitial && overflowRejected &&
            extremeIgnored;
    }

    private static bool KeyboardActivation()
    {
        GuideXosButton enter = new(10, 20, 96, 28, "Open");
        enter.Focus();
        bool focusedEnter = enter.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosButtonResult.Activated;
        enter.Blur();
        bool unfocusedEnter = enter.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosButtonResult.Ignored;
        enter.Focus();
        enter.SetEnabled(false);
        bool disabledEnter = enter.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosButtonResult.Disabled;

        GuideXosButton space = new(10, 20, 96, 28, "Open");
        space.Focus();
        bool focusedSpace = space.HandleCharacter(' ') ==
            GuideXosButtonResult.Activated;
        space.Blur();
        bool unfocusedSpace = space.HandleCharacter(' ') ==
            GuideXosButtonResult.Ignored;
        space.Focus();
        space.SetEnabled(false);
        bool disabledSpace = space.HandleCharacter(' ') ==
            GuideXosButtonResult.Disabled;

        GuideXosButton exactlyOnce = new(10, 20, 96, 28, "Open");
        exactlyOnce.Focus();
        int activations = 0;
        bool keyDownSpaceIgnored = exactlyOnce.HandleKey(
            (GuideXosTextInputKey)' ') == GuideXosButtonResult.Ignored;
        if (exactlyOnce.HandleCharacter(' ') == GuideXosButtonResult.Activated)
        {
            ++activations;
        }
        bool exactlyOneSpace = keyDownSpaceIgnored && activations == 1;

        string before = exactlyOnce.Label;
        bool unrelated = exactlyOnce.HandleCharacter('A') ==
                GuideXosButtonResult.Ignored &&
            exactlyOnce.HandleKey(GuideXosTextInputKey.Up) ==
                GuideXosButtonResult.Ignored && exactlyOnce.Label == before;
        return focusedEnter && unfocusedEnter && disabledEnter && focusedSpace &&
            unfocusedSpace && disabledSpace && exactlyOneSpace && unrelated;
    }

    private static bool LifecycleAndIndependence()
    {
        GuideXosButton button = new(10, 20, 96, 28, "Open");
        button.Focus();
        bool repeated = button.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosButtonResult.Activated &&
            button.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosButtonResult.Activated &&
                button.HandleCharacter(' ') == GuideXosButtonResult.Activated &&
            button.HandleCharacter(' ') == GuideXosButtonResult.Activated;
        button.SetLabel(string.Empty);
        bool rejectedBeforeReset = button.RejectedInputCount > 0u;
        button.Reset();
        bool reset = button.Enabled && !button.IsFocused &&
            button.Label == "Open" && button.X == 10 && button.Y == 20 &&
            button.Width == 96 && button.Height == 28 &&
            button.RejectedInputCount == 0u && rejectedBeforeReset;

        GuideXosButton first = new(10, 20, 96, 28, "Open");
        GuideXosButton second = new(120, 20, 112, 28, "Save As");
        bool independentValues = first.Label == "Open" &&
            second.Label == "Save As" && first.X != second.X;
        first.Focus();
        bool independentFocus = first.IsFocused && !second.IsFocused;
        first.SetEnabled(false);
        bool independentEnabled = !first.Enabled && second.Enabled;
        second.Focus();
        bool secondActivates = second.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosButtonResult.Activated && !first.IsFocused;
        return repeated && reset && independentValues && independentFocus &&
            independentEnabled && secondActivates;
    }

    private static bool Rendering(GuideXosSurface surface)
    {
        if (surface == null) return false;

        GuideXosButton bounded = new(
            10, 20, GuideXosButton.MinimumSupportedWidth,
            GuideXosButton.MinimumSupportedHeight,
            new string('L', GuideXosButton.MaximumSupportedLabelLength),
            GuideXosButton.MaximumSupportedLabelLength);
        bool boundedRender = bounded.Render(surface) == GuideXosResult.Success;

        GuideXosButton focused = new(120, 20, 96, 28, "Open");
        focused.Focus();
        bool focusedRender = focused.Render(surface) == GuideXosResult.Success;

        GuideXosButton disabled = new(230, 20, 96, 28, "Open");
        disabled.SetEnabled(false);
        bool disabledRender = disabled.Render(surface) == GuideXosResult.Success;
        return boundedRender && focusedRender && disabledRender;
    }
}
