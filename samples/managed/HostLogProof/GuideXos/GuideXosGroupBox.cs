using System;

namespace HostLogProof;

/// <summary>
/// A bounded text-backed structural boundary. The box owns only its geometry,
/// optional caption, and visibility; it does not own, focus, or route any
/// child control.
/// </summary>
public sealed class GuideXosGroupBox
{
    public const int CharacterWidth = 8;
    public const int TextRowHeight = 18;
    public const int MinimumSupportedCoordinate = 0;
    public const int MaximumSupportedCoordinate = 4095;
    public const int MinimumSupportedRenderWidth = 8;
    public const int MaximumSupportedRenderWidth = 63;
    public const int MinimumSupportedWidth =
        MinimumSupportedRenderWidth * CharacterWidth;
    public const int MaximumSupportedWidth =
        MaximumSupportedRenderWidth * CharacterWidth;
    public const int MinimumSupportedHeight = TextRowHeight * 3;
    public const int MaximumSupportedHeight = TextRowHeight * 16;
    public const int DefaultMaximumCaptionLength = 48;
    public const int MaximumSupportedCaptionLength = 48;
    private const int CaptionLeadingColumns = 3;
    private const int CaptionTrailingColumns = 2;

    private readonly char[] _captionStorage;
    private int _captionLength;
    private int _x;
    private int _y;
    private int _width;
    private int _height;
    private bool _visible = true;
    private uint _rejectedInputCount;

    public GuideXosGroupBox(
        int x,
        int y,
        int width,
        int height,
        string caption = "")
    {
        _captionStorage = new char[DefaultMaximumCaptionLength];
        _x = MinimumSupportedCoordinate;
        _y = MinimumSupportedCoordinate;
        _width = MinimumSupportedWidth;
        _height = MinimumSupportedHeight;
        TrySetBounds(x, y, width, height);
        TrySetCaption(caption ?? string.Empty);
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => _height;
    public int RenderWidth => _width / CharacterWidth;
    public int RenderRowCount => _height / TextRowHeight;
    public int MaximumCaptionLength => _captionStorage.Length;
    public int CaptionLength => _captionLength;
    public int RenderCaptionLength => Math.Min(
        _captionLength, GetCaptionRenderCapacity());
    public string Caption => new string(_captionStorage, 0, _captionLength);
    public bool Visible => _visible;
    public bool Focusable => false;
    public uint RejectedInputCount => _rejectedInputCount;

    /// <summary>
    /// Replaces the bounded printable-ASCII caption atomically. Empty is valid;
    /// null, control characters, and over-capacity text are rejected.
    /// </summary>
    public bool TrySetCaption(string caption)
    {
        return caption != null && TrySetCaption(caption.AsSpan());
    }

    /// <summary>Bounded span overload for callers that already own the value.</summary>
    public bool TrySetCaption(ReadOnlySpan<char> caption)
    {
        if (!IsValidCaption(caption))
        {
            ++_rejectedInputCount;
            return false;
        }

        for (int index = 0; index < caption.Length; index++)
        {
            _captionStorage[index] = caption[index];
        }
        _captionLength = caption.Length;
        return true;
    }

    public bool ClearCaption()
    {
        return TrySetCaption(ReadOnlySpan<char>.Empty);
    }

    public bool TrySetBounds(int x, int y, int width, int height)
    {
        if (x < MinimumSupportedCoordinate ||
            y < MinimumSupportedCoordinate ||
            width < MinimumSupportedWidth ||
            width > MaximumSupportedWidth ||
            width % CharacterWidth != 0 ||
            height < MinimumSupportedHeight ||
            height > MaximumSupportedHeight ||
            height % TextRowHeight != 0 ||
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
    }

    /// <summary>
    /// Uses a half-open rectangle: X &lt;= px &lt; X+Width and
    /// Y &lt;= py &lt; Y+Height. It is geometry only and does not route input.
    /// </summary>
    public bool ContainsPoint(int x, int y)
    {
        return x >= _x && x < _x + _width &&
            y >= _y && y < _y + _height;
    }

    /// <summary>
    /// Resolves a pixel coordinate relative to this box without changing state.
    /// Relative coordinates use the same half-open bounds as ContainsPoint.
    /// </summary>
    public bool TryResolvePoint(
        int relativeX,
        int relativeY,
        out int absoluteX,
        out int absoluteY)
    {
        absoluteX = 0;
        absoluteY = 0;
        if (relativeX < 0 || relativeY < 0 ||
            relativeX >= _width || relativeY >= _height ||
            relativeX > MaximumSupportedCoordinate - _x ||
            relativeY > MaximumSupportedCoordinate - _y)
        {
            ++_rejectedInputCount;
            return false;
        }

        absoluteX = _x + relativeX;
        absoluteY = _y + relativeY;
        return true;
    }

    /// <summary>
    /// Restores visibility and diagnostics while retaining geometry and caption.
    /// </summary>
    public void Reset()
    {
        _visible = true;
        _rejectedInputCount = 0u;
    }

    /// <summary>
    /// Renders a complete bounded text frame. The first and last text rows are
    /// horizontal borders; interior rows contain vertical sides and spaces.
    /// Caption clipping is render-only and never changes stored caption text.
    /// </summary>
    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!_visible) return GuideXosResult.Success;

        Span<byte> line = stackalloc byte[MaximumSupportedRenderWidth + 1];
        int rowCount = RenderRowCount;
        for (int row = 0; row < rowCount; row++)
        {
            int renderLength = RenderWidth;
            if (row == 0)
            {
                for (int index = 0; index < renderLength; index++)
                {
                    line[index] = (byte)'-';
                }
                line[0] = (byte)'+';
                line[renderLength - 1] = (byte)'+';
                RenderCaption(line, renderLength);
            }
            else if (row == rowCount - 1)
            {
                for (int index = 0; index < renderLength; index++)
                {
                    line[index] = (byte)'-';
                }
                line[0] = (byte)'+';
                line[renderLength - 1] = (byte)'+';
            }
            else
            {
                for (int index = 0; index < renderLength; index++)
                {
                    line[index] = (byte)' ';
                }
                line[0] = (byte)'|';
                line[renderLength - 1] = (byte)'|';
            }

            GuideXosResult result = surface.TrySetText(
                _x, _y + row * TextRowHeight, line[..renderLength]);
            if (result != GuideXosResult.Success) return result;
        }
        return GuideXosResult.Success;
    }

    private int GetCaptionRenderCapacity()
    {
        return Math.Max(0, RenderWidth -
            CaptionLeadingColumns - CaptionTrailingColumns - 1);
    }

    private void RenderCaption(Span<byte> line, int renderLength)
    {
        if (_captionLength == 0) return;

        int renderCaptionLength = RenderCaptionLength;
        int captionStart = CaptionLeadingColumns;
        for (int index = 0; index < renderCaptionLength; index++)
        {
            line[captionStart + index] = (byte)_captionStorage[index];
        }
        int captionEnd = captionStart + renderCaptionLength;
        if (captionEnd < renderLength - 1)
        {
            line[captionEnd] = (byte)' ';
        }
    }

    private bool IsValidCaption(ReadOnlySpan<char> caption)
    {
        if (caption.Length > _captionStorage.Length) return false;
        for (int index = 0; index < caption.Length; index++)
        {
            if (caption[index] < 0x20 || caption[index] > 0x7E) return false;
        }
        return true;
    }
}
