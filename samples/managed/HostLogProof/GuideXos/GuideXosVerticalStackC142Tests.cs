namespace HostLogProof;

/// <summary>Bounded C142 member metadata, margins, alignment, and composition proof.</summary>
public static class GuideXosVerticalStackC142Tests
{
    public const int MetadataCaseCount = 20;
    public const int SpacingCaseCount = 12;
    public const int HorizontalCaseCount = 12;
    public const int ScrollViewCaseCount = 14;
    public const int PopupControlCaseCount = 10;
    public const int TotalCaseCount = MetadataCaseCount + SpacingCaseCount +
        HorizontalCaseCount + ScrollViewCaseCount + PopupControlCaseCount;

    public static bool Run(GuideXosHost host)
    {
        bool metadata = MetadataCases();
        bool spacing = SpacingCases();
        bool horizontal = HorizontalCases();
        bool scrollView = ScrollViewCases();
        bool popup = PopupControlCases();
        bool result = metadata && spacing && horizontal && scrollView && popup;
        host?.TryLog(result
            ? "C142-FOCUSED metadata=20 spacing=12 horizontal=12 scrollview=14 popup=10 total=68 result=PASS"u8
            : "C142-FOCUSED result=FAIL"u8);
        return result;
    }

    private static GuideXosVerticalStack NewStack(int width = 240,
        int maximumMemberCount = 8)
    {
        return new GuideXosVerticalStack(20, 20, width, maximumMemberCount);
    }

    private static GuideXosButton Button(string label = "button", int width = 80)
    {
        return new GuideXosButton(0, 0, width, 18, label);
    }

    private static bool MetadataCases()
    {
        GuideXosVerticalStack stack = NewStack(200);
        GuideXosButton member = Button("metadata", 80);
        bool added = stack.TryAddMember(member) == GuideXosVerticalStackResult.Added;
        bool defaults = member.MarginLeft == 0 && member.MarginTop == 0 &&
            member.MarginRight == 0 && member.MarginBottom == 0 &&
            member.HorizontalAlignment ==
                GuideXosVerticalStackHorizontalAlignment.Stretch;
        member.MarginLeft = 5;
        bool left = member.MarginLeft == 5;
        member.MarginTop = 10;
        bool top = member.MarginTop == 10;
        member.MarginRight = 7;
        bool right = member.MarginRight == 7;
        member.MarginBottom = 11;
        bool bottom = member.MarginBottom == 11;
        bool all = member.TrySetMargins(6, 8, 10, 12) &&
            member.MarginLeft == 6 && member.MarginTop == 8 &&
            member.MarginRight == 10 && member.MarginBottom == 12;
        int beforeInvalid = member.MarginLeft;
        bool invalid = !member.TrySetMargins(-1, 0, 0, 0) &&
            member.MarginLeft == beforeInvalid;
        member.MarginLeft = -5;
        member.MarginRight = GuideXosVerticalStackMemberLayoutRules.MaximumSupportedMargin + 1;
        bool clamped = member.MarginLeft == 0 &&
            member.MarginRight == GuideXosVerticalStackMemberLayoutRules.MaximumSupportedMargin;
        bool defaultAlignment = member.HorizontalAlignment ==
            GuideXosVerticalStackHorizontalAlignment.Stretch;
        member.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        bool leftAlignment = member.HorizontalAlignment ==
            GuideXosVerticalStackHorizontalAlignment.Left;
        member.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Center;
        bool centerAlignment = member.HorizontalAlignment ==
            GuideXosVerticalStackHorizontalAlignment.Center;
        member.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        bool rightAlignment = member.HorizontalAlignment ==
            GuideXosVerticalStackHorizontalAlignment.Right;
        bool badAlignment = !member.TrySetHorizontalAlignment((GuideXosVerticalStackHorizontalAlignment)99) &&
            member.HorizontalAlignment == GuideXosVerticalStackHorizontalAlignment.Right;
        member.TrySetMargins(5, 8, 7, 11);
        member.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Center;
        bool relayout = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            member.X == 79 && member.Y == 28 && member.Width == 80 &&
            stack.ContentHeight == 37;
        int stableX = member.X;
        int stableWidth = member.Width;
        bool repeated = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            member.X == stableX && member.Width == stableWidth;
        member.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Stretch;
        bool stretchTransition = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            member.Width == 188 && member.X == 25;
        member.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        bool fixedTransition = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            member.Width == 80 && member.X == 25;
        member.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        bool sameAssignment = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            member.X == 25 && member.Width == 80;

        GuideXosButton remaining = Button("remaining", 72);
        stack.TryAddMember(remaining);
        remaining.TrySetMargins(2, 3, 4, 5);
        remaining.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        bool removal = stack.TryRemoveMember(member) ==
            GuideXosVerticalStackResult.Removed &&
            remaining.MarginLeft == 2 && remaining.MarginTop == 3 &&
            remaining.MarginRight == 4 && remaining.MarginBottom == 5 &&
            remaining.HorizontalAlignment == GuideXosVerticalStackHorizontalAlignment.Right;
        return added && defaults && left && top && right && bottom && all &&
            invalid && clamped && defaultAlignment && leftAlignment &&
            centerAlignment && rightAlignment && badAlignment && relayout &&
            repeated && stretchTransition && fixedTransition && sameAssignment &&
            removal;
    }

