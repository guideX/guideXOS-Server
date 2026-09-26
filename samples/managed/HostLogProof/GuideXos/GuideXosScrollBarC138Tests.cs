using System;

namespace HostLogProof;

/// <summary>Focused C138 coverage for the reusable scrollbar and its bindings.</summary>
public static class GuideXosScrollBarC138Tests
{
    public const int ApiCaseCount = 22;
    public const int DragHostCaseCount = 20;
    public const int TextAreaBindingCaseCount = 10;
    public const int ListBoxBindingCaseCount = 10;
    public const int TotalCaseCount = ApiCaseCount + DragHostCaseCount +
        TextAreaBindingCaseCount + ListBoxBindingCaseCount;

    public static bool Run(GuideXosHost host)
    {
        bool result = RunApi();
        bool drag = RunDragHost();
        bool text = RunTextAreaBinding();
        bool list = RunListBoxBinding();
        if (host != null)
        {
            host.TryLog(result && drag && text && list
                ? "C138-FOCUSED api=22 drag-host=20 textarea=10 listbox=10 total=62 result=PASS"u8
                : "C138-FOCUSED result=FAIL"u8);
        }
        return result && drag && text && list;
    }

    public static bool RunApi()
    {
        int callbacks = 0;
        GuideXosScrollBar bar = new(10, 20, 16, 100);
        bool construction = bar.X == 10 && bar.Y == 20 && bar.Width == 16 &&
            bar.Height == 100;
        bool defaults = bar.Minimum == 0 && bar.Maximum == 0 && bar.Value == 0 &&
            bar.PageSize == 0 && bar.SmallChange == 1 && bar.LargeChange == 1;
        bar.Changed += _ => callbacks++;
        bar.Maximum = 100;
        bar.PageSize = 10;
        bar.SmallChange = 1;
        bar.LargeChange = 9;
        bool programmatic = SetAndCheck(bar, 40, 40);
        bool below = SetAndCheck(bar, -10, 0);
        bool above = SetAndCheck(bar, 999, 100);
        int beforeSame = callbacks;
        bar.Value = 100;
        bool same = callbacks == beforeSame;
        bool callbackOnce = callbacks == 3;

        bar.Minimum = 25;
        bool collapsed = bar.Minimum == 25 && bar.Maximum == 100 &&
            bar.Value == 100;
        bar.Maximum = 25;
        bool rangeCollapse = bar.Minimum == 25 && bar.Maximum == 25 &&
            bar.Value == 25;
        bar.PageSize = 0;
        bool zeroPage = bar.ThumbHeight <= bar.TrackLength;

        bar.Minimum = 0;
        bar.Maximum = 100;
        bar.PageSize = 50;
        bar.Value = 0;
        bool thumbMinimum = bar.ThumbTop == bar.TrackTop;
        int bottomTop = bar.TrackTop + bar.TrackLength - bar.ThumbHeight;
        bar.Value = bar.Maximum;
        bool thumbMaximum = bar.ThumbTop == bottomTop;
        bar.Value = 50;
        int midpoint = bar.ThumbTop;
        bool midpointMapping = midpoint == bar.TrackTop +
            (bar.TrackLength - bar.ThumbHeight) / 2;

        GuideXosScrollBar tiny = new(0, 0, 2, 2);
        tiny.Maximum = int.MaxValue;
        tiny.PageSize = 1;
        bool tinyTrack = tiny.ThumbHeight == tiny.TrackLength &&
            tiny.ThumbTop == tiny.TrackTop;
        bar.SetEnabledForTest(false);
        bool disabled = bar.HandlePointerDown(bar.X + 1, bar.ThumbTop) ==
            GuideXosScrollBarResult.Disabled;
        bar.SetEnabledForTest(true);
        bar.SetVisibleForTest(false);
        bool hidden = bar.HandleWheel(-1) == GuideXosScrollBarResult.Ignored;
        bar.SetVisibleForTest(true);
        bar.Value = 50;
        bool pageUp = bar.HandlePointerDown(bar.X + 1, bar.TrackTop + 1) ==
            GuideXosScrollBarResult.Paged && bar.Value == 41;
        bool pageDown = bar.HandlePointerDown(bar.X + 1,
            bar.ThumbBottom + 1) == GuideXosScrollBarResult.Paged &&
            bar.Value == 50;
        bool arrowStep = bar.HandlePointerDown(bar.X + 1, bar.Y) ==
            GuideXosScrollBarResult.Stepped && bar.Value == 49;

        int mutationCallbacks = 0;
        GuideXosScrollBar mutation = new(0, 0, 12, 80) { Maximum = 10 };
        mutation.Changed += value =>
        {
            mutationCallbacks++;
            if (value == 3) mutation.Value = 4;
        };
        mutation.Value = 3;
        bool boundedMutation = mutation.Value == 4 && mutationCallbacks == 1;
        mutation.Focus();
        bool keyboard = mutation.HandleKey(GuideXosTextInputKey.Home) ==
            GuideXosScrollBarResult.Changed && mutation.Value == 0;
        bool wheel = mutation.HandleWheel(-1) == GuideXosScrollBarResult.Stepped &&
            mutation.Value == 4;

        return construction && defaults && programmatic && below && above && same &&
            callbackOnce && collapsed && rangeCollapse && zeroPage &&
            thumbMinimum && thumbMaximum && midpointMapping && tinyTrack &&
            disabled && hidden && pageUp && pageDown && arrowStep &&
            boundedMutation && keyboard && wheel;
    }

