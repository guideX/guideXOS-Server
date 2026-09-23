#if HOSTLOGPROOF_C135_HOST_TESTS
namespace HostLogProof;

/// <summary>Focused C135 host probes for the shared transient-capture lease.</summary>
public static class GuideXosPopupMenuC135HostTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool result = true;
        result &= Check(Registration());
        result &= Check(EmptyAcquisitionRejected());
        result &= Check(OpenAcquisition());
        result &= Check(OwnerIdentity());
        result &= Check(PointerActivation());
        result &= Check(CallbackExactlyOnce());
        result &= Check(DisabledItemCannotActivate());
        result &= Check(OutsideClickConsumed());
        result &= Check(UnderlyingControlUnaffected());
        result &= Check(DownNavigation());
        result &= Check(UpNavigation());
        result &= Check(DisabledRowsSkipped());
        result &= Check(EnterActivation());
        result &= Check(SpaceActivation());
        result &= Check(EscapeCancellation());
        result &= Check(TabForwardTraversal());
        result &= Check(ShiftTabReverseTraversal());
        result &= Check(HideReleasesCapture());
        result &= Check(DisableReleasesCapture());
        result &= Check(PanelMembershipReleasesCapture());
        result &= Check(ModalReleasesCapture());
        result &= Check(UnregisterReleasesCapture());
        result &= Check(ApplicationCloseReleasesCapture());
        result &= Check(StalePointerIgnored());
        result &= Check(StaleKeyCharacterIgnored());
        result &= Check(CallbackMutationReleasesCapture());
        result &= Check(RepeatedOpenCloseNoLeak());
        result &= Check(SecondMenuRejected());
        result &= Check(ComboThenMenuConflict());
        result &= Check(MenuThenComboConflict());
        result &= Check(FocusRemainsWithInvoker());
        result &= Check(MenuIsNotFocusable());
        result &= Check(CaptureReacquiresAfterClose());
        result &= Check(OutsideClickDoesNotTraverse());
        result &= Check(SeparatorDoesNotActivate());
        result &= Check(DisabledClickKeepsLease());
        result &= Check(ModalInputIsolated());
        result &= Check(RegistrationCountStable());
        result &= Check(MenuRelaunchStateClean());
        result &= Check(HostCapacityBounded());
        return result && s_caseCount == 40;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static GuideXosPopupMenu NewMenu()
    {
        GuideXosPopupMenu menu = new(20, 40, 160, 4, 16);
        menu.TryAddItem("Open", 20u);
        menu.TryAddItem("Save", 22u);
        menu.TryAddSeparator();
        menu.TryAddItem("Delete", 24u);
        return menu;
    }

    private static GuideXosControlHost NewHost(
        GuideXosPopupMenu menu,
        GuideXosButton button = null)
    {
        GuideXosControlHost host = new(4);
        host.TryRegisterPopupMenu(1, menu, false);
        if (button != null) host.TryRegisterButton(2, button);
        return host;
    }

    private static bool OpenCapture(
        GuideXosControlHost host, GuideXosPopupMenu menu)
    {
        return menu.Open(20, 40) == GuideXosPopupMenuResult.Opened &&
            host.TryAcquireTransientInputCapture(1);
    }

    private static bool Registration()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        return host.RegistrationCount == 1 &&
            host.TryRegisterPopupMenu(1, new(20, 40, 160), false) ==
                GuideXosControlHostResult.Rejected;
    }

    private static bool EmptyAcquisitionRejected()
    {
        GuideXosPopupMenu menu = new(20, 40, 160);
        GuideXosControlHost host = NewHost(menu);
        return !host.TryAcquireTransientInputCapture(1) &&
            !host.HasTransientInputCapture;
    }

    private static bool OpenAcquisition()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        return OpenCapture(host, menu) && host.HasTransientInputCapture;
    }

    private static bool OwnerIdentity()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        return host.TransientInputCaptureOwnerId == 1 &&
            host.TransientInputCaptureKind == GuideXosManagedControlKind.PopupMenu;
    }

    private static bool PointerActivation()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        int callbacks = 0;
        menu.CommandInvoked = _ => ++callbacks;
        OpenCapture(host, menu);
        return host.FocusAndRoutePointer(1, 20,
                40 + GuideXosPopupMenu.PopupRowHeight + 2) ==
            GuideXosControlHostResult.Activated && callbacks == 1 &&
            !host.HasTransientInputCapture;
    }

    private static bool CallbackExactlyOnce()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        int callbacks = 0;
        menu.CommandInvoked = _ => ++callbacks;
        OpenCapture(host, menu);
        host.HandleKey(GuideXosTextInputKey.Enter);
        return callbacks == 1 && !menu.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool DisabledItemCannotActivate()
    {
        GuideXosPopupMenu menu = NewMenu();
        menu.SetItemEnabled(1, false);
        GuideXosControlHost host = NewHost(menu);
        int callbacks = 0;
        menu.CommandInvoked = _ => ++callbacks;
        OpenCapture(host, menu);
        return host.FocusAndRoutePointer(1, 20,
                40 + GuideXosPopupMenu.PopupRowHeight + 2) ==
            GuideXosControlHostResult.Disabled && callbacks == 0 &&
            menu.IsOpen && host.HasTransientInputCapture;
    }

    private static bool OutsideClickConsumed()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        return host.FocusAndRoutePointer(1, 400, 400) ==
            GuideXosControlHostResult.Cancelled && !host.HasTransientInputCapture;
    }

    private static bool UnderlyingControlUnaffected()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(menu, button);
        host.TryFocus(2);
        OpenCapture(host, menu);
        return host.FocusAndRoutePointer(2, 220, 20) ==
            GuideXosControlHostResult.Cancelled && button.IsFocused;
    }

    private static bool DownNavigation()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        return host.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosControlHostResult.Moved && menu.ActiveIndex == 1;
    }

    private static bool UpNavigation()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        host.HandleKey(GuideXosTextInputKey.Down);
        return host.HandleKey(GuideXosTextInputKey.Up) ==
            GuideXosControlHostResult.Moved && menu.ActiveIndex == 0;
    }

    private static bool DisabledRowsSkipped()
    {
        GuideXosPopupMenu menu = NewMenu();
        menu.SetItemEnabled(1, false);
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        return host.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosControlHostResult.Moved && menu.ActiveIndex == 3;
    }

    private static bool EnterActivation()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        return host.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Activated && !host.HasTransientInputCapture;
    }

    private static bool SpaceActivation()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        return host.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosControlHostResult.Activated && !host.HasTransientInputCapture;
    }

    private static bool EscapeCancellation()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        return host.HandleKey(GuideXosTextInputKey.Escape) ==
            GuideXosControlHostResult.Cancelled && !host.HasTransientInputCapture;
    }

    private static bool TabForwardTraversal()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(menu, button);
        host.TryFocus(2);
        OpenCapture(host, menu);
        return host.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosControlHostResult.Traversed && !menu.IsOpen &&
            host.ActiveControlId == 2;
    }

    private static bool ShiftTabReverseTraversal()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(menu, button);
        host.TryFocus(2);
        OpenCapture(host, menu);
        return host.HandleKey(GuideXosTextInputKey.Tab, true) ==
            GuideXosControlHostResult.Traversed && !menu.IsOpen &&
            host.ActiveControlId == 2;
    }

    private static bool HideReleasesCapture()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        menu.SetVisible(false);
        return !host.HasTransientInputCapture && !menu.IsOpen;
    }

    private static bool DisableReleasesCapture()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        menu.SetEnabled(false);
        return !host.HasTransientInputCapture && !menu.IsOpen;
    }

    private static bool PanelMembershipReleasesCapture()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        menu.SetInvokerAvailable(false);
        return !host.HasTransientInputCapture && !menu.IsOpen;
    }

    private static bool ModalReleasesCapture()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        GuideXosControlHost modal = new(1);
        modal.TryRegisterButton(9, new GuideXosButton(220, 20, 80, 28, "Modal"));
        OpenCapture(host, menu);
        return host.EnterModal(modal) && !menu.IsOpen &&
            !host.HasTransientInputCapture;
    }

    private static bool UnregisterReleasesCapture()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        return host.TryUnregister(1) == GuideXosControlHostResult.Unregistered &&
            !menu.IsOpen && !host.HasTransientInputCapture;
    }

    private static bool ApplicationCloseReleasesCapture()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        host.Reset();
        return host.RegistrationCount == 0 && !menu.IsOpen &&
            !host.HasTransientInputCapture;
    }

    private static bool StalePointerIgnored()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        int callbacks = 0;
        menu.CommandInvoked = _ => ++callbacks;
        OpenCapture(host, menu);
        menu.SetVisible(false);
        return host.FocusAndRoutePointer(1, 20, 40) ==
            GuideXosControlHostResult.Rejected && callbacks == 0;
    }

    private static bool StaleKeyCharacterIgnored()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        int callbacks = 0;
        menu.CommandInvoked = _ => ++callbacks;
        OpenCapture(host, menu);
        menu.SetEnabled(false);
        return host.HandleCharacter(' ') == GuideXosControlHostResult.Ignored &&
            callbacks == 0;
    }

    private static bool CallbackMutationReleasesCapture()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        bool closedInCallback = false;
        menu.CommandInvoked = _ => closedInCallback = !menu.IsOpen;
        OpenCapture(host, menu);
        host.HandleKey(GuideXosTextInputKey.Enter);
        return closedInCallback && !host.HasTransientInputCapture;
    }

    private static bool RepeatedOpenCloseNoLeak()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        for (int count = 0; count < 6; count++)
        {
            if (!OpenCapture(host, menu) ||
                host.HandleKey(GuideXosTextInputKey.Escape) !=
                    GuideXosControlHostResult.Cancelled ||
                host.HasTransientInputCapture)
            {
                return false;
            }
        }
        return true;
    }

    private static bool SecondMenuRejected()
    {
        GuideXosPopupMenu first = NewMenu();
        GuideXosPopupMenu second = NewMenu();
        GuideXosControlHost host = NewHost(first);
        host.TryRegisterPopupMenu(2, second, false);
        OpenCapture(host, first);
        second.Open(220, 40);
        bool rejected = !host.TryAcquireTransientInputCapture(2) &&
            host.TransientInputCaptureOwnerId == 1;
        second.Cancel();
        first.Cancel();
        return rejected && !host.HasTransientInputCapture;
    }

    private static bool ComboThenMenuConflict()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosComboBox combo = new(220, 20, 160, 28, 2, 8, 2);
        combo.TryAddItem("A");
        combo.TryAddItem("B");
        GuideXosControlHost host = NewHost(menu);
        host.TryRegisterComboBox(3, combo);
        host.TryFocus(3);
        host.HandleKey(GuideXosTextInputKey.Enter);
        menu.Open(20, 40);
        bool rejected = !host.TryAcquireTransientInputCapture(1) &&
            host.TransientInputCaptureOwnerId == 3;
        menu.Cancel();
        combo.Close();
        return rejected && !host.HasTransientInputCapture;
    }

    private static bool MenuThenComboConflict()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosComboBox combo = new(220, 20, 160, 28, 2, 8, 2);
        combo.TryAddItem("A");
        combo.TryAddItem("B");
        GuideXosControlHost host = NewHost(menu);
        host.TryRegisterComboBox(3, combo);
        OpenCapture(host, menu);
        bool blocked = host.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Activated && !combo.IsOpen;
        host.HandleKey(GuideXosTextInputKey.Escape);
        host.TryFocus(3);
        bool usable = host.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosControlHostResult.Activated && combo.IsOpen;
        combo.Close();
        return blocked && usable && !host.HasTransientInputCapture;
    }

    private static bool FocusRemainsWithInvoker()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(menu, button);
        host.TryFocus(2);
        OpenCapture(host, menu);
        bool retained = host.ActiveControlId == 2 && button.IsFocused;
        menu.Cancel();
        return retained;
    }

    private static bool MenuIsNotFocusable()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu,
            new GuideXosButton(220, 20, 80, 28, "Next"));
        return host.TryFocus(1) == GuideXosControlHostResult.Rejected &&
            host.ActiveIndex == -1;
    }

    private static bool CaptureReacquiresAfterClose()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        host.HandleKey(GuideXosTextInputKey.Escape);
        return OpenCapture(host, menu) && host.HasTransientInputCapture;
    }

    private static bool OutsideClickDoesNotTraverse()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosButton button = new(220, 20, 80, 28, "Next");
        GuideXosControlHost host = NewHost(menu, button);
        host.TryFocus(2);
        OpenCapture(host, menu);
        GuideXosControlHostResult result = host.FocusAndRoutePointer(2, 220, 20);
        return result == GuideXosControlHostResult.Cancelled &&
            host.ActiveControlId == 2;
    }

    private static bool SeparatorDoesNotActivate()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        return host.FocusAndRoutePointer(1, 20,
                40 + 2 * GuideXosPopupMenu.PopupRowHeight + 2) ==
            GuideXosControlHostResult.Ignored && menu.IsOpen;
    }

    private static bool DisabledClickKeepsLease()
    {
        GuideXosPopupMenu menu = NewMenu();
        menu.SetItemEnabled(0, false);
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        host.FocusAndRoutePointer(1, 20, 40);
        return host.HasTransientInputCapture && menu.IsOpen;
    }

    private static bool ModalInputIsolated()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        GuideXosControlHost modal = new(1);
        GuideXosButton button = new(220, 20, 80, 28, "Modal");
        modal.TryRegisterButton(9, button);
        OpenCapture(host, menu);
        host.EnterModal(modal);
        bool isolated = !menu.IsOpen && !host.HasTransientInputCapture &&
            host.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosControlHostResult.Ignored;
        host.ExitModal();
        return isolated;
    }

    private static bool RegistrationCountStable()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu,
            new GuideXosButton(220, 20, 80, 28, "Next"));
        int count = host.RegistrationCount;
        OpenCapture(host, menu);
        host.HandleKey(GuideXosTextInputKey.Escape);
        return count == host.RegistrationCount && count == 2;
    }

    private static bool MenuRelaunchStateClean()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = NewHost(menu);
        OpenCapture(host, menu);
        host.Reset();
        menu.Reset();
        menu.TryAddItem("Open", 20u);
        GuideXosControlHost fresh = NewHost(menu);
        return fresh.RegistrationCount == 1 && OpenCapture(fresh, menu) &&
            menu.ActiveIndex == 0;
    }

    private static bool HostCapacityBounded()
    {
        GuideXosPopupMenu menu = NewMenu();
        GuideXosControlHost host = new(1);
        return host.TryRegisterPopupMenu(1, menu, false) ==
                GuideXosControlHostResult.Registered &&
            host.TryRegisterButton(2, new GuideXosButton(220, 20, 80, 28, "No")) ==
                GuideXosControlHostResult.Rejected;
    }
}
#endif
