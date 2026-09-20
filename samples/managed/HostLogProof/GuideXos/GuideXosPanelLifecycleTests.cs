using System;

namespace HostLogProof;

/// <summary>
/// Regression probes for the existing Panel/ControlHost lifecycle boundary.
/// The tests use the real KeyDown/KeyChar split; no synthetic pointer-up or
/// private control state is introduced.
/// </summary>
public static class GuideXosPanelLifecycleTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool result = PanelVisibilityCancelsPendingSpace() &&
            MembershipCancellationAndReuse() &&
            ChildStateCancellation() &&
            ModalCancellation() &&
            FocusRecoveryAndTraversal() &&
            StandaloneRemovalAndReattach();
        if (host != null)
        {
            Span<byte> line = stackalloc byte[128];
            int position = 0;
            GuideXosText.Append(line, ref position,
                "C128-PANEL-LIFECYCLE-TESTS cases="u8);
            GuideXosText.AppendUnsigned(line, ref position, (uint)s_caseCount);
            GuideXosText.Append(line, ref position,
                result ? " result=PASS"u8 : " result=FAIL"u8);
            host.TryLog(line[..position]);
        }
        return result;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool PanelVisibilityCancelsPendingSpace()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosCheckBox checkBox = new(10, 20, 128, 28, "Panel choice");
        GuideXosButton fallback = new(10, 220, 80, 24, "Fallback");
        panel.TryAddChild(checkBox, 8, 14);
        GuideXosControlHost host = new(2);
        host.TryRegisterCheckBox(1, checkBox);
        host.TryRegisterButton(2, fallback);
        bool focused = Check(host.TryFocus(1) ==
            GuideXosControlHostResult.Focused && checkBox.IsFocused);
        bool began = Check(host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked);

        panel.SetVisible(false);
        bool hidden = Check(!checkBox.EffectiveVisible && !checkBox.IsFocused);
        panel.SetVisible(true);
        bool shownNoSteal = Check(checkBox.EffectiveVisible &&
            !checkBox.IsFocused && host.ActiveControlId == 1);
        bool cancelled = Check(host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked &&
            host.ActiveControlId == 2 && fallback.IsFocused);

        bool ordinary = Check(host.TryFocus(1) ==
            GuideXosControlHostResult.Focused &&
            host.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosControlHostResult.Ignored &&
            host.HandleCharacter(' ') == GuideXosControlHostResult.Toggled &&
            checkBox.Checked);
        return focused && began && hidden && shownNoSteal && cancelled &&
            ordinary;
    }

    private static bool MembershipCancellationAndReuse()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosCheckBox checkBox = new(10, 20, 128, 28, "Reusable");
        GuideXosButton fallback = new(10, 220, 80, 24, "Fallback");
        panel.TryAddChild(checkBox, 8, 14);
        GuideXosControlHost host = new(2);
        host.TryRegisterCheckBox(1, checkBox);
        host.TryRegisterButton(2, fallback);
        host.TryFocus(1);
        bool began = Check(host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored);
        bool removed = Check(panel.TryRemoveChild(checkBox) ==
            GuideXosPanelResult.Removed && checkBox.ParentPanel == null &&
            checkBox.EffectiveVisible && panel.ChildCount == 0);
        bool staleAfterRemove = Check(host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked &&
            fallback.IsFocused && host.RegistrationCount == 2);

        bool reattached = Check(panel.TryAddChild(checkBox, 8, 14) ==
            GuideXosPanelResult.Added && checkBox.ParentPanel == panel);
        host.TryFocus(1);
        bool beganClear = Check(host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored);
        bool cleared = Check(panel.Clear() == GuideXosPanelResult.Cleared &&
            checkBox.ParentPanel == null && checkBox.EffectiveVisible &&
            panel.ChildCount == 0);
        bool staleAfterClear = Check(host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !checkBox.Checked);

        bool reused = Check(panel.TryAddChild(checkBox, 16, 18) ==
            GuideXosPanelResult.Added && checkBox.ParentPanel == panel);
        bool ordinary = Check(host.TryFocus(1) ==
            GuideXosControlHostResult.Focused &&
            host.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosControlHostResult.Ignored &&
            host.HandleCharacter(' ') == GuideXosControlHostResult.Toggled &&
            checkBox.Checked);
        return began && removed && staleAfterRemove && reattached &&
            beganClear && cleared && staleAfterClear && reused && ordinary;
    }

    private static bool ChildStateCancellation()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosRadioButton radio = new(10, 20, 128, 28, "Radio");
        GuideXosButton fallback = new(10, 220, 80, 24, "Fallback");
        panel.TryAddChild(radio, 8, 14);
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(1, radio);
        host.TryRegisterButton(2, fallback);
        host.TryFocus(1);
        bool beganHidden = Check(host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored);
        radio.SetVisible(false);
        bool hiddenRecovery = Check(host.RefreshVisibility() ==
            GuideXosControlHostResult.Focused && host.ActiveControlId == 2 &&
            !radio.IsFocused);
        radio.SetVisible(true);
        bool shownNoSteal = Check(host.RefreshVisibility() ==
            GuideXosControlHostResult.Ignored && host.ActiveControlId == 2 &&
            !radio.IsFocused);
        bool hiddenCancelled = Check(host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !radio.Selected);

        host.TryFocus(1);
        bool beganDisabled = Check(host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored);
        radio.SetEnabled(false);
        bool disabledRecovery = Check(host.RefreshVisibility() ==
            GuideXosControlHostResult.Focused && host.ActiveControlId == 2 &&
            !radio.IsFocused);
        radio.SetEnabled(true);
        bool enabledNoSteal = Check(host.ActiveControlId == 2 &&
            !radio.IsFocused);
        bool disabledCancelled = Check(host.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !radio.Selected);

        bool ordinary = Check(host.TryFocus(1) ==
            GuideXosControlHostResult.Focused &&
            host.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosControlHostResult.Ignored &&
            host.HandleCharacter(' ') == GuideXosControlHostResult.Changed &&
            radio.Selected);
        return beganHidden && hiddenRecovery && shownNoSteal &&
            hiddenCancelled && beganDisabled && disabledRecovery &&
            enabledNoSteal && disabledCancelled && ordinary;
    }

    private static bool ModalCancellation()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosRadioButton radio = new(10, 20, 128, 28, "Background");
        panel.TryAddChild(radio, 8, 14);
        GuideXosControlHost main = new(1);
        main.TryRegisterRadioButton(1, radio);
        main.TryFocus(1);
        bool began = Check(main.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored);

        GuideXosControlHost modal = new(1);
        GuideXosButton modalButton = new(10, 10, 80, 24, "Modal");
        modal.TryRegisterButton(9, modalButton);
        bool entered = Check(main.EnterModal(modal) && main.IsModalActive &&
            main.ActiveControlId == 0 && !radio.IsFocused);
        bool modalInput = Check(modal.TryFocus(9) ==
            GuideXosControlHostResult.Focused &&
            modal.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosControlHostResult.Ignored &&
            modal.HandleCharacter(' ') == GuideXosControlHostResult.Activated &&
            !radio.Selected);
        bool staleDuringModal = Check(main.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !radio.Selected);
        bool exited = Check(main.ExitModal() && !main.IsModalActive &&
            main.ActiveControlId == 1 && radio.IsFocused);
        bool ordinary = Check(main.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Ignored &&
            main.HandleCharacter(' ') == GuideXosControlHostResult.Changed &&
            radio.Selected);
        return began && entered && modalInput && staleDuringModal && exited &&
            ordinary;
    }

    private static bool FocusRecoveryAndTraversal()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 2);
        GuideXosRadioButton first = new(10, 20, 128, 28, "First");
        GuideXosRadioButton second = new(10, 20, 128, 28, "Second");
        panel.TryAddChild(first, 8, 14);
        panel.TryAddChild(second, 160, 14);
        GuideXosControlHost host = new(2);
        host.TryRegisterRadioButton(1, first);
        host.TryRegisterRadioButton(2, second);
        host.TryFocus(1);
        panel.SetVisible(false);
        bool noEligible = Check(host.RefreshVisibility() ==
            GuideXosControlHostResult.Ignored && host.ActiveIndex == -1 &&
            !first.IsFocused && !second.IsFocused);
        panel.SetVisible(true);
        bool showNoSteal = Check(host.RefreshVisibility() ==
            GuideXosControlHostResult.Ignored && host.ActiveIndex == -1);
        bool tabFirst = Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 1);
        bool tabSecond = Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 2);
        bool reverseFirst = Check(host.HandleKey(
            GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 1);

        second.SetVisible(false);
        bool localSkip = Check(host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 1);
        second.SetVisible(true);
        bool localRestoreNoSteal = Check(host.RefreshVisibility() ==
            GuideXosControlHostResult.Ignored && host.ActiveControlId == 1 &&
            !second.IsFocused);
        bool restoredTraversal = Check(host.HandleKey(
            GuideXosTextInputKey.Tab) == GuideXosControlHostResult.Traversed &&
            host.ActiveControlId == 2 && second.IsFocused);
        return noEligible && showNoSteal && tabFirst && tabSecond &&
            reverseFirst && localSkip && localRestoreNoSteal &&
            restoredTraversal;
    }

    private static bool StandaloneRemovalAndReattach()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosRadioButton radio = new(10, 20, 128, 28, "Reusable");
        GuideXosControlHost host = new(1);
        host.TryRegisterRadioButton(1, radio);
        bool added = Check(panel.TryAddChild(radio, 8, 14) ==
            GuideXosPanelResult.Added && radio.X == 108 && radio.Y == 114);
        panel.SetVisible(false);
        bool hidden = Check(host.RefreshVisibility() ==
            GuideXosControlHostResult.Ignored && host.ActiveIndex == -1 &&
            !radio.EffectiveVisible);
        bool removed = Check(panel.TryRemoveChild(radio) ==
            GuideXosPanelResult.Removed && radio.ParentPanel == null &&
            radio.EffectiveVisible && radio.X == 108 && radio.Y == 114 &&
            host.RegistrationCount == 1);
        bool standalone = Check(radio.TrySetBounds(240, 240, 128, 28) &&
            host.TryFocus(1) == GuideXosControlHostResult.Focused &&
            radio.IsFocused);

        bool reattached = Check(panel.TryAddChild(radio, 16, 18) ==
            GuideXosPanelResult.Added && radio.ParentPanel == panel &&
            radio.X == 116 && radio.Y == 118 && !radio.EffectiveVisible &&
            !radio.IsFocused);
        bool reattachRecovery = Check(host.RefreshVisibility() ==
            GuideXosControlHostResult.Ignored && host.ActiveIndex == -1);
        panel.SetVisible(true);
        bool shownNoSteal = Check(radio.EffectiveVisible &&
            host.ActiveIndex == -1 && !radio.IsFocused);
        bool eligibleAgain = Check(host.TryFocus(1) ==
            GuideXosControlHostResult.Focused && radio.IsFocused);
        return added && hidden && removed && standalone && reattached &&
            reattachRecovery && shownNoSteal && eligibleAgain;
    }
}
