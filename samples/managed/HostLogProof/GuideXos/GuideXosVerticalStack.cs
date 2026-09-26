using System;

namespace HostLogProof;

public enum GuideXosVerticalStackResult
{
    Added = 0,
    Removed = 1,
    Cleared = 2,
    Empty = 3,
    Duplicate = 4,
    CapacityReached = 5,
    InvalidMember = 6,
    MembershipConflict = 7,
    InvalidFrame = 8,
    InvalidPadding = 9,
    InvalidSpacing = 10,
    LayoutFailed = 11,
    LaidOut = 12,
    NotMember = 13,
}

public enum GuideXosVerticalStackWidthPolicy
{
    Stretch = 0,
    KeepWidth = 1,
}

/// <summary>
/// A bounded, non-owning vertical arrangement of ordinary leaf controls.
/// The stack stores references only and writes the controls' existing bounds;
/// it does not register, render, focus, or capture any member.
/// </summary>
public sealed class GuideXosVerticalStack
{
    public const int DefaultMaximumMemberCount = 8;
    public const int MaximumSupportedMemberCount = 8;
    public const int MaximumSupportedCoordinate = 4095;
    public const int MaximumSupportedWidth = 504;
    public const int MaximumSupportedPadding = 1024;
    public const int MaximumSupportedSpacing = 1024;
    public const int DefaultSpacing = 4;

    private readonly IGuideXosVerticalStackMember[] _members =
        new IGuideXosVerticalStackMember[MaximumSupportedMemberCount];
    private readonly int[] _computedWidths =
        new int[MaximumSupportedMemberCount];
    private readonly int[] _computedYs =
        new int[MaximumSupportedMemberCount];
    private readonly int _capacity;
    private int _memberCount;
    private int _x;
    private int _y;
    private int _width;
    private int _contentHeight;
    private int _layoutCount;
    private int _topPadding;
    private int _bottomPadding;
    private int _leftPadding;
    private int _rightPadding;
    private int _spacing = DefaultSpacing;

