namespace HostLogProof;

/// <summary>Focused C133 API/state probes for the bounded non-editable selector.</summary>
public static class GuideXosComboBoxC133Tests
{
    private static int s_caseCount;
    private static GuideXosComboBox s_reentrantCombo;
    private static int s_reentrantCallbacks;

    public static bool Run(GuideXosHost host)
    {
        s_caseCount = 0;
        bool result = true;
        result &= Check(Construction());
        result &= Check(ZeroItems());
        result &= Check(AddOne());
        result &= Check(AddMultiple());
        result &= Check(MaximumCapacity());
        result &= Check(OverflowRejected());
        result &= Check(NoSelection());
        result &= Check(ProgrammaticSelection());
        result &= Check(SelectedText());
        result &= Check(RepeatedSelection());
        result &= Check(InvalidSelection());
        result &= Check(ClearItems());
        result &= Check(CallbackCount());
        result &= Check(PointerOpen());
        result &= Check(SpaceOpen());
        result &= Check(EnterOpen());
        result &= Check(EscapeCancel());
        result &= Check(DownHighlight());
        result &= Check(UpHighlight());
        result &= Check(EnterCommit());
        result &= Check(SpaceCommit());
        result &= Check(PointerSelection());
        result &= Check(CurrentPointerCloses());
        result &= Check(OutsideCloses());
        result &= Check(EmptyText());
        result &= Check(MaximumText());
        result &= Check(OverlongTextRejected());
        result &= Check(LongSelectedText());
        result &= Check(ProgrammaticMutationWhileOpen());
        result &= Check(ClearWhileOpen());
        result &= Check(VisibilityCancellation());
        result &= Check(EnabledCancellation());
        result &= Check(FocusCancellation());
        result &= Check(PanelCancellation());
        result &= Check(StaleCharacterIgnored());
        result &= Check(StalePointerIgnored());
        result &= Check(ResetRelaunch());
        result &= Check(DeterministicGeometry());
        result &= Check(ActiveStartsAtCommitted());
        result &= Check(NoSelectionCancelRestores());
        result &= Check(CallbackReentrantBounded());
        result &= Check(HomeEndBounded());
        result &= Check(ClosedStateRenderingContract());
        result &= Check(PopupStateRenderingContract());

        bool count = s_caseCount == 44;
        return result && count;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    // Keep the fixture footprint small; the production/default-capacity case
    // is covered explicitly by Construction and MaximumCapacity.
    private static GuideXosComboBox New(int capacity = 4, int textLength = 8)
    {
        GuideXosComboBox combo = new(10, 20, 200, 28,
            capacity, textLength, 4);
        combo.TryAddItem("A");
        combo.TryAddItem("B");
        combo.TryAddItem("C");
        return combo;
    }

    private static bool Construction()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28, 4, 8, 4);
        return combo.X == 10 && combo.Y == 20 && combo.Width == 200 &&
            combo.Height == 28 && combo.MaximumItemCount == 4 &&
            combo.MaximumItemTextLength == 8 && !combo.IsOpen;
    }

    private static bool ZeroItems()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28, 4, 8, 4);
        return combo.ItemCount == 0 && combo.SelectedIndex == -1 &&
            combo.SelectedText == string.Empty &&
            combo.Open() == GuideXosComboBoxResult.Rejected && !combo.IsOpen;
    }

    private static bool AddOne()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28, 4, 8, 4);
        return combo.TryAddItem("Only") && combo.ItemCount == 1 &&
            combo.GetItemText(0) == "Only" && combo.SelectedIndex == -1;
    }

    private static bool AddMultiple()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28, 4, 8, 4);
        return combo.TryAddItem("A") && combo.TryAddItem("B") &&
            combo.TryAddItem("C") && combo.ItemCount == 3;
    }

    private static bool MaximumCapacity()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28, 4);
        bool added = true;
        for (int index = 0; index < combo.MaximumItemCount; index++)
        {
            added &= combo.TryAddItem(index == 0 ? "0" :
                index == 1 ? "1" : index == 2 ? "2" : "3");
        }
        return added && combo.ItemCount == 4 && combo.GetItemText(3) == "3";
    }

    private static bool OverflowRejected()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28, 2);
        combo.TryAddItem("A");
        combo.TryAddItem("B");
        bool rejected = !combo.TryAddItem("C");
        return rejected && combo.ItemCount == 2 && combo.GetItemText(1) == "B";
    }

    private static bool NoSelection()
    {
        GuideXosComboBox combo = New();
        return combo.SelectedIndex == -1 && !combo.HasSelection &&
            combo.SelectedText.Length == 0;
    }

    private static bool ProgrammaticSelection()
    {
        GuideXosComboBox combo = New();
        return combo.TrySetSelectedIndex(1) && combo.SelectedIndex == 1 &&
            combo.SelectedText == "B";
    }

    private static bool SelectedText()
    {
        GuideXosComboBox combo = New();
        combo.SelectedIndex = 2;
        return combo.SelectedText == "C" && combo.GetItemText(4) == string.Empty;
    }

    private static bool RepeatedSelection()
    {
        GuideXosComboBox combo = New();
        int callbacks = 0;
        combo.Changed = _ => ++callbacks;
        combo.SelectedIndex = 1;
        combo.SelectedIndex = 1;
        return callbacks == 1 && combo.SelectedIndex == 1;
    }

    private static bool InvalidSelection()
    {
        GuideXosComboBox combo = New();
        bool rejected = !combo.TrySetSelectedIndex(combo.ItemCount + 1);
        return rejected && combo.SelectedIndex == -1 &&
            combo.RejectedInputCount == 1u;
    }

    private static bool ClearItems()
    {
        GuideXosComboBox combo = New();
        combo.SelectedIndex = 1;
        combo.ClearItems();
        return combo.ItemCount == 0 && combo.SelectedIndex == -1 &&
            combo.SelectedText == string.Empty && !combo.IsOpen;
    }

    private static bool CallbackCount()
    {
        GuideXosComboBox combo = New();
        int callbacks = 0;
        combo.Changed = _ => ++callbacks;
        combo.SelectedIndex = 0;
        combo.SelectedIndex = 2;
        combo.ClearItems();
        return callbacks == 3;
    }

    private static bool PointerOpen()
    {
        GuideXosComboBox combo = New();
        return combo.HandlePointerDown(10, 20) == GuideXosComboBoxResult.Opened &&
            combo.IsOpen && combo.IsFocused;
    }

    private static bool SpaceOpen()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        return combo.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosComboBoxResult.Ignored &&
            combo.HandleCharacter(' ') == GuideXosComboBoxResult.Opened &&
            combo.IsOpen;
    }

    private static bool EnterOpen()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        return combo.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosComboBoxResult.Opened && combo.IsOpen;
    }

    private static bool EscapeCancel()
    {
        GuideXosComboBox combo = New();
        combo.SelectedIndex = 1;
        combo.Focus();
        combo.Open();
        combo.HandleKey(GuideXosTextInputKey.Down);
        return combo.HandleKey(GuideXosTextInputKey.Escape) ==
                GuideXosComboBoxResult.Cancelled && !combo.IsOpen &&
            combo.SelectedIndex == 1;
    }

    private static bool DownHighlight()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        return combo.ActiveIndex == 0 &&
            combo.HandleKey(GuideXosTextInputKey.Down) ==
                GuideXosComboBoxResult.Moved &&
            combo.ActiveIndex == 1 && combo.SelectedIndex == -1;
    }

    private static bool UpHighlight()
    {
        GuideXosComboBox combo = New();
        combo.SelectedIndex = 2;
        combo.Focus();
        combo.Open();
        return combo.HandleKey(GuideXosTextInputKey.Up) ==
                GuideXosComboBoxResult.Moved && combo.ActiveIndex == 1 &&
            combo.SelectedIndex == 2;
    }

    private static bool EnterCommit()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        combo.HandleKey(GuideXosTextInputKey.Down);
        return combo.HandleKey(GuideXosTextInputKey.Enter) ==
                GuideXosComboBoxResult.SelectionChanged &&
            combo.SelectedIndex == 1 && !combo.IsOpen;
    }

    private static bool SpaceCommit()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        combo.HandleKey(GuideXosTextInputKey.Down);
        return combo.HandleKey((GuideXosTextInputKey)' ') ==
                GuideXosComboBoxResult.Ignored &&
            combo.HandleCharacter(' ') == GuideXosComboBoxResult.SelectionChanged &&
            combo.SelectedIndex == 1 && !combo.IsOpen;
    }

    private static bool PointerSelection()
    {
        GuideXosComboBox combo = New();
        combo.HandlePointerDown(10, 20);
        int callbacks = 0;
        combo.Changed = _ => ++callbacks;
        return combo.HandlePointerDown(10, 66) ==
                GuideXosComboBoxResult.SelectionChanged &&
            combo.SelectedIndex == 1 && callbacks == 1 && !combo.IsOpen;
    }

    private static bool CurrentPointerCloses()
    {
        GuideXosComboBox combo = New();
        combo.SelectedIndex = 1;
        int callbacks = 0;
        combo.Changed = _ => ++callbacks;
        combo.HandlePointerDown(10, 20);
        return combo.HandlePointerDown(10, 66) == GuideXosComboBoxResult.Closed &&
            combo.SelectedIndex == 1 && callbacks == 0 && !combo.IsOpen;
    }

    private static bool OutsideCloses()
    {
        GuideXosComboBox combo = New();
        combo.SelectedIndex = 1;
        combo.Focus();
        combo.Open();
        combo.HandleKey(GuideXosTextInputKey.Down);
        return combo.HandlePointerDown(400, 400) == GuideXosComboBoxResult.Cancelled &&
            combo.SelectedIndex == 1 && !combo.IsOpen;
    }

    private static bool EmptyText()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28);
        return combo.TryAddItem(string.Empty) && combo.GetItemText(0) == string.Empty;
    }

    private static bool MaximumText()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28, 2, 4);
        return combo.TryAddItem("ABCD") && combo.GetItemText(0) == "ABCD";
    }

    private static bool OverlongTextRejected()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28, 2, 4);
        return !combo.TryAddItem("ABCDE") && combo.ItemCount == 0;
    }

    private static bool LongSelectedText()
    {
        GuideXosComboBox combo = new(10, 20, 200, 28, 2, 8);
        combo.TryAddItem("ABCDEFGH");
        combo.TrySetSelectedIndex(0);
        return combo.SelectedText == "ABCDEFGH";
    }

    private static bool ProgrammaticMutationWhileOpen()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        combo.HandleKey(GuideXosTextInputKey.Down);
        return combo.TrySetSelectedIndex(2) && combo.SelectedIndex == 2 &&
            !combo.IsOpen && combo.ActiveIndex == -1;
    }

    private static bool ClearWhileOpen()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        combo.ClearItems();
        return combo.ItemCount == 0 && combo.SelectedIndex == -1 && !combo.IsOpen;
    }

    private static bool VisibilityCancellation()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        combo.SetVisible(false);
        return !combo.IsOpen && !combo.IsFocused && !combo.EffectiveVisible;
    }

    private static bool EnabledCancellation()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        combo.SetEnabled(false);
        return !combo.IsOpen && !combo.IsFocused && !combo.Enabled;
    }

    private static bool FocusCancellation()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        combo.Blur();
        return !combo.IsOpen && !combo.IsFocused;
    }

    private static bool PanelCancellation()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosComboBox combo = New();
        bool attached = panel.TryAddChild(combo, 8, 14) == GuideXosPanelResult.Added;
        combo.Focus();
        combo.Open();
        panel.SetVisible(false);
        return attached && !combo.IsOpen && !combo.EffectiveVisible;
    }

    private static bool StaleCharacterIgnored()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.HandleKey((GuideXosTextInputKey)' ');
        combo.SetVisible(false);
        combo.SetVisible(true);
        combo.Focus();
        return combo.HandleCharacter(' ') == GuideXosComboBoxResult.Opened &&
            combo.SelectedIndex == -1;
    }

    private static bool StalePointerIgnored()
    {
        GuideXosComboBox combo = New();
        combo.HandlePointerDown(10, 20);
        combo.HandlePointerDown(400, 400);
        return combo.SelectedIndex == -1 && !combo.IsOpen &&
            combo.HandlePointerDown(400, 66) == GuideXosComboBoxResult.Ignored;
    }

    private static bool ResetRelaunch()
    {
        GuideXosComboBox combo = New();
        combo.SelectedIndex = 1;
        combo.Focus();
        combo.Open();
        combo.Reset();
        return !combo.IsOpen && !combo.IsFocused && combo.SelectedIndex == 1 &&
            combo.ItemCount == 3;
    }

    private static bool DeterministicGeometry()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        return combo.PopupY == 48 && combo.ContainsDropDownPoint(10, 48) &&
            combo.ContainsDropDownPoint(10, 47 + GuideXosComboBox.PopupRowHeight) &&
            !combo.ContainsDropDownPoint(10, 48 + 4 * GuideXosComboBox.PopupRowHeight);
    }

    private static bool ActiveStartsAtCommitted()
    {
        GuideXosComboBox combo = New();
        combo.SelectedIndex = 2;
        combo.Focus();
        combo.Open();
        return combo.ActiveIndex == 2;
    }

    private static bool NoSelectionCancelRestores()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        combo.HandleKey(GuideXosTextInputKey.Down);
        combo.HandleKey(GuideXosTextInputKey.Escape);
        return combo.SelectedIndex == -1 && combo.SelectedText.Length == 0;
    }

    private static bool CallbackReentrantBounded()
    {
        GuideXosComboBox combo = New();
        s_reentrantCombo = combo;
        s_reentrantCallbacks = 0;
        combo.Changed = ReentrantChanged;
        combo.SelectedIndex = 0;
        bool result = s_reentrantCallbacks == 1 && combo.SelectedIndex == 1;
        s_reentrantCombo = null;
        return result;
    }

    private static void ReentrantChanged(int index)
    {
        ++s_reentrantCallbacks;
        if (index == 0) s_reentrantCombo.SelectedIndex = 1;
    }

    private static bool HomeEndBounded()
    {
        GuideXosComboBox combo = New();
        combo.Focus();
        combo.Open();
        bool home = combo.HandleKey(GuideXosTextInputKey.Home) ==
            GuideXosComboBoxResult.Ignored;
        bool end = combo.HandleKey(GuideXosTextInputKey.End) ==
            GuideXosComboBoxResult.Moved && combo.ActiveIndex == 2;
        return home && end;
    }

    private static bool ClosedStateRenderingContract()
    {
        GuideXosComboBox combo = New();
        return !combo.IsOpen && combo.SelectedText.Length == 0 &&
            combo.EffectiveVisible;
    }

    private static bool PopupStateRenderingContract()
    {
        GuideXosComboBox combo = New();
        combo.SelectedIndex = 1;
        combo.Focus();
        combo.Open();
        return combo.IsOpen && combo.ActiveIndex == combo.SelectedIndex &&
            combo.ContainsDropDownPoint(combo.X, combo.PopupY);
    }
}
