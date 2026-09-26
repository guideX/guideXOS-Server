using System;

namespace HostLogProof;

/// <summary>Opaque managed handle for the current compositor-backed surface.</summary>
public sealed unsafe class GuideXosSurface
{
    internal readonly struct RenderState
    {
        public RenderState(
            bool clipEnabled, int clipX, int clipY, int clipWidth,
            int clipHeight, int translationX, int translationY)
        {
            ClipEnabled = clipEnabled;
            ClipX = clipX;
            ClipY = clipY;
            ClipWidth = clipWidth;
            ClipHeight = clipHeight;
            TranslationX = translationX;
            TranslationY = translationY;
        }

        public bool ClipEnabled { get; }
        public int ClipX { get; }
        public int ClipY { get; }
        public int ClipWidth { get; }
        public int ClipHeight { get; }
        public int TranslationX { get; }
        public int TranslationY { get; }
    }

    private readonly GuideXosHost _host;
    private bool _clipEnabled;
    private int _clipX;
    private int _clipY;
    private int _clipWidth;
    private int _clipHeight;
    private int _translationX;
    private int _translationY;

    internal GuideXosSurface(GuideXosHost host, ulong handle)
    {
        _host = host;
        Handle = handle;
    }

    public ulong Handle { get; }

    internal RenderState SaveRenderState()
    {
        return new RenderState(_clipEnabled, _clipX, _clipY, _clipWidth,
            _clipHeight, _translationX, _translationY);
    }

    internal void RestoreRenderState(RenderState state)
    {
        _clipEnabled = state.ClipEnabled;
        _clipX = state.ClipX;
        _clipY = state.ClipY;
        _clipWidth = state.ClipWidth;
        _clipHeight = state.ClipHeight;
        _translationX = state.TranslationX;
        _translationY = state.TranslationY;
    }

    internal void SetRenderClip(int x, int y, int width, int height)
    {
        _clipEnabled = width > 0 && height > 0;
        _clipX = x;
        _clipY = y;
        _clipWidth = width;
        _clipHeight = height;
    }

    internal void SetRenderTranslation(int x, int y)
    {
        _translationX = x;
        _translationY = y;
    }

    public GuideXosResult TrySetText(int x, int y, ReadOnlySpan<byte> text)
    {
        NativeHostCallTable* table = _host.HostTable;
        if (!_host.HasCapability(GuideXosCapability.Text) || table->drawText == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (x < 0 || y < 0 || text.Length > 63) return GuideXosResult.InvalidArgument;

        int renderX = x + _translationX;
        int renderY = y + _translationY;
        if (_clipEnabled)
        {
            int clipRight = _clipX + _clipWidth;
            int clipBottom = _clipY + _clipHeight;
            // The compositor's text primitive is an atomic 18-pixel row.  Do
            // not emit a row that would cross a vertical clip edge.  Horizontal
            // clipping remains exact at the eight-pixel glyph granularity.
            if (renderY < _clipY || renderY + 18 > clipBottom ||
                renderY >= clipBottom || renderX >= clipRight)
            {
                return GuideXosResult.Success;
            }

            int first = renderX < _clipX
                ? (_clipX - renderX + 7) / 8 : 0;
            int last = text.Length;
            if (renderX + last * 8 > clipRight)
            {
                last = Math.Max(first, (clipRight - renderX) / 8);
            }
            if (first >= last) return GuideXosResult.Success;
            renderX += first * 8;
            text = text[first..last];
        }

        Span<byte> buffer = stackalloc byte[text.Length + 1];
        text.CopyTo(buffer);
        buffer[text.Length] = 0;
        fixed (byte* pointer = buffer)
        {
            return table->drawText(_host.Context, Handle, renderX, renderY, pointer) == 0
                ? GuideXosResult.Success
                : GuideXosResult.InvalidArgument;
        }
    }

    public GuideXosResult TryFillRect(
        int x,
        int y,
        int width,
        int height,
        uint color)
    {
        NativeHostCallTable* table = _host.HostTable;
        if (!_host.HasCapability(GuideXosCapability.Primitive) || table->drawRect == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (x < 0 || y < 0 || width <= 0 || height <= 0)
        {
            return GuideXosResult.InvalidArgument;
        }

        int renderX = x + _translationX;
        int renderY = y + _translationY;
        int renderWidth = width;
        int renderHeight = height;
        if (_clipEnabled)
        {
            int clipRight = _clipX + _clipWidth;
            int clipBottom = _clipY + _clipHeight;
            int right = Math.Min(renderX + renderWidth, clipRight);
            int bottom = Math.Min(renderY + renderHeight, clipBottom);
            renderX = Math.Max(renderX, _clipX);
            renderY = Math.Max(renderY, _clipY);
            renderWidth = right - renderX;
            renderHeight = bottom - renderY;
            if (renderWidth <= 0 || renderHeight <= 0)
            {
                return GuideXosResult.Success;
            }
        }

        return table->drawRect(_host.Context, Handle, renderX, renderY,
                renderWidth, renderHeight, color) == 0
            ? GuideXosResult.Success
            : GuideXosResult.InvalidArgument;
    }

    public GuideXosResult TryAddButton(
        int x,
        int y,
        int width,
        int height,
        ReadOnlySpan<byte> label,
        uint actionId,
        out int widget)
    {
        widget = -1;
        NativeHostCallTable* table = _host.HostTable;
        if (!_host.HasCapability(GuideXosCapability.Action) ||
            table->addActionButton == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        if (actionId == 0u || actionId > GxAbi.LaunchFlagPayloadMask ||
            x < 0 || y < 0 || width <= 0 || height <= 0 || label.Length > 63)
        {
            return GuideXosResult.InvalidArgument;
        }

        Span<byte> buffer = stackalloc byte[label.Length + 1];
        label.CopyTo(buffer);
        buffer[label.Length] = 0;
        int widgetValue = -1;
        fixed (byte* pointer = buffer)
        {
            if (table->addActionButton(
                    _host.Context, Handle, x, y, width, height, pointer,
                    actionId, &widgetValue) != 0 || widgetValue < 0)
            {
                return GuideXosResult.InvalidAction;
            }
        }
        widget = widgetValue;
        return GuideXosResult.Success;
    }

    public GuideXosResult TryClose()
    {
        NativeHostCallTable* table = _host.HostTable;
        if (!_host.HasCapability(GuideXosCapability.Close) || table->closeWindow == null)
        {
            return GuideXosResult.CapabilityUnavailable;
        }
        return table->closeWindow(_host.Context, Handle) == 0
            ? GuideXosResult.Success
            : GuideXosResult.InvalidArgument;
    }
}