    private static bool SpacingCases()
    {
        GuideXosVerticalStack stack = NewStack(200);
        GuideXosButton a = Button("a");
        GuideXosButton b = Button("b");
        GuideXosButton c = Button("c");
        GuideXosButton d = Button("d");
        a.TrySetMargins(0, 1, 0, 2);
        b.TrySetMargins(0, 3, 0, 4);
        c.TrySetMargins(0, 5, 0, 6);
        d.TrySetMargins(0, 7, 0, 8);
        bool added = stack.TryAddMember(a) == GuideXosVerticalStackResult.Added &&
            stack.TryAddMember(b) == GuideXosVerticalStackResult.Added &&
            stack.TryAddMember(c) == GuideXosVerticalStackResult.Added &&
            stack.TryAddMember(d) == GuideXosVerticalStackResult.Added;
        bool padding = stack.TrySetPadding(3, 5, 4, 4) && stack.TrySetSpacing(2);
        bool first = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            a.Y == 24;
        bool adjacent = b.Y == 49 && c.Y == 78 && d.Y == 111;
        bool lastMargin = stack.ContentHeight == 122;
        b.SetVisible(false);
        bool hiddenMiddle = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            c.Y == 51;
        c.SetVisible(false);
        bool hiddenConsecutive = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            d.Y == 53;
        a.SetVisible(false);
        bool hiddenFirst = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            d.Y == 30;
        d.SetVisible(false);
        bool hiddenLast = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stack.ContentHeight == 8;
        a.SetVisible(true); b.SetVisible(true); c.SetVisible(true); d.SetVisible(true);
        bool showAgain = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            a.Y == 24 && b.Y == 49 && c.Y == 78 && d.Y == 111;
        int fullHeight = stack.ContentHeight;
        c.SetEnabled(false);
        bool disabled = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            c.Y == 78 && stack.ContentHeight == fullHeight;
        b.SetVisible(false); c.SetVisible(false);
        bool shrink = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stack.ContentHeight < fullHeight;
        b.SetVisible(true); c.SetVisible(true);
        bool grow = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stack.ContentHeight == fullHeight;
        return added && padding && first && adjacent && lastMargin && hiddenMiddle &&
            hiddenConsecutive && hiddenFirst && hiddenLast && showAgain && disabled &&
            shrink && grow;
    }

