namespace HostLogProof;

/// <summary>Focused C140 state, clipping, hit-translation, focus, and binding proof.</summary>
public static class GuideXosScrollViewC140Tests
{
    public const int CoreCaseCount = 20;
    public const int ClippingCaseCount = 9;
    public const int HitTestCaseCount = 10;
    public const int FocusCaseCount = 9;
    public const int ScrollBarCaseCount = 10;
    public const int TotalCaseCount = CoreCaseCount + ClippingCaseCount +
        HitTestCaseCount + FocusCaseCount + ScrollBarCaseCount;

    public static bool Run(GuideXosHost host)
    {
        bool core = CoreCases();
        bool clipping = ClippingCases();
        bool hit = HitTestCases();
        bool focus = FocusCases();
        bool scrollBar = ScrollBarCases();
        bool result = core && clipping && hit && focus && scrollBar;
        host?.TryLog(result
            ? "C140-FOCUSED core=20 clipping=9 hit=10 focus=9 scrollbar=10 total=58 result=PASS"u8
            : "C140-FOCUSED core=FAIL clipping=FAIL hit=FAIL focus=FAIL scrollbar=FAIL result=FAIL"u8);
        return result;
    }

    private static GuideXosScrollView NewView(int height = 56)
    {
        return new GuideXosScrollView(20, 20, 240, height, 8);
    }

    private static GuideXosButton Button(string label, int width = 120)
    {
        return new GuideXosButton(0, 0, width, 18, label);
    }

    private static bool CoreCases()
    {
        GuideXosScrollView view = NewView();
        GuideXosButton first = Button("first");
        bool construction = view.MemberCount == 0 && view.Offset == 0 &&
            view.ContentExtent == 0 && view.VisibleExtent == 54;
        bool empty = view.Clear() == GuideXosScrollViewResult.Empty;
        bool one = view.TryAddMember(first, 8, 0) ==
            GuideXosScrollViewResult.Added && view.MemberCount == 1 &&
            view.ContentExtent == 18;
        bool many = true;
        GuideXosButton[] buttons = new GuideXosButton[7];
        for (int index = 0; index < buttons.Length; index++)
        {
            buttons[index] = Button("member");
            many &= view.TryAddMember(buttons[index], 8, 24 + index * 24) ==
                GuideXosScrollViewResult.Added;
        }
        GuideXosButton overflow = Button("overflow");
        bool capacity = view.TryAddMember(overflow, 8, 220) ==
            GuideXosScrollViewResult.CapacityReached;
        bool duplicate = view.TryAddMember(first, 8, 0) ==
            GuideXosScrollViewResult.Duplicate;
        bool removal = view.TryRemoveMember(buttons[6]) ==
            GuideXosScrollViewResult.Removed && !view.ContainsMember(buttons[6]);
        bool extent = view.ContentExtent == 162;
        bool tall = view.MaximumOffset == view.ContentExtent - view.VisibleExtent;
        bool wheelDown = view.HandleWheel(view.InnerX + 4, view.InnerY + 4, -1) ==
            GuideXosScrollViewResult.Scrolled && view.Offset == 3;
        bool wheelUp = view.HandleWheel(view.InnerX + 4, view.InnerY + 4, 1) ==
            GuideXosScrollViewResult.Scrolled && view.Offset == 0;
        bool topClamp = !view.ScrollSmall(-100) && view.Offset == 0;
        bool bottomClamp = view.ScrollSmall(1000) && view.Offset == view.MaximumOffset;
        bool direct = view.SetOffset(12) && view.Offset == 12;
        bool page = view.ScrollPage(1) && view.Offset == 12 + view.VisibleExtent - 1;
        bool repeated = view.SetOffset(view.MaximumOffset) &&
            !view.ScrollPage(1) && !view.ScrollPage(1) &&
            view.Offset >= 0 && view.Offset <= view.MaximumOffset;
        int prior = view.Offset;
        bool shrink = view.TryRemoveMember(buttons[5]) ==
            GuideXosScrollViewResult.Removed && view.Offset <= view.MaximumOffset &&
            view.Offset <= prior;
        bool shortContent = view.TryRemoveMember(buttons[4]) ==
            GuideXosScrollViewResult.Removed && view.MaximumOffset >= 0;
        bool position = view.TrySetMemberPosition(first, 8, 4) ==
            GuideXosScrollViewResult.Moved && view.ContentExtent >= 22;
        bool invalid = view.TryAddMember(new GuideXosTextArea(16, 2, 2, 16), 0, 0) ==
            GuideXosScrollViewResult.InvalidMember;
        bool bounded = view.Offset >= 0 && view.Offset <= view.MaximumOffset;
        return construction && empty && one && many && capacity && duplicate &&
            removal && extent && tall && wheelDown && wheelUp && topClamp &&
            bottomClamp && direct && page && repeated && shrink && shortContent &&
            position && invalid && bounded;
    }