    public static bool RunDragHost()
    {
        GuideXosScrollBar bar = NewScrollableBar();
        GuideXosControlHost host = new(2);
        bool registered = host.TryRegisterScrollBar(1, bar) ==
            GuideXosControlHostResult.Registered;
        int pressY = bar.ThumbTop + 2;
        bool started = host.FocusAndRoutePointer(1, bar.X + 2, pressY) ==
            GuideXosControlHostResult.DragStarted && host.HasPointerDragCapture;
        int old = bar.Value;
        bool moved = host.HandlePointerMove(bar.X + 2, bar.TrackTop +
            bar.TrackLength + 100) == GuideXosControlHostResult.Dragged &&
            bar.Value == bar.Maximum;
        bool released = host.HandlePointerUp(bar.X + 2, bar.Y + bar.Height + 100) ==
            GuideXosControlHostResult.DragEnded && !host.HasPointerDragCapture;
        bool movedOutside = old != bar.Value;

        bar.Value = 50;
        int offset = 5;
        int originalTop = bar.ThumbTop;
        bool offsetStart = host.FocusAndRoutePointer(1, bar.X + 1,
            originalTop + offset) == GuideXosControlHostResult.DragStarted &&
            bar.DragOffset == offset;
        host.HandlePointerMove(bar.X + 1, originalTop + offset);
        bool noJump = bar.ThumbTop == originalTop;
        host.HandlePointerUp(0, 0);

        bar.Value = 20;
        host.FocusAndRoutePointer(1, bar.X + 1, bar.ThumbTop + 1);
        host.CancelPointerDrag();
        bool staleRelease = host.HandlePointerUp(900, 900) ==
            GuideXosControlHostResult.Ignored && !host.HasPointerDragCapture;

        host.FocusAndRoutePointer(1, bar.X + 1, bar.ThumbTop + 1);
        bar.SetVisible(false);
        host.RefreshVisibility();
        bool hideCancels = !host.HasPointerDragCapture && !bar.IsDragging;
        bar.SetVisible(true);
        host.FocusAndRoutePointer(1, bar.X + 1, bar.ThumbTop + 1);
        bar.SetEnabled(false);
        host.RefreshVisibility();
        bool disableCancels = !host.HasPointerDragCapture && !bar.IsDragging;
        bar.SetEnabled(true);
        host.FocusAndRoutePointer(1, bar.X + 1, bar.ThumbTop + 1);
        host.TryUnregister(1);
        bool unregisterCancels = !host.HasPointerDragCapture && !bar.IsDragging;

        bar = NewScrollableBar();
        host = new(2);
        host.TryRegisterScrollBar(1, bar);
        host.FocusAndRoutePointer(1, bar.X + 1, bar.ThumbTop + 1);
        GuideXosControlHost modal = new();
        bool modalEntered = host.EnterModal(modal);
        bool modalCancels = modalEntered && !host.HasPointerDragCapture &&
            !bar.IsDragging;
        host.ExitModal();
        host.Reset();
        bool closeCancels = !host.HasPointerDragCapture && !bar.IsDragging;

        GuideXosScrollBar secondary = NewScrollableBar();
        bool secondaryIgnored = secondary.HandlePointerDown(
            secondary.X + 1, secondary.ThumbTop,
            GuideXosPointerButton.Secondary) == GuideXosScrollBarResult.Ignored &&
            !secondary.IsDragging;

        GuideXosScrollBar popupBar = NewScrollableBar();
        GuideXosPopupMenu menu = new(40, 10, 80);
        menu.TryAddItem("Open", 1);
        GuideXosControlHost popupHost = new(2);
        popupHost.TryRegisterScrollBar(1, popupBar);
        popupHost.TryRegisterPopupMenu(2, menu);
        menu.Open(40, 10);
        bool popupCapture = popupHost.TryAcquireTransientInputCapture(2);
        bool popupBlocksDrag = popupCapture && popupHost.FocusAndRoutePointer(
            1, popupBar.X + 1, popupBar.ThumbTop) !=
            GuideXosControlHostResult.DragStarted && !popupBar.IsDragging;
        menu.Close();
        popupHost.FocusAndRoutePointer(1, popupBar.X + 1, popupBar.ThumbTop);
        bool dragBlocksPopup = popupHost.HasPointerDragCapture &&
            !popupHost.TryAcquireTransientInputCapture(2);
        popupHost.HandlePointerUp(0, 0);
        bool stableOwner = !popupHost.HasPointerDragCapture;

        GuideXosScrollBar wheel = NewScrollableBar();
        GuideXosControlHost wheelHost = new(1);
        wheelHost.TryRegisterScrollBar(1, wheel);
        wheelHost.FocusAndRoutePointer(1, wheel.X + 1, wheel.ThumbTop);
        int duringDrag = wheel.Value;
        bool wheelIgnored = wheelHost.HandleWheel(1, wheel.X + 1,
            wheel.ThumbTop, -1) == GuideXosControlHostResult.Ignored &&
            wheel.Value == duringDrag;
        wheelHost.HandlePointerUp(0, 0);
        bool finalNone = !wheelHost.HasPointerDragCapture && !wheel.IsDragging;

        return registered && started && moved && released && movedOutside &&
            offsetStart && noJump && staleRelease && hideCancels && disableCancels &&
            unregisterCancels && modalCancels && closeCancels && secondaryIgnored &&
            popupBlocksDrag && dragBlocksPopup && stableOwner && wheelIgnored &&
            finalNone;
    }

