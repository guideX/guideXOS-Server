using System;

namespace HostLogProof;

/// <summary>Focused C131 API, callback, rendering, and Panel probes.</summary>
public static class GuideXosCheckBoxC131Tests
{
    private const int ExpectedCaseCount = 34;
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        bool result = InitialState() &&
            ProgrammaticStateAndCallback() &&
            PointerActivation() &&
            KeyboardActivation() &&
            RenderingStates(surface) &&
            PanelMembership();
        if (host != null)
        {
            Span<byte> line = stackalloc byte[128];
            int position = 0;
            GuideXosText.Append(line, ref position,
                "C131-CHECKBOX-TESTS cases="u8);
            GuideXosText.AppendUnsigned(line, ref position, (uint)s_caseCount);
            GuideXosText.Append(line, ref position,
                result ? " result=PASS"u8 : " result=FAIL"u8);
            host.TryLog(line[..position]);
        }
        return result && s_caseCount == ExpectedCaseCount;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool InitialState()
    {
        GuideXosCheckBox uncheckedBox =
            new(10, 20, 160, 24, "Show status");
        GuideXosCheckBox checkedBox =
            new(10, 50, 160, 24, "Show status", true);
        return Check(!uncheckedBox.Checked && !uncheckedBox.IsFocused) &&
            Check(checkedBox.Checked && checkedBox.Visible && checkedBox.Enabled) &&
            Check(uncheckedBox.Text == "Show status" &&
                uncheckedBox.Label == uncheckedBox.Text) &&
            Check(uncheckedBox.ParentPanel == null);
    }

    private static bool ProgrammaticStateAndCallback()
    {
        GuideXosCheckBox checkBox =
            new(10, 20, 160, 24, "Show status");
        int callbackCount = 0;
        bool callbackValue = false;
        checkBox.Changed = value =>
        {
            ++callbackCount;
            callbackValue = value;
            if (callbackCount == 1) checkBox.Checked = !value;
        };

        checkBox.Checked = true;
        bool firstChange = Check(!checkBox.Checked && callbackCount == 1 &&
            callbackValue);
        checkBox.Checked = false;
        bool sameValue = Check(callbackCount == 1);
        checkBox.SetChecked(true);
        bool secondChange = Check(checkBox.Checked && callbackCount == 2 &&
            callbackValue);
        bool textUpdate = Check(checkBox.TrySetText("Status") &&
            checkBox.Text == "Status");
        checkBox.SetVisible(false);
        bool visibilityProperty = Check(!checkBox.Visible && !checkBox.IsFocused);
        checkBox.Visible = true;
        checkBox.Enabled = false;
        bool enabledProperty = Check(!checkBox.Enabled && !checkBox.IsFocused);
        return firstChange && sameValue && secondChange && textUpdate &&
            visibilityProperty && enabledProperty;
    }

    private static bool PointerActivation()
    {
        GuideXosCheckBox checkBox =
            new(10, 20, 160, 24, "Show status");
        int callbackCount = 0;
        checkBox.Changed = _ => ++callbackCount;
        bool outside = Check(checkBox.HandlePointerDown(9, 20) ==
            GuideXosCheckBoxResult.Ignored && !checkBox.Checked);
        bool labelClick = Check(checkBox.HandlePointerDown(150, 30) ==
            GuideXosCheckBoxResult.Toggled && checkBox.Checked &&
            checkBox.IsFocused && callbackCount == 1);
        bool secondClick = Check(checkBox.HandlePointerDown(10, 20) ==
            GuideXosCheckBoxResult.Toggled && !checkBox.Checked &&
            callbackCount == 2);
        checkBox.Enabled = false;
        bool disabled = Check(checkBox.HandlePointerDown(20, 25) ==
            GuideXosCheckBoxResult.Disabled && !checkBox.Checked &&
            callbackCount == 2);
        checkBox.Enabled = true;
        checkBox.Visible = false;
        bool hidden = Check(checkBox.HandlePointerDown(20, 25) ==
            GuideXosCheckBoxResult.Ignored && !checkBox.Checked &&
            callbackCount == 2);
        return outside && labelClick && secondClick && disabled && hidden;
    }

