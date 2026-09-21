namespace HostLogProof;

/// <summary>
/// Fixed-capacity mutually-exclusive membership for independent radio
/// buttons. Registration order is the deterministic order used by optional
/// arrow navigation; the group is not a Panel owner or a second UI registry.
/// </summary>
public sealed class GuideXosRadioGroup
{
    public const int DefaultMaximumMemberCount = 4;
    public const int MaximumSupportedMemberCount = 4;
    private const int MaximumSelectionTransactions = 8;

    private readonly GuideXosRadioButton[] _members;
    private readonly int _capacity;
    private int _memberCount;
    private int _selectedIndex = -1;
    private uint _rejectedOperationCount;
    private uint _selectionTransactionCount;
    private bool _applyingSelection;
    private bool _hasPendingSelection;
    private int _pendingSelectionIndex = -1;

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
    public uint SelectionTransactionCount => _selectionTransactionCount;

    public bool TryRegister(GuideXosRadioButton button)
    {
        if (button == null || _memberCount >= _capacity ||
            button.Group != null || Contains(button))
        {
            ++_rejectedOperationCount;
            return false;
        }

        int index = _memberCount;
        _members[index] = button;
        button.AttachGroup(this, index);
        ++_memberCount;

        // Last registered initially checked member wins. This policy is
        // deterministic and goes through the same ordered callback path as a
        // later programmatic or input selection.
        if (button.Checked) RequestSelection(index);
        return true;
    }

    public bool TryUnregister(GuideXosRadioButton button)
    {
        if (!TryGetMemberIndex(button, out int index))
        {
            ++_rejectedOperationCount;
            return false;
        }

        if (_selectedIndex == index) RequestSelection(-1);
        button.DetachGroup();
        for (int move = index + 1; move < _memberCount; move++)
        {
            _members[move - 1] = _members[move];
            _members[move - 1].SetGroupIndex(move - 1);
        }
        _members[--_memberCount] = null;
        if (_selectedIndex > index) --_selectedIndex;
        return true;
    }

    public bool TrySelect(GuideXosRadioButton button)
    {
        if (!TryGetMemberIndex(button, out int index) || !button.Enabled)
        {
            ++_rejectedOperationCount;
            return false;
        }
        return RequestSelection(index);
    }

    public bool TrySelectIndex(int index)
    {
        if (index < 0 || index >= _memberCount || !_members[index].Enabled)
        {
            ++_rejectedOperationCount;
            return false;
        }
        return RequestSelection(index);
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
        _selectionTransactionCount = 0u;
        _applyingSelection = false;
        _hasPendingSelection = false;
        _pendingSelectionIndex = -1;
    }

    internal bool TryMove(
        GuideXosRadioButton current, bool reverse, out int targetIndex)
    {
        targetIndex = -1;
        if (!TryGetMoveTarget(current, reverse, out targetIndex))
        {
            ++_rejectedOperationCount;
            return false;
        }
        return RequestSelection(targetIndex);
    }

    internal bool TryGetMoveTarget(
        GuideXosRadioButton current, bool reverse, out int targetIndex)
    {
        targetIndex = -1;
        if (!TryGetMemberIndex(current, out int currentIndex)) return false;

        int index = currentIndex;
        for (int count = 0; count < _memberCount; count++)
        {
            index += reverse ? -1 : 1;
            if (index < 0) index = _memberCount - 1;
            if (index >= _memberCount) index = 0;
            GuideXosRadioButton candidate = _members[index];
            if (candidate.Enabled && candidate.EffectiveVisible)
            {
                targetIndex = index;
                return true;
            }
        }
        return false;
    }

    internal void SetCheckedProgrammatically(GuideXosRadioButton button)
    {
        if (TryGetMemberIndex(button, out int index)) RequestSelection(index);
    }

    internal void ClearSelectionFor(
        GuideXosRadioButton button, bool notify = true)
    {
        if (!TryGetMemberIndex(button, out int index)) return;
        if (_selectedIndex == index)
        {
            if (notify) RequestSelection(-1);
            else
            {
                _selectedIndex = -1;
                _members[index].SetSelectedInternal(false, false);
            }
        }
    }

    private bool RequestSelection(int index)
    {
        if (index < -1 || index >= _memberCount)
        {
            ++_rejectedOperationCount;
            return false;
        }
        if (_applyingSelection)
        {
            _pendingSelectionIndex = index;
            _hasPendingSelection = true;
            return true;
        }

        _applyingSelection = true;
        try
        {
            int requested = index;
            for (int transaction = 0;
                 transaction < MaximumSelectionTransactions;
                 transaction++)
            {
                _hasPendingSelection = false;
                if (!(requested == _selectedIndex &&
                    IsOnlySelected(requested)))
                {
                    ++_selectionTransactionCount;
                    ApplySelectionOnce(requested);
                }
                if (!_hasPendingSelection) break;
                requested = _pendingSelectionIndex;
            }
            _hasPendingSelection = false;
            return true;
        }
        finally
        {
            _applyingSelection = false;
            _pendingSelectionIndex = -1;
        }
    }

    private void ApplySelectionOnce(int index)
    {
        int previous = _selectedIndex;
        _selectedIndex = -1;
        if (previous >= 0 && previous < _memberCount)
        {
            _members[previous].SetSelectedInternal(false);
        }
        for (int memberIndex = 0; memberIndex < _memberCount; memberIndex++)
        {
            if (memberIndex != index && _members[memberIndex].Checked)
            {
                _members[memberIndex].SetSelectedInternal(false);
            }
        }
        if (index >= 0)
        {
            _selectedIndex = index;
            _members[index].SetSelectedInternal(true);
        }
    }

    private bool IsOnlySelected(int index)
    {
        for (int memberIndex = 0; memberIndex < _memberCount; memberIndex++)
        {
            if (memberIndex != index && _members[memberIndex].Checked)
            {
                return false;
            }
        }
        return index < 0 || _members[index].Checked;
    }

    private bool Contains(GuideXosRadioButton button)
    {
        for (int index = 0; index < _memberCount; index++)
        {
            if (ReferenceEquals(_members[index], button)) return true;
        }
        return false;
    }

    private bool TryGetMemberIndex(
        GuideXosRadioButton button, out int index)
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
