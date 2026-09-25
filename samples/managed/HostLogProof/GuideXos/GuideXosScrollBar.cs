using System;

namespace HostLogProof;

public enum GuideXosScrollBarResult
{
    Ignored = 0,
    Focused = 1,
    Changed = 2,
    Disabled = 3,
    Rejected = 4,
    DragStarted = 5,
    Dragged = 6,
    DragEnded = 7,
    Paged = 8,
    Stepped = 9,
    Scrolled = 10,
    Cancelled = 11,
}

/// <summary>
/// Bounded vertical scrollbar.  Maximum is the largest legal first-visible
/// position; PageSize is the visible extent used only for thumb geometry.
/// The control never owns a content viewport.  A caller supplies the binding
/// callback and synchronizes Value from its authoritative viewport state.
/// </summary>
public sealed class GuideXosScrollBar
{
    public const int DefaultWidth = 16;
    public const int DefaultHeight = 128;
    public const int MinimumThumbPixels = 8;
    public const int ArrowPixels = 12;
    public const int MaximumSupportedCoordinate = 4095;

    private readonly int _x;
    private readonly int _y;
    private readonly int _width;
    private readonly int _height;
    private int _minimum;
    private int _maximum;
    private int _value;
    private int _pageSize;
    private int _smallChange = 1;
    private int _largeChange = 1;
    private bool _isFocused;
    private bool _isVisible = true;
    private bool _isEnabled = true;
    private bool _isDragging;
    private int _dragOffset;
    private int _dragStartThumbOffset;
    private int _dragStartValue;
    private bool _notifying;

