using System;

namespace HostLogProof;

public enum GuideXosCheckBoxResult
{
    Ignored = 0,
    Focused = 1,
    Toggled = 2,
    Disabled = 3,
    Rejected = 4,
}

/// <summary>
/// A bounded managed two-state boolean choice. The application owns the
/// meaning of the value; this control owns only label, bounds, state, focus,
/// pointer-down, keyboard, and text rendering.
/// </summary>
public sealed class GuideXosCheckBox
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
    private bool _checked;
    private bool _enabled = true;
    private bool _isFocused;
    private uint _rejectedInputCount;

    public GuideXosCheckBox(
        int x,
        int y,
        int width,
        int height,
        string label,
        bool isChecked = false,
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
        _checked = isChecked;
        if (!TrySetBounds(x, y, width, height) || !SetLabel(label))
        {
            // Construction remains total and bounded for rejected input.
        }
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int MaximumLabelLength => _maximumLabelLength;
    public string Label => new string(_labelStorage, 0, _labelLength);
    public bool Checked => _checked;
    public bool Enabled => _enabled;
    public bool IsFocused => _isFocused;
    public uint RejectedInputCount => _rejectedInputCount;

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

    public void SetChecked(bool isChecked)
    {
        _checked = isChecked;
    }

    public GuideXosCheckBoxResult Toggle()
    {
        if (!_enabled) return GuideXosCheckBoxResult.Disabled;
        if (!_isFocused) return GuideXosCheckBoxResult.Ignored;
        _checked = !_checked;
        return GuideXosCheckBoxResult.Toggled;
    }

    public void SetEnabled(bool enabled)
    {
        _enabled = enabled;
        if (!enabled) _isFocused = false;
    }

    public void Focus()
    {
        if (_enabled) _isFocused = true;
    }

    public void Blur()
    {
        _isFocused = false;
    }

    /// <summary>Restores transient enabled/focus/rejection state.</summary>
    public void Reset()
    {
        _enabled = true;
        _isFocused = false;
        _rejectedInputCount = 0u;
    }

    public GuideXosCheckBoxResult HandlePointerDown(int x, int y)
    {
        if (x < _x || y < _y ||
            x >= _x + _width || y >= _y + _height)
        {
            return GuideXosCheckBoxResult.Ignored;
        }
        if (!_enabled) return GuideXosCheckBoxResult.Disabled;

        _isFocused = true;
        _checked = !_checked;
        return GuideXosCheckBoxResult.Toggled;
    }

    public GuideXosCheckBoxResult HandleKey(GuideXosTextInputKey key)
    {
        if (!_enabled) return GuideXosCheckBoxResult.Disabled;
        if (!_isFocused) return GuideXosCheckBoxResult.Ignored;
        // Space is committed only by KeyChar, matching GuideXosButton's
        // exactly-once policy when KeyDown and KeyChar share one press.
        return GuideXosCheckBoxResult.Ignored;
    }

    public GuideXosCheckBoxResult HandleCharacter(char character)
    {
        if (!_enabled) return GuideXosCheckBoxResult.Disabled;
        if (!_isFocused) return GuideXosCheckBoxResult.Ignored;
        if (character != ' ') return GuideXosCheckBoxResult.Ignored;
        _checked = !_checked;
        return GuideXosCheckBoxResult.Toggled;
    }

    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;

        Span<byte> line = stackalloc byte[64];
        line.Clear();
        int position = 0;
        if (!AppendByte(line, ref position, !_enabled
                ? (byte)'x' : _isFocused ? (byte)'>' : (byte)'['))
        {
            return GuideXosResult.InvalidArgument;
        }
        if (_enabled && !_isFocused && position > 0 &&
            line[0] == (byte)'[')
        {
            // The normal marker already opens the state box.
        }
        else if (!AppendByte(line, ref position, (byte)'['))
        {
            return GuideXosResult.InvalidArgument;
        }
        if (!AppendByte(line, ref position, _checked ? (byte)'x' : (byte)' ') ||
            !AppendByte(line, ref position, (byte)']') ||
            !AppendByte(line, ref position, (byte)' '))
        {
            return GuideXosResult.InvalidArgument;
        }

        int availableLabelLength = Math.Max(0, (_width / 8) - 5);
        availableLabelLength = Math.Min(_labelLength, availableLabelLength);
        for (int index = 0; index < availableLabelLength; index++)
        {
            if (!AppendByte(line, ref position, (byte)_labelStorage[index]))
            {
                return GuideXosResult.InvalidArgument;
            }
        }
        return surface.TrySetText(_x, _y, line[..position]);
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

    private static bool AppendByte(
        Span<byte> destination, ref int position, byte value)
    {
        if (position >= destination.Length) return false;
        destination[position++] = value;
        return true;
    }
}
