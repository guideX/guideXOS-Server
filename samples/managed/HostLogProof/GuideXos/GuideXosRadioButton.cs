using System;

namespace HostLogProof;

public enum GuideXosRadioButtonResult
{
    Ignored = 0,
    Focused = 1,
    Selected = 2,
    Disabled = 3,
    Moved = 4,
    Rejected = 5,
}

/// <summary>
/// A bounded managed mutually-exclusive choice. Selection is owned by the
/// optional GuideXosRadioGroup; the button owns only its label, bounds,
/// enabled/focus state, pointer and keyboard interpretation, and rendering.
/// </summary>
public sealed class GuideXosRadioButton
{
    public const int DefaultMaximumLabelLength = 32;
    public const int MaximumSupportedLabelLength = 48;
    public const int MinimumSupportedWidth = 32;
    public const int MaximumSupportedWidth = 512;
    public const int MinimumSupportedHeight = 18;
    public const int MaximumSupportedHeight = 128;
    public const int MaximumSupportedCoordinate = 4095;

    private readonly char[] _labelStorage;
    private readonly int _maximumLabelLength;
    private int _labelLength;
    private int _x;
    private int _y;
    private int _width;
    private int _height;
    private bool _selected;
    private bool _enabled = true;
    private bool _isFocused;
    private uint _rejectedInputCount;
    private GuideXosRadioGroup _group;
    private int _groupIndex = -1;
    private int _requestedGroupIndex = -1;

    public GuideXosRadioButton(
        int x,
        int y,
        int width,
        int height,
        string label,
        int maximumLabelLength = DefaultMaximumLabelLength)
    {
        if (maximumLabelLength < 1 ||
            maximumLabelLength > MaximumSupportedLabelLength)
        {
            maximumLabelLength = DefaultMaximumLabelLength;
        }

        _maximumLabelLength = maximumLabelLength;
        _labelStorage = new char[maximumLabelLength];
        _x = 0;
        _y = 0;
        _width = MinimumSupportedWidth;
        _height = MinimumSupportedHeight;
        TrySetBounds(x, y, width, height);
        SetLabel(label);
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int MaximumLabelLength => _maximumLabelLength;
    public string Label => new string(_labelStorage, 0, _labelLength);
    public bool Selected => _selected;
    public bool Enabled => _enabled;
    public bool IsFocused => _isFocused;
    public uint RejectedInputCount => _rejectedInputCount;
    public GuideXosRadioGroup Group => _group;
    public int GroupIndex => _groupIndex;
    public int RequestedGroupIndex => _requestedGroupIndex;

    public bool SetLabel(string label)
    {
        if (!IsValidLabel(label))
        {
            ++_rejectedInputCount;
            return false;
        }

        for (int index = 0; index < label.Length; index++)
        {
            _labelStorage[index] = label[index];
        }
        _labelLength = label.Length;
        return true;
    }

    public bool TrySetBounds(int x, int y, int width, int height)
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

    /// <summary>Requests selection through the group, or selects an ungrouped instance.</summary>
    public bool TrySelect()
    {
        if (!_enabled) return false;
        return _group == null ? SelectInternal() : _group.TrySelect(this);
    }

    public void SetEnabled(bool enabled)
    {
        _enabled = enabled;
        if (!enabled)
        {
            _isFocused = false;
            _group?.HandleMemberDisabled(this);
        }
    }

    public void Focus()
    {
        if (_enabled) _isFocused = true;
    }

    public void Blur()
    {
        _isFocused = false;
    }

    /// <summary>Restores the transient button state and clears this member's selection.</summary>
    public void Reset()
    {
        _group?.ClearSelectionFor(this);
        _selected = false;
        _enabled = true;
        _isFocused = false;
        _rejectedInputCount = 0u;
        _requestedGroupIndex = -1;
    }

    public bool ContainsPoint(int x, int y)
    {
        return x >= _x && y >= _y && x < _x + _width && y < _y + _height;
    }

    public GuideXosRadioButtonResult HandlePointerDown(int x, int y)
    {
        if (!ContainsPoint(x, y)) return GuideXosRadioButtonResult.Ignored;
        if (!_enabled) return GuideXosRadioButtonResult.Disabled;

        _isFocused = true;
        return TrySelect()
            ? GuideXosRadioButtonResult.Selected
            : GuideXosRadioButtonResult.Rejected;
    }

    public GuideXosRadioButtonResult HandleKey(GuideXosTextInputKey key)
    {
        _requestedGroupIndex = -1;
        if (!_enabled) return GuideXosRadioButtonResult.Disabled;
        if (!_isFocused) return GuideXosRadioButtonResult.Ignored;

        if (key == GuideXosTextInputKey.Left ||
            key == GuideXosTextInputKey.Up ||
            key == GuideXosTextInputKey.Right ||
            key == GuideXosTextInputKey.Down)
        {
            if (_group == null) return GuideXosRadioButtonResult.Ignored;
            bool reverse = key == GuideXosTextInputKey.Left ||
                key == GuideXosTextInputKey.Up;
            if (!_group.TryMove(this, reverse, out int targetIndex))
            {
                return GuideXosRadioButtonResult.Ignored;
            }
            _requestedGroupIndex = targetIndex;
            return GuideXosRadioButtonResult.Moved;
        }

        // Space is committed by KeyChar. KeyDown is deliberately ignored so
        // one physical press cannot select twice on the shared transport.
        return GuideXosRadioButtonResult.Ignored;
    }

    public GuideXosRadioButtonResult HandleCharacter(char character)
    {
        if (!_enabled) return GuideXosRadioButtonResult.Disabled;
        if (!_isFocused || character != ' ')
        {
            return GuideXosRadioButtonResult.Ignored;
        }
        return TrySelect()
            ? GuideXosRadioButtonResult.Selected
            : GuideXosRadioButtonResult.Rejected;
    }

    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;

        Span<byte> line = stackalloc byte[64];
        int position = 0;
        if (!_enabled) line[position++] = (byte)'x';
        else if (_isFocused) line[position++] = (byte)'>';
        line[position++] = (byte)'('; 
        line[position++] = _selected ? (byte)'o' : (byte)' ';
        line[position++] = (byte)')';
        line[position++] = (byte)' ';

        int availableLabelLength = Math.Max(0, (_width / 8) - position);
        availableLabelLength = Math.Min(_labelLength, availableLabelLength);
        for (int index = 0; index < availableLabelLength; index++)
        {
            line[position++] = (byte)_labelStorage[index];
        }
        return surface.TrySetText(_x, _y, line[..position]);
    }

    internal void AttachGroup(GuideXosRadioGroup group, int index)
    {
        _group = group;
        _groupIndex = index;
        _selected = false;
        _requestedGroupIndex = -1;
    }

    internal void DetachGroup()
    {
        _group = null;
        _groupIndex = -1;
        _selected = false;
        _requestedGroupIndex = -1;
    }

    internal void SetSelectedInternal(bool selected)
    {
        _selected = selected;
    }

    private bool SelectInternal()
    {
        _selected = true;
        return true;
    }

    private bool IsValidLabel(string label)
    {
        if (label == null || label.Length == 0 ||
            label.Length > _maximumLabelLength)
        {
            return false;
        }
        for (int index = 0; index < label.Length; index++)
        {
            if (label[index] < 0x20 || label[index] > 0x7E) return false;
        }
        return true;
    }
}
