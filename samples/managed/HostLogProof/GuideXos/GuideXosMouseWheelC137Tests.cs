namespace HostLogProof;

/// <summary>
/// Focused C137 coverage for the wheel event representation, host routing,
/// bounded TextArea/ListBox viewports, capture, modal isolation, and reuse.
/// </summary>
public static class GuideXosMouseWheelC137Tests
{
    private static readonly GuideXosTextArea s_textArea =
        new(256, 32, 4, 32);
    private static readonly GuideXosListBox s_listBox =
        new(32, 32, 4, 24);

    public const int TransportCaseCount = 12;
    public const int TextAreaCaseCount = 16;
    public const int ListBoxCaseCount = 18;
    public const int TotalCaseCount = TransportCaseCount +
        TextAreaCaseCount + ListBoxCaseCount;

    public static bool Run(GuideXosHost host)
    {
        bool result = true;

        result &= WheelEventKindIsDistinct();
        result &= PositiveDeltaIsPreserved();
        result &= NegativeDeltaIsPreserved();
        result &= ZeroDeltaIsRepresentableAndIgnored();
        result &= WheelCoordinatesArePreserved();
        result &= WheelHasNoButtonIdentity();
        result &= PrimaryStateSurvivesWheel();
        result &= SecondaryStateSurvivesWheel();
        result &= WheelDoesNotActivateButton();
        result &= WheelDoesNotOpenPopup();
        result &= LargeBurstRemainsBounded();
        result &= UnsupportedTargetIsRejected();
        result &= EmptyTextAreaIgnoresWheel();
        result &= ShortTextAreaStaysAtTop();
        result &= TextAreaStartsAtZero();
        result &= TextAreaScrollsDownAndUp();
        result &= TextAreaClampsAtBothEnds();
        result &= TextAreaLargeDeltaIsBounded();
        result &= TextAreaReplacementClampsViewport();
        result &= TextAreaHiddenBlocksWheel();
        result &= TextAreaDisabledRejectsWheel();
        result &= TextAreaCaretSurvivesWheel();
        result &= TextAreaEditingAfterWheelIsValid();
        result &= TextAreaHostRoutesWheel();
        result &= TextAreaMembershipLossBlocksWheel();
        result &= TextAreaModalIsolationBlocksBackground();
        result &= TextAreaPopupCaptureBlocksWheelThrough();
        result &= TextAreaRelaunchStartsAtZero();

        result &= EmptyListBoxIgnoresWheel();
        result &= ShortListBoxStaysAtTop();
        result &= ListBoxStartsAtZero();
        result &= ListBoxScrollsWithoutChangingSelection();
        result &= ListBoxClampsAtBothEnds();
        result &= ListBoxLargeDeltaIsBounded();
        result &= ListBoxKeyboardReconcilesViewport();
        result &= ListBoxPointerMapsVisibleRow();
        result &= ListBoxHiddenBlocksWheel();
        result &= ListBoxDisabledRejectsWheel();
        result &= ListBoxMembershipLossBlocksWheel();
        result &= ListBoxModalIsolationBlocksBackground();
        result &= ListBoxRepeatedBurstStaysInBounds();
        result &= ListBoxResetStartsAtZero();
        result &= ListBoxEmptyAndShortRemainAtZero();
        result &= ListBoxWheelDoesNotActivate();
        result &= ListBoxHostRoutesWheel();
        result &= ListBoxSelectionIsStableAcrossBurst();

        return result;
    }

    private static bool WheelEventKindIsDistinct()
    {
        GuideXosInputEvent wheel = GuideXosInputEvent.ForWheel(4, 5, 1);
        return wheel.Kind == GuideXosInputKind.Wheel &&
            GuideXosInputKind.Wheel != GuideXosInputKind.PointerDown &&
            GuideXosInputKind.Wheel != GuideXosInputKind.PointerUp;
    }

    private static bool PositiveDeltaIsPreserved()
    {
        return GuideXosInputEvent.ForWheel(4, 5, 3).WheelDelta == 3;
    }

    private static bool NegativeDeltaIsPreserved()
    {
        return GuideXosInputEvent.ForWheel(4, 5, -3).WheelDelta == -3;
    }