    private static bool KeyboardActivation()
    {
        GuideXosCheckBox checkBox =
            new(10, 20, 160, 24, "Show status");
        int callbackCount = 0;
        checkBox.Changed = _ => ++callbackCount;
        bool unfocused = Check(checkBox.HandleCharacter(' ') ==
            GuideXosCheckBoxResult.Ignored && !checkBox.Checked);
        checkBox.Focus();
        bool keyDownOnly = Check(checkBox.HandleKey(
            (GuideXosTextInputKey)' ') == GuideXosCheckBoxResult.Ignored &&
            !checkBox.Checked && callbackCount == 0);
        bool keyChar = Check(checkBox.HandleCharacter(' ') ==
            GuideXosCheckBoxResult.Toggled && checkBox.Checked &&
            callbackCount == 1);
        bool noSyntheticTabChar = Check(checkBox.HandleCharacter('\t') ==
            GuideXosCheckBoxResult.Ignored && checkBox.Checked);
        checkBox.Enabled = false;
        bool disabledKeyDown = Check(checkBox.HandleKey(
            (GuideXosTextInputKey)' ') == GuideXosCheckBoxResult.Disabled);
        bool disabledKeyChar = Check(checkBox.HandleCharacter(' ') ==
            GuideXosCheckBoxResult.Disabled && checkBox.Checked &&
            callbackCount == 1);
        return unfocused && keyDownOnly && keyChar && noSyntheticTabChar &&
            disabledKeyDown && disabledKeyChar;
    }

    private static bool RenderingStates(GuideXosSurface surface)
    {
        GuideXosCheckBox uncheckedBox =
            new(10, 20, 160, 24, "Show status");
        GuideXosCheckBox checkedBox =
            new(10, 50, 160, 24, "Show status", true);
        GuideXosCheckBox focusedBox =
            new(10, 80, 160, 24, "Show status");
        GuideXosCheckBox disabledBox =
            new(10, 110, 160, 24, "Show status", true);
        focusedBox.Focus();
        disabledBox.Enabled = false;
        bool validSurface = Check(surface != null);
        bool uncheckedState = Check(uncheckedBox.Render(surface) ==
            GuideXosResult.Success);
        bool checkedState = Check(checkedBox.Render(surface) ==
            GuideXosResult.Success);
        bool focusedState = Check(focusedBox.Render(surface) ==
            GuideXosResult.Success && focusedBox.IsFocused);
        bool disabledState = Check(disabledBox.Render(surface) ==
            GuideXosResult.Success && !disabledBox.Enabled);
        bool hiddenState = Check((uncheckedBox.Visible = false) == false &&
            uncheckedBox.Render(surface) == GuideXosResult.Success);
        return validSurface && uncheckedState && checkedState && focusedState &&
            disabledState && hiddenState;
    }

    private static bool PanelMembership()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosCheckBox checkBox =
            new(10, 20, 128, 24, "Show status");
        bool added = Check(panel.TryAddChild(checkBox, 8, 14) ==
            GuideXosPanelResult.Added && checkBox.ParentPanel == panel);
        bool positioned = Check(checkBox.X == 108 && checkBox.Y == 114 &&
            panel.ContainsChild(checkBox));
        panel.SetVisible(false);
        bool hidden = Check(!checkBox.EffectiveVisible && !checkBox.IsFocused);
        panel.SetVisible(true);
        bool shown = Check(checkBox.EffectiveVisible && !checkBox.IsFocused);
        bool moved = Check(panel.TrySetChildPosition(checkBox, 16, 18) ==
            GuideXosPanelResult.Moved && checkBox.X == 116 && checkBox.Y == 118);
        bool removed = Check(panel.TryRemoveChild(checkBox) ==
            GuideXosPanelResult.Removed && checkBox.ParentPanel == null &&
            checkBox.EffectiveVisible);
        bool standalone = Check(checkBox.TrySetBounds(240, 240, 128, 24) &&
            checkBox.X == 240 && checkBox.Y == 240);
        return added && positioned && hidden && shown && moved && removed &&
            standalone;
    }
}
