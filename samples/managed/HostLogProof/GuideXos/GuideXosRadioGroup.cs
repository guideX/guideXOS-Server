using System;

namespace HostLogProof;

/// <summary>
/// Explicitly bounded mutually-exclusive membership for radio buttons.
/// Registration order is the deterministic order used by arrow navigation.
/// </summary>
public sealed class GuideXosRadioGroup
{
    public const int DefaultMaximumMemberCount = 4;
    public const int MaximumSupportedMemberCount = 4;

    private readonly GuideXosRadioButton[] _members;
    private readonly int _capacity;
    private int _memberCount;
    private int _selectedIndex = -1;
    private uint _rejectedOperationCount;

    public GuideXosRadioGroup(int maximumMemberCount = DefaultMaximumMemberCount)
    {
        if (maximumMemberCount < 1 ||
            maximumMemberCount > MaximumSupportedMemberCount)
        {
            maximumMemberCount = DefaultMaximumMemberCount;
        }
        _capacity = maximumMemberCount;
        _members = new GuideXosRadioButton[maximumMemberCount];
    }

    public int Capacity => _capacity;
    public int MemberCount => _memberCount;
    public int SelectedIndex => _selectedIndex;
    public bool HasSelection => _selectedIndex >= 0;
    public GuideXosRadioButton SelectedMember =>
        _selectedIndex >= 0 ? _members[_selectedIndex] : null;
    public uint RejectedOperationCount => _rejectedOperationCount;

    public bool TryRegister(GuideXosRadioButton button)
    {
        if (button == null || _memberCount >= _capacity ||
            button.Group != null || Contains(button))
        {
            ++_rejectedOperationCount;
            return false;
        }

        _members[_memberCount] = button;
        button.AttachGroup(this, _memberCount);
        ++_memberCount;
        return true;
    }

    public bool TrySelect(GuideXosRadioButton button)
    {
        if (!TryGetMemberIndex(button, out int index) ||
            !button.Enabled)
        {
            ++_rejectedOperationCount;
            return false;
        }
        SelectIndexInternal(index);
        return true;
    }

    public bool TrySelectIndex(int index)
    {
        if (index < 0 || index >= _memberCount ||
            !_members[index].Enabled)
        {
            ++_rejectedOperationCount;
            return false;
        }
        SelectIndexInternal(index);
        return true;
    }

    public GuideXosRadioButton GetMember(int index)
    {
        return index >= 0 && index < _memberCount ? _members[index] : null;
    }

    public bool TryMoveNext(GuideXosRadioButton current, out int targetIndex)
    {
        return TryMove(current, false, out targetIndex);
    }

    public bool TryMovePrevious(GuideXosRadioButton current, out int targetIndex)
    {
        return TryMove(current, true, out targetIndex);
    }

    public void Reset()
    {
        for (int index = 0; index < _memberCount; index++)
        {
            _members[index].DetachGroup();
            _members[index] = null;
        }
        _memberCount = 0;
        _selectedIndex = -1;
        _rejectedOperationCount = 0u;
    }

    internal bool TryMove(
        GuideXosRadioButton current, bool reverse, out int targetIndex)
    {
        targetIndex = -1;
        if (!TryGetMemberIndex(current, out int currentIndex))
        {
            ++_rejectedOperationCount;
            return false;
        }

        int index = currentIndex;
        for (int count = 0; count < _memberCount; count++)
        {
            index += reverse ? -1 : 1;
            if (index < 0) index = _memberCount - 1;
            if (index >= _memberCount) index = 0;
            if (_members[index].Enabled)
            {
                SelectIndexInternal(index);
                targetIndex = index;
                return true;
            }
        }
        return false;
    }

    internal void HandleMemberDisabled(GuideXosRadioButton button)
    {
        if (!TryGetMemberIndex(button, out int index) ||
            _selectedIndex != index)
        {
            return;
        }

        int next = FindEnabledFrom(index, false);
        if (next >= 0) SelectIndexInternal(next);
        else ClearSelectionInternal();
    }

    internal void ClearSelectionFor(GuideXosRadioButton button)
    {
        if (TryGetMemberIndex(button, out int index) &&
            _selectedIndex == index)
        {
            ClearSelectionInternal();
        }
    }

    private int FindEnabledFrom(int start, bool reverse)
    {
        for (int count = 0; count < _memberCount; count++)
        {
            int index = start + (reverse ? -count : count);
            while (index < 0) index += _memberCount;
            while (index >= _memberCount) index -= _memberCount;
            if (_members[index].Enabled) return index;
        }
        return -1;
    }

    private void SelectIndexInternal(int index)
    {
        for (int memberIndex = 0; memberIndex < _memberCount; memberIndex++)
        {
            _members[memberIndex].SetSelectedInternal(memberIndex == index);
        }
        _selectedIndex = index;
    }

    private void ClearSelectionInternal()
    {
        for (int index = 0; index < _memberCount; index++)
        {
            _members[index].SetSelectedInternal(false);
        }
        _selectedIndex = -1;
    }

    private bool Contains(GuideXosRadioButton button)
    {
        for (int index = 0; index < _memberCount; index++)
        {
            if (ReferenceEquals(_members[index], button)) return true;
        }
        return false;
    }

    private bool TryGetMemberIndex(GuideXosRadioButton button, out int index)
    {
        for (int candidate = 0; candidate < _memberCount; candidate++)
        {
            if (ReferenceEquals(_members[candidate], button))
            {
                index = candidate;
                return true;
            }
        }
        index = -1;
        return false;
    }
}
