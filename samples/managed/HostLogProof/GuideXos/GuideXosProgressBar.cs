using System;

namespace HostLogProof;

/// <summary>
/// A bounded, determinate, presentation-only progress bar. The range and
/// value are non-negative bounded integers; rejected changes preserve state.
/// It has no focus, input, host registration, callbacks, or application
/// operations.
/// </summary>
public sealed class GuideXosProgressBar
{
    public const int DefaultMinimum = 0;
    public const int DefaultMaximum = 100;
    public const int MaximumSupportedValue = 65535;
    public const int CharacterWidth = 8;
    public const int TextRowHeight = 18;
    public const int MinimumSupportedCoordinate = 0;
    public const int MaximumSupportedCoordinate = 4095;
    public const int MinimumFillCellCount = 1;
    public const int MaximumFillCellCount = 48;
    public const int RenderingOverheadColumns = 7;
    public const int MinimumSupportedWidth =
        (MinimumFillCellCount + RenderingOverheadColumns) * CharacterWidth;
    public const int MaximumSupportedWidth =
        (MaximumFillCellCount + RenderingOverheadColumns) * CharacterWidth;

    private int _minimum = DefaultMinimum;
    private int _maximum = DefaultMaximum;
    private int _value = DefaultMinimum;
    private int _x;
    private int _y;
    private int _width = MinimumSupportedWidth;
    private bool _visible = true;
    private uint _rejectedInputCount;

    public GuideXosProgressBar(
        int x,
        int y,
        int width,
        int minimum = DefaultMinimum,
        int maximum = DefaultMaximum,
        int value = DefaultMinimum)
    {
        TrySetBounds(x, y, width);
        if (IsValidRange(minimum, maximum) &&
            value >= minimum && value <= maximum)
        {
            _minimum = minimum;
            _maximum = maximum;
            _value = value;
        }
        else
        {
            ++_rejectedInputCount;
        }
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => TextRowHeight;
    public int RenderWidth => _width / CharacterWidth;
    public int FillCellCount => RenderWidth - RenderingOverheadColumns;
    public int Minimum => _minimum;
    public int Maximum => _maximum;
    public int Value => _value;
    public bool Visible => _visible;
    public uint RejectedInputCount => _rejectedInputCount;

    /// <summary>Returns floor((Value-Minimum)*100/(Maximum-Minimum)).</summary>
    public int Percentage => ComputePercentage(_value, _minimum, _maximum);

    /// <summary>Returns floor((Value-Minimum)*cells/(Maximum-Minimum)).</summary>
    public int FilledCells => ComputeFilledCells(
        _value, _minimum, _maximum, FillCellCount);

    /// <summary>
    /// Replaces the inclusive range only when it is valid and still contains
    /// the current value. Invalid ranges are rejected without mutation.
    /// </summary>
    public bool TrySetRange(int minimum, int maximum)
    {
        if (!IsValidRange(minimum, maximum) ||
            _value < minimum || _value > maximum)
        {
            ++_rejectedInputCount;
            return false;
        }

        _minimum = minimum;
        _maximum = maximum;
        return true;
    }

    /// <summary>Sets the value only when it is inside the current range.</summary>
    public bool TrySetValue(int value)
    {
        if (value < _minimum || value > _maximum ||
            value < 0 || value > MaximumSupportedValue)
        {
            ++_rejectedInputCount;
            return false;
        }

        _value = value;
        return true;
    }

    public void SetVisible(bool visible)
    {
        _visible = visible;
    }

    public bool SetWidth(int width)
    {
        return TrySetBounds(_x, _y, width);
    }

    /// <summary>
    /// Updates pixel-aligned text geometry atomically. Coordinates and width
    /// are bounded by the managed surface contract and fixed fill-cell limit.
    /// </summary>
    public bool TrySetBounds(int x, int y, int width)
    {
        if (x < MinimumSupportedCoordinate ||
            y < MinimumSupportedCoordinate ||
            width < MinimumSupportedWidth ||
            width > MaximumSupportedWidth ||
            width % CharacterWidth != 0 ||
            x > MaximumSupportedCoordinate - width ||
            y > MaximumSupportedCoordinate - TextRowHeight)
        {
            ++_rejectedInputCount;
            return false;
        }

        _x = x;
        _y = y;
        _width = width;
        return true;
    }

    /// <summary>Restores visibility and diagnostics while retaining progress.</summary>
    public void Reset()
    {
        _visible = true;
        _rejectedInputCount = 0u;
    }

    /// <summary>
    /// Renders [filled-empty] percentage text using one bounded stack buffer.
    /// Hidden bars issue no text call. Integer arithmetic uses long
    /// intermediates, and floor/truncation is the explicit rounding policy.
    /// </summary>
    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!_visible) return GuideXosResult.Success;

        Span<byte> line = stackalloc byte[64];
        int position = 0;
        line[position++] = (byte)'[';
        int filled = FilledCells;
        for (int index = 0; index < FillCellCount; index++)
        {
            line[position++] = index < filled ? (byte)'#' : (byte)'-';
        }
        line[position++] = (byte)']';
        line[position++] = (byte)' ';
        AppendUnsigned(line, ref position, Percentage);
        line[position++] = (byte)'%';
        return surface.TrySetText(_x, _y, line[..position]);
    }

    private static bool IsValidRange(int minimum, int maximum)
    {
        return minimum >= 0 && maximum >= 0 &&
            minimum <= MaximumSupportedValue &&
            maximum <= MaximumSupportedValue && maximum > minimum;
    }

    private static int ComputePercentage(int value, int minimum, int maximum)
    {
        long normalized = value - (long)minimum;
        long span = maximum - (long)minimum;
        return (int)(normalized * 100L / span);
    }

    private static int ComputeFilledCells(
        int value, int minimum, int maximum, int cellCount)
    {
        long normalized = value - (long)minimum;
        long span = maximum - (long)minimum;
        return (int)(normalized * cellCount / span);
    }

    private static void AppendUnsigned(Span<byte> destination, ref int position,
        int value)
    {
        if (value >= 100)
        {
            destination[position++] = (byte)('0' + value / 100);
            value %= 100;
            destination[position++] = (byte)('0' + value / 10);
            destination[position++] = (byte)('0' + value % 10);
        }
        else if (value >= 10)
        {
            destination[position++] = (byte)('0' + value / 10);
            destination[position++] = (byte)('0' + value % 10);
        }
        else
        {
            destination[position++] = (byte)('0' + value);
        }
    }
}
