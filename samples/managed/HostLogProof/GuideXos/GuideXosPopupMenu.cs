using System;

namespace HostLogProof;

public enum GuideXosPopupMenuResult
{
    Ignored = 0,
    Opened = 1,
    Moved = 2,
    Activated = 3,
    Cancelled = 4,
    Closed = 5,
    Disabled = 6,
    Rejected = 7,
}

/// <summary>
/// A bounded managed popup menu. Rows are transient rendering and input state
/// owned by this object; they are not registered child controls or a Panel.
/// </summary>
public sealed class GuideXosPopupMenu
{
    public const int DefaultMaximumItemCount = 8;
    public const int MaximumSupportedItemCount = 8;
    public const int DefaultMaximumItemTextLength = 48;
    public const int MaximumSupportedItemTextLength = 48;
    public const int MinimumSupportedWidth = 64;
    public const int MaximumSupportedWidth = GuideXosButton.MaximumSupportedWidth;
    public const int MaximumSupportedCoordinate = GuideXosButton.MaximumSupportedCoordinate;
    public const int CharacterWidth = 8;
    public const int PopupRowHeight = 18;

    private readonly char[] _itemStorage;
    private readonly int[] _itemLengths;
    private readonly uint[] _commandIds;
    private readonly bool[] _enabledItems;
    private readonly bool[] _separatorItems;
    private readonly int _maximumItemTextLength;
    private int _itemCount;
    private int _activeIndex = -1;
    private int _x;
    private int _y;
    private int _width;
    private bool _enabled = true;
    private bool _visible = true;
    private bool _isOpen;
    private bool _invokerAvailable = true;
    private int _renderedRowCount;
    private uint _rejectedInputCount;
    private bool _dispatchingCommand;

    public GuideXosPopupMenu(
        int x,
        int y,
        int width,
        int maximumItemCount = DefaultMaximumItemCount,
        int maximumItemTextLength = DefaultMaximumItemTextLength)
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

        _itemStorage = new char[maximumItemCount * maximumItemTextLength];
        _itemLengths = new int[maximumItemCount];
        _commandIds = new uint[maximumItemCount];
        _enabledItems = new bool[maximumItemCount];
        _separatorItems = new bool[maximumItemCount];
        _maximumItemTextLength = maximumItemTextLength;
        _x = 0;
        _y = 0;
        _width = MinimumSupportedWidth;
        for (int index = 0; index < _enabledItems.Length; index++)
        {
            _enabledItems[index] = true;
        }
        TrySetBounds(x, y, width);
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _itemCount * PopupRowHeight;
    public int MaximumItemCount => _itemLengths.Length;
    public int MaximumItemTextLength => _maximumItemTextLength;
    public int ItemCount => _itemCount;
    public int ActiveIndex => _activeIndex;
    public bool Enabled => _enabled;
    public bool Visible => _visible;
    public bool IsOpen => _isOpen;
    public bool EffectiveVisible => _visible;
    public bool InvokerAvailable => _invokerAvailable;
    public uint RejectedInputCount => _rejectedInputCount;

    /// <summary>One bounded callback receives the committed command ID.</summary>
    public Action<uint> CommandInvoked { get; set; }

    public bool IsValidIndex(int index)
    {
        return index >= 0 && index < _itemCount;
    }

    public bool IsSeparator(int index)
    {
        return IsValidIndex(index) && _separatorItems[index];
    }

    public bool IsItemEnabled(int index)
    {
        return IsValidIndex(index) && !_separatorItems[index] &&
            _enabledItems[index];
    }

