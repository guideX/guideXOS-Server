namespace HostLogProof;

/// <summary>Focused probes for fixed-capacity group registration and navigation.</summary>
public static class GuideXosRadioGroupTests
{
    public static bool Run(GuideXosHost host)
    {
        bool result = EmptyAndCapacity() && Selection() && DisabledNavigation() &&
            IndependenceAndReset();
        host?.TryLog(result
            ? "C124-RADIO-GROUP-TESTS result=PASS"u8
            : "C124-RADIO-GROUP-TESTS result=FAIL"u8);
        return result;
    }

    private static bool EmptyAndCapacity()
    {
        GuideXosRadioGroup empty = new(4);
        GuideXosRadioButton first = New("First");
        GuideXosRadioButton second = New("Second");
        GuideXosRadioButton third = New("Third");
        GuideXosRadioButton fourth = New("Fourth");
        GuideXosRadioButton overflow = New("Overflow");
        bool initial = empty.MemberCount == 0 && empty.SelectedIndex == -1 &&
            !empty.HasSelection && empty.SelectedMember == null;
        bool registrations = empty.TryRegister(first) &&
            empty.TryRegister(second) && empty.TryRegister(third) &&
            empty.TryRegister(fourth) && empty.MemberCount == 4;
        bool overflowRejected = !empty.TryRegister(overflow) &&
            empty.MemberCount == 4;
        bool duplicateRejected = !empty.TryRegister(first) &&
            empty.MemberCount == 4;
        return initial && registrations && overflowRejected && duplicateRejected;
    }

    private static bool Selection()
    {
        GuideXosRadioGroup group = new(4);
        GuideXosRadioButton first = New("First");
        GuideXosRadioButton middle = New("Middle");
        GuideXosRadioButton final = New("Final");
        group.TryRegister(first);
        group.TryRegister(middle);
        group.TryRegister(final);
        bool noSelection = group.SelectedIndex == -1 && !first.Selected;
        bool firstSelected = group.TrySelect(first) && group.SelectedIndex == 0 &&
            first.Selected && !middle.Selected && !final.Selected;
        bool middleSelected = group.TrySelectIndex(1) &&
            group.SelectedIndex == 1 && !first.Selected && middle.Selected;
        bool finalSelected = group.TrySelect(final) &&
            group.SelectedIndex == 2 && !middle.Selected && final.Selected;
        bool reselected = group.TrySelect(final) && group.SelectedIndex == 2 &&
            final.Selected;
        bool invalidRejected = !group.TrySelectIndex(9) &&
            group.SelectedIndex == 2 && final.Selected;
        return noSelection && firstSelected && middleSelected && finalSelected &&
            reselected && invalidRejected;
    }

    private static bool DisabledNavigation()
    {
        GuideXosRadioGroup group = new(4);
        GuideXosRadioButton first = New("First");
        GuideXosRadioButton second = New("Second");
        GuideXosRadioButton third = New("Third");
        GuideXosRadioButton fourth = New("Fourth");
        group.TryRegister(first);
        group.TryRegister(second);
        group.TryRegister(third);
        group.TryRegister(fourth);
        group.TrySelect(second);
        second.SetEnabled(false);
        bool preserve = group.SelectedIndex == 1 && second.Selected;
        third.SetEnabled(false);
        fourth.SetEnabled(false);
        bool skipForward = group.TryMoveNext(first, out int forward) &&
            forward == 0 && group.SelectedIndex == 0;
        bool reverseWrap = group.TryMovePrevious(first, out int reverse) &&
            reverse == 0 && group.SelectedIndex == 0;
        first.SetEnabled(false);
        bool allDisabled = group.SelectedIndex == 0 && first.Selected &&
            !group.TryMoveNext(first, out _);
        return preserve && skipForward && reverseWrap && allDisabled;
    }

    private static bool IndependenceAndReset()
    {
        GuideXosRadioGroup firstGroup = new(2);
        GuideXosRadioGroup secondGroup = new(2);
        GuideXosRadioButton first = New("A");
        GuideXosRadioButton second = New("B");
        GuideXosRadioButton other = New("Other");
        firstGroup.TryRegister(first);
        firstGroup.TryRegister(second);
        secondGroup.TryRegister(other);
        firstGroup.TrySelect(first);
        secondGroup.TrySelect(other);
        bool independent = firstGroup.SelectedIndex == 0 &&
            secondGroup.SelectedIndex == 0 && first.Selected && other.Selected;
        firstGroup.Reset();
        bool reset = firstGroup.MemberCount == 0 && firstGroup.SelectedIndex == -1 &&
            first.Group == null && !first.Selected &&
            firstGroup.TryRegister(first) && firstGroup.MemberCount == 1;
        return independent && reset;
    }

    private static GuideXosRadioButton New(string label)
    {
        return new GuideXosRadioButton(10, 20, 160, 28, label);
    }
}
