using System;

namespace HostLogProof;

public enum GuideXosComboBoxResult
{
    Ignored = 0,
    Focused = 1,
    Opened = 2,
    Moved = 3,
    SelectionChanged = 4,
    Closed = 5,
    Cancelled = 6,
    Disabled = 7,
    Rejected = 8,
}

/// <summary>
/// A bounded, non-editable managed selector. The drop-down is transient state
/// owned by this control; it is not a second registered control or Panel.
/// </summary>
public sealed class GuideXosComboBox : IGuideXosVerticalStackMember
{
    public const int DefaultMaximumItemCount = GuideXosListBox.DefaultMaximumItemCount;
    public const int MaximumSupportedItemCount = GuideXosListBox.MaximumSupportedItemCount;
    public const int DefaultMaximumItemTextLength = GuideXosListBox.DefaultMaximumLabelLength;
    public const int MaximumSupportedItemTextLength = GuideXosListBox.MaximumSupportedLabelLength;
    public const int DefaultVisibleRowCount = GuideXosListBox.DefaultVisibleRowCount;
    public const int MaximumVisibleRowCount = GuideXosListBox.MaximumVisibleRowCount;
    public const int MinimumSupportedWidth = GuideXosButton.MinimumSupportedWidth;
    public const int MaximumSupportedWidth = GuideXosButton.MaximumSupportedWidth;
    public const int MinimumSupportedHeight = GuideXosButton.MinimumSupportedHeight;
    public const int MaximumSupportedHeight = GuideXosButton.MaximumSupportedHeight;
    public const int MaximumSupportedCoordinate = GuideXosButton.MaximumSupportedCoordinate;
    public const int CharacterWidth = 8;
    public const int PopupRowHeight = 18;

    private readonly char[] _itemStorage;
    private readonly int[] _itemLengths;
    private readonly int _maximumItemTextLength;
    private readonly int _visibleRowCount;
    private int _itemCount;
    private int _selectedIndex = -1;
    private int _activeIndex = -1;
    private int _x;
    private int _y;
    private int _width;
    private int _height;
    private bool _enabled = true;
    private bool _visible = true;
    private bool _isFocused;
    private bool _isOpen;
    private bool _panelVisible = true;
    private GuideXosPanel _panelOwner;
    private GuideXosScrollView _scrollViewOwner;
    private GuideXosVerticalStack _verticalStackOwner;
    private uint _rejectedInputCount;
    private bool _dispatchingChanged;
    private bool _popupRowsRendered;