    public GuideXosScrollBar(
        int x,
        int y,
        int width = DefaultWidth,
        int height = DefaultHeight)
    {
        if (x < 0 || x > MaximumSupportedCoordinate) x = 0;
        if (y < 0 || y > MaximumSupportedCoordinate) y = 0;
        if (width < 1 || width > MaximumSupportedCoordinate - x)
        {
            width = DefaultWidth;
        }
        if (height < 1 || height > MaximumSupportedCoordinate - y)
        {
            height = DefaultHeight;
        }
        _x = x;
        _y = y;
        _width = width;
        _height = height;
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int Minimum
    {
        get => _minimum;
        set
        {
            int next = value;
            if (next > _maximum) _maximum = next;
            _minimum = next;
            SetValueInternal(_value, true);
        }
    }
    public int Maximum
    {
        get => _maximum;
        set
        {
            _maximum = value < _minimum ? _minimum : value;
            SetValueInternal(_value, true);
        }
    }
    public int Value
    {
        get => _value;
        set => SetValueInternal(value, true);
    }
    public int PageSize
    {
        get => _pageSize;
        set => _pageSize = value < 0 ? 0 : value;
    }
    public int SmallChange
    {
        get => _smallChange;
        set => _smallChange = value < 1 ? 1 : value;
    }
    public int LargeChange
    {
        get => _largeChange;
        set => _largeChange = value < 1 ? 1 : value;
    }
    public bool Enabled => _isEnabled;
    public bool Visible => _isVisible;
    public bool EffectiveVisible => _isVisible;
    public bool IsFocused => _isFocused;
    public bool IsDragging => _isDragging;
    public int DragOffset => _dragOffset;
    public bool IsScrollable => _maximum > _minimum;
    public event Action<int> Changed;

    public int TrackTop => _y + TrackArrowPixels;
    public int TrackLength => Math.Max(0, _height - TrackArrowPixels * 2);
    public int ThumbHeight => CalculateThumbHeight();
    public int ThumbTop => CalculateThumbTop();
    public int ThumbBottom => ThumbTop + ThumbHeight;

    private int TrackArrowPixels => _height >= ArrowPixels * 2 + 1
        ? ArrowPixels : 0;

    public void Focus()
    {
        if (_isVisible && _isEnabled) _isFocused = true;
    }

    public void Blur()
    {
        _isFocused = false;
    }

    public void SetVisible(bool visible)
    {
        _isVisible = visible;
        if (!visible)
        {
            _isFocused = false;
            CancelDrag();
        }
    }

    public void SetEnabled(bool enabled)
    {
        _isEnabled = enabled;
        if (!enabled)
        {
            _isFocused = false;
            CancelDrag();
        }
    }

    public void ResetTransientState()
    {
        _isFocused = false;
        CancelDrag();
    }

    public void CancelDrag()
    {
        _isDragging = false;
        _dragOffset = 0;
        _dragStartThumbOffset = 0;
        _dragStartValue = _value;
    }

    public GuideXosScrollBarResult HandleKey(GuideXosTextInputKey key)
    {
        if (!_isVisible) return GuideXosScrollBarResult.Ignored;
        if (!_isEnabled) return GuideXosScrollBarResult.Disabled;
        if (!_isFocused || _isDragging) return GuideXosScrollBarResult.Ignored;

        switch (key)
        {
            case GuideXosTextInputKey.Up:
                return Step(-_smallChange);
            case GuideXosTextInputKey.Down:
                return Step(_smallChange);
            case GuideXosTextInputKey.Home:
                return SetPosition(_minimum);
            case GuideXosTextInputKey.End:
                return SetPosition(_maximum);
            default:
                return GuideXosScrollBarResult.Ignored;
        }
    }

    public GuideXosScrollBarResult HandleWheel(int wheelDelta)
    {
        if (!_isVisible) return GuideXosScrollBarResult.Ignored;
        if (!_isEnabled) return GuideXosScrollBarResult.Disabled;
        if (_isDragging || wheelDelta == 0) return GuideXosScrollBarResult.Ignored;
        int bounded = wheelDelta;
        if (bounded > 8) bounded = 8;
        if (bounded < -8) bounded = -8;
        return Step(-bounded * _smallChange * 3);
    }

    /// <summary>Handles one primary-button press.  Secondary clicks never act.</summary>
    public GuideXosScrollBarResult HandlePointerDown(
        int x,
        int y,
        GuideXosPointerButton button = GuideXosPointerButton.Primary)
    {
        if (button != GuideXosPointerButton.Primary)
        {
            return GuideXosScrollBarResult.Ignored;
        }
        if (!_isVisible) return GuideXosScrollBarResult.Ignored;
        if (!_isEnabled) return GuideXosScrollBarResult.Disabled;
        if (!ContainsPoint(x, y)) return GuideXosScrollBarResult.Ignored;

        Focus();
        int thumbTop = ThumbTop;
        int thumbBottom = thumbTop + ThumbHeight;
        if (y >= thumbTop && y < thumbBottom)
        {
            _isDragging = true;
            _dragOffset = y - thumbTop;
            _dragStartThumbOffset = thumbTop - TrackTop;
            _dragStartValue = _value;
            return GuideXosScrollBarResult.DragStarted;
        }

        if (TrackArrowPixels != 0 && y < TrackTop)
        {
            return Step(-_smallChange);
        }
        if (TrackArrowPixels != 0 && y >= TrackTop + TrackLength)
        {
            return Step(_smallChange);
        }

        return y < thumbTop
            ? Page(-_largeChange)
            : Page(_largeChange);
    }

    /// <summary>
    /// Updates the active thumb using the press offset.  The host calls this
    /// through its single drag owner even when x/y leave the scrollbar bounds.
    /// </summary>
    public GuideXosScrollBarResult HandlePointerMove(int x, int y)
    {
        if (!_isDragging) return GuideXosScrollBarResult.Ignored;
        if (!_isVisible || !_isEnabled)
        {
            CancelDrag();
            return GuideXosScrollBarResult.Cancelled;
        }

        int available = Math.Max(0, TrackLength - ThumbHeight);
        int desired = y - _dragOffset - TrackTop;
        if (desired < 0) desired = 0;
        if (desired > available) desired = available;
        int next = desired == _dragStartThumbOffset
            ? _dragStartValue
            : ValueForThumbOffset(desired, available);
        bool changed = SetValueInternal(next, true);
        return changed
            ? GuideXosScrollBarResult.Dragged
            : GuideXosScrollBarResult.Ignored;
    }

    public GuideXosScrollBarResult HandlePointerUp(
        int x,
        int y,
        GuideXosPointerButton button = GuideXosPointerButton.Primary)
    {
        if (button != GuideXosPointerButton.Primary || !_isDragging)
        {
            return GuideXosScrollBarResult.Ignored;
        }
        CancelDrag();
        return GuideXosScrollBarResult.DragEnded;
    }

    public bool ContainsPoint(int x, int y)
    {
        return x >= _x && x < _x + _width &&
            y >= _y && y < _y + _height;
    }

    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null || !_isVisible) return GuideXosResult.InvalidArgument;
        uint track = !_isEnabled ? 0x00505050u : 0x00383848u;
        uint thumb = !_isEnabled ? 0x00707070u :
            _isFocused ? 0x00D0D0E0u : 0x009090A8u;
        if (TrackLength > 0 && surface.TryFillRect(
                _x, TrackTop, _width, TrackLength, track) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }
        if (ThumbHeight > 0 && surface.TryFillRect(
                _x, ThumbTop, _width, ThumbHeight, thumb) != GuideXosResult.Success)
        {
            return GuideXosResult.InvalidArgument;
        }
        return GuideXosResult.Success;
    }

    private GuideXosScrollBarResult Page(int delta)
    {
        bool changed = SetValueInternal(
            Clamp((long)_value + delta), true);
        return changed ? GuideXosScrollBarResult.Paged
            : GuideXosScrollBarResult.Ignored;
    }

    private GuideXosScrollBarResult Step(int delta)
    {
        bool changed = SetValueInternal(
            Clamp((long)_value + delta), true);
        return changed ? GuideXosScrollBarResult.Stepped
            : GuideXosScrollBarResult.Ignored;
    }

    private GuideXosScrollBarResult SetPosition(int value)
    {
        bool changed = SetValueInternal(value, true);
        return changed ? GuideXosScrollBarResult.Changed
            : GuideXosScrollBarResult.Ignored;
    }

    private bool SetValueInternal(long requested, bool notify)
    {
        int next = Clamp(requested);
        if (_value == next) return false;
        _value = next;
        if (notify && !_notifying && Changed != null)
        {
            _notifying = true;
            Changed(_value);
            _notifying = false;
        }
        return true;
    }

    private int Clamp(long value)
    {
        if (value < _minimum) return _minimum;
        if (value > _maximum) return _maximum;
        return (int)value;
    }

    private int CalculateThumbHeight()
    {
        int track = TrackLength;
        if (track <= 0) return 0;
        if (_maximum <= _minimum) return track;
        long page = _pageSize > 0 ? _pageSize : 1;
        long range = (long)_maximum - _minimum;
        long total = range + page;
        long proportion = ((long)track * page) / total;
        if (proportion < MinimumThumbPixels) proportion = MinimumThumbPixels;
        if (proportion > track) proportion = track;
        return (int)proportion;
    }

    private int CalculateThumbTop()
    {
        int track = TrackLength;
        int thumb = ThumbHeight;
        if (track <= 0) return _y;
        int available = Math.Max(0, track - thumb);
        long range = (long)_maximum - _minimum;
        if (available == 0 || range <= 0) return TrackTop;
        long offset = ((long)(_value - _minimum) * available) / range;
        return TrackTop + (int)offset;
    }

    private int ValueForThumbOffset(int offset, int available)
    {
        long range = (long)_maximum - _minimum;
        if (available <= 0 || range <= 0) return _minimum;
        long value = _minimum +
            ((long)offset * range + available / 2) / available;
        return Clamp(value);
    }
}
