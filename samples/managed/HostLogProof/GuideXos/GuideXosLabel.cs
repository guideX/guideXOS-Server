using System;

namespace HostLogProof;

/// <summary>
/// Bounded, presentation-only single-line text. The application owns the
/// meaning of the text; the label owns only its stored value, bounds, and
/// visibility. It deliberately has no focus or input surface.
/// </summary>
public sealed class GuideXosLabel
{
    public const int DefaultMaximumTextLength = 48;
    public const int MaximumSupportedTextLength = 56;
    public const int CharacterWidth = 8;
    public const int TextRowHeight = 18;
    public const int DefaultRenderWidth = 48;
    public const int MaximumRenderWidth = 63;
    public const int MinimumSupportedCoordinate = 0;
    public const int MaximumSupportedCoordinate = 4095;

    private readonly char[] _textStorage;
    private readonly int _maximumTextLength;
    private int _textLength;
    private int _x;
    private int _y;
    private int _width;
    private bool _visible = true;
    private uint _rejectedInputCount;

    public GuideXosLabel(
        int x,
        int y,
        int width,
        string text = "",
        int maximumTextLength = DefaultMaximumTextLength)
    {
        if (maximumTextLength < 1 ||
            maximumTextLength > MaximumSupportedTextLength)
        {
            maximumTextLength = DefaultMaximumTextLength;
        }

        _maximumTextLength = maximumTextLength;
        _textStorage = new char[maximumTextLength];
        _x = MinimumSupportedCoordinate;
        _y = MinimumSupportedCoordinate;
        _width = CharacterWidth;
        TrySetBounds(x, y, width);
        SetText(text ?? string.Empty);
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => TextRowHeight;
    public int RenderWidth => _width / CharacterWidth;
    public int MaximumTextLength => _maximumTextLength;
    public int Length => _textLength;
    public string Text => new string(_textStorage, 0, _textLength);
    public bool Visible => _visible;
    public uint RejectedInputCount => _rejectedInputCount;

    /// <summary>
    /// Replaces the complete value only when every character is valid and it
    /// fits the fixed storage. Empty text is valid.
    /// </summary>
    public bool SetText(string text)
    {
        return text != null && SetText(text.AsSpan());
    }

    /// <summary>Bounded span overload for callers that already own the value.</summary>
    public bool SetText(ReadOnlySpan<char> text)
    {
        if (!IsValidText(text))
        {
            ++_rejectedInputCount;
            return false;
        }

        for (int index = 0; index < text.Length; index++)
        {
            _textStorage[index] = text[index];
        }
        _textLength = text.Length;
        return true;
    }

    public bool Clear()
    {
        return SetText(ReadOnlySpan<char>.Empty);
    }

    public void SetVisible(bool visible)
    {
        _visible = visible;
    }

    public bool TrySetBounds(int x, int y, int width)
    {
        if (x < MinimumSupportedCoordinate ||
            y < MinimumSupportedCoordinate ||
            width < CharacterWidth ||
            width > MaximumRenderWidth * CharacterWidth ||
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

    /// <summary>Restores visibility and rejection diagnostics, retaining text and bounds.</summary>
    public void Reset()
    {
        _visible = true;
        _rejectedInputCount = 0u;
    }

    /// <summary>
    /// Emits at most RenderWidth printable characters. Hidden and empty labels
    /// intentionally issue no text call; clipping never changes stored text.
    /// </summary>
    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!_visible || _textLength == 0) return GuideXosResult.Success;

        Span<byte> line = stackalloc byte[MaximumRenderWidth + 1];
        int renderLength = Math.Min(_textLength, RenderWidth);
        for (int index = 0; index < renderLength; index++)
        {
            line[index] = (byte)_textStorage[index];
        }
        return surface.TrySetText(_x, _y, line[..renderLength]);
    }

    private bool IsValidText(ReadOnlySpan<char> text)
    {
        if (text.Length > _maximumTextLength) return false;
        for (int index = 0; index < text.Length; index++)
        {
            if (text[index] < 0x20 || text[index] > 0x7E) return false;
        }
        return true;
    }
}
