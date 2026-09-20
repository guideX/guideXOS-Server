using System;

namespace HostLogProof;

/// <summary>Focused bounded probes for membership, layout, visibility, and host recovery.</summary>
public static class GuideXosPanelTests
{
    private static int s_caseCount;

    public static bool Run(GuideXosHost host, GuideXosSurface surface)
    {
        s_caseCount = 0;
        bool result = ConstructionAndBounds() &&
            MembershipAndReuse() &&
            CoordinatesAndMovement() &&
            VisibilityAndRendering(surface) &&
            HostFocusAndInput() &&
            ModalIsolationAndLifetime();
        if (host != null)
        {
            Span<byte> line = stackalloc byte[96];
            int position = 0;
            GuideXosText.Append(line, ref position, "C127-PANEL-TESTS cases="u8);
            GuideXosText.AppendUnsigned(line, ref position, (uint)s_caseCount);
            GuideXosText.Append(line, ref position,
                result ? " result=PASS"u8 : " result=FAIL"u8);
            host.TryLog(line[..position]);
        }
        return result;
    }

    private static bool Check(bool condition)
    {
        ++s_caseCount;
        return condition;
    }

    private static bool ConstructionAndBounds()
    {
        GuideXosPanel panel = new(16, 24, 320, 72, 2);
        bool geometry = Check(panel.X == 16 && panel.Y == 24 &&
            panel.Width == 320 && panel.Height == 72);
        bool capacity = Check(panel.MaximumChildCount == 2 &&
            panel.ChildCount == 0 && panel.Focusable == false);
        bool visible = Check(panel.Visible && panel.RejectedInputCount == 0u);
        bool minimums = Check(GuideXosPanel.MinimumSupportedWidth == 8 &&
            GuideXosPanel.MinimumSupportedHeight == 18);
        bool invalidX = Check(!panel.TrySetBounds(-1, 24, 320, 72));
        bool invalidY = Check(!panel.TrySetBounds(16, -1, 320, 72));
        bool invalidWidth = Check(!panel.TrySetBounds(16, 24, 7, 72));
        bool unalignedWidth = Check(!panel.TrySetBounds(16, 24, 321, 72));
        bool invalidHeight = Check(!panel.TrySetBounds(16, 24, 320, 17));
        bool unalignedHeight = Check(!panel.TrySetBounds(16, 24, 320, 19));
        bool overflowX = Check(!panel.TrySetBounds(4090, 24, 8, 18));
        bool overflowY = Check(!panel.TrySetBounds(16, 4090, 8, 18));
        bool preserved = Check(panel.X == 16 && panel.Y == 24 &&
            panel.Width == 320 && panel.Height == 72);
        return geometry && capacity && visible && minimums && invalidX &&
            invalidY && invalidWidth && unalignedWidth && invalidHeight &&
            unalignedHeight && overflowX && overflowY && preserved;
    }

