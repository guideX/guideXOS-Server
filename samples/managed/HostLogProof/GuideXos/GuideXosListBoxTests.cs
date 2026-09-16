using System;

namespace HostLogProof;

/// <summary>
/// Deterministic public-API probes for the reusable bounded list box.
/// </summary>
public static class GuideXosListBoxTests
{
    public static bool Run(GuideXosHost host)
    {
        bool storage = StorageAndPopulation();
        bool selection = SelectionAndActivation();
        bool pointer = PointerSelection();
        bool viewport = ViewportAndBounds();
        bool focus = FocusIsolation();
        bool reset = ResetAndRepeatedActivation();
        bool result = storage && selection && pointer && viewport && focus && reset;
        if (host != null)
        {
            if (!storage) host.TryLog("C118-TEST-GROUP storage=FAIL"u8);
            if (!selection) host.TryLog("C118-TEST-GROUP selection=FAIL"u8);
            if (!pointer) host.TryLog("C118-TEST-GROUP pointer=FAIL"u8);
            if (!viewport) host.TryLog("C118-TEST-GROUP viewport=FAIL"u8);
            if (!focus) host.TryLog("C118-TEST-GROUP focus=FAIL"u8);
            if (!reset) host.TryLog("C118-TEST-GROUP reset=FAIL"u8);
            host.TryLog(result
                ? "C118-TESTS cases=38 storage=PASS selection=PASS pointer=PASS viewport=PASS focus=PASS reset=PASS result=PASS"u8
                : "C118-TESTS cases=38 result=FAIL"u8);
        }
        return result;
    }

    private static bool StorageAndPopulation()
    {
        GuideXosListBox empty = new(4, 8, 3, 12);
        bool emptyState = empty.ItemCount == 0 && empty.SelectedIndex == -1 &&
            !empty.HasSelection && empty.FirstVisibleIndex == 0;
        empty.Focus();
        bool emptyEnter = empty.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosListBoxResult.Rejected && empty.SelectedIndex == -1;

        GuideXosListBox one = new(4, 8, 3, 12);
        bool oneItem = one.TryAdd("one") ==
            GuideXosListBoxPopulationResult.Added && one.ItemCount == 1 &&
            one.SelectedIndex == 0 && one.SelectedLabel == "one";
        GuideXosListBox many = new(8, 8, 3, 12);
        bool population = Add(many, "zero", "one", "two", "three", "four") &&
            many.ItemCount == 5 && many.GetItemLabel(3) == "three";
        many.Clear();
        bool clear = many.ItemCount == 0 && many.SelectedIndex == -1 &&
            many.FirstVisibleIndex == 0 && !many.HasSelection;
        bool repopulate = many.TryAdd("repop") ==
            GuideXosListBoxPopulationResult.Added && many.SelectedIndex == 0 &&
            many.SelectedLabel == "repop";

        GuideXosListBox capacity = new(3, 8, 2, 12);
        bool maximum = Add(capacity, "a", "b", "c") &&
            capacity.ItemCount == capacity.MaximumItemCount;
        int selectedBeforeOverflow = capacity.SelectedIndex;
        bool overflow = capacity.TryAdd("d") ==
            GuideXosListBoxPopulationResult.Rejected && capacity.ItemCount == 3 &&
            capacity.SelectedIndex == selectedBeforeOverflow &&
            capacity.GetItemLabel(2) == "c";

        GuideXosListBox labels = new(2, 5, 2, 12);
        bool maximumLabel = labels.TryAdd("12345") ==
            GuideXosListBoxPopulationResult.Added;
        bool labelOverflow = labels.TryAdd("123456") ==
            GuideXosListBoxPopulationResult.Rejected && labels.ItemCount == 1 &&
            labels.SelectedLabel == "12345";
        return emptyState && emptyEnter && oneItem && population && clear &&
            repopulate && maximum && overflow && maximumLabel && labelOverflow;
    }

    private static bool SelectionAndActivation()
    {
        GuideXosListBox list = New("zero", "one", "two");
        bool initial = list.SelectedIndex == 0 && list.HasSelection;
        bool down = list.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosListBoxResult.SelectionChanged && list.SelectedIndex == 1;
        bool up = list.HandleKey(GuideXosTextInputKey.Up) ==
            GuideXosListBoxResult.SelectionChanged && list.SelectedIndex == 0;
        bool upClamp = list.HandleKey(GuideXosTextInputKey.Up) ==
            GuideXosListBoxResult.Ignored && list.SelectedIndex == 0;
        bool home = list.HandleKey(GuideXosTextInputKey.Home) ==
            GuideXosListBoxResult.Ignored && list.SelectedIndex == 0;
        bool end = list.HandleKey(GuideXosTextInputKey.End) ==
            GuideXosListBoxResult.SelectionChanged && list.SelectedIndex == 2;
        bool downClamp = list.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosListBoxResult.Ignored && list.SelectedIndex == 2;
        bool activate = list.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosListBoxResult.Activated && list.SelectedIndex == 2 &&
            list.SelectedLabel == "two";
        return initial && down && up && upClamp && home && end && downClamp &&
            activate;
    }

