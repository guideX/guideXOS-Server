using System;

namespace HostLogProof;

public enum GuideXosMessageBoxButtons
{
    OK = 0,
    OKCancel = 1,
    YesNo = 2,
    YesNoCancel = 3,
}

/// <summary>
/// Bounded button-set convenience configuration for GuideXosDialog. The
/// caller supplies and retains the three application-owned Button controls.
/// </summary>
public static class GuideXosMessageBox
{
    private const int ButtonHeight = 18;
    private const int ButtonGap = 8;

    public static bool TryConfigure(
        GuideXosDialog dialog,
        GuideXosMessageBoxButtons buttonSet,
        string title,
        string message,
        GuideXosButton firstButton,
        GuideXosButton secondButton,
        GuideXosButton thirdButton,
        GuideXosDialogResult defaultResult,
        GuideXosDialogResult cancelResult = GuideXosDialogResult.None)
    {
        if (dialog == null || dialog.IsOpen || title == null || message == null ||
            !GuideXosDialog.IsValidAscii(title, GuideXosDialog.MaximumTitleLength, false) ||
            !GuideXosDialog.IsValidMessage(message,
                Math.Min(56, (dialog.Width - 24) / 8)))
        {
            return false;
        }

        int count = buttonSet switch
        {
            GuideXosMessageBoxButtons.OK => 1,
            GuideXosMessageBoxButtons.OKCancel => 2,
            GuideXosMessageBoxButtons.YesNo => 2,
            GuideXosMessageBoxButtons.YesNoCancel => 3,
            _ => 0,
        };
        if (count == 0 || dialog.Width < 320 ||
            !IsButtonUsable(firstButton) ||
            (count >= 2 && !IsButtonUsable(secondButton)) ||
            (count >= 3 && !IsButtonUsable(thirdButton)) ||
            (count < 2 && secondButton != null) ||
            (count < 3 && thirdButton != null) ||
            (count >= 2 && ReferenceEquals(firstButton, secondButton)) ||
            (count >= 3 && (ReferenceEquals(firstButton, thirdButton) ||
                ReferenceEquals(secondButton, thirdButton))) ||
            !IsAvailableResult(buttonSet, defaultResult))
        {
            return false;
        }

        GuideXosDialogResult effectiveCancel = cancelResult == GuideXosDialogResult.None
            ? DefaultCancelResult(buttonSet) : cancelResult;
        if (!IsAvailableResult(buttonSet, effectiveCancel)) return false;

        string label1 = buttonSet is GuideXosMessageBoxButtons.OK or
            GuideXosMessageBoxButtons.OKCancel ? "OK" : "Yes";
        GuideXosDialogResult result1 = buttonSet is GuideXosMessageBoxButtons.OK or
            GuideXosMessageBoxButtons.OKCancel
                ? GuideXosDialogResult.OK : GuideXosDialogResult.Yes;
        string label2 = buttonSet == GuideXosMessageBoxButtons.OKCancel
            ? "Cancel" : "No";
        GuideXosDialogResult result2 = buttonSet == GuideXosMessageBoxButtons.OKCancel
            ? GuideXosDialogResult.Cancel : GuideXosDialogResult.No;
        string label3 = "Cancel";
        GuideXosDialogResult result3 = GuideXosDialogResult.Cancel;

        if (firstButton.MaximumLabelLength < label1.Length ||
            (count >= 2 && secondButton.MaximumLabelLength < label2.Length) ||
            (count >= 3 && thirdButton.MaximumLabelLength < label3.Length))
        {
            return false;
        }

        int available = dialog.Width - 24 - ButtonGap * (count - 1);
        int buttonWidth = available / count;
        if (buttonWidth > 96) buttonWidth = 96;
        if (buttonWidth < 64) return false;
        int totalWidth = count * buttonWidth + (count - 1) * ButtonGap;
        int startX = dialog.X + (dialog.Width - totalWidth) / 2;
        int buttonY = dialog.Y + dialog.Height - 30;

        if (!dialog.TrySetTitle(title) || !dialog.TrySetMessage(message) ||
            !dialog.TryClearMembers())
        {
            return false;
        }

        if (!ConfigureButton(dialog, firstButton, label1, result1,
                startX, buttonY, buttonWidth) ||
            (count >= 2 && !ConfigureButton(dialog, secondButton, label2, result2,
                startX + buttonWidth + ButtonGap, buttonY, buttonWidth)) ||
            (count >= 3 && !ConfigureButton(dialog, thirdButton, label3, result3,
                startX + 2 * (buttonWidth + ButtonGap), buttonY, buttonWidth)))
        {
            dialog.TryClearMembers();
            return false;
        }

        GuideXosButton defaultButton = ResultButton(buttonSet, defaultResult,
            firstButton, secondButton, thirdButton);
        return dialog.TrySetDefaultButton(defaultButton) &&
            dialog.TrySetCancelResult(effectiveCancel);
    }

    private static bool ConfigureButton(GuideXosDialog dialog,
        GuideXosButton button, string label, GuideXosDialogResult result,
        int x, int y, int width)
    {
        return button.TrySetBounds(x, y, width, ButtonHeight) &&
            button.SetLabel(label) && dialog.TryAddMember(button) &&
            dialog.TrySetButtonResult(button, result);
    }

    private static bool IsButtonUsable(GuideXosButton button) => button != null &&
        button.EffectiveEnabled && button.EffectiveVisible &&
        button.ParentPanel == null && button.ParentScrollView == null &&
        button.ParentGroupBox == null && button.VerticalStackOwner == null;

    private static bool IsAvailableResult(GuideXosMessageBoxButtons set,
        GuideXosDialogResult result) => set switch
    {
        GuideXosMessageBoxButtons.OK => result == GuideXosDialogResult.OK,
        GuideXosMessageBoxButtons.OKCancel =>
            result is GuideXosDialogResult.OK or GuideXosDialogResult.Cancel,
        GuideXosMessageBoxButtons.YesNo =>
            result is GuideXosDialogResult.Yes or GuideXosDialogResult.No,
        GuideXosMessageBoxButtons.YesNoCancel =>
            result is GuideXosDialogResult.Yes or GuideXosDialogResult.No or
                GuideXosDialogResult.Cancel,
        _ => false,
    };

    private static GuideXosDialogResult DefaultCancelResult(
        GuideXosMessageBoxButtons set) => set switch
    {
        GuideXosMessageBoxButtons.OK => GuideXosDialogResult.OK,
        GuideXosMessageBoxButtons.YesNo => GuideXosDialogResult.No,
        _ => GuideXosDialogResult.Cancel,
    };

    private static GuideXosButton ResultButton(
        GuideXosMessageBoxButtons set,
        GuideXosDialogResult result,
        GuideXosButton first,
        GuideXosButton second,
        GuideXosButton third)
    {
        if (result == (set is GuideXosMessageBoxButtons.OK or
                GuideXosMessageBoxButtons.OKCancel
                ? GuideXosDialogResult.OK : GuideXosDialogResult.Yes)) return first;
        if (result == (set == GuideXosMessageBoxButtons.OKCancel
                ? GuideXosDialogResult.Cancel : GuideXosDialogResult.No)) return second;
        return third;
    }
}