    private static bool HorizontalCases()
    {
        GuideXosVerticalStack stack = NewStack(200);
        GuideXosButton stretch = Button("stretch", 80);
        bool stretchAdded = stack.TryAddMember(stretch) == GuideXosVerticalStackResult.Added &&
            stretch.X == 20 && stretch.Width == 200;
        stretch.TrySetMargins(0, 0, 0, 0);
        bool stretchNoMargins = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stretch.Width == 200;
        stretch.TrySetMargins(8, 0, 12, 0);
        bool stretchMargins = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stretch.X == 28 && stretch.Width == 180;
        stretch.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        bool left = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stretch.X == 28 && stretch.Width == 80;
        stretch.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Center;
        bool center = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stretch.X == 78 && stretch.Width == 80;
        stretch.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        bool right = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stretch.X == 128 && stretch.Width == 80;
        GuideXosVerticalStack narrow = NewStack(50);
        GuideXosButton tiny = Button("tiny", 32);
        narrow.TryAddMember(tiny);
        tiny.TrySetMargins(8, 0, 8, 0);
        tiny.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Stretch;
        bool narrowRegion = narrow.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            tiny.Width == 34;
        int oldWidth = tiny.Width;
        tiny.TrySetMargins(30, 0, 30, 0);
        bool excessiveMargins = narrow.PerformLayout() == GuideXosVerticalStackResult.LayoutFailed &&
            tiny.Width == oldWidth;
        GuideXosVerticalStack largeStack = NewStack(100);
        GuideXosButton large = Button("large", 240);
        largeStack.TryAddMember(large);
        large.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        bool largeFixed = largeStack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            large.Width == 240 && large.X == 20;
        large.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        bool largeRightRejected = largeStack.PerformLayout() ==
            GuideXosVerticalStackResult.LayoutFailed && large.Width == 240;
        bool resized = stack.TrySetFrame(20, 20, 240) &&
            stretch.HorizontalAlignment == GuideXosVerticalStackHorizontalAlignment.Right &&
            stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stretch.X == 168 && stretch.Width == 80;
        GuideXosVerticalStack mixed = NewStack(240, 4);
        GuideXosButton leftMember = Button("left", 64);
        GuideXosButton centerMember = Button("center", 64);
        GuideXosButton rightMember = Button("right", 64);
        GuideXosSeparator separator = new(0, 0, 64);
        mixed.TryAddMember(leftMember); mixed.TryAddMember(centerMember);
        mixed.TryAddMember(rightMember); mixed.TryAddMember(separator);
        leftMember.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        centerMember.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Center;
        rightMember.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        separator.TrySetMargins(8, 0, 8, 0);
        bool mixedAlignments = mixed.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            leftMember.X == 20 && centerMember.X == 108 && rightMember.X == 196;
        bool separatorInset = separator.Width == 224 && separator.X == 28;
        return stretchAdded && stretchNoMargins && stretchMargins && left && center &&
            right && narrowRegion && excessiveMargins && largeFixed && largeRightRejected &&
            resized && mixedAlignments && separatorInset;
    }