    public GuideXosComboBox(
        int x,
        int y,
        int width,
        int height,
        int maximumItemCount = DefaultMaximumItemCount,
        int maximumItemTextLength = DefaultMaximumItemTextLength,
        int visibleRowCount = DefaultVisibleRowCount)
    {
        if (maximumItemCount < 1 || maximumItemCount > MaximumSupportedItemCount)
        {
            maximumItemCount = DefaultMaximumItemCount;
        }
        if (maximumItemTextLength < 1 ||
            maximumItemTextLength > MaximumSupportedItemTextLength)
        {
            maximumItemTextLength = DefaultMaximumItemTextLength;
        }
        if (visibleRowCount < 1 || visibleRowCount > MaximumVisibleRowCount)
        {
            visibleRowCount = DefaultVisibleRowCount;
        }

        _itemStorage = new char[maximumItemCount * maximumItemTextLength];
        _itemLengths = new int[maximumItemCount];
        _maximumItemTextLength = maximumItemTextLength;
        _visibleRowCount = visibleRowCount;
        _x = 0;
        _y = 0;
        _width = MinimumSupportedWidth;
        _height = MinimumSupportedHeight;
        TrySetBounds(x, y, width, height);
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int MaximumItemCount => _itemLengths.Length;
    public int MaximumItemTextLength => _maximumItemTextLength;
    public int VisibleRowCount => _visibleRowCount;
    public int ItemCount => _itemCount;
    public int SelectedIndex
    {
        get => _selectedIndex;
        set => TrySetSelectedIndex(value);
    }
    public int ActiveIndex => _activeIndex;
    public string SelectedText => IsValidIndex(_selectedIndex)
        ? GetItemText(_selectedIndex) : string.Empty;
    public bool HasSelection => IsValidIndex(_selectedIndex);
    public bool Enabled => _enabled;
    public bool Visible => _visible;
    public bool IsFocused => _isFocused;
    public bool IsOpen => _isOpen;
    public bool EffectiveVisible => _visible && _panelVisible;
    public GuideXosPanel ParentPanel => _panelOwner;
    public GuideXosScrollView ParentScrollView => _scrollViewOwner;
    internal GuideXosVerticalStack VerticalStackOwner => _verticalStackOwner;
    public uint RejectedInputCount => _rejectedInputCount;

    /// <summary>One bounded callback for each real committed selection change.</summary>
    public Action<int> Changed { get; set; }

    public bool IsValidIndex(int index)
    {
        return index >= 0 && index < _itemCount;
    }

    public string GetItemText(int index)
    {
        if (!IsValidIndex(index)) return string.Empty;
        int length = _itemLengths[index];
        if (length == 0) return string.Empty;
        char[] text = new char[length];
        for (int character = 0; character < length; character++)
        {
            text[character] = _itemStorage[
                index * _maximumItemTextLength + character];
        }
        return new string(text);
    }

    /// <summary>
    /// Appends bounded printable ASCII text. Empty text is a valid item and an
    /// overflow item is rejected without modifying existing storage.
    /// </summary>
    public bool TryAddItem(string text)
    {
        if (_itemCount >= MaximumItemCount || !IsValidItemText(text))
        {
            ++_rejectedInputCount;
            return false;
        }

        for (int index = 0; index < text.Length; index++)
        {
            _itemStorage[
                _itemCount * _maximumItemTextLength + index] = text[index];
        }
        _itemLengths[_itemCount] = text.Length;
        ++_itemCount;
        return true;
    }

    public bool AddItem(string text)
    {
        return TryAddItem(text);
    }

    public bool TrySetSelectedIndex(int index)
    {
        if (index != -1 && !IsValidIndex(index))
        {
            ++_rejectedInputCount;
            return false;
        }

        CloseDropDown();
        CommitSelection(index);
        return true;
    }

    public void ClearItems()
    {
        CloseDropDown();
        _itemCount = 0;
        _activeIndex = -1;
        CommitSelection(-1);
    }

    public void SetVisible(bool visible)
    {
        _visible = visible;
        if (!visible)
        {
            _isFocused = false;
            CloseDropDown();
        }
    }

    public void SetEnabled(bool enabled)
    {
        _enabled = enabled;
        if (!enabled)
        {
            _isFocused = false;
            CloseDropDown();
        }
    }

    public void Focus()
    {
        if (_enabled && EffectiveVisible) _isFocused = true;
    }

    public void Blur()
    {
        _isFocused = false;
        CloseDropDown();
    }

    public void Reset()
    {
        _enabled = true;
        _visible = true;
        _isFocused = false;
        CloseDropDown();
        _rejectedInputCount = 0u;
        if (_panelOwner == null) _panelVisible = true;
    }

    public bool ContainsPoint(int x, int y)
    {
        return EffectiveVisible && x >= _x && y >= _y &&
            x < _x + _width && y < _y + _height;
    }

    public bool ContainsDropDownPoint(int x, int y)
    {
        return _isOpen && EffectiveVisible &&
            x >= _x && x < _x + _width &&
            y >= PopupY && y < PopupY + _visibleRowCount * PopupRowHeight;
    }

    public int PopupY => _y + _height;

    public GuideXosComboBoxResult Open()
    {
        if (!EffectiveVisible) return GuideXosComboBoxResult.Ignored;
        if (!_enabled) return GuideXosComboBoxResult.Disabled;
        if (_itemCount == 0) return RejectOperation();
        if (_isOpen) return GuideXosComboBoxResult.Ignored;

        _activeIndex = IsValidIndex(_selectedIndex) ? _selectedIndex : 0;
        _isOpen = true;
        return GuideXosComboBoxResult.Opened;
    }

    public GuideXosComboBoxResult Close()
    {
        return CloseDropDown()
            ? GuideXosComboBoxResult.Closed
            : GuideXosComboBoxResult.Ignored;
    }

    public GuideXosComboBoxResult HandlePointerDown(int x, int y)
    {
        if (!EffectiveVisible) return GuideXosComboBoxResult.Ignored;
        if (!_enabled) return GuideXosComboBoxResult.Disabled;

        if (!_isOpen)
        {
            if (!ContainsPoint(x, y)) return GuideXosComboBoxResult.Ignored;
            _isFocused = true;
            return Open();
        }

        if (ContainsDropDownPoint(x, y))
        {
            int row = (y - PopupY) / PopupRowHeight;
            int index = _activeIndex - VisibleRowStart();
            index = VisibleRowStart() + row;
            if (!IsValidIndex(index)) return CancelDropDown();
            _activeIndex = index;
            bool changed = _selectedIndex != index;
            CommitSelection(index);
            _isOpen = false;
            _activeIndex = -1;
            return changed
                ? GuideXosComboBoxResult.SelectionChanged
                : GuideXosComboBoxResult.Closed;
        }

        // The popup owns the whole pointer gesture. A click on the closed
        // face or anywhere outside closes it and is consumed.
        return CancelDropDown();
    }

    public GuideXosComboBoxResult HandleKey(GuideXosTextInputKey key)
    {
        if (!EffectiveVisible) return GuideXosComboBoxResult.Ignored;
        if (!_enabled) return GuideXosComboBoxResult.Disabled;
        if (!_isFocused) return GuideXosComboBoxResult.Ignored;

        if (!_isOpen)
        {
            if (key == GuideXosTextInputKey.Enter) return Open();
            // Space is committed by KeyChar through the existing split
            // gesture path, matching Button and CheckBox semantics.
            return GuideXosComboBoxResult.Ignored;
        }

        switch (key)
        {
            case GuideXosTextInputKey.Escape:
                return CancelDropDown();
            case GuideXosTextInputKey.Enter:
                return CommitActive();
            case GuideXosTextInputKey.Up:
                return MoveActive(-1);
            case GuideXosTextInputKey.Down:
                return MoveActive(1);
            case GuideXosTextInputKey.Home:
                return MoveActiveTo(0);
            case GuideXosTextInputKey.End:
                return MoveActiveTo(_itemCount - 1);
            default:
                return GuideXosComboBoxResult.Ignored;
        }
    }

    public GuideXosComboBoxResult HandleCharacter(char character)
    {
        if (!EffectiveVisible) return GuideXosComboBoxResult.Ignored;
        if (!_enabled) return GuideXosComboBoxResult.Disabled;
        if (!_isFocused || character != ' ')
        {
            return GuideXosComboBoxResult.Ignored;
        }
        return _isOpen ? CommitActive() : Open();
    }

    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!EffectiveVisible)
        {
            return ClearPopupRows(surface) ? GuideXosResult.Success :
                GuideXosResult.InvalidArgument;
        }

