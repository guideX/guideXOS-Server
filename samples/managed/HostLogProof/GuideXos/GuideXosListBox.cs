using System;

namespace HostLogProof;

public enum GuideXosListBoxPopulationResult
{
    Added = 0,
    Rejected = 1,
}

public enum GuideXosListBoxResult
{
    Ignored = 0,
    Focused = 1,
    SelectionChanged = 2,
    Activated = 3,
    Rejected = 4,
}

/// <summary>
/// Small bounded single-selection list of printable ASCII labels. The control
/// owns fixed label storage, focus, selected index, and vertical viewport.
/// It consumes ordinary managed input events; it never handles hardware input.
/// </summary>
public sealed class GuideXosListBox
{
    public const int DefaultMaximumItemCount = 16;
    public const int MaximumSupportedItemCount = 64;
    public const int DefaultMaximumLabelLength = 48;
    public const int MaximumSupportedLabelLength = 127;
    public const int DefaultVisibleRowCount = 4;
    public const int MaximumVisibleRowCount = 16;
    public const int DefaultRenderWidth = 48;
    public const int MaximumSupportedRenderWidth = 61;

    private readonly char[] _labelStorage;
    private readonly int[] _labelLengths;
    private readonly int _maximumLabelLength;
    private readonly int _visibleRowCount;
    private readonly int _renderWidth;
    private int _itemCount;
    private int _selectedIndex = -1;
    private int _firstVisibleIndex;
    private bool _isFocused;
    private uint _rejectedOperationCount;

    public GuideXosListBox(
        int maximumItemCount = DefaultMaximumItemCount,
        int maximumLabelLength = DefaultMaximumLabelLength,
        int visibleRowCount = DefaultVisibleRowCount,
        int renderWidth = DefaultRenderWidth)
    {
        if (maximumItemCount < 1 || maximumItemCount > MaximumSupportedItemCount)
        {
            maximumItemCount = DefaultMaximumItemCount;
        }
        if (maximumLabelLength < 1 ||
            maximumLabelLength > MaximumSupportedLabelLength)
        {
            maximumLabelLength = DefaultMaximumLabelLength;
        }
        if (visibleRowCount < 1 || visibleRowCount > MaximumVisibleRowCount)
        {
            visibleRowCount = DefaultVisibleRowCount;
        }
        if (renderWidth < 2 || renderWidth > MaximumSupportedRenderWidth)
        {
            renderWidth = DefaultRenderWidth;
        }

        _labelStorage = new char[maximumItemCount * maximumLabelLength];
        _labelLengths = new int[maximumItemCount];
        _maximumLabelLength = maximumLabelLength;
        _visibleRowCount = visibleRowCount;
        _renderWidth = renderWidth;
    }

    public int MaximumItemCount => _labelLengths.Length;
    public int MaximumLabelLength => _maximumLabelLength;
    public int VisibleRowCount => _visibleRowCount;
    public int RenderWidth => _renderWidth;
    public int ItemCount => _itemCount;
    public int SelectedIndex => _selectedIndex;
    public bool HasSelection => IsValidIndex(_selectedIndex);
    public string SelectedLabel => HasSelection
        ? GetItemLabel(_selectedIndex) : string.Empty;
    public bool IsFocused => _isFocused;
    public int FirstVisibleIndex => _firstVisibleIndex;
    public uint RejectedOperationCount => _rejectedOperationCount;

    public bool IsValidIndex(int index)
    {
        return index >= 0 && index < _itemCount;
    }

    public string GetItemLabel(int index)
    {
        if (!IsValidIndex(index)) return string.Empty;
        return new string(
            _labelStorage, index * _maximumLabelLength, _labelLengths[index]);
    }

    /// <summary>
    /// Appends one label. Overflow is rejected before state is changed.
    /// Labels are bounded printable ASCII so rendering is deterministic.
    /// </summary>
    public GuideXosListBoxPopulationResult TryAdd(string label)
    {
        if (_itemCount >= MaximumItemCount || !IsValidLabel(label))
        {
            ++_rejectedOperationCount;
            return GuideXosListBoxPopulationResult.Rejected;
        }

        int offset = _itemCount * _maximumLabelLength;
        for (int index = 0; index < label.Length; index++)
        {
            _labelStorage[offset + index] = label[index];
        }
        _labelLengths[_itemCount] = label.Length;
        ++_itemCount;
        if (_selectedIndex < 0)
        {
            _selectedIndex = 0;
            EnsureSelectionVisible();
        }
        return GuideXosListBoxPopulationResult.Added;
    }

    public void Clear()
    {
        _itemCount = 0;
        _selectedIndex = -1;
        _firstVisibleIndex = 0;
    }

    public void Focus()
    {
        _isFocused = true;
        EnsureSelectionVisible();
    }

    public void Blur()
    {
        _isFocused = false;
    }

    public void ResetTransientState()
    {
        _isFocused = false;
        _rejectedOperationCount = 0u;
    }

    public void Reset()
    {
        Clear();
        ResetTransientState();
    }

    /// <summary>Allows an application to select a known bounded index.</summary>
    public GuideXosListBoxResult SelectIndex(int index)
    {
        if (!IsValidIndex(index))
        {
            ++_rejectedOperationCount;
            return GuideXosListBoxResult.Rejected;
        }
        return SelectIndexInternal(index);
    }