    public static bool RunTextAreaBinding()
    {
        GuideXosTextArea area = new(256, 64, 4, 64);
        area.SetText(BuildLines(16));
        area.SetCaretToStart();
        GuideXosScrollBar bar = new(100, 0, 16, 100);
        BindTextArea(area, bar);
        bool initial = bar.Value == area.FirstVisibleLine &&
            bar.Maximum == area.MaximumFirstVisibleLine;

        area.HandleWheel(-1);
        SyncTextArea(area, bar);
        bool wheel = area.FirstVisibleLine == 3 && bar.Value == 3;

        GuideXosControlHost host = new(1);
        host.TryRegisterScrollBar(1, bar);
        host.FocusAndRoutePointer(1, bar.X + 1, bar.ThumbTop + 1);
        host.HandlePointerMove(bar.X + 1, bar.TrackTop + bar.TrackLength);
        bool drag = bar.Value == area.FirstVisibleLine && area.FirstVisibleLine ==
            area.MaximumFirstVisibleLine;
        host.HandlePointerUp(0, 0);

        int beforePage = area.FirstVisibleLine;
        host.FocusAndRoutePointer(1, bar.X + 1, bar.TrackTop);
        SyncTextArea(area, bar);
        bool page = area.FirstVisibleLine < beforePage;
        host.HandlePointerUp(0, 0);

        bar.Value = -100;
        SyncTextArea(area, bar);
        bool top = area.FirstVisibleLine == 0;
        bar.Value = int.MaxValue;
        SyncTextArea(area, bar);
        bool bottom = area.FirstVisibleLine == area.MaximumFirstVisibleLine;

        area.SetText("One\nTwo");
        SyncTextArea(area, bar);
        bool replacement = area.FirstVisibleLine == 0 && bar.Value == 0 &&
            bar.Maximum == 0;
        area.SetText("One\nTwo\nThree");
        SyncTextArea(area, bar);
        bool shortContent = bar.Maximum == 0 && bar.ThumbHeight == bar.TrackLength;

        area.SetText(BuildLines(16));
        area.SetCaretToStart();
        area.SetFirstVisibleLine(area.MaximumFirstVisibleLine);
        BindTextArea(area, bar);
        bool caretValid = area.CaretIndex == 0 && area.CaretLine == 0;
        area.Focus();
        bool edit = area.HandleCharacter('Z') == GuideXosTextAreaEditResult.Changed;
        SyncTextArea(area, bar);

        area.SetVisible(false);
        bar.SetVisible(false);
        bool hiddenSafe = area.HandleWheel(-1) == GuideXosTextAreaEditResult.Ignored &&
            bar.HandleWheel(-1) == GuideXosScrollBarResult.Ignored;
        area.SetVisible(true);
        bar.SetVisible(true);
        area.SetText(BuildLines(16));
        area.SetCaretToStart();
        BindTextArea(area, bar);
        bool relaunch = area.FirstVisibleLine == 0 && bar.Value == 0;

        return initial && wheel && drag && page && top && bottom && replacement &&
            shortContent && caretValid && edit && hiddenSafe && relaunch;
    }

