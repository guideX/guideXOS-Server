namespace HostLogProof;

/// <summary>Bounded C141 layout, composition, focus, and popup proof.</summary>
public static class GuideXosVerticalStackC141Tests
{
    public const int CoreCaseCount = 20;
    public const int VisibilityCaseCount = 10;
    public const int ScrollViewCaseCount = 10;
    public const int FocusCaseCount = 10;
    public const int PopupCaseCount = 6;
    public const int TotalCaseCount = CoreCaseCount + VisibilityCaseCount +
        ScrollViewCaseCount + FocusCaseCount + PopupCaseCount;

    public static bool Run(GuideXosHost host)
    {
        bool core = CoreCases();
        bool visibility = VisibilityCases();
        bool scrollView = ScrollViewCases();
        bool focus = FocusCases();
        bool popup = PopupCases();
        bool result = core && visibility && scrollView && focus && popup;
        host?.TryLog(result
            ? "C141-FOCUSED core=20 visibility=10 scrollview=10 focus=10 popup=6 total=56 result=PASS"u8
            : "C141-FOCUSED result=FAIL"u8);
        return result;
    }

    private static GuideXosVerticalStack NewStack(int width = 240)
    {
        return new GuideXosVerticalStack(20, 20, width, 8);
    }

    private static GuideXosButton Button(string label = "button", int width = 120)
    {
        return new GuideXosButton(0, 0, width, 18, label);
    }

    private static bool CoreCases()
    {
        GuideXosVerticalStack stack = NewStack();
        bool construction = stack.MemberCount == 0 && stack.ContentHeight == 0;
        bool empty = stack.Clear() == GuideXosVerticalStackResult.Empty;
        GuideXosButton first = Button("first");
        bool add = stack.TryAddMember(first) == GuideXosVerticalStackResult.Added &&
            first.Y == stack.Y && stack.MemberCount == 1;
        GuideXosCheckBox second = new(0, 0, 120, 18, "second");
        GuideXosLabel third = new(0, 0, 120, "third");
        bool order = stack.TryAddMember(second) == GuideXosVerticalStackResult.Added &&
            stack.TryAddMember(third) == GuideXosVerticalStackResult.Added &&
            stack.GetMemberAt(0) == first && stack.GetMemberAt(1) == second &&
            stack.GetMemberAt(2) == third && second.Y > first.Y && third.Y > second.Y;

        GuideXosVerticalStack full = NewStack();
        GuideXosButton[] members = new GuideXosButton[8];
        bool capacity = true;
        for (int index = 0; index < members.Length; index++)
        {
            members[index] = Button("member");
            capacity &= full.TryAddMember(members[index]) ==
                GuideXosVerticalStackResult.Added;
        }
        bool overflow = full.TryAddMember(Button("overflow")) ==
            GuideXosVerticalStackResult.CapacityReached;
        bool duplicate = full.TryAddMember(members[0]) ==
            GuideXosVerticalStackResult.Duplicate;
        bool unsupported = full.TryAddMember(new GuideXosTextArea(0, 0, 2, 16)) ==
            GuideXosVerticalStackResult.InvalidMember;

        GuideXosPanel panel = new(0, 0, 240, 54);
        GuideXosButton panelButton = Button("panel");
        GuideXosPanelResult panelAdd = panel.TryAddChild(panelButton, 0, 0);
        GuideXosVerticalStackResult panelStack = stack.TryAddMember(panelButton);
        bool panelConflict = panelAdd == GuideXosPanelResult.Added &&
            panelStack == GuideXosVerticalStackResult.MembershipConflict;
        GuideXosVerticalStack other = NewStack();
        bool stackConflict = other.TryAddMember(first) ==
            GuideXosVerticalStackResult.MembershipConflict;

        int firstY = first.Y;
        bool defaultSpacing = second.Y - firstY == first.Height + stack.Spacing;
        bool customSpacing = stack.TrySetSpacing(7) &&
            second.Y - first.Y == first.Height + 7;
        bool top = stack.TrySetPadding(11, stack.BottomPadding,
            stack.LeftPadding, stack.RightPadding) && first.Y == stack.Y + 11;
        bool bottom = stack.TrySetPadding(stack.TopPadding, 13,
            stack.LeftPadding, stack.RightPadding) &&
            stack.ContentHeight == 11 + 18 + 7 + 18 + 7 + 18 + 13;
        bool horizontal = stack.TrySetPadding(stack.TopPadding, stack.BottomPadding,
            12, 16) && first.X == stack.X + 12 && first.Width == 212;
        int preservedHeight = first.Height;
        bool fixedHeight = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            first.Height == preservedHeight;
        bool stretch = first.Width == 212;
        GuideXosVerticalStackWidthPolicy oldPolicy = stack.WidthPolicy;
        stack.WidthPolicy = GuideXosVerticalStackWidthPolicy.KeepWidth;
        bool keepWidth = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            first.Width == 212;
        stack.WidthPolicy = oldPolicy;
        bool contentHeight = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stack.ContentHeight == 11 + 18 + 7 + 18 + 7 + 18 + 13;
        int priorThirdY = third.Y;
        bool remove = stack.TryRemoveMember(second) == GuideXosVerticalStackResult.Removed &&
            stack.MemberCount == 2 && third.Y < priorThirdY;
        return construction && empty && add && order && capacity && overflow &&
            duplicate && unsupported && panelConflict && stackConflict &&
            defaultSpacing && customSpacing && top && bottom && horizontal &&
            fixedHeight && stretch && keepWidth && contentHeight && remove;
    }