    private static bool ClippingCases()
    {
        GuideXosScrollView view = NewView();
        GuideXosButton above = Button("above");
        GuideXosButton partialTop = Button("partial-top");
        GuideXosLabel visible = new(0, 0, 160, "visible");
        GuideXosButton partialBottom = Button("partial-bottom");
        GuideXosButton below = Button("below");
        bool added = view.TryAddMember(above, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(partialTop, 0, 20) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(visible, 0, 40) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(partialBottom, 0, 70) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(below, 0, 100) == GuideXosScrollViewResult.Added;
        view.SetOffset(23);
        bool fullyAbove = !view.TryGetVisibleMemberBounds(above, out _, out _, out _, out _);
        bool partialAbove = view.TryGetVisibleMemberBounds(partialTop, out _, out int topY,
            out _, out int topHeight) && topY == view.InnerY && topHeight < partialTop.Height;
        bool fullyVisible = view.TryGetVisibleMemberBounds(visible, out _, out int visibleY,
            out _, out int visibleHeight) && visibleY == view.InnerY + 17 && visibleHeight == visible.Height;
        bool partialBelow = view.TryGetVisibleMemberBounds(partialBottom, out _, out _,
            out _, out int bottomHeight) && bottomHeight < partialBottom.Height;
        bool fullyBelow = !view.TryGetVisibleMemberBounds(below, out _, out _, out _, out _);
        bool clipTop = topY == view.InnerY;
        bool clipBottom = bottomHeight > 0 && bottomHeight < partialBottom.Height;
        bool frame = view.BackgroundColor != 0 && view.BorderColor != 0 &&
            view.InnerWidth == view.Width - 2 && view.InnerHeight == view.Height - 2;
        bool shifted = view.TryGetVisibleMemberBounds(visible, out _, out int shiftedY,
            out _, out _) && shiftedY == visibleY;
        bool outside = !view.TryGetVisibleMemberBounds(below, out _, out _, out _, out _);
        return added && fullyAbove && partialAbove && fullyVisible && partialBelow &&
            fullyBelow && clipTop && clipBottom && frame && shifted && outside;
    }

    private static bool HitTestCases()
    {
        GuideXosScrollView view = NewView();
        GuideXosButton first = Button("first");
        GuideXosButton second = Button("second");
        GuideXosButton clipped = Button("clipped");
        GuideXosButton bottomMember = Button("bottom");
        GuideXosCheckBox hidden = new(0, 0, 120, 18, "hidden");
        GuideXosCheckBox disabled = new(0, 0, 120, 18, "disabled");
        bool added = view.TryAddMember(first, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(second, 0, 60) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(clipped, 0, 52) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(bottomMember, 0, 100) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(hidden, 0, 100) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(disabled, 0, 130) == GuideXosScrollViewResult.Added;
        hidden.SetVisible(false);
        disabled.SetEnabled(false);
        bool top = view.TryHitTest(view.InnerX + 4, view.InnerY + 4, out object topTarget) &&
            ReferenceEquals(topTarget, first);
        view.SetOffset(42);
        int secondY = view.InnerY + 60 - view.Offset + 12;
        bool translated = view.TryHitTest(view.InnerX + 4, secondY, out object secondTarget) &&
            ReferenceEquals(secondTarget, second);
        bool sameScreenDifferent = !ReferenceEquals(secondTarget, first);
        bool clippedRegion = !view.TryHitTest(view.InnerX + 4, view.InnerY + 2, out _);
        bool visiblePart = view.TryHitTest(view.InnerX + 4, view.InnerY + 12, out object partialTarget) &&
            ReferenceEquals(partialTarget, clipped);
        bool bottom = view.TryHitTest(view.InnerX + 4, view.InnerY + 35,
            out object bottomTarget) && ReferenceEquals(bottomTarget, second);
        view.SetOffset(view.MaximumOffset);
        bool maxMapping = view.TryHitTest(view.InnerX + 4, view.InnerY + 8, out object maxTarget) &&
            ReferenceEquals(maxTarget, bottomMember);
        bool hiddenSkipped = !ReferenceEquals(maxTarget, hidden);
        bool disabledNoActivate = view.HandlePointerDown(view.InnerX + 4,
            view.InnerY + 40) == GuideXosScrollViewResult.Disabled &&
            ReferenceEquals(view.LastTargetMember, disabled) &&
            view.LastActivatedMember == null;
        bool focus = view.TryFocusMember(second) && view.HasFocus &&
            ReferenceEquals(view.FocusedMember, second);
        return added && top && translated && sameScreenDifferent && clippedRegion &&
            visiblePart && bottom && maxMapping && hiddenSkipped && disabledNoActivate && focus;
    }

    private static bool FocusCases()
    {
        GuideXosScrollView view = NewView();
        GuideXosButton first = Button("first");
        GuideXosButton second = Button("second");
        GuideXosButton third = Button("third");
        GuideXosCheckBox hidden = new(0, 0, 120, 18, "hidden");
        bool added = view.TryAddMember(first, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(second, 0, 70) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(third, 0, 140) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(hidden, 0, 180) == GuideXosScrollViewResult.Added;
        hidden.SetVisible(false);
        third.SetEnabled(false);
        bool tabVisible = view.TryMoveFocus(false) && ReferenceEquals(view.FocusedMember, first);
        bool tabReveal = view.TryMoveFocus(false) && ReferenceEquals(view.FocusedMember, second) &&
            view.TryGetVisibleMemberBounds(second, out _, out _, out _, out _);
        bool skip = !view.TryMoveFocus(false);
        bool shiftReveal = view.TryMoveFocus(true) && ReferenceEquals(view.FocusedMember, first) &&
            view.Offset == 0;
        view.TryFocusMember(first);
        int focusBeforeWheel = view.Offset;
        view.HandleWheel(view.InnerX + 4, view.InnerY + 4, -1);
        bool wheelNoFocus = ReferenceEquals(view.FocusedMember, first) &&
            view.Offset >= focusBeforeWheel;
        bool clickFocus = view.SetOffset(66) &&
            view.HandlePointerDown(view.InnerX + 4, view.InnerY + 4) !=
            GuideXosScrollViewResult.Ignored && view.HasFocus;
        bool shrink = view.TryRemoveMember(third) == GuideXosScrollViewResult.Removed &&
            view.FocusedMember != third && view.Offset <= view.MaximumOffset;
        view.SetVisible(false);
        bool hiddenView = !view.HasFocus && view.HandleWheel(view.InnerX + 2,
            view.InnerY + 2, -1) == GuideXosScrollViewResult.Ignored;
        view.SetVisible(true);
        view.SetEnabled(true);
        view.Reset();
        bool relaunch = view.Offset == 0 && !view.HasFocus;
        return added && tabVisible && tabReveal && skip && shiftReveal && wheelNoFocus &&
            clickFocus && shrink && hiddenView && relaunch;
    }

    private static bool ScrollBarCases()
    {
        GuideXosScrollView view = NewView();
        GuideXosButton member = Button("member");
        bool added = view.TryAddMember(member, 0, 160) == GuideXosScrollViewResult.Added;
        GuideXosScrollBar bar = new(300, 20, 16, 96);
        view.BindScrollBar(bar);
        bool bound = view.HasScrollBarBinding && bar.HasViewportBinding;
        bool initial = bar.Minimum == 0 && bar.Maximum == view.MaximumOffset &&
            bar.PageSize == view.VisibleExtent && bar.Value == view.Offset;
        view.HandleWheel(view.InnerX + 4, view.InnerY + 4, -1);
        bool wheel = bar.Value == view.Offset && bar.ThumbTop >= bar.TrackTop;
        bar.Value = bar.Maximum;
        bool direct = view.Offset == view.MaximumOffset && bar.Value == view.Offset;
        bool page = view.ScrollPage(-1) && view.Offset < view.MaximumOffset &&
            bar.Value == view.Offset;
        int oldValue = bar.Value;
        bool dragPress = bar.HandlePointerDown(bar.X + 4, bar.ThumbTop) ==
            GuideXosScrollBarResult.DragStarted;
        bool dragMove = bar.HandlePointerMove(bar.X + 4, bar.TrackTop + bar.TrackLength - 1) ==
            GuideXosScrollBarResult.Dragged && bar.Value >= oldValue;
        bool dragRelease = bar.HandlePointerUp(bar.X + 4, bar.ThumbTop) ==
            GuideXosScrollBarResult.DragEnded && !bar.IsDragging;
        bool shrink = view.TryRemoveMember(member) == GuideXosScrollViewResult.Removed &&
            view.Offset == 0 && bar.Maximum == 0 && !bar.IsScrollable;
        view.BindScrollBar(bar);
        bool noDuplicateCallback = bar.HasViewportBinding && bar.Value == 0;
        bool finalRange = bar.Minimum <= bar.Value && bar.Value <= bar.Maximum;
        view.UnbindScrollBar();
        bool unbound = !view.HasScrollBarBinding && !bar.HasViewportBinding;
        return added && bound && initial && wheel && direct && page && dragPress &&
            dragMove && dragRelease && shrink && noDuplicateCallback && finalRange && unbound;
    }
}