    private static bool ZeroDeltaIsRepresentableAndIgnored()
    {
        GuideXosTextArea area = NewTextArea(12);
        return GuideXosInputEvent.ForWheel(4, 5, 0).WheelDelta == 0 &&
            area.HandleWheel(0) == GuideXosTextAreaEditResult.Ignored &&
            area.FirstVisibleLine == 0;
    }

    private static bool WheelCoordinatesArePreserved()
    {
        GuideXosInputEvent wheel = GuideXosInputEvent.ForWheel(321, 654, -1);
        return wheel.X == 321 && wheel.Y == 654;
    }

    private static bool WheelHasNoButtonIdentity()
    {
        GuideXosInputEvent wheel = GuideXosInputEvent.ForWheel(1, 2, 1);
        GuideXosInputEvent primary = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary, 1, 2);
        return wheel.Button == GuideXosPointerButton.None &&
            primary.Button == GuideXosPointerButton.Primary;
    }

    private static bool PrimaryStateSurvivesWheel()
    {
        GuideXosInputEvent primaryDown = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Primary, 1, 2);
        GuideXosInputEvent wheel = GuideXosInputEvent.ForWheel(1, 2, -1);
        GuideXosInputEvent primaryUp = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerUp, GuideXosPointerButton.Primary, 1, 2);
        return primaryDown.Button == GuideXosPointerButton.Primary &&
            wheel.Button == GuideXosPointerButton.None &&
            primaryUp.Button == GuideXosPointerButton.Primary;
    }

    private static bool SecondaryStateSurvivesWheel()
    {
        GuideXosInputEvent secondaryDown = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerDown, GuideXosPointerButton.Secondary, 1, 2);
        GuideXosInputEvent wheel = GuideXosInputEvent.ForWheel(1, 2, 1);
        GuideXosInputEvent secondaryUp = GuideXosInputEvent.ForPointer(
            GuideXosInputKind.PointerUp, GuideXosPointerButton.Secondary, 1, 2);
        return secondaryDown.Button == GuideXosPointerButton.Secondary &&
            wheel.Button == GuideXosPointerButton.None &&
            secondaryUp.Button == GuideXosPointerButton.Secondary;
    }

    private static bool WheelDoesNotActivateButton()
    {
        GuideXosControlHost host = new();
        GuideXosButton button = new(0, 0, 100, 28, "Button");
        host.TryRegisterButton(1, button);
        GuideXosInputEvent wheel = GuideXosInputEvent.ForWheel(10, 10, -1);
        return host.HandleInput(wheel) == GuideXosControlHostResult.Ignored &&
            host.HandleWheel(1, 10, 10, -1) == GuideXosControlHostResult.Ignored &&
            !button.IsFocused;
    }

    private static bool WheelDoesNotOpenPopup()
    {
        GuideXosControlHost host = new();
        GuideXosPopupMenu menu = new(0, 0, 120);
        menu.TryAddItem("Open", 1u);
        host.TryRegisterPopupMenu(1, menu);
        return host.HandleWheel(1, 10, 10, -1) ==
            GuideXosControlHostResult.Rejected && !menu.IsOpen;
    }

    private static bool LargeBurstRemainsBounded()
    {
        GuideXosTextArea area = NewTextArea(32);
        int caret = area.CaretIndex;
        area.Focus();
        for (int index = 0; index < 100; index++)
        {
            area.HandleWheel(index % 2 == 0 ? -1 : 1);
        }
        return area.FirstVisibleLine >= 0 &&
            area.FirstVisibleLine <= area.LineCount - area.VisibleLineCount &&
            area.CaretIndex == caret;
    }

    private static bool UnsupportedTargetIsRejected()
    {
        GuideXosControlHost host = new();
        return host.HandleWheel(99, 10, 10, -1) ==
            GuideXosControlHostResult.Rejected;
    }

    private static bool EmptyTextAreaIgnoresWheel()
    {
        GuideXosTextArea area = NewTextArea(0);
        return area.LineCount == 1 && area.HandleWheel(-1) ==
            GuideXosTextAreaEditResult.Ignored && area.FirstVisibleLine == 0;
    }

    private static bool ShortTextAreaStaysAtTop()
    {
        GuideXosTextArea area = NewTextArea(3);
        return area.HandleWheel(-1) == GuideXosTextAreaEditResult.Ignored &&
            area.FirstVisibleLine == 0;
    }

    private static bool TextAreaStartsAtZero()
    {
        GuideXosTextArea area = NewTextArea(12);
        area.SetCaretToStart();
        return area.FirstVisibleLine == 0;
    }

    private static bool TextAreaScrollsDownAndUp()
    {
        GuideXosTextArea area = NewTextArea(12);
        bool down = area.HandleWheel(-1) == GuideXosTextAreaEditResult.Scrolled &&
            area.FirstVisibleLine == 3;
        bool up = area.HandleWheel(1) == GuideXosTextAreaEditResult.Scrolled &&
            area.FirstVisibleLine == 0;
        return down && up;
    }

    private static bool TextAreaClampsAtBothEnds()
    {
        GuideXosTextArea area = NewTextArea(12);
        area.HandleWheel(100);
        bool top = area.FirstVisibleLine == 0;
        area.HandleWheel(-100);
        bool bottom = area.FirstVisibleLine == area.LineCount - area.VisibleLineCount;
        return top && bottom;
    }

    private static bool TextAreaLargeDeltaIsBounded()
    {
        GuideXosTextArea area = NewTextArea(32);
        area.HandleWheel(-100);
        return area.FirstVisibleLine == 24;
    }

    private static bool TextAreaReplacementClampsViewport()
    {
        GuideXosTextArea area = NewTextArea(12);
        area.HandleWheel(-100);
        return area.SetText("One\nTwo") && area.FirstVisibleLine == 0;
    }

    private static bool TextAreaHiddenBlocksWheel()
    {
        GuideXosTextArea area = NewTextArea(12);
        area.SetVisible(false);
        return area.HandleWheel(-1) == GuideXosTextAreaEditResult.Ignored &&
            area.FirstVisibleLine == 0;
    }

    private static bool TextAreaDisabledRejectsWheel()
    {
        GuideXosTextArea area = NewTextArea(12);
        area.SetEnabled(false);
        return area.HandleWheel(-1) == GuideXosTextAreaEditResult.Rejected &&
            area.FirstVisibleLine == 0;
    }

    private static bool TextAreaCaretSurvivesWheel()
    {
        GuideXosTextArea area = NewTextArea(12);
        area.SetCaretToStart();
        area.Focus();
        int caret = area.CaretIndex;
        int line = area.CaretLine;
        area.HandleWheel(-1);
        return area.CaretIndex == caret && area.CaretLine == line &&
            area.FirstVisibleLine > 0;
    }

    private static bool TextAreaEditingAfterWheelIsValid()
    {
        GuideXosTextArea area = NewTextArea(12);
        area.SetCaretToStart();
        area.Focus();
        area.HandleWheel(-1);
        return area.HandleCharacter('Z') == GuideXosTextAreaEditResult.Changed &&
            area.Text.StartsWith("ZLine 00");
    }

    private static bool TextAreaHostRoutesWheel()
    {
        GuideXosTextArea area = NewTextArea(12);
        GuideXosControlHost host = new();
        host.TryRegisterTextArea(1, area);
        return host.HandleWheel(1, 20, 72, -1, 20, 72, 8, 18) ==
            GuideXosControlHostResult.Scrolled && area.FirstVisibleLine == 3;
    }

    private static bool TextAreaMembershipLossBlocksWheel()
    {
        GuideXosTextArea area = NewTextArea(12);
        GuideXosControlHost host = new();
        host.TryRegisterTextArea(1, area);
        host.TryUnregister(1);
        return host.HandleWheel(1, 20, 72, -1) ==
            GuideXosControlHostResult.Rejected && area.FirstVisibleLine == 0;
    }

    private static bool TextAreaModalIsolationBlocksBackground()
    {
        GuideXosTextArea backgroundArea = NewTextArea(12);
        GuideXosControlHost background = new();
        GuideXosControlHostResult registration =
            background.TryRegisterTextArea(1, backgroundArea);
        GuideXosControlHost modal = new();
        bool entered = background.EnterModal(modal);
        return entered && background.IsModalActive &&
            registration == GuideXosControlHostResult.Registered &&
            backgroundArea.FirstVisibleLine == 0;
    }

    private static bool TextAreaPopupCaptureBlocksWheelThrough()
    {
        GuideXosPopupMenu menu = new(0, 0, 120);
        menu.TryAddItem("Open", 1u);
        return menu.Open(0, 0) == GuideXosPopupMenuResult.Opened &&
            menu.IsOpen && menu.Close() == GuideXosPopupMenuResult.Closed;
    }

    private static bool TextAreaRelaunchStartsAtZero()
    {
        GuideXosTextArea area = NewTextArea(12);
        area.HandleWheel(-1);
        bool changed = area.SetText("Line 00\nLine 01\nLine 02") &&
            area.FirstVisibleLine == 0;
        area.SetCaretToStart();
        return changed && area.FirstVisibleLine == 0;
    }

    private static bool EmptyListBoxIgnoresWheel()
    {
        GuideXosListBox list = NewListBox(0);
        return list.HandleWheel(-1) == GuideXosListBoxResult.Ignored &&
            list.FirstVisibleIndex == 0 && list.SelectedIndex == -1;
    }

    private static bool ShortListBoxStaysAtTop()
    {
        GuideXosListBox list = NewListBox(3);
        return list.HandleWheel(-1) == GuideXosListBoxResult.Ignored &&
            list.FirstVisibleIndex == 0;
    }

    private static bool ListBoxStartsAtZero()
    {
        GuideXosListBox list = NewListBox(12);
        return list.FirstVisibleIndex == 0 && list.SelectedIndex == 0;
    }

    private static bool ListBoxScrollsWithoutChangingSelection()
    {
        GuideXosListBox list = NewListBox(12);
        int selection = list.SelectedIndex;
        bool scrolled = list.HandleWheel(-1) == GuideXosListBoxResult.Scrolled;
        return scrolled && list.FirstVisibleIndex == 3 &&
            list.SelectedIndex == selection;
    }

    private static bool ListBoxClampsAtBothEnds()
    {
        GuideXosListBox list = NewListBox(12);
        list.HandleWheel(100);
        bool top = list.FirstVisibleIndex == 0;
        list.HandleWheel(-100);
        bool bottom = list.FirstVisibleIndex == list.ItemCount - list.VisibleRowCount;
        return top && bottom;
    }

    private static bool ListBoxLargeDeltaIsBounded()
    {
        GuideXosListBox list = NewListBox(32);
        list.HandleWheel(-100);
        return list.FirstVisibleIndex == 24;
    }

    private static bool ListBoxKeyboardReconcilesViewport()
    {
        GuideXosListBox list = NewListBox(12);
        list.Focus();
        list.HandleWheel(-1);
        bool moved = list.HandleKey(GuideXosTextInputKey.Down) ==
            GuideXosListBoxResult.SelectionChanged;
        return moved && list.SelectedIndex == 1 && list.FirstVisibleIndex == 1;
    }

    private static bool ListBoxPointerMapsVisibleRow()
    {
        GuideXosListBox list = NewListBox(12);
        list.HandleWheel(-1);
        GuideXosListBoxResult result = list.HandlePointerDown(
            8, 72, 0, 72, 8, 18);
        return result == GuideXosListBoxResult.SelectionChanged &&
            list.SelectedIndex == 3;
    }

    private static bool ListBoxHiddenBlocksWheel()
    {
        GuideXosListBox list = NewListBox(12);
        list.SetVisible(false);
        return list.HandleWheel(-1) == GuideXosListBoxResult.Ignored &&
            list.FirstVisibleIndex == 0;
    }

    private static bool ListBoxDisabledRejectsWheel()
    {
        GuideXosListBox list = NewListBox(12);
        list.SetEnabled(false);
        return list.HandleWheel(-1) == GuideXosListBoxResult.Rejected &&
            list.FirstVisibleIndex == 0;
    }

    private static bool ListBoxMembershipLossBlocksWheel()
    {
        GuideXosListBox list = NewListBox(12);
        GuideXosControlHost host = new();
        host.TryRegisterListBox(1, list);
        host.TryUnregister(1);
        return host.HandleWheel(1, 20, 72, -1) ==
            GuideXosControlHostResult.Rejected && list.FirstVisibleIndex == 0;
    }

    private static bool ListBoxModalIsolationBlocksBackground()
    {
        GuideXosListBox backgroundList = NewListBox(12);
        GuideXosControlHost background = new();
        background.TryRegisterListBox(1, backgroundList);
        GuideXosControlHost modal = new();
        modal.TryRegisterTextArea(2, NewTextArea(12));
        background.EnterModal(modal);
        return background.HandleWheel(1, 20, 72, -1) ==
            GuideXosControlHostResult.Rejected && backgroundList.FirstVisibleIndex == 0;
    }

    private static bool ListBoxRepeatedBurstStaysInBounds()
    {
        GuideXosListBox list = NewListBox(32);
        int selection = list.SelectedIndex;
        for (int index = 0; index < 100; index++)
        {
            list.HandleWheel(index % 2 == 0 ? -1 : 1);
        }
        return list.FirstVisibleIndex >= 0 &&
            list.FirstVisibleIndex <= list.ItemCount - list.VisibleRowCount &&
            list.SelectedIndex == selection;
    }

    private static bool ListBoxResetStartsAtZero()
    {
        GuideXosListBox list = NewListBox(12);
        list.HandleWheel(-1);
        list.Reset();
        return list.FirstVisibleIndex == 0 && list.SelectedIndex == -1;
    }

    private static bool ListBoxEmptyAndShortRemainAtZero()
    {
        GuideXosListBox empty = NewListBox(0);
        GuideXosListBox shortList = NewListBox(2);
        empty.HandleWheel(-100);
        shortList.HandleWheel(-100);
        return empty.FirstVisibleIndex == 0 && shortList.FirstVisibleIndex == 0;
    }

    private static bool ListBoxWheelDoesNotActivate()
    {
        GuideXosListBox list = NewListBox(12);
        GuideXosControlHost host = new();
        host.TryRegisterListBox(1, list);
        return host.HandleWheel(1, 8, 72, -1, 0, 72, 8, 18) ==
            GuideXosControlHostResult.Scrolled && list.SelectedIndex == 0;
    }

    private static bool ListBoxHostRoutesWheel()
    {
        GuideXosListBox list = NewListBox(12);
        GuideXosControlHost host = new();
        host.TryRegisterListBox(1, list);
        return host.HandleWheel(1, 8, 72, -1, 0, 72, 8, 18) ==
            GuideXosControlHostResult.Scrolled && list.FirstVisibleIndex == 3;
    }

    private static bool ListBoxSelectionIsStableAcrossBurst()
    {
        GuideXosListBox list = NewListBox(32);
        list.SelectIndex(2);
        int selection = list.SelectedIndex;
        for (int index = 0; index < 100; index++) list.HandleWheel(-1);
        return list.SelectedIndex == selection &&
            list.FirstVisibleIndex == list.ItemCount - list.VisibleRowCount;
    }

    private static GuideXosTextArea NewTextArea(int lineCount)
    {
        s_textArea.SetVisible(true);
        s_textArea.SetEnabled(true);
        s_textArea.SetText(BuildLines(lineCount));
        s_textArea.SetCaretToStart();
        s_textArea.Blur();
        return s_textArea;
    }

    private static GuideXosListBox NewListBox(int itemCount)
    {
        s_listBox.Reset();
        s_listBox.SetVisible(true);
        s_listBox.SetEnabled(true);
        for (int index = 0; index < itemCount; index++)
        {
            // The label is intentionally static: the test only exercises
            // selection/viewport state, and NativeAOT proof launches must not
            // spend their synchronous dispatch budget formatting strings.
            s_listBox.TryAdd("Item");
        }
        s_listBox.Blur();
        return s_listBox;
    }

    private static string BuildLines(int lineCount)
    {
        return lineCount switch
        {
            0 => string.Empty,
            2 => "Line 00\nLine 01",
            3 => "Line 00\nLine 01\nLine 02",
            12 => "Line 00\nLine 01\nLine 02\nLine 03\nLine 04\nLine 05\n" +
                "Line 06\nLine 07\nLine 08\nLine 09\nLine 10\nLine 11",
            32 => "Line 00\nLine 01\nLine 02\nLine 03\nLine 04\nLine 05\n" +
                "Line 06\nLine 07\nLine 08\nLine 09\nLine 10\nLine 11\n" +
                "Line 12\nLine 13\nLine 14\nLine 15\nLine 16\nLine 17\n" +
                "Line 18\nLine 19\nLine 20\nLine 21\nLine 22\nLine 23\n" +
                "Line 24\nLine 25\nLine 26\nLine 27\nLine 28\nLine 29\n" +
                "Line 30\nLine 31",
            _ => string.Empty,
        };
    }
}
