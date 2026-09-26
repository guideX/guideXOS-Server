using System;

namespace HostLogProof;

/// <summary>
/// A bounded horizontal presentation-only divider. It owns only geometry and
/// visibility; applications own the meaning of the visual boundary.
/// </summary>
public sealed class GuideXosSeparator
{
    public const int CharacterWidth = 8;
    public const int TextRowHeight = 18;
    public const int MinimumSupportedWidth = CharacterWidth;
    public const int MaximumRenderWidth = 63;
    public const int MaximumSupportedWidth = MaximumRenderWidth * CharacterWidth;
    public const int MinimumSupportedCoordinate = 0;
    public const int MaximumSupportedCoordinate = 4095;
    public const byte SeparatorGlyph = (byte)'-';

    private int _x;
    private int _y;
    private int _width;
    private bool _visible = true;
    private bool _panelVisible = true;
    private GuideXosPanel _panelOwner;
    private GuideXosScrollView _scrollViewOwner;
    private uint _rejectedInputCount;

    public GuideXosSeparator(int x, int y, int width)
    {
        _x = MinimumSupportedCoordinate;
        _y = MinimumSupportedCoordinate;
        _width = MinimumSupportedWidth;
        TrySetBounds(x, y, width);
    }

    public int X => _x;
    public int Y => _y;
    public int Width => _width;
    public int Height => TextRowHeight;
    public int RenderWidth => _width / CharacterWidth;
    public bool Visible => _visible;
    public bool EffectiveVisible => _visible && _panelVisible;
    public GuideXosPanel ParentPanel => _panelOwner;
    public GuideXosScrollView ParentScrollView => _scrollViewOwner;
    public uint RejectedInputCount => _rejectedInputCount;

    public void SetVisible(bool visible)
    {
        _visible = visible;
    }

    public bool SetWidth(int width)
    {
        return TrySetBounds(_x, _y, width);
    }

    public bool TrySetBounds(int x, int y, int width)
    {
        if (_panelOwner != null || _scrollViewOwner != null)
        {
            ++_rejectedInputCount;
            return false;
        }
        return TrySetBoundsCore(x, y, width);
    }

    internal bool TrySetPanelBounds(int x, int y, int width)
    {
        return TrySetBoundsCore(x, y, width);
    }

    internal bool TrySetScrollViewBounds(int x, int y, int width)
    {
        return TrySetBoundsCore(x, y, width);
    }

    private bool TrySetBoundsCore(int x, int y, int width)
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

    /// <summary>Restores visibility and diagnostics while retaining geometry.</summary>
    public void Reset()
    {
        _visible = true;
        if (_panelOwner == null) _panelVisible = true;
        _rejectedInputCount = 0u;
    }

    /// <summary>
    /// Emits one deterministic horizontal line. The fixed stack buffer is
    /// bounded by the 63-column surface text contract; hidden separators emit
    /// no text call. The compositor redraws the complete frame before this
    /// call, so shortening the line cannot leave a stale trailing tail.
    /// </summary>
    public GuideXosResult Render(GuideXosSurface surface)
    {
        if (surface == null) return GuideXosResult.InvalidArgument;
        if (!EffectiveVisible) return GuideXosResult.Success;

        Span<byte> line = stackalloc byte[MaximumRenderWidth + 1];
        for (int index = 0; index < RenderWidth; index++)
        {
            line[index] = SeparatorGlyph;
        }
        return surface.TrySetText(_x, _y, line[..RenderWidth]);
    }

    internal bool TryAttachToPanel(GuideXosPanel panel)
    {
        if (panel == null || _panelOwner != null || _scrollViewOwner != null) return false;
        _panelOwner = panel;
        _panelVisible = panel.Visible;
        return true;
    }

    internal void SetPanelVisible(bool visible)
    {
        _panelVisible = visible;
    }

    internal void DetachFromPanel()
    {
        _panelOwner = null;
        _panelVisible = true;
    }

    internal bool TryAttachToScrollView(GuideXosScrollView scrollView)
    {
        if (scrollView == null || _panelOwner != null || _scrollViewOwner != null)
            return false;
        _scrollViewOwner = scrollView;
        return true;
    }

    internal void DetachFromScrollView()
    {
        _scrollViewOwner = null;
    }
}
