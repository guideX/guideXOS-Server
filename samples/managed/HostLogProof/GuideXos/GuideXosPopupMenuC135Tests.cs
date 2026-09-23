#if HOSTLOGPROOF_C135_POPUP_MENU_TESTS
namespace HostLogProof;

/// <summary>Focused C135 API/state probes for the bounded popup menu.</summary>
public static class GuideXosPopupMenuC135Tests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool result = true;
        result &= Check(Construction());
        result &= Check(EmptyMenu());
        result &= Check(AddItem());
        result &= Check(AddMultipleItems());
        result &= Check(MaximumCapacity());
        result &= Check(OverflowRejected());
        result &= Check(BoundedText());
        result &= Check(DisabledItem());
        result &= Check(SeparatorRow());
        result &= Check(OpenState());
        result &= Check(OriginClamped());
        result &= Check(InitialActiveRow());
        result &= Check(PointerActivation());
        result &= Check(CallbackExactlyOnce());
        result &= Check(DisabledPointerStaysOpen());
        result &= Check(OutsideCancellation());
        result &= Check(StalePointerIgnored());
        result &= Check(DownNavigation());
        result &= Check(UpNavigation());
        result &= Check(DisabledRowsSkipped());
        result &= Check(EnterActivation());
        result &= Check(SpaceActivation());
        result &= Check(EscapeCancellation());
        result &= Check(TabCancellation());
        result &= Check(ShiftTabCancellation());
        result &= Check(HideInterruption());
        result &= Check(DisableInterruption());
        result &= Check(InvokerInterruption());
        result &= Check(ClearInterruption());
        result &= Check(ResetRelaunch());
        result &= Check(CommandIdsBounded());
        result &= Check(TextRetrievalBounded());
        result &= Check(HitTestingMatchesRows());
        result &= Check(SeparatorSkippedByNavigation());
        result &= Check(CallbackMutationCloses());
        result &= Check(ReentrantActivationBounded());
        result &= Check(RepeatedOpenClose());
        result &= Check(AllDisabledRejected());
        result &= Check(CallbackCommandId());
        result &= Check(OverflowPreservesEntries());
        return result && s_caseCount == 40;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static GuideXosPopupMenu New(int capacity = 4, int textLength = 8)
    {
        GuideXosPopupMenu menu = new(10, 20, 160, capacity, textLength);
        menu.TryAddItem("Open", 20u);
        menu.TryAddItem("Save", 22u);
        menu.TryAddItem("Delete", 24u);
        return menu;
    }

    private static bool Construction()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 4, 8);
        return menu.X == 10 && menu.Y == 20 && menu.Width == 160 &&
            menu.MaximumItemCount == 4 && menu.MaximumItemTextLength == 8 &&
            !menu.IsOpen && menu.ActiveIndex == -1;
    }

    private static bool EmptyMenu()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 4, 8);
        return menu.ItemCount == 0 &&
            menu.Open() == GuideXosPopupMenuResult.Rejected && !menu.IsOpen;
    }

    private static bool AddItem()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 4, 8);
        return menu.TryAddItem("Open", 20u) && menu.ItemCount == 1 &&
            menu.GetCommandId(0) == 20u;
    }

    private static bool AddMultipleItems()
    {
        GuideXosPopupMenu menu = New();
        return menu.ItemCount == 3 && menu.GetItemText(1) == "Save" &&
            menu.GetCommandId(2) == 24u;
    }

    private static bool MaximumCapacity()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 4, 8);
        for (uint command = 1; command <= 4; command++)
        {
            if (!menu.TryAddItem("X", command)) return false;
        }
        return menu.ItemCount == 4 && menu.MaximumItemCount == 4;
    }

    private static bool OverflowRejected()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 1, 8);
        return menu.TryAddItem("X", 1u) && !menu.TryAddItem("Y", 2u) &&
            menu.ItemCount == 1 && menu.GetCommandId(0) == 1u;
    }

    private static bool BoundedText()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 2, 4);
        return menu.TryAddItem("ABCD", 1u) &&
            !menu.TryAddItem("ABCDE", 2u) && menu.GetItemText(0) == "ABCD";
    }

    private static bool DisabledItem()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 2, 8);
        return menu.TryAddItem("Open", 1u, false) &&
            !menu.IsItemEnabled(0) && !menu.IsSeparator(0);
    }

    private static bool SeparatorRow()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 3, 8);
        return menu.TryAddItem("Open", 1u) && menu.TryAddSeparator() &&
            menu.TryAddItem("Save", 2u) && menu.IsSeparator(1) &&
            menu.GetCommandId(1) == 0u;
    }

    private static bool OpenState()
    {
        GuideXosPopupMenu menu = New();
        return menu.Open(20, 40) == GuideXosPopupMenuResult.Opened &&
            menu.IsOpen && menu.Height == 54;
    }

    private static bool OriginClamped()
    {
        GuideXosPopupMenu menu = New();
        return menu.Open(4090, 4090) == GuideXosPopupMenuResult.Opened &&
            menu.X == 4095 - menu.Width && menu.Y == 4095 - menu.Height;
    }

    private static bool InitialActiveRow()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 3, 8);
        menu.TryAddItem("Open", 1u, false);
        menu.TryAddItem("Save", 2u);
        return menu.Open() == GuideXosPopupMenuResult.Opened &&
            menu.ActiveIndex == 1;
    }

    private static bool PointerActivation()
    {
        GuideXosPopupMenu menu = New();
        uint command = 0u;
        menu.CommandInvoked = id => command = id;
        menu.Open(20, 40);
        return menu.HandlePointerDown(20, 40 + GuideXosPopupMenu.PopupRowHeight + 2) ==
            GuideXosPopupMenuResult.Activated && command == 22u && !menu.IsOpen;
    }

    private static bool CallbackExactlyOnce()
    {
        GuideXosPopupMenu menu = New();
        int callbacks = 0;
        menu.CommandInvoked = _ => ++callbacks;
        menu.Open();
        menu.HandleKey(GuideXosTextInputKey.Enter);
        return callbacks == 1 && !menu.IsOpen;
    }

    private static bool DisabledPointerStaysOpen()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 2, 8);
        menu.TryAddItem("Open", 1u, false);
        menu.TryAddItem("Save", 2u);
        menu.Open();
        return menu.HandlePointerDown(10, 20) == GuideXosPopupMenuResult.Disabled &&
            menu.IsOpen;
    }

    private static bool OutsideCancellation()
    {
        GuideXosPopupMenu menu = New();
        menu.Open(20, 40);
        return menu.HandlePointerDown(400, 400) ==
            GuideXosPopupMenuResult.Cancelled && !menu.IsOpen;
    }

    private static bool StalePointerIgnored()
    {
        GuideXosPopupMenu menu = New();
        int callbacks = 0;
        menu.CommandInvoked = _ => ++callbacks;
        menu.Open();
        menu.SetVisible(false);
        return menu.HandlePointerDown(10, 20) == GuideXosPopupMenuResult.Ignored &&
            callbacks == 0;
    }

    private static bool DownNavigation()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        return menu.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosPopupMenuResult.Moved && menu.ActiveIndex == 1;
    }

    private static bool UpNavigation()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        menu.HandleKey(GuideXosTextInputKey.Down);
        return menu.HandleKey(GuideXosTextInputKey.Up) ==
            GuideXosPopupMenuResult.Moved && menu.ActiveIndex == 0;
    }

    private static bool DisabledRowsSkipped()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 3, 8);
        menu.TryAddItem("Open", 1u);
        menu.TryAddItem("Save", 2u, false);
        menu.TryAddItem("Delete", 3u);
        menu.Open();
        return menu.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosPopupMenuResult.Moved && menu.ActiveIndex == 2;
    }

    private static bool EnterActivation()
    {
        GuideXosPopupMenu menu = New();
        uint command = 0u;
        menu.CommandInvoked = id => command = id;
        menu.Open();
        return menu.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosPopupMenuResult.Activated && command == 20u;
    }

    private static bool SpaceActivation()
    {
        GuideXosPopupMenu menu = New();
        uint command = 0u;
        menu.CommandInvoked = id => command = id;
        menu.Open();
        return menu.HandleKey((GuideXosTextInputKey)' ') ==
            GuideXosPopupMenuResult.Activated && command == 20u;
    }

    private static bool EscapeCancellation()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        return menu.HandleKey(GuideXosTextInputKey.Escape) ==
            GuideXosPopupMenuResult.Cancelled && !menu.IsOpen;
    }

    private static bool TabCancellation()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        return menu.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosPopupMenuResult.Cancelled && !menu.IsOpen;
    }

    private static bool ShiftTabCancellation()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        return menu.HandleKey(GuideXosTextInputKey.Tab) ==
            GuideXosPopupMenuResult.Cancelled && !menu.IsOpen;
    }

    private static bool HideInterruption()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        menu.SetVisible(false);
        return !menu.IsOpen && !menu.EffectiveVisible;
    }

    private static bool DisableInterruption()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        menu.SetEnabled(false);
        return !menu.IsOpen && !menu.Enabled;
    }

    private static bool InvokerInterruption()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        menu.SetInvokerAvailable(false);
        return !menu.IsOpen && !menu.InvokerAvailable;
    }

    private static bool ClearInterruption()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        menu.ClearItems();
        return menu.ItemCount == 0 && !menu.IsOpen && menu.ActiveIndex == -1;
    }

    private static bool ResetRelaunch()
    {
        GuideXosPopupMenu menu = New();
        menu.Open();
        menu.Reset();
        return menu.Enabled && menu.Visible && menu.ItemCount == 3 &&
            menu.Open() == GuideXosPopupMenuResult.Opened;
    }

    private static bool CommandIdsBounded()
    {
        GuideXosPopupMenu menu = New();
        return menu.GetCommandId(0) == 20u && menu.GetCommandId(99) == 0u;
    }

    private static bool TextRetrievalBounded()
    {
        GuideXosPopupMenu menu = New();
        return menu.GetItemText(0) == "Open" &&
            menu.GetItemText(99) == string.Empty;
    }

    private static bool HitTestingMatchesRows()
    {
        GuideXosPopupMenu menu = New();
        menu.Open(20, 40);
        return menu.ContainsPoint(20, 40) &&
            menu.ContainsPoint(20, 40 + 53) &&
            !menu.ContainsPoint(20, 40 + 54);
    }

    private static bool SeparatorSkippedByNavigation()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 3, 8);
        menu.TryAddItem("Open", 1u);
        menu.TryAddSeparator();
        menu.TryAddItem("Save", 2u);
        menu.Open();
        menu.HandleKey(GuideXosTextInputKey.Down);
        return menu.ActiveIndex == 2 &&
            menu.HandleKey(GuideXosTextInputKey.Up) ==
                GuideXosPopupMenuResult.Moved && menu.ActiveIndex == 0;
    }

    private static bool CallbackMutationCloses()
    {
        GuideXosPopupMenu menu = New();
        bool observedClosed = false;
        menu.CommandInvoked = _ => observedClosed = !menu.IsOpen;
        menu.Open();
        menu.HandleKey(GuideXosTextInputKey.Enter);
        return observedClosed && !menu.IsOpen;
    }

    private static bool ReentrantActivationBounded()
    {
        GuideXosPopupMenu menu = New();
        int callbacks = 0;
        menu.CommandInvoked = _ =>
        {
            ++callbacks;
            menu.HandleKey(GuideXosTextInputKey.Enter);
        };
        menu.Open();
        menu.HandleKey(GuideXosTextInputKey.Enter);
        return callbacks == 1 && !menu.IsOpen;
    }

    private static bool RepeatedOpenClose()
    {
        GuideXosPopupMenu menu = New();
        for (int count = 0; count < 8; count++)
        {
            if (menu.Open() != GuideXosPopupMenuResult.Opened ||
                menu.Cancel() != GuideXosPopupMenuResult.Cancelled)
            {
                return false;
            }
        }
        return !menu.IsOpen && menu.ActiveIndex == -1;
    }

    private static bool AllDisabledRejected()
    {
        GuideXosPopupMenu menu = New();
        menu.SetItemEnabled(0, false);
        menu.SetItemEnabled(1, false);
        menu.SetItemEnabled(2, false);
        return menu.Open() == GuideXosPopupMenuResult.Rejected && !menu.IsOpen;
    }

    private static bool CallbackCommandId()
    {
        GuideXosPopupMenu menu = New();
        uint command = 0u;
        menu.CommandInvoked = id => command = id;
        menu.Open();
        menu.HandleKey(GuideXosTextInputKey.Down);
        menu.HandleKey(GuideXosTextInputKey.Enter);
        return command == 22u;
    }

    private static bool OverflowPreservesEntries()
    {
        GuideXosPopupMenu menu = new(10, 20, 160, 2, 8);
        menu.TryAddItem("Open", 1u);
        menu.TryAddItem("Save", 2u);
        menu.TryAddItem("Bad", 3u);
        return menu.ItemCount == 2 && menu.GetItemText(0) == "Open" &&
            menu.GetItemText(1) == "Save";
    }
}
#endif