    public GuideXosVerticalStack(int x, int y, int width,
        int maximumMemberCount = DefaultMaximumMemberCount)
    {
        _capacity = maximumMemberCount < 1 ||
            maximumMemberCount > MaximumSupportedMemberCount
            ? DefaultMaximumMemberCount : maximumMemberCount;
        _x = 0;
        _y = 0;
        _width = 1;
        _contentHeight = 0;
        TrySetFrame(x, y, width);
        RecomputeEmptyHeight();
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int MemberCount => _memberCount;
    public int MaximumMemberCount => _capacity;
    public int ContentHeight => _contentHeight;
    public int ContentBottom => _y + _contentHeight;
    public int LayoutCount => _layoutCount;
    public int TopPadding => _topPadding;
    public int BottomPadding => _bottomPadding;
    public int LeftPadding => _leftPadding;
    public int RightPadding => _rightPadding;
    public int Spacing => _spacing;
    public GuideXosVerticalStackWidthPolicy WidthPolicy { get; set; } =
        GuideXosVerticalStackWidthPolicy.Stretch;

    public bool TrySetFrame(int x, int y, int width)
    {
        if (x < 0 || y < 0 || width < 1 || width > MaximumSupportedWidth ||
            x > MaximumSupportedCoordinate - width ||
            y > MaximumSupportedCoordinate)
            return false;

        int oldX = _x;
        int oldY = _y;
        int oldWidth = _width;
        _x = x;
        _y = y;
        _width = width;
        if (PerformLayout() == GuideXosVerticalStackResult.LaidOut)
            return true;
        _x = oldX;
        _y = oldY;
        _width = oldWidth;
        return false;
    }

    public bool TrySetPadding(int top, int bottom, int left, int right)
    {
        if (!IsValidPadding(top) || !IsValidPadding(bottom) ||
            !IsValidPadding(left) || !IsValidPadding(right))
            return false;

        int oldTop = _topPadding;
        int oldBottom = _bottomPadding;
        int oldLeft = _leftPadding;
        int oldRight = _rightPadding;
        _topPadding = top;
        _bottomPadding = bottom;
        _leftPadding = left;
        _rightPadding = right;
        if (PerformLayout() == GuideXosVerticalStackResult.LaidOut)
            return true;

        _topPadding = oldTop;
        _bottomPadding = oldBottom;
        _leftPadding = oldLeft;
        _rightPadding = oldRight;
        return false;
    }

    public bool TrySetSpacing(int spacing)
    {
        if (spacing < 0 || spacing > MaximumSupportedSpacing) return false;
        int oldSpacing = _spacing;
        _spacing = spacing;
        if (PerformLayout() == GuideXosVerticalStackResult.LaidOut)
            return true;
        _spacing = oldSpacing;
        return false;
    }

    public GuideXosVerticalStackResult TryAddMember(object member)
    {
        if (member is not IGuideXosVerticalStackMember stackMember)
            return GuideXosVerticalStackResult.InvalidMember;
        if (FindMemberIndex(member) >= 0)
            return GuideXosVerticalStackResult.Duplicate;
        if (_memberCount >= _capacity)
            return GuideXosVerticalStackResult.CapacityReached;
        if (!TryAttachMember(member, this))
            return GuideXosVerticalStackResult.MembershipConflict;

        _members[_memberCount++] = stackMember;
        GuideXosVerticalStackResult result = PerformLayout();
        if (result == GuideXosVerticalStackResult.LaidOut)
            return GuideXosVerticalStackResult.Added;

        _members[--_memberCount] = null;
        DetachMember(stackMember, this);
        return GuideXosVerticalStackResult.LayoutFailed;
    }

    public GuideXosVerticalStackResult TryRemoveMember(object member)
    {
        int index = FindMemberIndex(member);
        if (index < 0) return GuideXosVerticalStackResult.NotMember;
        IGuideXosVerticalStackMember removed = _members[index];
        DetachMember(removed, this);
        for (int move = index + 1; move < _memberCount; move++)
            _members[move - 1] = _members[move];
        _members[--_memberCount] = null;
        GuideXosVerticalStackResult result = PerformLayout();
        return result == GuideXosVerticalStackResult.LaidOut
            ? GuideXosVerticalStackResult.Removed
            : GuideXosVerticalStackResult.LayoutFailed;
    }

    public GuideXosVerticalStackResult Clear()
    {
        if (_memberCount == 0) return GuideXosVerticalStackResult.Empty;
        for (int index = 0; index < _memberCount; index++)
            DetachMember(_members[index], this);
        Array.Clear(_members, 0, _memberCount);
        _memberCount = 0;
        RecomputeEmptyHeight();
        return GuideXosVerticalStackResult.Cleared;
    }

    public bool ContainsMember(object member) => FindMemberIndex(member) >= 0;

    public object GetMemberAt(int index)
    {
        return index >= 0 && index < _memberCount ? _members[index] : null;
    }

    /// <summary>
    /// Places visible members in insertion order. Spacing is between visible
    /// members only; hidden members consume neither a slot nor spacing.
    /// </summary>
    public GuideXosVerticalStackResult PerformLayout()
    {
        if (!TryComputeLayout(out int[] widths, out int[] ys,
                out int computedHeight))
            return GuideXosVerticalStackResult.LayoutFailed;

        int visibleIndex = 0;
        for (int index = 0; index < _memberCount; index++)
        {
            object member = _members[index];
            if (!IsMemberVisible(member)) continue;
            if (!TrySetMemberBounds(member,
                    _x + _leftPadding, ys[visibleIndex], widths[visibleIndex]))
                return GuideXosVerticalStackResult.LayoutFailed;
            visibleIndex++;
        }

        _contentHeight = computedHeight;
        ++_layoutCount;
        return GuideXosVerticalStackResult.LaidOut;
    }

    /// <summary>
    /// Arranges members and explicitly asks the supplied ScrollView to refresh
    /// its derived logical bounds/extent. The two objects remain independent.
    /// </summary>
    public GuideXosVerticalStackResult PerformLayout(GuideXosScrollView scrollView)
    {
        GuideXosVerticalStackResult result = PerformLayout();
        if (result != GuideXosVerticalStackResult.LaidOut || scrollView == null)
            return result;
        return scrollView.TryRecalculateContentExtent(this)
            ? result : GuideXosVerticalStackResult.LayoutFailed;
    }

    internal bool ContainsAllMembersIn(GuideXosScrollView scrollView)
    {
        if (scrollView == null) return false;
        for (int index = 0; index < _memberCount; index++)
            if (!scrollView.ContainsMember(_members[index])) return false;
        return true;
    }

    private bool TryComputeLayout(out int[] widths, out int[] ys,
        out int computedHeight)
    {
        widths = _computedWidths;
        ys = _computedYs;
        computedHeight = 0;
        long currentY = (long)_y + _topPadding;
        int visibleCount = 0;
        for (int index = 0; index < _memberCount; index++)
        {
            object member = _members[index];
            if (!IsMemberVisible(member)) continue;
            int width = WidthPolicy == GuideXosVerticalStackWidthPolicy.KeepWidth
                ? GetMemberWidth(member) : ComputeStretchWidth(member);
            if (width < GetMemberMinimumWidth(member) ||
                width > GetMemberMaximumWidth(member) ||
                (RequiresCharacterAlignment(member) && width % 8 != 0))
                return false;
            long y = currentY;
            long right = (long)_x + _leftPadding + width;
            long bottom = y + GetMemberHeight(member);
            if (y < 0 || y > MaximumSupportedCoordinate ||
                right > MaximumSupportedCoordinate + 1L ||
                bottom > MaximumSupportedCoordinate + 1L ||
                y > MaximumSupportedCoordinate - GetMemberHeight(member))
                return false;
            widths[visibleCount] = width;
            ys[visibleCount] = (int)y;
            currentY = bottom;
            ++visibleCount;
            if (HasVisibleMemberAfter(index))
                currentY += _spacing;
        }

        long end = currentY + _bottomPadding;
        long height = end - _y;
        if (height < 0 || height > int.MaxValue ||
            end > MaximumSupportedCoordinate + 1L)
            return false;
        computedHeight = (int)height;
        return true;
    }

    private int ComputeStretchWidth(object member)
    {
        long available = (long)_width - _leftPadding - _rightPadding;
        if (available > int.MaxValue) return 0;
        int width = (int)available;
        if (RequiresCharacterAlignment(member))
            width -= width % 8;
        return width;
    }

    private bool HasVisibleMemberAfter(int index)
    {
        for (int next = index + 1; next < _memberCount; next++)
            if (IsMemberVisible(_members[next])) return true;
        return false;
    }

    private void RecomputeEmptyHeight()
    {
        long height = (long)_topPadding + _bottomPadding;
        _contentHeight = height > int.MaxValue ? int.MaxValue : (int)height;
    }

    private int FindMemberIndex(object member)
    {
        for (int index = 0; index < _memberCount; index++)
            if (ReferenceEquals(_members[index], member)) return index;
        return -1;
    }

    private static bool IsValidPadding(int value)
    {
        return value >= 0 && value <= MaximumSupportedPadding;
    }

    private static bool IsMemberVisible(object member)
    {
        return member switch
        {
            GuideXosButton button => button.Visible,
            GuideXosCheckBox checkBox => checkBox.Visible,
            GuideXosLabel label => label.Visible,
            GuideXosSeparator separator => separator.Visible,
            GuideXosRadioButton radio => radio.Visible,
            GuideXosProgressBar progress => progress.Visible,
            GuideXosComboBox combo => combo.Visible,
            _ => false,
        };
    }

    private static int GetMemberWidth(object member)
    {
        return member switch
        {
            GuideXosButton button => button.Width,
            GuideXosCheckBox checkBox => checkBox.Width,
            GuideXosLabel label => label.Width,
            GuideXosSeparator separator => separator.Width,
            GuideXosRadioButton radio => radio.Width,
            GuideXosProgressBar progress => progress.Width,
            GuideXosComboBox combo => combo.Width,
            _ => 0,
        };
    }

    private static int GetMemberHeight(object member)
    {
        return member switch
        {
            GuideXosButton button => button.Height,
            GuideXosCheckBox checkBox => checkBox.Height,
            GuideXosLabel label => label.Height,
            GuideXosSeparator separator => separator.Height,
            GuideXosRadioButton radio => radio.Height,
            GuideXosProgressBar progress => progress.Height,
            GuideXosComboBox combo => combo.Height,
            _ => 0,
        };
    }

    private static int GetMemberMinimumWidth(object member)
    {
        return member switch
        {
            GuideXosButton => GuideXosButton.MinimumSupportedWidth,
            GuideXosCheckBox => GuideXosCheckBox.MinimumSupportedWidth,
            GuideXosLabel => GuideXosLabel.CharacterWidth,
            GuideXosSeparator => GuideXosSeparator.MinimumSupportedWidth,
            GuideXosRadioButton => GuideXosRadioButton.MinimumSupportedWidth,
            GuideXosProgressBar => GuideXosProgressBar.MinimumSupportedWidth,
            GuideXosComboBox => GuideXosComboBox.MinimumSupportedWidth,
            _ => int.MaxValue,
        };
    }

    private static int GetMemberMaximumWidth(object member)
    {
        return member switch
        {
            GuideXosButton => GuideXosButton.MaximumSupportedWidth,
            GuideXosCheckBox => GuideXosCheckBox.MaximumSupportedWidth,
            GuideXosLabel => GuideXosLabel.MaximumRenderWidth * GuideXosLabel.CharacterWidth,
            GuideXosSeparator => GuideXosSeparator.MaximumSupportedWidth,
            GuideXosRadioButton => GuideXosRadioButton.MaximumSupportedWidth,
            GuideXosProgressBar => GuideXosProgressBar.MaximumSupportedWidth,
            GuideXosComboBox => GuideXosComboBox.MaximumSupportedWidth,
            _ => 0,
        };
    }

    private static bool RequiresCharacterAlignment(object member)
    {
        return member is GuideXosLabel || member is GuideXosSeparator ||
            member is GuideXosProgressBar;
    }

    private static bool TrySetMemberBounds(
        object member, int x, int y, int width)
    {
        return member switch
        {
            GuideXosButton button => button.TrySetVerticalStackBoundsCore(x, y, width),
            GuideXosCheckBox checkBox => checkBox.TrySetVerticalStackBoundsCore(x, y, width),
            GuideXosLabel label => label.TrySetVerticalStackBoundsCore(x, y, width),
            GuideXosSeparator separator => separator.TrySetVerticalStackBoundsCore(x, y, width),
            GuideXosRadioButton radio => radio.TrySetVerticalStackBoundsCore(x, y, width),
            GuideXosProgressBar progress => progress.TrySetVerticalStackBoundsCore(x, y, width),
            GuideXosComboBox combo => combo.TrySetVerticalStackBoundsCore(x, y, width),
            _ => false,
        };
    }

    private static bool TryAttachMember(object member, GuideXosVerticalStack stack)
    {
        return member switch
        {
            GuideXosButton button => button.TryAttachToVerticalStackCore(stack),
            GuideXosCheckBox checkBox => checkBox.TryAttachToVerticalStackCore(stack),
            GuideXosLabel label => label.TryAttachToVerticalStackCore(stack),
            GuideXosSeparator separator => separator.TryAttachToVerticalStackCore(stack),
            GuideXosRadioButton radio => radio.TryAttachToVerticalStackCore(stack),
            GuideXosProgressBar progress => progress.TryAttachToVerticalStackCore(stack),
            GuideXosComboBox combo => combo.TryAttachToVerticalStackCore(stack),
            _ => false,
        };
    }

    private static void DetachMember(
        IGuideXosVerticalStackMember member, GuideXosVerticalStack stack)
    {
        switch (member)
        {
            case GuideXosButton button: button.DetachFromVerticalStackCore(stack); break;
            case GuideXosCheckBox checkBox: checkBox.DetachFromVerticalStackCore(stack); break;
            case GuideXosLabel label: label.DetachFromVerticalStackCore(stack); break;
            case GuideXosSeparator separator: separator.DetachFromVerticalStackCore(stack); break;
            case GuideXosRadioButton radio: radio.DetachFromVerticalStackCore(stack); break;
            case GuideXosProgressBar progress: progress.DetachFromVerticalStackCore(stack); break;
            case GuideXosComboBox combo: combo.DetachFromVerticalStackCore(stack); break;
        }
    }
}
