using System;

namespace HostLogProof;

/// <summary>Focused C143 proofs for bounded section membership and composition.</summary>
public static class GuideXosGroupBoxC143Tests
{
    private static int s_lastFailedCase;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_lastFailedCase = 0;
        bool core = Core(host, surface, out int coreCases);
        Log(host, core ? "C143-CORE core=20 result=PASS" : "C143-CORE result=FAIL");
        s_lastFailedCase = 0;
        bool composition = Composition(host, out int compositionCases);
        Log(host, composition
            ? "C143-COMPOSITION cases=10 result=PASS"
            : "C143-COMPOSITION result=FAIL");
        if (!composition)
            LogNumber(host, "C143-DEBUG composition-failedCase="u8, s_lastFailedCase);
        s_lastFailedCase = 0;
        bool rendering = Rendering(host, surface, out int renderingCases);
        Log(host, rendering
            ? "C143-RENDER-SCROLL cases=10 result=PASS"
            : "C143-RENDER-SCROLL result=FAIL");
        if (!rendering)
            LogNumber(host, "C143-DEBUG rendering-failedCase="u8, s_lastFailedCase);
        s_lastFailedCase = 0;
        bool focus = FocusAndLifecycle(out int focusCases);
        Log(host, focus
            ? "C143-FOCUS-LIFECYCLE cases=10 result=PASS"
            : "C143-FOCUS-LIFECYCLE result=FAIL");
        if (!focus)
            LogNumber(host, "C143-DEBUG focus-failedCase="u8, s_lastFailedCase);
        s_lastFailedCase = 0;
        bool popup = Popup(out int popupCases);
        Log(host, popup
            ? "C143-POPUP cases=8 result=PASS"
            : "C143-POPUP result=FAIL");
        if (!popup)
            LogNumber(host, "C143-DEBUG popup-failedCase="u8, s_lastFailedCase);
        s_lastFailedCase = 0;
        bool dynamic = DynamicLayout(out int dynamicCases);
        Log(host, dynamic
            ? "C143-DYNAMIC-LAYOUT cases=10 result=PASS"
            : "C143-DYNAMIC-LAYOUT result=FAIL");
        if (!dynamic)
            LogNumber(host, "C143-DEBUG dynamic-failedCase="u8, s_lastFailedCase);
        bool passed = core && composition && rendering && focus && popup && dynamic &&
            coreCases == 20 && compositionCases == 10 && renderingCases == 10 &&
            focusCases == 10 && popupCases == 8 && dynamicCases == 10;
        Log(host, passed
            ? "C143-TESTS core=20 composition=10 rendering=10 focus=10 popup=8 dynamic=10 total=68 result=PASS"
            : "C143-TESTS result=FAIL");
        return passed;
    }

    private static bool Case(ref int count, bool value)
    {
        ++count;
        if (!value) s_lastFailedCase = count;
        return value;
    }

    private static bool Core(GuideXosHost host, GuideXosSurface surface,
        out int count)
    {
        count = 0;
        bool result = true;
        GuideXosGroupBox group = new(20, 20, 240, 180);
        result &= Case(ref count, group.X == 20 && group.Y == 20 &&
            group.Width == 240 && group.Height == 180);
        result &= Case(ref count, group.Text.Length == 0 && group.Visible && group.Enabled);
        result &= Case(ref count, !group.Focusable && group.MemberCount == 0);
        result &= Case(ref count, group.MemberCapacity == 8 &&
            GuideXosGroupBox.MaximumSupportedMemberCount == 8);
        result &= Case(ref count, group.ContentPadding == 8);
        result &= Case(ref count, group.TryGetContentRectangle(out int left,
            out int top, out int width, out int height) && left == 36 &&
            top == 46 && width == 208 && height == 128);
        result &= Case(ref count, group.TrySetText("Network") && group.Caption == "Network");
        string maximum = new('T', GuideXosGroupBox.MaximumSupportedCaptionLength);
        result &= Case(ref count, group.TrySetText(maximum) && group.Text.Length == 48);
        result &= Case(ref count, !group.TrySetText(maximum + "X") && group.Text == maximum);
        result &= Case(ref count, !group.TrySetContentPadding(-1) && group.ContentPadding == 8);
        result &= Case(ref count, group.TrySetContentPadding(4) &&
            group.ContentLeft == 32 && group.ContentTop == 42);

        GuideXosCheckBox check = new(40, 50, 120, 18, "Keep callback", true);
        Action<bool> callback = _ => { };
        check.Changed = callback;
        GuideXosControlHost membershipHost = new(1);
        bool registered = membershipHost.TryRegisterCheckBox(41, check) ==
            GuideXosControlHostResult.Registered;
        result &= Case(ref count, group.TryAddMember(check) == GuideXosGroupBoxResult.Added &&
            ReferenceEquals(check.ParentGroupBox, group));
        result &= Case(ref count, group.TryAddMember(check) == GuideXosGroupBoxResult.Duplicate);
        GuideXosGroupBox second = new(20, 20, 240, 180, "Second");
        result &= Case(ref count, second.TryAddMember(check) ==
            GuideXosGroupBoxResult.MembershipConflict);
        GuideXosLabel label = new(40, 70, 120, "A second member");
        result &= Case(ref count, group.TryAddMember(label) == GuideXosGroupBoxResult.Added &&
            group.MemberCount == 2);
        for (int index = 0; index < 6; index++)
        {
            GuideXosLabel extra = new(40, 90 + index * 18, 120, "extra");
            result &= group.TryAddMember(extra) == GuideXosGroupBoxResult.Added;
        }
        result &= Case(ref count, group.MemberCount == 8 &&
            group.TryAddMember(new GuideXosLabel(40, 200, 120, "overflow")) ==
                GuideXosGroupBoxResult.CapacityReached);
        result &= Case(ref count, group.TryAddMember(new GuideXosPanel(20, 20, 160, 54)) ==
            GuideXosGroupBoxResult.InvalidMember);
        result &= Case(ref count, registered && membershipHost.RegistrationCount == 1 &&
            ReferenceEquals(check.Changed, callback));
        result &= Case(ref count, group.TryRemoveMember(check) == GuideXosGroupBoxResult.Removed &&
            check.ParentGroupBox == null && check.Changed == callback &&
            membershipHost.RegistrationCount == 1);
        bool rendered = group.Render(surface) == GuideXosResult.Success;
        int oldX = group.X;
        int oldY = group.Y;
        string oldText = group.Text;
        rendered &= group.Render(surface) == GuideXosResult.Success &&
            group.X == oldX && group.Y == oldY && group.Text == oldText;
        result &= Case(ref count, rendered && group.Focusable == false);
        return result && count == 20;
    }

    private static bool Composition(GuideXosHost host, out int count)
    {
        count = 0;
        bool result = true;
        GuideXosGroupBox group = new(20, 20, 240, 180, "Compose");
        GuideXosButton first = new(0, 0, 100, 18, "Left");
        GuideXosButton hidden = new(0, 0, 100, 18, "Hidden");
        GuideXosButton last = new(0, 0, 100, 18, "Right");
        result &= Case(ref count, group.TryAddMember(first) == GuideXosGroupBoxResult.Added &&
            group.TryAddMember(hidden) == GuideXosGroupBoxResult.Added &&
            group.TryAddMember(last) == GuideXosGroupBoxResult.Added);
        GuideXosScrollView view = new(20, 20, 280, 88, 9);
        result &= Case(ref count, view.TryAddMember(group, 4, 4) == GuideXosScrollViewResult.Added);
        GuideXosVerticalStack stack = new(group.ContentLeft, group.ContentTop,
            group.ContentWidth, 4);
        result &= Case(ref count, stack.TryAddMember(first) == GuideXosVerticalStackResult.Added &&
            stack.TryAddMember(hidden) == GuideXosVerticalStackResult.Added &&
            stack.TryAddMember(last) == GuideXosVerticalStackResult.Added);
        result &= Case(ref count, view.TryAddMember(first, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(hidden, 0, 0) == GuideXosScrollViewResult.Added &&
            view.TryAddMember(last, 0, 0) == GuideXosScrollViewResult.Added);
        first.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Left;
        last.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        first.TrySetMargins(4, 2, 4, 3);
        result &= Case(ref count, first.ParentGroupBox == group &&
            first.ParentScrollView == view && first.VerticalStackOwner == stack);
        result &= Case(ref count, stack.PerformLayout(view) ==
            GuideXosVerticalStackResult.LaidOut && stack.X == group.ContentLeft &&
            stack.Y == group.ContentTop && stack.Width == group.ContentWidth);
        result &= Case(ref count, first.MarginLeft == 4 && first.MarginBottom == 3 &&
            first.X == stack.X + stack.LeftPadding + 4);
        result &= Case(ref count, last.X + last.Width ==
            stack.X + stack.Width - stack.RightPadding - last.MarginRight);
        int frameX = group.X;
        int frameY = group.Y;
        int priorLastY = last.Y;
        hidden.SetVisible(false);
        bool relaid = stack.PerformLayout(view) == GuideXosVerticalStackResult.LaidOut;
        result &= Case(ref count, relaid && last.Y < priorLastY &&
            group.X == frameX && group.Y == frameY);
        result &= Case(ref count, view.MemberCount == 4 && stack.MemberCount == 3 &&
            group.MemberCount == 3 && view.ContentExtent > view.VisibleExtent &&
            view.Offset >= 0 && view.Offset <= view.MaximumOffset &&
            group.TryRemoveMember(hidden) == GuideXosGroupBoxResult.Removed &&
            hidden.ParentGroupBox == null && hidden.ParentScrollView == view &&
            hidden.VerticalStackOwner == stack && stack.PerformLayout() ==
                GuideXosVerticalStackResult.LaidOut && last.Y >= first.Y);
        return result && count == 10;
    }

    private static bool Rendering(GuideXosHost host, GuideXosSurface surface, out int count)
    {
        count = 0;
        bool result = true;
        Log(host, "C143-RENDER-SCROLL stage=setup-start");
        GuideXosGroupBox group = new(20, 20, 240, 180, "Clipped Section");
        Log(host, "C143-RENDER-SCROLL stage=first-frame-created");
        GuideXosButton button = new(0, 0, 100, 18, "Visible member");
        Log(host, "C143-RENDER-SCROLL stage=first-button-created");
        GuideXosGroupBox second = new(20, 20, 240, 180, "Second frame");
        Log(host, "C143-RENDER-SCROLL stage=second-frame-created");
        GuideXosButton lower = new(0, 0, 100, 18, "Lower member");
        Log(host, "C143-RENDER-SCROLL stage=lower-button-created");
        Log(host, "C143-RENDER-SCROLL stage=first-member-adding");
        group.TryAddMember(button);
        Log(host, "C143-RENDER-SCROLL stage=first-member-added");
        second.TryAddMember(lower);
        Log(host, "C143-RENDER-SCROLL stage=second-member-added");
        GuideXosScrollView view = new(20, 20, 280, 88, 9);
        Log(host, "C143-RENDER-SCROLL stage=viewport-created");
        view.TryAddMember(group, 4, 0);
        Log(host, "C143-RENDER-SCROLL stage=first-frame-positioned");
        view.TryAddMember(button, 20, 36);
        Log(host, "C143-RENDER-SCROLL stage=button-positioned");
        view.TryAddMember(second, 4, 204);
        Log(host, "C143-RENDER-SCROLL stage=second-frame-positioned");
        view.TryAddMember(lower, 20, 240);
        Log(host, "C143-RENDER-SCROLL stage=render-start");
        result &= Case(ref count, surface.TryFillRect(view.X, view.Y,
            view.Width, view.Height, view.BackgroundColor) == GuideXosResult.Success &&
            group.Render(surface) == GuideXosResult.Success);
        Log(host, "C143-RENDER-SCROLL stage=frame-rendered");
        result &= Case(ref count, view.Render(surface) == GuideXosResult.Success);
        Log(host, "C143-RENDER-SCROLL stage=initial-view-rendered");
        result &= Case(ref count, view.ContentExtent > view.VisibleExtent &&
            view.MaximumOffset > 0);
        view.SetOffset(24);
        result &= Case(ref count, view.Render(surface) == GuideXosResult.Success &&
            view.TryGetVisibleMemberBounds(group, out _, out _, out _, out _));
        Log(host, "C143-RENDER-SCROLL stage=partial-top-rendered");
        view.SetOffset(view.MaximumOffset);
        result &= Case(ref count, view.Render(surface) == GuideXosResult.Success &&
            view.TryGetVisibleMemberBounds(second, out _, out _, out _, out _));
        Log(host, "C143-RENDER-SCROLL stage=partial-bottom-rendered");
        view.SetOffset(24);
        result &= Case(ref count, view.TryGetVisibleMemberBounds(button,
            out _, out _, out _, out _) && view.TryGetVisibleMemberBounds(group,
                out _, out _, out _, out _));
        view.SetOffset(204);
        result &= Case(ref count, view.TryGetVisibleMemberBounds(lower,
            out int lowerX, out int lowerY, out int lowerWidth, out int lowerHeight) &&
            lowerWidth > 0 && lowerHeight > 0 && view.TryHitTest(
                lowerX + lowerWidth / 2, lowerY + lowerHeight / 2, out object hit) &&
            ReferenceEquals(hit, lower));
        Log(host, "C143-RENDER-SCROLL stage=lower-hit-tested");
        view.SetOffset(24);
        int emptyX = group.X + 1;
        int emptyY = view.InnerY + 5;
        bool emptyTarget = !view.TryHitTest(emptyX, emptyY, out object emptyHit) ||
            ReferenceEquals(emptyHit, group);
        result &= Case(ref count, emptyTarget &&
            view.HandlePointerDown(emptyX, emptyY) == GuideXosScrollViewResult.Ignored &&
            view.LastActivatedMember == null);
        Log(host, "C143-RENDER-SCROLL stage=empty-frame-checked");
        int beforeWheel = view.Offset;
        result &= Case(ref count, view.HandleWheel(view.InnerX + 4,
            view.InnerY + 4, -1) == GuideXosScrollViewResult.Scrolled &&
            view.Offset > beforeWheel && view.Render(surface) == GuideXosResult.Success);
        Log(host, "C143-RENDER-SCROLL stage=wheel-rendered");
        GuideXosScrollBar bar = new(304, 20, 16, 88);
        view.BindScrollBar(bar);
        int beforeDrag = view.Offset;
        bool dragged = bar.HandlePointerDown(bar.X + 4, bar.ThumbTop) ==
                GuideXosScrollBarResult.DragStarted &&
            bar.HandlePointerMove(bar.X + 4, bar.TrackTop + bar.TrackLength - 1) ==
                GuideXosScrollBarResult.Dragged &&
            bar.HandlePointerUp(bar.X + 4, bar.TrackTop + bar.TrackLength - 1) ==
                GuideXosScrollBarResult.DragEnded;
        result &= Case(ref count, dragged && view.Offset >= beforeDrag &&
            !bar.IsDragging && view.Render(surface) == GuideXosResult.Success);
        Log(host, "C143-RENDER-SCROLL stage=scrollbar-rendered");
        return result && count == 10;
    }

    private static bool FocusAndLifecycle(out int count)
    {
        count = 0;
        bool result = true;
        GuideXosGroupBox group = new(0, 0, 240, 180, "Focus");
        GuideXosButton first = new(0, 0, 100, 18, "First");
        GuideXosCheckBox second = new(0, 0, 100, 18, "Second", true);
        GuideXosComboBox third = new(0, 0, 100, 18, 4, 16, 2);
        third.TryAddItem("One");
        third.TryAddItem("Two");
        group.TryAddMember(first);
        group.TryAddMember(second);
        group.TryAddMember(third);
        GuideXosScrollView view = new(20, 20, 280, 88, 9);
        view.TryAddMember(group, 4, 0);
        view.TryAddMember(first, 0, 36);
        view.TryAddMember(second, 0, 60);
        view.TryAddMember(third, 0, 84);
        GuideXosControlHost host = new(1);
        host.TryRegisterScrollView(1, view, true);
        result &= Case(ref count, !view.TryFocusMember(group) && !group.Focusable);
        result &= Case(ref count, view.TryMoveFocus(false) &&
            ReferenceEquals(view.FocusedMember, first));
        result &= Case(ref count, view.TryMoveFocus(false) &&
            ReferenceEquals(view.FocusedMember, second));
        result &= Case(ref count, view.TryMoveFocus(false) &&
            ReferenceEquals(view.FocusedMember, third) && view.Offset > 0);
        result &= Case(ref count, view.TryMoveFocus(true) &&
            ReferenceEquals(view.FocusedMember, second));
        second.SetVisible(false);
        result &= Case(ref count, view.TryMoveFocus(true) &&
            ReferenceEquals(view.FocusedMember, first));
        second.SetVisible(true);
        second.SetEnabled(false);
        result &= Case(ref count, !second.EffectiveEnabled &&
            view.TryMoveFocus(false) && ReferenceEquals(view.FocusedMember, third));
        group.SetVisible(false);
        result &= Case(ref count, !first.EffectiveVisible && !third.EffectiveVisible &&
            !view.HasFocus);
        group.SetVisible(true);
        second.SetEnabled(false);
        group.SetEnabled(false);
        group.SetEnabled(true);
        result &= Case(ref count, second.Enabled == false &&
            second.EffectiveEnabled == false && first.EffectiveEnabled);
        result &= Case(ref count, host.TryFocus(1) == GuideXosControlHostResult.Focused &&
            host.RegistrationCount == 1 && view.MemberCount == 4 &&
            group.MemberCount == 3);
        return result && count == 10;
    }

    private static bool Popup(out int count)
    {
        count = 0;
        bool result = true;
        GuideXosGroupBox group = new(20, 20, 240, 180, "Popup");
        GuideXosComboBox combo = new(0, 0, 160, 18, 4, 16, 2);
        combo.TryAddItem("Default");
        combo.TryAddItem("Detailed");
        group.TryAddMember(combo);
        GuideXosVerticalStack stack = new(group.ContentLeft, group.ContentTop,
            group.ContentWidth, 1);
        stack.TryAddMember(combo);
        GuideXosScrollView view = new(20, 20, 280, 88, 9);
        view.TryAddMember(group, 4, 4);
        view.TryAddMember(combo, 0, 0);
        stack.PerformLayout(view);
        view.SetOffset(18);
        GuideXosControlHost host = new(1);
        host.TryRegisterComboBox(8, combo, true);
        result &= Case(ref count, host.TryFocus(8) == GuideXosControlHostResult.Focused);
        combo.TrySetSelectedIndex(1);
        result &= Case(ref count, combo.Open() == GuideXosComboBoxResult.Opened &&
            combo.PopupY == combo.Y + combo.Height && combo.EffectiveVisible);
        result &= Case(ref count, host.TryAcquireTransientInputCapture(8) &&
            host.HasTransientInputCapture && host.TransientInputCaptureOwnerId == 8);
        result &= Case(ref count, combo.HandlePointerDown(combo.X + 1,
            combo.PopupY + 1) == GuideXosComboBoxResult.SelectionChanged &&
            combo.SelectedIndex == 0);
        combo.Open();
        host.TryAcquireTransientInputCapture(8);
        group.SetEnabled(false);
        result &= Case(ref count, !combo.EffectiveEnabled &&
            !host.HasTransientInputCapture && !combo.IsOpen);
        group.SetEnabled(true);
        result &= Case(ref count, combo.EffectiveEnabled && !combo.IsOpen &&
            !host.HasTransientInputCapture);
        combo.Open();
        host.TryAcquireTransientInputCapture(8);
        group.SetVisible(false);
        result &= Case(ref count, !combo.EffectiveVisible &&
            !host.HasTransientInputCapture && !combo.IsOpen);
        result &= Case(ref count, host.TransientInputCaptureKind ==
            GuideXosManagedControlKind.None && stack.MemberCount == 1 &&
            view.MemberCount == 2);
        return result && count == 8;
    }

    private static bool DynamicLayout(out int count)
    {
        count = 0;
        bool result = true;
        GuideXosGroupBox group = new(20, 20, 240, 198, "Dynamic");
        GuideXosCheckBox toggle = new(0, 0, 180, 18, "Show progress", true);
        GuideXosProgressBar progress = new(0, 0, 180, 0, 100, 50);
        GuideXosButton button = new(0, 0, 120, 18, "Apply");
        group.TryAddMember(toggle);
        group.TryAddMember(progress);
        group.TryAddMember(button);
        toggle.TrySetMargins(0, 2, 0, 2);
        progress.TrySetMargins(8, 2, 8, 2);
        button.TrySetMargins(0, 2, 0, 2);
        button.HorizontalAlignment = GuideXosVerticalStackHorizontalAlignment.Right;
        GuideXosScrollView view = new(20, 20, 280, 88, 9);
        view.TryAddMember(group, 4, 4);
        GuideXosVerticalStack stack = new(group.ContentLeft, group.ContentTop,
            group.ContentWidth, 3);
        stack.TrySetPadding(4, 4, 4, 4);
        stack.TryAddMember(toggle);
        stack.TryAddMember(progress);
        stack.TryAddMember(button);
        view.TryAddMember(toggle, 0, 0);
        view.TryAddMember(progress, 0, 0);
        view.TryAddMember(button, 0, 0);
        result &= Case(ref count, stack.PerformLayout(view) ==
            GuideXosVerticalStackResult.LaidOut &&
            group.TryGetContentRectangle(out int left, out int top, out int width, out _) &&
            stack.X == left && stack.Y == top && stack.Width == width);
        int initialHeight = stack.ContentHeight;
        int initialButtonY = button.Y;
        int frameX = group.X;
        int frameY = group.Y;
        int registrations = 0;
        GuideXosControlHost host = new(1);
        host.TryRegisterCheckBox(30, toggle, true);
        registrations = host.RegistrationCount;
        toggle.SetChecked(false);
        progress.SetVisible(false);
        result &= Case(ref count, stack.PerformLayout(view) ==
            GuideXosVerticalStackResult.LaidOut && stack.ContentHeight < initialHeight);
        result &= Case(ref count, button.Y < initialButtonY && group.X == frameX &&
            group.Y == frameY && button.X + button.Width ==
                stack.X + stack.Width - stack.RightPadding - button.MarginRight);
        result &= Case(ref count, view.ContentExtent >= group.Y + group.Height - view.InnerY &&
            view.Offset >= 0 && view.Offset <= view.MaximumOffset);
        result &= Case(ref count, host.RegistrationCount == registrations &&
            group.MemberCount == 3 && stack.MemberCount == 3 && view.MemberCount == 4);
        toggle.SetChecked(true);
        progress.SetVisible(true);
        result &= Case(ref count, stack.PerformLayout(view) ==
            GuideXosVerticalStackResult.LaidOut && stack.ContentHeight == initialHeight &&
            button.Y == initialButtonY);
        result &= Case(ref count, progress.EffectiveVisible && toggle.EffectiveVisible &&
            group.ValidateMembershipGeometry(out int outside) && outside == 0);
        group.SetVisible(true);
        result &= Case(ref count, stack.PerformLayout(view) ==
            GuideXosVerticalStackResult.LaidOut);
        result &= Case(ref count, view.ContentExtent > view.VisibleExtent &&
            button.X + button.Width <= group.ContentLeft + group.ContentWidth &&
            button.Y >= group.ContentTop);
        result &= Case(ref count, registrations == 1 && view.BoundScrollBar == null);
        return result && count == 10;
    }

    private static void Log(GuideXosHost host, string message)
    {
        if (host != null)
            host.TryLog(System.Text.Encoding.ASCII.GetBytes(message));
    }

    private static void LogNumber(GuideXosHost host, ReadOnlySpan<byte> prefix,
        int value)
    {
        if (host == null) return;
        Span<byte> line = stackalloc byte[80];
        int position = 0;
        GuideXosText.Append(line, ref position, prefix);
        GuideXosText.AppendUnsigned(line, ref position, (uint)value);
        host.TryLog(line[..position]);
    }
}
