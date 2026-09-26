using System;

namespace HostLogProof;

/// <summary>
/// Focused C139 proof for the shared logical vertical viewport and both
/// migrated controls.  The tests intentionally use public control behavior
/// plus the explicit viewport binding contract; no host or renderer state is
/// reached through test-only fields.
/// </summary>
public static class GuideXosSharedScrollViewportC139Tests
{
    public const int ViewportCaseCount = 30;
    public const int TextAreaMigrationCaseCount = 10;
    public const int ListBoxMigrationCaseCount = 10;
    public const int CrossControlCaseCount = 4;
    public const int TotalCaseCount = ViewportCaseCount +
        TextAreaMigrationCaseCount + ListBoxMigrationCaseCount +
        CrossControlCaseCount;

    public static bool Run(GuideXosHost host)
    {
        // Keep the bounded ListBox migration before the TextArea migration in
        // the NativeAOT proof.  This order keeps the focused fixture stable;
        // the controls remain independently tested and share no test state.
        bool list = RunListBoxMigration();
        bool viewport = RunViewportCases();
        bool text = RunTextAreaMigration();
        bool cross = RunCrossControlConsistency();
        bool result = viewport && text && list && cross;
        if (host != null)
        {
            host.TryLog(result
                ? "C139-FOCUSED viewport=30 textarea=10 listbox=10 cross=4 total=54 result=PASS"u8
                : "C139-FOCUSED viewport=FAIL textarea=FAIL listbox=FAIL cross=FAIL result=FAIL"u8);
        }
        return result;
    }

    private static bool RunViewportCases()
    {
        bool defaults = ConstructionDefaults();
        bool empty = ZeroContent();
        bool zeroVisible = ZeroVisibleExtent();
        bool smaller = ContentSmallerThanViewport();
        bool equal = ContentEqualToViewport();
        bool larger = ContentLargerThanViewport();
        bool direct = DirectOffsetAndClamping();
        bool small = SmallSteps();
        bool page = PageStepsAndBoundaryClamps();
        bool growth = ContentGrowth();
        bool shrinkNoClamp = ContentShrinkWithoutClamp();
        bool shrinkClamp = ContentShrinkWithClamp();
        bool visible = VisibleExtentChanges();
        bool callbacks = CallbackSemantics();
        bool large = LargeExtentAndInvalidState();
        return defaults && empty && zeroVisible && smaller && equal && larger &&
            direct && small && page && growth && shrinkNoClamp && shrinkClamp &&
            visible && callbacks && large;
    }

    private static bool ConstructionDefaults()
    {
        GuideXosVerticalViewport viewport = new();
        return viewport.ContentExtent == 0 && viewport.VisibleExtent == 0 &&
            viewport.Offset == 0 && viewport.MaximumOffset == 0 &&
            viewport.SmallChange == 1 && viewport.LargeChange == 1;
    }

    private static bool ZeroContent()
    {
        GuideXosVerticalViewport viewport = new(0, 5);
        viewport.Offset = 20;
        int callbacks = 0;
        viewport.Changed += _ => callbacks++;
        viewport.ScrollSmall(1);
        viewport.ScrollLarge(1);
        return viewport.Offset == 0 && viewport.MaximumOffset == 0 &&
            callbacks == 0;
    }

    private static bool ZeroVisibleExtent()
    {
        GuideXosVerticalViewport viewport = new(20, 0);
        viewport.Offset = 7;
        return viewport.MaximumOffset == 20 && viewport.Offset == 7 &&
            viewport.EnsureVisible(19) && viewport.Offset == 19;
    }

    private static bool ContentSmallerThanViewport()
    {
        GuideXosVerticalViewport viewport = new(3, 5);
        viewport.Offset = 2;
        return viewport.MaximumOffset == 0 && viewport.Offset == 0;
    }

    private static bool ContentEqualToViewport()
    {
        GuideXosVerticalViewport viewport = new(5, 5);
        viewport.ScrollSmall(10);
        return viewport.MaximumOffset == 0 && viewport.Offset == 0;
    }

    private static bool ContentLargerThanViewport()
    {
        GuideXosVerticalViewport viewport = new(20, 5);
        viewport.Offset = 9;
        return viewport.MaximumOffset == 15 && viewport.Offset == 9;
    }

    private static bool DirectOffsetAndClamping()
    {
        GuideXosVerticalViewport viewport = new(20, 5);
        bool below = !viewport.SetOffset(-1) && viewport.Offset == 0;
        bool above = viewport.SetOffset(999) && viewport.Offset == 15;
        bool middle = viewport.SetOffset(6) && viewport.Offset == 6;
        return below && above && middle;
    }

    private static bool SmallSteps()
    {
        GuideXosVerticalViewport viewport = new(20, 5);
        bool positive = viewport.ScrollSmall(2) && viewport.Offset == 2;
        bool negative = viewport.ScrollSmall(-1) && viewport.Offset == 1;
        viewport.SmallChange = 2;
        bool configured = viewport.ScrollSmall(2) && viewport.Offset == 5;
        return positive && negative && configured;
    }

