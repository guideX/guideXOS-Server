using System;

namespace HostLogProof;

public enum GuideXosButtonResult
{
    Ignored = 0,
    Focused = 1,
    Activated = 2,
    Disabled = 3,
    Rejected = 4,
}

/// <summary>
/// A bounded managed command button. The application owns the command that
/// follows activation; this control owns only label, bounds, focus, and state.
/// Pointer-down is the complete pointer gesture because pointer-up is not part
/// of the managed input contract.
/// </summary>
public sealed class GuideXosButton
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
    private bool _enabled = true;
    private bool _isFocused;
    private bool _visible = true;
    private bool _panelVisible = true;
    private GuideXosPanel _panelOwner;
    private GuideXosScrollView _scrollViewOwner;
    private uint _rejectedInputCount;

    public GuideXosButton(
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
        if (!TrySetBounds(x, y, width, height) || !SetLabel(label))
        {
            // Construction stays total and bounded even for rejected input.
            // The rejected count records which configuration was not applied.
        }
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int MaximumLabelLength => _maximumLabelLength;
    public string Label => new string(_labelStorage, 0, _labelLength);
    public bool Enabled => _enabled;
    public bool IsFocused => _isFocused;
    public bool Visible => _visible;
    public bool EffectiveVisible => _visible && _panelVisible;
    public GuideXosPanel ParentPanel => _panelOwner;
    public GuideXosScrollView ParentScrollView => _scrollViewOwner;
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
        if (_panelOwner != null || _scrollViewOwner != null)
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

    internal bool TrySetScrollViewBounds(int x, int y, int width, int height)
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

    /// <summary>Restores transient state while retaining label and bounds.</summary>
    public void Reset()
    {
        _enabled = true;
        _isFocused = false;
        _visible = true;
        if (_panelOwner == null) _panelVisible = true;
        _rejectedInputCount = 0u;
    }

    public GuideXosButtonResult HandlePointerDown(int x, int y)
    {
        if (!EffectiveVisible) return GuideXosButtonResult.Ignored;
        if (x < _x || y < _y ||
            x >= _x + _width || y >= _y + _height)
        {
            return GuideXosButtonResult.Ignored;
        }
        if (!_enabled) return GuideXosButtonResult.Disabled;

        _isFocused = true;
        return GuideXosButtonResult.Activated;
    }

    public GuideXosButtonResult HandleKey(GuideXosTextInputKey key)
    {
        if (!EffectiveVisible) return GuideXosButtonResult.Ignored;
        if (!_enabled) return GuideXosButtonResult.Disabled;
        if (!_isFocused) return GuideXosButtonResult.Ignored;
        return key == GuideXosTextInputKey.Enter
            ? GuideXosButtonResult.Activated
            : GuideXosButtonResult.Ignored;
    }

    public GuideXosButtonResult HandleCharacter(char character)
    {
        if (!EffectiveVisible) return GuideXosButtonResult.Ignored;
        if (!_enabled) return GuideXosButtonResult.Disabled;
        if (!_isFocused) return GuideXosButtonResult.Ignored;
        // Space arrives as KeyChar after KeyDown in the desktop route. It is
        // intentionally handled here only, so one physical Space activates once.
        return character == ' '
            ? GuideXosButtonResult.Activated
            : GuideXosButtonResult.Ignored;
    }

    /// <summary>
    /// Renders text only. The managed frame primitive resets prior rectangles,
    /// so the application supplies the shared background and this control does
    /// not add a fill rectangle of its own.
    /// </summary>
    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!EffectiveVisible) return GuideXosResult.Success;

        Span<byte> line = stackalloc byte[64];
        line.Clear();
        int position = 0;
        byte lead = !_enabled
            ? (byte)'x'
            : _isFocused ? (byte)'>' : (byte)'[';
        if (!AppendByte(line, ref position, lead) ||
            (lead != (byte)'[' && !AppendByte(line, ref position, (byte)'[')) ||
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
        if (!AppendByte(line, ref position, (byte)' ') ||
            !AppendByte(line, ref position, (byte)']'))
        {
            return GuideXosResult.InvalidArgument;
        }
        return surface.TrySetText(_x, _y, line[..position]);
    }

    internal bool TryAttachToPanel(GuideXosPanel panel)
    {
        if (panel == null || _panelOwner != null || _scrollViewOwner != null) return false;
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
        _scrollViewOwner = null;
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
