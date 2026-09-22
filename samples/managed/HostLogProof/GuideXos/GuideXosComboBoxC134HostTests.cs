#if HOSTLOGPROOF_C134_HOST_TESTS
namespace HostLogProof;

/// <summary>Focused C134 coverage for the host's single transient-input lease.</summary>
public static class GuideXosComboBoxC134HostTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool result = true;
        result &= Check(ClosedHasNoCapture());
        result &= Check(OpeningAcquiresCapture());
        result &= Check(OwnerIsDeterministic());
        result &= Check(PointerItemReachesOwner());
        result &= Check(PointerCommitReleasesCapture());
        result &= Check(OutsideReachesOwner());
        result &= Check(OutsideClosesPopup());
        result &= Check(OutsideIsConsumed());
        result &= Check(UnderlyingDoesNotActivate());
        result &= Check(DownReachesOwner());
        result &= Check(UpReachesOwner());
        result &= Check(EnterCommits());
        result &= Check(EscapeCancels());
        result &= Check(TabClosesAndTraverses());
        result &= Check(ShiftTabClosesAndTraverses());
        result &= Check(HideCancelsCapture());
        result &= Check(DisableCancelsCapture());
        result &= Check(MembershipLossCancelsCapture());
        result &= Check(ModalTransitionCancelsCapture());
        result &= Check(UnregisterCancelsCapture());
        result &= Check(ApplicationCloseCancelsCapture());
        result &= Check(StalePointerReleaseIgnored());
        result &= Check(StaleKeyboardCompletionIgnored());
        result &= Check(DeadOwnerCannotRemain());
        result &= Check(NormalRoutingResumes());
        result &= Check(DisabledCannotAcquire());
        result &= Check(SecondOwnerRejected());
        result &= Check(CallbackMutationReleases());
        result &= Check(ProgrammaticSelectionReleases());
        result &= Check(RepeatedCyclesDoNotLeak());

        bool count = s_caseCount == 30;
        host?.TryLog(result && count
            ? "C134-TRANSIENT-HOST-TESTS cases=30 result=PASS"u8
            : "C134-TRANSIENT-HOST-TESTS cases=30 result=FAIL"u8);
        return result && count;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static GuideXosComboBox NewCombo()
    {
        GuideXosComboBox combo = new(10, 20, 160, 28, 4, 8, 4);
        combo.TryAddItem("A");
        combo.TryAddItem("B");
        combo.TryAddItem("C");
        return combo;
    }

    private static GuideXosControlHost NewHost(
        GuideXosComboBox combo, GuideXosButton button = null)
    {
        GuideXosControlHost host = new(2);
        host.TryRegisterComboBox(1, combo);
        if (button != null) host.TryRegisterButton(2, button);
        return host;
    }

    private static bool ClosedHasNoCapture()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        return !combo.IsOpen && !host.HasTransientInputCapture &&
            host.TransientInputCaptureOwnerId == 0;
    }

    private static bool OpeningAcquiresCapture()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        bool opened = host.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Activated;
        return opened && combo.IsOpen && host.HasTransientInputCapture;
    }

    private static bool OwnerIsDeterministic()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        return host.TransientInputCaptureOwnerId == 1 &&
            host.TransientInputCaptureKind == GuideXosManagedControlKind.ComboBox;
    }

    private static bool PointerItemReachesOwner()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.FocusAndRoutePointer(1, 10, 20);
        return host.FocusAndRoutePointer(1, 10, 66) ==
            GuideXosControlHostResult.Changed && combo.SelectedIndex == 1;
    }

    private static bool PointerCommitReleasesCapture()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.FocusAndRoutePointer(1, 10, 20);
        host.FocusAndRoutePointer(1, 10, 66);
        return !combo.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool OutsideReachesOwner()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.FocusAndRoutePointer(1, 10, 20);
        return host.FocusAndRoutePointer(1, 400, 400) ==
            GuideXosControlHostResult.Cancelled;
    }

    private static bool OutsideClosesPopup()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.FocusAndRoutePointer(1, 10, 20);
        host.FocusAndRoutePointer(1, 400, 400);
        return !combo.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool OutsideIsConsumed()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.FocusAndRoutePointer(1, 10, 20);
        return host.FocusAndRoutePointer(1, 400, 400) ==
            GuideXosControlHostResult.Cancelled && host.ActiveControlId == 1;
    }

    private static bool UnderlyingDoesNotActivate()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(combo, button);
        host.FocusAndRoutePointer(1, 10, 20);
        host.FocusAndRoutePointer(2, 220, 20);
        return host.ActiveControlId == 1 && !combo.IsOpen;
    }

    private static bool DownReachesOwner()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        return host.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosControlHostResult.Moved && combo.ActiveIndex == 1;
    }

    private static bool UpReachesOwner()
    {
        GuideXosComboBox combo = NewCombo();
        combo.SelectedIndex = 2;
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        return host.HandleKey(GuideXosTextInputKey.Up) ==
            GuideXosControlHostResult.Moved && combo.ActiveIndex == 1;
    }

    private static bool EnterCommits()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        host.HandleKey(GuideXosTextInputKey.Down);
        return host.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Changed && combo.SelectedIndex == 1 &&
            !host.HasTransientInputCapture;
    }

    private static bool EscapeCancels()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        host.HandleKey(GuideXosTextInputKey.Down);
        return host.HandleKey(GuideXosTextInputKey.Escape) ==
            GuideXosControlHostResult.Cancelled && combo.SelectedIndex == -1 &&
            !host.HasTransientInputCapture;
    }

    private static bool TabClosesAndTraverses()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo,
            new GuideXosButton(220, 20, 80, 28, "Next"));
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        return host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 2 &&
            !combo.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool ShiftTabClosesAndTraverses()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo,
            new GuideXosButton(220, 20, 80, 28, "Next"));
        host.TryFocus(2);
        host.HandleKey(GuideXosTextInputKey.Tab, true);
        host.HandleKey(GuideXosTextInputKey.Enter);
        return host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && host.ActiveControlId == 2 &&
            !combo.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool HideCancelsCapture()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        combo.SetVisible(false);
        host.RefreshVisibility();
        return !combo.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool DisableCancelsCapture()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        combo.SetEnabled(false);
        host.RefreshVisibility();
        return !combo.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool MembershipLossCancelsCapture()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosComboBox combo = NewCombo();
        panel.TryAddChild(combo, 8, 14);
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        panel.TryRemoveChild(combo);
        host.RefreshVisibility();
        return !combo.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool ModalTransitionCancelsCapture()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        GuideXosControlHost modal = new(1);
        modal.TryRegisterButton(9, new GuideXosButton(220, 20, 80, 28, "Modal"));
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        bool entered = host.EnterModal(modal);
        return entered && !combo.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool UnregisterCancelsCapture()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        bool removed = host.TryUnregister(1) == GuideXosControlHostResult.Unregistered;
        return removed && !combo.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool ApplicationCloseCancelsCapture()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        host.Reset();
        return !combo.IsOpen && !host.HasTransientInputCapture &&
            host.RegistrationCount == 0;
    }

    private static bool StalePointerReleaseIgnored()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.FocusAndRoutePointer(1, 10, 20);
        combo.SetVisible(false);
        host.RefreshVisibility();
        combo.SetVisible(true);
        host.RefreshVisibility();
        return host.FocusAndRoutePointer(1, 10, 66) ==
            GuideXosControlHostResult.Ignored && combo.SelectedIndex == -1 &&
            !host.HasTransientInputCapture;
    }

    private static bool StaleKeyboardCompletionIgnored()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        combo.SetVisible(false);
        host.RefreshVisibility();
        combo.SetVisible(true);
        host.RefreshVisibility();
        return host.HandleCharacter(' ') == GuideXosControlHostResult.Ignored &&
            combo.SelectedIndex == -1 && !host.HasTransientInputCapture;
    }

    private static bool DeadOwnerCannotRemain()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        host.TryUnregister(1);
        return host.TransientInputCaptureOwnerId == 0 &&
            host.TransientInputCaptureKind == GuideXosManagedControlKind.None;
    }

    private static bool NormalRoutingResumes()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo,
            new GuideXosButton(220, 20, 80, 28, "Next"));
        host.FocusAndRoutePointer(1, 10, 20);
        host.FocusAndRoutePointer(2, 220, 20);
        return host.FocusAndRoutePointer(2, 220, 20) ==
            GuideXosControlHostResult.Activated && host.ActiveControlId == 2;
    }

    private static bool DisabledCannotAcquire()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        combo.Open();
        combo.SetEnabled(false);
        return !host.TryAcquireTransientInputCapture(1) &&
            !host.HasTransientInputCapture;
    }

    private static bool SecondOwnerRejected()
    {
        GuideXosComboBox first = NewCombo();
        GuideXosComboBox second = new(10, 80, 160, 28, 4, 8, 4);
        second.TryAddItem("A");
        second.TryAddItem("B");
        GuideXosControlHost host = NewHost(first);
        host.TryRegisterComboBox(2, second);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        second.Focus();
        second.Open();
        return !host.TryAcquireTransientInputCapture(2) &&
            host.TransientInputCaptureOwnerId == 1;
    }

    private static bool CallbackMutationReleases()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        combo.Changed = _ => combo.SetVisible(false);
        host.FocusAndRoutePointer(1, 10, 20);
        host.FocusAndRoutePointer(1, 10, 66);
        return combo.SelectedIndex == 1 && !combo.IsOpen &&
            !host.HasTransientInputCapture;
    }

    private static bool ProgrammaticSelectionReleases()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        host.TryFocus(1);
        host.HandleKey(GuideXosTextInputKey.Enter);
        combo.SelectedIndex = 2;
        return combo.SelectedIndex == 2 && !combo.IsOpen &&
            !host.HasTransientInputCapture;
    }

    private static bool RepeatedCyclesDoNotLeak()
    {
        GuideXosComboBox combo = NewCombo();
        GuideXosControlHost host = NewHost(combo);
        for (int cycle = 0; cycle < 5; cycle++)
        {
            host.TryFocus(1);
            if (host.HandleKey(GuideXosTextInputKey.Enter) !=
                    GuideXosControlHostResult.Activated ||
                !host.HasTransientInputCapture ||
                host.HandleKey(GuideXosTextInputKey.Escape) !=
                    GuideXosControlHostResult.Cancelled ||
                host.HasTransientInputCapture)
            {
                return false;
            }
        }
        return !combo.IsOpen && !host.HasTransientInputCapture;
    }
}
#endif