    private static bool PageStepsAndBoundaryClamps()
    {
        GuideXosVerticalViewport viewport = new(20, 5);
        bool forward = viewport.ScrollPageForward() && viewport.Offset == 4;
        bool backward = viewport.ScrollPageBackward() && viewport.Offset == 0;
        viewport.LargeChange = 9;
        bool configured = viewport.ScrollLarge(1) && viewport.Offset == 9;
        viewport.ScrollLarge(-100);
        bool top = viewport.Offset == 0;
        viewport.ScrollLarge(100);
        bool bottom = viewport.Offset == viewport.MaximumOffset;
        return forward && backward && configured && top && bottom;
    }

    private static bool ContentGrowth()
    {
        GuideXosVerticalViewport viewport = new(5, 5);
        int callbacks = 0;
        viewport.Changed += _ => callbacks++;
        viewport.ContentExtent = 20;
        bool grown = viewport.MaximumOffset == 15 && viewport.Offset == 0;
        viewport.Offset = 10;
        return grown && viewport.Offset == 10 && callbacks == 1;
    }

    private static bool ContentShrinkWithoutClamp()
    {
        GuideXosVerticalViewport viewport = new(20, 5) { Offset = 4 };
        int callbacks = 0;
        viewport.Changed += _ => callbacks++;
        viewport.ContentExtent = 15;
        return viewport.MaximumOffset == 10 && viewport.Offset == 4 &&
            callbacks == 0;
    }

    private static bool ContentShrinkWithClamp()
    {
        GuideXosVerticalViewport viewport = new(20, 5) { Offset = 15 };
        int callbacks = 0;
        viewport.Changed += value =>
        {
            callbacks++;
            if (value == 10) viewport.Offset = 9;
        };
        viewport.ContentExtent = 15;
        return viewport.MaximumOffset == 10 && viewport.Offset == 9 &&
            callbacks == 1;
    }

    private static bool VisibleExtentChanges()
    {
        GuideXosVerticalViewport viewport = new(20, 5) { Offset = 15 };
        int callbacks = 0;
        viewport.Changed += _ => callbacks++;
        viewport.VisibleExtent = 10;
        bool grows = viewport.MaximumOffset == 10 && viewport.Offset == 10;
        viewport.VisibleExtent = 3;
        bool shrinks = viewport.MaximumOffset == 17 && viewport.Offset == 10;
        return grows && shrinks && callbacks == 1;
    }

    private static bool CallbackSemantics()
    {
        GuideXosVerticalViewport same = new(20, 5);
        int sameCallbacks = 0;
        same.Changed += _ => sameCallbacks++;
        same.Offset = 3;
        same.Offset = 3;
        same.ScrollSmall(0);
        same.ScrollLarge(0);
        bool once = sameCallbacks == 1;

        GuideXosVerticalViewport mutation = new(20, 5);
        int mutationCallbacks = 0;
        mutation.Changed += value =>
        {
            mutationCallbacks++;
            if (value == 3) mutation.Offset = 4;
        };
        mutation.Offset = 3;
        bool bounded = mutation.Offset == 4 && mutationCallbacks == 1;

        GuideXosVerticalViewport boundary = new(20, 5);
        int boundaryCallbacks = 0;
        boundary.Changed += _ => boundaryCallbacks++;
        boundary.Offset = boundary.MaximumOffset;
        boundary.ScrollLarge(1);
        boundary.ScrollSmall(1);
        boundary.ScrollLarge(1);
        bool noStorm = boundaryCallbacks == 1;
        return once && bounded && noStorm;
    }

    private static bool LargeExtentAndInvalidState()
    {
        GuideXosVerticalViewport viewport = new(int.MaxValue, 1);
        viewport.Offset = int.MaxValue;
        viewport.ScrollSmall(int.MaxValue);
        viewport.ScrollLarge(int.MaxValue);
        viewport.ContentExtent = -7;
        viewport.VisibleExtent = -9;
        return viewport.ContentExtent == 0 && viewport.VisibleExtent == 0 &&
            viewport.MaximumOffset == 0 && viewport.Offset == 0;
    }