    private static bool VisibilityCases()
    {
        GuideXosVerticalStack stack = NewStack();
        GuideXosButton a = Button("a");
        GuideXosButton b = Button("b");
        GuideXosButton c = Button("c");
        GuideXosButton d = Button("d");
        stack.TryAddMember(a); stack.TryAddMember(b);
        stack.TryAddMember(c); stack.TryAddMember(d);
        int originalC = c.Y;
        b.SetVisible(false);
        bool middleHidden = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            c.Y < originalC && c.Y == a.Y + a.Height + stack.Spacing;
        a.SetVisible(false);
        bool firstHidden = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            c.Y == stack.Y;
        d.SetVisible(false);
        bool lastHidden = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stack.ContentHeight < originalC + c.Height - stack.Y;
        b.SetVisible(true); d.SetVisible(true);
        bool shown = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            b.Y < c.Y && d.Y > c.Y;
        c.SetEnabled(false);
        int disabledY = c.Y;
        bool disabledRetains = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            c.Y == disabledY && stack.ContentHeight > 0;
        int stableY = d.Y;
        bool stable = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut && d.Y == stableY;
        b.SetVisible(false); c.SetVisible(false);
        bool shrinks = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stack.ContentHeight < stableY - stack.Y + d.Height;
        b.SetVisible(true); c.SetVisible(true);
        bool grows = stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut &&
            stack.ContentHeight > 0 && c.Y > b.Y;
        bool noMembershipMutation = stack.MemberCount == 4 && stack.ContainsMember(b);
        bool toggleCycles = true;
        for (int cycle = 0; cycle < 4; cycle++)
        {
            bool visible = (cycle & 1) == 0;
            b.SetVisible(visible);
            toggleCycles &= stack.PerformLayout() == GuideXosVerticalStackResult.LaidOut;
        }
        b.SetVisible(true);
        return middleHidden && firstHidden && lastHidden && shown && disabledRetains &&
            stable && shrinks && grows && noMembershipMutation && toggleCycles;
    }

    private static GuideXosScrollView MakeScrollView(
        out GuideXosVerticalStack stack, out GuideXosButton[] buttons)
    {
        GuideXosScrollView view = new(20, 20, 240, 56, 8);
        stack = new GuideXosVerticalStack(view.InnerX, view.InnerY,
            view.InnerWidth, 8);
        buttons = new GuideXosButton[8];
        for (int index = 0; index < buttons.Length; index++)
        {
            buttons[index] = Button("scroll");
            view.TryAddMember(buttons[index], 0, 0);
            stack.TryAddMember(buttons[index]);
        }
        stack.TrySetSpacing(4);
        stack.PerformLayout(view);
        return view;
    }

    private static bool ScrollViewCases()
    {
        GuideXosVerticalStack stack;
        GuideXosButton[] buttons;
        GuideXosScrollView view = MakeScrollView(out stack, out buttons);
        bool composed = stack.MemberCount == 8 && view.MemberCount == 8;
        bool extent = view.ContentExtent == stack.ContentHeight &&
            view.ContentExtent > view.VisibleExtent;
        bool tall = view.MaximumOffset > 0;
        GuideXosScrollBar bar = new(264, 20, 16, 56);
        view.BindScrollBar(bar);
        bool wheel = view.HandleWheel(view.InnerX + 4, view.InnerY + 4, -1) ==
            GuideXosScrollViewResult.Scrolled && view.Offset > 0;
        bool scrollbar = bar.Maximum == view.MaximumOffset && bar.Value == view.Offset;
        int oldOffset = view.Offset;
        bool drag = bar.HandlePointerDown(bar.X + 4, bar.ThumbTop) ==
            GuideXosScrollBarResult.DragStarted &&
            bar.HandlePointerMove(bar.X + 4, bar.TrackTop + bar.TrackLength - 1) ==
            GuideXosScrollBarResult.Dragged && view.Offset >= oldOffset;
        bool clip = view.TryGetVisibleMemberBounds(buttons[0], out _, out _,
            out _, out _) == false;
        view.SetOffset(3);
        bool hit = view.TryHitTest(view.InnerX + 4, view.InnerY + 20,
            out object target) && target != null;
        buttons[4].SetVisible(false);
        int oldExtent = view.ContentExtent;
        bool dynamic = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            view.ContentExtent < oldExtent;
        view.SetOffset(view.MaximumOffset);
        buttons[5].SetVisible(false); buttons[6].SetVisible(false);
        bool clamp = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            view.Offset <= view.MaximumOffset;
        bool sync = bar.Value == view.Offset && bar.Maximum == view.MaximumOffset;
        return composed && extent && tall && wheel && scrollbar && drag && clip &&
            hit && dynamic && clamp && sync;
    }