    public static bool RunListBoxBinding()
    {
        GuideXosListBox list = NewListBox(16);
        GuideXosScrollBar bar = new(100, 0, 16, 100);
        BindListBox(list, bar);
        bool initial = bar.Value == 0 && bar.Maximum == list.MaximumFirstVisibleIndex;
        int selection = list.SelectedIndex;
        list.HandleWheel(-1);
        SyncListBox(list, bar);
        bool wheel = list.FirstVisibleIndex == 3 && bar.Value == 3 &&
            list.SelectedIndex == selection;

        GuideXosControlHost host = new(2);
        host.TryRegisterListBox(1, list);
        host.TryRegisterScrollBar(2, bar);
        host.FocusAndRoutePointer(2, bar.X + 1, bar.ThumbTop + 1);
        host.HandlePointerMove(bar.X + 1, bar.TrackTop + bar.TrackLength);
        SyncListBox(list, bar);
        bool drag = list.FirstVisibleIndex == list.MaximumFirstVisibleIndex &&
            list.SelectedIndex == selection;
        host.HandlePointerUp(0, 0);

        int beforePage = list.FirstVisibleIndex;
        host.FocusAndRoutePointer(2, bar.X + 1, bar.TrackTop);
        SyncListBox(list, bar);
        bool page = list.FirstVisibleIndex < beforePage;
        host.HandlePointerUp(0, 0);

        bar.Value = -10;
        SyncListBox(list, bar);
        bool top = list.FirstVisibleIndex == 0;
        bar.Value = 1000;
        SyncListBox(list, bar);
        bool bottom = list.FirstVisibleIndex == list.MaximumFirstVisibleIndex;

        list.Clear();
        list.TryAdd("Only");
        SyncListBox(list, bar);
        bool reduction = list.FirstVisibleIndex == 0 && bar.Maximum == 0;

        list = NewListBox(16);
        bar = new GuideXosScrollBar(100, 0, 16, 100);
        BindListBox(list, bar);
        int selected = list.SelectedIndex;
        bar.Value = 8;
        SyncListBox(list, bar);
        bool selectionStable = list.SelectedIndex == selected;
        GuideXosListBoxResult pointer = list.HandlePointerDown(
            8, 18, 0, 0, 8, 18);
        bool pointerMaps = pointer == GuideXosListBoxResult.SelectionChanged &&
            list.SelectedIndex == 9;
        list.Focus();
        list.HandleKey(GuideXosTextInputKey.Down);
        SyncListBox(list, bar);
        bool keyboard = list.SelectedIndex == 10 &&
            list.FirstVisibleIndex == 8 && bar.Value == 8;

        list.SetVisible(false);
        bar.SetVisible(false);
        bool modalSafe = list.HandleWheel(-1) == GuideXosListBoxResult.Ignored &&
            bar.HandleWheel(-1) == GuideXosScrollBarResult.Ignored;
        list.SetVisible(true);
        bar.SetVisible(true);
        list.Reset();
        for (int index = 0; index < 16; index++) list.TryAdd("Item");
        BindListBox(list, bar);
        bool relaunch = list.FirstVisibleIndex == 0 && bar.Value == 0 &&
            list.SelectedIndex == 0;

        return initial && wheel && drag && page && top && bottom && reduction &&
            selectionStable && pointerMaps && keyboard && modalSafe && relaunch;
    }