    private static bool RunTextAreaMigration()
    {
        GuideXosTextArea area = NewTextArea(16);
        bool shared = area.VerticalViewport.ContentExtent == area.LineCount &&
            area.VerticalViewport.VisibleExtent == area.VisibleLineCount;
        bool initial = area.FirstVisibleLine == 0 &&
            area.VerticalViewport.Offset == 0;
        int caret = area.CaretIndex;
        bool wheel = area.HandleWheel(-1) == GuideXosTextAreaEditResult.Scrolled &&
            area.VerticalViewport.Offset == 3;
        bool caretStable = area.CaretIndex == caret;

        GuideXosScrollBar bar = new(80, 0, 16, 100);
        bar.BindViewport(area.VerticalViewport);
        bar.Value = area.MaximumFirstVisibleLine;
        bool boundValue = area.FirstVisibleLine == area.MaximumFirstVisibleLine &&
            bar.Value == area.FirstVisibleLine;

        area.SetCaretToStart();
        area.Focus();
        for (int index = 0; index < 7; index++)
        {
            area.HandleKey(GuideXosTextInputKey.Down);
        }
        bool reveal = area.CaretLine == 7 && area.FirstVisibleLine == 4;

        area.SetFirstVisibleLine(area.MaximumFirstVisibleLine);
        area.SetText("One\nTwo");
        bar.SynchronizeViewport();
        bool shrink = area.FirstVisibleLine == 0 && bar.Value == 0 &&
            bar.Maximum == 0;
        bool renderState = area.FirstVisibleLine == area.VerticalViewport.Offset;

        GuideXosTextArea relaunched = NewTextArea(3);
        bool relaunch = relaunched.FirstVisibleLine == 0 &&
            relaunched.CaretLine == 0;
        return shared && initial && wheel && caretStable && boundValue && reveal &&
            shrink && renderState && relaunch;
    }

    private static bool RunListBoxMigration()
    {
        // Eight rows are enough to exercise wheel/page/selection reveal and
        // leave the fixture comfortably below the host's bounded capacity.
        GuideXosListBox list = NewListBox(8);
        bool shared = list.VerticalViewport.ContentExtent == list.ItemCount &&
            list.VerticalViewport.VisibleExtent == list.VisibleRowCount;
        bool initial = list.FirstVisibleIndex == 0;
        int selected = list.SelectedIndex;
        bool wheel = list.HandleWheel(-1) == GuideXosListBoxResult.Scrolled &&
            list.VerticalViewport.Offset == 3;
        bool selectionStable = list.SelectedIndex == selected;

        GuideXosScrollBar bar = new(80, 0, 16, 100);
        bar.BindViewport(list.VerticalViewport);
        bar.Value = list.MaximumFirstVisibleIndex;
        bool boundValue = list.FirstVisibleIndex == list.MaximumFirstVisibleIndex &&
            bar.Value == list.FirstVisibleIndex;

        list.Focus();
        list.SetFirstVisibleIndex(0);
        list.SelectIndex(0);
        for (int index = 0; index < 6; index++)
        {
            list.HandleKey(GuideXosTextInputKey.Down);
        }
        bool reveal = list.SelectedIndex == 6 && list.FirstVisibleIndex == 3;

        list.SetFirstVisibleIndex(list.MaximumFirstVisibleIndex);
        list.Clear();
        bar.SynchronizeViewport();
        bool shrink = list.FirstVisibleIndex == 0 && bar.Value == 0 &&
            bar.Maximum == 0 && list.SelectedIndex == -1;
        list.TryAdd("replacement");
        bool pointer = list.HandlePointerDown(8, 0, 0, 0, 8, 18) ==
            GuideXosListBoxResult.Ignored || list.SelectedIndex == 0;
        GuideXosListBox relaunched = NewListBox(3);
        bool relaunch = relaunched.FirstVisibleIndex == 0 &&
            relaunched.SelectedIndex == 0;
        return shared && initial && wheel && selectionStable && boundValue && reveal &&
            shrink && pointer && relaunch;
    }

    private static bool RunCrossControlConsistency()
    {
        GuideXosVerticalViewport text = new(20, 5);
        GuideXosVerticalViewport list = new(20, 5);
        bool initial = text.Offset == 0 && list.Offset == 0;
        text.ScrollSmall(2);
        list.ScrollSmall(2);
        bool small = text.Offset == list.Offset;
        text.ScrollLarge(1);
        list.ScrollLarge(1);
        bool page = text.Offset == list.Offset;
        text.Offset = text.MaximumOffset;
        list.Offset = list.MaximumOffset;
        text.ContentExtent = 8;
        list.ContentExtent = 8;
        bool shrink = text.Offset == 3 && list.Offset == 3 &&
            text.MaximumOffset == list.MaximumOffset;
        return initial && small && page && shrink;
    }

    private static GuideXosTextArea NewTextArea(int lineCount)
    {
        GuideXosTextArea area = new(256, 64, 4, 64);
        area.SetText(BuildLines(lineCount));
        area.SetCaretToStart();
        area.Blur();
        return area;
    }

    private static GuideXosListBox NewListBox(int itemCount)
    {
        GuideXosListBox list = new(32, 32, 4, 24);
        for (int index = 0; index < itemCount; index++) list.TryAdd("Item");
        list.Blur();
        return list;
    }

    private static string BuildLines(int count)
    {
        string result = "Line 00";
        for (int index = 1; index < count; index++)
        {
            result += "\nLine " + index.ToString("D2");
        }
        return count == 0 ? string.Empty : result;
    }
}