    private static bool FocusCases()
    {
        GuideXosVerticalStack stack;
        GuideXosButton[] buttons;
        GuideXosScrollView view = MakeScrollView(out stack, out buttons);
        bool forward = view.TryMoveFocus(false) && ReferenceEquals(view.FocusedMember, buttons[0]);
        bool lowerReveal = view.TryMoveFocus(false) &&
            ReferenceEquals(view.FocusedMember, buttons[1]) && view.Offset == 0;
        while (view.TryMoveFocus(false)) { }
        bool offscreen = ReferenceEquals(view.FocusedMember, buttons[7]) &&
            view.TryGetVisibleMemberBounds(buttons[7], out _, out _, out _, out _);
        bool hiddenSkipped = true;
        buttons[3].SetVisible(false);
        stack.PerformLayout(view);
        view.Blur();
        view.TryMoveFocus(false);
        view.TryMoveFocus(false);
        view.TryMoveFocus(false);
        hiddenSkipped = !ReferenceEquals(view.FocusedMember, buttons[3]);
        buttons[3].SetVisible(true);
        stack.PerformLayout(view);
        buttons[2].SetEnabled(false);
        view.Blur();
        view.TryMoveFocus(false);
        bool disabledSkipped = !ReferenceEquals(view.FocusedMember, buttons[2]);
        buttons[2].SetEnabled(true);
        view.TryFocusMember(buttons[6]);
        int focusY = buttons[6].Y;
        bool relayoutIdentity = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            ReferenceEquals(view.FocusedMember, buttons[6]) && buttons[6].Y == focusY;
        buttons[5].SetVisible(false);
        bool movedFocused = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            ReferenceEquals(view.FocusedMember, buttons[6]);
        buttons[5].SetVisible(true);
        bool click = view.SetOffset(0) &&
            view.HandlePointerDown(view.InnerX + 4, view.InnerY + 4) !=
            GuideXosScrollViewResult.Ignored &&
            ReferenceEquals(view.FocusedMember, buttons[0]);
        bool shift = view.TryFocusMember(buttons[5]) && view.TryMoveFocus(true) &&
            ReferenceEquals(view.FocusedMember, buttons[4]);
        view.TryFocusMember(buttons[5]);
        bool removal = stack.TryRemoveMember(buttons[4]) ==
            GuideXosVerticalStackResult.Removed && view.ContainsMember(buttons[4]) &&
            ReferenceEquals(view.FocusedMember, buttons[5]);
        bool deterministic = view.Offset >= 0 && view.Offset <= view.MaximumOffset;
        return forward && lowerReveal && offscreen && hiddenSkipped && disabledSkipped &&
            relayoutIdentity && movedFocused && click && shift && removal && deterministic;
    }

    private static bool PopupCases()
    {
        GuideXosScrollView view = new(20, 20, 240, 74, 2);
        GuideXosVerticalStack stack = new(view.InnerX, view.InnerY, view.InnerWidth, 2);
        GuideXosLabel label = new(0, 0, 120, "label");
        GuideXosComboBox combo = new(0, 0, 120, 18, 4, 16, 2);
        combo.TryAddItem("one"); combo.TryAddItem("two");
        bool added = view.TryAddMember(label, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(combo, 0, 0) == GuideXosScrollViewResult.Added &&
            stack.TryAddMember(label) == GuideXosVerticalStackResult.Added &&
            stack.TryAddMember(combo) == GuideXosVerticalStackResult.Added;
        bool firstLayout = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut;
        int arrangedY = combo.Y;
        bool open = combo.Open() == GuideXosComboBoxResult.Opened &&
            combo.PopupY == arrangedY + combo.Height;
        label.SetVisible(false);
        bool relayoutCloses = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut &&
            !combo.IsOpen && combo.Y < arrangedY;
        bool reopenCurrent = combo.Open() == GuideXosComboBoxResult.Opened &&
            combo.PopupY == combo.Y + combo.Height;
        bool followUp = combo.HandlePointerDown(combo.X + 4,
            combo.PopupY + 2) == GuideXosComboBoxResult.SelectionChanged &&
            combo.SelectedIndex == 0;
        bool clean = combo.Close() == GuideXosComboBoxResult.Ignored && !combo.IsOpen;
        return added && firstLayout && open && relayoutCloses && reopenCurrent &&
            followUp && clean;
    }
}