        Span<byte> line = stackalloc byte[64];
        line.Clear();
        int position = 0;
        byte marker = !_enabled ? (byte)'x' : _isFocused ? (byte)'>' : (byte)'[';
        if (!AppendByte(line, ref position, marker) ||
            (marker != (byte)'[' && !AppendByte(line, ref position, (byte)'[')) ||
            !AppendByte(line, ref position, (byte)' '))
        {
            return GuideXosResult.InvalidArgument;
        }

        int labelWidth = Math.Max(0, (_width / CharacterWidth) - position - 3);
        if (IsValidIndex(_selectedIndex))
        {
            int length = Math.Min(_itemLengths[_selectedIndex], labelWidth);
            for (int index = 0; index < length; index++)
            {
                if (!AppendByte(line, ref position,
                        (byte)_itemStorage[
                            _selectedIndex * _maximumItemTextLength + index]))
                {
                    return GuideXosResult.InvalidArgument;
                }
            }
        }
        while (position < Math.Max(0, (_width / CharacterWidth) - 3))
        {
            line[position++] = (byte)' ';
        }
        if (!AppendByte(line, ref position, (byte)'v') ||
            !AppendByte(line, ref position, (byte)']') ||
            surface.TrySetText(_x, _y, line[..position]) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }

        if (!_isOpen)
        {
            return ClearPopupRows(surface) ? GuideXosResult.Success :
                GuideXosResult.InvalidArgument;
        }
        int first = VisibleRowStart();
        _popupRowsRendered = true;
        for (int row = 0; row < _visibleRowCount; row++)
        {
            line.Clear();
            position = 0;
            int index = first + row;
            byte active = index == _activeIndex ? (byte)'>' : (byte)' ';
            if (!AppendByte(line, ref position, active) ||
                !AppendByte(line, ref position, index == _selectedIndex
                    ? (byte)'*' : (byte)' ') ||
                !AppendByte(line, ref position, (byte)' '))
            {
                return GuideXosResult.InvalidArgument;
            }
            if (IsValidIndex(index))
            {
                int length = Math.Min(_itemLengths[index],
                    Math.Max(0, (_width / CharacterWidth) - position));
                for (int column = 0; column < length; column++)
                {
                    if (!AppendByte(line, ref position,
                            (byte)_itemStorage[
                                index * _maximumItemTextLength + column]))
                    {
                        return GuideXosResult.InvalidArgument;
                    }
                }
            }
            if (surface.TrySetText(_x, PopupY + row * PopupRowHeight,
                    line[..position]) != GuideXosResult.Success)
            {
                return GuideXosResult.InvalidArgument;
            }
        }
        return GuideXosResult.Success;
    }

    private bool ClearPopupRows(GuideXosSurface surface)
    {
        if (!_popupRowsRendered) return true;
        for (int row = 0; row < _visibleRowCount; row++)
        {
            if (surface.TrySetText(_x, PopupY + row * PopupRowHeight,
                    ReadOnlySpan<byte>.Empty) != GuideXosResult.Success)
            {
                return false;
            }
        }
        _popupRowsRendered = false;
        return true;
    }

    internal bool TryAttachToPanel(GuideXosPanel panel)
    {
        if (panel == null || _panelOwner != null || _scrollViewOwner != null ||
            _verticalStackOwner != null) return false;
        _panelOwner = panel;
        _panelVisible = panel.Visible;
        if (!_panelVisible)
        {
            _isFocused = false;
            CloseDropDown();
        }
        return true;
    }

    internal void SetPanelVisible(bool visible)
    {
        _panelVisible = visible;
        if (!visible)
        {
            _isFocused = false;
            CloseDropDown();
        }
    }

    internal void DetachFromPanel()
    {
        _panelOwner = null;
        _panelVisible = true;
        _isFocused = false;
        CloseDropDown();
    }

    internal bool TryAttachToScrollView(GuideXosScrollView scrollView)
    {
        if (scrollView == null || _panelOwner != null || _scrollViewOwner != null)
            return false;
        _scrollViewOwner = scrollView;
        _isFocused = false;
        return true;
    }

    internal void DetachFromScrollView()
    {
        CloseDropDown();
        _scrollViewOwner = null;
        _isFocused = false;
    }

    private GuideXosComboBoxResult MoveActive(int delta)
    {
        if (_itemCount == 0) return RejectOperation();
        return MoveActiveTo(Math.Clamp(_activeIndex + delta, 0, _itemCount - 1));
    }

    private GuideXosComboBoxResult MoveActiveTo(int index)
    {
        if (!IsValidIndex(index)) return RejectOperation();
        if (_activeIndex == index) return GuideXosComboBoxResult.Ignored;
        _activeIndex = index;
        return GuideXosComboBoxResult.Moved;
    }

    private GuideXosComboBoxResult CommitActive()
    {
        if (!IsValidIndex(_activeIndex)) return RejectOperation();
        bool changed = _selectedIndex != _activeIndex;
        CommitSelection(_activeIndex);
        _isOpen = false;
        _activeIndex = -1;
        return changed
            ? GuideXosComboBoxResult.SelectionChanged
            : GuideXosComboBoxResult.Closed;
    }

    private GuideXosComboBoxResult CancelDropDown()
    {
        if (!_isOpen) return GuideXosComboBoxResult.Ignored;
        CloseDropDown();
        return GuideXosComboBoxResult.Cancelled;
    }

    private bool CloseDropDown()
    {
        bool wasOpen = _isOpen;
        _isOpen = false;
        _activeIndex = -1;
        return wasOpen;
    }

    private void CommitSelection(int index)
    {
        if (_selectedIndex == index) return;
        _selectedIndex = index;
        Action<int> changed = Changed;
        if (changed == null || _dispatchingChanged) return;
        _dispatchingChanged = true;
        try
        {
            changed(_selectedIndex);
        }
        finally
        {
            _dispatchingChanged = false;
        }
    }

    private int VisibleRowStart()
    {
        if (_itemCount <= _visibleRowCount) return 0;
        int first = _activeIndex - _visibleRowCount + 1;
        if (first < 0) first = 0;
        int maximum = _itemCount - _visibleRowCount;
        return first > maximum ? maximum : first;
    }

    private GuideXosComboBoxResult RejectOperation()
    {
        ++_rejectedInputCount;
        return GuideXosComboBoxResult.Rejected;
    }

    private bool TrySetBoundsCore(int x, int y, int width, int height)
    {
        if (x < 0 || y < 0 ||
            width < MinimumSupportedWidth || width > MaximumSupportedWidth ||
            height < MinimumSupportedHeight || height > MaximumSupportedHeight ||
            x > MaximumSupportedCoordinate - width ||
            y > MaximumSupportedCoordinate - height)
        {
            ++_rejectedInputCount;
            return false;
        }
        _x = x;
        _y = y;
        _width = width;
        _height = height;
        return true;
    }

    public bool TrySetBounds(int x, int y, int width, int height)
    {
        if (_panelOwner != null || _scrollViewOwner != null ||
            _verticalStackOwner != null)
        {
            ++_rejectedInputCount;
            return false;
        }
        return TrySetBoundsCore(x, y, width, height);
    }

    internal bool TrySetPanelBounds(int x, int y, int width, int height)
    {
        CloseDropDown();
        return TrySetBoundsCore(x, y, width, height);
    }

    internal bool TrySetScrollViewBounds(int x, int y, int width, int height)
    {
        CloseDropDown();
        return TrySetBoundsCore(x, y, width, height);
    }

    bool IGuideXosVerticalStackMember.Visible => Visible;
    GuideXosPanel IGuideXosVerticalStackMember.ParentPanel => _panelOwner;
    GuideXosVerticalStack IGuideXosVerticalStackMember.VerticalStackOwner =>
        _verticalStackOwner;
    int IGuideXosVerticalStackMember.MinimumWidth => MinimumSupportedWidth;
    int IGuideXosVerticalStackMember.MaximumWidth => MaximumSupportedWidth;
    bool IGuideXosVerticalStackMember.WidthRequiresCharacterAlignment => false;
    bool IGuideXosVerticalStackMember.TrySetVerticalStackBounds(
        int x, int y, int width)
    {
        CloseDropDown();
        return TrySetBoundsCore(x, y, width, _height);
    }
    bool IGuideXosVerticalStackMember.TrySetVerticalStackBounds(
        int x, int y, int width, int height)
    {
        CloseDropDown();
        return TrySetBoundsCore(x, y, width, height);
    }

    internal bool TrySetVerticalStackBoundsCore(int x, int y, int width)
    {
        CloseDropDown();
        return TrySetBoundsCore(x, y, width, _height);
    }

    internal bool TrySetVerticalStackBoundsCore(
        int x, int y, int width, int height)
    {
        CloseDropDown();
        return TrySetBoundsCore(x, y, width, height);
    }
    bool IGuideXosVerticalStackMember.TryAttachToVerticalStack(
        GuideXosVerticalStack stack)
    {
        return TryAttachToVerticalStackCore(stack);
    }
    void IGuideXosVerticalStackMember.DetachFromVerticalStack(
        GuideXosVerticalStack stack)
    {
        if (ReferenceEquals(_verticalStackOwner, stack))
            _verticalStackOwner = null;
    }

    internal bool TryAttachToVerticalStackCore(GuideXosVerticalStack stack)
    {
        if (stack == null || _panelOwner != null || _verticalStackOwner != null)
            return false;
        _verticalStackOwner = stack;
        return true;
    }

    internal void DetachFromVerticalStackCore(GuideXosVerticalStack stack)
    {
        if (ReferenceEquals(_verticalStackOwner, stack))
            _verticalStackOwner = null;
    }

    private bool IsValidItemText(string text)
    {
        if (text == null || text.Length > _maximumItemTextLength) return false;
        for (int index = 0; index < text.Length; index++)
        {
            if (text[index] < 0x20 || text[index] > 0x7E) return false;
        }
        return true;
    }

    private static bool AppendByte(
        Span<byte> destination, ref int position, byte value)
    {
        if (position >= destination.Length) return false;
        destination[position++] = value;
        return true;
    }
}
