using System;

namespace HostLogProof;

/// <summary>
/// Allocation-free bounded state for a logical vertical viewport.  The
/// abstraction deliberately knows nothing about controls, pixels, input, or
/// rendering; its units are owned by the caller (lines, rows, or items).
/// </summary>
public sealed class GuideXosVerticalViewport
{
    private int _contentExtent;
    private int _visibleExtent;
    private int _offset;
    private int _smallChange = 1;
    private int _configuredLargeChange;
    private bool _notifying;

    public GuideXosVerticalViewport(
        int contentExtent = 0,
        int visibleExtent = 0)
    {
        _contentExtent = NormalizeExtent(contentExtent);
        _visibleExtent = NormalizeExtent(visibleExtent);
    }

    public int ContentExtent
    {
        get => _contentExtent;
        set => SetContentExtent(value);
    }

    public int VisibleExtent
    {
        get => _visibleExtent;
        set => SetVisibleExtent(value);
    }

    public int Offset
    {
        get => _offset;
        set => SetOffset(value);
    }

    public int MaximumOffset
    {
        get
        {
            long maximum = (long)_contentExtent - _visibleExtent;
            return maximum > 0 ? (int)maximum : 0;
        }
    }

    public int SmallChange
    {
        get => _smallChange;
        set
        {
            int next = value < 1 ? 1 : value;
            if (_smallChange == next) return;
            _smallChange = next;
            NotifyStateChanged(false);
        }
    }

    public int LargeChange
    {
        get => _configuredLargeChange > 0
            ? _configuredLargeChange
            : Math.Max(1, _visibleExtent - 1);
        set
        {
            int next = value < 1 ? 1 : value;
            if (_configuredLargeChange == next) return;
            _configuredLargeChange = next;
            NotifyStateChanged(false);
        }
    }

    /// <summary>
    /// Fires once for an effective offset change.  Same-value assignments and
    /// changes that only alter the range do not invoke this callback.
    /// </summary>
    public event Action<int> Changed;

    // ScrollBar uses this narrow internal hook to refresh range/page geometry
    // when content or visible extent changes without changing Offset.  It is
    // not a general property-change framework.
    internal event Action StateChanged;

    public bool SetContentExtent(int value)
    {
        int next = NormalizeExtent(value);
        if (_contentExtent == next) return false;
        _contentExtent = next;
        bool offsetChanged = SetOffsetCore(_offset);
        NotifyStateChanged(offsetChanged);
        return true;
    }

    public bool SetVisibleExtent(int value)
    {
        int next = NormalizeExtent(value);
        if (_visibleExtent == next) return false;
        _visibleExtent = next;
        bool offsetChanged = SetOffsetCore(_offset);
        NotifyStateChanged(offsetChanged);
        return true;
    }

    public bool SetOffset(int value)
    {
        return SetOffsetLong(value);
    }

    private bool SetOffsetLong(long value)
    {
        bool changed = SetOffsetCore(value);
        if (changed) NotifyStateChanged(true);
        return changed;
    }

    /// <summary>Moves by delta logical small units and clamps at both ends.</summary>
    public bool ScrollSmall(int delta)
    {
        if (delta == 0) return false;
        long movement = (long)delta * _smallChange;
        return SetOffsetLong(AddSaturating(_offset, movement));
    }

    /// <summary>Moves by delta logical page units and clamps at both ends.</summary>
    public bool ScrollLarge(int delta)
    {
        if (delta == 0) return false;
        long movement = (long)delta * LargeChange;
        return SetOffsetLong(AddSaturating(_offset, movement));
    }

    public bool ScrollPageForward() => ScrollLarge(1);

    public bool ScrollPageBackward() => ScrollLarge(-1);

    /// <summary>
    /// Adjusts the viewport just enough to expose one logical index.  A zero
    /// visible extent remains well-defined and exposes the requested index.
    /// </summary>
    public bool EnsureVisible(int index)
    {
        if (_contentExtent == 0) return SetOffset(0);
        if (index < 0) index = 0;
        if (index >= _contentExtent) index = _contentExtent - 1;

        int next = _offset;
        if (_visibleExtent == 0)
        {
            next = index;
        }
        else if (index < _offset)
        {
            next = index;
        }
        else if ((long)index >= (long)_offset + _visibleExtent)
        {
            next = index - _visibleExtent + 1;
        }
        return SetOffset(next);
    }

    private bool SetOffsetCore(long requested)
    {
        int next;
        if (requested <= 0)
        {
            next = 0;
        }
        else if (requested >= MaximumOffset)
        {
            next = MaximumOffset;
        }
        else
        {
            next = (int)requested;
        }

        if (_offset == next) return false;
        _offset = next;
        return true;
    }

    private void NotifyStateChanged(bool offsetChanged)
    {
        if (_notifying) return;
        _notifying = true;
        try
        {
            if (offsetChanged) Changed?.Invoke(_offset);
            StateChanged?.Invoke();
        }
        finally
        {
            _notifying = false;
        }
    }

    private static int NormalizeExtent(int value) => value < 0 ? 0 : value;

    private static long AddSaturating(int value, long movement)
    {
        long result = (long)value + movement;
        if (result < 0) return 0;
        if (result > int.MaxValue) return int.MaxValue;
        return result;
    }
}