    private static bool PointerSelection()
    {
        GuideXosListBox list = New("first", "middle", "last");
        list.Blur();
        bool outside = list.HandlePointerDown(9, 20, 10, 20) ==
            GuideXosListBoxResult.Ignored && !list.IsFocused;
        bool focus = list.HandlePointerDown(11, 21, 10, 20) ==
            GuideXosListBoxResult.Focused && list.IsFocused &&
            list.SelectedIndex == 0;
        bool middle = list.HandlePointerDown(11, 20 + 18 + 1, 10, 20) ==
            GuideXosListBoxResult.SelectionChanged && list.SelectedIndex == 1;
        bool last = list.HandlePointerDown(11, 20 + 36 + 1, 10, 20) ==
            GuideXosListBoxResult.SelectionChanged && list.SelectedIndex == 2;
        GuideXosListBox unusedRows = New("first", "middle");
        bool unused = unusedRows.HandlePointerDown(11, 20 + 2 * 18 + 1, 10, 20) ==
            GuideXosListBoxResult.Focused && unusedRows.SelectedIndex == 0;
        GuideXosListBox scrolled = New("a", "b", "c", "d", "e");
        bool below = scrolled.HandlePointerDown(11, 20 + 4 * 18 + 1, 10, 20) ==
            GuideXosListBoxResult.Ignored && scrolled.SelectedIndex == 0;
        return outside && focus && middle && last && unused && below;
    }

    private static bool ViewportAndBounds()
    {
        GuideXosListBox list = New(3, "a", "b", "c", "d", "e", "f", "g");
        bool start = list.FirstVisibleIndex == 0 && Visible(list);
        bool scrollDown = Move(list, GuideXosTextInputKey.Down, 5) &&
            list.SelectedIndex == 5 && list.FirstVisibleIndex == 3 && Visible(list);
        bool end = list.HandleKey(GuideXosTextInputKey.End) ==
            GuideXosListBoxResult.SelectionChanged && list.SelectedIndex == 6 &&
            list.FirstVisibleIndex == 4 && Visible(list);
        bool home = list.HandleKey(GuideXosTextInputKey.Home) ==
            GuideXosListBoxResult.SelectionChanged && list.SelectedIndex == 0 &&
            list.FirstVisibleIndex == 0 && Visible(list);
        bool scrollUp = Move(list, GuideXosTextInputKey.Down, 6) &&
            Move(list, GuideXosTextInputKey.Up, 6) && list.SelectedIndex == 0 &&
            list.FirstVisibleIndex == 0 && Visible(list);

        list.HandleKey(GuideXosTextInputKey.End);
        list.Clear();
        bool clearViewport = list.ItemCount == 0 && list.SelectedIndex == -1 &&
            list.FirstVisibleIndex == 0;
        bool repopulated = Add(list, "new0", "new1") &&
            list.SelectedIndex == 0 && list.FirstVisibleIndex == 0 && Visible(list);
        return start && scrollDown && end && home && scrollUp && clearViewport &&
            repopulated;
    }

    private static bool FocusIsolation()
    {
        GuideXosListBox first = New("a", "b");
        GuideXosListBox second = New("x", "y");
        second.Blur();
        bool unfocused = second.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosListBoxResult.Ignored && second.SelectedIndex == 0;
        bool independent = first.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosListBoxResult.SelectionChanged && first.SelectedIndex == 1 &&
            second.SelectedIndex == 0;
        first.Blur();
        bool firstIgnored = first.HandleKey(GuideXosTextInputKey.Home) ==
            GuideXosListBoxResult.Ignored && first.SelectedIndex == 1;
        return unfocused && independent && firstIgnored;
    }

    private static bool ResetAndRepeatedActivation()
    {
        GuideXosListBox list = New("one", "two");
        list.HandleKey(GuideXosTextInputKey.End);
        bool first = list.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosListBoxResult.Activated && list.SelectedIndex == 1;
        bool second = list.HandleKey(GuideXosTextInputKey.Enter) ==
            GuideXosListBoxResult.Activated && list.SelectedIndex == 1 &&
            list.FirstVisibleIndex == 0;
        list.Reset();
        bool reset = !list.IsFocused && list.ItemCount == 0 &&
            list.SelectedIndex == -1 && list.FirstVisibleIndex == 0;
        return first && second && reset;
    }

    private static GuideXosListBox New(params string[] labels)
    {
        GuideXosListBox list = new(16, 16, 3, 20);
        return Add(list, labels) ? list : null;
    }

    private static GuideXosListBox New(int visibleRows, params string[] labels)
    {
        GuideXosListBox list = new(16, 16, visibleRows, 20);
        return Add(list, labels) ? list : null;
    }

    private static bool Add(GuideXosListBox list, params string[] labels)
    {
        if (list == null || labels == null) return false;
        for (int index = 0; index < labels.Length; index++)
        {
            if (list.TryAdd(labels[index]) !=
                GuideXosListBoxPopulationResult.Added) return false;
        }
        list.Focus();
        return true;
    }

    private static bool Move(
        GuideXosListBox list, GuideXosTextInputKey key, int count)
    {
        if (list == null) return false;
        for (int index = 0; index < count; index++)
        {
            GuideXosListBoxResult result = list.HandleKey(key);
            if (result == GuideXosListBoxResult.Rejected) return false;
        }
        return true;
    }

    private static bool Visible(GuideXosListBox list)
    {
        return list != null && list.HasSelection &&
            list.SelectedIndex >= list.FirstVisibleIndex &&
            list.SelectedIndex < list.FirstVisibleIndex + list.VisibleRowCount &&
            list.FirstVisibleIndex >= 0 &&
            list.FirstVisibleIndex <= Math.Max(0, list.ItemCount - list.VisibleRowCount);
    }
}