    private static bool MembershipAndReuse()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 4);
        GuideXosRadioButton radio = NewRadio("Radio");
        GuideXosLabel label = new(0, 0, 80, "Label");
        GuideXosSeparator separator = new(0, 0, 96);
        GuideXosProgressBar progress = new(0, 0, 64);
        bool first = Check(panel.TryAddChild(radio, 8, 14) ==
            GuideXosPanelResult.Added && radio.ParentPanel == panel);
        bool duplicate = Check(panel.TryAddChild(radio, 8, 14) ==
            GuideXosPanelResult.Duplicate && panel.ChildCount == 1);
        bool second = Check(panel.TryAddChild(label, 8, 46) ==
            GuideXosPanelResult.Added && panel.GetChildAt(1) == label);

        GuideXosPanel other = new(500, 100, 320, 72, 2);
        bool conflict = Check(other.TryAddChild(radio, 8, 14) ==
            GuideXosPanelResult.MembershipConflict && other.ChildCount == 0);
        bool unsupported = Check(panel.TryAddChild(new GuideXosGroupBox(
            100, 100, 320, 54), 0, 0) == GuideXosPanelResult.InvalidChild);
        bool removed = Check(panel.TryRemoveChild(label) ==
            GuideXosPanelResult.Removed && label.ParentPanel == null &&
            panel.ChildCount == 1);
        bool removalLifetime = Check(label.Visible && label.TrySetBounds(
            112, 120, 80) && label.X == 112 && label.Y == 120);
        bool reused = Check(panel.TryAddChild(separator, 8, 46) ==
            GuideXosPanelResult.Added && panel.ChildCount == 2);
        bool filled = Check(panel.TryAddChild(progress, 120, 46) ==
            GuideXosPanelResult.Added && panel.ChildCount == 3);
        GuideXosLabel fourth = new(0, 0, 80, "Fourth");
        bool fourthAdded = Check(panel.TryAddChild(fourth, 224, 46) ==
            GuideXosPanelResult.Added && panel.ChildCount == 4);
        GuideXosLabel overCapacity = new(0, 0, 80, "Over");
        bool capacity = Check(panel.TryAddChild(overCapacity, 8, 64) ==
            GuideXosPanelResult.CapacityReached && panel.ChildCount == 4 &&
            overCapacity.ParentPanel == null);
        bool cleared = Check(panel.Clear() == GuideXosPanelResult.Cleared &&
            panel.ChildCount == 0 && radio.ParentPanel == null &&
            separator.ParentPanel == null && progress.ParentPanel == null);
        bool clearEmpty = Check(panel.Clear() == GuideXosPanelResult.Empty);
        bool reusable = Check(panel.TryAddChild(radio, 8, 14) ==
            GuideXosPanelResult.Added && panel.ChildCount == 1);
        return first && duplicate && second && conflict && unsupported &&
            removed && removalLifetime && reused && filled && fourthAdded &&
            capacity && cleared && clearEmpty && reusable;
    }

    private static bool CoordinatesAndMovement()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 2);
        GuideXosRadioButton radio = NewRadio("Position");
        bool added = Check(panel.TryAddChild(radio, 8, 14) ==
            GuideXosPanelResult.Added && radio.X == 108 && radio.Y == 114);
        bool resolved = Check(panel.TryResolvePoint(12, 24,
            out int absoluteX, out int absoluteY) && absoluteX == 112 &&
            absoluteY == 124);
        bool edge = Check(panel.ContainsPoint(100, 100) &&
            panel.ContainsPoint(419, 171) && !panel.ContainsPoint(420, 171));
        bool bottomRight = Check(panel.TryResolvePoint(319, 71,
            out absoluteX, out absoluteY) && absoluteX == 419 &&
            absoluteY == 171);
        bool invalidPoint = Check(!panel.TryResolvePoint(320, 0,
            out _, out _) && !panel.TryResolvePoint(0, 72, out _, out _));
        bool moved = Check(panel.TrySetBounds(140, 160, 320, 72) &&
            radio.X == 148 && radio.Y == 174);
        bool movedAgain = Check(panel.TrySetBounds(100, 100, 320, 72) &&
            radio.X == 108 && radio.Y == 114);
        bool localMoved = Check(panel.TrySetChildPosition(radio, 24, 18) ==
            GuideXosPanelResult.Moved && radio.X == 124 && radio.Y == 118);
        bool localPreserved = Check(panel.TrySetBounds(140, 160, 320, 72) &&
            radio.X == 164 && radio.Y == 178);
        bool directRejected = Check(!radio.TrySetBounds(1, 1, 128, 28) &&
            radio.X == 164 && radio.Y == 178);
        bool resizeRejected = Check(!panel.TrySetBounds(140, 160, 128, 36) &&
            panel.Width == 320 && panel.Height == 72 && radio.X == 164);
        bool invalidMove = Check(panel.TrySetChildPosition(radio, 200, 0) ==
            GuideXosPanelResult.OutOfBounds && radio.X == 164);
        return added && resolved && edge && bottomRight && invalidPoint &&
            moved && movedAgain && localMoved && localPreserved &&
            directRejected && resizeRejected && invalidMove;
    }

    private static bool VisibilityAndRendering(GuideXosSurface surface)
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 2);
        GuideXosRadioButton radio = NewRadio("Visible");
        GuideXosLabel label = new(0, 0, 80, "Local");
        bool added = Check(panel.TryAddChild(radio, 8, 14) ==
            GuideXosPanelResult.Added && panel.TryAddChild(label, 160, 14) ==
            GuideXosPanelResult.Added);
        bool render = Check(surface != null && panel.Render(surface) ==
            GuideXosResult.Success && radio.EffectiveVisible &&
            label.EffectiveVisible);
        radio.SetVisible(false);
        bool localHidden = Check(!radio.Visible && !radio.EffectiveVisible &&
            panel.Visible && label.EffectiveVisible);
        bool localRender = Check(panel.Render(surface) == GuideXosResult.Success);
        panel.SetVisible(false);
        bool parentHidden = Check(!panel.Visible && !radio.EffectiveVisible &&
            !label.EffectiveVisible && label.Visible);
        bool hiddenRender = Check(panel.Render(surface) == GuideXosResult.Success);
        panel.SetVisible(true);
        bool restored = Check(panel.Visible && !radio.EffectiveVisible &&
            label.EffectiveVisible);
        bool nullSurface = Check(panel.Render(null) == GuideXosResult.InvalidArgument);
        return added && render && localHidden && localRender && parentHidden &&
            hiddenRender && restored && nullSurface;
    }

    private static bool HostFocusAndInput()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 2);
        GuideXosRadioButton first = NewRadio("First");
        GuideXosRadioButton second = NewRadio("Second");
        GuideXosRadioButton fallback = NewRadio("Fallback");
        panel.TryAddChild(first, 8, 14);
        panel.TryAddChild(second, 160, 14);
        GuideXosControlHost focusHost = new(3);
        focusHost.TryRegisterRadioButton(1, first);
        focusHost.TryRegisterRadioButton(2, second);
        focusHost.TryRegisterRadioButton(3, fallback);
        bool focus = Check(focusHost.TryFocus(1) ==
            GuideXosControlHostResult.Focused && first.IsFocused);
        panel.SetVisible(false);
        bool recovered = Check(focusHost.RefreshVisibility() ==
            GuideXosControlHostResult.Focused && focusHost.ActiveControlId == 3 &&
            fallback.IsFocused && !first.IsFocused);
        bool hiddenPointer = Check(focusHost.FocusAndRoutePointer(
            1, 108, 114) == GuideXosControlHostResult.Ignored &&
            !first.Selected);
        panel.SetVisible(true);
        bool noSteal = Check(focusHost.ActiveControlId == 3 &&
            !first.IsFocused && fallback.IsFocused);
        bool traversal = Check(focusHost.HandleKey(
            GuideXosTextInputKey.Tab) == GuideXosControlHostResult.Traversed &&
            focusHost.ActiveControlId == 1 && first.IsFocused);
        bool exactlyOnce = Check(focusHost.HandleKey(
            (GuideXosTextInputKey)' ') == GuideXosControlHostResult.Ignored &&
            focusHost.HandleCharacter(' ') == GuideXosControlHostResult.Changed &&
            first.Selected);
        second.SetEnabled(false);
        bool disabledSkip = Check(focusHost.HandleKey(
            GuideXosTextInputKey.Tab) == GuideXosControlHostResult.Traversed &&
            focusHost.ActiveControlId == 3);
        return focus && recovered && hiddenPointer && noSteal && traversal &&
            exactlyOnce && disabledSkip;
    }

    private static bool ModalIsolationAndLifetime()
    {
        GuideXosPanel panel = new(100, 100, 320, 72, 1);
        GuideXosRadioButton radio = NewRadio("Modal");
        panel.TryAddChild(radio, 8, 14);
        GuideXosControlHost main = new(1);
        main.TryRegisterRadioButton(10, radio);
        main.TryFocus(10);
        GuideXosControlHost modal = new(1);
        GuideXosListBox list = new(10, 10, 2, 18);
        list.TryAdd("one");
        modal.TryRegisterListBox(90, list);
        bool entered = Check(main.EnterModal(modal) && main.IsModalActive &&
            !radio.IsFocused && main.ActiveControlId == 0);
        bool isolated = Check(main.HandleCharacter(' ') ==
            GuideXosControlHostResult.Ignored && !radio.Selected);
        bool restored = Check(main.ExitModal() && main.ActiveControlId == 10 &&
            radio.IsFocused);
        bool removed = Check(panel.TryRemoveChild(radio) ==
            GuideXosPanelResult.Removed && radio.ParentPanel == null &&
            !radio.IsFocused && panel.ChildCount == 0);
        bool reusable = Check(radio.TrySetBounds(240, 240, 128, 28) &&
            radio.X == 240 && radio.Y == 240 && radio.Visible);
        panel.Reset();
        bool reset = Check(panel.Visible && panel.RejectedInputCount == 0u);
        return entered && isolated && restored && removed && reusable && reset;
    }

    private static GuideXosRadioButton NewRadio(string label)
    {
        return new GuideXosRadioButton(10, 20, 128, 28, label);
    }
}