    public uint GetCommandId(int index)
    {
        return IsValidIndex(index) ? _commandIds[index] : 0u;
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
    /// Appends bounded printable ASCII text. Overflow is rejected without
    /// modifying existing entries.
    /// </summary>
    public bool TryAddItem(string text, uint commandId, bool enabled = true)
    {
        if (_itemCount >= MaximumItemCount || commandId == 0u ||
            !IsValidItemText(text))
        {
            ++_rejectedInputCount;
            return false;
        }
        StoreText(text, _itemCount);
        _commandIds[_itemCount] = commandId;
        _enabledItems[_itemCount] = enabled;
        _separatorItems[_itemCount] = false;
        ++_itemCount;
        return true;
    }

    public bool TryAddSeparator()
    {
        if (_itemCount >= MaximumItemCount)
        {
            ++_rejectedInputCount;
            return false;
        }
        _itemLengths[_itemCount] = 0;
        _commandIds[_itemCount] = 0u;
        _enabledItems[_itemCount] = false;
        _separatorItems[_itemCount] = true;
        ++_itemCount;
        return true;
    }

    public bool SetItemEnabled(int index, bool enabled)
    {
        if (!IsValidIndex(index) || _separatorItems[index])
        {
            ++_rejectedInputCount;
            return false;
        }
        _enabledItems[index] = enabled;
        if (!enabled && _activeIndex == index)
        {
            _activeIndex = FindEligible(1, index);
        }
        return true;
    }

    public void ClearItems()
    {
        CloseCore();
        _itemCount = 0;
        _activeIndex = -1;
    }

    public void SetVisible(bool visible)
    {
        _visible = visible;
        if (!visible) CloseCore();
    }

    public void SetEnabled(bool enabled)
    {
        _enabled = enabled;
        if (!enabled) CloseCore();
    }

    /// <summary>
    /// Invalidates the invoking application/panel membership without adding a
    /// parent relationship to the menu. An invalid invoker closes the menu.
    /// </summary>
    public void SetInvokerAvailable(bool available)
    {
        _invokerAvailable = available;
        if (!available) CloseCore();
    }

    public void Reset()
    {
        _enabled = true;
        _visible = true;
        _invokerAvailable = true;
        CloseCore();
        _rejectedInputCount = 0u;
    }

    public bool ContainsPoint(int x, int y)
    {
        return _isOpen && EffectiveVisible &&
            x >= _x && x < _x + _width &&
            y >= _y && y < _y + Height;
    }

    public GuideXosPopupMenuResult Open()
    {
        return Open(_x, _y);
    }

    public GuideXosPopupMenuResult Open(int x, int y)
    {
        if (!EffectiveVisible || !_invokerAvailable)
        {
            return GuideXosPopupMenuResult.Ignored;
        }
        if (_dispatchingCommand) return RejectOperation();
        if (!_enabled) return GuideXosPopupMenuResult.Disabled;
        if (_itemCount == 0 || FindEligible(1, -1) < 0)
        {
            return RejectOperation();
        }
        if (_isOpen) return GuideXosPopupMenuResult.Ignored;

        ClampOrigin(x, y);
        _activeIndex = FindEligible(1, -1);
        _isOpen = true;
        return GuideXosPopupMenuResult.Opened;
    }

    public GuideXosPopupMenuResult Close()
    {
        bool wasOpen = _isOpen;
        CloseCore();
        return wasOpen ? GuideXosPopupMenuResult.Closed :
            GuideXosPopupMenuResult.Ignored;
    }

    public GuideXosPopupMenuResult Cancel()
    {
        if (!_isOpen) return GuideXosPopupMenuResult.Ignored;
        CloseCore();
        return GuideXosPopupMenuResult.Cancelled;
    }

    public GuideXosPopupMenuResult HandlePointerDown(int x, int y)
    {
        if (!EffectiveVisible || !_invokerAvailable || !_isOpen)
        {
            return GuideXosPopupMenuResult.Ignored;
        }
        if (!_enabled) return GuideXosPopupMenuResult.Disabled;
        if (!ContainsPoint(x, y))
        {
            CloseCore();
            return GuideXosPopupMenuResult.Cancelled;
        }

        int row = (y - _y) / PopupRowHeight;
        if (!IsValidIndex(row)) return GuideXosPopupMenuResult.Cancelled;
        if (_separatorItems[row]) return GuideXosPopupMenuResult.Ignored;
        if (!_enabledItems[row]) return GuideXosPopupMenuResult.Disabled;
        _activeIndex = row;
        return ActivateActive();
    }

    public GuideXosPopupMenuResult HandleKey(GuideXosTextInputKey key)
    {
        if (!EffectiveVisible || !_invokerAvailable || !_isOpen)
        {
            return GuideXosPopupMenuResult.Ignored;
        }
        if (!_enabled) return GuideXosPopupMenuResult.Disabled;
        return key switch
        {
            GuideXosTextInputKey.Escape => Cancel(),
            GuideXosTextInputKey.Enter => ActivateActive(),
            GuideXosTextInputKey.Up => MoveActive(-1),
            GuideXosTextInputKey.Down => MoveActive(1),
            GuideXosTextInputKey.Home => MoveToEdge(false),
            GuideXosTextInputKey.End => MoveToEdge(true),
            (GuideXosTextInputKey)' ' => ActivateActive(),
            GuideXosTextInputKey.Tab => Cancel(),
            _ => GuideXosPopupMenuResult.Ignored,
        };
    }

    /// <summary>
    /// Menus do not consume synthetic KeyChar input. Space commits on the
    /// existing KeyDown transport while the menu is open.
    /// </summary>
    public GuideXosPopupMenuResult HandleCharacter(char character)
    {
        return GuideXosPopupMenuResult.Ignored;
    }

    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!_isOpen || !EffectiveVisible)
        {
            return ClearRenderedRows(surface) ? GuideXosResult.Success :
                GuideXosResult.InvalidArgument;
        }