    private static bool ScrollViewCases()
    {
        GuideXosScrollView view = new(20, 20, 180, 56, 6);
        GuideXosVerticalStack stack = new(view.InnerX, view.InnerY,
            view.InnerWidth, 6);
        GuideXosButton left = Button("left", 48);
        GuideXosButton center = Button("center", 48);
        GuideXosButton right = Button("right", 48);
        GuideXosButton stretch = Button("stretch", 48);
        GuideXosButton extra = Button("extra", 48);
        GuideXosButton focus = Button("focus", 48);
        left.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        center.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Center;
        right.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        stretch.TrySetMargins(8, 8, 8, 8);
        bool added = view.TryAddMember(left, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(center, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(right, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(stretch, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(extra, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(focus, 0, 0) == GuideXosScrollViewResult.Added;
        stack.TryAddMember(left); stack.TryAddMember(center); stack.TryAddMember(right);
        stack.TryAddMember(stretch); stack.TryAddMember(extra); stack.TryAddMember(focus);
        GuideXosScrollBar bar = new(204, 20, 16, 56);
        view.BindScrollBar(bar);
        bool arranged = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            view.ContentExtent == stack.ContentHeight && view.MaximumOffset > 0;
        bool horizontal = left.X == view.InnerX &&
            center.X == view.InnerX + (view.InnerWidth - center.Width) / 2 &&
            right.X + right.Width == view.InnerX + view.InnerWidth &&
            stretch.Width == view.InnerWidth - 16;
        int leftX = left.X;
        int centerX = center.X;
        int rightX = right.X;
        bool wheel = view.HandleWheel(view.InnerX + 4, view.InnerY + 4, -1) ==
            GuideXosScrollViewResult.Scrolled && view.Offset > 0;
        bool unchangedX = left.X == leftX && center.X == centerX && right.X == rightX;
        bool scrollbar = bar.Maximum == view.MaximumOffset && bar.Value == view.Offset;
        object hit = null;
        bool translatedHit = view.TryGetVisibleMemberBounds(left, out int focusX,
            out int focusY, out int focusWidth, out int focusHeight) &&
            view.TryHitTest(focusX + focusWidth / 2, focusY + focusHeight / 2,
                out hit) && ReferenceEquals(hit, left);
        int oldExtent = view.ContentExtent;
        extra.SetVisible(false);
        view.SetOffset(view.MaximumOffset);
        bool shrink = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            view.ContentExtent < oldExtent && view.Offset <= view.MaximumOffset;
        bool clamp = bar.Value == view.Offset && bar.Maximum == view.MaximumOffset;
        extra.SetVisible(true);
        bool grow = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            view.ContentExtent == oldExtent;
        focus.TrySetMargins(0, 14, 0, 16);
        bool focusReveal = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            view.TryFocusMember(focus) &&
            view.TryGetVisibleMemberBounds(focus, out _, out _, out _, out _);
        bool visibleClip = view.TryGetVisibleMemberBounds(extra, out _, out _, out _, out _);
        return added && arranged && horizontal && wheel && unchangedX && scrollbar &&
            translatedHit && shrink && clamp && grow && focusReveal && visibleClip;
    }

    private static bool PopupControlCases()
    {
        GuideXosScrollView view = new(20, 20, 240, 72, 6);
        GuideXosVerticalStack stack = new(view.InnerX, view.InnerY,
            view.InnerWidth, 6);
        GuideXosComboBox combo = new(0, 0, 120, 18, 4, 16, 2);
        GuideXosButton button = Button("popup button", 96);
        GuideXosCheckBox check = new(0, 0, 120, 18, "check");
        GuideXosRadioButton radioOne = new(0, 0, 120, 18, "one", true);
        GuideXosRadioButton radioTwo = new(0, 0, 120, 18, "two");
        GuideXosProgressBar progress = new(0, 0, 120, 0, 100, 65);
        GuideXosRadioGroup group = new(2);
        group.TryRegister(radioOne); group.TryRegister(radioTwo);
        combo.TryAddItem("one"); combo.TryAddItem("two");
        combo.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Center;
        button.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        progress.TrySetMargins(8, 0, 8, 0);
        bool added = view.TryAddMember(combo, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(button, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(check, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(radioOne, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(radioTwo, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(progress, 0, 0) == GuideXosScrollViewResult.Added;
        stack.TryAddMember(combo); stack.TryAddMember(button); stack.TryAddMember(check);
        stack.TryAddMember(radioOne); stack.TryAddMember(radioTwo); stack.TryAddMember(progress);
        bool arranged = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut;
        bool centered = combo.X > view.InnerX &&
            combo.X + combo.Width < view.InnerX + view.InnerWidth;
        int popupY = combo.Y + combo.Height;
        bool open = combo.Open() == GuideXosComboBoxResult.Opened &&
            combo.PopupY == popupY;
        bool followUp = combo.HandlePointerDown(combo.X + 4, combo.PopupY + 2) ==
            GuideXosComboBoxResult.SelectionChanged && combo.SelectedIndex == 0;
        bool rightButton = button.X + button.Width == view.InnerX + view.InnerWidth &&
            button.HandlePointerDown(button.X + 4, button.Y + 4) ==
                GuideXosButtonResult.Activated;
        check.SetChecked(true);
        bool checkbox = check.Checked;
        bool radio = radioTwo.HandlePointerDown(radioTwo.X + 4, radioTwo.Y + 4) ==
            GuideXosRadioButtonResult.Selected && radioTwo.Checked && !radioOne.Checked;
        int value = progress.Value;
        bool progressState = progress.Width == ((view.InnerWidth - 16) / 8) * 8 &&
            progress.Value == value;
        int oldButtonY = button.Y;
        combo.Close();
        combo.SetVisible(false);
        bool relayout = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            button.Y < oldButtonY && view.ContentExtent == stack.ContentHeight;
        combo.SetVisible(true);
        bool restore = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            button.Y == oldButtonY;
        bool clean = !combo.IsOpen;
        return added && arranged && centered && open && followUp && rightButton &&
            checkbox && radio && progressState && relayout && restore && clean;
    }
}