    public GuideXosListBoxResult HandleKey(GuideXosTextInputKey key)
    {
        if (!_isFocused) return GuideXosListBoxResult.Ignored;
        if (_itemCount == 0)
        {
            ++_rejectedOperationCount;
            return GuideXosListBoxResult.Rejected;
        }

        switch (key)
        {
            case GuideXosTextInputKey.Up:
                return MoveSelection(-1);
            case GuideXosTextInputKey.Down:
                return MoveSelection(1);
            case GuideXosTextInputKey.Home:
                return SelectIndexInternal(0);
            case GuideXosTextInputKey.End:
                return SelectIndexInternal(_itemCount - 1);
            case GuideXosTextInputKey.Enter:
                return IsValidIndex(_selectedIndex)
                    ? GuideXosListBoxResult.Activated
                    : RejectOperation();
            default:
                return RejectOperation();
        }
    }

    /// <summary>
    /// Hits the visible row rectangle at an application-provided origin.
    /// A valid row selects only; Enter remains the activation gesture.
    /// </summary>
    public GuideXosListBoxResult HandlePointerDown(
        int x,
        int y,
        int originX,
        int originY,
        int characterWidth = 8,
        int lineHeight = 18)
    {
        if (characterWidth < 1 || lineHeight < 1 || originX < 0 || originY < 0 ||
            x < originX || y < originY ||
            x >= originX + _renderWidth * characterWidth ||
            y >= originY + _visibleRowCount * lineHeight)
        {
            return GuideXosListBoxResult.Ignored;
        }

        Focus();
        int row = (y - originY) / lineHeight;
        int index = _firstVisibleIndex + row;
        if (!IsValidIndex(index)) return GuideXosListBoxResult.Focused;
        GuideXosListBoxResult result = SelectIndexInternal(index);
        return result == GuideXosListBoxResult.Ignored
            ? GuideXosListBoxResult.Focused : result;
    }

    /// <summary>
    /// Renders exactly VisibleRowCount rows. Unused rows are blank and labels
    /// are clipped to RenderWidth; no hidden horizontal viewport is kept.
    /// </summary>
    public GuideXosResult Render(
        GuideXosSurface surface,
        int x,
        int y,
        int lineHeight = 18)
    {
        if (surface == null || x < 0 || y < 0 || lineHeight < 1)
        {
            return GuideXosResult.InvalidArgument;
        }

        Span<byte> line = stackalloc byte[64];
        int labelWidth = _renderWidth - 2;
        for (int row = 0; row < _visibleRowCount; row++)
        {
            line.Clear();
            int position = 0;
            int index = _firstVisibleIndex + row;
            if (!AppendByte(line, ref position,
                    index == _selectedIndex ? (byte)'>' : (byte)' ') ||
                !AppendByte(line, ref position, (byte)' '))
            {
                return GuideXosResult.InvalidArgument;
            }
            if (IsValidIndex(index))
            {
                int length = Math.Min(_labelLengths[index], labelWidth);
                int offset = index * _maximumLabelLength;
                for (int column = 0; column < length; column++)
                {
                    line[position++] = (byte)_labelStorage[offset + column];
                }
            }
            if (surface.TrySetText(x, y + row * lineHeight,
                    line[..position]) != GuideXosResult.Success)
            {
                return GuideXosResult.InvalidArgument;
            }
        }
        return GuideXosResult.Success;
    }

    private GuideXosListBoxResult MoveSelection(int delta)
    {
        int target = _selectedIndex + delta;
        if (target < 0) target = 0;
        if (target >= _itemCount) target = _itemCount - 1;
        return SelectIndexInternal(target);
    }

    private GuideXosListBoxResult SelectIndexInternal(int index)
    {
        if (!IsValidIndex(index)) return RejectOperation();
        bool changed = _selectedIndex != index;
        _selectedIndex = index;
        EnsureSelectionVisible();
        return changed
            ? GuideXosListBoxResult.SelectionChanged
            : GuideXosListBoxResult.Ignored;
    }

    private void EnsureSelectionVisible()
    {
        if (_itemCount == 0)
        {
            _firstVisibleIndex = 0;
            return;
        }
        if (_selectedIndex < 0) _selectedIndex = 0;
        if (_selectedIndex < _firstVisibleIndex)
        {
            _firstVisibleIndex = _selectedIndex;
        }
        else if (_selectedIndex >= _firstVisibleIndex + _visibleRowCount)
        {
            _firstVisibleIndex = _selectedIndex - _visibleRowCount + 1;
        }
        int maximumFirst = Math.Max(0, _itemCount - _visibleRowCount);
        if (_firstVisibleIndex > maximumFirst) _firstVisibleIndex = maximumFirst;
        if (_firstVisibleIndex < 0) _firstVisibleIndex = 0;
    }

    private GuideXosListBoxResult RejectOperation()
    {
        ++_rejectedOperationCount;
        return GuideXosListBoxResult.Rejected;
    }

    private bool IsValidLabel(string label)
    {
        if (label == null || label.Length > _maximumLabelLength) return false;
        for (int index = 0; index < label.Length; index++)
        {
            if (label[index] < 0x20 || label[index] > 0x7E) return false;
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