        Span<byte> line = stackalloc byte[64];
        for (int row = 0; row < _itemCount; row++)
        {
            line.Clear();
            int position = 0;
            if (_separatorItems[row])
            {
                for (int column = 0; column < Math.Min(16, line.Length); column++)
                {
                    line[position++] = (byte)'-';
                }
            }
            else
            {
                line[position++] = row == _activeIndex ? (byte)'>' : (byte)' ';
                line[position++] = _enabledItems[row] ? (byte)' ' : (byte)'x';
                line[position++] = (byte)' ';
                int length = Math.Min(_itemLengths[row], line.Length - position);
                for (int column = 0; column < length; column++)
                {
                    line[position++] = (byte)_itemStorage[
                        row * _maximumItemTextLength + column];
                }
            }
            if (surface.TrySetText(_x, _y + row * PopupRowHeight,
                    line[..position]) != GuideXosResult.Success)
            {
                return GuideXosResult.InvalidArgument;
            }
        }
        _renderedRowCount = _itemCount;
        return GuideXosResult.Success;
    }

    internal void Blur()
    {
        CloseCore();
    }

    private GuideXosPopupMenuResult MoveActive(int delta)
    {
        int next = FindEligible(delta, _activeIndex);
        if (next < 0 || next == _activeIndex)
        {
            return GuideXosPopupMenuResult.Ignored;
        }
        _activeIndex = next;
        return GuideXosPopupMenuResult.Moved;
    }

    private GuideXosPopupMenuResult MoveToEdge(bool end)
    {
        int next = end ? FindLastEligible() : FindEligible(1, -1);
        if (next < 0 || next == _activeIndex)
        {
            return GuideXosPopupMenuResult.Ignored;
        }
        _activeIndex = next;
        return GuideXosPopupMenuResult.Moved;
    }

    private GuideXosPopupMenuResult ActivateActive()
    {
        if (!IsItemEnabled(_activeIndex))
        {
            return GuideXosPopupMenuResult.Rejected;
        }
        uint commandId = _commandIds[_activeIndex];
        CloseCore();
        Action<uint> callback = CommandInvoked;
        if (callback != null && !_dispatchingCommand)
        {
            _dispatchingCommand = true;
            try
            {
                callback(commandId);
            }
            finally
            {
                _dispatchingCommand = false;
            }
        }
        return GuideXosPopupMenuResult.Activated;
    }

    private int FindEligible(int direction, int start)
    {
        if (_itemCount == 0) return -1;
        int index = start;
        for (int count = 0; count < _itemCount; count++)
        {
            index += direction;
            if (index < 0) index = _itemCount - 1;
            if (index >= _itemCount) index = 0;
            if (IsItemEnabled(index)) return index;
        }
        return -1;
    }

    private int FindLastEligible()
    {
        for (int index = _itemCount - 1; index >= 0; index--)
        {
            if (IsItemEnabled(index)) return index;
        }
        return -1;
    }

    private GuideXosPopupMenuResult RejectOperation()
    {
        ++_rejectedInputCount;
        return GuideXosPopupMenuResult.Rejected;
    }

    private void StoreText(string text, int index)
    {
        for (int character = 0; character < text.Length; character++)
        {
            _itemStorage[index * _maximumItemTextLength + character] =
                text[character];
        }
        _itemLengths[index] = text.Length;
    }

    private void ClampOrigin(int x, int y)
    {
        int height = Math.Max(PopupRowHeight, Height);
        _x = Math.Clamp(x, 0, MaximumSupportedCoordinate - _width);
        _y = Math.Clamp(y, 0, MaximumSupportedCoordinate - height);
    }

    private bool TrySetBounds(int x, int y, int width)
    {
        if (x < 0 || y < 0 || width < MinimumSupportedWidth ||
            width > MaximumSupportedWidth ||
            x > MaximumSupportedCoordinate - width)
        {
            ++_rejectedInputCount;
            return false;
        }
        _x = x;
        _y = y;
        _width = width;
        return true;
    }

    public bool TrySetOrigin(int x, int y)
    {
        if (_isOpen)
        {
            ++_rejectedInputCount;
            return false;
        }
        return TrySetBounds(x, y, _width);
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

    private void CloseCore()
    {
        _isOpen = false;
        _activeIndex = -1;
    }

    private bool ClearRenderedRows(GuideXosSurface surface)
    {
        for (int row = 0; row < _renderedRowCount; row++)
        {
            if (surface.TrySetText(_x, _y + row * PopupRowHeight,
                    ReadOnlySpan<byte>.Empty) != GuideXosResult.Success)
            {
                return false;
            }
        }
        _renderedRowCount = 0;
        return true;
    }
}
