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
    private bool _visible = true;
    private bool _panelVisible = true;
    private bool _dispatchingChanged;
    private GuideXosPanel _panelOwner;
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
    public string Text => Label;
    public bool Checked
    {
        get => _checked;
        set => SetChecked(value);
    }
    public bool Enabled
    {
        get => _enabled;
        set => SetEnabled(value);
    }
    public bool IsFocused => _isFocused;
    public bool Visible
    {
        get => _visible;
        set => SetVisible(value);
    }
    public bool EffectiveVisible => _visible && _panelVisible;
    public GuideXosPanel ParentPanel => _panelOwner;
    public uint RejectedInputCount => _rejectedInputCount;

    /// <summary>
    /// One bounded callback invoked after a real value transition. Reentrant
    /// assignments update the value but do not recursively invoke the same
    /// callback, keeping callback-driven state changes bounded.
    /// </summary>
    public Action<bool> Changed { get; set; }

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

    public bool TrySetText(string text)
    {
        return SetLabel(text);
    }

    public bool TrySetBounds(int x, int y, int width, int height)
    {
        if (_panelOwner != null)
        {
            ++_rejectedInputCount;
            return false;
        }
        return TrySetBoundsCore(x, y, width, height);
    }

    internal bool TrySetPanelBounds(int x, int y, int width, int height)
    {
        return TrySetBoundsCore(x, y, width, height);
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

    public void SetVisible(bool visible)
    {
        _visible = visible;
        if (!visible) _isFocused = false;
    }

    public void SetChecked(bool isChecked)
    {
        if (_checked == isChecked) return;
        _checked = isChecked;
        Action<bool> changed = Changed;
        if (changed == null || _dispatchingChanged) return;

        _dispatchingChanged = true;
        try
        {
            changed(_checked);
        }
        finally
        {
            _dispatchingChanged = false;
        }
    }

    public GuideXosCheckBoxResult Toggle()
    {
        if (!_enabled) return GuideXosCheckBoxResult.Disabled;
        if (!_isFocused) return GuideXosCheckBoxResult.Ignored;
        SetChecked(!_checked);
        return GuideXosCheckBoxResult.Toggled;
    }

    public void SetEnabled(bool enabled)
    {
        _enabled = enabled;
        if (!enabled) _isFocused = false;
    }

    public void Focus()
    {
        if (_enabled && EffectiveVisible) _isFocused = true;
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
        _visible = true;
        if (_panelOwner == null) _panelVisible = true;
        _rejectedInputCount = 0u;
    }

    public GuideXosCheckBoxResult HandlePointerDown(int x, int y)
    {
        if (!EffectiveVisible) return GuideXosCheckBoxResult.Ignored;
        if (x < _x || y < _y ||
            x >= _x + _width || y >= _y + _height)
        {
            return GuideXosCheckBoxResult.Ignored;
        }
        if (!_enabled) return GuideXosCheckBoxResult.Disabled;

        _isFocused = true;
        SetChecked(!_checked);
        return GuideXosCheckBoxResult.Toggled;
    }

    public GuideXosCheckBoxResult HandleKey(GuideXosTextInputKey key)
    {
        if (!EffectiveVisible) return GuideXosCheckBoxResult.Ignored;
        if (!_enabled) return GuideXosCheckBoxResult.Disabled;
        if (!_isFocused) return GuideXosCheckBoxResult.Ignored;
        // Space is committed only by KeyChar, matching GuideXosButton's
        // exactly-once policy when KeyDown and KeyChar share one press.
        return GuideXosCheckBoxResult.Ignored;
    }

    public GuideXosCheckBoxResult HandleCharacter(char character)
    {
        if (!EffectiveVisible) return GuideXosCheckBoxResult.Ignored;
        if (!_enabled) return GuideXosCheckBoxResult.Disabled;
        if (!_isFocused) return GuideXosCheckBoxResult.Ignored;
        if (character != ' ') return GuideXosCheckBoxResult.Ignored;
        SetChecked(!_checked);
        return GuideXosCheckBoxResult.Toggled;
    }

    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!EffectiveVisible) return GuideXosResult.Success;

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

    internal bool TryAttachToPanel(GuideXosPanel panel)
    {
        if (panel == null || _panelOwner != null) return false;
        _panelOwner = panel;
        _panelVisible = panel.Visible;
        if (!_panelVisible) _isFocused = false;
        return true;
    }

    internal void SetPanelVisible(bool visible)
    {
        _panelVisible = visible;
        if (!visible) _isFocused = false;
    }

    internal void DetachFromPanel()
    {
        _panelOwner = null;
        _panelVisible = true;
        _isFocused = false;
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
