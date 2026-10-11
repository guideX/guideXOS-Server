#if HOSTLOGPROOF_C169_MANAGED_UI_REGRESSION
using System;

namespace HostLogProof;

/// <summary>Focused C169 checks for visible list selection and keyboard focus.</summary>
public static class GuideXosListBoxC169Tests
{
    public const int CaseCount = 10;
    public const int FocusTransitionStressCount = 500;

    public static bool Run(GuideXosHost host)
    {
        int cases = 0;
        bool result = true;
        GuideXosListBox list = new(4, 16, 2, 24, 12, 18);
        result &= Case(ref cases,
            list.TryAdd("Alpha") == GuideXosListBoxPopulationResult.Added &&
            list.TryAdd("Beta") == GuideXosListBoxPopulationResult.Added &&
            list.TryAdd("Gamma") == GuideXosListBoxPopulationResult.Added &&
            list.SelectedIndex == 0 && !list.IsFocused &&
            list.GetRenderMarker(0) == (byte)'*');

        list.Focus();
        result &= Case(ref cases, list.IsFocused &&
            list.GetRenderMarker(0) == (byte)'>');

        result &= Case(ref cases,
            list.HandleKey(GuideXosTextInputKey.Down) ==
                GuideXosListBoxResult.SelectionChanged &&
            list.SelectedIndex == 1 &&
            list.GetRenderMarker(0) == (byte)' ' &&
            list.GetRenderMarker(1) == (byte)'>');

        list.Blur();
        result &= Case(ref cases, !list.IsFocused &&
            list.GetRenderMarker(1) == (byte)'*');

        result &= Case(ref cases,
            list.HandlePointerDown(16, 18, 12, 18) ==
                GuideXosListBoxResult.SelectionChanged &&
            list.IsFocused && list.SelectedIndex == 0 &&
            list.GetRenderMarker(0) == (byte)'>');

        list.SetFirstVisibleIndex(1);
        list.HandlePointerDown(16, 36, 12, 18);
        result &= Case(ref cases, list.SelectedIndex == 2 &&
            list.GetRenderMarker(2) == (byte)'>');

        list.SetEnabled(false);
        result &= Case(ref cases, !list.IsFocused &&
            list.GetRenderMarker(0) == (byte)'x' &&
            list.GetRenderMarker(2) == (byte)'x');

        list.SetEnabled(true);
        list.Focus();
        list.SetFirstVisibleIndex(1);
        result &= Case(ref cases, list.SelectedIndex == 2 &&
            list.FirstVisibleIndex == 1 &&
            list.GetRenderMarker(2) == (byte)'>');

        bool focusTransitions = true;
        _ = list.GetRenderMarker(2);
        long allocatedBeforeTransitions = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < FocusTransitionStressCount; index++)
        {
            list.Blur();
            focusTransitions &= list.GetRenderMarker(2) == (byte)'*';
            list.Focus();
            focusTransitions &= list.GetRenderMarker(2) == (byte)'>';
        }
        bool noPerTransitionAllocation =
            GC.GetAllocatedBytesForCurrentThread() == allocatedBeforeTransitions;
        result &= Case(ref cases, focusTransitions && list.IsFocused &&
            list.MaximumItemCount == 4 && list.ItemCount == 3 &&
            noPerTransitionAllocation);

        list.ClearSelection();
        result &= Case(ref cases, list.GetRenderMarker(0) == (byte)' ' &&
            list.GetRenderMarker(2) == (byte)' ' && list.IsFocused);

        bool passed = result && cases == CaseCount;
        host?.TryLog(passed
            ? "C169-LISTBOX-RENDER cases=10 focusTransitions=500 result=PASS"u8
            : "C169-LISTBOX-RENDER result=FAIL"u8);
        return passed;
    }

    private static bool Case(ref int count, bool passed)
    {
        count++;
        return passed;
    }
}

public static class GuideXosManagedUiStressC169Tests
{
    public const int PopupCycleCount = 500;
    public const int ScrollMovementCount = 250;
    public const int SettingsLifecycleCycleCount = 100;

    public static bool Run(GuideXosHost host)
    {
        GuideXosPopupMenu menu = new(12, 18, 160, 2, 16);
        bool menuReady = menu.TryAddItem("Open", 1u) &&
            menu.TryAddItem("Close", 2u);
        GuideXosControlHost controlHost = new(2);
        menuReady &= controlHost.TryRegisterPopupMenu(1, menu, false) ==
            GuideXosControlHostResult.Registered;
        bool popupCycles = menuReady;
        for (int index = 0; popupCycles && index < PopupCycleCount; index++)
        {
            popupCycles &= menu.Open() == GuideXosPopupMenuResult.Opened &&
                controlHost.TryAcquireTransientInputCapture(1) &&
                controlHost.HandleKey(GuideXosTextInputKey.Escape) ==
                    GuideXosControlHostResult.Cancelled &&
                !menu.IsOpen && !controlHost.HasTransientInputCapture;
        }

        GuideXosScrollView view = new(8, 8, 120, 40, 8);
        bool viewReady = true;
        for (int index = 0; index < 8; index++)
        {
            GuideXosButton button = new(0, 0, 80, 18, "row");
            viewReady &= view.TryAddMember(button, 0, index * 18) ==
                GuideXosScrollViewResult.Added;
        }
        bool scrollMovements = viewReady && view.MaximumOffset > 0;
        for (int index = 0; scrollMovements && index < ScrollMovementCount; index++)
        {
            int before = view.Offset;
            int wheel = (index & 1) == 0 ? -1 : 1;
            scrollMovements &= view.HandleWheel(view.InnerX + 1,
                    view.InnerY + 1, wheel, 1) ==
                GuideXosScrollViewResult.Scrolled && view.Offset != before;
        }

        bool settingsLifecycle = true;
        for (int index = 0; settingsLifecycle &&
                index < SettingsLifecycleCycleCount; index++)
        {
            settingsLifecycle &= GuideXosDefaultAppsC168Tests.Run(null,
                new Applications.ManagedSettingsCenter());
        }

        bool passed = popupCycles && scrollMovements && settingsLifecycle;
        host?.TryLog(passed
            ? "C169-STRESS popupCycles=500 scrollViewMovements=250 c168Replays=100 c168ReplayPageCycles=25000 c168ReplaySelectionChanges=25000 c168ReplayApplyResetCycles=10000 c168ReplayPopupCycles=25000 settingsCloseReopenCycles=100 result=PASS"u8
            : "C169-STRESS result=FAIL"u8);
        return passed;
    }
}
#endif