    private static bool SetAndCheck(GuideXosScrollBar bar, int requested, int expected)
    {
        bar.Value = requested;
        return bar.Value == expected;
    }

    private static GuideXosScrollBar NewScrollableBar()
    {
        return new GuideXosScrollBar(40, 10, 16, 140)
        {
            Maximum = 100,
            PageSize = 20,
            SmallChange = 1,
            LargeChange = 9,
            Value = 20,
        };
    }

    private static void BindTextArea(
        GuideXosTextArea area, GuideXosScrollBar bar)
    {
        bar.BindViewport(area.VerticalViewport);
        bar.SynchronizeViewport();
    }

    private static void SyncTextArea(
        GuideXosTextArea area, GuideXosScrollBar bar)
    {
        bar.SynchronizeViewport();
    }

    private static void BindListBox(
        GuideXosListBox list, GuideXosScrollBar bar)
    {
        bar.BindViewport(list.VerticalViewport);
        bar.SynchronizeViewport();
    }

    private static void SyncListBox(
        GuideXosListBox list, GuideXosScrollBar bar)
    {
        bar.SynchronizeViewport();
    }

    private static GuideXosTextArea NewTextArea(int lineCount)
    {
        GuideXosTextArea area = new(256, 64, 4, 64);
        area.SetText(BuildLines(lineCount));
        area.SetCaretToStart();
        return area;
    }

    private static GuideXosListBox NewListBox(int itemCount)
    {
        GuideXosListBox list = new(32, 32, 4, 24);
        for (int index = 0; index < itemCount; index++) list.TryAdd("Item");
        return list;
    }

    private static string BuildLines(int count)
    {
        string result = "Line 00";
        for (int index = 1; index < count; index++)
        {
            result += "\nLine " + index.ToString("D2");
        }
        return result;
    }
}

internal static class GuideXosScrollBarTestExtensions
{
    public static void SetEnabledForTest(this GuideXosScrollBar bar, bool value)
    {
        bar.SetEnabled(value);
    }

    public static void SetVisibleForTest(this GuideXosScrollBar bar, bool value)
    {
        bar.SetVisible(value);
    }
}
